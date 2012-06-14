/* audit_watch.c -- watching inodes
 *
 * Copyright 2003-2009 Red Hat, Inc.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
 * Reference counting:
 *
 * audit_parent: lifetime is from audit_init_parent() to receipt of an FS_IGNORED
 * 	event.  Each audit_watch holds a reference to its associated parent.
 *
 * audit_watch: if added to lists, lifetime is from audit_init_watch() to
 * 	audit_remove_watch().  Additionally, an audit_watch may exist
 * 	temporarily to assist in searching existing filter data.  Each
 * 	audit_krule holds a reference to its associated watch.
 */

struct audit_watch {
	atomic_t		count;	/* reference count */
	dev_t			dev;	/* associated superblock device */
	char			*path;	/* insertion path */
	unsigned long		ino;	/* associated inode number */
	struct audit_parent	*parent; /* associated parent */
	struct list_head	wlist;	/* entry in parent->watches list */
	struct list_head	rules;	/* anchor for krule->rlist */
};

struct audit_parent {
	struct list_head	watches; /* anchor for audit_watch->wlist */
	struct fsnotify_mark mark; /* fsnotify mark on the inode */
};

/* fsnotify handle. */
static struct fsnotify_group *audit_watch_group;

/* fsnotify events we care about. */
#define AUDIT_FS_WATCH (FS_MOVE | FS_CREATE | FS_DELETE | FS_DELETE_SELF |\
			FS_MOVE_SELF | FS_EVENT_ON_CHILD)

static void audit_free_parent(struct audit_parent *parent)
{
	WARN_ON(!list_empty(&parent->watches));
	kfree(parent);
}

static void audit_watch_free_mark(struct fsnotify_mark *entry)
{
	struct audit_parent *parent;

	parent = container_of(entry, struct audit_parent, mark);
	audit_free_parent(parent);
}

static void audit_get_parent(struct audit_parent *parent)
{
	if (likely(parent))
		fsnotify_get_mark(&parent->mark);
}

static void audit_put_parent(struct audit_parent *parent)
{
	if (likely(parent))
		fsnotify_put_mark(&parent->mark);
}

/*
 * Find and return the audit_parent on the given inode.  If found a reference
 * is taken on this parent.
 */
static inline struct audit_parent *audit_find_parent(struct inode *inode)
{
	struct audit_parent *parent = NULL;
	struct fsnotify_mark *entry;

	entry = fsnotify_find_inode_mark(audit_watch_group, inode);
	if (entry)
		parent = container_of(entry, struct audit_parent, mark);

	return parent;
}

void audit_get_watch(struct audit_watch *watch)
{
	atomic_inc(&watch->count);
}

void audit_put_watch(struct audit_watch *watch)
{
	if (atomic_dec_and_test(&watch->count)) {
		WARN_ON(watch->parent);
		WARN_ON(!list_empty(&watch->rules));
		kfree(watch->path);
		kfree(watch);
	}
}

static void audit_remove_watch(struct audit_watch *watch)
{
	list_del(&watch->wlist);
	audit_put_parent(watch->parent);
	watch->parent = NULL;
	audit_put_watch(watch); /* match initial get */
}

char *audit_watch_path(struct audit_watch *watch)
{
	return watch->path;
}

int audit_watch_compare(struct audit_watch *watch, unsigned long ino, dev_t dev)
{
	return (watch->ino != (unsigned long)-1) &&
		(watch->ino == ino) &&
		(watch->dev == dev);
}

/* Initialize a parent watch entry. */
static struct audit_parent *audit_init_parent(struct path *path)
{
	struct inode *inode = path->dentry->d_inode;
	struct audit_parent *parent;
	int ret;

