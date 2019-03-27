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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Regression test for multicast sockets and options:
 *
 * - Check the defaults for ttl, if, and loopback.  Make sure they can be set
 *   and then read.
 *
 * - Check that adding and removing multicast addresses seems to work.
 *
 * - Send a test message over loop back multicast and make sure it arrives.
 *
 * NB:
 *
 * Would be nice to use BPF or if_tap to actually check packet contents and
 * layout, make sure that the ttl is set right, etc.
 *
 * Would be nice if attempts to use multicast options on TCP sockets returned
 * an error, as the docs suggest it might.
 */

#ifdef WARN_TCP
#define	WARN_SUCCESS	0x00000001	/* Set for TCP to warn on success. */
#else
#define	WARN_SUCCESS	0x00000000
#endif

/*
 * Multicast test address, picked arbitrarily.  Will be used with the
 * loopback interface.
 */
#define	TEST_MADDR	"224.100.100.100"

/*
 * Test that a given IP socket option (optname) has a default value of
 * 'defaultv', that we can set it to 'modifiedv', and use 'fakev' as a dummy
 * value that shouldn't be returned at any point during the tests.  Perform
 * the tests on the raw socket, tcp socket, and upd socket passed.
 * 'optstring' is used in printing warnings and errors as needed.
 */
static void
test_u_char(int optname, const char *optstring, u_char defaultv,
    u_char modifiedv, u_char fakev, const char *socktype, int sock,
    int flags)
{
	socklen_t socklen;
	u_char uc;
	int ret;

	/*
	 * Check that we read back the expected default.
	 */
	uc = fakev;
	socklen = sizeof(uc);

	ret = getsockopt(sock, IPPROTO_IP, optname, &uc, &socklen);
	if (ret < 0)
		err(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: getsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);
	if (uc != defaultv)
		errx(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s) default is "
		    "%d not %d", socktype, optstring, uc, defaultv);

	/*
	 * Set to a modifiedv value, read it back and make sure it got there.
	 */
	uc = modifiedv;
	ret = setsockopt(sock, IPPROTO_IP, optname, &uc, sizeof(uc));
	if (ret == -1)
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: setsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);

	uc = fakev;
	socklen = sizeof(uc);
	ret = getsockopt(sock, IPPROTO_IP, optname, &uc, &socklen);
	if (ret < 0)
		err(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: getsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);
	if (uc != modifiedv)
		errx(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s) set value is "
		    "%d not %d", socktype, optstring, uc, modifiedv);
}

/*
 * test_in_addr() is like test_u_char(), only it runs on a struct in_addr
 * (surprise).
 */
static void
test_in_addr(int optname, const char *optstring, struct in_addr defaultv,
    struct in_addr modifiedv, struct in_addr fakev, const char *socktype,
    int sock, int flags)
{
	socklen_t socklen;
	struct in_addr ia;
	int ret;

	/*
	 * Check that we read back the expected default.
	 */
	ia = fakev;
	socklen = sizeof(ia);

	ret = getsockopt(sock, IPPROTO_IP, optname, &ia, &socklen);
	if (ret < 0)
		err(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: getsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);
	if (memcmp(&ia, &defaultv, sizeof(struct in_addr)))
		errx(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s) default is "
		    "%s not %s", socktype, optstring, inet_ntoa(ia),
		    inet_ntoa(defaultv));

	/*
	 * Set to a modifiedv value, read it back and make sure it got there.
	 */
	ia = modifiedv;
	ret = setsockopt(sock, IPPROTO_IP, optname, &ia, sizeof(ia));
	if (ret == -1)
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: setsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);

	ia = fakev;
	socklen = sizeof(ia);
	ret = getsockopt(sock, IPPROTO_IP, optname, &ia, &socklen);
	if (ret < 0)
		err(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s)",
		    socktype, optstring);
	if (ret == 0 && (flags & WARN_SUCCESS))
		warnx("WARN: getsockopt(%s, IPPROTO_IP, %s) returned 0",
		    socktype, optstring);
	if (memcmp(&ia, &modifiedv, sizeof(struct in_addr)))
		errx(-1, "FAIL: getsockopt(%s, IPPROTO_IP, %s) set value is "
		    "%s not %s", socktype, optstring, inet_ntoa(ia),
		    inet_ntoa(modifiedv));
}

