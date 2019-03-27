/*
 * netgraph.h
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: netgraph.h,v 1.29 1999/11/01 07:56:13 julian Exp $
 */

#ifndef _NETGRAPH_NETGRAPH_H_
#define _NETGRAPH_NETGRAPH_H_

#ifndef _KERNEL
#error "This file should not be included in user level programs"
#endif

#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/refcount.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_netgraph.h"
#include "opt_kdb.h"
#endif

/* debugging options */
#define NG_SEPARATE_MALLOC	/* make modules use their own malloc types */

/*
 * This defines the in-kernel binary interface version.
 * It is possible to change this but leave the external message
 * API the same. Each type also has it's own cookies for versioning as well.
 * Change it for NETGRAPH_DEBUG version so we cannot mix debug and non debug
 * modules.
 */
#define _NG_ABI_VERSION 12
#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
#define NG_ABI_VERSION	(_NG_ABI_VERSION + 0x10000)
#else	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/
#define NG_ABI_VERSION	_NG_ABI_VERSION
#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/


/*
 * Forward references for the basic structures so we can
 * define the typedefs and use them in the structures themselves.
 */
struct ng_hook ;
struct ng_node ;
struct ng_item ;
typedef	struct ng_item *item_p;
typedef struct ng_node *node_p;
typedef struct ng_hook *hook_p;

/* node method definitions */
typedef	int	ng_constructor_t(node_p node);
typedef	int	ng_close_t(node_p node);
typedef	int	ng_shutdown_t(node_p node);
typedef	int	ng_newhook_t(node_p node, hook_p hook, const char *name);
typedef	hook_p	ng_findhook_t(node_p node, const char *name);
typedef	int	ng_connect_t(hook_p hook);
typedef	int	ng_rcvmsg_t(node_p node, item_p item, hook_p lasthook);
typedef	int	ng_rcvdata_t(hook_p hook, item_p item);
typedef	int	ng_disconnect_t(hook_p hook);
typedef	int	ng_rcvitem (node_p node, hook_p hook, item_p item);

/***********************************************************************
 ***************** Hook Structure and Methods **************************
 ***********************************************************************
 *
 * Structure of a hook
 */
struct ng_hook {
	char	hk_name[NG_HOOKSIZ];	/* what this node knows this link as */
	void   *hk_private;		/* node dependent ID for this hook */
	int	hk_flags;		/* info about this hook/link */
	int	hk_type;		/* tbd: hook data link type */
	struct	ng_hook *hk_peer;	/* the other end of this link */
	struct	ng_node *hk_node;	/* The node this hook is attached to */
	LIST_ENTRY(ng_hook) hk_hooks;	/* linked list of all hooks on node */
	ng_rcvmsg_t	*hk_rcvmsg;	/* control messages come here */
	ng_rcvdata_t	*hk_rcvdata;	/* data comes here */
	int	hk_refs;		/* dont actually free this till 0 */
#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
#define HK_MAGIC 0x78573011
	int	hk_magic;
	char	*lastfile;
	int	lastline;
	SLIST_ENTRY(ng_hook)	  hk_all;		/* all existing items */
#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/
};
/* Flags for a hook */
#define HK_INVALID		0x0001	/* don't trust it! */
#define HK_QUEUE		0x0002	/* queue for later delivery */
#define HK_FORCE_WRITER		0x0004	/* Incoming data queued as a writer */
#define HK_DEAD			0x0008	/* This is the dead hook.. don't free */
#define HK_HI_STACK		0x0010	/* Hook has hi stack usage */
#define HK_TO_INBOUND		0x0020	/* Hook on ntw. stack inbound path. */

/*
 * Public Methods for hook
 * If you can't do it with these you probably shouldn;t be doing it.
 */
void ng_unref_hook(hook_p hook); /* don't move this */
#define	_NG_HOOK_REF(hook)	refcount_acquire(&(hook)->hk_refs)
#define _NG_HOOK_NAME(hook)	((hook)->hk_name)
#define _NG_HOOK_UNREF(hook)	ng_unref_hook(hook)
#define	_NG_HOOK_SET_PRIVATE(hook, val)	do {(hook)->hk_private = val;} while (0)
#define	_NG_HOOK_SET_RCVMSG(hook, val)	do {(hook)->hk_rcvmsg = val;} while (0)
#define	_NG_HOOK_SET_RCVDATA(hook, val)	do {(hook)->hk_rcvdata = val;} while (0)
#define	_NG_HOOK_PRIVATE(hook)	((hook)->hk_private)
#define _NG_HOOK_NOT_VALID(hook)	((hook)->hk_flags & HK_INVALID)
#define _NG_HOOK_IS_VALID(hook)	(!((hook)->hk_flags & HK_INVALID))
#define _NG_HOOK_NODE(hook)	((hook)->hk_node) /* only rvalue! */
#define _NG_HOOK_PEER(hook)	((hook)->hk_peer) /* only rvalue! */
#define _NG_HOOK_FORCE_WRITER(hook)				\
		do { hook->hk_flags |= HK_FORCE_WRITER; } while (0)
#define _NG_HOOK_FORCE_QUEUE(hook) do { hook->hk_flags |= HK_QUEUE; } while (0)
#define _NG_HOOK_SET_TO_INBOUND(hook)				\
		do { hook->hk_flags |= HK_TO_INBOUND; } while (0)
#define _NG_HOOK_HI_STACK(hook) do { hook->hk_flags |= HK_HI_STACK; } while (0)

/* Some shortcuts */
#define NG_PEER_NODE(hook)	NG_HOOK_NODE(NG_HOOK_PEER(hook))
#define NG_PEER_HOOK_NAME(hook)	NG_HOOK_NAME(NG_HOOK_PEER(hook))
#define NG_PEER_NODE_NAME(hook)	NG_NODE_NAME(NG_PEER_NODE(hook))

