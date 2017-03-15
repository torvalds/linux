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
	fsnotify_clear_marks_by_group_flags(group, FSNOTIFY_OBJ_TYPE_VFSMOUNT);
}

void fsnotify_recalc_vfsmount_mask(struct vfsmount *mnt)
{
	fsnotify_recalc_mask(real_mount(mnt)->mnt_fsnotify_marks);
}

void fsnotify_destroy_vfsmount_mark(struct fsnotify_mark *mark)
{
	struct vfsmount *mnt = mark->connector->mnt;
	struct mount *m = real_mount(mnt);

	BUG_ON(!mutex_is_locked(&mark->group->mark_mutex));
	assert_spin_locked(&mark->lock);

	spin_lock(&mnt->mnt_root->d_lock);

	hlist_del_init_rcu(&mark->obj_list);
	mark->connector = NULL;

	spin_unlock(&mnt->mnt_root->d_lock);

	fsnotify_recalc_mask(m->mnt_fsnotify_marks);
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
	mark = fsnotify_find_mark(m->mnt_fsnotify_marks, group);
	spin_unlock(&mnt->mnt_root->d_lock);

	return mark;
}
