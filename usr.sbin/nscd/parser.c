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

#include <sys/time.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "log.h"
#include "parser.h"

static void enable_cache(struct configuration *,const char *, int);
static struct configuration_entry *find_create_entry(struct configuration *,
	const char *);
static int get_number(const char *, int, int);
static enum cache_policy_t get_policy(const char *);
static int get_yesno(const char *);
static int check_cachename(const char *);
static void check_files(struct configuration *, const char *, int);
static void set_keep_hot_count(struct configuration *, const char *, int);
static void set_negative_policy(struct configuration *, const char *,
	enum cache_policy_t);
static void set_negative_time_to_live(struct configuration *,
	const char *, int);
static void set_positive_policy(struct configuration *, const char *,
	enum cache_policy_t);
static void set_perform_actual_lookups(struct configuration *, const char *,
	int);
static void set_positive_time_to_live(struct configuration *,
	const char *, int);
static void set_suggested_size(struct configuration *, const char *,
	int size);
static void set_threads_num(struct configuration *, int);
static int strbreak(char *, char **, int);

static int
strbreak(char *str, char **fields, int fields_size)
{
	char	*c = str;
	int	i, num;

	TRACE_IN(strbreak);
	num = 0;
	for (i = 0;
	     ((*fields =
	     	strsep(i < fields_size ? &c : NULL, "\n\t ")) != NULL);
	     ++i)
		if ((*(*fields)) != '\0') {
			++fields;
			++num;
		}

	TRACE_OUT(strbreak);
	return (num);
}

/*
 * Tries to find the configuration entry with the specified name. If search
 * fails, the new entry with the default parameters will be created.
 */
static struct configuration_entry *
find_create_entry(struct configuration *config,
	const char *entry_name)
{
	struct configuration_entry *entry = NULL;
	int res;

	TRACE_IN(find_create_entry);
	entry = configuration_find_entry(config, entry_name);
	if (entry == NULL) {
		entry = create_def_configuration_entry(entry_name);
		assert( entry != NULL);
		res = add_configuration_entry(config, entry);
		assert(res == 0);
	}

	TRACE_OUT(find_create_entry);
	return (entry);
}

/*
 * The vast majority of the functions below corresponds to the particular
 * keywords in the configuration file.
 */
static void
enable_cache(struct configuration *config, const char *entry_name, int flag)
{
	struct configuration_entry	*entry;

	TRACE_IN(enable_cache);
	entry = find_create_entry(config, entry_name);
	entry->enabled = flag;
	TRACE_OUT(enable_cache);
}

static void
set_positive_time_to_live(struct configuration *config,
	const char *entry_name, int ttl)
{
	struct configuration_entry *entry;
	struct timeval lifetime;

	TRACE_IN(set_positive_time_to_live);
	assert(ttl >= 0);
	assert(entry_name != NULL);
	memset(&lifetime, 0, sizeof(struct timeval));
	lifetime.tv_sec = ttl;

	entry = find_create_entry(config, entry_name);
	memcpy(&entry->positive_cache_params.max_lifetime,
		&lifetime, sizeof(struct timeval));
	memcpy(&entry->mp_cache_params.max_lifetime,
		&lifetime, sizeof(struct timeval));

	TRACE_OUT(set_positive_time_to_live);
}

static void
set_negative_time_to_live(struct configuration *config,
	const char *entry_name, int nttl)
{
	struct configuration_entry *entry;
	struct timeval lifetime;

	TRACE_IN(set_negative_time_to_live);
	assert(nttl > 0);
	assert(entry_name != NULL);
	memset(&lifetime, 0, sizeof(struct timeval));
	lifetime.tv_sec = nttl;

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	memcpy(&entry->negative_cache_params.max_lifetime,
		&lifetime, sizeof(struct timeval));

	TRACE_OUT(set_negative_time_to_live);
}

static void
set_positive_confidence_threshold(struct configuration *config,
	const char *entry_name, int conf_thresh)
{
	struct configuration_entry *entry;

	TRACE_IN(set_positive_conf_thresh);
	assert(conf_thresh > 0);
	assert(entry_name != NULL);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->positive_cache_params.confidence_threshold = conf_thresh;

	TRACE_OUT(set_positive_conf_thresh);
}

