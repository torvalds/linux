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
typedef void (*put_listent_func_t)(struct xfs_attr_list_context *context,
		int flags, unsigned char *name, int namelen, void *value,
		int valuelen);

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
 * Enum values for xfs_attr_intent.xattri_da_state
 *
 * These values are used by delayed attribute operations to keep track  of where
 * they were before they returned -EAGAIN.  A return code of -EAGAIN signals the
 * calling function to roll the transaction, and then call the subroutine to
 * finish the operation.  The enum is then used by the subroutine to jump back
 * to where it was and resume executing where it left off.
 */
enum xfs_delattr_state {
	XFS_DAS_UNINIT		= 0,	/* No state has been set yet */

	/*
	 * Initial sequence states. The replace setup code relies on the
	 * ADD and REMOVE states for a specific format to be sequential so
	 * that we can transform the initial operation to be performed
	 * according to the xfs_has_larp() state easily.
	 */
	XFS_DAS_SF_ADD,			/* Initial sf add state */
	XFS_DAS_SF_REMOVE,		/* Initial sf replace/remove state */

	XFS_DAS_LEAF_ADD,		/* Initial leaf add state */
	XFS_DAS_LEAF_REMOVE,		/* Initial leaf replace/remove state */

	XFS_DAS_NODE_ADD,		/* Initial node add state */
	XFS_DAS_NODE_REMOVE,		/* Initial node replace/remove state */

	/* Leaf state set/replace/remove sequence */
	XFS_DAS_LEAF_SET_RMT,		/* set a remote xattr from a leaf */
	XFS_DAS_LEAF_ALLOC_RMT,		/* We are allocating remote blocks */
	XFS_DAS_LEAF_REPLACE,		/* Perform replace ops on a leaf */
	XFS_DAS_LEAF_REMOVE_OLD,	/* Start removing old attr from leaf */
	XFS_DAS_LEAF_REMOVE_RMT,	/* A rename is removing remote blocks */
	XFS_DAS_LEAF_REMOVE_ATTR,	/* Remove the old attr from a leaf */

	/* Node state sequence, must match leaf state above */
	XFS_DAS_NODE_SET_RMT,		/* set a remote xattr from a node */
	XFS_DAS_NODE_ALLOC_RMT,		/* We are allocating remote blocks */
	XFS_DAS_NODE_REPLACE,		/* Perform replace ops on a node */
	XFS_DAS_NODE_REMOVE_OLD,	/* Start removing old attr from node */
	XFS_DAS_NODE_REMOVE_RMT,	/* A rename is removing remote blocks */
	XFS_DAS_NODE_REMOVE_ATTR,	/* Remove the old attr from a node */

	XFS_DAS_DONE,			/* finished operation */
};

#define XFS_DAS_STRINGS	\
	{ XFS_DAS_UNINIT,		"XFS_DAS_UNINIT" }, \
	{ XFS_DAS_SF_ADD,		"XFS_DAS_SF_ADD" }, \
	{ XFS_DAS_SF_REMOVE,		"XFS_DAS_SF_REMOVE" }, \
	{ XFS_DAS_LEAF_ADD,		"XFS_DAS_LEAF_ADD" }, \
	{ XFS_DAS_LEAF_REMOVE,		"XFS_DAS_LEAF_REMOVE" }, \
	{ XFS_DAS_NODE_ADD,		"XFS_DAS_NODE_ADD" }, \
	{ XFS_DAS_NODE_REMOVE,		"XFS_DAS_NODE_REMOVE" }, \
	{ XFS_DAS_LEAF_SET_RMT,		"XFS_DAS_LEAF_SET_RMT" }, \
	{ XFS_DAS_LEAF_ALLOC_RMT,	"XFS_DAS_LEAF_ALLOC_RMT" }, \
	{ XFS_DAS_LEAF_REPLACE,		"XFS_DAS_LEAF_REPLACE" }, \
	{ XFS_DAS_LEAF_REMOVE_OLD,	"XFS_DAS_LEAF_REMOVE_OLD" }, \
	{ XFS_DAS_LEAF_REMOVE_RMT,	"XFS_DAS_LEAF_REMOVE_RMT" }, \
	{ XFS_DAS_LEAF_REMOVE_ATTR,	"XFS_DAS_LEAF_REMOVE_ATTR" }, \
	{ XFS_DAS_NODE_SET_RMT,		"XFS_DAS_NODE_SET_RMT" }, \
	{ XFS_DAS_NODE_ALLOC_RMT,	"XFS_DAS_NODE_ALLOC_RMT" },  \
	{ XFS_DAS_NODE_REPLACE,		"XFS_DAS_NODE_REPLACE" },  \
	{ XFS_DAS_NODE_REMOVE_OLD,	"XFS_DAS_NODE_REMOVE_OLD" }, \
	{ XFS_DAS_NODE_REMOVE_RMT,	"XFS_DAS_NODE_REMOVE_RMT" }, \
	{ XFS_DAS_NODE_REMOVE_ATTR,	"XFS_DAS_NODE_REMOVE_ATTR" }, \
	{ XFS_DAS_DONE,			"XFS_DAS_DONE" }

struct xfs_attri_log_nameval;

/*
 * Context used for keeping track of delayed attribute operations
 */
struct xfs_attr_intent {
	/*
	 * used to log this item to an intent containing a list of attrs to
	 * commit later
	 */
	struct list_head		xattri_list;

