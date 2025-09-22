/*
 * validator/val_kentry.h - validator key entry definition.
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
 * This file contains functions for dealing with validator key entries.
 */

#ifndef VALIDATOR_VAL_KENTRY_H
#define VALIDATOR_VAL_KENTRY_H
struct packed_rrset_data;
struct regional;
struct ub_packed_rrset_key;
#include "util/storage/lruhash.h"
#include "sldns/rrdef.h"

/**
 * A key entry for the validator.
 * This may or may not be a trusted key.
 * This is what is stored in the key cache.
 * This is the key part for the cache; the key entry key.
 */
struct key_entry_key {
	/** lru hash entry */
	struct lruhash_entry entry;
	/** name of the key */
	uint8_t* name;
	/** length of name */
	size_t namelen;
	/** class of the key, host byteorder */
	uint16_t key_class;
};

/**
 * Key entry for the validator.
 * Contains key status.
 * This is the data part for the cache, the key entry data.
 *
 * Can be in three basic states:
 * 	isbad=0:		good key
 * 	isbad=1:		bad key
 * 	isbad=0 && rrset=0:	insecure space.
 */
struct key_entry_data {
	/** the TTL of this entry (absolute time) */
	time_t ttl;
	/** the key rrdata. can be NULL to signal keyless name. */
	struct packed_rrset_data* rrset_data;
	/** not NULL sometimes to give reason why bogus */
	char* reason;
        /** not NULL to give reason why bogus */
        sldns_ede_code reason_bogus;
	/** list of algorithms signalled, ends with 0, or NULL */
	uint8_t* algo;
	/** DNS RR type of the rrset data (host order) */
	uint16_t rrset_type;
	/** if the key is bad: Bogus or malformed */
	uint8_t isbad;
};

/** function for lruhash operation */
size_t key_entry_sizefunc(void* key, void* data);

/** function for lruhash operation */
int key_entry_compfunc(void* k1, void* k2);

/** function for lruhash operation */
void key_entry_delkeyfunc(void* key, void* userarg);

/** function for lruhash operation */
void key_entry_deldatafunc(void* data, void* userarg);

/** calculate hash for key entry 
 * @param kk: key entry. The lruhash entry.hash value is filled in.
 */
void key_entry_hash(struct key_entry_key* kk);

/**
 * Copy a key entry, to be region-allocated.
 * @param kkey: the key entry key (and data pointer) to copy.
 * @param region: where to allocate it
 * @return newly region-allocated entry or NULL on a failure to allocate.
 */
struct key_entry_key* key_entry_copy_toregion(struct key_entry_key* kkey, 
	struct regional* region);

/**
 * Copy a key entry, malloced.
 * @param kkey: the key entry key (and data pointer) to copy.
 * @param copy_reason: if the reason string needs to be copied (allocated).
 * @return newly allocated entry or NULL on a failure to allocate memory.
 */
struct key_entry_key* key_entry_copy(struct key_entry_key* kkey,
	int copy_reason);

/**
 * See if this is a null entry. Does not do locking.
 * @param kkey: must have data pointer set correctly
 * @return true if it is a NULL rrset entry.
 */
int key_entry_isnull(struct key_entry_key* kkey);

/**
 * See if this entry is good. Does not do locking.
 * @param kkey: must have data pointer set correctly
 * @return true if it is good.
 */
int key_entry_isgood(struct key_entry_key* kkey);

/**
 * See if this entry is bad. Does not do locking.
 * @param kkey: must have data pointer set correctly
 * @return true if it is bad.
 */
int key_entry_isbad(struct key_entry_key* kkey);

/**
 * Get reason why a key is bad.
 * @param kkey: bad key
 * @return pointer to string.
 *    String is part of key entry and is deleted with it.
 */
char* key_entry_get_reason(struct key_entry_key* kkey);

/**
 * Get the EDE (RFC8914) code why a key is bad. Can return LDNS_EDE_NONE.
 * @param kkey: bad key
 * @return the ede code.
 */
sldns_ede_code key_entry_get_reason_bogus(struct key_entry_key* kkey);

/**
 * Create a null entry, in the given region.
 * @param region: where to allocate
 * @param name: the key name
 * @param namelen: length of name
 * @param dclass: class of key entry. (host order);
 * @param ttl: what ttl should the key have. relative.
 * @param reason_bogus: accompanying EDE code.
 * @param reason: accompanying NULL-terminated EDE string (or NULL).
 * @param now: current time (added to ttl).
 * @return new key entry or NULL on alloc failure
 */
struct key_entry_key* key_entry_create_null(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass, time_t ttl,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now);

/**
 * Create a key entry from an rrset, in the given region.
 * @param region: where to allocate.
 * @param name: the key name
 * @param namelen: length of name
 * @param dclass: class of key entry. (host order);
 * @param rrset: data for key entry. This is copied to the region.
 * @param sigalg: signalled algorithm list (or NULL).
 * @param reason_bogus: accompanying EDE code (usually LDNS_EDE_NONE).
 * @param reason: accompanying NULL-terminated EDE string (or NULL).
 * @param now: current time (added to ttl of rrset)
 * @return new key entry or NULL on alloc failure
 */
struct key_entry_key* key_entry_create_rrset(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass,
	struct ub_packed_rrset_key* rrset, uint8_t* sigalg,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now);

/**
 * Create a bad entry, in the given region.
 * @param region: where to allocate
 * @param name: the key name
 * @param namelen: length of name
 * @param dclass: class of key entry. (host order);
 * @param ttl: what ttl should the key have. relative.
 * @param reason_bogus: accompanying EDE code.
 * @param reason: accompanying NULL-terminated EDE string (or NULL).
 * @param now: current time (added to ttl).
 * @return new key entry or NULL on alloc failure
 */
struct key_entry_key* key_entry_create_bad(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass, time_t ttl,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now);

/**
 * Obtain rrset from a key entry, allocated in region.
 * @param kkey: key entry to convert to a rrset.
 * @param region: where to allocate rrset
 * @return rrset copy; if no rrset or alloc error returns NULL.
 */
struct ub_packed_rrset_key* key_entry_get_rrset(struct key_entry_key* kkey,
	struct regional* region);

/**
 * Get keysize of the keyentry.
 * @param kkey: key, must be a good key, with contents.
 * @return size in bits of the key.
 */
size_t key_entry_keysize(struct key_entry_key* kkey);

#endif /* VALIDATOR_VAL_KENTRY_H */
