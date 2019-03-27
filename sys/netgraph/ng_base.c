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
 * Authors: Julian Elischer <julian@freebsd.org>
 *          Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_base.c,v 1.39 1999/01/28 23:54:53 julian Exp $
 */

/*
 * This file implements the base netgraph code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ctype.h>
#include <sys/hash.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/rwlock.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/unistd.h>
#include <machine/cpu.h>
#include <vm/uma.h>

#include <net/netisr.h>
#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>

MODULE_VERSION(netgraph, NG_ABI_VERSION);

/* Mutex to protect topology events. */
static struct rwlock	ng_topo_lock;
#define	TOPOLOGY_RLOCK()	rw_rlock(&ng_topo_lock)
#define	TOPOLOGY_RUNLOCK()	rw_runlock(&ng_topo_lock)
#define	TOPOLOGY_WLOCK()	rw_wlock(&ng_topo_lock)
#define	TOPOLOGY_WUNLOCK()	rw_wunlock(&ng_topo_lock)
#define	TOPOLOGY_NOTOWNED()	rw_assert(&ng_topo_lock, RA_UNLOCKED)

#ifdef	NETGRAPH_DEBUG
static struct mtx	ng_nodelist_mtx; /* protects global node/hook lists */
static struct mtx	ngq_mtx;	/* protects the queue item list */

static SLIST_HEAD(, ng_node) ng_allnodes;
static LIST_HEAD(, ng_node) ng_freenodes; /* in debug, we never free() them */
static SLIST_HEAD(, ng_hook) ng_allhooks;
static LIST_HEAD(, ng_hook) ng_freehooks; /* in debug, we never free() them */

static void ng_dumpitems(void);
static void ng_dumpnodes(void);
static void ng_dumphooks(void);

#endif	/* NETGRAPH_DEBUG */
/*
 * DEAD versions of the structures.
 * In order to avoid races, it is sometimes necessary to point
 * at SOMETHING even though theoretically, the current entity is
 * INVALID. Use these to avoid these races.
 */
struct ng_type ng_deadtype = {
	NG_ABI_VERSION,
	"dead",
	NULL,	/* modevent */
	NULL,	/* constructor */
	NULL,	/* rcvmsg */
	NULL,	/* shutdown */
	NULL,	/* newhook */
	NULL,	/* findhook */
	NULL,	/* connect */
	NULL,	/* rcvdata */
	NULL,	/* disconnect */
	NULL, 	/* cmdlist */
};

struct ng_node ng_deadnode = {
	"dead",
	&ng_deadtype,	
	NGF_INVALID,
	0,	/* numhooks */
	NULL,	/* private */
	0,	/* ID */
	LIST_HEAD_INITIALIZER(ng_deadnode.nd_hooks),
	{},	/* all_nodes list entry */
	{},	/* id hashtable list entry */
	{	0,
		0,
		{}, /* should never use! (should hang) */
		{}, /* workqueue entry */
		STAILQ_HEAD_INITIALIZER(ng_deadnode.nd_input_queue.queue),
	},
	1,	/* refs */
	NULL,	/* vnet */
#ifdef	NETGRAPH_DEBUG
	ND_MAGIC,
	__FILE__,
	__LINE__,
	{NULL}
#endif	/* NETGRAPH_DEBUG */
};

struct ng_hook ng_deadhook = {
	"dead",
	NULL,		/* private */
	HK_INVALID | HK_DEAD,
	0,		/* undefined data link type */
	&ng_deadhook,	/* Peer is self */
	&ng_deadnode,	/* attached to deadnode */
	{},		/* hooks list */
	NULL,		/* override rcvmsg() */
	NULL,		/* override rcvdata() */
	1,		/* refs always >= 1 */
#ifdef	NETGRAPH_DEBUG
	HK_MAGIC,
	__FILE__,
	__LINE__,
	{NULL}
#endif	/* NETGRAPH_DEBUG */
};

/*
 * END DEAD STRUCTURES
 */
/* List nodes with unallocated work */
static STAILQ_HEAD(, ng_node) ng_worklist = STAILQ_HEAD_INITIALIZER(ng_worklist);
static struct mtx	ng_worklist_mtx;   /* MUST LOCK NODE FIRST */

/* List of installed types */
static LIST_HEAD(, ng_type) ng_typelist;
static struct rwlock	ng_typelist_lock;
#define	TYPELIST_RLOCK()	rw_rlock(&ng_typelist_lock)
#define	TYPELIST_RUNLOCK()	rw_runlock(&ng_typelist_lock)
#define	TYPELIST_WLOCK()	rw_wlock(&ng_typelist_lock)
#define	TYPELIST_WUNLOCK()	rw_wunlock(&ng_typelist_lock)

/* Hash related definitions. */
LIST_HEAD(nodehash, ng_node);
VNET_DEFINE_STATIC(struct nodehash *, ng_ID_hash);
VNET_DEFINE_STATIC(u_long, ng_ID_hmask);
VNET_DEFINE_STATIC(u_long, ng_nodes);
VNET_DEFINE_STATIC(struct nodehash *, ng_name_hash);
VNET_DEFINE_STATIC(u_long, ng_name_hmask);
VNET_DEFINE_STATIC(u_long, ng_named_nodes);
#define	V_ng_ID_hash		VNET(ng_ID_hash)
#define	V_ng_ID_hmask		VNET(ng_ID_hmask)
#define	V_ng_nodes		VNET(ng_nodes)
#define	V_ng_name_hash		VNET(ng_name_hash)
#define	V_ng_name_hmask		VNET(ng_name_hmask)
#define	V_ng_named_nodes	VNET(ng_named_nodes)

static struct rwlock	ng_idhash_lock;
#define	IDHASH_RLOCK()		rw_rlock(&ng_idhash_lock)
#define	IDHASH_RUNLOCK()	rw_runlock(&ng_idhash_lock)
#define	IDHASH_WLOCK()		rw_wlock(&ng_idhash_lock)
#define	IDHASH_WUNLOCK()	rw_wunlock(&ng_idhash_lock)

/* Method to find a node.. used twice so do it here */
#define NG_IDHASH_FN(ID) ((ID) % (V_ng_ID_hmask + 1))
#define NG_IDHASH_FIND(ID, node)					\
	do { 								\
		rw_assert(&ng_idhash_lock, RA_LOCKED);			\
		LIST_FOREACH(node, &V_ng_ID_hash[NG_IDHASH_FN(ID)],	\
						nd_idnodes) {		\
			if (NG_NODE_IS_VALID(node)			\
			&& (NG_NODE_ID(node) == ID)) {			\
				break;					\
			}						\
		}							\
	} while (0)

static struct rwlock	ng_namehash_lock;
#define	NAMEHASH_RLOCK()	rw_rlock(&ng_namehash_lock)
#define	NAMEHASH_RUNLOCK()	rw_runlock(&ng_namehash_lock)
#define	NAMEHASH_WLOCK()	rw_wlock(&ng_namehash_lock)
#define	NAMEHASH_WUNLOCK()	rw_wunlock(&ng_namehash_lock)

/* Internal functions */
static int	ng_add_hook(node_p node, const char *name, hook_p * hookp);
static int	ng_generic_msg(node_p here, item_p item, hook_p lasthook);
static ng_ID_t	ng_decodeidname(const char *name);
static int	ngb_mod_event(module_t mod, int event, void *data);
static void	ng_worklist_add(node_p node);
static void	ngthread(void *);
static int	ng_apply_item(node_p node, item_p item, int rw);
static void	ng_flush_input_queue(node_p node);
static node_p	ng_ID2noderef(ng_ID_t ID);
static int	ng_con_nodes(item_p item, node_p node, const char *name,
		    node_p node2, const char *name2);
static int	ng_con_part2(node_p node, item_p item, hook_p hook);
static int	ng_con_part3(node_p node, item_p item, hook_p hook);
static int	ng_mkpeer(node_p node, const char *name, const char *name2,
		    char *type);
static void	ng_name_rehash(void);
static void	ng_ID_rehash(void);

/* Imported, these used to be externally visible, some may go back. */
void	ng_destroy_hook(hook_p hook);
int	ng_path2noderef(node_p here, const char *path,
	node_p *dest, hook_p *lasthook);
int	ng_make_node(const char *type, node_p *nodepp);
int	ng_path_parse(char *addr, char **node, char **path, char **hook);
void	ng_rmnode(node_p node, hook_p dummy1, void *dummy2, int dummy3);
void	ng_unname(node_p node);

/* Our own netgraph malloc type */
MALLOC_DEFINE(M_NETGRAPH, "netgraph", "netgraph structures and ctrl messages");
MALLOC_DEFINE(M_NETGRAPH_MSG, "netgraph_msg", "netgraph name storage");
static MALLOC_DEFINE(M_NETGRAPH_HOOK, "netgraph_hook",
    "netgraph hook structures");
static MALLOC_DEFINE(M_NETGRAPH_NODE, "netgraph_node",
    "netgraph node structures");
static MALLOC_DEFINE(M_NETGRAPH_ITEM, "netgraph_item",
    "netgraph item structures");

/* Should not be visible outside this file */

#define _NG_ALLOC_HOOK(hook) \
	hook = malloc(sizeof(*hook), M_NETGRAPH_HOOK, M_NOWAIT | M_ZERO)
#define _NG_ALLOC_NODE(node) \
	node = malloc(sizeof(*node), M_NETGRAPH_NODE, M_NOWAIT | M_ZERO)

#define	NG_QUEUE_LOCK_INIT(n)			\
	mtx_init(&(n)->q_mtx, "ng_node", NULL, MTX_DEF)
#define	NG_QUEUE_LOCK(n)			\
	mtx_lock(&(n)->q_mtx)
#define	NG_QUEUE_UNLOCK(n)			\
	mtx_unlock(&(n)->q_mtx)
#define	NG_WORKLIST_LOCK_INIT()			\
	mtx_init(&ng_worklist_mtx, "ng_worklist", NULL, MTX_DEF)
#define	NG_WORKLIST_LOCK()			\
	mtx_lock(&ng_worklist_mtx)
#define	NG_WORKLIST_UNLOCK()			\
	mtx_unlock(&ng_worklist_mtx)
#define	NG_WORKLIST_SLEEP()			\
	mtx_sleep(&ng_worklist, &ng_worklist_mtx, PI_NET, "sleep", 0)
#define	NG_WORKLIST_WAKEUP()			\
	wakeup_one(&ng_worklist)

#ifdef NETGRAPH_DEBUG /*----------------------------------------------*/
/*
 * In debug mode:
 * In an attempt to help track reference count screwups
 * we do not free objects back to the malloc system, but keep them
 * in a local cache where we can examine them and keep information safely
 * after they have been freed.
 * We use this scheme for nodes and hooks, and to some extent for items.
 */
static __inline hook_p
ng_alloc_hook(void)
{
	hook_p hook;
	SLIST_ENTRY(ng_hook) temp;
	mtx_lock(&ng_nodelist_mtx);
	hook = LIST_FIRST(&ng_freehooks);
	if (hook) {
		LIST_REMOVE(hook, hk_hooks);
		bcopy(&hook->hk_all, &temp, sizeof(temp));
		bzero(hook, sizeof(struct ng_hook));
		bcopy(&temp, &hook->hk_all, sizeof(temp));
		mtx_unlock(&ng_nodelist_mtx);
		hook->hk_magic = HK_MAGIC;
	} else {
		mtx_unlock(&ng_nodelist_mtx);
		_NG_ALLOC_HOOK(hook);
		if (hook) {
			hook->hk_magic = HK_MAGIC;
			mtx_lock(&ng_nodelist_mtx);
			SLIST_INSERT_HEAD(&ng_allhooks, hook, hk_all);
			mtx_unlock(&ng_nodelist_mtx);
		}
	}
	return (hook);
}

static __inline node_p
ng_alloc_node(void)
{
	node_p node;
	SLIST_ENTRY(ng_node) temp;
	mtx_lock(&ng_nodelist_mtx);
	node = LIST_FIRST(&ng_freenodes);
	if (node) {
		LIST_REMOVE(node, nd_nodes);
		bcopy(&node->nd_all, &temp, sizeof(temp));
		bzero(node, sizeof(struct ng_node));
		bcopy(&temp, &node->nd_all, sizeof(temp));
		mtx_unlock(&ng_nodelist_mtx);
		node->nd_magic = ND_MAGIC;
	} else {
		mtx_unlock(&ng_nodelist_mtx);
		_NG_ALLOC_NODE(node);
		if (node) {
			node->nd_magic = ND_MAGIC;
			mtx_lock(&ng_nodelist_mtx);
			SLIST_INSERT_HEAD(&ng_allnodes, node, nd_all);
			mtx_unlock(&ng_nodelist_mtx);
		}
	}
	return (node);
}

#define NG_ALLOC_HOOK(hook) do { (hook) = ng_alloc_hook(); } while (0)
#define NG_ALLOC_NODE(node) do { (node) = ng_alloc_node(); } while (0)

#define NG_FREE_HOOK(hook)						\
	do {								\
		mtx_lock(&ng_nodelist_mtx);				\
		LIST_INSERT_HEAD(&ng_freehooks, hook, hk_hooks);	\
		hook->hk_magic = 0;					\
		mtx_unlock(&ng_nodelist_mtx);				\
	} while (0)

#define NG_FREE_NODE(node)						\
	do {								\
		mtx_lock(&ng_nodelist_mtx);				\
		LIST_INSERT_HEAD(&ng_freenodes, node, nd_nodes);	\
		node->nd_magic = 0;					\
		mtx_unlock(&ng_nodelist_mtx);				\
	} while (0)

#else /* NETGRAPH_DEBUG */ /*----------------------------------------------*/

#define NG_ALLOC_HOOK(hook) _NG_ALLOC_HOOK(hook)
#define NG_ALLOC_NODE(node) _NG_ALLOC_NODE(node)

#define NG_FREE_HOOK(hook) do { free((hook), M_NETGRAPH_HOOK); } while (0)
#define NG_FREE_NODE(node) do { free((node), M_NETGRAPH_NODE); } while (0)

#endif /* NETGRAPH_DEBUG */ /*----------------------------------------------*/

/* Set this to kdb_enter("X") to catch all errors as they occur */
#ifndef TRAP_ERROR
#define TRAP_ERROR()
#endif

VNET_DEFINE_STATIC(ng_ID_t, nextID) = 1;
#define	V_nextID			VNET(nextID)

#ifdef INVARIANTS
#define CHECK_DATA_MBUF(m)	do {					\
		struct mbuf *n;						\
		int total;						\
									\
		M_ASSERTPKTHDR(m);					\
		for (total = 0, n = (m); n != NULL; n = n->m_next) {	\
			total += n->m_len;				\
			if (n->m_nextpkt != NULL)			\
				panic("%s: m_nextpkt", __func__);	\
		}							\
									\
		if ((m)->m_pkthdr.len != total) {			\
			panic("%s: %d != %d",				\
			    __func__, (m)->m_pkthdr.len, total);	\
		}							\
	} while (0)
#else
#define CHECK_DATA_MBUF(m)
#endif

#define ERROUT(x)	do { error = (x); goto done; } while (0)

/************************************************************************
	Parse type definitions for generic messages
************************************************************************/

/* Handy structure parse type defining macro */
#define DEFINE_PARSE_STRUCT_TYPE(lo, up, args)				\
static const struct ng_parse_struct_field				\
	ng_ ## lo ## _type_fields[] = NG_GENERIC_ ## up ## _INFO args;	\
