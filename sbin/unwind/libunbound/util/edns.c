/*
 * util/edns.c - handle base EDNS options.
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

#include "config.h"
#include "util/edns.h"
#include "util/config_file.h"
#include "util/netevent.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/rfc_1982.h"
#include "util/siphash.h"
#include "util/data/msgparse.h"
#include "util/data/msgreply.h"
#include "sldns/sbuffer.h"

struct edns_strings* edns_strings_create(void)
{
	struct edns_strings* edns_strings = calloc(1,
		sizeof(struct edns_strings));
	if(!edns_strings)
		return NULL;
	if(!(edns_strings->region = regional_create())) {
		edns_strings_delete(edns_strings);
		return NULL;
	}
	return edns_strings;
}

void edns_strings_delete(struct edns_strings* edns_strings)
{
	if(!edns_strings)
		return;
	regional_destroy(edns_strings->region);
	free(edns_strings);
}

static int
edns_strings_client_insert(struct edns_strings* edns_strings,
	struct sockaddr_storage* addr, socklen_t addrlen, int net,
	const char* string)
{
	struct edns_string_addr* esa = regional_alloc_zero(edns_strings->region,
		sizeof(struct edns_string_addr));
	if(!esa)
		return 0;
	esa->string_len = strlen(string);
	esa->string = regional_alloc_init(edns_strings->region, string,
		esa->string_len);
	if(!esa->string)
		return 0;
	if(!addr_tree_insert(&edns_strings->client_strings, &esa->node, addr,
		addrlen, net)) {
		verbose(VERB_QUERY, "duplicate EDNS client string ignored.");
	}
	return 1;
}

int edns_strings_apply_cfg(struct edns_strings* edns_strings,
	struct config_file* config)
{
	struct config_str2list* c;
	regional_free_all(edns_strings->region);
	addr_tree_init(&edns_strings->client_strings);

	for(c=config->edns_client_strings; c; c=c->next) {
		struct sockaddr_storage addr;
		socklen_t addrlen;
		int net;
		log_assert(c->str && c->str2);

		if(!netblockstrtoaddr(c->str, UNBOUND_DNS_PORT, &addr, &addrlen,
			&net)) {
			log_err("cannot parse EDNS client string IP netblock: "
				"%s", c->str);
			return 0;
		}
		if(!edns_strings_client_insert(edns_strings, &addr, addrlen,
			net, c->str2)) {
			log_err("out of memory while adding EDNS strings");
			return 0;
		}
	}
	edns_strings->client_string_opcode = config->edns_client_string_opcode;

	addr_tree_init_parents(&edns_strings->client_strings);
	return 1;
}

struct edns_string_addr*
edns_string_addr_lookup(rbtree_type* tree, struct sockaddr_storage* addr,
	socklen_t addrlen)
{
	return (struct edns_string_addr*)addr_tree_lookup(tree, addr, addrlen);
}

size_t
edns_strings_get_mem(struct edns_strings* edns_strings)
{
	if(!edns_strings) return 0;
	return regional_get_mem(edns_strings->region) + sizeof(*edns_strings);
}

void
edns_strings_swap_tree(struct edns_strings* edns_strings,
	struct edns_strings* data)
{
	rbtree_type tree = edns_strings->client_strings;
	uint16_t opcode = edns_strings->client_string_opcode;
	struct regional* region = edns_strings->region;

	edns_strings->client_strings = data->client_strings;
	edns_strings->client_string_opcode = data->client_string_opcode;
	edns_strings->region = data->region;
	data->client_strings = tree;
	data->client_string_opcode = opcode;
	data->region = region;
}

uint8_t*
edns_cookie_server_hash(const uint8_t* in, const uint8_t* secret, int v4,
	uint8_t* hash)
{
	v4?siphash(in, 20, secret, hash, 8):siphash(in, 32, secret, hash, 8);
	return hash;
}

void
edns_cookie_server_write(uint8_t* buf, const uint8_t* secret, int v4,
	uint32_t timestamp)
{
	uint8_t hash[8];
	buf[ 8] = 1;   /* Version */
	buf[ 9] = 0;   /* Reserved */
	buf[10] = 0;   /* Reserved */
	buf[11] = 0;   /* Reserved */
	sldns_write_uint32(buf + 12, timestamp);
	(void)edns_cookie_server_hash(buf, secret, v4, hash);
	memcpy(buf + 16, hash, 8);
}

