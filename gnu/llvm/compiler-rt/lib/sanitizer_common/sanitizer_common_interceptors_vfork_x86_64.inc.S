#if defined(__x86_64__) && defined(__linux__)

#include "sanitizer_common/sanitizer_asm.h"

.comm _ZN14__interception10real_vforkE,8,8
.globl ASM_WRAPPER_NAME(vfork)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(vfork))
ASM_WRAPPER_NAME(vfork):
        _CET_ENDBR
        // Store return address in the spill area and tear down the stack frame.
        push    %rcx
        call    COMMON_INTERCEPTOR_SPILL_AREA
        pop     %rcx
        pop     %rdi
        mov     %rdi, (%rax)

        call    *_ZN14__interception10real_vforkE(%rip)

        // Restore return address from the spill area.
        push    %rcx
        push    %rax
        call    COMMON_INTERCEPTOR_SPILL_AREA
        mov     (%rax), %rdx
        mov     %rdx, 8(%rsp)
        mov     (%rsp), %rax

        // Call handle_vfork in the parent process (%rax != 0).
        test    %rax, %rax
        je      .L_exit

        lea     16(%rsp), %rdi
        call    COMMON_INTERCEPTOR_HANDLE_VFORK@PLT

.L_exit:
        pop     %rax
        ret
ASM_SIZE(ASM_WRAPPER_NAME(vfork))

ASM_INTERCEPTOR_TRAMPOLINE(vfork)
ASM_TRAMPOLINE_ALIAS(vfork, vfork)

#endif
