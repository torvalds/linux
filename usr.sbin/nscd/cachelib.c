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
#include <stdlib.h>
#include <string.h>

#include "cachelib.h"
#include "debug.h"

#define INITIAL_ENTRIES_CAPACITY 32
#define ENTRIES_CAPACITY_STEP 32

#define STRING_SIMPLE_HASH_BODY(in_var, var, a, M)		\
	for ((var) = 0; *(in_var) != '\0'; ++(in_var))		\
		(var) = ((a)*(var) + *(in_var)) % (M)

#define STRING_SIMPLE_MP2_HASH_BODY(in_var, var, a, M)		\
	for ((var) = 0; *(in_var) != 0; ++(in_var))		\
		(var) = ((a)*(var) + *(in_var)) & (M - 1)

static int cache_elemsize_common_continue_func(struct cache_common_entry_ *,
	struct cache_policy_item_ *);
static int cache_lifetime_common_continue_func(struct cache_common_entry_ *,
	struct cache_policy_item_ *);
static void clear_cache_entry(struct cache_entry_ *);
static void destroy_cache_entry(struct cache_entry_ *);
static void destroy_cache_mp_read_session(struct cache_mp_read_session_ *);
static void destroy_cache_mp_write_session(struct cache_mp_write_session_ *);
static int entries_bsearch_cmp_func(const void *, const void *);
static int entries_qsort_cmp_func(const void *, const void *);
static struct cache_entry_ ** find_cache_entry_p(struct cache_ *,
	const char *);
static void flush_cache_entry(struct cache_entry_ *);
static void flush_cache_policy(struct cache_common_entry_ *,
	struct cache_policy_ *, struct cache_policy_ *,
		int (*)(struct cache_common_entry_ *,
		struct cache_policy_item_ *));
static int ht_items_cmp_func(const void *, const void *);
static int ht_items_fixed_size_left_cmp_func(const void *, const void *);
static hashtable_index_t ht_item_hash_func(const void *, size_t);

/*
 * Hashing and comparing routines, that are used with the hash tables
 */
static int
ht_items_cmp_func(const void *p1, const void *p2)
{
    	struct cache_ht_item_data_ *hp1, *hp2;
	size_t min_size;
	int result;

	hp1 = (struct cache_ht_item_data_ *)p1;
	hp2 = (struct cache_ht_item_data_ *)p2;

	assert(hp1->key != NULL);
	assert(hp2->key != NULL);

	if (hp1->key_size != hp2->key_size) {
		min_size = (hp1->key_size < hp2->key_size) ? hp1->key_size :
			hp2->key_size;
		result = memcmp(hp1->key, hp2->key, min_size);

		if (result == 0)
			return ((hp1->key_size < hp2->key_size) ? -1 : 1);
		else
			return (result);
	} else
		return (memcmp(hp1->key, hp2->key, hp1->key_size));
}

static int
ht_items_fixed_size_left_cmp_func(const void *p1, const void *p2)
{
    	struct cache_ht_item_data_ *hp1, *hp2;
	size_t min_size;
	int result;

	hp1 = (struct cache_ht_item_data_ *)p1;
	hp2 = (struct cache_ht_item_data_ *)p2;

	assert(hp1->key != NULL);
	assert(hp2->key != NULL);

	if (hp1->key_size != hp2->key_size) {
		min_size = (hp1->key_size < hp2->key_size) ? hp1->key_size :
			hp2->key_size;
		result = memcmp(hp1->key, hp2->key, min_size);

		if (result == 0)
			if (min_size == hp1->key_size)
			    return (0);
			else
			    return ((hp1->key_size < hp2->key_size) ? -1 : 1);
		else
			return (result);
	} else
		return (memcmp(hp1->key, hp2->key, hp1->key_size));
}

static hashtable_index_t
ht_item_hash_func(const void *p, size_t cache_entries_size)
{
    	struct cache_ht_item_data_ *hp;
	size_t i;

	hashtable_index_t retval;

	hp = (struct cache_ht_item_data_ *)p;
	assert(hp->key != NULL);

	retval = 0;
	for (i = 0; i < hp->key_size; ++i)
	    retval = (127 * retval + (unsigned char)hp->key[i]) %
		cache_entries_size;

	return retval;
}

HASHTABLE_PROTOTYPE(cache_ht_, cache_ht_item_, struct cache_ht_item_data_);
HASHTABLE_GENERATE(cache_ht_, cache_ht_item_, struct cache_ht_item_data_, data,
	ht_item_hash_func, ht_items_cmp_func);

/*
 * Routines to sort and search the entries by name
 */
static int
entries_bsearch_cmp_func(const void *key, const void *ent)
{

	assert(key != NULL);
	assert(ent != NULL);

	return (strcmp((char const *)key,
		(*(struct cache_entry_ const **)ent)->name));
}

