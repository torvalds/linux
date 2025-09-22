/*
 * iterator/iter_utils.c - iterative resolver module utility functions.
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
 * This file contains functions to assist the iterator module.
 * Configuration options. Forward zones.
 */
#include "config.h"
#include "iterator/iter_utils.h"
#include "iterator/iterator.h"
#include "iterator/iter_hints.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_donotq.h"
#include "iterator/iter_delegpt.h"
#include "iterator/iter_priv.h"
#include "services/cache/infra.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "services/outside_network.h"
#include "util/net_help.h"
#include "util/module.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/regional.h"
#include "util/data/msgparse.h"
#include "util/data/dname.h"
#include "util/random.h"
#include "util/fptr_wlist.h"
#include "validator/val_anchor.h"
#include "validator/val_kcache.h"
#include "validator/val_kentry.h"
#include "validator/val_utils.h"
#include "validator/val_sigcrypt.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"

/** time when nameserver glue is said to be 'recent' */
#define SUSPICION_RECENT_EXPIRY 86400

/** if NAT64 is enabled and no NAT64 prefix is configured, first fall back to
 * DNS64 prefix.  If that is not configured, fall back to this default value.
 */
static const char DEFAULT_NAT64_PREFIX[] = "64:ff9b::/96";

/** fillup fetch policy array */
static int
fetch_fill(int* target_fetch_policy, int max_dependency_depth, const char* str)
{
	char* s = (char*)str, *e;
	int i;
	for(i=0; i<max_dependency_depth+1; i++) {
		target_fetch_policy[i] = strtol(s, &e, 10);
		if(s == e) {
			log_err("cannot parse fetch policy number %s", s);
			return 0;
		}
		s = e;
	}
	return 1;
}

/** Read config string that represents the target fetch policy */
int
read_fetch_policy(int** target_fetch_policy, int* max_dependency_depth,
	const char* str)
{
	int count = cfg_count_numbers(str);
	if(count < 1) {
		log_err("Cannot parse target fetch policy: \"%s\"", str);
		return 0;
	}
	*max_dependency_depth = count - 1;
	*target_fetch_policy = (int*)calloc(
		(size_t)(*max_dependency_depth)+1, sizeof(int));
	if(!*target_fetch_policy) {
		log_err("alloc fetch policy: out of memory");
		return 0;
	}
	if(!fetch_fill(*target_fetch_policy, *max_dependency_depth, str))
		return 0;
	return 1;
}

struct rbtree_type*
caps_white_create(void)
{
	struct rbtree_type* caps_white = rbtree_create(name_tree_compare);
	if(!caps_white)
		log_err("out of memory");
	return caps_white;
}

/** delete caps_whitelist element */
static void
caps_free(struct rbnode_type* n, void* ATTR_UNUSED(d))
{
	if(n) {
		free(((struct name_tree_node*)n)->name);
		free(n);
	}
}

void
caps_white_delete(struct rbtree_type* caps_white)
{
	if(!caps_white)
		return;
	traverse_postorder(caps_white, caps_free, NULL);
	free(caps_white);
}

int
caps_white_apply_cfg(rbtree_type* ntree, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p=cfg->caps_whitelist; p; p=p->next) {
		struct name_tree_node* n;
		size_t len;
		uint8_t* nm = sldns_str2wire_dname(p->str, &len);
		if(!nm) {
			log_err("could not parse %s", p->str);
			return 0;
		}
		n = (struct name_tree_node*)calloc(1, sizeof(*n));
		if(!n) {
			log_err("out of memory");
			free(nm);
			return 0;
		}
		n->node.key = n;
		n->name = nm;
		n->len = len;
		n->labs = dname_count_labels(nm);
		n->dclass = LDNS_RR_CLASS_IN;
		if(!name_tree_insert(ntree, n, nm, len, n->labs, n->dclass)) {
			/* duplicate element ignored, idempotent */
			free(n->name);
			free(n);
		}
	}
	name_tree_init_parents(ntree);
	return 1;
}

int
nat64_apply_cfg(struct iter_nat64* nat64, struct config_file* cfg)
{
	const char *nat64_prefix;

	nat64_prefix = cfg->nat64_prefix;
	if(!nat64_prefix)
		nat64_prefix = cfg->dns64_prefix;
	if(!nat64_prefix)
		nat64_prefix = DEFAULT_NAT64_PREFIX;
	if(!netblockstrtoaddr(nat64_prefix, 0, &nat64->nat64_prefix_addr,
		&nat64->nat64_prefix_addrlen, &nat64->nat64_prefix_net)) {
		log_err("cannot parse nat64-prefix netblock: %s", nat64_prefix);
		return 0;
	}
	if(!addr_is_ip6(&nat64->nat64_prefix_addr,
		nat64->nat64_prefix_addrlen)) {
		log_err("nat64-prefix is not IPv6: %s", cfg->nat64_prefix);
		return 0;
	}
	if(!prefixnet_is_nat64(nat64->nat64_prefix_net)) {
		log_err("nat64-prefix length it not 32, 40, 48, 56, 64 or 96: %s",
			nat64_prefix);
		return 0;
	}
	nat64->use_nat64 = cfg->do_nat64;
	return 1;
}

int
iter_apply_cfg(struct iter_env* iter_env, struct config_file* cfg)
{
	int i;
	/* target fetch policy */
	if(!read_fetch_policy(&iter_env->target_fetch_policy,
		&iter_env->max_dependency_depth, cfg->target_fetch_policy))
		return 0;
	for(i=0; i<iter_env->max_dependency_depth+1; i++)
		verbose(VERB_QUERY, "target fetch policy for level %d is %d",
			i, iter_env->target_fetch_policy[i]);

	if(!iter_env->donotq)
		iter_env->donotq = donotq_create();
	if(!iter_env->donotq || !donotq_apply_cfg(iter_env->donotq, cfg)) {
		log_err("Could not set donotqueryaddresses");
		return 0;
	}
	if(!iter_env->priv)
		iter_env->priv = priv_create();
	if(!iter_env->priv || !priv_apply_cfg(iter_env->priv, cfg)) {
		log_err("Could not set private addresses");
		return 0;
	}
	if(cfg->caps_whitelist) {
		if(!iter_env->caps_white)
			iter_env->caps_white = caps_white_create();
		if(!iter_env->caps_white || !caps_white_apply_cfg(
			iter_env->caps_white, cfg)) {
			log_err("Could not set capsforid whitelist");
			return 0;
		}

	}

	if(!nat64_apply_cfg(&iter_env->nat64, cfg)) {
		log_err("Could not setup nat64");
		return 0;
	}

	iter_env->supports_ipv6 = cfg->do_ip6;
	iter_env->supports_ipv4 = cfg->do_ip4;
	iter_env->outbound_msg_retry = cfg->outbound_msg_retry;
	iter_env->max_sent_count = cfg->max_sent_count;
	iter_env->max_query_restarts = cfg->max_query_restarts;
	return 1;
}

