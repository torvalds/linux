/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * filecheck.c
 *
 * Code which implements online file check.
 *
 * Copyright (C) 2016 SuSE.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/sysctl.h>
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "ocfs2_fs.h"
#include "stackglue.h"
#include "inode.h"

#include "filecheck.h"


/* File check error strings,
 * must correspond with error number in header file.
 */
static const char * const ocfs2_filecheck_errs[] = {
	"SUCCESS",
	"FAILED",
	"INPROGRESS",
	"READONLY",
	"INJBD",
	"INVALIDINO",
	"BLOCKECC",
	"BLOCKNO",
	"VALIDFLAG",
	"GENERATION",
	"UNSUPPORTED"
};

struct ocfs2_filecheck_entry {
	struct list_head fe_list;
	unsigned long fe_ino;
	unsigned int fe_type;
	unsigned int fe_done:1;
	unsigned int fe_status:31;
};

struct ocfs2_filecheck_args {
	unsigned int fa_type;
	union {
		unsigned long fa_ino;
		unsigned int fa_len;
	};
};

static const char *
ocfs2_filecheck_error(int errno)
{
	if (!errno)
		return ocfs2_filecheck_errs[errno];

	BUG_ON(errno < OCFS2_FILECHECK_ERR_START ||
	       errno > OCFS2_FILECHECK_ERR_END);
	return ocfs2_filecheck_errs[errno - OCFS2_FILECHECK_ERR_START + 1];
}

static ssize_t ocfs2_filecheck_attr_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf);
static ssize_t ocfs2_filecheck_attr_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count);
static struct kobj_attribute ocfs2_filecheck_attr_chk =
					__ATTR(check, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_attr_show,
					ocfs2_filecheck_attr_store);
static struct kobj_attribute ocfs2_filecheck_attr_fix =
					__ATTR(fix, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_attr_show,
					ocfs2_filecheck_attr_store);
static struct kobj_attribute ocfs2_filecheck_attr_set =
					__ATTR(set, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_attr_show,
					ocfs2_filecheck_attr_store);
static struct attribute *ocfs2_filecheck_attrs[] = {
	&ocfs2_filecheck_attr_chk.attr,
	&ocfs2_filecheck_attr_fix.attr,
	&ocfs2_filecheck_attr_set.attr,
	NULL
};

static void ocfs2_filecheck_release(struct kobject *kobj)
{
	struct ocfs2_filecheck_sysfs_entry *entry = container_of(kobj,
				struct ocfs2_filecheck_sysfs_entry, fs_kobj);

	complete(&entry->fs_kobj_unregister);
}

static ssize_t
ocfs2_filecheck_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	ssize_t ret = -EIO;
	struct kobj_attribute *kattr = container_of(attr,
					struct kobj_attribute, attr);

	kobject_get(kobj);
	if (kattr->show)
		ret = kattr->show(kobj, kattr, buf);
	kobject_put(kobj);
	return ret;
}

static ssize_t
ocfs2_filecheck_store(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count)
{
	ssize_t ret = -EIO;
	struct kobj_attribute *kattr = container_of(attr,
					struct kobj_attribute, attr);

	kobject_get(kobj);
	if (kattr->store)
		ret = kattr->store(kobj, kattr, buf, count);
	kobject_put(kobj);
	return ret;
}

static const struct sysfs_ops ocfs2_filecheck_ops = {
	.show = ocfs2_filecheck_show,
	.store = ocfs2_filecheck_store,
};

static struct kobj_type ocfs2_ktype_filecheck = {
	.default_attrs = ocfs2_filecheck_attrs,
	.sysfs_ops = &ocfs2_filecheck_ops,
	.release = ocfs2_filecheck_release,
};

