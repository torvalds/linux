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
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelib.h"
#include "config.h"
#include "debug.h"
#include "log.h"
#include "query.h"
#include "mp_ws_query.h"
#include "singletons.h"

static int on_mp_write_session_abandon_notification(struct query_state *);
static int on_mp_write_session_close_notification(struct query_state *);
static void on_mp_write_session_destroy(struct query_state *);
static int on_mp_write_session_mapper(struct query_state *);
/* int on_mp_write_session_request_read1(struct query_state *); */
static int on_mp_write_session_request_read2(struct query_state *);
static int on_mp_write_session_request_process(struct query_state *);
static int on_mp_write_session_response_write1(struct query_state *);
static int on_mp_write_session_write_request_read1(struct query_state *);
static int on_mp_write_session_write_request_read2(struct query_state *);
static int on_mp_write_session_write_request_process(struct query_state *);
static int on_mp_write_session_write_response_write1(struct query_state *);

/*
 * This function is used as the query_state's destroy_func to make the
 * proper cleanup in case of errors.
 */
static void
on_mp_write_session_destroy(struct query_state *qstate)
{

	TRACE_IN(on_mp_write_session_destroy);
	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	if (qstate->mdata != NULL) {
		configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
		abandon_cache_mp_write_session(
	    		(cache_mp_write_session)qstate->mdata);
		configuration_unlock_entry(qstate->config_entry,
			CELT_MULTIPART);
	}
	TRACE_OUT(on_mp_write_session_destroy);
}

/*
 * The functions below are used to process multipart write session initiation
 * requests.
 * - on_mp_write_session_request_read1 and on_mp_write_session_request_read2
 *   read the request itself
 * - on_mp_write_session_request_process processes it
 * - on_mp_write_session_response_write1 sends the response
 */
int
on_mp_write_session_request_read1(struct query_state *qstate)
{
	struct cache_mp_write_session_request	*c_mp_ws_request;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_request_read1);
	if (qstate->kevent_watermark == 0)
		qstate->kevent_watermark = sizeof(size_t);
	else {
		init_comm_element(&qstate->request,
	    		CET_MP_WRITE_SESSION_REQUEST);
		c_mp_ws_request = get_cache_mp_write_session_request(
	    		&qstate->request);

		result = qstate->read_func(qstate,
	    		&c_mp_ws_request->entry_length, sizeof(size_t));

		if (result != sizeof(size_t)) {
			LOG_ERR_3("on_mp_write_session_request_read1",
				"read failed");
			TRACE_OUT(on_mp_write_session_request_read1);
			return (-1);
		}

		if (BUFSIZE_INVALID(c_mp_ws_request->entry_length)) {
			LOG_ERR_3("on_mp_write_session_request_read1",
				"invalid entry_length value");
			TRACE_OUT(on_mp_write_session_request_read1);
			return (-1);
		}

		c_mp_ws_request->entry = calloc(1,
			c_mp_ws_request->entry_length + 1);
		assert(c_mp_ws_request->entry != NULL);

		qstate->kevent_watermark = c_mp_ws_request->entry_length;
		qstate->process_func = on_mp_write_session_request_read2;
	}
	TRACE_OUT(on_mp_write_session_request_read1);
	return (0);
}

static int
on_mp_write_session_request_read2(struct query_state *qstate)
{
	struct cache_mp_write_session_request	*c_mp_ws_request;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_request_read2);
	c_mp_ws_request = get_cache_mp_write_session_request(&qstate->request);

	result = qstate->read_func(qstate, c_mp_ws_request->entry,
		c_mp_ws_request->entry_length);

	if (result < 0 || (size_t)result != qstate->kevent_watermark) {
		LOG_ERR_3("on_mp_write_session_request_read2",
			"read failed");
		TRACE_OUT(on_mp_write_session_request_read2);
		return (-1);
	}

	qstate->kevent_watermark = 0;
	qstate->process_func = on_mp_write_session_request_process;

	TRACE_OUT(on_mp_write_session_request_read2);
	return (0);
}

