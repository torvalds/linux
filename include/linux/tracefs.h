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

/**
 * eventfs_callback - A callback function to create dynamic files in eventfs
 * @name: The name of the file that is to be created
 * @mode: return the file mode for the file (RW access, etc)
 * @data: data to pass to the created file ops
 * @fops: the file operations of the created file
 *
 * The evetnfs files are dynamically created. The struct eventfs_entry array
 * is passed to eventfs_create_dir() or eventfs_create_events_dir() that will
 * be used to create the files within those directories. When a lookup
 * or access to a file within the directory is made, the struct eventfs_entry
 * array is used to find a callback() with the matching name that is being
 * referenced (for lookups, the entire array is iterated and each callback
 * will be called).
 *
 * The callback will be called with @name for the name of the file to create.
 * The callback can return less than 1 to indicate  that no file should be
 * created.
 *
 * If a file is to be created, then @mode should be populated with the file
 * mode (permissions) for which the file is created for. This would be
 * used to set the created inode i_mode field.
 *
 * The @data should be set to the data passed to the other file operations
 * (read, write, etc). Note, @data will also point to the data passed in
 * to eventfs_create_dir() or eventfs_create_events_dir(), but the callback
 * can replace the data if it chooses to. Otherwise, the original data
 * will be used for the file operation functions.
 *
 * The @fops should be set to the file operations that will be used to create
 * the inode.
 *
 * NB. This callback is called while holding internal locks of the eventfs
 *     system. The callback must not call any code that might also call into
 *     the tracefs or eventfs system or it will risk creating a deadlock.
 */
typedef int (*eventfs_callback)(const char *name, umode_t *mode, void **data,
				const struct file_operations **fops);

typedef void (*eventfs_release)(const char *name, void *data);

/**
 * struct eventfs_entry - dynamically created eventfs file call back handler
 * @name:	Then name of the dynamic file in an eventfs directory
 * @callback:	The callback to get the fops of the file when it is created
 *
 * See evenfs_callback() typedef for how to set up @callback.
 */
struct eventfs_entry {
	const char			*name;
	eventfs_callback		callback;
	eventfs_release			release;
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
