#include "sanitizer_common/sanitizer_asm.h"

.section .text

.comm _ZN14__interception11real_setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(setjmp))
ASM_SYMBOL_INTERCEPTOR(setjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi sp, sp, -32
  sd ra, 24(sp)
  sd s0, 16(sp)
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (8, -16)

  // Adjust the SP for previous frame
  addi s0, sp, 32
  CFI_DEF_CFA_REGISTER (8)

  // Save env parameter
  sd a0, 8(sp)
  CFI_OFFSET (10, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi  a0, s0, 0

  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld a0, 8(sp)
  CFI_RESTORE (10)

  // Restore frame/link register
  ld s0, 16(sp)
  ld ra, 24(sp)
  addi sp, sp, 32
  CFI_RESTORE (8)
  CFI_RESTORE (1)
  CFI_DEF_CFA (2, 0)

  // tail jump to libc setjmp
  la t1, _ZN14__interception11real_setjmpE
  ld t1, 0(t1)
  jr t1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(setjmp))

.comm _ZN14__interception12real__setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(_setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(_setjmp))
ASM_SYMBOL_INTERCEPTOR(_setjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi sp, sp, -32
  sd ra, 24(sp)
  sd s0, 16(sp)
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (8, -16)

  // Adjust the SP for previous frame
  addi s0, sp, 32
  CFI_DEF_CFA_REGISTER (8)

  // Save env parameter
  sd a0, 8(sp)
  CFI_OFFSET (10, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi  a0, s0, 0

  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld a0, 8(sp)
  CFI_RESTORE (10)

  // Restore frame/link register
  ld s0, 16(sp)
  ld ra, 24(sp)
  addi sp, sp, 32
  CFI_RESTORE (8)
  CFI_RESTORE (1)
  CFI_DEF_CFA (2, 0)

  // tail jump to libc setjmp
  la t1, _ZN14__interception12real__setjmpE
  ld t1, 0(t1)
  jr t1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(_setjmp))

.comm _ZN14__interception14real_sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(sigsetjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi sp, sp, -32
  sd ra, 24(sp)
  sd s0, 16(sp)
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (8, -16)

  // Adjust the SP for previous frame
  addi s0, sp, 32
  CFI_DEF_CFA_REGISTER (8)

  // Save env parameter
  sd a0, 8(sp)
  sd a1, 0(sp)
  CFI_OFFSET (10, -24)
  CFI_OFFSET (11, -32)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi  a0, s0, 0

  // call tsan interceptor
  call      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld a0, 8(sp)
  ld a1, 0(sp)
  CFI_RESTORE (10)
  CFI_RESTORE (11)

  // Restore frame/link register
  ld s0, 16(sp)
  ld ra, 24(sp)
  addi sp, sp, 32
  CFI_RESTORE (8)
  CFI_RESTORE (1)
  CFI_DEF_CFA (2, 0)

  // tail jump to libc setjmp
  la t1, _ZN14__interception14real_sigsetjmpE
  ld t1, 0(t1)
  jr t1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))

.comm _ZN14__interception16real___sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi sp, sp, -32
  sd ra, 24(sp)
  sd s0, 16(sp)
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (8, -16)

  // Adjust the SP for previous frame
  addi s0, sp, 32
  CFI_DEF_CFA_REGISTER (8)

  // Save env parameter
  sd a0, 8(sp)
  sd a1, 0(sp)
  CFI_OFFSET (10, -24)
  CFI_OFFSET (11, -32)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi  a0, s0, 0

  // call tsan interceptor
  call      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld a0, 8(sp)
  ld a1, 0(sp)
  CFI_RESTORE (10)
  CFI_RESTORE (11)

  // Restore frame/link register
  ld s0, 16(sp)
  ld ra, 24(sp)
  addi sp, sp, 32
  CFI_RESTORE (8)
  CFI_RESTORE (1)
  CFI_DEF_CFA (2, 0)

  // tail jump to libc setjmp
  la t1, _ZN14__interception16real___sigsetjmpE
  ld t1, 0(t1)
  jr t1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
