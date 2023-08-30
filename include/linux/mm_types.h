/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/mm_types_task.h>

#include <linux/auxvec.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/maple_tree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/rcupdate.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>
#include <linux/seqlock.h>
#include <linux/percpu_counter.h>

#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

#define INIT_PASID	0

struct address_space;
struct mem_cgroup;

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 *
 * If you allocate the page using alloc_pages(), you can use some of the
 * space in struct page for your own purposes.  The five words in the main
 * union are available, except for bit 0 of the first word which must be
 * kept clear.  Many users use this word to store a pointer to an object
 * which is guaranteed to be aligned.  If you use the same storage as
 * page->mapping, you must restore it to NULL before freeing the page.
 *
 * If your page will not be mapped to userspace, you can also use the four
 * bytes in the mapcount union, but you must call page_mapcount_reset()
 * before freeing it.
 *
 * If you want to use the refcount field, it must be used in such a way
 * that other CPUs temporarily incrementing and then decrementing the
 * refcount does not cause problems.  On receiving the page from
 * alloc_pages(), the refcount will be positive.
 *
 * If you allocate pages of order > 0, you can use some of the fields
 * in each subpage, but you may need to restore some of their values
 * afterwards.
 *
 * SLUB uses cmpxchg_double() to atomically update its freelist and counters.
 * That requires that freelist & counters in struct slab be adjacent and
 * double-word aligned. Because struct slab currently just reinterprets the
 * bits of struct page, we align all struct pages to double-word boundaries,
 * and ensure that 'freelist' is aligned within struct slab.
 */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
#define _struct_page_alignment	__aligned(2 * sizeof(unsigned long))
#else
#define _struct_page_alignment	__aligned(sizeof(unsigned long))
#endif

struct page {
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	/*
	 * Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That
	 * means the other users of this union MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
		struct {	/* Page cache and anonymous pages */
			/**
			 * @lru: Pageout list, eg. active_list protected by
			 * lruvec->lru_lock.  Sometimes used as a generic list
			 * by the page owner.
			 */
			union {
				struct list_head lru;

				/* Or, for the Unevictable "LRU list" slot */
				struct {
					/* Always even, to negate PageTail */
					void *__filler;
					/* Count page's or folio's mlocks */
					unsigned int mlock_count;
				};

				/* Or, free page */
				struct list_head buddy_list;
				struct list_head pcp_list;
			};
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
			struct address_space *mapping;
			union {
				pgoff_t index;		/* Our offset within mapping. */
				unsigned long share;	/* share count for fsdax */
			};
			/**
			 * @private: Mapping-private opaque data.
			 * Usually used for buffer_heads if PagePrivate.
			 * Used for swp_entry_t if PageSwapCache.
			 * Indicates order in the buddy system if PageBuddy.
			 */
			unsigned long private;
		};
		struct {	/* page_pool used by netstack */
			/**
			 * @pp_magic: magic value to avoid recycling non
			 * page_pool allocated pages.
			 */
			unsigned long pp_magic;
			struct page_pool *pp;
			unsigned long _pp_mapping_pad;
			unsigned long dma_addr;
			union {
				/**
				 * dma_addr_upper: might require a 64-bit
				 * value on 32-bit architectures.
				 */
				unsigned long dma_addr_upper;
				/**
				 * For frag page support, not supported in
				 * 32-bit architectures with 64-bit DMA.
				 */
				atomic_long_t pp_frag_count;
			};
		};
		struct {	/* Tail pages of compound page */
			unsigned long compound_head;	/* Bit zero is set */
		};
		struct {	/* Page table pages */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};
		struct {	/* ZONE_DEVICE pages */
			/** @pgmap: Points to the hosting device page map. */
			struct dev_pagemap *pgmap;
			void *zone_device_data;
			/*
			 * ZONE_DEVICE private pages are counted as being
			 * mapped so the next 3 words hold the mapping, index,
			 * and private fields from the source anonymous or
			 * page cache page while the page is migrated to device
			 * private memory.
			 * ZONE_DEVICE MEMORY_DEVICE_FS_DAX pages also
			 * use the mapping, index, and private fields when
			 * pmem backed DAX files are mapped.
			 */
		};

		/** @rcu_head: You can use this to free a page by RCU. */
		struct rcu_head rcu_head;
	};

	union {		/* This union is 4 bytes in size. */
		/*
		 * If the page can be mapped to userspace, encodes the number
		 * of times this page is referenced by a page table.
		 */
		atomic_t _mapcount;

		/*
		 * If the page is neither PageSlab nor mappable to userspace,
		 * the value stored here may help determine what this page
		 * is used for.  See page-flags.h for a list of page types
		 * which are currently stored here.
		 */
		unsigned int page_type;
	};

	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
	atomic_t _refcount;

#ifdef CONFIG_MEMCG
	unsigned long memcg_data;
