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
#include <nsswitch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachelib.h"
#include "config.h"
#include "debug.h"
#include "log.h"
#include "query.h"
#include "mp_rs_query.h"
#include "mp_ws_query.h"
#include "singletons.h"

static int on_mp_read_session_close_notification(struct query_state *);
static void on_mp_read_session_destroy(struct query_state *);
static int on_mp_read_session_mapper(struct query_state *);
/* int on_mp_read_session_request_read1(struct query_state *); */
static int on_mp_read_session_request_read2(struct query_state *);
static int on_mp_read_session_request_process(struct query_state *);
static int on_mp_read_session_response_write1(struct query_state *);
static int on_mp_read_session_read_request_process(struct query_state *);
static int on_mp_read_session_read_response_write1(struct query_state *);
static int on_mp_read_session_read_response_write2(struct query_state *);

/*
 * This function is used as the query_state's destroy_func to make the
 * proper cleanup in case of errors.
 */
static void
on_mp_read_session_destroy(struct query_state *qstate)
{
	TRACE_IN(on_mp_read_session_destroy);
	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	if (qstate->mdata != NULL) {
		configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
		close_cache_mp_read_session(
	    		(cache_mp_read_session)qstate->mdata);
		configuration_unlock_entry(qstate->config_entry,
			CELT_MULTIPART);
	}
	TRACE_OUT(on_mp_read_session_destroy);
}

/*
 * The functions below are used to process multipart read session initiation
 * requests.
 * - on_mp_read_session_request_read1 and on_mp_read_session_request_read2 read
 *   the request itself
 * - on_mp_read_session_request_process processes it
 * - on_mp_read_session_response_write1 sends the response
 */
int
on_mp_read_session_request_read1(struct query_state *qstate)
{
	struct cache_mp_read_session_request	*c_mp_rs_request;
	ssize_t	result;

	TRACE_IN(on_mp_read_session_request_read1);
	if (qstate->kevent_watermark == 0)
		qstate->kevent_watermark = sizeof(size_t);
	else {
		init_comm_element(&qstate->request,
	    		CET_MP_READ_SESSION_REQUEST);
		c_mp_rs_request = get_cache_mp_read_session_request(
	    		&qstate->request);

		result = qstate->read_func(qstate,
	    		&c_mp_rs_request->entry_length, sizeof(size_t));

		if (result != sizeof(size_t)) {
			TRACE_OUT(on_mp_read_session_request_read1);
			return (-1);
		}

		if (BUFSIZE_INVALID(c_mp_rs_request->entry_length)) {
			TRACE_OUT(on_mp_read_session_request_read1);
			return (-1);
		}

		c_mp_rs_request->entry = calloc(1,
			c_mp_rs_request->entry_length + 1);
		assert(c_mp_rs_request->entry != NULL);

		qstate->kevent_watermark = c_mp_rs_request->entry_length;
		qstate->process_func = on_mp_read_session_request_read2;
	}
	TRACE_OUT(on_mp_read_session_request_read1);
	return (0);
}

static int
on_mp_read_session_request_read2(struct query_state *qstate)
{
	struct cache_mp_read_session_request	*c_mp_rs_request;
	ssize_t	result;

	TRACE_IN(on_mp_read_session_request_read2);
	c_mp_rs_request = get_cache_mp_read_session_request(&qstate->request);

	result = qstate->read_func(qstate, c_mp_rs_request->entry,
		c_mp_rs_request->entry_length);

	if (result < 0 || (size_t)result != qstate->kevent_watermark) {
		LOG_ERR_3("on_mp_read_session_request_read2",
			"read failed");
		TRACE_OUT(on_mp_read_session_request_read2);
		return (-1);
	}

	qstate->kevent_watermark = 0;
	qstate->process_func = on_mp_read_session_request_process;
	TRACE_OUT(on_mp_read_session_request_read2);
	return (0);
}

