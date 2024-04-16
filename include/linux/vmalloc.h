/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <asm/page.h>		/* pgprot_t */
#include <linux/rbtree.h>
#include <linux/overflow.h>

#include <asm/vmalloc.h>

struct vm_area_struct;		/* vma defining user mapping in mm_types.h */
struct notifier_block;		/* in notifier.h */

/* bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		0x00000001	/* ioremap() and friends */
#define VM_ALLOC		0x00000002	/* vmalloc() */
#define VM_MAP			0x00000004	/* vmap()ed pages */
#define VM_USERMAP		0x00000008	/* suitable for remap_vmalloc_range */
#define VM_DMA_COHERENT		0x00000010	/* dma_alloc_coherent */
#define VM_UNINITIALIZED	0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		0x00000040      /* ***DANGEROUS*** don't add guard page */
#define VM_KASAN		0x00000080      /* has allocated kasan shadow memory */
#define VM_FLUSH_RESET_PERMS	0x00000100	/* reset direct map and flush TLB on unmap, can't be freed in atomic context */
#define VM_MAP_PUT_PAGES	0x00000200	/* put pages and free array in vfree */
#define VM_ALLOW_HUGE_VMAP	0x00000400      /* Allow for huge pages on archs with HAVE_ARCH_HUGE_VMALLOC */

#if (defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)) && \
	!defined(CONFIG_KASAN_VMALLOC)
#define VM_DEFER_KMEMLEAK	0x00000800	/* defer kmemleak object creation */
#else
#define VM_DEFER_KMEMLEAK	0
#endif

/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overridden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */
#endif

struct vm_struct {
	struct vm_struct	*next;
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
#ifdef CONFIG_HAVE_ARCH_HUGE_VMALLOC
	unsigned int		page_order;
#endif
	unsigned int		nr_pages;
	phys_addr_t		phys_addr;
	const void		*caller;
};

struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;         /* address sorted rbtree */
	struct list_head list;          /* address sorted list */

	/*
	 * The following two variables can be packed, because
	 * a vmap_area object can be either:
	 *    1) in "free" tree (root is free_vmap_area_root)
	 *    2) or "busy" tree (root is vmap_area_root)
	 */
	union {
		unsigned long subtree_max_size; /* in "free" tree */
		struct vm_struct *vm;           /* in "busy" tree */
	};
};

/* archs that select HAVE_ARCH_HUGE_VMAP should override one or more of these */
#ifndef arch_vmap_p4d_supported
static inline bool arch_vmap_p4d_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pud_supported
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pmd_supported
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pte_range_map_size
static inline unsigned long arch_vmap_pte_range_map_size(unsigned long addr, unsigned long end,
							 u64 pfn, unsigned int max_page_shift)
{
	return PAGE_SIZE;
}
#endif

#ifndef arch_vmap_pte_supported_shift
static inline int arch_vmap_pte_supported_shift(unsigned long size)
{
	return PAGE_SHIFT;
}
#endif

#ifndef arch_vmap_pgprot_tagged
static inline pgprot_t arch_vmap_pgprot_tagged(pgprot_t prot)
{
	return prot;
}
#endif

/*
 *	Highlevel APIs for driver use
 */
extern void vm_unmap_ram(const void *mem, unsigned int count);
extern void *vm_map_ram(struct page **pages, unsigned int count, int node);
extern void vm_unmap_aliases(void);

#ifdef CONFIG_MMU
extern void __init vmalloc_init(void);
extern unsigned long vmalloc_nr_pages(void);
#else
static inline void vmalloc_init(void)
{
}
static inline unsigned long vmalloc_nr_pages(void) { return 0; }
#endif

extern void *vmalloc(unsigned long size) __alloc_size(1);
extern void *vzalloc(unsigned long size) __alloc_size(1);
extern void *vmalloc_user(unsigned long size) __alloc_size(1);
extern void *vmalloc_node(unsigned long size, int node) __alloc_size(1);
extern void *vzalloc_node(unsigned long size, int node) __alloc_size(1);
extern void *vmalloc_32(unsigned long size) __alloc_size(1);
extern void *vmalloc_32_user(unsigned long size) __alloc_size(1);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask) __alloc_size(1);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller) __alloc_size(1);
void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);
void *vmalloc_huge(unsigned long size, gfp_t gfp_mask) __alloc_size(1);

