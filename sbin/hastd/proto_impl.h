/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_PROTO_IMPL_H_
#define	_PROTO_IMPL_H_

#include <sys/queue.h>

#include <stdbool.h>	/* bool */
#include <stdlib.h>	/* size_t */

#define	__constructor	__attribute__((constructor))

typedef int prt_client_t(const char *, const char *, void **);
typedef int prt_connect_t(void *, int);
typedef int prt_connect_wait_t(void *, int);
typedef int prt_server_t(const char *, void **);
typedef int prt_accept_t(void *, void **);
typedef int prt_wrap_t(int, bool, void **);
typedef int prt_send_t(void *, const unsigned char *, size_t, int);
typedef int prt_recv_t(void *, unsigned char *, size_t, int *);
typedef int prt_descriptor_t(const void *);
typedef bool prt_address_match_t(const void *, const char *);
typedef void prt_local_address_t(const void *, char *, size_t);
typedef void prt_remote_address_t(const void *, char *, size_t);
typedef void prt_close_t(void *);

struct proto {
	const char		*prt_name;
	prt_client_t		*prt_client;
	prt_connect_t		*prt_connect;
	prt_connect_wait_t	*prt_connect_wait;
	prt_server_t		*prt_server;
	prt_accept_t		*prt_accept;
	prt_wrap_t		*prt_wrap;
	prt_send_t		*prt_send;
	prt_recv_t		*prt_recv;
	prt_descriptor_t	*prt_descriptor;
	prt_address_match_t	*prt_address_match;
	prt_local_address_t	*prt_local_address;
	prt_remote_address_t	*prt_remote_address;
	prt_close_t		*prt_close;
	TAILQ_ENTRY(proto)	 prt_next;
};

void proto_register(struct proto *proto, bool isdefault);

int proto_common_send(int sock, const unsigned char *data, size_t size, int fd);
int proto_common_recv(int sock, unsigned char *data, size_t size, int *fdp);

#endif	/* !_PROTO_IMPL_H_ */
