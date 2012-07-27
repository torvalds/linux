/*
 *  debugfs.h - a tiny little debug file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See Documentation/DocBook/filesystems for more details.
 */

#ifndef _DEBUGFS_H_
#define _DEBUGFS_H_

#include <linux/fs.h>
#include <linux/seq_file.h>

#include <linux/types.h>

struct file_operations;

struct debugfs_blob_wrapper {
	void *data;
	unsigned long size;
};

struct debugfs_reg32 {
	char *name;
	unsigned long offset;
};

struct debugfs_regset32 {
	struct debugfs_reg32 *regs;
	int nregs;
	void __iomem *base;
};

extern struct dentry *arch_debugfs_dir;

#if defined(CONFIG_DEBUG_FS)

/* declared over in file.c */
extern const struct file_operations debugfs_file_operations;
extern const struct inode_operations debugfs_link_operations;

struct dentry *debugfs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops);

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);

struct dentry *debugfs_create_symlink(const char *name, struct dentry *parent,
				      const char *dest);

void debugfs_remove(struct dentry *dentry);
void debugfs_remove_recursive(struct dentry *dentry);

struct dentry *debugfs_rename(struct dentry *old_dir, struct dentry *old_dentry,
                struct dentry *new_dir, const char *new_name);

struct dentry *debugfs_create_u8(const char *name, umode_t mode,
				 struct dentry *parent, u8 *value);
struct dentry *debugfs_create_u16(const char *name, umode_t mode,
				  struct dentry *parent, u16 *value);
struct dentry *debugfs_create_u32(const char *name, umode_t mode,
				  struct dentry *parent, u32 *value);
struct dentry *debugfs_create_u64(const char *name, umode_t mode,
				  struct dentry *parent, u64 *value);
struct dentry *debugfs_create_x8(const char *name, umode_t mode,
				 struct dentry *parent, u8 *value);
struct dentry *debugfs_create_x16(const char *name, umode_t mode,
				  struct dentry *parent, u16 *value);
struct dentry *debugfs_create_x32(const char *name, umode_t mode,
				  struct dentry *parent, u32 *value);
struct dentry *debugfs_create_x64(const char *name, umode_t mode,
				  struct dentry *parent, u64 *value);
struct dentry *debugfs_create_size_t(const char *name, umode_t mode,
				     struct dentry *parent, size_t *value);
struct dentry *debugfs_create_bool(const char *name, umode_t mode,
				  struct dentry *parent, u32 *value);

struct dentry *debugfs_create_blob(const char *name, umode_t mode,
				  struct dentry *parent,
				  struct debugfs_blob_wrapper *blob);

struct dentry *debugfs_create_regset32(const char *name, umode_t mode,
				     struct dentry *parent,
				     struct debugfs_regset32 *regset);

int debugfs_print_regs32(struct seq_file *s, const struct debugfs_reg32 *regs,
			 int nregs, void __iomem *base, char *prefix);

struct dentry *debugfs_create_u32_array(const char *name, umode_t mode,
					struct dentry *parent,
					u32 *array, u32 elements);

bool debugfs_initialized(void);

#else

#include <linux/err.h>

/* 
 * We do not return NULL from these functions if CONFIG_DEBUG_FS is not enabled
 * so users have a chance to detect if there was a real error or not.  We don't
 * want to duplicate the design decision mistakes of procfs and devfs again.
 */

static inline struct dentry *debugfs_create_file(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_dir(const char *name,
						struct dentry *parent)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_symlink(const char *name,
						    struct dentry *parent,
						    const char *dest)
{
	return ERR_PTR(-ENODEV);
}

static inline void debugfs_remove(struct dentry *dentry)
{ }

static inline void debugfs_remove_recursive(struct dentry *dentry)
{ }

static inline struct dentry *debugfs_rename(struct dentry *old_dir, struct dentry *old_dentry,
                struct dentry *new_dir, char *new_name)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u8(const char *name, umode_t mode,
					       struct dentry *parent,
					       u8 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u16(const char *name, umode_t mode,
						struct dentry *parent,
						u16 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u32(const char *name, umode_t mode,
						struct dentry *parent,
						u32 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_u64(const char *name, umode_t mode,
						struct dentry *parent,
						u64 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_x8(const char *name, umode_t mode,
					       struct dentry *parent,
					       u8 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_x16(const char *name, umode_t mode,
						struct dentry *parent,
						u16 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_x32(const char *name, umode_t mode,
						struct dentry *parent,
						u32 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_size_t(const char *name, umode_t mode,
				     struct dentry *parent,
				     size_t *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_bool(const char *name, umode_t mode,
						 struct dentry *parent,
						 u32 *value)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_blob(const char *name, umode_t mode,
				  struct dentry *parent,
				  struct debugfs_blob_wrapper *blob)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *debugfs_create_regset32(const char *name,
				   umode_t mode, struct dentry *parent,
				   struct debugfs_regset32 *regset)
{
	return ERR_PTR(-ENODEV);
}

static inline bool debugfs_initialized(void)
{
	return false;
}

static inline struct dentry *debugfs_create_u32_array(const char *name, umode_t mode,
					struct dentry *parent,
					u32 *array, u32 elements)
{
	return ERR_PTR(-ENODEV);
}

#endif

#endif
