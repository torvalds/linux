/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SYSFS_UTILS_H
#define __SYSFS_UTILS_H

int write_sysfs_attribute(const char *attr_path, const char *new_value,
			  size_t len);

#endif