static void
test_ttl(int raw_sock, int tcp_sock, int udp_sock)
{

	test_u_char(IP_MULTICAST_TTL, "IP_MULTICAST_TTL", 1, 2, 243,
	    "raw_sock", raw_sock, 0);
	test_u_char(IP_MULTICAST_TTL, "IP_MULTICAST_TTL", 1, 2, 243,
	    "tcp_sock", tcp_sock, WARN_SUCCESS);
	test_u_char(IP_MULTICAST_TTL, "IP_MULTICAST_TTL", 1, 2, 243,
	    "udp_sock", udp_sock, 0);
}

static void
test_loop(int raw_sock, int tcp_sock, int udp_sock)
{

	test_u_char(IP_MULTICAST_LOOP, "IP_MULTICAST_LOOP", 1, 0, 243,
	    "raw_sock", raw_sock, 0);
	test_u_char(IP_MULTICAST_LOOP, "IP_MULTICAST_LOOP", 1, 0, 243,
	    "tcp_sock", tcp_sock, WARN_SUCCESS);
	test_u_char(IP_MULTICAST_LOOP, "IP_MULTICAST_LOOP", 1, 0, 243,
	    "udp_sock", udp_sock, 0);
}

static void
test_if(int raw_sock, int tcp_sock, int udp_sock)
{
	struct in_addr defaultv, modifiedv, fakev;

	defaultv.s_addr = inet_addr("0.0.0.0");

	/* Should be valid on all hosts. */
	modifiedv.s_addr = inet_addr("127.0.0.1");

	/* Should not happen. */
	fakev.s_addr = inet_addr("255.255.255.255");

	test_in_addr(IP_MULTICAST_IF, "IP_MULTICAST_IF", defaultv, modifiedv,
	    fakev, "raw_sock", raw_sock, 0);
	test_in_addr(IP_MULTICAST_IF, "IP_MULTICAST_IF", defaultv, modifiedv,
	    fakev, "tcp_sock", tcp_sock, WARN_SUCCESS);
	test_in_addr(IP_MULTICAST_IF, "IP_MULTICAST_IF", defaultv, modifiedv,
	    fakev, "udp_sock", udp_sock, 0);
}

/*
 * Add a multicast address to an interface.  Warn if appropriate.  No query
 * interface so can't check if it's there directly; instead we have to try
 * to add it a second time and make sure we get back EADDRINUSE.
 */
static void
test_add_multi(int sock, const char *socktype, struct ip_mreq imr,
    int flags)
{
	char buf[128];
	int ret;

	ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr,
	    sizeof(imr));
	if (ret < 0) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_ADD_MEMBERSHIP "
		    "%s, %s)", socktype, buf, inet_ntoa(imr.imr_interface));
	}
	if (ret == 0 && (flags & WARN_SUCCESS)) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		warnx("WARN: setsockopt(%s, IPPROTO_IP, IP_ADD_MEMBERSHIP "
		    "%s, %s) returned 0", socktype, buf,
		    inet_ntoa(imr.imr_interface));
	}

	/* Try to add a second time to make sure it got there. */
	ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr,
	    sizeof(imr));
	if (ret == 0) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_ADD_MEMBERSHIP "
		    "%s, %s) dup returned 0", socktype, buf,
		    inet_ntoa(imr.imr_interface));
	}
	if (ret < 0 && errno != EADDRINUSE) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_ADD_MEMBERSHIP "
		    "%s, %s)", socktype, buf, inet_ntoa(imr.imr_interface));
	}
}

/*
 * Drop a multicast address from an interface.  Warn if appropriate.  No
 * query interface so can't check if it's gone directly; instead we have to
 * try to drop it a second time and make sure we get back EADDRNOTAVAIL.
 */
static void
test_drop_multi(int sock, const char *socktype, struct ip_mreq imr,
    int flags)
{
	char buf[128];
	int ret;

	ret = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &imr,
	    sizeof(imr));
	if (ret < 0) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_DROP_MEMBERSHIP "
		    "%s, %s)", socktype, buf, inet_ntoa(imr.imr_interface));
	}
	if (ret == 0 && (flags & WARN_SUCCESS)) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		warnx("WARN: setsockopt(%s, IPPROTO_IP, IP_DROP_MEMBERSHIP "
		    "%s, %s) returned 0", socktype, buf,
		    inet_ntoa(imr.imr_interface));
	}

	/* Try a second time to make sure it's gone. */
	ret = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &imr,
	    sizeof(imr));	
	if (ret == 0) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_DROP_MEMBERSHIP "
		    "%s, %s) returned 0", socktype, buf,
		    inet_ntoa(imr.imr_interface));
	}
	if (ret < 0 && errno != EADDRNOTAVAIL) {
		strlcpy(buf, inet_ntoa(imr.imr_multiaddr), 128);
		err(-1, "FAIL: setsockopt(%s, IPPROTO_IP, IP_DROP_MEMBERSHIP "
		    "%s, %s)", socktype, buf, inet_ntoa(imr.imr_interface));
	}
}