#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
#define _NN_ __FILE__,__LINE__
void	dumphook (hook_p hook, char *file, int line);
static __inline void	_chkhook(hook_p hook, char *file, int line);
static __inline void	_ng_hook_ref(hook_p hook, char * file, int line);
static __inline char *	_ng_hook_name(hook_p hook, char * file, int line);
static __inline void	_ng_hook_unref(hook_p hook, char * file, int line);
static __inline void	_ng_hook_set_private(hook_p hook,
				void * val, char * file, int line);
static __inline void	_ng_hook_set_rcvmsg(hook_p hook,
				ng_rcvmsg_t *val, char * file, int line);
static __inline void	_ng_hook_set_rcvdata(hook_p hook,
				ng_rcvdata_t *val, char * file, int line);
static __inline void *	_ng_hook_private(hook_p hook, char * file, int line);
static __inline int	_ng_hook_not_valid(hook_p hook, char * file, int line);
static __inline int	_ng_hook_is_valid(hook_p hook, char * file, int line);
static __inline node_p	_ng_hook_node(hook_p hook, char * file, int line);
static __inline hook_p	_ng_hook_peer(hook_p hook, char * file, int line);
static __inline void	_ng_hook_force_writer(hook_p hook, char * file,
				int line);
static __inline void	_ng_hook_force_queue(hook_p hook, char * file,
				int line);
static __inline void	_ng_hook_set_to_inbound(hook_p hook, char * file,
				int line);

static __inline void
_chkhook(hook_p hook, char *file, int line)
{
	if (hook->hk_magic != HK_MAGIC) {
		printf("Accessing freed ");
		dumphook(hook, file, line);
	}
	hook->lastline = line;
	hook->lastfile = file;
}

static __inline void
_ng_hook_ref(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_REF(hook);
}

static __inline char *
_ng_hook_name(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_NAME(hook));
}

static __inline void
_ng_hook_unref(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_UNREF(hook);
}

static __inline void
_ng_hook_set_private(hook_p hook, void *val, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_SET_PRIVATE(hook, val);
}

static __inline void
_ng_hook_set_rcvmsg(hook_p hook, ng_rcvmsg_t *val, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_SET_RCVMSG(hook, val);
}

static __inline void
_ng_hook_set_rcvdata(hook_p hook, ng_rcvdata_t *val, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_SET_RCVDATA(hook, val);
}

static __inline void *
_ng_hook_private(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_PRIVATE(hook));
}

static __inline int
_ng_hook_not_valid(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_NOT_VALID(hook));
}

static __inline int
_ng_hook_is_valid(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_IS_VALID(hook));
}

static __inline node_p
_ng_hook_node(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_NODE(hook));
}

static __inline hook_p
_ng_hook_peer(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	return (_NG_HOOK_PEER(hook));
}

static __inline void
_ng_hook_force_writer(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_FORCE_WRITER(hook);
}

static __inline void
_ng_hook_force_queue(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_FORCE_QUEUE(hook);
}

static __inline void
_ng_hook_set_to_inbound(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_SET_TO_INBOUND(hook);
}

static __inline void
_ng_hook_hi_stack(hook_p hook, char * file, int line)
{
	_chkhook(hook, file, line);
	_NG_HOOK_HI_STACK(hook);
}


#define	NG_HOOK_REF(hook)		_ng_hook_ref(hook, _NN_)
#define NG_HOOK_NAME(hook)		_ng_hook_name(hook, _NN_)
#define NG_HOOK_UNREF(hook)		_ng_hook_unref(hook, _NN_)
#define	NG_HOOK_SET_PRIVATE(hook, val)	_ng_hook_set_private(hook, val, _NN_)
#define	NG_HOOK_SET_RCVMSG(hook, val)	_ng_hook_set_rcvmsg(hook, val, _NN_)
#define	NG_HOOK_SET_RCVDATA(hook, val)	_ng_hook_set_rcvdata(hook, val, _NN_)
#define	NG_HOOK_PRIVATE(hook)		_ng_hook_private(hook, _NN_)
#define NG_HOOK_NOT_VALID(hook)		_ng_hook_not_valid(hook, _NN_)
#define NG_HOOK_IS_VALID(hook)		_ng_hook_is_valid(hook, _NN_)
#define NG_HOOK_NODE(hook)		_ng_hook_node(hook, _NN_)
#define NG_HOOK_PEER(hook)		_ng_hook_peer(hook, _NN_)
#define NG_HOOK_FORCE_WRITER(hook)	_ng_hook_force_writer(hook, _NN_)
#define NG_HOOK_FORCE_QUEUE(hook)	_ng_hook_force_queue(hook, _NN_)
#define NG_HOOK_SET_TO_INBOUND(hook)	_ng_hook_set_to_inbound(hook, _NN_)
#define NG_HOOK_HI_STACK(hook)		_ng_hook_hi_stack(hook, _NN_)

#else	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

#define	NG_HOOK_REF(hook)		_NG_HOOK_REF(hook)
#define NG_HOOK_NAME(hook)		_NG_HOOK_NAME(hook)
#define NG_HOOK_UNREF(hook)		_NG_HOOK_UNREF(hook)
#define	NG_HOOK_SET_PRIVATE(hook, val)	_NG_HOOK_SET_PRIVATE(hook, val)
#define	NG_HOOK_SET_RCVMSG(hook, val)	_NG_HOOK_SET_RCVMSG(hook, val)
#define	NG_HOOK_SET_RCVDATA(hook, val)	_NG_HOOK_SET_RCVDATA(hook, val)
#define	NG_HOOK_PRIVATE(hook)		_NG_HOOK_PRIVATE(hook)
#define NG_HOOK_NOT_VALID(hook)		_NG_HOOK_NOT_VALID(hook)
#define NG_HOOK_IS_VALID(hook)		_NG_HOOK_IS_VALID(hook)
#define NG_HOOK_NODE(hook)		_NG_HOOK_NODE(hook)
#define NG_HOOK_PEER(hook)		_NG_HOOK_PEER(hook)
#define NG_HOOK_FORCE_WRITER(hook)	_NG_HOOK_FORCE_WRITER(hook)
#define NG_HOOK_FORCE_QUEUE(hook)	_NG_HOOK_FORCE_QUEUE(hook)
#define NG_HOOK_SET_TO_INBOUND(hook)	_NG_HOOK_SET_TO_INBOUND(hook)
#define NG_HOOK_HI_STACK(hook)		_NG_HOOK_HI_STACK(hook)

