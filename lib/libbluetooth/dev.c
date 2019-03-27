/*
 * dev.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <stdio.h>
#include <string.h>

struct bt_devaddr_match_arg
{
	char		devname[HCI_DEVNAME_SIZE];
	bdaddr_t const	*bdaddr;
};

static bt_devenum_cb_t	bt_devaddr_match;

int
bt_devaddr(char const *devname, bdaddr_t *addr)
{
	struct bt_devinfo	di;

	strlcpy(di.devname, devname, sizeof(di.devname));

	if (bt_devinfo(&di) < 0)
		return (0);

	if (addr != NULL)
		bdaddr_copy(addr, &di.bdaddr);

	return (1);
}

int
bt_devname(char *devname, bdaddr_t const *addr)
{
	struct bt_devaddr_match_arg	arg;

	memset(&arg, 0, sizeof(arg));
	arg.bdaddr = addr;

	if (bt_devenum(&bt_devaddr_match, &arg) < 0)
		return (0);
	
	if (arg.devname[0] == '\0') {
		errno = ENXIO;
		return (0);
	}

	if (devname != NULL)
		strlcpy(devname, arg.devname, HCI_DEVNAME_SIZE);

	return (1);
}

static int
bt_devaddr_match(int s, struct bt_devinfo const *di, void *arg)
{
	struct bt_devaddr_match_arg	*m = (struct bt_devaddr_match_arg *)arg;

	if (!bdaddr_same(&di->bdaddr, m->bdaddr))
		return (0);

	strlcpy(m->devname, di->devname, sizeof(m->devname));

	return (1);
}

