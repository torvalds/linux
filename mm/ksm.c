// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory merging support.
 *
 * This code enables dynamic sharing of identical pages found in different
 * memory areas, even if they are yest shared by fork()
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.
 * Authors:
 *	Izik Eidus
 *	Andrea Arcangeli
 *	Chris Wright
 *	Hugh Dickins
 */

#include <linux/erryes.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/rwsem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/spinlock.h>
#include <linux/xxhash.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/memory.h>
#include <linux/mmu_yestifier.h>
#include <linux/swap.h>
#include <linux/ksm.h>
#include <linux/hashtable.h>
#include <linux/freezer.h>
#include <linux/oom.h>
#include <linux/numa.h>

#include <asm/tlbflush.h>
#include "internal.h"

#ifdef CONFIG_NUMA
#define NUMA(x)		(x)
#define DO_NUMA(x)	do { (x); } while (0)
#else
#define NUMA(x)		(0)
#define DO_NUMA(x)	do { } while (0)
#endif

/**
 * DOC: Overview
 *
 * A few yestes about the KSM scanning process,
 * to make it easier to understand the data structures below:
 *
 * In order to reduce excessive scanning, KSM sorts the memory pages by their
 * contents into a data structure that holds pointers to the pages' locations.
 *
 * Since the contents of the pages may change at any moment, KSM canyest just
 * insert the pages into a yesrmal sorted tree and expect it to find anything.
 * Therefore KSM uses two data structures - the stable and the unstable tree.
 *
 * The stable tree holds pointers to all the merged pages (ksm pages), sorted
 * by their contents.  Because each such page is write-protected, searching on
 * this tree is fully assured to be working (except when pages are unmapped),
 * and therefore this tree is called the stable tree.
 *
 * The stable tree yesde includes information required for reverse
 * mapping from a KSM page to virtual addresses that map this page.
 *
 * In order to avoid large latencies of the rmap walks on KSM pages,
 * KSM maintains two types of yesdes in the stable tree:
 *
 * * the regular yesdes that keep the reverse mapping structures in a
 *   linked list
 * * the "chains" that link yesdes ("dups") that represent the same
 *   write protected memory content, but each "dup" corresponds to a
 *   different KSM page copy of that content
 *
 * Internally, the regular yesdes, "dups" and "chains" are represented
 * using the same :c:type:`struct stable_yesde` structure.
 *
 * In addition to the stable tree, KSM uses a second data structure called the
 * unstable tree: this tree holds pointers to pages which have been found to
 * be "unchanged for a period of time".  The unstable tree sorts these pages
 * by their contents, but since they are yest write-protected, KSM canyest rely
 * upon the unstable tree to work correctly - the unstable tree is liable to
 * be corrupted as its contents are modified, and so it is called unstable.
 *
 * KSM solves this problem by several techniques:
 *
 * 1) The unstable tree is flushed every time KSM completes scanning all
 *    memory areas, and then the tree is rebuilt again from the beginning.
 * 2) KSM will only insert into the unstable tree, pages whose hash value
 *    has yest changed since the previous scan of all memory areas.
 * 3) The unstable tree is a RedBlack Tree - so its balancing is based on the
 *    colors of the yesdes and yest on their contents, assuring that even when
 *    the tree gets "corrupted" it won't get out of balance, so scanning time
 *    remains the same (also, searching and inserting yesdes in an rbtree uses
 *    the same algorithm, so we have yes overhead when we flush and rebuild).
 * 4) KSM never flushes the stable tree, which means that even if it were to
 *    take 10 attempts to find a page in the unstable tree, once it is found,
 *    it is secured in the stable tree.  (When we scan a new page, we first
 *    compare it against the stable tree, and then against the unstable tree.)
 *
 * If the merge_across_yesdes tunable is unset, then KSM maintains multiple
 * stable trees and multiple unstable trees: one of each for each NUMA yesde.
 */

/**
 * struct mm_slot - ksm information per mm that is being scanned
 * @link: link to the mm_slots hash list
 * @mm_list: link into the mm_slots list, rooted in ksm_mm_head
 * @rmap_list: head for this mm_slot's singly-linked list of rmap_items
 * @mm: the mm that this information is valid for
 */
struct mm_slot {
	struct hlist_yesde link;
	struct list_head mm_list;
	struct rmap_item *rmap_list;
	struct mm_struct *mm;
};

/**
 * struct ksm_scan - cursor for scanning
 * @mm_slot: the current mm_slot we are scanning
 * @address: the next address inside that to be scanned
 * @rmap_list: link to the next rmap to be scanned in the rmap_list
 * @seqnr: count of completed full scans (needed when removing unstable yesde)
 *
 * There is only the one ksm_scan instance of this cursor structure.
 */
struct ksm_scan {
	struct mm_slot *mm_slot;
	unsigned long address;
	struct rmap_item **rmap_list;
	unsigned long seqnr;
};

/**
 * struct stable_yesde - yesde of the stable rbtree
 * @yesde: rb yesde of this ksm page in the stable tree
 * @head: (overlaying parent) &migrate_yesdes indicates temporarily on that list
 * @hlist_dup: linked into the stable_yesde->hlist with a stable_yesde chain
 * @list: linked into migrate_yesdes, pending placement in the proper yesde tree
 * @hlist: hlist head of rmap_items using this ksm page
 * @kpfn: page frame number of this ksm page (perhaps temporarily on wrong nid)
 * @chain_prune_time: time of the last full garbage collection
 * @rmap_hlist_len: number of rmap_item entries in hlist or STABLE_NODE_CHAIN
 * @nid: NUMA yesde id of stable tree in which linked (may yest match kpfn)
 */
struct stable_yesde {
	union {
		struct rb_yesde yesde;	/* when yesde of stable tree */
		struct {		/* when listed for migration */
			struct list_head *head;
			struct {
				struct hlist_yesde hlist_dup;
				struct list_head list;
			};
		};
	};
	struct hlist_head hlist;
	union {
		unsigned long kpfn;
		unsigned long chain_prune_time;
	};
	/*
	 * STABLE_NODE_CHAIN can be any negative number in
	 * rmap_hlist_len negative range, but better yest -1 to be able
	 * to reliably detect underflows.
	 */
#define STABLE_NODE_CHAIN -1024
	int rmap_hlist_len;
#ifdef CONFIG_NUMA
	int nid;
#endif
};

/**
 * struct rmap_item - reverse mapping item for virtual addresses
 * @rmap_list: next rmap_item in mm_slot's singly-linked rmap_list
 * @ayesn_vma: pointer to ayesn_vma for this mm,address, when in stable tree
 * @nid: NUMA yesde id of unstable tree in which linked (may yest match page)
 * @mm: the memory structure this rmap_item is pointing into
 * @address: the virtual address this rmap_item tracks (+ flags in low bits)
 * @oldchecksum: previous checksum of the page at that virtual address
 * @yesde: rb yesde of this rmap_item in the unstable tree
 * @head: pointer to stable_yesde heading this list in the stable tree
 * @hlist: link into hlist of rmap_items hanging off that stable_yesde
 */
struct rmap_item {
	struct rmap_item *rmap_list;
	union {
		struct ayesn_vma *ayesn_vma;	/* when stable */
#ifdef CONFIG_NUMA
		int nid;		/* when yesde of unstable tree */
#endif
	};
	struct mm_struct *mm;
	unsigned long address;		/* + low bits used for flags below */
	unsigned int oldchecksum;	/* when unstable */
	union {
		struct rb_yesde yesde;	/* when yesde of unstable tree */
		struct {		/* when listed from stable tree */
			struct stable_yesde *head;
			struct hlist_yesde hlist;
		};
	};
};

#define SEQNR_MASK	0x0ff	/* low bits of unstable tree seqnr */
#define UNSTABLE_FLAG	0x100	/* is a yesde of the unstable tree */
#define STABLE_FLAG	0x200	/* is listed from the stable tree */
#define KSM_FLAG_MASK	(SEQNR_MASK|UNSTABLE_FLAG|STABLE_FLAG)
				/* to mask all the flags */

/* The stable and unstable tree heads */
static struct rb_root one_stable_tree[1] = { RB_ROOT };
static struct rb_root one_unstable_tree[1] = { RB_ROOT };
static struct rb_root *root_stable_tree = one_stable_tree;
static struct rb_root *root_unstable_tree = one_unstable_tree;

/* Recently migrated yesdes of stable tree, pending proper placement */
static LIST_HEAD(migrate_yesdes);
#define STABLE_NODE_DUP_HEAD ((struct list_head *)&migrate_yesdes.prev)

#define MM_SLOTS_HASH_BITS 10
static DEFINE_HASHTABLE(mm_slots_hash, MM_SLOTS_HASH_BITS);

static struct mm_slot ksm_mm_head = {
	.mm_list = LIST_HEAD_INIT(ksm_mm_head.mm_list),
};
static struct ksm_scan ksm_scan = {
	.mm_slot = &ksm_mm_head,
};

static struct kmem_cache *rmap_item_cache;
static struct kmem_cache *stable_yesde_cache;
static struct kmem_cache *mm_slot_cache;

/* The number of yesdes in the stable tree */
static unsigned long ksm_pages_shared;

/* The number of page slots additionally sharing those yesdes */
static unsigned long ksm_pages_sharing;

/* The number of yesdes in the unstable tree */
static unsigned long ksm_pages_unshared;

/* The number of rmap_items in use: to calculate pages_volatile */
static unsigned long ksm_rmap_items;

/* The number of stable_yesde chains */
static unsigned long ksm_stable_yesde_chains;

/* The number of stable_yesde dups linked to the stable_yesde chains */
static unsigned long ksm_stable_yesde_dups;

/* Delay in pruning stale stable_yesde_dups in the stable_yesde_chains */
static int ksm_stable_yesde_chains_prune_millisecs = 2000;

/* Maximum number of page slots sharing a stable yesde */
static int ksm_max_page_sharing = 256;

/* Number of pages ksmd should scan in one batch */
static unsigned int ksm_thread_pages_to_scan = 100;

/* Milliseconds ksmd should sleep between batches */
static unsigned int ksm_thread_sleep_millisecs = 20;

/* Checksum of an empty (zeroed) page */
static unsigned int zero_checksum __read_mostly;

/* Whether to merge empty (zeroed) pages with actual zero pages */
static bool ksm_use_zero_pages __read_mostly;

#ifdef CONFIG_NUMA
/* Zeroed when merging across yesdes is yest allowed */
static unsigned int ksm_merge_across_yesdes = 1;
static int ksm_nr_yesde_ids = 1;
#else
#define ksm_merge_across_yesdes	1U
#define ksm_nr_yesde_ids		1
#endif

#define KSM_RUN_STOP	0
#define KSM_RUN_MERGE	1
#define KSM_RUN_UNMERGE	2
#define KSM_RUN_OFFLINE	4
static unsigned long ksm_run = KSM_RUN_STOP;
static void wait_while_offlining(void);

static DECLARE_WAIT_QUEUE_HEAD(ksm_thread_wait);
static DECLARE_WAIT_QUEUE_HEAD(ksm_iter_wait);
static DEFINE_MUTEX(ksm_thread_mutex);
static DEFINE_SPINLOCK(ksm_mmlist_lock);

#define KSM_KMEM_CACHE(__struct, __flags) kmem_cache_create("ksm_"#__struct,\
		sizeof(struct __struct), __aligyesf__(struct __struct),\
		(__flags), NULL)

static int __init ksm_slab_init(void)
{
	rmap_item_cache = KSM_KMEM_CACHE(rmap_item, 0);
	if (!rmap_item_cache)
		goto out;

	stable_yesde_cache = KSM_KMEM_CACHE(stable_yesde, 0);
	if (!stable_yesde_cache)
		goto out_free1;

	mm_slot_cache = KSM_KMEM_CACHE(mm_slot, 0);
	if (!mm_slot_cache)
		goto out_free2;

	return 0;

out_free2:
	kmem_cache_destroy(stable_yesde_cache);
out_free1:
	kmem_cache_destroy(rmap_item_cache);
out:
	return -ENOMEM;
}

static void __init ksm_slab_free(void)
{
	kmem_cache_destroy(mm_slot_cache);
	kmem_cache_destroy(stable_yesde_cache);
	kmem_cache_destroy(rmap_item_cache);
	mm_slot_cache = NULL;
}

static __always_inline bool is_stable_yesde_chain(struct stable_yesde *chain)
{
	return chain->rmap_hlist_len == STABLE_NODE_CHAIN;
}

static __always_inline bool is_stable_yesde_dup(struct stable_yesde *dup)
{
	return dup->head == STABLE_NODE_DUP_HEAD;
}

static inline void stable_yesde_chain_add_dup(struct stable_yesde *dup,
					     struct stable_yesde *chain)
{
	VM_BUG_ON(is_stable_yesde_dup(dup));
	dup->head = STABLE_NODE_DUP_HEAD;
	VM_BUG_ON(!is_stable_yesde_chain(chain));
	hlist_add_head(&dup->hlist_dup, &chain->hlist);
	ksm_stable_yesde_dups++;
}

static inline void __stable_yesde_dup_del(struct stable_yesde *dup)
{
	VM_BUG_ON(!is_stable_yesde_dup(dup));
	hlist_del(&dup->hlist_dup);
	ksm_stable_yesde_dups--;
}

static inline void stable_yesde_dup_del(struct stable_yesde *dup)
{
	VM_BUG_ON(is_stable_yesde_chain(dup));
	if (is_stable_yesde_dup(dup))
		__stable_yesde_dup_del(dup);
	else
		rb_erase(&dup->yesde, root_stable_tree + NUMA(dup->nid));
#ifdef CONFIG_DEBUG_VM
	dup->head = NULL;
#endif
}

static inline struct rmap_item *alloc_rmap_item(void)
{
	struct rmap_item *rmap_item;

	rmap_item = kmem_cache_zalloc(rmap_item_cache, GFP_KERNEL |
						__GFP_NORETRY | __GFP_NOWARN);
	if (rmap_item)
		ksm_rmap_items++;
	return rmap_item;
}

static inline void free_rmap_item(struct rmap_item *rmap_item)
{
	ksm_rmap_items--;
	rmap_item->mm = NULL;	/* debug safety */
	kmem_cache_free(rmap_item_cache, rmap_item);
}

static inline struct stable_yesde *alloc_stable_yesde(void)
{
	/*
	 * The allocation can take too long with GFP_KERNEL when memory is under
	 * pressure, which may lead to hung task warnings.  Adding __GFP_HIGH
	 * grants access to memory reserves, helping to avoid this problem.
	 */
	return kmem_cache_alloc(stable_yesde_cache, GFP_KERNEL | __GFP_HIGH);
}

static inline void free_stable_yesde(struct stable_yesde *stable_yesde)
{
	VM_BUG_ON(stable_yesde->rmap_hlist_len &&
		  !is_stable_yesde_chain(stable_yesde));
	kmem_cache_free(stable_yesde_cache, stable_yesde);
}

static inline struct mm_slot *alloc_mm_slot(void)
{
	if (!mm_slot_cache)	/* initialization failed */
		return NULL;
	return kmem_cache_zalloc(mm_slot_cache, GFP_KERNEL);
}

static inline void free_mm_slot(struct mm_slot *mm_slot)
{
	kmem_cache_free(mm_slot_cache, mm_slot);
}

static struct mm_slot *get_mm_slot(struct mm_struct *mm)
{
	struct mm_slot *slot;

	hash_for_each_possible(mm_slots_hash, slot, link, (unsigned long)mm)
		if (slot->mm == mm)
			return slot;

	return NULL;
}

static void insert_to_mm_slots_hash(struct mm_struct *mm,
				    struct mm_slot *mm_slot)
{
	mm_slot->mm = mm;
	hash_add(mm_slots_hash, &mm_slot->link, (unsigned long)mm);
}

/*
 * ksmd, and unmerge_and_remove_all_rmap_items(), must yest touch an mm's
 * page tables after it has passed through ksm_exit() - which, if necessary,
 * takes mmap_sem briefly to serialize against them.  ksm_exit() does yest set
 * a special flag: they can just back out as soon as mm_users goes to zero.
 * ksm_test_exit() is used throughout to make this test for exit: in some
 * places for correctness, in some places just to avoid unnecessary work.
 */
static inline bool ksm_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0;
}

/*
 * We use break_ksm to break COW on a ksm page: it's a stripped down
 *
 *	if (get_user_pages(addr, 1, 1, 1, &page, NULL) == 1)
 *		put_page(page);
 *
 * but taking great care only to touch a ksm page, in a VM_MERGEABLE vma,
 * in case the application has unmapped and remapped mm,addr meanwhile.
 * Could a ksm page appear anywhere else?  Actually no, in a VM_PFNMAP
 * mmap of /dev/mem or /dev/kmem, where we would yest want to touch it.
 *
 * FAULT_FLAG/FOLL_REMOTE are because we do this outside the context
 * of the process that owns 'vma'.  We also do yest want to enforce
 * protection keys here anyway.
 */
static int break_ksm(struct vm_area_struct *vma, unsigned long addr)
{
	struct page *page;
	vm_fault_t ret = 0;

	do {
		cond_resched();
		page = follow_page(vma, addr,
				FOLL_GET | FOLL_MIGRATION | FOLL_REMOTE);
		if (IS_ERR_OR_NULL(page))
			break;
		if (PageKsm(page))
			ret = handle_mm_fault(vma, addr,
					FAULT_FLAG_WRITE | FAULT_FLAG_REMOTE);
		else
			ret = VM_FAULT_WRITE;
		put_page(page);
	} while (!(ret & (VM_FAULT_WRITE | VM_FAULT_SIGBUS | VM_FAULT_SIGSEGV | VM_FAULT_OOM)));
	/*
	 * We must loop because handle_mm_fault() may back out if there's
	 * any difficulty e.g. if pte accessed bit gets updated concurrently.
	 *
	 * VM_FAULT_WRITE is what we have been hoping for: it indicates that
	 * COW has been broken, even if the vma does yest permit VM_WRITE;
	 * but yeste that a concurrent fault might break PageKsm for us.
	 *
	 * VM_FAULT_SIGBUS could occur if we race with truncation of the
	 * backing file, which also invalidates ayesnymous pages: that's
	 * okay, that truncation will have unmapped the PageKsm for us.
	 *
	 * VM_FAULT_OOM: at the time of writing (late July 2009), setting
	 * aside mem_cgroup limits, VM_FAULT_OOM would only be set if the
	 * current task has TIF_MEMDIE set, and will be OOM killed on return
	 * to user; and ksmd, having yes mm, would never be chosen for that.
	 *
	 * But if the mm is in a limited mem_cgroup, then the fault may fail
	 * with VM_FAULT_OOM even if the current task is yest TIF_MEMDIE; and
	 * even ksmd can fail in this way - though it's usually breaking ksm
	 * just to undo a merge it made a moment before, so unlikely to oom.
	 *
	 * That's a pity: we might therefore have more kernel pages allocated
	 * than we're counting as yesdes in the stable tree; but ksm_do_scan
	 * will retry to break_cow on each pass, so should recover the page
	 * in due course.  The important thing is to yest let VM_MERGEABLE
	 * be cleared while any such pages might remain in the area.
	 */
	return (ret & VM_FAULT_OOM) ? -ENOMEM : 0;
}

static struct vm_area_struct *find_mergeable_vma(struct mm_struct *mm,
		unsigned long addr)
{
	struct vm_area_struct *vma;
	if (ksm_test_exit(mm))
		return NULL;
	vma = find_vma(mm, addr);
	if (!vma || vma->vm_start > addr)
		return NULL;
	if (!(vma->vm_flags & VM_MERGEABLE) || !vma->ayesn_vma)
		return NULL;
	return vma;
}

static void break_cow(struct rmap_item *rmap_item)
{
	struct mm_struct *mm = rmap_item->mm;
	unsigned long addr = rmap_item->address;
	struct vm_area_struct *vma;

	/*
	 * It is yest an accident that whenever we want to break COW
	 * to undo, we also need to drop a reference to the ayesn_vma.
	 */
	put_ayesn_vma(rmap_item->ayesn_vma);

	down_read(&mm->mmap_sem);
	vma = find_mergeable_vma(mm, addr);
	if (vma)
		break_ksm(vma, addr);
	up_read(&mm->mmap_sem);
}

static struct page *get_mergeable_page(struct rmap_item *rmap_item)
{
	struct mm_struct *mm = rmap_item->mm;
	unsigned long addr = rmap_item->address;
	struct vm_area_struct *vma;
	struct page *page;

	down_read(&mm->mmap_sem);
	vma = find_mergeable_vma(mm, addr);
	if (!vma)
		goto out;

	page = follow_page(vma, addr, FOLL_GET);
	if (IS_ERR_OR_NULL(page))
		goto out;
	if (PageAyesn(page)) {
		flush_ayesn_page(vma, page, addr);
		flush_dcache_page(page);
	} else {
		put_page(page);
out:
		page = NULL;
	}
	up_read(&mm->mmap_sem);
	return page;
}

/*
 * This helper is used for getting right index into array of tree roots.
 * When merge_across_yesdes kyesb is set to 1, there are only two rb-trees for
 * stable and unstable pages from all yesdes with roots in index 0. Otherwise,
 * every yesde has its own stable and unstable tree.
 */
static inline int get_kpfn_nid(unsigned long kpfn)
{
	return ksm_merge_across_yesdes ? 0 : NUMA(pfn_to_nid(kpfn));
}

static struct stable_yesde *alloc_stable_yesde_chain(struct stable_yesde *dup,
						   struct rb_root *root)
{
	struct stable_yesde *chain = alloc_stable_yesde();
	VM_BUG_ON(is_stable_yesde_chain(dup));
	if (likely(chain)) {
		INIT_HLIST_HEAD(&chain->hlist);
		chain->chain_prune_time = jiffies;
		chain->rmap_hlist_len = STABLE_NODE_CHAIN;
#if defined (CONFIG_DEBUG_VM) && defined(CONFIG_NUMA)
		chain->nid = NUMA_NO_NODE; /* debug */
#endif
		ksm_stable_yesde_chains++;

		/*
		 * Put the stable yesde chain in the first dimension of
		 * the stable tree and at the same time remove the old
		 * stable yesde.
		 */
		rb_replace_yesde(&dup->yesde, &chain->yesde, root);

		/*
		 * Move the old stable yesde to the second dimension
		 * queued in the hlist_dup. The invariant is that all
		 * dup stable_yesdes in the chain->hlist point to pages
		 * that are wrprotected and have the exact same
		 * content.
		 */
		stable_yesde_chain_add_dup(dup, chain);
	}
	return chain;
}

static inline void free_stable_yesde_chain(struct stable_yesde *chain,
					  struct rb_root *root)
{
	rb_erase(&chain->yesde, root);
	free_stable_yesde(chain);
	ksm_stable_yesde_chains--;
}

static void remove_yesde_from_stable_tree(struct stable_yesde *stable_yesde)
{
	struct rmap_item *rmap_item;

	/* check it's yest STABLE_NODE_CHAIN or negative */
	BUG_ON(stable_yesde->rmap_hlist_len < 0);

	hlist_for_each_entry(rmap_item, &stable_yesde->hlist, hlist) {
		if (rmap_item->hlist.next)
			ksm_pages_sharing--;
		else
			ksm_pages_shared--;
		VM_BUG_ON(stable_yesde->rmap_hlist_len <= 0);
		stable_yesde->rmap_hlist_len--;
		put_ayesn_vma(rmap_item->ayesn_vma);
		rmap_item->address &= PAGE_MASK;
		cond_resched();
	}

	/*
	 * We need the second aligned pointer of the migrate_yesdes
	 * list_head to stay clear from the rb_parent_color union
	 * (aligned and different than any yesde) and also different
	 * from &migrate_yesdes. This will verify that future list.h changes
	 * don't break STABLE_NODE_DUP_HEAD. Only recent gcc can handle it.
	 */
#if defined(GCC_VERSION) && GCC_VERSION >= 40903
	BUILD_BUG_ON(STABLE_NODE_DUP_HEAD <= &migrate_yesdes);
	BUILD_BUG_ON(STABLE_NODE_DUP_HEAD >= &migrate_yesdes + 1);
#endif

	if (stable_yesde->head == &migrate_yesdes)
		list_del(&stable_yesde->list);
	else
		stable_yesde_dup_del(stable_yesde);
	free_stable_yesde(stable_yesde);
}

enum get_ksm_page_flags {
	GET_KSM_PAGE_NOLOCK,
	GET_KSM_PAGE_LOCK,
	GET_KSM_PAGE_TRYLOCK
};

/*
 * get_ksm_page: checks if the page indicated by the stable yesde
 * is still its ksm page, despite having held yes reference to it.
 * In which case we can trust the content of the page, and it
 * returns the gotten page; but if the page has yesw been zapped,
 * remove the stale yesde from the stable tree and return NULL.
 * But beware, the stable yesde's page might be being migrated.
 *
 * You would expect the stable_yesde to hold a reference to the ksm page.
 * But if it increments the page's count, swapping out has to wait for
 * ksmd to come around again before it can free the page, which may take
 * seconds or even minutes: much too unresponsive.  So instead we use a
 * "keyhole reference": access to the ksm page from the stable yesde peeps
 * out through its keyhole to see if that page still holds the right key,
 * pointing back to this stable yesde.  This relies on freeing a PageAyesn
 * page to reset its page->mapping to NULL, and relies on yes other use of
 * a page to put something that might look like our key in page->mapping.
 * is on its way to being freed; but it is an ayesmaly to bear in mind.
 */
static struct page *get_ksm_page(struct stable_yesde *stable_yesde,
				 enum get_ksm_page_flags flags)
{
	struct page *page;
	void *expected_mapping;
	unsigned long kpfn;

	expected_mapping = (void *)((unsigned long)stable_yesde |
					PAGE_MAPPING_KSM);
again:
	kpfn = READ_ONCE(stable_yesde->kpfn); /* Address dependency. */
	page = pfn_to_page(kpfn);
	if (READ_ONCE(page->mapping) != expected_mapping)
		goto stale;

	/*
	 * We canyest do anything with the page while its refcount is 0.
	 * Usually 0 means free, or tail of a higher-order page: in which
	 * case this yesde is yes longer referenced, and should be freed;
	 * however, it might mean that the page is under page_ref_freeze().
	 * The __remove_mapping() case is easy, again the yesde is yesw stale;
	 * the same is in reuse_ksm_page() case; but if page is swapcache
	 * in migrate_page_move_mapping(), it might still be our page,
	 * in which case it's essential to keep the yesde.
	 */
	while (!get_page_unless_zero(page)) {
		/*
		 * Ayesther check for page->mapping != expected_mapping would
		 * work here too.  We have chosen the !PageSwapCache test to
		 * optimize the common case, when the page is or is about to
		 * be freed: PageSwapCache is cleared (under spin_lock_irq)
		 * in the ref_freeze section of __remove_mapping(); but Ayesn
		 * page->mapping reset to NULL later, in free_pages_prepare().
		 */
		if (!PageSwapCache(page))
			goto stale;
		cpu_relax();
	}

	if (READ_ONCE(page->mapping) != expected_mapping) {
		put_page(page);
		goto stale;
	}

	if (flags == GET_KSM_PAGE_TRYLOCK) {
		if (!trylock_page(page)) {
			put_page(page);
			return ERR_PTR(-EBUSY);
		}
	} else if (flags == GET_KSM_PAGE_LOCK)
		lock_page(page);

	if (flags != GET_KSM_PAGE_NOLOCK) {
		if (READ_ONCE(page->mapping) != expected_mapping) {
			unlock_page(page);
			put_page(page);
			goto stale;
		}
	}
	return page;

stale:
	/*
	 * We come here from above when page->mapping or !PageSwapCache
	 * suggests that the yesde is stale; but it might be under migration.
	 * We need smp_rmb(), matching the smp_wmb() in ksm_migrate_page(),
	 * before checking whether yesde->kpfn has been changed.
	 */
	smp_rmb();
	if (READ_ONCE(stable_yesde->kpfn) != kpfn)
		goto again;
	remove_yesde_from_stable_tree(stable_yesde);
	return NULL;
}

/*
 * Removing rmap_item from stable or unstable tree.
 * This function will clean the information from the stable/unstable tree.
 */
static void remove_rmap_item_from_tree(struct rmap_item *rmap_item)
{
	if (rmap_item->address & STABLE_FLAG) {
		struct stable_yesde *stable_yesde;
		struct page *page;

		stable_yesde = rmap_item->head;
		page = get_ksm_page(stable_yesde, GET_KSM_PAGE_LOCK);
		if (!page)
			goto out;

		hlist_del(&rmap_item->hlist);
		unlock_page(page);
		put_page(page);

		if (!hlist_empty(&stable_yesde->hlist))
			ksm_pages_sharing--;
		else
			ksm_pages_shared--;
		VM_BUG_ON(stable_yesde->rmap_hlist_len <= 0);
		stable_yesde->rmap_hlist_len--;

		put_ayesn_vma(rmap_item->ayesn_vma);
		rmap_item->address &= PAGE_MASK;

	} else if (rmap_item->address & UNSTABLE_FLAG) {
		unsigned char age;
		/*
		 * Usually ksmd can and must skip the rb_erase, because
		 * root_unstable_tree was already reset to RB_ROOT.
		 * But be careful when an mm is exiting: do the rb_erase
		 * if this rmap_item was inserted by this scan, rather
		 * than left over from before.
		 */
		age = (unsigned char)(ksm_scan.seqnr - rmap_item->address);
		BUG_ON(age > 1);
		if (!age)
			rb_erase(&rmap_item->yesde,
				 root_unstable_tree + NUMA(rmap_item->nid));
		ksm_pages_unshared--;
		rmap_item->address &= PAGE_MASK;
	}
out:
	cond_resched();		/* we're called from many long loops */
}

static void remove_trailing_rmap_items(struct mm_slot *mm_slot,
				       struct rmap_item **rmap_list)
{
	while (*rmap_list) {
		struct rmap_item *rmap_item = *rmap_list;
		*rmap_list = rmap_item->rmap_list;
		remove_rmap_item_from_tree(rmap_item);
		free_rmap_item(rmap_item);
	}
}

/*
 * Though it's very tempting to unmerge rmap_items from stable tree rather
 * than check every pte of a given vma, the locking doesn't quite work for
 * that - an rmap_item is assigned to the stable tree after inserting ksm
 * page and upping mmap_sem.  Nor does it fit with the way we skip dup'ing
 * rmap_items from parent to child at fork time (so as yest to waste time
 * if exit comes before the next scan reaches it).
 *
 * Similarly, although we'd like to remove rmap_items (so updating counts
 * and freeing memory) when unmerging an area, it's easier to leave that
 * to the next pass of ksmd - consider, for example, how ksmd might be
 * in cmp_and_merge_page on one of the rmap_items we would be removing.
 */
static int unmerge_ksm_pages(struct vm_area_struct *vma,
			     unsigned long start, unsigned long end)
{
	unsigned long addr;
	int err = 0;

	for (addr = start; addr < end && !err; addr += PAGE_SIZE) {
		if (ksm_test_exit(vma->vm_mm))
			break;
		if (signal_pending(current))
			err = -ERESTARTSYS;
		else
			err = break_ksm(vma, addr);
	}
	return err;
}

static inline struct stable_yesde *page_stable_yesde(struct page *page)
{
	return PageKsm(page) ? page_rmapping(page) : NULL;
}

static inline void set_page_stable_yesde(struct page *page,
					struct stable_yesde *stable_yesde)
{
	page->mapping = (void *)((unsigned long)stable_yesde | PAGE_MAPPING_KSM);
}

#ifdef CONFIG_SYSFS
/*
 * Only called through the sysfs control interface:
 */
static int remove_stable_yesde(struct stable_yesde *stable_yesde)
{
	struct page *page;
	int err;

	page = get_ksm_page(stable_yesde, GET_KSM_PAGE_LOCK);
	if (!page) {
		/*
		 * get_ksm_page did remove_yesde_from_stable_tree itself.
		 */
		return 0;
	}

	/*
	 * Page could be still mapped if this races with __mmput() running in
	 * between ksm_exit() and exit_mmap(). Just refuse to let
	 * merge_across_yesdes/max_page_sharing be switched.
	 */
	err = -EBUSY;
	if (!page_mapped(page)) {
		/*
		 * The stable yesde did yest yet appear stale to get_ksm_page(),
		 * since that allows for an unmapped ksm page to be recognized
		 * right up until it is freed; but the yesde is safe to remove.
		 * This page might be in a pagevec waiting to be freed,
		 * or it might be PageSwapCache (perhaps under writeback),
		 * or it might have been removed from swapcache a moment ago.
		 */
		set_page_stable_yesde(page, NULL);
		remove_yesde_from_stable_tree(stable_yesde);
		err = 0;
	}

	unlock_page(page);
	put_page(page);
	return err;
}

static int remove_stable_yesde_chain(struct stable_yesde *stable_yesde,
				    struct rb_root *root)
{
	struct stable_yesde *dup;
	struct hlist_yesde *hlist_safe;

	if (!is_stable_yesde_chain(stable_yesde)) {
		VM_BUG_ON(is_stable_yesde_dup(stable_yesde));
		if (remove_stable_yesde(stable_yesde))
			return true;
		else
			return false;
	}

	hlist_for_each_entry_safe(dup, hlist_safe,
				  &stable_yesde->hlist, hlist_dup) {
		VM_BUG_ON(!is_stable_yesde_dup(dup));
		if (remove_stable_yesde(dup))
			return true;
	}
	BUG_ON(!hlist_empty(&stable_yesde->hlist));
	free_stable_yesde_chain(stable_yesde, root);
	return false;
}

static int remove_all_stable_yesdes(void)
{
	struct stable_yesde *stable_yesde, *next;
	int nid;
	int err = 0;

	for (nid = 0; nid < ksm_nr_yesde_ids; nid++) {
		while (root_stable_tree[nid].rb_yesde) {
			stable_yesde = rb_entry(root_stable_tree[nid].rb_yesde,
						struct stable_yesde, yesde);
			if (remove_stable_yesde_chain(stable_yesde,
						     root_stable_tree + nid)) {
				err = -EBUSY;
				break;	/* proceed to next nid */
			}
			cond_resched();
		}
	}
	list_for_each_entry_safe(stable_yesde, next, &migrate_yesdes, list) {
		if (remove_stable_yesde(stable_yesde))
			err = -EBUSY;
		cond_resched();
	}
	return err;
}

static int unmerge_and_remove_all_rmap_items(void)
{
	struct mm_slot *mm_slot;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int err = 0;

	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = list_entry(ksm_mm_head.mm_list.next,
						struct mm_slot, mm_list);
	spin_unlock(&ksm_mmlist_lock);

	for (mm_slot = ksm_scan.mm_slot;
			mm_slot != &ksm_mm_head; mm_slot = ksm_scan.mm_slot) {
		mm = mm_slot->mm;
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			if (ksm_test_exit(mm))
				break;
			if (!(vma->vm_flags & VM_MERGEABLE) || !vma->ayesn_vma)
				continue;
			err = unmerge_ksm_pages(vma,
						vma->vm_start, vma->vm_end);
			if (err)
				goto error;
		}

		remove_trailing_rmap_items(mm_slot, &mm_slot->rmap_list);
		up_read(&mm->mmap_sem);

		spin_lock(&ksm_mmlist_lock);
		ksm_scan.mm_slot = list_entry(mm_slot->mm_list.next,
						struct mm_slot, mm_list);
		if (ksm_test_exit(mm)) {
			hash_del(&mm_slot->link);
			list_del(&mm_slot->mm_list);
			spin_unlock(&ksm_mmlist_lock);

			free_mm_slot(mm_slot);
			clear_bit(MMF_VM_MERGEABLE, &mm->flags);
			mmdrop(mm);
		} else
			spin_unlock(&ksm_mmlist_lock);
	}

	/* Clean up stable yesdes, but don't worry if some are still busy */
	remove_all_stable_yesdes();
	ksm_scan.seqnr = 0;
	return 0;

error:
	up_read(&mm->mmap_sem);
	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = &ksm_mm_head;
	spin_unlock(&ksm_mmlist_lock);
	return err;
}
#endif /* CONFIG_SYSFS */

static u32 calc_checksum(struct page *page)
{
	u32 checksum;
	void *addr = kmap_atomic(page);
	checksum = xxhash(addr, PAGE_SIZE, 0);
	kunmap_atomic(addr);
	return checksum;
}

static int write_protect_page(struct vm_area_struct *vma, struct page *page,
			      pte_t *orig_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page_vma_mapped_walk pvmw = {
		.page = page,
		.vma = vma,
	};
	int swapped;
	int err = -EFAULT;
	struct mmu_yestifier_range range;

	pvmw.address = page_address_in_vma(page, vma);
	if (pvmw.address == -EFAULT)
		goto out;

	BUG_ON(PageTransCompound(page));

	mmu_yestifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, mm,
				pvmw.address,
				pvmw.address + PAGE_SIZE);
	mmu_yestifier_invalidate_range_start(&range);

	if (!page_vma_mapped_walk(&pvmw))
		goto out_mn;
	if (WARN_ONCE(!pvmw.pte, "Unexpected PMD mapping?"))
		goto out_unlock;

	if (pte_write(*pvmw.pte) || pte_dirty(*pvmw.pte) ||
	    (pte_protyesne(*pvmw.pte) && pte_savedwrite(*pvmw.pte)) ||
						mm_tlb_flush_pending(mm)) {
		pte_t entry;

		swapped = PageSwapCache(page);
		flush_cache_page(vma, pvmw.address, page_to_pfn(page));
		/*
		 * Ok this is tricky, when get_user_pages_fast() run it doesn't
		 * take any lock, therefore the check that we are going to make
		 * with the pagecount against the mapcount is racey and
		 * O_DIRECT can happen right after the check.
		 * So we clear the pte and flush the tlb before the check
		 * this assure us that yes O_DIRECT can happen after the check
		 * or in the middle of the check.
		 *
		 * No need to yestify as we are downgrading page table to read
		 * only yest changing it to point to a new page.
		 *
		 * See Documentation/vm/mmu_yestifier.rst
		 */
		entry = ptep_clear_flush(vma, pvmw.address, pvmw.pte);
		/*
		 * Check that yes O_DIRECT or similar I/O is in progress on the
		 * page
		 */
		if (page_mapcount(page) + 1 + swapped != page_count(page)) {
			set_pte_at(mm, pvmw.address, pvmw.pte, entry);
			goto out_unlock;
		}
		if (pte_dirty(entry))
			set_page_dirty(page);

		if (pte_protyesne(entry))
			entry = pte_mkclean(pte_clear_savedwrite(entry));
		else
			entry = pte_mkclean(pte_wrprotect(entry));
		set_pte_at_yestify(mm, pvmw.address, pvmw.pte, entry);
	}
	*orig_pte = *pvmw.pte;
	err = 0;

out_unlock:
	page_vma_mapped_walk_done(&pvmw);
out_mn:
	mmu_yestifier_invalidate_range_end(&range);
out:
	return err;
}

/**
 * replace_page - replace page in vma by new ksm page
 * @vma:      vma that holds the pte pointing to page
 * @page:     the page we are replacing by kpage
 * @kpage:    the ksm page we replace page by
 * @orig_pte: the original value of the pte
 *
 * Returns 0 on success, -EFAULT on failure.
 */
static int replace_page(struct vm_area_struct *vma, struct page *page,
			struct page *kpage, pte_t orig_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *pmd;
	pte_t *ptep;
	pte_t newpte;
	spinlock_t *ptl;
	unsigned long addr;
	int err = -EFAULT;
	struct mmu_yestifier_range range;

	addr = page_address_in_vma(page, vma);
	if (addr == -EFAULT)
		goto out;

	pmd = mm_find_pmd(mm, addr);
	if (!pmd)
		goto out;

	mmu_yestifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, mm, addr,
				addr + PAGE_SIZE);
	mmu_yestifier_invalidate_range_start(&range);

	ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte_same(*ptep, orig_pte)) {
		pte_unmap_unlock(ptep, ptl);
		goto out_mn;
	}

	/*
	 * No need to check ksm_use_zero_pages here: we can only have a
	 * zero_page here if ksm_use_zero_pages was enabled alreaady.
	 */
	if (!is_zero_pfn(page_to_pfn(kpage))) {
		get_page(kpage);
		page_add_ayesn_rmap(kpage, vma, addr, false);
		newpte = mk_pte(kpage, vma->vm_page_prot);
	} else {
		newpte = pte_mkspecial(pfn_pte(page_to_pfn(kpage),
					       vma->vm_page_prot));
		/*
		 * We're replacing an ayesnymous page with a zero page, which is
		 * yest ayesnymous. We need to do proper accounting otherwise we
		 * will get wrong values in /proc, and a BUG message in dmesg
		 * when tearing down the mm.
		 */
		dec_mm_counter(mm, MM_ANONPAGES);
	}

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	/*
	 * No need to yestify as we are replacing a read only page with ayesther
	 * read only page with the same content.
	 *
	 * See Documentation/vm/mmu_yestifier.rst
	 */
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_yestify(mm, addr, ptep, newpte);

	page_remove_rmap(page, false);
	if (!page_mapped(page))
		try_to_free_swap(page);
	put_page(page);

	pte_unmap_unlock(ptep, ptl);
	err = 0;
out_mn:
	mmu_yestifier_invalidate_range_end(&range);
out:
	return err;
}

/*
 * try_to_merge_one_page - take two pages and merge them into one
 * @vma: the vma that holds the pte pointing to page
 * @page: the PageAyesn page that we want to replace with kpage
 * @kpage: the PageKsm page that we want to map instead of page,
 *         or NULL the first time when we want to use page as kpage.
 *
 * This function returns 0 if the pages were merged, -EFAULT otherwise.
 */
static int try_to_merge_one_page(struct vm_area_struct *vma,
				 struct page *page, struct page *kpage)
{
	pte_t orig_pte = __pte(0);
	int err = -EFAULT;

	if (page == kpage)			/* ksm page forked */
		return 0;

	if (!PageAyesn(page))
		goto out;

	/*
	 * We need the page lock to read a stable PageSwapCache in
	 * write_protect_page().  We use trylock_page() instead of
	 * lock_page() because we don't want to wait here - we
	 * prefer to continue scanning and merging different pages,
	 * then come back to this page when it is unlocked.
	 */
	if (!trylock_page(page))
		goto out;

	if (PageTransCompound(page)) {
		if (split_huge_page(page))
			goto out_unlock;
	}

	/*
	 * If this ayesnymous page is mapped only here, its pte may need
	 * to be write-protected.  If it's mapped elsewhere, all of its
	 * ptes are necessarily already write-protected.  But in either
	 * case, we need to lock and check page_count is yest raised.
	 */
	if (write_protect_page(vma, page, &orig_pte) == 0) {
		if (!kpage) {
			/*
			 * While we hold page lock, upgrade page from
			 * PageAyesn+ayesn_vma to PageKsm+NULL stable_yesde:
			 * stable_tree_insert() will update stable_yesde.
			 */
			set_page_stable_yesde(page, NULL);
			mark_page_accessed(page);
			/*
			 * Page reclaim just frees a clean page with yes dirty
			 * ptes: make sure that the ksm page would be swapped.
			 */
			if (!PageDirty(page))
				SetPageDirty(page);
			err = 0;
		} else if (pages_identical(page, kpage))
			err = replace_page(vma, page, kpage, orig_pte);
	}

	if ((vma->vm_flags & VM_LOCKED) && kpage && !err) {
		munlock_vma_page(page);
		if (!PageMlocked(kpage)) {
			unlock_page(page);
			lock_page(kpage);
			mlock_vma_page(kpage);
			page = kpage;		/* for final unlock */
		}
	}

out_unlock:
	unlock_page(page);
out:
	return err;
}

/*
 * try_to_merge_with_ksm_page - like try_to_merge_two_pages,
 * but yes new kernel page is allocated: kpage must already be a ksm page.
 *
 * This function returns 0 if the pages were merged, -EFAULT otherwise.
 */
static int try_to_merge_with_ksm_page(struct rmap_item *rmap_item,
				      struct page *page, struct page *kpage)
{
	struct mm_struct *mm = rmap_item->mm;
	struct vm_area_struct *vma;
	int err = -EFAULT;

	down_read(&mm->mmap_sem);
	vma = find_mergeable_vma(mm, rmap_item->address);
	if (!vma)
		goto out;

	err = try_to_merge_one_page(vma, page, kpage);
	if (err)
		goto out;

	/* Unstable nid is in union with stable ayesn_vma: remove first */
	remove_rmap_item_from_tree(rmap_item);

	/* Must get reference to ayesn_vma while still holding mmap_sem */
	rmap_item->ayesn_vma = vma->ayesn_vma;
	get_ayesn_vma(vma->ayesn_vma);
out:
	up_read(&mm->mmap_sem);
	return err;
}

/*
 * try_to_merge_two_pages - take two identical pages and prepare them
 * to be merged into one page.
 *
 * This function returns the kpage if we successfully merged two identical
 * pages into one ksm page, NULL otherwise.
 *
 * Note that this function upgrades page to ksm page: if one of the pages
 * is already a ksm page, try_to_merge_with_ksm_page should be used.
 */
static struct page *try_to_merge_two_pages(struct rmap_item *rmap_item,
					   struct page *page,
					   struct rmap_item *tree_rmap_item,
					   struct page *tree_page)
{
	int err;

	err = try_to_merge_with_ksm_page(rmap_item, page, NULL);
	if (!err) {
		err = try_to_merge_with_ksm_page(tree_rmap_item,
							tree_page, page);
		/*
		 * If that fails, we have a ksm page with only one pte
		 * pointing to it: so break it.
		 */
		if (err)
			break_cow(rmap_item);
	}
	return err ? NULL : page;
}

static __always_inline
bool __is_page_sharing_candidate(struct stable_yesde *stable_yesde, int offset)
{
	VM_BUG_ON(stable_yesde->rmap_hlist_len < 0);
	/*
	 * Check that at least one mapping still exists, otherwise
	 * there's yes much point to merge and share with this
	 * stable_yesde, as the underlying tree_page of the other
	 * sharer is going to be freed soon.
	 */
	return stable_yesde->rmap_hlist_len &&
		stable_yesde->rmap_hlist_len + offset < ksm_max_page_sharing;
}

static __always_inline
bool is_page_sharing_candidate(struct stable_yesde *stable_yesde)
{
	return __is_page_sharing_candidate(stable_yesde, 0);
}

static struct page *stable_yesde_dup(struct stable_yesde **_stable_yesde_dup,
				    struct stable_yesde **_stable_yesde,
				    struct rb_root *root,
				    bool prune_stale_stable_yesdes)
{
	struct stable_yesde *dup, *found = NULL, *stable_yesde = *_stable_yesde;
	struct hlist_yesde *hlist_safe;
	struct page *_tree_page, *tree_page = NULL;
	int nr = 0;
	int found_rmap_hlist_len;

	if (!prune_stale_stable_yesdes ||
	    time_before(jiffies, stable_yesde->chain_prune_time +
			msecs_to_jiffies(
				ksm_stable_yesde_chains_prune_millisecs)))
		prune_stale_stable_yesdes = false;
	else
		stable_yesde->chain_prune_time = jiffies;

	hlist_for_each_entry_safe(dup, hlist_safe,
				  &stable_yesde->hlist, hlist_dup) {
		cond_resched();
		/*
		 * We must walk all stable_yesde_dup to prune the stale
		 * stable yesdes during lookup.
		 *
		 * get_ksm_page can drop the yesdes from the
		 * stable_yesde->hlist if they point to freed pages
		 * (that's why we do a _safe walk). The "dup"
		 * stable_yesde parameter itself will be freed from
		 * under us if it returns NULL.
		 */
		_tree_page = get_ksm_page(dup, GET_KSM_PAGE_NOLOCK);
		if (!_tree_page)
			continue;
		nr += 1;
		if (is_page_sharing_candidate(dup)) {
			if (!found ||
			    dup->rmap_hlist_len > found_rmap_hlist_len) {
				if (found)
					put_page(tree_page);
				found = dup;
				found_rmap_hlist_len = found->rmap_hlist_len;
				tree_page = _tree_page;

				/* skip put_page for found dup */
				if (!prune_stale_stable_yesdes)
					break;
				continue;
			}
		}
		put_page(_tree_page);
	}

	if (found) {
		/*
		 * nr is counting all dups in the chain only if
		 * prune_stale_stable_yesdes is true, otherwise we may
		 * break the loop at nr == 1 even if there are
		 * multiple entries.
		 */
		if (prune_stale_stable_yesdes && nr == 1) {
			/*
			 * If there's yest just one entry it would
			 * corrupt memory, better BUG_ON. In KSM
			 * context with yes lock held it's yest even
			 * fatal.
			 */
			BUG_ON(stable_yesde->hlist.first->next);

			/*
			 * There's just one entry and it is below the
			 * deduplication limit so drop the chain.
			 */
			rb_replace_yesde(&stable_yesde->yesde, &found->yesde,
					root);
			free_stable_yesde(stable_yesde);
			ksm_stable_yesde_chains--;
			ksm_stable_yesde_dups--;
			/*
			 * NOTE: the caller depends on the stable_yesde
			 * to be equal to stable_yesde_dup if the chain
			 * was collapsed.
			 */
			*_stable_yesde = found;
			/*
			 * Just for robustneess as stable_yesde is
			 * otherwise left as a stable pointer, the
			 * compiler shall optimize it away at build
			 * time.
			 */
			stable_yesde = NULL;
		} else if (stable_yesde->hlist.first != &found->hlist_dup &&
			   __is_page_sharing_candidate(found, 1)) {
			/*
			 * If the found stable_yesde dup can accept one
			 * more future merge (in addition to the one
			 * that is underway) and is yest at the head of
			 * the chain, put it there so next search will
			 * be quicker in the !prune_stale_stable_yesdes
			 * case.
			 *
			 * NOTE: it would be inaccurate to use nr > 1
			 * instead of checking the hlist.first pointer
			 * directly, because in the
			 * prune_stale_stable_yesdes case "nr" isn't
			 * the position of the found dup in the chain,
			 * but the total number of dups in the chain.
			 */
			hlist_del(&found->hlist_dup);
			hlist_add_head(&found->hlist_dup,
				       &stable_yesde->hlist);
		}
	}

	*_stable_yesde_dup = found;
	return tree_page;
}

static struct stable_yesde *stable_yesde_dup_any(struct stable_yesde *stable_yesde,
					       struct rb_root *root)
{
	if (!is_stable_yesde_chain(stable_yesde))
		return stable_yesde;
	if (hlist_empty(&stable_yesde->hlist)) {
		free_stable_yesde_chain(stable_yesde, root);
		return NULL;
	}
	return hlist_entry(stable_yesde->hlist.first,
			   typeof(*stable_yesde), hlist_dup);
}

/*
 * Like for get_ksm_page, this function can free the *_stable_yesde and
 * *_stable_yesde_dup if the returned tree_page is NULL.
 *
 * It can also free and overwrite *_stable_yesde with the found
 * stable_yesde_dup if the chain is collapsed (in which case
 * *_stable_yesde will be equal to *_stable_yesde_dup like if the chain
 * never existed). It's up to the caller to verify tree_page is yest
 * NULL before dereferencing *_stable_yesde or *_stable_yesde_dup.
 *
 * *_stable_yesde_dup is really a second output parameter of this
 * function and will be overwritten in all cases, the caller doesn't
 * need to initialize it.
 */
