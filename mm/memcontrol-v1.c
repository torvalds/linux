// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/memcontrol.h>
#include <linux/swap.h>
#include <linux/mm_inline.h>
#include <linux/pagewalk.h>
#include <linux/backing-dev.h>
#include <linux/swap_cgroup.h>
#include <linux/eventfd.h>
#include <linux/poll.h>
#include <linux/sort.h>
#include <linux/file.h>
#include <linux/seq_buf.h>

#include "internal.h"
#include "swap.h"
#include "memcontrol-v1.h"

/*
 * Cgroups above their limits are maintained in a RB-Tree, independent of
 * their hierarchy representation
 */

struct mem_cgroup_tree_per_node {
	struct rb_root rb_root;
	struct rb_node *rb_rightmost;
	spinlock_t lock;
};

struct mem_cgroup_tree {
	struct mem_cgroup_tree_per_node *rb_tree_per_node[MAX_NUMNODES];
};

static struct mem_cgroup_tree soft_limit_tree __read_mostly;

/*
 * Maximum loops in mem_cgroup_soft_reclaim(), used for soft
 * limit reclaim to prevent infinite loops, if they ever occur.
 */
#define	MEM_CGROUP_MAX_RECLAIM_LOOPS		100
#define	MEM_CGROUP_MAX_SOFT_LIMIT_RECLAIM_LOOPS	2

/* Stuffs for move charges at task migration. */
/*
 * Types of charges to be moved.
 */
#define MOVE_ANON	0x1ULL
#define MOVE_FILE	0x2ULL
#define MOVE_MASK	(MOVE_ANON | MOVE_FILE)

/* "mc" and its members are protected by cgroup_mutex */
static struct move_charge_struct {
	spinlock_t	  lock; /* for from, to */
	struct mm_struct  *mm;
	struct mem_cgroup *from;
	struct mem_cgroup *to;
	unsigned long flags;
	unsigned long precharge;
	unsigned long moved_charge;
	unsigned long moved_swap;
	struct task_struct *moving_task;	/* a task moving charges */
	wait_queue_head_t waitq;		/* a waitq for other context */
} mc = {
	.lock = __SPIN_LOCK_UNLOCKED(mc.lock),
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(mc.waitq),
};

/* for OOM */
struct mem_cgroup_eventfd_list {
	struct list_head list;
	struct eventfd_ctx *eventfd;
};

/*
 * cgroup_event represents events which userspace want to receive.
 */
struct mem_cgroup_event {
	/*
	 * memcg which the event belongs to.
	 */
	struct mem_cgroup *memcg;
	/*
	 * eventfd to signal userspace about the event.
	 */
	struct eventfd_ctx *eventfd;
	/*
	 * Each of these stored in a list by the cgroup.
	 */
	struct list_head list;
	/*
	 * register_event() callback will be used to add new userspace
	 * waiter for changes related to this event.  Use eventfd_signal()
	 * on eventfd to send notification to userspace.
	 */
	int (*register_event)(struct mem_cgroup *memcg,
			      struct eventfd_ctx *eventfd, const char *args);
	/*
	 * unregister_event() callback will be called when userspace closes
	 * the eventfd or on cgroup removing.  This callback must be set,
	 * if you want provide notification functionality.
	 */
	void (*unregister_event)(struct mem_cgroup *memcg,
				 struct eventfd_ctx *eventfd);
	/*
	 * All fields below needed to unregister event when
	 * userspace closes eventfd.
	 */
	poll_table pt;
	wait_queue_head_t *wqh;
	wait_queue_entry_t wait;
	struct work_struct remove;
};

#define MEMFILE_PRIVATE(x, val)	((x) << 16 | (val))
#define MEMFILE_TYPE(val)	((val) >> 16 & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)

enum {
	RES_USAGE,
	RES_LIMIT,
	RES_MAX_USAGE,
	RES_FAILCNT,
	RES_SOFT_LIMIT,
};

#ifdef CONFIG_LOCKDEP
static struct lockdep_map memcg_oom_lock_dep_map = {
	.name = "memcg_oom_lock",
};
#endif

DEFINE_SPINLOCK(memcg_oom_lock);

static void __mem_cgroup_insert_exceeded(struct mem_cgroup_per_node *mz,
					 struct mem_cgroup_tree_per_node *mctz,
					 unsigned long new_usage_in_excess)
{
	struct rb_node **p = &mctz->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct mem_cgroup_per_node *mz_node;
	bool rightmost = true;

	if (mz->on_tree)
		return;

	mz->usage_in_excess = new_usage_in_excess;
	if (!mz->usage_in_excess)
		return;
	while (*p) {
		parent = *p;
		mz_node = rb_entry(parent, struct mem_cgroup_per_node,
					tree_node);
		if (mz->usage_in_excess < mz_node->usage_in_excess) {
			p = &(*p)->rb_left;
			rightmost = false;
		} else {
			p = &(*p)->rb_right;
		}
	}

	if (rightmost)
		mctz->rb_rightmost = &mz->tree_node;

	rb_link_node(&mz->tree_node, parent, p);
	rb_insert_color(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = true;
}

static void __mem_cgroup_remove_exceeded(struct mem_cgroup_per_node *mz,
					 struct mem_cgroup_tree_per_node *mctz)
{
	if (!mz->on_tree)
		return;

	if (&mz->tree_node == mctz->rb_rightmost)
		mctz->rb_rightmost = rb_prev(&mz->tree_node);

	rb_erase(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = false;
}

static void mem_cgroup_remove_exceeded(struct mem_cgroup_per_node *mz,
				       struct mem_cgroup_tree_per_node *mctz)
{
	unsigned long flags;

	spin_lock_irqsave(&mctz->lock, flags);
	__mem_cgroup_remove_exceeded(mz, mctz);
	spin_unlock_irqrestore(&mctz->lock, flags);
}

static unsigned long soft_limit_excess(struct mem_cgroup *memcg)
{
	unsigned long nr_pages = page_counter_read(&memcg->memory);
	unsigned long soft_limit = READ_ONCE(memcg->soft_limit);
	unsigned long excess = 0;

	if (nr_pages > soft_limit)
		excess = nr_pages - soft_limit;

	return excess;
}

static void memcg1_update_tree(struct mem_cgroup *memcg, int nid)
{
	unsigned long excess;
	struct mem_cgroup_per_node *mz;
	struct mem_cgroup_tree_per_node *mctz;

	if (lru_gen_enabled()) {
		if (soft_limit_excess(memcg))
			lru_gen_soft_reclaim(memcg, nid);
		return;
	}

	mctz = soft_limit_tree.rb_tree_per_node[nid];
	if (!mctz)
		return;
	/*
	 * Necessary to update all ancestors when hierarchy is used.
	 * because their event counter is not touched.
	 */
	for (; memcg; memcg = parent_mem_cgroup(memcg)) {
		mz = memcg->nodeinfo[nid];
		excess = soft_limit_excess(memcg);
		/*
		 * We have to update the tree if mz is on RB-tree or
		 * mem is over its softlimit.
		 */
		if (excess || mz->on_tree) {
			unsigned long flags;

			spin_lock_irqsave(&mctz->lock, flags);
			/* if on-tree, remove it */
			if (mz->on_tree)
				__mem_cgroup_remove_exceeded(mz, mctz);
			/*
			 * Insert again. mz->usage_in_excess will be updated.
			 * If excess is 0, no tree ops.
			 */
			__mem_cgroup_insert_exceeded(mz, mctz, excess);
			spin_unlock_irqrestore(&mctz->lock, flags);
		}
	}
}

void memcg1_remove_from_trees(struct mem_cgroup *memcg)
{
	struct mem_cgroup_tree_per_node *mctz;
	struct mem_cgroup_per_node *mz;
	int nid;

	for_each_node(nid) {
		mz = memcg->nodeinfo[nid];
		mctz = soft_limit_tree.rb_tree_per_node[nid];
		if (mctz)
			mem_cgroup_remove_exceeded(mz, mctz);
	}
}

static struct mem_cgroup_per_node *
__mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_node *mctz)
{
	struct mem_cgroup_per_node *mz;

retry:
	mz = NULL;
	if (!mctz->rb_rightmost)
		goto done;		/* Nothing to reclaim from */

	mz = rb_entry(mctz->rb_rightmost,
		      struct mem_cgroup_per_node, tree_node);
	/*
	 * Remove the node now but someone else can add it back,
	 * we will to add it back at the end of reclaim to its correct
	 * position in the tree.
	 */
	__mem_cgroup_remove_exceeded(mz, mctz);
	if (!soft_limit_excess(mz->memcg) ||
	    !css_tryget(&mz->memcg->css))
		goto retry;
done:
	return mz;
}

static struct mem_cgroup_per_node *
mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_node *mctz)
{
	struct mem_cgroup_per_node *mz;

	spin_lock_irq(&mctz->lock);
	mz = __mem_cgroup_largest_soft_limit_node(mctz);
	spin_unlock_irq(&mctz->lock);
	return mz;
}

static int mem_cgroup_soft_reclaim(struct mem_cgroup *root_memcg,
				   pg_data_t *pgdat,
				   gfp_t gfp_mask,
				   unsigned long *total_scanned)
{
	struct mem_cgroup *victim = NULL;
	int total = 0;
	int loop = 0;
	unsigned long excess;
	unsigned long nr_scanned;
	struct mem_cgroup_reclaim_cookie reclaim = {
		.pgdat = pgdat,
	};

	excess = soft_limit_excess(root_memcg);

	while (1) {
		victim = mem_cgroup_iter(root_memcg, victim, &reclaim);
		if (!victim) {
			loop++;
			if (loop >= 2) {
				/*
				 * If we have not been able to reclaim
				 * anything, it might because there are
				 * no reclaimable pages under this hierarchy
				 */
				if (!total)
					break;
				/*
				 * We want to do more targeted reclaim.
				 * excess >> 2 is not to excessive so as to
				 * reclaim too much, nor too less that we keep
				 * coming back to reclaim from this cgroup
				 */
				if (total >= (excess >> 2) ||
					(loop > MEM_CGROUP_MAX_RECLAIM_LOOPS))
					break;
			}
			continue;
		}
		total += mem_cgroup_shrink_node(victim, gfp_mask, false,
					pgdat, &nr_scanned);
		*total_scanned += nr_scanned;
		if (!soft_limit_excess(root_memcg))
			break;
	}
	mem_cgroup_iter_break(root_memcg, victim);
	return total;
}

