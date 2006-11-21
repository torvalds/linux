/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This abstraction represents an SCTP endpoint.
 *
 * This file is part of the implementation of the add-IP extension,
 * based on <draft-ietf-tsvwg-addip-sctp-02.txt> June 29, 2001,
 * for the SCTP kernel reference Implementation.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@austin.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Dajiang Zhang <dajiang.zhang@nokia.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/random.h>	/* get_random_bytes() */
#include <linux/crypto.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for internal helpers. */
static void sctp_endpoint_bh_rcv(struct sctp_endpoint *ep);

/*
 * Initialize the base fields of the endpoint structure.
 */
static struct sctp_endpoint *sctp_endpoint_init(struct sctp_endpoint *ep,
						struct sock *sk,
						gfp_t gfp)
{
	memset(ep, 0, sizeof(struct sctp_endpoint));

	ep->digest = kzalloc(SCTP_SIGNATURE_SIZE, gfp);
	if (!ep->digest)
		return NULL;

	/* Initialize the base structure. */
	/* What type of endpoint are we?  */
	ep->base.type = SCTP_EP_TYPE_SOCKET;

	/* Initialize the basic object fields. */
	atomic_set(&ep->base.refcnt, 1);
	ep->base.dead = 0;
	ep->base.malloced = 1;

	/* Create an input queue.  */
	sctp_inq_init(&ep->base.inqueue);

	/* Set its top-half handler */
	sctp_inq_set_th_handler(&ep->base.inqueue,
				(void (*)(void *))sctp_endpoint_bh_rcv, ep);

	/* Initialize the bind addr area */
	sctp_bind_addr_init(&ep->base.bind_addr, 0);
	rwlock_init(&ep->base.addr_lock);

	/* Remember who we are attached to.  */
	ep->base.sk = sk;
	sock_hold(ep->base.sk);

	/* Create the lists of associations.  */
	INIT_LIST_HEAD(&ep->asocs);

	/* Use SCTP specific send buffer space queues.  */
	ep->sndbuf_policy = sctp_sndbuf_policy;
	sk->sk_write_space = sctp_write_space;
	sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);

	/* Get the receive buffer policy for this endpoint */
	ep->rcvbuf_policy = sctp_rcvbuf_policy;

	/* Initialize the secret key used with cookie. */
	get_random_bytes(&ep->secret_key[0], SCTP_SECRET_SIZE);
	ep->last_key = ep->current_key = 0;
	ep->key_changed_at = jiffies;

	return ep;
}

/* Create a sctp_endpoint with all that boring stuff initialized.
 * Returns NULL if there isn't enough memory.
 */
struct sctp_endpoint *sctp_endpoint_new(struct sock *sk, gfp_t gfp)
{
	struct sctp_endpoint *ep;

	/* Build a local endpoint. */
	ep = t_new(struct sctp_endpoint, gfp);
	if (!ep)
		goto fail;
	if (!sctp_endpoint_init(ep, sk, gfp))
		goto fail_init;
	ep->base.malloced = 1;
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
		sk->sk_ack_backlog++;
}

/* Free the endpoint structure.  Delay cleanup until
 * all users have released their reference count on this structure.
 */
void sctp_endpoint_free(struct sctp_endpoint *ep)
{
	ep->base.dead = 1;

	ep->base.sk->sk_state = SCTP_SS_CLOSED;

	/* Unlink this endpoint, so we can't find it again! */
	sctp_unhash_endpoint(ep);

	sctp_endpoint_put(ep);
}

/* Final destructor for endpoint.  */
static void sctp_endpoint_destroy(struct sctp_endpoint *ep)
{
	SCTP_ASSERT(ep->base.dead, "Endpoint is not dead", return);

	/* Free up the HMAC transform. */
	crypto_free_hash(sctp_sk(ep->base.sk)->hmac);

	/* Free the digest buffer */
	kfree(ep->digest);

	/* Cleanup. */
	sctp_inq_free(&ep->base.inqueue);
	sctp_bind_addr_free(&ep->base.bind_addr);

	/* Remove and free the port */
	if (sctp_sk(ep->base.sk)->bind_hash)
		sctp_put_port(ep->base.sk);

	/* Give up our hold on the sock. */
	if (ep->base.sk)
		sock_put(ep->base.sk);

	/* Finally, free up our memory. */
	if (ep->base.malloced) {
		kfree(ep);
		SCTP_DBG_OBJCNT_DEC(ep);
	}
}

