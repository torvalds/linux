/*
 * respip/respip.c - filtering response IP module
 */

/**
 * \file
 *
 * This file contains a module that inspects a result of recursive resolution
 * to see if any IP address record should trigger a special action.
 * If applicable these actions can modify the original response.
 */
#include "config.h"

#include "services/localzone.h"
#include "services/authzone.h"
#include "services/cache/dns.h"
#include "sldns/str2wire.h"
#include "util/config_file.h"
#include "util/fptr_wlist.h"
#include "util/module.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/data/msgreply.h"
#include "util/storage/dnstree.h"
#include "respip/respip.h"
#include "services/view.h"
#include "sldns/rrdef.h"
#include "util/data/dname.h"


/** Subset of resp_addr.node, used for inform-variant logging */
struct respip_addr_info {
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int net;
};

/** Query state regarding the response-ip module. */
enum respip_state {
	/**
	 * The general state.  Unless CNAME chasing takes place, all processing
	 * is completed in this state without any other asynchronous event.
	 */
	RESPIP_INIT = 0,

	/**
	 * A subquery for CNAME chasing is completed.
	 */
	RESPIP_SUBQUERY_FINISHED
};

/** Per query state for the response-ip module. */
struct respip_qstate {
	enum respip_state state;
};

struct respip_set*
respip_set_create(void)
{
	struct respip_set* set = calloc(1, sizeof(*set));
	if(!set)
		return NULL;
	set->region = regional_create();
	if(!set->region) {
		free(set);
		return NULL;
	}
	addr_tree_init(&set->ip_tree);
	lock_rw_init(&set->lock);
	return set;
}

/** helper traverse to delete resp_addr nodes */
static void
resp_addr_del(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct resp_addr* r = (struct resp_addr*)n->key;
	lock_rw_destroy(&r->lock);
#ifdef THREADS_DISABLED
	(void)r;
#endif
}

void
respip_set_delete(struct respip_set* set)
{
	if(!set)
		return;
	lock_rw_destroy(&set->lock);
	traverse_postorder(&set->ip_tree, resp_addr_del, NULL);
	regional_destroy(set->region);
	free(set);
}

struct rbtree_type*
respip_set_get_tree(struct respip_set* set)
{
	if(!set)
		return NULL;
	return &set->ip_tree;
}

struct resp_addr*
respip_sockaddr_find_or_create(struct respip_set* set, struct sockaddr_storage* addr,
		socklen_t addrlen, int net, int create, const char* ipstr)
{
	struct resp_addr* node;
	log_assert(set);
	node = (struct resp_addr*)addr_tree_find(&set->ip_tree, addr, addrlen, net);
	if(!node && create) {
		node = regional_alloc_zero(set->region, sizeof(*node));
		if(!node) {
			log_err("out of memory");
			return NULL;
		}
		lock_rw_init(&node->lock);
		node->action = respip_none;
		if(!addr_tree_insert(&set->ip_tree, &node->node, addr,
			addrlen, net)) {
			/* We know we didn't find it, so this should be
			 * impossible. */
			log_warn("unexpected: duplicate address: %s", ipstr);
		}
	}
	return node;
}

void
respip_sockaddr_delete(struct respip_set* set, struct resp_addr* node)
{
	struct resp_addr* prev;
	log_assert(set);
	prev = (struct resp_addr*)rbtree_previous((struct rbnode_type*)node);	
	lock_rw_destroy(&node->lock);
	(void)rbtree_delete(&set->ip_tree, node);
	/* no free'ing, all allocated in region */
	if(!prev)
		addr_tree_init_parents((rbtree_type*)set);
	else
		addr_tree_init_parents_node(&prev->node);
}

/** returns the node in the address tree for the specified netblock string;
 * non-existent node will be created if 'create' is true */
static struct resp_addr*
respip_find_or_create(struct respip_set* set, const char* ipstr, int create)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	log_assert(set);

	if(!netblockstrtoaddr(ipstr, 0, &addr, &addrlen, &net)) {
		log_err("cannot parse netblock: '%s'", ipstr);
		return NULL;
	}
	return respip_sockaddr_find_or_create(set, &addr, addrlen, net, create,
		ipstr);
}

static int
respip_tag_cfg(struct respip_set* set, const char* ipstr,
	const uint8_t* taglist, size_t taglen)
{
	struct resp_addr* node;
	log_assert(set);

	if(!(node=respip_find_or_create(set, ipstr, 1)))
		return 0;
	if(node->taglist) {
		log_warn("duplicate response-address-tag for '%s', overridden.",
			ipstr);
	}
	node->taglist = regional_alloc_init(set->region, taglist, taglen);
	if(!node->taglist) {
		log_err("out of memory");
		return 0;
	}
	node->taglen = taglen;
	return 1;
}

/** set action for the node specified by the netblock string */
static int
respip_action_cfg(struct respip_set* set, const char* ipstr,
	const char* actnstr)
{
	struct resp_addr* node;
	enum respip_action action;
	log_assert(set);

	if(!(node=respip_find_or_create(set, ipstr, 1)))
		return 0;
	if(node->action != respip_none) {
		verbose(VERB_QUERY, "duplicate response-ip action for '%s', overridden.",
			ipstr);
	}
        if(strcmp(actnstr, "deny") == 0)
                action = respip_deny;
        else if(strcmp(actnstr, "redirect") == 0)
                action = respip_redirect;
        else if(strcmp(actnstr, "inform") == 0)
                action = respip_inform;
        else if(strcmp(actnstr, "inform_deny") == 0)
                action = respip_inform_deny;
        else if(strcmp(actnstr, "inform_redirect") == 0)
                action = respip_inform_redirect;
        else if(strcmp(actnstr, "always_transparent") == 0)
                action = respip_always_transparent;
        else if(strcmp(actnstr, "always_refuse") == 0)
                action = respip_always_refuse;
        else if(strcmp(actnstr, "always_nxdomain") == 0)
                action = respip_always_nxdomain;
        else if(strcmp(actnstr, "always_nodata") == 0)
                action = respip_always_nodata;
        else if(strcmp(actnstr, "always_deny") == 0)
                action = respip_always_deny;
        else {
                log_err("unknown response-ip action %s", actnstr);
                return 0;
        }
	node->action = action;
	return 1;
}