extern void *__vmalloc_array(size_t n, size_t size, gfp_t flags) __alloc_size(1, 2);
extern void *vmalloc_array(size_t n, size_t size) __alloc_size(1, 2);
extern void *__vcalloc(size_t n, size_t size, gfp_t flags) __alloc_size(1, 2);
extern void *vcalloc(size_t n, size_t size) __alloc_size(1, 2);

extern void vfree(const void *addr);
extern void vfree_atomic(const void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
void *vmap_pfn(unsigned long *pfns, unsigned int count, pgprot_t prot);
extern void vunmap(const void *addr);

extern int remap_vmalloc_range_partial(struct vm_area_struct *vma,
				       unsigned long uaddr, void *kaddr,
				       unsigned long pgoff, unsigned long size);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);

/*
 * Architectures can set this mask to a combination of PGTBL_P?D_MODIFIED values
 * and let generic vmalloc and ioremap code know when arch_sync_kernel_mappings()
 * needs to be called.
 */
#ifndef ARCH_PAGE_TABLE_SYNC_MASK
#define ARCH_PAGE_TABLE_SYNC_MASK 0
#endif

/*
 * There is no default implementation for arch_sync_kernel_mappings(). It is
 * relied upon the compiler to optimize calls out if ARCH_PAGE_TABLE_SYNC_MASK
 * is 0.
 */
void arch_sync_kernel_mappings(unsigned long start, unsigned long end);

/*
 *	Lowlevel-APIs (not for driver use!)
 */

static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	if (!(area->flags & VM_NO_GUARD))
		/* return actual size without guard page */
		return area->size - PAGE_SIZE;
	else
		return area->size;

}

extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *get_vm_area_caller(unsigned long size,
					unsigned long flags, const void *caller);
extern struct vm_struct *__get_vm_area_caller(unsigned long size,
					unsigned long flags,
					unsigned long start, unsigned long end,
					const void *caller);
void free_vm_area(struct vm_struct *area);
extern struct vm_struct *remove_vm_area(const void *addr);
extern struct vm_struct *find_vm_area(const void *addr);
struct vmap_area *find_vmap_area(unsigned long addr);

static inline bool is_vm_area_hugepages(const void *addr)
{
	/*
	 * This may not 100% tell if the area is mapped with > PAGE_SIZE
	 * page table entries, if for some reason the architecture indicates
	 * larger sizes are available but decides not to use them, nothing
	 * prevents that. This only indicates the size of the physical page
	 * allocated in the vmalloc layer.
	 */
#ifdef CONFIG_HAVE_ARCH_HUGE_VMALLOC
	return find_vm_area(addr)->page_order > 0;
#else
	return false;
#endif
}

#ifdef CONFIG_MMU
void vunmap_range(unsigned long addr, unsigned long end);
static inline void set_vm_flush_reset_perms(void *addr)
{
	struct vm_struct *vm = find_vm_area(addr);

	if (vm)
		vm->flags |= VM_FLUSH_RESET_PERMS;
}

#else
static inline void set_vm_flush_reset_perms(void *addr)
{
}
#endif

/* for /proc/kcore */
extern long vread(char *buf, char *addr, unsigned long count);

/*
 *	Internals.  Don't use..
 */
extern struct list_head vmap_area_list;
extern __init void vm_area_add_early(struct vm_struct *vm);
extern __init void vm_area_register_early(struct vm_struct *vm, size_t align);

#ifdef CONFIG_SMP
# ifdef CONFIG_MMU
struct vm_struct **pcpu_get_vm_areas(const unsigned long *offsets,
				     const size_t *sizes, int nr_vms,
				     size_t align);

void pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms);
# else
static inline struct vm_struct **
pcpu_get_vm_areas(const unsigned long *offsets,
		const size_t *sizes, int nr_vms,
		size_t align)
{
	return NULL;
}

static inline void
pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms)
{
}
# endif
#endif

#ifdef CONFIG_MMU
#define VMALLOC_TOTAL (VMALLOC_END - VMALLOC_START)
#else
#define VMALLOC_TOTAL 0UL
#endif

int register_vmap_purge_notifier(struct notifier_block *nb);
int unregister_vmap_purge_notifier(struct notifier_block *nb);

#if defined(CONFIG_MMU) && defined(CONFIG_PRINTK)
bool vmalloc_dump_obj(void *object);
#else
static inline bool vmalloc_dump_obj(void *object) { return false; }
#endif

#endif /* _LINUX_VMALLOC_H */
