/*
 * validator/validator.c - secure validator DNS query response module
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
 * This file contains a module that performs validation of DNS queries.
 * According to RFC 4034.
 */
#include "config.h"
#include <ctype.h>
#include "validator/validator.h"
#include "validator/val_anchor.h"
#include "validator/val_kcache.h"
#include "validator/val_kentry.h"
#include "validator/val_utils.h"
#include "validator/val_nsec.h"
#include "validator/val_nsec3.h"
#include "validator/val_neg.h"
#include "validator/val_sigcrypt.h"
#include "validator/autotrust.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "util/data/dname.h"
#include "util/module.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/config_file.h"
#include "util/fptr_wlist.h"
#include "sldns/rrdef.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"

/** Max number of RRSIGs to validate at once, suspend query for later. */
#define MAX_VALIDATE_AT_ONCE 8
/** Max number of validation suspends allowed, error out otherwise. */
#define MAX_VALIDATION_SUSPENDS 16

/* forward decl for cache response and normal super inform calls of a DS */
static void process_ds_response(struct module_qstate* qstate, 
	struct val_qstate* vq, int id, int rcode, struct dns_msg* msg, 
	struct query_info* qinfo, struct sock_list* origin, int* suspend,
	struct module_qstate* sub_qstate);


/* Updates the suplied EDE (RFC8914) code selectively so we don't lose
 * a more specific code */
static void
update_reason_bogus(struct reply_info* rep, sldns_ede_code reason_bogus)
{
	if(reason_bogus == LDNS_EDE_NONE) return;
	if(reason_bogus == LDNS_EDE_DNSSEC_BOGUS
		&& rep->reason_bogus != LDNS_EDE_NONE
		&& rep->reason_bogus != LDNS_EDE_DNSSEC_BOGUS) return;
	rep->reason_bogus = reason_bogus;
}


/** fill up nsec3 key iterations config entry */
static int
fill_nsec3_iter(size_t** keysize, size_t** maxiter, char* s, int c)
{
	char* e;
	int i;
	*keysize = (size_t*)calloc((size_t)c, sizeof(size_t));
	*maxiter = (size_t*)calloc((size_t)c, sizeof(size_t));
	if(!*keysize || !*maxiter) {
		free(*keysize);
		*keysize = NULL;
		free(*maxiter);
		*maxiter = NULL;
		log_err("out of memory");
		return 0;
	}
	for(i=0; i<c; i++) {
		(*keysize)[i] = (size_t)strtol(s, &e, 10);
		if(s == e) {
			log_err("cannot parse: %s", s);
			free(*keysize);
			*keysize = NULL;
			free(*maxiter);
			*maxiter = NULL;
			return 0;
		}
		s = e;
		(*maxiter)[i] = (size_t)strtol(s, &e, 10);
		if(s == e) {
			log_err("cannot parse: %s", s);
			free(*keysize);
			*keysize = NULL;
			free(*maxiter);
			*maxiter = NULL;
			return 0;
		}
		s = e;
		if(i>0 && (*keysize)[i-1] >= (*keysize)[i]) {
			log_err("nsec3 key iterations not ascending: %d %d",
				(int)(*keysize)[i-1], (int)(*keysize)[i]);
			free(*keysize);
			*keysize = NULL;
			free(*maxiter);
			*maxiter = NULL;
			return 0;
		}
		verbose(VERB_ALGO, "validator nsec3cfg keysz %d mxiter %d",
			(int)(*keysize)[i], (int)(*maxiter)[i]);
	}
	return 1;
}

int
val_env_parse_key_iter(char* val_nsec3_key_iterations, size_t** keysize,
	size_t** maxiter, int* keyiter_count)
{
	int c;
	c = cfg_count_numbers(val_nsec3_key_iterations);
	if(c < 1 || (c&1)) {
		log_err("validator: unparsable or odd nsec3 key "
			"iterations: %s", val_nsec3_key_iterations);
		return 0;
	}
	*keyiter_count = c/2;
	if(!fill_nsec3_iter(keysize, maxiter, val_nsec3_key_iterations, c/2)) {
		log_err("validator: cannot apply nsec3 key iterations");
		return 0;
	}
	return 1;
}

void
val_env_apply_cfg(struct val_env* val_env, struct config_file* cfg,
	size_t* keysize, size_t* maxiter, int keyiter_count)
{
	free(val_env->nsec3_keysize);
	free(val_env->nsec3_maxiter);
	val_env->nsec3_keysize = keysize;
	val_env->nsec3_maxiter = maxiter;
	val_env->nsec3_keyiter_count = keyiter_count;
	val_env->bogus_ttl = (uint32_t)cfg->bogus_ttl;
	val_env->date_override = cfg->val_date_override;
	val_env->skew_min = cfg->val_sig_skew_min;
	val_env->skew_max = cfg->val_sig_skew_max;
	val_env->max_restart = cfg->val_max_restart;
}

/** apply config settings to validator */
static int
val_apply_cfg(struct module_env* env, struct val_env* val_env, 
	struct config_file* cfg)
{
	size_t* keysize=NULL, *maxiter=NULL;
	int keyiter_count = 0;
	if(!env->anchors)
		env->anchors = anchors_create();
	if(!env->anchors) {
		log_err("out of memory");
		return 0;
	}
	if (env->key_cache)
		val_env->kcache = env->key_cache;
	if(!val_env->kcache)
		val_env->kcache = key_cache_create(cfg);
	if(!val_env->kcache) {
		log_err("out of memory");
		return 0;
	}
	env->key_cache = val_env->kcache;
	if(!anchors_apply_cfg(env->anchors, cfg)) {
		log_err("validator: error in trustanchors config");
		return 0;
	}
	if(!val_env_parse_key_iter(cfg->val_nsec3_key_iterations,
		&keysize, &maxiter, &keyiter_count)) {
		return 0;
	}
	val_env_apply_cfg(val_env, cfg, keysize, maxiter, keyiter_count);
	if (env->neg_cache)
		val_env->neg_cache = env->neg_cache;
	if(!val_env->neg_cache)
		val_env->neg_cache = val_neg_create(cfg,
			val_env->nsec3_maxiter[val_env->nsec3_keyiter_count-1]);
	if(!val_env->neg_cache) {
		log_err("out of memory");
		return 0;
	}
	env->neg_cache = val_env->neg_cache;
	return 1;
}

#ifdef USE_ECDSA_EVP_WORKAROUND
void ecdsa_evp_workaround_init(void);
#endif
int
val_init(struct module_env* env, int id)
{
	struct val_env* val_env = (struct val_env*)calloc(1,
		sizeof(struct val_env));
	if(!val_env) {
		log_err("malloc failure");
		return 0;
	}
	env->modinfo[id] = (void*)val_env;
	env->need_to_validate = 1;
	lock_basic_init(&val_env->bogus_lock);
	lock_protect(&val_env->bogus_lock, &val_env->num_rrset_bogus,
		sizeof(val_env->num_rrset_bogus));
#ifdef USE_ECDSA_EVP_WORKAROUND
	ecdsa_evp_workaround_init();
#endif
	if(!val_apply_cfg(env, val_env, env->cfg)) {
		log_err("validator: could not apply configuration settings.");
		return 0;
	}
	if(env->cfg->disable_edns_do) {
		struct trust_anchor* anchor = anchors_find_any_noninsecure(
			env->anchors);
		if(anchor) {
			char b[LDNS_MAX_DOMAINLEN];
			dname_str(anchor->name, b);
			log_warn("validator: disable-edns-do is enabled, but there is a trust anchor for '%s'. Since DNSSEC could not work, the disable-edns-do setting is turned off. Continuing without it.", b);
			lock_basic_unlock(&anchor->lock);
			env->cfg->disable_edns_do = 0;
		}
	}

	return 1;
}

void
val_deinit(struct module_env* env, int id)
{
	struct val_env* val_env;
	if(!env || !env->modinfo[id])
		return;
	val_env = (struct val_env*)env->modinfo[id];
	lock_basic_destroy(&val_env->bogus_lock);
	anchors_delete(env->anchors);
	env->anchors = NULL;
	key_cache_delete(val_env->kcache);
	env->key_cache = NULL;
	neg_cache_delete(val_env->neg_cache);
	env->neg_cache = NULL;
	free(val_env->nsec3_keysize);
	free(val_env->nsec3_maxiter);
	free(val_env);
	env->modinfo[id] = NULL;
}

/** fill in message structure */
static struct val_qstate*
val_new_getmsg(struct module_qstate* qstate, struct val_qstate* vq)
{
	if(!qstate->return_msg || qstate->return_rcode != LDNS_RCODE_NOERROR) {
		/* create a message to verify */
		verbose(VERB_ALGO, "constructing reply for validation");
		vq->orig_msg = (struct dns_msg*)regional_alloc(qstate->region,
			sizeof(struct dns_msg));
		if(!vq->orig_msg)
			return NULL;
		vq->orig_msg->qinfo = qstate->qinfo;
		vq->orig_msg->rep = (struct reply_info*)regional_alloc(
			qstate->region, sizeof(struct reply_info));
		if(!vq->orig_msg->rep)
			return NULL;
		memset(vq->orig_msg->rep, 0, sizeof(struct reply_info));
		vq->orig_msg->rep->flags = (uint16_t)(qstate->return_rcode&0xf)
			|BIT_QR|BIT_RA|(qstate->query_flags|(BIT_CD|BIT_RD));
		vq->orig_msg->rep->qdcount = 1;
		vq->orig_msg->rep->reason_bogus = LDNS_EDE_NONE;
	} else {
		vq->orig_msg = qstate->return_msg;
	}
	vq->qchase = qstate->qinfo;
	/* chase reply will be an edited (sub)set of the orig msg rrset ptrs */
	vq->chase_reply = regional_alloc_init(qstate->region, 
		vq->orig_msg->rep, 
		sizeof(struct reply_info) - sizeof(struct rrset_ref));
	if(!vq->chase_reply)
		return NULL;
	if(vq->orig_msg->rep->rrset_count > RR_COUNT_MAX)
		return NULL; /* protect against integer overflow */
	/* Over allocate (+an_numrrsets) in case we need to put extra DNAME
	 * records for unsigned CNAME repetitions */
	vq->chase_reply->rrsets = regional_alloc(qstate->region,
		sizeof(struct ub_packed_rrset_key*) *
		(vq->orig_msg->rep->rrset_count
		+ vq->orig_msg->rep->an_numrrsets));
	if(!vq->chase_reply->rrsets)
		return NULL;
	memmove(vq->chase_reply->rrsets, vq->orig_msg->rep->rrsets,
		sizeof(struct ub_packed_rrset_key*) *
		vq->orig_msg->rep->rrset_count);
	vq->rrset_skip = 0;
	return vq;
}

/** allocate new validator query state */
static struct val_qstate*
val_new(struct module_qstate* qstate, int id)
{
	struct val_qstate* vq = (struct val_qstate*)regional_alloc(
		qstate->region, sizeof(*vq));
	log_assert(!qstate->minfo[id]);
	if(!vq)
		return NULL;
	memset(vq, 0, sizeof(*vq));
	qstate->minfo[id] = vq;
	vq->state = VAL_INIT_STATE;
	return val_new_getmsg(qstate, vq);
}

/** reset validator query state for query restart */
static void
val_restart(struct val_qstate* vq)
{
	struct comm_timer* temp_timer;
	int restart_count;
	if(!vq) return;
	temp_timer = vq->suspend_timer;
	restart_count = vq->restart_count+1;
	memset(vq, 0, sizeof(*vq));
	vq->suspend_timer = temp_timer;
	vq->restart_count = restart_count;
	vq->state = VAL_INIT_STATE;
}

/**
 * Exit validation with an error status
 * 
 * @param qstate: query state
 * @param id: validator id.
 * @return false, for use by caller to return to stop processing.
 */
static int
val_error(struct module_qstate* qstate, int id)
{
	qstate->ext_state[id] = module_error;
	qstate->return_rcode = LDNS_RCODE_SERVFAIL;
	return 0;
}

/** 
 * Check to see if a given response needs to go through the validation
 * process. Typical reasons for this routine to return false are: CD bit was
 * on in the original request, or the response is a kind of message that 
 * is unvalidatable (i.e., SERVFAIL, REFUSED, etc.)
 *
 * @param qstate: query state.
 * @param ret_rc: rcode for this message (if noerror - examine ret_msg).
 * @param ret_msg: return msg, can be NULL; look at rcode instead.
 * @return true if the response could use validation (although this does not
 *         mean we can actually validate this response).
 */
static int
needs_validation(struct module_qstate* qstate, int ret_rc, 
	struct dns_msg* ret_msg)
{
	int rcode;

	/* If the CD bit is on in the original request, then you could think
	 * that we don't bother to validate anything.
	 * But this is signalled internally with the valrec flag.
	 * User queries are validated with BIT_CD to make our cache clean
	 * so that bogus messages get retried by the upstream also for
	 * downstream validators that set BIT_CD.
	 * For DNS64 bit_cd signals no dns64 processing, but we want to
	 * provide validation there too */
	/*
	if(qstate->query_flags & BIT_CD) {
		verbose(VERB_ALGO, "not validating response due to CD bit");
		return 0;
	}
	*/
	if(qstate->is_valrec) {
		verbose(VERB_ALGO, "not validating response, is valrec"
			"(validation recursion lookup)");
		return 0;
	}

	if(ret_rc != LDNS_RCODE_NOERROR || !ret_msg)
		rcode = ret_rc;
	else 	rcode = (int)FLAGS_GET_RCODE(ret_msg->rep->flags);

	if(rcode != LDNS_RCODE_NOERROR && rcode != LDNS_RCODE_NXDOMAIN) {
		if(verbosity >= VERB_ALGO) {
			char rc[16];
			rc[0]=0;
			(void)sldns_wire2str_rcode_buf(rcode, rc, sizeof(rc));
			verbose(VERB_ALGO, "cannot validate non-answer, rcode %s", rc);
		}
		return 0;
	}

	/* cannot validate positive RRSIG response. (negatives can) */
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_RRSIG &&
		rcode == LDNS_RCODE_NOERROR && ret_msg &&
		ret_msg->rep->an_numrrsets > 0) {
		verbose(VERB_ALGO, "cannot validate RRSIG, no sigs on sigs.");
		return 0;
	}
	return 1;
}

/**
 * Check to see if the response has already been validated.
 * @param ret_msg: return msg, can be NULL
 * @return true if the response has already been validated
 */
static int
already_validated(struct dns_msg* ret_msg)
{
	/* validate unchecked, and re-validate bogus messages */
	if (ret_msg && ret_msg->rep->security > sec_status_bogus)
	{
		verbose(VERB_ALGO, "response has already been validated: %s",
			sec_status_to_string(ret_msg->rep->security));
		return 1;
	}
	return 0;
}

/**
 * Generate a request for DNS data.
 *
 * @param qstate: query state that is the parent.
 * @param id: module id.
 * @param name: what name to query for.
 * @param namelen: length of name.
 * @param qtype: query type.
 * @param qclass: query class.
 * @param flags: additional flags, such as the CD bit (BIT_CD), or 0.
 * @param newq: If the subquery is newly created, it is returned,
 * 	otherwise NULL is returned
 * @param detached: true if this qstate should not attach to the subquery
 * @return false on alloc failure.
 */
static int
generate_request(struct module_qstate* qstate, int id, uint8_t* name, 
	size_t namelen, uint16_t qtype, uint16_t qclass, uint16_t flags, 
	struct module_qstate** newq, int detached)
{
	struct val_qstate* vq = (struct val_qstate*)qstate->minfo[id];
	struct query_info ask;
	int valrec;
	ask.qname = name;
	ask.qname_len = namelen;
	ask.qtype = qtype;
	ask.qclass = qclass;
	ask.local_alias = NULL;
	log_query_info(VERB_ALGO, "generate request", &ask);
	/* enable valrec flag to avoid recursion to the same validation
	 * routine, this lookup is simply a lookup. */
	valrec = 1;