#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

/***********************************************************************
 ***************** Node Structure and Methods **************************
 ***********************************************************************
 * Structure of a node
 * including the eembedded queue structure.
 *
 * The structure for queueing Netgraph request items
 * embedded in the node structure
 */
struct ng_queue {
	u_int		q_flags;	/* Current r/w/q lock flags */
	u_int		q_flags2;	/* Other queue flags */
	struct mtx	q_mtx;
	STAILQ_ENTRY(ng_node)	q_work;	/* nodes with work to do */
	STAILQ_HEAD(, ng_item)	queue;	/* actually items queue */
};

struct ng_node {
	char	nd_name[NG_NODESIZ];	/* optional globally unique name */
	struct	ng_type *nd_type;	/* the installed 'type' */
	int	nd_flags;		/* see below for bit definitions */
	int	nd_numhooks;		/* number of hooks */
	void   *nd_private;		/* node type dependent node ID */
	ng_ID_t	nd_ID;			/* Unique per node */
	LIST_HEAD(hooks, ng_hook) nd_hooks;	/* linked list of node hooks */
	LIST_ENTRY(ng_node)	  nd_nodes;	/* name hash collision list */
	LIST_ENTRY(ng_node)	  nd_idnodes;	/* ID hash collision list */
	struct	ng_queue	  nd_input_queue; /* input queue for locking */
	int	nd_refs;		/* # of references to this node */
	struct	vnet		 *nd_vnet;	/* network stack instance */
#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
#define ND_MAGIC 0x59264837
	int	nd_magic;
	char	*lastfile;
	int	lastline;
	SLIST_ENTRY(ng_node)	  nd_all;	/* all existing nodes */
#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/
};

/* Flags for a node */
#define NGF_INVALID	0x00000001	/* free when refs go to 0 */
#define NG_INVALID	NGF_INVALID	/* compat for old code */
#define NGF_FORCE_WRITER	0x00000004	/* Never multithread this node */
#define NG_FORCE_WRITER	NGF_FORCE_WRITER /* compat for old code */
#define NGF_CLOSING	0x00000008	/* ng_rmnode() at work */
#define NG_CLOSING	NGF_CLOSING	/* compat for old code */
#define NGF_REALLY_DIE	0x00000010	/* "persistent" node is unloading */
#define NG_REALLY_DIE	NGF_REALLY_DIE	/* compat for old code */
#define NGF_HI_STACK	0x00000020	/* node has hi stack usage */
#define NGF_TYPE1	0x10000000	/* reserved for type specific storage */
#define NGF_TYPE2	0x20000000	/* reserved for type specific storage */
#define NGF_TYPE3	0x40000000	/* reserved for type specific storage */
#define NGF_TYPE4	0x80000000	/* reserved for type specific storage */

/*
 * Public methods for nodes.
 * If you can't do it with these you probably shouldn't be doing it.
 */
void	ng_unref_node(node_p node); /* don't move this */
#define _NG_NODE_NAME(node)	((node)->nd_name + 0)
#define _NG_NODE_HAS_NAME(node)	((node)->nd_name[0] + 0)
#define _NG_NODE_ID(node)	((node)->nd_ID + 0)
#define	_NG_NODE_REF(node)	refcount_acquire(&(node)->nd_refs)
#define	_NG_NODE_UNREF(node)	ng_unref_node(node)
#define	_NG_NODE_SET_PRIVATE(node, val)	do {(node)->nd_private = val;} while (0)
#define	_NG_NODE_PRIVATE(node)	((node)->nd_private)
#define _NG_NODE_IS_VALID(node)	(!((node)->nd_flags & NGF_INVALID))
#define _NG_NODE_NOT_VALID(node)	((node)->nd_flags & NGF_INVALID)
#define _NG_NODE_NUMHOOKS(node)	((node)->nd_numhooks + 0) /* rvalue */
#define _NG_NODE_FORCE_WRITER(node)					\
	do{ node->nd_flags |= NGF_FORCE_WRITER; }while (0)
#define _NG_NODE_HI_STACK(node)						\
	do{ node->nd_flags |= NGF_HI_STACK; }while (0)
#define _NG_NODE_REALLY_DIE(node)					\
	do{ node->nd_flags |= (NGF_REALLY_DIE|NGF_INVALID); }while (0)
#define _NG_NODE_REVIVE(node) \
	do { node->nd_flags &= ~NGF_INVALID; } while (0)
/*
 * The hook iterator.
 * This macro will call a function of type ng_fn_eachhook for each
 * hook attached to the node. If the function returns 0, then the
 * iterator will stop and return a pointer to the hook that returned 0.
 */
typedef	int	ng_fn_eachhook(hook_p hook, void* arg);
#define _NG_NODE_FOREACH_HOOK(node, fn, arg, rethook)			\
	do {								\
		hook_p _hook;						\
		(rethook) = NULL;					\
		LIST_FOREACH(_hook, &((node)->nd_hooks), hk_hooks) {	\
			if ((fn)(_hook, arg) == 0) {			\
				(rethook) = _hook;			\
				break;					\
			}						\
		}							\
	} while (0)

#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
void	dumpnode(node_p node, char *file, int line);
static __inline void _chknode(node_p node, char *file, int line);
static __inline char * _ng_node_name(node_p node, char *file, int line);
static __inline int _ng_node_has_name(node_p node, char *file, int line);
static __inline ng_ID_t _ng_node_id(node_p node, char *file, int line);
static __inline void _ng_node_ref(node_p node, char *file, int line);
static __inline void _ng_node_unref(node_p node, char *file, int line);
static __inline void _ng_node_set_private(node_p node, void * val,
							char *file, int line);