static const struct ng_parse_type ng_generic_ ## lo ## _type = {	\
	&ng_parse_struct_type,						\
	&ng_ ## lo ## _type_fields					\
}

DEFINE_PARSE_STRUCT_TYPE(mkpeer, MKPEER, ());
DEFINE_PARSE_STRUCT_TYPE(connect, CONNECT, ());
DEFINE_PARSE_STRUCT_TYPE(name, NAME, ());
DEFINE_PARSE_STRUCT_TYPE(rmhook, RMHOOK, ());
DEFINE_PARSE_STRUCT_TYPE(nodeinfo, NODEINFO, ());
DEFINE_PARSE_STRUCT_TYPE(typeinfo, TYPEINFO, ());
DEFINE_PARSE_STRUCT_TYPE(linkinfo, LINKINFO, (&ng_generic_nodeinfo_type));

/* Get length of an array when the length is stored as a 32 bit
   value immediately preceding the array -- as with struct namelist
   and struct typelist. */
static int
ng_generic_list_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	return *((const u_int32_t *)(buf - 4));
}

/* Get length of the array of struct linkinfo inside a struct hooklist */
static int
ng_generic_linkinfo_getLength(const struct ng_parse_type *type,
	const u_char *start, const u_char *buf)
{
	const struct hooklist *hl = (const struct hooklist *)start;

	return hl->nodeinfo.hooks;
}

/* Array type for a variable length array of struct namelist */
static const struct ng_parse_array_info ng_nodeinfoarray_type_info = {
	&ng_generic_nodeinfo_type,
	&ng_generic_list_getLength
};
static const struct ng_parse_type ng_generic_nodeinfoarray_type = {
	&ng_parse_array_type,
	&ng_nodeinfoarray_type_info
};

/* Array type for a variable length array of struct typelist */
static const struct ng_parse_array_info ng_typeinfoarray_type_info = {
	&ng_generic_typeinfo_type,
	&ng_generic_list_getLength
};
static const struct ng_parse_type ng_generic_typeinfoarray_type = {
	&ng_parse_array_type,
	&ng_typeinfoarray_type_info
};

/* Array type for array of struct linkinfo in struct hooklist */
static const struct ng_parse_array_info ng_generic_linkinfo_array_type_info = {
	&ng_generic_linkinfo_type,
	&ng_generic_linkinfo_getLength
};
static const struct ng_parse_type ng_generic_linkinfo_array_type = {
	&ng_parse_array_type,
	&ng_generic_linkinfo_array_type_info
};

DEFINE_PARSE_STRUCT_TYPE(typelist, TYPELIST, (&ng_generic_typeinfoarray_type));
DEFINE_PARSE_STRUCT_TYPE(hooklist, HOOKLIST,
	(&ng_generic_nodeinfo_type, &ng_generic_linkinfo_array_type));
DEFINE_PARSE_STRUCT_TYPE(listnodes, LISTNODES,
	(&ng_generic_nodeinfoarray_type));

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_generic_cmds[] = {
	{
	  NGM_GENERIC_COOKIE,
	  NGM_SHUTDOWN,
	  "shutdown",
	  NULL,
	  NULL
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_MKPEER,
	  "mkpeer",
	  &ng_generic_mkpeer_type,
	  NULL
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_CONNECT,
	  "connect",
	  &ng_generic_connect_type,
	  NULL
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_NAME,
	  "name",
	  &ng_generic_name_type,
	  NULL
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_RMHOOK,
	  "rmhook",
	  &ng_generic_rmhook_type,
	  NULL
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_NODEINFO,
	  "nodeinfo",
	  NULL,
	  &ng_generic_nodeinfo_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_LISTHOOKS,
	  "listhooks",
	  NULL,
	  &ng_generic_hooklist_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_LISTNAMES,
	  "listnames",
	  NULL,
	  &ng_generic_listnodes_type	/* same as NGM_LISTNODES */
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_LISTNODES,
	  "listnodes",
	  NULL,
	  &ng_generic_listnodes_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_LISTTYPES,
	  "listtypes",
	  NULL,
	  &ng_generic_typelist_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_TEXT_CONFIG,
	  "textconfig",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_TEXT_STATUS,
	  "textstatus",
	  NULL,
	  &ng_parse_string_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_ASCII2BINARY,
	  "ascii2binary",
	  &ng_parse_ng_mesg_type,
	  &ng_parse_ng_mesg_type
	},
	{
	  NGM_GENERIC_COOKIE,
	  NGM_BINARY2ASCII,
	  "binary2ascii",
	  &ng_parse_ng_mesg_type,
	  &ng_parse_ng_mesg_type
	},
	{ 0 }
};

/************************************************************************
			Node routines
************************************************************************/

/*
 * Instantiate a node of the requested type
 */
int
ng_make_node(const char *typename, node_p *nodepp)
{
	struct ng_type *type;
	int	error;

	/* Check that the type makes sense */
	if (typename == NULL) {
		TRAP_ERROR();
		return (EINVAL);
	}

	/* Locate the node type. If we fail we return. Do not try to load
	 * module.
	 */
	if ((type = ng_findtype(typename)) == NULL)
		return (ENXIO);

	/*
	 * If we have a constructor, then make the node and
	 * call the constructor to do type specific initialisation.
	 */
	if (type->constructor != NULL) {
		if ((error = ng_make_node_common(type, nodepp)) == 0) {
			if ((error = ((*type->constructor)(*nodepp))) != 0) {
				NG_NODE_UNREF(*nodepp);
			}
		}
	} else {
		/*
		 * Node has no constructor. We cannot ask for one
		 * to be made. It must be brought into existence by
		 * some external agency. The external agency should
		 * call ng_make_node_common() directly to get the
		 * netgraph part initialised.
		 */
		TRAP_ERROR();
		error = EINVAL;
	}
	return (error);
}

/*
 * Generic node creation. Called by node initialisation for externally
 * instantiated nodes (e.g. hardware, sockets, etc ).
 * The returned node has a reference count of 1.
 */
int
ng_make_node_common(struct ng_type *type, node_p *nodepp)
{
	node_p node;

	/* Require the node type to have been already installed */
	if (ng_findtype(type->name) == NULL) {
		TRAP_ERROR();
		return (EINVAL);
	}

	/* Make a node and try attach it to the type */
	NG_ALLOC_NODE(node);
	if (node == NULL) {
		TRAP_ERROR();
		return (ENOMEM);
	}
	node->nd_type = type;
#ifdef VIMAGE
	node->nd_vnet = curvnet;
#endif
	NG_NODE_REF(node);				/* note reference */
	type->refs++;

	NG_QUEUE_LOCK_INIT(&node->nd_input_queue);
	STAILQ_INIT(&node->nd_input_queue.queue);
	node->nd_input_queue.q_flags = 0;

	/* Initialize hook list for new node */
	LIST_INIT(&node->nd_hooks);

	/* Get an ID and put us in the hash chain. */
	IDHASH_WLOCK();
	for (;;) { /* wrap protection, even if silly */
		node_p node2 = NULL;
		node->nd_ID = V_nextID++; /* 137/sec for 1 year before wrap */

		/* Is there a problem with the new number? */
		NG_IDHASH_FIND(node->nd_ID, node2); /* already taken? */
		if ((node->nd_ID != 0) && (node2 == NULL)) {
			break;
		}
	}
	V_ng_nodes++;
	if (V_ng_nodes * 2 > V_ng_ID_hmask)
		ng_ID_rehash();
	LIST_INSERT_HEAD(&V_ng_ID_hash[NG_IDHASH_FN(node->nd_ID)], node,
	    nd_idnodes);
	IDHASH_WUNLOCK();

	/* Done */
	*nodepp = node;
	return (0);
}

/*
 * Forceably start the shutdown process on a node. Either call
 * its shutdown method, or do the default shutdown if there is
 * no type-specific method.
 *
 * We can only be called from a shutdown message, so we know we have
 * a writer lock, and therefore exclusive access. It also means
 * that we should not be on the work queue, but we check anyhow.
 *
 * Persistent node types must have a type-specific method which
 * allocates a new node in which case, this one is irretrievably going away,
 * or cleans up anything it needs, and just makes the node valid again,
 * in which case we allow the node to survive.
 *
 * XXX We need to think of how to tell a persistent node that we
 * REALLY need to go away because the hardware has gone or we
 * are rebooting.... etc.
 */
void
ng_rmnode(node_p node, hook_p dummy1, void *dummy2, int dummy3)
{
	hook_p hook;

	/* Check if it's already shutting down */
	if ((node->nd_flags & NGF_CLOSING) != 0)
		return;

	if (node == &ng_deadnode) {
		printf ("shutdown called on deadnode\n");
		return;
	}

	/* Add an extra reference so it doesn't go away during this */
	NG_NODE_REF(node);

	/*
	 * Mark it invalid so any newcomers know not to try use it
	 * Also add our own mark so we can't recurse
	 * note that NGF_INVALID does not do this as it's also set during
	 * creation
	 */
	node->nd_flags |= NGF_INVALID|NGF_CLOSING;

	/* If node has its pre-shutdown method, then call it first*/
	if (node->nd_type && node->nd_type->close)
		(*node->nd_type->close)(node);

	/* Notify all remaining connected nodes to disconnect */
	while ((hook = LIST_FIRST(&node->nd_hooks)) != NULL)
		ng_destroy_hook(hook);

	/*
	 * Drain the input queue forceably.
	 * it has no hooks so what's it going to do, bleed on someone?
	 * Theoretically we came here from a queue entry that was added
	 * Just before the queue was closed, so it should be empty anyway.
	 * Also removes us from worklist if needed.
	 */
	ng_flush_input_queue(node);

	/* Ask the type if it has anything to do in this case */
	if (node->nd_type && node->nd_type->shutdown) {
		(*node->nd_type->shutdown)(node);
		if (NG_NODE_IS_VALID(node)) {
			/*
			 * Well, blow me down if the node code hasn't declared
			 * that it doesn't want to die.
			 * Presumably it is a persistent node.
			 * If we REALLY want it to go away,
			 *  e.g. hardware going away,
			 * Our caller should set NGF_REALLY_DIE in nd_flags.
			 */
			node->nd_flags &= ~(NGF_INVALID|NGF_CLOSING);
			NG_NODE_UNREF(node); /* Assume they still have theirs */
			return;
		}
	} else {				/* do the default thing */
		NG_NODE_UNREF(node);
	}

	ng_unname(node); /* basically a NOP these days */

	/*
	 * Remove extra reference, possibly the last
	 * Possible other holders of references may include
	 * timeout callouts, but theoretically the node's supposed to
	 * have cancelled them. Possibly hardware dependencies may
	 * force a driver to 'linger' with a reference.
	 */
	NG_NODE_UNREF(node);
}

/*
 * Remove a reference to the node, possibly the last.
 * deadnode always acts as it it were the last.
 */
void
ng_unref_node(node_p node)
{

	if (node == &ng_deadnode)
		return;

	CURVNET_SET(node->nd_vnet);

	if (refcount_release(&node->nd_refs)) { /* we were the last */

		node->nd_type->refs--; /* XXX maybe should get types lock? */
		NAMEHASH_WLOCK();
		if (NG_NODE_HAS_NAME(node)) {
			V_ng_named_nodes--;
			LIST_REMOVE(node, nd_nodes);
		}
		NAMEHASH_WUNLOCK();

		IDHASH_WLOCK();
		V_ng_nodes--;
		LIST_REMOVE(node, nd_idnodes);
		IDHASH_WUNLOCK();

		mtx_destroy(&node->nd_input_queue.q_mtx);
		NG_FREE_NODE(node);
	}
	CURVNET_RESTORE();
}

/************************************************************************
			Node ID handling
************************************************************************/
static node_p
ng_ID2noderef(ng_ID_t ID)
{
	node_p node;

	IDHASH_RLOCK();
	NG_IDHASH_FIND(ID, node);
	if (node)
		NG_NODE_REF(node);
	IDHASH_RUNLOCK();
	return(node);
}

ng_ID_t
ng_node2ID(node_p node)
{
	return (node ? NG_NODE_ID(node) : 0);
}

/************************************************************************
			Node name handling
************************************************************************/

/*
 * Assign a node a name.
 */
int
ng_name_node(node_p node, const char *name)
{
	uint32_t hash;
	node_p node2;
	int i;

	/* Check the name is valid */
	for (i = 0; i < NG_NODESIZ; i++) {
		if (name[i] == '\0' || name[i] == '.' || name[i] == ':')
			break;
	}
	if (i == 0 || name[i] != '\0') {
		TRAP_ERROR();
		return (EINVAL);
	}
	if (ng_decodeidname(name) != 0) { /* valid IDs not allowed here */
		TRAP_ERROR();
		return (EINVAL);
	}

	NAMEHASH_WLOCK();
	if (V_ng_named_nodes * 2 > V_ng_name_hmask)
		ng_name_rehash();

	hash = hash32_str(name, HASHINIT) & V_ng_name_hmask;
	/* Check the name isn't already being used. */
	LIST_FOREACH(node2, &V_ng_name_hash[hash], nd_nodes)
		if (NG_NODE_IS_VALID(node2) &&
		    (strcmp(NG_NODE_NAME(node2), name) == 0)) {
			NAMEHASH_WUNLOCK();
			return (EADDRINUSE);
		}

	if (NG_NODE_HAS_NAME(node))
		LIST_REMOVE(node, nd_nodes);
	else
		V_ng_named_nodes++;
	/* Copy it. */
	strlcpy(NG_NODE_NAME(node), name, NG_NODESIZ);
	/* Update name hash. */
	LIST_INSERT_HEAD(&V_ng_name_hash[hash], node, nd_nodes);
	NAMEHASH_WUNLOCK();

	return (0);
}

/*
 * Find a node by absolute name. The name should NOT end with ':'
 * The name "." means "this node" and "[xxx]" means "the node
 * with ID (ie, at address) xxx".
 *
 * Returns the node if found, else NULL.
 * Eventually should add something faster than a sequential search.
 * Note it acquires a reference on the node so you can be sure it's still
 * there.
 */
node_p
ng_name2noderef(node_p here, const char *name)
{
	node_p node;
	ng_ID_t temp;
	int	hash;

	/* "." means "this node" */
	if (strcmp(name, ".") == 0) {
		NG_NODE_REF(here);
		return(here);
	}

	/* Check for name-by-ID */
	if ((temp = ng_decodeidname(name)) != 0) {
		return (ng_ID2noderef(temp));
	}

	/* Find node by name. */
	hash = hash32_str(name, HASHINIT) & V_ng_name_hmask;
	NAMEHASH_RLOCK();
	LIST_FOREACH(node, &V_ng_name_hash[hash], nd_nodes)
		if (NG_NODE_IS_VALID(node) &&
		    (strcmp(NG_NODE_NAME(node), name) == 0)) {
			NG_NODE_REF(node);
			break;
		}
	NAMEHASH_RUNLOCK();

	return (node);
}

/*
 * Decode an ID name, eg. "[f03034de]". Returns 0 if the
 * string is not valid, otherwise returns the value.
 */
static ng_ID_t
ng_decodeidname(const char *name)
{
	const int len = strlen(name);
	char *eptr;
	u_long val;

	/* Check for proper length, brackets, no leading junk */
	if ((len < 3) || (name[0] != '[') || (name[len - 1] != ']') ||
	    (!isxdigit(name[1])))
		return ((ng_ID_t)0);

	/* Decode number */
	val = strtoul(name + 1, &eptr, 16);
	if ((eptr - name != len - 1) || (val == ULONG_MAX) || (val == 0))
		return ((ng_ID_t)0);

	return ((ng_ID_t)val);
}

