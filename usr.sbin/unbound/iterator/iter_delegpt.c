/*
 * iterator/iter_delegpt.c - delegation point with NS and address information.
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
 * This file implements the Delegation Point. It contains a list of name servers
 * and their addresses if known.
 */
#include "config.h"
#include "iterator/iter_delegpt.h"
#include "services/cache/dns.h"
#include "util/regional.h"
#include "util/data/dname.h"
#include "util/data/packed_rrset.h"
#include "util/data/msgreply.h"
#include "util/net_help.h"
#include "sldns/rrdef.h"
#include "sldns/sbuffer.h"

struct delegpt* 
delegpt_create(struct regional* region)
{
	struct delegpt* dp=(struct delegpt*)regional_alloc(
		region, sizeof(*dp));
	if(!dp)
		return NULL;
	memset(dp, 0, sizeof(*dp));
	return dp;
}

struct delegpt* delegpt_copy(struct delegpt* dp, struct regional* region)
{
	struct delegpt* copy = delegpt_create(region);
	struct delegpt_ns* ns;
	struct delegpt_addr* a;
	if(!copy) 
		return NULL;
	if(!delegpt_set_name(copy, region, dp->name))
		return NULL;
	copy->bogus = dp->bogus;
	copy->has_parent_side_NS = dp->has_parent_side_NS;
	copy->ssl_upstream = dp->ssl_upstream;
	copy->tcp_upstream = dp->tcp_upstream;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(!delegpt_add_ns(copy, region, ns->name, ns->lame,
			ns->tls_auth_name, ns->port))
			return NULL;
		copy->nslist->cache_lookup_count = ns->cache_lookup_count;
		copy->nslist->resolved = ns->resolved;
		copy->nslist->got4 = ns->got4;
		copy->nslist->got6 = ns->got6;
		copy->nslist->done_pside4 = ns->done_pside4;
		copy->nslist->done_pside6 = ns->done_pside6;
	}
	for(a = dp->target_list; a; a = a->next_target) {
		if(!delegpt_add_addr(copy, region, &a->addr, a->addrlen,
			a->bogus, a->lame, a->tls_auth_name, -1, NULL))
			return NULL;
	}
	return copy;
}

int 
delegpt_set_name(struct delegpt* dp, struct regional* region, uint8_t* name)
{
	log_assert(!dp->dp_type_mlc);
	dp->namelabs = dname_count_size_labels(name, &dp->namelen);
	dp->name = regional_alloc_init(region, name, dp->namelen);
	return dp->name != 0;
}

int 
delegpt_add_ns(struct delegpt* dp, struct regional* region, uint8_t* name,
	uint8_t lame, char* tls_auth_name, int port)
{
	struct delegpt_ns* ns;
	size_t len;
	(void)dname_count_size_labels(name, &len);
	log_assert(!dp->dp_type_mlc);
	/* slow check for duplicates to avoid counting failures when
	 * adding the same server as a dependency twice */
	if(delegpt_find_ns(dp, name, len))
		return 1;
	ns = (struct delegpt_ns*)regional_alloc(region,
		sizeof(struct delegpt_ns));
	if(!ns)
		return 0;
	ns->next = dp->nslist;
	ns->namelen = len;
	dp->nslist = ns;
	ns->name = regional_alloc_init(region, name, ns->namelen);
	ns->cache_lookup_count = 0;
	ns->resolved = 0;
	ns->got4 = 0;
	ns->got6 = 0;
	ns->lame = lame;
	ns->done_pside4 = 0;
	ns->done_pside6 = 0;
	ns->port = port;
	if(tls_auth_name) {
		ns->tls_auth_name = regional_strdup(region, tls_auth_name);
		if(!ns->tls_auth_name)
			return 0;
	} else {
		ns->tls_auth_name = NULL;
	}
	return ns->name != 0;
}

struct delegpt_ns*
delegpt_find_ns(struct delegpt* dp, uint8_t* name, size_t namelen)
{
	struct delegpt_ns* p = dp->nslist;
	while(p) {
		if(namelen == p->namelen && 
			query_dname_compare(name, p->name) == 0) {
			return p;
		}
		p = p->next;
	}
	return NULL;
}

