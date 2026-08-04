#include "texture_bridge.h"

#include <windows.foundation.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>

#include "util/diag.h"
#include "util/direct3d11.interop.h"

namespace {
const int kNumBuffers = 1;

// TEMPORARY DIAGNOSTICS -- see util/diag.h.
bool ShouldLog(unsigned int& counter) {
  ++counter;
  return counter <= 3u || (counter % 300u) == 0u;
}
}  // namespace

TextureBridge::TextureBridge(GraphicsContext* graphics_context,
                             ABI::Windows::UI::Composition::IVisual* visual)
    : graphics_context_(graphics_context) {
  capture_item_ =
      graphics_context_->CreateGraphicsCaptureItemFromVisual(visual);
  assert(capture_item_);

  capture_item_->add_Closed(
      Microsoft::WRL::Callback<ABI::Windows::Foundation::ITypedEventHandler<
          ABI::Windows::Graphics::Capture::GraphicsCaptureItem*,
          IInspectable*>>(
          [](ABI::Windows::Graphics::Capture::IGraphicsCaptureItem* item,
             IInspectable* args) -> HRESULT {
            std::cerr << "Capture item was closed." << std::endl;
            return S_OK;
          })
          .Get(),
      &on_closed_token_);
}

TextureBridge::~TextureBridge() {
  const std::lock_guard<std::mutex> lock(mutex_);
  StopInternal();
  if (capture_item_) {
    capture_item_->remove_Closed(on_closed_token_);
  }
}

bool TextureBridge::Start() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (is_running_ || !capture_item_) {
    return false;
  }

  ABI::Windows::Graphics::SizeInt32 size;
  capture_item_->get_Size(&size);

  frame_pool_ = graphics_context_->CreateCaptureFramePool(
      graphics_context_->device(),
      static_cast<ABI::Windows::Graphics::DirectX::DirectXPixelFormat>(
          kPixelFormat),
      kNumBuffers, size);
  // Patched (Mức A): CreateCaptureFramePool can return null when the
  // underlying D3D11 device is unusable (e.g. user disabled the GPU in
  // Device Manager or device was removed). Fail gracefully instead of
  // asserting and popping a Visual C++ Runtime crash dialog.
  if (!frame_pool_) {
    std::cerr << "Failed to create capture frame pool (D3D11 device lost?)"
              << std::endl;
    return false;
  }

  frame_pool_->add_FrameArrived(
      Microsoft::WRL::Callback<ABI::Windows::Foundation::ITypedEventHandler<
          ABI::Windows::Graphics::Capture::Direct3D11CaptureFramePool*,
          IInspectable*>>(
          [this](ABI::Windows::Graphics::Capture::IDirect3D11CaptureFramePool*
                     pool,
                 IInspectable* args) -> HRESULT {
            OnFrameArrived();
            return S_OK;
          })
          .Get(),
      &on_frame_arrived_token_);

  if (FAILED(frame_pool_->CreateCaptureSession(capture_item_.get(),
                                               capture_session_.put()))) {
    std::cerr << "Creating capture session failed." << std::endl;
    return false;
  }

  if (SUCCEEDED(capture_session_->StartCapture())) {
    is_running_ = true;
    if (diag::enabled()) {
      std::cerr << "[webview_windows] capture started, item size " << size.Width
                << "x" << size.Height << std::endl;
    }
    return true;
  }

  if (diag::enabled()) {
    std::cerr << "[webview_windows] StartCapture failed" << std::endl;
  }
  return false;
}

void TextureBridge::Stop() {
  const std::lock_guard<std::mutex> lock(mutex_);
  StopInternal();
}

void TextureBridge::StopInternal() {
  if (is_running_) {
    is_running_ = false;
    // Patched (Mức A): frame_pool_ may be null if Start() failed midway.
    if (frame_pool_) {
      frame_pool_->remove_FrameArrived(on_frame_arrived_token_);
    }
    if (capture_session_) {
      auto closable =
          capture_session_.try_as<ABI::Windows::Foundation::IClosable>();
      if (closable) {
        closable->Close();
      }
      capture_session_ = nullptr;
    }
  }
}

void TextureBridge::OnFrameArrived() {
  const std::lock_guard<std::mutex> lock(mutex_);
  // Patched (Mức A): frame_pool_ can be null after a failed Start.
  if (!is_running_ || !frame_pool_) {
    return;
  }

  bool has_frame = false;

  winrt::com_ptr<ABI::Windows::Graphics::Capture::IDirect3D11CaptureFrame>
      frame;
  auto hr = frame_pool_->TryGetNextFrame(frame.put());
  if (SUCCEEDED(hr) && frame) {
    winrt::com_ptr<
        ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>
        frame_surface;

    if (SUCCEEDED(frame->get_Surface(frame_surface.put()))) {
      last_frame_ =
          util::TryGetDXGIInterfaceFromObject<ID3D11Texture2D>(frame_surface);
      has_frame = !ShouldDropFrame();
    }
  }

  // Patched (Mức A): also guard the Recreate call.
  if (needs_update_ && frame_pool_) {
    ABI::Windows::Graphics::SizeInt32 size;
    capture_item_->get_Size(&size);
    frame_pool_->Recreate(
        graphics_context_->device(),
        static_cast<ABI::Windows::Graphics::DirectX::DirectXPixelFormat>(
            kPixelFormat),
        kNumBuffers, size);
    needs_update_ = false;
  }

  if (diag::enabled()) {
    static unsigned int arrived_counter = 0;
    if (ShouldLog(arrived_counter)) {
      std::cerr << "[webview_windows] OnFrameArrived #" << arrived_counter
                << " hr=0x" << std::hex << hr << std::dec
                << " frame=" << (frame ? "yes" : "no")
                << " last_frame=" << (last_frame_ ? "yes" : "no")
                << " has_frame=" << has_frame
                << " notify=" << (frame_available_ ? "yes" : "no") << std::endl;
    }
  }

  if (has_frame && frame_available_) {
    frame_available_();
  }
}

bool TextureBridge::ShouldDropFrame() {
  if (!frame_duration_.has_value()) {
    return false;
  }
  auto now = std::chrono::high_resolution_clock::now();

  bool should_drop_frame = false;
  if (last_frame_timestamp_.has_value()) {
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_frame_timestamp_.value());
    should_drop_frame = diff < frame_duration_.value();
  }

  if (!should_drop_frame) {
    last_frame_timestamp_ = now;
  }
  return should_drop_frame;
}

void TextureBridge::NotifySurfaceSizeChanged() {
  const std::lock_guard<std::mutex> lock(mutex_);
  needs_update_ = true;
}

void TextureBridge::SetFpsLimit(std::optional<int> max_fps) {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto value = max_fps.value_or(0);
  if (value != 0) {
    frame_duration_ = FrameDuration(1000.0 / value);
  } else {
    frame_duration_.reset();
    last_frame_timestamp_.reset();
  }
}
