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
#define _NS_PRIVATE
#include <nsswitch.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"
#include "nscachedcli.h"
#include "nscache.h"

#define NSS_CACHE_KEY_INITIAL_SIZE	(256)
#define NSS_CACHE_KEY_SIZE_LIMIT	(NSS_CACHE_KEY_INITIAL_SIZE << 4)

#define NSS_CACHE_BUFFER_INITIAL_SIZE	(1024)
#define NSS_CACHE_BUFFER_SIZE_LIMIT	(NSS_CACHE_BUFFER_INITIAL_SIZE << 8)

#define CACHED_SOCKET_PATH 		"/var/run/nscd"

int
__nss_cache_handler(void *retval, void *mdata, va_list ap)
{
	return (NS_UNAVAIL);
}

int
__nss_common_cache_read(void *retval, void *mdata, va_list ap)
{
	struct cached_connection_params params;
	cached_connection connection;

	char *buffer;
	size_t buffer_size, size;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;
	va_list ap_new;
	int res;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	memset(&params, 0, sizeof(struct cached_connection_params));
	params.socket_path = CACHED_SOCKET_PATH;

	cache_data->key = (char *)malloc(NSS_CACHE_KEY_INITIAL_SIZE);
	memset(cache_data->key, 0, NSS_CACHE_KEY_INITIAL_SIZE);
	cache_data->key_size = NSS_CACHE_KEY_INITIAL_SIZE;
	va_copy(ap_new, ap);

	do {
		size = cache_data->key_size;
		res = cache_info->id_func(cache_data->key, &size, ap_new,
		    cache_info->mdata);
		va_end(ap_new);
		if (res == NS_RETURN) {
			if (cache_data->key_size > NSS_CACHE_KEY_SIZE_LIMIT)
				break;

			cache_data->key_size <<= 1;
			cache_data->key = realloc(cache_data->key,
			    cache_data->key_size);
			memset(cache_data->key, 0, cache_data->key_size);
			va_copy(ap_new, ap);
		}
	} while (res == NS_RETURN);

	if (res != NS_SUCCESS) {
		free(cache_data->key);
		cache_data->key = NULL;
		cache_data->key_size = 0;
		return (res);
	} else
		cache_data->key_size = size;

	buffer_size = NSS_CACHE_BUFFER_INITIAL_SIZE;
	buffer = (char *)malloc(NSS_CACHE_BUFFER_INITIAL_SIZE);
	memset(buffer, 0, NSS_CACHE_BUFFER_INITIAL_SIZE);

	do {
		connection = __open_cached_connection(&params);
		if (connection == NULL) {
			res = -1;
			break;
		}
		res = __cached_read(connection, cache_info->entry_name,
		    cache_data->key, cache_data->key_size, buffer,
		    &buffer_size);
		__close_cached_connection(connection);
		if (res == -2 && buffer_size < NSS_CACHE_BUFFER_SIZE_LIMIT) {
			buffer = (char *)realloc(buffer, buffer_size);
			memset(buffer, 0, buffer_size);
		}
	} while (res == -2);

	if (res == 0) {
		if (buffer_size == 0) {
			free(buffer);
			free(cache_data->key);
			cache_data->key = NULL;
			cache_data->key_size = 0;
			return (NS_RETURN);
		}

		va_copy(ap_new, ap);
		res = cache_info->unmarshal_func(buffer, buffer_size, retval,
		    ap_new, cache_info->mdata);
		va_end(ap_new);

		if (res != NS_SUCCESS) {
			free(buffer);
			free(cache_data->key);
			cache_data->key = NULL;
			cache_data->key_size = 0;
			return (res);
		} else
			res = 0;
	}

	if (res == 0) {
		free(cache_data->key);
		cache_data->key = NULL;
		cache_data->key_size = 0;
	}

	free(buffer);
	return (res == 0 ? NS_SUCCESS : NS_NOTFOUND);
}

