/*
 * PowerPC memory management structures
 *
 * Dave Engebretsen & Mike Corrigan <{engebret|mikejc}@us.ibm.com>
 *   PPC64 rework.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _PPC64_MMU_H_
#define _PPC64_MMU_H_

#include <linux/config.h>
#include <asm/page.h>

/*
 * Segment table
 */

#define STE_ESID_V	0x80
#define STE_ESID_KS	0x20
#define STE_ESID_KP	0x10
#define STE_ESID_N	0x08

#define STE_VSID_SHIFT	12

/* Location of cpu0's segment table */
#define STAB0_PAGE	0x9
#define STAB0_PHYS_ADDR	(STAB0_PAGE<<PAGE_SHIFT)
#define STAB0_VIRT_ADDR	(KERNELBASE+STAB0_PHYS_ADDR)

/*
 * SLB
 */

#define SLB_NUM_BOLTED		3
#define SLB_CACHE_ENTRIES	8

/* Bits in the SLB ESID word */
#define SLB_ESID_V		ASM_CONST(0x0000000008000000) /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT		12
#define SLB_VSID_KS		ASM_CONST(0x0000000000000800)
#define SLB_VSID_KP		ASM_CONST(0x0000000000000400)
#define SLB_VSID_N		ASM_CONST(0x0000000000000200) /* no-execute */
#define SLB_VSID_L		ASM_CONST(0x0000000000000100) /* largepage 16M */
#define SLB_VSID_C		ASM_CONST(0x0000000000000080) /* class */

#define SLB_VSID_KERNEL		(SLB_VSID_KP|SLB_VSID_C)
#define SLB_VSID_USER		(SLB_VSID_KP|SLB_VSID_KS)

/*
 * Hash table
 */

#define HPTES_PER_GROUP 8

/* Values for PP (assumes Ks=0, Kp=1) */
/* pp0 will always be 0 for linux     */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

#ifndef __ASSEMBLY__

/* Hardware Page Table Entry */
typedef struct {
	unsigned long avpn:57; /* vsid | api == avpn  */
	unsigned long :     2; /* Software use */
	unsigned long bolted: 1; /* HPTE is "bolted" */
	unsigned long lock: 1; /* lock on pSeries SMP */
	unsigned long l:    1; /* Virtual page is large (L=1) or 4 KB (L=0) */
	unsigned long h:    1; /* Hash function identifier */
	unsigned long v:    1; /* Valid (v=1) or invalid (v=0) */
} Hpte_dword0;

typedef struct {
	unsigned long pp0:  1; /* Page protection bit 0 */
	unsigned long ts:   1; /* Tag set bit */
	unsigned long rpn: 50; /* Real page number */
	unsigned long :     2; /* Reserved */
	unsigned long ac:   1; /* Address compare */ 
	unsigned long r:    1; /* Referenced */
	unsigned long c:    1; /* Changed */
	unsigned long w:    1; /* Write-thru cache mode */
	unsigned long i:    1; /* Cache inhibited */
	unsigned long m:    1; /* Memory coherence required */
	unsigned long g:    1; /* Guarded */
	unsigned long n:    1; /* No-execute */
	unsigned long pp:   2; /* Page protection bits 1:2 */
} Hpte_dword1;

typedef struct {
	char padding[6];	   	/* padding */
	unsigned long :       6;	/* padding */ 
	unsigned long flags: 10;	/* HPTE flags */
} Hpte_dword1_flags;

typedef struct {
	union {
		unsigned long dword0;
		Hpte_dword0   dw0;
	} dw0;

	union {
		unsigned long dword1;
		Hpte_dword1 dw1;
		Hpte_dword1_flags flags;
	} dw1;
} HPTE; 

extern HPTE *		htab_address;
extern unsigned long	htab_hash_mask;

static inline unsigned long hpt_hash(unsigned long vpn, int large)
{
	unsigned long vsid;
	unsigned long page;

	if (large) {
		vsid = vpn >> 4;
		page = vpn & 0xf;
	} else {
		vsid = vpn >> 16;
		page = vpn & 0xffff;
	}

	return (vsid & 0x7fffffffffUL) ^ page;
}

static inline void __tlbie(unsigned long va, int large)
{
	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);

	if (large) {
		va &= HPAGE_MASK;
		asm volatile("tlbie %0,1" : : "r"(va) : "memory");
	} else {
		va &= PAGE_MASK;
		asm volatile("tlbie %0,0" : : "r"(va) : "memory");
	}
}

