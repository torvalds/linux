/*
 * inode.h - Defines for inode structures NTFS Linux kernel driver. Part of
 *	     the Linux-NTFS project.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_INODE_H
#define _LINUX_NTFS_INODE_H

#include <linux/atomic.h>

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include "layout.h"
#include "volume.h"
#include "types.h"
#include "runlist.h"
#include "debug.h"

typedef struct _ntfs_inode ntfs_inode;

/*
 * The NTFS in-memory inode structure. It is just used as an extension to the
 * fields already provided in the VFS inode.
 */
struct _ntfs_inode {
	rwlock_t size_lock;	/* Lock serializing access to inode sizes. */
	s64 initialized_size;	/* Copy from the attribute record. */
	s64 allocated_size;	/* Copy from the attribute record. */
	unsigned long state;	/* NTFS specific flags describing this inode.
				   See ntfs_inode_state_bits below. */
	unsigned long mft_no;	/* Number of the mft record / inode. */
	u16 seq_no;		/* Sequence number of the mft record. */
	atomic_t count;		/* Inode reference count for book keeping. */
	ntfs_volume *vol;	/* Pointer to the ntfs volume of this inode. */
	/*
	 * If NInoAttr() is true, the below fields describe the attribute which
	 * this fake inode belongs to. The actual inode of this attribute is
	 * pointed to by base_ntfs_ino and nr_extents is always set to -1 (see
	 * below). For real inodes, we also set the type (AT_DATA for files and
	 * AT_INDEX_ALLOCATION for directories), with the name = NULL and
	 * name_len = 0 for files and name = I30 (global constant) and
	 * name_len = 4 for directories.
	 */
	ATTR_TYPE type;	/* Attribute type of this fake inode. */
	ntfschar *name;		/* Attribute name of this fake inode. */
	u32 name_len;		/* Attribute name length of this fake inode. */
	runlist runlist;	/* If state has the NI_NonResident bit set,
				   the runlist of the unnamed data attribute
				   (if a file) or of the index allocation
				   attribute (directory) or of the attribute
				   described by the fake inode (if NInoAttr()).
				   If runlist.rl is NULL, the runlist has not
				   been read in yet or has been unmapped. If
				   NI_NonResident is clear, the attribute is
				   resident (file and fake inode) or there is
				   no $I30 index allocation attribute
				   (small directory). In the latter case
				   runlist.rl is always NULL.*/
	/*
	 * The following fields are only valid for real inodes and extent
	 * inodes.
	 */
	struct mutex mrec_lock;	/* Lock for serializing access to the
				   mft record belonging to this inode. */
	struct page *page;	/* The page containing the mft record of the
				   inode. This should only be touched by the
				   (un)map_mft_record*() functions. */
	int page_ofs;		/* Offset into the page at which the mft record
				   begins. This should only be touched by the
				   (un)map_mft_record*() functions. */
	/*
	 * Attribute list support (only for use by the attribute lookup
	 * functions). Setup during read_inode for all inodes with attribute
	 * lists. Only valid if NI_AttrList is set in state, and attr_list_rl is
	 * further only valid if NI_AttrListNonResident is set.
	 */
	u32 attr_list_size;	/* Length of attribute list value in bytes. */
	u8 *attr_list;		/* Attribute list value itself. */
	runlist attr_list_rl;	/* Run list for the attribute list value. */
	union {
		struct { /* It is a directory, $MFT, or an index inode. */
			u32 block_size;		/* Size of an index block. */
			u32 vcn_size;		/* Size of a vcn in this
						   index. */
			COLLATION_RULE collation_rule; /* The collation rule
						   for the index. */
			u8 block_size_bits; 	/* Log2 of the above. */
			u8 vcn_size_bits;	/* Log2 of the above. */
		} index;
		struct { /* It is a compressed/sparse file/attribute inode. */
			s64 size;		/* Copy of compressed_size from
						   $DATA. */
			u32 block_size;		/* Size of a compression block
						   (cb). */
			u8 block_size_bits;	/* Log2 of the size of a cb. */
			u8 block_clusters;	/* Number of clusters per cb. */
		} compressed;
	} itype;
	struct mutex extent_lock;	/* Lock for accessing/modifying the
					   below . */
	s32 nr_extents;	/* For a base mft record, the number of attached extent
			   inodes (0 if none), for extent records and for fake
			   inodes describing an attribute this is -1. */
	union {		/* This union is only used if nr_extents != 0. */
		ntfs_inode **extent_ntfs_inos;	/* For nr_extents > 0, array of
						   the ntfs inodes of the extent
						   mft records belonging to
						   this base inode which have
						   been loaded. */
		ntfs_inode *base_ntfs_ino;	/* For nr_extents == -1, the
						   ntfs inode of the base mft
						   record. For fake inodes, the
						   real (base) inode to which
						   the attribute belongs. */
	} ext;
};

