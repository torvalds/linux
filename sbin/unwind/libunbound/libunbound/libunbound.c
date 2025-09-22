/*
 * unbound.c - unbound validating resolver public API implementation
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
 * This file contains functions to resolve DNS queries and 
 * validate the answers. Synchronously and asynchronously.
 *
 */

/* include the public api first, it should be able to stand alone */
#include "libunbound/unbound.h"
#include "libunbound/unbound-event.h"
#include "config.h"
#include <ctype.h>
#include "libunbound/context.h"
#include "libunbound/libworker.h"
#include "util/locks.h"
#include "util/config_file.h"
#include "util/alloc.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/random.h"
#include "util/net_help.h"
#include "util/tube.h"
#include "util/ub_event.h"
#include "util/edns.h"
#include "services/modstack.h"
#include "services/localzone.h"
#include "services/cache/infra.h"
#include "services/cache/rrset.h"
#include "services/authzone.h"
#include "services/listen_dnsport.h"
#include "sldns/sbuffer.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#ifdef HAVE_PTHREAD
#include <signal.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#if defined(UB_ON_WINDOWS) && defined (HAVE_WINDOWS_H)
#include <windows.h>
#include <iphlpapi.h>
#endif /* UB_ON_WINDOWS */

/** store that the logfile has a debug override */
int ctx_logfile_overridden = 0;

/** create context functionality, but no pipes */
static struct ub_ctx* ub_ctx_create_nopipe(void)
{
	struct ub_ctx* ctx;
#ifdef USE_WINSOCK
	int r;
	WSADATA wsa_data;
#endif
	
	checklock_start();
	if(!ctx_logfile_overridden)
		log_init(NULL, 0, NULL); /* logs to stderr */
	log_ident_set("libunbound");
#ifdef USE_WINSOCK
	if((r = WSAStartup(MAKEWORD(2,2), &wsa_data)) != 0) {
		log_err("could not init winsock. WSAStartup: %s",
			wsa_strerror(r));
		return NULL;
	}
#endif
	verbosity = NO_VERBOSE; /* errors only */
	checklock_start();
	ctx = (struct ub_ctx*)calloc(1, sizeof(*ctx));
	if(!ctx) {
		errno = ENOMEM;
		return NULL;
	}
	alloc_init(&ctx->superalloc, NULL, 0);
	if(!(ctx->seed_rnd = ub_initstate(NULL))) {
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}
	lock_basic_init(&ctx->qqpipe_lock);
	lock_basic_init(&ctx->rrpipe_lock);
	lock_basic_init(&ctx->cfglock);
	ctx->env = (struct module_env*)calloc(1, sizeof(*ctx->env));
	if(!ctx->env) {
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}
	ctx->env->cfg = config_create_forlib();
	if(!ctx->env->cfg) {
		free(ctx->env);
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}
	/* init edns_known_options */
	if(!edns_known_options_init(ctx->env)) {
		config_delete(ctx->env->cfg);
		free(ctx->env);
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}
	ctx->env->auth_zones = auth_zones_create();
	if(!ctx->env->auth_zones) {
		edns_known_options_delete(ctx->env);
		config_delete(ctx->env->cfg);
		free(ctx->env);
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}
	ctx->env->edns_strings = edns_strings_create();
	if(!ctx->env->edns_strings) {
		auth_zones_delete(ctx->env->auth_zones);
		edns_known_options_delete(ctx->env);
		config_delete(ctx->env->cfg);
		free(ctx->env);
		ub_randfree(ctx->seed_rnd);
		free(ctx);
		errno = ENOMEM;
		return NULL;
	}

	ctx->env->alloc = &ctx->superalloc;
	ctx->env->worker = NULL;
	ctx->env->need_to_validate = 0;
	modstack_init(&ctx->mods);
	ctx->env->modstack = &ctx->mods;
	rbtree_init(&ctx->queries, &context_query_cmp);
	return ctx;
}

struct ub_ctx* 
ub_ctx_create(void)
{
	struct ub_ctx* ctx = ub_ctx_create_nopipe();
	if(!ctx)
		return NULL;
	if((ctx->qq_pipe = tube_create()) == NULL) {
		int e = errno;
		ub_randfree(ctx->seed_rnd);
		config_delete(ctx->env->cfg);
		modstack_call_deinit(&ctx->mods, ctx->env);
		modstack_call_destartup(&ctx->mods, ctx->env);
		modstack_free(&ctx->mods);
		listen_desetup_locks();
		edns_known_options_delete(ctx->env);
		edns_strings_delete(ctx->env->edns_strings);
		free(ctx->env);
		free(ctx);
		errno = e;
		return NULL;
	}
	if((ctx->rr_pipe = tube_create()) == NULL) {
		int e = errno;
		tube_delete(ctx->qq_pipe);
		ub_randfree(ctx->seed_rnd);
		config_delete(ctx->env->cfg);
		modstack_call_deinit(&ctx->mods, ctx->env);
		modstack_call_destartup(&ctx->mods, ctx->env);
		modstack_free(&ctx->mods);
		listen_desetup_locks();
		edns_known_options_delete(ctx->env);
		edns_strings_delete(ctx->env->edns_strings);
		free(ctx->env);
		free(ctx);
		errno = e;
		return NULL;
	}
	return ctx;
}

