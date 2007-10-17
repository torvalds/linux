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

/*
 * Access Control Lists
 */
typedef __uint16_t	xfs_acl_perm_t;
typedef __int32_t	xfs_acl_type_t;
typedef __int32_t	xfs_acl_tag_t;
typedef __int32_t	xfs_acl_id_t;

#define XFS_ACL_MAX_ENTRIES 25
#define XFS_ACL_NOT_PRESENT (-1)

typedef struct xfs_acl_entry {
	xfs_acl_tag_t	ae_tag;
	xfs_acl_id_t	ae_id;
	xfs_acl_perm_t	ae_perm;
} xfs_acl_entry_t;

typedef struct xfs_acl {
	__int32_t	acl_cnt;
	xfs_acl_entry_t	acl_entry[XFS_ACL_MAX_ENTRIES];
} xfs_acl_t;

/* On-disk XFS extended attribute names */
#define SGI_ACL_FILE	"SGI_ACL_FILE"
#define SGI_ACL_DEFAULT	"SGI_ACL_DEFAULT"
#define SGI_ACL_FILE_SIZE	(sizeof(SGI_ACL_FILE)-1)
#define SGI_ACL_DEFAULT_SIZE	(sizeof(SGI_ACL_DEFAULT)-1)


#ifdef CONFIG_XFS_POSIX_ACL

struct vattr;
struct xfs_inode;

extern struct kmem_zone *xfs_acl_zone;
#define xfs_acl_zone_init(zone, name)	\
		(zone) = kmem_zone_init(sizeof(xfs_acl_t), (name))
#define xfs_acl_zone_destroy(zone)	kmem_zone_destroy(zone)

extern int xfs_acl_inherit(bhv_vnode_t *, mode_t mode, xfs_acl_t *);
extern int xfs_acl_iaccess(struct xfs_inode *, mode_t, cred_t *);
extern int xfs_acl_vtoacl(bhv_vnode_t *, xfs_acl_t *, xfs_acl_t *);
extern int xfs_acl_vhasacl_access(bhv_vnode_t *);
extern int xfs_acl_vhasacl_default(bhv_vnode_t *);
extern int xfs_acl_vset(bhv_vnode_t *, void *, size_t, int);
extern int xfs_acl_vget(bhv_vnode_t *, void *, size_t, int);
extern int xfs_acl_vremove(bhv_vnode_t *, int);

#define _ACL_TYPE_ACCESS	1
#define _ACL_TYPE_DEFAULT	2
#define _ACL_PERM_INVALID(perm)	((perm) & ~(ACL_READ|ACL_WRITE|ACL_EXECUTE))

#define _ACL_INHERIT(c,m,d)	(xfs_acl_inherit(c,m,d))
#define _ACL_GET_ACCESS(pv,pa)	(xfs_acl_vtoacl(pv,pa,NULL) == 0)
#define _ACL_GET_DEFAULT(pv,pd)	(xfs_acl_vtoacl(pv,NULL,pd) == 0)
#define _ACL_ACCESS_EXISTS	xfs_acl_vhasacl_access
#define _ACL_DEFAULT_EXISTS	xfs_acl_vhasacl_default
#define _ACL_XFS_IACCESS(i,m,c) (XFS_IFORK_Q(i) ? xfs_acl_iaccess(i,m,c) : -1)

#define _ACL_ALLOC(a)		((a) = kmem_zone_alloc(xfs_acl_zone, KM_SLEEP))
#define _ACL_FREE(a)		((a)? kmem_zone_free(xfs_acl_zone, (a)):(void)0)

#else
#define xfs_acl_zone_init(zone,name)
#define xfs_acl_zone_destroy(zone)
#define xfs_acl_vset(v,p,sz,t)	(-EOPNOTSUPP)
#define xfs_acl_vget(v,p,sz,t)	(-EOPNOTSUPP)
#define xfs_acl_vremove(v,t)	(-EOPNOTSUPP)
#define xfs_acl_vhasacl_access(v)	(0)
#define xfs_acl_vhasacl_default(v)	(0)
#define _ACL_ALLOC(a)		(1)	/* successfully allocate nothing */
#define _ACL_FREE(a)		((void)0)
#define _ACL_INHERIT(c,m,d)	(0)
#define _ACL_GET_ACCESS(pv,pa)	(0)
#define _ACL_GET_DEFAULT(pv,pd)	(0)
#define _ACL_ACCESS_EXISTS	(NULL)
#define _ACL_DEFAULT_EXISTS	(NULL)
#define _ACL_XFS_IACCESS(i,m,c) (-1)
#endif

#endif	/* __XFS_ACL_H__ */
