/*
 * ipsecmod/ipsecmod.c - facilitate opportunistic IPsec module
 *
 * Copyright (c) 2017, NLnet Labs. All rights reserved.
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
 * This file contains a module that facilitates opportunistic IPsec. It does so
 * by also querying for the IPSECKEY for A/AAAA queries and calling a
 * configurable hook (eg. signaling an IKE daemon) before replying.
 */

#include "config.h"
#ifdef USE_IPSECMOD
#include "ipsecmod/ipsecmod.h"
#include "ipsecmod/ipsecmod-whitelist.h"
#include "util/fptr_wlist.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "services/cache/dns.h"
#include "sldns/wire2str.h"

/** Apply configuration to ipsecmod module 'global' state. */
static int
ipsecmod_apply_cfg(struct ipsecmod_env* ipsecmod_env, struct config_file* cfg)
{
	if(!cfg->ipsecmod_hook || (cfg->ipsecmod_hook && !cfg->ipsecmod_hook[0])) {
		log_err("ipsecmod: missing ipsecmod-hook.");
		return 0;
	}
	if(cfg->ipsecmod_whitelist &&
		!ipsecmod_whitelist_apply_cfg(ipsecmod_env, cfg))
		return 0;
	return 1;
}

int
ipsecmod_init(struct module_env* env, int id)
{
	struct ipsecmod_env* ipsecmod_env = (struct ipsecmod_env*)calloc(1,
		sizeof(struct ipsecmod_env));
	if(!ipsecmod_env) {
		log_err("malloc failure");
		return 0;
	}
	env->modinfo[id] = (void*)ipsecmod_env;
	ipsecmod_env->whitelist = NULL;
	if(!ipsecmod_apply_cfg(ipsecmod_env, env->cfg)) {
		log_err("ipsecmod: could not apply configuration settings.");
		return 0;
	}
	return 1;
}

void
ipsecmod_deinit(struct module_env* env, int id)
{
	struct ipsecmod_env* ipsecmod_env;
	if(!env || !env->modinfo[id])
		return;
	ipsecmod_env = (struct ipsecmod_env*)env->modinfo[id];
	/* Free contents. */
	ipsecmod_whitelist_delete(ipsecmod_env->whitelist);
	free(ipsecmod_env);
	env->modinfo[id] = NULL;
}

/** New query for ipsecmod. */
static int
ipsecmod_new(struct module_qstate* qstate, int id)
{
	struct ipsecmod_qstate* iq = (struct ipsecmod_qstate*)regional_alloc(
		qstate->region, sizeof(struct ipsecmod_qstate));
	qstate->minfo[id] = iq;
	if(!iq)
		return 0;
	/* Initialise it. */
	memset(iq, 0, sizeof(*iq));
	iq->enabled = qstate->env->cfg->ipsecmod_enabled;
	iq->is_whitelisted = ipsecmod_domain_is_whitelisted(
		(struct ipsecmod_env*)qstate->env->modinfo[id], qstate->qinfo.qname,
		qstate->qinfo.qname_len, qstate->qinfo.qclass);
	return 1;
}

/**
 * Exit module with an error status.
 * @param qstate: query state
 * @param id: module id.
 */
static void
ipsecmod_error(struct module_qstate* qstate, int id)
{
	qstate->ext_state[id] = module_error;
	qstate->return_rcode = LDNS_RCODE_SERVFAIL;
}

/**
 * Generate a request for the IPSECKEY.
 *
 * @param qstate: query state that is the parent.
 * @param id: module id.
 * @param name: what name to query for.
 * @param namelen: length of name.
 * @param qtype: query type.
 * @param qclass: query class.
 * @param flags: additional flags, such as the CD bit (BIT_CD), or 0.
 * @return false on alloc failure.
 */
static int
generate_request(struct module_qstate* qstate, int id, uint8_t* name,
	size_t namelen, uint16_t qtype, uint16_t qclass, uint16_t flags)
{
	struct module_qstate* newq;
	struct query_info ask;
	ask.qname = name;
	ask.qname_len = namelen;
	ask.qtype = qtype;
	ask.qclass = qclass;
	ask.local_alias = NULL;
	log_query_info(VERB_ALGO, "ipsecmod: generate request", &ask);

	/* Explicitly check for cycle before trying to attach. Will result in
	 * cleaner error message. The attach_sub code also checks for cycle but the
	 * message will be out of memory in both cases then. */
	fptr_ok(fptr_whitelist_modenv_detect_cycle(qstate->env->detect_cycle));
	if((*qstate->env->detect_cycle)(qstate, &ask,
		(uint16_t)(BIT_RD|flags), 0, 0)) {
		verbose(VERB_ALGO, "Could not generate request: cycle detected");
		return 0;
	}

	fptr_ok(fptr_whitelist_modenv_attach_sub(qstate->env->attach_sub));
	if(!(*qstate->env->attach_sub)(qstate, &ask,
		(uint16_t)(BIT_RD|flags), 0, 0, &newq)){
		log_err("Could not generate request: out of memory");
		return 0;
	}
	qstate->ext_state[id] = module_wait_subquery;
	return 1;
}

