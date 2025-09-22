/*	$OpenBSD: udp_encap.c,v 1.24 2022/01/16 14:30:11 naddy Exp $	*/

/*
 * Copyright (c) 1998, 1999, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2000 Angelos D. Keromytis.  All rights reserved.
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
#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "if.h"
#include "ipsec_doi.h"
#include "isakmp.h"
#include "log.h"
#include "message.h"
#include "monitor.h"
#include "transport.h"
#include "udp.h"
#include "udp_encap.h"
#include "util.h"
#include "virtual.h"

#define UDP_SIZE 65536

/* If a system doesn't have SO_REUSEPORT, SO_REUSEADDR will have to do.  */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

/* Reused, from udp.c */
struct transport *udp_clone(struct transport *, struct sockaddr *);
int		  udp_fd_set(struct transport *, fd_set *, int);
int		  udp_fd_isset(struct transport *, fd_set *);
void		  udp_get_dst(struct transport *, struct sockaddr **);
void		  udp_get_src(struct transport *, struct sockaddr **);
char		 *udp_decode_ids(struct transport *);
void		  udp_remove(struct transport *);

static struct transport *udp_encap_create(char *);
static void		 udp_encap_report(struct transport *);
static void		 udp_encap_handle_message(struct transport *);
static struct transport *udp_encap_make(struct sockaddr *);
static int		 udp_encap_send_message(struct message *,
    struct transport *);

static struct transport_vtbl udp_encap_transport_vtbl = {
	{ 0 }, "udp_encap",
	udp_encap_create,
	0,
	udp_remove,
	udp_encap_report,
	udp_fd_set,
	udp_fd_isset,
	udp_encap_handle_message,
	udp_encap_send_message,
	udp_get_dst,
	udp_get_src,
	udp_decode_ids,
	udp_clone,
	0
};

char	 *udp_encap_default_port = 0;

void
udp_encap_init(void)
{
	transport_method_add(&udp_encap_transport_vtbl);
}

/* Create a UDP transport structure bound to LADDR just for listening.  */
static struct transport *
udp_encap_make(struct sockaddr *laddr)
{
	struct udp_transport *t = 0;
	int	 s, on, wildcardaddress = 0;
	char	*tstr;

	t = calloc(1, sizeof *t);
	if (!t) {
		log_print("udp_encap_make: malloc(%lu) failed",
		    (unsigned long)sizeof *t);
		free(laddr);
		return 0;
	}
	t->src = laddr;

	s = socket(laddr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		log_error("udp_encap_make: socket (%d, %d, %d)",
		    laddr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
		goto err;
	}

	/* Make sure we don't get our traffic encrypted.  */
	if (sysdep_cleartext(s, laddr->sa_family) == -1)
		goto err;

	/* Wildcard address ?  */
	switch (laddr->sa_family) {
	case AF_INET:
		if (((struct sockaddr_in *)laddr)->sin_addr.s_addr
		    == INADDR_ANY)
			wildcardaddress = 1;
		break;
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *)laddr)->sin6_addr))
			wildcardaddress = 1;
		break;
	}

	/*
	 * In order to have several bound specific address-port combinations
	 * with the same port SO_REUSEADDR is needed.
	 * If this is a wildcard socket and we are not listening there, but
	 * only sending from it make sure it is entirely reuseable with
	 * SO_REUSEPORT.
	 */
	on = 1;
	if (setsockopt(s, SOL_SOCKET,
	    wildcardaddress ? SO_REUSEPORT : SO_REUSEADDR,
	    (void *)&on, sizeof on) == -1) {
		log_error("udp_encap_make: setsockopt (%d, %d, %d, %p, %lu)",
		    s, SOL_SOCKET,
		    wildcardaddress ? SO_REUSEPORT : SO_REUSEADDR, &on,
		    (unsigned long)sizeof on);
		goto err;
	}

	t->transport.vtbl = &udp_encap_transport_vtbl;
	if (monitor_bind(s, t->src, SA_LEN(t->src))) {
		if (sockaddr2text(t->src, &tstr, 0))
			log_error("udp_encap_make: bind (%d, %p, %lu)", s,
			    &t->src, (unsigned long)sizeof t->src);
		else {
			log_error("udp_encap_make: bind (%d, %s, %lu)", s,
			    tstr, (unsigned long)sizeof t->src);
			free(tstr);
		}
		goto err;
	}

	t->s = s;
	if (sockaddr2text(t->src, &tstr, 0))
		LOG_DBG((LOG_MISC, 20, "udp_encap_make: "
		    "transport %p socket %d family %d", t, s,
		    t->src->sa_family == AF_INET ? 4 : 6));
	else {
		LOG_DBG((LOG_MISC, 20, "udp_encap_make: "
		    "transport %p socket %d ip %s port %d", t, s,
		    tstr, ntohs(sockaddr_port(t->src))));
		free(tstr);
	}
	transport_setup(&t->transport, 0);
	t->transport.flags |= TRANSPORT_LISTEN;
	return &t->transport;

