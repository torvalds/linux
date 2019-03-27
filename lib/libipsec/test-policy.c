/*	$KAME: test-policy.c,v 1.16 2003/08/26 03:24:08 itojun Exp $	*/

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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <net/pfkeyv2.h>
#include <netipsec/key_debug.h>
#include <netipsec/ipsec.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "libpfkey.h"

struct req_t {
	int result;	/* expected result; 0:ok 1:ng */
	char *str;
} reqs[] = {
{ 0, "out ipsec" },
{ 1, "must_error" },
{ 1, "in ipsec must_error" },
{ 1, "out ipsec esp/must_error" },
{ 1, "out discard" },
{ 1, "out none" },
{ 0, "in entrust" },
{ 0, "out entrust" },
{ 1, "out ipsec esp" },
{ 0, "in ipsec ah/transport" },
{ 1, "in ipsec ah/tunnel" },
{ 0, "out ipsec ah/transport/" },
{ 1, "out ipsec ah/tunnel/" },
{ 0, "in ipsec esp / transport / 10.0.0.1-10.0.0.2" },
{ 0, "in ipsec esp/tunnel/::1-::2" },
{ 1, "in ipsec esp/tunnel/10.0.0.1-::2" },
{ 0, "in ipsec esp/tunnel/::1-::2/require" },
{ 0, "out ipsec ah/transport//use" },
{ 1, "out ipsec ah/transport esp/use" },
{ 1, "in ipsec ah/transport esp/tunnel" },
{ 0, "in ipsec ah/transport esp/tunnel/::1-::1" },
{ 0, "in ipsec\n"
	"ah / transport\n"
	"esp / tunnel / ::1-::2" },
{ 0, "out ipsec\n"
	"ah/transport/::1-::2 esp/tunnel/::3-::4/use ah/transport/::5-::6/require\n"
	"ah/transport/::1-::2 esp/tunnel/::3-::4/use ah/transport/::5-::6/require\n"
	"ah/transport/::1-::2 esp/tunnel/::3-::4/use ah/transport/::5-::6/require\n" },
{ 0, "out ipsec esp/transport/fec0::10-fec0::11/use" },
};

int test1(void);
int test1sub1(struct req_t *);
int test1sub2(char *, int);
int test2(void);
int test2sub(int);

int
main(ac, av)
	int ac;
	char **av;
{
	test1();
	test2();

	exit(0);
}

int
test1()
{
	int i;
	int result;

	printf("TEST1\n");
	for (i = 0; i < sizeof(reqs)/sizeof(reqs[0]); i++) {
		printf("#%d [%s]\n", i + 1, reqs[i].str);

		result = test1sub1(&reqs[i]);
		if (result == 0 && reqs[i].result == 1) {
			warnx("ERROR: expecting failure.");
		} else if (result == 1 && reqs[i].result == 0) {
			warnx("ERROR: expecting success.");
		}
	}

	return 0;
}

int
test1sub1(req)
	struct req_t *req;
{
	char *buf;

	buf = ipsec_set_policy(req->str, strlen(req->str));
	if (buf == NULL) {
		printf("ipsec_set_policy: %s\n", ipsec_strerror());
		return 1;
	}

	if (test1sub2(buf, PF_INET) != 0
	 || test1sub2(buf, PF_INET6) != 0) {
		free(buf);
		return 1;
	}
#if 0
	kdebug_sadb_x_policy((struct sadb_ext *)buf);
#endif

	free(buf);
	return 0;
}

int
test1sub2(policy, family)
	char *policy;
	int family;
{
	int so;
	int proto = 0, optname = 0;
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
		err(1, "socket");

	len = ipsec_get_policylen(policy);
#if 0
	printf("\tsetlen:%d\n", len);
#endif

	if (setsockopt(so, proto, optname, policy, len) < 0) {
		printf("fail to set sockopt; %s\n", strerror(errno));
		close(so);
		return 1;
	}

	memset(getbuf, 0, sizeof(getbuf));
	memcpy(getbuf, policy, sizeof(struct sadb_x_policy));
	if (getsockopt(so, proto, optname, getbuf, &len) < 0) {
		printf("fail to get sockopt; %s\n", strerror(errno));
		close(so);
		return 1;
	}

    {
	char *buf = NULL;

#if 0
	printf("\tgetlen:%d\n", len);
#endif

	if ((buf = ipsec_dump_policy(getbuf, NULL)) == NULL) {
		printf("%s\n", ipsec_strerror());
		close(so);
		return 1;
	}
#if 0
	printf("\t[%s]\n", buf);
#endif
	free(buf);
    }

	close (so);
	return 0;
}

char addr[] = {
	28, 28, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	0, 0, 0, 0,
};

int
test2()
{
	int so;
	char *pol1 = "out ipsec";
	char *pol2 = "out ipsec ah/transport//use";
	char *sp1, *sp2;
	int splen1, splen2;
	int spid;
	struct sadb_msg *m;

	printf("TEST2\n");
	if (getuid() != 0)
		errx(1, "root privilege required.");

	sp1 = ipsec_set_policy(pol1, strlen(pol1));
	splen1 = ipsec_get_policylen(sp1);
	sp2 = ipsec_set_policy(pol2, strlen(pol2));
	splen2 = ipsec_get_policylen(sp2);

	if ((so = pfkey_open()) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());

	printf("spdflush()\n");
	if (pfkey_send_spdflush(so) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);

	printf("spdsetidx()\n");
	if (pfkey_send_spdsetidx(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, sp1, splen1, 0) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);
	
	printf("spdupdate()\n");
	if (pfkey_send_spdupdate(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, sp2, splen2, 0) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);

	printf("sleep(4)\n");
	sleep(4);

	printf("spddelete()\n");
	if (pfkey_send_spddelete(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, sp1, splen1, 0) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);

	printf("spdadd()\n");
	if (pfkey_send_spdadd(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, sp2, splen2, 0) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	spid = test2sub(so);

	printf("spdget(%u)\n", spid);
	if (pfkey_send_spdget(so, spid) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);

	printf("sleep(4)\n");
	sleep(4);

	printf("spddelete2()\n");
	if (pfkey_send_spddelete2(so, spid) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	m = pfkey_recv(so);
	free(m);

	printf("spdadd() with lifetime's 10(s)\n");
	if (pfkey_send_spdadd2(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, 0, 10, sp2, splen2, 0) < 0)
		errx(1, "ERROR: %s", ipsec_strerror());
	spid = test2sub(so);

	/* expecting failure */
	printf("spdupdate()\n");
	if (pfkey_send_spdupdate(so, (struct sockaddr *)addr, 128,
				(struct sockaddr *)addr, 128,
				255, sp2, splen2, 0) == 0) {
		warnx("ERROR: expecting failure.");
	}

	return 0;
}

int
test2sub(so)
	int so;
{
	struct sadb_msg *msg;
	caddr_t mhp[SADB_EXT_MAX + 1];

	if ((msg = pfkey_recv(so)) == NULL)
		errx(1, "ERROR: pfkey_recv failure.");
	if (pfkey_align(msg, mhp) < 0)
		errx(1, "ERROR: pfkey_align failure.");

	return ((struct sadb_x_policy *)mhp[SADB_X_EXT_POLICY])->sadb_x_policy_id;
}