/**
 * Check if the string passed is a valid domain name with safe characters to
 * pass to a shell.
 * This will only allow:
 *  - digits
 *  - alphas
 *  - hyphen (not at the start)
 *  - dot (not at the start, or the only character)
 *  - underscore
 * @param s: pointer to the string.
 * @param slen: string's length.
 * @return true if s only contains safe characters; false otherwise.
 */
static int
domainname_has_safe_characters(char* s, size_t slen) {
	size_t i;
	for(i = 0; i < slen; i++) {
		if(s[i] == '\0') return 1;
		if((s[i] == '-' && i != 0)
			|| (s[i] == '.' && (i != 0 || s[1] == '\0'))
			|| (s[i] == '_') || (s[i] >= '0' && s[i] <= '9')
			|| (s[i] >= 'A' && s[i] <= 'Z')
			|| (s[i] >= 'a' && s[i] <= 'z')) {
			continue;
		}
		return 0;
	}
	return 1;
}

/**
 * Check if the stringified IPSECKEY RDATA contains safe characters to pass to
 * a shell.
 * This is only relevant for checking the gateway when the gateway type is 3
 * (domainname).
 * @param s: pointer to the string.
 * @param slen: string's length.
 * @return true if s contains only safe characters; false otherwise.
 */
static int
ipseckey_has_safe_characters(char* s, size_t slen) {
	int precedence, gateway_type, algorithm;
	char* gateway;
	gateway = (char*)calloc(slen, sizeof(char));
	if(!gateway) {
		log_err("ipsecmod: out of memory when calling the hook");
		return 0;
	}
	if(sscanf(s, "%d %d %d %s ",
			&precedence, &gateway_type, &algorithm, gateway) != 4) {
		free(gateway);
		return 0;
	}
	if(gateway_type != 3) {
		free(gateway);
		return 1;
	}
	if(domainname_has_safe_characters(gateway, slen)) {
		free(gateway);
		return 1;
	}
	free(gateway);
	return 0;
}

/**
 *  Prepare the data and call the hook.
 *
 *  @param qstate: query state.
 *  @param iq: ipsecmod qstate.
 *  @param ie: ipsecmod environment.
 *  @return true on success, false otherwise.
 */
static int
call_hook(struct module_qstate* qstate, struct ipsecmod_qstate* iq,
	struct ipsecmod_env* ATTR_UNUSED(ie))
{
	size_t slen, tempdata_len, tempstring_len, i;
	char str[65535], *s, *tempstring;
	int w = 0, w_temp, qtype;
	struct ub_packed_rrset_key* rrset_key;
	struct packed_rrset_data* rrset_data;
	uint8_t *tempdata;

	/* Check if a shell is available */
	if(system(NULL) == 0) {
		log_err("ipsecmod: no shell available for ipsecmod-hook");
		return 0;
	}

	/* Zero the buffer. */
	s = str;
	slen = sizeof(str);
	memset(s, 0, slen);

