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

#ifndef __NSCD_CACHEPLCS_H__
#define __NSCD_CACHEPLCS_H__

#include <sys/queue.h>

/* common policy definitions */
#define CACHELIB_MAX_FREQUENCY 100

/*
 * cache_policy_item_ represents some abstract cache element in the policy
 * queue. connected_item pointers to the corresponding cache_policy_item_ in
 * another policy queue.
 */
struct cache_policy_item_ {
	char	*key;
    	size_t	key_size;

	size_t	request_count;
	struct timeval last_request_time;
	struct timeval creation_time;

	struct cache_policy_item_ *connected_item;
};

/*
 * cache_policy_ represents an abstract policy queue. It can be customized by
 * setting appropriate function pointers
 */
struct cache_policy_ {
	struct cache_policy_item_* (*create_item_func)(void);
	void (*destroy_item_func)(struct cache_policy_item_ *);

	void (*add_item_func)(struct cache_policy_ *,
		struct cache_policy_item_ *);
	void (*remove_item_func)(struct cache_policy_ *,
		struct cache_policy_item_ *);
	void (*update_item_func)(struct cache_policy_ *,
		struct cache_policy_item_ *);

	struct cache_policy_item_ *(*get_first_item_func)(
		struct cache_policy_ *);
	struct cache_policy_item_ *(*get_last_item_func)(
		struct cache_policy_ *);
	struct cache_policy_item_ *(*get_next_item_func)(
		struct cache_policy_ *, struct cache_policy_item_ *);
	struct cache_policy_item_ *(*get_prev_item_func)(
		struct cache_policy_ *, struct cache_policy_item_ *);
};

/*
 * LFU cache policy item "inherited" from cache_policy_item_ structure
 */
struct cache_lfu_policy_item_ {
	struct cache_policy_item_ parent_data;
	int	frequency;

	TAILQ_ENTRY(cache_lfu_policy_item_) entries;
};

TAILQ_HEAD(cache_lfu_policy_group_, cache_lfu_policy_item_);

/*
 * LFU policy queue "inherited" from cache_policy_.
 */
struct cache_lfu_policy_ {
	struct cache_policy_ parent_data;
	struct cache_lfu_policy_group_ groups[CACHELIB_MAX_FREQUENCY];
};

/*
 * LRU and FIFO policies item "inherited" from cache_policy_item_
 */
struct cache_queue_policy_item_ {
	struct cache_policy_item_ parent_data;
	TAILQ_ENTRY(cache_queue_policy_item_) entries;
};

/*
 * LRU and FIFO policies "inherited" from cache_policy_
 */
struct cache_queue_policy_ {
	struct cache_policy_ parent_data;
	TAILQ_HEAD(cache_queue_policy_head_, cache_queue_policy_item_) head;
};

typedef struct cache_queue_policy_ cache_fifo_policy_;
typedef struct cache_queue_policy_ cache_lru_policy_;

/* fifo policy routines */
struct cache_policy_ *init_cache_fifo_policy(void);
void destroy_cache_fifo_policy(struct cache_policy_ *);

/* lru policy routines */
struct cache_policy_ *init_cache_lru_policy(void);
void destroy_cache_lru_policy(struct cache_policy_ *);

/* lfu policy routines */
struct cache_policy_ *init_cache_lfu_policy(void);
void destroy_cache_lfu_policy(struct cache_policy_ *);

#endif