static int
entries_qsort_cmp_func(const void *e1, const void *e2)
{

	assert(e1 != NULL);
	assert(e2 != NULL);

	return (strcmp((*(struct cache_entry_ const **)e1)->name,
		(*(struct cache_entry_ const **)e2)->name));
}

static struct cache_entry_ **
find_cache_entry_p(struct cache_ *the_cache, const char *entry_name)
{

	return ((struct cache_entry_ **)(bsearch(entry_name, the_cache->entries,
		the_cache->entries_size, sizeof(struct cache_entry_ *),
		entries_bsearch_cmp_func)));
}

static void
destroy_cache_mp_write_session(struct cache_mp_write_session_ *ws)
{

	struct cache_mp_data_item_	*data_item;

	TRACE_IN(destroy_cache_mp_write_session);
	assert(ws != NULL);
	while (!TAILQ_EMPTY(&ws->items)) {
		data_item = TAILQ_FIRST(&ws->items);
		TAILQ_REMOVE(&ws->items, data_item, entries);
		free(data_item->value);
		free(data_item);
	}

	free(ws);
	TRACE_OUT(destroy_cache_mp_write_session);
}

static void
destroy_cache_mp_read_session(struct cache_mp_read_session_ *rs)
{

	TRACE_IN(destroy_cache_mp_read_session);
	assert(rs != NULL);
	free(rs);
	TRACE_OUT(destroy_cache_mp_read_session);
}

static void
destroy_cache_entry(struct cache_entry_ *entry)
{
	struct cache_common_entry_	*common_entry;
	struct cache_mp_entry_		*mp_entry;
	struct cache_mp_read_session_	*rs;
	struct cache_mp_write_session_	*ws;
	struct cache_ht_item_ *ht_item;
	struct cache_ht_item_data_ *ht_item_data;

	TRACE_IN(destroy_cache_entry);
	assert(entry != NULL);

	if (entry->params->entry_type == CET_COMMON) {
		common_entry = (struct cache_common_entry_ *)entry;

		HASHTABLE_FOREACH(&(common_entry->items), ht_item) {
			HASHTABLE_ENTRY_FOREACH(ht_item, data, ht_item_data)
			{
				free(ht_item_data->key);
				free(ht_item_data->value);
			}
			HASHTABLE_ENTRY_CLEAR(ht_item, data);
		}

		HASHTABLE_DESTROY(&(common_entry->items), data);

		/* FIFO policy is always first */
		destroy_cache_fifo_policy(common_entry->policies[0]);
		switch (common_entry->common_params.policy) {
		case CPT_LRU:
			destroy_cache_lru_policy(common_entry->policies[1]);
			break;
		case CPT_LFU:
			destroy_cache_lfu_policy(common_entry->policies[1]);
			break;
		default:
		break;
		}
		free(common_entry->policies);
	} else {
		mp_entry = (struct cache_mp_entry_ *)entry;

		while (!TAILQ_EMPTY(&mp_entry->ws_head)) {
			ws = TAILQ_FIRST(&mp_entry->ws_head);
			TAILQ_REMOVE(&mp_entry->ws_head, ws, entries);
			destroy_cache_mp_write_session(ws);
		}

		while (!TAILQ_EMPTY(&mp_entry->rs_head)) {
			rs = TAILQ_FIRST(&mp_entry->rs_head);
			TAILQ_REMOVE(&mp_entry->rs_head, rs, entries);
			destroy_cache_mp_read_session(rs);
		}

		if (mp_entry->completed_write_session != NULL)
			destroy_cache_mp_write_session(
				mp_entry->completed_write_session);

		if (mp_entry->pending_write_session != NULL)
			destroy_cache_mp_write_session(
				mp_entry->pending_write_session);
	}

	free(entry->name);
	free(entry);
	TRACE_OUT(destroy_cache_entry);
}

static void
clear_cache_entry(struct cache_entry_ *entry)
{
	struct cache_mp_entry_		*mp_entry;
	struct cache_common_entry_	*common_entry;
	struct cache_ht_item_ *ht_item;
	struct cache_ht_item_data_ *ht_item_data;
	struct cache_policy_ *policy;
	struct cache_policy_item_ *item, *next_item;
	size_t entry_size;
	unsigned int i;

	if (entry->params->entry_type == CET_COMMON) {
		common_entry = (struct cache_common_entry_ *)entry;

		entry_size = 0;
		HASHTABLE_FOREACH(&(common_entry->items), ht_item) {
			HASHTABLE_ENTRY_FOREACH(ht_item, data, ht_item_data)
			{
				free(ht_item_data->key);
				free(ht_item_data->value);
			}
			entry_size += HASHTABLE_ENTRY_SIZE(ht_item, data);
			HASHTABLE_ENTRY_CLEAR(ht_item, data);
		}

		common_entry->items_size -= entry_size;
		for (i = 0; i < common_entry->policies_size; ++i) {
			policy = common_entry->policies[i];

			next_item = NULL;
			item = policy->get_first_item_func(policy);
			while (item != NULL) {
				next_item = policy->get_next_item_func(policy,
			    		item);
				policy->remove_item_func(policy, item);
				policy->destroy_item_func(item);
				item = next_item;
			}
		}
	} else {
		mp_entry = (struct cache_mp_entry_ *)entry;

		if (mp_entry->rs_size == 0) {
			if (mp_entry->completed_write_session != NULL) {
				destroy_cache_mp_write_session(
					mp_entry->completed_write_session);
				mp_entry->completed_write_session = NULL;
			}

			memset(&mp_entry->creation_time, 0,
				sizeof(struct timeval));
			memset(&mp_entry->last_request_time, 0,
				sizeof(struct timeval));
		}
	}
}

