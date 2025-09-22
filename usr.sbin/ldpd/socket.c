/*	$OpenBSD: socket.c,v 1.10 2019/06/28 13:32:48 deraadt Exp $ */

/*
 * Copyright (c) 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

int
ldp_create_socket(int af, enum socket_type type)
{
	int			 fd, domain, proto;
	union ldpd_addr		 addr;
	struct sockaddr_storage	 local_sa;
	int			 opt;

	/* create socket */
	switch (type) {
	case LDP_SOCKET_DISC:
	case LDP_SOCKET_EDISC:
		domain = SOCK_DGRAM;
		proto = IPPROTO_UDP;
		break;
	case LDP_SOCKET_SESSION:
		domain = SOCK_STREAM;
		proto = IPPROTO_TCP;
		break;
	default:
		fatalx("ldp_create_socket: unknown socket type");
	}
	fd = socket(af, domain | SOCK_NONBLOCK | SOCK_CLOEXEC, proto);
	if (fd == -1) {
		log_warn("%s: error creating socket", __func__);
		return (-1);
	}

	/* bind to a local address/port */
	switch (type) {
	case LDP_SOCKET_DISC:
		/* listen on all addresses */
		memset(&addr, 0, sizeof(addr));
		memcpy(&local_sa, addr2sa(af, &addr, LDP_PORT),
		    sizeof(local_sa));
		break;
	case LDP_SOCKET_EDISC:
	case LDP_SOCKET_SESSION:
		addr = (ldp_af_conf_get(ldpd_conf, af))->trans_addr;
		memcpy(&local_sa, addr2sa(af, &addr, LDP_PORT),
		    sizeof(local_sa));
		if (sock_set_bindany(fd, 1) == -1) {
			close(fd);
			return (-1);
		}
		break;
	}
	if (sock_set_reuse(fd, 1) == -1) {
		close(fd);
		return (-1);
	}
	if (bind(fd, (struct sockaddr *)&local_sa, local_sa.ss_len) == -1) {
		log_warn("%s: error binding socket", __func__);
		close(fd);
		return (-1);
	}

	/* set options */
	switch (af) {
	case AF_INET:
		if (sock_set_ipv4_tos(fd, IPTOS_PREC_INTERNETCONTROL) == -1) {
			close(fd);
			return (-1);
		}
		if (type == LDP_SOCKET_DISC) {
			if (sock_set_ipv4_mcast_ttl(fd,
			    IP_DEFAULT_MULTICAST_TTL) == -1) {
				close(fd);
				return (-1);
			}
			if (sock_set_ipv4_mcast_loop(fd) == -1) {
				close(fd);
				return (-1);
			}
		}
		if (type == LDP_SOCKET_DISC || type == LDP_SOCKET_EDISC) {
			if (sock_set_ipv4_recvif(fd, 1) == -1) {
				close(fd);
				return (-1);
			}
		}
		if (type == LDP_SOCKET_SESSION) {
			if (sock_set_ipv4_ucast_ttl(fd, 255) == -1) {
				close(fd);
				return (-1);
			}
		}
		break;
	case AF_INET6:
		if (sock_set_ipv6_dscp(fd, IPTOS_PREC_INTERNETCONTROL) == -1) {
			close(fd);
			return (-1);
		}
		if (type == LDP_SOCKET_DISC) {
			if (sock_set_ipv6_mcast_loop(fd) == -1) {
				close(fd);
				return (-1);
			}
			if (sock_set_ipv6_mcast_hops(fd, 255) == -1) {
				close(fd);
				return (-1);
			}
			if (!(ldpd_conf->ipv6.flags & F_LDPD_AF_NO_GTSM)) {
				if (sock_set_ipv6_minhopcount(fd, 255) == -1) {
					close(fd);
					return (-1);
				}
			}
		}
		if (type == LDP_SOCKET_DISC || type == LDP_SOCKET_EDISC) {
			if (sock_set_ipv6_pktinfo(fd, 1) == -1) {
				close(fd);
				return (-1);
			}
		}
		if (type == LDP_SOCKET_SESSION) {
			if (sock_set_ipv6_ucast_hops(fd, 255) == -1) {
				close(fd);
				return (-1);
			}
		}
		break;
	}
	switch (type) {
	case LDP_SOCKET_DISC:
	case LDP_SOCKET_EDISC:
		sock_set_recvbuf(fd);
		break;
	case LDP_SOCKET_SESSION:
		if (listen(fd, LDP_BACKLOG) == -1)
			log_warn("%s: error listening on socket", __func__);

		opt = 1;
		if (setsockopt(fd, IPPROTO_TCP, TCP_MD5SIG, &opt,
		    sizeof(opt)) == -1) {
			if (errno == ENOPROTOOPT) {	/* system w/o md5sig */
				log_warnx("md5sig not available, disabling");
				sysdep.no_md5sig = 1;
			} else {
				close(fd);
				return (-1);
			}
		}
		break;
	}

	return (fd);
}

