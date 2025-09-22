/*	$OpenBSD: virtual.c,v 1.33 2019/06/28 13:32:44 deraadt Exp $	*/

/*
 * Copyright (c) 2004 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "if.h"
#include "exchange.h"
#include "log.h"
#include "message.h"
#include "nat_traversal.h"
#include "transport.h"
#include "virtual.h"
#include "udp.h"
#include "util.h"

#include "udp_encap.h"

static struct transport	*virtual_bind(const struct sockaddr *);
static struct transport	*virtual_bind_ADDR_ANY(sa_family_t);
static int		 virtual_bind_if(char *, struct sockaddr *, void *);
static struct transport	*virtual_clone(struct transport *, struct sockaddr *);
static struct transport	*virtual_create(char *);
static char		*virtual_decode_ids (struct transport *);
static void		 virtual_get_dst(struct transport *,
			     struct sockaddr **);
static struct msg_head	*virtual_get_queue(struct message *);
static void		 virtual_get_src(struct transport *,
			     struct sockaddr **);
static void		 virtual_handle_message(struct transport *);
static void		 virtual_reinit(void);
static void		 virtual_remove(struct transport *);
static void		 virtual_report(struct transport *);
static int		 virtual_send_message(struct message *,
			     struct transport *);

static struct transport_vtbl virtual_transport_vtbl = {
	{ 0 }, "udp",
	virtual_create,
	virtual_reinit,
	virtual_remove,
	virtual_report,
	0,
	0,
	virtual_handle_message,
	virtual_send_message,
	virtual_get_dst,
	virtual_get_src,
	virtual_decode_ids,
	virtual_clone,
	virtual_get_queue
};

static LIST_HEAD (virtual_listen_list, virtual_transport) virtual_listen_list;
static struct transport *default_transport, *default_transport6;

void
virtual_init(void)
{
	struct conf_list *listen_on;

	LIST_INIT(&virtual_listen_list);

	transport_method_add(&virtual_transport_vtbl);

	/* Bind the ISAKMP port(s) on all network interfaces we have.  */
	if (if_map(virtual_bind_if, 0) == -1)
		log_fatal("virtual_init: "
		    "could not bind the ISAKMP port(s) on all interfaces");

	/* Only listen to the specified address if Listen-on is configured */
	listen_on = conf_get_list("General", "Listen-on");
	if (listen_on) {
		LOG_DBG((LOG_TRANSPORT, 50,
		    "virtual_init: not binding ISAKMP port(s) to ADDR_ANY"));
		conf_free_list(listen_on);
		return;
	}

	/*
	 * Bind to INADDR_ANY in case of new addresses popping up.
	 * Packet reception on this transport is taken as a hint to reprobe the
	 * interface list.
	 */
	if (!bind_family || (bind_family & BIND_FAMILY_INET4)) {
		default_transport = virtual_bind_ADDR_ANY(AF_INET);
		if (!default_transport)
			return;
		LIST_INSERT_HEAD(&virtual_listen_list,
		    (struct virtual_transport *)default_transport, link);
		transport_reference(default_transport);
	}

	if (!bind_family || (bind_family & BIND_FAMILY_INET6)) {
		default_transport6 = virtual_bind_ADDR_ANY(AF_INET6);
		if (!default_transport6)
			return;
		LIST_INSERT_HEAD(&virtual_listen_list,
		    (struct virtual_transport *)default_transport6, link);
		transport_reference(default_transport6);
	}
}

struct virtual_transport *
virtual_get_default(sa_family_t af)
{
	switch (af) {
	case AF_INET:
		return (struct virtual_transport *)default_transport;
	case AF_INET6:
		return (struct virtual_transport *)default_transport6;
	default:
		return 0;
	}
}

/*
 * Probe the interface list and determine what new interfaces have
 * appeared.
 *
 * At the same time, we try to determine whether existing interfaces have
 * been rendered invalid; we do this by marking all virtual transports before
 * we call virtual_bind_if() through if_map(), and then releasing those
 * transports that have not been unmarked.
 */
