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

#include "cacheplcs.h"
#include "debug.h"

static void cache_fifo_policy_update_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static void cache_lfu_policy_add_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static struct cache_policy_item_ * cache_lfu_policy_create_item(void);
static void cache_lfu_policy_destroy_item(struct cache_policy_item_ *);
static struct cache_policy_item_ *cache_lfu_policy_get_first_item(
	struct cache_policy_ *);
static struct cache_policy_item_ *cache_lfu_policy_get_last_item(
	struct cache_policy_ *);
static struct cache_policy_item_ *cache_lfu_policy_get_next_item(
	struct cache_policy_ *, struct cache_policy_item_ *);
static struct cache_policy_item_ *cache_lfu_policy_get_prev_item(
	struct cache_policy_ *, struct cache_policy_item_ *);
static void cache_lfu_policy_remove_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static void cache_lfu_policy_update_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static void cache_lru_policy_update_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static void cache_queue_policy_add_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static struct cache_policy_item_ * cache_queue_policy_create_item(void);
static void cache_queue_policy_destroy_item(struct cache_policy_item_ *);
static struct cache_policy_item_ *cache_queue_policy_get_first_item(
	struct cache_policy_ *);
static struct cache_policy_item_ *cache_queue_policy_get_last_item(
	struct cache_policy_ *);
static struct cache_policy_item_ *cache_queue_policy_get_next_item(
	struct cache_policy_ *, struct cache_policy_item_ *);
static struct cache_policy_item_ *cache_queue_policy_get_prev_item(
	struct cache_policy_ *, struct cache_policy_item_ *);
static void cache_queue_policy_remove_item(struct cache_policy_ *,
	struct cache_policy_item_ *);
static void destroy_cache_queue_policy(struct cache_queue_policy_ *);
static struct cache_queue_policy_ *init_cache_queue_policy(void);

/*
 * All cache_queue_policy_XXX functions below will be used to fill
 * the cache_queue_policy structure. They implement the most functionality of
 * LRU and FIFO policies. LRU and FIFO policies are actually the
 * cache_queue_policy_ with cache_update_item function changed.
 */
static struct cache_policy_item_ *
cache_queue_policy_create_item(void)
{
	struct cache_queue_policy_item_ *retval;

	TRACE_IN(cache_queue_policy_create_item);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	TRACE_OUT(cache_queue_policy_create_item);
	return ((struct cache_policy_item_ *)retval);
}

static void
cache_queue_policy_destroy_item(struct cache_policy_item_ *item)
{

	TRACE_IN(cache_queue_policy_destroy_item);
	assert(item != NULL);
	free(item);
	TRACE_OUT(cache_queue_policy_destroy_item);
}

static void
cache_queue_policy_add_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_queue_policy_ *queue_policy;
	struct cache_queue_policy_item_ *queue_item;

	TRACE_IN(cache_queue_policy_add_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	queue_item = (struct cache_queue_policy_item_ *)item;
	TAILQ_INSERT_TAIL(&queue_policy->head, queue_item, entries);
	TRACE_OUT(cache_queue_policy_add_item);
}

static void
cache_queue_policy_remove_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_queue_policy_ *queue_policy;
	struct cache_queue_policy_item_	*queue_item;

	TRACE_IN(cache_queue_policy_remove_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	queue_item = (struct cache_queue_policy_item_ *)item;
	TAILQ_REMOVE(&queue_policy->head, queue_item, entries);
	TRACE_OUT(cache_queue_policy_remove_item);
}

