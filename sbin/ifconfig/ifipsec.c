/*-
 * Copyright (c) 2016 Yandex LLC
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stdint.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_ipsec.h>
#include <net/route.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static void
ipsec_status(int s)
{
	uint32_t reqid;

	ifr.ifr_data = (caddr_t)&reqid;
	if (ioctl(s, IPSECGREQID, &ifr) == -1)
		return;
	printf("\treqid: %u\n", reqid);
}

static
DECL_CMD_FUNC(setreqid, val, arg)
{
	char *ep;
	uint32_t v;

	v = strtoul(val, &ep, 0);
	if (*ep != '\0') {
		warn("Invalid reqid value %s", val);
		return;
	}
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (char *)&v;
	if (ioctl(s, IPSECSREQID, &ifr) == -1) {
		warn("ioctl(IPSECSREQID)");
		return;
	}
}

static struct cmd ipsec_cmds[] = {
	DEF_CMD_ARG("reqid",		setreqid),
};

static struct afswtch af_ipsec = {
	.af_name	= "af_ipsec",
	.af_af		= AF_UNSPEC,
	.af_other_status = ipsec_status,
};

static __constructor void
ipsec_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(ipsec_cmds); i++)
		cmd_register(&ipsec_cmds[i]);
	af_register(&af_ipsec);
#undef N
}
