/*
 * ng_socket.c
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
 * $Whistle: ng_socket.c,v 1.28 1999/11/01 09:24:52 julian Exp $
 */

/*
 * Netgraph socket nodes
 *
 * There are two types of netgraph sockets, control and data.
 * Control sockets have a netgraph node, but data sockets are
 * parasitic on control sockets, and have no node of their own.
 */

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>

#include <net/vnet.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_socketvar.h>
#include <netgraph/ng_socket.h>

#ifdef NG_SEPARATE_MALLOC
static MALLOC_DEFINE(M_NETGRAPH_PATH, "netgraph_path", "netgraph path info");
static MALLOC_DEFINE(M_NETGRAPH_SOCK, "netgraph_sock", "netgraph socket info");
#else
#define M_NETGRAPH_PATH M_NETGRAPH
#define M_NETGRAPH_SOCK M_NETGRAPH
#endif

/*
 * It's Ascii-art time!
 *   +-------------+   +-------------+
 *   |socket  (ctl)|   |socket (data)|
 *   +-------------+   +-------------+
 *          ^                 ^
 *          |                 |
 *          v                 v
 *    +-----------+     +-----------+
 *    |pcb   (ctl)|     |pcb  (data)|
 *    +-----------+     +-----------+
 *          ^                 ^
 *          |                 |
 *          v                 v
 *      +--------------------------+
 *      |   Socket type private    |
 *      |       data               |
 *      +--------------------------+
 *                   ^
 *                   |
 *                   v
 *           +----------------+
 *           | struct ng_node |
 *           +----------------+
 */

/* Netgraph node methods */
static ng_constructor_t	ngs_constructor;
static ng_rcvmsg_t	ngs_rcvmsg;
static ng_shutdown_t	ngs_shutdown;
static ng_newhook_t	ngs_newhook;
static ng_connect_t	ngs_connect;
static ng_findhook_t	ngs_findhook;
static ng_rcvdata_t	ngs_rcvdata;
static ng_disconnect_t	ngs_disconnect;

/* Internal methods */
static int	ng_attach_data(struct socket *so);
static int	ng_attach_cntl(struct socket *so);
static int	ng_attach_common(struct socket *so, int type);
static void	ng_detach_common(struct ngpcb *pcbp, int type);
static void	ng_socket_free_priv(struct ngsock *priv);
static int	ng_connect_data(struct sockaddr *nam, struct ngpcb *pcbp);
static int	ng_bind(struct sockaddr *nam, struct ngpcb *pcbp);

static int	ngs_mod_event(module_t mod, int event, void *data);
static void	ng_socket_item_applied(void *context, int error);

/* Netgraph type descriptor */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_SOCKET_NODE_TYPE,
	.mod_event =	ngs_mod_event,
	.constructor =	ngs_constructor,
	.rcvmsg =	ngs_rcvmsg,
	.shutdown =	ngs_shutdown,
	.newhook =	ngs_newhook,
	.connect =	ngs_connect,
	.findhook =	ngs_findhook,
	.rcvdata =	ngs_rcvdata,
	.disconnect =	ngs_disconnect,
};
NETGRAPH_INIT_ORDERED(socket, &typestruct, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);

/* Buffer space */
static u_long ngpdg_sendspace = 20 * 1024;	/* really max datagram size */
SYSCTL_ULONG(_net_graph, OID_AUTO, maxdgram, CTLFLAG_RW,
    &ngpdg_sendspace , 0, "Maximum outgoing Netgraph datagram size");
static u_long ngpdg_recvspace = 20 * 1024;
SYSCTL_ULONG(_net_graph, OID_AUTO, recvspace, CTLFLAG_RW,
    &ngpdg_recvspace , 0, "Maximum space for incoming Netgraph datagrams");

/* List of all sockets (for netstat -f netgraph) */
static LIST_HEAD(, ngpcb) ngsocklist;

static struct mtx	ngsocketlist_mtx;

#define sotongpcb(so) ((struct ngpcb *)(so)->so_pcb)

/* If getting unexplained errors returned, set this to "kdb_enter("X"); */
#ifndef TRAP_ERROR
#define TRAP_ERROR
#endif

struct hookpriv {
	LIST_ENTRY(hookpriv)	next;
	hook_p			hook;
};
LIST_HEAD(ngshash, hookpriv);

