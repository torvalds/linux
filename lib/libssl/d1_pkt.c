/* $OpenBSD: d1_pkt.c,v 1.130 2025/03/12 14:03:55 jsing Exp $ */
/*
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005.
 */
/* ====================================================================
 * Copyright (c) 1998-2005 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <endian.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "bytestring.h"
#include "dtls_local.h"
#include "pqueue.h"
#include "ssl_local.h"
#include "tls_content.h"

/* mod 128 saturating subtract of two 64-bit values in big-endian order */
static int
satsub64be(const unsigned char *v1, const unsigned char *v2)
{
	int ret, sat, brw, i;

	if (sizeof(long) == 8)
		do {
			long l;

			if (BYTE_ORDER == LITTLE_ENDIAN)
				break;
			/* not reached on little-endians */
			/* following test is redundant, because input is
			 * always aligned, but I take no chances... */
			if (((size_t)v1 | (size_t)v2) & 0x7)
				break;

			l  = *((long *)v1);
			l -= *((long *)v2);
			if (l > 128)
				return 128;
			else if (l<-128)
				return -128;
			else
				return (int)l;
		} while (0);

	ret = (int)v1[7] - (int)v2[7];
	sat = 0;
	brw = ret >> 8;	/* brw is either 0 or -1 */
	if (ret & 0x80) {
		for (i = 6; i >= 0; i--) {
			brw += (int)v1[i]-(int)v2[i];
			sat |= ~brw;
			brw >>= 8;
		}
	} else {
		for (i = 6; i >= 0; i--) {
			brw += (int)v1[i]-(int)v2[i];
			sat |= brw;
			brw >>= 8;
		}
	}
	brw <<= 8;	/* brw is either 0 or -256 */

	if (sat & 0xff)
		return brw | 0x80;
	else
		return brw + (ret & 0xFF);
}

static int dtls1_record_replay_check(SSL *s, DTLS1_BITMAP *bitmap,
    const unsigned char *seq);
static void dtls1_record_bitmap_update(SSL *s, DTLS1_BITMAP *bitmap,
    const unsigned char *seq);
static DTLS1_BITMAP *dtls1_get_bitmap(SSL *s, SSL3_RECORD_INTERNAL *rr,
    unsigned int *is_next_epoch);
static int dtls1_buffer_record(SSL *s, record_pqueue *q,
    unsigned char *priority);
static int dtls1_process_record(SSL *s);

/* copy buffered record into SSL structure */
static int
dtls1_copy_record(SSL *s, DTLS1_RECORD_DATA_INTERNAL *rdata)
{
	ssl3_release_buffer(&s->s3->rbuf);

	s->packet = rdata->packet;
	s->packet_length = rdata->packet_length;
	memcpy(&(s->s3->rbuf), &(rdata->rbuf), sizeof(SSL3_BUFFER_INTERNAL));
	memcpy(&(s->s3->rrec), &(rdata->rrec), sizeof(SSL3_RECORD_INTERNAL));

	return (1);
}

static int
dtls1_buffer_record(SSL *s, record_pqueue *queue, unsigned char *priority)
{
	DTLS1_RECORD_DATA_INTERNAL *rdata = NULL;
	pitem *item = NULL;

	/* Limit the size of the queue to prevent DOS attacks */
	if (pqueue_size(queue->q) >= 100)
		return 0;

	if ((rdata = malloc(sizeof(*rdata))) == NULL)
		goto init_err;
	if ((item = pitem_new(priority, rdata)) == NULL)
		goto init_err;

	rdata->packet = s->packet;
	rdata->packet_length = s->packet_length;
	memcpy(&(rdata->rbuf), &(s->s3->rbuf), sizeof(SSL3_BUFFER_INTERNAL));
	memcpy(&(rdata->rrec), &(s->s3->rrec), sizeof(SSL3_RECORD_INTERNAL));

	item->data = rdata;

	s->packet = NULL;
	s->packet_length = 0;
	memset(&(s->s3->rbuf), 0, sizeof(SSL3_BUFFER_INTERNAL));
	memset(&(s->s3->rrec), 0, sizeof(SSL3_RECORD_INTERNAL));

	if (!ssl3_setup_buffers(s))
		goto err;

	/* insert should not fail, since duplicates are dropped */
	if (pqueue_insert(queue->q, item) == NULL)
		goto err;

	return (1);

 err:
	ssl3_release_buffer(&rdata->rbuf);

 init_err:
	SSLerror(s, ERR_R_INTERNAL_ERROR);
	free(rdata);
	pitem_free(item);
	return (-1);
}

