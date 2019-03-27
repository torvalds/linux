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
#include <unistd.h>

#include "config.h"
#include "debug.h"
#include "query.h"
#include "log.h"
#include "mp_ws_query.h"
#include "mp_rs_query.h"
#include "singletons.h"

static const char negative_data[1] = { 0 };

extern	void get_time_func(struct timeval *);

static 	void clear_config_entry(struct configuration_entry *);
static 	void clear_config_entry_part(struct configuration_entry *,
	const char *, size_t);

static	int on_query_startup(struct query_state *);
static	void on_query_destroy(struct query_state *);

static	int on_read_request_read1(struct query_state *);
static	int on_read_request_read2(struct query_state *);
static	int on_read_request_process(struct query_state *);
static	int on_read_response_write1(struct query_state *);
static	int on_read_response_write2(struct query_state *);

static	int on_rw_mapper(struct query_state *);

static	int on_transform_request_read1(struct query_state *);
static	int on_transform_request_read2(struct query_state *);
static	int on_transform_request_process(struct query_state *);
static	int on_transform_response_write1(struct query_state *);

static	int on_write_request_read1(struct query_state *);
static	int on_write_request_read2(struct query_state *);
static	int on_negative_write_request_process(struct query_state *);
static	int on_write_request_process(struct query_state *);
static	int on_write_response_write1(struct query_state *);

/*
 * Clears the specified configuration entry (clears the cache for positive and
 * and negative entries) and also for all multipart entries.
 */
static void
clear_config_entry(struct configuration_entry *config_entry)
{
	size_t i;

	TRACE_IN(clear_config_entry);
	configuration_lock_entry(config_entry, CELT_POSITIVE);
	if (config_entry->positive_cache_entry != NULL)
		transform_cache_entry(
			config_entry->positive_cache_entry,
			CTT_CLEAR);
	configuration_unlock_entry(config_entry, CELT_POSITIVE);

	configuration_lock_entry(config_entry, CELT_NEGATIVE);
	if (config_entry->negative_cache_entry != NULL)
		transform_cache_entry(
			config_entry->negative_cache_entry,
			CTT_CLEAR);
	configuration_unlock_entry(config_entry, CELT_NEGATIVE);

	configuration_lock_entry(config_entry, CELT_MULTIPART);
	for (i = 0; i < config_entry->mp_cache_entries_size; ++i)
		transform_cache_entry(
			config_entry->mp_cache_entries[i],
			CTT_CLEAR);
	configuration_unlock_entry(config_entry, CELT_MULTIPART);

	TRACE_OUT(clear_config_entry);
}

/*
 * Clears the specified configuration entry by deleting only the elements,
 * that are owned by the user with specified eid_str.
 */
static void
clear_config_entry_part(struct configuration_entry *config_entry,
	const char *eid_str, size_t eid_str_length)
{
	cache_entry *start, *finish, *mp_entry;
	TRACE_IN(clear_config_entry_part);
	configuration_lock_entry(config_entry, CELT_POSITIVE);
	if (config_entry->positive_cache_entry != NULL)
		transform_cache_entry_part(
			config_entry->positive_cache_entry,
			CTT_CLEAR, eid_str, eid_str_length, KPPT_LEFT);
	configuration_unlock_entry(config_entry, CELT_POSITIVE);

	configuration_lock_entry(config_entry, CELT_NEGATIVE);
	if (config_entry->negative_cache_entry != NULL)
		transform_cache_entry_part(
			config_entry->negative_cache_entry,
			CTT_CLEAR, eid_str, eid_str_length, KPPT_LEFT);
	configuration_unlock_entry(config_entry, CELT_NEGATIVE);

	configuration_lock_entry(config_entry, CELT_MULTIPART);
	if (configuration_entry_find_mp_cache_entries(config_entry,
		eid_str, &start, &finish) == 0) {
		for (mp_entry = start; mp_entry != finish; ++mp_entry)
			transform_cache_entry(*mp_entry, CTT_CLEAR);
	}
	configuration_unlock_entry(config_entry, CELT_MULTIPART);

	TRACE_OUT(clear_config_entry_part);
}

/*
 * This function is assigned to the query_state structue on its creation.
 * It's main purpose is to receive credentials from the client.
 */
