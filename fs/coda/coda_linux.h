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
extern const struct inode_operations coda_dir_inode_operations;
extern const struct inode_operations coda_file_inode_operations;
extern const struct inode_operations coda_ioctl_inode_operations;

extern const struct dentry_operations coda_dentry_operations;

extern const struct address_space_operations coda_file_aops;
extern const struct address_space_operations coda_symlink_aops;

extern const struct file_operations coda_dir_operations;
extern const struct file_operations coda_file_operations;
extern const struct file_operations coda_ioctl_operations;

/* operations shared over more than one file */
int coda_open(struct inode *i, struct file *f);
int coda_release(struct inode *i, struct file *f);
int coda_permission(struct inode *inode, int mask);
int coda_revalidate_inode(struct inode *);
int coda_getattr(struct vfsmount *, struct dentry *, struct kstat *);
int coda_setattr(struct dentry *, struct iattr *);

/* this file:  heloers */
char *coda_f2s(struct CodaFid *f);
int coda_iscontrol(const char *name, size_t length);

void coda_vattr_to_iattr(struct inode *, struct coda_vattr *);
void coda_iattr_to_vattr(struct iattr *, struct coda_vattr *);
unsigned short coda_flags_to_cflags(unsigned short);

/* sysctl.h */
void coda_sysctl_init(void);
void coda_sysctl_clean(void);

#define CODA_ALLOC(ptr, cast, size) do { \
    if (size < PAGE_SIZE) \
        ptr = kzalloc((unsigned long) size, GFP_KERNEL); \
    else \
        ptr = (cast)vzalloc((unsigned long) size); \
    if (!ptr) \
	pr_warn("kernel malloc returns 0 at %s:%d\n", __FILE__, __LINE__); \
} while (0)


#define CODA_FREE(ptr,size) \
    do { if (size < PAGE_SIZE) kfree((ptr)); else vfree((ptr)); } while (0)

/* inode to cnode access functions */

static inline struct coda_inode_info *ITOC(struct inode *inode)
{
	return container_of(inode, struct coda_inode_info, vfs_inode);
}

static __inline__ struct CodaFid *coda_i2f(struct inode *inode)
{
	return &(ITOC(inode)->c_fid);
}

static __inline__ char *coda_i2s(struct inode *inode)
{
	return coda_f2s(&(ITOC(inode)->c_fid));
}

/* this will not zap the inode away */
static __inline__ void coda_flag_inode(struct inode *inode, int flag)
{
	struct coda_inode_info *cii = ITOC(inode);

	spin_lock(&cii->c_lock);
	cii->c_flags |= flag;
	spin_unlock(&cii->c_lock);
}		

#endif