unsigned long memcg1_soft_limit_reclaim(pg_data_t *pgdat, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	unsigned long nr_reclaimed = 0;
	struct mem_cgroup_per_node *mz, *next_mz = NULL;
	unsigned long reclaimed;
	int loop = 0;
	struct mem_cgroup_tree_per_node *mctz;
	unsigned long excess;

	if (lru_gen_enabled())
		return 0;

	if (order > 0)
		return 0;

	mctz = soft_limit_tree.rb_tree_per_node[pgdat->node_id];

	/*
	 * Do not even bother to check the largest node if the root
	 * is empty. Do it lockless to prevent lock bouncing. Races
	 * are acceptable as soft limit is best effort anyway.
	 */
	if (!mctz || RB_EMPTY_ROOT(&mctz->rb_root))
		return 0;

	/*
	 * This loop can run a while, specially if mem_cgroup's continuously
	 * keep exceeding their soft limit and putting the system under
	 * pressure
	 */
	do {
		if (next_mz)
			mz = next_mz;
		else
			mz = mem_cgroup_largest_soft_limit_node(mctz);
		if (!mz)
			break;

		reclaimed = mem_cgroup_soft_reclaim(mz->memcg, pgdat,
						    gfp_mask, total_scanned);
		nr_reclaimed += reclaimed;
		spin_lock_irq(&mctz->lock);

		/*
		 * If we failed to reclaim anything from this memory cgroup
		 * it is time to move on to the next cgroup
		 */
		next_mz = NULL;
		if (!reclaimed)
			next_mz = __mem_cgroup_largest_soft_limit_node(mctz);

		excess = soft_limit_excess(mz->memcg);
		/*
		 * One school of thought says that we should not add
		 * back the node to the tree if reclaim returns 0.
		 * But our reclaim could return 0, simply because due
		 * to priority we are exposing a smaller subset of
		 * memory to reclaim from. Consider this as a longer
		 * term TODO.
		 */
		/* If excess == 0, no tree ops */
		__mem_cgroup_insert_exceeded(mz, mctz, excess);
		spin_unlock_irq(&mctz->lock);
		css_put(&mz->memcg->css);
		loop++;
		/*
		 * Could not reclaim anything and there are no more
		 * mem cgroups to try or we seem to be looping without
		 * reclaiming anything.
		 */
		if (!nr_reclaimed &&
			(next_mz == NULL ||
			loop > MEM_CGROUP_MAX_SOFT_LIMIT_RECLAIM_LOOPS))
			break;
	} while (!nr_reclaimed);
	if (next_mz)
		css_put(&next_mz->memcg->css);
	return nr_reclaimed;
}

/*
 * A routine for checking "mem" is under move_account() or not.
 *
 * Checking a cgroup is mc.from or mc.to or under hierarchy of
 * moving cgroups. This is for waiting at high-memory pressure
 * caused by "move".
 */
static bool mem_cgroup_under_move(struct mem_cgroup *memcg)
{
	struct mem_cgroup *from;
	struct mem_cgroup *to;
	bool ret = false;
	/*
	 * Unlike task_move routines, we access mc.to, mc.from not under
	 * mutual exclusion by cgroup_mutex. Here, we take spinlock instead.
	 */
	spin_lock(&mc.lock);
	from = mc.from;
	to = mc.to;
	if (!from)
		goto unlock;

	ret = mem_cgroup_is_descendant(from, memcg) ||
		mem_cgroup_is_descendant(to, memcg);
unlock:
	spin_unlock(&mc.lock);
	return ret;
}

bool memcg1_wait_acct_move(struct mem_cgroup *memcg)
{
	if (mc.moving_task && current != mc.moving_task) {
		if (mem_cgroup_under_move(memcg)) {
			DEFINE_WAIT(wait);
			prepare_to_wait(&mc.waitq, &wait, TASK_INTERRUPTIBLE);
			/* moving charge context might have finished. */
			if (mc.moving_task)
				schedule();
			finish_wait(&mc.waitq, &wait);
			return true;
		}
	}
	return false;
}

/**
 * folio_memcg_lock - Bind a folio to its memcg.
 * @folio: The folio.
 *
 * This function prevents unlocked LRU folios from being moved to
 * another cgroup.
 *
 * It ensures lifetime of the bound memcg.  The caller is responsible
 * for the lifetime of the folio.
 */
void folio_memcg_lock(struct folio *folio)
{
	struct mem_cgroup *memcg;
	unsigned long flags;

	/*
	 * The RCU lock is held throughout the transaction.  The fast
	 * path can get away without acquiring the memcg->move_lock
	 * because page moving starts with an RCU grace period.
         */
	rcu_read_lock();

	if (mem_cgroup_disabled())
		return;
again:
	memcg = folio_memcg(folio);
	if (unlikely(!memcg))
		return;

#ifdef CONFIG_PROVE_LOCKING
	local_irq_save(flags);
	might_lock(&memcg->move_lock);
	local_irq_restore(flags);
#endif

	if (atomic_read(&memcg->moving_account) <= 0)
		return;

	spin_lock_irqsave(&memcg->move_lock, flags);
	if (memcg != folio_memcg(folio)) {
		spin_unlock_irqrestore(&memcg->move_lock, flags);
		goto again;
	}

	/*
	 * When charge migration first begins, we can have multiple
	 * critical sections holding the fast-path RCU lock and one
	 * holding the slowpath move_lock. Track the task who has the
	 * move_lock for folio_memcg_unlock().
	 */
	memcg->move_lock_task = current;
	memcg->move_lock_flags = flags;
}

static void __folio_memcg_unlock(struct mem_cgroup *memcg)
{
	if (memcg && memcg->move_lock_task == current) {
		unsigned long flags = memcg->move_lock_flags;

		memcg->move_lock_task = NULL;
		memcg->move_lock_flags = 0;

		spin_unlock_irqrestore(&memcg->move_lock, flags);
	}

	rcu_read_unlock();
}

/**
 * folio_memcg_unlock - Release the binding between a folio and its memcg.
 * @folio: The folio.
 *
 * This releases the binding created by folio_memcg_lock().  This does
 * not change the accounting of this folio to its memcg, but it does
 * permit others to change it.
 */
void folio_memcg_unlock(struct folio *folio)
{
	__folio_memcg_unlock(folio_memcg(folio));
}

#ifdef CONFIG_SWAP
/**
 * mem_cgroup_move_swap_account - move swap charge and swap_cgroup's record.
 * @entry: swap entry to be moved
 * @from:  mem_cgroup which the entry is moved from
 * @to:  mem_cgroup which the entry is moved to
 *
 * It succeeds only when the swap_cgroup's record for this entry is the same
 * as the mem_cgroup's id of @from.
 *
 * Returns 0 on success, -EINVAL on failure.
 *
 * The caller must have charged to @to, IOW, called page_counter_charge() about
 * both res and memsw, and called css_get().
 */
static int mem_cgroup_move_swap_account(swp_entry_t entry,
				struct mem_cgroup *from, struct mem_cgroup *to)
{
	unsigned short old_id, new_id;

	old_id = mem_cgroup_id(from);
	new_id = mem_cgroup_id(to);

	if (swap_cgroup_cmpxchg(entry, old_id, new_id) == old_id) {
		mod_memcg_state(from, MEMCG_SWAP, -1);
		mod_memcg_state(to, MEMCG_SWAP, 1);
		return 0;
	}
	return -EINVAL;
}
#else
static inline int mem_cgroup_move_swap_account(swp_entry_t entry,
				struct mem_cgroup *from, struct mem_cgroup *to)
{
	return -EINVAL;
}
#endif

static u64 mem_cgroup_move_charge_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return mem_cgroup_from_css(css)->move_charge_at_immigrate;
}

#ifdef CONFIG_MMU
static int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	pr_warn_once("Cgroup memory moving (move_charge_at_immigrate) is deprecated. "
		     "Please report your usecase to linux-mm@kvack.org if you "
		     "depend on this functionality.\n");

	if (val & ~MOVE_MASK)
		return -EINVAL;

	/*
	 * No kind of locking is needed in here, because ->can_attach() will
	 * check this value once in the beginning of the process, and then carry
	 * on with stale data. This means that changes to this value will only
	 * affect task migrations starting after the change.
	 */
	memcg->move_charge_at_immigrate = val;
	return 0;
}
#else
static int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val)
{
	return -ENOSYS;
}
#endif

#ifdef CONFIG_MMU
/* Handlers for move charge at task migration. */
static int mem_cgroup_do_precharge(unsigned long count)
{
	int ret;

	/* Try a single bulk charge without reclaim first, kswapd may wake */
	ret = try_charge(mc.to, GFP_KERNEL & ~__GFP_DIRECT_RECLAIM, count);
	if (!ret) {
		mc.precharge += count;
		return ret;
	}

	/* Try charges one by one with reclaim, but do not retry */
	while (count--) {
		ret = try_charge(mc.to, GFP_KERNEL | __GFP_NORETRY, 1);
		if (ret)
			return ret;
		mc.precharge++;
		cond_resched();
	}
	return 0;
}

union mc_target {
	struct folio	*folio;
	swp_entry_t	ent;
};

enum mc_target_type {
	MC_TARGET_NONE = 0,
	MC_TARGET_PAGE,
	MC_TARGET_SWAP,
	MC_TARGET_DEVICE,
};

static struct page *mc_handle_present_pte(struct vm_area_struct *vma,
						unsigned long addr, pte_t ptent)
{
	struct page *page = vm_normal_page(vma, addr, ptent);

	if (!page)
		return NULL;
	if (PageAnon(page)) {
		if (!(mc.flags & MOVE_ANON))
			return NULL;
	} else {
		if (!(mc.flags & MOVE_FILE))
			return NULL;
	}
	get_page(page);

	return page;
}

#if defined(CONFIG_SWAP) || defined(CONFIG_DEVICE_PRIVATE)
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			pte_t ptent, swp_entry_t *entry)
{
	struct page *page = NULL;
	swp_entry_t ent = pte_to_swp_entry(ptent);

	if (!(mc.flags & MOVE_ANON))
		return NULL;

	/*
	 * Handle device private pages that are not accessible by the CPU, but
	 * stored as special swap entries in the page table.
	 */
	if (is_device_private_entry(ent)) {
		page = pfn_swap_entry_to_page(ent);
		if (!get_page_unless_zero(page))
			return NULL;
		return page;
	}

	if (non_swap_entry(ent))
		return NULL;

	/*
	 * Because swap_cache_get_folio() updates some statistics counter,
	 * we call find_get_page() with swapper_space directly.
	 */
	page = find_get_page(swap_address_space(ent), swap_cache_index(ent));
	entry->val = ent.val;

	return page;
}
#else
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			pte_t ptent, swp_entry_t *entry)
{
	return NULL;
}
#endif

