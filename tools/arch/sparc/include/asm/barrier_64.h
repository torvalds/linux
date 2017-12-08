/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_SPARC64_BARRIER_H
#define __TOOLS_LINUX_SPARC64_BARRIER_H

/* Copied from the kernel sources to tools/:
 *
 * These are here in an effort to more fully work around Spitfire Errata
 * #51.  Essentially, if a memory barrier occurs soon after a mispredicted
 * branch, the chip can stop executing instructions until a trap occurs.
 * Therefore, if interrupts are disabled, the chip can hang forever.
 *
 * It used to be believed that the memory barrier had to be right in the
 * delay slot, but a case has been traced recently wherein the memory barrier
 * was one instruction after the branch delay slot and the chip still hung.
 * The offending sequence was the following in sym_wakeup_done() of the
 * sym53c8xx_2 driver:
 *
 *	call	sym_ccb_from_dsa, 0
 *	 movge	%icc, 0, %l0
 *	brz,pn	%o0, .LL1303
 *	 mov	%o0, %l2
 *	membar	#LoadLoad
 *
 * The branch has to be mispredicted for the bug to occur.  Therefore, we put
 * the memory barrier explicitly into a "branch always, predicted taken"
 * delay slot to avoid the problem case.
 */
#define membar_safe(type) \
do {	__asm__ __volatile__("ba,pt	%%xcc, 1f\n\t" \
			     " membar	" type "\n" \
			     "1:\n" \
			     : : : "memory"); \
} while (0)

/* The kernel always executes in TSO memory model these days,
 * and furthermore most sparc64 chips implement more stringent
 * memory ordering than required by the specifications.
 */
#define mb()	membar_safe("#StoreLoad")
#define rmb()	__asm__ __volatile__("":::"memory")
#define wmb()	__asm__ __volatile__("":::"memory")

#endif /* !(__TOOLS_LINUX_SPARC64_BARRIER_H) */
