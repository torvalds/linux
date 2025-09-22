/*
 * services/mesh.c - deal with mesh of query states and handle events for that.
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
 * This file contains functions to assist in dealing with a mesh of
 * query states. This mesh is supposed to be thread-specific.
 * It consists of query states (per qname, qtype, qclass) and connections
 * between query states and the super and subquery states, and replies to
 * send back to clients.
 */
#include "config.h"
#include "services/mesh.h"
#include "services/outbound_list.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/data/msgencode.h"
#include "util/timehist.h"
#include "util/fptr_wlist.h"
#include "util/alloc.h"
#include "util/config_file.h"
#include "util/edns.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "services/localzone.h"
#include "util/data/dname.h"
#include "respip/respip.h"
#include "services/listen_dnsport.h"
#include "util/timeval_func.h"

#ifdef CLIENT_SUBNET
#include "edns-subnet/subnetmod.h"
#include "edns-subnet/edns-subnet.h"
#endif
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

/** Compare two views by name */
static int
view_name_compare(const char* v_a, const char* v_b)
{
	if(v_a == NULL && v_b == NULL)
		return 0;
	/* The NULL name is smaller than if the name is set. */
	if(v_a == NULL)
		return -1;
	if(v_b == NULL)
		return 1;
	return strcmp(v_a, v_b);
}

/**
 * Compare two response-ip client info entries for the purpose of mesh state
 * compare.  It returns 0 if ci_a and ci_b are considered equal; otherwise
 * 1 or -1 (they mean 'ci_a is larger/smaller than ci_b', respectively, but
 * in practice it should be only used to mean they are different).
 * We cannot share the mesh state for two queries if different response-ip
 * actions can apply in the end, even if those queries are otherwise identical.
 * For this purpose we compare tag lists and tag action lists; they should be
 * identical to share the same state.
 * For tag data, we don't look into the data content, as it can be
 * expensive; unless tag data are not defined for both or they point to the
 * exact same data in memory (i.e., they come from the same ACL entry), we
 * consider these data different.
 * Likewise, if the client info is associated with views, we don't look into
 * the views.  They are considered different unless they are exactly the same
 * even if the views only differ in the names.
 */
static int
client_info_compare(const struct respip_client_info* ci_a,
	const struct respip_client_info* ci_b)
{
	int cmp;

	if(!ci_a && !ci_b)
		return 0;
	if(ci_a && !ci_b)
		return -1;
	if(!ci_a && ci_b)
		return 1;
	if(ci_a->taglen != ci_b->taglen)
		return (ci_a->taglen < ci_b->taglen) ? -1 : 1;
	if(ci_a->taglist && !ci_b->taglist)
		return -1;
	if(!ci_a->taglist && ci_b->taglist)
		return 1;
	if(ci_a->taglist && ci_b->taglist) {
		cmp = memcmp(ci_a->taglist, ci_b->taglist, ci_a->taglen);
		if(cmp != 0)
			return cmp;
	}
	if(ci_a->tag_actions_size != ci_b->tag_actions_size)
		return (ci_a->tag_actions_size < ci_b->tag_actions_size) ?
			-1 : 1;
	if(ci_a->tag_actions && !ci_b->tag_actions)
		return -1;
	if(!ci_a->tag_actions && ci_b->tag_actions)
		return 1;
	if(ci_a->tag_actions && ci_b->tag_actions) {
		cmp = memcmp(ci_a->tag_actions, ci_b->tag_actions,
			ci_a->tag_actions_size);
		if(cmp != 0)
			return cmp;
	}
	if(ci_a->tag_datas != ci_b->tag_datas)
		return ci_a->tag_datas < ci_b->tag_datas ? -1 : 1;
	if(ci_a->view || ci_a->view_name || ci_b->view || ci_b->view_name) {
		/* Compare the views by name. */
		cmp = view_name_compare(
			(ci_a->view?ci_a->view->name:ci_a->view_name),
			(ci_b->view?ci_b->view->name:ci_b->view_name));
		if(cmp != 0)
			return cmp;
	}
	return 0;
}

int
mesh_state_compare(const void* ap, const void* bp)
{
	struct mesh_state* a = (struct mesh_state*)ap;
	struct mesh_state* b = (struct mesh_state*)bp;
	int cmp;

	if(a->unique < b->unique)
		return -1;
	if(a->unique > b->unique)
		return 1;

	if(a->s.is_priming && !b->s.is_priming)
		return -1;
	if(!a->s.is_priming && b->s.is_priming)
		return 1;

	if(a->s.is_valrec && !b->s.is_valrec)
		return -1;
	if(!a->s.is_valrec && b->s.is_valrec)
		return 1;

	if((a->s.query_flags&BIT_RD) && !(b->s.query_flags&BIT_RD))
		return -1;
	if(!(a->s.query_flags&BIT_RD) && (b->s.query_flags&BIT_RD))
		return 1;

	if((a->s.query_flags&BIT_CD) && !(b->s.query_flags&BIT_CD))
		return -1;
	if(!(a->s.query_flags&BIT_CD) && (b->s.query_flags&BIT_CD))
		return 1;

	cmp = query_info_compare(&a->s.qinfo, &b->s.qinfo);
	if(cmp != 0)
		return cmp;
	return client_info_compare(a->s.client_info, b->s.client_info);
}

int
mesh_state_ref_compare(const void* ap, const void* bp)
{
	struct mesh_state_ref* a = (struct mesh_state_ref*)ap;
	struct mesh_state_ref* b = (struct mesh_state_ref*)bp;
	return mesh_state_compare(a->s, b->s);
}

struct mesh_area*
mesh_create(struct module_stack* stack, struct module_env* env)
{
	struct mesh_area* mesh = calloc(1, sizeof(struct mesh_area));
	if(!mesh) {
		log_err("mesh area alloc: out of memory");
		return NULL;
	}
	mesh->histogram = timehist_setup();
	mesh->qbuf_bak = sldns_buffer_new(env->cfg->msg_buffer_size);
	if(!mesh->histogram || !mesh->qbuf_bak) {
		free(mesh);
		log_err("mesh area alloc: out of memory");
		return NULL;
	}
	mesh->mods = *stack;
	mesh->env = env;
	rbtree_init(&mesh->run, &mesh_state_compare);
	rbtree_init(&mesh->all, &mesh_state_compare);
	mesh->num_reply_addrs = 0;
	mesh->num_reply_states = 0;
	mesh->num_detached_states = 0;
	mesh->num_forever_states = 0;
	mesh->stats_jostled = 0;
	mesh->stats_dropped = 0;
	mesh->ans_expired = 0;
	mesh->ans_cachedb = 0;
	mesh->num_queries_discard_timeout = 0;
	mesh->num_queries_wait_limit = 0;
	mesh->num_dns_error_reports = 0;
	mesh->max_reply_states = env->cfg->num_queries_per_thread;
	mesh->max_forever_states = (mesh->max_reply_states+1)/2;
#ifndef S_SPLINT_S
	mesh->jostle_max.tv_sec = (time_t)(env->cfg->jostle_time / 1000);
	mesh->jostle_max.tv_usec = (time_t)((env->cfg->jostle_time % 1000)
		*1000);
#endif
	return mesh;
}

/** help mesh delete delete mesh states */
static void
mesh_delete_helper(rbnode_type* n)
{
	struct mesh_state* mstate = (struct mesh_state*)n->key;
	/* perform a full delete, not only 'cleanup' routine,
	 * because other callbacks expect a clean state in the mesh.
	 * For 're-entrant' calls */
	mesh_state_delete(&mstate->s);
	/* but because these delete the items from the tree, postorder
	 * traversal and rbtree rebalancing do not work together */
}

void
mesh_delete(struct mesh_area* mesh)
{
	if(!mesh)
		return;
	/* free all query states */
	while(mesh->all.count)
		mesh_delete_helper(mesh->all.root);
	timehist_delete(mesh->histogram);
	sldns_buffer_free(mesh->qbuf_bak);
	free(mesh);
}

void
mesh_delete_all(struct mesh_area* mesh)
{
	/* free all query states */
	while(mesh->all.count)
		mesh_delete_helper(mesh->all.root);
	mesh->stats_dropped += mesh->num_reply_addrs;
	/* clear mesh area references */
	rbtree_init(&mesh->run, &mesh_state_compare);
	rbtree_init(&mesh->all, &mesh_state_compare);
	mesh->num_reply_addrs = 0;
	mesh->num_reply_states = 0;
	mesh->num_detached_states = 0;
	mesh->num_forever_states = 0;
	mesh->forever_first = NULL;
	mesh->forever_last = NULL;
	mesh->jostle_first = NULL;
	mesh->jostle_last = NULL;
}

int mesh_make_new_space(struct mesh_area* mesh, sldns_buffer* qbuf)
{
	struct mesh_state* m = mesh->jostle_first;
	/* free space is available */
	if(mesh->num_reply_states < mesh->max_reply_states)
		return 1;
	/* try to kick out a jostle-list item */
	if(m && m->reply_list && m->list_select == mesh_jostle_list) {
		/* how old is it? */
		struct timeval age;
		timeval_subtract(&age, mesh->env->now_tv,
			&m->reply_list->start_time);
		if(timeval_smaller(&mesh->jostle_max, &age)) {
			/* its a goner */
			log_nametypeclass(VERB_ALGO, "query jostled out to "
				"make space for a new one",
				m->s.qinfo.qname, m->s.qinfo.qtype,
				m->s.qinfo.qclass);
			/* backup the query */
			if(qbuf) sldns_buffer_copy(mesh->qbuf_bak, qbuf);
			/* notify supers */
			if(m->super_set.count > 0) {
				verbose(VERB_ALGO, "notify supers of failure");
				m->s.return_msg = NULL;
				m->s.return_rcode = LDNS_RCODE_SERVFAIL;
				mesh_walk_supers(mesh, m);
			}
			mesh->stats_jostled ++;
			mesh_state_delete(&m->s);
			/* restore the query - note that the qinfo ptr to
			 * the querybuffer is then correct again. */
			if(qbuf) sldns_buffer_copy(qbuf, mesh->qbuf_bak);
			return 1;
		}
	}
	/* no space for new item */
	return 0;
}

struct dns_msg*
mesh_serve_expired_lookup(struct module_qstate* qstate,
	struct query_info* lookup_qinfo, int* is_expired)
{
	hashvalue_type h;
	struct lruhash_entry* e;
	struct dns_msg* msg;
	struct reply_info* data;
	struct msgreply_entry* key;
	time_t timenow = *qstate->env->now;
	int must_validate = (!(qstate->query_flags&BIT_CD)
		|| qstate->env->cfg->ignore_cd) && qstate->env->need_to_validate;
	*is_expired = 0;
	/* Lookup cache */
	h = query_info_hash(lookup_qinfo, qstate->query_flags);
	e = slabhash_lookup(qstate->env->msg_cache, h, lookup_qinfo, 0);
	if(!e) return NULL;

	key = (struct msgreply_entry*)e->key;
	data = (struct reply_info*)e->data;
	if(data->ttl < timenow) *is_expired = 1;
	msg = tomsg(qstate->env, &key->key, data, qstate->region, timenow,
		qstate->env->cfg->serve_expired, qstate->env->scratch);
	if(!msg)
		goto bail_out;

	/* Check CNAME chain (if any)
	 * This is part of tomsg above; no need to check now. */

	/* Check security status of the cached answer.
	 * tomsg above has a subset of these checks, so we are leaving
	 * these as is.
	 * In case of bogus or revalidation we don't care to reply here. */
	if(must_validate && (msg->rep->security == sec_status_bogus ||
		msg->rep->security == sec_status_secure_sentinel_fail)) {
		verbose(VERB_ALGO, "Serve expired: bogus answer found in cache");
		goto bail_out;
	} else if(msg->rep->security == sec_status_unchecked && must_validate) {
		verbose(VERB_ALGO, "Serve expired: unchecked entry needs "
			"validation");
		goto bail_out; /* need to validate cache entry first */
	} else if(msg->rep->security == sec_status_secure &&
		!reply_all_rrsets_secure(msg->rep) && must_validate) {
			verbose(VERB_ALGO, "Serve expired: secure entry"
				" changed status");
			goto bail_out; /* rrset changed, re-verify */
	}

	lock_rw_unlock(&e->lock);
	return msg;

bail_out:
	lock_rw_unlock(&e->lock);
	return NULL;
}