static int
on_query_startup(struct query_state *qstate)
{
	union {
		struct cmsghdr hdr;
		char pad[CMSG_SPACE(sizeof(struct cmsgcred))];
	} cmsg;
	struct msghdr mhdr;
	struct iovec iov;
	struct cmsgcred *cred;
	int elem_type;

	TRACE_IN(on_query_startup);
	assert(qstate != NULL);

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = &cmsg;
	mhdr.msg_controllen = sizeof(cmsg);

	memset(&iov, 0, sizeof(iov));
	iov.iov_base = &elem_type;
	iov.iov_len = sizeof(elem_type);

	if (recvmsg(qstate->sockfd, &mhdr, 0) == -1) {
		TRACE_OUT(on_query_startup);
		return (-1);
	}

	if (mhdr.msg_controllen != CMSG_SPACE(sizeof(struct cmsgcred)) ||
	    cmsg.hdr.cmsg_len != CMSG_LEN(sizeof(struct cmsgcred)) ||
	    cmsg.hdr.cmsg_level != SOL_SOCKET ||
	    cmsg.hdr.cmsg_type != SCM_CREDS) {
		TRACE_OUT(on_query_startup);
		return (-1);
	}

	cred = (struct cmsgcred *)CMSG_DATA(&cmsg);
	qstate->uid = cred->cmcred_uid;
	qstate->gid = cred->cmcred_gid;

#if defined(NS_NSCD_EID_CHECKING) || defined(NS_STRICT_NSCD_EID_CHECKING)
/*
 * This check is probably a bit redundant - per-user cache is always separated
 * by the euid/egid pair
 */
	if (check_query_eids(qstate) != 0) {
#ifdef NS_STRICT_NSCD_EID_CHECKING
		TRACE_OUT(on_query_startup);
		return (-1);
#else
		if ((elem_type != CET_READ_REQUEST) &&
		    (elem_type != CET_MP_READ_SESSION_REQUEST) &&
		    (elem_type != CET_WRITE_REQUEST) &&
		    (elem_type != CET_MP_WRITE_SESSION_REQUEST)) {
			TRACE_OUT(on_query_startup);
			return (-1);
		}
#endif
	}
#endif

	switch (elem_type) {
	case CET_WRITE_REQUEST:
		qstate->process_func = on_write_request_read1;
		break;
	case CET_READ_REQUEST:
		qstate->process_func = on_read_request_read1;
		break;
	case CET_TRANSFORM_REQUEST:
		qstate->process_func = on_transform_request_read1;
		break;
	case CET_MP_WRITE_SESSION_REQUEST:
		qstate->process_func = on_mp_write_session_request_read1;
		break;
	case CET_MP_READ_SESSION_REQUEST:
		qstate->process_func = on_mp_read_session_request_read1;
		break;
	default:
		TRACE_OUT(on_query_startup);
		return (-1);
	}

	qstate->kevent_watermark = 0;
	TRACE_OUT(on_query_startup);
	return (0);
}

/*
 * on_rw_mapper is used to process multiple read/write requests during
 * one connection session. It's never called in the beginning (on query_state
 * creation) as it does not process the multipart requests and does not
 * receive credentials
 */
static int
on_rw_mapper(struct query_state *qstate)
{
	ssize_t	result;
	int	elem_type;

	TRACE_IN(on_rw_mapper);
	if (qstate->kevent_watermark == 0) {
		qstate->kevent_watermark = sizeof(int);
	} else {
		result = qstate->read_func(qstate, &elem_type, sizeof(int));
		if (result != sizeof(int)) {
			TRACE_OUT(on_rw_mapper);
			return (-1);
		}

		switch (elem_type) {
		case CET_WRITE_REQUEST:
			qstate->kevent_watermark = sizeof(size_t);
			qstate->process_func = on_write_request_read1;
		break;
		case CET_READ_REQUEST:
			qstate->kevent_watermark = sizeof(size_t);
			qstate->process_func = on_read_request_read1;
		break;
		default:
			TRACE_OUT(on_rw_mapper);
			return (-1);
		break;
		}
	}
	TRACE_OUT(on_rw_mapper);
	return (0);
}

/*
 * The default query_destroy function
 */
static void
on_query_destroy(struct query_state *qstate)
{

	TRACE_IN(on_query_destroy);
	finalize_comm_element(&qstate->response);
	finalize_comm_element(&qstate->request);
	TRACE_OUT(on_query_destroy);
}

/*
 * The functions below are used to process write requests.
 * - on_write_request_read1 and on_write_request_read2 read the request itself
 * - on_write_request_process processes it (if the client requests to
 *    cache the negative result, the on_negative_write_request_process is used)
 * - on_write_response_write1 sends the response
 */