static __inline void * _ng_node_private(node_p node, char *file, int line);
static __inline int _ng_node_is_valid(node_p node, char *file, int line);
static __inline int _ng_node_not_valid(node_p node, char *file, int line);
static __inline int _ng_node_numhooks(node_p node, char *file, int line);
static __inline void _ng_node_force_writer(node_p node, char *file, int line);
static __inline hook_p _ng_node_foreach_hook(node_p node,
			ng_fn_eachhook *fn, void *arg, char *file, int line);
static __inline void _ng_node_revive(node_p node, char *file, int line);

static __inline void
_chknode(node_p node, char *file, int line)
{
	if (node->nd_magic != ND_MAGIC) {
		printf("Accessing freed ");
		dumpnode(node, file, line);
	}
	node->lastline = line;
	node->lastfile = file;
}

static __inline char *
_ng_node_name(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_NAME(node));
}

static __inline int
_ng_node_has_name(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_HAS_NAME(node));
}

static __inline ng_ID_t
_ng_node_id(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_ID(node));
}

static __inline void
_ng_node_ref(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_REF(node);
}

static __inline void
_ng_node_unref(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_UNREF(node);
}

static __inline void
_ng_node_set_private(node_p node, void * val, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_SET_PRIVATE(node, val);
}

static __inline void *
_ng_node_private(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return (_NG_NODE_PRIVATE(node));
}

static __inline int
_ng_node_is_valid(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_IS_VALID(node));
}

static __inline int
_ng_node_not_valid(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_NOT_VALID(node));
}

static __inline int
_ng_node_numhooks(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	return(_NG_NODE_NUMHOOKS(node));
}

static __inline void
_ng_node_force_writer(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_FORCE_WRITER(node);
}

static __inline void
_ng_node_hi_stack(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_HI_STACK(node);
}

static __inline void
_ng_node_really_die(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_REALLY_DIE(node);
}

static __inline void
_ng_node_revive(node_p node, char *file, int line)
{
	_chknode(node, file, line);
	_NG_NODE_REVIVE(node);
}

static __inline hook_p
_ng_node_foreach_hook(node_p node, ng_fn_eachhook *fn, void *arg,
						char *file, int line)
{
	hook_p hook;
	_chknode(node, file, line);
	_NG_NODE_FOREACH_HOOK(node, fn, arg, hook);
	return (hook);
}

#define NG_NODE_NAME(node)		_ng_node_name(node, _NN_)	
#define NG_NODE_HAS_NAME(node)		_ng_node_has_name(node, _NN_)	
#define NG_NODE_ID(node)		_ng_node_id(node, _NN_)
#define NG_NODE_REF(node)		_ng_node_ref(node, _NN_)
#define	NG_NODE_UNREF(node)		_ng_node_unref(node, _NN_)
#define	NG_NODE_SET_PRIVATE(node, val)	_ng_node_set_private(node, val, _NN_)
#define	NG_NODE_PRIVATE(node)		_ng_node_private(node, _NN_)
#define NG_NODE_IS_VALID(node)		_ng_node_is_valid(node, _NN_)
#define NG_NODE_NOT_VALID(node)		_ng_node_not_valid(node, _NN_)
#define NG_NODE_FORCE_WRITER(node) 	_ng_node_force_writer(node, _NN_)
#define NG_NODE_HI_STACK(node) 		_ng_node_hi_stack(node, _NN_)
#define NG_NODE_REALLY_DIE(node) 	_ng_node_really_die(node, _NN_)
#define NG_NODE_NUMHOOKS(node)		_ng_node_numhooks(node, _NN_)
#define NG_NODE_REVIVE(node)		_ng_node_revive(node, _NN_)
#define NG_NODE_FOREACH_HOOK(node, fn, arg, rethook)			      \
	do {								      \
		rethook = _ng_node_foreach_hook(node, fn, (void *)arg, _NN_); \
	} while (0)

#else	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

#define NG_NODE_NAME(node)		_NG_NODE_NAME(node)	
#define NG_NODE_HAS_NAME(node)		_NG_NODE_HAS_NAME(node)	
#define NG_NODE_ID(node)		_NG_NODE_ID(node)	
#define	NG_NODE_REF(node)		_NG_NODE_REF(node)	
#define	NG_NODE_UNREF(node)		_NG_NODE_UNREF(node)	
#define	NG_NODE_SET_PRIVATE(node, val)	_NG_NODE_SET_PRIVATE(node, val)	
#define	NG_NODE_PRIVATE(node)		_NG_NODE_PRIVATE(node)	
#define NG_NODE_IS_VALID(node)		_NG_NODE_IS_VALID(node)	
#define NG_NODE_NOT_VALID(node)		_NG_NODE_NOT_VALID(node)	
#define NG_NODE_FORCE_WRITER(node) 	_NG_NODE_FORCE_WRITER(node)
#define NG_NODE_HI_STACK(node) 		_NG_NODE_HI_STACK(node)
#define NG_NODE_REALLY_DIE(node) 	_NG_NODE_REALLY_DIE(node)
#define NG_NODE_NUMHOOKS(node)		_NG_NODE_NUMHOOKS(node)	
#define NG_NODE_REVIVE(node)		_NG_NODE_REVIVE(node)
#define NG_NODE_FOREACH_HOOK(node, fn, arg, rethook)			\
		_NG_NODE_FOREACH_HOOK(node, fn, arg, rethook)
#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

/***********************************************************************
 ************* Node Queue and Item Structures and Methods **************
 ***********************************************************************
 *
 */
