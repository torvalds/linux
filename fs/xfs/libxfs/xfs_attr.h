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


/*
 * ========================================================================
 * Structure used to pass context around among the delayed routines.
 * ========================================================================
 */

/*
 * Below is a state machine diagram for attr remove operations. The  XFS_DAS_*
 * states indicate places where the function would return -EAGAIN, and then
 * immediately resume from after being called by the calling function. States
 * marked as a "subroutine state" indicate that they belong to a subroutine, and
 * so the calling function needs to pass them back to that subroutine to allow
 * it to finish where it left off. But they otherwise do not have a role in the
 * calling function other than just passing through.
 *
 * xfs_attr_remove_iter()
 *              │
 *              v
 *        have attr to remove? ──n──> done
 *              │
 *              y
 *              │
 *              v
 *        are we short form? ──y──> xfs_attr_shortform_remove ──> done
 *              │
 *              n
 *              │
 *              V
 *        are we leaf form? ──y──> xfs_attr_leaf_removename ──> done
 *              │
 *              n
 *              │
 *              V
 *   ┌── need to setup state?
 *   │          │
 *   n          y
 *   │          │
 *   │          v
 *   │ find attr and get state
 *   │ attr has remote blks? ──n─┐
 *   │          │                v
 *   │          │         find and invalidate
 *   │          y         the remote blocks.
 *   │          │         mark attr incomplete
 *   │          ├────────────────┘
 *   └──────────┤
 *              │
 *              v
 *   Have remote blks to remove? ───y─────┐
 *              │        ^          remove the blks
 *              │        │                │
 *              │        │                v
 *              │  XFS_DAS_RMTBLK <─n── done?
 *              │  re-enter with          │
 *              │  one less blk to        y
 *              │      remove             │
 *              │                         V
 *              │                  refill the state
 *              n                         │
 *              │                         v
 *              │                   XFS_DAS_RM_NAME
 *              │                         │
 *              ├─────────────────────────┘
 *              │
 *              v
 *       remove leaf and
 *       update hash with
 *   xfs_attr_node_remove_cleanup
 *              │
 *              v
 *           need to
 *        shrink tree? ─n─┐
 *              │         │
 *              y         │
 *              │         │
 *              v         │
 *          join leaf     │
 *              │         │
 *              v         │
 *      XFS_DAS_RM_SHRINK │
 *              │         │
 *              v         │
 *       do the shrink    │
 *              │         │
 *              v         │
 *          free state <──┘
 *              │
 *              v
 *            done
 *
 */

/*
 * Enum values for xfs_delattr_context.da_state
 *
 * These values are used by delayed attribute operations to keep track  of where
 * they were before they returned -EAGAIN.  A return code of -EAGAIN signals the
 * calling function to roll the transaction, and then call the subroutine to
 * finish the operation.  The enum is then used by the subroutine to jump back
 * to where it was and resume executing where it left off.
 */
enum xfs_delattr_state {
	XFS_DAS_UNINIT		= 0,  /* No state has been set yet */
	XFS_DAS_RMTBLK,		      /* Removing remote blks */
	XFS_DAS_RM_NAME,	      /* Remove attr name */
	XFS_DAS_RM_SHRINK,	      /* We are shrinking the tree */
};

/*
 * Defines for xfs_delattr_context.flags
 */
#define XFS_DAC_DEFER_FINISH		0x01 /* finish the transaction */

/*
 * Context used for keeping track of delayed attribute operations
 */
struct xfs_delattr_context {
	struct xfs_da_args      *da_args;

	/* Used in xfs_attr_node_removename to roll through removing blocks */
	struct xfs_da_state     *da_state;

	/* Used to keep track of current state of delayed operation */
	unsigned int            flags;
	enum xfs_delattr_state  dela_state;
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
bool xfs_attr_is_leaf(struct xfs_inode *ip);
int xfs_attr_get_ilocked(struct xfs_da_args *args);
int xfs_attr_get(struct xfs_da_args *args);
int xfs_attr_set(struct xfs_da_args *args);
int xfs_attr_set_args(struct xfs_da_args *args);
int xfs_has_attr(struct xfs_da_args *args);
int xfs_attr_remove_args(struct xfs_da_args *args);
int xfs_attr_remove_iter(struct xfs_delattr_context *dac);
int xfs_attr_trans_roll(struct xfs_delattr_context *dac);
bool xfs_attr_namecheck(const void *name, size_t length);
void xfs_delattr_context_init(struct xfs_delattr_context *dac,
			      struct xfs_da_args *args);

#endif	/* __XFS_ATTR_H__ */
