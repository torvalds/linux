// The content of this file is x86_64-only:
#if defined(__x86_64__)

#include "sanitizer_common/sanitizer_asm.h"

#if !defined(__APPLE__)
.section .text
#else
.section __TEXT,__text
#endif

ASM_HIDDEN(__tsan_setjmp)
#if defined(__NetBSD__)
.comm _ZN14__interception15real___setjmp14E,8,8
#elif !defined(__APPLE__)
.comm _ZN14__interception11real_setjmpE,8,8
#endif
#if defined(__NetBSD__)
.globl ASM_SYMBOL_INTERCEPTOR(__setjmp14)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__setjmp14))
ASM_SYMBOL_INTERCEPTOR(__setjmp14):
#else
.globl ASM_SYMBOL_INTERCEPTOR(setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(setjmp))
ASM_SYMBOL_INTERCEPTOR(setjmp):
#endif
  CFI_STARTPROC
  _CET_ENDBR
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // obtain SP, store in %rdi, first argument to `void __tsan_setjmp(uptr sp)`
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 8(%rsp), %rdi
#elif defined(__linux__) || defined(__APPLE__)
  lea 16(%rsp), %rdi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc setjmp
  movl $0, %eax
#if defined(__NetBSD__)
  movq _ZN14__interception15real___setjmp14E@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#elif !defined(__APPLE__)
  movq _ZN14__interception11real_setjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(setjmp)
#endif
  CFI_ENDPROC
#if defined(__NetBSD__)
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__setjmp14))
#else
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(setjmp))
#endif

.comm _ZN14__interception12real__setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(_setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(_setjmp))
ASM_SYMBOL_INTERCEPTOR(_setjmp):
  CFI_STARTPROC
  _CET_ENDBR
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // obtain SP, store in %rdi, first argument to `void __tsan_setjmp(uptr sp)`
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 8(%rsp), %rdi
#elif defined(__linux__) || defined(__APPLE__)
  lea 16(%rsp), %rdi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc setjmp
  movl $0, %eax
#if !defined(__APPLE__)
  movq _ZN14__interception12real__setjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(_setjmp)
#endif
  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(_setjmp))

#if defined(__NetBSD__)
.comm _ZN14__interception18real___sigsetjmp14E,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14):
#else
.comm _ZN14__interception14real_sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(sigsetjmp):
#endif
  CFI_STARTPROC
  _CET_ENDBR
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // save savesigs parameter
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  // align stack frame
  sub $8, %rsp
  CFI_ADJUST_CFA_OFFSET(8)
  // obtain SP, store in %rdi, first argument to `void __tsan_setjmp(uptr sp)`
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 24(%rsp), %rdi
#elif defined(__linux__) || defined(__APPLE__)
  lea 32(%rsp), %rdi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // unalign stack frame
  add $8, %rsp
  CFI_ADJUST_CFA_OFFSET(-8)
  // restore savesigs parameter
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rsi)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc sigsetjmp
  movl $0, %eax
#if defined(__NetBSD__)
  movq _ZN14__interception18real___sigsetjmp14E@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#elif !defined(__APPLE__)
  movq _ZN14__interception14real_sigsetjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(sigsetjmp)
#endif
  CFI_ENDPROC
#if defined(__NetBSD__)
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14))
#else
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
#endif

#if !defined(__APPLE__) && !defined(__NetBSD__)
.comm _ZN14__interception16real___sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp):
  CFI_STARTPROC
  _CET_ENDBR
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // save savesigs parameter
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  // align stack frame
  sub $8, %rsp
  CFI_ADJUST_CFA_OFFSET(8)
  // obtain SP, store in %rdi, first argument to `void __tsan_setjmp(uptr sp)`
#if defined(__FreeBSD__)
  lea 24(%rsp), %rdi
#else
  lea 32(%rsp), %rdi
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // unalign stack frame
  add $8, %rsp
  CFI_ADJUST_CFA_OFFSET(-8)
  // restore savesigs parameter
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rsi)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc sigsetjmp
  movl $0, %eax
  movq _ZN14__interception16real___sigsetjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
#endif  // !defined(__APPLE__) && !defined(__NetBSD__)

NO_EXEC_STACK_DIRECTIVE

#endif
