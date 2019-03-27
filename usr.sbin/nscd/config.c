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

#include <sys/stat.h>
#include <sys/time.h>

#include <assert.h>
#include <math.h>
#include <nsswitch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "log.h"

/*
 * Default entries, which always exist in the configuration
 */
const char *c_default_entries[6] = {
	NSDB_PASSWD,
	NSDB_GROUP,
	NSDB_HOSTS,
	NSDB_SERVICES,
	NSDB_PROTOCOLS,
	NSDB_RPC
	};

static int configuration_entry_cmp(const void *, const void *);
static int configuration_entry_sort_cmp(const void *, const void *);
static int configuration_entry_cache_mp_sort_cmp(const void *, const void *);
static int configuration_entry_cache_mp_cmp(const void *, const void *);
static int configuration_entry_cache_mp_part_cmp(const void *, const void *);
static struct configuration_entry *create_configuration_entry(const char *,
	struct timeval const *, struct timeval const *,
	struct common_cache_entry_params const *,
	struct common_cache_entry_params const *,
	struct mp_cache_entry_params const *);

static int
configuration_entry_sort_cmp(const void *e1, const void *e2)
{
	return (strcmp((*((struct configuration_entry **)e1))->name,
		(*((struct configuration_entry **)e2))->name
		));
}

static int
configuration_entry_cmp(const void *e1, const void *e2)
{
	return (strcmp((const char *)e1,
		(*((struct configuration_entry **)e2))->name
		));
}

static int
configuration_entry_cache_mp_sort_cmp(const void *e1, const void *e2)
{
	return (strcmp((*((cache_entry *)e1))->params->entry_name,
		(*((cache_entry *)e2))->params->entry_name
		));
}

static int
configuration_entry_cache_mp_cmp(const void *e1, const void *e2)
{
	return (strcmp((const char *)e1,
		(*((cache_entry *)e2))->params->entry_name
		));
}

static int
configuration_entry_cache_mp_part_cmp(const void *e1, const void *e2)
{
	return (strncmp((const char *)e1,
		(*((cache_entry *)e2))->params->entry_name,
		strlen((const char *)e1)
		));
}

static struct configuration_entry *
create_configuration_entry(const char *name,
	struct timeval const *common_timeout,
	struct timeval const *mp_timeout,
	struct common_cache_entry_params const *positive_params,
	struct common_cache_entry_params const *negative_params,
	struct mp_cache_entry_params const *mp_params)
{
	struct configuration_entry *retval;
	size_t	size;
	int res;

	TRACE_IN(create_configuration_entry);
	assert(name != NULL);
	assert(positive_params != NULL);
	assert(negative_params != NULL);
	assert(mp_params != NULL);

	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	res = pthread_mutex_init(&retval->positive_cache_lock, NULL);
	if (res != 0) {
		free(retval);
		LOG_ERR_2("create_configuration_entry",
			"can't create positive cache lock");
		TRACE_OUT(create_configuration_entry);
		return (NULL);
	}

	res = pthread_mutex_init(&retval->negative_cache_lock, NULL);
	if (res != 0) {
		pthread_mutex_destroy(&retval->positive_cache_lock);
		free(retval);
		LOG_ERR_2("create_configuration_entry",
			"can't create negative cache lock");
		TRACE_OUT(create_configuration_entry);
		return (NULL);
	}

	res = pthread_mutex_init(&retval->mp_cache_lock, NULL);
	if (res != 0) {
		pthread_mutex_destroy(&retval->positive_cache_lock);
		pthread_mutex_destroy(&retval->negative_cache_lock);
		free(retval);
		LOG_ERR_2("create_configuration_entry",
			"can't create negative cache lock");
		TRACE_OUT(create_configuration_entry);
		return (NULL);
	}

	memcpy(&retval->positive_cache_params, positive_params,
		sizeof(struct common_cache_entry_params));
	memcpy(&retval->negative_cache_params, negative_params,
		sizeof(struct common_cache_entry_params));
	memcpy(&retval->mp_cache_params, mp_params,
		sizeof(struct mp_cache_entry_params));

	size = strlen(name);
	retval->name = calloc(1, size + 1);
	assert(retval->name != NULL);
	memcpy(retval->name, name, size);

	memcpy(&retval->common_query_timeout, common_timeout,
		sizeof(struct timeval));
	memcpy(&retval->mp_query_timeout, mp_timeout,
		sizeof(struct timeval));