static int
on_mp_read_session_request_process(struct query_state *qstate)
{
	struct cache_mp_read_session_request	*c_mp_rs_request;
	struct cache_mp_read_session_response	*c_mp_rs_response;
	cache_mp_read_session	rs;
	cache_entry	c_entry;
	char	*dec_cache_entry_name;

	char *buffer;
	size_t buffer_size;
	cache_mp_write_session ws;
	struct agent	*lookup_agent;
	struct multipart_agent *mp_agent;
	void *mdata;
	int res;

	TRACE_IN(on_mp_read_session_request_process);
	init_comm_element(&qstate->response, CET_MP_READ_SESSION_RESPONSE);
	c_mp_rs_response = get_cache_mp_read_session_response(
		&qstate->response);
	c_mp_rs_request = get_cache_mp_read_session_request(&qstate->request);

	qstate->config_entry = configuration_find_entry(
		s_configuration, c_mp_rs_request->entry);
	if (qstate->config_entry == NULL) {
		c_mp_rs_response->error_code = ENOENT;

		LOG_ERR_2("read_session_request",
			"can't find configuration entry '%s'."
			" aborting request", c_mp_rs_request->entry);
		goto fin;
	}

	if (qstate->config_entry->enabled == 0) {
		c_mp_rs_response->error_code = EACCES;

		LOG_ERR_2("read_session_request",
			"configuration entry '%s' is disabled",
			c_mp_rs_request->entry);
		goto fin;
	}

	if (qstate->config_entry->perform_actual_lookups != 0)
		dec_cache_entry_name = strdup(
			qstate->config_entry->mp_cache_params.cep.entry_name);
	else {
#ifdef NS_NSCD_EID_CHECKING
		if (check_query_eids(qstate) != 0) {
			c_mp_rs_response->error_code = EPERM;
			goto fin;
		}
#endif

		asprintf(&dec_cache_entry_name, "%s%s", qstate->eid_str,
			qstate->config_entry->mp_cache_params.cep.entry_name);
	}

	assert(dec_cache_entry_name != NULL);

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache, dec_cache_entry_name);
	configuration_unlock(s_configuration);

	if ((c_entry == INVALID_CACHE) &&
	   (qstate->config_entry->perform_actual_lookups != 0))
		c_entry = register_new_mp_cache_entry(qstate,
			dec_cache_entry_name);

	free(dec_cache_entry_name);

	if (c_entry != INVALID_CACHE_ENTRY) {
		configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
		rs = open_cache_mp_read_session(c_entry);
		configuration_unlock_entry(qstate->config_entry,
			CELT_MULTIPART);

		if ((rs == INVALID_CACHE_MP_READ_SESSION) &&
		   (qstate->config_entry->perform_actual_lookups != 0)) {
			lookup_agent = find_agent(s_agent_table,
				c_mp_rs_request->entry, MULTIPART_AGENT);

			if ((lookup_agent != NULL) &&
			(lookup_agent->type == MULTIPART_AGENT)) {
				mp_agent = (struct multipart_agent *)
					lookup_agent;
				mdata = mp_agent->mp_init_func();

				/*
				 * Multipart agents read the whole snapshot
				 * of the data at one time.
				 */
				configuration_lock_entry(qstate->config_entry,
					CELT_MULTIPART);
				ws = open_cache_mp_write_session(c_entry);
				configuration_unlock_entry(qstate->config_entry,
					CELT_MULTIPART);
				if (ws != NULL) {
				    do {
					buffer = NULL;
					res = mp_agent->mp_lookup_func(&buffer,
						&buffer_size,
						mdata);

					if ((res & NS_TERMINATE) &&
					   (buffer != NULL)) {
						configuration_lock_entry(
							qstate->config_entry,
						   	CELT_MULTIPART);
						if (cache_mp_write(ws, buffer,
						    buffer_size) != 0) {
							abandon_cache_mp_write_session(ws);
							ws = NULL;
						}
						configuration_unlock_entry(
							qstate->config_entry,
							CELT_MULTIPART);

						free(buffer);
						buffer = NULL;
					} else {
						configuration_lock_entry(
							qstate->config_entry,
							CELT_MULTIPART);
						close_cache_mp_write_session(ws);
						configuration_unlock_entry(
							qstate->config_entry,
							CELT_MULTIPART);

						free(buffer);
						buffer = NULL;
					}
				    } while ((res & NS_TERMINATE) &&
				    	    (ws != NULL));
				}

				configuration_lock_entry(qstate->config_entry,
					CELT_MULTIPART);
				rs = open_cache_mp_read_session(c_entry);
				configuration_unlock_entry(qstate->config_entry,
					CELT_MULTIPART);
			}
		}

		if (rs == INVALID_CACHE_MP_READ_SESSION)
			c_mp_rs_response->error_code = -1;
		else {
		    qstate->mdata = rs;
		    qstate->destroy_func = on_mp_read_session_destroy;

		    configuration_lock_entry(qstate->config_entry,
			CELT_MULTIPART);
		    if ((qstate->config_entry->mp_query_timeout.tv_sec != 0) ||
		    (qstate->config_entry->mp_query_timeout.tv_usec != 0))
			memcpy(&qstate->timeout,
			    &qstate->config_entry->mp_query_timeout,
			    sizeof(struct timeval));
		    configuration_unlock_entry(qstate->config_entry,
			CELT_MULTIPART);
		}
	} else
		c_mp_rs_response->error_code = -1;

