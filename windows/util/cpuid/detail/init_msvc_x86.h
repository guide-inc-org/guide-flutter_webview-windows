#pragma once

#include <intrin.h>

#include <cstdint>

#include "cpuinfo_impl.h"
#include "extract_x86_flags.h"

namespace cpuid {

void init_cpuinfo(cpuinfo::impl& info) {
  int registers[4];

  // The register information per input can be extracted from here:
  // http://en.wikipedia.org/wiki/CPUID
  //
  // CPUID should be called with EAX=0 first, as this will return the
  // maximum supported EAX input value for future calls
  __cpuid(registers, 0);
  uint32_t maximum_eax = registers[0];

  // Whether the OS has actually enabled the extended register state that
  // AVX / AVX-512 instructions need. See below.
  bool os_enabled_avx = false;
  bool os_enabled_avx512 = false;

  // Set registers for basic flag extraction, eax=1
  // All CPUs should support index=1
  if (maximum_eax >= 1U) {
    __cpuid(registers, 1);
    extract_x86_flags(info, registers[2], registers[3]);

    // The CPUID feature bits only say what the CPU implements. Executing a
    // VEX-encoded instruction additionally requires the OS to have enabled
    // XSAVE and the AVX (YMM) state in XCR0 -- otherwise the instruction
    // faults with STATUS_ILLEGAL_INSTRUCTION (0xC000001D) even though the
    // CPUID bit reads as set. That happens on Windows booted with
    // `bcdedit /set xsavedisable 1`, and under hypervisors that mask the
    // state out. Checking OSXSAVE + XGETBV is the documented way to detect
    // *usable* AVX support, so do it here: every consumer of has_avx() /
    // has_avx2() is deciding whether it may run AVX code.
    const bool has_osxsave =
        (static_cast<uint32_t>(registers[2]) & (1U << 27)) != 0;
    if (has_osxsave) {
      // _XCR_XFEATURE_ENABLED_MASK == 0
      const uint64_t xcr0 = _xgetbv(0);
      // Bit 1: SSE (XMM) state, bit 2: AVX (YMM) state.
      os_enabled_avx = (xcr0 & 0x6U) == 0x6U;
      // Bits 5-7: opmask, ZMM_Hi256 and Hi16_ZMM state.
      os_enabled_avx512 = os_enabled_avx && (xcr0 & 0xE0U) == 0xE0U;
    }
  }

  // Set registers for extended flags extraction, eax=7 and ecx=0
  // This operation is not supported on older CPUs, so it should be skipped
  // to avoid incorrect results
  if (maximum_eax >= 7U) {
    __cpuidex(registers, 7, 0);
    extract_x86_extended_flags(info, registers[1], registers[2], registers[3]);
  }

  // Report AVX / AVX-512 as unavailable unless the OS enabled the state they
  // need. Done after extraction so it overrides both flag sets.
  if (!os_enabled_avx) {
    info.m_has_avx = false;
    info.m_has_avx2 = false;
  }

  if (!os_enabled_avx512) {
    info.m_has_avx512_f = false;
    info.m_has_avx512_dq = false;
    info.m_has_avx512_ifma = false;
    info.m_has_avx512_pf = false;
    info.m_has_avx512_er = false;
    info.m_has_avx512_cd = false;
    info.m_has_avx512_bw = false;
    info.m_has_avx512_vl = false;
    info.m_has_avx512_vbmi = false;
    info.m_has_avx512_vbmi2 = false;
    info.m_has_avx512_vnni = false;
    info.m_has_avx512_bitalg = false;
    info.m_has_avx512_vpopcntdq = false;
    info.m_has_avx512_4vnniw = false;
    info.m_has_avx512_4fmaps = false;
    info.m_has_avx512_vp2intersect = false;
  }
}
}  // namespace cpuid
