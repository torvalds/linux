/*-
 * Copyright (c) 2007 Bjoern A. Zeeb
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Confirm that privilege is required to open a pfkey socket, and that this
 * is not allowed in jail.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/pfkeyv2.h>
#include <netinet/in.h>
#include <netipsec/ipsec.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

static char	policy_bypass[]	= "in bypass";
static char	policy_entrust[] = "in entrust";
static char	*bypassbuf = NULL;
static char	*entrustbuf = NULL;
static int	sd = -1;


static int
priv_netinet_ipsec_policy_bypass_setup_af(int asroot, int injail,
    struct test *test, int af)
{

	bypassbuf = ipsec_set_policy(policy_bypass, sizeof(policy_bypass) - 1);
	if (bypassbuf == NULL) {
		warn("%s: ipsec_set_policy(NULL)", __func__);
		return (-1);
	}
	switch (af) {
	case AF_INET:
		sd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sd < 0) {
			warn("%s: socket4", __func__);
			return (-1);
		}
		break;
#ifdef INET6
	case AF_INET6:
		sd = socket(AF_INET6, SOCK_DGRAM, 0);
		if (sd < 0) {
			warn("%s: socket6", __func__);
			return (-1);
		}
		break;
#endif
	default:
		warnx("%s: unexpected address family", __func__);
		return (-1);
	}
	return (0);
}

int
priv_netinet_ipsec_policy4_bypass_setup(int asroot, int injail,
    struct test *test)
{

	return (priv_netinet_ipsec_policy_bypass_setup_af(asroot, injail, test,
	    AF_INET));
}

#ifdef INET6
int
priv_netinet_ipsec_policy6_bypass_setup(int asroot, int injail,
    struct test *test)
{

	return (priv_netinet_ipsec_policy_bypass_setup_af(asroot, injail, test,
	    AF_INET6));
}
#endif


static int
priv_netinet_ipsec_policy_entrust_setup_af(int asroot, int injail,
    struct test *test, int af)
{

	entrustbuf = ipsec_set_policy(policy_entrust, sizeof(policy_entrust)-1);
	if (entrustbuf == NULL) {
		warn("%s: ipsec_set_policy(NULL)", __func__);
		return (-1);
	}
	switch (af) {
	case AF_INET:
		sd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sd < 0) {
			warn("%s: socket4", __func__);
			return (-1);
		}
		break;
#ifdef INET6
	case AF_INET6:
		sd = socket(AF_INET6, SOCK_DGRAM, 0);
		if (sd < 0) {
			warn("%s: socket6", __func__);
			return (-1);
		}
		break;
#endif
	default:
		warnx("%s: unexpected address family", __func__);
		return (-1);
	}
	return (0);
}

int
priv_netinet_ipsec_policy4_entrust_setup(int asroot, int injail,
    struct test *test)
{

	return (priv_netinet_ipsec_policy_entrust_setup_af(asroot, injail, test,
	    AF_INET));
}

#ifdef INET6
int
priv_netinet_ipsec_policy6_entrust_setup(int asroot, int injail,
    struct test *test)
{

	return (priv_netinet_ipsec_policy_entrust_setup_af(asroot, injail, test,
	    AF_INET6));
}
#endif

void
priv_netinet_ipsec_pfkey(int asroot, int injail, struct test *test)
{
	int error, fd;

	fd = socket(PF_KEY, SOCK_RAW, PF_KEY_V2);
	if (fd < 0)
		error = -1;
	else
		error = 0;
	/*
	 * The injail checks are not really priv checks but making sure
	 * sys/kern/uipc_socket.c:socreate cred checks are working correctly.
	 */
	if (asroot && injail)
		expect("priv_netinet_ipsec_pfkey(asroot, injail)", error,
		    -1, EPROTONOSUPPORT);
	if (asroot && !injail)
		expect("priv_netinet_ipsec_pfkey(asroot, !injail)", error,
		    0, 0);
	if (!asroot && injail)
		expect("priv_netinet_ipsec_pfkey(!asroot, injail)", error,
		    -1, EPROTONOSUPPORT);
	if (!asroot && !injail)
		expect("priv_netinet_ipsec_pfkey(!asroot, !injail)", error,
		    -1, EPERM);
	if (fd >= 0)
		(void)close(fd);
}


