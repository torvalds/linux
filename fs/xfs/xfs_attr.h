/*
 * Copyright (c) 2000,2002-2003,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_ATTR_H__
#define	__XFS_ATTR_H__

/*
 * xfs_attr.h
 *
 * Large attribute lists are structured around Btrees where all the data
 * elements are in the leaf nodes.  Attribute names are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of an attribute name may not be unique, we may have duplicate keys.
 * The internal links in the Btree are logical block offsets into the file.
 *
 * Small attribute lists use a different format and are packed as tightly
 * as possible so as to fit into the literal area of the inode.
 */

/*========================================================================
 * External interfaces
 *========================================================================*/

struct cred;
struct bhv_vnode;

typedef int (*attrset_t)(struct bhv_vnode *, char *, void *, size_t, int);
typedef int (*attrget_t)(struct bhv_vnode *, char *, void *, size_t, int);
typedef int (*attrremove_t)(struct bhv_vnode *, char *, int);
typedef int (*attrexists_t)(struct bhv_vnode *);
typedef int (*attrcapable_t)(struct bhv_vnode *, struct cred *);

typedef struct attrnames {
	char *		attr_name;
	unsigned int	attr_namelen;
	unsigned int	attr_flag;
	attrget_t	attr_get;
	attrset_t	attr_set;
	attrremove_t	attr_remove;
	attrexists_t	attr_exists;
	attrcapable_t	attr_capable;
} attrnames_t;

#define ATTR_NAMECOUNT	4
extern struct attrnames attr_user;
extern struct attrnames attr_secure;
extern struct attrnames attr_system;
extern struct attrnames attr_trusted;
extern struct attrnames *attr_namespaces[ATTR_NAMECOUNT];

extern attrnames_t *attr_lookup_namespace(char *, attrnames_t **, int);
extern int attr_generic_list(struct bhv_vnode *, void *, size_t, int, ssize_t *);

#define ATTR_DONTFOLLOW	0x0001	/* -- unused, from IRIX -- */
#define ATTR_ROOT	0x0002	/* use attrs in root (trusted) namespace */
#define ATTR_TRUST	0x0004	/* -- unused, from IRIX -- */
#define ATTR_SECURE	0x0008	/* use attrs in security namespace */
#define ATTR_CREATE	0x0010	/* pure create: fail if attr already exists */
#define ATTR_REPLACE	0x0020	/* pure set: fail if attr does not exist */
#define ATTR_SYSTEM	0x0100	/* use attrs in system (pseudo) namespace */

#define ATTR_KERNACCESS	0x0400	/* [kernel] iaccess, inode held io-locked */
#define ATTR_KERNOTIME	0x1000	/* [kernel] don't update inode timestamps */
#define ATTR_KERNOVAL	0x2000	/* [kernel] get attr size only, not value */
#define ATTR_KERNAMELS	0x4000	/* [kernel] list attr names (simple list) */

#define ATTR_KERNORMALS	0x0800	/* [kernel] normal attr list: user+secure */
#define ATTR_KERNROOTLS	0x8000	/* [kernel] include root in the attr list */
#define ATTR_KERNFULLS	(ATTR_KERNORMALS|ATTR_KERNROOTLS)

/*
 * The maximum size (into the kernel or returned from the kernel) of an
 * attribute value or the buffer used for an attr_list() call.  Larger
 * sizes will result in an ERANGE return code.
 */
#define	ATTR_MAX_VALUELEN	(64*1024)	/* max length of a value */

/*
 * Define how lists of attribute names are returned to the user from
 * the attr_list() call.  A large, 32bit aligned, buffer is passed in
 * along with its size.  We put an array of offsets at the top that each
 * reference an attrlist_ent_t and pack the attrlist_ent_t's at the bottom.
 */
typedef struct attrlist {
	__s32	al_count;	/* number of entries in attrlist */
	__s32	al_more;	/* T/F: more attrs (do call again) */
	__s32	al_offset[1];	/* byte offsets of attrs [var-sized] */
} attrlist_t;

/*
 * Show the interesting info about one attribute.  This is what the
 * al_offset[i] entry points to.
 */
typedef struct attrlist_ent {	/* data from attr_list() */
	__u32	a_valuelen;	/* number bytes in value of attr */
	char	a_name[1];	/* attr name (NULL terminated) */
} attrlist_ent_t;

/*
 * Given a pointer to the (char*) buffer containing the attr_list() result,
 * and an index, return a pointer to the indicated attribute in the buffer.
 */
#define	ATTR_ENTRY(buffer, index)		\
	((attrlist_ent_t *)			\
	 &((char *)buffer)[ ((attrlist_t *)(buffer))->al_offset[index] ])

/*
 * Multi-attribute operation vector.
 */
typedef struct attr_multiop {
	int	am_opcode;	/* operation to perform (ATTR_OP_GET, etc.) */
	int	am_error;	/* [out arg] result of this sub-op (an errno) */
	char	*am_attrname;	/* attribute name to work with */
	char	*am_attrvalue;	/* [in/out arg] attribute value (raw bytes) */
	int	am_length;	/* [in/out arg] length of value */
	int	am_flags;	/* bitwise OR of attr API flags defined above */
} attr_multiop_t;

#define ATTR_OP_GET	1	/* return the indicated attr's value */
#define ATTR_OP_SET	2	/* set/create the indicated attr/value pair */
#define ATTR_OP_REMOVE	3	/* remove the indicated attr */

/*
 * Kernel-internal version of the attrlist cursor.
 */
typedef struct attrlist_cursor_kern {
	__u32	hashval;	/* hash value of next entry to add */
	__u32	blkno;		/* block containing entry (suggestion) */
	__u32	offset;		/* offset in list of equal-hashvals */
	__u16	pad1;		/* padding to match user-level */
	__u8	pad2;		/* padding to match user-level */
	__u8	initted;	/* T/F: cursor has been initialized */
} attrlist_cursor_kern_t;


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct xfs_inode;
struct attrlist_cursor_kern;
struct xfs_da_args;

/*
 * Overall external interface routines.
 */
int xfs_attr_get(bhv_desc_t *, const char *, char *, int *, int, struct cred *);
int xfs_attr_set(bhv_desc_t *, const char *, char *, int, int, struct cred *);
int xfs_attr_remove(bhv_desc_t *, const char *, int, struct cred *);
int xfs_attr_list(bhv_desc_t *, char *, int, int,
			 struct attrlist_cursor_kern *, struct cred *);
int xfs_attr_inactive(struct xfs_inode *dp);

int xfs_attr_shortform_getvalue(struct xfs_da_args *);
int xfs_attr_fetch(struct xfs_inode *, const char *, int,
			char *, int *, int, struct cred *);

#endif	/* __XFS_ATTR_H__ */
