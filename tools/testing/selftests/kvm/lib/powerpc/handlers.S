/* SPDX-License-Identifier: GPL-2.0 */

#include <ppc_asm.h>

.macro INTERRUPT vec
. = __interrupts_start + \vec
	std	%r0,(0*8)(%r13)
	std	%r3,(3*8)(%r13)
	mfspr	%r0,SPR_CFAR
	li	%r3,\vec
	b	handle_interrupt
.endm

.balign 0x1000
.global __interrupts_start
__interrupts_start:
INTERRUPT 0x100
INTERRUPT 0x200
INTERRUPT 0x300
INTERRUPT 0x380
INTERRUPT 0x400
INTERRUPT 0x480
INTERRUPT 0x500
INTERRUPT 0x600
INTERRUPT 0x700
INTERRUPT 0x800
INTERRUPT 0x900
INTERRUPT 0xa00
INTERRUPT 0xc00
INTERRUPT 0xd00
INTERRUPT 0xf00
INTERRUPT 0xf20
INTERRUPT 0xf40
INTERRUPT 0xf60

virt_handle_interrupt:
	stdu	%r1,-INT_FRAME_SIZE(%r1)
	mr	%r3,%r31
	bl	route_interrupt
	ld	%r4,(32*8)(%r31) /* NIA */
	ld	%r5,(33*8)(%r31) /* MSR */
	ld	%r6,(35*8)(%r31) /* LR */
	ld	%r7,(36*8)(%r31) /* CTR */
	ld	%r8,(37*8)(%r31) /* XER */
	lwz	%r9,(38*8)(%r31) /* CR */
	mtspr	SPR_SRR0,%r4
	mtspr	SPR_SRR1,%r5
	mtlr	%r6
	mtctr	%r7
	mtxer	%r8
	mtcr	%r9
reg=4
	ld	%r0,(0*8)(%r31)
	ld	%r3,(3*8)(%r31)
.rept 28
	ld	reg,(reg*8)(%r31)
	reg=reg+1
.endr
	addi	%r1,%r1,INT_FRAME_SIZE
	rfid

virt_handle_interrupt_p:
	.llong virt_handle_interrupt

handle_interrupt:
reg=4
.rept 28
	std	reg,(reg*8)(%r13)
	reg=reg+1
.endr
	mfspr	%r4,SPR_SRR0
	mfspr	%r5,SPR_SRR1
	mflr	%r6
	mfctr	%r7
	mfxer	%r8
	mfcr	%r9
	std	%r4,(32*8)(%r13) /* NIA */
	std	%r5,(33*8)(%r13) /* MSR */
	std	%r0,(34*8)(%r13) /* CFAR */
	std	%r6,(35*8)(%r13) /* LR */
	std	%r7,(36*8)(%r13) /* CTR */
	std	%r8,(37*8)(%r13) /* XER */
	stw	%r9,(38*8 + 0)(%r13) /* CR */
	stw	%r3,(38*8 + 4)(%r13) /* TRAP */

	ld	%r31,(39*8)(%r13) /* vaddr */
	ld	%r4,virt_handle_interrupt_p - __interrupts_start(0)
	mtspr	SPR_SRR0,%r4
	/* Reuse SRR1 */

	rfid
.global __interrupts_end
__interrupts_end:
