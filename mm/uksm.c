/*
 * Ultra KSM. Copyright (C) 2011-2012 Nai Xia
 *
 * This is an improvement upon KSM. Some basic data structures and routines
 * are borrowed from ksm.c .
 *
 * Its new features:
 * 1. Full system scan:
 *      It automatically scans all user processes' anonymous VMAs. Kernel-user
 *      interaction to submit a memory area to KSM is no longer needed.
 *
 * 2. Rich area detection:
 *      It automatically detects rich areas containing abundant duplicated
 *      pages based. Rich areas are given a full scan speed. Poor areas are
 *      sampled at a reasonable speed with very low CPU consumption.
 *
 * 3. Ultra Per-page scan speed improvement:
 *      A new hash algorithm is proposed. As a result, on a machine with
 *      Core(TM)2 Quad Q9300 CPU in 32-bit mode and 800MHZ DDR2 main memory, it
 *      can scan memory areas that does not contain duplicated pages at speed of
 *      627MB/sec ~ 2445MB/sec and can merge duplicated areas at speed of
 *      477MB/sec ~ 923MB/sec.
 *
 * 4. Thrashing area avoidance:
 *      Thrashing area(an VMA that has frequent Ksm page break-out) can be
 *      filtered out. My benchmark shows it's more efficient than KSM's per-page
 *      hash value based volatile page detection.
 *
 *
 * 5. Misc changes upon KSM:
 *      * It has a fully x86-opitmized memcmp dedicated for 4-byte-aligned page
 *        comparison. It's much faster than default C version on x86.
 *      * rmap_item now has an struct *page member to loosely cache a
 *        address-->page mapping, which reduces too much time-costly
 *        follow_page().
 *      * The VMA creation/exit procedures are hooked to let the Ultra KSM know.
 *      * try_to_merge_two_pages() now can revert a pte if it fails. No break_
 *        ksm is needed for this case.
 *
 * 6. Full Zero Page consideration(contributed by Figo Zhang)
 *    Now uksmd consider full zero pages as special pages and merge them to an
 *    special unswappable uksm zero page.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/spinlock.h>
#include <linux/jhash.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/memory.h>
#include <linux/mmu_notifier.h>
#include <linux/swap.h>
#include <linux/ksm.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <crypto/hash.h>
#include <linux/random.h>
#include <linux/math64.h>
#include <linux/gcd.h>
#include <linux/freezer.h>
#include <linux/sradix-tree.h>

#include <asm/tlbflush.h>
#include "internal.h"

#ifdef CONFIG_X86
#undef memcmp

#ifdef CONFIG_X86_32
#define memcmp memcmpx86_32
/*
 * Compare 4-byte-aligned address s1 and s2, with length n
 */
int memcmpx86_32(void *s1, void *s2, size_t n)
{
	size_t num = n / 4;
	register int res;

	__asm__ __volatile__
	(
	 "testl %3,%3\n\t"
	 "repe; cmpsd\n\t"
	 "je        1f\n\t"
	 "sbbl      %0,%0\n\t"
	 "orl       $1,%0\n"
	 "1:"
	 : "=&a" (res), "+&S" (s1), "+&D" (s2), "+&c" (num)
	 : "0" (0)
	 : "cc");

	return res;
}

/*
 * Check the page is all zero ?
 */
static int is_full_zero(const void *s1, size_t len)
{
	unsigned char same;

	len /= 4;

	__asm__ __volatile__
	("repe; scasl;"
	 "sete %0"
	 : "=qm" (same), "+D" (s1), "+c" (len)
	 : "a" (0)
	 : "cc");

	return same;
}


#elif defined(CONFIG_X86_64)
#define memcmp memcmpx86_64
/*
 * Compare 8-byte-aligned address s1 and s2, with length n
 */
int memcmpx86_64(void *s1, void *s2, size_t n)
{
	size_t num = n / 8;
	register int res;

	__asm__ __volatile__
	(
	 "testq %q3,%q3\n\t"
	 "repe; cmpsq\n\t"
	 "je        1f\n\t"
	 "sbbq      %q0,%q0\n\t"
	 "orq       $1,%q0\n"
	 "1:"
	 : "=&a" (res), "+&S" (s1), "+&D" (s2), "+&c" (num)
	 : "0" (0)
	 : "cc");

	return res;
}

static int is_full_zero(const void *s1, size_t len)
{
	unsigned char same;

	len /= 8;

	__asm__ __volatile__
	("repe; scasq;"
	 "sete %0"
	 : "=qm" (same), "+D" (s1), "+c" (len)
	 : "a" (0)
	 : "cc");

	return same;
}

#endif
#else
static int is_full_zero(const void *s1, size_t len)
{
	unsigned long *src = s1;
	int i;

	len /= sizeof(*src);

	for (i = 0; i < len; i++) {
		if (src[i])
			return 0;
	}

	return 1;
}
#endif

#define U64_MAX		(~((u64)0))
#define UKSM_RUNG_ROUND_FINISHED  (1 << 0)
#define TIME_RATIO_SCALE	10000

#define SLOT_TREE_NODE_SHIFT	8
#define SLOT_TREE_NODE_STORE_SIZE	(1UL << SLOT_TREE_NODE_SHIFT)
struct slot_tree_node {
	unsigned long size;
	struct sradix_tree_node snode;
	void *stores[SLOT_TREE_NODE_STORE_SIZE];
};

static struct kmem_cache *slot_tree_node_cachep;

static struct sradix_tree_node *slot_tree_node_alloc(void)
{
	struct slot_tree_node *p;
	p = kmem_cache_zalloc(slot_tree_node_cachep, GFP_KERNEL);
	if (!p)
		return NULL;

	return &p->snode;
}

static void slot_tree_node_free(struct sradix_tree_node *node)
{
	struct slot_tree_node *p;

	p = container_of(node, struct slot_tree_node, snode);
	kmem_cache_free(slot_tree_node_cachep, p);
}

static void slot_tree_node_extend(struct sradix_tree_node *parent,
				  struct sradix_tree_node *child)
{
	struct slot_tree_node *p, *c;

	p = container_of(parent, struct slot_tree_node, snode);
	c = container_of(child, struct slot_tree_node, snode);

	p->size += c->size;
}

void slot_tree_node_assign(struct sradix_tree_node *node,
			   unsigned index, void *item)
{
	struct vma_slot *slot = item;
	struct slot_tree_node *cur;

	slot->snode = node;
	slot->sindex = index;

	while (node) {
		cur = container_of(node, struct slot_tree_node, snode);
		cur->size += slot->pages;
		node = node->parent;
	}
}

void slot_tree_node_rm(struct sradix_tree_node *node, unsigned offset)
{
	struct vma_slot *slot;
	struct slot_tree_node *cur;
	unsigned long pages;

	if (node->height == 1) {
		slot = node->stores[offset];
		pages = slot->pages;
	} else {
		cur = container_of(node->stores[offset],
				   struct slot_tree_node, snode);
		pages = cur->size;
	}

	while (node) {
		cur = container_of(node, struct slot_tree_node, snode);
		cur->size -= pages;
		node = node->parent;
	}
}

unsigned long slot_iter_index;
int slot_iter(void *item,  unsigned long height)
{
	struct slot_tree_node *node;
	struct vma_slot *slot;

	if (height == 1) {
		slot = item;
		if (slot_iter_index < slot->pages) {
			/*in this one*/
			return 1;
		} else {
			slot_iter_index -= slot->pages;
			return 0;
		}

	} else {
		node = container_of(item, struct slot_tree_node, snode);
		if (slot_iter_index < node->size) {
			/*in this one*/
			return 1;
		} else {
			slot_iter_index -= node->size;
			return 0;
		}
	}
}


static inline void slot_tree_init_root(struct sradix_tree_root *root)
{
	init_sradix_tree_root(root, SLOT_TREE_NODE_SHIFT);
	root->alloc = slot_tree_node_alloc;
	root->free = slot_tree_node_free;
	root->extend = slot_tree_node_extend;
	root->assign = slot_tree_node_assign;
	root->rm = slot_tree_node_rm;
}

void slot_tree_init(void)
{
	slot_tree_node_cachep = kmem_cache_create("slot_tree_node",
				sizeof(struct slot_tree_node), 0,
				SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
				NULL);
}


/* Each rung of this ladder is a list of VMAs having a same scan ratio */
struct scan_rung {
	//struct list_head scanned_list;
	struct sradix_tree_root vma_root;
	struct sradix_tree_root vma_root2;

	struct vma_slot *current_scan;
	unsigned long current_offset;

	/*
	 * The initial value for current_offset, it should loop over
	 * [0~ step - 1] to let all slot have its chance to be scanned.
	 */
	unsigned long offset_init;
	unsigned long step; /* dynamic step for current_offset */
	unsigned int flags;
	unsigned long pages_to_scan;
	//unsigned long fully_scanned_slots;
	/*
	 * a little bit tricky - if cpu_time_ratio > 0, then the value is the
	 * the cpu time ratio it can spend in rung_i for every scan
	 * period. if < 0, then it is the cpu time ratio relative to the
	 * max cpu percentage user specified. Both in unit of
	 * 1/TIME_RATIO_SCALE
	 */
	int cpu_ratio;

	/*
	 * How long it will take for all slots in this rung to be fully
	 * scanned? If it's zero, we don't care about the cover time:
	 * it's fully scanned.
	 */
	unsigned int cover_msecs;
	//unsigned long vma_num;
	//unsigned long pages; /* Sum of all slot's pages in rung */
};

/**
 * node of either the stable or unstale rbtree
 *
 */
struct tree_node {
	struct rb_node node; /* link in the main (un)stable rbtree */
	struct rb_root sub_root; /* rb_root for sublevel collision rbtree */
	u32 hash;
	unsigned long count; /* TODO: merged with sub_root */
	struct list_head all_list; /* all tree nodes in stable/unstable tree */
};

/**
 * struct stable_node - node of the stable rbtree
 * @node: rb node of this ksm page in the stable tree
 * @hlist: hlist head of rmap_items using this ksm page
 * @kpfn: page frame number of this ksm page
 */
struct stable_node {
	struct rb_node node; /* link in sub-rbtree */
	struct tree_node *tree_node; /* it's tree node root in stable tree, NULL if it's in hell list */
	struct hlist_head hlist;
	unsigned long kpfn;
	u32 hash_max; /* if ==0 then it's not been calculated yet */
	struct list_head all_list; /* in a list for all stable nodes */
};

/**
 * struct node_vma - group rmap_items linked in a same stable
 * node together.
 */
struct node_vma {
	union {
		struct vma_slot *slot;
		unsigned long key;  /* slot is used as key sorted on hlist */
	};
	struct hlist_node hlist;
	struct hlist_head rmap_hlist;
	struct stable_node *head;
};

/**
 * struct rmap_item - reverse mapping item for virtual addresses
 * @rmap_list: next rmap_item in mm_slot's singly-linked rmap_list
 * @anon_vma: pointer to anon_vma for this mm,address, when in stable tree
 * @mm: the memory structure this rmap_item is pointing into
 * @address: the virtual address this rmap_item tracks (+ flags in low bits)
 * @node: rb node of this rmap_item in the unstable tree
 * @head: pointer to stable_node heading this list in the stable tree
 * @hlist: link into hlist of rmap_items hanging off that stable_node
 */
struct rmap_item {
	struct vma_slot *slot;
	struct page *page;
	unsigned long address;	/* + low bits used for flags below */
	unsigned long hash_round;
	unsigned long entry_index;
	union {
		struct {/* when in unstable tree */
			struct rb_node node;
			struct tree_node *tree_node;
			u32 hash_max;
		};
		struct { /* when in stable tree */
			struct node_vma *head;
			struct hlist_node hlist;
			struct anon_vma *anon_vma;
		};
	};
} __attribute__((aligned(4)));

struct rmap_list_entry {
	union {
		struct rmap_item *item;
		unsigned long addr;
	};
	/* lowest bit is used for is_addr tag */
} __attribute__((aligned(4))); /* 4 aligned to fit in to pages*/


/* Basic data structure definition ends */


/*
 * Flags for rmap_item to judge if it's listed in the stable/unstable tree.
 * The flags use the low bits of rmap_item.address
 */
#define UNSTABLE_FLAG	0x1
#define STABLE_FLAG	0x2
#define get_rmap_addr(x)	((x)->address & PAGE_MASK)

/*
 * rmap_list_entry helpers
 */
#define IS_ADDR_FLAG	1
#define is_addr(ptr)		((unsigned long)(ptr) & IS_ADDR_FLAG)
#define set_is_addr(ptr)	((ptr) |= IS_ADDR_FLAG)
#define get_clean_addr(ptr)	(((ptr) & ~(__typeof__(ptr))IS_ADDR_FLAG))


/*
 * High speed caches for frequently allocated and freed structs
 */
static struct kmem_cache *rmap_item_cache;
static struct kmem_cache *stable_node_cache;
static struct kmem_cache *node_vma_cache;
static struct kmem_cache *vma_slot_cache;
static struct kmem_cache *tree_node_cache;
#define UKSM_KMEM_CACHE(__struct, __flags) kmem_cache_create("uksm_"#__struct,\
		sizeof(struct __struct), __alignof__(struct __struct),\
		(__flags), NULL)

/* Array of all scan_rung, uksm_scan_ladder[0] having the minimum scan ratio */
#define SCAN_LADDER_SIZE 4
static struct scan_rung uksm_scan_ladder[SCAN_LADDER_SIZE];

/* The evaluation rounds uksmd has finished */
static unsigned long long uksm_eval_round = 1;

/*
 * we add 1 to this var when we consider we should rebuild the whole
 * unstable tree.
 */
static unsigned long uksm_hash_round = 1;

/*
 * How many times the whole memory is scanned.
 */
static unsigned long long fully_scanned_round = 1;

/* The total number of virtual pages of all vma slots */
static u64 uksm_pages_total;

/* The number of pages has been scanned since the start up */
static u64 uksm_pages_scanned;

static u64 scanned_virtual_pages;

/* The number of pages has been scanned since last encode_benefit call */
static u64 uksm_pages_scanned_last;

/* If the scanned number is tooo large, we encode it here */
static u64 pages_scanned_stored;

static unsigned long pages_scanned_base;

/* The number of nodes in the stable tree */
static unsigned long uksm_pages_shared;

/* The number of page slots additionally sharing those nodes */
static unsigned long uksm_pages_sharing;

/* The number of nodes in the unstable tree */
static unsigned long uksm_pages_unshared;

/*
 * Milliseconds ksmd should sleep between scans,
 * >= 100ms to be consistent with
 * scan_time_to_sleep_msec()
 */
static unsigned int uksm_sleep_jiffies;

/* The real value for the uksmd next sleep */
static unsigned int uksm_sleep_real;

/* Saved value for user input uksm_sleep_jiffies when it's enlarged */
static unsigned int uksm_sleep_saved;

/* Max percentage of cpu utilization ksmd can take to scan in one batch */
static unsigned int uksm_max_cpu_percentage;

static int uksm_cpu_governor;

static char *uksm_cpu_governor_str[4] = { "full", "medium", "low", "quiet" };

struct uksm_cpu_preset_s {
	int cpu_ratio[SCAN_LADDER_SIZE];
	unsigned int cover_msecs[SCAN_LADDER_SIZE];
	unsigned int max_cpu; /* percentage */
};

struct uksm_cpu_preset_s uksm_cpu_preset[4] = {
	{ {20, 40, -2500, -10000}, {1000, 500, 200, 50}, 95},
	{ {20, 30, -2500, -10000}, {1000, 500, 400, 100}, 50},
	{ {10, 20, -5000, -10000}, {1500, 1000, 1000, 250}, 20},
	{ {10, 20, 40, 75}, {2000, 1000, 1000, 1000}, 1},
};

/* The default value for uksm_ema_page_time if it's not initialized */
#define UKSM_PAGE_TIME_DEFAULT	500

/*cost to scan one page by expotional moving average in nsecs */
static unsigned long uksm_ema_page_time = UKSM_PAGE_TIME_DEFAULT;

/* The expotional moving average alpha weight, in percentage. */
#define EMA_ALPHA	20

/*
 * The threshold used to filter out thrashing areas,
 * If it == 0, filtering is disabled, otherwise it's the percentage up-bound
 * of the thrashing ratio of all areas. Any area with a bigger thrashing ratio
 * will be considered as having a zero duplication ratio.
 */
static unsigned int uksm_thrash_threshold = 50;

/* How much dedup ratio is considered to be abundant*/
static unsigned int uksm_abundant_threshold = 10;

/* All slots having merged pages in this eval round. */
struct list_head vma_slot_dedup = LIST_HEAD_INIT(vma_slot_dedup);

/* How many times the ksmd has slept since startup */
static unsigned long long uksm_sleep_times;

#define UKSM_RUN_STOP	0
#define UKSM_RUN_MERGE	1
static unsigned int uksm_run = 1;

static DECLARE_WAIT_QUEUE_HEAD(uksm_thread_wait);
static DEFINE_MUTEX(uksm_thread_mutex);

/*
 * List vma_slot_new is for newly created vma_slot waiting to be added by
 * ksmd. If one cannot be added(e.g. due to it's too small), it's moved to
 * vma_slot_noadd. vma_slot_del is the list for vma_slot whose corresponding
 * VMA has been removed/freed.
 */
struct list_head vma_slot_new = LIST_HEAD_INIT(vma_slot_new);
struct list_head vma_slot_noadd = LIST_HEAD_INIT(vma_slot_noadd);
struct list_head vma_slot_del = LIST_HEAD_INIT(vma_slot_del);
static DEFINE_SPINLOCK(vma_slot_list_lock);

/* The unstable tree heads */
static struct rb_root root_unstable_tree = RB_ROOT;

/*
 * All tree_nodes are in a list to be freed at once when unstable tree is
 * freed after each scan round.
 */
static struct list_head unstable_tree_node_list =
				LIST_HEAD_INIT(unstable_tree_node_list);

/* List contains all stable nodes */
static struct list_head stable_node_list = LIST_HEAD_INIT(stable_node_list);

/*
 * When the hash strength is changed, the stable tree must be delta_hashed and
 * re-structured. We use two set of below structs to speed up the
 * re-structuring of stable tree.
 */
static struct list_head
stable_tree_node_list[2] = {LIST_HEAD_INIT(stable_tree_node_list[0]),
			    LIST_HEAD_INIT(stable_tree_node_list[1])};

static struct list_head *stable_tree_node_listp = &stable_tree_node_list[0];
static struct rb_root root_stable_tree[2] = {RB_ROOT, RB_ROOT};
static struct rb_root *root_stable_treep = &root_stable_tree[0];
static unsigned long stable_tree_index;

/* The hash strength needed to hash a full page */
#define HASH_STRENGTH_FULL		(PAGE_SIZE / sizeof(u32))

/* The hash strength needed for loop-back hashing */
#define HASH_STRENGTH_MAX		(HASH_STRENGTH_FULL + 10)

/* The random offsets in a page */
static u32 *random_nums;

/* The hash strength */
static unsigned long hash_strength = HASH_STRENGTH_FULL >> 4;

/* The delta value each time the hash strength increases or decreases */
static unsigned long hash_strength_delta;
#define HASH_STRENGTH_DELTA_MAX	5

/* The time we have saved due to random_sample_hash */
static u64 rshash_pos;

/* The time we have wasted due to hash collision */
static u64 rshash_neg;

struct uksm_benefit {
	u64 pos;
	u64 neg;
	u64 scanned;
	unsigned long base;
} benefit;

/*
 * The relative cost of memcmp, compared to 1 time unit of random sample
 * hash, this value is tested when ksm module is initialized
 */
static unsigned long memcmp_cost;

static unsigned long  rshash_neg_cont_zero;
static unsigned long  rshash_cont_obscure;

/* The possible states of hash strength adjustment heuristic */
enum rshash_states {
		RSHASH_STILL,
		RSHASH_TRYUP,
		RSHASH_TRYDOWN,
		RSHASH_NEW,
		RSHASH_PRE_STILL,
};

/* The possible direction we are about to adjust hash strength */
enum rshash_direct {
	GO_UP,
	GO_DOWN,
	OBSCURE,
	STILL,
};

/* random sampling hash state machine */
static struct {
	enum rshash_states state;
	enum rshash_direct pre_direct;
	u8 below_count;
	/* Keep a lookup window of size 5, iff above_count/below_count > 3
	 * in this window we stop trying.
	 */
	u8 lookup_window_index;
	u64 stable_benefit;
	unsigned long turn_point_down;
	unsigned long turn_benefit_down;
	unsigned long turn_point_up;
	unsigned long turn_benefit_up;
	unsigned long stable_point;
} rshash_state;

/*zero page hash table, hash_strength [0 ~ HASH_STRENGTH_MAX]*/
static u32 *zero_hash_table;

static inline struct node_vma *alloc_node_vma(void)
{
	struct node_vma *node_vma;
	node_vma = kmem_cache_zalloc(node_vma_cache, GFP_KERNEL);
	if (node_vma) {
		INIT_HLIST_HEAD(&node_vma->rmap_hlist);
		INIT_HLIST_NODE(&node_vma->hlist);
	}
	return node_vma;
}

static inline void free_node_vma(struct node_vma *node_vma)
{
	kmem_cache_free(node_vma_cache, node_vma);
}


static inline struct vma_slot *alloc_vma_slot(void)
{
	struct vma_slot *slot;

	/*
	 * In case ksm is not initialized by now.
	 * Oops, we need to consider the call site of uksm_init() in the future.
	 */
	if (!vma_slot_cache)
		return NULL;

	slot = kmem_cache_zalloc(vma_slot_cache, GFP_KERNEL);
	if (slot) {
		INIT_LIST_HEAD(&slot->slot_list);
		INIT_LIST_HEAD(&slot->dedup_list);
		slot->flags |= UKSM_SLOT_NEED_RERAND;
	}
	return slot;
}

static inline void free_vma_slot(struct vma_slot *vma_slot)
{
	kmem_cache_free(vma_slot_cache, vma_slot);
}



static inline struct rmap_item *alloc_rmap_item(void)
{
	struct rmap_item *rmap_item;

	rmap_item = kmem_cache_zalloc(rmap_item_cache, GFP_KERNEL);
	if (rmap_item) {
		/* bug on lowest bit is not clear for flag use */
		BUG_ON(is_addr(rmap_item));
	}
	return rmap_item;
}

static inline void free_rmap_item(struct rmap_item *rmap_item)
{
	rmap_item->slot = NULL;	/* debug safety */
	kmem_cache_free(rmap_item_cache, rmap_item);
}

static inline struct stable_node *alloc_stable_node(void)
{
	struct stable_node *node;
	node = kmem_cache_alloc(stable_node_cache, GFP_KERNEL | GFP_ATOMIC);
	if (!node)
		return NULL;

	INIT_HLIST_HEAD(&node->hlist);
	list_add(&node->all_list, &stable_node_list);
	return node;
}

static inline void free_stable_node(struct stable_node *stable_node)
{
	list_del(&stable_node->all_list);
	kmem_cache_free(stable_node_cache, stable_node);
}

static inline struct tree_node *alloc_tree_node(struct list_head *list)
{
	struct tree_node *node;
	node = kmem_cache_zalloc(tree_node_cache, GFP_KERNEL | GFP_ATOMIC);
	if (!node)
		return NULL;

	list_add(&node->all_list, list);
	return node;
}

static inline void free_tree_node(struct tree_node *node)
{
	list_del(&node->all_list);
	kmem_cache_free(tree_node_cache, node);
}

static void uksm_drop_anon_vma(struct rmap_item *rmap_item)
{
	struct anon_vma *anon_vma = rmap_item->anon_vma;

	put_anon_vma(anon_vma);
}


/**
 * Remove a stable node from stable_tree, may unlink from its tree_node and
 * may remove its parent tree_node if no other stable node is pending.
 *
 * @stable_node 	The node need to be removed
 * @unlink_rb 		Will this node be unlinked from the rbtree?
 * @remove_tree_	node Will its tree_node be removed if empty?
 */
static void remove_node_from_stable_tree(struct stable_node *stable_node,
					 int unlink_rb,  int remove_tree_node)
{
	struct node_vma *node_vma;
	struct rmap_item *rmap_item;
	struct hlist_node *n;

	if (!hlist_empty(&stable_node->hlist)) {
		hlist_for_each_entry_safe(node_vma, n,
					  &stable_node->hlist, hlist) {
			hlist_for_each_entry(rmap_item, &node_vma->rmap_hlist, hlist) {
				uksm_pages_sharing--;

				uksm_drop_anon_vma(rmap_item);
				rmap_item->address &= PAGE_MASK;
			}
			free_node_vma(node_vma);
			cond_resched();
		}

		/* the last one is counted as shared */
		uksm_pages_shared--;
		uksm_pages_sharing++;
	}

	if (stable_node->tree_node && unlink_rb) {
		rb_erase(&stable_node->node,
			 &stable_node->tree_node->sub_root);

		if (RB_EMPTY_ROOT(&stable_node->tree_node->sub_root) &&
		    remove_tree_node) {
			rb_erase(&stable_node->tree_node->node,
				 root_stable_treep);
			free_tree_node(stable_node->tree_node);
		} else {
			stable_node->tree_node->count--;
		}
	}

	free_stable_node(stable_node);
}


/*
 * get_uksm_page: checks if the page indicated by the stable node
 * is still its ksm page, despite having held no reference to it.
 * In which case we can trust the content of the page, and it
 * returns the gotten page; but if the page has now been zapped,
 * remove the stale node from the stable tree and return NULL.
 *
 * You would expect the stable_node to hold a reference to the ksm page.
 * But if it increments the page's count, swapping out has to wait for
 * ksmd to come around again before it can free the page, which may take
 * seconds or even minutes: much too unresponsive.  So instead we use a
 * "keyhole reference": access to the ksm page from the stable node peeps
 * out through its keyhole to see if that page still holds the right key,
 * pointing back to this stable node.  This relies on freeing a PageAnon
 * page to reset its page->mapping to NULL, and relies on no other use of
 * a page to put something that might look like our key in page->mapping.
 *
 * include/linux/pagemap.h page_cache_get_speculative() is a good reference,
 * but this is different - made simpler by uksm_thread_mutex being held, but
 * interesting for assuming that no other use of the struct page could ever
 * put our expected_mapping into page->mapping (or a field of the union which
 * coincides with page->mapping).  The RCU calls are not for KSM at all, but
 * to keep the page_count protocol described with page_cache_get_speculative.
 *
 * Note: it is possible that get_uksm_page() will return NULL one moment,
 * then page the next, if the page is in between page_freeze_refs() and
 * page_unfreeze_refs(): this shouldn't be a problem anywhere, the page
 * is on its way to being freed; but it is an anomaly to bear in mind.
 *
 * @unlink_rb: 		if the removal of this node will firstly unlink from
 * its rbtree. stable_node_reinsert will prevent this when restructuring the
 * node from its old tree.
 *
 * @remove_tree_node:	if this is the last one of its tree_node, will the
 * tree_node be freed ? If we are inserting stable node, this tree_node may
 * be reused, so don't free it.
 */
static struct page *get_uksm_page(struct stable_node *stable_node,
				 int unlink_rb, int remove_tree_node)
{
	struct page *page;
	void *expected_mapping;

	page = pfn_to_page(stable_node->kpfn);
	expected_mapping = (void *)stable_node +
				(PAGE_MAPPING_ANON | PAGE_MAPPING_KSM);
	rcu_read_lock();
	if (page->mapping != expected_mapping)
		goto stale;
	if (!get_page_unless_zero(page))
		goto stale;
	if (page->mapping != expected_mapping) {
		put_page(page);
		goto stale;
	}
	rcu_read_unlock();
	return page;
stale:
	rcu_read_unlock();
	remove_node_from_stable_tree(stable_node, unlink_rb, remove_tree_node);

	return NULL;
}

/*
 * Removing rmap_item from stable or unstable tree.
 * This function will clean the information from the stable/unstable tree.
 */
static inline void remove_rmap_item_from_tree(struct rmap_item *rmap_item)
{
	if (rmap_item->address & STABLE_FLAG) {
		struct stable_node *stable_node;
		struct node_vma *node_vma;
		struct page *page;

		node_vma = rmap_item->head;
		stable_node = node_vma->head;
		page = get_uksm_page(stable_node, 1, 1);
		if (!page)
			goto out;

		/*
		 * page lock is needed because it's racing with
		 * try_to_unmap_ksm(), etc.
		 */
		lock_page(page);
		hlist_del(&rmap_item->hlist);

		if (hlist_empty(&node_vma->rmap_hlist)) {
			hlist_del(&node_vma->hlist);
			free_node_vma(node_vma);
		}
		unlock_page(page);

		put_page(page);
		if (hlist_empty(&stable_node->hlist)) {
			/* do NOT call remove_node_from_stable_tree() here,
			 * it's possible for a forked rmap_item not in
			 * stable tree while the in-tree rmap_items were
			 * deleted.
			 */
			uksm_pages_shared--;
		} else
			uksm_pages_sharing--;


		uksm_drop_anon_vma(rmap_item);
	} else if (rmap_item->address & UNSTABLE_FLAG) {
		if (rmap_item->hash_round == uksm_hash_round) {

			rb_erase(&rmap_item->node,
				 &rmap_item->tree_node->sub_root);
			if (RB_EMPTY_ROOT(&rmap_item->tree_node->sub_root)) {
				rb_erase(&rmap_item->tree_node->node,
					 &root_unstable_tree);

				free_tree_node(rmap_item->tree_node);
			} else
				rmap_item->tree_node->count--;
		}
		uksm_pages_unshared--;
	}

	rmap_item->address &= PAGE_MASK;
	rmap_item->hash_max = 0;

out:
	cond_resched();		/* we're called from many long loops */
}

static inline int slot_in_uksm(struct vma_slot *slot)
{
	return list_empty(&slot->slot_list);
}

/*
 * Test if the mm is exiting
 */
static inline bool uksm_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0;
}

/**
 * Need to do two things:
 * 1. check if slot was moved to del list
 * 2. make sure the mmap_sem is manipulated under valid vma.
 *
 * My concern here is that in some cases, this may make
 * vma_slot_list_lock() waiters to serialized further by some
 * sem->wait_lock, can this really be expensive?
 *
 *
 * @return
 * 0: if successfully locked mmap_sem
 * -ENOENT: this slot was moved to del list
 * -EBUSY: vma lock failed
 */
static int try_down_read_slot_mmap_sem(struct vma_slot *slot)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct rw_semaphore *sem;

	spin_lock(&vma_slot_list_lock);

	/* the slot_list was removed and inited from new list, when it enters
	 * uksm_list. If now it's not empty, then it must be moved to del list
	 */
	if (!slot_in_uksm(slot)) {
		spin_unlock(&vma_slot_list_lock);
		return -ENOENT;
	}

	BUG_ON(slot->pages != vma_pages(slot->vma));
	/* Ok, vma still valid */
	vma = slot->vma;
	mm = vma->vm_mm;
	sem = &mm->mmap_sem;

	if (uksm_test_exit(mm)) {
		spin_unlock(&vma_slot_list_lock);
		return -ENOENT;
	}

	if (down_read_trylock(sem)) {
		spin_unlock(&vma_slot_list_lock);
		return 0;
	}

	spin_unlock(&vma_slot_list_lock);
	return -EBUSY;
}

static inline unsigned long
vma_page_address(struct page *page, struct vm_area_struct *vma)
{
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	unsigned long address;

	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	if (unlikely(address < vma->vm_start || address >= vma->vm_end)) {
		/* page should be within @vma mapping range */
		return -EFAULT;
	}
	return address;
}


/* return 0 on success with the item's mmap_sem locked */
static inline int get_mergeable_page_lock_mmap(struct rmap_item *item)
{
	struct mm_struct *mm;
	struct vma_slot *slot = item->slot;
	int err = -EINVAL;

	struct page *page;

	/*
	 * try_down_read_slot_mmap_sem() returns non-zero if the slot
	 * has been removed by uksm_remove_vma().
	 */
	if (try_down_read_slot_mmap_sem(slot))
		return -EBUSY;

	mm = slot->vma->vm_mm;

	if (uksm_test_exit(mm))
		goto failout_up;

	page = item->page;
	rcu_read_lock();
	if (!get_page_unless_zero(page)) {
		rcu_read_unlock();
		goto failout_up;
	}

	/* No need to consider huge page here. */
	if (item->slot->vma->anon_vma != page_anon_vma(page) ||
	    vma_page_address(page, item->slot->vma) != get_rmap_addr(item)) {
		/*
		 * TODO:
		 * should we release this item becase of its stale page
		 * mapping?
		 */
		put_page(page);
		rcu_read_unlock();
		goto failout_up;
	}
	rcu_read_unlock();
	return 0;

failout_up:
	up_read(&mm->mmap_sem);
	return err;
}

/*
 * What kind of VMA is considered ?
 */
static inline int vma_can_enter(struct vm_area_struct *vma)
{
	return uksm_flags_can_scan(vma->vm_flags);
}

/*
 * Called whenever a fresh new vma is created A new vma_slot.
 * is created and inserted into a global list Must be called.
 * after vma is inserted to its mm      		    .
 */
void uksm_vma_add_new(struct vm_area_struct *vma)
{
	struct vma_slot *slot;

	if (!vma_can_enter(vma)) {
		vma->uksm_vma_slot = NULL;
		return;
	}

	slot = alloc_vma_slot();
	if (!slot) {
		vma->uksm_vma_slot = NULL;
		return;
	}

	vma->uksm_vma_slot = slot;
	vma->vm_flags |= VM_MERGEABLE;
	slot->vma = vma;
	slot->mm = vma->vm_mm;
	slot->ctime_j = jiffies;
	slot->pages = vma_pages(vma);
	spin_lock(&vma_slot_list_lock);
	list_add_tail(&slot->slot_list, &vma_slot_new);
	spin_unlock(&vma_slot_list_lock);
}

/*
 * Called after vma is unlinked from its mm
 */
void uksm_remove_vma(struct vm_area_struct *vma)
{
	struct vma_slot *slot;

	if (!vma->uksm_vma_slot)
		return;

	slot = vma->uksm_vma_slot;
	spin_lock(&vma_slot_list_lock);
	if (slot_in_uksm(slot)) {
		/**
		 * This slot has been added by ksmd, so move to the del list
		 * waiting ksmd to free it.
		 */
		list_add_tail(&slot->slot_list, &vma_slot_del);
	} else {
		/**
		 * It's still on new list. It's ok to free slot directly.
		 */
		list_del(&slot->slot_list);
		free_vma_slot(slot);
	}
	spin_unlock(&vma_slot_list_lock);
	vma->uksm_vma_slot = NULL;
}

/*   32/3 < they < 32/2 */
#define shiftl	8
#define shiftr	12

#define HASH_FROM_TO(from, to) 				\
for (index = from; index < to; index++) {		\
	pos = random_nums[index];			\
	hash += key[pos];				\
	hash += (hash << shiftl);			\
	hash ^= (hash >> shiftr);			\
}


#define HASH_FROM_DOWN_TO(from, to) 			\
for (index = from - 1; index >= to; index--) {		\
	hash ^= (hash >> shiftr);			\
	hash ^= (hash >> (shiftr*2));			\
	hash -= (hash << shiftl);			\
	hash += (hash << (shiftl*2));			\
	pos = random_nums[index];			\
	hash -= key[pos];				\
}

/*
 * The main random sample hash function.
 */
static u32 random_sample_hash(void *addr, u32 hash_strength)
{
	u32 hash = 0xdeadbeef;
	int index, pos, loop = hash_strength;
	u32 *key = (u32 *)addr;

	if (loop > HASH_STRENGTH_FULL)
		loop = HASH_STRENGTH_FULL;

	HASH_FROM_TO(0, loop);

	if (hash_strength > HASH_STRENGTH_FULL) {
		loop = hash_strength - HASH_STRENGTH_FULL;
		HASH_FROM_TO(0, loop);
	}

	return hash;
}


/**
 * It's used when hash strength is adjusted
 *
 * @addr The page's virtual address
 * @from The original hash strength
 * @to   The hash strength changed to
 * @hash The hash value generated with "from" hash value
 *
 * return the hash value
 */
static u32 delta_hash(void *addr, int from, int to, u32 hash)
{
	u32 *key = (u32 *)addr;
	int index, pos; /* make sure they are int type */

	if (to > from) {
		if (from >= HASH_STRENGTH_FULL) {
			from -= HASH_STRENGTH_FULL;
			to -= HASH_STRENGTH_FULL;
			HASH_FROM_TO(from, to);
		} else if (to <= HASH_STRENGTH_FULL) {
			HASH_FROM_TO(from, to);
		} else {
			HASH_FROM_TO(from, HASH_STRENGTH_FULL);
			HASH_FROM_TO(0, to - HASH_STRENGTH_FULL);
		}
	} else {
		if (from <= HASH_STRENGTH_FULL) {
			HASH_FROM_DOWN_TO(from, to);
		} else if (to >= HASH_STRENGTH_FULL) {
			from -= HASH_STRENGTH_FULL;
			to -= HASH_STRENGTH_FULL;
			HASH_FROM_DOWN_TO(from, to);
		} else {
			HASH_FROM_DOWN_TO(from - HASH_STRENGTH_FULL, 0);
			HASH_FROM_DOWN_TO(HASH_STRENGTH_FULL, to);
		}
	}

	return hash;
}




#define CAN_OVERFLOW_U64(x, delta) (U64_MAX - (x) < (delta))

/**
 *
 * Called when: rshash_pos or rshash_neg is about to overflow or a scan round
 * has finished.
 *
 * return 0 if no page has been scanned since last call, 1 otherwise.
 */
static inline int encode_benefit(void)
{
	u64 scanned_delta, pos_delta, neg_delta;
	unsigned long base = benefit.base;

	scanned_delta = uksm_pages_scanned - uksm_pages_scanned_last;

	if (!scanned_delta)
		return 0;

	scanned_delta >>= base;
	pos_delta = rshash_pos >> base;
	neg_delta = rshash_neg >> base;

	if (CAN_OVERFLOW_U64(benefit.pos, pos_delta) ||
	    CAN_OVERFLOW_U64(benefit.neg, neg_delta) ||
	    CAN_OVERFLOW_U64(benefit.scanned, scanned_delta)) {
		benefit.scanned >>= 1;
		benefit.neg >>= 1;
		benefit.pos >>= 1;
		benefit.base++;
		scanned_delta >>= 1;
		pos_delta >>= 1;
		neg_delta >>= 1;
	}

	benefit.pos += pos_delta;
	benefit.neg += neg_delta;
	benefit.scanned += scanned_delta;

	BUG_ON(!benefit.scanned);

	rshash_pos = rshash_neg = 0;
	uksm_pages_scanned_last = uksm_pages_scanned;

	return 1;
}

static inline void reset_benefit(void)
{
	benefit.pos = 0;
	benefit.neg = 0;
	benefit.base = 0;
	benefit.scanned = 0;
}

static inline void inc_rshash_pos(unsigned long delta)
{
	if (CAN_OVERFLOW_U64(rshash_pos, delta))
		encode_benefit();

	rshash_pos += delta;
}

static inline void inc_rshash_neg(unsigned long delta)
{
	if (CAN_OVERFLOW_U64(rshash_neg, delta))
		encode_benefit();

	rshash_neg += delta;
}


static inline u32 page_hash(struct page *page, unsigned long hash_strength,
			    int cost_accounting)
{
	u32 val;
	unsigned long delta;

	void *addr = kmap_atomic(page);

	val = random_sample_hash(addr, hash_strength);
	kunmap_atomic(addr);

	if (cost_accounting) {
		if (HASH_STRENGTH_FULL > hash_strength)
			delta = HASH_STRENGTH_FULL - hash_strength;
		else
			delta = 0;

		inc_rshash_pos(delta);
	}

	return val;
}

static int memcmp_pages(struct page *page1, struct page *page2,
			int cost_accounting)
{
	char *addr1, *addr2;
	int ret;

	addr1 = kmap_atomic(page1);
	addr2 = kmap_atomic(page2);
	ret = memcmp(addr1, addr2, PAGE_SIZE);
	kunmap_atomic(addr2);
	kunmap_atomic(addr1);

	if (cost_accounting)
		inc_rshash_neg(memcmp_cost);

	return ret;
}

static inline int pages_identical(struct page *page1, struct page *page2)
{
	return !memcmp_pages(page1, page2, 0);
}

static inline int is_page_full_zero(struct page *page)
{
	char *addr;
	int ret;

	addr = kmap_atomic(page);
	ret = is_full_zero(addr, PAGE_SIZE);
	kunmap_atomic(addr);

	return ret;
}

static int write_protect_page(struct vm_area_struct *vma, struct page *page,
			      pte_t *orig_pte, pte_t *old_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr;
	pte_t *ptep;
	spinlock_t *ptl;
	int swapped;
	int err = -EFAULT;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	addr = page_address_in_vma(page, vma);
	if (addr == -EFAULT)
		goto out;

	BUG_ON(PageTransCompound(page));

	mmun_start = addr;
	mmun_end   = addr + PAGE_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	ptep = page_check_address(page, mm, addr, &ptl, 0);
	if (!ptep)
		goto out_mn;

	if (old_pte)
		*old_pte = *ptep;

	if (pte_write(*ptep) || pte_dirty(*ptep)) {
		pte_t entry;

		swapped = PageSwapCache(page);
		flush_cache_page(vma, addr, page_to_pfn(page));
		/*
		 * Ok this is tricky, when get_user_pages_fast() run it doesnt
		 * take any lock, therefore the check that we are going to make
		 * with the pagecount against the mapcount is racey and
		 * O_DIRECT can happen right after the check.
		 * So we clear the pte and flush the tlb before the check
		 * this assure us that no O_DIRECT can happen after the check
		 * or in the middle of the check.
		 */
		entry = ptep_clear_flush(vma, addr, ptep);
		/*
		 * Check that no O_DIRECT or similar I/O is in progress on the
		 * page
		 */
		if (page_mapcount(page) + 1 + swapped != page_count(page)) {
			set_pte_at(mm, addr, ptep, entry);
			goto out_unlock;
		}
		if (pte_dirty(entry))
			set_page_dirty(page);
		entry = pte_mkclean(pte_wrprotect(entry));
		set_pte_at_notify(mm, addr, ptep, entry);
	}
	*orig_pte = *ptep;
	err = 0;

out_unlock:
	pte_unmap_unlock(ptep, ptl);
out_mn:
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
out:
	return err;
}

#define MERGE_ERR_PGERR		1 /* the page is invalid cannot continue */
#define MERGE_ERR_COLLI		2 /* there is a collision */
#define MERGE_ERR_COLLI_MAX	3 /* collision at the max hash strength */
#define MERGE_ERR_CHANGED	4 /* the page has changed since last hash */


/**
 * replace_page - replace page in vma by new ksm page
 * @vma:      vma that holds the pte pointing to page
 * @page:     the page we are replacing by kpage
 * @kpage:    the ksm page we replace page by
 * @orig_pte: the original value of the pte
 *
 * Returns 0 on success, MERGE_ERR_PGERR on failure.
 */
static int replace_page(struct vm_area_struct *vma, struct page *page,
			struct page *kpage, pte_t orig_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;
	pte_t entry;

	unsigned long addr;
	int err = MERGE_ERR_PGERR;
	unsigned long mmun_start;	/* For mmu_notifiers */
	unsigned long mmun_end;		/* For mmu_notifiers */

	addr = page_address_in_vma(page, vma);
	if (addr == -EFAULT)
		goto out;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, addr);
	BUG_ON(pmd_trans_huge(*pmd));
	if (!pmd_present(*pmd))
		goto out;

	mmun_start = addr;
	mmun_end   = addr + PAGE_SIZE;
	mmu_notifier_invalidate_range_start(mm, mmun_start, mmun_end);

	ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte_same(*ptep, orig_pte)) {
		pte_unmap_unlock(ptep, ptl);
		goto out_mn;
	}

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	entry = mk_pte(kpage, vma->vm_page_prot);

	/* special treatment is needed for zero_page */
	if ((page_to_pfn(kpage) == uksm_zero_pfn) ||
				(page_to_pfn(kpage) == zero_pfn))
		entry = pte_mkspecial(entry);
	else {
		get_page(kpage);
		page_add_anon_rmap(kpage, vma, addr);
	}

	set_pte_at_notify(mm, addr, ptep, entry);

	page_remove_rmap(page);
	if (!page_mapped(page))
		try_to_free_swap(page);
	put_page(page);

	pte_unmap_unlock(ptep, ptl);
	err = 0;
out_mn:
	mmu_notifier_invalidate_range_end(mm, mmun_start, mmun_end);
out:
	return err;
}


/**
 *  Fully hash a page with HASH_STRENGTH_MAX return a non-zero hash value. The
 *  zero hash value at HASH_STRENGTH_MAX is used to indicated that its
 *  hash_max member has not been calculated.
 *
 * @page The page needs to be hashed
 * @hash_old The hash value calculated with current hash strength
 *
 * return the new hash value calculated at HASH_STRENGTH_MAX
 */
static inline u32 page_hash_max(struct page *page, u32 hash_old)
{
	u32 hash_max = 0;
	void *addr;

	addr = kmap_atomic(page);
	hash_max = delta_hash(addr, hash_strength,
			      HASH_STRENGTH_MAX, hash_old);

	kunmap_atomic(addr);

	if (!hash_max)
		hash_max = 1;

	inc_rshash_neg(HASH_STRENGTH_MAX - hash_strength);
	return hash_max;
}

/*
 * We compare the hash again, to ensure that it is really a hash collision
 * instead of being caused by page write.
 */
static inline int check_collision(struct rmap_item *rmap_item,
				  u32 hash)
{
	int err;
	struct page *page = rmap_item->page;

	/* if this rmap_item has already been hash_maxed, then the collision
	 * must appears in the second-level rbtree search. In this case we check
	 * if its hash_max value has been changed. Otherwise, the collision
	 * happens in the first-level rbtree search, so we check against it's
	 * current hash value.
	 */
	if (rmap_item->hash_max) {
		inc_rshash_neg(memcmp_cost);
		inc_rshash_neg(HASH_STRENGTH_MAX - hash_strength);

		if (rmap_item->hash_max == page_hash_max(page, hash))
			err = MERGE_ERR_COLLI;
		else
			err = MERGE_ERR_CHANGED;
	} else {
		inc_rshash_neg(memcmp_cost + hash_strength);

		if (page_hash(page, hash_strength, 0) == hash)
			err = MERGE_ERR_COLLI;
		else
			err = MERGE_ERR_CHANGED;
	}

	return err;
}

static struct page *page_trans_compound_anon(struct page *page)
{
	if (PageTransCompound(page)) {
		struct page *head = compound_trans_head(page);
		/*
		 * head may actually be splitted and freed from under
		 * us but it's ok here.
		 */
		if (PageAnon(head))
			return head;
	}
	return NULL;
}

static int page_trans_compound_anon_split(struct page *page)
{
	int ret = 0;
	struct page *transhuge_head = page_trans_compound_anon(page);
	if (transhuge_head) {
		/* Get the reference on the head to split it. */
		if (get_page_unless_zero(transhuge_head)) {
			/*
			 * Recheck we got the reference while the head
			 * was still anonymous.
			 */
			if (PageAnon(transhuge_head))
				ret = split_huge_page(transhuge_head);
			else
				/*
				 * Retry later if split_huge_page run
				 * from under us.
				 */
				ret = 1;
			put_page(transhuge_head);
		} else
			/* Retry later if split_huge_page run from under us. */
			ret = 1;
	}
	return ret;
}

/**
 * Try to merge a rmap_item.page with a kpage in stable node. kpage must
 * already be a ksm page.
 *
 * @return 0 if the pages were merged, -EFAULT otherwise.
 */
static int try_to_merge_with_uksm_page(struct rmap_item *rmap_item,
				      struct page *kpage, u32 hash)
{
	struct vm_area_struct *vma = rmap_item->slot->vma;
	struct mm_struct *mm = vma->vm_mm;
	pte_t orig_pte = __pte(0);
	int err = MERGE_ERR_PGERR;
	struct page *page;

	if (uksm_test_exit(mm))
		goto out;

	page = rmap_item->page;

	if (page == kpage) { /* ksm page forked */
		err = 0;
		goto out;
	}

	if (PageTransCompound(page) && page_trans_compound_anon_split(page))
		goto out;
	BUG_ON(PageTransCompound(page));

	if (!PageAnon(page) || !PageKsm(kpage))
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
	/*
	 * If this anonymous page is mapped only here, its pte may need
	 * to be write-protected.  If it's mapped elsewhere, all of its
	 * ptes are necessarily already write-protected.  But in either
	 * case, we need to lock and check page_count is not raised.
	 */
	if (write_protect_page(vma, page, &orig_pte, NULL) == 0) {
		if (pages_identical(page, kpage))
			err = replace_page(vma, page, kpage, orig_pte);
		else
			err = check_collision(rmap_item, hash);
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

	unlock_page(page);
out:
	return err;
}



/**
 * If two pages fail to merge in try_to_merge_two_pages, then we have a chance
 * to restore a page mapping that has been changed in try_to_merge_two_pages.
 *
 * @return 0 on success.
 */
static int restore_uksm_page_pte(struct vm_area_struct *vma, unsigned long addr,
			     pte_t orig_pte, pte_t wprt_pte)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;

	int err = -EFAULT;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		goto out;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		goto out;

	ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte_same(*ptep, wprt_pte)) {
		/* already copied, let it be */
		pte_unmap_unlock(ptep, ptl);
		goto out;
	}

	/*
	 * Good boy, still here. When we still get the ksm page, it does not
	 * return to the free page pool, there is no way that a pte was changed
	 * to other page and gets back to this page. And remind that ksm page
	 * do not reuse in do_wp_page(). So it's safe to restore the original
	 * pte.
	 */
	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_notify(mm, addr, ptep, orig_pte);

	pte_unmap_unlock(ptep, ptl);
	err = 0;
out:
	return err;
}

/**
 * try_to_merge_two_pages() - take two identical pages and prepare
 * them to be merged into one page(rmap_item->page)
 *
 * @return 0 if we successfully merged two identical pages into
 *         one ksm page. MERGE_ERR_COLLI if it's only a hash collision
 *         search in rbtree. MERGE_ERR_CHANGED if rmap_item has been
 *         changed since it's hashed. MERGE_ERR_PGERR otherwise.
 *
 */
static int try_to_merge_two_pages(struct rmap_item *rmap_item,
				  struct rmap_item *tree_rmap_item,
				  u32 hash)
{
	pte_t orig_pte1 = __pte(0), orig_pte2 = __pte(0);
	pte_t wprt_pte1 = __pte(0), wprt_pte2 = __pte(0);
	struct vm_area_struct *vma1 = rmap_item->slot->vma;
	struct vm_area_struct *vma2 = tree_rmap_item->slot->vma;
	struct page *page = rmap_item->page;
	struct page *tree_page = tree_rmap_item->page;
	int err = MERGE_ERR_PGERR;
	struct address_space *saved_mapping;


	if (rmap_item->page == tree_rmap_item->page)
		goto out;

	if (PageTransCompound(page) && page_trans_compound_anon_split(page))
		goto out;
	BUG_ON(PageTransCompound(page));

	if (PageTransCompound(tree_page) && page_trans_compound_anon_split(tree_page))
		goto out;
	BUG_ON(PageTransCompound(tree_page));

	if (!PageAnon(page) || !PageAnon(tree_page))
		goto out;

	if (!trylock_page(page))
		goto out;


	if (write_protect_page(vma1, page, &wprt_pte1, &orig_pte1) != 0) {
		unlock_page(page);
		goto out;
	}

	/*
	 * While we hold page lock, upgrade page from
	 * PageAnon+anon_vma to PageKsm+NULL stable_node:
	 * stable_tree_insert() will update stable_node.
	 */
	saved_mapping = page->mapping;
	set_page_stable_node(page, NULL);
	mark_page_accessed(page);
	unlock_page(page);

	if (!trylock_page(tree_page))
		goto restore_out;

	if (write_protect_page(vma2, tree_page, &wprt_pte2, &orig_pte2) != 0) {
		unlock_page(tree_page);
		goto restore_out;
	}

	if (pages_identical(page, tree_page)) {
		err = replace_page(vma2, tree_page, page, wprt_pte2);
		if (err) {
			unlock_page(tree_page);
			goto restore_out;
		}

		if ((vma2->vm_flags & VM_LOCKED)) {
			munlock_vma_page(tree_page);
			if (!PageMlocked(page)) {
				unlock_page(tree_page);
				lock_page(page);
				mlock_vma_page(page);
				tree_page = page; /* for final unlock */
			}
		}

		unlock_page(tree_page);

		goto out; /* success */

	} else {
		if (tree_rmap_item->hash_max &&
		    tree_rmap_item->hash_max == rmap_item->hash_max) {
			err = MERGE_ERR_COLLI_MAX;
		} else if (page_hash(page, hash_strength, 0) ==
		    page_hash(tree_page, hash_strength, 0)) {
			inc_rshash_neg(memcmp_cost + hash_strength * 2);
			err = MERGE_ERR_COLLI;
		} else {
			err = MERGE_ERR_CHANGED;
		}

		unlock_page(tree_page);
	}

restore_out:
	lock_page(page);
	if (!restore_uksm_page_pte(vma1, get_rmap_addr(rmap_item),
				  orig_pte1, wprt_pte1))
		page->mapping = saved_mapping;

	unlock_page(page);
out:
	return err;
}

static inline int hash_cmp(u32 new_val, u32 node_val)
{
	if (new_val > node_val)
		return 1;
	else if (new_val < node_val)
		return -1;
	else
		return 0;
}

static inline u32 rmap_item_hash_max(struct rmap_item *item, u32 hash)
{
	u32 hash_max = item->hash_max;

	if (!hash_max) {
		hash_max = page_hash_max(item->page, hash);

		item->hash_max = hash_max;
	}

	return hash_max;
}



/**
 * stable_tree_search() - search the stable tree for a page
 *
 * @item: 	the rmap_item we are comparing with
 * @hash: 	the hash value of this item->page already calculated
 *
 * @return 	the page we have found, NULL otherwise. The page returned has
 *         	been gotten.
 */
static struct page *stable_tree_search(struct rmap_item *item, u32 hash)
{
	struct rb_node *node = root_stable_treep->rb_node;
	struct tree_node *tree_node;
	unsigned long hash_max;
	struct page *page = item->page;
	struct stable_node *stable_node;

	stable_node = page_stable_node(page);
	if (stable_node) {
		/* ksm page forked, that is
		 * if (PageKsm(page) && !in_stable_tree(rmap_item))
		 * it's actually gotten once outside.
		 */
		get_page(page);
		return page;
	}

	while (node) {
		int cmp;

		tree_node = rb_entry(node, struct tree_node, node);

		cmp = hash_cmp(hash, tree_node->hash);

		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			break;
	}

	if (!node)
		return NULL;

	if (tree_node->count == 1) {
		stable_node = rb_entry(tree_node->sub_root.rb_node,
				       struct stable_node, node);
		BUG_ON(!stable_node);

		goto get_page_out;
	}

	/*
	 * ok, we have to search the second
	 * level subtree, hash the page to a
	 * full strength.
	 */
	node = tree_node->sub_root.rb_node;
	BUG_ON(!node);
	hash_max = rmap_item_hash_max(item, hash);

	while (node) {
		int cmp;

		stable_node = rb_entry(node, struct stable_node, node);

		cmp = hash_cmp(hash_max, stable_node->hash_max);

		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			goto get_page_out;
	}

	return NULL;

get_page_out:
	page = get_uksm_page(stable_node, 1, 1);
	return page;
}

static int try_merge_rmap_item(struct rmap_item *item,
			       struct page *kpage,
			       struct page *tree_page)
{
	spinlock_t *ptl;
	pte_t *ptep;
	unsigned long addr;
	struct vm_area_struct *vma = item->slot->vma;

	addr = get_rmap_addr(item);
	ptep = page_check_address(kpage, vma->vm_mm, addr, &ptl, 0);
	if (!ptep)
		return 0;

	if (pte_write(*ptep)) {
		/* has changed, abort! */
		pte_unmap_unlock(ptep, ptl);
		return 0;
	}

	get_page(tree_page);
	page_add_anon_rmap(tree_page, vma, addr);

	flush_cache_page(vma, addr, pte_pfn(*ptep));
	ptep_clear_flush(vma, addr, ptep);
	set_pte_at_notify(vma->vm_mm, addr, ptep,
			  mk_pte(tree_page, vma->vm_page_prot));

	page_remove_rmap(kpage);
	put_page(kpage);

	pte_unmap_unlock(ptep, ptl);

	return 1;
}

/**
 * try_to_merge_with_stable_page() - when two rmap_items need to be inserted
 * into stable tree, the page was found to be identical to a stable ksm page,
 * this is the last chance we can merge them into one.
 *
 * @item1:	the rmap_item holding the page which we wanted to insert
 *       	into stable tree.
 * @item2:	the other rmap_item we found when unstable tree search
 * @oldpage:	the page currently mapped by the two rmap_items
 * @tree_page: 	the page we found identical in stable tree node
 * @success1:	return if item1 is successfully merged
 * @success2:	return if item2 is successfully merged
 */
static void try_merge_with_stable(struct rmap_item *item1,
				  struct rmap_item *item2,
				  struct page **kpage,
				  struct page *tree_page,
				  int *success1, int *success2)
{
	struct vm_area_struct *vma1 = item1->slot->vma;
	struct vm_area_struct *vma2 = item2->slot->vma;
	*success1 = 0;
	*success2 = 0;

	if (unlikely(*kpage == tree_page)) {
		/* I don't think this can really happen */
		printk(KERN_WARNING "UKSM: unexpected condition detected in "
			"try_merge_with_stable() -- *kpage == tree_page !\n");
		*success1 = 1;
		*success2 = 1;
		return;
	}

	if (!PageAnon(*kpage) || !PageKsm(*kpage))
		goto failed;

	if (!trylock_page(tree_page))
		goto failed;

	/* If the oldpage is still ksm and still pointed
	 * to in the right place, and still write protected,
	 * we are confident it's not changed, no need to
	 * memcmp anymore.
	 * be ware, we cannot take nested pte locks,
	 * deadlock risk.
	 */
	if (!try_merge_rmap_item(item1, *kpage, tree_page))
		goto unlock_failed;

	/* ok, then vma2, remind that pte1 already set */
	if (!try_merge_rmap_item(item2, *kpage, tree_page))
		goto success_1;

	*success2 = 1;
success_1:
	*success1 = 1;


	if ((*success1 && vma1->vm_flags & VM_LOCKED) ||
	    (*success2 && vma2->vm_flags & VM_LOCKED)) {
		munlock_vma_page(*kpage);
		if (!PageMlocked(tree_page))
			mlock_vma_page(tree_page);
	}

	/*
	 * We do not need oldpage any more in the caller, so can break the lock
	 * now.
	 */
	unlock_page(*kpage);
	*kpage = tree_page; /* Get unlocked outside. */
	return;

unlock_failed:
	unlock_page(tree_page);
failed:
	return;
}

static inline void stable_node_hash_max(struct stable_node *node,
					 struct page *page, u32 hash)
{
	u32 hash_max = node->hash_max;

	if (!hash_max) {
		hash_max = page_hash_max(page, hash);
		node->hash_max = hash_max;
	}
}

static inline
struct stable_node *new_stable_node(struct tree_node *tree_node,
				    struct page *kpage, u32 hash_max)
{
	struct stable_node *new_stable_node;

	new_stable_node = alloc_stable_node();
	if (!new_stable_node)
		return NULL;

	new_stable_node->kpfn = page_to_pfn(kpage);
	new_stable_node->hash_max = hash_max;
	new_stable_node->tree_node = tree_node;
	set_page_stable_node(kpage, new_stable_node);

	return new_stable_node;
}

static inline
struct stable_node *first_level_insert(struct tree_node *tree_node,
				       struct rmap_item *rmap_item,
				       struct rmap_item *tree_rmap_item,
				       struct page **kpage, u32 hash,
				       int *success1, int *success2)
{
	int cmp;
	struct page *tree_page;
	u32 hash_max = 0;
	struct stable_node *stable_node, *new_snode;
	struct rb_node *parent = NULL, **new;

	/* this tree node contains no sub-tree yet */
	stable_node = rb_entry(tree_node->sub_root.rb_node,
			       struct stable_node, node);

	tree_page = get_uksm_page(stable_node, 1, 0);
	if (tree_page) {
		cmp = memcmp_pages(*kpage, tree_page, 1);
		if (!cmp) {
			try_merge_with_stable(rmap_item, tree_rmap_item, kpage,
					      tree_page, success1, success2);
			put_page(tree_page);
			if (!*success1 && !*success2)
				goto failed;

			return stable_node;

		} else {
			/*
			 * collision in first level try to create a subtree.
			 * A new node need to be created.
			 */
			put_page(tree_page);

			stable_node_hash_max(stable_node, tree_page,
					     tree_node->hash);
			hash_max = rmap_item_hash_max(rmap_item, hash);
			cmp = hash_cmp(hash_max, stable_node->hash_max);

			parent = &stable_node->node;
			if (cmp < 0) {
				new = &parent->rb_left;
			} else if (cmp > 0) {
				new = &parent->rb_right;
			} else {
				goto failed;
			}
		}

	} else {
		/* the only stable_node deleted, we reuse its tree_node.
		 */
		parent = NULL;
		new = &tree_node->sub_root.rb_node;
	}

	new_snode = new_stable_node(tree_node, *kpage, hash_max);
	if (!new_snode)
		goto failed;

	rb_link_node(&new_snode->node, parent, new);
	rb_insert_color(&new_snode->node, &tree_node->sub_root);
	tree_node->count++;
	*success1 = *success2 = 1;

	return new_snode;

failed:
	return NULL;
}

static inline
struct stable_node *stable_subtree_insert(struct tree_node *tree_node,
					  struct rmap_item *rmap_item,
					  struct rmap_item *tree_rmap_item,
					  struct page **kpage, u32 hash,
					  int *success1, int *success2)
{
	struct page *tree_page;
	u32 hash_max;
	struct stable_node *stable_node, *new_snode;
	struct rb_node *parent, **new;

research:
	parent = NULL;
	new = &tree_node->sub_root.rb_node;
	BUG_ON(!*new);
	hash_max = rmap_item_hash_max(rmap_item, hash);
	while (*new) {
		int cmp;

		stable_node = rb_entry(*new, struct stable_node, node);

		cmp = hash_cmp(hash_max, stable_node->hash_max);

		if (cmp < 0) {
			parent = *new;
			new = &parent->rb_left;
		} else if (cmp > 0) {
			parent = *new;
			new = &parent->rb_right;
		} else {
			tree_page = get_uksm_page(stable_node, 1, 0);
			if (tree_page) {
				cmp = memcmp_pages(*kpage, tree_page, 1);
				if (!cmp) {
					try_merge_with_stable(rmap_item,
						tree_rmap_item, kpage,
						tree_page, success1, success2);

					put_page(tree_page);
					if (!*success1 && !*success2)
						goto failed;
					/*
					 * successfully merged with a stable
					 * node
					 */
					return stable_node;
				} else {
					put_page(tree_page);
					goto failed;
				}
			} else {
				/*
				 * stable node may be deleted,
				 * and subtree maybe
				 * restructed, cannot
				 * continue, research it.
				 */
				if (tree_node->count) {
					goto research;
				} else {
					/* reuse the tree node*/
					parent = NULL;
					new = &tree_node->sub_root.rb_node;
				}
			}
		}
	}

	new_snode = new_stable_node(tree_node, *kpage, hash_max);
	if (!new_snode)
		goto failed;

	rb_link_node(&new_snode->node, parent, new);
	rb_insert_color(&new_snode->node, &tree_node->sub_root);
	tree_node->count++;
	*success1 = *success2 = 1;

	return new_snode;

failed:
	return NULL;
}


/**
 * stable_tree_insert() - try to insert a merged page in unstable tree to
 * the stable tree
 *
 * @kpage:		the page need to be inserted
 * @hash:		the current hash of this page
 * @rmap_item:		the rmap_item being scanned
 * @tree_rmap_item:	the rmap_item found on unstable tree
 * @success1:		return if rmap_item is merged
 * @success2:		return if tree_rmap_item is merged
 *
 * @return 		the stable_node on stable tree if at least one
 *      		rmap_item is inserted into stable tree, NULL
 *      		otherwise.
 */
static struct stable_node *
stable_tree_insert(struct page **kpage, u32 hash,
		   struct rmap_item *rmap_item,
		   struct rmap_item *tree_rmap_item,
		   int *success1, int *success2)
{
	struct rb_node **new = &root_stable_treep->rb_node;
	struct rb_node *parent = NULL;
	struct stable_node *stable_node;
	struct tree_node *tree_node;
	u32 hash_max = 0;

	*success1 = *success2 = 0;

	while (*new) {
		int cmp;

		tree_node = rb_entry(*new, struct tree_node, node);

		cmp = hash_cmp(hash, tree_node->hash);

		if (cmp < 0) {
			parent = *new;
			new = &parent->rb_left;
		} else if (cmp > 0) {
			parent = *new;
			new = &parent->rb_right;
		} else
			break;
	}

	if (*new) {
		if (tree_node->count == 1) {
			stable_node = first_level_insert(tree_node, rmap_item,
						tree_rmap_item, kpage,
						hash, success1, success2);
		} else {
			stable_node = stable_subtree_insert(tree_node,
					rmap_item, tree_rmap_item, kpage,
					hash, success1, success2);
		}
	} else {

		/* no tree node found */
		tree_node = alloc_tree_node(stable_tree_node_listp);
		if (!tree_node) {
			stable_node = NULL;
			goto out;
		}

		stable_node = new_stable_node(tree_node, *kpage, hash_max);
		if (!stable_node) {
			free_tree_node(tree_node);
			goto out;
		}

		tree_node->hash = hash;
		rb_link_node(&tree_node->node, parent, new);
		rb_insert_color(&tree_node->node, root_stable_treep);
		parent = NULL;
		new = &tree_node->sub_root.rb_node;

		rb_link_node(&stable_node->node, parent, new);
		rb_insert_color(&stable_node->node, &tree_node->sub_root);
		tree_node->count++;
		*success1 = *success2 = 1;
	}

out:
	return stable_node;
}


/**
 * get_tree_rmap_item_page() - try to get the page and lock the mmap_sem
 *
 * @return 	0 on success, -EBUSY if unable to lock the mmap_sem,
 *         	-EINVAL if the page mapping has been changed.
 */
static inline int get_tree_rmap_item_page(struct rmap_item *tree_rmap_item)
{
	int err;

	err = get_mergeable_page_lock_mmap(tree_rmap_item);

	if (err == -EINVAL) {
		/* its page map has been changed, remove it */
		remove_rmap_item_from_tree(tree_rmap_item);
	}

	/* The page is gotten and mmap_sem is locked now. */
	return err;
}


/**
 * unstable_tree_search_insert() - search an unstable tree rmap_item with the
 * same hash value. Get its page and trylock the mmap_sem
 */
static inline
struct rmap_item *unstable_tree_search_insert(struct rmap_item *rmap_item,
					      u32 hash)

{
	struct rb_node **new = &root_unstable_tree.rb_node;
	struct rb_node *parent = NULL;
	struct tree_node *tree_node;
	u32 hash_max;
	struct rmap_item *tree_rmap_item;

	while (*new) {
		int cmp;

		tree_node = rb_entry(*new, struct tree_node, node);

		cmp = hash_cmp(hash, tree_node->hash);

		if (cmp < 0) {
			parent = *new;
			new = &parent->rb_left;
		} else if (cmp > 0) {
			parent = *new;
			new = &parent->rb_right;
		} else
			break;
	}

	if (*new) {
		/* got the tree_node */
		if (tree_node->count == 1) {
			tree_rmap_item = rb_entry(tree_node->sub_root.rb_node,
						  struct rmap_item, node);
			BUG_ON(!tree_rmap_item);

			goto get_page_out;
		}

		/* well, search the collision subtree */
		new = &tree_node->sub_root.rb_node;
		BUG_ON(!*new);
		hash_max = rmap_item_hash_max(rmap_item, hash);

		while (*new) {
			int cmp;

			tree_rmap_item = rb_entry(*new, struct rmap_item,
						  node);

			cmp = hash_cmp(hash_max, tree_rmap_item->hash_max);
			parent = *new;
			if (cmp < 0)
				new = &parent->rb_left;
			else if (cmp > 0)
				new = &parent->rb_right;
			else
				goto get_page_out;
		}
	} else {
		/* alloc a new tree_node */
		tree_node = alloc_tree_node(&unstable_tree_node_list);
		if (!tree_node)
			return NULL;

		tree_node->hash = hash;
		rb_link_node(&tree_node->node, parent, new);
		rb_insert_color(&tree_node->node, &root_unstable_tree);
		parent = NULL;
		new = &tree_node->sub_root.rb_node;
	}

	/* did not found even in sub-tree */
	rmap_item->tree_node = tree_node;
	rmap_item->address |= UNSTABLE_FLAG;
	rmap_item->hash_round = uksm_hash_round;
	rb_link_node(&rmap_item->node, parent, new);
	rb_insert_color(&rmap_item->node, &tree_node->sub_root);

	uksm_pages_unshared++;
	return NULL;

get_page_out:
	if (tree_rmap_item->page == rmap_item->page)
		return NULL;

	if (get_tree_rmap_item_page(tree_rmap_item))
		return NULL;

	return tree_rmap_item;
}

static void hold_anon_vma(struct rmap_item *rmap_item,
			  struct anon_vma *anon_vma)
{
	rmap_item->anon_vma = anon_vma;
	get_anon_vma(anon_vma);
}


/**
 * stable_tree_append() - append a rmap_item to a stable node. Deduplication
 * ratio statistics is done in this function.
 *
 */
static void stable_tree_append(struct rmap_item *rmap_item,
			       struct stable_node *stable_node, int logdedup)
{
	struct node_vma *node_vma = NULL, *new_node_vma, *node_vma_cont = NULL;
	unsigned long key = (unsigned long)rmap_item->slot;
	unsigned long factor = rmap_item->slot->rung->step;

	BUG_ON(!stable_node);
	rmap_item->address |= STABLE_FLAG;

	if (hlist_empty(&stable_node->hlist)) {
		uksm_pages_shared++;
		goto node_vma_new;
	} else {
		uksm_pages_sharing++;
	}

	hlist_for_each_entry(node_vma, &stable_node->hlist, hlist) {
		if (node_vma->key >= key)
			break;

		if (logdedup) {
			node_vma->slot->pages_bemerged += factor;
			if (list_empty(&node_vma->slot->dedup_list))
				list_add(&node_vma->slot->dedup_list,
					 &vma_slot_dedup);
		}
	}

	if (node_vma) {
		if (node_vma->key == key) {
			node_vma_cont = hlist_entry_safe(node_vma->hlist.next, struct node_vma, hlist);
			goto node_vma_ok;
		} else if (node_vma->key > key) {
			node_vma_cont = node_vma;
		}
	}

node_vma_new:
	/* no same vma already in node, alloc a new node_vma */
	new_node_vma = alloc_node_vma();
	BUG_ON(!new_node_vma);
	new_node_vma->head = stable_node;
	new_node_vma->slot = rmap_item->slot;

	if (!node_vma) {
		hlist_add_head(&new_node_vma->hlist, &stable_node->hlist);
	} else if (node_vma->key != key) {
		if (node_vma->key < key)
			hlist_add_after(&node_vma->hlist, &new_node_vma->hlist);
		else {
			hlist_add_before(&new_node_vma->hlist,
					 &node_vma->hlist);
		}

	}
	node_vma = new_node_vma;

node_vma_ok: /* ok, ready to add to the list */
	rmap_item->head = node_vma;
	hlist_add_head(&rmap_item->hlist, &node_vma->rmap_hlist);
	hold_anon_vma(rmap_item, rmap_item->slot->vma->anon_vma);
	if (logdedup) {
		rmap_item->slot->pages_merged++;
		if (node_vma_cont) {
			node_vma = node_vma_cont;
			hlist_for_each_entry_continue(node_vma, hlist) {
				node_vma->slot->pages_bemerged += factor;
				if (list_empty(&node_vma->slot->dedup_list))
					list_add(&node_vma->slot->dedup_list,
						 &vma_slot_dedup);
			}
		}
	}
}

/*
 * We use break_ksm to break COW on a ksm page: it's a stripped down
 *
 *	if (get_user_pages(current, mm, addr, 1, 1, 1, &page, NULL) == 1)
 *		put_page(page);
 *
 * but taking great care only to touch a ksm page, in a VM_MERGEABLE vma,
 * in case the application has unmapped and remapped mm,addr meanwhile.
 * Could a ksm page appear anywhere else?  Actually yes, in a VM_PFNMAP
 * mmap of /dev/mem or /dev/kmem, where we would not want to touch it.
 */
static int break_ksm(struct vm_area_struct *vma, unsigned long addr)
{
	struct page *page;
	int ret = 0;

	do {
		cond_resched();
		page = follow_page(vma, addr, FOLL_GET);
		if (IS_ERR_OR_NULL(page))
			break;
		if (PageKsm(page)) {
			ret = handle_mm_fault(vma->vm_mm, vma, addr,
					      FAULT_FLAG_WRITE);
		} else
			ret = VM_FAULT_WRITE;
		put_page(page);
	} while (!(ret & (VM_FAULT_WRITE | VM_FAULT_SIGBUS | VM_FAULT_OOM)));
	/*
	 * We must loop because handle_mm_fault() may back out if there's
	 * any difficulty e.g. if pte accessed bit gets updated concurrently.
	 *
	 * VM_FAULT_WRITE is what we have been hoping for: it indicates that
	 * COW has been broken, even if the vma does not permit VM_WRITE;
	 * but note that a concurrent fault might break PageKsm for us.
	 *
	 * VM_FAULT_SIGBUS could occur if we race with truncation of the
	 * backing file, which also invalidates anonymous pages: that's
	 * okay, that truncation will have unmapped the PageKsm for us.
	 *
	 * VM_FAULT_OOM: at the time of writing (late July 2009), setting
	 * aside mem_cgroup limits, VM_FAULT_OOM would only be set if the
	 * current task has TIF_MEMDIE set, and will be OOM killed on return
	 * to user; and ksmd, having no mm, would never be chosen for that.
	 *
	 * But if the mm is in a limited mem_cgroup, then the fault may fail
	 * with VM_FAULT_OOM even if the current task is not TIF_MEMDIE; and
	 * even ksmd can fail in this way - though it's usually breaking ksm
	 * just to undo a merge it made a moment before, so unlikely to oom.
	 *
	 * That's a pity: we might therefore have more kernel pages allocated
	 * than we're counting as nodes in the stable tree; but uksm_do_scan
	 * will retry to break_cow on each pass, so should recover the page
	 * in due course.  The important thing is to not let VM_MERGEABLE
	 * be cleared while any such pages might remain in the area.
	 */
	return (ret & VM_FAULT_OOM) ? -ENOMEM : 0;
}

static void break_cow(struct rmap_item *rmap_item)
{
	struct vm_area_struct *vma = rmap_item->slot->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = get_rmap_addr(rmap_item);

	if (uksm_test_exit(mm))
		goto out;

	break_ksm(vma, addr);
out:
	return;
}

/*
 * Though it's very tempting to unmerge in_stable_tree(rmap_item)s rather
 * than check every pte of a given vma, the locking doesn't quite work for
 * that - an rmap_item is assigned to the stable tree after inserting ksm
 * page and upping mmap_sem.  Nor does it fit with the way we skip dup'ing
 * rmap_items from parent to child at fork time (so as not to waste time
 * if exit comes before the next scan reaches it).
 *
 * Similarly, although we'd like to remove rmap_items (so updating counts
 * and freeing memory) when unmerging an area, it's easier to leave that
 * to the next pass of ksmd - consider, for example, how ksmd might be
 * in cmp_and_merge_page on one of the rmap_items we would be removing.
 */
inline int unmerge_uksm_pages(struct vm_area_struct *vma,
		      unsigned long start, unsigned long end)
{
	unsigned long addr;
	int err = 0;

	for (addr = start; addr < end && !err; addr += PAGE_SIZE) {
		if (uksm_test_exit(vma->vm_mm))
			break;
		if (signal_pending(current))
			err = -ERESTARTSYS;
		else
			err = break_ksm(vma, addr);
	}
	return err;
}

static inline void inc_uksm_pages_scanned(void)
{
	u64 delta;


	if (uksm_pages_scanned == U64_MAX) {
		encode_benefit();

		delta = uksm_pages_scanned >> pages_scanned_base;

		if (CAN_OVERFLOW_U64(pages_scanned_stored, delta)) {
			pages_scanned_stored >>= 1;
			delta >>= 1;
			pages_scanned_base++;
		}

		pages_scanned_stored += delta;

		uksm_pages_scanned = uksm_pages_scanned_last = 0;
	}

	uksm_pages_scanned++;
}

static inline int find_zero_page_hash(int strength, u32 hash)
{
	return (zero_hash_table[strength] == hash);
}

static
int cmp_and_merge_zero_page(struct vm_area_struct *vma, struct page *page)
{
	struct page *zero_page = empty_uksm_zero_page;
	struct mm_struct *mm = vma->vm_mm;
	pte_t orig_pte = __pte(0);
	int err = -EFAULT;

	if (uksm_test_exit(mm))
		goto out;

	if (PageTransCompound(page) && page_trans_compound_anon_split(page))
		goto out;
	BUG_ON(PageTransCompound(page));

	if (!PageAnon(page))
		goto out;

	if (!trylock_page(page))
		goto out;

	if (write_protect_page(vma, page, &orig_pte, 0) == 0) {
		if (is_page_full_zero(page))
			err = replace_page(vma, page, zero_page, orig_pte);
	}

	unlock_page(page);
out:
	return err;
}