	/* Copy the hook into the buffer. */
	w += sldns_str_print(&s, &slen, "%s", qstate->env->cfg->ipsecmod_hook);
	/* Put space into the buffer. */
	w += sldns_str_print(&s, &slen, " ");
	/* Copy the qname into the buffer. */
	tempstring = sldns_wire2str_dname(qstate->qinfo.qname,
		qstate->qinfo.qname_len);
	if(!tempstring) {
		log_err("ipsecmod: out of memory when calling the hook");
		return 0;
	}
	if(!domainname_has_safe_characters(tempstring, strlen(tempstring))) {
		log_err("ipsecmod: qname has unsafe characters");
		free(tempstring);
		return 0;
	}
	w += sldns_str_print(&s, &slen, "\"%s\"", tempstring);
	free(tempstring);
	/* Put space into the buffer. */
	w += sldns_str_print(&s, &slen, " ");
	/* Copy the IPSECKEY TTL into the buffer. */
	rrset_data = (struct packed_rrset_data*)iq->ipseckey_rrset->entry.data;
	w += sldns_str_print(&s, &slen, "\"%ld\"", (long)rrset_data->ttl);
	/* Put space into the buffer. */
	w += sldns_str_print(&s, &slen, " ");
	rrset_key = reply_find_answer_rrset(&qstate->return_msg->qinfo,
		qstate->return_msg->rep);
	/* Double check that the records are indeed A/AAAA.
	 * This should never happen as this function is only executed for A/AAAA
	 * queries but make sure we don't pass anything other than A/AAAA to the
	 * shell. */
	qtype = ntohs(rrset_key->rk.type);
	if(qtype != LDNS_RR_TYPE_AAAA && qtype != LDNS_RR_TYPE_A) {
		log_err("ipsecmod: Answer is not of A or AAAA type");
		return 0;
	}
	rrset_data = (struct packed_rrset_data*)rrset_key->entry.data;
	/* Copy the A/AAAA record(s) into the buffer. Start and end this section
	 * with a double quote. */
	w += sldns_str_print(&s, &slen, "\"");
	for(i=0; i<rrset_data->count; i++) {
		if(i > 0) {
			/* Put space into the buffer. */
			w += sldns_str_print(&s, &slen, " ");
		}
		/* Ignore the first two bytes, they are the rr_data len. */
		w_temp = sldns_wire2str_rdata_buf(rrset_data->rr_data[i] + 2,
			rrset_data->rr_len[i] - 2, s, slen, qstate->qinfo.qtype);
		if(w_temp < 0) {
			/* Error in printout. */
			log_err("ipsecmod: Error in printing IP address");
			return 0;
		} else if((size_t)w_temp >= slen) {
			s = NULL; /* We do not want str to point outside of buffer. */
			slen = 0;
			log_err("ipsecmod: shell command too long");
			return 0;
		} else {
			s += w_temp;
			slen -= w_temp;
			w += w_temp;
		}
	}
	w += sldns_str_print(&s, &slen, "\"");
	/* Put space into the buffer. */
	w += sldns_str_print(&s, &slen, " ");
	/* Copy the IPSECKEY record(s) into the buffer. Start and end this section
	 * with a double quote. */
	w += sldns_str_print(&s, &slen, "\"");
	rrset_data = (struct packed_rrset_data*)iq->ipseckey_rrset->entry.data;
	for(i=0; i<rrset_data->count; i++) {
		if(i > 0) {
			/* Put space into the buffer. */
			w += sldns_str_print(&s, &slen, " ");
		}
		/* Ignore the first two bytes, they are the rr_data len. */
		tempdata = rrset_data->rr_data[i] + 2;
		tempdata_len = rrset_data->rr_len[i] - 2;
		/* Save the buffer pointers. */
		tempstring = s; tempstring_len = slen;
		w_temp = sldns_wire2str_ipseckey_scan(&tempdata, &tempdata_len, &s,
			&slen, NULL, 0, NULL);
		/* There was an error when parsing the IPSECKEY; reset the buffer
		 * pointers to their previous values. */
		if(w_temp == -1) {
			s = tempstring; slen = tempstring_len;
		} else if(w_temp > 0) {
			if(!ipseckey_has_safe_characters(
					tempstring, tempstring_len - slen)) {
				log_err("ipsecmod: ipseckey has unsafe characters");
				return 0;
			}
			w += w_temp;
		}
	}
	w += sldns_str_print(&s, &slen, "\"");
	if(w >= (int)sizeof(str)) {
		log_err("ipsecmod: shell command too long");
		return 0;
	}
	verbose(VERB_ALGO, "ipsecmod: shell command: '%s'", str);
	/* ipsecmod-hook should return 0 on success. */
	if(system(str) != 0)
		return 0;
	return 1;
}

/**
 * Handle an ipsecmod module event with a query
 * @param qstate: query state (from the mesh), passed between modules.
 * 	contains qstate->env module environment with global caches and so on.
 * @param iq: query state specific for this module.  per-query.
 * @param ie: environment specific for this module.  global.
 * @param id: module id.
 */
