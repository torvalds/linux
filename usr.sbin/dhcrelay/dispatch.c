/*	$OpenBSD: dispatch.c,v 1.23 2021/01/17 13:40:59 claudio Exp $	*/

/*
 * Copyright 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.   All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "dhcpd.h"
#include "log.h"

/*
 * Macros implementation used to generate link-local addresses. This
 * code was copied from: sys/netinet6/in6_ifattach.c.
 */
#define EUI64_UBIT		0x02
#define EUI64_TO_IFID(in6) \
	do { (in6)->s6_addr[8] ^= EUI64_UBIT; } while (0)

struct protocol *protocols;
struct timeout *timeouts;
static struct timeout *free_timeouts;
static int interfaces_invalidated;

void (*bootp_packet_handler)(struct interface_info *,
    struct dhcp_packet *, int, struct packet_ctx *);

static int interface_status(struct interface_info *ifinfo);

struct interface_info *
iflist_getbyname(const char *name)
{
	struct interface_info	*intf;

	TAILQ_FOREACH(intf, &intflist, entry) {
		if (strcmp(intf->name, name) != 0)
			continue;

		return intf;
	}

	return NULL;
}

void
setup_iflist(void)
{
	struct interface_info		*intf;
	struct sockaddr_dl		*sdl;
	struct ifaddrs			*ifap, *ifa;
	struct if_data			*ifi;
	struct sockaddr_in		*sin;
	struct sockaddr_in6		*sin6;

	TAILQ_INIT(&intflist);
	if (getifaddrs(&ifap))
		fatalx("getifaddrs failed");

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_LOOPBACK) ||
		    (ifa->ifa_flags & IFF_POINTOPOINT))
			continue;

		/* Find interface or create it. */
		intf = iflist_getbyname(ifa->ifa_name);
		if (intf == NULL) {
			intf = calloc(1, sizeof(*intf));
			if (intf == NULL)
				fatal("calloc");

			strlcpy(intf->name, ifa->ifa_name,
			    sizeof(intf->name));
			TAILQ_INSERT_HEAD(&intflist, intf, entry);
		}

		/* Signal disabled interface. */
		if ((ifa->ifa_flags & IFF_UP) == 0)
			intf->dead = 1;

		if (ifa->ifa_addr->sa_family == AF_LINK) {
			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			ifi = (struct if_data *)ifa->ifa_data;

			/* Skip unsupported interfaces. */
			if (ifi->ifi_type != IFT_ETHER &&
			    ifi->ifi_type != IFT_ENC &&
			    ifi->ifi_type != IFT_CARP) {
				TAILQ_REMOVE(&intflist, intf, entry);
				free(intf);
				continue;
			}

			if (ifi->ifi_type == IFT_ENC)
				intf->hw_address.htype = HTYPE_IPSEC_TUNNEL;
			else
				intf->hw_address.htype = HTYPE_ETHER;

			intf->index = sdl->sdl_index;
			intf->hw_address.hlen = sdl->sdl_alen;
			memcpy(intf->hw_address.haddr,
			    LLADDR(sdl), sdl->sdl_alen);
		} else if (ifa->ifa_addr->sa_family == AF_INET) {
			sin = (struct sockaddr_in *)ifa->ifa_addr;
			if (sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK) ||
			    intf->primary_address.s_addr != INADDR_ANY)
				continue;

			intf->primary_address = sin->sin_addr;
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				intf->linklocal = sin6->sin6_addr;
#ifdef __KAME__
				/*
				 * Remove possible scope from address if
				 * link-local.
				 */
				intf->linklocal.s6_addr[2] = 0;
				intf->linklocal.s6_addr[3] = 0;
