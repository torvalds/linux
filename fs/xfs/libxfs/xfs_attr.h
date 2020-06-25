/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000,2002-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
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

/*
 * The maximum size (into the kernel or returned from the kernel) of an
 * attribute value or the buffer used for an attr_list() call.  Larger
 * sizes will result in an ERANGE return code.
 */
#define	ATTR_MAX_VALUELEN	(64*1024)	/* max length of a value */

/*
 * Kernel-internal version of the attrlist cursor.
 */
struct xfs_attrlist_cursor_kern {
	__u32	hashval;	/* hash value of next entry to add */
	__u32	blkno;		/* block containing entry (suggestion) */
	__u32	offset;		/* offset in list of equal-hashvals */
	__u16	pad1;		/* padding to match user-level */
	__u8	pad2;		/* padding to match user-level */
	__u8	initted;	/* T/F: cursor has been initialized */
};


/*========================================================================
 * Structure used to pass context around among the routines.
 *========================================================================*/


/* void; state communicated via *context */
typedef void (*put_listent_func_t)(struct xfs_attr_list_context *, int,
			      unsigned char *, int, int);

struct xfs_attr_list_context {
	struct xfs_trans	*tp;
	struct xfs_inode	*dp;		/* inode */
	struct xfs_attrlist_cursor_kern cursor;	/* position in list */
	void			*buffer;	/* output buffer */

	/*
	 * Abort attribute list iteration if non-zero.  Can be used to pass
	 * error values to the xfs_attr_list caller.
	 */
	int			seen_enough;
	bool			allow_incomplete;

	ssize_t			count;		/* num used entries */
	int			dupcnt;		/* count dup hashvals seen */
	int			bufsize;	/* total buffer size */
	int			firstu;		/* first used byte in buffer */
	unsigned int		attr_filter;	/* XFS_ATTR_{ROOT,SECURE} */
	int			resynch;	/* T/F: resynch with cursor */
	put_listent_func_t	put_listent;	/* list output fmt function */
	int			index;		/* index into output buffer */
};


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Overall external interface routines.
 */
int xfs_attr_inactive(struct xfs_inode *dp);
int xfs_attr_list_ilocked(struct xfs_attr_list_context *);
int xfs_attr_list(struct xfs_attr_list_context *);
int xfs_inode_hasattr(struct xfs_inode *ip);
int xfs_attr_get_ilocked(struct xfs_da_args *args);
int xfs_attr_get(struct xfs_da_args *args);
int xfs_attr_set(struct xfs_da_args *args);
int xfs_attr_set_args(struct xfs_da_args *args);
int xfs_attr_remove_args(struct xfs_da_args *args);
bool xfs_attr_namecheck(const void *name, size_t length);

#endif	/* __XFS_ATTR_H__ */