err:
	if (s >= 0)
		close (s);
	if (t) {
		/* Already closed.  */
		t->s = -1;
		udp_remove(&t->transport);
	}
	return 0;
}

/*
 * Initialize an object of the UDP transport class.  Fill in the local
 * IP address and port information and create a server socket bound to
 * that specific port.  Add the polymorphic transport structure to the
 * system-wide pools of known ISAKMP transports.
 */
struct transport *
udp_encap_bind(const struct sockaddr *addr)
{
	struct sockaddr	*src;

	src = malloc(SA_LEN(addr));
	if (!src)
		return 0;

	memcpy(src, addr, SA_LEN(addr));
	return udp_encap_make(src);
}

/*
 * NAME is a section name found in the config database.  Setup and return
 * a transport useable to talk to the peer specified by that name.
 */
static struct transport *
udp_encap_create(char *name)
{
	struct virtual_transport *v;
	struct udp_transport	*u;
	struct transport	*rv;
	struct sockaddr		*dst, *addr;
	struct conf_list	*addr_list = 0;
	struct conf_list_node	*addr_node;
	char	*addr_str, *port_str;

	port_str = conf_get_str(name, "Port"); /* XXX "Encap-port" ? */
	if (!port_str)
		port_str = udp_encap_default_port;
	if (!port_str)
		port_str = UDP_ENCAP_DEFAULT_PORT_STR;

	addr_str = conf_get_str(name, "Address");
	if (!addr_str) {
		log_print("udp_encap_create: no address configured "
		    "for \"%s\"", name);
		return 0;
	}
	if (text2sockaddr(addr_str, port_str, &dst, 0, 0)) {
		log_print("udp_encap_create: address \"%s\" not understood",
		    addr_str);
		return 0;
	}

	addr_str = conf_get_str(name, "Local-address");
	if (!addr_str)
		addr_list = conf_get_list("General", "Listen-on");
	if (!addr_str && !addr_list) {
		v = virtual_get_default(dst->sa_family);
		u = (struct udp_transport *)v->encap;

		if (!u) {
			log_print("udp_encap_create: no default transport");
			rv = 0;
			goto ret;
		} else {
			rv = udp_clone((struct transport *)u, dst);
			if (rv)
				rv->vtbl = &udp_encap_transport_vtbl;
			goto ret;
		}
	}

	if (addr_list) {
		for (addr_node = TAILQ_FIRST(&addr_list->fields);
		    addr_node; addr_node = TAILQ_NEXT(addr_node, link))
			if (text2sockaddr(addr_node->field, port_str,
			    &addr, 0, 0) == 0) {
				v = virtual_listen_lookup(addr);
				free(addr);
				if (v) {
					addr_str = addr_node->field;
					break;
				}
			}
		if (!addr_str) {
			log_print("udp_encap_create: "
			    "no matching listener found");
			rv = 0;
			goto ret;
		}
	}
	if (text2sockaddr(addr_str, port_str, &addr, 0, 0)) {
		log_print("udp_encap_create: "
		    "address \"%s\" not understood", addr_str);
		rv = 0;
		goto ret;
	}
	v = virtual_listen_lookup(addr);
	free(addr);
	if (!v) {
		log_print("udp_encap_create: "
		    "%s:%s must exist as a listener too", addr_str, port_str);
		rv = 0;
		goto ret;
	}
	rv = udp_clone(v->encap, dst);
	if (rv)
		rv->vtbl = &udp_encap_transport_vtbl;

ret:
	if (addr_list)
		conf_free_list(addr_list);
	free(dst);
	return rv;
}

/* Report transport-method specifics of the T transport.  */
void
udp_encap_report(struct transport *t)
{
	struct udp_transport *u = (struct udp_transport *)t;
	char	 *src = NULL, *dst = NULL;
	in_port_t sport, dport;

	if (sockaddr2text(u->src, &src, 0))
		return;
	sport = sockaddr_port(u->src);

	if (!u->dst || sockaddr2text(u->dst, &dst, 0))
		dst = 0;
	dport = dst ? sockaddr_port(u->dst) : 0;

	LOG_DBG ((LOG_REPORT, 0, "udp_encap_report: fd %d src %s:%u dst %s:%u",
	    u->s, src, ntohs(sport), dst ? dst : "*", ntohs(dport)));

	free(dst);
	free(src);
}