typedef	void	ng_item_fn(node_p node, hook_p hook, void *arg1, int arg2);
typedef	int	ng_item_fn2(node_p node, struct ng_item *item, hook_p hook);
typedef	void	ng_apply_t(void *context, int error);
struct ng_apply_info {
	ng_apply_t	*apply;
	void		*context;
	int		refs;
	int		error;
};
struct ng_item {
	u_long	el_flags;
	STAILQ_ENTRY(ng_item)	el_next;
	node_p	el_dest; /* The node it will be applied against (or NULL) */
	hook_p	el_hook; /* Entering hook. Optional in Control messages */
	union {
		struct mbuf	*da_m;
		struct {
			struct ng_mesg	*msg_msg;
			ng_ID_t		msg_retaddr;
		} msg;
		struct {
			union {
				ng_item_fn	*fn_fn;
				ng_item_fn2	*fn_fn2;
			} fn_fn;
			void 		*fn_arg1;
			int		fn_arg2;
		} fn;
	} body;
	/*
	 * Optional callback called when item is being applied,
	 * and its context.
	 */
	struct ng_apply_info	*apply;
	u_int	depth;
#ifdef	NETGRAPH_DEBUG /*----------------------------------------------*/
	char *lastfile;
	int  lastline;
	TAILQ_ENTRY(ng_item)	  all;		/* all existing items */
#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/
};

#define NGQF_TYPE	0x03		/* MASK of content definition */
#define NGQF_MESG	0x00		/* the queue element is a message */
#define NGQF_DATA	0x01		/* the queue element is data */
#define NGQF_FN		0x02		/* the queue element is a function */
#define NGQF_FN2	0x03		/* the queue element is a new function */

#define NGQF_RW		0x04		/* MASK for wanted queue mode */
#define NGQF_READER	0x04		/* wants to be a reader */
#define NGQF_WRITER	0x00		/* wants to be a writer */

#define NGQF_QMODE	0x08		/* MASK for how it was queued */
#define NGQF_QREADER	0x08		/* was queued as a reader */
#define NGQF_QWRITER	0x00		/* was queued as a writer */

/*
 * Get the mbuf (etc) out of an item.
 * Sets the value in the item to NULL in case we need to call NG_FREE_ITEM()
 * with it, (to avoid freeing the things twice).
 * If you don't want to zero out the item then realise that the
 * item still owns it.
 * Retaddr is different. There are no references on that. It's just a number.
 * The debug versions must be either all used everywhere or not at all.
 */

#define _NGI_M(i) ((i)->body.da_m)
#define _NGI_MSG(i) ((i)->body.msg.msg_msg)
#define _NGI_RETADDR(i) ((i)->body.msg.msg_retaddr)
#define	_NGI_FN(i) ((i)->body.fn.fn_fn.fn_fn)
#define	_NGI_FN2(i) ((i)->body.fn.fn_fn.fn_fn2)
#define	_NGI_ARG1(i) ((i)->body.fn.fn_arg1)
#define	_NGI_ARG2(i) ((i)->body.fn.fn_arg2)
#define	_NGI_NODE(i) ((i)->el_dest)
#define	_NGI_HOOK(i) ((i)->el_hook)
#define	_NGI_SET_HOOK(i,h) do { _NGI_HOOK(i) = h; h = NULL;} while (0)
#define	_NGI_CLR_HOOK(i)   do {						\
		hook_p _hook = _NGI_HOOK(i);				\
		if (_hook) {						\
			_NG_HOOK_UNREF(_hook);				\
			_NGI_HOOK(i) = NULL;				\
		}							\
	} while (0)
#define	_NGI_SET_NODE(i,n) do { _NGI_NODE(i) = n; n = NULL;} while (0)
#define	_NGI_CLR_NODE(i)   do {						\
		node_p _node = _NGI_NODE(i);				\
		if (_node) {						\
			_NG_NODE_UNREF(_node);				\
			_NGI_NODE(i) = NULL;				\
		}							\
	} while (0)

#ifdef NETGRAPH_DEBUG /*----------------------------------------------*/
void				dumpitem(item_p item, char *file, int line);
static __inline void		_ngi_check(item_p item, char *file, int line) ;
static __inline struct mbuf **	_ngi_m(item_p item, char *file, int line) ;
static __inline ng_ID_t *	_ngi_retaddr(item_p item, char *file, int line);
static __inline struct ng_mesg ** _ngi_msg(item_p item, char *file, int line) ;
static __inline ng_item_fn **	_ngi_fn(item_p item, char *file, int line) ;
static __inline ng_item_fn2 **	_ngi_fn2(item_p item, char *file, int line) ;
static __inline void **		_ngi_arg1(item_p item, char *file, int line) ;
static __inline int *		_ngi_arg2(item_p item, char *file, int line) ;
static __inline node_p		_ngi_node(item_p item, char *file, int line);
static __inline hook_p		_ngi_hook(item_p item, char *file, int line);

static __inline void
_ngi_check(item_p item, char *file, int line)
{
	(item)->lastline = line;
	(item)->lastfile = file;
}

static __inline struct mbuf **
_ngi_m(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_M(item));
}

static __inline struct ng_mesg **
_ngi_msg(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_MSG(item));
}

static __inline ng_ID_t *
_ngi_retaddr(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_RETADDR(item));
}

static __inline ng_item_fn **
_ngi_fn(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_FN(item));
}

static __inline ng_item_fn2 **
_ngi_fn2(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_FN2(item));
}

static __inline void **
_ngi_arg1(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_ARG1(item));
}

static __inline int *
_ngi_arg2(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (&_NGI_ARG2(item));
}

static __inline node_p
_ngi_node(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (_NGI_NODE(item));
}

static __inline hook_p
_ngi_hook(item_p item, char *file, int line)
{
	_ngi_check(item, file, line);
	return (_NGI_HOOK(item));
}

