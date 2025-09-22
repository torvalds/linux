/*
 * services/cache/infra.c - infrastructure cache, server rtt and capabilities
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
 * This file contains the infrastructure cache.
 */
#include "config.h"
#include "sldns/rrdef.h"
#include "sldns/str2wire.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "services/cache/infra.h"
#include "util/storage/slabhash.h"
#include "util/storage/lookup3.h"
#include "util/data/dname.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "iterator/iterator.h"

/** ratelimit value for delegation point */
int infra_dp_ratelimit = 0;

/** ratelimit value for client ip addresses,
 *  in queries per second. */
int infra_ip_ratelimit = 0;

/** ratelimit value for client ip addresses,
 *  in queries per second.
 *  For clients with a valid DNS Cookie. */
int infra_ip_ratelimit_cookie = 0;

/** Minus 1000 because that is outside of the RTTBAND, so
 * blacklisted servers stay blacklisted if this is chosen.
 * If USEFUL_SERVER_TOP_TIMEOUT is below 1000 (configured via RTT_MAX_TIMEOUT,
 * infra-cache-max-rtt) change it to just above the RTT_BAND. */
int
still_useful_timeout()
{
	return
	USEFUL_SERVER_TOP_TIMEOUT < 1000 ||
	USEFUL_SERVER_TOP_TIMEOUT - 1000 <= RTT_BAND
		?RTT_BAND + 1
		:USEFUL_SERVER_TOP_TIMEOUT - 1000;
}

size_t 
infra_sizefunc(void* k, void* ATTR_UNUSED(d))
{
	struct infra_key* key = (struct infra_key*)k;
	return sizeof(*key) + sizeof(struct infra_data) + key->namelen
		+ lock_get_mem(&key->entry.lock);
}

int 
infra_compfunc(void* key1, void* key2)
{
	struct infra_key* k1 = (struct infra_key*)key1;
	struct infra_key* k2 = (struct infra_key*)key2;
	int r = sockaddr_cmp(&k1->addr, k1->addrlen, &k2->addr, k2->addrlen);
	if(r != 0)
		return r;
	if(k1->namelen != k2->namelen) {
		if(k1->namelen < k2->namelen)
			return -1;
		return 1;
	}
	return query_dname_compare(k1->zonename, k2->zonename);
}

void 
infra_delkeyfunc(void* k, void* ATTR_UNUSED(arg))
{
	struct infra_key* key = (struct infra_key*)k;
	if(!key)
		return;
	lock_rw_destroy(&key->entry.lock);
	free(key->zonename);
	free(key);
}

void 
infra_deldatafunc(void* d, void* ATTR_UNUSED(arg))
{
	struct infra_data* data = (struct infra_data*)d;
	free(data);
}

size_t 
rate_sizefunc(void* k, void* ATTR_UNUSED(d))
{
	struct rate_key* key = (struct rate_key*)k;
	return sizeof(*key) + sizeof(struct rate_data) + key->namelen
		+ lock_get_mem(&key->entry.lock);
}

int 
rate_compfunc(void* key1, void* key2)
{
	struct rate_key* k1 = (struct rate_key*)key1;
	struct rate_key* k2 = (struct rate_key*)key2;
	if(k1->namelen != k2->namelen) {
		if(k1->namelen < k2->namelen)
			return -1;
		return 1;
	}
	return query_dname_compare(k1->name, k2->name);
}

void 
rate_delkeyfunc(void* k, void* ATTR_UNUSED(arg))
{
	struct rate_key* key = (struct rate_key*)k;
	if(!key)
		return;
	lock_rw_destroy(&key->entry.lock);
	free(key->name);
	free(key);
}

void 
rate_deldatafunc(void* d, void* ATTR_UNUSED(arg))
{
	struct rate_data* data = (struct rate_data*)d;
	free(data);
}

/** find or create element in domainlimit tree */
static struct domain_limit_data* domain_limit_findcreate(
	struct rbtree_type* domain_limits, char* name)
{
	uint8_t* nm;
	int labs;
	size_t nmlen;
	struct domain_limit_data* d;

	/* parse name */
	nm = sldns_str2wire_dname(name, &nmlen);
	if(!nm) {
		log_err("could not parse %s", name);
		return NULL;
	}
	labs = dname_count_labels(nm);

	/* can we find it? */
	d = (struct domain_limit_data*)name_tree_find(domain_limits, nm,
		nmlen, labs, LDNS_RR_CLASS_IN);
	if(d) {
		free(nm);
		return d;
	}
	
	/* create it */
	d = (struct domain_limit_data*)calloc(1, sizeof(*d));
	if(!d) {
		free(nm);
		return NULL;
	}
	d->node.node.key = &d->node;
	d->node.name = nm;
	d->node.len = nmlen;
	d->node.labs = labs;
	d->node.dclass = LDNS_RR_CLASS_IN;
	d->lim = -1;
	d->below = -1;
	if(!name_tree_insert(domain_limits, &d->node, nm, nmlen, labs,
		LDNS_RR_CLASS_IN)) {
		log_err("duplicate element in domainlimit tree");
		free(nm);
		free(d);
		return NULL;
	}
	return d;
}

/** insert rate limit configuration into lookup tree */
static int infra_ratelimit_cfg_insert(struct rbtree_type* domain_limits,
	struct config_file* cfg)
{
	struct config_str2list* p;
	struct domain_limit_data* d;
	for(p = cfg->ratelimit_for_domain; p; p = p->next) {
		d = domain_limit_findcreate(domain_limits, p->str);
		if(!d)
			return 0;
		d->lim = atoi(p->str2);
	}
	for(p = cfg->ratelimit_below_domain; p; p = p->next) {
		d = domain_limit_findcreate(domain_limits, p->str);
		if(!d)
			return 0;
		d->below = atoi(p->str2);
	}
	return 1;
}

