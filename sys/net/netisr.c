/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Robert N. M. Watson
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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
 * netisr is a packet dispatch service, allowing synchronous (directly
 * dispatched) and asynchronous (deferred dispatch) processing of packets by
 * registered protocol handlers.  Callers pass a protocol identifier and
 * packet to netisr, along with a direct dispatch hint, and work will either
 * be immediately processed by the registered handler, or passed to a
 * software interrupt (SWI) thread for deferred dispatch.  Callers will
 * generally select one or the other based on:
 *
 * - Whether directly dispatching a netisr handler lead to code reentrance or
 *   lock recursion, such as entering the socket code from the socket code.
 * - Whether directly dispatching a netisr handler lead to recursive
 *   processing, such as when decapsulating several wrapped layers of tunnel
 *   information (IPSEC within IPSEC within ...).
 *
 * Maintaining ordering for protocol streams is a critical design concern.
 * Enforcing ordering limits the opportunity for concurrency, but maintains
 * the strong ordering requirements found in some protocols, such as TCP.  Of
 * related concern is CPU affinity--it is desirable to process all data
 * associated with a particular stream on the same CPU over time in order to
 * avoid acquiring locks associated with the connection on different CPUs,
 * keep connection data in one cache, and to generally encourage associated
 * user threads to live on the same CPU as the stream.  It's also desirable
 * to avoid lock migration and contention where locks are associated with
 * more than one flow.
 *
 * netisr supports several policy variations, represented by the
 * NETISR_POLICY_* constants, allowing protocols to play various roles in
 * identifying flows, assigning work to CPUs, etc.  These are described in
 * netisr.h.
 */

#include "opt_ddb.h"
#include "opt_device_polling.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/interrupt.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#define	_WANT_NETISR_INTERNAL	/* Enable definitions from netisr_internal.h */
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/netisr_internal.h>
#include <net/vnet.h>

/*-
 * Synchronize use and modification of the registered netisr data structures;
 * acquire a read lock while modifying the set of registered protocols to
 * prevent partially registered or unregistered protocols from being run.
 *
 * The following data structures and fields are protected by this lock:
 *
 * - The netisr_proto array, including all fields of struct netisr_proto.
 * - The nws array, including all fields of struct netisr_worker.
 * - The nws_array array.
 *
 * Note: the NETISR_LOCKING define controls whether read locks are acquired
 * in packet processing paths requiring netisr registration stability.  This
 * is disabled by default as it can lead to measurable performance
 * degradation even with rmlocks (3%-6% for loopback ping-pong traffic), and
 * because netisr registration and unregistration is extremely rare at
 * runtime.  If it becomes more common, this decision should be revisited.
 *
 * XXXRW: rmlocks don't support assertions.
 */
static struct rmlock	netisr_rmlock;
#define	NETISR_LOCK_INIT()	rm_init_flags(&netisr_rmlock, "netisr", \
				    RM_NOWITNESS)
#define	NETISR_LOCK_ASSERT()
#define	NETISR_RLOCK(tracker)	rm_rlock(&netisr_rmlock, (tracker))
#define	NETISR_RUNLOCK(tracker)	rm_runlock(&netisr_rmlock, (tracker))
#define	NETISR_WLOCK()		rm_wlock(&netisr_rmlock)
#define	NETISR_WUNLOCK()	rm_wunlock(&netisr_rmlock)
/* #define	NETISR_LOCKING */

static SYSCTL_NODE(_net, OID_AUTO, isr, CTLFLAG_RW, 0, "netisr");

/*-
 * Three global direct dispatch policies are supported:
 *
 * NETISR_DISPATCH_DEFERRED: All work is deferred for a netisr, regardless of
 * context (may be overriden by protocols).
 *
 * NETISR_DISPATCH_HYBRID: If the executing context allows direct dispatch,
 * and we're running on the CPU the work would be performed on, then direct
 * dispatch it if it wouldn't violate ordering constraints on the workstream.
 *
 * NETISR_DISPATCH_DIRECT: If the executing context allows direct dispatch,
 * always direct dispatch.  (The default.)
 *
 * Notice that changing the global policy could lead to short periods of
 * misordered processing, but this is considered acceptable as compared to
 * the complexity of enforcing ordering during policy changes.  Protocols can
 * override the global policy (when they're not doing that, they select
 * NETISR_DISPATCH_DEFAULT).
 */
#define	NETISR_DISPATCH_POLICY_DEFAULT	NETISR_DISPATCH_DIRECT
#define	NETISR_DISPATCH_POLICY_MAXSTR	20 /* Used for temporary buffers. */
static u_int	netisr_dispatch_policy = NETISR_DISPATCH_POLICY_DEFAULT;
static int	sysctl_netisr_dispatch_policy(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_isr, OID_AUTO, dispatch, CTLTYPE_STRING | CTLFLAG_RWTUN,
    0, 0, sysctl_netisr_dispatch_policy, "A",
    "netisr dispatch policy");

/*
 * Allow the administrator to limit the number of threads (CPUs) to use for
 * netisr.  We don't check netisr_maxthreads before creating the thread for
 * CPU 0. This must be set at boot. We will create at most one thread per CPU.
 * By default we initialize this to 1 which would assign just 1 cpu (cpu0) and
 * therefore only 1 workstream. If set to -1, netisr would use all cpus
 * (mp_ncpus) and therefore would have those many workstreams. One workstream
 * per thread (CPU).
 */
static int	netisr_maxthreads = 1;		/* Max number of threads. */
SYSCTL_INT(_net_isr, OID_AUTO, maxthreads, CTLFLAG_RDTUN,
    &netisr_maxthreads, 0,
    "Use at most this many CPUs for netisr processing");

static int	netisr_bindthreads = 0;		/* Bind threads to CPUs. */
SYSCTL_INT(_net_isr, OID_AUTO, bindthreads, CTLFLAG_RDTUN,
    &netisr_bindthreads, 0, "Bind netisr threads to CPUs.");

/*
 * Limit per-workstream mbuf queue limits s to at most net.isr.maxqlimit,
 * both for initial configuration and later modification using
 * netisr_setqlimit().
 */
#define	NETISR_DEFAULT_MAXQLIMIT	10240
static u_int	netisr_maxqlimit = NETISR_DEFAULT_MAXQLIMIT;
SYSCTL_UINT(_net_isr, OID_AUTO, maxqlimit, CTLFLAG_RDTUN,
    &netisr_maxqlimit, 0,
    "Maximum netisr per-protocol, per-CPU queue depth.");

