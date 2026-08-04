/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * see skia/src/opts/SkSwizzler_opts.h
 */

#pragma once

#include <cstdint>

// Converts RGBA pixels to BGRA, dispatching at runtime to the fastest
// implementation the current CPU supports.
//
// The implementations live in separate translation units on purpose: the AVX2
// kernel is the only one compiled with /arch:AVX2, so the rest of the plugin
// stays at the x64 baseline (SSE2). Compiling the whole target with /arch:AVX2
// lets MSVC emit VEX-encoded instructions anywhere -- including in load-time
// initializers -- which crashes the DLL at load with STATUS_ILLEGAL_INSTRUCTION
// on CPUs without AVX (Pentium Silver / Celeron N-series, Atom).
void RGBA_to_BGRA(uint32_t* dst, const uint32_t* src, int height,
                  int src_stride, int dst_stride);

// Baseline implementation, always available.
void RGBA_to_BGRA_portable(uint32_t* dst, const uint32_t* src, int height,
                           int src_stride, int dst_stride);

// Requires AVX2. Only call after verifying cpuid::cpuinfo::has_avx2().
// Defined in swizzle_avx2.cc, the only TU built with /arch:AVX2.
void RGBA_to_BGRA_AVX2(uint32_t* dst, const uint32_t* src, int height,
                       int src_stride, int dst_stride);