#endif

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef CONFIG_KMSAN
	/*
	 * KMSAN metadata for this page:
	 *  - shadow page: every bit indicates whether the corresponding
	 *    bit of the original page is initialized (0) or not (1);
	 *  - origin page: every 4 bytes contain an id of the stack trace
	 *    where the uninitialized value was created.
	 */
	struct page *kmsan_shadow;
	struct page *kmsan_origin;
#endif

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
} _struct_page_alignment;

/*
 * struct encoded_page - a nonexistent type marking this pointer
 *
 * An 'encoded_page' pointer is a pointer to a regular 'struct page', but
 * with the low bits of the pointer indicating extra context-dependent
 * information. Not super-common, but happens in mmu_gather and mlock
 * handling, and this acts as a type system check on that use.
 *
 * We only really have two guaranteed bits in general, although you could
 * play with 'struct page' alignment (see CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
 * for more.
 *
 * Use the supplied helper functions to endcode/decode the pointer and bits.
 */
struct encoded_page;
#define ENCODE_PAGE_BITS 3ul
static __always_inline struct encoded_page *encode_page(struct page *page, unsigned long flags)
{
	BUILD_BUG_ON(flags > ENCODE_PAGE_BITS);
	return (struct encoded_page *)(flags | (unsigned long)page);
}

static inline unsigned long encoded_page_flags(struct encoded_page *page)
{
	return ENCODE_PAGE_BITS & (unsigned long)page;
}

static inline struct page *encoded_page_ptr(struct encoded_page *page)
{
	return (struct page *)(~ENCODE_PAGE_BITS & (unsigned long)page);
}

/**
 * struct folio - Represents a contiguous set of bytes.
 * @flags: Identical to the page flags.
 * @lru: Least Recently Used list; tracks how recently this folio was used.
 * @mlock_count: Number of times this folio has been pinned by mlock().
 * @mapping: The file this page belongs to, or refers to the anon_vma for
 *    anonymous memory.
 * @index: Offset within the file, in units of pages.  For anonymous memory,
 *    this is the index from the beginning of the mmap.
 * @private: Filesystem per-folio data (see folio_attach_private()).
 *    Used for swp_entry_t if folio_test_swapcache().
 * @_mapcount: Do not access this member directly.  Use folio_mapcount() to
 *    find out how many times this folio is mapped by userspace.
 * @_refcount: Do not access this member directly.  Use folio_ref_count()
 *    to find how many references there are to this folio.
 * @memcg_data: Memory Control Group data.
 * @_folio_dtor: Which destructor to use for this folio.
 * @_folio_order: Do not use directly, call folio_order().
 * @_entire_mapcount: Do not use directly, call folio_entire_mapcount().
 * @_nr_pages_mapped: Do not use directly, call folio_mapcount().
 * @_pincount: Do not use directly, call folio_maybe_dma_pinned().
 * @_folio_nr_pages: Do not use directly, call folio_nr_pages().
 * @_hugetlb_subpool: Do not use directly, use accessor in hugetlb.h.
 * @_hugetlb_cgroup: Do not use directly, use accessor in hugetlb_cgroup.h.
 * @_hugetlb_cgroup_rsvd: Do not use directly, use accessor in hugetlb_cgroup.h.
 * @_hugetlb_hwpoison: Do not use directly, call raw_hwp_list_head().
 * @_deferred_list: Folios to be split under memory pressure.
 *
 * A folio is a physically, virtually and logically contiguous set
 * of bytes.  It is a power-of-two in size, and it is aligned to that
 * same power-of-two.  It is at least as large as %PAGE_SIZE.  If it is
 * in the page cache, it is at a file offset which is a multiple of that
 * power-of-two.  It may be mapped into userspace at an address which is
 * at an arbitrary page offset, but its kernel virtual address is aligned
 * to its size.
 */
struct folio {
	/* private: don't document the anon union */
	union {
		struct {
	/* public: */
			unsigned long flags;
			union {
				struct list_head lru;
	/* private: avoid cluttering the output */
				struct {
					void *__filler;
	/* public: */
					unsigned int mlock_count;
	/* private: */
				};
	/* public: */
			};
			struct address_space *mapping;
			pgoff_t index;
			void *private;
			atomic_t _mapcount;
			atomic_t _refcount;
#ifdef CONFIG_MEMCG
			unsigned long memcg_data;
#endif
	/* private: the union with struct page is transitional */
		};
		struct page page;
	};
	union {
		struct {
			unsigned long _flags_1;
			unsigned long _head_1;
	/* public: */
			unsigned char _folio_dtor;
			unsigned char _folio_order;
			atomic_t _entire_mapcount;
			atomic_t _nr_pages_mapped;
			atomic_t _pincount;
#ifdef CONFIG_64BIT
			unsigned int _folio_nr_pages;
#endif
	/* private: the union with struct page is transitional */
		};
		struct page __page_1;
	};
	union {
		struct {
			unsigned long _flags_2;
			unsigned long _head_2;
	/* public: */
			void *_hugetlb_subpool;
			void *_hugetlb_cgroup;
			void *_hugetlb_cgroup_rsvd;
			void *_hugetlb_hwpoison;
	/* private: the union with struct page is transitional */
		};
		struct {
			unsigned long _flags_2a;
			unsigned long _head_2a;
	/* public: */
			struct list_head _deferred_list;
	/* private: the union with struct page is transitional */
		};
		struct page __page_2;
	};
};

