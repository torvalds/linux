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
 * swap_cgroup_record - record mem_cgroup for a set of swap entries.
 * These entries must belong to one single folio, and that folio
 * must be being charged for swap space (swap out), and these
 * entries must not have been charged
 *
 * @folio: the folio that the swap entry belongs to
 * @id: mem_cgroup ID to be recorded
 * @ent: the first swap entry to be recorded
 */
void swap_cgroup_record(struct folio *folio, unsigned short id,
			swp_entry_t ent)
{
	unsigned int nr_ents = folio_nr_pages(folio);
	struct swap_cgroup *map;
	pgoff_t offset, end;
	unsigned short old;

	offset = swp_offset(ent);
	end = offset + nr_ents;
	map = swap_cgroup_ctrl[swp_type(ent)].map;

	do {
		old = __swap_cgroup_id_xchg(map, offset, id);
		VM_BUG_ON(old);
	} while (++offset != end);
}

/**
 * swap_cgroup_clear - clear mem_cgroup for a set of swap entries.
 * These entries must be being uncharged from swap. They either
 * belongs to one single folio in the swap cache (swap in for
 * cgroup v1), or no longer have any users (slot freeing).
 *
 * @ent: the first swap entry to be recorded into
 * @nr_ents: number of swap entries to be recorded
 *
 * Returns the existing old value.
 */
unsigned short swap_cgroup_clear(swp_entry_t ent, unsigned int nr_ents)
{
	pgoff_t offset = swp_offset(ent);
	pgoff_t end = offset + nr_ents;
	struct swap_cgroup *map;
	unsigned short old, iter = 0;

	offset = swp_offset(ent);
	end = offset + nr_ents;
	map = swap_cgroup_ctrl[swp_type(ent)].map;

	do {
		old = __swap_cgroup_id_xchg(map, offset, 0);
		if (!iter)
			iter = old;
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
	map = vzalloc(DIV_ROUND_UP(max_pages, ID_PER_SC) *
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
