/*-
 * Copyright (c) 2007 Bruce M. Simpson
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ifaddrs.h>

static int dorandom = 0;
static int verbose = 0;
static char *ifname = NULL;

/*
 * The test tool exercises IP-level socket options by interrogating the
 * getsockopt()/setsockopt() APIs.  It does not currently test that the
 * intended semantics of each option are implemented (i.e., that setting IP
 * options on the socket results in packets with the desired IP options in
 * it).
 */

/*
 * get_socket() is a wrapper function that returns a socket of the specified
 * type, and created with or without restored root privilege (if running
 * with a real uid of root and an effective uid of some other user).  This
 * us to test whether the same rights are granted using a socket with a
 * privileged cached credential vs. a socket with a regular credential.
 */
#define	PRIV_ASIS	0
#define	PRIV_GETROOT	1
static int
get_socket_unpriv(int type)
{

	return (socket(PF_INET, type, 0));
}

static int
get_socket_priv(int type)
{
	uid_t olduid;
	int sock;

	if (getuid() != 0)
		errx(-1, "get_sock_priv: running without real uid 0");
	
	olduid = geteuid();
	if (seteuid(0) < 0)
		err(-1, "get_sock_priv: seteuid(0)");

	sock = socket(PF_INET, type, 0);

	if (seteuid(olduid) < 0)
		err(-1, "get_sock_priv: seteuid(%d)", olduid);

	return (sock);
}

static int
get_socket(int type, int priv)
{

	if (priv)
		return (get_socket_priv(type));
	else
		return (get_socket_unpriv(type));
}

union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
};
typedef union sockunion sockunion_t;

static void
test_ether_multi(int sock)
{
	struct ifreq ifr;
	struct sockaddr_dl *dlp;
	struct ether_addr ea;
	struct ifmaddrs *ifma, *ifmap;
	int found;

	/* Choose an 802 multicast address. */
	if (dorandom) {
		uint32_t mac4;

		srandomdev();
		mac4 = random();
		ea.octet[0] = 0x01;
		ea.octet[1] = 0x80;
		ea.octet[2] = ((mac4 >> 24 & 0xFF));
		ea.octet[3] = ((mac4 >> 16 & 0xFF));
		ea.octet[4] = ((mac4 >> 8 & 0xFF));
		ea.octet[5] = (mac4 & 0xFF);
	} else {
		struct ether_addr *nep = ether_aton("01:80:DE:FA:CA:7E");
		ea = *nep;
	}

	/* Fill out ifreq, and fill out 802 group address. */
	memset(&ifr, 0, sizeof(struct ifreq));
	strlcpy(&ifr.ifr_name[0], ifname, IFNAMSIZ);
	dlp = (struct sockaddr_dl *)&ifr.ifr_addr;
	memset(dlp, 0, sizeof(struct sockaddr_dl));
	dlp->sdl_len = sizeof(struct sockaddr_dl);
	dlp->sdl_family = AF_LINK;
	dlp->sdl_alen = sizeof(struct ether_addr);
	memcpy(LLADDR(dlp), &ea, sizeof(struct ether_addr));

	/* Join an 802 group. */
	if (ioctl(sock, SIOCADDMULTI, &ifr) < 0) {
		warn("can't add ethernet multicast membership");
		return;
	}

	/* Check that we joined the group by calling getifmaddrs(). */
	found = 0;
	if (getifmaddrs(&ifmap) != 0) {
		warn("getifmaddrs()");
	} else {
		for (ifma = ifmap; ifma; ifma = ifma->ifma_next) {
			sockunion_t *psa = (sockunion_t *)ifma->ifma_addr;
			if (ifma->ifma_name == NULL || psa == NULL)
				continue;

			if (psa->sa.sa_family != AF_LINK ||
			    psa->sdl.sdl_alen != ETHER_ADDR_LEN)
				continue;

			if (bcmp(LLADDR(&psa->sdl), LLADDR(dlp),
			    ETHER_ADDR_LEN) == 0) {
				found = 1;
				break;
			}
		}
		freeifmaddrs(ifmap);
	}
	if (!found) {
		warnx("group membership for %s not returned by getifmaddrs()",
		   ether_ntoa(&ea));
	}

	/* Fill out ifreq, and fill out 802 group address. */
	memset(&ifr, 0, sizeof(struct ifreq));
	strlcpy(&ifr.ifr_name[0], ifname, IFNAMSIZ);
	dlp = (struct sockaddr_dl *)&ifr.ifr_addr;
	memset(dlp, 0, sizeof(struct sockaddr_dl));
	dlp->sdl_len = sizeof(struct sockaddr_dl);
	dlp->sdl_family = AF_LINK;
	dlp->sdl_alen = sizeof(struct ether_addr);
	memcpy(LLADDR(dlp), &ea, sizeof(struct ether_addr));

	/* Leave an 802 group. */
	if (ioctl(sock, SIOCDELMULTI, &ifr) < 0)
		warn("can't delete ethernet multicast membership");

}

static void
testsuite(int priv)
{
	int sock;

	sock = get_socket(SOCK_DGRAM, 0);
	if (sock == -1)
		err(-1, "get_socket(SOCK_DGRAM) for test_ether_multi()", priv);
	test_ether_multi(sock);
	close(sock);
}

static void
usage()
{

	fprintf(stderr, "usage: ethermulti -i ifname [-r] [-v]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "i:rv")) != -1) {
		switch (ch) {
		case 'i':
			ifname = optarg;
			break;
		case 'r':
			dorandom = 1;	/* introduce non-determinism */
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	if (ifname == NULL)
		usage();

	printf("1..1\n");
	if (geteuid() != 0) {
		errx(1, "Not running as root, can't run tests as non-root");
		/*NOTREACHED*/
	} else {
		fprintf(stderr,
		    "Running tests with ruid %d euid %d sock uid 0\n",
		    getuid(), geteuid());
		testsuite(PRIV_ASIS);
	}
	printf("ok 1 - ethermulti\n");
	exit(0);
}
