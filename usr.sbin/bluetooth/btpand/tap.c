/*	$NetBSD: tap.c,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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

/* $FreeBSD$ */

#include <sys/cdefs.h>
__RCSID("$NetBSD: tap.c,v 1.1 2008/08/17 13:20:57 plunky Exp $");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <net/if_tap.h>

#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <stdio.h>
#include <unistd.h>

#define L2CAP_SOCKET_CHECKED
#include "btpand.h"

static bool tap_send(channel_t *, packet_t *);
static bool tap_recv(packet_t *);

void
tap_init(void)
{
	channel_t *chan;
	struct ifreq ifr;
	int fd, s;
	char pidfile[PATH_MAX];

	fd = open(interface_name, O_RDWR);
	if (fd == -1) {
		log_err("Could not open \"%s\": %m", interface_name);
		exit(EXIT_FAILURE);
	}

	memset(&ifr, 0, sizeof(ifr));
	if (ioctl(fd, TAPGIFNAME, &ifr) == -1) {
		log_err("Could not get interface name: %m");
		exit(EXIT_FAILURE);
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		log_err("Could not open PF_LINK socket: %m");
		exit(EXIT_FAILURE);
	}

	ifr.ifr_addr.sa_family = AF_LINK;
	ifr.ifr_addr.sa_len = ETHER_ADDR_LEN;
	b2eaddr(ifr.ifr_addr.sa_data, &local_bdaddr);

	if (ioctl(s, SIOCSIFLLADDR, &ifr) == -1) {
		log_err("Could not set %s physical address: %m", ifr.ifr_name);
		exit(EXIT_FAILURE);
	}

	if (ioctl(s, SIOCGIFFLAGS, &ifr) == -1) {
		log_err("Could not get interface flags: %m");
		exit(EXIT_FAILURE);
	}

	if ((ifr.ifr_flags & IFF_UP) == 0) {
		ifr.ifr_flags |= IFF_UP;

		if (ioctl(s, SIOCSIFFLAGS, &ifr) == -1) {
			log_err("Could not set IFF_UP: %m");
			exit(EXIT_FAILURE);
		}
	}

	close(s);

	log_info("Using interface %s with addr %s", ifr.ifr_name,
		ether_ntoa((struct ether_addr *)&ifr.ifr_addr.sa_data));

	chan = channel_alloc();
	if (chan == NULL)
		exit(EXIT_FAILURE);

	chan->send = tap_send;
	chan->recv = tap_recv;
	chan->mru = ETHER_HDR_LEN + ETHER_MAX_LEN;
	memcpy(chan->raddr, ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
	memcpy(chan->laddr, ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
	chan->state = CHANNEL_OPEN;
	if (!channel_open(chan, fd))
		exit(EXIT_FAILURE);

	snprintf(pidfile, sizeof(pidfile), "%s/%s.pid",
		_PATH_VARRUN, ifr.ifr_name);
	chan->pfh = pidfile_open(pidfile, 0600, NULL);
	if (chan->pfh == NULL)
		log_err("can't create pidfile");
	else if (pidfile_write(chan->pfh) < 0) {
		log_err("can't write pidfile");
		pidfile_remove(chan->pfh);
		chan->pfh = NULL;
	}
}

static bool
tap_send(channel_t *chan, packet_t *pkt)
{
	struct iovec iov[4];
	ssize_t nw;

	iov[0].iov_base = pkt->dst;
	iov[0].iov_len = ETHER_ADDR_LEN;
	iov[1].iov_base = pkt->src;
	iov[1].iov_len = ETHER_ADDR_LEN;
	iov[2].iov_base = pkt->type;
	iov[2].iov_len = ETHER_TYPE_LEN;
	iov[3].iov_base = pkt->ptr;
	iov[3].iov_len = pkt->len;

	/* tap device write never fails */
	nw = writev(chan->fd, iov, __arraycount(iov));
	assert(nw > 0);

	return true;
}

static bool
tap_recv(packet_t *pkt)
{

	if (pkt->len < ETHER_HDR_LEN)
		return false;

	pkt->dst = pkt->ptr;
	packet_adj(pkt, ETHER_ADDR_LEN);
	pkt->src = pkt->ptr;
	packet_adj(pkt, ETHER_ADDR_LEN);
	pkt->type = pkt->ptr;
	packet_adj(pkt, ETHER_TYPE_LEN);

	return true;
}
