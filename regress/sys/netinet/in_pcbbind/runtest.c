/* $OpenBSD: runtest.c,v 1.7 2022/04/10 14:08:35 claudio Exp $ */
/*
 * Copyright (c) 2015 Vincent Gross <vincent.gross@kilob.yt>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <netdb.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>

int
runtest(int *sockp, struct addrinfo *ai, int reuseaddr, int reuseport,
    void *mreq, int expected)
{
	int error, optval;
	struct ip_mreq imr;

	*sockp = socket(ai->ai_family, ai->ai_socktype, 0);
	if (*sockp == -1) {
		warn("%s : socket()", ai->ai_canonname);
		return (3);
	}

	if (reuseaddr) {
		optval = 1;
		error = setsockopt(*sockp, SOL_SOCKET, SO_REUSEADDR,
		    &optval, sizeof(int));
		if (error) {
			warn("%s : setsockopt(SO_REUSEADDR)", ai->ai_canonname);
			return (2);
		}
	}

	if (reuseport) {
		optval = 1;
		error = setsockopt(*sockp, SOL_SOCKET, SO_REUSEPORT,
		    &optval, sizeof(int));
		if (error) {
			warn("%s : setsockopt(SO_REUSEPORT)", ai->ai_canonname);
			return (2);
		}
	}

	if (mreq) {
		switch (ai->ai_family) {
		case AF_INET6:
			error = setsockopt(*sockp, IPPROTO_IPV6, IPV6_JOIN_GROUP,
			    mreq, sizeof(struct ipv6_mreq));
			if (error) {
				warn("%s : setsockopt(IPV6_JOIN_GROUP)",
				    ai->ai_canonname);
				return (2);
			}
			break;
		case AF_INET:
			error = setsockopt(*sockp, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			    mreq, sizeof(struct ip_mreq));
			if (error) {
				warn("%s : setsockopt(IP_ADD_MEMBERSHIP)",
				    ai->ai_canonname);
				return (2);
			}
			break;
		default:
			warnx("%s : trying to join multicast group in unknown AF",
			    ai->ai_canonname);
			return (2);
		}
	}


	error = bind(*sockp, ai->ai_addr, ai->ai_addrlen);
	if (error && (expected == 0 || expected != errno)) {
		warn("bind(%s,%s,%s)", ai->ai_canonname,
		    reuseaddr ? "REUSEADDR" : "", reuseport ? "REUSEPORT" : "");
		return (1);
	}
	if (error == 0 && expected != 0) {
		warnx("bind(%s,%s,%s) succeeded, expected : %s", ai->ai_canonname,
		    reuseaddr ? "REUSEADDR" : "", reuseport ? "REUSEPORT" : "",
		    strerror(expected));
		return (1);
	}

	return (0);
}

void
cleanup(int *fds, int num_fds)
{
	while (num_fds-- > 0)
		if (close(*fds++) && errno != EBADF)
			err(2, "unable to clean up sockets, aborting");
}

int
unicast_testsuite(struct addrinfo *local, struct addrinfo *any)
{
	int test_rc, rc, *s;
	int sockets[4];

	test_rc = 0;
	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 0, NULL, 0);
	rc |= runtest(s++, any,   0, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, any,   1, 0, NULL, 0);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 1);

	rc = 0; s = sockets;
	rc |= runtest(s++, any,   0, 0, NULL, 0);
	rc |= runtest(s++, local, 0, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, local, 1, 0, NULL, 0);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 2);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 1, NULL, 0);
	rc |= runtest(s++, local, 0, 1, NULL, 0);
	rc |= runtest(s++, local, 1, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, local, 0, 0, NULL, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 3);

	rc = 0; s = sockets;
	rc |= runtest(s++, any, 0, 1, NULL, 0);
	rc |= runtest(s++, any, 0, 1, NULL, 0);
	rc |= runtest(s++, any, 1, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, any, 0, 0, NULL, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 4);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 0, NULL, 0);
	rc |= runtest(s++, local, 1, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, local, 0, 1, NULL, EADDRINUSE);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 5);

	rc = 0; s = sockets;
	rc |= runtest(s++, any, 1, 0, NULL, 0);
	rc |= runtest(s++, any, 1, 0, NULL, EADDRINUSE);
	rc |= runtest(s++, any, 0, 1, NULL, EADDRINUSE);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 6);

	return (test_rc);
}

int
mcast_reuse_testsuite(struct addrinfo *local, void *mr)
{
	int test_rc, rc, *s;
	int sockets[6];
	int testnum = 1;

	test_rc = 0;
	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 0, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, EADDRINUSE);
	rc |= runtest(s++, local, 0, 1, mr, EADDRINUSE);
	rc |= runtest(s++, local, 1, 1, mr, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 1);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 1, mr, 0);
	rc |= runtest(s++, local, 0, 0, mr, EADDRINUSE);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 1, 1, mr, 0);
	cleanup(sockets, 5);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 2);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 0, 0, mr, EADDRINUSE);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	rc |= runtest(s++, local, 1, 1, mr, 0);
	cleanup(sockets, 5);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 3);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 0, 0, mr, EADDRINUSE);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 1, 1, mr, 0);
	cleanup(sockets, 5);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 4);

#if 0
	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 5);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	cleanup(sockets, 5);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 6);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 1, 1, mr, 0);
	rc |= runtest(s++, local, 1, 0, mr, 0);
	rc |= runtest(s++, local, 0, 1, mr, 0);
	cleanup(sockets, 5);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 7);
#endif
	return (test_rc);
}

int
mcast6_testsuite(struct addrinfo *local, struct ipv6_mreq *local_mreq,
    struct addrinfo *any, struct ipv6_mreq *any_mreq)
{
	int test_rc, rc, *s;
	int sockets[4];
	int testnum = 1;

	test_rc = 0;
	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 0, local_mreq, 0);
	rc |= runtest(s++, any,   0, 0, any_mreq,   EADDRINUSE);
	rc |= runtest(s++, any,   1, 0, any_mreq,   0);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 1);

	rc = 0; s = sockets;
	rc |= runtest(s++, any,   0, 0, any_mreq,   0);
	rc |= runtest(s++, local, 0, 0, local_mreq, EADDRINUSE);
	rc |= runtest(s++, local, 1, 0, local_mreq, 0);
	cleanup(sockets, 3);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 2);
	
	rc = 0; s = sockets;
	rc |= runtest(s++, local, 0, 1, local_mreq, 0);
	rc |= runtest(s++, local, 0, 1, local_mreq, 0);
	rc |= runtest(s++, local, 1, 0, local_mreq, 0);
	rc |= runtest(s++, local, 0, 0, local_mreq, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 3);

	/*
	 * :: is not a multicast address, SO_REUSEADDR and SO_REUSEPORT
	 * keep their unicast semantics although we are binding on multicast
	 */

	rc = 0; s = sockets;
	rc |= runtest(s++, any, 0, 1, any_mreq, 0);
	rc |= runtest(s++, any, 0, 1, any_mreq, 0);
	rc |= runtest(s++, any, 1, 0, any_mreq, EADDRINUSE);
	rc |= runtest(s++, any, 0, 0, any_mreq, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 4);

	rc = 0; s = sockets;
	rc |= runtest(s++, local, 1, 0, local_mreq, 0);
	rc |= runtest(s++, local, 1, 0, local_mreq, 0);
	rc |= runtest(s++, local, 0, 1, local_mreq, 0);
	rc |= runtest(s++, local, 0, 0, local_mreq, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 5);

	/* See above */

	rc = 0; s = sockets;
	rc |= runtest(s++, any, 1, 0, any_mreq, 0);
	rc |= runtest(s++, any, 1, 0, any_mreq, EADDRINUSE);
	rc |= runtest(s++, any, 0, 1, any_mreq, EADDRINUSE);
	rc |= runtest(s++, any, 0, 0, any_mreq, EADDRINUSE);
	cleanup(sockets, 4);
	test_rc |= rc;
	if (rc)
		warnx("%s : test #%d failed", __func__, 6);

	return (test_rc);
}

