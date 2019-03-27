/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2000 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: scope6.c,v 1.10 2000/07/24 13:29:31 itojun Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>

#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>

#ifdef ENABLE_DEFAULT_SCOPE
VNET_DEFINE(int, ip6_use_defzone) = 1;
#else
VNET_DEFINE(int, ip6_use_defzone) = 0;
#endif
VNET_DEFINE(int, deembed_scopeid) = 1;
SYSCTL_DECL(_net_inet6_ip6);
SYSCTL_INT(_net_inet6_ip6, OID_AUTO, deembed_scopeid, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(deembed_scopeid), 0,
    "Extract embedded zone ID and set it to sin6_scope_id in sockaddr_in6.");

/*
 * The scope6_lock protects the global sid default stored in
 * sid_default below.
 */
static struct mtx scope6_lock;
#define	SCOPE6_LOCK_INIT()	mtx_init(&scope6_lock, "scope6_lock", NULL, MTX_DEF)
#define	SCOPE6_LOCK()		mtx_lock(&scope6_lock)
#define	SCOPE6_UNLOCK()		mtx_unlock(&scope6_lock)
#define	SCOPE6_LOCK_ASSERT()	mtx_assert(&scope6_lock, MA_OWNED)

VNET_DEFINE_STATIC(struct scope6_id, sid_default);
#define	V_sid_default			VNET(sid_default)

#define SID(ifp) \
	(((struct in6_ifextra *)(ifp)->if_afdata[AF_INET6])->scope6_id)

static int	scope6_get(struct ifnet *, struct scope6_id *);
static int	scope6_set(struct ifnet *, struct scope6_id *);

void
scope6_init(void)
{

	bzero(&V_sid_default, sizeof(V_sid_default));

	if (!IS_DEFAULT_VNET(curvnet))
		return;

	SCOPE6_LOCK_INIT();
}

struct scope6_id *
scope6_ifattach(struct ifnet *ifp)
{
	struct scope6_id *sid;

	sid = malloc(sizeof(*sid), M_IFADDR, M_WAITOK | M_ZERO);
	/*
	 * XXX: IPV6_ADDR_SCOPE_xxx macros are not standard.
	 * Should we rather hardcode here?
	 */
	sid->s6id_list[IPV6_ADDR_SCOPE_INTFACELOCAL] = ifp->if_index;
	sid->s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] = ifp->if_index;
	return (sid);
}

void
scope6_ifdetach(struct scope6_id *sid)
{

	free(sid, M_IFADDR);
}

int
scope6_ioctl(u_long cmd, caddr_t data, struct ifnet *ifp)
{
	struct in6_ifreq *ifr;

	if (ifp->if_afdata[AF_INET6] == NULL)
		return (EPFNOSUPPORT);

	ifr = (struct in6_ifreq *)data;
	switch (cmd) {
	case SIOCSSCOPE6:
		return (scope6_set(ifp,
		    (struct scope6_id *)ifr->ifr_ifru.ifru_scope_id));
	case SIOCGSCOPE6:
		return (scope6_get(ifp,
		    (struct scope6_id *)ifr->ifr_ifru.ifru_scope_id));
	case SIOCGSCOPE6DEF:
		return (scope6_get_default(
		    (struct scope6_id *)ifr->ifr_ifru.ifru_scope_id));
	default:
		return (EOPNOTSUPP);
	}
}

static int
scope6_set(struct ifnet *ifp, struct scope6_id *idlist)
{
	int i;
	int error = 0;
	struct scope6_id *sid = NULL;

	IF_AFDATA_WLOCK(ifp);
	sid = SID(ifp);

	if (!sid) {	/* paranoid? */
		IF_AFDATA_WUNLOCK(ifp);
		return (EINVAL);
	}

	/*
	 * XXX: We need more consistency checks of the relationship among
	 * scopes (e.g. an organization should be larger than a site).
	 */

	/*
	 * TODO(XXX): after setting, we should reflect the changes to
	 * interface addresses, routing table entries, PCB entries...
	 */

	for (i = 0; i < 16; i++) {
		if (idlist->s6id_list[i] &&
		    idlist->s6id_list[i] != sid->s6id_list[i]) {
			/*
			 * An interface zone ID must be the corresponding
			 * interface index by definition.
			 */
			if (i == IPV6_ADDR_SCOPE_INTFACELOCAL &&
			    idlist->s6id_list[i] != ifp->if_index) {
				IF_AFDATA_WUNLOCK(ifp);
				return (EINVAL);
			}

			if (i == IPV6_ADDR_SCOPE_LINKLOCAL &&
			    idlist->s6id_list[i] > V_if_index) {
				/*
				 * XXX: theoretically, there should be no
				 * relationship between link IDs and interface
				 * IDs, but we check the consistency for
				 * safety in later use.
				 */
				IF_AFDATA_WUNLOCK(ifp);
				return (EINVAL);
			}

			/*
			 * XXX: we must need lots of work in this case,
			 * but we simply set the new value in this initial
			 * implementation.
			 */
			sid->s6id_list[i] = idlist->s6id_list[i];
		}
	}
	IF_AFDATA_WUNLOCK(ifp);

	return (error);
}