	asprintf(&retval->positive_cache_params.cep.entry_name, "%s+", name);
	assert(retval->positive_cache_params.cep.entry_name != NULL);

	asprintf(&retval->negative_cache_params.cep.entry_name, "%s-", name);
	assert(retval->negative_cache_params.cep.entry_name != NULL);

	asprintf(&retval->mp_cache_params.cep.entry_name, "%s*", name);
	assert(retval->mp_cache_params.cep.entry_name != NULL);

	TRACE_OUT(create_configuration_entry);
	return (retval);
}

/*
 * Creates configuration entry and fills it with default values
 */
struct configuration_entry *
create_def_configuration_entry(const char *name)
{
	struct common_cache_entry_params positive_params, negative_params;
	struct mp_cache_entry_params mp_params;
	struct timeval default_common_timeout, default_mp_timeout;

	struct configuration_entry *res = NULL;

	TRACE_IN(create_def_configuration_entry);
	memset(&positive_params, 0,
		sizeof(struct common_cache_entry_params));
	positive_params.cep.entry_type = CET_COMMON;
	positive_params.cache_entries_size = DEFAULT_CACHE_HT_SIZE;
	positive_params.max_elemsize = DEFAULT_POSITIVE_ELEMENTS_SIZE;
	positive_params.satisf_elemsize = DEFAULT_POSITIVE_ELEMENTS_SIZE / 2;
	positive_params.max_lifetime.tv_sec = DEFAULT_POSITIVE_LIFETIME;
	positive_params.confidence_threshold = DEFAULT_POSITIVE_CONF_THRESH;
	positive_params.policy = CPT_LRU;

	memcpy(&negative_params, &positive_params,
		sizeof(struct common_cache_entry_params));
	negative_params.max_elemsize = DEFAULT_NEGATIVE_ELEMENTS_SIZE;
	negative_params.satisf_elemsize = DEFAULT_NEGATIVE_ELEMENTS_SIZE / 2;
	negative_params.max_lifetime.tv_sec = DEFAULT_NEGATIVE_LIFETIME;
	negative_params.confidence_threshold = DEFAULT_NEGATIVE_CONF_THRESH;
	negative_params.policy = CPT_FIFO;

	memset(&default_common_timeout, 0, sizeof(struct timeval));
	default_common_timeout.tv_sec = DEFAULT_COMMON_ENTRY_TIMEOUT;

	memset(&default_mp_timeout, 0, sizeof(struct timeval));
	default_mp_timeout.tv_sec = DEFAULT_MP_ENTRY_TIMEOUT;

	memset(&mp_params, 0,
		sizeof(struct mp_cache_entry_params));
	mp_params.cep.entry_type = CET_MULTIPART;
	mp_params.max_elemsize = DEFAULT_MULTIPART_ELEMENTS_SIZE;
	mp_params.max_sessions = DEFAULT_MULITPART_SESSIONS_SIZE;
	mp_params.max_lifetime.tv_sec = DEFAULT_MULITPART_LIFETIME;

	res = create_configuration_entry(name, &default_common_timeout,
		&default_mp_timeout, &positive_params, &negative_params,
		&mp_params);

	TRACE_OUT(create_def_configuration_entry);
	return (res);
}

void
destroy_configuration_entry(struct configuration_entry *entry)
{
	TRACE_IN(destroy_configuration_entry);
	assert(entry != NULL);
	pthread_mutex_destroy(&entry->positive_cache_lock);
	pthread_mutex_destroy(&entry->negative_cache_lock);
	pthread_mutex_destroy(&entry->mp_cache_lock);
	free(entry->name);
	free(entry->positive_cache_params.cep.entry_name);
	free(entry->negative_cache_params.cep.entry_name);
	free(entry->mp_cache_params.cep.entry_name);
	free(entry->mp_cache_entries);
	free(entry);
	TRACE_OUT(destroy_configuration_entry);
}

int
add_configuration_entry(struct configuration *config,
	struct configuration_entry *entry)
{
	TRACE_IN(add_configuration_entry);
	assert(entry != NULL);
	assert(entry->name != NULL);
	if (configuration_find_entry(config, entry->name) != NULL) {
		TRACE_OUT(add_configuration_entry);
		return (-1);
	}

	if (config->entries_size == config->entries_capacity) {
		struct configuration_entry **new_entries;

		config->entries_capacity *= 2;
		new_entries = calloc(config->entries_capacity,
			sizeof(*new_entries));
		assert(new_entries != NULL);
		memcpy(new_entries, config->entries,
			sizeof(struct configuration_entry *) *
		        config->entries_size);

		free(config->entries);
		config->entries = new_entries;
	}

