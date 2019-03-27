/*-
 * Copyright (c) 2014 Yandex LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Kernel interface tracking API.
 *
 */

#include "opt_ipfw.h"
#include "opt_inet.h"
#ifndef INET
#error IPFIREWALL requires INET.
#endif /* INET */
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/eventhandler.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>	/* struct ipfw_rule_ref */
#include <netinet/ip_fw.h>

#include <netpfil/ipfw/ip_fw_private.h>

#define	CHAIN_TO_II(ch)		((struct namedobj_instance *)ch->ifcfg)

#define	DEFAULT_IFACES	128

static void handle_ifdetach(struct ip_fw_chain *ch, struct ipfw_iface *iif,
    uint16_t ifindex);
static void handle_ifattach(struct ip_fw_chain *ch, struct ipfw_iface *iif,
    uint16_t ifindex);
static int list_ifaces(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd);

static struct ipfw_sopt_handler	scodes[] = {
	{ IP_FW_XIFLIST,	0,	HDIR_GET,	list_ifaces },
};

/*
 * FreeBSD Kernel interface.
 */
static void ipfw_kifhandler(void *arg, struct ifnet *ifp);
static int ipfw_kiflookup(char *name);
static void iface_khandler_register(void);
static void iface_khandler_deregister(void);

static eventhandler_tag ipfw_ifdetach_event, ipfw_ifattach_event;
static int num_vnets = 0;
static struct mtx vnet_mtx;

/*
 * Checks if kernel interface is contained in our tracked
 * interface list and calls attach/detach handler.
 */
static void
ipfw_kifhandler(void *arg, struct ifnet *ifp)
{
	struct ip_fw_chain *ch;
	struct ipfw_iface *iif;
	struct namedobj_instance *ii;
	uintptr_t htype;

	if (V_ipfw_vnet_ready == 0)
		return;

	ch = &V_layer3_chain;
	htype = (uintptr_t)arg;

	IPFW_UH_WLOCK(ch);
	ii = CHAIN_TO_II(ch);
	if (ii == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return;
	}
	iif = (struct ipfw_iface*)ipfw_objhash_lookup_name(ii, 0,
	    if_name(ifp));
	if (iif != NULL) {
		if (htype == 1)
			handle_ifattach(ch, iif, ifp->if_index);
		else
			handle_ifdetach(ch, iif, ifp->if_index);
	}
	IPFW_UH_WUNLOCK(ch);
}

/*
 * Reference current VNET as iface tracking API user.
 * Registers interface tracking handlers for first VNET.
 */
static void
iface_khandler_register()
{
	int create;

	create = 0;

	mtx_lock(&vnet_mtx);
	if (num_vnets == 0)
		create = 1;
	num_vnets++;
	mtx_unlock(&vnet_mtx);

	if (create == 0)
		return;

	printf("IPFW: starting up interface tracker\n");

	ipfw_ifdetach_event = EVENTHANDLER_REGISTER(
	    ifnet_departure_event, ipfw_kifhandler, NULL,
	    EVENTHANDLER_PRI_ANY);
	ipfw_ifattach_event = EVENTHANDLER_REGISTER(
	    ifnet_arrival_event, ipfw_kifhandler, (void*)((uintptr_t)1),
	    EVENTHANDLER_PRI_ANY);
}

/*
 *
 * Detach interface event handlers on last VNET instance
 * detach.
 */
static void
iface_khandler_deregister()
{
	int destroy;

	destroy = 0;
	mtx_lock(&vnet_mtx);
	if (num_vnets == 1)
		destroy = 1;
	num_vnets--;
	mtx_unlock(&vnet_mtx);

	if (destroy == 0)
		return;

	EVENTHANDLER_DEREGISTER(ifnet_arrival_event,
	    ipfw_ifattach_event);
	EVENTHANDLER_DEREGISTER(ifnet_departure_event,
	    ipfw_ifdetach_event);
}

/*
 * Retrieves ifindex for given @name.
 *
 * Returns ifindex or 0.
 */
static int
ipfw_kiflookup(char *name)
{
	struct ifnet *ifp;
	int ifindex;

	ifindex = 0;

	if ((ifp = ifunit_ref(name)) != NULL) {
		ifindex = ifp->if_index;
		if_rele(ifp);
	}

	return (ifindex);
}