/* Per-node private data */
struct ngsock {
	struct ng_node	*node;		/* the associated netgraph node */
	struct ngpcb	*datasock;	/* optional data socket */
	struct ngpcb	*ctlsock;	/* optional control socket */
	struct ngshash	*hash;		/* hash for hook names */
	u_long		hmask;		/* hash mask */
	int	flags;
	int	refs;
	struct mtx	mtx;		/* mtx to wait on */
	int		error;		/* place to store error */
};

#define	NGS_FLAG_NOLINGER	1	/* close with last hook */

/***************************************************************
	Control sockets
***************************************************************/

static int
ngc_attach(struct socket *so, int proto, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);
	int error;

	error = priv_check(td, PRIV_NETGRAPH_CONTROL);
	if (error)
		return (error);
	if (pcbp != NULL)
		return (EISCONN);
	return (ng_attach_cntl(so));
}

static void
ngc_detach(struct socket *so)
{
	struct ngpcb *const pcbp = sotongpcb(so);

	KASSERT(pcbp != NULL, ("ngc_detach: pcbp == NULL"));
	ng_detach_common(pcbp, NG_CONTROL);
}

static int
ngc_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
	 struct mbuf *control, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);
	struct ngsock *const priv = NG_NODE_PRIVATE(pcbp->sockdata->node);
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) addr;
	struct ng_mesg *msg;
	struct mbuf *m0;
	item_p item;
	char *path = NULL;
	int len, error = 0;
	struct ng_apply_info apply;

	if (control) {
		error = EINVAL;
		goto release;
	}

	/* Require destination as there may be >= 1 hooks on this node. */
	if (addr == NULL) {
		error = EDESTADDRREQ;
		goto release;
	}

	/*
	 * Allocate an expendable buffer for the path, chop off
	 * the sockaddr header, and make sure it's NUL terminated.
	 */
	len = sap->sg_len - 2;
	path = malloc(len + 1, M_NETGRAPH_PATH, M_WAITOK);
	bcopy(sap->sg_data, path, len);
	path[len] = '\0';

	/*
	 * Move the actual message out of mbufs into a linear buffer.
	 * Start by adding up the size of the data. (could use mh_len?)
	 */
	for (len = 0, m0 = m; m0 != NULL; m0 = m0->m_next)
		len += m0->m_len;

	/*
	 * Move the data into a linear buffer as well.
	 * Messages are not delivered in mbufs.
	 */
	msg = malloc(len + 1, M_NETGRAPH_MSG, M_WAITOK);
	m_copydata(m, 0, len, (char *)msg);

	if (msg->header.version != NG_VERSION) {
		free(msg, M_NETGRAPH_MSG);
		error = EINVAL;
		goto release;
	}

	/*
	 * Hack alert!
	 * We look into the message and if it mkpeers a node of unknown type, we
	 * try to load it. We need to do this now, in syscall thread, because if
	 * message gets queued and applied later we will get panic.
	 */
	if (msg->header.typecookie == NGM_GENERIC_COOKIE &&
	    msg->header.cmd == NGM_MKPEER) {
		struct ngm_mkpeer *const mkp = (struct ngm_mkpeer *) msg->data;

		if (ng_findtype(mkp->type) == NULL) {
			char filename[NG_TYPESIZ + 3];
			int fileid;

			/* Not found, try to load it as a loadable module. */
			snprintf(filename, sizeof(filename), "ng_%s",
			    mkp->type);
			error = kern_kldload(curthread, filename, &fileid);
			if (error != 0) {
				free(msg, M_NETGRAPH_MSG);
				goto release;
			}

			/* See if type has been loaded successfully. */
			if (ng_findtype(mkp->type) == NULL) {
				free(msg, M_NETGRAPH_MSG);
				(void)kern_kldunload(curthread, fileid,
				    LINKER_UNLOAD_NORMAL);
				error =  ENXIO;
				goto release;
			}
		}
	}

	item = ng_package_msg(msg, NG_WAITOK);
	if ((error = ng_address_path((pcbp->sockdata->node), item, path, 0))
	    != 0) {
#ifdef TRACE_MESSAGES
		printf("ng_address_path: errx=%d\n", error);
#endif
		goto release;
	}

#ifdef TRACE_MESSAGES
	printf("[%x]:<---------[socket]: c=<%d>cmd=%x(%s) f=%x #%d (%s)\n",
		item->el_dest->nd_ID,
		msg->header.typecookie,
		msg->header.cmd,
		msg->header.cmdstr,
		msg->header.flags,
		msg->header.token,
		item->el_dest->nd_type->name);