	fptr_ok(fptr_whitelist_modenv_detect_cycle(qstate->env->detect_cycle));
	if((*qstate->env->detect_cycle)(qstate, &ask,
		(uint16_t)(BIT_RD|flags), 0, valrec)) {
		verbose(VERB_ALGO, "Could not generate request: cycle detected");
		return 0;
	}

	if(detached) {
		struct mesh_state* sub = NULL;
		fptr_ok(fptr_whitelist_modenv_add_sub(
			qstate->env->add_sub));
		if(!(*qstate->env->add_sub)(qstate, &ask, 
			(uint16_t)(BIT_RD|flags), 0, valrec, newq, &sub)){
			log_err("Could not generate request: out of memory");
			return 0;
		}
	}
	else {
		fptr_ok(fptr_whitelist_modenv_attach_sub(
			qstate->env->attach_sub));
		if(!(*qstate->env->attach_sub)(qstate, &ask, 
			(uint16_t)(BIT_RD|flags), 0, valrec, newq)){
			log_err("Could not generate request: out of memory");
			return 0;
		}
	}
	/* newq; validator does not need state created for that
	 * query, and its a 'normal' for iterator as well */
	if(*newq) {
		/* add our blacklist to the query blacklist */
		sock_list_merge(&(*newq)->blacklist, (*newq)->region,
			vq->chain_blacklist);
	}
	qstate->ext_state[id] = module_wait_subquery;
	return 1;
}

/**
 * Generate, send and detach key tag signaling query.
 *
 * @param qstate: query state.
 * @param id: module id.
 * @param ta: trust anchor, locked.
 * @return false on a processing error.
 */
static int
generate_keytag_query(struct module_qstate* qstate, int id,
	struct trust_anchor* ta)
{
	/* 3 bytes for "_ta", 5 bytes per tag (4 bytes + "-") */
#define MAX_LABEL_TAGS (LDNS_MAX_LABELLEN-3)/5
	size_t i, numtag;
	uint16_t tags[MAX_LABEL_TAGS];
	char tagstr[LDNS_MAX_LABELLEN+1] = "_ta"; /* +1 for NULL byte */
	size_t tagstr_left = sizeof(tagstr) - strlen(tagstr);
	char* tagstr_pos = tagstr + strlen(tagstr);
	uint8_t dnamebuf[LDNS_MAX_DOMAINLEN+1]; /* +1 for label length byte */
	size_t dnamebuf_len = sizeof(dnamebuf);
	uint8_t* keytagdname;
	struct module_qstate* newq = NULL;
	enum module_ext_state ext_state = qstate->ext_state[id];

	numtag = anchor_list_keytags(ta, tags, MAX_LABEL_TAGS);
	if(numtag == 0)
		return 0;

	for(i=0; i<numtag; i++) {
		/* Buffer can't overflow; numtag is limited to tags that fit in
		 * the buffer. */
		snprintf(tagstr_pos, tagstr_left, "-%04x", (unsigned)tags[i]);
		tagstr_left -= strlen(tagstr_pos);
		tagstr_pos += strlen(tagstr_pos);
	}

	sldns_str2wire_dname_buf_origin(tagstr, dnamebuf, &dnamebuf_len,
		ta->name, ta->namelen);
	if(!(keytagdname = (uint8_t*)regional_alloc_init(qstate->region,
		dnamebuf, dnamebuf_len))) {
		log_err("could not generate key tag query: out of memory");
		return 0;
	}

	log_nametypeclass(VERB_OPS, "generate keytag query", keytagdname,
		LDNS_RR_TYPE_NULL, ta->dclass);
	if(!generate_request(qstate, id, keytagdname, dnamebuf_len,
		LDNS_RR_TYPE_NULL, ta->dclass, 0, &newq, 1)) {
		verbose(VERB_ALGO, "failed to generate key tag signaling request");
		return 0;
	}

	/* Not interested in subquery response. Restore the ext_state,
	 * that might be changed by generate_request() */
	qstate->ext_state[id] = ext_state;

	return 1;
}

/**
 * Get keytag as uint16_t from string
 *
 * @param start: start of string containing keytag
 * @param keytag: pointer where to store the extracted keytag
 * @return: 1 if keytag was extracted, else 0.
 */
static int
sentinel_get_keytag(char* start, uint16_t* keytag) {
	char* keytag_str;
	char* e = NULL;
	keytag_str = calloc(1, SENTINEL_KEYTAG_LEN + 1 /* null byte */);
	if(!keytag_str)
		return 0;
	memmove(keytag_str, start, SENTINEL_KEYTAG_LEN);
	keytag_str[SENTINEL_KEYTAG_LEN] = '\0';
	*keytag = (uint16_t)strtol(keytag_str, &e, 10);
	if(!e || *e != '\0') {
		free(keytag_str);
		return 0;
	}
	free(keytag_str);
	return 1;
}

/**
 * Prime trust anchor for use.
 * Generate and dispatch a priming query for the given trust anchor.
 * The trust anchor can be DNSKEY or DS and does not have to be signed.
 *
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param id: module id.
 * @param toprime: what to prime.
 * @return false on a processing error.
 */
static int
prime_trust_anchor(struct module_qstate* qstate, struct val_qstate* vq,
	int id, struct trust_anchor* toprime)
{
	struct module_qstate* newq = NULL;
	int ret = generate_request(qstate, id, toprime->name, toprime->namelen,
		LDNS_RR_TYPE_DNSKEY, toprime->dclass, BIT_CD, &newq, 0);

	if(newq && qstate->env->cfg->trust_anchor_signaling &&
		!generate_keytag_query(qstate, id, toprime)) {
		verbose(VERB_ALGO, "keytag signaling query failed");
		return 0;
	}

	if(!ret) {
		verbose(VERB_ALGO, "Could not prime trust anchor");
		return 0;
	}
	/* ignore newq; validator does not need state created for that
	 * query, and its a 'normal' for iterator as well */
	vq->wait_prime_ta = 1; /* to elicit PRIME_RESP_STATE processing 
		from the validator inform_super() routine */
	/* store trust anchor name for later lookup when prime returns */
	vq->trust_anchor_name = regional_alloc_init(qstate->region,
		toprime->name, toprime->namelen);
	vq->trust_anchor_len = toprime->namelen;
	vq->trust_anchor_labs = toprime->namelabs;
	if(!vq->trust_anchor_name) {
		log_err("Could not prime trust anchor: out of memory");
		return 0;
	}
	return 1;
}

/**
 * Validate if the ANSWER and AUTHORITY sections contain valid rrsets.
 * They must be validly signed with the given key.
 * Tries to validate ADDITIONAL rrsets as well, but only to check them.
 * Allows unsigned CNAME after a DNAME that expands the DNAME.
 * 
 * Note that by the time this method is called, the process of finding the
 * trusted DNSKEY rrset that signs this response must already have been
 * completed.
 * 
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param chase_reply: answer to validate.
 * @param key_entry: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 * @return false if any of the rrsets in the an or ns sections of the message 
 * 	fail to verify. The message is then set to bogus.
 */
static int
validate_msg_signatures(struct module_qstate* qstate, struct val_qstate* vq,
	struct module_env* env, struct val_env* ve,
	struct reply_info* chase_reply, struct key_entry_key* key_entry,
	int* suspend)
{
	uint8_t* sname;
	size_t i, slen;
	struct ub_packed_rrset_key* s;
	enum sec_status sec;
	int num_verifies = 0, verified, have_state = 0;
	char reasonbuf[256];
	char* reason = NULL;
	sldns_ede_code reason_bogus = LDNS_EDE_DNSSEC_BOGUS;
	*suspend = 0;
	if(vq->msg_signatures_state) {
		/* Pick up the state, and reset it, may not be needed now. */
		vq->msg_signatures_state = 0;
		have_state = 1;
	}

	/* validate the ANSWER section */
	for(i=0; i<chase_reply->an_numrrsets; i++) {
		if(have_state && i <= vq->msg_signatures_index)
			continue;
		s = chase_reply->rrsets[i];
		/* Skip the CNAME following a (validated) DNAME.
		 * Because of the normalization routines in the iterator, 
		 * there will always be an unsigned CNAME following a DNAME 
		 * (unless qtype=DNAME in the answer part). */
		if(i>0 && ntohs(chase_reply->rrsets[i-1]->rk.type) ==
			LDNS_RR_TYPE_DNAME &&
			ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME &&
			((struct packed_rrset_data*)chase_reply->rrsets[i-1]->entry.data)->security == sec_status_secure &&
			dname_strict_subdomain_c(s->rk.dname, chase_reply->rrsets[i-1]->rk.dname)
			) {
			/* CNAME was synthesized by our own iterator */
			/* since the DNAME verified, mark the CNAME as secure */
			((struct packed_rrset_data*)s->entry.data)->security =
				sec_status_secure;
			((struct packed_rrset_data*)s->entry.data)->trust =
				rrset_trust_validated;
			continue;
		}

		/* Verify the answer rrset */
		sec = val_verify_rrset_entry(env, ve, s, key_entry, &reason,
			&reason_bogus, LDNS_SECTION_ANSWER, qstate, &verified,
			reasonbuf, sizeof(reasonbuf));
		/* If the (answer) rrset failed to validate, then this 
		 * message is BAD. */
		if(sec != sec_status_secure) {
			log_nametypeclass(VERB_QUERY, "validator: response "
				"has failed ANSWER rrset:", s->rk.dname,
				ntohs(s->rk.type), ntohs(s->rk.rrset_class));
			errinf_ede(qstate, reason, reason_bogus);
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME)
				errinf(qstate, "for CNAME");
			else if(ntohs(s->rk.type) == LDNS_RR_TYPE_DNAME)
				errinf(qstate, "for DNAME");
			errinf_origin(qstate, qstate->reply_origin);
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, reason_bogus);

			return 0;
		}

		num_verifies += verified;
		if(num_verifies > MAX_VALIDATE_AT_ONCE &&
			i+1 < (env->cfg->val_clean_additional?
			chase_reply->an_numrrsets+chase_reply->ns_numrrsets:
			chase_reply->rrset_count)) {
			/* If the number of RRSIGs exceeds the maximum in
			 * one go, suspend. Only suspend if there is a next
			 * rrset to verify, i+1<loopmax. Store where to
			 * continue later. */
			*suspend = 1;
			vq->msg_signatures_state = 1;
			vq->msg_signatures_index = i;
			verbose(VERB_ALGO, "msg signature validation "
				"suspended");
			return 0;
		}
	}

	/* validate the AUTHORITY section */
	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		if(have_state && i <= vq->msg_signatures_index)
			continue;
		s = chase_reply->rrsets[i];
		sec = val_verify_rrset_entry(env, ve, s, key_entry, &reason,
			&reason_bogus, LDNS_SECTION_AUTHORITY, qstate,
			&verified, reasonbuf, sizeof(reasonbuf));
		/* If anything in the authority section fails to be secure, 
		 * we have a bad message. */
		if(sec != sec_status_secure) {
			log_nametypeclass(VERB_QUERY, "validator: response "
				"has failed AUTHORITY rrset:", s->rk.dname,
				ntohs(s->rk.type), ntohs(s->rk.rrset_class));
			errinf_ede(qstate, reason, reason_bogus);
			errinf_origin(qstate, qstate->reply_origin);
			errinf_rrset(qstate, s);
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, reason_bogus);
			return 0;
		}
		num_verifies += verified;
		if(num_verifies > MAX_VALIDATE_AT_ONCE &&
			i+1 < (env->cfg->val_clean_additional?
			chase_reply->an_numrrsets+chase_reply->ns_numrrsets:
			chase_reply->rrset_count)) {
			*suspend = 1;
			vq->msg_signatures_state = 1;
			vq->msg_signatures_index = i;
			verbose(VERB_ALGO, "msg signature validation "
				"suspended");
			return 0;
		}
	}

	/* If set, the validator should clean the additional section of
	 * secure messages. */
	if(!env->cfg->val_clean_additional)
		return 1;
	/* attempt to validate the ADDITIONAL section rrsets */
	for(i=chase_reply->an_numrrsets+chase_reply->ns_numrrsets; 
		i<chase_reply->rrset_count; i++) {
		if(have_state && i <= vq->msg_signatures_index)
			continue;
		s = chase_reply->rrsets[i];
		/* only validate rrs that have signatures with the key */
		/* leave others unchecked, those get removed later on too */
		val_find_rrset_signer(s, &sname, &slen);

		verified = 0;
		if(sname && query_dname_compare(sname, key_entry->name)==0)
			(void)val_verify_rrset_entry(env, ve, s, key_entry,
				&reason, NULL, LDNS_SECTION_ADDITIONAL, qstate,
				&verified, reasonbuf, sizeof(reasonbuf));
		/* the additional section can fail to be secure, 
		 * it is optional, check signature in case we need
		 * to clean the additional section later. */
		num_verifies += verified;
		if(num_verifies > MAX_VALIDATE_AT_ONCE &&
			i+1 < chase_reply->rrset_count) {
			*suspend = 1;
			vq->msg_signatures_state = 1;
			vq->msg_signatures_index = i;
			verbose(VERB_ALGO, "msg signature validation "
				"suspended");
			return 0;
		}
	}

	return 1;
}

void
validate_suspend_timer_cb(void* arg)
{
	struct module_qstate* qstate = (struct module_qstate*)arg;
	verbose(VERB_ALGO, "validate_suspend timer, continue");
	mesh_run(qstate->env->mesh, qstate->mesh_info, module_event_pass,
		NULL);
}

/** Setup timer to continue validation of msg signatures later */
static int
validate_suspend_setup_timer(struct module_qstate* qstate,
	struct val_qstate* vq, int id, enum val_state resume_state)
{
	struct timeval tv;
	int usec, slack, base;
	if(vq->suspend_count >= MAX_VALIDATION_SUSPENDS) {
		verbose(VERB_ALGO, "validate_suspend timer: "
			"reached MAX_VALIDATION_SUSPENDS (%d); error out",
			MAX_VALIDATION_SUSPENDS);
		errinf(qstate, "max validation suspends reached, "
			"too many RRSIG validations");
		return 0;
	}
	verbose(VERB_ALGO, "validate_suspend timer, set for suspend");
	vq->state = resume_state;
	qstate->ext_state[id] = module_wait_reply;
	if(!vq->suspend_timer) {
		vq->suspend_timer = comm_timer_create(
			qstate->env->worker_base,
			validate_suspend_timer_cb, qstate);
		if(!vq->suspend_timer) {
			log_err("validate_suspend_setup_timer: "
				"out of memory for comm_timer_create");
			return 0;
		}
	}
	/* The timer is activated later, after other events in the event
	 * loop have been processed. The query state can also be deleted,
	 * when the list is full and query states are dropped. */
	/* Extend wait time if there are a lot of queries or if this one
	 * is taking long, to keep around cpu time for ordinary queries. */
	usec = 50000; /* 50 msec */
	slack = 0;
	if(qstate->env->mesh->all.count >= qstate->env->mesh->max_reply_states)
		slack += 3;
	else if(qstate->env->mesh->all.count >= qstate->env->mesh->max_reply_states/2)
		slack += 2;
	else if(qstate->env->mesh->all.count >= qstate->env->mesh->max_reply_states/4)
		slack += 1;
	if(vq->suspend_count > 3)
		slack += 3;
	else if(vq->suspend_count > 0)
		slack += vq->suspend_count;
	if(slack != 0 && slack <= 12 /* No numeric overflow. */) {
		usec = usec << slack;
	}
	/* Spread such timeouts within 90%-100% of the original timer. */
	base = usec * 9/10;
	usec = base + ub_random_max(qstate->env->rnd, usec-base);
	tv.tv_usec = (usec % 1000000);
	tv.tv_sec = (usec / 1000000);
	vq->suspend_count ++;
	comm_timer_set(vq->suspend_timer, &tv);
	return 1;
}

/**
 * Detect wrong truncated response (say from BIND 9.6.1 that is forwarding
 * and saw the NS record without signatures from a referral).
 * The positive response has a mangled authority section.
 * Remove that authority section and the additional section.
 * @param rep: reply
 * @return true if a wrongly truncated response.
 */