static struct page *mc_handle_file_pte(struct vm_area_struct *vma,
			unsigned long addr, pte_t ptent)
{
	unsigned long index;
	struct folio *folio;

	if (!vma->vm_file) /* anonymous vma */
		return NULL;
	if (!(mc.flags & MOVE_FILE))
		return NULL;

	/* folio is moved even if it's not RSS of this task(page-faulted). */
	/* shmem/tmpfs may report page out on swap: account for that too. */
	index = linear_page_index(vma, addr);
	folio = filemap_get_incore_folio(vma->vm_file->f_mapping, index);
	if (IS_ERR(folio))
		return NULL;
	return folio_file_page(folio, index);
}

/**
 * mem_cgroup_move_account - move account of the folio
 * @folio: The folio.
 * @compound: charge the page as compound or small page
 * @from: mem_cgroup which the folio is moved from.
 * @to:	mem_cgroup which the folio is moved to. @from != @to.
 *
 * The folio must be locked and not on the LRU.
 *
 * This function doesn't do "charge" to new cgroup and doesn't do "uncharge"
 * from old cgroup.
 */
static int mem_cgroup_move_account(struct folio *folio,
				   bool compound,
				   struct mem_cgroup *from,
				   struct mem_cgroup *to)
{
	struct lruvec *from_vec, *to_vec;
	struct pglist_data *pgdat;
	unsigned int nr_pages = compound ? folio_nr_pages(folio) : 1;
	int nid, ret;

	VM_BUG_ON(from == to);
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);
	VM_BUG_ON(compound && !folio_test_large(folio));

	ret = -EINVAL;
	if (folio_memcg(folio) != from)
		goto out;

	pgdat = folio_pgdat(folio);
	from_vec = mem_cgroup_lruvec(from, pgdat);
	to_vec = mem_cgroup_lruvec(to, pgdat);

	folio_memcg_lock(folio);

	if (folio_test_anon(folio)) {
		if (folio_mapped(folio)) {
			__mod_lruvec_state(from_vec, NR_ANON_MAPPED, -nr_pages);
			__mod_lruvec_state(to_vec, NR_ANON_MAPPED, nr_pages);
			if (folio_test_pmd_mappable(folio)) {
				__mod_lruvec_state(from_vec, NR_ANON_THPS,
						   -nr_pages);
				__mod_lruvec_state(to_vec, NR_ANON_THPS,
						   nr_pages);
			}
		}
	} else {
		__mod_lruvec_state(from_vec, NR_FILE_PAGES, -nr_pages);
		__mod_lruvec_state(to_vec, NR_FILE_PAGES, nr_pages);

		if (folio_test_swapbacked(folio)) {
			__mod_lruvec_state(from_vec, NR_SHMEM, -nr_pages);
			__mod_lruvec_state(to_vec, NR_SHMEM, nr_pages);
		}

		if (folio_mapped(folio)) {
			__mod_lruvec_state(from_vec, NR_FILE_MAPPED, -nr_pages);
			__mod_lruvec_state(to_vec, NR_FILE_MAPPED, nr_pages);
		}

		if (folio_test_dirty(folio)) {
			struct address_space *mapping = folio_mapping(folio);

			if (mapping_can_writeback(mapping)) {
				__mod_lruvec_state(from_vec, NR_FILE_DIRTY,
						   -nr_pages);
				__mod_lruvec_state(to_vec, NR_FILE_DIRTY,
						   nr_pages);
			}
		}
	}

#ifdef CONFIG_SWAP
	if (folio_test_swapcache(folio)) {
		__mod_lruvec_state(from_vec, NR_SWAPCACHE, -nr_pages);
		__mod_lruvec_state(to_vec, NR_SWAPCACHE, nr_pages);
	}
#endif
	if (folio_test_writeback(folio)) {
		__mod_lruvec_state(from_vec, NR_WRITEBACK, -nr_pages);
		__mod_lruvec_state(to_vec, NR_WRITEBACK, nr_pages);
	}

	/*
	 * All state has been migrated, let's switch to the new memcg.
	 *
	 * It is safe to change page's memcg here because the page
	 * is referenced, charged, isolated, and locked: we can't race
	 * with (un)charging, migration, LRU putback, or anything else
	 * that would rely on a stable page's memory cgroup.
	 *
	 * Note that folio_memcg_lock is a memcg lock, not a page lock,
	 * to save space. As soon as we switch page's memory cgroup to a
	 * new memcg that isn't locked, the above state can change
	 * concurrently again. Make sure we're truly done with it.
	 */
	smp_mb();

	css_get(&to->css);
	css_put(&from->css);

	folio->memcg_data = (unsigned long)to;

	__folio_memcg_unlock(from);

	ret = 0;
	nid = folio_nid(folio);

	local_irq_disable();
	mem_cgroup_charge_statistics(to, nr_pages);
	memcg1_check_events(to, nid);
	mem_cgroup_charge_statistics(from, -nr_pages);
	memcg1_check_events(from, nid);
	local_irq_enable();
out:
	return ret;
}

/**
 * get_mctgt_type - get target type of moving charge
 * @vma: the vma the pte to be checked belongs
 * @addr: the address corresponding to the pte to be checked
 * @ptent: the pte to be checked
 * @target: the pointer the target page or swap ent will be stored(can be NULL)
 *
 * Context: Called with pte lock held.
 * Return:
 * * MC_TARGET_NONE - If the pte is not a target for move charge.
 * * MC_TARGET_PAGE - If the page corresponding to this pte is a target for
 *   move charge. If @target is not NULL, the folio is stored in target->folio
 *   with extra refcnt taken (Caller should release it).
 * * MC_TARGET_SWAP - If the swap entry corresponding to this pte is a
 *   target for charge migration.  If @target is not NULL, the entry is
 *   stored in target->ent.
 * * MC_TARGET_DEVICE - Like MC_TARGET_PAGE but page is device memory and
 *   thus not on the lru.  For now such page is charged like a regular page
 *   would be as it is just special memory taking the place of a regular page.
 *   See Documentations/vm/hmm.txt and include/linux/hmm.h
 */
static enum mc_target_type get_mctgt_type(struct vm_area_struct *vma,
		unsigned long addr, pte_t ptent, union mc_target *target)
{
	struct page *page = NULL;
	struct folio *folio;
	enum mc_target_type ret = MC_TARGET_NONE;
	swp_entry_t ent = { .val = 0 };

	if (pte_present(ptent))
		page = mc_handle_present_pte(vma, addr, ptent);
	else if (pte_none_mostly(ptent))
		/*
		 * PTE markers should be treated as a none pte here, separated
		 * from other swap handling below.
		 */
		page = mc_handle_file_pte(vma, addr, ptent);
	else if (is_swap_pte(ptent))
		page = mc_handle_swap_pte(vma, ptent, &ent);

	if (page)
		folio = page_folio(page);
	if (target && page) {
		if (!folio_trylock(folio)) {
			folio_put(folio);
			return ret;
		}
		/*
		 * page_mapped() must be stable during the move. This
		 * pte is locked, so if it's present, the page cannot
		 * become unmapped. If it isn't, we have only partial
		 * control over the mapped state: the page lock will
		 * prevent new faults against pagecache and swapcache,
		 * so an unmapped page cannot become mapped. However,
		 * if the page is already mapped elsewhere, it can
		 * unmap, and there is nothing we can do about it.
		 * Alas, skip moving the page in this case.
		 */
		if (!pte_present(ptent) && page_mapped(page)) {
			folio_unlock(folio);
			folio_put(folio);
			return ret;
		}
	}

	if (!page && !ent.val)
		return ret;
	if (page) {
		/*
		 * Do only loose check w/o serialization.
		 * mem_cgroup_move_account() checks the page is valid or
		 * not under LRU exclusion.
		 */
		if (folio_memcg(folio) == mc.from) {
			ret = MC_TARGET_PAGE;
			if (folio_is_device_private(folio) ||
			    folio_is_device_coherent(folio))
				ret = MC_TARGET_DEVICE;
			if (target)
				target->folio = folio;
		}
		if (!ret || !target) {
			if (target)
				folio_unlock(folio);
			folio_put(folio);
		}
	}
	/*
	 * There is a swap entry and a page doesn't exist or isn't charged.
	 * But we cannot move a tail-page in a THP.
	 */
	if (ent.val && !ret && (!page || !PageTransCompound(page)) &&
	    mem_cgroup_id(mc.from) == lookup_swap_cgroup_id(ent)) {
		ret = MC_TARGET_SWAP;
		if (target)
			target->ent = ent;
	}
	return ret;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * We don't consider PMD mapped swapping or file mapped pages because THP does
 * not support them for now.
 * Caller should make sure that pmd_trans_huge(pmd) is true.
 */
static enum mc_target_type get_mctgt_type_thp(struct vm_area_struct *vma,
		unsigned long addr, pmd_t pmd, union mc_target *target)
{
	struct page *page = NULL;
	struct folio *folio;
	enum mc_target_type ret = MC_TARGET_NONE;

	if (unlikely(is_swap_pmd(pmd))) {
		VM_BUG_ON(thp_migration_supported() &&
				  !is_pmd_migration_entry(pmd));
		return ret;
	}
	page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!page || !PageHead(page), page);
	folio = page_folio(page);
	if (!(mc.flags & MOVE_ANON))
		return ret;
	if (folio_memcg(folio) == mc.from) {
		ret = MC_TARGET_PAGE;
		if (target) {
			folio_get(folio);
			if (!folio_trylock(folio)) {
				folio_put(folio);
				return MC_TARGET_NONE;
			}
			target->folio = folio;
		}
	}
	return ret;
}
#else
static inline enum mc_target_type get_mctgt_type_thp(struct vm_area_struct *vma,
		unsigned long addr, pmd_t pmd, union mc_target *target)
{
	return MC_TARGET_NONE;
}
#endif

