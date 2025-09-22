/* $OpenBSD: transport.c,v 1.39 2021/01/28 01:18:44 mortimer Exp $	 */
/* $EOM: transport.c,v 1.43 2000/10/10 12:36:39 provos Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2001, 2004 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/queue.h>
#include <netdb.h>
#include <string.h>

#include "conf.h"
#include "exchange.h"
#include "log.h"
#include "message.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "virtual.h"

/* If no retransmit limit is given, use this as a default.  */
#define RETRANSMIT_DEFAULT 10

LIST_HEAD(transport_method_list, transport_vtbl) transport_method_list;

struct transport_list transport_list;

/* Call the reinit function of the various transports.  */
void
transport_reinit(void)
{
	struct transport_vtbl *method;

	for (method = LIST_FIRST(&transport_method_list); method;
	    method = LIST_NEXT(method, link))
		if (method->reinit)
			method->reinit();
}

/* Initialize the transport maintenance module.  */
void
transport_init(void)
{
	LIST_INIT(&transport_list);
	LIST_INIT(&transport_method_list);
}

/* Register another transport T.  */
void
transport_setup(struct transport *t, int toplevel)
{
	if (toplevel) {
		/* Only the toplevel (virtual) transport has sendqueues.  */
		LOG_DBG((LOG_TRANSPORT, 70,
		    "transport_setup: virtual transport %p", t));
		TAILQ_INIT(&t->sendq);
		TAILQ_INIT(&t->prio_sendq);
		t->refcnt = 0;
	} else {
		/* udp and udp_encap trp goes into the transport list.  */
		LOG_DBG((LOG_TRANSPORT, 70,
		    "transport_setup: added %p to transport list", t));
		LIST_INSERT_HEAD(&transport_list, t, link);
		t->refcnt = 1;
	}
	t->flags = 0;
}

/* Add a referer to transport T.  */
void
transport_reference(struct transport *t)
{
	t->refcnt++;
	LOG_DBG((LOG_TRANSPORT, 95,
	    "transport_reference: transport %p now has %d references", t,
	    t->refcnt));
}

/*
 * Remove a referer from transport T, removing all of T when no referers left.
 */
void
transport_release(struct transport *t)
{
	LOG_DBG((LOG_TRANSPORT, 95,
	    "transport_release: transport %p had %d references", t,
	    t->refcnt));
	if (--t->refcnt)
		return;

	LOG_DBG((LOG_TRANSPORT, 70, "transport_release: freeing %p", t));
	t->vtbl->remove(t);
}

void
transport_report(void)
{
	struct virtual_transport *v;
	struct transport *t;
	struct message *msg;

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link)) {
		LOG_DBG((LOG_REPORT, 0,
		    "transport_report: transport %p flags %x refcnt %d", t,
		    t->flags, t->refcnt));

		/* XXX Report sth on the virtual transport?  */
		t->vtbl->report(t);

		/*
		 * This is the reason message_dump_raw lives outside
		 * message.c.
		 */
		v = (struct virtual_transport *)t->virtual;
		if ((v->encap_is_active && v->encap == t) ||
		    (!v->encap_is_active && v->main == t)) {
			for (msg = TAILQ_FIRST(&t->virtual->prio_sendq); msg;
			    msg = TAILQ_NEXT(msg, link))
				message_dump_raw("udp_report(prio)", msg,
				    LOG_REPORT);

			for (msg = TAILQ_FIRST(&t->virtual->sendq); msg;
			    msg = TAILQ_NEXT(msg, link))
				message_dump_raw("udp_report", msg,
				    LOG_REPORT);
		}
	}
}

int
transport_prio_sendqs_empty(void)
{
	struct transport *t;

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link))
		if (TAILQ_FIRST(&t->virtual->prio_sendq))
			return 0;
	return 1;
}

/* Register another transport method T.  */
void
transport_method_add(struct transport_vtbl *t)
{
	LIST_INSERT_HEAD(&transport_method_list, t, link);
}