static void
set_negative_confidence_threshold(struct configuration *config,
	const char *entry_name, int conf_thresh)
{
	struct configuration_entry *entry;

	TRACE_IN(set_negative_conf_thresh);
	assert(conf_thresh > 0);
	assert(entry_name != NULL);
	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->negative_cache_params.confidence_threshold = conf_thresh;
	TRACE_OUT(set_negative_conf_thresh);
}

/*
 * Hot count is actually the elements size limit.
 */
static void
set_keep_hot_count(struct configuration *config,
	const char *entry_name, int count)
{
	struct configuration_entry *entry;

	TRACE_IN(set_keep_hot_count);
	assert(count >= 0);
	assert(entry_name != NULL);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->positive_cache_params.max_elemsize = count;

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->negative_cache_params.max_elemsize = count;

	TRACE_OUT(set_keep_hot_count);
}

static void
set_positive_policy(struct configuration *config,
	const char *entry_name, enum cache_policy_t policy)
{
	struct configuration_entry *entry;

	TRACE_IN(set_positive_policy);
	assert(entry_name != NULL);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->positive_cache_params.policy = policy;

	TRACE_OUT(set_positive_policy);
}

static void
set_negative_policy(struct configuration *config,
	const char *entry_name, enum cache_policy_t policy)
{
	struct configuration_entry *entry;

	TRACE_IN(set_negative_policy);
	assert(entry_name != NULL);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->negative_cache_params.policy = policy;

	TRACE_OUT(set_negative_policy);
}

static void
set_perform_actual_lookups(struct configuration *config,
	const char *entry_name, int flag)
{
	struct configuration_entry *entry;

	TRACE_IN(set_perform_actual_lookups);
	assert(entry_name != NULL);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->perform_actual_lookups = flag;

	TRACE_OUT(set_perform_actual_lookups);
}

static void
set_suggested_size(struct configuration *config,
	const char *entry_name, int size)
{
	struct configuration_entry	*entry;

	TRACE_IN(set_suggested_size);
	assert(config != NULL);
	assert(entry_name != NULL);
	assert(size > 0);

	entry = find_create_entry(config, entry_name);
	assert(entry != NULL);
	entry->positive_cache_params.cache_entries_size = size;
	entry->negative_cache_params.cache_entries_size = size;

	TRACE_OUT(set_suggested_size);
}

static void
check_files(struct configuration *config, const char *entry_name, int flag)
{

	TRACE_IN(check_files);
	assert(entry_name != NULL);
	TRACE_OUT(check_files);
}

static int
get_yesno(const char *str)
{

	if (strcmp(str, "yes") == 0)
		return (1);
	else if (strcmp(str, "no") == 0)
		return (0);
	else
		return (-1);
}

static int
get_number(const char *str, int low, int max)
{

	char *end = NULL;
	int res = 0;

	if (str[0] == '\0')
		return (-1);

	res = strtol(str, &end, 10);
	if (*end != '\0')
		return (-1);
	else
		if (((res >= low) || (low == -1)) &&
			((res <= max) || (max == -1)))
			return (res);
		else
			return (-2);
}

static enum cache_policy_t
get_policy(const char *str)
{

	if (strcmp(str, "fifo") == 0)
		return (CPT_FIFO);
	else if (strcmp(str, "lru") == 0)
		return (CPT_LRU);
	else if (strcmp(str, "lfu") == 0)
		return (CPT_LFU);

	return (-1);
}

static int
check_cachename(const char *str)
{

	assert(str != NULL);
	return ((strlen(str) > 0) ? 0 : -1);
}

static void
set_threads_num(struct configuration *config, int value)
{

	assert(config != NULL);
	config->threads_num = value;
}

/*
 * The main configuration routine. Its implementation is hugely inspired by the
 * the same routine implementation in Solaris NSCD.
 */
