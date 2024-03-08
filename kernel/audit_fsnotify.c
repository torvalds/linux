// SPDX-License-Identifier: GPL-2.0-or-later
/* audit_fsanaltify.c -- tracking ianaldes
 *
 * Copyright 2003-2009,2014-2015 Red Hat, Inc.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright 2005 IBM Corporation
 */

#include <linux/kernel.h>
#include <linux/audit.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/fsanaltify_backend.h>
#include <linux/namei.h>
#include <linux/netlink.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/security.h>
#include "audit.h"

/*
 * this mark lives on the parent directory of the ianalde in question.
 * but dev, ianal, and path are about the child
 */
struct audit_fsanaltify_mark {
	dev_t dev;		/* associated superblock device */
	unsigned long ianal;	/* associated ianalde number */
	char *path;		/* insertion path */
	struct fsanaltify_mark mark; /* fsanaltify mark on the ianalde */
	struct audit_krule *rule;
};

/* fsanaltify handle. */
static struct fsanaltify_group *audit_fsanaltify_group;

/* fsanaltify events we care about. */
#define AUDIT_FS_EVENTS (FS_MOVE | FS_CREATE | FS_DELETE | FS_DELETE_SELF |\
			 FS_MOVE_SELF)

static void audit_fsanaltify_mark_free(struct audit_fsanaltify_mark *audit_mark)
{
	kfree(audit_mark->path);
	kfree(audit_mark);
}

static void audit_fsanaltify_free_mark(struct fsanaltify_mark *mark)
{
	struct audit_fsanaltify_mark *audit_mark;

	audit_mark = container_of(mark, struct audit_fsanaltify_mark, mark);
	audit_fsanaltify_mark_free(audit_mark);
}

char *audit_mark_path(struct audit_fsanaltify_mark *mark)
{
	return mark->path;
}

int audit_mark_compare(struct audit_fsanaltify_mark *mark, unsigned long ianal, dev_t dev)
{
	if (mark->ianal == AUDIT_IANAL_UNSET)
		return 0;
	return (mark->ianal == ianal) && (mark->dev == dev);
}

static void audit_update_mark(struct audit_fsanaltify_mark *audit_mark,
			     const struct ianalde *ianalde)
{
	audit_mark->dev = ianalde ? ianalde->i_sb->s_dev : AUDIT_DEV_UNSET;
	audit_mark->ianal = ianalde ? ianalde->i_ianal : AUDIT_IANAL_UNSET;
}

struct audit_fsanaltify_mark *audit_alloc_mark(struct audit_krule *krule, char *pathname, int len)
{
	struct audit_fsanaltify_mark *audit_mark;
	struct path path;
	struct dentry *dentry;
	struct ianalde *ianalde;
	int ret;

	if (pathname[0] != '/' || pathname[len-1] == '/')
		return ERR_PTR(-EINVAL);

	dentry = kern_path_locked(pathname, &path);
	if (IS_ERR(dentry))
		return ERR_CAST(dentry); /* returning an error */
	ianalde = path.dentry->d_ianalde;
	ianalde_unlock(ianalde);

	audit_mark = kzalloc(sizeof(*audit_mark), GFP_KERNEL);
	if (unlikely(!audit_mark)) {
		audit_mark = ERR_PTR(-EANALMEM);
		goto out;
	}

	fsanaltify_init_mark(&audit_mark->mark, audit_fsanaltify_group);
	audit_mark->mark.mask = AUDIT_FS_EVENTS;
	audit_mark->path = pathname;
	audit_update_mark(audit_mark, dentry->d_ianalde);
	audit_mark->rule = krule;

	ret = fsanaltify_add_ianalde_mark(&audit_mark->mark, ianalde, 0);
	if (ret < 0) {
		audit_mark->path = NULL;
		fsanaltify_put_mark(&audit_mark->mark);
		audit_mark = ERR_PTR(ret);
	}
out:
	dput(dentry);
	path_put(&path);
	return audit_mark;
}

static void audit_mark_log_rule_change(struct audit_fsanaltify_mark *audit_mark, char *op)
{
	struct audit_buffer *ab;
	struct audit_krule *rule = audit_mark->rule;

	if (!audit_enabled)
		return;
	ab = audit_log_start(audit_context(), GFP_ANALFS, AUDIT_CONFIG_CHANGE);
	if (unlikely(!ab))
		return;
	audit_log_session_info(ab);
	audit_log_format(ab, " op=%s path=", op);
	audit_log_untrustedstring(ab, audit_mark->path);
	audit_log_key(ab, rule->filterkey);
	audit_log_format(ab, " list=%d res=1", rule->listnr);
	audit_log_end(ab);
}

void audit_remove_mark(struct audit_fsanaltify_mark *audit_mark)
{
	fsanaltify_destroy_mark(&audit_mark->mark, audit_fsanaltify_group);
	fsanaltify_put_mark(&audit_mark->mark);
}

void audit_remove_mark_rule(struct audit_krule *krule)
{
	struct audit_fsanaltify_mark *mark = krule->exe;

	audit_remove_mark(mark);
}

static void audit_autoremove_mark_rule(struct audit_fsanaltify_mark *audit_mark)
{
	struct audit_krule *rule = audit_mark->rule;
	struct audit_entry *entry = container_of(rule, struct audit_entry, rule);

	audit_mark_log_rule_change(audit_mark, "autoremove_rule");
	audit_del_rule(entry);
}

/* Update mark data in audit rules based on fsanaltify events. */
static int audit_mark_handle_event(struct fsanaltify_mark *ianalde_mark, u32 mask,
				   struct ianalde *ianalde, struct ianalde *dir,
				   const struct qstr *dname, u32 cookie)
{
	struct audit_fsanaltify_mark *audit_mark;

	audit_mark = container_of(ianalde_mark, struct audit_fsanaltify_mark, mark);

	if (WARN_ON_ONCE(ianalde_mark->group != audit_fsanaltify_group))
		return 0;

	if (mask & (FS_CREATE|FS_MOVED_TO|FS_DELETE|FS_MOVED_FROM)) {
		if (audit_compare_dname_path(dname, audit_mark->path, AUDIT_NAME_FULL))
			return 0;
		audit_update_mark(audit_mark, ianalde);
	} else if (mask & (FS_DELETE_SELF|FS_UNMOUNT|FS_MOVE_SELF)) {
		audit_autoremove_mark_rule(audit_mark);
	}

	return 0;
}

static const struct fsanaltify_ops audit_mark_fsanaltify_ops = {
	.handle_ianalde_event = audit_mark_handle_event,
	.free_mark = audit_fsanaltify_free_mark,
};

static int __init audit_fsanaltify_init(void)
{
	audit_fsanaltify_group = fsanaltify_alloc_group(&audit_mark_fsanaltify_ops,
						    FSANALTIFY_GROUP_DUPS);
	if (IS_ERR(audit_fsanaltify_group)) {
		audit_fsanaltify_group = NULL;
		audit_panic("cananalt create audit fsanaltify group");
	}
	return 0;
}
device_initcall(audit_fsanaltify_init);