static int
detect_wrongly_truncated(struct reply_info* rep)
{
	size_t i;
	/* only NS in authority, and it is bogus */
	if(rep->ns_numrrsets != 1 || rep->an_numrrsets == 0)
		return 0;
	if(ntohs(rep->rrsets[ rep->an_numrrsets ]->rk.type) != LDNS_RR_TYPE_NS)
		return 0;
	if(((struct packed_rrset_data*)rep->rrsets[ rep->an_numrrsets ]
		->entry.data)->security == sec_status_secure)
		return 0;
	/* answer section is present and secure */
	for(i=0; i<rep->an_numrrsets; i++) {
		if(((struct packed_rrset_data*)rep->rrsets[ i ]
			->entry.data)->security != sec_status_secure)
			return 0;
	}
	verbose(VERB_ALGO, "truncating to minimal response");
	return 1;
}

/**
 * For messages that are not referrals, if the chase reply contains an
 * unsigned NS record in the authority section it could have been
 * inserted by a (BIND) forwarder that thinks the zone is insecure, and
 * that has an NS record without signatures in cache.  Remove the NS
 * record since the reply does not hinge on that record (in the authority
 * section), but do not remove it if it removes the last record from the
 * answer+authority sections.
 * @param chase_reply: the chased reply, we have a key for this contents,
 * 	so we should have signatures for these rrsets and not having
 * 	signatures means it will be bogus.
 * @param orig_reply: original reply, remove NS from there as well because
 * 	we cannot mark the NS record as DNSSEC valid because it is not
 * 	validated by signatures.
 */
static void
remove_spurious_authority(struct reply_info* chase_reply,
	struct reply_info* orig_reply)
{
	size_t i, found = 0;
	int remove = 0;
	/* if no answer and only 1 auth RRset, do not remove that one */
	if(chase_reply->an_numrrsets == 0 && chase_reply->ns_numrrsets == 1)
		return;
	/* search authority section for unsigned NS records */
	for(i = chase_reply->an_numrrsets;
		i < chase_reply->an_numrrsets+chase_reply->ns_numrrsets; i++) {
		struct packed_rrset_data* d = (struct packed_rrset_data*)
			chase_reply->rrsets[i]->entry.data;
		if(ntohs(chase_reply->rrsets[i]->rk.type) == LDNS_RR_TYPE_NS
			&& d->rrsig_count == 0) {
			found = i;
			remove = 1;
			break;
		}
	}
	/* see if we found the entry */
	if(!remove) return;
	log_rrset_key(VERB_ALGO, "Removing spurious unsigned NS record "
		"(likely inserted by forwarder)", chase_reply->rrsets[found]);

	/* find rrset in orig_reply */
	for(i = orig_reply->an_numrrsets;
		i < orig_reply->an_numrrsets+orig_reply->ns_numrrsets; i++) {
		if(ntohs(orig_reply->rrsets[i]->rk.type) == LDNS_RR_TYPE_NS
			&& query_dname_compare(orig_reply->rrsets[i]->rk.dname,
				chase_reply->rrsets[found]->rk.dname) == 0) {
			/* remove from orig_msg */
			val_reply_remove_auth(orig_reply, i);
			break;
		}
	}
	/* remove rrset from chase_reply */
	val_reply_remove_auth(chase_reply, found);
}

/**
 * Given a "positive" response -- a response that contains an answer to the
 * question, and no CNAME chain, validate this response. 
 *
 * The answer and authority RRsets must already be verified as secure.
 * 
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_positive_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, struct module_qstate* qstate,
	struct val_qstate* vq, int* nsec3_calculations, int* suspend)
{
	uint8_t* wc = NULL;
	size_t wl;
	int wc_cached = 0;
	int wc_NSEC_ok = 0;
	int nsec3s_seen = 0;
	size_t i;
	struct ub_packed_rrset_key* s;
	*suspend = 0;

	/* validate the ANSWER section - this will be the answer itself */
	for(i=0; i<chase_reply->an_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* Check to see if the rrset is the result of a wildcard 
		 * expansion. If so, an additional check will need to be 
		 * made in the authority section. */
		if(!val_rrset_wildcard(s, &wc, &wl)) {
			log_nametypeclass(VERB_QUERY, "Positive response has "
				"inconsistent wildcard sigs:", s->rk.dname,
				ntohs(s->rk.type), ntohs(s->rk.rrset_class));
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
			return;
		}
		if(wc && !wc_cached && env->cfg->aggressive_nsec) {
			rrset_cache_update_wildcard(env->rrset_cache, s, wc, wl,
				env->alloc, *env->now);
			wc_cached = 1;
		}

	}

	/* validate the AUTHORITY section as well - this will generally be 
	 * the NS rrset (which could be missing, no problem) */
	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* If this is a positive wildcard response, and we have a 
		 * (just verified) NSEC record, try to use it to 1) prove 
		 * that qname doesn't exist and 2) that the correct wildcard 
		 * was used. */
		if(wc != NULL && ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(val_nsec_proves_positive_wildcard(s, qchase, wc)) {
				wc_NSEC_ok = 1;
			}
			/* if not, continue looking for proof */
		}

		/* Otherwise, if this is a positive wildcard response and 
		 * we have NSEC3 records */
		if(wc != NULL && ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			nsec3s_seen = 1;
		}
	}

	/* If this was a positive wildcard response that we haven't already
	 * proven, and we have NSEC3 records, try to prove it using the NSEC3
	 * records. */
	if(wc != NULL && !wc_NSEC_ok && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		enum sec_status sec = nsec3_prove_wildcard(env, ve,
			chase_reply->rrsets+chase_reply->an_numrrsets,
			chase_reply->ns_numrrsets, qchase, kkey, wc,
			&vq->nsec3_cache_table, nsec3_calculations);
		if(sec == sec_status_insecure) {
			verbose(VERB_ALGO, "Positive wildcard response is "
				"insecure");
			chase_reply->security = sec_status_insecure;
			return;
		} else if(sec == sec_status_secure) {
			wc_NSEC_ok = 1;
		} else if(sec == sec_status_unchecked) {
			*suspend = 1;
			return;
		}
	}

	/* If after all this, we still haven't proven the positive wildcard
	 * response, fail. */
	if(wc != NULL && !wc_NSEC_ok) {
		verbose(VERB_QUERY, "positive response was wildcard "
			"expansion and did not prove original data "
			"did not exist");
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	verbose(VERB_ALGO, "Successfully validated positive response");
	chase_reply->security = sec_status_secure;
}

/** 
 * Validate a NOERROR/NODATA signed response -- a response that has a
 * NOERROR Rcode but no ANSWER section RRsets. This consists of making 
 * certain that the authority section NSEC/NSEC3s proves that the qname 
 * does exist and the qtype doesn't.
 *
 * The answer and authority RRsets must already be verified as secure.
 *
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_nodata_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, struct module_qstate* qstate,
	struct val_qstate* vq, int* nsec3_calculations, int* suspend)
{
	/* Since we are here, there must be nothing in the ANSWER section to
	 * validate. */
	/* (Note: CNAME/DNAME responses will not directly get here --
	 * instead, they are chased down into individual CNAME validations,
	 * and at the end of the cname chain a POSITIVE, or CNAME_NOANSWER 
	 * validation.) */
	
	/* validate the AUTHORITY section */
	int has_valid_nsec = 0; /* If true, then the NODATA has been proven.*/
	uint8_t* ce = NULL; /* for wildcard nodata responses. This is the 
				proven closest encloser. */
	uint8_t* wc = NULL; /* for wildcard nodata responses. wildcard nsec */
	int nsec3s_seen = 0; /* nsec3s seen */
	struct ub_packed_rrset_key* s; 
	size_t i;
	*suspend = 0;

	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		s = chase_reply->rrsets[i];
		/* If we encounter an NSEC record, try to use it to prove 
		 * NODATA.
		 * This needs to handle the ENT NODATA case. */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(nsec_proves_nodata(s, qchase, &wc)) {
				has_valid_nsec = 1;
				/* sets wc-encloser if wildcard applicable */
			} 
			if(val_nsec_proves_name_error(s, qchase->qname)) {
				ce = nsec_closest_encloser(qchase->qname, s);
			}
			if(val_nsec_proves_insecuredelegation(s, qchase)) {
				verbose(VERB_ALGO, "delegation is insecure");
				chase_reply->security = sec_status_insecure;
				return;
			}
		} else if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			nsec3s_seen = 1;
		}
	}

	/* check to see if we have a wildcard NODATA proof. */

	/* The wildcard NODATA is 1 NSEC proving that qname does not exist 
	 * (and also proving what the closest encloser is), and 1 NSEC 
	 * showing the matching wildcard, which must be *.closest_encloser. */
	if(wc && !ce)
		has_valid_nsec = 0;
	else if(wc && ce) {
		if(query_dname_compare(wc, ce) != 0) {
			has_valid_nsec = 0;
		}
	}
	
	if(!has_valid_nsec && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		enum sec_status sec = nsec3_prove_nodata(env, ve, 
			chase_reply->rrsets+chase_reply->an_numrrsets,
			chase_reply->ns_numrrsets, qchase, kkey,
			&vq->nsec3_cache_table, nsec3_calculations);
		if(sec == sec_status_insecure) {
			verbose(VERB_ALGO, "NODATA response is insecure");
			chase_reply->security = sec_status_insecure;
			return;
		} else if(sec == sec_status_secure) {
			has_valid_nsec = 1;
		} else if(sec == sec_status_unchecked) {
			/* check is incomplete; suspend */
			*suspend = 1;
			return;
		}
	}

	if(!has_valid_nsec) {
		verbose(VERB_QUERY, "NODATA response failed to prove NODATA "
			"status with NSEC/NSEC3");
		if(verbosity >= VERB_ALGO)
			log_dns_msg("Failed NODATA", qchase, chase_reply);
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	verbose(VERB_ALGO, "successfully validated NODATA response.");
	chase_reply->security = sec_status_secure;
}

/** 
 * Validate a NAMEERROR signed response -- a response that has a NXDOMAIN
 * Rcode. 
 * This consists of making certain that the authority section NSEC proves 
 * that the qname doesn't exist and the covering wildcard also doesn't exist..
 * 
 * The answer and authority RRsets must have already been verified as secure.
 *
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param rcode: adjusted RCODE, in case of RCODE/proof mismatch leniency.
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_nameerror_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, int* rcode,
	struct module_qstate* qstate, struct val_qstate* vq,
	int* nsec3_calculations, int* suspend)
{
	int has_valid_nsec = 0;
	int has_valid_wnsec = 0;
	int nsec3s_seen = 0;
	struct ub_packed_rrset_key* s; 
	size_t i;
	uint8_t* ce;
	int ce_labs = 0;
	int prev_ce_labs = 0;
	*suspend = 0;

	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		s = chase_reply->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(val_nsec_proves_name_error(s, qchase->qname))
				has_valid_nsec = 1;
			ce = nsec_closest_encloser(qchase->qname, s);            
			ce_labs = dname_count_labels(ce);                        
			/* Use longest closest encloser to prove wildcard. */
			if(ce_labs > prev_ce_labs ||                             
			       (ce_labs == prev_ce_labs &&                      
				       has_valid_wnsec == 0)) {                 
			       if(val_nsec_proves_no_wc(s, qchase->qname,       
				       qchase->qname_len))                      
				       has_valid_wnsec = 1;                     
			       else                                             
				       has_valid_wnsec = 0;                     
			}                                                        
			prev_ce_labs = ce_labs; 
			if(val_nsec_proves_insecuredelegation(s, qchase)) {
				verbose(VERB_ALGO, "delegation is insecure");
				chase_reply->security = sec_status_insecure;
				return;
			}
		} else if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3)
			nsec3s_seen = 1;
	}

	if((!has_valid_nsec || !has_valid_wnsec) && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		/* use NSEC3 proof, both answer and auth rrsets, in case
		 * NSEC3s end up in the answer (due to qtype=NSEC3 or so) */
		chase_reply->security = nsec3_prove_nameerror(env, ve,
			chase_reply->rrsets, chase_reply->an_numrrsets+
			chase_reply->ns_numrrsets, qchase, kkey,
			&vq->nsec3_cache_table, nsec3_calculations);
		if(chase_reply->security == sec_status_unchecked) {
			*suspend = 1;
			return;
		} else if(chase_reply->security != sec_status_secure) {
			verbose(VERB_QUERY, "NameError response failed nsec, "
				"nsec3 proof was %s", sec_status_to_string(
				chase_reply->security));
			return;
		}
		has_valid_nsec = 1;
		has_valid_wnsec = 1;
	}

	/* If the message fails to prove either condition, it is bogus. */
	if(!has_valid_nsec) {
		validate_nodata_response(env, ve, qchase, chase_reply, kkey,
			qstate, vq, nsec3_calculations, suspend);
		if(*suspend) return;
		verbose(VERB_QUERY, "NameError response has failed to prove: "
		          "qname does not exist");
		/* Be lenient with RCODE in NSEC NameError responses */
		if(chase_reply->security == sec_status_secure) {
			*rcode = LDNS_RCODE_NOERROR;
		} else {
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		}
		return;
	}

	if(!has_valid_wnsec) {
		validate_nodata_response(env, ve, qchase, chase_reply, kkey,
			qstate, vq, nsec3_calculations, suspend);
		if(*suspend) return;
		verbose(VERB_QUERY, "NameError response has failed to prove: "
		          "covering wildcard does not exist");
		/* Be lenient with RCODE in NSEC NameError responses */
		if (chase_reply->security == sec_status_secure) {
			*rcode = LDNS_RCODE_NOERROR;
		} else {
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		}
		return;
	}

	/* Otherwise, we consider the message secure. */
	verbose(VERB_ALGO, "successfully validated NAME ERROR response.");
	chase_reply->security = sec_status_secure;
}

/** 
 * Given a referral response, validate rrsets and take least trusted rrset
 * as the current validation status.
 * 
 * Note that by the time this method is called, the process of finding the
 * trusted DNSKEY rrset that signs this response must already have been
 * completed.
 * 
 * @param chase_reply: answer to validate.
 */
static void
validate_referral_response(struct reply_info* chase_reply)
{
	size_t i;
	enum sec_status s;
	/* message security equals lowest rrset security */
	chase_reply->security = sec_status_secure;
	for(i=0; i<chase_reply->rrset_count; i++) {
		s = ((struct packed_rrset_data*)chase_reply->rrsets[i]
			->entry.data)->security;
		if(s < chase_reply->security)
			chase_reply->security = s;
	}
	verbose(VERB_ALGO, "validated part of referral response as %s",
		sec_status_to_string(chase_reply->security));
}

