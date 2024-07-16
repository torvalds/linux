/* SPDX-License-Identifier: GPL-2.0 */
.macro save_registers
	add	sp, sp, #-16 * 17

	stp	x0, x1, [sp, #16 * 0]
	stp	x2, x3, [sp, #16 * 1]
	stp	x4, x5, [sp, #16 * 2]
	stp	x6, x7, [sp, #16 * 3]
	stp	x8, x9, [sp, #16 * 4]
	stp	x10, x11, [sp, #16 * 5]
	stp	x12, x13, [sp, #16 * 6]
	stp	x14, x15, [sp, #16 * 7]
	stp	x16, x17, [sp, #16 * 8]
	stp	x18, x19, [sp, #16 * 9]
	stp	x20, x21, [sp, #16 * 10]
	stp	x22, x23, [sp, #16 * 11]
	stp	x24, x25, [sp, #16 * 12]
	stp	x26, x27, [sp, #16 * 13]
	stp	x28, x29, [sp, #16 * 14]

	/*
	 * This stores sp_el1 into ex_regs.sp so exception handlers can "look"
	 * at it. It will _not_ be used to restore the sp on return from the
	 * exception so handlers can not update it.
	 */
	add	x1, sp, #16 * 17
	stp	x30, x1, [sp, #16 * 15] /* x30, SP */

	mrs	x1, elr_el1
	mrs	x2, spsr_el1
	stp	x1, x2, [sp, #16 * 16] /* PC, PSTATE */
.endm

.macro restore_registers
	ldp	x1, x2, [sp, #16 * 16] /* PC, PSTATE */
	msr	elr_el1, x1
	msr	spsr_el1, x2

	/* sp is not restored */
	ldp	x30, xzr, [sp, #16 * 15] /* x30, SP */

	ldp	x28, x29, [sp, #16 * 14]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x0, x1, [sp, #16 * 0]

	add	sp, sp, #16 * 17

	eret
.endm

.pushsection ".entry.text", "ax"
.balign 0x800
.global vectors
vectors:
.popsection

.set	vector, 0

/*
 * Build an exception handler for vector and append a jump to it into
 * vectors (while making sure that it's 0x80 aligned).
 */
.macro HANDLER, label
handler_\label:
	save_registers
	mov	x0, sp
	mov	x1, #vector
	bl	route_exception
	restore_registers

.pushsection ".entry.text", "ax"
.balign 0x80
	b	handler_\label
.popsection

.set	vector, vector + 1
.endm

.macro HANDLER_INVALID
.pushsection ".entry.text", "ax"
.balign 0x80
/* This will abort so no need to save and restore registers. */
	mov	x0, #vector
	mov	x1, #0 /* ec */
	mov	x2, #0 /* valid_ec */
	b	kvm_exit_unexpected_exception
.popsection

.set	vector, vector + 1
.endm

/*
 * Caution: be sure to not add anything between the declaration of vectors
 * above and these macro calls that will build the vectors table below it.
 */
	HANDLER_INVALID                         // Synchronous EL1t
	HANDLER_INVALID                         // IRQ EL1t
	HANDLER_INVALID                         // FIQ EL1t
	HANDLER_INVALID                         // Error EL1t

	HANDLER	el1h_sync                       // Synchronous EL1h
	HANDLER	el1h_irq                        // IRQ EL1h
	HANDLER el1h_fiq                        // FIQ EL1h
	HANDLER	el1h_error                      // Error EL1h

	HANDLER	el0_sync_64                     // Synchronous 64-bit EL0
	HANDLER	el0_irq_64                      // IRQ 64-bit EL0
	HANDLER	el0_fiq_64                      // FIQ 64-bit EL0
	HANDLER	el0_error_64                    // Error 64-bit EL0

	HANDLER	el0_sync_32                     // Synchronous 32-bit EL0
	HANDLER	el0_irq_32                      // IRQ 32-bit EL0
	HANDLER	el0_fiq_32                      // FIQ 32-bit EL0
	HANDLER	el0_error_32                    // Error 32-bit EL0
