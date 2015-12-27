/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <linux/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

void fsnotify_clear_vfsmount_marks_by_group(struct fsnotify_group *group)
{
	fsnotify_clear_marks_by_group_flags(group, FSNOTIFY_MARK_FLAG_VFSMOUNT);
}

/*
 * Recalculate the mnt->mnt_fsnotify_mask, or the mask of all FS_* event types
 * any notifier is interested in hearing for this mount point
 */
void fsnotify_recalc_vfsmount_mask(struct vfsmount *mnt)
{
	struct mount *m = real_mount(mnt);

	spin_lock(&mnt->mnt_root->d_lock);
	m->mnt_fsnotify_mask = fsnotify_recalc_mask(&m->mnt_fsnotify_marks);
	spin_unlock(&mnt->mnt_root->d_lock);
}

void fsnotify_destroy_vfsmount_mark(struct fsnotify_mark *mark)
{
	struct vfsmount *mnt = mark->mnt;
	struct mount *m = real_mount(mnt);

	BUG_ON(!mutex_is_locked(&mark->group->mark_mutex));
	assert_spin_locked(&mark->lock);

	spin_lock(&mnt->mnt_root->d_lock);

	hlist_del_init_rcu(&mark->obj_list);
	mark->mnt = NULL;

	m->mnt_fsnotify_mask = fsnotify_recalc_mask(&m->mnt_fsnotify_marks);
	spin_unlock(&mnt->mnt_root->d_lock);
}

/*
 * given a group and vfsmount, find the mark associated with that combination.
 * if found take a reference to that mark and return it, else return NULL
 */
struct fsnotify_mark *fsnotify_find_vfsmount_mark(struct fsnotify_group *group,
						  struct vfsmount *mnt)
{
	struct mount *m = real_mount(mnt);
	struct fsnotify_mark *mark;

	spin_lock(&mnt->mnt_root->d_lock);
	mark = fsnotify_find_mark(&m->mnt_fsnotify_marks, group);
	spin_unlock(&mnt->mnt_root->d_lock);

	return mark;
}

/*
 * Attach an initialized mark to a given group and vfsmount.
 * These marks may be used for the fsnotify backend to determine which
 * event types should be delivered to which groups.
 */
int fsnotify_add_vfsmount_mark(struct fsnotify_mark *mark,
			       struct fsnotify_group *group, struct vfsmount *mnt,
			       int allow_dups)
{
	struct mount *m = real_mount(mnt);
	int ret;

	mark->flags |= FSNOTIFY_MARK_FLAG_VFSMOUNT;

	BUG_ON(!mutex_is_locked(&group->mark_mutex));
	assert_spin_locked(&mark->lock);

	spin_lock(&mnt->mnt_root->d_lock);
	mark->mnt = mnt;
	ret = fsnotify_add_mark_list(&m->mnt_fsnotify_marks, mark, allow_dups);
	m->mnt_fsnotify_mask = fsnotify_recalc_mask(&m->mnt_fsnotify_marks);
	spin_unlock(&mnt->mnt_root->d_lock);

	return ret;
}
