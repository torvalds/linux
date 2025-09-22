/*
 * edns-subnet/subnetmod.c - edns subnet module. Must be called before validator
 * and iterator.
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
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
 * subnet module for unbound.
 */

#include "config.h"

#ifdef CLIENT_SUBNET /* keeps splint happy */

#include "edns-subnet/subnetmod.h"
#include "edns-subnet/edns-subnet.h"
#include "edns-subnet/addrtree.h"
#include "edns-subnet/subnet-whitelist.h"

#include "services/mesh.h"
#include "services/cache/dns.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/fptr_wlist.h"
#include "util/storage/slabhash.h"
#include "util/config_file.h"
#include "util/data/msgreply.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "iterator/iter_utils.h"
#ifdef USE_CACHEDB
#include "cachedb/cachedb.h"
#endif

/** externally called */
void 
subnet_data_delete(void *d, void *ATTR_UNUSED(arg))
{
	struct subnet_msg_cache_data *r;
	r = (struct subnet_msg_cache_data*)d;
	addrtree_delete(r->tree4);
	addrtree_delete(r->tree6);
	free(r);
}

/** externally called */
size_t 
msg_cache_sizefunc(void *k, void *d)
{
	struct msgreply_entry *q = (struct msgreply_entry*)k;
	struct subnet_msg_cache_data *r = (struct subnet_msg_cache_data*)d;
	size_t s = sizeof(struct msgreply_entry) 
		+ sizeof(struct subnet_msg_cache_data)
		+ q->key.qname_len + lock_get_mem(&q->entry.lock);
	s += addrtree_size(r->tree4);
	s += addrtree_size(r->tree6);
	return s;
}

/** new query for ecs module */
static int
subnet_new_qstate(struct module_qstate *qstate, int id)
{
	struct subnet_qstate *sq = (struct subnet_qstate*)regional_alloc(
		qstate->region, sizeof(struct subnet_qstate));
	if(!sq) 
		return 0;
	qstate->minfo[id] = sq;
	memset(sq, 0, sizeof(*sq));
	sq->started_no_cache_store = qstate->no_cache_store;
	sq->started_no_cache_lookup = qstate->no_cache_lookup;
	return 1;
}

/** Add ecs struct to edns list, after parsing it to wire format. */
void
subnet_ecs_opt_list_append(struct ecs_data* ecs, struct edns_option** list,
	struct module_qstate *qstate, struct regional *region)
{
	size_t sn_octs, sn_octs_remainder;
	sldns_buffer* buf = qstate->env->scratch_buffer;

	if(ecs->subnet_validdata) {
		log_assert(ecs->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4 || 
			ecs->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP6);
		log_assert(ecs->subnet_addr_fam != EDNSSUBNET_ADDRFAM_IP4 || 
			ecs->subnet_source_mask <=  INET_SIZE*8);
		log_assert(ecs->subnet_addr_fam != EDNSSUBNET_ADDRFAM_IP6 || 
			ecs->subnet_source_mask <= INET6_SIZE*8);

		sn_octs = ecs->subnet_source_mask / 8;
		sn_octs_remainder =
			(size_t)((ecs->subnet_source_mask % 8)>0?1:0);
		
		log_assert(sn_octs + sn_octs_remainder <= INET6_SIZE);
		
		sldns_buffer_clear(buf);
		sldns_buffer_write_u16(buf, ecs->subnet_addr_fam);
		sldns_buffer_write_u8(buf, ecs->subnet_source_mask);
		sldns_buffer_write_u8(buf, ecs->subnet_scope_mask);
		sldns_buffer_write(buf, ecs->subnet_addr, sn_octs);
		if(sn_octs_remainder)
			sldns_buffer_write_u8(buf, ecs->subnet_addr[sn_octs] & 
				~(0xFF >> (ecs->subnet_source_mask % 8)));
		sldns_buffer_flip(buf);

		edns_opt_list_append(list,
				qstate->env->cfg->client_subnet_opcode,
				sn_octs + sn_octs_remainder + 4,
				sldns_buffer_begin(buf), region);
	}
}

int ecs_whitelist_check(struct query_info* qinfo,
	uint16_t ATTR_UNUSED(flags), struct module_qstate* qstate,
	struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* ATTR_UNUSED(zone), size_t ATTR_UNUSED(zonelen),
	struct regional *region, int id, void* ATTR_UNUSED(cbargs))
{
	struct subnet_qstate *sq;
	struct subnet_env *sn_env;
	
	if(!(sq=(struct subnet_qstate*)qstate->minfo[id]))
		return 1;
	sn_env = (struct subnet_env*)qstate->env->modinfo[id];

	/* Cache by default, might be disabled after parsing EDNS option
	 * received from nameserver. */
	if(!iter_stub_fwd_no_cache(qstate, &qstate->qinfo, NULL, NULL, NULL, 0)
		&& sq->ecs_client_in.subnet_validdata) {
		qstate->no_cache_store = 0;
	}

