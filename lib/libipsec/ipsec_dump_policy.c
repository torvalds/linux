/*	$KAME: ipsec_dump_policy.c,v 1.13 2002/06/27 14:35:11 itojun Exp $	*/

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

#include <netipsec/key_var.h>
#include <netinet/in.h>
#include <netipsec/ipsec.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "ipsec_strerror.h"

static const char *ipsp_dir_strs[] = {
	"any", "in", "out",
};

static const char *ipsp_policy_strs[] = {
	"discard", "none", "ipsec", "entrust", "bypass",
};

static char *ipsec_dump_ipsecrequest(char *, size_t,
	struct sadb_x_ipsecrequest *, size_t);
static int set_addresses(char *, size_t, struct sockaddr *, struct sockaddr *);
static char *set_address(char *, size_t, struct sockaddr *);

/*
 * policy is sadb_x_policy buffer.
 * Must call free() later.
 * When delimiter == NULL, alternatively ' '(space) is applied.
 */
char *
ipsec_dump_policy(policy, delimiter)
	caddr_t policy;
	char *delimiter;
{
	struct sadb_x_policy *xpl = (struct sadb_x_policy *)policy;
	struct sadb_x_ipsecrequest *xisr;
	size_t off, buflen;
	char *buf;
	char isrbuf[1024];
	char *newbuf;

	/* sanity check */
	if (policy == NULL)
		return NULL;
	if (xpl->sadb_x_policy_exttype != SADB_X_EXT_POLICY) {
		__ipsec_errcode = EIPSEC_INVAL_EXTTYPE;
		return NULL;
	}

	/* set delimiter */
	if (delimiter == NULL)
		delimiter = " ";

	switch (xpl->sadb_x_policy_dir) {
	case IPSEC_DIR_ANY:
	case IPSEC_DIR_INBOUND:
	case IPSEC_DIR_OUTBOUND:
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_DIR;
		return NULL;
	}

	switch (xpl->sadb_x_policy_type) {
	case IPSEC_POLICY_DISCARD:
	case IPSEC_POLICY_NONE:
	case IPSEC_POLICY_IPSEC:
	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_ENTRUST:
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_POLICY;
		return NULL;
	}

	buflen = strlen(ipsp_dir_strs[xpl->sadb_x_policy_dir])
		+ 1	/* space */
		+ strlen(ipsp_policy_strs[xpl->sadb_x_policy_type])
		+ 1;	/* NUL */

	if ((buf = malloc(buflen)) == NULL) {
		__ipsec_errcode = EIPSEC_NO_BUFS;
		return NULL;
	}
	snprintf(buf, buflen, "%s %s", ipsp_dir_strs[xpl->sadb_x_policy_dir],
	    ipsp_policy_strs[xpl->sadb_x_policy_type]);

	if (xpl->sadb_x_policy_type != IPSEC_POLICY_IPSEC) {
		__ipsec_errcode = EIPSEC_NO_ERROR;
		return buf;
	}

	/* count length of buffer for use */
	off = sizeof(*xpl);
	while (off < PFKEY_EXTLEN(xpl)) {
		xisr = (struct sadb_x_ipsecrequest *)((caddr_t)xpl + off);
		off += xisr->sadb_x_ipsecrequest_len;
	}

	/* validity check */
	if (off != PFKEY_EXTLEN(xpl)) {
		__ipsec_errcode = EIPSEC_INVAL_SADBMSG;
		free(buf);
		return NULL;
	}

	off = sizeof(*xpl);
	while (off < PFKEY_EXTLEN(xpl)) {
		xisr = (struct sadb_x_ipsecrequest *)((caddr_t)xpl + off);

		if (ipsec_dump_ipsecrequest(isrbuf, sizeof(isrbuf), xisr,
		    PFKEY_EXTLEN(xpl) - off) == NULL) {
			free(buf);
			return NULL;
		}

		buflen = strlen(buf) + strlen(delimiter) + strlen(isrbuf) + 1;
		newbuf = (char *)realloc(buf, buflen);
		if (newbuf == NULL) {
			__ipsec_errcode = EIPSEC_NO_BUFS;
			free(buf);
			return NULL;
		}
		buf = newbuf;
		snprintf(buf + strlen(buf), buflen - strlen(buf),
		    "%s%s", delimiter, isrbuf);

		off += xisr->sadb_x_ipsecrequest_len;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return buf;
}

static char *
ipsec_dump_ipsecrequest(buf, len, xisr, bound)
	char *buf;
	size_t len;
	struct sadb_x_ipsecrequest *xisr;
	size_t bound;	/* boundary */
{
	const char *proto, *mode, *level;
	char abuf[NI_MAXHOST * 2 + 2];

	if (xisr->sadb_x_ipsecrequest_len > bound) {
		__ipsec_errcode = EIPSEC_INVAL_PROTO;
		return NULL;
	}

	switch (xisr->sadb_x_ipsecrequest_proto) {
	case IPPROTO_ESP:
		proto = "esp";
		break;
	case IPPROTO_AH:
		proto = "ah";
		break;
	case IPPROTO_IPCOMP:
		proto = "ipcomp";
		break;
	case IPPROTO_TCP:
		proto = "tcp";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_PROTO;
		return NULL;
	}

	switch (xisr->sadb_x_ipsecrequest_mode) {
	case IPSEC_MODE_ANY:
		mode = "any";
		break;
	case IPSEC_MODE_TRANSPORT:
		mode = "transport";
		break;
	case IPSEC_MODE_TUNNEL:
		mode = "tunnel";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_MODE;
		return NULL;
	}

	abuf[0] = '\0';
	if (xisr->sadb_x_ipsecrequest_len > sizeof(*xisr)) {
		struct sockaddr *sa1, *sa2;
		caddr_t p;

		p = (caddr_t)(xisr + 1);
		sa1 = (struct sockaddr *)p;
		sa2 = (struct sockaddr *)(p + sa1->sa_len);
		if (sizeof(*xisr) + sa1->sa_len + sa2->sa_len !=
		    xisr->sadb_x_ipsecrequest_len) {
			__ipsec_errcode = EIPSEC_INVAL_ADDRESS;
			return NULL;
		}
		if (set_addresses(abuf, sizeof(abuf), sa1, sa2) != 0) {
			__ipsec_errcode = EIPSEC_INVAL_ADDRESS;
			return NULL;
		}
	}

	switch (xisr->sadb_x_ipsecrequest_level) {
	case IPSEC_LEVEL_DEFAULT:
		level = "default";
		break;
	case IPSEC_LEVEL_USE:
		level = "use";
		break;
	case IPSEC_LEVEL_REQUIRE:
		level = "require";
		break;
	case IPSEC_LEVEL_UNIQUE:
		level = "unique";
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_LEVEL;
		return NULL;
	}

	if (xisr->sadb_x_ipsecrequest_reqid == 0)
		snprintf(buf, len, "%s/%s/%s/%s", proto, mode, abuf, level);
	else {
		int ch;

		if (xisr->sadb_x_ipsecrequest_reqid > IPSEC_MANUAL_REQID_MAX)
			ch = '#';
		else
			ch = ':';
		snprintf(buf, len, "%s/%s/%s/%s%c%u", proto, mode, abuf, level,
		    ch, xisr->sadb_x_ipsecrequest_reqid);
	}

	return buf;
}

static int
set_addresses(buf, len, sa1, sa2)
	char *buf;
	size_t len;
	struct sockaddr *sa1;
	struct sockaddr *sa2;
{
	char tmp1[NI_MAXHOST], tmp2[NI_MAXHOST];

	if (set_address(tmp1, sizeof(tmp1), sa1) == NULL ||
	    set_address(tmp2, sizeof(tmp2), sa2) == NULL)
		return -1;
	if (strlen(tmp1) + 1 + strlen(tmp2) + 1 > len)
		return -1;
	snprintf(buf, len, "%s-%s", tmp1, tmp2);
	return 0;
}

static char *
set_address(buf, len, sa)
	char *buf;
	size_t len;
	struct sockaddr *sa;
{
	const int niflags = NI_NUMERICHOST;

	if (len < 1)
		return NULL;
	buf[0] = '\0';
	if (getnameinfo(sa, sa->sa_len, buf, len, NULL, 0, niflags) != 0)
		return NULL;
	return buf;
}