static int
dtls1_buffer_rcontent(SSL *s, rcontent_pqueue *queue, unsigned char *priority)
{
	DTLS1_RCONTENT_DATA_INTERNAL *rdata = NULL;
	pitem *item = NULL;

	/* Limit the size of the queue to prevent DOS attacks */
	if (pqueue_size(queue->q) >= 100)
		return 0;

	if ((rdata = malloc(sizeof(*rdata))) == NULL)
		goto init_err;
	if ((item = pitem_new(priority, rdata)) == NULL)
		goto init_err;

	rdata->rcontent = s->s3->rcontent;
	s->s3->rcontent = NULL;

	item->data = rdata;

	/* insert should not fail, since duplicates are dropped */
	if (pqueue_insert(queue->q, item) == NULL)
		goto err;

	if ((s->s3->rcontent = tls_content_new()) == NULL)
		goto err;

	return (1);

 err:
	tls_content_free(rdata->rcontent);

 init_err:
	SSLerror(s, ERR_R_INTERNAL_ERROR);
	free(rdata);
	pitem_free(item);
	return (-1);
}

static int
dtls1_retrieve_buffered_record(SSL *s, record_pqueue *queue)
{
	pitem *item;

	item = pqueue_pop(queue->q);
	if (item) {
		dtls1_copy_record(s, item->data);

		free(item->data);
		pitem_free(item);

		return (1);
	}

	return (0);
}

static int
dtls1_retrieve_buffered_rcontent(SSL *s, rcontent_pqueue *queue)
{
	DTLS1_RCONTENT_DATA_INTERNAL *rdata;
	pitem *item;

	item = pqueue_pop(queue->q);
	if (item) {
		rdata = item->data;

		tls_content_free(s->s3->rcontent);
		s->s3->rcontent = rdata->rcontent;
		s->s3->rrec.epoch = tls_content_epoch(s->s3->rcontent);

		free(item->data);
		pitem_free(item);

		return (1);
	}

	return (0);
}

static int
dtls1_process_buffered_record(SSL *s)
{
	/* Check if epoch is current. */
	if (s->d1->unprocessed_rcds.epoch !=
	    tls12_record_layer_read_epoch(s->rl))
		return (0);

	/* Update epoch once all unprocessed records have been processed. */
	if (pqueue_peek(s->d1->unprocessed_rcds.q) == NULL) {
		s->d1->unprocessed_rcds.epoch =
		    tls12_record_layer_read_epoch(s->rl) + 1;
		return (0);
	}

	/* Process one of the records. */
	if (!dtls1_retrieve_buffered_record(s, &s->d1->unprocessed_rcds))
		return (-1);
	if (!dtls1_process_record(s))
		return (-1);

	return (1);
}