static int
on_write_request_read1(struct query_state *qstate)
{
	struct cache_write_request	*write_request;
	ssize_t	result;

	TRACE_IN(on_write_request_read1);
	if (qstate->kevent_watermark == 0)
		qstate->kevent_watermark = sizeof(size_t) * 3;
	else {
		init_comm_element(&qstate->request, CET_WRITE_REQUEST);
		write_request = get_cache_write_request(&qstate->request);

		result = qstate->read_func(qstate, &write_request->entry_length,
	    		sizeof(size_t));
		result += qstate->read_func(qstate,
	    		&write_request->cache_key_size, sizeof(size_t));
		result += qstate->read_func(qstate,
	    		&write_request->data_size, sizeof(size_t));

		if (result != sizeof(size_t) * 3) {
			TRACE_OUT(on_write_request_read1);
			return (-1);
		}

		if (BUFSIZE_INVALID(write_request->entry_length) ||
			BUFSIZE_INVALID(write_request->cache_key_size) ||
			(BUFSIZE_INVALID(write_request->data_size) &&
			(write_request->data_size != 0))) {
			TRACE_OUT(on_write_request_read1);
			return (-1);
		}

		write_request->entry = calloc(1,
			write_request->entry_length + 1);
		assert(write_request->entry != NULL);

		write_request->cache_key = calloc(1,
			write_request->cache_key_size +
			qstate->eid_str_length);
		assert(write_request->cache_key != NULL);
		memcpy(write_request->cache_key, qstate->eid_str,
			qstate->eid_str_length);

		if (write_request->data_size != 0) {
			write_request->data = calloc(1,
				write_request->data_size);
			assert(write_request->data != NULL);
		}

		qstate->kevent_watermark = write_request->entry_length +
			write_request->cache_key_size +
			write_request->data_size;
		qstate->process_func = on_write_request_read2;
	}

	TRACE_OUT(on_write_request_read1);
	return (0);
}

static int
on_write_request_read2(struct query_state *qstate)
{
	struct cache_write_request	*write_request;
	ssize_t	result;

	TRACE_IN(on_write_request_read2);
	write_request = get_cache_write_request(&qstate->request);

	result = qstate->read_func(qstate, write_request->entry,
		write_request->entry_length);
	result += qstate->read_func(qstate, write_request->cache_key +
		qstate->eid_str_length, write_request->cache_key_size);
	if (write_request->data_size != 0)
		result += qstate->read_func(qstate, write_request->data,
			write_request->data_size);

	if (result != (ssize_t)qstate->kevent_watermark) {
		TRACE_OUT(on_write_request_read2);
		return (-1);
	}
	write_request->cache_key_size += qstate->eid_str_length;

	qstate->kevent_watermark = 0;
	if (write_request->data_size != 0)
		qstate->process_func = on_write_request_process;
	else
	    	qstate->process_func = on_negative_write_request_process;
	TRACE_OUT(on_write_request_read2);
	return (0);
}

static	int
on_write_request_process(struct query_state *qstate)
{
	struct cache_write_request	*write_request;
	struct cache_write_response	*write_response;
	cache_entry c_entry;

	TRACE_IN(on_write_request_process);
	init_comm_element(&qstate->response, CET_WRITE_RESPONSE);
	write_response = get_cache_write_response(&qstate->response);
	write_request = get_cache_write_request(&qstate->request);

	qstate->config_entry = configuration_find_entry(
		s_configuration, write_request->entry);

	if (qstate->config_entry == NULL) {
		write_response->error_code = ENOENT;

		LOG_ERR_2("write_request", "can't find configuration"
		    " entry '%s'. aborting request", write_request->entry);
		goto fin;
	}

	if (qstate->config_entry->enabled == 0) {
		write_response->error_code = EACCES;

		LOG_ERR_2("write_request",
			"configuration entry '%s' is disabled",
			write_request->entry);
		goto fin;
	}

	if (qstate->config_entry->perform_actual_lookups != 0) {
		write_response->error_code = EOPNOTSUPP;

		LOG_ERR_2("write_request",
			"entry '%s' performs lookups by itself: "
			"can't write to it", write_request->entry);
		goto fin;
	}

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache,
		qstate->config_entry->positive_cache_params.cep.entry_name);
	configuration_unlock(s_configuration);
	if (c_entry != NULL) {
		configuration_lock_entry(qstate->config_entry, CELT_POSITIVE);
		qstate->config_entry->positive_cache_entry = c_entry;
		write_response->error_code = cache_write(c_entry,
			write_request->cache_key,
	    		write_request->cache_key_size,
	    		write_request->data,
			write_request->data_size);
		configuration_unlock_entry(qstate->config_entry, CELT_POSITIVE);

		if ((qstate->config_entry->common_query_timeout.tv_sec != 0) ||
		    (qstate->config_entry->common_query_timeout.tv_usec != 0))
			memcpy(&qstate->timeout,
				&qstate->config_entry->common_query_timeout,
				sizeof(struct timeval));

	} else
		write_response->error_code = -1;

fin:
	qstate->kevent_filter = EVFILT_WRITE;
	qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_write_response_write1;

	TRACE_OUT(on_write_request_process);
	return (0);
}

