/* $Id: vfsmod.h $ */
/** @file
 * vboxsf - Linux Shared Folders VFS, internal header.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h
#define GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#include "the-linux-kernel.h"
#include <VBox/log.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
# include <linux/backing-dev.h>
#endif

#include <VBox/VBoxGuestLibSharedFolders.h>
#include "vbsfmount.h"

#define DIR_BUFFER_SIZE (16*_1K)

/* per-shared folder information */
struct sf_glob_info {
	VBGLSFMAP map;
	struct nls_table *nls;
	int ttl;
	int uid;
	int gid;
	int dmode;
	int fmode;
	int dmask;
	int fmask;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
	struct backing_dev_info bdi;
#endif
	char tag[32];		/**< Mount tag for VBoxService automounter.  @since 6.0 */
};

/* per-inode information */
struct sf_inode_info {
	/* which file */
	SHFLSTRING *path;
	/* some information was changed, update data on next revalidate */
	int force_restat;
	/* directory content changed, update the whole directory on next sf_getdent */
	int force_reread;
	/* file structure, only valid between open() and release() */
	struct file *file;
	/* handle valid if a file was created with sf_create_aux until it will
	 * be opened with sf_reg_open() */
	SHFLHANDLE handle;
};

struct sf_dir_info {
	struct list_head info_list;
};

struct sf_dir_buf {
	size_t cEntries;
	size_t cbFree;
	size_t cbUsed;
	void *buf;
	struct list_head head;
};

struct sf_reg_info {
	SHFLHANDLE handle;
};

/* globals */
extern VBGLSFCLIENT client_handle;

/* forward declarations */
extern struct inode_operations sf_dir_iops;
extern struct inode_operations sf_lnk_iops;
extern struct inode_operations sf_reg_iops;
extern struct file_operations sf_dir_fops;
extern struct file_operations sf_reg_fops;
extern struct dentry_operations sf_dentry_ops;
extern struct address_space_operations sf_reg_aops;

extern void sf_init_inode(struct sf_glob_info *sf_g, struct inode *inode,
			  PSHFLFSOBJINFO info);
extern int sf_stat(const char *caller, struct sf_glob_info *sf_g,
		   SHFLSTRING * path, PSHFLFSOBJINFO result, int ok_to_fail);
extern int sf_inode_revalidate(struct dentry *dentry);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
extern int sf_getattr(const struct path *path, struct kstat *kstat,
		      u32 request_mask, unsigned int query_flags);
# else
extern int sf_getattr(struct vfsmount *mnt, struct dentry *dentry,
		      struct kstat *kstat);
# endif
extern int sf_setattr(struct dentry *dentry, struct iattr *iattr);
#endif
extern int sf_path_from_dentry(const char *caller, struct sf_glob_info *sf_g,
			       struct sf_inode_info *sf_i,
			       struct dentry *dentry, SHFLSTRING ** result);
extern int sf_nlscpy(struct sf_glob_info *sf_g, char *name,
		     size_t name_bound_len, const unsigned char *utf8_name,
		     size_t utf8_len);
extern void sf_dir_info_free(struct sf_dir_info *p);
extern void sf_dir_info_empty(struct sf_dir_info *p);
extern struct sf_dir_info *sf_dir_info_alloc(void);
extern int sf_dir_read_all(struct sf_glob_info *sf_g,
			   struct sf_inode_info *sf_i, struct sf_dir_info *sf_d,
			   SHFLHANDLE handle);
extern int sf_init_backing_dev(struct sf_glob_info *sf_g);
extern void sf_done_backing_dev(struct sf_glob_info *sf_g);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
# define STRUCT_STATFS  struct statfs
#else
# define STRUCT_STATFS  struct kstatfs
#endif
int sf_get_volume_info(struct super_block *sb, STRUCT_STATFS * stat);

#ifdef __cplusplus
# define CMC_API __attribute__ ((cdecl, regparm (0)))
#else
# define CMC_API __attribute__ ((regparm (0)))
#endif

#define TRACE() LogFunc(("tracepoint\n"))

/* Following casts are here to prevent assignment of void * to
   pointers of arbitrary type */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
# define GET_GLOB_INFO(sb)       ((struct sf_glob_info *) (sb)->u.generic_sbp)
# define SET_GLOB_INFO(sb, sf_g) (sb)->u.generic_sbp = sf_g
#else
# define GET_GLOB_INFO(sb)       ((struct sf_glob_info *) (sb)->s_fs_info)
# define SET_GLOB_INFO(sb, sf_g) (sb)->s_fs_info = sf_g
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) || defined(KERNEL_FC6)
/* FC6 kernel 2.6.18, vanilla kernel 2.6.19+ */
# define GET_INODE_INFO(i)       ((struct sf_inode_info *) (i)->i_private)
# define SET_INODE_INFO(i, sf_i) (i)->i_private = sf_i
#else
/* vanilla kernel up to 2.6.18 */
# define GET_INODE_INFO(i)       ((struct sf_inode_info *) (i)->u.generic_ip)
# define SET_INODE_INFO(i, sf_i) (i)->u.generic_ip = sf_i
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
# define GET_F_DENTRY(f)        (f->f_path.dentry)
#else
# define GET_F_DENTRY(f)        (f->f_dentry)
#endif

#endif /* !GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h */