/*
 * Remove a name from a node. This should only be called
 * when shutting down and removing the node.
 */
void
ng_unname(node_p node)
{
}

/*
 * Allocate a bigger name hash.
 */
static void
ng_name_rehash()
{
	struct nodehash *new;
	uint32_t hash;
	u_long hmask;
	node_p node, node2;
	int i;

	new = hashinit_flags((V_ng_name_hmask + 1) * 2, M_NETGRAPH_NODE, &hmask,
	    HASH_NOWAIT);
	if (new == NULL)
		return;

	for (i = 0; i <= V_ng_name_hmask; i++)
		LIST_FOREACH_SAFE(node, &V_ng_name_hash[i], nd_nodes, node2) {
#ifdef INVARIANTS
			LIST_REMOVE(node, nd_nodes);
#endif
			hash = hash32_str(NG_NODE_NAME(node), HASHINIT) & hmask;
			LIST_INSERT_HEAD(&new[hash], node, nd_nodes);
		}

	hashdestroy(V_ng_name_hash, M_NETGRAPH_NODE, V_ng_name_hmask);
	V_ng_name_hash = new;
	V_ng_name_hmask = hmask;
}

/*
 * Allocate a bigger ID hash.
 */
static void
ng_ID_rehash()
{
	struct nodehash *new;
	uint32_t hash;
	u_long hmask;
	node_p node, node2;
	int i;

	new = hashinit_flags((V_ng_ID_hmask + 1) * 2, M_NETGRAPH_NODE, &hmask,
	    HASH_NOWAIT);
	if (new == NULL)
		return;

	for (i = 0; i <= V_ng_ID_hmask; i++)
		LIST_FOREACH_SAFE(node, &V_ng_ID_hash[i], nd_idnodes, node2) {
#ifdef INVARIANTS
			LIST_REMOVE(node, nd_idnodes);
#endif
			hash = (node->nd_ID % (hmask + 1));
			LIST_INSERT_HEAD(&new[hash], node, nd_idnodes);
		}

	hashdestroy(V_ng_ID_hash, M_NETGRAPH_NODE, V_ng_name_hmask);
	V_ng_ID_hash = new;
	V_ng_ID_hmask = hmask;
}

/************************************************************************
			Hook routines
 Names are not optional. Hooks are always connected, except for a
 brief moment within these routines. On invalidation or during creation
 they are connected to the 'dead' hook.
************************************************************************/

/*
 * Remove a hook reference
 */
void
ng_unref_hook(hook_p hook)
{

	if (hook == &ng_deadhook)
		return;

	if (refcount_release(&hook->hk_refs)) { /* we were the last */
		if (_NG_HOOK_NODE(hook)) /* it'll probably be ng_deadnode */
			_NG_NODE_UNREF((_NG_HOOK_NODE(hook)));
		NG_FREE_HOOK(hook);
	}
}

/*
 * Add an unconnected hook to a node. Only used internally.
 * Assumes node is locked. (XXX not yet true )
 */
static int
ng_add_hook(node_p node, const char *name, hook_p *hookp)
{
	hook_p hook;
	int error = 0;

	/* Check that the given name is good */
	if (name == NULL) {
		TRAP_ERROR();
		return (EINVAL);
	}
	if (ng_findhook(node, name) != NULL) {
		TRAP_ERROR();
		return (EEXIST);
	}

	/* Allocate the hook and link it up */
	NG_ALLOC_HOOK(hook);
	if (hook == NULL) {
		TRAP_ERROR();
		return (ENOMEM);
	}
	hook->hk_refs = 1;		/* add a reference for us to return */
	hook->hk_flags = HK_INVALID;
	hook->hk_peer = &ng_deadhook;	/* start off this way */
	hook->hk_node = node;
	NG_NODE_REF(node);		/* each hook counts as a reference */

	/* Set hook name */
	strlcpy(NG_HOOK_NAME(hook), name, NG_HOOKSIZ);

	/*
	 * Check if the node type code has something to say about it
	 * If it fails, the unref of the hook will also unref the node.
	 */
	if (node->nd_type->newhook != NULL) {
		if ((error = (*node->nd_type->newhook)(node, hook, name))) {
			NG_HOOK_UNREF(hook);	/* this frees the hook */
			return (error);
		}
	}
	/*
	 * The 'type' agrees so far, so go ahead and link it in.
	 * We'll ask again later when we actually connect the hooks.
	 */
	LIST_INSERT_HEAD(&node->nd_hooks, hook, hk_hooks);
	node->nd_numhooks++;
	NG_HOOK_REF(hook);	/* one for the node */

	if (hookp)
		*hookp = hook;
	return (0);
}

/*
 * Find a hook
 *
 * Node types may supply their own optimized routines for finding
 * hooks.  If none is supplied, we just do a linear search.
 * XXX Possibly we should add a reference to the hook?
 */
hook_p
ng_findhook(node_p node, const char *name)
{
	hook_p hook;

	if (node->nd_type->findhook != NULL)
		return (*node->nd_type->findhook)(node, name);
	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		if (NG_HOOK_IS_VALID(hook) &&
		    (strcmp(NG_HOOK_NAME(hook), name) == 0))
			return (hook);
	}
	return (NULL);
}

/*
 * Destroy a hook
 *
 * As hooks are always attached, this really destroys two hooks.
 * The one given, and the one attached to it. Disconnect the hooks
 * from each other first. We reconnect the peer hook to the 'dead'
 * hook so that it can still exist after we depart. We then
 * send the peer its own destroy message. This ensures that we only
 * interact with the peer's structures when it is locked processing that
 * message. We hold a reference to the peer hook so we are guaranteed that
 * the peer hook and node are still going to exist until
 * we are finished there as the hook holds a ref on the node.
 * We run this same code again on the peer hook, but that time it is already
 * attached to the 'dead' hook.
 *
 * This routine is called at all stages of hook creation
 * on error detection and must be able to handle any such stage.
 */
void
ng_destroy_hook(hook_p hook)
{
	hook_p peer;
	node_p node;

	if (hook == &ng_deadhook) {	/* better safe than sorry */
		printf("ng_destroy_hook called on deadhook\n");
		return;
	}

	/*
	 * Protect divorce process with mutex, to avoid races on
	 * simultaneous disconnect.
	 */
	TOPOLOGY_WLOCK();

	hook->hk_flags |= HK_INVALID;

	peer = NG_HOOK_PEER(hook);
	node = NG_HOOK_NODE(hook);

	if (peer && (peer != &ng_deadhook)) {
		/*
		 * Set the peer to point to ng_deadhook
		 * from this moment on we are effectively independent it.
		 * send it an rmhook message of its own.
		 */
		peer->hk_peer = &ng_deadhook;	/* They no longer know us */
		hook->hk_peer = &ng_deadhook;	/* Nor us, them */
		if (NG_HOOK_NODE(peer) == &ng_deadnode) {
			/*
			 * If it's already divorced from a node,
			 * just free it.
			 */
			TOPOLOGY_WUNLOCK();
		} else {
			TOPOLOGY_WUNLOCK();
			ng_rmhook_self(peer); 	/* Send it a surprise */
		}
		NG_HOOK_UNREF(peer);		/* account for peer link */
		NG_HOOK_UNREF(hook);		/* account for peer link */
	} else
		TOPOLOGY_WUNLOCK();

	TOPOLOGY_NOTOWNED();

	/*
	 * Remove the hook from the node's list to avoid possible recursion
	 * in case the disconnection results in node shutdown.
	 */
	if (node == &ng_deadnode) { /* happens if called from ng_con_nodes() */
		return;
	}
	LIST_REMOVE(hook, hk_hooks);
	node->nd_numhooks--;
	if (node->nd_type->disconnect) {
		/*
		 * The type handler may elect to destroy the node so don't
		 * trust its existence after this point. (except
		 * that we still hold a reference on it. (which we
		 * inherrited from the hook we are destroying)
		 */
		(*node->nd_type->disconnect) (hook);
	}

	/*
	 * Note that because we will point to ng_deadnode, the original node
	 * is not decremented automatically so we do that manually.
	 */
	_NG_HOOK_NODE(hook) = &ng_deadnode;
	NG_NODE_UNREF(node);	/* We no longer point to it so adjust count */
	NG_HOOK_UNREF(hook);	/* Account for linkage (in list) to node */
}

/*
 * Take two hooks on a node and merge the connection so that the given node
 * is effectively bypassed.
 */
int
ng_bypass(hook_p hook1, hook_p hook2)
{
	if (hook1->hk_node != hook2->hk_node) {
		TRAP_ERROR();
		return (EINVAL);
	}
	TOPOLOGY_WLOCK();
	if (NG_HOOK_NOT_VALID(hook1) || NG_HOOK_NOT_VALID(hook2)) {
		TOPOLOGY_WUNLOCK();
		return (EINVAL);
	}
	hook1->hk_peer->hk_peer = hook2->hk_peer;
	hook2->hk_peer->hk_peer = hook1->hk_peer;

	hook1->hk_peer = &ng_deadhook;
	hook2->hk_peer = &ng_deadhook;
	TOPOLOGY_WUNLOCK();

	NG_HOOK_UNREF(hook1);
	NG_HOOK_UNREF(hook2);

	/* XXX If we ever cache methods on hooks update them as well */
	ng_destroy_hook(hook1);
	ng_destroy_hook(hook2);
	return (0);
}

/*
 * Install a new netgraph type
 */
int
ng_newtype(struct ng_type *tp)
{
	const size_t namelen = strlen(tp->name);

	/* Check version and type name fields */
	if ((tp->version != NG_ABI_VERSION) || (namelen == 0) ||
	    (namelen >= NG_TYPESIZ)) {
		TRAP_ERROR();
		if (tp->version != NG_ABI_VERSION) {
			printf("Netgraph: Node type rejected. ABI mismatch. "
			    "Suggest recompile\n");
		}
		return (EINVAL);
	}

	/* Check for name collision */
	if (ng_findtype(tp->name) != NULL) {
		TRAP_ERROR();
		return (EEXIST);
	}

	/* Link in new type */
	TYPELIST_WLOCK();
	LIST_INSERT_HEAD(&ng_typelist, tp, types);
	tp->refs = 1;	/* first ref is linked list */
	TYPELIST_WUNLOCK();
	return (0);
}

/*
 * unlink a netgraph type
 * If no examples exist
 */
int
ng_rmtype(struct ng_type *tp)
{
	/* Check for name collision */
	if (tp->refs != 1) {
		TRAP_ERROR();
		return (EBUSY);
	}

	/* Unlink type */
	TYPELIST_WLOCK();
	LIST_REMOVE(tp, types);
	TYPELIST_WUNLOCK();
	return (0);
}

/*
 * Look for a type of the name given
 */
struct ng_type *
ng_findtype(const char *typename)
{
	struct ng_type *type;

	TYPELIST_RLOCK();
	LIST_FOREACH(type, &ng_typelist, types) {
		if (strcmp(type->name, typename) == 0)
			break;
	}
	TYPELIST_RUNLOCK();
	return (type);
}

/************************************************************************
			Composite routines
************************************************************************/
/*
 * Connect two nodes using the specified hooks, using queued functions.
 */
static int
ng_con_part3(node_p node, item_p item, hook_p hook)
{
	int	error = 0;

	/*
	 * When we run, we know that the node 'node' is locked for us.
	 * Our caller has a reference on the hook.
	 * Our caller has a reference on the node.
	 * (In this case our caller is ng_apply_item() ).
	 * The peer hook has a reference on the hook.
	 * We are all set up except for the final call to the node, and
	 * the clearing of the INVALID flag.
	 */
	if (NG_HOOK_NODE(hook) == &ng_deadnode) {
		/*
		 * The node must have been freed again since we last visited
		 * here. ng_destry_hook() has this effect but nothing else does.
		 * We should just release our references and
		 * free anything we can think of.
		 * Since we know it's been destroyed, and it's our caller
		 * that holds the references, just return.
		 */
		ERROUT(ENOENT);
	}
	if (hook->hk_node->nd_type->connect) {
		if ((error = (*hook->hk_node->nd_type->connect) (hook))) {
			ng_destroy_hook(hook);	/* also zaps peer */
			printf("failed in ng_con_part3()\n");
			ERROUT(error);
		}
	}
	/*
	 *  XXX this is wrong for SMP. Possibly we need
	 * to separate out 'create' and 'invalid' flags.
	 * should only set flags on hooks we have locked under our node.
	 */
	hook->hk_flags &= ~HK_INVALID;
done:
	NG_FREE_ITEM(item);
	return (error);
}

static int
ng_con_part2(node_p node, item_p item, hook_p hook)
{
	hook_p	peer;
	int	error = 0;

	/*
	 * When we run, we know that the node 'node' is locked for us.
	 * Our caller has a reference on the hook.
	 * Our caller has a reference on the node.
	 * (In this case our caller is ng_apply_item() ).
	 * The peer hook has a reference on the hook.
	 * our node pointer points to the 'dead' node.
	 * First check the hook name is unique.
	 * Should not happen because we checked before queueing this.
	 */
	if (ng_findhook(node, NG_HOOK_NAME(hook)) != NULL) {
		TRAP_ERROR();
		ng_destroy_hook(hook); /* should destroy peer too */
		printf("failed in ng_con_part2()\n");
		ERROUT(EEXIST);
	}
	/*
	 * Check if the node type code has something to say about it
	 * If it fails, the unref of the hook will also unref the attached node,
	 * however since that node is 'ng_deadnode' this will do nothing.
	 * The peer hook will also be destroyed.
	 */
	if (node->nd_type->newhook != NULL) {
		if ((error = (*node->nd_type->newhook)(node, hook,
		    hook->hk_name))) {
			ng_destroy_hook(hook); /* should destroy peer too */
			printf("failed in ng_con_part2()\n");
			ERROUT(error);
		}
	}

	/*
	 * The 'type' agrees so far, so go ahead and link it in.
	 * We'll ask again later when we actually connect the hooks.
	 */
	hook->hk_node = node;		/* just overwrite ng_deadnode */
	NG_NODE_REF(node);		/* each hook counts as a reference */
	LIST_INSERT_HEAD(&node->nd_hooks, hook, hk_hooks);
	node->nd_numhooks++;
	NG_HOOK_REF(hook);	/* one for the node */
	
	/*
	 * We now have a symmetrical situation, where both hooks have been
	 * linked to their nodes, the newhook methods have been called
	 * And the references are all correct. The hooks are still marked
	 * as invalid, as we have not called the 'connect' methods
	 * yet.
	 * We can call the local one immediately as we have the
	 * node locked, but we need to queue the remote one.
	 */
	if (hook->hk_node->nd_type->connect) {
		if ((error = (*hook->hk_node->nd_type->connect) (hook))) {
			ng_destroy_hook(hook);	/* also zaps peer */
			printf("failed in ng_con_part2(A)\n");
			ERROUT(error);
		}
	}

	/*
	 * Acquire topo mutex to avoid race with ng_destroy_hook().
	 */
	TOPOLOGY_RLOCK();
	peer = hook->hk_peer;
	if (peer == &ng_deadhook) {
		TOPOLOGY_RUNLOCK();
		printf("failed in ng_con_part2(B)\n");
		ng_destroy_hook(hook);
		ERROUT(ENOENT);
	}
	TOPOLOGY_RUNLOCK();

	if ((error = ng_send_fn2(peer->hk_node, peer, item, &ng_con_part3,
	    NULL, 0, NG_REUSE_ITEM))) {
		printf("failed in ng_con_part2(C)\n");
		ng_destroy_hook(hook);	/* also zaps peer */
		return (error);		/* item was consumed. */
	}
	hook->hk_flags &= ~HK_INVALID; /* need both to be able to work */
	return (0);			/* item was consumed. */
done:
	NG_FREE_ITEM(item);
	return (error);
}