#define NGI_M(i)	(*_ngi_m(i, _NN_))
#define NGI_MSG(i)	(*_ngi_msg(i, _NN_))
#define NGI_RETADDR(i)	(*_ngi_retaddr(i, _NN_))
#define NGI_FN(i)	(*_ngi_fn(i, _NN_))
#define NGI_FN2(i)	(*_ngi_fn2(i, _NN_))
#define NGI_ARG1(i)	(*_ngi_arg1(i, _NN_))
#define NGI_ARG2(i)	(*_ngi_arg2(i, _NN_))
#define NGI_HOOK(i)	_ngi_hook(i, _NN_)
#define NGI_NODE(i)	_ngi_node(i, _NN_)
#define	NGI_SET_HOOK(i,h)						\
	do { _ngi_check(i, _NN_); _NGI_SET_HOOK(i, h); } while (0)
#define	NGI_CLR_HOOK(i)							\
	do { _ngi_check(i, _NN_); _NGI_CLR_HOOK(i); } while (0)
#define	NGI_SET_NODE(i,n)						\
	do { _ngi_check(i, _NN_); _NGI_SET_NODE(i, n); } while (0)
#define	NGI_CLR_NODE(i)							\
	do { _ngi_check(i, _NN_); _NGI_CLR_NODE(i); } while (0)

#define NG_FREE_ITEM(item)						\
	do {								\
		_ngi_check(item, _NN_);					\
		ng_free_item((item));					\
	} while (0)

#define	SAVE_LINE(item)							\
	do {								\
		(item)->lastline = __LINE__;				\
		(item)->lastfile = __FILE__;				\
	} while (0)

#else	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

#define NGI_M(i)	_NGI_M(i)
#define NGI_MSG(i)	_NGI_MSG(i)
#define NGI_RETADDR(i)	_NGI_RETADDR(i)
#define NGI_FN(i)	_NGI_FN(i)
#define NGI_FN2(i)	_NGI_FN2(i)
#define NGI_ARG1(i)	_NGI_ARG1(i)
#define NGI_ARG2(i)	_NGI_ARG2(i)
#define	NGI_NODE(i)	_NGI_NODE(i)
#define	NGI_HOOK(i)	_NGI_HOOK(i)
#define	NGI_SET_HOOK(i,h) _NGI_SET_HOOK(i,h)
#define	NGI_CLR_HOOK(i)	  _NGI_CLR_HOOK(i)
#define	NGI_SET_NODE(i,n) _NGI_SET_NODE(i,n)
#define	NGI_CLR_NODE(i)	  _NGI_CLR_NODE(i)

#define	NG_FREE_ITEM(item)	ng_free_item((item))
#define	SAVE_LINE(item)		do {} while (0)

#endif	/* NETGRAPH_DEBUG */ /*----------------------------------------------*/

#define NGI_GET_M(i,m)							\
	do {								\
		(m) = NGI_M(i);						\
		_NGI_M(i) = NULL;					\
	} while (0)

#define NGI_GET_MSG(i,m)						\
	do {								\
		(m) = NGI_MSG(i);					\
		_NGI_MSG(i) = NULL;					\
	} while (0)

#define NGI_GET_NODE(i,n)	/* YOU NOW HAVE THE REFERENCE */	\
	do {								\
		(n) = NGI_NODE(i);					\
		_NGI_NODE(i) = NULL;					\
	} while (0)

#define NGI_GET_HOOK(i,h)						\
	do {								\
		(h) = NGI_HOOK(i);					\
		_NGI_HOOK(i) = NULL;					\
	} while (0)

#define NGI_SET_WRITER(i)	((i)->el_flags &= ~NGQF_QMODE)
#define NGI_SET_READER(i)	((i)->el_flags |= NGQF_QREADER)

#define NGI_QUEUED_READER(i)	((i)->el_flags & NGQF_QREADER)
#define NGI_QUEUED_WRITER(i)	(((i)->el_flags & NGQF_QMODE) == NGQF_QWRITER)
	
/**********************************************************************
* Data macros.  Send, manipulate and free.
**********************************************************************/
/*
 * Assuming the data is already ok, just set the new address and send
 */
#define NG_FWD_ITEM_HOOK_FLAGS(error, item, hook, flags)		\
	do {								\
		(error) =						\
		    ng_address_hook(NULL, (item), (hook), NG_NOFLAGS);	\
		if (error == 0) {					\
			SAVE_LINE(item);				\
			(error) = ng_snd_item((item), (flags));		\
		}							\
		(item) = NULL;						\
	} while (0)
#define	NG_FWD_ITEM_HOOK(error, item, hook)	\
		NG_FWD_ITEM_HOOK_FLAGS(error, item, hook, NG_NOFLAGS)

/*
 * Forward a data packet. Mbuf pointer is updated to new value. We
 * presume you dealt with the old one when you update it to the new one
 * (or it maybe the old one). We got a packet and possibly had to modify
 * the mbuf. You should probably use NGI_GET_M() if you are going to use
 * this too.
 */
#define NG_FWD_NEW_DATA_FLAGS(error, item, hook, m, flags)		\
	do {								\
		NGI_M(item) = (m);					\
		(m) = NULL;						\
		NG_FWD_ITEM_HOOK_FLAGS(error, item, hook, flags);	\
	} while (0)
#define	NG_FWD_NEW_DATA(error, item, hook, m)	\
		NG_FWD_NEW_DATA_FLAGS(error, item, hook, m, NG_NOFLAGS)

/* Send a previously unpackaged mbuf. XXX: This should be called
 * NG_SEND_DATA in future, but this name is kept for compatibility
 * reasons.
 */
#define NG_SEND_DATA_FLAGS(error, hook, m, flags)			\
	do {								\
		item_p _item;						\
		if ((_item = ng_package_data((m), flags))) {		\
			NG_FWD_ITEM_HOOK_FLAGS(error, _item, hook, flags);\
		} else {						\
			(error) = ENOMEM;				\
		}							\
		(m) = NULL;						\
	} while (0)

#define NG_SEND_DATA_ONLY(error, hook, m)	\
		NG_SEND_DATA_FLAGS(error, hook, m, NG_NOFLAGS)
