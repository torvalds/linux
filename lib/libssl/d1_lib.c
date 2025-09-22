/* $OpenBSD: d1_lib.c,v 1.65 2024/07/23 14:40:53 jsing Exp $ */
/*
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005.
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <stdio.h>

#include <openssl/objects.h>

#include "dtls_local.h"
#include "pqueue.h"
#include "ssl_local.h"

void dtls1_hm_fragment_free(hm_fragment *frag);

static int dtls1_listen(SSL *s, struct sockaddr *client);

int
dtls1_new(SSL *s)
{
	if (!ssl3_new(s))
		goto err;

	if ((s->d1 = calloc(1, sizeof(*s->d1))) == NULL)
		goto err;

	if ((s->d1->unprocessed_rcds.q = pqueue_new()) == NULL)
		goto err;
	if ((s->d1->buffered_messages = pqueue_new()) == NULL)
		goto err;
	if ((s->d1->sent_messages = pqueue_new()) == NULL)
		goto err;
	if ((s->d1->buffered_app_data.q = pqueue_new()) == NULL)
		goto err;

	if (s->server)
		s->d1->cookie_len = sizeof(s->d1->cookie);

	s->method->ssl_clear(s);
	return (1);

 err:
	dtls1_free(s);
	return (0);
}

static void
dtls1_drain_rcontents(pqueue queue)
{
	DTLS1_RCONTENT_DATA_INTERNAL *rdata;
	pitem *item;

	if (queue == NULL)
		return;

	while ((item = pqueue_pop(queue)) != NULL) {
		rdata = (DTLS1_RCONTENT_DATA_INTERNAL *)item->data;
		tls_content_free(rdata->rcontent);
		free(item->data);
		pitem_free(item);
	}
}

static void
dtls1_drain_records(pqueue queue)
{
	pitem *item;
	DTLS1_RECORD_DATA_INTERNAL *rdata;

	if (queue == NULL)
		return;

	while ((item = pqueue_pop(queue)) != NULL) {
		rdata = (DTLS1_RECORD_DATA_INTERNAL *)item->data;
		ssl3_release_buffer(&rdata->rbuf);
		free(item->data);
		pitem_free(item);
	}
}

static void
dtls1_drain_fragments(pqueue queue)
{
	pitem *item;

	if (queue == NULL)
		return;

	while ((item = pqueue_pop(queue)) != NULL) {
		dtls1_hm_fragment_free(item->data);
		pitem_free(item);
	}
}

static void
dtls1_clear_queues(SSL *s)
{
	dtls1_drain_records(s->d1->unprocessed_rcds.q);
	dtls1_drain_fragments(s->d1->buffered_messages);
	dtls1_drain_fragments(s->d1->sent_messages);
	dtls1_drain_rcontents(s->d1->buffered_app_data.q);
}

void
dtls1_free(SSL *s)
{
	if (s == NULL)
		return;

	ssl3_free(s);

	if (s->d1 == NULL)
		return;

	dtls1_clear_queues(s);

	pqueue_free(s->d1->unprocessed_rcds.q);
	pqueue_free(s->d1->buffered_messages);
	pqueue_free(s->d1->sent_messages);
	pqueue_free(s->d1->buffered_app_data.q);

	freezero(s->d1, sizeof(*s->d1));
	s->d1 = NULL;
}

void
dtls1_clear(SSL *s)
{
	pqueue unprocessed_rcds;
	pqueue buffered_messages;
	pqueue sent_messages;
	pqueue buffered_app_data;
	unsigned int mtu;

	if (s->d1) {
		unprocessed_rcds = s->d1->unprocessed_rcds.q;
		buffered_messages = s->d1->buffered_messages;
		sent_messages = s->d1->sent_messages;
		buffered_app_data = s->d1->buffered_app_data.q;
		mtu = s->d1->mtu;

		dtls1_clear_queues(s);

		memset(s->d1, 0, sizeof(*s->d1));

		s->d1->unprocessed_rcds.epoch =
		    tls12_record_layer_read_epoch(s->rl) + 1;

		if (s->server) {
			s->d1->cookie_len = sizeof(s->d1->cookie);
		}

		if (SSL_get_options(s) & SSL_OP_NO_QUERY_MTU) {
			s->d1->mtu = mtu;
		}

		s->d1->unprocessed_rcds.q = unprocessed_rcds;
		s->d1->buffered_messages = buffered_messages;
		s->d1->sent_messages = sent_messages;
		s->d1->buffered_app_data.q = buffered_app_data;
	}

	ssl3_clear(s);

	s->version = DTLS1_VERSION;
}

long
dtls1_ctrl(SSL *s, int cmd, long larg, void *parg)
{
	int ret = 0;

	switch (cmd) {
	case DTLS_CTRL_GET_TIMEOUT:
		if (dtls1_get_timeout(s, (struct timeval*) parg) != NULL) {
			ret = 1;
		}
		break;
	case DTLS_CTRL_HANDLE_TIMEOUT:
		ret = dtls1_handle_timeout(s);
		break;
	case DTLS_CTRL_LISTEN:
		ret = dtls1_listen(s, parg);
		break;

	default:
		ret = ssl3_ctrl(s, cmd, larg, parg);
		break;
	}
	return (ret);
}

void
dtls1_start_timer(SSL *s)
{

	/* If timer is not set, initialize duration with 1 second */
	if (s->d1->next_timeout.tv_sec == 0 && s->d1->next_timeout.tv_usec == 0) {
		s->d1->timeout_duration = 1;
	}

	/* Set timeout to current time */
	gettimeofday(&(s->d1->next_timeout), NULL);

	/* Add duration to current time */
	s->d1->next_timeout.tv_sec += s->d1->timeout_duration;
	BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT, 0,
	    &s->d1->next_timeout);
}