/** Init the serve expired data structure */
static int
mesh_serve_expired_init(struct mesh_state* mstate, int timeout)
{
	struct timeval t;

	/* Create serve_expired_data if not there yet */
	if(!mstate->s.serve_expired_data) {
		mstate->s.serve_expired_data = (struct serve_expired_data*)
			regional_alloc_zero(
				mstate->s.region, sizeof(struct serve_expired_data));
		if(!mstate->s.serve_expired_data)
			return 0;
	}

	/* Don't overwrite the function if already set */
	mstate->s.serve_expired_data->get_cached_answer =
		mstate->s.serve_expired_data->get_cached_answer?
		mstate->s.serve_expired_data->get_cached_answer:
		&mesh_serve_expired_lookup;

	/* In case this timer already popped, start it again */
	if(!mstate->s.serve_expired_data->timer && timeout != -1) {
		mstate->s.serve_expired_data->timer = comm_timer_create(
			mstate->s.env->worker_base, mesh_serve_expired_callback, mstate);
		if(!mstate->s.serve_expired_data->timer)
			return 0;
#ifndef S_SPLINT_S
		t.tv_sec = timeout/1000;
		t.tv_usec = (timeout%1000)*1000;
#endif
		comm_timer_set(mstate->s.serve_expired_data->timer, &t);
	}
	return 1;
}

void mesh_new_client(struct mesh_area* mesh, struct query_info* qinfo,
	struct respip_client_info* cinfo, uint16_t qflags,
	struct edns_data* edns, struct comm_reply* rep, uint16_t qid,
	int rpz_passthru)
{
	struct mesh_state* s = NULL;
	int unique = unique_mesh_state(edns->opt_list_in, mesh->env);
	int was_detached = 0;
	int was_noreply = 0;
	int added = 0;
	int timeout = mesh->env->cfg->serve_expired?
		mesh->env->cfg->serve_expired_client_timeout:0;
	struct sldns_buffer* r_buffer = rep->c->buffer;
	uint16_t mesh_flags = qflags&(BIT_RD|BIT_CD);
	if(rep->c->tcp_req_info) {
		r_buffer = rep->c->tcp_req_info->spool_buffer;
	}
	if(!infra_wait_limit_allowed(mesh->env->infra_cache, rep,
		edns->cookie_valid, mesh->env->cfg)) {
		verbose(VERB_ALGO, "Too many queries waiting from the IP. "
			"dropping incoming query.");
		comm_point_drop_reply(rep);
		mesh->num_queries_wait_limit++;
		return;
	}
	if(!unique)
		s = mesh_area_find(mesh, cinfo, qinfo, mesh_flags, 0, 0);
	/* does this create a new reply state? */
	if(!s || s->list_select == mesh_no_list) {
		if(!mesh_make_new_space(mesh, rep->c->buffer)) {
			verbose(VERB_ALGO, "Too many queries. dropping "
				"incoming query.");
			comm_point_drop_reply(rep);
			mesh->stats_dropped++;
			return;
		}
		/* for this new reply state, the reply address is free,
		 * so the limit of reply addresses does not stop reply states*/
	} else {
		/* protect our memory usage from storing reply addresses */
		if(mesh->num_reply_addrs > mesh->max_reply_states*16) {
			verbose(VERB_ALGO, "Too many requests queued. "
				"dropping incoming query.");
			comm_point_drop_reply(rep);
			mesh->stats_dropped++;
			return;
		}
	}
	/* see if it already exists, if not, create one */
	if(!s) {
#ifdef UNBOUND_DEBUG
		struct rbnode_type* n;
#endif
		s = mesh_state_create(mesh->env, qinfo, cinfo,
			mesh_flags, 0, 0);
		if(!s) {
			log_err("mesh_state_create: out of memory; SERVFAIL");
			if(!inplace_cb_reply_servfail_call(mesh->env, qinfo, NULL, NULL,
				LDNS_RCODE_SERVFAIL, edns, rep, mesh->env->scratch, mesh->env->now_tv))
					edns->opt_list_inplace_cb_out = NULL;
			error_encode(r_buffer, LDNS_RCODE_SERVFAIL,
				qinfo, qid, qflags, edns);
			comm_point_send_reply(rep);
			return;
		}
		/* set detached (it is now) */
		mesh->num_detached_states++;
		if(unique)
			mesh_state_make_unique(s);
		s->s.rpz_passthru = rpz_passthru;
		/* copy the edns options we got from the front */
		if(edns->opt_list_in) {
			s->s.edns_opts_front_in = edns_opt_copy_region(edns->opt_list_in,
				s->s.region);
			if(!s->s.edns_opts_front_in) {
				log_err("edns_opt_copy_region: out of memory; SERVFAIL");
				if(!inplace_cb_reply_servfail_call(mesh->env, qinfo, NULL,
					NULL, LDNS_RCODE_SERVFAIL, edns, rep, mesh->env->scratch, mesh->env->now_tv))
						edns->opt_list_inplace_cb_out = NULL;
				error_encode(r_buffer, LDNS_RCODE_SERVFAIL,
					qinfo, qid, qflags, edns);
				comm_point_send_reply(rep);
				mesh_state_delete(&s->s);
				return;
			}
		}

#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->all, &s->node);
		log_assert(n != NULL);
		added = 1;
	}
	if(!s->reply_list && !s->cb_list) {
		was_noreply = 1;
		if(s->super_set.count == 0) {
			was_detached = 1;
		}
	}
	/* add reply to s */
	if(!mesh_state_add_reply(s, edns, rep, qid, qflags, qinfo)) {
		log_err("mesh_new_client: out of memory; SERVFAIL");
		goto servfail_mem;
	}
	if(rep->c->tcp_req_info) {
		if(!tcp_req_info_add_meshstate(rep->c->tcp_req_info, mesh, s)) {
			log_err("mesh_new_client: out of memory add tcpreqinfo");
			goto servfail_mem;
		}
	}
	if(rep->c->use_h2) {
		http2_stream_add_meshstate(rep->c->h2_stream, mesh, s);
	}
	/* add serve expired timer if required and not already there */
	if(timeout && !mesh_serve_expired_init(s, timeout)) {
		log_err("mesh_new_client: out of memory initializing serve expired");
		goto servfail_mem;
	}
#ifdef USE_CACHEDB
	if(!timeout && mesh->env->cfg->serve_expired &&
		!mesh->env->cfg->serve_expired_client_timeout &&
		(mesh->env->cachedb_enabled &&
		 mesh->env->cfg->cachedb_check_when_serve_expired)) {
		if(!mesh_serve_expired_init(s, -1)) {
			log_err("mesh_new_client: out of memory initializing serve expired");
			goto servfail_mem;
		}
	}
#endif
	infra_wait_limit_inc(mesh->env->infra_cache, rep, *mesh->env->now,
		mesh->env->cfg);
	/* update statistics */
	if(was_detached) {
		log_assert(mesh->num_detached_states > 0);
		mesh->num_detached_states--;
	}
	if(was_noreply) {
		mesh->num_reply_states ++;
	}
	mesh->num_reply_addrs++;
	if(s->list_select == mesh_no_list) {
		/* move to either the forever or the jostle_list */
		if(mesh->num_forever_states < mesh->max_forever_states) {
			mesh->num_forever_states ++;
			mesh_list_insert(s, &mesh->forever_first,
				&mesh->forever_last);
			s->list_select = mesh_forever_list;
		} else {
			mesh_list_insert(s, &mesh->jostle_first,
				&mesh->jostle_last);
			s->list_select = mesh_jostle_list;
		}
	}
	if(added)
		mesh_run(mesh, s, module_event_new, NULL);
	return;

servfail_mem:
	if(!inplace_cb_reply_servfail_call(mesh->env, qinfo, &s->s,
		NULL, LDNS_RCODE_SERVFAIL, edns, rep, mesh->env->scratch, mesh->env->now_tv))
			edns->opt_list_inplace_cb_out = NULL;
	error_encode(r_buffer, LDNS_RCODE_SERVFAIL,
		qinfo, qid, qflags, edns);
	if(rep->c->use_h2)
		http2_stream_remove_mesh_state(rep->c->h2_stream);
	comm_point_send_reply(rep);
	if(added)
		mesh_state_delete(&s->s);
	return;
}

int
mesh_new_callback(struct mesh_area* mesh, struct query_info* qinfo,
	uint16_t qflags, struct edns_data* edns, sldns_buffer* buf,
	uint16_t qid, mesh_cb_func_type cb, void* cb_arg, int rpz_passthru)
{
	struct mesh_state* s = NULL;
	int unique = unique_mesh_state(edns->opt_list_in, mesh->env);
	int timeout = mesh->env->cfg->serve_expired?
		mesh->env->cfg->serve_expired_client_timeout:0;
	int was_detached = 0;
	int was_noreply = 0;
	int added = 0;
	uint16_t mesh_flags = qflags&(BIT_RD|BIT_CD);
	if(!unique)
		s = mesh_area_find(mesh, NULL, qinfo, mesh_flags, 0, 0);

	/* there are no limits on the number of callbacks */

	/* see if it already exists, if not, create one */
	if(!s) {
#ifdef UNBOUND_DEBUG
		struct rbnode_type* n;
#endif
		s = mesh_state_create(mesh->env, qinfo, NULL,
			mesh_flags, 0, 0);
		if(!s) {
			return 0;
		}
		/* set detached (it is now) */
		mesh->num_detached_states++;
		if(unique)
			mesh_state_make_unique(s);
		s->s.rpz_passthru = rpz_passthru;
		if(edns->opt_list_in) {
			s->s.edns_opts_front_in = edns_opt_copy_region(edns->opt_list_in,
				s->s.region);
			if(!s->s.edns_opts_front_in) {
				mesh_state_delete(&s->s);
				return 0;
			}
		}
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->all, &s->node);
		log_assert(n != NULL);
		added = 1;
	}
	if(!s->reply_list && !s->cb_list) {
		was_noreply = 1;
		if(s->super_set.count == 0) {
			was_detached = 1;
		}
	}
	/* add reply to s */
	if(!mesh_state_add_cb(s, edns, buf, cb, cb_arg, qid, qflags)) {
		if(added)
			mesh_state_delete(&s->s);
		return 0;
	}
	/* add serve expired timer if not already there */
	if(timeout && !mesh_serve_expired_init(s, timeout)) {
		if(added)
			mesh_state_delete(&s->s);
		return 0;
	}
#ifdef USE_CACHEDB
	if(!timeout && mesh->env->cfg->serve_expired &&
		!mesh->env->cfg->serve_expired_client_timeout &&
		(mesh->env->cachedb_enabled &&
		 mesh->env->cfg->cachedb_check_when_serve_expired)) {
		if(!mesh_serve_expired_init(s, -1)) {
			if(added)
				mesh_state_delete(&s->s);
			return 0;
		}
	}
#endif
	/* update statistics */
	if(was_detached) {
		log_assert(mesh->num_detached_states > 0);
		mesh->num_detached_states--;
	}
	if(was_noreply) {
		mesh->num_reply_states ++;
	}
	mesh->num_reply_addrs++;
	if(added)
		mesh_run(mesh, s, module_event_new, NULL);
	return 1;
}

/* Internal backend routine of mesh_new_prefetch().  It takes one additional
 * parameter, 'run', which controls whether to run the prefetch state
 * immediately.  When this function is called internally 'run' could be
 * 0 (false), in which case the new state is only made runnable so it
 * will not be run recursively on top of the current state. */
static void mesh_schedule_prefetch(struct mesh_area* mesh,
	struct query_info* qinfo, uint16_t qflags, time_t leeway, int run,
	int rpz_passthru)
{
	/* Explicitly set the BIT_RD regardless of the client's flags. This is
	 * for a prefetch query (no client attached) but it needs to be treated
	 * as a recursion query. */
	uint16_t mesh_flags = BIT_RD|(qflags&BIT_CD);
	struct mesh_state* s = mesh_area_find(mesh, NULL, qinfo,
		mesh_flags, 0, 0);
#ifdef UNBOUND_DEBUG
	struct rbnode_type* n;
#endif
	/* already exists, and for a different purpose perhaps.
	 * if mesh_no_list, keep it that way. */
	if(s) {
		/* make it ignore the cache from now on */
		if(!s->s.blacklist)
			sock_list_insert(&s->s.blacklist, NULL, 0, s->s.region);
		if(s->s.prefetch_leeway < leeway)
			s->s.prefetch_leeway = leeway;
		return;
	}
	if(!mesh_make_new_space(mesh, NULL)) {
		verbose(VERB_ALGO, "Too many queries. dropped prefetch.");
		mesh->stats_dropped ++;
		return;
	}

	s = mesh_state_create(mesh->env, qinfo, NULL, mesh_flags, 0, 0);
	if(!s) {
		log_err("prefetch mesh_state_create: out of memory");
		return;
	}
#ifdef UNBOUND_DEBUG
	n =
#else
	(void)
#endif
	rbtree_insert(&mesh->all, &s->node);
	log_assert(n != NULL);
	/* set detached (it is now) */
	mesh->num_detached_states++;
	/* make it ignore the cache */
	sock_list_insert(&s->s.blacklist, NULL, 0, s->s.region);
	s->s.prefetch_leeway = leeway;

	if(s->list_select == mesh_no_list) {
		/* move to either the forever or the jostle_list */
		if(mesh->num_forever_states < mesh->max_forever_states) {
			mesh->num_forever_states ++;
			mesh_list_insert(s, &mesh->forever_first,
				&mesh->forever_last);
			s->list_select = mesh_forever_list;
		} else {
			mesh_list_insert(s, &mesh->jostle_first,
				&mesh->jostle_last);
			s->list_select = mesh_jostle_list;
		}
	}
	s->s.rpz_passthru = rpz_passthru;

	if(!run) {
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->run, &s->run_node);
		log_assert(n != NULL);
		return;
	}

	mesh_run(mesh, s, module_event_new, NULL);
}