/*
 * The default per-workstream mbuf queue limit for protocols that don't
 * initialize the nh_qlimit field of their struct netisr_handler.  If this is
 * set above netisr_maxqlimit, we truncate it to the maximum during boot.
 */
#define	NETISR_DEFAULT_DEFAULTQLIMIT	256
static u_int	netisr_defaultqlimit = NETISR_DEFAULT_DEFAULTQLIMIT;
SYSCTL_UINT(_net_isr, OID_AUTO, defaultqlimit, CTLFLAG_RDTUN,
    &netisr_defaultqlimit, 0,
    "Default netisr per-protocol, per-CPU queue limit if not set by protocol");

/*
 * Store and export the compile-time constant NETISR_MAXPROT limit on the
 * number of protocols that can register with netisr at a time.  This is
 * required for crashdump analysis, as it sizes netisr_proto[].
 */
static u_int	netisr_maxprot = NETISR_MAXPROT;
SYSCTL_UINT(_net_isr, OID_AUTO, maxprot, CTLFLAG_RD,
    &netisr_maxprot, 0,
    "Compile-time limit on the number of protocols supported by netisr.");

/*
 * The netisr_proto array describes all registered protocols, indexed by
 * protocol number.  See netisr_internal.h for more details.
 */
static struct netisr_proto	netisr_proto[NETISR_MAXPROT];

#ifdef VIMAGE
/*
 * The netisr_enable array describes a per-VNET flag for registered
 * protocols on whether this netisr is active in this VNET or not.
 * netisr_register() will automatically enable the netisr for the
 * default VNET and all currently active instances.
 * netisr_unregister() will disable all active VNETs, including vnet0.
 * Individual network stack instances can be enabled/disabled by the
 * netisr_(un)register _vnet() functions.
 * With this we keep the one netisr_proto per protocol but add a
 * mechanism to stop netisr processing for vnet teardown.
 * Apart from that we expect a VNET to always be enabled.
 */
VNET_DEFINE_STATIC(u_int,	netisr_enable[NETISR_MAXPROT]);
#define	V_netisr_enable		VNET(netisr_enable)
#endif

/*
 * Per-CPU workstream data.  See netisr_internal.h for more details.
 */
DPCPU_DEFINE(struct netisr_workstream, nws);

/*
 * Map contiguous values between 0 and nws_count into CPU IDs appropriate for
 * accessing workstreams.  This allows constructions of the form
 * DPCPU_ID_GET(nws_array[arbitraryvalue % nws_count], nws).
 */
static u_int				 nws_array[MAXCPU];

/*
 * Number of registered workstreams.  Will be at most the number of running
 * CPUs once fully started.
 */
static u_int				 nws_count;
SYSCTL_UINT(_net_isr, OID_AUTO, numthreads, CTLFLAG_RD,
    &nws_count, 0, "Number of extant netisr threads.");

/*
 * Synchronization for each workstream: a mutex protects all mutable fields
 * in each stream, including per-protocol state (mbuf queues).  The SWI is
 * woken up if asynchronous dispatch is required.
 */
#define	NWS_LOCK(s)		mtx_lock(&(s)->nws_mtx)
#define	NWS_LOCK_ASSERT(s)	mtx_assert(&(s)->nws_mtx, MA_OWNED)
#define	NWS_UNLOCK(s)		mtx_unlock(&(s)->nws_mtx)
#define	NWS_SIGNAL(s)		swi_sched((s)->nws_swi_cookie, 0)

/*
 * Utility routines for protocols that implement their own mapping of flows
 * to CPUs.
 */
u_int
netisr_get_cpucount(void)
{

	return (nws_count);
}

u_int
netisr_get_cpuid(u_int cpunumber)
{

	return (nws_array[cpunumber % nws_count]);
}

/*
 * The default implementation of flow -> CPU ID mapping.
 *
 * Non-static so that protocols can use it to map their own work to specific
 * CPUs in a manner consistent to netisr for affinity purposes.
 */
u_int
netisr_default_flow2cpu(u_int flowid)
{

	return (nws_array[flowid % nws_count]);
}

/*
 * Dispatch tunable and sysctl configuration.
 */
struct netisr_dispatch_table_entry {
	u_int		 ndte_policy;
	const char	*ndte_policy_str;
};
static const struct netisr_dispatch_table_entry netisr_dispatch_table[] = {
	{ NETISR_DISPATCH_DEFAULT, "default" },
	{ NETISR_DISPATCH_DEFERRED, "deferred" },
	{ NETISR_DISPATCH_HYBRID, "hybrid" },
	{ NETISR_DISPATCH_DIRECT, "direct" },
};

static void
netisr_dispatch_policy_to_str(u_int dispatch_policy, char *buffer,
    u_int buflen)
{
	const struct netisr_dispatch_table_entry *ndtep;
	const char *str;
	u_int i;

	str = "unknown";
	for (i = 0; i < nitems(netisr_dispatch_table); i++) {
		ndtep = &netisr_dispatch_table[i];
		if (ndtep->ndte_policy == dispatch_policy) {
			str = ndtep->ndte_policy_str;
			break;
		}
	}
	snprintf(buffer, buflen, "%s", str);
}

static int
netisr_dispatch_policy_from_str(const char *str, u_int *dispatch_policyp)
{
	const struct netisr_dispatch_table_entry *ndtep;
	u_int i;

	for (i = 0; i < nitems(netisr_dispatch_table); i++) {
		ndtep = &netisr_dispatch_table[i];
		if (strcmp(ndtep->ndte_policy_str, str) == 0) {
			*dispatch_policyp = ndtep->ndte_policy;
			return (0);
		}
	}
	return (EINVAL);
}

static int
sysctl_netisr_dispatch_policy(SYSCTL_HANDLER_ARGS)
{
	char tmp[NETISR_DISPATCH_POLICY_MAXSTR];
	u_int dispatch_policy;
	int error;

	netisr_dispatch_policy_to_str(netisr_dispatch_policy, tmp,
	    sizeof(tmp));
	error = sysctl_handle_string(oidp, tmp, sizeof(tmp), req);
	if (error == 0 && req->newptr != NULL) {
		error = netisr_dispatch_policy_from_str(tmp,
		    &dispatch_policy);
		if (error == 0 && dispatch_policy == NETISR_DISPATCH_DEFAULT)
			error = EINVAL;
		if (error == 0)
			netisr_dispatch_policy = dispatch_policy;
	}
	return (error);
}

