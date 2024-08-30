/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vma_internal.h
 *
 * Header providing userland wrappers and shims for the functionality provided
 * by mm/vma_internal.h.
 *
 * We make the header guard the same as mm/vma_internal.h, so if this shim
 * header is included, it precludes the inclusion of the kernel one.
 */

#ifndef __MM_VMA_INTERNAL_H
#define __MM_VMA_INTERNAL_H

#define __private
#define __bitwise
#define __randomize_layout

#define CONFIG_MMU
#define CONFIG_PER_VMA_LOCK

#include <stdlib.h>

#include <linux/list.h>
#include <linux/maple_tree.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>

#define VM_WARN_ON(_expr) (WARN_ON(_expr))
#define VM_WARN_ON_ONCE(_expr) (WARN_ON_ONCE(_expr))
#define VM_BUG_ON(_expr) (BUG_ON(_expr))
#define VM_BUG_ON_VMA(_expr, _vma) (BUG_ON(_expr))

#define VM_NONE		0x00000000
#define VM_READ		0x00000001
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008
#define VM_MAYREAD	0x00000010
#define VM_MAYWRITE	0x00000020
#define VM_GROWSDOWN	0x00000100
#define VM_PFNMAP	0x00000400
#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000
#define VM_DONTEXPAND	0x00040000
#define VM_ACCOUNT	0x00100000
#define VM_MIXEDMAP	0x10000000
#define VM_STACK	VM_GROWSDOWN
#define VM_SHADOW_STACK	VM_NONE
#define VM_SOFTDIRTY	0

#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)
#define VM_SPECIAL (VM_IO | VM_DONTEXPAND | VM_PFNMAP | VM_MIXEDMAP)

#define FIRST_USER_ADDRESS	0UL
#define USER_PGTABLES_CEILING	0UL

#define vma_policy(vma) NULL

#define down_write_nest_lock(sem, nest_lock)

#define pgprot_val(x)		((x).pgprot)
#define __pgprot(x)		((pgprot_t) { (x) } )

#define for_each_vma(__vmi, __vma)					\
	while (((__vma) = vma_next(&(__vmi))) != NULL)

/* The MM code likes to work with exclusive end addresses */
#define for_each_vma_range(__vmi, __vma, __end)				\
	while (((__vma) = vma_find(&(__vmi), (__end))) != NULL)

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))

#define test_and_set_bit(nr, addr) __test_and_set_bit(nr, addr)
#define test_and_clear_bit(nr, addr) __test_and_clear_bit(nr, addr)

#define TASK_SIZE ((1ul << 47)-PAGE_SIZE)

#define AS_MM_ALL_LOCKS 2

/* We hardcode this for now. */
#define sysctl_max_map_count 0x1000000UL

#define pgoff_t unsigned long
typedef unsigned long	pgprotval_t;
typedef struct pgprot { pgprotval_t pgprot; } pgprot_t;
typedef unsigned long vm_flags_t;
typedef __bitwise unsigned int vm_fault_t;

/*
 * The shared stubs do not implement this, it amounts to an fprintf(STDERR,...)
 * either way :)
 */
#define pr_warn_once pr_err

typedef struct refcount_struct {
	atomic_t refs;
} refcount_t;

struct kref {
	refcount_t refcount;
};

/*
 * Define the task command name length as enum, then it can be visible to
 * BPF programs.
 */
enum {
	TASK_COMM_LEN = 16,
};

struct task_struct {
	char comm[TASK_COMM_LEN];
	pid_t pid;
	struct mm_struct *mm;
};

struct task_struct *get_current(void);
#define current get_current()

struct anon_vma {
	struct anon_vma *root;
	struct rb_root_cached rb_root;

	/* Test fields. */
	bool was_cloned;
	bool was_unlinked;
};

struct anon_vma_chain {
	struct anon_vma *anon_vma;
	struct list_head same_vma;
};

struct anon_vma_name {
	struct kref kref;
	/* The name needs to be at the end because it is dynamically sized. */
	char name[];
};

