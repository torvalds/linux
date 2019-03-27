/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Bjoern A. Zeeb <bz@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include "findsaddr.h"
#include "traceroute.h"

/*
 * Return the source address for the given destination address.
 *
 * This makes use of proper source address selection in the FreeBSD kernel
 * even taking jails into account (sys/netinet/in_pcb.c:in_pcbladdr()).
 * We open a UDP socket, and connect to the destination, letting the kernel
 * do the bind and then read the source IPv4 address using getsockname(2).
 * This has multiple advantages: no need to do PF_ROUTE operations possibly
 * needing special privileges, jails properly taken into account and most
 * important - getting the result the kernel would give us rather than
 * best-guessing ourselves.
 */
const char *
findsaddr(register const struct sockaddr_in *to,
    register struct sockaddr_in *from)
{
	const char *errstr;
	struct sockaddr_in cto, cfrom;
	int s;
	socklen_t len;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
		return ("failed to open DGRAM socket for src addr selection.");

	errstr = NULL;
	len = sizeof(struct sockaddr_in);
	memcpy(&cto, to, len);
	cto.sin_port = htons(65535);	/* Dummy port for connect(2). */
	if (connect(s, (struct sockaddr *)&cto, len) == -1) {
		errstr = "failed to connect to peer for src addr selection.";
		goto err;
	}

	if (getsockname(s, (struct sockaddr *)&cfrom, &len) == -1) {
		errstr = "failed to get socket name for src addr selection.";
		goto err;
	}

	if (len != sizeof(struct sockaddr_in) || cfrom.sin_family != AF_INET) {
		errstr = "unexpected address family in src addr selection.";
		goto err;
	}

	/* Update source address for traceroute. */
	setsin(from, cfrom.sin_addr.s_addr);

err:
	(void) close(s);

	/* No error (string) to return. */
	return (errstr);
}

/* end */