/** allocate and initialize an rrset structure; this function is based
 * on new_local_rrset() from the localzone.c module */
static struct ub_packed_rrset_key*
new_rrset(struct regional* region, uint16_t rrtype, uint16_t rrclass)
{
	struct packed_rrset_data* pd;
	struct ub_packed_rrset_key* rrset = regional_alloc_zero(
		region, sizeof(*rrset));
	if(!rrset) {
		log_err("out of memory");
		return NULL;
	}
	rrset->entry.key = rrset;
	pd = regional_alloc_zero(region, sizeof(*pd));
	if(!pd) {
		log_err("out of memory");
		return NULL;
	}
	pd->trust = rrset_trust_prim_noglue;
	pd->security = sec_status_insecure;
	rrset->entry.data = pd;
	rrset->rk.dname = regional_alloc_zero(region, 1);
	if(!rrset->rk.dname) {
		log_err("out of memory");
		return NULL;
	}
	rrset->rk.dname_len = 1;
	rrset->rk.type = htons(rrtype);
	rrset->rk.rrset_class = htons(rrclass);
	return rrset;
}

/** enter local data as resource records into a response-ip node */

int
respip_enter_rr(struct regional* region, struct resp_addr* raddr,
	uint16_t rrtype, uint16_t rrclass, time_t ttl, uint8_t* rdata,
	size_t rdata_len, const char* rrstr, const char* netblockstr)
{
	struct packed_rrset_data* pd;
	struct sockaddr* sa;
	sa = (struct sockaddr*)&raddr->node.addr;
	if (rrtype == LDNS_RR_TYPE_CNAME && raddr->data) {
		log_err("CNAME response-ip data (%s) can not co-exist with other "
			"response-ip data for netblock %s", rrstr, netblockstr);
		return 0;
	} else if (raddr->data &&
		raddr->data->rk.type == htons(LDNS_RR_TYPE_CNAME)) {
		log_err("response-ip data (%s) can not be added; CNAME response-ip "
			"data already in place for netblock %s", rrstr, netblockstr);
		return 0;
	} else if((rrtype != LDNS_RR_TYPE_CNAME) &&
		((sa->sa_family == AF_INET && rrtype != LDNS_RR_TYPE_A) ||
		(sa->sa_family == AF_INET6 && rrtype != LDNS_RR_TYPE_AAAA))) {
		log_err("response-ip data %s record type does not correspond "
			"to netblock %s address family", rrstr, netblockstr);
		return 0;
	}

	if(!raddr->data) {
		raddr->data = new_rrset(region, rrtype, rrclass);
		if(!raddr->data)
			return 0;
	}
	pd = raddr->data->entry.data;
	return rrset_insert_rr(region, pd, rdata, rdata_len, ttl, rrstr);
}

static int
respip_enter_rrstr(struct regional* region, struct resp_addr* raddr,
		const char* rrstr, const char* netblock)
{
	uint8_t* nm;
	uint16_t rrtype = 0, rrclass = 0;
	time_t ttl = 0;
	uint8_t rr[LDNS_RR_BUF_SIZE];
	uint8_t* rdata = NULL;
	size_t rdata_len = 0;
	char buf[65536];
	char bufshort[64];
	int ret;
	if(raddr->action != respip_redirect
		&& raddr->action != respip_inform_redirect) {
		log_err("cannot parse response-ip-data %s: response-ip "
			"action for %s is not redirect", rrstr, netblock);
		return 0;
	}
	ret = snprintf(buf, sizeof(buf), ". %s", rrstr);
	if(ret < 0 || ret >= (int)sizeof(buf)) {
		strlcpy(bufshort, rrstr, sizeof(bufshort));
		log_err("bad response-ip-data: %s...", bufshort);
		return 0;
	}
	if(!rrstr_get_rr_content(buf, &nm, &rrtype, &rrclass, &ttl, rr, sizeof(rr),
		&rdata, &rdata_len)) {
		log_err("bad response-ip-data: %s", rrstr);
		return 0;
	}
	free(nm);
	return respip_enter_rr(region, raddr, rrtype, rrclass, ttl, rdata,
		rdata_len, rrstr, netblock);
}

static int
respip_data_cfg(struct respip_set* set, const char* ipstr, const char* rrstr)
{
	struct resp_addr* node;
	log_assert(set);

	node=respip_find_or_create(set, ipstr, 0);
	if(!node || node->action == respip_none) {
		log_err("cannot parse response-ip-data %s: "
			"response-ip node for %s not found", rrstr, ipstr);
		return 0;
	}
	return respip_enter_rrstr(set->region, node, rrstr, ipstr);
}

static int
respip_set_apply_cfg(struct respip_set* set, char* const* tagname, int num_tags,
	struct config_strbytelist* respip_tags,
	struct config_str2list* respip_actions,
	struct config_str2list* respip_data)
{
	struct config_strbytelist* p;
	struct config_str2list* pa;
	struct config_str2list* pd;
	log_assert(set);

	set->tagname = tagname;
	set->num_tags = num_tags;

	p = respip_tags;
	while(p) {
		struct config_strbytelist* np = p->next;

		log_assert(p->str && p->str2);
		if(!respip_tag_cfg(set, p->str, p->str2, p->str2len)) {
			config_del_strbytelist(p);
			return 0;
		}
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}

	pa = respip_actions;
	while(pa) {
		struct config_str2list* np = pa->next;
		log_assert(pa->str && pa->str2);
		if(!respip_action_cfg(set, pa->str, pa->str2)) {
			config_deldblstrlist(pa);
			return 0;
		}
		free(pa->str);
		free(pa->str2);
		free(pa);
		pa = np;
	}

	pd = respip_data;
	while(pd) {
		struct config_str2list* np = pd->next;
		log_assert(pd->str && pd->str2);
		if(!respip_data_cfg(set, pd->str, pd->str2)) {
			config_deldblstrlist(pd);
			return 0;
		}
		free(pd->str);
		free(pd->str2);
		free(pd);
		pd = np;
	}
	addr_tree_init_parents(&set->ip_tree);

	return 1;
}