/*
 * Global ipfw startup hook.
 * Since we perform lazy initialization, do nothing except
 * mutex init.
 */
int
ipfw_iface_init()
{

	mtx_init(&vnet_mtx, "IPFW ifhandler mtx", NULL, MTX_DEF);
	IPFW_ADD_SOPT_HANDLER(1, scodes);
	return (0);
}

/*
 * Global ipfw destroy hook.
 * Unregister khandlers iff init has been done.
 */
void
ipfw_iface_destroy()
{

	IPFW_DEL_SOPT_HANDLER(1, scodes);
	mtx_destroy(&vnet_mtx);
}

/*
 * Perform actual init on internal request.
 * Inits both namehash and global khandler.
 */
static void
vnet_ipfw_iface_init(struct ip_fw_chain *ch)
{
	struct namedobj_instance *ii;

	ii = ipfw_objhash_create(DEFAULT_IFACES);
	IPFW_UH_WLOCK(ch);
	if (ch->ifcfg == NULL) {
		ch->ifcfg = ii;
		ii = NULL;
	}
	IPFW_UH_WUNLOCK(ch);

	if (ii != NULL) {
		/* Already initialized. Free namehash. */
		ipfw_objhash_destroy(ii);
	} else {
		/* We're the first ones. Init kernel hooks. */
		iface_khandler_register();
	}
}

static int
destroy_iface(struct namedobj_instance *ii, struct named_object *no,
    void *arg)
{

	/* Assume all consumers have been already detached */
	free(no, M_IPFW);
	return (0);
}

/*
 * Per-VNET ipfw detach hook.
 *
 */
void
vnet_ipfw_iface_destroy(struct ip_fw_chain *ch)
{
	struct namedobj_instance *ii;

	IPFW_UH_WLOCK(ch);
	ii = CHAIN_TO_II(ch);
	ch->ifcfg = NULL;
	IPFW_UH_WUNLOCK(ch);

	if (ii != NULL) {
		ipfw_objhash_foreach(ii, destroy_iface, ch);
		ipfw_objhash_destroy(ii);
		iface_khandler_deregister();
	}
}

/*
 * Notify the subsystem that we are interested in tracking
 * interface @name. This function has to be called without
 * holding any locks to permit allocating the necessary states
 * for proper interface tracking.
 *
 * Returns 0 on success.
 */
int
ipfw_iface_ref(struct ip_fw_chain *ch, char *name,
    struct ipfw_ifc *ic)
{
	struct namedobj_instance *ii;
	struct ipfw_iface *iif, *tmp;

	if (strlen(name) >= sizeof(iif->ifname))
		return (EINVAL);

	IPFW_UH_WLOCK(ch);

	ii = CHAIN_TO_II(ch);
	if (ii == NULL) {

		/*
		 * First request to subsystem.
		 * Let's perform init.
		 */
		IPFW_UH_WUNLOCK(ch);
		vnet_ipfw_iface_init(ch);
		IPFW_UH_WLOCK(ch);
		ii = CHAIN_TO_II(ch);
	}

	iif = (struct ipfw_iface *)ipfw_objhash_lookup_name(ii, 0, name);

	if (iif != NULL) {
		iif->no.refcnt++;
		ic->iface = iif;
		IPFW_UH_WUNLOCK(ch);
		return (0);
	}

	IPFW_UH_WUNLOCK(ch);

	/* Not found. Let's create one */
	iif = malloc(sizeof(struct ipfw_iface), M_IPFW, M_WAITOK | M_ZERO);
	TAILQ_INIT(&iif->consumers);
	iif->no.name = iif->ifname;
	strlcpy(iif->ifname, name, sizeof(iif->ifname));

	/*
	 * Ref & link to the list.
	 *
	 * We assume  ifnet_arrival_event / ifnet_departure_event
	 * are not holding any locks.
	 */
	iif->no.refcnt = 1;
	IPFW_UH_WLOCK(ch);

	tmp = (struct ipfw_iface *)ipfw_objhash_lookup_name(ii, 0, name);
	if (tmp != NULL) {
		/* Interface has been created since unlock. Ref and return */
		tmp->no.refcnt++;
		ic->iface = tmp;
		IPFW_UH_WUNLOCK(ch);
		free(iif, M_IPFW);
		return (0);
	}

