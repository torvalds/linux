/*-
 * Copyright (c) 2014-2018, Matthew Macy <mmacy@mattmacy.io>
 * Copyright (C) 2017-2018 Joyent Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Neither the name of Matthew Macy nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_acpi.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/event.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/kobj.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>
#include <sys/limits.h>
#include <sys/queue.h>
#include <sys/jail.h>
#include <sys/md5.h>
#include <sys/proc.h>


#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_clone.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/vnet.h>

#include <net/iflib.h>
#include <net/iflib_private.h>
#include "ifdi_if.h"

int
noop_attach(device_t dev)
{
	return (0);
}

int
iflib_pseudo_detach(device_t dev)
{
	if_ctx_t ctx;
	uint32_t ifc_flags;

	ctx = device_get_softc(dev);
	ifc_flags = iflib_get_flags(ctx);
	if ((ifc_flags & IFC_INIT_DONE) == 0)
		return (0);
	return (IFDI_DETACH(ctx));
}

static device_t iflib_pseudodev;

static struct mtx pseudoif_mtx;
MTX_SYSINIT(pseudoif_mtx, &pseudoif_mtx, "pseudoif_mtx", MTX_DEF);

#define PSEUDO_LOCK() mtx_lock(&pseudoif_mtx);
#define PSEUDO_UNLOCK() mtx_unlock(&pseudoif_mtx);

struct if_pseudo {
	eventhandler_tag ip_detach_tag;
	eventhandler_tag ip_lladdr_tag;
	struct if_clone *ip_ifc;
	if_shared_ctx_t ip_sctx;
	devclass_t ip_dc;
	LIST_ENTRY(if_pseudo) ip_list;
	int ip_on_list;
};

static LIST_HEAD(, if_pseudo) iflib_pseudos = LIST_HEAD_INITIALIZER(iflib_pseudos);

/*
 * XXX this assumes that the rest of the
 * code won't hang on to it after it's
 * removed / unloaded
 */
static if_pseudo_t
iflib_ip_lookup(const char *name)
{
	if_pseudo_t ip = NULL;

	PSEUDO_LOCK();
	LIST_FOREACH(ip, &iflib_pseudos, ip_list) {
		if (!strcmp(ip->ip_sctx->isc_name, name))
			break;
	}
	PSEUDO_UNLOCK();
	return (ip);
}

static void
iflib_ip_delete(if_pseudo_t ip)
{
	PSEUDO_LOCK();
	if (ip->ip_on_list) {
		LIST_REMOVE(ip, ip_list);
		ip->ip_on_list = 0;
	}
	PSEUDO_UNLOCK();
}

static void
iflib_ip_insert(if_pseudo_t ip)
{
	PSEUDO_LOCK();
	if (!ip->ip_on_list) {
		LIST_INSERT_HEAD(&iflib_pseudos, ip, ip_list);
		ip->ip_on_list = 1;
	}
	PSEUDO_UNLOCK();
}

static void
iflib_ifdetach(void *arg __unused, struct ifnet *ifp)
{
	/* If the ifnet is just being renamed, don't do anything. */
	if (ifp->if_flags & IFF_RENAMING)
		return;
}

static void
iflib_iflladdr(void *arg __unused, struct ifnet *ifp)
{
}

static int
iflib_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	const char *name = ifc_name(ifc);
	struct iflib_cloneattach_ctx clctx;
	if_ctx_t ctx;
	if_pseudo_t ip;
	device_t dev;
	int rc;

	clctx.cc_ifc = ifc;
	clctx.cc_len = 0;
	clctx.cc_params = params;
	clctx.cc_name = name;

	if (__predict_false(iflib_pseudodev == NULL)) {
		/* SYSINIT initialization would panic !?! */
		mtx_lock(&Giant);
		iflib_pseudodev = device_add_child(root_bus, "ifpseudo", 0);
		mtx_unlock(&Giant);
		MPASS(iflib_pseudodev != NULL);
	}
	ip = iflib_ip_lookup(name);
	if (ip == NULL) {
		printf("no ip found for %s\n", name);
		return (ENOENT);
	}
	if ((dev = devclass_get_device(ip->ip_dc, unit)) != NULL) {
		printf("unit %d allocated\n", unit);
		bus_generic_print_child(iflib_pseudodev, dev);
		return (EBUSY);
	}
	PSEUDO_LOCK();
	dev = device_add_child(iflib_pseudodev, name, unit);
	device_set_driver(dev, &iflib_pseudodriver);
	PSEUDO_UNLOCK();
	device_quiet(dev);
	rc = device_attach(dev);
	MPASS(rc == 0);
	MPASS(dev != NULL);
	MPASS(devclass_get_device(ip->ip_dc, unit) == dev);
	rc = iflib_pseudo_register(dev, ip->ip_sctx, &ctx, &clctx);
	if (rc) {
		mtx_lock(&Giant);
		device_delete_child(iflib_pseudodev, dev);
		mtx_unlock(&Giant);
	} else
		device_set_softc(dev, ctx);

	return (rc);
}

