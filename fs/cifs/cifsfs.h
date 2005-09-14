/*
 *   fs/cifs/cifsfs.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002, 2005
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

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

extern struct address_space_operations cifs_addr_ops;

/* Functions related to super block operations */
extern struct super_operations cifs_super_ops;
extern void cifs_read_inode(struct inode *);
extern void cifs_delete_inode(struct inode *);
/* extern void cifs_write_inode(struct inode *); *//* BB not needed yet */

/* Functions related to inodes */
extern struct inode_operations cifs_dir_inode_ops;
extern int cifs_create(struct inode *, struct dentry *, int, 
		       struct nameidata *);
extern struct dentry * cifs_lookup(struct inode *, struct dentry *,
				  struct nameidata *);
extern int cifs_unlink(struct inode *, struct dentry *);
extern int cifs_hardlink(struct dentry *, struct inode *, struct dentry *);
extern int cifs_mknod(struct inode *, struct dentry *, int, dev_t);
extern int cifs_mkdir(struct inode *, struct dentry *, int);
extern int cifs_rmdir(struct inode *, struct dentry *);
extern int cifs_rename(struct inode *, struct dentry *, struct inode *,
		       struct dentry *);
extern int cifs_revalidate(struct dentry *);
extern int cifs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern int cifs_setattr(struct dentry *, struct iattr *);

extern struct inode_operations cifs_file_inode_ops;
extern struct inode_operations cifs_symlink_inode_ops;

/* Functions related to files and directories */
extern struct file_operations cifs_file_ops;
extern struct file_operations cifs_file_direct_ops; /* if directio mount */
extern int cifs_open(struct inode *inode, struct file *file);
extern int cifs_close(struct inode *inode, struct file *file);
extern int cifs_closedir(struct inode *inode, struct file *file);
extern ssize_t cifs_user_read(struct file *file, char __user *read_data,
			 size_t read_size, loff_t * poffset);
extern ssize_t cifs_user_write(struct file *file, const char __user *write_data,
			 size_t write_size, loff_t * poffset);
extern int cifs_lock(struct file *, int, struct file_lock *);
extern int cifs_fsync(struct file *, struct dentry *, int);
extern int cifs_flush(struct file *);
extern int cifs_file_mmap(struct file * , struct vm_area_struct *);
extern struct file_operations cifs_dir_ops;
extern int cifs_dir_open(struct inode *inode, struct file *file);
extern int cifs_readdir(struct file *file, void *direntry, filldir_t filldir);
extern int cifs_dir_notify(struct file *, unsigned long arg);

/* Functions related to dir entries */
extern struct dentry_operations cifs_dentry_ops;

/* Functions related to symlinks */
extern void *cifs_follow_link(struct dentry *direntry, struct nameidata *nd);
extern void cifs_put_link(struct dentry *direntry, struct nameidata *nd, void *);
extern int cifs_readlink(struct dentry *direntry, char __user *buffer, 
			 int buflen);
extern int cifs_symlink(struct inode *inode, struct dentry *direntry,
			const char *symname);
extern int	cifs_removexattr(struct dentry *, const char *);
extern int 	cifs_setxattr(struct dentry *, const char *, const void *,
			size_t, int);
extern ssize_t	cifs_getxattr(struct dentry *, const char *, void *, size_t);
extern ssize_t	cifs_listxattr(struct dentry *, char *, size_t);
extern int cifs_ioctl (struct inode * inode, struct file * filep,
		       unsigned int command, unsigned long arg);
#define CIFS_VERSION   "1.35"
#endif				/* _CIFSFS_H */