static int
on_mp_write_session_request_process(struct query_state *qstate)
{
	struct cache_mp_write_session_request	*c_mp_ws_request;
	struct cache_mp_write_session_response	*c_mp_ws_response;
	cache_mp_write_session	ws;
	cache_entry	c_entry;
	char	*dec_cache_entry_name;

	TRACE_IN(on_mp_write_session_request_process);
	init_comm_element(&qstate->response, CET_MP_WRITE_SESSION_RESPONSE);
	c_mp_ws_response = get_cache_mp_write_session_response(
		&qstate->response);
	c_mp_ws_request = get_cache_mp_write_session_request(&qstate->request);

	qstate->config_entry = configuration_find_entry(
		s_configuration, c_mp_ws_request->entry);
	if (qstate->config_entry == NULL) {
		c_mp_ws_response->error_code = ENOENT;

		LOG_ERR_2("write_session_request",
			"can't find configuration entry '%s'. "
	    		"aborting request", c_mp_ws_request->entry);
	    	goto fin;
	}

	if (qstate->config_entry->enabled == 0) {
		c_mp_ws_response->error_code = EACCES;

		LOG_ERR_2("write_session_request",
			"configuration entry '%s' is disabled",
			c_mp_ws_request->entry);
		goto fin;
	}

	if (qstate->config_entry->perform_actual_lookups != 0) {
		c_mp_ws_response->error_code = EOPNOTSUPP;

		LOG_ERR_2("write_session_request",
			"entry '%s' performs lookups by itself: "
			"can't write to it", c_mp_ws_request->entry);
		goto fin;
	} else {
#ifdef NS_NSCD_EID_CHECKING
		if (check_query_eids(qstate) != 0) {
			c_mp_ws_response->error_code = EPERM;
			goto fin;
		}
#endif
	}

	/*
	 * All multipart entries are separated by their name decorations.
	 * For one configuration entry there will be a lot of multipart
	 * cache entries - each with its own decorated name.
	 */
	asprintf(&dec_cache_entry_name, "%s%s", qstate->eid_str,
		qstate->config_entry->mp_cache_params.cep.entry_name);
	assert(dec_cache_entry_name != NULL);

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache,
		dec_cache_entry_name);
	configuration_unlock(s_configuration);

	if (c_entry == INVALID_CACHE_ENTRY)
		c_entry = register_new_mp_cache_entry(qstate,
			dec_cache_entry_name);

	free(dec_cache_entry_name);

	assert(c_entry != NULL);
	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	ws = open_cache_mp_write_session(c_entry);
	if (ws == INVALID_CACHE_MP_WRITE_SESSION)
		c_mp_ws_response->error_code = -1;
	else {
		qstate->mdata = ws;
		qstate->destroy_func = on_mp_write_session_destroy;

		if ((qstate->config_entry->mp_query_timeout.tv_sec != 0) ||
		    (qstate->config_entry->mp_query_timeout.tv_usec != 0))
			memcpy(&qstate->timeout,
				&qstate->config_entry->mp_query_timeout,
				sizeof(struct timeval));
	}
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);

fin:
	qstate->process_func = on_mp_write_session_response_write1;
	qstate->kevent_watermark = sizeof(int);
	qstate->kevent_filter = EVFILT_WRITE;

	TRACE_OUT(on_mp_write_session_request_process);
	return (0);
}

static int
on_mp_write_session_response_write1(struct query_state *qstate)
{
	struct cache_mp_write_session_response	*c_mp_ws_response;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_response_write1);
	c_mp_ws_response = get_cache_mp_write_session_response(
		&qstate->response);
	result = qstate->write_func(qstate, &c_mp_ws_response->error_code,
		sizeof(int));
	if (result != sizeof(int)) {
		LOG_ERR_3("on_mp_write_session_response_write1",
			"write failed");
		TRACE_OUT(on_mp_write_session_response_write1);
		return (-1);
	}

	if (c_mp_ws_response->error_code == 0) {
		qstate->kevent_watermark = sizeof(int);
		qstate->process_func = on_mp_write_session_mapper;
		qstate->kevent_filter = EVFILT_READ;
	} else {
		qstate->kevent_watermark = 0;
		qstate->process_func = NULL;
	}
	TRACE_OUT(on_mp_write_session_response_write1);
	return (0);
}

