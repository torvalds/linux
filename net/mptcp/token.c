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
#include <linux/radix-tree.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/protocol.h>
#include <net/mptcp.h>
#include "protocol.h"

static RADIX_TREE(token_tree, GFP_ATOMIC);
static RADIX_TREE(token_req_tree, GFP_ATOMIC);
static DEFINE_SPINLOCK(token_tree_lock);
static int token_used __read_mostly;

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
	int err;

	while (1) {
		u32 token;

		mptcp_crypto_key_gen_sha(&subflow_req->local_key,
					 &subflow_req->token,
					 &subflow_req->idsn);
		pr_debug("req=%p local_key=%llu, token=%u, idsn=%llu\n",
			 req, subflow_req->local_key, subflow_req->token,
			 subflow_req->idsn);

		token = subflow_req->token;
		spin_lock_bh(&token_tree_lock);
		if (!radix_tree_lookup(&token_req_tree, token) &&
		    !radix_tree_lookup(&token_tree, token))
			break;
		spin_unlock_bh(&token_tree_lock);
	}

	err = radix_tree_insert(&token_req_tree,
				subflow_req->token, &token_used);
	spin_unlock_bh(&token_tree_lock);
	return err;
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
	struct sock *mptcp_sock = subflow->conn;
	int err;

	while (1) {
		u32 token;

		mptcp_crypto_key_gen_sha(&subflow->local_key, &subflow->token,
					 &subflow->idsn);

		pr_debug("ssk=%p, local_key=%llu, token=%u, idsn=%llu\n",
			 sk, subflow->local_key, subflow->token, subflow->idsn);

		token = subflow->token;
		spin_lock_bh(&token_tree_lock);
		if (!radix_tree_lookup(&token_req_tree, token) &&
		    !radix_tree_lookup(&token_tree, token))
			break;
		spin_unlock_bh(&token_tree_lock);
	}
	err = radix_tree_insert(&token_tree, subflow->token, mptcp_sock);
	spin_unlock_bh(&token_tree_lock);

	return err;
}

/**
 * mptcp_token_new_accept - insert token for later processing
 * @token: the token to insert to the tree
 * @conn: the just cloned socket linked to the new connection
 *
 * Called when a SYN packet creates a new logical connection, i.e.
 * is not a join request.
 */
int mptcp_token_new_accept(u32 token, struct sock *conn)
{
	int err;

	spin_lock_bh(&token_tree_lock);
	err = radix_tree_insert(&token_tree, token, conn);
	spin_unlock_bh(&token_tree_lock);

	return err;
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
	struct sock *conn;

	spin_lock_bh(&token_tree_lock);
	conn = radix_tree_lookup(&token_tree, token);
	if (conn) {
		/* token still reserved? */
		if (conn == (struct sock *)&token_used)
			conn = NULL;
		else
			sock_hold(conn);
	}
	spin_unlock_bh(&token_tree_lock);

	return mptcp_sk(conn);
}

/**
 * mptcp_token_destroy_request - remove mptcp connection/token
 * @token: token of mptcp connection to remove
 *
 * Remove not-yet-fully-established incoming connection identified
 * by @token.
 */
void mptcp_token_destroy_request(u32 token)
{
	spin_lock_bh(&token_tree_lock);
	radix_tree_delete(&token_req_tree, token);
	spin_unlock_bh(&token_tree_lock);
}

/**
 * mptcp_token_destroy - remove mptcp connection/token
 * @token: token of mptcp connection to remove
 *
 * Remove the connection identified by @token.
 */
void mptcp_token_destroy(u32 token)
{
	spin_lock_bh(&token_tree_lock);
	radix_tree_delete(&token_tree, token);
	spin_unlock_bh(&token_tree_lock);
}
