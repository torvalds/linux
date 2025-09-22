#if defined(__loongarch_lp64) && defined(__linux__)

#include "sanitizer_common/sanitizer_asm.h"

ASM_HIDDEN(COMMON_INTERCEPTOR_SPILL_AREA)
ASM_HIDDEN(_ZN14__interception10real_vforkE)

.text
.globl ASM_WRAPPER_NAME(vfork)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(vfork))
ASM_WRAPPER_NAME(vfork):
        // Save ra in the off-stack spill area.
        // allocate space on stack
        addi.d    $sp, $sp, -16
        // store $ra value
        st.d      $ra, $sp, 8
        bl        COMMON_INTERCEPTOR_SPILL_AREA
        // restore previous values from stack
        ld.d      $ra, $sp, 8
        // adjust stack
        addi.d    $sp, $sp, 16
        // store $ra by $a0
        st.d      $ra, $a0, 0

        // Call real vfork. This may return twice. User code that runs between the first and the second return
        // may clobber the stack frame of the interceptor; that's why it does not have a frame.
        la.local  $a0, _ZN14__interception10real_vforkE
        ld.d      $a0, $a0, 0
        jirl      $ra, $a0, 0

        // adjust stack
        addi.d    $sp, $sp, -16
        // store $a0 by adjusted stack
        st.d      $a0, $sp, 8
        // jump to exit label if $a0 is 0
        beqz      $a0, .L_exit

        // $a0 != 0 => parent process. Clear stack shadow.
        // put old $sp to $a0
        addi.d    $a0, $sp, 16
        bl        %plt(COMMON_INTERCEPTOR_HANDLE_VFORK)

.L_exit:
        // Restore $ra
        bl        COMMON_INTERCEPTOR_SPILL_AREA
        ld.d      $ra, $a0, 0
        // load value by stack
        ld.d      $a0, $sp, 8
        // adjust stack
        addi.d    $sp, $sp, 16
        jr        $ra
ASM_SIZE(vfork)

ASM_INTERCEPTOR_TRAMPOLINE(vfork)
ASM_TRAMPOLINE_ALIAS(vfork, vfork)

#endif
