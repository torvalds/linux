/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  tracefs.h - a pseudo file system for activating tracing
 *
 * Based on debugfs by: 2004 Greg Kroah-Hartman <greg@kroah.com>
 *
 *  Copyright (C) 2014 Red Hat Inc, author: Steven Rostedt <srostedt@redhat.com>
 *
 * tracefs is the file system that is used by the tracing infrastructure.
 */

#ifndef _TRACEFS_H_
#define _TRACEFS_H_

#include <linux/fs.h>
#include <linux/seq_file.h>

#include <linux/types.h>

struct file_operations;

#ifdef CONFIG_TRACING

struct eventfs_file;

typedef int (*eventfs_callback)(const char *name, umode_t *mode, void **data,
				const struct file_operations **fops);

struct eventfs_entry {
	const char			*name;
	eventfs_callback		callback;
};

struct eventfs_inode;

struct eventfs_inode *eventfs_create_events_dir(const char *name, struct dentry *parent,
						const struct eventfs_entry *entries,
						int size, void *data);

struct eventfs_inode *eventfs_create_dir(const char *name, struct eventfs_inode *parent,
					 const struct eventfs_entry *entries,
					 int size, void *data);

void eventfs_remove_events_dir(struct eventfs_inode *ei);
void eventfs_remove_dir(struct eventfs_inode *ei);

struct dentry *tracefs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops);

struct dentry *tracefs_create_dir(const char *name, struct dentry *parent);

void tracefs_remove(struct dentry *dentry);

struct dentry *tracefs_create_instance_dir(const char *name, struct dentry *parent,
					   int (*mkdir)(const char *name),
					   int (*rmdir)(const char *name));

bool tracefs_initialized(void);

#endif /* CONFIG_TRACING */

#endif