/*
 * Defined bits for the state field in the ntfs_inode structure.
 * (f) = files only, (d) = directories only, (a) = attributes/fake inodes only
 */
typedef enum {
	NI_Dirty,		/* 1: Mft record needs to be written to disk. */
	NI_AttrList,		/* 1: Mft record contains an attribute list. */
	NI_AttrListNonResident,	/* 1: Attribute list is non-resident. Implies
				      NI_AttrList is set. */

	NI_Attr,		/* 1: Fake inode for attribute i/o.
				   0: Real inode or extent inode. */

	NI_MstProtected,	/* 1: Attribute is protected by MST fixups.
				   0: Attribute is not protected by fixups. */
	NI_NonResident,		/* 1: Unnamed data attr is non-resident (f).
				   1: Attribute is non-resident (a). */
	NI_IndexAllocPresent = NI_NonResident,	/* 1: $I30 index alloc attr is
						   present (d). */
	NI_Compressed,		/* 1: Unnamed data attr is compressed (f).
				   1: Create compressed files by default (d).
				   1: Attribute is compressed (a). */
	NI_Encrypted,		/* 1: Unnamed data attr is encrypted (f).
				   1: Create encrypted files by default (d).
				   1: Attribute is encrypted (a). */
	NI_Sparse,		/* 1: Unnamed data attr is sparse (f).
				   1: Create sparse files by default (d).
				   1: Attribute is sparse (a). */
	NI_SparseDisabled,	/* 1: May not create sparse regions. */
	NI_TruncateFailed,	/* 1: Last ntfs_truncate() call failed. */
} ntfs_inode_state_bits;

/*
 * NOTE: We should be adding dirty mft records to a list somewhere and they
 * should be independent of the (ntfs/vfs) inode structure so that an inode can
 * be removed but the record can be left dirty for syncing later.
 */

/*
 * Macro tricks to expand the NInoFoo(), NInoSetFoo(), and NInoClearFoo()
 * functions.
 */