struct vma_iterator {
	struct ma_state mas;
};

#define VMA_ITERATOR(name, __mm, __addr)				\
	struct vma_iterator name = {					\
		.mas = {						\
			.tree = &(__mm)->mm_mt,				\
			.index = __addr,				\
			.node = NULL,					\
			.status = ma_start,				\
		},							\
	}

struct address_space {
	struct rb_root_cached	i_mmap;
	unsigned long		flags;
	atomic_t		i_mmap_writable;
};

struct vm_userfaultfd_ctx {};
struct mempolicy {};
struct mmu_gather {};
struct mutex {};
#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = {}

struct mm_struct {
	struct maple_tree mm_mt;
	int map_count;			/* number of VMAs */
	unsigned long total_vm;	   /* Total pages mapped */
	unsigned long locked_vm;   /* Pages that have PG_mlocked set */
	unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
	unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
	unsigned long stack_vm;	   /* VM_STACK */
};

struct vma_lock {
	struct rw_semaphore lock;
};


struct file {
	struct address_space	*f_mapping;
};

struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

	union {
		struct {
			/* VMA covers [vm_start; vm_end) addresses within mm */
			unsigned long vm_start;
			unsigned long vm_end;
		};
#ifdef CONFIG_PER_VMA_LOCK
		struct rcu_head vm_rcu;	/* Used for deferred freeing. */
#endif
	};

	struct mm_struct *vm_mm;	/* The address space we belong to. */
	pgprot_t vm_page_prot;          /* Access permissions of this VMA. */

	/*
	 * Flags, see mm.h.
	 * To modify use vm_flags_{init|reset|set|clear|mod} functions.
	 */
	union {
		const vm_flags_t vm_flags;
		vm_flags_t __private __vm_flags;
	};

#ifdef CONFIG_PER_VMA_LOCK
	/* Flag to indicate areas detached from the mm->mm_mt tree */
	bool detached;

	/*
	 * Can only be written (using WRITE_ONCE()) while holding both:
	 *  - mmap_lock (in write mode)
	 *  - vm_lock->lock (in write mode)
	 * Can be read reliably while holding one of:
	 *  - mmap_lock (in read or write mode)
	 *  - vm_lock->lock (in read or write mode)
	 * Can be read unreliably (using READ_ONCE()) for pessimistic bailout
	 * while holding nothing (except RCU to keep the VMA struct allocated).
	 *
	 * This sequence counter is explicitly allowed to overflow; sequence
	 * counter reuse can only lead to occasional unnecessary use of the
	 * slowpath.
	 */
	int vm_lock_seq;
	struct vma_lock *vm_lock;
#endif

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 *
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	struct list_head anon_vma_chain; /* Serialized by mmap_lock &
					  * page_table_lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units */
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifdef CONFIG_ANON_VMA_NAME
	/*
	 * For private and shared anonymous mappings, a pointer to a null
	 * terminated string containing the name given to the vma, or NULL if
	 * unnamed. Serialized by mmap_lock. Use anon_vma_name to access.
	 */
	struct anon_vma_name *anon_name;
#endif
#ifdef CONFIG_SWAP
	atomic_long_t swap_readahead_info;
#endif
#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
#ifdef CONFIG_NUMA_BALANCING
	struct vma_numab_state *numab_state;	/* NUMA Balancing state */
#endif
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;

struct vm_fault {};

struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	/**
	 * @close: Called when the VMA is being removed from the MM.
	 * Context: User context.  May sleep.  Caller holds mmap_lock.
	 */
	void (*close)(struct vm_area_struct * area);
	/* Called any time before splitting to check if it's allowed */
	int (*may_split)(struct vm_area_struct *area, unsigned long addr);
	int (*mremap)(struct vm_area_struct *area);
	/*
	 * Called by mprotect() to make driver-specific permission
	 * checks before mprotect() is finalised.   The VMA must not
	 * be modified.  Returns 0 if mprotect() can proceed.
	 */
	int (*mprotect)(struct vm_area_struct *vma, unsigned long start,
			unsigned long end, unsigned long newflags);
	vm_fault_t (*fault)(struct vm_fault *vmf);
	vm_fault_t (*huge_fault)(struct vm_fault *vmf, unsigned int order);
	vm_fault_t (*map_pages)(struct vm_fault *vmf,
			pgoff_t start_pgoff, pgoff_t end_pgoff);
	unsigned long (*pagesize)(struct vm_area_struct * area);

	/* notification that a previously read-only page is about to become
	 * writable, if an error is returned it will cause a SIGBUS */
	vm_fault_t (*page_mkwrite)(struct vm_fault *vmf);

	/* same as page_mkwrite when using VM_PFNMAP|VM_MIXEDMAP */
	vm_fault_t (*pfn_mkwrite)(struct vm_fault *vmf);

	/* called by access_process_vm when get_user_pages() fails, typically
	 * for use by special VMAs. See also generic_access_phys() for a generic
	 * implementation useful for any iomem mapping.
	 */
	int (*access)(struct vm_area_struct *vma, unsigned long addr,
		      void *buf, int len, int write);

	/* Called by the /proc/PID/maps code to ask the vma whether it
	 * has a special name.  Returning non-NULL will also cause this
	 * vma to be dumped unconditionally. */
	const char *(*name)(struct vm_area_struct *vma);

#ifdef CONFIG_NUMA
	/*
	 * set_policy() op must add a reference to any non-NULL @new mempolicy
	 * to hold the policy upon return.  Caller should pass NULL @new to
	 * remove a policy and fall back to surrounding context--i.e. do not
	 * install a MPOL_DEFAULT policy, nor the task or system default
	 * mempolicy.
	 */
	int (*set_policy)(struct vm_area_struct *vma, struct mempolicy *new);

	/*
	 * get_policy() op must add reference [mpol_get()] to any policy at
	 * (vma,addr) marked as MPOL_SHARED.  The shared policy infrastructure
	 * in mm/mempolicy.c will do this automatically.
	 * get_policy() must NOT add a ref if the policy at (vma,addr) is not
	 * marked as MPOL_SHARED. vma policies are protected by the mmap_lock.
	 * If no [shared/vma] mempolicy exists at the addr, get_policy() op
	 * must return NULL--i.e., do not "fallback" to task or system default
	 * policy.
	 */
	struct mempolicy *(*get_policy)(struct vm_area_struct *vma,
					unsigned long addr, pgoff_t *ilx);
#endif
	/*
	 * Called by vm_normal_page() for special PTEs to find the
	 * page for @addr.  This is useful if the default behavior
	 * (using pte_page()) would not find the correct page.
	 */
	struct page *(*find_special_page)(struct vm_area_struct *vma,
					  unsigned long addr);
};

static inline void vma_iter_invalidate(struct vma_iterator *vmi)
{
	mas_pause(&vmi->mas);
}

static inline pgprot_t pgprot_modify(pgprot_t oldprot, pgprot_t newprot)
{
	return __pgprot(pgprot_val(oldprot) | pgprot_val(newprot));
}

static inline pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	return __pgprot(vm_flags);
}

static inline bool is_shared_maywrite(vm_flags_t vm_flags)
{
	return (vm_flags & (VM_SHARED | VM_MAYWRITE)) ==
		(VM_SHARED | VM_MAYWRITE);
}

static inline bool vma_is_shared_maywrite(struct vm_area_struct *vma)
{
	return is_shared_maywrite(vma->vm_flags);
}

static inline struct vm_area_struct *vma_next(struct vma_iterator *vmi)
{
	/*
	 * Uses mas_find() to get the first VMA when the iterator starts.
	 * Calling mas_next() could skip the first entry.
	 */
	return mas_find(&vmi->mas, ULONG_MAX);
}

