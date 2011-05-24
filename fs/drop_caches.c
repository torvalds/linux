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
	struct inode *inode, *toput_inode = NULL;

	spin_lock(&inode_sb_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		spin_lock(&inode->i_lock);
		if ((inode->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) ||
		    (inode->i_mapping->nrpages == 0)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&inode_sb_list_lock);
		invalidate_mapping_pages(inode->i_mapping, 0, -1);
		iput(toput_inode);
		toput_inode = inode;
		spin_lock(&inode_sb_list_lock);
	}
	spin_unlock(&inode_sb_list_lock);
	iput(toput_inode);
}

static void drop_slab(void)
{
	int nr_objects;

	do {
		nr_objects = shrink_slab(1000, GFP_KERNEL, 1000);
	} while (nr_objects > 10);
}

int drop_caches_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret)
		return ret;
	if (write) {
		if (sysctl_drop_caches & 1)
			iterate_supers(drop_pagecache_sb, NULL);
		if (sysctl_drop_caches & 2)
			drop_slab();
	}
	return 0;
}
