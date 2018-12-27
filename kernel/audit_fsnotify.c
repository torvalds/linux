/* audit_fsnotify.c -- tracking inodes
 *
 * Copyright 2003-2009,2014-2015 Red Hat, Inc.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright 2005 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/audit.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/fsnotify_backend.h>
#include <linux/namei.h>
#include <linux/netlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/security.h>
#include "audit.h"

/*
 * this mark lives on the parent directory of the inode in question.
 * but dev, ino, and path are about the child
 */
struct audit_fsnotify_mark {
	dev_t dev;		/* associated superblock device */
	unsigned long ino;	/* associated inode number */
	char *path;		/* insertion path */
	struct fsnotify_mark mark; /* fsnotify mark on the inode */
	struct audit_krule *rule;
};

/* fsnotify handle. */
static struct fsnotify_group *audit_fsnotify_group;

/* fsnotify events we care about. */
#define AUDIT_FS_EVENTS (FS_MOVE | FS_CREATE | FS_DELETE | FS_DELETE_SELF |\
			 FS_MOVE_SELF | FS_EVENT_ON_CHILD)

static void audit_fsnotify_mark_free(struct audit_fsnotify_mark *audit_mark)
{
	kfree(audit_mark->path);
	kfree(audit_mark);
}

static void audit_fsnotify_free_mark(struct fsnotify_mark *mark)
{
	struct audit_fsnotify_mark *audit_mark;

	audit_mark = container_of(mark, struct audit_fsnotify_mark, mark);
	audit_fsnotify_mark_free(audit_mark);
}

char *audit_mark_path(struct audit_fsnotify_mark *mark)
{
	return mark->path;
}

int audit_mark_compare(struct audit_fsnotify_mark *mark, unsigned long ino, dev_t dev)
{
	if (mark->ino == AUDIT_INO_UNSET)
		return 0;
	return (mark->ino == ino) && (mark->dev == dev);
}

static void audit_update_mark(struct audit_fsnotify_mark *audit_mark,
			     const struct inode *inode)
{
	audit_mark->dev = inode ? inode->i_sb->s_dev : AUDIT_DEV_UNSET;
	audit_mark->ino = inode ? inode->i_ino : AUDIT_INO_UNSET;
}

struct audit_fsnotify_mark *audit_alloc_mark(struct audit_krule *krule, char *pathname, int len)
{
	struct audit_fsnotify_mark *audit_mark;
	struct path path;
	struct dentry *dentry;
	struct inode *inode;
	int ret;

	if (pathname[0] != '/' || pathname[len-1] == '/')
		return ERR_PTR(-EINVAL);

	dentry = kern_path_locked(pathname, &path);
	if (IS_ERR(dentry))
		return (void *)dentry; /* returning an error */
	inode = path.dentry->d_inode;
	inode_unlock(inode);

	audit_mark = kzalloc(sizeof(*audit_mark), GFP_KERNEL);
	if (unlikely(!audit_mark)) {
		audit_mark = ERR_PTR(-ENOMEM);
		goto out;
	}

	fsnotify_init_mark(&audit_mark->mark, audit_fsnotify_group);
	audit_mark->mark.mask = AUDIT_FS_EVENTS;
	audit_mark->path = pathname;
	audit_update_mark(audit_mark, dentry->d_inode);
	audit_mark->rule = krule;

	ret = fsnotify_add_inode_mark(&audit_mark->mark, inode, true);
	if (ret < 0) {
		fsnotify_put_mark(&audit_mark->mark);
		audit_mark = ERR_PTR(ret);
	}
out:
	dput(dentry);
	path_put(&path);
	return audit_mark;
}

static void audit_mark_log_rule_change(struct audit_fsnotify_mark *audit_mark, char *op)
{
	struct audit_buffer *ab;
	struct audit_krule *rule = audit_mark->rule;

	if (!audit_enabled)
		return;
	ab = audit_log_start(NULL, GFP_NOFS, AUDIT_CONFIG_CHANGE);
	if (unlikely(!ab))
		return;
	audit_log_session_info(ab);
	audit_log_format(ab, " op=%s path=", op);
	audit_log_untrustedstring(ab, audit_mark->path);
	audit_log_key(ab, rule->filterkey);
	audit_log_format(ab, " list=%d res=1", rule->listnr);
	audit_log_end(ab);
}

void audit_remove_mark(struct audit_fsnotify_mark *audit_mark)
{
	fsnotify_destroy_mark(&audit_mark->mark, audit_fsnotify_group);
	fsnotify_put_mark(&audit_mark->mark);
}

void audit_remove_mark_rule(struct audit_krule *krule)
{
	struct audit_fsnotify_mark *mark = krule->exe;

	audit_remove_mark(mark);
}

static void audit_autoremove_mark_rule(struct audit_fsnotify_mark *audit_mark)
{
	struct audit_krule *rule = audit_mark->rule;
	struct audit_entry *entry = container_of(rule, struct audit_entry, rule);

	audit_mark_log_rule_change(audit_mark, "autoremove_rule");
	audit_del_rule(entry);
}

/* Update mark data in audit rules based on fsnotify events. */
static int audit_mark_handle_event(struct fsnotify_group *group,
				    struct inode *to_tell,
				    u32 mask, const void *data, int data_type,
				    const unsigned char *dname, u32 cookie,
				    struct fsnotify_iter_info *iter_info)
{
	struct fsnotify_mark *inode_mark = fsnotify_iter_inode_mark(iter_info);
	struct audit_fsnotify_mark *audit_mark;
	const struct inode *inode = NULL;

	audit_mark = container_of(inode_mark, struct audit_fsnotify_mark, mark);

	BUG_ON(group != audit_fsnotify_group);

	switch (data_type) {
	case (FSNOTIFY_EVENT_PATH):
		inode = ((const struct path *)data)->dentry->d_inode;
		break;
	case (FSNOTIFY_EVENT_INODE):
		inode = (const struct inode *)data;
		break;
	default:
		BUG();
		return 0;
	}

	if (mask & (FS_CREATE|FS_MOVED_TO|FS_DELETE|FS_MOVED_FROM)) {
		if (audit_compare_dname_path(dname, audit_mark->path, AUDIT_NAME_FULL))
			return 0;
		audit_update_mark(audit_mark, inode);
	} else if (mask & (FS_DELETE_SELF|FS_UNMOUNT|FS_MOVE_SELF))
		audit_autoremove_mark_rule(audit_mark);

	return 0;
}

static const struct fsnotify_ops audit_mark_fsnotify_ops = {
	.handle_event =	audit_mark_handle_event,
	.free_mark = audit_fsnotify_free_mark,
};

static int __init audit_fsnotify_init(void)
{
	audit_fsnotify_group = fsnotify_alloc_group(&audit_mark_fsnotify_ops);
	if (IS_ERR(audit_fsnotify_group)) {
		audit_fsnotify_group = NULL;
		audit_panic("cannot create audit fsnotify group");
	}
	return 0;
}
device_initcall(audit_fsnotify_init);
