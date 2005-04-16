/*
 * kernel/ksysfs.c - sysfs attributes in /sys/kernel, which
 * 		     are not related to any other subsystem
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * 
 * This file is release under the GPLv2
 *
 */

#include <linux/config.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>

#define KERNEL_ATTR_RO(_name) \
static struct subsys_attribute _name##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) \
static struct subsys_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

#ifdef CONFIG_HOTPLUG
static ssize_t hotplug_seqnum_show(struct subsystem *subsys, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)hotplug_seqnum);
}
KERNEL_ATTR_RO(hotplug_seqnum);
#endif

decl_subsys(kernel, NULL, NULL);
EXPORT_SYMBOL_GPL(kernel_subsys);

static struct attribute * kernel_attrs[] = {
#ifdef CONFIG_HOTPLUG
	&hotplug_seqnum_attr.attr,
#endif
	NULL
};

static struct attribute_group kernel_attr_group = {
	.attrs = kernel_attrs,
};

static int __init ksysfs_init(void)
{
	int error = subsystem_register(&kernel_subsys);
	if (!error)
		error = sysfs_create_group(&kernel_subsys.kset.kobj,
					   &kernel_attr_group);

	return error;
}

core_initcall(ksysfs_init);