/*
 * Register a new netisr handler, which requires initializing per-protocol
 * fields for each workstream.  All netisr work is briefly suspended while
 * the protocol is installed.
 */
void
netisr_register(const struct netisr_handler *nhp)
{
	VNET_ITERATOR_DECL(vnet_iter);
	struct netisr_work *npwp;
	const char *name;
	u_int i, proto;

	proto = nhp->nh_proto;
	name = nhp->nh_name;

	/*
	 * Test that the requested registration is valid.
	 */
	KASSERT(nhp->nh_name != NULL,
	    ("%s: nh_name NULL for %u", __func__, proto));
	KASSERT(nhp->nh_handler != NULL,
	    ("%s: nh_handler NULL for %s", __func__, name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_SOURCE ||
	    nhp->nh_policy == NETISR_POLICY_FLOW ||
	    nhp->nh_policy == NETISR_POLICY_CPU,
	    ("%s: unsupported nh_policy %u for %s", __func__,
	    nhp->nh_policy, name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_FLOW ||
	    nhp->nh_m2flow == NULL,
	    ("%s: nh_policy != FLOW but m2flow defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_policy == NETISR_POLICY_CPU || nhp->nh_m2cpuid == NULL,
	    ("%s: nh_policy != CPU but m2cpuid defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_policy != NETISR_POLICY_CPU || nhp->nh_m2cpuid != NULL,
	    ("%s: nh_policy == CPU but m2cpuid not defined for %s", __func__,
	    name));
	KASSERT(nhp->nh_dispatch == NETISR_DISPATCH_DEFAULT ||
	    nhp->nh_dispatch == NETISR_DISPATCH_DEFERRED ||
	    nhp->nh_dispatch == NETISR_DISPATCH_HYBRID ||
	    nhp->nh_dispatch == NETISR_DISPATCH_DIRECT,
	    ("%s: invalid nh_dispatch (%u)", __func__, nhp->nh_dispatch));

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u, %s): protocol too big", __func__, proto, name));

	/*
	 * Test that no existing registration exists for this protocol.
	 */
	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_name == NULL,
	    ("%s(%u, %s): name present", __func__, proto, name));
	KASSERT(netisr_proto[proto].np_handler == NULL,
	    ("%s(%u, %s): handler present", __func__, proto, name));

	netisr_proto[proto].np_name = name;
	netisr_proto[proto].np_handler = nhp->nh_handler;
	netisr_proto[proto].np_m2flow = nhp->nh_m2flow;
	netisr_proto[proto].np_m2cpuid = nhp->nh_m2cpuid;
	netisr_proto[proto].np_drainedcpu = nhp->nh_drainedcpu;
	if (nhp->nh_qlimit == 0)
		netisr_proto[proto].np_qlimit = netisr_defaultqlimit;
	else if (nhp->nh_qlimit > netisr_maxqlimit) {
		printf("%s: %s requested queue limit %u capped to "
		    "net.isr.maxqlimit %u\n", __func__, name, nhp->nh_qlimit,
		    netisr_maxqlimit);
		netisr_proto[proto].np_qlimit = netisr_maxqlimit;
	} else
		netisr_proto[proto].np_qlimit = nhp->nh_qlimit;
	netisr_proto[proto].np_policy = nhp->nh_policy;
	netisr_proto[proto].np_dispatch = nhp->nh_dispatch;
	CPU_FOREACH(i) {
		npwp = &(DPCPU_ID_PTR(i, nws))->nws_work[proto];
		bzero(npwp, sizeof(*npwp));
		npwp->nw_qlimit = netisr_proto[proto].np_qlimit;
	}

#ifdef VIMAGE
	/*
	 * Test that we are in vnet0 and have a curvnet set.
	 */
	KASSERT(curvnet != NULL, ("%s: curvnet is NULL", __func__));
	KASSERT(IS_DEFAULT_VNET(curvnet), ("%s: curvnet %p is not vnet0 %p",
	    __func__, curvnet, vnet0));
	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		V_netisr_enable[proto] = 1;
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
#endif
	NETISR_WUNLOCK();
}

/*
 * Clear drop counters across all workstreams for a protocol.
 */
void
netisr_clearqdrops(const struct netisr_handler *nhp)
{
	struct netisr_work *npwp;
#ifdef INVARIANTS
	const char *name;
#endif
	u_int i, proto;

	proto = nhp->nh_proto;
#ifdef INVARIANTS
	name = nhp->nh_name;
#endif
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));

	CPU_FOREACH(i) {
		npwp = &(DPCPU_ID_PTR(i, nws))->nws_work[proto];
		npwp->nw_qdrops = 0;
	}
	NETISR_WUNLOCK();
}

/*
 * Query current drop counters across all workstreams for a protocol.
 */
void
netisr_getqdrops(const struct netisr_handler *nhp, u_int64_t *qdropp)
{
	struct netisr_work *npwp;
	struct rm_priotracker tracker;
#ifdef INVARIANTS
	const char *name;
#endif
	u_int i, proto;

	*qdropp = 0;
	proto = nhp->nh_proto;
#ifdef INVARIANTS
	name = nhp->nh_name;
#endif
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_RLOCK(&tracker);
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));

	CPU_FOREACH(i) {
		npwp = &(DPCPU_ID_PTR(i, nws))->nws_work[proto];
		*qdropp += npwp->nw_qdrops;
	}
	NETISR_RUNLOCK(&tracker);
}

/*
 * Query current per-workstream queue limit for a protocol.
 */
void
netisr_getqlimit(const struct netisr_handler *nhp, u_int *qlimitp)
{
	struct rm_priotracker tracker;
#ifdef INVARIANTS
	const char *name;
#endif
	u_int proto;

	proto = nhp->nh_proto;
#ifdef INVARIANTS
	name = nhp->nh_name;
#endif
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_RLOCK(&tracker);
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));
	*qlimitp = netisr_proto[proto].np_qlimit;
	NETISR_RUNLOCK(&tracker);
}

/*
 * Update the queue limit across per-workstream queues for a protocol.  We
 * simply change the limits, and don't drain overflowed packets as they will
 * (hopefully) take care of themselves shortly.
 */
int
netisr_setqlimit(const struct netisr_handler *nhp, u_int qlimit)
{
	struct netisr_work *npwp;
#ifdef INVARIANTS
	const char *name;
#endif
	u_int i, proto;

	if (qlimit > netisr_maxqlimit)
		return (EINVAL);

	proto = nhp->nh_proto;
#ifdef INVARIANTS
	name = nhp->nh_name;
#endif
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));

	netisr_proto[proto].np_qlimit = qlimit;
	CPU_FOREACH(i) {
		npwp = &(DPCPU_ID_PTR(i, nws))->nws_work[proto];
		npwp->nw_qlimit = qlimit;
	}
	NETISR_WUNLOCK();
	return (0);
}