static int mem_cgroup_count_precharge_pte_range(pmd_t *pmd,
					unsigned long addr, unsigned long end,
					struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		/*
		 * Note their can not be MC_TARGET_DEVICE for now as we do not
		 * support transparent huge page with MEMORY_DEVICE_PRIVATE but
		 * this might change.
		 */
		if (get_mctgt_type_thp(vma, addr, *pmd, NULL) == MC_TARGET_PAGE)
			mc.precharge += HPAGE_PMD_NR;
		spin_unlock(ptl);
		return 0;
	}

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte)
		return 0;
	for (; addr != end; pte++, addr += PAGE_SIZE)
		if (get_mctgt_type(vma, addr, ptep_get(pte), NULL))
			mc.precharge++;	/* increment precharge temporarily */
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();

	return 0;
}

static const struct mm_walk_ops precharge_walk_ops = {
	.pmd_entry	= mem_cgroup_count_precharge_pte_range,
	.walk_lock	= PGWALK_RDLOCK,
};

static unsigned long mem_cgroup_count_precharge(struct mm_struct *mm)
{
	unsigned long precharge;

	mmap_read_lock(mm);
	walk_page_range(mm, 0, ULONG_MAX, &precharge_walk_ops, NULL);
	mmap_read_unlock(mm);

	precharge = mc.precharge;
	mc.precharge = 0;

	return precharge;
}

static int mem_cgroup_precharge_mc(struct mm_struct *mm)
{
	unsigned long precharge = mem_cgroup_count_precharge(mm);

	VM_BUG_ON(mc.moving_task);
	mc.moving_task = current;
	return mem_cgroup_do_precharge(precharge);
}

/* cancels all extra charges on mc.from and mc.to, and wakes up all waiters. */
static void __mem_cgroup_clear_mc(void)
{
	struct mem_cgroup *from = mc.from;
	struct mem_cgroup *to = mc.to;

	/* we must uncharge all the leftover precharges from mc.to */
	if (mc.precharge) {
		mem_cgroup_cancel_charge(mc.to, mc.precharge);
		mc.precharge = 0;
	}
	/*
	 * we didn't uncharge from mc.from at mem_cgroup_move_account(), so
	 * we must uncharge here.
	 */
	if (mc.moved_charge) {
		mem_cgroup_cancel_charge(mc.from, mc.moved_charge);
		mc.moved_charge = 0;
	}
	/* we must fixup refcnts and charges */
	if (mc.moved_swap) {
		/* uncharge swap account from the old cgroup */
		if (!mem_cgroup_is_root(mc.from))
			page_counter_uncharge(&mc.from->memsw, mc.moved_swap);

		mem_cgroup_id_put_many(mc.from, mc.moved_swap);

		/*
		 * we charged both to->memory and to->memsw, so we
		 * should uncharge to->memory.
		 */
		if (!mem_cgroup_is_root(mc.to))
			page_counter_uncharge(&mc.to->memory, mc.moved_swap);

		mc.moved_swap = 0;
	}
	memcg1_oom_recover(from);
	memcg1_oom_recover(to);
	wake_up_all(&mc.waitq);
}

static void mem_cgroup_clear_mc(void)
{
	struct mm_struct *mm = mc.mm;

	/*
	 * we must clear moving_task before waking up waiters at the end of
	 * task migration.
	 */
	mc.moving_task = NULL;
	__mem_cgroup_clear_mc();
	spin_lock(&mc.lock);
	mc.from = NULL;
	mc.to = NULL;
	mc.mm = NULL;
	spin_unlock(&mc.lock);

	mmput(mm);
}

int memcg1_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct mem_cgroup *memcg = NULL; /* unneeded init to make gcc happy */
	struct mem_cgroup *from;
	struct task_struct *leader, *p;
	struct mm_struct *mm;
	unsigned long move_flags;
	int ret = 0;

	/* charge immigration isn't supported on the default hierarchy */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return 0;

	/*
	 * Multi-process migrations only happen on the default hierarchy
	 * where charge immigration is not used.  Perform charge
	 * immigration if @tset contains a leader and whine if there are
	 * multiple.
	 */
	p = NULL;
	cgroup_taskset_for_each_leader(leader, css, tset) {
		WARN_ON_ONCE(p);
		p = leader;
		memcg = mem_cgroup_from_css(css);
	}
	if (!p)
		return 0;

	/*
	 * We are now committed to this value whatever it is. Changes in this
	 * tunable will only affect upcoming migrations, not the current one.
	 * So we need to save it, and keep it going.
	 */
	move_flags = READ_ONCE(memcg->move_charge_at_immigrate);
	if (!move_flags)
		return 0;

	from = mem_cgroup_from_task(p);

	VM_BUG_ON(from == memcg);

	mm = get_task_mm(p);
	if (!mm)
		return 0;
	/* We move charges only when we move a owner of the mm */
	if (mm->owner == p) {
		VM_BUG_ON(mc.from);
		VM_BUG_ON(mc.to);
		VM_BUG_ON(mc.precharge);
		VM_BUG_ON(mc.moved_charge);
		VM_BUG_ON(mc.moved_swap);

		spin_lock(&mc.lock);
		mc.mm = mm;
		mc.from = from;
		mc.to = memcg;
		mc.flags = move_flags;
		spin_unlock(&mc.lock);
		/* We set mc.moving_task later */

		ret = mem_cgroup_precharge_mc(mm);
		if (ret)
			mem_cgroup_clear_mc();
	} else {
		mmput(mm);
	}
	return ret;
}

void memcg1_cancel_attach(struct cgroup_taskset *tset)
{
	if (mc.to)
		mem_cgroup_clear_mc();
}

static int mem_cgroup_move_charge_pte_range(pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mm_walk *walk)
{
	int ret = 0;
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;
	enum mc_target_type target_type;
	union mc_target target;
	struct folio *folio;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		if (mc.precharge < HPAGE_PMD_NR) {
			spin_unlock(ptl);
			return 0;
		}
		target_type = get_mctgt_type_thp(vma, addr, *pmd, &target);
		if (target_type == MC_TARGET_PAGE) {
			folio = target.folio;
			if (folio_isolate_lru(folio)) {
				if (!mem_cgroup_move_account(folio, true,
							     mc.from, mc.to)) {
					mc.precharge -= HPAGE_PMD_NR;
					mc.moved_charge += HPAGE_PMD_NR;
				}
				folio_putback_lru(folio);
			}
			folio_unlock(folio);
			folio_put(folio);
		} else if (target_type == MC_TARGET_DEVICE) {
			folio = target.folio;
			if (!mem_cgroup_move_account(folio, true,
						     mc.from, mc.to)) {
				mc.precharge -= HPAGE_PMD_NR;
				mc.moved_charge += HPAGE_PMD_NR;
			}
			folio_unlock(folio);
			folio_put(folio);
		}
		spin_unlock(ptl);
		return 0;
	}

retry:
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte)
		return 0;
	for (; addr != end; addr += PAGE_SIZE) {
		pte_t ptent = ptep_get(pte++);
		bool device = false;
		swp_entry_t ent;

		if (!mc.precharge)
			break;

		switch (get_mctgt_type(vma, addr, ptent, &target)) {
		case MC_TARGET_DEVICE:
			device = true;
			fallthrough;
		case MC_TARGET_PAGE:
			folio = target.folio;
			/*
			 * We can have a part of the split pmd here. Moving it
			 * can be done but it would be too convoluted so simply
			 * ignore such a partial THP and keep it in original
			 * memcg. There should be somebody mapping the head.
			 */
			if (folio_test_large(folio))
				goto put;
			if (!device && !folio_isolate_lru(folio))
				goto put;
			if (!mem_cgroup_move_account(folio, false,
						mc.from, mc.to)) {
				mc.precharge--;
				/* we uncharge from mc.from later. */
				mc.moved_charge++;
			}
			if (!device)
				folio_putback_lru(folio);
put:			/* get_mctgt_type() gets & locks the page */
			folio_unlock(folio);
			folio_put(folio);
			break;
		case MC_TARGET_SWAP:
			ent = target.ent;
			if (!mem_cgroup_move_swap_account(ent, mc.from, mc.to)) {
				mc.precharge--;
				mem_cgroup_id_get_many(mc.to, 1);
				/* we fixup other refcnts and charges later. */
				mc.moved_swap++;
			}
			break;
		default:
			break;
		}
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();

	if (addr != end) {
		/*
		 * We have consumed all precharges we got in can_attach().
		 * We try charge one by one, but don't do any additional
		 * charges to mc.to if we have failed in charge once in attach()
		 * phase.
		 */
		ret = mem_cgroup_do_precharge(1);
		if (!ret)
			goto retry;
	}

	return ret;
}

static const struct mm_walk_ops charge_walk_ops = {
	.pmd_entry	= mem_cgroup_move_charge_pte_range,
	.walk_lock	= PGWALK_RDLOCK,
};

static void mem_cgroup_move_charge(void)
{
	lru_add_drain_all();
	/*
	 * Signal folio_memcg_lock() to take the memcg's move_lock
	 * while we're moving its pages to another memcg. Then wait
	 * for already started RCU-only updates to finish.
	 */
	atomic_inc(&mc.from->moving_account);
	synchronize_rcu();
retry:
	if (unlikely(!mmap_read_trylock(mc.mm))) {
		/*
		 * Someone who are holding the mmap_lock might be waiting in
		 * waitq. So we cancel all extra charges, wake up all waiters,
		 * and retry. Because we cancel precharges, we might not be able
		 * to move enough charges, but moving charge is a best-effort
		 * feature anyway, so it wouldn't be a big problem.
		 */
		__mem_cgroup_clear_mc();
		cond_resched();
		goto retry;
	}
	/*
	 * When we have consumed all precharges and failed in doing
	 * additional charge, the page walk just aborts.
	 */
	walk_page_range(mc.mm, 0, ULONG_MAX, &charge_walk_ops, NULL);
	mmap_read_unlock(mc.mm);
	atomic_dec(&mc.from->moving_account);
}

void memcg1_move_task(void)
{
	if (mc.to) {
		mem_cgroup_move_charge();
		mem_cgroup_clear_mc();
	}
}

#else	/* !CONFIG_MMU */
int memcg1_can_attach(struct cgroup_taskset *tset)
{
	return 0;
}
void memcg1_cancel_attach(struct cgroup_taskset *tset)
{
}
void memcg1_move_task(void)
{
}
#endif

