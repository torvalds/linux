/*
 * iterator/iter_delegpt.h - delegation point with NS and address information.
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
 * This file implements the Delegation Point. It contains a list of name servers
 * and their addresses if known.
 */

#ifndef ITERATOR_ITER_DELEGPT_H
#define ITERATOR_ITER_DELEGPT_H
#include "util/log.h"
struct regional;
struct delegpt_ns;
struct delegpt_addr;
struct dns_msg;
struct ub_packed_rrset_key;
struct msgreply_entry;

/**
 * Delegation Point.
 * For a domain name, the NS rrset, and the A and AAAA records for those.
 */
struct delegpt {
	/** the domain name of the delegation point. */
	uint8_t* name;
	/** length of the delegation point name */
	size_t namelen;
	/** number of labels in delegation point */
	int namelabs;

	/** the nameservers, names from the NS RRset rdata. */
	struct delegpt_ns* nslist;
	/** the target addresses for delegation */
	struct delegpt_addr* target_list;
	/** the list of usable targets; subset of target_list 
	 * the items in this list are not part of the result list.  */
	struct delegpt_addr* usable_list;
	/** the list of returned targets; subset of target_list */
	struct delegpt_addr* result_list;

	/** if true, the NS RRset was bogus. All info is bad. */
	int bogus;
	/** if true, the parent-side NS record has been applied:
	 * its names have been added and their addresses can follow later.
	 * Also true if the delegationpoint was created from a delegation
	 * message and thus contains the parent-side-info already. */
	uint8_t has_parent_side_NS;
	/** for assertions on type of delegpt */
	uint8_t dp_type_mlc;
	/** use SSL for upstream query */
	uint8_t ssl_upstream;
	/** use TCP for upstream query */
	uint8_t tcp_upstream;
	/** delegpt from authoritative zone that is locally hosted */
	uint8_t auth_dp;
	/*** no cache */
	int no_cache;
};

/**
 * Nameservers for a delegation point.
 */
struct delegpt_ns {
	/** next in list */
	struct delegpt_ns* next;
	/** name of nameserver */
	uint8_t* name;
	/** length of name */
	size_t namelen;
	/** number of cache lookups for the name */
	int cache_lookup_count;
	/** 
	 * If the name has been resolved. false if not queried for yet.
	 * true if the A, AAAA queries have been generated.
	 * marked true if those queries fail.
	 * and marked true if got4 and got6 are both true.
	 */
	int resolved;
	/** if the ipv4 address is in the delegpt, 0=not, 1=yes 2=negative,
	 * negative means it was done, but no content. */
	uint8_t got4;
	/** if the ipv6 address is in the delegpt, 0=not, 1=yes 2=negative */
	uint8_t got6;
	/**
	 * If the name is parent-side only and thus dispreferred.
	 * Its addresses become dispreferred as well
	 */
	uint8_t lame;
	/** if the parent-side ipv4 address has been looked up (last resort).
	 * Also enabled if a parent-side cache entry exists, or a parent-side
	 * negative-cache entry exists. */
	uint8_t done_pside4;
	/** if the parent-side ipv6 address has been looked up (last resort).
	 * Also enabled if a parent-side cache entry exists, or a parent-side
	 * negative-cache entry exists. */
	uint8_t done_pside6;
	/** the TLS authentication name, (if not NULL) to use. */
	char* tls_auth_name;
	/** the port to use; it should mostly be the default 53 but configured
	 *  upstreams can provide nondefault ports. */
	int port;
};

/**
 * Address of target nameserver in delegation point.
 */
struct delegpt_addr {
	/** next delegation point in results */
	struct delegpt_addr* next_result;
	/** next delegation point in usable list */
	struct delegpt_addr* next_usable;
	/** next delegation point in all targets list */
	struct delegpt_addr* next_target;

