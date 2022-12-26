// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel implementation
 *
 * This abstraction represents an SCTP endpoint.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@austin.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Dajiang Zhang <dajiang.zhang@nokia.com>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/random.h>	/* get_random_bytes() */
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers. */
static void sctp_endpoint_bh_rcv(struct work_struct *work);

/*
 * Initialize the base fields of the endpoint structure.
 */
static struct sctp_endpoint *sctp_endpoint_init(struct sctp_endpoint *ep,
						struct sock *sk,
						gfp_t gfp)
{
	struct net *net = sock_net(sk);
	struct sctp_shared_key *null_key;

	ep->digest = kzalloc(SCTP_SIGNATURE_SIZE, gfp);
	if (!ep->digest)
		return NULL;

	ep->asconf_enable = net->sctp.addip_enable;
	ep->auth_enable = net->sctp.auth_enable;
	if (ep->auth_enable) {
		if (sctp_auth_init(ep, gfp))
			goto nomem;
		if (ep->asconf_enable) {
			sctp_auth_ep_add_chunkid(ep, SCTP_CID_ASCONF);
			sctp_auth_ep_add_chunkid(ep, SCTP_CID_ASCONF_ACK);
		}
	}

	/* Initialize the base structure. */
	/* What type of endpoint are we?  */
	ep->base.type = SCTP_EP_TYPE_SOCKET;

	/* Initialize the basic object fields. */
	refcount_set(&ep->base.refcnt, 1);
	ep->base.dead = false;

	/* Create an input queue.  */
	sctp_inq_init(&ep->base.inqueue);

	/* Set its top-half handler */
	sctp_inq_set_th_handler(&ep->base.inqueue, sctp_endpoint_bh_rcv);

	/* Initialize the bind addr area */
	sctp_bind_addr_init(&ep->base.bind_addr, 0);

	/* Create the lists of associations.  */
	INIT_LIST_HEAD(&ep->asocs);

	/* Use SCTP specific send buffer space queues.  */
	ep->sndbuf_policy = net->sctp.sndbuf_policy;

	sk->sk_data_ready = sctp_data_ready;
	sk->sk_write_space = sctp_write_space;
	sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);

	/* Get the receive buffer policy for this endpoint */
	ep->rcvbuf_policy = net->sctp.rcvbuf_policy;

	/* Initialize the secret key used with cookie. */
	get_random_bytes(ep->secret_key, sizeof(ep->secret_key));

	/* SCTP-AUTH extensions*/
	INIT_LIST_HEAD(&ep->endpoint_shared_keys);
	null_key = sctp_auth_shkey_create(0, gfp);
	if (!null_key)
		goto nomem_shkey;

	list_add(&null_key->key_list, &ep->endpoint_shared_keys);

	/* Add the null key to the endpoint shared keys list and
	 * set the hmcas and chunks pointers.
	 */
	ep->prsctp_enable = net->sctp.prsctp_enable;
	ep->reconf_enable = net->sctp.reconf_enable;
	ep->ecn_enable = net->sctp.ecn_enable;

	/* Remember who we are attached to.  */
	ep->base.sk = sk;
	ep->base.net = sock_net(sk);
	sock_hold(ep->base.sk);

	return ep;

nomem_shkey:
	sctp_auth_free(ep);
nomem:
	kfree(ep->digest);
	return NULL;

}

/* Create a sctp_endpoint with all that boring stuff initialized.
 * Returns NULL if there isn't enough memory.
 */
struct sctp_endpoint *sctp_endpoint_new(struct sock *sk, gfp_t gfp)
{
	struct sctp_endpoint *ep;

	/* Build a local endpoint. */
	ep = kzalloc(sizeof(*ep), gfp);
	if (!ep)
		goto fail;

	if (!sctp_endpoint_init(ep, sk, gfp))
		goto fail_init;

	SCTP_DBG_OBJCNT_INC(ep);
	return ep;

fail_init:
	kfree(ep);
fail:
	return NULL;
}

/* Add an association to an endpoint.  */
void sctp_endpoint_add_asoc(struct sctp_endpoint *ep,
			    struct sctp_association *asoc)
{
	struct sock *sk = ep->base.sk;

