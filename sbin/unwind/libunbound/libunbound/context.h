/*
 * libunbound/context.h - validating context for unbound internal use
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains the validator context structure.
 */
#ifndef LIBUNBOUND_CONTEXT_H
#define LIBUNBOUND_CONTEXT_H
#include "util/locks.h"
#include "util/alloc.h"
#include "util/rbtree.h"
#include "services/modstack.h"
#include "libunbound/unbound.h"
#include "libunbound/unbound-event.h"
#include "util/data/packed_rrset.h"
struct libworker;
struct tube;
struct sldns_buffer;
struct ub_event_base;

/** store that the logfile has a debug override */
extern int ctx_logfile_overridden;

/**
 * The context structure
 *
 * Contains two pipes for async service
 *	qq : write queries to the async service pid/tid.
 *	rr : read results from the async service pid/tid.
 */
struct ub_ctx {
	/* --- pipes --- */
	/** mutex on query write pipe */
	lock_basic_type qqpipe_lock;
	/** the query write pipe */
	struct tube* qq_pipe;
	/** mutex on result read pipe */
	lock_basic_type rrpipe_lock;
	/** the result read pipe */
	struct tube* rr_pipe;

	/* --- shared data --- */
	/** mutex for access to env.cfg, finalized and dothread */
	lock_basic_type cfglock;
	/** 
	 * The context has been finalized 
	 * This is after config when the first resolve is done.
	 * The modules are inited (module-init()) and shared caches created.
	 */
	int finalized;

	/** is bg worker created yet ? */
	int created_bg;
	/** pid of bg worker process */
	pid_t bg_pid;
	/** tid of bg worker thread */
	ub_thread_type bg_tid;
	/** pid when pipes are created. This was the process when the
	 * setup was called. Helps with clean up, so we can tell after a fork
	 * which side of the fork the delete is on. */
	pid_t pipe_pid;
	/** when threaded, the worker that exists in the created thread. */
	struct libworker* thread_worker;

	/** do threading (instead of forking) for async resolution */
	int dothread;
	/** next thread number for new threads */
	int thr_next_num;
	/** if logfile is overridden */
	int logfile_override;
	/** what logfile to use instead */
	FILE* log_out;
	/** 
	 * List of alloc-cache-id points per threadnum for notinuse threads.
	 * Simply the entire struct alloc_cache with the 'super' member used
	 * to link a simply linked list. Reset super member to the superalloc
	 * before use.
	 */
	struct alloc_cache* alloc_list;

	/** shared caches, and so on */
	struct alloc_cache superalloc;
	/** module env master value */
	struct module_env* env;
	/** module stack */
	struct module_stack mods;
	/** local authority zones */
	struct local_zones* local_zones;
	/** random state used to seed new random state structures */
	struct ub_randstate* seed_rnd;

	/** event base for event oriented interface */
	struct ub_event_base* event_base;
	/** true if the event_base is a pluggable base that is malloced
	 * with a user event base inside, if so, clean up the pluggable alloc*/
	int event_base_malloced;
	/** libworker for event based interface */
	struct libworker* event_worker;

	/** next query number (to try) to use */
	int next_querynum;
	/** number of async queries outstanding */
	size_t num_async;
	/** 
	 * Tree of outstanding queries. Indexed by querynum 
	 * Used when results come in for async to lookup.
	 * Used when cancel is done for lookup (and delete).
	 * Used to see if querynum is free for use.
	 * Content of type ctx_query.
	 */ 
	rbtree_type queries;
};

/**
 * The queries outstanding for the libunbound resolver.
 * These are outstanding for async resolution.
 * But also, outstanding for sync resolution by one of the threads that
 * has joined the threadpool.
 */
struct ctx_query {
	/** node in rbtree, must be first entry, key is ptr to the querynum */
	struct rbnode_type node;
	/** query id number, key for node */
	int querynum;
	/** was this an async query? */
	int async;
	/** was this query cancelled (for bg worker) */
	int cancelled;

	/** for async query, the callback function of type ub_callback_type */
	ub_callback_type cb;
	/** for event callbacks the type is ub_event_callback_type */
        ub_event_callback_type cb_event;
	/** for async query, the callback user arg */
	void* cb_arg;

	/** answer message, result from resolver lookup. */
	uint8_t* msg;
	/** resulting message length. */
	size_t msg_len;
	/** validation status on security */
	enum sec_status msg_security;
	/** store libworker that is handling this query */
	struct libworker* w;

	/** result structure, also contains original query, type, class.
	 * malloced ptr ready to hand to the client. */
	struct ub_result* res;
};

/**
 * Command codes for libunbound pipe.
 *
 * Serialization looks like this:
 * 	o length (of remainder) uint32.
 * 	o uint32 command code.
 * 	o per command format.
 */