struct delegpt_addr*
delegpt_find_addr(struct delegpt* dp, struct sockaddr_storage* addr, 
	socklen_t addrlen)
{
	struct delegpt_addr* p = dp->target_list;
	while(p) {
		if(sockaddr_cmp_addr(addr, addrlen, &p->addr, p->addrlen)==0
			&& ((struct sockaddr_in*)addr)->sin_port ==
			   ((struct sockaddr_in*)&p->addr)->sin_port) {
			return p;
		}
		p = p->next_target;
	}
	return NULL;
}

int
delegpt_add_target(struct delegpt* dp, struct regional* region,
	uint8_t* name, size_t namelen, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t bogus, uint8_t lame, int* additions)
{
	struct delegpt_ns* ns = delegpt_find_ns(dp, name, namelen);
	log_assert(!dp->dp_type_mlc);
	if(!ns) {
		/* ignore it */
		return 1;
	}
	if(!lame) {
		if(addr_is_ip6(addr, addrlen))
			ns->got6 = 1;
		else	ns->got4 = 1;
		if(ns->got4 && ns->got6)
			ns->resolved = 1;
	} else {
		if(addr_is_ip6(addr, addrlen))
			ns->done_pside6 = 1;
		else	ns->done_pside4 = 1;
	}
	log_assert(ns->port>0);
	return delegpt_add_addr(dp, region, addr, addrlen, bogus, lame,
		ns->tls_auth_name, ns->port, additions);
}

int
delegpt_add_addr(struct delegpt* dp, struct regional* region,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t bogus,
	uint8_t lame, char* tls_auth_name, int port, int* additions)
{
	struct delegpt_addr* a;
	log_assert(!dp->dp_type_mlc);
	if(port != -1) {
		log_assert(port>0);
		sockaddr_store_port(addr, addrlen, port);
	}
	/* check for duplicates */
	if((a = delegpt_find_addr(dp, addr, addrlen))) {
		if(bogus)
			a->bogus = bogus;
		if(!lame)
			a->lame = 0;
		return 1;
	}
	if(additions)
		*additions = 1;

	a = (struct delegpt_addr*)regional_alloc(region,
		sizeof(struct delegpt_addr));
	if(!a)
		return 0;
	a->next_target = dp->target_list;
	dp->target_list = a;
	a->next_result = 0;
	a->next_usable = dp->usable_list;
	dp->usable_list = a;
	memcpy(&a->addr, addr, addrlen);
	a->addrlen = addrlen;
	a->attempts = 0;
	a->bogus = bogus;
	a->lame = lame;
	a->dnsseclame = 0;
	if(tls_auth_name) {
		a->tls_auth_name = regional_strdup(region, tls_auth_name);
		if(!a->tls_auth_name)
			return 0;
	} else {
		a->tls_auth_name = NULL;
	}
	return 1;
}

void
delegpt_count_ns(struct delegpt* dp, size_t* numns, size_t* missing)
{
	struct delegpt_ns* ns;
	*numns = 0;
	*missing = 0;
	for(ns = dp->nslist; ns; ns = ns->next) {
		(*numns)++;
		if(!ns->resolved)
			(*missing)++;
	}
}

void
delegpt_count_addr(struct delegpt* dp, size_t* numaddr, size_t* numres, 
	size_t* numavail)
{
	struct delegpt_addr* a;
	*numaddr = 0;
	*numres = 0;
	*numavail = 0;
	for(a = dp->target_list; a; a = a->next_target) {
		(*numaddr)++;
	}
	for(a = dp->result_list; a; a = a->next_result) {
		(*numres)++;
	}
	for(a = dp->usable_list; a; a = a->next_usable) {
		(*numavail)++;
	}
}

