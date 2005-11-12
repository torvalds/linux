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
#include <asm/asm-compat.h>
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
#define STAB0_PAGE	0x6
#define STAB0_PHYS_ADDR	(STAB0_PAGE<<12)

#ifndef __ASSEMBLY__
extern char initial_stab[];
#endif /* ! __ASSEMBLY */

/*
 * SLB
 */

#define SLB_NUM_BOLTED		3
#define SLB_CACHE_ENTRIES	8

/* Bits in the SLB ESID word */
#define SLB_ESID_V		ASM_CONST(0x0000000008000000) /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT		12
#define SLB_VSID_B		ASM_CONST(0xc000000000000000)
#define SLB_VSID_B_256M		ASM_CONST(0x0000000000000000)
#define SLB_VSID_B_1T		ASM_CONST(0x4000000000000000)
#define SLB_VSID_KS		ASM_CONST(0x0000000000000800)
#define SLB_VSID_KP		ASM_CONST(0x0000000000000400)
#define SLB_VSID_N		ASM_CONST(0x0000000000000200) /* no-execute */
#define SLB_VSID_L		ASM_CONST(0x0000000000000100)
#define SLB_VSID_C		ASM_CONST(0x0000000000000080) /* class */
#define SLB_VSID_LP		ASM_CONST(0x0000000000000030)
#define SLB_VSID_LP_00		ASM_CONST(0x0000000000000000)
#define SLB_VSID_LP_01		ASM_CONST(0x0000000000000010)
#define SLB_VSID_LP_10		ASM_CONST(0x0000000000000020)
#define SLB_VSID_LP_11		ASM_CONST(0x0000000000000030)
#define SLB_VSID_LLP		(SLB_VSID_L|SLB_VSID_LP)

#define SLB_VSID_KERNEL		(SLB_VSID_KP)
#define SLB_VSID_USER		(SLB_VSID_KP|SLB_VSID_KS|SLB_VSID_C)

#define SLBIE_C			(0x08000000)

/*
 * Hash table
 */

#define HPTES_PER_GROUP 8

#define HPTE_V_AVPN_SHIFT	7
#define HPTE_V_AVPN		ASM_CONST(0xffffffffffffff80)
#define HPTE_V_AVPN_VAL(x)	(((x) & HPTE_V_AVPN) >> HPTE_V_AVPN_SHIFT)
#define HPTE_V_COMPARE(x,y)	(!(((x) ^ (y)) & HPTE_V_AVPN))
#define HPTE_V_BOLTED		ASM_CONST(0x0000000000000010)
#define HPTE_V_LOCK		ASM_CONST(0x0000000000000008)
#define HPTE_V_LARGE		ASM_CONST(0x0000000000000004)
#define HPTE_V_SECONDARY	ASM_CONST(0x0000000000000002)
#define HPTE_V_VALID		ASM_CONST(0x0000000000000001)

#define HPTE_R_PP0		ASM_CONST(0x8000000000000000)
#define HPTE_R_TS		ASM_CONST(0x4000000000000000)
#define HPTE_R_RPN_SHIFT	12
#define HPTE_R_RPN		ASM_CONST(0x3ffffffffffff000)
#define HPTE_R_FLAGS		ASM_CONST(0x00000000000003ff)
#define HPTE_R_PP		ASM_CONST(0x0000000000000003)
#define HPTE_R_N		ASM_CONST(0x0000000000000004)

/* Values for PP (assumes Ks=0, Kp=1) */
/* pp0 will always be 0 for linux     */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long v;
	unsigned long r;
} hpte_t;

extern hpte_t *htab_address;
extern unsigned long htab_hash_mask;

/*
 * Page size definition
 *
 *    shift : is the "PAGE_SHIFT" value for that page size
 *    sllp  : is a bit mask with the value of SLB L || LP to be or'ed
 *            directly to a slbmte "vsid" value
 *    penc  : is the HPTE encoding mask for the "LP" field:
 *
 */
struct mmu_psize_def
{
	unsigned int	shift;	/* number of bits */
	unsigned int	penc;	/* HPTE encoding */
	unsigned int	tlbiel;	/* tlbiel supported for that page size */
	unsigned long	avpnm;	/* bits to mask out in AVPN in the HPTE */
	unsigned long	sllp;	/* SLB L||LP (exact mask to use in slbmte) */
};

#endif /* __ASSEMBLY__ */

