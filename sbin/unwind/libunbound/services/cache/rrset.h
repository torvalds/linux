/*
 * services/cache/rrset.h - Resource record set cache.
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
 * This file contains the rrset cache.
 */

#ifndef SERVICES_CACHE_RRSET_H
#define SERVICES_CACHE_RRSET_H
#include "util/storage/lruhash.h"
#include "util/storage/slabhash.h"
#include "util/data/packed_rrset.h"
struct config_file;
struct alloc_cache;
struct rrset_ref;
struct regional;

/**
 * The rrset cache
 * Thin wrapper around hashtable, like a typedef.
 */
struct rrset_cache {
	/** uses partitioned hash table */
	struct slabhash table;
};

/**
 * Create rrset cache
 * @param cfg: config settings or NULL for defaults.
 * @param alloc: initial default rrset key allocation.
 * @return: NULL on error.
 */
struct rrset_cache* rrset_cache_create(struct config_file* cfg, 
	struct alloc_cache* alloc);

/**
 * Delete rrset cache
 * @param r: rrset cache to delete.
 */
void rrset_cache_delete(struct rrset_cache* r);

/**
 * Adjust settings of the cache to settings from the config file.
 * May purge the cache. May recreate the cache.
 * There may be no threading or use by other threads.
 * @param r: rrset cache to adjust (like realloc).
 * @param cfg: config settings or NULL for defaults.
 * @param alloc: initial default rrset key allocation.
 * @return 0 on error, or new rrset cache pointer on success.
 */
struct rrset_cache* rrset_cache_adjust(struct rrset_cache* r, 
	struct config_file* cfg, struct alloc_cache* alloc);

/**
 * Touch rrset, with given pointer and id.
 * Caller may not hold a lock on ANY rrset, this could give deadlock.
 *
 * This routine is faster than a hashtable lookup:
 *	o no bin_lock is acquired.
 *	o no walk through the bin-overflow-list. 
 *	o no comparison of the entry key to find it.
 *
 * @param r: rrset cache.
 * @param key: rrset key. Marked recently used (if it was not deleted
 *	before the lock is acquired, in that case nothing happens).
 * @param hash: hash value of the item. Please read it from the key when
 *	you have it locked. Used to find slab from slabhash.
 * @param id: used to check that the item is unchanged and not deleted.
 */
void rrset_cache_touch(struct rrset_cache* r, struct ub_packed_rrset_key* key,
	hashvalue_type hash, rrset_id_type id);

/**
 * Update an rrset in the rrset cache. Stores the information for later use.
 * Will lookup if the rrset is in the cache and perform an update if necessary.
 * If the item was present, and superior, references are returned to that.
 * The passed item is then deallocated with rrset_parsedelete.
 *
 * A superior rrset is:
 *	o rrset with better trust value.
 *	o same trust value, different rdata, newly passed rrset is inserted.
 * If rdata is the same, TTL in the cache is updated.
 *
 * @param r: the rrset cache.
 * @param ref: reference (ptr and id) to the rrset. Pass reference setup for
 *	the new rrset. The reference may be changed if the cached rrset is
 *	superior.
 *	Before calling the rrset is presumed newly allocated and changeable.
 *	After calling you do not hold a lock, and the rrset is inserted in
 *	the hashtable so you need a lock to change it.
 * @param alloc: how to allocate (and deallocate) the special rrset key.
 * @param timenow: current time (to see if ttl in cache is expired).
 * @return: true if the passed reference is updated, false if it is unchanged.
 * 	0: reference unchanged, inserted in cache.
 * 	1: reference updated, item is inserted in cache.
 * 	2: reference updated, item in cache is considered superior.
 *	   also the rdata is equal (but other parameters in cache are superior).
 */
int rrset_cache_update(struct rrset_cache* r, struct rrset_ref* ref, 
	struct alloc_cache* alloc, time_t timenow);

/**
 * Update or add an rrset in the rrset cache using a wildcard dname.
 * Generates wildcard dname by prepending the wildcard label to the closest
 * encloser. Will lookup if the rrset is in the cache and perform an update if
 * necessary.
 *
 * @param rrset_cache: the rrset cache.
 * @param rrset: which rrset to cache as wildcard. This rrset is left 
 * 	untouched.
 * @param ce: the closest encloser, will be uses to generate the wildcard dname.
 * @param ce_len: the closest encloser length.
 * @param alloc: how to allocate (and deallocate) the special rrset key.
 * @param timenow: current time (to see if ttl in cache is expired).
 */
void rrset_cache_update_wildcard(struct rrset_cache* rrset_cache, 
	struct ub_packed_rrset_key* rrset, uint8_t* ce, size_t ce_len,
	struct alloc_cache* alloc, time_t timenow);

/**
 * Lookup rrset. You obtain read/write lock. You must unlock before lookup
 * anything of else.
 * @param r: the rrset cache.
 * @param qname: name of rrset to lookup.
 * @param qnamelen: length of name of rrset to lookup.
 * @param qtype: type of rrset to lookup (host order).
 * @param qclass: class of rrset to lookup (host order).
 * @param flags: rrset flags, or 0.
 * @param timenow: used to compare with TTL.
 * @param wr: set true to get writelock.
 * @return packed rrset key pointer. Remember to unlock the key.entry.lock.
 * 	or NULL if could not be found or it was timed out.
 */