/** 
 * Given an "ANY" response -- a response that contains an answer to a
 * qtype==ANY question, with answers. This does no checking that all 
 * types are present.
 * 
 * NOTE: it may be possible to get parent-side delegation point records
 * here, which won't all be signed. Right now, this routine relies on the
 * upstream iterative resolver to not return these responses -- instead
 * treating them as referrals.
 * 
 * NOTE: RFC 4035 is silent on this issue, so this may change upon
 * clarification. Clarification draft -05 says to not check all types are
 * present.
 * 
 * Note that by the time this method is called, the process of finding the
 * trusted DNSKEY rrset that signs this response must already have been
 * completed.
 * 
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_any_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, struct module_qstate* qstate,
	struct val_qstate* vq, int* nsec3_calculations, int* suspend)
{
	/* all answer and auth rrsets already verified */
	/* but check if a wildcard response is given, then check NSEC/NSEC3
	 * for qname denial to see if wildcard is applicable */
	uint8_t* wc = NULL;
	size_t wl;
	int wc_NSEC_ok = 0;
	int nsec3s_seen = 0;
	size_t i;
	struct ub_packed_rrset_key* s;
	*suspend = 0;

	if(qchase->qtype != LDNS_RR_TYPE_ANY) {
		log_err("internal error: ANY validation called for non-ANY");
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	/* validate the ANSWER section - this will be the answer itself */
	for(i=0; i<chase_reply->an_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* Check to see if the rrset is the result of a wildcard 
		 * expansion. If so, an additional check will need to be 
		 * made in the authority section. */
		if(!val_rrset_wildcard(s, &wc, &wl)) {
			log_nametypeclass(VERB_QUERY, "Positive ANY response"
				" has inconsistent wildcard sigs:", 
				s->rk.dname, ntohs(s->rk.type), 
				ntohs(s->rk.rrset_class));
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
			return;
		}
	}

	/* if it was a wildcard, check for NSEC/NSEC3s in both answer
	 * and authority sections (NSEC may be moved to the ANSWER section) */
	if(wc != NULL)
	  for(i=0; i<chase_reply->an_numrrsets+chase_reply->ns_numrrsets; 
	  	i++) {
		s = chase_reply->rrsets[i];

		/* If this is a positive wildcard response, and we have a 
		 * (just verified) NSEC record, try to use it to 1) prove 
		 * that qname doesn't exist and 2) that the correct wildcard 
		 * was used. */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(val_nsec_proves_positive_wildcard(s, qchase, wc)) {
				wc_NSEC_ok = 1;
			}
			/* if not, continue looking for proof */
		}

		/* Otherwise, if this is a positive wildcard response and 
		 * we have NSEC3 records */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			nsec3s_seen = 1;
		}
	}

	/* If this was a positive wildcard response that we haven't already
	 * proven, and we have NSEC3 records, try to prove it using the NSEC3
	 * records. */
	if(wc != NULL && !wc_NSEC_ok && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		/* look both in answer and auth section for NSEC3s */
		enum sec_status sec = nsec3_prove_wildcard(env, ve,
			chase_reply->rrsets,
			chase_reply->an_numrrsets+chase_reply->ns_numrrsets,
			qchase, kkey, wc, &vq->nsec3_cache_table,
			nsec3_calculations);
		if(sec == sec_status_insecure) {
			verbose(VERB_ALGO, "Positive ANY wildcard response is "
				"insecure");
			chase_reply->security = sec_status_insecure;
			return;
		} else if(sec == sec_status_secure) {
			wc_NSEC_ok = 1;
		} else if(sec == sec_status_unchecked) {
			*suspend = 1;
			return;
		}
	}

	/* If after all this, we still haven't proven the positive wildcard
	 * response, fail. */
	if(wc != NULL && !wc_NSEC_ok) {
		verbose(VERB_QUERY, "positive ANY response was wildcard "
			"expansion and did not prove original data "
			"did not exist");
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	verbose(VERB_ALGO, "Successfully validated positive ANY response");
	chase_reply->security = sec_status_secure;
}

/**
 * Validate CNAME response, or DNAME+CNAME.
 * This is just like a positive proof, except that this is about a 
 * DNAME+CNAME. Possible wildcard proof.
 * Difference with positive proof is that this routine refuses 
 * wildcarded DNAMEs.
 * 
 * The answer and authority rrsets must already be verified as secure.
 * 
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_cname_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, struct module_qstate* qstate,
	struct val_qstate* vq, int* nsec3_calculations, int* suspend)
{
	uint8_t* wc = NULL;
	size_t wl;
	int wc_NSEC_ok = 0;
	int nsec3s_seen = 0;
	size_t i;
	struct ub_packed_rrset_key* s;
	*suspend = 0;

	/* validate the ANSWER section - this will be the CNAME (+DNAME) */
	for(i=0; i<chase_reply->an_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* Check to see if the rrset is the result of a wildcard 
		 * expansion. If so, an additional check will need to be 
		 * made in the authority section. */
		if(!val_rrset_wildcard(s, &wc, &wl)) {
			log_nametypeclass(VERB_QUERY, "Cname response has "
				"inconsistent wildcard sigs:", s->rk.dname,
				ntohs(s->rk.type), ntohs(s->rk.rrset_class));
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
			return;
		}
		
		/* Refuse wildcarded DNAMEs rfc 4597. 
		 * Do not follow a wildcarded DNAME because 
		 * its synthesized CNAME expansion is underdefined */
		if(qchase->qtype != LDNS_RR_TYPE_DNAME && 
			ntohs(s->rk.type) == LDNS_RR_TYPE_DNAME && wc) {
			log_nametypeclass(VERB_QUERY, "cannot validate a "
				"wildcarded DNAME:", s->rk.dname, 
				ntohs(s->rk.type), ntohs(s->rk.rrset_class));
			chase_reply->security = sec_status_bogus;
			update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
			return;
		}

		/* If we have found a CNAME, stop looking for one.
		 * The iterator has placed the CNAME chain in correct
		 * order. */
		if (ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME) {
			break;
		}
	}

	/* AUTHORITY section */
	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* If this is a positive wildcard response, and we have a 
		 * (just verified) NSEC record, try to use it to 1) prove 
		 * that qname doesn't exist and 2) that the correct wildcard 
		 * was used. */
		if(wc != NULL && ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(val_nsec_proves_positive_wildcard(s, qchase, wc)) {
				wc_NSEC_ok = 1;
			}
			/* if not, continue looking for proof */
		}

		/* Otherwise, if this is a positive wildcard response and 
		 * we have NSEC3 records */
		if(wc != NULL && ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			nsec3s_seen = 1;
		}
	}

	/* If this was a positive wildcard response that we haven't already
	 * proven, and we have NSEC3 records, try to prove it using the NSEC3
	 * records. */
	if(wc != NULL && !wc_NSEC_ok && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		enum sec_status sec = nsec3_prove_wildcard(env, ve,
			chase_reply->rrsets+chase_reply->an_numrrsets,
			chase_reply->ns_numrrsets, qchase, kkey, wc,
			&vq->nsec3_cache_table, nsec3_calculations);
		if(sec == sec_status_insecure) {
			verbose(VERB_ALGO, "wildcard CNAME response is "
				"insecure");
			chase_reply->security = sec_status_insecure;
			return;
		} else if(sec == sec_status_secure) {
			wc_NSEC_ok = 1;
		} else if(sec == sec_status_unchecked) {
			*suspend = 1;
			return;
		}
	}

	/* If after all this, we still haven't proven the positive wildcard
	 * response, fail. */
	if(wc != NULL && !wc_NSEC_ok) {
		verbose(VERB_QUERY, "CNAME response was wildcard "
			"expansion and did not prove original data "
			"did not exist");
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	verbose(VERB_ALGO, "Successfully validated CNAME response");
	chase_reply->security = sec_status_secure;
}

/**
 * Validate CNAME NOANSWER response, no more data after a CNAME chain.
 * This can be a NODATA or a NAME ERROR case, but not both at the same time.
 * We don't know because the rcode has been set to NOERROR by the CNAME.
 * 
 * The answer and authority rrsets must already be verified as secure.
 * 
 * @param env: module env for verify.
 * @param ve: validator env for verify.
 * @param qchase: query that was made.
 * @param chase_reply: answer to that query to validate.
 * @param kkey: the key entry, which is trusted, and which matches
 * 	the signer of the answer. The key entry isgood().
 * @param qstate: query state for the region.
 * @param vq: validator state for the nsec3 cache table.
 * @param nsec3_calculations: current nsec3 hash calculations.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 */
static void
validate_cname_noanswer_response(struct module_env* env, struct val_env* ve,
	struct query_info* qchase, struct reply_info* chase_reply,
	struct key_entry_key* kkey, struct module_qstate* qstate,
	struct val_qstate* vq, int* nsec3_calculations, int* suspend)
{
	int nodata_valid_nsec = 0; /* If true, then NODATA has been proven.*/
	uint8_t* ce = NULL; /* for wildcard nodata responses. This is the 
				proven closest encloser. */
	uint8_t* wc = NULL; /* for wildcard nodata responses. wildcard nsec */
	int nxdomain_valid_nsec = 0; /* if true, nameerror has been proven */
	int nxdomain_valid_wnsec = 0;
	int nsec3s_seen = 0; /* nsec3s seen */
	struct ub_packed_rrset_key* s; 
	size_t i;
	uint8_t* nsec_ce; /* Used to find the NSEC with the longest ce */
	int ce_labs = 0;
	int prev_ce_labs = 0;
	*suspend = 0;

	/* the AUTHORITY section */
	for(i=chase_reply->an_numrrsets; i<chase_reply->an_numrrsets+
		chase_reply->ns_numrrsets; i++) {
		s = chase_reply->rrsets[i];

		/* If we encounter an NSEC record, try to use it to prove 
		 * NODATA. This needs to handle the ENT NODATA case. 
		 * Also try to prove NAMEERROR, and absence of a wildcard */
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC) {
			if(nsec_proves_nodata(s, qchase, &wc)) {
				nodata_valid_nsec = 1;
				/* set wc encloser if wildcard applicable */
			} 
			if(val_nsec_proves_name_error(s, qchase->qname)) {
				ce = nsec_closest_encloser(qchase->qname, s);
				nxdomain_valid_nsec = 1;
			}
			nsec_ce = nsec_closest_encloser(qchase->qname, s);
			ce_labs = dname_count_labels(nsec_ce);
			/* Use longest closest encloser to prove wildcard. */
			if(ce_labs > prev_ce_labs ||
			       (ce_labs == prev_ce_labs &&
				       nxdomain_valid_wnsec == 0)) {
			       if(val_nsec_proves_no_wc(s, qchase->qname,
				       qchase->qname_len))
				       nxdomain_valid_wnsec = 1;
			       else
				       nxdomain_valid_wnsec = 0;
			}
			prev_ce_labs = ce_labs;
			if(val_nsec_proves_insecuredelegation(s, qchase)) {
				verbose(VERB_ALGO, "delegation is insecure");
				chase_reply->security = sec_status_insecure;
				return;
			}
		} else if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			nsec3s_seen = 1;
		}
	}

	/* check to see if we have a wildcard NODATA proof. */

	/* The wildcard NODATA is 1 NSEC proving that qname does not exists 
	 * (and also proving what the closest encloser is), and 1 NSEC 
	 * showing the matching wildcard, which must be *.closest_encloser. */
	if(wc && !ce)
		nodata_valid_nsec = 0;
	else if(wc && ce) {
		if(query_dname_compare(wc, ce) != 0) {
			nodata_valid_nsec = 0;
		}
	}
	if(nxdomain_valid_nsec && !nxdomain_valid_wnsec) {
		/* name error is missing wildcard denial proof */
		nxdomain_valid_nsec = 0;
	}
	
	if(nodata_valid_nsec && nxdomain_valid_nsec) {
		verbose(VERB_QUERY, "CNAMEchain to noanswer proves that name "
			"exists and not exists, bogus");
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}
	if(!nodata_valid_nsec && !nxdomain_valid_nsec && nsec3s_seen &&
		nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
		int nodata;
		enum sec_status sec = nsec3_prove_nxornodata(env, ve, 
			chase_reply->rrsets+chase_reply->an_numrrsets,
			chase_reply->ns_numrrsets, qchase, kkey, &nodata,
			&vq->nsec3_cache_table, nsec3_calculations);
		if(sec == sec_status_insecure) {
			verbose(VERB_ALGO, "CNAMEchain to noanswer response "
				"is insecure");
			chase_reply->security = sec_status_insecure;
			return;
		} else if(sec == sec_status_secure) {
			if(nodata)
				nodata_valid_nsec = 1;
			else	nxdomain_valid_nsec = 1;
		} else if(sec == sec_status_unchecked) {
			*suspend = 1;
			return;
		}
	}

	if(!nodata_valid_nsec && !nxdomain_valid_nsec) {
		verbose(VERB_QUERY, "CNAMEchain to noanswer response failed "
			"to prove status with NSEC/NSEC3");
		if(verbosity >= VERB_ALGO)
			log_dns_msg("Failed CNAMEnoanswer", qchase, chase_reply);
		chase_reply->security = sec_status_bogus;
		update_reason_bogus(chase_reply, LDNS_EDE_DNSSEC_BOGUS);
		return;
	}

	if(nodata_valid_nsec)
		verbose(VERB_ALGO, "successfully validated CNAME chain to a "
			"NODATA response.");
	else	verbose(VERB_ALGO, "successfully validated CNAME chain to a "
			"NAMEERROR response.");
	chase_reply->security = sec_status_secure;
}

/** 
 * Process init state for validator.
 * Process the INIT state. First tier responses start in the INIT state.
 * This is where they are vetted for validation suitability, and the initial
 * key search is done.
 * 
 * Currently, events the come through this routine will be either promoted
 * to FINISHED/CNAME_RESP (no validation needed), FINDKEY (next step to
 * validation), or will be (temporarily) retired and a new priming request
 * event will be generated.
 *
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param ve: validator shared global environment.
 * @param id: module id.
 * @return true if the event should be processed further on return, false if
 *         not.
 */
static int
processInit(struct module_qstate* qstate, struct val_qstate* vq, 
	struct val_env* ve, int id)
{
	uint8_t* lookup_name;
	size_t lookup_len;
	struct trust_anchor* anchor;
	enum val_classification subtype = val_classify_response(
		qstate->query_flags, &qstate->qinfo, &vq->qchase, 
		vq->orig_msg->rep, vq->rrset_skip);
	if(vq->restart_count > ve->max_restart) {
		verbose(VERB_ALGO, "restart count exceeded");
		return val_error(qstate, id);
	}

	/* correctly initialize reason_bogus */
	update_reason_bogus(vq->chase_reply, LDNS_EDE_DNSSEC_BOGUS);

	verbose(VERB_ALGO, "validator classification %s", 
		val_classification_to_string(subtype));
	if(subtype == VAL_CLASS_REFERRAL && 
		vq->rrset_skip < vq->orig_msg->rep->rrset_count) {
		/* referral uses the rrset name as qchase, to find keys for
		 * that rrset */
		vq->qchase.qname = vq->orig_msg->rep->
			rrsets[vq->rrset_skip]->rk.dname;
		vq->qchase.qname_len = vq->orig_msg->rep->
			rrsets[vq->rrset_skip]->rk.dname_len;
		vq->qchase.qtype = ntohs(vq->orig_msg->rep->
			rrsets[vq->rrset_skip]->rk.type);
		vq->qchase.qclass = ntohs(vq->orig_msg->rep->
			rrsets[vq->rrset_skip]->rk.rrset_class);
	}
	lookup_name = vq->qchase.qname;
	lookup_len = vq->qchase.qname_len;
	/* for type DS look at the parent side for keys/trustanchor */
	/* also for NSEC not at apex */
	if(vq->qchase.qtype == LDNS_RR_TYPE_DS ||
		(vq->qchase.qtype == LDNS_RR_TYPE_NSEC && 
		 vq->orig_msg->rep->rrset_count > vq->rrset_skip &&
		 ntohs(vq->orig_msg->rep->rrsets[vq->rrset_skip]->rk.type) ==
		 LDNS_RR_TYPE_NSEC &&
		 !(vq->orig_msg->rep->rrsets[vq->rrset_skip]->
		 rk.flags&PACKED_RRSET_NSEC_AT_APEX))) {
		dname_remove_label(&lookup_name, &lookup_len);
	}

	val_mark_indeterminate(vq->chase_reply, qstate->env->anchors, 
		qstate->env->rrset_cache, qstate->env);
	vq->key_entry = NULL;
	vq->empty_DS_name = NULL;
	vq->ds_rrset = 0;
	anchor = anchors_lookup(qstate->env->anchors, 
		lookup_name, lookup_len, vq->qchase.qclass);

	/* Determine the signer/lookup name */
	val_find_signer(subtype, &vq->qchase, vq->orig_msg->rep, 
		vq->rrset_skip, &vq->signer_name, &vq->signer_len);
	if(vq->signer_name != NULL &&
		!dname_subdomain_c(lookup_name, vq->signer_name)) {
		log_nametypeclass(VERB_ALGO, "this signer name is not a parent "
			"of lookupname, omitted", vq->signer_name, 0, 0);
		vq->signer_name = NULL;
	}
	if(vq->signer_name == NULL) {
		log_nametypeclass(VERB_ALGO, "no signer, using", lookup_name,
			0, 0);
	} else {
		lookup_name = vq->signer_name;
		lookup_len = vq->signer_len;
		log_nametypeclass(VERB_ALGO, "signer is", lookup_name, 0, 0);
	}