/*
 * cmp_and_merge_page() - first see if page can be merged into the stable
 * tree; if not, compare hash to previous and if it's the same, see if page
 * can be inserted into the unstable tree, or merged with a page already there
 * and both transferred to the stable tree.
 *
 * @page: the page that we are searching identical page to.
 * @rmap_item: the reverse mapping into the virtual address of this page
 */
static void cmp_and_merge_page(struct rmap_item *rmap_item, u32 hash)
{
	struct rmap_item *tree_rmap_item;
	struct page *page;
	struct page *kpage = NULL;
	u32 hash_max;
	int err;
	unsigned int success1, success2;
	struct stable_node *snode;
	int cmp;
	struct rb_node *parent = NULL, **new;

	remove_rmap_item_from_tree(rmap_item);
	page = rmap_item->page;

	/* We first start with searching the page inside the stable tree */
	kpage = stable_tree_search(rmap_item, hash);
	if (kpage) {
		err = try_to_merge_with_uksm_page(rmap_item, kpage,
						 hash);
		if (!err) {
			/*
			 * The page was successfully merged, add
			 * its rmap_item to the stable tree.
			 * page lock is needed because it's
			 * racing with try_to_unmap_ksm(), etc.
			 */
			lock_page(kpage);
			snode = page_stable_node(kpage);
			stable_tree_append(rmap_item, snode, 1);
			unlock_page(kpage);
			put_page(kpage);
			return; /* success */
		}
		put_page(kpage);

		/*
		 * if it's a collision and it has been search in sub-rbtree
		 * (hash_max != 0), we want to abort, because if it is
		 * successfully merged in unstable tree, the collision trends to
		 * happen again.
		 */
		if (err == MERGE_ERR_COLLI && rmap_item->hash_max)
			return;
	}

	tree_rmap_item =
		unstable_tree_search_insert(rmap_item, hash);
	if (tree_rmap_item) {
		err = try_to_merge_two_pages(rmap_item, tree_rmap_item, hash);
		/*
		 * As soon as we merge this page, we want to remove the
		 * rmap_item of the page we have merged with from the unstable
		 * tree, and insert it instead as new node in the stable tree.
		 */
		if (!err) {
			kpage = page;
			remove_rmap_item_from_tree(tree_rmap_item);
			lock_page(kpage);
			snode = stable_tree_insert(&kpage, hash,
						   rmap_item, tree_rmap_item,
						   &success1, &success2);

			/*
			 * Do not log dedup for tree item, it's not counted as
			 * scanned in this round.
			 */
			if (success2)
				stable_tree_append(tree_rmap_item, snode, 0);

			/*
			 * The order of these two stable append is important:
			 * we are scanning rmap_item.
			 */
			if (success1)
				stable_tree_append(rmap_item, snode, 1);

			/*
			 * The original kpage may be unlocked inside
			 * stable_tree_insert() already. This page
			 * should be unlocked before doing
			 * break_cow().
			 */
			unlock_page(kpage);

			if (!success1)
				break_cow(rmap_item);

			if (!success2)
				break_cow(tree_rmap_item);

		} else if (err == MERGE_ERR_COLLI) {
			BUG_ON(tree_rmap_item->tree_node->count > 1);

			rmap_item_hash_max(tree_rmap_item,
					   tree_rmap_item->tree_node->hash);

			hash_max = rmap_item_hash_max(rmap_item, hash);
			cmp = hash_cmp(hash_max, tree_rmap_item->hash_max);
			parent = &tree_rmap_item->node;
			if (cmp < 0)
				new = &parent->rb_left;
			else if (cmp > 0)
				new = &parent->rb_right;
			else
				goto put_up_out;

			rmap_item->tree_node = tree_rmap_item->tree_node;
			rmap_item->address |= UNSTABLE_FLAG;
			rmap_item->hash_round = uksm_hash_round;
			rb_link_node(&rmap_item->node, parent, new);
			rb_insert_color(&rmap_item->node,
					&tree_rmap_item->tree_node->sub_root);
			rmap_item->tree_node->count++;
		} else {
			/*
			 * either one of the page has changed or they collide
			 * at the max hash, we consider them as ill items.
			 */
			remove_rmap_item_from_tree(tree_rmap_item);
		}
put_up_out:
		put_page(tree_rmap_item->page);
		up_read(&tree_rmap_item->slot->vma->vm_mm->mmap_sem);
	}
}




static inline unsigned long get_pool_index(struct vma_slot *slot,
					   unsigned long index)
{
	unsigned long pool_index;

	pool_index = (sizeof(struct rmap_list_entry *) * index) >> PAGE_SHIFT;
	if (pool_index >= slot->pool_size)
		BUG();
	return pool_index;
}

static inline unsigned long index_page_offset(unsigned long index)
{
	return offset_in_page(sizeof(struct rmap_list_entry *) * index);
}

static inline
struct rmap_list_entry *get_rmap_list_entry(struct vma_slot *slot,
					    unsigned long index, int need_alloc)
{
	unsigned long pool_index;
	struct page *page;
	void *addr;


	pool_index = get_pool_index(slot, index);
	if (!slot->rmap_list_pool[pool_index]) {
		if (!need_alloc)
			return NULL;

		page = alloc_page(GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN);
		if (!page)
			return NULL;

		slot->rmap_list_pool[pool_index] = page;
	}

	addr = kmap(slot->rmap_list_pool[pool_index]);
	addr += index_page_offset(index);

	return addr;
}

static inline void put_rmap_list_entry(struct vma_slot *slot,
				       unsigned long index)
{
	unsigned long pool_index;

	pool_index = get_pool_index(slot, index);
	BUG_ON(!slot->rmap_list_pool[pool_index]);
	kunmap(slot->rmap_list_pool[pool_index]);
}

static inline int entry_is_new(struct rmap_list_entry *entry)
{
	return !entry->item;
}

static inline unsigned long get_index_orig_addr(struct vma_slot *slot,
						unsigned long index)
{
	return slot->vma->vm_start + (index << PAGE_SHIFT);
}

static inline unsigned long get_entry_address(struct rmap_list_entry *entry)
{
	unsigned long addr;

	if (is_addr(entry->addr))
		addr = get_clean_addr(entry->addr);
	else if (entry->item)
		addr = get_rmap_addr(entry->item);
	else
		BUG();

	return addr;
}

static inline struct rmap_item *get_entry_item(struct rmap_list_entry *entry)
{
	if (is_addr(entry->addr))
		return NULL;

	return entry->item;
}

static inline void inc_rmap_list_pool_count(struct vma_slot *slot,
					    unsigned long index)
{
	unsigned long pool_index;

	pool_index = get_pool_index(slot, index);
	BUG_ON(!slot->rmap_list_pool[pool_index]);
	slot->pool_counts[pool_index]++;
}

static inline void dec_rmap_list_pool_count(struct vma_slot *slot,
					    unsigned long index)
{
	unsigned long pool_index;

	pool_index = get_pool_index(slot, index);
	BUG_ON(!slot->rmap_list_pool[pool_index]);
	BUG_ON(!slot->pool_counts[pool_index]);
	slot->pool_counts[pool_index]--;
}

static inline int entry_has_rmap(struct rmap_list_entry *entry)
{
	return !is_addr(entry->addr) && entry->item;
}

static inline void swap_entries(struct rmap_list_entry *entry1,
				unsigned long index1,
				struct rmap_list_entry *entry2,
				unsigned long index2)
{
	struct rmap_list_entry tmp;

	/* swapping two new entries is meaningless */
	BUG_ON(entry_is_new(entry1) && entry_is_new(entry2));

	tmp = *entry1;
	*entry1 = *entry2;
	*entry2 = tmp;

	if (entry_has_rmap(entry1))
		entry1->item->entry_index = index1;

	if (entry_has_rmap(entry2))
		entry2->item->entry_index = index2;

	if (entry_has_rmap(entry1) && !entry_has_rmap(entry2)) {
		inc_rmap_list_pool_count(entry1->item->slot, index1);
		dec_rmap_list_pool_count(entry1->item->slot, index2);
	} else if (!entry_has_rmap(entry1) && entry_has_rmap(entry2)) {
		inc_rmap_list_pool_count(entry2->item->slot, index2);
		dec_rmap_list_pool_count(entry2->item->slot, index1);
	}
}

static inline void free_entry_item(struct rmap_list_entry *entry)
{
	unsigned long index;
	struct rmap_item *item;

	if (!is_addr(entry->addr)) {
		BUG_ON(!entry->item);
		item = entry->item;
		entry->addr = get_rmap_addr(item);
		set_is_addr(entry->addr);
		index = item->entry_index;
		remove_rmap_item_from_tree(item);
		dec_rmap_list_pool_count(item->slot, index);
		free_rmap_item(item);
	}
}

static inline int pool_entry_boundary(unsigned long index)
{
	unsigned long linear_addr;

	linear_addr = sizeof(struct rmap_list_entry *) * index;
	return index && !offset_in_page(linear_addr);
}

static inline void try_free_last_pool(struct vma_slot *slot,
				      unsigned long index)
{
	unsigned long pool_index;

	pool_index = get_pool_index(slot, index);
	if (slot->rmap_list_pool[pool_index] &&
	    !slot->pool_counts[pool_index]) {
		__free_page(slot->rmap_list_pool[pool_index]);
		slot->rmap_list_pool[pool_index] = NULL;
		slot->flags |= UKSM_SLOT_NEED_SORT;
	}

}

static inline unsigned long vma_item_index(struct vm_area_struct *vma,
					   struct rmap_item *item)
{
	return (get_rmap_addr(item) - vma->vm_start) >> PAGE_SHIFT;
}

static int within_same_pool(struct vma_slot *slot,
			    unsigned long i, unsigned long j)
{
	unsigned long pool_i, pool_j;

	pool_i = get_pool_index(slot, i);
	pool_j = get_pool_index(slot, j);

	return (pool_i == pool_j);
}

static void sort_rmap_entry_list(struct vma_slot *slot)
{
	unsigned long i, j;
	struct rmap_list_entry *entry, *swap_entry;

	entry = get_rmap_list_entry(slot, 0, 0);
	for (i = 0; i < slot->pages; ) {

		if (!entry)
			goto skip_whole_pool;

		if (entry_is_new(entry))
			goto next_entry;

		if (is_addr(entry->addr)) {
			entry->addr = 0;
			goto next_entry;
		}

		j = vma_item_index(slot->vma, entry->item);
		if (j == i)
			goto next_entry;

		if (within_same_pool(slot, i, j))
			swap_entry = entry + j - i;
		else
			swap_entry = get_rmap_list_entry(slot, j, 1);

		swap_entries(entry, i, swap_entry, j);
		if (!within_same_pool(slot, i, j))
			put_rmap_list_entry(slot, j);
		continue;

skip_whole_pool:
		i += PAGE_SIZE / sizeof(*entry);
		if (i < slot->pages)
			entry = get_rmap_list_entry(slot, i, 0);
		continue;

next_entry:
		if (i >= slot->pages - 1 ||
		    !within_same_pool(slot, i, i + 1)) {
			put_rmap_list_entry(slot, i);
			if (i + 1 < slot->pages)
				entry = get_rmap_list_entry(slot, i + 1, 0);
		} else
			entry++;
		i++;
		continue;
	}

	/* free empty pool entries which contain no rmap_item */
	/* CAN be simplied to based on only pool_counts when bug freed !!!!! */
	for (i = 0; i < slot->pool_size; i++) {
		unsigned char has_rmap;
		void *addr;

		if (!slot->rmap_list_pool[i])
			continue;

		has_rmap = 0;
		addr = kmap(slot->rmap_list_pool[i]);
		BUG_ON(!addr);
		for (j = 0; j < PAGE_SIZE / sizeof(*entry); j++) {
			entry = (struct rmap_list_entry *)addr + j;
			if (is_addr(entry->addr))
				continue;
			if (!entry->item)
				continue;
			has_rmap = 1;
		}
		kunmap(slot->rmap_list_pool[i]);
		if (!has_rmap) {
			BUG_ON(slot->pool_counts[i]);
			__free_page(slot->rmap_list_pool[i]);
			slot->rmap_list_pool[i] = NULL;
		}
	}

	slot->flags &= ~UKSM_SLOT_NEED_SORT;
}

/*
 * vma_fully_scanned() - if all the pages in this slot have been scanned.
 */
static inline int vma_fully_scanned(struct vma_slot *slot)
{
	return slot->pages_scanned == slot->pages;
}

/**
 * get_next_rmap_item() - Get the next rmap_item in a vma_slot according to
 * its random permutation. This function is embedded with the random
 * permutation index management code.
 */
static struct rmap_item *get_next_rmap_item(struct vma_slot *slot, u32 *hash)
{
	unsigned long rand_range, addr, swap_index, scan_index;
	struct rmap_item *item = NULL;
	struct rmap_list_entry *scan_entry, *swap_entry = NULL;
	struct page *page;

	scan_index = swap_index = slot->pages_scanned % slot->pages;

	if (pool_entry_boundary(scan_index))
		try_free_last_pool(slot, scan_index - 1);

	if (vma_fully_scanned(slot)) {
		if (slot->flags & UKSM_SLOT_NEED_SORT)
			slot->flags |= UKSM_SLOT_NEED_RERAND;
		else
			slot->flags &= ~UKSM_SLOT_NEED_RERAND;
		if (slot->flags & UKSM_SLOT_NEED_SORT)
			sort_rmap_entry_list(slot);
	}

	scan_entry = get_rmap_list_entry(slot, scan_index, 1);
	if (!scan_entry)
		return NULL;

	if (entry_is_new(scan_entry)) {
		scan_entry->addr = get_index_orig_addr(slot, scan_index);
		set_is_addr(scan_entry->addr);
	}

	if (slot->flags & UKSM_SLOT_NEED_RERAND) {
		rand_range = slot->pages - scan_index;
		BUG_ON(!rand_range);
		swap_index = scan_index + (prandom_u32() % rand_range);
	}

	if (swap_index != scan_index) {
		swap_entry = get_rmap_list_entry(slot, swap_index, 1);
		if (entry_is_new(swap_entry)) {
			swap_entry->addr = get_index_orig_addr(slot,
							       swap_index);
			set_is_addr(swap_entry->addr);
		}
		swap_entries(scan_entry, scan_index, swap_entry, swap_index);
	}

	addr = get_entry_address(scan_entry);
	item = get_entry_item(scan_entry);
	BUG_ON(addr > slot->vma->vm_end || addr < slot->vma->vm_start);

	page = follow_page(slot->vma, addr, FOLL_GET);
	if (IS_ERR_OR_NULL(page))
		goto nopage;

	if (!PageAnon(page) && !page_trans_compound_anon(page))
		goto putpage;

	/*check is zero_page pfn or uksm_zero_page*/
	if ((page_to_pfn(page) == zero_pfn)
			|| (page_to_pfn(page) == uksm_zero_pfn))
		goto putpage;

	flush_anon_page(slot->vma, page, addr);
	flush_dcache_page(page);


	*hash = page_hash(page, hash_strength, 1);
	inc_uksm_pages_scanned();
	/*if the page content all zero, re-map to zero-page*/
	if (find_zero_page_hash(hash_strength, *hash)) {
		if (!cmp_and_merge_zero_page(slot->vma, page)) {
			slot->pages_merged++;
			__inc_zone_page_state(page, NR_UKSM_ZERO_PAGES);
			dec_mm_counter(slot->mm, MM_ANONPAGES);

			/* For full-zero pages, no need to create rmap item */
			goto putpage;
		} else {
			inc_rshash_neg(memcmp_cost / 2);
		}
	}

	if (!item) {
		item = alloc_rmap_item();
		if (item) {
			/* It has already been zeroed */
			item->slot = slot;
			item->address = addr;
			item->entry_index = scan_index;
			scan_entry->item = item;
			inc_rmap_list_pool_count(slot, scan_index);
		} else
			goto putpage;
	}

	BUG_ON(item->slot != slot);
	/* the page may have changed */
	item->page = page;
	put_rmap_list_entry(slot, scan_index);
	if (swap_entry)
		put_rmap_list_entry(slot, swap_index);
	return item;

putpage:
	put_page(page);
	page = NULL;
nopage:
	/* no page, store addr back and free rmap_item if possible */
	free_entry_item(scan_entry);
	put_rmap_list_entry(slot, scan_index);
	if (swap_entry)
		put_rmap_list_entry(slot, swap_index);
	return NULL;
}

static inline int in_stable_tree(struct rmap_item *rmap_item)
{
	return rmap_item->address & STABLE_FLAG;
}

/**
 * scan_vma_one_page() - scan the next page in a vma_slot. Called with
 * mmap_sem locked.
 */
static noinline void scan_vma_one_page(struct vma_slot *slot)
{
	u32 hash;
	struct mm_struct *mm;
	struct rmap_item *rmap_item = NULL;
	struct vm_area_struct *vma = slot->vma;

	mm = vma->vm_mm;
	BUG_ON(!mm);
	BUG_ON(!slot);

	rmap_item = get_next_rmap_item(slot, &hash);
	if (!rmap_item)
		goto out1;

	if (PageKsm(rmap_item->page) && in_stable_tree(rmap_item))
		goto out2;

	cmp_and_merge_page(rmap_item, hash);
out2:
	put_page(rmap_item->page);
out1:
	slot->pages_scanned++;
	if (slot->fully_scanned_round != fully_scanned_round)
		scanned_virtual_pages++;

	if (vma_fully_scanned(slot))
		slot->fully_scanned_round = fully_scanned_round;
}

static inline unsigned long rung_get_pages(struct scan_rung *rung)
{
	struct slot_tree_node *node;

	if (!rung->vma_root.rnode)
		return 0;

	node = container_of(rung->vma_root.rnode, struct slot_tree_node, snode);

	return node->size;
}

#define RUNG_SAMPLED_MIN	3

static inline
void uksm_calc_rung_step(struct scan_rung *rung,
			 unsigned long page_time, unsigned long ratio)
{
	unsigned long sampled, pages;

	/* will be fully scanned ? */
	if (!rung->cover_msecs) {
		rung->step = 1;
		return;
	}

	sampled = rung->cover_msecs * (NSEC_PER_MSEC / TIME_RATIO_SCALE)
		  * ratio / page_time;

	/*
	 *  Before we finsish a scan round and expensive per-round jobs,
	 *  we need to have a chance to estimate the per page time. So
	 *  the sampled number can not be too small.
	 */
	if (sampled < RUNG_SAMPLED_MIN)
		sampled = RUNG_SAMPLED_MIN;

	pages = rung_get_pages(rung);
	if (likely(pages > sampled))
		rung->step = pages / sampled;
	else
		rung->step = 1;
}

static inline int step_need_recalc(struct scan_rung *rung)
{
	unsigned long pages, stepmax;

	pages = rung_get_pages(rung);
	stepmax = pages / RUNG_SAMPLED_MIN;

	return pages && (rung->step > pages ||
			 (stepmax && rung->step > stepmax));
}

static inline
void reset_current_scan(struct scan_rung *rung, int finished, int step_recalc)
{
	struct vma_slot *slot;

	if (finished)
		rung->flags |= UKSM_RUNG_ROUND_FINISHED;

	if (step_recalc || step_need_recalc(rung)) {
		uksm_calc_rung_step(rung, uksm_ema_page_time, rung->cpu_ratio);
		BUG_ON(step_need_recalc(rung));
	}

	slot_iter_index = prandom_u32() % rung->step;
	BUG_ON(!rung->vma_root.rnode);
	slot = sradix_tree_next(&rung->vma_root, NULL, 0, slot_iter);
	BUG_ON(!slot);

	rung->current_scan = slot;
	rung->current_offset = slot_iter_index;
}

