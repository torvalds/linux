/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAP_SLOTS_H
#define _LINUX_SWAP_SLOTS_H

#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#define SWAP_SLOTS_CACHE_SIZE			SWAP_BATCH
#define THRESHOLD_ACTIVATE_SWAP_SLOTS_CACHE	(5*SWAP_SLOTS_CACHE_SIZE)
#define THRESHOLD_DEACTIVATE_SWAP_SLOTS_CACHE	(2*SWAP_SLOTS_CACHE_SIZE)

struct swap_slots_cache {
	bool		lock_initialized;
	struct mutex	alloc_lock; /* protects slots, nr, cur */
	swp_entry_t	*slots;
	int		nr;
	int		cur;
	int		n_ret;
};

void disable_swap_slots_cache_lock(void);
void reenable_swap_slots_cache_unlock(void);
void enable_swap_slots_cache(void);

extern bool swap_slot_cache_enabled;

#endif /* _LINUX_SWAP_SLOTS_H */