#define NINO_FNS(flag)					\
static inline int NIno##flag(ntfs_inode *ni)		\
{							\
	return test_bit(NI_##flag, &(ni)->state);	\
}							\
static inline void NInoSet##flag(ntfs_inode *ni)	\
{							\
	set_bit(NI_##flag, &(ni)->state);		\
}							\
static inline void NInoClear##flag(ntfs_inode *ni)	\
{							\
	clear_bit(NI_##flag, &(ni)->state);		\
}

/*
 * As above for NInoTestSetFoo() and NInoTestClearFoo().
 */
#define TAS_NINO_FNS(flag)					\
static inline int NInoTestSet##flag(ntfs_inode *ni)		\
{								\
	return test_and_set_bit(NI_##flag, &(ni)->state);	\
}								\
static inline int NInoTestClear##flag(ntfs_inode *ni)		\
{								\
	return test_and_clear_bit(NI_##flag, &(ni)->state);	\
}

/* Emit the ntfs inode bitops functions. */
NINO_FNS(Dirty)
TAS_NINO_FNS(Dirty)
NINO_FNS(AttrList)
NINO_FNS(AttrListNonResident)
NINO_FNS(Attr)
NINO_FNS(MstProtected)
NINO_FNS(NonResident)
NINO_FNS(IndexAllocPresent)
NINO_FNS(Compressed)
NINO_FNS(Encrypted)
NINO_FNS(Sparse)
NINO_FNS(SparseDisabled)
NINO_FNS(TruncateFailed)

/*
 * The full structure containing a ntfs_inode and a vfs struct inode. Used for
 * all real and fake inodes but not for extent inodes which lack the vfs struct
 * inode.
 */
typedef struct {
	ntfs_inode ntfs_inode;
	struct inode vfs_inode;		/* The vfs inode structure. */
} big_ntfs_inode;

/**
 * NTFS_I - return the ntfs inode given a vfs inode
 * @inode:	VFS inode
 *
 * NTFS_I() returns the ntfs inode associated with the VFS @inode.
 */
static inline ntfs_inode *NTFS_I(struct inode *inode)
{
	return (ntfs_inode *)container_of(inode, big_ntfs_inode, vfs_inode);
}

static inline struct inode *VFS_I(ntfs_inode *ni)
{
	return &((big_ntfs_inode *)ni)->vfs_inode;
}

/**
 * ntfs_attr - ntfs in memory attribute structure
 * @mft_no:	mft record number of the base mft record of this attribute
 * @name:	Unicode name of the attribute (NULL if unnamed)
 * @name_len:	length of @name in Unicode characters (0 if unnamed)
 * @type:	attribute type (see layout.h)
 *
 * This structure exists only to provide a small structure for the
 * ntfs_{attr_}iget()/ntfs_test_inode()/ntfs_init_locked_inode() mechanism.
 *
 * NOTE: Elements are ordered by size to make the structure as compact as
 * possible on all architectures.
 */
typedef struct {
	unsigned long mft_no;
	ntfschar *name;
	u32 name_len;
	ATTR_TYPE type;
} ntfs_attr;

typedef int (*test_t)(struct inode *, void *);

extern int ntfs_test_inode(struct inode *vi, ntfs_attr *na);

extern struct inode *ntfs_iget(struct super_block *sb, unsigned long mft_no);
extern struct inode *ntfs_attr_iget(struct inode *base_vi, ATTR_TYPE type,
		ntfschar *name, u32 name_len);
extern struct inode *ntfs_index_iget(struct inode *base_vi, ntfschar *name,
		u32 name_len);

extern struct inode *ntfs_alloc_big_inode(struct super_block *sb);
extern void ntfs_free_big_inode(struct inode *inode);
extern void ntfs_evict_big_inode(struct inode *vi);

extern void __ntfs_init_inode(struct super_block *sb, ntfs_inode *ni);

static inline void ntfs_init_big_inode(struct inode *vi)
{
	ntfs_inode *ni = NTFS_I(vi);

	ntfs_debug("Entering.");
	__ntfs_init_inode(vi->i_sb, ni);
	ni->mft_no = vi->i_ino;
}

extern ntfs_inode *ntfs_new_extent_inode(struct super_block *sb,
		unsigned long mft_no);
extern void ntfs_clear_extent_inode(ntfs_inode *ni);

extern int ntfs_read_inode_mount(struct inode *vi);

extern int ntfs_show_options(struct seq_file *sf, struct dentry *root);

#ifdef NTFS_RW

extern int ntfs_truncate(struct inode *vi);
extern void ntfs_truncate_vfs(struct inode *vi);

extern int ntfs_setattr(struct dentry *dentry, struct iattr *attr);

extern int __ntfs_write_inode(struct inode *vi, int sync);

static inline void ntfs_commit_inode(struct inode *vi)
{
	if (!is_bad_inode(vi))
		__ntfs_write_inode(vi, 1);
	return;
}

#else

static inline void ntfs_truncate_vfs(struct inode *vi) {}

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_INODE_H */