/** filter out unsuitable targets
 * @param iter_env: iterator environment with ipv6-support flag.
 * @param env: module environment with infra cache.
 * @param name: zone name
 * @param namelen: length of name
 * @param qtype: query type (host order).
 * @param now: current time
 * @param a: address in delegation point we are examining.
 * @return an integer that signals the target suitability.
 *	as follows:
 *	-1: The address should be omitted from the list.
 *	    Because:
 *		o The address is bogus (DNSSEC validation failure).
 *		o Listed as donotquery
 *		o is ipv6 but no ipv6 support (in operating system).
 *		o is ipv4 but no ipv4 support (in operating system).
 *		o is lame
 *	Otherwise, an rtt in milliseconds.
 *	0 .. USEFUL_SERVER_TOP_TIMEOUT-1
 *		The roundtrip time timeout estimate. less than 2 minutes.
 *		Note that util/rtt.c has a MIN_TIMEOUT of 50 msec, thus
 *		values 0 .. 49 are not used, unless that is changed.
 *	USEFUL_SERVER_TOP_TIMEOUT
 *		This value exactly is given for unresponsive blacklisted.
 *	USEFUL_SERVER_TOP_TIMEOUT+1
 *		For non-blacklisted servers: huge timeout, but has traffic.
 *	USEFUL_SERVER_TOP_TIMEOUT*1 ..
 *		parent-side lame servers get this penalty. A dispreferential
 *		server. (lame in delegpt).
 *	USEFUL_SERVER_TOP_TIMEOUT*2 ..
 *		dnsseclame servers get penalty
 *	USEFUL_SERVER_TOP_TIMEOUT*3 ..
 *		recursion lame servers get penalty
 *	UNKNOWN_SERVER_NICENESS
 *		If no information is known about the server, this is
 *		returned. 376 msec or so.
 *	+BLACKLIST_PENALTY (of USEFUL_TOP_TIMEOUT*4) for dnssec failed IPs.
 *
 * When a final value is chosen that is dnsseclame ; dnsseclameness checking
 * is turned off (so we do not discard the reply).
 * When a final value is chosen that is recursionlame; RD bit is set on query.
 * Because of the numbers this means recursionlame also have dnssec lameness
 * checking turned off.
 */
