/*-
 * Copyright (c) 2007 Michael Telahun Makonnen
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_subr.h"

static void init_hdrs(struct msghdr *, struct cmsghdr *, char *, size_t);
static void test_cmsg_firsthdr();
static void test_cmsg_nexthdr();
static void test_rth_space();
static void test_rth_segments();
static void test_rth_add();
static void test_rth_init();

int
main(int argc, char* argv[])
{
	/*
	 * Initialize global variables.
	 */
	g_total = 0;
	g_pass = 0;
	g_fail = 0;
	memset(g_funcname, 0, sizeof(g_funcname));

	/*
	 * Start the tests.
	 */
	printf("Starting inet6_rth_* and cmsg macro regression tests...\n");

	test_cmsg_firsthdr();			/* CMSG_FIRSTHDR    */
	test_cmsg_nexthdr();			/* CMSG_NEXTHDR	    */
	test_rth_space();			/* inet6_rth_space  */
	test_rth_segments();			/* inet6_rth_segments */
	test_rth_add();				/* inet6_rth_add    */
	test_rth_init();			/* inet6_rth_space  */

	if (g_fail == 0)
		printf("OK. ");
	else
		printf("NOT OK. ");
	printf("Total: %d  Pass: %d  Fail: %d\n", g_total, g_pass, g_fail);

	return (g_fail);
}

void
test_rth_init()
{
	char buf[10240];
	char *pbuf;

	set_funcname("test_rth_init", sizeof("test_rth_init\0"));

	pbuf = inet6_rth_init((void *)buf, 10, IPV6_RTHDR_TYPE_0, 100);
	checkptr(NULL, pbuf, "buffer too small\0");

	pbuf = inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 0);
	checkptr((caddr_t)&buf, pbuf, "0 segments\0");

	pbuf = inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 127);
	checkptr((caddr_t)&buf, pbuf, "127 segments\0");

	pbuf = inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, -1);
	checkptr(NULL, pbuf, "negative number of segments\0");

	pbuf = inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 128);
	checkptr(NULL, pbuf, "128 segments\0");
}

void
test_rth_add()
{
	int	i, ret;
	char	buf[10240];
	struct addrinfo *res;
	struct addrinfo hints;

	set_funcname("test_rth_add", sizeof("test_rth_add\0"));

	if (NULL == inet6_rth_init(buf, 10240, IPV6_RTHDR_TYPE_0, 127))
		abort();
	memset((void *)&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_flags = AI_NUMERICHOST;
	if (0 != getaddrinfo("::1", NULL, (const struct addrinfo *)&hints, &res))
		abort();
	for (i = 0; i < 127; i++)
		inet6_rth_add((void *)buf,
		    &((struct sockaddr_in6 *)(res->ai_addr))->sin6_addr);
	checknum(127, ((struct ip6_rthdr0 *)buf)->ip6r0_segleft, 0,
	    "add 127 segments\0");

	ret = inet6_rth_add((void *)buf,
	    &((struct sockaddr_in6 *)(res->ai_addr))->sin6_addr);
	checknum(-1, ret, 0, "add 128th segment to 127 segment header\0");

	freeaddrinfo(res);
}

void
test_rth_segments()
{
	int	seg;
	char	buf[10240];

	set_funcname("test_rth_segments", sizeof("test_rth_segments\0"));

	/*
	 * Test: invalid routing header type.
	 */
	if (NULL == inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 0))
		abort();
	((struct ip6_rthdr *)buf)->ip6r_type = ~IPV6_RTHDR_TYPE_0;
	seg = inet6_rth_segments((const void *)buf);
	checknum(-1, seg, 0, "invalid routing header type\0");

	/*
	 * Test: 0 segments.
	 */
	if (NULL == inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 0))
		abort();
	seg = inet6_rth_segments((const void *)buf);
	checknum(0, seg, 0, "0 segments\0");

	/*
	 * Test: 127 segments.
	 */
	if (NULL == inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 127))
		abort();
	seg = inet6_rth_segments((const void *)buf);
	checknum(127, seg, 0, "127 segments\0");

	/*
	 * Test: -1 segments.
	 */
/*
	if (NULL == inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 0))
		abort();
	((struct ip6_rthdr0 *)buf)->ip6r0_len = -1 * 2;
	seg = inet6_rth_segments((const void *)buf);
	checknum(-1, seg, 0, "-1 segments\0");
*/
	/*
	 * Test: 128 segments.
	 */
/*
	if (NULL == inet6_rth_init((void *)buf, 10240, IPV6_RTHDR_TYPE_0, 127))
		abort();
	((struct ip6_rthdr0 *)buf)->ip6r0_len = 128 * 2;
	seg = inet6_rth_segments((const void *)buf);
	checknum(-1, seg, 0, "128 segments\0");
*/
}

void
test_rth_space()
{
	socklen_t len;

	set_funcname("test_rth_space", sizeof("test_rth_space\0"));

	/*
	 * Test: invalid routing header type.
	 */
	len = inet6_rth_space(~IPV6_RTHDR_TYPE_0, 0);
	checknum(0, len, 0, "invalid routing header type\0");

	/*
	 * Test: valid number of segments.
	 */
	len = inet6_rth_space(IPV6_RTHDR_TYPE_0, 0);
	checknum(0, len, 1, "0 segments\0");
	len = inet6_rth_space(IPV6_RTHDR_TYPE_0, 127);
	checknum(0, len, 1, "0 segments\0");

	/*
	 * Test: invalid number of segments.
	 */
	len = inet6_rth_space(IPV6_RTHDR_TYPE_0, -1);
	checknum(0, len, 0, "-1 segments\0");
	len = inet6_rth_space(IPV6_RTHDR_TYPE_0, 128);
	checknum(0, len, 0, "128 segments\0");
}

