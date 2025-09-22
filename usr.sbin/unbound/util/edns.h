/*
 * util/edns.h - handle base EDNS options.
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
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
 * This file contains functions for base EDNS options.
 */

#ifndef UTIL_EDNS_H
#define UTIL_EDNS_H

#include "util/storage/dnstree.h"
#include "util/locks.h"

struct edns_data;
struct config_file;
struct comm_point;
struct regional;

/**
 * Structure containing all EDNS strings.
 */
struct edns_strings {
	/** Tree of EDNS client strings to use in upstream queries, per address
	 * prefix. Contains nodes of type edns_string_addr. */
	rbtree_type client_strings;
	/** EDNS opcode to use for client strings */
	uint16_t client_string_opcode;
	/** region to allocate tree nodes in */
	struct regional* region;
};

/**
 * EDNS string. Node of rbtree, containing string and prefix.
 */
struct edns_string_addr {
	/** node in address tree, used for tree lookups. Need to be the first
	 * member of this struct. */
	struct addr_tree_node node;
	/** string, ascii format */
	uint8_t* string;
	/** length of string */
	size_t string_len;
};

#define UNBOUND_COOKIE_HISTORY_SIZE 2
#define UNBOUND_COOKIE_SECRET_SIZE 16

typedef struct cookie_secret cookie_secret_type;
struct cookie_secret {
	/** cookie secret */
	uint8_t cookie_secret[UNBOUND_COOKIE_SECRET_SIZE];
};

/**
 * The cookie secrets from the cookie-secret-file.
 */
struct cookie_secrets {
	/** lock on the structure, in case there are modifications
	 * from remote control, this avoids race conditions. */
	lock_basic_type lock;

	/** how many cookies are there in the cookies array */
	size_t cookie_count;

	/* keep track of the last `UNBOUND_COOKIE_HISTORY_SIZE`
	 * cookies as per rfc requirement .*/
	cookie_secret_type cookie_secrets[UNBOUND_COOKIE_HISTORY_SIZE];
};

enum edns_cookie_val_status {
	COOKIE_STATUS_CLIENT_ONLY = -3,
	COOKIE_STATUS_FUTURE = -2,
	COOKIE_STATUS_EXPIRED = -1,
	COOKIE_STATUS_INVALID = 0,
	COOKIE_STATUS_VALID = 1,
	COOKIE_STATUS_VALID_RENEW = 2,
};

/**
 * Create structure to hold EDNS strings
 * @return: newly created edns_strings, NULL on alloc failure.
 */
struct edns_strings* edns_strings_create(void);

/** Delete EDNS strings structure
 * @param edns_strings: struct to delete
 */
void edns_strings_delete(struct edns_strings* edns_strings);

/**
 * Add configured EDNS strings
 * @param edns_strings: edns strings to apply config to
 * @param config: struct containing EDNS strings configuration
 * @return 0 on error
 */
int edns_strings_apply_cfg(struct edns_strings* edns_strings,
	struct config_file* config);

/**
 * Find string for address.
 * @param tree: tree containing EDNS strings per address prefix.
 * @param addr: address to use for tree lookup
 * @param addrlen: length of address
 * @return: matching tree node, NULL otherwise
 */
struct edns_string_addr*
edns_string_addr_lookup(rbtree_type* tree, struct sockaddr_storage* addr,
	socklen_t addrlen);

/**
 * Get memory usage of edns strings.
 * @param edns_strings: the edns strings
 * @return memory usage
 */
size_t edns_strings_get_mem(struct edns_strings* edns_strings);

/**
 * Swap internal tree with preallocated entries.
 * @param edns_strings: the edns strings structure.
 * @param data: the data structure used to take elements from. This contains
 * 	the old elements on return.
 */
void edns_strings_swap_tree(struct edns_strings* edns_strings,
	struct edns_strings* data);

/**
 * Compute the interoperable DNS cookie (RFC9018) hash.
 * @param in: buffer input for the hash generation. It needs to be:
 *	Client Cookie | Version | Reserved | Timestamp | Client-IP
 * @param secret: the server secret; implicit length of 16 octets.
 * @param v4: if the client IP is v4 or v6.
 * @param hash: buffer to write the hash to.
 * return a pointer to the hash.
 */