static void __mem_cgroup_threshold(struct mem_cgroup *memcg, bool swap)
{
	struct mem_cgroup_threshold_ary *t;
	unsigned long usage;
	int i;

	rcu_read_lock();
	if (!swap)
		t = rcu_dereference(memcg->thresholds.primary);
	else
		t = rcu_dereference(memcg->memsw_thresholds.primary);

	if (!t)
		goto unlock;

	usage = mem_cgroup_usage(memcg, swap);

	/*
	 * current_threshold points to threshold just below or equal to usage.
	 * If it's not true, a threshold was crossed after last
	 * call of __mem_cgroup_threshold().
	 */
	i = t->current_threshold;

	/*
	 * Iterate backward over array of thresholds starting from
	 * current_threshold and check if a threshold is crossed.
	 * If none of thresholds below usage is crossed, we read
	 * only one element of the array here.
	 */
	for (; i >= 0 && unlikely(t->entries[i].threshold > usage); i--)
		eventfd_signal(t->entries[i].eventfd);

	/* i = current_threshold + 1 */
	i++;

	/*
	 * Iterate forward over array of thresholds starting from
	 * current_threshold+1 and check if a threshold is crossed.
	 * If none of thresholds above usage is crossed, we read
	 * only one element of the array here.
	 */
	for (; i < t->size && unlikely(t->entries[i].threshold <= usage); i++)
		eventfd_signal(t->entries[i].eventfd);

	/* Update current_threshold */
	t->current_threshold = i - 1;
unlock:
	rcu_read_unlock();
}

static void mem_cgroup_threshold(struct mem_cgroup *memcg)
{
	while (memcg) {
		__mem_cgroup_threshold(memcg, false);
		if (do_memsw_account())
			__mem_cgroup_threshold(memcg, true);

		memcg = parent_mem_cgroup(memcg);
	}
}

/*
 * Check events in order.
 *
 */
void memcg1_check_events(struct mem_cgroup *memcg, int nid)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		return;

	/* threshold event is triggered in finer grain than soft limit */
	if (unlikely(mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_THRESH))) {
		bool do_softlimit;

		do_softlimit = mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_SOFTLIMIT);
		mem_cgroup_threshold(memcg);
		if (unlikely(do_softlimit))
			memcg1_update_tree(memcg, nid);
	}
}

static int compare_thresholds(const void *a, const void *b)
{
	const struct mem_cgroup_threshold *_a = a;
	const struct mem_cgroup_threshold *_b = b;

	if (_a->threshold > _b->threshold)
		return 1;

	if (_a->threshold < _b->threshold)
		return -1;

	return 0;
}

static int mem_cgroup_oom_notify_cb(struct mem_cgroup *memcg)
{
	struct mem_cgroup_eventfd_list *ev;

	spin_lock(&memcg_oom_lock);

	list_for_each_entry(ev, &memcg->oom_notify, list)
		eventfd_signal(ev->eventfd);

	spin_unlock(&memcg_oom_lock);
	return 0;
}

static void mem_cgroup_oom_notify(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	for_each_mem_cgroup_tree(iter, memcg)
		mem_cgroup_oom_notify_cb(iter);
}

static int __mem_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args, enum res_type type)
{
	struct mem_cgroup_thresholds *thresholds;
	struct mem_cgroup_threshold_ary *new;
	unsigned long threshold;
	unsigned long usage;
	int i, size, ret;

	ret = page_counter_memparse(args, "-1", &threshold);
	if (ret)
		return ret;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = mem_cgroup_usage(memcg, false);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = mem_cgroup_usage(memcg, true);
	} else
		BUG();

	/* Check if a threshold crossed before adding a new one */
	if (thresholds->primary)
		__mem_cgroup_threshold(memcg, type == _MEMSWAP);

	size = thresholds->primary ? thresholds->primary->size + 1 : 1;

	/* Allocate memory for new array of thresholds */
	new = kmalloc(struct_size(new, entries, size), GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto unlock;
	}
	new->size = size;

	/* Copy thresholds (if any) to new array */
	if (thresholds->primary)
		memcpy(new->entries, thresholds->primary->entries,
		       flex_array_size(new, entries, size - 1));

	/* Add new threshold */
	new->entries[size - 1].eventfd = eventfd;
	new->entries[size - 1].threshold = threshold;

	/* Sort thresholds. Registering of new threshold isn't time-critical */
	sort(new->entries, size, sizeof(*new->entries),
			compare_thresholds, NULL);

	/* Find current threshold */
	new->current_threshold = -1;
	for (i = 0; i < size; i++) {
		if (new->entries[i].threshold <= usage) {
			/*
			 * new->current_threshold will not be used until
			 * rcu_assign_pointer(), so it's safe to increment
			 * it here.
			 */
			++new->current_threshold;
		} else
			break;
	}

	/* Free old spare buffer and save old primary buffer as spare */
	kfree(thresholds->spare);
	thresholds->spare = thresholds->primary;

	rcu_assign_pointer(thresholds->primary, new);

	/* To be sure that nobody uses thresholds */
	synchronize_rcu();

unlock:
	mutex_unlock(&memcg->thresholds_lock);

	return ret;
}

static int mem_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	return __mem_cgroup_usage_register_event(memcg, eventfd, args, _MEM);
}

static int memsw_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	return __mem_cgroup_usage_register_event(memcg, eventfd, args, _MEMSWAP);
}

static void __mem_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, enum res_type type)
{
	struct mem_cgroup_thresholds *thresholds;
	struct mem_cgroup_threshold_ary *new;
	unsigned long usage;
	int i, j, size, entries;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = mem_cgroup_usage(memcg, false);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = mem_cgroup_usage(memcg, true);
	} else
		BUG();

	if (!thresholds->primary)
		goto unlock;

	/* Check if a threshold crossed before removing */
	__mem_cgroup_threshold(memcg, type == _MEMSWAP);

	/* Calculate new number of threshold */
	size = entries = 0;
	for (i = 0; i < thresholds->primary->size; i++) {
		if (thresholds->primary->entries[i].eventfd != eventfd)
			size++;
		else
			entries++;
	}

	new = thresholds->spare;

	/* If no items related to eventfd have been cleared, nothing to do */
	if (!entries)
		goto unlock;

	/* Set thresholds array to NULL if we don't have thresholds */
	if (!size) {
		kfree(new);
		new = NULL;
		goto swap_buffers;
	}

	new->size = size;

	/* Copy thresholds and find current threshold */
	new->current_threshold = -1;
	for (i = 0, j = 0; i < thresholds->primary->size; i++) {
		if (thresholds->primary->entries[i].eventfd == eventfd)
			continue;

		new->entries[j] = thresholds->primary->entries[i];
		if (new->entries[j].threshold <= usage) {
			/*
			 * new->current_threshold will not be used
			 * until rcu_assign_pointer(), so it's safe to increment
			 * it here.
			 */
			++new->current_threshold;
		}
		j++;
	}

swap_buffers:
	/* Swap primary and spare array */
	thresholds->spare = thresholds->primary;

	rcu_assign_pointer(thresholds->primary, new);

	/* To be sure that nobody uses thresholds */
	synchronize_rcu();

	/* If all events are unregistered, free the spare array */
	if (!new) {
		kfree(thresholds->spare);
		thresholds->spare = NULL;
	}
unlock:
	mutex_unlock(&memcg->thresholds_lock);
}

static void mem_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	return __mem_cgroup_usage_unregister_event(memcg, eventfd, _MEM);
}

static void memsw_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	return __mem_cgroup_usage_unregister_event(memcg, eventfd, _MEMSWAP);
}

static int mem_cgroup_oom_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	struct mem_cgroup_eventfd_list *event;

	event = kmalloc(sizeof(*event),	GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	spin_lock(&memcg_oom_lock);

	event->eventfd = eventfd;
	list_add(&event->list, &memcg->oom_notify);

	/* already in OOM ? */
	if (memcg->under_oom)
		eventfd_signal(eventfd);
	spin_unlock(&memcg_oom_lock);

	return 0;
}

static void mem_cgroup_oom_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	struct mem_cgroup_eventfd_list *ev, *tmp;

	spin_lock(&memcg_oom_lock);

	list_for_each_entry_safe(ev, tmp, &memcg->oom_notify, list) {
		if (ev->eventfd == eventfd) {
			list_del(&ev->list);
			kfree(ev);
		}
	}

	spin_unlock(&memcg_oom_lock);
}

/*
 * DO NOT USE IN NEW FILES.
 *
 * "cgroup.event_control" implementation.
 *
 * This is way over-engineered.  It tries to support fully configurable
 * events for each user.  Such level of flexibility is completely
 * unnecessary especially in the light of the planned unified hierarchy.
 *
 * Please deprecate this and replace with something simpler if at all
 * possible.
 */

/*
 * Unregister event and free resources.
 *
 * Gets called from workqueue.
 */
static void memcg_event_remove(struct work_struct *work)
{
	struct mem_cgroup_event *event =
		container_of(work, struct mem_cgroup_event, remove);
	struct mem_cgroup *memcg = event->memcg;

	remove_wait_queue(event->wqh, &event->wait);

	event->unregister_event(memcg, event->eventfd);

	/* Notify userspace the event is going away. */
	eventfd_signal(event->eventfd);

	eventfd_ctx_put(event->eventfd);
	kfree(event);
	css_put(&memcg->css);
}

/*
 * Gets called on EPOLLHUP on eventfd when user closes it.
 *
 * Called with wqh->lock held and interrupts disabled.
 */
static int memcg_event_wake(wait_queue_entry_t *wait, unsigned mode,
			    int sync, void *key)
{
	struct mem_cgroup_event *event =
		container_of(wait, struct mem_cgroup_event, wait);
	struct mem_cgroup *memcg = event->memcg;
	__poll_t flags = key_to_poll(key);

	if (flags & EPOLLHUP) {
		/*
		 * If the event has been detached at cgroup removal, we
		 * can simply return knowing the other side will cleanup
		 * for us.
		 *
		 * We can't race against event freeing since the other
		 * side will require wqh->lock via remove_wait_queue(),
		 * which we hold.
		 */
		spin_lock(&memcg->event_list_lock);
		if (!list_empty(&event->list)) {
			list_del_init(&event->list);
			/*
			 * We are in atomic context, but cgroup_event_remove()
			 * may sleep, so we have to call it in workqueue.
			 */
			schedule_work(&event->remove);
		}
		spin_unlock(&memcg->event_list_lock);
	}

	return 0;
}

static void memcg_event_ptable_queue_proc(struct file *file,
		wait_queue_head_t *wqh, poll_table *pt)
{
	struct mem_cgroup_event *event =
		container_of(pt, struct mem_cgroup_event, pt);

	event->wqh = wqh;
	add_wait_queue(wqh, &event->wait);
}

