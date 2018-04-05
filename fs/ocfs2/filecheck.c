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

static DEFINE_SPINLOCK(ocfs2_filecheck_sysfs_lock);
static LIST_HEAD(ocfs2_filecheck_sysfs_list);

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

static ssize_t ocfs2_filecheck_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf);
static ssize_t ocfs2_filecheck_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count);
static struct kobj_attribute ocfs2_attr_filecheck_chk =
					__ATTR(check, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_show,
					ocfs2_filecheck_store);
static struct kobj_attribute ocfs2_attr_filecheck_fix =
					__ATTR(fix, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_show,
					ocfs2_filecheck_store);
static struct kobj_attribute ocfs2_attr_filecheck_set =
					__ATTR(set, S_IRUSR | S_IWUSR,
					ocfs2_filecheck_show,
					ocfs2_filecheck_store);

static void
ocfs2_filecheck_sysfs_free(struct ocfs2_filecheck_sysfs_entry *entry)
{
	struct ocfs2_filecheck_entry *p;

	if (!atomic_dec_and_test(&entry->fs_count)) {
		wait_var_event(&entry->fs_count,
			       !atomic_read(&entry->fs_count));
	}

	spin_lock(&entry->fs_fcheck->fc_lock);
	while (!list_empty(&entry->fs_fcheck->fc_head)) {
		p = list_first_entry(&entry->fs_fcheck->fc_head,
				     struct ocfs2_filecheck_entry, fe_list);
		list_del(&p->fe_list);
		BUG_ON(!p->fe_done); /* To free a undone file check entry */
		kfree(p);
	}
	spin_unlock(&entry->fs_fcheck->fc_lock);

	kset_unregister(entry->fs_fcheckkset);
	kset_unregister(entry->fs_devicekset);
	kfree(entry->fs_fcheck);
	kfree(entry);
}

static void
ocfs2_filecheck_sysfs_add(struct ocfs2_filecheck_sysfs_entry *entry)
{
	spin_lock(&ocfs2_filecheck_sysfs_lock);
	list_add_tail(&entry->fs_list, &ocfs2_filecheck_sysfs_list);
	spin_unlock(&ocfs2_filecheck_sysfs_lock);
}

static int ocfs2_filecheck_sysfs_del(const char *devname)
{
	struct ocfs2_filecheck_sysfs_entry *p;

	spin_lock(&ocfs2_filecheck_sysfs_lock);
	list_for_each_entry(p, &ocfs2_filecheck_sysfs_list, fs_list) {
		if (!strcmp(p->fs_sb->s_id, devname)) {
			list_del(&p->fs_list);
			spin_unlock(&ocfs2_filecheck_sysfs_lock);
			ocfs2_filecheck_sysfs_free(p);
			return 0;
		}
	}
	spin_unlock(&ocfs2_filecheck_sysfs_lock);
	return 1;
}

static void
ocfs2_filecheck_sysfs_put(struct ocfs2_filecheck_sysfs_entry *entry)
{
	if (atomic_dec_and_test(&entry->fs_count))
		wake_up_var(&entry->fs_count);
}

static struct ocfs2_filecheck_sysfs_entry *
ocfs2_filecheck_sysfs_get(const char *devname)
{
	struct ocfs2_filecheck_sysfs_entry *p = NULL;

	spin_lock(&ocfs2_filecheck_sysfs_lock);
	list_for_each_entry(p, &ocfs2_filecheck_sysfs_list, fs_list) {
		if (!strcmp(p->fs_sb->s_id, devname)) {
			atomic_inc(&p->fs_count);
			spin_unlock(&ocfs2_filecheck_sysfs_lock);
			return p;
		}
	}
	spin_unlock(&ocfs2_filecheck_sysfs_lock);
	return NULL;
}