static inline struct sradix_tree_root *slot_get_root(struct vma_slot *slot)
{
	return &slot->rung->vma_root;
}

/*
 * return if resetted.
 */
static int advance_current_scan(struct scan_rung *rung)
{
	unsigned short n;
	struct vma_slot *slot, *next = NULL;

	BUG_ON(!rung->vma_root.num);

	slot = rung->current_scan;
	n = (slot->pages - rung->current_offset) % rung->step;
	slot_iter_index = rung->step - n;
	next = sradix_tree_next(&rung->vma_root, slot->snode,
				slot->sindex, slot_iter);

	if (next) {
		rung->current_offset = slot_iter_index;
		rung->current_scan = next;
		return 0;
	} else {
		reset_current_scan(rung, 1, 0);
		return 1;
	}
}

static inline void rung_rm_slot(struct vma_slot *slot)
{
	struct scan_rung *rung = slot->rung;
	struct sradix_tree_root *root;

	if (rung->current_scan == slot)
		advance_current_scan(rung);

	root = slot_get_root(slot);
	sradix_tree_delete_from_leaf(root, slot->snode, slot->sindex);
	slot->snode = NULL;
	if (step_need_recalc(rung)) {
		uksm_calc_rung_step(rung, uksm_ema_page_time, rung->cpu_ratio);
		BUG_ON(step_need_recalc(rung));
	}

	/* In case advance_current_scan loop back to this slot again */
	if (rung->vma_root.num && rung->current_scan == slot)
		reset_current_scan(slot->rung, 1, 0);
}

static inline void rung_add_new_slots(struct scan_rung *rung,
			struct vma_slot **slots, unsigned long num)
{
	int err;
	struct vma_slot *slot;
	unsigned long i;
	struct sradix_tree_root *root = &rung->vma_root;

	err = sradix_tree_enter(root, (void **)slots, num);
	BUG_ON(err);

	for (i = 0; i < num; i++) {
		slot = slots[i];
		slot->rung = rung;
		BUG_ON(vma_fully_scanned(slot));
	}

	if (rung->vma_root.num == num)
		reset_current_scan(rung, 0, 1);
}

static inline int rung_add_one_slot(struct scan_rung *rung,
				     struct vma_slot *slot)
{
	int err;

	err = sradix_tree_enter(&rung->vma_root, (void **)&slot, 1);
	if (err)
		return err;

	slot->rung = rung;
	if (rung->vma_root.num == 1)
		reset_current_scan(rung, 0, 1);

	return 0;
}

/*
 * Return true if the slot is deleted from its rung.
 */
static inline int vma_rung_enter(struct vma_slot *slot, struct scan_rung *rung)
{
	struct scan_rung *old_rung = slot->rung;
	int err;

	if (old_rung == rung)
		return 0;

	rung_rm_slot(slot);
	err = rung_add_one_slot(rung, slot);
	if (err) {
		err = rung_add_one_slot(old_rung, slot);
		WARN_ON(err); /* OOPS, badly OOM, we lost this slot */
	}

	return 1;
}

static inline int vma_rung_up(struct vma_slot *slot)
{
	struct scan_rung *rung;

	rung = slot->rung;
	if (slot->rung != &uksm_scan_ladder[SCAN_LADDER_SIZE-1])
		rung++;

	return vma_rung_enter(slot, rung);
}

static inline int vma_rung_down(struct vma_slot *slot)
{
	struct scan_rung *rung;

	rung = slot->rung;
	if (slot->rung != &uksm_scan_ladder[0])
		rung--;

	return vma_rung_enter(slot, rung);
}

/**
 * cal_dedup_ratio() - Calculate the deduplication ratio for this slot.
 */
static unsigned long cal_dedup_ratio(struct vma_slot *slot)
{
	unsigned long ret;

	BUG_ON(slot->pages_scanned == slot->last_scanned);

	ret = slot->pages_merged;

	/* Thrashing area filtering */
	if (ret && uksm_thrash_threshold) {
		if (slot->pages_cowed * 100 / slot->pages_merged
		    > uksm_thrash_threshold) {
			ret = 0;
		} else {
			ret = slot->pages_merged - slot->pages_cowed;
		}
	}

	return ret;
}

/**
 * cal_dedup_ratio() - Calculate the deduplication ratio for this slot.
 */
static unsigned long cal_dedup_ratio_old(struct vma_slot *slot)
{
	unsigned long ret;
	unsigned long pages_scanned;

	pages_scanned = slot->pages_scanned;
	if (!pages_scanned) {
		if (uksm_thrash_threshold)
			return 0;
		else
			pages_scanned = slot->pages_scanned;
	}

	ret = slot->pages_bemerged * 100 / pages_scanned;

	/* Thrashing area filtering */
	if (ret && uksm_thrash_threshold) {
		if (slot->pages_cowed * 100 / slot->pages_bemerged
		    > uksm_thrash_threshold) {
			ret = 0;
		} else {
			ret = slot->pages_bemerged - slot->pages_cowed;
		}
	}

	return ret;
}

/**
 * stable_node_reinsert() - When the hash_strength has been adjusted, the
 * stable tree need to be restructured, this is the function re-inserting the
 * stable node.
 */
static inline void stable_node_reinsert(struct stable_node *new_node,
					struct page *page,
					struct rb_root *root_treep,
					struct list_head *tree_node_listp,
					u32 hash)
{
	struct rb_node **new = &root_treep->rb_node;
	struct rb_node *parent = NULL;
	struct stable_node *stable_node;
	struct tree_node *tree_node;
	struct page *tree_page;
	int cmp;

	while (*new) {
		int cmp;

		tree_node = rb_entry(*new, struct tree_node, node);

		cmp = hash_cmp(hash, tree_node->hash);

		if (cmp < 0) {
			parent = *new;
			new = &parent->rb_left;
		} else if (cmp > 0) {
			parent = *new;
			new = &parent->rb_right;
		} else
			break;
	}

	if (*new) {
		/* find a stable tree node with same first level hash value */
		stable_node_hash_max(new_node, page, hash);
		if (tree_node->count == 1) {
			stable_node = rb_entry(tree_node->sub_root.rb_node,
					       struct stable_node, node);
			tree_page = get_uksm_page(stable_node, 1, 0);
			if (tree_page) {
				stable_node_hash_max(stable_node,
						      tree_page, hash);
				put_page(tree_page);

				/* prepare for stable node insertion */

				cmp = hash_cmp(new_node->hash_max,
						   stable_node->hash_max);
				parent = &stable_node->node;
				if (cmp < 0)
					new = &parent->rb_left;
				else if (cmp > 0)
					new = &parent->rb_right;
				else
					goto failed;

				goto add_node;
			} else {
				/* the only stable_node deleted, the tree node
				 * was not deleted.
				 */
				goto tree_node_reuse;
			}
		}

		/* well, search the collision subtree */
		new = &tree_node->sub_root.rb_node;
		parent = NULL;
		BUG_ON(!*new);
		while (*new) {
			int cmp;

			stable_node = rb_entry(*new, struct stable_node, node);

			cmp = hash_cmp(new_node->hash_max,
					   stable_node->hash_max);

			if (cmp < 0) {
				parent = *new;
				new = &parent->rb_left;
			} else if (cmp > 0) {
				parent = *new;
				new = &parent->rb_right;
			} else {
				/* oh, no, still a collision */
				goto failed;
			}
		}

		goto add_node;
	}

	/* no tree node found */
	tree_node = alloc_tree_node(tree_node_listp);
	if (!tree_node) {
		printk(KERN_ERR "UKSM: memory allocation error!\n");
		goto failed;
	} else {
		tree_node->hash = hash;
		rb_link_node(&tree_node->node, parent, new);
		rb_insert_color(&tree_node->node, root_treep);

tree_node_reuse:
		/* prepare for stable node insertion */
		parent = NULL;
		new = &tree_node->sub_root.rb_node;
	}

add_node:
	rb_link_node(&new_node->node, parent, new);
	rb_insert_color(&new_node->node, &tree_node->sub_root);
	new_node->tree_node = tree_node;
	tree_node->count++;
	return;

failed:
	/* This can only happen when two nodes have collided
	 * in two levels.
	 */
	new_node->tree_node = NULL;
	return;
}

static inline void free_all_tree_nodes(struct list_head *list)
{
	struct tree_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, list, all_list) {
		free_tree_node(node);
	}
}

/**
 * stable_tree_delta_hash() - Delta hash the stable tree from previous hash
 * strength to the current hash_strength. It re-structures the hole tree.
 */
static inline void stable_tree_delta_hash(u32 prev_hash_strength)
{
	struct stable_node *node, *tmp;
	struct rb_root *root_new_treep;
	struct list_head *new_tree_node_listp;

	stable_tree_index = (stable_tree_index + 1) % 2;
	root_new_treep = &root_stable_tree[stable_tree_index];
	new_tree_node_listp = &stable_tree_node_list[stable_tree_index];
	*root_new_treep = RB_ROOT;
	BUG_ON(!list_empty(new_tree_node_listp));

	/*
	 * we need to be safe, the node could be removed by get_uksm_page()
	 */
	list_for_each_entry_safe(node, tmp, &stable_node_list, all_list) {
		void *addr;
		struct page *node_page;
		u32 hash;

		/*
		 * We are completely re-structuring the stable nodes to a new
		 * stable tree. We don't want to touch the old tree unlinks and
		 * old tree_nodes. The old tree_nodes will be freed at once.
		 */
		node_page = get_uksm_page(node, 0, 0);
		if (!node_page)
			continue;

		if (node->tree_node) {
			hash = node->tree_node->hash;

			addr = kmap_atomic(node_page);

			hash = delta_hash(addr, prev_hash_strength,
					  hash_strength, hash);
			kunmap_atomic(addr);
		} else {
			/*
			 *it was not inserted to rbtree due to collision in last
			 *round scan.
			 */
			hash = page_hash(node_page, hash_strength, 0);
		}

		stable_node_reinsert(node, node_page, root_new_treep,
				     new_tree_node_listp, hash);
		put_page(node_page);
	}

	root_stable_treep = root_new_treep;
	free_all_tree_nodes(stable_tree_node_listp);
	BUG_ON(!list_empty(stable_tree_node_listp));
	stable_tree_node_listp = new_tree_node_listp;
}

static inline void inc_hash_strength(unsigned long delta)
{
	hash_strength += 1 << delta;
	if (hash_strength > HASH_STRENGTH_MAX)
		hash_strength = HASH_STRENGTH_MAX;
}

static inline void dec_hash_strength(unsigned long delta)
{
	unsigned long change = 1 << delta;

	if (hash_strength <= change + 1)
		hash_strength = 1;
	else
		hash_strength -= change;
}

static inline void inc_hash_strength_delta(void)
{
	hash_strength_delta++;
	if (hash_strength_delta > HASH_STRENGTH_DELTA_MAX)
		hash_strength_delta = HASH_STRENGTH_DELTA_MAX;
}

/*
static inline unsigned long get_current_neg_ratio(void)
{
	if (!rshash_pos || rshash_neg > rshash_pos)
		return 100;

	return div64_u64(100 * rshash_neg , rshash_pos);
}
*/

static inline unsigned long get_current_neg_ratio(void)
{
	u64 pos = benefit.pos;
	u64 neg = benefit.neg;

	if (!neg)
		return 0;

	if (!pos || neg > pos)
		return 100;

	if (neg > div64_u64(U64_MAX, 100))
		pos = div64_u64(pos, 100);
	else
		neg *= 100;

	return div64_u64(neg, pos);
}

static inline unsigned long get_current_benefit(void)
{
	u64 pos = benefit.pos;
	u64 neg = benefit.neg;
	u64 scanned = benefit.scanned;

	if (neg > pos)
		return 0;

	return div64_u64((pos - neg), scanned);
}

static inline int judge_rshash_direction(void)
{
	u64 current_neg_ratio, stable_benefit;
	u64 current_benefit, delta = 0;
	int ret = STILL;

	/* Try to probe a value after the boot, and in case the system
	   are still for a long time. */
	if ((fully_scanned_round & 0xFFULL) == 10) {
		ret = OBSCURE;
		goto out;
	}

	current_neg_ratio = get_current_neg_ratio();

	if (current_neg_ratio == 0) {
		rshash_neg_cont_zero++;
		if (rshash_neg_cont_zero > 2)
			return GO_DOWN;
		else
			return STILL;
	}
	rshash_neg_cont_zero = 0;

	if (current_neg_ratio > 90) {
		ret = GO_UP;
		goto out;
	}

	current_benefit = get_current_benefit();
	stable_benefit = rshash_state.stable_benefit;

	if (!stable_benefit) {
		ret = OBSCURE;
		goto out;
	}

	if (current_benefit > stable_benefit)
		delta = current_benefit - stable_benefit;
	else if (current_benefit < stable_benefit)
		delta = stable_benefit - current_benefit;

	delta = div64_u64(100 * delta , stable_benefit);

	if (delta > 50) {
		rshash_cont_obscure++;
		if (rshash_cont_obscure > 2)
			return OBSCURE;
		else
			return STILL;
	}

out:
	rshash_cont_obscure = 0;
	return ret;
}

/**
 * rshash_adjust() - The main function to control the random sampling state
 * machine for hash strength adapting.
 *
 * return true if hash_strength has changed.
 */
static inline int rshash_adjust(void)
{
	unsigned long prev_hash_strength = hash_strength;

	if (!encode_benefit())
		return 0;

	switch (rshash_state.state) {
	case RSHASH_STILL:
		switch (judge_rshash_direction()) {
		case GO_UP:
			if (rshash_state.pre_direct == GO_DOWN)
				hash_strength_delta = 0;

			inc_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
			rshash_state.stable_benefit = get_current_benefit();
			rshash_state.pre_direct = GO_UP;
			break;

		case GO_DOWN:
			if (rshash_state.pre_direct == GO_UP)
				hash_strength_delta = 0;

			dec_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
			rshash_state.stable_benefit = get_current_benefit();
			rshash_state.pre_direct = GO_DOWN;
			break;

		case OBSCURE:
			rshash_state.stable_point = hash_strength;
			rshash_state.turn_point_down = hash_strength;
			rshash_state.turn_point_up = hash_strength;
			rshash_state.turn_benefit_down = get_current_benefit();
			rshash_state.turn_benefit_up = get_current_benefit();
			rshash_state.lookup_window_index = 0;
			rshash_state.state = RSHASH_TRYDOWN;
			dec_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
			break;

		case STILL:
			break;
		default:
			BUG();
		}
		break;

	case RSHASH_TRYDOWN:
		if (rshash_state.lookup_window_index++ % 5 == 0)
			rshash_state.below_count = 0;

		if (get_current_benefit() < rshash_state.stable_benefit)
			rshash_state.below_count++;
		else if (get_current_benefit() >
			 rshash_state.turn_benefit_down) {
			rshash_state.turn_point_down = hash_strength;
			rshash_state.turn_benefit_down = get_current_benefit();
		}

		if (rshash_state.below_count >= 3 ||
		    judge_rshash_direction() == GO_UP ||
		    hash_strength == 1) {
			hash_strength = rshash_state.stable_point;
			hash_strength_delta = 0;
			inc_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
			rshash_state.lookup_window_index = 0;
			rshash_state.state = RSHASH_TRYUP;
			hash_strength_delta = 0;
		} else {
			dec_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
		}
		break;

	case RSHASH_TRYUP:
		if (rshash_state.lookup_window_index++ % 5 == 0)
			rshash_state.below_count = 0;

		if (get_current_benefit() < rshash_state.turn_benefit_down)
			rshash_state.below_count++;
		else if (get_current_benefit() > rshash_state.turn_benefit_up) {
			rshash_state.turn_point_up = hash_strength;
			rshash_state.turn_benefit_up = get_current_benefit();
		}

		if (rshash_state.below_count >= 3 ||
		    judge_rshash_direction() == GO_DOWN ||
		    hash_strength == HASH_STRENGTH_MAX) {
			hash_strength = rshash_state.turn_benefit_up >
				rshash_state.turn_benefit_down ?
				rshash_state.turn_point_up :
				rshash_state.turn_point_down;

			rshash_state.state = RSHASH_PRE_STILL;
		} else {
			inc_hash_strength(hash_strength_delta);
			inc_hash_strength_delta();
		}

		break;

	case RSHASH_NEW:
	case RSHASH_PRE_STILL:
		rshash_state.stable_benefit = get_current_benefit();
		rshash_state.state = RSHASH_STILL;
		hash_strength_delta = 0;
		break;
	default:
		BUG();
	}

	/* rshash_neg = rshash_pos = 0; */
	reset_benefit();

	if (prev_hash_strength != hash_strength)
		stable_tree_delta_hash(prev_hash_strength);

	return prev_hash_strength != hash_strength;
}

/**
 * round_update_ladder() - The main function to do update of all the
 * adjustments whenever a scan round is finished.
 */
static noinline void round_update_ladder(void)
{
	int i;
	unsigned long dedup;
	struct vma_slot *slot, *tmp_slot;

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		uksm_scan_ladder[i].flags &= ~UKSM_RUNG_ROUND_FINISHED;
	}

	list_for_each_entry_safe(slot, tmp_slot, &vma_slot_dedup, dedup_list) {

		/* slot may be rung_rm_slot() when mm exits */
		if (slot->snode) {
			dedup = cal_dedup_ratio_old(slot);
			if (dedup && dedup >= uksm_abundant_threshold)
				vma_rung_up(slot);
		}

		slot->pages_bemerged = 0;
		slot->pages_cowed = 0;

		list_del_init(&slot->dedup_list);
	}
}

static void uksm_del_vma_slot(struct vma_slot *slot)
{
	int i, j;
	struct rmap_list_entry *entry;

	if (slot->snode) {
		/*
		 * In case it just failed when entering the rung, it's not
		 * necessary.
		 */
		rung_rm_slot(slot);
	}

	if (!list_empty(&slot->dedup_list))
		list_del(&slot->dedup_list);

	if (!slot->rmap_list_pool || !slot->pool_counts) {
		/* In case it OOMed in uksm_vma_enter() */
		goto out;
	}

	for (i = 0; i < slot->pool_size; i++) {
		void *addr;

		if (!slot->rmap_list_pool[i])
			continue;

		addr = kmap(slot->rmap_list_pool[i]);
		for (j = 0; j < PAGE_SIZE / sizeof(*entry); j++) {
			entry = (struct rmap_list_entry *)addr + j;
			if (is_addr(entry->addr))
				continue;
			if (!entry->item)
				continue;

			remove_rmap_item_from_tree(entry->item);
			free_rmap_item(entry->item);
			slot->pool_counts[i]--;
		}
		BUG_ON(slot->pool_counts[i]);
		kunmap(slot->rmap_list_pool[i]);
		__free_page(slot->rmap_list_pool[i]);
	}
	kfree(slot->rmap_list_pool);
	kfree(slot->pool_counts);

out:
	slot->rung = NULL;
	BUG_ON(uksm_pages_total < slot->pages);
	if (slot->flags & UKSM_SLOT_IN_UKSM)
		uksm_pages_total -= slot->pages;

	if (slot->fully_scanned_round == fully_scanned_round)
		scanned_virtual_pages -= slot->pages;
	else
		scanned_virtual_pages -= slot->pages_scanned;
	free_vma_slot(slot);
}