int
setup_domain_limits(struct rbtree_type* domain_limits, struct config_file* cfg)
{
	name_tree_init(domain_limits);
	if(!infra_ratelimit_cfg_insert(domain_limits, cfg)) {
		return 0;
	}
	name_tree_init_parents(domain_limits);
	return 1;
}

/** find or create element in wait limit netblock tree */
static struct wait_limit_netblock_info*
wait_limit_netblock_findcreate(struct rbtree_type* tree, char* str)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	struct wait_limit_netblock_info* d;

	if(!netblockstrtoaddr(str, 0, &addr, &addrlen, &net)) {
		log_err("cannot parse wait limit netblock '%s'", str);
		return 0;
	}

	/* can we find it? */
	d = (struct wait_limit_netblock_info*)addr_tree_find(tree, &addr,
		addrlen, net);
	if(d)
		return d;

	/* create it */
	d = (struct wait_limit_netblock_info*)calloc(1, sizeof(*d));
	if(!d)
		return NULL;
	d->limit = -1;
	if(!addr_tree_insert(tree, &d->node, &addr, addrlen, net)) {
		log_err("duplicate element in domainlimit tree");
		free(d);
		return NULL;
	}
	return d;
}


/** insert wait limit information into lookup tree */
static int
infra_wait_limit_netblock_insert(rbtree_type* wait_limits_netblock,
        rbtree_type* wait_limits_cookie_netblock, struct config_file* cfg)
{
	struct config_str2list* p;
	struct wait_limit_netblock_info* d;
	for(p = cfg->wait_limit_netblock; p; p = p->next) {
		d = wait_limit_netblock_findcreate(wait_limits_netblock,
			p->str);
		if(!d)
			return 0;
		d->limit = atoi(p->str2);
	}
	for(p = cfg->wait_limit_cookie_netblock; p; p = p->next) {
		d = wait_limit_netblock_findcreate(wait_limits_cookie_netblock,
			p->str);
		if(!d)
			return 0;
		d->limit = atoi(p->str2);
	}
	return 1;
}

/** Add a default wait limit netblock */
static int
wait_limit_netblock_default(struct rbtree_type* tree, char* str, int limit)
{
	struct wait_limit_netblock_info* d;
	d = wait_limit_netblock_findcreate(tree, str);
	if(!d)
		return 0;
	d->limit = limit;
	return 1;
}

int
setup_wait_limits(rbtree_type* wait_limits_netblock,
	rbtree_type* wait_limits_cookie_netblock, struct config_file* cfg)
{
	addr_tree_init(wait_limits_netblock);
	addr_tree_init(wait_limits_cookie_netblock);

	/* Insert defaults */
	/* The loopback address is separated from the rest of the network. */
	/* wait-limit-netblock: 127.0.0.0/8 -1 */
	if(!wait_limit_netblock_default(wait_limits_netblock, "127.0.0.0/8",
		-1))
		return 0;
	/* wait-limit-netblock: ::1/128 -1 */
	if(!wait_limit_netblock_default(wait_limits_netblock, "::1/128", -1))
		return 0;
	/* wait-limit-cookie-netblock: 127.0.0.0/8 -1 */
	if(!wait_limit_netblock_default(wait_limits_cookie_netblock,
		"127.0.0.0/8", -1))
		return 0;
	/* wait-limit-cookie-netblock: ::1/128 -1 */
	if(!wait_limit_netblock_default(wait_limits_cookie_netblock,
		"::1/128", -1))
		return 0;

	if(!infra_wait_limit_netblock_insert(wait_limits_netblock,
		wait_limits_cookie_netblock, cfg))
		return 0;
	addr_tree_init_parents(wait_limits_netblock);
	addr_tree_init_parents(wait_limits_cookie_netblock);
	return 1;
}

struct infra_cache* 
infra_create(struct config_file* cfg)
{
	struct infra_cache* infra = (struct infra_cache*)calloc(1, 
		sizeof(struct infra_cache));
	size_t maxmem = cfg->infra_cache_numhosts * (sizeof(struct infra_key)+
		sizeof(struct infra_data)+INFRA_BYTES_NAME);
	if(!infra) {
		return NULL;
	}
	infra->hosts = slabhash_create(cfg->infra_cache_slabs,
		INFRA_HOST_STARTSIZE, maxmem, &infra_sizefunc, &infra_compfunc,
		&infra_delkeyfunc, &infra_deldatafunc, NULL);
	if(!infra->hosts) {
		free(infra);
		return NULL;
	}
	infra->host_ttl = cfg->host_ttl;
	infra->infra_keep_probing = cfg->infra_keep_probing;
	infra_dp_ratelimit = cfg->ratelimit;
	infra->domain_rates = slabhash_create(cfg->ratelimit_slabs,
		INFRA_HOST_STARTSIZE, cfg->ratelimit_size,
		&rate_sizefunc, &rate_compfunc, &rate_delkeyfunc,
		&rate_deldatafunc, NULL);
	if(!infra->domain_rates) {
		infra_delete(infra);
		return NULL;
	}
	/* insert config data into ratelimits */
	if(!setup_domain_limits(&infra->domain_limits, cfg)) {
		infra_delete(infra);
		return NULL;
	}
	if(!setup_wait_limits(&infra->wait_limits_netblock,
		&infra->wait_limits_cookie_netblock, cfg)) {
		infra_delete(infra);
		return NULL;
	}
	infra_ip_ratelimit = cfg->ip_ratelimit;
	infra_ip_ratelimit_cookie = cfg->ip_ratelimit_cookie;
	infra->client_ip_rates = slabhash_create(cfg->ip_ratelimit_slabs,
	    INFRA_HOST_STARTSIZE, cfg->ip_ratelimit_size, &ip_rate_sizefunc,
	    &ip_rate_compfunc, &ip_rate_delkeyfunc, &ip_rate_deldatafunc, NULL);
	if(!infra->client_ip_rates) {
		infra_delete(infra);
		return NULL;
	}
	return infra;
}

