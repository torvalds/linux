/*
 * services/rpz.h - rpz service
 *
 * Copyright (c) 2019, NLnet Labs. All rights reserved.
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
 * This file contains functions to enable RPZ service.
 */

#ifndef SERVICES_RPZ_H
#define SERVICES_RPZ_H

#include "services/localzone.h"
#include "util/locks.h"
#include "util/log.h"
#include "util/config_file.h"
#include "services/authzone.h"
#include "sldns/sbuffer.h"
#include "daemon/stats.h"
#include "respip/respip.h"
struct iter_qstate;

/**
 * RPZ triggers, only the QNAME trigger is currently supported in Unbound.
 */
enum rpz_trigger {
	RPZ_QNAME_TRIGGER = 0,
	/* unsupported triggers */
	RPZ_CLIENT_IP_TRIGGER,	 /* rpz-client-ip */
	RPZ_RESPONSE_IP_TRIGGER, /* rpz-ip */
	RPZ_NSDNAME_TRIGGER,	 /* rpz-nsdname */
	RPZ_NSIP_TRIGGER,	 /* rpz-nsip */
	RPZ_INVALID_TRIGGER, 	 /* dname does not contain valid trigger */
};

/**
 * RPZ actions.
 */
enum rpz_action {
	RPZ_NXDOMAIN_ACTION = 0,/* CNAME . */
	RPZ_NODATA_ACTION,	/* CNAME *. */
	RPZ_PASSTHRU_ACTION,	/* CNAME rpz-passthru. */
	RPZ_DROP_ACTION,	/* CNAME rpz-drop. */
	RPZ_TCP_ONLY_ACTION,	/* CNAME rpz-tcp-only. */
	RPZ_INVALID_ACTION,	/* CNAME with (child of) TLD starting with
				   "rpz-" in target, SOA, NS, DNAME and
				   DNSSEC-related records. */
	RPZ_LOCAL_DATA_ACTION,	/* anything else */
	/* RPZ override actions */
	RPZ_DISABLED_ACTION,    /* RPZ action disabled using override */
	RPZ_NO_OVERRIDE_ACTION, /* RPZ action no override*/
	RPZ_CNAME_OVERRIDE_ACTION, /* RPZ CNAME action override*/
};

struct clientip_synthesized_rrset {
	struct regional* region;
	struct rbtree_type entries;
	/** lock on the entries tree */
	lock_rw_type lock;
};

struct clientip_synthesized_rr {
	/** node in address tree */
	struct addr_tree_node node;
	/** lock on the node item */
	lock_rw_type lock;
	/** action for this address span */
	enum rpz_action action;
	/** "local data" for this node */
	struct local_rrset* data;
};

/**
 * RPZ containing policies. Pointed to from corresponding auth-zone. Part of a
 * linked list to keep configuration order. Iterating or changing the linked
 * list requires the rpz_lock from struct auth_zones. Changing items in this
 * struct require the lock from struct auth_zone.
 */
struct rpz {
	struct local_zones* local_zones;
	struct respip_set* respip_set;
	struct clientip_synthesized_rrset* client_set;
	struct clientip_synthesized_rrset* ns_set;
	struct local_zones* nsdname_zones;
	uint8_t* taglist;
	size_t taglistlen;
	enum rpz_action action_override;
	struct ub_packed_rrset_key* cname_override;
	int log;
	char* log_name;
	/** signal NXDOMAIN blocked with unset RA flag */
	int signal_nxdomain_ra;
	struct regional* region;
	int disabled;
};

/**
 * Create policy from RR and add to this RPZ.
 * @param r: the rpz to add the policy to.
 * @param azname: dname of the auth-zone
 * @param aznamelen: the length of the auth-zone name
 * @param dname: dname of the RR
 * @param dnamelen: length of the dname
 * @param rr_type: RR type of the RR
 * @param rr_class: RR class of the RR
 * @param rr_ttl: TTL of the RR
 * @param rdatawl: rdata of the RR, prepended with the rdata size
 * @param rdatalen: length if the RR, including the prepended rdata size
 * @param rr: the complete RR, for logging purposes
 * @param rr_len: the length of the complete RR
 * @return: 0 on error
 */
int rpz_insert_rr(struct rpz* r, uint8_t* azname, size_t aznamelen, uint8_t* dname,
	size_t dnamelen, uint16_t rr_type, uint16_t rr_class, uint32_t rr_ttl,
	uint8_t* rdatawl, size_t rdatalen, uint8_t* rr, size_t rr_len);

