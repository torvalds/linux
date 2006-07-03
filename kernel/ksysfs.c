/*
 * kernel/ksysfs.c - sysfs attributes in /sys/kernel, which
 * 		     are not related to any other subsystem
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 * 
 * This file is release under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kexec.h>

#define KERNEL_ATTR_RO(_name) \
static struct subsys_attribute _name##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) \
static struct subsys_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

#if defined(CONFIG_HOTPLUG) && defined(CONFIG_NET)
/* current uevent sequence number */
static ssize_t uevent_seqnum_show(struct subsystem *subsys, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)uevent_seqnum);
}
KERNEL_ATTR_RO(uevent_seqnum);

/* uevent helper program, used during early boo */
static ssize_t uevent_helper_show(struct subsystem *subsys, char *page)
{
	return sprintf(page, "%s\n", uevent_helper);
}
static ssize_t uevent_helper_store(struct subsystem *subsys, const char *page, size_t count)
{
	if (count+1 > UEVENT_HELPER_PATH_LEN)
		return -ENOENT;
	memcpy(uevent_helper, page, count);
	uevent_helper[count] = '\0';
	if (count && uevent_helper[count-1] == '\n')
		uevent_helper[count-1] = '\0';
	return count;
}
KERNEL_ATTR_RW(uevent_helper);
#endif

#ifdef CONFIG_KEXEC
static ssize_t kexec_loaded_show(struct subsystem *subsys, char *page)
{
	return sprintf(page, "%d\n", !!kexec_image);
}
KERNEL_ATTR_RO(kexec_loaded);

static ssize_t kexec_crash_loaded_show(struct subsystem *subsys, char *page)
{
	return sprintf(page, "%d\n", !!kexec_crash_image);
}
KERNEL_ATTR_RO(kexec_crash_loaded);
#endif /* CONFIG_KEXEC */

decl_subsys(kernel, NULL, NULL);
EXPORT_SYMBOL_GPL(kernel_subsys);

static struct attribute * kernel_attrs[] = {
#if defined(CONFIG_HOTPLUG) && defined(CONFIG_NET)
	&uevent_seqnum_attr.attr,
	&uevent_helper_attr.attr,
#endif
#ifdef CONFIG_KEXEC
	&kexec_loaded_attr.attr,
	&kexec_crash_loaded_attr.attr,
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