/** delete domain_limit entries */
static void domain_limit_free(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	if(n) {
		free(((struct domain_limit_data*)n)->node.name);
		free(n);
	}
}

void
domain_limits_free(struct rbtree_type* domain_limits)
{
	if(!domain_limits)
		return;
	traverse_postorder(domain_limits, domain_limit_free, NULL);
}

/** delete wait_limit_netblock_info entries */
static void wait_limit_netblock_del(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	free(n);
}

void
wait_limits_free(struct rbtree_type* wait_limits_tree)
{
	if(!wait_limits_tree)
		return;
	traverse_postorder(wait_limits_tree, wait_limit_netblock_del,
		NULL);
}

void 
infra_delete(struct infra_cache* infra)
{
	if(!infra)
		return;
	slabhash_delete(infra->hosts);
	slabhash_delete(infra->domain_rates);
	domain_limits_free(&infra->domain_limits);
	slabhash_delete(infra->client_ip_rates);
	wait_limits_free(&infra->wait_limits_netblock);
	wait_limits_free(&infra->wait_limits_cookie_netblock);
	free(infra);
}

struct infra_cache* 
infra_adjust(struct infra_cache* infra, struct config_file* cfg)
{
	size_t maxmem;
	if(!infra)
		return infra_create(cfg);
	infra->host_ttl = cfg->host_ttl;
	infra->infra_keep_probing = cfg->infra_keep_probing;
	infra_dp_ratelimit = cfg->ratelimit;
	infra_ip_ratelimit = cfg->ip_ratelimit;
	infra_ip_ratelimit_cookie = cfg->ip_ratelimit_cookie;
	maxmem = cfg->infra_cache_numhosts * (sizeof(struct infra_key)+
		sizeof(struct infra_data)+INFRA_BYTES_NAME);
	/* divide cachesize by slabs and multiply by slabs, because if the
	 * cachesize is not an even multiple of slabs, that is the resulting
	 * size of the slabhash */
	if(!slabhash_is_size(infra->hosts, maxmem, cfg->infra_cache_slabs) ||
	   !slabhash_is_size(infra->domain_rates, cfg->ratelimit_size,
	   	cfg->ratelimit_slabs) ||
	   !slabhash_is_size(infra->client_ip_rates, cfg->ip_ratelimit_size,
	   	cfg->ip_ratelimit_slabs)) {
		infra_delete(infra);
		infra = infra_create(cfg);
	} else {
		/* reapply domain limits */
		traverse_postorder(&infra->domain_limits, domain_limit_free,
			NULL);
		if(!setup_domain_limits(&infra->domain_limits, cfg)) {
			infra_delete(infra);
			return NULL;
		}
	}
	return infra;
}

/** calculate the hash value for a host key
 *  set use_port to a non-0 number to use the port in
 *  the hash calculation; 0 to ignore the port.*/
static hashvalue_type
hash_addr(struct sockaddr_storage* addr, socklen_t addrlen,
  int use_port)
{
	hashvalue_type h = 0xab;
	/* select the pieces to hash, some OS have changing data inside */
	if(addr_is_ip6(addr, addrlen)) {
		struct sockaddr_in6* in6 = (struct sockaddr_in6*)addr;
		h = hashlittle(&in6->sin6_family, sizeof(in6->sin6_family), h);
		if(use_port){
			h = hashlittle(&in6->sin6_port, sizeof(in6->sin6_port), h);
		}
		h = hashlittle(&in6->sin6_addr, INET6_SIZE, h);
	} else {
		struct sockaddr_in* in = (struct sockaddr_in*)addr;
		h = hashlittle(&in->sin_family, sizeof(in->sin_family), h);
		if(use_port){
			h = hashlittle(&in->sin_port, sizeof(in->sin_port), h);
		}
		h = hashlittle(&in->sin_addr, INET_SIZE, h);
	}
	return h;
}

/** calculate infra hash for a key */
static hashvalue_type
hash_infra(struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* name)
{
	return dname_query_hash(name, hash_addr(addr, addrlen, 1));
}

/** lookup version that does not check host ttl (you check it) */
struct lruhash_entry* 
infra_lookup_nottl(struct infra_cache* infra, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* name, size_t namelen, int wr)
{
	struct infra_key k;
	k.addrlen = addrlen;
	memcpy(&k.addr, addr, addrlen);
	k.namelen = namelen;
	k.zonename = name;
	k.entry.hash = hash_infra(addr, addrlen, name);
	k.entry.key = (void*)&k;
	k.entry.data = NULL;
	return slabhash_lookup(infra->hosts, k.entry.hash, &k, wr);
}

/** init the data elements */
static void
data_entry_init(struct infra_cache* infra, struct lruhash_entry* e, 
	time_t timenow)
{
	struct infra_data* data = (struct infra_data*)e->data;
	data->ttl = timenow + infra->host_ttl;
	rtt_init(&data->rtt);
	data->edns_version = 0;
	data->edns_lame_known = 0;
	data->probedelay = 0;
	data->isdnsseclame = 0;
	data->rec_lame = 0;
	data->lame_type_A = 0;
	data->lame_other = 0;
	data->timeout_A = 0;
	data->timeout_AAAA = 0;
	data->timeout_other = 0;
}

/** 
 * Create and init a new entry for a host 
 * @param infra: infra structure with config parameters.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: name of zone
 * @param namelen: length of name.
 * @param tm: time now.
 * @return: the new entry or NULL on malloc failure.
 */
