/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DIR_H__
#define	__XFS_DIR_H__

/*
 * Large directories are structured around Btrees where all the data
 * elements are in the leaf nodes.  Filenames are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of a filename may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 *
 * Small directories use a different format and are packed as tightly
 * as possible so as to fit into the literal area of the inode.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct uio;
struct xfs_bmap_free;
struct xfs_da_args;
struct xfs_dinode;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Directory function types.
 * Put in structures (xfs_dirops_t) for v1 and v2 directories.
 */
typedef void	(*xfs_dir_mount_t)(struct xfs_mount *mp);
typedef int	(*xfs_dir_isempty_t)(struct xfs_inode *dp);
typedef int	(*xfs_dir_init_t)(struct xfs_trans *tp,
				  struct xfs_inode *dp,
				  struct xfs_inode *pdp);
typedef int	(*xfs_dir_createname_t)(struct xfs_trans *tp,
					struct xfs_inode *dp,
					char *name,
					int namelen,
					xfs_ino_t inum,
					xfs_fsblock_t *first,
					struct xfs_bmap_free *flist,
					xfs_extlen_t total);
typedef int	(*xfs_dir_lookup_t)(struct xfs_trans *tp,
				    struct xfs_inode *dp,
				    char *name,
				    int namelen,
				    xfs_ino_t *inum);
typedef int	(*xfs_dir_removename_t)(struct xfs_trans *tp,
					struct xfs_inode *dp,
					char *name,
					int namelen,
					xfs_ino_t ino,
					xfs_fsblock_t *first,
					struct xfs_bmap_free *flist,
					xfs_extlen_t total);
typedef int	(*xfs_dir_getdents_t)(struct xfs_trans *tp,
				      struct xfs_inode *dp,
				      struct uio *uio,
				      int *eofp);
typedef int	(*xfs_dir_replace_t)(struct xfs_trans *tp,
				     struct xfs_inode *dp,
				     char *name,
				     int namelen,
				     xfs_ino_t inum,
				     xfs_fsblock_t *first,
				     struct xfs_bmap_free *flist,
				     xfs_extlen_t total);
typedef int	(*xfs_dir_canenter_t)(struct xfs_trans *tp,
				      struct xfs_inode *dp,
				      char *name,
				      int namelen);
typedef int	(*xfs_dir_shortform_validate_ondisk_t)(struct xfs_mount *mp,
						       struct xfs_dinode *dip);
typedef int	(*xfs_dir_shortform_to_single_t)(struct xfs_da_args *args);

typedef struct xfs_dirops {
	xfs_dir_mount_t				xd_mount;
	xfs_dir_isempty_t			xd_isempty;
	xfs_dir_init_t				xd_init;
	xfs_dir_createname_t			xd_createname;
	xfs_dir_lookup_t			xd_lookup;
	xfs_dir_removename_t			xd_removename;
	xfs_dir_getdents_t			xd_getdents;
	xfs_dir_replace_t			xd_replace;
	xfs_dir_canenter_t			xd_canenter;
	xfs_dir_shortform_validate_ondisk_t	xd_shortform_validate_ondisk;
	xfs_dir_shortform_to_single_t		xd_shortform_to_single;
} xfs_dirops_t;

/*
 * Overall external interface routines.
 */
void	xfs_dir_startup(void);	/* called exactly once */

#define	XFS_DIR_MOUNT(mp)	\
	((mp)->m_dirops.xd_mount(mp))
#define	XFS_DIR_ISEMPTY(mp,dp)	\
	((mp)->m_dirops.xd_isempty(dp))
#define	XFS_DIR_INIT(mp,tp,dp,pdp)	\
	((mp)->m_dirops.xd_init(tp,dp,pdp))
#define	XFS_DIR_CREATENAME(mp,tp,dp,name,namelen,inum,first,flist,total) \
	((mp)->m_dirops.xd_createname(tp,dp,name,namelen,inum,first,flist,\
				      total))
#define	XFS_DIR_LOOKUP(mp,tp,dp,name,namelen,inum)	\
	((mp)->m_dirops.xd_lookup(tp,dp,name,namelen,inum))
#define	XFS_DIR_REMOVENAME(mp,tp,dp,name,namelen,ino,first,flist,total)	\
	((mp)->m_dirops.xd_removename(tp,dp,name,namelen,ino,first,flist,total))
#define	XFS_DIR_GETDENTS(mp,tp,dp,uio,eofp)	\
	((mp)->m_dirops.xd_getdents(tp,dp,uio,eofp))
#define	XFS_DIR_REPLACE(mp,tp,dp,name,namelen,inum,first,flist,total)	\
	((mp)->m_dirops.xd_replace(tp,dp,name,namelen,inum,first,flist,total))
#define	XFS_DIR_CANENTER(mp,tp,dp,name,namelen)	\
	((mp)->m_dirops.xd_canenter(tp,dp,name,namelen))
#define	XFS_DIR_SHORTFORM_VALIDATE_ONDISK(mp,dip)	\
	((mp)->m_dirops.xd_shortform_validate_ondisk(mp,dip))
#define	XFS_DIR_SHORTFORM_TO_SINGLE(mp,args)	\
	((mp)->m_dirops.xd_shortform_to_single(args))

#define	XFS_DIR_IS_V1(mp)	((mp)->m_dirversion == 1)
extern xfs_dirops_t xfsv1_dirops;

#endif	/* __XFS_DIR_H__ */