static struct cache_policy_item_ *
cache_queue_policy_get_first_item(struct cache_policy_ *policy)
{
	struct cache_queue_policy_ *queue_policy;

	TRACE_IN(cache_queue_policy_get_first_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	TRACE_OUT(cache_queue_policy_get_first_item);
	return ((struct cache_policy_item_ *)TAILQ_FIRST(&queue_policy->head));
}

static struct cache_policy_item_ *
cache_queue_policy_get_last_item(struct cache_policy_ *policy)
{
	struct cache_queue_policy_ *queue_policy;

	TRACE_IN(cache_queue_policy_get_last_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	TRACE_OUT(cache_queue_policy_get_last_item);
	return ((struct cache_policy_item_ *)TAILQ_LAST(&queue_policy->head,
		cache_queue_policy_head_));
}

static struct cache_policy_item_ *
cache_queue_policy_get_next_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_queue_policy_ *queue_policy;
	struct cache_queue_policy_item_	*queue_item;

	TRACE_IN(cache_queue_policy_get_next_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	queue_item = (struct cache_queue_policy_item_ *)item;

	TRACE_OUT(cache_queue_policy_get_next_item);
	return ((struct cache_policy_item_ *)TAILQ_NEXT(queue_item, entries));
}

static struct cache_policy_item_ *
cache_queue_policy_get_prev_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_queue_policy_ *queue_policy;
	struct cache_queue_policy_item_	*queue_item;

	TRACE_IN(cache_queue_policy_get_prev_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	queue_item = (struct cache_queue_policy_item_ *)item;

	TRACE_OUT(cache_queue_policy_get_prev_item);
	return ((struct cache_policy_item_ *)TAILQ_PREV(queue_item,
		cache_queue_policy_head_, entries));
}

/*
 * Initializes cache_queue_policy_ by filling the structure with the functions
 * pointers, defined above
 */
static struct cache_queue_policy_ *
init_cache_queue_policy(void)
{
	struct cache_queue_policy_	*retval;

	TRACE_IN(init_cache_queue_policy);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	retval->parent_data.create_item_func = cache_queue_policy_create_item;
	retval->parent_data.destroy_item_func = cache_queue_policy_destroy_item;

	retval->parent_data.add_item_func = cache_queue_policy_add_item;
	retval->parent_data.remove_item_func = cache_queue_policy_remove_item;

	retval->parent_data.get_first_item_func =
		cache_queue_policy_get_first_item;
	retval->parent_data.get_last_item_func =
		cache_queue_policy_get_last_item;
	retval->parent_data.get_next_item_func =
		cache_queue_policy_get_next_item;
	retval->parent_data.get_prev_item_func =
		cache_queue_policy_get_prev_item;

	TAILQ_INIT(&retval->head);
	TRACE_OUT(init_cache_queue_policy);
	return (retval);
}

static void
destroy_cache_queue_policy(struct cache_queue_policy_ *queue_policy)
{
	struct cache_queue_policy_item_	*queue_item;

	TRACE_IN(destroy_cache_queue_policy);
	while (!TAILQ_EMPTY(&queue_policy->head)) {
		queue_item = TAILQ_FIRST(&queue_policy->head);
		TAILQ_REMOVE(&queue_policy->head, queue_item, entries);
		cache_queue_policy_destroy_item(
			(struct cache_policy_item_ *)queue_item);
	}
	free(queue_policy);
	TRACE_OUT(destroy_cache_queue_policy);
}

/*
 * Makes cache_queue_policy_ behave like FIFO policy - we don't do anything,
 * when the cache element is updated. So it always stays in its initial
 * position in the queue - that is exactly the FIFO functionality.
 */
static void
cache_fifo_policy_update_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{

	TRACE_IN(cache_fifo_policy_update_item);
	/* policy and item arguments are ignored */
	TRACE_OUT(cache_fifo_policy_update_item);
}

struct cache_policy_ *
init_cache_fifo_policy(void)
{
	struct cache_queue_policy_ *retval;

	TRACE_IN(init_cache_fifo_policy);
	retval = init_cache_queue_policy();
	retval->parent_data.update_item_func = cache_fifo_policy_update_item;

	TRACE_OUT(init_cache_fifo_policy);
	return ((struct cache_policy_ *)retval);
}

void
destroy_cache_fifo_policy(struct cache_policy_ *policy)
{
	struct cache_queue_policy_	*queue_policy;

	TRACE_IN(destroy_cache_fifo_policy);
	queue_policy = (struct cache_queue_policy_ *)policy;
	destroy_cache_queue_policy(queue_policy);
	TRACE_OUT(destroy_cache_fifo_policy);
}

/*
 * Makes cache_queue_policy_ behave like LRU policy. On each update, cache
 * element is moved to the end of the queue - so it would be deleted in last
 * turn. That is exactly the LRU policy functionality.
 */
static void
cache_lru_policy_update_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_queue_policy_ *queue_policy;
	struct cache_queue_policy_item_ *queue_item;

	TRACE_IN(cache_lru_policy_update_item);
	queue_policy = (struct cache_queue_policy_ *)policy;
	queue_item = (struct cache_queue_policy_item_ *)item;

	TAILQ_REMOVE(&queue_policy->head, queue_item, entries);
	TAILQ_INSERT_TAIL(&queue_policy->head, queue_item, entries);
	TRACE_OUT(cache_lru_policy_update_item);
}

struct cache_policy_ *
init_cache_lru_policy(void)
{
	struct cache_queue_policy_ *retval;

	TRACE_IN(init_cache_lru_policy);
	retval = init_cache_queue_policy();
	retval->parent_data.update_item_func = cache_lru_policy_update_item;

	TRACE_OUT(init_cache_lru_policy);
	return ((struct cache_policy_ *)retval);
}

void
destroy_cache_lru_policy(struct cache_policy_ *policy)
{
	struct cache_queue_policy_	*queue_policy;

	TRACE_IN(destroy_cache_lru_policy);
	queue_policy = (struct cache_queue_policy_ *)policy;
	destroy_cache_queue_policy(queue_policy);
	TRACE_OUT(destroy_cache_lru_policy);
}

/*
 * LFU (least frequently used) policy implementation differs much from the
 * LRU and FIFO (both based on cache_queue_policy_). Almost all cache_policy_
 * functions are implemented specifically for this policy. The idea of this
 * policy is to represent frequency (real number) as the integer number and
 * use it as the index in the array. Each array's element is
 * the list of elements. For example, if we have the 100-elements
 * array for this policy, the elements with frequency 0.1 (calls per-second)
 * would be in 10th element of the array.
 */
static struct cache_policy_item_ *
cache_lfu_policy_create_item(void)
{
	struct cache_lfu_policy_item_ *retval;

	TRACE_IN(cache_lfu_policy_create_item);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	TRACE_OUT(cache_lfu_policy_create_item);
	return ((struct cache_policy_item_ *)retval);
}

static void
cache_lfu_policy_destroy_item(struct cache_policy_item_ *item)
{

	TRACE_IN(cache_lfu_policy_destroy_item);
	assert(item != NULL);
	free(item);
	TRACE_OUT(cache_lfu_policy_destroy_item);
}

/*
 * When placed in the LFU policy queue for the first time, the maximum
 * frequency is assigned to the element
 */
static void
cache_lfu_policy_add_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;

	TRACE_IN(cache_lfu_policy_add_item);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	lfu_item = (struct cache_lfu_policy_item_ *)item;

	lfu_item->frequency = CACHELIB_MAX_FREQUENCY - 1;
	TAILQ_INSERT_HEAD(&(lfu_policy->groups[CACHELIB_MAX_FREQUENCY - 1]),
		lfu_item, entries);
	TRACE_OUT(cache_lfu_policy_add_item);
}