/*
 * Drain all packets currently held in a particular protocol work queue.
 */
static void
netisr_drain_proto(struct netisr_work *npwp)
{
	struct mbuf *m;

	/*
	 * We would assert the lock on the workstream but it's not passed in.
	 */
	while ((m = npwp->nw_head) != NULL) {
		npwp->nw_head = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if (npwp->nw_head == NULL)
			npwp->nw_tail = NULL;
		npwp->nw_len--;
		m_freem(m);
	}
	KASSERT(npwp->nw_tail == NULL, ("%s: tail", __func__));
	KASSERT(npwp->nw_len == 0, ("%s: len", __func__));
}

/*
 * Remove the registration of a network protocol, which requires clearing
 * per-protocol fields across all workstreams, including freeing all mbufs in
 * the queues at time of unregister.  All work in netisr is briefly suspended
 * while this takes place.
 */
void
netisr_unregister(const struct netisr_handler *nhp)
{
	VNET_ITERATOR_DECL(vnet_iter);
	struct netisr_work *npwp;
#ifdef INVARIANTS
	const char *name;
#endif
	u_int i, proto;

	proto = nhp->nh_proto;
#ifdef INVARIANTS
	name = nhp->nh_name;
#endif
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, name));

	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    name));

#ifdef VIMAGE
	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		V_netisr_enable[proto] = 0;
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
#endif

	netisr_proto[proto].np_name = NULL;
	netisr_proto[proto].np_handler = NULL;
	netisr_proto[proto].np_m2flow = NULL;
	netisr_proto[proto].np_m2cpuid = NULL;
	netisr_proto[proto].np_qlimit = 0;
	netisr_proto[proto].np_policy = 0;
	CPU_FOREACH(i) {
		npwp = &(DPCPU_ID_PTR(i, nws))->nws_work[proto];
		netisr_drain_proto(npwp);
		bzero(npwp, sizeof(*npwp));
	}
	NETISR_WUNLOCK();
}

#ifdef VIMAGE
void
netisr_register_vnet(const struct netisr_handler *nhp)
{
	u_int proto;

	proto = nhp->nh_proto;

	KASSERT(curvnet != NULL, ("%s: curvnet is NULL", __func__));
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, nhp->nh_name));
	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    nhp->nh_name));
	
	V_netisr_enable[proto] = 1;
	NETISR_WUNLOCK();
}

static void
netisr_drain_proto_vnet(struct vnet *vnet, u_int proto)
{
	struct netisr_workstream *nwsp;
	struct netisr_work *npwp;
	struct mbuf *m, *mp, *n, *ne;
	u_int i;

	KASSERT(vnet != NULL, ("%s: vnet is NULL", __func__));
	NETISR_LOCK_ASSERT();

	CPU_FOREACH(i) {
		nwsp = DPCPU_ID_PTR(i, nws);
		if (nwsp->nws_intr_event == NULL)
			continue;
		npwp = &nwsp->nws_work[proto];
		NWS_LOCK(nwsp);

		/*
		 * Rather than dissecting and removing mbufs from the middle
		 * of the chain, we build a new chain if the packet stays and
		 * update the head and tail pointers at the end.  All packets
		 * matching the given vnet are freed.
		 */
		m = npwp->nw_head;
		n = ne = NULL;
		while (m != NULL) {
			mp = m;
			m = m->m_nextpkt;
			mp->m_nextpkt = NULL;
			if (mp->m_pkthdr.rcvif->if_vnet != vnet) {
				if (n == NULL) {
					n = ne = mp;
				} else {
					ne->m_nextpkt = mp;
					ne = mp;
				}
				continue;
			}
			/* This is a packet in the selected vnet. Free it. */
			npwp->nw_len--;
			m_freem(mp);
		}
		npwp->nw_head = n;
		npwp->nw_tail = ne;
		NWS_UNLOCK(nwsp);
	}
}

void
netisr_unregister_vnet(const struct netisr_handler *nhp)
{
	u_int proto;

	proto = nhp->nh_proto;

	KASSERT(curvnet != NULL, ("%s: curvnet is NULL", __func__));
	KASSERT(proto < NETISR_MAXPROT,
	    ("%s(%u): protocol too big for %s", __func__, proto, nhp->nh_name));
	NETISR_WLOCK();
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s(%u): protocol not registered for %s", __func__, proto,
	    nhp->nh_name));
	
	V_netisr_enable[proto] = 0;

	netisr_drain_proto_vnet(curvnet, proto);
	NETISR_WUNLOCK();
}
#endif

/*
 * Compose the global and per-protocol policies on dispatch, and return the
 * dispatch policy to use.
 */
static u_int
netisr_get_dispatch(struct netisr_proto *npp)
{

	/*
	 * Protocol-specific configuration overrides the global default.
	 */
	if (npp->np_dispatch != NETISR_DISPATCH_DEFAULT)
		return (npp->np_dispatch);
	return (netisr_dispatch_policy);
}

/*
 * Look up the workstream given a packet and source identifier.  Do this by
 * checking the protocol's policy, and optionally call out to the protocol
 * for assistance if required.
 */
