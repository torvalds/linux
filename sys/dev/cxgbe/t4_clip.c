/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/types.h>
#include <sys/ck.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/rmlock.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>
#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/scope6_var.h>

#include "common/common.h"
#include "t4_clip.h"

#if defined(INET6)
static int add_lip(struct adapter *, struct in6_addr *);
static int delete_lip(struct adapter *, struct in6_addr *);
static struct clip_entry *search_lip(struct adapter *, struct in6_addr *);
static void update_clip(struct adapter *, void *);
static void t4_clip_task(void *, int);
static void update_clip_table(struct adapter *);

static int in6_ifaddr_gen;
static eventhandler_tag ifaddr_evhandler;
static struct timeout_task clip_task;

static int
add_lip(struct adapter *sc, struct in6_addr *lip)
{
        struct fw_clip_cmd c;

	ASSERT_SYNCHRONIZED_OP(sc);
	mtx_assert(&sc->clip_table_lock, MA_OWNED);

        memset(&c, 0, sizeof(c));
	c.op_to_write = htonl(V_FW_CMD_OP(FW_CLIP_CMD) | F_FW_CMD_REQUEST |
	    F_FW_CMD_WRITE);
        c.alloc_to_len16 = htonl(F_FW_CLIP_CMD_ALLOC | FW_LEN16(c));
        c.ip_hi = *(uint64_t *)&lip->s6_addr[0];
        c.ip_lo = *(uint64_t *)&lip->s6_addr[8];

	return (-t4_wr_mbox_ns(sc, sc->mbox, &c, sizeof(c), &c));
}

static int
delete_lip(struct adapter *sc, struct in6_addr *lip)
{
	struct fw_clip_cmd c;

	ASSERT_SYNCHRONIZED_OP(sc);
	mtx_assert(&sc->clip_table_lock, MA_OWNED);

	memset(&c, 0, sizeof(c));
	c.op_to_write = htonl(V_FW_CMD_OP(FW_CLIP_CMD) | F_FW_CMD_REQUEST |
	    F_FW_CMD_READ);
        c.alloc_to_len16 = htonl(F_FW_CLIP_CMD_FREE | FW_LEN16(c));
        c.ip_hi = *(uint64_t *)&lip->s6_addr[0];
        c.ip_lo = *(uint64_t *)&lip->s6_addr[8];

	return (-t4_wr_mbox_ns(sc, sc->mbox, &c, sizeof(c), &c));
}

static struct clip_entry *
search_lip(struct adapter *sc, struct in6_addr *lip)
{
	struct clip_entry *ce;

	mtx_assert(&sc->clip_table_lock, MA_OWNED);

	TAILQ_FOREACH(ce, &sc->clip_table, link) {
		if (IN6_ARE_ADDR_EQUAL(&ce->lip, lip))
			return (ce);
	}

	return (NULL);
}
#endif

struct clip_entry *
t4_hold_lip(struct adapter *sc, struct in6_addr *lip, struct clip_entry *ce)
{

#ifdef INET6
	mtx_lock(&sc->clip_table_lock);
	if (ce == NULL)
		ce = search_lip(sc, lip);
	if (ce != NULL)
		ce->refcount++;
	mtx_unlock(&sc->clip_table_lock);

	return (ce);
#else
	return (NULL);
#endif
}

void
t4_release_lip(struct adapter *sc, struct clip_entry *ce)
{

#ifdef INET6
	mtx_lock(&sc->clip_table_lock);
	KASSERT(search_lip(sc, &ce->lip) == ce,
	    ("%s: CLIP entry %p p not in CLIP table.", __func__, ce));
	KASSERT(ce->refcount > 0,
	    ("%s: CLIP entry %p has refcount 0", __func__, ce));
	--ce->refcount;
	mtx_unlock(&sc->clip_table_lock);
#endif
}