	sq->subnet_sent_no_subnet = 0;
	if(sq->ecs_server_out.subnet_validdata && ((sq->subnet_downstream &&
		qstate->env->cfg->client_subnet_always_forward) ||
		ecs_is_whitelisted(sn_env->whitelist, 
		addr, addrlen, qinfo->qname, qinfo->qname_len,
		qinfo->qclass))) {
		/* Address on whitelist or client query contains ECS option, we
		 * want to sent out ECS. Only add option if it is not already
		 * set. */
		if(!edns_opt_list_find(qstate->edns_opts_back_out,
			qstate->env->cfg->client_subnet_opcode)) {
			/* if the client is not wanting an EDNS subnet option,
			 * omit it and store that we omitted it but actually
			 * are doing EDNS subnet to the server. */
			if(sq->ecs_server_out.subnet_source_mask == 0) {
				sq->subnet_sent_no_subnet = 1;
				sq->subnet_sent = 0;
				return 1;
			}
			subnet_ecs_opt_list_append(&sq->ecs_server_out,
				&qstate->edns_opts_back_out, qstate, region);
		}
		sq->subnet_sent = 1;
	}
	else {
		/* Outgoing ECS option is set, but we don't want to sent it to
		 * this address, remove option. */
		if(edns_opt_list_find(qstate->edns_opts_back_out,
			qstate->env->cfg->client_subnet_opcode)) {
			edns_opt_list_remove(&qstate->edns_opts_back_out,
				qstate->env->cfg->client_subnet_opcode);
		}
		sq->subnet_sent = 0;
	}
	return 1;
}


void
subnet_markdel(void* key)
{
	struct msgreply_entry *e = (struct msgreply_entry*)key;
	e->key.qtype = 0;
	e->key.qclass = 0;
}

int
subnetmod_init(struct module_env *env, int id)
{
	struct subnet_env *sn_env = (struct subnet_env*)calloc(1,
		sizeof(struct subnet_env));
	if(!sn_env) {
		log_err("malloc failure");
		return 0;
	}
	alloc_init(&sn_env->alloc, NULL, 0);
	env->modinfo[id] = (void*)sn_env;

	/* Warn that serve-expired and prefetch do not work with the subnet
	 * module cache. */
	if(env->cfg->serve_expired)
		log_warn(
			"subnetcache: serve-expired is set but not working "
			"for data originating from the subnet module cache.");
	if(env->cfg->prefetch)
		log_warn(
			"subnetcache: prefetch is set but not working "
			"for data originating from the subnet module cache.");
	/* Copy msg_cache settings */
	sn_env->subnet_msg_cache = slabhash_create(env->cfg->msg_cache_slabs,
		HASH_DEFAULT_STARTARRAY, env->cfg->msg_cache_size,
		msg_cache_sizefunc, query_info_compare, query_entry_delete,
		subnet_data_delete, NULL);
	slabhash_setmarkdel(sn_env->subnet_msg_cache, &subnet_markdel);
	if(!sn_env->subnet_msg_cache) {
		log_err("subnetcache: could not create cache");
		free(sn_env);
		env->modinfo[id] = NULL;
		return 0;
	}
	/* whitelist for edns subnet capable servers */
	sn_env->whitelist = ecs_whitelist_create();
	if(!sn_env->whitelist ||
		!ecs_whitelist_apply_cfg(sn_env->whitelist, env->cfg)) {
		log_err("subnetcache: could not create ECS whitelist");
		slabhash_delete(sn_env->subnet_msg_cache);
		free(sn_env);
		env->modinfo[id] = NULL;
		return 0;
	}

	verbose(VERB_QUERY, "subnetcache: option registered (%d)",
		env->cfg->client_subnet_opcode);
	/* Create new mesh state for all queries. */
	env->unique_mesh = 1;
	if(!edns_register_option(env->cfg->client_subnet_opcode,
		env->cfg->client_subnet_always_forward /* bypass cache */,
		1 /* no aggregation */, env)) {
		log_err("subnetcache: could not register opcode");
		ecs_whitelist_delete(sn_env->whitelist);
		slabhash_delete(sn_env->subnet_msg_cache);
		free(sn_env);
		env->modinfo[id] = NULL;
		return 0;
	}
	inplace_cb_register((void*)ecs_whitelist_check, inplace_cb_query, NULL,
		env, id);
	inplace_cb_register((void*)ecs_edns_back_parsed,
		inplace_cb_edns_back_parsed, NULL, env, id);
	inplace_cb_register((void*)ecs_query_response,
		inplace_cb_query_response, NULL, env, id);
	lock_rw_init(&sn_env->biglock);
	return 1;
}

void
subnetmod_deinit(struct module_env *env, int id)
{
	struct subnet_env *sn_env;
	if(!env || !env->modinfo[id])
		return;
	sn_env = (struct subnet_env*)env->modinfo[id];
	lock_rw_destroy(&sn_env->biglock);
	inplace_cb_delete(env, inplace_cb_edns_back_parsed, id);
	inplace_cb_delete(env, inplace_cb_query, id);
	inplace_cb_delete(env, inplace_cb_query_response, id);
	ecs_whitelist_delete(sn_env->whitelist);
	slabhash_delete(sn_env->subnet_msg_cache);
	alloc_clear(&sn_env->alloc);
	free(sn_env);
	env->modinfo[id] = NULL;
}

/** Tells client that upstream has no/improper support */
static void
cp_edns_bad_response(struct ecs_data *target, struct ecs_data *source)
{
	target->subnet_scope_mask  = 0;
	target->subnet_source_mask = source->subnet_source_mask;
	target->subnet_addr_fam    = source->subnet_addr_fam;
	memcpy(target->subnet_addr, source->subnet_addr, INET6_SIZE);
	target->subnet_validdata = 1;
}

static void
delfunc(void *envptr, void *elemptr) {
	struct reply_info *elem = (struct reply_info *)elemptr;
	struct subnet_env *env = (struct subnet_env *)envptr;
	reply_info_parsedelete(elem, &env->alloc);
}