/*
 * When passed to the flush_cache_policy, ensures that all old elements are
 * deleted.
 */
static int
cache_lifetime_common_continue_func(struct cache_common_entry_ *entry,
	struct cache_policy_item_ *item)
{

	return ((item->last_request_time.tv_sec - item->creation_time.tv_sec >
		entry->common_params.max_lifetime.tv_sec) ? 1: 0);
}

/*
 * When passed to the flush_cache_policy, ensures that all elements, that
 * exceed the size limit, are deleted.
 */
static int
cache_elemsize_common_continue_func(struct cache_common_entry_ *entry,
	struct cache_policy_item_ *item)
{

	return ((entry->items_size > entry->common_params.satisf_elemsize) ? 1
    		: 0);
}

/*
 * Removes the elements from the cache entry, while the continue_func returns 1.
 */
static void
flush_cache_policy(struct cache_common_entry_ *entry,
	struct cache_policy_ *policy,
	struct cache_policy_ *connected_policy,
	int (*continue_func)(struct cache_common_entry_ *,
		struct cache_policy_item_ *))
{
	struct cache_policy_item_ *item, *next_item, *connected_item;
	struct cache_ht_item_ *ht_item;
	struct cache_ht_item_data_ *ht_item_data, ht_key;
	hashtable_index_t hash;

	assert(policy != NULL);

	next_item = NULL;
	item = policy->get_first_item_func(policy);
	while ((item != NULL) && (continue_func(entry, item) == 1)) {
		next_item = policy->get_next_item_func(policy, item);

		connected_item = item->connected_item;
		policy->remove_item_func(policy, item);

		memset(&ht_key, 0, sizeof(struct cache_ht_item_data_));
		ht_key.key = item->key;
		ht_key.key_size = item->key_size;

		hash = HASHTABLE_CALCULATE_HASH(cache_ht_, &entry->items,
			&ht_key);
		assert(hash < HASHTABLE_ENTRIES_COUNT(&entry->items));

		ht_item = HASHTABLE_GET_ENTRY(&(entry->items), hash);
		ht_item_data = HASHTABLE_ENTRY_FIND(cache_ht_, ht_item,
			&ht_key);
		assert(ht_item_data != NULL);
		free(ht_item_data->key);
		free(ht_item_data->value);
		HASHTABLE_ENTRY_REMOVE(cache_ht_, ht_item, ht_item_data);
		--entry->items_size;

		policy->destroy_item_func(item);

		if (connected_item != NULL) {
			connected_policy->remove_item_func(connected_policy,
				connected_item);
			connected_policy->destroy_item_func(connected_item);
		}

		item = next_item;
	}
}

static void
flush_cache_entry(struct cache_entry_ *entry)
{
	struct cache_mp_entry_		*mp_entry;
	struct cache_common_entry_	*common_entry;
	struct cache_policy_ *policy, *connected_policy;

	connected_policy = NULL;
	if (entry->params->entry_type == CET_COMMON) {
		common_entry = (struct cache_common_entry_ *)entry;
		if ((common_entry->common_params.max_lifetime.tv_sec != 0) ||
		    (common_entry->common_params.max_lifetime.tv_usec != 0)) {

			policy = common_entry->policies[0];
			if (common_entry->policies_size > 1)
				connected_policy = common_entry->policies[1];

			flush_cache_policy(common_entry, policy,
				connected_policy,
				cache_lifetime_common_continue_func);
		}


		if ((common_entry->common_params.max_elemsize != 0) &&
			common_entry->items_size >
			common_entry->common_params.max_elemsize) {

			if (common_entry->policies_size > 1) {
				policy = common_entry->policies[1];
				connected_policy = common_entry->policies[0];
			} else {
				policy = common_entry->policies[0];
				connected_policy = NULL;
			}

			flush_cache_policy(common_entry, policy,
				connected_policy,
				cache_elemsize_common_continue_func);
		}
	} else {
		mp_entry = (struct cache_mp_entry_ *)entry;

		if ((mp_entry->mp_params.max_lifetime.tv_sec != 0)
			|| (mp_entry->mp_params.max_lifetime.tv_usec != 0)) {

			if (mp_entry->last_request_time.tv_sec -
				mp_entry->last_request_time.tv_sec >
				mp_entry->mp_params.max_lifetime.tv_sec)
				clear_cache_entry(entry);
		}
	}
}

