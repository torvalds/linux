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
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/profile.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/compiler.h>

#include <linux/rcupdate.h>	/* rcu_expedited */

#define KERNEL_ATTR_RO(_name) \
static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) \
static struct kobj_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

/* current uevent sequence number */
static ssize_t uevent_seqnum_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", (unsigned long long)uevent_seqnum);
}
KERNEL_ATTR_RO(uevent_seqnum);

#ifdef CONFIG_UEVENT_HELPER
/* uevent helper program, used during early boot */
static ssize_t uevent_helper_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", uevent_helper);
}
static ssize_t uevent_helper_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	if (count+1 > UEVENT_HELPER_PATH_LEN)
		return -ENOENT;
	memcpy(uevent_helper, buf, count);
	uevent_helper[count] = '\0';
	if (count && uevent_helper[count-1] == '\n')
		uevent_helper[count-1] = '\0';
	return count;
}
KERNEL_ATTR_RW(uevent_helper);
#endif

#ifdef CONFIG_PROFILING
static ssize_t profiling_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", prof_on);
}
static ssize_t profiling_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;

	if (prof_on)
		return -EEXIST;
	/*
	 * This eventually calls into get_option() which
	 * has a ton of callers and is not const.  It is
	 * easiest to cast it away here.
	 */
	profile_setup((char *)buf);
	ret = profile_init();
	if (ret)
		return ret;
	ret = create_proc_profile();
	if (ret)
		return ret;
	return count;
}
KERNEL_ATTR_RW(profiling);
#endif

#ifdef CONFIG_KEXEC_CORE
static ssize_t kexec_loaded_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", !!kexec_image);
}
KERNEL_ATTR_RO(kexec_loaded);

static ssize_t kexec_crash_loaded_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", !!kexec_crash_image);
}
KERNEL_ATTR_RO(kexec_crash_loaded);

static ssize_t kexec_crash_size_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%zu\n", crash_get_memory_size());
}
static ssize_t kexec_crash_size_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long cnt;
	int ret;

	if (kstrtoul(buf, 0, &cnt))
		return -EINVAL;

	ret = crash_shrink_memory(cnt);
	return ret < 0 ? ret : count;
}
KERNEL_ATTR_RW(kexec_crash_size);

static ssize_t vmcoreinfo_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lx %x\n",
		       paddr_vmcoreinfo_note(),
		       (unsigned int)sizeof(vmcoreinfo_note));
}
KERNEL_ATTR_RO(vmcoreinfo);

#endif /* CONFIG_KEXEC_CORE */

/* whether file capabilities are enabled */
static ssize_t fscaps_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", file_caps_enabled);
}
KERNEL_ATTR_RO(fscaps);

int rcu_expedited;
static ssize_t rcu_expedited_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rcu_expedited);
}
static ssize_t rcu_expedited_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	if (kstrtoint(buf, 0, &rcu_expedited))
		return -EINVAL;

	return count;
}
KERNEL_ATTR_RW(rcu_expedited);

/*
 * Make /sys/kernel/notes give the raw contents of our kernel .notes section.
 */
extern const void __start_notes __weak;
extern const void __stop_notes __weak;
#define	notes_size (&__stop_notes - &__start_notes)

static ssize_t notes_read(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr,
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

struct kobject *kernel_kobj;
EXPORT_SYMBOL_GPL(kernel_kobj);

static struct attribute * kernel_attrs[] = {
	&fscaps_attr.attr,
	&uevent_seqnum_attr.attr,
#ifdef CONFIG_UEVENT_HELPER
	&uevent_helper_attr.attr,
#endif
#ifdef CONFIG_PROFILING
	&profiling_attr.attr,
#endif
#ifdef CONFIG_KEXEC_CORE
	&kexec_loaded_attr.attr,
	&kexec_crash_loaded_attr.attr,
	&kexec_crash_size_attr.attr,
	&vmcoreinfo_attr.attr,
#endif
	&rcu_expedited_attr.attr,
	NULL
};

static struct attribute_group kernel_attr_group = {
	.attrs = kernel_attrs,
};

static int __init ksysfs_init(void)
{
	int error;

	kernel_kobj = kobject_create_and_add("kernel", NULL);
	if (!kernel_kobj) {
		error = -ENOMEM;
		goto exit;
	}
	error = sysfs_create_group(kernel_kobj, &kernel_attr_group);
	if (error)
		goto kset_exit;

	if (notes_size > 0) {
		notes_attr.size = notes_size;
		error = sysfs_create_bin_file(kernel_kobj, &notes_attr);
		if (error)
			goto group_exit;
	}

	return 0;

group_exit:
	sysfs_remove_group(kernel_kobj, &kernel_attr_group);
kset_exit:
	kobject_put(kernel_kobj);
exit:
	return error;
}

core_initcall(ksysfs_init);