static int
on_negative_write_request_process(struct query_state *qstate)
{
	struct cache_write_request	*write_request;
	struct cache_write_response	*write_response;
	cache_entry c_entry;

	TRACE_IN(on_negative_write_request_process);
	init_comm_element(&qstate->response, CET_WRITE_RESPONSE);
	write_response = get_cache_write_response(&qstate->response);
	write_request = get_cache_write_request(&qstate->request);

	qstate->config_entry = configuration_find_entry	(
		s_configuration, write_request->entry);

	if (qstate->config_entry == NULL) {
		write_response->error_code = ENOENT;

		LOG_ERR_2("negative_write_request",
			"can't find configuration"
		   	" entry '%s'. aborting request", write_request->entry);
		goto fin;
	}

	if (qstate->config_entry->enabled == 0) {
		write_response->error_code = EACCES;

		LOG_ERR_2("negative_write_request",
			"configuration entry '%s' is disabled",
			write_request->entry);
		goto fin;
	}

	if (qstate->config_entry->perform_actual_lookups != 0) {
		write_response->error_code = EOPNOTSUPP;

		LOG_ERR_2("negative_write_request",
			"entry '%s' performs lookups by itself: "
			"can't write to it", write_request->entry);
		goto fin;
	} else {
#ifdef NS_NSCD_EID_CHECKING
		if (check_query_eids(qstate) != 0) {
			write_response->error_code = EPERM;
			goto fin;
		}
#endif
	}

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache,
		qstate->config_entry->negative_cache_params.cep.entry_name);
	configuration_unlock(s_configuration);
	if (c_entry != NULL) {
		configuration_lock_entry(qstate->config_entry, CELT_NEGATIVE);
		qstate->config_entry->negative_cache_entry = c_entry;
		write_response->error_code = cache_write(c_entry,
			write_request->cache_key,
	    		write_request->cache_key_size,
	    		negative_data,
			sizeof(negative_data));
		configuration_unlock_entry(qstate->config_entry, CELT_NEGATIVE);

		if ((qstate->config_entry->common_query_timeout.tv_sec != 0) ||
		    (qstate->config_entry->common_query_timeout.tv_usec != 0))
			memcpy(&qstate->timeout,
				&qstate->config_entry->common_query_timeout,
				sizeof(struct timeval));
	} else
		write_response->error_code = -1;

fin:
	qstate->kevent_filter = EVFILT_WRITE;
	qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_write_response_write1;

	TRACE_OUT(on_negative_write_request_process);
	return (0);
}

static int
on_write_response_write1(struct query_state *qstate)
{
	struct cache_write_response	*write_response;
	ssize_t	result;

	TRACE_IN(on_write_response_write1);
	write_response = get_cache_write_response(&qstate->response);
	result = qstate->write_func(qstate, &write_response->error_code,
		sizeof(int));
	if (result != sizeof(int)) {
		TRACE_OUT(on_write_response_write1);
		return (-1);
	}

	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	qstate->kevent_watermark = sizeof(int);
	qstate->kevent_filter = EVFILT_READ;
	qstate->process_func = on_rw_mapper;

	TRACE_OUT(on_write_response_write1);
	return (0);
}

/*
 * The functions below are used to process read requests.
 * - on_read_request_read1 and on_read_request_read2 read the request itself
 * - on_read_request_process processes it
 * - on_read_response_write1 and on_read_response_write2 send the response
 */
static int
on_read_request_read1(struct query_state *qstate)
{
	struct cache_read_request *read_request;
	ssize_t	result;

	TRACE_IN(on_read_request_read1);
	if (qstate->kevent_watermark == 0)
		qstate->kevent_watermark = sizeof(size_t) * 2;
	else {
		init_comm_element(&qstate->request, CET_READ_REQUEST);
		read_request = get_cache_read_request(&qstate->request);

		result = qstate->read_func(qstate,
	    		&read_request->entry_length, sizeof(size_t));
		result += qstate->read_func(qstate,
	    		&read_request->cache_key_size, sizeof(size_t));

		if (result != sizeof(size_t) * 2) {
			TRACE_OUT(on_read_request_read1);
			return (-1);
		}

		if (BUFSIZE_INVALID(read_request->entry_length) ||
			BUFSIZE_INVALID(read_request->cache_key_size)) {
			TRACE_OUT(on_read_request_read1);
			return (-1);
		}

		read_request->entry = calloc(1,
			read_request->entry_length + 1);
		assert(read_request->entry != NULL);

		read_request->cache_key = calloc(1,
			read_request->cache_key_size +
			qstate->eid_str_length);
		assert(read_request->cache_key != NULL);
		memcpy(read_request->cache_key, qstate->eid_str,
			qstate->eid_str_length);

		qstate->kevent_watermark = read_request->entry_length +
			read_request->cache_key_size;
		qstate->process_func = on_read_request_read2;
	}

	TRACE_OUT(on_read_request_read1);
	return (0);
}

