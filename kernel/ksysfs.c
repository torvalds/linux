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
#include <linux/sched.h>

#define KERNEL_ATTR_RO(_name) \
static struct subsys_attribute _name##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) \
static struct subsys_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

#if defined(CONFIG_HOTPLUG) && defined(CONFIG_NET)
/* current uevent sequence number */
static ssize_t uevent_seqnum_show(struct kset *kset, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)uevent_seqnum);
}
KERNEL_ATTR_RO(uevent_seqnum);

/* uevent helper program, used during early boo */
static ssize_t uevent_helper_show(struct kset *kset, char *page)
{
	return sprintf(page, "%s\n", uevent_helper);
}
static ssize_t uevent_helper_store(struct kset *kset, const char *page, size_t count)
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
static ssize_t kexec_loaded_show(struct kset *kset, char *page)
{
	return sprintf(page, "%d\n", !!kexec_image);
}
KERNEL_ATTR_RO(kexec_loaded);

static ssize_t kexec_crash_loaded_show(struct kset *kset, char *page)
{
	return sprintf(page, "%d\n", !!kexec_crash_image);
}
KERNEL_ATTR_RO(kexec_crash_loaded);

static ssize_t vmcoreinfo_show(struct kset *kset, char *page)
{
	return sprintf(page, "%lx %x\n",
		       paddr_vmcoreinfo_note(),
		       (unsigned int)vmcoreinfo_max_size);
}
KERNEL_ATTR_RO(vmcoreinfo);

#endif /* CONFIG_KEXEC */

/*
 * Make /sys/kernel/notes give the raw contents of our kernel .notes section.
 */
extern const void __start_notes __attribute__((weak));
extern const void __stop_notes __attribute__((weak));
#define	notes_size (&__stop_notes - &__start_notes)

static ssize_t notes_read(struct kobject *kobj, struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	memcpy(buf, &__start_notes + off, count);
	return count;
}

static struct bin_attribute notes_attr = {
	.attr = {
		.name = "notes",
		.mode = S_IRUGO,
	},
	.read = &notes_read,
};

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
	&vmcoreinfo_attr.attr,
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
		error = sysfs_create_group(&kernel_subsys.kobj,
					   &kernel_attr_group);

	if (!error && notes_size > 0) {
		notes_attr.size = notes_size;
		error = sysfs_create_bin_file(&kernel_subsys.kobj,
					      &notes_attr);
	}

	/*
	 * Create "/sys/kernel/uids" directory and corresponding root user's
	 * directory under it.
	 */
	if (!error)
		error = uids_kobject_init();

	return error;
}

core_initcall(ksysfs_init);
