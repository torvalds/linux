/* $OpenBSD: ssl_pkt.c,v 1.69 2025/03/12 14:03:55 jsing Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
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

#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "bytestring.h"
#include "dtls_local.h"
#include "ssl_local.h"
#include "tls_content.h"

static int do_ssl3_write(SSL *s, int type, const unsigned char *buf,
    unsigned int len);
static int ssl3_get_record(SSL *s);

/*
 * Force a WANT_READ return for certain error conditions where
 * we don't want to spin internally.
 */
void
ssl_force_want_read(SSL *s)
{
	BIO *bio;

	bio = SSL_get_rbio(s);
	BIO_clear_retry_flags(bio);
	BIO_set_retry_read(bio);

	s->rwstate = SSL_READING;
}

/*
 * If extend == 0, obtain new n-byte packet; if extend == 1, increase
 * packet by another n bytes.
 * The packet will be in the sub-array of s->s3->rbuf.buf specified
 * by s->packet and s->packet_length.
 * (If s->read_ahead is set, 'max' bytes may be stored in rbuf
 * [plus s->packet_length bytes if extend == 1].)
 */
static int
ssl3_read_n(SSL *s, int n, int max, int extend)
{
	SSL3_BUFFER_INTERNAL *rb = &(s->s3->rbuf);
	int i, len, left;
	size_t align;
	unsigned char *pkt;

	if (n <= 0)
		return n;

	if (rb->buf == NULL) {
		if (!ssl3_setup_read_buffer(s))
			return -1;
	}
	if (rb->buf == NULL)
		return -1;

	left = rb->left;
	align = (size_t)rb->buf + SSL3_RT_HEADER_LENGTH;
	align = (-align) & (SSL3_ALIGN_PAYLOAD - 1);

	if (!extend) {
		/* start with empty packet ... */
		if (left == 0)
			rb->offset = align;
		else if (align != 0 && left >= SSL3_RT_HEADER_LENGTH) {
			/* check if next packet length is large
			 * enough to justify payload alignment... */
			pkt = rb->buf + rb->offset;
			if (pkt[0] == SSL3_RT_APPLICATION_DATA &&
			    (pkt[3]<<8|pkt[4]) >= 128) {
				/* Note that even if packet is corrupted
				 * and its length field is insane, we can
				 * only be led to wrong decision about
				 * whether memmove will occur or not.
				 * Header values has no effect on memmove
				 * arguments and therefore no buffer
				 * overrun can be triggered. */
				memmove(rb->buf + align, pkt, left);
				rb->offset = align;
			}
		}
		s->packet = rb->buf + rb->offset;
		s->packet_length = 0;
		/* ... now we can act as if 'extend' was set */
	}

	/* For DTLS/UDP reads should not span multiple packets
	 * because the read operation returns the whole packet
	 * at once (as long as it fits into the buffer). */
	if (SSL_is_dtls(s)) {
		if (left > 0 && n > left)
			n = left;
	}

	/* if there is enough in the buffer from a previous read, take some */
	if (left >= n) {
		s->packet_length += n;
		rb->left = left - n;
		rb->offset += n;
		return (n);
	}

	/* else we need to read more data */

	len = s->packet_length;
	pkt = rb->buf + align;
	/* Move any available bytes to front of buffer:
	 * 'len' bytes already pointed to by 'packet',
	 * 'left' extra ones at the end */
	if (s->packet != pkt)  {
		/* len > 0 */
		memmove(pkt, s->packet, len + left);
		s->packet = pkt;
		rb->offset = len + align;
	}

	if (n > (int)(rb->len - rb->offset)) {
		/* does not happen */
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	if (s->read_ahead || SSL_is_dtls(s)) {
		if (max < n)
			max = n;
		if (max > (int)(rb->len - rb->offset))
			max = rb->len - rb->offset;
	} else {
		/* ignore max parameter */
		max = n;
	}

	while (left < n) {
		/* Now we have len+left bytes at the front of s->s3->rbuf.buf
		 * and need to read in more until we have len+n (up to
		 * len+max if possible) */

		errno = 0;
		if (s->rbio != NULL) {
			s->rwstate = SSL_READING;
			i = BIO_read(s->rbio, pkt + len + left, max - left);
		} else {
			SSLerror(s, SSL_R_READ_BIO_NOT_SET);
			i = -1;
		}

		if (i <= 0) {
			rb->left = left;
			if (s->mode & SSL_MODE_RELEASE_BUFFERS &&
			    !SSL_is_dtls(s)) {
				if (len + left == 0)
					ssl3_release_read_buffer(s);
			}
			return (i);
		}
		left += i;

		/*
		 * reads should *never* span multiple packets for DTLS because
		 * the underlying transport protocol is message oriented as
		 * opposed to byte oriented as in the TLS case.
		 */
		if (SSL_is_dtls(s)) {
			if (n > left)
				n = left; /* makes the while condition false */
		}
	}

	/* done reading, now the book-keeping */
	rb->offset += n;
	rb->left = left - n;
	s->packet_length += n;
	s->rwstate = SSL_NOTHING;

	return (n);
}

int
ssl3_packet_read(SSL *s, int plen)
{
	int n;

	n = ssl3_read_n(s, plen, s->s3->rbuf.len, 0);
	if (n <= 0)
		return n;
	if (s->packet_length < plen)
		return s->packet_length;

	return plen;
}

int
ssl3_packet_extend(SSL *s, int plen)
{
	int rlen, n;

	if (s->packet_length >= plen)
		return plen;
	rlen = plen - s->packet_length;

	n = ssl3_read_n(s, rlen, rlen, 1);
	if (n <= 0)
		return n;
	if (s->packet_length < plen)
		return s->packet_length;

	return plen;
}

/* Call this to get a new input record.
 * It will return <= 0 if more data is needed, normally due to an error
 * or non-blocking IO.
 * When it finishes, one packet has been decoded and can be found in
 * ssl->s3->rrec.type    - is the type of record
 * ssl->s3->rrec.data, 	 - data
 * ssl->s3->rrec.length, - number of bytes
 */
/* used only by ssl3_read_bytes */
static int
ssl3_get_record(SSL *s)
{
	SSL3_BUFFER_INTERNAL *rb = &(s->s3->rbuf);
	SSL3_RECORD_INTERNAL *rr = &(s->s3->rrec);
	uint8_t alert_desc;
	int al, n;
	int ret = -1;

 again:
	/* check if we have the header */
	if ((s->rstate != SSL_ST_READ_BODY) ||
	    (s->packet_length < SSL3_RT_HEADER_LENGTH)) {
		CBS header;
		uint16_t len, ssl_version;
		uint8_t type;

		n = ssl3_packet_read(s, SSL3_RT_HEADER_LENGTH);
		if (n <= 0)
			return (n);

		s->mac_packet = 1;
		s->rstate = SSL_ST_READ_BODY;

		if (s->server && s->first_packet) {
			if ((ret = ssl_server_legacy_first_packet(s)) != 1)
				return (ret);
			ret = -1;
		}

		CBS_init(&header, s->packet, SSL3_RT_HEADER_LENGTH);

		/* Pull apart the header into the SSL3_RECORD_INTERNAL */
		if (!CBS_get_u8(&header, &type) ||
		    !CBS_get_u16(&header, &ssl_version) ||
		    !CBS_get_u16(&header, &len)) {
			SSLerror(s, SSL_R_BAD_PACKET_LENGTH);
			goto err;
		}

		rr->type = type;
		rr->length = len;

		/* Lets check version */
		if (!s->first_packet && ssl_version != s->version) {
			if ((s->version & 0xFF00) == (ssl_version & 0xFF00) &&
			    !tls12_record_layer_write_protected(s->rl)) {
				/* Send back error using their minor version number :-) */
				s->version = ssl_version;
			}
			SSLerror(s, SSL_R_WRONG_VERSION_NUMBER);
			al = SSL_AD_PROTOCOL_VERSION;
			goto fatal_err;
		}

		if ((ssl_version >> 8) != SSL3_VERSION_MAJOR) {
			SSLerror(s, SSL_R_WRONG_VERSION_NUMBER);
			goto err;
		}

		if (rr->length > rb->len - SSL3_RT_HEADER_LENGTH) {
			al = SSL_AD_RECORD_OVERFLOW;
			SSLerror(s, SSL_R_PACKET_LENGTH_TOO_LONG);
			goto fatal_err;
		}
	}

	n = ssl3_packet_extend(s, SSL3_RT_HEADER_LENGTH + rr->length);
	if (n <= 0)
		return (n);
	if (n != SSL3_RT_HEADER_LENGTH + rr->length)
		return (n);

	s->rstate = SSL_ST_READ_HEADER; /* set state for later operations */

	/*
	 * A full record has now been read from the wire, which now needs
	 * to be processed.
	 */
	tls12_record_layer_set_version(s->rl, s->version);

	if (!tls12_record_layer_open_record(s->rl, s->packet, s->packet_length,
	    s->s3->rcontent)) {
		tls12_record_layer_alert(s->rl, &alert_desc);

		if (alert_desc == 0)
			goto err;

		if (alert_desc == SSL_AD_RECORD_OVERFLOW)
			SSLerror(s, SSL_R_ENCRYPTED_LENGTH_TOO_LONG);
		else if (alert_desc == SSL_AD_BAD_RECORD_MAC)
			SSLerror(s, SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC);

		al = alert_desc;
		goto fatal_err;
	}

	/* we have pulled in a full packet so zero things */
	s->packet_length = 0;

	if (tls_content_remaining(s->s3->rcontent) == 0) {
		/*
		 * Zero-length fragments are only permitted for application
		 * data, as per RFC 5246 section 6.2.1.
		 */
		if (rr->type != SSL3_RT_APPLICATION_DATA) {
			SSLerror(s, SSL_R_BAD_LENGTH);
			al = SSL_AD_UNEXPECTED_MESSAGE;
			goto fatal_err;
		}

		tls_content_clear(s->s3->rcontent);

		/*
		 * CBC countermeasures for known IV weaknesses can legitimately
		 * insert a single empty record, so we allow ourselves to read
		 * once past a single empty record without forcing want_read.
		 */
		if (s->empty_record_count++ > SSL_MAX_EMPTY_RECORDS) {
			SSLerror(s, SSL_R_PEER_BEHAVING_BADLY);
			return -1;
		}
		if (s->empty_record_count > 1) {
			ssl_force_want_read(s);
			return -1;
		}
		goto again;
	}

	s->empty_record_count = 0;

	return (1);

 fatal_err:
	ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
	return (ret);
}

/* Call this to write data in records of type 'type'
 * It will return <= 0 if not all data has been sent or non-blocking IO.
 */
int
ssl3_write_bytes(SSL *s, int type, const void *buf_, int len)
{
	const unsigned char *buf = buf_;
	unsigned int tot, n, nw;
	int i;

	if (len < 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	s->rwstate = SSL_NOTHING;
	tot = s->s3->wnum;
	s->s3->wnum = 0;

	if (SSL_in_init(s) && !s->in_handshake) {
		i = s->handshake_func(s);
		if (i < 0)
			return (i);
		if (i == 0) {
			SSLerror(s, SSL_R_SSL_HANDSHAKE_FAILURE);
			return -1;
		}
	}

	if (len < tot)
		len = tot;
	n = (len - tot);
	for (;;) {
		if (n > s->max_send_fragment)
			nw = s->max_send_fragment;
		else
			nw = n;

		i = do_ssl3_write(s, type, &(buf[tot]), nw);
		if (i <= 0) {
			s->s3->wnum = tot;
			return i;
		}

		if ((i == (int)n) || (type == SSL3_RT_APPLICATION_DATA &&
		    (s->mode & SSL_MODE_ENABLE_PARTIAL_WRITE))) {
			/*
			 * Next chunk of data should get another prepended
			 * empty fragment in ciphersuites with known-IV
			 * weakness.
			 */
			s->s3->empty_fragment_done = 0;

			return tot + i;
		}

		n -= i;
		tot += i;
	}
}

static int
do_ssl3_write(SSL *s, int type, const unsigned char *buf, unsigned int len)
{
	SSL3_BUFFER_INTERNAL *wb = &(s->s3->wbuf);
	SSL_SESSION *sess = s->session;
	int need_empty_fragment = 0;
	size_t align, out_len;
	CBB cbb;
	int ret;

	memset(&cbb, 0, sizeof(cbb));

	if (wb->buf == NULL)
		if (!ssl3_setup_write_buffer(s))
			return -1;

	/*
	 * First check if there is a SSL3_BUFFER_INTERNAL still being written
	 * out.  This will happen with non blocking IO.
	 */
	if (wb->left != 0)
		return (ssl3_write_pending(s, type, buf, len));

	/* If we have an alert to send, let's send it. */
	if (s->s3->alert_dispatch) {
		if ((ret = ssl3_dispatch_alert(s)) <= 0)
			return (ret);
		/* If it went, fall through and send more stuff. */

		/* We may have released our buffer, if so get it again. */
		if (wb->buf == NULL)
			if (!ssl3_setup_write_buffer(s))
				return -1;
	}

	if (len == 0)
		return 0;

	/*
	 * Countermeasure against known-IV weakness in CBC ciphersuites
	 * (see http://www.openssl.org/~bodo/tls-cbc.txt). Note that this
	 * is unnecessary for AEAD.
	 */
	if (sess != NULL && tls12_record_layer_write_protected(s->rl)) {
		if (s->s3->need_empty_fragments &&
		    !s->s3->empty_fragment_done &&
		    type == SSL3_RT_APPLICATION_DATA)
			need_empty_fragment = 1;
	}

	/*
	 * An extra fragment would be a couple of cipher blocks, which would
	 * be a multiple of SSL3_ALIGN_PAYLOAD, so if we want to align the real
	 * payload, then we can just simply pretend we have two headers.
	 */
	align = (size_t)wb->buf + SSL3_RT_HEADER_LENGTH;
	if (need_empty_fragment)
		align += SSL3_RT_HEADER_LENGTH;
	align = (-align) & (SSL3_ALIGN_PAYLOAD - 1);
	wb->offset = align;

	if (!CBB_init_fixed(&cbb, wb->buf + align, wb->len - align))
		goto err;

	tls12_record_layer_set_version(s->rl, s->version);

	if (need_empty_fragment) {
		if (!tls12_record_layer_seal_record(s->rl, type,
		    buf, 0, &cbb))
			goto err;
		s->s3->empty_fragment_done = 1;
	}

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

/* if s->s3->wbuf.left != 0, we need to call this */
int
ssl3_write_pending(SSL *s, int type, const unsigned char *buf, unsigned int len)
{
	int i;
	SSL3_BUFFER_INTERNAL *wb = &(s->s3->wbuf);

	/* XXXX */
	if ((s->s3->wpend_tot > (int)len) || ((s->s3->wpend_buf != buf) &&
	    !(s->mode & SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER)) ||
	    (s->s3->wpend_type != type)) {
		SSLerror(s, SSL_R_BAD_WRITE_RETRY);
		return (-1);
	}

	for (;;) {
		errno = 0;
		if (s->wbio != NULL) {
			s->rwstate = SSL_WRITING;
			i = BIO_write(s->wbio, (char *)&(wb->buf[wb->offset]),
			    (unsigned int)wb->left);
		} else {
			SSLerror(s, SSL_R_BIO_NOT_SET);
			i = -1;
		}
		if (i == wb->left) {
			wb->left = 0;
			wb->offset += i;
			if (s->mode & SSL_MODE_RELEASE_BUFFERS &&
			    !SSL_is_dtls(s))
				ssl3_release_write_buffer(s);
			s->rwstate = SSL_NOTHING;
			return (s->s3->wpend_ret);
		} else if (i <= 0) {
			/*
			 * For DTLS, just drop it. That's kind of the
			 * whole point in using a datagram service.
			 */
			if (SSL_is_dtls(s))
				wb->left = 0;
			return (i);
		}
		wb->offset += i;
		wb->left -= i;
	}
}

static ssize_t
ssl3_read_cb(void *buf, size_t n, void *cb_arg)
{
	SSL *s = cb_arg;

	return tls_content_read(s->s3->rcontent, buf, n);
}

#define SSL3_ALERT_LENGTH	2

int
ssl3_read_alert(SSL *s)
{
	uint8_t alert_level, alert_descr;
	ssize_t ret;
	CBS cbs;

	/*
	 * TLSv1.2 permits an alert to be fragmented across multiple records or
	 * for multiple alerts to be be coalesced into a single alert record.
	 * In the case of DTLS, there is no way to reassemble an alert
	 * fragmented across multiple records, hence a full alert must be
	 * available in the record.
	 */
	if (s->s3->alert_fragment == NULL) {
		if ((s->s3->alert_fragment = tls_buffer_new(0)) == NULL)
			return -1;
		tls_buffer_set_capacity_limit(s->s3->alert_fragment,
		    SSL3_ALERT_LENGTH);
	}
	ret = tls_buffer_extend(s->s3->alert_fragment, SSL3_ALERT_LENGTH,
	    ssl3_read_cb, s);
	if (ret <= 0 && ret != TLS_IO_WANT_POLLIN)
		return -1;
	if (ret != SSL3_ALERT_LENGTH) {
		if (SSL_is_dtls(s)) {
			SSLerror(s, SSL_R_BAD_LENGTH);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			return -1;
		}
		return 1;
	}

	if (!tls_buffer_data(s->s3->alert_fragment, &cbs))
		return -1;

	ssl_msg_callback_cbs(s, 0, SSL3_RT_ALERT, &cbs);

	if (!CBS_get_u8(&cbs, &alert_level))
		return -1;
	if (!CBS_get_u8(&cbs, &alert_descr))
		return -1;

	tls_buffer_free(s->s3->alert_fragment);
	s->s3->alert_fragment = NULL;

	ssl_info_callback(s, SSL_CB_READ_ALERT,
	    (alert_level << 8) | alert_descr);

	if (alert_level == SSL3_AL_WARNING) {
		s->s3->warn_alert = alert_descr;
		if (alert_descr == SSL_AD_CLOSE_NOTIFY) {
			s->shutdown |= SSL_RECEIVED_SHUTDOWN;
			return 0;
		}
		/* We requested renegotiation and the peer rejected it. */
		if (alert_descr == SSL_AD_NO_RENEGOTIATION) {
			SSLerror(s, SSL_R_NO_RENEGOTIATION);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_HANDSHAKE_FAILURE);
			return -1;
		}
	} else if (alert_level == SSL3_AL_FATAL) {
		s->rwstate = SSL_NOTHING;
		s->s3->fatal_alert = alert_descr;
		SSLerror(s, SSL_AD_REASON_OFFSET + alert_descr);
		ERR_asprintf_error_data("SSL alert number %d", alert_descr);
		s->shutdown |= SSL_RECEIVED_SHUTDOWN;
		SSL_CTX_remove_session(s->ctx, s->session);
		return 0;
	} else {
		SSLerror(s, SSL_R_UNKNOWN_ALERT_TYPE);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		return -1;
	}

	return 1;
}

int
ssl3_read_change_cipher_spec(SSL *s)
{
	const uint8_t ccs[1] = { SSL3_MT_CCS };

	/*
	 * 'Change Cipher Spec' is just a single byte, so we know exactly what
	 * the record payload has to look like.
	 */
	if (tls_content_remaining(s->s3->rcontent) != sizeof(ccs)) {
		SSLerror(s, SSL_R_BAD_CHANGE_CIPHER_SPEC);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
		return -1;
	}
	if (!tls_content_equal(s->s3->rcontent, ccs, sizeof(ccs))) {
		SSLerror(s, SSL_R_BAD_CHANGE_CIPHER_SPEC);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_ILLEGAL_PARAMETER);
		return -1;
	}

	/* XDTLS: check that epoch is consistent */

	ssl_msg_callback_cbs(s, 0, SSL3_RT_CHANGE_CIPHER_SPEC,
	    tls_content_cbs(s->s3->rcontent));

	/* Check that we have a cipher to change to. */
	if (s->s3->hs.cipher == NULL) {
		SSLerror(s, SSL_R_CCS_RECEIVED_EARLY);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
		return -1;
	}

	/* Check that we should be receiving a Change Cipher Spec. */
	if (SSL_is_dtls(s)) {
		if (!s->d1->change_cipher_spec_ok) {
			/*
			 * We can't process a CCS now, because previous
			 * handshake messages are still missing, so just
			 * drop it.
			 */
			tls_content_clear(s->s3->rcontent);
			return 1;
		}
		s->d1->change_cipher_spec_ok = 0;
	} else {
		if ((s->s3->flags & SSL3_FLAGS_CCS_OK) == 0) {
			SSLerror(s, SSL_R_CCS_RECEIVED_EARLY);
			ssl3_send_alert(s, SSL3_AL_FATAL,
			    SSL_AD_UNEXPECTED_MESSAGE);
			return -1;
		}
		s->s3->flags &= ~SSL3_FLAGS_CCS_OK;
	}

	tls_content_clear(s->s3->rcontent);

	s->s3->change_cipher_spec = 1;
	if (!ssl3_do_change_cipher_spec(s))
		return -1;

	return 1;
}

static int
ssl3_read_handshake_unexpected(SSL *s)
{
	uint32_t hs_msg_length;
	uint8_t hs_msg_type;
	ssize_t ssret;
	CBS cbs;
	int ret;

	/*
	 * We need four bytes of handshake data so we have a handshake message
	 * header - this may be in the same record or fragmented across multiple
	 * records.
	 */
	if (s->s3->handshake_fragment == NULL) {
		if ((s->s3->handshake_fragment = tls_buffer_new(0)) == NULL)
			return -1;
		tls_buffer_set_capacity_limit(s->s3->handshake_fragment,
		    SSL3_HM_HEADER_LENGTH);
	}
	ssret = tls_buffer_extend(s->s3->handshake_fragment, SSL3_HM_HEADER_LENGTH,
	    ssl3_read_cb, s);
	if (ssret <= 0 && ssret != TLS_IO_WANT_POLLIN)
		return -1;
	if (ssret != SSL3_HM_HEADER_LENGTH)
		return 1;

	if (s->in_handshake) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}

	/*
	 * This code currently deals with HelloRequest and ClientHello messages -
	 * anything else is pushed to the handshake_func. Almost all of this
	 * belongs in the client/server handshake code.
	 */

	/* Parse handshake message header. */
	if (!tls_buffer_data(s->s3->handshake_fragment, &cbs))
		return -1;
	if (!CBS_get_u8(&cbs, &hs_msg_type))
		return -1;
	if (!CBS_get_u24(&cbs, &hs_msg_length))
		return -1;

	if (hs_msg_type == SSL3_MT_HELLO_REQUEST) {
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

		if (hs_msg_length != 0) {
			SSLerror(s, SSL_R_BAD_HELLO_REQUEST);
			ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
			return -1;
		}

		if (!tls_buffer_data(s->s3->handshake_fragment, &cbs))
			return -1;
		ssl_msg_callback_cbs(s, 0, SSL3_RT_HANDSHAKE, &cbs);

		tls_buffer_free(s->s3->handshake_fragment);
		s->s3->handshake_fragment = NULL;

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

		if (!ssl3_renegotiate(s))
			return 1;
		if (!ssl3_renegotiate_check(s))
			return 1;

	} else if (hs_msg_type == SSL3_MT_CLIENT_HELLO) {
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
ssl3_read_bytes(SSL *s, int type, unsigned char *buf, int len, int peek)
{
	int rrcount = 0;
	ssize_t ssret;
	int ret;

	if (s->s3->rbuf.buf == NULL) {
		if (!ssl3_setup_read_buffer(s))
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

	if (type == SSL3_RT_HANDSHAKE &&
	    s->s3->handshake_fragment != NULL &&
	    tls_buffer_remaining(s->s3->handshake_fragment) > 0) {
		ssize_t ssn;

		if ((ssn = tls_buffer_read(s->s3->handshake_fragment, buf,
		    len)) <= 0)
			return -1;

		if (tls_buffer_remaining(s->s3->handshake_fragment) == 0) {
			tls_buffer_free(s->s3->handshake_fragment);
			s->s3->handshake_fragment = NULL;
		}

		return (int)ssn;
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

	if (tls_content_remaining(s->s3->rcontent) == 0) {
		if ((ret = ssl3_get_record(s)) <= 0)
			return ret;
	}

	/* We now have a packet which can be read and processed. */

	if (s->s3->change_cipher_spec &&
	    tls_content_type(s->s3->rcontent) != SSL3_RT_HANDSHAKE) {
		SSLerror(s, SSL_R_DATA_BETWEEN_CCS_AND_FINISHED);
		ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
		return -1;
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

		if (tls_content_remaining(s->s3->rcontent) == 0) {
			s->rstate = SSL_ST_READ_HEADER;

			if (s->mode & SSL_MODE_RELEASE_BUFFERS &&
			    s->s3->rbuf.left == 0)
				ssl3_release_read_buffer(s);
		}

		return ssret;
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
		return 0;
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
		if ((ret = ssl3_read_handshake_unexpected(s)) <= 0)
			return ret;
		goto start;
	}

	/*
	 * Unknown record type - TLSv1.2 sends an unexpected message alert while
	 * earlier versions silently ignore the record.
	 */
	if (ssl_effective_tls_version(s) <= TLS1_1_VERSION) {
		tls_content_clear(s->s3->rcontent);
		goto start;
	}
	SSLerror(s, SSL_R_UNEXPECTED_RECORD);
	ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_UNEXPECTED_MESSAGE);
	return -1;
}

int
ssl3_do_change_cipher_spec(SSL *s)
{
	if (s->s3->hs.tls12.key_block == NULL) {
		if (s->session == NULL || s->session->master_key_length == 0) {
			/* might happen if dtls1_read_bytes() calls this */
			SSLerror(s, SSL_R_CCS_RECEIVED_EARLY);
			return (0);
		}

		s->session->cipher_value = s->s3->hs.cipher->value;

		if (!tls1_setup_key_block(s))
			return (0);
	}

	if (!tls1_change_read_cipher_state(s))
		return (0);

	/*
	 * We have to record the message digest at this point so we can get it
	 * before we read the finished message.
	 */
	if (!tls12_derive_peer_finished(s))
		return (0);

	return (1);
}

static int
ssl3_write_alert(SSL *s)
{
	if (SSL_is_dtls(s))
		return do_dtls1_write(s, SSL3_RT_ALERT, s->s3->send_alert,
		    sizeof(s->s3->send_alert));

	return do_ssl3_write(s, SSL3_RT_ALERT, s->s3->send_alert,
	    sizeof(s->s3->send_alert));
}

int
ssl3_send_alert(SSL *s, int level, int desc)
{
	/* If alert is fatal, remove session from cache. */
	if (level == SSL3_AL_FATAL)
		SSL_CTX_remove_session(s->ctx, s->session);

	s->s3->alert_dispatch = 1;
	s->s3->send_alert[0] = level;
	s->s3->send_alert[1] = desc;

	/*
	 * If data is still being written out, the alert will be dispatched at
	 * some point in the future.
	 */
	if (s->s3->wbuf.left != 0)
		return -1;

	return ssl3_dispatch_alert(s);
}

int
ssl3_dispatch_alert(SSL *s)
{
	int ret;

	s->s3->alert_dispatch = 0;
	if ((ret = ssl3_write_alert(s)) <= 0) {
		s->s3->alert_dispatch = 1;
		return ret;
	}

	/*
	 * Alert sent to BIO.  If it is important, flush it now.
	 * If the message does not get sent due to non-blocking IO,
	 * we will not worry too much.
	 */
	if (s->s3->send_alert[0] == SSL3_AL_FATAL)
		(void)BIO_flush(s->wbio);

	ssl_msg_callback(s, 1, SSL3_RT_ALERT, s->s3->send_alert, 2);

	ssl_info_callback(s, SSL_CB_WRITE_ALERT,
	    (s->s3->send_alert[0] << 8) | s->s3->send_alert[1]);

	return ret;
}