#endif
	SAVE_LINE(item);
	/*
	 * We do not want to return from syscall until the item
	 * is processed by destination node. We register callback
	 * on the item, which will update priv->error when item
	 * was applied.
	 * If ng_snd_item() has queued item, we sleep until
	 * callback wakes us up.
	 */
	bzero(&apply, sizeof(apply));
	apply.apply = ng_socket_item_applied;
	apply.context = priv;
	item->apply = &apply;
	priv->error = -1;

	error = ng_snd_item(item, 0);

	mtx_lock(&priv->mtx);
	if (priv->error == -1)
		msleep(priv, &priv->mtx, 0, "ngsock", 0);
	mtx_unlock(&priv->mtx);
	KASSERT(priv->error != -1,
	    ("ng_socket: priv->error wasn't updated"));
	error = priv->error;

release:
	if (path != NULL)
		free(path, M_NETGRAPH_PATH);
	if (control != NULL)
		m_freem(control);
	if (m != NULL)
		m_freem(m);
	return (error);
}

static int
ngc_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);

	if (pcbp == NULL)
		return (EINVAL);
	return (ng_bind(nam, pcbp));
}

static int
ngc_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	/*
	 * At this time refuse to do this.. it used to
	 * do something but it was undocumented and not used.
	 */
	printf("program tried to connect control socket to remote node\n");
	return (EINVAL);
}

/***************************************************************
	Data sockets
***************************************************************/

static int
ngd_attach(struct socket *so, int proto, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);

	if (pcbp != NULL)
		return (EISCONN);
	return (ng_attach_data(so));
}

static void
ngd_detach(struct socket *so)
{
	struct ngpcb *const pcbp = sotongpcb(so);

	KASSERT(pcbp != NULL, ("ngd_detach: pcbp == NULL"));
	ng_detach_common(pcbp, NG_DATA);
}

static int
ngd_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
	 struct mbuf *control, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) addr;
	int	len, error;
	hook_p  hook = NULL;
	char	hookname[NG_HOOKSIZ];

	if ((pcbp == NULL) || (control != NULL)) {
		error = EINVAL;
		goto release;
	}
	if (pcbp->sockdata == NULL) {
		error = ENOTCONN;
		goto release;
	}

	if (sap == NULL)
		len = 0;		/* Make compiler happy. */
	else
		len = sap->sg_len - 2;

	/*
	 * If the user used any of these ways to not specify an address
	 * then handle specially.
	 */
	if ((sap == NULL) || (len <= 0) || (*sap->sg_data == '\0')) {
		if (NG_NODE_NUMHOOKS(pcbp->sockdata->node) != 1) {
			error = EDESTADDRREQ;
			goto release;
		}
		/*
		 * If exactly one hook exists, just use it.
		 * Special case to allow write(2) to work on an ng_socket.
		 */
		hook = LIST_FIRST(&pcbp->sockdata->node->nd_hooks);
	} else {
		if (len >= NG_HOOKSIZ) {
			error = EINVAL;
			goto release;
		}

		/*
		 * chop off the sockaddr header, and make sure it's NUL
		 * terminated
		 */
		bcopy(sap->sg_data, hookname, len);
		hookname[len] = '\0';

		/* Find the correct hook from 'hookname' */
		hook = ng_findhook(pcbp->sockdata->node, hookname);
		if (hook == NULL) {
			error = EHOSTUNREACH;
			goto release;
		}
	}

	/* Send data. */
	NG_SEND_DATA_FLAGS(error, hook, m, NG_WAITOK);

release:
	if (control != NULL)
		m_freem(control);
	if (m != NULL)
		m_freem(m);
	return (error);
}

static int
ngd_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct ngpcb *const pcbp = sotongpcb(so);

	if (pcbp == NULL)
		return (EINVAL);
	return (ng_connect_data(nam, pcbp));
}

/*
 * Used for both data and control sockets
 */