/* NG_SEND_DATA() compat for meta-data times */
#define	NG_SEND_DATA(error, hook, m, x)	\
		NG_SEND_DATA_FLAGS(error, hook, m, NG_NOFLAGS)

#define NG_FREE_MSG(msg)						\
	do {								\
		if ((msg)) {						\
			free((msg), M_NETGRAPH_MSG);			\
			(msg) = NULL;					\
		}	 						\
	} while (0)

#define NG_FREE_M(m)							\
	do {								\
		if ((m)) {						\
			m_freem((m));					\
			(m) = NULL;					\
		}							\
	} while (0)

/*****************************************
* Message macros
*****************************************/

#define NG_SEND_MSG_HOOK(error, here, msg, hook, retaddr)		\
	do {								\
		item_p _item;						\
		if ((_item = ng_package_msg(msg, NG_NOFLAGS)) == NULL) {\
			(msg) = NULL;					\
			(error) = ENOMEM;				\
			break;						\
		}							\
		if (((error) = ng_address_hook((here), (_item),		\
					(hook), (retaddr))) == 0) {	\
			SAVE_LINE(_item);				\
			(error) = ng_snd_item((_item), 0);		\
		}							\
		(msg) = NULL;						\
	} while (0)

#define NG_SEND_MSG_PATH(error, here, msg, path, retaddr)		\
	do {								\
		item_p _item;						\
		if ((_item = ng_package_msg(msg, NG_NOFLAGS)) == NULL) {\
			(msg) = NULL;					\
			(error) = ENOMEM;				\
			break;						\
		}							\
		if (((error) = ng_address_path((here), (_item),		\
					(path), (retaddr))) == 0) {	\
			SAVE_LINE(_item);				\
			(error) = ng_snd_item((_item), 0);		\
		}							\
		(msg) = NULL;						\
	} while (0)

#define NG_SEND_MSG_ID(error, here, msg, ID, retaddr)			\
	do {								\
		item_p _item;						\
		if ((_item = ng_package_msg(msg, NG_NOFLAGS)) == NULL) {\
			(msg) = NULL;					\
			(error) = ENOMEM;				\
			break;						\
		}							\
		if (((error) = ng_address_ID((here), (_item),		\
					(ID), (retaddr))) == 0) {	\
			SAVE_LINE(_item);				\
			(error) = ng_snd_item((_item), 0);		\
		}							\
		(msg) = NULL;						\
	} while (0)

/*
 * Redirect the message to the next hop using the given hook.
 * ng_retarget_msg() frees the item if there is an error
 * and returns an error code.  It returns 0 on success.
 */
#define NG_FWD_MSG_HOOK(error, here, item, hook, retaddr)		\
	do {								\
		if (((error) = ng_address_hook((here), (item),		\
					(hook), (retaddr))) == 0) {	\
			SAVE_LINE(item);				\
			(error) = ng_snd_item((item), 0);		\
		}							\
		(item) = NULL;						\
	} while (0)

/*
 * Send a queue item back to it's originator with a response message.
 * Assume original message was removed and freed separatly.
 */
#define NG_RESPOND_MSG(error, here, item, resp)				\
	do {								\
		if (resp) {						\
			ng_ID_t _dest = NGI_RETADDR(item);		\
			NGI_RETADDR(item) = 0;				\
			NGI_MSG(item) = resp;				\
			if ((error = ng_address_ID((here), (item),	\
					_dest, 0)) == 0) {		\
				SAVE_LINE(item);			\
				(error) = ng_snd_item((item), NG_QUEUE);\
			}						\
		} else							\
			NG_FREE_ITEM(item);				\
		(item) = NULL;						\
	} while (0)


/***********************************************************************
 ******** Structures Definitions and Macros for defining a node  *******
 ***********************************************************************
 *
 * Here we define the structures needed to actually define a new node
 * type.
 */

/*
 * Command list -- each node type specifies the command that it knows
 * how to convert between ASCII and binary using an array of these.
 * The last element in the array must be a terminator with cookie=0.
 */

struct ng_cmdlist {
	u_int32_t			cookie;		/* command typecookie */
	int				cmd;		/* command number */
	const char			*name;		/* command name */
	const struct ng_parse_type	*mesgType;	/* args if !NGF_RESP */
	const struct ng_parse_type	*respType;	/* args if NGF_RESP */
};

/*
 * Structure of a node type
 * If data is sent to the "rcvdata()" entrypoint then the system
 * may decide to defer it until later by queing it with the normal netgraph
 * input queuing system.  This is decidde by the HK_QUEUE flag being set in
 * the flags word of the peer (receiving) hook. The dequeuing mechanism will
 * ensure it is not requeued again.
 * Note the input queueing system is to allow modules
 * to 'release the stack' or to pass data across spl layers.
 * The data will be redelivered as soon as the NETISR code runs
 * which may be almost immediately.  A node may also do it's own queueing
 * for other reasons (e.g. device output queuing).
 */
struct ng_type {

	u_int32_t	version; 	/* must equal NG_API_VERSION */
	const char	*name;		/* Unique type name */
	modeventhand_t	mod_event;	/* Module event handler (optional) */
	ng_constructor_t *constructor;	/* Node constructor */
	ng_rcvmsg_t	*rcvmsg;	/* control messages come here */
	ng_close_t	*close;		/* warn about forthcoming shutdown */
	ng_shutdown_t	*shutdown;	/* reset, and free resources */
	ng_newhook_t	*newhook;	/* first notification of new hook */
	ng_findhook_t	*findhook;	/* only if you have lots of hooks */
	ng_connect_t	*connect;	/* final notification of new hook */
	ng_rcvdata_t	*rcvdata;	/* data comes here */
	ng_disconnect_t	*disconnect;	/* notify on disconnect */

	const struct	ng_cmdlist *cmdlist;	/* commands we can convert */

