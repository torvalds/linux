/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Andrew Thompson. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_gre.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "ifconfig.h"

#define	GREBITS	"\020\01ENABLE_CSUM\02ENABLE_SEQ"

static	void gre_status(int s);

static void
gre_status(int s)
{
	uint32_t opts = 0;

	ifr.ifr_data = (caddr_t)&opts;
	if (ioctl(s, GREGKEY, &ifr) == 0)
		if (opts != 0)
			printf("\tgrekey: 0x%x (%u)\n", opts, opts);
	opts = 0;
	if (ioctl(s, GREGOPTS, &ifr) != 0 || opts == 0)
		return;
	printb("\toptions", opts, GREBITS);
	putchar('\n');
}

static void
setifgrekey(const char *val, int dummy __unused, int s, 
    const struct afswtch *afp)
{
	uint32_t grekey = strtol(val, NULL, 0);

	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&grekey;
	if (ioctl(s, GRESKEY, (caddr_t)&ifr) < 0)
		warn("ioctl (set grekey)");
}

static void
setifgreopts(const char *val, int d, int s, const struct afswtch *afp)
{
	uint32_t opts;

	ifr.ifr_data = (caddr_t)&opts;
	if (ioctl(s, GREGOPTS, &ifr) == -1) {
		warn("ioctl(GREGOPTS)");
		return;
	}

	if (d < 0)
		opts &= ~(-d);
	else
		opts |= d;

	if (ioctl(s, GRESOPTS, &ifr) == -1) {
		warn("ioctl(GIFSOPTS)");
		return;
	}
}


static struct cmd gre_cmds[] = {
	DEF_CMD_ARG("grekey",			setifgrekey),
	DEF_CMD("enable_csum", GRE_ENABLE_CSUM,	setifgreopts),
	DEF_CMD("-enable_csum",-GRE_ENABLE_CSUM,setifgreopts),
	DEF_CMD("enable_seq", GRE_ENABLE_SEQ,	setifgreopts),
	DEF_CMD("-enable_seq",-GRE_ENABLE_SEQ,	setifgreopts),
};
static struct afswtch af_gre = {
	.af_name	= "af_gre",
	.af_af		= AF_UNSPEC,
	.af_other_status = gre_status,
};

static __constructor void
gre_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(gre_cmds);  i++)
		cmd_register(&gre_cmds[i]);
	af_register(&af_gre);
}