enum edns_cookie_val_status
edns_cookie_server_validate(const uint8_t* cookie, size_t cookie_len,
	const uint8_t* secret, size_t secret_len, int v4,
	const uint8_t* hash_input, uint32_t now)
{
	uint8_t hash[8];
	uint32_t timestamp;
	uint32_t subt_1982 = 0; /* Initialize for the compiler; unused value */
	int comp_1982;
	if(cookie_len != 24)
		/* RFC9018 cookies are 24 bytes long */
		return COOKIE_STATUS_CLIENT_ONLY;
	if(secret_len != 16 ||  /* RFC9018 cookies have 16 byte secrets */
		cookie[8] != 1) /* RFC9018 cookies are cookie version 1 */
		return COOKIE_STATUS_INVALID;
	timestamp = sldns_read_uint32(cookie + 12);
	if((comp_1982 = compare_1982(now, timestamp)) > 0
		&& (subt_1982 = subtract_1982(timestamp, now)) > 3600)
		/* Cookie is older than 1 hour (see RFC9018 Section 4.3.) */
		return COOKIE_STATUS_EXPIRED;
	if(comp_1982 <= 0 && subtract_1982(now, timestamp) > 300)
		/* Cookie time is more than 5 minutes in the future.
		 * (see RFC9018 Section 4.3.) */
		return COOKIE_STATUS_FUTURE;
	if(memcmp(edns_cookie_server_hash(hash_input, secret, v4, hash),
		cookie + 16, 8) != 0)
		/* Hashes do not match */
		return COOKIE_STATUS_INVALID;
	if(comp_1982 > 0 && subt_1982 > 1800)
		/* Valid cookie but older than 30 minutes, so create a new one
		 * anyway */
		return COOKIE_STATUS_VALID_RENEW;
	return COOKIE_STATUS_VALID;
}

struct cookie_secrets*
cookie_secrets_create(void)
{
	struct cookie_secrets* cookie_secrets = calloc(1,
		sizeof(*cookie_secrets));
	if(!cookie_secrets)
		return NULL;
	lock_basic_init(&cookie_secrets->lock);
	lock_protect(&cookie_secrets->lock, &cookie_secrets->cookie_count,
		sizeof(cookie_secrets->cookie_count));
	lock_protect(&cookie_secrets->lock, cookie_secrets->cookie_secrets,
		sizeof(cookie_secret_type)*UNBOUND_COOKIE_HISTORY_SIZE);
	return cookie_secrets;
}

void
cookie_secrets_delete(struct cookie_secrets* cookie_secrets)
{
	if(!cookie_secrets)
		return;
	lock_basic_destroy(&cookie_secrets->lock);
	explicit_bzero(cookie_secrets->cookie_secrets,
		sizeof(cookie_secret_type)*UNBOUND_COOKIE_HISTORY_SIZE);
	free(cookie_secrets);
}

/** Read the cookie secret file */
static int
cookie_secret_file_read(struct cookie_secrets* cookie_secrets,
	char* cookie_secret_file)
{
	char secret[UNBOUND_COOKIE_SECRET_SIZE * 2 + 2/*'\n' and '\0'*/];
	FILE* f;
	int corrupt = 0;
	size_t count;

	log_assert(cookie_secret_file != NULL);
	cookie_secrets->cookie_count = 0;
	f = fopen(cookie_secret_file, "r");
	/* a non-existing cookie file is not an error */
	if( f == NULL ) {
		if(errno != EPERM) {
			log_err("Could not read cookie-secret-file '%s': %s",
				cookie_secret_file, strerror(errno));
			return 0;
		}
		return 1;
	}
	/* cookie secret file exists and is readable */
	for( count = 0; count < UNBOUND_COOKIE_HISTORY_SIZE; count++ ) {
		size_t secret_len = 0;
		ssize_t decoded_len = 0;
		if( fgets(secret, sizeof(secret), f) == NULL ) { break; }
		secret_len = strlen(secret);
		if( secret_len == 0 ) { break; }
		log_assert( secret_len <= sizeof(secret) );
		secret_len = secret[secret_len - 1] == '\n' ? secret_len - 1 : secret_len;
		if( secret_len != UNBOUND_COOKIE_SECRET_SIZE * 2 ) { corrupt++; break; }
		/* needed for `hex_pton`; stripping potential `\n` */
		secret[secret_len] = '\0';
		decoded_len = hex_pton(secret, cookie_secrets->cookie_secrets[count].cookie_secret,
		                       UNBOUND_COOKIE_SECRET_SIZE);
		if( decoded_len != UNBOUND_COOKIE_SECRET_SIZE ) { corrupt++; break; }
		cookie_secrets->cookie_count++;
	}
	fclose(f);
	return corrupt == 0;
}

int
cookie_secrets_apply_cfg(struct cookie_secrets* cookie_secrets,
	char* cookie_secret_file)
{
	if(!cookie_secrets) {
		if(!cookie_secret_file || !cookie_secret_file[0])
			return 1; /* There is nothing to read anyway */
		log_err("Could not read cookie secrets, no structure alloced");
		return 0;
	}
	if(!cookie_secret_file_read(cookie_secrets, cookie_secret_file))
		return 0;
	return 1;
}

