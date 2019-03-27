/*
 * bthidd.h
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
 * $Id: bthidd.h,v 1.7 2006/09/07 21:06:53 max Exp $
 * $FreeBSD$
 */

#ifndef _BTHIDD_H_
#define _BTHIDD_H_ 1

#define BTHIDD_IDENT	"bthidd"
#define BTHIDD_PIDFILE	"/var/run/" BTHIDD_IDENT ".pid"

struct bthid_session;

struct bthid_server
{
	bdaddr_t			 bdaddr; /* local bdaddr */
	int32_t				 cons;	 /* /dev/consolectl */
	int32_t				 ctrl;   /* control channel (listen) */
	int32_t				 intr;   /* intr. channel (listen) */
	int32_t				 maxfd;	 /* max fd in sets */
	int32_t				 uinput; /* enable evdev support */
	fd_set				 rfdset; /* read descriptor set */
	fd_set				 wfdset; /* write descriptor set */
	LIST_HEAD(, bthid_session)	 sessions;
};

typedef struct bthid_server	bthid_server_t;
typedef struct bthid_server *	bthid_server_p;

struct bthid_session
{
	bthid_server_p			 srv;	/* pointer back to server */
	int32_t				 ctrl;	/* control channel */
	int32_t				 intr;	/* interrupt channel */
	int32_t				 vkbd;	/* virual keyboard */
	void				*ctx;   /* product specific dev state */
	int32_t				 ukbd;  /* evdev user input */
	int32_t				 umouse;/* evdev user input */
	int32_t				 obutt; /* previous mouse buttons */
	int32_t				 consk; /* last consumer page key */
	bdaddr_t			 bdaddr;/* remote bdaddr */
	uint16_t			 state;	/* session state */
#define CLOSED	0
#define	W4CTRL	1
#define	W4INTR	2
#define	OPEN	3
	bitstr_t			*keys1;	/* keys map (new) */
	bitstr_t			*keys2;	/* keys map (old) */
	LIST_ENTRY(bthid_session)	 next;	/* link to next */
};

typedef struct bthid_session	bthid_session_t;
typedef struct bthid_session *	bthid_session_p;

int32_t		server_init      (bthid_server_p srv);
void		server_shutdown  (bthid_server_p srv);
int32_t		server_do        (bthid_server_p srv);

int32_t		client_rescan    (bthid_server_p srv);
int32_t		client_connect   (bthid_server_p srv, int fd);

bthid_session_p	session_open     (bthid_server_p srv, hid_device_p const d);
bthid_session_p	session_by_bdaddr(bthid_server_p srv, bdaddr_p bdaddr);
bthid_session_p	session_by_fd    (bthid_server_p srv, int32_t fd);
int32_t		session_run      (bthid_session_p s);
void		session_close    (bthid_session_p s);

void		hid_initialise	 (bthid_session_p s);
int32_t		hid_control      (bthid_session_p s, uint8_t *data, int32_t len);
int32_t		hid_interrupt    (bthid_session_p s, uint8_t *data, int32_t len);

#endif /* ndef _BTHIDD_H_ */

