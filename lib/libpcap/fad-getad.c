/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <net/if.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <limits.h>

#include "pcap-int.h"

static struct sockaddr *
dup_sockaddr(struct sockaddr *sa, size_t sa_length)
{
	struct sockaddr *newsa;

	if ((newsa = malloc(sa_length)) == NULL)
		return (NULL);
	return (memcpy(newsa, sa, sa_length));
}

static int
get_instance(const char *name)
{
	const char *cp, *endcp, *errstr;
	int n;

	if (strcmp(name, "any") == 0) {
		/*
		 * Give the "any" device an artificially high instance
		 * number, so it shows up after all other non-loopback
		 * interfaces.
		 */
		return INT_MAX;
	}

	endcp = name + strlen(name);
	for (cp = name; cp < endcp && !isdigit((unsigned char)*cp); ++cp)
		continue;

	n = strtonum(cp, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		return -1;
	return n;
}

static int
add_or_find_if(pcap_if_t **curdev_ret, pcap_if_t **alldevs, const char *name,
    u_int flags, const char *description, char *errbuf)
{
	pcap_t *p;
	pcap_if_t *curdev, *prevdev, *nextdev;
	int this_instance;
	size_t len;

	/*
	 * Can we open this interface for live capture?
	 *
	 * We do this check so that interfaces that are supplied
	 * by the interface enumeration mechanism we're using
	 * but that don't support packet capture aren't included
	 * in the list.  An example of this is loopback interfaces
	 * on Solaris; we don't just omit loopback interfaces
	 * because you *can* capture on loopback interfaces on some
	 * OSes.
	 */
	p = pcap_open_live(name, 68, 0, 0, errbuf);
	if (p == NULL) {
		/*
		 * No.  Don't bother including it.
		 * Don't treat this as an error, though.
		 */
		*curdev_ret = NULL;
		return (0);
	}
	pcap_close(p);

	/*
	 * Is there already an entry in the list for this interface?
	 */
	for (curdev = *alldevs; curdev != NULL; curdev = curdev->next) {
		if (strcmp(name, curdev->name) == 0)
			break;	/* yes, we found it */
	}
	if (curdev == NULL) {
		/*
		 * No, we didn't find it.
		 * Allocate a new entry.
		 */
		curdev = calloc(1, sizeof(pcap_if_t));
		if (curdev == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "calloc: %s", pcap_strerror(errno));
			goto fail;
		}

		/*
		 * Fill in the entry.
		 */
		curdev->next = NULL;
		len = strlen(name) + 1;
		curdev->name = malloc(len);
		if (curdev->name == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			goto fail;
		}
		strlcpy(curdev->name, name, len);
		if (description != NULL) {
			/*
			 * We have a description for this interface.
			 */
			len = strlen(description) + 1;
			curdev->description = malloc(len);
			if (curdev->description == NULL) {
				(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
				    "malloc: %s", pcap_strerror(errno));
				goto fail;
			}
			strlcpy(curdev->description, description, len);
		}
		curdev->addresses = NULL;	/* list starts out as empty */
		curdev->flags = 0;
		if (ISLOOPBACK(name, flags))
			curdev->flags |= PCAP_IF_LOOPBACK;

		/*
		 * Add it to the list, in the appropriate location.
		 * First, get the instance number of this interface.
		 */
		if ((this_instance = get_instance(name)) == -1) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malformed device name: %s", name);
			goto fail;
		}

		/*
		 * Now look for the last interface with an instance number
		 * less than or equal to the new interface's instance
		 * number - except that non-loopback interfaces are
		 * arbitrarily treated as having interface numbers less
		 * than those of loopback interfaces, so the loopback
		 * interfaces are put at the end of the list.
		 *
		 * We start with "prevdev" being NULL, meaning we're before
		 * the first element in the list.
		 */
		prevdev = NULL;
		for (;;) {
			/*
			 * Get the interface after this one.
			 */
			if (prevdev == NULL) {
				/*
				 * The next element is the first element.
				 */
				nextdev = *alldevs;
			} else
				nextdev = prevdev->next;

			/*
			 * Are we at the end of the list?
			 */
			if (nextdev == NULL) {
				/*
				 * Yes - we have to put the new entry
				 * after "prevdev".
				 */
				break;
			}

			/*
			 * Is the new interface a non-loopback interface
			 * and the next interface a loopback interface?
			 */
			if (!(curdev->flags & PCAP_IF_LOOPBACK) &&
			    (nextdev->flags & PCAP_IF_LOOPBACK)) {
				/*
				 * Yes, we should put the new entry
				 * before "nextdev", i.e. after "prevdev".
				 */
				break;
			}

			/*
			 * Is the new interface's instance number less
			 * than the next interface's instance number,
			 * and is it the case that the new interface is a
			 * non-loopback interface or the next interface is
			 * a loopback interface?
			 *
			 * (The goal of both loopback tests is to make
			 * sure that we never put a loopback interface
			 * before any non-loopback interface and that we
			 * always put a non-loopback interface before all
			 * loopback interfaces.)
			 */
			if (this_instance < get_instance(nextdev->name) &&
			    (!(curdev->flags & PCAP_IF_LOOPBACK) ||
			       (nextdev->flags & PCAP_IF_LOOPBACK))) {
				/*
				 * Yes - we should put the new entry
				 * before "nextdev", i.e. after "prevdev".
				 */
				break;
			}

			prevdev = nextdev;
		}

		/*
		 * Insert before "nextdev".
		 */
		curdev->next = nextdev;

		/*
		 * Insert after "prevdev" - unless "prevdev" is null,
		 * in which case this is the first interface.
		 */
		if (prevdev == NULL) {
			/*
			 * This is the first interface.  Pass back a
			 * pointer to it, and put "curdev" before
			 * "nextdev".
			 */
			*alldevs = curdev;
		} else
			prevdev->next = curdev;
	}

	*curdev_ret = curdev;
	return (0);