struct cache_ *
init_cache(struct cache_params const *params)
{
	struct cache_ *retval;

	TRACE_IN(init_cache);
	assert(params != NULL);

	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	assert(params != NULL);
	memcpy(&retval->params, params, sizeof(struct cache_params));

	retval->entries = calloc(INITIAL_ENTRIES_CAPACITY,
		sizeof(*retval->entries));
	assert(retval->entries != NULL);

	retval->entries_capacity = INITIAL_ENTRIES_CAPACITY;
	retval->entries_size = 0;

	TRACE_OUT(init_cache);
	return (retval);
}

void
destroy_cache(struct cache_ *the_cache)
{

	TRACE_IN(destroy_cache);
	assert(the_cache != NULL);

	if (the_cache->entries != NULL) {
		size_t i;
		for (i = 0; i < the_cache->entries_size; ++i)
			destroy_cache_entry(the_cache->entries[i]);

		free(the_cache->entries);
	}

	free(the_cache);
	TRACE_OUT(destroy_cache);
}

int
register_cache_entry(struct cache_ *the_cache,
	struct cache_entry_params const *params)
{
	int policies_size;
	size_t entry_name_size;
	struct cache_common_entry_	*new_common_entry;
	struct cache_mp_entry_		*new_mp_entry;

	TRACE_IN(register_cache_entry);
	assert(the_cache != NULL);

	if (find_cache_entry(the_cache, params->entry_name) != NULL) {
		TRACE_OUT(register_cache_entry);
		return (-1);
	}

	if (the_cache->entries_size == the_cache->entries_capacity) {
		struct cache_entry_ **new_entries;
		size_t	new_capacity;

		new_capacity = the_cache->entries_capacity +
			ENTRIES_CAPACITY_STEP;
		new_entries = calloc(new_capacity,
			sizeof(*new_entries));
		assert(new_entries != NULL);

		memcpy(new_entries, the_cache->entries,
			sizeof(struct cache_entry_ *)
			* the_cache->entries_size);

		free(the_cache->entries);
		the_cache->entries = new_entries;
	}

	entry_name_size = strlen(params->entry_name) + 1;
	switch (params->entry_type)
	{
	case CET_COMMON:
		new_common_entry = calloc(1,
			sizeof(*new_common_entry));
		assert(new_common_entry != NULL);

		memcpy(&new_common_entry->common_params, params,
			sizeof(struct common_cache_entry_params));
		new_common_entry->params =
		  (struct cache_entry_params *)&new_common_entry->common_params;

		new_common_entry->common_params.cep.entry_name = calloc(1,
			entry_name_size);
		assert(new_common_entry->common_params.cep.entry_name != NULL);
		strlcpy(new_common_entry->common_params.cep.entry_name,
			params->entry_name, entry_name_size);
		new_common_entry->name =
			new_common_entry->common_params.cep.entry_name;

		HASHTABLE_INIT(&(new_common_entry->items),
			struct cache_ht_item_data_, data,
			new_common_entry->common_params.cache_entries_size);

		if (new_common_entry->common_params.policy == CPT_FIFO)
			policies_size = 1;
		else
			policies_size = 2;

		new_common_entry->policies = calloc(policies_size,
			sizeof(*new_common_entry->policies));
		assert(new_common_entry->policies != NULL);

		new_common_entry->policies_size = policies_size;
		new_common_entry->policies[0] = init_cache_fifo_policy();

		if (policies_size > 1) {
			switch (new_common_entry->common_params.policy) {
			case CPT_LRU:
				new_common_entry->policies[1] =
					init_cache_lru_policy();
			break;
			case CPT_LFU:
				new_common_entry->policies[1] =
					init_cache_lfu_policy();
			break;
			default:
			break;
			}
		}

		new_common_entry->get_time_func =
			the_cache->params.get_time_func;
		the_cache->entries[the_cache->entries_size++] =
			(struct cache_entry_ *)new_common_entry;
		break;
	case CET_MULTIPART:
		new_mp_entry = calloc(1,
			sizeof(*new_mp_entry));
		assert(new_mp_entry != NULL);

		memcpy(&new_mp_entry->mp_params, params,
			sizeof(struct mp_cache_entry_params));
		new_mp_entry->params =
			(struct cache_entry_params *)&new_mp_entry->mp_params;

		new_mp_entry->mp_params.cep.entry_name = calloc(1,
			entry_name_size);
		assert(new_mp_entry->mp_params.cep.entry_name != NULL);
		strlcpy(new_mp_entry->mp_params.cep.entry_name, params->entry_name,
			entry_name_size);
		new_mp_entry->name = new_mp_entry->mp_params.cep.entry_name;

		TAILQ_INIT(&new_mp_entry->ws_head);
		TAILQ_INIT(&new_mp_entry->rs_head);

		new_mp_entry->get_time_func = the_cache->params.get_time_func;
		the_cache->entries[the_cache->entries_size++] =
			(struct cache_entry_ *)new_mp_entry;
		break;
	}


