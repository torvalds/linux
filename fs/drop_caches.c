// SPDX-License-Identifier: GPL-2.0
/*
 * Implement the manual drop-all-pagecache function
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/sysctl.h>
#include <linux/gfp.h>
#include "internal.h"

/* A global variable is a bit ugly, but it keeps the code simple */
int sysctl_drop_caches;

static void drop_pagecache_sb(struct super_block *sb, void *unused)
{
	struct iyesde *iyesde, *toput_iyesde = NULL;

	spin_lock(&sb->s_iyesde_list_lock);
	list_for_each_entry(iyesde, &sb->s_iyesdes, i_sb_list) {
		spin_lock(&iyesde->i_lock);
		/*
		 * We must skip iyesdes in unusual state. We may also skip
		 * iyesdes without pages but we deliberately won't in case
		 * we need to reschedule to avoid softlockups.
		 */
		if ((iyesde->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) ||
		    (iyesde->i_mapping->nrpages == 0 && !need_resched())) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}
		__iget(iyesde);
		spin_unlock(&iyesde->i_lock);
		spin_unlock(&sb->s_iyesde_list_lock);

		invalidate_mapping_pages(iyesde->i_mapping, 0, -1);
		iput(toput_iyesde);
		toput_iyesde = iyesde;

		cond_resched();
		spin_lock(&sb->s_iyesde_list_lock);
	}
	spin_unlock(&sb->s_iyesde_list_lock);
	iput(toput_iyesde);
}

int drop_caches_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		return ret;
	if (write) {
		static int stfu;

		if (sysctl_drop_caches & 1) {
			iterate_supers(drop_pagecache_sb, NULL);
			count_vm_event(DROP_PAGECACHE);
		}
		if (sysctl_drop_caches & 2) {
			drop_slab();
			count_vm_event(DROP_SLAB);
		}
		if (!stfu) {
			pr_info("%s (%d): drop_caches: %d\n",
				current->comm, task_pid_nr(current),
				sysctl_drop_caches);
		}
		stfu |= sysctl_drop_caches & 4;
	}
	return 0;
}
