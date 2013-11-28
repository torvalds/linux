/*
 * fs/kernfs/kernfs-internal.h - kernfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#ifndef __KERNFS_INTERNAL_H
#define __KERNFS_INTERNAL_H

#include <linux/lockdep.h>
#include <linux/fs.h>
#include <linux/rbtree.h>

#include <linux/kernfs.h>

struct sysfs_open_dirent;

/* type-specific structures for sysfs_dirent->s_* union members */
struct sysfs_elem_dir {
	unsigned long		subdirs;
	/* children rbtree starts here and goes through sd->s_rb */
	struct rb_root		children;
};

struct sysfs_elem_symlink {
	struct sysfs_dirent	*target_sd;
};

struct sysfs_elem_attr {
	const struct kernfs_ops	*ops;
	struct sysfs_open_dirent *open;
	loff_t			size;
};

struct sysfs_inode_attrs {
	struct iattr	ia_iattr;
	void		*ia_secdata;
	u32		ia_secdata_len;
};

/*
 * sysfs_dirent - the building block of sysfs hierarchy.  Each and
 * every sysfs node is represented by single sysfs_dirent.
 *
 * As long as s_count reference is held, the sysfs_dirent itself is
 * accessible.  Dereferencing s_elem or any other outer entity
 * requires s_active reference.
 */
struct sysfs_dirent {
	atomic_t		s_count;
	atomic_t		s_active;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
	struct sysfs_dirent	*s_parent;
	const char		*s_name;

	struct rb_node		s_rb;

	union {
		struct completion	*completion;
		struct sysfs_dirent	*removed_list;
	} u;

	const void		*s_ns; /* namespace tag */
	unsigned int		s_hash; /* ns + name hash */
	union {
		struct sysfs_elem_dir		s_dir;
		struct sysfs_elem_symlink	s_symlink;
		struct sysfs_elem_attr		s_attr;
	};

	void			*priv;

	unsigned short		s_flags;
	umode_t			s_mode;
	unsigned int		s_ino;
	struct sysfs_inode_attrs *s_iattr;
};

#define SD_DEACTIVATED_BIAS		INT_MIN

#define SYSFS_TYPE_MASK			0x000f
#define SYSFS_DIR			0x0001
#define SYSFS_KOBJ_ATTR			0x0002
#define SYSFS_KOBJ_LINK			0x0004
#define SYSFS_COPY_NAME			(SYSFS_DIR | SYSFS_KOBJ_LINK)
#define SYSFS_ACTIVE_REF		SYSFS_KOBJ_ATTR

#define SYSFS_FLAG_MASK			~SYSFS_TYPE_MASK
#define SYSFS_FLAG_REMOVED		0x0010
#define SYSFS_FLAG_NS			0x0020
#define SYSFS_FLAG_HAS_SEQ_SHOW		0x0040
#define SYSFS_FLAG_HAS_MMAP		0x0080
#define SYSFS_FLAG_LOCKDEP		0x0100

static inline unsigned int sysfs_type(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_TYPE_MASK;
}

/*
 * Context structure to be used while adding/removing nodes.
 */
struct sysfs_addrm_cxt {
	struct sysfs_dirent	*removed;
};

#include "../sysfs/sysfs.h"

#endif	/* __KERNFS_INTERNAL_H */