static int
on_read_request_read2(struct query_state *qstate)
{
	struct cache_read_request	*read_request;
	ssize_t	result;

	TRACE_IN(on_read_request_read2);
	read_request = get_cache_read_request(&qstate->request);

	result = qstate->read_func(qstate, read_request->entry,
		read_request->entry_length);
	result += qstate->read_func(qstate,
		read_request->cache_key + qstate->eid_str_length,
		read_request->cache_key_size);

	if (result != (ssize_t)qstate->kevent_watermark) {
		TRACE_OUT(on_read_request_read2);
		return (-1);
	}
	read_request->cache_key_size += qstate->eid_str_length;

	qstate->kevent_watermark = 0;
	qstate->process_func = on_read_request_process;

	TRACE_OUT(on_read_request_read2);
	return (0);
}

static int
on_read_request_process(struct query_state *qstate)
{
	struct cache_read_request *read_request;
	struct cache_read_response *read_response;
	cache_entry	c_entry, neg_c_entry;

	struct agent	*lookup_agent;
	struct common_agent *c_agent;
	int res;

	TRACE_IN(on_read_request_process);
	init_comm_element(&qstate->response, CET_READ_RESPONSE);
	read_response = get_cache_read_response(&qstate->response);
	read_request = get_cache_read_request(&qstate->request);

	qstate->config_entry = configuration_find_entry(
		s_configuration, read_request->entry);
	if (qstate->config_entry == NULL) {
		read_response->error_code = ENOENT;

		LOG_ERR_2("read_request",
			"can't find configuration "
	    		"entry '%s'. aborting request", read_request->entry);
	    	goto fin;
	}

	if (qstate->config_entry->enabled == 0) {
		read_response->error_code = EACCES;

		LOG_ERR_2("read_request",
			"configuration entry '%s' is disabled",
			read_request->entry);
		goto fin;
	}

	/*
	 * if we perform lookups by ourselves, then we don't need to separate
	 * cache entries by euid and egid
	 */
	if (qstate->config_entry->perform_actual_lookups != 0)
		memset(read_request->cache_key, 0, qstate->eid_str_length);
	else {
#ifdef NS_NSCD_EID_CHECKING
		if (check_query_eids(qstate) != 0) {
		/* if the lookup is not self-performing, we check for clients euid/egid */
			read_response->error_code = EPERM;
			goto fin;
		}
#endif
	}

	configuration_lock_rdlock(s_configuration);
	c_entry = find_cache_entry(s_cache,
		qstate->config_entry->positive_cache_params.cep.entry_name);
	neg_c_entry = find_cache_entry(s_cache,
		qstate->config_entry->negative_cache_params.cep.entry_name);
	configuration_unlock(s_configuration);
	if ((c_entry != NULL) && (neg_c_entry != NULL)) {
		configuration_lock_entry(qstate->config_entry, CELT_POSITIVE);
		qstate->config_entry->positive_cache_entry = c_entry;
		read_response->error_code = cache_read(c_entry,
	    		read_request->cache_key,
	    		read_request->cache_key_size, NULL,
	    		&read_response->data_size);

		if (read_response->error_code == -2) {
			read_response->data = malloc(
				read_response->data_size);
			assert(read_response->data != NULL);
			read_response->error_code = cache_read(c_entry,
				read_request->cache_key,
		    		read_request->cache_key_size,
		    		read_response->data,
		    		&read_response->data_size);
		}
		configuration_unlock_entry(qstate->config_entry, CELT_POSITIVE);

		configuration_lock_entry(qstate->config_entry, CELT_NEGATIVE);
		qstate->config_entry->negative_cache_entry = neg_c_entry;
		if (read_response->error_code == -1) {
			read_response->error_code = cache_read(neg_c_entry,
				read_request->cache_key,
				read_request->cache_key_size, NULL,
				&read_response->data_size);

			if (read_response->error_code == -2) {
				read_response->data = malloc(
					read_response->data_size);
				assert(read_response->data != NULL);
				read_response->error_code = cache_read(neg_c_entry,
					read_request->cache_key,
		    			read_request->cache_key_size,
		    			read_response->data,
		    			&read_response->data_size);
			}
		}
		configuration_unlock_entry(qstate->config_entry, CELT_NEGATIVE);

		if ((read_response->error_code == -1) &&
			(qstate->config_entry->perform_actual_lookups != 0)) {
			free(read_response->data);
			read_response->data = NULL;
			read_response->data_size = 0;

			lookup_agent = find_agent(s_agent_table,
				read_request->entry, COMMON_AGENT);

			if ((lookup_agent != NULL) &&
			(lookup_agent->type == COMMON_AGENT)) {
				c_agent = (struct common_agent *)lookup_agent;
				res = c_agent->lookup_func(
					read_request->cache_key +
						qstate->eid_str_length,
					read_request->cache_key_size -
						qstate->eid_str_length,
					&read_response->data,
					&read_response->data_size);

				if (res == NS_SUCCESS) {
					read_response->error_code = 0;
					configuration_lock_entry(
						qstate->config_entry,
						CELT_POSITIVE);
					cache_write(c_entry,
						read_request->cache_key,
	    					read_request->cache_key_size,
	    					read_response->data,
						read_response->data_size);
					configuration_unlock_entry(
						qstate->config_entry,
						CELT_POSITIVE);
				} else if ((res == NS_NOTFOUND) ||
					  (res == NS_RETURN)) {
					configuration_lock_entry(
						  qstate->config_entry,
						  CELT_NEGATIVE);
					cache_write(neg_c_entry,
						read_request->cache_key,
						read_request->cache_key_size,
						negative_data,
						sizeof(negative_data));
					configuration_unlock_entry(
						  qstate->config_entry,
						  CELT_NEGATIVE);

					read_response->error_code = 0;
					read_response->data = NULL;
					read_response->data_size = 0;
				}
			}
		}

		if ((qstate->config_entry->common_query_timeout.tv_sec != 0) ||
		    (qstate->config_entry->common_query_timeout.tv_usec != 0))
			memcpy(&qstate->timeout,
				&qstate->config_entry->common_query_timeout,
				sizeof(struct timeval));
	} else
		read_response->error_code = -1;

fin:
	qstate->kevent_filter = EVFILT_WRITE;
	if (read_response->error_code == 0)
		qstate->kevent_watermark = sizeof(int) + sizeof(size_t);
	else
		qstate->kevent_watermark = sizeof(int);
	qstate->process_func = on_read_response_write1;

	TRACE_OUT(on_read_request_process);
	return (0);
}

