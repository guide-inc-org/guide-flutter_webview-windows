/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * see skia/src/opts/SkSwizzler_opts.h
 */

#include "swizzle.h"

#include <algorithm>
#include <iostream>

#include "cpuid/cpuinfo.h"
#include "diag.h"

void RGBA_to_BGRA_portable(uint32_t* dst, const uint32_t* src, int height,
                           int src_stride, int dst_stride) {
  auto width = std::min<int>(src_stride, dst_stride);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t a = (src[x] >> 24) & 0xFF, b = (src[x] >> 16) & 0xFF,
              g = (src[x] >> 8) & 0xFF, r = (src[x] >> 0) & 0xFF;
      dst[x] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 |
               (uint32_t)b << 0;
    }

    src += src_stride;
    dst += dst_stride;
  }
}

namespace {

bool SelectAvx2Kernel() {
  const cpuid::cpuinfo info;
  const bool use_avx2 = info.has_avx2();

  if (diag::enabled()) {
    // has_avx()/has_avx2() account for both the CPUID feature bits and whether
    // the OS enabled AVX state (see cpuid/detail/init_msvc_x86.h), so a false
    // here on a CPU that clearly has AVX2 means the OS disabled it.
    std::cerr << "[webview_windows] swizzle kernel: "
              << (use_avx2 ? "AVX2" : "portable")
              << " (has_avx=" << info.has_avx()
              << " has_avx2=" << info.has_avx2()
              << " has_sse4_2=" << info.has_sse4_2() << ")" << std::endl;
  }

  return use_avx2;
}

}  // namespace

void RGBA_to_BGRA(uint32_t* dst, const uint32_t* src, int height,
                  int src_stride, int dst_stride) {
  // Resolved on first use, not at DLL load time.
  static const bool use_avx2 = SelectAvx2Kernel();

  if (use_avx2) {
    return RGBA_to_BGRA_AVX2(dst, src, height, src_stride, dst_stride);
  }

  RGBA_to_BGRA_portable(dst, src, height, src_stride, dst_stride);
}