/*
 * On each update the frequency of the element is recalculated and, if it
 * changed, the element would be moved to the another place in the array.
 */
static void
cache_lfu_policy_update_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;
	int index;

	TRACE_IN(cache_lfu_policy_update_item);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	lfu_item = (struct cache_lfu_policy_item_ *)item;

	/*
	 * We calculate the square of the request_count to avoid grouping of
	 * all elements at the start of the array (for example, if array size is
	 * 100 and most of its elements has frequency below the 0.01, they
	 * all would be grouped in the first array's position). Other
	 * techniques should be used here later to ensure, that elements are
	 * equally distributed  in the array and not grouped in its beginning.
	 */
	if (lfu_item->parent_data.last_request_time.tv_sec !=
		lfu_item->parent_data.creation_time.tv_sec) {
		index = ((double)lfu_item->parent_data.request_count *
			(double)lfu_item->parent_data.request_count /
			(lfu_item->parent_data.last_request_time.tv_sec -
			    lfu_item->parent_data.creation_time.tv_sec + 1)) *
			    CACHELIB_MAX_FREQUENCY;
		if (index >= CACHELIB_MAX_FREQUENCY)
			index = CACHELIB_MAX_FREQUENCY - 1;
	} else
		index = CACHELIB_MAX_FREQUENCY - 1;

	TAILQ_REMOVE(&(lfu_policy->groups[lfu_item->frequency]), lfu_item,
		entries);
	lfu_item->frequency = index;
	TAILQ_INSERT_HEAD(&(lfu_policy->groups[index]), lfu_item, entries);

	TRACE_OUT(cache_lfu_policy_update_item);
}

static void
cache_lfu_policy_remove_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;

	TRACE_IN(cache_lfu_policy_remove_item);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	lfu_item = (struct cache_lfu_policy_item_ *)item;

	TAILQ_REMOVE(&(lfu_policy->groups[lfu_item->frequency]), lfu_item,
		entries);
	TRACE_OUT(cache_lfu_policy_remove_item);
}

static struct cache_policy_item_ *
cache_lfu_policy_get_first_item(struct cache_policy_ *policy)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;
	int i;

	TRACE_IN(cache_lfu_policy_get_first_item);
	lfu_item = NULL;
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	for (i = 0; i < CACHELIB_MAX_FREQUENCY; ++i)
		if (!TAILQ_EMPTY(&(lfu_policy->groups[i]))) {
			lfu_item = TAILQ_FIRST(&(lfu_policy->groups[i]));
			break;
		}

	TRACE_OUT(cache_lfu_policy_get_first_item);
	return ((struct cache_policy_item_ *)lfu_item);
}