static size_t
sizefunc(void *elemptr) {
	struct reply_info *elem  = (struct reply_info *)elemptr;
	size_t s = sizeof (struct reply_info) - sizeof (struct rrset_ref)
		+ elem->rrset_count * sizeof (struct rrset_ref)
		+ elem->rrset_count * sizeof (struct ub_packed_rrset_key *);
	size_t i;
	for (i = 0; i < elem->rrset_count; i++) {
		struct ub_packed_rrset_key *key = elem->rrsets[i];
		struct packed_rrset_data *data = key->entry.data;
		s += ub_rrset_sizefunc(key, data);
	}
	if(elem->reason_bogus_str)
		s += strlen(elem->reason_bogus_str)+1;
	return s;
}

/**
 * Select tree from cache entry based on edns data.
 * If for address family not present it will create a new one.
 * NULL on failure to create. */
static struct addrtree* 
get_tree(struct subnet_msg_cache_data *data, struct ecs_data *edns, 
	struct subnet_env *env, struct config_file* cfg)
{
	struct addrtree *tree;
	if (edns->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4) {
		if (!data->tree4)
			data->tree4 = addrtree_create(
				cfg->max_client_subnet_ipv4, &delfunc,
				&sizefunc, env, cfg->max_ecs_tree_size_ipv4);
		tree = data->tree4;
	} else {
		if (!data->tree6)
			data->tree6 = addrtree_create(
				cfg->max_client_subnet_ipv6, &delfunc,
				&sizefunc, env, cfg->max_ecs_tree_size_ipv6);
		tree = data->tree6;
	}
	return tree;
}

static void
update_cache(struct module_qstate *qstate, int id)
{
	struct msgreply_entry *mrep_entry;
	struct addrtree *tree;
	struct reply_info *rep;
	struct query_info qinf;
	struct subnet_env *sne = qstate->env->modinfo[id];
	struct subnet_qstate *sq = (struct subnet_qstate*)qstate->minfo[id];
	struct slabhash *subnet_msg_cache = sne->subnet_msg_cache;
	struct ecs_data *edns = &sq->ecs_client_in;
	size_t i;
	int only_match_scope_zero, diff_size;

	/* We already calculated hash upon lookup (lookup_and_reply) if we were
	 * allowed to look in the ECS cache */
	hashvalue_type h = qstate->minfo[id] &&
		((struct subnet_qstate*)qstate->minfo[id])->qinfo_hash_calculated?
		((struct subnet_qstate*)qstate->minfo[id])->qinfo_hash :
		query_info_hash(&qstate->qinfo, qstate->query_flags);
	/* Step 1, general qinfo lookup */
	struct lruhash_entry* lru_entry = slabhash_lookup(subnet_msg_cache, h,
		&qstate->qinfo, 1);
	int need_to_insert = (lru_entry == NULL);
	if (!lru_entry) {
		void* data = calloc(1,
			sizeof(struct subnet_msg_cache_data));
		if(!data) {
			log_err("malloc failed");
			return;
		}
		qinf = qstate->qinfo;
		qinf.qname = memdup(qstate->qinfo.qname,
			qstate->qinfo.qname_len);
		if(!qinf.qname) {
			free(data);
			log_err("memdup failed");
			return;
		}
		mrep_entry = query_info_entrysetup(&qinf, data, h);
		free(qinf.qname); /* if qname 'consumed', it is set to NULL */
		if (!mrep_entry) {
			free(data);
			log_err("query_info_entrysetup failed");
			return;
		}
		lru_entry = &mrep_entry->entry;
		lock_rw_wrlock(&lru_entry->lock);
	}
	/* lru_entry->lock is locked regardless of how we got here,
	 * either from the slabhash_lookup, or above in the new allocated */
	/* Step 2, find the correct tree */
	if (!(tree = get_tree(lru_entry->data, edns, sne, qstate->env->cfg))) {
		lock_rw_unlock(&lru_entry->lock);
		log_err("subnetcache: cache insertion failed");
		return;
	}
	lock_quick_lock(&sne->alloc.lock);
	rep = reply_info_copy(qstate->return_msg->rep, &sne->alloc, NULL);
	lock_quick_unlock(&sne->alloc.lock);
	if (!rep) {
		lock_rw_unlock(&lru_entry->lock);
		log_err("subnetcache: cache insertion failed");
		return;
	}

	/* store RRsets */
	for(i=0; i<rep->rrset_count; i++) {
		rep->ref[i].key = rep->rrsets[i];
		rep->ref[i].id = rep->rrsets[i]->id;
	}
	reply_info_set_ttls(rep, *qstate->env->now);
	reply_info_sortref(rep);
	rep->flags |= (BIT_RA | BIT_QR); /* fix flags to be sensible for */
	rep->flags &= ~(BIT_AA | BIT_CD);/* a reply based on the cache   */
	if(edns->subnet_source_mask == 0 && edns->subnet_scope_mask == 0)
		only_match_scope_zero = 1;
	else only_match_scope_zero = 0;
	diff_size = (int)tree->size_bytes;
	addrtree_insert(tree, (addrkey_t*)edns->subnet_addr, 
		edns->subnet_source_mask, sq->max_scope, rep,
		rep->ttl, *qstate->env->now, only_match_scope_zero);
	diff_size = (int)tree->size_bytes - diff_size;

	lock_rw_unlock(&lru_entry->lock);
	if (need_to_insert) {
		slabhash_insert(subnet_msg_cache, h, lru_entry, lru_entry->data,
			NULL);
	} else {
		slabhash_update_space_used(subnet_msg_cache, h, NULL,
			diff_size);
	}
}