#ifdef CLIENT_SUBNET
/* Same logic as mesh_schedule_prefetch but tailored to the subnet module logic
 * like passing along the comm_reply info. This will be faked into an EDNS
 * option for processing by the subnet module if the client has not already
 * attached its own ECS data. */
static void mesh_schedule_prefetch_subnet(struct mesh_area* mesh,
	struct query_info* qinfo, uint16_t qflags, time_t leeway, int run,
	int rpz_passthru, struct sockaddr_storage* addr, struct edns_option* edns_list)
{
	struct mesh_state* s = NULL;
	struct edns_option* opt = NULL;
#ifdef UNBOUND_DEBUG
	struct rbnode_type* n;
#endif
	/* Explicitly set the BIT_RD regardless of the client's flags. This is
	 * for a prefetch query (no client attached) but it needs to be treated
	 * as a recursion query. */
	uint16_t mesh_flags = BIT_RD|(qflags&BIT_CD);
	if(!mesh_make_new_space(mesh, NULL)) {
		verbose(VERB_ALGO, "Too many queries. dropped prefetch.");
		mesh->stats_dropped ++;
		return;
	}

	s = mesh_state_create(mesh->env, qinfo, NULL, mesh_flags, 0, 0);
	if(!s) {
		log_err("prefetch_subnet mesh_state_create: out of memory");
		return;
	}
	mesh_state_make_unique(s);

	opt = edns_opt_list_find(edns_list, mesh->env->cfg->client_subnet_opcode);
	if(opt) {
		/* Use the client's ECS data */
		if(!edns_opt_list_append(&s->s.edns_opts_front_in, opt->opt_code,
			opt->opt_len, opt->opt_data, s->s.region)) {
			log_err("prefetch_subnet edns_opt_list_append: out of memory");
			return;
		}
	} else {
		/* Store the client's address. Later in the subnet module,
		 * it is decided whether to include an ECS option or not.
		 */
		s->s.client_addr =  *addr;
	}
#ifdef UNBOUND_DEBUG
	n =
#else
	(void)
#endif
	rbtree_insert(&mesh->all, &s->node);
	log_assert(n != NULL);
	/* set detached (it is now) */
	mesh->num_detached_states++;
	/* make it ignore the cache */
	sock_list_insert(&s->s.blacklist, NULL, 0, s->s.region);
	s->s.prefetch_leeway = leeway;

	if(s->list_select == mesh_no_list) {
		/* move to either the forever or the jostle_list */
		if(mesh->num_forever_states < mesh->max_forever_states) {
			mesh->num_forever_states ++;
			mesh_list_insert(s, &mesh->forever_first,
				&mesh->forever_last);
			s->list_select = mesh_forever_list;
		} else {
			mesh_list_insert(s, &mesh->jostle_first,
				&mesh->jostle_last);
			s->list_select = mesh_jostle_list;
		}
	}
	s->s.rpz_passthru = rpz_passthru;

	if(!run) {
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->run, &s->run_node);
		log_assert(n != NULL);
		return;
	}

	mesh_run(mesh, s, module_event_new, NULL);
}
#endif /* CLIENT_SUBNET */

void mesh_new_prefetch(struct mesh_area* mesh, struct query_info* qinfo,
	uint16_t qflags, time_t leeway, int rpz_passthru,
	struct sockaddr_storage* addr, struct edns_option* opt_list)
{
	(void)addr;
	(void)opt_list;
#ifdef CLIENT_SUBNET
	if(addr)
		mesh_schedule_prefetch_subnet(mesh, qinfo, qflags, leeway, 1,
			rpz_passthru, addr, opt_list);
	else
#endif
		mesh_schedule_prefetch(mesh, qinfo, qflags, leeway, 1,
			rpz_passthru);
}

void mesh_report_reply(struct mesh_area* mesh, struct outbound_entry* e,
        struct comm_reply* reply, int what)
{
	enum module_ev event = module_event_reply;
	e->qstate->reply = reply;
	if(what != NETEVENT_NOERROR) {
		event = module_event_noreply;
		if(what == NETEVENT_CAPSFAIL)
			event = module_event_capsfail;
	}
	mesh_run(mesh, e->qstate->mesh_info, event, e);
}

/** copy strlist to region */
static struct config_strlist*
cfg_region_strlist_copy(struct regional* region, struct config_strlist* list)
{
	struct config_strlist* result = NULL, *last = NULL, *s = list;
	while(s) {
		struct config_strlist* n = regional_alloc_zero(region,
			sizeof(*n));
		if(!n)
			return NULL;
		n->str = regional_strdup(region, s->str);
		if(!n->str)
			return NULL;
		if(last)
			last->next = n;
		else	result = n;
		last = n;
		s = s->next;
	}
	return result;
}

/** Copy the client info to the query region. */
static struct respip_client_info*
mesh_copy_client_info(struct regional* region, struct respip_client_info* cinfo)
{
	size_t i;
	struct respip_client_info* client_info;
	client_info = regional_alloc_init(region, cinfo, sizeof(*cinfo));
	if(!client_info)
		return NULL;
	/* Copy the client_info so that if the configuration changes,
	 * then the data stays valid. */
	if(cinfo->taglist) {
		client_info->taglist = regional_alloc_init(region, cinfo->taglist,
			cinfo->taglen);
		if(!client_info->taglist)
			return NULL;
	}
	if(cinfo->tag_actions) {
		client_info->tag_actions = regional_alloc_init(region, cinfo->tag_actions,
			cinfo->tag_actions_size);
		if(!client_info->tag_actions)
			return NULL;
	}
	if(cinfo->tag_datas) {
		client_info->tag_datas = regional_alloc_zero(region,
			sizeof(struct config_strlist*)*cinfo->tag_datas_size);
		if(!client_info->tag_datas)
			return NULL;
		for(i=0; i<cinfo->tag_datas_size; i++) {
			if(cinfo->tag_datas[i]) {
				client_info->tag_datas[i] = cfg_region_strlist_copy(
					region, cinfo->tag_datas[i]);
				if(!client_info->tag_datas[i])
					return NULL;
			}
		}
	}
	if(cinfo->view) {
		/* Do not copy the view pointer but store a name instead.
		 * The name is looked up later when done, this means that
		 * the view tree can be changed, by reloads. */
		client_info->view = NULL;
		client_info->view_name = regional_strdup(region,
			cinfo->view->name);
		if(!client_info->view_name)
			return NULL;
	}
	return client_info;
}

struct mesh_state*
mesh_state_create(struct module_env* env, struct query_info* qinfo,
	struct respip_client_info* cinfo, uint16_t qflags, int prime,
	int valrec)
{
	struct regional* region = alloc_reg_obtain(env->alloc);
	struct mesh_state* mstate;
	int i;
	if(!region)
		return NULL;
	mstate = (struct mesh_state*)regional_alloc(region,
		sizeof(struct mesh_state));
	if(!mstate) {
		alloc_reg_release(env->alloc, region);
		return NULL;
	}
	memset(mstate, 0, sizeof(*mstate));
	mstate->node = *RBTREE_NULL;
	mstate->run_node = *RBTREE_NULL;
	mstate->node.key = mstate;
	mstate->run_node.key = mstate;
	mstate->reply_list = NULL;
	mstate->list_select = mesh_no_list;
	mstate->replies_sent = 0;
	rbtree_init(&mstate->super_set, &mesh_state_ref_compare);
	rbtree_init(&mstate->sub_set, &mesh_state_ref_compare);
	mstate->num_activated = 0;
	mstate->unique = NULL;
	/* init module qstate */
	mstate->s.qinfo.qtype = qinfo->qtype;
	mstate->s.qinfo.qclass = qinfo->qclass;
	mstate->s.qinfo.local_alias = NULL;
	mstate->s.qinfo.qname_len = qinfo->qname_len;
	mstate->s.qinfo.qname = regional_alloc_init(region, qinfo->qname,
		qinfo->qname_len);
	if(!mstate->s.qinfo.qname) {
		alloc_reg_release(env->alloc, region);
		return NULL;
	}
	if(cinfo) {
		mstate->s.client_info = mesh_copy_client_info(region, cinfo);
		if(!mstate->s.client_info) {
			alloc_reg_release(env->alloc, region);
			return NULL;
		}
	}
	/* remove all weird bits from qflags */
	mstate->s.query_flags = (qflags & (BIT_RD|BIT_CD));
	mstate->s.is_priming = prime;
	mstate->s.is_valrec = valrec;
	mstate->s.reply = NULL;
	mstate->s.region = region;
	mstate->s.curmod = 0;
	mstate->s.return_msg = 0;
	mstate->s.return_rcode = LDNS_RCODE_NOERROR;
	mstate->s.env = env;
	mstate->s.mesh_info = mstate;
	mstate->s.prefetch_leeway = 0;
	mstate->s.serve_expired_data = NULL;
	mstate->s.no_cache_lookup = 0;
	mstate->s.no_cache_store = 0;
	mstate->s.need_refetch = 0;
	mstate->s.was_ratelimited = 0;
	mstate->s.qstarttime = *env->now;

	/* init modules */
	for(i=0; i<env->mesh->mods.num; i++) {
		mstate->s.minfo[i] = NULL;
		mstate->s.ext_state[i] = module_state_initial;
	}
	/* init edns option lists */
	mstate->s.edns_opts_front_in = NULL;
	mstate->s.edns_opts_back_out = NULL;
	mstate->s.edns_opts_back_in = NULL;
	mstate->s.edns_opts_front_out = NULL;

	return mstate;
}

void
mesh_state_make_unique(struct mesh_state* mstate)
{
	mstate->unique = mstate;
}

void
mesh_state_cleanup(struct mesh_state* mstate)
{
	struct mesh_area* mesh;
	int i;
	if(!mstate)
		return;
	mesh = mstate->s.env->mesh;
	/* Stop and delete the serve expired timer */
	if(mstate->s.serve_expired_data && mstate->s.serve_expired_data->timer) {
		comm_timer_delete(mstate->s.serve_expired_data->timer);
		mstate->s.serve_expired_data->timer = NULL;
	}
	/* drop unsent replies */
	if(!mstate->replies_sent) {
		struct mesh_reply* rep = mstate->reply_list;
		struct mesh_cb* cb;
		/* in tcp_req_info, the mstates linked are removed, but
		 * the reply_list is now NULL, so the remove-from-empty-list
		 * takes no time and also it does not do the mesh accounting */
		mstate->reply_list = NULL;
		for(; rep; rep=rep->next) {
			infra_wait_limit_dec(mesh->env->infra_cache,
				&rep->query_reply, mesh->env->cfg);
			if(rep->query_reply.c->use_h2)
				http2_stream_remove_mesh_state(rep->h2_stream);
			comm_point_drop_reply(&rep->query_reply);
			log_assert(mesh->num_reply_addrs > 0);
			mesh->num_reply_addrs--;
		}
		while((cb = mstate->cb_list)!=NULL) {
			mstate->cb_list = cb->next;
			fptr_ok(fptr_whitelist_mesh_cb(cb->cb));
			(*cb->cb)(cb->cb_arg, LDNS_RCODE_SERVFAIL, NULL,
				sec_status_unchecked, NULL, 0);
			log_assert(mesh->num_reply_addrs > 0);
			mesh->num_reply_addrs--;
		}
	}

	/* de-init modules */
	for(i=0; i<mesh->mods.num; i++) {
		fptr_ok(fptr_whitelist_mod_clear(mesh->mods.mod[i]->clear));
		(*mesh->mods.mod[i]->clear)(&mstate->s, i);
		mstate->s.minfo[i] = NULL;
		mstate->s.ext_state[i] = module_finished;
	}
	alloc_reg_release(mstate->s.env->alloc, mstate->s.region);
}