#endif
			} else
				intf->gipv6 = 1;

			/* At least one IPv6 address was found. */
			intf->ipv6 = 1;
		}
	}

	freeifaddrs(ifap);

	/*
	 * Generate link-local IPv6 address for interfaces without it.
	 *
	 * For IPv6 DHCP Relay it doesn't matter what is used for
	 * link-addr field, so let's generate an address that won't
	 * change during execution so we can always find the interface
	 * to relay packets back. This is only used for layer 2 relaying
	 * when the interface might not have an address.
	 */
	TAILQ_FOREACH(intf, &intflist, entry) {
		if (memcmp(&intf->linklocal, &in6addr_any,
		    sizeof(in6addr_any)) != 0)
			continue;

		intf->linklocal.s6_addr[0] = 0xfe;
		intf->linklocal.s6_addr[1] = 0x80;
		intf->linklocal.s6_addr[8] = intf->hw_address.haddr[0];
		intf->linklocal.s6_addr[9] = intf->hw_address.haddr[1];
		intf->linklocal.s6_addr[10] = intf->hw_address.haddr[2];
		intf->linklocal.s6_addr[11] = 0xff;
		intf->linklocal.s6_addr[12] = 0xfe;
		intf->linklocal.s6_addr[13] = intf->hw_address.haddr[3];
		intf->linklocal.s6_addr[14] = intf->hw_address.haddr[4];
		intf->linklocal.s6_addr[15] = intf->hw_address.haddr[5];
		EUI64_TO_IFID(&intf->linklocal);
	}
}

struct interface_info *
register_interface(const char *ifname, void (*handler)(struct protocol *),
    int isserver)
{
	struct interface_info		*intf;

	if ((intf = iflist_getbyname(ifname)) == NULL)
		return NULL;

	/* Don't register disabled interfaces. */
	if (intf->dead)
		return NULL;

	/* Check if we already registered the interface. */
	if (intf->ifr.ifr_name[0] != 0)
		return intf;

	if (strlcpy(intf->ifr.ifr_name, ifname,
	    sizeof(intf->ifr.ifr_name)) >= sizeof(intf->ifr.ifr_name))
		fatalx("interface name '%s' too long", ifname);

	if_register_receive(intf, isserver);
	if_register_send(intf);
	add_protocol(intf->name, intf->rfdesc, handler, intf);

	return intf;
}

/*
 * Wait for packets to come in using poll().  When a packet comes in,
 * call receive_packet to receive the packet and possibly strip hardware
 * addressing information from it, and then call through the
 * bootp_packet_handler hook to try to do something with it.
 */
void
dispatch(void)
{
	int count, i, to_msec, nfds = 0;
	struct protocol *l;
	struct pollfd *fds;
	time_t howlong;

	nfds = 0;
	for (l = protocols; l; l = l->next)
		nfds++;

	fds = calloc(nfds, sizeof(struct pollfd));
	if (fds == NULL)
		fatalx("Can't allocate poll structures.");

	do {
		/*
		 * Call any expired timeouts, and then if there's still
		 * a timeout registered, time out the select call then.
		 */
another:
		if (timeouts) {
			if (timeouts->when <= cur_time) {
				struct timeout *t = timeouts;

				timeouts = timeouts->next;
				(*(t->func))(t->what);
				t->next = free_timeouts;
				free_timeouts = t;
				goto another;
			}

			/*
			 * Figure timeout in milliseconds, and check for
			 * potential overflow, so we can cram into an
			 * int for poll, while not polling with a
			 * negative timeout and blocking indefinitely.
			 */
			howlong = timeouts->when - cur_time;
			if (howlong > INT_MAX / 1000)
				howlong = INT_MAX / 1000;
			to_msec = howlong * 1000;
		} else
			to_msec = -1;

		/* Set up the descriptors to be polled. */
		i = 0;

		for (l = protocols; l; l = l->next) {
			struct interface_info *ip = l->local;

			if (ip && (l->handler != got_one || !ip->dead)) {
				fds[i].fd = l->fd;
				fds[i].events = POLLIN;
				fds[i].revents = 0;
				i++;
			}
		}

		if (i == 0)
			fatalx("No live interfaces to poll on - exiting.");

		/* Wait for a packet or a timeout... XXX */
		count = poll(fds, nfds, to_msec);

		/* Not likely to be transitory... */
		if (count == -1) {
			if (errno == EAGAIN || errno == EINTR) {
				time(&cur_time);
				continue;
			}
			else
				fatal("poll");
		}

		/* Get the current time... */
		time(&cur_time);

		i = 0;
		for (l = protocols; l; l = l->next) {
			struct interface_info *ip = l->local;

			if ((fds[i].revents & (POLLIN | POLLHUP))) {
				fds[i].revents = 0;
				if (ip && (l->handler != got_one ||
				    !ip->dead))
					(*(l->handler))(l);
				if (interfaces_invalidated)
					break;
			}
			i++;
		}
		interfaces_invalidated = 0;
	} while (1);
}