static struct lruhash_entry*
new_entry(struct infra_cache* infra, struct sockaddr_storage* addr, 
	socklen_t addrlen, uint8_t* name, size_t namelen, time_t tm)
{
	struct infra_data* data;
	struct infra_key* key = (struct infra_key*)malloc(sizeof(*key));
	if(!key)
		return NULL;
	data = (struct infra_data*)malloc(sizeof(struct infra_data));
	if(!data) {
		free(key);
		return NULL;
	}
	key->zonename = memdup(name, namelen);
	if(!key->zonename) {
		free(key);
		free(data);
		return NULL;
	}
	key->namelen = namelen;
	lock_rw_init(&key->entry.lock);
	key->entry.hash = hash_infra(addr, addrlen, name);
	key->entry.key = (void*)key;
	key->entry.data = (void*)data;
	key->addrlen = addrlen;
	memcpy(&key->addr, addr, addrlen);
	data_entry_init(infra, &key->entry, tm);
	return &key->entry;
}

int 
infra_host(struct infra_cache* infra, struct sockaddr_storage* addr,
        socklen_t addrlen, uint8_t* nm, size_t nmlen, time_t timenow,
	int* edns_vs, uint8_t* edns_lame_known, int* to)
{
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		nm, nmlen, 0);
	struct infra_data* data;
	int wr = 0;
	if(e && ((struct infra_data*)e->data)->ttl < timenow) {
		/* it expired, try to reuse existing entry */
		int old = ((struct infra_data*)e->data)->rtt.rto;
		time_t tprobe = ((struct infra_data*)e->data)->probedelay;
		uint8_t tA = ((struct infra_data*)e->data)->timeout_A;
		uint8_t tAAAA = ((struct infra_data*)e->data)->timeout_AAAA;
		uint8_t tother = ((struct infra_data*)e->data)->timeout_other;
		lock_rw_unlock(&e->lock);
		e = infra_lookup_nottl(infra, addr, addrlen, nm, nmlen, 1);
		if(e) {
			/* if its still there we have a writelock, init */
			/* re-initialise */
			/* do not touch lameness, it may be valid still */
			data_entry_init(infra, e, timenow);
			wr = 1;
			/* TOP_TIMEOUT remains on reuse */
			if(old >= USEFUL_SERVER_TOP_TIMEOUT) {
				((struct infra_data*)e->data)->rtt.rto
					= USEFUL_SERVER_TOP_TIMEOUT;
				((struct infra_data*)e->data)->probedelay = tprobe;
				((struct infra_data*)e->data)->timeout_A = tA;
				((struct infra_data*)e->data)->timeout_AAAA = tAAAA;
				((struct infra_data*)e->data)->timeout_other = tother;
			}
		}
	}
	if(!e) {
		/* insert new entry */
		if(!(e = new_entry(infra, addr, addrlen, nm, nmlen, timenow)))
			return 0;
		data = (struct infra_data*)e->data;
		*edns_vs = data->edns_version;
		*edns_lame_known = data->edns_lame_known;
		*to = rtt_timeout(&data->rtt);
		slabhash_insert(infra->hosts, e->hash, e, data, NULL);
		return 1;
	}
	/* use existing entry */
	data = (struct infra_data*)e->data;
	*edns_vs = data->edns_version;
	*edns_lame_known = data->edns_lame_known;
	*to = rtt_timeout(&data->rtt);
	if(*to >= PROBE_MAXRTO && (infra->infra_keep_probing ||
		rtt_notimeout(&data->rtt)*4 <= *to)) {
		/* delay other queries, this is the probe query */
		if(!wr) {
			lock_rw_unlock(&e->lock);
			e = infra_lookup_nottl(infra, addr,addrlen,nm,nmlen, 1);
			if(!e) { /* flushed from cache real fast, no use to
				allocate just for the probedelay */
				return 1;
			}
			data = (struct infra_data*)e->data;
		}
		/* add 999 to round up the timeout value from msec to sec,
		 * then add a whole second so it is certain that this probe
		 * has timed out before the next is allowed */
		data->probedelay = timenow + ((*to)+1999)/1000;
	}
	lock_rw_unlock(&e->lock);
	return 1;
}

int 
infra_set_lame(struct infra_cache* infra, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* nm, size_t nmlen, time_t timenow,
	int dnsseclame, int reclame, uint16_t qtype)
{
	struct infra_data* data;
	struct lruhash_entry* e;
	int needtoinsert = 0;
	e = infra_lookup_nottl(infra, addr, addrlen, nm, nmlen, 1);
	if(!e) {
		/* insert it */
		if(!(e = new_entry(infra, addr, addrlen, nm, nmlen, timenow))) {
			log_err("set_lame: malloc failure");
			return 0;
		}
		needtoinsert = 1;
	} else if( ((struct infra_data*)e->data)->ttl < timenow) {
		/* expired, reuse existing entry */
		data_entry_init(infra, e, timenow);
	}
	/* got an entry, now set the zone lame */
	data = (struct infra_data*)e->data;
	/* merge data (if any) */
	if(dnsseclame)
		data->isdnsseclame = 1;
	if(reclame)
		data->rec_lame = 1;
	if(!dnsseclame && !reclame && qtype == LDNS_RR_TYPE_A)
		data->lame_type_A = 1;
	if(!dnsseclame  && !reclame && qtype != LDNS_RR_TYPE_A)
		data->lame_other = 1;
	/* done */
	if(needtoinsert)
		slabhash_insert(infra->hosts, e->hash, e, e->data, NULL);
	else 	{ lock_rw_unlock(&e->lock); }
	return 1;
}

