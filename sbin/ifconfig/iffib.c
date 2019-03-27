/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Alexander V. Chernikov
 * Copyright (c) 2011 Christian S.J. Peron
 * Copyright (c) 2011 Bjoern A. Zeeb
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/route.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "ifconfig.h"

static void
fib_status(int s)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFIB, (caddr_t)&ifr) == 0 &&
	    ifr.ifr_fib != RT_DEFAULT_FIB)
		printf("\tfib: %u\n", ifr.ifr_fib);

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGTUNFIB, (caddr_t)&ifr) == 0 &&
	    ifr.ifr_fib != RT_DEFAULT_FIB)
		printf("\ttunnelfib: %u\n", ifr.ifr_fib);
}

static void
setiffib(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	unsigned long fib;
	char *ep;

	fib = strtoul(val, &ep, 0);
	if (*ep != '\0' || fib > UINT_MAX) {
		warn("fib %s not valid", val);
		return;
	}

	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_fib = fib;
	if (ioctl(s, SIOCSIFFIB, (caddr_t)&ifr) < 0)
		warn("ioctl (SIOCSIFFIB)");
}

static void
settunfib(const char *val, int dummy __unused, int s,
    const struct afswtch *afp)
{
	unsigned long fib;
	char *ep;

	fib = strtoul(val, &ep, 0);
	if (*ep != '\0' || fib > UINT_MAX) {
		warn("fib %s not valid", val);
		return;
	}

	strlcpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_fib = fib;
	if (ioctl(s, SIOCSTUNFIB, (caddr_t)&ifr) < 0)
		warn("ioctl (SIOCSTUNFIB)");
}

static struct cmd fib_cmds[] = {
	DEF_CMD_ARG("fib", setiffib),
	DEF_CMD_ARG("tunnelfib", settunfib),
};

static struct afswtch af_fib = {
	.af_name	= "af_fib",
	.af_af		= AF_UNSPEC,
	.af_other_status = fib_status,
};

static __constructor void
fib_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(fib_cmds);  i++)
		cmd_register(&fib_cmds[i]);
	af_register(&af_fib);
}