static int
ng_getsockaddr(struct socket *so, struct sockaddr **addr)
{
	struct ngpcb *pcbp;
	struct sockaddr_ng *sg;
	int sg_len;
	int error = 0;

	pcbp = sotongpcb(so);
	if ((pcbp == NULL) || (pcbp->sockdata == NULL))
		/* XXXGL: can this still happen? */
		return (EINVAL);

	sg_len = sizeof(struct sockaddr_ng) + NG_NODESIZ -
	    sizeof(sg->sg_data);
	sg = malloc(sg_len, M_SONAME, M_WAITOK | M_ZERO);

	mtx_lock(&pcbp->sockdata->mtx);
	if (pcbp->sockdata->node != NULL) {
		node_p node = pcbp->sockdata->node;

		if (NG_NODE_HAS_NAME(node))
			bcopy(NG_NODE_NAME(node), sg->sg_data,
			    strlen(NG_NODE_NAME(node)));
		mtx_unlock(&pcbp->sockdata->mtx);

		sg->sg_len = sg_len;
		sg->sg_family = AF_NETGRAPH;
		*addr = (struct sockaddr *)sg;
	} else {
		mtx_unlock(&pcbp->sockdata->mtx);
		free(sg, M_SONAME);
		error = EINVAL;
	}

	return (error);
}

/*
 * Attach a socket to it's protocol specific partner.
 * For a control socket, actually create a netgraph node and attach
 * to it as well.
 */

static int
ng_attach_cntl(struct socket *so)
{
	struct ngsock *priv;
	struct ngpcb *pcbp;
	node_p node;
	int error;

	/* Setup protocol control block */
	if ((error = ng_attach_common(so, NG_CONTROL)) != 0)
		return (error);
	pcbp = sotongpcb(so);

	/* Make the generic node components */
	if ((error = ng_make_node_common(&typestruct, &node)) != 0) {
		ng_detach_common(pcbp, NG_CONTROL);
		return (error);
	}

	/*
	 * Allocate node private info and hash. We start
	 * with 16 hash entries, however we may grow later
	 * in ngs_newhook(). We can't predict how much hooks
	 * does this node plan to have.
	 */
	priv = malloc(sizeof(*priv), M_NETGRAPH_SOCK, M_WAITOK | M_ZERO);
	priv->hash = hashinit(16, M_NETGRAPH_SOCK, &priv->hmask);

	/* Initialize mutex. */
	mtx_init(&priv->mtx, "ng_socket", NULL, MTX_DEF);

	/* Link the pcb the private data. */
	priv->ctlsock = pcbp;
	pcbp->sockdata = priv;
	priv->refs++;
	priv->node = node;
	pcbp->node_id = node->nd_ID;	/* hint for netstat(1) */

	/* Link the node and the private data. */
	NG_NODE_SET_PRIVATE(priv->node, priv);
	NG_NODE_REF(priv->node);
	priv->refs++;

	return (0);
}

static int
ng_attach_data(struct socket *so)
{
	return (ng_attach_common(so, NG_DATA));
}

/*
 * Set up a socket protocol control block.
 * This code is shared between control and data sockets.
 */
static int
ng_attach_common(struct socket *so, int type)
{
	struct ngpcb *pcbp;
	int error;

	/* Standard socket setup stuff. */
	error = soreserve(so, ngpdg_sendspace, ngpdg_recvspace);
	if (error)
		return (error);

	/* Allocate the pcb. */
	pcbp = malloc(sizeof(struct ngpcb), M_PCB, M_WAITOK | M_ZERO);
	pcbp->type = type;

	/* Link the pcb and the socket. */
	so->so_pcb = (caddr_t)pcbp;
	pcbp->ng_socket = so;

	/* Add the socket to linked list */
	mtx_lock(&ngsocketlist_mtx);
	LIST_INSERT_HEAD(&ngsocklist, pcbp, socks);
	mtx_unlock(&ngsocketlist_mtx);
	return (0);
}

/*
 * Disassociate the socket from it's protocol specific
 * partner. If it's attached to a node's private data structure,
 * then unlink from that too. If we were the last socket attached to it,
 * then shut down the entire node. Shared code for control and data sockets.
 */
static void
ng_detach_common(struct ngpcb *pcbp, int which)
{
	struct ngsock *priv = pcbp->sockdata;

	if (priv != NULL) {
		mtx_lock(&priv->mtx);

		switch (which) {
		case NG_CONTROL:
			priv->ctlsock = NULL;
			break;
		case NG_DATA:
			priv->datasock = NULL;
			break;
		default:
			panic("%s", __func__);
		}
		pcbp->sockdata = NULL;
		pcbp->node_id = 0;

		ng_socket_free_priv(priv);
	}

	pcbp->ng_socket->so_pcb = NULL;
	mtx_lock(&ngsocketlist_mtx);
	LIST_REMOVE(pcbp, socks);
	mtx_unlock(&ngsocketlist_mtx);
	free(pcbp, M_PCB);
}