void
virtual_reinit(void)
{
	struct virtual_transport *v, *v2;

	/* Mark all UDP transports, except the default ones. */
	for (v = LIST_FIRST(&virtual_listen_list); v; v = LIST_NEXT(v, link))
		if (&v->transport != default_transport &&
		    &v->transport != default_transport6)
			v->transport.flags |= TRANSPORT_MARK;

	/* Re-probe interface list.  */
	if (if_map(virtual_bind_if, 0) == -1)
		log_print("virtual_init: "
		    "could not bind the ISAKMP port(s) on all interfaces");

	/*
	 * Release listening transports for local addresses that no
	 * longer exist. virtual_bind_if () will have left those still marked.
	 */
	v = LIST_FIRST(&virtual_listen_list);
	while (v) {
		v2 = LIST_NEXT(v, link);
		if (v->transport.flags & TRANSPORT_MARK) {
			LIST_REMOVE(v, link);
			transport_release(&v->transport);
		}
		v = v2;
	}
}

struct virtual_transport *
virtual_listen_lookup(struct sockaddr *addr)
{
	struct virtual_transport *v;
	struct udp_transport	 *u;

	for (v = LIST_FIRST(&virtual_listen_list); v;
	    v = LIST_NEXT(v, link)) {
		if (!(u = (struct udp_transport *)v->main))
			if (!(u = (struct udp_transport *)v->encap)) {
				log_print("virtual_listen_lookup: "
				    "virtual %p has no low-level transports",
				    v);
				continue;
			}

		if (u->src->sa_family == addr->sa_family &&
		    sockaddr_addrlen(u->src) == sockaddr_addrlen(addr) &&
		    memcmp(sockaddr_addrdata (u->src), sockaddr_addrdata(addr),
		    sockaddr_addrlen(addr)) == 0)
			return v;
	}

	LOG_DBG((LOG_TRANSPORT, 40, "virtual_listen_lookup: no match"));
	return 0;
}

/*
 * Initialize an object of the VIRTUAL transport class.
 */
static struct transport *
virtual_bind(const struct sockaddr *addr)
{
	struct virtual_transport *v;
	struct sockaddr_storage	  tmp_sa;
	char	*stport;
	in_port_t port;

	v = calloc(1, sizeof *v);
	if (!v) {
		log_error("virtual_bind: calloc(1, %lu) failed",
		    (unsigned long)sizeof *v);
		return 0;
	}

	v->transport.vtbl = &virtual_transport_vtbl;

	memcpy(&tmp_sa, addr, SA_LEN(addr));

	/* Get port. */
	stport = udp_default_port ? udp_default_port : UDP_DEFAULT_PORT_STR;
	port = text2port(stport);
	if (port == 0) {
		log_print("virtual_bind: bad port \"%s\"", stport);
		free(v);
		return 0;
	}

	sockaddr_set_port((struct sockaddr *)&tmp_sa, port);
	v->main = udp_bind((struct sockaddr *)&tmp_sa);
	if (!v->main) {
		free(v);
		return 0;
	}
	v->main->virtual = (struct transport *)v;

	if (!disable_nat_t) {
		memcpy(&tmp_sa, addr, SA_LEN(addr));

		/* Get port. */
		stport = udp_encap_default_port
		    ? udp_encap_default_port : UDP_ENCAP_DEFAULT_PORT_STR;
		port = text2port(stport);
		if (port == 0) {
			log_print("virtual_bind: bad encap port \"%s\"",
			    stport);
			v->main->vtbl->remove(v->main);
			free(v);
			return 0;
		}

		sockaddr_set_port((struct sockaddr *)&tmp_sa, port);
		v->encap = udp_encap_bind((struct sockaddr *)&tmp_sa);
		if (!v->encap) {
			v->main->vtbl->remove(v->main);
			free(v);
			return 0;
		}
		v->encap->virtual = (struct transport *)v;
	}
	v->encap_is_active = 0;

	transport_setup(&v->transport, 1);
	v->transport.flags |= TRANSPORT_LISTEN;

	return (struct transport *)v;
}