int
respip_global_apply_cfg(struct respip_set* set, struct config_file* cfg)
{
	int ret = respip_set_apply_cfg(set, cfg->tagname, cfg->num_tags,
		cfg->respip_tags, cfg->respip_actions, cfg->respip_data);
	cfg->respip_data = NULL;
	cfg->respip_actions = NULL;
	cfg->respip_tags = NULL;
	return ret;
}

/** Iterate through raw view data and apply the view-specific respip
 * configuration; at this point we should have already seen all the views,
 * so if any of the views that respip data refer to does not exist, that's
 * an error.  This additional iteration through view configuration data
 * is expected to not have significant performance impact (or rather, its
 * performance impact is not expected to be prohibitive in the configuration
 * processing phase).
 */
int
respip_views_apply_cfg(struct views* vs, struct config_file* cfg,
	int* have_view_respip_cfg)
{
	struct config_view* cv;
	struct view* v;
	int ret;

	for(cv = cfg->views; cv; cv = cv->next) {

		/** if no respip config for this view then there's
		  * nothing to do; note that even though respip data must go
		  * with respip action, we're checking for both here because
		  * we want to catch the case where the respip action is missing
		  * while the data is present */
		if(!cv->respip_actions && !cv->respip_data)
			continue;

		if(!(v = views_find_view(vs, cv->name, 1))) {
			log_err("view '%s' unexpectedly missing", cv->name);
			return 0;
		}
		if(!v->respip_set) {
			v->respip_set = respip_set_create();
			if(!v->respip_set) {
				log_err("out of memory");
				lock_rw_unlock(&v->lock);
				return 0;
			}
		}
		ret = respip_set_apply_cfg(v->respip_set, NULL, 0, NULL,
			cv->respip_actions, cv->respip_data);
		lock_rw_unlock(&v->lock);
		if(!ret) {
			log_err("Error while applying respip configuration "
				"for view '%s'", cv->name);
			return 0;
		}
		*have_view_respip_cfg = (*have_view_respip_cfg ||
			v->respip_set->ip_tree.count);
		cv->respip_actions = NULL;
		cv->respip_data = NULL;
	}
	return 1;
}

/**
 * make a deep copy of 'key' in 'region'.
 * This is largely derived from packed_rrset_copy_region() and
 * packed_rrset_ptr_fixup(), but differs in the following points:
 *
 * - It doesn't assume all data in 'key' are in a contiguous memory region.
 *   Although that would be the case in most cases, 'key' can be passed from
 *   a lower-level module and it might not build the rrset to meet the
 *   assumption.  In fact, an rrset specified as response-ip-data or generated
 *   in local_data_find_tag_datas() breaks the assumption.  So it would be
 *   safer not to naively rely on the assumption.  On the other hand, this
 *   function ensures the copied rrset data are in a contiguous region so
 *   that it won't cause a disruption even if an upper layer module naively
 *   assumes the memory layout.
 * - It doesn't copy RRSIGs (if any) in 'key'.  The rrset will be used in
 *   a reply that was already faked, so it doesn't make much sense to provide
 *   partial sigs even if they are valid themselves.
 * - It doesn't adjust TTLs as it basically has to be a verbatim copy of 'key'
 *   just allocated in 'region' (the assumption is necessary TTL adjustment
 *   has been already done in 'key').
 *
 * This function returns the copied rrset key on success, and NULL on memory
 * allocation failure.
 */
struct ub_packed_rrset_key*
respip_copy_rrset(const struct ub_packed_rrset_key* key, struct regional* region)
{
	struct ub_packed_rrset_key* ck = regional_alloc(region,
		sizeof(struct ub_packed_rrset_key));
	struct packed_rrset_data* d;
	struct packed_rrset_data* data = key->entry.data;
	size_t dsize, i;
	uint8_t* nextrdata;

	/* derived from packed_rrset_copy_region(), but don't use
	 * packed_rrset_sizeof() and do exclude RRSIGs */
	if(!ck)
		return NULL;
	ck->id = key->id;
	memset(&ck->entry, 0, sizeof(ck->entry));
	ck->entry.hash = key->entry.hash;
	ck->entry.key = ck;
	ck->rk = key->rk;
	if(key->rk.dname) {
		ck->rk.dname = regional_alloc_init(region, key->rk.dname,
			key->rk.dname_len);
		if(!ck->rk.dname)
			return NULL;
		ck->rk.dname_len = key->rk.dname_len;
	} else {
		ck->rk.dname = NULL;
		ck->rk.dname_len = 0;
	}

	if((unsigned)data->count >= 0xffff00U)
		return NULL; /* guard against integer overflow in dsize */
	dsize = sizeof(struct packed_rrset_data) + data->count *
		(sizeof(size_t)+sizeof(uint8_t*)+sizeof(time_t));
	for(i=0; i<data->count; i++) {
		if((unsigned)dsize >= 0x0fffffffU ||
			(unsigned)data->rr_len[i] >= 0x0fffffffU)
			return NULL; /* guard against integer overflow */
		dsize += data->rr_len[i];
	}
	d = regional_alloc_zero(region, dsize);
	if(!d)
		return NULL;
	*d = *data;
	d->rrsig_count = 0;
	ck->entry.data = d;

	/* derived from packed_rrset_ptr_fixup() with copying the data */
	d->rr_len = (size_t*)((uint8_t*)d + sizeof(struct packed_rrset_data));
	d->rr_data = (uint8_t**)&(d->rr_len[d->count]);
	d->rr_ttl = (time_t*)&(d->rr_data[d->count]);
	nextrdata = (uint8_t*)&(d->rr_ttl[d->count]);
	for(i=0; i<d->count; i++) {
		d->rr_len[i] = data->rr_len[i];
		d->rr_ttl[i] = data->rr_ttl[i];
		d->rr_data[i] = nextrdata;
		memcpy(d->rr_data[i], data->rr_data[i], data->rr_len[i]);
		nextrdata += d->rr_len[i];
	}

	return ck;
}

int
respip_init(struct module_env* env, int id)
{
	(void)env;
	(void)id;
	return 1;
}

void
respip_deinit(struct module_env* env, int id)
{
	(void)env;
	(void)id;
}

