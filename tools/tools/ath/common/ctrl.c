/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * This is a simple abstraction of the control channel used to access
 * device specific data.
 *
 * In the past it used a ifnet socket on athX, but since those devices
 * are now gone, they can use wlanX.  However, there are debug cases
 * where you'll instead want to talk to the hardware before any VAPs are
 * up, so we should also handle the case of talking to /dev/athX.
 *
 * For now this'll be a drop-in replacement for the existing ioctl()
 * based method until the /dev/athX (and associated new ioctls) land
 * in the tree.
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/sockio.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ah.h"
#include "ah_desc.h"
#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_radiotap.h"
#include "if_athioctl.h"
#include "if_athrate.h"

#include "ctrl.h"

int
ath_driver_req_init(struct ath_driver_req *req)
{

	bzero(req, sizeof(*req));
	req->s = -1;
	return (0);
}

/*
 * Open a suitable file descriptor and populate the relevant interface
 * information for ioctls.
 *
 * For file path based access the ifreq isn't required; it'll just be
 * a direct ioctl on the file descriptor.
 */
int
ath_driver_req_open(struct ath_driver_req *req, const char *ifname)
{
	int s;

	if (s != -1)
		ath_driver_req_close(req);

	/* For now, netif socket, not /dev/ filedescriptor */
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		warn("%s: socket", __func__);
		return (-1);
	}
	req->ifname = strdup(ifname);
	req->s = s;

	return (0);
}

/*
 * Close an open descriptor.
 */
int
ath_driver_req_close(struct ath_driver_req *req)
{
	if (req->s == -1)
		return (0);
	close(req->s);
	free(req->ifname);
	req->s = -1;
	req->ifname = NULL;
	return (0);
}

/*
 * Issue a diagnostic API request.
 */
int
ath_driver_req_fetch_diag(struct ath_driver_req *req, unsigned long cmd,
    struct ath_diag *ad)
{
	int ret;

	ret = ioctl(req->s, cmd, ad);
	if (ret < 0)
		warn("%s: ioctl", __func__);
	return (ret);
}

/*
 * Issue a zero statistics API request.
 */
int
ath_driver_req_zero_stats(struct ath_driver_req *req)
{
	struct ifreq ifr;
	int ret;

	/* Setup ifreq */
	bzero(&ifr, sizeof(ifr));
	strncpy(ifr.ifr_name, req->ifname, sizeof (ifr.ifr_name));
	ifr.ifr_data = NULL;

	/* ioctl */
	ret = ioctl(req->s, SIOCZATHSTATS, &ifr);
	if (ret < 0)
		warn("%s: ioctl", __func__);
	return (ret);
}

/*
 * Fetch general statistics.
 */
int
ath_driver_req_fetch_stats(struct ath_driver_req *req, struct ath_stats *st)
{
	struct ifreq ifr;
	int ret;

	/* Setup ifreq */
	bzero(&ifr, sizeof(ifr));
	strncpy(ifr.ifr_name, req->ifname, sizeof (ifr.ifr_name));
	ifr.ifr_data = (caddr_t) st;

	/* ioctl */
	ret = ioctl(req->s, SIOCGATHSTATS, &ifr);
	if (ret < 0)
		warn("%s: ioctl", __func__);
	return (ret);
}

/*
 * Fetch aggregate statistics.
 */
int
ath_drive_req_fetch_aggr_stats(struct ath_driver_req *req,
    struct ath_tx_aggr_stats *tx)
{
	struct ifreq ifr;
	int ret;

	/* Setup ifreq */
	bzero(&ifr, sizeof(ifr));
	strncpy(ifr.ifr_name, req->ifname, sizeof (ifr.ifr_name));
	ifr.ifr_data = (caddr_t) tx;

	/* ioctl */
	ret = ioctl(req->s, SIOCGATHAGSTATS, &ifr);
	if (ret < 0)
		warn("%s: ioctl", __func__);
	return (ret);

}

/*
 * Fetch rate control statistics.
 *
 * Caller has to populate the interface name and MAC address.
 */
int
ath_drive_req_fetch_ratectrl_stats(struct ath_driver_req *req,
    struct ath_rateioctl *r)
{
	int ret;

	/* ioctl */
	ret = ioctl(req->s, SIOCGATHNODERATESTATS, r);
	if (ret < 0)
		warn("%s: ioctl", __func__);
	return (ret);
}