void 
infra_update_tcp_works(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* nm,
	size_t nmlen)
{
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		nm, nmlen, 1);
	struct infra_data* data;
	if(!e)
		return; /* doesn't exist */
	data = (struct infra_data*)e->data;
	if(data->rtt.rto >= RTT_MAX_TIMEOUT)
		/* do not disqualify this server altogether, it is better
		 * than nothing */
		data->rtt.rto = still_useful_timeout();
	lock_rw_unlock(&e->lock);
}

int 
infra_rtt_update(struct infra_cache* infra, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* nm, size_t nmlen, int qtype,
	int roundtrip, int orig_rtt, time_t timenow)
{
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		nm, nmlen, 1);
	struct infra_data* data;
	int needtoinsert = 0, expired = 0;
	int rto = 1;
	time_t oldprobedelay = 0;
	if(!e) {
		if(!(e = new_entry(infra, addr, addrlen, nm, nmlen, timenow)))
			return 0;
		needtoinsert = 1;
	} else if(((struct infra_data*)e->data)->ttl < timenow) {
		oldprobedelay = ((struct infra_data*)e->data)->probedelay;
		data_entry_init(infra, e, timenow);
		expired = 1;
	}
	/* have an entry, update the rtt */
	data = (struct infra_data*)e->data;
	if(roundtrip == -1) {
		if(needtoinsert || expired) {
			/* timeout on entry that has expired before the timer
			 * keep old timeout from the function caller */
			data->rtt.rto = orig_rtt;
			data->probedelay = oldprobedelay;
		}
		rtt_lost(&data->rtt, orig_rtt);
		if(qtype == LDNS_RR_TYPE_A) {
			if(data->timeout_A < TIMEOUT_COUNT_MAX)
				data->timeout_A++;
		} else if(qtype == LDNS_RR_TYPE_AAAA) {
			if(data->timeout_AAAA < TIMEOUT_COUNT_MAX)
				data->timeout_AAAA++;
		} else {
			if(data->timeout_other < TIMEOUT_COUNT_MAX)
				data->timeout_other++;
		}
	} else {
		/* if we got a reply, but the old timeout was above server
		 * selection height, delete the timeout so the server is
		 * fully available again */
		if(rtt_unclamped(&data->rtt) >= USEFUL_SERVER_TOP_TIMEOUT)
			rtt_init(&data->rtt);
		rtt_update(&data->rtt, roundtrip);
		data->probedelay = 0;
		if(qtype == LDNS_RR_TYPE_A)
			data->timeout_A = 0;
		else if(qtype == LDNS_RR_TYPE_AAAA)
			data->timeout_AAAA = 0;
		else	data->timeout_other = 0;
	}
	if(data->rtt.rto > 0)
		rto = data->rtt.rto;

	if(needtoinsert)
		slabhash_insert(infra->hosts, e->hash, e, e->data, NULL);
	else 	{ lock_rw_unlock(&e->lock); }
	return rto;
}

long long infra_get_host_rto(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* nm,
	size_t nmlen, struct rtt_info* rtt, int* delay, time_t timenow,
	int* tA, int* tAAAA, int* tother)
{
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		nm, nmlen, 0);
	struct infra_data* data;
	long long ttl = -2;
	if(!e) return -1;
	data = (struct infra_data*)e->data;
	if(data->ttl >= timenow) {
		ttl = (long long)(data->ttl - timenow);
		memmove(rtt, &data->rtt, sizeof(*rtt));
		if(timenow < data->probedelay)
			*delay = (int)(data->probedelay - timenow);
		else	*delay = 0;
	}
	*tA = (int)data->timeout_A;
	*tAAAA = (int)data->timeout_AAAA;
	*tother = (int)data->timeout_other;
	lock_rw_unlock(&e->lock);
	return ttl;
}

int 
infra_edns_update(struct infra_cache* infra, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* nm, size_t nmlen, int edns_version,
	time_t timenow)
{
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		nm, nmlen, 1);
	struct infra_data* data;
	int needtoinsert = 0;
	if(!e) {
		if(!(e = new_entry(infra, addr, addrlen, nm, nmlen, timenow)))
			return 0;
		needtoinsert = 1;
	} else if(((struct infra_data*)e->data)->ttl < timenow) {
		data_entry_init(infra, e, timenow);
	}
	/* have an entry, update the rtt, and the ttl */
	data = (struct infra_data*)e->data;
	/* do not update if noEDNS and stored is yesEDNS */
	if(!(edns_version == -1 && (data->edns_version != -1 &&
		data->edns_lame_known))) {
		data->edns_version = edns_version;
		data->edns_lame_known = 1;
	}

	if(needtoinsert)
		slabhash_insert(infra->hosts, e->hash, e, e->data, NULL);
	else 	{ lock_rw_unlock(&e->lock); }
	return 1;
}