	/* If this is a temporary association, don't bother
	 * since we'll be removing it shortly and don't
	 * want anyone to find it anyway.
	 */
	if (asoc->temp)
		return;

	/* Now just add it to our list of asocs */
	list_add_tail(&asoc->asocs, &ep->asocs);

	/* Increment the backlog value for a TCP-style listening socket. */
	if (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING))
		sk_acceptq_added(sk);
}

/* Free the endpoint structure.  Delay cleanup until
 * all users have released their reference count on this structure.
 */
void sctp_endpoint_free(struct sctp_endpoint *ep)
{
	ep->base.dead = true;

	inet_sk_set_state(ep->base.sk, SCTP_SS_CLOSED);

	/* Unlink this endpoint, so we can't find it again! */
	sctp_unhash_endpoint(ep);

	sctp_endpoint_put(ep);
}

/* Final destructor for endpoint.  */
static void sctp_endpoint_destroy_rcu(struct rcu_head *head)
{
	struct sctp_endpoint *ep = container_of(head, struct sctp_endpoint, rcu);
	struct sock *sk = ep->base.sk;

	sctp_sk(sk)->ep = NULL;
	sock_put(sk);

	kfree(ep);
	SCTP_DBG_OBJCNT_DEC(ep);
}

static void sctp_endpoint_destroy(struct sctp_endpoint *ep)
{
	struct sock *sk;

	if (unlikely(!ep->base.dead)) {
		WARN(1, "Attempt to destroy undead endpoint %p!\n", ep);
		return;
	}

	/* Free the digest buffer */
	kfree(ep->digest);

	/* SCTP-AUTH: Free up AUTH releated data such as shared keys
	 * chunks and hmacs arrays that were allocated
	 */
	sctp_auth_destroy_keys(&ep->endpoint_shared_keys);
	sctp_auth_free(ep);

	/* Cleanup. */
	sctp_inq_free(&ep->base.inqueue);
	sctp_bind_addr_free(&ep->base.bind_addr);

	memset(ep->secret_key, 0, sizeof(ep->secret_key));

	sk = ep->base.sk;
	/* Remove and free the port */
	if (sctp_sk(sk)->bind_hash)
		sctp_put_port(sk);

	call_rcu(&ep->rcu, sctp_endpoint_destroy_rcu);
}

/* Hold a reference to an endpoint. */
int sctp_endpoint_hold(struct sctp_endpoint *ep)
{
	return refcount_inc_not_zero(&ep->base.refcnt);
}

/* Release a reference to an endpoint and clean up if there are
 * no more references.
 */
void sctp_endpoint_put(struct sctp_endpoint *ep)
{
	if (refcount_dec_and_test(&ep->base.refcnt))
		sctp_endpoint_destroy(ep);
}

/* Is this the endpoint we are looking for?  */
struct sctp_endpoint *sctp_endpoint_is_match(struct sctp_endpoint *ep,
					       struct net *net,
					       const union sctp_addr *laddr,
					       int dif, int sdif)
{
	int bound_dev_if = READ_ONCE(ep->base.sk->sk_bound_dev_if);
	struct sctp_endpoint *retval = NULL;

	if (net_eq(ep->base.net, net) &&
	    sctp_sk_bound_dev_eq(net, bound_dev_if, dif, sdif) &&
	    (htons(ep->base.bind_addr.port) == laddr->v4.sin_port)) {
		if (sctp_bind_addr_match(&ep->base.bind_addr, laddr,
					 sctp_sk(ep->base.sk)))
			retval = ep;
	}

	return retval;
}

/* Find the association that goes with this chunk.
 * We lookup the transport from hashtable at first, then get association
 * through t->assoc.
 */
struct sctp_association *sctp_endpoint_lookup_assoc(
	const struct sctp_endpoint *ep,
	const union sctp_addr *paddr,
	struct sctp_transport **transport)
{
	struct sctp_association *asoc = NULL;
	struct sctp_transport *t;

	*transport = NULL;

	/* If the local port is not set, there can't be any associations
	 * on this endpoint.
	 */
	if (!ep->base.bind_addr.port)
		return NULL;