static int
iter_filter_unsuitable(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now,
	struct delegpt_addr* a)
{
	int rtt, lame, reclame, dnsseclame;
	if(a->bogus)
		return -1; /* address of server is bogus */
	if(donotq_lookup(iter_env->donotq, &a->addr, a->addrlen)) {
		log_addr(VERB_ALGO, "skip addr on the donotquery list",
			&a->addr, a->addrlen);
		return -1; /* server is on the donotquery list */
	}
	if(!iter_env->supports_ipv6 && addr_is_ip6(&a->addr, a->addrlen)) {
		return -1; /* there is no ip6 available */
	}
	if(!iter_env->supports_ipv4 && !iter_env->nat64.use_nat64 &&
	   !addr_is_ip6(&a->addr, a->addrlen)) {
		return -1; /* there is no ip4 available */
	}
	/* check lameness - need zone , class info */
	if(infra_get_lame_rtt(env->infra_cache, &a->addr, a->addrlen,
		name, namelen, qtype, &lame, &dnsseclame, &reclame,
		&rtt, now)) {
		log_addr(VERB_ALGO, "servselect", &a->addr, a->addrlen);
		verbose(VERB_ALGO, "   rtt=%d%s%s%s%s%s", rtt,
			lame?" LAME":"",
			dnsseclame?" DNSSEC_LAME":"",
			a->dnsseclame?" ADDR_DNSSEC_LAME":"",
			reclame?" REC_LAME":"",
			a->lame?" ADDR_LAME":"");
		if(lame)
			return -1; /* server is lame */
		else if(rtt >= USEFUL_SERVER_TOP_TIMEOUT)
			/* server is unresponsive,
			 * we used to return TOP_TIMEOUT, but fairly useless,
			 * because if == TOP_TIMEOUT is dropped because
			 * blacklisted later, instead, remove it here, so
			 * other choices (that are not blacklisted) can be
			 * tried */
			return -1;
		/* select remainder from worst to best */
		else if(reclame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT*3; /* nonpref */
		else if(dnsseclame || a->dnsseclame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT*2; /* nonpref */
		else if(a->lame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT+1; /* nonpref */
		else	return rtt;
	}
	/* no server information present */
	if(a->dnsseclame)
		return UNKNOWN_SERVER_NICENESS+USEFUL_SERVER_TOP_TIMEOUT*2; /* nonpref */
	else if(a->lame)
		return USEFUL_SERVER_TOP_TIMEOUT+1+UNKNOWN_SERVER_NICENESS; /* nonpref */
	return UNKNOWN_SERVER_NICENESS;
}

/** lookup RTT information, and also store fastest rtt (if any) */
static int
iter_fill_rtt(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now,
	struct delegpt* dp, int* best_rtt, struct sock_list* blacklist,
	size_t* num_suitable_results)
{
	int got_it = 0;
	struct delegpt_addr* a;
	*num_suitable_results = 0;

	if(dp->bogus)
		return 0; /* NS bogus, all bogus, nothing found */
	for(a=dp->result_list; a; a = a->next_result) {
		a->sel_rtt = iter_filter_unsuitable(iter_env, env,
			name, namelen, qtype, now, a);
		if(a->sel_rtt != -1) {
			if(sock_list_find(blacklist, &a->addr, a->addrlen))
				a->sel_rtt += BLACKLIST_PENALTY;

			if(!got_it) {
				*best_rtt = a->sel_rtt;
				got_it = 1;
			} else if(a->sel_rtt < *best_rtt) {
				*best_rtt = a->sel_rtt;
			}
			(*num_suitable_results)++;
		}
	}
	return got_it;
}

/** compare two rtts, return -1, 0 or 1 */
static int
rtt_compare(const void* x, const void* y)
{
	if(*(int*)x == *(int*)y)
		return 0;
	if(*(int*)x > *(int*)y)
		return 1;
	return -1;
}

/** get RTT for the Nth fastest server */
static int
nth_rtt(struct delegpt_addr* result_list, size_t num_results, size_t n)
{
	int rtt_band;
	size_t i;
	int* rtt_list, *rtt_index;

	if(num_results < 1 || n >= num_results) {
		return -1;
	}

	rtt_list = calloc(num_results, sizeof(int));
	if(!rtt_list) {
		log_err("malloc failure: allocating rtt_list");
		return -1;
	}
	rtt_index = rtt_list;

	for(i=0; i<num_results && result_list; i++) {
		if(result_list->sel_rtt != -1) {
			*rtt_index = result_list->sel_rtt;
			rtt_index++;
		}
		result_list=result_list->next_result;
	}
	qsort(rtt_list, num_results, sizeof(*rtt_list), rtt_compare);

	log_assert(n > 0);
	rtt_band = rtt_list[n-1];
	free(rtt_list);

	return rtt_band;
}

/** filter the address list, putting best targets at front,
 * returns number of best targets (or 0, no suitable targets) */
static int
iter_filter_order(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now,
	struct delegpt* dp, int* selected_rtt, int open_target,
	struct sock_list* blacklist, time_t prefetch)
{
	int got_num = 0, low_rtt = 0, swap_to_front, rtt_band = RTT_BAND, nth;
	int alllame = 0;
	size_t num_results;
	struct delegpt_addr* a, *n, *prev=NULL;

	/* fillup sel_rtt and find best rtt in the bunch */
	got_num = iter_fill_rtt(iter_env, env, name, namelen, qtype, now, dp,
		&low_rtt, blacklist, &num_results);
	if(got_num == 0)
		return 0;
	if(low_rtt >= USEFUL_SERVER_TOP_TIMEOUT &&
		/* If all missing (or not fully resolved) targets are lame,
		 * then use the remaining lame address. */
		((delegpt_count_missing_targets(dp, &alllame) > 0 && !alllame) ||
		open_target > 0)) {
		verbose(VERB_ALGO, "Bad choices, trying to get more choice");
		return 0; /* we want more choice. The best choice is a bad one.
			     return 0 to force the caller to fetch more */
	}

	if(env->cfg->fast_server_permil != 0 && prefetch == 0 &&
		num_results > env->cfg->fast_server_num &&
		ub_random_max(env->rnd, 1000) < env->cfg->fast_server_permil) {
		/* the query is not prefetch, but for a downstream client,
		 * there are more servers available then the fastest N we want
		 * to choose from. Limit our choice to the fastest servers. */
		nth = nth_rtt(dp->result_list, num_results,
			env->cfg->fast_server_num);
		if(nth > 0) {
			rtt_band = nth - low_rtt;
			if(rtt_band > RTT_BAND)
				rtt_band = RTT_BAND;
		}
	}

	got_num = 0;
	a = dp->result_list;
	while(a) {
		/* skip unsuitable targets */
		if(a->sel_rtt == -1) {
			prev = a;
			a = a->next_result;
			continue;
		}
		/* classify the server address and determine what to do */
		swap_to_front = 0;
		if(a->sel_rtt >= low_rtt && a->sel_rtt - low_rtt <= rtt_band) {
			got_num++;
			swap_to_front = 1;
		} else if(a->sel_rtt<low_rtt && low_rtt-a->sel_rtt<=rtt_band) {
			got_num++;
			swap_to_front = 1;
		}
		/* swap to front if necessary, or move to next result */
		if(swap_to_front && prev) {
			n = a->next_result;
			prev->next_result = n;
			a->next_result = dp->result_list;
			dp->result_list = a;
			a = n;
		} else {
			prev = a;
			a = a->next_result;
		}
	}
	*selected_rtt = low_rtt;

	if (env->cfg->prefer_ip6) {
		int got_num6 = 0;
		int low_rtt6 = 0;
		int i;
		int attempt = -1; /* filter to make sure addresses have
		  less attempts on them than the first, to force round
		  robin when all the IPv6 addresses fail */
		int num4ok = 0; /* number ip4 at low attempt count */
		int num4_lowrtt = 0;
		prev = NULL;
		a = dp->result_list;
		for(i = 0; i < got_num; i++) {
			if(!a) break; /* robustness */
			swap_to_front = 0;
			if(a->addr.ss_family != AF_INET6 && attempt == -1) {
				/* if we only have ip4 at low attempt count,
				 * then ip6 is failing, and we need to
				 * select one of the remaining IPv4 addrs */
				attempt = a->attempts;
				num4ok++;
				num4_lowrtt = a->sel_rtt;
			} else if(a->addr.ss_family != AF_INET6 && attempt == a->attempts) {
				num4ok++;
				if(num4_lowrtt == 0 || a->sel_rtt < num4_lowrtt) {
					num4_lowrtt = a->sel_rtt;
				}
			}
			if(a->addr.ss_family == AF_INET6) {
				if(attempt == -1) {
					attempt = a->attempts;
				} else if(a->attempts > attempt) {
					break;
				}
				got_num6++;
				swap_to_front = 1;
				if(low_rtt6 == 0 || a->sel_rtt < low_rtt6) {
					low_rtt6 = a->sel_rtt;
				}
			}
			/* swap to front if IPv6, or move to next result */
			if(swap_to_front && prev) {
				n = a->next_result;
				prev->next_result = n;
				a->next_result = dp->result_list;
				dp->result_list = a;
				a = n;
			} else {
				prev = a;
				a = a->next_result;
			}
		}
		if(got_num6 > 0) {
			got_num = got_num6;
			*selected_rtt = low_rtt6;
		} else if(num4ok > 0) {
			got_num = num4ok;
			*selected_rtt = num4_lowrtt;
		}
	} else if (env->cfg->prefer_ip4) {
		int got_num4 = 0;
		int low_rtt4 = 0;
		int i;
		int attempt = -1; /* filter to make sure addresses have
		  less attempts on them than the first, to force round
		  robin when all the IPv4 addresses fail */
		int num6ok = 0; /* number ip6 at low attempt count */
		int num6_lowrtt = 0;
		prev = NULL;
		a = dp->result_list;
		for(i = 0; i < got_num; i++) {
			if(!a) break; /* robustness */
			swap_to_front = 0;
			if(a->addr.ss_family != AF_INET && attempt == -1) {
				/* if we only have ip6 at low attempt count,
				 * then ip4 is failing, and we need to
				 * select one of the remaining IPv6 addrs */
				attempt = a->attempts;
				num6ok++;
				num6_lowrtt = a->sel_rtt;
			} else if(a->addr.ss_family != AF_INET && attempt == a->attempts) {
				num6ok++;
				if(num6_lowrtt == 0 || a->sel_rtt < num6_lowrtt) {
					num6_lowrtt = a->sel_rtt;
				}
			}
			if(a->addr.ss_family == AF_INET) {
				if(attempt == -1) {
					attempt = a->attempts;
				} else if(a->attempts > attempt) {
					break;
				}
				got_num4++;
				swap_to_front = 1;
				if(low_rtt4 == 0 || a->sel_rtt < low_rtt4) {
					low_rtt4 = a->sel_rtt;
				}
			}
			/* swap to front if IPv4, or move to next result */
			if(swap_to_front && prev) {
				n = a->next_result;
				prev->next_result = n;
				a->next_result = dp->result_list;
				dp->result_list = a;
				a = n;
			} else {
				prev = a;
				a = a->next_result;
			}
		}
		if(got_num4 > 0) {
			got_num = got_num4;
			*selected_rtt = low_rtt4;
		} else if(num6ok > 0) {
			got_num = num6ok;
			*selected_rtt = num6_lowrtt;
		}
	}
	return got_num;
}

struct delegpt_addr*
iter_server_selection(struct iter_env* iter_env,
	struct module_env* env, struct delegpt* dp,
	uint8_t* name, size_t namelen, uint16_t qtype, int* dnssec_lame,
	int* chase_to_rd, int open_target, struct sock_list* blacklist,
	time_t prefetch)
{
	int sel;
	int selrtt;
	struct delegpt_addr* a, *prev;
	int num = iter_filter_order(iter_env, env, name, namelen, qtype,
		*env->now, dp, &selrtt, open_target, blacklist, prefetch);

	if(num == 0)
		return NULL;
	verbose(VERB_ALGO, "selrtt %d", selrtt);
	if(selrtt > BLACKLIST_PENALTY) {
		if(selrtt-BLACKLIST_PENALTY > USEFUL_SERVER_TOP_TIMEOUT*3) {
			verbose(VERB_ALGO, "chase to "
				"blacklisted recursion lame server");
			*chase_to_rd = 1;
		}
		if(selrtt-BLACKLIST_PENALTY > USEFUL_SERVER_TOP_TIMEOUT*2) {
			verbose(VERB_ALGO, "chase to "
				"blacklisted dnssec lame server");
			*dnssec_lame = 1;
		}
	} else {
		if(selrtt > USEFUL_SERVER_TOP_TIMEOUT*3) {
			verbose(VERB_ALGO, "chase to recursion lame server");
			*chase_to_rd = 1;
		}
		if(selrtt > USEFUL_SERVER_TOP_TIMEOUT*2) {
			verbose(VERB_ALGO, "chase to dnssec lame server");
			*dnssec_lame = 1;
		}
		if(selrtt == USEFUL_SERVER_TOP_TIMEOUT) {
			verbose(VERB_ALGO, "chase to blacklisted lame server");
			return NULL;
		}
	}

	if(num == 1) {
		a = dp->result_list;
		if(++a->attempts < iter_env->outbound_msg_retry)
			return a;
		dp->result_list = a->next_result;
		return a;
	}

	/* randomly select a target from the list */
	log_assert(num > 1);
	/* grab secure random number, to pick unexpected server.
	 * also we need it to be threadsafe. */
	sel = ub_random_max(env->rnd, num);
	a = dp->result_list;
	prev = NULL;
	while(sel > 0 && a) {
		prev = a;
		a = a->next_result;
		sel--;
	}
	if(!a)  /* robustness */
		return NULL;
	if(++a->attempts < iter_env->outbound_msg_retry)
		return a;
	/* remove it from the delegation point result list */
	if(prev)
		prev->next_result = a->next_result;
	else	dp->result_list = a->next_result;
	return a;
}

struct dns_msg*
dns_alloc_msg(sldns_buffer* pkt, struct msg_parse* msg,
	struct regional* region)
{
	struct dns_msg* m = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!m)
		return NULL;
	memset(m, 0, sizeof(*m));
	if(!parse_create_msg(pkt, msg, NULL, &m->qinfo, &m->rep, region)) {
		log_err("malloc failure: allocating incoming dns_msg");
		return NULL;
	}
	return m;
}

struct dns_msg*
dns_copy_msg(struct dns_msg* from, struct regional* region)
{
	struct dns_msg* m = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!m)
		return NULL;
	m->qinfo = from->qinfo;
	if(!(m->qinfo.qname = regional_alloc_init(region, from->qinfo.qname,
		from->qinfo.qname_len)))
		return NULL;
	if(!(m->rep = reply_info_copy(from->rep, NULL, region)))
		return NULL;
	return m;
}

void
iter_dns_store(struct module_env* env, struct query_info* msgqinf,
	struct reply_info* msgrep, int is_referral, time_t leeway, int pside,
	struct regional* region, uint16_t flags, time_t qstarttime,
	int is_valrec)
{
	if(!dns_cache_store(env, msgqinf, msgrep, is_referral, leeway,
		pside, region, flags, qstarttime, is_valrec))
		log_err("out of memory: cannot store data in cache");
}

int
iter_ns_probability(struct ub_randstate* rnd, int n, int m)
{
	int sel;
	if(n == m) /* 100% chance */
		return 1;
	/* we do not need secure random numbers here, but
	 * we do need it to be threadsafe, so we use this */
	sel = ub_random_max(rnd, m);
	return (sel < n);
}

/** detect dependency cycle for query and target */
static int
causes_cycle(struct module_qstate* qstate, uint8_t* name, size_t namelen,
	uint16_t t, uint16_t c)
{
	struct query_info qinf;
	qinf.qname = name;
	qinf.qname_len = namelen;
	qinf.qtype = t;
	qinf.qclass = c;
	qinf.local_alias = NULL;
	fptr_ok(fptr_whitelist_modenv_detect_cycle(
		qstate->env->detect_cycle));
	return (*qstate->env->detect_cycle)(qstate, &qinf,
		(uint16_t)(BIT_RD|BIT_CD), qstate->is_priming,
		qstate->is_valrec);
}

void
iter_mark_cycle_targets(struct module_qstate* qstate, struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->resolved)
			continue;
		/* see if this ns as target causes dependency cycle */
		if(causes_cycle(qstate, ns->name, ns->namelen,
			LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass) ||
		   causes_cycle(qstate, ns->name, ns->namelen,
			LDNS_RR_TYPE_A, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle (harden-glue: no may "
				"fix some of the cycles)",
				ns->name, LDNS_RR_TYPE_A,
				qstate->qinfo.qclass);
			ns->resolved = 1;
		}
	}
}

