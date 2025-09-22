/*
 * iterator/iterator.c - iterative resolver DNS query response module
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
 * This file contains a module that performs recursive iterative DNS query
 * processing.
 */

#include "config.h"
#include "iterator/iterator.h"
#include "iterator/iter_utils.h"
#include "iterator/iter_hints.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_donotq.h"
#include "iterator/iter_delegpt.h"
#include "iterator/iter_resptype.h"
#include "iterator/iter_scrub.h"
#include "iterator/iter_priv.h"
#include "validator/val_neg.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/authzone.h"
#include "util/module.h"
#include "util/netevent.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/data/dname.h"
#include "util/data/msgencode.h"
#include "util/fptr_wlist.h"
#include "util/config_file.h"
#include "util/random.h"
#include "sldns/rrdef.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"
#include "sldns/parseutil.h"
#include "sldns/sbuffer.h"

/* number of packets */
int MAX_GLOBAL_QUOTA = 200;
/* in msec */
int UNKNOWN_SERVER_NICENESS = 376;
/* in msec */
int USEFUL_SERVER_TOP_TIMEOUT = 120000;
/* Equals USEFUL_SERVER_TOP_TIMEOUT*4 */
int BLACKLIST_PENALTY = (120000*4);
/** Timeout when only a single probe query per IP is allowed. */
int PROBE_MAXRTO = PROBE_MAXRTO_DEFAULT; /* in msec */

static void target_count_increase_nx(struct iter_qstate* iq, int num);

int 
iter_init(struct module_env* env, int id)
{
	struct iter_env* iter_env = (struct iter_env*)calloc(1,
		sizeof(struct iter_env));
	if(!iter_env) {
		log_err("malloc failure");
		return 0;
	}
	env->modinfo[id] = (void*)iter_env;

	lock_basic_init(&iter_env->queries_ratelimit_lock);
	lock_protect(&iter_env->queries_ratelimit_lock,
			&iter_env->num_queries_ratelimited,
		sizeof(iter_env->num_queries_ratelimited));

	if(!iter_apply_cfg(iter_env, env->cfg)) {
		log_err("iterator: could not apply configuration settings.");
		return 0;
	}

	return 1;
}

void 
iter_deinit(struct module_env* env, int id)
{
	struct iter_env* iter_env;
	if(!env || !env->modinfo[id])
		return;
	iter_env = (struct iter_env*)env->modinfo[id];
	lock_basic_destroy(&iter_env->queries_ratelimit_lock);
	free(iter_env->target_fetch_policy);
	priv_delete(iter_env->priv);
	donotq_delete(iter_env->donotq);
	caps_white_delete(iter_env->caps_white);
	free(iter_env);
	env->modinfo[id] = NULL;
}

/** new query for iterator */
static int
iter_new(struct module_qstate* qstate, int id)
{
	struct iter_qstate* iq = (struct iter_qstate*)regional_alloc(
		qstate->region, sizeof(struct iter_qstate));
	qstate->minfo[id] = iq;
	if(!iq) 
		return 0;
	memset(iq, 0, sizeof(*iq));
	iq->state = INIT_REQUEST_STATE;
	iq->final_state = FINISHED_STATE;
	iq->an_prepend_list = NULL;
	iq->an_prepend_last = NULL;
	iq->ns_prepend_list = NULL;
	iq->ns_prepend_last = NULL;
	iq->dp = NULL;
	iq->depth = 0;
	iq->num_target_queries = 0;
	iq->num_current_queries = 0;
	iq->query_restart_count = 0;
	iq->referral_count = 0;
	iq->sent_count = 0;
	iq->ratelimit_ok = 0;
	iq->target_count = NULL;
	iq->dp_target_count = 0;
	iq->wait_priming_stub = 0;
	iq->refetch_glue = 0;
	iq->dnssec_expected = 0;
	iq->dnssec_lame_query = 0;
	iq->chase_flags = qstate->query_flags;
	/* Start with the (current) qname. */
	iq->qchase = qstate->qinfo;
	outbound_list_init(&iq->outlist);
	iq->minimise_count = 0;
	iq->timeout_count = 0;
	if (qstate->env->cfg->qname_minimisation)
		iq->minimisation_state = INIT_MINIMISE_STATE;
	else
		iq->minimisation_state = DONOT_MINIMISE_STATE;
	
	memset(&iq->qinfo_out, 0, sizeof(struct query_info));
	return 1;
}

/**
 * Transition to the next state. This can be used to advance a currently
 * processing event. It cannot be used to reactivate a forEvent.
 *
 * @param iq: iterator query state
 * @param nextstate The state to transition to.
 * @return true. This is so this can be called as the return value for the
 *         actual process*State() methods. (Transitioning to the next state
 *         implies further processing).
 */
static int
next_state(struct iter_qstate* iq, enum iter_state nextstate)
{
	/* If transitioning to a "response" state, make sure that there is a
	 * response */
	if(iter_state_is_responsestate(nextstate)) {
		if(iq->response == NULL) {
			log_err("transitioning to response state sans "
				"response.");
		}
	}
	iq->state = nextstate;
	return 1;
}

/**
 * Transition an event to its final state. Final states always either return
 * a result up the module chain, or reactivate a dependent event. Which
 * final state to transition to is set in the module state for the event when
 * it was created, and depends on the original purpose of the event.
 *
 * The response is stored in the qstate->buf buffer.
 *
 * @param iq: iterator query state
 * @return false. This is so this method can be used as the return value for
 *         the processState methods. (Transitioning to the final state
 */
static int
final_state(struct iter_qstate* iq)
{
	return next_state(iq, iq->final_state);
}

/**
 * Callback routine to handle errors in parent query states
 * @param qstate: query state that failed.
 * @param id: module id.
 * @param super: super state.
 */
static void
error_supers(struct module_qstate* qstate, int id, struct module_qstate* super)
{
	struct iter_env* ie = (struct iter_env*)qstate->env->modinfo[id];
	struct iter_qstate* super_iq = (struct iter_qstate*)super->minfo[id];

	if(qstate->qinfo.qtype == LDNS_RR_TYPE_A ||
		qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA) {
		/* mark address as failed. */
		struct delegpt_ns* dpns = NULL;
		super_iq->num_target_queries--; 
		if(super_iq->dp)
			dpns = delegpt_find_ns(super_iq->dp, 
				qstate->qinfo.qname, qstate->qinfo.qname_len);
		if(!dpns) {
			/* not interested */
			/* this can happen, for eg. qname minimisation asked
			 * for an NXDOMAIN to be validated, and used qtype
			 * A for that, and the error of that, the name, is
			 * not listed in super_iq->dp */
			verbose(VERB_ALGO, "subq error, but not interested");
			log_query_info(VERB_ALGO, "superq", &super->qinfo);
			return;
		} else {
			/* see if the failure did get (parent-lame) info */
			if(!cache_fill_missing(super->env, super_iq->qchase.qclass,
				super->region, super_iq->dp, 0))
				log_err("out of memory adding missing");
		}
		delegpt_mark_neg(dpns, qstate->qinfo.qtype);
		if((dpns->got4 == 2 || (!ie->supports_ipv4 && !ie->nat64.use_nat64)) &&
			(dpns->got6 == 2 || !ie->supports_ipv6)) {
			dpns->resolved = 1; /* mark as failed */
			target_count_increase_nx(super_iq, 1);
		}
	}
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_NS) {
		/* prime failed to get delegation */
		super_iq->dp = NULL;
	}
	/* evaluate targets again */
	super_iq->state = QUERYTARGETS_STATE; 
	/* super becomes runnable, and will process this change */
}

/**
 * Return an error to the client
 * @param qstate: our query state
 * @param id: module id
 * @param rcode: error code (DNS errcode).
 * @return: 0 for use by caller, to make notation easy, like:
 * 	return error_response(..). 
 */
static int
error_response(struct module_qstate* qstate, int id, int rcode)
{
	verbose(VERB_QUERY, "return error response %s", 
		sldns_lookup_by_id(sldns_rcodes, rcode)?
		sldns_lookup_by_id(sldns_rcodes, rcode)->name:"??");
	qstate->return_rcode = rcode;
	qstate->return_msg = NULL;
	qstate->ext_state[id] = module_finished;
	return 0;
}

/**
 * Return an error to the client and cache the error code in the
 * message cache (so per qname, qtype, qclass).
 * @param qstate: our query state
 * @param id: module id
 * @param rcode: error code (DNS errcode).
 * @return: 0 for use by caller, to make notation easy, like:
 * 	return error_response(..). 
 */
static int
error_response_cache(struct module_qstate* qstate, int id, int rcode)
{
	struct reply_info err;
	struct msgreply_entry* msg;
	if(qstate->no_cache_store) {
		return error_response(qstate, id, rcode);
	}
	if(qstate->prefetch_leeway > NORR_TTL) {
		verbose(VERB_ALGO, "error response for prefetch in cache");
		/* attempt to adjust the cache entry prefetch */
		if(dns_cache_prefetch_adjust(qstate->env, &qstate->qinfo,
			NORR_TTL, qstate->query_flags))
			return error_response(qstate, id, rcode);
		/* if that fails (not in cache), fall through to store err */
	}
	if((msg=msg_cache_lookup(qstate->env,
		qstate->qinfo.qname, qstate->qinfo.qname_len,
		qstate->qinfo.qtype, qstate->qinfo.qclass,
		qstate->query_flags, 0,
		qstate->env->cfg->serve_expired)) != NULL) {
		struct reply_info* rep = (struct reply_info*)msg->entry.data;
		if(qstate->env->cfg->serve_expired && rep) {
			if(qstate->env->cfg->serve_expired_ttl_reset &&
				*qstate->env->now + qstate->env->cfg->serve_expired_ttl
				> rep->serve_expired_ttl) {
				verbose(VERB_ALGO, "reset serve-expired-ttl for "
					"response in cache");
				rep->serve_expired_ttl = *qstate->env->now +
					qstate->env->cfg->serve_expired_ttl;
			}
			verbose(VERB_ALGO, "set serve-expired-norec-ttl for "
				"response in cache");
			rep->serve_expired_norec_ttl = NORR_TTL +
				*qstate->env->now;
		}
		if(rep && (FLAGS_GET_RCODE(rep->flags) ==
			LDNS_RCODE_NOERROR ||
			FLAGS_GET_RCODE(rep->flags) ==
			LDNS_RCODE_NXDOMAIN ||
			FLAGS_GET_RCODE(rep->flags) ==
			LDNS_RCODE_YXDOMAIN) &&
			(qstate->env->cfg->serve_expired ||
			*qstate->env->now <= rep->ttl)) {
			/* we have a good entry, don't overwrite */
			lock_rw_unlock(&msg->entry.lock);
			return error_response(qstate, id, rcode);
		}
		lock_rw_unlock(&msg->entry.lock);
		/* nothing interesting is cached (already error response or
		 * expired good record when we don't serve expired), so this
		 * servfail cache entry is useful (stops waste of time on this
		 * servfail NORR_TTL) */
	}
	/* store in cache */
	memset(&err, 0, sizeof(err));
	err.flags = (uint16_t)(BIT_QR | BIT_RA);
	FLAGS_SET_RCODE(err.flags, rcode);
	err.qdcount = 1;
	err.ttl = NORR_TTL;
	err.prefetch_ttl = PREFETCH_TTL_CALC(err.ttl);
	err.serve_expired_ttl = NORR_TTL;
	/* do not waste time trying to validate this servfail */
	err.security = sec_status_indeterminate;
	verbose(VERB_ALGO, "store error response in message cache");
	iter_dns_store(qstate->env, &qstate->qinfo, &err, 0, 0, 0, NULL,
		qstate->query_flags, qstate->qstarttime, qstate->is_valrec);
	return error_response(qstate, id, rcode);
}

/** check if prepend item is duplicate item */
static int
prepend_is_duplicate(struct ub_packed_rrset_key** sets, size_t to,
	struct ub_packed_rrset_key* dup)
{
	size_t i;
	for(i=0; i<to; i++) {
		if(sets[i]->rk.type == dup->rk.type &&
			sets[i]->rk.rrset_class == dup->rk.rrset_class &&
			sets[i]->rk.dname_len == dup->rk.dname_len &&
			query_dname_compare(sets[i]->rk.dname, dup->rk.dname)
			== 0)
			return 1;
	}
	return 0;
}

/** prepend the prepend list in the answer and authority section of dns_msg */
static int
iter_prepend(struct iter_qstate* iq, struct dns_msg* msg, 
	struct regional* region)
{
	struct iter_prep_list* p;
	struct ub_packed_rrset_key** sets;
	size_t num_an = 0, num_ns = 0;;
	for(p = iq->an_prepend_list; p; p = p->next)
		num_an++;
	for(p = iq->ns_prepend_list; p; p = p->next)
		num_ns++;
	if(num_an + num_ns == 0)
		return 1;
	verbose(VERB_ALGO, "prepending %d rrsets", (int)num_an + (int)num_ns);
	if(num_an > RR_COUNT_MAX || num_ns > RR_COUNT_MAX ||
		msg->rep->rrset_count > RR_COUNT_MAX) return 0; /* overflow */
	sets = regional_alloc(region, (num_an+num_ns+msg->rep->rrset_count) *
		sizeof(struct ub_packed_rrset_key*));
	if(!sets) 
		return 0;
	/* ANSWER section */
	num_an = 0;
	for(p = iq->an_prepend_list; p; p = p->next) {
		sets[num_an++] = p->rrset;
		if(ub_packed_rrset_ttl(p->rrset) < msg->rep->ttl) {
			msg->rep->ttl = ub_packed_rrset_ttl(p->rrset);
			msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
			msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
		}
	}
	memcpy(sets+num_an, msg->rep->rrsets, msg->rep->an_numrrsets *
		sizeof(struct ub_packed_rrset_key*));
	/* AUTH section */
	num_ns = 0;
	for(p = iq->ns_prepend_list; p; p = p->next) {
		if(prepend_is_duplicate(sets+msg->rep->an_numrrsets+num_an,
			num_ns, p->rrset) || prepend_is_duplicate(
			msg->rep->rrsets+msg->rep->an_numrrsets, 
			msg->rep->ns_numrrsets, p->rrset))
			continue;
		sets[msg->rep->an_numrrsets + num_an + num_ns++] = p->rrset;
		if(ub_packed_rrset_ttl(p->rrset) < msg->rep->ttl) {
			msg->rep->ttl = ub_packed_rrset_ttl(p->rrset);
			msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
			msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
		}
	}
	memcpy(sets + num_an + msg->rep->an_numrrsets + num_ns, 
		msg->rep->rrsets + msg->rep->an_numrrsets, 
		(msg->rep->ns_numrrsets + msg->rep->ar_numrrsets) *
		sizeof(struct ub_packed_rrset_key*));

	/* NXDOMAIN rcode can stay if we prepended DNAME/CNAMEs, because
	 * this is what recursors should give. */
	msg->rep->rrset_count += num_an + num_ns;
	msg->rep->an_numrrsets += num_an;
	msg->rep->ns_numrrsets += num_ns;
	msg->rep->rrsets = sets;
	return 1;
}

/**
 * Find rrset in ANSWER prepend list.
 * to avoid duplicate DNAMEs when a DNAME is traversed twice.
 * @param iq: iterator query state.
 * @param rrset: rrset to add.
 * @return false if not found
 */
static int
iter_find_rrset_in_prepend_answer(struct iter_qstate* iq,
	struct ub_packed_rrset_key* rrset)
{
	struct iter_prep_list* p = iq->an_prepend_list;
	while(p) {
		if(ub_rrset_compare(p->rrset, rrset) == 0 &&
			rrsetdata_equal((struct packed_rrset_data*)p->rrset
			->entry.data, (struct packed_rrset_data*)rrset
			->entry.data))
			return 1;
		p = p->next;
	}
	return 0;
}

/**
 * Add rrset to ANSWER prepend list
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param rrset: rrset to add.
 * @return false on failure (malloc).
 */
static int
iter_add_prepend_answer(struct module_qstate* qstate, struct iter_qstate* iq,
	struct ub_packed_rrset_key* rrset)
{
	struct iter_prep_list* p = (struct iter_prep_list*)regional_alloc(
		qstate->region, sizeof(struct iter_prep_list));
	if(!p)
		return 0;
	p->rrset = rrset;
	p->next = NULL;
	/* add at end */
	if(iq->an_prepend_last)
		iq->an_prepend_last->next = p;
	else	iq->an_prepend_list = p;
	iq->an_prepend_last = p;
	return 1;
}

/**
 * Add rrset to AUTHORITY prepend list
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param rrset: rrset to add.
 * @return false on failure (malloc).
 */
static int
iter_add_prepend_auth(struct module_qstate* qstate, struct iter_qstate* iq,
	struct ub_packed_rrset_key* rrset)
{
	struct iter_prep_list* p = (struct iter_prep_list*)regional_alloc(
		qstate->region, sizeof(struct iter_prep_list));
	if(!p)
		return 0;
	p->rrset = rrset;
	p->next = NULL;
	/* add at end */
	if(iq->ns_prepend_last)
		iq->ns_prepend_last->next = p;
	else	iq->ns_prepend_list = p;
	iq->ns_prepend_last = p;
	return 1;
}

/**
 * Given a CNAME response (defined as a response containing a CNAME or DNAME
 * that does not answer the request), process the response, modifying the
 * state as necessary. This follows the CNAME/DNAME chain and returns the
 * final query name.
 *
 * sets the new query name, after following the CNAME/DNAME chain.
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param msg: the response.
 * @param mname: returned target new query name.
 * @param mname_len: length of mname.
 * @return false on (malloc) error.
 */
static int
handle_cname_response(struct module_qstate* qstate, struct iter_qstate* iq,
        struct dns_msg* msg, uint8_t** mname, size_t* mname_len)
{
	size_t i;
	/* Start with the (current) qname. */
	*mname = iq->qchase.qname;
	*mname_len = iq->qchase.qname_len;

	/* Iterate over the ANSWER rrsets in order, looking for CNAMEs and 
	 * DNAMES. */
	for(i=0; i<msg->rep->an_numrrsets; i++) {
		struct ub_packed_rrset_key* r = msg->rep->rrsets[i];
		/* If there is a (relevant) DNAME, add it to the list.
		 * We always expect there to be CNAME that was generated 
		 * by this DNAME following, so we don't process the DNAME 
		 * directly.  */
		if(ntohs(r->rk.type) == LDNS_RR_TYPE_DNAME &&
			dname_strict_subdomain_c(*mname, r->rk.dname) &&
			!iter_find_rrset_in_prepend_answer(iq, r)) {
			if(!iter_add_prepend_answer(qstate, iq, r))
				return 0;
			continue;
		}

		if(ntohs(r->rk.type) == LDNS_RR_TYPE_CNAME &&
			query_dname_compare(*mname, r->rk.dname) == 0 &&
			!iter_find_rrset_in_prepend_answer(iq, r)) {
			/* Add this relevant CNAME rrset to the prepend list.*/
			if(!iter_add_prepend_answer(qstate, iq, r))
				return 0;
			get_cname_target(r, mname, mname_len);
		}

		/* Other rrsets in the section are ignored. */
	}
	/* add authority rrsets to authority prepend, for wildcarded CNAMEs */
	for(i=msg->rep->an_numrrsets; i<msg->rep->an_numrrsets +
		msg->rep->ns_numrrsets; i++) {
		struct ub_packed_rrset_key* r = msg->rep->rrsets[i];
		/* only add NSEC/NSEC3, as they may be needed for validation */
		if(ntohs(r->rk.type) == LDNS_RR_TYPE_NSEC ||
			ntohs(r->rk.type) == LDNS_RR_TYPE_NSEC3) {
			if(!iter_add_prepend_auth(qstate, iq, r))
				return 0;
		}
	}
	return 1;
}

/** fill fail address for later recovery */
static void
fill_fail_addr(struct iter_qstate* iq, struct sockaddr_storage* addr,
	socklen_t addrlen)
{
	if(addrlen == 0) {
		iq->fail_addr_type = 0;
		return;
	}
	if(((struct sockaddr_in*)addr)->sin_family == AF_INET) {
		iq->fail_addr_type = 4;
		memcpy(&iq->fail_addr.in,
			&((struct sockaddr_in*)addr)->sin_addr,
			sizeof(iq->fail_addr.in));
	}
#ifdef AF_INET6
	else if(((struct sockaddr_in*)addr)->sin_family == AF_INET6) {
		iq->fail_addr_type = 6;
		memcpy(&iq->fail_addr.in6,
			&((struct sockaddr_in6*)addr)->sin6_addr,
			sizeof(iq->fail_addr.in6));
	}
#endif
	else {
		iq->fail_addr_type = 0;
	}
}

/** print fail addr to string */
static void
print_fail_addr(struct iter_qstate* iq, char* buf, size_t len)
{
	if(iq->fail_addr_type == 4) {
		if(inet_ntop(AF_INET, &iq->fail_addr.in, buf,
			(socklen_t)len) == 0)
			(void)strlcpy(buf, "(inet_ntop error)", len);
	}
#ifdef AF_INET6
	else if(iq->fail_addr_type == 6) {
		if(inet_ntop(AF_INET6, &iq->fail_addr.in6, buf,
			(socklen_t)len) == 0)
			(void)strlcpy(buf, "(inet_ntop error)", len);
	}
#endif
	else
		(void)strlcpy(buf, "", len);
}