	rcu_read_lock();
	t = sctp_epaddr_lookup_transport(ep, paddr);
	if (!t)
		goto out;

	*transport = t;
	asoc = t->asoc;
out:
	rcu_read_unlock();
	return asoc;
}

/* Look for any peeled off association from the endpoint that matches the
 * given peer address.
 */
bool sctp_endpoint_is_peeled_off(struct sctp_endpoint *ep,
				 const union sctp_addr *paddr)
{
	int bound_dev_if = READ_ONCE(ep->base.sk->sk_bound_dev_if);
	struct sctp_sockaddr_entry *addr;
	struct net *net = ep->base.net;
	struct sctp_bind_addr *bp;

	bp = &ep->base.bind_addr;
	/* This function is called with the socket lock held,
	 * so the address_list can not change.
	 */
	list_for_each_entry(addr, &bp->address_list, list) {
		if (sctp_has_association(net, &addr->a, paddr,
					 bound_dev_if, bound_dev_if))
			return true;
	}

	return false;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv().
 * This may be called on BH or task time.
 */
static void sctp_endpoint_bh_rcv(struct work_struct *work)
{
	struct sctp_endpoint *ep =
		container_of(work, struct sctp_endpoint,
			     base.inqueue.immediate);
	struct sctp_association *asoc;
	struct sock *sk;
	struct net *net;
	struct sctp_transport *transport;
	struct sctp_chunk *chunk;
	struct sctp_inq *inqueue;
	union sctp_subtype subtype;
	enum sctp_state state;
	int error = 0;
	int first_time = 1;	/* is this the first time through the loop */

	if (ep->base.dead)
		return;

	asoc = NULL;
	inqueue = &ep->base.inqueue;
	sk = ep->base.sk;
	net = sock_net(sk);

	while (NULL != (chunk = sctp_inq_pop(inqueue))) {
		subtype = SCTP_ST_CHUNK(chunk->chunk_hdr->type);

		/* If the first chunk in the packet is AUTH, do special
		 * processing specified in Section 6.3 of SCTP-AUTH spec
		 */
		if (first_time && (subtype.chunk == SCTP_CID_AUTH)) {
			struct sctp_chunkhdr *next_hdr;

			next_hdr = sctp_inq_peek(inqueue);
			if (!next_hdr)
				goto normal;

			/* If the next chunk is COOKIE-ECHO, skip the AUTH
			 * chunk while saving a pointer to it so we can do
			 * Authentication later (during cookie-echo
			 * processing).
			 */
			if (next_hdr->type == SCTP_CID_COOKIE_ECHO) {
				chunk->auth_chunk = skb_clone(chunk->skb,
								GFP_ATOMIC);
				chunk->auth = 1;
				continue;
			}
		}
normal:
		/* We might have grown an association since last we
		 * looked, so try again.
		 *
		 * This happens when we've just processed our
		 * COOKIE-ECHO chunk.
		 */
		if (NULL == chunk->asoc) {
			asoc = sctp_endpoint_lookup_assoc(ep,
							  sctp_source(chunk),
							  &transport);
			chunk->asoc = asoc;
			chunk->transport = transport;
		}

		state = asoc ? asoc->state : SCTP_STATE_CLOSED;
		if (sctp_auth_recv_cid(subtype.chunk, asoc) && !chunk->auth)
			continue;

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (asoc && sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;
		else {
			SCTP_INC_STATS(ep->base.net, SCTP_MIB_INCTRLCHUNKS);
			if (asoc)
				asoc->stats.ictrlchunks++;
		}

		if (chunk->transport)
			chunk->transport->last_time_heard = ktime_get();

		error = sctp_do_sm(net, SCTP_EVENT_T_CHUNK, subtype, state,
				   ep, asoc, chunk, GFP_ATOMIC);

		if (error && chunk)
			chunk->pdiscard = 1;

		/* Check to see if the endpoint is freed in response to
		 * the incoming chunk. If so, get out of the while loop.
		 */
		if (!sctp_sk(sk)->ep)
			break;

		if (first_time)
			first_time = 0;
	}
}