void
sock_set_recvbuf(int fd)
{
	int	bsize;

	bsize = 65535;
	while (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bsize,
	    sizeof(bsize)) == -1)
		bsize /= 2;
}

int
sock_set_reuse(int fd, int enable)
{
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable,
	    sizeof(int)) == -1) {
		log_warn("%s: error setting SO_REUSEADDR", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_bindany(int fd, int enable)
{
	if (setsockopt(fd, SOL_SOCKET, SO_BINDANY, &enable,
	    sizeof(int)) == -1) {
		log_warn("%s: error setting SO_BINDANY", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_tos(int fd, int tos)
{
	if (setsockopt(fd, IPPROTO_IP, IP_TOS, (int *)&tos, sizeof(tos)) == -1) {
		log_warn("%s: error setting IP_TOS to 0x%x", __func__, tos);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_recvif(int fd, int enable)
{
	if (setsockopt(fd, IPPROTO_IP, IP_RECVIF, &enable,
	    sizeof(enable)) == -1) {
		log_warn("%s: error setting IP_RECVIF", __func__);
		return (-1);
	}
	return (0);
}

int
sock_set_ipv4_minttl(int fd, int ttl)
{
	if (setsockopt(fd, IPPROTO_IP, IP_MINTTL, &ttl, sizeof(ttl)) == -1) {
		log_warn("%s: error setting IP_MINTTL", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_ucast_ttl(int fd, int ttl)
{
	if (setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == -1) {
		log_warn("%s: error setting IP_TTL", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_mcast_ttl(int fd, uint8_t ttl)
{
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
	    (char *)&ttl, sizeof(ttl)) == -1) {
		log_warn("%s: error setting IP_MULTICAST_TTL to %d",
		    __func__, ttl);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_mcast(struct iface *iface)
{
	in_addr_t		 addr;

	addr = if_get_ipv4_addr(iface);

	if (setsockopt(global.ipv4.ldp_disc_socket, IPPROTO_IP, IP_MULTICAST_IF,
	    &addr, sizeof(addr)) == -1) {
		log_warn("%s: error setting IP_MULTICAST_IF, interface %s",
		    __func__, iface->name);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv4_mcast_loop(int fd)
{
	uint8_t	loop = 0;

	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
	    (char *)&loop, sizeof(loop)) == -1) {
		log_warn("%s: error setting IP_MULTICAST_LOOP", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_dscp(int fd, int dscp)
{
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &dscp,
	    sizeof(dscp)) == -1) {
		log_warn("%s: error setting IPV6_TCLASS", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_pktinfo(int fd, int enable)
{
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &enable,
	    sizeof(enable)) == -1) {
		log_warn("%s: error setting IPV6_RECVPKTINFO", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_minhopcount(int fd, int hoplimit)
{
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MINHOPCOUNT,
	    &hoplimit, sizeof(hoplimit)) == -1) {
		log_warn("%s: error setting IPV6_MINHOPCOUNT", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_ucast_hops(int fd, int hoplimit)
{
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
	    &hoplimit, sizeof(hoplimit)) == -1) {
		log_warn("%s: error setting IPV6_UNICAST_HOPS", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_mcast_hops(int fd, int hoplimit)
{
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
	    &hoplimit, sizeof(hoplimit)) == -1) {
		log_warn("%s: error setting IPV6_MULTICAST_HOPS", __func__);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_mcast(struct iface *iface)
{
	if (setsockopt(global.ipv6.ldp_disc_socket, IPPROTO_IPV6,
	    IPV6_MULTICAST_IF, &iface->ifindex, sizeof(iface->ifindex)) == -1) {
		log_warn("%s: error setting IPV6_MULTICAST_IF, interface %s",
		    __func__, iface->name);
		return (-1);
	}

	return (0);
}

int
sock_set_ipv6_mcast_loop(int fd)
{
	unsigned int	loop = 0;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
	    &loop, sizeof(loop)) == -1) {
		log_warn("%s: error setting IPV6_MULTICAST_LOOP", __func__);
		return (-1);
	}

	return (0);
}