void
test_cmsg_nexthdr()
{
	struct msghdr  mh;
	struct cmsghdr cmh;
	struct cmsghdr *cmhp, *cmhnextp;
	char ancbuf[10240];
	char magic[] = "MAGIC";

	set_funcname("test_cmsg_nexthdr", sizeof("test_cmsg_nexthdr"));

	/*
	 * Test: More than one cmsghdr
	 */
	init_hdrs(&mh, &cmh, ancbuf, sizeof(ancbuf));
	mh.msg_control = (caddr_t)ancbuf;
	mh.msg_controllen  = CMSG_SPACE(0) * 2;	/* 2 cmsghdr with no data */
	cmh.cmsg_len = CMSG_LEN(0);

	/* 
	 * Copy the same instance of cmsghdr twice. Use a magic value
	 * to id the second copy.
	 */
	bcopy((void *)&cmh, (void *)ancbuf, sizeof(cmh));
	strlcpy((char *)&cmh, (const char *)&magic, sizeof(magic));
	bcopy((void *)&cmh,
	    (void *)((caddr_t)ancbuf + CMSG_SPACE(0)),
	    sizeof(cmh));
	cmhp = CMSG_FIRSTHDR(&mh);
	cmhnextp = CMSG_NXTHDR(&mh, cmhp);
	checkstr((const char *)&magic, (const char *)cmhnextp, sizeof(magic),
	    "more than one cmsghdr\0");

	/*
	 * Test: only one cmsghdr
	 */
	init_hdrs(&mh, &cmh, ancbuf, sizeof(ancbuf));
	mh.msg_control = (caddr_t)ancbuf;
	mh.msg_controllen  = CMSG_SPACE(0);
	cmh.cmsg_len = CMSG_LEN(0);
	bcopy((void *)&cmh, (void *)ancbuf, sizeof(cmh));
	cmhp = CMSG_FIRSTHDR(&mh);
	cmhnextp = CMSG_NXTHDR(&mh, cmhp);
	checkptr(NULL, (caddr_t)cmhnextp, "only one cmsghdr\0");

	/*
	 * Test: NULL cmsg pointer
	 */
	init_hdrs(&mh, &cmh, ancbuf, sizeof(ancbuf));
	mh.msg_control = (caddr_t)ancbuf;
	mh.msg_controllen  = sizeof(ancbuf);
	cmh.cmsg_len = sizeof(ancbuf);
	bcopy((void *)&cmh, (void *)ancbuf, sizeof(cmh));
	cmhp = CMSG_FIRSTHDR(&mh);
	cmhnextp = CMSG_NXTHDR(&mh, NULL);
	checkptr((caddr_t)cmhp, (caddr_t)cmhnextp, "null second argument\0");
}

void
test_cmsg_firsthdr()
{
	struct msghdr  mh;
	struct cmsghdr cmh;
	struct cmsghdr *cmhp;
	char ancbuf[1024];
	char magic[] = "MAGIC";

	set_funcname("test_cmsg_firsthdr", sizeof("test_cmsg_firsthdr"));

	/* CMSG_FIRSTHDR() where msg_control is NULL */
	init_hdrs(&mh, NULL, NULL, 0);
	mh.msg_control = NULL;
	cmhp = CMSG_FIRSTHDR(&mh);
	checkptr(NULL, (caddr_t)cmhp,
	    "msg_control is NULL\0");

	/* - where msg_controllen < sizeof cmsghdr */
	init_hdrs(&mh, NULL, NULL, 0);
	mh.msg_control = (caddr_t)&cmh;
	mh.msg_controllen = sizeof(cmh) - 1;
	cmhp = CMSG_FIRSTHDR(&mh);
	checkptr(NULL, (caddr_t)cmhp,
	    "msg_controllen < sizeof cmsghdr\0");

	/* - where msg_controllen == 0 */
	init_hdrs(&mh, NULL, NULL, 0);
	mh.msg_control = (caddr_t)&cmh;
	mh.msg_controllen = 0;
	cmhp = CMSG_FIRSTHDR(&mh);
	checkptr(NULL, (caddr_t)cmhp,
	    "msg_controllen == 0\0");

	/* no errors */
	init_hdrs(&mh, &cmh, ancbuf, sizeof(ancbuf));
	memset((void *)ancbuf, 0, sizeof(ancbuf));
	mh.msg_control = (caddr_t)ancbuf;
	mh.msg_controllen  = sizeof(ancbuf);
	strlcpy((char *)&cmh, (const char *)&magic, sizeof(magic));
	bcopy((void *)&cmh, (void *)ancbuf, sizeof(cmh));
	cmhp = CMSG_FIRSTHDR(&mh);
	checkstr((const char *)&magic, (const char *)cmhp, sizeof(magic),
	    "with payload\0");
}

void
init_hdrs(struct msghdr *mhp, struct cmsghdr *cmhp, char *bufp, size_t bufsize)
{
	if (mhp != NULL)
		memset((void *)mhp, 0, sizeof(struct msghdr));
	if (cmhp != NULL)
		memset((void *)cmhp, 0, sizeof(struct cmsghdr));
	if (bufp != NULL)
		memset((void *)bufp, 0, bufsize);
}