#define FOLIO_MATCH(pg, fl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct folio, fl))
FOLIO_MATCH(flags, flags);
FOLIO_MATCH(lru, lru);
FOLIO_MATCH(mapping, mapping);
FOLIO_MATCH(compound_head, lru);
FOLIO_MATCH(index, index);
FOLIO_MATCH(private, private);
FOLIO_MATCH(_mapcount, _mapcount);
FOLIO_MATCH(_refcount, _refcount);
#ifdef CONFIG_MEMCG
FOLIO_MATCH(memcg_data, memcg_data);
#endif
#undef FOLIO_MATCH
#define FOLIO_MATCH(pg, fl)						\
	static_assert(offsetof(struct folio, fl) ==			\
			offsetof(struct page, pg) + sizeof(struct page))
FOLIO_MATCH(flags, _flags_1);
FOLIO_MATCH(compound_head, _head_1);
#undef FOLIO_MATCH
#define FOLIO_MATCH(pg, fl)						\
	static_assert(offsetof(struct folio, fl) ==			\
			offsetof(struct page, pg) + 2 * sizeof(struct page))
FOLIO_MATCH(flags, _flags_2);
FOLIO_MATCH(compound_head, _head_2);
#undef FOLIO_MATCH

/*
 * Used for sizing the vmemmap region on some architectures
 */
#define STRUCT_PAGE_MAX_SHIFT	(order_base_2(sizeof(struct page)))

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

/*
 * page_private can be used on tail pages.  However, PagePrivate is only
 * checked by the VM on the head page.  So page_private on the tail pages
 * should be used for data that's ancillary to the head page (eg attaching
 * buffer heads to tail pages after attaching buffer heads to the head page)
 */
#define page_private(page)		((page)->private)

static inline void set_page_private(struct page *page, unsigned long private)
{
	page->private = private;
}

static inline void *folio_get_private(struct folio *folio)
{
	return folio->private;
}

struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	/* we maintain a pagecount bias, so that we dont dirty cache line
	 * containing page->_refcount every time we allocate a fragment.
	 */
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

#ifdef CONFIG_USERFAULTFD
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) { NULL, })
struct vm_userfaultfd_ctx {
	struct userfaultfd_ctx *ctx;
};
#else /* CONFIG_USERFAULTFD */
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) {})
struct vm_userfaultfd_ctx {};
#endif /* CONFIG_USERFAULTFD */

struct anon_vma_name {
	struct kref kref;
	/* The name needs to be at the end because it is dynamically sized. */
	char name[];
};

struct vma_lock {
	struct rw_semaphore lock;
};

struct vma_numab_state {
	unsigned long next_scan;
	unsigned long next_pid_reset;
	unsigned long access_pids[2];
};

/*
 * This struct describes a virtual memory area. There is one of these
 * per VM-area/task. A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
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
	int vm_lock_seq;
	struct vma_lock *vm_lock;

	/* Flag to indicate areas detached from the mm->mm_mt tree */
	bool detached;
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

#ifdef CONFIG_SCHED_MM_CID
struct mm_cid {
	u64 time;
	int cid;
};
#endif

struct kioctx_table;
struct mm_struct {
	struct {
		struct maple_tree mm_mt;
#ifdef CONFIG_MMU
		unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
		unsigned long mmap_base;	/* base of mmap area */
		unsigned long mmap_legacy_base;	/* base of mmap area in bottom-up allocations */
#ifdef CONFIG_HAVE_ARCH_COMPAT_MMAP_BASES
		/* Base addresses for compatible mmap() */
		unsigned long mmap_compat_base;
		unsigned long mmap_compat_legacy_base;
#endif
		unsigned long task_size;	/* size of task vm space */
		pgd_t * pgd;

#ifdef CONFIG_MEMBARRIER
		/**
		 * @membarrier_state: Flags controlling membarrier behavior.
		 *
		 * This field is close to @pgd to hopefully fit in the same
		 * cache-line, which needs to be touched by switch_mm().
		 */
		atomic_t membarrier_state;
#endif

