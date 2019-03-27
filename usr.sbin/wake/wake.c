/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2006, 2007, 2008, 2009, 2010 Marc Balmer <marc@msys.ch>
 * Copyright (C) 2000 Eugene M. Kim.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Author's name may not be used endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <err.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	_PATH_BPF	"/dev/bpf"

#ifndef SYNC_LEN
#define	SYNC_LEN	6
#endif

#ifndef DESTADDR_COUNT
#define	DESTADDR_COUNT	16
#endif

static int	bind_if_to_bpf(char const *ifname, int bpf);
static int	find_ether(char *dst, size_t len);
static int	get_ether(char const *text, struct ether_addr *addr);
static int	send_wakeup(int bpf, struct ether_addr const *addr);
static void	usage(void);
static int	wake(int bpf, const char *host);

static void
usage(void)
{

	(void)fprintf(stderr, "usage: wake [interface] lladdr [lladdr ...]\n");
	exit(1);
}

static int
wake(int bpf, const char *host)
{
	struct ether_addr macaddr;

	if (get_ether(host, &macaddr) == -1)
		return (-1);

	return (send_wakeup(bpf, &macaddr));
}

static int
bind_if_to_bpf(char const *ifname, int bpf)
{
	struct ifreq ifr;
	u_int dlt;

	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		return (-1);

	if (ioctl(bpf, BIOCSETIF, &ifr) == -1)
		return (-1);

	if (ioctl(bpf, BIOCGDLT, &dlt) == -1)
		return (-1);

	if (dlt != DLT_EN10MB)
		return (-1);

	return (0);
}

static int
find_ether(char *dst, size_t len)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl *sdl = NULL;
	int nifs;

	if (dst == NULL || len == 0)
		return (0);

	if (getifaddrs(&ifap) != 0)
		return (-1);

	/* XXX also check the link state */
	for (nifs = 0, ifa = ifap; ifa; ifa = ifa->ifa_next)
		if (ifa->ifa_addr->sa_family == AF_LINK &&
		    ifa->ifa_flags & IFF_UP && ifa->ifa_flags & IFF_RUNNING) {
			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			if (sdl->sdl_type == IFT_ETHER) {
				strlcpy(dst, ifa->ifa_name, len);
				nifs++;
			}
		}

	freeifaddrs(ifap);
	return (nifs == 1 ? 0 : -1);
}

static int
get_ether(char const *text, struct ether_addr *addr)
{
	struct ether_addr *paddr;

	paddr = ether_aton(text);
	if (paddr != NULL) {
		*addr = *paddr;
		return (0);
	}
	if (ether_hostton(text, addr)) {
		warnx("no match for host %s found", text);
		return (-1);
	}
	return (0);
}

static int
send_wakeup(int bpf, struct ether_addr const *addr)
{
	struct {
		struct ether_header hdr;
		u_char data[SYNC_LEN + ETHER_ADDR_LEN * DESTADDR_COUNT];
	} __packed pkt;
	u_char *p;
	ssize_t bw;
	ssize_t len;
	int i;

	(void)memset(pkt.hdr.ether_dhost, 0xff, sizeof(pkt.hdr.ether_dhost));
	pkt.hdr.ether_type = htons(0);
	(void)memset(pkt.data, 0xff, SYNC_LEN);
	for (p = pkt.data + SYNC_LEN, i = 0; i < DESTADDR_COUNT;
	    p += ETHER_ADDR_LEN, i++)
		bcopy(addr->octet, p, ETHER_ADDR_LEN);
	p = (u_char *)&pkt;
	len = sizeof(pkt);
	bw = 0;
	while (len) {
		if ((bw = write(bpf, p, len)) == -1) {
			warn("write()");
			return (-1);
		}
		len -= bw;
		p += bw;
	}
	return (0);
}

int
main(int argc, char *argv[])
{
	int bpf, n, rval;
	char ifname[IF_NAMESIZE];

	if (argc < 2)
		usage();

	if ((bpf = open(_PATH_BPF, O_RDWR)) == -1)
		err(1, "Cannot open bpf interface");

	n = 2;
	if (bind_if_to_bpf(argv[1], bpf) == -1) {
		if (find_ether(ifname, sizeof(ifname)))
			err(1, "Failed to determine ethernet interface");
		if (bind_if_to_bpf(ifname, bpf) == -1)
			err(1, "Cannot bind to interface `%s'", ifname);
		--n;
	} else
		strlcpy(ifname, argv[1], sizeof(ifname));

	if (n >= argc)
		usage();
	rval = 0;
	for (; n < argc; n++) {
		if (wake(bpf, argv[n]) != 0) {
			rval = 1;
			warn("Cannot send Wake on LAN frame over `%s' to `%s'",
			    ifname, argv[n]);
		}
	}
	exit(rval);
}