void
iter_mark_pside_cycle_targets(struct module_qstate* qstate, struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->done_pside4 && ns->done_pside6)
			continue;
		/* see if this ns as target causes dependency cycle */
		if(causes_cycle(qstate, ns->name, ns->namelen,
			LDNS_RR_TYPE_A, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle", ns->name,
				LDNS_RR_TYPE_A, qstate->qinfo.qclass);
			ns->done_pside4 = 1;
		}
		if(causes_cycle(qstate, ns->name, ns->namelen,
			LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle", ns->name,
				LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass);
			ns->done_pside6 = 1;
		}
	}
}

int
iter_dp_is_useless(struct query_info* qinfo, uint16_t qflags,
	struct delegpt* dp, int supports_ipv4, int supports_ipv6,
	int use_nat64)
{
	struct delegpt_ns* ns;
	struct delegpt_addr* a;

	if(supports_ipv6 && use_nat64)
		supports_ipv4 = 1;

	/* check:
	 *      o RD qflag is on.
	 *      o no addresses are provided.
	 *      o all NS items are required glue.
	 * OR
	 *      o RD qflag is on.
	 *      o no addresses are provided.
	 *      o the query is for one of the nameservers in dp,
	 *        and that nameserver is a glue-name for this dp.
	 */
	if(!(qflags&BIT_RD))
		return 0;
	/* either available or unused targets,
	 * if they exist, the dp is not useless. */
	for(a = dp->usable_list; a; a = a->next_usable) {
		if(!addr_is_ip6(&a->addr, a->addrlen) && supports_ipv4)
			return 0;
		else if(addr_is_ip6(&a->addr, a->addrlen) && supports_ipv6)
			return 0;
	}
	for(a = dp->result_list; a; a = a->next_result) {
		if(!addr_is_ip6(&a->addr, a->addrlen) && supports_ipv4)
			return 0;
		else if(addr_is_ip6(&a->addr, a->addrlen) && supports_ipv6)
			return 0;
	}

	/* see if query is for one of the nameservers, which is glue */
	if( ((qinfo->qtype == LDNS_RR_TYPE_A && supports_ipv4) ||
		(qinfo->qtype == LDNS_RR_TYPE_AAAA && supports_ipv6)) &&
		dname_subdomain_c(qinfo->qname, dp->name) &&
		delegpt_find_ns(dp, qinfo->qname, qinfo->qname_len))
		return 1;

	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->resolved) /* skip failed targets */
			continue;
		if(!dname_subdomain_c(ns->name, dp->name))
			return 0; /* one address is not required glue */
	}
	return 1;
}