fin:
	qstate->process_func = on_mp_read_session_response_write1;
	qstate->kevent_watermark = sizeof(int);
	qstate->kevent_filter = EVFILT_WRITE;

	TRACE_OUT(on_mp_read_session_request_process);
	return (0);
}

static int
on_mp_read_session_response_write1(struct query_state *qstate)
{
	struct cache_mp_read_session_response	*c_mp_rs_response;
	ssize_t	result;

	TRACE_IN(on_mp_read_session_response_write1);
	c_mp_rs_response = get_cache_mp_read_session_response(
		&qstate->response);
	result = qstate->write_func(qstate, &c_mp_rs_response->error_code,
		sizeof(int));

	if (result != sizeof(int)) {
		LOG_ERR_3("on_mp_read_session_response_write1",
			"write failed");
		TRACE_OUT(on_mp_read_session_response_write1);
		return (-1);
	}

	if (c_mp_rs_response->error_code == 0) {
		qstate->kevent_watermark = sizeof(int);
		qstate->process_func = on_mp_read_session_mapper;
		qstate->kevent_filter = EVFILT_READ;
	} else {
		qstate->kevent_watermark = 0;
		qstate->process_func = NULL;
	}
	TRACE_OUT(on_mp_read_session_response_write1);
	return (0);
}

/*
 * Mapper function is used to avoid multiple connections for each session
 * write or read requests. After processing the request, it does not close
 * the connection, but waits for the next request.
 */
static int
on_mp_read_session_mapper(struct query_state *qstate)
{
	ssize_t	result;
	int elem_type;

	TRACE_IN(on_mp_read_session_mapper);
	if (qstate->kevent_watermark == 0) {
		qstate->kevent_watermark = sizeof(int);
	} else {
		result = qstate->read_func(qstate, &elem_type, sizeof(int));
		if (result != sizeof(int)) {
			LOG_ERR_3("on_mp_read_session_mapper",
				"read failed");
			TRACE_OUT(on_mp_read_session_mapper);
			return (-1);
		}

		switch (elem_type) {
		case CET_MP_READ_SESSION_READ_REQUEST:
			qstate->kevent_watermark = 0;
			qstate->process_func =
				on_mp_read_session_read_request_process;
			break;
		case CET_MP_READ_SESSION_CLOSE_NOTIFICATION:
			qstate->kevent_watermark = 0;
			qstate->process_func =
				on_mp_read_session_close_notification;
			break;
		default:
			qstate->kevent_watermark = 0;
			qstate->process_func = NULL;
			LOG_ERR_3("on_mp_read_session_mapper",
				"unknown element type");
			TRACE_OUT(on_mp_read_session_mapper);
			return (-1);
		}
	}
	TRACE_OUT(on_mp_read_session_mapper);
	return (0);
}

/*
 * The functions below are used to process multipart read sessions read
 * requests. User doesn't have to pass any kind of data, besides the
 * request identificator itself. So we don't need any XXX_read functions and
 * start with the XXX_process function.
 * - on_mp_read_session_read_request_process processes it
 * - on_mp_read_session_read_response_write1 and
 *   on_mp_read_session_read_response_write2 sends the response
 */