static void
ipsecmod_handle_query(struct module_qstate* qstate,
	struct ipsecmod_qstate* iq, struct ipsecmod_env* ie, int id)
{
	struct ub_packed_rrset_key* rrset_key;
	struct packed_rrset_data* rrset_data;
	size_t i;
	/* Pass to next module if we are not enabled and whitelisted. */
	if(!(iq->enabled && iq->is_whitelisted)) {
		qstate->ext_state[id] = module_wait_module;
		return;
	}
	/* New query, check if the query is for an A/AAAA record and disable
	 * caching for other modules. */
	if(!iq->ipseckey_done) {
		if(qstate->qinfo.qtype == LDNS_RR_TYPE_A ||
			qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA) {
			char type[16];
			sldns_wire2str_type_buf(qstate->qinfo.qtype, type,
				sizeof(type));
			verbose(VERB_ALGO, "ipsecmod: query for %s; engaging",
				type);
			qstate->no_cache_store = 1;
		}
		/* Pass request to next module. */
		qstate->ext_state[id] = module_wait_module;
		return;
	}
	/* IPSECKEY subquery is finished. */
	/* We have an IPSECKEY answer. */
	if(iq->ipseckey_rrset) {
		rrset_data = (struct packed_rrset_data*)iq->ipseckey_rrset->entry.data;
		if(rrset_data) {
			/* If bogus return SERVFAIL. */
			if(!qstate->env->cfg->ipsecmod_ignore_bogus &&
				rrset_data->security == sec_status_bogus) {
				log_err("ipsecmod: bogus IPSECKEY");
				errinf(qstate, "ipsecmod: bogus IPSECKEY");
				ipsecmod_error(qstate, id);
				return;
			}
			/* We have a valid IPSECKEY reply, call hook. */
			if(!call_hook(qstate, iq, ie) &&
				qstate->env->cfg->ipsecmod_strict) {
				log_err("ipsecmod: ipsecmod-hook failed");
				errinf(qstate, "ipsecmod: ipsecmod-hook failed");
				ipsecmod_error(qstate, id);
				return;
			}
			/* Make sure the A/AAAA's TTL is equal/less than the
			 * ipsecmod_max_ttl. */
			rrset_key = reply_find_answer_rrset(&qstate->return_msg->qinfo,
				qstate->return_msg->rep);
			rrset_data = (struct packed_rrset_data*)rrset_key->entry.data;
			if(rrset_data->ttl > (time_t)qstate->env->cfg->ipsecmod_max_ttl) {
				/* Update TTL for rrset to fixed value. */
				rrset_data->ttl = qstate->env->cfg->ipsecmod_max_ttl;
				for(i=0; i<rrset_data->count+rrset_data->rrsig_count; i++)
					rrset_data->rr_ttl[i] = qstate->env->cfg->ipsecmod_max_ttl;
				/* Also update reply_info's TTL */
				if(qstate->return_msg->rep->ttl > (time_t)qstate->env->cfg->ipsecmod_max_ttl) {
					qstate->return_msg->rep->ttl =
						qstate->env->cfg->ipsecmod_max_ttl;
					qstate->return_msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(
						qstate->return_msg->rep->ttl);
					qstate->return_msg->rep->serve_expired_ttl = qstate->return_msg->rep->ttl +
						qstate->env->cfg->serve_expired_ttl;
				}
			}
		}
	}
	/* Store A/AAAA in cache. */
	if(!dns_cache_store(qstate->env, &qstate->qinfo,
		qstate->return_msg->rep, 0, qstate->prefetch_leeway,
		0, qstate->region, qstate->query_flags, qstate->qstarttime,
		qstate->is_valrec)) {
		log_err("ipsecmod: out of memory caching record");
	}
	qstate->ext_state[id] = module_finished;
}

/**
 * Handle an ipsecmod module event with a response from the iterator.
 * @param qstate: query state (from the mesh), passed between modules.
 * 	contains qstate->env module environment with global caches and so on.
 * @param iq: query state specific for this module.  per-query.
 * @param ie: environment specific for this module.  global.
 * @param id: module id.
 */
static void
ipsecmod_handle_response(struct module_qstate* qstate,
	struct ipsecmod_qstate* ATTR_UNUSED(iq),
	struct ipsecmod_env* ATTR_UNUSED(ie), int id)
{
	/* Pass to previous module if we are not enabled and whitelisted. */
	if(!(iq->enabled && iq->is_whitelisted)) {
		qstate->ext_state[id] = module_finished;
		return;
	}
	/* check if the response is for an A/AAAA query. */
	if((qstate->qinfo.qtype == LDNS_RR_TYPE_A ||
		qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA) &&
		/* check that we had an answer for the A/AAAA query. */
		qstate->return_msg &&
		reply_find_answer_rrset(&qstate->return_msg->qinfo,
		qstate->return_msg->rep) &&
		/* check that another module didn't SERVFAIL. */
		qstate->return_rcode == LDNS_RCODE_NOERROR) {
		char type[16];
		sldns_wire2str_type_buf(qstate->qinfo.qtype, type,
			sizeof(type));
		verbose(VERB_ALGO, "ipsecmod: response for %s; generating IPSECKEY "
			"subquery", type);
		/* generate an IPSECKEY query. */
		if(!generate_request(qstate, id, qstate->qinfo.qname,
			qstate->qinfo.qname_len, LDNS_RR_TYPE_IPSECKEY,
			qstate->qinfo.qclass, 0)) {
			log_err("ipsecmod: could not generate subquery.");
			errinf(qstate, "ipsecmod: could not generate subquery.");
			ipsecmod_error(qstate, id);
		}
		return;
	}
	/* we are done with the query. */
	qstate->ext_state[id] = module_finished;
}

