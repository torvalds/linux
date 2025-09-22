#include "sanitizer_common/sanitizer_asm.h"

.section .text

ASM_HIDDEN(__tsan_setjmp)
.comm _ZN14__interception11real_setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(setjmp))
ASM_SYMBOL_INTERCEPTOR(setjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi.d $sp, $sp, -32
  st.d $ra, $sp, 24
  st.d $fp, $sp, 16
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (22, -16)

  // Adjust the SP for previous frame
  addi.d $fp, $sp, 32
  CFI_DEF_CFA_REGISTER (22)

  // Save env parameter
  st.d $a0, $sp, 8
  CFI_OFFSET (4, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi.d  $a0, $fp, 0

  // call tsan interceptor
  bl      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld.d $a0, $sp, 8
  CFI_RESTORE (4)

  // Restore frame/link register
  ld.d $fp, $sp, 16
  ld.d $ra, $sp, 24
  addi.d $sp, $sp, 32
  CFI_RESTORE (22)
  CFI_RESTORE (1)
  CFI_DEF_CFA (3, 0)

  // tail jump to libc setjmp
  la.local $a1, _ZN14__interception11real_setjmpE
  ld.d $a1, $a1, 0
  jr $a1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(setjmp))

.comm _ZN14__interception12real__setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(_setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(_setjmp))
ASM_SYMBOL_INTERCEPTOR(_setjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi.d $sp, $sp, -32
  st.d $ra, $sp, 24
  st.d $fp, $sp, 16
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (22, -16)

  // Adjust the SP for previous frame
  addi.d $fp, $sp, 32
  CFI_DEF_CFA_REGISTER (22)

  // Save env parameter
  st.d $a0, $sp, 8
  CFI_OFFSET (4, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi.d  $a0, $fp, 0

  // call tsan interceptor
  bl      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld.d $a0, $sp, 8
  CFI_RESTORE (4)

  // Restore frame/link register
  ld.d $fp, $sp, 16
  ld.d $ra, $sp, 24
  addi.d $sp, $sp, 32
  CFI_RESTORE (22)
  CFI_RESTORE (1)
  CFI_DEF_CFA (3, 0)

  // tail jump to libc setjmp
  la.local $a1, _ZN14__interception12real__setjmpE
  ld.d $a1, $a1, 0
  jr $a1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(_setjmp))

.comm _ZN14__interception14real_sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(sigsetjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi.d $sp, $sp, -32
  st.d $ra, $sp, 24
  st.d $fp, $sp, 16
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (22, -16)

  // Adjust the SP for previous frame
  addi.d $fp, $sp, 32
  CFI_DEF_CFA_REGISTER (22)

  // Save env parameter
  st.d $a0, $sp, 8
  CFI_OFFSET (4, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi.d  $a0, $fp, 0

  // call tsan interceptor
  bl      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld.d $a0, $sp, 8
  CFI_RESTORE (4)

  // Restore frame/link register
  ld.d $fp, $sp, 16
  ld.d $ra, $sp, 24
  addi.d $sp, $sp, 32
  CFI_RESTORE (22)
  CFI_RESTORE (1)
  CFI_DEF_CFA (3, 0)

  // tail jump to libc setjmp
  la.local $a1, _ZN14__interception14real_sigsetjmpE
  ld.d $a1, $a1, 0
  jr $a1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))

.comm _ZN14__interception16real___sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp):
  CFI_STARTPROC

  // Save frame pointer and return address register
  addi.d $sp, $sp, -32
  st.d $ra, $sp, 24
  st.d $fp, $sp, 16
  CFI_DEF_CFA_OFFSET (32)
  CFI_OFFSET (1, -8)
  CFI_OFFSET (22, -16)

  // Adjust the SP for previous frame
  addi.d $fp, $sp, 32
  CFI_DEF_CFA_REGISTER (22)

  // Save env parameter
  st.d $a0, $sp, 8
  CFI_OFFSET (4, -24)

  // Obtain SP, first argument to `void __tsan_setjmp(uptr sp)`
  addi.d  $a0, $fp, 0

  // call tsan interceptor
  bl      ASM_SYMBOL(__tsan_setjmp)

  // Restore env parameter
  ld.d $a0, $sp, 8
  CFI_RESTORE (4)

  // Restore frame/link register
  ld.d $fp, $sp, 16
  ld.d $ra, $sp, 24
  addi.d $sp, $sp, 32
  CFI_RESTORE (22)
  CFI_RESTORE (1)
  CFI_DEF_CFA (3, 0)

  // tail jump to libc setjmp
  la.local $a1, _ZN14__interception16real___sigsetjmpE
  ld.d $a1, $a1, 0
  jr $a1

  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
