// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handshake request lifetime events
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/fdtable.h>
#include <linux/rhashtable.h>

#include <net/sock.h>
#include <net/genetlink.h>
#include <net/netns/generic.h>

#include <kunit/visibility.h>

#include <uapi/linux/handshake.h>
#include "handshake.h"

#include <trace/events/handshake.h>

/*
 * We need both a handshake_req -> sock mapping, and a sock ->
 * handshake_req mapping. Both are one-to-one.
 *
 * To avoid adding another pointer field to struct sock, net/handshake
 * maintains a hash table, indexed by the memory address of @sock, to
 * find the struct handshake_req outstanding for that socket. The
 * reverse direction uses a simple pointer field in the handshake_req
 * struct.
 */

static struct rhashtable handshake_rhashtbl ____cacheline_aligned_in_smp;

static const struct rhashtable_params handshake_rhash_params = {
	.key_len		= sizeof_field(struct handshake_req, hr_sk),
	.key_offset		= offsetof(struct handshake_req, hr_sk),
	.head_offset		= offsetof(struct handshake_req, hr_rhash),
	.automatic_shrinking	= true,
};

int handshake_req_hash_init(void)
{
	return rhashtable_init(&handshake_rhashtbl, &handshake_rhash_params);
}

void handshake_req_hash_destroy(void)
{
	rhashtable_destroy(&handshake_rhashtbl);
}

struct handshake_req *handshake_req_hash_lookup(struct sock *sk)
{
	return rhashtable_lookup_fast(&handshake_rhashtbl, &sk,
				      handshake_rhash_params);
}
EXPORT_SYMBOL_IF_KUNIT(handshake_req_hash_lookup);

static bool handshake_req_hash_add(struct handshake_req *req)
{
	int ret;

	ret = rhashtable_lookup_insert_fast(&handshake_rhashtbl,
					    &req->hr_rhash,
					    handshake_rhash_params);
	return ret == 0;
}

static void handshake_req_destroy(struct handshake_req *req)
{
	if (req->hr_proto->hp_destroy)
		req->hr_proto->hp_destroy(req);
	rhashtable_remove_fast(&handshake_rhashtbl, &req->hr_rhash,
			       handshake_rhash_params);
	kfree(req);
}

static void handshake_sk_destruct(struct sock *sk)
{
	void (*sk_destruct)(struct sock *sk);
	struct handshake_req *req;

	req = handshake_req_hash_lookup(sk);
	if (!req)
		return;

	trace_handshake_destruct(sock_net(sk), req, sk);
	sk_destruct = req->hr_odestruct;
	handshake_req_destroy(req);
	if (sk_destruct)
		sk_destruct(sk);
}

/**
 * handshake_req_alloc - Allocate a handshake request
 * @proto: security protocol
 * @flags: memory allocation flags
 *
 * Returns an initialized handshake_req or NULL.
 */
struct handshake_req *handshake_req_alloc(const struct handshake_proto *proto,
					  gfp_t flags)
{
	struct handshake_req *req;

	if (!proto)
		return NULL;
	if (proto->hp_handler_class <= HANDSHAKE_HANDLER_CLASS_NONE)
		return NULL;
	if (proto->hp_handler_class >= HANDSHAKE_HANDLER_CLASS_MAX)
		return NULL;
	if (!proto->hp_accept || !proto->hp_done)
		return NULL;

	req = kzalloc(struct_size(req, hr_priv, proto->hp_privsize), flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->hr_list);
	req->hr_proto = proto;
	return req;
}
EXPORT_SYMBOL(handshake_req_alloc);

/**
 * handshake_req_private - Get per-handshake private data
 * @req: handshake arguments
 *
 */
void *handshake_req_private(struct handshake_req *req)
{
	return (void *)&req->hr_priv;
}
EXPORT_SYMBOL(handshake_req_private);

static bool __add_pending_locked(struct handshake_net *hn,
				 struct handshake_req *req)
{
	if (WARN_ON_ONCE(!list_empty(&req->hr_list)))
		return false;
	hn->hn_pending++;
	list_add_tail(&req->hr_list, &hn->hn_requests);
	return true;
}

static void __remove_pending_locked(struct handshake_net *hn,
				    struct handshake_req *req)
{
	hn->hn_pending--;
	list_del_init(&req->hr_list);
}

/*
 * Returns %true if the request was found on @net's pending list,
 * otherwise %false.
 *
 * If @req was on a pending list, it has not yet been accepted.
 */
static bool remove_pending(struct handshake_net *hn, struct handshake_req *req)
{
	bool ret = false;

	spin_lock(&hn->hn_lock);
	if (!list_empty(&req->hr_list)) {
		__remove_pending_locked(hn, req);
		ret = true;
	}
	spin_unlock(&hn->hn_lock);

	return ret;
}

struct handshake_req *handshake_req_next(struct handshake_net *hn, int class)
{
	struct handshake_req *req, *pos;