/** Lookup in cache and reply true iff reply is sent. */
static int
lookup_and_reply(struct module_qstate *qstate, int id, struct subnet_qstate *sq, int prefetch)
{
	struct lruhash_entry *e;
	struct module_env *env = qstate->env;
	struct subnet_env *sne = (struct subnet_env*)env->modinfo[id];
	hashvalue_type h = query_info_hash(&qstate->qinfo, qstate->query_flags);
	struct subnet_msg_cache_data *data;
	struct ecs_data *ecs = &sq->ecs_client_in;
	struct addrtree *tree;
	struct addrnode *node;
	uint8_t scope;

	memset(&sq->ecs_client_out, 0, sizeof(sq->ecs_client_out));

	if (sq) {
		sq->qinfo_hash = h; /* Might be useful on cache miss */
		sq->qinfo_hash_calculated = 1;
	}
	e = slabhash_lookup(sne->subnet_msg_cache, h, &qstate->qinfo, 1);
	if (!e) return 0; /* qinfo not in cache */
	data = e->data;
	tree = (ecs->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4)?
		data->tree4 : data->tree6;
	if (!tree) { /* qinfo in cache but not for this family */
		lock_rw_unlock(&e->lock);
		return 0;
	}
	node = addrtree_find(tree, (addrkey_t*)ecs->subnet_addr, 
		ecs->subnet_source_mask, *env->now);
	if (!node) { /* plain old cache miss */
		lock_rw_unlock(&e->lock);
		return 0;
	}

	qstate->return_msg = tomsg(NULL, &qstate->qinfo,
		(struct reply_info *)node->elem, qstate->region, *env->now, 0,
		env->scratch);
	scope = (uint8_t)node->scope;
	lock_rw_unlock(&e->lock);
	
	if (!qstate->return_msg) { /* Failed allocation or expired TTL */
		return 0;
	}
	
	if (sq->subnet_downstream) { /* relay to interested client */
		sq->ecs_client_out.subnet_scope_mask = scope;
		sq->ecs_client_out.subnet_addr_fam = ecs->subnet_addr_fam;
		sq->ecs_client_out.subnet_source_mask = ecs->subnet_source_mask;
		memcpy(&sq->ecs_client_out.subnet_addr, &ecs->subnet_addr,
			INET6_SIZE);
		sq->ecs_client_out.subnet_validdata = 1;
	}

	if (prefetch && *qstate->env->now >= ((struct reply_info *)node->elem)->prefetch_ttl) {
		qstate->need_refetch = 1;
	}
	return 1;
}

/**
 * Test first bits of addresses for equality. Caller is responsible
 * for making sure that both a and b are at least net/8 octets long.
 * @param a: first address.
 * @param a: seconds address.
 * @param net: Number of bits to test.
 * @return: 1 if equal, 0 otherwise.
 */
static int 
common_prefix(uint8_t *a, uint8_t *b, uint8_t net)
{
	size_t n = (size_t)net / 8;
	return !memcmp(a, b, n) && ((net % 8) == 0 || a[n] == b[n]);
}

/**
 * Create sub request that looks up the query.
 * @param qstate: query state
 * @param sq: subnet qstate
 * @return false on failure.
 */
static int
generate_sub_request(struct module_qstate *qstate, struct subnet_qstate* sq)
{
	struct module_qstate* subq = NULL;
	uint16_t qflags = 0; /* OPCODE QUERY, no flags */
	int prime = 0;
	int valrec = 0;
	struct query_info qinf;
	qinf.qname = qstate->qinfo.qname;
	qinf.qname_len = qstate->qinfo.qname_len;
	qinf.qtype = qstate->qinfo.qtype;
	qinf.qclass = qstate->qinfo.qclass;
	qinf.local_alias = NULL;

	qflags |= BIT_RD;
	if((qstate->query_flags & BIT_CD)!=0) {
		qflags |= BIT_CD;
		valrec = 1;
	}

	fptr_ok(fptr_whitelist_modenv_attach_sub(qstate->env->attach_sub));
	if(!(*qstate->env->attach_sub)(qstate, &qinf, qflags, prime, valrec,
		&subq)) {
		return 0;
	}
	if(subq) {
		/* It is possible to access the subquery module state. */
		if(sq->ecs_client_in.subnet_source_mask == 0 &&
			edns_opt_list_find(qstate->edns_opts_front_in,
				qstate->env->cfg->client_subnet_opcode)) {
			subq->no_cache_store = 1;
		}
	}
	return 1;
}

/**
 * Perform the query without subnet
 * @param qstate: query state
 * @param sq: subnet qstate
 * @return module state
 */
static enum module_ext_state
generate_lookup_without_subnet(struct module_qstate *qstate,
	struct subnet_qstate* sq)
{
	verbose(VERB_ALGO, "subnetcache: make subquery to look up without subnet");
	if(!generate_sub_request(qstate, sq)) {
		verbose(VERB_ALGO, "Could not generate sub query");
		qstate->return_rcode = LDNS_RCODE_FORMERR;
		qstate->return_msg = NULL;
		return module_finished;
	}
	sq->wait_subquery = 1;
	return module_wait_subquery;
}