static int
on_read_response_write1(struct query_state *qstate)
{
	struct cache_read_response	*read_response;
	ssize_t	result;

	TRACE_IN(on_read_response_write1);
	read_response = get_cache_read_response(&qstate->response);

	result = qstate->write_func(qstate, &read_response->error_code,
		sizeof(int));

	if (read_response->error_code == 0) {
		result += qstate->write_func(qstate, &read_response->data_size,
			sizeof(size_t));
		if (result != (ssize_t)qstate->kevent_watermark) {
			TRACE_OUT(on_read_response_write1);
			return (-1);
		}

		qstate->kevent_watermark = read_response->data_size;
		qstate->process_func = on_read_response_write2;
	} else {
		if (result != (ssize_t)qstate->kevent_watermark) {
			TRACE_OUT(on_read_response_write1);
			return (-1);
		}

		qstate->kevent_watermark = 0;
		qstate->process_func = NULL;
	}

	TRACE_OUT(on_read_response_write1);
	return (0);
}

static int
on_read_response_write2(struct query_state *qstate)
{
	struct cache_read_response	*read_response;
	ssize_t	result;

	TRACE_IN(on_read_response_write2);
	read_response = get_cache_read_response(&qstate->response);
	if (read_response->data_size > 0) {
		result = qstate->write_func(qstate, read_response->data,
			read_response->data_size);
		if (result != (ssize_t)qstate->kevent_watermark) {
			TRACE_OUT(on_read_response_write2);
			return (-1);
		}
	}

	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	qstate->kevent_watermark = sizeof(int);
	qstate->kevent_filter = EVFILT_READ;
	qstate->process_func = on_rw_mapper;
	TRACE_OUT(on_read_response_write2);
	return (0);
}

/*
 * The functions below are used to process write requests.
 * - on_transform_request_read1 and on_transform_request_read2 read the
 *   request itself
 * - on_transform_request_process processes it
 * - on_transform_response_write1 sends the response
 */
static int
on_transform_request_read1(struct query_state *qstate)
{
	struct cache_transform_request *transform_request;
	ssize_t	result;

	TRACE_IN(on_transform_request_read1);
	if (qstate->kevent_watermark == 0)
		qstate->kevent_watermark = sizeof(size_t) + sizeof(int);
	else {
		init_comm_element(&qstate->request, CET_TRANSFORM_REQUEST);
		transform_request =
			get_cache_transform_request(&qstate->request);

		result = qstate->read_func(qstate,
	    		&transform_request->entry_length, sizeof(size_t));
		result += qstate->read_func(qstate,
	    		&transform_request->transformation_type, sizeof(int));

		if (result != sizeof(size_t) + sizeof(int)) {
			TRACE_OUT(on_transform_request_read1);
			return (-1);
		}

		if ((transform_request->transformation_type != TT_USER) &&
		    (transform_request->transformation_type != TT_ALL)) {
			TRACE_OUT(on_transform_request_read1);
			return (-1);
		}

		if (transform_request->entry_length != 0) {
			if (BUFSIZE_INVALID(transform_request->entry_length)) {
				TRACE_OUT(on_transform_request_read1);
				return (-1);
			}

			transform_request->entry = calloc(1,
				transform_request->entry_length + 1);
			assert(transform_request->entry != NULL);

			qstate->process_func = on_transform_request_read2;
		} else
			qstate->process_func = on_transform_request_process;

		qstate->kevent_watermark = transform_request->entry_length;
	}

	TRACE_OUT(on_transform_request_read1);
	return (0);
}