#define SPIN_LOCK_PERIOD	32
static struct vma_slot *cleanup_slots[SPIN_LOCK_PERIOD];
static inline void cleanup_vma_slots(void)
{
	struct vma_slot *slot;
	int i;

	i = 0;
	spin_lock(&vma_slot_list_lock);
	while (!list_empty(&vma_slot_del)) {
		slot = list_entry(vma_slot_del.next,
				  struct vma_slot, slot_list);
		list_del(&slot->slot_list);
		cleanup_slots[i++] = slot;
		if (i == SPIN_LOCK_PERIOD) {
			spin_unlock(&vma_slot_list_lock);
			while (--i >= 0)
				uksm_del_vma_slot(cleanup_slots[i]);
			i = 0;
			spin_lock(&vma_slot_list_lock);
		}
	}
	spin_unlock(&vma_slot_list_lock);

	while (--i >= 0)
		uksm_del_vma_slot(cleanup_slots[i]);
}

/*
*expotional moving average formula
*/
static inline unsigned long ema(unsigned long curr, unsigned long last_ema)
{
	/*
	 * For a very high burst, even the ema cannot work well, a false very
	 * high per-page time estimation can result in feedback in very high
	 * overhead of context swith and rung update -- this will then lead
	 * to higher per-paper time, this may not converge.
	 *
	 * Instead, we try to approach this value in a binary manner.
	 */
	if (curr > last_ema * 10)
		return last_ema * 2;

	return (EMA_ALPHA * curr + (100 - EMA_ALPHA) * last_ema) / 100;
}

/*
 * convert cpu ratio in 1/TIME_RATIO_SCALE configured by user to
 * nanoseconds based on current uksm_sleep_jiffies.
 */
static inline unsigned long cpu_ratio_to_nsec(unsigned int ratio)
{
	return NSEC_PER_USEC * jiffies_to_usecs(uksm_sleep_jiffies) /
		(TIME_RATIO_SCALE - ratio) * ratio;
}


static inline unsigned long rung_real_ratio(int cpu_time_ratio)
{
	unsigned long ret;

	BUG_ON(!cpu_time_ratio);

	if (cpu_time_ratio > 0)
		ret = cpu_time_ratio;
	else
		ret = (unsigned long)(-cpu_time_ratio) *
			uksm_max_cpu_percentage / 100UL;

	return ret ? ret : 1;
}

static noinline void uksm_calc_scan_pages(void)
{
	struct scan_rung *ladder = uksm_scan_ladder;
	unsigned long sleep_usecs, nsecs;
	unsigned long ratio;
	int i;
	unsigned long per_page;

	if (uksm_ema_page_time > 100000 ||
	    (((unsigned long) uksm_eval_round & (256UL - 1)) == 0UL))
		uksm_ema_page_time = UKSM_PAGE_TIME_DEFAULT;

	per_page = uksm_ema_page_time;
	BUG_ON(!per_page);

	/*
	 * For every 8 eval round, we try to probe a uksm_sleep_jiffies value
	 * based on saved user input.
	 */
	if (((unsigned long) uksm_eval_round & (8UL - 1)) == 0UL)
		uksm_sleep_jiffies = uksm_sleep_saved;

	/* We require a rung scan at least 1 page in a period. */
	nsecs = per_page;
	ratio = rung_real_ratio(ladder[0].cpu_ratio);
	if (cpu_ratio_to_nsec(ratio) < nsecs) {
		sleep_usecs = nsecs * (TIME_RATIO_SCALE - ratio) / ratio
				/ NSEC_PER_USEC;
		uksm_sleep_jiffies = usecs_to_jiffies(sleep_usecs) + 1;
	}

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		ratio = rung_real_ratio(ladder[i].cpu_ratio);
		ladder[i].pages_to_scan = cpu_ratio_to_nsec(ratio) /
					per_page;
		BUG_ON(!ladder[i].pages_to_scan);
		uksm_calc_rung_step(&ladder[i], per_page, ratio);
	}
}

/*
 * From the scan time of this round (ns) to next expected min sleep time
 * (ms), be careful of the possible overflows. ratio is taken from
 * rung_real_ratio()
 */
static inline
unsigned int scan_time_to_sleep(unsigned long long scan_time, unsigned long ratio)
{
	scan_time >>= 20; /* to msec level now */
	BUG_ON(scan_time > (ULONG_MAX / TIME_RATIO_SCALE));

	return (unsigned int) ((unsigned long) scan_time *
			       (TIME_RATIO_SCALE - ratio) / ratio);
}

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)

static inline unsigned long vma_pool_size(struct vma_slot *slot)
{
	return round_up(sizeof(struct rmap_list_entry) * slot->pages,
			PAGE_SIZE) >> PAGE_SHIFT;
}

static void uksm_vma_enter(struct vma_slot **slots, unsigned long num)
{
	struct scan_rung *rung;
	unsigned long pool_size, i;
	struct vma_slot *slot;
	int failed;

	rung = &uksm_scan_ladder[0];

	failed = 0;
	for (i = 0; i < num; i++) {
		slot = slots[i];

		pool_size = vma_pool_size(slot);
		slot->rmap_list_pool = kzalloc(sizeof(struct page *) *
					       pool_size, GFP_KERNEL);
		if (!slot->rmap_list_pool)
			break;

		slot->pool_counts = kzalloc(sizeof(unsigned int) * pool_size,
					    GFP_KERNEL);
		if (!slot->pool_counts) {
			kfree(slot->rmap_list_pool);
			break;
		}

		slot->pool_size = pool_size;
		BUG_ON(CAN_OVERFLOW_U64(uksm_pages_total, slot->pages));
		slot->flags |= UKSM_SLOT_IN_UKSM;
		uksm_pages_total += slot->pages;
	}

	if (i)
		rung_add_new_slots(rung, slots, i);

	return;
}

static struct vma_slot *batch_slots[SLOT_TREE_NODE_STORE_SIZE];

static void uksm_enter_all_slots(void)
{
	struct vma_slot *slot;
	unsigned long index;
	struct list_head empty_vma_list;
	int i;

	i = 0;
	index = 0;
	INIT_LIST_HEAD(&empty_vma_list);

	spin_lock(&vma_slot_list_lock);
	while (!list_empty(&vma_slot_new)) {
		slot = list_entry(vma_slot_new.next,
				  struct vma_slot, slot_list);

		if (!slot->vma->anon_vma) {
			list_move(&slot->slot_list, &empty_vma_list);
		} else if (vma_can_enter(slot->vma)) {
			batch_slots[index++] = slot;
			list_del_init(&slot->slot_list);
		} else {
			list_move(&slot->slot_list, &vma_slot_noadd);
		}

		if (++i == SPIN_LOCK_PERIOD ||
		    (index && !(index % SLOT_TREE_NODE_STORE_SIZE))) {
			spin_unlock(&vma_slot_list_lock);

			if (index && !(index % SLOT_TREE_NODE_STORE_SIZE)) {
				uksm_vma_enter(batch_slots, index);
				index = 0;
			}
			i = 0;
			cond_resched();
			spin_lock(&vma_slot_list_lock);
		}
	}

	list_splice(&empty_vma_list, &vma_slot_new);

	spin_unlock(&vma_slot_list_lock);

	if (index)
		uksm_vma_enter(batch_slots, index);

}

static inline int rung_round_finished(struct scan_rung *rung)
{
	return rung->flags & UKSM_RUNG_ROUND_FINISHED;
}

static inline void judge_slot(struct vma_slot *slot)
{
	struct scan_rung *rung = slot->rung;
	unsigned long dedup;
	int deleted;

	dedup = cal_dedup_ratio(slot);
	if (vma_fully_scanned(slot) && uksm_thrash_threshold)
		deleted = vma_rung_enter(slot, &uksm_scan_ladder[0]);
	else if (dedup && dedup >= uksm_abundant_threshold)
		deleted = vma_rung_up(slot);
	else
		deleted = vma_rung_down(slot);

	slot->pages_merged = 0;
	slot->pages_cowed = 0;

	if (vma_fully_scanned(slot))
		slot->pages_scanned = 0;

	slot->last_scanned = slot->pages_scanned;

	/* If its deleted in above, then rung was already advanced. */
	if (!deleted)
		advance_current_scan(rung);
}


static inline int hash_round_finished(void)
{
	if (scanned_virtual_pages > (uksm_pages_total >> 2)) {
		scanned_virtual_pages = 0;
		if (uksm_pages_scanned)
			fully_scanned_round++;

		return 1;
	} else {
		return 0;
	}
}

#define UKSM_MMSEM_BATCH	5
#define BUSY_RETRY		100

/**
 * uksm_do_scan()  - the main worker function.
 */
static noinline void uksm_do_scan(void)
{
	struct vma_slot *slot, *iter;
	struct mm_struct *busy_mm;
	unsigned char round_finished, all_rungs_emtpy;
	int i, err, mmsem_batch;
	unsigned long pcost;
	long long delta_exec;
	unsigned long vpages, max_cpu_ratio;
	unsigned long long start_time, end_time, scan_time;
	unsigned int expected_jiffies;

	might_sleep();

	vpages = 0;

	start_time = task_sched_runtime(current);
	max_cpu_ratio = 0;
	mmsem_batch = 0;

	for (i = 0; i < SCAN_LADDER_SIZE;) {
		struct scan_rung *rung = &uksm_scan_ladder[i];
		unsigned long ratio;
		int busy_retry;

		if (!rung->pages_to_scan) {
			i++;
			continue;
		}

		if (!rung->vma_root.num) {
			rung->pages_to_scan = 0;
			i++;
			continue;
		}

		ratio = rung_real_ratio(rung->cpu_ratio);
		if (ratio > max_cpu_ratio)
			max_cpu_ratio = ratio;

		busy_retry = BUSY_RETRY;
		/*
		 * Do not consider rung_round_finished() here, just used up the
		 * rung->pages_to_scan quota.
		 */
		while (rung->pages_to_scan && rung->vma_root.num &&
		       likely(!freezing(current))) {
			int reset = 0;

			slot = rung->current_scan;

			BUG_ON(vma_fully_scanned(slot));

			if (mmsem_batch) {
				err = 0;
			} else {
				err = try_down_read_slot_mmap_sem(slot);
			}

			if (err == -ENOENT) {
rm_slot:
				rung_rm_slot(slot);
				continue;
			}

			busy_mm = slot->mm;

			if (err == -EBUSY) {
				/* skip other vmas on the same mm */
				do {
					reset = advance_current_scan(rung);
					iter = rung->current_scan;
					busy_retry--;
					if (iter->vma->vm_mm != busy_mm ||
					    !busy_retry || reset)
						break;
				} while (1);

				if (iter->vma->vm_mm != busy_mm) {
					continue;
				} else {
					/* scan round finsished */
					break;
				}
			}

			BUG_ON(!vma_can_enter(slot->vma));
			if (uksm_test_exit(slot->vma->vm_mm)) {
				mmsem_batch = 0;
				up_read(&slot->vma->vm_mm->mmap_sem);
				goto rm_slot;
			}

			if (mmsem_batch)
				mmsem_batch--;
			else
				mmsem_batch = UKSM_MMSEM_BATCH;

			/* Ok, we have take the mmap_sem, ready to scan */
			scan_vma_one_page(slot);
			rung->pages_to_scan--;
			vpages++;

			if (rung->current_offset + rung->step > slot->pages - 1
			    || vma_fully_scanned(slot)) {
				up_read(&slot->vma->vm_mm->mmap_sem);
				judge_slot(slot);
				mmsem_batch = 0;
			} else {
				rung->current_offset += rung->step;
				if (!mmsem_batch)
					up_read(&slot->vma->vm_mm->mmap_sem);
			}

			busy_retry = BUSY_RETRY;
			cond_resched();
		}

		if (mmsem_batch) {
			up_read(&slot->vma->vm_mm->mmap_sem);
			mmsem_batch = 0;
		}

		if (freezing(current))
			break;

		cond_resched();
	}
	end_time = task_sched_runtime(current);
	delta_exec = end_time - start_time;

	if (freezing(current))
		return;

	cleanup_vma_slots();
	uksm_enter_all_slots();

	round_finished = 1;
	all_rungs_emtpy = 1;
	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		struct scan_rung *rung = &uksm_scan_ladder[i];

		if (rung->vma_root.num) {
			all_rungs_emtpy = 0;
			if (!rung_round_finished(rung))
				round_finished = 0;
		}
	}

	if (all_rungs_emtpy)
		round_finished = 0;

	if (round_finished) {
		round_update_ladder();
		uksm_eval_round++;

		if (hash_round_finished() && rshash_adjust()) {
			/* Reset the unstable root iff hash strength changed */
			uksm_hash_round++;
			root_unstable_tree = RB_ROOT;
			free_all_tree_nodes(&unstable_tree_node_list);
		}

		/*
		 * A number of pages can hang around indefinitely on per-cpu
		 * pagevecs, raised page count preventing write_protect_page
		 * from merging them.  Though it doesn't really matter much,
		 * it is puzzling to see some stuck in pages_volatile until
		 * other activity jostles them out, and they also prevented
		 * LTP's KSM test from succeeding deterministically; so drain
		 * them here (here rather than on entry to uksm_do_scan(),
		 * so we don't IPI too often when pages_to_scan is set low).
		 */
		lru_add_drain_all();
	}


	if (vpages && delta_exec > 0) {
		pcost = (unsigned long) delta_exec / vpages;
		if (likely(uksm_ema_page_time))
			uksm_ema_page_time = ema(pcost, uksm_ema_page_time);
		else
			uksm_ema_page_time = pcost;
	}

	uksm_calc_scan_pages();
	uksm_sleep_real = uksm_sleep_jiffies;
	/* in case of radical cpu bursts, apply the upper bound */
	end_time = task_sched_runtime(current);
	if (max_cpu_ratio && end_time > start_time) {
		scan_time = end_time - start_time;
		expected_jiffies = msecs_to_jiffies(
			scan_time_to_sleep(scan_time, max_cpu_ratio));

		if (expected_jiffies > uksm_sleep_real)
			uksm_sleep_real = expected_jiffies;

		/* We have a 1 second up bound for responsiveness. */
		if (jiffies_to_msecs(uksm_sleep_real) > MSEC_PER_SEC)
			uksm_sleep_real = msecs_to_jiffies(1000);
	}

	return;
}

static int ksmd_should_run(void)
{
	return uksm_run & UKSM_RUN_MERGE;
}

static int uksm_scan_thread(void *nothing)
{
	set_freezable();
	set_user_nice(current, 5);

	while (!kthread_should_stop()) {
		mutex_lock(&uksm_thread_mutex);
		if (ksmd_should_run()) {
			uksm_do_scan();
		}
		mutex_unlock(&uksm_thread_mutex);

		try_to_freeze();

		if (ksmd_should_run()) {
			schedule_timeout_interruptible(uksm_sleep_real);
			uksm_sleep_times++;
		} else {
			wait_event_freezable(uksm_thread_wait,
				ksmd_should_run() || kthread_should_stop());
		}
	}
	return 0;
}

int page_referenced_ksm(struct page *page, struct mem_cgroup *memcg,
			unsigned long *vm_flags)
{
	struct stable_node *stable_node;
	struct node_vma *node_vma;
	struct rmap_item *rmap_item;
	unsigned int mapcount = page_mapcount(page);
	int referenced = 0;
	int search_new_forks = 0;
	unsigned long address;

	VM_BUG_ON(!PageKsm(page));
	VM_BUG_ON(!PageLocked(page));

	stable_node = page_stable_node(page);
	if (!stable_node)
		return 0;


again:
	hlist_for_each_entry(node_vma, &stable_node->hlist, hlist) {
		hlist_for_each_entry(rmap_item, &node_vma->rmap_hlist, hlist) {
			struct anon_vma *anon_vma = rmap_item->anon_vma;
			struct anon_vma_chain *vmac;
			struct vm_area_struct *vma;

			anon_vma_lock_read(anon_vma);
			anon_vma_interval_tree_foreach(vmac, &anon_vma->rb_root,
						       0, ULONG_MAX) {

				vma = vmac->vma;
				address = get_rmap_addr(rmap_item);

				if (address < vma->vm_start ||
				    address >= vma->vm_end)
					continue;
				/*
				 * Initially we examine only the vma which
				 * covers this rmap_item; but later, if there
				 * is still work to do, we examine covering
				 * vmas in other mms: in case they were forked
				 * from the original since ksmd passed.
				 */
				if ((rmap_item->slot->vma == vma) ==
				    search_new_forks)
					continue;

				if (memcg &&
				    !mm_match_cgroup(vma->vm_mm, memcg))
					continue;

				referenced +=
					page_referenced_one(page, vma,
						address, &mapcount, vm_flags);
				if (!search_new_forks || !mapcount)
					break;
			}

			anon_vma_unlock_read(anon_vma);
			if (!mapcount)
				goto out;
		}
	}
	if (!search_new_forks++)
		goto again;
out:
	return referenced;
}

int try_to_unmap_ksm(struct page *page, enum ttu_flags flags)
{
	struct stable_node *stable_node;
	struct node_vma *node_vma;
	struct rmap_item *rmap_item;
	int ret = SWAP_AGAIN;
	int search_new_forks = 0;
	unsigned long address;

	VM_BUG_ON(!PageKsm(page));
	VM_BUG_ON(!PageLocked(page));

	stable_node = page_stable_node(page);
	if (!stable_node)
		return SWAP_FAIL;
again:
	hlist_for_each_entry(node_vma, &stable_node->hlist, hlist) {
		hlist_for_each_entry(rmap_item, &node_vma->rmap_hlist, hlist) {
			struct anon_vma *anon_vma = rmap_item->anon_vma;
			struct anon_vma_chain *vmac;
			struct vm_area_struct *vma;

			anon_vma_lock_read(anon_vma);
			anon_vma_interval_tree_foreach(vmac, &anon_vma->rb_root,
						       0, ULONG_MAX) {
				vma = vmac->vma;
				address = get_rmap_addr(rmap_item);

				if (address < vma->vm_start ||
				    address >= vma->vm_end)
					continue;
				/*
				 * Initially we examine only the vma which
				 * covers this rmap_item; but later, if there
				 * is still work to do, we examine covering
				 * vmas in other mms: in case they were forked
				 * from the original since ksmd passed.
				 */
				if ((rmap_item->slot->vma == vma) ==
				    search_new_forks)
					continue;

				ret = try_to_unmap_one(page, vma,
						       address, flags);
				if (ret != SWAP_AGAIN || !page_mapped(page)) {
					anon_vma_unlock_read(anon_vma);
					goto out;
				}
			}
			anon_vma_unlock_read(anon_vma);
		}
	}
	if (!search_new_forks++)
		goto again;
out:
	return ret;
}

#ifdef CONFIG_MIGRATION
int rmap_walk_ksm(struct page *page, int (*rmap_one)(struct page *,
		  struct vm_area_struct *, unsigned long, void *), void *arg)
{
	struct stable_node *stable_node;
	struct node_vma *node_vma;
	struct rmap_item *rmap_item;
	int ret = SWAP_AGAIN;
	int search_new_forks = 0;
	unsigned long address;

	VM_BUG_ON(!PageKsm(page));
	VM_BUG_ON(!PageLocked(page));

	stable_node = page_stable_node(page);
	if (!stable_node)
		return ret;
again:
	hlist_for_each_entry(node_vma, &stable_node->hlist, hlist) {
		hlist_for_each_entry(rmap_item, &node_vma->rmap_hlist, hlist) {
			struct anon_vma *anon_vma = rmap_item->anon_vma;
			struct anon_vma_chain *vmac;
			struct vm_area_struct *vma;

			anon_vma_lock_read(anon_vma);
			anon_vma_interval_tree_foreach(vmac, &anon_vma->rb_root,
						       0, ULONG_MAX) {
				vma = vmac->vma;
				address = get_rmap_addr(rmap_item);

				if (address < vma->vm_start ||
				    address >= vma->vm_end)
					continue;

				if ((rmap_item->slot->vma == vma) ==
				    search_new_forks)
					continue;

				ret = rmap_one(page, vma, address, arg);
				if (ret != SWAP_AGAIN) {
					anon_vma_unlock_read(anon_vma);
					goto out;
				}
			}
			anon_vma_unlock_read(anon_vma);
		}
	}
	if (!search_new_forks++)
		goto again;
out:
	return ret;
}