static enum module_ext_state
eval_response(struct module_qstate *qstate, int id, struct subnet_qstate *sq)
{
	struct subnet_env *sne = qstate->env->modinfo[id];

	struct ecs_data *c_in  = &sq->ecs_client_in; /* rcvd from client */
	struct ecs_data *c_out = &sq->ecs_client_out;/* will send to client */
	struct ecs_data *s_in  = &sq->ecs_server_in; /* rcvd from auth */
	struct ecs_data *s_out = &sq->ecs_server_out;/* sent to auth */

	memset(c_out, 0, sizeof(*c_out));

	if (!qstate->return_msg) {
		/* already an answer and its not a message, but retain
		 * the actual rcode, instead of module_error, so send
		 * module_finished */
		return module_finished;
	}

	/* We have not asked for subnet data */
	if (!sq->subnet_sent && !sq->subnet_sent_no_subnet) {
		if (s_in->subnet_validdata)
			verbose(VERB_QUERY, "subnetcache: received spurious data");
		if (sq->subnet_downstream) /* Copy back to client */
			cp_edns_bad_response(c_out, c_in);
		return module_finished;
	}

	/* subnet sent but nothing came back */
	if (!s_in->subnet_validdata && !sq->subnet_sent_no_subnet) {
		/* The authority indicated no support for edns subnet. As a
		 * consequence the answer ended up in the regular cache. It
		 * is still useful to put it in the edns subnet cache for
		 * when a client explicitly asks for subnet specific answer. */
		verbose(VERB_QUERY, "subnetcache: Authority indicates no support");
		return generate_lookup_without_subnet(qstate, sq);
	}

	/* Purposefully there was no sent subnet, and there is consequently
	 * no subnet in the answer. If there was, use the subnet in the answer
	 * anyway. But if there is not, treat it as a prefix 0 answer. */
	if(sq->subnet_sent_no_subnet && !s_in->subnet_validdata) {
		/* Fill in 0.0.0.0/0 scope 0, or ::0/0 scope 0, for caching. */
		s_in->subnet_addr_fam = s_out->subnet_addr_fam;
		s_in->subnet_source_mask = 0;
		s_in->subnet_scope_mask = 0;
		memset(s_in->subnet_addr, 0, INET6_SIZE);
		s_in->subnet_validdata = 1;
	}

	/* Being here means we have asked for and got a subnet specific 
	 * answer. Also, the answer from the authority is not yet cached 
	 * anywhere. */

	/* can we accept response? */
	if(s_out->subnet_addr_fam != s_in->subnet_addr_fam ||
		s_out->subnet_source_mask != s_in->subnet_source_mask ||
		!common_prefix(s_out->subnet_addr, s_in->subnet_addr, 
			s_out->subnet_source_mask))
	{
		/* we can not accept, perform query without option */
		verbose(VERB_QUERY, "subnetcache: forged data");
		s_out->subnet_validdata = 0;
		(void)edns_opt_list_remove(&qstate->edns_opts_back_out,
			qstate->env->cfg->client_subnet_opcode);
		sq->subnet_sent = 0;
		sq->subnet_sent_no_subnet = 0;
		return generate_lookup_without_subnet(qstate, sq);
	}

	lock_rw_wrlock(&sne->biglock);
	if(!sq->started_no_cache_store) {
		update_cache(qstate, id);
	}
	sne->num_msg_nocache++;
	lock_rw_unlock(&sne->biglock);

	/* If there is an expired answer in the global cache, remove that,
	 * because expired answers would otherwise resurface once the ecs data
	 * expires, giving once in a while global data responses for ecs
	 * domains, with serve expired enabled. */
	if(qstate->env->cfg->serve_expired) {
		msg_cache_remove(qstate->env, qstate->qinfo.qname,
			qstate->qinfo.qname_len, qstate->qinfo.qtype,
			qstate->qinfo.qclass, 0);
#ifdef USE_CACHEDB
		if(qstate->env->cachedb_enabled)
			cachedb_msg_remove(qstate);
#endif
	}

	if (sq->subnet_downstream) {
		/* Client wants to see the answer, echo option back
		 * and adjust the scope. */
		c_out->subnet_addr_fam = c_in->subnet_addr_fam;
		c_out->subnet_source_mask = c_in->subnet_source_mask;
		memcpy(&c_out->subnet_addr, &c_in->subnet_addr, INET6_SIZE);
		c_out->subnet_scope_mask = sq->max_scope;
		/* Limit scope returned to client to scope used for caching. */
		if(c_out->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4) {
			if(c_out->subnet_scope_mask >
				qstate->env->cfg->max_client_subnet_ipv4) {
				c_out->subnet_scope_mask =
					qstate->env->cfg->max_client_subnet_ipv4;
			}
		}
		else if(c_out->subnet_scope_mask >
				qstate->env->cfg->max_client_subnet_ipv6) {
				c_out->subnet_scope_mask =
					qstate->env->cfg->max_client_subnet_ipv6;
		}
		c_out->subnet_validdata = 1;
	}
	return module_finished;
}

/** Parse EDNS opt data containing ECS */
static int
parse_subnet_option(struct edns_option* ecs_option, struct ecs_data* ecs)
{
	memset(ecs, 0, sizeof(*ecs));
	if (ecs_option->opt_len < 4)
		return 0;

	ecs->subnet_addr_fam = sldns_read_uint16(ecs_option->opt_data);
	ecs->subnet_source_mask = ecs_option->opt_data[2];
	ecs->subnet_scope_mask = ecs_option->opt_data[3];
	/* remaining bytes indicate address */
	
	/* validate input*/
	/* option length matches calculated length? */
	if (ecs_option->opt_len != (size_t)((ecs->subnet_source_mask+7)/8 + 4))
		return 0;
	if (ecs_option->opt_len - 4 > INET6_SIZE || ecs_option->opt_len == 0)
		return 0;
	if (ecs->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4) {
		if (ecs->subnet_source_mask > 32 || ecs->subnet_scope_mask > 32)
			return 0;
	} else if (ecs->subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP6) {
		if (ecs->subnet_source_mask > 128 ||
			ecs->subnet_scope_mask > 128)
			return 0;
	} else
		return 0;
	
	/* valid ECS data, write to ecs_data */
	if (copy_clear(ecs->subnet_addr, INET6_SIZE, ecs_option->opt_data + 4, 
		ecs_option->opt_len - 4, ecs->subnet_source_mask))
		return 0;
	ecs->subnet_validdata = 1;
	return 1;
}