static int
on_transform_request_read2(struct query_state *qstate)
{
	struct cache_transform_request	*transform_request;
	ssize_t	result;

	TRACE_IN(on_transform_request_read2);
	transform_request = get_cache_transform_request(&qstate->request);

	result = qstate->read_func(qstate, transform_request->entry,
		transform_request->entry_length);

	if (result != (ssize_t)qstate->kevent_watermark) {
		TRACE_OUT(on_transform_request_read2);
		return (-1);
	}

	qstate->kevent_watermark = 0;
	qstate->process_func = on_transform_request_process;

	TRACE_OUT(on_transform_request_read2);
	return (0);
}

static int
on_transform_request_process(struct query_state *qstate)
{
	struct cache_transform_request *transform_request;
	struct cache_transform_response *transform_response;
	struct configuration_entry *config_entry;
	size_t	i, size;

	TRACE_IN(on_transform_request_process);
	init_comm_element(&qstate->response, CET_TRANSFORM_RESPONSE);
	transform_response = get_cache_transform_response(&qstate->response);
	transform_request = get_cache_transform_request(&qstate->request);

	switch (transform_request->transformation_type) {
	case TT_USER:
		if (transform_request->entry == NULL) {
			size = configuration_get_entries_size(s_configuration);
			for (i = 0; i < size; ++i) {
			    config_entry = configuration_get_entry(
				s_configuration, i);

			    if (config_entry->perform_actual_lookups == 0)
			    	clear_config_entry_part(config_entry,
				    qstate->eid_str, qstate->eid_str_length);
			}
		} else {
			qstate->config_entry = configuration_find_entry(
				s_configuration, transform_request->entry);

			if (qstate->config_entry == NULL) {
				LOG_ERR_2("transform_request",
					"can't find configuration"
		   			" entry '%s'. aborting request",
					transform_request->entry);
				transform_response->error_code = -1;
				goto fin;
			}

			if (qstate->config_entry->perform_actual_lookups != 0) {
				LOG_ERR_2("transform_request",
					"can't transform the cache entry %s"
					", because it ised for actual lookups",
					transform_request->entry);
				transform_response->error_code = -1;
				goto fin;
			}

			clear_config_entry_part(qstate->config_entry,
				qstate->eid_str, qstate->eid_str_length);
		}
		break;
	case TT_ALL:
		if (qstate->euid != 0)
			transform_response->error_code = -1;
		else {
			if (transform_request->entry == NULL) {
				size = configuration_get_entries_size(
					s_configuration);
				for (i = 0; i < size; ++i) {
				    clear_config_entry(
					configuration_get_entry(
						s_configuration, i));
				}
			} else {
				qstate->config_entry = configuration_find_entry(
					s_configuration,
					transform_request->entry);

				if (qstate->config_entry == NULL) {
					LOG_ERR_2("transform_request",
						"can't find configuration"
		   				" entry '%s'. aborting request",
						transform_request->entry);
					transform_response->error_code = -1;
					goto fin;
				}

				clear_config_entry(qstate->config_entry);
			}
		}
		break;
	default:
		transform_response->error_code = -1;
	}

fin:
	qstate->kevent_watermark = 0;
	qstate->process_func = on_transform_response_write1;
	TRACE_OUT(on_transform_request_process);
	return (0);
}

static int
on_transform_response_write1(struct query_state *qstate)
{
	struct cache_transform_response	*transform_response;
	ssize_t	result;

	TRACE_IN(on_transform_response_write1);
	transform_response = get_cache_transform_response(&qstate->response);
	result = qstate->write_func(qstate, &transform_response->error_code,
		sizeof(int));
	if (result != sizeof(int)) {
		TRACE_OUT(on_transform_response_write1);
		return (-1);
	}

	finalize_comm_element(&qstate->request);
	finalize_comm_element(&qstate->response);

	qstate->kevent_watermark = 0;
	qstate->process_func = NULL;
	TRACE_OUT(on_transform_response_write1);
	return (0);
}

/*
 * Checks if the client's euid and egid do not differ from its uid and gid.
 * Returns 0 on success.
 */
int
check_query_eids(struct query_state *qstate)
{

	return ((qstate->uid != qstate->euid) || (qstate->gid != qstate->egid) ? -1 : 0);
}

/*
 * Uses the qstate fields to process an "alternate" read - when the buffer is
 * too large to be received during one socket read operation
 */