#ifdef INET6
void
t4_init_clip_table(struct adapter *sc)
{

	mtx_init(&sc->clip_table_lock, "CLIP table lock", NULL, MTX_DEF);
	TAILQ_INIT(&sc->clip_table);
	sc->clip_gen = -1;

	/*
	 * Don't bother forcing an update of the clip table when the
	 * adapter is initialized.  Before an interface can be used it
	 * must be assigned an address which will trigger the event
	 * handler to update the table.
	 */
}

static void
update_clip(struct adapter *sc, void *arg __unused)
{

	if (begin_synchronized_op(sc, NULL, HOLD_LOCK, "t4clip"))
		return;

	if (mtx_initialized(&sc->clip_table_lock))
		update_clip_table(sc);

	end_synchronized_op(sc, LOCK_HELD);
}

static void
t4_clip_task(void *arg, int count)
{

	t4_iterate(update_clip, NULL);
}

static void
update_clip_table(struct adapter *sc)
{
	struct rm_priotracker in6_ifa_tracker;
	struct in6_ifaddr *ia;
	struct in6_addr *lip, tlip;
	TAILQ_HEAD(, clip_entry) stale;
	struct clip_entry *ce, *ce_temp;
	struct vi_info *vi;
	int rc, gen, i, j;
	uintptr_t last_vnet;

	ASSERT_SYNCHRONIZED_OP(sc);

	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	mtx_lock(&sc->clip_table_lock);

	gen = atomic_load_acq_int(&in6_ifaddr_gen);
	if (gen == sc->clip_gen)
		goto done;

	TAILQ_INIT(&stale);
	TAILQ_CONCAT(&stale, &sc->clip_table, link);

	/*
	 * last_vnet optimizes the common cases where all if_vnet = NULL (no
	 * VIMAGE) or all if_vnet = vnet0.
	 */
	last_vnet = (uintptr_t)(-1);
	for_each_port(sc, i)
	for_each_vi(sc->port[i], j, vi) {
		if (last_vnet == (uintptr_t)vi->ifp->if_vnet)
			continue;

		/* XXX: races with if_vmove */
		CURVNET_SET(vi->ifp->if_vnet);
		CK_STAILQ_FOREACH(ia, &V_in6_ifaddrhead, ia_link) {
			lip = &ia->ia_addr.sin6_addr;

			KASSERT(!IN6_IS_ADDR_MULTICAST(lip),
			    ("%s: mcast address in in6_ifaddr list", __func__));

			if (IN6_IS_ADDR_LOOPBACK(lip))
				continue;
			if (IN6_IS_SCOPE_EMBED(lip)) {
				/* Remove the embedded scope */
				tlip = *lip;
				lip = &tlip;
				in6_clearscope(lip);
			}
			/*
			 * XXX: how to weed out the link local address for the
			 * loopback interface?  It's fe80::1 usually (always?).
			 */

			/*
			 * If it's in the main list then we already know it's
			 * not stale.
			 */
			TAILQ_FOREACH(ce, &sc->clip_table, link) {
				if (IN6_ARE_ADDR_EQUAL(&ce->lip, lip))
					goto next;
			}

			/*
			 * If it's in the stale list we should move it to the
			 * main list.
			 */
			TAILQ_FOREACH(ce, &stale, link) {
				if (IN6_ARE_ADDR_EQUAL(&ce->lip, lip)) {
					TAILQ_REMOVE(&stale, ce, link);
					TAILQ_INSERT_TAIL(&sc->clip_table, ce,
					    link);
					goto next;
				}
			}

			/* A new IP6 address; add it to the CLIP table */
			ce = malloc(sizeof(*ce), M_CXGBE, M_NOWAIT);
			memcpy(&ce->lip, lip, sizeof(ce->lip));
			ce->refcount = 0;
			rc = add_lip(sc, lip);
			if (rc == 0)
				TAILQ_INSERT_TAIL(&sc->clip_table, ce, link);
			else {
				char ip[INET6_ADDRSTRLEN];

				inet_ntop(AF_INET6, &ce->lip, &ip[0],
				    sizeof(ip));
				log(LOG_ERR, "%s: could not add %s (%d)\n",
				    __func__, ip, rc);
				free(ce, M_CXGBE);
			}
next:
			continue;
		}
		CURVNET_RESTORE();
		last_vnet = (uintptr_t)vi->ifp->if_vnet;
	}

	/*
	 * Remove stale addresses (those no longer in V_in6_ifaddrhead) that are
	 * no longer referenced by the driver.
	 */
	TAILQ_FOREACH_SAFE(ce, &stale, link, ce_temp) {
		if (ce->refcount == 0) {
			rc = delete_lip(sc, &ce->lip);
			if (rc == 0) {
				TAILQ_REMOVE(&stale, ce, link);
				free(ce, M_CXGBE);
			} else {
				char ip[INET6_ADDRSTRLEN];

				inet_ntop(AF_INET6, &ce->lip, &ip[0],
				    sizeof(ip));
				log(LOG_ERR, "%s: could not delete %s (%d)\n",
				    __func__, ip, rc);
			}
		}
	}
	/* The ones that are still referenced need to stay in the CLIP table */
	TAILQ_CONCAT(&sc->clip_table, &stale, link);

	sc->clip_gen = gen;
done:
	mtx_unlock(&sc->clip_table_lock);
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
}

