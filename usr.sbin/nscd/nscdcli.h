/*-
 * Copyright (c) 2004 Michael Bushkov <bushman@rsu.ru>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __NSCD_NSCDCLI_H__
#define __NSCD_NSCDCLI_H__

struct nscd_connection_params {
	char *socket_path;
	struct timeval timeout;
};

struct nscd_connection_ {
	int sockfd;
	int read_queue;
	int write_queue;
};

/* simple abstractions for not to write "struct" every time */
typedef struct nscd_connection_ *nscd_connection;
typedef struct nscd_connection_ *nscd_mp_write_session;
typedef struct nscd_connection_ *nscd_mp_read_session;

#define	INVALID_NSCD_CONNECTION	(NULL)

/* initialization/destruction routines */
nscd_connection	open_nscd_connection__(struct nscd_connection_params const *);
void close_nscd_connection__(nscd_connection);
int nscd_transform__(nscd_connection, const char *, int);

#endif
