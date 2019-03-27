/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#ifndef __NS_CACHED_CLI_H__
#define __NS_CACHED_CLI_H__

/*
 * This file contains API for working with caching daemon
 */

enum comm_element_t {
	CET_UNDEFINED = 0,
	CET_WRITE_REQUEST = 1,
	CET_WRITE_RESPONSE = 2,
	CET_READ_REQUEST = 3,
	CET_READ_RESPONSE = 4,
	CET_TRANSFORM_REQUEST = 5,
	CET_TRANSFORM_RESPONSE = 6,
	CET_MP_WRITE_SESSION_REQUEST = 7,
	CET_MP_WRITE_SESSION_RESPONSE = 8,
	CET_MP_WRITE_SESSION_WRITE_REQUEST = 9,
	CET_MP_WRITE_SESSION_WRITE_RESPONSE = 10,
	CET_MP_WRITE_SESSION_CLOSE_NOTIFICATION = 11,
	CET_MP_WRITE_SESSION_ABANDON_NOTIFICATION = 12,
	CET_MP_READ_SESSION_REQUEST = 13,
	CET_MP_READ_SESSION_RESPONSE = 14,
	CET_MP_READ_SESSION_READ_REQUEST = 15,
	CET_MP_READ_SESSION_READ_RESPONSE = 16,
	CET_MP_READ_SESSION_CLOSE_NOTIFICATION = 17
};

struct cached_connection_params {
	char	*socket_path;
	struct	timeval	timeout;
};

struct cached_connection_ {
	int	sockfd;
	int	read_queue;
	int	write_queue;

	int	mp_flag;	/* shows if the connection is used for
				 * multipart operations */
};

/* simple abstractions for not to write "struct" every time */
typedef struct cached_connection_	*cached_connection;
typedef struct cached_connection_	*cached_mp_write_session;
typedef struct cached_connection_	*cached_mp_read_session;

#define	INVALID_CACHED_CONNECTION	(NULL)
#define	INVALID_CACHED_MP_WRITE_SESSION	(NULL)
#define	INVALID_CACHED_MP_READ_SESSION	(NULL)

__BEGIN_DECLS

/* initialization/destruction routines */
extern	cached_connection __open_cached_connection(
	struct cached_connection_params const *);
extern	void __close_cached_connection(cached_connection);

/* simple read/write operations */
extern	int __cached_write(cached_connection, const char *, const char *,
	size_t, const char *, size_t);
extern	int __cached_read(cached_connection, const char *, const char *,
	size_t, char *, size_t *);

/* multipart read/write operations */
extern	cached_mp_write_session __open_cached_mp_write_session(
	struct cached_connection_params const *, const char *);
extern	int __cached_mp_write(cached_mp_write_session, const char *, size_t);
extern	int __abandon_cached_mp_write_session(cached_mp_write_session);
extern	int __close_cached_mp_write_session(cached_mp_write_session);

extern	cached_mp_read_session __open_cached_mp_read_session(
	struct cached_connection_params const *, const char *);
extern	int __cached_mp_read(cached_mp_read_session, char *, size_t *);
extern	int __close_cached_mp_read_session(cached_mp_read_session);

__END_DECLS

#endif
