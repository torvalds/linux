/*
 * client.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: client.c,v 1.7 2006/09/07 21:06:53 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <assert.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <usbhid.h>
#include "bthid_config.h"
#include "bthidd.h"

static int32_t	client_socket(bdaddr_p bdaddr, uint16_t psm);

/*
 * Get next config entry and create outbound connection (if required)
 *
 * XXX	Do only one device at a time. At least one of my devices (3COM
 *	Bluetooth PCCARD) rejects Create_Connection command if another
 *	Create_Connection command is still pending. Weird...
 */

static int32_t	connect_in_progress = 0;

int32_t
client_rescan(bthid_server_p srv)
{
	static hid_device_p	d;
	bthid_session_p		s;

	assert(srv != NULL);

	if (connect_in_progress)
		return (0); /* another connect is still pending */ 

	d = get_next_hid_device(d);
	if (d == NULL)
		return (0); /* XXX should not happen? empty config? */

	if ((s = session_by_bdaddr(srv, &d->bdaddr)) != NULL)
		return (0); /* session already active */

	if (!d->new_device) {
		if (d->reconnect_initiate)
			return (0); /* device will initiate reconnect */
	}

	syslog(LOG_NOTICE, "Opening outbound session for %s " \
		"(new_device=%d, reconnect_initiate=%d)",
		bt_ntoa(&d->bdaddr, NULL), d->new_device, d->reconnect_initiate);

	if ((s = session_open(srv, d)) == NULL) {
		syslog(LOG_CRIT, "Could not create outbound session for %s",
			bt_ntoa(&d->bdaddr, NULL));
		return (-1);
	}

	/* Open control channel */
	s->ctrl = client_socket(&s->bdaddr, d->control_psm);
	if (s->ctrl < 0) {
		syslog(LOG_ERR, "Could not open control channel to %s. %s (%d)",
			bt_ntoa(&s->bdaddr, NULL), strerror(errno), errno);
		session_close(s);
		return (-1);
	}

	s->state = W4CTRL;

	FD_SET(s->ctrl, &srv->wfdset);
	if (s->ctrl > srv->maxfd)
		srv->maxfd = s->ctrl;

	connect_in_progress = 1;

	return (0);
}

/*
 * Process connect on the socket
 */

int32_t
client_connect(bthid_server_p srv, int32_t fd)
{
	bthid_session_p	s;
	hid_device_p	d;
	int32_t		error;
	socklen_t	len;

	assert(srv != NULL);
	assert(fd >= 0);

	s = session_by_fd(srv, fd);
	assert(s != NULL);

	d = get_hid_device(&s->bdaddr);
	assert(d != NULL);

	error = 0;
	len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
		syslog(LOG_ERR, "Could not get socket error for %s. %s (%d)",
			bt_ntoa(&s->bdaddr, NULL), strerror(errno), errno);
		session_close(s);
		connect_in_progress = 0;

		return (-1);
	}

	if (error != 0) {
		syslog(LOG_ERR, "Could not connect to %s. %s (%d)",
			bt_ntoa(&s->bdaddr, NULL), strerror(error), error);
		session_close(s);
		connect_in_progress = 0;

		return (0);
	}

	switch (s->state) {
	case W4CTRL: /* Control channel is open */
		assert(s->ctrl == fd);
		assert(s->intr == -1);

		/* Open interrupt channel */
		s->intr = client_socket(&s->bdaddr, d->interrupt_psm);
		if (s->intr < 0) { 
			syslog(LOG_ERR, "Could not open interrupt channel " \
				"to %s. %s (%d)", bt_ntoa(&s->bdaddr, NULL),
				strerror(errno), errno);
			session_close(s);
			connect_in_progress = 0;

			return (-1);
		}

		s->state = W4INTR;

		FD_SET(s->intr, &srv->wfdset);
		if (s->intr > srv->maxfd)
			srv->maxfd = s->intr;

		d->new_device = 0; /* reset new device flag */
		write_hids_file();
		break;

	case W4INTR: /* Interrupt channel is open */
		assert(s->ctrl != -1);
		assert(s->intr == fd);

		s->state = OPEN;
		connect_in_progress = 0;

		/* Create kbd/mouse after both channels are established */
		if (session_run(s) < 0) {
			session_close(s);
			return (-1);
		}
		break;

	default:
		assert(0);
		break;
	}

	/* Move fd to from the write fd set into read fd set */
	FD_CLR(fd, &srv->wfdset);
	FD_SET(fd, &srv->rfdset);

	return (0);
}

/*
 * Create bound non-blocking socket and initiate connect
 */

static int
client_socket(bdaddr_p bdaddr, uint16_t psm)
{
	struct sockaddr_l2cap	l2addr;
	int32_t			s, m;

	s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (s < 0)
		return (-1);

	m = fcntl(s, F_GETFL);
	if (m < 0) {
		close(s);
		return (-1);
	}

	if (fcntl(s, F_SETFL, (m|O_NONBLOCK)) < 0) {
		close(s);
		return (-1);
	}

	l2addr.l2cap_len = sizeof(l2addr);
	l2addr.l2cap_family = AF_BLUETOOTH;
	memset(&l2addr.l2cap_bdaddr, 0, sizeof(l2addr.l2cap_bdaddr));
	l2addr.l2cap_psm = 0;
	l2addr.l2cap_bdaddr_type = BDADDR_BREDR;
	l2addr.l2cap_cid = 0;
	
	if (bind(s, (struct sockaddr *) &l2addr, sizeof(l2addr)) < 0) {
		close(s);
		return (-1);
	}

	memcpy(&l2addr.l2cap_bdaddr, bdaddr, sizeof(l2addr.l2cap_bdaddr));
	l2addr.l2cap_psm = htole16(psm);

	if (connect(s, (struct sockaddr *) &l2addr, sizeof(l2addr)) < 0 &&
	    errno != EINPROGRESS) {
		close(s);
		return (-1);
	}

	return (s);
}

