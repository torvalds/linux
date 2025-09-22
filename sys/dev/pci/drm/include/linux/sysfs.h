/* Public domain. */

#ifndef _LINUX_SYSFS_H
#define _LINUX_SYSFS_H

#include <linux/kernfs.h>

struct attribute {
	const char *name;
	int mode;
};

struct bin_attribute {
};

struct attribute_group {
	const char *name;
	struct attribute **attrs;
	struct bin_attribute **bin_attrs;
};

#define ATTRIBUTE_GROUPS(x)

#define sysfs_create_link(x, y, z)	0
#define sysfs_remove_link(x, y)
#define sysfs_create_group(x, y)	0
#define sysfs_remove_group(x, y)
#define sysfs_create_file(x, y)		0
#define sysfs_remove_file(x, y)
#define sysfs_create_bin_file(x, y)	0
#define sysfs_remove_bin_file(x, y)
#define sysfs_remove_file_from_group(x, y, z)
#define sysfs_create_files(x, y)	0
#define sysfs_remove_files(x, y)
#define sysfs_bin_attr_init(x)

static inline int
sysfs_emit(char *str, const char *format, ...)
{
	return 0;
}

static inline int
sysfs_emit_at(char *str, int pos, const char *format, ...)
{
	return 0;
}

#endif
