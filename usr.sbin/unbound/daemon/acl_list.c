/*
 * daemon/acl_list.h - client access control storage for the server.
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
 * This file helps the server keep out queries from outside sources, that
 * should not be answered.
 */
#include "config.h"
#include "daemon/acl_list.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "services/localzone.h"
#include "services/listen_dnsport.h"
#include "sldns/str2wire.h"

struct acl_list*
acl_list_create(void)
{
	struct acl_list* acl = (struct acl_list*)calloc(1,
		sizeof(struct acl_list));
	if(!acl)
		return NULL;
	acl->region = regional_create();
	if(!acl->region) {
		acl_list_delete(acl);
		return NULL;
	}
	return acl;
}

void
acl_list_delete(struct acl_list* acl)
{
	if(!acl)
		return;
	regional_destroy(acl->region);
	free(acl);
}

/** insert new address into acl_list structure */
static struct acl_addr*
acl_list_insert(struct acl_list* acl, struct sockaddr_storage* addr,
	socklen_t addrlen, int net, enum acl_access control,
	int complain_duplicates)
{
	struct acl_addr* node = regional_alloc_zero(acl->region,
		sizeof(struct acl_addr));
	if(!node)
		return NULL;
	node->control = control;
	if(!addr_tree_insert(&acl->tree, &node->node, addr, addrlen, net)) {
		if(complain_duplicates)
			verbose(VERB_QUERY, "duplicate acl address ignored.");
	}
	return node;
}

/** parse str to acl_access enum */
static int
parse_acl_access(const char* str, enum acl_access* control)
{
	if(strcmp(str, "allow") == 0)
		*control = acl_allow;
	else if(strcmp(str, "deny") == 0)
		*control = acl_deny;
	else if(strcmp(str, "refuse") == 0)
		*control = acl_refuse;
	else if(strcmp(str, "deny_non_local") == 0)
		*control = acl_deny_non_local;
	else if(strcmp(str, "refuse_non_local") == 0)
		*control = acl_refuse_non_local;
	else if(strcmp(str, "allow_snoop") == 0)
		*control = acl_allow_snoop;
	else if(strcmp(str, "allow_setrd") == 0)
		*control = acl_allow_setrd;
	else if (strcmp(str, "allow_cookie") == 0)
		*control = acl_allow_cookie;
	else {
		log_err("access control type %s unknown", str);
		return 0;
	}
	return 1;
}

/** apply acl_list string */
static int
acl_list_str_cfg(struct acl_list* acl, const char* str, const char* s2,
	int complain_duplicates)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	enum acl_access control;
	if(!parse_acl_access(s2, &control)) {
		return 0;
	}
	if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
		log_err("cannot parse access control: %s %s", str, s2);
		return 0;
	}
	if(!acl_list_insert(acl, &addr, addrlen, net, control,
		complain_duplicates)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** find or create node (NULL on parse or error) */
static struct acl_addr*
acl_find_or_create_str2addr(struct acl_list* acl, const char* str,
	int is_interface, int port)
{
	struct acl_addr* node;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int net = (str_is_ip6(str)?128:32);
	if(is_interface) {
		if(!extstrtoaddr(str, &addr, &addrlen, port)) {
			log_err("cannot parse interface: %s", str);
			return NULL;
		}
	} else {
		if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
			log_err("cannot parse netblock: %s", str);
			return NULL;
		}
	}
	/* find or create node */
	if(!(node=(struct acl_addr*)addr_tree_find(&acl->tree, &addr,
		addrlen, net)) && !is_interface) {
		/* create node, type 'allow' since otherwise tags are
		 * pointless, can override with specific access-control: cfg */
		if(!(node=(struct acl_addr*)acl_list_insert(acl, &addr,
			addrlen, net, acl_allow, 1))) {
			log_err("out of memory");
			return NULL;
		}
	}
	return node;
}

/** find or create node (NULL on error) */
static struct acl_addr*
acl_find_or_create(struct acl_list* acl, struct sockaddr_storage* addr,
	socklen_t addrlen, enum acl_access control)
{
	struct acl_addr* node;
	int net = (addr_is_ip6(addr, addrlen)?128:32);
	/* find or create node */
	if(!(node=(struct acl_addr*)addr_tree_find(&acl->tree, addr,
		addrlen, net))) {
		/* create node;
		 * can override with specific access-control: cfg */
		if(!(node=(struct acl_addr*)acl_list_insert(acl, addr,
			addrlen, net, control, 1))) {
			log_err("out of memory");
			return NULL;
		}
	}
	return node;
}

