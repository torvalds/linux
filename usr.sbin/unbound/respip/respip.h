/*
 * respip/respip.h - IP-based response modification module
 */

/**
 * \file
 *
 * This file contains a module that selectively modifies query responses
 * based on their AAAA/A IP addresses.
 */

#ifndef RESPIP_RESPIP_H
#define RESPIP_RESPIP_H

#include "util/module.h"
#include "services/localzone.h"
#include "util/locks.h"

/**
 * Conceptual set of IP addresses for response AAAA or A records that should
 * trigger special actions.
 */
struct respip_set {
	struct regional* region;
	struct rbtree_type ip_tree;
	lock_rw_type lock;	/* lock on the respip tree. It is ordered
		after views and before hints, stubs and local zones. */
	char* const* tagname;	/* shallow copy of tag names, for logging */
	int num_tags;		/* number of tagname entries */
};


/** An address span with response control information */
struct resp_addr {
	/** node in address tree */
	struct addr_tree_node node;
	/** lock on the node item */
	lock_rw_type lock;
	/** tag bitlist */
	uint8_t* taglist;
	/** length of the taglist (in bytes) */
	size_t taglen;
	/** action for this address span */
	enum respip_action action;
        /** "local data" for this node */
	struct ub_packed_rrset_key* data;
};


/**
 * Forward declaration for the structure that represents a tree of view data.
 */

struct views;

struct respip_addr_info;

/**
 * Client-specific attributes that can affect IP-based actions.
 * This is essentially a subset of acl_addr (except for respip_set) but
 * defined as a separate structure to avoid dependency on the daemon-specific
 * structure.
 */
struct respip_client_info {
	uint8_t* taglist;
	size_t taglen;
	uint8_t* tag_actions;
	size_t tag_actions_size;
	struct config_strlist** tag_datas;
	size_t tag_datas_size;
	/** The view for the action, during cache callback that is by
	 * pointer. */
	struct view* view;
	/** If from module query state, the view pointer is NULL, but the
	 * name is stored in reference to the view. */
	char* view_name;
};

/**
 * Data items representing the result of response-ip processing.
 * Note: this structure currently only define a few members, but exists
 * as a separate struct mainly for the convenience of custom extensions.
 */
struct respip_action_info {
	enum respip_action action;
	int rpz_used;
	int rpz_log;
	int rpz_disabled;
	char* log_name;
	int rpz_cname_override;
	struct respip_addr_info* addrinfo; /* set only for inform variants */
};

/**
  * Forward declaration for the structure that represents a node in the
  * respip_set address tree
  */
struct resp_addr;

/**
 * Create response IP set.
 * @return new struct or NULL on error.
 */
struct respip_set* respip_set_create(void);

/**
 * Delete response IP set.
 * @param set: to delete.
 */
void respip_set_delete(struct respip_set* set);

/**
 * Apply response-ip config settings to the global (default) view.
 * It assumes exclusive access to set (no internal locks).
 * @param set: processed global respip config data
 * @param cfg: config data.
 * @return 1 on success, 0 on error.
 */
int respip_global_apply_cfg(struct respip_set* set, struct config_file* cfg);

/**
 * Apply response-ip config settings in named views.
 * @param vs: view structures with processed config data
 * @param cfg: config data.
 * @param have_view_respip_cfg: set to true if any named view has respip
 * 	configuration; otherwise set to false
 * @return 1 on success, 0 on error.
 */
int respip_views_apply_cfg(struct views* vs, struct config_file* cfg,
	int* have_view_respip_cfg);

/**
 * Merge two replies to build a complete CNAME chain.
 * It appends the content of 'tgt_rep' to 'base_rep', assuming (but not
 * checking) the former ends with a CNAME and the latter resolves its target.
 * A merged new reply will be built using 'region' and *new_repp will point
 * to the new one on success.
 * If the target reply would also be subject to a response-ip action for
 * 'cinfo', this function uses 'base_rep' as the merged reply, ignoring
 * 'tgt_rep'.  This is for avoiding cases like a CNAME loop or failure of
 * applying an action to an address.
 * RRSIGs in 'tgt_rep' will be excluded in the merged reply, as the resulting
 * reply is assumed to be faked due to a response-ip action and can't be
 * considered secure in terms of DNSSEC.
 * The caller must ensure that neither 'base_rep' nor 'tgt_rep' can be modified
 * until this function returns. 
 * @param base_rep: the reply info containing an incomplete CNAME.
 * @param qinfo: query info corresponding to 'base_rep'.
 * @param tgt_rep: the reply info that completes the CNAME chain.
 * @param cinfo: client info corresponding to 'base_rep'.
 * @param must_validate: whether 'tgt_rep' must be DNSSEC-validated.
 * @param new_repp: pointer placeholder for the merged reply.  will be intact
 *   on error.
 * @param region: allocator to build *new_repp.
 * @param az: auth zones containing RPZ information.
 * @param views: views tree to lookup view used.
 * @param respip_set: the respip set for the global view.
 * @return 1 on success, 0 on error.
 */
int respip_merge_cname(struct reply_info* base_rep,
	const struct query_info* qinfo, const struct reply_info* tgt_rep,
	const struct respip_client_info* cinfo, int must_validate,
	struct reply_info** new_repp, struct regional* region,
	struct auth_zones* az, struct views* views,
	struct respip_set* respip_set);