static void
ocfs2_filecheck_sysfs_free(struct ocfs2_filecheck_sysfs_entry *entry)
{
	struct ocfs2_filecheck_entry *p;

	spin_lock(&entry->fs_fcheck->fc_lock);
	while (!list_empty(&entry->fs_fcheck->fc_head)) {
		p = list_first_entry(&entry->fs_fcheck->fc_head,
				     struct ocfs2_filecheck_entry, fe_list);
		list_del(&p->fe_list);
		BUG_ON(!p->fe_done); /* To free a undone file check entry */
		kfree(p);
	}
	spin_unlock(&entry->fs_fcheck->fc_lock);

	kfree(entry->fs_fcheck);
	entry->fs_fcheck = NULL;
}

int ocfs2_filecheck_create_sysfs(struct ocfs2_super *osb)
{
	int ret;
	struct ocfs2_filecheck *fcheck;
	struct ocfs2_filecheck_sysfs_entry *entry = &osb->osb_fc_ent;

	fcheck = kmalloc(sizeof(struct ocfs2_filecheck), GFP_NOFS);
	if (!fcheck)
		return -ENOMEM;

	INIT_LIST_HEAD(&fcheck->fc_head);
	spin_lock_init(&fcheck->fc_lock);
	fcheck->fc_max = OCFS2_FILECHECK_MINSIZE;
	fcheck->fc_size = 0;
	fcheck->fc_done = 0;

	entry->fs_kobj.kset = osb->osb_dev_kset;
	init_completion(&entry->fs_kobj_unregister);
	ret = kobject_init_and_add(&entry->fs_kobj, &ocfs2_ktype_filecheck,
					NULL, "filecheck");
	if (ret) {
		kfree(fcheck);
		return ret;
	}

	entry->fs_fcheck = fcheck;
	return 0;
}

void ocfs2_filecheck_remove_sysfs(struct ocfs2_super *osb)
{
	if (!osb->osb_fc_ent.fs_fcheck)
		return;

	kobject_del(&osb->osb_fc_ent.fs_kobj);
	kobject_put(&osb->osb_fc_ent.fs_kobj);
	wait_for_completion(&osb->osb_fc_ent.fs_kobj_unregister);
	ocfs2_filecheck_sysfs_free(&osb->osb_fc_ent);
}

static int
ocfs2_filecheck_erase_entries(struct ocfs2_filecheck_sysfs_entry *ent,
			      unsigned int count);
static int
ocfs2_filecheck_adjust_max(struct ocfs2_filecheck_sysfs_entry *ent,
			   unsigned int len)
{
	int ret;

	if ((len < OCFS2_FILECHECK_MINSIZE) || (len > OCFS2_FILECHECK_MAXSIZE))
		return -EINVAL;

	spin_lock(&ent->fs_fcheck->fc_lock);
	if (len < (ent->fs_fcheck->fc_size - ent->fs_fcheck->fc_done)) {
		mlog(ML_NOTICE,
		"Cannot set online file check maximum entry number "
		"to %u due to too many pending entries(%u)\n",
		len, ent->fs_fcheck->fc_size - ent->fs_fcheck->fc_done);
		ret = -EBUSY;
	} else {
		if (len < ent->fs_fcheck->fc_size)
			BUG_ON(!ocfs2_filecheck_erase_entries(ent,
				ent->fs_fcheck->fc_size - len));

		ent->fs_fcheck->fc_max = len;
		ret = 0;
	}
	spin_unlock(&ent->fs_fcheck->fc_lock);

	return ret;
}

#define OCFS2_FILECHECK_ARGS_LEN	24
static int
ocfs2_filecheck_args_get_long(const char *buf, size_t count,
			      unsigned long *val)
{
	char buffer[OCFS2_FILECHECK_ARGS_LEN];

	memcpy(buffer, buf, count);
	buffer[count] = '\0';

	if (kstrtoul(buffer, 0, val))
		return 1;

	return 0;
}

static int
ocfs2_filecheck_type_parse(const char *name, unsigned int *type)
{
	if (!strncmp(name, "fix", 4))
		*type = OCFS2_FILECHECK_TYPE_FIX;
	else if (!strncmp(name, "check", 6))
		*type = OCFS2_FILECHECK_TYPE_CHK;
	else if (!strncmp(name, "set", 4))
		*type = OCFS2_FILECHECK_TYPE_SET;
	else
		return 1;

	return 0;
}