static int
dtls1_process_record(SSL *s)
{
	SSL3_RECORD_INTERNAL *rr = &(s->s3->rrec);
	uint8_t alert_desc;

	tls12_record_layer_set_version(s->rl, s->version);

	if (!tls12_record_layer_open_record(s->rl, s->packet, s->packet_length,
	    s->s3->rcontent)) {
		tls12_record_layer_alert(s->rl, &alert_desc);

		if (alert_desc == 0)
			goto err;

		/*
		 * DTLS should silently discard invalid records, including those
		 * with a bad MAC, as per RFC 6347 section 4.1.2.1.
		 */
		if (alert_desc == SSL_AD_BAD_RECORD_MAC)
			goto done;

		if (alert_desc == SSL_AD_RECORD_OVERFLOW)
			SSLerror(s, SSL_R_ENCRYPTED_LENGTH_TOO_LONG);

		goto fatal_err;
	}

	/* XXX move to record layer. */
	tls_content_set_epoch(s->s3->rcontent, rr->epoch);

 done:
	s->packet_length = 0;

	return (1);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, alert_desc);
 err:
	return (0);
}

/* Call this to get a new input record.
 * It will return <= 0 if more data is needed, normally due to an error
 * or non-blocking IO.
 * When it finishes, one packet has been decoded and can be found in
 * ssl->s3->rrec.type    - is the type of record
 * ssl->s3->rrec.data, 	 - data
 * ssl->s3->rrec.length, - number of bytes
 */
/* used only by dtls1_read_bytes */
int
dtls1_get_record(SSL *s)
{
	SSL3_RECORD_INTERNAL *rr = &(s->s3->rrec);
	unsigned char *p = NULL;
	DTLS1_BITMAP *bitmap;
	unsigned int is_next_epoch;
	int ret, n;

	/* See if there are pending records that can now be processed. */
	if ((ret = dtls1_process_buffered_record(s)) != 0)
		return (ret);

	/* get something from the wire */
	if (0) {
 again:
		/* dump this record on all retries */
		rr->length = 0;
		s->packet_length = 0;
	}

	/* check if we have the header */
	if ((s->rstate != SSL_ST_READ_BODY) ||
	    (s->packet_length < DTLS1_RT_HEADER_LENGTH)) {
		CBS header, seq_no;
		uint16_t epoch, len, ssl_version;
		uint8_t type;

		n = ssl3_packet_read(s, DTLS1_RT_HEADER_LENGTH);
		if (n <= 0)
			return (n);

		/* If this packet contained a partial record, dump it. */
		if (n != DTLS1_RT_HEADER_LENGTH)
			goto again;

		s->rstate = SSL_ST_READ_BODY;

		CBS_init(&header, s->packet, s->packet_length);

		/* Pull apart the header into the DTLS1_RECORD */
		if (!CBS_get_u8(&header, &type))
			goto again;
		if (!CBS_get_u16(&header, &ssl_version))
			goto again;

		/* Sequence number is 64 bits, with top 2 bytes = epoch. */
		if (!CBS_get_bytes(&header, &seq_no, SSL3_SEQUENCE_SIZE))
			goto again;
		if (!CBS_get_u16(&seq_no, &epoch))
			goto again;
		if (!CBS_write_bytes(&seq_no, &rr->seq_num[2],
		    sizeof(rr->seq_num) - 2, NULL))
			goto again;

		if (!CBS_get_u16(&header, &len))
			goto again;

		rr->type = type;
		rr->epoch = epoch;
		rr->length = len;

		/* unexpected version, silently discard */
		if (!s->first_packet && ssl_version != s->version)
			goto again;

		/* wrong version, silently discard record */
		if ((ssl_version & 0xff00) != (s->version & 0xff00))
			goto again;

		/* record too long, silently discard it */
		if (rr->length > SSL3_RT_MAX_ENCRYPTED_LENGTH)
			goto again;

		/* now s->rstate == SSL_ST_READ_BODY */
		p = (unsigned char *)CBS_data(&header);
	}

	/* s->rstate == SSL_ST_READ_BODY, get and decode the data */

	n = ssl3_packet_extend(s, DTLS1_RT_HEADER_LENGTH + rr->length);
	if (n <= 0)
		return (n);

	/* If this packet contained a partial record, dump it. */
	if (n != DTLS1_RT_HEADER_LENGTH + rr->length)
		goto again;

	s->rstate = SSL_ST_READ_HEADER; /* set state for later operations */

	/* match epochs.  NULL means the packet is dropped on the floor */
	bitmap = dtls1_get_bitmap(s, rr, &is_next_epoch);
	if (bitmap == NULL)
		goto again;

	/*
	 * Check whether this is a repeat, or aged record.
	 * Don't check if we're listening and this message is
	 * a ClientHello. They can look as if they're replayed,
	 * since they arrive from different connections and
	 * would be dropped unnecessarily.
	 */
	if (!(s->d1->listen && rr->type == SSL3_RT_HANDSHAKE &&
	    p != NULL && *p == SSL3_MT_CLIENT_HELLO) &&
	    !dtls1_record_replay_check(s, bitmap, rr->seq_num))
		goto again;

	/* just read a 0 length packet */
	if (rr->length == 0)
		goto again;

	/* If this record is from the next epoch (either HM or ALERT),
	 * and a handshake is currently in progress, buffer it since it
	 * cannot be processed at this time. However, do not buffer
	 * anything while listening.
	 */
	if (is_next_epoch) {
		if ((SSL_in_init(s) || s->in_handshake) && !s->d1->listen) {
			if (dtls1_buffer_record(s, &(s->d1->unprocessed_rcds),
			    rr->seq_num) < 0)
				return (-1);
			/* Mark receipt of record. */
			dtls1_record_bitmap_update(s, bitmap, rr->seq_num);
		}
		goto again;
	}

	if (!dtls1_process_record(s))
		goto again;

	/* Mark receipt of record. */
	dtls1_record_bitmap_update(s, bitmap, rr->seq_num);

	return (1);
}