	iif->ifindex = ipfw_kiflookup(name);
	if (iif->ifindex != 0)
		iif->resolved = 1;

	ipfw_objhash_add(ii, &iif->no);
	ic->iface = iif;

	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Adds @ic to the list of iif interface consumers.
 * Must be called with holding both UH+WLOCK.
 * Callback may be immediately called (if interface exists).
 */
void
ipfw_iface_add_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic)
{
	struct ipfw_iface *iif;

	IPFW_UH_WLOCK_ASSERT(ch);
	IPFW_WLOCK_ASSERT(ch);

	iif = ic->iface;
	
	TAILQ_INSERT_TAIL(&iif->consumers, ic, next);
	if (iif->resolved != 0)
		ic->cb(ch, ic->cbdata, iif->ifindex);
}

/*
 * Unlinks interface tracker object @ic from interface.
 * Must be called while holding UH lock.
 */
void
ipfw_iface_del_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic)
{
	struct ipfw_iface *iif;

	IPFW_UH_WLOCK_ASSERT(ch);

	iif = ic->iface;
	TAILQ_REMOVE(&iif->consumers, ic, next);
}

/*
 * Unreference interface specified by @ic.
 * Must be called while holding UH lock.
 */
void
ipfw_iface_unref(struct ip_fw_chain *ch, struct ipfw_ifc *ic)
{
	struct ipfw_iface *iif;

	IPFW_UH_WLOCK_ASSERT(ch);

	iif = ic->iface;
	ic->iface = NULL;

	iif->no.refcnt--;
	/* TODO: check for references & delete */
}

/*
 * Interface arrival handler.
 */
static void
handle_ifattach(struct ip_fw_chain *ch, struct ipfw_iface *iif,
    uint16_t ifindex)
{
	struct ipfw_ifc *ic;

	IPFW_UH_WLOCK_ASSERT(ch);

	iif->gencnt++;
	iif->resolved = 1;
	iif->ifindex = ifindex;

	IPFW_WLOCK(ch);
	TAILQ_FOREACH(ic, &iif->consumers, next)
		ic->cb(ch, ic->cbdata, iif->ifindex);
	IPFW_WUNLOCK(ch);
}

/*
 * Interface departure handler.
 */
static void
handle_ifdetach(struct ip_fw_chain *ch, struct ipfw_iface *iif,
    uint16_t ifindex)
{
	struct ipfw_ifc *ic;

	IPFW_UH_WLOCK_ASSERT(ch);

	IPFW_WLOCK(ch);
	TAILQ_FOREACH(ic, &iif->consumers, next)
		ic->cb(ch, ic->cbdata, 0);
	IPFW_WUNLOCK(ch);

	iif->gencnt++;
	iif->resolved = 0;
	iif->ifindex = 0;
}

struct dump_iface_args {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
};

static int
export_iface_internal(struct namedobj_instance *ii, struct named_object *no,
    void *arg)
{
	ipfw_iface_info *i;
	struct dump_iface_args *da;
	struct ipfw_iface *iif;

	da = (struct dump_iface_args *)arg;

	i = (ipfw_iface_info *)ipfw_get_sopt_space(da->sd, sizeof(*i));
	KASSERT(i != NULL, ("previously checked buffer is not enough"));

	iif = (struct ipfw_iface *)no;

	strlcpy(i->ifname, iif->ifname, sizeof(i->ifname));
	if (iif->resolved)
		i->flags |= IPFW_IFFLAG_RESOLVED;
	i->ifindex = iif->ifindex;
	i->refcnt = iif->no.refcnt;
	i->gencnt = iif->gencnt;
	return (0);
}

/*
 * Lists all interface currently tracked by ipfw.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_iface_info x N ]
 *
 * Returns 0 on success
 */
static int
list_ifaces(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct namedobj_instance *ii;
	struct _ipfw_obj_lheader *olh;
	struct dump_iface_args da;
	uint32_t count, size;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	ii = CHAIN_TO_II(ch);
	if (ii != NULL)
		count = ipfw_objhash_count(ii);
	else
		count = 0;
	size = count * sizeof(ipfw_iface_info) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_iface_info);

	if (size > olh->size) {
		olh->size = size;
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	olh->size = size;

	da.ch = ch;
	da.sd = sd;

	if (ii != NULL)
		ipfw_objhash_foreach(ii, export_iface_internal, &da);
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