/*
 * A message has arrived on transport T's socket.  If T is single-ended,
 * clone it into a double-ended transport which we will use from now on.
 * Package the message as we want it and continue processing in the message
 * module.
 */
static void
udp_encap_handle_message(struct transport *t)
{
	struct udp_transport	*u = (struct udp_transport *)t;
	struct sockaddr_storage	 from;
	struct message		*msg;
	u_int32_t	len = sizeof from;
	ssize_t		n;
	u_int8_t	buf[UDP_SIZE];

	n = recvfrom(u->s, buf, UDP_SIZE, 0, (struct sockaddr *)&from, &len);
	if (n == -1) {
		log_error("recvfrom (%d, %p, %d, %d, %p, %p)", u->s, buf,
		    UDP_SIZE, 0, &from, &len);
		return;
	}

	if (t->virtual == (struct transport *)virtual_get_default(AF_INET) ||
	    t->virtual == (struct transport *)virtual_get_default(AF_INET6)) {
		t->virtual->vtbl->reinit();

		/*
		 * As we don't know the actual destination address of the
		 * packet, we can't really deal with it. So, just ignore it
		 * and hope we catch the retransmission.
		 */
		return;
	}

	/*
	 * Make a specialized UDP transport structure out of the incoming
	 * transport and the address information we got from recvfrom(2).
	 */
	t = t->virtual->vtbl->clone(t->virtual, (struct sockaddr *)&from);
	if (!t)
		return;

	/* Check NULL-ESP marker.  */
	if (n < (ssize_t)sizeof(u_int32_t) || *(u_int32_t *)buf != 0) {
		/* Should never happen.  */
		log_print("udp_encap_handle_message: "
		    "Null-ESP marker not NULL or short message");
		return;
	}

	/* NAT-Keepalive messages should not be processed further.  */
	n -= sizeof(u_int32_t);
	if (n == 1 && buf[sizeof(u_int32_t)] == 0xFF)
		return;

	msg = message_alloc(t, buf + sizeof (u_int32_t), n);
	if (!msg) {
		log_error("failed to allocate message structure, dropping "
		    "packet received on transport %p", u);
		return;
	}

	msg->flags |= MSG_NATT;

	message_recv(msg);
}

/*
 * Physically send the message MSG over its associated transport.
 * Special: if 'msg' is NULL, send a NAT-T keepalive message.
 */
static int
udp_encap_send_message(struct message *msg, struct transport *t)
{
	struct udp_transport *u = (struct udp_transport *)t;
	struct msghdr	 m;
	struct iovec	*new_iov = 0, keepalive;
	ssize_t		 n;
	u_int32_t	 marker = 0;			/* NULL-ESP Marker */

	if (msg) {
		/* Construct new iov array, prefixing NULL-ESP Marker.  */
		new_iov = calloc(msg->iovlen + 1, sizeof *new_iov);
		if (!new_iov) {
			log_error ("udp_encap_send_message: "
			    "calloc(%lu, %lu) failed",
			    (unsigned long)msg->iovlen + 1,
			    (unsigned long)sizeof *new_iov);
			return -1;
		}
		new_iov[0].iov_base = &marker;
		new_iov[0].iov_len = IPSEC_SPI_SIZE;
		memcpy (new_iov + 1, msg->iov, msg->iovlen * sizeof *new_iov);
	} else {
		marker = ~marker;
		keepalive.iov_base = &marker;
		keepalive.iov_len = 1;
	}

	/*
	 * Sending on connected sockets requires that no destination address is
	 * given, or else EISCONN will occur.
	 */
	m.msg_name = (caddr_t)u->dst;
	m.msg_namelen = SA_LEN(u->dst);
	m.msg_iov = msg ? new_iov : &keepalive;
	m.msg_iovlen = msg ? msg->iovlen + 1 : 1;
	m.msg_control = 0;
	m.msg_controllen = 0;
	m.msg_flags = 0;
	n = sendmsg (u->s, &m, 0);
	if (msg)
		free (new_iov);
	if (n == -1) {
		/* XXX We should check whether the address has gone away */
		log_error ("sendmsg (%d, %p, %d)", u->s, &m, 0);
		return -1;
	}
	return 0;
}