/*
 * Build up a file descriptor set FDS with all transport descriptors we want
 * to read from.  Return the number of file descriptors select(2) needs to
 * check in order to cover the ones we setup in here.
 */
int
transport_fd_set(fd_set * fds)
{
	struct transport *t;
	int	n;
	int	max = -1;

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link))
		if (t->virtual->flags & TRANSPORT_LISTEN) {
			n = t->vtbl->fd_set(t, fds, 1);
			if (n > max)
				max = n;

			LOG_DBG((LOG_TRANSPORT, 95, "transport_fd_set: "
			    "transport %p (virtual %p) fd %d", t,
			    t->virtual, n));
		}
	return max + 1;
}

/*
 * Build up a file descriptor set FDS with all the descriptors belonging to
 * transport where messages are queued for transmittal.  Return the number
 * of file descriptors select(2) needs to check in order to cover the ones
 * we setup in here.
 */
int
transport_pending_wfd_set(fd_set * fds)
{
	struct transport *t;
	int	n;
	int	max = -1;

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link)) {
		if (TAILQ_FIRST(&t->virtual->sendq) ||
		    TAILQ_FIRST(&t->virtual->prio_sendq)) {
			n = t->vtbl->fd_set(t, fds, 1);
			LOG_DBG((LOG_TRANSPORT, 95,
			    "transport_pending_wfd_set: "
			    "transport %p (virtual %p) fd %d pending", t,
			    t->virtual, n));
			if (n > max)
				max = n;
		}
	}
	return max + 1;
}

/*
 * For each transport with a file descriptor in FDS, try to get an
 * incoming message and start processing it.
 */
void
transport_handle_messages(fd_set *fds)
{
	struct transport *t;

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link)) {
		if ((t->flags & TRANSPORT_LISTEN) &&
		    (*t->vtbl->fd_isset)(t, fds)) {
			(*t->virtual->vtbl->handle_message)(t);
			(*t->vtbl->fd_set)(t, fds, 0);
		}
	}
}

/*
 * Send the first queued message on the transports found whose file
 * descriptor is in FDS and has messages queued.  Remove the fd bit from
 * FDS as soon as one message has been sent on it so other transports
 * sharing the socket won't get service without an intervening select
 * call.  Perhaps a fairness strategy should be implemented between
 * such transports.  Now early transports in the list will potentially
 * be favoured to later ones sharing the file descriptor.
 */
