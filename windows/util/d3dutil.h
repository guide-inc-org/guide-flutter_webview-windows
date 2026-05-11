#pragma once

#include <D3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

inline auto CreateD3DDevice(D3D_DRIVER_TYPE const type,
                            winrt::com_ptr<ID3D11Device>& device) {
  WINRT_ASSERT(!device);

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  // Patched: VIDEO_SUPPORT is unsupported by WARP on some Windows
  // versions (D3D11CreateDevice fails with DXGI_ERROR_UNSUPPORTED).
  // Only request it when running on a hardware adapter.
  if (type == D3D_DRIVER_TYPE_HARDWARE) {
    flags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
  }

  //#ifdef _DEBUG
  //	flags |= D3D11_CREATE_DEVICE_DEBUG;
  //#endif

  return D3D11CreateDevice(nullptr, type, nullptr, flags, nullptr, 0,
                           D3D11_SDK_VERSION, device.put(), nullptr, nullptr);
}

inline auto CreateD3DDevice() {
  // Patched: ALWAYS use WARP (CPU-based D3D11). The capture pipeline
  // must stay decoupled from the user's GPU state — when the user
  // enables/disables the GPU at runtime (Device Manager, sleep/wake on
  // certain machines), a hardware D3D11 device is removed and WebView2's
  // visual goes stale. WARP is unaffected because it runs on the CPU.
  //
  // Performance trade-off is acceptable for a 2D chart (well within
  // CPU rasterization capacity on any modern machine).
  winrt::com_ptr<ID3D11Device> device;
  CreateD3DDevice(D3D_DRIVER_TYPE_WARP, device);
  return device;
}