static struct page *__stable_yesde_chain(struct stable_yesde **_stable_yesde_dup,
					struct stable_yesde **_stable_yesde,
					struct rb_root *root,
					bool prune_stale_stable_yesdes)
{
	struct stable_yesde *stable_yesde = *_stable_yesde;
	if (!is_stable_yesde_chain(stable_yesde)) {
		if (is_page_sharing_candidate(stable_yesde)) {
			*_stable_yesde_dup = stable_yesde;
			return get_ksm_page(stable_yesde, GET_KSM_PAGE_NOLOCK);
		}
		/*
		 * _stable_yesde_dup set to NULL means the stable_yesde
		 * reached the ksm_max_page_sharing limit.
		 */
		*_stable_yesde_dup = NULL;
		return NULL;
	}
	return stable_yesde_dup(_stable_yesde_dup, _stable_yesde, root,
			       prune_stale_stable_yesdes);
}

static __always_inline struct page *chain_prune(struct stable_yesde **s_n_d,
						struct stable_yesde **s_n,
						struct rb_root *root)
{
	return __stable_yesde_chain(s_n_d, s_n, root, true);
}

static __always_inline struct page *chain(struct stable_yesde **s_n_d,
					  struct stable_yesde *s_n,
					  struct rb_root *root)
{
	struct stable_yesde *old_stable_yesde = s_n;
	struct page *tree_page;

	tree_page = __stable_yesde_chain(s_n_d, &s_n, root, false);
	/* yest pruning dups so s_n canyest have changed */
	VM_BUG_ON(s_n != old_stable_yesde);
	return tree_page;
}

/*
 * stable_tree_search - search for page inside the stable tree
 *
 * This function checks if there is a page inside the stable tree
 * with identical content to the page that we are scanning right yesw.
 *
 * This function returns the stable tree yesde of identical content if found,
 * NULL otherwise.
 */
static struct page *stable_tree_search(struct page *page)
{
	int nid;
	struct rb_root *root;
	struct rb_yesde **new;
	struct rb_yesde *parent;
	struct stable_yesde *stable_yesde, *stable_yesde_dup, *stable_yesde_any;
	struct stable_yesde *page_yesde;

	page_yesde = page_stable_yesde(page);
	if (page_yesde && page_yesde->head != &migrate_yesdes) {
		/* ksm page forked */
		get_page(page);
		return page;
	}

	nid = get_kpfn_nid(page_to_pfn(page));
	root = root_stable_tree + nid;
again:
	new = &root->rb_yesde;
	parent = NULL;

	while (*new) {
		struct page *tree_page;
		int ret;

		cond_resched();
		stable_yesde = rb_entry(*new, struct stable_yesde, yesde);
		stable_yesde_any = NULL;
		tree_page = chain_prune(&stable_yesde_dup, &stable_yesde,	root);
		/*
		 * NOTE: stable_yesde may have been freed by
		 * chain_prune() if the returned stable_yesde_dup is
		 * yest NULL. stable_yesde_dup may have been inserted in
		 * the rbtree instead as a regular stable_yesde (in
		 * order to collapse the stable_yesde chain if a single
		 * stable_yesde dup was found in it). In such case the
		 * stable_yesde is overwritten by the calleee to point
		 * to the stable_yesde_dup that was collapsed in the
		 * stable rbtree and stable_yesde will be equal to
		 * stable_yesde_dup like if the chain never existed.
		 */
		if (!stable_yesde_dup) {
			/*
			 * Either all stable_yesde dups were full in
			 * this stable_yesde chain, or this chain was
			 * empty and should be rb_erased.
			 */
			stable_yesde_any = stable_yesde_dup_any(stable_yesde,
							      root);
			if (!stable_yesde_any) {
				/* rb_erase just run */
				goto again;
			}
			/*
			 * Take any of the stable_yesde dups page of
			 * this stable_yesde chain to let the tree walk
			 * continue. All KSM pages belonging to the
			 * stable_yesde dups in a stable_yesde chain
			 * have the same content and they're
			 * wrprotected at all times. Any will work
			 * fine to continue the walk.
			 */
			tree_page = get_ksm_page(stable_yesde_any,
						 GET_KSM_PAGE_NOLOCK);
		}
		VM_BUG_ON(!stable_yesde_dup ^ !!stable_yesde_any);
		if (!tree_page) {
			/*
			 * If we walked over a stale stable_yesde,
			 * get_ksm_page() will call rb_erase() and it
			 * may rebalance the tree from under us. So
			 * restart the search from scratch. Returning
			 * NULL would be safe too, but we'd generate
			 * false negative insertions just because some
			 * stable_yesde was stale.
			 */
			goto again;
		}

		ret = memcmp_pages(page, tree_page);
		put_page(tree_page);

		parent = *new;
		if (ret < 0)
			new = &parent->rb_left;
		else if (ret > 0)
			new = &parent->rb_right;
		else {
			if (page_yesde) {
				VM_BUG_ON(page_yesde->head != &migrate_yesdes);
				/*
				 * Test if the migrated page should be merged
				 * into a stable yesde dup. If the mapcount is
				 * 1 we can migrate it with ayesther KSM page
				 * without adding it to the chain.
				 */
				if (page_mapcount(page) > 1)
					goto chain_append;
			}

			if (!stable_yesde_dup) {
				/*
				 * If the stable_yesde is a chain and
				 * we got a payload match in memcmp
				 * but we canyest merge the scanned
				 * page in any of the existing
				 * stable_yesde dups because they're
				 * all full, we need to wait the
				 * scanned page to find itself a match
				 * in the unstable tree to create a
				 * brand new KSM page to add later to
				 * the dups of this stable_yesde.
				 */
				return NULL;
			}

			/*
			 * Lock and unlock the stable_yesde's page (which
			 * might already have been migrated) so that page
			 * migration is sure to yestice its raised count.
			 * It would be more elegant to return stable_yesde
			 * than kpage, but that involves more changes.
			 */
			tree_page = get_ksm_page(stable_yesde_dup,
						 GET_KSM_PAGE_TRYLOCK);

			if (PTR_ERR(tree_page) == -EBUSY)
				return ERR_PTR(-EBUSY);

			if (unlikely(!tree_page))
				/*
				 * The tree may have been rebalanced,
				 * so re-evaluate parent and new.
				 */
				goto again;
			unlock_page(tree_page);

			if (get_kpfn_nid(stable_yesde_dup->kpfn) !=
			    NUMA(stable_yesde_dup->nid)) {
				put_page(tree_page);
				goto replace;
			}
			return tree_page;
		}
	}

	if (!page_yesde)
		return NULL;

	list_del(&page_yesde->list);
	DO_NUMA(page_yesde->nid = nid);
	rb_link_yesde(&page_yesde->yesde, parent, new);
	rb_insert_color(&page_yesde->yesde, root);
out:
	if (is_page_sharing_candidate(page_yesde)) {
		get_page(page);
		return page;
	} else
		return NULL;

replace:
	/*
	 * If stable_yesde was a chain and chain_prune collapsed it,
	 * stable_yesde has been updated to be the new regular
	 * stable_yesde. A collapse of the chain is indistinguishable
	 * from the case there was yes chain in the stable
	 * rbtree. Otherwise stable_yesde is the chain and
	 * stable_yesde_dup is the dup to replace.
	 */
	if (stable_yesde_dup == stable_yesde) {
		VM_BUG_ON(is_stable_yesde_chain(stable_yesde_dup));
		VM_BUG_ON(is_stable_yesde_dup(stable_yesde_dup));
		/* there is yes chain */
		if (page_yesde) {
			VM_BUG_ON(page_yesde->head != &migrate_yesdes);
			list_del(&page_yesde->list);
			DO_NUMA(page_yesde->nid = nid);
			rb_replace_yesde(&stable_yesde_dup->yesde,
					&page_yesde->yesde,
					root);
			if (is_page_sharing_candidate(page_yesde))
				get_page(page);
			else
				page = NULL;
		} else {
			rb_erase(&stable_yesde_dup->yesde, root);
			page = NULL;
		}
	} else {
		VM_BUG_ON(!is_stable_yesde_chain(stable_yesde));
		__stable_yesde_dup_del(stable_yesde_dup);
		if (page_yesde) {
			VM_BUG_ON(page_yesde->head != &migrate_yesdes);
			list_del(&page_yesde->list);
			DO_NUMA(page_yesde->nid = nid);
			stable_yesde_chain_add_dup(page_yesde, stable_yesde);
			if (is_page_sharing_candidate(page_yesde))
				get_page(page);
			else
				page = NULL;
		} else {
			page = NULL;
		}
	}
	stable_yesde_dup->head = &migrate_yesdes;
	list_add(&stable_yesde_dup->list, stable_yesde_dup->head);
	return page;

chain_append:
	/* stable_yesde_dup could be null if it reached the limit */
	if (!stable_yesde_dup)
		stable_yesde_dup = stable_yesde_any;
	/*
	 * If stable_yesde was a chain and chain_prune collapsed it,
	 * stable_yesde has been updated to be the new regular
	 * stable_yesde. A collapse of the chain is indistinguishable
	 * from the case there was yes chain in the stable
	 * rbtree. Otherwise stable_yesde is the chain and
	 * stable_yesde_dup is the dup to replace.
	 */
	if (stable_yesde_dup == stable_yesde) {
		VM_BUG_ON(is_stable_yesde_chain(stable_yesde_dup));
		VM_BUG_ON(is_stable_yesde_dup(stable_yesde_dup));
		/* chain is missing so create it */
		stable_yesde = alloc_stable_yesde_chain(stable_yesde_dup,
						      root);
		if (!stable_yesde)
			return NULL;
	}
	/*
	 * Add this stable_yesde dup that was
	 * migrated to the stable_yesde chain
	 * of the current nid for this page
	 * content.
	 */
	VM_BUG_ON(!is_stable_yesde_chain(stable_yesde));
	VM_BUG_ON(!is_stable_yesde_dup(stable_yesde_dup));
	VM_BUG_ON(page_yesde->head != &migrate_yesdes);
	list_del(&page_yesde->list);
	DO_NUMA(page_yesde->nid = nid);
	stable_yesde_chain_add_dup(page_yesde, stable_yesde);
	goto out;
}

/*
 * stable_tree_insert - insert stable tree yesde pointing to new ksm page
 * into the stable tree.
 *
 * This function returns the stable tree yesde just allocated on success,
 * NULL otherwise.
 */
static struct stable_yesde *stable_tree_insert(struct page *kpage)
{
	int nid;
	unsigned long kpfn;
	struct rb_root *root;
	struct rb_yesde **new;
	struct rb_yesde *parent;
	struct stable_yesde *stable_yesde, *stable_yesde_dup, *stable_yesde_any;
	bool need_chain = false;

	kpfn = page_to_pfn(kpage);
	nid = get_kpfn_nid(kpfn);
	root = root_stable_tree + nid;
again:
	parent = NULL;
	new = &root->rb_yesde;

	while (*new) {
		struct page *tree_page;
		int ret;

		cond_resched();
		stable_yesde = rb_entry(*new, struct stable_yesde, yesde);
		stable_yesde_any = NULL;
		tree_page = chain(&stable_yesde_dup, stable_yesde, root);
		if (!stable_yesde_dup) {
			/*
			 * Either all stable_yesde dups were full in
			 * this stable_yesde chain, or this chain was
			 * empty and should be rb_erased.
			 */
			stable_yesde_any = stable_yesde_dup_any(stable_yesde,
							      root);
			if (!stable_yesde_any) {
				/* rb_erase just run */
				goto again;
			}
			/*
			 * Take any of the stable_yesde dups page of
			 * this stable_yesde chain to let the tree walk
			 * continue. All KSM pages belonging to the
			 * stable_yesde dups in a stable_yesde chain
			 * have the same content and they're
			 * wrprotected at all times. Any will work
			 * fine to continue the walk.
			 */
			tree_page = get_ksm_page(stable_yesde_any,
						 GET_KSM_PAGE_NOLOCK);
		}
		VM_BUG_ON(!stable_yesde_dup ^ !!stable_yesde_any);
		if (!tree_page) {
			/*
			 * If we walked over a stale stable_yesde,
			 * get_ksm_page() will call rb_erase() and it
			 * may rebalance the tree from under us. So
			 * restart the search from scratch. Returning
			 * NULL would be safe too, but we'd generate
			 * false negative insertions just because some
			 * stable_yesde was stale.
			 */
			goto again;
		}

		ret = memcmp_pages(kpage, tree_page);
		put_page(tree_page);

		parent = *new;
		if (ret < 0)
			new = &parent->rb_left;
		else if (ret > 0)
			new = &parent->rb_right;
		else {
			need_chain = true;
			break;
		}
	}

	stable_yesde_dup = alloc_stable_yesde();
	if (!stable_yesde_dup)
		return NULL;

	INIT_HLIST_HEAD(&stable_yesde_dup->hlist);
	stable_yesde_dup->kpfn = kpfn;
	set_page_stable_yesde(kpage, stable_yesde_dup);
	stable_yesde_dup->rmap_hlist_len = 0;
	DO_NUMA(stable_yesde_dup->nid = nid);
	if (!need_chain) {
		rb_link_yesde(&stable_yesde_dup->yesde, parent, new);
		rb_insert_color(&stable_yesde_dup->yesde, root);
	} else {
		if (!is_stable_yesde_chain(stable_yesde)) {
			struct stable_yesde *orig = stable_yesde;
			/* chain is missing so create it */
			stable_yesde = alloc_stable_yesde_chain(orig, root);
			if (!stable_yesde) {
				free_stable_yesde(stable_yesde_dup);
				return NULL;
			}
		}
		stable_yesde_chain_add_dup(stable_yesde_dup, stable_yesde);
	}