/*
 * DO NOT USE IN NEW FILES.
 *
 * Parse input and register new cgroup event handler.
 *
 * Input must be in format '<event_fd> <control_fd> <args>'.
 * Interpretation of args is defined by control file implementation.
 */
static ssize_t memcg_write_event_control(struct kernfs_open_file *of,
					 char *buf, size_t nbytes, loff_t off)
{
	struct cgroup_subsys_state *css = of_css(of);
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup_event *event;
	struct cgroup_subsys_state *cfile_css;
	unsigned int efd, cfd;
	struct fd efile;
	struct fd cfile;
	struct dentry *cdentry;
	const char *name;
	char *endp;
	int ret;

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		return -EOPNOTSUPP;

	buf = strstrip(buf);

	efd = simple_strtoul(buf, &endp, 10);
	if (*endp != ' ')
		return -EINVAL;
	buf = endp + 1;

	cfd = simple_strtoul(buf, &endp, 10);
	if (*endp == '\0')
		buf = endp;
	else if (*endp == ' ')
		buf = endp + 1;
	else
		return -EINVAL;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->memcg = memcg;
	INIT_LIST_HEAD(&event->list);
	init_poll_funcptr(&event->pt, memcg_event_ptable_queue_proc);
	init_waitqueue_func_entry(&event->wait, memcg_event_wake);
	INIT_WORK(&event->remove, memcg_event_remove);

	efile = fdget(efd);
	if (!efile.file) {
		ret = -EBADF;
		goto out_kfree;
	}

	event->eventfd = eventfd_ctx_fileget(efile.file);
	if (IS_ERR(event->eventfd)) {
		ret = PTR_ERR(event->eventfd);
		goto out_put_efile;
	}

	cfile = fdget(cfd);
	if (!cfile.file) {
		ret = -EBADF;
		goto out_put_eventfd;
	}

	/* the process need read permission on control file */
	/* AV: shouldn't we check that it's been opened for read instead? */
	ret = file_permission(cfile.file, MAY_READ);
	if (ret < 0)
		goto out_put_cfile;

	/*
	 * The control file must be a regular cgroup1 file. As a regular cgroup
	 * file can't be renamed, it's safe to access its name afterwards.
	 */
	cdentry = cfile.file->f_path.dentry;
	if (cdentry->d_sb->s_type != &cgroup_fs_type || !d_is_reg(cdentry)) {
		ret = -EINVAL;
		goto out_put_cfile;
	}

	/*
	 * Determine the event callbacks and set them in @event.  This used
	 * to be done via struct cftype but cgroup core no longer knows
	 * about these events.  The following is crude but the whole thing
	 * is for compatibility anyway.
	 *
	 * DO NOT ADD NEW FILES.
	 */
	name = cdentry->d_name.name;

	if (!strcmp(name, "memory.usage_in_bytes")) {
		event->register_event = mem_cgroup_usage_register_event;
		event->unregister_event = mem_cgroup_usage_unregister_event;
	} else if (!strcmp(name, "memory.oom_control")) {
		event->register_event = mem_cgroup_oom_register_event;
		event->unregister_event = mem_cgroup_oom_unregister_event;
	} else if (!strcmp(name, "memory.pressure_level")) {
		event->register_event = vmpressure_register_event;
		event->unregister_event = vmpressure_unregister_event;
	} else if (!strcmp(name, "memory.memsw.usage_in_bytes")) {
		event->register_event = memsw_cgroup_usage_register_event;
		event->unregister_event = memsw_cgroup_usage_unregister_event;
	} else {
		ret = -EINVAL;
		goto out_put_cfile;
	}

	/*
	 * Verify @cfile should belong to @css.  Also, remaining events are
	 * automatically removed on cgroup destruction but the removal is
	 * asynchronous, so take an extra ref on @css.
	 */
	cfile_css = css_tryget_online_from_dir(cdentry->d_parent,
					       &memory_cgrp_subsys);
	ret = -EINVAL;
	if (IS_ERR(cfile_css))
		goto out_put_cfile;
	if (cfile_css != css) {
		css_put(cfile_css);
		goto out_put_cfile;
	}

	ret = event->register_event(memcg, event->eventfd, buf);
	if (ret)
		goto out_put_css;

	vfs_poll(efile.file, &event->pt);

	spin_lock_irq(&memcg->event_list_lock);
	list_add(&event->list, &memcg->event_list);
	spin_unlock_irq(&memcg->event_list_lock);

	fdput(cfile);
	fdput(efile);

	return nbytes;

out_put_css:
	css_put(css);
out_put_cfile:
	fdput(cfile);
out_put_eventfd:
	eventfd_ctx_put(event->eventfd);
out_put_efile:
	fdput(efile);
out_kfree:
	kfree(event);

	return ret;
}

void memcg1_memcg_init(struct mem_cgroup *memcg)
{
	INIT_LIST_HEAD(&memcg->oom_notify);
	mutex_init(&memcg->thresholds_lock);
	spin_lock_init(&memcg->move_lock);
	INIT_LIST_HEAD(&memcg->event_list);
	spin_lock_init(&memcg->event_list_lock);
}

void memcg1_css_offline(struct mem_cgroup *memcg)
{
	struct mem_cgroup_event *event, *tmp;

	/*
	 * Unregister events and notify userspace.
	 * Notify userspace about cgroup removing only after rmdir of cgroup
	 * directory to avoid race between userspace and kernelspace.
	 */
	spin_lock_irq(&memcg->event_list_lock);
	list_for_each_entry_safe(event, tmp, &memcg->event_list, list) {
		list_del_init(&event->list);
		schedule_work(&event->remove);
	}
	spin_unlock_irq(&memcg->event_list_lock);
}

/*
 * Check OOM-Killer is already running under our hierarchy.
 * If someone is running, return false.
 */
static bool mem_cgroup_oom_trylock(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter, *failed = NULL;

	spin_lock(&memcg_oom_lock);

	for_each_mem_cgroup_tree(iter, memcg) {
		if (iter->oom_lock) {
			/*
			 * this subtree of our hierarchy is already locked
			 * so we cannot give a lock.
			 */
			failed = iter;
			mem_cgroup_iter_break(memcg, iter);
			break;
		} else
			iter->oom_lock = true;
	}

	if (failed) {
		/*
		 * OK, we failed to lock the whole subtree so we have
		 * to clean up what we set up to the failing subtree
		 */
		for_each_mem_cgroup_tree(iter, memcg) {
			if (iter == failed) {
				mem_cgroup_iter_break(memcg, iter);
				break;
			}
			iter->oom_lock = false;
		}
	} else
		mutex_acquire(&memcg_oom_lock_dep_map, 0, 1, _RET_IP_);

	spin_unlock(&memcg_oom_lock);

	return !failed;
}

static void mem_cgroup_oom_unlock(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	spin_lock(&memcg_oom_lock);
	mutex_release(&memcg_oom_lock_dep_map, _RET_IP_);
	for_each_mem_cgroup_tree(iter, memcg)
		iter->oom_lock = false;
	spin_unlock(&memcg_oom_lock);
}

static void mem_cgroup_mark_under_oom(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	spin_lock(&memcg_oom_lock);
	for_each_mem_cgroup_tree(iter, memcg)
		iter->under_oom++;
	spin_unlock(&memcg_oom_lock);
}

static void mem_cgroup_unmark_under_oom(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	/*
	 * Be careful about under_oom underflows because a child memcg
	 * could have been added after mem_cgroup_mark_under_oom.
	 */
	spin_lock(&memcg_oom_lock);
	for_each_mem_cgroup_tree(iter, memcg)
		if (iter->under_oom > 0)
			iter->under_oom--;
	spin_unlock(&memcg_oom_lock);
}

static DECLARE_WAIT_QUEUE_HEAD(memcg_oom_waitq);

struct oom_wait_info {
	struct mem_cgroup *memcg;
	wait_queue_entry_t	wait;
};

static int memcg_oom_wake_function(wait_queue_entry_t *wait,
	unsigned mode, int sync, void *arg)
{
	struct mem_cgroup *wake_memcg = (struct mem_cgroup *)arg;
	struct mem_cgroup *oom_wait_memcg;
	struct oom_wait_info *oom_wait_info;

	oom_wait_info = container_of(wait, struct oom_wait_info, wait);
	oom_wait_memcg = oom_wait_info->memcg;

	if (!mem_cgroup_is_descendant(wake_memcg, oom_wait_memcg) &&
	    !mem_cgroup_is_descendant(oom_wait_memcg, wake_memcg))
		return 0;
	return autoremove_wake_function(wait, mode, sync, arg);
}

void memcg1_oom_recover(struct mem_cgroup *memcg)
{
	/*
	 * For the following lockless ->under_oom test, the only required
	 * guarantee is that it must see the state asserted by an OOM when
	 * this function is called as a result of userland actions
	 * triggered by the notification of the OOM.  This is trivially
	 * achieved by invoking mem_cgroup_mark_under_oom() before
	 * triggering notification.
	 */
	if (memcg && memcg->under_oom)
		__wake_up(&memcg_oom_waitq, TASK_NORMAL, 0, memcg);
}

/**
 * mem_cgroup_oom_synchronize - complete memcg OOM handling
 * @handle: actually kill/wait or just clean up the OOM state
 *
 * This has to be called at the end of a page fault if the memcg OOM
 * handler was enabled.
 *
 * Memcg supports userspace OOM handling where failed allocations must
 * sleep on a waitqueue until the userspace task resolves the
 * situation.  Sleeping directly in the charge context with all kinds
 * of locks held is not a good idea, instead we remember an OOM state
 * in the task and mem_cgroup_oom_synchronize() has to be called at
 * the end of the page fault to complete the OOM handling.
 *
 * Returns %true if an ongoing memcg OOM situation was detected and
 * completed, %false otherwise.
 */
bool mem_cgroup_oom_synchronize(bool handle)
{
	struct mem_cgroup *memcg = current->memcg_in_oom;
	struct oom_wait_info owait;
	bool locked;

	/* OOM is global, do not handle */
	if (!memcg)
		return false;

	if (!handle)
		goto cleanup;

	owait.memcg = memcg;
	owait.wait.flags = 0;
	owait.wait.func = memcg_oom_wake_function;
	owait.wait.private = current;
	INIT_LIST_HEAD(&owait.wait.entry);

	prepare_to_wait(&memcg_oom_waitq, &owait.wait, TASK_KILLABLE);
	mem_cgroup_mark_under_oom(memcg);

	locked = mem_cgroup_oom_trylock(memcg);

	if (locked)
		mem_cgroup_oom_notify(memcg);

	schedule();
	mem_cgroup_unmark_under_oom(memcg);
	finish_wait(&memcg_oom_waitq, &owait.wait);

	if (locked)
		mem_cgroup_oom_unlock(memcg);
cleanup:
	current->memcg_in_oom = NULL;
	css_put(&memcg->css);
	return true;
}