	parent = kzalloc(sizeof(*parent), GFP_KERNEL);
	if (unlikely(!parent))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&parent->watches);

	fsnotify_init_mark(&parent->mark, audit_watch_free_mark);
	parent->mark.mask = AUDIT_FS_WATCH;
	ret = fsnotify_add_mark(&parent->mark, audit_watch_group, inode, NULL, 0);
	if (ret < 0) {
		audit_free_parent(parent);
		return ERR_PTR(ret);
	}

	return parent;
}

/* Initialize a watch entry. */
static struct audit_watch *audit_init_watch(char *path)
{
	struct audit_watch *watch;

	watch = kzalloc(sizeof(*watch), GFP_KERNEL);
	if (unlikely(!watch))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&watch->rules);
	atomic_set(&watch->count, 1);
	watch->path = path;
	watch->dev = (dev_t)-1;
	watch->ino = (unsigned long)-1;

	return watch;
}

/* Translate a watch string to kernel respresentation. */
int audit_to_watch(struct audit_krule *krule, char *path, int len, u32 op)
{
	struct audit_watch *watch;

	if (!audit_watch_group)
		return -EOPNOTSUPP;

	if (path[0] != '/' || path[len-1] == '/' ||
	    krule->listnr != AUDIT_FILTER_EXIT ||
	    op != Audit_equal ||
	    krule->inode_f || krule->watch || krule->tree)
		return -EINVAL;

	watch = audit_init_watch(path);
	if (IS_ERR(watch))
		return PTR_ERR(watch);

	audit_get_watch(watch);
	krule->watch = watch;

	return 0;
}

/* Duplicate the given audit watch.  The new watch's rules list is initialized
 * to an empty list and wlist is undefined. */
static struct audit_watch *audit_dupe_watch(struct audit_watch *old)
{
	char *path;
	struct audit_watch *new;

	path = kstrdup(old->path, GFP_KERNEL);
	if (unlikely(!path))
		return ERR_PTR(-ENOMEM);

	new = audit_init_watch(path);
	if (IS_ERR(new)) {
		kfree(path);
		goto out;
	}

	new->dev = old->dev;
	new->ino = old->ino;
	audit_get_parent(old->parent);
	new->parent = old->parent;

out:
	return new;
}

static void audit_watch_log_rule_change(struct audit_krule *r, struct audit_watch *w, char *op)
{
	if (audit_enabled) {
		struct audit_buffer *ab;
		ab = audit_log_start(NULL, GFP_NOFS, AUDIT_CONFIG_CHANGE);
		audit_log_format(ab, "auid=%u ses=%u op=",
				 audit_get_loginuid(current),
				 audit_get_sessionid(current));
		audit_log_string(ab, op);
		audit_log_format(ab, " path=");
		audit_log_untrustedstring(ab, w->path);
		audit_log_key(ab, r->filterkey);
		audit_log_format(ab, " list=%d res=1", r->listnr);
		audit_log_end(ab);
	}
}