static struct mbuf *
netisr_select_cpuid(struct netisr_proto *npp, u_int dispatch_policy,
    uintptr_t source, struct mbuf *m, u_int *cpuidp)
{
	struct ifnet *ifp;
	u_int policy;

	NETISR_LOCK_ASSERT();

	/*
	 * In the event we have only one worker, shortcut and deliver to it
	 * without further ado.
	 */
	if (nws_count == 1) {
		*cpuidp = nws_array[0];
		return (m);
	}

	/*
	 * What happens next depends on the policy selected by the protocol.
	 * If we want to support per-interface policies, we should do that
	 * here first.
	 */
	policy = npp->np_policy;
	if (policy == NETISR_POLICY_CPU) {
		m = npp->np_m2cpuid(m, source, cpuidp);
		if (m == NULL)
			return (NULL);

		/*
		 * It's possible for a protocol not to have a good idea about
		 * where to process a packet, in which case we fall back on
		 * the netisr code to decide.  In the hybrid case, return the
		 * current CPU ID, which will force an immediate direct
		 * dispatch.  In the queued case, fall back on the SOURCE
		 * policy.
		 */
		if (*cpuidp != NETISR_CPUID_NONE) {
			*cpuidp = netisr_get_cpuid(*cpuidp);
			return (m);
		}
		if (dispatch_policy == NETISR_DISPATCH_HYBRID) {
			*cpuidp = netisr_get_cpuid(curcpu);
			return (m);
		}
		policy = NETISR_POLICY_SOURCE;
	}

	if (policy == NETISR_POLICY_FLOW) {
		if (M_HASHTYPE_GET(m) == M_HASHTYPE_NONE &&
		    npp->np_m2flow != NULL) {
			m = npp->np_m2flow(m, source);
			if (m == NULL)
				return (NULL);
		}
		if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) {
			*cpuidp =
			    netisr_default_flow2cpu(m->m_pkthdr.flowid);
			return (m);
		}
		policy = NETISR_POLICY_SOURCE;
	}

	KASSERT(policy == NETISR_POLICY_SOURCE,
	    ("%s: invalid policy %u for %s", __func__, npp->np_policy,
	    npp->np_name));

	ifp = m->m_pkthdr.rcvif;
	if (ifp != NULL)
		*cpuidp = nws_array[(ifp->if_index + source) % nws_count];
	else
		*cpuidp = nws_array[source % nws_count];
	return (m);
}

/*
 * Process packets associated with a workstream and protocol.  For reasons of
 * fairness, we process up to one complete netisr queue at a time, moving the
 * queue to a stack-local queue for processing, but do not loop refreshing
 * from the global queue.  The caller is responsible for deciding whether to
 * loop, and for setting the NWS_RUNNING flag.  The passed workstream will be
 * locked on entry and relocked before return, but will be released while
 * processing.  The number of packets processed is returned.
 */
static u_int
netisr_process_workstream_proto(struct netisr_workstream *nwsp, u_int proto)
{
	struct netisr_work local_npw, *npwp;
	u_int handled;
	struct mbuf *m;

	NETISR_LOCK_ASSERT();
	NWS_LOCK_ASSERT(nwsp);

	KASSERT(nwsp->nws_flags & NWS_RUNNING,
	    ("%s(%u): not running", __func__, proto));
	KASSERT(proto >= 0 && proto < NETISR_MAXPROT,
	    ("%s(%u): invalid proto\n", __func__, proto));

	npwp = &nwsp->nws_work[proto];
	if (npwp->nw_len == 0)
		return (0);

	/*
	 * Move the global work queue to a thread-local work queue.
	 *
	 * Notice that this means the effective maximum length of the queue
	 * is actually twice that of the maximum queue length specified in
	 * the protocol registration call.
	 */
	handled = npwp->nw_len;
	local_npw = *npwp;
	npwp->nw_head = NULL;
	npwp->nw_tail = NULL;
	npwp->nw_len = 0;
	nwsp->nws_pendingbits &= ~(1 << proto);
	NWS_UNLOCK(nwsp);
	while ((m = local_npw.nw_head) != NULL) {
		local_npw.nw_head = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if (local_npw.nw_head == NULL)
			local_npw.nw_tail = NULL;
		local_npw.nw_len--;
		VNET_ASSERT(m->m_pkthdr.rcvif != NULL,
		    ("%s:%d rcvif == NULL: m=%p", __func__, __LINE__, m));
		CURVNET_SET(m->m_pkthdr.rcvif->if_vnet);
		netisr_proto[proto].np_handler(m);
		CURVNET_RESTORE();
	}
	KASSERT(local_npw.nw_len == 0,
	    ("%s(%u): len %u", __func__, proto, local_npw.nw_len));
	if (netisr_proto[proto].np_drainedcpu)
		netisr_proto[proto].np_drainedcpu(nwsp->nws_cpu);
	NWS_LOCK(nwsp);
	npwp->nw_handled += handled;
	return (handled);
}

/*
 * SWI handler for netisr -- processes packets in a set of workstreams that
 * it owns, woken up by calls to NWS_SIGNAL().  If this workstream is already
 * being direct dispatched, go back to sleep and wait for the dispatching
 * thread to wake us up again.
 */
static void
swi_net(void *arg)
{
#ifdef NETISR_LOCKING
	struct rm_priotracker tracker;
#endif
	struct netisr_workstream *nwsp;
	u_int bits, prot;

	nwsp = arg;

#ifdef DEVICE_POLLING
	KASSERT(nws_count == 1,
	    ("%s: device_polling but nws_count != 1", __func__));
	netisr_poll();
#endif
#ifdef NETISR_LOCKING
	NETISR_RLOCK(&tracker);
#endif
	NWS_LOCK(nwsp);
	KASSERT(!(nwsp->nws_flags & NWS_RUNNING), ("swi_net: running"));
	if (nwsp->nws_flags & NWS_DISPATCHING)
		goto out;
	nwsp->nws_flags |= NWS_RUNNING;
	nwsp->nws_flags &= ~NWS_SCHEDULED;
	while ((bits = nwsp->nws_pendingbits) != 0) {
		while ((prot = ffs(bits)) != 0) {
			prot--;
			bits &= ~(1 << prot);
			(void)netisr_process_workstream_proto(nwsp, prot);
		}
	}
	nwsp->nws_flags &= ~NWS_RUNNING;
out:
	NWS_UNLOCK(nwsp);
#ifdef NETISR_LOCKING
	NETISR_RUNLOCK(&tracker);
#endif
#ifdef DEVICE_POLLING
	netisr_pollmore();
#endif
}

static int
netisr_queue_workstream(struct netisr_workstream *nwsp, u_int proto,
    struct netisr_work *npwp, struct mbuf *m, int *dosignalp)
{

	NWS_LOCK_ASSERT(nwsp);

	*dosignalp = 0;
	if (npwp->nw_len < npwp->nw_qlimit) {
		m->m_nextpkt = NULL;
		if (npwp->nw_head == NULL) {
			npwp->nw_head = m;
			npwp->nw_tail = m;
		} else {
			npwp->nw_tail->m_nextpkt = m;
			npwp->nw_tail = m;
		}
		npwp->nw_len++;
		if (npwp->nw_len > npwp->nw_watermark)
			npwp->nw_watermark = npwp->nw_len;

		/*
		 * We must set the bit regardless of NWS_RUNNING, so that
		 * swi_net() keeps calling netisr_process_workstream_proto().
		 */
		nwsp->nws_pendingbits |= (1 << proto);
		if (!(nwsp->nws_flags & 
		    (NWS_RUNNING | NWS_DISPATCHING | NWS_SCHEDULED))) {
			nwsp->nws_flags |= NWS_SCHEDULED;
			*dosignalp = 1;	/* Defer until unlocked. */
		}
		npwp->nw_queued++;
		return (0);
	} else {
		m_freem(m);
		npwp->nw_qdrops++;
		return (ENOBUFS);
	}
}