int
infra_get_lame_rtt(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen,
        uint8_t* name, size_t namelen, uint16_t qtype, 
	int* lame, int* dnsseclame, int* reclame, int* rtt, time_t timenow)
{
	struct infra_data* host;
	struct lruhash_entry* e = infra_lookup_nottl(infra, addr, addrlen,
		name, namelen, 0);
	if(!e) 
		return 0;
	host = (struct infra_data*)e->data;
	*rtt = rtt_unclamped(&host->rtt);
	if(host->rtt.rto >= PROBE_MAXRTO && timenow >= host->probedelay
		&& infra->infra_keep_probing) {
		/* single probe, keep probing */
		if(*rtt >= USEFUL_SERVER_TOP_TIMEOUT)
			*rtt = still_useful_timeout();
	} else if(host->rtt.rto >= PROBE_MAXRTO && timenow < host->probedelay
		&& rtt_notimeout(&host->rtt)*4 <= host->rtt.rto) {
		/* single probe for this domain, and we are not probing */
		/* unless the query type allows a probe to happen */
		if(qtype == LDNS_RR_TYPE_A) {
			if(host->timeout_A >= TIMEOUT_COUNT_MAX)
				*rtt = USEFUL_SERVER_TOP_TIMEOUT;
			else	*rtt = still_useful_timeout();
		} else if(qtype == LDNS_RR_TYPE_AAAA) {
			if(host->timeout_AAAA >= TIMEOUT_COUNT_MAX)
				*rtt = USEFUL_SERVER_TOP_TIMEOUT;
			else	*rtt = still_useful_timeout();
		} else {
			if(host->timeout_other >= TIMEOUT_COUNT_MAX)
				*rtt = USEFUL_SERVER_TOP_TIMEOUT;
			else	*rtt = still_useful_timeout();
		}
	}
	/* expired entry */
	if(timenow > host->ttl) {
		/* see if this can be a re-probe of an unresponsive server */
		if(host->rtt.rto >= USEFUL_SERVER_TOP_TIMEOUT) {
			lock_rw_unlock(&e->lock);
			*rtt = still_useful_timeout();
			*lame = 0;
			*dnsseclame = 0;
			*reclame = 0;
			return 1;
		}
		lock_rw_unlock(&e->lock);
		return 0;
	}
	/* check lameness first */
	if(host->lame_type_A && qtype == LDNS_RR_TYPE_A) {
		lock_rw_unlock(&e->lock);
		*lame = 1;
		*dnsseclame = 0;
		*reclame = 0;
		return 1;
	} else if(host->lame_other && qtype != LDNS_RR_TYPE_A) {
		lock_rw_unlock(&e->lock);
		*lame = 1;
		*dnsseclame = 0;
		*reclame = 0;
		return 1;
	} else if(host->isdnsseclame) {
		lock_rw_unlock(&e->lock);
		*lame = 0;
		*dnsseclame = 1;
		*reclame = 0;
		return 1;
	} else if(host->rec_lame) {
		lock_rw_unlock(&e->lock);
		*lame = 0;
		*dnsseclame = 0;
		*reclame = 1;
		return 1;
	}
	/* no lameness for this type of query */
	lock_rw_unlock(&e->lock);
	*lame = 0;
	*dnsseclame = 0;
	*reclame = 0;
	return 1;
}

int infra_find_ratelimit(struct infra_cache* infra, uint8_t* name,
	size_t namelen)
{
	int labs = dname_count_labels(name);
	struct domain_limit_data* d = (struct domain_limit_data*)
		name_tree_lookup(&infra->domain_limits, name, namelen, labs,
		LDNS_RR_CLASS_IN);
	if(!d) return infra_dp_ratelimit;

	if(d->node.labs == labs && d->lim != -1)
		return d->lim; /* exact match */

	/* find 'below match' */
	if(d->node.labs == labs)
		d = (struct domain_limit_data*)d->node.parent;
	while(d) {
		if(d->below != -1)
			return d->below;
		d = (struct domain_limit_data*)d->node.parent;
	}
	return infra_dp_ratelimit;
}

size_t ip_rate_sizefunc(void* k, void* ATTR_UNUSED(d))
{
	struct ip_rate_key* key = (struct ip_rate_key*)k;
	return sizeof(*key) + sizeof(struct ip_rate_data)
		+ lock_get_mem(&key->entry.lock);
}

int ip_rate_compfunc(void* key1, void* key2)
{
	struct ip_rate_key* k1 = (struct ip_rate_key*)key1;
	struct ip_rate_key* k2 = (struct ip_rate_key*)key2;
	return sockaddr_cmp_addr(&k1->addr, k1->addrlen,
		&k2->addr, k2->addrlen);
}

void ip_rate_delkeyfunc(void* k, void* ATTR_UNUSED(arg))
{
	struct ip_rate_key* key = (struct ip_rate_key*)k;
	if(!key)
		return;
	lock_rw_destroy(&key->entry.lock);
	free(key);
}

/** find data item in array, for write access, caller unlocks */
static struct lruhash_entry* infra_find_ratedata(struct infra_cache* infra,
	uint8_t* name, size_t namelen, int wr)
{
	struct rate_key key;
	hashvalue_type h = dname_query_hash(name, 0xab);
	memset(&key, 0, sizeof(key));
	key.name = name;
	key.namelen = namelen;
	key.entry.hash = h;
	return slabhash_lookup(infra->domain_rates, h, &key, wr);
}

/** find data item in array for ip addresses */
static struct lruhash_entry* infra_find_ip_ratedata(struct infra_cache* infra,
	struct sockaddr_storage* addr, socklen_t addrlen, int wr)
{
	struct ip_rate_key key;
	hashvalue_type h = hash_addr(addr, addrlen, 0);
	memset(&key, 0, sizeof(key));
	key.addr = *addr;
	key.addrlen = addrlen;
	key.entry.hash = h;
	return slabhash_lookup(infra->client_ip_rates, h, &key, wr);
}

/** create rate data item for name, number 1 in now */
static void infra_create_ratedata(struct infra_cache* infra,
	uint8_t* name, size_t namelen, time_t timenow)
{
	hashvalue_type h = dname_query_hash(name, 0xab);
	struct rate_key* k = (struct rate_key*)calloc(1, sizeof(*k));
	struct rate_data* d = (struct rate_data*)calloc(1, sizeof(*d));
	if(!k || !d) {
		free(k);
		free(d);
		return; /* alloc failure */
	}
	k->namelen = namelen;
	k->name = memdup(name, namelen);
	if(!k->name) {
		free(k);
		free(d);
		return; /* alloc failure */
	}
	lock_rw_init(&k->entry.lock);
	k->entry.hash = h;
	k->entry.key = k;
	k->entry.data = d;
	d->qps[0] = 1;
	d->timestamp[0] = timenow;
	slabhash_insert(infra->domain_rates, h, &k->entry, d, NULL);
}

