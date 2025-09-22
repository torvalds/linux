#include "tsan_ppc_regs.h"

        .section .text
        .hidden __tsan_setjmp
        .globl _setjmp
        .type _setjmp, @function
        .align 4
#if _CALL_ELF == 2
_setjmp:
#else
	.section ".opd","aw"
	.align 3
_setjmp:
	.quad   .L._setjmp,.TOC.@tocbase,0
	.previous
#endif
.L._setjmp:
        mflr    r0
        stdu    r1,-48(r1)
        std     r2,24(r1)
        std     r3,32(r1)
        std     r0,40(r1)
        // r3 is the original stack pointer.
        addi    r3,r1,48
        // r4 is the mangled stack pointer (see glibc)
        ld      r4,-28696(r13)
        xor     r4,r3,r4
        // Materialize a TOC in case we were called from libc.
        // For big-endian, we load the TOC from the OPD.  For little-
        // endian, we use the .TOC. symbol to find it.
        nop
        bcl     20,31,0f
0:
        mflr    r2
#if _CALL_ELF == 2
        addis   r2,r2,.TOC.-0b@ha
        addi    r2,r2,.TOC.-0b@l
#else
        addis   r2,r2,_setjmp-0b@ha
        addi    r2,r2,_setjmp-0b@l
        ld      r2,8(r2)
#endif
        // Call the interceptor.
        bl      __tsan_setjmp
        nop
        // Restore regs needed for setjmp.
        ld      r3,32(r1)
        ld      r0,40(r1)
        // Emulate the real setjmp function.  We do this because we can't
        // perform a sibcall:  The real setjmp function trashes the TOC
        // pointer, and with a sibcall we have no way to restore it.
        // This way we can make sure our caller's stack pointer and
        // link register are saved correctly in the jmpbuf.
        ld      r6,-28696(r13)
        addi    r5,r1,48  // original stack ptr of caller
        xor     r5,r6,r5
        std     r5,0(r3)  // mangled stack ptr of caller
        ld      r5,24(r1)
        std     r5,8(r3)  // caller's saved TOC pointer
        xor     r0,r6,r0
        std     r0,16(r3) // caller's mangled return address
        mfcr    r0
        // Nonvolatiles.
        std     r14,24(r3)
        stfd    f14,176(r3)
        stw     r0,172(r3) // CR
        std     r15,32(r3)
        stfd    f15,184(r3)
        std     r16,40(r3)
        stfd    f16,192(r3)
        std     r17,48(r3)
        stfd    f17,200(r3)
        std     r18,56(r3)
        stfd    f18,208(r3)
        std     r19,64(r3)
        stfd    f19,216(r3)
        std     r20,72(r3)
        stfd    f20,224(r3)
        std     r21,80(r3)
        stfd    f21,232(r3)
        std     r22,88(r3)
        stfd    f22,240(r3)
        std     r23,96(r3)
        stfd    f23,248(r3)
        std     r24,104(r3)
        stfd    f24,256(r3)
        std     r25,112(r3)
        stfd    f25,264(r3)
        std     r26,120(r3)
        stfd    f26,272(r3)
        std     r27,128(r3)
        stfd    f27,280(r3)
        std     r28,136(r3)
        stfd    f28,288(r3)
        std     r29,144(r3)
        stfd    f29,296(r3)
        std     r30,152(r3)
        stfd    f30,304(r3)
        std     r31,160(r3)
        stfd    f31,312(r3)
        addi    r5,r3,320
        mfspr   r0,256
        stw     r0,168(r3)  // VRSAVE
        addi    r6,r5,16
        stvx    v20,0,r5
        addi    r5,r5,32
        stvx    v21,0,r6
        addi    r6,r6,32
        stvx    v22,0,r5
        addi    r5,r5,32
        stvx    v23,0,r6
        addi    r6,r6,32
        stvx    v24,0,r5
        addi    r5,r5,32
        stvx    v25,0,r6
        addi    r6,r6,32
        stvx    v26,0,r5
        addi    r5,r5,32
        stvx    v27,0,r6
        addi    r6,r6,32
        stvx    v28,0,r5
        addi    r5,r5,32
        stvx    v29,0,r6
        addi    r6,r6,32
        stvx    v30,0,r5
        stvx    v31,0,r6
        // Clear the "mask-saved" slot.
        li      r4,0
        stw     r4,512(r3)
        // Restore TOC, LR, and stack and return to caller.
        ld      r2,24(r1)
        ld      r0,40(r1)
        addi    r1,r1,48
        li      r3,0  // This is the setjmp return path
        mtlr    r0
        blr
        .size _setjmp, .-.L._setjmp

        .globl setjmp
        .type setjmp, @function
        .align 4
setjmp:
        b       _setjmp
        .size setjmp, .-setjmp

        // sigsetjmp is like setjmp, except that the mask in r4 needs
        // to be saved at offset 512 of the jump buffer.
        .globl __sigsetjmp
        .type __sigsetjmp, @function
        .align 4