bool memcg1_oom_prepare(struct mem_cgroup *memcg, bool *locked)
{
	/*
	 * We are in the middle of the charge context here, so we
	 * don't want to block when potentially sitting on a callstack
	 * that holds all kinds of filesystem and mm locks.
	 *
	 * cgroup1 allows disabling the OOM killer and waiting for outside
	 * handling until the charge can succeed; remember the context and put
	 * the task to sleep at the end of the page fault when all locks are
	 * released.
	 *
	 * On the other hand, in-kernel OOM killer allows for an async victim
	 * memory reclaim (oom_reaper) and that means that we are not solely
	 * relying on the oom victim to make a forward progress and we can
	 * invoke the oom killer here.
	 *
	 * Please note that mem_cgroup_out_of_memory might fail to find a
	 * victim and then we have to bail out from the charge path.
	 */
	if (READ_ONCE(memcg->oom_kill_disable)) {
		if (current->in_user_fault) {
			css_get(&memcg->css);
			current->memcg_in_oom = memcg;
		}
		return false;
	}

	mem_cgroup_mark_under_oom(memcg);

	*locked = mem_cgroup_oom_trylock(memcg);

	if (*locked)
		mem_cgroup_oom_notify(memcg);

	mem_cgroup_unmark_under_oom(memcg);

	return true;
}

void memcg1_oom_finish(struct mem_cgroup *memcg, bool locked)
{
	if (locked)
		mem_cgroup_oom_unlock(memcg);
}

static DEFINE_MUTEX(memcg_max_mutex);

static int mem_cgroup_resize_max(struct mem_cgroup *memcg,
				 unsigned long max, bool memsw)
{
	bool enlarge = false;
	bool drained = false;
	int ret;
	bool limits_invariant;
	struct page_counter *counter = memsw ? &memcg->memsw : &memcg->memory;

	do {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		mutex_lock(&memcg_max_mutex);
		/*
		 * Make sure that the new limit (memsw or memory limit) doesn't
		 * break our basic invariant rule memory.max <= memsw.max.
		 */
		limits_invariant = memsw ? max >= READ_ONCE(memcg->memory.max) :
					   max <= memcg->memsw.max;
		if (!limits_invariant) {
			mutex_unlock(&memcg_max_mutex);
			ret = -EINVAL;
			break;
		}
		if (max > counter->max)
			enlarge = true;
		ret = page_counter_set_max(counter, max);
		mutex_unlock(&memcg_max_mutex);

		if (!ret)
			break;

		if (!drained) {
			drain_all_stock(memcg);
			drained = true;
			continue;
		}

		if (!try_to_free_mem_cgroup_pages(memcg, 1, GFP_KERNEL,
				memsw ? 0 : MEMCG_RECLAIM_MAY_SWAP, NULL)) {
			ret = -EBUSY;
			break;
		}
	} while (true);

	if (!ret && enlarge)
		memcg1_oom_recover(memcg);

	return ret;
}

/*
 * Reclaims as many pages from the given memcg as possible.
 *
 * Caller is responsible for holding css reference for memcg.
 */
static int mem_cgroup_force_empty(struct mem_cgroup *memcg)
{
	int nr_retries = MAX_RECLAIM_RETRIES;

	/* we call try-to-free pages for make this cgroup empty */
	lru_add_drain_all();

	drain_all_stock(memcg);

	/* try to free all pages in this cgroup */
	while (nr_retries && page_counter_read(&memcg->memory)) {
		if (signal_pending(current))
			return -EINTR;

		if (!try_to_free_mem_cgroup_pages(memcg, 1, GFP_KERNEL,
						  MEMCG_RECLAIM_MAY_SWAP, NULL))
			nr_retries--;
	}

	return 0;
}

static ssize_t mem_cgroup_force_empty_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));

	if (mem_cgroup_is_root(memcg))
		return -EINVAL;
	return mem_cgroup_force_empty(memcg) ?: nbytes;
}

static u64 mem_cgroup_hierarchy_read(struct cgroup_subsys_state *css,
				     struct cftype *cft)
{
	return 1;
}

static int mem_cgroup_hierarchy_write(struct cgroup_subsys_state *css,
				      struct cftype *cft, u64 val)
{
	if (val == 1)
		return 0;

	pr_warn_once("Non-hierarchical mode is deprecated. "
		     "Please report your usecase to linux-mm@kvack.org if you "
		     "depend on this functionality.\n");

	return -EINVAL;
}

static u64 mem_cgroup_read_u64(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct page_counter *counter;

	switch (MEMFILE_TYPE(cft->private)) {
	case _MEM:
		counter = &memcg->memory;
		break;
	case _MEMSWAP:
		counter = &memcg->memsw;
		break;
	case _KMEM:
		counter = &memcg->kmem;
		break;
	case _TCP:
		counter = &memcg->tcpmem;
		break;
	default:
		BUG();
	}

	switch (MEMFILE_ATTR(cft->private)) {
	case RES_USAGE:
		if (counter == &memcg->memory)
			return (u64)mem_cgroup_usage(memcg, false) * PAGE_SIZE;
		if (counter == &memcg->memsw)
			return (u64)mem_cgroup_usage(memcg, true) * PAGE_SIZE;
		return (u64)page_counter_read(counter) * PAGE_SIZE;
	case RES_LIMIT:
		return (u64)counter->max * PAGE_SIZE;
	case RES_MAX_USAGE:
		return (u64)counter->watermark * PAGE_SIZE;
	case RES_FAILCNT:
		return counter->failcnt;
	case RES_SOFT_LIMIT:
		return (u64)READ_ONCE(memcg->soft_limit) * PAGE_SIZE;
	default:
		BUG();
	}
}

/*
 * This function doesn't do anything useful. Its only job is to provide a read
 * handler for a file so that cgroup_file_mode() will add read permissions.
 */
static int mem_cgroup_dummy_seq_show(__always_unused struct seq_file *m,
				     __always_unused void *v)
{
	return -EINVAL;
}

static int memcg_update_tcp_max(struct mem_cgroup *memcg, unsigned long max)
{
	int ret;

	mutex_lock(&memcg_max_mutex);

	ret = page_counter_set_max(&memcg->tcpmem, max);
	if (ret)
		goto out;

	if (!memcg->tcpmem_active) {
		/*
		 * The active flag needs to be written after the static_key
		 * update. This is what guarantees that the socket activation
		 * function is the last one to run. See mem_cgroup_sk_alloc()
		 * for details, and note that we don't mark any socket as
		 * belonging to this memcg until that flag is up.
		 *
		 * We need to do this, because static_keys will span multiple
		 * sites, but we can't control their order. If we mark a socket
		 * as accounted, but the accounting functions are not patched in
		 * yet, we'll lose accounting.
		 *
		 * We never race with the readers in mem_cgroup_sk_alloc(),
		 * because when this value change, the code to process it is not
		 * patched in yet.
		 */
		static_branch_inc(&memcg_sockets_enabled_key);
		memcg->tcpmem_active = true;
	}
out:
	mutex_unlock(&memcg_max_mutex);
	return ret;
}

/*
 * The user of this function is...
 * RES_LIMIT.
 */
static ssize_t mem_cgroup_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long nr_pages;
	int ret;

	buf = strstrip(buf);
	ret = page_counter_memparse(buf, "-1", &nr_pages);
	if (ret)
		return ret;

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_LIMIT:
		if (mem_cgroup_is_root(memcg)) { /* Can't set limit on root */
			ret = -EINVAL;
			break;
		}
		switch (MEMFILE_TYPE(of_cft(of)->private)) {
		case _MEM:
			ret = mem_cgroup_resize_max(memcg, nr_pages, false);
			break;
		case _MEMSWAP:
			ret = mem_cgroup_resize_max(memcg, nr_pages, true);
			break;
		case _KMEM:
			pr_warn_once("kmem.limit_in_bytes is deprecated and will be removed. "
				     "Writing any value to this file has no effect. "
				     "Please report your usecase to linux-mm@kvack.org if you "
				     "depend on this functionality.\n");
			ret = 0;
			break;
		case _TCP:
			ret = memcg_update_tcp_max(memcg, nr_pages);
			break;
		}
		break;
	case RES_SOFT_LIMIT:
		if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
			ret = -EOPNOTSUPP;
		} else {
			WRITE_ONCE(memcg->soft_limit, nr_pages);
			ret = 0;
		}
		break;
	}
	return ret ?: nbytes;
}

static ssize_t mem_cgroup_reset(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	struct page_counter *counter;

	switch (MEMFILE_TYPE(of_cft(of)->private)) {
	case _MEM:
		counter = &memcg->memory;
		break;
	case _MEMSWAP:
		counter = &memcg->memsw;
		break;
	case _KMEM:
		counter = &memcg->kmem;
		break;
	case _TCP:
		counter = &memcg->tcpmem;
		break;
	default:
		BUG();
	}

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_MAX_USAGE:
		page_counter_reset_watermark(counter);
		break;
	case RES_FAILCNT:
		counter->failcnt = 0;
		break;
	default:
		BUG();
	}

	return nbytes;
}

#ifdef CONFIG_NUMA

#define LRU_ALL_FILE (BIT(LRU_INACTIVE_FILE) | BIT(LRU_ACTIVE_FILE))
#define LRU_ALL_ANON (BIT(LRU_INACTIVE_ANON) | BIT(LRU_ACTIVE_ANON))
#define LRU_ALL	     ((1 << NR_LRU_LISTS) - 1)

static unsigned long mem_cgroup_node_nr_lru_pages(struct mem_cgroup *memcg,
				int nid, unsigned int lru_mask, bool tree)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
	unsigned long nr = 0;
	enum lru_list lru;

	VM_BUG_ON((unsigned)nid >= nr_node_ids);

	for_each_lru(lru) {
		if (!(BIT(lru) & lru_mask))
			continue;
		if (tree)
			nr += lruvec_page_state(lruvec, NR_LRU_BASE + lru);
		else
			nr += lruvec_page_state_local(lruvec, NR_LRU_BASE + lru);
	}
	return nr;
}