void
mesh_state_delete(struct module_qstate* qstate)
{
	struct mesh_area* mesh;
	struct mesh_state_ref* super, ref;
	struct mesh_state* mstate;
	if(!qstate)
		return;
	mstate = qstate->mesh_info;
	mesh = mstate->s.env->mesh;
	mesh_detach_subs(&mstate->s);
	if(mstate->list_select == mesh_forever_list) {
		mesh->num_forever_states --;
		mesh_list_remove(mstate, &mesh->forever_first,
			&mesh->forever_last);
	} else if(mstate->list_select == mesh_jostle_list) {
		mesh_list_remove(mstate, &mesh->jostle_first,
			&mesh->jostle_last);
	}
	if(!mstate->reply_list && !mstate->cb_list
		&& mstate->super_set.count == 0) {
		log_assert(mesh->num_detached_states > 0);
		mesh->num_detached_states--;
	}
	if(mstate->reply_list || mstate->cb_list) {
		log_assert(mesh->num_reply_states > 0);
		mesh->num_reply_states--;
	}
	ref.node.key = &ref;
	ref.s = mstate;
	RBTREE_FOR(super, struct mesh_state_ref*, &mstate->super_set) {
		(void)rbtree_delete(&super->s->sub_set, &ref);
	}
	(void)rbtree_delete(&mesh->run, mstate);
	(void)rbtree_delete(&mesh->all, mstate);
	mesh_state_cleanup(mstate);
}

/** helper recursive rbtree find routine */
static int
find_in_subsub(struct mesh_state* m, struct mesh_state* tofind, size_t *c)
{
	struct mesh_state_ref* r;
	if((*c)++ > MESH_MAX_SUBSUB)
		return 1;
	RBTREE_FOR(r, struct mesh_state_ref*, &m->sub_set) {
		if(r->s == tofind || find_in_subsub(r->s, tofind, c))
			return 1;
	}
	return 0;
}

/** find cycle for already looked up mesh_state */
static int
mesh_detect_cycle_found(struct module_qstate* qstate, struct mesh_state* dep_m)
{
	struct mesh_state* cyc_m = qstate->mesh_info;
	size_t counter = 0;
	if(!dep_m)
		return 0;
	if(dep_m == cyc_m || find_in_subsub(dep_m, cyc_m, &counter)) {
		if(counter > MESH_MAX_SUBSUB)
			return 2;
		return 1;
	}
	return 0;
}

void mesh_detach_subs(struct module_qstate* qstate)
{
	struct mesh_area* mesh = qstate->env->mesh;
	struct mesh_state_ref* ref, lookup;
#ifdef UNBOUND_DEBUG
	struct rbnode_type* n;
#endif
	lookup.node.key = &lookup;
	lookup.s = qstate->mesh_info;
	RBTREE_FOR(ref, struct mesh_state_ref*, &qstate->mesh_info->sub_set) {
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_delete(&ref->s->super_set, &lookup);
		log_assert(n != NULL); /* must have been present */
		if(!ref->s->reply_list && !ref->s->cb_list
			&& ref->s->super_set.count == 0) {
			mesh->num_detached_states++;
			log_assert(mesh->num_detached_states +
				mesh->num_reply_states <= mesh->all.count);
		}
	}
	rbtree_init(&qstate->mesh_info->sub_set, &mesh_state_ref_compare);
}

int mesh_add_sub(struct module_qstate* qstate, struct query_info* qinfo,
        uint16_t qflags, int prime, int valrec, struct module_qstate** newq,
	struct mesh_state** sub)
{
	/* find it, if not, create it */
	struct mesh_area* mesh = qstate->env->mesh;
	*sub = mesh_area_find(mesh, NULL, qinfo, qflags,
		prime, valrec);
	if(mesh_detect_cycle_found(qstate, *sub)) {
		verbose(VERB_ALGO, "attach failed, cycle detected");
		return 0;
	}
	if(!*sub) {
#ifdef UNBOUND_DEBUG
		struct rbnode_type* n;
#endif
		/* create a new one */
		*sub = mesh_state_create(qstate->env, qinfo, NULL, qflags, prime,
			valrec);
		if(!*sub) {
			log_err("mesh_attach_sub: out of memory");
			return 0;
		}
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->all, &(*sub)->node);
		log_assert(n != NULL);
		/* set detached (it is now) */
		mesh->num_detached_states++;
		/* set new query state to run */
#ifdef UNBOUND_DEBUG
		n =
#else
		(void)
#endif
		rbtree_insert(&mesh->run, &(*sub)->run_node);
		log_assert(n != NULL);
		*newq = &(*sub)->s;
	} else
		*newq = NULL;
	return 1;
}

int mesh_attach_sub(struct module_qstate* qstate, struct query_info* qinfo,
        uint16_t qflags, int prime, int valrec, struct module_qstate** newq)
{
	struct mesh_area* mesh = qstate->env->mesh;
	struct mesh_state* sub = NULL;
	int was_detached;
	if(!mesh_add_sub(qstate, qinfo, qflags, prime, valrec, newq, &sub))
		return 0;
	was_detached = (sub->super_set.count == 0);
	if(!mesh_state_attachment(qstate->mesh_info, sub))
		return 0;
	/* if it was a duplicate  attachment, the count was not zero before */
	if(!sub->reply_list && !sub->cb_list && was_detached &&
		sub->super_set.count == 1) {
		/* it used to be detached, before this one got added */
		log_assert(mesh->num_detached_states > 0);
		mesh->num_detached_states--;
	}
	/* *newq will be run when inited after the current module stops */
	return 1;
}

int mesh_state_attachment(struct mesh_state* super, struct mesh_state* sub)
{
#ifdef UNBOUND_DEBUG
	struct rbnode_type* n;
#endif
	struct mesh_state_ref* subref; /* points to sub, inserted in super */
	struct mesh_state_ref* superref; /* points to super, inserted in sub */
	if( !(subref = regional_alloc(super->s.region,
		sizeof(struct mesh_state_ref))) ||
		!(superref = regional_alloc(sub->s.region,
		sizeof(struct mesh_state_ref))) ) {
		log_err("mesh_state_attachment: out of memory");
		return 0;
	}
	superref->node.key = superref;
	superref->s = super;
	subref->node.key = subref;
	subref->s = sub;
	if(!rbtree_insert(&sub->super_set, &superref->node)) {
		/* this should not happen, iterator and validator do not
		 * attach subqueries that are identical. */
		/* already attached, we are done, nothing todo.
		 * since superref and subref already allocated in region,
		 * we cannot free them */
		return 1;
	}
#ifdef UNBOUND_DEBUG
	n =
#else
	(void)
#endif
	rbtree_insert(&super->sub_set, &subref->node);
	log_assert(n != NULL); /* we checked above if statement, the reverse
	  administration should not fail now, unless they are out of sync */
	return 1;
}

/**
 * callback results to mesh cb entry
 * @param m: mesh state to send it for.
 * @param rcode: if not 0, error code.
 * @param rep: reply to send (or NULL if rcode is set).
 * @param r: callback entry
 * @param start_time: the time to pass to callback functions, it is 0 or
 * 	a value from one of the packets if the mesh state had packets.
 */
static void
mesh_do_callback(struct mesh_state* m, int rcode, struct reply_info* rep,
	struct mesh_cb* r, struct timeval* start_time)
{
	int secure;
	char* reason = NULL;
	int was_ratelimited = m->s.was_ratelimited;
	/* bogus messages are not made into servfail, sec_status passed
	 * to the callback function */
	if(rep && rep->security == sec_status_secure)
		secure = 1;
	else	secure = 0;
	if(!rep && rcode == LDNS_RCODE_NOERROR)
		rcode = LDNS_RCODE_SERVFAIL;
	if(!rcode && rep && (rep->security == sec_status_bogus ||
		rep->security == sec_status_secure_sentinel_fail)) {
		if(!(reason = errinf_to_str_bogus(&m->s, NULL)))
			rcode = LDNS_RCODE_SERVFAIL;
	}
	/* send the reply */
	if(rcode) {
		if(rcode == LDNS_RCODE_SERVFAIL) {
			if(!inplace_cb_reply_servfail_call(m->s.env, &m->s.qinfo, &m->s,
				rep, rcode, &r->edns, NULL, m->s.region, start_time))
					r->edns.opt_list_inplace_cb_out = NULL;
		} else {
			if(!inplace_cb_reply_call(m->s.env, &m->s.qinfo, &m->s, rep, rcode,
				&r->edns, NULL, m->s.region, start_time))
					r->edns.opt_list_inplace_cb_out = NULL;
		}
		fptr_ok(fptr_whitelist_mesh_cb(r->cb));
		(*r->cb)(r->cb_arg, rcode, r->buf, sec_status_unchecked, NULL,
			was_ratelimited);
	} else {
		size_t udp_size = r->edns.udp_size;
		sldns_buffer_clear(r->buf);
		r->edns.edns_version = EDNS_ADVERTISED_VERSION;
		r->edns.udp_size = EDNS_ADVERTISED_SIZE;
		r->edns.ext_rcode = 0;
		r->edns.bits &= EDNS_DO;
		if(m->s.env->cfg->disable_edns_do && (r->edns.bits&EDNS_DO))
			r->edns.edns_present = 0;

		if(!inplace_cb_reply_call(m->s.env, &m->s.qinfo, &m->s, rep,
			LDNS_RCODE_NOERROR, &r->edns, NULL, m->s.region, start_time) ||
			!reply_info_answer_encode(&m->s.qinfo, rep, r->qid,
			r->qflags, r->buf, 0, 1,
			m->s.env->scratch, udp_size, &r->edns,
			(int)(r->edns.bits & EDNS_DO), secure))
		{
			fptr_ok(fptr_whitelist_mesh_cb(r->cb));
			(*r->cb)(r->cb_arg, LDNS_RCODE_SERVFAIL, r->buf,
				sec_status_unchecked, NULL, 0);
		} else {
			fptr_ok(fptr_whitelist_mesh_cb(r->cb));
			(*r->cb)(r->cb_arg, LDNS_RCODE_NOERROR, r->buf,
				(rep?rep->security:sec_status_unchecked),
				reason, was_ratelimited);
		}
	}
	free(reason);
	log_assert(m->s.env->mesh->num_reply_addrs > 0);
	m->s.env->mesh->num_reply_addrs--;
}

static inline int
mesh_is_rpz_respip_tcponly_action(struct mesh_state const* m)
{
	struct respip_action_info const* respip_info = m->s.respip_action_info;
	return (respip_info == NULL
			? 0
			: (respip_info->rpz_used
			&& !respip_info->rpz_disabled
			&& respip_info->action == respip_truncate))
		|| m->s.tcp_required;
}

static inline int
mesh_is_udp(struct mesh_reply const* r)
{
	return r->query_reply.c->type == comm_udp;
}

static inline void
mesh_find_and_attach_ede_and_reason(struct mesh_state* m,
	struct reply_info* rep, struct mesh_reply* r)
{
	/* OLD note:
	 * During validation the EDE code can be received via two
	 * code paths. One code path fills the reply_info EDE, and
	 * the other fills it in the errinf_strlist. These paths
	 * intersect at some points, but where is opaque due to
	 * the complexity of the validator. At the time of writing
	 * we make the choice to prefer the EDE from errinf_strlist
	 * but a compelling reason to do otherwise is just as valid
	 * NEW note:
	 * The compelling reason is that with caching support, the value
	 * in the reply_info is cached.
	 * The reason members of the reply_info struct should be
	 * updated as they are already cached. No reason to
	 * try and find the EDE information in errinf anymore.
	 */
	if(rep->reason_bogus != LDNS_EDE_NONE) {
		edns_opt_list_append_ede(&r->edns.opt_list_out,
			m->s.region, rep->reason_bogus, rep->reason_bogus_str);
	}
}

/**
 * Send reply to mesh reply entry
 * @param m: mesh state to send it for.
 * @param rcode: if not 0, error code.
 * @param rep: reply to send (or NULL if rcode is set).
 * @param r: reply entry
 * @param r_buffer: buffer to use for reply entry.
 * @param prev: previous reply, already has its answer encoded in buffer.
 * @param prev_buffer: buffer for previous reply.
 */
