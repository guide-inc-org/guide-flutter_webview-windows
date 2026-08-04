/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * see skia/src/opts/SkSwizzler_opts.h
 *
 * The swizzle uses SSSE3, which MSVC accepts at the default x64 baseline and
 * every x64 CPU since 2006 supports. Do NOT reach for /arch:AVX2: it raises the
 * baseline for the whole target rather than for this kernel, so the compiler
 * emits VEX-encoded instructions into unrelated code, and the plugin DLL then
 * faults with STATUS_ILLEGAL_INSTRUCTION (0xC000001D) at load time on CPUs
 * without AVX -- Pentium Silver / Celeron / Atom parts such as the Jasper Lake
 * N6005. AVX2 is not worth that risk here either way: this copy is bandwidth
 * bound, and a 256-bit kernel measured only 2-7% faster than the 128-bit one
 * (both ~3-4x faster than the scalar loop).
 */

#pragma once

#include <tmmintrin.h>  // SSSE3 (_mm_shuffle_epi8) -- no /arch flag required.

#include <algorithm>
#include <cstdint>

#include "cpuid/cpuinfo.h"

#if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F__)
#error "This target must be compiled at the SSE2 baseline: a target-wide \
/arch:AVX* flag lets the compiler emit AVX instructions throughout the plugin, \
crashing at startup on CPUs without AVX. See the comment above."
#endif

inline void RGBA_to_BGRA_portable(uint32_t* dst, const uint32_t* src,
                                  int height, int src_stride, int dst_stride) {
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

inline void RGBA_to_BGRA_SSSE3(uint32_t* dst, const uint32_t* src, int height,
                               int src_stride, int dst_stride) {
  const __m128i swapRB =
      _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);

  auto width = std::min<int>(src_stride, dst_stride);

  for (int y = 0; y < height; y++) {
    auto cw = width;
    auto rptr = src;
    auto dptr = dst;
    while (cw >= 4) {
      __m128i rgba = _mm_loadu_si128((const __m128i*)rptr);
      __m128i bgra = _mm_shuffle_epi8(rgba, swapRB);
      _mm_storeu_si128((__m128i*)dptr, bgra);

      rptr += 4;
      dptr += 4;
      cw -= 4;
    }

    for (auto x = 0; x < cw; x++) {
      uint8_t a = (rptr[x] >> 24) & 0xFF, b = (rptr[x] >> 16) & 0xFF,
              g = (rptr[x] >> 8) & 0xFF, r = (rptr[x] >> 0) & 0xFF;
      dptr[x] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 |
                (uint32_t)b << 0;
    }

    src += src_stride;
    dst += dst_stride;
  }
}

inline void RGBA_to_BGRA(uint32_t* dst, const uint32_t* src, int height,
                         int src_stride, int dst_stride) {
  // Detected once, on first use; initialization of a function-local static is
  // thread safe. Pre-SSSE3 x64 parts (AMD K8/K10) fall back to the scalar loop.
  static const bool has_ssse3 = cpuid::cpuinfo().has_ssse3();

  if (has_ssse3) {
    return RGBA_to_BGRA_SSSE3(dst, src, height, src_stride, dst_stride);
  }
  return RGBA_to_BGRA_portable(dst, src, height, src_stride, dst_stride);
}