/*
 * Mapper function is used to avoid multiple connections for each session
 * write or read requests. After processing the request, it does not close
 * the connection, but waits for the next request.
 */
static int
on_mp_write_session_mapper(struct query_state *qstate)
{
	ssize_t	result;
	int		elem_type;

	TRACE_IN(on_mp_write_session_mapper);
	if (qstate->kevent_watermark == 0) {
		qstate->kevent_watermark = sizeof(int);
	} else {
		result = qstate->read_func(qstate, &elem_type, sizeof(int));
		if (result != sizeof(int)) {
			LOG_ERR_3("on_mp_write_session_mapper",
				"read failed");
			TRACE_OUT(on_mp_write_session_mapper);
			return (-1);
		}

		switch (elem_type) {
		case CET_MP_WRITE_SESSION_WRITE_REQUEST:
			qstate->kevent_watermark = sizeof(size_t);
			qstate->process_func =
				on_mp_write_session_write_request_read1;
			break;
		case CET_MP_WRITE_SESSION_ABANDON_NOTIFICATION:
			qstate->kevent_watermark = 0;
			qstate->process_func =
				on_mp_write_session_abandon_notification;
			break;
		case CET_MP_WRITE_SESSION_CLOSE_NOTIFICATION:
			qstate->kevent_watermark = 0;
			qstate->process_func =
				on_mp_write_session_close_notification;
			break;
		default:
			qstate->kevent_watermark = 0;
			qstate->process_func = NULL;
			LOG_ERR_2("on_mp_write_session_mapper",
				"unknown element type");
			TRACE_OUT(on_mp_write_session_mapper);
			return (-1);
		}
	}
	TRACE_OUT(on_mp_write_session_mapper);
	return (0);
}

/*
 * The functions below are used to process multipart write sessions write
 * requests.
 * - on_mp_write_session_write_request_read1 and
 *   on_mp_write_session_write_request_read2 read the request itself
 * - on_mp_write_session_write_request_process processes it
 * - on_mp_write_session_write_response_write1 sends the response
 */
static int
on_mp_write_session_write_request_read1(struct query_state *qstate)
{
	struct cache_mp_write_session_write_request	*write_request;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_write_request_read1);
	init_comm_element(&qstate->request,
		CET_MP_WRITE_SESSION_WRITE_REQUEST);
	write_request = get_cache_mp_write_session_write_request(
		&qstate->request);

	result = qstate->read_func(qstate, &write_request->data_size,
		sizeof(size_t));

	if (result != sizeof(size_t)) {
		LOG_ERR_3("on_mp_write_session_write_request_read1",
			"read failed");
		TRACE_OUT(on_mp_write_session_write_request_read1);
		return (-1);
	}

	if (BUFSIZE_INVALID(write_request->data_size)) {
		LOG_ERR_3("on_mp_write_session_write_request_read1",
			"invalid data_size value");
		TRACE_OUT(on_mp_write_session_write_request_read1);
		return (-1);
	}

	write_request->data = calloc(1, write_request->data_size);
	assert(write_request->data != NULL);

	qstate->kevent_watermark = write_request->data_size;
	qstate->process_func = on_mp_write_session_write_request_read2;
	TRACE_OUT(on_mp_write_session_write_request_read1);
	return (0);
}

static int
on_mp_write_session_write_request_read2(struct query_state *qstate)
{
	struct cache_mp_write_session_write_request	*write_request;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_write_request_read2);
	write_request = get_cache_mp_write_session_write_request(
		&qstate->request);

	result = qstate->read_func(qstate, write_request->data,
		write_request->data_size);

	if (result < 0 || (size_t)result != qstate->kevent_watermark) {
		LOG_ERR_3("on_mp_write_session_write_request_read2",
			"read failed");
		TRACE_OUT(on_mp_write_session_write_request_read2);
		return (-1);
	}

	qstate->kevent_watermark = 0;
	qstate->process_func = on_mp_write_session_write_request_process;
	TRACE_OUT(on_mp_write_session_write_request_read2);
	return (0);
}