struct ub_ctx* 
ub_ctx_create_ub_event(struct ub_event_base* ueb)
{
	struct ub_ctx* ctx = ub_ctx_create_nopipe();
	if(!ctx)
		return NULL;
	/* no pipes, but we have the locks to make sure everything works */
	ctx->created_bg = 0;
	ctx->dothread = 1; /* the processing is in the same process,
		makes ub_cancel and ub_ctx_delete do the right thing */
	ctx->event_base = ueb;
	return ctx;
}

struct ub_ctx* 
ub_ctx_create_event(struct event_base* eb)
{
	struct ub_ctx* ctx = ub_ctx_create_nopipe();
	if(!ctx)
		return NULL;
	/* no pipes, but we have the locks to make sure everything works */
	ctx->created_bg = 0;
	ctx->dothread = 1; /* the processing is in the same process,
		makes ub_cancel and ub_ctx_delete do the right thing */
	ctx->event_base = ub_libevent_event_base(eb);
	if (!ctx->event_base) {
		ub_ctx_delete(ctx);
		return NULL;
	}
	ctx->event_base_malloced = 1;
	return ctx;
}
	
/** delete q */
static void
delq(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct ctx_query* q = (struct ctx_query*)n;
	context_query_delete(q);
}

/** stop the bg thread */
static void ub_stop_bg(struct ub_ctx* ctx)
{
	/* stop the bg thread */
	lock_basic_lock(&ctx->cfglock);
	if(ctx->created_bg) {
		uint8_t* msg;
		uint32_t len;
		uint32_t cmd = UB_LIBCMD_QUIT;
		lock_basic_unlock(&ctx->cfglock);
		lock_basic_lock(&ctx->qqpipe_lock);
		(void)tube_write_msg(ctx->qq_pipe, (uint8_t*)&cmd, 
			(uint32_t)sizeof(cmd), 0);
		lock_basic_unlock(&ctx->qqpipe_lock);
		lock_basic_lock(&ctx->rrpipe_lock);
		while(tube_read_msg(ctx->rr_pipe, &msg, &len, 0)) {
			/* discard all results except a quit confirm */
			if(context_serial_getcmd(msg, len) == UB_LIBCMD_QUIT) {
				free(msg);
				break;
			}
			free(msg);
		}
		lock_basic_unlock(&ctx->rrpipe_lock);

		/* if bg worker is a thread, wait for it to exit, so that all
	 	 * resources are really gone. */
		lock_basic_lock(&ctx->cfglock);
		if(ctx->dothread) {
			lock_basic_unlock(&ctx->cfglock);
			ub_thread_join(ctx->bg_tid);
		} else {
			lock_basic_unlock(&ctx->cfglock);
#ifndef UB_ON_WINDOWS
			if(waitpid(ctx->bg_pid, NULL, 0) == -1) {
				if(verbosity > 2)
					log_err("waitpid: %s", strerror(errno));
			}
#endif
		}
	}
	else {
		lock_basic_unlock(&ctx->cfglock);
	}
}

void 
ub_ctx_delete(struct ub_ctx* ctx)
{
	struct alloc_cache* a, *na;
	int do_stop = 1;
	if(!ctx) return;

	/* if the delete is called but it has forked, and before the fork
	 * the context was finalized, then the bg worker is not stopped
	 * from here. There is one worker, but two contexts that refer to
	 * it and only one should clean up, the one with getpid == pipe_pid.*/
	if(ctx->created_bg && ctx->pipe_pid != getpid()) {
		do_stop = 0;
#ifndef USE_WINSOCK
		/* Stop events from getting deregistered, if the backend is
		 * epoll, the epoll fd is the same as the other process.
		 * That process should deregister them. */
		if(ctx->qq_pipe->listen_com)
			ctx->qq_pipe->listen_com->event_added = 0;
		if(ctx->qq_pipe->res_com)
			ctx->qq_pipe->res_com->event_added = 0;
		if(ctx->rr_pipe->listen_com)
			ctx->rr_pipe->listen_com->event_added = 0;
		if(ctx->rr_pipe->res_com)
			ctx->rr_pipe->res_com->event_added = 0;
#endif
	}
	/* see if bg thread is created and if threads have been killed */
	/* no locks, because those may be held by terminated threads */
	/* for processes the read pipe is closed and we see that on read */
#ifdef HAVE_PTHREAD
	if(ctx->created_bg && ctx->dothread && do_stop) {
		if(pthread_kill(ctx->bg_tid, 0) == ESRCH) {
			/* thread has been killed */
			do_stop = 0;
		}
	}
#endif /* HAVE_PTHREAD */
	if(do_stop)
		ub_stop_bg(ctx);
	if(ctx->created_bg && ctx->pipe_pid != getpid() && ctx->thread_worker) {
		/* This delete is happening from a different process. Delete
		 * the thread worker from this process memory space. The
		 * thread is not there to do so, so it is freed here. */
		struct ub_event_base* evbase = comm_base_internal(
			ctx->thread_worker->base);
		libworker_delete_event(ctx->thread_worker);
		ctx->thread_worker = NULL;
#ifdef USE_MINI_EVENT
		ub_event_base_free(evbase);
#else
		/* cannot event_base_free, because the epoll_fd cleanup
		 * in libevent could stop the original event_base in the
		 * other process from working. */
		free(evbase);
#endif
	}
	libworker_delete_event(ctx->event_worker);

	modstack_call_deinit(&ctx->mods, ctx->env);
	modstack_call_destartup(&ctx->mods, ctx->env);
	modstack_free(&ctx->mods);
	a = ctx->alloc_list;
	while(a) {
		na = a->super;
		a->super = &ctx->superalloc;
		alloc_clear(a);
		free(a);
		a = na;
	}
	local_zones_delete(ctx->local_zones);
	lock_basic_destroy(&ctx->qqpipe_lock);
	lock_basic_destroy(&ctx->rrpipe_lock);
	lock_basic_destroy(&ctx->cfglock);
	tube_delete(ctx->qq_pipe);
	tube_delete(ctx->rr_pipe);
	if(ctx->env) {
		slabhash_delete(ctx->env->msg_cache);
		rrset_cache_delete(ctx->env->rrset_cache);
		infra_delete(ctx->env->infra_cache);
		config_delete(ctx->env->cfg);
		edns_known_options_delete(ctx->env);
		edns_strings_delete(ctx->env->edns_strings);
		forwards_delete(ctx->env->fwds);
		hints_delete(ctx->env->hints);
		auth_zones_delete(ctx->env->auth_zones);
		free(ctx->env);
	}
	ub_randfree(ctx->seed_rnd);
	alloc_clear(&ctx->superalloc);
	listen_desetup_locks();
	traverse_postorder(&ctx->queries, delq, NULL);
	if(ctx_logfile_overridden) {
		log_file(NULL);
		ctx_logfile_overridden = 0;
	}
	if(ctx->event_base_malloced)
		free(ctx->event_base);
	free(ctx);
#ifdef USE_WINSOCK
	WSACleanup();
#endif
}