static void
mesh_send_reply(struct mesh_state* m, int rcode, struct reply_info* rep,
	struct mesh_reply* r, struct sldns_buffer* r_buffer,
	struct mesh_reply* prev, struct sldns_buffer* prev_buffer)
{
	struct timeval end_time;
	struct timeval duration;
	int secure;
	/* briefly set the replylist to null in case the
	 * meshsendreply calls tcpreqinfo sendreply that
	 * comm_point_drops because of size, and then the
	 * null stops the mesh state remove and thus
	 * reply_list modification and accounting */
	struct mesh_reply* rlist = m->reply_list;

	/* rpz: apply actions */
	rcode = mesh_is_udp(r) && mesh_is_rpz_respip_tcponly_action(m)
			? (rcode|BIT_TC) : rcode;

	/* examine security status */
	if(m->s.env->need_to_validate && (!(r->qflags&BIT_CD) ||
		m->s.env->cfg->ignore_cd) && rep &&
		(rep->security <= sec_status_bogus ||
		rep->security == sec_status_secure_sentinel_fail)) {
		rcode = LDNS_RCODE_SERVFAIL;
		if(m->s.env->cfg->stat_extended)
			m->s.env->mesh->ans_bogus++;
	}
	if(rep && rep->security == sec_status_secure)
		secure = 1;
	else	secure = 0;
	if(!rep && rcode == LDNS_RCODE_NOERROR)
		rcode = LDNS_RCODE_SERVFAIL;
	if(r->query_reply.c->use_h2) {
		r->query_reply.c->h2_stream = r->h2_stream;
		/* Mesh reply won't exist for long anymore. Make it impossible
		 * for HTTP/2 stream to refer to mesh state, in case
		 * connection gets cleanup before HTTP/2 stream close. */
		r->h2_stream->mesh_state = NULL;
	}
	/* send the reply */
	/* We don't reuse the encoded answer if:
	 * - either the previous or current response has a local alias.  We could
	 *   compare the alias records and still reuse the previous answer if they
	 *   are the same, but that would be complicated and error prone for the
	 *   relatively minor case. So we err on the side of safety.
	 * - there are registered callback functions for the given rcode, as these
	 *   need to be called for each reply. */
	if(((rcode != LDNS_RCODE_SERVFAIL &&
			!m->s.env->inplace_cb_lists[inplace_cb_reply]) ||
		(rcode == LDNS_RCODE_SERVFAIL &&
			!m->s.env->inplace_cb_lists[inplace_cb_reply_servfail])) &&
		prev && prev_buffer && prev->qflags == r->qflags &&
		!prev->local_alias && !r->local_alias &&
		prev->edns.edns_present == r->edns.edns_present &&
		prev->edns.bits == r->edns.bits &&
		prev->edns.udp_size == r->edns.udp_size &&
		edns_opt_list_compare(prev->edns.opt_list_out, r->edns.opt_list_out) == 0 &&
		edns_opt_list_compare(prev->edns.opt_list_inplace_cb_out, r->edns.opt_list_inplace_cb_out) == 0
		) {
		/* if the previous reply is identical to this one, fix ID */
		if(prev_buffer != r_buffer)
			sldns_buffer_copy(r_buffer, prev_buffer);
		sldns_buffer_write_at(r_buffer, 0, &r->qid, sizeof(uint16_t));
		sldns_buffer_write_at(r_buffer, 12, r->qname,
			m->s.qinfo.qname_len);
		m->reply_list = NULL;
		comm_point_send_reply(&r->query_reply);
		m->reply_list = rlist;
	} else if(rcode) {
		m->s.qinfo.qname = r->qname;
		m->s.qinfo.local_alias = r->local_alias;
		if(rcode == LDNS_RCODE_SERVFAIL) {
			if(!inplace_cb_reply_servfail_call(m->s.env, &m->s.qinfo, &m->s,
				rep, rcode, &r->edns, &r->query_reply, m->s.region, &r->start_time))
					r->edns.opt_list_inplace_cb_out = NULL;
		} else {
			if(!inplace_cb_reply_call(m->s.env, &m->s.qinfo, &m->s, rep, rcode,
				&r->edns, &r->query_reply, m->s.region, &r->start_time))
					r->edns.opt_list_inplace_cb_out = NULL;
		}
		/* Send along EDE EDNS0 option when SERVFAILing; usually
		 * DNSSEC validation failures */
		/* Since we are SERVFAILing here, CD bit and rep->security
		 * is already handled. */
		if(m->s.env->cfg->ede && rep) {
			mesh_find_and_attach_ede_and_reason(m, rep, r);
		}
		error_encode(r_buffer, rcode, &m->s.qinfo, r->qid,
			r->qflags, &r->edns);
		m->reply_list = NULL;
		comm_point_send_reply(&r->query_reply);
		m->reply_list = rlist;
	} else {
		size_t udp_size = r->edns.udp_size;
		r->edns.edns_version = EDNS_ADVERTISED_VERSION;
		r->edns.udp_size = EDNS_ADVERTISED_SIZE;
		r->edns.ext_rcode = 0;
		r->edns.bits &= EDNS_DO;
		if(m->s.env->cfg->disable_edns_do && (r->edns.bits&EDNS_DO))
			r->edns.edns_present = 0;
		m->s.qinfo.qname = r->qname;
		m->s.qinfo.local_alias = r->local_alias;

		/* Attach EDE without SERVFAIL if the validation failed.
		 * Need to explicitly check for rep->security otherwise failed
		 * validation paths may attach to a secure answer. */
		if(m->s.env->cfg->ede && rep &&
			(rep->security <= sec_status_bogus ||
			rep->security == sec_status_secure_sentinel_fail)) {
			mesh_find_and_attach_ede_and_reason(m, rep, r);
		}

		if(!inplace_cb_reply_call(m->s.env, &m->s.qinfo, &m->s, rep,
			LDNS_RCODE_NOERROR, &r->edns, &r->query_reply, m->s.region, &r->start_time) ||
			!reply_info_answer_encode(&m->s.qinfo, rep, r->qid,
			r->qflags, r_buffer, 0, 1, m->s.env->scratch,
			udp_size, &r->edns, (int)(r->edns.bits & EDNS_DO),
			secure))
		{
			if(!inplace_cb_reply_servfail_call(m->s.env, &m->s.qinfo, &m->s,
			rep, LDNS_RCODE_SERVFAIL, &r->edns, &r->query_reply, m->s.region, &r->start_time))
				r->edns.opt_list_inplace_cb_out = NULL;
			/* internal server error (probably malloc failure) so no
			 * EDE (RFC8914) needed */
			error_encode(r_buffer, LDNS_RCODE_SERVFAIL,
				&m->s.qinfo, r->qid, r->qflags, &r->edns);
		}
		m->reply_list = NULL;
		comm_point_send_reply(&r->query_reply);
		m->reply_list = rlist;
	}
	infra_wait_limit_dec(m->s.env->infra_cache, &r->query_reply,
		m->s.env->cfg);
	/* account */
	log_assert(m->s.env->mesh->num_reply_addrs > 0);
	m->s.env->mesh->num_reply_addrs--;
	end_time = *m->s.env->now_tv;
	timeval_subtract(&duration, &end_time, &r->start_time);
	verbose(VERB_ALGO, "query took " ARG_LL "d.%6.6d sec",
		(long long)duration.tv_sec, (int)duration.tv_usec);
	m->s.env->mesh->replies_sent++;
	timeval_add(&m->s.env->mesh->replies_sum_wait, &duration);
	timehist_insert(m->s.env->mesh->histogram, &duration);
	if(m->s.env->cfg->stat_extended) {
		uint16_t rc = FLAGS_GET_RCODE(sldns_buffer_read_u16_at(
			r_buffer, 2));
		if(secure) m->s.env->mesh->ans_secure++;
		m->s.env->mesh->ans_rcode[ rc ] ++;
		if(rc == 0 && LDNS_ANCOUNT(sldns_buffer_begin(r_buffer)) == 0)
			m->s.env->mesh->ans_nodata++;
	}
	/* Log reply sent */
	if(m->s.env->cfg->log_replies) {
		log_reply_info(NO_VERBOSE, &m->s.qinfo,
			&r->query_reply.client_addr,
			r->query_reply.client_addrlen, duration, 0, r_buffer,
			(m->s.env->cfg->log_destaddr?(void*)r->query_reply.c->socket->addr:NULL),
			r->query_reply.c->type, r->query_reply.c->ssl);
	}
}

/**
 * Generate the DNS Error Report (RFC9567).
 * If there is an EDE attached for this reply and there was a Report-Channel
 * EDNS0 option from the upstream, fire up a report query.
 * @param qstate: module qstate.
 * @param rep: prepared reply to be sent.
 */
static void dns_error_reporting(struct module_qstate* qstate,
	struct reply_info* rep)
{
	struct query_info qinfo;
	struct mesh_state* sub;
	struct module_qstate* newq;
	uint8_t buf[LDNS_MAX_DOMAINLEN];
	size_t count = 0;
	int written;
	size_t expected_length;
	struct edns_option* opt;
	sldns_ede_code reason_bogus = LDNS_EDE_NONE;
	sldns_rr_type qtype = qstate->qinfo.qtype;
	uint8_t* qname = qstate->qinfo.qname;
	size_t qname_len = qstate->qinfo.qname_len-1; /* skip the trailing \0 */
	uint8_t* agent_domain;
	size_t agent_domain_len;

	/* We need a valid reporting agent;
	 * this is based on qstate->edns_opts_back_in that will probably have
	 * the latest reporting agent we found while iterating */
	opt = edns_opt_list_find(qstate->edns_opts_back_in,
		LDNS_EDNS_REPORT_CHANNEL);
	if(!opt) return;
	agent_domain_len = opt->opt_len;
	agent_domain = opt->opt_data;
	if(dname_valid(agent_domain, agent_domain_len) < 3) {
		/* The agent domain needs to be a valid dname that is not the
		 * root; from RFC9567. */
		return;
	}

	/* Get the EDE generated from the mesh state, these are mostly
	 * validator errors. If other errors are produced in the future (e.g.,
	 * RPZ) we would not want them to result in error reports. */
	reason_bogus = errinf_to_reason_bogus(qstate);
	if(rep && ((reason_bogus == LDNS_EDE_DNSSEC_BOGUS &&
		rep->reason_bogus != LDNS_EDE_NONE) ||
		reason_bogus == LDNS_EDE_NONE)) {
		reason_bogus = rep->reason_bogus;
	}
	if(reason_bogus == LDNS_EDE_NONE ||
		/* other, does not make sense without the text that comes
		 * with it */
		reason_bogus == LDNS_EDE_OTHER) return;

	/* Synthesize the error report query in the format:
	 * "_er.$qtype.$qname.$ede._er.$reporting-agent-domain" */
	/* First check if the static length parts fit in the buffer.
	 * That is everything except for qtype and ede that need to be
	 * converted to decimal and checked further on. */
	expected_length = 4/*_er*/+qname_len+4/*_er*/+agent_domain_len;
	if(expected_length > LDNS_MAX_DOMAINLEN) goto skip;

	memmove(buf+count, "\3_er", 4);
	count += 4;

	written = snprintf((char*)buf+count, LDNS_MAX_DOMAINLEN-count,
		"X%d", qtype);
	expected_length += written;
	/* Skip on error, truncation or long expected length */
	if(written < 0 || (size_t)written >= LDNS_MAX_DOMAINLEN-count ||
		expected_length > LDNS_MAX_DOMAINLEN ) goto skip;
	/* Put in the label length */
	*(buf+count) = (char)(written - 1);
	count += written;

	memmove(buf+count, qname, qname_len);
	count += qname_len;

	written = snprintf((char*)buf+count, LDNS_MAX_DOMAINLEN-count,
		"X%d", reason_bogus);
	expected_length += written;
	/* Skip on error, truncation or long expected length */
	if(written < 0 || (size_t)written >= LDNS_MAX_DOMAINLEN-count ||
		expected_length > LDNS_MAX_DOMAINLEN ) goto skip;
	*(buf+count) = (char)(written - 1);
	count += written;

	memmove(buf+count, "\3_er", 4);
	count += 4;

	/* Copy the agent domain */
	memmove(buf+count, agent_domain, agent_domain_len);
	count += agent_domain_len;

	qinfo.qname = buf;
	qinfo.qname_len = count;
	qinfo.qtype = LDNS_RR_TYPE_TXT;
	qinfo.qclass = qstate->qinfo.qclass;
	qinfo.local_alias = NULL;