/*
 * Remove a reference from node private data.
 */
static void
ng_socket_free_priv(struct ngsock *priv)
{
	mtx_assert(&priv->mtx, MA_OWNED);

	priv->refs--;

	if (priv->refs == 0) {
		mtx_destroy(&priv->mtx);
		hashdestroy(priv->hash, M_NETGRAPH_SOCK, priv->hmask);
		free(priv, M_NETGRAPH_SOCK);
		return;
	}

	if ((priv->refs == 1) && (priv->node != NULL)) {
		node_p node = priv->node;

		priv->node = NULL;
		mtx_unlock(&priv->mtx);
		NG_NODE_UNREF(node);
		ng_rmnode_self(node);
	} else
		mtx_unlock(&priv->mtx);
}

/*
 * Connect the data socket to a named control socket node.
 */
static int
ng_connect_data(struct sockaddr *nam, struct ngpcb *pcbp)
{
	struct sockaddr_ng *sap;
	node_p farnode;
	struct ngsock *priv;
	int error;
	item_p item;

	/* If we are already connected, don't do it again. */
	if (pcbp->sockdata != NULL)
		return (EISCONN);

	/*
	 * Find the target (victim) and check it doesn't already have
	 * a data socket. Also check it is a 'socket' type node.
	 * Use ng_package_data() and ng_address_path() to do this.
	 */

	sap = (struct sockaddr_ng *) nam;
	/* The item will hold the node reference. */
	item = ng_package_data(NULL, NG_WAITOK);

	if ((error = ng_address_path(NULL, item,  sap->sg_data, 0)))
		return (error); /* item is freed on failure */

	/*
	 * Extract node from item and free item. Remember we now have
	 * a reference on the node. The item holds it for us.
	 * when we free the item we release the reference.
	 */
	farnode = item->el_dest; /* shortcut */
	if (strcmp(farnode->nd_type->name, NG_SOCKET_NODE_TYPE) != 0) {
		NG_FREE_ITEM(item); /* drop the reference to the node */
		return (EINVAL);
	}
	priv = NG_NODE_PRIVATE(farnode);
	if (priv->datasock != NULL) {
		NG_FREE_ITEM(item);	/* drop the reference to the node */
		return (EADDRINUSE);
	}

	/*
	 * Link the PCB and the private data struct. and note the extra
	 * reference. Drop the extra reference on the node.
	 */
	mtx_lock(&priv->mtx);
	priv->datasock = pcbp;
	pcbp->sockdata = priv;
	pcbp->node_id = priv->node->nd_ID;	/* hint for netstat(1) */
	priv->refs++;
	mtx_unlock(&priv->mtx);
	NG_FREE_ITEM(item);	/* drop the reference to the node */
	return (0);
}

/*
 * Binding a socket means giving the corresponding node a name
 */
static int
ng_bind(struct sockaddr *nam, struct ngpcb *pcbp)
{
	struct ngsock *const priv = pcbp->sockdata;
	struct sockaddr_ng *const sap = (struct sockaddr_ng *) nam;

	if (priv == NULL) {
		TRAP_ERROR;
		return (EINVAL);
	}
	if ((sap->sg_len < 4) || (sap->sg_len > (NG_NODESIZ + 2)) ||
	    (sap->sg_data[0] == '\0') ||
	    (sap->sg_data[sap->sg_len - 3] != '\0')) {
		TRAP_ERROR;
		return (EINVAL);
	}
	return (ng_name_node(priv->node, sap->sg_data));
}

/***************************************************************
	Netgraph node
***************************************************************/

/*
 * You can only create new nodes from the socket end of things.
 */
static int
ngs_constructor(node_p nodep)
{
	return (EINVAL);
}