/* Common ksm interface but may be specific to uksm */
void ksm_migrate_page(struct page *newpage, struct page *oldpage)
{
	struct stable_node *stable_node;

	VM_BUG_ON(!PageLocked(oldpage));
	VM_BUG_ON(!PageLocked(newpage));
	VM_BUG_ON(newpage->mapping != oldpage->mapping);

	stable_node = page_stable_node(newpage);
	if (stable_node) {
		VM_BUG_ON(stable_node->kpfn != page_to_pfn(oldpage));
		stable_node->kpfn = page_to_pfn(newpage);
	}
}
#endif /* CONFIG_MIGRATION */

#ifdef CONFIG_MEMORY_HOTREMOVE
static struct stable_node *uksm_check_stable_tree(unsigned long start_pfn,
						 unsigned long end_pfn)
{
	struct rb_node *node;

	for (node = rb_first(root_stable_treep); node; node = rb_next(node)) {
		struct stable_node *stable_node;

		stable_node = rb_entry(node, struct stable_node, node);
		if (stable_node->kpfn >= start_pfn &&
		    stable_node->kpfn < end_pfn)
			return stable_node;
	}
	return NULL;
}

static int uksm_memory_callback(struct notifier_block *self,
			       unsigned long action, void *arg)
{
	struct memory_notify *mn = arg;
	struct stable_node *stable_node;

	switch (action) {
	case MEM_GOING_OFFLINE:
		/*
		 * Keep it very simple for now: just lock out ksmd and
		 * MADV_UNMERGEABLE while any memory is going offline.
		 * mutex_lock_nested() is necessary because lockdep was alarmed
		 * that here we take uksm_thread_mutex inside notifier chain
		 * mutex, and later take notifier chain mutex inside
		 * uksm_thread_mutex to unlock it.   But that's safe because both
		 * are inside mem_hotplug_mutex.
		 */
		mutex_lock_nested(&uksm_thread_mutex, SINGLE_DEPTH_NESTING);
		break;

	case MEM_OFFLINE:
		/*
		 * Most of the work is done by page migration; but there might
		 * be a few stable_nodes left over, still pointing to struct
		 * pages which have been offlined: prune those from the tree.
		 */
		while ((stable_node = uksm_check_stable_tree(mn->start_pfn,
					mn->start_pfn + mn->nr_pages)) != NULL)
			remove_node_from_stable_tree(stable_node, 1, 1);
		/* fallthrough */

	case MEM_CANCEL_OFFLINE:
		mutex_unlock(&uksm_thread_mutex);
		break;
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

#ifdef CONFIG_SYSFS
/*
 * This all compiles without CONFIG_SYSFS, but is a waste of space.
 */

#define UKSM_ATTR_RO(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define UKSM_ATTR(_name) \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t max_cpu_percentage_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", uksm_max_cpu_percentage);
}

static ssize_t max_cpu_percentage_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long max_cpu_percentage;
	int err;

	err = strict_strtoul(buf, 10, &max_cpu_percentage);
	if (err || max_cpu_percentage > 100)
		return -EINVAL;

	if (max_cpu_percentage == 100)
		max_cpu_percentage = 99;
	else if (max_cpu_percentage < 10)
		max_cpu_percentage = 10;

	uksm_max_cpu_percentage = max_cpu_percentage;

	return count;
}
UKSM_ATTR(max_cpu_percentage);

static ssize_t sleep_millisecs_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", jiffies_to_msecs(uksm_sleep_jiffies));
}

static ssize_t sleep_millisecs_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned long msecs;
	int err;

	err = strict_strtoul(buf, 10, &msecs);
	if (err || msecs > MSEC_PER_SEC)
		return -EINVAL;

	uksm_sleep_jiffies = msecs_to_jiffies(msecs);
	uksm_sleep_saved = uksm_sleep_jiffies;

	return count;
}
UKSM_ATTR(sleep_millisecs);


static ssize_t cpu_governor_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	int n = sizeof(uksm_cpu_governor_str) / sizeof(char *);
	int i;

	buf[0] = '\0';
	for (i = 0; i < n ; i++) {
		if (uksm_cpu_governor == i)
			strcat(buf, "[");

		strcat(buf, uksm_cpu_governor_str[i]);

		if (uksm_cpu_governor == i)
			strcat(buf, "]");

		strcat(buf, " ");
	}
	strcat(buf, "\n");

	return strlen(buf);
}

static inline void init_performance_values(void)
{
	int i;
	struct scan_rung *rung;
	struct uksm_cpu_preset_s *preset = uksm_cpu_preset + uksm_cpu_governor;


	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = uksm_scan_ladder + i;
		rung->cpu_ratio = preset->cpu_ratio[i];
		rung->cover_msecs = preset->cover_msecs[i];
	}

	uksm_max_cpu_percentage = preset->max_cpu;
}

static ssize_t cpu_governor_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int n = sizeof(uksm_cpu_governor_str) / sizeof(char *);

	for (n--; n >=0 ; n--) {
		if (!strncmp(buf, uksm_cpu_governor_str[n],
			     strlen(uksm_cpu_governor_str[n])))
			break;
	}

	if (n < 0)
		return -EINVAL;
	else
		uksm_cpu_governor = n;

	init_performance_values();

	return count;
}
UKSM_ATTR(cpu_governor);

static ssize_t run_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%u\n", uksm_run);
}

static ssize_t run_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int err;
	unsigned long flags;

	err = strict_strtoul(buf, 10, &flags);
	if (err || flags > UINT_MAX)
		return -EINVAL;
	if (flags > UKSM_RUN_MERGE)
		return -EINVAL;

	mutex_lock(&uksm_thread_mutex);
	if (uksm_run != flags) {
		uksm_run = flags;
	}
	mutex_unlock(&uksm_thread_mutex);

	if (flags & UKSM_RUN_MERGE)
		wake_up_interruptible(&uksm_thread_wait);

	return count;
}
UKSM_ATTR(run);

static ssize_t abundant_threshold_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", uksm_abundant_threshold);
}

static ssize_t abundant_threshold_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int err;
	unsigned long flags;

	err = strict_strtoul(buf, 10, &flags);
	if (err || flags > 99)
		return -EINVAL;

	uksm_abundant_threshold = flags;

	return count;
}
UKSM_ATTR(abundant_threshold);

static ssize_t thrash_threshold_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", uksm_thrash_threshold);
}

static ssize_t thrash_threshold_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int err;
	unsigned long flags;

	err = strict_strtoul(buf, 10, &flags);
	if (err || flags > 99)
		return -EINVAL;

	uksm_thrash_threshold = flags;

	return count;
}
UKSM_ATTR(thrash_threshold);

static ssize_t cpu_ratios_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	int i, size;
	struct scan_rung *rung;
	char *p = buf;

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = &uksm_scan_ladder[i];

		if (rung->cpu_ratio > 0)
			size = sprintf(p, "%d ", rung->cpu_ratio);
		else
			size = sprintf(p, "MAX/%d ",
					TIME_RATIO_SCALE / -rung->cpu_ratio);

		p += size;
	}

	*p++ = '\n';
	*p = '\0';

	return p - buf;
}

static ssize_t cpu_ratios_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int i, cpuratios[SCAN_LADDER_SIZE], err;
	unsigned long value;
	struct scan_rung *rung;
	char *p, *end = NULL;

	p = kzalloc(count, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, buf, count);

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		if (i != SCAN_LADDER_SIZE -1) {
			end = strchr(p, ' ');
			if (!end)
				return -EINVAL;

			*end = '\0';
		}

		if (strstr(p, "MAX/")) {
			p = strchr(p, '/') + 1;
			err = strict_strtoul(p, 10, &value);
			if (err || value > TIME_RATIO_SCALE || !value)
				return -EINVAL;

			cpuratios[i] = - (int) (TIME_RATIO_SCALE / value);
		} else {
			err = strict_strtoul(p, 10, &value);
			if (err || value > TIME_RATIO_SCALE || !value)
				return -EINVAL;

			cpuratios[i] = value;
		}

		p = end + 1;
	}

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = &uksm_scan_ladder[i];

		rung->cpu_ratio = cpuratios[i];
	}

	return count;
}
UKSM_ATTR(cpu_ratios);

static ssize_t eval_intervals_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	int i, size;
	struct scan_rung *rung;
	char *p = buf;

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = &uksm_scan_ladder[i];
		size = sprintf(p, "%u ", rung->cover_msecs);
		p += size;
	}

	*p++ = '\n';
	*p = '\0';

	return p - buf;
}

static ssize_t eval_intervals_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int i, err;
	unsigned long values[SCAN_LADDER_SIZE];
	struct scan_rung *rung;
	char *p, *end = NULL;

	p = kzalloc(count, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, buf, count);

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		if (i != SCAN_LADDER_SIZE -1) {
			end = strchr(p, ' ');
			if (!end)
				return -EINVAL;

			*end = '\0';
		}

		err = strict_strtoul(p, 10, &values[i]);
		if (err)
			return -EINVAL;

		p = end + 1;
	}

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = &uksm_scan_ladder[i];

		rung->cover_msecs = values[i];
	}

	return count;
}
UKSM_ATTR(eval_intervals);

static ssize_t ema_per_page_time_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", uksm_ema_page_time);
}
UKSM_ATTR_RO(ema_per_page_time);

static ssize_t pages_shared_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", uksm_pages_shared);
}
UKSM_ATTR_RO(pages_shared);

static ssize_t pages_sharing_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", uksm_pages_sharing);
}
UKSM_ATTR_RO(pages_sharing);

static ssize_t pages_unshared_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", uksm_pages_unshared);
}
UKSM_ATTR_RO(pages_unshared);

static ssize_t full_scans_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", fully_scanned_round);
}
UKSM_ATTR_RO(full_scans);

static ssize_t pages_scanned_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	unsigned long base = 0;
	u64 delta, ret;

	if (pages_scanned_stored) {
		base = pages_scanned_base;
		ret = pages_scanned_stored;
		delta = uksm_pages_scanned >> base;
		if (CAN_OVERFLOW_U64(ret, delta)) {
			ret >>= 1;
			delta >>= 1;
			base++;
			ret += delta;
		}
	} else {
		ret = uksm_pages_scanned;
	}

	while (ret > ULONG_MAX) {
		ret >>= 1;
		base++;
	}

	if (base)
		return sprintf(buf, "%lu * 2^%lu\n", (unsigned long)ret, base);
	else
		return sprintf(buf, "%lu\n", (unsigned long)ret);
}
UKSM_ATTR_RO(pages_scanned);

static ssize_t hash_strength_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", hash_strength);
}
UKSM_ATTR_RO(hash_strength);

static ssize_t sleep_times_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", uksm_sleep_times);
}
UKSM_ATTR_RO(sleep_times);


static struct attribute *uksm_attrs[] = {
	&max_cpu_percentage_attr.attr,
	&sleep_millisecs_attr.attr,
	&cpu_governor_attr.attr,
	&run_attr.attr,
	&ema_per_page_time_attr.attr,
	&pages_shared_attr.attr,
	&pages_sharing_attr.attr,
	&pages_unshared_attr.attr,
	&full_scans_attr.attr,
	&pages_scanned_attr.attr,
	&hash_strength_attr.attr,
	&sleep_times_attr.attr,
	&thrash_threshold_attr.attr,
	&abundant_threshold_attr.attr,
	&cpu_ratios_attr.attr,
	&eval_intervals_attr.attr,
	NULL,
};

static struct attribute_group uksm_attr_group = {
	.attrs = uksm_attrs,
	.name = "uksm",
};
#endif /* CONFIG_SYSFS */

static inline void init_scan_ladder(void)
{
	int i;
	struct scan_rung *rung;

	for (i = 0; i < SCAN_LADDER_SIZE; i++) {
		rung = uksm_scan_ladder + i;
		slot_tree_init_root(&rung->vma_root);
	}

	init_performance_values();
	uksm_calc_scan_pages();
}

static inline int cal_positive_negative_costs(void)
{
	struct page *p1, *p2;
	unsigned char *addr1, *addr2;
	unsigned long i, time_start, hash_cost;
	unsigned long loopnum = 0;

	/*IMPORTANT: volatile is needed to prevent over-optimization by gcc. */
	volatile u32 hash;
	volatile int ret;

	p1 = alloc_page(GFP_KERNEL);
	if (!p1)
		return -ENOMEM;

	p2 = alloc_page(GFP_KERNEL);
	if (!p2)
		return -ENOMEM;

	addr1 = kmap_atomic(p1);
	addr2 = kmap_atomic(p2);
	memset(addr1, prandom_u32(), PAGE_SIZE);
	memcpy(addr2, addr1, PAGE_SIZE);

	/* make sure that the two pages differ in last byte */
	addr2[PAGE_SIZE-1] = ~addr2[PAGE_SIZE-1];
	kunmap_atomic(addr2);
	kunmap_atomic(addr1);

	time_start = jiffies;
	while (jiffies - time_start < 100) {
		for (i = 0; i < 100; i++)
			hash = page_hash(p1, HASH_STRENGTH_FULL, 0);
		loopnum += 100;
	}
	hash_cost = (jiffies - time_start);

	time_start = jiffies;
	for (i = 0; i < loopnum; i++)
		ret = pages_identical(p1, p2);
	memcmp_cost = HASH_STRENGTH_FULL * (jiffies - time_start);
	memcmp_cost /= hash_cost;
	printk(KERN_INFO "UKSM: relative memcmp_cost = %lu "
			 "hash=%u cmp_ret=%d.\n",
	       memcmp_cost, hash, ret);

	__free_page(p1);
	__free_page(p2);
	return 0;
}

static int init_zeropage_hash_table(void)
{
	struct page *page;
	char *addr;
	int i;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	addr = kmap_atomic(page);
	memset(addr, 0, PAGE_SIZE);
	kunmap_atomic(addr);

	zero_hash_table = kmalloc(HASH_STRENGTH_MAX * sizeof(u32),
		GFP_KERNEL);
	if (!zero_hash_table)
		return -ENOMEM;

	for (i = 0; i < HASH_STRENGTH_MAX; i++)
		zero_hash_table[i] = page_hash(page, i, 0);

	__free_page(page);

	return 0;
}

static inline int init_random_sampling(void)
{
	unsigned long i;
	random_nums = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!random_nums)
		return -ENOMEM;

	for (i = 0; i < HASH_STRENGTH_FULL; i++)
		random_nums[i] = i;

	for (i = 0; i < HASH_STRENGTH_FULL; i++) {
		unsigned long rand_range, swap_index, tmp;

		rand_range = HASH_STRENGTH_FULL - i;
		swap_index = i + prandom_u32() % rand_range;
		tmp = random_nums[i];
		random_nums[i] =  random_nums[swap_index];
		random_nums[swap_index] = tmp;
	}

	rshash_state.state = RSHASH_NEW;
	rshash_state.below_count = 0;
	rshash_state.lookup_window_index = 0;

	return cal_positive_negative_costs();
}

static int __init uksm_slab_init(void)
{
	rmap_item_cache = UKSM_KMEM_CACHE(rmap_item, 0);
	if (!rmap_item_cache)
		goto out;

	stable_node_cache = UKSM_KMEM_CACHE(stable_node, 0);
	if (!stable_node_cache)
		goto out_free1;

	node_vma_cache = UKSM_KMEM_CACHE(node_vma, 0);
	if (!node_vma_cache)
		goto out_free2;

	vma_slot_cache = UKSM_KMEM_CACHE(vma_slot, 0);
	if (!vma_slot_cache)
		goto out_free3;

	tree_node_cache = UKSM_KMEM_CACHE(tree_node, 0);
	if (!tree_node_cache)
		goto out_free4;

	return 0;

out_free4:
	kmem_cache_destroy(vma_slot_cache);
out_free3:
	kmem_cache_destroy(node_vma_cache);
out_free2:
	kmem_cache_destroy(stable_node_cache);
out_free1:
	kmem_cache_destroy(rmap_item_cache);
out:
	return -ENOMEM;
}

static void __init uksm_slab_free(void)
{
	kmem_cache_destroy(stable_node_cache);
	kmem_cache_destroy(rmap_item_cache);
	kmem_cache_destroy(node_vma_cache);
	kmem_cache_destroy(vma_slot_cache);
	kmem_cache_destroy(tree_node_cache);
}

/* Common interface to ksm, different to it. */
int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	int err;

	switch (advice) {
	case MADV_MERGEABLE:
		return 0;		/* just ignore the advice */

	case MADV_UNMERGEABLE:
		if (!(*vm_flags & VM_MERGEABLE))
			return 0;		/* just ignore the advice */

		if (vma->anon_vma) {
			err = unmerge_uksm_pages(vma, start, end);
			if (err)
				return err;
		}

		uksm_remove_vma(vma);
		*vm_flags &= ~VM_MERGEABLE;
		break;
	}

	return 0;
}

/* Common interface to ksm, actually the same. */
struct page *ksm_might_need_to_copy(struct page *page,
			struct vm_area_struct *vma, unsigned long address)
{
	struct anon_vma *anon_vma = page_anon_vma(page);
	struct page *new_page;

	if (PageKsm(page)) {
		if (page_stable_node(page))
			return page;	/* no need to copy it */
	} else if (!anon_vma) {
		return page;		/* no need to copy it */
	} else if (anon_vma->root == vma->anon_vma->root &&
		 page->index == linear_page_index(vma, address)) {
		return page;		/* still no need to copy it */
	}
	if (!PageUptodate(page))
		return page;		/* let do_swap_page report the error */

	new_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, address);
	if (new_page) {
		copy_user_highpage(new_page, page, address, vma);

		SetPageDirty(new_page);
		__SetPageUptodate(new_page);
		__set_page_locked(new_page);
	}

	return new_page;
}

static int __init uksm_init(void)
{
	struct task_struct *uksm_thread;
	int err;

	uksm_sleep_jiffies = msecs_to_jiffies(100);
	uksm_sleep_saved = uksm_sleep_jiffies;

	slot_tree_init();
	init_scan_ladder();


	err = init_random_sampling();
	if (err)
		goto out_free2;

	err = uksm_slab_init();
	if (err)
		goto out_free1;

	err = init_zeropage_hash_table();
	if (err)
		goto out_free0;

	uksm_thread = kthread_run(uksm_scan_thread, NULL, "uksmd");
	if (IS_ERR(uksm_thread)) {
		printk(KERN_ERR "uksm: creating kthread failed\n");
		err = PTR_ERR(uksm_thread);
		goto out_free;
	}

#ifdef CONFIG_SYSFS
	err = sysfs_create_group(mm_kobj, &uksm_attr_group);
	if (err) {
		printk(KERN_ERR "uksm: register sysfs failed\n");
		kthread_stop(uksm_thread);
		goto out_free;
	}
#else
	uksm_run = UKSM_RUN_MERGE;	/* no way for user to start it */

#endif /* CONFIG_SYSFS */

#ifdef CONFIG_MEMORY_HOTREMOVE
	/*
	 * Choose a high priority since the callback takes uksm_thread_mutex:
	 * later callbacks could only be taking locks which nest within that.
	 */
	hotplug_memory_notifier(uksm_memory_callback, 100);
#endif
	return 0;

out_free:
	kfree(zero_hash_table);
out_free0:
	uksm_slab_free();
out_free1:
	kfree(random_nums);
out_free2:
	kfree(uksm_scan_ladder);
	return err;
}

#ifdef MODULE
module_init(uksm_init)
#else
late_initcall(uksm_init);
#endif

