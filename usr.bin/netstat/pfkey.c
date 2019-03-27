/*	$NetBSD: inet.c,v 1.35.2.1 1999/04/29 14:57:08 perry Exp $	*/
/*	$KAME: ipsec.c,v 1.25 2001/03/12 09:04:39 itojun Exp $	*/
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
/*-
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)inet.c	8.5 (Berkeley) 5/24/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <netinet/in.h>

#ifdef IPSEC
#include <netipsec/keysock.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <libxo/xo.h>
#include "netstat.h"

#ifdef IPSEC

static const char *pfkey_msgtypenames[] = {
	"reserved", "getspi", "update", "add", "delete",
	"get", "acquire", "register", "expire", "flush",
	"dump", "x_promisc", "x_pchange", "x_spdupdate", "x_spdadd",
	"x_spddelete", "x_spdget", "x_spdacquire", "x_spddump", "x_spdflush",
	"x_spdsetidx", "x_spdexpire", "x_spddelete2"
};

static const char *pfkey_msgtype_names (int);


static const char *
pfkey_msgtype_names(int x)
{
	const int max = nitems(pfkey_msgtypenames);
	static char buf[20];

	if (x < max && pfkey_msgtypenames[x])
		return pfkey_msgtypenames[x];
	snprintf(buf, sizeof(buf), "#%d", x);
	return buf;
}

void
pfkey_stats(u_long off, const char *name, int family __unused,
    int proto __unused)
{
	struct pfkeystat pfkeystat;
	unsigned first, type;

	if (off == 0)
		return;
	xo_emit("{T:/%s}:\n", name);
	xo_open_container(name);
	kread_counters(off, (char *)&pfkeystat, sizeof(pfkeystat));

#define	p(f, m) if (pfkeystat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)pfkeystat.f, plural(pfkeystat.f))

	/* userland -> kernel */
	p(out_total, "\t{:sent-requests/%ju} "
	    "{N:/request%s sent from userland}\n");
	p(out_bytes, "\t{:sent-bytes/%ju} "
	    "{N:/byte%s sent from userland}\n");
	for (first = 1, type = 0;
	    type<sizeof(pfkeystat.out_msgtype)/sizeof(pfkeystat.out_msgtype[0]);
	    type++) {
		if (pfkeystat.out_msgtype[type] <= 0)
			continue;
		if (first) {
			xo_open_list("output-histogram");
			xo_emit("\t{T:histogram by message type}:\n");
			first = 0;
		}
		xo_open_instance("output-histogram");
		xo_emit("\t\t{k::type/%s}: {:count/%ju}\n",
		    pfkey_msgtype_names(type),
		    (uintmax_t)pfkeystat.out_msgtype[type]);
		xo_close_instance("output-histogram");
	}
	if (!first)
		xo_close_list("output-histogram");

	p(out_invlen, "\t{:dropped-bad-length/%ju} "
	    "{N:/message%s with invalid length field}\n");
	p(out_invver, "\t{:dropped-bad-version/%ju} "
	    "{N:/message%s with invalid version field}\n");
	p(out_invmsgtype, "\t{:dropped-bad-type/%ju} "
	    "{N:/message%s with invalid message type field}\n");
	p(out_tooshort, "\t{:dropped-too-short/%ju} "
	    "{N:/message%s too short}\n");
	p(out_nomem, "\t{:dropped-no-memory/%ju} "
	    "{N:/message%s with memory allocation failure}\n");
	p(out_dupext, "\t{:dropped-duplicate-extension/%ju} "
	    "{N:/message%s with duplicate extension}\n");
	p(out_invexttype, "\t{:dropped-bad-extension/%ju} "
	    "{N:/message%s with invalid extension type}\n");
	p(out_invsatype, "\t{:dropped-bad-sa-type/%ju} "
	    "{N:/message%s with invalid sa type}\n");
	p(out_invaddr, "\t{:dropped-bad-address-extension/%ju} "
	    "{N:/message%s with invalid address extension}\n");

	/* kernel -> userland */
	p(in_total, "\t{:received-requests/%ju} "
	    "{N:/request%s sent to userland}\n");
	p(in_bytes, "\t{:received-bytes/%ju} "
	    "{N:/byte%s sent to userland}\n");
	for (first = 1, type = 0;
	    type < sizeof(pfkeystat.in_msgtype)/sizeof(pfkeystat.in_msgtype[0]);
	    type++) {
		if (pfkeystat.in_msgtype[type] <= 0)
			continue;
		if (first) {
			xo_open_list("input-histogram");
			xo_emit("\t{T:histogram by message type}:\n");
			first = 0;
		}
		xo_open_instance("input-histogram");
		xo_emit("\t\t{k:type/%s}: {:count/%ju}\n",
		    pfkey_msgtype_names(type),
		    (uintmax_t)pfkeystat.in_msgtype[type]);
		xo_close_instance("input-histogram");
	}
	if (!first)
		xo_close_list("input-histogram");
	p(in_msgtarget[KEY_SENDUP_ONE], "\t{:received-one-socket/%ju} "
	    "{N:/message%s toward single socket}\n");
	p(in_msgtarget[KEY_SENDUP_ALL], "\t{:received-all-sockets/%ju} "
	    "{N:/message%s toward all sockets}\n");
	p(in_msgtarget[KEY_SENDUP_REGISTERED],
	    "\t{:received-registered-sockets/%ju} "
	    "{N:/message%s toward registered sockets}\n");
	p(in_nomem, "\t{:discarded-no-memory/%ju} "
	    "{N:/message%s with memory allocation failure}\n");
#undef p
	xo_close_container(name);
}
#endif /* IPSEC */