	/** delegation point address */
	struct sockaddr_storage addr;
	/** length of addr */
	socklen_t addrlen;
	/** number of attempts for this addr */
	int attempts;
	/** rtt stored here in the selection algorithm */
	int sel_rtt;
	/** if true, the A or AAAA RR was bogus, so this address is bad.
	 * Also check the dp->bogus to see if everything is bogus. */
	uint8_t bogus;
	/** if true, this address is dispreferred: it is a lame IP address */
	uint8_t lame;
	/** if the address is dnsseclame, but this cannot be cached, this
	 * option is useful to mark the address dnsseclame.
	 * This value is not copied in addr-copy and dp-copy. */
	uint8_t dnsseclame;
	/** the TLS authentication name, (if not NULL) to use. */
	char* tls_auth_name;
};

/**
 * Create new delegation point.
 * @param regional: where to allocate it.
 * @return new delegation point or NULL on error.
 */
struct delegpt* delegpt_create(struct regional* regional);

/**
 * Create a copy of a delegation point.
 * @param dp: delegation point to copy.
 * @param regional: where to allocate it.
 * @return new delegation point or NULL on error.
 */
struct delegpt* delegpt_copy(struct delegpt* dp, struct regional* regional);

/**
 * Set name of delegation point.
 * @param dp: delegation point.
 * @param regional: where to allocate the name copy.
 * @param name: name to use.
 * @return false on error.
 */
int delegpt_set_name(struct delegpt* dp, struct regional* regional, 
	uint8_t* name);

/**
 * Add a name to the delegation point.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param name: domain name in wire format.
 * @param lame: name is lame, disprefer it.
 * @param tls_auth_name: TLS authentication name (or NULL).
 * @param port: port to use for resolved addresses.
 * @return false on error.
 */
int delegpt_add_ns(struct delegpt* dp, struct regional* regional,
	uint8_t* name, uint8_t lame, char* tls_auth_name, int port);

/**
 * Add NS rrset; calls add_ns repeatedly.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param ns_rrset: NS rrset.
 * @param lame: rrset is lame, disprefer it.
 * @return 0 on alloc error.
 */
int delegpt_rrset_add_ns(struct delegpt* dp, struct regional* regional,
	struct ub_packed_rrset_key* ns_rrset, uint8_t lame);

/**
 * Add target address to the delegation point.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param name: name for which target was found (must be in nslist).
 *	This name is marked resolved.
 * @param namelen: length of name.
 * @param addr: the address.
 * @param addrlen: the length of addr.
 * @param bogus: security status for the address, pass true if bogus.
 * @param lame: address is lame.
 * @param additions: will be set to 1 if a new address is added
 * @return false on error.
 */
int delegpt_add_target(struct delegpt* dp, struct regional* regional, 
	uint8_t* name, size_t namelen, struct sockaddr_storage* addr, 
	socklen_t addrlen, uint8_t bogus, uint8_t lame, int* additions);

/**
 * Add A RRset to delegpt.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param rrset: RRset A to add.
 * @param lame: rrset is lame, disprefer it.
 * @param additions: will be set to 1 if a new address is added
 * @return 0 on alloc error.
 */
int delegpt_add_rrset_A(struct delegpt* dp, struct regional* regional, 
	struct ub_packed_rrset_key* rrset, uint8_t lame, int* additions);

/**
 * Add AAAA RRset to delegpt.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param rrset: RRset AAAA to add.
 * @param lame: rrset is lame, disprefer it.
 * @param additions: will be set to 1 if a new address is added
 * @return 0 on alloc error.
 */
int delegpt_add_rrset_AAAA(struct delegpt* dp, struct regional* regional, 
	struct ub_packed_rrset_key* rrset, uint8_t lame, int* additions);

/**
 * Add any RRset to delegpt.
 * Does not check for duplicates added.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param rrset: RRset to add, NS, A, AAAA.
 * @param lame: rrset is lame, disprefer it.
 * @param additions: will be set to 1 if a new address is added
 * @return 0 on alloc error.
 */