/*
 * Should really also test trying to add an invalid address, delete one
 * that's not there, etc.
 */
static void
test_addr(int raw_sock, int tcp_sock, int udp_sock)
{
	struct ip_mreq imr;

	/* Arbitrary. */
	imr.imr_multiaddr.s_addr = inet_addr(TEST_MADDR);

	/* Localhost should be OK. */
	imr.imr_interface.s_addr = inet_addr("127.0.0.1");

	test_add_multi(raw_sock, "raw_sock", imr, 0);
	test_drop_multi(raw_sock, "raw_sock", imr, 0);

	test_add_multi(tcp_sock, "raw_sock", imr, WARN_SUCCESS);
	test_drop_multi(tcp_sock, "raw_sock", imr, WARN_SUCCESS);

	test_add_multi(udp_sock, "raw_sock", imr, 0);
	test_drop_multi(udp_sock, "raw_sock", imr, 0);
}

/*
 * Test an actual simple UDP message - send a single byte to an address we're
 * subscribed to, and hope to get it back.  We create a new UDP socket for
 * this purpose because we will need to bind it.
 */
#define	UDP_PORT	5012
static void
test_udp(void)
{
	struct sockaddr_in sin;
	struct ip_mreq imr;
	struct in_addr if_addr;
	char message;
	ssize_t len;
	int sock;

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		err(-1, "FAIL: test_udp: socket(PF_INET, SOCK_DGRAM)");

	if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
		err(-1, "FAIL: test_udp: fcntl(F_SETFL, O_NONBLOCK)");

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(UDP_PORT);
	sin.sin_addr.s_addr = inet_addr(TEST_MADDR);

	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(-1, "FAIL: test_udp: bind(udp_sock, 127.0.0.1:%d",
		    UDP_PORT);

	/* Arbitrary. */
	imr.imr_multiaddr.s_addr = inet_addr(TEST_MADDR);

	/* Localhost should be OK. */
	imr.imr_interface.s_addr = inet_addr("127.0.0.1");

	/*
	 * Tell socket what interface to send on -- use localhost.
	 */
	if_addr.s_addr = inet_addr("127.0.0.1");
	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &if_addr,
	    sizeof(if_addr)) < 0)
		err(-1, "test_udp: setsockopt(IPPROTO_IP, IP_MULTICAST_IF)");

	test_add_multi(sock, "udp_sock", imr, 0);

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(UDP_PORT);
	sin.sin_addr.s_addr = inet_addr(TEST_MADDR);

	message = 'A';
	len = sizeof(message);
	len = sendto(sock, &message, len, 0, (struct sockaddr *)&sin,
	    sizeof(sin));
	if (len < 0)
		err(-1, "test_udp: sendto");

	if (len != sizeof(message))
		errx(-1, "test_udp: sendto: expected to send %d, instead %d",
		    sizeof(message), len);

	message = 'B';
	len = sizeof(sin);
	len = recvfrom(sock, &message, sizeof(message), 0,
	    (struct sockaddr *)&sin, &len);
	if (len < 0)
		err(-1, "test_udp: recvfrom");

	if (len != sizeof(message))
		errx(-1, "test_udp: recvfrom: len %d != message len %d",
		    len, sizeof(message));

	if (message != 'A')
		errx(-1, "test_udp: recvfrom: expected 'A', got '%c'",
		    message);

	test_drop_multi(sock, "udp_sock", imr, 0);

	close(sock);
}
#undef UDP_PORT

int
main(int argc, char *argv[])
{
	int raw_sock, tcp_sock, udp_sock;

	if (geteuid() != 0)
		errx(-1, "FAIL: root privilege required");

	raw_sock = socket(PF_INET, SOCK_RAW, 0);
	if (raw_sock == -1)
		err(-1, "FAIL: socket(PF_INET, SOCK_RAW)");

	tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (raw_sock == -1)
		err(-1, "FAIL: socket(PF_INET, SOCK_STREAM)");

	udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (raw_sock == -1)
		err(-1, "FAIL: socket(PF_INET, SOCK_DGRAM)");

	test_ttl(raw_sock, tcp_sock, udp_sock);
	test_loop(raw_sock, tcp_sock, udp_sock);
	test_if(raw_sock, tcp_sock, udp_sock);
	test_addr(raw_sock, tcp_sock, udp_sock);

	close(udp_sock);
	close(tcp_sock);
	close(raw_sock);

	test_udp();

	return (0);
}
