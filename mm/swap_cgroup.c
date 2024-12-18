// SPDX-License-Identifier: GPL-2.0
#include <linux/swap_cgroup.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include <linux/swapops.h> /* depends on mm.h include */

static DEFINE_MUTEX(swap_cgroup_mutex);

/* Pack two cgroup id (short) of two entries in one swap_cgroup (atomic_t) */
#define ID_PER_SC (sizeof(struct swap_cgroup) / sizeof(unsigned short))
#define ID_SHIFT (BITS_PER_TYPE(unsigned short))
#define ID_MASK (BIT(ID_SHIFT) - 1)
struct swap_cgroup {
	atomic_t ids;
};

struct swap_cgroup_ctrl {
	struct swap_cgroup *map;
};

static struct swap_cgroup_ctrl swap_cgroup_ctrl[MAX_SWAPFILES];

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
 */
static unsigned short __swap_cgroup_id_lookup(struct swap_cgroup *map,
					      pgoff_t offset)
{
	unsigned int shift = (offset % ID_PER_SC) * ID_SHIFT;
	unsigned int old_ids = atomic_read(&map[offset / ID_PER_SC].ids);

	BUILD_BUG_ON(!is_power_of_2(ID_PER_SC));
	BUILD_BUG_ON(sizeof(struct swap_cgroup) != sizeof(atomic_t));

	return (old_ids >> shift) & ID_MASK;
}

static unsigned short __swap_cgroup_id_xchg(struct swap_cgroup *map,
					    pgoff_t offset,
					    unsigned short new_id)
{
	unsigned short old_id;
	struct swap_cgroup *sc = &map[offset / ID_PER_SC];
	unsigned int shift = (offset % ID_PER_SC) * ID_SHIFT;
	unsigned int new_ids, old_ids = atomic_read(&sc->ids);

	do {
		old_id = (old_ids >> shift) & ID_MASK;
		new_ids = (old_ids & ~(ID_MASK << shift));
		new_ids |= ((unsigned int)new_id) << shift;
	} while (!atomic_try_cmpxchg(&sc->ids, &old_ids, new_ids));

	return old_id;
}

/**
 * swap_cgroup_record - record mem_cgroup for a set of swap entries
 * @ent: the first swap entry to be recorded into
 * @id: mem_cgroup to be recorded
 * @nr_ents: number of swap entries to be recorded
 *
 * Returns old value at success, 0 at failure.
 * (Of course, old value can be 0.)
 */
unsigned short swap_cgroup_record(swp_entry_t ent, unsigned short id,
				  unsigned int nr_ents)
{
	struct swap_cgroup_ctrl *ctrl;
	pgoff_t offset = swp_offset(ent);
	pgoff_t end = offset + nr_ents;
	unsigned short old, iter;
	struct swap_cgroup *map;

	ctrl = &swap_cgroup_ctrl[swp_type(ent)];
	map = ctrl->map;

	old = __swap_cgroup_id_lookup(map, offset);
	do {
		iter = __swap_cgroup_id_xchg(map, offset, id);
		VM_BUG_ON(iter != old);
	} while (++offset != end);

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
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return 0;

	ctrl = &swap_cgroup_ctrl[swp_type(ent)];
	return __swap_cgroup_id_lookup(ctrl->map, swp_offset(ent));
}

int swap_cgroup_swapon(int type, unsigned long max_pages)
{
	struct swap_cgroup *map;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return 0;

	BUILD_BUG_ON(sizeof(unsigned short) * ID_PER_SC !=
		     sizeof(struct swap_cgroup));
	map = vcalloc(DIV_ROUND_UP(max_pages, ID_PER_SC),
		      sizeof(struct swap_cgroup));
	if (!map)
		goto nomem;

	ctrl = &swap_cgroup_ctrl[type];
	mutex_lock(&swap_cgroup_mutex);
	ctrl->map = map;
	mutex_unlock(&swap_cgroup_mutex);

	return 0;
nomem:
	pr_info("couldn't allocate enough memory for swap_cgroup\n");
	pr_info("swap_cgroup can be disabled by swapaccount=0 boot option\n");
	return -ENOMEM;
}

void swap_cgroup_swapoff(int type)
{
	struct swap_cgroup *map;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return;

	mutex_lock(&swap_cgroup_mutex);
	ctrl = &swap_cgroup_ctrl[type];
	map = ctrl->map;
	ctrl->map = NULL;
	mutex_unlock(&swap_cgroup_mutex);

	vfree(map);
}