/** create rate data item for ip address */
static void infra_ip_create_ratedata(struct infra_cache* infra,
	struct sockaddr_storage* addr, socklen_t addrlen, time_t timenow,
	int mesh_wait)
{
	hashvalue_type h = hash_addr(addr, addrlen, 0);
	struct ip_rate_key* k = (struct ip_rate_key*)calloc(1, sizeof(*k));
	struct ip_rate_data* d = (struct ip_rate_data*)calloc(1, sizeof(*d));
	if(!k || !d) {
		free(k);
		free(d);
		return; /* alloc failure */
	}
	k->addr = *addr;
	k->addrlen = addrlen;
	lock_rw_init(&k->entry.lock);
	k->entry.hash = h;
	k->entry.key = k;
	k->entry.data = d;
	d->qps[0] = 1;
	d->timestamp[0] = timenow;
	d->mesh_wait = mesh_wait;
	slabhash_insert(infra->client_ip_rates, h, &k->entry, d, NULL);
}

/** Find the second and return its rate counter. If none and should_add, remove
 *  oldest to accommodate. Else return none. */
static int* infra_rate_find_second_or_none(void* data, time_t t, int should_add)
{
	struct rate_data* d = (struct rate_data*)data;
	int i, oldest;
	for(i=0; i<RATE_WINDOW; i++) {
		if(d->timestamp[i] == t)
			return &(d->qps[i]);
	}
	if(!should_add) return NULL;
	/* remove oldest timestamp, and insert it at t with 0 qps */
	oldest = 0;
	for(i=0; i<RATE_WINDOW; i++) {
		if(d->timestamp[i] < d->timestamp[oldest])
			oldest = i;
	}
	d->timestamp[oldest] = t;
	d->qps[oldest] = 0;
	return &(d->qps[oldest]);
}

/** find the second and return its rate counter, if none, remove oldest to
 *  accommodate */
static int* infra_rate_give_second(void* data, time_t t)
{
    return infra_rate_find_second_or_none(data, t, 1);
}

/** find the second and return its rate counter only if it exists. Caller
 *  should check for NULL return value */
static int* infra_rate_get_second(void* data, time_t t)
{
    return infra_rate_find_second_or_none(data, t, 0);
}

int infra_rate_max(void* data, time_t now, int backoff)
{
	struct rate_data* d = (struct rate_data*)data;
	int i, max = 0;
	for(i=0; i<RATE_WINDOW; i++) {
		if(backoff) {
			if(now-d->timestamp[i] <= RATE_WINDOW &&
				d->qps[i] > max) {
				max = d->qps[i];
			}
		} else {
			if(now == d->timestamp[i]) {
				return d->qps[i];
			}
		}
	}
	return max;
}

int infra_ratelimit_inc(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow, int backoff, struct query_info* qinfo,
	struct comm_reply* replylist)
{
	int lim, max;
	struct lruhash_entry* entry;

	if(!infra_dp_ratelimit)
		return 1; /* not enabled */

	/* find ratelimit */
	lim = infra_find_ratelimit(infra, name, namelen);
	if(!lim)
		return 1; /* disabled for this domain */
	
	/* find or insert ratedata */
	entry = infra_find_ratedata(infra, name, namelen, 1);
	if(entry) {
		int premax = infra_rate_max(entry->data, timenow, backoff);
		int* cur = infra_rate_give_second(entry->data, timenow);
		(*cur)++;
		max = infra_rate_max(entry->data, timenow, backoff);
		lock_rw_unlock(&entry->lock);

		if(premax <= lim && max > lim) {
			char buf[LDNS_MAX_DOMAINLEN], qnm[LDNS_MAX_DOMAINLEN];
			char ts[12], cs[12], ip[128];
			dname_str(name, buf);
			dname_str(qinfo->qname, qnm);
			sldns_wire2str_type_buf(qinfo->qtype, ts, sizeof(ts));
			sldns_wire2str_class_buf(qinfo->qclass, cs, sizeof(cs));
			ip[0]=0;
			if(replylist) {
				addr_to_str((struct sockaddr_storage *)&replylist->remote_addr,
					replylist->remote_addrlen, ip, sizeof(ip));
				verbose(VERB_OPS, "ratelimit exceeded %s %d query %s %s %s from %s", buf, lim, qnm, cs, ts, ip);
			} else {
				verbose(VERB_OPS, "ratelimit exceeded %s %d query %s %s %s", buf, lim, qnm, cs, ts);
			}
		}
		return (max <= lim);
	}

	/* create */
	infra_create_ratedata(infra, name, namelen, timenow);
	return (1 <= lim);
}

void infra_ratelimit_dec(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow)
{
	struct lruhash_entry* entry;
	int* cur;
	if(!infra_dp_ratelimit)
		return; /* not enabled */
	entry = infra_find_ratedata(infra, name, namelen, 1);
	if(!entry) return; /* not cached */
	cur = infra_rate_get_second(entry->data, timenow);
	if(cur == NULL) {
		/* our timenow is not available anymore; nothing to decrease */
		lock_rw_unlock(&entry->lock);
		return;
	}
	if((*cur) > 0)
		(*cur)--;
	lock_rw_unlock(&entry->lock);
}

int infra_ratelimit_exceeded(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow, int backoff)
{
	struct lruhash_entry* entry;
	int lim, max;
	if(!infra_dp_ratelimit)
		return 0; /* not enabled */

	/* find ratelimit */
	lim = infra_find_ratelimit(infra, name, namelen);
	if(!lim)
		return 0; /* disabled for this domain */

	/* find current rate */
	entry = infra_find_ratedata(infra, name, namelen, 0);
	if(!entry)
		return 0; /* not cached */
	max = infra_rate_max(entry->data, timenow, backoff);
	lock_rw_unlock(&entry->lock);

	return (max > lim);
}

