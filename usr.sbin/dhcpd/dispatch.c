/*	$OpenBSD: dispatch.c,v 1.51 2025/06/28 13:02:27 jsg Exp $ */

/*
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

#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>

#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"
#include "sync.h"

extern int rdomain;

struct interface_info *interfaces;
struct protocol *protocols;
struct dhcpd_timeout *timeouts;
static int interfaces_invalidated;

static int interface_status(struct interface_info *ifinfo);

/* Use getifaddrs() to get a list of all the attached interfaces.
   For each interface that's of type INET and not the loopback interface,
   register that interface with the network I/O software, figure out what
   subnet it's on, and add it to the list of interfaces. */

void
discover_interfaces(void)
{
	struct interface_info *tmp;
	struct interface_info *last, *next;
	struct subnet *subnet;
	struct shared_network *share;
	struct sockaddr_in foo;
	int ir;
	struct ifreq *tif;
	struct ifaddrs *ifap, *ifa;

	if (getifaddrs(&ifap) != 0)
		fatalx("getifaddrs failed");

	/*
	 * If we already have a list of interfaces, the interfaces were
	 * requested.
	 */
	ir = (interfaces != NULL);

	/* Cycle through the list of interfaces looking for IP addresses. */
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/*
		 * See if this is the sort of interface we want to
		 * deal with.
		 */
		if ((ifa->ifa_flags & IFF_LOOPBACK) ||
		    (ifa->ifa_flags & IFF_POINTOPOINT) ||
		    (!(ifa->ifa_flags & IFF_BROADCAST)))
			continue;

		/* See if we've seen an interface that matches this one. */
		for (tmp = interfaces; tmp; tmp = tmp->next)
			if (!strcmp(tmp->name, ifa->ifa_name))
				break;

		/* If we are looking for specific interfaces, ignore others. */
		if (tmp == NULL && ir)
			continue;

		/* If there isn't already an interface by this name,
		   allocate one. */
		if (tmp == NULL) {
			tmp = calloc(1, sizeof *tmp);
			if (!tmp)
				fatalx("Insufficient memory to %s %s",
				    "record interface", ifa->ifa_name);
			strlcpy(tmp->name, ifa->ifa_name, sizeof(tmp->name));
			tmp->next = interfaces;
			tmp->noifmedia = tmp->dead = tmp->errors = 0;
			interfaces = tmp;
		}

		/* If we have the capability, extract link information
		   and record it in a linked list. */
		if (ifa->ifa_addr->sa_family == AF_LINK) {
			struct if_data *ifi = ifa->ifa_data;
			struct sockaddr_dl *sdl;

			if (rdomain != ifi->ifi_rdomain)
				continue;

			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			tmp->index = sdl->sdl_index;
			tmp->hw_address.hlen = sdl->sdl_alen;
			tmp->hw_address.htype = HTYPE_ETHER; /* XXX */
			memcpy(tmp->hw_address.haddr,
			    LLADDR(sdl), sdl->sdl_alen);
		} else if (ifa->ifa_addr->sa_family == AF_INET) {
			/* Get a pointer to the address... */
			memcpy(&foo, ifa->ifa_addr, sizeof(foo));

			/* We don't want the loopback interface. */
			if (foo.sin_addr.s_addr == htonl (INADDR_LOOPBACK))
				continue;

			/* If this is the first real IP address we've
			   found, keep a pointer to ifreq structure in
			   which we found it. */
			if (!tmp->ifp) {
				int len = (IFNAMSIZ + ifa->ifa_addr->sa_len);
				tif = malloc(len);
				if (!tif)
					fatalx("no space to remember ifp.");
				strlcpy(tif->ifr_name, ifa->ifa_name,
				    IFNAMSIZ);
				memcpy(&tif->ifr_addr, ifa->ifa_addr,
				    ifa->ifa_addr->sa_len);
				tmp->ifp = tif;
				tmp->primary_address = foo.sin_addr;
			}
		}
	}

	/* Discard interfaces we can't listen on. */
	last = NULL;
	for (tmp = interfaces; tmp; tmp = next) {
		struct iaddr addr;

		next = tmp->next;

		if (tmp->index == 0) {
			log_warnx("Can't listen on %s - wrong rdomain",
			    tmp->name);
			/* Remove tmp from the list of interfaces. */
			if (!last)
				interfaces = interfaces->next;
			else
				last->next = tmp->next;
			continue;
		}

		if (!tmp->ifp) {
			log_warnx("Can't listen on %s - it has no IP address.",
			    tmp->name);
			/* Remove tmp from the list of interfaces. */
			if (!last)
				interfaces = interfaces->next;
			else
				last->next = tmp->next;
			continue;
		}

		memcpy(&foo, &tmp->ifp->ifr_addr, sizeof tmp->ifp->ifr_addr);

		/* Grab the address... */
		addr.len = 4;
		memcpy(addr.iabuf, &foo.sin_addr.s_addr, addr.len);

		/* If there's a registered subnet for this address,
		   connect it together... */
		if ((subnet = find_subnet(addr))) {
			/* If this interface has multiple aliases
			   on the same subnet, ignore all but the
			   first we encounter. */
			if (!subnet->interface) {
				subnet->interface = tmp;
				subnet->interface_address = addr;
			} else if (subnet->interface != tmp) {
				log_warnx("Multiple %s %s: %s %s",
				    "interfaces match the",
				    "same subnet",
				    subnet->interface->name,
				    tmp->name);
			}
			share = subnet->shared_network;
			if (tmp->shared_network &&
			    tmp->shared_network != share) {
				log_warnx("Interface %s matches %s",
				    tmp->name,
				    "multiple shared networks");
			} else {
				tmp->shared_network = share;
			}

			if (!share->interface) {
				share->interface = tmp;
			} else if (share->interface != tmp) {
				log_warnx("Multiple %s %s: %s %s",
				    "interfaces match the",
				    "same shared network",
				    share->interface->name,
				    tmp->name);
			}
		}

		if (!tmp->shared_network) {
			log_warnx("Can't listen on %s - dhcpd.conf has no "
			    "subnet declaration for %s.", tmp->name,
			    inet_ntoa(foo.sin_addr));
			/* Remove tmp from the list of interfaces. */
			if (!last)
				interfaces = interfaces->next;
			else
				last->next = tmp->next;
			continue;
		}

		last = tmp;

		/* Find subnets that don't have valid interface addresses. */
		for (subnet = (tmp->shared_network ?
		    tmp->shared_network->subnets : NULL); subnet;
		    subnet = subnet->next_sibling) {
			if (!subnet->interface_address.len) {
				/*
				 * Set the interface address for this subnet
				 * to the first address we found.
				 */
				subnet->interface_address.len = 4;
				memcpy(subnet->interface_address.iabuf,
				    &foo.sin_addr.s_addr, 4);
			}
		}

		/* Register the interface... */
		if_register_receive(tmp);
		if_register_send(tmp);
		log_info("Listening on %s (%s).", tmp->name,
		    inet_ntoa(foo.sin_addr));
	}

	if (interfaces == NULL)
		fatalx("No interfaces to listen on.");

	/* Now register all the remaining interfaces as protocols. */
	for (tmp = interfaces; tmp; tmp = tmp->next)
		add_protocol(tmp->name, tmp->rfdesc, got_one, tmp);

	freeifaddrs(ifap);
}

