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
 *
 * Below is a state machine diagram for attr set operations.
 *
 * It seems the challenge with understanding this system comes from trying to
 * absorb the state machine all at once, when really one should only be looking
 * at it with in the context of a single function. Once a state sensitive
 * function is called, the idea is that it "takes ownership" of the
 * state machine. It isn't concerned with the states that may have belonged to
 * it's calling parent. Only the states relevant to itself or any other
 * subroutines there in. Once a calling function hands off the state machine to
 * a subroutine, it needs to respect the simple rule that it doesn't "own" the
 * state machine anymore, and it's the responsibility of that calling function
 * to propagate the -EAGAIN back up the call stack. Upon reentry, it is
 * committed to re-calling that subroutine until it returns something other than
 * -EAGAIN. Once that subroutine signals completion (by returning anything other
 * than -EAGAIN), the calling function can resume using the state machine.
 *
 *  xfs_attr_set_iter()
 *              │
 *              v
 *   ┌─y─ has an attr fork?
 *   │          |
 *   │          n
 *   │          |
 *   │          V
 *   │       add a fork
 *   │          │
 *   └──────────┤
 *              │
 *              V
 *   ┌─── is shortform?
 *   │          │
 *   │          y
 *   │          │
 *   │          V
 *   │   xfs_attr_set_fmt
 *   │          |
 *   │          V
 *   │ xfs_attr_try_sf_addname
 *   │          │
 *   │          V
 *   │      had enough ──y──> done
 *   │        space?
 *   n          │
 *   │          n
 *   │          │
 *   │          V
 *   │   transform to leaf
 *   │          │
 *   │          V
 *   │   hold the leaf buffer
 *   │          │
 *   │          V
 *   │     return -EAGAIN
 *   │      Re-enter in
 *   │       leaf form
 *   │
 *   └─> release leaf buffer
 *          if needed
 *              │
 *              V
 *   ┌───n── fork has
 *   │      only 1 blk?
 *   │          │
 *   │          y
 *   │          │
 *   │          v
 *   │ xfs_attr_leaf_try_add()
 *   │          │
 *   │          v
 *   │      had enough ──────────────y─────────────┐
 *   │        space?                               │
 *   │          │                                  │
 *   │          n                                  │
 *   │          │                                  │
 *   │          v                                  │
 *   │    return -EAGAIN                           │
 *   │      re-enter in                            │
 *   │        node form                            │
 *   │          │                                  │
 *   └──────────┤                                  │
 *              │                                  │
 *              V                                  │
 * xfs_attr_node_addname_find_attr                 │
 *        determines if this                       │
 *       is create or rename                       │
 *     find space to store attr                    │
 *              │                                  │
 *              v                                  │
 *     xfs_attr_node_addname                       │
 *              │                                  │
 *              v                                  │
 *   fits in a node leaf? ────n─────┐              │
 *              │     ^             v              │
 *              │     │       single leaf node?    │
 *              │     │         │            │     │
 *              y     │         y            n     │
 *              │     │         │            │     │
 *              v     │         v            v     │
 *            update  │    grow the leaf  split if │
 *           hashvals └── return -EAGAIN   needed  │
 *              │         retry leaf add     │     │
 *              │           on reentry       │     │
 *              ├────────────────────────────┘     │
 *              │                                  │
 *              v                                  │
 *         need to alloc                           │
 *   ┌─y── or flip flag?                           │
 *   │          │                                  │
 *   │          n                                  │
 *   │          │                                  │
 *   │          v                                  │
 *   │         done                                │
 *   │                                             │
 *   │                                             │
 *   │         XFS_DAS_FOUND_LBLK <────────────────┘
 *   │                  │
 *   │                  V
 *   │        xfs_attr_leaf_addname()
 *   │                  │
 *   │                  v
 *   │      ┌──first time through?
 *   │      │          │
 *   │      │          y
 *   │      │          │
 *   │      n          v
 *   │      │    if we have rmt blks
 *   │      │    find space for them
 *   │      │          │
 *   │      └──────────┤
 *   │                 │
 *   │                 v
 *   │            still have
 *   │      ┌─n─ blks to alloc? <──┐
 *   │      │          │           │
 *   │      │          y           │
 *   │      │          │           │
 *   │      │          v           │
 *   │      │     alloc one blk    │
 *   │      │     return -EAGAIN ──┘
 *   │      │    re-enter with one
 *   │      │    less blk to alloc
 *   │      │
 *   │      │
 *   │      └───> set the rmt
 *   │               value
 *   │                 │
 *   │                 v
 *   │               was this
 *   │              a rename? ──n─┐
 *   │                 │          │
 *   │                 y          │
 *   │                 │          │
 *   │                 v          │
 *   │           flip incomplete  │
 *   │               flag         │
 *   │                 │          │
 *   │                 v          │
 *   │         XFS_DAS_FLIP_LFLAG │
 *   │                 │          │
 *   │                 v          │
 *   │          need to remove    │
 *   │              old bks? ──n──┤
 *   │                 │          │
 *   │                 y          │
 *   │                 │          │
 *   │                 V          │
 *   │               remove       │
 *   │        ┌───> old blks      │
 *   │        │        │          │
 *   │ XFS_DAS_RM_LBLK │          │
 *   │        ^        │          │
 *   │        │        v          │
 *   │        └──y── more to      │
 *   │              remove?       │
 *   │                 │          │
 *   │                 n          │
 *   │                 │          │
 *   │                 v          │
 *   │          XFS_DAS_RD_LEAF   │
 *   │                 │          │
 *   │                 v          │
 *   │            remove leaf     │
 *   │                 │          │
 *   │                 v          │
 *   │            shrink to sf    │
 *   │             if needed      │
 *   │                 │          │
 *   │                 v          │
 *   │                done <──────┘
 *   │
 *   └──────> XFS_DAS_FOUND_NBLK
 *                     │
 *                     v
 *       ┌─────n──  need to
 *       │        alloc blks?
 *       │             │
 *       │             y
 *       │             │
 *       │             v
 *       │        find space
 *       │             │
 *       │             v
 *       │  ┌─>XFS_DAS_ALLOC_NODE
 *       │  │          │
 *       │  │          v
 *       │  │      alloc blk
 *       │  │          │
 *       │  │          v
 *       │  └──y── need to alloc
 *       │         more blocks?
 *       │             │
 *       │             n
 *       │             │
 *       │             v
 *       │      set the rmt value
 *       │             │
 *       │             v
 *       │          was this
 *       └────────> a rename? ──n─┐
 *                     │          │
 *                     y          │
 *                     │          │
 *                     v          │
 *               flip incomplete  │
 *                   flag         │
 *                     │          │
 *                     v          │
 *             XFS_DAS_FLIP_NFLAG │
 *                     │          │
 *                     v          │
 *                 need to        │
 *               remove blks? ─n──┤
 *                     │          │
 *                     y          │
 *                     │          │
 *                     v          │
 *                   remove       │
 *        ┌────────> old blks     │
 *        │            │          │
 *  XFS_DAS_RM_NBLK    │          │
 *        ^            │          │
 *        │            v          │
 *        └──────y── more to      │
 *                   remove       │
 *                     │          │
 *                     n          │
 *                     │          │
 *                     v          │
 *              XFS_DAS_CLR_FLAG  │
 *                     │          │
 *                     v          │
 *                clear flags     │
 *                     │          │
 *                     ├──────────┘
 *                     │
 *                     v
 *                   done
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
	XFS_DAS_FOUND_LBLK,	      /* We found leaf blk for attr */
	XFS_DAS_FOUND_NBLK,	      /* We found node blk for attr */
	XFS_DAS_FLIP_LFLAG,	      /* Flipped leaf INCOMPLETE attr flag */
	XFS_DAS_RM_LBLK,	      /* A rename is removing leaf blocks */
	XFS_DAS_RD_LEAF,	      /* Read in the new leaf */
	XFS_DAS_ALLOC_NODE,	      /* We are allocating node blocks */
	XFS_DAS_FLIP_NFLAG,	      /* Flipped node INCOMPLETE attr flag */
	XFS_DAS_RM_NBLK,	      /* A rename is removing node blocks */
	XFS_DAS_CLR_FLAG,	      /* Clear incomplete flag */
};

/*
 * Defines for xfs_delattr_context.flags
 */
#define XFS_DAC_DEFER_FINISH		0x01 /* finish the transaction */
#define XFS_DAC_LEAF_ADDNAME_INIT	0x02 /* xfs_attr_leaf_addname init*/

/*
 * Context used for keeping track of delayed attribute operations
 */
struct xfs_delattr_context {
	struct xfs_da_args      *da_args;

	/* Used in xfs_attr_rmtval_set_blk to roll through allocating blocks */
	struct xfs_bmbt_irec	map;
	xfs_dablk_t		lblkno;
	int			blkcnt;

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
bool xfs_attr_namecheck(const void *name, size_t length);
void xfs_delattr_context_init(struct xfs_delattr_context *dac,
			      struct xfs_da_args *args);

#endif	/* __XFS_ATTR_H__ */
