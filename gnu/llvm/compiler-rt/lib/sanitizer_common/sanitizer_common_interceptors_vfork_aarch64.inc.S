#if defined(__aarch64__) && defined(__linux__)

#include "sanitizer_common/sanitizer_asm.h"
#include "builtins/assembly.h"

ASM_HIDDEN(COMMON_INTERCEPTOR_SPILL_AREA)

.comm _ZN14__interception10real_vforkE,8,8
.globl ASM_WRAPPER_NAME(vfork)
ASM_TYPE_FUNCTION(ASM_WRAPPER_NAME(vfork))
ASM_WRAPPER_NAME(vfork):
        // Save x30 in the off-stack spill area.
        hint    #25 // paciasp
        stp     xzr, x30, [sp, #-16]!
        bl      COMMON_INTERCEPTOR_SPILL_AREA
        ldp     xzr, x30, [sp], 16
        str     x30, [x0]

        // Call real vfork. This may return twice. User code that runs between the first and the second return
        // may clobber the stack frame of the interceptor; that's why it does not have a frame.
        adrp    x0, _ZN14__interception10real_vforkE
        ldr     x0, [x0, :lo12:_ZN14__interception10real_vforkE]
        blr     x0

        stp     x0, xzr, [sp, #-16]!
        cmp     x0, #0
        b.eq   .L_exit

        // x0 != 0 => parent process. Clear stack shadow.
        add    x0, sp, #16
        bl     COMMON_INTERCEPTOR_HANDLE_VFORK

.L_exit:
        // Restore x30.
        bl     COMMON_INTERCEPTOR_SPILL_AREA
        ldr    x30, [x0]
        ldp    x0, xzr, [sp], 16
        hint   #29 // autiasp

        ret
ASM_SIZE(vfork)

ASM_INTERCEPTOR_TRAMPOLINE(vfork)
ASM_TRAMPOLINE_ALIAS(vfork, vfork)

GNU_PROPERTY_BTI_PAC

#endif
