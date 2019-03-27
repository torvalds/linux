/*
 * Copyright (c) 1983, 1993
 *  The Regents of the University of California.  All rights reserved.
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
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/ioctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_lagg.h>
#include <net/ieee8023ad_lacp.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "libifconfig.h"
#include "libifconfig_internal.h"

/* Internal structure used for allocations and frees */
struct _ifconfig_lagg_status {
	struct ifconfig_lagg_status l;  /* Must be first */
	struct lagg_reqall ra;
	struct lagg_reqopts ro;
	struct lagg_reqflags rf;
	struct lagg_reqport rpbuf[LAGG_MAX_PORTS];
};

int
ifconfig_lagg_get_laggport_status(ifconfig_handle_t *h,
    const char *name, struct lagg_reqport *rp)
{
	strlcpy(rp->rp_ifname, name, sizeof(rp->rp_portname));
	strlcpy(rp->rp_portname, name, sizeof(rp->rp_portname));

	return (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGLAGGPORT, rp));
}

int
ifconfig_lagg_get_lagg_status(ifconfig_handle_t *h,
    const char *name, struct ifconfig_lagg_status **lagg_status)
{
	struct _ifconfig_lagg_status *ls;

	ls = calloc(1, sizeof(struct _ifconfig_lagg_status));
	if (ls == NULL) {
		h->error.errtype = OTHER;
		h->error.errcode = ENOMEM;
		return (-1);
	}
	ls->l.ra = &ls->ra;
	ls->l.ro = &ls->ro;
	ls->l.rf = &ls->rf;
	*lagg_status = &ls->l;

	ls->ra.ra_port = ls->rpbuf;
	ls->ra.ra_size = sizeof(ls->rpbuf);

	strlcpy(ls->ro.ro_ifname, name, sizeof(ls->ro.ro_ifname));
	ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGLAGGOPTS, &ls->ro);

	strlcpy(ls->rf.rf_ifname, name, sizeof(ls->rf.rf_ifname));
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGLAGGFLAGS, &ls->rf) != 0) {
		ls->rf.rf_flags = 0;
	}

	strlcpy(ls->ra.ra_ifname, name, sizeof(ls->ra.ra_ifname));
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGLAGG, &ls->ra) != 0) {
		free(ls);
		return (-1);
	}

	return (0);
}

void
ifconfig_lagg_free_lagg_status(struct ifconfig_lagg_status *laggstat)
{
	free(laggstat);
}