int 
ub_ctx_set_option(struct ub_ctx* ctx, const char* opt, const char* val)
{
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_AFTERFINAL;
	}
	if(!config_set_option(ctx->env->cfg, opt, val)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_SYNTAX;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int
ub_ctx_get_option(struct ub_ctx* ctx, const char* opt, char** str)
{
	int r;
	lock_basic_lock(&ctx->cfglock);
	r = config_get_option_collate(ctx->env->cfg, opt, str);
	lock_basic_unlock(&ctx->cfglock);
	if(r == 0) r = UB_NOERROR;
	else if(r == 1) r = UB_SYNTAX;
	else if(r == 2) r = UB_NOMEM;
	return r;
}

int 
ub_ctx_config(struct ub_ctx* ctx, const char* fname)
{
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_AFTERFINAL;
	}
	if(!config_read(ctx->env->cfg, fname, NULL)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_SYNTAX;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_ctx_add_ta(struct ub_ctx* ctx, const char* ta)
{
	char* dup = strdup(ta);
	if(!dup) return UB_NOMEM;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		free(dup);
		return UB_AFTERFINAL;
	}
	if(!cfg_strlist_insert(&ctx->env->cfg->trust_anchor_list, dup)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_ctx_add_ta_file(struct ub_ctx* ctx, const char* fname)
{
	char* dup = strdup(fname);
	if(!dup) return UB_NOMEM;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		free(dup);
		return UB_AFTERFINAL;
	}
	if(!cfg_strlist_insert(&ctx->env->cfg->trust_anchor_file_list, dup)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int ub_ctx_add_ta_autr(struct ub_ctx* ctx, const char* fname)
{
	char* dup = strdup(fname);
	if(!dup) return UB_NOMEM;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		free(dup);
		return UB_AFTERFINAL;
	}
	if(!cfg_strlist_insert(&ctx->env->cfg->auto_trust_anchor_file_list,
		dup)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_ctx_trustedkeys(struct ub_ctx* ctx, const char* fname)
{
	char* dup = strdup(fname);
	if(!dup) return UB_NOMEM;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		free(dup);
		return UB_AFTERFINAL;
	}
	if(!cfg_strlist_insert(&ctx->env->cfg->trusted_keys_file_list, dup)) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int
ub_ctx_debuglevel(struct ub_ctx* ctx, int d)
{
	lock_basic_lock(&ctx->cfglock);
	verbosity = d;
	ctx->env->cfg->verbosity = d;
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int ub_ctx_debugout(struct ub_ctx* ctx, void* out)
{
	lock_basic_lock(&ctx->cfglock);
	log_file((FILE*)out);
	ctx_logfile_overridden = 1;
	ctx->logfile_override = 1;
	ctx->log_out = out;
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_ctx_async(struct ub_ctx* ctx, int dothread)
{
#ifdef THREADS_DISABLED
	if(dothread) /* cannot do threading */
		return UB_NOERROR;
#endif
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		return UB_AFTERFINAL;
	}
	ctx->dothread = dothread;
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_poll(struct ub_ctx* ctx)
{
	/* no need to hold lock while testing for readability. */
	return tube_poll(ctx->rr_pipe);
}

int 
ub_fd(struct ub_ctx* ctx)
{
	return tube_read_fd(ctx->rr_pipe);
}

/** process answer from bg worker */
static int
process_answer_detail(struct ub_ctx* ctx, uint8_t* msg, uint32_t len,
	ub_callback_type* cb, void** cbarg, int* err,
	struct ub_result** res)
{
	struct ctx_query* q;
	if(context_serial_getcmd(msg, len) != UB_LIBCMD_ANSWER) {
		log_err("error: bad data from bg worker %d",
			(int)context_serial_getcmd(msg, len));
		return 0;
	}

	lock_basic_lock(&ctx->cfglock);
	q = context_deserialize_answer(ctx, msg, len, err);
	if(!q) {
		lock_basic_unlock(&ctx->cfglock);
		/* probably simply the lookup that failed, i.e.
		 * response returned before cancel was sent out, so noerror */
		return 1;
	}
	log_assert(q->async);

	/* grab cb while locked */
	if(q->cancelled) {
		*cb = NULL;
		*cbarg = NULL;
	} else {
		*cb = q->cb;
		*cbarg = q->cb_arg;
	}
	if(*err) {
		*res = NULL;
		ub_resolve_free(q->res);
	} else {
		/* parse the message, extract rcode, fill result */
		sldns_buffer* buf = sldns_buffer_new(q->msg_len);
		struct regional* region = regional_create();
		*res = q->res;
		(*res)->rcode = LDNS_RCODE_SERVFAIL;
		if(region && buf) {
			sldns_buffer_clear(buf);
			sldns_buffer_write(buf, q->msg, q->msg_len);
			sldns_buffer_flip(buf);
			libworker_enter_result(*res, buf, region,
				q->msg_security);
		}
		(*res)->answer_packet = q->msg;
		(*res)->answer_len = (int)q->msg_len;
		q->msg = NULL;
		sldns_buffer_free(buf);
		regional_destroy(region);
	}
	q->res = NULL;
	/* delete the q from list */
	(void)rbtree_delete(&ctx->queries, q->node.key);
	ctx->num_async--;
	context_query_delete(q);
	lock_basic_unlock(&ctx->cfglock);

	if(*cb) return 2;
	ub_resolve_free(*res);
	return 1;
}

/** process answer from bg worker */
static int
process_answer(struct ub_ctx* ctx, uint8_t* msg, uint32_t len)
{
	int err;
	ub_callback_type cb;
	void* cbarg;
	struct ub_result* res;
	int r;

	r = process_answer_detail(ctx, msg, len, &cb, &cbarg, &err, &res);

	/* no locks held while calling callback, so that library is
	 * re-entrant. */
	if(r == 2)
		(*cb)(cbarg, err, res);

	return r;
}

int 
ub_process(struct ub_ctx* ctx)
{
	int r;
	uint8_t* msg;
	uint32_t len;
	while(1) {
		msg = NULL;
		lock_basic_lock(&ctx->rrpipe_lock);
		r = tube_read_msg(ctx->rr_pipe, &msg, &len, 1);
		lock_basic_unlock(&ctx->rrpipe_lock);
		if(r == 0)
			return UB_PIPE;
		else if(r == -1)
			break;
		if(!process_answer(ctx, msg, len)) {
			free(msg);
			return UB_PIPE;
		}
		free(msg);
	}
	return UB_NOERROR;
}

int 
ub_wait(struct ub_ctx* ctx)
{
	int err;
	ub_callback_type cb;
	void* cbarg;
	struct ub_result* res;
	int r;
	uint8_t* msg;
	uint32_t len;
	/* this is basically the same loop as _process(), but with changes.
	 * holds the rrpipe lock and waits with tube_wait */
	while(1) {
		lock_basic_lock(&ctx->rrpipe_lock);
		lock_basic_lock(&ctx->cfglock);
		if(ctx->num_async == 0) {
			lock_basic_unlock(&ctx->cfglock);
			lock_basic_unlock(&ctx->rrpipe_lock);
			break;
		}
		lock_basic_unlock(&ctx->cfglock);

		/* keep rrpipe locked, while
		 * 	o waiting for pipe readable
		 * 	o parsing message
		 * 	o possibly decrementing num_async
		 * do callback without lock
		 */
		r = tube_wait(ctx->rr_pipe);
		if(r) {
			r = tube_read_msg(ctx->rr_pipe, &msg, &len, 1);
			if(r == 0) {
				lock_basic_unlock(&ctx->rrpipe_lock);
				return UB_PIPE;
			}
			if(r == -1) {
				lock_basic_unlock(&ctx->rrpipe_lock);
				continue;
			}
			r = process_answer_detail(ctx, msg, len, 
				&cb, &cbarg, &err, &res);
			lock_basic_unlock(&ctx->rrpipe_lock);
			free(msg);
			if(r == 0)
				return UB_PIPE;
			if(r == 2)
				(*cb)(cbarg, err, res);
		} else {
			lock_basic_unlock(&ctx->rrpipe_lock);
		}
	}
	return UB_NOERROR;
}

int 
ub_resolve(struct ub_ctx* ctx, const char* name, int rrtype, 
	int rrclass, struct ub_result** result)
{
	struct ctx_query* q;
	int r;
	*result = NULL;

	lock_basic_lock(&ctx->cfglock);
	if(!ctx->finalized) {
		r = context_finalize(ctx);
		if(r) {
			lock_basic_unlock(&ctx->cfglock);
			return r;
		}
	}
	/* create new ctx_query and attempt to add to the list */
	lock_basic_unlock(&ctx->cfglock);
	q = context_new(ctx, name, rrtype, rrclass, NULL, NULL, NULL);
	if(!q)
		return UB_NOMEM;
	/* become a resolver thread for a bit */

	r = libworker_fg(ctx, q);
	if(r) {
		lock_basic_lock(&ctx->cfglock);
		(void)rbtree_delete(&ctx->queries, q->node.key);
		context_query_delete(q);
		lock_basic_unlock(&ctx->cfglock);
		return r;
	}
	q->res->answer_packet = q->msg;
	q->res->answer_len = (int)q->msg_len;
	q->msg = NULL;
	*result = q->res;
	q->res = NULL;

	lock_basic_lock(&ctx->cfglock);
	(void)rbtree_delete(&ctx->queries, q->node.key);
	context_query_delete(q);
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_resolve_event(struct ub_ctx* ctx, const char* name, int rrtype, 
	int rrclass, void* mydata, ub_event_callback_type callback,
	int* async_id)
{
	struct ctx_query* q;
	int r;

	if(async_id)
		*async_id = 0;
	lock_basic_lock(&ctx->cfglock);
	if(!ctx->finalized) {
		r = context_finalize(ctx);
		if(r) {
			lock_basic_unlock(&ctx->cfglock);
			return r;
		}
	}
	lock_basic_unlock(&ctx->cfglock);
	if(!ctx->event_worker) {
		ctx->event_worker = libworker_create_event(ctx,
			ctx->event_base);
		if(!ctx->event_worker) {
			return UB_INITFAIL;
		}
	}

	/* set time in case answer comes from cache */
	ub_comm_base_now(ctx->event_worker->base);

	/* create new ctx_query and attempt to add to the list */
	q = context_new(ctx, name, rrtype, rrclass, NULL, callback, mydata);
	if(!q)
		return UB_NOMEM;

	/* attach to mesh */
	if((r=libworker_attach_mesh(ctx, q, async_id)) != 0)
		return r;
	return UB_NOERROR;
}


int 
ub_resolve_async(struct ub_ctx* ctx, const char* name, int rrtype, 
	int rrclass, void* mydata, ub_callback_type callback, int* async_id)
{
	struct ctx_query* q;
	uint8_t* msg = NULL;
	uint32_t len = 0;

	if(async_id)
		*async_id = 0;
	lock_basic_lock(&ctx->cfglock);
	if(!ctx->finalized) {
		int r = context_finalize(ctx);
		if(r) {
			lock_basic_unlock(&ctx->cfglock);
			return r;
		}
	}
	if(!ctx->created_bg) {
		int r;
		ctx->created_bg = 1;
		lock_basic_unlock(&ctx->cfglock);
		r = libworker_bg(ctx);
		if(r) {
			lock_basic_lock(&ctx->cfglock);
			ctx->created_bg = 0;
			lock_basic_unlock(&ctx->cfglock);
			return r;
		}
	} else {
		lock_basic_unlock(&ctx->cfglock);
	}

	/* create new ctx_query and attempt to add to the list */
	q = context_new(ctx, name, rrtype, rrclass, callback, NULL, mydata);
	if(!q)
		return UB_NOMEM;

	/* write over pipe to background worker */
	lock_basic_lock(&ctx->cfglock);
	msg = context_serialize_new_query(q, &len);
	if(!msg) {
		(void)rbtree_delete(&ctx->queries, q->node.key);
		ctx->num_async--;
		context_query_delete(q);
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOMEM;
	}
	if(async_id)
		*async_id = q->querynum;
	lock_basic_unlock(&ctx->cfglock);
	
	lock_basic_lock(&ctx->qqpipe_lock);
	if(!tube_write_msg(ctx->qq_pipe, msg, len, 0)) {
		lock_basic_unlock(&ctx->qqpipe_lock);
		free(msg);
		return UB_PIPE;
	}
	lock_basic_unlock(&ctx->qqpipe_lock);
	free(msg);
	return UB_NOERROR;
}

int 
ub_cancel(struct ub_ctx* ctx, int async_id)
{
	struct ctx_query* q;
	uint8_t* msg = NULL;
	uint32_t len = 0;
	lock_basic_lock(&ctx->cfglock);
	q = (struct ctx_query*)rbtree_search(&ctx->queries, &async_id);
	if(!q || !q->async) {
		/* it is not there, so nothing to do */
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOID;
	}
	log_assert(q->async);
	q->cancelled = 1;
	
	/* delete it */
	if(!ctx->dothread) { /* if forked */
		(void)rbtree_delete(&ctx->queries, q->node.key);
		ctx->num_async--;
		msg = context_serialize_cancel(q, &len);
		context_query_delete(q);
		lock_basic_unlock(&ctx->cfglock);
		if(!msg) {
			return UB_NOMEM;
		}
		/* send cancel to background worker */
		lock_basic_lock(&ctx->qqpipe_lock);
		if(!tube_write_msg(ctx->qq_pipe, msg, len, 0)) {
			lock_basic_unlock(&ctx->qqpipe_lock);
			free(msg);
			return UB_PIPE;
		}
		lock_basic_unlock(&ctx->qqpipe_lock);
		free(msg);
	} else {
		lock_basic_unlock(&ctx->cfglock);
	}
	return UB_NOERROR;
}

void 
ub_resolve_free(struct ub_result* result)
{
	char** p;
	if(!result) return;
	free(result->qname);
	if(result->canonname != result->qname)
		free(result->canonname);
	if(result->data)
		for(p = result->data; *p; p++)
			free(*p);
	free(result->data);
	free(result->len);
	free(result->answer_packet);
	free(result->why_bogus);
	free(result);
}

const char* 
ub_strerror(int err)
{
	switch(err) {
		case UB_NOERROR: return "no error";
		case UB_SOCKET: return "socket io error";
		case UB_NOMEM: return "out of memory";
		case UB_SYNTAX: return "syntax error";
		case UB_SERVFAIL: return "server failure";
		case UB_FORKFAIL: return "could not fork";
		case UB_INITFAIL: return "initialization failure";
		case UB_AFTERFINAL: return "setting change after finalize";
		case UB_PIPE: return "error in pipe communication with async";
		case UB_READFILE: return "error reading file";
		case UB_NOID: return "error async_id does not exist";
		default: return "unknown error";
	}
}

int 
ub_ctx_set_fwd(struct ub_ctx* ctx, const char* addr)
{
	struct sockaddr_storage storage;
	socklen_t stlen;
	struct config_stub* s;
	char* dupl;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		errno=EINVAL;
		return UB_AFTERFINAL;
	}
	if(!addr) {
		/* disable fwd mode - the root stub should be first. */
		if(ctx->env->cfg->forwards &&
			(ctx->env->cfg->forwards->name &&
			strcmp(ctx->env->cfg->forwards->name, ".") == 0)) {
			s = ctx->env->cfg->forwards;
			ctx->env->cfg->forwards = s->next;
			s->next = NULL;
			config_delstubs(s);
		}
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOERROR;
	}
	lock_basic_unlock(&ctx->cfglock);

	/* check syntax for addr */
	if(!extstrtoaddr(addr, &storage, &stlen, UNBOUND_DNS_PORT)) {
		errno=EINVAL;
		return UB_SYNTAX;
	}
	
	/* it parses, add root stub in front of list */
	lock_basic_lock(&ctx->cfglock);
	if(!ctx->env->cfg->forwards ||
		(ctx->env->cfg->forwards->name &&
		strcmp(ctx->env->cfg->forwards->name, ".") != 0)) {
		s = calloc(1, sizeof(*s));
		if(!s) {
			lock_basic_unlock(&ctx->cfglock);
			errno=ENOMEM;
			return UB_NOMEM;
		}
		s->name = strdup(".");
		if(!s->name) {
			free(s);
			lock_basic_unlock(&ctx->cfglock);
			errno=ENOMEM;
			return UB_NOMEM;
		}
		s->next = ctx->env->cfg->forwards;
		ctx->env->cfg->forwards = s;
	} else {
		log_assert(ctx->env->cfg->forwards);
		log_assert(ctx->env->cfg->forwards->name);
		s = ctx->env->cfg->forwards;
	}
	dupl = strdup(addr);
	if(!dupl) {
		lock_basic_unlock(&ctx->cfglock);
		errno=ENOMEM;
		return UB_NOMEM;
	}
	if(!cfg_strlist_insert(&s->addrs, dupl)) {
		lock_basic_unlock(&ctx->cfglock);
		errno=ENOMEM;
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int ub_ctx_set_tls(struct ub_ctx* ctx, int tls)
{
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		errno=EINVAL;
		return UB_AFTERFINAL;
	}
	ctx->env->cfg->ssl_upstream = tls;
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int ub_ctx_set_stub(struct ub_ctx* ctx, const char* zone, const char* addr,
	int isprime)
{
	char* a;
	struct config_stub **prev, *elem;

	/* check syntax for zone name */
	if(zone) {
		uint8_t* nm;
		int nmlabs;
		size_t nmlen;
		if(!parse_dname(zone, &nm, &nmlen, &nmlabs)) {
			errno=EINVAL;
			return UB_SYNTAX;
		}
		free(nm);
	} else {
		zone = ".";
	}

	/* check syntax for addr (if not NULL) */
	if(addr) {
		struct sockaddr_storage storage;
		socklen_t stlen;
		if(!extstrtoaddr(addr, &storage, &stlen, UNBOUND_DNS_PORT)) {
			errno=EINVAL;
			return UB_SYNTAX;
		}
	}

	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		errno=EINVAL;
		return UB_AFTERFINAL;
	}

	/* arguments all right, now find or add the stub */
	prev = &ctx->env->cfg->stubs;
	elem = cfg_stub_find(&prev, zone);
	if(!elem && !addr) {
		/* not found and we want to delete, nothing to do */
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOERROR;
	} else if(elem && !addr) {
		/* found, and we want to delete */
		*prev = elem->next;
		config_delstub(elem);
		lock_basic_unlock(&ctx->cfglock);
		return UB_NOERROR;
	} else if(!elem) {
		/* not found, create the stub entry */
		elem=(struct config_stub*)calloc(1, sizeof(struct config_stub));
		if(elem) elem->name = strdup(zone);
		if(!elem || !elem->name) {
			free(elem);
			lock_basic_unlock(&ctx->cfglock);
			errno = ENOMEM;
			return UB_NOMEM;
		}
		elem->next = ctx->env->cfg->stubs;
		ctx->env->cfg->stubs = elem;
	}

	/* add the address to the list and set settings */
	elem->isprime = isprime;
	a = strdup(addr);
	if(!a) {
		lock_basic_unlock(&ctx->cfglock);
		errno = ENOMEM;
		return UB_NOMEM;
	}
	if(!cfg_strlist_insert(&elem->addrs, a)) {
		lock_basic_unlock(&ctx->cfglock);
		errno = ENOMEM;
		return UB_NOMEM;
	}
	lock_basic_unlock(&ctx->cfglock);
	return UB_NOERROR;
}

int 
ub_ctx_resolvconf(struct ub_ctx* ctx, const char* fname)
{
	FILE* in;
	int numserv = 0;
	char buf[1024];
	char* parse, *addr;
	int r;

	if(fname == NULL) {
#if !defined(UB_ON_WINDOWS) || !defined(HAVE_WINDOWS_H)
		fname = "/etc/resolv.conf";
#else
		FIXED_INFO *info;
		ULONG buflen = sizeof(*info);
		IP_ADDR_STRING *ptr;

		info = (FIXED_INFO *) malloc(sizeof (FIXED_INFO));
		if (info == NULL) 
			return UB_READFILE;

		if (GetNetworkParams(info, &buflen) == ERROR_BUFFER_OVERFLOW) {
			free(info);
			info = (FIXED_INFO *) malloc(buflen);
			if (info == NULL)
				return UB_READFILE;
		}

		if (GetNetworkParams(info, &buflen) == NO_ERROR) {
			int retval=0;
			ptr = &(info->DnsServerList);
			while (ptr) {
				numserv++;
				if((retval=ub_ctx_set_fwd(ctx, 
					ptr->IpAddress.String))!=0) {
					free(info);
					return retval;
				}
				ptr = ptr->Next;
			}
			free(info);
			if (numserv==0)
				return UB_READFILE;
			return UB_NOERROR;
		}
		free(info);
		return UB_READFILE;
#endif /* WINDOWS */
	}
	in = fopen(fname, "r");
	if(!in) {
		/* error in errno! perror(fname) */
		return UB_READFILE;
	}
	while(fgets(buf, (int)sizeof(buf), in)) {
		buf[sizeof(buf)-1] = 0;
		parse=buf;
		while(*parse == ' ' || *parse == '\t')
			parse++;
		if(strncmp(parse, "nameserver", 10) == 0) {
			numserv++;
			parse += 10; /* skip 'nameserver' */
			/* skip whitespace */
			while(*parse == ' ' || *parse == '\t')
				parse++;
			addr = parse;
			/* skip [0-9a-fA-F.:]*, i.e. IP4 and IP6 address */
			while(isxdigit((unsigned char)*parse) || *parse=='.' || *parse==':')
				parse++;
			/* terminate after the address, remove newline */
			*parse = 0;
			
			if((r = ub_ctx_set_fwd(ctx, addr)) != UB_NOERROR) {
				fclose(in);
				return r;
			}
		}
	}
	fclose(in);
	if(numserv == 0) {
		/* from resolv.conf(5) if none given, use localhost */
		return ub_ctx_set_fwd(ctx, "127.0.0.1");
	}
	return UB_NOERROR;
}

int
ub_ctx_hosts(struct ub_ctx* ctx, const char* fname)
{
	FILE* in;
	char buf[1024], ldata[2048];
	char* parse, *addr, *name, *ins;
	lock_basic_lock(&ctx->cfglock);
	if(ctx->finalized) {
		lock_basic_unlock(&ctx->cfglock);
		errno=EINVAL;
		return UB_AFTERFINAL;
	}
	lock_basic_unlock(&ctx->cfglock);
	if(fname == NULL) {
#if defined(UB_ON_WINDOWS) && defined(HAVE_WINDOWS_H)
		/*
		 * If this is Windows NT/XP/2K it's in
		 * %WINDIR%\system32\drivers\etc\hosts.
		 * If this is Windows 95/98/Me it's in %WINDIR%\hosts.
		 */
		name = getenv("WINDIR");
		if (name != NULL) {
			int retval=0;
			snprintf(buf, sizeof(buf), "%s%s", name, 
				"\\system32\\drivers\\etc\\hosts");
			if((retval=ub_ctx_hosts(ctx, buf)) !=0 ) {
				snprintf(buf, sizeof(buf), "%s%s", name, 
					"\\hosts");
				retval=ub_ctx_hosts(ctx, buf);
			}
			return retval;
		}
		return UB_READFILE;
#else
		fname = "/etc/hosts";
#endif /* WIN32 */
	}
	in = fopen(fname, "r");
	if(!in) {
		/* error in errno! perror(fname) */
		return UB_READFILE;
	}
	while(fgets(buf, (int)sizeof(buf), in)) {
		buf[sizeof(buf)-1] = 0;
		parse=buf;
		while(*parse == ' ' || *parse == '\t')
			parse++;
		if(*parse == '#')
			continue; /* skip comment */
		/* format: <addr> spaces <name> spaces <name> ... */
		addr = parse;
		/* skip addr */
		while(isxdigit((unsigned char)*parse) || *parse == '.' || *parse == ':')
			parse++;
		if(*parse == '\r')
			parse++;
		if(*parse == '\n' || *parse == 0)
			continue;
		if(*parse == '%') 
			continue; /* ignore macOSX fe80::1%lo0 localhost */
		if(*parse != ' ' && *parse != '\t') {
			/* must have whitespace after address */
			fclose(in);
			errno=EINVAL;
			return UB_SYNTAX;
		}
		*parse++ = 0; /* end delimiter for addr ... */
		/* go to names and add them */
		while(*parse) {
			while(*parse == ' ' || *parse == '\t' || *parse=='\n'
				|| *parse=='\r')
				parse++;
			if(*parse == 0 || *parse == '#')
				break;
			/* skip name, allows (too) many printable characters */
			name = parse;
			while('!' <= *parse && *parse <= '~')
				parse++;
			if(*parse)
				*parse++ = 0; /* end delimiter for name */
			snprintf(ldata, sizeof(ldata), "%s %s %s",
				name, str_is_ip6(addr)?"AAAA":"A", addr);
			ins = strdup(ldata);
			if(!ins) {
				/* out of memory */
				fclose(in);
				errno=ENOMEM;
				return UB_NOMEM;
			}
			lock_basic_lock(&ctx->cfglock);
			if(!cfg_strlist_insert(&ctx->env->cfg->local_data, 
				ins)) {
				lock_basic_unlock(&ctx->cfglock);
				fclose(in);
				errno=ENOMEM;
				return UB_NOMEM;
			}
			lock_basic_unlock(&ctx->cfglock);
		}
	}
	fclose(in);
	return UB_NOERROR;
}

/** finalize the context, if not already finalized */
static int ub_ctx_finalize(struct ub_ctx* ctx)
{
	int res = 0;
	lock_basic_lock(&ctx->cfglock);
	if (!ctx->finalized) {
		res = context_finalize(ctx);
	}
	lock_basic_unlock(&ctx->cfglock);
	return res;
}

/* Print local zones and RR data */
int ub_ctx_print_local_zones(struct ub_ctx* ctx)
{   
	int res = ub_ctx_finalize(ctx);
	if (res) return res;

	local_zones_print(ctx->local_zones);

	return UB_NOERROR;
}

/* Add a new zone */
int ub_ctx_zone_add(struct ub_ctx* ctx, const char *zone_name, 
	const char *zone_type)
{
	enum localzone_type t;
	struct local_zone* z;
	uint8_t* nm;
	int nmlabs;
	size_t nmlen;

	int res = ub_ctx_finalize(ctx);
	if (res) return res;

	if(!local_zone_str2type(zone_type, &t)) {
		return UB_SYNTAX;
	}

	if(!parse_dname(zone_name, &nm, &nmlen, &nmlabs)) {
		return UB_SYNTAX;
	}

	lock_rw_wrlock(&ctx->local_zones->lock);
	if((z=local_zones_find(ctx->local_zones, nm, nmlen, nmlabs, 
		LDNS_RR_CLASS_IN))) {
		/* already present in tree */
		lock_rw_wrlock(&z->lock);
		z->type = t; /* update type anyway */
		lock_rw_unlock(&z->lock);
		lock_rw_unlock(&ctx->local_zones->lock);
		free(nm);
		return UB_NOERROR;
	}
	if(!local_zones_add_zone(ctx->local_zones, nm, nmlen, nmlabs, 
		LDNS_RR_CLASS_IN, t)) {
		lock_rw_unlock(&ctx->local_zones->lock);
		return UB_NOMEM;
	}
	lock_rw_unlock(&ctx->local_zones->lock);
	return UB_NOERROR;
}

/* Remove zone */
int ub_ctx_zone_remove(struct ub_ctx* ctx, const char *zone_name)
{   
	struct local_zone* z;
	uint8_t* nm;
	int nmlabs;
	size_t nmlen;

	int res = ub_ctx_finalize(ctx);
	if (res) return res;

	if(!parse_dname(zone_name, &nm, &nmlen, &nmlabs)) {
		return UB_SYNTAX;
	}

	lock_rw_wrlock(&ctx->local_zones->lock);
	if((z=local_zones_find(ctx->local_zones, nm, nmlen, nmlabs, 
		LDNS_RR_CLASS_IN))) {
		/* present in tree */
		local_zones_del_zone(ctx->local_zones, z);
	}
	lock_rw_unlock(&ctx->local_zones->lock);
	free(nm);
	return UB_NOERROR;
}

/* Add new RR data */
int ub_ctx_data_add(struct ub_ctx* ctx, const char *data)
{
	int res = ub_ctx_finalize(ctx);
	if (res) return res;

	res = local_zones_add_RR(ctx->local_zones, data);
	return (!res) ? UB_NOMEM : UB_NOERROR;
}

/* Remove RR data */
int ub_ctx_data_remove(struct ub_ctx* ctx, const char *data)
{
	uint8_t* nm;
	int nmlabs;
	size_t nmlen;
	int res = ub_ctx_finalize(ctx);
	if (res) return res;

	if(!parse_dname(data, &nm, &nmlen, &nmlabs)) 
		return UB_SYNTAX;

	local_zones_del_data(ctx->local_zones, nm, nmlen, nmlabs, 
		LDNS_RR_CLASS_IN);

	free(nm);
	return UB_NOERROR;
}

const char* ub_version(void)
{
	return PACKAGE_VERSION;
}

int 
ub_ctx_set_event(struct ub_ctx* ctx, struct event_base* base) {
	struct ub_event_base* new_base;

	if (!ctx || !ctx->event_base || !base) {
		return UB_INITFAIL;
	}
	if (ub_libevent_get_event_base(ctx->event_base) == base) {
		/* already set */
		return UB_NOERROR;
	}
	
	lock_basic_lock(&ctx->cfglock);
	/* destroy the current worker - safe to pass in NULL */
	libworker_delete_event(ctx->event_worker);
	ctx->event_worker = NULL;
	new_base = ub_libevent_event_base(base);
	if (new_base)
		ctx->event_base = new_base;	
	ctx->created_bg = 0;
	ctx->dothread = 1;
	lock_basic_unlock(&ctx->cfglock);
	return new_base ? UB_NOERROR : UB_INITFAIL;
}
