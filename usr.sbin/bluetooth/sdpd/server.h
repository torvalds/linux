/*-
 * server.h
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: server.h,v 1.5 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#ifndef _SERVER_H_
#define _SERVER_H_

/*
 * File descriptor index entry
 */

struct fd_idx
{
	unsigned	 valid    : 1;	/* descriptor is valid */
	unsigned	 server   : 1;	/* descriptor is listening */
	unsigned	 control  : 1;	/* descriptor is a control socket */
	unsigned	 priv     : 1;	/* descriptor is privileged */
	unsigned	 reserved : 1;
	unsigned	 rsp_cs   : 11; /* response continuation state */
	uint16_t	 rsp_size;	/* response size */
	uint16_t	 rsp_limit;	/* response limit */
	uint16_t	 omtu;		/* outgoing MTU */
	uint8_t		*rsp;		/* outgoing buffer */
};

typedef struct fd_idx	fd_idx_t;
typedef struct fd_idx *	fd_idx_p;

/*
 * SDP server
 */

struct server
{
	uint32_t		 imtu;		/* incoming MTU */
	uint8_t			*req;		/* incoming buffer */
	int32_t			 maxfd;		/* max. descriptor is the set */
	fd_set			 fdset;		/* current descriptor set */
	fd_idx_p		 fdidx;		/* descriptor index */
	struct sockaddr_l2cap	 req_sa;	/* local address */
};

typedef struct server	server_t;
typedef struct server *	server_p;

/*
 * External API
 */

int32_t	server_init(server_p srv, const char *control);
void	server_shutdown(server_p srv);
int32_t	server_do(server_p srv);

int32_t	server_prepare_service_search_response(server_p srv, int32_t fd);
int32_t	server_send_service_search_response(server_p srv, int32_t fd);

int32_t	server_prepare_service_attribute_response(server_p srv, int32_t fd);
int32_t	server_send_service_attribute_response(server_p srv, int32_t fd);

int32_t	server_prepare_service_search_attribute_response(server_p srv, int32_t fd);
#define	server_send_service_search_attribute_response \
	server_send_service_attribute_response

int32_t	server_prepare_service_register_response(server_p srv, int32_t fd);
int32_t	server_send_service_register_response(server_p srv, int32_t fd);

int32_t	server_prepare_service_unregister_response(server_p srv, int32_t fd);
#define	server_send_service_unregister_response \
	server_send_service_register_response

int32_t	server_prepare_service_change_response(server_p srv, int32_t fd);
#define	server_send_service_change_response \
	server_send_service_register_response

#endif /* ndef _SERVER_H_ */