	config->entries[config->entries_size++] = entry;
	qsort(config->entries, config->entries_size,
		sizeof(struct configuration_entry *),
		configuration_entry_sort_cmp);

	TRACE_OUT(add_configuration_entry);
	return (0);
}

size_t
configuration_get_entries_size(struct configuration *config)
{
	TRACE_IN(configuration_get_entries_size);
	assert(config != NULL);
	TRACE_OUT(configuration_get_entries_size);
	return (config->entries_size);
}

struct configuration_entry *
configuration_get_entry(struct configuration *config, size_t index)
{
	TRACE_IN(configuration_get_entry);
	assert(config != NULL);
	assert(index < config->entries_size);
	TRACE_OUT(configuration_get_entry);
	return (config->entries[index]);
}

struct configuration_entry *
configuration_find_entry(struct configuration *config,
	const char *name)
{
	struct configuration_entry	**retval;

	TRACE_IN(configuration_find_entry);

	retval = bsearch(name, config->entries, config->entries_size,
		sizeof(struct configuration_entry *), configuration_entry_cmp);
	TRACE_OUT(configuration_find_entry);

	return ((retval != NULL) ? *retval : NULL);
}

/*
 * All multipart cache entries are stored in the configuration_entry in the
 * sorted array (sorted by names). The 3 functions below manage this array.
 */

int
configuration_entry_add_mp_cache_entry(struct configuration_entry *config_entry,
	cache_entry c_entry)
{
	cache_entry *new_mp_entries, *old_mp_entries;

	TRACE_IN(configuration_entry_add_mp_cache_entry);
	++config_entry->mp_cache_entries_size;
	new_mp_entries = malloc(sizeof(*new_mp_entries) *
		config_entry->mp_cache_entries_size);
	assert(new_mp_entries != NULL);
	new_mp_entries[0] = c_entry;

	if (config_entry->mp_cache_entries_size - 1 > 0) {
		memcpy(new_mp_entries + 1,
		    config_entry->mp_cache_entries,
		    (config_entry->mp_cache_entries_size - 1) *
		    sizeof(cache_entry));
	}

	old_mp_entries = config_entry->mp_cache_entries;
	config_entry->mp_cache_entries = new_mp_entries;
	free(old_mp_entries);

	qsort(config_entry->mp_cache_entries,
		config_entry->mp_cache_entries_size,
		sizeof(cache_entry),
		configuration_entry_cache_mp_sort_cmp);

	TRACE_OUT(configuration_entry_add_mp_cache_entry);
	return (0);
}

cache_entry
configuration_entry_find_mp_cache_entry(
	struct configuration_entry *config_entry, const char *mp_name)
{
	cache_entry *result;

	TRACE_IN(configuration_entry_find_mp_cache_entry);
	result = bsearch(mp_name, config_entry->mp_cache_entries,
		config_entry->mp_cache_entries_size,
		sizeof(cache_entry), configuration_entry_cache_mp_cmp);

	if (result == NULL) {
		TRACE_OUT(configuration_entry_find_mp_cache_entry);
		return (NULL);
	} else {
		TRACE_OUT(configuration_entry_find_mp_cache_entry);
		return (*result);
	}
}

/*
 * Searches for all multipart entries with names starting with mp_name.
 * Needed for cache flushing.
 */
int
configuration_entry_find_mp_cache_entries(
	struct configuration_entry *config_entry, const char *mp_name,
	cache_entry **start, cache_entry **finish)
{
	cache_entry *result;

	TRACE_IN(configuration_entry_find_mp_cache_entries);
	result = bsearch(mp_name, config_entry->mp_cache_entries,
		config_entry->mp_cache_entries_size,
		sizeof(cache_entry), configuration_entry_cache_mp_part_cmp);

	if (result == NULL) {
		TRACE_OUT(configuration_entry_find_mp_cache_entries);
		return (-1);
	}

	*start = result;
	*finish = result + 1;

	while (*start != config_entry->mp_cache_entries) {
	    if (configuration_entry_cache_mp_part_cmp(mp_name, *start - 1) == 0)
		*start = *start - 1;
	    else
		break;
	}

	while (*finish != config_entry->mp_cache_entries +
		config_entry->mp_cache_entries_size) {

	    if (configuration_entry_cache_mp_part_cmp(
		mp_name, *finish) == 0)
	    	*finish = *finish + 1;
	    else
		break;
	}

	TRACE_OUT(configuration_entry_find_mp_cache_entries);
	return (0);
}