static int
on_mp_write_session_write_request_process(struct query_state *qstate)
{
	struct cache_mp_write_session_write_request	*write_request;
	struct cache_mp_write_session_write_response	*write_response;

	TRACE_IN(on_mp_write_session_write_request_process);
	init_comm_element(&qstate->response,
		CET_MP_WRITE_SESSION_WRITE_RESPONSE);
	write_response = get_cache_mp_write_session_write_response(
		&qstate->response);
	write_request = get_cache_mp_write_session_write_request(
		&qstate->request);

	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	write_response->error_code = cache_mp_write(
		(cache_mp_write_session)qstate->mdata,
		write_request->data,
		write_request->data_size);
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);

	qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_mp_write_session_write_response_write1;
	qstate->kevent_filter = EVFILT_WRITE;

	TRACE_OUT(on_mp_write_session_write_request_process);
	return (0);
}

static int
on_mp_write_session_write_response_write1(struct query_state *qstate)
{
	struct cache_mp_write_session_write_response	*write_response;
	ssize_t	result;

	TRACE_IN(on_mp_write_session_write_response_write1);
	write_response = get_cache_mp_write_session_write_response(
		&qstate->response);
	result = qstate->write_func(qstate, &write_response->error_code,
		sizeof(int));
	if (result != sizeof(int)) {
		LOG_ERR_3("on_mp_write_session_write_response_write1",
			"write failed");
		TRACE_OUT(on_mp_write_session_write_response_write1);
		return (-1);
	}

	if (write_response->error_code == 0) {
		finalize_comm_element(&qstate->request);
		finalize_comm_element(&qstate->response);

		qstate->kevent_watermark = sizeof(int);
		qstate->process_func = on_mp_write_session_mapper;
		qstate->kevent_filter = EVFILT_READ;
	} else {
		qstate->kevent_watermark = 0;
		qstate->process_func = 0;
	}

	TRACE_OUT(on_mp_write_session_write_response_write1);
	return (0);
}

/*
 * Handles abandon notifications. Destroys the session by calling the
 * abandon_cache_mp_write_session.
 */
static int
on_mp_write_session_abandon_notification(struct query_state *qstate)
{
	TRACE_IN(on_mp_write_session_abandon_notification);
	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	abandon_cache_mp_write_session((cache_mp_write_session)qstate->mdata);
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);
	qstate->mdata = INVALID_CACHE_MP_WRITE_SESSION;

	qstate->kevent_watermark = 0;
	qstate->process_func = NULL;
	TRACE_OUT(on_mp_write_session_abandon_notification);
	return (0);
}

/*
 * Handles close notifications. Commits the session by calling
 * the close_cache_mp_write_session.
 */
static int
on_mp_write_session_close_notification(struct query_state *qstate)
{
	TRACE_IN(on_mp_write_session_close_notification);
	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	close_cache_mp_write_session((cache_mp_write_session)qstate->mdata);
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);
	qstate->mdata = INVALID_CACHE_MP_WRITE_SESSION;

	qstate->kevent_watermark = 0;
	qstate->process_func = NULL;
	TRACE_OUT(on_mp_write_session_close_notification);
	return (0);
}

cache_entry register_new_mp_cache_entry(struct query_state *qstate,
	const char *dec_cache_entry_name)
{
	cache_entry c_entry;
	char *en_bkp;

	TRACE_IN(register_new_mp_cache_entry);
	c_entry = INVALID_CACHE_ENTRY;
	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);

	configuration_lock_wrlock(s_configuration);
	en_bkp = qstate->config_entry->mp_cache_params.cep.entry_name;
	qstate->config_entry->mp_cache_params.cep.entry_name =
		(char *)dec_cache_entry_name;
	register_cache_entry(s_cache, (struct cache_entry_params *)
		&qstate->config_entry->mp_cache_params);
	qstate->config_entry->mp_cache_params.cep.entry_name = en_bkp;
	configuration_unlock(s_configuration);

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache,
		dec_cache_entry_name);
	configuration_unlock(s_configuration);

	configuration_entry_add_mp_cache_entry(qstate->config_entry,
		c_entry);

	configuration_unlock_entry(qstate->config_entry,
		CELT_MULTIPART);

	TRACE_OUT(register_new_mp_cache_entry);
	return (c_entry);
}
