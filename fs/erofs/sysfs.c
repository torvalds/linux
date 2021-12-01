// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C), 2008-2021, OPPO Mobile Comm Corp., Ltd.
 *             https://www.oppo.com/
 */
#include <linux/sysfs.h>
#include <linux/kobject.h>

#include "internal.h"

enum {
	attr_feature,
	attr_pointer_ui,
	attr_pointer_bool,
};

enum {
	struct_erofs_sb_info,
};

struct erofs_attr {
	struct attribute attr;
	short attr_id;
	int struct_type, offset;
};

#define EROFS_ATTR(_name, _mode, _id)					\
static struct erofs_attr erofs_attr_##_name = {				\
	.attr = {.name = __stringify(_name), .mode = _mode },		\
	.attr_id = attr_##_id,						\
}
#define EROFS_ATTR_FUNC(_name, _mode)	EROFS_ATTR(_name, _mode, _name)
#define EROFS_ATTR_FEATURE(_name)	EROFS_ATTR(_name, 0444, feature)

#define EROFS_ATTR_OFFSET(_name, _mode, _id, _struct)	\
static struct erofs_attr erofs_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.attr_id = attr_##_id,					\
	.struct_type = struct_##_struct,			\
	.offset = offsetof(struct _struct, _name),\
}

#define EROFS_ATTR_RW(_name, _id, _struct)	\
	EROFS_ATTR_OFFSET(_name, 0644, _id, _struct)

#define EROFS_RO_ATTR(_name, _id, _struct)	\
	EROFS_ATTR_OFFSET(_name, 0444, _id, _struct)

#define EROFS_ATTR_RW_UI(_name, _struct)	\
	EROFS_ATTR_RW(_name, pointer_ui, _struct)

#define EROFS_ATTR_RW_BOOL(_name, _struct)	\
	EROFS_ATTR_RW(_name, pointer_bool, _struct)

#define ATTR_LIST(name) (&erofs_attr_##name.attr)

static struct attribute *erofs_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(erofs);

/* Features this copy of erofs supports */
EROFS_ATTR_FEATURE(zero_padding);
EROFS_ATTR_FEATURE(compr_cfgs);
EROFS_ATTR_FEATURE(big_pcluster);
EROFS_ATTR_FEATURE(chunked_file);
EROFS_ATTR_FEATURE(device_table);
EROFS_ATTR_FEATURE(compr_head2);
EROFS_ATTR_FEATURE(sb_chksum);

static struct attribute *erofs_feat_attrs[] = {
	ATTR_LIST(zero_padding),
	ATTR_LIST(compr_cfgs),
	ATTR_LIST(big_pcluster),
	ATTR_LIST(chunked_file),
	ATTR_LIST(device_table),
	ATTR_LIST(compr_head2),
	ATTR_LIST(sb_chksum),
	NULL,
};
ATTRIBUTE_GROUPS(erofs_feat);

static unsigned char *__struct_ptr(struct erofs_sb_info *sbi,
					  int struct_type, int offset)
{
	if (struct_type == struct_erofs_sb_info)
		return (unsigned char *)sbi + offset;
	return NULL;
}

static ssize_t erofs_attr_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct erofs_sb_info *sbi = container_of(kobj, struct erofs_sb_info,
						s_kobj);
	struct erofs_attr *a = container_of(attr, struct erofs_attr, attr);
	unsigned char *ptr = __struct_ptr(sbi, a->struct_type, a->offset);

	switch (a->attr_id) {
	case attr_feature:
		return sysfs_emit(buf, "supported\n");
	case attr_pointer_ui:
		if (!ptr)
			return 0;
		return sysfs_emit(buf, "%u\n", *(unsigned int *)ptr);
	case attr_pointer_bool:
		if (!ptr)
			return 0;
		return sysfs_emit(buf, "%d\n", *(bool *)ptr);
	}
	return 0;
}

static ssize_t erofs_attr_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t len)
{
	struct erofs_sb_info *sbi = container_of(kobj, struct erofs_sb_info,
						s_kobj);
	struct erofs_attr *a = container_of(attr, struct erofs_attr, attr);
	unsigned char *ptr = __struct_ptr(sbi, a->struct_type, a->offset);
	unsigned long t;
	int ret;

	switch (a->attr_id) {
	case attr_pointer_ui:
		if (!ptr)
			return 0;
		ret = kstrtoul(skip_spaces(buf), 0, &t);
		if (ret)
			return ret;
		if (t != (unsigned int)t)
			return -ERANGE;
		*(unsigned int *)ptr = t;
		return len;
	case attr_pointer_bool:
		if (!ptr)
			return 0;
		ret = kstrtoul(skip_spaces(buf), 0, &t);
		if (ret)
			return ret;
		if (t != 0 && t != 1)
			return -EINVAL;
		*(bool *)ptr = !!t;
		return len;
	}
	return 0;
}

static void erofs_sb_release(struct kobject *kobj)
{
	struct erofs_sb_info *sbi = container_of(kobj, struct erofs_sb_info,
						 s_kobj);
	complete(&sbi->s_kobj_unregister);
}

static const struct sysfs_ops erofs_attr_ops = {
	.show	= erofs_attr_show,
	.store	= erofs_attr_store,
};

static struct kobj_type erofs_sb_ktype = {
	.default_groups = erofs_groups,
	.sysfs_ops	= &erofs_attr_ops,
	.release	= erofs_sb_release,
};

static struct kobj_type erofs_ktype = {
	.sysfs_ops	= &erofs_attr_ops,
};

static struct kset erofs_root = {
	.kobj	= {.ktype = &erofs_ktype},
};

static struct kobj_type erofs_feat_ktype = {
	.default_groups = erofs_feat_groups,
	.sysfs_ops	= &erofs_attr_ops,
};

static struct kobject erofs_feat = {
	.kset	= &erofs_root,
};

int erofs_register_sysfs(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	int err;

	sbi->s_kobj.kset = &erofs_root;
	init_completion(&sbi->s_kobj_unregister);
	err = kobject_init_and_add(&sbi->s_kobj, &erofs_sb_ktype, NULL,
				   "%s", sb->s_id);
	if (err)
		goto put_sb_kobj;
	return 0;

put_sb_kobj:
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
	return err;
}

void erofs_unregister_sysfs(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	kobject_del(&sbi->s_kobj);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
}

int __init erofs_init_sysfs(void)
{
	int ret;

	kobject_set_name(&erofs_root.kobj, "erofs");
	erofs_root.kobj.parent = fs_kobj;
	ret = kset_register(&erofs_root);
	if (ret)
		goto root_err;

	ret = kobject_init_and_add(&erofs_feat, &erofs_feat_ktype,
				   NULL, "features");
	if (ret)
		goto feat_err;
	return ret;

feat_err:
	kobject_put(&erofs_feat);
	kset_unregister(&erofs_root);
root_err:
	return ret;
}

void erofs_exit_sysfs(void)
{
	kobject_put(&erofs_feat);
	kset_unregister(&erofs_root);
}