int
main(int argc, char *argv[])
{
	int error, rc;
	char *baddr_s, *bport_s, *bmifa_s;
	struct addrinfo hints, *baddr, *any, *mifa;
	struct ifaddrs *ifap, *curifa;
	struct ip_mreq local_imr, any_imr;
	struct ipv6_mreq local_i6mr, any_i6mr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int *s;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV | \
	    AI_PASSIVE;
	hints.ai_socktype = SOCK_DGRAM;

	baddr_s = argv[1];
	bport_s = argv[2];

	if ((error = getaddrinfo(baddr_s, bport_s, &hints, &baddr)))
		errx(2, "getaddrinfo(%s,%s): %s", baddr_s, bport_s,
		    gai_strerror(error));
	baddr->ai_canonname = baddr_s;

	hints.ai_family = baddr->ai_family;
	if ((error = getaddrinfo(NULL, bport_s, &hints, &any)))
		errx(2, "getaddrinfo(NULL,%s): %s", bport_s,
		    gai_strerror(error));
	any->ai_canonname = "*";

	switch (baddr->ai_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)baddr->ai_addr;
		if (!IN_MULTICAST( ntohl(sin->sin_addr.s_addr) )) {
			puts("executing unicast testsuite");
			return unicast_testsuite(baddr, any);
		}
		bmifa_s = argv[3];
		hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST;

		if ((error = getaddrinfo(bmifa_s, NULL, &hints, &mifa)))
			errx(2, "getaddrinfo(%s,NULL): %s", bmifa_s,
			    gai_strerror(error));

		local_imr.imr_interface =
		    ((struct sockaddr_in *)mifa->ai_addr)->sin_addr;
		local_imr.imr_multiaddr =
		    ((struct sockaddr_in *)baddr->ai_addr)->sin_addr;

		puts("executing ipv4 multicast testsuite");

		/* no 'any' mcast group in ipv4 */
		return mcast_reuse_testsuite(baddr, &local_imr);
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)baddr->ai_addr;
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
			puts("executing unicast testsuite");
			return unicast_testsuite(baddr, any);
		}
		bmifa_s = argv[3];
		hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST;

		if ((error = getaddrinfo(bmifa_s, NULL, &hints, &mifa)))
			errx(2, "getaddrinfo(%s,NULL): %s", bmifa_s,
			    gai_strerror(error));

		if (getifaddrs(&ifap))
			err(2, "getifaddrs()");
		curifa = ifap;
		while (curifa) {
			if (curifa->ifa_addr != NULL &&
			    memcmp(curifa->ifa_addr,
			    mifa->ai_addr,
			    mifa->ai_addrlen) == 0)
				break;
			curifa = curifa->ifa_next;
		}
		if (curifa == NULL)
			errx(2, "no interface configured with %s", argv[4]);
		local_i6mr.ipv6mr_interface =
		    if_nametoindex(curifa->ifa_name);
		if (local_i6mr.ipv6mr_interface == 0)
			errx(2, "unable to get \"%s\" index",
			    curifa->ifa_name);
		freeifaddrs(ifap);

		local_i6mr.ipv6mr_multiaddr =
		    ((struct sockaddr_in6 *)baddr->ai_addr)->sin6_addr;

		any_i6mr.ipv6mr_interface = local_i6mr.ipv6mr_interface;
		any_i6mr.ipv6mr_multiaddr =
		    ((struct sockaddr_in6 *)any->ai_addr)->sin6_addr;

		puts("executing ipv6 multicast testsuite");

		rc = 0;
		rc |= mcast_reuse_testsuite(baddr, &local_i6mr);
		if (geteuid() == 0)
			rc |= mcast6_testsuite(baddr, &local_i6mr, any, &any_i6mr);
		else
			warnx("skipping mcast6_testsuite() due to insufficient privs, please run again as root");
		return (rc);
	default:
		errx(2,"unknown AF");
	}

	return (2);
}