struct ub_packed_rrset_key* rrset_cache_lookup(struct rrset_cache* r,
	uint8_t* qname, size_t qnamelen, uint16_t qtype, uint16_t qclass,
	uint32_t flags, time_t timenow, int wr);

/**
 * Obtain readlock on a (sorted) list of rrset references.
 * Checks TTLs and IDs of the rrsets and rollbacks locking if not Ok.
 * @param ref: array of rrset references (key pointer and ID value).
 *	duplicate references are allowed and handled.
 * @param count: size of array.
 * @param timenow: used to compare with TTL.
 * @return true on success, false on a failure, which can be that some
 * 	RRsets have timed out, or that they do not exist any more, the
 *	RRsets have been purged from the cache.
 *	If true, you hold readlocks on all the ref items. 
 */
int rrset_array_lock(struct rrset_ref* ref, size_t count, time_t timenow);

/**
 * Unlock array (sorted) of rrset references.
 * @param ref: array of rrset references (key pointer and ID value).
 *	duplicate references are allowed and handled.
 * @param count: size of array.
 */
void rrset_array_unlock(struct rrset_ref* ref, size_t count);

/**
 * Unlock array (sorted) of rrset references and at the same time
 * touch LRU on the rrsets. It needs the scratch region for temporary
 * storage as it uses the initial locks to obtain hash values.
 * @param r: the rrset cache. In this cache LRU is updated.
 * @param scratch: region for temporary storage of hash values.
 *	if memory allocation fails, the lru touch fails silently,
 *	but locks are released. memory errors are logged.
 * @param ref: array of rrset references (key pointer and ID value).
 *	duplicate references are allowed and handled.
 * @param count: size of array.
 */
void rrset_array_unlock_touch(struct rrset_cache* r, struct regional* scratch,
	struct rrset_ref* ref, size_t count);

/**
 * Update security status of an rrset. Looks up the rrset.
 * If found, checks if rdata is equal.
 * If so, it will update the security, trust and rrset-ttl values.
 * The values are only updated if security is increased (towards secure).
 * @param r: the rrset cache. 
 * @param rrset: which rrset to attempt to update. This rrset is left 
 * 	untouched. The rrset in the cache is updated in-place.
 * @param now: current time.
 */
void rrset_update_sec_status(struct rrset_cache* r, 
	struct ub_packed_rrset_key* rrset, time_t now);

/**
 * Looks up security status of an rrset. Looks up the rrset.
 * If found, checks if rdata is equal, and entry did not expire.
 * If so, it will update the security, trust and rrset-ttl values.
 * @param r: the rrset cache. 
 * @param rrset: This rrset may change security status due to the cache.
 * 	But its status will only improve, towards secure.
 * @param now: current time.
 */
void rrset_check_sec_status(struct rrset_cache* r, 
	struct ub_packed_rrset_key* rrset, time_t now);

/**
 * Removes rrsets above the qname, returns upper qname.
 * @param r: the rrset cache.
 * @param qname: the start qname, also used as the output.
 * @param qnamelen: length of qname, updated when it returns.
 * @param searchtype: qtype to search for.
 * @param qclass: qclass to search for.
 * @param now: current time.
 * @param qnametop: the top qname to stop removal (it is not removed).
 * @param qnametoplen: length of qnametop.
 */
void rrset_cache_remove_above(struct rrset_cache* r, uint8_t** qname,
	size_t* qnamelen, uint16_t searchtype, uint16_t qclass, time_t now,
	uint8_t* qnametop, size_t qnametoplen);

/**
 * Sees if an rrset is expired above the qname, returns upper qname.
 * @param r: the rrset cache.
 * @param qname: the start qname, also used as the output.
 * @param qnamelen: length of qname, updated when it returns.
 * @param searchtype: qtype to search for.
 * @param qclass: qclass to search for.
 * @param now: current time.
 * @param qnametop: the top qname, don't look farther than that.
 * @param qnametoplen: length of qnametop.
 * @return true if there is an expired rrset above, false otherwise.
 */
int rrset_cache_expired_above(struct rrset_cache* r, uint8_t** qname,
	size_t* qnamelen, uint16_t searchtype, uint16_t qclass, time_t now,
	uint8_t* qnametop, size_t qnametoplen);

/**
 * Remove an rrset from the cache, by name and type and flags
 * @param r: rrset cache
 * @param nm: name of rrset
 * @param nmlen: length of name
 * @param type: type of rrset
 * @param dclass: class of rrset, host order
 * @param flags: flags of rrset, host order
 */
void rrset_cache_remove(struct rrset_cache* r, uint8_t* nm, size_t nmlen,
	uint16_t type, uint16_t dclass, uint32_t flags);

/** mark rrset to be deleted, set id=0 */
void rrset_markdel(void* key);

#endif /* SERVICES_CACHE_RRSET_H */
