// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Migration
 *
 * This file contains the core logic of mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/sysfs.h>

#ifdef CONFIG_64BIT
#if PAGE_SIZE == SZ_4K
DEFINE_STATIC_KEY_TRUE(pgsize_migration_enabled);

#define is_pgsize_migration_enabled() 	(static_branch_likely(&pgsize_migration_enabled))
#else /* PAGE_SIZE != SZ_4K */
DEFINE_STATIC_KEY_FALSE(pgsize_migration_enabled);

#define is_pgsize_migration_enabled() 	(static_branch_unlikely(&pgsize_migration_enabled))
#endif /* PAGE_SIZE == SZ_4K */

static ssize_t show_pgsize_migration_enabled(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	if (is_pgsize_migration_enabled())
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t store_pgsize_migration_enabled(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t n)
{
	unsigned long val;

	/* Migration is only applicable to 4kB kernels */
	if (PAGE_SIZE != SZ_4K)
		return n;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	if (val == 1)
		static_branch_enable(&pgsize_migration_enabled);
	else if (val == 0)
		static_branch_disable(&pgsize_migration_enabled);

	return n;
}

static struct kobj_attribute pgsize_migration_enabled_attr = __ATTR(
	enabled,
	0644,
	show_pgsize_migration_enabled,
	store_pgsize_migration_enabled
);

static struct attribute *pgsize_migration_attrs[] = {
	&pgsize_migration_enabled_attr.attr,
	NULL
};

static struct attribute_group pgsize_migration_attr_group = {
	.name = "pgsize_migration",
	.attrs = pgsize_migration_attrs,
};

/**
 * What:          /sys/kernel/mm/pgsize_migration/enabled
 * Date:          April 2024
 * KernelVersion: v5.4+ (GKI kernels)
 * Contact:       Kalesh Singh <kaleshsingh@google.com>
 * Description:   /sys/kernel/mm/pgsize_migration/enabled
 *                allows for userspace to turn on or off page size
 *                migration mitigations necessary for app compatibility
 *                during Android's transition from 4kB to 16kB page size.
 *                Such mitigations include preserving /proc/<pid>/[s]maps
 *                output as if there was no segment extension by the
 *                dynamic loader; and preventing fault around in the padding
 *                sections of ELF LOAD segment mappings.
 * Users:         Bionic's dynamic linker
 */
static int __init init_pgsize_migration(void)
{
	if (sysfs_create_group(mm_kobj, &pgsize_migration_attr_group))
		pr_err("pgsize_migration: failed to create sysfs group\n");

	return 0;
};
late_initcall(init_pgsize_migration);
#endif /* CONFIG_64BIT */