	/* R/W data private to the base netgraph code DON'T TOUCH! */
	LIST_ENTRY(ng_type) types;		/* linked list of all types */
	int		    refs;		/* number of instances */
};

/*
 * Use the NETGRAPH_INIT() macro to link a node type into the
 * netgraph system. This works for types compiled into the kernel
 * as well as KLD modules. The first argument should be the type
 * name (eg, echo) and the second a pointer to the type struct.
 *
 * If a different link time is desired, e.g., a device driver that
 * needs to install its netgraph type before probing, use the
 * NETGRAPH_INIT_ORDERED() macro instead.  Device drivers probably
 * want to use SI_SUB_DRIVERS/SI_ORDER_FIRST.
 */

#define NETGRAPH_INIT_ORDERED(typename, typestructp, sub, order)	\
static moduledata_t ng_##typename##_mod = {				\
	"ng_" #typename,						\
	ng_mod_event,							\
	(typestructp)							\
};									\
DECLARE_MODULE(ng_##typename, ng_##typename##_mod, sub, order);		\
MODULE_DEPEND(ng_##typename, netgraph,	NG_ABI_VERSION,			\
					NG_ABI_VERSION,			\
					NG_ABI_VERSION)

#define NETGRAPH_INIT(tn, tp)						\
	NETGRAPH_INIT_ORDERED(tn, tp, SI_SUB_PSEUDO, SI_ORDER_MIDDLE)

/* Special malloc() type for netgraph structs and ctrl messages */
/* Only these two types should be visible to nodes */
MALLOC_DECLARE(M_NETGRAPH);
MALLOC_DECLARE(M_NETGRAPH_MSG);

/* declare the base of the netgraph sysclt hierarchy */
/* but only if this file cares about sysctls */
#ifdef	SYSCTL_DECL
SYSCTL_DECL(_net_graph);
#endif

/*
 * Methods that the nodes can use.
 * Many of these methods should usually NOT be used directly but via
 * Macros above.
 */
int	ng_address_ID(node_p here, item_p item, ng_ID_t ID, ng_ID_t retaddr);
int	ng_address_hook(node_p here, item_p item, hook_p hook, ng_ID_t retaddr);
int	ng_address_path(node_p here, item_p item, const char *address, ng_ID_t raddr);
int	ng_bypass(hook_p hook1, hook_p hook2);
hook_p	ng_findhook(node_p node, const char *name);
struct	ng_type *ng_findtype(const char *type);
int	ng_make_node_common(struct ng_type *typep, node_p *nodep);
int	ng_name_node(node_p node, const char *name);
node_p	ng_name2noderef(node_p node, const char *name);
int	ng_newtype(struct ng_type *tp);
ng_ID_t ng_node2ID(node_p node);
item_p	ng_package_data(struct mbuf *m, int flags);
item_p	ng_package_msg(struct ng_mesg *msg, int flags);
item_p	ng_package_msg_self(node_p here, hook_p hook, struct ng_mesg *msg);
void	ng_replace_retaddr(node_p here, item_p item, ng_ID_t retaddr);
int	ng_rmhook_self(hook_p hook);	/* if a node wants to kill a hook */
int	ng_rmnode_self(node_p here);	/* if a node wants to suicide */
int	ng_rmtype(struct ng_type *tp);
int	ng_snd_item(item_p item, int queue);
int 	ng_send_fn(node_p node, hook_p hook, ng_item_fn *fn, void *arg1,
	int arg2);
int 	ng_send_fn1(node_p node, hook_p hook, ng_item_fn *fn, void *arg1,
	int arg2, int flags);
int 	ng_send_fn2(node_p node, hook_p hook, item_p pitem, ng_item_fn2 *fn,
	void *arg1, int arg2, int flags);
int	ng_uncallout(struct callout *c, node_p node);
int	ng_callout(struct callout *c, node_p node, hook_p hook, int ticks,
	    ng_item_fn *fn, void * arg1, int arg2);
#define	ng_callout_init(c)	callout_init(c, 1)

/* Flags for netgraph functions. */
#define	NG_NOFLAGS	0x00000000	/* no special options */
#define	NG_QUEUE	0x00000001	/* enqueue item, don't dispatch */
#define	NG_WAITOK	0x00000002	/* use M_WAITOK, etc. */
/* XXXGL: NG_PROGRESS unused since ng_base.c rev. 1.136. Should be deleted? */
#define	NG_PROGRESS	0x00000004	/* return EINPROGRESS if queued */
#define	NG_REUSE_ITEM	0x00000008	/* supplied item should be reused */

/*
 * prototypes the user should DEFINITELY not use directly
 */
void	ng_free_item(item_p item); /* Use NG_FREE_ITEM instead */
int	ng_mod_event(module_t mod, int what, void *arg);

/*
 * Tag definitions and constants
 */

#define	NG_TAG_PRIO	1

struct ng_tag_prio {
	struct m_tag	tag;
	char	priority;
	char	discardability;
};

#define	NG_PRIO_CUTOFF		32
#define	NG_PRIO_LINKSTATE	64

/* Macros and declarations to keep compatibility with metadata, which
 * is obsoleted now. To be deleted.
 */
typedef void *meta_p;
#define _NGI_META(i)	NULL
#define NGI_META(i)	NULL
#define NG_FREE_META(meta)
#define NGI_GET_META(i,m)
#define	ng_copy_meta(meta) NULL

/*
 * Mark the current thread when called from the outbound path of the
 * network stack, in order to enforce queuing on ng nodes calling into
 * the inbound network stack path.
 */
#define NG_OUTBOUND_THREAD_REF()					\
	curthread->td_ng_outbound++
#define NG_OUTBOUND_THREAD_UNREF()					\
	do {								\
		curthread->td_ng_outbound--;				\
		KASSERT(curthread->td_ng_outbound >= 0,			\
		    ("%s: negative td_ng_outbound", __func__));		\
	} while (0)

#endif /* _NETGRAPH_NETGRAPH_H_ */
