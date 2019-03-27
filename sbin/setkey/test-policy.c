/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
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
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet6/in6.h>
#include <netipsec/ipsec.h>
#include <stdlib.h>
#include <string.h>


char *requests[] = {
"must_error",		/* must be error */
"ipsec must_error",	/* must be error */
"ipsec esp/must_error",	/* must be error */
"discard",
"none",
"entrust",
"bypass",		/* may be error */
"ipsec esp",		/* must be error */
"ipsec ah/require",
"ipsec ah/use/",
"ipsec esp/require ah/default/203.178.141.194",
"ipsec ah/use/203.178.141.195 esp/use/203.178.141.194",
"ipsec esp/elf.wide.ydc.co.jp esp/www.wide.ydc.co.jp"
"
ipsec esp/require ah/use esp/require/10.0.0.1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1 ah/use/3ffe:501:481d::1
ah/use/3ffe:501:481d::1  ah/use/3ffe:501:481d::1ah/use/3ffe:501:481d::1
",
};

u_char	*p_secpolicy;

int	test(char *buf, int family);
char	*setpolicy(char *req);

main()
{
	int i;
	char *buf;

	for (i = 0; i < nitems(requests); i++) {
		printf("* requests:[%s]\n", requests[i]);
		if ((buf = setpolicy(requests[i])) == NULL)
			continue;
		printf("\tsetlen:%d\n", PFKEY_EXTLEN(buf));

		printf("\tPF_INET:\n");
		test(buf, PF_INET);

		printf("\tPF_INET6:\n");
		test(buf, PF_INET6);
		free(buf);
	}
}

int test(char *policy, int family)
{
	int so, proto, optname;
	int len;
	char getbuf[1024];

	switch (family) {
	case PF_INET:
		proto = IPPROTO_IP;
		optname = IP_IPSEC_POLICY;
		break;
	case PF_INET6:
		proto = IPPROTO_IPV6;
		optname = IPV6_IPSEC_POLICY;
		break;
	}

	if ((so = socket(family, SOCK_DGRAM, 0)) < 0)
		perror("socket");

	if (setsockopt(so, proto, optname, policy, PFKEY_EXTLEN(policy)) < 0)
		perror("setsockopt");

	len = sizeof(getbuf);
	memset(getbuf, 0, sizeof(getbuf));
	if (getsockopt(so, proto, optname, getbuf, &len) < 0)
		perror("getsockopt");

    {
	char *buf = NULL;

	printf("\tgetlen:%d\n", len);

	if ((buf = ipsec_dump_policy(getbuf, NULL)) == NULL)
		ipsec_strerror();
	else
		printf("\t[%s]\n", buf);

	free(buf);
    }

	close (so);
}

char *setpolicy(char *req)
{
	int len;
	char *buf;

	if ((len = ipsec_get_policylen(req)) < 0) {
		printf("ipsec_get_policylen: %s\n", ipsec_strerror());
		return NULL;
	}

	if ((buf = malloc(len)) == NULL) {
		perror("malloc");
		return NULL;
	}

	if ((len = ipsec_set_policy(buf, len, req)) < 0) {
		printf("ipsec_set_policy: %s\n", ipsec_strerror());
		free(buf);
		return NULL;
	}

	return buf;
}
