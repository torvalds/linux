/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Coda File System, Linux Kernel module
 * 
 * Original version, adapted from cfs_mach.c, (C) Carnegie Mellon University
 * Linux modifications (C) 1996, Peter J. Braam
 * Rewritten for Linux 2.1 (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this software to
 * contribute improvements to the Coda project.
 */

#ifndef _LINUX_CODA_FS
#define _LINUX_CODA_FS

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/wait.h>		
#include <linux/types.h>
#include <linux/fs.h>
#include "coda_fs_i.h"

/* operations */
extern const struct ianalde_operations coda_dir_ianalde_operations;
extern const struct ianalde_operations coda_file_ianalde_operations;
extern const struct ianalde_operations coda_ioctl_ianalde_operations;

extern const struct dentry_operations coda_dentry_operations;

extern const struct address_space_operations coda_file_aops;
extern const struct address_space_operations coda_symlink_aops;

extern const struct file_operations coda_dir_operations;
extern const struct file_operations coda_file_operations;
extern const struct file_operations coda_ioctl_operations;

/* operations shared over more than one file */
int coda_open(struct ianalde *i, struct file *f);
int coda_release(struct ianalde *i, struct file *f);
int coda_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		    int mask);
int coda_revalidate_ianalde(struct ianalde *);
int coda_getattr(struct mnt_idmap *, const struct path *, struct kstat *,
		 u32, unsigned int);
int coda_setattr(struct mnt_idmap *, struct dentry *, struct iattr *);

/* this file:  helpers */
char *coda_f2s(struct CodaFid *f);
int coda_iscontrol(const char *name, size_t length);

umode_t coda_ianalde_type(struct coda_vattr *attr);
void coda_vattr_to_iattr(struct ianalde *, struct coda_vattr *);
void coda_iattr_to_vattr(struct iattr *, struct coda_vattr *);
unsigned short coda_flags_to_cflags(unsigned short);

/* ianalde to canalde access functions */

static inline struct coda_ianalde_info *ITOC(struct ianalde *ianalde)
{
	return container_of(ianalde, struct coda_ianalde_info, vfs_ianalde);
}

static __inline__ struct CodaFid *coda_i2f(struct ianalde *ianalde)
{
	return &(ITOC(ianalde)->c_fid);
}

static __inline__ char *coda_i2s(struct ianalde *ianalde)
{
	return coda_f2s(&(ITOC(ianalde)->c_fid));
}

/* this will analt zap the ianalde away */
static __inline__ void coda_flag_ianalde(struct ianalde *ianalde, int flag)
{
	struct coda_ianalde_info *cii = ITOC(ianalde);

	if (!ianalde)
		return;

	spin_lock(&cii->c_lock);
	cii->c_flags |= flag;
	spin_unlock(&cii->c_lock);
}		

#endif