		/**
		 * @mm_users: The number of users including userspace.
		 *
		 * Use mmget()/mmget_not_zero()/mmput() to modify. When this
		 * drops to 0 (i.e. when the task exits and there are no other
		 * temporary reference holders), we also release a reference on
		 * @mm_count (which may then free the &struct mm_struct if
		 * @mm_count also drops to 0).
		 */
		atomic_t mm_users;

		/**
		 * @mm_count: The number of references to &struct mm_struct
		 * (@mm_users count as 1).
		 *
		 * Use mmgrab()/mmdrop() to modify. When this drops to 0, the
		 * &struct mm_struct is freed.
		 */
		atomic_t mm_count;
#ifdef CONFIG_SCHED_MM_CID
		/**
		 * @pcpu_cid: Per-cpu current cid.
		 *
		 * Keep track of the currently allocated mm_cid for each cpu.
		 * The per-cpu mm_cid values are serialized by their respective
		 * runqueue locks.
		 */
		struct mm_cid __percpu *pcpu_cid;
		/*
		 * @mm_cid_next_scan: Next mm_cid scan (in jiffies).
		 *
		 * When the next mm_cid scan is due (in jiffies).
		 */
		unsigned long mm_cid_next_scan;
#endif
#ifdef CONFIG_MMU
		atomic_long_t pgtables_bytes;	/* size of all page tables */
#endif
		int map_count;			/* number of VMAs */

		spinlock_t page_table_lock; /* Protects page tables and some
					     * counters
					     */
		/*
		 * With some kernel config, the current mmap_lock's offset
		 * inside 'mm_struct' is at 0x120, which is very optimal, as
		 * its two hot fields 'count' and 'owner' sit in 2 different
		 * cachelines,  and when mmap_lock is highly contended, both
		 * of the 2 fields will be accessed frequently, current layout
		 * will help to reduce cache bouncing.
		 *
		 * So please be careful with adding new fields before
		 * mmap_lock, which can easily push the 2 fields into one
		 * cacheline.
		 */
		struct rw_semaphore mmap_lock;

		struct list_head mmlist; /* List of maybe swapped mm's.	These
					  * are globally strung together off
					  * init_mm.mmlist, and are protected
					  * by mmlist_lock
					  */
#ifdef CONFIG_PER_VMA_LOCK
		int mm_lock_seq;
#endif


		unsigned long hiwater_rss; /* High-watermark of RSS usage */
		unsigned long hiwater_vm;  /* High-water virtual memory usage */

		unsigned long total_vm;	   /* Total pages mapped */
		unsigned long locked_vm;   /* Pages that have PG_mlocked set */
		atomic64_t    pinned_vm;   /* Refcount permanently increased */
		unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
		unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
		unsigned long stack_vm;	   /* VM_STACK */
		unsigned long def_flags;

		/**
		 * @write_protect_seq: Locked when any thread is write
		 * protecting pages mapped by this mm to enforce a later COW,
		 * for instance during page table copying for fork().
		 */
		seqcount_t write_protect_seq;

		spinlock_t arg_lock; /* protect the below fields */

		unsigned long start_code, end_code, start_data, end_data;
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end;

		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

		struct percpu_counter rss_stat[NR_MM_COUNTERS];

		struct linux_binfmt *binfmt;

		/* Architecture-specific MM context */
		mm_context_t context;

		unsigned long flags; /* Must use atomic bitops to access */

#ifdef CONFIG_AIO
		spinlock_t			ioctx_lock;
		struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
		/*
		 * "owner" points to a task that is regarded as the canonical
		 * user/owner of this mm. All of the following must be true in
		 * order for it to be changed:
		 *
		 * current == mm->owner
		 * current->mm != mm
		 * new_owner->mm == mm
		 * new_owner->alloc_lock is held
		 */
		struct task_struct __rcu *owner;
#endif
		struct user_namespace *user_ns;

		/* store ref to file /proc/<pid>/exe symlink points to */
		struct file __rcu *exe_file;
#ifdef CONFIG_MMU_NOTIFIER
		struct mmu_notifier_subscriptions *notifier_subscriptions;
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
		pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif
#ifdef CONFIG_NUMA_BALANCING
		/*
		 * numa_next_scan is the next time that PTEs will be remapped
		 * PROT_NONE to trigger NUMA hinting faults; such faults gather
		 * statistics and migrate pages to new nodes if necessary.
		 */
		unsigned long numa_next_scan;

		/* Restart point for scanning and remapping PTEs. */
		unsigned long numa_scan_offset;

