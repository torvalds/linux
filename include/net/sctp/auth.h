/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SCTP kernel implementation
 * (C) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *
 * This file is part of the SCTP kernel implementation
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *   Vlad Yasevich     <vladislav.yasevich@hp.com>
 */

#ifndef __sctp_auth_h__
#define __sctp_auth_h__

#include <linux/list.h>
#include <linux/refcount.h>

struct sctp_endpoint;
struct sctp_association;
struct sctp_authkey;
struct sctp_hmacalgo;
struct crypto_shash;

/*
 * Define a generic struct that will hold all the info
 * necessary for an HMAC transform
 */
struct sctp_hmac {
	__u16 hmac_id;		/* one of the above ids */
	char *hmac_name;	/* name for loading */
	__u16 hmac_len;		/* length of the signature */
};

/* This is generic structure that containst authentication bytes used
 * as keying material.  It's a what is referred to as byte-vector all
 * over SCTP-AUTH
 */
struct sctp_auth_bytes {
	refcount_t refcnt;
	__u32 len;
	__u8  data[];
};

/* Definition for a shared key, weather endpoint or association */
struct sctp_shared_key {
	struct list_head key_list;
	struct sctp_auth_bytes *key;
	refcount_t refcnt;
	__u16 key_id;
	__u8 deactivated;
};

#define key_for_each(__key, __list_head) \
	list_for_each_entry(__key, __list_head, key_list)

#define key_for_each_safe(__key, __tmp, __list_head) \
	list_for_each_entry_safe(__key, __tmp, __list_head, key_list)

static inline void sctp_auth_key_hold(struct sctp_auth_bytes *key)
{
	if (!key)
		return;

	refcount_inc(&key->refcnt);
}

void sctp_auth_key_put(struct sctp_auth_bytes *key);
struct sctp_shared_key *sctp_auth_shkey_create(__u16 key_id, gfp_t gfp);
void sctp_auth_destroy_keys(struct list_head *keys);
int sctp_auth_asoc_init_active_key(struct sctp_association *asoc, gfp_t gfp);
struct sctp_shared_key *sctp_auth_get_shkey(
				const struct sctp_association *asoc,
				__u16 key_id);
int sctp_auth_asoc_copy_shkeys(const struct sctp_endpoint *ep,
				struct sctp_association *asoc,
				gfp_t gfp);
int sctp_auth_init_hmacs(struct sctp_endpoint *ep, gfp_t gfp);
void sctp_auth_destroy_hmacs(struct crypto_shash *auth_hmacs[]);
struct sctp_hmac *sctp_auth_get_hmac(__u16 hmac_id);
struct sctp_hmac *sctp_auth_asoc_get_hmac(const struct sctp_association *asoc);
void sctp_auth_asoc_set_default_hmac(struct sctp_association *asoc,
				     struct sctp_hmac_algo_param *hmacs);
int sctp_auth_asoc_verify_hmac_id(const struct sctp_association *asoc,
				    __be16 hmac_id);
int sctp_auth_send_cid(enum sctp_cid chunk,
		       const struct sctp_association *asoc);
int sctp_auth_recv_cid(enum sctp_cid chunk,
		       const struct sctp_association *asoc);
void sctp_auth_calculate_hmac(const struct sctp_association *asoc,
			      struct sk_buff *skb, struct sctp_auth_chunk *auth,
			      struct sctp_shared_key *ep_key, gfp_t gfp);
void sctp_auth_shkey_release(struct sctp_shared_key *sh_key);
void sctp_auth_shkey_hold(struct sctp_shared_key *sh_key);

/* API Helpers */
int sctp_auth_ep_add_chunkid(struct sctp_endpoint *ep, __u8 chunk_id);
int sctp_auth_ep_set_hmacs(struct sctp_endpoint *ep,
			    struct sctp_hmacalgo *hmacs);
int sctp_auth_set_key(struct sctp_endpoint *ep, struct sctp_association *asoc,
		      struct sctp_authkey *auth_key);
int sctp_auth_set_active_key(struct sctp_endpoint *ep,
			     struct sctp_association *asoc, __u16 key_id);
int sctp_auth_del_key_id(struct sctp_endpoint *ep,
			 struct sctp_association *asoc, __u16 key_id);
int sctp_auth_deact_key_id(struct sctp_endpoint *ep,
			   struct sctp_association *asoc, __u16 key_id);

#endif