/*
 * The kernel use the constants below to index in the page sizes array.
 * The use of fixed constants for this purpose is better for performances
 * of the low level hash refill handlers.
 *
 * A non supported page size has a "shift" field set to 0
 *
 * Any new page size being implemented can get a new entry in here. Whether
 * the kernel will use it or not is a different matter though. The actual page
 * size used by hugetlbfs is not defined here and may be made variable
 */

#define MMU_PAGE_4K		0	/* 4K */
#define MMU_PAGE_64K		1	/* 64K */
#define MMU_PAGE_64K_AP		2	/* 64K Admixed (in a 4K segment) */
#define MMU_PAGE_1M		3	/* 1M */
#define MMU_PAGE_16M		4	/* 16M */
#define MMU_PAGE_16G		5	/* 16G */
#define MMU_PAGE_COUNT		6

#ifndef __ASSEMBLY__

/*
 * The current system page sizes
 */
extern struct mmu_psize_def mmu_psize_defs[MMU_PAGE_COUNT];
extern int mmu_linear_psize;
extern int mmu_virtual_psize;

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The page size index of the huge pages for use by hugetlbfs
 */
extern int mmu_huge_psize;

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * This function sets the AVPN and L fields of the HPTE  appropriately
 * for the page size
 */
static inline unsigned long hpte_encode_v(unsigned long va, int psize)
{
	unsigned long v =
	v = (va >> 23) & ~(mmu_psize_defs[psize].avpnm);
	v <<= HPTE_V_AVPN_SHIFT;
	if (psize != MMU_PAGE_4K)
		v |= HPTE_V_LARGE;
	return v;
}

/*
 * This function sets the ARPN, and LP fields of the HPTE appropriately
 * for the page size. We assume the pa is already "clean" that is properly
 * aligned for the requested page size
 */
static inline unsigned long hpte_encode_r(unsigned long pa, int psize)
{
	unsigned long r;

	/* A 4K page needs no special encoding */
	if (psize == MMU_PAGE_4K)
		return pa & HPTE_R_RPN;
	else {
		unsigned int penc = mmu_psize_defs[psize].penc;
		unsigned int shift = mmu_psize_defs[psize].shift;
		return (pa & ~((1ul << shift) - 1)) | (penc << 12);
	}
	return r;
}

/*
 * This hashes a virtual address for a 256Mb segment only for now
 */

static inline unsigned long hpt_hash(unsigned long va, unsigned int shift)
{
	return ((va >> 28) & 0x7fffffffffUL) ^ ((va & 0x0fffffffUL) >> shift);
}

extern int __hash_page_4K(unsigned long ea, unsigned long access,
			  unsigned long vsid, pte_t *ptep, unsigned long trap,
			  unsigned int local);
extern int __hash_page_64K(unsigned long ea, unsigned long access,
			   unsigned long vsid, pte_t *ptep, unsigned long trap,
			   unsigned int local);
struct mm_struct;
extern int hash_huge_page(struct mm_struct *mm, unsigned long access,
			  unsigned long ea, unsigned long vsid, int local);

extern void htab_finish_init(void);
extern int htab_bolt_mapping(unsigned long vstart, unsigned long vend,
			     unsigned long pstart, unsigned long mode,
			     int psize);

extern void htab_initialize(void);
extern void htab_initialize_secondary(void);
extern void hpte_init_native(void);
extern void hpte_init_lpar(void);
extern void hpte_init_iSeries(void);
extern void mm_init_ppc64(void);

extern long pSeries_lpar_hpte_insert(unsigned long hpte_group,
				     unsigned long va, unsigned long prpn,
				     unsigned long rflags,
				     unsigned long vflags, int psize);

extern long native_hpte_insert(unsigned long hpte_group,
			       unsigned long va, unsigned long prpn,
			       unsigned long rflags,
			       unsigned long vflags, int psize);

extern long iSeries_hpte_insert(unsigned long hpte_group,
				unsigned long va, unsigned long prpn,
				unsigned long rflags,
				unsigned long vflags, int psize);

extern void stabs_alloc(void);
extern void slb_initialize(void);
extern void stab_initialize(unsigned long stab);

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

#define CONTEXT_BITS	19
#define USER_ESID_BITS	16

#define USER_VSID_RANGE	(1UL << (USER_ESID_BITS + SID_SHIFT))

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
	u16 low_htlb_areas, high_htlb_areas;
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

#define VSID_SCRAMBLE(pvsid)	(((pvsid) * VSID_MULTIPLIER) % VSID_MODULUS)
#define KERNEL_VSID(ea)		VSID_SCRAMBLE(GET_ESID(ea))

#endif /* __ASSEMBLY */

#endif /* _PPC64_MMU_H_ */