void
subnet_option_from_ss(struct sockaddr_storage *ss, struct ecs_data* ecs,
	struct config_file* cfg)
{
	void* sinaddr;

	/* Construct subnet option from original query */
	if(((struct sockaddr_in*)ss)->sin_family == AF_INET) {
		ecs->subnet_source_mask = cfg->max_client_subnet_ipv4;
		ecs->subnet_addr_fam = EDNSSUBNET_ADDRFAM_IP4;
		sinaddr = &((struct sockaddr_in*)ss)->sin_addr;
		if (!copy_clear( ecs->subnet_addr, INET6_SIZE,
			(uint8_t *)sinaddr, INET_SIZE,
			ecs->subnet_source_mask)) {
			ecs->subnet_validdata = 1;
		}
	}
#ifdef INET6
	else {
		ecs->subnet_source_mask = cfg->max_client_subnet_ipv6;
		ecs->subnet_addr_fam = EDNSSUBNET_ADDRFAM_IP6;
		sinaddr = &((struct sockaddr_in6*)ss)->sin6_addr;
		if (!copy_clear( ecs->subnet_addr, INET6_SIZE,
			(uint8_t *)sinaddr, INET6_SIZE,
			ecs->subnet_source_mask)) {
			ecs->subnet_validdata = 1;
		}
	}
#else
			/* We don't know how to handle ip6, just pass */
#endif /* INET6 */
}

int
ecs_query_response(struct module_qstate* qstate, struct dns_msg* response,
	int id, void* ATTR_UNUSED(cbargs))
{
	struct subnet_qstate *sq;
	
	if(!response || !(sq=(struct subnet_qstate*)qstate->minfo[id]))
		return 1;

	if(sq->subnet_sent &&
		FLAGS_GET_RCODE(response->rep->flags) == LDNS_RCODE_REFUSED) {
		/* REFUSED response to ECS query, remove ECS option. */
		edns_opt_list_remove(&qstate->edns_opts_back_out,
			qstate->env->cfg->client_subnet_opcode);
		sq->subnet_sent = 0;
		sq->subnet_sent_no_subnet = 0;
		memset(&sq->ecs_server_out, 0, sizeof(sq->ecs_server_out));
	} else if (!sq->track_max_scope &&
		FLAGS_GET_RCODE(response->rep->flags) == LDNS_RCODE_NOERROR &&
		response->rep->an_numrrsets > 0
		) {
		struct ub_packed_rrset_key* s = response->rep->rrsets[0];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME &&
			query_dname_compare(qstate->qinfo.qname,
			s->rk.dname) == 0) {
			/* CNAME response for QNAME. From now on keep track of
			 * longest received ECS prefix for all queries on this
			 * qstate. */
			sq->track_max_scope = 1;
		}
	}
	return 1;
}

/** verbose print edns subnet option in pretty print */
static void
subnet_log_print(const char* s, struct edns_option* ecs_opt)
{
	if(verbosity >= VERB_ALGO) {
		char buf[256];
		char* str = buf;
		size_t str_len = sizeof(buf);
		if(!ecs_opt) {
			verbose(VERB_ALGO, "%s (null)", s);
			return;
		}
		(void)sldns_wire2str_edns_subnet_print(&str, &str_len,
			ecs_opt->opt_data, ecs_opt->opt_len);
		verbose(VERB_ALGO, "%s %s", s, buf);
	}
}

int
ecs_edns_back_parsed(struct module_qstate* qstate, int id,
	void* ATTR_UNUSED(cbargs))
{
	struct subnet_qstate *sq;
	struct edns_option* ecs_opt;
	
	if(!(sq=(struct subnet_qstate*)qstate->minfo[id]))
		return 1;
	if((ecs_opt = edns_opt_list_find(
		qstate->edns_opts_back_in,
		qstate->env->cfg->client_subnet_opcode)) &&
		parse_subnet_option(ecs_opt, &sq->ecs_server_in) &&
		sq->subnet_sent && sq->ecs_server_in.subnet_validdata) {
			subnet_log_print("answer has edns subnet", ecs_opt);
			/* Only skip global cache store if we sent an ECS option
			 * and received one back. Answers from non-whitelisted
			 * servers will end up in global cache. Answers for
			 * queries with 0 source will not (unless nameserver
			 * does not support ECS). */
			qstate->no_cache_store = 1;
			if(!sq->track_max_scope || (sq->track_max_scope &&
				sq->ecs_server_in.subnet_scope_mask >
				sq->max_scope))
				sq->max_scope = sq->ecs_server_in.subnet_scope_mask;
	} else if(sq->subnet_sent_no_subnet) {
		/* The answer can be stored as scope 0, not in global cache. */
		qstate->no_cache_store = 1;
	} else if(sq->subnet_sent) {
		/* Need another query to be able to store in global cache. */
		qstate->no_cache_store = 1;
	}

	return 1;
}

