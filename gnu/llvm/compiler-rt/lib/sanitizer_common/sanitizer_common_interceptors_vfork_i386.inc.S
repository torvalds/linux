#if defined(__i386__) && defined(__linux__)

#include "sanitizer_common/sanitizer_asm.h"

.comm _ZN14__interception10real_vforkE,4,4
.globl ASM_WRAPPER_NAME(vfork)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(vfork))
ASM_WRAPPER_NAME(vfork):
        _CET_ENDBR
        // Store return address in the spill area and tear down the stack frame.
        sub     $12, %esp
        call    COMMON_INTERCEPTOR_SPILL_AREA
        mov     12(%esp), %ecx
        mov     %ecx, (%eax)
        add     $16, %esp

        call    .L0$pb
.L0$pb:
        pop     %eax
.Ltmp0:
        add     $_GLOBAL_OFFSET_TABLE_+(.Ltmp0-.L0$pb), %eax
        call    *_ZN14__interception10real_vforkE@GOTOFF(%eax)

        // Restore the stack frame.
        // 12(%esp) return address
        // 8(%esp) spill %ebx
        // 4(%esp) spill REAL(vfork) return value
        // (%esp) call frame (arg0) for __*_handle_vfork
        sub     $16, %esp
        mov     %ebx, 8(%esp)
        mov     %eax, 4(%esp)

        // Form GOT address in %ebx.
        call    .L1$pb
.L1$pb:
        pop     %ebx
.Ltmp1:
        add     $_GLOBAL_OFFSET_TABLE_+(.Ltmp1-.L1$pb), %ebx

        // Restore original return address.
        call    COMMON_INTERCEPTOR_SPILL_AREA
        mov     (%eax), %ecx
        mov     %ecx, 12(%esp)
        mov     4(%esp), %eax

        // Call handle_vfork in the parent process (%rax != 0).
        test    %eax, %eax
        je      .L_exit

        lea     16(%esp), %ecx
        mov     %ecx, (%esp)
        call    COMMON_INTERCEPTOR_HANDLE_VFORK@PLT

.L_exit:
        mov     4(%esp), %eax
        mov     8(%esp), %ebx
        add     $12, %esp
        ret
ASM_SIZE(vfork)

ASM_INTERCEPTOR_TRAMPOLINE(vfork)
ASM_TRAMPOLINE_ALIAS(vfork, vfork)

#endif