	/* Used in xfs_attr_node_removename to roll through removing blocks */
	struct xfs_da_state		*xattri_da_state;

	struct xfs_da_args		*xattri_da_args;

	/*
	 * Shared buffer containing the attr name, new name, and value so that
	 * the logging code can share large memory buffers between log items.
	 */
	struct xfs_attri_log_nameval	*xattri_nameval;

	/* Used to keep track of current state of delayed operation */
	enum xfs_delattr_state		xattri_dela_state;

	/*
	 * Attr operation being performed - XFS_ATTRI_OP_FLAGS_*
	 */
	unsigned int			xattri_op_flags;

	/* Used in xfs_attr_rmtval_set_blk to roll through allocating blocks */
	xfs_dablk_t			xattri_lblkno;
	int				xattri_blkcnt;
	struct xfs_bmbt_irec		xattri_map;
};

static inline unsigned int
xfs_attr_intent_op(const struct xfs_attr_intent *attr)
{
	return attr->xattri_op_flags & XFS_ATTRI_OP_FLAGS_TYPE_MASK;
}

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

enum xfs_attr_update {
	XFS_ATTRUPDATE_REMOVE,	/* remove attr */
	XFS_ATTRUPDATE_UPSERT,	/* set value, replace any existing attr */
	XFS_ATTRUPDATE_CREATE,	/* set value, fail if attr already exists */
	XFS_ATTRUPDATE_REPLACE,	/* set value, fail if attr does not exist */
};

int xfs_attr_set(struct xfs_da_args *args, enum xfs_attr_update op, bool rsvd);
int xfs_attr_set_iter(struct xfs_attr_intent *attr);
int xfs_attr_remove_iter(struct xfs_attr_intent *attr);
bool xfs_attr_check_namespace(unsigned int attr_flags);
bool xfs_attr_namecheck(unsigned int attr_flags, const void *name,
		size_t length);
int xfs_attr_calc_size(struct xfs_da_args *args, int *local);
struct xfs_trans_res xfs_attr_set_resv(const struct xfs_da_args *args);

/*
 * Check to see if the attr should be upgraded from non-existent or shortform to
 * single-leaf-block attribute list.
 */
static inline bool
xfs_attr_is_shortform(
	struct xfs_inode    *ip)
{
	return ip->i_af.if_format == XFS_DINODE_FMT_LOCAL ||
	       (ip->i_af.if_format == XFS_DINODE_FMT_EXTENTS &&
		ip->i_af.if_nextents == 0);
}

static inline enum xfs_delattr_state
xfs_attr_init_add_state(struct xfs_da_args *args)
{
	/*
	 * When called from the completion of a attr remove to determine the
	 * next state, the attribute fork may be null. This can occur only occur
	 * on a pure remove, but we grab the next state before we check if a
	 * replace operation is being performed. If we are called from any other
	 * context, i_af is guaranteed to exist. Hence if the attr fork is
	 * null, we were called from a pure remove operation and so we are done.
	 */
	if (!xfs_inode_has_attr_fork(args->dp))
		return XFS_DAS_DONE;

	args->op_flags |= XFS_DA_OP_ADDNAME;
	if (xfs_attr_is_shortform(args->dp))
		return XFS_DAS_SF_ADD;
	if (xfs_attr_is_leaf(args->dp))
		return XFS_DAS_LEAF_ADD;
	return XFS_DAS_NODE_ADD;
}

static inline enum xfs_delattr_state
xfs_attr_init_remove_state(struct xfs_da_args *args)
{
	if (xfs_attr_is_shortform(args->dp))
		return XFS_DAS_SF_REMOVE;
	if (xfs_attr_is_leaf(args->dp))
		return XFS_DAS_LEAF_REMOVE;
	return XFS_DAS_NODE_REMOVE;
}

/*
 * If we are logging the attributes, then we have to start with removal of the
 * old attribute so that there is always consistent state that we can recover
 * from if the system goes down part way through. We always log the new attr
 * value, so even when we remove the attr first we still have the information in
 * the log to finish the replace operation atomically.
 */
static inline enum xfs_delattr_state
xfs_attr_init_replace_state(struct xfs_da_args *args)
{
	args->op_flags |= XFS_DA_OP_ADDNAME | XFS_DA_OP_REPLACE;
	if (args->op_flags & XFS_DA_OP_LOGGED)
		return xfs_attr_init_remove_state(args);
	return xfs_attr_init_add_state(args);
}

xfs_dahash_t xfs_attr_hashname(const uint8_t *name, int namelen);

xfs_dahash_t xfs_attr_hashval(struct xfs_mount *mp, unsigned int attr_flags,
		const uint8_t *name, int namelen, const void *value,
		int valuelen);

/* Set the hash value for any extended attribute from any namespace. */
static inline void xfs_attr_sethash(struct xfs_da_args *args)
{
	args->hashval = xfs_attr_hashval(args->dp->i_mount, args->attr_filter,
					 args->name, args->namelen,
					 args->value, args->valuelen);
}

extern struct kmem_cache *xfs_attr_intent_cache;
int __init xfs_attr_intent_init_cache(void);
void xfs_attr_intent_destroy_cache(void);

int xfs_attr_sf_totsize(struct xfs_inode *dp);
int xfs_attr_add_fork(struct xfs_inode *ip, int size, int rsvd);

#endif	/* __XFS_ATTR_H__ */
