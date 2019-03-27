/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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

#ifndef __NSCD_QUERY_H__
#define __NSCD_QUERY_H__

#include "cachelib.h"
#include "config.h"
#include "protocol.h"

struct query_state;
struct configuration;
struct configuration_entry;

typedef	int (*query_process_func)(struct query_state *);
typedef void (*query_destroy_func)(struct query_state *);
typedef ssize_t (*query_read_func)(struct query_state *, void *, size_t);
typedef ssize_t (*query_write_func)(struct query_state *, const void *, size_t);

/*
 * The query state structure contains the information to process all types of
 * requests and to send all types of responses.
 */
struct query_state {
	struct timeval creation_time;
	struct timeval timeout;

	struct comm_element request;
	struct comm_element response;
	struct configuration_entry *config_entry;
	void	*mdata;

	query_process_func process_func;	/* called on each event */
	query_destroy_func destroy_func;	/* called on destroy */

	/*
	 * By substituting these functions we can opaquely send and received
	 * very large buffers
	 */
	query_write_func write_func;		/* data write function */
	query_read_func read_func;		/* data read function */

	char	*eid_str;	/* the user-identifying string (euid_egid_) */
	size_t	eid_str_length;

	uid_t	euid;	/* euid of the caller, received via getpeereid */
	uid_t	uid;	/* uid of the caller, received via credentials */
	gid_t	egid;	/* egid of the caller, received via getpeereid */
	gid_t	gid;	/* gid of the caller received via credentials */

	size_t	io_buffer_size;
	size_t	io_buffer_watermark;
	size_t	kevent_watermark;	/* bytes to be sent/received */
	int	sockfd;			/* the unix socket to read/write */
	int	kevent_filter;	/* EVFILT_READ or EVFILT_WRITE */
	int socket_failed; /* set to 1 if the socket doesn't work correctly */

	/*
	 * These fields are used to opaquely proceed sending/receiving of
	 * the large buffers
	 */
	char	*io_buffer;
	char	*io_buffer_p;
	int	io_buffer_filter;
	int	use_alternate_io;
};

int check_query_eids(struct query_state *);

ssize_t query_io_buffer_read(struct query_state *, void *, size_t);
ssize_t query_io_buffer_write(struct query_state *, const void *, size_t);

ssize_t query_socket_read(struct query_state *, void *, size_t);
ssize_t query_socket_write(struct query_state *, const void *, size_t);

struct query_state *init_query_state(int, size_t, uid_t, gid_t);
void destroy_query_state(struct query_state *);

#endif