		/* numa_scan_seq prevents two threads remapping PTEs. */
		int numa_scan_seq;
#endif
		/*
		 * An operation with batched TLB flushing is going on. Anything
		 * that can move process memory needs to flush the TLB when
		 * moving a PROT_NONE mapped page.
		 */
		atomic_t tlb_flush_pending;
#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
		/* See flush_tlb_batched_pending() */
		atomic_t tlb_flush_batched;
#endif
		struct uprobes_state uprobes_state;
#ifdef CONFIG_PREEMPT_RT
		struct rcu_head delayed_drop;
#endif
#ifdef CONFIG_HUGETLB_PAGE
		atomic_long_t hugetlb_usage;
#endif
		struct work_struct async_put_work;

#ifdef CONFIG_IOMMU_SVA
		u32 pasid;
#endif
#ifdef CONFIG_KSM
		/*
		 * Represent how many pages of this process are involved in KSM
		 * merging.
		 */
		unsigned long ksm_merging_pages;
		/*
		 * Represent how many pages are checked for ksm merging
		 * including merged and not merged.
		 */
		unsigned long ksm_rmap_items;
#endif
#ifdef CONFIG_LRU_GEN
		struct {
			/* this mm_struct is on lru_gen_mm_list */
			struct list_head list;
			/*
			 * Set when switching to this mm_struct, as a hint of
			 * whether it has been used since the last time per-node
			 * page table walkers cleared the corresponding bits.
			 */
			unsigned long bitmap;
#ifdef CONFIG_MEMCG
			/* points to the memcg of "owner" above */
			struct mem_cgroup *memcg;
#endif
		} lru_gen;
#endif /* CONFIG_LRU_GEN */
	} __randomize_layout;

	/*
	 * The mm_cpumask needs to be at the end of mm_struct, because it
	 * is dynamically sized based on nr_cpu_ids.
	 */
	unsigned long cpu_bitmap[];
};

#define MM_MT_FLAGS	(MT_FLAGS_ALLOC_RANGE | MT_FLAGS_LOCK_EXTERN | \
			 MT_FLAGS_USE_RCU)
extern struct mm_struct init_mm;

/* Pointer magic because the dynamic array size confuses some compilers. */
static inline void mm_init_cpumask(struct mm_struct *mm)
{
	unsigned long cpu_bitmap = (unsigned long)mm;

	cpu_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	cpumask_clear((struct cpumask *)cpu_bitmap);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return (struct cpumask *)&mm->cpu_bitmap;
}

#ifdef CONFIG_LRU_GEN

struct lru_gen_mm_list {
	/* mm_struct list for page table walkers */
	struct list_head fifo;
	/* protects the list above */
	spinlock_t lock;
};

void lru_gen_add_mm(struct mm_struct *mm);
void lru_gen_del_mm(struct mm_struct *mm);
#ifdef CONFIG_MEMCG
void lru_gen_migrate_mm(struct mm_struct *mm);
#endif

static inline void lru_gen_init_mm(struct mm_struct *mm)
{
	INIT_LIST_HEAD(&mm->lru_gen.list);
	mm->lru_gen.bitmap = 0;
#ifdef CONFIG_MEMCG
	mm->lru_gen.memcg = NULL;
#endif
}

static inline void lru_gen_use_mm(struct mm_struct *mm)
{
	/*
	 * When the bitmap is set, page reclaim knows this mm_struct has been
	 * used since the last time it cleared the bitmap. So it might be worth
	 * walking the page tables of this mm_struct to clear the accessed bit.
	 */
	WRITE_ONCE(mm->lru_gen.bitmap, -1);
}

#else /* !CONFIG_LRU_GEN */

static inline void lru_gen_add_mm(struct mm_struct *mm)
{
}

static inline void lru_gen_del_mm(struct mm_struct *mm)
{
}

#ifdef CONFIG_MEMCG
static inline void lru_gen_migrate_mm(struct mm_struct *mm)
{
}
#endif

static inline void lru_gen_init_mm(struct mm_struct *mm)
{
}

static inline void lru_gen_use_mm(struct mm_struct *mm)
{
}

#endif /* CONFIG_LRU_GEN */

struct vma_iterator {
	struct ma_state mas;
};

#define VMA_ITERATOR(name, __mm, __addr)				\
	struct vma_iterator name = {					\
		.mas = {						\
			.tree = &(__mm)->mm_mt,				\
			.index = __addr,				\
			.node = MAS_START,				\
		},							\
	}

static inline void vma_iter_init(struct vma_iterator *vmi,
		struct mm_struct *mm, unsigned long addr)
{
	mas_init(&vmi->mas, &mm->mm_mt, addr);
}

#ifdef CONFIG_SCHED_MM_CID

enum mm_cid_state {
	MM_CID_UNSET = -1U,		/* Unset state has lazy_put flag set. */
	MM_CID_LAZY_PUT = (1U << 31),
};

static inline bool mm_cid_is_unset(int cid)
{
	return cid == MM_CID_UNSET;
}

static inline bool mm_cid_is_lazy_put(int cid)
{
	return !mm_cid_is_unset(cid) && (cid & MM_CID_LAZY_PUT);
}

static inline bool mm_cid_is_valid(int cid)
{
	return !(cid & MM_CID_LAZY_PUT);
}

