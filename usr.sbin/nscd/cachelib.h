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

#ifndef __NSCD_CACHELIB_H__
#define __NSCD_CACHELIB_H__

#include "hashtable.h"
#include "cacheplcs.h"

enum cache_entry_t	{
	CET_COMMON = 0,	/* cache item is atomic */
	CET_MULTIPART	/* cache item is formed part by part */
};

enum cache_transformation_t {
	CTT_FLUSH = 0,	/* flush the cache - delete all obsolete items */
	CTT_CLEAR = 1	/* delete all items in the cache */
};

/* cache deletion policy type enum */
enum cache_policy_t {
	CPT_FIFO = 0, 	/* first-in first-out */
	CPT_LRU = 1,	/* least recently used */
	CPT_LFU = 2 	/* least frequently used */
};

/* multipart sessions can be used for reading and writing */
enum cache_mp_session_t {
	CMPT_READ_SESSION,
	CMPT_WRITE_SESSION
};

/*
 * When doing partial transformations of entries (which are applied for
 * elements with keys, that contain specified buffer in its left or
 * right part), this enum will show the needed position of the key part.
 */
enum part_position_t {
	KPPT_LEFT,
	KPPT_RIGHT
};

/* num_levels attribute is obsolete, i think - user can always emulate it
 * by using one entry.
 * get_time_func is needed to have the clocks-independent counter
 */
struct cache_params {
	void	(*get_time_func)(struct timeval *);
};

/*
 * base structure - normal_cache_entry_params and multipart_cache_entry_params
 * are "inherited" from it
 */
struct cache_entry_params {
	enum cache_entry_t entry_type;
	char	*entry_name;
};

/* params, used for most entries */
struct common_cache_entry_params {
	struct cache_entry_params cep;

	size_t	cache_entries_size;

	size_t	max_elemsize;		/* if 0 then no check is made */
	size_t	satisf_elemsize;	/* if entry size is exceeded,
					 * this number of elements will be left,
					 * others will be deleted */
	int	confidence_threshold;	/* number matching replies required */
	struct timeval	max_lifetime;	/* if 0 then no check is made */
	enum cache_policy_t policy;	/* policy used for transformations */
};

/* params, used for multipart entries */
struct	mp_cache_entry_params {
	struct cache_entry_params cep;

	/* unique fields */
	size_t	max_elemsize;	/* if 0 then no check is made */
	size_t	max_sessions;	/* maximum number of active sessions */

	struct timeval	max_lifetime;	/* maximum elements lifetime */
};

struct cache_ht_item_data_ {
    	/* key is the bytes sequence only - not the null-terminated string */
	char	*key;
    	size_t	key_size;

	char	*value;
	size_t	value_size;

	struct cache_policy_item_ *fifo_policy_item;
	int	confidence;	/* incremented for each verification */
};

struct cache_ht_item_ {
	HASHTABLE_ENTRY_HEAD(ht_item_, struct cache_ht_item_data_) data;
};

struct cache_entry_ {
	char	*name;
	struct cache_entry_params *params;
};

struct cache_common_entry_ {
	char	*name;
	struct cache_entry_params *params;

	struct common_cache_entry_params common_params;

	HASHTABLE_HEAD(cache_ht_, cache_ht_item_) items;
	size_t items_size;

	/*
	 * Entry always has the FIFO policy, that is used to eliminate old
	 * elements (the ones, with lifetime more than max_lifetime). Besides,
	 * user can specify another policy to be applied, when there are too
	 * many elements in the entry. So policies_size can be 1 or 2.
	 */
	struct cache_policy_ **policies;
	size_t policies_size;

	void	(*get_time_func)(struct timeval *);
};

struct cache_mp_data_item_ {
	char	*value;
	size_t	value_size;

	TAILQ_ENTRY(cache_mp_data_item_) entries;
};

struct cache_mp_write_session_ {
	struct cache_mp_entry_	*parent_entry;

	/*
	 * All items are accumulated in this queue. When the session is
	 * committed, they all will be copied to the multipart entry.
	 */
	TAILQ_HEAD(cache_mp_data_item_head, cache_mp_data_item_) items;
	size_t	items_size;

	TAILQ_ENTRY(cache_mp_write_session_) entries;
};

struct cache_mp_read_session_ {
	struct cache_mp_entry_ *parent_entry;
	struct cache_mp_data_item_ *current_item;

	TAILQ_ENTRY(cache_mp_read_session_) entries;
};

struct cache_mp_entry_ {
	char	*name;
	struct cache_entry_params *params;

	struct mp_cache_entry_params mp_params;

	/* All opened write sessions */
	TAILQ_HEAD(write_sessions_head, cache_mp_write_session_) ws_head;
	size_t	ws_size;

	/* All opened read sessions */
	TAILQ_HEAD(read_sessions_head, cache_mp_read_session_) rs_head;
	size_t	rs_size;

	/*
	 * completed_write_session is the committed write sessions. All read
	 * sessions use data from it. If the completed_write_session is out of
	 * date, but still in use by some of the read sessions, the newly
	 * committed write session is stored in the pending_write_session.
	 * In such a case, completed_write_session will be substituted with
	 * pending_write_session as soon as it won't be used by any of
	 * the read sessions.
	 */
	struct cache_mp_write_session_	*completed_write_session;
	struct cache_mp_write_session_	*pending_write_session;
	struct timeval	creation_time;
	struct timeval	last_request_time;

	void	(*get_time_func)(struct timeval *);
};

struct cache_ {
	struct cache_params params;

	struct cache_entry_ **entries;
	size_t	entries_capacity;
	size_t	entries_size;
};

/* simple abstractions - for not to write "struct" every time */
typedef struct cache_		*cache;
typedef struct cache_entry_	*cache_entry;
typedef struct cache_mp_write_session_	*cache_mp_write_session;
typedef struct cache_mp_read_session_	*cache_mp_read_session;

#define INVALID_CACHE		(NULL)
#define INVALID_CACHE_ENTRY	(NULL)
#define INVALID_CACHE_MP_WRITE_SESSION	(NULL)
#define INVALID_CACHE_MP_READ_SESSION	(NULL)

/*
 * NOTE: all cache operations are thread-unsafe. You must ensure thread-safety
 * externally, by yourself.
 */

/* cache initialization/destruction routines */
cache init_cache(struct cache_params const *);
void destroy_cache(cache);

/* cache entries manipulation routines */
int register_cache_entry(cache, struct cache_entry_params const *);
int unregister_cache_entry(cache, const char *);
cache_entry find_cache_entry(cache, const char *);

/* read/write operations used on common entries */
int cache_read(cache_entry, const char *, size_t, char *, size_t *);
int cache_write(cache_entry, const char *, size_t, char const *, size_t);

/* read/write operations used on multipart entries */
cache_mp_write_session open_cache_mp_write_session(cache_entry);
int cache_mp_write(cache_mp_write_session, char *, size_t);
void abandon_cache_mp_write_session(cache_mp_write_session);
void close_cache_mp_write_session(cache_mp_write_session);

cache_mp_read_session open_cache_mp_read_session(cache_entry);
int cache_mp_read(cache_mp_read_session, char *, size_t *);
void close_cache_mp_read_session(cache_mp_read_session);

/* transformation routines */
int transform_cache_entry(cache_entry, enum cache_transformation_t);
int transform_cache_entry_part(cache_entry, enum cache_transformation_t,
	const char *, size_t, enum part_position_t);

#endif