fail:
	if (curdev != NULL) {
		free(curdev->name);
		free(curdev->description);
		free(curdev);
	}
	return (-1);
}

static int
add_addr_to_iflist(pcap_if_t **alldevs, const char *name, u_int flags,
    struct sockaddr *addr, size_t addr_size,
    struct sockaddr *netmask, size_t netmask_size,
    struct sockaddr *broadaddr, size_t broadaddr_size,
    struct sockaddr *dstaddr, size_t dstaddr_size,
    char *errbuf)
{
	pcap_if_t *curdev;
	pcap_addr_t *curaddr, *prevaddr, *nextaddr;

	if (add_or_find_if(&curdev, alldevs, name, flags, NULL, errbuf) == -1) {
		/*
		 * Error - give up.
		 */
		return (-1);
	}
	if (curdev == NULL) {
		/*
		 * Device wasn't added because it can't be opened.
		 * Not a fatal error.
		 */
		return (0);
	}

	/*
	 * "curdev" is an entry for this interface; add an entry for this
	 * address to its list of addresses.
	 *
	 * Allocate the new entry and fill it in.
	 */
	curaddr = calloc(1, sizeof(pcap_addr_t));
	if (curaddr == NULL) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "calloc: %s", pcap_strerror(errno));
		goto fail;
	}

	curaddr->next = NULL;
	if (addr != NULL) {
		curaddr->addr = dup_sockaddr(addr, addr_size);
		if (curaddr->addr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			goto fail;
		}
	}

	if (netmask != NULL) {
		curaddr->netmask = dup_sockaddr(netmask, netmask_size);
		if (curaddr->netmask == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			goto fail;
		}
	}

	if (broadaddr != NULL) {
		curaddr->broadaddr = dup_sockaddr(broadaddr, broadaddr_size);
		if (curaddr->broadaddr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			goto fail;
		}
	}

	if (dstaddr != NULL) {
		curaddr->dstaddr = dup_sockaddr(dstaddr, dstaddr_size);
		if (curaddr->dstaddr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			goto fail;
		}
	}

	/*
	 * Find the end of the list of addresses.
	 */
	for (prevaddr = curdev->addresses; prevaddr != NULL;
	    prevaddr = nextaddr) {
		nextaddr = prevaddr->next;
		if (nextaddr == NULL) {
			/*
			 * This is the end of the list.
			 */
			break;
		}
	}

	if (prevaddr == NULL) {
		/*
		 * The list was empty; this is the first member.
		 */
		curdev->addresses = curaddr;
	} else {
		/*
		 * "prevaddr" is the last member of the list; append
		 * this member to it.
		 */
		prevaddr->next = curaddr;
	}

	return (0);

