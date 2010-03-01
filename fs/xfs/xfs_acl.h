/*
 * Copyright (c) 2001-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_ACL_H__
#define __XFS_ACL_H__

struct inode;
struct posix_acl;
struct xfs_inode;

#define XFS_ACL_MAX_ENTRIES 25
#define XFS_ACL_NOT_PRESENT (-1)

/* On-disk XFS access control list structure */
struct xfs_acl {
	__be32		acl_cnt;
	struct xfs_acl_entry {
		__be32	ae_tag;
		__be32	ae_id;
		__be16	ae_perm;
	} acl_entry[XFS_ACL_MAX_ENTRIES];
};

/* On-disk XFS extended attribute names */
#define SGI_ACL_FILE		(unsigned char *)"SGI_ACL_FILE"
#define SGI_ACL_DEFAULT		(unsigned char *)"SGI_ACL_DEFAULT"
#define SGI_ACL_FILE_SIZE	(sizeof(SGI_ACL_FILE)-1)
#define SGI_ACL_DEFAULT_SIZE	(sizeof(SGI_ACL_DEFAULT)-1)

#ifdef CONFIG_XFS_POSIX_ACL
extern int xfs_check_acl(struct inode *inode, int mask);
extern struct posix_acl *xfs_get_acl(struct inode *inode, int type);
extern int xfs_inherit_acl(struct inode *inode, struct posix_acl *default_acl);
extern int xfs_acl_chmod(struct inode *inode);
extern int posix_acl_access_exists(struct inode *inode);
extern int posix_acl_default_exists(struct inode *inode);

extern struct xattr_handler xfs_xattr_acl_access_handler;
extern struct xattr_handler xfs_xattr_acl_default_handler;
#else
# define xfs_check_acl					NULL
# define xfs_get_acl(inode, type)			NULL
# define xfs_inherit_acl(inode, default_acl)		0
# define xfs_acl_chmod(inode)				0
# define posix_acl_access_exists(inode)			0
# define posix_acl_default_exists(inode)		0
#endif /* CONFIG_XFS_POSIX_ACL */
#endif	/* __XFS_ACL_H__ */
