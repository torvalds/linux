//===-- LinuxPTraceDefines_arm64sve.h ------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPTRACEDEFINES_ARM64SVE_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPTRACEDEFINES_ARM64SVE_H

#include <cstdint>

namespace lldb_private {
namespace sve {

/*
 * The SVE architecture leaves space for future expansion of the
 * vector length beyond its initial architectural limit of 2048 bits
 * (16 quadwords).
 *
 * See <Linux kernel source tree>/Documentation/arm64/sve.rst for a description
 * of the vl/vq terminology.
 */

const uint16_t vq_bytes = 16; /* number of bytes per quadword */

const uint16_t vq_min = 1;
const uint16_t vq_max = 512;

const uint16_t vl_min = vq_min * vq_bytes;
const uint16_t vl_max = vq_max * vq_bytes;

const uint16_t num_of_zregs = 32;
const uint16_t num_of_pregs = 16;

inline uint16_t vl_valid(uint16_t vl) {
  return (vl % vq_bytes == 0 && vl >= vl_min && vl <= vl_max);
}

inline uint16_t vq_from_vl(uint16_t vl) { return vl / vq_bytes; }
inline uint16_t vl_from_vq(uint16_t vq) { return vq * vq_bytes; }

/* A new signal frame record sve_context encodes the SVE Registers on signal
 * delivery. sve_context struct definition may be included in asm/sigcontext.h.
 * We define sve_context_size which will be used by LLDB sve helper functions.
 * More information on sve_context can be found in Linux kernel source tree at
 * Documentation/arm64/sve.rst.
 */

const uint16_t sve_context_size = 16;

/*
 * If the SVE registers are currently live for the thread at signal delivery,
 * sve_context.head.size >=
 * SigContextSize(vq_from_vl(sve_context.vl))
 * and the register data may be accessed using the Sig*() functions.
 *
 * If sve_context.head.size <
 * SigContextSize(vq_from_vl(sve_context.vl)),
 * the SVE registers were not live for the thread and no register data
 * is included: in this case, the Sig*() functions should not be
 * used except for this check.
 *
 * The same convention applies when returning from a signal: a caller
 * will need to remove or resize the sve_context block if it wants to
 * make the SVE registers live when they were previously non-live or
 * vice-versa.  This may require the caller to allocate fresh
 * memory and/or move other context blocks in the signal frame.
 *
 * Changing the vector length during signal return is not permitted:
 * sve_context.vl must equal the thread's current vector length when
 * doing a sigreturn.
 *
 *
 * Note: for all these functions, the "vq" argument denotes the SVE
 * vector length in quadwords (i.e., units of 128 bits).
 *
 * The correct way to obtain vq is to use vq_from_vl(vl).  The
 * result is valid if and only if vl_valid(vl) is true.  This is
 * guaranteed for a struct sve_context written by the kernel.
 *
 *
 * Additional functions describe the contents and layout of the payload.
 * For each, Sig*Offset(args) is the start offset relative to
 * the start of struct sve_context, and Sig*Size(args) is the
 * size in bytes:
 *
 *	x	type				description
 *	-	----				-----------
 *	REGS					the entire SVE context
 *
 *	ZREGS	__uint128_t[num_of_zregs][vq]	all Z-registers
 *	ZREG	__uint128_t[vq]			individual Z-register Zn
 *
 *	PREGS	uint16_t[num_of_pregs][vq]	all P-registers
 *	PREG	uint16_t[vq]			individual P-register Pn
 *
 *	FFR	uint16_t[vq]			first-fault status register
 *
 * Additional data might be appended in the future.
 */

inline uint16_t SigZRegSize(uint16_t vq) { return vq * vq_bytes; }
inline uint16_t SigPRegSize(uint16_t vq) { return vq * vq_bytes / 8; }
inline uint16_t SigFFRSize(uint16_t vq) { return SigPRegSize(vq); }

inline uint32_t SigRegsOffset() {
  return (sve_context_size + vq_bytes - 1) / vq_bytes * vq_bytes;
}

inline uint32_t SigZRegsOffset() { return SigRegsOffset(); }

inline uint32_t SigZRegOffset(uint16_t vq, uint16_t n) {
  return SigRegsOffset() + SigZRegSize(vq) * n;
}

inline uint32_t SigZRegsSize(uint16_t vq) {
  return SigZRegOffset(vq, num_of_zregs) - SigRegsOffset();
}

inline uint32_t SigPRegsOffset(uint16_t vq) {
  return SigRegsOffset() + SigZRegsSize(vq);
}

inline uint32_t SigPRegOffset(uint16_t vq, uint16_t n) {
  return SigPRegsOffset(vq) + SigPRegSize(vq) * n;
}

inline uint32_t SigpRegsSize(uint16_t vq) {
  return SigPRegOffset(vq, num_of_pregs) - SigPRegsOffset(vq);
}

inline uint32_t SigFFROffset(uint16_t vq) {
  return SigPRegsOffset(vq) + SigpRegsSize(vq);
}

inline uint32_t SigRegsSize(uint16_t vq) {
  return SigFFROffset(vq) + SigFFRSize(vq) - SigRegsOffset();
}

inline uint32_t SVESigContextSize(uint16_t vq) {
  return SigRegsOffset() + SigRegsSize(vq);
}

struct user_sve_header {
  uint32_t size;     /* total meaningful regset content in bytes */
  uint32_t max_size; /* maxmium possible size for this thread */
  uint16_t vl;       /* current vector length */
  uint16_t max_vl;   /* maximum possible vector length */
  uint16_t flags;
  uint16_t reserved;
};

using user_za_header = user_sve_header;

/* Definitions for user_sve_header.flags: */
const uint16_t ptrace_regs_mask = 1 << 0;
const uint16_t ptrace_regs_fpsimd = 0;
const uint16_t ptrace_regs_sve = ptrace_regs_mask;

/*
 * The remainder of the SVE state follows struct user_sve_header.  The
 * total size of the SVE state (including header) depends on the
 * metadata in the header:  PTraceSize(vq, flags) gives the total size
 * of the state in bytes, including the header.
 *
 * Refer to <asm/sigcontext.h> for details of how to pass the correct
 * "vq" argument to these macros.
 */

/* Offset from the start of struct user_sve_header to the register data */
inline uint16_t PTraceRegsOffset() {
  return (sizeof(struct user_sve_header) + vq_bytes - 1) / vq_bytes * vq_bytes;
}

/*
 * The register data content and layout depends on the value of the
 * flags field.
 */

/*
 * (flags & ptrace_regs_mask) == ptrace_regs_fpsimd case:
 *
 * The payload starts at offset PTraceFPSIMDOffset, and is of type
 * struct user_fpsimd_state.  Additional data might be appended in the
 * future: use PTraceFPSIMDSize(vq, flags) to compute the total size.
 * PTraceFPSIMDSize(vq, flags) will never be less than
 * sizeof(struct user_fpsimd_state).
 */

const uint32_t ptrace_fpsimd_offset = PTraceRegsOffset();

/* Return size of struct user_fpsimd_state from asm/ptrace.h */
inline uint32_t PTraceFPSIMDSize(uint16_t vq, uint16_t flags) { return 528; }

/*
 * (flags & ptrace_regs_mask) == ptrace_regs_sve case:
 *
 * The payload starts at offset PTraceSVEOffset, and is of size
 * PTraceSVESize(vq, flags).
 *
 * Additional functions describe the contents and layout of the payload.
 * For each, PTrace*X*Offset(args) is the start offset relative to
 * the start of struct user_sve_header, and PTrace*X*Size(args) is
 * the size in bytes:
 *
 *	x	type				description
 *	-	----				-----------
 *	ZREGS		\
 *	ZREG		|
 *	PREGS		| refer to <asm/sigcontext.h>
 *	PREG		|
 *	FFR		/
 *
 *	FPSR	uint32_t			FPSR
 *	FPCR	uint32_t			FPCR
 *
 * Additional data might be appended in the future.
 */

inline uint32_t PTraceZRegSize(uint16_t vq) { return SigZRegSize(vq); }

inline uint32_t PTracePRegSize(uint16_t vq) { return SigPRegSize(vq); }

inline uint32_t PTraceFFRSize(uint16_t vq) { return SigFFRSize(vq); }

const uint32_t fpsr_size = sizeof(uint32_t);
const uint32_t fpcr_size = sizeof(uint32_t);

inline uint32_t SigToPTrace(uint32_t offset) {
  return offset - SigRegsOffset() + PTraceRegsOffset();
}

const uint32_t ptrace_sve_offset = PTraceRegsOffset();

inline uint32_t PTraceZRegsOffset(uint16_t vq) {
  return SigToPTrace(SigZRegsOffset());
}

inline uint32_t PTraceZRegOffset(uint16_t vq, uint16_t n) {
  return SigToPTrace(SigZRegOffset(vq, n));
}

inline uint32_t PTraceZRegsSize(uint16_t vq) {
  return PTraceZRegOffset(vq, num_of_zregs) - SigToPTrace(SigRegsOffset());
}

inline uint32_t PTracePRegsOffset(uint16_t vq) {
  return SigToPTrace(SigPRegsOffset(vq));
}

inline uint32_t PTracePRegOffset(uint16_t vq, uint16_t n) {
  return SigToPTrace(SigPRegOffset(vq, n));
}

inline uint32_t PTracePRegsSize(uint16_t vq) {
  return PTracePRegOffset(vq, num_of_pregs) - PTracePRegsOffset(vq);
}

inline uint32_t PTraceFFROffset(uint16_t vq) {
  return SigToPTrace(SigFFROffset(vq));
}

inline uint32_t PTraceFPSROffset(uint16_t vq) {
  return (PTraceFFROffset(vq) + PTraceFFRSize(vq) + (vq_bytes - 1)) / vq_bytes *
         vq_bytes;
}

inline uint32_t PTraceFPCROffset(uint16_t vq) {
  return PTraceFPSROffset(vq) + fpsr_size;
}

/*
 * Any future extension appended after FPCR must be aligned to the next
 * 128-bit boundary.
 */

inline uint32_t PTraceSVESize(uint16_t vq, uint16_t flags) {
  return (PTraceFPCROffset(vq) + fpcr_size - ptrace_sve_offset + vq_bytes - 1) /
         vq_bytes * vq_bytes;
}

inline uint32_t PTraceSize(uint16_t vq, uint16_t flags) {
  return (flags & ptrace_regs_mask) == ptrace_regs_sve
             ? ptrace_sve_offset + PTraceSVESize(vq, flags)
             : ptrace_fpsimd_offset + PTraceFPSIMDSize(vq, flags);
}

} // namespace SVE
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LINUXPTRACEDEFINES_ARM64SVE_H
