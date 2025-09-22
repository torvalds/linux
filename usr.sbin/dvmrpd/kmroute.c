/*	$OpenBSD: kmroute.c,v 1.3 2019/06/28 13:32:47 deraadt Exp $ */

/*
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip_mroute.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "dvmrpe.h"
#include "log.h"

extern struct dvmrpd_conf	*conf;
char				*mroute_ptr;	/* packet buffer */

void	main_imsg_compose_rde(int, pid_t, void *, u_int16_t);

int
kmr_init(int fd)
{
	struct iface		*iface;
	struct route_report	 rr;

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		log_debug("kmr_init: interface %s", iface->name);

		rr.net.s_addr = iface->addr.s_addr & iface->mask.s_addr;
		rr.mask = iface->mask;
		rr.nexthop.s_addr = 0;
		rr.metric = iface->metric;
		rr.ifindex = iface->ifindex;
		main_imsg_compose_rde(IMSG_ROUTE_REPORT, -1, &rr, sizeof(rr));

		mrt_add_vif(conf->mroute_socket, iface);
	}

	if ((mroute_ptr = calloc(1, IBUF_READ_SIZE)) == NULL)
		fatal("kmr_init");

	return (0);
}

void
kmr_shutdown(void)
{
	struct iface		*iface;

	kmr_mfc_decouple();
	kmroute_clear();

	LIST_FOREACH(iface, &conf->iface_list, entry) {
		log_debug("kmr_shutdown: interface %s", iface->name);

		mrt_del_vif(conf->mroute_socket, iface);
	}

	free(mroute_ptr);
}

void
kmr_recv_msg(int fd, short event, void *bula)
{
	struct mfc		 mfc;
	struct igmpmsg		 kernel_msg;
	char			*buf;
	ssize_t			 r;

	if (event != EV_READ)
		return;

	/* setup buffer */
	buf = mroute_ptr;

	if ((r = recvfrom(fd, buf, IBUF_READ_SIZE, 0, NULL, NULL)) == -1) {
		if (errno != EAGAIN && errno != EINTR)
			log_debug("kmr_recv_msg: error receiving packet");
		return;
	}

	memcpy(&kernel_msg, buf, sizeof(kernel_msg));

	/* we are only interested in kernel messages */
	if (kernel_msg.im_mbz != 0)
		return;

	switch (kernel_msg.im_msgtype) {
	case IGMPMSG_NOCACHE:
		/* verify that dst is a multicast group */
		if (!IN_MULTICAST(ntohl(kernel_msg.im_dst.s_addr))) {
			log_debug("kmr_recv_msg: kernel providing garbage!");
			return;
		}

		/* send MFC entry to RDE */
		mfc.origin = kernel_msg.im_src;
		mfc.group = kernel_msg.im_dst;
		mfc.ifindex = kernel_msg.im_vif;
		main_imsg_compose_rde(IMSG_MFC_ADD, 0, &mfc, sizeof(mfc));
		break;
	case IGMPMSG_WRONGVIF:
	case IGMPMSG_WHOLEPKT:
	case IGMPMSG_BW_UPCALL:
	default:
		log_debug("kmr_recv_msg: unhandled msg type %d!",
		    kernel_msg.im_msgtype);
	}
}

void
kmr_mfc_couple(void)
{
	log_info("kernel multicast forwarding cache coupled");
}

void
kmr_mfc_decouple(void)
{
	log_info("kernel multicast forwarding cache decoupled");
}

void
kmroute_clear(void)
{

}

int
mrt_init(int fd)
{
	int	flag = 1;

	if (setsockopt(fd, IPPROTO_IP, MRT_INIT, &flag,
	    sizeof(flag)) == -1) {
		log_warn("mrt_init: error setting MRT_INIT");
		return (-1);
	}

	return (0);
}

int
mrt_done(int fd)
{
	int	flag = 0;

	if (setsockopt(fd, IPPROTO_IP, MRT_DONE, &flag,
	    sizeof(flag)) == -1) {
		log_warn("mrt_done: error setting MRT_DONE");
		return (-1);
	}

	return (0);
}

int
mrt_add_vif(int fd, struct iface *iface)
{
	struct vifctl	vc;

	vc.vifc_vifi            = iface->ifindex;
	vc.vifc_flags           = 0;
	vc.vifc_threshold       = 1;
	vc.vifc_rate_limit	= 0;
	vc.vifc_lcl_addr.s_addr = iface->addr.s_addr;
	vc.vifc_rmt_addr.s_addr = 0;

	if (setsockopt(fd, IPPROTO_IP, MRT_ADD_VIF, &vc,
	    sizeof(vc)) == -1) {
		log_warn("mrt_add_vif: error adding VIF");
		return (-1);
	}

	return (0);
}

void
mrt_del_vif(int fd, struct iface *iface)
{
	vifi_t	 vifi;

	vifi = iface->ifindex;

	if (setsockopt(fd, IPPROTO_IP, MRT_DEL_VIF, &vifi,
	    sizeof(vifi)) == -1)
		log_warn("mrt_del_vif: error deleting VIF");
}

int
mrt_add_mfc(int fd, struct mfc *mfc)
{
	struct mfcctl	 mc;
	int		 i;

	log_debug("mrt_add_mfc: interface %d, group %s", mfc->ifindex,
	    inet_ntoa(mfc->group));

	mc.mfcc_origin = mfc->origin;
	mc.mfcc_mcastgrp = mfc->group;
	mc.mfcc_parent = mfc->ifindex;

	for (i = 0; i < MAXVIFS; i++) {
		mc.mfcc_ttls[i] = mfc->ttls[i];
	}

	if (setsockopt(fd, IPPROTO_IP, MRT_ADD_MFC, &mc, sizeof(mc))
	    == -1) {
		log_warnx("mrt_add_mfc: error adding group %s to interface %d",
		    inet_ntoa(mfc->group), mfc->ifindex);
		return (-1);
	}

	return (0);
}

int
mrt_del_mfc(int fd, struct mfc *mfc)
{
	struct mfcctl	 mc;

	log_debug("mrt_del_mfc: group %s", inet_ntoa(mfc->group));

	mc.mfcc_origin = mfc->origin;
	mc.mfcc_mcastgrp = mfc->group;

	if (setsockopt(fd, IPPROTO_IP, MRT_DEL_MFC, &mc, sizeof(mc))
	    == -1) {
		log_warnx("mrt_del_mfc: error deleting group %s ",
		    inet_ntoa(mfc->group));
		return (-1);
	}

	return (0);
}
