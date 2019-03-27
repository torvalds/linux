/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include "namespace.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"
#include "nscachedcli.h"

#define NS_DEFAULT_CACHED_IO_TIMEOUT	4

static int safe_write(struct cached_connection_ *, const void *, size_t);
static int safe_read(struct cached_connection_ *, void *, size_t);
static int send_credentials(struct cached_connection_ *, int);

/*
 * safe_write writes data to the specified connection and tries to do it in
 * the very safe manner. We ensure, that we can write to the socket with
 * kevent. If the data_size can't be sent in one piece, then it would be
 * splitted.
 */
static int
safe_write(struct cached_connection_ *connection, const void *data,
    size_t data_size)
{
	struct kevent eventlist;
	int nevents;
	size_t result;
	ssize_t s_result;
	struct timespec timeout;

	if (data_size == 0)
		return (0);

	timeout.tv_sec = NS_DEFAULT_CACHED_IO_TIMEOUT;
	timeout.tv_nsec = 0;
	result = 0;
	do {
		nevents = _kevent(connection->write_queue, NULL, 0, &eventlist,
		    1, &timeout);
		if ((nevents == 1) && (eventlist.filter == EVFILT_WRITE)) {
			s_result = _sendto(connection->sockfd, data + result,
			    eventlist.data < data_size - result ?
			    eventlist.data : data_size - result, MSG_NOSIGNAL,
			    NULL, 0);
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

/*
 * safe_read reads data from connection and tries to do it in the very safe
 * and stable way. It uses kevent to ensure, that the data are available for
 * reading. If the amount of data to be read is too large, then they would
 * be splitted.
 */
static int
safe_read(struct cached_connection_ *connection, void *data, size_t data_size)
{
	struct kevent eventlist;
	size_t result;
	ssize_t s_result;
	struct timespec timeout;
	int nevents;

	if (data_size == 0)
		return (0);

	timeout.tv_sec = NS_DEFAULT_CACHED_IO_TIMEOUT;
	timeout.tv_nsec = 0;
	result = 0;
	do {
		nevents = _kevent(connection->read_queue, NULL, 0, &eventlist,
		    1, &timeout);
		if (nevents == 1 && eventlist.filter == EVFILT_READ) {
			s_result = _read(connection->sockfd, data + result,
			    eventlist.data <= data_size - result ?
			    eventlist.data : data_size - result);
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

/*
 * Sends the credentials information to the connection along with the
 * communication element type.
 */
static int
send_credentials(struct cached_connection_ *connection, int type)
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
	res = _kevent(connection->write_queue, &eventlist, 1, NULL, 0, NULL);

	nevents = _kevent(connection->write_queue, NULL, 0, &eventlist, 1,
	    NULL);
	if (nevents == 1 && eventlist.filter == EVFILT_WRITE) {
		result = _sendmsg(connection->sockfd, &mhdr,
		    MSG_NOSIGNAL) == -1 ? -1 : 0;
		EV_SET(&eventlist, connection->sockfd, EVFILT_WRITE, EV_ADD,
		    0, 0, NULL);
		_kevent(connection->write_queue, &eventlist, 1, NULL, 0, NULL);
		return (result);
	} else
		return (-1);
}

/*
 * Opens the connection with the specified params. Initializes all kqueues.
 */
struct cached_connection_ *
__open_cached_connection(struct cached_connection_params const *params)
{
	struct cached_connection_ *retval;
	struct kevent eventlist;
	struct sockaddr_un client_address;
	int client_address_len, client_socket;
	int res;

	assert(params != NULL);

	client_socket = _socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	client_address.sun_family = PF_LOCAL;
	strncpy(client_address.sun_path, params->socket_path,
	    sizeof(client_address.sun_path));
	client_address_len = sizeof(client_address.sun_family) +
	    strlen(client_address.sun_path) + 1;

	res = _connect(client_socket, (struct sockaddr *)&client_address,
	    client_address_len);
	if (res == -1) {
		_close(client_socket);
		return (NULL);
	}
	_fcntl(client_socket, F_SETFL, O_NONBLOCK);

	retval = malloc(sizeof(struct cached_connection_));
	assert(retval != NULL);
	memset(retval, 0, sizeof(struct cached_connection_));

	retval->sockfd = client_socket;

	retval->write_queue = kqueue();
	assert(retval->write_queue != -1);

	EV_SET(&eventlist, retval->sockfd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
	res = _kevent(retval->write_queue, &eventlist, 1, NULL, 0, NULL);

	retval->read_queue = kqueue();
	assert(retval->read_queue != -1);

	EV_SET(&eventlist, retval->sockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	res = _kevent(retval->read_queue, &eventlist, 1, NULL, 0, NULL);

	return (retval);
}

void
__close_cached_connection(struct cached_connection_ *connection)
{
	assert(connection != NULL);

	_close(connection->sockfd);
	_close(connection->read_queue);
	_close(connection->write_queue);
	free(connection);
}

/*
 * This function is very close to the cache_write function of the caching
 * library, which is used in the caching daemon. It caches the data with the
 * specified key in the cache entry with entry_name.
 */
int
__cached_write(struct cached_connection_ *connection, const char *entry_name,
    const char *key, size_t key_size, const char *data, size_t data_size)
{
	size_t name_size;
	int error_code;
	int result;

	error_code = -1;
	result = 0;
	result = send_credentials(connection, CET_WRITE_REQUEST);
	if (result != 0)
		goto fin;

	name_size = strlen(entry_name);
	result = safe_write(connection, &name_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, &key_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, &data_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, entry_name, name_size);
	if (result != 0)
		goto fin;

	result = safe_write(connection, key, key_size);
	if (result != 0)
		goto fin;

	result = safe_write(connection, data, data_size);
	if (result != 0)
		goto fin;

	result = safe_read(connection, &error_code, sizeof(int));
	if (result != 0)
		error_code = -1;

fin:
	return (error_code);
}

/*
 * This function is very close to the cache_read function of the caching
 * library, which is used in the caching daemon. It reads cached data with the
 * specified key from the cache entry with entry_name.
 */
int
__cached_read(struct cached_connection_ *connection, const char *entry_name,
    const char *key, size_t key_size, char *data, size_t *data_size)
{
	size_t name_size, result_size;
	int error_code, rec_error_code;
	int result;

	assert(connection != NULL);
	result = 0;
	error_code = -1;

	result = send_credentials(connection, CET_READ_REQUEST);
	if (result != 0)
		goto fin;

	name_size = strlen(entry_name);
	result = safe_write(connection, &name_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, &key_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, entry_name, name_size);
	if (result != 0)
		goto fin;

	result = safe_write(connection, key, key_size);
	if (result != 0)
		goto fin;

	result = safe_read(connection, &rec_error_code, sizeof(int));
	if (result != 0)
		goto fin;

	if (rec_error_code != 0) {
		error_code = rec_error_code;
		goto fin;
	}

	result = safe_read(connection, &result_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	 if (result_size > *data_size) {
		 *data_size = result_size;
		 error_code = -2;
		 goto fin;
	 }

	result = safe_read(connection, data, result_size);
	if (result != 0)
		goto fin;

	*data_size = result_size;
	error_code = 0;

fin:
	return (error_code);
}

/*
 * Initializes the mp_write_session. For such a session the new connection
 * would be opened. The data should be written to the session with
 * __cached_mp_write function. The __close_cached_mp_write_session function
 * should be used to submit session and __abandon_cached_mp_write_session - to
 * abandon it. When the session is submitted, the whole se
 */
struct cached_connection_ *
__open_cached_mp_write_session(struct cached_connection_params const *params,
    const char *entry_name)
{
	struct cached_connection_ *connection, *retval;
	size_t name_size;
	int error_code;
	int result;

	retval = NULL;
	connection = __open_cached_connection(params);
	if (connection == NULL)
		return (NULL);
	connection->mp_flag = 1;

	result = send_credentials(connection, CET_MP_WRITE_SESSION_REQUEST);
	if (result != 0)
		goto fin;

	name_size = strlen(entry_name);
	result = safe_write(connection, &name_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, entry_name, name_size);
	if (result != 0)
		goto fin;

	result = safe_read(connection, &error_code, sizeof(int));
	if (result != 0)
		goto fin;

	if (error_code != 0)
		result = error_code;

fin:
	if (result != 0)
		__close_cached_connection(connection);
	else
		retval = connection;
	return (retval);
}

/*
 * Adds new portion of data to the opened write session
 */
int
__cached_mp_write(struct cached_connection_ *ws, const char *data,
    size_t data_size)
{
	int request, result;
	int error_code;

	error_code = -1;

	request = CET_MP_WRITE_SESSION_WRITE_REQUEST;
	result = safe_write(ws, &request, sizeof(int));
	if (result != 0)
		goto fin;

	result = safe_write(ws, &data_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(ws, data, data_size);
	if (result != 0)
		goto fin;

	result = safe_read(ws, &error_code, sizeof(int));
	if (result != 0)
		error_code = -1;

fin:
	return (error_code);
}

/*
 * Abandons all operations with the write session. All data, that were written
 * to the session before, are discarded.
 */
int
__abandon_cached_mp_write_session(struct cached_connection_ *ws)
{
	int notification;
	int result;

	notification = CET_MP_WRITE_SESSION_ABANDON_NOTIFICATION;
	result = safe_write(ws, &notification, sizeof(int));
	__close_cached_connection(ws);
	return (result);
}

/*
 * Gracefully closes the write session. The data, that were previously written
 * to the session, are committed.
 */
int
__close_cached_mp_write_session(struct cached_connection_ *ws)
{
	int notification;
	int result;

	notification = CET_MP_WRITE_SESSION_CLOSE_NOTIFICATION;
	result = safe_write(ws, &notification, sizeof(int));
	__close_cached_connection(ws);
	return (0);
}

struct cached_connection_ *
__open_cached_mp_read_session(struct cached_connection_params const *params,
	const char *entry_name)
{
	struct cached_connection_ *connection, *retval;
	size_t name_size;
	int error_code;
	int result;

	retval = NULL;
	connection = __open_cached_connection(params);
	if (connection == NULL)
		return (NULL);
	connection->mp_flag = 1;

	result = send_credentials(connection, CET_MP_READ_SESSION_REQUEST);
	if (result != 0)
		goto fin;

	name_size = strlen(entry_name);
	result = safe_write(connection, &name_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	result = safe_write(connection, entry_name, name_size);
	if (result != 0)
		goto fin;

	result = safe_read(connection, &error_code, sizeof(int));
	if (result != 0)
		goto fin;

	if (error_code != 0)
		result = error_code;

fin:
	if (result != 0)
		__close_cached_connection(connection);
	else
		retval = connection;
	return (retval);
}

int
__cached_mp_read(struct cached_connection_ *rs, char *data, size_t *data_size)
{
	size_t result_size;
	int error_code, rec_error_code;
	int request, result;

	error_code = -1;
	request = CET_MP_READ_SESSION_READ_REQUEST;
	result = safe_write(rs, &request, sizeof(int));
	if (result != 0)
		goto fin;

	result = safe_read(rs, &rec_error_code, sizeof(int));
	if (result != 0)
		goto fin;

	if (rec_error_code != 0) {
		error_code = rec_error_code;
		goto fin;
	}

	result = safe_read(rs, &result_size, sizeof(size_t));
	if (result != 0)
		goto fin;

	if (result_size > *data_size) {
		*data_size = result_size;
		error_code = -2;
		goto fin;
	}

	result = safe_read(rs, data, result_size);
	if (result != 0)
		goto fin;

	*data_size = result_size;
	error_code = 0;

fin:
	return (error_code);
}

int
__close_cached_mp_read_session(struct cached_connection_ *rs)
{

	__close_cached_connection(rs);
	return (0);
}
