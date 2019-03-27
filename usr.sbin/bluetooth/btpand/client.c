/*	$NetBSD: client.c,v 1.2 2008/12/06 20:01:14 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
__RCSID("$NetBSD: client.c,v 1.2 2008/12/06 20:01:14 plunky Exp $");

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <sdp.h>
#include <unistd.h>

#include "btpand.h"
#include "bnep.h"
#include "sdp.h"

static void client_query(void);

void
client_init(void)
{
	struct sockaddr_l2cap sa;
	channel_t *chan;
	socklen_t len;
	int fd, n;
	uint16_t mru, mtu;

	if (bdaddr_any(&remote_bdaddr))
		return;

	if (service_name)
		client_query();

	fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (fd == -1) {
		log_err("Could not open L2CAP socket: %m");
		exit(EXIT_FAILURE);
	}

	memset(&sa, 0, sizeof(sa));
	sa.l2cap_family = AF_BLUETOOTH;
	sa.l2cap_len = sizeof(sa);
	sa.l2cap_bdaddr_type = BDADDR_BREDR;
	sa.l2cap_cid = 0;
	 
	bdaddr_copy(&sa.l2cap_bdaddr, &local_bdaddr);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		log_err("Could not bind client socket: %m");
		exit(EXIT_FAILURE);
	}

	mru = BNEP_MTU_MIN;
	if (setsockopt(fd, SOL_L2CAP, SO_L2CAP_IMTU, &mru, sizeof(mru)) == -1) {
		log_err("Could not set L2CAP IMTU (%d): %m", mru);
		exit(EXIT_FAILURE);
	}

	log_info("Opening connection to service 0x%4.4x at %s",
	    service_class, bt_ntoa(&remote_bdaddr, NULL));

	sa.l2cap_psm = htole16(l2cap_psm);
	bdaddr_copy(&sa.l2cap_bdaddr, &remote_bdaddr);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		log_err("Could not connect: %m");
		exit(EXIT_FAILURE);
	}

	len = sizeof(mru);
	if (getsockopt(fd, SOL_L2CAP, SO_L2CAP_IMTU, &mru, &len) == -1) {
		log_err("Could not get IMTU: %m");
		exit(EXIT_FAILURE);
	}
	if (mru < BNEP_MTU_MIN) {
		log_err("L2CAP IMTU too small (%d)", mru);
		exit(EXIT_FAILURE);
	}

	len = sizeof(n);
	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, &len) == -1) {
		log_err("Could not read SO_RCVBUF");
		exit(EXIT_FAILURE);
	}
	if (n < (mru * 10)) {
		n = mru * 10;
		if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) == -1)
			log_info("Could not increase SO_RCVBUF (from %d)", n);
	}

	len = sizeof(mtu);
	if (getsockopt(fd, SOL_L2CAP, SO_L2CAP_OMTU, &mtu, &len) == -1) {
		log_err("Could not get L2CAP OMTU: %m");
		exit(EXIT_FAILURE);
	}
	if (mtu < BNEP_MTU_MIN) {
		log_err("L2CAP OMTU too small (%d)", mtu);
		exit(EXIT_FAILURE);
	}

	len = sizeof(n);
	if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &n, &len) == -1) {
		log_err("Could not get socket send buffer size: %m");
		close(fd);
		return;
	}
	if (n < (mtu * 2)) {
		n = mtu * 2;
		if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &n, sizeof(n)) == -1) {
			log_err("Could not set socket send buffer size (%d): %m", n);
			close(fd);
			return;
		}
	}
	n = mtu;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDLOWAT, &n, sizeof(n)) == -1) {
		log_err("Could not set socket low water mark (%d): %m", n);
		close(fd);
		return;
	}

	chan = channel_alloc();
	if (chan == NULL)
		exit(EXIT_FAILURE);

	chan->send = bnep_send;
	chan->recv = bnep_recv;
	chan->mru = mru;
	chan->mtu = mtu;
	b2eaddr(chan->raddr, &remote_bdaddr);
	b2eaddr(chan->laddr, &local_bdaddr);
	chan->state = CHANNEL_WAIT_CONNECT_RSP;
	channel_timeout(chan, 10);
	if (!channel_open(chan, fd))
		exit(EXIT_FAILURE);

	bnep_send_control(chan, BNEP_SETUP_CONNECTION_REQUEST,
	    2, service_class, SDP_SERVICE_CLASS_PANU);
}

static void
client_query(void)
{
	uint8_t buffer[512];
	sdp_attr_t attr;
	uint32_t range;
	void *ss;
	int rv;
	uint8_t *seq0, *seq1;

	attr.flags = SDP_ATTR_INVALID;
	attr.attr = 0;
	attr.vlen = sizeof(buffer);
	attr.value = buffer;

	range = SDP_ATTR_RANGE(SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
			       SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST);

	ss = sdp_open(&local_bdaddr, &remote_bdaddr);
	if (ss == NULL || (errno = sdp_error(ss)) != 0) {
		log_err("%s: %m", service_name);
		exit(EXIT_FAILURE);
	}

	log_info("Searching for %s service at %s",
	    service_name, bt_ntoa(&remote_bdaddr, NULL));

	rv = sdp_search(ss, 1, &service_class, 1, &range, 1, &attr);
	if (rv != 0) {
		log_err("%s: %s", service_name, strerror(sdp_error(ss)));
		exit(EXIT_FAILURE);
	}

	sdp_close(ss);

	if (attr.flags != SDP_ATTR_OK
	    || attr.attr != SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST) {
		log_err("%s service not found", service_name);
		exit(EXIT_FAILURE);
	}

	/*
	 * we expect the following protocol descriptor list
	 *
	 *	seq len
	 *	  seq len
	 *	    uuid value == L2CAP
	 *	    uint16 value16 => PSM
	 *	  seq len
	 *	    uuid value == BNEP
	 */
	if (_sdp_get_seq(&attr.value, attr.value + attr.vlen, &seq0)
	    && _sdp_get_seq(&seq0, attr.value, &seq1)
	    && _sdp_match_uuid16(&seq1, seq0, SDP_UUID_PROTOCOL_L2CAP)
	    && _sdp_get_uint16(&seq1, seq0, &l2cap_psm)
	    && _sdp_get_seq(&seq0, attr.value, &seq1)
	    && _sdp_match_uuid16(&seq1, seq0, SDP_UUID_PROTOCOL_BNEP)) {
		log_info("Found PSM %d for service %s", l2cap_psm, service_name);
		return;
	}

	log_err("%s query failed", service_name);
	exit(EXIT_FAILURE);
}