/**
 * Delete policy matching RR, used for IXFR.
 * @param r: the rpz to add the policy to.
 * @param azname: dname of the auth-zone
 * @param aznamelen: the length of the auth-zone name
 * @param dname: dname of the RR
 * @param dnamelen: length of the dname
 * @param rr_type: RR type of the RR
 * @param rr_class: RR class of the RR
 * @param rdatawl: rdata of the RR, prepended with the rdata size
 * @param rdatalen: length if the RR, including the prepended rdata size
 */
void rpz_remove_rr(struct rpz* r, uint8_t* azname, size_t aznamelen,
	uint8_t* dname, size_t dnamelen, uint16_t rr_type, uint16_t rr_class,
	uint8_t* rdatawl, size_t rdatalen);

/**
 * Walk over the RPZ zones to find and apply a QNAME trigger policy.
 * @param az: auth_zones struct, containing first RPZ item and RPZ lock
 * @param env: module env
 * @param qinfo: qinfo containing qname and qtype
 * @param edns: edns data
 * @param buf: buffer to write answer to
 * @param temp: scratchpad
 * @param repinfo: reply info
 * @param taglist: taglist to lookup.
 * @param taglen: length of taglist.
 * @param stats: worker stats struct
 * @param passthru: returns if the query can passthru further rpz processing.
 * @return: 1 if client answer is ready, 0 to continue resolving
 */
int rpz_callback_from_worker_request(struct auth_zones* az, struct module_env* env,
	struct query_info* qinfo, struct edns_data* edns, sldns_buffer* buf,
	struct regional* temp, struct comm_reply* repinfo,
	uint8_t* taglist, size_t taglen, struct ub_server_stats* stats,
	int* passthru);

/**
 * Callback to process when the iterator module is about to send queries.
 * Checks for nsip and nsdname triggers.
 * @param qstate: the query state.
 * @param iq: iterator module query state.
 * @return NULL if nothing is done. Or a new message with the contents from
 * 	the rpz, based on the delegation point. It is allocated in the
 * 	qstate region.
 */
struct dns_msg* rpz_callback_from_iterator_module(struct module_qstate* qstate,
	struct iter_qstate* iq);

/**
 * Callback to process when the iterator module has followed a cname.
 * There can be a qname trigger for the new query name.
 * @param qstate: the query state.
 * @param iq: iterator module query state.
 * @return NULL if nothing is done. Or a new message with the contents from
 * 	the rpz, based on the iq.qchase. It is allocated in the qstate region.
 */
struct dns_msg* rpz_callback_from_iterator_cname(struct module_qstate* qstate,
	struct iter_qstate* iq);

/**
 * Delete RPZ
 * @param r: RPZ struct to delete
 */
void rpz_delete(struct rpz* r);

/**
 * Clear local-zones and respip data in RPZ, used after reloading file or
 * AXFR/HTTP transfer.
 * @param r: RPZ to use
 */
int rpz_clear(struct rpz* r);

/**
 * Create RPZ. RPZ must be added to linked list after creation.
 * @return: the newly created RPZ
 */
struct rpz* rpz_create(struct config_auth* p);

/**
 * Change config on rpz, after reload.
 * @param r: the rpz structure.
 * @param p: the config that was read.
 * @return false on failure.
 */
int rpz_config(struct rpz* r, struct config_auth* p);

/**
 * String for RPZ action enum
 * @param a: RPZ action to get string for
 * @return: string for RPZ action
 */
const char* rpz_action_to_string(enum rpz_action a);

enum rpz_action
respip_action_to_rpz_action(enum respip_action a);

/**
 * Prepare RPZ after processing feed content.
 * @param r: RPZ to use
 */
void rpz_finish_config(struct rpz* r);

/**
 * Classify respip action for RPZ action
 * @param a: RPZ action
 * @return: the respip action
 */
enum respip_action
rpz_action_to_respip_action(enum rpz_action a);

/**
 * Enable RPZ
 * @param r: RPZ struct to enable
 */
void rpz_enable(struct rpz* r);

/**
 * Disable RPZ
 * @param r: RPZ struct to disable
 */
void rpz_disable(struct rpz* r);

/**
 * Get memory usage of rpz. Caller must manage locks.
 * @param r: RPZ struct.
 * @return memory usage.
 */
size_t rpz_get_mem(struct rpz* r);

#endif /* SERVICES_RPZ_H */