static struct transport *
virtual_bind_ADDR_ANY(sa_family_t af)
{
	struct sockaddr_storage dflt_stor;
	struct sockaddr_in	*d4 = (struct sockaddr_in *)&dflt_stor;
	struct sockaddr_in6	*d6 = (struct sockaddr_in6 *)&dflt_stor;
	struct transport	*t;
	struct in6_addr		in6addr_any = IN6ADDR_ANY_INIT;

	bzero(&dflt_stor, sizeof dflt_stor);
	switch (af) {
	case AF_INET:
		d4->sin_family = af;
		d4->sin_len = sizeof(struct sockaddr_in);
		d4->sin_addr.s_addr = INADDR_ANY;
		break;

	case AF_INET6:
		d6->sin6_family = af;
		d6->sin6_len = sizeof(struct sockaddr_in6);
		memcpy(&d6->sin6_addr.s6_addr, &in6addr_any,
		    sizeof in6addr_any);
		break;
	}

	t = virtual_bind((struct sockaddr *)&dflt_stor);
	if (!t)
		log_error("virtual_bind_ADDR_ANY: "
		    "could not allocate default IPv%s ISAKMP port(s)",
		    af == AF_INET ? "4" : "6");
	return t;
}

static int
virtual_bind_if(char *ifname, struct sockaddr *if_addr, void *arg)
{
	struct conf_list	*listen_on;
	struct virtual_transport *v;
	struct conf_list_node	*address;
	struct sockaddr		*addr;
	struct transport	*t;
	struct ifreq		flags_ifr;
	struct in6_ifreq	flags_ifr6;
	char	*addr_str;
	int	 s, error;

	if (sockaddr2text(if_addr, &addr_str, 0))
		addr_str = 0;

	LOG_DBG((LOG_TRANSPORT, 90,
	    "virtual_bind_if: interface %s family %s address %s",
	    ifname ? ifname : "<unknown>",
	    if_addr->sa_family == AF_INET ? "v4" :
	    (if_addr->sa_family == AF_INET6 ? "v6" : "<unknown>"),
	    addr_str ? addr_str : "<invalid>"));
	free(addr_str);

	/*
	 * Drop non-Internet stuff.
	 */
	if ((if_addr->sa_family != AF_INET ||
	    SA_LEN(if_addr) != sizeof (struct sockaddr_in)) &&
	    (if_addr->sa_family != AF_INET6 ||
	    SA_LEN(if_addr) != sizeof (struct sockaddr_in6)))
		return 0;

	/*
	 * Only create sockets for families we should listen to.
	 */
	if (bind_family)
		switch (if_addr->sa_family) {
		case AF_INET:
			if ((bind_family & BIND_FAMILY_INET4) == 0)
				return 0;
			break;
		case AF_INET6:
			if ((bind_family & BIND_FAMILY_INET6) == 0)
				return 0;
			break;
		default:
			return 0;
		}

	/*
	 * These special addresses are not useable as they have special meaning
	 * in the IP stack.
	 */
	if (if_addr->sa_family == AF_INET &&
	    (((struct sockaddr_in *)if_addr)->sin_addr.s_addr == INADDR_ANY ||
	    (((struct sockaddr_in *)if_addr)->sin_addr.s_addr == INADDR_NONE)))
		return 0;

	/*
	 * Go through the list of transports and see if we already have this
	 * address bound. If so, unmark the transport and skip it; this allows
	 * us to call this function when we suspect a new address has appeared.
	 */
	if ((v = virtual_listen_lookup(if_addr)) != 0) {
		LOG_DBG ((LOG_TRANSPORT, 90, "virtual_bind_if: "
		    "already bound"));
		v->transport.flags &= ~TRANSPORT_MARK;
		return 0;
	}

	/*
	 * Don't bother with interfaces that are down.
	 * Note: This socket is only used to collect the interface status,
	 * rtables and inet6 addresses.
	 */
	s = socket(if_addr->sa_family, SOCK_DGRAM, 0);
	if (s == -1) {
		log_error("virtual_bind_if: "
		    "socket (%d, SOCK_DGRAM, 0) failed", if_addr->sa_family);
		return -1;
	}
	strlcpy(flags_ifr.ifr_name, ifname, sizeof flags_ifr.ifr_name);
	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&flags_ifr) == -1) {
		log_error("virtual_bind_if: "
		    "ioctl (%d, SIOCGIFFLAGS, ...) failed", s);
		close(s);
		return -1;
	}
	if (!(flags_ifr.ifr_flags & IFF_UP)) {
		close(s);
		return 0;
	}
	/* Also skip tentative addresses during DAD since bind(2) would fail. */
	if (if_addr->sa_family == AF_INET6) {
		memset(&flags_ifr6, 0, sizeof(flags_ifr6));
		strlcpy(flags_ifr6.ifr_name, ifname, sizeof flags_ifr6.ifr_name);
		flags_ifr6.ifr_addr = *(struct sockaddr_in6 *)if_addr;
		if (ioctl(s, SIOCGIFAFLAG_IN6, (caddr_t)&flags_ifr6) == -1) {
			log_error("virtual_bind_if: "
			    "ioctl (%d, SIOCGIFAFLAG_IN6, ...) failed", s);
			close(s);
			return 0;
		}
		if (flags_ifr6.ifr_ifru.ifru_flags6 & (IN6_IFF_ANYCAST|
		    IN6_IFF_TENTATIVE|IN6_IFF_DUPLICATED|IN6_IFF_DETACHED)) {
			error = sockaddr2text(if_addr, &addr_str, 0);
			log_print("virtual_bind_if: "
			    "IPv6 address %s not ready (flags 0x%x)",
			    error ? "unknown" : addr_str,
			    flags_ifr6.ifr_ifru.ifru_flags6);
			/* XXX schedule an interface rescan */
			if (!error)
				free(addr_str);
			close(s);
			return 0;
		}
	}

	if (ioctl(s, SIOCGIFRDOMAIN, (caddr_t)&flags_ifr) == -1) {
		log_error("virtual_bind_if: "
		    "ioctl (%d, SIOCGIFRDOMAIN, ...) failed", s);
		close(s);
		return -1;
	}

	/*
	 * Ignore interfaces outside of our rtable
	 */
	if (getrtable() != flags_ifr.ifr_rdomainid) {
		close(s);
		return 0;
	}

	close(s);

	/* Set the port number to zero.  */
	switch (if_addr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)if_addr)->sin_port = htons(0);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)if_addr)->sin6_port = htons(0);
		break;
	default:
		log_print("virtual_bind_if: unsupported protocol family %d",
		    if_addr->sa_family);
		break;
	}

	/*
	 * If we are explicit about what addresses we can listen to, be sure
	 * to respect that option.
	 * This is quite wasteful redoing the list-run for every interface,
	 * but who cares?  This is not an operation that needs to be fast.
	 */
	listen_on = conf_get_list("General", "Listen-on");
	if (listen_on) {
		for (address = TAILQ_FIRST(&listen_on->fields); address;
		    address = TAILQ_NEXT(address, link)) {
			if (text2sockaddr(address->field, 0, &addr, 0, 0)) {
				log_print("virtual_bind_if: "
				    "invalid address %s in \"Listen-on\"",
				    address->field);
				continue;
			}

			/* If found, take the easy way out. */
			if (memcmp(addr, if_addr, SA_LEN(addr)) == 0) {
				free(addr);
				break;
			}
			free(addr);
		}
		conf_free_list(listen_on);

		/*
		 * If address is zero then we did not find the address among
		 * the ones we should listen to.
		 * XXX We do not discover if we do not find our listen
		 * addresses. Maybe this should be the other way round.
		 */
		if (!address)
			return 0;
	}

	t = virtual_bind(if_addr);
	if (!t) {
		error = sockaddr2text(if_addr, &addr_str, 0);
		log_print("virtual_bind_if: failed to create a socket on %s",
		    error ? "unknown" : addr_str);
		if (!error)
			free(addr_str);
		return -1;
	}
	LIST_INSERT_HEAD(&virtual_listen_list, (struct virtual_transport *)t,
	    link);
	transport_reference(t);
	return 0;
}

