#include "sanitizer_common/sanitizer_asm.h"

#define CFA_OFFSET 160
#define R2_REL_OFFSET 16
#define R3_REL_OFFSET 24
#define R14_REL_OFFSET 112
#define R15_REL_OFFSET 120
#define FRAME_SIZE 160

.text

ASM_HIDDEN(__tsan_setjmp)

.macro intercept symbol, real
.comm \real, 8, 8
.globl ASM_SYMBOL_INTERCEPTOR(\symbol)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(\symbol))
ASM_SYMBOL_INTERCEPTOR(\symbol):
  CFI_STARTPROC
  stmg %r2, %r3, R2_REL_OFFSET(%r15)
  CFI_REL_OFFSET(%r2, R2_REL_OFFSET)
  CFI_REL_OFFSET(%r3, R3_REL_OFFSET)
  stmg %r14, %r15, R14_REL_OFFSET(%r15)
  CFI_REL_OFFSET(%r14, R14_REL_OFFSET)
  CFI_REL_OFFSET(%r15, R15_REL_OFFSET)
  aghi %r15, -FRAME_SIZE
  CFI_ADJUST_CFA_OFFSET(FRAME_SIZE)
  la %r2, FRAME_SIZE(%r15)
  brasl %r14, ASM_SYMBOL(__tsan_setjmp)
  lmg %r14, %r15, FRAME_SIZE + R14_REL_OFFSET(%r15)
  CFI_RESTORE(%r14)
  CFI_RESTORE(%r15)
  CFI_DEF_CFA_OFFSET(CFA_OFFSET)
  lmg %r2, %r3, R2_REL_OFFSET(%r15)
  CFI_RESTORE(%r2)
  CFI_RESTORE(%r3)
  larl %r1, \real
  lg %r1, 0(%r1)
  br %r1
  CFI_ENDPROC
  ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(\symbol))
.endm

intercept setjmp, _ZN14__interception11real_setjmpE
intercept _setjmp, _ZN14__interception12real__setjmpE
intercept sigsetjmp, _ZN14__interception14real_sigsetjmpE
intercept __sigsetjmp, _ZN14__interception16real___sigsetjmpE

NO_EXEC_STACK_DIRECTIVE