	qsort(the_cache->entries, the_cache->entries_size,
		sizeof(struct cache_entry_ *), entries_qsort_cmp_func);

	TRACE_OUT(register_cache_entry);
	return (0);
}

int
unregister_cache_entry(struct cache_ *the_cache, const char *entry_name)
{
	struct cache_entry_ **del_ent;

	TRACE_IN(unregister_cache_entry);
	assert(the_cache != NULL);

	del_ent = find_cache_entry_p(the_cache, entry_name);
	if (del_ent != NULL) {
		destroy_cache_entry(*del_ent);
		--the_cache->entries_size;

		memmove(del_ent, del_ent + 1,
			(&(the_cache->entries[--the_cache->entries_size]) -
	    		del_ent) * sizeof(struct cache_entry_ *));

		TRACE_OUT(unregister_cache_entry);
		return (0);
	} else {
		TRACE_OUT(unregister_cache_entry);
		return (-1);
	}
}

struct cache_entry_ *
find_cache_entry(struct cache_ *the_cache, const char *entry_name)
{
	struct cache_entry_ **result;

	TRACE_IN(find_cache_entry);
	result = find_cache_entry_p(the_cache, entry_name);

	if (result == NULL) {
		TRACE_OUT(find_cache_entry);
		return (NULL);
	} else {
		TRACE_OUT(find_cache_entry);
		return (*result);
	}
}

/*
 * Tries to read the element with the specified key from the cache. If the
 * value_size is too small, it will be filled with the proper number, and
 * the user will need to call cache_read again with the value buffer, that
 * is large enough.
 * Function returns 0 on success, -1 on error, and -2 if the value_size is too
 * small.
 */
int
cache_read(struct cache_entry_ *entry, const char *key, size_t key_size,
	char *value, size_t *value_size)
{
	struct cache_common_entry_	*common_entry;
	struct cache_ht_item_data_	item_data, *find_res;
	struct cache_ht_item_		*item;
	hashtable_index_t	hash;
	struct cache_policy_item_ *connected_item;

	TRACE_IN(cache_read);
	assert(entry != NULL);
	assert(key != NULL);
	assert(value_size != NULL);
	assert(entry->params->entry_type == CET_COMMON);

	common_entry = (struct cache_common_entry_ *)entry;

	memset(&item_data, 0, sizeof(struct cache_ht_item_data_));
	/* can't avoid the cast here */
	item_data.key = (char *)key;
	item_data.key_size = key_size;

	hash = HASHTABLE_CALCULATE_HASH(cache_ht_, &common_entry->items,
		&item_data);
	assert(hash < HASHTABLE_ENTRIES_COUNT(&common_entry->items));

	item = HASHTABLE_GET_ENTRY(&(common_entry->items), hash);
	find_res = HASHTABLE_ENTRY_FIND(cache_ht_, item, &item_data);
	if (find_res == NULL) {
		TRACE_OUT(cache_read);
		return (-1);
	}
	/* pretend that entry was not found if confidence is below threshold*/
	if (find_res->confidence < 
	    common_entry->common_params.confidence_threshold) {
		TRACE_OUT(cache_read);
		return (-1);
	}

	if ((common_entry->common_params.max_lifetime.tv_sec != 0) ||
		(common_entry->common_params.max_lifetime.tv_usec != 0)) {

		if (find_res->fifo_policy_item->last_request_time.tv_sec -
			find_res->fifo_policy_item->creation_time.tv_sec >
			common_entry->common_params.max_lifetime.tv_sec) {

			free(find_res->key);
			free(find_res->value);

			connected_item =
			    find_res->fifo_policy_item->connected_item;
			if (connected_item != NULL) {
				common_entry->policies[1]->remove_item_func(
					common_entry->policies[1],
			    		connected_item);
				common_entry->policies[1]->destroy_item_func(
					connected_item);
			}

			common_entry->policies[0]->remove_item_func(
				common_entry->policies[0],
					find_res->fifo_policy_item);
			common_entry->policies[0]->destroy_item_func(
				find_res->fifo_policy_item);

			HASHTABLE_ENTRY_REMOVE(cache_ht_, item, find_res);
			--common_entry->items_size;
		}
	}

	if ((*value_size < find_res->value_size) || (value == NULL)) {
		*value_size = find_res->value_size;
		TRACE_OUT(cache_read);
		return (-2);
	}

	*value_size = find_res->value_size;
	memcpy(value, find_res->value, find_res->value_size);

	++find_res->fifo_policy_item->request_count;
	common_entry->get_time_func(
		&find_res->fifo_policy_item->last_request_time);
	common_entry->policies[0]->update_item_func(common_entry->policies[0],
		find_res->fifo_policy_item);

	if (find_res->fifo_policy_item->connected_item != NULL) {
		connected_item = find_res->fifo_policy_item->connected_item;
		memcpy(&connected_item->last_request_time,
			&find_res->fifo_policy_item->last_request_time,
			sizeof(struct timeval));
		connected_item->request_count =
			find_res->fifo_policy_item->request_count;

		common_entry->policies[1]->update_item_func(
			common_entry->policies[1], connected_item);
	}

	TRACE_OUT(cache_read);
	return (0);
}

