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

struct xfs_inode;
struct xfs_da_args;
struct xfs_attr_list_context;

/*
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


#define ATTR_DONTFOLLOW	0x0001	/* -- unused, from IRIX -- */
#define ATTR_ROOT	0x0002	/* use attrs in root (trusted) namespace */
#define ATTR_TRUST	0x0004	/* -- unused, from IRIX -- */
#define ATTR_SECURE	0x0008	/* use attrs in security namespace */
#define ATTR_CREATE	0x0010	/* pure create: fail if attr already exists */
#define ATTR_REPLACE	0x0020	/* pure set: fail if attr does not exist */

#define ATTR_KERNOTIME	0x1000	/* [kernel] don't update inode timestamps */
#define ATTR_KERNOVAL	0x2000	/* [kernel] get attr size only, not value */

#define XFS_ATTR_FLAGS \
	{ ATTR_DONTFOLLOW, 	"DONTFOLLOW" }, \
	{ ATTR_ROOT,		"ROOT" }, \
	{ ATTR_TRUST,		"TRUST" }, \
	{ ATTR_SECURE,		"SECURE" }, \
	{ ATTR_CREATE,		"CREATE" }, \
	{ ATTR_REPLACE,		"REPLACE" }, \
	{ ATTR_KERNOTIME,	"KERNOTIME" }, \
	{ ATTR_KERNOVAL,	"KERNOVAL" }

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
 * Structure used to pass context around among the routines.
 *========================================================================*/


typedef int (*put_listent_func_t)(struct xfs_attr_list_context *, int,
				      char *, int, int, char *);

typedef struct xfs_attr_list_context {
	struct xfs_inode		*dp;		/* inode */
	struct attrlist_cursor_kern	*cursor;	/* position in list */
	char				*alist;		/* output buffer */
	int				seen_enough;	/* T/F: seen enough of list? */
	ssize_t				count;		/* num used entries */
	int				dupcnt;		/* count dup hashvals seen */
	int				bufsize;	/* total buffer size */
	int				firstu;		/* first used byte in buffer */
	int				flags;		/* from VOP call */
	int				resynch;	/* T/F: resynch with cursor */
	int				put_value;	/* T/F: need value for listent */
	put_listent_func_t		put_listent;	/* list output fmt function */
	int				index;		/* index into output buffer */
} xfs_attr_list_context_t;


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Overall external interface routines.
 */
int xfs_attr_calc_size(struct xfs_inode *, int, int, int *);
int xfs_attr_inactive(struct xfs_inode *dp);
int xfs_attr_rmtval_get(struct xfs_da_args *args);
int xfs_attr_list_int(struct xfs_attr_list_context *);

#endif	/* __XFS_ATTR_H__ */