	log_query_info(VERB_ALGO, "DNS Error Reporting: generating report "
		"query for", &qinfo);
	if(mesh_add_sub(qstate, &qinfo, BIT_RD, 0, 0, &newq, &sub)) {
		qstate->env->mesh->num_dns_error_reports++;
	}
	return;
skip:
	verbose(VERB_ALGO, "DNS Error Reporting: report query qname too long; "
		"skip");
	return;
}

void mesh_query_done(struct mesh_state* mstate)
{
	struct mesh_reply* r;
	struct mesh_reply* prev = NULL;
	struct sldns_buffer* prev_buffer = NULL;
	struct mesh_cb* c;
	struct reply_info* rep = (mstate->s.return_msg?
		mstate->s.return_msg->rep:NULL);
	struct timeval tv = {0, 0};
	int i = 0;
	/* No need for the serve expired timer anymore; we are going to reply. */
	if(mstate->s.serve_expired_data) {
		comm_timer_delete(mstate->s.serve_expired_data->timer);
		mstate->s.serve_expired_data->timer = NULL;
	}
	if(mstate->s.return_rcode == LDNS_RCODE_SERVFAIL ||
		(rep && FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_SERVFAIL)) {
		if(mstate->s.env->cfg->serve_expired) {
			/* we are SERVFAILing; check for expired answer here */
			mesh_respond_serve_expired(mstate);
		}
		if((mstate->reply_list || mstate->cb_list)
		&& mstate->s.env->cfg->log_servfail
		&& !mstate->s.env->cfg->val_log_squelch) {
			char* err = errinf_to_str_servfail(&mstate->s);
			if(err) { log_err("%s", err); }
		}
	}

	if(mstate->reply_list && mstate->s.env->cfg->dns_error_reporting)
		dns_error_reporting(&mstate->s, rep);

	for(r = mstate->reply_list; r; r = r->next) {
		struct timeval old;
		timeval_subtract(&old, mstate->s.env->now_tv, &r->start_time);
		if(mstate->s.env->cfg->discard_timeout != 0 &&
			((int)old.tv_sec)*1000+((int)old.tv_usec)/1000 >
			mstate->s.env->cfg->discard_timeout) {
			/* Drop the reply, it is too old */
			/* briefly set the reply_list to NULL, so that the
			 * tcp req info cleanup routine that calls the mesh
			 * to deregister the meshstate for it is not done
			 * because the list is NULL and also accounting is not
			 * done there, but instead we do that here. */
			struct mesh_reply* reply_list = mstate->reply_list;
			verbose(VERB_ALGO, "drop reply, it is older than discard-timeout");
			infra_wait_limit_dec(mstate->s.env->infra_cache,
				&r->query_reply, mstate->s.env->cfg);
			mstate->reply_list = NULL;
			if(r->query_reply.c->use_h2)
				http2_stream_remove_mesh_state(r->h2_stream);
			comm_point_drop_reply(&r->query_reply);
			mstate->reply_list = reply_list;
			mstate->s.env->mesh->num_queries_discard_timeout++;
			continue;
		}

		i++;
		tv = r->start_time;

		/* if a response-ip address block has been stored the
		 *  information should be logged for each client. */
		if(mstate->s.respip_action_info &&
			mstate->s.respip_action_info->addrinfo) {
			respip_inform_print(mstate->s.respip_action_info,
				r->qname, mstate->s.qinfo.qtype,
				mstate->s.qinfo.qclass, r->local_alias,
				&r->query_reply.client_addr,
				r->query_reply.client_addrlen);
		}

		/* if this query is determined to be dropped during the
		 * mesh processing, this is the point to take that action. */
		if(mstate->s.is_drop) {
			/* briefly set the reply_list to NULL, so that the
			 * tcp req info cleanup routine that calls the mesh
			 * to deregister the meshstate for it is not done
			 * because the list is NULL and also accounting is not
			 * done there, but instead we do that here. */
			struct mesh_reply* reply_list = mstate->reply_list;
			infra_wait_limit_dec(mstate->s.env->infra_cache,
				&r->query_reply, mstate->s.env->cfg);
			mstate->reply_list = NULL;
			if(r->query_reply.c->use_h2) {
				http2_stream_remove_mesh_state(r->h2_stream);
			}
			comm_point_drop_reply(&r->query_reply);
			mstate->reply_list = reply_list;
		} else {
			struct sldns_buffer* r_buffer = r->query_reply.c->buffer;
			if(r->query_reply.c->tcp_req_info) {
				r_buffer = r->query_reply.c->tcp_req_info->spool_buffer;
				prev_buffer = NULL;
			}
			mesh_send_reply(mstate, mstate->s.return_rcode, rep,
				r, r_buffer, prev, prev_buffer);
			if(r->query_reply.c->tcp_req_info) {
				tcp_req_info_remove_mesh_state(r->query_reply.c->tcp_req_info, mstate);
				r_buffer = NULL;
			}
			/* mesh_send_reply removed mesh state from
			 * http2_stream. */
			prev = r;
			prev_buffer = r_buffer;
		}
	}
	/* Account for each reply sent. */
	if(i > 0 && mstate->s.respip_action_info &&
		mstate->s.respip_action_info->addrinfo &&
		mstate->s.env->cfg->stat_extended &&
		mstate->s.respip_action_info->rpz_used) {
		if(mstate->s.respip_action_info->rpz_disabled)
			mstate->s.env->mesh->rpz_action[RPZ_DISABLED_ACTION] += i;
		if(mstate->s.respip_action_info->rpz_cname_override)
			mstate->s.env->mesh->rpz_action[RPZ_CNAME_OVERRIDE_ACTION] += i;
		else
			mstate->s.env->mesh->rpz_action[respip_action_to_rpz_action(
				mstate->s.respip_action_info->action)] += i;
	}
	if(!mstate->s.is_drop && i > 0) {
		if(mstate->s.env->cfg->stat_extended
			&& mstate->s.is_cachedb_answer) {
			mstate->s.env->mesh->ans_cachedb += i;
		}
	}

	/* Mesh area accounting */
	if(mstate->reply_list) {
		mstate->reply_list = NULL;
		if(!mstate->reply_list && !mstate->cb_list) {
			/* was a reply state, not anymore */
			log_assert(mstate->s.env->mesh->num_reply_states > 0);
			mstate->s.env->mesh->num_reply_states--;
		}
		if(!mstate->reply_list && !mstate->cb_list &&
			mstate->super_set.count == 0)
			mstate->s.env->mesh->num_detached_states++;
	}
	mstate->replies_sent = 1;

	while((c = mstate->cb_list) != NULL) {
		/* take this cb off the list; so that the list can be
		 * changed, eg. by adds from the callback routine */
		if(!mstate->reply_list && mstate->cb_list && !c->next) {
			/* was a reply state, not anymore */
			log_assert(mstate->s.env->mesh->num_reply_states > 0);
			mstate->s.env->mesh->num_reply_states--;
		}
		mstate->cb_list = c->next;
		if(!mstate->reply_list && !mstate->cb_list &&
			mstate->super_set.count == 0)
			mstate->s.env->mesh->num_detached_states++;
		mesh_do_callback(mstate, mstate->s.return_rcode, rep, c, &tv);
	}
}

void mesh_walk_supers(struct mesh_area* mesh, struct mesh_state* mstate)
{
	struct mesh_state_ref* ref;
	RBTREE_FOR(ref, struct mesh_state_ref*, &mstate->super_set)
	{
		/* make super runnable */
		(void)rbtree_insert(&mesh->run, &ref->s->run_node);
		/* callback the function to inform super of result */
		fptr_ok(fptr_whitelist_mod_inform_super(
			mesh->mods.mod[ref->s->s.curmod]->inform_super));
		(*mesh->mods.mod[ref->s->s.curmod]->inform_super)(&mstate->s,
			ref->s->s.curmod, &ref->s->s);
		/* copy state that is always relevant to super */
		copy_state_to_super(&mstate->s, ref->s->s.curmod, &ref->s->s);
	}
}

struct mesh_state* mesh_area_find(struct mesh_area* mesh,
	struct respip_client_info* cinfo, struct query_info* qinfo,
	uint16_t qflags, int prime, int valrec)
{
	struct mesh_state key;
	struct mesh_state* result;

	key.node.key = &key;
	key.s.is_priming = prime;
	key.s.is_valrec = valrec;
	key.s.qinfo = *qinfo;
	key.s.query_flags = qflags;
	/* We are searching for a similar mesh state when we DO want to
	 * aggregate the state. Thus unique is set to NULL. (default when we
	 * desire aggregation).*/
	key.unique = NULL;
	key.s.client_info = cinfo;

	result = (struct mesh_state*)rbtree_search(&mesh->all, &key);
	return result;
}

/** remove mesh state callback */
int mesh_state_del_cb(struct mesh_state* s, mesh_cb_func_type cb, void* cb_arg)
{
	struct mesh_cb* r, *prev = NULL;
	r = s->cb_list;
	while(r) {
		if(r->cb == cb && r->cb_arg == cb_arg) {
			/* Delete this entry. */
			/* It was allocated in the s.region, so no free. */
			if(prev) prev->next = r->next;
			else s->cb_list = r->next;
			return 1;
		}
		prev = r;
		r = r->next;
	}
	return 0;
}

int mesh_state_add_cb(struct mesh_state* s, struct edns_data* edns,
        sldns_buffer* buf, mesh_cb_func_type cb, void* cb_arg,
	uint16_t qid, uint16_t qflags)
{
	struct mesh_cb* r = regional_alloc(s->s.region,
		sizeof(struct mesh_cb));
	if(!r)
		return 0;
	r->buf = buf;
	log_assert(fptr_whitelist_mesh_cb(cb)); /* early failure ifmissing*/
	r->cb = cb;
	r->cb_arg = cb_arg;
	r->edns = *edns;
	if(edns->opt_list_in && !(r->edns.opt_list_in =
			edns_opt_copy_region(edns->opt_list_in, s->s.region)))
		return 0;
	if(edns->opt_list_out && !(r->edns.opt_list_out =
			edns_opt_copy_region(edns->opt_list_out, s->s.region)))
		return 0;
	if(edns->opt_list_inplace_cb_out && !(r->edns.opt_list_inplace_cb_out =
			edns_opt_copy_region(edns->opt_list_inplace_cb_out, s->s.region)))
		return 0;
	r->qid = qid;
	r->qflags = qflags;
	r->next = s->cb_list;
	s->cb_list = r;
	return 1;

}

int mesh_state_add_reply(struct mesh_state* s, struct edns_data* edns,
        struct comm_reply* rep, uint16_t qid, uint16_t qflags,
        const struct query_info* qinfo)
{
	struct mesh_reply* r = regional_alloc(s->s.region,
		sizeof(struct mesh_reply));
	if(!r)
		return 0;
	r->query_reply = *rep;
	r->edns = *edns;
	if(edns->opt_list_in && !(r->edns.opt_list_in =
			edns_opt_copy_region(edns->opt_list_in, s->s.region)))
		return 0;
	if(edns->opt_list_out && !(r->edns.opt_list_out =
			edns_opt_copy_region(edns->opt_list_out, s->s.region)))
		return 0;
	if(edns->opt_list_inplace_cb_out && !(r->edns.opt_list_inplace_cb_out =
			edns_opt_copy_region(edns->opt_list_inplace_cb_out, s->s.region)))
		return 0;
	r->qid = qid;
	r->qflags = qflags;
	r->start_time = *s->s.env->now_tv;
	r->next = s->reply_list;
	r->qname = regional_alloc_init(s->s.region, qinfo->qname,
		s->s.qinfo.qname_len);
	if(!r->qname)
		return 0;
	if(rep->c->use_h2)
		r->h2_stream = rep->c->h2_stream;
	else	r->h2_stream = NULL;

	/* Data related to local alias stored in 'qinfo' (if any) is ephemeral
	 * and can be different for different original queries (even if the
	 * replaced query name is the same).  So we need to make a deep copy
	 * and store the copy for each reply info. */
	if(qinfo->local_alias) {
		struct packed_rrset_data* d;
		struct packed_rrset_data* dsrc;
		r->local_alias = regional_alloc_zero(s->s.region,
			sizeof(*qinfo->local_alias));
		if(!r->local_alias)
			return 0;
		r->local_alias->rrset = regional_alloc_init(s->s.region,
			qinfo->local_alias->rrset,
			sizeof(*qinfo->local_alias->rrset));
		if(!r->local_alias->rrset)
			return 0;
		dsrc = qinfo->local_alias->rrset->entry.data;

		/* In the current implementation, a local alias must be
		 * a single CNAME RR (see worker_handle_request()). */
		log_assert(!qinfo->local_alias->next && dsrc->count == 1 &&
			qinfo->local_alias->rrset->rk.type ==
			htons(LDNS_RR_TYPE_CNAME));
		/* we should make a local copy for the owner name of
		 * the RRset */
		r->local_alias->rrset->rk.dname_len =
			qinfo->local_alias->rrset->rk.dname_len;
		r->local_alias->rrset->rk.dname = regional_alloc_init(
			s->s.region, qinfo->local_alias->rrset->rk.dname,
			qinfo->local_alias->rrset->rk.dname_len);
		if(!r->local_alias->rrset->rk.dname)
			return 0;

		/* the rrset is not packed, like in the cache, but it is
		 * individually allocated with an allocator from localzone. */
		d = regional_alloc_zero(s->s.region, sizeof(*d));
		if(!d)
			return 0;
		r->local_alias->rrset->entry.data = d;
		if(!rrset_insert_rr(s->s.region, d, dsrc->rr_data[0],
			dsrc->rr_len[0], dsrc->rr_ttl[0], "CNAME local alias"))
			return 0;
	} else
		r->local_alias = NULL;

	s->reply_list = r;
	return 1;
}