/*
 * Connect this node with another node. We assume that this node is
 * currently locked, as we are only called from an NGM_CONNECT message.
 */
static int
ng_con_nodes(item_p item, node_p node, const char *name,
    node_p node2, const char *name2)
{
	int	error;
	hook_p	hook;
	hook_p	hook2;

	if (ng_findhook(node2, name2) != NULL) {
		return(EEXIST);
	}
	if ((error = ng_add_hook(node, name, &hook)))  /* gives us a ref */
		return (error);
	/* Allocate the other hook and link it up */
	NG_ALLOC_HOOK(hook2);
	if (hook2 == NULL) {
		TRAP_ERROR();
		ng_destroy_hook(hook);	/* XXX check ref counts so far */
		NG_HOOK_UNREF(hook);	/* including our ref */
		return (ENOMEM);
	}
	hook2->hk_refs = 1;		/* start with a reference for us. */
	hook2->hk_flags = HK_INVALID;
	hook2->hk_peer = hook;		/* Link the two together */
	hook->hk_peer = hook2;	
	NG_HOOK_REF(hook);		/* Add a ref for the peer to each*/
	NG_HOOK_REF(hook2);
	hook2->hk_node = &ng_deadnode;
	strlcpy(NG_HOOK_NAME(hook2), name2, NG_HOOKSIZ);

	/*
	 * Queue the function above.
	 * Procesing continues in that function in the lock context of
	 * the other node.
	 */
	if ((error = ng_send_fn2(node2, hook2, item, &ng_con_part2, NULL, 0,
	    NG_NOFLAGS))) {
		printf("failed in ng_con_nodes(): %d\n", error);
		ng_destroy_hook(hook);	/* also zaps peer */
	}

	NG_HOOK_UNREF(hook);		/* Let each hook go if it wants to */
	NG_HOOK_UNREF(hook2);
	return (error);
}

/*
 * Make a peer and connect.
 * We assume that the local node is locked.
 * The new node probably doesn't need a lock until
 * it has a hook, because it cannot really have any work until then,
 * but we should think about it a bit more.
 *
 * The problem may come if the other node also fires up
 * some hardware or a timer or some other source of activation,
 * also it may already get a command msg via it's ID.
 *
 * We could use the same method as ng_con_nodes() but we'd have
 * to add ability to remove the node when failing. (Not hard, just
 * make arg1 point to the node to remove).
 * Unless of course we just ignore failure to connect and leave
 * an unconnected node?
 */
static int
ng_mkpeer(node_p node, const char *name, const char *name2, char *type)
{
	node_p	node2;
	hook_p	hook1, hook2;
	int	error;

	if ((error = ng_make_node(type, &node2))) {
		return (error);
	}

	if ((error = ng_add_hook(node, name, &hook1))) { /* gives us a ref */
		ng_rmnode(node2, NULL, NULL, 0);
		return (error);
	}

	if ((error = ng_add_hook(node2, name2, &hook2))) {
		ng_rmnode(node2, NULL, NULL, 0);
		ng_destroy_hook(hook1);
		NG_HOOK_UNREF(hook1);
		return (error);
	}

	/*
	 * Actually link the two hooks together.
	 */
	hook1->hk_peer = hook2;
	hook2->hk_peer = hook1;

	/* Each hook is referenced by the other */
	NG_HOOK_REF(hook1);
	NG_HOOK_REF(hook2);

	/* Give each node the opportunity to veto the pending connection */
	if (hook1->hk_node->nd_type->connect) {
		error = (*hook1->hk_node->nd_type->connect) (hook1);
	}

	if ((error == 0) && hook2->hk_node->nd_type->connect) {
		error = (*hook2->hk_node->nd_type->connect) (hook2);

	}

	/*
	 * drop the references we were holding on the two hooks.
	 */
	if (error) {
		ng_destroy_hook(hook2);	/* also zaps hook1 */
		ng_rmnode(node2, NULL, NULL, 0);
	} else {
		/* As a last act, allow the hooks to be used */
		hook1->hk_flags &= ~HK_INVALID;
		hook2->hk_flags &= ~HK_INVALID;
	}
	NG_HOOK_UNREF(hook1);
	NG_HOOK_UNREF(hook2);
	return (error);
}

/************************************************************************
		Utility routines to send self messages
************************************************************************/
	
/* Shut this node down as soon as everyone is clear of it */
/* Should add arg "immediately" to jump the queue */
int
ng_rmnode_self(node_p node)
{
	int		error;

	if (node == &ng_deadnode)
		return (0);
	node->nd_flags |= NGF_INVALID;
	if (node->nd_flags & NGF_CLOSING)
		return (0);

	error = ng_send_fn(node, NULL, &ng_rmnode, NULL, 0);
	return (error);
}

static void
ng_rmhook_part2(node_p node, hook_p hook, void *arg1, int arg2)
{
	ng_destroy_hook(hook);
	return ;
}

int
ng_rmhook_self(hook_p hook)
{
	int		error;
	node_p node = NG_HOOK_NODE(hook);

	if (node == &ng_deadnode)
		return (0);

	error = ng_send_fn(node, hook, &ng_rmhook_part2, NULL, 0);
	return (error);
}

/***********************************************************************
 * Parse and verify a string of the form:  <NODE:><PATH>
 *
 * Such a string can refer to a specific node or a specific hook
 * on a specific node, depending on how you look at it. In the
 * latter case, the PATH component must not end in a dot.
 *
 * Both <NODE:> and <PATH> are optional. The <PATH> is a string
 * of hook names separated by dots. This breaks out the original
 * string, setting *nodep to "NODE" (or NULL if none) and *pathp
 * to "PATH" (or NULL if degenerate). Also, *hookp will point to
 * the final hook component of <PATH>, if any, otherwise NULL.
 *
 * This returns -1 if the path is malformed. The char ** are optional.
 ***********************************************************************/
int
ng_path_parse(char *addr, char **nodep, char **pathp, char **hookp)
{
	char	*node, *path, *hook;
	int	k;

	/*
	 * Extract absolute NODE, if any
	 */
	for (path = addr; *path && *path != ':'; path++);
	if (*path) {
		node = addr;	/* Here's the NODE */
		*path++ = '\0';	/* Here's the PATH */

		/* Node name must not be empty */
		if (!*node)
			return -1;

		/* A name of "." is OK; otherwise '.' not allowed */
		if (strcmp(node, ".") != 0) {
			for (k = 0; node[k]; k++)
				if (node[k] == '.')
					return -1;
		}
	} else {
		node = NULL;	/* No absolute NODE */
		path = addr;	/* Here's the PATH */
	}

	/* Snoop for illegal characters in PATH */
	for (k = 0; path[k]; k++)
		if (path[k] == ':')
			return -1;

	/* Check for no repeated dots in PATH */
	for (k = 0; path[k]; k++)
		if (path[k] == '.' && path[k + 1] == '.')
			return -1;

	/* Remove extra (degenerate) dots from beginning or end of PATH */
	if (path[0] == '.')
		path++;
	if (*path && path[strlen(path) - 1] == '.')
		path[strlen(path) - 1] = 0;

	/* If PATH has a dot, then we're not talking about a hook */
	if (*path) {
		for (hook = path, k = 0; path[k]; k++)
			if (path[k] == '.') {
				hook = NULL;
				break;
			}
	} else
		path = hook = NULL;

	/* Done */
	if (nodep)
		*nodep = node;
	if (pathp)
		*pathp = path;
	if (hookp)
		*hookp = hook;
	return (0);
}

/*
 * Given a path, which may be absolute or relative, and a starting node,
 * return the destination node.
 */
int
ng_path2noderef(node_p here, const char *address, node_p *destp,
    hook_p *lasthook)
{
	char    fullpath[NG_PATHSIZ];
	char   *nodename, *path;
	node_p  node, oldnode;

	/* Initialize */
	if (destp == NULL) {
		TRAP_ERROR();
		return EINVAL;
	}
	*destp = NULL;

	/* Make a writable copy of address for ng_path_parse() */
	strncpy(fullpath, address, sizeof(fullpath) - 1);
	fullpath[sizeof(fullpath) - 1] = '\0';

	/* Parse out node and sequence of hooks */
	if (ng_path_parse(fullpath, &nodename, &path, NULL) < 0) {
		TRAP_ERROR();
		return EINVAL;
	}

	/*
	 * For an absolute address, jump to the starting node.
	 * Note that this holds a reference on the node for us.
	 * Don't forget to drop the reference if we don't need it.
	 */
	if (nodename) {
		node = ng_name2noderef(here, nodename);
		if (node == NULL) {
			TRAP_ERROR();
			return (ENOENT);
		}
	} else {
		if (here == NULL) {
			TRAP_ERROR();
			return (EINVAL);
		}
		node = here;
		NG_NODE_REF(node);
	}

	if (path == NULL) {
		if (lasthook != NULL)
			*lasthook = NULL;
		*destp = node;
		return (0);
	}

	/*
	 * Now follow the sequence of hooks
	 *
	 * XXXGL: The path may demolish as we go the sequence, but if
	 * we hold the topology mutex at critical places, then, I hope,
	 * we would always have valid pointers in hand, although the
	 * path behind us may no longer exist.
	 */
	for (;;) {
		hook_p hook;
		char *segment;

		/*
		 * Break out the next path segment. Replace the dot we just
		 * found with a NUL; "path" points to the next segment (or the
		 * NUL at the end).
		 */
		for (segment = path; *path != '\0'; path++) {
			if (*path == '.') {
				*path++ = '\0';
				break;
			}
		}

		/* We have a segment, so look for a hook by that name */
		hook = ng_findhook(node, segment);

		TOPOLOGY_WLOCK();
		/* Can't get there from here... */
		if (hook == NULL || NG_HOOK_PEER(hook) == NULL ||
		    NG_HOOK_NOT_VALID(hook) ||
		    NG_HOOK_NOT_VALID(NG_HOOK_PEER(hook))) {
			TRAP_ERROR();
			NG_NODE_UNREF(node);
			TOPOLOGY_WUNLOCK();
			return (ENOENT);
		}

		/*
		 * Hop on over to the next node
		 * XXX
		 * Big race conditions here as hooks and nodes go away
		 * *** Idea.. store an ng_ID_t in each hook and use that
		 * instead of the direct hook in this crawl?
		 */
		oldnode = node;
		if ((node = NG_PEER_NODE(hook)))
			NG_NODE_REF(node);	/* XXX RACE */
		NG_NODE_UNREF(oldnode);	/* XXX another race */
		if (NG_NODE_NOT_VALID(node)) {
			NG_NODE_UNREF(node);	/* XXX more races */
			TOPOLOGY_WUNLOCK();
			TRAP_ERROR();
			return (ENXIO);
		}

		if (*path == '\0') {
			if (lasthook != NULL) {
				if (hook != NULL) {
					*lasthook = NG_HOOK_PEER(hook);
					NG_HOOK_REF(*lasthook);
				} else
					*lasthook = NULL;
			}
			TOPOLOGY_WUNLOCK();
			*destp = node;
			return (0);
		}
		TOPOLOGY_WUNLOCK();
	}
}

/***************************************************************\
* Input queue handling.
* All activities are submitted to the node via the input queue
* which implements a multiple-reader/single-writer gate.
* Items which cannot be handled immediately are queued.
*
* read-write queue locking inline functions			*
\***************************************************************/

static __inline void	ng_queue_rw(node_p node, item_p  item, int rw);
static __inline item_p	ng_dequeue(node_p node, int *rw);
static __inline item_p	ng_acquire_read(node_p node, item_p  item);
static __inline item_p	ng_acquire_write(node_p node, item_p  item);
static __inline void	ng_leave_read(node_p node);
static __inline void	ng_leave_write(node_p node);

/*
 * Definition of the bits fields in the ng_queue flag word.
 * Defined here rather than in netgraph.h because no-one should fiddle
 * with them.
 *
 * The ordering here may be important! don't shuffle these.
 */
/*-
 Safety Barrier--------+ (adjustable to suit taste) (not used yet)
                       |
                       V
+-------+-------+-------+-------+-------+-------+-------+-------+
  | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
  | |A|c|t|i|v|e| |R|e|a|d|e|r| |C|o|u|n|t| | | | | | | | | |P|A|
  | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |O|W|
+-------+-------+-------+-------+-------+-------+-------+-------+
  \___________________________ ____________________________/ | |
                            V                                | |
                  [active reader count]                      | |
                                                             | |
            Operation Pending -------------------------------+ |
                                                               |
          Active Writer ---------------------------------------+

Node queue has such semantics:
- All flags modifications are atomic.
- Reader count can be incremented only if there is no writer or pending flags.
  As soon as this can't be done with single operation, it is implemented with
  spin loop and atomic_cmpset().
- Writer flag can be set only if there is no any bits set.
  It is implemented with atomic_cmpset().
- Pending flag can be set any time, but to avoid collision on queue processing
  all queue fields are protected by the mutex.
- Queue processing thread reads queue holding the mutex, but releases it while
  processing. When queue is empty pending flag is removed.
*/

#define WRITER_ACTIVE	0x00000001
#define OP_PENDING	0x00000002
#define READER_INCREMENT 0x00000004
#define READER_MASK	0xfffffffc	/* Not valid if WRITER_ACTIVE is set */
#define SAFETY_BARRIER	0x00100000	/* 128K items queued should be enough */

/* Defines of more elaborate states on the queue */
/* Mask of bits a new read cares about */
#define NGQ_RMASK	(WRITER_ACTIVE|OP_PENDING)

/* Mask of bits a new write cares about */
#define NGQ_WMASK	(NGQ_RMASK|READER_MASK)

/* Test to decide if there is something on the queue. */
#define QUEUE_ACTIVE(QP) ((QP)->q_flags & OP_PENDING)

/* How to decide what the next queued item is. */
#define HEAD_IS_READER(QP)  NGI_QUEUED_READER(STAILQ_FIRST(&(QP)->queue))
#define HEAD_IS_WRITER(QP)  NGI_QUEUED_WRITER(STAILQ_FIRST(&(QP)->queue)) /* notused */

/* Read the status to decide if the next item on the queue can now run. */
#define QUEUED_READER_CAN_PROCEED(QP)			\
		(((QP)->q_flags & (NGQ_RMASK & ~OP_PENDING)) == 0)
#define QUEUED_WRITER_CAN_PROCEED(QP)			\
		(((QP)->q_flags & (NGQ_WMASK & ~OP_PENDING)) == 0)

/* Is there a chance of getting ANY work off the queue? */
#define NEXT_QUEUED_ITEM_CAN_PROCEED(QP)				\
	((HEAD_IS_READER(QP)) ? QUEUED_READER_CAN_PROCEED(QP) :		\
				QUEUED_WRITER_CAN_PROCEED(QP))

#define NGQRW_R 0
#define NGQRW_W 1

#define NGQ2_WORKQ	0x00000001