static struct cache_policy_item_ *
cache_lfu_policy_get_last_item(struct cache_policy_ *policy)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;
	int i;

	TRACE_IN(cache_lfu_policy_get_last_item);
	lfu_item = NULL;
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	for (i = CACHELIB_MAX_FREQUENCY - 1; i >= 0; --i)
		if (!TAILQ_EMPTY(&(lfu_policy->groups[i]))) {
			lfu_item = TAILQ_LAST(&(lfu_policy->groups[i]),
				cache_lfu_policy_group_);
			break;
		}

	TRACE_OUT(cache_lfu_policy_get_last_item);
	return ((struct cache_policy_item_ *)lfu_item);
}

static struct cache_policy_item_ *
cache_lfu_policy_get_next_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;
	int i;

	TRACE_IN(cache_lfu_policy_get_next_item);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	lfu_item = TAILQ_NEXT((struct cache_lfu_policy_item_ *)item, entries);
	if (lfu_item == NULL)
	{
		for (i = ((struct cache_lfu_policy_item_ *)item)->frequency + 1;
			i < CACHELIB_MAX_FREQUENCY; ++i) {
			if (!TAILQ_EMPTY(&(lfu_policy->groups[i]))) {
			    lfu_item = TAILQ_FIRST(&(lfu_policy->groups[i]));
			    break;
			}
		}
	}

	TRACE_OUT(cache_lfu_policy_get_next_item);
	return ((struct cache_policy_item_ *)lfu_item);
}

static struct cache_policy_item_ *
cache_lfu_policy_get_prev_item(struct cache_policy_ *policy,
	struct cache_policy_item_ *item)
{
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;
	int i;

	TRACE_IN(cache_lfu_policy_get_prev_item);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	lfu_item = TAILQ_PREV((struct cache_lfu_policy_item_ *)item,
		cache_lfu_policy_group_, entries);
	if (lfu_item == NULL)
	{
		for (i = ((struct cache_lfu_policy_item_ *)item)->frequency - 1;
			i >= 0; --i)
			if (!TAILQ_EMPTY(&(lfu_policy->groups[i]))) {
				lfu_item = TAILQ_LAST(&(lfu_policy->groups[i]),
					cache_lfu_policy_group_);
				break;
		}
	}

	TRACE_OUT(cache_lfu_policy_get_prev_item);
	return ((struct cache_policy_item_ *)lfu_item);
}

/*
 * Initializes the cache_policy_ structure by filling it with appropriate
 * functions pointers
 */
struct cache_policy_ *
init_cache_lfu_policy(void)
{
	int i;
	struct cache_lfu_policy_ *retval;

	TRACE_IN(init_cache_lfu_policy);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	retval->parent_data.create_item_func = cache_lfu_policy_create_item;
	retval->parent_data.destroy_item_func = cache_lfu_policy_destroy_item;

	retval->parent_data.add_item_func = cache_lfu_policy_add_item;
	retval->parent_data.update_item_func = cache_lfu_policy_update_item;
	retval->parent_data.remove_item_func = cache_lfu_policy_remove_item;

	retval->parent_data.get_first_item_func =
		cache_lfu_policy_get_first_item;
	retval->parent_data.get_last_item_func =
		cache_lfu_policy_get_last_item;
	retval->parent_data.get_next_item_func =
		cache_lfu_policy_get_next_item;
	retval->parent_data.get_prev_item_func =
		cache_lfu_policy_get_prev_item;

	for (i = 0; i < CACHELIB_MAX_FREQUENCY; ++i)
		TAILQ_INIT(&(retval->groups[i]));

	TRACE_OUT(init_cache_lfu_policy);
	return ((struct cache_policy_ *)retval);
}

void
destroy_cache_lfu_policy(struct cache_policy_ *policy)
{
	int i;
	struct cache_lfu_policy_ *lfu_policy;
	struct cache_lfu_policy_item_ *lfu_item;

	TRACE_IN(destroy_cache_lfu_policy);
	lfu_policy = (struct cache_lfu_policy_ *)policy;
	for (i = 0; i < CACHELIB_MAX_FREQUENCY; ++i) {
		while (!TAILQ_EMPTY(&(lfu_policy->groups[i]))) {
			lfu_item = TAILQ_FIRST(&(lfu_policy->groups[i]));
			TAILQ_REMOVE(&(lfu_policy->groups[i]), lfu_item,
				entries);
			cache_lfu_policy_destroy_item(
				(struct cache_policy_item_ *)lfu_item);
		}
	}
	free(policy);
	TRACE_OUT(destroy_cache_lfu_policy);
}
