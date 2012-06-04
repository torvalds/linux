/*
 * Copyright (C) 2012
 * Sachin Bhamare <sbhamare@panasas.com>
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License 2 as published by
 * the Free Software Foundation.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the:
 *	Free Software Foundation <licensing@fsf.org>
 */

#include <linux/kobject.h>
#include <linux/device.h>

#include "exofs.h"

struct odev_attr {
	struct attribute attr;
	ssize_t (*show)(struct exofs_dev *, char *);
	ssize_t (*store)(struct exofs_dev *, const char *, size_t);
};

static ssize_t odev_attr_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
	struct exofs_dev *edp = container_of(kobj, struct exofs_dev, ed_kobj);
	struct odev_attr *a = container_of(attr, struct odev_attr, attr);

	return a->show ? a->show(edp, buf) : 0;
}

static ssize_t odev_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t len)
{
	struct exofs_dev *edp = container_of(kobj, struct exofs_dev, ed_kobj);
	struct odev_attr *a = container_of(attr, struct odev_attr, attr);

	return a->store ? a->store(edp, buf, len) : len;
}

static const struct sysfs_ops odev_attr_ops = {
	.show  = odev_attr_show,
	.store = odev_attr_store,
};


static struct kset *exofs_kset;

static ssize_t osdname_show(struct exofs_dev *edp, char *buf)
{
	struct osd_dev *odev = edp->ored.od;
	const struct osd_dev_info *odi = osduld_device_info(odev);

	return snprintf(buf, odi->osdname_len + 1, "%s", odi->osdname);
}

static ssize_t systemid_show(struct exofs_dev *edp, char *buf)
{
	struct osd_dev *odev = edp->ored.od;
	const struct osd_dev_info *odi = osduld_device_info(odev);

	memcpy(buf, odi->systemid, odi->systemid_len);
	return odi->systemid_len;
}

static ssize_t uri_show(struct exofs_dev *edp, char *buf)
{
	return snprintf(buf, edp->urilen, "%s", edp->uri);
}

static ssize_t uri_store(struct exofs_dev *edp, const char *buf, size_t len)
{
	edp->urilen = strlen(buf) + 1;
	edp->uri = krealloc(edp->uri, edp->urilen, GFP_KERNEL);
	strncpy(edp->uri, buf, edp->urilen);
	return edp->urilen;
}

#define OSD_ATTR(name, mode, show, store) \
	static struct odev_attr odev_attr_##name = \
					__ATTR(name, mode, show, store)

OSD_ATTR(osdname, S_IRUGO, osdname_show, NULL);
OSD_ATTR(systemid, S_IRUGO, systemid_show, NULL);
OSD_ATTR(uri, S_IRWXU, uri_show, uri_store);

static struct attribute *odev_attrs[] = {
	&odev_attr_osdname.attr,
	&odev_attr_systemid.attr,
	&odev_attr_uri.attr,
	NULL,
};

static struct kobj_type odev_ktype = {
	.default_attrs	= odev_attrs,
	.sysfs_ops	= &odev_attr_ops,
};

static struct kobj_type uuid_ktype = {
};

void exofs_sysfs_dbg_print()
{
#ifdef CONFIG_EXOFS_DEBUG
	struct kobject *k_name, *k_tmp;

	list_for_each_entry_safe(k_name, k_tmp, &exofs_kset->list, entry) {
		printk(KERN_INFO "%s: name %s ref %d\n",
			__func__, kobject_name(k_name),
			(int)atomic_read(&k_name->kref.refcount));
	}
#endif
}
/*
 * This function removes all kobjects under exofs_kset
 * At the end of it, exofs_kset kobject will have a refcount
 * of 1 which gets decremented only on exofs module unload
 */
void exofs_sysfs_sb_del(struct exofs_sb_info *sbi)
{
	struct kobject *k_name, *k_tmp;
	struct kobject *s_kobj = &sbi->s_kobj;

	list_for_each_entry_safe(k_name, k_tmp, &exofs_kset->list, entry) {
		/* Remove all that are children of this SBI */
		if (k_name->parent == s_kobj)
			kobject_put(k_name);
	}
	kobject_put(s_kobj);
}

/*
 * This function creates sysfs entries to hold the current exofs cluster
 * instance (uniquely identified by osdname,pid tuple).
 * This function gets called once per exofs mount instance.
 */
int exofs_sysfs_sb_add(struct exofs_sb_info *sbi,
		       struct exofs_dt_device_info *dt_dev)
{
	struct kobject *s_kobj;
	int retval = 0;
	uint64_t pid = sbi->one_comp.obj.partition;

	/* allocate new uuid dirent */
	s_kobj = &sbi->s_kobj;
	s_kobj->kset = exofs_kset;
	retval = kobject_init_and_add(s_kobj, &uuid_ktype,
			&exofs_kset->kobj,  "%s_%llx", dt_dev->osdname, pid);
	if (retval) {
		EXOFS_ERR("ERROR: Failed to create sysfs entry for "
			  "uuid-%s_%llx => %d\n", dt_dev->osdname, pid, retval);
		return -ENOMEM;
	}
	return 0;
}

int exofs_sysfs_odev_add(struct exofs_dev *edev, struct exofs_sb_info *sbi)
{
	struct kobject *d_kobj;
	int retval = 0;

	/* create osd device group which contains following attributes
	 * osdname, systemid & uri
	 */
	d_kobj = &edev->ed_kobj;
	d_kobj->kset = exofs_kset;
	retval = kobject_init_and_add(d_kobj, &odev_ktype,
			&sbi->s_kobj, "dev%u", edev->did);
	if (retval) {
		EXOFS_ERR("ERROR: Failed to create sysfs entry for "
				"device dev%u\n", edev->did);
		return retval;
	}
	return 0;
}

int exofs_sysfs_init(void)
{
	exofs_kset = kset_create_and_add("exofs", NULL, fs_kobj);
	if (!exofs_kset) {
		EXOFS_ERR("ERROR: kset_create_and_add exofs failed\n");
		return -ENOMEM;
	}
	return 0;
}

void exofs_sysfs_uninit(void)
{
	kset_unregister(exofs_kset);
}
