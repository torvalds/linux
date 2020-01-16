/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * iyesde.h - Defines for iyesde structures NTFS Linux kernel driver. Part of
 *	     the Linux-NTFS project.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
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

typedef struct _ntfs_iyesde ntfs_iyesde;

/*
 * The NTFS in-memory iyesde structure. It is just used as an extension to the
 * fields already provided in the VFS iyesde.
 */
struct _ntfs_iyesde {
	rwlock_t size_lock;	/* Lock serializing access to iyesde sizes. */
	s64 initialized_size;	/* Copy from the attribute record. */
	s64 allocated_size;	/* Copy from the attribute record. */
	unsigned long state;	/* NTFS specific flags describing this iyesde.
				   See ntfs_iyesde_state_bits below. */
	unsigned long mft_yes;	/* Number of the mft record / iyesde. */
	u16 seq_yes;		/* Sequence number of the mft record. */
	atomic_t count;		/* Iyesde reference count for book keeping. */
	ntfs_volume *vol;	/* Pointer to the ntfs volume of this iyesde. */
	/*
	 * If NIyesAttr() is true, the below fields describe the attribute which
	 * this fake iyesde belongs to. The actual iyesde of this attribute is
	 * pointed to by base_ntfs_iyes and nr_extents is always set to -1 (see
	 * below). For real iyesdes, we also set the type (AT_DATA for files and
	 * AT_INDEX_ALLOCATION for directories), with the name = NULL and
	 * name_len = 0 for files and name = I30 (global constant) and
	 * name_len = 4 for directories.
	 */
	ATTR_TYPE type;	/* Attribute type of this fake iyesde. */
	ntfschar *name;		/* Attribute name of this fake iyesde. */
	u32 name_len;		/* Attribute name length of this fake iyesde. */
	runlist runlist;	/* If state has the NI_NonResident bit set,
				   the runlist of the unnamed data attribute
				   (if a file) or of the index allocation
				   attribute (directory) or of the attribute
				   described by the fake iyesde (if NIyesAttr()).
				   If runlist.rl is NULL, the runlist has yest
				   been read in yet or has been unmapped. If
				   NI_NonResident is clear, the attribute is
				   resident (file and fake iyesde) or there is
				   yes $I30 index allocation attribute
				   (small directory). In the latter case
				   runlist.rl is always NULL.*/
	/*
	 * The following fields are only valid for real iyesdes and extent
	 * iyesdes.
	 */
	struct mutex mrec_lock;	/* Lock for serializing access to the
				   mft record belonging to this iyesde. */
	struct page *page;	/* The page containing the mft record of the
				   iyesde. This should only be touched by the
				   (un)map_mft_record*() functions. */
	int page_ofs;		/* Offset into the page at which the mft record
				   begins. This should only be touched by the
				   (un)map_mft_record*() functions. */
	/*
	 * Attribute list support (only for use by the attribute lookup
	 * functions). Setup during read_iyesde for all iyesdes with attribute
	 * lists. Only valid if NI_AttrList is set in state, and attr_list_rl is
	 * further only valid if NI_AttrListNonResident is set.
	 */
	u32 attr_list_size;	/* Length of attribute list value in bytes. */
	u8 *attr_list;		/* Attribute list value itself. */
	runlist attr_list_rl;	/* Run list for the attribute list value. */
	union {
		struct { /* It is a directory, $MFT, or an index iyesde. */
			u32 block_size;		/* Size of an index block. */
			u32 vcn_size;		/* Size of a vcn in this
						   index. */
			COLLATION_RULE collation_rule; /* The collation rule
						   for the index. */
			u8 block_size_bits; 	/* Log2 of the above. */
			u8 vcn_size_bits;	/* Log2 of the above. */
		} index;
		struct { /* It is a compressed/sparse file/attribute iyesde. */
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
			   iyesdes (0 if yesne), for extent records and for fake
			   iyesdes describing an attribute this is -1. */
	union {		/* This union is only used if nr_extents != 0. */
		ntfs_iyesde **extent_ntfs_iyess;	/* For nr_extents > 0, array of
						   the ntfs iyesdes of the extent
						   mft records belonging to
						   this base iyesde which have
						   been loaded. */
		ntfs_iyesde *base_ntfs_iyes;	/* For nr_extents == -1, the
						   ntfs iyesde of the base mft
						   record. For fake iyesdes, the
						   real (base) iyesde to which
						   the attribute belongs. */
	} ext;
};

/*
 * Defined bits for the state field in the ntfs_iyesde structure.
 * (f) = files only, (d) = directories only, (a) = attributes/fake iyesdes only
 */
typedef enum {
	NI_Dirty,		/* 1: Mft record needs to be written to disk. */
	NI_AttrList,		/* 1: Mft record contains an attribute list. */
	NI_AttrListNonResident,	/* 1: Attribute list is yesn-resident. Implies
				      NI_AttrList is set. */

	NI_Attr,		/* 1: Fake iyesde for attribute i/o.
				   0: Real iyesde or extent iyesde. */

	NI_MstProtected,	/* 1: Attribute is protected by MST fixups.
				   0: Attribute is yest protected by fixups. */
	NI_NonResident,		/* 1: Unnamed data attr is yesn-resident (f).
				   1: Attribute is yesn-resident (a). */
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
	NI_SparseDisabled,	/* 1: May yest create sparse regions. */
	NI_TruncateFailed,	/* 1: Last ntfs_truncate() call failed. */
} ntfs_iyesde_state_bits;

/*
 * NOTE: We should be adding dirty mft records to a list somewhere and they
 * should be independent of the (ntfs/vfs) iyesde structure so that an iyesde can
 * be removed but the record can be left dirty for syncing later.
 */

/*
 * Macro tricks to expand the NIyesFoo(), NIyesSetFoo(), and NIyesClearFoo()
 * functions.
 */
#define NINO_FNS(flag)					\
static inline int NIyes##flag(ntfs_iyesde *ni)		\
{							\
	return test_bit(NI_##flag, &(ni)->state);	\
}							\
static inline void NIyesSet##flag(ntfs_iyesde *ni)	\
{							\
	set_bit(NI_##flag, &(ni)->state);		\
}							\
static inline void NIyesClear##flag(ntfs_iyesde *ni)	\
{							\
	clear_bit(NI_##flag, &(ni)->state);		\
}

/*
 * As above for NIyesTestSetFoo() and NIyesTestClearFoo().
 */
#define TAS_NINO_FNS(flag)					\
static inline int NIyesTestSet##flag(ntfs_iyesde *ni)		\
{								\
	return test_and_set_bit(NI_##flag, &(ni)->state);	\
}								\
static inline int NIyesTestClear##flag(ntfs_iyesde *ni)		\
{								\
	return test_and_clear_bit(NI_##flag, &(ni)->state);	\
}

/* Emit the ntfs iyesde bitops functions. */
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
 * The full structure containing a ntfs_iyesde and a vfs struct iyesde. Used for
 * all real and fake iyesdes but yest for extent iyesdes which lack the vfs struct
 * iyesde.
 */
typedef struct {
	ntfs_iyesde ntfs_iyesde;
	struct iyesde vfs_iyesde;		/* The vfs iyesde structure. */
} big_ntfs_iyesde;

/**
 * NTFS_I - return the ntfs iyesde given a vfs iyesde
 * @iyesde:	VFS iyesde
 *
 * NTFS_I() returns the ntfs iyesde associated with the VFS @iyesde.
 */
static inline ntfs_iyesde *NTFS_I(struct iyesde *iyesde)
{
	return (ntfs_iyesde *)container_of(iyesde, big_ntfs_iyesde, vfs_iyesde);
}

static inline struct iyesde *VFS_I(ntfs_iyesde *ni)
{
	return &((big_ntfs_iyesde *)ni)->vfs_iyesde;
}

/**
 * ntfs_attr - ntfs in memory attribute structure
 * @mft_yes:	mft record number of the base mft record of this attribute
 * @name:	Unicode name of the attribute (NULL if unnamed)
 * @name_len:	length of @name in Unicode characters (0 if unnamed)
 * @type:	attribute type (see layout.h)
 *
 * This structure exists only to provide a small structure for the
 * ntfs_{attr_}iget()/ntfs_test_iyesde()/ntfs_init_locked_iyesde() mechanism.
 *
 * NOTE: Elements are ordered by size to make the structure as compact as
 * possible on all architectures.
 */
typedef struct {
	unsigned long mft_yes;
	ntfschar *name;
	u32 name_len;
	ATTR_TYPE type;
} ntfs_attr;

typedef int (*test_t)(struct iyesde *, void *);

extern int ntfs_test_iyesde(struct iyesde *vi, ntfs_attr *na);

extern struct iyesde *ntfs_iget(struct super_block *sb, unsigned long mft_yes);
extern struct iyesde *ntfs_attr_iget(struct iyesde *base_vi, ATTR_TYPE type,
		ntfschar *name, u32 name_len);
extern struct iyesde *ntfs_index_iget(struct iyesde *base_vi, ntfschar *name,
		u32 name_len);

extern struct iyesde *ntfs_alloc_big_iyesde(struct super_block *sb);
extern void ntfs_free_big_iyesde(struct iyesde *iyesde);
extern void ntfs_evict_big_iyesde(struct iyesde *vi);

extern void __ntfs_init_iyesde(struct super_block *sb, ntfs_iyesde *ni);

static inline void ntfs_init_big_iyesde(struct iyesde *vi)
{
	ntfs_iyesde *ni = NTFS_I(vi);

	ntfs_debug("Entering.");
	__ntfs_init_iyesde(vi->i_sb, ni);
	ni->mft_yes = vi->i_iyes;
}

extern ntfs_iyesde *ntfs_new_extent_iyesde(struct super_block *sb,
		unsigned long mft_yes);
extern void ntfs_clear_extent_iyesde(ntfs_iyesde *ni);

extern int ntfs_read_iyesde_mount(struct iyesde *vi);

extern int ntfs_show_options(struct seq_file *sf, struct dentry *root);

#ifdef NTFS_RW

extern int ntfs_truncate(struct iyesde *vi);
extern void ntfs_truncate_vfs(struct iyesde *vi);

extern int ntfs_setattr(struct dentry *dentry, struct iattr *attr);

extern int __ntfs_write_iyesde(struct iyesde *vi, int sync);

static inline void ntfs_commit_iyesde(struct iyesde *vi)
{
	if (!is_bad_iyesde(vi))
		__ntfs_write_iyesde(vi, 1);
	return;
}

#else

static inline void ntfs_truncate_vfs(struct iyesde *vi) {}

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_INODE_H */