ssize_t
query_io_buffer_read(struct query_state *qstate, void *buf, size_t nbytes)
{
	size_t remaining;
	ssize_t	result;

	TRACE_IN(query_io_buffer_read);
	if ((qstate->io_buffer_size == 0) || (qstate->io_buffer == NULL))
		return (-1);

	assert(qstate->io_buffer_p <=
		qstate->io_buffer + qstate->io_buffer_size);
	remaining = qstate->io_buffer + qstate->io_buffer_size -
		qstate->io_buffer_p;
	if (nbytes < remaining)
		result = nbytes;
	else
		result = remaining;

	memcpy(buf, qstate->io_buffer_p, result);
	qstate->io_buffer_p += result;

	if (remaining == 0) {
		free(qstate->io_buffer);
		qstate->io_buffer = NULL;

		qstate->write_func = query_socket_write;
		qstate->read_func = query_socket_read;
	}

	TRACE_OUT(query_io_buffer_read);
	return (result);
}

/*
 * Uses the qstate fields to process an "alternate" write - when the buffer is
 * too large to be sent during one socket write operation
 */
ssize_t
query_io_buffer_write(struct query_state *qstate, const void *buf,
	size_t nbytes)
{
	size_t remaining;
	ssize_t	result;

	TRACE_IN(query_io_buffer_write);
	if ((qstate->io_buffer_size == 0) || (qstate->io_buffer == NULL))
		return (-1);

	assert(qstate->io_buffer_p <=
		qstate->io_buffer + qstate->io_buffer_size);
	remaining = qstate->io_buffer + qstate->io_buffer_size -
		qstate->io_buffer_p;
	if (nbytes < remaining)
		result = nbytes;
	else
		result = remaining;

	memcpy(qstate->io_buffer_p, buf, result);
	qstate->io_buffer_p += result;

	if (remaining == 0) {
		qstate->use_alternate_io = 1;
		qstate->io_buffer_p = qstate->io_buffer;

		qstate->write_func = query_socket_write;
		qstate->read_func = query_socket_read;
	}

	TRACE_OUT(query_io_buffer_write);
	return (result);
}

/*
 * The default "read" function, which reads data directly from socket
 */
ssize_t
query_socket_read(struct query_state *qstate, void *buf, size_t nbytes)
{
	ssize_t	result;

	TRACE_IN(query_socket_read);
	if (qstate->socket_failed != 0) {
		TRACE_OUT(query_socket_read);
		return (-1);
	}

	result = read(qstate->sockfd, buf, nbytes);
	if (result < 0 || (size_t)result < nbytes)
		qstate->socket_failed = 1;

	TRACE_OUT(query_socket_read);
	return (result);
}

/*
 * The default "write" function, which writes data directly to socket
 */
ssize_t
query_socket_write(struct query_state *qstate, const void *buf, size_t nbytes)
{
	ssize_t	result;

	TRACE_IN(query_socket_write);
	if (qstate->socket_failed != 0) {
		TRACE_OUT(query_socket_write);
		return (-1);
	}

	result = write(qstate->sockfd, buf, nbytes);
	if (result < 0 || (size_t)result < nbytes)
		qstate->socket_failed = 1;

	TRACE_OUT(query_socket_write);
	return (result);
}

/*
 * Initializes the query_state structure by filling it with the default values.
 */
struct query_state *
init_query_state(int sockfd, size_t kevent_watermark, uid_t euid, gid_t egid)
{
	struct query_state	*retval;

	TRACE_IN(init_query_state);
	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->sockfd = sockfd;
	retval->kevent_filter = EVFILT_READ;
	retval->kevent_watermark = kevent_watermark;

	retval->euid = euid;
	retval->egid = egid;
	retval->uid = retval->gid = -1;

	if (asprintf(&retval->eid_str, "%d_%d_", retval->euid,
		retval->egid) == -1) {
		free(retval);
		return (NULL);
	}
	retval->eid_str_length = strlen(retval->eid_str);

	init_comm_element(&retval->request, CET_UNDEFINED);
	init_comm_element(&retval->response, CET_UNDEFINED);
	retval->process_func = on_query_startup;
	retval->destroy_func = on_query_destroy;

	retval->write_func = query_socket_write;
	retval->read_func = query_socket_read;

	get_time_func(&retval->creation_time);
	retval->timeout.tv_sec = s_configuration->query_timeout;
	retval->timeout.tv_usec = 0;

	TRACE_OUT(init_query_state);
	return (retval);
}

void
destroy_query_state(struct query_state *qstate)
{

	TRACE_IN(destroy_query_state);
	if (qstate->eid_str != NULL)
	    free(qstate->eid_str);

	if (qstate->io_buffer != NULL)
		free(qstate->io_buffer);

	qstate->destroy_func(qstate);
	free(qstate);
	TRACE_OUT(destroy_query_state);
}
