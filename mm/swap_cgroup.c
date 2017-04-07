#include <linux/swap_cgroup.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include <linux/swapops.h> /* depends on mm.h include */

static DEFINE_MUTEX(swap_cgroup_mutex);
struct swap_cgroup_ctrl {
	struct page **map;
	unsigned long length;
	spinlock_t	lock;
};

static struct swap_cgroup_ctrl swap_cgroup_ctrl[MAX_SWAPFILES];

struct swap_cgroup {
	unsigned short		id;
};
#define SC_PER_PAGE	(PAGE_SIZE/sizeof(struct swap_cgroup))

/*
 * SwapCgroup implements "lookup" and "exchange" operations.
 * In typical usage, this swap_cgroup is accessed via memcg's charge/uncharge
 * against SwapCache. At swap_free(), this is accessed directly from swap.
 *
 * This means,
 *  - we have no race in "exchange" when we're accessed via SwapCache because
 *    SwapCache(and its swp_entry) is under lock.
 *  - When called via swap_free(), there is no user of this entry and no race.
 * Then, we don't need lock around "exchange".
 *
 * TODO: we can push these buffers out to HIGHMEM.
 */

/*
 * allocate buffer for swap_cgroup.
 */
static int swap_cgroup_prepare(int type)
{
	struct page *page;
	struct swap_cgroup_ctrl *ctrl;
	unsigned long idx, max;

	ctrl = &swap_cgroup_ctrl[type];

	for (idx = 0; idx < ctrl->length; idx++) {
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			goto not_enough_page;
		ctrl->map[idx] = page;

		if (!(idx % SWAP_CLUSTER_MAX))
			cond_resched();
	}
	return 0;
not_enough_page:
	max = idx;
	for (idx = 0; idx < max; idx++)
		__free_page(ctrl->map[idx]);

	return -ENOMEM;
}

static struct swap_cgroup *lookup_swap_cgroup(swp_entry_t ent,
					struct swap_cgroup_ctrl **ctrlp)
{
	pgoff_t offset = swp_offset(ent);
	struct swap_cgroup_ctrl *ctrl;
	struct page *mappage;
	struct swap_cgroup *sc;

	ctrl = &swap_cgroup_ctrl[swp_type(ent)];
	if (ctrlp)
		*ctrlp = ctrl;

	mappage = ctrl->map[offset / SC_PER_PAGE];
	sc = page_address(mappage);
	return sc + offset % SC_PER_PAGE;
}

/**
 * swap_cgroup_cmpxchg - cmpxchg mem_cgroup's id for this swp_entry.
 * @ent: swap entry to be cmpxchged
 * @old: old id
 * @new: new id
 *
 * Returns old id at success, 0 at failure.
 * (There is no mem_cgroup using 0 as its id)
 */
unsigned short swap_cgroup_cmpxchg(swp_entry_t ent,
					unsigned short old, unsigned short new)
{
	struct swap_cgroup_ctrl *ctrl;
	struct swap_cgroup *sc;
	unsigned long flags;
	unsigned short retval;

	sc = lookup_swap_cgroup(ent, &ctrl);

	spin_lock_irqsave(&ctrl->lock, flags);
	retval = sc->id;
	if (retval == old)
		sc->id = new;
	else
		retval = 0;
	spin_unlock_irqrestore(&ctrl->lock, flags);
	return retval;
}

/**
 * swap_cgroup_record - record mem_cgroup for this swp_entry.
 * @ent: swap entry to be recorded into
 * @id: mem_cgroup to be recorded
 *
 * Returns old value at success, 0 at failure.
 * (Of course, old value can be 0.)
 */
unsigned short swap_cgroup_record(swp_entry_t ent, unsigned short id)
{
	struct swap_cgroup_ctrl *ctrl;
	struct swap_cgroup *sc;
	unsigned short old;
	unsigned long flags;

	sc = lookup_swap_cgroup(ent, &ctrl);

	spin_lock_irqsave(&ctrl->lock, flags);
	old = sc->id;
	sc->id = id;
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return old;
}

/**
 * lookup_swap_cgroup_id - lookup mem_cgroup id tied to swap entry
 * @ent: swap entry to be looked up.
 *
 * Returns ID of mem_cgroup at success. 0 at failure. (0 is invalid ID)
 */
unsigned short lookup_swap_cgroup_id(swp_entry_t ent)
{
	return lookup_swap_cgroup(ent, NULL)->id;
}

int swap_cgroup_swapon(int type, unsigned long max_pages)
{
	void *array;
	unsigned long array_size;
	unsigned long length;
	struct swap_cgroup_ctrl *ctrl;

	if (!do_swap_account)
		return 0;

	length = DIV_ROUND_UP(max_pages, SC_PER_PAGE);
	array_size = length * sizeof(void *);

	array = vzalloc(array_size);
	if (!array)
		goto nomem;

	ctrl = &swap_cgroup_ctrl[type];
	mutex_lock(&swap_cgroup_mutex);
	ctrl->length = length;
	ctrl->map = array;
	spin_lock_init(&ctrl->lock);
	if (swap_cgroup_prepare(type)) {
		/* memory shortage */
		ctrl->map = NULL;
		ctrl->length = 0;
		mutex_unlock(&swap_cgroup_mutex);
		vfree(array);
		goto nomem;
	}
	mutex_unlock(&swap_cgroup_mutex);

	return 0;
nomem:
	pr_info("couldn't allocate enough memory for swap_cgroup\n");
	pr_info("swap_cgroup can be disabled by swapaccount=0 boot option\n");
	return -ENOMEM;
}

void swap_cgroup_swapoff(int type)
{
	struct page **map;
	unsigned long i, length;
	struct swap_cgroup_ctrl *ctrl;

	if (!do_swap_account)
		return;

	mutex_lock(&swap_cgroup_mutex);
	ctrl = &swap_cgroup_ctrl[type];
	map = ctrl->map;
	length = ctrl->length;
	ctrl->map = NULL;
	ctrl->length = 0;
	mutex_unlock(&swap_cgroup_mutex);

	if (map) {
		for (i = 0; i < length; i++) {
			struct page *page = map[i];
			if (page)
				__free_page(page);
			if (!(i % SWAP_CLUSTER_MAX))
				cond_resched();
		}
		vfree(map);
	}
}