fail:
	if (curaddr != NULL) {
		free(curaddr->addr);
		free(curaddr->netmask);
		free(curaddr->broadaddr);
		free(curaddr->dstaddr);
		free(curaddr);
	}
	return (-1);
}

/*
 * Get a list of all interfaces that are up and that we can open.
 * Returns -1 on error, 0 otherwise.
 * The list, as returned through "alldevsp", may be null if no interfaces
 * were up and could be opened.
 *
 * This is the implementation used on platforms that have "getifaddrs()".
 */
int
pcap_findalldevs(pcap_if_t **alldevsp, char *errbuf)
{
	pcap_if_t *devlist = NULL;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr *addr, *netmask, *broadaddr, *dstaddr;
	size_t addr_size, broadaddr_size, dstaddr_size;
	int ret = 0;

	/*
	 * Get the list of interface addresses.
	 *
	 * Note: this won't return information about interfaces
	 * with no addresses; are there any such interfaces
	 * that would be capable of receiving packets?
	 * (Interfaces incapable of receiving packets aren't
	 * very interesting from libpcap's point of view.)
	 *
	 * LAN interfaces will probably have link-layer
	 * addresses; I don't know whether all implementations
	 * of "getifaddrs()" now, or in the future, will return
	 * those.
	 */
	if (getifaddrs(&ifap) != 0) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "getifaddrs: %s", pcap_strerror(errno));
		return (-1);
	}
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/*
		 * Is this interface up?
		 */
		if (!(ifa->ifa_flags & IFF_UP)) {
			/*
			 * No, so don't add it to the list.
			 */
			continue;
		}

		/*
		 * "ifa_addr" was apparently null on at least one
		 * interface on some system.
		 *
		 * "ifa_broadaddr" may be non-null even on
		 * non-broadcast interfaces, and was null on
		 * at least one OpenBSD 3.4 system on at least
		 * one interface with IFF_BROADCAST set.
		 *
		 * "ifa_dstaddr" was, on at least one FreeBSD 4.1
		 * system, non-null on a non-point-to-point
		 * interface.
		 *
		 * Therefore, we supply the address and netmask only
		 * if "ifa_addr" is non-null (if there's no address,
		 * there's obviously no netmask), and supply the
		 * broadcast and destination addresses if the appropriate
		 * flag is set *and* the appropriate "ifa_" entry doesn't
		 * evaluate to a null pointer.
		 */
		if (ifa->ifa_addr != NULL) {
			addr = ifa->ifa_addr;
			addr_size = SA_LEN(addr);
			netmask = ifa->ifa_netmask;
		} else {
			addr = NULL;
			addr_size = 0;
			netmask = NULL;
		}
		if (ifa->ifa_flags & IFF_BROADCAST &&
		    ifa->ifa_broadaddr != NULL) {
			broadaddr = ifa->ifa_broadaddr;
			broadaddr_size = SA_LEN(broadaddr);
		} else {
			broadaddr = NULL;
			broadaddr_size = 0;
		}
		if (ifa->ifa_flags & IFF_POINTOPOINT &&
		    ifa->ifa_dstaddr != NULL) {
			dstaddr = ifa->ifa_dstaddr;
			dstaddr_size = SA_LEN(ifa->ifa_dstaddr);
		} else {
			dstaddr = NULL;
			dstaddr_size = 0;
		}

		/*
		 * Add information for this address to the list.
		 */
		if (add_addr_to_iflist(&devlist, ifa->ifa_name,
		    ifa->ifa_flags, addr, addr_size, netmask, addr_size,
		    broadaddr, broadaddr_size, dstaddr, dstaddr_size,
		    errbuf) < 0) {
			ret = -1;
			break;
		}
	}

	freeifaddrs(ifap);

	if (ret == -1) {
		/*
		 * We had an error; free the list we've been constructing.
		 */
		if (devlist != NULL) {
			pcap_freealldevs(devlist);
			devlist = NULL;
		}
	}

	*alldevsp = devlist;
	return (ret);
}