/* Extract the query info and flags from 'mstate' into '*qinfop' and '*qflags'.
 * Since this is only used for internal refetch of otherwise-expired answer,
 * we simply ignore the rare failure mode when memory allocation fails. */
static void
mesh_copy_qinfo(struct mesh_state* mstate, struct query_info** qinfop,
	uint16_t* qflags)
{
	struct regional* region = mstate->s.env->scratch;
	struct query_info* qinfo;

	qinfo = regional_alloc_init(region, &mstate->s.qinfo, sizeof(*qinfo));
	if(!qinfo)
		return;
	qinfo->qname = regional_alloc_init(region, qinfo->qname,
		qinfo->qname_len);
	if(!qinfo->qname)
		return;
	*qinfop = qinfo;
	*qflags = mstate->s.query_flags;
}

/**
 * Continue processing the mesh state at another module.
 * Handles module to modules transfer of control.
 * Handles module finished.
 * @param mesh: the mesh area.
 * @param mstate: currently active mesh state.
 * 	Deleted if finished, calls _done and _supers to
 * 	send replies to clients and inform other mesh states.
 * 	This in turn may create additional runnable mesh states.
 * @param s: state at which the current module exited.
 * @param ev: the event sent to the module.
 * 	returned is the event to send to the next module.
 * @return true if continue processing at the new module.
 * 	false if not continued processing is needed.
 */
static int
mesh_continue(struct mesh_area* mesh, struct mesh_state* mstate,
	enum module_ext_state s, enum module_ev* ev)
{
	mstate->num_activated++;
	if(mstate->num_activated > MESH_MAX_ACTIVATION) {
		/* module is looping. Stop it. */
		log_err("internal error: looping module (%s) stopped",
			mesh->mods.mod[mstate->s.curmod]->name);
		log_query_info(NO_VERBOSE, "pass error for qstate",
			&mstate->s.qinfo);
		s = module_error;
	}
	if(s == module_wait_module || s == module_restart_next) {
		/* start next module */
		mstate->s.curmod++;
		if(mesh->mods.num == mstate->s.curmod) {
			log_err("Cannot pass to next module; at last module");
			log_query_info(VERB_QUERY, "pass error for qstate",
				&mstate->s.qinfo);
			mstate->s.curmod--;
			return mesh_continue(mesh, mstate, module_error, ev);
		}
		if(s == module_restart_next) {
			int curmod = mstate->s.curmod;
			for(; mstate->s.curmod < mesh->mods.num;
				mstate->s.curmod++) {
				fptr_ok(fptr_whitelist_mod_clear(
					mesh->mods.mod[mstate->s.curmod]->clear));
				(*mesh->mods.mod[mstate->s.curmod]->clear)
					(&mstate->s, mstate->s.curmod);
				mstate->s.minfo[mstate->s.curmod] = NULL;
			}
			mstate->s.curmod = curmod;
		}
		*ev = module_event_pass;
		return 1;
	}
	if(s == module_wait_subquery && mstate->sub_set.count == 0) {
		log_err("module cannot wait for subquery, subquery list empty");
		log_query_info(VERB_QUERY, "pass error for qstate",
			&mstate->s.qinfo);
		s = module_error;
	}
	if(s == module_error && mstate->s.return_rcode == LDNS_RCODE_NOERROR) {
		/* error is bad, handle pass back up below */
		mstate->s.return_rcode = LDNS_RCODE_SERVFAIL;
	}
	if(s == module_error) {
		mesh_query_done(mstate);
		mesh_walk_supers(mesh, mstate);
		mesh_state_delete(&mstate->s);
		return 0;
	}
	if(s == module_finished) {
		if(mstate->s.curmod == 0) {
			struct query_info* qinfo = NULL;
			struct edns_option* opt_list = NULL;
			struct sockaddr_storage addr;
			uint16_t qflags;
			int rpz_p = 0;

#ifdef CLIENT_SUBNET
			struct edns_option* ecs;
			if(mstate->s.need_refetch && mstate->reply_list &&
				modstack_find(&mesh->mods, "subnetcache") != -1 &&
				mstate->s.env->unique_mesh) {
				addr = mstate->reply_list->query_reply.client_addr;
			} else
#endif
				memset(&addr, 0, sizeof(addr));

			mesh_query_done(mstate);
			mesh_walk_supers(mesh, mstate);

			/* If the answer to the query needs to be refetched
			 * from an external DNS server, we'll need to schedule
			 * a prefetch after removing the current state, so
			 * we need to make a copy of the query info here. */
			if(mstate->s.need_refetch) {
				mesh_copy_qinfo(mstate, &qinfo, &qflags);
#ifdef CLIENT_SUBNET
				/* Make also a copy of the ecs option if any */
				if((ecs = edns_opt_list_find(
					mstate->s.edns_opts_front_in,
					mstate->s.env->cfg->client_subnet_opcode)) != NULL) {
					(void)edns_opt_list_append(&opt_list,
						ecs->opt_code, ecs->opt_len,
						ecs->opt_data,
						mstate->s.env->scratch);
				}
#endif
				rpz_p = mstate->s.rpz_passthru;
			}

			if(qinfo) {
				mesh_state_delete(&mstate->s);
				mesh_new_prefetch(mesh, qinfo, qflags, 0,
					rpz_p,
					addr.ss_family!=AF_UNSPEC?&addr:NULL,
					opt_list);
			} else {
				mesh_state_delete(&mstate->s);
			}
			return 0;
		}
		/* pass along the locus of control */
		mstate->s.curmod --;
		*ev = module_event_moddone;
		return 1;
	}
	return 0;
}

void mesh_run(struct mesh_area* mesh, struct mesh_state* mstate,
	enum module_ev ev, struct outbound_entry* e)
{
	enum module_ext_state s;
	verbose(VERB_ALGO, "mesh_run: start");
	while(mstate) {
		/* run the module */
		fptr_ok(fptr_whitelist_mod_operate(
			mesh->mods.mod[mstate->s.curmod]->operate));
		(*mesh->mods.mod[mstate->s.curmod]->operate)
			(&mstate->s, ev, mstate->s.curmod, e);

		/* examine results */
		mstate->s.reply = NULL;
		regional_free_all(mstate->s.env->scratch);
		s = mstate->s.ext_state[mstate->s.curmod];
		verbose(VERB_ALGO, "mesh_run: %s module exit state is %s",
			mesh->mods.mod[mstate->s.curmod]->name, strextstate(s));
		e = NULL;
		if(mesh_continue(mesh, mstate, s, &ev))
			continue;

		/* run more modules */
		ev = module_event_pass;
		if(mesh->run.count > 0) {
			/* pop random element off the runnable tree */
			mstate = (struct mesh_state*)mesh->run.root->key;
			(void)rbtree_delete(&mesh->run, mstate);
		} else mstate = NULL;
	}
	if(verbosity >= VERB_ALGO) {
		mesh_stats(mesh, "mesh_run: end");
		mesh_log_list(mesh);
	}
}

void
mesh_log_list(struct mesh_area* mesh)
{
	char buf[30];
	struct mesh_state* m;
	int num = 0;
	RBTREE_FOR(m, struct mesh_state*, &mesh->all) {
		snprintf(buf, sizeof(buf), "%d%s%s%s%s%s%s mod%d %s%s",
			num++, (m->s.is_priming)?"p":"",  /* prime */
			(m->s.is_valrec)?"v":"",  /* prime */
			(m->s.query_flags&BIT_RD)?"RD":"",
			(m->s.query_flags&BIT_CD)?"CD":"",
			(m->super_set.count==0)?"d":"", /* detached */
			(m->sub_set.count!=0)?"c":"",  /* children */
			m->s.curmod, (m->reply_list)?"rep":"", /*hasreply*/
			(m->cb_list)?"cb":"" /* callbacks */
			);
		log_query_info(VERB_ALGO, buf, &m->s.qinfo);
	}
}

void
mesh_stats(struct mesh_area* mesh, const char* str)
{
	verbose(VERB_DETAIL, "%s %u recursion states (%u with reply, "
		"%u detached), %u waiting replies, %u recursion replies "
		"sent, %d replies dropped, %d states jostled out",
		str, (unsigned)mesh->all.count,
		(unsigned)mesh->num_reply_states,
		(unsigned)mesh->num_detached_states,
		(unsigned)mesh->num_reply_addrs,
		(unsigned)mesh->replies_sent,
		(unsigned)mesh->stats_dropped,
		(unsigned)mesh->stats_jostled);
	if(mesh->replies_sent > 0) {
		struct timeval avg;
		timeval_divide(&avg, &mesh->replies_sum_wait,
			mesh->replies_sent);
		log_info("average recursion processing time "
			ARG_LL "d.%6.6d sec",
			(long long)avg.tv_sec, (int)avg.tv_usec);
		log_info("histogram of recursion processing times");
		timehist_log(mesh->histogram, "recursions");
	}
}

void
mesh_stats_clear(struct mesh_area* mesh)
{
	if(!mesh)
		return;
	mesh->num_query_authzone_up = 0;
	mesh->num_query_authzone_down = 0;
	mesh->replies_sent = 0;
	mesh->replies_sum_wait.tv_sec = 0;
	mesh->replies_sum_wait.tv_usec = 0;
	mesh->stats_jostled = 0;
	mesh->stats_dropped = 0;
	timehist_clear(mesh->histogram);
	mesh->ans_secure = 0;
	mesh->ans_bogus = 0;
	mesh->ans_expired = 0;
	mesh->ans_cachedb = 0;
	memset(&mesh->ans_rcode[0], 0, sizeof(size_t)*UB_STATS_RCODE_NUM);
	memset(&mesh->rpz_action[0], 0, sizeof(size_t)*UB_STATS_RPZ_ACTION_NUM);
	mesh->ans_nodata = 0;
	mesh->num_queries_discard_timeout = 0;
	mesh->num_queries_wait_limit = 0;
	mesh->num_dns_error_reports = 0;
}

size_t
mesh_get_mem(struct mesh_area* mesh)
{
	struct mesh_state* m;
	size_t s = sizeof(*mesh) + sizeof(struct timehist) +
		sizeof(struct th_buck)*mesh->histogram->num +
		sizeof(sldns_buffer) + sldns_buffer_capacity(mesh->qbuf_bak);
	RBTREE_FOR(m, struct mesh_state*, &mesh->all) {
		/* all, including m itself allocated in qstate region */
		s += regional_get_mem(m->s.region);
	}
	return s;
}

int
mesh_detect_cycle(struct module_qstate* qstate, struct query_info* qinfo,
	uint16_t flags, int prime, int valrec)
{
	struct mesh_area* mesh = qstate->env->mesh;
	struct mesh_state* dep_m = NULL;
	dep_m = mesh_area_find(mesh, NULL, qinfo, flags, prime, valrec);
	return mesh_detect_cycle_found(qstate, dep_m);
}

void mesh_list_insert(struct mesh_state* m, struct mesh_state** fp,
        struct mesh_state** lp)
{
	/* insert as last element */
	m->prev = *lp;
	m->next = NULL;
	if(*lp)
		(*lp)->next = m;
	else	*fp = m;
	*lp = m;
}

void mesh_list_remove(struct mesh_state* m, struct mesh_state** fp,
        struct mesh_state** lp)
{
	if(m->next)
		m->next->prev = m->prev;
	else	*lp = m->prev;
	if(m->prev)
		m->prev->next = m->next;
	else	*fp = m->next;
}