enum edns_cookie_val_status
cookie_secrets_server_validate(const uint8_t* cookie, size_t cookie_len,
	struct cookie_secrets* cookie_secrets, int v4,
	const uint8_t* hash_input, uint32_t now)
{
	size_t i;
	enum edns_cookie_val_status cookie_val_status,
		last = COOKIE_STATUS_INVALID;
	if(!cookie_secrets)
		return COOKIE_STATUS_INVALID; /* There are no cookie secrets.*/
	lock_basic_lock(&cookie_secrets->lock);
	if(cookie_secrets->cookie_count == 0) {
		lock_basic_unlock(&cookie_secrets->lock);
		return COOKIE_STATUS_INVALID; /* There are no cookie secrets.*/
	}
	for(i=0; i<cookie_secrets->cookie_count; i++) {
		cookie_val_status = edns_cookie_server_validate(cookie,
			cookie_len,
			cookie_secrets->cookie_secrets[i].cookie_secret,
			UNBOUND_COOKIE_SECRET_SIZE, v4, hash_input, now);
		if(cookie_val_status == COOKIE_STATUS_VALID ||
			cookie_val_status == COOKIE_STATUS_VALID_RENEW) {
			lock_basic_unlock(&cookie_secrets->lock);
			/* For staging cookies, write a fresh cookie. */
			if(i != 0)
				return COOKIE_STATUS_VALID_RENEW;
			return cookie_val_status;
		}
		if(last == COOKIE_STATUS_INVALID)
			last = cookie_val_status; /* Store more interesting
				failure to return. */
	}
	lock_basic_unlock(&cookie_secrets->lock);
	return last;
}

void add_cookie_secret(struct cookie_secrets* cookie_secrets,
	uint8_t* secret, size_t secret_len)
{
	log_assert(secret_len == UNBOUND_COOKIE_SECRET_SIZE);
	(void)secret_len;
	if(!cookie_secrets)
		return;

	/* New cookie secret becomes the staging secret (position 1)
	 * unless there is no active cookie yet, then it becomes the active
	 * secret.  If the UNBOUND_COOKIE_HISTORY_SIZE > 2 then all staging cookies
	 * are moved one position down.
	 */
	if(cookie_secrets->cookie_count == 0) {
		memcpy( cookie_secrets->cookie_secrets->cookie_secret
		       , secret, UNBOUND_COOKIE_SECRET_SIZE);
		cookie_secrets->cookie_count = 1;
		explicit_bzero(secret, UNBOUND_COOKIE_SECRET_SIZE);
		return;
	}
#if UNBOUND_COOKIE_HISTORY_SIZE > 2
	memmove( &cookie_secrets->cookie_secrets[2], &cookie_secrets->cookie_secrets[1]
	       , sizeof(struct cookie_secret) * (UNBOUND_COOKIE_HISTORY_SIZE - 2));
#endif
	memcpy( cookie_secrets->cookie_secrets[1].cookie_secret
	      , secret, UNBOUND_COOKIE_SECRET_SIZE);
	cookie_secrets->cookie_count = cookie_secrets->cookie_count     < UNBOUND_COOKIE_HISTORY_SIZE
	                  ? cookie_secrets->cookie_count + 1 : UNBOUND_COOKIE_HISTORY_SIZE;
	explicit_bzero(secret, UNBOUND_COOKIE_SECRET_SIZE);
}

void activate_cookie_secret(struct cookie_secrets* cookie_secrets)
{
	uint8_t active_secret[UNBOUND_COOKIE_SECRET_SIZE];
	if(!cookie_secrets)
		return;
	/* The staging secret becomes the active secret.
	 * The active secret becomes a staging secret.
	 * If the UNBOUND_COOKIE_HISTORY_SIZE > 2 then all staging secrets are moved
	 * one position up and the previously active secret becomes the last
	 * staging secret.
	 */
	if(cookie_secrets->cookie_count < 2)
		return;
	memcpy( active_secret, cookie_secrets->cookie_secrets[0].cookie_secret
	      , UNBOUND_COOKIE_SECRET_SIZE);
	memmove( &cookie_secrets->cookie_secrets[0], &cookie_secrets->cookie_secrets[1]
	       , sizeof(struct cookie_secret) * (UNBOUND_COOKIE_HISTORY_SIZE - 1));
	memcpy( cookie_secrets->cookie_secrets[cookie_secrets->cookie_count - 1].cookie_secret
	      , active_secret, UNBOUND_COOKIE_SECRET_SIZE);
	explicit_bzero(active_secret, UNBOUND_COOKIE_SECRET_SIZE);
}

void drop_cookie_secret(struct cookie_secrets* cookie_secrets)
{
	if(!cookie_secrets)
		return;
	/* Drops a staging cookie secret. If there are more than one, it will
	 * drop the last staging secret. */
	if(cookie_secrets->cookie_count < 2)
		return;
	explicit_bzero( cookie_secrets->cookie_secrets[cookie_secrets->cookie_count - 1].cookie_secret
	              , UNBOUND_COOKIE_SECRET_SIZE);
	cookie_secrets->cookie_count -= 1;
}