static struct transport *
virtual_clone(struct transport *vt, struct sockaddr *raddr)
{
	struct virtual_transport *v = (struct virtual_transport *)vt;
	struct virtual_transport *v2;
	struct transport	 *t;
	char			 *stport;
	in_port_t		  port;

	t = malloc(sizeof *v);
	if (!t) {
		log_error("virtual_clone: malloc(%lu) failed",
		    (unsigned long)sizeof *v);
		return 0;
	}
	v2 = (struct virtual_transport *)t;

	memcpy(v2, v, sizeof *v);
	/* Remove the copy's links into virtual_listen_list.  */
	memset(&v2->link, 0, sizeof v2->link);

	if (v->encap_is_active)
		v2->main = 0; /* No need to clone this.  */
	else {
		v2->main = v->main->vtbl->clone(v->main, raddr);
		v2->main->virtual = (struct transport *)v2;
	}
	if (!disable_nat_t) {
		stport = udp_encap_default_port ? udp_encap_default_port :
		    UDP_ENCAP_DEFAULT_PORT_STR;
		port = text2port(stport);
		if (port == 0) {
			log_print("virtual_clone: port string \"%s\" not convertible "
			    "to in_port_t", stport);
			free(t);
			return 0;
		}
		sockaddr_set_port(raddr, port);
		v2->encap = v->encap->vtbl->clone(v->encap, raddr);
		v2->encap->virtual = (struct transport *)v2;
	}
	LOG_DBG((LOG_TRANSPORT, 50, "virtual_clone: old %p new %p (%s is %p)",
	    v, t, v->encap_is_active ? "encap" : "main",
	    v->encap_is_active ? v2->encap : v2->main));

	t->flags &= ~TRANSPORT_LISTEN;
	transport_setup(t, 1);
	return t;
}