/*
 * Writes the value with the specified key into the cache entry.
 * Functions returns 0 on success, and -1 on error.
 */
int
cache_write(struct cache_entry_ *entry, const char *key, size_t key_size,
    	char const *value, size_t value_size)
{
	struct cache_common_entry_	*common_entry;
	struct cache_ht_item_data_	item_data, *find_res;
	struct cache_ht_item_		*item;
	hashtable_index_t	hash;

	struct cache_policy_		*policy, *connected_policy;
	struct cache_policy_item_	*policy_item;
	struct cache_policy_item_	*connected_policy_item;

	TRACE_IN(cache_write);
	assert(entry != NULL);
	assert(key != NULL);
	assert(value != NULL);
	assert(entry->params->entry_type == CET_COMMON);

	common_entry = (struct cache_common_entry_ *)entry;

	memset(&item_data, 0, sizeof(struct cache_ht_item_data_));
	/* can't avoid the cast here */
	item_data.key = (char *)key;
	item_data.key_size = key_size;

	hash = HASHTABLE_CALCULATE_HASH(cache_ht_, &common_entry->items,
		&item_data);
	assert(hash < HASHTABLE_ENTRIES_COUNT(&common_entry->items));

	item = HASHTABLE_GET_ENTRY(&(common_entry->items), hash);
	find_res = HASHTABLE_ENTRY_FIND(cache_ht_, item, &item_data);
	if (find_res != NULL) {
		if (find_res->confidence < common_entry->common_params.confidence_threshold) {
		  	/* duplicate entry is no error, if confidence is low */
			if ((find_res->value_size == value_size) &&
			    (memcmp(find_res->value, value, value_size) == 0)) {
				/* increase confidence on exact match (key and values) */
				find_res->confidence++;
			} else {
				/* create new entry with low confidence, if value changed */
				free(item_data.value);
				item_data.value = malloc(value_size);
				assert(item_data.value != NULL);
				memcpy(item_data.value, value, value_size);
				item_data.value_size = value_size;
				find_res->confidence = 1;
			}
			TRACE_OUT(cache_write);
			return (0);
		}
		TRACE_OUT(cache_write);
		return (-1);
	}

	item_data.key = malloc(key_size);
	memcpy(item_data.key, key, key_size);

	item_data.value = malloc(value_size);
	assert(item_data.value != NULL);

	memcpy(item_data.value, value, value_size);
	item_data.value_size = value_size;

	item_data.confidence = 1;

	policy_item = common_entry->policies[0]->create_item_func();
	policy_item->key = item_data.key;
	policy_item->key_size = item_data.key_size;
	common_entry->get_time_func(&policy_item->creation_time);

	if (common_entry->policies_size > 1) {
		connected_policy_item =
			common_entry->policies[1]->create_item_func();
		memcpy(&connected_policy_item->creation_time,
			&policy_item->creation_time,
			sizeof(struct timeval));
		connected_policy_item->key = policy_item->key;
		connected_policy_item->key_size = policy_item->key_size;

		connected_policy_item->connected_item = policy_item;
		policy_item->connected_item = connected_policy_item;
	}

	item_data.fifo_policy_item = policy_item;

	common_entry->policies[0]->add_item_func(common_entry->policies[0],
		policy_item);
	if (common_entry->policies_size > 1)
		common_entry->policies[1]->add_item_func(
			common_entry->policies[1], connected_policy_item);

	HASHTABLE_ENTRY_STORE(cache_ht_, item, &item_data);
	++common_entry->items_size;

	if ((common_entry->common_params.max_elemsize != 0) &&
		(common_entry->items_size >
		common_entry->common_params.max_elemsize)) {
		if (common_entry->policies_size > 1) {
			policy = common_entry->policies[1];
			connected_policy = common_entry->policies[0];
		} else {
			policy = common_entry->policies[0];
			connected_policy = NULL;
		}

		flush_cache_policy(common_entry, policy, connected_policy,
			cache_elemsize_common_continue_func);
	}

	TRACE_OUT(cache_write);
	return (0);
}

/*
 * Initializes the write session for the specified multipart entry. This
 * session then should be filled with data either committed or abandoned by
 * using close_cache_mp_write_session or abandon_cache_mp_write_session
 * respectively.
 * Returns NULL on errors (when there are too many opened write sessions for
 * the entry).
 */