static int
ocfs2_filecheck_args_parse(const char *name, const char *buf, size_t count,
			   struct ocfs2_filecheck_args *args)
{
	unsigned long val = 0;
	unsigned int type;

	/* too short/long args length */
	if ((count < 1) || (count >= OCFS2_FILECHECK_ARGS_LEN))
		return 1;

	if (ocfs2_filecheck_type_parse(name, &type))
		return 1;
	if (ocfs2_filecheck_args_get_long(buf, count, &val))
		return 1;

	if (val <= 0)
		return 1;

	args->fa_type = type;
	if (type == OCFS2_FILECHECK_TYPE_SET)
		args->fa_len = (unsigned int)val;
	else
		args->fa_ino = val;

	return 0;
}

static ssize_t ocfs2_filecheck_attr_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{

	ssize_t ret = 0, total = 0, remain = PAGE_SIZE;
	unsigned int type;
	struct ocfs2_filecheck_entry *p;
	struct ocfs2_filecheck_sysfs_entry *ent = container_of(kobj,
				struct ocfs2_filecheck_sysfs_entry, fs_kobj);

	if (ocfs2_filecheck_type_parse(attr->attr.name, &type))
		return -EINVAL;

	if (type == OCFS2_FILECHECK_TYPE_SET) {
		spin_lock(&ent->fs_fcheck->fc_lock);
		total = snprintf(buf, remain, "%u\n", ent->fs_fcheck->fc_max);
		spin_unlock(&ent->fs_fcheck->fc_lock);
		goto exit;
	}

	ret = snprintf(buf, remain, "INO\t\tDONE\tERROR\n");
	total += ret;
	remain -= ret;
	spin_lock(&ent->fs_fcheck->fc_lock);
	list_for_each_entry(p, &ent->fs_fcheck->fc_head, fe_list) {
		if (p->fe_type != type)
			continue;

		ret = snprintf(buf + total, remain, "%lu\t\t%u\t%s\n",
			       p->fe_ino, p->fe_done,
			       ocfs2_filecheck_error(p->fe_status));
		if (ret < 0) {
			total = ret;
			break;
		}
		if (ret == remain) {
			/* snprintf() didn't fit */
			total = -E2BIG;
			break;
		}
		total += ret;
		remain -= ret;
	}
	spin_unlock(&ent->fs_fcheck->fc_lock);

exit:
	return total;
}

static inline int
ocfs2_filecheck_is_dup_entry(struct ocfs2_filecheck_sysfs_entry *ent,
				unsigned long ino)
{
	struct ocfs2_filecheck_entry *p;

	list_for_each_entry(p, &ent->fs_fcheck->fc_head, fe_list) {
		if (!p->fe_done) {
			if (p->fe_ino == ino)
				return 1;
		}
	}

	return 0;
}

static inline int
ocfs2_filecheck_erase_entry(struct ocfs2_filecheck_sysfs_entry *ent)
{
	struct ocfs2_filecheck_entry *p;

	list_for_each_entry(p, &ent->fs_fcheck->fc_head, fe_list) {
		if (p->fe_done) {
			list_del(&p->fe_list);
			kfree(p);
			ent->fs_fcheck->fc_size--;
			ent->fs_fcheck->fc_done--;
			return 1;
		}
	}

	return 0;
}

static int
ocfs2_filecheck_erase_entries(struct ocfs2_filecheck_sysfs_entry *ent,
			      unsigned int count)
{
	unsigned int i = 0;
	unsigned int ret = 0;

	while (i++ < count) {
		if (ocfs2_filecheck_erase_entry(ent))
			ret++;
		else
			break;
	}

	return (ret == count ? 1 : 0);
}

static void
ocfs2_filecheck_done_entry(struct ocfs2_filecheck_sysfs_entry *ent,
			   struct ocfs2_filecheck_entry *entry)
{
	spin_lock(&ent->fs_fcheck->fc_lock);
	entry->fe_done = 1;
	ent->fs_fcheck->fc_done++;
	spin_unlock(&ent->fs_fcheck->fc_lock);
}