static void
priv_netinet_ipsec_policy_bypass_af(int asroot, int injail, struct test *test,
    int af)
{
	int error, level, optname;

	switch (af) {
	case AF_INET:
		level = IPPROTO_IP;
		optname = IP_IPSEC_POLICY;
		break;
#ifdef INET6
	case AF_INET6:
		level = IPPROTO_IPV6;
		optname = IPV6_IPSEC_POLICY;
		break;
#endif
	default:
		warnx("%s: unexpected address family", __func__);
		return;
	}
	error = setsockopt(sd, level, optname,
	    bypassbuf, ipsec_get_policylen(bypassbuf));
	if (asroot && injail)
		expect("priv_netinet_ipsec_policy_bypass(asroot, injail)",
		    error, -1, EACCES); /* see ipsec_set_policy */
	if (asroot && !injail)
		expect("priv_netinet_ipsec_policy_bypass(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_netinet_ipsec_policy_bypass(!asroot, injail)",
		    error, -1, EACCES); /* see ipsec_set_policy */
	if (!asroot && !injail)
		expect("priv_netinet_ipsec_policy_bypass(!asroot, !injail)",
		    error, -1, EACCES); /* see ipsec_set_policy */
}

void
priv_netinet_ipsec_policy4_bypass(int asroot, int injail, struct test *test)
{

	priv_netinet_ipsec_policy_bypass_af(asroot, injail, test, AF_INET);
}

#ifdef INET6
void
priv_netinet_ipsec_policy6_bypass(int asroot, int injail, struct test *test)
{

	priv_netinet_ipsec_policy_bypass_af(asroot, injail, test, AF_INET6);
}
#endif

static void
priv_netinet_ipsec_policy_entrust_af(int asroot, int injail, struct test *test,
    int af)
{
	int error, level, optname;

	switch (af) {
	case AF_INET:
		level = IPPROTO_IP;
		optname = IP_IPSEC_POLICY;
		break;
#ifdef INET6
	case AF_INET6:
		level = IPPROTO_IPV6;
		optname = IPV6_IPSEC_POLICY;
		break;
#endif
	default:
		warnx("%s: unexpected address family", __func__);
		return;
	}
	error = setsockopt(sd, level, optname,
	    entrustbuf, ipsec_get_policylen(entrustbuf));
	if (asroot && injail)
		expect("priv_netinet_ipsec_policy_entrust(asroot, injail)",
		    error, 0, 0); /* XXX ipsec_set_policy */
	if (asroot && !injail)
		expect("priv_netinet_ipsec_policy_entrust(asroot, !injail)",
		    error, 0, 0);
	if (!asroot && injail)
		expect("priv_netinet_ipsec_policy_entrust(!asroot, injail)",
		    error, 0, 0); /* XXX ipsec_set_policy */
	if (!asroot && !injail)
		expect("priv_netinet_ipsec_policy_entrust(!asroot, !injail)",
		    error, 0, 0); /* XXX ipsec_set_policy */
}

void
priv_netinet_ipsec_policy4_entrust(int asroot, int injail, struct test *test)
{

	priv_netinet_ipsec_policy_entrust_af(asroot, injail, test, AF_INET);
}

#ifdef INET6
void
priv_netinet_ipsec_policy6_entrust(int asroot, int injail, struct test *test)
{

	priv_netinet_ipsec_policy_entrust_af(asroot, injail, test, AF_INET6);
}
#endif

void
priv_netinet_ipsec_policy_bypass_cleanup(int asroot, int injail,
    struct test *test)
{

	if (bypassbuf != NULL) {
		free(bypassbuf);
		bypassbuf = NULL;
	}
	if (sd >= 0) {
		close(sd);
		sd = -1;
	}
}

void
priv_netinet_ipsec_policy_entrust_cleanup(int asroot, int injail,
    struct test *test)
{

	if (entrustbuf != NULL) {
		free(entrustbuf);
		entrustbuf = NULL;
	}
	if (sd >= 0) {
		close(sd);
		sd = -1;
	}
}