static int
dtls1_read_handshake_unexpected(SSL *s)
{
	struct hm_header_st hs_msg_hdr;
	CBS cbs;
	int ret;

	if (s->in_handshake) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	/* Parse handshake message header. */
	CBS_dup(tls_content_cbs(s->s3->rcontent), &cbs);
	if (!dtls1_get_message_header(&cbs, &hs_msg_hdr))
		return -1; /* XXX - probably should drop/continue. */

	/* This may just be a stale retransmit. */
	if (tls_content_epoch(s->s3->rcontent) !=
	    tls12_record_layer_read_epoch(s->rl)) {
		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;
		return 1;
	}

	if (hs_msg_hdr.type == SSL3_MT_HELLO_REQUEST) {
		/*
		 * Incoming HelloRequest messages should only be received by a
		 * client. A server may send these at any time - a client should
		 * ignore the message if received in the middle of a handshake.
		 * See RFC 5246 sections 7.4 and 7.4.1.1.
		 */
		if (s->server) {
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			     SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}

		/* XXX - should also check frag offset/length. */
		if (hs_msg_hdr.msg_len != 0) {
			SSLerror(s, SSL_R_BAD_HELLO_REQUEST);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			return -1;
		}

		ssl_msg_callback_cbs(s, 0, SSL3_RT_HANDSHAKE,
		    tls_content_cbs(s->s3->rcontent));

		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;

		if ((s->options & SSL_OP_NO_RENEGOTIATION) != 0) {
			ssl3_send_alert(s, SSL3_AL_WARNING,
			    SSL_AD_NO_RENEGOTIATION);
			return 1;
		}

		/*
		 * It should be impossible to hit this, but keep the safety
		 * harness for now...
		 */
		if (s->session == NULL || s->s3->hs.cipher == NULL)
			return 1;

		/*
		 * Ignore this message if we're currently handshaking,
		 * renegotiation is already pending or renegotiation is disabled
		 * via flags.
		 */
		if (!SSL_is_init_finished(s) || s->s3->renegotiate ||
		    (s->s3->flags & SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS) != 0)
			return 1;

		s->d1->handshake_read_seq++;

		/* XXX - why is this set here but not in ssl3? */
		s->new_session = 1;

		if (!ssl3_renegotiate(s))
			return 1;
		if (!ssl3_renegotiate_check(s))
			return 1;

	} else if (hs_msg_hdr.type == SSL3_MT_CLIENT_HELLO) {
		/*
		 * Incoming ClientHello messages should only be received by a
		 * server. A client may send these in response to server
		 * initiated renegotiation (HelloRequest) or in order to
		 * initiate renegotiation by the client. See RFC 5246 section
		 * 7.4.1.2.
		 */
		if (!s->server) {
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			     SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}

		/*
		 * A client should not be sending a ClientHello unless we're not
		 * currently handshaking.
		 */
		if (!SSL_is_init_finished(s)) {
			SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}

		if ((s->options & SSL_OP_NO_CLIENT_RENEGOTIATION) != 0 ||
		    ((s->options & SSL_OP_NO_RENEGOTIATION) != 0 &&
		    (s->options & SSL_OP_ALLOW_CLIENT_RENEGOTIATION) == 0)) {
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_NO_RENEGOTIATION);
			return -1;
		}

		if (s->session == NULL || s->s3->hs.cipher == NULL) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			return -1;
		}

		/* Client requested renegotiation but it is not permitted. */
		if (!s->s3->send_connection_binding ||
		    (s->s3->flags & SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS) != 0) {
			ssl3_send_alert(s, SSL3_AL_WARNING,
			    SSL_AD_NO_RENEGOTIATION);
			return 1;
		}

		s->s3->hs.state = SSL_ST_ACCEPT;
		s->renegotiate = 1;
		s->new_session = 1;

	} else if (hs_msg_hdr.type == SSL3_MT_FINISHED && s->server) {
		/*
		 * If we are server, we may have a repeated FINISHED of the
		 * client here, then retransmit our CCS and FINISHED.
		 */
		if (dtls1_check_timeout_num(s) < 0)
			return -1;

		/* XXX - should this be calling ssl_msg_callback()? */

		dtls1_retransmit_buffered_messages(s);

		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;

		return 1;

	} else {
		SSLerror(s, SSL_R_UNEXPECTED_MESSAGE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
		return -1;
	}

	if ((ret = s->handshake_func(s)) < 0)
		return ret;
	if (ret == 0) {
		SSLerror(s, SSL_R_SSL_HANDSHAKE_FAILURE);
		return -1;
	}

	if (!(s->mode & SSL_MODE_AUTO_RETRY)) {
		if (s->s3->rbuf.left == 0) {
			ssl_force_want_read(s);
			return -1;
		}
	}

	/*
	 * We either finished a handshake or ignored the request, now try again
	 * to obtain the (application) data we were asked for.
	 */
	return 1;
}