/*
 * Wait for packets to come in using poll().  When a packet comes in,
 * call receive_packet to receive the packet and possibly strip hardware
 * addressing information from it, and then process it in do_packet.
 */
void
dispatch(void)
{
	int nfds, i, to_msec;
	struct protocol *l;
	static struct pollfd *fds;
	static int nfds_max;
	time_t howlong;
	int nifaces;

	for (nfds = 0, l = protocols; l; l = l->next)
		nfds++;
	if (nfds > nfds_max) {
		fds = reallocarray(fds, nfds, sizeof(struct pollfd));
		if (fds == NULL)
			fatalx("Can't allocate poll structures.");
		nfds_max = nfds;
	}

	for (;;) {
		/*
		 * Call any expired timeouts, and then if there's
		 * still a timeout registered, time out the poll
		 * call then.
		 */
		time(&cur_time);
another:
		if (timeouts) {
			if (timeouts->when <= cur_time) {
				struct dhcpd_timeout *t = timeouts;
				timeouts = timeouts->next;
				t->func(t->what);
				free(t);
				goto another;
			}

			/*
			 * Figure timeout in milliseconds, and check for
			 * potential overflow, so we can cram into an int
			 * for poll, while not polling with a negative
			 * timeout and blocking indefinitely.
			 */
			howlong = timeouts->when - cur_time;
			if (howlong > INT_MAX / 1000)
				howlong = INT_MAX / 1000;
			to_msec = howlong * 1000;
		} else
			to_msec = -1;

		/* Set up the descriptors to be polled. */
		nifaces = 0;
		for (i = 0, l = protocols; l; l = l->next) {
			if (l->handler == got_one) {
				struct interface_info *ip = l->local;
				if (ip->dead) {
					l->pfd = -1;
					continue;
				} else
					nifaces++;
			}

			fds[i].fd = l->fd;
			fds[i].events = POLLIN;
			l->pfd = i++;
		}

		if (nifaces == 0)
			fatalx("No live interfaces to poll on - exiting.");

		/* Wait for a packet or a timeout... */
		switch (poll(fds, nfds, to_msec)) {
		case -1:
			if (errno != EAGAIN && errno != EINTR)
				fatal("poll");
			/* FALLTHROUGH */
		case 0:
			continue;	/* no packets */
		}
		time(&cur_time);

		for (l = protocols; l; l = l->next) {
			i = l->pfd;
			if (i == -1)
				continue;

			if (fds[i].revents & (POLLIN | POLLHUP))
				l->handler(l);
		}
		interfaces_invalidated = 0;
	}
}