int
iter_qname_indicates_dnssec(struct module_env* env, struct query_info *qinfo)
{
	struct trust_anchor* a;
	if(!env || !env->anchors || !qinfo || !qinfo->qname)
		return 0;
	/* a trust anchor exists above the name? */
	if((a=anchors_lookup(env->anchors, qinfo->qname, qinfo->qname_len,
		qinfo->qclass))) {
		if(a->numDS == 0 && a->numDNSKEY == 0) {
			/* insecure trust point */
			lock_basic_unlock(&a->lock);
			return 0;
		}
		lock_basic_unlock(&a->lock);
		return 1;
	}
	/* no trust anchor above it. */
	return 0;
}

int
iter_indicates_dnssec(struct module_env* env, struct delegpt* dp,
        struct dns_msg* msg, uint16_t dclass)
{
	struct trust_anchor* a;
	/* information not available, !env->anchors can be common */
	if(!env || !env->anchors || !dp || !dp->name)
		return 0;
	/* a trust anchor exists with this name, RRSIGs expected */
	if((a=anchor_find(env->anchors, dp->name, dp->namelabs, dp->namelen,
		dclass))) {
		if(a->numDS == 0 && a->numDNSKEY == 0) {
			/* insecure trust point */
			lock_basic_unlock(&a->lock);
			return 0;
		}
		lock_basic_unlock(&a->lock);
		return 1;
	}
	/* see if DS rrset was given, in AUTH section */
	if(msg && msg->rep &&
		reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_DS, dclass))
		return 1;
	/* look in key cache */
	if(env->key_cache) {
		struct key_entry_key* kk = key_cache_obtain(env->key_cache,
			dp->name, dp->namelen, dclass, env->scratch, *env->now);
		if(kk) {
			if(query_dname_compare(kk->name, dp->name) == 0) {
			  if(key_entry_isgood(kk) || key_entry_isbad(kk)) {
				regional_free_all(env->scratch);
				return 1;
			  } else if(key_entry_isnull(kk)) {
				regional_free_all(env->scratch);
				return 0;
			  }
			}
			regional_free_all(env->scratch);
		}
	}
	return 0;
}

int
iter_msg_has_dnssec(struct dns_msg* msg)
{
	size_t i;
	if(!msg || !msg->rep)
		return 0;
	for(i=0; i<msg->rep->an_numrrsets + msg->rep->ns_numrrsets; i++) {
		if(((struct packed_rrset_data*)msg->rep->rrsets[i]->
			entry.data)->rrsig_count > 0)
			return 1;
	}
	/* empty message has no DNSSEC info, with DNSSEC the reply is
	 * not empty (NSEC) */
	return 0;
}

int iter_msg_from_zone(struct dns_msg* msg, struct delegpt* dp,
        enum response_type type, uint16_t dclass)
{
	if(!msg || !dp || !msg->rep || !dp->name)
		return 0;
	/* SOA RRset - always from reply zone */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_SOA, dclass) ||
	   reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_SOA, dclass))
		return 1;
	if(type == RESPONSE_TYPE_REFERRAL) {
		size_t i;
		/* if it adds a single label, i.e. we expect .com,
		 * and referral to example.com. NS ... , then origin zone
		 * is .com. For a referral to sub.example.com. NS ... then
		 * we do not know, since example.com. may be in between. */
		for(i=0; i<msg->rep->an_numrrsets+msg->rep->ns_numrrsets;
			i++) {
			struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_NS &&
				ntohs(s->rk.rrset_class) == dclass) {
				int l = dname_count_labels(s->rk.dname);
				if(l == dp->namelabs + 1 &&
					dname_strict_subdomain(s->rk.dname,
					l, dp->name, dp->namelabs))
					return 1;
			}
		}
		return 0;
	}
	log_assert(type==RESPONSE_TYPE_ANSWER || type==RESPONSE_TYPE_CNAME);
	/* not a referral, and not lame delegation (upwards), so,
	 * any NS rrset must be from the zone itself */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_NS, dclass) ||
	   reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_NS, dclass))
		return 1;
	/* a DNSKEY set is expected at the zone apex as well */
	/* this is for 'minimal responses' for DNSKEYs */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_DNSKEY, dclass))
		return 1;
	return 0;
}

/**
 * check equality of two rrsets
 * @param k1: rrset
 * @param k2: rrset
 * @return true if equal
 */
static int
rrset_equal(struct ub_packed_rrset_key* k1, struct ub_packed_rrset_key* k2)
{
	struct packed_rrset_data* d1 = (struct packed_rrset_data*)
		k1->entry.data;
	struct packed_rrset_data* d2 = (struct packed_rrset_data*)
		k2->entry.data;
	size_t i, t;
	if(k1->rk.dname_len != k2->rk.dname_len ||
		k1->rk.flags != k2->rk.flags ||
		k1->rk.type != k2->rk.type ||
		k1->rk.rrset_class != k2->rk.rrset_class ||
		query_dname_compare(k1->rk.dname, k2->rk.dname) != 0)
		return 0;
	if(	/* do not check ttl: d1->ttl != d2->ttl || */
		d1->count != d2->count ||
		d1->rrsig_count != d2->rrsig_count ||
		d1->trust != d2->trust ||
		d1->security != d2->security)
		return 0;
	t = d1->count + d1->rrsig_count;
	for(i=0; i<t; i++) {
		if(d1->rr_len[i] != d2->rr_len[i] ||
			/* no ttl check: d1->rr_ttl[i] != d2->rr_ttl[i] ||*/
			memcmp(d1->rr_data[i], d2->rr_data[i],
				d1->rr_len[i]) != 0)
			return 0;
	}
	return 1;
}

/** compare rrsets and sort canonically.  Compares rrset name, type, class.
 * return 0 if equal, +1 if x > y, and -1 if x < y.
 */
static int
rrset_canonical_sort_cmp(const void* x, const void* y)
{
	struct ub_packed_rrset_key* rrx = *(struct ub_packed_rrset_key**)x;
	struct ub_packed_rrset_key* rry = *(struct ub_packed_rrset_key**)y;
	int r = dname_canonical_compare(rrx->rk.dname, rry->rk.dname);
	if(r != 0)
		return r;
	if(rrx->rk.type != rry->rk.type) {
		if(ntohs(rrx->rk.type) > ntohs(rry->rk.type))
			return 1;
		else	return -1;
	}
	if(rrx->rk.rrset_class != rry->rk.rrset_class) {
		if(ntohs(rrx->rk.rrset_class) > ntohs(rry->rk.rrset_class))
			return 1;
		else	return -1;
	}
	return 0;
}