struct timeval*
dtls1_get_timeout(SSL *s, struct timeval* timeleft)
{
	struct timeval timenow;

	/* If no timeout is set, just return NULL */
	if (s->d1->next_timeout.tv_sec == 0 && s->d1->next_timeout.tv_usec == 0) {
		return NULL;
	}

	/* Get current time */
	gettimeofday(&timenow, NULL);

	/* If timer already expired, set remaining time to 0 */
	if (s->d1->next_timeout.tv_sec < timenow.tv_sec ||
	    (s->d1->next_timeout.tv_sec == timenow.tv_sec &&
	     s->d1->next_timeout.tv_usec <= timenow.tv_usec)) {
		memset(timeleft, 0, sizeof(struct timeval));
		return timeleft;
	}

	/* Calculate time left until timer expires */
	memcpy(timeleft, &(s->d1->next_timeout), sizeof(struct timeval));
	timeleft->tv_sec -= timenow.tv_sec;
	timeleft->tv_usec -= timenow.tv_usec;
	if (timeleft->tv_usec < 0) {
		timeleft->tv_sec--;
		timeleft->tv_usec += 1000000;
	}

	/* If remaining time is less than 15 ms, set it to 0
	 * to prevent issues because of small devergences with
	 * socket timeouts.
	 */
	if (timeleft->tv_sec == 0 && timeleft->tv_usec < 15000) {
		memset(timeleft, 0, sizeof(struct timeval));
	}


	return timeleft;
}

int
dtls1_is_timer_expired(SSL *s)
{
	struct timeval timeleft;

	/* Get time left until timeout, return false if no timer running */
	if (dtls1_get_timeout(s, &timeleft) == NULL) {
		return 0;
	}

	/* Return false if timer is not expired yet */
	if (timeleft.tv_sec > 0 || timeleft.tv_usec > 0) {
		return 0;
	}

	/* Timer expired, so return true */
	return 1;
}

void
dtls1_double_timeout(SSL *s)
{
	s->d1->timeout_duration *= 2;
	if (s->d1->timeout_duration > 60)
		s->d1->timeout_duration = 60;
	dtls1_start_timer(s);
}

void
dtls1_stop_timer(SSL *s)
{
	/* Reset everything */
	memset(&(s->d1->timeout), 0, sizeof(struct dtls1_timeout_st));
	memset(&(s->d1->next_timeout), 0, sizeof(struct timeval));
	s->d1->timeout_duration = 1;
	BIO_ctrl(SSL_get_rbio(s), BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT, 0,
	    &(s->d1->next_timeout));
	/* Clear retransmission buffer */
	dtls1_clear_record_buffer(s);
}

int
dtls1_check_timeout_num(SSL *s)
{
	s->d1->timeout.num_alerts++;

	/* Reduce MTU after 2 unsuccessful retransmissions */
	if (s->d1->timeout.num_alerts > 2) {
		s->d1->mtu = BIO_ctrl(SSL_get_wbio(s),
		    BIO_CTRL_DGRAM_GET_FALLBACK_MTU, 0, NULL);

	}

	if (s->d1->timeout.num_alerts > DTLS1_TMO_ALERT_COUNT) {
		/* fail the connection, enough alerts have been sent */
		SSLerror(s, SSL_R_READ_TIMEOUT_EXPIRED);
		return -1;
	}

	return 0;
}

int
dtls1_handle_timeout(SSL *s)
{
	/* if no timer is expired, don't do anything */
	if (!dtls1_is_timer_expired(s)) {
		return 0;
	}

	dtls1_double_timeout(s);

	if (dtls1_check_timeout_num(s) < 0)
		return -1;

	s->d1->timeout.read_timeouts++;
	if (s->d1->timeout.read_timeouts > DTLS1_TMO_READ_COUNT) {
		s->d1->timeout.read_timeouts = 1;
	}

	dtls1_start_timer(s);
	return dtls1_retransmit_buffered_messages(s);
}

int
dtls1_listen(SSL *s, struct sockaddr *client)
{
	int ret;

	/* Ensure there is no state left over from a previous invocation */
	SSL_clear(s);

	SSL_set_options(s, SSL_OP_COOKIE_EXCHANGE);
	s->d1->listen = 1;

	ret = SSL_accept(s);
	if (ret <= 0)
		return ret;

	(void)BIO_dgram_get_peer(SSL_get_rbio(s), client);
	return 1;
}