/*
 * Taking into account the current state of the queue and node, possibly take
 * the next entry off the queue and return it. Return NULL if there was
 * nothing we could return, either because there really was nothing there, or
 * because the node was in a state where it cannot yet process the next item
 * on the queue.
 */
static __inline item_p
ng_dequeue(node_p node, int *rw)
{
	item_p item;
	struct ng_queue *ngq = &node->nd_input_queue;

	/* This MUST be called with the mutex held. */
	mtx_assert(&ngq->q_mtx, MA_OWNED);

	/* If there is nothing queued, then just return. */
	if (!QUEUE_ACTIVE(ngq)) {
		CTR4(KTR_NET, "%20s: node [%x] (%p) queue empty; "
		    "queue flags 0x%lx", __func__,
		    node->nd_ID, node, ngq->q_flags);
		return (NULL);
	}

	/*
	 * From here, we can assume there is a head item.
	 * We need to find out what it is and if it can be dequeued, given
	 * the current state of the node.
	 */
	if (HEAD_IS_READER(ngq)) {
		while (1) {
			long t = ngq->q_flags;
			if (t & WRITER_ACTIVE) {
				/* There is writer, reader can't proceed. */
				CTR4(KTR_NET, "%20s: node [%x] (%p) queued "
				    "reader can't proceed; queue flags 0x%lx",
				    __func__, node->nd_ID, node, t);
				return (NULL);
			}
			if (atomic_cmpset_acq_int(&ngq->q_flags, t,
			    t + READER_INCREMENT))
				break;
			cpu_spinwait();
		}
		/* We have got reader lock for the node. */
		*rw = NGQRW_R;
	} else if (atomic_cmpset_acq_int(&ngq->q_flags, OP_PENDING,
	    OP_PENDING + WRITER_ACTIVE)) {
		/* We have got writer lock for the node. */
		*rw = NGQRW_W;
	} else {
		/* There is somebody other, writer can't proceed. */
		CTR4(KTR_NET, "%20s: node [%x] (%p) queued writer can't "
		    "proceed; queue flags 0x%lx", __func__, node->nd_ID, node,
		    ngq->q_flags);
		return (NULL);
	}

	/*
	 * Now we dequeue the request (whatever it may be) and correct the
	 * pending flags and the next and last pointers.
	 */
	item = STAILQ_FIRST(&ngq->queue);
	STAILQ_REMOVE_HEAD(&ngq->queue, el_next);
	if (STAILQ_EMPTY(&ngq->queue))
		atomic_clear_int(&ngq->q_flags, OP_PENDING);
	CTR6(KTR_NET, "%20s: node [%x] (%p) returning item %p as %s; queue "
	    "flags 0x%lx", __func__, node->nd_ID, node, item, *rw ? "WRITER" :
	    "READER", ngq->q_flags);
	return (item);
}

/*
 * Queue a packet to be picked up later by someone else.
 * If the queue could be run now, add node to the queue handler's worklist.
 */
static __inline void
ng_queue_rw(node_p node, item_p  item, int rw)
{
	struct ng_queue *ngq = &node->nd_input_queue;
	if (rw == NGQRW_W)
		NGI_SET_WRITER(item);
	else
		NGI_SET_READER(item);
	item->depth = 1;

	NG_QUEUE_LOCK(ngq);
	/* Set OP_PENDING flag and enqueue the item. */
	atomic_set_int(&ngq->q_flags, OP_PENDING);
	STAILQ_INSERT_TAIL(&ngq->queue, item, el_next);

	CTR5(KTR_NET, "%20s: node [%x] (%p) queued item %p as %s", __func__,
	    node->nd_ID, node, item, rw ? "WRITER" : "READER" );

	/*
	 * We can take the worklist lock with the node locked
	 * BUT NOT THE REVERSE!
	 */
	if (NEXT_QUEUED_ITEM_CAN_PROCEED(ngq))
		ng_worklist_add(node);
	NG_QUEUE_UNLOCK(ngq);
}

/* Acquire reader lock on node. If node is busy, queue the packet. */
static __inline item_p
ng_acquire_read(node_p node, item_p item)
{
	KASSERT(node != &ng_deadnode,
	    ("%s: working on deadnode", __func__));

	/* Reader needs node without writer and pending items. */
	for (;;) {
		long t = node->nd_input_queue.q_flags;
		if (t & NGQ_RMASK)
			break; /* Node is not ready for reader. */
		if (atomic_cmpset_acq_int(&node->nd_input_queue.q_flags, t,
		    t + READER_INCREMENT)) {
	    		/* Successfully grabbed node */
			CTR4(KTR_NET, "%20s: node [%x] (%p) acquired item %p",
			    __func__, node->nd_ID, node, item);
			return (item);
		}
		cpu_spinwait();
	}

	/* Queue the request for later. */
	ng_queue_rw(node, item, NGQRW_R);

	return (NULL);
}

/* Acquire writer lock on node. If node is busy, queue the packet. */
static __inline item_p
ng_acquire_write(node_p node, item_p item)
{
	KASSERT(node != &ng_deadnode,
	    ("%s: working on deadnode", __func__));

	/* Writer needs completely idle node. */
	if (atomic_cmpset_acq_int(&node->nd_input_queue.q_flags, 0,
	    WRITER_ACTIVE)) {
	    	/* Successfully grabbed node */
		CTR4(KTR_NET, "%20s: node [%x] (%p) acquired item %p",
		    __func__, node->nd_ID, node, item);
		return (item);
	}

	/* Queue the request for later. */
	ng_queue_rw(node, item, NGQRW_W);

	return (NULL);
}

#if 0
static __inline item_p
ng_upgrade_write(node_p node, item_p item)
{
	struct ng_queue *ngq = &node->nd_input_queue;
	KASSERT(node != &ng_deadnode,
	    ("%s: working on deadnode", __func__));

	NGI_SET_WRITER(item);

	NG_QUEUE_LOCK(ngq);

	/*
	 * There will never be no readers as we are there ourselves.
	 * Set the WRITER_ACTIVE flags ASAP to block out fast track readers.
	 * The caller we are running from will call ng_leave_read()
	 * soon, so we must account for that. We must leave again with the
	 * READER lock. If we find other readers, then
	 * queue the request for later. However "later" may be rignt now
	 * if there are no readers. We don't really care if there are queued
	 * items as we will bypass them anyhow.
	 */
	atomic_add_int(&ngq->q_flags, WRITER_ACTIVE - READER_INCREMENT);
	if ((ngq->q_flags & (NGQ_WMASK & ~OP_PENDING)) == WRITER_ACTIVE) {
		NG_QUEUE_UNLOCK(ngq);
		
		/* It's just us, act on the item. */
		/* will NOT drop writer lock when done */
		ng_apply_item(node, item, 0);

		/*
		 * Having acted on the item, atomically
		 * downgrade back to READER and finish up.
	 	 */
		atomic_add_int(&ngq->q_flags, READER_INCREMENT - WRITER_ACTIVE);

		/* Our caller will call ng_leave_read() */
		return;
	}
	/*
	 * It's not just us active, so queue us AT THE HEAD.
	 * "Why?" I hear you ask.
	 * Put us at the head of the queue as we've already been
	 * through it once. If there is nothing else waiting,
	 * set the correct flags.
	 */
	if (STAILQ_EMPTY(&ngq->queue)) {
		/* We've gone from, 0 to 1 item in the queue */
		atomic_set_int(&ngq->q_flags, OP_PENDING);

		CTR3(KTR_NET, "%20s: node [%x] (%p) set OP_PENDING", __func__,
		    node->nd_ID, node);
	};
	STAILQ_INSERT_HEAD(&ngq->queue, item, el_next);
	CTR4(KTR_NET, "%20s: node [%x] (%p) requeued item %p as WRITER",
	    __func__, node->nd_ID, node, item );

	/* Reverse what we did above. That downgrades us back to reader */
	atomic_add_int(&ngq->q_flags, READER_INCREMENT - WRITER_ACTIVE);
	if (QUEUE_ACTIVE(ngq) && NEXT_QUEUED_ITEM_CAN_PROCEED(ngq))
		ng_worklist_add(node);
	NG_QUEUE_UNLOCK(ngq);

	return;
}
#endif

/* Release reader lock. */
static __inline void
ng_leave_read(node_p node)
{
	atomic_subtract_rel_int(&node->nd_input_queue.q_flags, READER_INCREMENT);
}

/* Release writer lock. */
static __inline void
ng_leave_write(node_p node)
{
	atomic_clear_rel_int(&node->nd_input_queue.q_flags, WRITER_ACTIVE);
}

/* Purge node queue. Called on node shutdown. */
static void
ng_flush_input_queue(node_p node)
{
	struct ng_queue *ngq = &node->nd_input_queue;
	item_p item;

	NG_QUEUE_LOCK(ngq);
	while ((item = STAILQ_FIRST(&ngq->queue)) != NULL) {
		STAILQ_REMOVE_HEAD(&ngq->queue, el_next);
		if (STAILQ_EMPTY(&ngq->queue))
			atomic_clear_int(&ngq->q_flags, OP_PENDING);
		NG_QUEUE_UNLOCK(ngq);

		/* If the item is supplying a callback, call it with an error */
		if (item->apply != NULL) {
			if (item->depth == 1)
				item->apply->error = ENOENT;
			if (refcount_release(&item->apply->refs)) {
				(*item->apply->apply)(item->apply->context,
				    item->apply->error);
			}
		}
		NG_FREE_ITEM(item);
		NG_QUEUE_LOCK(ngq);
	}
	NG_QUEUE_UNLOCK(ngq);
}

/***********************************************************************
* Externally visible method for sending or queueing messages or data.
***********************************************************************/

/*
 * The module code should have filled out the item correctly by this stage:
 * Common:
 *    reference to destination node.
 *    Reference to destination rcv hook if relevant.
 *    apply pointer must be or NULL or reference valid struct ng_apply_info.
 * Data:
 *    pointer to mbuf
 * Control_Message:
 *    pointer to msg.
 *    ID of original sender node. (return address)
 * Function:
 *    Function pointer
 *    void * argument
 *    integer argument
 *
 * The nodes have several routines and macros to help with this task:
 */

int
ng_snd_item(item_p item, int flags)
{
	hook_p hook;
	node_p node;
	int queue, rw;
	struct ng_queue *ngq;
	int error = 0;

	/* We are sending item, so it must be present! */
	KASSERT(item != NULL, ("ng_snd_item: item is NULL"));

#ifdef	NETGRAPH_DEBUG
	_ngi_check(item, __FILE__, __LINE__);
#endif

	/* Item was sent once more, postpone apply() call. */
	if (item->apply)
		refcount_acquire(&item->apply->refs);

	node = NGI_NODE(item);
	/* Node is never optional. */
	KASSERT(node != NULL, ("ng_snd_item: node is NULL"));

	hook = NGI_HOOK(item);
	/* Valid hook and mbuf are mandatory for data. */
	if ((item->el_flags & NGQF_TYPE) == NGQF_DATA) {
		KASSERT(hook != NULL, ("ng_snd_item: hook for data is NULL"));
		if (NGI_M(item) == NULL)
			ERROUT(EINVAL);
		CHECK_DATA_MBUF(NGI_M(item));
	}

	/*
	 * If the item or the node specifies single threading, force
	 * writer semantics. Similarly, the node may say one hook always
	 * produces writers. These are overrides.
	 */
	if (((item->el_flags & NGQF_RW) == NGQF_WRITER) ||
	    (node->nd_flags & NGF_FORCE_WRITER) ||
	    (hook && (hook->hk_flags & HK_FORCE_WRITER))) {
		rw = NGQRW_W;
	} else {
		rw = NGQRW_R;
	}

	/*
	 * If sender or receiver requests queued delivery, or call graph
	 * loops back from outbound to inbound path, or stack usage
	 * level is dangerous - enqueue message.
	 */
	if ((flags & NG_QUEUE) || (hook && (hook->hk_flags & HK_QUEUE))) {
		queue = 1;
	} else if (hook && (hook->hk_flags & HK_TO_INBOUND) &&
	    curthread->td_ng_outbound) {
		queue = 1;
	} else {
		queue = 0;
#ifdef GET_STACK_USAGE
		/*
		 * Most of netgraph nodes have small stack consumption and
		 * for them 25% of free stack space is more than enough.
		 * Nodes/hooks with higher stack usage should be marked as
		 * HI_STACK. For them 50% of stack will be guaranteed then.
		 * XXX: Values 25% and 50% are completely empirical.
		 */
		size_t	st, su, sl;
		GET_STACK_USAGE(st, su);
		sl = st - su;
		if ((sl * 4 < st) || ((sl * 2 < st) &&
		    ((node->nd_flags & NGF_HI_STACK) || (hook &&
		    (hook->hk_flags & HK_HI_STACK)))))
			queue = 1;
#endif
	}

	if (queue) {
		/* Put it on the queue for that node*/
		ng_queue_rw(node, item, rw);
		return ((flags & NG_PROGRESS) ? EINPROGRESS : 0);
	}

	/*
	 * We already decided how we will be queueud or treated.
	 * Try get the appropriate operating permission.
	 */
 	if (rw == NGQRW_R)
		item = ng_acquire_read(node, item);
	else
		item = ng_acquire_write(node, item);

	/* Item was queued while trying to get permission. */
	if (item == NULL)
		return ((flags & NG_PROGRESS) ? EINPROGRESS : 0);

	NGI_GET_NODE(item, node); /* zaps stored node */

	item->depth++;
	error = ng_apply_item(node, item, rw); /* drops r/w lock when done */

	/* If something is waiting on queue and ready, schedule it. */
	ngq = &node->nd_input_queue;
	if (QUEUE_ACTIVE(ngq)) {
		NG_QUEUE_LOCK(ngq);
		if (QUEUE_ACTIVE(ngq) && NEXT_QUEUED_ITEM_CAN_PROCEED(ngq))
			ng_worklist_add(node);
		NG_QUEUE_UNLOCK(ngq);
	}

	/*
	 * Node may go away as soon as we remove the reference.
	 * Whatever we do, DO NOT access the node again!
	 */
	NG_NODE_UNREF(node);

	return (error);

done:
	/* If was not sent, apply callback here. */
	if (item->apply != NULL) {
		if (item->depth == 0 && error != 0)
			item->apply->error = error;
		if (refcount_release(&item->apply->refs)) {
			(*item->apply->apply)(item->apply->context,
			    item->apply->error);
		}
	}

	NG_FREE_ITEM(item);
	return (error);
}

/*
 * We have an item that was possibly queued somewhere.
 * It should contain all the information needed
 * to run it on the appropriate node/hook.
 * If there is apply pointer and we own the last reference, call apply().
 */
