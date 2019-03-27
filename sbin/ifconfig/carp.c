/*	$FreeBSD$ */
/*	from $OpenBSD: ifconfig.c,v 1.82 2003/10/19 05:43:35 mcbride Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Michael Shalayeff. All rights reserved.
 * Copyright (c) 2003 Ryan McBride. All rights reserved.
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip_carp.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static const char *carp_states[] = { CARP_STATES };

static void carp_status(int s);
static void setcarp_vhid(const char *, int, int, const struct afswtch *rafp);
static void setcarp_callback(int, void *);
static void setcarp_advbase(const char *,int, int, const struct afswtch *rafp);
static void setcarp_advskew(const char *, int, int, const struct afswtch *rafp);
static void setcarp_passwd(const char *, int, int, const struct afswtch *rafp);

static int carpr_vhid = -1;
static int carpr_advskew = -1;
static int carpr_advbase = -1;
static int carpr_state = -1;
static unsigned char const *carpr_key;

static void
carp_status(int s)
{
	struct carpreq carpr[CARP_MAXVHID];
	int i;

	bzero(carpr, sizeof(struct carpreq) * CARP_MAXVHID);
	carpr[0].carpr_count = CARP_MAXVHID;
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		return;

	for (i = 0; i < carpr[0].carpr_count; i++) {
		printf("\tcarp: %s vhid %d advbase %d advskew %d",
		    carp_states[carpr[i].carpr_state], carpr[i].carpr_vhid,
		    carpr[i].carpr_advbase, carpr[i].carpr_advskew);
		if (printkeys && carpr[i].carpr_key[0] != '\0')
			printf(" key \"%s\"\n", carpr[i].carpr_key);
		else
			printf("\n");
	}
}

static void
setcarp_vhid(const char *val, int d, int s, const struct afswtch *afp)
{

	carpr_vhid = atoi(val);

	if (carpr_vhid <= 0 || carpr_vhid > CARP_MAXVHID)
		errx(1, "vhid must be greater than 0 and less than %u",
		    CARP_MAXVHID);

	switch (afp->af_af) {
#ifdef INET
	case AF_INET:
	    {
		struct in_aliasreq *ifra;

		ifra = (struct in_aliasreq *)afp->af_addreq;
		ifra->ifra_vhid = carpr_vhid;
		break;
	    }
#endif
#ifdef INET6
	case AF_INET6:
	    {
		struct in6_aliasreq *ifra;

		ifra = (struct in6_aliasreq *)afp->af_addreq;
		ifra->ifra_vhid = carpr_vhid;
		break;
	    }
#endif
	default:
		errx(1, "%s doesn't support carp(4)", afp->af_name);
	}

	callback_register(setcarp_callback, NULL);
}

static void
setcarp_callback(int s, void *arg __unused)
{
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	carpr.carpr_vhid = carpr_vhid;
	carpr.carpr_count = 1;
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1 && errno != ENOENT)
		err(1, "SIOCGVH");

	if (carpr_key != NULL)
		/* XXX Should hash the password into the key here? */
		strlcpy(carpr.carpr_key, carpr_key, CARP_KEY_LEN);
	if (carpr_advskew > -1)
		carpr.carpr_advskew = carpr_advskew;
	if (carpr_advbase > -1)
		carpr.carpr_advbase = carpr_advbase;
	if (carpr_state > -1)
		carpr.carpr_state = carpr_state;

	if (ioctl(s, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCSVH");
}

static void
setcarp_passwd(const char *val, int d, int s, const struct afswtch *afp)
{

	if (carpr_vhid == -1)
		errx(1, "passwd requires vhid");

	carpr_key = val;
}

static void
setcarp_advskew(const char *val, int d, int s, const struct afswtch *afp)
{

	if (carpr_vhid == -1)
		errx(1, "advskew requires vhid");

	carpr_advskew = atoi(val);
}

static void
setcarp_advbase(const char *val, int d, int s, const struct afswtch *afp)
{

	if (carpr_vhid == -1)
		errx(1, "advbase requires vhid");

	carpr_advbase = atoi(val);
}

static void
setcarp_state(const char *val, int d, int s, const struct afswtch *afp)
{
	int i;

	if (carpr_vhid == -1)
		errx(1, "state requires vhid");

	for (i = 0; i <= CARP_MAXSTATE; i++)
		if (strcasecmp(carp_states[i], val) == 0) {
			carpr_state = i;
			return;
		}

	errx(1, "unknown state");
}

static struct cmd carp_cmds[] = {
	DEF_CMD_ARG("advbase",	setcarp_advbase),
	DEF_CMD_ARG("advskew",	setcarp_advskew),
	DEF_CMD_ARG("pass",	setcarp_passwd),
	DEF_CMD_ARG("vhid",	setcarp_vhid),
	DEF_CMD_ARG("state",	setcarp_state),
};
static struct afswtch af_carp = {
	.af_name	= "af_carp",
	.af_af		= AF_UNSPEC,
	.af_other_status = carp_status,
};

static __constructor void
carp_ctor(void)
{
	int i;

	for (i = 0; i < nitems(carp_cmds);  i++)
		cmd_register(&carp_cmds[i]);
	af_register(&af_carp);
}