static void
ngs_rehash(node_p node)
{
	struct ngsock *priv = NG_NODE_PRIVATE(node);
	struct ngshash *new;
	struct hookpriv *hp;
	hook_p hook;
	uint32_t h;
	u_long hmask;

	new = hashinit_flags((priv->hmask + 1) * 2, M_NETGRAPH_SOCK, &hmask,
	    HASH_NOWAIT);
	if (new == NULL)
		return;

	LIST_FOREACH(hook, &node->nd_hooks, hk_hooks) {
		hp = NG_HOOK_PRIVATE(hook);
#ifdef INVARIANTS
		LIST_REMOVE(hp, next);
#endif
		h = hash32_str(NG_HOOK_NAME(hook), HASHINIT) & hmask;
		LIST_INSERT_HEAD(&new[h], hp, next);
	}

	hashdestroy(priv->hash, M_NETGRAPH_SOCK, priv->hmask);
	priv->hash = new;
	priv->hmask = hmask;
}

/*
 * We allow any hook to be connected to the node.
 * There is no per-hook private information though.
 */
static int
ngs_newhook(node_p node, hook_p hook, const char *name)
{
	struct ngsock *const priv = NG_NODE_PRIVATE(node);
	struct hookpriv *hp;
	uint32_t h;

	hp = malloc(sizeof(*hp), M_NETGRAPH_SOCK, M_NOWAIT);
	if (hp == NULL)
		return (ENOMEM);
	if (node->nd_numhooks * 2 > priv->hmask)
		ngs_rehash(node);
	hp->hook = hook;
	h = hash32_str(name, HASHINIT) & priv->hmask;
	LIST_INSERT_HEAD(&priv->hash[h], hp, next);
	NG_HOOK_SET_PRIVATE(hook, hp);

	return (0);
}

/*
 * If only one hook, allow read(2) and write(2) to work.
 */
static int
ngs_connect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct ngsock *priv = NG_NODE_PRIVATE(node);

	if ((priv->datasock) && (priv->datasock->ng_socket)) {
		if (NG_NODE_NUMHOOKS(node) == 1)
			priv->datasock->ng_socket->so_state |= SS_ISCONNECTED;
		else
			priv->datasock->ng_socket->so_state &= ~SS_ISCONNECTED;
	}
	return (0);
}

/* Look up hook by name */
static hook_p
ngs_findhook(node_p node, const char *name)
{
	struct ngsock *priv = NG_NODE_PRIVATE(node);
	struct hookpriv *hp;
	uint32_t h;

	/*
	 * Microoptimisation for an ng_socket with
	 * a single hook, which is a common case.
	 */
	if (node->nd_numhooks == 1) {
		hook_p hook;

		hook = LIST_FIRST(&node->nd_hooks);

		if (strcmp(NG_HOOK_NAME(hook), name) == 0)
			return (hook);
		else
			return (NULL);
	}

	h = hash32_str(name, HASHINIT) & priv->hmask;

	LIST_FOREACH(hp, &priv->hash[h], next)
		if (strcmp(NG_HOOK_NAME(hp->hook), name) == 0)
			return (hp->hook);

	return (NULL);
}

/*
 * Incoming messages get passed up to the control socket.
 * Unless they are for us specifically (socket_type)
 */
static int
ngs_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ngsock *const priv = NG_NODE_PRIVATE(node);
	struct ngpcb *pcbp;
	struct socket *so;
	struct sockaddr_ng addr;
	struct ng_mesg *msg;
	struct mbuf *m;
	ng_ID_t	retaddr = NGI_RETADDR(item);
	int addrlen;
	int error = 0;

	NGI_GET_MSG(item, msg);
	NG_FREE_ITEM(item);

	/*
	 * Grab priv->mtx here to prevent destroying of control socket
	 * after checking that priv->ctlsock is not NULL.
	 */
	mtx_lock(&priv->mtx);
	pcbp = priv->ctlsock;

	/*
	 * Only allow mesgs to be passed if we have the control socket.
	 * Data sockets can only support the generic messages.
	 */
	if (pcbp == NULL) {
		mtx_unlock(&priv->mtx);
		TRAP_ERROR;
		NG_FREE_MSG(msg);
		return (EINVAL);
	}
	so = pcbp->ng_socket;
	SOCKBUF_LOCK(&so->so_rcv);

	/* As long as the race is handled, priv->mtx may be unlocked now. */
	mtx_unlock(&priv->mtx);

#ifdef TRACE_MESSAGES
	printf("[%x]:---------->[socket]: c=<%d>cmd=%x(%s) f=%x #%d\n",
		retaddr,
		msg->header.typecookie,
		msg->header.cmd,
		msg->header.cmdstr,
		msg->header.flags,
		msg->header.token);