void delegpt_log(enum verbosity_value v, struct delegpt* dp)
{
	char buf[LDNS_MAX_DOMAINLEN];
	struct delegpt_ns* ns;
	struct delegpt_addr* a;
	size_t missing=0, numns=0, numaddr=0, numres=0, numavail=0;
	if(verbosity < v)
		return;
	dname_str(dp->name, buf);
	if(dp->nslist == NULL && dp->target_list == NULL) {
		log_info("DelegationPoint<%s>: empty", buf);
		return;
	}
	delegpt_count_ns(dp, &numns, &missing);
	delegpt_count_addr(dp, &numaddr, &numres, &numavail);
	log_info("DelegationPoint<%s>: %u names (%u missing), "
		"%u addrs (%u result, %u avail)%s", 
		buf, (unsigned)numns, (unsigned)missing, 
		(unsigned)numaddr, (unsigned)numres, (unsigned)numavail,
		(dp->has_parent_side_NS?" parentNS":" cacheNS"));
	if(verbosity >= VERB_ALGO) {
		for(ns = dp->nslist; ns; ns = ns->next) {
			dname_str(ns->name, buf);
			log_info("  %s %s%s%s%s%s%s%s", buf, 
			(ns->resolved?"*":""),
			(ns->got4?" A":""), (ns->got6?" AAAA":""),
			(dp->bogus?" BOGUS":""), (ns->lame?" PARENTSIDE":""),
			(ns->done_pside4?" PSIDE_A":""),
			(ns->done_pside6?" PSIDE_AAAA":""));
		}
		for(a = dp->target_list; a; a = a->next_target) {
			char s[128];
			const char* str = "  ";
			if(a->bogus && a->lame) str = "  BOGUS ADDR_LAME ";
			else if(a->bogus) str = "  BOGUS ";
			else if(a->lame) str = "  ADDR_LAME ";
			if(a->tls_auth_name)
				snprintf(s, sizeof(s), "%s[%s]", str,
					a->tls_auth_name);
			else snprintf(s, sizeof(s), "%s", str);
			log_addr(VERB_ALGO, s, &a->addr, a->addrlen);
		}
	}
}

int
delegpt_addr_on_result_list(struct delegpt* dp, struct delegpt_addr* find)
{
	struct delegpt_addr* a = dp->result_list;
	while(a) {
		if(a == find)
			return 1;
		a = a->next_result;
	}
	return 0;
}

void
delegpt_usable_list_remove_addr(struct delegpt* dp, struct delegpt_addr* del)
{
	struct delegpt_addr* usa = dp->usable_list, *prev = NULL;
	while(usa) {
		if(usa == del) {
			/* snip off the usable list */
			if(prev)
				prev->next_usable = usa->next_usable;
			else	dp->usable_list = usa->next_usable;
			return;
		}
		prev = usa;
		usa = usa->next_usable;
	}
}

void
delegpt_add_to_result_list(struct delegpt* dp, struct delegpt_addr* a)
{
	if(delegpt_addr_on_result_list(dp, a))
		return;
	delegpt_usable_list_remove_addr(dp, a);
	a->next_result = dp->result_list;
	dp->result_list = a;
}

void 
delegpt_add_unused_targets(struct delegpt* dp)
{
	struct delegpt_addr* usa = dp->usable_list;
	dp->usable_list = NULL;
	while(usa) {
		usa->next_result = dp->result_list;
		dp->result_list = usa;
		usa = usa->next_usable;
	}
}

size_t
delegpt_count_targets(struct delegpt* dp)
{
	struct delegpt_addr* a;
	size_t n = 0;
	for(a = dp->target_list; a; a = a->next_target)
		n++;
	return n;
}

size_t 
delegpt_count_missing_targets(struct delegpt* dp, int* alllame)
{
	struct delegpt_ns* ns;
	size_t n = 0, nlame = 0;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->resolved) continue;
		n++;
		if(ns->lame) nlame++;
	}
	if(alllame && n == nlame) *alllame = 1;
	return n;
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