static void
iflib_clone_destroy(struct ifnet *ifp)
{
	if_ctx_t ctx;
	device_t dev;
	struct sx *ctx_lock;
	int rc;
	/*
	 * Detach device / free / free unit 
	 *
	 */
	ctx = if_getsoftc(ifp);
	dev = iflib_get_dev(ctx);
	ctx_lock = iflib_ctx_lock_get(ctx);
	sx_xlock(ctx_lock);
	iflib_set_detach(ctx);
	iflib_stop(ctx);
	sx_xunlock(ctx_lock);

	mtx_lock(&Giant);
	rc = device_delete_child(iflib_pseudodev, dev);
	mtx_unlock(&Giant);
	if (rc == 0)
		iflib_pseudo_deregister(ctx);
}

if_pseudo_t
iflib_clone_register(if_shared_ctx_t sctx)
{
	if_pseudo_t ip;

	if (sctx->isc_name == NULL) {
		printf("iflib_clone_register failed - shared_ctx needs to have a device name\n");
		return (NULL);
	}
	if (iflib_ip_lookup(sctx->isc_name) != NULL) {
		printf("iflib_clone_register failed - shared_ctx %s alread registered\n",
			   sctx->isc_name);
		return (NULL);
	}
	ip = malloc(sizeof(*ip), M_IFLIB, M_WAITOK|M_ZERO);
	ip->ip_sctx = sctx;
	ip->ip_dc = devclass_create(sctx->isc_name);
	if (ip->ip_dc == NULL)
		goto fail_clone;
	/* XXX --- we can handle clone_advanced later */
	ip->ip_ifc  = if_clone_simple(sctx->isc_name, iflib_clone_create, iflib_clone_destroy, 0);
	if (ip->ip_ifc == NULL) {
		printf("clone_simple failed -- cloned %s  devices will not be available\n", sctx->isc_name);
		goto fail_clone;
	}
	ifc_flags_set(ip->ip_ifc, IFC_NOGROUP);
	ip->ip_lladdr_tag = EVENTHANDLER_REGISTER(iflladdr_event,
											 iflib_iflladdr, NULL, EVENTHANDLER_PRI_ANY);
	if (ip->ip_lladdr_tag == NULL)
		goto fail_addr;
	ip->ip_detach_tag = EVENTHANDLER_REGISTER(ifnet_departure_event,
											 iflib_ifdetach, NULL, EVENTHANDLER_PRI_ANY);

	if (ip->ip_detach_tag == NULL)
		goto fail_depart;

	iflib_ip_insert(ip);
	return (ip);
 fail_depart:
	EVENTHANDLER_DEREGISTER(iflladdr_event, ip->ip_lladdr_tag);
 fail_addr:
	if_clone_detach(ip->ip_ifc);
 fail_clone:
	free(ip, M_IFLIB);
	return (NULL);
}

void
iflib_clone_deregister(if_pseudo_t ip)
{
	/* XXX check that is not still in use */
	iflib_ip_delete(ip);
	EVENTHANDLER_DEREGISTER(ifnet_departure_event, ip->ip_detach_tag);
	EVENTHANDLER_DEREGISTER(iflladdr_event, ip->ip_lladdr_tag);
	if_clone_detach(ip->ip_ifc);
	/* XXX free devclass */
	free(ip, M_IFLIB);
}