/** Convert a packed AAAA or A RRset to sockaddr. */
static int
rdata2sockaddr(const struct packed_rrset_data* rd, uint16_t rtype, size_t i,
	struct sockaddr_storage* ss, socklen_t* addrlenp)
{
	/* unbound can accept and cache odd-length AAAA/A records, so we have
	 * to validate the length. */
	if(rtype == LDNS_RR_TYPE_A && rd->rr_len[i] == 6) {
		struct sockaddr_in* sa4 = (struct sockaddr_in*)ss;

		memset(sa4, 0, sizeof(*sa4));
		sa4->sin_family = AF_INET;
		memcpy(&sa4->sin_addr, rd->rr_data[i] + 2,
			sizeof(sa4->sin_addr));
		*addrlenp = sizeof(*sa4);
		return 1;
	} else if(rtype == LDNS_RR_TYPE_AAAA && rd->rr_len[i] == 18) {
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)ss;

		memset(sa6, 0, sizeof(*sa6));
		sa6->sin6_family = AF_INET6;
		memcpy(&sa6->sin6_addr, rd->rr_data[i] + 2,
			sizeof(sa6->sin6_addr));
		*addrlenp = sizeof(*sa6);
		return 1;
	}
	return 0;
}

/**
 * Search the given 'iptree' for response address information that matches
 * any of the IP addresses in an AAAA or A in the answer section of the
 * response (stored in 'rep').  If found, a pointer to the matched resp_addr
 * structure will be returned, and '*rrset_id' is set to the index in
 * rep->rrsets for the RRset that contains the matching IP address record
 * (the index is normally 0, but can be larger than that if this is a CNAME
 * chain or type-ANY response).
 * Returns resp_addr holding read lock.
 */
static struct resp_addr*
respip_addr_lookup(const struct reply_info *rep, struct respip_set* rs,
	size_t* rrset_id, size_t* rr_id)
{
	size_t i;
	struct resp_addr* ra;
	struct sockaddr_storage ss;
	socklen_t addrlen;
	log_assert(rs);

	lock_rw_rdlock(&rs->lock);
	for(i=0; i<rep->an_numrrsets; i++) {
		size_t j;
		const struct packed_rrset_data* rd;
		uint16_t rtype = ntohs(rep->rrsets[i]->rk.type);

		if(rtype != LDNS_RR_TYPE_A && rtype != LDNS_RR_TYPE_AAAA)
			continue;
		rd = rep->rrsets[i]->entry.data;
		for(j = 0; j < rd->count; j++) {
			if(!rdata2sockaddr(rd, rtype, j, &ss, &addrlen))
				continue;
			ra = (struct resp_addr*)addr_tree_lookup(&rs->ip_tree,
				&ss, addrlen);
			if(ra) {
				*rrset_id = i;
				*rr_id = j;
				lock_rw_rdlock(&ra->lock);
				lock_rw_unlock(&rs->lock);
				return ra;
			}
		}
	}
	lock_rw_unlock(&rs->lock);
	return NULL;
}

/**
 * See if response-ip or tag data should override the original answer rrset
 * (which is rep->rrsets[rrset_id]) and if so override it.
 * This is (mostly) equivalent to localzone.c:local_data_answer() but for
 * response-ip actions.
 * Note that this function distinguishes error conditions from "success but
 * not overridden".  This is because we want to avoid accidentally applying
 * the "no data" action in case of error.
 * @param action: action to apply
 * @param data: RRset to use for override
 * @param qtype: original query type
 * @param rep: original reply message
 * @param rrset_id: the rrset ID in 'rep' to which the action should apply
 * @param new_repp: see respip_rewrite_reply
 * @param tag: if >= 0 the tag ID used to determine the action and data
 * @param tag_datas: data corresponding to 'tag'.
 * @param tag_datas_size: size of 'tag_datas'
 * @param tagname: array of tag names, used for logging
 * @param num_tags: size of 'tagname', used for logging
 * @param redirect_rrsetp: ptr to redirect record
 * @param region: region for building new reply
 * @return 1 if overridden, 0 if not overridden, -1 on error.
 */
static int
respip_data_answer(enum respip_action action,
	struct ub_packed_rrset_key* data,
	uint16_t qtype, const struct reply_info* rep,
	size_t rrset_id, struct reply_info** new_repp, int tag,
	struct config_strlist** tag_datas, size_t tag_datas_size,
	char* const* tagname, int num_tags,
	struct ub_packed_rrset_key** redirect_rrsetp, struct regional* region)
{
	struct ub_packed_rrset_key* rp = data;
	struct reply_info* new_rep;
	*redirect_rrsetp = NULL;

	if(action == respip_redirect && tag != -1 &&
		(size_t)tag<tag_datas_size && tag_datas[tag]) {
		struct query_info dataqinfo;
		struct ub_packed_rrset_key r;

		/* Extract parameters of the original answer rrset that can be
		 * rewritten below, in the form of query_info.  Note that these
		 * can be different from the info of the original query if the
		 * rrset is a CNAME target.*/
		memset(&dataqinfo, 0, sizeof(dataqinfo));
		dataqinfo.qname = rep->rrsets[rrset_id]->rk.dname;
		dataqinfo.qname_len = rep->rrsets[rrset_id]->rk.dname_len;
		dataqinfo.qtype = ntohs(rep->rrsets[rrset_id]->rk.type);
		dataqinfo.qclass = ntohs(rep->rrsets[rrset_id]->rk.rrset_class);

		memset(&r, 0, sizeof(r));
		if(local_data_find_tag_datas(&dataqinfo, tag_datas[tag], &r,
			region)) {
			verbose(VERB_ALGO,
				"response-ip redirect with tag data [%d] %s",
				tag, (tag<num_tags?tagname[tag]:"null"));
			/* use copy_rrset() to 'normalize' memory layout */
			rp = respip_copy_rrset(&r, region);
			if(!rp)
				return -1;
		}
	}
	if(!rp)
		return 0;

	/* If we are using response-ip-data, we need to make a copy of rrset
	 * to replace the rrset's dname.  Note that, unlike local data, we
	 * rename the dname for other actions than redirect.  This is because
	 * response-ip-data isn't associated to any specific name. */
	if(rp == data) {
		rp = respip_copy_rrset(rp, region);
		if(!rp)
			return -1;
		rp->rk.dname = rep->rrsets[rrset_id]->rk.dname;
		rp->rk.dname_len = rep->rrsets[rrset_id]->rk.dname_len;
	}

