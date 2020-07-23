/* SPDX-License-Identifier: GPL-2.0 */

int __init init_mount(const char *dev_name, const char *dir_name,
		const char *type_page, unsigned long flags, void *data_page);
int __init init_umount(const char *name, int flags);
int __init init_unlink(const char *pathname);