/** apply acl_interface string */
static int
acl_interface_str_cfg(struct acl_list* acl_interface, const char* iface,
	const char* s2, int port)
{
	struct acl_addr* node;
	enum acl_access control;
	if(!parse_acl_access(s2, &control)) {
		return 0;
	}
	if(!(node=acl_find_or_create_str2addr(acl_interface, iface, 1, port))) {
		log_err("cannot update ACL on non-configured interface: %s %d",
			iface, port);
		return 0;
	}
	node->control = control;
	return 1;
}

struct acl_addr*
acl_interface_insert(struct acl_list* acl_interface,
	struct sockaddr_storage* addr, socklen_t addrlen,
	enum acl_access control)
{
	struct acl_addr* node = acl_find_or_create(acl_interface, addr, addrlen, control);
	node->is_interface = 1;
	return node;
}

/** apply acl_tag string */
static int
acl_list_tags_cfg(struct acl_list* acl, const char* str, uint8_t* bitmap,
	size_t bitmaplen, int is_interface, int port)
{
	struct acl_addr* node;
	if(!(node=acl_find_or_create_str2addr(acl, str, is_interface, port))) {
		if(is_interface)
			log_err("non-configured interface: %s", str);
		return 0;
	}
	node->taglen = bitmaplen;
	node->taglist = regional_alloc_init(acl->region, bitmap, bitmaplen);
	if(!node->taglist) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** apply acl_view string */
static int
acl_list_view_cfg(struct acl_list* acl, const char* str, const char* str2,
	struct views* vs, int is_interface, int port)
{
	struct acl_addr* node;
	if(!(node=acl_find_or_create_str2addr(acl, str, is_interface, port))) {
		if(is_interface)
			log_err("non-configured interface: %s", str);
		return 0;
	}
	node->view = views_find_view(vs, str2, 0 /* get read lock*/);
	if(!node->view) {
		log_err("no view with name: %s", str2);
		return 0;
	}
	lock_rw_unlock(&node->view->lock);
	return 1;
}

/** apply acl_tag_action string */
static int
acl_list_tag_action_cfg(struct acl_list* acl, struct config_file* cfg,
	const char* str, const char* tag, const char* action,
	int is_interface, int port)
{
	struct acl_addr* node;
	int tagid;
	enum localzone_type t;
	if(!(node=acl_find_or_create_str2addr(acl, str, is_interface, port))) {
		if(is_interface)
			log_err("non-configured interface: %s", str);
		return 0;
	}
	/* allocate array if not yet */
	if(!node->tag_actions) {
		node->tag_actions = (uint8_t*)regional_alloc_zero(acl->region,
			sizeof(*node->tag_actions)*cfg->num_tags);
		if(!node->tag_actions) {
			log_err("out of memory");
			return 0;
		}
		node->tag_actions_size = (size_t)cfg->num_tags;
	}
	/* parse tag */
	if((tagid=find_tag_id(cfg, tag)) == -1) {
		log_err("cannot parse tag (define-tag it): %s %s", str, tag);
		return 0;
	}
	if((size_t)tagid >= node->tag_actions_size) {
		log_err("tagid too large for array %s %s", str, tag);
		return 0;
	}
	if(!local_zone_str2type(action, &t)) {
		log_err("cannot parse access control action type: %s %s %s",
			str, tag, action);
		return 0;
	}
	node->tag_actions[tagid] = (uint8_t)t;
	return 1;
}

/** check wire data parse */
static int
check_data(const char* data, const struct config_strlist* head)
{
	char buf[65536];
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t len = sizeof(rr);
	int res;
	/* '.' is sufficient for validation, and it makes the call to
	 * sldns_wirerr_get_type() simpler below. */
	snprintf(buf, sizeof(buf), "%s %s", ".", data);
	res = sldns_str2wire_rr_buf(buf, rr, &len, NULL, 3600, NULL, 0,
		NULL, 0);

	/* Reject it if we would end up having CNAME and other data (including
	 * another CNAME) for the same tag. */
	if(res == 0 && head) {
		const char* err_data = NULL;

		if(sldns_wirerr_get_type(rr, len, 1) == LDNS_RR_TYPE_CNAME) {
			/* adding CNAME while other data already exists. */
			err_data = data;
		} else {
			snprintf(buf, sizeof(buf), "%s %s", ".", head->str);
			len = sizeof(rr);
			res = sldns_str2wire_rr_buf(buf, rr, &len, NULL, 3600,
				NULL, 0, NULL, 0);
			if(res != 0) {
				/* This should be impossible here as head->str
				 * has been validated, but we check it just in
				 * case. */
				return 0;
			}
			if(sldns_wirerr_get_type(rr, len, 1) ==
				LDNS_RR_TYPE_CNAME) /* already have CNAME */
				err_data = head->str;
		}
		if(err_data) {
			log_err("redirect tag data '%s' must not coexist with "
				"other data.", err_data);
			return 0;
		}
	}
	if(res == 0)
		return 1;
	log_err("rr data [char %d] parse error %s",
		(int)LDNS_WIREPARSE_OFFSET(res)-2,
		sldns_get_errorstr_parse(res));
	return 0;
}

/** apply acl_tag_data string */
static int
acl_list_tag_data_cfg(struct acl_list* acl, struct config_file* cfg,
	const char* str, const char* tag, const char* data,
	int is_interface, int port)
{
	struct acl_addr* node;
	int tagid;
	char* dupdata;
	if(!(node=acl_find_or_create_str2addr(acl, str, is_interface, port))) {
		if(is_interface)
			log_err("non-configured interface: %s", str);
		return 0;
	}
	/* allocate array if not yet */
	if(!node->tag_datas) {
		node->tag_datas = (struct config_strlist**)regional_alloc_zero(
			acl->region, sizeof(*node->tag_datas)*cfg->num_tags);
		if(!node->tag_datas) {
			log_err("out of memory");
			return 0;
		}
		node->tag_datas_size = (size_t)cfg->num_tags;
	}
	/* parse tag */
	if((tagid=find_tag_id(cfg, tag)) == -1) {
		log_err("cannot parse tag (define-tag it): %s %s", str, tag);
		return 0;
	}
	if((size_t)tagid >= node->tag_datas_size) {
		log_err("tagid too large for array %s %s", str, tag);
		return 0;
	}

	/* check data? */
	if(!check_data(data, node->tag_datas[tagid])) {
		log_err("cannot parse access-control-tag data: %s %s '%s'",
			str, tag, data);
		return 0;
	}

	dupdata = regional_strdup(acl->region, data);
	if(!dupdata) {
		log_err("out of memory");
		return 0;
	}
	if(!cfg_region_strlist_insert(acl->region,
		&(node->tag_datas[tagid]), dupdata)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** read acl_list config */
static int
read_acl_list(struct acl_list* acl, struct config_str2list* acls)
{
	struct config_str2list* p;
	for(p = acls; p; p = p->next) {
		log_assert(p->str && p->str2);
		if(!acl_list_str_cfg(acl, p->str, p->str2, 1))
			return 0;
	}
	return 1;
}

/** read acl view config */
static int
read_acl_view(struct acl_list* acl, struct config_str2list** acl_view,
	struct views* v)
{
	struct config_str2list* np, *p = *acl_view;
	*acl_view = NULL;
	while(p) {
		log_assert(p->str && p->str2);
		if(!acl_list_view_cfg(acl, p->str, p->str2, v, 0, 0)) {
			config_deldblstrlist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tags config */
static int
read_acl_tags(struct acl_list* acl, struct config_strbytelist** acl_tags)
{
	struct config_strbytelist* np, *p = *acl_tags;
	*acl_tags = NULL;
	while(p) {
		log_assert(p->str && p->str2);
		if(!acl_list_tags_cfg(acl, p->str, p->str2, p->str2len, 0, 0)) {
			config_del_strbytelist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag actions config */
static int
read_acl_tag_actions(struct acl_list* acl, struct config_file* cfg,
	struct config_str3list** acl_tag_actions)
{
	struct config_str3list* p, *np;
	p = *acl_tag_actions;
	*acl_tag_actions = NULL;
	while(p) {
		log_assert(p->str && p->str2 && p->str3);
		if(!acl_list_tag_action_cfg(acl, cfg, p->str, p->str2,
			p->str3, 0, 0)) {
			config_deltrplstrlist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag datas config */
static int
read_acl_tag_datas(struct acl_list* acl, struct config_file* cfg,
	struct config_str3list** acl_tag_datas)
{
	struct config_str3list* p, *np;
	p = *acl_tag_datas;
	*acl_tag_datas = NULL;
	while(p) {
		log_assert(p->str && p->str2 && p->str3);
		if(!acl_list_tag_data_cfg(acl, cfg, p->str, p->str2, p->str3,
			0, 0)) {
			config_deltrplstrlist(p);
			return 0;
		}
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

int
acl_list_apply_cfg(struct acl_list* acl, struct config_file* cfg,
	struct views* v)
{
	regional_free_all(acl->region);
	addr_tree_init(&acl->tree);
	if(!read_acl_list(acl, cfg->acls))
		return 0;
	if(!read_acl_view(acl, &cfg->acl_view, v))
		return 0;
	if(!read_acl_tags(acl, &cfg->acl_tags))
		return 0;
	if(!read_acl_tag_actions(acl, cfg, &cfg->acl_tag_actions))
		return 0;
	if(!read_acl_tag_datas(acl, cfg, &cfg->acl_tag_datas))
		return 0;
	/* insert defaults, with '0' to ignore them if they are duplicates */
	/* the 'refuse' defaults for /0 are now done per interface instead */
	if(!acl_list_str_cfg(acl, "127.0.0.0/8", "allow", 0))
		return 0;
	if(cfg->do_ip6) {
		if(!acl_list_str_cfg(acl, "::1", "allow", 0))
			return 0;
		if(!acl_list_str_cfg(acl, "::ffff:127.0.0.1", "allow", 0))
			return 0;
	}
	addr_tree_init_parents(&acl->tree);
	return 1;
}

void
acl_interface_init(struct acl_list* acl_interface)
{
	regional_free_all(acl_interface->region);
	/* We want comparison in the tree to include only address and port.
	 * We don't care about comparing node->net. All addresses in the
	 * acl_interface->tree should have either 32 (ipv4) or 128 (ipv6).
	 * Initialise with the appropriate compare function but keep treating
	 * it as an addr_tree. */
	addr_tree_addrport_init(&acl_interface->tree);
}

static int
read_acl_interface_action(struct acl_list* acl_interface,
	struct config_str2list* acls, int port)
{
	struct config_str2list* p;
	for(p = acls; p; p = p->next) {
		char** resif = NULL;
		int num_resif = 0;
		int i;
		log_assert(p->str && p->str2);
		if(!resolve_interface_names(&p->str, 1, NULL, &resif, &num_resif))
			return 0;
		for(i = 0; i<num_resif; i++) {
			if(!acl_interface_str_cfg(acl_interface, resif[i], p->str2, port)){
				config_del_strarray(resif, num_resif);
				return 0;
			}
		}
		config_del_strarray(resif, num_resif);
	}
	return 1;
}

/** read acl view config for interface */
static int
read_acl_interface_view(struct acl_list* acl_interface,
	struct config_str2list** acl_view,
	struct views* v, int port)
{
	struct config_str2list* np, *p = *acl_view;
	*acl_view = NULL;
	while(p) {
		char** resif = NULL;
		int num_resif = 0;
		int i;
		log_assert(p->str && p->str2);
		if(!resolve_interface_names(&p->str, 1, NULL, &resif, &num_resif)) {
			config_deldblstrlist(p);
			return 0;
		}
		for(i = 0; i<num_resif; i++) {
			if(!acl_list_view_cfg(acl_interface, resif[i], p->str2,
				v, 1, port)) {
				config_del_strarray(resif, num_resif);
				config_deldblstrlist(p);
				return 0;
			}
		}
		config_del_strarray(resif, num_resif);
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tags config for interface */
static int
read_acl_interface_tags(struct acl_list* acl_interface,
	struct config_strbytelist** acl_tags, int port)
{
	struct config_strbytelist* np, *p = *acl_tags;
	*acl_tags = NULL;
	while(p) {
		char** resif = NULL;
		int num_resif = 0;
		int i;
		log_assert(p->str && p->str2);
		if(!resolve_interface_names(&p->str, 1, NULL, &resif, &num_resif)) {
			config_del_strbytelist(p);
			return 0;
		}
		for(i = 0; i<num_resif; i++) {
			if(!acl_list_tags_cfg(acl_interface, resif[i], p->str2,
				p->str2len, 1, port)) {
				config_del_strbytelist(p);
				config_del_strarray(resif, num_resif);
				return 0;
			}
		}
		config_del_strarray(resif, num_resif);
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag actions config for interface*/
static int
read_acl_interface_tag_actions(struct acl_list* acl_interface,
	struct config_file* cfg,
	struct config_str3list** acl_tag_actions, int port)
{
	struct config_str3list* p, *np;
	p = *acl_tag_actions;
	*acl_tag_actions = NULL;
	while(p) {
		char** resif = NULL;
		int num_resif = 0;
		int i;
		log_assert(p->str && p->str2 && p->str3);
		if(!resolve_interface_names(&p->str, 1, NULL, &resif, &num_resif)) {
			config_deltrplstrlist(p);
			return 0;
		}
		for(i = 0; i<num_resif; i++) {
			if(!acl_list_tag_action_cfg(acl_interface, cfg,
				resif[i], p->str2, p->str3, 1, port)) {
				config_deltrplstrlist(p);
				config_del_strarray(resif, num_resif);
				return 0;
			}
		}
		config_del_strarray(resif, num_resif);
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

/** read acl tag datas config for interface */
static int
read_acl_interface_tag_datas(struct acl_list* acl_interface,
	struct config_file* cfg,
	struct config_str3list** acl_tag_datas, int port)
{
	struct config_str3list* p, *np;
	p = *acl_tag_datas;
	*acl_tag_datas = NULL;
	while(p) {
		char** resif = NULL;
		int num_resif = 0;
		int i;
		log_assert(p->str && p->str2 && p->str3);
		if(!resolve_interface_names(&p->str, 1, NULL, &resif, &num_resif)) {
			config_deltrplstrlist(p);
			return 0;
		}
		for(i = 0; i<num_resif; i++) {
			if(!acl_list_tag_data_cfg(acl_interface, cfg,
				resif[i], p->str2, p->str3, 1, port)) {
				config_deltrplstrlist(p);
				config_del_strarray(resif, num_resif);
				return 0;
			}
		}
		config_del_strarray(resif, num_resif);
		/* free the items as we go to free up memory */
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
	return 1;
}

int
acl_interface_apply_cfg(struct acl_list* acl_interface, struct config_file* cfg,
	struct views* v)
{
	if(!read_acl_interface_action(acl_interface, cfg->interface_actions,
		cfg->port))
		return 0;
	if(!read_acl_interface_view(acl_interface, &cfg->interface_view, v,
		cfg->port))
		return 0;
	if(!read_acl_interface_tags(acl_interface, &cfg->interface_tags,
		cfg->port))
		return 0;
	if(!read_acl_interface_tag_actions(acl_interface, cfg,
		&cfg->interface_tag_actions, cfg->port))
		return 0;
	if(!read_acl_interface_tag_datas(acl_interface, cfg,
		&cfg->interface_tag_datas, cfg->port))
		return 0;
	addr_tree_init_parents(&acl_interface->tree);
	return 1;
}

enum acl_access
acl_get_control(struct acl_addr* acl)
{
	if(acl) return acl->control;
	return acl_deny;
}

struct acl_addr*
acl_addr_lookup(struct acl_list* acl, struct sockaddr_storage* addr,
        socklen_t addrlen)
{
	return (struct acl_addr*)addr_tree_lookup(&acl->tree,
		addr, addrlen);
}

size_t
acl_list_get_mem(struct acl_list* acl)
{
	if(!acl) return 0;
	return sizeof(*acl) + regional_get_mem(acl->region);
}

const char* acl_access_to_str(enum acl_access acl)
{
	switch(acl) {
	case acl_deny: return "deny";
	case acl_refuse: return "refuse";
	case acl_deny_non_local: return "deny_non_local";
	case acl_refuse_non_local: return "refuse_non_local";
	case acl_allow: return "allow";
	case acl_allow_snoop: return "allow_snoop";
	case acl_allow_setrd: return "allow_setrd";
	default: break;
	}
	return "unknown";
}

void
log_acl_action(const char* action, struct sockaddr_storage* addr,
	socklen_t addrlen, enum acl_access acl, struct acl_addr* acladdr)
{
	char a[128], n[128];
	uint16_t port;
	addr_to_str(addr, addrlen, a, sizeof(a));
	port = ntohs(((struct sockaddr_in*)addr)->sin_port);
	if(acladdr) {
		addr_to_str(&acladdr->node.addr, acladdr->node.addrlen,
			n, sizeof(n));
		verbose(VERB_ALGO, "%s query from %s port %d because of "
			"%s/%d %s%s", action, a, (int)port, n,
			acladdr->node.net,
			acladdr->is_interface?"(ACL on interface IP) ":"",
			acl_access_to_str(acl));
	} else {
		verbose(VERB_ALGO, "%s query from %s port %d", action, a,
			(int)port);
	}
}

void acl_list_swap_tree(struct acl_list* acl, struct acl_list* data)
{
	/* swap tree and region */
	rbtree_type oldtree = acl->tree;
	struct regional* oldregion = acl->region;
	acl->tree = data->tree;
	acl->region = data->region;
	data->tree = oldtree;
	data->region = oldregion;
}