	return stable_yesde_dup;
}

/*
 * unstable_tree_search_insert - search for identical page,
 * else insert rmap_item into the unstable tree.
 *
 * This function searches for a page in the unstable tree identical to the
 * page currently being scanned; and if yes identical page is found in the
 * tree, we insert rmap_item as a new object into the unstable tree.
 *
 * This function returns pointer to rmap_item found to be identical
 * to the currently scanned page, NULL otherwise.
 *
 * This function does both searching and inserting, because they share
 * the same walking algorithm in an rbtree.
 */
static
struct rmap_item *unstable_tree_search_insert(struct rmap_item *rmap_item,
					      struct page *page,
					      struct page **tree_pagep)
{
	struct rb_yesde **new;
	struct rb_root *root;
	struct rb_yesde *parent = NULL;
	int nid;

	nid = get_kpfn_nid(page_to_pfn(page));
	root = root_unstable_tree + nid;
	new = &root->rb_yesde;

	while (*new) {
		struct rmap_item *tree_rmap_item;
		struct page *tree_page;
		int ret;

		cond_resched();
		tree_rmap_item = rb_entry(*new, struct rmap_item, yesde);
		tree_page = get_mergeable_page(tree_rmap_item);
		if (!tree_page)
			return NULL;

		/*
		 * Don't substitute a ksm page for a forked page.
		 */
		if (page == tree_page) {
			put_page(tree_page);
			return NULL;
		}

		ret = memcmp_pages(page, tree_page);

		parent = *new;
		if (ret < 0) {
			put_page(tree_page);
			new = &parent->rb_left;
		} else if (ret > 0) {
			put_page(tree_page);
			new = &parent->rb_right;
		} else if (!ksm_merge_across_yesdes &&
			   page_to_nid(tree_page) != nid) {
			/*
			 * If tree_page has been migrated to ayesther NUMA yesde,
			 * it will be flushed out and put in the right unstable
			 * tree next time: only merge with it when across_yesdes.
			 */
			put_page(tree_page);
			return NULL;
		} else {
			*tree_pagep = tree_page;
			return tree_rmap_item;
		}
	}

	rmap_item->address |= UNSTABLE_FLAG;
	rmap_item->address |= (ksm_scan.seqnr & SEQNR_MASK);
	DO_NUMA(rmap_item->nid = nid);
	rb_link_yesde(&rmap_item->yesde, parent, new);
	rb_insert_color(&rmap_item->yesde, root);

	ksm_pages_unshared++;
	return NULL;
}

/*
 * stable_tree_append - add ayesther rmap_item to the linked list of
 * rmap_items hanging off a given yesde of the stable tree, all sharing
 * the same ksm page.
 */
static void stable_tree_append(struct rmap_item *rmap_item,
			       struct stable_yesde *stable_yesde,
			       bool max_page_sharing_bypass)
{
	/*
	 * rmap won't find this mapping if we don't insert the
	 * rmap_item in the right stable_yesde
	 * duplicate. page_migration could break later if rmap breaks,
	 * so we can as well crash here. We really need to check for
	 * rmap_hlist_len == STABLE_NODE_CHAIN, but we can as well check
	 * for other negative values as an undeflow if detected here
	 * for the first time (and yest when decreasing rmap_hlist_len)
	 * would be sign of memory corruption in the stable_yesde.
	 */
	BUG_ON(stable_yesde->rmap_hlist_len < 0);

	stable_yesde->rmap_hlist_len++;
	if (!max_page_sharing_bypass)
		/* possibly yesn fatal but unexpected overflow, only warn */
		WARN_ON_ONCE(stable_yesde->rmap_hlist_len >
			     ksm_max_page_sharing);

	rmap_item->head = stable_yesde;
	rmap_item->address |= STABLE_FLAG;
	hlist_add_head(&rmap_item->hlist, &stable_yesde->hlist);

	if (rmap_item->hlist.next)
		ksm_pages_sharing++;
	else
		ksm_pages_shared++;
}

/*
 * cmp_and_merge_page - first see if page can be merged into the stable tree;
 * if yest, compare checksum to previous and if it's the same, see if page can
 * be inserted into the unstable tree, or merged with a page already there and
 * both transferred to the stable tree.
 *
 * @page: the page that we are searching identical page to.
 * @rmap_item: the reverse mapping into the virtual address of this page
 */
static void cmp_and_merge_page(struct page *page, struct rmap_item *rmap_item)
{
	struct mm_struct *mm = rmap_item->mm;
	struct rmap_item *tree_rmap_item;
	struct page *tree_page = NULL;
	struct stable_yesde *stable_yesde;
	struct page *kpage;
	unsigned int checksum;
	int err;
	bool max_page_sharing_bypass = false;

	stable_yesde = page_stable_yesde(page);
	if (stable_yesde) {
		if (stable_yesde->head != &migrate_yesdes &&
		    get_kpfn_nid(READ_ONCE(stable_yesde->kpfn)) !=
		    NUMA(stable_yesde->nid)) {
			stable_yesde_dup_del(stable_yesde);
			stable_yesde->head = &migrate_yesdes;
			list_add(&stable_yesde->list, stable_yesde->head);
		}
		if (stable_yesde->head != &migrate_yesdes &&
		    rmap_item->head == stable_yesde)
			return;
		/*
		 * If it's a KSM fork, allow it to go over the sharing limit
		 * without warnings.
		 */
		if (!is_page_sharing_candidate(stable_yesde))
			max_page_sharing_bypass = true;
	}

	/* We first start with searching the page inside the stable tree */
	kpage = stable_tree_search(page);
	if (kpage == page && rmap_item->head == stable_yesde) {
		put_page(kpage);
		return;
	}

	remove_rmap_item_from_tree(rmap_item);

	if (kpage) {
		if (PTR_ERR(kpage) == -EBUSY)
			return;

		err = try_to_merge_with_ksm_page(rmap_item, page, kpage);
		if (!err) {
			/*
			 * The page was successfully merged:
			 * add its rmap_item to the stable tree.
			 */
			lock_page(kpage);
			stable_tree_append(rmap_item, page_stable_yesde(kpage),
					   max_page_sharing_bypass);
			unlock_page(kpage);
		}
		put_page(kpage);
		return;
	}

	/*
	 * If the hash value of the page has changed from the last time
	 * we calculated it, this page is changing frequently: therefore we
	 * don't want to insert it in the unstable tree, and we don't want
	 * to waste our time searching for something identical to it there.
	 */
	checksum = calc_checksum(page);
	if (rmap_item->oldchecksum != checksum) {
		rmap_item->oldchecksum = checksum;
		return;
	}

	/*
	 * Same checksum as an empty page. We attempt to merge it with the
	 * appropriate zero page if the user enabled this via sysfs.
	 */
	if (ksm_use_zero_pages && (checksum == zero_checksum)) {
		struct vm_area_struct *vma;

		down_read(&mm->mmap_sem);
		vma = find_mergeable_vma(mm, rmap_item->address);
		err = try_to_merge_one_page(vma, page,
					    ZERO_PAGE(rmap_item->address));
		up_read(&mm->mmap_sem);
		/*
		 * In case of failure, the page was yest really empty, so we
		 * need to continue. Otherwise we're done.
		 */
		if (!err)
			return;
	}
	tree_rmap_item =
		unstable_tree_search_insert(rmap_item, page, &tree_page);
	if (tree_rmap_item) {
		bool split;

		kpage = try_to_merge_two_pages(rmap_item, page,
						tree_rmap_item, tree_page);
		/*
		 * If both pages we tried to merge belong to the same compound
		 * page, then we actually ended up increasing the reference
		 * count of the same compound page twice, and split_huge_page
		 * failed.
		 * Here we set a flag if that happened, and we use it later to
		 * try split_huge_page again. Since we call put_page right
		 * afterwards, the reference count will be correct and
		 * split_huge_page should succeed.
		 */
		split = PageTransCompound(page)
			&& compound_head(page) == compound_head(tree_page);
		put_page(tree_page);
		if (kpage) {
			/*
			 * The pages were successfully merged: insert new
			 * yesde in the stable tree and add both rmap_items.
			 */
			lock_page(kpage);
			stable_yesde = stable_tree_insert(kpage);
			if (stable_yesde) {
				stable_tree_append(tree_rmap_item, stable_yesde,
						   false);
				stable_tree_append(rmap_item, stable_yesde,
						   false);
			}
			unlock_page(kpage);

			/*
			 * If we fail to insert the page into the stable tree,
			 * we will have 2 virtual addresses that are pointing
			 * to a ksm page left outside the stable tree,
			 * in which case we need to break_cow on both.
			 */
			if (!stable_yesde) {
				break_cow(tree_rmap_item);
				break_cow(rmap_item);
			}
		} else if (split) {
			/*
			 * We are here if we tried to merge two pages and
			 * failed because they both belonged to the same
			 * compound page. We will split the page yesw, but yes
			 * merging will take place.
			 * We do yest want to add the cost of a full lock; if
			 * the page is locked, it is better to skip it and
			 * perhaps try again later.
			 */
			if (!trylock_page(page))
				return;
			split_huge_page(page);
			unlock_page(page);
		}
	}
}

static struct rmap_item *get_next_rmap_item(struct mm_slot *mm_slot,
					    struct rmap_item **rmap_list,
					    unsigned long addr)
{
	struct rmap_item *rmap_item;

	while (*rmap_list) {
		rmap_item = *rmap_list;
		if ((rmap_item->address & PAGE_MASK) == addr)
			return rmap_item;
		if (rmap_item->address > addr)
			break;
		*rmap_list = rmap_item->rmap_list;
		remove_rmap_item_from_tree(rmap_item);
		free_rmap_item(rmap_item);
	}

	rmap_item = alloc_rmap_item();
	if (rmap_item) {
		/* It has already been zeroed */
		rmap_item->mm = mm_slot->mm;
		rmap_item->address = addr;
		rmap_item->rmap_list = *rmap_list;
		*rmap_list = rmap_item;
	}
	return rmap_item;
}

static struct rmap_item *scan_get_next_rmap_item(struct page **page)
{
	struct mm_struct *mm;
	struct mm_slot *slot;
	struct vm_area_struct *vma;
	struct rmap_item *rmap_item;
	int nid;

	if (list_empty(&ksm_mm_head.mm_list))
		return NULL;

	slot = ksm_scan.mm_slot;
	if (slot == &ksm_mm_head) {
		/*
		 * A number of pages can hang around indefinitely on per-cpu
		 * pagevecs, raised page count preventing write_protect_page
		 * from merging them.  Though it doesn't really matter much,
		 * it is puzzling to see some stuck in pages_volatile until
		 * other activity jostles them out, and they also prevented
		 * LTP's KSM test from succeeding deterministically; so drain
		 * them here (here rather than on entry to ksm_do_scan(),
		 * so we don't IPI too often when pages_to_scan is set low).
		 */
		lru_add_drain_all();

		/*
		 * Whereas stale stable_yesdes on the stable_tree itself
		 * get pruned in the regular course of stable_tree_search(),
		 * those moved out to the migrate_yesdes list can accumulate:
		 * so prune them once before each full scan.
		 */
		if (!ksm_merge_across_yesdes) {
			struct stable_yesde *stable_yesde, *next;
			struct page *page;

			list_for_each_entry_safe(stable_yesde, next,
						 &migrate_yesdes, list) {
				page = get_ksm_page(stable_yesde,
						    GET_KSM_PAGE_NOLOCK);
				if (page)
					put_page(page);
				cond_resched();
			}
		}

		for (nid = 0; nid < ksm_nr_yesde_ids; nid++)
			root_unstable_tree[nid] = RB_ROOT;

		spin_lock(&ksm_mmlist_lock);
		slot = list_entry(slot->mm_list.next, struct mm_slot, mm_list);
		ksm_scan.mm_slot = slot;
		spin_unlock(&ksm_mmlist_lock);
		/*
		 * Although we tested list_empty() above, a racing __ksm_exit
		 * of the last mm on the list may have removed it since then.
		 */
		if (slot == &ksm_mm_head)
			return NULL;
next_mm:
		ksm_scan.address = 0;
		ksm_scan.rmap_list = &slot->rmap_list;
	}

	mm = slot->mm;
	down_read(&mm->mmap_sem);
	if (ksm_test_exit(mm))
		vma = NULL;
	else
		vma = find_vma(mm, ksm_scan.address);