static int
scope6_get(struct ifnet *ifp, struct scope6_id *idlist)
{
	struct epoch_tracker et;
	struct scope6_id *sid;

	/* We only need to lock the interface's afdata for SID() to work. */
	NET_EPOCH_ENTER(et);
	sid = SID(ifp);
	if (sid == NULL) {	/* paranoid? */
		NET_EPOCH_EXIT(et);
		return (EINVAL);
	}

	*idlist = *sid;

	NET_EPOCH_EXIT(et);
	return (0);
}

/*
 * Get a scope of the address. Node-local, link-local, site-local or global.
 */
int
in6_addrscope(const struct in6_addr *addr)
{

	if (IN6_IS_ADDR_MULTICAST(addr)) {
		/*
		 * Addresses with reserved value F must be treated as
		 * global multicast addresses.
		 */
		if (IPV6_ADDR_MC_SCOPE(addr) == 0x0f)
			return (IPV6_ADDR_SCOPE_GLOBAL);
		return (IPV6_ADDR_MC_SCOPE(addr));
	}
	if (IN6_IS_ADDR_LINKLOCAL(addr) ||
	    IN6_IS_ADDR_LOOPBACK(addr))
		return (IPV6_ADDR_SCOPE_LINKLOCAL);
	if (IN6_IS_ADDR_SITELOCAL(addr))
		return (IPV6_ADDR_SCOPE_SITELOCAL);
	return (IPV6_ADDR_SCOPE_GLOBAL);
}

/*
 * ifp - note that this might be NULL
 */

void
scope6_setdefault(struct ifnet *ifp)
{

	/*
	 * Currently, this function just sets the default "interfaces"
	 * and "links" according to the given interface.
	 * We might eventually have to separate the notion of "link" from
	 * "interface" and provide a user interface to set the default.
	 */
	SCOPE6_LOCK();
	if (ifp) {
		V_sid_default.s6id_list[IPV6_ADDR_SCOPE_INTFACELOCAL] =
			ifp->if_index;
		V_sid_default.s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] =
			ifp->if_index;
	} else {
		V_sid_default.s6id_list[IPV6_ADDR_SCOPE_INTFACELOCAL] = 0;
		V_sid_default.s6id_list[IPV6_ADDR_SCOPE_LINKLOCAL] = 0;
	}
	SCOPE6_UNLOCK();
}

int
scope6_get_default(struct scope6_id *idlist)
{

	SCOPE6_LOCK();
	*idlist = V_sid_default;
	SCOPE6_UNLOCK();

	return (0);
}

u_int32_t
scope6_addr2default(struct in6_addr *addr)
{
	u_int32_t id;

	/*
	 * special case: The loopback address should be considered as
	 * link-local, but there's no ambiguity in the syntax.
	 */
	if (IN6_IS_ADDR_LOOPBACK(addr))
		return (0);

	/*
	 * XXX: 32-bit read is atomic on all our platforms, is it OK
	 * not to lock here?
	 */
	SCOPE6_LOCK();
	id = V_sid_default.s6id_list[in6_addrscope(addr)];
	SCOPE6_UNLOCK();
	return (id);
}

/*
 * Validate the specified scope zone ID in the sin6_scope_id field.  If the ID
 * is unspecified (=0), needs to be specified, and the default zone ID can be
 * used, the default value will be used.
 * This routine then generates the kernel-internal form: if the address scope
 * of is interface-local or link-local, embed the interface index in the
 * address.
 */
