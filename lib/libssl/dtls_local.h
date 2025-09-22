/* $OpenBSD: dtls_local.h,v 1.2 2022/11/26 17:23:18 tb Exp $ */
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

#ifndef HEADER_DTLS_LOCL_H
#define HEADER_DTLS_LOCL_H

#include <sys/time.h>

#include <openssl/dtls1.h>

#include "ssl_local.h"
#include "tls_content.h"

__BEGIN_HIDDEN_DECLS

typedef struct dtls1_bitmap_st {
	unsigned long map;		/* track 32 packets on 32-bit systems
					   and 64 - on 64-bit systems */
	unsigned char max_seq_num[8];	/* max record number seen so far,
					   64-bit value in big-endian
					   encoding */
} DTLS1_BITMAP;

struct dtls1_retransmit_state {
	SSL_SESSION *session;
	unsigned short epoch;
};

struct hm_header_st {
	unsigned char type;
	unsigned long msg_len;
	unsigned short seq;
	unsigned long frag_off;
	unsigned long frag_len;
	unsigned int is_ccs;
	struct dtls1_retransmit_state saved_retransmit_state;
};

struct dtls1_timeout_st {
	/* Number of read timeouts so far */
	unsigned int read_timeouts;

	/* Number of write timeouts so far */
	unsigned int write_timeouts;

	/* Number of alerts received so far */
	unsigned int num_alerts;
};

struct _pqueue;

typedef struct record_pqueue_st {
	unsigned short epoch;
	struct _pqueue *q;
} record_pqueue;

typedef struct rcontent_pqueue_st {
	unsigned short epoch;
	struct _pqueue *q;
} rcontent_pqueue;

typedef struct hm_fragment_st {
	struct hm_header_st msg_header;
	unsigned char *fragment;
	unsigned char *reassembly;
} hm_fragment;

typedef struct dtls1_record_data_internal_st {
	unsigned char *packet;
	unsigned int packet_length;
	SSL3_BUFFER_INTERNAL rbuf;
	SSL3_RECORD_INTERNAL rrec;
} DTLS1_RECORD_DATA_INTERNAL;

typedef struct dtls1_rcontent_data_internal_st {
	struct tls_content *rcontent;
} DTLS1_RCONTENT_DATA_INTERNAL;

struct dtls1_state_st {
	/* Buffered (sent) handshake records */
	struct _pqueue *sent_messages;

	/* Indicates when the last handshake msg or heartbeat sent will timeout */
	struct timeval next_timeout;

	/* Timeout duration */
	unsigned short timeout_duration;

	unsigned int send_cookie;
	unsigned char cookie[DTLS1_COOKIE_LENGTH];
	unsigned char rcvd_cookie[DTLS1_COOKIE_LENGTH];
	unsigned int cookie_len;

	/* records being received in the current epoch */
	DTLS1_BITMAP bitmap;

	/* renegotiation starts a new set of sequence numbers */
	DTLS1_BITMAP next_bitmap;

	/* handshake message numbers */
	unsigned short handshake_write_seq;
	unsigned short next_handshake_write_seq;

	unsigned short handshake_read_seq;

	/* Received handshake records (unprocessed) */
	record_pqueue unprocessed_rcds;

	/* Buffered handshake messages */
	struct _pqueue *buffered_messages;

	/* Buffered application records.
	 * Only for records between CCS and Finished
	 * to prevent either protocol violation or
	 * unnecessary message loss.
	 */
	rcontent_pqueue buffered_app_data;

	/* Is set when listening for new connections with dtls1_listen() */
	unsigned int listen;

	unsigned int mtu; /* max DTLS packet size */

	struct hm_header_st w_msg_hdr;
	struct hm_header_st r_msg_hdr;

	struct dtls1_timeout_st timeout;

	unsigned int retransmitting;
	unsigned int change_cipher_spec_ok;
};

int dtls1_do_write(SSL *s, int type);
int dtls1_read_bytes(SSL *s, int type, unsigned char *buf, int len, int peek);
void dtls1_set_message_header(SSL *s, unsigned char mt, unsigned long len,
    unsigned long frag_off, unsigned long frag_len);
void dtls1_set_message_header_int(SSL *s, unsigned char mt,
    unsigned long len, unsigned short seq_num, unsigned long frag_off,
    unsigned long frag_len);

int do_dtls1_write(SSL *s, int type, const unsigned char *buf,
    unsigned int len);

int dtls1_write_app_data_bytes(SSL *s, int type, const void *buf, int len);
int dtls1_write_bytes(SSL *s, int type, const void *buf, int len);

int dtls1_read_failed(SSL *s, int code);
int dtls1_buffer_message(SSL *s, int ccs);
int dtls1_retransmit_message(SSL *s, unsigned short seq,
    unsigned long frag_off, int *found);
int dtls1_get_queue_priority(unsigned short seq, int is_ccs);
int dtls1_retransmit_buffered_messages(SSL *s);
void dtls1_clear_record_buffer(SSL *s);
int dtls1_get_message_header(CBS *header, struct hm_header_st *msg_hdr);
void dtls1_reset_read_seq_numbers(SSL *s);
struct timeval* dtls1_get_timeout(SSL *s, struct timeval* timeleft);
int dtls1_check_timeout_num(SSL *s);
int dtls1_handle_timeout(SSL *s);
const SSL_CIPHER *dtls1_get_cipher(unsigned int u);
void dtls1_start_timer(SSL *s);
void dtls1_stop_timer(SSL *s);
int dtls1_is_timer_expired(SSL *s);
void dtls1_double_timeout(SSL *s);
unsigned int dtls1_min_mtu(void);

int dtls1_new(SSL *s);
void dtls1_free(SSL *s);
void dtls1_clear(SSL *s);
long dtls1_ctrl(SSL *s, int cmd, long larg, void *parg);

int dtls1_get_message(SSL *s, int st1, int stn, int mt, long max);
int dtls1_get_record(SSL *s);

__END_HIDDEN_DECLS

#endif /* !HEADER_DTLS_LOCL_H */
