/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include "masklog.h"

struct mlog_bits mlog_and_bits = MLOG_BITS_RHS(MLOG_INITIAL_AND_MASK);
EXPORT_SYMBOL_GPL(mlog_and_bits);
struct mlog_bits mlog_not_bits = MLOG_BITS_RHS(MLOG_INITIAL_NOT_MASK);
EXPORT_SYMBOL_GPL(mlog_not_bits);

static ssize_t mlog_mask_show(u64 mask, char *buf)
{
	char *state;

	if (__mlog_test_u64(mask, mlog_and_bits))
		state = "allow";
	else if (__mlog_test_u64(mask, mlog_not_bits))
		state = "deny";
	else
		state = "off";

	return snprintf(buf, PAGE_SIZE, "%s\n", state);
}

static ssize_t mlog_mask_store(u64 mask, const char *buf, size_t count)
{
	if (!strnicmp(buf, "allow", 5)) {
		__mlog_set_u64(mask, mlog_and_bits);
		__mlog_clear_u64(mask, mlog_not_bits);
	} else if (!strnicmp(buf, "deny", 4)) {
		__mlog_set_u64(mask, mlog_not_bits);
		__mlog_clear_u64(mask, mlog_and_bits);
	} else if (!strnicmp(buf, "off", 3)) {
		__mlog_clear_u64(mask, mlog_not_bits);
		__mlog_clear_u64(mask, mlog_and_bits);
	} else
		return -EINVAL;

	return count;
}

struct mlog_attribute {
	struct attribute attr;
	u64 mask;
};

#define to_mlog_attr(_attr) container_of(_attr, struct mlog_attribute, attr)

#define define_mask(_name) {			\
	.attr = {				\
		.name = #_name,			\
		.mode = S_IRUGO | S_IWUSR,	\
	},					\
	.mask = ML_##_name,			\
}

static struct mlog_attribute mlog_attrs[MLOG_MAX_BITS] = {
	define_mask(ENTRY),
	define_mask(EXIT),
	define_mask(TCP),
	define_mask(MSG),
	define_mask(SOCKET),
	define_mask(HEARTBEAT),
	define_mask(HB_BIO),
	define_mask(DLMFS),
	define_mask(DLM),
	define_mask(DLM_DOMAIN),
	define_mask(DLM_THREAD),
	define_mask(DLM_MASTER),
	define_mask(DLM_RECOVERY),
	define_mask(AIO),
	define_mask(JOURNAL),
	define_mask(DISK_ALLOC),
	define_mask(SUPER),
	define_mask(FILE_IO),
	define_mask(EXTENT_MAP),
	define_mask(DLM_GLUE),
	define_mask(BH_IO),
	define_mask(UPTODATE),
	define_mask(NAMEI),
	define_mask(INODE),
	define_mask(VOTE),
	define_mask(DCACHE),
	define_mask(CONN),
	define_mask(QUORUM),
	define_mask(EXPORT),
	define_mask(XATTR),
	define_mask(QUOTA),
	define_mask(REFCOUNT),
	define_mask(BASTS),
	define_mask(ERROR),
	define_mask(NOTICE),
	define_mask(KTHREAD),
};

static struct attribute *mlog_attr_ptrs[MLOG_MAX_BITS] = {NULL, };

static ssize_t mlog_show(struct kobject *obj, struct attribute *attr,
			 char *buf)
{
	struct mlog_attribute *mlog_attr = to_mlog_attr(attr);

	return mlog_mask_show(mlog_attr->mask, buf);
}

static ssize_t mlog_store(struct kobject *obj, struct attribute *attr,
			  const char *buf, size_t count)
{
	struct mlog_attribute *mlog_attr = to_mlog_attr(attr);

	return mlog_mask_store(mlog_attr->mask, buf, count);
}

static struct sysfs_ops mlog_attr_ops = {
	.show  = mlog_show,
	.store = mlog_store,
};

static struct kobj_type mlog_ktype = {
	.default_attrs = mlog_attr_ptrs,
	.sysfs_ops     = &mlog_attr_ops,
};

static struct kset mlog_kset = {
	.kobj   = {.ktype = &mlog_ktype},
};

int mlog_sys_init(struct kset *o2cb_kset)
{
	int i = 0;

	while (mlog_attrs[i].attr.mode) {
		mlog_attr_ptrs[i] = &mlog_attrs[i].attr;
		i++;
	}
	mlog_attr_ptrs[i] = NULL;

	kobject_set_name(&mlog_kset.kobj, "logmask");
	mlog_kset.kobj.kset = o2cb_kset;
	return kset_register(&mlog_kset);
}

void mlog_sys_shutdown(void)
{
	kset_unregister(&mlog_kset);
}
