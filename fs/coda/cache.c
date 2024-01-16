// SPDX-License-Identifier: GPL-2.0
/*
 * Cache operations for Coda.
 * For Linux 2.1: (C) 1997 Carnegie Mellon University
 * For Linux 2.3: (C) 2000 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project http://www.coda.cs.cmu.edu/ <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"
#include "coda_cache.h"

static atomic_t permission_epoch = ATOMIC_INIT(0);

/* replace or extend an acl cache hit */
void coda_cache_enter(struct inode *inode, int mask)
{
	struct coda_inode_info *cii = ITOC(inode);

	spin_lock(&cii->c_lock);
	cii->c_cached_epoch = atomic_read(&permission_epoch);
	if (!uid_eq(cii->c_uid, current_fsuid())) {
		cii->c_uid = current_fsuid();
                cii->c_cached_perm = mask;
        } else
                cii->c_cached_perm |= mask;
	spin_unlock(&cii->c_lock);
}

/* remove cached acl from an inode */
void coda_cache_clear_inode(struct inode *inode)
{
	struct coda_inode_info *cii = ITOC(inode);
	spin_lock(&cii->c_lock);
	cii->c_cached_epoch = atomic_read(&permission_epoch) - 1;
	spin_unlock(&cii->c_lock);
}

/* remove all acl caches */
void coda_cache_clear_all(struct super_block *sb)
{
	atomic_inc(&permission_epoch);
}


/* check if the mask has been matched against the acl already */
int coda_cache_check(struct inode *inode, int mask)
{
	struct coda_inode_info *cii = ITOC(inode);
	int hit;
	
	spin_lock(&cii->c_lock);
	hit = (mask & cii->c_cached_perm) == mask &&
	    uid_eq(cii->c_uid, current_fsuid()) &&
	    cii->c_cached_epoch == atomic_read(&permission_epoch);
	spin_unlock(&cii->c_lock);

	return hit;
}


/* Purging dentries and children */
/* The following routines drop dentries which are not
   in use and flag dentries which are in use to be 
   zapped later.

   The flags are detected by:
   - coda_dentry_revalidate (for lookups) if the flag is C_PURGE
   - coda_dentry_delete: to remove dentry from the cache when d_count
     falls to zero
   - an inode method coda_revalidate (for attributes) if the 
     flag is C_VATTR
*/

/* this won't do any harm: just flag all children */
static void coda_flag_children(struct dentry *parent, int flag)
{
	struct dentry *de;

	spin_lock(&parent->d_lock);
	list_for_each_entry(de, &parent->d_subdirs, d_child) {
		/* don't know what to do with negative dentries */
		if (d_inode(de) ) 
			coda_flag_inode(d_inode(de), flag);
	}
	spin_unlock(&parent->d_lock);
	return; 
}

void coda_flag_inode_children(struct inode *inode, int flag)
{
	struct dentry *alias_de;

	if ( !inode || !S_ISDIR(inode->i_mode)) 
		return; 

	alias_de = d_find_alias(inode);
	if (!alias_de)
		return;
	coda_flag_children(alias_de, flag);
	shrink_dcache_parent(alias_de);
	dput(alias_de);
}