static struct transport *
virtual_create(char *name)
{
	struct virtual_transport *v;
	struct transport	 *t, *t2 = 0;

	t = transport_create("udp_physical", name);
	if (!t)
		return 0;

	if (!disable_nat_t) {
		t2 = transport_create("udp_encap", name);
		if (!t2) {
			t->vtbl->remove(t);
			return 0;
		}
	}

	v = calloc(1, sizeof *v);
	if (!v) {
		log_error("virtual_create: calloc(1, %lu) failed",
		    (unsigned long)sizeof *v);
		t->vtbl->remove(t);
		if (t2)
			t2->vtbl->remove(t2);
		return 0;
	}

	memcpy(v, t, sizeof *t);
	v->transport.virtual = 0;
	v->main = t;
	v->encap = t2;
	v->transport.vtbl = &virtual_transport_vtbl;
	t->virtual = (struct transport *)v;
	if (t2)
		t2->virtual = (struct transport *)v;
	transport_setup(&v->transport, 1);
	return (struct transport *)v;
}

static void
virtual_remove(struct transport *t)
{
	struct virtual_transport *p, *v = (struct virtual_transport *)t;

	if (v->encap)
		v->encap->vtbl->remove(v->encap);
	if (v->main)
		v->main->vtbl->remove(v->main);

	for (p = LIST_FIRST(&virtual_listen_list); p && p != v; p =
	    LIST_NEXT(p, link))
		;
	if (p == v)
		LIST_REMOVE(v, link);

	LOG_DBG((LOG_TRANSPORT, 90, "virtual_remove: removed %p", v));
	free(t);
}

