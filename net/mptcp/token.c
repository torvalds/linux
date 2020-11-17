// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP token management
 * Copyright (c) 2017 - 2019, Intel Corporation.
 *
 * Note: This code is based on mptcp_ctrl.c from multipath-tcp.org,
 *       authored by:
 *
 *       Sébastien Barré <sebastien.barre@uclouvain.be>
 *       Christoph Paasch <christoph.paasch@uclouvain.be>
 *       Jaakko Korkeaniemi <jaakko.korkeaniemi@aalto.fi>
 *       Gregory Detal <gregory.detal@uclouvain.be>
 *       Fabien Duchêne <fabien.duchene@uclouvain.be>
 *       Andreas Seelinger <Andreas.Seelinger@rwth-aachen.de>
 *       Lavkesh Lahngir <lavkesh51@gmail.com>
 *       Andreas Ripke <ripke@neclab.eu>
 *       Vlad Dogaru <vlad.dogaru@intel.com>
 *       Octavian Purdila <octavian.purdila@intel.com>
 *       John Ronan <jronan@tssg.org>
 *       Catalin Nicutar <catalin.nicutar@gmail.com>
 *       Brandon Heller <brandonh@stanford.edu>
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/protocol.h>
#include <net/mptcp.h>
#include "protocol.h"

#define TOKEN_MAX_RETRIES	4
#define TOKEN_MAX_CHAIN_LEN	4

struct token_bucket {
	spinlock_t		lock;
	int			chain_len;
	struct hlist_nulls_head	req_chain;
	struct hlist_nulls_head	msk_chain;
};

static struct token_bucket *token_hash __read_mostly;
static unsigned int token_mask __read_mostly;

static struct token_bucket *token_bucket(u32 token)
{
	return &token_hash[token & token_mask];
}

/* called with bucket lock held */
static struct mptcp_subflow_request_sock *
__token_lookup_req(struct token_bucket *t, u32 token)
{
	struct mptcp_subflow_request_sock *req;
	struct hlist_nulls_node *pos;

	hlist_nulls_for_each_entry_rcu(req, pos, &t->req_chain, token_node)
		if (req->token == token)
			return req;
	return NULL;
}

/* called with bucket lock held */
static struct mptcp_sock *
__token_lookup_msk(struct token_bucket *t, u32 token)
{
	struct hlist_nulls_node *pos;
	struct sock *sk;

	sk_nulls_for_each_rcu(sk, pos, &t->msk_chain)
		if (mptcp_sk(sk)->token == token)
			return mptcp_sk(sk);
	return NULL;
}

static bool __token_bucket_busy(struct token_bucket *t, u32 token)
{
	return !token || t->chain_len >= TOKEN_MAX_CHAIN_LEN ||
	       __token_lookup_req(t, token) || __token_lookup_msk(t, token);
}

static void mptcp_crypto_key_gen_sha(u64 *key, u32 *token, u64 *idsn)
{
	/* we might consider a faster version that computes the key as a
	 * hash of some information available in the MPTCP socket. Use
	 * random data at the moment, as it's probably the safest option
	 * in case multiple sockets are opened in different namespaces at
	 * the same time.
	 */
	get_random_bytes(key, sizeof(u64));
	mptcp_crypto_key_sha(*key, token, idsn);
}

/**
 * mptcp_token_new_request - create new key/idsn/token for subflow_request
 * @req: the request socket
 *
 * This function is called when a new mptcp connection is coming in.
 *
 * It creates a unique token to identify the new mptcp connection,
 * a secret local key and the initial data sequence number (idsn).
 *
 * Returns 0 on success.
 */
int mptcp_token_new_request(struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct token_bucket *bucket;
	u32 token;

	mptcp_crypto_key_sha(subflow_req->local_key,
			     &subflow_req->token,
			     &subflow_req->idsn);
	pr_debug("req=%p local_key=%llu, token=%u, idsn=%llu\n",
		 req, subflow_req->local_key, subflow_req->token,
		 subflow_req->idsn);

	token = subflow_req->token;
	bucket = token_bucket(token);
	spin_lock_bh(&bucket->lock);
	if (__token_bucket_busy(bucket, token)) {
		spin_unlock_bh(&bucket->lock);
		return -EBUSY;
	}

	hlist_nulls_add_head_rcu(&subflow_req->token_node, &bucket->req_chain);
	bucket->chain_len++;
	spin_unlock_bh(&bucket->lock);
	return 0;
}

