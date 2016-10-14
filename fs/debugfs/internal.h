/*
 *  internal.h - declarations internal to debugfs
 *
 *  Copyright (C) 2016 Nicolai Stange <nicstange@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#ifndef _DEBUGFS_INTERNAL_H_
#define _DEBUGFS_INTERNAL_H_

struct file_operations;

/* declared over in file.c */
extern const struct file_operations debugfs_noop_file_operations;
extern const struct file_operations debugfs_open_proxy_file_operations;
extern const struct file_operations debugfs_full_proxy_file_operations;

struct dentry *debugfs_create_file_unsafe(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops);

#endif /* _DEBUGFS_INTERNAL_H_ */