void
got_one(struct protocol *l)
{
	struct sockaddr_in from;
	struct hardware hfrom;
	struct iaddr ifrom;
	ssize_t result;
	union {
		unsigned char packbuf[4095];
		struct dhcp_packet packet;
	} u;
	struct interface_info *ip = l->local;

	memset(&u, 0, sizeof(u));

	if ((result = receive_packet(ip, u.packbuf, sizeof u,
	    &from, &hfrom)) == -1) {
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

	ifrom.len = 4;
	memcpy(ifrom.iabuf, &from.sin_addr, ifrom.len);

	do_packet(ip, &u.packet, result, from.sin_port, ifrom, &hfrom);
}

int
interface_status(struct interface_info *ifinfo)
{
	char * ifname = ifinfo->name;
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
	if ((ifr.ifr_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
		goto inactive;

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

int
locate_network(struct packet *packet)
{
	struct iaddr ia;

	/* If this came through a gateway, find the corresponding subnet... */
	if (packet->raw->giaddr.s_addr) {
		struct subnet *subnet;

		ia.len = 4;
		memcpy(ia.iabuf, &packet->raw->giaddr, 4);
		subnet = find_subnet(ia);
		if (subnet)
			packet->shared_network = subnet->shared_network;
		else
			packet->shared_network = NULL;
	} else {
		packet->shared_network = packet->interface->shared_network;
	}
	if (packet->shared_network)
		return 1;
	return 0;
}

void
add_timeout(time_t when, void (*where)(void *), void *what)
{
	struct dhcpd_timeout *t, *q;

	/* See if this timeout supersedes an existing timeout. */
	t = NULL;
	for (q = timeouts; q; q = q->next) {
		if (q->func == where && q->what == what) {
			if (t)
				t->next = q->next;
			else
				timeouts = q->next;
			break;
		}
		t = q;
	}

	/* If we didn't supersede a timeout, allocate a timeout
	   structure now. */
	if (!q) {
		q = malloc(sizeof (struct dhcpd_timeout));
		if (!q)
			fatalx("Can't allocate timeout structure!");
		q->func = where;
		q->what = what;
	}

	q->when = when;

	/* Now sort this timeout into the timeout list. */

	/* Beginning of list? */
	if (!timeouts || timeouts->when > q->when) {
		q->next = timeouts;
		timeouts = q;
		return;
	}

	/* Middle of list? */
	for (t = timeouts; t->next; t = t->next) {
		if (t->next->when > q->when) {
			q->next = t->next;
			t->next = q;
			return;
		}
	}

	/* End of list. */
	t->next = q;
	q->next = NULL;
}

void
cancel_timeout(void (*where)(void *), void *what)
{
	struct dhcpd_timeout *t, *q;

	/* Look for this timeout on the list, and unlink it if we find it. */
	t = NULL;
	for (q = timeouts; q; q = q->next) {
		if (q->func == where && q->what == what) {
			if (t)
				t->next = q->next;
			else
				timeouts = q->next;
			free(q);
			return;
		}
		t = q;
	}
}

/* Add a protocol to the list of protocols... */
void
add_protocol(char *name, int fd, void (*handler)(struct protocol *),
    void *local)
{
	struct protocol *p;

	p = malloc(sizeof *p);
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
	struct protocol *p, *next, *prev = NULL;

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
