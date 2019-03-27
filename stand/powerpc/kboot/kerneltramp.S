/*
 * This is the analog to the kexec "purgatory" code
 *
 * The goal here is to call the actual kernel entry point with the arguments it
 * expects when kexec calls into it with no arguments. The value of the kernel
 * entry point and arguments r3-r7 are copied into the trampoline text (which
 * can be executed from any address) at bytes 8-32. kexec begins execution
 * of APs at 0x60 bytes past the entry point, executing in a copy relocated
 * to the absolute address 0x60. Here we implement a loop waiting on the release
 * of a lock by the kernel at 0x40.
 * 
 * $FreeBSD$
 */

#include <machine/asm.h>

        .globl  CNAME(kerneltramp),CNAME(szkerneltramp)
CNAME(kerneltramp):
	mflr %r9
	bl 2f
	.space 24	/* branch address, r3-r7 */

/*
 * MUST BE IN SYNC WITH:
 *  struct trampoline_data {
 *   uint32_t	kernel_entry;
 *   uint32_t	dtb;
 *   uint32_t	phys_mem_offset;
 *   uint32_t	of_entry;
 *   uint32_t	mdp;
 *   uint32_t	mdp_size;
 *  };
 */

. = kerneltramp + 0x40	/* AP spinlock */
	.long 0

. = kerneltramp + 0x60	/* AP entry point */
	li	%r3,0x40
1:	lwz	%r1,0(%r3)
	cmpwi	%r1,0
	beq	1b

	/* Jump into CPU reset */
	li	%r0,0x100
	icbi	0,%r0
	isync
	sync
	ba	0x100

2:	/* Continuation of kerneltramp */
	mflr	%r8
	mtlr	%r9

	mfmsr	%r10
	andi.	%r10, %r10, 1	/* test MSR_LE */
	bne	little_endian

/* We're starting in BE */
big_endian:
	lwz	%r3,4(%r8)
	lwz	%r4,8(%r8)
	lwz	%r5,12(%r8)
	lwz	%r6,16(%r8)
	lwz	%r7,20(%r8)

	lwz	%r10, 0(%r8)
	mtctr	%r10
	bctr

/* We're starting in LE */
little_endian:

	/* Entries are BE, swap them during load. */
	li	%r10, 4
	lwbrx	%r3, %r8, %r10
	li	%r10, 8
	lwbrx	%r4, %r8, %r10
	li	%r10, 12
	lwbrx	%r5, %r8, %r10
	li	%r10, 16
	lwbrx	%r6, %r8, %r10
	li	%r10, 20
	lwbrx	%r7, %r8, %r10

	/* Clear MSR_LE flag to enter the BE world */
	mfmsr	%r10
	clrrdi	%r10, %r10, 1
	mtsrr1	%r10

	/* Entry is at 0(%r8) */
	li	%r10, 0
	lwbrx	%r10, %r8, %r10
	mtsrr0	%r10

	rfid

endkerneltramp:

	.data
CNAME(szkerneltramp):
	.long endkerneltramp - CNAME(kerneltramp)