#endif

	if (msg->header.typecookie == NGM_SOCKET_COOKIE) {
		switch (msg->header.cmd) {
		case NGM_SOCK_CMD_NOLINGER:
			priv->flags |= NGS_FLAG_NOLINGER;
			break;
		case NGM_SOCK_CMD_LINGER:
			priv->flags &= ~NGS_FLAG_NOLINGER;
			break;
		default:
			error = EINVAL;		/* unknown command */
		}
		SOCKBUF_UNLOCK(&so->so_rcv);

		/* Free the message and return. */
		NG_FREE_MSG(msg);
		return (error);
	}

	/* Get the return address into a sockaddr. */
	bzero(&addr, sizeof(addr));
	addr.sg_len = sizeof(addr);
	addr.sg_family = AF_NETGRAPH;
	addrlen = snprintf((char *)&addr.sg_data, sizeof(addr.sg_data),
	    "[%x]:", retaddr);
	if (addrlen < 0 || addrlen > sizeof(addr.sg_data)) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		printf("%s: snprintf([%x]) failed - %d\n", __func__, retaddr,
		    addrlen);
		NG_FREE_MSG(msg);
		return (EINVAL);
	}

	/* Copy the message itself into an mbuf chain. */
	m = m_devget((caddr_t)msg, sizeof(struct ng_mesg) + msg->header.arglen,
	    0, NULL, NULL);

	/*
	 * Here we free the message. We need to do that
	 * regardless of whether we got mbufs.
	 */
	NG_FREE_MSG(msg);

	if (m == NULL) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		TRAP_ERROR;
		return (ENOBUFS);
	}

	/* Send it up to the socket. */
	if (sbappendaddr_locked(&so->so_rcv, (struct sockaddr *)&addr, m,
	    NULL) == 0) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		TRAP_ERROR;
		m_freem(m);
		return (ENOBUFS);
	}
	sorwakeup_locked(so);
	
	return (error);
}

/*
 * Receive data on a hook
 */
static int
ngs_rcvdata(hook_p hook, item_p item)
{
	struct ngsock *const priv = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ngpcb *const pcbp = priv->datasock;
	struct socket *so;
	struct sockaddr_ng *addr;
	char *addrbuf[NG_HOOKSIZ + 4];
	int addrlen;
	struct mbuf *m;

	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/* If there is no data socket, black-hole it. */
	if (pcbp == NULL) {
		NG_FREE_M(m);
		return (0);
	}
	so = pcbp->ng_socket;

	/* Get the return address into a sockaddr. */
	addrlen = strlen(NG_HOOK_NAME(hook));	/* <= NG_HOOKSIZ - 1 */
	addr = (struct sockaddr_ng *) addrbuf;
	addr->sg_len = addrlen + 3;
	addr->sg_family = AF_NETGRAPH;
	bcopy(NG_HOOK_NAME(hook), addr->sg_data, addrlen);
	addr->sg_data[addrlen] = '\0';

	/* Try to tell the socket which hook it came in on. */
	if (sbappendaddr(&so->so_rcv, (struct sockaddr *)addr, m, NULL) == 0) {
		m_freem(m);
		TRAP_ERROR;
		return (ENOBUFS);
	}
	sorwakeup(so);
	return (0);
}

/*
 * Hook disconnection
 *
 * For this type, removal of the last link destroys the node
 * if the NOLINGER flag is set.
 */
static int
ngs_disconnect(hook_p hook)
{
	node_p node = NG_HOOK_NODE(hook);
	struct ngsock *const priv = NG_NODE_PRIVATE(node);
	struct hookpriv *hp = NG_HOOK_PRIVATE(hook);

	LIST_REMOVE(hp, next);
	free(hp, M_NETGRAPH_SOCK);

	if ((priv->datasock) && (priv->datasock->ng_socket)) {
		if (NG_NODE_NUMHOOKS(node) == 1)
			priv->datasock->ng_socket->so_state |= SS_ISCONNECTED;
		else
			priv->datasock->ng_socket->so_state &= ~SS_ISCONNECTED;
	}

	if ((priv->flags & NGS_FLAG_NOLINGER) &&
	    (NG_NODE_NUMHOOKS(node) == 0) && (NG_NODE_IS_VALID(node)))
		ng_rmnode_self(node);

	return (0);
}

/*
 * Do local shutdown processing.
 * In this case, that involves making sure the socket
 * knows we should be shutting down.
 */