	/* Build a new reply with redirect rrset.  We keep any preceding CNAMEs
	 * and replace the address rrset that triggers the action.  If it's
	 * type ANY query, however, no other answer records should be kept
	 * (note that it can't be a CNAME chain in this case due to
	 * sanitizing). */
	if(qtype == LDNS_RR_TYPE_ANY)
		rrset_id = 0;
	new_rep = make_new_reply_info(rep, region, rrset_id + 1, rrset_id);
	if(!new_rep)
		return -1;
	rp->rk.flags |= PACKED_RRSET_FIXEDTTL; /* avoid adjusting TTL */
	new_rep->rrsets[rrset_id] = rp;

	*redirect_rrsetp = rp;
	*new_repp = new_rep;
	return 1;
}

/**
 * apply response ip action in case where no action data is provided.
 * this is similar to localzone.c:lz_zone_answer() but simplified due to
 * the characteristics of response ip:
 * - 'deny' variants will be handled at the caller side
 * - no specific processing for 'transparent' variants: unlike local zones,
 *   there is no such a case of 'no data but name existing'.  so all variants
 *   just mean 'transparent if no data'.
 * @param qtype: query type
 * @param action: found action
 * @param rep:
 * @param new_repp
 * @param rrset_id
 * @param region: region for building new reply
 * @return 1 on success, 0 on error.
 */
static int
respip_nodata_answer(uint16_t qtype, enum respip_action action,
	const struct reply_info *rep, size_t rrset_id,
	struct reply_info** new_repp, struct regional* region)
{
	struct reply_info* new_rep;

	if(action == respip_refuse || action == respip_always_refuse) {
		new_rep = make_new_reply_info(rep, region, 0, 0);
		if(!new_rep)
			return 0;
		FLAGS_SET_RCODE(new_rep->flags, LDNS_RCODE_REFUSED);
		*new_repp = new_rep;
		return 1;
	} else if(action == respip_static || action == respip_redirect ||
		action == respip_always_nxdomain ||
		action == respip_always_nodata ||
		action == respip_inform_redirect) {
		/* Since we don't know about other types of the owner name,
		 * we generally return NOERROR/NODATA unless an NXDOMAIN action
		 * is explicitly specified. */
		int rcode = (action == respip_always_nxdomain)?
			LDNS_RCODE_NXDOMAIN:LDNS_RCODE_NOERROR;
		/* We should empty the answer section except for any preceding
		 * CNAMEs (in that case rrset_id > 0).  Type-ANY case is
		 * special as noted in respip_data_answer(). */
		if(qtype == LDNS_RR_TYPE_ANY)
			rrset_id = 0;
		new_rep = make_new_reply_info(rep, region, rrset_id, rrset_id);
		if(!new_rep)
			return 0;
		FLAGS_SET_RCODE(new_rep->flags, rcode);
		*new_repp = new_rep;
		return 1;
	}

	return 1;
}

/** Populate action info structure with the results of response-ip action
 *  processing, iff as the result of response-ip processing we are actually
 *  taking some action. Only action is set if action_only is true.
 *  Returns true on success, false on failure.
 */
static int
populate_action_info(struct respip_action_info* actinfo,
	enum respip_action action, const struct resp_addr* raddr,
	const struct ub_packed_rrset_key* ATTR_UNUSED(rrset),
	int ATTR_UNUSED(tag), const struct respip_set* ATTR_UNUSED(ipset),
	int ATTR_UNUSED(action_only), struct regional* region, int rpz_used,
	int rpz_log, char* log_name, int rpz_cname_override)
{
	if(action == respip_none || !raddr)
		return 1;
	actinfo->action = action;
	actinfo->rpz_used = rpz_used;
	actinfo->rpz_log = rpz_log;
	actinfo->log_name = log_name;
	actinfo->rpz_cname_override = rpz_cname_override;

	/* for inform variants, make a copy of the matched address block for
	 * later logging.  We make a copy to proactively avoid disruption if
	 *  and when we allow a dynamic update to the respip tree. */
	if(action == respip_inform || action == respip_inform_deny ||
		rpz_used) {
		struct respip_addr_info* a =
			regional_alloc_zero(region, sizeof(*a));
		if(!a) {
			log_err("out of memory");
			return 0;
		}
		a->addr = raddr->node.addr;
		a->addrlen = raddr->node.addrlen;
		a->net = raddr->node.net;
		actinfo->addrinfo = a;
	}

	return 1;
}

static int
respip_use_rpz(struct resp_addr* raddr, struct rpz* r,
	enum respip_action* action,
	struct ub_packed_rrset_key** data, int* rpz_log, char** log_name,
	int* rpz_cname_override, struct regional* region, int* is_rpz,
	int* rpz_passthru)
{
	if(rpz_passthru && *rpz_passthru)
		return 0;
	if(r->action_override == RPZ_DISABLED_ACTION) {
		*is_rpz = 0;
		return 1;
	}
	else if(r->action_override == RPZ_NO_OVERRIDE_ACTION)
		*action = raddr->action;
	else
		*action = rpz_action_to_respip_action(r->action_override);
	if(r->action_override == RPZ_CNAME_OVERRIDE_ACTION &&
		r->cname_override) {
		*data = r->cname_override;
		*rpz_cname_override = 1;
	}
	if(*action == respip_always_transparent /* RPZ_PASSTHRU_ACTION */
		&& rpz_passthru)
		*rpz_passthru = 1;
	*rpz_log = r->log;
	if(r->log_name)
		if(!(*log_name = regional_strdup(region, r->log_name)))
			return 0;
	*is_rpz = 1;
	return 1;
}

int
respip_rewrite_reply(const struct query_info* qinfo,
	const struct respip_client_info* cinfo, const struct reply_info* rep,
	struct reply_info** new_repp, struct respip_action_info* actinfo,
	struct ub_packed_rrset_key** alias_rrset, int search_only,
	struct regional* region, struct auth_zones* az, int* rpz_passthru,
	struct views* views, struct respip_set* ipset)
{
	const uint8_t* ctaglist;
	size_t ctaglen;
	const uint8_t* tag_actions;
	size_t tag_actions_size;
	struct config_strlist** tag_datas;
	size_t tag_datas_size;
	struct view* view = NULL;
	size_t rrset_id = 0, rr_id = 0;
	enum respip_action action = respip_none;
	int tag = -1;
	struct resp_addr* raddr = NULL;
	int ret = 1;
	struct ub_packed_rrset_key* redirect_rrset = NULL;
	struct rpz* r;
	struct auth_zone* a = NULL;
	struct ub_packed_rrset_key* data = NULL;
	int rpz_used = 0;
	int rpz_log = 0;
	int rpz_cname_override = 0;
	char* log_name = NULL;