/* Hold a reference to an endpoint. */
void sctp_endpoint_hold(struct sctp_endpoint *ep)
{
	atomic_inc(&ep->base.refcnt);
}

/* Release a reference to an endpoint and clean up if there are
 * no more references.
 */
void sctp_endpoint_put(struct sctp_endpoint *ep)
{
	if (atomic_dec_and_test(&ep->base.refcnt))
		sctp_endpoint_destroy(ep);
}

/* Is this the endpoint we are looking for?  */
struct sctp_endpoint *sctp_endpoint_is_match(struct sctp_endpoint *ep,
					       const union sctp_addr *laddr)
{
	struct sctp_endpoint *retval;
	union sctp_addr tmp;
	flip_to_n(&tmp, laddr);

	sctp_read_lock(&ep->base.addr_lock);
	if (ep->base.bind_addr.port == laddr->v4.sin_port) {
		if (sctp_bind_addr_match(&ep->base.bind_addr, &tmp,
					 sctp_sk(ep->base.sk))) {
			retval = ep;
			goto out;
		}
	}

	retval = NULL;

out:
	sctp_read_unlock(&ep->base.addr_lock);
	return retval;
}

/* Find the association that goes with this chunk.
 * We do a linear search of the associations for this endpoint.
 * We return the matching transport address too.
 */
static struct sctp_association *__sctp_endpoint_lookup_assoc(
	const struct sctp_endpoint *ep,
	const union sctp_addr *paddr,
	struct sctp_transport **transport)
{
	int rport;
	struct sctp_association *asoc;
	struct list_head *pos;

	rport = paddr->v4.sin_port;

	list_for_each(pos, &ep->asocs) {
		asoc = list_entry(pos, struct sctp_association, asocs);
		if (rport == asoc->peer.port) {
			sctp_read_lock(&asoc->base.addr_lock);
			*transport = sctp_assoc_lookup_paddr(asoc, paddr);
			sctp_read_unlock(&asoc->base.addr_lock);

			if (*transport)
				return asoc;
		}
	}

	*transport = NULL;
	return NULL;
}

/* Lookup association on an endpoint based on a peer address.  BH-safe.  */
struct sctp_association *sctp_endpoint_lookup_assoc(
	const struct sctp_endpoint *ep,
	const union sctp_addr *paddr,
	struct sctp_transport **transport)
{
	struct sctp_association *asoc;

	sctp_local_bh_disable();
	asoc = __sctp_endpoint_lookup_assoc(ep, paddr, transport);
	sctp_local_bh_enable();

	return asoc;
}

/* Look for any peeled off association from the endpoint that matches the
 * given peer address.
 */
int sctp_endpoint_is_peeled_off(struct sctp_endpoint *ep,
				const union sctp_addr *paddr)
{
	struct list_head *pos;
	struct sctp_sockaddr_entry *addr;
	struct sctp_bind_addr *bp;

	sctp_read_lock(&ep->base.addr_lock);
	bp = &ep->base.bind_addr;
	list_for_each(pos, &bp->address_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		if (sctp_has_association(&addr->a_h, paddr)) {
			sctp_read_unlock(&ep->base.addr_lock);
			return 1;
		}
	}
	sctp_read_unlock(&ep->base.addr_lock);

	return 0;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv().
 * This may be called on BH or task time.
 */
static void sctp_endpoint_bh_rcv(struct sctp_endpoint *ep)
{
	struct sctp_association *asoc;
	struct sock *sk;
	struct sctp_transport *transport;
	struct sctp_chunk *chunk;
	struct sctp_inq *inqueue;
	sctp_subtype_t subtype;
	sctp_state_t state;
	int error = 0;

	if (ep->base.dead)
		return;

	asoc = NULL;
	inqueue = &ep->base.inqueue;
	sk = ep->base.sk;

	while (NULL != (chunk = sctp_inq_pop(inqueue))) {
		subtype = SCTP_ST_CHUNK(chunk->chunk_hdr->type);

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

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (asoc && sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;
		else
			SCTP_INC_STATS(SCTP_MIB_INCTRLCHUNKS);

		if (chunk->transport)
			chunk->transport->last_time_heard = jiffies;

		error = sctp_do_sm(SCTP_EVENT_T_CHUNK, subtype, state,
                                   ep, asoc, chunk, GFP_ATOMIC);

		if (error && chunk)
			chunk->pdiscard = 1;

		/* Check to see if the endpoint is freed in response to
		 * the incoming chunk. If so, get out of the while loop.
		 */
		if (!sctp_sk(sk)->ep)
			break;
	}
}
