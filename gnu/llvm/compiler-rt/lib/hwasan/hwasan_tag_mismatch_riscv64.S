#include "sanitizer_common/sanitizer_asm.h"

// The content of this file is RISCV64-only:
#if defined(__riscv) && (__riscv_xlen == 64)

// The responsibility of the HWASan entry point in compiler-rt is to primarily
// readjust the stack from the callee and save the current register values to
// the stack.
// This entry point function should be called from a __hwasan_check_* symbol.
// These are generated during a lowering pass in the backend, and are found in
// RISCVAsmPrinter::EmitHwasanMemaccessSymbols(). Please look there for
// further information.
// The __hwasan_check_* caller of this function should have expanded the stack
// and saved the previous values of x10(arg0), x11(arg1), x1(ra), and x8(fp).
// This function will "consume" these saved values and treats it as part of its
// own stack frame. In this sense, the __hwasan_check_* callee and this function
// "share" a stack frame. This allows us to omit having unwinding information
// (.cfi_*) present in every __hwasan_check_* function, therefore reducing binary size.
// This is particularly important as hwasan_check_* instances are duplicated in every
// translation unit where HWASan is enabled.
// This function calls HwasanTagMismatch to step back into the C++ code that
// completes the stack unwinding and error printing. This function is is not
// permitted to return.


// |              ...                |
// |              ...                |
// | Previous stack frames...        |
// +=================================+
// |              ...                |
// |                                 |
// | Stack frame space for x12 - x31.|
// |                                 |
// |              ...                |
// +---------------------------------+ <-- [SP + 96]
// | Saved x11(arg1), as             |
// | __hwasan_check_* clobbers it.   |
// +---------------------------------+ <-- [SP + 88]
// | Saved x10(arg0), as             |
// | __hwasan_check_* clobbers it.   |
// +---------------------------------+ <-- [SP + 80]
// |                                 |
// | Stack frame space for x9.       |
// +---------------------------------+ <-- [SP + 72]
// |                                 |
// | Saved x8(fp), as                |
// | __hwasan_check_* clobbers it.   |
// +---------------------------------+ <-- [SP + 64]
// |              ...                |
// |                                 |
// | Stack frame space for x2 - x7.  |
// |                                 |
// |              ...                |
// +---------------------------------+ <-- [SP + 16]
// | Return address (x1) for caller  |
// | of __hwasan_check_*.            |
// +---------------------------------+ <-- [SP + 8]
// | Reserved place for x0, possibly |
// | junk, since we don't save it.   |
// +---------------------------------+ <-- [x2 / SP]

// This function takes two arguments:
//   * x10/a0: The data address.
//   * x11/a1: The encoded access info for the failing access.

.section .text
.file "hwasan_tag_mismatch_riscv64.S"

.global __hwasan_tag_mismatch_v2
ASM_TYPE_FUNCTION(__hwasan_tag_mismatch_v2)
__hwasan_tag_mismatch_v2:
  CFI_STARTPROC

  // Set the CFA to be the return address for caller of __hwasan_check_*. Note
  // that we do not emit CFI predicates to describe the contents of this stack
  // frame, as this proxy entry point should never be debugged. The contents
  // are static and are handled by the unwinder after calling
  // __hwasan_tag_mismatch. The frame pointer is already correctly setup
  // by __hwasan_check_*.
  addi fp, sp, 256
  CFI_DEF_CFA(fp, 0)
  CFI_OFFSET(ra, -248)
  CFI_OFFSET(fp, -192)

  // Save the rest of the registers into the preallocated space left by
  // __hwasan_check.
  sd x31, 248(sp)
  sd x30, 240(sp)
  sd x29, 232(sp)
  sd x28, 224(sp)
  sd x27, 216(sp)
  sd x26, 208(sp)
  sd x25, 200(sp)
  sd x24, 192(sp)
  sd x23, 184(sp)
  sd x22, 176(sp)
  sd x21, 168(sp)
  sd x20, 160(sp)
  sd x19, 152(sp)
  sd x18, 144(sp)
  sd x17, 136(sp)
  sd x16, 128(sp)
  sd x15, 120(sp)
  sd x14, 112(sp)
  sd x13, 104(sp)
  sd x12, 96(sp)
  // sd x11, 88(sp) ; already saved
  // sd x10, 80(sp) ; already saved
  sd x9, 72(sp)
  // sd x8, 64(sp) ; already saved
  sd x7, 56(sp)
  sd x6, 48(sp)
  sd x5, 40(sp)
  sd x4, 32(sp)
  sd x3, 24(sp)
  sd x2, 16(sp)
  // sd x1, 8(sp) ; already saved
  // sd x0, 0(sp) ; don't store zero register

  // Pass the address of the frame to __hwasan_tag_mismatch4, so that it can
  // extract the saved registers from this frame without having to worry about
  // finding this frame.
  mv x12, sp

  call __hwasan_tag_mismatch4
  CFI_ENDPROC
ASM_SIZE(__hwasan_tag_mismatch_v2)

#endif  // defined(__riscv) && (__riscv_xlen == 64)

// We do not need executable stack.
NO_EXEC_STACK_DIRECTIVE