static void
virtual_report(struct transport *t)
{
}

static void
virtual_handle_message(struct transport *t)
{
	/*
	 * As per the NAT-T draft, in case we have already switched ports,
	 * any messages received on the old (500) port SHOULD be discarded.
	 * (Actually, while phase 1 messages should be discarded,
	 *  informational exchanges MAY be processed normally. For now, we
	 *  discard them all.)
	 */
	if (((struct virtual_transport *)t->virtual)->encap_is_active &&
	    ((struct virtual_transport *)t->virtual)->main == t) {
		LOG_DBG((LOG_MESSAGE, 10, "virtual_handle_message: "
		    "message on old port discarded"));
		return;
	}

	t->vtbl->handle_message(t);
}

static int
virtual_send_message(struct message *msg, struct transport *t)
{
	struct virtual_transport *v =
	    (struct virtual_transport *)msg->transport;
	struct sockaddr *dst;
	in_port_t port, default_port;

	/*
	 * Activate NAT-T Encapsulation if
	 *   - the exchange says we can, and
	 *   - in ID_PROT, after step 4 (draft-ietf-ipsec-nat-t-ike-03), or
	 *   - in other exchange (Aggressive, ), asap
	 * XXX ISAKMP_EXCH_BASE etc?
	 */

	if (msg->flags & MSG_NATT) {
		msg->exchange->flags |= EXCHANGE_FLAG_NAT_T_ENABLE;
		msg->exchange->flags |= EXCHANGE_FLAG_NAT_T_CAP_PEER;
	}

	if ((v->encap_is_active == 0 &&
	    (msg->exchange->flags & EXCHANGE_FLAG_NAT_T_ENABLE) &&
	    (msg->exchange->type != ISAKMP_EXCH_ID_PROT ||
		msg->exchange->step > 4)) || (msg->flags & MSG_NATT)) {
		LOG_DBG((LOG_MESSAGE, 10, "virtual_send_message: "
		    "enabling NAT-T encapsulation for this exchange"));
		v->encap_is_active++;

		/* Copy destination port if it is translated (NAT).  */
		v->main->vtbl->get_dst(v->main, &dst);
		port = ntohs(sockaddr_port(dst));

		if (udp_default_port)
			default_port = text2port(udp_default_port);
		else
			default_port = UDP_DEFAULT_PORT;
		if (port != default_port) {
			v->main->vtbl->get_dst(v->encap, &dst);
			sockaddr_set_port(dst, port);
		}
	}

	if (v->encap_is_active)
		return v->encap->vtbl->send_message(msg, v->encap);
	else
		return v->main->vtbl->send_message(msg, v->main);
}

static void
virtual_get_src(struct transport *t, struct sockaddr **s)
{
	struct virtual_transport *v = (struct virtual_transport *)t;

	if (v->encap_is_active)
		v->encap->vtbl->get_src(v->encap, s);
	else
		v->main->vtbl->get_src(v->main, s);
}

static void
virtual_get_dst(struct transport *t, struct sockaddr **s)
{
	struct virtual_transport *v = (struct virtual_transport *)t;

	if (v->encap_is_active)
		v->encap->vtbl->get_dst(v->encap, s);
	else
		v->main->vtbl->get_dst(v->main, s);
}

static char *
virtual_decode_ids(struct transport *t)
{
	struct virtual_transport *v = (struct virtual_transport *)t;

	if (v->encap_is_active)
		return v->encap->vtbl->decode_ids(t);
	else
		return v->main->vtbl->decode_ids(t);
}

static struct msg_head *
virtual_get_queue(struct message *msg)
{
	if (msg->flags & MSG_PRIORITIZED)
		return &msg->transport->prio_sendq;
	else
		return &msg->transport->sendq;
}