static inline bool vma_lock_alloc(struct vm_area_struct *vma)
{
	vma->vm_lock = calloc(1, sizeof(struct vma_lock));

	if (!vma->vm_lock)
		return false;

	init_rwsem(&vma->vm_lock->lock);
	vma->vm_lock_seq = -1;

	return true;
}

static inline void vma_assert_write_locked(struct vm_area_struct *);
static inline void vma_mark_detached(struct vm_area_struct *vma, bool detached)
{
	/* When detaching vma should be write-locked */
	if (detached)
		vma_assert_write_locked(vma);
	vma->detached = detached;
}

extern const struct vm_operations_struct vma_dummy_vm_ops;

static inline void vma_init(struct vm_area_struct *vma, struct mm_struct *mm)
{
	memset(vma, 0, sizeof(*vma));
	vma->vm_mm = mm;
	vma->vm_ops = &vma_dummy_vm_ops;
	INIT_LIST_HEAD(&vma->anon_vma_chain);
	vma_mark_detached(vma, false);
}

static inline struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma = calloc(1, sizeof(struct vm_area_struct));

	if (!vma)
		return NULL;

	vma_init(vma, mm);
	if (!vma_lock_alloc(vma)) {
		free(vma);
		return NULL;
	}

	return vma;
}

static inline struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new = calloc(1, sizeof(struct vm_area_struct));

	if (!new)
		return NULL;

	memcpy(new, orig, sizeof(*new));
	if (!vma_lock_alloc(new)) {
		free(new);
		return NULL;
	}
	INIT_LIST_HEAD(&new->anon_vma_chain);

	return new;
}

/*
 * These are defined in vma.h, but sadly vm_stat_account() is referenced by
 * kernel/fork.c, so we have to these broadly available there, and temporarily
 * define them here to resolve the dependency cycle.
 */

#define is_exec_mapping(flags) \
	((flags & (VM_EXEC | VM_WRITE | VM_STACK)) == VM_EXEC)

#define is_stack_mapping(flags) \
	(((flags & VM_STACK) == VM_STACK) || (flags & VM_SHADOW_STACK))

#define is_data_mapping(flags) \
	((flags & (VM_WRITE | VM_SHARED | VM_STACK)) == VM_WRITE)

static inline void vm_stat_account(struct mm_struct *mm, vm_flags_t flags,
				   long npages)
{
	WRITE_ONCE(mm->total_vm, READ_ONCE(mm->total_vm)+npages);

	if (is_exec_mapping(flags))
		mm->exec_vm += npages;
	else if (is_stack_mapping(flags))
		mm->stack_vm += npages;
	else if (is_data_mapping(flags))
		mm->data_vm += npages;
}

#undef is_exec_mapping
#undef is_stack_mapping
#undef is_data_mapping

/* Currently stubbed but we may later wish to un-stub. */
static inline void vm_acct_memory(long pages);
static inline void vm_unacct_memory(long pages)
{
	vm_acct_memory(-pages);
}

static inline void mapping_allow_writable(struct address_space *mapping)
{
	atomic_inc(&mapping->i_mmap_writable);
}

static inline void vma_set_range(struct vm_area_struct *vma,
				 unsigned long start, unsigned long end,
				 pgoff_t pgoff)
{
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
}

static inline
struct vm_area_struct *vma_find(struct vma_iterator *vmi, unsigned long max)
{
	return mas_find(&vmi->mas, max - 1);
}

static inline int vma_iter_clear_gfp(struct vma_iterator *vmi,
			unsigned long start, unsigned long end, gfp_t gfp)
{
	__mas_set_range(&vmi->mas, start, end - 1);
	mas_store_gfp(&vmi->mas, NULL, gfp);
	if (unlikely(mas_is_err(&vmi->mas)))
		return -ENOMEM;

	return 0;
}

static inline void mmap_assert_locked(struct mm_struct *);
static inline struct vm_area_struct *find_vma_intersection(struct mm_struct *mm,
						unsigned long start_addr,
						unsigned long end_addr)
{
	unsigned long index = start_addr;

	mmap_assert_locked(mm);
	return mt_find(&mm->mm_mt, &index, end_addr - 1);
}