struct delegpt* 
delegpt_from_message(struct dns_msg* msg, struct regional* region)
{
	struct ub_packed_rrset_key* ns_rrset = NULL;
	struct delegpt* dp;
	size_t i;
	/* look for NS records in the authority section... */
	ns_rrset = find_NS(msg->rep, msg->rep->an_numrrsets, 
		msg->rep->an_numrrsets+msg->rep->ns_numrrsets);

	/* In some cases (even legitimate, perfectly legal cases), the 
	 * NS set for the "referral" might be in the answer section. */
	if(!ns_rrset)
		ns_rrset = find_NS(msg->rep, 0, msg->rep->an_numrrsets);
	
	/* If there was no NS rrset in the authority section, then this 
	 * wasn't a referral message. (It might not actually be a 
	 * referral message anyway) */
	if(!ns_rrset)
		return NULL;
	
	/* If we found any, then Yay! we have a delegation point. */
	dp = delegpt_create(region);
	if(!dp)
		return NULL;
	dp->has_parent_side_NS = 1; /* created from message */
	if(!delegpt_set_name(dp, region, ns_rrset->rk.dname))
		return NULL;
	if(!delegpt_rrset_add_ns(dp, region, ns_rrset, 0))
		return NULL;

	/* add glue, A and AAAA in answer and additional section */
	for(i=0; i<msg->rep->rrset_count; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		/* skip auth section. FIXME really needed?*/
		if(msg->rep->an_numrrsets <= i && 
			i < (msg->rep->an_numrrsets+msg->rep->ns_numrrsets))
			continue;

		if(ntohs(s->rk.type) == LDNS_RR_TYPE_A) {
			if(!delegpt_add_rrset_A(dp, region, s, 0, NULL))
				return NULL;
		} else if(ntohs(s->rk.type) == LDNS_RR_TYPE_AAAA) {
			if(!delegpt_add_rrset_AAAA(dp, region, s, 0, NULL))
				return NULL;
		}
	}
	return dp;
}

int 
delegpt_rrset_add_ns(struct delegpt* dp, struct regional* region,
        struct ub_packed_rrset_key* ns_rrset, uint8_t lame)
{
	struct packed_rrset_data* nsdata = (struct packed_rrset_data*)
		ns_rrset->entry.data;
	size_t i;
	log_assert(!dp->dp_type_mlc);
	if(nsdata->security == sec_status_bogus)
		dp->bogus = 1;
	for(i=0; i<nsdata->count; i++) {
		if(nsdata->rr_len[i] < 2+1) continue; /* len + root label */
		if(dname_valid(nsdata->rr_data[i]+2, nsdata->rr_len[i]-2) !=
			(size_t)sldns_read_uint16(nsdata->rr_data[i]))
			continue; /* bad format */
		/* add rdata of NS (= wirefmt dname), skip rdatalen bytes */
		if(!delegpt_add_ns(dp, region, nsdata->rr_data[i]+2, lame,
			NULL, UNBOUND_DNS_PORT))
			return 0;
	}
	return 1;
}

int 
delegpt_add_rrset_A(struct delegpt* dp, struct regional* region,
	struct ub_packed_rrset_key* ak, uint8_t lame, int* additions)
{
        struct packed_rrset_data* d=(struct packed_rrset_data*)ak->entry.data;
        size_t i;
        struct sockaddr_in sa;
        socklen_t len = (socklen_t)sizeof(sa);
	log_assert(!dp->dp_type_mlc);
        memset(&sa, 0, len);
        sa.sin_family = AF_INET;
        for(i=0; i<d->count; i++) {
                if(d->rr_len[i] != 2 + INET_SIZE)
                        continue;
                memmove(&sa.sin_addr, d->rr_data[i]+2, INET_SIZE);
                if(!delegpt_add_target(dp, region, ak->rk.dname,
                        ak->rk.dname_len, (struct sockaddr_storage*)&sa,
                        len, (d->security==sec_status_bogus), lame, additions))
                        return 0;
        }
        return 1;
}