	/* for NXDOMAIN it could be signed by a parent of the trust anchor */
	if(subtype == VAL_CLASS_NAMEERROR && vq->signer_name &&
		anchor && dname_strict_subdomain_c(anchor->name, lookup_name)){
		lock_basic_unlock(&anchor->lock);
		anchor = anchors_lookup(qstate->env->anchors, 
			lookup_name, lookup_len, vq->qchase.qclass);
		if(!anchor) { /* unsigned parent denies anchor*/
			verbose(VERB_QUERY, "unsigned parent zone denies"
				" trust anchor, indeterminate");
			vq->chase_reply->security = sec_status_indeterminate;
			update_reason_bogus(vq->chase_reply, LDNS_EDE_DNSSEC_INDETERMINATE);
			vq->state = VAL_FINISHED_STATE;
			return 1;
		}
		verbose(VERB_ALGO, "trust anchor NXDOMAIN by signed parent");
	} else if(subtype == VAL_CLASS_POSITIVE &&
		qstate->qinfo.qtype == LDNS_RR_TYPE_DNSKEY &&
		query_dname_compare(lookup_name, qstate->qinfo.qname) == 0) {
		/* is a DNSKEY so lookup a bit higher since we want to
		 * get it from a parent or from trustanchor */
		dname_remove_label(&lookup_name, &lookup_len);
	}

	if(vq->rrset_skip > 0 || subtype == VAL_CLASS_CNAME ||
		subtype == VAL_CLASS_REFERRAL) {
		/* extract this part of orig_msg into chase_reply for
		 * the eventual VALIDATE stage */
		val_fill_reply(vq->chase_reply, vq->orig_msg->rep, 
			vq->rrset_skip, lookup_name, lookup_len, 
			vq->signer_name);
		if(verbosity >= VERB_ALGO)
			log_dns_msg("chased extract", &vq->qchase, 
				vq->chase_reply);
	}

	vq->key_entry = key_cache_obtain(ve->kcache, lookup_name, lookup_len,
		vq->qchase.qclass, qstate->region, *qstate->env->now);

	/* there is no key and no trust anchor */
	if(vq->key_entry == NULL && anchor == NULL) {
		/*response isn't under a trust anchor, so we cannot validate.*/
		vq->chase_reply->security = sec_status_indeterminate;
		update_reason_bogus(vq->chase_reply, LDNS_EDE_DNSSEC_INDETERMINATE);
		/* go to finished state to cache this result */
		vq->state = VAL_FINISHED_STATE;
		return 1;
	}
	/* if not key, or if keyentry is *above* the trustanchor, i.e.
	 * the keyentry is based on another (higher) trustanchor */
	else if(vq->key_entry == NULL || (anchor &&
		dname_strict_subdomain_c(anchor->name, vq->key_entry->name))) {
		/* trust anchor is an 'unsigned' trust anchor */
		if(anchor && anchor->numDS == 0 && anchor->numDNSKEY == 0) {
			vq->chase_reply->security = sec_status_insecure;
			val_mark_insecure(vq->chase_reply, anchor->name, 
				qstate->env->rrset_cache, qstate->env);
			lock_basic_unlock(&anchor->lock);
			/* go to finished state to cache this result */
			vq->state = VAL_FINISHED_STATE;
			return 1;
		}
		/* fire off a trust anchor priming query. */
		verbose(VERB_DETAIL, "prime trust anchor");
		if(!prime_trust_anchor(qstate, vq, id, anchor)) {
			lock_basic_unlock(&anchor->lock);
			return val_error(qstate, id);
		}
		lock_basic_unlock(&anchor->lock);
		/* and otherwise, don't continue processing this event.
		 * (it will be reactivated when the priming query returns). */
		vq->state = VAL_FINDKEY_STATE;
		return 0;
	}
	if(anchor) {
		lock_basic_unlock(&anchor->lock);
	}

	if(key_entry_isnull(vq->key_entry)) {
		/* response is under a null key, so we cannot validate
		 * However, we do set the status to INSECURE, since it is 
		 * essentially proven insecure. */
		vq->chase_reply->security = sec_status_insecure;
		val_mark_insecure(vq->chase_reply, vq->key_entry->name, 
			qstate->env->rrset_cache, qstate->env);
		/* go to finished state to cache this result */
		vq->state = VAL_FINISHED_STATE;
		return 1;
	} else if(key_entry_isbad(vq->key_entry)) {
		/* Bad keys should have the relevant EDE code and text */
		sldns_ede_code ede = key_entry_get_reason_bogus(vq->key_entry);
		/* key is bad, chain is bad, reply is bogus */
		errinf_dname(qstate, "key for validation", vq->key_entry->name);
		errinf_ede(qstate, "is marked as invalid", ede);
		errinf(qstate, "because of a previous");
		errinf(qstate, key_entry_get_reason(vq->key_entry));

		/* no retries, stop bothering the authority until timeout */
		vq->restart_count = ve->max_restart;
		vq->chase_reply->security = sec_status_bogus;
		update_reason_bogus(vq->chase_reply, ede);
		vq->state = VAL_FINISHED_STATE;
		return 1;
	}

	/* otherwise, we have our "closest" cached key -- continue 
	 * processing in the next state. */
	vq->state = VAL_FINDKEY_STATE;
	return 1;
}

/**
 * Process the FINDKEY state. Generally this just calculates the next name
 * to query and either issues a DS or a DNSKEY query. It will check to see
 * if the correct key has already been reached, in which case it will
 * advance the event to the next state.
 *
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param id: module id.
 * @return true if the event should be processed further on return, false if
 *         not.
 */
static int
processFindKey(struct module_qstate* qstate, struct val_qstate* vq, int id)
{
	uint8_t* target_key_name, *current_key_name;
	size_t target_key_len;
	int strip_lab;
	struct module_qstate* newq = NULL;

	log_query_info(VERB_ALGO, "validator: FindKey", &vq->qchase);
	/* We know that state.key_entry is not 0 or bad key -- if it were,
	 * then previous processing should have directed this event to 
	 * a different state. 
	 * It could be an isnull key, which signals the DNSKEY failed
	 * with retry and has to be looked up again. */
	log_assert(vq->key_entry && !key_entry_isbad(vq->key_entry));
	if(key_entry_isnull(vq->key_entry)) {
		if(!generate_request(qstate, id, vq->ds_rrset->rk.dname, 
			vq->ds_rrset->rk.dname_len, LDNS_RR_TYPE_DNSKEY, 
			vq->qchase.qclass, BIT_CD, &newq, 0)) {
			verbose(VERB_ALGO, "error generating DNSKEY request");
			return val_error(qstate, id);
		}
		return 0;
	}

	target_key_name = vq->signer_name;
	target_key_len = vq->signer_len;
	if(!target_key_name) {
		target_key_name = vq->qchase.qname;
		target_key_len = vq->qchase.qname_len;
	}

	current_key_name = vq->key_entry->name;

	/* If our current key entry matches our target, then we are done. */
	if(query_dname_compare(target_key_name, current_key_name) == 0) {
		vq->state = VAL_VALIDATE_STATE;
		return 1;
	}

	if(vq->empty_DS_name) {
		/* if the last empty nonterminal/emptyDS name we detected is
		 * below the current key, use that name to make progress
		 * along the chain of trust */
		if(query_dname_compare(target_key_name, 
			vq->empty_DS_name) == 0) {
			/* do not query for empty_DS_name again */
			verbose(VERB_ALGO, "Cannot retrieve DS for signature");
			errinf_ede(qstate, "no signatures", LDNS_EDE_RRSIGS_MISSING);
			errinf_origin(qstate, qstate->reply_origin);
			vq->chase_reply->security = sec_status_bogus;
			update_reason_bogus(vq->chase_reply, LDNS_EDE_RRSIGS_MISSING);
			vq->state = VAL_FINISHED_STATE;
			return 1;
		}
		current_key_name = vq->empty_DS_name;
	}

	log_nametypeclass(VERB_ALGO, "current keyname", current_key_name,
		LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN);
	log_nametypeclass(VERB_ALGO, "target keyname", target_key_name,
		LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN);
	/* assert we are walking down the DNS tree */
	if(!dname_subdomain_c(target_key_name, current_key_name)) {
		verbose(VERB_ALGO, "bad signer name");
		vq->chase_reply->security = sec_status_bogus;
		vq->state = VAL_FINISHED_STATE;
		return 1;
	}
	/* so this value is >= -1 */
	strip_lab = dname_count_labels(target_key_name) - 
		dname_count_labels(current_key_name) - 1;
	log_assert(strip_lab >= -1);
	verbose(VERB_ALGO, "striplab %d", strip_lab);
	if(strip_lab > 0) {
		dname_remove_labels(&target_key_name, &target_key_len, 
			strip_lab);
	}
	log_nametypeclass(VERB_ALGO, "next keyname", target_key_name,
		LDNS_RR_TYPE_DNSKEY, LDNS_RR_CLASS_IN);

	/* The next step is either to query for the next DS, or to query 
	 * for the next DNSKEY. */
	if(vq->ds_rrset)
		log_nametypeclass(VERB_ALGO, "DS RRset", vq->ds_rrset->rk.dname, LDNS_RR_TYPE_DS, LDNS_RR_CLASS_IN);
	else verbose(VERB_ALGO, "No DS RRset");

	if(vq->ds_rrset && query_dname_compare(vq->ds_rrset->rk.dname,
		vq->key_entry->name) != 0) {
		if(!generate_request(qstate, id, vq->ds_rrset->rk.dname, 
			vq->ds_rrset->rk.dname_len, LDNS_RR_TYPE_DNSKEY, 
			vq->qchase.qclass, BIT_CD, &newq, 0)) {
			verbose(VERB_ALGO, "error generating DNSKEY request");
			return val_error(qstate, id);
		}
		return 0;
	}

	if(!vq->ds_rrset || query_dname_compare(vq->ds_rrset->rk.dname,
		target_key_name) != 0) {
		/* check if there is a cache entry : pick up an NSEC if
		 * there is no DS, check if that NSEC has DS-bit unset, and
		 * thus can disprove the secure delegation we seek.
		 * We can then use that NSEC even in the absence of a SOA
		 * record that would be required by the iterator to supply
		 * a completely protocol-correct response. 
		 * Uses negative cache for NSEC3 lookup of DS responses. */
		/* only if cache not blacklisted, of course */
		struct dns_msg* msg;
		int suspend;
		if(vq->sub_ds_msg) {
			/* We have a suspended DS reply from a sub-query;
			 * process it. */
			verbose(VERB_ALGO, "Process suspended sub DS response");
			msg = vq->sub_ds_msg;
			process_ds_response(qstate, vq, id, LDNS_RCODE_NOERROR,
				msg, &msg->qinfo, NULL, &suspend, NULL);
			if(suspend) {
				/* we'll come back here later to continue */
				if(!validate_suspend_setup_timer(qstate, vq,
					id, VAL_FINDKEY_STATE))
					return val_error(qstate, id);
				return 0;
			}
			vq->sub_ds_msg = NULL;
			return 1; /* continue processing ds-response results */
		} else if(!qstate->blacklist && !vq->chain_blacklist &&
			(msg=val_find_DS(qstate->env, target_key_name, 
			target_key_len, vq->qchase.qclass, qstate->region,
			vq->key_entry->name)) ) {
			verbose(VERB_ALGO, "Process cached DS response");
			process_ds_response(qstate, vq, id, LDNS_RCODE_NOERROR,
				msg, &msg->qinfo, NULL, &suspend, NULL);
			if(suspend) {
				/* we'll come back here later to continue */
				if(!validate_suspend_setup_timer(qstate, vq,
					id, VAL_FINDKEY_STATE))
					return val_error(qstate, id);
				return 0;
			}
			return 1; /* continue processing ds-response results */
		}
		if(!generate_request(qstate, id, target_key_name, 
			target_key_len, LDNS_RR_TYPE_DS, vq->qchase.qclass,
			BIT_CD, &newq, 0)) {
			verbose(VERB_ALGO, "error generating DS request");
			return val_error(qstate, id);
		}
		return 0;
	}

	/* Otherwise, it is time to query for the DNSKEY */
	if(!generate_request(qstate, id, vq->ds_rrset->rk.dname, 
		vq->ds_rrset->rk.dname_len, LDNS_RR_TYPE_DNSKEY, 
		vq->qchase.qclass, BIT_CD, &newq, 0)) {
		verbose(VERB_ALGO, "error generating DNSKEY request");
		return val_error(qstate, id);
	}

	return 0;
}

/**
 * Process the VALIDATE stage, the init and findkey stages are finished,
 * and the right keys are available to validate the response.
 * Or, there are no keys available, in order to invalidate the response.
 *
 * After validation, the status is recorded in the message and rrsets,
 * and finished state is started.
 *
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param ve: validator shared global environment.
 * @param id: module id.
 * @return true if the event should be processed further on return, false if
 *         not.
 */
static int
processValidate(struct module_qstate* qstate, struct val_qstate* vq, 
	struct val_env* ve, int id)
{
	enum val_classification subtype;
	int rcode, suspend, nsec3_calculations = 0;

	if(!vq->key_entry) {
		verbose(VERB_ALGO, "validate: no key entry, failed");
		return val_error(qstate, id);
	}

	/* This is the default next state. */
	vq->state = VAL_FINISHED_STATE;

	/* Unsigned responses must be underneath a "null" key entry.*/
	if(key_entry_isnull(vq->key_entry)) {
		verbose(VERB_DETAIL, "Verified that %sresponse is INSECURE",
			vq->signer_name?"":"unsigned ");
		vq->chase_reply->security = sec_status_insecure;
		val_mark_insecure(vq->chase_reply, vq->key_entry->name, 
			qstate->env->rrset_cache, qstate->env);
		key_cache_insert(ve->kcache, vq->key_entry,
			qstate->env->cfg->val_log_level >= 2);
		return 1;
	}

	if(key_entry_isbad(vq->key_entry)) {
		log_nametypeclass(VERB_DETAIL, "Could not establish a chain "
			"of trust to keys for", vq->key_entry->name,
			LDNS_RR_TYPE_DNSKEY, vq->key_entry->key_class);
		vq->chase_reply->security = sec_status_bogus;
		update_reason_bogus(vq->chase_reply,
			key_entry_get_reason_bogus(vq->key_entry));
		errinf_ede(qstate, "while building chain of trust",
			key_entry_get_reason_bogus(vq->key_entry));
		if(vq->restart_count >= ve->max_restart)
			key_cache_insert(ve->kcache, vq->key_entry,
				qstate->env->cfg->val_log_level >= 2);
		return 1;
	}

	/* signerName being null is the indicator that this response was 
	 * unsigned */
	if(vq->signer_name == NULL) {
		log_query_info(VERB_ALGO, "processValidate: state has no "
			"signer name", &vq->qchase);
		verbose(VERB_DETAIL, "Could not establish validation of "
		          "INSECURE status of unsigned response.");
		errinf_ede(qstate, "no signatures", LDNS_EDE_RRSIGS_MISSING);
		errinf_origin(qstate, qstate->reply_origin);
		vq->chase_reply->security = sec_status_bogus;
		update_reason_bogus(vq->chase_reply, LDNS_EDE_RRSIGS_MISSING);
		return 1;
	}
	subtype = val_classify_response(qstate->query_flags, &qstate->qinfo,
		&vq->qchase, vq->orig_msg->rep, vq->rrset_skip);
	if(subtype != VAL_CLASS_REFERRAL)
		remove_spurious_authority(vq->chase_reply, vq->orig_msg->rep);

	/* check signatures in the message; 
	 * answer and authority must be valid, additional is only checked. */
	if(!validate_msg_signatures(qstate, vq, qstate->env, ve,
		vq->chase_reply, vq->key_entry, &suspend)) {
		if(suspend) {
			if(!validate_suspend_setup_timer(qstate, vq,
				id, VAL_VALIDATE_STATE))
				return val_error(qstate, id);
			return 0;
		}
		/* workaround bad recursor out there that truncates (even
		 * with EDNS4k) to 512 by removing RRSIG from auth section
		 * for positive replies*/
		if((subtype == VAL_CLASS_POSITIVE || subtype == VAL_CLASS_ANY
			|| subtype == VAL_CLASS_CNAME) &&
			detect_wrongly_truncated(vq->orig_msg->rep)) {
			/* truncate the message some more */
			vq->orig_msg->rep->ns_numrrsets = 0;
			vq->orig_msg->rep->ar_numrrsets = 0;
			vq->orig_msg->rep->rrset_count = 
				vq->orig_msg->rep->an_numrrsets;
			vq->chase_reply->ns_numrrsets = 0;
			vq->chase_reply->ar_numrrsets = 0;
			vq->chase_reply->rrset_count = 
				vq->chase_reply->an_numrrsets;
			qstate->errinf = NULL;
		}
		else {
			verbose(VERB_DETAIL, "Validate: message contains "
				"bad rrsets");
			return 1;
		}
	}

