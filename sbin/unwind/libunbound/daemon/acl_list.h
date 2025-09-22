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
 * This file keeps track of the list of clients that are allowed to
 * access the server.
 */

#ifndef DAEMON_ACL_LIST_H
#define DAEMON_ACL_LIST_H
#include "util/storage/dnstree.h"
#include "services/view.h"
struct config_file;
struct regional;

/**
 * Enumeration of access control options for an address range.
 * Allow or deny access.
 */
enum acl_access {
	/** disallow any access whatsoever, drop it */
	acl_deny = 0,
	/** disallow access, send a polite 'REFUSED' reply */
	acl_refuse,
	/** disallow any access to zones that aren't local, drop it */
	acl_deny_non_local,
	/** disallow access to zones that aren't local, 'REFUSED' reply */
	acl_refuse_non_local,
	/** allow full access for recursion (+RD) queries */
	acl_allow,
	/** allow full access for all queries, recursion and cache snooping */
	acl_allow_snoop,
	/** allow full access for recursion queries and set RD flag regardless
	 *  of request */
	acl_allow_setrd,
	/** allow full access for recursion (+RD) queries if valid cookie
	 *  present or stateful transport */
	acl_allow_cookie
};

/**
 * Access control storage structure
 */
struct acl_list {
	/** regional for allocation */
	struct regional* region;
	/**
	 * Tree of the addresses that are allowed/blocked.
	 * contents of type acl_addr.
	 */
	rbtree_type tree;
};

/**
 *
 * An address span with access control information
 */
struct acl_addr {
	/** node in address tree */
	struct addr_tree_node node;
	/** access control on this netblock */
	enum acl_access control;
	/** tag bitlist */
	uint8_t* taglist;
	/** length of the taglist (in bytes) */
	size_t taglen;
	/** array per tagnumber of localzonetype(in one byte). NULL if none. */
	uint8_t* tag_actions;
	/** size of the tag_actions_array */
	size_t tag_actions_size;
	/** array per tagnumber, with per tag a list of rdata strings.
	 * NULL if none.  strings are like 'A 127.0.0.1' 'AAAA ::1' */
	struct config_strlist** tag_datas;
	/** size of the tag_datas array */
	size_t tag_datas_size;
	/* If the acl node is for an interface */
	int is_interface;
	/* view element, NULL if none */
	struct view* view;
};

/**
 * Create acl structure
 * @return new structure or NULL on error.
 */
struct acl_list* acl_list_create(void);

/**
 * Delete acl structure.
 * @param acl: to delete.
 */
void acl_list_delete(struct acl_list* acl);

/**
 * Insert interface in the acl_list. This should happen when the listening
 * interface is setup.
 * @param acl_interface: acl_list to insert to.
 * @param addr: interface IP.
 * @param addrlen: length of the interface IP.
 * @param control: acl_access.
 * @return new structure or NULL on error.
 */
struct acl_addr*
acl_interface_insert(struct acl_list* acl_interface,
	struct sockaddr_storage* addr, socklen_t addrlen,
	enum acl_access control);

/**
 * Process access control config.
 * @param acl: where to store.
 * @param cfg: config options.
 * @param v: views structure
 * @return 0 on error.
 */
int acl_list_apply_cfg(struct acl_list* acl, struct config_file* cfg,
	struct views* v);

/**
 * Initialise (also clean) the acl_interface struct.
 * @param acl_interface: where to store.
 */
void acl_interface_init(struct acl_list* acl_interface);

/**
 * Process interface control config.
 * @param acl_interface: where to store.
 * @param cfg: config options.
 * @param v: views structure
 * @return 0 on error.
 */
int acl_interface_apply_cfg(struct acl_list* acl_interface, struct config_file* cfg,
	struct views* v);

/**
 * Lookup access control status for acl structure.
 * @param acl: structure for acl storage.
 * @return: what to do with message from this address.
 */
enum acl_access acl_get_control(struct acl_addr* acl);

/**
 * Lookup address to see its acl structure
 * @param acl: structure for address storage.
 * @param addr: address to check
 * @param addrlen: length of addr.
 * @return: acl structure from this address.
 */
struct acl_addr*
acl_addr_lookup(struct acl_list* acl, struct sockaddr_storage* addr,
        socklen_t addrlen);

/**
 * Get memory used by acl structure.
 * @param acl: structure for address storage.
 * @return bytes in use.
 */
size_t acl_list_get_mem(struct acl_list* acl);

/*
 * Get string for acl access specification
 * @param acl: access type value
 * @return string
 */
const char* acl_access_to_str(enum acl_access acl);

/* log acl and addr for action */
void log_acl_action(const char* action, struct sockaddr_storage* addr,
	socklen_t addrlen, enum acl_access acl, struct acl_addr* acladdr);

/**
 * Swap internal tree with preallocated entries.
 * @param acl: the acl structure.
 * @param data: the data structure used to take elements from. This contains
 * 	the old elements on return.
 */
void acl_list_swap_tree(struct acl_list* acl, struct acl_list* data);

#endif /* DAEMON_ACL_LIST_H */