int delegpt_add_rrset(struct delegpt* dp, struct regional* regional, 
	struct ub_packed_rrset_key* rrset, uint8_t lame, int* additions);

/**
 * Add address to the delegation point. No servername is associated or checked.
 * @param dp: delegation point.
 * @param regional: where to allocate the info.
 * @param addr: the address.
 * @param addrlen: the length of addr.
 * @param bogus: if address is bogus.
 * @param lame: if address is lame.
 * @param tls_auth_name: TLS authentication name (or NULL).
 * @param port: the port to use; if -1 the port is taken from addr.
 * @param additions: will be set to 1 if a new address is added
 * @return false on error.
 */
int delegpt_add_addr(struct delegpt* dp, struct regional* regional,
	struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t bogus, uint8_t lame, char* tls_auth_name, int port,
	int* additions);

/** 
 * Find NS record in name list of delegation point.
 * @param dp: delegation point.
 * @param name: name of nameserver to look for, uncompressed wireformat.
 * @param namelen: length of name.
 * @return the ns structure or NULL if not found.
 */
struct delegpt_ns* delegpt_find_ns(struct delegpt* dp, uint8_t* name, 
	size_t namelen);

/** 
 * Find address record in total list of delegation point.
 * @param dp: delegation point.
 * @param addr: address
 * @param addrlen: length of addr
 * @return the addr structure or NULL if not found.
 */