static int
netisr_queue_internal(u_int proto, struct mbuf *m, u_int cpuid)
{
	struct netisr_workstream *nwsp;
	struct netisr_work *npwp;
	int dosignal, error;

#ifdef NETISR_LOCKING
	NETISR_LOCK_ASSERT();
#endif
	KASSERT(cpuid <= mp_maxid, ("%s: cpuid too big (%u, %u)", __func__,
	    cpuid, mp_maxid));
	KASSERT(!CPU_ABSENT(cpuid), ("%s: CPU %u absent", __func__, cpuid));

	dosignal = 0;
	error = 0;
	nwsp = DPCPU_ID_PTR(cpuid, nws);
	npwp = &nwsp->nws_work[proto];
	NWS_LOCK(nwsp);
	error = netisr_queue_workstream(nwsp, proto, npwp, m, &dosignal);
	NWS_UNLOCK(nwsp);
	if (dosignal)
		NWS_SIGNAL(nwsp);
	return (error);
}

int
netisr_queue_src(u_int proto, uintptr_t source, struct mbuf *m)
{
#ifdef NETISR_LOCKING
	struct rm_priotracker tracker;
#endif
	u_int cpuid;
	int error;

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s: invalid proto %u", __func__, proto));

#ifdef NETISR_LOCKING
	NETISR_RLOCK(&tracker);
#endif
	KASSERT(netisr_proto[proto].np_handler != NULL,
	    ("%s: invalid proto %u", __func__, proto));

#ifdef VIMAGE
	if (V_netisr_enable[proto] == 0) {
		m_freem(m);
		return (ENOPROTOOPT);
	}
#endif

	m = netisr_select_cpuid(&netisr_proto[proto], NETISR_DISPATCH_DEFERRED,
	    source, m, &cpuid);
	if (m != NULL) {
		KASSERT(!CPU_ABSENT(cpuid), ("%s: CPU %u absent", __func__,
		    cpuid));
		error = netisr_queue_internal(proto, m, cpuid);
	} else
		error = ENOBUFS;
#ifdef NETISR_LOCKING
	NETISR_RUNLOCK(&tracker);
#endif
	return (error);
}

int
netisr_queue(u_int proto, struct mbuf *m)
{

	return (netisr_queue_src(proto, 0, m));
}

/*
 * Dispatch a packet for netisr processing; direct dispatch is permitted by
 * calling context.
 */
int
netisr_dispatch_src(u_int proto, uintptr_t source, struct mbuf *m)
{
#ifdef NETISR_LOCKING
	struct rm_priotracker tracker;
#endif
	struct netisr_workstream *nwsp;
	struct netisr_proto *npp;
	struct netisr_work *npwp;
	int dosignal, error;
	u_int cpuid, dispatch_policy;

	KASSERT(proto < NETISR_MAXPROT,
	    ("%s: invalid proto %u", __func__, proto));
#ifdef NETISR_LOCKING
	NETISR_RLOCK(&tracker);
#endif
	npp = &netisr_proto[proto];
	KASSERT(npp->np_handler != NULL, ("%s: invalid proto %u", __func__,
	    proto));

#ifdef VIMAGE
	if (V_netisr_enable[proto] == 0) {
		m_freem(m);
		return (ENOPROTOOPT);
	}
#endif

	dispatch_policy = netisr_get_dispatch(npp);
	if (dispatch_policy == NETISR_DISPATCH_DEFERRED)
		return (netisr_queue_src(proto, source, m));

	/*
	 * If direct dispatch is forced, then unconditionally dispatch
	 * without a formal CPU selection.  Borrow the current CPU's stats,
	 * even if there's no worker on it.  In this case we don't update
	 * nws_flags because all netisr processing will be source ordered due
	 * to always being forced to directly dispatch.
	 */
	if (dispatch_policy == NETISR_DISPATCH_DIRECT) {
		nwsp = DPCPU_PTR(nws);
		npwp = &nwsp->nws_work[proto];
		npwp->nw_dispatched++;
		npwp->nw_handled++;
		netisr_proto[proto].np_handler(m);
		error = 0;
		goto out_unlock;
	}

	KASSERT(dispatch_policy == NETISR_DISPATCH_HYBRID,
	    ("%s: unknown dispatch policy (%u)", __func__, dispatch_policy));

	/*
	 * Otherwise, we execute in a hybrid mode where we will try to direct
	 * dispatch if we're on the right CPU and the netisr worker isn't
	 * already running.
	 */
	sched_pin();
	m = netisr_select_cpuid(&netisr_proto[proto], NETISR_DISPATCH_HYBRID,
	    source, m, &cpuid);
	if (m == NULL) {
		error = ENOBUFS;
		goto out_unpin;
	}
	KASSERT(!CPU_ABSENT(cpuid), ("%s: CPU %u absent", __func__, cpuid));
	if (cpuid != curcpu)
		goto queue_fallback;
	nwsp = DPCPU_PTR(nws);
	npwp = &nwsp->nws_work[proto];

	/*-
	 * We are willing to direct dispatch only if three conditions hold:
	 *
	 * (1) The netisr worker isn't already running,
	 * (2) Another thread isn't already directly dispatching, and
	 * (3) The netisr hasn't already been woken up.
	 */
	NWS_LOCK(nwsp);
	if (nwsp->nws_flags & (NWS_RUNNING | NWS_DISPATCHING | NWS_SCHEDULED)) {
		error = netisr_queue_workstream(nwsp, proto, npwp, m,
		    &dosignal);
		NWS_UNLOCK(nwsp);
		if (dosignal)
			NWS_SIGNAL(nwsp);
		goto out_unpin;
	}

	/*
	 * The current thread is now effectively the netisr worker, so set
	 * the dispatching flag to prevent concurrent processing of the
	 * stream from another thread (even the netisr worker), which could
	 * otherwise lead to effective misordering of the stream.
	 */
	nwsp->nws_flags |= NWS_DISPATCHING;
	NWS_UNLOCK(nwsp);
	netisr_proto[proto].np_handler(m);
	NWS_LOCK(nwsp);
	nwsp->nws_flags &= ~NWS_DISPATCHING;
	npwp->nw_handled++;
	npwp->nw_hybrid_dispatched++;

	/*
	 * If other work was enqueued by another thread while we were direct
	 * dispatching, we need to signal the netisr worker to do that work.
	 * In the future, we might want to do some of that work in the
	 * current thread, rather than trigger further context switches.  If
	 * so, we'll want to establish a reasonable bound on the work done in
	 * the "borrowed" context.
	 */
	if (nwsp->nws_pendingbits != 0) {
		nwsp->nws_flags |= NWS_SCHEDULED;
		dosignal = 1;
	} else
		dosignal = 0;
	NWS_UNLOCK(nwsp);
	if (dosignal)
		NWS_SIGNAL(nwsp);
	error = 0;
	goto out_unpin;

queue_fallback:
	error = netisr_queue_internal(proto, m, cpuid);
out_unpin:
	sched_unpin();
out_unlock:
#ifdef NETISR_LOCKING
	NETISR_RUNLOCK(&tracker);
#endif
	return (error);
}