	for (; vma; vma = vma->vm_next) {
		if (!(vma->vm_flags & VM_MERGEABLE))
			continue;
		if (ksm_scan.address < vma->vm_start)
			ksm_scan.address = vma->vm_start;
		if (!vma->ayesn_vma)
			ksm_scan.address = vma->vm_end;

		while (ksm_scan.address < vma->vm_end) {
			if (ksm_test_exit(mm))
				break;
			*page = follow_page(vma, ksm_scan.address, FOLL_GET);
			if (IS_ERR_OR_NULL(*page)) {
				ksm_scan.address += PAGE_SIZE;
				cond_resched();
				continue;
			}
			if (PageAyesn(*page)) {
				flush_ayesn_page(vma, *page, ksm_scan.address);
				flush_dcache_page(*page);
				rmap_item = get_next_rmap_item(slot,
					ksm_scan.rmap_list, ksm_scan.address);
				if (rmap_item) {
					ksm_scan.rmap_list =
							&rmap_item->rmap_list;
					ksm_scan.address += PAGE_SIZE;
				} else
					put_page(*page);
				up_read(&mm->mmap_sem);
				return rmap_item;
			}
			put_page(*page);
			ksm_scan.address += PAGE_SIZE;
			cond_resched();
		}
	}

	if (ksm_test_exit(mm)) {
		ksm_scan.address = 0;
		ksm_scan.rmap_list = &slot->rmap_list;
	}
	/*
	 * Nuke all the rmap_items that are above this current rmap:
	 * because there were yes VM_MERGEABLE vmas with such addresses.
	 */
	remove_trailing_rmap_items(slot, ksm_scan.rmap_list);

	spin_lock(&ksm_mmlist_lock);
	ksm_scan.mm_slot = list_entry(slot->mm_list.next,
						struct mm_slot, mm_list);
	if (ksm_scan.address == 0) {
		/*
		 * We've completed a full scan of all vmas, holding mmap_sem
		 * throughout, and found yes VM_MERGEABLE: so do the same as
		 * __ksm_exit does to remove this mm from all our lists yesw.
		 * This applies either when cleaning up after __ksm_exit
		 * (but beware: we can reach here even before __ksm_exit),
		 * or when all VM_MERGEABLE areas have been unmapped (and
		 * mmap_sem then protects against race with MADV_MERGEABLE).
		 */
		hash_del(&slot->link);
		list_del(&slot->mm_list);
		spin_unlock(&ksm_mmlist_lock);

		free_mm_slot(slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		up_read(&mm->mmap_sem);
		mmdrop(mm);
	} else {
		up_read(&mm->mmap_sem);
		/*
		 * up_read(&mm->mmap_sem) first because after
		 * spin_unlock(&ksm_mmlist_lock) run, the "mm" may
		 * already have been freed under us by __ksm_exit()
		 * because the "mm_slot" is still hashed and
		 * ksm_scan.mm_slot doesn't point to it anymore.
		 */
		spin_unlock(&ksm_mmlist_lock);
	}

	/* Repeat until we've completed scanning the whole list */
	slot = ksm_scan.mm_slot;
	if (slot != &ksm_mm_head)
		goto next_mm;

	ksm_scan.seqnr++;
	return NULL;
}

/**
 * ksm_do_scan  - the ksm scanner main worker function.
 * @scan_npages:  number of pages we want to scan before we return.
 */
static void ksm_do_scan(unsigned int scan_npages)
{
	struct rmap_item *rmap_item;
	struct page *uninitialized_var(page);

	while (scan_npages-- && likely(!freezing(current))) {
		cond_resched();
		rmap_item = scan_get_next_rmap_item(&page);
		if (!rmap_item)
			return;
		cmp_and_merge_page(page, rmap_item);
		put_page(page);
	}
}

static int ksmd_should_run(void)
{
	return (ksm_run & KSM_RUN_MERGE) && !list_empty(&ksm_mm_head.mm_list);
}

static int ksm_scan_thread(void *yesthing)
{
	unsigned int sleep_ms;

	set_freezable();
	set_user_nice(current, 5);

	while (!kthread_should_stop()) {
		mutex_lock(&ksm_thread_mutex);
		wait_while_offlining();
		if (ksmd_should_run())
			ksm_do_scan(ksm_thread_pages_to_scan);
		mutex_unlock(&ksm_thread_mutex);

		try_to_freeze();

		if (ksmd_should_run()) {
			sleep_ms = READ_ONCE(ksm_thread_sleep_millisecs);
			wait_event_interruptible_timeout(ksm_iter_wait,
				sleep_ms != READ_ONCE(ksm_thread_sleep_millisecs),
				msecs_to_jiffies(sleep_ms));
		} else {
			wait_event_freezable(ksm_thread_wait,
				ksmd_should_run() || kthread_should_stop());
		}
	}
	return 0;
}

int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	int err;

	switch (advice) {
	case MADV_MERGEABLE:
		/*
		 * Be somewhat over-protective for yesw!
		 */
		if (*vm_flags & (VM_MERGEABLE | VM_SHARED  | VM_MAYSHARE   |
				 VM_PFNMAP    | VM_IO      | VM_DONTEXPAND |
				 VM_HUGETLB | VM_MIXEDMAP))
			return 0;		/* just igyesre the advice */

		if (vma_is_dax(vma))
			return 0;

#ifdef VM_SAO
		if (*vm_flags & VM_SAO)
			return 0;
#endif
#ifdef VM_SPARC_ADI
		if (*vm_flags & VM_SPARC_ADI)
			return 0;
#endif

		if (!test_bit(MMF_VM_MERGEABLE, &mm->flags)) {
			err = __ksm_enter(mm);
			if (err)
				return err;
		}

		*vm_flags |= VM_MERGEABLE;
		break;

	case MADV_UNMERGEABLE:
		if (!(*vm_flags & VM_MERGEABLE))
			return 0;		/* just igyesre the advice */

		if (vma->ayesn_vma) {
			err = unmerge_ksm_pages(vma, start, end);
			if (err)
				return err;
		}

		*vm_flags &= ~VM_MERGEABLE;
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ksm_madvise);

int __ksm_enter(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int needs_wakeup;

	mm_slot = alloc_mm_slot();
	if (!mm_slot)
		return -ENOMEM;

	/* Check ksm_run too?  Would need tighter locking */
	needs_wakeup = list_empty(&ksm_mm_head.mm_list);

	spin_lock(&ksm_mmlist_lock);
	insert_to_mm_slots_hash(mm, mm_slot);
	/*
	 * When KSM_RUN_MERGE (or KSM_RUN_STOP),
	 * insert just behind the scanning cursor, to let the area settle
	 * down a little; when fork is followed by immediate exec, we don't
	 * want ksmd to waste time setting up and tearing down an rmap_list.
	 *
	 * But when KSM_RUN_UNMERGE, it's important to insert ahead of its
	 * scanning cursor, otherwise KSM pages in newly forked mms will be
	 * missed: then we might as well insert at the end of the list.
	 */
	if (ksm_run & KSM_RUN_UNMERGE)
		list_add_tail(&mm_slot->mm_list, &ksm_mm_head.mm_list);
	else
		list_add_tail(&mm_slot->mm_list, &ksm_scan.mm_slot->mm_list);
	spin_unlock(&ksm_mmlist_lock);

	set_bit(MMF_VM_MERGEABLE, &mm->flags);
	mmgrab(mm);

	if (needs_wakeup)
		wake_up_interruptible(&ksm_thread_wait);

	return 0;
}

void __ksm_exit(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int easy_to_free = 0;

	/*
	 * This process is exiting: if it's straightforward (as is the
	 * case when ksmd was never running), free mm_slot immediately.
	 * But if it's at the cursor or has rmap_items linked to it, use
	 * mmap_sem to synchronize with any break_cows before pagetables
	 * are freed, and leave the mm_slot on the list for ksmd to free.
	 * Beware: ksm may already have yesticed it exiting and freed the slot.
	 */

	spin_lock(&ksm_mmlist_lock);
	mm_slot = get_mm_slot(mm);
	if (mm_slot && ksm_scan.mm_slot != mm_slot) {
		if (!mm_slot->rmap_list) {
			hash_del(&mm_slot->link);
			list_del(&mm_slot->mm_list);
			easy_to_free = 1;
		} else {
			list_move(&mm_slot->mm_list,
				  &ksm_scan.mm_slot->mm_list);
		}
	}
	spin_unlock(&ksm_mmlist_lock);

	if (easy_to_free) {
		free_mm_slot(mm_slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		mmdrop(mm);
	} else if (mm_slot) {
		down_write(&mm->mmap_sem);
		up_write(&mm->mmap_sem);
	}
}

struct page *ksm_might_need_to_copy(struct page *page,
			struct vm_area_struct *vma, unsigned long address)
{
	struct ayesn_vma *ayesn_vma = page_ayesn_vma(page);
	struct page *new_page;

	if (PageKsm(page)) {
		if (page_stable_yesde(page) &&
		    !(ksm_run & KSM_RUN_UNMERGE))
			return page;	/* yes need to copy it */
	} else if (!ayesn_vma) {
		return page;		/* yes need to copy it */
	} else if (ayesn_vma->root == vma->ayesn_vma->root &&
		 page->index == linear_page_index(vma, address)) {
		return page;		/* still yes need to copy it */
	}
	if (!PageUptodate(page))
		return page;		/* let do_swap_page report the error */

	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
	if (new_page) {
		copy_user_highpage(new_page, page, address, vma);

		SetPageDirty(new_page);
		__SetPageUptodate(new_page);
		__SetPageLocked(new_page);
	}

	return new_page;
}

void rmap_walk_ksm(struct page *page, struct rmap_walk_control *rwc)
{
	struct stable_yesde *stable_yesde;
	struct rmap_item *rmap_item;
	int search_new_forks = 0;

	VM_BUG_ON_PAGE(!PageKsm(page), page);

	/*
	 * Rely on the page lock to protect against concurrent modifications
	 * to that page's yesde of the stable tree.
	 */
	VM_BUG_ON_PAGE(!PageLocked(page), page);

	stable_yesde = page_stable_yesde(page);
	if (!stable_yesde)
		return;
again:
	hlist_for_each_entry(rmap_item, &stable_yesde->hlist, hlist) {
		struct ayesn_vma *ayesn_vma = rmap_item->ayesn_vma;
		struct ayesn_vma_chain *vmac;
		struct vm_area_struct *vma;

		cond_resched();
		ayesn_vma_lock_read(ayesn_vma);
		ayesn_vma_interval_tree_foreach(vmac, &ayesn_vma->rb_root,
					       0, ULONG_MAX) {
			unsigned long addr;

			cond_resched();
			vma = vmac->vma;

			/* Igyesre the stable/unstable/sqnr flags */
			addr = rmap_item->address & ~KSM_FLAG_MASK;

			if (addr < vma->vm_start || addr >= vma->vm_end)
				continue;
			/*
			 * Initially we examine only the vma which covers this
			 * rmap_item; but later, if there is still work to do,
			 * we examine covering vmas in other mms: in case they
			 * were forked from the original since ksmd passed.
			 */
			if ((rmap_item->mm == vma->vm_mm) == search_new_forks)
				continue;

			if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
				continue;

			if (!rwc->rmap_one(page, vma, addr, rwc->arg)) {
				ayesn_vma_unlock_read(ayesn_vma);
				return;
			}
			if (rwc->done && rwc->done(page)) {
				ayesn_vma_unlock_read(ayesn_vma);
				return;
			}
		}
		ayesn_vma_unlock_read(ayesn_vma);
	}
	if (!search_new_forks++)
		goto again;
}

bool reuse_ksm_page(struct page *page,
		    struct vm_area_struct *vma,
		    unsigned long address)
{
#ifdef CONFIG_DEBUG_VM
	if (WARN_ON(is_zero_pfn(page_to_pfn(page))) ||
			WARN_ON(!page_mapped(page)) ||
			WARN_ON(!PageLocked(page))) {
		dump_page(page, "reuse_ksm_page");
		return false;
	}
#endif

	if (PageSwapCache(page) || !page_stable_yesde(page))
		return false;
	/* Prohibit parallel get_ksm_page() */
	if (!page_ref_freeze(page, 1))
		return false;

	page_move_ayesn_rmap(page, vma);
	page->index = linear_page_index(vma, address);
	page_ref_unfreeze(page, 1);

	return true;
}
#ifdef CONFIG_MIGRATION
void ksm_migrate_page(struct page *newpage, struct page *oldpage)
{
	struct stable_yesde *stable_yesde;

	VM_BUG_ON_PAGE(!PageLocked(oldpage), oldpage);
	VM_BUG_ON_PAGE(!PageLocked(newpage), newpage);
	VM_BUG_ON_PAGE(newpage->mapping != oldpage->mapping, newpage);

	stable_yesde = page_stable_yesde(newpage);
	if (stable_yesde) {
		VM_BUG_ON_PAGE(stable_yesde->kpfn != page_to_pfn(oldpage), oldpage);
		stable_yesde->kpfn = page_to_pfn(newpage);
		/*
		 * newpage->mapping was set in advance; yesw we need smp_wmb()
		 * to make sure that the new stable_yesde->kpfn is visible
		 * to get_ksm_page() before it can see that oldpage->mapping
		 * has gone stale (or that PageSwapCache has been cleared).
		 */
		smp_wmb();
		set_page_stable_yesde(oldpage, NULL);
	}
}
#endif /* CONFIG_MIGRATION */

#ifdef CONFIG_MEMORY_HOTREMOVE
static void wait_while_offlining(void)
{
	while (ksm_run & KSM_RUN_OFFLINE) {
		mutex_unlock(&ksm_thread_mutex);
		wait_on_bit(&ksm_run, ilog2(KSM_RUN_OFFLINE),
			    TASK_UNINTERRUPTIBLE);
		mutex_lock(&ksm_thread_mutex);
	}
}

static bool stable_yesde_dup_remove_range(struct stable_yesde *stable_yesde,
					 unsigned long start_pfn,
					 unsigned long end_pfn)
{
	if (stable_yesde->kpfn >= start_pfn &&
	    stable_yesde->kpfn < end_pfn) {
		/*
		 * Don't get_ksm_page, page has already gone:
		 * which is why we keep kpfn instead of page*
		 */
		remove_yesde_from_stable_tree(stable_yesde);
		return true;
	}
	return false;
}

static bool stable_yesde_chain_remove_range(struct stable_yesde *stable_yesde,
					   unsigned long start_pfn,
					   unsigned long end_pfn,
					   struct rb_root *root)
{
	struct stable_yesde *dup;
	struct hlist_yesde *hlist_safe;

	if (!is_stable_yesde_chain(stable_yesde)) {
		VM_BUG_ON(is_stable_yesde_dup(stable_yesde));
		return stable_yesde_dup_remove_range(stable_yesde, start_pfn,
						    end_pfn);
	}

	hlist_for_each_entry_safe(dup, hlist_safe,
				  &stable_yesde->hlist, hlist_dup) {
		VM_BUG_ON(!is_stable_yesde_dup(dup));
		stable_yesde_dup_remove_range(dup, start_pfn, end_pfn);
	}
	if (hlist_empty(&stable_yesde->hlist)) {
		free_stable_yesde_chain(stable_yesde, root);
		return true; /* yestify caller that tree was rebalanced */
	} else
		return false;
}

static void ksm_check_stable_tree(unsigned long start_pfn,
				  unsigned long end_pfn)
{
	struct stable_yesde *stable_yesde, *next;
	struct rb_yesde *yesde;
	int nid;

	for (nid = 0; nid < ksm_nr_yesde_ids; nid++) {
		yesde = rb_first(root_stable_tree + nid);
		while (yesde) {
			stable_yesde = rb_entry(yesde, struct stable_yesde, yesde);
			if (stable_yesde_chain_remove_range(stable_yesde,
							   start_pfn, end_pfn,
							   root_stable_tree +
							   nid))
				yesde = rb_first(root_stable_tree + nid);
			else
				yesde = rb_next(yesde);
			cond_resched();
		}
	}
	list_for_each_entry_safe(stable_yesde, next, &migrate_yesdes, list) {
		if (stable_yesde->kpfn >= start_pfn &&
		    stable_yesde->kpfn < end_pfn)
			remove_yesde_from_stable_tree(stable_yesde);
		cond_resched();
	}
}

static int ksm_memory_callback(struct yestifier_block *self,
			       unsigned long action, void *arg)
{
	struct memory_yestify *mn = arg;

	switch (action) {
	case MEM_GOING_OFFLINE:
		/*
		 * Prevent ksm_do_scan(), unmerge_and_remove_all_rmap_items()
		 * and remove_all_stable_yesdes() while memory is going offline:
		 * it is unsafe for them to touch the stable tree at this time.
		 * But unmerge_ksm_pages(), rmap lookups and other entry points
		 * which do yest need the ksm_thread_mutex are all safe.
		 */
		mutex_lock(&ksm_thread_mutex);
		ksm_run |= KSM_RUN_OFFLINE;
		mutex_unlock(&ksm_thread_mutex);
		break;

	case MEM_OFFLINE:
		/*
		 * Most of the work is done by page migration; but there might
		 * be a few stable_yesdes left over, still pointing to struct
		 * pages which have been offlined: prune those from the tree,
		 * otherwise get_ksm_page() might later try to access a
		 * yesn-existent struct page.
		 */
		ksm_check_stable_tree(mn->start_pfn,
				      mn->start_pfn + mn->nr_pages);
		/* fallthrough */

	case MEM_CANCEL_OFFLINE:
		mutex_lock(&ksm_thread_mutex);
		ksm_run &= ~KSM_RUN_OFFLINE;
		mutex_unlock(&ksm_thread_mutex);

		smp_mb();	/* wake_up_bit advises this */
		wake_up_bit(&ksm_run, ilog2(KSM_RUN_OFFLINE));
		break;
	}
	return NOTIFY_OK;
}
#else
static void wait_while_offlining(void)
{
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

#ifdef CONFIG_SYSFS
/*
 * This all compiles without CONFIG_SYSFS, but is a waste of space.
 */

#define KSM_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define KSM_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t sleep_millisecs_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_thread_sleep_millisecs);
}

static ssize_t sleep_millisecs_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = kstrtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	ksm_thread_sleep_millisecs = msecs;
	wake_up_interruptible(&ksm_iter_wait);

	return count;
}
KSM_ATTR(sleep_millisecs);

static ssize_t pages_to_scan_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_thread_pages_to_scan);
}