int ocfs2_filecheck_create_sysfs(struct super_block *sb)
{
	int ret = 0;
	struct kset *device_kset = NULL;
	struct kset *fcheck_kset = NULL;
	struct ocfs2_filecheck *fcheck = NULL;
	struct ocfs2_filecheck_sysfs_entry *entry = NULL;
	struct attribute **attrs = NULL;
	struct attribute_group attrgp;

	if (!ocfs2_kset)
		return -ENOMEM;

	attrs = kmalloc(sizeof(struct attribute *) * 4, GFP_NOFS);
	if (!attrs) {
		ret = -ENOMEM;
		goto error;
	} else {
		attrs[0] = &ocfs2_attr_filecheck_chk.attr;
		attrs[1] = &ocfs2_attr_filecheck_fix.attr;
		attrs[2] = &ocfs2_attr_filecheck_set.attr;
		attrs[3] = NULL;
		memset(&attrgp, 0, sizeof(attrgp));
		attrgp.attrs = attrs;
	}

	fcheck = kmalloc(sizeof(struct ocfs2_filecheck), GFP_NOFS);
	if (!fcheck) {
		ret = -ENOMEM;
		goto error;
	} else {
		INIT_LIST_HEAD(&fcheck->fc_head);
		spin_lock_init(&fcheck->fc_lock);
		fcheck->fc_max = OCFS2_FILECHECK_MINSIZE;
		fcheck->fc_size = 0;
		fcheck->fc_done = 0;
	}

	if (strlen(sb->s_id) <= 0) {
		mlog(ML_ERROR,
		"Cannot get device basename when create filecheck sysfs\n");
		ret = -ENODEV;
		goto error;
	}

	device_kset = kset_create_and_add(sb->s_id, NULL, &ocfs2_kset->kobj);
	if (!device_kset) {
		ret = -ENOMEM;
		goto error;
	}

	fcheck_kset = kset_create_and_add("filecheck", NULL,
					  &device_kset->kobj);
	if (!fcheck_kset) {
		ret = -ENOMEM;
		goto error;
	}

	ret = sysfs_create_group(&fcheck_kset->kobj, &attrgp);
	if (ret)
		goto error;

	entry = kmalloc(sizeof(struct ocfs2_filecheck_sysfs_entry), GFP_NOFS);
	if (!entry) {
		ret = -ENOMEM;
		goto error;
	} else {
		atomic_set(&entry->fs_count, 1);
		entry->fs_sb = sb;
		entry->fs_devicekset = device_kset;
		entry->fs_fcheckkset = fcheck_kset;
		entry->fs_fcheck = fcheck;
		ocfs2_filecheck_sysfs_add(entry);
	}

	kfree(attrs);
	return 0;

error:
	kfree(attrs);
	kfree(entry);
	kfree(fcheck);
	kset_unregister(fcheck_kset);
	kset_unregister(device_kset);
	return ret;
}

int ocfs2_filecheck_remove_sysfs(struct super_block *sb)
{
	return ocfs2_filecheck_sysfs_del(sb->s_id);
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
		mlog(ML_ERROR,
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

static ssize_t ocfs2_filecheck_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{

	ssize_t ret = 0, total = 0, remain = PAGE_SIZE;
	unsigned int type;
	struct ocfs2_filecheck_entry *p;
	struct ocfs2_filecheck_sysfs_entry *ent;

	if (ocfs2_filecheck_type_parse(attr->attr.name, &type))
		return -EINVAL;

	ent = ocfs2_filecheck_sysfs_get(kobj->parent->name);
	if (!ent) {
		mlog(ML_ERROR,
		"Cannot get the corresponding entry via device basename %s\n",
		kobj->name);
		return -ENODEV;
	}

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
	ocfs2_filecheck_sysfs_put(ent);
	return total;
}

static int
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
	entry->fe_done = 1;
	spin_lock(&ent->fs_fcheck->fc_lock);
	ent->fs_fcheck->fc_done++;
	spin_unlock(&ent->fs_fcheck->fc_lock);
}

static unsigned int
ocfs2_filecheck_handle(struct super_block *sb,
		       unsigned long ino, unsigned int flags)
{
	unsigned int ret = OCFS2_FILECHECK_ERR_SUCCESS;
	struct inode *inode = NULL;
	int rc;

	inode = ocfs2_iget(OCFS2_SB(sb), ino, flags, 0);
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
	if (entry->fe_type == OCFS2_FILECHECK_TYPE_CHK)
		entry->fe_status = ocfs2_filecheck_handle(ent->fs_sb,
				entry->fe_ino, OCFS2_FI_FLAG_FILECHECK_CHK);
	else if (entry->fe_type == OCFS2_FILECHECK_TYPE_FIX)
		entry->fe_status = ocfs2_filecheck_handle(ent->fs_sb,
				entry->fe_ino, OCFS2_FI_FLAG_FILECHECK_FIX);
	else
		entry->fe_status = OCFS2_FILECHECK_ERR_UNSUPPORTED;

	ocfs2_filecheck_done_entry(ent, entry);
}

static ssize_t ocfs2_filecheck_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	struct ocfs2_filecheck_args args;
	struct ocfs2_filecheck_entry *entry;
	struct ocfs2_filecheck_sysfs_entry *ent;
	ssize_t ret = 0;

	if (count == 0)
		return count;

	if (ocfs2_filecheck_args_parse(attr->attr.name, buf, count, &args)) {
		mlog(ML_ERROR, "Invalid arguments for online file check\n");
		return -EINVAL;
	}

	ent = ocfs2_filecheck_sysfs_get(kobj->parent->name);
	if (!ent) {
		mlog(ML_ERROR,
		"Cannot get the corresponding entry via device basename %s\n",
		kobj->parent->name);
		return -ENODEV;
	}

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
	if ((ent->fs_fcheck->fc_size >= ent->fs_fcheck->fc_max) &&
	    (ent->fs_fcheck->fc_done == 0)) {
		mlog(ML_ERROR,
		"Cannot do more file check "
		"since file check queue(%u) is full now\n",
		ent->fs_fcheck->fc_max);
		ret = -EBUSY;
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
	ocfs2_filecheck_sysfs_put(ent);
	return (!ret ? count : ret);
}