int
parse_config_file(struct configuration *config,
	const char *fname, char const **error_str, int *error_line)
{
	FILE	*fin;
	char	buffer[255];
	char	*fields[128];
	int	field_count, line_num, value;
	int	res;

	TRACE_IN(parse_config_file);
	assert(config != NULL);
	assert(fname != NULL);

	fin = fopen(fname, "r");
	if (fin == NULL) {
		TRACE_OUT(parse_config_file);
		return (-1);
	}

	res = 0;
	line_num = 0;
	memset(buffer, 0, sizeof(buffer));
	while ((res == 0) && (fgets(buffer, sizeof(buffer) - 1, fin) != NULL)) {
		field_count = strbreak(buffer, fields, sizeof(fields));
		++line_num;

		if (field_count == 0)
			continue;

		switch (fields[0][0]) {
		case '#':
		case '\0':
			continue;
		case 'e':
			if ((field_count == 3) &&
			(strcmp(fields[0], "enable-cache") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_yesno(fields[2])) != -1)) {
				enable_cache(config, fields[1], value);
				continue;
			}
			break;
		case 'd':
			if ((field_count == 2) &&
			(strcmp(fields[0], "debug-level") == 0) &&
			((value = get_number(fields[1], 0, 10)) != -1)) {
				continue;
			}
			break;
		case 'p':
			if ((field_count == 3) &&
			(strcmp(fields[0], "positive-time-to-live") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_number(fields[2], 0, -1)) != -1)) {
				set_positive_time_to_live(config,
					fields[1], value);
				continue;
			} else if ((field_count == 3) &&
			(strcmp(fields[0], "positive-confidence-threshold") == 0) &&
			((value = get_number(fields[2], 1, -1)) != -1)) {
				set_positive_confidence_threshold(config,
					fields[1], value);
				continue;
			} else if ((field_count == 3) &&
			(strcmp(fields[0], "positive-policy") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_policy(fields[2])) != -1)) {
				set_positive_policy(config, fields[1], value);
				continue;
			} else if ((field_count == 3) &&
			(strcmp(fields[0], "perform-actual-lookups") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_yesno(fields[2])) != -1)) {
				set_perform_actual_lookups(config, fields[1],
					value);
				continue;
			}
			break;
		case 'n':
			if ((field_count == 3) &&
			(strcmp(fields[0], "negative-time-to-live") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_number(fields[2], 0, -1)) != -1)) {
				set_negative_time_to_live(config,
					fields[1], value);
				continue;
			} else if ((field_count == 3) &&
			(strcmp(fields[0], "negative-confidence-threshold") == 0) &&
			((value = get_number(fields[2], 1, -1)) != -1)) {
				set_negative_confidence_threshold(config,
					fields[1], value);
				continue;
			} else if ((field_count == 3) &&
			(strcmp(fields[0], "negative-policy") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_policy(fields[2])) != -1)) {
				set_negative_policy(config,
					fields[1], value);
				continue;
			}
			break;
		case 's':
			if ((field_count == 3) &&
			(strcmp(fields[0], "suggested-size") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_number(fields[2], 1, -1)) != -1)) {
				set_suggested_size(config, fields[1], value);
				continue;
			}
			break;
		case 't':
			if ((field_count == 2) &&
			(strcmp(fields[0], "threads") == 0) &&
			((value = get_number(fields[1], 1, -1)) != -1)) {
				set_threads_num(config, value);
				continue;
			}
			break;
		case 'k':
			if ((field_count == 3) &&
			(strcmp(fields[0], "keep-hot-count") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_number(fields[2], 0, -1)) != -1)) {
				set_keep_hot_count(config,
					fields[1], value);
				continue;
			}
			break;
		case 'c':
			if ((field_count == 3) &&
			(strcmp(fields[0], "check-files") == 0) &&
			(check_cachename(fields[1]) == 0) &&
			((value = get_yesno(fields[2])) != -1)) {
				check_files(config,
					fields[1], value);
				continue;
			}
			break;
		default:
			break;
		}

		LOG_ERR_2("config file parser", "error in file "
			"%s on line %d", fname, line_num);
		*error_str = "syntax error";
		*error_line = line_num;
		res = -1;
	}
	fclose(fin);

	TRACE_OUT(parse_config_file);
	return (res);
}