/* Return up to 'len' payload bytes received in 'type' records.
 * 'type' is one of the following:
 *
 *   -  SSL3_RT_HANDSHAKE (when ssl3_get_message calls us)
 *   -  SSL3_RT_APPLICATION_DATA (when ssl3_read calls us)
 *   -  0 (during a shutdown, no data has to be returned)
 *
 * If we don't have stored data to work from, read a SSL/TLS record first
 * (possibly multiple records if we still don't have anything to return).
 *
 * This function must handle any surprises the peer may have for us, such as
 * Alert records (e.g. close_notify), ChangeCipherSpec records (not really
 * a surprise, but handled as if it were), or renegotiation requests.
 * Also if record payloads contain fragments too small to process, we store
 * them until there is enough for the respective protocol (the record protocol
 * may use arbitrary fragmentation and even interleaving):
 *     Change cipher spec protocol
 *             just 1 byte needed, no need for keeping anything stored
 *     Alert protocol
 *             2 bytes needed (AlertLevel, AlertDescription)
 *     Handshake protocol
 *             4 bytes needed (HandshakeType, uint24 length) -- we just have
 *             to detect unexpected Client Hello and Hello Request messages
 *             here, anything else is handled by higher layers
 *     Application data protocol
 *             none of our business
 */
int
dtls1_read_bytes(SSL *s, int type, unsigned char *buf, int len, int peek)
{
	int rrcount = 0;
	ssize_t ssret;
	int ret;

	if (s->s3->rbuf.buf == NULL) {
		if (!ssl3_setup_buffers(s))
			return -1;
	}

	if (s->s3->rcontent == NULL) {
		if ((s->s3->rcontent = tls_content_new()) == NULL)
			return -1;
	}

	if (len < 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	if (type != 0 && type != SSL3_RT_APPLICATION_DATA &&
	    type != SSL3_RT_HANDSHAKE) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}
	if (peek && type != SSL3_RT_APPLICATION_DATA) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	if (SSL_in_init(s) && !s->in_handshake) {
		if ((ret = s->handshake_func(s)) < 0)
			return ret;
		if (ret == 0) {
			SSLerror(s, SSL_R_SSL_HANDSHAKE_FAILURE);
			return -1;
		}
	}

 start:
	/*
	 * Do not process more than three consecutive records, otherwise the
	 * peer can cause us to loop indefinitely. Instead, return with an
	 * SSL_ERROR_WANT_READ so the caller can choose when to handle further
	 * processing. In the future, the total number of non-handshake and
	 * non-application data records per connection should probably also be
	 * limited...
	 */
	if (rrcount++ >= 3) {
		ssl_force_want_read(s);
		return -1;
	}

	s->rwstate = SSL_NOTHING;

	/*
	 * We are not handshaking and have no data yet, so process data buffered
	 * during the last handshake in advance, if any.
	 */
	if (s->s3->hs.state == SSL_ST_OK &&
	    tls_content_remaining(s->s3->rcontent) == 0)
		dtls1_retrieve_buffered_rcontent(s, &s->d1->buffered_app_data);

	if (dtls1_handle_timeout(s) > 0)
		goto start;

	if (tls_content_remaining(s->s3->rcontent) == 0) {
		if ((ret = dtls1_get_record(s)) <= 0) {
			/* Anything other than a timeout is an error. */
			if ((ret = dtls1_read_failed(s, ret)) <= 0)
				return ret;
			goto start;
		}
	}

	if (s->d1->listen &&
	    tls_content_type(s->s3->rcontent) != SSL3_RT_HANDSHAKE) {
		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;
		goto start;
	}

	/* We now have a packet which can be read and processed. */

	if (s->s3->change_cipher_spec &&
	    tls_content_type(s->s3->rcontent) != SSL3_RT_HANDSHAKE) {
		/*
		 * We now have application data between CCS and Finished.
		 * Most likely the packets were reordered on their way, so
		 * buffer the application data for later processing rather
		 * than dropping the connection.
		 */
		if (dtls1_buffer_rcontent(s, &s->d1->buffered_app_data,
		    s->s3->rrec.seq_num) < 0) {
			SSLerror(s, ERR_R_INTERNAL_ERROR);
			return (-1);
		}
		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;
		goto start;
	}

	/*
	 * If the other end has shut down, throw anything we read away (even in
	 * 'peek' mode).
	 */
	if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
		s->rwstate = SSL_NOTHING;
		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;
		return 0;
	}

	/* SSL3_RT_APPLICATION_DATA or SSL3_RT_HANDSHAKE */
	if (tls_content_type(s->s3->rcontent) == type) {
		/*
		 * Make sure that we are not getting application data when we
		 * are doing a handshake for the first time.
		 */
		if (SSL_in_init(s) && type == SSL3_RT_APPLICATION_DATA &&
		    !tls12_record_layer_read_protected(s->rl)) {
			SSLerror(s, SSL_R_APP_DATA_IN_HANDSHAKE);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}

		if (len <= 0)
			return len;

		if (peek) {
			ssret = tls_content_peek(s->s3->rcontent, buf, len);
		} else {
			ssret = tls_content_read(s->s3->rcontent, buf, len);
		}
		if (ssret < INT_MIN || ssret > INT_MAX)
			return -1;
		if (ssret < 0)
			return (int)ssret;

		if (tls_content_remaining(s->s3->rcontent) == 0)
			s->rstate = SSL_ST_READ_HEADER;

		return (int)ssret;
	}

	if (tls_content_type(s->s3->rcontent) == SSL3_RT_ALERT) {
		if ((ret = ssl3_read_alert(s)) <= 0)
			return ret;
		goto start;
	}

	if (s->shutdown & SSL_SENT_SHUTDOWN) {
		s->rwstate = SSL_NOTHING;
		tls_content_clear(s->s3->rcontent);
		s->s3->rrec.length = 0;
		return (0);
	}

	if (tls_content_type(s->s3->rcontent) == SSL3_RT_APPLICATION_DATA) {
		/*
		 * At this point, we were expecting handshake data, but have
		 * application data. If the library was running inside
		 * ssl3_read() (i.e. in_read_app_data is set) and it makes
		 * sense to read application data at this point (session
		 * renegotiation not yet started), we will indulge it.
		 */
		if (s->s3->in_read_app_data != 0 &&
		    s->s3->total_renegotiations != 0 &&
		    (((s->s3->hs.state & SSL_ST_CONNECT) &&
		    (s->s3->hs.state >= SSL3_ST_CW_CLNT_HELLO_A) &&
		    (s->s3->hs.state <= SSL3_ST_CR_SRVR_HELLO_A)) || (
		    (s->s3->hs.state & SSL_ST_ACCEPT) &&
		    (s->s3->hs.state <= SSL3_ST_SW_HELLO_REQ_A) &&
		    (s->s3->hs.state >= SSL3_ST_SR_CLNT_HELLO_A)))) {
			s->s3->in_read_app_data = 2;
			return -1;
		} else {
			SSLerror(s, SSL_R_UNEXPECTED_RECORD);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}
	}

	if (tls_content_type(s->s3->rcontent) == SSL3_RT_CHANGE_CIPHER_SPEC) {
		if ((ret = ssl3_read_change_cipher_spec(s)) <= 0)
			return ret;
		goto start;
	}

	if (tls_content_type(s->s3->rcontent) == SSL3_RT_HANDSHAKE) {
		if ((ret = dtls1_read_handshake_unexpected(s)) <= 0)
			return ret;
		goto start;
	}

	/* Unknown record type. */
	SSLerror(s, SSL_R_UNEXPECTED_RECORD);
	ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
	return -1;
}

