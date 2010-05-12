/*
 *   fs/cifs/cifsfs.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002, 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _CIFSFS_H
#define _CIFSFS_H

#define ROOT_I 2

/*
 * ino_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static inline ino_t
cifs_uniqueid_to_ino_t(u64 fileid)
{
	ino_t ino = (ino_t) fileid;
	if (sizeof(ino_t) < sizeof(u64))
		ino ^= fileid >> (sizeof(u64)-sizeof(ino_t)) * 8;
	return ino;
}

extern struct file_system_type cifs_fs_type;
extern const struct address_space_operations cifs_addr_ops;
extern const struct address_space_operations cifs_addr_ops_smallbuf;

/* Functions related to super block operations */
/* extern const struct super_operations cifs_super_ops;*/
extern void cifs_read_inode(struct inode *);
/*extern void cifs_delete_inode(struct inode *);*/  /* BB not needed yet */
/* extern void cifs_write_inode(struct inode *); */ /* BB not needed yet */

/* Functions related to inodes */
extern const struct inode_operations cifs_dir_inode_ops;
extern struct inode *cifs_root_iget(struct super_block *, unsigned long);
extern int cifs_create(struct inode *, struct dentry *, int,
		       struct nameidata *);
extern struct dentry *cifs_lookup(struct inode *, struct dentry *,
				  struct nameidata *);
extern int cifs_unlink(struct inode *dir, struct dentry *dentry);
extern int cifs_hardlink(struct dentry *, struct inode *, struct dentry *);
extern int cifs_mknod(struct inode *, struct dentry *, int, dev_t);
extern int cifs_mkdir(struct inode *, struct dentry *, int);
extern int cifs_rmdir(struct inode *, struct dentry *);
extern int cifs_rename(struct inode *, struct dentry *, struct inode *,
		       struct dentry *);
extern int cifs_revalidate_file(struct file *filp);
extern int cifs_revalidate_dentry(struct dentry *);
extern int cifs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern int cifs_setattr(struct dentry *, struct iattr *);

extern const struct inode_operations cifs_file_inode_ops;
extern const struct inode_operations cifs_symlink_inode_ops;
extern const struct inode_operations cifs_dfs_referral_inode_operations;


/* Functions related to files and directories */
extern const struct file_operations cifs_file_ops;
extern const struct file_operations cifs_file_direct_ops; /* if directio mnt */
extern const struct file_operations cifs_file_nobrl_ops;
extern const struct file_operations cifs_file_direct_nobrl_ops; /* no brlocks */
extern int cifs_open(struct inode *inode, struct file *file);
extern int cifs_close(struct inode *inode, struct file *file);
extern int cifs_closedir(struct inode *inode, struct file *file);
extern ssize_t cifs_user_read(struct file *file, char __user *read_data,
			 size_t read_size, loff_t *poffset);
extern ssize_t cifs_user_write(struct file *file, const char __user *write_data,
			 size_t write_size, loff_t *poffset);
extern int cifs_lock(struct file *, int, struct file_lock *);
extern int cifs_fsync(struct file *, struct dentry *, int);
extern int cifs_flush(struct file *, fl_owner_t id);
extern int cifs_file_mmap(struct file * , struct vm_area_struct *);
extern const struct file_operations cifs_dir_ops;
extern int cifs_dir_open(struct inode *inode, struct file *file);
extern int cifs_readdir(struct file *file, void *direntry, filldir_t filldir);

/* Functions related to dir entries */
extern const struct dentry_operations cifs_dentry_ops;
extern const struct dentry_operations cifs_ci_dentry_ops;

/* Functions related to symlinks */
extern void *cifs_follow_link(struct dentry *direntry, struct nameidata *nd);
extern void cifs_put_link(struct dentry *direntry,
			  struct nameidata *nd, void *);
extern int cifs_readlink(struct dentry *direntry, char __user *buffer,
			 int buflen);
extern int cifs_symlink(struct inode *inode, struct dentry *direntry,
			const char *symname);
extern int	cifs_removexattr(struct dentry *, const char *);
extern int 	cifs_setxattr(struct dentry *, const char *, const void *,
			size_t, int);
extern ssize_t	cifs_getxattr(struct dentry *, const char *, void *, size_t);
extern ssize_t	cifs_listxattr(struct dentry *, char *, size_t);
extern long cifs_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

#ifdef CONFIG_CIFS_EXPERIMENTAL
extern const struct export_operations cifs_export_ops;
#endif /* EXPERIMENTAL */

#define CIFS_VERSION   "1.62"
#endif				/* _CIFSFS_H */
