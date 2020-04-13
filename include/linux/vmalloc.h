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
#define VM_NO_GUARD		0x00000040      /* don't add guard page */
#define VM_KASAN		0x00000080      /* has allocated kasan shadow memory */

/*
 * VM_KASAN is used slighly differently depending on CONFIG_KASAN_VMALLOC.
 *
 * If IS_ENABLED(CONFIG_KASAN_VMALLOC), VM_KASAN is set on a vm_struct after
 * shadow memory has been mapped. It's used to handle allocation errors so that
 * we don't try to poision shadow on free if it was never allocated.
 *
 * Otherwise, VM_KASAN is set for kasan_module_alloc() allocations and used to
 * determine which allocations need the module shadow freed.
 */

/*
 * Memory with VM_FLUSH_RESET_PERMS cannot be freed in an interrupt or with
 * vfree_atomic().
 */
#define VM_FLUSH_RESET_PERMS	0x00000100      /* Reset direct map and flush TLB on unmap */

/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overriden by arch-specific value.
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
	 * The following three variables can be packed, because
	 * a vmap_area object is always one of the three states:
	 *    1) in "free" tree (root is vmap_area_root)
	 *    2) in "busy" tree (root is free_vmap_area_root)
	 *    3) in purge list  (head is vmap_purge_list)
	 */
	union {
		unsigned long subtree_max_size; /* in "free" tree */
		struct vm_struct *vm;           /* in "busy" tree */
		struct llist_node purge_list;   /* in purge list */
	};
};

/*
 *	Highlevel APIs for driver use
 */
extern void vm_unmap_ram(const void *mem, unsigned int count);
extern void *vm_map_ram(struct page **pages, unsigned int count,
				int node, pgprot_t prot);
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

extern void *vmalloc(unsigned long size);
extern void *vzalloc(unsigned long size);
extern void *vmalloc_user(unsigned long size);
extern void *vmalloc_node(unsigned long size, int node);
extern void *vzalloc_node(unsigned long size, int node);
extern void *vmalloc_user_node_flags(unsigned long size, int node, gfp_t flags);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *vmalloc_32_user(unsigned long size);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller);
#ifndef CONFIG_MMU
extern void *__vmalloc_node_flags(unsigned long size, int node, gfp_t flags);
static inline void *__vmalloc_node_flags_caller(unsigned long size, int node,
						gfp_t flags, void *caller)
{
	return __vmalloc_node_flags(size, node, flags);
}
#else
extern void *__vmalloc_node_flags_caller(unsigned long size,
					 int node, gfp_t flags, void *caller);
#endif

extern void vfree(const void *addr);
extern void vfree_atomic(const void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(const void *addr);

extern int remap_vmalloc_range_partial(struct vm_area_struct *vma,
				       unsigned long uaddr, void *kaddr,
				       unsigned long size);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);
void vmalloc_sync_mappings(void);
void vmalloc_sync_unmappings(void);

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
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct *__get_vm_area_caller(unsigned long size,
					unsigned long flags,
					unsigned long start, unsigned long end,
					const void *caller);
extern struct vm_struct *remove_vm_area(const void *addr);
extern struct vm_struct *find_vm_area(const void *addr);

extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page **pages);
#ifdef CONFIG_MMU
extern int map_kernel_range_noflush(unsigned long start, unsigned long size,
				    pgprot_t prot, struct page **pages);
extern void unmap_kernel_range_noflush(unsigned long addr, unsigned long size);
extern void unmap_kernel_range(unsigned long addr, unsigned long size);
static inline void set_vm_flush_reset_perms(void *addr)
{
	struct vm_struct *vm = find_vm_area(addr);

	if (vm)
		vm->flags |= VM_FLUSH_RESET_PERMS;
}
#else
static inline int
map_kernel_range_noflush(unsigned long start, unsigned long size,
			pgprot_t prot, struct page **pages)
{
	return size >> PAGE_SHIFT;
}
static inline void
unmap_kernel_range_noflush(unsigned long addr, unsigned long size)
{
}
static inline void
unmap_kernel_range(unsigned long addr, unsigned long size)
{
}
static inline void set_vm_flush_reset_perms(void *addr)
{
}
#endif

/* Allocate/destroy a 'vmalloc' VM area. */
extern struct vm_struct *alloc_vm_area(size_t size, pte_t **ptes);
extern void free_vm_area(struct vm_struct *area);

/* for /dev/kmem */
extern long vread(char *buf, char *addr, unsigned long count);
extern long vwrite(char *buf, char *addr, unsigned long count);

/*
 *	Internals.  Dont't use..
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

#endif /* _LINUX_VMALLOC_H */