static inline void tlbie(unsigned long va, int large)
{
	asm volatile("ptesync": : :"memory");
	__tlbie(va, large);
	asm volatile("eieio; tlbsync; ptesync": : :"memory");
}

static inline void __tlbiel(unsigned long va)
{
	/* clear top 16 bits, non SLS segment */
	va &= ~(0xffffULL << 48);
	va &= PAGE_MASK;

	/* 
	 * Thanks to Alan Modra we are now able to use machine specific 
	 * assembly instructions (like tlbiel) by using the gas -many flag.
	 * However we have to support older toolchains so for the moment 
	 * we hardwire it.
	 */
#if 0
	asm volatile("tlbiel %0" : : "r"(va) : "memory");
#else
	asm volatile(".long 0x7c000224 | (%0 << 11)" : : "r"(va) : "memory");
#endif
}

static inline void tlbiel(unsigned long va)
{
	asm volatile("ptesync": : :"memory");
	__tlbiel(va);
	asm volatile("ptesync": : :"memory");
}

/*
 * Handle a fault by adding an HPTE. If the address can't be determined
 * to be valid via Linux page tables, return 1. If handled return 0
 */
extern int __hash_page(unsigned long ea, unsigned long access,
		       unsigned long vsid, pte_t *ptep, unsigned long trap,
		       int local);

extern void htab_finish_init(void);

extern void hpte_init_native(void);
extern void hpte_init_lpar(void);
extern void hpte_init_iSeries(void);

extern long pSeries_lpar_hpte_insert(unsigned long hpte_group,
				     unsigned long va, unsigned long prpn,
				     int secondary, unsigned long hpteflags,
				     int bolted, int large);
extern long native_hpte_insert(unsigned long hpte_group, unsigned long va,
			       unsigned long prpn, int secondary,
			       unsigned long hpteflags, int bolted, int large);

#endif /* __ASSEMBLY__ */

/*
 * VSID allocation
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
 * implementations in slb_allocate (slb_low.S), do_stab_bolted
 * (head.S) and ASM_VSID_SCRAMBLE (below) are changed accordingly.
 *
 * You'll also need to change the precomputed VSID values in head.S
 * which are used by the iSeries firmware.
 */

#define VSID_MULTIPLIER	ASM_CONST(200730139)	/* 28-bit prime */
#define VSID_BITS	36
#define VSID_MODULUS	((1UL<<VSID_BITS)-1)

#define CONTEXT_BITS	20
#define USER_ESID_BITS	15

/*
 * This macro generates asm code to compute the VSID scramble
 * function.  Used in slb_allocate() and do_stab_bolted.  The function
 * computed is: (protovsid*VSID_MULTIPLIER) % VSID_MODULUS
 *
 *	rt = register continaing the proto-VSID and into which the
 *		VSID will be stored
 *	rx = scratch register (clobbered)
 *
 * 	- rt and rx must be different registers
 * 	- The answer will end up in the low 36 bits of rt.  The higher
 * 	  bits may contain other garbage, so you may need to mask the
 * 	  result.
 */
#define ASM_VSID_SCRAMBLE(rt, rx)	\
	lis	rx,VSID_MULTIPLIER@h;					\
	ori	rx,rx,VSID_MULTIPLIER@l;				\
	mulld	rt,rt,rx;		/* rt = rt * MULTIPLIER */	\
									\
	srdi	rx,rt,VSID_BITS;					\
	clrldi	rt,rt,(64-VSID_BITS);					\
	add	rt,rt,rx;		/* add high and low bits */	\
	/* Now, r3 == VSID (mod 2^36-1), and lies between 0 and		\
	 * 2^36-1+2^28-1.  That in particular means that if r3 >=	\
	 * 2^36-1, then r3+1 has the 2^36 bit set.  So, if r3+1 has	\
	 * the bit clear, r3 already has the answer we want, if it	\
	 * doesn't, the answer is the low 36 bits of r3+1.  So in all	\
	 * cases the answer is the low 36 bits of (r3 + ((r3+1) >> 36))*/\
	addi	rx,rt,1;						\
	srdi	rx,rx,VSID_BITS;	/* extract 2^36 bit */		\
	add	rt,rt,rx


#ifndef __ASSEMBLY__

typedef unsigned long mm_context_id_t;

typedef struct {
	mm_context_id_t id;
#ifdef CONFIG_HUGETLB_PAGE
	pgd_t *huge_pgdir;
	u16 htlb_segs; /* bitmask */
#endif
} mm_context_t;


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

#endif /* __ASSEMBLY */

#endif /* _PPC64_MMU_H_ */