static inline
struct vm_area_struct *vma_lookup(struct mm_struct *mm, unsigned long addr)
{
	return mtree_load(&mm->mm_mt, addr);
}

static inline struct vm_area_struct *vma_prev(struct vma_iterator *vmi)
{
	return mas_prev(&vmi->mas, 0);
}

static inline void vma_iter_set(struct vma_iterator *vmi, unsigned long addr)
{
	mas_set(&vmi->mas, addr);
}

static inline bool vma_is_anonymous(struct vm_area_struct *vma)
{
	return !vma->vm_ops;
}

/* Defined in vma.h, so temporarily define here to avoid circular dependency. */
#define vma_iter_load(vmi) \
	mas_walk(&(vmi)->mas)

static inline struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, addr);

	vma = vma_iter_load(&vmi);
	*pprev = vma_prev(&vmi);
	if (!vma)
		vma = vma_next(&vmi);
	return vma;
}

#undef vma_iter_load

static inline void vma_iter_init(struct vma_iterator *vmi,
		struct mm_struct *mm, unsigned long addr)
{
	mas_init(&vmi->mas, &mm->mm_mt, addr);
}

/* Stubbed functions. */

static inline struct anon_vma_name *anon_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}

static inline bool is_mergeable_vm_userfaultfd_ctx(struct vm_area_struct *vma,
					struct vm_userfaultfd_ctx vm_ctx)
{
	return true;
}

static inline bool anon_vma_name_eq(struct anon_vma_name *anon_name1,
				    struct anon_vma_name *anon_name2)
{
	return true;
}

static inline void might_sleep(void)
{
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

static inline void fput(struct file *)
{
}

static inline void mpol_put(struct mempolicy *)
{
}

static inline void vma_lock_free(struct vm_area_struct *vma)
{
	free(vma->vm_lock);
}

static inline void __vm_area_free(struct vm_area_struct *vma)
{
	vma_lock_free(vma);
	free(vma);
}

static inline void vm_area_free(struct vm_area_struct *vma)
{
	__vm_area_free(vma);
}

static inline void lru_add_drain(void)
{
}

static inline void tlb_gather_mmu(struct mmu_gather *, struct mm_struct *)
{
}

static inline void update_hiwater_rss(struct mm_struct *)
{
}

static inline void update_hiwater_vm(struct mm_struct *)
{
}

static inline void unmap_vmas(struct mmu_gather *tlb, struct ma_state *mas,
		      struct vm_area_struct *vma, unsigned long start_addr,
		      unsigned long end_addr, unsigned long tree_end,
		      bool mm_wr_locked)
{
	(void)tlb;
	(void)mas;
	(void)vma;
	(void)start_addr;
	(void)end_addr;
	(void)tree_end;
	(void)mm_wr_locked;
}

static inline void free_pgtables(struct mmu_gather *tlb, struct ma_state *mas,
		   struct vm_area_struct *vma, unsigned long floor,
		   unsigned long ceiling, bool mm_wr_locked)
{
	(void)tlb;
	(void)mas;
	(void)vma;
	(void)floor;
	(void)ceiling;
	(void)mm_wr_locked;
}

static inline void mapping_unmap_writable(struct address_space *)
{
}

static inline void flush_dcache_mmap_lock(struct address_space *)
{
}

static inline void tlb_finish_mmu(struct mmu_gather *)
{
}

static inline void get_file(struct file *)
{
}

static inline int vma_dup_policy(struct vm_area_struct *, struct vm_area_struct *)
{
	return 0;
}

static inline int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src)
{
	/* For testing purposes. We indicate that an anon_vma has been cloned. */
	if (src->anon_vma != NULL) {
		dst->anon_vma = src->anon_vma;
		dst->anon_vma->was_cloned = true;
	}

	return 0;
}