/**
 * mptcp_token_new_connect - create new key/idsn/token for subflow
 * @sk: the socket that will initiate a connection
 *
 * This function is called when a new outgoing mptcp connection is
 * initiated.
 *
 * It creates a unique token to identify the new mptcp connection,
 * a secret local key and the initial data sequence number (idsn).
 *
 * On success, the mptcp connection can be found again using
 * the computed token at a later time, this is needed to process
 * join requests.
 *
 * returns 0 on success.
 */
int mptcp_token_new_connect(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	int retries = TOKEN_MAX_RETRIES;
	struct token_bucket *bucket;

	pr_debug("ssk=%p, local_key=%llu, token=%u, idsn=%llu\n",
		 sk, subflow->local_key, subflow->token, subflow->idsn);

again:
	mptcp_crypto_key_gen_sha(&subflow->local_key, &subflow->token,
				 &subflow->idsn);

	bucket = token_bucket(subflow->token);
	spin_lock_bh(&bucket->lock);
	if (__token_bucket_busy(bucket, subflow->token)) {
		spin_unlock_bh(&bucket->lock);
		if (!--retries)
			return -EBUSY;
		goto again;
	}

	WRITE_ONCE(msk->token, subflow->token);
	__sk_nulls_add_node_rcu((struct sock *)msk, &bucket->msk_chain);
	bucket->chain_len++;
	spin_unlock_bh(&bucket->lock);
	return 0;
}

/**
 * mptcp_token_accept - replace a req sk with full sock in token hash
 * @req: the request socket to be removed
 * @msk: the just cloned socket linked to the new connection
 *
 * Called when a SYN packet creates a new logical connection, i.e.
 * is not a join request.
 */
void mptcp_token_accept(struct mptcp_subflow_request_sock *req,
			struct mptcp_sock *msk)
{
	struct mptcp_subflow_request_sock *pos;
	struct token_bucket *bucket;

	bucket = token_bucket(req->token);
	spin_lock_bh(&bucket->lock);

	/* pedantic lookup check for the moved token */
	pos = __token_lookup_req(bucket, req->token);
	if (!WARN_ON_ONCE(pos != req))
		hlist_nulls_del_init_rcu(&req->token_node);
	__sk_nulls_add_node_rcu((struct sock *)msk, &bucket->msk_chain);
	spin_unlock_bh(&bucket->lock);
}

bool mptcp_token_exists(u32 token)
{
	struct hlist_nulls_node *pos;
	struct token_bucket *bucket;
	struct mptcp_sock *msk;
	struct sock *sk;

	rcu_read_lock();
	bucket = token_bucket(token);

again:
	sk_nulls_for_each_rcu(sk, pos, &bucket->msk_chain) {
		msk = mptcp_sk(sk);
		if (READ_ONCE(msk->token) == token)
			goto found;
	}
	if (get_nulls_value(pos) != (token & token_mask))
		goto again;

	rcu_read_unlock();
	return false;
found:
	rcu_read_unlock();
	return true;
}

/**
 * mptcp_token_get_sock - retrieve mptcp connection sock using its token
 * @token: token of the mptcp connection to retrieve
 *
 * This function returns the mptcp connection structure with the given token.
 * A reference count on the mptcp socket returned is taken.
 *
 * returns NULL if no connection with the given token value exists.
 */
struct mptcp_sock *mptcp_token_get_sock(u32 token)
{
	struct hlist_nulls_node *pos;
	struct token_bucket *bucket;
	struct mptcp_sock *msk;
	struct sock *sk;

	rcu_read_lock();
	bucket = token_bucket(token);

again:
	sk_nulls_for_each_rcu(sk, pos, &bucket->msk_chain) {
		msk = mptcp_sk(sk);
		if (READ_ONCE(msk->token) != token)
			continue;
		if (!refcount_inc_not_zero(&sk->sk_refcnt))
			goto not_found;
		if (READ_ONCE(msk->token) != token) {
			sock_put(sk);
			goto again;
		}
		goto found;
	}
	if (get_nulls_value(pos) != (token & token_mask))
		goto again;

not_found:
	msk = NULL;

found:
	rcu_read_unlock();
	return msk;
}
EXPORT_SYMBOL_GPL(mptcp_token_get_sock);