static unsigned int
ocfs2_filecheck_handle(struct ocfs2_super *osb,
		       unsigned long ino, unsigned int flags)
{
	unsigned int ret = OCFS2_FILECHECK_ERR_SUCCESS;
	struct inode *inode = NULL;
	int rc;

	inode = ocfs2_iget(osb, ino, flags, 0);
	if (IS_ERR(inode)) {
		rc = (int)(-(long)inode);
		if (rc >= OCFS2_FILECHECK_ERR_START &&
		    rc < OCFS2_FILECHECK_ERR_END)
			ret = rc;
		else
			ret = OCFS2_FILECHECK_ERR_FAILED;
	} else
		iput(inode);

	return ret;
}

static void
ocfs2_filecheck_handle_entry(struct ocfs2_filecheck_sysfs_entry *ent,
			     struct ocfs2_filecheck_entry *entry)
{
	struct ocfs2_super *osb = container_of(ent, struct ocfs2_super,
						osb_fc_ent);

	if (entry->fe_type == OCFS2_FILECHECK_TYPE_CHK)
		entry->fe_status = ocfs2_filecheck_handle(osb,
				entry->fe_ino, OCFS2_FI_FLAG_FILECHECK_CHK);
	else if (entry->fe_type == OCFS2_FILECHECK_TYPE_FIX)
		entry->fe_status = ocfs2_filecheck_handle(osb,
				entry->fe_ino, OCFS2_FI_FLAG_FILECHECK_FIX);
	else
		entry->fe_status = OCFS2_FILECHECK_ERR_UNSUPPORTED;

	ocfs2_filecheck_done_entry(ent, entry);
}

static ssize_t ocfs2_filecheck_attr_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	ssize_t ret = 0;
	struct ocfs2_filecheck_args args;
	struct ocfs2_filecheck_entry *entry;
	struct ocfs2_filecheck_sysfs_entry *ent = container_of(kobj,
				struct ocfs2_filecheck_sysfs_entry, fs_kobj);

	if (count == 0)
		return count;

	if (ocfs2_filecheck_args_parse(attr->attr.name, buf, count, &args))
		return -EINVAL;

	if (args.fa_type == OCFS2_FILECHECK_TYPE_SET) {
		ret = ocfs2_filecheck_adjust_max(ent, args.fa_len);
		goto exit;
	}

	entry = kmalloc(sizeof(struct ocfs2_filecheck_entry), GFP_NOFS);
	if (!entry) {
		ret = -ENOMEM;
		goto exit;
	}

	spin_lock(&ent->fs_fcheck->fc_lock);
	if (ocfs2_filecheck_is_dup_entry(ent, args.fa_ino)) {
		ret = -EEXIST;
		kfree(entry);
	} else if ((ent->fs_fcheck->fc_size >= ent->fs_fcheck->fc_max) &&
		(ent->fs_fcheck->fc_done == 0)) {
		mlog(ML_NOTICE,
		"Cannot do more file check "
		"since file check queue(%u) is full now\n",
		ent->fs_fcheck->fc_max);
		ret = -EAGAIN;
		kfree(entry);
	} else {
		if ((ent->fs_fcheck->fc_size >= ent->fs_fcheck->fc_max) &&
		    (ent->fs_fcheck->fc_done > 0)) {
			/* Delete the oldest entry which was done,
			 * make sure the entry size in list does
			 * not exceed maximum value
			 */
			BUG_ON(!ocfs2_filecheck_erase_entry(ent));
		}

		entry->fe_ino = args.fa_ino;
		entry->fe_type = args.fa_type;
		entry->fe_done = 0;
		entry->fe_status = OCFS2_FILECHECK_ERR_INPROGRESS;
		list_add_tail(&entry->fe_list, &ent->fs_fcheck->fc_head);
		ent->fs_fcheck->fc_size++;
	}
	spin_unlock(&ent->fs_fcheck->fc_lock);

	if (!ret)
		ocfs2_filecheck_handle_entry(ent, entry);

exit:
	return (!ret ? count : ret);
}
