/*-
 * Copyright (c) 2012 Cisco Systems, Inc.
 * All rights reserved.
 *
 * This software was developed by Bjoern Zeeb under contract to
 * Cisco Systems, Inc..
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
 * Regression test on SO_SETFIB setsockopt(2).
 * 
 * Check that the expected domain(9) families all handle the socket option
 * correctly and do proper bounds checks.
 *
 * Test plan:
 * 1. Get system wide number of FIBs from sysctl and convert to index (-= 1).
 * 2. For each protocol family (INET, INET6, ROUTE and LOCAL) open socketes of
 *    type (STREAM, DGRAM and RAW) as supported.
 * 3. Do a sequence of -2, -1, 0, .. n, n+1, n+2 SO_SETFIB sockopt calls,
 *    expecting the first two and last two to fail (valid 0 ... n).
 * 4. Try 3 random numbers.  Calculate result based on valid range.
 * 5. Repeat for next domain family and type from (2) on.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct t_dom {
	int		domain;
	const char	*name;
} t_dom[] = {
#ifdef INET6
	{ .domain = PF_INET6, .name = "PF_INET6" },
#endif
#ifdef INET
	{ .domain = PF_INET, .name = "PF_INET" },
#endif
	{ .domain = PF_ROUTE, .name = "PF_ROUTE" },
	{ .domain = PF_LOCAL, .name = "PF_LOCAL" },
};

static struct t_type {
	int		type;
	const char	*name;
} t_type[] = {
	{ .type = SOCK_STREAM, .name = "SOCK_STREAM" },
	{ .type = SOCK_DGRAM, .name = "SOCK_DGRAM" },
	{ .type = SOCK_RAW, .name = "SOCK_RAW" },
};

/*
 * Number of FIBs as read from net.fibs sysctl - 1.  Initialize to clear out of
 * bounds value to not accidentally run on a limited range. 
 */
static int rt_numfibs = -42;

/* Number of test case. */
static int testno = 1;


/*
 * Try the setsockopt with given FIB number i on socket s.
 * Handle result given on error and valid range and errno.
 */
static void
so_setfib(int s, int i, u_int dom, u_int type)
{
	int error;

	error = setsockopt(s, SOL_SOCKET, SO_SETFIB, &i, sizeof(i));
	/* For out of bounds we expect an error. */
	if (error == -1 && (i < 0 || i > rt_numfibs))
		printf("ok %d %s_%s_%d\n", testno, t_dom[dom].name,
		    t_type[type].name, i);
	else if (error != -1 && (i < 0 || i > rt_numfibs))
		printf("not ok %d %s_%s_%d # setsockopt(%d, SOL_SOCKET, "
		    "SO_SETFIB, %d, ..) unexpectedly succeeded\n", testno,
		    t_dom[dom].name, t_type[type].name, i, s, i);
	else if (error == 0)
		printf("ok %d %s_%s_%d\n", testno, t_dom[dom].name,
		    t_type[type].name, i);
	else if (errno != EINVAL)
		printf("not ok %d %s_%s_%d # setsockopt(%d, SOL_SOCKET, "
		    "SO_SETFIB, %d, ..) unexpected error: %s\n", testno,
		    t_dom[dom].name, t_type[type].name, i, s, i,
		    strerror(errno));
	else
		printf("not ok %d %s_%s_%d\n", testno, t_dom[dom].name,
		    t_type[type].name, i);

	/* Test run done, next please. */
	testno++;
}

/*
 * Main test.  Open socket given domain family and type.  For each FIB, out of
 * bounds FIB numbers and 3 random FIB numbers set the socket option.
 */
static void
t(u_int dom, u_int type)
{
	int i, s;

	/* PF_ROUTE only supports RAW socket types, while PF_LOCAL does not. */
	if (t_dom[dom].domain == PF_ROUTE && t_type[type].type != SOCK_RAW)
		return;
	if (t_dom[dom].domain == PF_LOCAL && t_type[type].type == SOCK_RAW)
		return;

	/* Open socket for given combination. */
	s = socket(t_dom[dom].domain, t_type[type].type, 0);
	if (s == -1) {
		printf("not ok %d %s_%s # socket(): %s\n", testno,
		    t_dom[dom].name, t_type[type].name, strerror(errno));
		testno++;
		return;
	}
	
	/* Test FIBs -2, -1, 0, .. n, n + 1, n + 2. */
	for (i = -2; i <= (rt_numfibs + 2); i++)
		so_setfib(s, i, dom, type);

	/* Test 3 random FIB numbers. */
	for (i = 0; i < 3; i++)
		so_setfib(s, (int)random(), dom, type);

	/* Close socket. */
	close(s);
}

/*
 * Returns 0 if no program error, 1 on sysctlbyname error.
 * Test results are communicated by printf("[not ]ok <n> ..").
 */
int
main(int argc __unused, char *argv[] __unused)
{
	u_int i, j;
	size_t s;

	if (geteuid() != 0) {
		printf("1..0 # SKIP: must be root\n");
		return (0);
	}

	/* Initialize randomness. */
	srandomdev();

	/* Get number of FIBs supported by kernel. */
	s = sizeof(rt_numfibs);
	if (sysctlbyname("net.fibs", &rt_numfibs, &s, NULL, 0) == -1)
		err(1, "sysctlbyname(net.fibs, ..)");

	printf("1..%lu\n",
	    (nitems(t_dom) - 1) * nitems(t_type) * (2 + rt_numfibs + 2 + 3));

	/* Adjust from number to index. */
	rt_numfibs -= 1;

	/* Run tests. */
	for (i = 0; i < sizeof(t_dom) / sizeof(struct t_dom); i++)
		for (j = 0; j < sizeof(t_type) / sizeof(struct t_type); j++)
			t(i, j);

	return (0);
}

/* end */