void
subnetmod_operate(struct module_qstate *qstate, enum module_ev event, 
	int id, struct outbound_entry* outbound)
{
	struct subnet_env *sne = qstate->env->modinfo[id];
	struct subnet_qstate *sq = (struct subnet_qstate*)qstate->minfo[id];
	
	verbose(VERB_QUERY, "subnetcache[module %d] operate: extstate:%s "
		"event:%s", id, strextstate(qstate->ext_state[id]), 
		strmodulevent(event));
	log_query_info(VERB_QUERY, "subnetcache operate: query", &qstate->qinfo);

	if(sq && sq->wait_subquery_done) {
		/* The subquery lookup returned. */
		if(sq->ecs_client_in.subnet_source_mask == 0 &&
			edns_opt_list_find(qstate->edns_opts_front_in,
				qstate->env->cfg->client_subnet_opcode)) {
			if(!sq->started_no_cache_store &&
				qstate->return_msg) {
				lock_rw_wrlock(&sne->biglock);
				update_cache(qstate, id);
				lock_rw_unlock(&sne->biglock);
			}
			if (sq->subnet_downstream)
				cp_edns_bad_response(&sq->ecs_client_out,
					&sq->ecs_client_in);
			/* It is a scope zero lookup, append edns subnet
			 * option to the querier. */
			subnet_ecs_opt_list_append(&sq->ecs_client_out,
				&qstate->edns_opts_front_out, qstate,
				qstate->region);
		}
		sq->wait_subquery_done = 0;
		qstate->ext_state[id] = module_finished;
		qstate->no_cache_store = sq->started_no_cache_store;
		qstate->no_cache_lookup = sq->started_no_cache_lookup;
		return;
	}
	if((event == module_event_new || event == module_event_pass) &&
		sq == NULL) {
		struct edns_option* ecs_opt;
		if(!subnet_new_qstate(qstate, id)) {
			qstate->return_msg = NULL;
			qstate->ext_state[id] = module_finished;
			return;
		}

		sq = (struct subnet_qstate*)qstate->minfo[id];
		if(sq->wait_subquery)
			return; /* Wait for that subquery to return */

		if((ecs_opt = edns_opt_list_find(
			qstate->edns_opts_front_in,
			qstate->env->cfg->client_subnet_opcode))) {
			if(!parse_subnet_option(ecs_opt, &sq->ecs_client_in)) {
				/* Wrongly formatted ECS option. RFC mandates to
				 * return FORMERROR. */
				qstate->return_rcode = LDNS_RCODE_FORMERR;
				qstate->ext_state[id] = module_finished;
				return;
			}
			subnet_log_print("query has edns subnet", ecs_opt);
			sq->subnet_downstream = 1;
		}
		else if(qstate->mesh_info->reply_list) {
			subnet_option_from_ss(
				&qstate->mesh_info->reply_list->query_reply.client_addr,
				&sq->ecs_client_in, qstate->env->cfg);
		}
		else if(qstate->client_addr.ss_family != AF_UNSPEC) {
			subnet_option_from_ss(
				&qstate->client_addr,
				&sq->ecs_client_in, qstate->env->cfg);
		}
		
		if(sq->ecs_client_in.subnet_validdata == 0) {
			/* No clients are interested in result or we could not
			 * parse it, we don't do client subnet */
			sq->ecs_server_out.subnet_validdata = 0;
			if(edns_opt_list_find(qstate->edns_opts_front_in,
				qstate->env->cfg->client_subnet_opcode)) {
				/* aggregated this deaggregated state */
				qstate->ext_state[id] =
					generate_lookup_without_subnet(
					qstate, sq);
				return;
			}
			verbose(VERB_ALGO, "subnetcache: pass to next module");
			qstate->ext_state[id] = module_wait_module;
			return;
		}

		/* Limit to minimum allowed source mask */
		if(sq->ecs_client_in.subnet_source_mask != 0 && (
			(sq->ecs_client_in.subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4 &&
			 sq->ecs_client_in.subnet_source_mask < qstate->env->cfg->min_client_subnet_ipv4) ||
			(sq->ecs_client_in.subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP6 &&
			 sq->ecs_client_in.subnet_source_mask < qstate->env->cfg->min_client_subnet_ipv6))) {
				qstate->return_rcode = LDNS_RCODE_REFUSED;
				qstate->ext_state[id] = module_finished;
				return;
		}

		if(!sq->started_no_cache_lookup && !qstate->blacklist) {
			lock_rw_wrlock(&sne->biglock);
			if(qstate->mesh_info->reply_list &&
				lookup_and_reply(qstate, id, sq,
				qstate->env->cfg->prefetch)) {
				sne->num_msg_cache++;
				lock_rw_unlock(&sne->biglock);
				verbose(VERB_QUERY, "subnetcache: answered from cache");
				qstate->ext_state[id] = module_finished;

				subnet_ecs_opt_list_append(&sq->ecs_client_out,
					&qstate->edns_opts_front_out, qstate,
					qstate->region);
				if(verbosity >= VERB_ALGO) {
					subnet_log_print("reply has edns subnet",
						edns_opt_list_find(
						qstate->edns_opts_front_out,
						qstate->env->cfg->
						client_subnet_opcode));
				}
				return;
			}
			lock_rw_unlock(&sne->biglock);
		}
		if(sq->ecs_client_in.subnet_source_mask == 0 &&
			edns_opt_list_find(qstate->edns_opts_front_in,
				qstate->env->cfg->client_subnet_opcode)) {
			/* client asked for resolution without edns subnet */
			qstate->ext_state[id] = generate_lookup_without_subnet(
				qstate, sq);
			return;
		}
		
		sq->ecs_server_out.subnet_addr_fam =
			sq->ecs_client_in.subnet_addr_fam;
		sq->ecs_server_out.subnet_source_mask =
			sq->ecs_client_in.subnet_source_mask;
		/* Limit source prefix to configured maximum */
		if(sq->ecs_server_out.subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP4 
			&& sq->ecs_server_out.subnet_source_mask >
			qstate->env->cfg->max_client_subnet_ipv4)
			sq->ecs_server_out.subnet_source_mask =
				qstate->env->cfg->max_client_subnet_ipv4;
		else if(sq->ecs_server_out.subnet_addr_fam == EDNSSUBNET_ADDRFAM_IP6 
			&& sq->ecs_server_out.subnet_source_mask >
			qstate->env->cfg->max_client_subnet_ipv6)
			sq->ecs_server_out.subnet_source_mask =
				qstate->env->cfg->max_client_subnet_ipv6;
		/* Safe to copy completely, even if the source is limited by the
		 * configuration. subnet_ecs_opt_list_append() will limit the address.
		 * */
		memcpy(&sq->ecs_server_out.subnet_addr,
			sq->ecs_client_in.subnet_addr, INET6_SIZE);
		sq->ecs_server_out.subnet_scope_mask = 0;
		sq->ecs_server_out.subnet_validdata = 1;
		if(sq->ecs_server_out.subnet_source_mask != 0 &&
			qstate->env->cfg->client_subnet_always_forward &&
			sq->subnet_downstream)
			/* ECS specific data required, do not look at the global
			 * cache in other modules. */
			qstate->no_cache_lookup = 1;
		
		/* pass request to next module */
		verbose(VERB_ALGO,
			"subnetcache: not found in cache. pass to next module");
		qstate->ext_state[id] = module_wait_module;
		return;
	}
	if(sq && sq->wait_subquery)
		return; /* Wait for that subquery to return */
	/* Query handed back by next module, we have a 'final' answer */
	if(sq && event == module_event_moddone) {
		qstate->ext_state[id] = eval_response(qstate, id, sq);
		if(qstate->ext_state[id] == module_finished &&
			qstate->return_msg) {
			subnet_ecs_opt_list_append(&sq->ecs_client_out,
				&qstate->edns_opts_front_out, qstate,
				qstate->region);
			if(verbosity >= VERB_ALGO) {
				subnet_log_print("reply has edns subnet",
					edns_opt_list_find(
					qstate->edns_opts_front_out,
					qstate->env->cfg->
					client_subnet_opcode));
			}
		}
		qstate->no_cache_store = sq->started_no_cache_store;
		qstate->no_cache_lookup = sq->started_no_cache_lookup;
		return;
	}
	if(sq && outbound) {
		return;
	}
	/* We are being revisited */
	if(event == module_event_pass || event == module_event_new) {
		/* Just pass it on, we already did the work */
		verbose(VERB_ALGO, "subnetcache: pass to next module");
		qstate->ext_state[id] = module_wait_module;
		return;
	}
	if(!sq && (event == module_event_moddone)) {
		/* during priming, module done but we never started */
		qstate->ext_state[id] = module_finished;
		return;
	}
	log_err("subnetcache: bad event %s", strmodulevent(event));
	qstate->ext_state[id] = module_error;
	return;
}