int
dtls1_write_app_data_bytes(SSL *s, int type, const void *buf_, int len)
{
	int i;

	if (SSL_in_init(s) && !s->in_handshake) {
		i = s->handshake_func(s);
		if (i < 0)
			return (i);
		if (i == 0) {
			SSLerror(s, SSL_R_SSL_HANDSHAKE_FAILURE);
			return -1;
		}
	}

	if (len > SSL3_RT_MAX_PLAIN_LENGTH) {
		SSLerror(s, SSL_R_DTLS_MESSAGE_TOO_BIG);
		return -1;
	}

	i = dtls1_write_bytes(s, type, buf_, len);
	return i;
}

/* Call this to write data in records of type 'type'
 * It will return <= 0 if not all data has been sent or non-blocking IO.
 */
int
dtls1_write_bytes(SSL *s, int type, const void *buf, int len)
{
	int i;

	OPENSSL_assert(len <= SSL3_RT_MAX_PLAIN_LENGTH);
	s->rwstate = SSL_NOTHING;
	i = do_dtls1_write(s, type, buf, len);
	return i;
}

int
do_dtls1_write(SSL *s, int type, const unsigned char *buf, unsigned int len)
{
	SSL3_BUFFER_INTERNAL *wb = &(s->s3->wbuf);
	size_t out_len;
	CBB cbb;
	int ret;

	memset(&cbb, 0, sizeof(cbb));

	/*
	 * First check if there is a SSL3_BUFFER_INTERNAL still being written
	 * out.  This will happen with non blocking IO.
	 */
	if (wb->left != 0) {
		OPENSSL_assert(0); /* XDTLS:  want to see if we ever get here */
		return (ssl3_write_pending(s, type, buf, len));
	}

	/* If we have an alert to send, let's send it */
	if (s->s3->alert_dispatch) {
		if ((ret = ssl3_dispatch_alert(s)) <= 0)
			return (ret);
		/* If it went, fall through and send more stuff. */
	}

	if (len == 0)
		return 0;

	wb->offset = 0;

	if (!CBB_init_fixed(&cbb, wb->buf, wb->len))
		goto err;

	tls12_record_layer_set_version(s->rl, s->version);

	if (!tls12_record_layer_seal_record(s->rl, type, buf, len, &cbb))
		goto err;

	if (!CBB_finish(&cbb, NULL, &out_len))
		goto err;

	wb->left = out_len;

	/*
	 * Memorize arguments so that ssl3_write_pending can detect
	 * bad write retries later.
	 */
	s->s3->wpend_tot = len;
	s->s3->wpend_buf = buf;
	s->s3->wpend_type = type;
	s->s3->wpend_ret = len;

	/* We now just need to write the buffer. */
	return ssl3_write_pending(s, type, buf, len);

 err:
	CBB_cleanup(&cbb);

	return -1;
}