uint8_t* edns_cookie_server_hash(const uint8_t* in, const uint8_t* secret,
	int v4, uint8_t* hash);

/**
 * Write an interoperable DNS server cookie (RFC9018).
 * @param buf: buffer to write to. It should have a size of at least 32 octets
 *	as it doubles as the output buffer and the hash input buffer.
 *	The first 8 octets are expected to be the Client Cookie and will be
 *		left untouched.
 *	The next 8 octets will be written with Version | Reserved | Timestamp.
 *	The next 4 or 16 octets are expected to be the IPv4 or the IPv6 address
 *		based on the v4 flag.
 *	Thus the first 20 or 32 octets, based on the v4 flag, will be used as
 *		the hash input.
 *	The server hash (8 octets) will be written after the first 16 octets;
 *		overwriting the address information.
 *	The caller expects a complete, 24 octet long cookie in the buffer.
 * @param secret: the server secret; implicit length of 16 octets.
 * @param v4: if the client IP is v4 or v6.
 * @param timestamp: the timestamp to use.
 */
void edns_cookie_server_write(uint8_t* buf, const uint8_t* secret, int v4,
	uint32_t timestamp);

/**
 * Validate an interoperable DNS cookie (RFC9018).
 * @param cookie: pointer to the cookie data.
 * @param cookie_len: the length of the cookie data.
 * @param secret: pointer to the server secret.
 * @param secret_len: the length of the secret.
 * @param v4: if the client IP is v4 or v6.
 * @param hash_input: pointer to the hash input for validation. It needs to be:
 *	Client Cookie | Version | Reserved | Timestamp | Client-IP
 * @param now: the current time.
 * return edns_cookie_val_status with the cookie validation status i.e.,
 *	<=0 for invalid, else valid.
 */
enum edns_cookie_val_status edns_cookie_server_validate(const uint8_t* cookie,
	size_t cookie_len, const uint8_t* secret, size_t secret_len, int v4,
	const uint8_t* hash_input, uint32_t now);

/**
 * Create the cookie secrets structure.
 * @return the structure or NULL on failure.
 */
struct cookie_secrets* cookie_secrets_create(void);

/**
 * Delete the cookie secrets.
 * @param cookie_secrets: the cookie secrets.
 */
void cookie_secrets_delete(struct cookie_secrets* cookie_secrets);

/**
 * Apply configuration to cookie secrets, read them from file.
 * @param cookie_secrets: the cookie secrets structure.
 * @param cookie_secret_file: the file name, it is read.
 * @return false on failure.
 */
int cookie_secrets_apply_cfg(struct cookie_secrets* cookie_secrets,
	char* cookie_secret_file);

/**
 * Validate the cookie secrets, try all of them.
 * @param cookie: pointer to the cookie data.
 * @param cookie_len: the length of the cookie data.
 * @param cookie_secrets: struct of cookie secrets.
 * @param v4: if the client IP is v4 or v6.
 * @param hash_input: pointer to the hash input for validation. It needs to be:
 *	Client Cookie | Version | Reserved | Timestamp | Client-IP
 * @param now: the current time.
 * return edns_cookie_val_status with the cookie validation status i.e.,
 *	<=0 for invalid, else valid.
 */
enum edns_cookie_val_status cookie_secrets_server_validate(
	const uint8_t* cookie, size_t cookie_len,
	struct cookie_secrets* cookie_secrets, int v4,
	const uint8_t* hash_input, uint32_t now);

/**
 * Add a cookie secret. If there are no secrets yet, the secret will become
 * the active secret. Otherwise it will become the staging secret.
 * Active secrets are used to both verify and create new DNS Cookies.
 * Staging secrets are only used to verify DNS Cookies. Caller has to lock.
 */
void add_cookie_secret(struct cookie_secrets* cookie_secrets, uint8_t* secret,
	size_t secret_len);

/**
 * Makes the staging cookie secret active and the active secret staging.
 * Caller has to lock.
 */
void activate_cookie_secret(struct cookie_secrets* cookie_secrets);

/**
 * Drop a cookie secret. Drops the staging secret. An active secret will not
 * be dropped. Caller has to lock.
 */
void drop_cookie_secret(struct cookie_secrets* cookie_secrets);

#endif