static int
ngs_shutdown(node_p node)
{
	struct ngsock *const priv = NG_NODE_PRIVATE(node);
	struct ngpcb *dpcbp, *pcbp;

	mtx_lock(&priv->mtx);
	dpcbp = priv->datasock;
	pcbp = priv->ctlsock;

	if (dpcbp != NULL)
		soisdisconnected(dpcbp->ng_socket);

	if (pcbp != NULL)
		soisdisconnected(pcbp->ng_socket);

	priv->node = NULL;
	NG_NODE_SET_PRIVATE(node, NULL);
	ng_socket_free_priv(priv);

	NG_NODE_UNREF(node);
	return (0);
}

static void
ng_socket_item_applied(void *context, int error)
{
	struct ngsock *const priv = (struct ngsock *)context;

	mtx_lock(&priv->mtx);
	priv->error = error;
	wakeup(priv);
	mtx_unlock(&priv->mtx);

}

static	int
dummy_disconnect(struct socket *so)
{
	return (0);
}
/*
 * Control and data socket type descriptors
 *
 * XXXRW: Perhaps _close should do something?
 */

static struct pr_usrreqs ngc_usrreqs = {
	.pru_abort =		NULL,
	.pru_attach =		ngc_attach,
	.pru_bind =		ngc_bind,
	.pru_connect =		ngc_connect,
	.pru_detach =		ngc_detach,
	.pru_disconnect =	dummy_disconnect,
	.pru_peeraddr =		NULL,
	.pru_send =		ngc_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_getsockaddr,
	.pru_close =		NULL,
};

static struct pr_usrreqs ngd_usrreqs = {
	.pru_abort =		NULL,
	.pru_attach =		ngd_attach,
	.pru_bind =		NULL,
	.pru_connect =		ngd_connect,
	.pru_detach =		ngd_detach,
	.pru_disconnect =	dummy_disconnect,
	.pru_peeraddr =		NULL,
	.pru_send =		ngd_send,
	.pru_shutdown =		NULL,
	.pru_sockaddr =		ng_getsockaddr,
	.pru_close =		NULL,
};

/*
 * Definitions of protocols supported in the NETGRAPH domain.
 */

extern struct domain ngdomain;		/* stop compiler warnings */

static struct protosw ngsw[] = {
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&ngdomain,
	.pr_protocol =		NG_CONTROL,
	.pr_flags =		PR_ATOMIC | PR_ADDR /* | PR_RIGHTS */,
	.pr_usrreqs =		&ngc_usrreqs
},
{
	.pr_type =		SOCK_DGRAM,
	.pr_domain =		&ngdomain,
	.pr_protocol =		NG_DATA,
	.pr_flags =		PR_ATOMIC | PR_ADDR,
	.pr_usrreqs =		&ngd_usrreqs
}
};

struct domain ngdomain = {
	.dom_family =		AF_NETGRAPH,
	.dom_name =		"netgraph",
	.dom_protosw =		ngsw,
	.dom_protoswNPROTOSW =	&ngsw[nitems(ngsw)]
};

/*
 * Handle loading and unloading for this node type.
 * This is to handle auxiliary linkages (e.g protocol domain addition).
 */
static int
ngs_mod_event(module_t mod, int event, void *data)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		mtx_init(&ngsocketlist_mtx, "ng_socketlist", NULL, MTX_DEF);
		break;
	case MOD_UNLOAD:
		/* Ensure there are no open netgraph sockets. */
		if (!LIST_EMPTY(&ngsocklist)) {
			error = EBUSY;
			break;
		}
#ifdef NOTYET
		/* Unregister protocol domain XXX can't do this yet.. */
#endif
		error = EBUSY;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

VNET_DOMAIN_SET(ng);

SYSCTL_INT(_net_graph, OID_AUTO, family, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, AF_NETGRAPH, "");
static SYSCTL_NODE(_net_graph, OID_AUTO, data, CTLFLAG_RW, 0, "DATA");
SYSCTL_INT(_net_graph_data, OID_AUTO, proto, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, NG_DATA, "");
static SYSCTL_NODE(_net_graph, OID_AUTO, control, CTLFLAG_RW, 0, "CONTROL");
SYSCTL_INT(_net_graph_control, OID_AUTO, proto, CTLFLAG_RD, SYSCTL_NULL_INT_PTR, NG_CONTROL, "");