	if(!cinfo)
		goto done;
	ctaglist = cinfo->taglist;
	ctaglen = cinfo->taglen;
	tag_actions = cinfo->tag_actions;
	tag_actions_size = cinfo->tag_actions_size;
	tag_datas = cinfo->tag_datas;
	tag_datas_size = cinfo->tag_datas_size;
	if(cinfo->view) {
		view = cinfo->view;
		lock_rw_rdlock(&view->lock);
	} else if(cinfo->view_name) {
		view = views_find_view(views, cinfo->view_name, 0);
		if(!view) {
			/* If the view no longer exists, the rewrite can not
			 * be processed further. */
			verbose(VERB_ALGO, "respip: failed because view %s no "
				"longer exists", cinfo->view_name);
			return 0;
		}
		/* The view is rdlocked by views_find_view. */
	}

	log_assert(ipset);

	/** Try to use response-ip config from the view first; use
	  * global response-ip config if we don't have the view or we don't
	  * have the matching per-view config (and the view allows the use
	  * of global data in this case).
	  * Note that we lock the view even if we only use view members that
	  * currently don't change after creation.  This is for safety for
	  * future possible changes as the view documentation seems to expect
	  * any of its member can change in the view's lifetime.
	  * Note also that we assume 'view' is valid in this function, which
	  * should be safe (see unbound bug #1191) */
	if(view) {
		if(view->respip_set) {
			if((raddr = respip_addr_lookup(rep,
				view->respip_set, &rrset_id, &rr_id))) {
				/** for per-view respip directives the action
				 * can only be direct (i.e. not tag-based) */
				action = raddr->action;
			}
		}
		if(!raddr && !view->isfirst)
			goto done;
		if(!raddr && view->isfirst) {
			lock_rw_unlock(&view->lock);
			view = NULL;
		}
	}
	if(!raddr && (raddr = respip_addr_lookup(rep, ipset,
		&rrset_id, &rr_id))) {
		action = (enum respip_action)local_data_find_tag_action(
			raddr->taglist, raddr->taglen, ctaglist, ctaglen,
			tag_actions, tag_actions_size,
			(enum localzone_type)raddr->action, &tag,
			ipset->tagname, ipset->num_tags);
	}
	lock_rw_rdlock(&az->rpz_lock);
	for(a = az->rpz_first; a && !raddr && !(rpz_passthru && *rpz_passthru); a = a->rpz_az_next) {
		lock_rw_rdlock(&a->lock);
		r = a->rpz;
		if(!r->taglist || taglist_intersect(r->taglist, 
			r->taglistlen, ctaglist, ctaglen)) {
			if((raddr = respip_addr_lookup(rep,
				r->respip_set, &rrset_id, &rr_id))) {
				if(!respip_use_rpz(raddr, r, &action, &data,
					&rpz_log, &log_name, &rpz_cname_override,
					region, &rpz_used, rpz_passthru)) {
					log_err("out of memory");
					lock_rw_unlock(&raddr->lock);
					lock_rw_unlock(&a->lock);
					lock_rw_unlock(&az->rpz_lock);
					return 0;
				}
				if(rpz_used) {
					if(verbosity >= VERB_ALGO) {
						struct sockaddr_storage ss;
						socklen_t ss_len = 0;
						char nm[256], ip[256];
						char qn[LDNS_MAX_DOMAINLEN];
						if(!rdata2sockaddr(rep->rrsets[rrset_id]->entry.data, ntohs(rep->rrsets[rrset_id]->rk.type), rr_id, &ss, &ss_len))
							snprintf(ip, sizeof(ip), "invalidRRdata");
						else
							addr_to_str(&ss, ss_len, ip, sizeof(ip));
						dname_str(qinfo->qname, qn);
						addr_to_str(&raddr->node.addr,
							raddr->node.addrlen,
							nm, sizeof(nm));
						verbose(VERB_ALGO, "respip: rpz: response-ip trigger %s/%d on %s %s with action %s", nm, raddr->node.net, qn, ip, rpz_action_to_string(respip_action_to_rpz_action(action)));
					}
					/* break to make sure 'a' stays pointed
					 * to used auth_zone, and keeps lock */
					break;
				}
				lock_rw_unlock(&raddr->lock);
				raddr = NULL;
				actinfo->rpz_disabled++;
			}
		}
		lock_rw_unlock(&a->lock);
	}
	lock_rw_unlock(&az->rpz_lock);
	if(raddr && !search_only) {
		int result = 0;

		/* first, see if we have response-ip or tag action for the
		 * action except for 'always' variants. */
		if(action != respip_always_refuse
			&& action != respip_always_transparent
			&& action != respip_always_nxdomain
			&& action != respip_always_nodata
			&& action != respip_always_deny
			&& (result = respip_data_answer(action,
			(data) ? data : raddr->data, qinfo->qtype, rep,
			rrset_id, new_repp, tag, tag_datas, tag_datas_size,
			ipset->tagname, ipset->num_tags, &redirect_rrset,
			region)) < 0) {
			ret = 0;
			goto done;
		}

		/* if no action data applied, take action specific to the
		 * action without data. */
		if(!result && !respip_nodata_answer(qinfo->qtype, action, rep,
			rrset_id, new_repp, region)) {
			ret = 0;
			goto done;
		}
	}
  done:
	if(view) {
		lock_rw_unlock(&view->lock);
	}
	if(ret) {
		/* If we're redirecting the original answer to a
		 * CNAME, record the CNAME rrset so the caller can take
		 * the appropriate action.  Note that we don't check the
		 * action type; it should normally be 'redirect', but it
		 * can be of other type when a data-dependent tag action
		 * uses redirect response-ip data.
		 */
		if(redirect_rrset &&
			redirect_rrset->rk.type == ntohs(LDNS_RR_TYPE_CNAME) &&
			qinfo->qtype != LDNS_RR_TYPE_ANY)
			*alias_rrset = redirect_rrset;
		/* on success, populate respip result structure */
		ret = populate_action_info(actinfo, action, raddr,
			redirect_rrset, tag, ipset, search_only, region,
				rpz_used, rpz_log, log_name, rpz_cname_override);
	}
	if(raddr) {
		lock_rw_unlock(&raddr->lock);
	}
	if(rpz_used) {
		lock_rw_unlock(&a->lock);
	}
	return ret;
}