struct delegpt_addr* delegpt_find_addr(struct delegpt* dp, 
	struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Print the delegation point to the log. For debugging.
 * @param v: verbosity value that is needed to emit to log.
 * @param dp: delegation point.
 */
void delegpt_log(enum verbosity_value v, struct delegpt* dp);

/** count NS and number missing for logging */
void delegpt_count_ns(struct delegpt* dp, size_t* numns, size_t* missing);

/** count addresses, and number in result and available lists, for logging */
void delegpt_count_addr(struct delegpt* dp, size_t* numaddr, size_t* numres, 
	size_t* numavail);

/**
 * Add all usable targets to the result list.
 * @param dp: delegation point.
 */
void delegpt_add_unused_targets(struct delegpt* dp);

/**
 * Count number of missing targets. These are ns names with no resolved flag.
 * @param dp: delegation point.
 * @param alllame: if set, check if all the missing targets are lame.
 * @return number of missing targets (or 0).
 */
size_t delegpt_count_missing_targets(struct delegpt* dp, int* alllame);

/** count total number of targets in dp */
size_t delegpt_count_targets(struct delegpt* dp);

/**
 * Create new delegation point from a dns message
 *
 * Note that this method does not actually test to see if the message is an
 * actual referral. It really is just checking to see if it can construct a
 * delegation point, so the message could be of some other type (some ANSWER
 * messages, some CNAME messages, generally.) Note that the resulting
 * DelegationPoint will contain targets for all "relevant" glue (i.e.,
 * address records whose ownernames match the target of one of the NS
 * records), so if policy dictates that some glue should be discarded beyond
 * that, discard it before calling this method. Note that this method will
 * find "glue" in either the ADDITIONAL section or the ANSWER section.
 *
 * @param msg: the dns message, referral.
 * @param regional: where to allocate delegation point.
 * @return new delegation point or NULL on alloc error, or if the
 *         message was not appropriate.
 */
struct delegpt* delegpt_from_message(struct dns_msg* msg, 
	struct regional* regional);

/**
 * Mark negative return in delegation point for specific nameserver.
 * sets the got4 or got6 to negative, updates the ns->resolved.
 * @param ns: the nameserver in the delegpt.
 * @param qtype: A or AAAA (host order).
 */
void delegpt_mark_neg(struct delegpt_ns* ns, uint16_t qtype);

/**
 * Add negative message to delegation point.
 * @param dp: delegation point.
 * @param msg: the message added, marks off A or AAAA from an NS entry.
 */
void delegpt_add_neg_msg(struct delegpt* dp, struct msgreply_entry* msg);

/**
 * Register the fact that there is no ipv6 and thus AAAAs are not going 
 * to be queried for or be useful.
 * @param dp: the delegation point. Updated to reflect no ipv6.
 */
void delegpt_no_ipv6(struct delegpt* dp);

/**
 * Register the fact that there is no ipv4 and thus As are not going 
 * to be queried for or be useful.
 * @param dp: the delegation point. Updated to reflect no ipv4.
 */
void delegpt_no_ipv4(struct delegpt* dp);

/** 
 * create malloced delegation point, with the given name 
 * @param name: uncompressed wireformat of delegpt name.
 * @return NULL on alloc failure
 */
struct delegpt* delegpt_create_mlc(uint8_t* name);

/** 
 * free malloced delegation point.
 * @param dp: must have been created with delegpt_create_mlc, free'd. 
 */
void delegpt_free_mlc(struct delegpt* dp);

/**
 * Set name of delegation point.
 * @param dp: delegation point. malloced.
 * @param name: name to use.
 * @return false on error.
 */
int delegpt_set_name_mlc(struct delegpt* dp, uint8_t* name);

/**
 * add a name to malloced delegation point.
 * @param dp: must have been created with delegpt_create_mlc. 
 * @param name: the name to add.
 * @param lame: the name is lame, disprefer.
 * @param tls_auth_name: TLS authentication name (or NULL).
 * @param port: port to use for resolved addresses.
 * @return false on error.
 */
int delegpt_add_ns_mlc(struct delegpt* dp, uint8_t* name, uint8_t lame,
	char* tls_auth_name, int port);

/**
 * add an address to a malloced delegation point.
 * @param dp: must have been created with delegpt_create_mlc.
 * @param addr: the address.
 * @param addrlen: the length of addr.
 * @param bogus: if address is bogus.
 * @param lame: if address is lame.
 * @param tls_auth_name: TLS authentication name (or NULL).
 * @param port: the port to use; if -1 the port is taken from addr.
 * @return false on error.
 */
int delegpt_add_addr_mlc(struct delegpt* dp, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t bogus, uint8_t lame, char* tls_auth_name,
	int port);

/**
 * Add target address to the delegation point.
 * @param dp: must have been created with delegpt_create_mlc. 
 * @param name: name for which target was found (must be in nslist).
 *	This name is marked resolved.
 * @param namelen: length of name.
 * @param addr: the address.
 * @param addrlen: the length of addr.
 * @param bogus: security status for the address, pass true if bogus.
 * @param lame: address is lame.
 * @return false on error.
 */
int delegpt_add_target_mlc(struct delegpt* dp, uint8_t* name, size_t namelen,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t bogus,
	uint8_t lame);

/** get memory in use by dp */
size_t delegpt_get_mem(struct delegpt* dp);

/**
 * See if the addr is on the result list.
 * @param dp: delegation point.
 * @param find: the pointer is searched for on the result list.
 * @return 1 if found, 0 if not found.
 */
int delegpt_addr_on_result_list(struct delegpt* dp, struct delegpt_addr* find);

/**
 * Remove the addr from the usable list.
 * @param dp: the delegation point.
 * @param del: the addr to remove from the list, the pointer is searched for.
 */
void delegpt_usable_list_remove_addr(struct delegpt* dp,
	struct delegpt_addr* del);

/**
 * Add the delegpt_addr back to the result list, if it is not already on
 * the result list. Also removes it from the usable list.
 * @param dp: delegation point.
 * @param a: addr to add, nothing happens if it is already on the result list.
 *	It is removed from the usable list.
 */
void delegpt_add_to_result_list(struct delegpt* dp, struct delegpt_addr* a);

#endif /* ITERATOR_ITER_DELEGPT_H */