/*
 * Configuration entry uses rwlock to handle access to its fields.
 */
void
configuration_lock_rdlock(struct configuration *config)
{
    TRACE_IN(configuration_lock_rdlock);
    pthread_rwlock_rdlock(&config->rwlock);
    TRACE_OUT(configuration_lock_rdlock);
}

void
configuration_lock_wrlock(struct configuration *config)
{
    TRACE_IN(configuration_lock_wrlock);
    pthread_rwlock_wrlock(&config->rwlock);
    TRACE_OUT(configuration_lock_wrlock);
}

void
configuration_unlock(struct configuration *config)
{
    TRACE_IN(configuration_unlock);
    pthread_rwlock_unlock(&config->rwlock);
    TRACE_OUT(configuration_unlock);
}

/*
 * Configuration entry uses 3 mutexes to handle cache operations. They are
 * acquired by configuration_lock_entry and configuration_unlock_entry
 * functions.
 */
void
configuration_lock_entry(struct configuration_entry *entry,
	enum config_entry_lock_type lock_type)
{
	TRACE_IN(configuration_lock_entry);
	assert(entry != NULL);

	switch (lock_type) {
	case CELT_POSITIVE:
		pthread_mutex_lock(&entry->positive_cache_lock);
		break;
	case CELT_NEGATIVE:
		pthread_mutex_lock(&entry->negative_cache_lock);
		break;
	case CELT_MULTIPART:
		pthread_mutex_lock(&entry->mp_cache_lock);
		break;
	default:
		/* should be unreachable */
		break;
	}
	TRACE_OUT(configuration_lock_entry);
}

void
configuration_unlock_entry(struct configuration_entry *entry,
	enum config_entry_lock_type lock_type)
{
	TRACE_IN(configuration_unlock_entry);
	assert(entry != NULL);

	switch (lock_type) {
	case CELT_POSITIVE:
		pthread_mutex_unlock(&entry->positive_cache_lock);
		break;
	case CELT_NEGATIVE:
		pthread_mutex_unlock(&entry->negative_cache_lock);
		break;
	case CELT_MULTIPART:
		pthread_mutex_unlock(&entry->mp_cache_lock);
		break;
	default:
		/* should be unreachable */
		break;
	}
	TRACE_OUT(configuration_unlock_entry);
}

struct configuration *
init_configuration(void)
{
	struct configuration	*retval;

	TRACE_IN(init_configuration);
	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->entries_capacity = INITIAL_ENTRIES_CAPACITY;
	retval->entries = calloc(retval->entries_capacity,
		sizeof(*retval->entries));
	assert(retval->entries != NULL);

	pthread_rwlock_init(&retval->rwlock, NULL);

	TRACE_OUT(init_configuration);
	return (retval);
}

void
fill_configuration_defaults(struct configuration *config)
{
	size_t	len, i;

	TRACE_IN(fill_configuration_defaults);
	assert(config != NULL);

	if (config->socket_path != NULL)
		free(config->socket_path);

	len = strlen(DEFAULT_SOCKET_PATH);
	config->socket_path = calloc(1, len + 1);
	assert(config->socket_path != NULL);
	memcpy(config->socket_path, DEFAULT_SOCKET_PATH, len);

	len = strlen(DEFAULT_PIDFILE_PATH);
	config->pidfile_path = calloc(1, len + 1);
	assert(config->pidfile_path != NULL);
	memcpy(config->pidfile_path, DEFAULT_PIDFILE_PATH, len);

	config->socket_mode =  S_IFSOCK | S_IRUSR | S_IWUSR |
		S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	config->force_unlink = 1;

	config->query_timeout = DEFAULT_QUERY_TIMEOUT;
	config->threads_num = DEFAULT_THREADS_NUM;

	for (i = 0; i < config->entries_size; ++i)
		destroy_configuration_entry(config->entries[i]);
	config->entries_size = 0;

	TRACE_OUT(fill_configuration_defaults);
}

void
destroy_configuration(struct configuration *config)
{
	unsigned int i;

	TRACE_IN(destroy_configuration);
	assert(config != NULL);
	free(config->pidfile_path);
	free(config->socket_path);

	for (i = 0; i < config->entries_size; ++i)
		destroy_configuration_entry(config->entries[i]);
	free(config->entries);

	pthread_rwlock_destroy(&config->rwlock);
	free(config);
	TRACE_OUT(destroy_configuration);
}