struct cache_mp_write_session_ *
open_cache_mp_write_session(struct cache_entry_ *entry)
{
	struct cache_mp_entry_	*mp_entry;
	struct cache_mp_write_session_	*retval;

	TRACE_IN(open_cache_mp_write_session);
	assert(entry != NULL);
	assert(entry->params->entry_type == CET_MULTIPART);
	mp_entry = (struct cache_mp_entry_ *)entry;

	if ((mp_entry->mp_params.max_sessions > 0) &&
		(mp_entry->ws_size == mp_entry->mp_params.max_sessions)) {
		TRACE_OUT(open_cache_mp_write_session);
		return (NULL);
	}

	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	TAILQ_INIT(&retval->items);
	retval->parent_entry = mp_entry;

	TAILQ_INSERT_HEAD(&mp_entry->ws_head, retval, entries);
	++mp_entry->ws_size;

	TRACE_OUT(open_cache_mp_write_session);
	return (retval);
}

/*
 * Writes data to the specified session. Return 0 on success and -1 on errors
 * (when write session size limit is exceeded).
 */
int
cache_mp_write(struct cache_mp_write_session_ *ws, char *data,
	size_t data_size)
{
	struct cache_mp_data_item_	*new_item;

	TRACE_IN(cache_mp_write);
	assert(ws != NULL);
	assert(ws->parent_entry != NULL);
	assert(ws->parent_entry->params->entry_type == CET_MULTIPART);

	if ((ws->parent_entry->mp_params.max_elemsize > 0) &&
		(ws->parent_entry->mp_params.max_elemsize == ws->items_size)) {
		TRACE_OUT(cache_mp_write);
		return (-1);
	}

	new_item = calloc(1,
		sizeof(*new_item));
	assert(new_item != NULL);

	new_item->value = malloc(data_size);
	assert(new_item->value != NULL);
	memcpy(new_item->value, data, data_size);
	new_item->value_size = data_size;

	TAILQ_INSERT_TAIL(&ws->items, new_item, entries);
	++ws->items_size;

	TRACE_OUT(cache_mp_write);
	return (0);
}

/*
 * Abandons the write session and frees all the connected resources.
 */
void
abandon_cache_mp_write_session(struct cache_mp_write_session_ *ws)
{

	TRACE_IN(abandon_cache_mp_write_session);
	assert(ws != NULL);
	assert(ws->parent_entry != NULL);
	assert(ws->parent_entry->params->entry_type == CET_MULTIPART);

	TAILQ_REMOVE(&ws->parent_entry->ws_head, ws, entries);
	--ws->parent_entry->ws_size;

	destroy_cache_mp_write_session(ws);
	TRACE_OUT(abandon_cache_mp_write_session);
}

/*
 * Commits the session to the entry, for which it was created.
 */
void
close_cache_mp_write_session(struct cache_mp_write_session_ *ws)
{

	TRACE_IN(close_cache_mp_write_session);
	assert(ws != NULL);
	assert(ws->parent_entry != NULL);
	assert(ws->parent_entry->params->entry_type == CET_MULTIPART);

	TAILQ_REMOVE(&ws->parent_entry->ws_head, ws, entries);
	--ws->parent_entry->ws_size;

	if (ws->parent_entry->completed_write_session == NULL) {
		/*
		 * If there is no completed session yet, this will be the one
		 */
		ws->parent_entry->get_time_func(
	    		&ws->parent_entry->creation_time);
		ws->parent_entry->completed_write_session = ws;
	} else {
		/*
		 * If there is a completed session, then we'll save our session
		 * as a pending session. If there is already a pending session,
		 * it would be destroyed.
		 */
		if (ws->parent_entry->pending_write_session != NULL)
			destroy_cache_mp_write_session(
				ws->parent_entry->pending_write_session);

		ws->parent_entry->pending_write_session = ws;
	}
	TRACE_OUT(close_cache_mp_write_session);
}

/*
 * Opens read session for the specified entry. Returns NULL on errors (when
 * there are no data in the entry, or the data are obsolete).
 */
struct cache_mp_read_session_ *
open_cache_mp_read_session(struct cache_entry_ *entry)
{
	struct cache_mp_entry_			*mp_entry;
	struct cache_mp_read_session_	*retval;

	TRACE_IN(open_cache_mp_read_session);
	assert(entry != NULL);
	assert(entry->params->entry_type == CET_MULTIPART);
	mp_entry = (struct cache_mp_entry_ *)entry;

	if (mp_entry->completed_write_session == NULL) {
		TRACE_OUT(open_cache_mp_read_session);
		return (NULL);
	}

	if ((mp_entry->mp_params.max_lifetime.tv_sec != 0)
		|| (mp_entry->mp_params.max_lifetime.tv_usec != 0)) {
		if (mp_entry->last_request_time.tv_sec -
			mp_entry->last_request_time.tv_sec >
			mp_entry->mp_params.max_lifetime.tv_sec) {
			flush_cache_entry(entry);
			TRACE_OUT(open_cache_mp_read_session);
			return (NULL);
		}
	}

	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	retval->parent_entry = mp_entry;
	retval->current_item = TAILQ_FIRST(
		&mp_entry->completed_write_session->items);

	TAILQ_INSERT_HEAD(&mp_entry->rs_head, retval, entries);
	++mp_entry->rs_size;

	mp_entry->get_time_func(&mp_entry->last_request_time);
	TRACE_OUT(open_cache_mp_read_session);
	return (retval);
}

