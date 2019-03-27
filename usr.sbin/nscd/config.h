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

#ifndef __NSCD_CONFIG_H__
#define __NSCD_CONFIG_H__

#include "cachelib.h"

#define DEFAULT_QUERY_TIMEOUT		8
#define DEFAULT_THREADS_NUM		8

#define DEFAULT_COMMON_ENTRY_TIMEOUT	10
#define DEFAULT_MP_ENTRY_TIMEOUT	60
#define DEFAULT_CACHE_HT_SIZE		257

#define INITIAL_ENTRIES_CAPACITY	8
#define DEFAULT_SOCKET_PATH		"/var/run/nscd"
#define DEFAULT_PIDFILE_PATH		"/var/run/nscd.pid"

#define DEFAULT_POSITIVE_ELEMENTS_SIZE	(2048)
#define DEFAULT_POSITIVE_LIFETIME 	(3600)
#define DEFAULT_POSITIVE_CONF_THRESH 	(1)

#define DEFAULT_NEGATIVE_ELEMENTS_SIZE	(2048)
#define DEFAULT_NEGATIVE_LIFETIME	(60)
#define DEFAULT_NEGATIVE_CONF_THRESH 	(1) /* (2) ??? */

#define DEFAULT_MULTIPART_ELEMENTS_SIZE	(1024 * 8)
#define DEFAULT_MULITPART_SESSIONS_SIZE	(1024)
#define DEFAULT_MULITPART_LIFETIME	(3600)

extern const char *c_default_entries[6];

/*
 * Configuration entry represents the details of each cache entry in the
 * config file (i.e. passwd or group). Its purpose also is to acquire locks
 * of three different types (for usual read/write caching, for multipart
 * caching and for caching of the negative results) for that cache entry.
 */
struct configuration_entry {
	struct common_cache_entry_params positive_cache_params;
	struct common_cache_entry_params negative_cache_params;
	struct mp_cache_entry_params mp_cache_params;

	/*
	 * configuration_entry holds pointers for all actual cache_entries,
	 * which are used for it. There is one for positive caching, one for
	 * for negative caching, and several (one per each euid/egid) for
	 * multipart caching.
	 */
	cache_entry positive_cache_entry;
	cache_entry negative_cache_entry;

	cache_entry *mp_cache_entries;
	size_t mp_cache_entries_size;

	struct timeval common_query_timeout;
	struct timeval mp_query_timeout;

	char	*name;
	pthread_mutex_t positive_cache_lock;
	pthread_mutex_t negative_cache_lock;
	pthread_mutex_t mp_cache_lock;

	int	perform_actual_lookups;
	int	enabled;
};

/*
 * Contains global configuration options and array of all configuration entries
 */
struct configuration {
	char	*pidfile_path;
	char	*socket_path;

	struct configuration_entry **entries;
	size_t	entries_capacity;
	size_t	entries_size;

	pthread_rwlock_t rwlock;

	mode_t	socket_mode;
	int	force_unlink;
	int	query_timeout;

	int	threads_num;
};

enum config_entry_lock_type {
	CELT_POSITIVE,
	CELT_NEGATIVE,
	CELT_MULTIPART
};

struct configuration *init_configuration(void);
void destroy_configuration(struct configuration *);
void fill_configuration_defaults(struct configuration *);

int add_configuration_entry(struct configuration *,
	struct configuration_entry *);
struct configuration_entry *create_def_configuration_entry(const char *);
void destroy_configuration_entry(struct configuration_entry *);
size_t configuration_get_entries_size(struct configuration *);
struct configuration_entry *configuration_get_entry(struct configuration *,
	size_t);
struct configuration_entry *configuration_find_entry(struct configuration *,
	const char *);

int configuration_entry_add_mp_cache_entry(struct configuration_entry *,
	cache_entry);
cache_entry configuration_entry_find_mp_cache_entry(
	struct configuration_entry *, const char *);
int configuration_entry_find_mp_cache_entries(struct configuration_entry *,
	const char *, cache_entry **, cache_entry **);

void configuration_lock_rdlock(struct configuration *config);
void configuration_lock_wrlock(struct configuration *config);
void configuration_unlock(struct configuration *config);

void configuration_lock_entry(struct configuration_entry *,
	enum config_entry_lock_type);
void configuration_unlock_entry(struct configuration_entry *,
	enum config_entry_lock_type);

#endif