void
subnetmod_clear(struct module_qstate *ATTR_UNUSED(qstate),
	int ATTR_UNUSED(id))
{
	/* qstate has no data outside region */
}

void
subnetmod_inform_super(struct module_qstate *qstate, int id,
	struct module_qstate *super)
{
	struct subnet_qstate* super_sq =
		(struct subnet_qstate*)super->minfo[id];
	log_query_info(VERB_ALGO, "subnetcache inform_super: query",
		&super->qinfo);
	super_sq->wait_subquery = 0;
	super_sq->wait_subquery_done = 1;
	if(qstate->return_rcode != LDNS_RCODE_NOERROR ||
		!qstate->return_msg) {
		super->return_msg = NULL;
		super->return_rcode = LDNS_RCODE_SERVFAIL;
		return;
	}
	super->return_rcode = LDNS_RCODE_NOERROR;
	super->return_msg = dns_copy_msg(qstate->return_msg, super->region);
	if(!super->return_msg) {
		log_err("subnetcache: copy response, out of memory");
		super->return_rcode = LDNS_RCODE_SERVFAIL;
	}
}

size_t
subnetmod_get_mem(struct module_env *env, int id)
{
	struct subnet_env *sn_env = env->modinfo[id];
	if (!sn_env) return 0;
	return sizeof(*sn_env) + 
		slabhash_get_mem(sn_env->subnet_msg_cache) +
		ecs_whitelist_get_mem(sn_env->whitelist);
}

/**
 * The module function block 
 */
static struct module_func_block subnetmod_block = {
	"subnetcache",
	NULL, NULL, &subnetmod_init, &subnetmod_deinit, &subnetmod_operate,
	&subnetmod_inform_super, &subnetmod_clear, &subnetmod_get_mem
};

struct module_func_block*
subnetmod_get_funcblock(void)
{
	return &subnetmod_block;
}

/** Wrappers for static functions to unit test */
size_t
unittest_wrapper_subnetmod_sizefunc(void *elemptr)
{
	return sizefunc(elemptr);
}

#endif  /* CLIENT_SUBNET */
