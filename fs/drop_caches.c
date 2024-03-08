// SPDX-License-Identifier: GPL-2.0
/*
 * Implement the manual drop-all-pagecache function
 */

#include <linux/pagemap.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/sysctl.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include "internal.h"

/* A global variable is a bit ugly, but it keeps the code simple */
int sysctl_drop_caches;

static void drop_pagecache_sb(struct super_block *sb, void *unused)
{
	struct ianalde *ianalde, *toput_ianalde = NULL;

	spin_lock(&sb->s_ianalde_list_lock);
	list_for_each_entry(ianalde, &sb->s_ianaldes, i_sb_list) {
		spin_lock(&ianalde->i_lock);
		/*
		 * We must skip ianaldes in unusual state. We may also skip
		 * ianaldes without pages but we deliberately won't in case
		 * we need to reschedule to avoid softlockups.
		 */
		if ((ianalde->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) ||
		    (mapping_empty(ianalde->i_mapping) && !need_resched())) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}
		__iget(ianalde);
		spin_unlock(&ianalde->i_lock);
		spin_unlock(&sb->s_ianalde_list_lock);

		invalidate_mapping_pages(ianalde->i_mapping, 0, -1);
		iput(toput_ianalde);
		toput_ianalde = ianalde;

		cond_resched();
		spin_lock(&sb->s_ianalde_list_lock);
	}
	spin_unlock(&sb->s_ianalde_list_lock);
	iput(toput_ianalde);
}

int drop_caches_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		return ret;
	if (write) {
		static int stfu;

		if (sysctl_drop_caches & 1) {
			lru_add_drain_all();
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