static int
generate_cname_request(struct module_qstate* qstate,
	struct ub_packed_rrset_key* alias_rrset)
{
	struct module_qstate* subq = NULL;
	struct query_info subqi;

	memset(&subqi, 0, sizeof(subqi));
	get_cname_target(alias_rrset, &subqi.qname, &subqi.qname_len);
	if(!subqi.qname)
		return 0;    /* unexpected: not a valid CNAME RDATA */
	subqi.qtype = qstate->qinfo.qtype;
	subqi.qclass = qstate->qinfo.qclass;
	fptr_ok(fptr_whitelist_modenv_attach_sub(qstate->env->attach_sub));
	return (*qstate->env->attach_sub)(qstate, &subqi, BIT_RD, 0, 0, &subq);
}

void
respip_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound)
{
	struct respip_qstate* rq = (struct respip_qstate*)qstate->minfo[id];

	log_query_info(VERB_QUERY, "respip operate: query", &qstate->qinfo);
	(void)outbound;

	if(event == module_event_new || event == module_event_pass) {
		if(!rq) {
			rq = regional_alloc_zero(qstate->region, sizeof(*rq));
			if(!rq)
				goto servfail;
			rq->state = RESPIP_INIT;
			qstate->minfo[id] = rq;
		}
		if(rq->state == RESPIP_SUBQUERY_FINISHED) {
			qstate->ext_state[id] = module_finished;
			return;
		}
		verbose(VERB_ALGO, "respip: pass to next module");
		qstate->ext_state[id] = module_wait_module;
	} else if(event == module_event_moddone) {
		/* If the reply may be subject to response-ip rewriting
		 * according to the query type, check the actions.  If a
		 * rewrite is necessary, we'll replace the reply in qstate
		 * with the new one. */
		enum module_ext_state next_state = module_finished;

		if((qstate->qinfo.qtype == LDNS_RR_TYPE_A ||
			qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA ||
			qstate->qinfo.qtype == LDNS_RR_TYPE_ANY) &&
			qstate->return_msg && qstate->return_msg->rep) {
			struct reply_info* new_rep = qstate->return_msg->rep;
			struct ub_packed_rrset_key* alias_rrset = NULL;
			struct respip_action_info actinfo = {0, 0, 0, 0, NULL, 0, NULL};
			actinfo.action = respip_none;

			if(!respip_rewrite_reply(&qstate->qinfo,
				qstate->client_info, qstate->return_msg->rep,
				&new_rep, &actinfo, &alias_rrset, 0,
				qstate->region, qstate->env->auth_zones,
				&qstate->rpz_passthru, qstate->env->views,
				qstate->env->respip_set)) {
				goto servfail;
			}
			if(actinfo.action != respip_none) {
				/* save action info for logging on a
				 * per-front-end-query basis */
				if(!(qstate->respip_action_info =
					regional_alloc_init(qstate->region,
						&actinfo, sizeof(actinfo))))
				{
					log_err("out of memory");
					goto servfail;
				}
			} else {
				qstate->respip_action_info = NULL;
			}
			if (actinfo.action == respip_always_deny ||
				(new_rep == qstate->return_msg->rep &&
				(actinfo.action == respip_deny ||
				actinfo.action == respip_inform_deny))) {
				/* for deny-variant actions (unless response-ip
				 * data is applied), mark the query state so
				 * the response will be dropped for all
				 * clients. */
				qstate->is_drop = 1;
			} else if(alias_rrset) {
				if(!generate_cname_request(qstate, alias_rrset))
					goto servfail;
				next_state = module_wait_subquery;
			}
			qstate->return_msg->rep = new_rep;
		}
		qstate->ext_state[id] = next_state;
	} else
		qstate->ext_state[id] = module_finished;

	return;

  servfail:
	qstate->return_rcode = LDNS_RCODE_SERVFAIL;
	qstate->return_msg = NULL;
}

int
respip_merge_cname(struct reply_info* base_rep,
	const struct query_info* qinfo, const struct reply_info* tgt_rep,
	const struct respip_client_info* cinfo, int must_validate,
	struct reply_info** new_repp, struct regional* region,
	struct auth_zones* az, struct views* views,
	struct respip_set* respip_set)
{
	struct reply_info* new_rep;
	struct reply_info* tmp_rep = NULL; /* just a placeholder */
	struct ub_packed_rrset_key* alias_rrset = NULL; /* ditto */
	uint16_t tgt_rcode;
	size_t i, j;
	struct respip_action_info actinfo = {0, 0, 0, 0, NULL, 0, NULL};
	actinfo.action = respip_none;

	/* If the query for the CNAME target would result in an unusual rcode,
	 * we generally translate it as a failure for the base query
	 * (which would then be translated into SERVFAIL).  The only exception
	 * is NXDOMAIN and YXDOMAIN, which are passed to the end client(s).
	 * The YXDOMAIN case would be rare but still possible (when
	 * DNSSEC-validated DNAME has been cached but synthesizing CNAME
	 * can't be generated due to length limitation) */
	tgt_rcode = FLAGS_GET_RCODE(tgt_rep->flags);
	if((tgt_rcode != LDNS_RCODE_NOERROR &&
		tgt_rcode != LDNS_RCODE_NXDOMAIN &&
		tgt_rcode != LDNS_RCODE_YXDOMAIN) ||
		(must_validate && tgt_rep->security <= sec_status_bogus)) {
		return 0;
	}