int 
delegpt_add_rrset_AAAA(struct delegpt* dp, struct regional* region,
	struct ub_packed_rrset_key* ak, uint8_t lame, int* additions)
{
        struct packed_rrset_data* d=(struct packed_rrset_data*)ak->entry.data;
        size_t i;
        struct sockaddr_in6 sa;
        socklen_t len = (socklen_t)sizeof(sa);
	log_assert(!dp->dp_type_mlc);
        memset(&sa, 0, len);
        sa.sin6_family = AF_INET6;
        for(i=0; i<d->count; i++) {
                if(d->rr_len[i] != 2 + INET6_SIZE) /* rdatalen + len of IP6 */
                        continue;
                memmove(&sa.sin6_addr, d->rr_data[i]+2, INET6_SIZE);
                if(!delegpt_add_target(dp, region, ak->rk.dname,
                        ak->rk.dname_len, (struct sockaddr_storage*)&sa,
                        len, (d->security==sec_status_bogus), lame, additions))
                        return 0;
        }
        return 1;
}

int 
delegpt_add_rrset(struct delegpt* dp, struct regional* region,
        struct ub_packed_rrset_key* rrset, uint8_t lame, int* additions)
{
	if(!rrset)
		return 1;
	if(ntohs(rrset->rk.type) == LDNS_RR_TYPE_NS)
		return delegpt_rrset_add_ns(dp, region, rrset, lame);
	else if(ntohs(rrset->rk.type) == LDNS_RR_TYPE_A)
		return delegpt_add_rrset_A(dp, region, rrset, lame, additions);
	else if(ntohs(rrset->rk.type) == LDNS_RR_TYPE_AAAA)
		return delegpt_add_rrset_AAAA(dp, region, rrset, lame,
			additions);
	log_warn("Unknown rrset type added to delegpt");
	return 1;
}

void delegpt_mark_neg(struct delegpt_ns* ns, uint16_t qtype)
{
	if(ns) {
		if(qtype == LDNS_RR_TYPE_A)
			ns->got4 = 2;
		else if(qtype == LDNS_RR_TYPE_AAAA)
			ns->got6 = 2;
		if(ns->got4 && ns->got6)
			ns->resolved = 1;
	}
}

void delegpt_add_neg_msg(struct delegpt* dp, struct msgreply_entry* msg)
{
	struct reply_info* rep = (struct reply_info*)msg->entry.data;
	if(!rep) return;

	/* if error or no answers */
	if(FLAGS_GET_RCODE(rep->flags) != 0 || rep->an_numrrsets == 0) {
		struct delegpt_ns* ns = delegpt_find_ns(dp, msg->key.qname, 
			msg->key.qname_len);
		delegpt_mark_neg(ns, msg->key.qtype);
	}
}

void delegpt_no_ipv6(struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		/* no ipv6, so only ipv4 is enough to resolve a nameserver */
		if(ns->got4)
			ns->resolved = 1;
	}
}

void delegpt_no_ipv4(struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		/* no ipv4, so only ipv6 is enough to resolve a nameserver */
		if(ns->got6)
			ns->resolved = 1;
	}
}

struct delegpt* delegpt_create_mlc(uint8_t* name)
{
	struct delegpt* dp=(struct delegpt*)calloc(1, sizeof(*dp));
	if(!dp)
		return NULL;
	dp->dp_type_mlc = 1;
	if(name) {
		dp->namelabs = dname_count_size_labels(name, &dp->namelen);
		dp->name = memdup(name, dp->namelen);
		if(!dp->name) {
			free(dp);
			return NULL;
		}
	}
	return dp;
}

void delegpt_free_mlc(struct delegpt* dp)
{
	struct delegpt_ns* n, *nn;
	struct delegpt_addr* a, *na;
	if(!dp) return;
	log_assert(dp->dp_type_mlc);
	n = dp->nslist;
	while(n) {
		nn = n->next;
		free(n->name);
		free(n->tls_auth_name);
		free(n);
		n = nn;
	}
	a = dp->target_list;
	while(a) {
		na = a->next_target;
		free(a->tls_auth_name);
		free(a);
		a = na;
	}
	free(dp->name);
	free(dp);
}

int delegpt_set_name_mlc(struct delegpt* dp, uint8_t* name)
{
	log_assert(dp->dp_type_mlc);
	dp->namelabs = dname_count_size_labels(name, &dp->namelen);
	dp->name = memdup(name, dp->namelen);
	return (dp->name != NULL);
}