void
t4_destroy_clip_table(struct adapter *sc)
{
	struct clip_entry *ce, *ce_temp;

	if (mtx_initialized(&sc->clip_table_lock)) {
		mtx_lock(&sc->clip_table_lock);
		TAILQ_FOREACH_SAFE(ce, &sc->clip_table, link, ce_temp) {
			KASSERT(ce->refcount == 0,
			    ("%s: CLIP entry %p still in use (%d)", __func__,
			    ce, ce->refcount));
			TAILQ_REMOVE(&sc->clip_table, ce, link);
#if 0
			delete_lip(sc, &ce->lip);
#endif
			free(ce, M_CXGBE);
		}
		mtx_unlock(&sc->clip_table_lock);
		mtx_destroy(&sc->clip_table_lock);
	}
}

static void
t4_tom_ifaddr_event(void *arg __unused, struct ifnet *ifp)
{

	atomic_add_rel_int(&in6_ifaddr_gen, 1);
	taskqueue_enqueue_timeout(taskqueue_thread, &clip_task, -hz / 4);
}

int
sysctl_clip(SYSCTL_HANDLER_ARGS)
{
	struct adapter *sc = arg1;
	struct clip_entry *ce;
	struct sbuf *sb;
	int rc, header = 0;
	char ip[INET6_ADDRSTRLEN];

	rc = sysctl_wire_old_buffer(req, 0);
	if (rc != 0)
		return (rc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->clip_table_lock);
	TAILQ_FOREACH(ce, &sc->clip_table, link) {
		if (header == 0) {
			sbuf_printf(sb, "%-40s %-5s", "IP address", "Users");
			header = 1;
		}
		inet_ntop(AF_INET6, &ce->lip, &ip[0], sizeof(ip));

		sbuf_printf(sb, "\n%-40s %5u", ip, ce->refcount);
	}
	mtx_unlock(&sc->clip_table_lock);

	rc = sbuf_finish(sb);
	sbuf_delete(sb);

	return (rc);
}

void
t4_clip_modload(void)
{

	TIMEOUT_TASK_INIT(taskqueue_thread, &clip_task, 0, t4_clip_task, NULL);
	ifaddr_evhandler = EVENTHANDLER_REGISTER(ifaddr_event,
	    t4_tom_ifaddr_event, NULL, EVENTHANDLER_PRI_ANY);
}

void
t4_clip_modunload(void)
{

	EVENTHANDLER_DEREGISTER(ifaddr_event, ifaddr_evhandler);
	taskqueue_cancel_timeout(taskqueue_thread, &clip_task, NULL);
}
#endif