int
__nss_common_cache_write(void *retval, void *mdata, va_list ap)
{
	struct cached_connection_params params;
	cached_connection connection;

	char *buffer;
	size_t buffer_size;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;
	va_list ap_new;
	int res;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	if (cache_data->key == NULL)
		return (NS_UNAVAIL);

	memset(&params, 0, sizeof(struct cached_connection_params));
	params.socket_path = CACHED_SOCKET_PATH;

	connection = __open_cached_connection(&params);
	if (connection == NULL) {
		free(cache_data->key);
		return (NS_UNAVAIL);
	}

	buffer_size = NSS_CACHE_BUFFER_INITIAL_SIZE;
	buffer = (char *)malloc(NSS_CACHE_BUFFER_INITIAL_SIZE);
	memset(buffer, 0, NSS_CACHE_BUFFER_INITIAL_SIZE);

	do {
		size_t size;

		size = buffer_size;
		va_copy(ap_new, ap);
		res = cache_info->marshal_func(buffer, &size, retval, ap_new,
		    cache_info->mdata);
		va_end(ap_new);

		if (res == NS_RETURN) {
			if (buffer_size > NSS_CACHE_BUFFER_SIZE_LIMIT)
				break;

			buffer_size <<= 1;
			buffer = (char *)realloc(buffer, buffer_size);
			memset(buffer, 0, buffer_size);
		}
	} while (res == NS_RETURN);

	if (res != NS_SUCCESS) {
		__close_cached_connection(connection);
		free(cache_data->key);
		free(buffer);
		return (res);
	}

	res = __cached_write(connection, cache_info->entry_name,
	    cache_data->key, cache_data->key_size, buffer, buffer_size);
	__close_cached_connection(connection);

	free(cache_data->key);
	free(buffer);

	return (res == 0 ? NS_SUCCESS : NS_UNAVAIL);
}

int
__nss_common_cache_write_negative(void *mdata)
{
	struct cached_connection_params params;
	cached_connection connection;
	int res;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	if (cache_data->key == NULL)
		return (NS_UNAVAIL);

	memset(&params, 0, sizeof(struct cached_connection_params));
	params.socket_path = CACHED_SOCKET_PATH;

	connection = __open_cached_connection(&params);
	if (connection == NULL) {
		free(cache_data->key);
		return (NS_UNAVAIL);
	}

	res = __cached_write(connection, cache_info->entry_name,
	    cache_data->key, cache_data->key_size, NULL, 0);
	__close_cached_connection(connection);

	free(cache_data->key);
	return (res == 0 ? NS_SUCCESS : NS_UNAVAIL);
}

int
__nss_mp_cache_read(void *retval, void *mdata, va_list ap)
{
	struct cached_connection_params params;
	cached_mp_read_session rs;

	char *buffer;
	size_t buffer_size;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;
	va_list ap_new;
	int res;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	if (cache_info->get_mp_ws_func() != INVALID_CACHED_MP_WRITE_SESSION)
		return (NS_UNAVAIL);

	rs = cache_info->get_mp_rs_func();
	if (rs == INVALID_CACHED_MP_READ_SESSION) {
		memset(&params, 0, sizeof(struct cached_connection_params));
		params.socket_path = CACHED_SOCKET_PATH;

		rs = __open_cached_mp_read_session(&params,
		    cache_info->entry_name);
		if (rs == INVALID_CACHED_MP_READ_SESSION)
			return (NS_UNAVAIL);

		cache_info->set_mp_rs_func(rs);
	}

	buffer_size = NSS_CACHE_BUFFER_INITIAL_SIZE;
	buffer = (char *)malloc(NSS_CACHE_BUFFER_INITIAL_SIZE);
	memset(buffer, 0, NSS_CACHE_BUFFER_INITIAL_SIZE);

	do {
		res = __cached_mp_read(rs, buffer, &buffer_size);
		if (res == -2 && buffer_size < NSS_CACHE_BUFFER_SIZE_LIMIT) {
			buffer = (char *)realloc(buffer, buffer_size);
			memset(buffer, 0, buffer_size);
		}
	} while (res == -2);

	if (res == 0) {
		va_copy(ap_new, ap);
		res = cache_info->unmarshal_func(buffer, buffer_size, retval,
		    ap_new, cache_info->mdata);
		va_end(ap_new);

		if (res != NS_SUCCESS) {
			free(buffer);
			return (res);
		} else
			res = 0;
	} else {
		free(buffer);
		__close_cached_mp_read_session(rs);
		rs = INVALID_CACHED_MP_READ_SESSION;
		cache_info->set_mp_rs_func(rs);
		return (res == -1 ? NS_RETURN : NS_UNAVAIL);
	}

	free(buffer);
	return (res == 0 ? NS_SUCCESS : NS_NOTFOUND);
}