static unsigned long mem_cgroup_nr_lru_pages(struct mem_cgroup *memcg,
					     unsigned int lru_mask,
					     bool tree)
{
	unsigned long nr = 0;
	enum lru_list lru;

	for_each_lru(lru) {
		if (!(BIT(lru) & lru_mask))
			continue;
		if (tree)
			nr += memcg_page_state(memcg, NR_LRU_BASE + lru);
		else
			nr += memcg_page_state_local(memcg, NR_LRU_BASE + lru);
	}
	return nr;
}

static int memcg_numa_stat_show(struct seq_file *m, void *v)
{
	struct numa_stat {
		const char *name;
		unsigned int lru_mask;
	};

	static const struct numa_stat stats[] = {
		{ "total", LRU_ALL },
		{ "file", LRU_ALL_FILE },
		{ "anon", LRU_ALL_ANON },
		{ "unevictable", BIT(LRU_UNEVICTABLE) },
	};
	const struct numa_stat *stat;
	int nid;
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	mem_cgroup_flush_stats(memcg);

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {
		seq_printf(m, "%s=%lu", stat->name,
			   mem_cgroup_nr_lru_pages(memcg, stat->lru_mask,
						   false));
		for_each_node_state(nid, N_MEMORY)
			seq_printf(m, " N%d=%lu", nid,
				   mem_cgroup_node_nr_lru_pages(memcg, nid,
							stat->lru_mask, false));
		seq_putc(m, '\n');
	}

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {

		seq_printf(m, "hierarchical_%s=%lu", stat->name,
			   mem_cgroup_nr_lru_pages(memcg, stat->lru_mask,
						   true));
		for_each_node_state(nid, N_MEMORY)
			seq_printf(m, " N%d=%lu", nid,
				   mem_cgroup_node_nr_lru_pages(memcg, nid,
							stat->lru_mask, true));
		seq_putc(m, '\n');
	}

	return 0;
}
#endif /* CONFIG_NUMA */

static const unsigned int memcg1_stats[] = {
	NR_FILE_PAGES,
	NR_ANON_MAPPED,
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	NR_ANON_THPS,
#endif
	NR_SHMEM,
	NR_FILE_MAPPED,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	WORKINGSET_REFAULT_ANON,
	WORKINGSET_REFAULT_FILE,
#ifdef CONFIG_SWAP
	MEMCG_SWAP,
	NR_SWAPCACHE,
#endif
};

static const char *const memcg1_stat_names[] = {
	"cache",
	"rss",
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	"rss_huge",
#endif
	"shmem",
	"mapped_file",
	"dirty",
	"writeback",
	"workingset_refault_anon",
	"workingset_refault_file",
#ifdef CONFIG_SWAP
	"swap",
	"swapcached",
#endif
};

/* Universal VM events cgroup1 shows, original sort order */
static const unsigned int memcg1_events[] = {
	PGPGIN,
	PGPGOUT,
	PGFAULT,
	PGMAJFAULT,
};

void memcg1_stat_format(struct mem_cgroup *memcg, struct seq_buf *s)
{
	unsigned long memory, memsw;
	struct mem_cgroup *mi;
	unsigned int i;

	BUILD_BUG_ON(ARRAY_SIZE(memcg1_stat_names) != ARRAY_SIZE(memcg1_stats));

	mem_cgroup_flush_stats(memcg);

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		unsigned long nr;

		nr = memcg_page_state_local_output(memcg, memcg1_stats[i]);
		seq_buf_printf(s, "%s %lu\n", memcg1_stat_names[i], nr);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++)
		seq_buf_printf(s, "%s %lu\n", vm_event_name(memcg1_events[i]),
			       memcg_events_local(memcg, memcg1_events[i]));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_buf_printf(s, "%s %lu\n", lru_list_name(i),
			       memcg_page_state_local(memcg, NR_LRU_BASE + i) *
			       PAGE_SIZE);

	/* Hierarchical information */
	memory = memsw = PAGE_COUNTER_MAX;
	for (mi = memcg; mi; mi = parent_mem_cgroup(mi)) {
		memory = min(memory, READ_ONCE(mi->memory.max));
		memsw = min(memsw, READ_ONCE(mi->memsw.max));
	}
	seq_buf_printf(s, "hierarchical_memory_limit %llu\n",
		       (u64)memory * PAGE_SIZE);
	seq_buf_printf(s, "hierarchical_memsw_limit %llu\n",
		       (u64)memsw * PAGE_SIZE);

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		unsigned long nr;

		nr = memcg_page_state_output(memcg, memcg1_stats[i]);
		seq_buf_printf(s, "total_%s %llu\n", memcg1_stat_names[i],
			       (u64)nr);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++)
		seq_buf_printf(s, "total_%s %llu\n",
			       vm_event_name(memcg1_events[i]),
			       (u64)memcg_events(memcg, memcg1_events[i]));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_buf_printf(s, "total_%s %llu\n", lru_list_name(i),
			       (u64)memcg_page_state(memcg, NR_LRU_BASE + i) *
			       PAGE_SIZE);

#ifdef CONFIG_DEBUG_VM
	{
		pg_data_t *pgdat;
		struct mem_cgroup_per_node *mz;
		unsigned long anon_cost = 0;
		unsigned long file_cost = 0;

		for_each_online_pgdat(pgdat) {
			mz = memcg->nodeinfo[pgdat->node_id];

			anon_cost += mz->lruvec.anon_cost;
			file_cost += mz->lruvec.file_cost;
		}
		seq_buf_printf(s, "anon_cost %lu\n", anon_cost);
		seq_buf_printf(s, "file_cost %lu\n", file_cost);
	}
#endif
}

static u64 mem_cgroup_swappiness_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return mem_cgroup_swappiness(memcg);
}

static int mem_cgroup_swappiness_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (val > MAX_SWAPPINESS)
		return -EINVAL;

	if (!mem_cgroup_is_root(memcg))
		WRITE_ONCE(memcg->swappiness, val);
	else
		WRITE_ONCE(vm_swappiness, val);

	return 0;
}

static int mem_cgroup_oom_control_read(struct seq_file *sf, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(sf);

	seq_printf(sf, "oom_kill_disable %d\n", READ_ONCE(memcg->oom_kill_disable));
	seq_printf(sf, "under_oom %d\n", (bool)memcg->under_oom);
	seq_printf(sf, "oom_kill %lu\n",
		   atomic_long_read(&memcg->memory_events[MEMCG_OOM_KILL]));
	return 0;
}

static int mem_cgroup_oom_control_write(struct cgroup_subsys_state *css,
	struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	/* cannot set to root cgroup and only 0 and 1 are allowed */
	if (mem_cgroup_is_root(memcg) || !((val == 0) || (val == 1)))
		return -EINVAL;

	WRITE_ONCE(memcg->oom_kill_disable, val);
	if (!val)
		memcg1_oom_recover(memcg);

	return 0;
}

#ifdef CONFIG_SLUB_DEBUG
static int mem_cgroup_slab_show(struct seq_file *m, void *p)
{
	/*
	 * Deprecated.
	 * Please, take a look at tools/cgroup/memcg_slabinfo.py .
	 */
	return 0;
}
#endif

struct cftype mem_cgroup_legacy_files[] = {
	{
		.name = "usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "soft_limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_SOFT_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "failcnt",
		.private = MEMFILE_PRIVATE(_MEM, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "stat",
		.seq_show = memory_stat_show,
	},
	{
		.name = "force_empty",
		.write = mem_cgroup_force_empty_write,
	},
	{
		.name = "use_hierarchy",
		.write_u64 = mem_cgroup_hierarchy_write,
		.read_u64 = mem_cgroup_hierarchy_read,
	},
	{
		.name = "cgroup.event_control",		/* XXX: for compat */
		.write = memcg_write_event_control,
		.flags = CFTYPE_NO_PREFIX | CFTYPE_WORLD_WRITABLE,
	},
	{
		.name = "swappiness",
		.read_u64 = mem_cgroup_swappiness_read,
		.write_u64 = mem_cgroup_swappiness_write,
	},
	{
		.name = "move_charge_at_immigrate",
		.read_u64 = mem_cgroup_move_charge_read,
		.write_u64 = mem_cgroup_move_charge_write,
	},
	{
		.name = "oom_control",
		.seq_show = mem_cgroup_oom_control_read,
		.write_u64 = mem_cgroup_oom_control_write,
	},
	{
		.name = "pressure_level",
		.seq_show = mem_cgroup_dummy_seq_show,
	},
#ifdef CONFIG_NUMA
	{
		.name = "numa_stat",
		.seq_show = memcg_numa_stat_show,
	},
#endif
	{
		.name = "kmem.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.failcnt",
		.private = MEMFILE_PRIVATE(_KMEM, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
#ifdef CONFIG_SLUB_DEBUG
	{
		.name = "kmem.slabinfo",
		.seq_show = mem_cgroup_slab_show,
	},
#endif
	{
		.name = "kmem.tcp.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.failcnt",
		.private = MEMFILE_PRIVATE(_TCP, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{ },	/* terminate */
};

struct cftype memsw_files[] = {
	{
		.name = "memsw.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.failcnt",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{ },	/* terminate */
};

void memcg1_account_kmem(struct mem_cgroup *memcg, int nr_pages)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		if (nr_pages > 0)
			page_counter_charge(&memcg->kmem, nr_pages);
		else
			page_counter_uncharge(&memcg->kmem, -nr_pages);
	}
}

bool memcg1_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
			 gfp_t gfp_mask)
{
	struct page_counter *fail;

	if (page_counter_try_charge(&memcg->tcpmem, nr_pages, &fail)) {
		memcg->tcpmem_pressure = 0;
		return true;
	}
	memcg->tcpmem_pressure = 1;
	if (gfp_mask & __GFP_NOFAIL) {
		page_counter_charge(&memcg->tcpmem, nr_pages);
		return true;
	}
	return false;
}

static int __init memcg1_init(void)
{
	int node;

	for_each_node(node) {
		struct mem_cgroup_tree_per_node *rtpn;

		rtpn = kzalloc_node(sizeof(*rtpn), GFP_KERNEL, node);

		rtpn->rb_root = RB_ROOT;
		rtpn->rb_rightmost = NULL;
		spin_lock_init(&rtpn->lock);
		soft_limit_tree.rb_tree_per_node[node] = rtpn;
	}

	return 0;
}
subsys_initcall(memcg1_init);