int
reply_equal(struct reply_info* p, struct reply_info* q, struct regional* region)
{
	size_t i;
	struct ub_packed_rrset_key** sorted_p, **sorted_q;
	if(p->flags != q->flags ||
		p->qdcount != q->qdcount ||
		/* do not check TTL, this may differ */
		/*
		p->ttl != q->ttl ||
		p->prefetch_ttl != q->prefetch_ttl ||
		*/
		p->security != q->security ||
		p->an_numrrsets != q->an_numrrsets ||
		p->ns_numrrsets != q->ns_numrrsets ||
		p->ar_numrrsets != q->ar_numrrsets ||
		p->rrset_count != q->rrset_count)
		return 0;
	/* sort the rrsets in the authority and additional sections before
	 * compare, the query and answer sections are ordered in the sequence
	 * they should have (eg. one after the other for aliases). */
	sorted_p = (struct ub_packed_rrset_key**)regional_alloc_init(
		region, p->rrsets, sizeof(*sorted_p)*p->rrset_count);
	if(!sorted_p) return 0;
	log_assert(p->an_numrrsets + p->ns_numrrsets + p->ar_numrrsets <=
		p->rrset_count);
	qsort(sorted_p + p->an_numrrsets, p->ns_numrrsets,
		sizeof(*sorted_p), rrset_canonical_sort_cmp);
	qsort(sorted_p + p->an_numrrsets + p->ns_numrrsets, p->ar_numrrsets,
		sizeof(*sorted_p), rrset_canonical_sort_cmp);

	sorted_q = (struct ub_packed_rrset_key**)regional_alloc_init(
		region, q->rrsets, sizeof(*sorted_q)*q->rrset_count);
	if(!sorted_q) {
		regional_free_all(region);
		return 0;
	}
	log_assert(q->an_numrrsets + q->ns_numrrsets + q->ar_numrrsets <=
		q->rrset_count);
	qsort(sorted_q + q->an_numrrsets, q->ns_numrrsets,
		sizeof(*sorted_q), rrset_canonical_sort_cmp);
	qsort(sorted_q + q->an_numrrsets + q->ns_numrrsets, q->ar_numrrsets,
		sizeof(*sorted_q), rrset_canonical_sort_cmp);

	/* compare the rrsets */
	for(i=0; i<p->rrset_count; i++) {
		if(!rrset_equal(sorted_p[i], sorted_q[i])) {
			if(!rrset_canonical_equal(region, sorted_p[i],
				sorted_q[i])) {
				regional_free_all(region);
				return 0;
			}
		}
	}
	regional_free_all(region);
	return 1;
}

void
caps_strip_reply(struct reply_info* rep)
{
	size_t i;
	if(!rep) return;
	/* see if message is a referral, in which case the additional and
	 * NS record cannot be removed */
	/* referrals have the AA flag unset (strict check, not elsewhere in
	 * unbound, but for 0x20 this is very convenient). */
	if(!(rep->flags&BIT_AA))
		return;
	/* remove the additional section from the reply */
	if(rep->ar_numrrsets != 0) {
		verbose(VERB_ALGO, "caps fallback: removing additional section");
		rep->rrset_count -= rep->ar_numrrsets;
		rep->ar_numrrsets = 0;
	}
	/* is there an NS set in the authority section to remove? */
	/* the failure case (Cisco firewalls) only has one rrset in authsec */
	for(i=rep->an_numrrsets; i<rep->an_numrrsets+rep->ns_numrrsets; i++) {
		struct ub_packed_rrset_key* s = rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NS) {
			/* remove NS rrset and break from loop (loop limits
			 * have changed) */
			/* move last rrset into this position (there is no
			 * additional section any more) */
			verbose(VERB_ALGO, "caps fallback: removing NS rrset");
			if(i < rep->rrset_count-1)
				rep->rrsets[i]=rep->rrsets[rep->rrset_count-1];
			rep->rrset_count --;
			rep->ns_numrrsets --;
			break;
		}
	}
}

int caps_failed_rcode(struct reply_info* rep)
{
	return !(FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NOERROR ||
		FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NXDOMAIN);
}

void
iter_store_parentside_rrset(struct module_env* env,
	struct ub_packed_rrset_key* rrset)
{
	struct rrset_ref ref;
	rrset = packed_rrset_copy_alloc(rrset, env->alloc, *env->now);
	if(!rrset) {
		log_err("malloc failure in store_parentside_rrset");
		return;
	}
	rrset->rk.flags |= PACKED_RRSET_PARENT_SIDE;
	rrset->entry.hash = rrset_key_hash(&rrset->rk);
	ref.key = rrset;
	ref.id = rrset->id;
	/* ignore ret: if it was in the cache, ref updated */
	(void)rrset_cache_update(env->rrset_cache, &ref, env->alloc, *env->now);
}

/** fetch NS record from reply, if any */
static struct ub_packed_rrset_key*
reply_get_NS_rrset(struct reply_info* rep)
{
	size_t i;
	for(i=0; i<rep->rrset_count; i++) {
		if(rep->rrsets[i]->rk.type == htons(LDNS_RR_TYPE_NS)) {
			return rep->rrsets[i];
		}
	}
	return NULL;
}

void
iter_store_parentside_NS(struct module_env* env, struct reply_info* rep)
{
	struct ub_packed_rrset_key* rrset = reply_get_NS_rrset(rep);
	if(rrset) {
		log_rrset_key(VERB_ALGO, "store parent-side NS", rrset);
		iter_store_parentside_rrset(env, rrset);
	}
}

void iter_store_parentside_neg(struct module_env* env,
        struct query_info* qinfo, struct reply_info* rep)
{
	/* TTL: NS from referral in iq->deleg_msg,
	 *      or first RR from iq->response,
	 *      or servfail5secs if !iq->response */
	time_t ttl = NORR_TTL;
	struct ub_packed_rrset_key* neg;
	struct packed_rrset_data* newd;
	if(rep) {
		struct ub_packed_rrset_key* rrset = reply_get_NS_rrset(rep);
		if(!rrset && rep->rrset_count != 0) rrset = rep->rrsets[0];
		if(rrset) ttl = ub_packed_rrset_ttl(rrset);
	}
	/* create empty rrset to store */
	neg = (struct ub_packed_rrset_key*)regional_alloc(env->scratch,
	                sizeof(struct ub_packed_rrset_key));
	if(!neg) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	memset(&neg->entry, 0, sizeof(neg->entry));
	neg->entry.key = neg;
	neg->rk.type = htons(qinfo->qtype);
	neg->rk.rrset_class = htons(qinfo->qclass);
	neg->rk.flags = 0;
	neg->rk.dname = regional_alloc_init(env->scratch, qinfo->qname,
		qinfo->qname_len);
	if(!neg->rk.dname) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	neg->rk.dname_len = qinfo->qname_len;
	neg->entry.hash = rrset_key_hash(&neg->rk);
	newd = (struct packed_rrset_data*)regional_alloc_zero(env->scratch,
		sizeof(struct packed_rrset_data) + sizeof(size_t) +
		sizeof(uint8_t*) + sizeof(time_t) + sizeof(uint16_t));
	if(!newd) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	neg->entry.data = newd;
	newd->ttl = ttl;
	/* entry must have one RR, otherwise not valid in cache.
	 * put in one RR with empty rdata: those are ignored as nameserver */
	newd->count = 1;
	newd->rrsig_count = 0;
	newd->trust = rrset_trust_ans_noAA;
	newd->rr_len = (size_t*)((uint8_t*)newd +
		sizeof(struct packed_rrset_data));
	newd->rr_len[0] = 0 /* zero len rdata */ + sizeof(uint16_t);
	packed_rrset_ptr_fixup(newd);
	newd->rr_ttl[0] = newd->ttl;
	sldns_write_uint16(newd->rr_data[0], 0 /* zero len rdata */);
	/* store it */
	log_rrset_key(VERB_ALGO, "store parent-side negative", neg);
	iter_store_parentside_rrset(env, neg);
}