void
ipsecmod_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound)
{
	struct ipsecmod_env* ie = (struct ipsecmod_env*)qstate->env->modinfo[id];
	struct ipsecmod_qstate* iq = (struct ipsecmod_qstate*)qstate->minfo[id];
	verbose(VERB_QUERY, "ipsecmod[module %d] operate: extstate:%s event:%s",
		id, strextstate(qstate->ext_state[id]), strmodulevent(event));
	if(iq) log_query_info(VERB_QUERY, "ipsecmod operate: query",
		&qstate->qinfo);

	/* create ipsecmod_qstate. */
	if((event == module_event_new || event == module_event_pass) &&
		iq == NULL) {
		if(!ipsecmod_new(qstate, id)) {
			errinf(qstate, "ipsecmod: could not ipsecmod_new");
			ipsecmod_error(qstate, id);
			return;
		}
		iq = (struct ipsecmod_qstate*)qstate->minfo[id];
	}
	if(iq && (event == module_event_pass || event == module_event_new)) {
		ipsecmod_handle_query(qstate, iq, ie, id);
		return;
	}
	if(iq && (event == module_event_moddone)) {
		ipsecmod_handle_response(qstate, iq, ie, id);
		return;
	}
	if(iq && outbound) {
		/* cachedb does not need to process responses at this time
		 * ignore it.
		cachedb_process_response(qstate, iq, ie, id, outbound, event);
		*/
		return;
	}
	if(event == module_event_error) {
		verbose(VERB_ALGO, "got called with event error, giving up");
		errinf(qstate, "ipsecmod: got called with event error");
		ipsecmod_error(qstate, id);
		return;
	}
	if(!iq && (event == module_event_moddone)) {
		/* during priming, module done but we never started. */
		qstate->ext_state[id] = module_finished;
		return;
	}

	log_err("ipsecmod: bad event %s", strmodulevent(event));
	errinf(qstate, "ipsecmod: operate got bad event");
	ipsecmod_error(qstate, id);
	return;
}

void
ipsecmod_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super)
{
	struct ipsecmod_qstate* siq;
	log_query_info(VERB_ALGO, "ipsecmod: inform_super, sub is",
		&qstate->qinfo);
	log_query_info(VERB_ALGO, "super is", &super->qinfo);
	siq = (struct ipsecmod_qstate*)super->minfo[id];
	if(!siq) {
		verbose(VERB_ALGO, "super has no ipsecmod state");
		return;
	}

	if(qstate->return_msg) {
		struct ub_packed_rrset_key* rrset_key = reply_find_answer_rrset(
			&qstate->return_msg->qinfo, qstate->return_msg->rep);
		if(rrset_key) {
			/* We have an answer. */
			/* Copy to super's region. */
			rrset_key = packed_rrset_copy_region(rrset_key, super->region, 0);
			siq->ipseckey_rrset = rrset_key;
			if(!rrset_key) {
				log_err("ipsecmod: out of memory.");
			}
		}
	}
	/* Notify super to proceed. */
	siq->ipseckey_done = 1;
}

void
ipsecmod_clear(struct module_qstate* qstate, int id)
{
	if(!qstate)
		return;
	qstate->minfo[id] = NULL;
}

size_t
ipsecmod_get_mem(struct module_env* env, int id)
{
	struct ipsecmod_env* ie = (struct ipsecmod_env*)env->modinfo[id];
	if(!ie)
		return 0;
	return sizeof(*ie) + ipsecmod_whitelist_get_mem(ie->whitelist);
}

/**
 * The ipsecmod function block
 */
static struct module_func_block ipsecmod_block = {
	"ipsecmod",
	NULL, NULL, &ipsecmod_init, &ipsecmod_deinit, &ipsecmod_operate,
	&ipsecmod_inform_super, &ipsecmod_clear, &ipsecmod_get_mem
};

struct module_func_block*
ipsecmod_get_funcblock(void)
{
	return &ipsecmod_block;
}
#endif /* USE_IPSECMOD */