	switch(subtype) {
		case VAL_CLASS_POSITIVE:
			verbose(VERB_ALGO, "Validating a positive response");
			validate_positive_response(qstate->env, ve,
				&vq->qchase, vq->chase_reply, vq->key_entry,
				qstate, vq, &nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(positive): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		case VAL_CLASS_NODATA:
			verbose(VERB_ALGO, "Validating a nodata response");
			validate_nodata_response(qstate->env, ve,
				&vq->qchase, vq->chase_reply, vq->key_entry,
				qstate, vq, &nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(nodata): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		case VAL_CLASS_NAMEERROR:
			rcode = (int)FLAGS_GET_RCODE(vq->orig_msg->rep->flags);
			verbose(VERB_ALGO, "Validating a nxdomain response");
			validate_nameerror_response(qstate->env, ve, 
				&vq->qchase, vq->chase_reply, vq->key_entry, &rcode,
				qstate, vq, &nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(nxdomain): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			FLAGS_SET_RCODE(vq->orig_msg->rep->flags, rcode);
			FLAGS_SET_RCODE(vq->chase_reply->flags, rcode);
			break;

		case VAL_CLASS_CNAME:
			verbose(VERB_ALGO, "Validating a cname response");
			validate_cname_response(qstate->env, ve,
				&vq->qchase, vq->chase_reply, vq->key_entry,
				qstate, vq, &nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(cname): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		case VAL_CLASS_CNAMENOANSWER:
			verbose(VERB_ALGO, "Validating a cname noanswer "
				"response");
			validate_cname_noanswer_response(qstate->env, ve,
				&vq->qchase, vq->chase_reply, vq->key_entry,
				qstate, vq, &nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(cname_noanswer): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		case VAL_CLASS_REFERRAL:
			verbose(VERB_ALGO, "Validating a referral response");
			validate_referral_response(vq->chase_reply);
			verbose(VERB_DETAIL, "validate(referral): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		case VAL_CLASS_ANY:
			verbose(VERB_ALGO, "Validating a positive ANY "
				"response");
			validate_any_response(qstate->env, ve, &vq->qchase,
				vq->chase_reply, vq->key_entry, qstate, vq,
				&nsec3_calculations, &suspend);
			if(suspend) {
				if(!validate_suspend_setup_timer(qstate,
					vq, id, VAL_VALIDATE_STATE))
					return val_error(qstate, id);
				return 0;
			}
			verbose(VERB_DETAIL, "validate(positive_any): %s",
			  	sec_status_to_string(
				vq->chase_reply->security));
			break;

		default:
			log_err("validate: unhandled response subtype: %d",
				subtype);
	}
	if(vq->chase_reply->security == sec_status_bogus) {
		if(subtype == VAL_CLASS_POSITIVE)
			errinf(qstate, "wildcard");
		else errinf(qstate, val_classification_to_string(subtype));
		errinf(qstate, "proof failed");
		errinf_origin(qstate, qstate->reply_origin);
	}

	return 1;
}

/**
 * The Finished state. The validation status (good or bad) has been determined.
 *
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param ve: validator shared global environment.
 * @param id: module id.
 * @return true if the event should be processed further on return, false if
 *         not.
 */
static int
processFinished(struct module_qstate* qstate, struct val_qstate* vq, 
	struct val_env* ve, int id)
{
	enum val_classification subtype = val_classify_response(
		qstate->query_flags, &qstate->qinfo, &vq->qchase, 
		vq->orig_msg->rep, vq->rrset_skip);

	/* store overall validation result in orig_msg */
	if(vq->rrset_skip == 0) {
		vq->orig_msg->rep->security = vq->chase_reply->security;
		update_reason_bogus(vq->orig_msg->rep, vq->chase_reply->reason_bogus);
	} else if(subtype != VAL_CLASS_REFERRAL ||
		vq->rrset_skip < vq->orig_msg->rep->an_numrrsets + 
		vq->orig_msg->rep->ns_numrrsets) {
		/* ignore sec status of additional section if a referral 
		 * type message skips there and
		 * use the lowest security status as end result. */
		if(vq->chase_reply->security < vq->orig_msg->rep->security) {
			vq->orig_msg->rep->security = 
				vq->chase_reply->security;
			update_reason_bogus(vq->orig_msg->rep, vq->chase_reply->reason_bogus);
		}
	}

	if(subtype == VAL_CLASS_REFERRAL) {
		/* for a referral, move to next unchecked rrset and check it*/
		vq->rrset_skip = val_next_unchecked(vq->orig_msg->rep, 
			vq->rrset_skip);
		if(vq->rrset_skip < vq->orig_msg->rep->rrset_count) {
			/* and restart for this rrset */
			verbose(VERB_ALGO, "validator: go to next rrset");
			vq->chase_reply->security = sec_status_unchecked;
			vq->state = VAL_INIT_STATE;
			return 1;
		}
		/* referral chase is done */
	}
	if(vq->chase_reply->security != sec_status_bogus &&
		subtype == VAL_CLASS_CNAME) {
		/* chase the CNAME; process next part of the message */
		if(!val_chase_cname(&vq->qchase, vq->orig_msg->rep, 
			&vq->rrset_skip)) {
			verbose(VERB_ALGO, "validator: failed to chase CNAME");
			vq->orig_msg->rep->security = sec_status_bogus;
			update_reason_bogus(vq->orig_msg->rep, LDNS_EDE_DNSSEC_BOGUS);
		} else {
			/* restart process for new qchase at rrset_skip */
			log_query_info(VERB_ALGO, "validator: chased to",
				&vq->qchase);
			vq->chase_reply->security = sec_status_unchecked;
			vq->state = VAL_INIT_STATE;
			return 1;
		}
	}

	if(vq->orig_msg->rep->security == sec_status_secure) {
		/* If the message is secure, check that all rrsets are
		 * secure (i.e. some inserted RRset for CNAME chain with
		 * a different signer name). And drop additional rrsets
		 * that are not secure (if clean-additional option is set) */
		/* this may cause the msg to be marked bogus */
		val_check_nonsecure(qstate->env, vq->orig_msg->rep);
		if(vq->orig_msg->rep->security == sec_status_secure) {
			log_query_info(VERB_DETAIL, "validation success", 
				&qstate->qinfo);
			if(!qstate->no_cache_store) {
				val_neg_addreply(qstate->env->neg_cache,
					vq->orig_msg->rep);
			}
		}
	}

	/* if the result is bogus - set message ttl to bogus ttl to avoid
	 * endless bogus revalidation */
	if(vq->orig_msg->rep->security == sec_status_bogus) {
		struct msgreply_entry* e;

		/* see if we can try again to fetch data */
		if(vq->restart_count < ve->max_restart) {
			verbose(VERB_ALGO, "validation failed, "
				"blacklist and retry to fetch data");
			val_blacklist(&qstate->blacklist, qstate->region, 
				qstate->reply_origin, 0);
			qstate->reply_origin = NULL;
			qstate->errinf = NULL;
			val_restart(vq);
			verbose(VERB_ALGO, "pass back to next module");
			qstate->ext_state[id] = module_restart_next;
			return 0;
		}

		if(qstate->env->cfg->serve_expired &&
			(e=msg_cache_lookup(qstate->env, qstate->qinfo.qname,
			qstate->qinfo.qname_len, qstate->qinfo.qtype,
			qstate->qinfo.qclass, qstate->query_flags,
			0 /*now; allow expired*/,
			1 /*wr; we may update the data*/))) {
			struct reply_info* rep = (struct reply_info*)e->entry.data;
			if(rep && rep->security > sec_status_bogus &&
				(!qstate->env->cfg->serve_expired_ttl ||
				 qstate->env->cfg->serve_expired_ttl_reset ||
				*qstate->env->now <= rep->serve_expired_ttl)) {
				verbose(VERB_ALGO, "validation failed but "
					"previously cached valid response "
					"exists; set serve-expired-norec-ttl "
					"for response in cache");
				rep->serve_expired_norec_ttl = NORR_TTL +
					*qstate->env->now;
				if(qstate->env->cfg->serve_expired_ttl_reset &&
					*qstate->env->now + qstate->env->cfg->serve_expired_ttl
					> rep->serve_expired_ttl) {
					verbose(VERB_ALGO, "reset serve-expired-ttl for "
						"valid response in cache");
					rep->serve_expired_ttl = *qstate->env->now +
						qstate->env->cfg->serve_expired_ttl;
				}
				/* Return an error response.
				 * If serve-expired-client-timeout is enabled,
				 * the client-timeout logic will try to find an
				 * (expired) answer in the cache as last
				 * resort. If it is not enabled, expired
				 * answers are already used before the mesh
				 * activation. */
				qstate->return_rcode = LDNS_RCODE_SERVFAIL;
				qstate->return_msg = NULL;
				qstate->ext_state[id] = module_finished;
				lock_rw_unlock(&e->entry.lock);
				return 0;
			}
			lock_rw_unlock(&e->entry.lock);
		}

		vq->orig_msg->rep->ttl = ve->bogus_ttl;
		vq->orig_msg->rep->prefetch_ttl = 
			PREFETCH_TTL_CALC(vq->orig_msg->rep->ttl);
		vq->orig_msg->rep->serve_expired_ttl =
			vq->orig_msg->rep->ttl + qstate->env->cfg->serve_expired_ttl;
		if((qstate->env->cfg->val_log_level >= 1 ||
			qstate->env->cfg->log_servfail) &&
			!qstate->env->cfg->val_log_squelch) {
			if(qstate->env->cfg->val_log_level < 2 &&
				!qstate->env->cfg->log_servfail)
				log_query_info(NO_VERBOSE, "validation failure",
					&qstate->qinfo);
			else {
				char* err_str = errinf_to_str_bogus(qstate,
					qstate->region);
				if(err_str) {
					log_info("%s", err_str);
					vq->orig_msg->rep->reason_bogus_str = err_str;
				}
			}
		}
		/*
		 * If set, the validator will not make messages bogus, instead
		 * indeterminate is issued, so that no clients receive SERVFAIL.
		 * This allows an operator to run validation 'shadow' without
		 * hurting responses to clients.
		 */
		/* If we are in permissive mode, bogus gets indeterminate */
		if(qstate->env->cfg->val_permissive_mode)
			vq->orig_msg->rep->security = sec_status_indeterminate;
	}

	if(vq->orig_msg->rep->security == sec_status_secure &&
		qstate->env->cfg->root_key_sentinel &&
		(qstate->qinfo.qtype == LDNS_RR_TYPE_A ||
		qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA)) {
		char* keytag_start;
		uint16_t keytag;
		if(*qstate->qinfo.qname == strlen(SENTINEL_IS) +
			SENTINEL_KEYTAG_LEN &&
			dname_lab_startswith(qstate->qinfo.qname, SENTINEL_IS,
			&keytag_start)) {
			if(sentinel_get_keytag(keytag_start, &keytag) &&
				!anchor_has_keytag(qstate->env->anchors,
				(uint8_t*)"", 1, 0, vq->qchase.qclass, keytag)) {
				vq->orig_msg->rep->security =
					sec_status_secure_sentinel_fail;
			}
		} else if(*qstate->qinfo.qname == strlen(SENTINEL_NOT) +
			SENTINEL_KEYTAG_LEN &&
			dname_lab_startswith(qstate->qinfo.qname, SENTINEL_NOT,
			&keytag_start)) {
			if(sentinel_get_keytag(keytag_start, &keytag) &&
				anchor_has_keytag(qstate->env->anchors,
				(uint8_t*)"", 1, 0, vq->qchase.qclass, keytag)) {
				vq->orig_msg->rep->security =
					sec_status_secure_sentinel_fail;
			}
		}
	}

	/* Update rep->reason_bogus as it is the one being cached */
	update_reason_bogus(vq->orig_msg->rep, errinf_to_reason_bogus(qstate));
	/* store results in cache */
	if(qstate->query_flags&BIT_RD) {
		/* if secure, this will override cache anyway, no need
		 * to check if from parentNS */
		if(!qstate->no_cache_store) {
			if(!dns_cache_store(qstate->env, &vq->orig_msg->qinfo,
				vq->orig_msg->rep, 0, qstate->prefetch_leeway,
				0, qstate->region, qstate->query_flags,
				qstate->qstarttime, qstate->is_valrec)) {
				log_err("out of memory caching validator results");
			}
		}
	} else {
		/* for a referral, store the verified RRsets */
		/* and this does not get prefetched, so no leeway */
		if(!dns_cache_store(qstate->env, &vq->orig_msg->qinfo,
			vq->orig_msg->rep, 1, 0, 0, qstate->region,
			qstate->query_flags, qstate->qstarttime,
			qstate->is_valrec)) {
			log_err("out of memory caching validator results");
		}
	}
	qstate->return_rcode = LDNS_RCODE_NOERROR;
	qstate->return_msg = vq->orig_msg;
	qstate->ext_state[id] = module_finished;
	return 0;
}

/** 
 * Handle validator state.
 * If a method returns true, the next state is started. If false, then
 * processing will stop.
 * @param qstate: query state.
 * @param vq: validator query state.
 * @param ve: validator shared global environment.
 * @param id: module id.
 */
static void
val_handle(struct module_qstate* qstate, struct val_qstate* vq, 
	struct val_env* ve, int id)
{
	int cont = 1;
	while(cont) {
		verbose(VERB_ALGO, "val handle processing q with state %s",
			val_state_to_string(vq->state));
		switch(vq->state) {
			case VAL_INIT_STATE:
				cont = processInit(qstate, vq, ve, id);
				break;
			case VAL_FINDKEY_STATE: 
				cont = processFindKey(qstate, vq, id);
				break;
			case VAL_VALIDATE_STATE: 
				cont = processValidate(qstate, vq, ve, id);
				break;
			case VAL_FINISHED_STATE: 
				cont = processFinished(qstate, vq, ve, id);
				break;
			default:
				log_warn("validator: invalid state %d",
					vq->state);
				cont = 0;
				break;
		}
	}
}

void
val_operate(struct module_qstate* qstate, enum module_ev event, int id,
        struct outbound_entry* outbound)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	struct val_qstate* vq = (struct val_qstate*)qstate->minfo[id];
	verbose(VERB_QUERY, "validator[module %d] operate: extstate:%s "
		"event:%s", id, strextstate(qstate->ext_state[id]), 
		strmodulevent(event));
	log_query_info(VERB_QUERY, "validator operate: query",
		&qstate->qinfo);
	if(vq && qstate->qinfo.qname != vq->qchase.qname) 
		log_query_info(VERB_QUERY, "validator operate: chased to",
		&vq->qchase);
	(void)outbound;
	if(event == module_event_new || 
		(event == module_event_pass && vq == NULL)) {

		/* pass request to next module, to get it */
		verbose(VERB_ALGO, "validator: pass to next module");
		qstate->ext_state[id] = module_wait_module;
		return;
	}
	if(event == module_event_moddone) {
		/* check if validation is needed */
		verbose(VERB_ALGO, "validator: nextmodule returned");

		if(!needs_validation(qstate, qstate->return_rcode, 
			qstate->return_msg)) {
			/* no need to validate this */
			if(qstate->return_msg)
				qstate->return_msg->rep->security =
					sec_status_indeterminate;
			qstate->ext_state[id] = module_finished;
			return;
		}
		if(already_validated(qstate->return_msg)) {
			qstate->ext_state[id] = module_finished;
			return;
		}
		if(qstate->rpz_applied) {
			verbose(VERB_ALGO, "rpz applied, mark it as insecure");
			if(qstate->return_msg)
				qstate->return_msg->rep->security =
					sec_status_insecure;
			qstate->ext_state[id] = module_finished;
			return;
		}
		/* qclass ANY should have validation result from spawned 
		 * queries. If we get here, it is bogus or an internal error */
		if(qstate->qinfo.qclass == LDNS_RR_CLASS_ANY) {
			verbose(VERB_ALGO, "cannot validate classANY: bogus");
			if(qstate->return_msg) {
				qstate->return_msg->rep->security =
					sec_status_bogus;
				update_reason_bogus(qstate->return_msg->rep, LDNS_EDE_DNSSEC_BOGUS);
			}
			qstate->ext_state[id] = module_finished;
			return;
		}
		/* create state to start validation */
		qstate->ext_state[id] = module_error; /* override this */
		if(!vq) {
			vq = val_new(qstate, id);
			if(!vq) {
				log_err("validator: malloc failure");
				qstate->ext_state[id] = module_error;
				return;
			}
		} else if(!vq->orig_msg) {
			if(!val_new_getmsg(qstate, vq)) {
				log_err("validator: malloc failure");
				qstate->ext_state[id] = module_error;
				return;
			}
		}
		val_handle(qstate, vq, ve, id);
		return;
	}
	if(event == module_event_pass) {
		qstate->ext_state[id] = module_error; /* override this */
		/* continue processing, since val_env exists */
		val_handle(qstate, vq, ve, id);
		return;
	}
	log_err("validator: bad event %s", strmodulevent(event));
	qstate->ext_state[id] = module_error;
	return;
}

/**
 * Evaluate the response to a priming request.
 *
 * @param dnskey_rrset: DNSKEY rrset (can be NULL if none) in prime reply.
 * 	(this rrset is allocated in the wrong region, not the qstate).
 * @param ta: trust anchor.
 * @param qstate: qstate that needs key.
 * @param id: module id.
 * @param sub_qstate: the sub query state, that is the lookup that fetched
 *	the trust anchor data, it contains error information for the answer.
 * @return new key entry or NULL on allocation failure.
 *	The key entry will either contain a validated DNSKEY rrset, or
 *	represent a Null key (query failed, but validation did not), or a
 *	Bad key (validation failed).
 */
static struct key_entry_key*
primeResponseToKE(struct ub_packed_rrset_key* dnskey_rrset, 
	struct trust_anchor* ta, struct module_qstate* qstate, int id,
	struct module_qstate* sub_qstate)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	struct key_entry_key* kkey = NULL;
	enum sec_status sec = sec_status_unchecked;
	char reasonbuf[256];
	char* reason = NULL;
	sldns_ede_code reason_bogus = LDNS_EDE_DNSSEC_BOGUS;
	int downprot = qstate->env->cfg->harden_algo_downgrade;

	if(!dnskey_rrset) {
		char* err = errinf_to_str_misc(sub_qstate);
		char rstr[1024];
		log_nametypeclass(VERB_OPS, "failed to prime trust anchor -- "
			"could not fetch DNSKEY rrset", 
			ta->name, LDNS_RR_TYPE_DNSKEY, ta->dclass);
		reason_bogus = LDNS_EDE_DNSKEY_MISSING;
		if(!err) {
			snprintf(rstr, sizeof(rstr), "no DNSKEY rrset");
		} else {
			snprintf(rstr, sizeof(rstr), "no DNSKEY rrset "
				"[%s]", err);
		}
		if(qstate->env->cfg->harden_dnssec_stripped) {
			errinf_ede(qstate, rstr, reason_bogus);
			kkey = key_entry_create_bad(qstate->region, ta->name,
				ta->namelen, ta->dclass, BOGUS_KEY_TTL,
				reason_bogus, rstr, *qstate->env->now);
		} else 	kkey = key_entry_create_null(qstate->region, ta->name,
				ta->namelen, ta->dclass, NULL_KEY_TTL,
				reason_bogus, rstr, *qstate->env->now);
		if(!kkey) {
			log_err("out of memory: allocate fail prime key");
			return NULL;
		}
		return kkey;
	}
	/* attempt to verify with trust anchor DS and DNSKEY */
	kkey = val_verify_new_DNSKEYs_with_ta(qstate->region, qstate->env, ve, 
		dnskey_rrset, ta->ds_rrset, ta->dnskey_rrset, downprot,
		&reason, &reason_bogus, qstate, reasonbuf, sizeof(reasonbuf));
	if(!kkey) {
		log_err("out of memory: verifying prime TA");
		return NULL;
	}
	if(key_entry_isgood(kkey))
		sec = sec_status_secure;
	else
		sec = sec_status_bogus;
	verbose(VERB_DETAIL, "validate keys with anchor(DS): %s", 
		sec_status_to_string(sec));

	if(sec != sec_status_secure) {
		log_nametypeclass(VERB_OPS, "failed to prime trust anchor -- "
			"DNSKEY rrset is not secure", 
			ta->name, LDNS_RR_TYPE_DNSKEY, ta->dclass);
		/* NOTE: in this case, we should probably reject the trust 
		 * anchor for longer, perhaps forever. */
		if(qstate->env->cfg->harden_dnssec_stripped) {
			errinf_ede(qstate, reason, reason_bogus);
			kkey = key_entry_create_bad(qstate->region, ta->name,
				ta->namelen, ta->dclass, BOGUS_KEY_TTL,
				reason_bogus, reason,
				*qstate->env->now);
		} else 	kkey = key_entry_create_null(qstate->region, ta->name,
				ta->namelen, ta->dclass, NULL_KEY_TTL,
				reason_bogus, reason,
				*qstate->env->now);
		if(!kkey) {
			log_err("out of memory: allocate null prime key");
			return NULL;
		}
		return kkey;
	}

	log_nametypeclass(VERB_DETAIL, "Successfully primed trust anchor", 
		ta->name, LDNS_RR_TYPE_DNSKEY, ta->dclass);
	return kkey;
}

/**
 * In inform supers, with the resulting message and rcode and the current
 * keyset in the super state, validate the DS response, returning a KeyEntry.
 *
 * @param qstate: query state that is validating and asked for a DS.
 * @param vq: validator query state
 * @param id: module id.
 * @param rcode: rcode result value.
 * @param msg: result message (if rcode is OK).
 * @param qinfo: from the sub query state, query info.
 * @param ke: the key entry to return. It returns
 *	is_bad if the DS response fails to validate, is_null if the
 *	DS response indicated an end to secure space, is_good if the DS
 *	validated. It returns ke=NULL if the DS response indicated that the
 *	request wasn't a delegation point.
 * @param sub_qstate: the sub query state, that is the lookup that fetched
 *	the trust anchor data, it contains error information for the answer.
 *	Can be NULL.
 * @return
 *	0 on success,
 *	1 on servfail error (malloc failure),
 *	2 on NSEC3 suspend.
 */
static int
ds_response_to_ke(struct module_qstate* qstate, struct val_qstate* vq,
        int id, int rcode, struct dns_msg* msg, struct query_info* qinfo,
	struct key_entry_key** ke, struct module_qstate* sub_qstate)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	char reasonbuf[256];
	char* reason = NULL;
	sldns_ede_code reason_bogus = LDNS_EDE_DNSSEC_BOGUS;
	enum val_classification subtype;
	int verified;
	if(rcode != LDNS_RCODE_NOERROR) {
		char rc[16];
		rc[0]=0;
		(void)sldns_wire2str_rcode_buf(rcode, rc, sizeof(rc));
		/* errors here pretty much break validation */
		verbose(VERB_DETAIL, "DS response was error, thus bogus");
		errinf(qstate, rc);
		reason = "no DS";
		if(sub_qstate) {
			char* err = errinf_to_str_misc(sub_qstate);
			if(err) {
				char buf[1024];
				snprintf(buf, sizeof(buf), "[%s]", err);
				errinf(qstate, buf);
			}
		}
		reason_bogus = LDNS_EDE_NETWORK_ERROR;
		errinf_ede(qstate, reason, reason_bogus);
		goto return_bogus;
	}

	subtype = val_classify_response(BIT_RD, qinfo, qinfo, msg->rep, 0);
	if(subtype == VAL_CLASS_POSITIVE) {
		struct ub_packed_rrset_key* ds;
		enum sec_status sec;
		ds = reply_find_answer_rrset(qinfo, msg->rep);
		/* If there was no DS rrset, then we have mis-classified 
		 * this message. */
		if(!ds) {
			log_warn("internal error: POSITIVE DS response was "
				"missing DS.");
			reason = "no DS record";
			errinf_ede(qstate, reason, reason_bogus);
			goto return_bogus;
		}
		/* Verify only returns BOGUS or SECURE. If the rrset is 
		 * bogus, then we are done. */
		sec = val_verify_rrset_entry(qstate->env, ve, ds,
			vq->key_entry, &reason, &reason_bogus,
			LDNS_SECTION_ANSWER, qstate, &verified, reasonbuf,
			sizeof(reasonbuf));
		if(sec != sec_status_secure) {
			verbose(VERB_DETAIL, "DS rrset in DS response did "
				"not verify");
			errinf_ede(qstate, reason, reason_bogus);
			goto return_bogus;
		}

		/* If the DS rrset validates, we still have to make sure 
		 * that they are usable. */
		if(!val_dsset_isusable(ds)) {
			/* If they aren't usable, then we treat it like 
			 * there was no DS. */
			*ke = key_entry_create_null(qstate->region,
				qinfo->qname, qinfo->qname_len, qinfo->qclass,
				ub_packed_rrset_ttl(ds),
				LDNS_EDE_UNSUPPORTED_DS_DIGEST, NULL,
				*qstate->env->now);
			return (*ke) == NULL;
		}

		/* Otherwise, we return the positive response. */
		log_query_info(VERB_DETAIL, "validated DS", qinfo);
		*ke = key_entry_create_rrset(qstate->region,
			qinfo->qname, qinfo->qname_len, qinfo->qclass, ds,
			NULL, LDNS_EDE_NONE, NULL, *qstate->env->now);
		return (*ke) == NULL;
	} else if(subtype == VAL_CLASS_NODATA || 
		subtype == VAL_CLASS_NAMEERROR) {
		/* NODATA means that the qname exists, but that there was 
		 * no DS.  This is a pretty normal case. */
		time_t proof_ttl = 0;
		enum sec_status sec;

		/* make sure there are NSECs or NSEC3s with signatures */
		if(!val_has_signed_nsecs(msg->rep, &reason)) {
			verbose(VERB_ALGO, "no NSECs: %s", reason);
			reason_bogus = LDNS_EDE_NSEC_MISSING;
			errinf_ede(qstate, reason, reason_bogus);
			goto return_bogus;
		}

		/* For subtype Name Error.
		 * attempt ANS 2.8.1.0 compatibility where it sets rcode
		 * to nxdomain, but really this is an Nodata/Noerror response.
		 * Find and prove the empty nonterminal in that case */

		/* Try to prove absence of the DS with NSEC */
		sec = val_nsec_prove_nodata_dsreply(
			qstate->env, ve, qinfo, msg->rep, vq->key_entry, 
			&proof_ttl, &reason, &reason_bogus, qstate,
			reasonbuf, sizeof(reasonbuf));
		switch(sec) {
			case sec_status_secure:
				verbose(VERB_DETAIL, "NSEC RRset for the "
					"referral proved no DS.");
				*ke = key_entry_create_null(qstate->region, 
					qinfo->qname, qinfo->qname_len, 
					qinfo->qclass, proof_ttl,
					LDNS_EDE_NONE, NULL,
					*qstate->env->now);
				return (*ke) == NULL;
			case sec_status_insecure:
				verbose(VERB_DETAIL, "NSEC RRset for the "
				  "referral proved not a delegation point");
				*ke = NULL;
				return 0;
			case sec_status_bogus:
				verbose(VERB_DETAIL, "NSEC RRset for the "
					"referral did not prove no DS.");
				errinf(qstate, reason);
				goto return_bogus;
			case sec_status_unchecked:
			default:
				/* NSEC proof did not work, try next */
				break;
		}

		if(!nsec3_cache_table_init(&vq->nsec3_cache_table, qstate->region)) {
			log_err("malloc failure in ds_response_to_ke for "
				"NSEC3 cache");
			reason = "malloc failure";
			errinf_ede(qstate, reason, 0);
			goto return_bogus;
		}
		sec = nsec3_prove_nods(qstate->env, ve, 
			msg->rep->rrsets + msg->rep->an_numrrsets,
			msg->rep->ns_numrrsets, qinfo, vq->key_entry, &reason,
			&reason_bogus, qstate, &vq->nsec3_cache_table,
			reasonbuf, sizeof(reasonbuf));
		switch(sec) {
			case sec_status_insecure:
				/* case insecure also continues to unsigned
				 * space.  If nsec3-iter-count too high or
				 * optout, then treat below as unsigned */
			case sec_status_secure:
				verbose(VERB_DETAIL, "NSEC3s for the "
					"referral proved no DS.");
				*ke = key_entry_create_null(qstate->region, 
					qinfo->qname, qinfo->qname_len, 
					qinfo->qclass, proof_ttl,
					LDNS_EDE_NONE, NULL,
					*qstate->env->now);
				return (*ke) == NULL;
			case sec_status_indeterminate:
				verbose(VERB_DETAIL, "NSEC3s for the "
				  "referral proved no delegation");
				*ke = NULL;
				return 0;
			case sec_status_bogus:
				verbose(VERB_DETAIL, "NSEC3s for the "
					"referral did not prove no DS.");
				errinf_ede(qstate, reason, reason_bogus);
				goto return_bogus;
			case sec_status_unchecked:
				return 2;
			default:
				/* NSEC3 proof did not work */
				break;
		}

		/* Apparently, no available NSEC/NSEC3 proved NODATA, so 
		 * this is BOGUS. */
		verbose(VERB_DETAIL, "DS %s ran out of options, so return "
			"bogus", val_classification_to_string(subtype));
		reason = "no DS but also no proof of that";
		errinf_ede(qstate, reason, reason_bogus);
		goto return_bogus;
	} else if(subtype == VAL_CLASS_CNAME || 
		subtype == VAL_CLASS_CNAMENOANSWER) {
		/* if the CNAME matches the exact name we want and is signed
		 * properly, then also, we are sure that no DS exists there,
		 * much like a NODATA proof */
		enum sec_status sec;
		struct ub_packed_rrset_key* cname;
		cname = reply_find_rrset_section_an(msg->rep, qinfo->qname,
			qinfo->qname_len, LDNS_RR_TYPE_CNAME, qinfo->qclass);
		if(!cname) {
			reason = "validator classified CNAME but no "
				"CNAME of the queried name for DS";
			errinf_ede(qstate, reason, reason_bogus);
			goto return_bogus;
		}
		if(((struct packed_rrset_data*)cname->entry.data)->rrsig_count
			== 0) {
		        if(msg->rep->an_numrrsets != 0 && ntohs(msg->rep->
				rrsets[0]->rk.type)==LDNS_RR_TYPE_DNAME) {
				reason = "DS got DNAME answer";
			} else {
				reason = "DS got unsigned CNAME answer";
			}
			errinf_ede(qstate, reason, reason_bogus);
			goto return_bogus;
		}
		sec = val_verify_rrset_entry(qstate->env, ve, cname,
			vq->key_entry, &reason, &reason_bogus,
			LDNS_SECTION_ANSWER, qstate, &verified, reasonbuf,
			sizeof(reasonbuf));
		if(sec == sec_status_secure) {
			verbose(VERB_ALGO, "CNAME validated, "
				"proof that DS does not exist");
			/* and that it is not a referral point */
			*ke = NULL;
			return 0;
		}
		errinf(qstate, "CNAME in DS response was not secure.");
		errinf_ede(qstate, reason, reason_bogus);
		goto return_bogus;
	} else {
		verbose(VERB_QUERY, "Encountered an unhandled type of "
			"DS response, thus bogus.");
		errinf(qstate, "no DS and");
		reason = "no DS";
		if(FLAGS_GET_RCODE(msg->rep->flags) != LDNS_RCODE_NOERROR) {
			char rc[16];
			rc[0]=0;
			(void)sldns_wire2str_rcode_buf((int)FLAGS_GET_RCODE(
				msg->rep->flags), rc, sizeof(rc));
			errinf(qstate, rc);
		} else	errinf(qstate, val_classification_to_string(subtype));
		errinf(qstate, "message fails to prove that");
		goto return_bogus;
	}
return_bogus:
	*ke = key_entry_create_bad(qstate->region, qinfo->qname,
		qinfo->qname_len, qinfo->qclass, BOGUS_KEY_TTL,
		reason_bogus, reason, *qstate->env->now);
	return (*ke) == NULL;
}

/**
 * Process DS response. Called from inform_supers.
 * Because it is in inform_supers, the mesh itself is busy doing callbacks
 * for a state that is to be deleted soon; don't touch the mesh; instead
 * set a state in the super, as the super will be reactivated soon.
 * Perform processing to determine what state to set in the super.
 *
 * @param qstate: query state that is validating and asked for a DS.
 * @param vq: validator query state
 * @param id: module id.
 * @param rcode: rcode result value.
 * @param msg: result message (if rcode is OK).
 * @param qinfo: from the sub query state, query info.
 * @param origin: the origin of msg.
 * @param suspend: returned true if the task takes too long and needs to
 * 	suspend to continue the effort later.
 * @param sub_qstate: the sub query state, that is the lookup that fetched
 *	the trust anchor data, it contains error information for the answer.
 *	Can be NULL.
 */
static void
process_ds_response(struct module_qstate* qstate, struct val_qstate* vq,
	int id, int rcode, struct dns_msg* msg, struct query_info* qinfo,
	struct sock_list* origin, int* suspend,
	struct module_qstate* sub_qstate)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	struct key_entry_key* dske = NULL;
	uint8_t* olds = vq->empty_DS_name;
	int ret;
	*suspend = 0;
	vq->empty_DS_name = NULL;
	if(sub_qstate && sub_qstate->rpz_applied) {
		verbose(VERB_ALGO, "rpz was applied to the DS lookup, "
			"make it insecure");
		vq->key_entry = NULL;
		vq->state = VAL_FINISHED_STATE;
		vq->chase_reply->security = sec_status_insecure;
		return;
	}
	ret = ds_response_to_ke(qstate, vq, id, rcode, msg, qinfo, &dske,
		sub_qstate);
	if(ret != 0) {
		switch(ret) {
		case 1:
			log_err("malloc failure in process_ds_response");
			vq->key_entry = NULL; /* make it error */
			vq->state = VAL_VALIDATE_STATE;
			return;
		case 2:
			*suspend = 1;
			return;
		default:
			log_err("unhandled error value for ds_response_to_ke");
			vq->key_entry = NULL; /* make it error */
			vq->state = VAL_VALIDATE_STATE;
			return;
		}
	}
	if(dske == NULL) {
		vq->empty_DS_name = regional_alloc_init(qstate->region,
			qinfo->qname, qinfo->qname_len);
		if(!vq->empty_DS_name) {
			log_err("malloc failure in empty_DS_name");
			vq->key_entry = NULL; /* make it error */
			vq->state = VAL_VALIDATE_STATE;
			return;
		}
		vq->empty_DS_len = qinfo->qname_len;
		vq->chain_blacklist = NULL;
		/* ds response indicated that we aren't on a delegation point.
		 * Keep the forState.state on FINDKEY. */
	} else if(key_entry_isgood(dske)) {
		vq->ds_rrset = key_entry_get_rrset(dske, qstate->region);
		if(!vq->ds_rrset) {
			log_err("malloc failure in process DS");
			vq->key_entry = NULL; /* make it error */
			vq->state = VAL_VALIDATE_STATE;
			return;
		}
		vq->chain_blacklist = NULL; /* fresh blacklist for next part*/
		/* Keep the forState.state on FINDKEY. */
	} else if(key_entry_isbad(dske) 
		&& vq->restart_count < ve->max_restart) {
		vq->empty_DS_name = olds;
		val_blacklist(&vq->chain_blacklist, qstate->region, origin, 1);
		qstate->errinf = NULL;
		vq->restart_count++;
	} else {
		if(key_entry_isbad(dske)) {
			errinf_origin(qstate, origin);
			errinf_dname(qstate, "for DS", qinfo->qname);
		}
		/* NOTE: the reason for the DS to be not good (that is, 
		 * either bad or null) should have been logged by 
		 * dsResponseToKE. */
		vq->key_entry = dske;
		/* The FINDKEY phase has ended, so move on. */
		vq->state = VAL_VALIDATE_STATE;
	}
}

/**
 * Process DNSKEY response. Called from inform_supers.
 * Sets the key entry in the state.
 * Because it is in inform_supers, the mesh itself is busy doing callbacks
 * for a state that is to be deleted soon; don't touch the mesh; instead
 * set a state in the super, as the super will be reactivated soon.
 * Perform processing to determine what state to set in the super.
 *
 * @param qstate: query state that is validating and asked for a DNSKEY.
 * @param vq: validator query state
 * @param id: module id.
 * @param rcode: rcode result value.
 * @param msg: result message (if rcode is OK).
 * @param qinfo: from the sub query state, query info.
 * @param origin: the origin of msg.
 * @param sub_qstate: the sub query state, that is the lookup that fetched
 *	the trust anchor data, it contains error information for the answer.
 */
static void
process_dnskey_response(struct module_qstate* qstate, struct val_qstate* vq,
	int id, int rcode, struct dns_msg* msg, struct query_info* qinfo,
	struct sock_list* origin, struct module_qstate* sub_qstate)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	struct key_entry_key* old = vq->key_entry;
	struct ub_packed_rrset_key* dnskey = NULL;
	int downprot;
	char reasonbuf[256];
	char* reason = NULL;
	sldns_ede_code reason_bogus = LDNS_EDE_DNSSEC_BOGUS;

	if(sub_qstate && sub_qstate->rpz_applied) {
		verbose(VERB_ALGO, "rpz was applied to the DNSKEY lookup, "
			"make it insecure");
		vq->key_entry = NULL;
		vq->state = VAL_FINISHED_STATE;
		vq->chase_reply->security = sec_status_insecure;
		return;
	}

	if(rcode == LDNS_RCODE_NOERROR)
		dnskey = reply_find_answer_rrset(qinfo, msg->rep);

	if(dnskey == NULL) {
		char* err;
		char rstr[1024];
		/* bad response */
		verbose(VERB_DETAIL, "Missing DNSKEY RRset in response to "
			"DNSKEY query.");

		if(vq->restart_count < ve->max_restart) {
			val_blacklist(&vq->chain_blacklist, qstate->region,
				origin, 1);
			qstate->errinf = NULL;
			vq->restart_count++;
			return;
		}
		err = errinf_to_str_misc(sub_qstate);
		if(!err) {
			snprintf(rstr, sizeof(rstr), "No DNSKEY record");
		} else {
			snprintf(rstr, sizeof(rstr), "No DNSKEY record "
				"[%s]", err);
		}
		reason_bogus = LDNS_EDE_DNSKEY_MISSING;
		vq->key_entry = key_entry_create_bad(qstate->region,
			qinfo->qname, qinfo->qname_len, qinfo->qclass,
			BOGUS_KEY_TTL, reason_bogus, rstr, *qstate->env->now);
		if(!vq->key_entry) {
			log_err("alloc failure in missing dnskey response");
			/* key_entry is NULL for failure in Validate */
		}
		errinf_ede(qstate, rstr, reason_bogus);
		errinf_origin(qstate, origin);
		errinf_dname(qstate, "for key", qinfo->qname);
		vq->state = VAL_VALIDATE_STATE;
		return;
	}
	if(!vq->ds_rrset) {
		log_err("internal error: no DS rrset for new DNSKEY response");
		vq->key_entry = NULL;
		vq->state = VAL_VALIDATE_STATE;
		return;
	}
	downprot = qstate->env->cfg->harden_algo_downgrade;
	vq->key_entry = val_verify_new_DNSKEYs(qstate->region, qstate->env,
		ve, dnskey, vq->ds_rrset, downprot, &reason, &reason_bogus,
		qstate, reasonbuf, sizeof(reasonbuf));

	if(!vq->key_entry) {
		log_err("out of memory in verify new DNSKEYs");
		vq->state = VAL_VALIDATE_STATE;
		return;
	}
	/* If the key entry isBad or isNull, then we can move on to the next
	 * state. */
	if(!key_entry_isgood(vq->key_entry)) {
		if(key_entry_isbad(vq->key_entry)) {
			if(vq->restart_count < ve->max_restart) {
				val_blacklist(&vq->chain_blacklist, 
					qstate->region, origin, 1);
				qstate->errinf = NULL;
				vq->restart_count++;
				vq->key_entry = old;
				return;
			}
			verbose(VERB_DETAIL, "Did not match a DS to a DNSKEY, "
				"thus bogus.");
			errinf_ede(qstate, reason, reason_bogus);
			errinf_origin(qstate, origin);
			errinf_dname(qstate, "for key", qinfo->qname);
		}
		vq->chain_blacklist = NULL;
		vq->state = VAL_VALIDATE_STATE;
		return;
	}
	vq->chain_blacklist = NULL;
	qstate->errinf = NULL;

	/* The DNSKEY validated, so cache it as a trusted key rrset. */
	key_cache_insert(ve->kcache, vq->key_entry,
		qstate->env->cfg->val_log_level >= 2);

	/* If good, we stay in the FINDKEY state. */
	log_query_info(VERB_DETAIL, "validated DNSKEY", qinfo);
}

/**
 * Process prime response
 * Sets the key entry in the state.
 *
 * @param qstate: query state that is validating and primed a trust anchor.
 * @param vq: validator query state
 * @param id: module id.
 * @param rcode: rcode result value.
 * @param msg: result message (if rcode is OK).
 * @param origin: the origin of msg.
 * @param sub_qstate: the sub query state, that is the lookup that fetched
 *	the trust anchor data, it contains error information for the answer.
 */
static void
process_prime_response(struct module_qstate* qstate, struct val_qstate* vq,
	int id, int rcode, struct dns_msg* msg, struct sock_list* origin,
	struct module_qstate* sub_qstate)
{
	struct val_env* ve = (struct val_env*)qstate->env->modinfo[id];
	struct ub_packed_rrset_key* dnskey_rrset = NULL;
	struct trust_anchor* ta = anchor_find(qstate->env->anchors, 
		vq->trust_anchor_name, vq->trust_anchor_labs,
		vq->trust_anchor_len, vq->qchase.qclass);
	if(!ta) {
		/* trust anchor revoked, restart with less anchors */
		vq->state = VAL_INIT_STATE;
		if(!vq->trust_anchor_name)
			vq->state = VAL_VALIDATE_STATE; /* break a loop */
		vq->trust_anchor_name = NULL;
		return;
	}
	/* Fetch and validate the keyEntry that corresponds to the 
	 * current trust anchor. */
	if(rcode == LDNS_RCODE_NOERROR) {
		dnskey_rrset = reply_find_rrset_section_an(msg->rep,
			ta->name, ta->namelen, LDNS_RR_TYPE_DNSKEY,
			ta->dclass);
	}

