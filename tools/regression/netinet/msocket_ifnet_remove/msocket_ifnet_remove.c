/*-
 * Copyright (c) 2005 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Regression test to reproduce problems associated with the removal of a
 * network interface being used by an active multicast socket.  This proves
 * to be somewhat complicated, as we need a multicast-capable synthetic
 * network device that can be torn down on demand, in order that the test
 * program can open a multicast socket, join a group on the interface, tear
 * down the interface, and then close the multicast socket.  We use the
 * if_disc ("discard") synthetic interface for this purpose.
 *
 * Because potential solutions to this problem require separate handling for
 * different IP socket types, we actually run the test twice: once for UDP
 * sockets, and once for raw IP sockets.
 */

/*
 * XXX: The following hopefully don't conflict with the local configuration.
 */
#define	MULTICAST_IP	"224.100.100.100"
#define	DISC_IP		"192.0.2.100"
#define	DISC_MASK	"255.255.255.0"
#define	DISC_IFNAME	"disc"
#define	DISC_IFUNIT	100

static int
disc_setup(void)
{
	struct ifreq ifr;
	int s;

	if (kldload("if_disc") < 0) {
		switch (errno) {
		case EEXIST:
			break;
		default:
			warn("disc_setup: kldload(if_disc)");
			return (-1);
		}
	}

	s = socket(PF_INET, SOCK_RAW, 0);
	if (s < 0) {
		warn("disc_setup: socket(PF_INET, SOCK_RAW, 0)");
		return (-1);
	}

	bzero(&ifr, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s%d", DISC_IFNAME,
	    DISC_IFUNIT);

	if (ioctl(s, SIOCIFCREATE, &ifr) < 0) {
		warn("disc_setup: ioctl(%s, SIOCIFCREATE)", ifr.ifr_name);
		close(s);
		return (-1);
	}

	close(s);
	return (0);
}

static void
disc_done(void)
{
	struct ifreq ifr;
	int s;

	s = socket(PF_INET, SOCK_RAW, 0);
	if (s < 0) {
		warn("disc_done: socket(PF_INET, SOCK_RAW, 0)");
		return;
	}

	bzero(&ifr, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s%d", DISC_IFNAME,
	    DISC_IFUNIT);

	if (ioctl(s, SIOCIFDESTROY, &ifr) < 0)
		warn("disc_done: ioctl(%s, SIOCIFDESTROY)", ifr.ifr_name);
	close(s);
}

/*
 * Configure an IP address and netmask on a network interface.
 */
static int
ifconfig_inet(char *ifname, int ifunit, char *ip, char *netmask)
{
	struct sockaddr_in *sinp;
	struct ifaliasreq ifra;
	int s;

	s = socket(PF_INET, SOCK_RAW, 0);
	if (s < 0) {
		warn("ifconfig_inet: socket(PF_INET, SOCK_RAW, 0)");
		return (-1);
	}

	bzero(&ifra, sizeof(ifra));
	snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "%s%d", ifname,
	    ifunit);

	sinp = (struct sockaddr_in *)&ifra.ifra_addr;
	sinp->sin_family = AF_INET;
	sinp->sin_len = sizeof(ifra.ifra_addr);
	sinp->sin_addr.s_addr = inet_addr(ip);

	sinp = (struct sockaddr_in *)&ifra.ifra_mask;
	sinp->sin_family = AF_INET;
	sinp->sin_len = sizeof(ifra.ifra_addr);
	sinp->sin_addr.s_addr = inet_addr(netmask);

	if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
		warn("ifconfig_inet: ioctl(%s%d, SIOCAIFADDR, %s)", ifname,
		    ifunit, ip);
		close(s);
		return (-1);
	}

	close(s);
	return (0);
}

static int
multicast_open(int *sockp, int type, const char *type_string)
{
	struct ip_mreq imr;
	int sock;

	sock = socket(PF_INET, type, 0);
	if (sock < 0) {
		warn("multicast_test: socket(PF_INET, %s, 0)", type_string);
		return (-1);
	}

	bzero(&imr, sizeof(imr));
	imr.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
	imr.imr_interface.s_addr = inet_addr(DISC_IP);

	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr,
	    sizeof(imr)) < 0) {
		warn("multicast_test: setsockopt(IPPROTO_IP, "
		    "IP_ADD_MEMBERSHIP, {%s, %s})", MULTICAST_IP, DISC_IP);
		close(sock);
		return (-1);
	}

	*sockp = sock;
	return (0);
}

static void
multicast_close(int udp_socket)
{

	close(udp_socket);
}

static int
test_sock_type(int type, const char *type_string)
{
	int sock;

	if (disc_setup() < 0)
		return (-1);

	if (ifconfig_inet(DISC_IFNAME, DISC_IFUNIT, DISC_IP, DISC_MASK) < 0) {
		disc_done();
		return (-1);
	}

	if (multicast_open(&sock, type, type_string) < 0) {
		disc_done();
		return (-1);
	}

	/*
	 * Tear down the interface first, then close the multicast socket and
	 * see if we make it to the end of the function.
	 */
	disc_done();
	multicast_close(sock);

	printf("test_sock_type(%s) passed\n", type_string);

	return (0);
}

int
main(int argc, char *argv[])
{

	if (test_sock_type(SOCK_RAW, "SOCK_RAW") < 0)
		return (-1);

	if (test_sock_type(SOCK_DGRAM, "SOCK_DGRAM") < 0)
		return (-1);

	return (0);
}