/** add response specific error information for log servfail */
static void
errinf_reply(struct module_qstate* qstate, struct iter_qstate* iq)
{
	if(qstate->env->cfg->val_log_level < 2 && !qstate->env->cfg->log_servfail)
		return;
	if((qstate->reply && qstate->reply->remote_addrlen != 0) ||
		(iq->fail_addr_type != 0)) {
		char from[256], frm[512];
		if(qstate->reply && qstate->reply->remote_addrlen != 0)
			addr_to_str(&qstate->reply->remote_addr,
				qstate->reply->remote_addrlen, from,
				sizeof(from));
		else
			print_fail_addr(iq, from, sizeof(from));
		snprintf(frm, sizeof(frm), "from %s", from);
		errinf(qstate, frm);
	}
	if(iq->scrub_failures || iq->parse_failures) {
		if(iq->scrub_failures)
			errinf(qstate, "upstream response failed scrub");
		if(iq->parse_failures)
			errinf(qstate, "could not parse upstream response");
	} else if(iq->response == NULL && iq->timeout_count != 0) {
		errinf(qstate, "upstream server timeout");
	} else if(iq->response == NULL) {
		errinf(qstate, "no server to query");
		if(iq->dp) {
			if(iq->dp->target_list == NULL)
				errinf(qstate, "no addresses for nameservers");
			else	errinf(qstate, "nameserver addresses not usable");
			if(iq->dp->nslist == NULL)
				errinf(qstate, "have no nameserver names");
			if(iq->dp->bogus)
				errinf(qstate, "NS record was dnssec bogus");
		}
	}
	if(iq->response && iq->response->rep) {
		if(FLAGS_GET_RCODE(iq->response->rep->flags) != 0) {
			char rcode[256], rc[32];
			(void)sldns_wire2str_rcode_buf(
				FLAGS_GET_RCODE(iq->response->rep->flags),
				rc, sizeof(rc));
			snprintf(rcode, sizeof(rcode), "got %s", rc);
			errinf(qstate, rcode);
		} else {
			/* rcode NOERROR */
			if(iq->response->rep->an_numrrsets == 0) {
				errinf(qstate, "nodata answer");
			}
		}
	}
}

/** see if last resort is possible - does config allow queries to parent */
static int
can_have_last_resort(struct module_env* env, uint8_t* nm, size_t ATTR_UNUSED(nmlen),
	uint16_t qclass, int* have_dp, struct delegpt** retdp,
	struct regional* region)
{
	struct delegpt* dp = NULL;
	int nolock = 0;
	/* do not process a last resort (the parent side) if a stub
	 * or forward is configured, because we do not want to go 'above'
	 * the configured servers */
	if(!dname_is_root(nm) &&
		(dp = hints_find(env->hints, nm, qclass, nolock)) &&
		/* has_parent side is turned off for stub_first, where we
		 * are allowed to go to the parent */
		dp->has_parent_side_NS) {
		if(retdp) *retdp = delegpt_copy(dp, region);
		lock_rw_unlock(&env->hints->lock);
		if(have_dp) *have_dp = 1;
		return 0;
	}
	if(dp) {
		lock_rw_unlock(&env->hints->lock);
		dp = NULL;
	}
	if((dp = forwards_find(env->fwds, nm, qclass, nolock)) &&
		/* has_parent_side is turned off for forward_first, where
		 * we are allowed to go to the parent */
		dp->has_parent_side_NS) {
		if(retdp) *retdp = delegpt_copy(dp, region);
		lock_rw_unlock(&env->fwds->lock);
		if(have_dp) *have_dp = 1;
		return 0;
	}
	/* lock_() calls are macros that could be nothing, surround in {} */
	if(dp) { lock_rw_unlock(&env->fwds->lock); }
	return 1;
}

/** see if target name is caps-for-id whitelisted */
static int
is_caps_whitelisted(struct iter_env* ie, struct iter_qstate* iq)
{
	if(!ie->caps_white) return 0; /* no whitelist, or no capsforid */
	return name_tree_lookup(ie->caps_white, iq->qchase.qname,
		iq->qchase.qname_len, dname_count_labels(iq->qchase.qname),
		iq->qchase.qclass) != NULL;
}

/**
 * Create target count structure for this query. This is always explicitly
 * created for the parent query.
 */
static void
target_count_create(struct iter_qstate* iq)
{
	if(!iq->target_count) {
		iq->target_count = (int*)calloc(TARGET_COUNT_MAX, sizeof(int));
		/* if calloc fails we simply do not track this number */
		if(iq->target_count) {
			iq->target_count[TARGET_COUNT_REF] = 1;
			iq->nxns_dp = (uint8_t**)calloc(1, sizeof(uint8_t*));
		}
	}
}

static void
target_count_increase(struct iter_qstate* iq, int num)
{
	target_count_create(iq);
	if(iq->target_count)
		iq->target_count[TARGET_COUNT_QUERIES] += num;
	iq->dp_target_count++;
}

static void
target_count_increase_nx(struct iter_qstate* iq, int num)
{
	target_count_create(iq);
	if(iq->target_count)
		iq->target_count[TARGET_COUNT_NX] += num;
}

static void
target_count_increase_global_quota(struct iter_qstate* iq, int num)
{
	target_count_create(iq);
	if(iq->target_count)
		iq->target_count[TARGET_COUNT_GLOBAL_QUOTA] += num;
}

/**
 * Generate a subrequest.
 * Generate a local request event. Local events are tied to this module, and
 * have a corresponding (first tier) event that is waiting for this event to
 * resolve to continue.
 *
 * @param qname The query name for this request.
 * @param qnamelen length of qname
 * @param qtype The query type for this request.
 * @param qclass The query class for this request.
 * @param qstate The event that is generating this event.
 * @param id: module id.
 * @param iq: The iterator state that is generating this event.
 * @param initial_state The initial response state (normally this
 *          is QUERY_RESP_STATE, unless it is known that the request won't
 *          need iterative processing
 * @param finalstate The final state for the response to this request.
 * @param subq_ret: if newly allocated, the subquerystate, or NULL if it does
 * 	not need initialisation.
 * @param v: if true, validation is done on the subquery.
 * @param detached: true if this qstate should not attach to the subquery
 * @return false on error (malloc).
 */
static int
generate_sub_request(uint8_t* qname, size_t qnamelen, uint16_t qtype, 
	uint16_t qclass, struct module_qstate* qstate, int id,
	struct iter_qstate* iq, enum iter_state initial_state, 
	enum iter_state finalstate, struct module_qstate** subq_ret, int v,
	int detached)
{
	struct module_qstate* subq = NULL;
	struct iter_qstate* subiq = NULL;
	uint16_t qflags = 0; /* OPCODE QUERY, no flags */
	struct query_info qinf;
	int prime = (finalstate == PRIME_RESP_STATE)?1:0;
	int valrec = 0;
	qinf.qname = qname;
	qinf.qname_len = qnamelen;
	qinf.qtype = qtype;
	qinf.qclass = qclass;
	qinf.local_alias = NULL;

	/* RD should be set only when sending the query back through the INIT
	 * state. */
	if(initial_state == INIT_REQUEST_STATE)
		qflags |= BIT_RD;
	/* We set the CD flag so we can send this through the "head" of 
	 * the resolution chain, which might have a validator. We are 
	 * uninterested in validating things not on the direct resolution 
	 * path.  */
	if(!v) {
		qflags |= BIT_CD;
		valrec = 1;
	}
	
	if(detached) {
		struct mesh_state* sub = NULL;
		fptr_ok(fptr_whitelist_modenv_add_sub(
			qstate->env->add_sub));
		if(!(*qstate->env->add_sub)(qstate, &qinf,
			qflags, prime, valrec, &subq, &sub)){
			return 0;
		}
	}
	else {
		/* attach subquery, lookup existing or make a new one */
		fptr_ok(fptr_whitelist_modenv_attach_sub(
			qstate->env->attach_sub));
		if(!(*qstate->env->attach_sub)(qstate, &qinf, qflags, prime,
			valrec, &subq)) {
			return 0;
		}
	}
	*subq_ret = subq;
	if(subq) {
		/* initialise the new subquery */
		subq->curmod = id;
		subq->ext_state[id] = module_state_initial;
		subq->minfo[id] = regional_alloc(subq->region, 
			sizeof(struct iter_qstate));
		if(!subq->minfo[id]) {
			log_err("init subq: out of memory");
			fptr_ok(fptr_whitelist_modenv_kill_sub(
				qstate->env->kill_sub));
			(*qstate->env->kill_sub)(subq);
			return 0;
		}
		subiq = (struct iter_qstate*)subq->minfo[id];
		memset(subiq, 0, sizeof(*subiq));
		subiq->num_target_queries = 0;
		target_count_create(iq);
		subiq->target_count = iq->target_count;
		if(iq->target_count) {
			iq->target_count[TARGET_COUNT_REF] ++; /* extra reference */
			subiq->nxns_dp = iq->nxns_dp;
		}
		subiq->dp_target_count = 0;
		subiq->num_current_queries = 0;
		subiq->depth = iq->depth+1;
		outbound_list_init(&subiq->outlist);
		subiq->state = initial_state;
		subiq->final_state = finalstate;
		subiq->qchase = subq->qinfo;
		subiq->chase_flags = subq->query_flags;
		subiq->refetch_glue = 0;
		if(qstate->env->cfg->qname_minimisation)
			subiq->minimisation_state = INIT_MINIMISE_STATE;
		else
			subiq->minimisation_state = DONOT_MINIMISE_STATE;
		memset(&subiq->qinfo_out, 0, sizeof(struct query_info));
	}
	return 1;
}

/**
 * Generate and send a root priming request.
 * @param qstate: the qtstate that triggered the need to prime.
 * @param iq: iterator query state.
 * @param id: module id.
 * @param qclass: the class to prime.
 * @return 0 on failure
 */
static int
prime_root(struct module_qstate* qstate, struct iter_qstate* iq, int id,
	uint16_t qclass)
{
	struct delegpt* dp;
	struct module_qstate* subq;
	int nolock = 0;
	verbose(VERB_DETAIL, "priming . %s NS", 
		sldns_lookup_by_id(sldns_rr_classes, (int)qclass)?
		sldns_lookup_by_id(sldns_rr_classes, (int)qclass)->name:"??");
	dp = hints_find_root(qstate->env->hints, qclass, nolock);
	if(!dp) {
		verbose(VERB_ALGO, "Cannot prime due to lack of hints");
		return 0;
	}
	/* Priming requests start at the QUERYTARGETS state, skipping 
	 * the normal INIT state logic (which would cause an infloop). */
	if(!generate_sub_request((uint8_t*)"\000", 1, LDNS_RR_TYPE_NS, 
		qclass, qstate, id, iq, QUERYTARGETS_STATE, PRIME_RESP_STATE,
		&subq, 0, 0)) {
		lock_rw_unlock(&qstate->env->hints->lock);
		verbose(VERB_ALGO, "could not prime root");
		return 0;
	}
	if(subq) {
		struct iter_qstate* subiq = 
			(struct iter_qstate*)subq->minfo[id];
		/* Set the initial delegation point to the hint.
		 * copy dp, it is now part of the root prime query. 
		 * dp was part of in the fixed hints structure. */
		subiq->dp = delegpt_copy(dp, subq->region);
		lock_rw_unlock(&qstate->env->hints->lock);
		if(!subiq->dp) {
			log_err("out of memory priming root, copydp");
			fptr_ok(fptr_whitelist_modenv_kill_sub(
				qstate->env->kill_sub));
			(*qstate->env->kill_sub)(subq);
			return 0;
		}
		/* there should not be any target queries. */
		subiq->num_target_queries = 0; 
		subiq->dnssec_expected = iter_indicates_dnssec(
			qstate->env, subiq->dp, NULL, subq->qinfo.qclass);
	} else {
		lock_rw_unlock(&qstate->env->hints->lock);
	}
	
	/* this module stops, our submodule starts, and does the query. */
	qstate->ext_state[id] = module_wait_subquery;
	return 1;
}

/**
 * Generate and process a stub priming request. This method tests for the
 * need to prime a stub zone, so it is safe to call for every request.
 *
 * @param qstate: the qtstate that triggered the need to prime.
 * @param iq: iterator query state.
 * @param id: module id.
 * @param qname: request name.
 * @param qclass: request class.
 * @return true if a priming subrequest was made, false if not. The will only
 *         issue a priming request if it detects an unprimed stub.
 *         Uses value of 2 to signal during stub-prime in root-prime situation
 *         that a noprime-stub is available and resolution can continue.
 */
static int
prime_stub(struct module_qstate* qstate, struct iter_qstate* iq, int id,
	uint8_t* qname, uint16_t qclass)
{
	/* Lookup the stub hint. This will return null if the stub doesn't 
	 * need to be re-primed. */
	struct iter_hints_stub* stub;
	struct delegpt* stub_dp;
	struct module_qstate* subq;
	int nolock = 0;

	if(!qname) return 0;
	stub = hints_lookup_stub(qstate->env->hints, qname, qclass, iq->dp,
		nolock);
	/* The stub (if there is one) does not need priming. */
	if(!stub) return 0;
	stub_dp = stub->dp;
	/* if we have an auth_zone dp, and stub is equal, don't prime stub
	 * yet, unless we want to fallback and avoid the auth_zone */
	if(!iq->auth_zone_avoid && iq->dp && iq->dp->auth_dp && 
		query_dname_compare(iq->dp->name, stub_dp->name) == 0) {
		lock_rw_unlock(&qstate->env->hints->lock);
		return 0;
	}

	/* is it a noprime stub (always use) */
	if(stub->noprime) {
		int r = 0;
		if(iq->dp == NULL) r = 2;
		/* copy the dp out of the fixed hints structure, so that
		 * it can be changed when servicing this query */
		iq->dp = delegpt_copy(stub_dp, qstate->region);
		lock_rw_unlock(&qstate->env->hints->lock);
		if(!iq->dp) {
			log_err("out of memory priming stub");
			errinf(qstate, "malloc failure, priming stub");
			(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return 1; /* return 1 to make module stop, with error */
		}
		log_nametypeclass(VERB_DETAIL, "use stub", iq->dp->name,
			LDNS_RR_TYPE_NS, qclass);
		return r;
	}

	/* Otherwise, we need to (re)prime the stub. */
	log_nametypeclass(VERB_DETAIL, "priming stub", stub_dp->name, 
		LDNS_RR_TYPE_NS, qclass);

	/* Stub priming events start at the QUERYTARGETS state to avoid the
	 * redundant INIT state processing. */
	if(!generate_sub_request(stub_dp->name, stub_dp->namelen, 
		LDNS_RR_TYPE_NS, qclass, qstate, id, iq,
		QUERYTARGETS_STATE, PRIME_RESP_STATE, &subq, 0, 0)) {
		lock_rw_unlock(&qstate->env->hints->lock);
		verbose(VERB_ALGO, "could not prime stub");
		errinf(qstate, "could not generate lookup for stub prime");
		(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		return 1; /* return 1 to make module stop, with error */
	}
	if(subq) {
		struct iter_qstate* subiq = 
			(struct iter_qstate*)subq->minfo[id];

		/* Set the initial delegation point to the hint. */
		/* make copy to avoid use of stub dp by different qs/threads */
		subiq->dp = delegpt_copy(stub_dp, subq->region);
		lock_rw_unlock(&qstate->env->hints->lock);
		if(!subiq->dp) {
			log_err("out of memory priming stub, copydp");
			fptr_ok(fptr_whitelist_modenv_kill_sub(
				qstate->env->kill_sub));
			(*qstate->env->kill_sub)(subq);
			errinf(qstate, "malloc failure, in stub prime");
			(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return 1; /* return 1 to make module stop, with error */
		}
		/* there should not be any target queries -- although there 
		 * wouldn't be anyway, since stub hints never have 
		 * missing targets. */
		subiq->num_target_queries = 0; 
		subiq->wait_priming_stub = 1;
		subiq->dnssec_expected = iter_indicates_dnssec(
			qstate->env, subiq->dp, NULL, subq->qinfo.qclass);
	} else {
		lock_rw_unlock(&qstate->env->hints->lock);
	}
	
	/* this module stops, our submodule starts, and does the query. */
	qstate->ext_state[id] = module_wait_subquery;
	return 1;
}

/**
 * Generate a delegation point for an auth zone (unless cached dp is better)
 * false on alloc failure.
 */
static int
auth_zone_delegpt(struct module_qstate* qstate, struct iter_qstate* iq,
	uint8_t* delname, size_t delnamelen)
{
	struct auth_zone* z;
	if(iq->auth_zone_avoid)
		return 1;
	if(!delname) {
		delname = iq->qchase.qname;
		delnamelen = iq->qchase.qname_len;
	}
	lock_rw_rdlock(&qstate->env->auth_zones->lock);
	z = auth_zones_find_zone(qstate->env->auth_zones, delname, delnamelen,
		qstate->qinfo.qclass);
	if(!z) {
		lock_rw_unlock(&qstate->env->auth_zones->lock);
		return 1;
	}
	lock_rw_rdlock(&z->lock);
	lock_rw_unlock(&qstate->env->auth_zones->lock);
	if(z->for_upstream) {
		if(iq->dp && query_dname_compare(z->name, iq->dp->name) == 0
			&& iq->dp->auth_dp && qstate->blacklist &&
			z->fallback_enabled) {
			/* cache is blacklisted and fallback, and we
			 * already have an auth_zone dp */
			if(verbosity>=VERB_ALGO) {
				char buf[LDNS_MAX_DOMAINLEN];
				dname_str(z->name, buf);
				verbose(VERB_ALGO, "auth_zone %s "
				  "fallback because cache blacklisted",
				  buf);
			}
			lock_rw_unlock(&z->lock);
			iq->dp = NULL;
			return 1;
		}
		if(iq->dp==NULL || dname_subdomain_c(z->name, iq->dp->name)) {
			struct delegpt* dp;
			if(qstate->blacklist && z->fallback_enabled) {
				/* cache is blacklisted because of a DNSSEC
				 * validation failure, and the zone allows
				 * fallback to the internet, query there. */
				if(verbosity>=VERB_ALGO) {
					char buf[LDNS_MAX_DOMAINLEN];
					dname_str(z->name, buf);
					verbose(VERB_ALGO, "auth_zone %s "
					  "fallback because cache blacklisted",
					  buf);
				}
				lock_rw_unlock(&z->lock);
				return 1;
			}
			dp = (struct delegpt*)regional_alloc_zero(
				qstate->region, sizeof(*dp));
			if(!dp) {
				log_err("alloc failure");
				if(z->fallback_enabled) {
					lock_rw_unlock(&z->lock);
					return 1; /* just fallback */
				}
				lock_rw_unlock(&z->lock);
				errinf(qstate, "malloc failure");
				return 0;
			}
			dp->name = regional_alloc_init(qstate->region,
				z->name, z->namelen);
			if(!dp->name) {
				log_err("alloc failure");
				if(z->fallback_enabled) {
					lock_rw_unlock(&z->lock);
					return 1; /* just fallback */
				}
				lock_rw_unlock(&z->lock);
				errinf(qstate, "malloc failure");
				return 0;
			}
			dp->namelen = z->namelen;
			dp->namelabs = z->namelabs;
			dp->auth_dp = 1;
			iq->dp = dp;
		}
	}

	lock_rw_unlock(&z->lock);
	return 1;
}

/**
 * Generate A and AAAA checks for glue that is in-zone for the referral
 * we just got to obtain authoritative information on the addresses.
 *
 * @param qstate: the qtstate that triggered the need to prime.
 * @param iq: iterator query state.
 * @param id: module id.
 */
static void
generate_a_aaaa_check(struct module_qstate* qstate, struct iter_qstate* iq, 
	int id)
{
	struct iter_env* ie = (struct iter_env*)qstate->env->modinfo[id];
	struct module_qstate* subq;
	size_t i;
	struct reply_info* rep = iq->response->rep;
	struct ub_packed_rrset_key* s;
	log_assert(iq->dp);

	if(iq->depth == ie->max_dependency_depth)
		return;
	/* walk through additional, and check if in-zone,
	 * only relevant A, AAAA are left after scrub anyway */
	for(i=rep->an_numrrsets+rep->ns_numrrsets; i<rep->rrset_count; i++) {
		s = rep->rrsets[i];
		/* check *ALL* addresses that are transmitted in additional*/
		/* is it an address ? */
		if( !(ntohs(s->rk.type)==LDNS_RR_TYPE_A ||
			ntohs(s->rk.type)==LDNS_RR_TYPE_AAAA)) {
			continue;
		}
		/* is this query the same as the A/AAAA check for it */
		if(qstate->qinfo.qtype == ntohs(s->rk.type) &&
			qstate->qinfo.qclass == ntohs(s->rk.rrset_class) &&
			query_dname_compare(qstate->qinfo.qname, 
				s->rk.dname)==0 &&
			(qstate->query_flags&BIT_RD) && 
			!(qstate->query_flags&BIT_CD))
			continue;

		/* generate subrequest for it */
		log_nametypeclass(VERB_ALGO, "schedule addr fetch", 
			s->rk.dname, ntohs(s->rk.type), 
			ntohs(s->rk.rrset_class));
		if(!generate_sub_request(s->rk.dname, s->rk.dname_len, 
			ntohs(s->rk.type), ntohs(s->rk.rrset_class),
			qstate, id, iq,
			INIT_REQUEST_STATE, FINISHED_STATE, &subq, 1, 0)) {
			verbose(VERB_ALGO, "could not generate addr check");
			return;
		}
		/* ignore subq - not need for more init */
	}
}

/**
 * Generate a NS check request to obtain authoritative information
 * on an NS rrset.
 *
 * @param qstate: the qstate that triggered the need to prime.
 * @param iq: iterator query state.
 * @param id: module id.
 */
static void
generate_ns_check(struct module_qstate* qstate, struct iter_qstate* iq, int id)
{
	struct iter_env* ie = (struct iter_env*)qstate->env->modinfo[id];
	struct module_qstate* subq;
	log_assert(iq->dp);

	if(iq->depth == ie->max_dependency_depth)
		return;
	if(!can_have_last_resort(qstate->env, iq->dp->name, iq->dp->namelen,
		iq->qchase.qclass, NULL, NULL, NULL))
		return;
	/* is this query the same as the nscheck? */
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_NS &&
		query_dname_compare(iq->dp->name, qstate->qinfo.qname)==0 &&
		(qstate->query_flags&BIT_RD) && !(qstate->query_flags&BIT_CD)){
		/* spawn off A, AAAA queries for in-zone glue to check */
		generate_a_aaaa_check(qstate, iq, id);
		return;
	}
	/* no need to get the NS record for DS, it is above the zonecut */
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_DS)
		return;

	log_nametypeclass(VERB_ALGO, "schedule ns fetch", 
		iq->dp->name, LDNS_RR_TYPE_NS, iq->qchase.qclass);
	if(!generate_sub_request(iq->dp->name, iq->dp->namelen, 
		LDNS_RR_TYPE_NS, iq->qchase.qclass, qstate, id, iq,
		INIT_REQUEST_STATE, FINISHED_STATE, &subq, 1, 0)) {
		verbose(VERB_ALGO, "could not generate ns check");
		return;
	}
	if(subq) {
		struct iter_qstate* subiq = 
			(struct iter_qstate*)subq->minfo[id];

		/* make copy to avoid use of stub dp by different qs/threads */
		/* refetch glue to start higher up the tree */
		subiq->refetch_glue = 1;
		subiq->dp = delegpt_copy(iq->dp, subq->region);
		if(!subiq->dp) {
			log_err("out of memory generating ns check, copydp");
			fptr_ok(fptr_whitelist_modenv_kill_sub(
				qstate->env->kill_sub));
			(*qstate->env->kill_sub)(subq);
			return;
		}
	}
}

/**
 * Generate a DNSKEY prefetch query to get the DNSKEY for the DS record we
 * just got in a referral (where we have dnssec_expected, thus have trust
 * anchors above it).  Note that right after calling this routine the
 * iterator detached subqueries (because of following the referral), and thus
 * the DNSKEY query becomes detached, its return stored in the cache for
 * later lookup by the validator.  This cache lookup by the validator avoids
 * the roundtrip incurred by the DNSKEY query.  The DNSKEY query is now
 * performed at about the same time the original query is sent to the domain,
 * thus the two answers are likely to be returned at about the same time,
 * saving a roundtrip from the validated lookup.
 *
 * @param qstate: the qtstate that triggered the need to prime.
 * @param iq: iterator query state.
 * @param id: module id.
 */
static void
generate_dnskey_prefetch(struct module_qstate* qstate, 
	struct iter_qstate* iq, int id)
{
	struct module_qstate* subq;
	log_assert(iq->dp);

	/* is this query the same as the prefetch? */
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_DNSKEY &&
		query_dname_compare(iq->dp->name, qstate->qinfo.qname)==0 &&
		(qstate->query_flags&BIT_RD) && !(qstate->query_flags&BIT_CD)){
		return;
	}
	/* we do not generate this prefetch when the query list is full,
	 * the query is fetched, if needed, when the validator wants it.
	 * At that time the validator waits for it, after spawning it.
	 * This means there is one state that uses cpu and a socket, the
	 * spawned while this one waits, and not several at the same time,
	 * if we had created the lookup here. And this helps to keep
	 * the total load down, but the query still succeeds to resolve. */
	if(mesh_jostle_exceeded(qstate->env->mesh))
		return;

	/* if the DNSKEY is in the cache this lookup will stop quickly */
	log_nametypeclass(VERB_ALGO, "schedule dnskey prefetch", 
		iq->dp->name, LDNS_RR_TYPE_DNSKEY, iq->qchase.qclass);
	if(!generate_sub_request(iq->dp->name, iq->dp->namelen, 
		LDNS_RR_TYPE_DNSKEY, iq->qchase.qclass, qstate, id, iq,
		INIT_REQUEST_STATE, FINISHED_STATE, &subq, 0, 0)) {
		/* we'll be slower, but it'll work */
		verbose(VERB_ALGO, "could not generate dnskey prefetch");
		return;
	}
	if(subq) {
		struct iter_qstate* subiq = 
			(struct iter_qstate*)subq->minfo[id];
		/* this qstate has the right delegation for the dnskey lookup*/
		/* make copy to avoid use of stub dp by different qs/threads */
		subiq->dp = delegpt_copy(iq->dp, subq->region);
		/* if !subiq->dp, it'll start from the cache, no problem */
	}
}