/*
 * Reads the data from the read session - step by step.
 * Returns 0 on success, -1 on error (when there are no more data), and -2 if
 * the data_size is too small.  In the last case, data_size would be filled
 * the proper value.
 */
int
cache_mp_read(struct cache_mp_read_session_ *rs, char *data, size_t *data_size)
{

	TRACE_IN(cache_mp_read);
	assert(rs != NULL);

	if (rs->current_item == NULL) {
		TRACE_OUT(cache_mp_read);
		return (-1);
	}

	if (rs->current_item->value_size > *data_size) {
		*data_size = rs->current_item->value_size;
		if (data == NULL) {
			TRACE_OUT(cache_mp_read);
			return (0);
		}

		TRACE_OUT(cache_mp_read);
		return (-2);
	}

	*data_size = rs->current_item->value_size;
	memcpy(data, rs->current_item->value, rs->current_item->value_size);
	rs->current_item = TAILQ_NEXT(rs->current_item, entries);

	TRACE_OUT(cache_mp_read);
	return (0);
}

/*
 * Closes the read session. If there are no more read sessions and there is
 * a pending write session, it will be committed and old
 * completed_write_session will be destroyed.
 */
void
close_cache_mp_read_session(struct cache_mp_read_session_ *rs)
{

	TRACE_IN(close_cache_mp_read_session);
	assert(rs != NULL);
	assert(rs->parent_entry != NULL);

	TAILQ_REMOVE(&rs->parent_entry->rs_head, rs, entries);
	--rs->parent_entry->rs_size;

	if ((rs->parent_entry->rs_size == 0) &&
		(rs->parent_entry->pending_write_session != NULL)) {
		destroy_cache_mp_write_session(
			rs->parent_entry->completed_write_session);
		rs->parent_entry->completed_write_session =
			rs->parent_entry->pending_write_session;
		rs->parent_entry->pending_write_session = NULL;
	}

	destroy_cache_mp_read_session(rs);
	TRACE_OUT(close_cache_mp_read_session);
}

int
transform_cache_entry(struct cache_entry_ *entry,
	enum cache_transformation_t transformation)
{

	TRACE_IN(transform_cache_entry);
	switch (transformation) {
	case CTT_CLEAR:
		clear_cache_entry(entry);
		TRACE_OUT(transform_cache_entry);
		return (0);
	case CTT_FLUSH:
		flush_cache_entry(entry);
		TRACE_OUT(transform_cache_entry);
		return (0);
	default:
		TRACE_OUT(transform_cache_entry);
		return (-1);
	}
}

int
transform_cache_entry_part(struct cache_entry_ *entry,
	enum cache_transformation_t transformation, const char *key_part,
	size_t key_part_size, enum part_position_t part_position)
{
	struct cache_common_entry_ *common_entry;
	struct cache_ht_item_ *ht_item;
	struct cache_ht_item_data_ *ht_item_data, ht_key;

	struct cache_policy_item_ *item, *connected_item;

	TRACE_IN(transform_cache_entry_part);
	if (entry->params->entry_type != CET_COMMON) {
		TRACE_OUT(transform_cache_entry_part);
		return (-1);
	}

	if (transformation != CTT_CLEAR) {
		TRACE_OUT(transform_cache_entry_part);
		return (-1);
	}

	memset(&ht_key, 0, sizeof(struct cache_ht_item_data_));
	ht_key.key = (char *)key_part;	/* can't avoid casting here */
	ht_key.key_size = key_part_size;

	common_entry = (struct cache_common_entry_ *)entry;
	HASHTABLE_FOREACH(&(common_entry->items), ht_item) {
		do {
			ht_item_data = HASHTABLE_ENTRY_FIND_SPECIAL(cache_ht_,
				ht_item, &ht_key,
				ht_items_fixed_size_left_cmp_func);

			if (ht_item_data != NULL) {
			    item = ht_item_data->fifo_policy_item;
			    connected_item = item->connected_item;

			    common_entry->policies[0]->remove_item_func(
				common_entry->policies[0],
				item);

			    free(ht_item_data->key);
			    free(ht_item_data->value);
			    HASHTABLE_ENTRY_REMOVE(cache_ht_, ht_item,
				ht_item_data);
			    --common_entry->items_size;

			    common_entry->policies[0]->destroy_item_func(
				item);
			    if (common_entry->policies_size == 2) {
				common_entry->policies[1]->remove_item_func(
				    common_entry->policies[1],
				    connected_item);
				common_entry->policies[1]->destroy_item_func(
				    connected_item);
			    }
			}
		} while (ht_item_data != NULL);
	}

	TRACE_OUT(transform_cache_entry_part);
	return (0);
}