/* Update inode info in audit rules based on filesystem event. */
static void audit_update_watch(struct audit_parent *parent,
			       const char *dname, dev_t dev,
			       unsigned long ino, unsigned invalidating)
{
	struct audit_watch *owatch, *nwatch, *nextw;
	struct audit_krule *r, *nextr;
	struct audit_entry *oentry, *nentry;

	mutex_lock(&audit_filter_mutex);
	/* Run all of the watches on this parent looking for the one that
	 * matches the given dname */
	list_for_each_entry_safe(owatch, nextw, &parent->watches, wlist) {
		if (audit_compare_dname_path(dname, owatch->path, NULL))
			continue;

		/* If the update involves invalidating rules, do the inode-based
		 * filtering now, so we don't omit records. */
		if (invalidating && !audit_dummy_context())
			audit_filter_inodes(current, current->audit_context);

		/* updating ino will likely change which audit_hash_list we
		 * are on so we need a new watch for the new list */
		nwatch = audit_dupe_watch(owatch);
		if (IS_ERR(nwatch)) {
			mutex_unlock(&audit_filter_mutex);
			audit_panic("error updating watch, skipping");
			return;
		}
		nwatch->dev = dev;
		nwatch->ino = ino;

		list_for_each_entry_safe(r, nextr, &owatch->rules, rlist) {

			oentry = container_of(r, struct audit_entry, rule);
			list_del(&oentry->rule.rlist);
			list_del_rcu(&oentry->list);

			nentry = audit_dupe_rule(&oentry->rule);
			if (IS_ERR(nentry)) {
				list_del(&oentry->rule.list);
				audit_panic("error updating watch, removing");
			} else {
				int h = audit_hash_ino((u32)ino);

				/*
				 * nentry->rule.watch == oentry->rule.watch so
				 * we must drop that reference and set it to our
				 * new watch.
				 */
				audit_put_watch(nentry->rule.watch);
				audit_get_watch(nwatch);
				nentry->rule.watch = nwatch;
				list_add(&nentry->rule.rlist, &nwatch->rules);
				list_add_rcu(&nentry->list, &audit_inode_hash[h]);
				list_replace(&oentry->rule.list,
					     &nentry->rule.list);
			}

			audit_watch_log_rule_change(r, owatch, "updated rules");

			call_rcu(&oentry->rcu, audit_free_rule_rcu);
		}

		audit_remove_watch(owatch);
		goto add_watch_to_parent; /* event applies to a single watch */
	}
	mutex_unlock(&audit_filter_mutex);
	return;

add_watch_to_parent:
	list_add(&nwatch->wlist, &parent->watches);
	mutex_unlock(&audit_filter_mutex);
	return;
}

/* Remove all watches & rules associated with a parent that is going away. */
static void audit_remove_parent_watches(struct audit_parent *parent)
{
	struct audit_watch *w, *nextw;
	struct audit_krule *r, *nextr;
	struct audit_entry *e;

	mutex_lock(&audit_filter_mutex);
	list_for_each_entry_safe(w, nextw, &parent->watches, wlist) {
		list_for_each_entry_safe(r, nextr, &w->rules, rlist) {
			e = container_of(r, struct audit_entry, rule);
			audit_watch_log_rule_change(r, w, "remove rule");
			list_del(&r->rlist);
			list_del(&r->list);
			list_del_rcu(&e->list);
			call_rcu(&e->rcu, audit_free_rule_rcu);
		}
		audit_remove_watch(w);
	}
	mutex_unlock(&audit_filter_mutex);

	fsnotify_destroy_mark(&parent->mark);
}

/* Get path information necessary for adding watches. */
static int audit_get_nd(struct audit_watch *watch, struct path *parent)
{
	struct dentry *d = kern_path_locked(watch->path, parent);
	if (IS_ERR(d))
		return PTR_ERR(d);
	mutex_unlock(&parent->dentry->d_inode->i_mutex);
	if (d->d_inode) {
		/* update watch filter fields */
		watch->dev = d->d_inode->i_sb->s_dev;
		watch->ino = d->d_inode->i_ino;
	}
	dput(d);
	return 0;
}

/* Associate the given rule with an existing parent.
 * Caller must hold audit_filter_mutex. */
static void audit_add_to_parent(struct audit_krule *krule,
				struct audit_parent *parent)
{
	struct audit_watch *w, *watch = krule->watch;
	int watch_found = 0;

	BUG_ON(!mutex_is_locked(&audit_filter_mutex));

	list_for_each_entry(w, &parent->watches, wlist) {
		if (strcmp(watch->path, w->path))
			continue;

		watch_found = 1;

		/* put krule's and initial refs to temporary watch */
		audit_put_watch(watch);
		audit_put_watch(watch);

		audit_get_watch(w);
		krule->watch = watch = w;
		break;
	}

	if (!watch_found) {
		audit_get_parent(parent);
		watch->parent = parent;

		list_add(&watch->wlist, &parent->watches);
	}
	list_add(&krule->rlist, &watch->rules);
}