int
sa6_embedscope(struct sockaddr_in6 *sin6, int defaultok)
{
	u_int32_t zoneid;

	if ((zoneid = sin6->sin6_scope_id) == 0 && defaultok)
		zoneid = scope6_addr2default(&sin6->sin6_addr);

	if (zoneid != 0 &&
	    (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6->sin6_addr))) {
		/*
		 * At this moment, we only check interface-local and
		 * link-local scope IDs, and use interface indices as the
		 * zone IDs assuming a one-to-one mapping between interfaces
		 * and links.
		 */
		if (V_if_index < zoneid || ifnet_byindex(zoneid) == NULL)
			return (ENXIO);

		/* XXX assignment to 16bit from 32bit variable */
		sin6->sin6_addr.s6_addr16[1] = htons(zoneid & 0xffff);
		sin6->sin6_scope_id = 0;
	}

	return 0;
}

/*
 * generate standard sockaddr_in6 from embedded form.
 */
int
sa6_recoverscope(struct sockaddr_in6 *sin6)
{
	char ip6buf[INET6_ADDRSTRLEN];
	u_int32_t zoneid;

	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6->sin6_addr)) {
		/*
		 * KAME assumption: link id == interface id
		 */
		zoneid = ntohs(sin6->sin6_addr.s6_addr16[1]);
		if (zoneid) {
			/* sanity check */
			if (V_if_index < zoneid)
				return (ENXIO);
#if 0
			/* XXX: Disabled due to possible deadlock. */
			if (!ifnet_byindex(zoneid))
				return (ENXIO);
#endif
			if (sin6->sin6_scope_id != 0 &&
			    zoneid != sin6->sin6_scope_id) {
				log(LOG_NOTICE,
				    "%s: embedded scope mismatch: %s%%%d. "
				    "sin6_scope_id was overridden\n", __func__,
				    ip6_sprintf(ip6buf, &sin6->sin6_addr),
				    sin6->sin6_scope_id);
			}
			sin6->sin6_addr.s6_addr16[1] = 0;
			sin6->sin6_scope_id = zoneid;
		}
	}

	return 0;
}

/*
 * Determine the appropriate scope zone ID for in6 and ifp.  If ret_id is
 * non NULL, it is set to the zone ID.  If the zone ID needs to be embedded
 * in the in6_addr structure, in6 will be modified.
 *
 * ret_id - unnecessary?
 */
int
in6_setscope(struct in6_addr *in6, struct ifnet *ifp, u_int32_t *ret_id)
{
	int scope;
	u_int32_t zoneid = 0;
	struct scope6_id *sid;

	/*
	 * special case: the loopback address can only belong to a loopback
	 * interface.
	 */
	if (IN6_IS_ADDR_LOOPBACK(in6)) {
		if (!(ifp->if_flags & IFF_LOOPBACK))
			return (EINVAL);
	} else {
		scope = in6_addrscope(in6);
		if (scope == IPV6_ADDR_SCOPE_INTFACELOCAL ||
		    scope == IPV6_ADDR_SCOPE_LINKLOCAL) {
			/*
			 * Currently we use interface indices as the
			 * zone IDs for interface-local and link-local
			 * scopes.
			 */
			zoneid = ifp->if_index;
			in6->s6_addr16[1] = htons(zoneid & 0xffff); /* XXX */
		} else if (scope != IPV6_ADDR_SCOPE_GLOBAL) {
			struct epoch_tracker et;

			NET_EPOCH_ENTER(et);
			sid = SID(ifp);
			zoneid = sid->s6id_list[scope];
			NET_EPOCH_EXIT(et);
		}
	}

	if (ret_id != NULL)
		*ret_id = zoneid;

	return (0);
}

/*
 * Just clear the embedded scope identifier.  Return 0 if the original address
 * is intact; return non 0 if the address is modified.
 */
int
in6_clearscope(struct in6_addr *in6)
{
	int modified = 0;

	if (IN6_IS_SCOPE_LINKLOCAL(in6) || IN6_IS_ADDR_MC_INTFACELOCAL(in6)) {
		if (in6->s6_addr16[1] != 0)
			modified = 1;
		in6->s6_addr16[1] = 0;
	}

	return (modified);
}