int
netisr_dispatch(u_int proto, struct mbuf *m)
{

	return (netisr_dispatch_src(proto, 0, m));
}

#ifdef DEVICE_POLLING
/*
 * Kernel polling borrows a netisr thread to run interface polling in; this
 * function allows kernel polling to request that the netisr thread be
 * scheduled even if no packets are pending for protocols.
 */
void
netisr_sched_poll(void)
{
	struct netisr_workstream *nwsp;

	nwsp = DPCPU_ID_PTR(nws_array[0], nws);
	NWS_SIGNAL(nwsp);
}
#endif

static void
netisr_start_swi(u_int cpuid, struct pcpu *pc)
{
	char swiname[12];
	struct netisr_workstream *nwsp;
	int error;

	KASSERT(!CPU_ABSENT(cpuid), ("%s: CPU %u absent", __func__, cpuid));

	nwsp = DPCPU_ID_PTR(cpuid, nws);
	mtx_init(&nwsp->nws_mtx, "netisr_mtx", NULL, MTX_DEF);
	nwsp->nws_cpu = cpuid;
	snprintf(swiname, sizeof(swiname), "netisr %u", cpuid);
	error = swi_add(&nwsp->nws_intr_event, swiname, swi_net, nwsp,
	    SWI_NET, INTR_MPSAFE, &nwsp->nws_swi_cookie);
	if (error)
		panic("%s: swi_add %d", __func__, error);
	pc->pc_netisr = nwsp->nws_intr_event;
	if (netisr_bindthreads) {
		error = intr_event_bind(nwsp->nws_intr_event, cpuid);
		if (error != 0)
			printf("%s: cpu %u: intr_event_bind: %d", __func__,
			    cpuid, error);
	}
	NETISR_WLOCK();
	nws_array[nws_count] = nwsp->nws_cpu;
	nws_count++;
	NETISR_WUNLOCK();
}

/*
 * Initialize the netisr subsystem.  We rely on BSS and static initialization
 * of most fields in global data structures.
 *
 * Start a worker thread for the boot CPU so that we can support network
 * traffic immediately in case the network stack is used before additional
 * CPUs are started (for example, diskless boot).
 */
static void
netisr_init(void *arg)
{
	struct pcpu *pc;

	NETISR_LOCK_INIT();
	if (netisr_maxthreads == 0 || netisr_maxthreads < -1 )
		netisr_maxthreads = 1;		/* default behavior */
	else if (netisr_maxthreads == -1)
		netisr_maxthreads = mp_ncpus;	/* use max cpus */
	if (netisr_maxthreads > mp_ncpus) {
		printf("netisr_init: forcing maxthreads from %d to %d\n",
		    netisr_maxthreads, mp_ncpus);
		netisr_maxthreads = mp_ncpus;
	}
	if (netisr_defaultqlimit > netisr_maxqlimit) {
		printf("netisr_init: forcing defaultqlimit from %d to %d\n",
		    netisr_defaultqlimit, netisr_maxqlimit);
		netisr_defaultqlimit = netisr_maxqlimit;
	}
#ifdef DEVICE_POLLING
	/*
	 * The device polling code is not yet aware of how to deal with
	 * multiple netisr threads, so for the time being compiling in device
	 * polling disables parallel netisr workers.
	 */
	if (netisr_maxthreads != 1 || netisr_bindthreads != 0) {
		printf("netisr_init: forcing maxthreads to 1 and "
		    "bindthreads to 0 for device polling\n");
		netisr_maxthreads = 1;
		netisr_bindthreads = 0;
	}
#endif

#ifdef EARLY_AP_STARTUP
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (nws_count >= netisr_maxthreads)
			break;
		netisr_start_swi(pc->pc_cpuid, pc);
	}
#else
	pc = get_pcpu();
	netisr_start_swi(pc->pc_cpuid, pc);
#endif
}
SYSINIT(netisr_init, SI_SUB_SOFTINTR, SI_ORDER_FIRST, netisr_init, NULL);

#ifndef EARLY_AP_STARTUP
/*
 * Start worker threads for additional CPUs.  No attempt to gracefully handle
 * work reassignment, we don't yet support dynamic reconfiguration.
 */
static void
netisr_start(void *arg)
{
	struct pcpu *pc;

	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (nws_count >= netisr_maxthreads)
			break;
		/* Worker will already be present for boot CPU. */
		if (pc->pc_netisr != NULL)
			continue;
		netisr_start_swi(pc->pc_cpuid, pc);
	}
}
SYSINIT(netisr_start, SI_SUB_SMP, SI_ORDER_MIDDLE, netisr_start, NULL);
#endif

/*
 * Sysctl monitoring for netisr: query a list of registered protocols.
 */