	req = NULL;
	spin_lock(&hn->hn_lock);
	list_for_each_entry(pos, &hn->hn_requests, hr_list) {
		if (pos->hr_proto->hp_handler_class != class)
			continue;
		__remove_pending_locked(hn, pos);
		req = pos;
		break;
	}
	spin_unlock(&hn->hn_lock);

	return req;
}
EXPORT_SYMBOL_IF_KUNIT(handshake_req_next);

/**
 * handshake_req_submit - Submit a handshake request
 * @sock: open socket on which to perform the handshake
 * @req: handshake arguments
 * @flags: memory allocation flags
 *
 * Return values:
 *   %0: Request queued
 *   %-EINVAL: Invalid argument
 *   %-EBUSY: A handshake is already under way for this socket
 *   %-ESRCH: No handshake agent is available
 *   %-EAGAIN: Too many pending handshake requests
 *   %-ENOMEM: Failed to allocate memory
 *   %-EMSGSIZE: Failed to construct notification message
 *   %-EOPNOTSUPP: Handshake module not initialized
 *
 * A zero return value from handshake_req_submit() means that
 * exactly one subsequent completion callback is guaranteed.
 *
 * A negative return value from handshake_req_submit() means that
 * no completion callback will be done and that @req has been
 * destroyed.
 */
int handshake_req_submit(struct socket *sock, struct handshake_req *req,
			 gfp_t flags)
{
	struct handshake_net *hn;
	struct net *net;
	int ret;

	if (!sock || !req || !sock->file) {
		kfree(req);
		return -EINVAL;
	}

	req->hr_sk = sock->sk;
	if (!req->hr_sk) {
		kfree(req);
		return -EINVAL;
	}
	req->hr_odestruct = req->hr_sk->sk_destruct;
	req->hr_sk->sk_destruct = handshake_sk_destruct;
	req->hr_file = sock->file;

	ret = -EOPNOTSUPP;
	net = sock_net(req->hr_sk);
	hn = handshake_pernet(net);
	if (!hn)
		goto out_err;

	ret = -EAGAIN;
	if (READ_ONCE(hn->hn_pending) >= hn->hn_pending_max)
		goto out_err;

	spin_lock(&hn->hn_lock);
	ret = -EOPNOTSUPP;
	if (test_bit(HANDSHAKE_F_NET_DRAINING, &hn->hn_flags))
		goto out_unlock;
	ret = -EBUSY;
	if (!handshake_req_hash_add(req))
		goto out_unlock;
	if (!__add_pending_locked(hn, req))
		goto out_unlock;
	spin_unlock(&hn->hn_lock);

	ret = handshake_genl_notify(net, req->hr_proto, flags);
	if (ret) {
		trace_handshake_notify_err(net, req, req->hr_sk, ret);
		if (remove_pending(hn, req))
			goto out_err;
	}

	/* Prevent socket release while a handshake request is pending */
	sock_hold(req->hr_sk);

	trace_handshake_submit(net, req, req->hr_sk);
	return 0;

out_unlock:
	spin_unlock(&hn->hn_lock);
out_err:
	trace_handshake_submit_err(net, req, req->hr_sk, ret);
	handshake_req_destroy(req);
	return ret;
}
EXPORT_SYMBOL(handshake_req_submit);

void handshake_complete(struct handshake_req *req, unsigned int status,
			struct genl_info *info)
{
	struct sock *sk = req->hr_sk;
	struct net *net = sock_net(sk);

	if (!test_and_set_bit(HANDSHAKE_F_REQ_COMPLETED, &req->hr_flags)) {
		trace_handshake_complete(net, req, sk, status);
		req->hr_proto->hp_done(req, status, info);

		/* Handshake request is no longer pending */
		sock_put(sk);
	}
}
EXPORT_SYMBOL_IF_KUNIT(handshake_complete);

/**
 * handshake_req_cancel - Cancel an in-progress handshake
 * @sk: socket on which there is an ongoing handshake
 *
 * Request cancellation races with request completion. To determine
 * who won, callers examine the return value from this function.
 *
 * Return values:
 *   %true - Uncompleted handshake request was canceled
 *   %false - Handshake request already completed or not found
 */
bool handshake_req_cancel(struct sock *sk)
{
	struct handshake_req *req;
	struct handshake_net *hn;
	struct net *net;

	net = sock_net(sk);
	req = handshake_req_hash_lookup(sk);
	if (!req) {
		trace_handshake_cancel_none(net, req, sk);
		return false;
	}

	hn = handshake_pernet(net);
	if (hn && remove_pending(hn, req)) {
		/* Request hadn't been accepted */
		goto out_true;
	}
	if (test_and_set_bit(HANDSHAKE_F_REQ_COMPLETED, &req->hr_flags)) {
		/* Request already completed */
		trace_handshake_cancel_busy(net, req, sk);
		return false;
	}

	/* Request accepted and waiting for DONE */
	fput(req->hr_file);

out_true:
	trace_handshake_cancel(net, req, sk);

	/* Handshake request is no longer pending */
	sock_put(sk);
	return true;
}
EXPORT_SYMBOL(handshake_req_cancel);