#if _CALL_ELF == 2
__sigsetjmp:
#else
	.section ".opd","aw"
	.align 3
__sigsetjmp:
	.quad   .L.__sigsetjmp,.TOC.@tocbase,0
	.previous
#endif
.L.__sigsetjmp:
        mflr    r0
        stdu    r1,-64(r1)
        std     r2,24(r1)
        std     r3,32(r1)
        std     r4,40(r1)
        std     r0,48(r1)
        // r3 is the original stack pointer.
        addi    r3,r1,64
        // r4 is the mangled stack pointer (see glibc)
        ld      r4,-28696(r13)
        xor     r4,r3,r4
        // Materialize a TOC in case we were called from libc.
        // For big-endian, we load the TOC from the OPD.  For little-
        // endian, we use the .TOC. symbol to find it.
        nop
        bcl     20,31,1f
1:
        mflr    r2
#if _CALL_ELF == 2
        addis   r2,r2,.TOC.-1b@ha
        addi    r2,r2,.TOC.-1b@l
#else
        addis   r2,r2,_setjmp-1b@ha
        addi    r2,r2,_setjmp-1b@l
        ld      r2,8(r2)
#endif
        // Call the interceptor.
        bl      __tsan_setjmp
        nop
        // Restore regs needed for __sigsetjmp.
        ld      r3,32(r1)
        ld      r4,40(r1)
        ld      r0,48(r1)
        // Emulate the real sigsetjmp function.  We do this because we can't
        // perform a sibcall:  The real sigsetjmp function trashes the TOC
        // pointer, and with a sibcall we have no way to restore it.
        // This way we can make sure our caller's stack pointer and
        // link register are saved correctly in the jmpbuf.
        ld      r6,-28696(r13)
        addi    r5,r1,64  // original stack ptr of caller
        xor     r5,r6,r5
        std     r5,0(r3)  // mangled stack ptr of caller
        ld      r5,24(r1)
        std     r5,8(r3)  // caller's saved TOC pointer
        xor     r0,r6,r0
        std     r0,16(r3) // caller's mangled return address
        mfcr    r0
        // Nonvolatiles.
        std     r14,24(r3)
        stfd    f14,176(r3)
        stw     r0,172(r3) // CR
        std     r15,32(r3)
        stfd    f15,184(r3)
        std     r16,40(r3)
        stfd    f16,192(r3)
        std     r17,48(r3)
        stfd    f17,200(r3)
        std     r18,56(r3)
        stfd    f18,208(r3)
        std     r19,64(r3)
        stfd    f19,216(r3)
        std     r20,72(r3)
        stfd    f20,224(r3)
        std     r21,80(r3)
        stfd    f21,232(r3)
        std     r22,88(r3)
        stfd    f22,240(r3)
        std     r23,96(r3)
        stfd    f23,248(r3)
        std     r24,104(r3)
        stfd    f24,256(r3)
        std     r25,112(r3)
        stfd    f25,264(r3)
        std     r26,120(r3)
        stfd    f26,272(r3)
        std     r27,128(r3)
        stfd    f27,280(r3)
        std     r28,136(r3)
        stfd    f28,288(r3)
        std     r29,144(r3)
        stfd    f29,296(r3)
        std     r30,152(r3)
        stfd    f30,304(r3)
        std     r31,160(r3)
        stfd    f31,312(r3)
        addi    r5,r3,320
        mfspr   r0,256
        stw     r0,168(r3) // VRSAVE
        addi    r6,r5,16
        stvx    v20,0,r5
        addi    r5,r5,32
        stvx    v21,0,r6
        addi    r6,r6,32
        stvx    v22,0,r5
        addi    r5,r5,32
        stvx    v23,0,r6
        addi    r6,r6,32
        stvx    v24,0,r5
        addi    r5,r5,32
        stvx    v25,0,r6
        addi    r6,r6,32
        stvx    v26,0,r5
        addi    r5,r5,32
        stvx    v27,0,r6
        addi    r6,r6,32
        stvx    v28,0,r5
        addi    r5,r5,32
        stvx    v29,0,r6
        addi    r6,r6,32
        stvx    v30,0,r5
        stvx    v31,0,r6
        // Save into the "mask-saved" slot.
        stw     r4,512(r3)
        // Restore TOC, LR, and stack and return to caller.
        ld      r2,24(r1)
        ld      r0,48(r1)
        addi    r1,r1,64
        li      r3,0  // This is the sigsetjmp return path
        mtlr    r0
        blr
        .size __sigsetjmp, .-.L.__sigsetjmp

        .globl sigsetjmp
        .type sigsetjmp, @function
        .align 4
sigsetjmp:
        b       __sigsetjmp
        .size sigsetjmp, .-sigsetjmp