/* Find a matching watch entry, or add this one.
 * Caller must hold audit_filter_mutex. */
int audit_add_watch(struct audit_krule *krule, struct list_head **list)
{
	struct audit_watch *watch = krule->watch;
	struct audit_parent *parent;
	struct path parent_path;
	int h, ret = 0;

	mutex_unlock(&audit_filter_mutex);

	/* Avoid calling path_lookup under audit_filter_mutex. */
	ret = audit_get_nd(watch, &parent_path);

	/* caller expects mutex locked */
	mutex_lock(&audit_filter_mutex);

	if (ret)
		return ret;

	/* either find an old parent or attach a new one */
	parent = audit_find_parent(parent_path.dentry->d_inode);
	if (!parent) {
		parent = audit_init_parent(&parent_path);
		if (IS_ERR(parent)) {
			ret = PTR_ERR(parent);
			goto error;
		}
	}

	audit_add_to_parent(krule, parent);

	/* match get in audit_find_parent or audit_init_parent */
	audit_put_parent(parent);

	h = audit_hash_ino((u32)watch->ino);
	*list = &audit_inode_hash[h];
error:
	path_put(&parent_path);
	return ret;
}

void audit_remove_watch_rule(struct audit_krule *krule)
{
	struct audit_watch *watch = krule->watch;
	struct audit_parent *parent = watch->parent;

	list_del(&krule->rlist);

	if (list_empty(&watch->rules)) {
		audit_remove_watch(watch);

		if (list_empty(&parent->watches)) {
			audit_get_parent(parent);
			fsnotify_destroy_mark(&parent->mark);
			audit_put_parent(parent);
		}
	}
}

static bool audit_watch_should_send_event(struct fsnotify_group *group, struct inode *inode,
					  struct fsnotify_mark *inode_mark,
					  struct fsnotify_mark *vfsmount_mark,
					  __u32 mask, void *data, int data_type)
{
       return true;
}

/* Update watch data in audit rules based on fsnotify events. */
static int audit_watch_handle_event(struct fsnotify_group *group,
				    struct fsnotify_mark *inode_mark,
				    struct fsnotify_mark *vfsmount_mark,
				    struct fsnotify_event *event)
{
	struct inode *inode;
	__u32 mask = event->mask;
	const char *dname = event->file_name;
	struct audit_parent *parent;

	parent = container_of(inode_mark, struct audit_parent, mark);

	BUG_ON(group != audit_watch_group);

	switch (event->data_type) {
	case (FSNOTIFY_EVENT_PATH):
		inode = event->path.dentry->d_inode;
		break;
	case (FSNOTIFY_EVENT_INODE):
		inode = event->inode;
		break;
	default:
		BUG();
		inode = NULL;
		break;
	};

	if (mask & (FS_CREATE|FS_MOVED_TO) && inode)
		audit_update_watch(parent, dname, inode->i_sb->s_dev, inode->i_ino, 0);
	else if (mask & (FS_DELETE|FS_MOVED_FROM))
		audit_update_watch(parent, dname, (dev_t)-1, (unsigned long)-1, 1);
	else if (mask & (FS_DELETE_SELF|FS_UNMOUNT|FS_MOVE_SELF))
		audit_remove_parent_watches(parent);

	return 0;
}

static const struct fsnotify_ops audit_watch_fsnotify_ops = {
	.should_send_event = 	audit_watch_should_send_event,
	.handle_event = 	audit_watch_handle_event,
	.free_group_priv = 	NULL,
	.freeing_mark = 	NULL,
	.free_event_priv = 	NULL,
};

static int __init audit_watch_init(void)
{
	audit_watch_group = fsnotify_alloc_group(&audit_watch_fsnotify_ops);
	if (IS_ERR(audit_watch_group)) {
		audit_watch_group = NULL;
		audit_panic("cannot create audit fsnotify group");
	}
	return 0;
}
device_initcall(audit_watch_init);