	if(ta->autr) {
		if(!autr_process_prime(qstate->env, ve, ta, dnskey_rrset,
			qstate)) {
			/* trust anchor revoked, restart with less anchors */
			vq->state = VAL_INIT_STATE;
			vq->trust_anchor_name = NULL;
			return;
		}
	}
	vq->key_entry = primeResponseToKE(dnskey_rrset, ta, qstate, id,
		sub_qstate);
	lock_basic_unlock(&ta->lock);
	if(vq->key_entry) {
		if(key_entry_isbad(vq->key_entry) 
			&& vq->restart_count < ve->max_restart) {
			val_blacklist(&vq->chain_blacklist, qstate->region, 
				origin, 1);
			qstate->errinf = NULL;
			vq->restart_count++;
			vq->key_entry = NULL;
			vq->state = VAL_INIT_STATE;
			return;
		} 
		vq->chain_blacklist = NULL;
		errinf_origin(qstate, origin);
		errinf_dname(qstate, "for trust anchor", ta->name);
		/* store the freshly primed entry in the cache */
		key_cache_insert(ve->kcache, vq->key_entry,
			qstate->env->cfg->val_log_level >= 2);
	}

	/* If the result of the prime is a null key, skip the FINDKEY state.*/
	if(!vq->key_entry || key_entry_isnull(vq->key_entry) ||
		key_entry_isbad(vq->key_entry)) {
		vq->state = VAL_VALIDATE_STATE;
	}
	/* the qstate will be reactivated after inform_super is done */
}

/* 
 * inform validator super.
 * 
 * @param qstate: query state that finished.
 * @param id: module id.
 * @param super: the qstate to inform.
 */
void
val_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super)
{
	struct val_qstate* vq = (struct val_qstate*)super->minfo[id];
	log_query_info(VERB_ALGO, "validator: inform_super, sub is",
		&qstate->qinfo);
	log_query_info(VERB_ALGO, "super is", &super->qinfo);
	if(!vq) {
		verbose(VERB_ALGO, "super: has no validator state");
		return;
	}
	if(vq->wait_prime_ta) {
		vq->wait_prime_ta = 0;
		process_prime_response(super, vq, id, qstate->return_rcode,
			qstate->return_msg, qstate->reply_origin, qstate);
		return;
	}
	if(qstate->qinfo.qtype == LDNS_RR_TYPE_DS) {
		int suspend;
		process_ds_response(super, vq, id, qstate->return_rcode,
			qstate->return_msg, &qstate->qinfo,
			qstate->reply_origin, &suspend, qstate);
		/* If NSEC3 was needed during validation, NULL the NSEC3 cache;
		 * it will be re-initiated if needed later on.
		 * Validation (and the cache table) are happening/allocated in
		 * the super qstate whilst the RRs are allocated (and pointed
		 * to) in this sub qstate. */
		if(vq->nsec3_cache_table.ct) {
			vq->nsec3_cache_table.ct = NULL;
		}
		if(suspend) {
			/* deep copy the return_msg to vq->sub_ds_msg; it will
			 * be resumed later in the super state with the caveat
			 * that the initial calculations will be re-caclulated
			 * and re-suspended there before continuing. */
			vq->sub_ds_msg = dns_msg_deepcopy_region(
				qstate->return_msg, super->region);
		}
		return;
	} else if(qstate->qinfo.qtype == LDNS_RR_TYPE_DNSKEY) {
		process_dnskey_response(super, vq, id, qstate->return_rcode,
			qstate->return_msg, &qstate->qinfo,
			qstate->reply_origin, qstate);
		return;
	}
	log_err("internal error in validator: no inform_supers possible");
}

