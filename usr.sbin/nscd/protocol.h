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

#ifndef __NSCD_PROTOCOL_H__
#define __NSCD_PROTOCOL_H__

/* maximum buffer size to receive - larger buffers are not allowed */
#define MAX_BUFFER_SIZE (1 << 20)

/* buffer size correctness checking routine */
#define BUFSIZE_CORRECT(x) (((x) > 0) && ((x) < MAX_BUFFER_SIZE))
#define BUFSIZE_INVALID(x) (!BUFSIZE_CORRECT(x))

/* structures below represent the data that are sent/received by the daemon */
struct cache_write_request {
	char	*entry;
	char	*cache_key;
	char	*data;

	size_t	entry_length;
	size_t	cache_key_size;
	size_t	data_size;
};

struct cache_write_response {
	int	error_code;
};

struct cache_read_request {
	char	*entry;
	char	*cache_key;

	size_t	entry_length;
	size_t	cache_key_size;
};

struct cache_read_response {
	char	*data;			// ignored if error_code is not 0
	size_t	data_size;		// ignored if error_code is not 0

	int	error_code;
};

enum transformation_type {
	TT_USER = 0,	// transform only the entries of the caller
	TT_ALL = 1	// transform all entries
};

struct cache_transform_request {
	char	*entry; 		// ignored if entry_length is 0
	size_t	entry_length;

	int	transformation_type;
};

struct cache_transform_response {
	int	error_code;
};

struct cache_mp_write_session_request {
	char	*entry;
	size_t	entry_length;
};

struct cache_mp_write_session_response {
	int	error_code;
};

struct cache_mp_write_session_write_request {
	char	*data;
	size_t	data_size;
};

struct cache_mp_write_session_write_response {
	int	error_code;
};

struct cache_mp_read_session_request {
	char	*entry;
	size_t	entry_length;
};

struct cache_mp_read_session_response {
	int	error_code;
};

struct cache_mp_read_session_read_response {
	char	*data;
	size_t	data_size;

	int	error_code;
};


enum comm_element_t {
	CET_UNDEFINED 	= 0,
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
	CET_MP_READ_SESSION_CLOSE_NOTIFICATION = 17,
	CET_MAX = 18
};

/*
 * The comm_element is used as the holder of any known (defined above) data
 * type that is to be sent/received.
 */
struct comm_element {
	union {
	struct cache_write_request c_write_request;
	struct cache_write_response c_write_response;
	struct cache_read_request c_read_request;
	struct cache_read_response c_read_response;
	struct cache_transform_request c_transform_request;
	struct cache_transform_response c_transform_response;

	struct cache_mp_write_session_request c_mp_ws_request;
	struct cache_mp_write_session_response c_mp_ws_response;
	struct cache_mp_write_session_write_request c_mp_ws_write_request;
	struct cache_mp_write_session_write_response c_mp_ws_write_response;

	struct cache_mp_read_session_request c_mp_rs_request;
	struct cache_mp_read_session_response c_mp_rs_response;
	struct cache_mp_read_session_read_response c_mp_rs_read_response;
	} /* anonymous */;
	enum comm_element_t type;
};

void init_comm_element(struct comm_element *, enum comm_element_t type);
void finalize_comm_element(struct comm_element *);

/*
 * For each type of data, there is three functions (init/finalize/get), that
 * used with comm_element structure
 */
void init_cache_write_request(struct cache_write_request *);
void finalize_cache_write_request(struct cache_write_request *);
struct cache_write_request *get_cache_write_request(struct comm_element *);

void init_cache_write_response(struct cache_write_response *);
void finalize_cache_write_response(struct cache_write_response *);
struct cache_write_response *get_cache_write_response(struct comm_element *);

void init_cache_read_request(struct cache_read_request *);
void finalize_cache_read_request(struct cache_read_request *);
struct cache_read_request *get_cache_read_request(struct comm_element *);

void init_cache_read_response(struct cache_read_response *);
void finalize_cache_read_response(struct cache_read_response *);
struct cache_read_response *get_cache_read_response(struct comm_element *);

void init_cache_transform_request(struct cache_transform_request *);
void finalize_cache_transform_request(struct cache_transform_request *);
struct cache_transform_request *get_cache_transform_request(
	struct comm_element *);

void init_cache_transform_response(struct cache_transform_response *);
void finalize_cache_transform_response(struct cache_transform_response *);
struct cache_transform_response *get_cache_transform_response(
	struct comm_element *);

void init_cache_mp_write_session_request(
	struct cache_mp_write_session_request *);
void finalize_cache_mp_write_session_request(
	struct cache_mp_write_session_request *);
struct cache_mp_write_session_request *
    	get_cache_mp_write_session_request(struct comm_element *);

void init_cache_mp_write_session_response(
	struct cache_mp_write_session_response *);
void finalize_cache_mp_write_session_response(
	struct cache_mp_write_session_response *);
struct cache_mp_write_session_response *
	get_cache_mp_write_session_response(struct comm_element *);

void init_cache_mp_write_session_write_request(
	struct cache_mp_write_session_write_request *);
void finalize_cache_mp_write_session_write_request(
	struct cache_mp_write_session_write_request *);
struct cache_mp_write_session_write_request *
	get_cache_mp_write_session_write_request(struct comm_element *);

void init_cache_mp_write_session_write_response(
	struct cache_mp_write_session_write_response *);
void finalize_cache_mp_write_session_write_response(
	struct cache_mp_write_session_write_response *);
struct cache_mp_write_session_write_response *
	get_cache_mp_write_session_write_response(struct comm_element *);

void init_cache_mp_read_session_request(
	struct cache_mp_read_session_request *);
void finalize_cache_mp_read_session_request(
	struct cache_mp_read_session_request *);
struct cache_mp_read_session_request *get_cache_mp_read_session_request(
	struct comm_element *);

void init_cache_mp_read_session_response(
	struct cache_mp_read_session_response *);
void finalize_cache_mp_read_session_response(
	struct cache_mp_read_session_response *);
struct cache_mp_read_session_response *
    	get_cache_mp_read_session_response(struct comm_element *);

void init_cache_mp_read_session_read_response(
	struct cache_mp_read_session_read_response *);
void finalize_cache_mp_read_session_read_response(
	struct cache_mp_read_session_read_response *);
struct cache_mp_read_session_read_response *
	get_cache_mp_read_session_read_response(struct comm_element *);

#endif