/**
 * See if the query needs forwarding.
 * 
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @return true if the request is forwarded, false if not.
 * 	If returns true but, iq->dp is NULL then a malloc failure occurred.
 */
static int
forward_request(struct module_qstate* qstate, struct iter_qstate* iq)
{
	struct delegpt* dp;
	uint8_t* delname = iq->qchase.qname;
	size_t delnamelen = iq->qchase.qname_len;
	int nolock = 0;
	if(iq->refetch_glue && iq->dp) {
		delname = iq->dp->name;
		delnamelen = iq->dp->namelen;
	}
	/* strip one label off of DS query to lookup higher for it */
	if( (iq->qchase.qtype == LDNS_RR_TYPE_DS || iq->refetch_glue)
		&& !dname_is_root(iq->qchase.qname))
		dname_remove_label(&delname, &delnamelen);
	dp = forwards_lookup(qstate->env->fwds, delname, iq->qchase.qclass,
		nolock);
	if(!dp) return 0;
	/* send recursion desired to forward addr */
	iq->chase_flags |= BIT_RD; 
	iq->dp = delegpt_copy(dp, qstate->region);
	lock_rw_unlock(&qstate->env->fwds->lock);
	/* iq->dp checked by caller */
	verbose(VERB_ALGO, "forwarding request");
	return 1;
}

/** 
 * Process the initial part of the request handling. This state roughly
 * corresponds to resolver algorithms steps 1 (find answer in cache) and 2
 * (find the best servers to ask).
 *
 * Note that all requests start here, and query restarts revisit this state.
 *
 * This state either generates: 1) a response, from cache or error, 2) a
 * priming event, or 3) forwards the request to the next state (init2,
 * generally).
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param ie: iterator shared global environment.
 * @param id: module id.
 * @return true if the event needs more request processing immediately,
 *         false if not.
 */