void
val_clear(struct module_qstate* qstate, int id)
{
	struct val_qstate* vq;
	if(!qstate)
		return;
	vq = (struct val_qstate*)qstate->minfo[id];
	if(vq) {
		if(vq->suspend_timer) {
			comm_timer_delete(vq->suspend_timer);
		}
	}
	/* everything is allocated in the region, so assign NULL */
	qstate->minfo[id] = NULL;
}

size_t 
val_get_mem(struct module_env* env, int id)
{
	struct val_env* ve = (struct val_env*)env->modinfo[id];
	if(!ve)
		return 0;
	return sizeof(*ve) + key_cache_get_mem(ve->kcache) + 
		val_neg_get_mem(ve->neg_cache) +
		sizeof(size_t)*2*ve->nsec3_keyiter_count;
}

/**
 * The validator function block 
 */
static struct module_func_block val_block = {
	"validator",
	NULL, NULL, &val_init, &val_deinit, &val_operate, &val_inform_super,
	&val_clear, &val_get_mem
};

struct module_func_block* 
val_get_funcblock(void)
{
	return &val_block;
}

const char* 
val_state_to_string(enum val_state state)
{
	switch(state) {
		case VAL_INIT_STATE: return "VAL_INIT_STATE";
		case VAL_FINDKEY_STATE: return "VAL_FINDKEY_STATE";
		case VAL_VALIDATE_STATE: return "VAL_VALIDATE_STATE";
		case VAL_FINISHED_STATE: return "VAL_FINISHED_STATE";
	}
	return "UNKNOWN VALIDATOR STATE";
}