void mesh_state_remove_reply(struct mesh_area* mesh, struct mesh_state* m,
	struct comm_point* cp)
{
	struct mesh_reply* n, *prev = NULL;
	n = m->reply_list;
	/* when in mesh_cleanup, it sets the reply_list to NULL, so that
	 * there is no accounting twice */
	if(!n) return; /* nothing to remove, also no accounting needed */
	while(n) {
		if(n->query_reply.c == cp) {
			/* unlink it */
			if(prev) prev->next = n->next;
			else m->reply_list = n->next;
			/* delete it, but allocated in m region */
			log_assert(mesh->num_reply_addrs > 0);
			mesh->num_reply_addrs--;
			infra_wait_limit_dec(mesh->env->infra_cache,
				&n->query_reply, mesh->env->cfg);

			/* prev = prev; */
			n = n->next;
			continue;
		}
		prev = n;
		n = n->next;
	}
	/* it was not detached (because it had a reply list), could be now */
	if(!m->reply_list && !m->cb_list
		&& m->super_set.count == 0) {
		mesh->num_detached_states++;
	}
	/* if not replies any more in mstate, it is no longer a reply_state */
	if(!m->reply_list && !m->cb_list) {
		log_assert(mesh->num_reply_states > 0);
		mesh->num_reply_states--;
	}
}


static int
apply_respip_action(struct module_qstate* qstate,
	const struct query_info* qinfo, struct respip_client_info* cinfo,
	struct respip_action_info* actinfo, struct reply_info* rep,
	struct ub_packed_rrset_key** alias_rrset,
	struct reply_info** encode_repp, struct auth_zones* az)
{
	if(qinfo->qtype != LDNS_RR_TYPE_A &&
		qinfo->qtype != LDNS_RR_TYPE_AAAA &&
		qinfo->qtype != LDNS_RR_TYPE_ANY)
		return 1;

	if(!respip_rewrite_reply(qinfo, cinfo, rep, encode_repp, actinfo,
		alias_rrset, 0, qstate->region, az, NULL, qstate->env->views,
		qstate->env->respip_set))
		return 0;

	/* xxx_deny actions mean dropping the reply, unless the original reply
	 * was redirected to response-ip data. */
	if((actinfo->action == respip_deny ||
		actinfo->action == respip_inform_deny) &&
		*encode_repp == rep)
		*encode_repp = NULL;

	return 1;
}

void
mesh_serve_expired_callback(void* arg)
{
	struct mesh_state* mstate = (struct mesh_state*) arg;
	struct module_qstate* qstate = &mstate->s;
	struct mesh_reply* r;
	struct mesh_area* mesh = qstate->env->mesh;
	struct dns_msg* msg;
	struct mesh_cb* c;
	struct mesh_reply* prev = NULL;
	struct sldns_buffer* prev_buffer = NULL;
	struct sldns_buffer* r_buffer = NULL;
	struct reply_info* partial_rep = NULL;
	struct ub_packed_rrset_key* alias_rrset = NULL;
	struct reply_info* encode_rep = NULL;
	struct respip_action_info actinfo;
	struct query_info* lookup_qinfo = &qstate->qinfo;
	struct query_info qinfo_tmp;
	struct timeval tv = {0, 0};
	int must_validate = (!(qstate->query_flags&BIT_CD)
		|| qstate->env->cfg->ignore_cd) && qstate->env->need_to_validate;
	int i = 0, for_count;
	int is_expired;
	if(!qstate->serve_expired_data) return;
	verbose(VERB_ALGO, "Serve expired: Trying to reply with expired data");
	comm_timer_delete(qstate->serve_expired_data->timer);
	qstate->serve_expired_data->timer = NULL;
	/* If is_drop or no_cache_lookup (modules that handle their own cache e.g.,
	 * subnetmod) ignore stale data from the main cache. */
	if(qstate->no_cache_lookup || qstate->is_drop) {
		verbose(VERB_ALGO,
			"Serve expired: Not allowed to look into cache for stale");
		return;
	}
	/* The following for is used instead of the `goto lookup_cache`
	 * like in the worker. This loop should get max 2 passes if we need to
	 * do any aliasing. */
	for(for_count = 0; for_count < 2; for_count++) {
		fptr_ok(fptr_whitelist_serve_expired_lookup(
			qstate->serve_expired_data->get_cached_answer));
		msg = (*qstate->serve_expired_data->get_cached_answer)(qstate,
			lookup_qinfo, &is_expired);
		if(!msg || (FLAGS_GET_RCODE(msg->rep->flags) != LDNS_RCODE_NOERROR
			&& FLAGS_GET_RCODE(msg->rep->flags) != LDNS_RCODE_NXDOMAIN
			&& FLAGS_GET_RCODE(msg->rep->flags) != LDNS_RCODE_YXDOMAIN)) {
			/* We don't care for cached failure answers at this
			 * stage. */
			return;
		}
		/* Reset these in case we pass a second time from here. */
		encode_rep = msg->rep;
		memset(&actinfo, 0, sizeof(actinfo));
		actinfo.action = respip_none;
		alias_rrset = NULL;
		if((mesh->use_response_ip || mesh->use_rpz) &&
			!partial_rep && !apply_respip_action(qstate, &qstate->qinfo,
			qstate->client_info, &actinfo, msg->rep, &alias_rrset, &encode_rep,
			qstate->env->auth_zones)) {
			return;
		} else if(partial_rep &&
			!respip_merge_cname(partial_rep, &qstate->qinfo, msg->rep,
			qstate->client_info, must_validate, &encode_rep, qstate->region,
			qstate->env->auth_zones, qstate->env->views,
			qstate->env->respip_set)) {
			return;
		}
		if(!encode_rep || alias_rrset) {
			if(!encode_rep) {
				/* Needs drop */
				return;
			} else {
				/* A partial CNAME chain is found. */
				partial_rep = encode_rep;
			}
		}
		/* We've found a partial reply ending with an
		* alias.  Replace the lookup qinfo for the
		* alias target and lookup the cache again to
		* (possibly) complete the reply.  As we're
		* passing the "base" reply, there will be no
		* more alias chasing. */
		if(partial_rep) {
			memset(&qinfo_tmp, 0, sizeof(qinfo_tmp));
			get_cname_target(alias_rrset, &qinfo_tmp.qname,
				&qinfo_tmp.qname_len);
			if(!qinfo_tmp.qname) {
				log_err("Serve expired: unexpected: invalid answer alias");
				return;
			}
			qinfo_tmp.qtype = qstate->qinfo.qtype;
			qinfo_tmp.qclass = qstate->qinfo.qclass;
			lookup_qinfo = &qinfo_tmp;
			continue;
		}
		break;
	}

	if(verbosity >= VERB_ALGO)
		log_dns_msg("Serve expired lookup", &qstate->qinfo, msg->rep);

	for(r = mstate->reply_list; r; r = r->next) {
		struct timeval old;
		timeval_subtract(&old, mstate->s.env->now_tv, &r->start_time);
		if(mstate->s.env->cfg->discard_timeout != 0 &&
			((int)old.tv_sec)*1000+((int)old.tv_usec)/1000 >
			mstate->s.env->cfg->discard_timeout) {
			/* Drop the reply, it is too old */
			/* briefly set the reply_list to NULL, so that the
			 * tcp req info cleanup routine that calls the mesh
			 * to deregister the meshstate for it is not done
			 * because the list is NULL and also accounting is not
			 * done there, but instead we do that here. */
			struct mesh_reply* reply_list = mstate->reply_list;
			verbose(VERB_ALGO, "drop reply, it is older than discard-timeout");
			infra_wait_limit_dec(mstate->s.env->infra_cache,
				&r->query_reply, mstate->s.env->cfg);
			mstate->reply_list = NULL;
			if(r->query_reply.c->use_h2)
				http2_stream_remove_mesh_state(r->h2_stream);
			comm_point_drop_reply(&r->query_reply);
			mstate->reply_list = reply_list;
			mstate->s.env->mesh->num_queries_discard_timeout++;
			continue;
		}

		i++;
		tv = r->start_time;

		/* If address info is returned, it means the action should be an
		* 'inform' variant and the information should be logged. */
		if(actinfo.addrinfo) {
			respip_inform_print(&actinfo, r->qname,
				qstate->qinfo.qtype, qstate->qinfo.qclass,
				r->local_alias, &r->query_reply.client_addr,
				r->query_reply.client_addrlen);
		}

		/* Add EDE Stale Answer (RCF8914). Ignore global ede as this is
		 * warning instead of an error */
		if(r->edns.edns_present &&
			qstate->env->cfg->ede_serve_expired &&
			qstate->env->cfg->ede &&
			is_expired) {
			edns_opt_list_append_ede(&r->edns.opt_list_out,
				mstate->s.region, LDNS_EDE_STALE_ANSWER, NULL);
		}

		r_buffer = r->query_reply.c->buffer;
		if(r->query_reply.c->tcp_req_info)
			r_buffer = r->query_reply.c->tcp_req_info->spool_buffer;
		mesh_send_reply(mstate, LDNS_RCODE_NOERROR, msg->rep,
			r, r_buffer, prev, prev_buffer);
		if(r->query_reply.c->tcp_req_info)
			tcp_req_info_remove_mesh_state(r->query_reply.c->tcp_req_info, mstate);
		/* mesh_send_reply removed mesh state from http2_stream. */
		infra_wait_limit_dec(mstate->s.env->infra_cache,
			&r->query_reply, mstate->s.env->cfg);
		prev = r;
		prev_buffer = r_buffer;
	}
	/* Account for each reply sent. */
	if(i > 0) {
		mesh->ans_expired += i;
		if(actinfo.addrinfo && qstate->env->cfg->stat_extended &&
			actinfo.rpz_used) {
			if(actinfo.rpz_disabled)
				qstate->env->mesh->rpz_action[RPZ_DISABLED_ACTION] += i;
			if(actinfo.rpz_cname_override)
				qstate->env->mesh->rpz_action[RPZ_CNAME_OVERRIDE_ACTION] += i;
			else
				qstate->env->mesh->rpz_action[
					respip_action_to_rpz_action(actinfo.action)] += i;
		}
	}

	/* Mesh area accounting */
	if(mstate->reply_list) {
		mstate->reply_list = NULL;
		if(!mstate->reply_list && !mstate->cb_list) {
			log_assert(mesh->num_reply_states > 0);
			mesh->num_reply_states--;
			if(mstate->super_set.count == 0) {
				mesh->num_detached_states++;
			}
		}
	}

	while((c = mstate->cb_list) != NULL) {
		/* take this cb off the list; so that the list can be
		 * changed, eg. by adds from the callback routine */
		if(!mstate->reply_list && mstate->cb_list && !c->next) {
			/* was a reply state, not anymore */
			log_assert(qstate->env->mesh->num_reply_states > 0);
			qstate->env->mesh->num_reply_states--;
		}
		mstate->cb_list = c->next;
		if(!mstate->reply_list && !mstate->cb_list &&
			mstate->super_set.count == 0)
			qstate->env->mesh->num_detached_states++;
		mesh_do_callback(mstate, LDNS_RCODE_NOERROR, msg->rep, c, &tv);
	}
}

void
mesh_respond_serve_expired(struct mesh_state* mstate)
{
	if(!mstate->s.serve_expired_data)
		mesh_serve_expired_init(mstate, -1);
	mesh_serve_expired_callback(mstate);
}

int mesh_jostle_exceeded(struct mesh_area* mesh)
{
	if(mesh->all.count < mesh->max_reply_states)
		return 0;
	return 1;
}

void mesh_remove_callback(struct mesh_area* mesh, struct query_info* qinfo,
	uint16_t qflags, mesh_cb_func_type cb, void* cb_arg)
{
	struct mesh_state* s = NULL;
	s = mesh_area_find(mesh, NULL, qinfo, qflags&(BIT_RD|BIT_CD), 0, 0);
	if(!s) return;
	if(!mesh_state_del_cb(s, cb, cb_arg)) return;

	/* It was in the list and removed. */
	log_assert(mesh->num_reply_addrs > 0);
	mesh->num_reply_addrs--;
	if(!s->reply_list && !s->cb_list) {
		/* was a reply state, not anymore */
		log_assert(mesh->num_reply_states > 0);
		mesh->num_reply_states--;
	}
	if(!s->reply_list && !s->cb_list &&
		s->super_set.count == 0) {
		mesh->num_detached_states++;
	}
}