static inline void vma_start_write(struct vm_area_struct *vma)
{
	/* Used to indicate to tests that a write operation has begun. */
	vma->vm_lock_seq++;
}

static inline void vma_adjust_trans_huge(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 long adjust_next)
{
	(void)vma;
	(void)start;
	(void)end;
	(void)adjust_next;
}

static inline void vma_iter_free(struct vma_iterator *vmi)
{
	mas_destroy(&vmi->mas);
}

static inline
struct vm_area_struct *vma_iter_next_range(struct vma_iterator *vmi)
{
	return mas_next_range(&vmi->mas, ULONG_MAX);
}

static inline void vm_acct_memory(long pages)
{
}

static inline void vma_interval_tree_insert(struct vm_area_struct *,
					    struct rb_root_cached *)
{
}

static inline void vma_interval_tree_remove(struct vm_area_struct *,
					    struct rb_root_cached *)
{
}

static inline void flush_dcache_mmap_unlock(struct address_space *)
{
}

static inline void anon_vma_interval_tree_insert(struct anon_vma_chain*,
						 struct rb_root_cached *)
{
}

static inline void anon_vma_interval_tree_remove(struct anon_vma_chain*,
						 struct rb_root_cached *)
{
}

static inline void uprobe_mmap(struct vm_area_struct *)
{
}

static inline void uprobe_munmap(struct vm_area_struct *vma,
				 unsigned long start, unsigned long end)
{
	(void)vma;
	(void)start;
	(void)end;
}

static inline void i_mmap_lock_write(struct address_space *)
{
}

static inline void anon_vma_lock_write(struct anon_vma *)
{
}

static inline void vma_assert_write_locked(struct vm_area_struct *)
{
}

static inline void unlink_anon_vmas(struct vm_area_struct *vma)
{
	/* For testing purposes, indicate that the anon_vma was unlinked. */
	vma->anon_vma->was_unlinked = true;
}

static inline void anon_vma_unlock_write(struct anon_vma *)
{
}

static inline void i_mmap_unlock_write(struct address_space *)
{
}

static inline void anon_vma_merge(struct vm_area_struct *,
				  struct vm_area_struct *)
{
}

static inline int userfaultfd_unmap_prep(struct vm_area_struct *vma,
					 unsigned long start,
					 unsigned long end,
					 struct list_head *unmaps)
{
	(void)vma;
	(void)start;
	(void)end;
	(void)unmaps;

	return 0;
}

static inline void mmap_write_downgrade(struct mm_struct *)
{
}

static inline void mmap_read_unlock(struct mm_struct *)
{
}

static inline void mmap_write_unlock(struct mm_struct *)
{
}

static inline bool can_modify_mm(struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end)
{
	(void)mm;
	(void)start;
	(void)end;

	return true;
}

static inline void arch_unmap(struct mm_struct *mm,
				 unsigned long start,
				 unsigned long end)
{
	(void)mm;
	(void)start;
	(void)end;
}

static inline void mmap_assert_locked(struct mm_struct *)
{
}

static inline bool mpol_equal(struct mempolicy *, struct mempolicy *)
{
	return true;
}

static inline void khugepaged_enter_vma(struct vm_area_struct *vma,
			  unsigned long vm_flags)
{
	(void)vma;
	(void)vm_flags;
}

static inline bool mapping_can_writeback(struct address_space *)
{
	return true;
}

static inline bool is_vm_hugetlb_page(struct vm_area_struct *)
{
	return false;
}

static inline bool vma_soft_dirty_enabled(struct vm_area_struct *)
{
	return false;
}

static inline bool userfaultfd_wp(struct vm_area_struct *)
{
	return false;
}

static inline void mmap_assert_write_locked(struct mm_struct *)
{
}

static inline void mutex_lock(struct mutex *)
{
}

static inline void mutex_unlock(struct mutex *)
{
}

static inline bool mutex_is_locked(struct mutex *)
{
	return true;
}

static inline bool signal_pending(void *)
{
	return false;
}

#endif	/* __MM_VMA_INTERNAL_H */
