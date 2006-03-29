/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BEHAVIOR_H__
#define __XFS_BEHAVIOR_H__

/*
 * Header file used to associate behaviors with virtualized objects.
 *
 * A virtualized object is an internal, virtualized representation of
 * OS entities such as persistent files, processes, or sockets.  Examples
 * of virtualized objects include vnodes, vprocs, and vsockets.  Often
 * a virtualized object is referred to simply as an "object."
 *
 * A behavior is essentially an implementation layer associated with
 * an object.  Multiple behaviors for an object are chained together,
 * the order of chaining determining the order of invocation.  Each
 * behavior of a given object implements the same set of interfaces
 * (e.g., the VOP interfaces).
 *
 * Behaviors may be dynamically inserted into an object's behavior chain,
 * such that the addition is transparent to consumers that already have
 * references to the object.  Typically, a given behavior will be inserted
 * at a particular location in the behavior chain.  Insertion of new
 * behaviors is synchronized with operations-in-progress (oip's) so that
 * the oip's always see a consistent view of the chain.
 *
 * The term "interposition" is used to refer to the act of inserting
 * a behavior such that it interposes on (i.e., is inserted in front
 * of) a particular other behavior.  A key example of this is when a
 * system implementing distributed single system image wishes to
 * interpose a distribution layer (providing distributed coherency)
 * in front of an object that is otherwise only accessed locally.
 *
 * Note that the traditional vnode/inode combination is simply a virtualized
 * object that has exactly one associated behavior.
 *
 * Behavior synchronization is logic which is necessary under certain
 * circumstances that there is no conflict between ongoing operations
 * traversing the behavior chain and those dynamically modifying the
 * behavior chain.  Because behavior synchronization adds extra overhead
 * to virtual operation invocation, we want to restrict, as much as
 * we can, the requirement for this extra code, to those situations
 * in which it is truly necessary.
 *
 * Behavior synchronization is needed whenever there's at least one class
 * of object in the system for which:
 * 1) multiple behaviors for a given object are supported,
 * -- AND --
 * 2a) insertion of a new behavior can happen dynamically at any time during
 *     the life of an active object,
 *	-- AND --
 *	3a) insertion of a new behavior needs to synchronize with existing
 *	    ops-in-progress.
 *	-- OR --
 *	3b) multiple different behaviors can be dynamically inserted at
 *	    any time during the life of an active object
 *	-- OR --
 *	3c) removal of a behavior can occur at any time during the life of
 *	    an active object.
 * -- OR --
 * 2b) removal of a behavior can occur at any time during the life of an
 *     active object
 *
 */

struct bhv_head_lock;

/*
 * Behavior head.  Head of the chain of behaviors.
 * Contained within each virtualized object data structure.
 */
typedef struct bhv_head {
	struct bhv_desc *bh_first;	/* first behavior in chain */
	struct bhv_head_lock *bh_lockp;	/* pointer to lock info struct */
} bhv_head_t;

/*
 * Behavior descriptor.	 Descriptor associated with each behavior.
 * Contained within the behavior's private data structure.
 */
typedef struct bhv_desc {
	void		*bd_pdata;	/* private data for this behavior */
	void		*bd_vobj;	/* virtual object associated with */
	void		*bd_ops;	/* ops for this behavior */
	struct bhv_desc *bd_next;	/* next behavior in chain */
} bhv_desc_t;

/*
 * Behavior identity field.  A behavior's identity determines the position
 * where it lives within a behavior chain, and it's always the first field
 * of the behavior's ops vector. The optional id field further identifies the
 * subsystem responsible for the behavior.
 */
typedef struct bhv_identity {
	__u16	bi_id;		/* owning subsystem id */
	__u16	bi_position;	/* position in chain */
} bhv_identity_t;

typedef bhv_identity_t bhv_position_t;

#define BHV_IDENTITY_INIT(id,pos)	{id, pos}
#define BHV_IDENTITY_INIT_POSITION(pos) BHV_IDENTITY_INIT(0, pos)

/*
 * Define boundaries of position values.
 */
#define BHV_POSITION_INVALID	0	/* invalid position number */
#define BHV_POSITION_BASE	1	/* base (last) implementation layer */
#define BHV_POSITION_TOP	63	/* top (first) implementation layer */

/*
 * Plumbing macros.
 */
#define BHV_HEAD_FIRST(bhp)	(ASSERT((bhp)->bh_first), (bhp)->bh_first)
#define BHV_NEXT(bdp)		(ASSERT((bdp)->bd_next), (bdp)->bd_next)
#define BHV_NEXTNULL(bdp)	((bdp)->bd_next)
#define BHV_VOBJ(bdp)		(ASSERT((bdp)->bd_vobj), (bdp)->bd_vobj)
#define BHV_VOBJNULL(bdp)	((bdp)->bd_vobj)
#define BHV_PDATA(bdp)		(bdp)->bd_pdata
#define BHV_OPS(bdp)		(bdp)->bd_ops
#define BHV_IDENTITY(bdp)	((bhv_identity_t *)(bdp)->bd_ops)
#define BHV_POSITION(bdp)	(BHV_IDENTITY(bdp)->bi_position)

extern void bhv_head_init(bhv_head_t *, char *);
extern void bhv_head_destroy(bhv_head_t *);
extern int  bhv_insert(bhv_head_t *, bhv_desc_t *);
extern void bhv_insert_initial(bhv_head_t *, bhv_desc_t *);

/*
 * Initialize a new behavior descriptor.
 * Arguments:
 *   bdp - pointer to behavior descriptor
 *   pdata - pointer to behavior's private data
 *   vobj - pointer to associated virtual object
 *   ops - pointer to ops for this behavior
 */
#define bhv_desc_init(bdp, pdata, vobj, ops)		\
 {							\
	(bdp)->bd_pdata = pdata;			\
	(bdp)->bd_vobj = vobj;				\
	(bdp)->bd_ops = ops;				\
	(bdp)->bd_next = NULL;				\
 }

/*
 * Remove a behavior descriptor from a behavior chain.
 */
#define bhv_remove(bhp, bdp)				\
 {							\
	if ((bhp)->bh_first == (bdp)) {			\
		/*					\
		* Remove from front of chain.		\
		* Atomic wrt oip's.			\
		*/					\
	       (bhp)->bh_first = (bdp)->bd_next;	\
	} else {					\
	       /* remove from non-front of chain */	\
	       bhv_remove_not_first(bhp, bdp);		\
	}						\
	(bdp)->bd_vobj = NULL;				\
 }

/*
 * Behavior module prototypes.
 */
extern void		bhv_remove_not_first(bhv_head_t *bhp, bhv_desc_t *bdp);
extern bhv_desc_t *	bhv_lookup(bhv_head_t *bhp, void *ops);
extern bhv_desc_t *	bhv_lookup_range(bhv_head_t *bhp, int low, int high);
extern bhv_desc_t *	bhv_base(bhv_head_t *bhp);

/* No bhv locking on Linux */
#define bhv_lookup_unlocked	bhv_lookup
#define bhv_base_unlocked	bhv_base

#endif /* __XFS_BEHAVIOR_H__ */
