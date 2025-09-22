/*
 * edns-subnet/subnetmod.h - edns subnet module. Must be called before validator
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

#ifndef SUBNETMOD_H
#define SUBNETMOD_H
#include "util/module.h"
#include "services/outbound_list.h"
#include "util/alloc.h"
#include "util/net_help.h"
#include "util/storage/slabhash.h"
#include "util/data/dname.h"
#include "edns-subnet/addrtree.h"
#include "edns-subnet/edns-subnet.h"

/**
 * Global state for the subnet module.
 */
struct subnet_env {
	/** shared message cache
	 * key: struct query_info*
	 * data: struct subnet_msg_cache_data* */
	struct slabhash* subnet_msg_cache;
	/** access control, which upstream servers we send client address */
	struct ecs_whitelist* whitelist;
	/** allocation service */
	struct alloc_cache alloc;
	lock_rw_type biglock;
	/** number of messages from cache */
	size_t num_msg_cache;
	/** number of messages not from cache */
	size_t num_msg_nocache;
};

struct subnet_msg_cache_data {
	struct addrtree* tree4;
	struct addrtree* tree6;
};

struct subnet_qstate {
	/** We need the hash for both cache lookup and insert */
	hashvalue_type qinfo_hash;
	int qinfo_hash_calculated;
	/** ecs_data for client communication */
	struct ecs_data	ecs_client_in;
	struct ecs_data	ecs_client_out;
	/** ecss data for server communication */
	struct ecs_data	ecs_server_in;
	struct ecs_data	ecs_server_out;
	int subnet_downstream;
	int subnet_sent;
	/**
	 * If there was no subnet sent because the client used source prefix
	 * length 0 for omitting the information. Then the answer is cached
	 * like subnet was a /0 scope. Like the subnet_sent flag, but when
	 * the EDNS subnet option is omitted because the client asked.
	 */
	int subnet_sent_no_subnet;
	/** keep track of longest received scope, set after receiving CNAME for
	 * incoming QNAME. */
	int track_max_scope;
	/** longest received scope mask since track_max_scope is set. This value
	 * is used for caching and answereing to client. */
	uint8_t max_scope;
	/** has the subnet module been started with no_cache_store? */
	int started_no_cache_store;
	/** has the subnet module been started with no_cache_lookup? */
	int started_no_cache_lookup;
	/** Wait for subquery that has been started for nonsubnet lookup. */
	int wait_subquery;
	/** The subquery waited for is done. */
	int wait_subquery_done;
};

void subnet_data_delete(void* d, void* ATTR_UNUSED(arg));
size_t msg_cache_sizefunc(void* k, void* d);

/**
 * Get the module function block.
 * @return: function block with function pointers to module methods.
 */
struct module_func_block* subnetmod_get_funcblock(void);

/** subnet module init */
int subnetmod_init(struct module_env* env, int id);

/** subnet module deinit */
void subnetmod_deinit(struct module_env* env, int id);

/** subnet module operate on a query */
void subnetmod_operate(struct module_qstate* qstate, enum module_ev event,
	int id, struct outbound_entry* outbound);

/** subnet module  */
void subnetmod_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

/** subnet module cleanup query state */
void subnetmod_clear(struct module_qstate* qstate, int id);

/** subnet module alloc size routine */
size_t subnetmod_get_mem(struct module_env* env, int id);

/** Wrappers for static functions to unit test */
size_t unittest_wrapper_subnetmod_sizefunc(void *elemptr);

/** Whitelist check, called just before query is sent upstream. */
int ecs_whitelist_check(struct query_info* qinfo, uint16_t flags,
	struct module_qstate* qstate, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* zone, size_t zonelen,
	struct regional* region, int id, void* cbargs);

/** Check whether response from server contains ECS record, if so, skip cache
 * store. Called just after parsing EDNS data from server. */
int ecs_edns_back_parsed(struct module_qstate* qstate, int id, void* cbargs);

/** Remove ECS record from back_out when query resulted in REFUSED response. */
int ecs_query_response(struct module_qstate* qstate, struct dns_msg* response,
	int id, void* cbargs);

/** mark subnet msg to be deleted */
void subnet_markdel(void* key);

/** Add ecs struct to edns list, after parsing it to wire format. */
void subnet_ecs_opt_list_append(struct ecs_data* ecs, struct edns_option** list,
	struct module_qstate *qstate, struct regional *region);

/** Create ecs_data from the sockaddr_storage information. */
void subnet_option_from_ss(struct sockaddr_storage *ss, struct ecs_data* ecs,
	struct config_file* cfg);
#endif /* SUBNETMOD_H */