static inline int mm_cid_set_lazy_put(int cid)
{
	return cid | MM_CID_LAZY_PUT;
}

static inline int mm_cid_clear_lazy_put(int cid)
{
	return cid & ~MM_CID_LAZY_PUT;
}

/* Accessor for struct mm_struct's cidmask. */
static inline cpumask_t *mm_cidmask(struct mm_struct *mm)
{
	unsigned long cid_bitmap = (unsigned long)mm;

	cid_bitmap += offsetof(struct mm_struct, cpu_bitmap);
	/* Skip cpu_bitmap */
	cid_bitmap += cpumask_size();
	return (struct cpumask *)cid_bitmap;
}

static inline void mm_init_cid(struct mm_struct *mm)
{
	int i;

	for_each_possible_cpu(i) {
		struct mm_cid *pcpu_cid = per_cpu_ptr(mm->pcpu_cid, i);

		pcpu_cid->cid = MM_CID_UNSET;
		pcpu_cid->time = 0;
	}
	cpumask_clear(mm_cidmask(mm));
}

static inline int mm_alloc_cid(struct mm_struct *mm)
{
	mm->pcpu_cid = alloc_percpu(struct mm_cid);
	if (!mm->pcpu_cid)
		return -ENOMEM;
	mm_init_cid(mm);
	return 0;
}

static inline void mm_destroy_cid(struct mm_struct *mm)
{
	free_percpu(mm->pcpu_cid);
	mm->pcpu_cid = NULL;
}

static inline unsigned int mm_cid_size(void)
{
	return cpumask_size();
}
#else /* CONFIG_SCHED_MM_CID */
static inline void mm_init_cid(struct mm_struct *mm) { }
static inline int mm_alloc_cid(struct mm_struct *mm) { return 0; }
static inline void mm_destroy_cid(struct mm_struct *mm) { }
static inline unsigned int mm_cid_size(void)
{
	return 0;
}
#endif /* CONFIG_SCHED_MM_CID */

struct mmu_gather;
extern void tlb_gather_mmu(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_gather_mmu_fullmm(struct mmu_gather *tlb, struct mm_struct *mm);
extern void tlb_finish_mmu(struct mmu_gather *tlb);

struct vm_fault;

/**
 * typedef vm_fault_t - Return type for page fault handlers.
 *
 * Page fault handlers return a bitmask of %VM_FAULT values.
 */
typedef __bitwise unsigned int vm_fault_t;

/**
 * enum vm_fault_reason - Page fault handlers return a bitmask of
 * these values to tell the core VM what happened when handling the
 * fault. Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 *
 * @VM_FAULT_OOM:		Out Of Memory
 * @VM_FAULT_SIGBUS:		Bad access
 * @VM_FAULT_MAJOR:		Page read from storage
 * @VM_FAULT_HWPOISON:		Hit poisoned small page
 * @VM_FAULT_HWPOISON_LARGE:	Hit poisoned large page. Index encoded
 *				in upper bits
 * @VM_FAULT_SIGSEGV:		segmentation fault
 * @VM_FAULT_NOPAGE:		->fault installed the pte, not return page
 * @VM_FAULT_LOCKED:		->fault locked the returned page
 * @VM_FAULT_RETRY:		->fault blocked, must retry
 * @VM_FAULT_FALLBACK:		huge page fault failed, fall back to small
 * @VM_FAULT_DONE_COW:		->fault has fully handled COW
 * @VM_FAULT_NEEDDSYNC:		->fault did not modify page tables and needs
 *				fsync() to complete (for synchronous page faults
 *				in DAX)
 * @VM_FAULT_COMPLETED:		->fault completed, meanwhile mmap lock released
 * @VM_FAULT_HINDEX_MASK:	mask HINDEX value
 *
 */
enum vm_fault_reason {
	VM_FAULT_OOM            = (__force vm_fault_t)0x000001,
	VM_FAULT_SIGBUS         = (__force vm_fault_t)0x000002,
	VM_FAULT_MAJOR          = (__force vm_fault_t)0x000004,
	VM_FAULT_HWPOISON       = (__force vm_fault_t)0x000010,
	VM_FAULT_HWPOISON_LARGE = (__force vm_fault_t)0x000020,
	VM_FAULT_SIGSEGV        = (__force vm_fault_t)0x000040,
	VM_FAULT_NOPAGE         = (__force vm_fault_t)0x000100,
	VM_FAULT_LOCKED         = (__force vm_fault_t)0x000200,
	VM_FAULT_RETRY          = (__force vm_fault_t)0x000400,
	VM_FAULT_FALLBACK       = (__force vm_fault_t)0x000800,
	VM_FAULT_DONE_COW       = (__force vm_fault_t)0x001000,
	VM_FAULT_NEEDDSYNC      = (__force vm_fault_t)0x002000,
	VM_FAULT_COMPLETED      = (__force vm_fault_t)0x004000,
	VM_FAULT_HINDEX_MASK    = (__force vm_fault_t)0x0f0000,
};

/* Encode hstate index for a hwpoisoned large page */
#define VM_FAULT_SET_HINDEX(x) ((__force vm_fault_t)((x) << 16))
#define VM_FAULT_GET_HINDEX(x) (((__force unsigned int)(x) >> 16) & 0xf)

#define VM_FAULT_ERROR (VM_FAULT_OOM | VM_FAULT_SIGBUS |	\
			VM_FAULT_SIGSEGV | VM_FAULT_HWPOISON |	\
			VM_FAULT_HWPOISON_LARGE | VM_FAULT_FALLBACK)

