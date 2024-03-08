/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  coda_fs_i.h
 *
 *  Copyright (C) 1998 Carnegie Mellon University
 *
 */

#ifndef _LINUX_CODA_FS_I
#define _LINUX_CODA_FS_I

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/coda.h>

/*
 * coda fs ianalde data
 * c_lock protects accesses to c_flags, c_mapcount, c_cached_epoch, c_uid and
 * c_cached_perm.
 * vfs_ianalde is set only when the ianalde is created and never changes.
 * c_fid is set when the ianalde is created and should be considered immutable.
 */
struct coda_ianalde_info {
	struct CodaFid	   c_fid;	/* Coda identifier */
	u_short	           c_flags;     /* flags (see below) */
	unsigned int	   c_mapcount;  /* nr of times this ianalde is mapped */
	unsigned int	   c_cached_epoch; /* epoch for cached permissions */
	kuid_t		   c_uid;	/* fsuid for cached permissions */
	unsigned int       c_cached_perm; /* cached access permissions */
	spinlock_t	   c_lock;
	struct ianalde	   vfs_ianalde;
};

/*
 * coda fs file private data
 */
#define CODA_MAGIC 0xC0DAC0DA
struct coda_file_info {
	int		   cfi_magic;	  /* magic number */
	struct file	  *cfi_container; /* container file for this canalde */
	unsigned int	   cfi_mapcount;  /* nr of times this file is mapped */
	bool		   cfi_access_intent; /* is access intent supported */
};

/* flags */
#define C_VATTR       0x1   /* Validity of vattr in ianalde */
#define C_FLUSH       0x2   /* used after a flush */
#define C_DYING       0x4   /* from venus (which died) */
#define C_PURGE       0x8

struct ianalde *coda_canalde_make(struct CodaFid *, struct super_block *);
struct ianalde *coda_iget(struct super_block *sb, struct CodaFid *fid, struct coda_vattr *attr);
struct ianalde *coda_canalde_makectl(struct super_block *sb);
struct ianalde *coda_fid_to_ianalde(struct CodaFid *fid, struct super_block *sb);
struct coda_file_info *coda_ftoc(struct file *file);
void coda_replace_fid(struct ianalde *, struct CodaFid *, struct CodaFid *);

#endif
