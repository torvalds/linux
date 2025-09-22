/*	$OpenBSD: inet.c,v 1.28 2024/08/28 11:41:42 op Exp $	*/

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


#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <sys/time.h>				/* concession to AIX */

#include <net/if.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/*
 * Free a list of interfaces.
 */
void
pcap_freealldevs(pcap_if_t *alldevs)
{
	pcap_if_t *curdev, *nextdev;
	pcap_addr_t *curaddr, *nextaddr;

	for (curdev = alldevs; curdev != NULL; curdev = nextdev) {
		nextdev = curdev->next;

		/*
		 * Free all addresses.
		 */
		for (curaddr = curdev->addresses; curaddr != NULL;
		    curaddr = nextaddr) {
			nextaddr = curaddr->next;
			free(curaddr->addr);
			free(curaddr->netmask);
			free(curaddr->broadaddr);
			free(curaddr->dstaddr);
			free(curaddr);
		}

		/*
		 * Free the name string.
		 */
		free(curdev->name);

		/*
		 * Free the description string, if any.
		 */
		free(curdev->description);

		/*
		 * Free the interface.
		 */
		free(curdev);
	}
}

/*
 * Return the name of a network interface attached to the system, or NULL
 * if none can be found.  The interface must be configured up; the
 * lowest unit number is preferred; loopback is ignored.
 */
char *
pcap_lookupdev(char *errbuf)
{
#ifdef HAVE_IFADDRS_H
	struct ifaddrs *ifap, *ifa, *mp;
	int n, minunit;
	char *cp;
	const char *errstr;
	static char device[IF_NAMESIZE + 1];

	if (getifaddrs(&ifap) != 0) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "getifaddrs: %s", pcap_strerror(errno));
		return NULL;
	}

	mp = NULL;
	minunit = 666;
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) == 0)
			continue;
		if (ISLOOPBACK(ifa->ifa_name, ifa->ifa_flags))
			continue;
		for (cp = ifa->ifa_name; !isdigit((unsigned char)*cp); ++cp)
			continue;
		n = strtonum(cp, 0, INT_MAX, &errstr);
		if (errstr != NULL)
			continue;
		if (n < minunit) {
			minunit = n;
			mp = ifa;
		}
	}
	if (mp == NULL) {
		(void)strlcpy(errbuf, "no suitable device found",
		    PCAP_ERRBUF_SIZE);
		freeifaddrs(ifap);
		return (NULL);
	}

	(void)strlcpy(device, mp->ifa_name, sizeof(device));
	freeifaddrs(ifap);
	return (device);
#else
	int fd, minunit, n;
	char *cp;
	const char *errstr;
	struct ifreq *ifrp, *ifend, *ifnext, *mp;
	struct ifconf ifc;
	struct ifreq ibuf[16], ifr;
	static char device[sizeof(ifrp->ifr_name) + 1];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE, "socket: %s",
		    pcap_strerror(errno));
		return (NULL);
	}
	ifc.ifc_len = sizeof ibuf;
	ifc.ifc_buf = (caddr_t)ibuf;

	memset((char *)ibuf, 0, sizeof(ibuf));
	if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) == -1 ||
	    ifc.ifc_len < sizeof(struct ifreq)) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE, "SIOCGIFCONF: %s",
		    pcap_strerror(errno));
		(void)close(fd);
		return (NULL);
	}
	ifrp = ibuf;
	ifend = (struct ifreq *)((char *)ibuf + ifc.ifc_len);

	mp = NULL;
	minunit = 666;
	for (; ifrp < ifend; ifrp = ifnext) {
#ifdef HAVE_SOCKADDR_SA_LEN
		n = ifrp->ifr_addr.sa_len + sizeof(ifrp->ifr_name);
		if (n < sizeof(*ifrp))
			ifnext = ifrp + 1;
		else
			ifnext = (struct ifreq *)((char *)ifrp + n);
		if (ifrp->ifr_addr.sa_family != AF_INET)
			continue;
#else
		ifnext = ifrp + 1;
#endif
		/*
		 * Need a template to preserve address info that is
		 * used below to locate the next entry.  (Otherwise,
		 * SIOCGIFFLAGS stomps over it because the requests
		 * are returned in a union.)
		 */
		(void)strlcpy(ifr.ifr_name, ifrp->ifr_name,
		    sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFFLAGS, (char *)&ifr) == -1) {
			if (errno == ENXIO)
				continue;
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "SIOCGIFFLAGS: %.*s: %s",
			    (int)sizeof(ifr.ifr_name), ifr.ifr_name,
			    pcap_strerror(errno));
			(void)close(fd);
			return (NULL);
		}

		/* Must be up and not the loopback */
		if ((ifr.ifr_flags & IFF_UP) == 0 || 
		    ISLOOPBACK(ifr.ifr_name, ifr.ifr_flags))
			continue;

		for (cp = ifrp->ifr_name; !isdigit((unsigned char)*cp); ++cp)
			continue;
		n = strtonum(cp, 0, INT_MAX, &errstr);
		if (errstr != NULL)
			continue;
		if (n < minunit) {
			minunit = n;
			mp = ifrp;
		}
	}
	(void)close(fd);
	if (mp == NULL) {
		(void)strlcpy(errbuf, "no suitable device found",
		    PCAP_ERRBUF_SIZE);
		return (NULL);
	}

	(void)strlcpy(device, mp->ifr_name, sizeof(device));
	return (device);
#endif
}

int
pcap_lookupnet(const char *device, bpf_u_int32 *netp, bpf_u_int32 *maskp,
    char *errbuf)
{
	int fd;
	struct sockaddr_in *sin;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE, "socket: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	memset(&ifr, 0, sizeof(ifr));
#ifdef linux
	/* XXX Work around Linux kernel bug */
	ifr.ifr_addr.sa_family = AF_INET;
#endif
	(void)strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, (char *)&ifr) == -1) {
		if (errno == EADDRNOTAVAIL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "%s: no IPv4 address assigned", device);
		} else {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "SIOCGIFADDR: %s: %s",
			    device, pcap_strerror(errno));
		}
		(void)close(fd);
		return (-1);
	}
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	*netp = sin->sin_addr.s_addr;
	if (ioctl(fd, SIOCGIFNETMASK, (char *)&ifr) == -1) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "SIOCGIFNETMASK: %s: %s", device, pcap_strerror(errno));
		(void)close(fd);
		return (-1);
	}
	(void)close(fd);
	*maskp = sin->sin_addr.s_addr;
	if (*maskp == 0) {
		if (IN_CLASSA(*netp))
			*maskp = IN_CLASSA_NET;
		else if (IN_CLASSB(*netp))
			*maskp = IN_CLASSB_NET;
		else if (IN_CLASSC(*netp))
			*maskp = IN_CLASSC_NET;
		else {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "inet class for 0x%x unknown", *netp);
			return (-1);
		}
	}
	*netp &= *maskp;
	return (0);
}