static int
ng_apply_item(node_p node, item_p item, int rw)
{
	hook_p  hook;
	ng_rcvdata_t *rcvdata;
	ng_rcvmsg_t *rcvmsg;
	struct ng_apply_info *apply;
	int	error = 0, depth;

	/* Node and item are never optional. */
	KASSERT(node != NULL, ("ng_apply_item: node is NULL"));
	KASSERT(item != NULL, ("ng_apply_item: item is NULL"));

	NGI_GET_HOOK(item, hook); /* clears stored hook */
#ifdef	NETGRAPH_DEBUG
	_ngi_check(item, __FILE__, __LINE__);
#endif

	apply = item->apply;
	depth = item->depth;

	switch (item->el_flags & NGQF_TYPE) {
	case NGQF_DATA:
		/*
		 * Check things are still ok as when we were queued.
		 */
		KASSERT(hook != NULL, ("ng_apply_item: hook for data is NULL"));
		if (NG_HOOK_NOT_VALID(hook) ||
		    NG_NODE_NOT_VALID(node)) {
			error = EIO;
			NG_FREE_ITEM(item);
			break;
		}
		/*
		 * If no receive method, just silently drop it.
		 * Give preference to the hook over-ride method.
		 */
		if ((!(rcvdata = hook->hk_rcvdata)) &&
		    (!(rcvdata = NG_HOOK_NODE(hook)->nd_type->rcvdata))) {
			error = 0;
			NG_FREE_ITEM(item);
			break;
		}
		error = (*rcvdata)(hook, item);
		break;
	case NGQF_MESG:
		if (hook && NG_HOOK_NOT_VALID(hook)) {
			/*
			 * The hook has been zapped then we can't use it.
			 * Immediately drop its reference.
			 * The message may not need it.
			 */
			NG_HOOK_UNREF(hook);
			hook = NULL;
		}
		/*
		 * Similarly, if the node is a zombie there is
		 * nothing we can do with it, drop everything.
		 */
		if (NG_NODE_NOT_VALID(node)) {
			TRAP_ERROR();
			error = EINVAL;
			NG_FREE_ITEM(item);
			break;
		}
		/*
		 * Call the appropriate message handler for the object.
		 * It is up to the message handler to free the message.
		 * If it's a generic message, handle it generically,
		 * otherwise call the type's message handler (if it exists).
		 * XXX (race). Remember that a queued message may
		 * reference a node or hook that has just been
		 * invalidated. It will exist as the queue code
		 * is holding a reference, but..
		 */
		if ((NGI_MSG(item)->header.typecookie == NGM_GENERIC_COOKIE) &&
		    ((NGI_MSG(item)->header.flags & NGF_RESP) == 0)) {
			error = ng_generic_msg(node, item, hook);
			break;
		}
		if (((!hook) || (!(rcvmsg = hook->hk_rcvmsg))) &&
		    (!(rcvmsg = node->nd_type->rcvmsg))) {
			TRAP_ERROR();
			error = 0;
			NG_FREE_ITEM(item);
			break;
		}
		error = (*rcvmsg)(node, item, hook);
		break;
	case NGQF_FN:
	case NGQF_FN2:
		/*
		 * In the case of the shutdown message we allow it to hit
		 * even if the node is invalid.
		 */
		if (NG_NODE_NOT_VALID(node) &&
		    NGI_FN(item) != &ng_rmnode) {
			TRAP_ERROR();
			error = EINVAL;
			NG_FREE_ITEM(item);
			break;
		}
		/* Same is about some internal functions and invalid hook. */
		if (hook && NG_HOOK_NOT_VALID(hook) &&
		    NGI_FN2(item) != &ng_con_part2 &&
		    NGI_FN2(item) != &ng_con_part3 &&
		    NGI_FN(item) != &ng_rmhook_part2) {
			TRAP_ERROR();
			error = EINVAL;
			NG_FREE_ITEM(item);
			break;
		}
		
		if ((item->el_flags & NGQF_TYPE) == NGQF_FN) {
			(*NGI_FN(item))(node, hook, NGI_ARG1(item),
			    NGI_ARG2(item));
			NG_FREE_ITEM(item);
		} else	/* it is NGQF_FN2 */
			error = (*NGI_FN2(item))(node, item, hook);
		break;
	}
	/*
	 * We held references on some of the resources
	 * that we took from the item. Now that we have
	 * finished doing everything, drop those references.
	 */
	if (hook)
		NG_HOOK_UNREF(hook);

 	if (rw == NGQRW_R)
		ng_leave_read(node);
	else
		ng_leave_write(node);

	/* Apply callback. */
	if (apply != NULL) {
		if (depth == 1 && error != 0)
			apply->error = error;
		if (refcount_release(&apply->refs))
			(*apply->apply)(apply->context, apply->error);
	}

	return (error);
}

/***********************************************************************
 * Implement the 'generic' control messages
 ***********************************************************************/