/**
 * See if any IP-based action should apply to any IP address of AAAA/A answer
 * record in the reply.  If so, apply the action.  In some cases it rewrites
 * the reply rrsets, in which case *new_repp will point to the updated reply
 * info.  Depending on the action, some of the rrsets in 'rep' will be
 * shallow-copied into '*new_repp'; the caller must ensure that the rrsets
 * in 'rep' are valid throughout the lifetime of *new_repp, and it must
 * provide appropriate mutex if the rrsets can be shared by multiple threads.
 * @param qinfo: query info corresponding to the reply.
 * @param cinfo: client-specific info to identify the best matching action.
 *   can be NULL.
 * @param rep: original reply info.  must not be NULL.
 * @param new_repp: can be set to the rewritten reply info (intact on failure).
 * @param actinfo: result of response-ip processing
 * @param alias_rrset: must not be NULL.
 * @param search_only: if true, only check if an action would apply.  actionp
 *   will be set (or intact) accordingly but the modified reply won't be built.
 * @param az: auth zones containing RPZ information.
 * @param region: allocator to build *new_repp.
 * @param rpz_passthru: keeps track of query state can have passthru that
 *   stops further rpz processing. Or NULL for cached answer processing.
 * @param views: views tree to lookup view used.
 * @param ipset: the respip set for the global view.
 * @return 1 on success, 0 on error.
 */
int respip_rewrite_reply(const struct query_info* qinfo,
	const struct respip_client_info* cinfo,
	const struct reply_info *rep, struct reply_info** new_repp,
	struct respip_action_info* actinfo,
	struct ub_packed_rrset_key** alias_rrset,
	int search_only, struct regional* region, struct auth_zones* az,
	int* rpz_passthru, struct views* views, struct respip_set* ipset);

/**
 * Get the response-ip function block.
 * @return: function block with function pointers to response-ip methods.
 */
struct module_func_block* respip_get_funcblock(void);

/** response-ip init */
int respip_init(struct module_env* env, int id);

/** response-ip deinit */
void respip_deinit(struct module_env* env, int id);

/** response-ip operate on a query */
void respip_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound);

/** inform response-ip super */
void respip_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

/** response-ip cleanup query state */
void respip_clear(struct module_qstate* qstate, int id);

/**
 * returns address of the IP address tree of the specified respip set;
 * returns NULL for NULL input; exists for test purposes only
 */
struct rbtree_type* respip_set_get_tree(struct respip_set* set);

/**
 * returns respip action for the specified node in the respip address
 * returns respip_none for NULL input; exists for test purposes only
 */
enum respip_action resp_addr_get_action(const struct resp_addr* addr);

/**
 * returns rrset portion of the specified node in the respip address
 * tree; returns NULL for NULL input; exists for test purposes only
 */
struct ub_packed_rrset_key* resp_addr_get_rrset(struct resp_addr* addr);

/** response-ip alloc size routine */
size_t respip_get_mem(struct module_env* env, int id);

/**
 * respip set emptiness test
 * @param set respip set to test
 * @return 0 if the specified set exists (non-NULL) and is non-empty;
 *	otherwise returns 1
 */
int respip_set_is_empty(const struct respip_set* set);

/**
 * print log information for a query subject to an inform or inform-deny
 * response-ip action.
 * @param respip_actinfo: response-ip information that causes the action
 * @param qname: query name in the context, will be ignored if local_alias is
 *   non-NULL.
 * @param qtype: query type, in host byte order.
 * @param qclass: query class, in host byte order.
 * @param local_alias: set to a local alias if the query matches an alias in
 *  a local zone.  In this case its owner name will be considered the actual
 *  query name.
 * @param addr: the client's source address and port.
 * @param addrlen: the client's source address length.
 */
void respip_inform_print(struct respip_action_info* respip_actinfo,
	uint8_t* qname, uint16_t qtype, uint16_t qclass,
	struct local_rrset* local_alias, struct sockaddr_storage* addr,
	socklen_t addrlen);

/**
 * Find resp_addr in tree, create and add to tree if it does not exist.
 * @param set: struct containing the tree and region to alloc new node on.
 * 	should hold write lock.
 * @param addr: address to look up.
 * @param addrlen: length of addr.
 * @param net: netblock to lookup.
 * @param create: create node if it does not exist when 1.
 * @param ipstr: human redable ip string, for logging.
 * @return newly created of found node, not holding lock.
 */
struct resp_addr*
respip_sockaddr_find_or_create(struct respip_set* set, struct sockaddr_storage* addr,
		socklen_t addrlen, int net, int create, const char* ipstr);

/**
 * Add RR to resp_addr's RRset. Create RRset if not existing.
 * @param region: region to alloc RR(set).
 * @param raddr: resp_addr containing RRset. Must hold write lock.
 * @param rrtype: RR type.
 * @param rrclass: RR class.
 * @param ttl: TTL.
 * @param rdata: RDATA.
 * @param rdata_len: length of rdata.
 * @param rrstr: RR as string, for logging
 * @param netblockstr: netblock as string, for logging
 * @return 0 on error
 */
int
respip_enter_rr(struct regional* region, struct resp_addr* raddr,
	uint16_t rrtype, uint16_t rrclass, time_t ttl, uint8_t* rdata,
	size_t rdata_len, const char* rrstr, const char* netblockstr);

/**
 * Delete resp_addr node from tree.
 * @param set: struct containing tree. Must hold write lock.
 * @param node: node to delete. Not locked.
 */
void
respip_sockaddr_delete(struct respip_set* set, struct resp_addr* node);

struct ub_packed_rrset_key*
respip_copy_rrset(const struct ub_packed_rrset_key* key, struct regional* region);

/** Get memory usage of respip set tree. The routine locks and unlocks the
 * set for reading. */
size_t respip_set_get_mem(struct respip_set* set);

/**
 * Swap internal tree with preallocated entries. Caller should manage
 * the locks.
 * @param respip_set: response ip tree
 * @param data: preallocated information.
 */
void respip_set_swap_tree(struct respip_set* respip_set,
	struct respip_set* data);

#endif	/* RESPIP_RESPIP_H */
