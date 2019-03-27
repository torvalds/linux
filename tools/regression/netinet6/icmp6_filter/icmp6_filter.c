/*-
 * Copyright (c) 2008 Robert N. M. Watson
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

/*
 * This regression test creates a raw IPv6 socket and confirms that it can
 * set and get filters on the socket.  No attempt is made to validate that
 * the filter is implemented, just that it can be properly retrieved, set,
 * etc.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/icmp6.h>

#include <err.h>
#include <string.h>
#include <unistd.h>

/*
 * Reference filters to set/test.
 */
static struct icmp6_filter ic6f_passall;
static struct icmp6_filter ic6f_blockall;

int
main(int argc, char *argv[])
{
	struct icmp6_filter ic6f;
	socklen_t len;
	int s;

	ICMP6_FILTER_SETPASSALL(&ic6f_passall);
	ICMP6_FILTER_SETBLOCKALL(&ic6f_blockall);

	s = socket(PF_INET6, SOCK_RAW, 0);
	if (s < 0)
		err(-1, "socket(PF_INET6, SOCK_RAW, 0)");

	/*
	 * Confirm that we can read before the first set, and that the
	 * default is to pass all ICMP.
	 */
	len = sizeof(ic6f);
	if (getsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, &len) < 0)
		err(-1, "1: getsockopt(ICMP6_FILTER)");
	if (memcmp(&ic6f, &ic6f_passall, sizeof(ic6f)) != 0)
		errx(-1, "1: getsockopt(ICMP6_FILTER) - default not passall");

	/*
	 * Confirm that we can write a pass all filter to the socket.
	 */
	len = sizeof(ic6f);
	ICMP6_FILTER_SETPASSALL(&ic6f);
	if (setsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, len) < 0)
		err(-1, "2: setsockopt(ICMP6_FILTER, PASSALL)");

	/*
	 * Confirm that we can still read a pass all filter.
	 */
	len = sizeof(ic6f);
	if (getsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, &len) < 0)
		err(-1, "3: getsockopt(ICMP6_FILTER)");
	if (memcmp(&ic6f, &ic6f_passall, sizeof(ic6f)) != 0)
		errx(-1, "3: getsockopt(ICMP6_FILTER) - not passall");

	/*
	 * Confirm that we can write a block all filter to the socket.
	 */
	len = sizeof(ic6f);
	ICMP6_FILTER_SETBLOCKALL(&ic6f);
	if (setsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, len) < 0)
		err(-1, "4: setsockopt(ICMP6_FILTER, BLOCKALL)");

	/*
	 * Confirm that we can read back a block all filter.
	 */
	len = sizeof(ic6f);
	if (getsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, &len) < 0)
		err(-1, "5: getsockopt(ICMP6_FILTER)");
	if (memcmp(&ic6f, &ic6f_blockall, sizeof(ic6f)) != 0)
		errx(-1, "5: getsockopt(ICMP6_FILTER) - not blockall");

	/*
	 * For completeness, confirm that we can reset to the default.
	 */
	len = sizeof(ic6f);
	ICMP6_FILTER_SETPASSALL(&ic6f);
	if (setsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, len) < 0)
		err(-1, "6: setsockopt(ICMP6_FILTER, PASSALL)");

	/*
	 * ... And that we can read back the pass all rule again.
	 */
	len = sizeof(ic6f);
	if (getsockopt(s, IPPROTO_ICMPV6, ICMP6_FILTER, &ic6f, &len) < 0)
		err(-1, "7: getsockopt(ICMP6_FILTER)");
	if (memcmp(&ic6f, &ic6f_passall, sizeof(ic6f)) != 0)
		errx(-1, "7: getsockopt(ICMP6_FILTER) - not passall");

	close(s);
	return (0);
}