static int
ng_generic_msg(node_p here, item_p item, hook_p lasthook)
{
	int error = 0;
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;

	NGI_GET_MSG(item, msg);
	if (msg->header.typecookie != NGM_GENERIC_COOKIE) {
		TRAP_ERROR();
		error = EINVAL;
		goto out;
	}
	switch (msg->header.cmd) {
	case NGM_SHUTDOWN:
		ng_rmnode(here, NULL, NULL, 0);
		break;
	case NGM_MKPEER:
	    {
		struct ngm_mkpeer *const mkp = (struct ngm_mkpeer *) msg->data;

		if (msg->header.arglen != sizeof(*mkp)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}
		mkp->type[sizeof(mkp->type) - 1] = '\0';
		mkp->ourhook[sizeof(mkp->ourhook) - 1] = '\0';
		mkp->peerhook[sizeof(mkp->peerhook) - 1] = '\0';
		error = ng_mkpeer(here, mkp->ourhook, mkp->peerhook, mkp->type);
		break;
	    }
	case NGM_CONNECT:
	    {
		struct ngm_connect *const con =
			(struct ngm_connect *) msg->data;
		node_p node2;

		if (msg->header.arglen != sizeof(*con)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}
		con->path[sizeof(con->path) - 1] = '\0';
		con->ourhook[sizeof(con->ourhook) - 1] = '\0';
		con->peerhook[sizeof(con->peerhook) - 1] = '\0';
		/* Don't forget we get a reference.. */
		error = ng_path2noderef(here, con->path, &node2, NULL);
		if (error)
			break;
		error = ng_con_nodes(item, here, con->ourhook,
		    node2, con->peerhook);
		NG_NODE_UNREF(node2);
		break;
	    }
	case NGM_NAME:
	    {
		struct ngm_name *const nam = (struct ngm_name *) msg->data;

		if (msg->header.arglen != sizeof(*nam)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}
		nam->name[sizeof(nam->name) - 1] = '\0';
		error = ng_name_node(here, nam->name);
		break;
	    }
	case NGM_RMHOOK:
	    {
		struct ngm_rmhook *const rmh = (struct ngm_rmhook *) msg->data;
		hook_p hook;

		if (msg->header.arglen != sizeof(*rmh)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}
		rmh->ourhook[sizeof(rmh->ourhook) - 1] = '\0';
		if ((hook = ng_findhook(here, rmh->ourhook)) != NULL)
			ng_destroy_hook(hook);
		break;
	    }
	case NGM_NODEINFO:
	    {
		struct nodeinfo *ni;

		NG_MKRESPONSE(resp, msg, sizeof(*ni), M_NOWAIT);
		if (resp == NULL) {
			error = ENOMEM;
			break;
		}

		/* Fill in node info */
		ni = (struct nodeinfo *) resp->data;
		if (NG_NODE_HAS_NAME(here))
			strcpy(ni->name, NG_NODE_NAME(here));
		strcpy(ni->type, here->nd_type->name);
		ni->id = ng_node2ID(here);
		ni->hooks = here->nd_numhooks;
		break;
	    }
	case NGM_LISTHOOKS:
	    {
		const int nhooks = here->nd_numhooks;
		struct hooklist *hl;
		struct nodeinfo *ni;
		hook_p hook;

		/* Get response struct */
		NG_MKRESPONSE(resp, msg, sizeof(*hl) +
		    (nhooks * sizeof(struct linkinfo)), M_NOWAIT);
		if (resp == NULL) {
			error = ENOMEM;
			break;
		}
		hl = (struct hooklist *) resp->data;
		ni = &hl->nodeinfo;

		/* Fill in node info */
		if (NG_NODE_HAS_NAME(here))
			strcpy(ni->name, NG_NODE_NAME(here));
		strcpy(ni->type, here->nd_type->name);
		ni->id = ng_node2ID(here);

		/* Cycle through the linked list of hooks */
		ni->hooks = 0;
		LIST_FOREACH(hook, &here->nd_hooks, hk_hooks) {
			struct linkinfo *const link = &hl->link[ni->hooks];

			if (ni->hooks >= nhooks) {
				log(LOG_ERR, "%s: number of %s changed\n",
				    __func__, "hooks");
				break;
			}
			if (NG_HOOK_NOT_VALID(hook))
				continue;
			strcpy(link->ourhook, NG_HOOK_NAME(hook));
			strcpy(link->peerhook, NG_PEER_HOOK_NAME(hook));
			if (NG_PEER_NODE_NAME(hook)[0] != '\0')
				strcpy(link->nodeinfo.name,
				    NG_PEER_NODE_NAME(hook));
			strcpy(link->nodeinfo.type,
			   NG_PEER_NODE(hook)->nd_type->name);
			link->nodeinfo.id = ng_node2ID(NG_PEER_NODE(hook));
			link->nodeinfo.hooks = NG_PEER_NODE(hook)->nd_numhooks;
			ni->hooks++;
		}
		break;
	    }

	case NGM_LISTNODES:
	    {
		struct namelist *nl;
		node_p node;
		int i;

		IDHASH_RLOCK();
		/* Get response struct. */
		NG_MKRESPONSE(resp, msg, sizeof(*nl) +
		    (V_ng_nodes * sizeof(struct nodeinfo)), M_NOWAIT);
		if (resp == NULL) {
			IDHASH_RUNLOCK();
			error = ENOMEM;
			break;
		}
		nl = (struct namelist *) resp->data;

		/* Cycle through the lists of nodes. */
		nl->numnames = 0;
		for (i = 0; i <= V_ng_ID_hmask; i++) {
			LIST_FOREACH(node, &V_ng_ID_hash[i], nd_idnodes) {
				struct nodeinfo *const np =
				    &nl->nodeinfo[nl->numnames];

				if (NG_NODE_NOT_VALID(node))
					continue;
				if (NG_NODE_HAS_NAME(node))
					strcpy(np->name, NG_NODE_NAME(node));
				strcpy(np->type, node->nd_type->name);
				np->id = ng_node2ID(node);
				np->hooks = node->nd_numhooks;
				KASSERT(nl->numnames < V_ng_nodes,
				    ("%s: no space", __func__));
				nl->numnames++;
			}
		}
		IDHASH_RUNLOCK();
		break;
	    }
	case NGM_LISTNAMES:
	    {
		struct namelist *nl;
		node_p node;
		int i;

		NAMEHASH_RLOCK();
		/* Get response struct. */
		NG_MKRESPONSE(resp, msg, sizeof(*nl) +
		    (V_ng_named_nodes * sizeof(struct nodeinfo)), M_NOWAIT);
		if (resp == NULL) {
			NAMEHASH_RUNLOCK();
			error = ENOMEM;
			break;
		}
		nl = (struct namelist *) resp->data;

		/* Cycle through the lists of nodes. */
		nl->numnames = 0;
		for (i = 0; i <= V_ng_name_hmask; i++) {
			LIST_FOREACH(node, &V_ng_name_hash[i], nd_nodes) {
				struct nodeinfo *const np =
				    &nl->nodeinfo[nl->numnames];

				if (NG_NODE_NOT_VALID(node))
					continue;
				strcpy(np->name, NG_NODE_NAME(node));
				strcpy(np->type, node->nd_type->name);
				np->id = ng_node2ID(node);
				np->hooks = node->nd_numhooks;
				KASSERT(nl->numnames < V_ng_named_nodes,
				    ("%s: no space", __func__));
				nl->numnames++;
			}
		}
		NAMEHASH_RUNLOCK();
		break;
	    }

	case NGM_LISTTYPES:
	    {
		struct typelist *tl;
		struct ng_type *type;
		int num = 0;

		TYPELIST_RLOCK();
		/* Count number of types */
		LIST_FOREACH(type, &ng_typelist, types)
			num++;

		/* Get response struct */
		NG_MKRESPONSE(resp, msg, sizeof(*tl) +
		    (num * sizeof(struct typeinfo)), M_NOWAIT);
		if (resp == NULL) {
			TYPELIST_RUNLOCK();
			error = ENOMEM;
			break;
		}
		tl = (struct typelist *) resp->data;

		/* Cycle through the linked list of types */
		tl->numtypes = 0;
		LIST_FOREACH(type, &ng_typelist, types) {
			struct typeinfo *const tp = &tl->typeinfo[tl->numtypes];

			strcpy(tp->type_name, type->name);
			tp->numnodes = type->refs - 1; /* don't count list */
			KASSERT(tl->numtypes < num, ("%s: no space", __func__));
			tl->numtypes++;
		}
		TYPELIST_RUNLOCK();
		break;
	    }

	case NGM_BINARY2ASCII:
	    {
		int bufSize = 20 * 1024;	/* XXX hard coded constant */
		const struct ng_parse_type *argstype;
		const struct ng_cmdlist *c;
		struct ng_mesg *binary, *ascii;

		/* Data area must contain a valid netgraph message */
		binary = (struct ng_mesg *)msg->data;
		if (msg->header.arglen < sizeof(struct ng_mesg) ||
		    (msg->header.arglen - sizeof(struct ng_mesg) <
		    binary->header.arglen)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}

		/* Get a response message with lots of room */
		NG_MKRESPONSE(resp, msg, sizeof(*ascii) + bufSize, M_NOWAIT);
		if (resp == NULL) {
			error = ENOMEM;
			break;
		}
		ascii = (struct ng_mesg *)resp->data;

		/* Copy binary message header to response message payload */
		bcopy(binary, ascii, sizeof(*binary));

		/* Find command by matching typecookie and command number */
		for (c = here->nd_type->cmdlist; c != NULL && c->name != NULL;
		    c++) {
			if (binary->header.typecookie == c->cookie &&
			    binary->header.cmd == c->cmd)
				break;
		}
		if (c == NULL || c->name == NULL) {
			for (c = ng_generic_cmds; c->name != NULL; c++) {
				if (binary->header.typecookie == c->cookie &&
				    binary->header.cmd == c->cmd)
					break;
			}
			if (c->name == NULL) {
				NG_FREE_MSG(resp);
				error = ENOSYS;
				break;
			}
		}

		/* Convert command name to ASCII */
		snprintf(ascii->header.cmdstr, sizeof(ascii->header.cmdstr),
		    "%s", c->name);

		/* Convert command arguments to ASCII */
		argstype = (binary->header.flags & NGF_RESP) ?
		    c->respType : c->mesgType;
		if (argstype == NULL) {
			*ascii->data = '\0';
		} else {
			if ((error = ng_unparse(argstype,
			    (u_char *)binary->data,
			    ascii->data, bufSize)) != 0) {
				NG_FREE_MSG(resp);
				break;
			}
		}

		/* Return the result as struct ng_mesg plus ASCII string */
		bufSize = strlen(ascii->data) + 1;
		ascii->header.arglen = bufSize;
		resp->header.arglen = sizeof(*ascii) + bufSize;
		break;
	    }

	case NGM_ASCII2BINARY:
	    {
		int bufSize = 20 * 1024;	/* XXX hard coded constant */
		const struct ng_cmdlist *c;
		const struct ng_parse_type *argstype;
		struct ng_mesg *ascii, *binary;
		int off = 0;

		/* Data area must contain at least a struct ng_mesg + '\0' */
		ascii = (struct ng_mesg *)msg->data;
		if ((msg->header.arglen < sizeof(*ascii) + 1) ||
		    (ascii->header.arglen < 1) ||
		    (msg->header.arglen < sizeof(*ascii) +
		    ascii->header.arglen)) {
			TRAP_ERROR();
			error = EINVAL;
			break;
		}
		ascii->data[ascii->header.arglen - 1] = '\0';

		/* Get a response message with lots of room */
		NG_MKRESPONSE(resp, msg, sizeof(*binary) + bufSize, M_NOWAIT);
		if (resp == NULL) {
			error = ENOMEM;
			break;
		}
		binary = (struct ng_mesg *)resp->data;

		/* Copy ASCII message header to response message payload */
		bcopy(ascii, binary, sizeof(*ascii));

		/* Find command by matching ASCII command string */
		for (c = here->nd_type->cmdlist;
		    c != NULL && c->name != NULL; c++) {
			if (strcmp(ascii->header.cmdstr, c->name) == 0)
				break;
		}
		if (c == NULL || c->name == NULL) {
			for (c = ng_generic_cmds; c->name != NULL; c++) {
				if (strcmp(ascii->header.cmdstr, c->name) == 0)
					break;
			}
			if (c->name == NULL) {
				NG_FREE_MSG(resp);
				error = ENOSYS;
				break;
			}
		}

		/* Convert command name to binary */
		binary->header.cmd = c->cmd;
		binary->header.typecookie = c->cookie;

		/* Convert command arguments to binary */
		argstype = (binary->header.flags & NGF_RESP) ?
		    c->respType : c->mesgType;
		if (argstype == NULL) {
			bufSize = 0;
		} else {
			if ((error = ng_parse(argstype, ascii->data, &off,
			    (u_char *)binary->data, &bufSize)) != 0) {
				NG_FREE_MSG(resp);
				break;
			}
		}

		/* Return the result */
		binary->header.arglen = bufSize;
		resp->header.arglen = sizeof(*binary) + bufSize;
		break;
	    }

	case NGM_TEXT_CONFIG:
	case NGM_TEXT_STATUS:
		/*
		 * This one is tricky as it passes the command down to the
		 * actual node, even though it is a generic type command.
		 * This means we must assume that the item/msg is already freed
		 * when control passes back to us.
		 */
		if (here->nd_type->rcvmsg != NULL) {
			NGI_MSG(item) = msg; /* put it back as we found it */
			return((*here->nd_type->rcvmsg)(here, item, lasthook));
		}
		/* Fall through if rcvmsg not supported */
	default:
		TRAP_ERROR();
		error = EINVAL;
	}
	/*
	 * Sometimes a generic message may be statically allocated
	 * to avoid problems with allocating when in tight memory situations.
	 * Don't free it if it is so.
	 * I break them apart here, because erros may cause a free if the item
	 * in which case we'd be doing it twice.
	 * they are kept together above, to simplify freeing.
	 */
out:
	NG_RESPOND_MSG(error, here, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/************************************************************************
			Queue element get/free routines
************************************************************************/

uma_zone_t			ng_qzone;
uma_zone_t			ng_qdzone;
static int			numthreads = 0; /* number of queue threads */
static int			maxalloc = 4096;/* limit the damage of a leak */
static int			maxdata = 4096;	/* limit the damage of a DoS */

SYSCTL_INT(_net_graph, OID_AUTO, threads, CTLFLAG_RDTUN, &numthreads,
    0, "Number of queue processing threads");
SYSCTL_INT(_net_graph, OID_AUTO, maxalloc, CTLFLAG_RDTUN, &maxalloc,
    0, "Maximum number of non-data queue items to allocate");
SYSCTL_INT(_net_graph, OID_AUTO, maxdata, CTLFLAG_RDTUN, &maxdata,
    0, "Maximum number of data queue items to allocate");

#ifdef	NETGRAPH_DEBUG
static TAILQ_HEAD(, ng_item) ng_itemlist = TAILQ_HEAD_INITIALIZER(ng_itemlist);
static int allocated;	/* number of items malloc'd */
#endif

/*
 * Get a queue entry.
 * This is usually called when a packet first enters netgraph.
 * By definition, this is usually from an interrupt, or from a user.
 * Users are not so important, but try be quick for the times that it's
 * an interrupt.
 */
static __inline item_p
ng_alloc_item(int type, int flags)
{
	item_p item;

	KASSERT(((type & ~NGQF_TYPE) == 0),
	    ("%s: incorrect item type: %d", __func__, type));

	item = uma_zalloc((type == NGQF_DATA) ? ng_qdzone : ng_qzone,
	    ((flags & NG_WAITOK) ? M_WAITOK : M_NOWAIT) | M_ZERO);

	if (item) {
		item->el_flags = type;
#ifdef	NETGRAPH_DEBUG
		mtx_lock(&ngq_mtx);
		TAILQ_INSERT_TAIL(&ng_itemlist, item, all);
		allocated++;
		mtx_unlock(&ngq_mtx);
#endif
	}

	return (item);
}

/*
 * Release a queue entry
 */
void
ng_free_item(item_p item)
{
	/*
	 * The item may hold resources on its own. We need to free
	 * these before we can free the item. What they are depends upon
	 * what kind of item it is. it is important that nodes zero
	 * out pointers to resources that they remove from the item
	 * or we release them again here.
	 */
	switch (item->el_flags & NGQF_TYPE) {
	case NGQF_DATA:
		/* If we have an mbuf still attached.. */
		NG_FREE_M(_NGI_M(item));
		break;
	case NGQF_MESG:
		_NGI_RETADDR(item) = 0;
		NG_FREE_MSG(_NGI_MSG(item));
		break;
	case NGQF_FN:
	case NGQF_FN2:
		/* nothing to free really, */
		_NGI_FN(item) = NULL;
		_NGI_ARG1(item) = NULL;
		_NGI_ARG2(item) = 0;
		break;
	}
	/* If we still have a node or hook referenced... */
	_NGI_CLR_NODE(item);
	_NGI_CLR_HOOK(item);

#ifdef	NETGRAPH_DEBUG
	mtx_lock(&ngq_mtx);
	TAILQ_REMOVE(&ng_itemlist, item, all);
	allocated--;
	mtx_unlock(&ngq_mtx);
#endif
	uma_zfree(((item->el_flags & NGQF_TYPE) == NGQF_DATA) ?
	    ng_qdzone : ng_qzone, item);
}

/*
 * Change type of the queue entry.
 * Possibly reallocates it from another UMA zone.
 */
static __inline item_p
ng_realloc_item(item_p pitem, int type, int flags)
{
	item_p item;
	int from, to;

	KASSERT((pitem != NULL), ("%s: can't reallocate NULL", __func__));
	KASSERT(((type & ~NGQF_TYPE) == 0),
	    ("%s: incorrect item type: %d", __func__, type));

	from = ((pitem->el_flags & NGQF_TYPE) == NGQF_DATA);
	to = (type == NGQF_DATA);
	if (from != to) {
		/* If reallocation is required do it and copy item. */
		if ((item = ng_alloc_item(type, flags)) == NULL) {
			ng_free_item(pitem);
			return (NULL);
		}
		*item = *pitem;
		ng_free_item(pitem);
	} else
		item = pitem;
	item->el_flags = (item->el_flags & ~NGQF_TYPE) | type;

	return (item);
}

/************************************************************************
			Module routines
************************************************************************/

/*
 * Handle the loading/unloading of a netgraph node type module
 */
int
ng_mod_event(module_t mod, int event, void *data)
{
	struct ng_type *const type = data;
	int error = 0;

	switch (event) {
	case MOD_LOAD:

		/* Register new netgraph node type */
		if ((error = ng_newtype(type)) != 0)
			break;

		/* Call type specific code */
		if (type->mod_event != NULL)
			if ((error = (*type->mod_event)(mod, event, data))) {
				TYPELIST_WLOCK();
				type->refs--;	/* undo it */
				LIST_REMOVE(type, types);
				TYPELIST_WUNLOCK();
			}
		break;

	case MOD_UNLOAD:
		if (type->refs > 1) {		/* make sure no nodes exist! */
			error = EBUSY;
		} else {
			if (type->refs == 0) /* failed load, nothing to undo */
				break;
			if (type->mod_event != NULL) {	/* check with type */
				error = (*type->mod_event)(mod, event, data);
				if (error != 0)	/* type refuses.. */
					break;
			}
			TYPELIST_WLOCK();
			LIST_REMOVE(type, types);
			TYPELIST_WUNLOCK();
		}
		break;

	default:
		if (type->mod_event != NULL)
			error = (*type->mod_event)(mod, event, data);
		else
			error = EOPNOTSUPP;		/* XXX ? */
		break;
	}
	return (error);
}

static void
vnet_netgraph_init(const void *unused __unused)
{

	/* We start with small hashes, but they can grow. */
	V_ng_ID_hash = hashinit(16, M_NETGRAPH_NODE, &V_ng_ID_hmask);
	V_ng_name_hash = hashinit(16, M_NETGRAPH_NODE, &V_ng_name_hmask);
}
VNET_SYSINIT(vnet_netgraph_init, SI_SUB_NETGRAPH, SI_ORDER_FIRST,
    vnet_netgraph_init, NULL);

#ifdef VIMAGE
static void
vnet_netgraph_uninit(const void *unused __unused)
{
	node_p node = NULL, last_killed = NULL;
	int i;

	do {
		/* Find a node to kill */
		IDHASH_RLOCK();
		for (i = 0; i <= V_ng_ID_hmask; i++) {
			LIST_FOREACH(node, &V_ng_ID_hash[i], nd_idnodes) {
				if (node != &ng_deadnode) {
					NG_NODE_REF(node);
					break;
				}
			}
			if (node != NULL)
				break;
		}
		IDHASH_RUNLOCK();

		/* Attempt to kill it only if it is a regular node */
		if (node != NULL) {
			if (node == last_killed) {
				/* This should never happen */
				printf("ng node %s needs NGF_REALLY_DIE\n",
				    node->nd_name);
				if (node->nd_flags & NGF_REALLY_DIE)
					panic("ng node %s won't die",
					    node->nd_name);
				node->nd_flags |= NGF_REALLY_DIE;
			}
			ng_rmnode(node, NULL, NULL, 0);
			NG_NODE_UNREF(node);
			last_killed = node;
		}
	} while (node != NULL);

	hashdestroy(V_ng_name_hash, M_NETGRAPH_NODE, V_ng_name_hmask);
	hashdestroy(V_ng_ID_hash, M_NETGRAPH_NODE, V_ng_ID_hmask);
}
VNET_SYSUNINIT(vnet_netgraph_uninit, SI_SUB_NETGRAPH, SI_ORDER_FIRST,
    vnet_netgraph_uninit, NULL);
#endif /* VIMAGE */

/*
 * Handle loading and unloading for this code.
 * The only thing we need to link into is the NETISR strucure.
 */
static int
ngb_mod_event(module_t mod, int event, void *data)
{
	struct proc *p;
	struct thread *td;
	int i, error = 0;

	switch (event) {
	case MOD_LOAD:
		/* Initialize everything. */
		NG_WORKLIST_LOCK_INIT();
		rw_init(&ng_typelist_lock, "netgraph types");
		rw_init(&ng_idhash_lock, "netgraph idhash");
		rw_init(&ng_namehash_lock, "netgraph namehash");
		rw_init(&ng_topo_lock, "netgraph topology mutex");
#ifdef	NETGRAPH_DEBUG
		mtx_init(&ng_nodelist_mtx, "netgraph nodelist mutex", NULL,
		    MTX_DEF);
		mtx_init(&ngq_mtx, "netgraph item list mutex", NULL,
		    MTX_DEF);
#endif
		ng_qzone = uma_zcreate("NetGraph items", sizeof(struct ng_item),
		    NULL, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0);
		uma_zone_set_max(ng_qzone, maxalloc);
		ng_qdzone = uma_zcreate("NetGraph data items",
		    sizeof(struct ng_item), NULL, NULL, NULL, NULL,
		    UMA_ALIGN_CACHE, 0);
		uma_zone_set_max(ng_qdzone, maxdata);
		/* Autoconfigure number of threads. */
		if (numthreads <= 0)
			numthreads = mp_ncpus;
		/* Create threads. */
    		p = NULL; /* start with no process */
		for (i = 0; i < numthreads; i++) {
			if (kproc_kthread_add(ngthread, NULL, &p, &td,
			    RFHIGHPID, 0, "ng_queue", "ng_queue%d", i)) {
				numthreads = i;
				break;
			}
		}
		break;
	case MOD_UNLOAD:
		/* You can't unload it because an interface may be using it. */
		error = EBUSY;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t netgraph_mod = {
	"netgraph",
	ngb_mod_event,
	(NULL)
};
DECLARE_MODULE(netgraph, netgraph_mod, SI_SUB_NETGRAPH, SI_ORDER_FIRST);
SYSCTL_NODE(_net, OID_AUTO, graph, CTLFLAG_RW, 0, "netgraph Family");
SYSCTL_INT(_net_graph, OID_AUTO, abi_version, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, NG_ABI_VERSION,"");
SYSCTL_INT(_net_graph, OID_AUTO, msg_version, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, NG_VERSION, "");

#ifdef	NETGRAPH_DEBUG
void
dumphook (hook_p hook, char *file, int line)
{
	printf("hook: name %s, %d refs, Last touched:\n",
		_NG_HOOK_NAME(hook), hook->hk_refs);
	printf("	Last active @ %s, line %d\n",
		hook->lastfile, hook->lastline);
	if (line) {
		printf(" problem discovered at file %s, line %d\n", file, line);
#ifdef KDB
		kdb_backtrace();
#endif
	}
}

void
dumpnode(node_p node, char *file, int line)
{
	printf("node: ID [%x]: type '%s', %d hooks, flags 0x%x, %d refs, %s:\n",
		_NG_NODE_ID(node), node->nd_type->name,
		node->nd_numhooks, node->nd_flags,
		node->nd_refs, node->nd_name);
	printf("	Last active @ %s, line %d\n",
		node->lastfile, node->lastline);
	if (line) {
		printf(" problem discovered at file %s, line %d\n", file, line);
#ifdef KDB
		kdb_backtrace();
#endif
	}
}

void
dumpitem(item_p item, char *file, int line)
{
	printf(" ACTIVE item, last used at %s, line %d",
		item->lastfile, item->lastline);
	switch(item->el_flags & NGQF_TYPE) {
	case NGQF_DATA:
		printf(" - [data]\n");
		break;
	case NGQF_MESG:
		printf(" - retaddr[%d]:\n", _NGI_RETADDR(item));
		break;
	case NGQF_FN:
		printf(" - fn@%p (%p, %p, %p, %d (%x))\n",
			_NGI_FN(item),
			_NGI_NODE(item),
			_NGI_HOOK(item),
			item->body.fn.fn_arg1,
			item->body.fn.fn_arg2,
			item->body.fn.fn_arg2);
		break;
	case NGQF_FN2:
		printf(" - fn2@%p (%p, %p, %p, %d (%x))\n",
			_NGI_FN2(item),
			_NGI_NODE(item),
			_NGI_HOOK(item),
			item->body.fn.fn_arg1,
			item->body.fn.fn_arg2,
			item->body.fn.fn_arg2);
		break;
	}
	if (line) {
		printf(" problem discovered at file %s, line %d\n", file, line);
		if (_NGI_NODE(item)) {
			printf("node %p ([%x])\n",
				_NGI_NODE(item), ng_node2ID(_NGI_NODE(item)));
		}
	}
}

static void
ng_dumpitems(void)
{
	item_p item;
	int i = 1;
	TAILQ_FOREACH(item, &ng_itemlist, all) {
		printf("[%d] ", i++);
		dumpitem(item, NULL, 0);
	}
}

static void
ng_dumpnodes(void)
{
	node_p node;
	int i = 1;
	mtx_lock(&ng_nodelist_mtx);
	SLIST_FOREACH(node, &ng_allnodes, nd_all) {
		printf("[%d] ", i++);
		dumpnode(node, NULL, 0);
	}
	mtx_unlock(&ng_nodelist_mtx);
}

static void
ng_dumphooks(void)
{
	hook_p hook;
	int i = 1;
	mtx_lock(&ng_nodelist_mtx);
	SLIST_FOREACH(hook, &ng_allhooks, hk_all) {
		printf("[%d] ", i++);
		dumphook(hook, NULL, 0);
	}
	mtx_unlock(&ng_nodelist_mtx);
}

static int
sysctl_debug_ng_dump_items(SYSCTL_HANDLER_ARGS)
{
	int error;
	int val;
	int i;

	val = allocated;
	i = 1;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (val == 42) {
		ng_dumpitems();
		ng_dumpnodes();
		ng_dumphooks();
	}
	return (0);
}

SYSCTL_PROC(_debug, OID_AUTO, ng_dump_items, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(int), sysctl_debug_ng_dump_items, "I", "Number of allocated items");
#endif	/* NETGRAPH_DEBUG */

/***********************************************************************
* Worklist routines
**********************************************************************/
/*
 * Pick a node off the list of nodes with work,
 * try get an item to process off it. Remove the node from the list.
 */
static void
ngthread(void *arg)
{
	for (;;) {
		node_p  node;

		/* Get node from the worklist. */
		NG_WORKLIST_LOCK();
		while ((node = STAILQ_FIRST(&ng_worklist)) == NULL)
			NG_WORKLIST_SLEEP();
		STAILQ_REMOVE_HEAD(&ng_worklist, nd_input_queue.q_work);
		NG_WORKLIST_UNLOCK();
		CURVNET_SET(node->nd_vnet);
		CTR3(KTR_NET, "%20s: node [%x] (%p) taken off worklist",
		    __func__, node->nd_ID, node);
		/*
		 * We have the node. We also take over the reference
		 * that the list had on it.
		 * Now process as much as you can, until it won't
		 * let you have another item off the queue.
		 * All this time, keep the reference
		 * that lets us be sure that the node still exists.
		 * Let the reference go at the last minute.
		 */
		for (;;) {
			item_p item;
			int rw;

			NG_QUEUE_LOCK(&node->nd_input_queue);
			item = ng_dequeue(node, &rw);
			if (item == NULL) {
				node->nd_input_queue.q_flags2 &= ~NGQ2_WORKQ;
				NG_QUEUE_UNLOCK(&node->nd_input_queue);
				break; /* go look for another node */
			} else {
				NG_QUEUE_UNLOCK(&node->nd_input_queue);
				NGI_GET_NODE(item, node); /* zaps stored node */
				ng_apply_item(node, item, rw);
				NG_NODE_UNREF(node);
			}
		}
		NG_NODE_UNREF(node);
		CURVNET_RESTORE();
	}
}

/*
 * XXX
 * It's posible that a debugging NG_NODE_REF may need
 * to be outside the mutex zone
 */
static void
ng_worklist_add(node_p node)
{

	mtx_assert(&node->nd_input_queue.q_mtx, MA_OWNED);

	if ((node->nd_input_queue.q_flags2 & NGQ2_WORKQ) == 0) {
		/*
		 * If we are not already on the work queue,
		 * then put us on.
		 */
		node->nd_input_queue.q_flags2 |= NGQ2_WORKQ;
		NG_NODE_REF(node); /* XXX safe in mutex? */
		NG_WORKLIST_LOCK();
		STAILQ_INSERT_TAIL(&ng_worklist, node, nd_input_queue.q_work);
		NG_WORKLIST_UNLOCK();
		CTR3(KTR_NET, "%20s: node [%x] (%p) put on worklist", __func__,
		    node->nd_ID, node);
		NG_WORKLIST_WAKEUP();
	} else {
		CTR3(KTR_NET, "%20s: node [%x] (%p) already on worklist",
		    __func__, node->nd_ID, node);
	}
}

/***********************************************************************
* Externally useable functions to set up a queue item ready for sending
***********************************************************************/

#ifdef	NETGRAPH_DEBUG
#define	ITEM_DEBUG_CHECKS						\
	do {								\
		if (NGI_NODE(item) ) {					\
			printf("item already has node");		\
			kdb_enter(KDB_WHY_NETGRAPH, "has node");	\
			NGI_CLR_NODE(item);				\
		}							\
		if (NGI_HOOK(item) ) {					\
			printf("item already has hook");		\
			kdb_enter(KDB_WHY_NETGRAPH, "has hook");	\
			NGI_CLR_HOOK(item);				\
		}							\
	} while (0)
#else
#define ITEM_DEBUG_CHECKS
#endif

/*
 * Put mbuf into the item.
 * Hook and node references will be removed when the item is dequeued.
 * (or equivalent)
 * (XXX) Unsafe because no reference held by peer on remote node.
 * remote node might go away in this timescale.
 * We know the hooks can't go away because that would require getting
 * a writer item on both nodes and we must have at least a  reader
 * here to be able to do this.
 * Note that the hook loaded is the REMOTE hook.
 *
 * This is possibly in the critical path for new data.
 */
item_p
ng_package_data(struct mbuf *m, int flags)
{
	item_p item;

	if ((item = ng_alloc_item(NGQF_DATA, flags)) == NULL) {
		NG_FREE_M(m);
		return (NULL);
	}
	ITEM_DEBUG_CHECKS;
	item->el_flags |= NGQF_READER;
	NGI_M(item) = m;
	return (item);
}

/*
 * Allocate a queue item and put items into it..
 * Evaluate the address as this will be needed to queue it and
 * to work out what some of the fields should be.
 * Hook and node references will be removed when the item is dequeued.
 * (or equivalent)
 */
item_p
ng_package_msg(struct ng_mesg *msg, int flags)
{
	item_p item;

	if ((item = ng_alloc_item(NGQF_MESG, flags)) == NULL) {
		NG_FREE_MSG(msg);
		return (NULL);
	}
	ITEM_DEBUG_CHECKS;
	/* Messages items count as writers unless explicitly exempted. */
	if (msg->header.cmd & NGM_READONLY)
		item->el_flags |= NGQF_READER;
	else
		item->el_flags |= NGQF_WRITER;
	/*
	 * Set the current lasthook into the queue item
	 */
	NGI_MSG(item) = msg;
	NGI_RETADDR(item) = 0;
	return (item);
}

#define SET_RETADDR(item, here, retaddr)				\
	do {	/* Data or fn items don't have retaddrs */		\
		if ((item->el_flags & NGQF_TYPE) == NGQF_MESG) {	\
			if (retaddr) {					\
				NGI_RETADDR(item) = retaddr;		\
			} else {					\
				/*					\
				 * The old return address should be ok.	\
				 * If there isn't one, use the address	\
				 * here.				\
				 */					\
				if (NGI_RETADDR(item) == 0) {		\
					NGI_RETADDR(item)		\
						= ng_node2ID(here);	\
				}					\
			}						\
		}							\
	} while (0)

int
ng_address_hook(node_p here, item_p item, hook_p hook, ng_ID_t retaddr)
{
	hook_p peer;
	node_p peernode;
	ITEM_DEBUG_CHECKS;
	/*
	 * Quick sanity check..
	 * Since a hook holds a reference on its node, once we know
	 * that the peer is still connected (even if invalid,) we know
	 * that the peer node is present, though maybe invalid.
	 */
	TOPOLOGY_RLOCK();
	if ((hook == NULL) || NG_HOOK_NOT_VALID(hook) ||
	    NG_HOOK_NOT_VALID(peer = NG_HOOK_PEER(hook)) ||
	    NG_NODE_NOT_VALID(peernode = NG_PEER_NODE(hook))) {
		NG_FREE_ITEM(item);
		TRAP_ERROR();
		TOPOLOGY_RUNLOCK();
		return (ENETDOWN);
	}

	/*
	 * Transfer our interest to the other (peer) end.
	 */
	NG_HOOK_REF(peer);
	NG_NODE_REF(peernode);
	NGI_SET_HOOK(item, peer);
	NGI_SET_NODE(item, peernode);
	SET_RETADDR(item, here, retaddr);

	TOPOLOGY_RUNLOCK();

	return (0);
}

int
ng_address_path(node_p here, item_p item, const char *address, ng_ID_t retaddr)
{
	node_p	dest = NULL;
	hook_p	hook = NULL;
	int	error;

	ITEM_DEBUG_CHECKS;
	/*
	 * Note that ng_path2noderef increments the reference count
	 * on the node for us if it finds one. So we don't have to.
	 */
	error = ng_path2noderef(here, address, &dest, &hook);
	if (error) {
		NG_FREE_ITEM(item);
		return (error);
	}
	NGI_SET_NODE(item, dest);
	if (hook)
		NGI_SET_HOOK(item, hook);

	SET_RETADDR(item, here, retaddr);
	return (0);
}

int
ng_address_ID(node_p here, item_p item, ng_ID_t ID, ng_ID_t retaddr)
{
	node_p dest;

	ITEM_DEBUG_CHECKS;
	/*
	 * Find the target node.
	 */
	dest = ng_ID2noderef(ID); /* GETS REFERENCE! */
	if (dest == NULL) {
		NG_FREE_ITEM(item);
		TRAP_ERROR();
		return(EINVAL);
	}
	/* Fill out the contents */
	NGI_SET_NODE(item, dest);
	NGI_CLR_HOOK(item);
	SET_RETADDR(item, here, retaddr);
	return (0);
}

/*
 * special case to send a message to self (e.g. destroy node)
 * Possibly indicate an arrival hook too.
 * Useful for removing that hook :-)
 */
item_p
ng_package_msg_self(node_p here, hook_p hook, struct ng_mesg *msg)
{
	item_p item;

	/*
	 * Find the target node.
	 * If there is a HOOK argument, then use that in preference
	 * to the address.
	 */
	if ((item = ng_alloc_item(NGQF_MESG, NG_NOFLAGS)) == NULL) {
		NG_FREE_MSG(msg);
		return (NULL);
	}

	/* Fill out the contents */
	item->el_flags |= NGQF_WRITER;
	NG_NODE_REF(here);
	NGI_SET_NODE(item, here);
	if (hook) {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);
	}
	NGI_MSG(item) = msg;
	NGI_RETADDR(item) = ng_node2ID(here);
	return (item);
}

/*
 * Send ng_item_fn function call to the specified node.
 */

int
ng_send_fn(node_p node, hook_p hook, ng_item_fn *fn, void * arg1, int arg2)
{

	return ng_send_fn1(node, hook, fn, arg1, arg2, NG_NOFLAGS);
}

int
ng_send_fn1(node_p node, hook_p hook, ng_item_fn *fn, void * arg1, int arg2,
	int flags)
{
	item_p item;

	if ((item = ng_alloc_item(NGQF_FN, flags)) == NULL) {
		return (ENOMEM);
	}
	item->el_flags |= NGQF_WRITER;
	NG_NODE_REF(node); /* and one for the item */
	NGI_SET_NODE(item, node);
	if (hook) {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);
	}
	NGI_FN(item) = fn;
	NGI_ARG1(item) = arg1;
	NGI_ARG2(item) = arg2;
	return(ng_snd_item(item, flags));
}

/*
 * Send ng_item_fn2 function call to the specified node.
 *
 * If an optional pitem parameter is supplied, its apply
 * callback will be copied to the new item. If also NG_REUSE_ITEM
 * flag is set, no new item will be allocated, but pitem will
 * be used.
 */
int
ng_send_fn2(node_p node, hook_p hook, item_p pitem, ng_item_fn2 *fn, void *arg1,
	int arg2, int flags)
{
	item_p item;

	KASSERT((pitem != NULL || (flags & NG_REUSE_ITEM) == 0),
	    ("%s: NG_REUSE_ITEM but no pitem", __func__));

	/*
	 * Allocate a new item if no supplied or
	 * if we can't use supplied one.
	 */
	if (pitem == NULL || (flags & NG_REUSE_ITEM) == 0) {
		if ((item = ng_alloc_item(NGQF_FN2, flags)) == NULL)
			return (ENOMEM);
		if (pitem != NULL)
			item->apply = pitem->apply;
	} else {
		if ((item = ng_realloc_item(pitem, NGQF_FN2, flags)) == NULL)
			return (ENOMEM);
	}

	item->el_flags = (item->el_flags & ~NGQF_RW) | NGQF_WRITER;
	NG_NODE_REF(node); /* and one for the item */
	NGI_SET_NODE(item, node);
	if (hook) {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);
	}
	NGI_FN2(item) = fn;
	NGI_ARG1(item) = arg1;
	NGI_ARG2(item) = arg2;
	return(ng_snd_item(item, flags));
}

/*
 * Official timeout routines for Netgraph nodes.
 */
static void
ng_callout_trampoline(void *arg)
{
	item_p item = arg;

	CURVNET_SET(NGI_NODE(item)->nd_vnet);
	ng_snd_item(item, 0);
	CURVNET_RESTORE();
}

int
ng_callout(struct callout *c, node_p node, hook_p hook, int ticks,
    ng_item_fn *fn, void * arg1, int arg2)
{
	item_p item, oitem;

	if ((item = ng_alloc_item(NGQF_FN, NG_NOFLAGS)) == NULL)
		return (ENOMEM);

	item->el_flags |= NGQF_WRITER;
	NG_NODE_REF(node);		/* and one for the item */
	NGI_SET_NODE(item, node);
	if (hook) {
		NG_HOOK_REF(hook);
		NGI_SET_HOOK(item, hook);
	}
	NGI_FN(item) = fn;
	NGI_ARG1(item) = arg1;
	NGI_ARG2(item) = arg2;
	oitem = c->c_arg;
	if (callout_reset(c, ticks, &ng_callout_trampoline, item) == 1 &&
	    oitem != NULL)
		NG_FREE_ITEM(oitem);
	return (0);
}

/* A special modified version of untimeout() */
int
ng_uncallout(struct callout *c, node_p node)
{
	item_p item;
	int rval;

	KASSERT(c != NULL, ("ng_uncallout: NULL callout"));
	KASSERT(node != NULL, ("ng_uncallout: NULL node"));

	rval = callout_stop(c);
	item = c->c_arg;
	/* Do an extra check */
	if ((rval > 0) && (c->c_func == &ng_callout_trampoline) &&
	    (item != NULL) && (NGI_NODE(item) == node)) {
		/*
		 * We successfully removed it from the queue before it ran
		 * So now we need to unreference everything that was
		 * given extra references. (NG_FREE_ITEM does this).
		 */
		NG_FREE_ITEM(item);
	}
	c->c_arg = NULL;

	/*
	 * Callers only want to know if the callout was cancelled and
	 * not draining or stopped.
	 */
	return (rval > 0);
}

/*
 * Set the address, if none given, give the node here.
 */
void
ng_replace_retaddr(node_p here, item_p item, ng_ID_t retaddr)
{
	if (retaddr) {
		NGI_RETADDR(item) = retaddr;
	} else {
		/*
		 * The old return address should be ok.
		 * If there isn't one, use the address here.
		 */
		NGI_RETADDR(item) = ng_node2ID(here);
	}
}