void
got_one(struct protocol *l)
{
	struct packet_ctx pc;
	ssize_t result;
	union {
		/*
		 * Packet input buffer.  Must be as large as largest
		 * possible MTU.
		 */
		unsigned char packbuf[4095];
		struct dhcp_packet packet;
	} u;
	struct interface_info *ip = l->local;

	memset(&pc, 0, sizeof(pc));

	if ((result = receive_packet(ip, u.packbuf, sizeof(u), &pc)) == -1) {
		log_warn("receive_packet failed on %s", ip->name);
		ip->errors++;
		if ((!interface_status(ip)) ||
		    (ip->noifmedia && ip->errors > 20)) {
			/* our interface has gone away. */
			log_warnx("Interface %s no longer appears valid.",
			    ip->name);
			ip->dead = 1;
			interfaces_invalidated = 1;
			close(l->fd);
			remove_protocol(l);
			free(ip);
		}
		return;
	}
	if (result == 0)
		return;

	if (bootp_packet_handler)
		(*bootp_packet_handler)(ip, &u.packet, result, &pc);
}

int
interface_status(struct interface_info *ifinfo)
{
	char *ifname = ifinfo->name;
	int ifsock = ifinfo->rfdesc;
	struct ifreq ifr;
	struct ifmediareq ifmr;

	/* get interface flags */
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(ifsock, SIOCGIFFLAGS, &ifr) == -1) {
		log_warn("ioctl(SIOCGIFFLAGS) on %s", ifname);
		goto inactive;
	}
	/*
	 * if one of UP and RUNNING flags is dropped,
	 * the interface is not active.
	 */
	if ((ifr.ifr_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
		goto inactive;
	}
	/* Next, check carrier on the interface, if possible */
	if (ifinfo->noifmedia)
		goto active;
	memset(&ifmr, 0, sizeof(ifmr));
	strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));
	if (ioctl(ifsock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1) {
		if (errno != EINVAL) {
			log_debug("ioctl(SIOCGIFMEDIA) on %s", ifname);
			ifinfo->noifmedia = 1;
			goto active;
		}
		/*
		 * EINVAL (or ENOTTY) simply means that the interface
		 * does not support the SIOCGIFMEDIA ioctl. We regard it alive.
		 */
		ifinfo->noifmedia = 1;
		goto active;
	}
	if (ifmr.ifm_status & IFM_AVALID) {
		switch (ifmr.ifm_active & IFM_NMASK) {
		case IFM_ETHER:
			if (ifmr.ifm_status & IFM_ACTIVE)
				goto active;
			else
				goto inactive;
			break;
		default:
			goto inactive;
		}
	}
inactive:
	return (0);
active:
	return (1);
}

/* Add a protocol to the list of protocols... */
void
add_protocol(char *name, int fd, void (*handler)(struct protocol *),
    void *local)
{
	struct protocol *p;

	p = malloc(sizeof(*p));
	if (!p)
		fatalx("can't allocate protocol struct for %s", name);

	p->fd = fd;
	p->handler = handler;
	p->local = local;
	p->next = protocols;
	protocols = p;
}

void
remove_protocol(struct protocol *proto)
{
	struct protocol *p, *next, *prev;

	prev = NULL;
	for (p = protocols; p; p = next) {
		next = p->next;
		if (p == proto) {
			if (prev)
				prev->next = p->next;
			else
				protocols = p->next;
			free(p);
		}
	}
}