static int
processInitRequest(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	uint8_t dpname_storage[LDNS_MAX_DOMAINLEN+1];
	uint8_t* delname, *dpname=NULL;
	size_t delnamelen, dpnamelen=0;
	struct dns_msg* msg = NULL;

	log_query_info(VERB_DETAIL, "resolving", &qstate->qinfo);
	/* check effort */

	/* We enforce a maximum number of query restarts. This is primarily a
	 * cheap way to prevent CNAME loops. */
	if(iq->query_restart_count > ie->max_query_restarts) {
		verbose(VERB_QUERY, "request has exceeded the maximum number"
			" of query restarts with %d", iq->query_restart_count);
		errinf(qstate, "request has exceeded the maximum number "
			"restarts (eg. indirections)");
		if(iq->qchase.qname)
			errinf_dname(qstate, "stop at", iq->qchase.qname);
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* We enforce a maximum recursion/dependency depth -- in general, 
	 * this is unnecessary for dependency loops (although it will 
	 * catch those), but it provides a sensible limit to the amount 
	 * of work required to answer a given query. */
	verbose(VERB_ALGO, "request has dependency depth of %d", iq->depth);
	if(iq->depth > ie->max_dependency_depth) {
		verbose(VERB_QUERY, "request has exceeded the maximum "
			"dependency depth with depth of %d", iq->depth);
		errinf(qstate, "request has exceeded the maximum dependency "
			"depth (eg. nameserver lookup recursion)");
		return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* If the request is qclass=ANY, setup to generate each class */
	if(qstate->qinfo.qclass == LDNS_RR_CLASS_ANY) {
		iq->qchase.qclass = 0;
		return next_state(iq, COLLECT_CLASS_STATE);
	}

	/*
	 * If we are restricted by a forward-zone or a stub-zone, we
	 * can't re-fetch glue for this delegation point.
	 * we won’t try to re-fetch glue if the iq->dp is null.
	 */
	if (iq->refetch_glue &&
	        iq->dp &&
	        !can_have_last_resort(qstate->env, iq->dp->name,
	             iq->dp->namelen, iq->qchase.qclass, NULL, NULL, NULL)) {
	    iq->refetch_glue = 0;
	}

	/* Resolver Algorithm Step 1 -- Look for the answer in local data. */

	/* This either results in a query restart (CNAME cache response), a
	 * terminating response (ANSWER), or a cache miss (null). */

	/* Check RPZ for override */
	if(qstate->env->auth_zones) {
		/* apply rpz qname triggers, like after cname */
		struct dns_msg* forged_response =
			rpz_callback_from_iterator_cname(qstate, iq);
		if(forged_response) {
			uint8_t* sname = 0;
			size_t slen = 0;
			int count = 0;
			while(forged_response && reply_find_rrset_section_an(
				forged_response->rep, iq->qchase.qname,
				iq->qchase.qname_len, LDNS_RR_TYPE_CNAME,
				iq->qchase.qclass) &&
				iq->qchase.qtype != LDNS_RR_TYPE_CNAME &&
				count++ < ie->max_query_restarts) {
				/* another cname to follow */
				if(!handle_cname_response(qstate, iq, forged_response,
					&sname, &slen)) {
					errinf(qstate, "malloc failure, CNAME info");
					return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
				}
				iq->qchase.qname = sname;
				iq->qchase.qname_len = slen;
				forged_response =
					rpz_callback_from_iterator_cname(qstate, iq);
			}
			if(forged_response != NULL) {
				qstate->ext_state[id] = module_finished;
				qstate->return_rcode = LDNS_RCODE_NOERROR;
				qstate->return_msg = forged_response;
				iq->response = forged_response;
				next_state(iq, FINISHED_STATE);
				if(!iter_prepend(iq, qstate->return_msg, qstate->region)) {
					log_err("rpz: after cached cname, prepend rrsets: out of memory");
					return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
				}
				qstate->return_msg->qinfo = qstate->qinfo;
				return 0;
			}
			/* Follow the CNAME response */
			iq->dp = NULL;
			iq->refetch_glue = 0;
			iq->query_restart_count++;
			iq->sent_count = 0;
			iq->dp_target_count = 0;
			sock_list_insert(&qstate->reply_origin, NULL, 0, qstate->region);
			if(qstate->env->cfg->qname_minimisation)
				iq->minimisation_state = INIT_MINIMISE_STATE;
			return next_state(iq, INIT_REQUEST_STATE);
		}
	}

	if (iter_stub_fwd_no_cache(qstate, &iq->qchase, &dpname, &dpnamelen,
		dpname_storage, sizeof(dpname_storage))) {
		/* Asked to not query cache. */
		verbose(VERB_ALGO, "no-cache set, going to the network");
		qstate->no_cache_lookup = 1;
		qstate->no_cache_store = 1;
		msg = NULL;
	} else if(qstate->blacklist) {
		/* if cache, or anything else, was blacklisted then
		 * getting older results from cache is a bad idea, no cache */
		verbose(VERB_ALGO, "cache blacklisted, going to the network");
		msg = NULL;
	} else if(!qstate->no_cache_lookup) {
		msg = dns_cache_lookup(qstate->env, iq->qchase.qname, 
			iq->qchase.qname_len, iq->qchase.qtype, 
			iq->qchase.qclass, qstate->query_flags,
			qstate->region, qstate->env->scratch, 0, dpname,
			dpnamelen);
		if(!msg && qstate->env->neg_cache &&
			iter_qname_indicates_dnssec(qstate->env, &iq->qchase)) {
			/* lookup in negative cache; may result in
			 * NOERROR/NODATA or NXDOMAIN answers that need validation */
			msg = val_neg_getmsg(qstate->env->neg_cache, &iq->qchase,
				qstate->region, qstate->env->rrset_cache,
				qstate->env->scratch_buffer, 
				*qstate->env->now, 1/*add SOA*/, NULL, 
				qstate->env->cfg);
		}
		/* item taken from cache does not match our query name, thus
		 * security needs to be re-examined later */
		if(msg && query_dname_compare(qstate->qinfo.qname,
			iq->qchase.qname) != 0)
			msg->rep->security = sec_status_unchecked;
	}
	if(msg) {
		/* handle positive cache response */
		enum response_type type = response_type_from_cache(msg, 
			&iq->qchase);
		if(verbosity >= VERB_ALGO) {
			log_dns_msg("msg from cache lookup", &msg->qinfo, 
				msg->rep);
			verbose(VERB_ALGO, "msg ttl is %d, prefetch ttl %d", 
				(int)msg->rep->ttl, 
				(int)msg->rep->prefetch_ttl);
		}

		if(type == RESPONSE_TYPE_CNAME) {
			uint8_t* sname = 0;
			size_t slen = 0;
			verbose(VERB_ALGO, "returning CNAME response from "
				"cache");
			if(!handle_cname_response(qstate, iq, msg, 
				&sname, &slen)) {
				errinf(qstate, "failed to prepend CNAME "
					"components, malloc failure");
				return error_response(qstate, id, 
					LDNS_RCODE_SERVFAIL);
			}
			iq->qchase.qname = sname;
			iq->qchase.qname_len = slen;
			/* This *is* a query restart, even if it is a cheap 
			 * one. */
			iq->dp = NULL;
			iq->refetch_glue = 0;
			iq->query_restart_count++;
			iq->sent_count = 0;
			iq->dp_target_count = 0;
			sock_list_insert(&qstate->reply_origin, NULL, 0, qstate->region);
			if(qstate->env->cfg->qname_minimisation)
				iq->minimisation_state = INIT_MINIMISE_STATE;
			return next_state(iq, INIT_REQUEST_STATE);
		}
		/* if from cache, NULL, else insert 'cache IP' len=0 */
		if(qstate->reply_origin)
			sock_list_insert(&qstate->reply_origin, NULL, 0, qstate->region);
		if(FLAGS_GET_RCODE(msg->rep->flags) == LDNS_RCODE_SERVFAIL)
			errinf(qstate, "SERVFAIL in cache");
		/* it is an answer, response, to final state */
		verbose(VERB_ALGO, "returning answer from cache.");
		iq->response = msg;
		return final_state(iq);
	}

	/* attempt to forward the request */
	if(forward_request(qstate, iq))
	{
		if(!iq->dp) {
			log_err("alloc failure for forward dp");
			errinf(qstate, "malloc failure for forward zone");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		if(!cache_fill_missing(qstate->env, iq->qchase.qclass,
			qstate->region, iq->dp, 0)) {
			errinf(qstate, "malloc failure, copy extra info into delegation point");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		if((qstate->query_flags&BIT_RD)==0) {
			/* If the server accepts RD=0 queries and forwards
			 * with RD=1, then if the server is listed as an NS
			 * entry, it starts query loops. Stop that loop by
			 * disallowing the query. The RD=0 was previously used
			 * to check the cache with allow_snoop. For stubs,
			 * the iterator pass would have primed the stub and
			 * then cached information can be used for further
			 * queries. */
			verbose(VERB_ALGO, "cannot forward RD=0 query, to stop query loops");
			errinf(qstate, "cannot forward RD=0 query");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		iq->refetch_glue = 0;
		iq->minimisation_state = DONOT_MINIMISE_STATE;
		/* the request has been forwarded.
		 * forwarded requests need to be immediately sent to the 
		 * next state, QUERYTARGETS. */
		return next_state(iq, QUERYTARGETS_STATE);
	}

	/* Resolver Algorithm Step 2 -- find the "best" servers. */

	/* first, adjust for DS queries. To avoid the grandparent problem, 
	 * we just look for the closest set of server to the parent of qname.
	 * When re-fetching glue we also need to ask the parent.
	 */
	if(iq->refetch_glue) {
		if(!iq->dp) {
			log_err("internal or malloc fail: no dp for refetch");
			errinf(qstate, "malloc failure, for delegation info");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		delname = iq->dp->name;
		delnamelen = iq->dp->namelen;
	} else {
		delname = iq->qchase.qname;
		delnamelen = iq->qchase.qname_len;
	}
	if(iq->qchase.qtype == LDNS_RR_TYPE_DS || iq->refetch_glue ||
	   (iq->qchase.qtype == LDNS_RR_TYPE_NS && qstate->prefetch_leeway
	   && can_have_last_resort(qstate->env, delname, delnamelen, iq->qchase.qclass, NULL, NULL, NULL))) {
		/* remove first label from delname, root goes to hints,
		 * but only to fetch glue, not for qtype=DS. */
		/* also when prefetching an NS record, fetch it again from
		 * its parent, just as if it expired, so that you do not
		 * get stuck on an older nameserver that gives old NSrecords */
		if(dname_is_root(delname) && (iq->refetch_glue ||
			(iq->qchase.qtype == LDNS_RR_TYPE_NS &&
			qstate->prefetch_leeway)))
			delname = NULL; /* go to root priming */
		else 	dname_remove_label(&delname, &delnamelen);
	}
	/* delname is the name to lookup a delegation for. If NULL rootprime */
	while(1) {
		
		/* Lookup the delegation in the cache. If null, then the 
		 * cache needs to be primed for the qclass. */
		if(delname)
		     iq->dp = dns_cache_find_delegation(qstate->env, delname, 
			delnamelen, iq->qchase.qtype, iq->qchase.qclass, 
			qstate->region, &iq->deleg_msg,
			*qstate->env->now+qstate->prefetch_leeway, 1,
			dpname, dpnamelen);
		else iq->dp = NULL;

		/* If the cache has returned nothing, then we have a 
		 * root priming situation. */
		if(iq->dp == NULL) {
			int r;
			int nolock = 0;
			/* if under auth zone, no prime needed */
			if(!auth_zone_delegpt(qstate, iq, delname, delnamelen))
				return error_response(qstate, id, 
					LDNS_RCODE_SERVFAIL);
			if(iq->dp) /* use auth zone dp */
				return next_state(iq, INIT_REQUEST_2_STATE);
			/* if there is a stub, then no root prime needed */
			r = prime_stub(qstate, iq, id, delname,
				iq->qchase.qclass);
			if(r == 2)
				break; /* got noprime-stub-zone, continue */
			else if(r)
				return 0; /* stub prime request made */
			if(forwards_lookup_root(qstate->env->fwds,
				iq->qchase.qclass, nolock)) {
				lock_rw_unlock(&qstate->env->fwds->lock);
				/* forward zone root, no root prime needed */
				/* fill in some dp - safety belt */
				iq->dp = hints_find_root(qstate->env->hints,
					iq->qchase.qclass, nolock);
				if(!iq->dp) {
					log_err("internal error: no hints dp");
					errinf(qstate, "no hints for this class");
					return error_response_cache(qstate, id,
						LDNS_RCODE_SERVFAIL);
				}
				iq->dp = delegpt_copy(iq->dp, qstate->region);
				lock_rw_unlock(&qstate->env->hints->lock);
				if(!iq->dp) {
					log_err("out of memory in safety belt");
					errinf(qstate, "malloc failure, in safety belt");
					return error_response(qstate, id, 
						LDNS_RCODE_SERVFAIL);
				}
				return next_state(iq, INIT_REQUEST_2_STATE);
			}
			/* Note that the result of this will set a new
			 * DelegationPoint based on the result of priming. */
			if(!prime_root(qstate, iq, id, iq->qchase.qclass))
				return error_response(qstate, id, 
					LDNS_RCODE_REFUSED);

			/* priming creates and sends a subordinate query, with 
			 * this query as the parent. So further processing for 
			 * this event will stop until reactivated by the 
			 * results of priming. */
			return 0;
		}
		if(!iq->ratelimit_ok && qstate->prefetch_leeway)
			iq->ratelimit_ok = 1; /* allow prefetches, this keeps
			otherwise valid data in the cache */

		/* see if this dp not useless.
		 * It is useless if:
		 *	o all NS items are required glue.
		 *	  or the query is for NS item that is required glue.
		 *	o no addresses are provided.
		 *	o RD qflag is on.
		 * Instead, go up one level, and try to get even further
		 * If the root was useless, use safety belt information.
		 * Only check cache returns, because replies for servers
		 * could be useless but lead to loops (bumping into the
		 * same server reply) if useless-checked.
		 */
		if(iter_dp_is_useless(&qstate->qinfo, qstate->query_flags,
			iq->dp, ie->supports_ipv4, ie->supports_ipv6,
			ie->nat64.use_nat64)) {
			int have_dp = 0;
			if(!can_have_last_resort(qstate->env, iq->dp->name, iq->dp->namelen, iq->qchase.qclass, &have_dp, &iq->dp, qstate->region)) {
				if(have_dp) {
					verbose(VERB_QUERY, "cache has stub "
						"or fwd but no addresses, "
						"fallback to config");
					if(have_dp && !iq->dp) {
						log_err("out of memory in "
							"stub/fwd fallback");
						errinf(qstate, "malloc failure, for fallback to config");
						return error_response(qstate,
						    id, LDNS_RCODE_SERVFAIL);
					}
					break;
				}
				verbose(VERB_ALGO, "useless dp "
					"but cannot go up, servfail");
				delegpt_log(VERB_ALGO, iq->dp);
				errinf(qstate, "no useful nameservers, "
					"and cannot go up");
				errinf_dname(qstate, "for zone", iq->dp->name);
				return error_response(qstate, id, 
					LDNS_RCODE_SERVFAIL);
			}
			if(dname_is_root(iq->dp->name)) {
				/* use safety belt */
				int nolock = 0;
				verbose(VERB_QUERY, "Cache has root NS but "
				"no addresses. Fallback to the safety belt.");
				iq->dp = hints_find_root(qstate->env->hints,
					iq->qchase.qclass, nolock);
				/* note deleg_msg is from previous lookup,
				 * but RD is on, so it is not used */
				if(!iq->dp) {
					log_err("internal error: no hints dp");
					return error_response(qstate, id, 
						LDNS_RCODE_REFUSED);
				}
				iq->dp = delegpt_copy(iq->dp, qstate->region);
				lock_rw_unlock(&qstate->env->hints->lock);
				if(!iq->dp) {
					log_err("out of memory in safety belt");
					errinf(qstate, "malloc failure, in safety belt, for root");
					return error_response(qstate, id, 
						LDNS_RCODE_SERVFAIL);
				}
				break;
			} else {
				verbose(VERB_ALGO, 
					"cache delegation was useless:");
				delegpt_log(VERB_ALGO, iq->dp);
				/* go up */
				delname = iq->dp->name;
				delnamelen = iq->dp->namelen;
				dname_remove_label(&delname, &delnamelen);
			}
		} else break;
	}

	verbose(VERB_ALGO, "cache delegation returns delegpt");
	delegpt_log(VERB_ALGO, iq->dp);

	/* Otherwise, set the current delegation point and move on to the 
	 * next state. */
	return next_state(iq, INIT_REQUEST_2_STATE);
}

/** 
 * Process the second part of the initial request handling. This state
 * basically exists so that queries that generate root priming events have
 * the same init processing as ones that do not. Request events that reach
 * this state must have a valid currentDelegationPoint set.
 *
 * This part is primarily handling stub zone priming. Events that reach this
 * state must have a current delegation point.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @return true if the event needs more request processing immediately,
 *         false if not.
 */
static int
processInitRequest2(struct module_qstate* qstate, struct iter_qstate* iq,
	int id)
{
	uint8_t* delname;
	size_t delnamelen;
	log_query_info(VERB_QUERY, "resolving (init part 2): ", 
		&qstate->qinfo);

	delname = iq->qchase.qname;
	delnamelen = iq->qchase.qname_len;
	if(iq->refetch_glue) {
		struct iter_hints_stub* stub;
		int nolock = 0;
		if(!iq->dp) {
			log_err("internal or malloc fail: no dp for refetch");
			errinf(qstate, "malloc failure, no delegation info");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		/* Do not send queries above stub, do not set delname to dp if
		 * this is above stub without stub-first. */
		stub = hints_lookup_stub(
			qstate->env->hints, iq->qchase.qname, iq->qchase.qclass,
			iq->dp, nolock);
		if(!stub || !stub->dp->has_parent_side_NS || 
			dname_subdomain_c(iq->dp->name, stub->dp->name)) {
			delname = iq->dp->name;
			delnamelen = iq->dp->namelen;
		}
		/* lock_() calls are macros that could be nothing, surround in {} */
		if(stub) { lock_rw_unlock(&qstate->env->hints->lock); }
	}
	if(iq->qchase.qtype == LDNS_RR_TYPE_DS || iq->refetch_glue) {
		if(!dname_is_root(delname))
			dname_remove_label(&delname, &delnamelen);
		iq->refetch_glue = 0; /* if CNAME causes restart, no refetch */
	}

	/* see if we have an auth zone to answer from, improves dp from cache
	 * (if any dp from cache) with auth zone dp, if that is lower */
	if(!auth_zone_delegpt(qstate, iq, delname, delnamelen))
		return error_response(qstate, id, LDNS_RCODE_SERVFAIL);

	/* Check to see if we need to prime a stub zone. */
	if(prime_stub(qstate, iq, id, delname, iq->qchase.qclass)) {
		/* A priming sub request was made */
		return 0;
	}

	/* most events just get forwarded to the next state. */
	return next_state(iq, INIT_REQUEST_3_STATE);
}

/** 
 * Process the third part of the initial request handling. This state exists
 * as a separate state so that queries that generate stub priming events
 * will get the tail end of the init process but not repeat the stub priming
 * check.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @return true, advancing the event to the QUERYTARGETS_STATE.
 */
static int
processInitRequest3(struct module_qstate* qstate, struct iter_qstate* iq, 
	int id)
{
	log_query_info(VERB_QUERY, "resolving (init part 3): ", 
		&qstate->qinfo);
	/* if the cache reply dp equals a validation anchor or msg has DS,
	 * then DNSSEC RRSIGs are expected in the reply */
	iq->dnssec_expected = iter_indicates_dnssec(qstate->env, iq->dp, 
		iq->deleg_msg, iq->qchase.qclass);

	/* If the RD flag wasn't set, then we just finish with the 
	 * cached referral as the response. */
	if(!(qstate->query_flags & BIT_RD) && iq->deleg_msg) {
		iq->response = iq->deleg_msg;
		if(verbosity >= VERB_ALGO && iq->response)
			log_dns_msg("no RD requested, using delegation msg", 
				&iq->response->qinfo, iq->response->rep);
		if(qstate->reply_origin)
			sock_list_insert(&qstate->reply_origin, NULL, 0, qstate->region);
		return final_state(iq);
	}
	/* After this point, unset the RD flag -- this query is going to 
	 * be sent to an auth. server. */
	iq->chase_flags &= ~BIT_RD;

	/* if dnssec expected, fetch key for the trust-anchor or cached-DS */
	if(iq->dnssec_expected && qstate->env->cfg->prefetch_key &&
		!(qstate->query_flags&BIT_CD)) {
		generate_dnskey_prefetch(qstate, iq, id);
		fptr_ok(fptr_whitelist_modenv_detach_subs(
			qstate->env->detach_subs));
		(*qstate->env->detach_subs)(qstate);
	}

	/* Jump to the next state. */
	return next_state(iq, QUERYTARGETS_STATE);
}

/**
 * Given a basic query, generate a parent-side "target" query. 
 * These are subordinate queries for missing delegation point target addresses,
 * for which only the parent of the delegation provides correct IP addresses.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @param name: target qname.
 * @param namelen: target qname length.
 * @param qtype: target qtype (either A or AAAA).
 * @param qclass: target qclass.
 * @return true on success, false on failure.
 */
static int
generate_parentside_target_query(struct module_qstate* qstate, 
	struct iter_qstate* iq, int id, uint8_t* name, size_t namelen, 
	uint16_t qtype, uint16_t qclass)
{
	struct module_qstate* subq;
	if(!generate_sub_request(name, namelen, qtype, qclass, qstate, 
		id, iq, INIT_REQUEST_STATE, FINISHED_STATE, &subq, 0, 0))
		return 0;
	if(subq) {
		struct iter_qstate* subiq = 
			(struct iter_qstate*)subq->minfo[id];
		/* blacklist the cache - we want to fetch parent stuff */
		sock_list_insert(&subq->blacklist, NULL, 0, subq->region);
		subiq->query_for_pside_glue = 1;
		if(dname_subdomain_c(name, iq->dp->name)) {
			subiq->dp = delegpt_copy(iq->dp, subq->region);
			subiq->dnssec_expected = iter_indicates_dnssec(
				qstate->env, subiq->dp, NULL, 
				subq->qinfo.qclass);
			subiq->refetch_glue = 1;
		} else {
			subiq->dp = dns_cache_find_delegation(qstate->env, 
				name, namelen, qtype, qclass, subq->region,
				&subiq->deleg_msg,
				*qstate->env->now+subq->prefetch_leeway,
				1, NULL, 0);
			/* if no dp, then it's from root, refetch unneeded */
			if(subiq->dp) { 
				subiq->dnssec_expected = iter_indicates_dnssec(
					qstate->env, subiq->dp, NULL, 
					subq->qinfo.qclass);
				subiq->refetch_glue = 1;
			}
		}
	}
	log_nametypeclass(VERB_QUERY, "new pside target", name, qtype, qclass);
	return 1;
}

/**
 * Given a basic query, generate a "target" query. These are subordinate
 * queries for missing delegation point target addresses.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @param name: target qname.
 * @param namelen: target qname length.
 * @param qtype: target qtype (either A or AAAA).
 * @param qclass: target qclass.
 * @return true on success, false on failure.
 */
static int
generate_target_query(struct module_qstate* qstate, struct iter_qstate* iq,
        int id, uint8_t* name, size_t namelen, uint16_t qtype, uint16_t qclass)
{
	struct module_qstate* subq;
	if(!generate_sub_request(name, namelen, qtype, qclass, qstate, 
		id, iq, INIT_REQUEST_STATE, FINISHED_STATE, &subq, 0, 0))
		return 0;
	log_nametypeclass(VERB_QUERY, "new target", name, qtype, qclass);
	return 1;
}

/**
 * Given an event at a certain state, generate zero or more target queries
 * for it's current delegation point.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param ie: iterator shared global environment.
 * @param id: module id.
 * @param maxtargets: The maximum number of targets to query for.
 *	if it is negative, there is no maximum number of targets.
 * @param num: returns the number of queries generated and processed, 
 *	which may be zero if there were no missing targets.
 * @return 0 on success, nonzero on error. 1 means temporary failure and
 * 	2 means the failure can be cached.
 */
static int
query_for_targets(struct module_qstate* qstate, struct iter_qstate* iq,
        struct iter_env* ie, int id, int maxtargets, int* num)
{
	int query_count = 0;
	struct delegpt_ns* ns;
	int missing;
	int toget = 0;

	iter_mark_cycle_targets(qstate, iq->dp);
	missing = (int)delegpt_count_missing_targets(iq->dp, NULL);
	log_assert(maxtargets != 0); /* that would not be useful */

	/* Generate target requests. Basically, any missing targets
	 * are queried for here, regardless if it is necessary to do
	 * so to continue processing. */
	if(maxtargets < 0 || maxtargets > missing)
		toget = missing;
	else	toget = maxtargets;
	if(toget == 0) {
		*num = 0;
		return 0;
	}

	/* now that we are sure that a target query is going to be made,
	 * check the limits. */
	if(iq->depth == ie->max_dependency_depth)
		return 1;
	if(iq->depth > 0 && iq->target_count &&
		iq->target_count[TARGET_COUNT_QUERIES] > MAX_TARGET_COUNT) {
		char s[LDNS_MAX_DOMAINLEN];
		dname_str(qstate->qinfo.qname, s);
		verbose(VERB_QUERY, "request %s has exceeded the maximum "
			"number of glue fetches %d", s,
			iq->target_count[TARGET_COUNT_QUERIES]);
		return 2;
	}
	if(iq->dp_target_count > MAX_DP_TARGET_COUNT) {
		char s[LDNS_MAX_DOMAINLEN];
		dname_str(qstate->qinfo.qname, s);
		verbose(VERB_QUERY, "request %s has exceeded the maximum "
			"number of glue fetches %d to a single delegation point",
			s, iq->dp_target_count);
		return 2;
	}

	/* select 'toget' items from the total of 'missing' items */
	log_assert(toget <= missing);

	/* loop over missing targets */
	for(ns = iq->dp->nslist; ns; ns = ns->next) {
		if(ns->resolved)
			continue;

		/* randomly select this item with probability toget/missing */
		if(!iter_ns_probability(qstate->env->rnd, toget, missing)) {
			/* do not select this one, next; select toget number
			 * of items from a list one less in size */
			missing --;
			continue;
		}

		if(ie->supports_ipv6 &&
			((ns->lame && !ns->done_pside6) ||
			(!ns->lame && !ns->got6))) {
			/* Send the AAAA request. */
			if(!generate_target_query(qstate, iq, id, 
				ns->name, ns->namelen,
				LDNS_RR_TYPE_AAAA, iq->qchase.qclass)) {
				*num = query_count;
				if(query_count > 0)
					qstate->ext_state[id] = module_wait_subquery;
				return 1;
			}
			query_count++;
			/* If the mesh query list is full, exit the loop here.
			 * This makes the routine spawn one query at a time,
			 * and this means there is no query state load
			 * increase, because the spawned state uses cpu and a
			 * socket while this state waits for that spawned
			 * state. Next time we can look up further targets */
			if(mesh_jostle_exceeded(qstate->env->mesh)) {
				/* If no ip4 query is possible, that makes
				 * this ns resolved. */
				if(!((ie->supports_ipv4 || ie->nat64.use_nat64) &&
					((ns->lame && !ns->done_pside4) ||
					(!ns->lame && !ns->got4)))) {
					ns->resolved = 1;
				}
				break;
		}
		}
		/* Send the A request. */
		if((ie->supports_ipv4 || ie->nat64.use_nat64) &&
			((ns->lame && !ns->done_pside4) ||
			(!ns->lame && !ns->got4))) {
			if(!generate_target_query(qstate, iq, id, 
				ns->name, ns->namelen, 
				LDNS_RR_TYPE_A, iq->qchase.qclass)) {
				*num = query_count;
				if(query_count > 0)
					qstate->ext_state[id] = module_wait_subquery;
				return 1;
			}
			query_count++;
			/* If the mesh query list is full, exit the loop. */
			if(mesh_jostle_exceeded(qstate->env->mesh)) {
				/* With the ip6 query already checked for,
				 * this makes the ns resolved. It is no longer
				 * a missing target. */
				ns->resolved = 1;
				break;
		}
		}

		/* mark this target as in progress. */
		ns->resolved = 1;
		missing--;
		toget--;
		if(toget == 0)
			break;
	}
	*num = query_count;
	if(query_count > 0)
		qstate->ext_state[id] = module_wait_subquery;

	return 0;
}

/**
 * Called by processQueryTargets when it would like extra targets to query
 * but it seems to be out of options.  At last resort some less appealing
 * options are explored.  If there are no more options, the result is SERVFAIL
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param ie: iterator shared global environment.
 * @param id: module id.
 * @return true if the event requires more request processing immediately,
 *         false if not. 
 */
static int
processLastResort(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	struct delegpt_ns* ns;
	int query_count = 0;
	verbose(VERB_ALGO, "No more query targets, attempting last resort");
	log_assert(iq->dp);

	if(!can_have_last_resort(qstate->env, iq->dp->name, iq->dp->namelen,
		iq->qchase.qclass, NULL, NULL, NULL)) {
		/* fail -- no more targets, no more hope of targets, no hope 
		 * of a response. */
		errinf(qstate, "all the configured stub or forward servers failed,");
		errinf_dname(qstate, "at zone", iq->dp->name);
		errinf_reply(qstate, iq);
		verbose(VERB_QUERY, "configured stub or forward servers failed -- returning SERVFAIL");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	if(qstate->env->cfg->harden_unverified_glue) {
		if(!cache_fill_missing(qstate->env, iq->qchase.qclass,
			qstate->region, iq->dp, PACKED_RRSET_UNVERIFIED_GLUE))
			log_err("out of memory in cache_fill_missing");
		if(iq->dp->usable_list) {
			verbose(VERB_ALGO, "try unverified glue from cache");
			return next_state(iq, QUERYTARGETS_STATE);
		}
	}
	if(!iq->dp->has_parent_side_NS && dname_is_root(iq->dp->name)) {
		struct delegpt* dp;
		int nolock = 0;
		dp = hints_find_root(qstate->env->hints,
			iq->qchase.qclass, nolock);
		if(dp) {
			struct delegpt_addr* a;
			iq->chase_flags &= ~BIT_RD; /* go to authorities */
			for(ns = dp->nslist; ns; ns=ns->next) {
				(void)delegpt_add_ns(iq->dp, qstate->region,
					ns->name, ns->lame, ns->tls_auth_name,
					ns->port);
			}
			for(a = dp->target_list; a; a=a->next_target) {
				(void)delegpt_add_addr(iq->dp, qstate->region,
					&a->addr, a->addrlen, a->bogus,
					a->lame, a->tls_auth_name, -1, NULL);
			}
			lock_rw_unlock(&qstate->env->hints->lock);
		}
		iq->dp->has_parent_side_NS = 1;
	} else if(!iq->dp->has_parent_side_NS) {
		if(!iter_lookup_parent_NS_from_cache(qstate->env, iq->dp,
			qstate->region, &qstate->qinfo) 
			|| !iq->dp->has_parent_side_NS) {
			/* if: malloc failure in lookup go up to try */
			/* if: no parent NS in cache - go up one level */
			verbose(VERB_ALGO, "try to grab parent NS");
			iq->store_parent_NS = iq->dp;
			iq->chase_flags &= ~BIT_RD; /* go to authorities */
			iq->deleg_msg = NULL;
			iq->refetch_glue = 1;
			iq->query_restart_count++;
			iq->sent_count = 0;
			iq->dp_target_count = 0;
			if(qstate->env->cfg->qname_minimisation)
				iq->minimisation_state = INIT_MINIMISE_STATE;
			return next_state(iq, INIT_REQUEST_STATE);
		}
	}
	/* see if that makes new names available */
	if(!cache_fill_missing(qstate->env, iq->qchase.qclass, 
		qstate->region, iq->dp, 0))
		log_err("out of memory in cache_fill_missing");
	if(iq->dp->usable_list) {
		verbose(VERB_ALGO, "try parent-side-name, w. glue from cache");
		return next_state(iq, QUERYTARGETS_STATE);
	}
	/* try to fill out parent glue from cache */
	if(iter_lookup_parent_glue_from_cache(qstate->env, iq->dp,
		qstate->region, &qstate->qinfo)) {
		/* got parent stuff from cache, see if we can continue */
		verbose(VERB_ALGO, "try parent-side glue from cache");
		return next_state(iq, QUERYTARGETS_STATE);
	}
	/* query for an extra name added by the parent-NS record */
	if(delegpt_count_missing_targets(iq->dp, NULL) > 0) {
		int qs = 0, ret;
		verbose(VERB_ALGO, "try parent-side target name");
		if((ret=query_for_targets(qstate, iq, ie, id, 1, &qs))!=0) {
			errinf(qstate, "could not fetch nameserver");
			errinf_dname(qstate, "at zone", iq->dp->name);
			if(ret == 1)
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		iq->num_target_queries += qs;
		target_count_increase(iq, qs);
		if(qs != 0) {
			qstate->ext_state[id] = module_wait_subquery;
			return 0; /* and wait for them */
		}
	}
	if(iq->depth == ie->max_dependency_depth) {
		verbose(VERB_QUERY, "maxdepth and need more nameservers, fail");
		errinf(qstate, "cannot fetch more nameservers because at max dependency depth");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	if(iq->depth > 0 && iq->target_count &&
		iq->target_count[TARGET_COUNT_QUERIES] > MAX_TARGET_COUNT) {
		char s[LDNS_MAX_DOMAINLEN];
		dname_str(qstate->qinfo.qname, s);
		verbose(VERB_QUERY, "request %s has exceeded the maximum "
			"number of glue fetches %d", s,
			iq->target_count[TARGET_COUNT_QUERIES]);
		errinf(qstate, "exceeded the maximum number of glue fetches");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	/* mark cycle targets for parent-side lookups */
	iter_mark_pside_cycle_targets(qstate, iq->dp);
	/* see if we can issue queries to get nameserver addresses */
	/* this lookup is not randomized, but sequential. */
	for(ns = iq->dp->nslist; ns; ns = ns->next) {
		/* if this nameserver is at a delegation point, but that
		 * delegation point is a stub and we cannot go higher, skip*/
		if( ((ie->supports_ipv6 && !ns->done_pside6) ||
		    ((ie->supports_ipv4 || ie->nat64.use_nat64) && !ns->done_pside4)) &&
		    !can_have_last_resort(qstate->env, ns->name, ns->namelen,
			iq->qchase.qclass, NULL, NULL, NULL)) {
			log_nametypeclass(VERB_ALGO, "cannot pside lookup ns "
				"because it is also a stub/forward,",
				ns->name, LDNS_RR_TYPE_NS, iq->qchase.qclass);
			if(ie->supports_ipv6) ns->done_pside6 = 1;
			if(ie->supports_ipv4 || ie->nat64.use_nat64) ns->done_pside4 = 1;
			continue;
		}
		/* query for parent-side A and AAAA for nameservers */
		if(ie->supports_ipv6 && !ns->done_pside6) {
			/* Send the AAAA request. */
			if(!generate_parentside_target_query(qstate, iq, id, 
				ns->name, ns->namelen,
				LDNS_RR_TYPE_AAAA, iq->qchase.qclass)) {
				errinf_dname(qstate, "could not generate nameserver AAAA lookup for", ns->name);
				return error_response(qstate, id,
					LDNS_RCODE_SERVFAIL);
			}
			ns->done_pside6 = 1;
			query_count++;
			if(mesh_jostle_exceeded(qstate->env->mesh)) {
				/* Wait for the lookup; do not spawn multiple
				 * lookups at a time. */
				verbose(VERB_ALGO, "try parent-side glue lookup");
				iq->num_target_queries += query_count;
				target_count_increase(iq, query_count);
				qstate->ext_state[id] = module_wait_subquery;
				return 0;
			}
		}
		if((ie->supports_ipv4 || ie->nat64.use_nat64) && !ns->done_pside4) {
			/* Send the A request. */
			if(!generate_parentside_target_query(qstate, iq, id, 
				ns->name, ns->namelen, 
				LDNS_RR_TYPE_A, iq->qchase.qclass)) {
				errinf_dname(qstate, "could not generate nameserver A lookup for", ns->name);
				return error_response(qstate, id,
					LDNS_RCODE_SERVFAIL);
			}
			ns->done_pside4 = 1;
			query_count++;
		}
		if(query_count != 0) { /* suspend to await results */
			verbose(VERB_ALGO, "try parent-side glue lookup");
			iq->num_target_queries += query_count;
			target_count_increase(iq, query_count);
			qstate->ext_state[id] = module_wait_subquery;
			return 0;
		}
	}

	/* if this was a parent-side glue query itself, then store that
	 * failure in cache. */
	if(!qstate->no_cache_store && iq->query_for_pside_glue
		&& !iq->pside_glue)
			iter_store_parentside_neg(qstate->env, &qstate->qinfo,
				iq->deleg_msg?iq->deleg_msg->rep:
				(iq->response?iq->response->rep:NULL));

	errinf(qstate, "all servers for this domain failed,");
	errinf_dname(qstate, "at zone", iq->dp->name);
	errinf_reply(qstate, iq);
	verbose(VERB_QUERY, "out of query targets -- returning SERVFAIL");
	/* fail -- no more targets, no more hope of targets, no hope 
	 * of a response. */
	return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
}

/** 
 * Try to find the NS record set that will resolve a qtype DS query. Due
 * to grandparent/grandchild reasons we did not get a proper lookup right
 * away.  We need to create type NS queries until we get the right parent
 * for this lookup.  We remove labels from the query to find the right point.
 * If we end up at the old dp name, then there is no solution.
 * 
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @return true if the event requires more immediate processing, false if
 *         not. This is generally only true when forwarding the request to
 *         the final state (i.e., on answer).
 */
static int
processDSNSFind(struct module_qstate* qstate, struct iter_qstate* iq, int id)
{
	struct module_qstate* subq = NULL;
	verbose(VERB_ALGO, "processDSNSFind");

	if(!iq->dsns_point) {
		/* initialize */
		iq->dsns_point = iq->qchase.qname;
		iq->dsns_point_len = iq->qchase.qname_len;
	}
	/* robustcheck for internal error: we are not underneath the dp */
	if(!dname_subdomain_c(iq->dsns_point, iq->dp->name)) {
		errinf_dname(qstate, "for DS query parent-child nameserver search the query is not under the zone", iq->dp->name);
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* go up one (more) step, until we hit the dp, if so, end */
	dname_remove_label(&iq->dsns_point, &iq->dsns_point_len);
	if(query_dname_compare(iq->dsns_point, iq->dp->name) == 0) {
		/* there was no inbetween nameserver, use the old delegation
		 * point again.  And this time, because dsns_point is nonNULL
		 * we are going to accept the (bad) result */
		iq->state = QUERYTARGETS_STATE;
		return 1;
	}
	iq->state = DSNS_FIND_STATE;

	/* spawn NS lookup (validation not needed, this is for DS lookup) */
	log_nametypeclass(VERB_ALGO, "fetch nameservers", 
		iq->dsns_point, LDNS_RR_TYPE_NS, iq->qchase.qclass);
	if(!generate_sub_request(iq->dsns_point, iq->dsns_point_len, 
		LDNS_RR_TYPE_NS, iq->qchase.qclass, qstate, id, iq,
		INIT_REQUEST_STATE, FINISHED_STATE, &subq, 0, 0)) {
		errinf_dname(qstate, "for DS query parent-child nameserver search, could not generate NS lookup for", iq->dsns_point);
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	return 0;
}

/**
 * Check if we wait responses for sent queries and update the iterator's
 * external state.
 */
static void
check_waiting_queries(struct iter_qstate* iq, struct module_qstate* qstate,
	int id)
{
	if(iq->num_target_queries>0 && iq->num_current_queries>0) {
		verbose(VERB_ALGO, "waiting for %d targets to "
			"resolve or %d outstanding queries to "
			"respond", iq->num_target_queries,
			iq->num_current_queries);
		qstate->ext_state[id] = module_wait_reply;
	} else if(iq->num_target_queries>0) {
		verbose(VERB_ALGO, "waiting for %d targets to "
			"resolve", iq->num_target_queries);
		qstate->ext_state[id] = module_wait_subquery;
	} else {
		verbose(VERB_ALGO, "waiting for %d "
			"outstanding queries to respond",
			iq->num_current_queries);
		qstate->ext_state[id] = module_wait_reply;
	}
}
	
/** 
 * This is the request event state where the request will be sent to one of
 * its current query targets. This state also handles issuing target lookup
 * queries for missing target IP addresses. Queries typically iterate on
 * this state, both when they are just trying different targets for a given
 * delegation point, and when they change delegation points. This state
 * roughly corresponds to RFC 1034 algorithm steps 3 and 4.
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param ie: iterator shared global environment.
 * @param id: module id.
 * @return true if the event requires more request processing immediately,
 *         false if not. This state only returns true when it is generating
 *         a SERVFAIL response because the query has hit a dead end.
 */
static int
processQueryTargets(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	int tf_policy;
	struct delegpt_addr* target;
	struct outbound_entry* outq;
	struct sockaddr_storage real_addr;
	socklen_t real_addrlen;
	int auth_fallback = 0;
	uint8_t* qout_orig = NULL;
	size_t qout_orig_len = 0;
	int sq_check_ratelimit = 1;
	int sq_was_ratelimited = 0;
	int can_do_promisc = 0;

	/* NOTE: a request will encounter this state for each target it
	 * needs to send a query to. That is, at least one per referral,
	 * more if some targets timeout or return throwaway answers. */

	log_query_info(VERB_QUERY, "processQueryTargets:", &qstate->qinfo);
	verbose(VERB_ALGO, "processQueryTargets: targetqueries %d, "
		"currentqueries %d sentcount %d", iq->num_target_queries, 
		iq->num_current_queries, iq->sent_count);

	/* Make sure that we haven't run away */
	if(iq->referral_count > MAX_REFERRAL_COUNT) {
		verbose(VERB_QUERY, "request has exceeded the maximum "
			"number of referrrals with %d", iq->referral_count);
		errinf(qstate, "exceeded the maximum of referrals");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	if(iq->sent_count > ie->max_sent_count) {
		verbose(VERB_QUERY, "request has exceeded the maximum "
			"number of sends with %d", iq->sent_count);
		errinf(qstate, "exceeded the maximum number of sends");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* Check if we reached MAX_TARGET_NX limit without a fallback activation. */
	if(iq->target_count && !*iq->nxns_dp &&
		iq->target_count[TARGET_COUNT_NX] > MAX_TARGET_NX) {
		struct delegpt_ns* ns;
		/* If we can wait for resolution, do so. */
		if(iq->num_target_queries>0 || iq->num_current_queries>0) {
			check_waiting_queries(iq, qstate, id);
			return 0;
		}
		verbose(VERB_ALGO, "request has exceeded the maximum "
			"number of nxdomain nameserver lookups (%d) with %d",
			MAX_TARGET_NX, iq->target_count[TARGET_COUNT_NX]);
		/* Check for dp because we require one below */
		if(!iq->dp) {
			verbose(VERB_QUERY, "Failed to get a delegation, "
				"giving up");
			errinf(qstate, "failed to get a delegation (eg. prime "
				"failure)");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		/* We reached the limit but we already have parent side
		 * information; stop resolution */
		if(iq->dp->has_parent_side_NS) {
			verbose(VERB_ALGO, "parent-side information is "
				"already present for the delegation point, no "
				"fallback possible");
			errinf(qstate, "exceeded the maximum nameserver nxdomains");
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		verbose(VERB_ALGO, "initiating parent-side fallback for "
			"nxdomain nameserver lookups");
		/* Mark all the current NSes as resolved to allow for parent
		 * fallback */
		for(ns=iq->dp->nslist; ns; ns=ns->next) {
			ns->resolved = 1;
		}
		/* Note the delegation point that triggered the NXNS fallback;
		 * no reason for shared queries to keep trying there.
		 * This also marks the fallback activation. */
		*iq->nxns_dp = malloc(iq->dp->namelen);
		if(!*iq->nxns_dp) {
			verbose(VERB_ALGO, "out of memory while initiating "
				"fallback");
			errinf(qstate, "exceeded the maximum nameserver "
				"nxdomains (malloc)");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		memcpy(*iq->nxns_dp, iq->dp->name, iq->dp->namelen);
	} else if(iq->target_count && *iq->nxns_dp) {
		/* Handle the NXNS fallback case. */
		/* If we can wait for resolution, do so. */
		if(iq->num_target_queries>0 || iq->num_current_queries>0) {
			check_waiting_queries(iq, qstate, id);
			return 0;
		}
		/* Check for dp because we require one below */
		if(!iq->dp) {
			verbose(VERB_QUERY, "Failed to get a delegation, "
				"giving up");
			errinf(qstate, "failed to get a delegation (eg. prime "
				"failure)");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}

		if(iq->target_count[TARGET_COUNT_NX] > MAX_TARGET_NX_FALLBACK) {
			verbose(VERB_ALGO, "request has exceeded the maximum "
				"number of fallback nxdomain nameserver "
				"lookups (%d) with %d", MAX_TARGET_NX_FALLBACK,
				iq->target_count[TARGET_COUNT_NX]);
			errinf(qstate, "exceeded the maximum nameserver nxdomains");
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}

		if(!iq->dp->has_parent_side_NS) {
			struct delegpt_ns* ns;
			if(!dname_canonical_compare(*iq->nxns_dp, iq->dp->name)) {
				verbose(VERB_ALGO, "this delegation point "
					"initiated the fallback, marking the "
					"nslist as resolved");
				for(ns=iq->dp->nslist; ns; ns=ns->next) {
					ns->resolved = 1;
				}
			}
		}
	}
	
	/* Make sure we have a delegation point, otherwise priming failed
	 * or another failure occurred */
	if(!iq->dp) {
		verbose(VERB_QUERY, "Failed to get a delegation, giving up");
		errinf(qstate, "failed to get a delegation (eg. prime failure)");
		return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	if(!ie->supports_ipv6)
		delegpt_no_ipv6(iq->dp);
	if(!ie->supports_ipv4 && !ie->nat64.use_nat64)
		delegpt_no_ipv4(iq->dp);
	delegpt_log(VERB_ALGO, iq->dp);

	if(iq->num_current_queries>0) {
		/* already busy answering a query, this restart is because
		 * more delegpt addrs became available, wait for existing
		 * query. */
		verbose(VERB_ALGO, "woke up, but wait for outstanding query");
		qstate->ext_state[id] = module_wait_reply;
		return 0;
	}

	if(iq->minimisation_state == INIT_MINIMISE_STATE
		&& !(iq->chase_flags & BIT_RD)) {
		/* (Re)set qinfo_out to (new) delegation point, except when
		 * qinfo_out is already a subdomain of dp. This happens when
		 * increasing by more than one label at once (QNAMEs with more
		 * than MAX_MINIMISE_COUNT labels). */
		if(!(iq->qinfo_out.qname_len 
			&& dname_subdomain_c(iq->qchase.qname, 
				iq->qinfo_out.qname)
			&& dname_subdomain_c(iq->qinfo_out.qname, 
				iq->dp->name))) {
			iq->qinfo_out.qname = iq->dp->name;
			iq->qinfo_out.qname_len = iq->dp->namelen;
			iq->qinfo_out.qtype = LDNS_RR_TYPE_A;
			iq->qinfo_out.qclass = iq->qchase.qclass;
			iq->qinfo_out.local_alias = NULL;
			iq->minimise_count = 0;
		}

		iq->minimisation_state = MINIMISE_STATE;
	}
	if(iq->minimisation_state == MINIMISE_STATE) {
		int qchaselabs = dname_count_labels(iq->qchase.qname);
		int labdiff = qchaselabs -
			dname_count_labels(iq->qinfo_out.qname);

		qout_orig = iq->qinfo_out.qname;
		qout_orig_len = iq->qinfo_out.qname_len;
		iq->qinfo_out.qname = iq->qchase.qname;
		iq->qinfo_out.qname_len = iq->qchase.qname_len;
		iq->minimise_count++;
		iq->timeout_count = 0;

		iter_dec_attempts(iq->dp, 1, ie->outbound_msg_retry);

		/* Limit number of iterations for QNAMEs with more
		 * than MAX_MINIMISE_COUNT labels. Send first MINIMISE_ONE_LAB
		 * labels of QNAME always individually.
		 */
		if(qchaselabs > MAX_MINIMISE_COUNT && labdiff > 1 && 
			iq->minimise_count > MINIMISE_ONE_LAB) {
			if(iq->minimise_count < MAX_MINIMISE_COUNT) {
				int multilabs = qchaselabs - 1 - 
					MINIMISE_ONE_LAB;
				int extralabs = multilabs / 
					MINIMISE_MULTIPLE_LABS;

				if (MAX_MINIMISE_COUNT - iq->minimise_count >= 
					multilabs % MINIMISE_MULTIPLE_LABS)
					/* Default behaviour is to add 1 label
					 * every iteration. Therefore, decrement
					 * the extralabs by 1 */
					extralabs--;
				if (extralabs < labdiff)
					labdiff -= extralabs;
				else
					labdiff = 1;
			}
			/* Last minimised iteration, send all labels with
			 * QTYPE=NS */
			else
				labdiff = 1;
		}

		if(labdiff > 1) {
			verbose(VERB_QUERY, "removing %d labels", labdiff-1);
			dname_remove_labels(&iq->qinfo_out.qname, 
				&iq->qinfo_out.qname_len, 
				labdiff-1);
		}
		if(labdiff < 1 || (labdiff < 2 
			&& (iq->qchase.qtype == LDNS_RR_TYPE_DS
			|| iq->qchase.qtype == LDNS_RR_TYPE_A)))
			/* Stop minimising this query, resolve "as usual" */
			iq->minimisation_state = DONOT_MINIMISE_STATE;
		else if(!qstate->no_cache_lookup) {
			struct dns_msg* msg = dns_cache_lookup(qstate->env, 
				iq->qinfo_out.qname, iq->qinfo_out.qname_len, 
				iq->qinfo_out.qtype, iq->qinfo_out.qclass, 
				qstate->query_flags, qstate->region, 
				qstate->env->scratch, 0, iq->dp->name,
				iq->dp->namelen);
			if(msg && FLAGS_GET_RCODE(msg->rep->flags) ==
				LDNS_RCODE_NOERROR)
				/* no need to send query if it is already 
				 * cached as NOERROR */
				return 1;
			if(msg && FLAGS_GET_RCODE(msg->rep->flags) ==
				LDNS_RCODE_NXDOMAIN &&
				qstate->env->need_to_validate &&
				qstate->env->cfg->harden_below_nxdomain) {
				if(msg->rep->security == sec_status_secure) {
					iq->response = msg;
					return final_state(iq);
				}
				if(msg->rep->security == sec_status_unchecked) {
					struct module_qstate* subq = NULL;
					if(!generate_sub_request(
						iq->qinfo_out.qname,
						iq->qinfo_out.qname_len,
						iq->qinfo_out.qtype,
						iq->qinfo_out.qclass,
						qstate, id, iq,
						INIT_REQUEST_STATE,
						FINISHED_STATE, &subq, 1, 1))
						verbose(VERB_ALGO,
						"could not validate NXDOMAIN "
						"response");
				}
			}
			if(msg && FLAGS_GET_RCODE(msg->rep->flags) ==
				LDNS_RCODE_NXDOMAIN) {
				/* return and add a label in the next
				 * minimisation iteration.
				 */
				return 1;
			}
		}
	}
	if(iq->minimisation_state == SKIP_MINIMISE_STATE) {
		if(iq->timeout_count < MAX_MINIMISE_TIMEOUT_COUNT)
			/* Do not increment qname, continue incrementing next 
			 * iteration */
			iq->minimisation_state = MINIMISE_STATE;
		else if(!qstate->env->cfg->qname_minimisation_strict)
			/* Too many time-outs detected for this QNAME and QTYPE.
			 * We give up, disable QNAME minimisation. */
			iq->minimisation_state = DONOT_MINIMISE_STATE;
	}
	if(iq->minimisation_state == DONOT_MINIMISE_STATE)
		iq->qinfo_out = iq->qchase;

	/* now find an answer to this query */
	/* see if authority zones have an answer */
	/* now we know the dp, we can check the auth zone for locally hosted
	 * contents */
	if(!iq->auth_zone_avoid && qstate->blacklist) {
		if(auth_zones_can_fallback(qstate->env->auth_zones,
			iq->dp->name, iq->dp->namelen, iq->qinfo_out.qclass)) {
			/* if cache is blacklisted and this zone allows us
			 * to fallback to the internet, then do so, and
			 * fetch results from the internet servers */
			iq->auth_zone_avoid = 1;
		}
	}
	if(iq->auth_zone_avoid) {
		iq->auth_zone_avoid = 0;
		auth_fallback = 1;
	} else if(auth_zones_lookup(qstate->env->auth_zones, &iq->qinfo_out,
		qstate->region, &iq->response, &auth_fallback, iq->dp->name,
		iq->dp->namelen)) {
		/* use this as a response to be processed by the iterator */
		if(verbosity >= VERB_ALGO) {
			log_dns_msg("msg from auth zone",
				&iq->response->qinfo, iq->response->rep);
		}
		if((iq->chase_flags&BIT_RD) && !(iq->response->rep->flags&BIT_AA)) {
			verbose(VERB_ALGO, "forwarder, ignoring referral from auth zone");
		} else {
			qstate->env->mesh->num_query_authzone_up++;
			iq->num_current_queries++;
			iq->chase_to_rd = 0;
			iq->dnssec_lame_query = 0;
			iq->auth_zone_response = 1;
			return next_state(iq, QUERY_RESP_STATE);
		}
	}
	iq->auth_zone_response = 0;
	if(auth_fallback == 0) {
		/* like we got servfail from the auth zone lookup, and
		 * no internet fallback */
		verbose(VERB_ALGO, "auth zone lookup failed, no fallback,"
			" servfail");
		errinf(qstate, "auth zone lookup failed, fallback is off");
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}
	if(iq->dp->auth_dp) {
		/* we wanted to fallback, but had no delegpt, only the
		 * auth zone generated delegpt, create an actual one */
		iq->auth_zone_avoid = 1;
		return next_state(iq, INIT_REQUEST_STATE);
	}
	/* but mostly, fallback==1 (like, when no such auth zone exists)
	 * and we continue with lookups */

	tf_policy = 0;
	/* < not <=, because although the array is large enough for <=, the
	 * generated query will immediately be discarded due to depth and
	 * that servfail is cached, which is not good as opportunism goes. */
	if(iq->depth < ie->max_dependency_depth
		&& iq->num_target_queries == 0
		&& (!iq->target_count || iq->target_count[TARGET_COUNT_NX]==0)
		&& iq->sent_count < TARGET_FETCH_STOP) {
		can_do_promisc = 1;
	}
	/* if the mesh query list is full, then do not waste cpu and sockets to
	 * fetch promiscuous targets. They can be looked up when needed. */
	if(can_do_promisc && !mesh_jostle_exceeded(qstate->env->mesh)) {
		tf_policy = ie->target_fetch_policy[iq->depth];
	}

	/* if in 0x20 fallback get as many targets as possible */
	if(iq->caps_fallback) {
		int extra = 0, ret;
		size_t naddr, nres, navail;
		if((ret=query_for_targets(qstate, iq, ie, id, -1, &extra))!=0) {
			errinf(qstate, "could not fetch nameservers for 0x20 fallback");
			if(ret == 1)
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		iq->num_target_queries += extra;
		target_count_increase(iq, extra);
		if(iq->num_target_queries > 0) {
			/* wait to get all targets, we want to try em */
			verbose(VERB_ALGO, "wait for all targets for fallback");
			qstate->ext_state[id] = module_wait_reply;
			/* undo qname minimise step because we'll get back here
			 * to do it again */
			if(qout_orig && iq->minimise_count > 0) {
				iq->minimise_count--;
				iq->qinfo_out.qname = qout_orig;
				iq->qinfo_out.qname_len = qout_orig_len;
			}
			return 0;
		}
		/* did we do enough fallback queries already? */
		delegpt_count_addr(iq->dp, &naddr, &nres, &navail);
		/* the current caps_server is the number of fallbacks sent.
		 * the original query is one that matched too, so we have
		 * caps_server+1 number of matching queries now */
		if(iq->caps_server+1 >= naddr*3 ||
			iq->caps_server*2+2 >= (size_t)ie->max_sent_count) {
			/* *2 on sentcount check because ipv6 may fail */
			/* we're done, process the response */
			verbose(VERB_ALGO, "0x20 fallback had %d responses "
				"match for %d wanted, done.", 
				(int)iq->caps_server+1, (int)naddr*3);
			iq->response = iq->caps_response;
			iq->caps_fallback = 0;
			iter_dec_attempts(iq->dp, 3, ie->outbound_msg_retry); /* space for fallback */
			iq->num_current_queries++; /* RespState decrements it*/
			iq->referral_count++; /* make sure we don't loop */
			iq->sent_count = 0;
			iq->dp_target_count = 0;
			iq->state = QUERY_RESP_STATE;
			return 1;
		}
		verbose(VERB_ALGO, "0x20 fallback number %d", 
			(int)iq->caps_server);

	/* if there is a policy to fetch missing targets 
	 * opportunistically, do it. we rely on the fact that once a 
	 * query (or queries) for a missing name have been issued, 
	 * they will not show up again. */
	} else if(tf_policy != 0) {
		int extra = 0;
		verbose(VERB_ALGO, "attempt to get extra %d targets", 
			tf_policy);
		(void)query_for_targets(qstate, iq, ie, id, tf_policy, &extra);
		/* errors ignored, these targets are not strictly necessary for
		 * this result, we do not have to reply with SERVFAIL */
		iq->num_target_queries += extra;
		target_count_increase(iq, extra);
	}

	/* Add the current set of unused targets to our queue. */
	delegpt_add_unused_targets(iq->dp);

	if(qstate->env->auth_zones) {
		uint8_t* sname = NULL;
		size_t snamelen = 0;
		/* apply rpz triggers at query time; nameserver IP and dname */
		struct dns_msg* forged_response_after_cname;
		struct dns_msg* forged_response = rpz_callback_from_iterator_module(qstate, iq);
		int count = 0;
		while(forged_response && reply_find_rrset_section_an(
			forged_response->rep, iq->qchase.qname,
			iq->qchase.qname_len, LDNS_RR_TYPE_CNAME,
			iq->qchase.qclass) &&
			iq->qchase.qtype != LDNS_RR_TYPE_CNAME &&
			count++ < ie->max_query_restarts) {
			/* another cname to follow */
			if(!handle_cname_response(qstate, iq, forged_response,
				&sname, &snamelen)) {
				errinf(qstate, "malloc failure, CNAME info");
				return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			}
			iq->qchase.qname = sname;
			iq->qchase.qname_len = snamelen;
			forged_response_after_cname =
				rpz_callback_from_iterator_cname(qstate, iq);
			if(forged_response_after_cname) {
				forged_response = forged_response_after_cname;
			} else {
				/* Follow the CNAME with a query restart */
				iq->deleg_msg = NULL;
				iq->dp = NULL;
				iq->dsns_point = NULL;
				iq->auth_zone_response = 0;
				iq->refetch_glue = 0;
				iq->query_restart_count++;
				iq->sent_count = 0;
				iq->dp_target_count = 0;
				if(qstate->env->cfg->qname_minimisation)
					iq->minimisation_state = INIT_MINIMISE_STATE;
				outbound_list_clear(&iq->outlist);
				iq->num_current_queries = 0;
				fptr_ok(fptr_whitelist_modenv_detach_subs(
					qstate->env->detach_subs));
				(*qstate->env->detach_subs)(qstate);
				iq->num_target_queries = 0;
				return next_state(iq, INIT_REQUEST_STATE);
			}
		}
		if(forged_response != NULL) {
			qstate->ext_state[id] = module_finished;
			qstate->return_rcode = LDNS_RCODE_NOERROR;
			qstate->return_msg = forged_response;
			iq->response = forged_response;
			next_state(iq, FINISHED_STATE);
			if(!iter_prepend(iq, qstate->return_msg, qstate->region)) {
				log_err("rpz: prepend rrsets: out of memory");
				return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			}
			return 0;
		}
	}

	/* Select the next usable target, filtering out unsuitable targets. */
	target = iter_server_selection(ie, qstate->env, iq->dp,
		iq->dp->name, iq->dp->namelen, iq->qchase.qtype,
		&iq->dnssec_lame_query, &iq->chase_to_rd,
		iq->num_target_queries, qstate->blacklist,
		qstate->prefetch_leeway);

	/* If no usable target was selected... */
	if(!target) {
		/* Here we distinguish between three states: generate a new 
		 * target query, just wait, or quit (with a SERVFAIL).
		 * We have the following information: number of active 
		 * target queries, number of active current queries, 
		 * the presence of missing targets at this delegation 
		 * point, and the given query target policy. */
		
		/* Check for the wait condition. If this is true, then 
		 * an action must be taken. */
		if(iq->num_target_queries==0 && iq->num_current_queries==0) {
			/* If there is nothing to wait for, then we need 
			 * to distinguish between generating (a) new target 
			 * query, or failing. */
			if(delegpt_count_missing_targets(iq->dp, NULL) > 0) {
				int qs = 0, ret;
				verbose(VERB_ALGO, "querying for next "
					"missing target");
				if((ret=query_for_targets(qstate, iq, ie, id, 
					1, &qs))!=0) {
					errinf(qstate, "could not fetch nameserver");
					errinf_dname(qstate, "at zone", iq->dp->name);
					if(ret == 1)
					return error_response(qstate, id,
						LDNS_RCODE_SERVFAIL);
					return error_response_cache(qstate, id,
						LDNS_RCODE_SERVFAIL);
				}
				if(qs == 0 && 
				   delegpt_count_missing_targets(iq->dp, NULL) == 0){
					/* it looked like there were missing
					 * targets, but they did not turn up.
					 * Try the bad choices again (if any),
					 * when we get back here missing==0,
					 * so this is not a loop. */
					return 1;
				}
				if(qs == 0) {
					/* There should be targets now, and
					 * if there are not, it should not
					 * wait for no targets. Stop it from
					 * waiting forever, or looping to
					 * here, as a safeguard. */
					errinf(qstate, "could not generate nameserver lookups");
					errinf_dname(qstate, "at zone", iq->dp->name);
					return error_response(qstate, id,
						LDNS_RCODE_SERVFAIL);
				}
				iq->num_target_queries += qs;
				target_count_increase(iq, qs);
			}
			/* Since a target query might have been made, we 
			 * need to check again. */
			if(iq->num_target_queries == 0) {
				/* if in capsforid fallback, instead of last
				 * resort, we agree with the current reply
				 * we have (if any) (our count of addrs bad)*/
				if(iq->caps_fallback && iq->caps_reply) {
					/* we're done, process the response */
					verbose(VERB_ALGO, "0x20 fallback had %d responses, "
						"but no more servers except "
						"last resort, done.", 
						(int)iq->caps_server+1);
					iq->response = iq->caps_response;
					iq->caps_fallback = 0;
					iter_dec_attempts(iq->dp, 3, ie->outbound_msg_retry); /* space for fallback */
					iq->num_current_queries++; /* RespState decrements it*/
					iq->referral_count++; /* make sure we don't loop */
					iq->sent_count = 0;
					iq->dp_target_count = 0;
					iq->state = QUERY_RESP_STATE;
					return 1;
				}
				return processLastResort(qstate, iq, ie, id);
			}
		}

		/* otherwise, we have no current targets, so submerge 
		 * until one of the target or direct queries return. */
		verbose(VERB_ALGO, "no current targets");
		check_waiting_queries(iq, qstate, id);
		/* undo qname minimise step because we'll get back here
		 * to do it again */
		if(qout_orig && iq->minimise_count > 0) {
			iq->minimise_count--;
			iq->qinfo_out.qname = qout_orig;
			iq->qinfo_out.qname_len = qout_orig_len;
		}
		return 0;
	}

	/* We have a target. We could have created promiscuous target
	 * queries but we are currently under pressure (mesh_jostle_exceeded).
	 * If we are configured to allow promiscuous target queries and haven't
	 * gone out to the network for a target query for this delegation, then
	 * it is possible to slip in a promiscuous one with a 1/10 chance. */
	if(can_do_promisc && tf_policy == 0 && iq->depth == 0
		&& iq->depth < ie->max_dependency_depth
		&& ie->target_fetch_policy[iq->depth] != 0
		&& iq->dp_target_count == 0
		&& !ub_random_max(qstate->env->rnd, 10)) {
		int extra = 0;
		verbose(VERB_ALGO, "available target exists in cache but "
			"attempt to get extra 1 target");
		(void)query_for_targets(qstate, iq, ie, id, 1, &extra);
		/* errors ignored, these targets are not strictly necessary for
		* this result, we do not have to reply with SERVFAIL */
		if(extra > 0) {
			iq->num_target_queries += extra;
			target_count_increase(iq, extra);
			check_waiting_queries(iq, qstate, id);
			/* undo qname minimise step because we'll get back here
			 * to do it again */
			if(qout_orig && iq->minimise_count > 0) {
				iq->minimise_count--;
				iq->qinfo_out.qname = qout_orig;
				iq->qinfo_out.qname_len = qout_orig_len;
			}
			return 0;
		}
	}

	target_count_increase_global_quota(iq, 1);
	if(iq->target_count && iq->target_count[TARGET_COUNT_GLOBAL_QUOTA]
		> MAX_GLOBAL_QUOTA) {
		char s[LDNS_MAX_DOMAINLEN];
		dname_str(qstate->qinfo.qname, s);
		verbose(VERB_QUERY, "request %s has exceeded the maximum "
			"global quota on number of upstream queries %d", s,
			iq->target_count[TARGET_COUNT_GLOBAL_QUOTA]);
		return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* Do not check ratelimit for forwarding queries or if we already got a
	 * pass. */
	sq_check_ratelimit = (!(iq->chase_flags & BIT_RD) && !iq->ratelimit_ok);
	/* We have a valid target. */
	if(verbosity >= VERB_QUERY) {
		log_query_info(VERB_QUERY, "sending query:", &iq->qinfo_out);
		log_name_addr(VERB_QUERY, "sending to target:", iq->dp->name,
			&target->addr, target->addrlen);
		verbose(VERB_ALGO, "dnssec status: %s%s",
			iq->dnssec_expected?"expected": "not expected",
			iq->dnssec_lame_query?" but lame_query anyway": "");
	}

	real_addr = target->addr;
	real_addrlen = target->addrlen;

	if(ie->nat64.use_nat64 && target->addr.ss_family == AF_INET) {
		addr_to_nat64(&target->addr, &ie->nat64.nat64_prefix_addr,
			ie->nat64.nat64_prefix_addrlen, ie->nat64.nat64_prefix_net,
			&real_addr, &real_addrlen);
		log_name_addr(VERB_QUERY, "applied NAT64:",
			iq->dp->name, &real_addr, real_addrlen);
	}

	fptr_ok(fptr_whitelist_modenv_send_query(qstate->env->send_query));
	outq = (*qstate->env->send_query)(&iq->qinfo_out,
		iq->chase_flags | (iq->chase_to_rd?BIT_RD:0),
		/* unset CD if to forwarder(RD set) and not dnssec retry
		 * (blacklist nonempty) and no trust-anchors are configured
		 * above the qname or on the first attempt when dnssec is on */
		(qstate->env->cfg->disable_edns_do?0:EDNS_DO)|
		((iq->chase_to_rd||(iq->chase_flags&BIT_RD)!=0)&&
		!qstate->blacklist&&(!iter_qname_indicates_dnssec(qstate->env,
		&iq->qinfo_out)||target->attempts==1)?0:BIT_CD),
		iq->dnssec_expected, iq->caps_fallback || is_caps_whitelisted(
		ie, iq), sq_check_ratelimit, &real_addr, real_addrlen,
		iq->dp->name, iq->dp->namelen,
		(iq->dp->tcp_upstream || qstate->env->cfg->tcp_upstream),
		(iq->dp->ssl_upstream || qstate->env->cfg->ssl_upstream),
		target->tls_auth_name, qstate, &sq_was_ratelimited);
	if(!outq) {
		if(sq_was_ratelimited) {
			lock_basic_lock(&ie->queries_ratelimit_lock);
			ie->num_queries_ratelimited++;
			lock_basic_unlock(&ie->queries_ratelimit_lock);
			verbose(VERB_ALGO, "query exceeded ratelimits");
			qstate->was_ratelimited = 1;
			errinf_dname(qstate, "exceeded ratelimit for zone",
				iq->dp->name);
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		log_addr(VERB_QUERY, "error sending query to auth server",
			&real_addr, real_addrlen);
		if(qstate->env->cfg->qname_minimisation)
			iq->minimisation_state = SKIP_MINIMISE_STATE;
		return next_state(iq, QUERYTARGETS_STATE);
	}
	outbound_list_insert(&iq->outlist, outq);
	iq->num_current_queries++;
	iq->sent_count++;
	qstate->ext_state[id] = module_wait_reply;

	return 0;
}

/** find NS rrset in given list */
static struct ub_packed_rrset_key*
find_NS(struct reply_info* rep, size_t from, size_t to)
{
	size_t i;
	for(i=from; i<to; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_NS)
			return rep->rrsets[i];
	}
	return NULL;
}


/** 
 * Process the query response. All queries end up at this state first. This
 * process generally consists of analyzing the response and routing the
 * event to the next state (either bouncing it back to a request state, or
 * terminating the processing for this event).
 * 
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param ie: iterator shared global environment.
 * @param id: module id.
 * @return true if the event requires more immediate processing, false if
 *         not. This is generally only true when forwarding the request to
 *         the final state (i.e., on answer).
 */
static int
processQueryResponse(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	int dnsseclame = 0, origtypecname = 0, orig_empty_nodata_found;
	enum response_type type;

	iq->num_current_queries--;

	if(!inplace_cb_query_response_call(qstate->env, qstate, iq->response))
		log_err("unable to call query_response callback");

	if(iq->response == NULL) {
		/* Don't increment qname when QNAME minimisation is enabled */
		if(qstate->env->cfg->qname_minimisation) {
			iq->minimisation_state = SKIP_MINIMISE_STATE;
		}
		iq->timeout_count++;
		iq->chase_to_rd = 0;
		iq->dnssec_lame_query = 0;
		verbose(VERB_ALGO, "query response was timeout");
		return next_state(iq, QUERYTARGETS_STATE);
	}
	iq->timeout_count = 0;
	orig_empty_nodata_found = iq->empty_nodata_found;
	type = response_type_from_server(
		(int)((iq->chase_flags&BIT_RD) || iq->chase_to_rd),
		iq->response, &iq->qinfo_out, iq->dp, &iq->empty_nodata_found);
	iq->chase_to_rd = 0;
	/* remove TC flag, if this is erroneously set by TCP upstream */
	iq->response->rep->flags &= ~BIT_TC;
	if(orig_empty_nodata_found != iq->empty_nodata_found &&
		iq->empty_nodata_found < EMPTY_NODATA_RETRY_COUNT) {
		/* try to search at another server */
		if(qstate->reply) {
			struct delegpt_addr* a = delegpt_find_addr(
				iq->dp, &qstate->reply->remote_addr,
				qstate->reply->remote_addrlen);
			/* make selection disprefer it */
			if(a) a->lame = 1;
		}
		return next_state(iq, QUERYTARGETS_STATE);
	}
	if(type == RESPONSE_TYPE_REFERRAL && (iq->chase_flags&BIT_RD) &&
		!iq->auth_zone_response) {
		/* When forwarding (RD bit is set), we handle referrals 
		 * differently. No queries should be sent elsewhere */
		type = RESPONSE_TYPE_ANSWER;
	}
	if(!qstate->env->cfg->disable_dnssec_lame_check && iq->dnssec_expected 
                && !iq->dnssec_lame_query &&
		!(iq->chase_flags&BIT_RD) 
		&& iq->sent_count < DNSSEC_LAME_DETECT_COUNT
		&& type != RESPONSE_TYPE_LAME 
		&& type != RESPONSE_TYPE_REC_LAME 
		&& type != RESPONSE_TYPE_THROWAWAY 
		&& type != RESPONSE_TYPE_UNTYPED) {
		/* a possible answer, see if it is missing DNSSEC */
		/* but not when forwarding, so we dont mark fwder lame */
		if(!iter_msg_has_dnssec(iq->response)) {
			/* Mark this address as dnsseclame in this dp,
			 * because that will make serverselection disprefer
			 * it, but also, once it is the only final option,
			 * use dnssec-lame-bypass if it needs to query there.*/
			if(qstate->reply) {
				struct delegpt_addr* a = delegpt_find_addr(
					iq->dp, &qstate->reply->remote_addr,
					qstate->reply->remote_addrlen);
				if(a) a->dnsseclame = 1;
			}
			/* test the answer is from the zone we expected,
		 	 * otherwise, (due to parent,child on same server), we
		 	 * might mark the server,zone lame inappropriately */
			if(!iter_msg_from_zone(iq->response, iq->dp, type,
				iq->qchase.qclass))
				qstate->reply = NULL;
			type = RESPONSE_TYPE_LAME;
			dnsseclame = 1;
		}
	} else iq->dnssec_lame_query = 0;
	/* see if referral brings us close to the target */
	if(type == RESPONSE_TYPE_REFERRAL) {
		struct ub_packed_rrset_key* ns = find_NS(
			iq->response->rep, iq->response->rep->an_numrrsets,
			iq->response->rep->an_numrrsets 
			+ iq->response->rep->ns_numrrsets);
		if(!ns) ns = find_NS(iq->response->rep, 0, 
				iq->response->rep->an_numrrsets);
		if(!ns || !dname_strict_subdomain_c(ns->rk.dname, iq->dp->name) 
			|| !dname_subdomain_c(iq->qchase.qname, ns->rk.dname)){
			verbose(VERB_ALGO, "bad referral, throwaway");
			type = RESPONSE_TYPE_THROWAWAY;
		} else
			iter_scrub_ds(iq->response, ns, iq->dp->name);
	} else iter_scrub_ds(iq->response, NULL, NULL);
	if(type == RESPONSE_TYPE_THROWAWAY &&
		FLAGS_GET_RCODE(iq->response->rep->flags) == LDNS_RCODE_YXDOMAIN) {
		/* YXDOMAIN is a permanent error, no need to retry */
		type = RESPONSE_TYPE_ANSWER;
	}
	if(type == RESPONSE_TYPE_CNAME)
		origtypecname = 1;
	if(type == RESPONSE_TYPE_CNAME && iq->response->rep->an_numrrsets >= 1
		&& ntohs(iq->response->rep->rrsets[0]->rk.type) == LDNS_RR_TYPE_DNAME) {
		uint8_t* sname = NULL;
		size_t snamelen = 0;
		get_cname_target(iq->response->rep->rrsets[0], &sname,
			&snamelen);
		if(snamelen && dname_subdomain_c(sname, iq->response->rep->rrsets[0]->rk.dname)) {
			/* DNAME to a subdomain loop; do not recurse */
			type = RESPONSE_TYPE_ANSWER;
		}
	}
	if(type == RESPONSE_TYPE_CNAME &&
		iq->qchase.qtype == LDNS_RR_TYPE_CNAME &&
		iq->minimisation_state == MINIMISE_STATE &&
		query_dname_compare(iq->qchase.qname, iq->qinfo_out.qname) == 0) {
		/* The minimised query for full QTYPE and hidden QTYPE can be
		 * classified as CNAME response type, even when the original
		 * QTYPE=CNAME. This should be treated as answer response type.
		 */
		type = RESPONSE_TYPE_ANSWER;
	}

	/* handle each of the type cases */
	if(type == RESPONSE_TYPE_ANSWER) {
		/* ANSWER type responses terminate the query algorithm, 
		 * so they sent on their */
		if(verbosity >= VERB_DETAIL) {
			verbose(VERB_DETAIL, "query response was %s",
				FLAGS_GET_RCODE(iq->response->rep->flags)
				==LDNS_RCODE_NXDOMAIN?"NXDOMAIN ANSWER":
				(iq->response->rep->an_numrrsets?"ANSWER":
				"nodata ANSWER"));
		}
		/* if qtype is DS, check we have the right level of answer,
		 * like grandchild answer but we need the middle, reject it */
		if(iq->qchase.qtype == LDNS_RR_TYPE_DS && !iq->dsns_point
			&& !(iq->chase_flags&BIT_RD)
			&& iter_ds_toolow(iq->response, iq->dp)
			&& iter_dp_cangodown(&iq->qchase, iq->dp)) {
			/* close down outstanding requests to be discarded */
			outbound_list_clear(&iq->outlist);
			iq->num_current_queries = 0;
			fptr_ok(fptr_whitelist_modenv_detach_subs(
				qstate->env->detach_subs));
			(*qstate->env->detach_subs)(qstate);
			iq->num_target_queries = 0;
			return processDSNSFind(qstate, iq, id);
		}
		if(iq->qchase.qtype == LDNS_RR_TYPE_DNSKEY && SERVE_EXPIRED
			&& qstate->is_valrec &&
			reply_find_answer_rrset(&iq->qchase, iq->response->rep) != NULL) {
			/* clean out the authority section, if any, so it
			 * does not overwrite dnssec valid data in the
			 * validation recursion lookup. */
			verbose(VERB_ALGO, "make DNSKEY minimal for serve "
				"expired");
			iter_make_minimal(iq->response->rep);
		}
		if(!qstate->no_cache_store)
			iter_dns_store(qstate->env, &iq->response->qinfo,
				iq->response->rep,
				iq->qchase.qtype != iq->response->qinfo.qtype,
				qstate->prefetch_leeway,
				iq->dp&&iq->dp->has_parent_side_NS,
				qstate->region, qstate->query_flags,
				qstate->qstarttime, qstate->is_valrec);
		/* close down outstanding requests to be discarded */
		outbound_list_clear(&iq->outlist);
		iq->num_current_queries = 0;
		fptr_ok(fptr_whitelist_modenv_detach_subs(
			qstate->env->detach_subs));
		(*qstate->env->detach_subs)(qstate);
		iq->num_target_queries = 0;
		if(qstate->reply)
			sock_list_insert(&qstate->reply_origin,
				&qstate->reply->remote_addr,
				qstate->reply->remote_addrlen, qstate->region);
		if(iq->minimisation_state != DONOT_MINIMISE_STATE
			&& !(iq->chase_flags & BIT_RD)) {
			if(FLAGS_GET_RCODE(iq->response->rep->flags) != 
				LDNS_RCODE_NOERROR) {
				if(qstate->env->cfg->qname_minimisation_strict) {
					if(FLAGS_GET_RCODE(iq->response->rep->flags) ==
						LDNS_RCODE_NXDOMAIN) {
						iter_scrub_nxdomain(iq->response);
						return final_state(iq);
					}
					return error_response_cache(qstate, id,
						LDNS_RCODE_SERVFAIL);
				}
				/* Best effort qname-minimisation. 
				 * Stop minimising and send full query when
				 * RCODE is not NOERROR. */
				iq->minimisation_state = DONOT_MINIMISE_STATE;
			}
			if(FLAGS_GET_RCODE(iq->response->rep->flags) ==
				LDNS_RCODE_NXDOMAIN && !origtypecname) {
				/* Stop resolving when NXDOMAIN is DNSSEC
				 * signed. Based on assumption that nameservers
				 * serving signed zones do not return NXDOMAIN
				 * for empty-non-terminals. */
				/* If this response is actually a CNAME type,
				 * the nxdomain rcode may not be for the qname,
				 * and so it is not the final response. */
				if(iq->dnssec_expected)
					return final_state(iq);
				/* Make subrequest to validate intermediate
				 * NXDOMAIN if harden-below-nxdomain is
				 * enabled. */
				if(qstate->env->cfg->harden_below_nxdomain &&
					qstate->env->need_to_validate) {
					struct module_qstate* subq = NULL;
					log_query_info(VERB_QUERY,
						"schedule NXDOMAIN validation:",
						&iq->response->qinfo);
					if(!generate_sub_request(
						iq->response->qinfo.qname,
						iq->response->qinfo.qname_len,
						iq->response->qinfo.qtype,
						iq->response->qinfo.qclass,
						qstate, id, iq,
						INIT_REQUEST_STATE,
						FINISHED_STATE, &subq, 1, 1))
						verbose(VERB_ALGO,
						"could not validate NXDOMAIN "
						"response");
				}
			}
			return next_state(iq, QUERYTARGETS_STATE);
		}
		return final_state(iq);
	} else if(type == RESPONSE_TYPE_REFERRAL) {
		struct delegpt* old_dp = NULL;
		/* REFERRAL type responses get a reset of the 
		 * delegation point, and back to the QUERYTARGETS_STATE. */
		verbose(VERB_DETAIL, "query response was REFERRAL");

		/* if hardened, only store referral if we asked for it */
		if(!qstate->no_cache_store &&
		(!qstate->env->cfg->harden_referral_path ||
		    (  qstate->qinfo.qtype == LDNS_RR_TYPE_NS 
			&& (qstate->query_flags&BIT_RD) 
			&& !(qstate->query_flags&BIT_CD)
			   /* we know that all other NS rrsets are scrubbed
			    * away, thus on referral only one is left.
			    * see if that equals the query name... */
			&& ( /* auth section, but sometimes in answer section*/
			  reply_find_rrset_section_ns(iq->response->rep,
				iq->qchase.qname, iq->qchase.qname_len,
				LDNS_RR_TYPE_NS, iq->qchase.qclass)
			  || reply_find_rrset_section_an(iq->response->rep,
				iq->qchase.qname, iq->qchase.qname_len,
				LDNS_RR_TYPE_NS, iq->qchase.qclass)
			  )
		    ))) {
			/* Store the referral under the current query */
			/* no prefetch-leeway, since its not the answer */
			iter_dns_store(qstate->env, &iq->response->qinfo,
				iq->response->rep, 1, 0, 0, NULL, 0,
				qstate->qstarttime, qstate->is_valrec);
			if(iq->store_parent_NS)
				iter_store_parentside_NS(qstate->env, 
					iq->response->rep);
			if(qstate->env->neg_cache)
				val_neg_addreferral(qstate->env->neg_cache, 
					iq->response->rep, iq->dp->name);
		}
		/* store parent-side-in-zone-glue, if directly queried for */
		if(!qstate->no_cache_store && iq->query_for_pside_glue
			&& !iq->pside_glue) {
				iq->pside_glue = reply_find_rrset(iq->response->rep, 
					iq->qchase.qname, iq->qchase.qname_len, 
					iq->qchase.qtype, iq->qchase.qclass);
				if(iq->pside_glue) {
					log_rrset_key(VERB_ALGO, "found parent-side "
						"glue", iq->pside_glue);
					iter_store_parentside_rrset(qstate->env,
						iq->pside_glue);
				}
		}

		/* Reset the event state, setting the current delegation 
		 * point to the referral. */
		iq->deleg_msg = iq->response;
		/* Keep current delegation point for label comparison */
		old_dp = iq->dp;
		iq->dp = delegpt_from_message(iq->response, qstate->region);
		if (qstate->env->cfg->qname_minimisation)
			iq->minimisation_state = INIT_MINIMISE_STATE;
		if(!iq->dp) {
			errinf(qstate, "malloc failure, for delegation point");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		if(old_dp->namelabs + 1 < iq->dp->namelabs) {
			/* We got a grandchild delegation (more than one label
			 * difference) than expected. Check for in-between
			 * delegations in the cache and remove them.
			 * They could prove problematic when they expire
			 * and rrset_expired_above() encounters them during
			 * delegation cache lookups. */
			uint8_t* qname = iq->dp->name;
			size_t qnamelen = iq->dp->namelen;
			rrset_cache_remove_above(qstate->env->rrset_cache,
				&qname, &qnamelen, LDNS_RR_TYPE_NS,
				iq->qchase.qclass, *qstate->env->now,
				old_dp->name, old_dp->namelen);
		}
		if(!cache_fill_missing(qstate->env, iq->qchase.qclass, 
			qstate->region, iq->dp, 0)) {
			errinf(qstate, "malloc failure, copy extra info into delegation point");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		if(iq->store_parent_NS && query_dname_compare(iq->dp->name,
			iq->store_parent_NS->name) == 0)
			iter_merge_retry_counts(iq->dp, iq->store_parent_NS,
				ie->outbound_msg_retry);
		delegpt_log(VERB_ALGO, iq->dp);
		/* Count this as a referral. */
		iq->referral_count++;
		iq->sent_count = 0;
		iq->dp_target_count = 0;
		/* see if the next dp is a trust anchor, or a DS was sent
		 * along, indicating dnssec is expected for next zone */
		iq->dnssec_expected = iter_indicates_dnssec(qstate->env, 
			iq->dp, iq->response, iq->qchase.qclass);
		/* if dnssec, validating then also fetch the key for the DS */
		if(iq->dnssec_expected && qstate->env->cfg->prefetch_key &&
			!(qstate->query_flags&BIT_CD))
			generate_dnskey_prefetch(qstate, iq, id);

		/* spawn off NS and addr to auth servers for the NS we just
		 * got in the referral. This gets authoritative answer
		 * (answer section trust level) rrset. 
		 * right after, we detach the subs, answer goes to cache. */
		if(qstate->env->cfg->harden_referral_path)
			generate_ns_check(qstate, iq, id);

		/* stop current outstanding queries. 
		 * FIXME: should the outstanding queries be waited for and
		 * handled? Say by a subquery that inherits the outbound_entry.
		 */
		outbound_list_clear(&iq->outlist);
		iq->num_current_queries = 0;
		fptr_ok(fptr_whitelist_modenv_detach_subs(
			qstate->env->detach_subs));
		(*qstate->env->detach_subs)(qstate);
		iq->num_target_queries = 0;
		iq->response = NULL;
		iq->fail_addr_type = 0;
		verbose(VERB_ALGO, "cleared outbound list for next round");
		return next_state(iq, QUERYTARGETS_STATE);
	} else if(type == RESPONSE_TYPE_CNAME) {
		uint8_t* sname = NULL;
		size_t snamelen = 0;
		/* CNAME type responses get a query restart (i.e., get a 
		 * reset of the query state and go back to INIT_REQUEST_STATE).
		 */
		verbose(VERB_DETAIL, "query response was CNAME");
		if(verbosity >= VERB_ALGO)
			log_dns_msg("cname msg", &iq->response->qinfo, 
				iq->response->rep);
		/* if qtype is DS, check we have the right level of answer,
		 * like grandchild answer but we need the middle, reject it */
		if(iq->qchase.qtype == LDNS_RR_TYPE_DS && !iq->dsns_point
			&& !(iq->chase_flags&BIT_RD)
			&& iter_ds_toolow(iq->response, iq->dp)
			&& iter_dp_cangodown(&iq->qchase, iq->dp)) {
			outbound_list_clear(&iq->outlist);
			iq->num_current_queries = 0;
			fptr_ok(fptr_whitelist_modenv_detach_subs(
				qstate->env->detach_subs));
			(*qstate->env->detach_subs)(qstate);
			iq->num_target_queries = 0;
			return processDSNSFind(qstate, iq, id);
		}
		/* Process the CNAME response. */
		if(!handle_cname_response(qstate, iq, iq->response, 
			&sname, &snamelen)) {
			errinf(qstate, "malloc failure, CNAME info");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		/* cache the CNAME response under the current query */
		/* NOTE : set referral=1, so that rrsets get stored but not 
		 * the partial query answer (CNAME only). */
		/* prefetchleeway applied because this updates answer parts */
		if(!qstate->no_cache_store)
			iter_dns_store(qstate->env, &iq->response->qinfo,
				iq->response->rep, 1, qstate->prefetch_leeway,
				iq->dp&&iq->dp->has_parent_side_NS, NULL,
				qstate->query_flags, qstate->qstarttime,
				qstate->is_valrec);
		/* set the current request's qname to the new value. */
		iq->qchase.qname = sname;
		iq->qchase.qname_len = snamelen;
		if(qstate->env->auth_zones) {
			/* apply rpz qname triggers after cname */
			struct dns_msg* forged_response =
				rpz_callback_from_iterator_cname(qstate, iq);
			int count = 0;
			while(forged_response && reply_find_rrset_section_an(
				forged_response->rep, iq->qchase.qname,
				iq->qchase.qname_len, LDNS_RR_TYPE_CNAME,
				iq->qchase.qclass) &&
				iq->qchase.qtype != LDNS_RR_TYPE_CNAME &&
				count++ < ie->max_query_restarts) {
				/* another cname to follow */
				if(!handle_cname_response(qstate, iq, forged_response,
					&sname, &snamelen)) {
					errinf(qstate, "malloc failure, CNAME info");
					return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
				}
				iq->qchase.qname = sname;
				iq->qchase.qname_len = snamelen;
				forged_response =
					rpz_callback_from_iterator_cname(qstate, iq);
			}
			if(forged_response != NULL) {
				qstate->ext_state[id] = module_finished;
				qstate->return_rcode = LDNS_RCODE_NOERROR;
				qstate->return_msg = forged_response;
				iq->response = forged_response;
				next_state(iq, FINISHED_STATE);
				if(!iter_prepend(iq, qstate->return_msg, qstate->region)) {
					log_err("rpz: after cname, prepend rrsets: out of memory");
					return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
				}
				qstate->return_msg->qinfo = qstate->qinfo;
				return 0;
			}
		}
		/* Clear the query state, since this is a query restart. */
		iq->deleg_msg = NULL;
		iq->dp = NULL;
		iq->dsns_point = NULL;
		iq->auth_zone_response = 0;
		iq->sent_count = 0;
		iq->dp_target_count = 0;
		if(iq->minimisation_state != MINIMISE_STATE)
			/* Only count as query restart when it is not an extra
			 * query as result of qname minimisation. */
			iq->query_restart_count++;
		if(qstate->env->cfg->qname_minimisation)
			iq->minimisation_state = INIT_MINIMISE_STATE;

		/* stop current outstanding queries. 
		 * FIXME: should the outstanding queries be waited for and
		 * handled? Say by a subquery that inherits the outbound_entry.
		 */
		outbound_list_clear(&iq->outlist);
		iq->num_current_queries = 0;
		fptr_ok(fptr_whitelist_modenv_detach_subs(
			qstate->env->detach_subs));
		(*qstate->env->detach_subs)(qstate);
		iq->num_target_queries = 0;
		if(qstate->reply)
			sock_list_insert(&qstate->reply_origin,
				&qstate->reply->remote_addr,
				qstate->reply->remote_addrlen, qstate->region);
		verbose(VERB_ALGO, "cleared outbound list for query restart");
		/* go to INIT_REQUEST_STATE for new qname. */
		return next_state(iq, INIT_REQUEST_STATE);
	} else if(type == RESPONSE_TYPE_LAME) {
		/* Cache the LAMEness. */
		verbose(VERB_DETAIL, "query response was %sLAME",
			dnsseclame?"DNSSEC ":"");
		if(!dname_subdomain_c(iq->qchase.qname, iq->dp->name)) {
			log_err("mark lame: mismatch in qname and dpname");
			/* throwaway this reply below */
		} else if(qstate->reply) {
			/* need addr for lameness cache, but we may have
			 * gotten this from cache, so test to be sure */
			if(!infra_set_lame(qstate->env->infra_cache,
				&qstate->reply->remote_addr,
				qstate->reply->remote_addrlen,
				iq->dp->name, iq->dp->namelen,
				*qstate->env->now, dnsseclame, 0,
				iq->qchase.qtype))
				log_err("mark host lame: out of memory");
		}
	} else if(type == RESPONSE_TYPE_REC_LAME) {
		/* Cache the LAMEness. */
		verbose(VERB_DETAIL, "query response REC_LAME: "
			"recursive but not authoritative server");
		if(!dname_subdomain_c(iq->qchase.qname, iq->dp->name)) {
			log_err("mark rec_lame: mismatch in qname and dpname");
			/* throwaway this reply below */
		} else if(qstate->reply) {
			/* need addr for lameness cache, but we may have
			 * gotten this from cache, so test to be sure */
			verbose(VERB_DETAIL, "mark as REC_LAME");
			if(!infra_set_lame(qstate->env->infra_cache, 
				&qstate->reply->remote_addr,
				qstate->reply->remote_addrlen,
				iq->dp->name, iq->dp->namelen,
				*qstate->env->now, 0, 1, iq->qchase.qtype))
				log_err("mark host lame: out of memory");
		} 
	} else if(type == RESPONSE_TYPE_THROWAWAY) {
		/* LAME and THROWAWAY responses are handled the same way. 
		 * In this case, the event is just sent directly back to 
		 * the QUERYTARGETS_STATE without resetting anything, 
		 * because, clearly, the next target must be tried. */
		verbose(VERB_DETAIL, "query response was THROWAWAY");
	} else {
		log_warn("A query response came back with an unknown type: %d",
			(int)type);
	}

	/* LAME, THROWAWAY and "unknown" all end up here.
	 * Recycle to the QUERYTARGETS state to hopefully try a 
	 * different target. */
	if (qstate->env->cfg->qname_minimisation &&
		!qstate->env->cfg->qname_minimisation_strict)
		iq->minimisation_state = DONOT_MINIMISE_STATE;
	if(iq->auth_zone_response) {
		/* can we fallback? */
		iq->auth_zone_response = 0;
		if(!auth_zones_can_fallback(qstate->env->auth_zones,
			iq->dp->name, iq->dp->namelen, qstate->qinfo.qclass)) {
			verbose(VERB_ALGO, "auth zone response bad, and no"
				" fallback possible, servfail");
			errinf_dname(qstate, "response is bad, no fallback, "
				"for auth zone", iq->dp->name);
			return error_response_cache(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		verbose(VERB_ALGO, "auth zone response was bad, "
			"fallback enabled");
		iq->auth_zone_avoid = 1;
		if(iq->dp->auth_dp) {
			/* we are using a dp for the auth zone, with no
			 * nameservers, get one first */
			iq->dp = NULL;
			return next_state(iq, INIT_REQUEST_STATE);
		}
	}
	return next_state(iq, QUERYTARGETS_STATE);
}

/**
 * Return priming query results to interested super querystates.
 * 
 * Sets the delegation point and delegation message (not nonRD queries).
 * This is a callback from walk_supers.
 *
 * @param qstate: priming query state that finished.
 * @param id: module id.
 * @param forq: the qstate for which priming has been done.
 */
static void
prime_supers(struct module_qstate* qstate, int id, struct module_qstate* forq)
{
	struct iter_qstate* foriq = (struct iter_qstate*)forq->minfo[id];
	struct delegpt* dp = NULL;

	log_assert(qstate->is_priming || foriq->wait_priming_stub);
	log_assert(qstate->return_rcode == LDNS_RCODE_NOERROR);
	/* Convert our response to a delegation point */
	dp = delegpt_from_message(qstate->return_msg, forq->region);
	if(!dp) {
		/* if there is no convertible delegation point, then 
		 * the ANSWER type was (presumably) a negative answer. */
		verbose(VERB_ALGO, "prime response was not a positive "
			"ANSWER; failing");
		foriq->dp = NULL;
		foriq->state = QUERYTARGETS_STATE;
		return;
	}

	log_query_info(VERB_DETAIL, "priming successful for", &qstate->qinfo);
	delegpt_log(VERB_ALGO, dp);
	foriq->dp = dp;
	foriq->deleg_msg = dns_copy_msg(qstate->return_msg, forq->region);
	if(!foriq->deleg_msg) {
		log_err("copy prime response: out of memory");
		foriq->dp = NULL;
		foriq->state = QUERYTARGETS_STATE;
		return;
	}

	/* root priming responses go to init stage 2, priming stub 
	 * responses to to stage 3. */
	if(foriq->wait_priming_stub) {
		foriq->state = INIT_REQUEST_3_STATE;
		foriq->wait_priming_stub = 0;
	} else	foriq->state = INIT_REQUEST_2_STATE;
	/* because we are finished, the parent will be reactivated */
}

/** 
 * This handles the response to a priming query. This is used to handle both
 * root and stub priming responses. This is basically the equivalent of the
 * QUERY_RESP_STATE, but will not handle CNAME responses and will treat
 * REFERRALs as ANSWERS. It will also update and reactivate the originating
 * event.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @return true if the event needs more immediate processing, false if not.
 *         This state always returns false.
 */
static int
processPrimeResponse(struct module_qstate* qstate, int id)
{
	struct iter_qstate* iq = (struct iter_qstate*)qstate->minfo[id];
	enum response_type type;
	iq->response->rep->flags &= ~(BIT_RD|BIT_RA); /* ignore rec-lame */
	type = response_type_from_server(
		(int)((iq->chase_flags&BIT_RD) || iq->chase_to_rd), 
		iq->response, &iq->qchase, iq->dp, NULL);
	if(type == RESPONSE_TYPE_ANSWER) {
		qstate->return_rcode = LDNS_RCODE_NOERROR;
		qstate->return_msg = iq->response;
	} else {
		errinf(qstate, "prime response did not get an answer");
		errinf_dname(qstate, "for", qstate->qinfo.qname);
		qstate->return_rcode = LDNS_RCODE_SERVFAIL;
		qstate->return_msg = NULL;
	}

	/* validate the root or stub after priming (if enabled).
	 * This is the same query as the prime query, but with validation.
	 * Now that we are primed, the additional queries that validation
	 * may need can be resolved. */
	if(qstate->env->cfg->harden_referral_path) {
		struct module_qstate* subq = NULL;
		log_nametypeclass(VERB_ALGO, "schedule prime validation", 
			qstate->qinfo.qname, qstate->qinfo.qtype,
			qstate->qinfo.qclass);
		if(!generate_sub_request(qstate->qinfo.qname, 
			qstate->qinfo.qname_len, qstate->qinfo.qtype,
			qstate->qinfo.qclass, qstate, id, iq,
			INIT_REQUEST_STATE, FINISHED_STATE, &subq, 1, 0)) {
			verbose(VERB_ALGO, "could not generate prime check");
		}
		generate_a_aaaa_check(qstate, iq, id);
	}

	/* This event is finished. */
	qstate->ext_state[id] = module_finished;
	return 0;
}

/** 
 * Do final processing on responses to target queries. Events reach this
 * state after the iterative resolution algorithm terminates. This state is
 * responsible for reactivating the original event, and housekeeping related
 * to received target responses (caching, updating the current delegation
 * point, etc).
 * Callback from walk_supers for every super state that is interested in 
 * the results from this query.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @param forq: super query state.
 */
static void
processTargetResponse(struct module_qstate* qstate, int id,
	struct module_qstate* forq)
{
	struct iter_env* ie = (struct iter_env*)qstate->env->modinfo[id];
	struct iter_qstate* iq = (struct iter_qstate*)qstate->minfo[id];
	struct iter_qstate* foriq = (struct iter_qstate*)forq->minfo[id];
	struct ub_packed_rrset_key* rrset;
	struct delegpt_ns* dpns;
	log_assert(qstate->return_rcode == LDNS_RCODE_NOERROR);

	foriq->state = QUERYTARGETS_STATE;
	log_query_info(VERB_ALGO, "processTargetResponse", &qstate->qinfo);
	log_query_info(VERB_ALGO, "processTargetResponse super", &forq->qinfo);

	/* Tell the originating event that this target query has finished
	 * (regardless if it succeeded or not). */
	foriq->num_target_queries--;

	/* check to see if parent event is still interested (in orig name).  */
	if(!foriq->dp) {
		verbose(VERB_ALGO, "subq: parent not interested, was reset");
		return; /* not interested anymore */
	}
	dpns = delegpt_find_ns(foriq->dp, qstate->qinfo.qname,
			qstate->qinfo.qname_len);
	if(!dpns) {
		/* If not interested, just stop processing this event */
		verbose(VERB_ALGO, "subq: parent not interested anymore");
		/* could be because parent was jostled out of the cache,
		   and a new identical query arrived, that does not want it*/
		return;
	}

	/* if iq->query_for_pside_glue then add the pside_glue (marked lame) */
	if(iq->pside_glue) {
		/* if the pside_glue is NULL, then it could not be found,
		 * the done_pside is already set when created and a cache
		 * entry created in processFinished so nothing to do here */
		log_rrset_key(VERB_ALGO, "add parentside glue to dp", 
			iq->pside_glue);
		if(!delegpt_add_rrset(foriq->dp, forq->region, 
			iq->pside_glue, 1, NULL))
			log_err("out of memory adding pside glue");
	}

	/* This response is relevant to the current query, so we
	 * add (attempt to add, anyway) this target(s) and reactivate
	 * the original event.
	 * NOTE: we could only look for the AnswerRRset if the
	 * response type was ANSWER. */
	rrset = reply_find_answer_rrset(&iq->qchase, qstate->return_msg->rep);
	if(rrset) {
		int additions = 0;
		/* if CNAMEs have been followed - add new NS to delegpt. */
		/* BTW. RFC 1918 says NS should not have got CNAMEs. Robust. */
		if(!delegpt_find_ns(foriq->dp, rrset->rk.dname,
			rrset->rk.dname_len)) {
			/* if dpns->lame then set newcname ns lame too */
			if(!delegpt_add_ns(foriq->dp, forq->region,
				rrset->rk.dname, dpns->lame, dpns->tls_auth_name,
				dpns->port))
				log_err("out of memory adding cnamed-ns");
		}
		/* if dpns->lame then set the address(es) lame too */
		if(!delegpt_add_rrset(foriq->dp, forq->region, rrset, 
			dpns->lame, &additions))
			log_err("out of memory adding targets");
		if(!additions) {
			/* no new addresses, increase the nxns counter, like
			 * this could be a list of wildcards with no new
			 * addresses */
			target_count_increase_nx(foriq, 1);
		}
		verbose(VERB_ALGO, "added target response");
		delegpt_log(VERB_ALGO, foriq->dp);
	} else {
		verbose(VERB_ALGO, "iterator TargetResponse failed");
		delegpt_mark_neg(dpns, qstate->qinfo.qtype);
		if((dpns->got4 == 2 || (!ie->supports_ipv4 && !ie->nat64.use_nat64)) &&
			(dpns->got6 == 2 || !ie->supports_ipv6)) {
			dpns->resolved = 1; /* fail the target */
			/* do not count cached answers */
			if(qstate->reply_origin && qstate->reply_origin->len != 0) {
				target_count_increase_nx(foriq, 1);
			}
		}
	}
}

/**
 * Process response for DS NS Find queries, that attempt to find the delegation
 * point where we ask the DS query from.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @param forq: super query state.
 */
static void
processDSNSResponse(struct module_qstate* qstate, int id,
	struct module_qstate* forq)
{
	struct iter_qstate* foriq = (struct iter_qstate*)forq->minfo[id];

	/* if the finished (iq->response) query has no NS set: continue
	 * up to look for the right dp; nothing to change, do DPNSstate */
	if(qstate->return_rcode != LDNS_RCODE_NOERROR)
		return; /* seek further */
	/* find the NS RRset (without allowing CNAMEs) */
	if(!reply_find_rrset(qstate->return_msg->rep, qstate->qinfo.qname,
		qstate->qinfo.qname_len, LDNS_RR_TYPE_NS,
		qstate->qinfo.qclass)){
		return; /* seek further */
	}

	/* else, store as DP and continue at querytargets */
	foriq->state = QUERYTARGETS_STATE;
	foriq->dp = delegpt_from_message(qstate->return_msg, forq->region);
	if(!foriq->dp) {
		log_err("out of memory in dsns dp alloc");
		errinf(qstate, "malloc failure, in DS search");
		return; /* dp==NULL in QUERYTARGETS makes SERVFAIL */
	}
	/* success, go query the querytargets in the new dp (and go down) */
}

/**
 * Process response for qclass=ANY queries for a particular class.
 * Append to result or error-exit.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @param forq: super query state.
 */
static void
processClassResponse(struct module_qstate* qstate, int id,
	struct module_qstate* forq)
{
	struct iter_qstate* foriq = (struct iter_qstate*)forq->minfo[id];
	struct dns_msg* from = qstate->return_msg;
	log_query_info(VERB_ALGO, "processClassResponse", &qstate->qinfo);
	log_query_info(VERB_ALGO, "processClassResponse super", &forq->qinfo);
	if(qstate->return_rcode != LDNS_RCODE_NOERROR) {
		/* cause servfail for qclass ANY query */
		foriq->response = NULL;
		foriq->state = FINISHED_STATE;
		return;
	}
	/* append result */
	if(!foriq->response) {
		/* allocate the response: copy RCODE, sec_state */
		foriq->response = dns_copy_msg(from, forq->region);
		if(!foriq->response) {
			log_err("malloc failed for qclass ANY response"); 
			foriq->state = FINISHED_STATE;
			return;
		}
		foriq->response->qinfo.qclass = forq->qinfo.qclass;
		/* qclass ANY does not receive the AA flag on replies */
		foriq->response->rep->authoritative = 0; 
	} else {
		struct dns_msg* to = foriq->response;
		/* add _from_ this response _to_ existing collection */
		/* if there are records, copy RCODE */
		/* lower sec_state if this message is lower */
		if(from->rep->rrset_count != 0) {
			size_t n = from->rep->rrset_count+to->rep->rrset_count;
			struct ub_packed_rrset_key** dest, **d;
			/* copy appropriate rcode */
			to->rep->flags = from->rep->flags;
			/* copy rrsets */
			if(from->rep->rrset_count > RR_COUNT_MAX ||
				to->rep->rrset_count > RR_COUNT_MAX) {
				log_err("malloc failed (too many rrsets) in collect ANY"); 
				foriq->state = FINISHED_STATE;
				return; /* integer overflow protection */
			}
			dest = regional_alloc(forq->region, sizeof(dest[0])*n);
			if(!dest) {
				log_err("malloc failed in collect ANY"); 
				foriq->state = FINISHED_STATE;
				return;
			}
			d = dest;
			/* copy AN */
			memcpy(dest, to->rep->rrsets, to->rep->an_numrrsets
				* sizeof(dest[0]));
			dest += to->rep->an_numrrsets;
			memcpy(dest, from->rep->rrsets, from->rep->an_numrrsets
				* sizeof(dest[0]));
			dest += from->rep->an_numrrsets;
			/* copy NS */
			memcpy(dest, to->rep->rrsets+to->rep->an_numrrsets,
				to->rep->ns_numrrsets * sizeof(dest[0]));
			dest += to->rep->ns_numrrsets;
			memcpy(dest, from->rep->rrsets+from->rep->an_numrrsets,
				from->rep->ns_numrrsets * sizeof(dest[0]));
			dest += from->rep->ns_numrrsets;
			/* copy AR */
			memcpy(dest, to->rep->rrsets+to->rep->an_numrrsets+
				to->rep->ns_numrrsets,
				to->rep->ar_numrrsets * sizeof(dest[0]));
			dest += to->rep->ar_numrrsets;
			memcpy(dest, from->rep->rrsets+from->rep->an_numrrsets+
				from->rep->ns_numrrsets,
				from->rep->ar_numrrsets * sizeof(dest[0]));
			/* update counts */
			to->rep->rrsets = d;
			to->rep->an_numrrsets += from->rep->an_numrrsets;
			to->rep->ns_numrrsets += from->rep->ns_numrrsets;
			to->rep->ar_numrrsets += from->rep->ar_numrrsets;
			to->rep->rrset_count = n;
		}
		if(from->rep->security < to->rep->security) /* lowest sec */
			to->rep->security = from->rep->security;
		if(from->rep->qdcount != 0) /* insert qd if appropriate */
			to->rep->qdcount = from->rep->qdcount;
		if(from->rep->ttl < to->rep->ttl) /* use smallest TTL */
			to->rep->ttl = from->rep->ttl;
		if(from->rep->prefetch_ttl < to->rep->prefetch_ttl)
			to->rep->prefetch_ttl = from->rep->prefetch_ttl;
		if(from->rep->serve_expired_ttl < to->rep->serve_expired_ttl)
			to->rep->serve_expired_ttl = from->rep->serve_expired_ttl;
		if(from->rep->serve_expired_norec_ttl < to->rep->serve_expired_norec_ttl)
			to->rep->serve_expired_norec_ttl = from->rep->serve_expired_norec_ttl;
	}
	/* are we done? */
	foriq->num_current_queries --;
	if(foriq->num_current_queries == 0)
		foriq->state = FINISHED_STATE;
}
	
/** 
 * Collect class ANY responses and make them into one response.  This
 * state is started and it creates queries for all classes (that have
 * root hints).  The answers are then collected.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @return true if the event needs more immediate processing, false if not.
 */
static int
processCollectClass(struct module_qstate* qstate, int id)
{
	struct iter_qstate* iq = (struct iter_qstate*)qstate->minfo[id];
	struct module_qstate* subq;
	/* If qchase.qclass == 0 then send out queries for all classes.
	 * Otherwise, do nothing (wait for all answers to arrive and the
	 * processClassResponse to put them together, and that moves us
	 * towards the Finished state when done. */
	if(iq->qchase.qclass == 0) {
		uint16_t c = 0;
		iq->qchase.qclass = LDNS_RR_CLASS_ANY;
		while(iter_get_next_root(qstate->env->hints,
			qstate->env->fwds, &c)) {
			/* generate query for this class */
			log_nametypeclass(VERB_ALGO, "spawn collect query",
				qstate->qinfo.qname, qstate->qinfo.qtype, c);
			if(!generate_sub_request(qstate->qinfo.qname,
				qstate->qinfo.qname_len, qstate->qinfo.qtype,
				c, qstate, id, iq, INIT_REQUEST_STATE,
				FINISHED_STATE, &subq, 
				(int)!(qstate->query_flags&BIT_CD), 0)) {
				errinf(qstate, "could not generate class ANY"
					" lookup query");
				return error_response(qstate, id, 
					LDNS_RCODE_SERVFAIL);
			}
			/* ignore subq, no special init required */
			iq->num_current_queries ++;
			if(c == 0xffff)
				break;
			else c++;
		}
		/* if no roots are configured at all, return */
		if(iq->num_current_queries == 0) {
			verbose(VERB_ALGO, "No root hints or fwds, giving up "
				"on qclass ANY");
			return error_response_cache(qstate, id, LDNS_RCODE_REFUSED);
		}
		/* return false, wait for queries to return */
	}
	/* if woke up here because of an answer, wait for more answers */
	return 0;
}

/** 
 * This handles the final state for first-tier responses (i.e., responses to
 * externally generated queries).
 *
 * @param qstate: query state.
 * @param iq: iterator query state.
 * @param id: module id.
 * @return true if the event needs more processing, false if not. Since this
 *         is the final state for an event, it always returns false.
 */
static int
processFinished(struct module_qstate* qstate, struct iter_qstate* iq,
	int id)
{
	log_query_info(VERB_QUERY, "finishing processing for", 
		&qstate->qinfo);

	/* store negative cache element for parent side glue. */
	if(!qstate->no_cache_store && iq->query_for_pside_glue
		&& !iq->pside_glue)
			iter_store_parentside_neg(qstate->env, &qstate->qinfo,
				iq->deleg_msg?iq->deleg_msg->rep:
				(iq->response?iq->response->rep:NULL));
	if(!iq->response) {
		verbose(VERB_ALGO, "No response is set, servfail");
		errinf(qstate, "(no response found at query finish)");
		return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
	}

	/* Make sure that the RA flag is set (since the presence of 
	 * this module means that recursion is available) */
	iq->response->rep->flags |= BIT_RA;

	/* Clear the AA flag */
	/* FIXME: does this action go here or in some other module? */
	iq->response->rep->flags &= ~BIT_AA;

	/* make sure QR flag is on */
	iq->response->rep->flags |= BIT_QR;

	/* explicitly set the EDE string to NULL */
	iq->response->rep->reason_bogus_str = NULL;
	if((qstate->env->cfg->val_log_level >= 2 ||
		qstate->env->cfg->log_servfail) && qstate->errinf &&
		!qstate->env->cfg->val_log_squelch) {
		char* err_str = errinf_to_str_misc(qstate);
		if(err_str) {
			verbose(VERB_ALGO, "iterator EDE: %s", err_str);
			iq->response->rep->reason_bogus_str = err_str;
		}
	}

	/* we have finished processing this query */
	qstate->ext_state[id] = module_finished;

	/* TODO:  we are using a private TTL, trim the response. */
	/* if (mPrivateTTL > 0){IterUtils.setPrivateTTL(resp, mPrivateTTL); } */

	/* prepend any items we have accumulated */
	if(iq->an_prepend_list || iq->ns_prepend_list) {
		if(!iter_prepend(iq, iq->response, qstate->region)) {
			log_err("prepend rrsets: out of memory");
			return error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		}
		/* reset the query name back */
		iq->response->qinfo = qstate->qinfo;
		/* the security state depends on the combination */
		iq->response->rep->security = sec_status_unchecked;
		/* store message with the finished prepended items,
		 * but only if we did recursion. The nonrecursion referral
		 * from cache does not need to be stored in the msg cache. */
		if(!qstate->no_cache_store && qstate->query_flags&BIT_RD) {
			iter_dns_store(qstate->env, &qstate->qinfo, 
				iq->response->rep, 0, qstate->prefetch_leeway,
				iq->dp&&iq->dp->has_parent_side_NS,
				qstate->region, qstate->query_flags,
				qstate->qstarttime, qstate->is_valrec);
		}
	}
	qstate->return_rcode = LDNS_RCODE_NOERROR;
	qstate->return_msg = iq->response;
	return 0;
}

/*
 * Return priming query results to interested super querystates.
 * 
 * Sets the delegation point and delegation message (not nonRD queries).
 * This is a callback from walk_supers.
 *
 * @param qstate: query state that finished.
 * @param id: module id.
 * @param super: the qstate to inform.
 */
void
iter_inform_super(struct module_qstate* qstate, int id, 
	struct module_qstate* super)
{
	if(!qstate->is_priming && super->qinfo.qclass == LDNS_RR_CLASS_ANY)
		processClassResponse(qstate, id, super);
	else if(super->qinfo.qtype == LDNS_RR_TYPE_DS && ((struct iter_qstate*)
		super->minfo[id])->state == DSNS_FIND_STATE)
		processDSNSResponse(qstate, id, super);
	else if(qstate->return_rcode != LDNS_RCODE_NOERROR)
		error_supers(qstate, id, super);
	else if(qstate->is_priming)
		prime_supers(qstate, id, super);
	else	processTargetResponse(qstate, id, super);
}

/**
 * Handle iterator state.
 * Handle events. This is the real processing loop for events, responsible
 * for moving events through the various states. If a processing method
 * returns true, then it will be advanced to the next state. If false, then
 * processing will stop.
 *
 * @param qstate: query state.
 * @param ie: iterator shared global environment.
 * @param iq: iterator query state.
 * @param id: module id.
 */
static void
iter_handle(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	int cont = 1;
	while(cont) {
		verbose(VERB_ALGO, "iter_handle processing q with state %s",
			iter_state_to_string(iq->state));
		switch(iq->state) {
			case INIT_REQUEST_STATE:
				cont = processInitRequest(qstate, iq, ie, id);
				break;
			case INIT_REQUEST_2_STATE:
				cont = processInitRequest2(qstate, iq, id);
				break;
			case INIT_REQUEST_3_STATE:
				cont = processInitRequest3(qstate, iq, id);
				break;
			case QUERYTARGETS_STATE:
				cont = processQueryTargets(qstate, iq, ie, id);
				break;
			case QUERY_RESP_STATE:
				cont = processQueryResponse(qstate, iq, ie, id);
				break;
			case PRIME_RESP_STATE:
				cont = processPrimeResponse(qstate, id);
				break;
			case COLLECT_CLASS_STATE:
				cont = processCollectClass(qstate, id);
				break;
			case DSNS_FIND_STATE:
				cont = processDSNSFind(qstate, iq, id);
				break;
			case FINISHED_STATE:
				cont = processFinished(qstate, iq, id);
				break;
			default:
				log_warn("iterator: invalid state: %d",
					iq->state);
				cont = 0;
				break;
		}
	}
}

/** 
 * This is the primary entry point for processing request events. Note that
 * this method should only be used by external modules.
 * @param qstate: query state.
 * @param ie: iterator shared global environment.
 * @param iq: iterator query state.
 * @param id: module id.
 */
static void
process_request(struct module_qstate* qstate, struct iter_qstate* iq,
	struct iter_env* ie, int id)
{
	/* external requests start in the INIT state, and finish using the
	 * FINISHED state. */
	iq->state = INIT_REQUEST_STATE;
	iq->final_state = FINISHED_STATE;
	verbose(VERB_ALGO, "process_request: new external request event");
	iter_handle(qstate, iq, ie, id);
}

/** process authoritative server reply */
static void
process_response(struct module_qstate* qstate, struct iter_qstate* iq, 
	struct iter_env* ie, int id, struct outbound_entry* outbound,
	enum module_ev event)
{
	struct msg_parse* prs;
	struct edns_data edns;
	sldns_buffer* pkt;

	verbose(VERB_ALGO, "process_response: new external response event");
	iq->response = NULL;
	iq->state = QUERY_RESP_STATE;
	if(event == module_event_noreply || event == module_event_error) {
		if(event == module_event_noreply && iq->timeout_count >= 3 &&
			qstate->env->cfg->use_caps_bits_for_id &&
			!iq->caps_fallback && !is_caps_whitelisted(ie, iq)) {
			/* start fallback */
			iq->caps_fallback = 1;
			iq->caps_server = 0;
			iq->caps_reply = NULL;
			iq->caps_response = NULL;
			iq->caps_minimisation_state = DONOT_MINIMISE_STATE;
			iq->state = QUERYTARGETS_STATE;
			iq->num_current_queries--;
			/* need fresh attempts for the 0x20 fallback, if
			 * that was the cause for the failure */
			iter_dec_attempts(iq->dp, 3, ie->outbound_msg_retry);
			verbose(VERB_DETAIL, "Capsforid: timeouts, starting fallback");
			goto handle_it;
		}
		goto handle_it;
	}
	if( (event != module_event_reply && event != module_event_capsfail)
		|| !qstate->reply) {
		log_err("Bad event combined with response");
		outbound_list_remove(&iq->outlist, outbound);
		errinf(qstate, "module iterator received wrong internal event with a response message");
		(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		return;
	}

	/* parse message */
	fill_fail_addr(iq, &qstate->reply->remote_addr,
		qstate->reply->remote_addrlen);
	prs = (struct msg_parse*)regional_alloc(qstate->env->scratch, 
		sizeof(struct msg_parse));
	if(!prs) {
		log_err("out of memory on incoming message");
		/* like packet got dropped */
		goto handle_it;
	}
	memset(prs, 0, sizeof(*prs));
	memset(&edns, 0, sizeof(edns));
	pkt = qstate->reply->c->buffer;
	sldns_buffer_set_position(pkt, 0);
	if(parse_packet(pkt, prs, qstate->env->scratch) != LDNS_RCODE_NOERROR) {
		verbose(VERB_ALGO, "parse error on reply packet");
		iq->parse_failures++;
		goto handle_it;
	}
	/* edns is not examined, but removed from message to help cache */
	if(parse_extract_edns_from_response_msg(prs, &edns, qstate->env->scratch) !=
		LDNS_RCODE_NOERROR) {
		iq->parse_failures++;
		goto handle_it;
	}

	/* Copy the edns options we may got from the back end */
	qstate->edns_opts_back_in = NULL;
	if(edns.opt_list_in) {
		qstate->edns_opts_back_in = edns_opt_copy_region(edns.opt_list_in,
			qstate->region);
		if(!qstate->edns_opts_back_in) {
			log_err("out of memory on incoming message");
			/* like packet got dropped */
			goto handle_it;
		}
	}
	if(!inplace_cb_edns_back_parsed_call(qstate->env, qstate)) {
		log_err("unable to call edns_back_parsed callback");
		goto handle_it;
	}

	/* remove CD-bit, we asked for in case we handle validation ourself */
	prs->flags &= ~BIT_CD;

	/* normalize and sanitize: easy to delete items from linked lists */
	if(!scrub_message(pkt, prs, &iq->qinfo_out, iq->dp->name, 
		qstate->env->scratch, qstate->env, qstate, ie)) {
		/* if 0x20 enabled, start fallback, but we have no message */
		if(event == module_event_capsfail && !iq->caps_fallback) {
			iq->caps_fallback = 1;
			iq->caps_server = 0;
			iq->caps_reply = NULL;
			iq->caps_response = NULL;
			iq->caps_minimisation_state = DONOT_MINIMISE_STATE;
			iq->state = QUERYTARGETS_STATE;
			iq->num_current_queries--;
			verbose(VERB_DETAIL, "Capsforid: scrub failed, starting fallback with no response");
		}
		iq->scrub_failures++;
		goto handle_it;
	}

	/* allocate response dns_msg in region */
	iq->response = dns_alloc_msg(pkt, prs, qstate->region);
	if(!iq->response)
		goto handle_it;
	log_query_info(VERB_DETAIL, "response for", &qstate->qinfo);
	log_name_addr(VERB_DETAIL, "reply from", iq->dp->name,
		&qstate->reply->remote_addr, qstate->reply->remote_addrlen);
	if(verbosity >= VERB_ALGO)
		log_dns_msg("incoming scrubbed packet:", &iq->response->qinfo, 
			iq->response->rep);

	if(qstate->env->cfg->aggressive_nsec) {
		limit_nsec_ttl(iq->response);
	}
	if(event == module_event_capsfail || iq->caps_fallback) {
		if(qstate->env->cfg->qname_minimisation &&
			iq->minimisation_state != DONOT_MINIMISE_STATE) {
			/* Skip QNAME minimisation for next query, since that
			 * one has to match the current query. */
			iq->minimisation_state = SKIP_MINIMISE_STATE;
		}
		/* for fallback we care about main answer, not additionals */
		/* removing that makes comparison more likely to succeed */
		caps_strip_reply(iq->response->rep);

		if(iq->caps_fallback &&
			iq->caps_minimisation_state != iq->minimisation_state) {
			/* QNAME minimisation state has changed, restart caps
			 * fallback. */
			iq->caps_fallback = 0;
		}

		if(!iq->caps_fallback) {
			/* start fallback */
			iq->caps_fallback = 1;
			iq->caps_server = 0;
			iq->caps_reply = iq->response->rep;
			iq->caps_response = iq->response;
			iq->caps_minimisation_state = iq->minimisation_state;
			iq->state = QUERYTARGETS_STATE;
			iq->num_current_queries--;
			verbose(VERB_DETAIL, "Capsforid: starting fallback");
			goto handle_it;
		} else {
			/* check if reply is the same, otherwise, fail */
			if(!iq->caps_reply) {
				iq->caps_reply = iq->response->rep;
				iq->caps_response = iq->response;
				iq->caps_server = -1; /*become zero at ++,
				so that we start the full set of trials */
			} else if(caps_failed_rcode(iq->caps_reply) &&
				!caps_failed_rcode(iq->response->rep)) {
				/* prefer to upgrade to non-SERVFAIL */
				iq->caps_reply = iq->response->rep;
				iq->caps_response = iq->response;
			} else if(!caps_failed_rcode(iq->caps_reply) &&
				caps_failed_rcode(iq->response->rep)) {
				/* if we have non-SERVFAIL as answer then 
				 * we can ignore SERVFAILs for the equality
				 * comparison */
				/* no instructions here, skip other else */
			} else if(caps_failed_rcode(iq->caps_reply) &&
				caps_failed_rcode(iq->response->rep)) {
				/* failure is same as other failure in fallbk*/
				/* no instructions here, skip other else */
			} else if(!reply_equal(iq->response->rep, iq->caps_reply,
				qstate->env->scratch)) {
				verbose(VERB_DETAIL, "Capsforid fallback: "
					"getting different replies, failed");
				outbound_list_remove(&iq->outlist, outbound);
				errinf(qstate, "0x20 failed, then got different replies in fallback");
				(void)error_response_cache(qstate, id,
					LDNS_RCODE_SERVFAIL);
				return;
			}
			/* continue the fallback procedure at next server */
			iq->caps_server++;
			iq->state = QUERYTARGETS_STATE;
			iq->num_current_queries--;
			verbose(VERB_DETAIL, "Capsforid: reply is equal. "
				"go to next fallback");
			goto handle_it;
		}
	}
	iq->caps_fallback = 0; /* if we were in fallback, 0x20 is OK now */

handle_it:
	outbound_list_remove(&iq->outlist, outbound);
	iter_handle(qstate, iq, ie, id);
}

void 
iter_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound)
{
	struct iter_env* ie = (struct iter_env*)qstate->env->modinfo[id];
	struct iter_qstate* iq = (struct iter_qstate*)qstate->minfo[id];
	verbose(VERB_QUERY, "iterator[module %d] operate: extstate:%s event:%s", 
		id, strextstate(qstate->ext_state[id]), strmodulevent(event));
	if(iq) log_query_info(VERB_QUERY, "iterator operate: query", 
		&qstate->qinfo);
	if(iq && qstate->qinfo.qname != iq->qchase.qname)
		log_query_info(VERB_QUERY, "iterator operate: chased to", 
			&iq->qchase);

	/* perform iterator state machine */
	if((event == module_event_new || event == module_event_pass) && 
		iq == NULL) {
		if(!iter_new(qstate, id)) {
			errinf(qstate, "malloc failure, new iterator module allocation");
			(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return;
		}
		iq = (struct iter_qstate*)qstate->minfo[id];
		process_request(qstate, iq, ie, id);
		return;
	}
	if(iq && event == module_event_pass) {
		iter_handle(qstate, iq, ie, id);
		return;
	}
	if(iq && outbound) {
		process_response(qstate, iq, ie, id, outbound, event);
		return;
	}
	if(event == module_event_error) {
		verbose(VERB_ALGO, "got called with event error, giving up");
		errinf(qstate, "iterator module got the error event");
		(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		return;
	}

	log_err("bad event for iterator");
	errinf(qstate, "iterator module received wrong event");
	(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
}

void 
iter_clear(struct module_qstate* qstate, int id)
{
	struct iter_qstate* iq;
	if(!qstate)
		return;
	iq = (struct iter_qstate*)qstate->minfo[id];
	if(iq) {
		outbound_list_clear(&iq->outlist);
		if(iq->target_count && --iq->target_count[TARGET_COUNT_REF] == 0) {
			free(iq->target_count);
			if(*iq->nxns_dp) free(*iq->nxns_dp);
			free(iq->nxns_dp);
		}
		iq->num_current_queries = 0;
	}
	qstate->minfo[id] = NULL;
}

size_t 
iter_get_mem(struct module_env* env, int id)
{
	struct iter_env* ie = (struct iter_env*)env->modinfo[id];
	if(!ie)
		return 0;
	return sizeof(*ie) + sizeof(int)*((size_t)ie->max_dependency_depth+1)
		+ donotq_get_mem(ie->donotq) + priv_get_mem(ie->priv);
}

/**
 * The iterator function block 
 */
static struct module_func_block iter_block = {
	"iterator",
	NULL, NULL, &iter_init, &iter_deinit, &iter_operate,
	&iter_inform_super, &iter_clear, &iter_get_mem
};

struct module_func_block* 
iter_get_funcblock(void)
{
	return &iter_block;
}

const char* 
iter_state_to_string(enum iter_state state)
{
	switch (state)
	{
	case INIT_REQUEST_STATE :
		return "INIT REQUEST STATE";
	case INIT_REQUEST_2_STATE :
		return "INIT REQUEST STATE (stage 2)";
	case INIT_REQUEST_3_STATE:
		return "INIT REQUEST STATE (stage 3)";
	case QUERYTARGETS_STATE :
		return "QUERY TARGETS STATE";
	case PRIME_RESP_STATE :
		return "PRIME RESPONSE STATE";
	case COLLECT_CLASS_STATE :
		return "COLLECT CLASS STATE";
	case DSNS_FIND_STATE :
		return "DSNS FIND STATE";
	case QUERY_RESP_STATE :
		return "QUERY RESPONSE STATE";
	case FINISHED_STATE :
		return "FINISHED RESPONSE STATE";
	default :
		return "UNKNOWN ITER STATE";
	}
}

int 
iter_state_is_responsestate(enum iter_state s)
{
	switch(s) {
		case INIT_REQUEST_STATE :
		case INIT_REQUEST_2_STATE :
		case INIT_REQUEST_3_STATE :
		case QUERYTARGETS_STATE :
		case COLLECT_CLASS_STATE :
			return 0;
		default:
			break;
	}
	return 1;
}
