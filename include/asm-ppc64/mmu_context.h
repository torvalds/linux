#ifndef __PPC64_MMU_CONTEXT_H
#define __PPC64_MMU_CONTEXT_H

#include <linux/config.h>
#include <linux/kernel.h>	
#include <linux/mm.h>	
#include <asm/mmu.h>	
#include <asm/cputable.h>

/*
 * Copyright (C) 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 64;
	return __ffs(b[2]) + 128;
}

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

#define NO_CONTEXT	0
#define MAX_CONTEXT	(0x100000-1)

extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);

extern void switch_stab(struct task_struct *tsk, struct mm_struct *mm);
extern void switch_slb(struct task_struct *tsk, struct mm_struct *mm);

/*
 * switch_mm is the entry point called from the architecture independent
 * code in kernel/sched.c
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	if (!cpu_isset(smp_processor_id(), next->cpu_vm_mask))
		cpu_set(smp_processor_id(), next->cpu_vm_mask);

	/* No need to flush userspace segments if the mm doesnt change */
	if (prev == next)
		return;

#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile ("dssall");
#endif /* CONFIG_ALTIVEC */

	if (cpu_has_feature(CPU_FTR_SLB))
		switch_slb(tsk, next);
	else
		switch_stab(tsk, next);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm(prev, next, current);
	local_irq_restore(flags);
}

/* VSID allocation
 * ===============
 *
 * We first generate a 36-bit "proto-VSID".  For kernel addresses this
 * is equal to the ESID, for user addresses it is:
 *	(context << 15) | (esid & 0x7fff)
 *
 * The two forms are distinguishable because the top bit is 0 for user
 * addresses, whereas the top two bits are 1 for kernel addresses.
 * Proto-VSIDs with the top two bits equal to 0b10 are reserved for
 * now.
 *
 * The proto-VSIDs are then scrambled into real VSIDs with the
 * multiplicative hash:
 *
 *	VSID = (proto-VSID * VSID_MULTIPLIER) % VSID_MODULUS
 *	where	VSID_MULTIPLIER = 268435399 = 0xFFFFFC7
 *		VSID_MODULUS = 2^36-1 = 0xFFFFFFFFF
 *
 * This scramble is only well defined for proto-VSIDs below
 * 0xFFFFFFFFF, so both proto-VSID and actual VSID 0xFFFFFFFFF are
 * reserved.  VSID_MULTIPLIER is prime, so in particular it is
 * co-prime to VSID_MODULUS, making this a 1:1 scrambling function.
 * Because the modulus is 2^n-1 we can compute it efficiently without
 * a divide or extra multiply (see below).
 *
 * This scheme has several advantages over older methods:
 *
 * 	- We have VSIDs allocated for every kernel address
 * (i.e. everything above 0xC000000000000000), except the very top
 * segment, which simplifies several things.
 *
 * 	- We allow for 15 significant bits of ESID and 20 bits of
 * context for user addresses.  i.e. 8T (43 bits) of address space for
 * up to 1M contexts (although the page table structure and context
 * allocation will need changes to take advantage of this).
 *
 * 	- The scramble function gives robust scattering in the hash
 * table (at least based on some initial results).  The previous
 * method was more susceptible to pathological cases giving excessive
 * hash collisions.
 */

/*
 * WARNING - If you change these you must make sure the asm
 * implementations in slb_allocate(), do_stab_bolted and mmu.h
 * (ASM_VSID_SCRAMBLE macro) are changed accordingly.
 *
 * You'll also need to change the precomputed VSID values in head.S
 * which are used by the iSeries firmware.
 */

static inline unsigned long vsid_scramble(unsigned long protovsid)
{
#if 0
	/* The code below is equivalent to this function for arguments
	 * < 2^VSID_BITS, which is all this should ever be called
	 * with.  However gcc is not clever enough to compute the
	 * modulus (2^n-1) without a second multiply. */
	return ((protovsid * VSID_MULTIPLIER) % VSID_MODULUS);
#else /* 1 */
	unsigned long x;

	x = protovsid * VSID_MULTIPLIER;
	x = (x >> VSID_BITS) + (x & VSID_MODULUS);
	return (x + ((x+1) >> VSID_BITS)) & VSID_MODULUS;
#endif /* 1 */
}

/* This is only valid for addresses >= KERNELBASE */
static inline unsigned long get_kernel_vsid(unsigned long ea)
{
	return vsid_scramble(ea >> SID_SHIFT);
}

/* This is only valid for user addresses (which are below 2^41) */
static inline unsigned long get_vsid(unsigned long context, unsigned long ea)
{
	return vsid_scramble((context << USER_ESID_BITS)
			     | (ea >> SID_SHIFT));
}

#endif /* __PPC64_MMU_CONTEXT_H */