static int
on_mp_read_session_read_request_process(struct query_state *qstate)
{
	struct cache_mp_read_session_read_response	*read_response;

	TRACE_IN(on_mp_read_session_response_process);
	init_comm_element(&qstate->response, CET_MP_READ_SESSION_READ_RESPONSE);
	read_response = get_cache_mp_read_session_read_response(
		&qstate->response);

	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	read_response->error_code = cache_mp_read(
		(cache_mp_read_session)qstate->mdata, NULL,
		&read_response->data_size);

	if (read_response->error_code == 0) {
		read_response->data = malloc(read_response->data_size);
		assert(read_response != NULL);
		read_response->error_code = cache_mp_read(
			(cache_mp_read_session)qstate->mdata,
	    		read_response->data,
			&read_response->data_size);
	}
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);

	if (read_response->error_code == 0)
		qstate->kevent_watermark = sizeof(size_t) + sizeof(int);
	else
		qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_mp_read_session_read_response_write1;
	qstate->kevent_filter = EVFILT_WRITE;

	TRACE_OUT(on_mp_read_session_response_process);
	return (0);
}

static int
on_mp_read_session_read_response_write1(struct query_state *qstate)
{
	struct cache_mp_read_session_read_response	*read_response;
	ssize_t	result;

	TRACE_IN(on_mp_read_session_read_response_write1);
	read_response = get_cache_mp_read_session_read_response(
		&qstate->response);

	result = qstate->write_func(qstate, &read_response->error_code,
		sizeof(int));
	if (read_response->error_code == 0) {
		result += qstate->write_func(qstate, &read_response->data_size,
			sizeof(size_t));
		if (result < 0 || (size_t)result != qstate->kevent_watermark) {
			TRACE_OUT(on_mp_read_session_read_response_write1);
			LOG_ERR_3("on_mp_read_session_read_response_write1",
				"write failed");
			return (-1);
		}

		qstate->kevent_watermark = read_response->data_size;
		qstate->process_func = on_mp_read_session_read_response_write2;
	} else {
		if (result < 0 || (size_t)result != qstate->kevent_watermark) {
			LOG_ERR_3("on_mp_read_session_read_response_write1",
				"write failed");
			TRACE_OUT(on_mp_read_session_read_response_write1);
			return (-1);
		}

		qstate->kevent_watermark = 0;
		qstate->process_func = NULL;
	}

	TRACE_OUT(on_mp_read_session_read_response_write1);
	return (0);
}

static int
on_mp_read_session_read_response_write2(struct query_state *qstate)
{
	struct cache_mp_read_session_read_response *read_response;
	ssize_t	result;

	TRACE_IN(on_mp_read_session_read_response_write2);
	read_response = get_cache_mp_read_session_read_response(
		&qstate->response);
	result = qstate->write_func(qstate, read_response->data,
		read_response->data_size);
	if (result < 0 || (size_t)result != qstate->kevent_watermark) {
		LOG_ERR_3("on_mp_read_session_read_response_write2",
			"write failed");
		TRACE_OUT(on_mp_read_session_read_response_write2);
		return (-1);
	}

	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_mp_read_session_mapper;
	qstate->kevent_filter = EVFILT_READ;

	TRACE_OUT(on_mp_read_session_read_response_write2);
	return (0);
}

/*
 * Handles session close notification by calling close_cache_mp_read_session
 * function.
 */
static int
on_mp_read_session_close_notification(struct query_state *qstate)
{

	TRACE_IN(on_mp_read_session_close_notification);
	configuration_lock_entry(qstate->config_entry, CELT_MULTIPART);
	close_cache_mp_read_session((cache_mp_read_session)qstate->mdata);
	configuration_unlock_entry(qstate->config_entry, CELT_MULTIPART);
	qstate->mdata = NULL;
	qstate->kevent_watermark = 0;
	qstate->process_func = NULL;
	TRACE_OUT(on_mp_read_session_close_notification);
	return (0);
}