#define VM_FAULT_RESULT_TRACE \
	{ VM_FAULT_OOM,                 "OOM" },	\
	{ VM_FAULT_SIGBUS,              "SIGBUS" },	\
	{ VM_FAULT_MAJOR,               "MAJOR" },	\
	{ VM_FAULT_HWPOISON,            "HWPOISON" },	\
	{ VM_FAULT_HWPOISON_LARGE,      "HWPOISON_LARGE" },	\
	{ VM_FAULT_SIGSEGV,             "SIGSEGV" },	\
	{ VM_FAULT_NOPAGE,              "NOPAGE" },	\
	{ VM_FAULT_LOCKED,              "LOCKED" },	\
	{ VM_FAULT_RETRY,               "RETRY" },	\
	{ VM_FAULT_FALLBACK,            "FALLBACK" },	\
	{ VM_FAULT_DONE_COW,            "DONE_COW" },	\
	{ VM_FAULT_NEEDDSYNC,           "NEEDDSYNC" }

struct vm_special_mapping {
	const char *name;	/* The name, e.g. "[vdso]". */

	/*
	 * If .fault is not provided, this points to a
	 * NULL-terminated array of pages that back the special mapping.
	 *
	 * This must not be NULL unless .fault is provided.
	 */
	struct page **pages;