void
transport_send_messages(fd_set * fds)
{
	struct transport *t, *next;
	struct message *msg;
	struct exchange *exchange;
	struct sockaddr *dst;
	struct timespec expiration;
	int             expiry, ok_to_drop_message;
	char peer[NI_MAXHOST], peersv[NI_MAXSERV];

	/*
	 * Reference all transports first so noone will disappear while in
	 * use.
	 */
	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link))
		transport_reference(t->virtual);

	for (t = LIST_FIRST(&transport_list); t; t = LIST_NEXT(t, link)) {
		if ((TAILQ_FIRST(&t->virtual->sendq) ||
		    TAILQ_FIRST(&t->virtual->prio_sendq)) &&
		    t->vtbl->fd_isset(t, fds)) {
			/* Remove fd bit.  */
			t->vtbl->fd_set(t, fds, 0);

			/* Prefer a message from the prioritized sendq.  */
			if (TAILQ_FIRST(&t->virtual->prio_sendq)) {
				msg = TAILQ_FIRST(&t->virtual->prio_sendq);
				TAILQ_REMOVE(&t->virtual->prio_sendq, msg,
				    link);
			} else {
				msg = TAILQ_FIRST(&t->virtual->sendq);
				TAILQ_REMOVE(&t->virtual->sendq, msg, link);
			}

			msg->flags &= ~MSG_IN_TRANSIT;
			exchange = msg->exchange;
			exchange->in_transit = 0;

			/*
			 * We disregard the potential error message here,
			 * hoping that the retransmit will go better.
			 * XXX Consider a retry/fatal error discriminator.
			 */
			t->virtual->vtbl->send_message(msg, 0);
			msg->xmits++;

			/*
			 * This piece of code has been proven to be quite
			 * delicate. Think twice for before altering.
			 * Here's an outline:
			 *
			 * If this message is not the one which finishes an
			 * exchange, check if we have reached the number of
			 * retransmit before queuing it up for another.
			 *
			 * If it is a finishing message we still may have to
			 * keep it around for an on-demand retransmit when
			 * seeing a duplicate of our peer's previous message.
			 */
			if ((msg->flags & MSG_LAST) == 0) {
				if (msg->flags & MSG_DONTRETRANSMIT)
					exchange->last_sent = 0;
				else if (msg->xmits > conf_get_num("General",
				    "retransmits", RETRANSMIT_DEFAULT)) {
					t->virtual->vtbl->get_dst(t->virtual, &dst);
					if (getnameinfo(dst, SA_LEN(dst), peer,
					    sizeof peer, peersv, sizeof peersv,
					    NI_NUMERICHOST | NI_NUMERICSERV)) {
						strlcpy(peer, "<unknown>", sizeof peer);
						strlcpy(peersv, "<?>", sizeof peersv);
					}
					log_print("transport_send_messages: "
					    "giving up on exchange %s, no "
					    "response from peer %s:%s",
					    exchange->name ? exchange->name :
					    "<unnamed>", peer, peersv);

					exchange->last_sent = 0;
#ifdef notyet
					exchange_free(exchange);
					exchange = 0;
#endif
				} else {
					clock_gettime(CLOCK_MONOTONIC,
					    &expiration);

					/*
					 * XXX Calculate from round trip
					 * timings and a backoff func.
					 */
					expiry = msg->xmits * 2 + 5;
					expiration.tv_sec += expiry;
					LOG_DBG((LOG_TRANSPORT, 30,
					    "transport_send_messages: "
					    "message %p scheduled for "
					    "retransmission %d in %d secs",
					    msg, msg->xmits, expiry));
					if (msg->retrans)
						timer_remove_event(msg->retrans);
					msg->retrans
					    = timer_add_event("message_send_expire",
						(void (*) (void *)) message_send_expire,
						msg, &expiration);
					/*
					 * If we cannot retransmit, we
					 * cannot...
					 */
					exchange->last_sent =
					    msg->retrans ? msg : 0;
				}
			} else
				exchange->last_sent =
				    exchange->last_received ? msg : 0;

			/*
			 * If this message is not referred to for later
			 * retransmission it will be ok for us to drop it
			 * after the post-send function. But as the post-send
			 * function may remove the exchange, we need to
			 * remember this fact here.
			 */
			ok_to_drop_message = exchange->last_sent == 0;

			/*
			 * If this is not a retransmit call post-send
			 * functions that allows parallel work to be done
			 * while the network and peer does their share of
			 * the job.  Note that a post-send function may take
			 * away the exchange we belong to, but only if no
			 * retransmits are possible.
			 */
			if (msg->xmits == 1)
				message_post_send(msg);

			if (ok_to_drop_message)
				message_free(msg);
		}
	}

	for (t = LIST_FIRST(&transport_list); t; t = next) {
		next = LIST_NEXT(t, link);
		transport_release(t->virtual);
	}
}

/*
 * Textual search after the transport method denoted by NAME, then create
 * a transport connected to the peer with address ADDR, given in a transport-
 * specific string format.
 */
struct transport *
transport_create(char *name, char *addr)
{
	struct transport_vtbl *method;

	for (method = LIST_FIRST(&transport_method_list); method;
	    method = LIST_NEXT(method, link))
		if (strcmp(method->name, name) == 0)
			return (*method->create) (addr);
	return 0;
}