int
__nss_mp_cache_write(void *retval, void *mdata, va_list ap)
{
	struct cached_connection_params params;
	cached_mp_write_session ws;

	char *buffer;
	size_t buffer_size;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;
	va_list ap_new;
	int res;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	ws = cache_info->get_mp_ws_func();
	if (ws == INVALID_CACHED_MP_WRITE_SESSION) {
		memset(&params, 0, sizeof(struct cached_connection_params));
		params.socket_path = CACHED_SOCKET_PATH;

		ws = __open_cached_mp_write_session(&params,
		    cache_info->entry_name);
		if (ws == INVALID_CACHED_MP_WRITE_SESSION)
			return (NS_UNAVAIL);

		cache_info->set_mp_ws_func(ws);
	}

	buffer_size = NSS_CACHE_BUFFER_INITIAL_SIZE;
	buffer = (char *)malloc(NSS_CACHE_BUFFER_INITIAL_SIZE);
	memset(buffer, 0, NSS_CACHE_BUFFER_INITIAL_SIZE);

	do {
		size_t size;

		size = buffer_size;
		va_copy(ap_new, ap);
		res = cache_info->marshal_func(buffer, &size, retval, ap_new,
		    cache_info->mdata);
		va_end(ap_new);

		if (res == NS_RETURN) {
			if (buffer_size > NSS_CACHE_BUFFER_SIZE_LIMIT)
				break;

			buffer_size <<= 1;
			buffer = (char *)realloc(buffer, buffer_size);
			memset(buffer, 0, buffer_size);
		}
	} while (res == NS_RETURN);

	if (res != NS_SUCCESS) {
		free(buffer);
		return (res);
	}

	res = __cached_mp_write(ws, buffer, buffer_size);

	free(buffer);
	return (res == 0 ? NS_SUCCESS : NS_UNAVAIL);
}

int
__nss_mp_cache_write_submit(void *retval, void *mdata, va_list ap)
{
	cached_mp_write_session ws;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	ws = cache_info->get_mp_ws_func();
	if (ws != INVALID_CACHED_MP_WRITE_SESSION) {
		__close_cached_mp_write_session(ws);
		ws = INVALID_CACHED_MP_WRITE_SESSION;
		cache_info->set_mp_ws_func(ws);
	}
	return (NS_UNAVAIL);
}

int
__nss_mp_cache_end(void *retval, void *mdata, va_list ap)
{
	cached_mp_write_session ws;
	cached_mp_read_session rs;

	nss_cache_info const *cache_info;
	nss_cache_data *cache_data;

	cache_data = (nss_cache_data *)mdata;
	cache_info = cache_data->info;

	ws = cache_info->get_mp_ws_func();
	if (ws != INVALID_CACHED_MP_WRITE_SESSION) {
		__abandon_cached_mp_write_session(ws);
		ws = INVALID_CACHED_MP_WRITE_SESSION;
		cache_info->set_mp_ws_func(ws);
	}

	rs = cache_info->get_mp_rs_func();
	if (rs != INVALID_CACHED_MP_READ_SESSION) {
		__close_cached_mp_read_session(rs);
		rs = INVALID_CACHED_MP_READ_SESSION;
		cache_info->set_mp_rs_func(rs);
	}

	return (NS_UNAVAIL);
}
