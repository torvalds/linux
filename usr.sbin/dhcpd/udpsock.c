/*	$OpenBSD: udpsock.c,v 1.12 2025/05/22 04:24:11 dlg Exp $	*/

/*
 * Copyright (c) 2014 YASUOKA Masahiko <yasuoka@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

void	 udpsock_handler (struct protocol *);
ssize_t	 udpsock_send_packet(struct interface_info *, struct dhcp_packet *,
    size_t, struct in_addr, struct sockaddr_in *, struct hardware *);

struct udpsock {
	int	 sock;
};

void
udpsock_startup(struct in_addr bindaddr)
{
	int			 sock, onoff;
	struct sockaddr_in	 sin4;
	struct udpsock		*udpsock;

	if ((udpsock = calloc(1, sizeof(struct udpsock))) == NULL)
		fatal("could not create udpsock");

	memset(&sin4, 0, sizeof(sin4));
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		fatal("creating a socket failed for udp");

	onoff = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_RECVIF, &onoff, sizeof(onoff)) !=
	    0)
		fatal("setsocketopt IP_RECVIF failed for udp");

	sin4.sin_family = AF_INET;
	sin4.sin_len = sizeof(sin4);
	sin4.sin_addr = bindaddr;
	sin4.sin_port = server_port;

	if (bind(sock, (struct sockaddr *)&sin4, sizeof(sin4)) != 0)
		fatal("bind failed for udp");

	udpsock->sock = sock;

	add_protocol("udp", sock, udpsock_handler, udpsock);
	log_info("Listening on %s:%d/udp.", inet_ntoa(sin4.sin_addr),
	    ntohs(server_port));
}

void
udpsock_handler(struct protocol *protocol)
{
	int			 sockio;
	char			 cbuf[256], ifname[IF_NAMESIZE];
	ssize_t			 len;
	struct udpsock		*udpsock = protocol->local;
	struct msghdr		 m;
	struct cmsghdr		*cm;
	struct iovec		 iov[1];
	struct sockaddr_storage	 ss;
	struct sockaddr_in	*sin4;
	struct sockaddr_dl	*sdl = NULL;
	struct interface_info	 iface;
	struct iaddr		 from, addr;
	unsigned char		 packetbuf[4095];
	struct dhcp_packet	*packet = (struct dhcp_packet *)packetbuf;
	struct hardware		 hw;
	struct ifreq		 ifr;
	struct subnet		*subnet;

	memset(&hw, 0, sizeof(hw));

	iov[0].iov_base = packetbuf;
	iov[0].iov_len = sizeof(packetbuf);
	memset(&m, 0, sizeof(m));
	m.msg_name = &ss;
	m.msg_namelen = sizeof(ss);
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof(cbuf);

	memset(&iface, 0, sizeof(iface));
	if ((len = recvmsg(udpsock->sock, &m, 0)) == -1) {
		log_warn("receiving a DHCP message failed");
		return;
	}
	if (ss.ss_family != AF_INET) {
		log_warnx("received DHCP message is not AF_INET");
		return;
	}
	sin4 = (struct sockaddr_in *)&ss;
	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(&m);
	    m.msg_controllen != 0 && cm;
	    cm = (struct cmsghdr *)CMSG_NXTHDR(&m, cm)) {
		if (cm->cmsg_level == IPPROTO_IP &&
		    cm->cmsg_type == IP_RECVIF)
			sdl = (struct sockaddr_dl *)CMSG_DATA(cm);
	}
	if (sdl == NULL) {
		log_warnx("could not get the received interface by IP_RECVIF");
		return;
	}
	if_indextoname(sdl->sdl_index, ifname);

	if ((sockio = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_warn("socket creation failed");
		return;
	}
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sockio, SIOCGIFADDR, &ifr, sizeof(ifr)) == -1) {
		log_warn("Failed to get address for %s", ifname);
		close(sockio);
		return;
	}
	close(sockio);

	if (ifr.ifr_addr.sa_family != AF_INET)
		return;

	iface.is_udpsock = 1;
	iface.send_packet = udpsock_send_packet;
	iface.wfdesc = udpsock->sock;
	iface.ifp = &ifr;
	iface.index = sdl->sdl_index;
	iface.primary_address =
	    ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
	strlcpy(iface.name, ifname, sizeof(iface.name));

	addr.len = 4;
	memcpy(&addr.iabuf, &iface.primary_address, addr.len);

	if ((subnet = find_subnet(addr)) == NULL)
		return;
	iface.shared_network = subnet->shared_network ;
	from.len = 4;
	memcpy(&from.iabuf, &sin4->sin_addr, from.len);
	do_packet(&iface, packet, len, sin4->sin_port, from, &hw);
}

ssize_t
udpsock_send_packet(struct interface_info *interface, struct dhcp_packet *raw,
    size_t len, struct in_addr from, struct sockaddr_in *to,
    struct hardware *hto)
{
	return (sendto(interface->wfdesc, raw, len, 0, (struct sockaddr *)to,
	    sizeof(struct sockaddr_in)));
}