static int
sysctl_netisr_proto(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;
	struct sysctl_netisr_proto *snpp, *snp_array;
	struct netisr_proto *npp;
	u_int counter, proto;
	int error;

	if (req->newptr != NULL)
		return (EINVAL);
	snp_array = malloc(sizeof(*snp_array) * NETISR_MAXPROT, M_TEMP,
	    M_ZERO | M_WAITOK);
	counter = 0;
	NETISR_RLOCK(&tracker);
	for (proto = 0; proto < NETISR_MAXPROT; proto++) {
		npp = &netisr_proto[proto];
		if (npp->np_name == NULL)
			continue;
		snpp = &snp_array[counter];
		snpp->snp_version = sizeof(*snpp);
		strlcpy(snpp->snp_name, npp->np_name, NETISR_NAMEMAXLEN);
		snpp->snp_proto = proto;
		snpp->snp_qlimit = npp->np_qlimit;
		snpp->snp_policy = npp->np_policy;
		snpp->snp_dispatch = npp->np_dispatch;
		if (npp->np_m2flow != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_M2FLOW;
		if (npp->np_m2cpuid != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_M2CPUID;
		if (npp->np_drainedcpu != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_DRAINEDCPU;
		counter++;
	}
	NETISR_RUNLOCK(&tracker);
	KASSERT(counter <= NETISR_MAXPROT,
	    ("sysctl_netisr_proto: counter too big (%d)", counter));
	error = SYSCTL_OUT(req, snp_array, sizeof(*snp_array) * counter);
	free(snp_array, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_isr, OID_AUTO, proto,
    CTLFLAG_RD|CTLTYPE_STRUCT|CTLFLAG_MPSAFE, 0, 0, sysctl_netisr_proto,
    "S,sysctl_netisr_proto",
    "Return list of protocols registered with netisr");

/*
 * Sysctl monitoring for netisr: query a list of workstreams.
 */
static int
sysctl_netisr_workstream(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;
	struct sysctl_netisr_workstream *snwsp, *snws_array;
	struct netisr_workstream *nwsp;
	u_int counter, cpuid;
	int error;

	if (req->newptr != NULL)
		return (EINVAL);
	snws_array = malloc(sizeof(*snws_array) * MAXCPU, M_TEMP,
	    M_ZERO | M_WAITOK);
	counter = 0;
	NETISR_RLOCK(&tracker);
	CPU_FOREACH(cpuid) {
		nwsp = DPCPU_ID_PTR(cpuid, nws);
		if (nwsp->nws_intr_event == NULL)
			continue;
		NWS_LOCK(nwsp);
		snwsp = &snws_array[counter];
		snwsp->snws_version = sizeof(*snwsp);

		/*
		 * For now, we equate workstream IDs and CPU IDs in the
		 * kernel, but expose them independently to userspace in case
		 * that assumption changes in the future.
		 */
		snwsp->snws_wsid = cpuid;
		snwsp->snws_cpu = cpuid;
		if (nwsp->nws_intr_event != NULL)
			snwsp->snws_flags |= NETISR_SNWS_FLAGS_INTR;
		NWS_UNLOCK(nwsp);
		counter++;
	}
	NETISR_RUNLOCK(&tracker);
	KASSERT(counter <= MAXCPU,
	    ("sysctl_netisr_workstream: counter too big (%d)", counter));
	error = SYSCTL_OUT(req, snws_array, sizeof(*snws_array) * counter);
	free(snws_array, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_isr, OID_AUTO, workstream,
    CTLFLAG_RD|CTLTYPE_STRUCT|CTLFLAG_MPSAFE, 0, 0, sysctl_netisr_workstream,
    "S,sysctl_netisr_workstream",
    "Return list of workstreams implemented by netisr");

/*
 * Sysctl monitoring for netisr: query per-protocol data across all
 * workstreams.
 */
static int
sysctl_netisr_work(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;
	struct sysctl_netisr_work *snwp, *snw_array;
	struct netisr_workstream *nwsp;
	struct netisr_proto *npp;
	struct netisr_work *nwp;
	u_int counter, cpuid, proto;
	int error;

	if (req->newptr != NULL)
		return (EINVAL);
	snw_array = malloc(sizeof(*snw_array) * MAXCPU * NETISR_MAXPROT,
	    M_TEMP, M_ZERO | M_WAITOK);
	counter = 0;
	NETISR_RLOCK(&tracker);
	CPU_FOREACH(cpuid) {
		nwsp = DPCPU_ID_PTR(cpuid, nws);
		if (nwsp->nws_intr_event == NULL)
			continue;
		NWS_LOCK(nwsp);
		for (proto = 0; proto < NETISR_MAXPROT; proto++) {
			npp = &netisr_proto[proto];
			if (npp->np_name == NULL)
				continue;
			nwp = &nwsp->nws_work[proto];
			snwp = &snw_array[counter];
			snwp->snw_version = sizeof(*snwp);
			snwp->snw_wsid = cpuid;		/* See comment above. */
			snwp->snw_proto = proto;
			snwp->snw_len = nwp->nw_len;
			snwp->snw_watermark = nwp->nw_watermark;
			snwp->snw_dispatched = nwp->nw_dispatched;
			snwp->snw_hybrid_dispatched =
			    nwp->nw_hybrid_dispatched;
			snwp->snw_qdrops = nwp->nw_qdrops;
			snwp->snw_queued = nwp->nw_queued;
			snwp->snw_handled = nwp->nw_handled;
			counter++;
		}
		NWS_UNLOCK(nwsp);
	}
	KASSERT(counter <= MAXCPU * NETISR_MAXPROT,
	    ("sysctl_netisr_work: counter too big (%d)", counter));
	NETISR_RUNLOCK(&tracker);
	error = SYSCTL_OUT(req, snw_array, sizeof(*snw_array) * counter);
	free(snw_array, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_isr, OID_AUTO, work,
    CTLFLAG_RD|CTLTYPE_STRUCT|CTLFLAG_MPSAFE, 0, 0, sysctl_netisr_work,
    "S,sysctl_netisr_work",
    "Return list of per-workstream, per-protocol work in netisr");

#ifdef DDB
DB_SHOW_COMMAND(netisr, db_show_netisr)
{
	struct netisr_workstream *nwsp;
	struct netisr_work *nwp;
	int first, proto;
	u_int cpuid;

	db_printf("%3s %6s %5s %5s %5s %8s %8s %8s %8s\n", "CPU", "Proto",
	    "Len", "WMark", "Max", "Disp", "HDisp", "Drop", "Queue");
	CPU_FOREACH(cpuid) {
		nwsp = DPCPU_ID_PTR(cpuid, nws);
		if (nwsp->nws_intr_event == NULL)
			continue;
		first = 1;
		for (proto = 0; proto < NETISR_MAXPROT; proto++) {
			if (netisr_proto[proto].np_handler == NULL)
				continue;
			nwp = &nwsp->nws_work[proto];
			if (first) {
				db_printf("%3d ", cpuid);
				first = 0;
			} else
				db_printf("%3s ", "");
			db_printf(
			    "%6s %5d %5d %5d %8ju %8ju %8ju %8ju\n",
			    netisr_proto[proto].np_name, nwp->nw_len,
			    nwp->nw_watermark, nwp->nw_qlimit,
			    nwp->nw_dispatched, nwp->nw_hybrid_dispatched,
			    nwp->nw_qdrops, nwp->nw_queued);
		}
	}
}
#endif
