#ifndef __XEN_PAGE_H
#define __XEN_PAGE_H

#include <linux/pfn.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include <xen/features.h>

#ifdef CONFIG_X86_PAE
/* Xen machine address */
typedef struct xmaddr {
	unsigned long long maddr;
} xmaddr_t;

/* Xen pseudo-physical address */
typedef struct xpaddr {
	unsigned long long paddr;
} xpaddr_t;
#else
/* Xen machine address */
typedef struct xmaddr {
	unsigned long maddr;
} xmaddr_t;

/* Xen pseudo-physical address */
typedef struct xpaddr {
	unsigned long paddr;
} xpaddr_t;
#endif

#define XMADDR(x)	((xmaddr_t) { .maddr = (x) })
#define XPADDR(x)	((xpaddr_t) { .paddr = (x) })

/**** MACHINE <-> PHYSICAL CONVERSION MACROS ****/
#define INVALID_P2M_ENTRY	(~0UL)
#define FOREIGN_FRAME_BIT	(1UL<<31)
#define FOREIGN_FRAME(m)	((m) | FOREIGN_FRAME_BIT)

extern unsigned long *phys_to_machine_mapping;

static inline unsigned long pfn_to_mfn(unsigned long pfn)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return pfn;

	return phys_to_machine_mapping[(unsigned int)(pfn)] &
		~FOREIGN_FRAME_BIT;
}

static inline int phys_to_machine_mapping_valid(unsigned long pfn)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 1;

	return (phys_to_machine_mapping[pfn] != INVALID_P2M_ENTRY);
}

static inline unsigned long mfn_to_pfn(unsigned long mfn)
{
	unsigned long pfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return mfn;

#if 0
	if (unlikely((mfn >> machine_to_phys_order) != 0))
		return max_mapnr;
#endif

	pfn = 0;
	/*
	 * The array access can fail (e.g., device space beyond end of RAM).
	 * In such cases it doesn't matter what we return (we return garbage),
	 * but we must handle the fault without crashing!
	 */
	__get_user(pfn, &machine_to_phys_mapping[mfn]);

	return pfn;
}

static inline xmaddr_t phys_to_machine(xpaddr_t phys)
{
	unsigned offset = phys.paddr & ~PAGE_MASK;
	return XMADDR(PFN_PHYS((u64)pfn_to_mfn(PFN_DOWN(phys.paddr))) | offset);
}

static inline xpaddr_t machine_to_phys(xmaddr_t machine)
{
	unsigned offset = machine.maddr & ~PAGE_MASK;
	return XPADDR(PFN_PHYS((u64)mfn_to_pfn(PFN_DOWN(machine.maddr))) | offset);
}

/*
 * We detect special mappings in one of two ways:
 *  1. If the MFN is an I/O page then Xen will set the m2p entry
 *     to be outside our maximum possible pseudophys range.
 *  2. If the MFN belongs to a different domain then we will certainly
 *     not have MFN in our p2m table. Conversely, if the page is ours,
 *     then we'll have p2m(m2p(MFN))==MFN.
 * If we detect a special mapping then it doesn't have a 'struct page'.
 * We force !pfn_valid() by returning an out-of-range pointer.
 *
 * NB. These checks require that, for any MFN that is not in our reservation,
 * there is no PFN such that p2m(PFN) == MFN. Otherwise we can get confused if
 * we are foreign-mapping the MFN, and the other domain as m2p(MFN) == PFN.
 * Yikes! Various places must poke in INVALID_P2M_ENTRY for safety.
 *
 * NB2. When deliberately mapping foreign pages into the p2m table, you *must*
 *      use FOREIGN_FRAME(). This will cause pte_pfn() to choke on it, as we
 *      require. In all the cases we care about, the FOREIGN_FRAME bit is
 *      masked (e.g., pfn_to_mfn()) so behaviour there is correct.
 */
static inline unsigned long mfn_to_local_pfn(unsigned long mfn)
{
	extern unsigned long max_mapnr;
	unsigned long pfn = mfn_to_pfn(mfn);
	if ((pfn < max_mapnr)
	    && !xen_feature(XENFEAT_auto_translated_physmap)
	    && (phys_to_machine_mapping[pfn] != mfn))
		return max_mapnr; /* force !pfn_valid() */
	return pfn;
}

static inline void set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		BUG_ON(pfn != mfn && mfn != INVALID_P2M_ENTRY);
		return;
	}
	phys_to_machine_mapping[pfn] = mfn;
}

/* VIRT <-> MACHINE conversion */
#define virt_to_machine(v)	(phys_to_machine(XPADDR(__pa(v))))
#define virt_to_mfn(v)		(pfn_to_mfn(PFN_DOWN(__pa(v))))
#define mfn_to_virt(m)		(__va(mfn_to_pfn(m) << PAGE_SHIFT))

#ifdef CONFIG_X86_PAE
#define pte_mfn(_pte) (((_pte).pte_low >> PAGE_SHIFT) |			\
		       (((_pte).pte_high & 0xfff) << (32-PAGE_SHIFT)))

static inline pte_t mfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte;

	pte.pte_high = (page_nr >> (32 - PAGE_SHIFT)) |
		(pgprot_val(pgprot) >> 32);
	pte.pte_high &= (__supported_pte_mask >> 32);
	pte.pte_low = ((page_nr << PAGE_SHIFT) | pgprot_val(pgprot));
	pte.pte_low &= __supported_pte_mask;

	return pte;
}

static inline unsigned long long pte_val_ma(pte_t x)
{
	return x.pte;
}
#define pmd_val_ma(v) ((v).pmd)
#define pud_val_ma(v) ((v).pgd.pgd)
#define __pte_ma(x)	((pte_t) { .pte = (x) })
#define __pmd_ma(x)	((pmd_t) { (x) } )
#else  /* !X86_PAE */
#define pte_mfn(_pte) ((_pte).pte_low >> PAGE_SHIFT)
#define mfn_pte(pfn, prot)	__pte_ma(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pte_val_ma(x)	((x).pte)
#define pmd_val_ma(v)	((v).pud.pgd.pgd)
#define __pte_ma(x)	((pte_t) { (x) } )
#endif	/* CONFIG_X86_PAE */

#define pgd_val_ma(x)	((x).pgd)


xmaddr_t arbitrary_virt_to_machine(unsigned long address);
void make_lowmem_page_readonly(void *vaddr);
void make_lowmem_page_readwrite(void *vaddr);

#endif /* __XEN_PAGE_H */