int
iter_lookup_parent_NS_from_cache(struct module_env* env, struct delegpt* dp,
	struct regional* region, struct query_info* qinfo)
{
	struct ub_packed_rrset_key* akey;
	akey = rrset_cache_lookup(env->rrset_cache, dp->name,
		dp->namelen, LDNS_RR_TYPE_NS, qinfo->qclass,
		PACKED_RRSET_PARENT_SIDE, *env->now, 0);
	if(akey) {
		log_rrset_key(VERB_ALGO, "found parent-side NS in cache", akey);
		dp->has_parent_side_NS = 1;
		/* and mark the new names as lame */
		if(!delegpt_rrset_add_ns(dp, region, akey, 1)) {
			lock_rw_unlock(&akey->entry.lock);
			return 0;
		}
		lock_rw_unlock(&akey->entry.lock);
	}
	return 1;
}

int iter_lookup_parent_glue_from_cache(struct module_env* env,
        struct delegpt* dp, struct regional* region, struct query_info* qinfo)
{
	struct ub_packed_rrset_key* akey;
	struct delegpt_ns* ns;
	size_t num = delegpt_count_targets(dp);
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->cache_lookup_count > ITERATOR_NAME_CACHELOOKUP_MAX_PSIDE)
			continue;
		ns->cache_lookup_count++;
		/* get cached parentside A */
		akey = rrset_cache_lookup(env->rrset_cache, ns->name,
			ns->namelen, LDNS_RR_TYPE_A, qinfo->qclass,
			PACKED_RRSET_PARENT_SIDE, *env->now, 0);
		if(akey) {
			log_rrset_key(VERB_ALGO, "found parent-side", akey);
			ns->done_pside4 = 1;
			/* a negative-cache-element has no addresses it adds */
			if(!delegpt_add_rrset_A(dp, region, akey, 1, NULL))
				log_err("malloc failure in lookup_parent_glue");
			lock_rw_unlock(&akey->entry.lock);
		}
		/* get cached parentside AAAA */
		akey = rrset_cache_lookup(env->rrset_cache, ns->name,
			ns->namelen, LDNS_RR_TYPE_AAAA, qinfo->qclass,
			PACKED_RRSET_PARENT_SIDE, *env->now, 0);
		if(akey) {
			log_rrset_key(VERB_ALGO, "found parent-side", akey);
			ns->done_pside6 = 1;
			/* a negative-cache-element has no addresses it adds */
			if(!delegpt_add_rrset_AAAA(dp, region, akey, 1, NULL))
				log_err("malloc failure in lookup_parent_glue");
			lock_rw_unlock(&akey->entry.lock);
		}
	}
	/* see if new (but lame) addresses have become available */
	return delegpt_count_targets(dp) != num;
}

int
iter_get_next_root(struct iter_hints* hints, struct iter_forwards* fwd,
	uint16_t* c)
{
	uint16_t c1 = *c, c2 = *c;
	int r1, r2;
	int nolock = 1;

	/* prelock both forwards and hints for atomic read. */
	lock_rw_rdlock(&fwd->lock);
	lock_rw_rdlock(&hints->lock);
	r1 = hints_next_root(hints, &c1, nolock);
	r2 = forwards_next_root(fwd, &c2, nolock);
	lock_rw_unlock(&fwd->lock);
	lock_rw_unlock(&hints->lock);

	if(!r1 && !r2) /* got none, end of list */
		return 0;
	else if(!r1) /* got one, return that */
		*c = c2;
	else if(!r2)
		*c = c1;
	else if(c1 < c2) /* got both take smallest */
		*c = c1;
	else	*c = c2;
	return 1;
}

void
iter_scrub_ds(struct dns_msg* msg, struct ub_packed_rrset_key* ns, uint8_t* z)
{
	/* Only the DS record for the delegation itself is expected.
	 * We allow DS for everything between the bailiwick and the
	 * zonecut, thus DS records must be at or above the zonecut.
	 * And the DS records must be below the server authority zone.
	 * The answer section is already scrubbed. */
	size_t i = msg->rep->an_numrrsets;
	while(i < (msg->rep->an_numrrsets + msg->rep->ns_numrrsets)) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DS &&
			(!ns || !dname_subdomain_c(ns->rk.dname, s->rk.dname)
			|| query_dname_compare(z, s->rk.dname) == 0)) {
			log_nametypeclass(VERB_ALGO, "removing irrelevant DS",
				s->rk.dname, ntohs(s->rk.type),
				ntohs(s->rk.rrset_class));
			memmove(msg->rep->rrsets+i, msg->rep->rrsets+i+1,
				sizeof(struct ub_packed_rrset_key*) *
				(msg->rep->rrset_count-i-1));
			msg->rep->ns_numrrsets--;
			msg->rep->rrset_count--;
			/* stay at same i, but new record */
			continue;
		}
		i++;
	}
}

void
iter_scrub_nxdomain(struct dns_msg* msg)
{
	if(msg->rep->an_numrrsets == 0)
		return;

	memmove(msg->rep->rrsets, msg->rep->rrsets+msg->rep->an_numrrsets,
		sizeof(struct ub_packed_rrset_key*) *
		(msg->rep->rrset_count-msg->rep->an_numrrsets));
	msg->rep->rrset_count -= msg->rep->an_numrrsets;
	msg->rep->an_numrrsets = 0;
}

void iter_dec_attempts(struct delegpt* dp, int d, int outbound_msg_retry)
{
	struct delegpt_addr* a;
	for(a=dp->target_list; a; a = a->next_target) {
		if(a->attempts >= outbound_msg_retry) {
			/* add back to result list */
			delegpt_add_to_result_list(dp, a);
		}
		if(a->attempts > d)
			a->attempts -= d;
		else a->attempts = 0;
	}
}

void iter_merge_retry_counts(struct delegpt* dp, struct delegpt* old,
	int outbound_msg_retry)
{
	struct delegpt_addr* a, *o, *prev;
	for(a=dp->target_list; a; a = a->next_target) {
		o = delegpt_find_addr(old, &a->addr, a->addrlen);
		if(o) {
			log_addr(VERB_ALGO, "copy attempt count previous dp",
				&a->addr, a->addrlen);
			a->attempts = o->attempts;
		}
	}
	prev = NULL;
	a = dp->usable_list;
	while(a) {
		if(a->attempts >= outbound_msg_retry) {
			log_addr(VERB_ALGO, "remove from usable list dp",
				&a->addr, a->addrlen);
			/* remove from result list */
			if(prev)
				prev->next_usable = a->next_usable;
			else	dp->usable_list = a->next_usable;
			/* prev stays the same */
			a = a->next_usable;
			continue;
		}
		prev = a;
		a = a->next_usable;
	}
}

