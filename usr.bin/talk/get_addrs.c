/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char sccsid[] = "@(#)get_addrs.c	8.1 (Berkeley) 6/6/93";
#endif

#include <err.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#include "talk.h"
#include "talk_ctl.h"

void
get_addrs(const char *my_machine_name __unused, const char *his_machine_name)
{
	struct hostent *hp;
	struct servent *sp;

	msg.pid = htonl(getpid());

	hp = gethostbyname(his_machine_name);
	if (hp == NULL)
		errx(1, "%s: %s", his_machine_name, hstrerror(h_errno));
	bcopy(hp->h_addr, (char *) &his_machine_addr, hp->h_length);
	if (get_iface(&his_machine_addr, &my_machine_addr) == -1)
		err(1, "failed to find my interface address");
	/* find the server's port */
	sp = getservbyname("ntalk", "udp");
	if (sp == NULL)
		errx(1, "ntalk/udp: service is not registered");
	daemon_port = sp->s_port;
}
