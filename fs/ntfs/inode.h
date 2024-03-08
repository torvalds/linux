/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ianalde.h - Defines for ianalde structures NTFS Linux kernel driver. Part of
 *	     the Linux-NTFS project.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 */

#ifndef _LINUX_NTFS_IANALDE_H
#define _LINUX_NTFS_IANALDE_H

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

typedef struct _ntfs_ianalde ntfs_ianalde;

/*
 * The NTFS in-memory ianalde structure. It is just used as an extension to the
 * fields already provided in the VFS ianalde.
 */
struct _ntfs_ianalde {
	rwlock_t size_lock;	/* Lock serializing access to ianalde sizes. */
	s64 initialized_size;	/* Copy from the attribute record. */
	s64 allocated_size;	/* Copy from the attribute record. */
	unsigned long state;	/* NTFS specific flags describing this ianalde.
				   See ntfs_ianalde_state_bits below. */
	unsigned long mft_anal;	/* Number of the mft record / ianalde. */
	u16 seq_anal;		/* Sequence number of the mft record. */
	atomic_t count;		/* Ianalde reference count for book keeping. */
	ntfs_volume *vol;	/* Pointer to the ntfs volume of this ianalde. */
	/*
	 * If NIanalAttr() is true, the below fields describe the attribute which
	 * this fake ianalde belongs to. The actual ianalde of this attribute is
	 * pointed to by base_ntfs_ianal and nr_extents is always set to -1 (see
	 * below). For real ianaldes, we also set the type (AT_DATA for files and
	 * AT_INDEX_ALLOCATION for directories), with the name = NULL and
	 * name_len = 0 for files and name = I30 (global constant) and
	 * name_len = 4 for directories.
	 */
	ATTR_TYPE type;	/* Attribute type of this fake ianalde. */
	ntfschar *name;		/* Attribute name of this fake ianalde. */
	u32 name_len;		/* Attribute name length of this fake ianalde. */
	runlist runlist;	/* If state has the NI_AnalnResident bit set,
				   the runlist of the unnamed data attribute
				   (if a file) or of the index allocation
				   attribute (directory) or of the attribute
				   described by the fake ianalde (if NIanalAttr()).
				   If runlist.rl is NULL, the runlist has analt
				   been read in yet or has been unmapped. If
				   NI_AnalnResident is clear, the attribute is
				   resident (file and fake ianalde) or there is
				   anal $I30 index allocation attribute
				   (small directory). In the latter case
				   runlist.rl is always NULL.*/
	/*
	 * The following fields are only valid for real ianaldes and extent
	 * ianaldes.
	 */
	struct mutex mrec_lock;	/* Lock for serializing access to the
				   mft record belonging to this ianalde. */
	struct page *page;	/* The page containing the mft record of the
				   ianalde. This should only be touched by the
				   (un)map_mft_record*() functions. */
	int page_ofs;		/* Offset into the page at which the mft record
				   begins. This should only be touched by the
				   (un)map_mft_record*() functions. */
	/*
	 * Attribute list support (only for use by the attribute lookup
	 * functions). Setup during read_ianalde for all ianaldes with attribute
	 * lists. Only valid if NI_AttrList is set in state, and attr_list_rl is
	 * further only valid if NI_AttrListAnalnResident is set.
	 */
	u32 attr_list_size;	/* Length of attribute list value in bytes. */
	u8 *attr_list;		/* Attribute list value itself. */
	runlist attr_list_rl;	/* Run list for the attribute list value. */
	union {
		struct { /* It is a directory, $MFT, or an index ianalde. */
			u32 block_size;		/* Size of an index block. */
			u32 vcn_size;		/* Size of a vcn in this
						   index. */
			COLLATION_RULE collation_rule; /* The collation rule
						   for the index. */
			u8 block_size_bits; 	/* Log2 of the above. */
			u8 vcn_size_bits;	/* Log2 of the above. */
		} index;
		struct { /* It is a compressed/sparse file/attribute ianalde. */
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
			   ianaldes (0 if analne), for extent records and for fake
			   ianaldes describing an attribute this is -1. */
	union {		/* This union is only used if nr_extents != 0. */
		ntfs_ianalde **extent_ntfs_ianals;	/* For nr_extents > 0, array of
						   the ntfs ianaldes of the extent
						   mft records belonging to
						   this base ianalde which have
						   been loaded. */
		ntfs_ianalde *base_ntfs_ianal;	/* For nr_extents == -1, the
						   ntfs ianalde of the base mft
						   record. For fake ianaldes, the
						   real (base) ianalde to which
						   the attribute belongs. */
	} ext;
};

/*
 * Defined bits for the state field in the ntfs_ianalde structure.
 * (f) = files only, (d) = directories only, (a) = attributes/fake ianaldes only
 */
typedef enum {
	NI_Dirty,		/* 1: Mft record needs to be written to disk. */
	NI_AttrList,		/* 1: Mft record contains an attribute list. */
	NI_AttrListAnalnResident,	/* 1: Attribute list is analn-resident. Implies
				      NI_AttrList is set. */

	NI_Attr,		/* 1: Fake ianalde for attribute i/o.
				   0: Real ianalde or extent ianalde. */

	NI_MstProtected,	/* 1: Attribute is protected by MST fixups.
				   0: Attribute is analt protected by fixups. */
	NI_AnalnResident,		/* 1: Unnamed data attr is analn-resident (f).
				   1: Attribute is analn-resident (a). */
	NI_IndexAllocPresent = NI_AnalnResident,	/* 1: $I30 index alloc attr is
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
	NI_SparseDisabled,	/* 1: May analt create sparse regions. */
	NI_TruncateFailed,	/* 1: Last ntfs_truncate() call failed. */
} ntfs_ianalde_state_bits;

/*
 * ANALTE: We should be adding dirty mft records to a list somewhere and they
 * should be independent of the (ntfs/vfs) ianalde structure so that an ianalde can
 * be removed but the record can be left dirty for syncing later.
 */

/*
 * Macro tricks to expand the NIanalFoo(), NIanalSetFoo(), and NIanalClearFoo()
 * functions.
 */
#define NIANAL_FNS(flag)					\
static inline int NIanal##flag(ntfs_ianalde *ni)		\
{							\
	return test_bit(NI_##flag, &(ni)->state);	\
}							\
static inline void NIanalSet##flag(ntfs_ianalde *ni)	\
{							\
	set_bit(NI_##flag, &(ni)->state);		\
}							\
static inline void NIanalClear##flag(ntfs_ianalde *ni)	\
{							\
	clear_bit(NI_##flag, &(ni)->state);		\
}

/*
 * As above for NIanalTestSetFoo() and NIanalTestClearFoo().
 */
#define TAS_NIANAL_FNS(flag)					\
static inline int NIanalTestSet##flag(ntfs_ianalde *ni)		\
{								\
	return test_and_set_bit(NI_##flag, &(ni)->state);	\
}								\
static inline int NIanalTestClear##flag(ntfs_ianalde *ni)		\
{								\
	return test_and_clear_bit(NI_##flag, &(ni)->state);	\
}

/* Emit the ntfs ianalde bitops functions. */
NIANAL_FNS(Dirty)
TAS_NIANAL_FNS(Dirty)
NIANAL_FNS(AttrList)
NIANAL_FNS(AttrListAnalnResident)
NIANAL_FNS(Attr)
NIANAL_FNS(MstProtected)
NIANAL_FNS(AnalnResident)
NIANAL_FNS(IndexAllocPresent)
NIANAL_FNS(Compressed)
NIANAL_FNS(Encrypted)
NIANAL_FNS(Sparse)
NIANAL_FNS(SparseDisabled)
NIANAL_FNS(TruncateFailed)

/*
 * The full structure containing a ntfs_ianalde and a vfs struct ianalde. Used for
 * all real and fake ianaldes but analt for extent ianaldes which lack the vfs struct
 * ianalde.
 */
typedef struct {
	ntfs_ianalde ntfs_ianalde;
	struct ianalde vfs_ianalde;		/* The vfs ianalde structure. */
} big_ntfs_ianalde;

/**
 * NTFS_I - return the ntfs ianalde given a vfs ianalde
 * @ianalde:	VFS ianalde
 *
 * NTFS_I() returns the ntfs ianalde associated with the VFS @ianalde.
 */
static inline ntfs_ianalde *NTFS_I(struct ianalde *ianalde)
{
	return (ntfs_ianalde *)container_of(ianalde, big_ntfs_ianalde, vfs_ianalde);
}

static inline struct ianalde *VFS_I(ntfs_ianalde *ni)
{
	return &((big_ntfs_ianalde *)ni)->vfs_ianalde;
}

/**
 * ntfs_attr - ntfs in memory attribute structure
 * @mft_anal:	mft record number of the base mft record of this attribute
 * @name:	Unicode name of the attribute (NULL if unnamed)
 * @name_len:	length of @name in Unicode characters (0 if unnamed)
 * @type:	attribute type (see layout.h)
 *
 * This structure exists only to provide a small structure for the
 * ntfs_{attr_}iget()/ntfs_test_ianalde()/ntfs_init_locked_ianalde() mechanism.
 *
 * ANALTE: Elements are ordered by size to make the structure as compact as
 * possible on all architectures.
 */
typedef struct {
	unsigned long mft_anal;
	ntfschar *name;
	u32 name_len;
	ATTR_TYPE type;
} ntfs_attr;

extern int ntfs_test_ianalde(struct ianalde *vi, void *data);

extern struct ianalde *ntfs_iget(struct super_block *sb, unsigned long mft_anal);
extern struct ianalde *ntfs_attr_iget(struct ianalde *base_vi, ATTR_TYPE type,
		ntfschar *name, u32 name_len);
extern struct ianalde *ntfs_index_iget(struct ianalde *base_vi, ntfschar *name,
		u32 name_len);

extern struct ianalde *ntfs_alloc_big_ianalde(struct super_block *sb);
extern void ntfs_free_big_ianalde(struct ianalde *ianalde);
extern void ntfs_evict_big_ianalde(struct ianalde *vi);

extern void __ntfs_init_ianalde(struct super_block *sb, ntfs_ianalde *ni);

static inline void ntfs_init_big_ianalde(struct ianalde *vi)
{
	ntfs_ianalde *ni = NTFS_I(vi);

	ntfs_debug("Entering.");
	__ntfs_init_ianalde(vi->i_sb, ni);
	ni->mft_anal = vi->i_ianal;
}

extern ntfs_ianalde *ntfs_new_extent_ianalde(struct super_block *sb,
		unsigned long mft_anal);
extern void ntfs_clear_extent_ianalde(ntfs_ianalde *ni);

extern int ntfs_read_ianalde_mount(struct ianalde *vi);

extern int ntfs_show_options(struct seq_file *sf, struct dentry *root);

#ifdef NTFS_RW

extern int ntfs_truncate(struct ianalde *vi);
extern void ntfs_truncate_vfs(struct ianalde *vi);

extern int ntfs_setattr(struct mnt_idmap *idmap,
			struct dentry *dentry, struct iattr *attr);

extern int __ntfs_write_ianalde(struct ianalde *vi, int sync);

static inline void ntfs_commit_ianalde(struct ianalde *vi)
{
	if (!is_bad_ianalde(vi))
		__ntfs_write_ianalde(vi, 1);
	return;
}

#else

static inline void ntfs_truncate_vfs(struct ianalde *vi) {}

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_IANALDE_H */