int
iter_ds_toolow(struct dns_msg* msg, struct delegpt* dp)
{
	/* if for query example.com, there is example.com SOA or a subdomain
	 * of example.com, then we are too low and need to fetch NS. */
	size_t i;
	/* if we have a DNAME or CNAME we are probably wrong */
	/* if we have a qtype DS in the answer section, its fine */
	for(i=0; i < msg->rep->an_numrrsets; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DNAME ||
			ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME) {
			/* not the right answer, maybe too low, check the
			 * RRSIG signer name (if there is any) for a hint
			 * that it is from the dp zone anyway */
			uint8_t* sname;
			size_t slen;
			val_find_rrset_signer(s, &sname, &slen);
			if(sname && query_dname_compare(dp->name, sname)==0)
				return 0; /* it is fine, from the right dp */
			return 1;
		}
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DS)
			return 0; /* fine, we have a DS record */
	}
	for(i=msg->rep->an_numrrsets;
		i < msg->rep->an_numrrsets + msg->rep->ns_numrrsets; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_SOA) {
			if(dname_subdomain_c(s->rk.dname, msg->qinfo.qname))
				return 1; /* point is too low */
			if(query_dname_compare(s->rk.dname, dp->name)==0)
				return 0; /* right dp */
		}
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC ||
			ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			uint8_t* sname;
			size_t slen;
			val_find_rrset_signer(s, &sname, &slen);
			if(sname && query_dname_compare(dp->name, sname)==0)
				return 0; /* it is fine, from the right dp */
			return 1;
		}
	}
	/* we do not know */
	return 1;
}

int iter_dp_cangodown(struct query_info* qinfo, struct delegpt* dp)
{
	/* no delegation point, do not see how we can go down,
	 * robust check, it should really exist */
	if(!dp) return 0;

	/* see if dp equals the qname, then we cannot go down further */
	if(query_dname_compare(qinfo->qname, dp->name) == 0)
		return 0;
	/* if dp is one label above the name we also cannot go down further */
	if(dname_count_labels(qinfo->qname) == dp->namelabs+1)
		return 0;
	return 1;
}

int
iter_stub_fwd_no_cache(struct module_qstate *qstate, struct query_info *qinf,
	uint8_t** retdpname, size_t* retdpnamelen, uint8_t* dpname_storage,
	size_t dpname_storage_len)
{
	struct iter_hints_stub *stub;
	struct delegpt *dp;
	int nolock = 1;

	/* Check for stub. */
	/* Lock both forwards and hints for atomic read. */
	lock_rw_rdlock(&qstate->env->fwds->lock);
	lock_rw_rdlock(&qstate->env->hints->lock);
	stub = hints_lookup_stub(qstate->env->hints, qinf->qname,
	    qinf->qclass, NULL, nolock);
	dp = forwards_lookup(qstate->env->fwds, qinf->qname, qinf->qclass,
		nolock);

	/* see if forward or stub is more pertinent */
	if(stub && stub->dp && dp) {
		if(dname_strict_subdomain(dp->name, dp->namelabs,
			stub->dp->name, stub->dp->namelabs)) {
			stub = NULL; /* ignore stub, forward is lower */
		} else {
			dp = NULL; /* ignore forward, stub is lower */
		}
	}

	/* check stub */
	if (stub != NULL && stub->dp != NULL) {
		enum verbosity_value level = VERB_ALGO;
		int stub_no_cache = stub->dp->no_cache;
		lock_rw_unlock(&qstate->env->fwds->lock);
		if(verbosity >= level && stub_no_cache) {
			char qname[LDNS_MAX_DOMAINLEN];
			char dpname[LDNS_MAX_DOMAINLEN];
			dname_str(qinf->qname, qname);
			dname_str(stub->dp->name, dpname);
			verbose(level, "stub for %s %s has no_cache", qname, dpname);
		}
		if(retdpname) {
			if(stub->dp->namelen > dpname_storage_len) {
				verbose(VERB_ALGO, "no cache stub dpname too long");
				lock_rw_unlock(&qstate->env->hints->lock);
				*retdpname = NULL;
				*retdpnamelen = 0;
				return stub_no_cache;
			}
			memmove(dpname_storage, stub->dp->name,
				stub->dp->namelen);
			*retdpname = dpname_storage;
			*retdpnamelen = stub->dp->namelen;
		}
		lock_rw_unlock(&qstate->env->hints->lock);
		return stub_no_cache;
	}

	/* Check for forward. */
	if (dp) {
		enum verbosity_value level = VERB_ALGO;
		int dp_no_cache = dp->no_cache;
		lock_rw_unlock(&qstate->env->hints->lock);
		if(verbosity >= level && dp_no_cache) {
			char qname[LDNS_MAX_DOMAINLEN];
			char dpname[LDNS_MAX_DOMAINLEN];
			dname_str(qinf->qname, qname);
			dname_str(dp->name, dpname);
			verbose(level, "forward for %s %s has no_cache", qname, dpname);
		}
		if(retdpname) {
			if(dp->namelen > dpname_storage_len) {
				verbose(VERB_ALGO, "no cache dpname too long");
				lock_rw_unlock(&qstate->env->fwds->lock);
				*retdpname = NULL;
				*retdpnamelen = 0;
				return dp_no_cache;
			}
			memmove(dpname_storage, dp->name, dp->namelen);
			*retdpname = dpname_storage;
			*retdpnamelen = dp->namelen;
		}
		lock_rw_unlock(&qstate->env->fwds->lock);
		return dp_no_cache;
	}
	lock_rw_unlock(&qstate->env->fwds->lock);
	lock_rw_unlock(&qstate->env->hints->lock);
	if(retdpname) {
		*retdpname = NULL;
		*retdpnamelen = 0;
	}
	return 0;
}

void iterator_set_ip46_support(struct module_stack* mods,
	struct module_env* env, struct outside_network* outnet)
{
	int m = modstack_find(mods, "iterator");
	struct iter_env* ie = NULL;
	if(m == -1)
		return;
	ie = (struct iter_env*)env->modinfo[m];
	if(outnet->pending == NULL)
		return; /* we are in testbound, no rbtree for UDP */
	if(outnet->num_ip4 == 0)
		ie->supports_ipv4 = 0;
	if(outnet->num_ip6 == 0)
		ie->supports_ipv6 = 0;
}

void
limit_nsec_ttl(struct dns_msg* msg)
{
	/* Limit NSEC and NSEC3 TTL in response, RFC9077 */
	size_t i;
	int found = 0;
	time_t soa_ttl = 0;
	/* Limit the NSEC and NSEC3 TTL values to the SOA TTL and SOA minimum
	 * TTL. That has already been applied to the SOA record ttl. */
	for(i=0; i<msg->rep->rrset_count; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_SOA) {
			struct packed_rrset_data* soadata = (struct packed_rrset_data*)s->entry.data;
			found = 1;
			soa_ttl = soadata->ttl;
			break;
		}
	}
	if(!found)
		return;
	for(i=0; i<msg->rep->rrset_count; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC ||
			ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			struct packed_rrset_data* data = (struct packed_rrset_data*)s->entry.data;
			/* Limit the negative TTL. */
			if(data->ttl > soa_ttl) {
				if(verbosity >= VERB_ALGO) {
					char buf[256];
					snprintf(buf, sizeof(buf),
						"limiting TTL %d of %s record to the SOA TTL of %d for",
						(int)data->ttl, ((ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC)?"NSEC":"NSEC3"), (int)soa_ttl);
					log_nametypeclass(VERB_ALGO, buf,
						s->rk.dname, ntohs(s->rk.type),
						ntohs(s->rk.rrset_class));
				}
				data->ttl = soa_ttl;
			}
		}
	}
}

void
iter_make_minimal(struct reply_info* rep)
{
	size_t rem = rep->ns_numrrsets + rep->ar_numrrsets;
	rep->ns_numrrsets = 0;
	rep->ar_numrrsets = 0;
	rep->rrset_count -= rem;
}