/**
 * mptcp_token_iter_next - iterate over the token container from given pos
 * @net: namespace to be iterated
 * @s_slot: start slot number
 * @s_num: start number inside the given lock
 *
 * This function returns the first mptcp connection structure found inside the
 * token container starting from the specified position, or NULL.
 *
 * On successful iteration, the iterator is move to the next position and the
 * the acquires a reference to the returned socket.
 */
struct mptcp_sock *mptcp_token_iter_next(const struct net *net, long *s_slot,
					 long *s_num)
{
	struct mptcp_sock *ret = NULL;
	struct hlist_nulls_node *pos;
	int slot, num = 0;

	for (slot = *s_slot; slot <= token_mask; *s_num = 0, slot++) {
		struct token_bucket *bucket = &token_hash[slot];
		struct sock *sk;

		num = 0;

		if (hlist_nulls_empty(&bucket->msk_chain))
			continue;

		rcu_read_lock();
		sk_nulls_for_each_rcu(sk, pos, &bucket->msk_chain) {
			++num;
			if (!net_eq(sock_net(sk), net))
				continue;

			if (num <= *s_num)
				continue;

			if (!refcount_inc_not_zero(&sk->sk_refcnt))
				continue;

			if (!net_eq(sock_net(sk), net)) {
				sock_put(sk);
				continue;
			}

			ret = mptcp_sk(sk);
			rcu_read_unlock();
			goto out;
		}
		rcu_read_unlock();
	}

out:
	*s_slot = slot;
	*s_num = num;
	return ret;
}
EXPORT_SYMBOL_GPL(mptcp_token_iter_next);

/**
 * mptcp_token_destroy_request - remove mptcp connection/token
 * @req: mptcp request socket dropping the token
 *
 * Remove the token associated to @req.
 */
void mptcp_token_destroy_request(struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_subflow_request_sock *pos;
	struct token_bucket *bucket;

	if (hlist_nulls_unhashed(&subflow_req->token_node))
		return;

	bucket = token_bucket(subflow_req->token);
	spin_lock_bh(&bucket->lock);
	pos = __token_lookup_req(bucket, subflow_req->token);
	if (!WARN_ON_ONCE(pos != subflow_req)) {
		hlist_nulls_del_init_rcu(&pos->token_node);
		bucket->chain_len--;
	}
	spin_unlock_bh(&bucket->lock);
}

/**
 * mptcp_token_destroy - remove mptcp connection/token
 * @msk: mptcp connection dropping the token
 *
 * Remove the token associated to @msk
 */
void mptcp_token_destroy(struct mptcp_sock *msk)
{
	struct token_bucket *bucket;
	struct mptcp_sock *pos;

	if (sk_unhashed((struct sock *)msk))
		return;

	bucket = token_bucket(msk->token);
	spin_lock_bh(&bucket->lock);
	pos = __token_lookup_msk(bucket, msk->token);
	if (!WARN_ON_ONCE(pos != msk)) {
		__sk_nulls_del_node_init_rcu((struct sock *)pos);
		bucket->chain_len--;
	}
	spin_unlock_bh(&bucket->lock);
}

void __init mptcp_token_init(void)
{
	int i;

	token_hash = alloc_large_system_hash("MPTCP token",
					     sizeof(struct token_bucket),
					     0,
					     20,/* one slot per 1MB of memory */
					     HASH_ZERO,
					     NULL,
					     &token_mask,
					     0,
					     64 * 1024);
	for (i = 0; i < token_mask + 1; ++i) {
		INIT_HLIST_NULLS_HEAD(&token_hash[i].req_chain, i);
		INIT_HLIST_NULLS_HEAD(&token_hash[i].msk_chain, i);
		spin_lock_init(&token_hash[i].lock);
	}
}

#if IS_MODULE(CONFIG_MPTCP_KUNIT_TESTS)
EXPORT_SYMBOL_GPL(mptcp_token_new_request);
EXPORT_SYMBOL_GPL(mptcp_token_new_connect);
EXPORT_SYMBOL_GPL(mptcp_token_accept);
EXPORT_SYMBOL_GPL(mptcp_token_destroy_request);
EXPORT_SYMBOL_GPL(mptcp_token_destroy);
#endif