	/* see if the target reply would be subject to a response-ip action. */
	if(!respip_rewrite_reply(qinfo, cinfo, tgt_rep, &tmp_rep, &actinfo,
		&alias_rrset, 1, region, az, NULL, views, respip_set))
		return 0;
	if(actinfo.action != respip_none) {
		log_info("CNAME target of redirect response-ip action would "
			"be subject to response-ip action, too; stripped");
		*new_repp = base_rep;
		return 1;
	}

	/* Append target reply to the base.  Since we cannot assume
	 * tgt_rep->rrsets is valid throughout the lifetime of new_rep
	 * or it can be safely shared by multiple threads, we need to make a
	 * deep copy. */
	new_rep = make_new_reply_info(base_rep, region,
		base_rep->an_numrrsets + tgt_rep->an_numrrsets,
		base_rep->an_numrrsets);
	if(!new_rep)
		return 0;
	for(i=0,j=base_rep->an_numrrsets; i<tgt_rep->an_numrrsets; i++,j++) {
		new_rep->rrsets[j] = respip_copy_rrset(tgt_rep->rrsets[i], region);
		if(!new_rep->rrsets[j])
			return 0;
	}

	FLAGS_SET_RCODE(new_rep->flags, tgt_rcode);
	*new_repp = new_rep;
	return 1;
}

void
respip_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super)
{
	struct respip_qstate* rq = (struct respip_qstate*)super->minfo[id];
	struct reply_info* new_rep = NULL;

	rq->state = RESPIP_SUBQUERY_FINISHED;

	/* respip subquery should have always been created with a valid reply
	 * in super. */
	log_assert(super->return_msg && super->return_msg->rep);

	/* return_msg can be NULL when, e.g., the sub query resulted in
	 * SERVFAIL, in which case we regard it as a failure of the original
	 * query.  Other checks are probably redundant, but we check them
	 * for safety. */
	if(!qstate->return_msg || !qstate->return_msg->rep ||
		qstate->return_rcode != LDNS_RCODE_NOERROR)
		goto fail;

	if(!respip_merge_cname(super->return_msg->rep, &qstate->qinfo,
		qstate->return_msg->rep, super->client_info,
		super->env->need_to_validate, &new_rep, super->region,
		qstate->env->auth_zones, qstate->env->views,
		qstate->env->respip_set))
		goto fail;
	super->return_msg->rep = new_rep;
	return;

  fail:
	super->return_rcode = LDNS_RCODE_SERVFAIL;
	super->return_msg = NULL;
	return;
}

void
respip_clear(struct module_qstate* qstate, int id)
{
	qstate->minfo[id] = NULL;
}

size_t
respip_get_mem(struct module_env* env, int id)
{
	(void)env;
	(void)id;
	return 0;
}

/**
 * The response-ip function block
 */
static struct module_func_block respip_block = {
	"respip",
	NULL, NULL, &respip_init, &respip_deinit, &respip_operate,
	&respip_inform_super, &respip_clear, &respip_get_mem
};

struct module_func_block*
respip_get_funcblock(void)
{
	return &respip_block;
}

enum respip_action
resp_addr_get_action(const struct resp_addr* addr)
{
	return addr ? addr->action : respip_none;
}

struct ub_packed_rrset_key*
resp_addr_get_rrset(struct resp_addr* addr)
{
	return addr ? addr->data : NULL;
}

int
respip_set_is_empty(const struct respip_set* set)
{
	return set ? set->ip_tree.count == 0 : 1;
}

void
respip_inform_print(struct respip_action_info* respip_actinfo, uint8_t* qname,
	uint16_t qtype, uint16_t qclass, struct local_rrset* local_alias,
	struct sockaddr_storage* addr, socklen_t addrlen)
{
	char srcip[128], respip[128], txt[512];
	unsigned port;
	struct respip_addr_info* respip_addr = respip_actinfo->addrinfo;
	size_t txtlen = 0;
	const char* actionstr = NULL;

	if(local_alias)
		qname = local_alias->rrset->rk.dname;
	port = (unsigned)((addr->ss_family == AF_INET) ?
		ntohs(((struct sockaddr_in*)addr)->sin_port) :
		ntohs(((struct sockaddr_in6*)addr)->sin6_port));
	addr_to_str(addr, addrlen, srcip, sizeof(srcip));
	addr_to_str(&respip_addr->addr, respip_addr->addrlen,
		respip, sizeof(respip));
	if(respip_actinfo->rpz_log) {
		txtlen += snprintf(txt+txtlen, sizeof(txt)-txtlen, "%s",
			"rpz: applied ");
		if(respip_actinfo->rpz_cname_override)
			actionstr = rpz_action_to_string(
				RPZ_CNAME_OVERRIDE_ACTION);
		else
			actionstr = rpz_action_to_string(
				respip_action_to_rpz_action(
					respip_actinfo->action));
	}
	if(respip_actinfo->log_name) {
		txtlen += snprintf(txt+txtlen, sizeof(txt)-txtlen,
			"[%s] ", respip_actinfo->log_name);
	}
	snprintf(txt+txtlen, sizeof(txt)-txtlen,
		"%s/%d %s %s@%u", respip, respip_addr->net,
		(actionstr) ? actionstr : "inform", srcip, port);
	log_nametypeclass(NO_VERBOSE, txt, qname, qtype, qclass);
}

size_t respip_set_get_mem(struct respip_set* set)
{
	size_t m;
	if(!set) return 0;
	m = sizeof(*set);
	lock_rw_rdlock(&set->lock);
	m += regional_get_mem(set->region);
	lock_rw_unlock(&set->lock);
	return m;
}

void
respip_set_swap_tree(struct respip_set* respip_set,
	struct respip_set* data)
{
	rbnode_type* oldroot = respip_set->ip_tree.root;
	size_t oldcount = respip_set->ip_tree.count;
	struct regional* oldregion = respip_set->region;
	char* const* oldtagname = respip_set->tagname;
	int oldnum_tags = respip_set->num_tags;
	respip_set->ip_tree.root = data->ip_tree.root;
	respip_set->ip_tree.count = data->ip_tree.count;
	respip_set->region = data->region;
	respip_set->tagname = data->tagname;
	respip_set->num_tags = data->num_tags;
	data->ip_tree.root = oldroot;
	data->ip_tree.count = oldcount;
	data->region = oldregion;
	data->tagname = oldtagname;
	data->num_tags = oldnum_tags;
}
