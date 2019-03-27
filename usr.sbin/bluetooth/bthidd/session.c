/*
 * session.c
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
 * $Id: session.c,v 1.3 2006/09/07 21:06:53 max Exp $
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
#include "btuinput.h"
#include "kbd.h"

/*
 * Create new session
 */

bthid_session_p
session_open(bthid_server_p srv, hid_device_p const d)
{
	bthid_session_p	s;

	assert(srv != NULL);
	assert(d != NULL);

	if ((s = (bthid_session_p) malloc(sizeof(*s))) == NULL)
		return (NULL);

	s->srv = srv;  
	memcpy(&s->bdaddr, &d->bdaddr, sizeof(s->bdaddr));
	s->ctrl = -1;
	s->intr = -1;
	s->vkbd = -1;
	s->ctx = NULL;
	s->state = CLOSED;
	s->ukbd = -1;
	s->umouse = -1;
	s->obutt = 0;

	s->keys1 = bit_alloc(kbd_maxkey());
	if (s->keys1 == NULL) {
		free(s);
		return (NULL);
	}

	s->keys2 = bit_alloc(kbd_maxkey());
	if (s->keys2 == NULL) {
		free(s->keys1);
		free(s);
		return (NULL);
	}

	LIST_INSERT_HEAD(&srv->sessions, s, next);

	return (s);
}

/*
 * Initialize virtual keyboard and mouse after both channels are established
 */

int32_t
session_run(bthid_session_p s)
{
	hid_device_p d = get_hid_device(&s->bdaddr);
	struct sockaddr_l2cap   local;
	socklen_t               len;

	if (d->keyboard) {
		/* Open /dev/vkbdctl */
		s->vkbd = open("/dev/vkbdctl", O_RDWR);
		if (s->vkbd < 0) {
			syslog(LOG_ERR, "Could not open /dev/vkbdctl " \
				"for %s. %s (%d)", bt_ntoa(&s->bdaddr, NULL),
				strerror(errno), errno);
			return (-1);
		}
		/* Register session's vkbd descriptor (if needed) for read */
		FD_SET(s->vkbd, &s->srv->rfdset);
		if (s->vkbd > s->srv->maxfd)
			s->srv->maxfd = s->vkbd;
	}

	/* Pass device for probing */
	hid_initialise(s);

	/* Take local bdaddr */
	len = sizeof(local);
	getsockname(s->ctrl, (struct sockaddr *) &local, &len);

	if (d->mouse && s->srv->uinput) {
		s->umouse = uinput_open_mouse(d, &local.l2cap_bdaddr);
		if (s->umouse < 0) {
			syslog(LOG_ERR, "Could not open /dev/uinput " \
				"for %s. %s (%d)", bt_ntoa(&s->bdaddr,
				NULL), strerror(errno), errno);
			return (-1);
		}
	}
	if (d->keyboard && s->srv->uinput) {
		s->ukbd = uinput_open_keyboard(d, &local.l2cap_bdaddr);
		if (s->ukbd < 0) {
			syslog(LOG_ERR, "Could not open /dev/uinput " \
				"for %s. %s (%d)", bt_ntoa(&s->bdaddr,
				NULL), strerror(errno), errno);
			return (-1);
		}
		/* Register session's ukbd descriptor (if needed) for read */
		FD_SET(s->ukbd, &s->srv->rfdset);
		if (s->ukbd > s->srv->maxfd)
			s->srv->maxfd = s->ukbd;
	}
	return (0);
}

/*
 * Lookup session by bdaddr
 */

bthid_session_p
session_by_bdaddr(bthid_server_p srv, bdaddr_p bdaddr)
{
	bthid_session_p	s;

	assert(srv != NULL);
	assert(bdaddr != NULL);

	LIST_FOREACH(s, &srv->sessions, next)
		if (memcmp(&s->bdaddr, bdaddr, sizeof(s->bdaddr)) == 0)
			break;

	return (s);
}

/*
 * Lookup session by fd
 */

bthid_session_p
session_by_fd(bthid_server_p srv, int32_t fd)
{
	bthid_session_p	s;

	assert(srv != NULL);
	assert(fd >= 0);

	LIST_FOREACH(s, &srv->sessions, next)
		if (s->ctrl == fd || s->intr == fd ||
		    s->vkbd == fd || s->ukbd == fd)
			break;

	return (s);
}

/*
 * Close session
 */

void
session_close(bthid_session_p s)
{
	assert(s != NULL);
	assert(s->srv != NULL);

	LIST_REMOVE(s, next);

	if (s->intr != -1) {
		FD_CLR(s->intr, &s->srv->rfdset);
		FD_CLR(s->intr, &s->srv->wfdset);
		close(s->intr);

		if (s->srv->maxfd == s->intr)
			s->srv->maxfd --;
	}

	if (s->ctrl != -1) {
		FD_CLR(s->ctrl, &s->srv->rfdset);
		FD_CLR(s->ctrl, &s->srv->wfdset);
		close(s->ctrl);

		if (s->srv->maxfd == s->ctrl)
			s->srv->maxfd --;
	}

	if (s->vkbd != -1) {
		FD_CLR(s->vkbd, &s->srv->rfdset);
		close(s->vkbd);

		if (s->srv->maxfd == s->vkbd)
			s->srv->maxfd --;
	}

	if (s->umouse != -1)
		close(s->umouse);

	if (s->ukbd != -1) {
		FD_CLR(s->ukbd, &s->srv->rfdset);
		close(s->ukbd);

		if (s->srv->maxfd == s->ukbd)
			s->srv->maxfd --;
	}

	free(s->ctx);
	free(s->keys1);
	free(s->keys2);

	memset(s, 0, sizeof(*s));
	free(s);
}