enum ub_ctx_cmd {
	/** QUIT */
	UB_LIBCMD_QUIT = 0,
	/** New query, sent to bg worker */
	UB_LIBCMD_NEWQUERY,
	/** Cancel query, sent to bg worker */
	UB_LIBCMD_CANCEL,
	/** Query result, originates from bg worker */
	UB_LIBCMD_ANSWER
};

/** 
 * finalize a context.
 * @param ctx: context to finalize. creates shared data.
 * @return 0 if OK, or errcode.
 */
int context_finalize(struct ub_ctx* ctx);

/** compare two ctx_query elements */
int context_query_cmp(const void* a, const void* b);

/** 
 * delete context query
 * @param q: query to delete, including message packet and prealloc result
 */
void context_query_delete(struct ctx_query* q);

/**
 * Create new query in context, add to querynum list.
 * @param ctx: context
 * @param name: query name
 * @param rrtype: type
 * @param rrclass: class
 * @param cb: callback for async, or NULL for sync.
 * @param cb_event: event callback for async, or NULL for sync.
 * @param cbarg: user arg for async queries.
 * @return new ctx_query or NULL for malloc failure.
 */
struct ctx_query* context_new(struct ub_ctx* ctx, const char* name, int rrtype,
        int rrclass,  ub_callback_type cb, ub_event_callback_type cb_event,
	void* cbarg);

/**
 * Get a new alloc. Creates a new one or uses a cached one.
 * @param ctx: context
 * @param locking: if true, cfglock is locked while getting alloc.
 * @return an alloc, or NULL on mem error.
 */
struct alloc_cache* context_obtain_alloc(struct ub_ctx* ctx, int locking);

/**
 * Release an alloc. Puts it into the cache.
 * @param ctx: context
 * @param locking: if true, cfglock is locked while releasing alloc.
 * @param alloc: alloc to relinquish.
 */
void context_release_alloc(struct ub_ctx* ctx, struct alloc_cache* alloc,
	int locking);

/**
 * Serialize a context query that questions data.
 * This serializes the query name, type, ...
 * As well as command code 'new_query'.
 * @param q: context query
 * @param len: the length of the allocation is returned.
 * @return: an alloc, or NULL on mem error.
 */
uint8_t* context_serialize_new_query(struct ctx_query* q, uint32_t* len);

/**
 * Serialize a context_query result to hand back to user.
 * This serializes the query name, type, ..., and result.
 * As well as command code 'answer'.
 * @param q: context query
 * @param err: error code to pass to client.
 * @param pkt: the packet to add, can be NULL.
 * @param len: the length of the allocation is returned.
 * @return: an alloc, or NULL on mem error.
 */
uint8_t* context_serialize_answer(struct ctx_query* q, int err, 
	struct sldns_buffer* pkt, uint32_t* len);

/**
 * Serialize a query cancellation. Serializes query async id
 * as well as command code 'cancel'
 * @param q: context query
 * @param len: the length of the allocation is returned.
 * @return: an alloc, or NULL on mem error.
 */
uint8_t* context_serialize_cancel(struct ctx_query* q, uint32_t* len);

/**
 * Serialize a 'quit' command.
 * @param len: the length of the allocation is returned.
 * @return: an alloc, or NULL on mem error.
 */
uint8_t* context_serialize_quit(uint32_t* len);

/**
 * Obtain command code from serialized buffer
 * @param p: buffer serialized.
 * @param len: length of buffer.
 * @return command code or QUIT on error.
 */
enum ub_ctx_cmd context_serial_getcmd(uint8_t* p, uint32_t len);

/**
 * Lookup query from new_query buffer.
 * @param ctx: context
 * @param p: buffer serialized.
 * @param len: length of buffer.
 * @return looked up ctx_query or NULL for malloc failure.
 */
struct ctx_query* context_lookup_new_query(struct ub_ctx* ctx, 
	uint8_t* p, uint32_t len);

/**
 * Deserialize a new_query buffer.
 * @param ctx: context
 * @param p: buffer serialized.
 * @param len: length of buffer.
 * @return new ctx_query or NULL for malloc failure.
 */
struct ctx_query* context_deserialize_new_query(struct ub_ctx* ctx, 
	uint8_t* p, uint32_t len);

/**
 * Deserialize an answer buffer.
 * @param ctx: context
 * @param p: buffer serialized.
 * @param len: length of buffer.
 * @param err: error code to be returned to client is passed.
 * @return ctx_query with answer added or NULL for malloc failure.
 */
struct ctx_query* context_deserialize_answer(struct ub_ctx* ctx, 
	uint8_t* p, uint32_t len, int* err);

/**
 * Deserialize a cancel buffer.
 * @param ctx: context
 * @param p: buffer serialized.
 * @param len: length of buffer.
 * @return ctx_query to cancel or NULL for failure.
 */
struct ctx_query* context_deserialize_cancel(struct ub_ctx* ctx, 
	uint8_t* p, uint32_t len);

#endif /* LIBUNBOUND_CONTEXT_H */
