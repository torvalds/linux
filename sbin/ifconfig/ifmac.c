/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by NAI Labs, the
 * Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
#include <sys/mac.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/route.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ifconfig.h"

static void
maclabel_status(int s)
{
	struct ifreq ifr;
	mac_t label;
	char *label_text;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (mac_prepare_ifnet_label(&label) == -1)
		return;
	ifr.ifr_ifru.ifru_data = (void *)label;
	if (ioctl(s, SIOCGIFMAC, &ifr) == -1)
		goto mac_free;

	
	if (mac_to_text(label, &label_text) == -1)
		goto mac_free;

	if (strlen(label_text) != 0)
		printf("\tmaclabel %s\n", label_text);
	free(label_text);

mac_free:
	mac_free(label);
}

static void
setifmaclabel(const char *val, int d, int s, const struct afswtch *rafp)
{
	struct ifreq ifr;
	mac_t label;
	int error;

	if (mac_from_text(&label, val) == -1) {
		perror(val);
		return;
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_ifru.ifru_data = (void *)label;

	error = ioctl(s, SIOCSIFMAC, &ifr);
	mac_free(label);
	if (error == -1)
		perror("setifmac");
}

static struct cmd mac_cmds[] = {
	DEF_CMD_ARG("maclabel",	setifmaclabel),
};
static struct afswtch af_mac = {
	.af_name	= "af_maclabel",
	.af_af		= AF_UNSPEC,
	.af_other_status = maclabel_status,
};

static __constructor void
mac_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(mac_cmds);  i++)
		cmd_register(&mac_cmds[i]);
	af_register(&af_mac);
}