	/*
	 * If non-NULL, then this is called to resolve page faults
	 * on the special mapping.  If used, .pages is not checked.
	 */
	vm_fault_t (*fault)(const struct vm_special_mapping *sm,
				struct vm_area_struct *vma,
				struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

/**
 * enum fault_flag - Fault flag definitions.
 * @FAULT_FLAG_WRITE: Fault was a write fault.
 * @FAULT_FLAG_MKWRITE: Fault was mkwrite of existing PTE.
 * @FAULT_FLAG_ALLOW_RETRY: Allow to retry the fault if blocked.
 * @FAULT_FLAG_RETRY_NOWAIT: Don't drop mmap_lock and wait when retrying.
 * @FAULT_FLAG_KILLABLE: The fault task is in SIGKILL killable region.
 * @FAULT_FLAG_TRIED: The fault has been tried once.
 * @FAULT_FLAG_USER: The fault originated in userspace.
 * @FAULT_FLAG_REMOTE: The fault is not for current task/mm.
 * @FAULT_FLAG_INSTRUCTION: The fault was during an instruction fetch.
 * @FAULT_FLAG_INTERRUPTIBLE: The fault can be interrupted by non-fatal signals.
 * @FAULT_FLAG_UNSHARE: The fault is an unsharing request to break COW in a
 *                      COW mapping, making sure that an exclusive anon page is
 *                      mapped after the fault.
 * @FAULT_FLAG_ORIG_PTE_VALID: whether the fault has vmf->orig_pte cached.
 *                        We should only access orig_pte if this flag set.
 * @FAULT_FLAG_VMA_LOCK: The fault is handled under VMA lock.
 *
 * About @FAULT_FLAG_ALLOW_RETRY and @FAULT_FLAG_TRIED: we can specify
 * whether we would allow page faults to retry by specifying these two
 * fault flags correctly.  Currently there can be three legal combinations:
 *
 * (a) ALLOW_RETRY and !TRIED:  this means the page fault allows retry, and
 *                              this is the first try
 *
 * (b) ALLOW_RETRY and TRIED:   this means the page fault allows retry, and
 *                              we've already tried at least once
 *
 * (c) !ALLOW_RETRY and !TRIED: this means the page fault does not allow retry
 *
 * The unlisted combination (!ALLOW_RETRY && TRIED) is illegal and should never
 * be used.  Note that page faults can be allowed to retry for multiple times,
 * in which case we'll have an initial fault with flags (a) then later on
 * continuous faults with flags (b).  We should always try to detect pending
 * signals before a retry to make sure the continuous page faults can still be
 * interrupted if necessary.
 *
 * The combination FAULT_FLAG_WRITE|FAULT_FLAG_UNSHARE is illegal.
 * FAULT_FLAG_UNSHARE is ignored and treated like an ordinary read fault when
 * applied to mappings that are not COW mappings.
 */
enum fault_flag {
	FAULT_FLAG_WRITE =		1 << 0,
	FAULT_FLAG_MKWRITE =		1 << 1,
	FAULT_FLAG_ALLOW_RETRY =	1 << 2,
	FAULT_FLAG_RETRY_NOWAIT = 	1 << 3,
	FAULT_FLAG_KILLABLE =		1 << 4,
	FAULT_FLAG_TRIED = 		1 << 5,
	FAULT_FLAG_USER =		1 << 6,
	FAULT_FLAG_REMOTE =		1 << 7,
	FAULT_FLAG_INSTRUCTION =	1 << 8,
	FAULT_FLAG_INTERRUPTIBLE =	1 << 9,
	FAULT_FLAG_UNSHARE =		1 << 10,
	FAULT_FLAG_ORIG_PTE_VALID =	1 << 11,
	FAULT_FLAG_VMA_LOCK =		1 << 12,
};

typedef unsigned int __bitwise zap_flags_t;

/*
 * FOLL_PIN and FOLL_LONGTERM may be used in various combinations with each
 * other. Here is what they mean, and how to use them:
 *
 *
 * FIXME: For pages which are part of a filesystem, mappings are subject to the
 * lifetime enforced by the filesystem and we need guarantees that longterm
 * users like RDMA and V4L2 only establish mappings which coordinate usage with
 * the filesystem.  Ideas for this coordination include revoking the longterm
 * pin, delaying writeback, bounce buffer page writeback, etc.  As FS DAX was
 * added after the problem with filesystems was found FS DAX VMAs are
 * specifically failed.  Filesystem pages are still subject to bugs and use of
 * FOLL_LONGTERM should be avoided on those pages.
 *
 * In the CMA case: long term pins in a CMA region would unnecessarily fragment
 * that region.  And so, CMA attempts to migrate the page before pinning, when
 * FOLL_LONGTERM is specified.
 *
 * FOLL_PIN indicates that a special kind of tracking (not just page->_refcount,
 * but an additional pin counting system) will be invoked. This is intended for
 * anything that gets a page reference and then touches page data (for example,
 * Direct IO). This lets the filesystem know that some non-file-system entity is
 * potentially changing the pages' data. In contrast to FOLL_GET (whose pages
 * are released via put_page()), FOLL_PIN pages must be released, ultimately, by
 * a call to unpin_user_page().
 *
 * FOLL_PIN is similar to FOLL_GET: both of these pin pages. They use different
 * and separate refcounting mechanisms, however, and that means that each has
 * its own acquire and release mechanisms:
 *
 *     FOLL_GET: get_user_pages*() to acquire, and put_page() to release.
 *
 *     FOLL_PIN: pin_user_pages*() to acquire, and unpin_user_pages to release.
 *
 * FOLL_PIN and FOLL_GET are mutually exclusive for a given function call.
 * (The underlying pages may experience both FOLL_GET-based and FOLL_PIN-based
 * calls applied to them, and that's perfectly OK. This is a constraint on the
 * callers, not on the pages.)
 *
 * FOLL_PIN should be set internally by the pin_user_pages*() APIs, never
 * directly by the caller. That's in order to help avoid mismatches when
 * releasing pages: get_user_pages*() pages must be released via put_page(),
 * while pin_user_pages*() pages must be released via unpin_user_page().
 *
 * Please see Documentation/core-api/pin_user_pages.rst for more information.
 */

enum {
	/* check pte is writable */
	FOLL_WRITE = 1 << 0,
	/* do get_page on page */
	FOLL_GET = 1 << 1,
	/* give error on hole if it would be zero */
	FOLL_DUMP = 1 << 2,
	/* get_user_pages read/write w/o permission */
	FOLL_FORCE = 1 << 3,
	/*
	 * if a disk transfer is needed, start the IO and return without waiting
	 * upon it
	 */
	FOLL_NOWAIT = 1 << 4,
	/* do not fault in pages */
	FOLL_NOFAULT = 1 << 5,
	/* check page is hwpoisoned */
	FOLL_HWPOISON = 1 << 6,
	/* don't do file mappings */
	FOLL_ANON = 1 << 7,
	/*
	 * FOLL_LONGTERM indicates that the page will be held for an indefinite
	 * time period _often_ under userspace control.  This is in contrast to
	 * iov_iter_get_pages(), whose usages are transient.
	 */
	FOLL_LONGTERM = 1 << 8,
	/* split huge pmd before returning */
	FOLL_SPLIT_PMD = 1 << 9,
	/* allow returning PCI P2PDMA pages */
	FOLL_PCI_P2PDMA = 1 << 10,
	/* allow interrupts from generic signals */
	FOLL_INTERRUPTIBLE = 1 << 11,

	/* See also internal only FOLL flags in mm/internal.h */
};

#endif /* _LINUX_MM_TYPES_H */
