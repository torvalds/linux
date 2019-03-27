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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "nscdcli.h"
#include "protocol.h"

#define DEFAULT_NSCD_IO_TIMEOUT	4

static int safe_write(struct nscd_connection_ *, const void *, size_t);
static int safe_read(struct nscd_connection_ *, void *, size_t);
static int send_credentials(struct nscd_connection_ *, int);

static int
safe_write(struct nscd_connection_ *connection, const void *data,
	size_t data_size)
{
	struct kevent eventlist;
	int	nevents;
	size_t result;
	ssize_t s_result;
	struct timespec	timeout;

	if (data_size == 0)
		return (0);

	timeout.tv_sec = DEFAULT_NSCD_IO_TIMEOUT;
	timeout.tv_nsec = 0;
	result = 0;
	do {
		nevents = kevent(connection->write_queue, NULL, 0, &eventlist,
	    		1, &timeout);
		if ((nevents == 1) && (eventlist.filter == EVFILT_WRITE)) {
			s_result = write(connection->sockfd,
				(char *)data + result,
				(size_t)eventlist.data < data_size - result ?
		    		(size_t)eventlist.data : data_size - result);
			if (s_result == -1)
				return (-1);
			else
				result += s_result;

			if (eventlist.flags & EV_EOF)
				return (result < data_size ? -1 : 0);
		} else
			return (-1);
	} while (result < data_size);

	return (0);
}

static int
safe_read(struct nscd_connection_ *connection, void *data, size_t data_size)
{
	struct kevent eventlist;
	size_t result;
	ssize_t s_result;
	struct timespec timeout;
	int nevents;

	if (data_size == 0)
		return (0);

	timeout.tv_sec = DEFAULT_NSCD_IO_TIMEOUT;
	timeout.tv_nsec = 0;
	result = 0;
	do {
		nevents = kevent(connection->read_queue, NULL, 0, &eventlist, 1,
			&timeout);
		if ((nevents == 1) && (eventlist.filter == EVFILT_READ)) {
			s_result = read(connection->sockfd,
				(char *)data + result,
				(size_t)eventlist.data <= data_size - result ?
				(size_t)eventlist.data : data_size - result);
			if (s_result == -1)
				return (-1);
			else
				result += s_result;

			if (eventlist.flags & EV_EOF)
				return (result < data_size ? -1 : 0);
		} else
			return (-1);
	} while (result < data_size);

	return (0);
}

static int
send_credentials(struct nscd_connection_ *connection, int type)
{
	union {
		struct cmsghdr hdr;
		char pad[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cmsg;
	struct msghdr mhdr;
	struct iovec iov;
	struct kevent eventlist;
	int nevents;
	ssize_t result;
	int res;

	TRACE_IN(send_credentials);
	memset(&cmsg, 0, sizeof(cmsg));
	cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(struct cmsgcred));
	cmsg.hdr.cmsg_level = SOL_SOCKET;
	cmsg.hdr.cmsg_type = SCM_CREDS;

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = &cmsg;
	mhdr.msg_controllen = CMSG_SPACE(sizeof(struct cmsgcred));

	iov.iov_base = &type;
	iov.iov_len = sizeof(int);

	EV_SET(&eventlist, connection->sockfd, EVFILT_WRITE, EV_ADD,
	    NOTE_LOWAT, sizeof(int), NULL);
	res = kevent(connection->write_queue, &eventlist, 1, NULL, 0, NULL);

	nevents = kevent(connection->write_queue, NULL, 0, &eventlist, 1, NULL);
	if ((nevents == 1) && (eventlist.filter == EVFILT_WRITE)) {
		result = sendmsg(connection->sockfd, &mhdr, 0) == -1 ? -1 : 0;
		EV_SET(&eventlist, connection->sockfd, EVFILT_WRITE, EV_ADD,
		    0, 0, NULL);
		kevent(connection->write_queue, &eventlist, 1, NULL, 0, NULL);
		TRACE_OUT(send_credentials);
		return (result);
	} else {
		TRACE_OUT(send_credentials);
		return (-1);
	}
}

struct nscd_connection_ *
open_nscd_connection__(struct nscd_connection_params const *params)
{
	struct nscd_connection_ *retval;
	struct kevent eventlist;
	struct sockaddr_un	client_address;
	int client_address_len, client_socket;
	int res;

	TRACE_IN(open_nscd_connection);
	assert(params != NULL);

	client_socket = socket(PF_LOCAL, SOCK_STREAM, 0);
	client_address.sun_family = PF_LOCAL;
	strlcpy(client_address.sun_path, params->socket_path,
		sizeof(client_address.sun_path));
	client_address_len = sizeof(client_address.sun_family) +
		strlen(client_address.sun_path) + 1;

	res = connect(client_socket, (struct sockaddr *)&client_address,
		client_address_len);
	if (res == -1) {
		close(client_socket);
		TRACE_OUT(open_nscd_connection);
		return (NULL);
	}
	fcntl(client_socket, F_SETFL, O_NONBLOCK);

	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->sockfd = client_socket;

	retval->write_queue = kqueue();
	assert(retval->write_queue != -1);

	EV_SET(&eventlist, retval->sockfd, EVFILT_WRITE, EV_ADD,
		0, 0, NULL);
	res = kevent(retval->write_queue, &eventlist, 1, NULL, 0, NULL);

	retval->read_queue = kqueue();
	assert(retval->read_queue != -1);

	EV_SET(&eventlist, retval->sockfd, EVFILT_READ, EV_ADD,
		0, 0, NULL);
	res = kevent(retval->read_queue, &eventlist, 1, NULL, 0, NULL);

	TRACE_OUT(open_nscd_connection);
	return (retval);
}

void
close_nscd_connection__(struct nscd_connection_ *connection)
{

	TRACE_IN(close_nscd_connection);
	assert(connection != NULL);

	close(connection->sockfd);
	close(connection->read_queue);
	close(connection->write_queue);
	free(connection);
	TRACE_OUT(close_nscd_connection);
}

int
nscd_transform__(struct nscd_connection_ *connection,
	const char *entry_name, int transformation_type)
{
	size_t name_size;
	int error_code;
	int result;

	TRACE_IN(nscd_transform);

	error_code = -1;
	result = 0;
	result = send_credentials(connection, CET_TRANSFORM_REQUEST);
	if (result != 0)
		goto fin;

	if (entry_name != NULL)
		name_size = strlen(entry_name);
	else
		name_size = 0;

	result = safe_write(connection, &name_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, &transformation_type, sizeof(int));
	if (result != 0)
		goto fin;

	if (entry_name != NULL) {
		result = safe_write(connection, entry_name, name_size);
		if (result != 0)
			goto fin;
	}

	result = safe_read(connection, &error_code, sizeof(int));
	if (result != 0)
		error_code = -1;

fin:
	TRACE_OUT(nscd_transform);
	return (error_code);
}
