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
#include <linux/list.h>
#include <linux/mount.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

static struct kmem_cache *fsnotify_event_cachep;

void fsnotify_get_event(struct fsnotify_event *event)
{
	atomic_inc(&event->refcnt);
}

void fsnotify_put_event(struct fsnotify_event *event)
{
	if (!event)
		return;

	if (atomic_dec_and_test(&event->refcnt)) {
		if (event->data_type == FSNOTIFY_EVENT_PATH)
			path_put(&event->path);

		kmem_cache_free(fsnotify_event_cachep, event);
	}
}

/*
 * Allocate a new event which will be sent to each group's handle_event function
 * if the group was interested in this particular event.
 */
struct fsnotify_event *fsnotify_create_event(struct inode *to_tell, __u32 mask,
					     void *data, int data_type)
{
	struct fsnotify_event *event;

	event = kmem_cache_alloc(fsnotify_event_cachep, GFP_KERNEL);
	if (!event)
		return NULL;

	atomic_set(&event->refcnt, 1);

	spin_lock_init(&event->lock);

	event->path.dentry = NULL;
	event->path.mnt = NULL;
	event->inode = NULL;

	event->to_tell = to_tell;

	switch (data_type) {
	case FSNOTIFY_EVENT_FILE: {
		struct file *file = data;
		struct path *path = &file->f_path;
		event->path.dentry = path->dentry;
		event->path.mnt = path->mnt;
		path_get(&event->path);
		event->data_type = FSNOTIFY_EVENT_PATH;
		break;
	}
	case FSNOTIFY_EVENT_PATH: {
		struct path *path = data;
		event->path.dentry = path->dentry;
		event->path.mnt = path->mnt;
		path_get(&event->path);
		event->data_type = FSNOTIFY_EVENT_PATH;
		break;
	}
	case FSNOTIFY_EVENT_INODE:
		event->inode = data;
		event->data_type = FSNOTIFY_EVENT_INODE;
		break;
	case FSNOTIFY_EVENT_NONE:
		event->inode = NULL;
		event->path.dentry = NULL;
		event->path.mnt = NULL;
		break;
	default:
		BUG();
	}

	event->mask = mask;

	return event;
}

__init int fsnotify_notification_init(void)
{
	fsnotify_event_cachep = KMEM_CACHE(fsnotify_event, SLAB_PANIC);

	return 0;
}
subsys_initcall(fsnotify_notification_init);