int delegpt_add_ns_mlc(struct delegpt* dp, uint8_t* name, uint8_t lame,
	char* tls_auth_name, int port)
{
	struct delegpt_ns* ns;
	size_t len;
	(void)dname_count_size_labels(name, &len);
	log_assert(dp->dp_type_mlc);
	/* slow check for duplicates to avoid counting failures when
	 * adding the same server as a dependency twice */
	if(delegpt_find_ns(dp, name, len))
		return 1;
	ns = (struct delegpt_ns*)malloc(sizeof(struct delegpt_ns));
	if(!ns)
		return 0;
	ns->namelen = len;
	ns->name = memdup(name, ns->namelen);
	if(!ns->name) {
		free(ns);
		return 0;
	}
	ns->next = dp->nslist;
	dp->nslist = ns;
	ns->cache_lookup_count = 0;
	ns->resolved = 0;
	ns->got4 = 0;
	ns->got6 = 0;
	ns->lame = (uint8_t)lame;
	ns->done_pside4 = 0;
	ns->done_pside6 = 0;
	ns->port = port;
	if(tls_auth_name) {
		ns->tls_auth_name = strdup(tls_auth_name);
		if(!ns->tls_auth_name) {
			free(ns->name);
			free(ns);
			return 0;
		}
	} else {
		ns->tls_auth_name = NULL;
	}
	return 1;
}

int delegpt_add_addr_mlc(struct delegpt* dp, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t bogus, uint8_t lame, char* tls_auth_name,
	int port)
{
	struct delegpt_addr* a;
	log_assert(dp->dp_type_mlc);
	if(port != -1) {
		log_assert(port>0);
		sockaddr_store_port(addr, addrlen, port);
	}
	/* check for duplicates */
	if((a = delegpt_find_addr(dp, addr, addrlen))) {
		if(bogus)
			a->bogus = bogus;
		if(!lame)
			a->lame = 0;
		return 1;
	}

	a = (struct delegpt_addr*)malloc(sizeof(struct delegpt_addr));
	if(!a)
		return 0;
	a->next_target = dp->target_list;
	dp->target_list = a;
	a->next_result = 0;
	a->next_usable = dp->usable_list;
	dp->usable_list = a;
	memcpy(&a->addr, addr, addrlen);
	a->addrlen = addrlen;
	a->attempts = 0;
	a->bogus = bogus;
	a->lame = lame;
	a->dnsseclame = 0;
	if(tls_auth_name) {
		a->tls_auth_name = strdup(tls_auth_name);
		if(!a->tls_auth_name) {
			free(a);
			return 0;
		}
	} else {
		a->tls_auth_name = NULL;
	}
	return 1;
}

int delegpt_add_target_mlc(struct delegpt* dp, uint8_t* name, size_t namelen,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t bogus,
	uint8_t lame)
{
	struct delegpt_ns* ns = delegpt_find_ns(dp, name, namelen);
	log_assert(dp->dp_type_mlc);
	if(!ns) {
		/* ignore it */
		return 1;
	}
	if(!lame) {
		if(addr_is_ip6(addr, addrlen))
			ns->got6 = 1;
		else	ns->got4 = 1;
		if(ns->got4 && ns->got6)
			ns->resolved = 1;
	} else {
		if(addr_is_ip6(addr, addrlen))
			ns->done_pside6 = 1;
		else	ns->done_pside4 = 1;
	}
	log_assert(ns->port>0);
	return delegpt_add_addr_mlc(dp, addr, addrlen, bogus, lame,
		ns->tls_auth_name, ns->port);
}

size_t delegpt_get_mem(struct delegpt* dp)
{
	struct delegpt_ns* ns;
	size_t s;
	if(!dp) return 0;
	s = sizeof(*dp) + dp->namelen +
		delegpt_count_targets(dp)*sizeof(struct delegpt_addr);
	for(ns=dp->nslist; ns; ns=ns->next)
		s += sizeof(*ns)+ns->namelen;
	return s;
}