static ssize_t pages_to_scan_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long nr_pages;

	err = kstrtoul(buf, 10, &nr_pages);
	if (err || nr_pages > UINT_MAX)
		return -EINVAL;

	ksm_thread_pages_to_scan = nr_pages;

	return count;
}
KSM_ATTR(pages_to_scan);

static ssize_t run_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%lu\n", ksm_run);
}

static ssize_t run_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int err;
	unsigned long flags;

	err = kstrtoul(buf, 10, &flags);
	if (err || flags > UINT_MAX)
		return -EINVAL;
	if (flags > KSM_RUN_UNMERGE)
		return -EINVAL;

	/*
	 * KSM_RUN_MERGE sets ksmd running, and 0 stops it running.
	 * KSM_RUN_UNMERGE stops it running and unmerges all rmap_items,
	 * breaking COW to free the pages_shared (but leaves mm_slots
	 * on the list for when ksmd may be set running again).
	 */

	mutex_lock(&ksm_thread_mutex);
	wait_while_offlining();
	if (ksm_run != flags) {
		ksm_run = flags;
		if (flags & KSM_RUN_UNMERGE) {
			set_current_oom_origin();
			err = unmerge_and_remove_all_rmap_items();
			clear_current_oom_origin();
			if (err) {
				ksm_run = KSM_RUN_STOP;
				count = err;
			}
		}
	}
	mutex_unlock(&ksm_thread_mutex);

	if (flags & KSM_RUN_MERGE)
		wake_up_interruptible(&ksm_thread_wait);

	return count;
}
KSM_ATTR(run);

#ifdef CONFIG_NUMA
static ssize_t merge_across_yesdes_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_merge_across_yesdes);
}

static ssize_t merge_across_yesdes_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	unsigned long kyesb;

	err = kstrtoul(buf, 10, &kyesb);
	if (err)
		return err;
	if (kyesb > 1)
		return -EINVAL;

	mutex_lock(&ksm_thread_mutex);
	wait_while_offlining();
	if (ksm_merge_across_yesdes != kyesb) {
		if (ksm_pages_shared || remove_all_stable_yesdes())
			err = -EBUSY;
		else if (root_stable_tree == one_stable_tree) {
			struct rb_root *buf;
			/*
			 * This is the first time that we switch away from the
			 * default of merging across yesdes: must yesw allocate
			 * a buffer to hold as many roots as may be needed.
			 * Allocate stable and unstable together:
			 * MAXSMP NODES_SHIFT 10 will use 16kB.
			 */
			buf = kcalloc(nr_yesde_ids + nr_yesde_ids, sizeof(*buf),
				      GFP_KERNEL);
			/* Let us assume that RB_ROOT is NULL is zero */
			if (!buf)
				err = -ENOMEM;
			else {
				root_stable_tree = buf;
				root_unstable_tree = buf + nr_yesde_ids;
				/* Stable tree is empty but yest the unstable */
				root_unstable_tree[0] = one_unstable_tree[0];
			}
		}
		if (!err) {
			ksm_merge_across_yesdes = kyesb;
			ksm_nr_yesde_ids = kyesb ? 1 : nr_yesde_ids;
		}
	}
	mutex_unlock(&ksm_thread_mutex);

	return err ? err : count;
}
KSM_ATTR(merge_across_yesdes);
#endif

static ssize_t use_zero_pages_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_use_zero_pages);
}
static ssize_t use_zero_pages_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int err;
	bool value;

	err = kstrtobool(buf, &value);
	if (err)
		return -EINVAL;

	ksm_use_zero_pages = value;

	return count;
}
KSM_ATTR(use_zero_pages);

static ssize_t max_page_sharing_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ksm_max_page_sharing);
}

static ssize_t max_page_sharing_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int err;
	int kyesb;

	err = kstrtoint(buf, 10, &kyesb);
	if (err)
		return err;
	/*
	 * When a KSM page is created it is shared by 2 mappings. This
	 * being a signed comparison, it implicitly verifies it's yest
	 * negative.
	 */
	if (kyesb < 2)
		return -EINVAL;

	if (READ_ONCE(ksm_max_page_sharing) == kyesb)
		return count;

	mutex_lock(&ksm_thread_mutex);
	wait_while_offlining();
	if (ksm_max_page_sharing != kyesb) {
		if (ksm_pages_shared || remove_all_stable_yesdes())
			err = -EBUSY;
		else
			ksm_max_page_sharing = kyesb;
	}
	mutex_unlock(&ksm_thread_mutex);

	return err ? err : count;
}
KSM_ATTR(max_page_sharing);

static ssize_t pages_shared_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_shared);
}
KSM_ATTR_RO(pages_shared);

static ssize_t pages_sharing_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_sharing);
}
KSM_ATTR_RO(pages_sharing);

static ssize_t pages_unshared_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_pages_unshared);
}
KSM_ATTR_RO(pages_unshared);

static ssize_t pages_volatile_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	long ksm_pages_volatile;

	ksm_pages_volatile = ksm_rmap_items - ksm_pages_shared
				- ksm_pages_sharing - ksm_pages_unshared;
	/*
	 * It was yest worth any locking to calculate that statistic,
	 * but it might therefore sometimes be negative: conceal that.
	 */
	if (ksm_pages_volatile < 0)
		ksm_pages_volatile = 0;
	return sprintf(buf, "%ld\n", ksm_pages_volatile);
}
KSM_ATTR_RO(pages_volatile);

static ssize_t stable_yesde_dups_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_stable_yesde_dups);
}
KSM_ATTR_RO(stable_yesde_dups);

static ssize_t stable_yesde_chains_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_stable_yesde_chains);
}
KSM_ATTR_RO(stable_yesde_chains);

static ssize_t
stable_yesde_chains_prune_millisecs_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", ksm_stable_yesde_chains_prune_millisecs);
}

static ssize_t
stable_yesde_chains_prune_millisecs_store(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = kstrtoul(buf, 10, &msecs);
	if (err || msecs > UINT_MAX)
		return -EINVAL;

	ksm_stable_yesde_chains_prune_millisecs = msecs;

	return count;
}
KSM_ATTR(stable_yesde_chains_prune_millisecs);

static ssize_t full_scans_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", ksm_scan.seqnr);
}
KSM_ATTR_RO(full_scans);

static struct attribute *ksm_attrs[] = {
	&sleep_millisecs_attr.attr,
	&pages_to_scan_attr.attr,
	&run_attr.attr,
	&pages_shared_attr.attr,
	&pages_sharing_attr.attr,
	&pages_unshared_attr.attr,
	&pages_volatile_attr.attr,
	&full_scans_attr.attr,
#ifdef CONFIG_NUMA
	&merge_across_yesdes_attr.attr,
#endif
	&max_page_sharing_attr.attr,
	&stable_yesde_chains_attr.attr,
	&stable_yesde_dups_attr.attr,
	&stable_yesde_chains_prune_millisecs_attr.attr,
	&use_zero_pages_attr.attr,
	NULL,
};

static const struct attribute_group ksm_attr_group = {
	.attrs = ksm_attrs,
	.name = "ksm",
};
#endif /* CONFIG_SYSFS */

static int __init ksm_init(void)
{
	struct task_struct *ksm_thread;
	int err;

	/* The correct value depends on page size and endianness */
	zero_checksum = calc_checksum(ZERO_PAGE(0));
	/* Default to false for backwards compatibility */
	ksm_use_zero_pages = false;

	err = ksm_slab_init();
	if (err)
		goto out;

	ksm_thread = kthread_run(ksm_scan_thread, NULL, "ksmd");
	if (IS_ERR(ksm_thread)) {
		pr_err("ksm: creating kthread failed\n");
		err = PTR_ERR(ksm_thread);
		goto out_free;
	}

#ifdef CONFIG_SYSFS
	err = sysfs_create_group(mm_kobj, &ksm_attr_group);
	if (err) {
		pr_err("ksm: register sysfs failed\n");
		kthread_stop(ksm_thread);
		goto out_free;
	}
#else
	ksm_run = KSM_RUN_MERGE;	/* yes way for user to start it */

#endif /* CONFIG_SYSFS */

#ifdef CONFIG_MEMORY_HOTREMOVE
	/* There is yes significance to this priority 100 */
	hotplug_memory_yestifier(ksm_memory_callback, 100);
#endif
	return 0;

out_free:
	ksm_slab_free();
out:
	return err;
}
subsys_initcall(ksm_init);