size_t 
infra_get_mem(struct infra_cache* infra)
{
	size_t s = sizeof(*infra) + slabhash_get_mem(infra->hosts);
	if(infra->domain_rates) s += slabhash_get_mem(infra->domain_rates);
	if(infra->client_ip_rates) s += slabhash_get_mem(infra->client_ip_rates);
	/* ignore domain_limits because walk through tree is big */
	return s;
}

/* Returns 1 if the limit has not been exceeded, 0 otherwise. */
static int
check_ip_ratelimit(struct sockaddr_storage* addr, socklen_t addrlen,
	struct sldns_buffer* buffer, int premax, int max, int has_cookie)
{
	int limit;

	if(has_cookie) limit = infra_ip_ratelimit_cookie;
	else           limit = infra_ip_ratelimit;

	/* Disabled */
	if(limit == 0) return 1;

	if(premax <= limit && max > limit) {
		char client_ip[128], qnm[LDNS_MAX_DOMAINLEN+1+12+12];
		addr_to_str(addr, addrlen, client_ip, sizeof(client_ip));
		qnm[0]=0;
		if(sldns_buffer_limit(buffer)>LDNS_HEADER_SIZE &&
			LDNS_QDCOUNT(sldns_buffer_begin(buffer))!=0) {
			(void)sldns_wire2str_rrquestion_buf(
				sldns_buffer_at(buffer, LDNS_HEADER_SIZE),
				sldns_buffer_limit(buffer)-LDNS_HEADER_SIZE,
				qnm, sizeof(qnm));
			if(strlen(qnm)>0 && qnm[strlen(qnm)-1]=='\n')
				qnm[strlen(qnm)-1] = 0; /*remove newline*/
			if(strchr(qnm, '\t'))
				*strchr(qnm, '\t') = ' ';
			if(strchr(qnm, '\t'))
				*strchr(qnm, '\t') = ' ';
			verbose(VERB_OPS, "ip_ratelimit exceeded %s %d%s %s",
				client_ip, limit,
				has_cookie?"(cookie)":"", qnm);
		} else {
			verbose(VERB_OPS, "ip_ratelimit exceeded %s %d%s (no query name)",
				client_ip, limit,
				has_cookie?"(cookie)":"");
		}
	}
	return (max <= limit);
}

int infra_ip_ratelimit_inc(struct infra_cache* infra,
	struct sockaddr_storage* addr, socklen_t addrlen, time_t timenow,
	int has_cookie, int backoff, struct sldns_buffer* buffer)
{
	int max;
	struct lruhash_entry* entry;

	/* not enabled */
	if(!infra_ip_ratelimit) {
		return 1;
	}
	/* find or insert ratedata */
	entry = infra_find_ip_ratedata(infra, addr, addrlen, 1);
	if(entry) {
		int premax = infra_rate_max(entry->data, timenow, backoff);
		int* cur = infra_rate_give_second(entry->data, timenow);
		(*cur)++;
		max = infra_rate_max(entry->data, timenow, backoff);
		lock_rw_unlock(&entry->lock);
		return check_ip_ratelimit(addr, addrlen, buffer, premax, max,
			has_cookie);
	}

	/* create */
	infra_ip_create_ratedata(infra, addr, addrlen, timenow, 0);
	return 1;
}

int infra_wait_limit_allowed(struct infra_cache* infra, struct comm_reply* rep,
	int cookie_valid, struct config_file* cfg)
{
	struct lruhash_entry* entry;
	if(cfg->wait_limit == 0)
		return 1;

	entry = infra_find_ip_ratedata(infra, &rep->client_addr,
		rep->client_addrlen, 0);
	if(entry) {
		rbtree_type* tree;
		struct wait_limit_netblock_info* w;
		struct rate_data* d = (struct rate_data*)entry->data;
		int mesh_wait = d->mesh_wait;
		lock_rw_unlock(&entry->lock);

		/* have the wait amount, check how much is allowed */
		if(cookie_valid)
			tree = &infra->wait_limits_cookie_netblock;
		else	tree = &infra->wait_limits_netblock;
		w = (struct wait_limit_netblock_info*)addr_tree_lookup(tree,
			&rep->client_addr, rep->client_addrlen);
		if(w) {
			if(w->limit != -1 && mesh_wait > w->limit)
				return 0;
		} else {
			/* if there is no IP netblock specific information,
			 * use the configured value. */
			if(mesh_wait > (cookie_valid?cfg->wait_limit_cookie:
				cfg->wait_limit))
				return 0;
		}
	}
	return 1;
}

void infra_wait_limit_inc(struct infra_cache* infra, struct comm_reply* rep,
	time_t timenow, struct config_file* cfg)
{
	struct lruhash_entry* entry;
	if(cfg->wait_limit == 0)
		return;

	/* Find it */
	entry = infra_find_ip_ratedata(infra, &rep->client_addr,
		rep->client_addrlen, 1);
	if(entry) {
		struct rate_data* d = (struct rate_data*)entry->data;
		d->mesh_wait++;
		lock_rw_unlock(&entry->lock);
		return;
	}

	/* Create it */
	infra_ip_create_ratedata(infra, &rep->client_addr,
		rep->client_addrlen, timenow, 1);
}

void infra_wait_limit_dec(struct infra_cache* infra, struct comm_reply* rep,
	struct config_file* cfg)
{
	struct lruhash_entry* entry;
	if(cfg->wait_limit == 0)
		return;

	entry = infra_find_ip_ratedata(infra, &rep->client_addr,
		rep->client_addrlen, 1);
	if(entry) {
		struct rate_data* d = (struct rate_data*)entry->data;
		if(d->mesh_wait > 0)
			d->mesh_wait--;
		lock_rw_unlock(&entry->lock);
	}
}