/*
 * Return the scope identifier or zero.
 */
uint16_t
in6_getscope(const struct in6_addr *in6)
{

	if (IN6_IS_SCOPE_LINKLOCAL(in6) || IN6_IS_ADDR_MC_INTFACELOCAL(in6))
		return (in6->s6_addr16[1]);

	return (0);
}

/*
 * Return pointer to ifnet structure, corresponding to the zone id of
 * link-local scope.
 */
struct ifnet*
in6_getlinkifnet(uint32_t zoneid)
{

	return (ifnet_byindex((u_short)zoneid));
}

/*
 * Return zone id for the specified scope.
 */
uint32_t
in6_getscopezone(const struct ifnet *ifp, int scope)
{

	if (scope == IPV6_ADDR_SCOPE_INTFACELOCAL ||
	    scope == IPV6_ADDR_SCOPE_LINKLOCAL)
		return (ifp->if_index);
	if (scope >= 0 && scope < IPV6_ADDR_SCOPES_COUNT)
		return (SID(ifp)->s6id_list[scope]);
	return (0);
}

/*
 * Extracts scope from adddress @dst, stores cleared address
 * inside @dst and zone inside @scopeid
 */
void
in6_splitscope(const struct in6_addr *src, struct in6_addr *dst,
    uint32_t *scopeid)
{
	uint32_t zoneid;

	*dst = *src;
	zoneid = ntohs(in6_getscope(dst));
	in6_clearscope(dst);
	*scopeid = zoneid;
}

/*
 * This function is for checking sockaddr_in6 structure passed
 * from the application level (usually).
 *
 * sin6_scope_id should be set for link-local unicast, link-local and
 * interface-local  multicast addresses.
 *
 * If it is zero, then look into default zone ids. If default zone id is
 * not set or disabled, then return error.
 */
int
sa6_checkzone(struct sockaddr_in6 *sa6)
{
	int scope;

	scope = in6_addrscope(&sa6->sin6_addr);
	if (scope == IPV6_ADDR_SCOPE_GLOBAL)
		return (sa6->sin6_scope_id ? EINVAL: 0);
	if (IN6_IS_ADDR_MULTICAST(&sa6->sin6_addr) &&
	    scope != IPV6_ADDR_SCOPE_LINKLOCAL &&
	    scope != IPV6_ADDR_SCOPE_INTFACELOCAL) {
		if (sa6->sin6_scope_id == 0 && V_ip6_use_defzone != 0)
			sa6->sin6_scope_id = V_sid_default.s6id_list[scope];
		return (0);
	}
	/*
	 * Since ::1 address always configured on the lo0, we can
	 * automatically set its zone id, when it is not specified.
	 * Return error, when specified zone id doesn't match with
	 * actual value.
	 */
	if (IN6_IS_ADDR_LOOPBACK(&sa6->sin6_addr)) {
		if (sa6->sin6_scope_id == 0)
			sa6->sin6_scope_id = in6_getscopezone(V_loif, scope);
		else if (sa6->sin6_scope_id != in6_getscopezone(V_loif, scope))
			return (EADDRNOTAVAIL);
	}
	/* XXX: we can validate sin6_scope_id here */
	if (sa6->sin6_scope_id != 0)
		return (0);
	if (V_ip6_use_defzone != 0)
		sa6->sin6_scope_id = V_sid_default.s6id_list[scope];
	/* Return error if we can't determine zone id */
	return (sa6->sin6_scope_id ? 0: EADDRNOTAVAIL);
}

/*
 * This function is similar to sa6_checkzone, but it uses given ifp
 * to initialize sin6_scope_id.
 */
int
sa6_checkzone_ifp(struct ifnet *ifp, struct sockaddr_in6 *sa6)
{
	int scope;

	scope = in6_addrscope(&sa6->sin6_addr);
	if (scope == IPV6_ADDR_SCOPE_LINKLOCAL ||
	    scope == IPV6_ADDR_SCOPE_INTFACELOCAL) {
		if (sa6->sin6_scope_id == 0) {
			sa6->sin6_scope_id = in6_getscopezone(ifp, scope);
			return (0);
		} else if (sa6->sin6_scope_id != in6_getscopezone(ifp, scope))
			return (EADDRNOTAVAIL);
	}
	return (sa6_checkzone(sa6));
}