static int
dtls1_record_replay_check(SSL *s, DTLS1_BITMAP *bitmap,
    const unsigned char *seq)
{
	unsigned int shift;
	int cmp;

	cmp = satsub64be(seq, bitmap->max_seq_num);
	if (cmp > 0)
		return 1; /* this record in new */
	shift = -cmp;
	if (shift >= sizeof(bitmap->map)*8)
		return 0; /* stale, outside the window */
	else if (bitmap->map & (1UL << shift))
		return 0; /* record previously received */

	return 1;
}

static void
dtls1_record_bitmap_update(SSL *s, DTLS1_BITMAP *bitmap,
    const unsigned char *seq)
{
	unsigned int shift;
	int cmp;

	cmp = satsub64be(seq, bitmap->max_seq_num);
	if (cmp > 0) {
		shift = cmp;
		if (shift < sizeof(bitmap->map)*8)
			bitmap->map <<= shift, bitmap->map |= 1UL;
		else
			bitmap->map = 1UL;
		memcpy(bitmap->max_seq_num, seq, 8);
	} else {
		shift = -cmp;
		if (shift < sizeof(bitmap->map) * 8)
			bitmap->map |= 1UL << shift;
	}
}

static DTLS1_BITMAP *
dtls1_get_bitmap(SSL *s, SSL3_RECORD_INTERNAL *rr, unsigned int *is_next_epoch)
{
	uint16_t read_epoch, read_epoch_next;

	*is_next_epoch = 0;

	read_epoch = tls12_record_layer_read_epoch(s->rl);
	read_epoch_next = read_epoch + 1;

	/* In current epoch, accept HM, CCS, DATA, & ALERT */
	if (rr->epoch == read_epoch)
		return &s->d1->bitmap;

	/* Only HM and ALERT messages can be from the next epoch */
	if (rr->epoch == read_epoch_next &&
	    (rr->type == SSL3_RT_HANDSHAKE || rr->type == SSL3_RT_ALERT)) {
		*is_next_epoch = 1;
		return &s->d1->next_bitmap;
	}

	return NULL;
}

void
dtls1_reset_read_seq_numbers(SSL *s)
{
	memcpy(&(s->d1->bitmap), &(s->d1->next_bitmap), sizeof(DTLS1_BITMAP));
	memset(&(s->d1->next_bitmap), 0, sizeof(DTLS1_BITMAP));
}
