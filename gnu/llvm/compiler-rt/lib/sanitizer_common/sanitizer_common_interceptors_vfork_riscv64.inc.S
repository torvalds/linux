#if (defined(__riscv) && (__riscv_xlen == 64)) && defined(__linux__)

#include "sanitizer_common/sanitizer_asm.h"

ASM_HIDDEN(COMMON_INTERCEPTOR_SPILL_AREA)

.comm _ZN14__interception10real_vforkE,8,8
.globl ASM_WRAPPER_NAME(vfork)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(vfork))
ASM_WRAPPER_NAME(vfork):
        // Save ra in the off-stack spill area.
        // allocate space on stack
        addi    sp, sp, -16
        // store ra value 
        sd      ra, 8(sp)
        call    COMMON_INTERCEPTOR_SPILL_AREA
        // restore previous values from stack
        ld      ra, 8(sp)
        // adjust stack
        addi    sp, sp, 16
        // store ra by x10
        sd      ra, 0(x10)

        // Call real vfork. This may return twice. User code that runs between the first and the second return
        // may clobber the stack frame of the interceptor; that's why it does not have a frame.
        la x10, _ZN14__interception10real_vforkE
        ld x10, 0(x10)
        jalr x10

        // adjust stack
        addi    sp, sp, -16
        // store x10 by adjusted stack
        sd      x10, 8(sp)
        // jump to exit label if x10 is 0
        beqz    x10, .L_exit

        // x0 != 0 => parent process. Clear stack shadow.
        // put old sp to x10
        addi   x10, sp, 16
        call   COMMON_INTERCEPTOR_HANDLE_VFORK

.L_exit:
        // Restore ra
        call   COMMON_INTERCEPTOR_SPILL_AREA
        ld     ra, 0(x10)
        // load value by stack
        ld     x10, 8(sp)
        // adjust stack
        addi   sp, sp, 16
        ret
ASM_SIZE(vfork)

ASM_INTERCEPTOR_TRAMPOLINE(vfork)
ASM_TRAMPOLINE_ALIAS(vfork, vfork)

#endif
