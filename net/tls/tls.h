/*
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 * Copyright (c) 2016-2017, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2017, Dave Watson <davejwatson@fb.com>. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _TLS_INT_H
#define _TLS_INT_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/skmsg.h>
#include <net/tls.h>

#define TLS_PAGE_ORDER	(min_t(unsigned int, PAGE_ALLOC_COSTLY_ORDER,	\
			       TLS_MAX_PAYLOAD_SIZE >> PAGE_SHIFT))

#define __TLS_INC_STATS(net, field)				\
	__SNMP_INC_STATS((net)->mib.tls_statistics, field)
#define TLS_INC_STATS(net, field)				\
	SNMP_INC_STATS((net)->mib.tls_statistics, field)
#define TLS_DEC_STATS(net, field)				\
	SNMP_DEC_STATS((net)->mib.tls_statistics, field)

/* TLS records are maintained in 'struct tls_rec'. It stores the memory pages
 * allocated or mapped for each TLS record. After encryption, the records are
 * stores in a linked list.
 */
struct tls_rec {
	struct list_head list;
	int tx_ready;
	int tx_flags;

	struct sk_msg msg_plaintext;
	struct sk_msg msg_encrypted;

	/* AAD | msg_plaintext.sg.data | sg_tag */
	struct scatterlist sg_aead_in[2];
	/* AAD | msg_encrypted.sg.data (data contains overhead for hdr & iv & tag) */
	struct scatterlist sg_aead_out[2];

	char content_type;
	struct scatterlist sg_content_type;

	struct sock *sk;

	char aad_space[TLS_AAD_SPACE_SIZE];
	u8 iv_data[MAX_IV_SIZE];
	struct aead_request aead_req;
	u8 aead_req_ctx[];
};

int __net_init tls_proc_init(struct net *net);
void __net_exit tls_proc_fini(struct net *net);

struct tls_context *tls_ctx_create(struct sock *sk);
void tls_ctx_free(struct sock *sk, struct tls_context *ctx);
void update_sk_prot(struct sock *sk, struct tls_context *ctx);

int wait_on_pending_writer(struct sock *sk, long *timeo);
int tls_sk_query(struct sock *sk, int optname, char __user *optval,
		 int __user *optlen);
int tls_sk_attach(struct sock *sk, int optname, char __user *optval,
		  unsigned int optlen);
void tls_err_abort(struct sock *sk, int err);

int tls_set_sw_offload(struct sock *sk, struct tls_context *ctx, int tx);
void tls_update_rx_zc_capable(struct tls_context *tls_ctx);
void tls_sw_strparser_arm(struct sock *sk, struct tls_context *ctx);
void tls_sw_strparser_done(struct tls_context *tls_ctx);
int tls_sw_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int tls_sw_sendpage_locked(struct sock *sk, struct page *page,
			   int offset, size_t size, int flags);
int tls_sw_sendpage(struct sock *sk, struct page *page,
		    int offset, size_t size, int flags);
void tls_sw_cancel_work_tx(struct tls_context *tls_ctx);
void tls_sw_release_resources_tx(struct sock *sk);
void tls_sw_free_ctx_tx(struct tls_context *tls_ctx);
void tls_sw_free_resources_rx(struct sock *sk);
void tls_sw_release_resources_rx(struct sock *sk);
void tls_sw_free_ctx_rx(struct tls_context *tls_ctx);
int tls_sw_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		   int flags, int *addr_len);
bool tls_sw_sock_is_readable(struct sock *sk);
ssize_t tls_sw_splice_read(struct socket *sock, loff_t *ppos,
			   struct pipe_inode_info *pipe,
			   size_t len, unsigned int flags);

int tls_device_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int tls_device_sendpage(struct sock *sk, struct page *page,
			int offset, size_t size, int flags);
int tls_tx_records(struct sock *sk, int flags);

void tls_sw_write_space(struct sock *sk, struct tls_context *ctx);
void tls_device_write_space(struct sock *sk, struct tls_context *ctx);

int tls_process_cmsg(struct sock *sk, struct msghdr *msg,
		     unsigned char *record_type);
int decrypt_skb(struct sock *sk, struct scatterlist *sgout);

int tls_sw_fallback_init(struct sock *sk,
			 struct tls_offload_context_tx *offload_ctx,
			 struct tls_crypto_info *crypto_info);

int tls_strp_dev_init(void);
void tls_strp_dev_exit(void);

void tls_strp_done(struct tls_strparser *strp);
void tls_strp_stop(struct tls_strparser *strp);
int tls_strp_init(struct tls_strparser *strp, struct sock *sk);
void tls_strp_data_ready(struct tls_strparser *strp);

void tls_strp_check_rcv(struct tls_strparser *strp);
void tls_strp_msg_done(struct tls_strparser *strp);

int tls_rx_msg_size(struct tls_strparser *strp, struct sk_buff *skb);
void tls_rx_msg_ready(struct tls_strparser *strp);

void tls_strp_msg_load(struct tls_strparser *strp, bool force_refresh);
int tls_strp_msg_cow(struct tls_sw_context_rx *ctx);
struct sk_buff *tls_strp_msg_detach(struct tls_sw_context_rx *ctx);
int tls_strp_msg_hold(struct tls_strparser *strp, struct sk_buff_head *dst);

static inline struct tls_msg *tls_msg(struct sk_buff *skb)
{
	struct sk_skb_cb *scb = (struct sk_skb_cb *)skb->cb;

	return &scb->tls;
}

static inline struct sk_buff *tls_strp_msg(struct tls_sw_context_rx *ctx)
{
	DEBUG_NET_WARN_ON_ONCE(!ctx->strp.msg_ready || !ctx->strp.anchor->len);
	return ctx->strp.anchor;
}

static inline bool tls_strp_msg_ready(struct tls_sw_context_rx *ctx)
{
	return ctx->strp.msg_ready;
}

static inline bool tls_strp_msg_mixed_decrypted(struct tls_sw_context_rx *ctx)
{
	return ctx->strp.mixed_decrypted;
}

#ifdef CONFIG_TLS_DEVICE
int tls_device_init(void);
void tls_device_cleanup(void);
int tls_set_device_offload(struct sock *sk, struct tls_context *ctx);
void tls_device_free_resources_tx(struct sock *sk);
int tls_set_device_offload_rx(struct sock *sk, struct tls_context *ctx);
void tls_device_offload_cleanup_rx(struct sock *sk);
void tls_device_rx_resync_new_rec(struct sock *sk, u32 rcd_len, u32 seq);
int tls_device_decrypted(struct sock *sk, struct tls_context *tls_ctx);
#else
static inline int tls_device_init(void) { return 0; }
static inline void tls_device_cleanup(void) {}

static inline int
tls_set_device_offload(struct sock *sk, struct tls_context *ctx)
{
	return -EOPNOTSUPP;
}

static inline void tls_device_free_resources_tx(struct sock *sk) {}

static inline int
tls_set_device_offload_rx(struct sock *sk, struct tls_context *ctx)
{
	return -EOPNOTSUPP;
}

static inline void tls_device_offload_cleanup_rx(struct sock *sk) {}
static inline void
tls_device_rx_resync_new_rec(struct sock *sk, u32 rcd_len, u32 seq) {}

static inline int
tls_device_decrypted(struct sock *sk, struct tls_context *tls_ctx)
{
	return 0;
}
#endif

int tls_push_sg(struct sock *sk, struct tls_context *ctx,
		struct scatterlist *sg, u16 first_offset,
		int flags);
int tls_push_partial_record(struct sock *sk, struct tls_context *ctx,
			    int flags);
void tls_free_partial_record(struct sock *sk, struct tls_context *ctx);

static inline bool tls_is_partially_sent_record(struct tls_context *ctx)
{
	return !!ctx->partially_sent_record;
}

static inline bool tls_is_pending_open_record(struct tls_context *tls_ctx)
{
	return tls_ctx->pending_open_record_frags;
}

static inline bool tls_bigint_increment(unsigned char *seq, int len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		++seq[i];
		if (seq[i] != 0)
			break;
	}

	return (i == -1);
}

static inline void tls_bigint_subtract(unsigned char *seq, int  n)
{
	u64 rcd_sn;
	__be64 *p;

	BUILD_BUG_ON(TLS_MAX_REC_SEQ_SIZE != 8);

	p = (__be64 *)seq;
	rcd_sn = be64_to_cpu(*p);
	*p = cpu_to_be64(rcd_sn - n);
}

static inline void
tls_advance_record_sn(struct sock *sk, struct tls_prot_info *prot,
		      struct cipher_context *ctx)
{
	if (tls_bigint_increment(ctx->rec_seq, prot->rec_seq_size))
		tls_err_abort(sk, -EBADMSG);

	if (prot->version != TLS_1_3_VERSION &&
	    prot->cipher_type != TLS_CIPHER_CHACHA20_POLY1305)
		tls_bigint_increment(ctx->iv + prot->salt_size,
				     prot->iv_size);
}

static inline void
tls_xor_iv_with_seq(struct tls_prot_info *prot, char *iv, char *seq)
{
	int i;

	if (prot->version == TLS_1_3_VERSION ||
	    prot->cipher_type == TLS_CIPHER_CHACHA20_POLY1305) {
		for (i = 0; i < 8; i++)
			iv[i + 4] ^= seq[i];
	}
}

static inline void
tls_fill_prepend(struct tls_context *ctx, char *buf, size_t plaintext_len,
		 unsigned char record_type)
{
	struct tls_prot_info *prot = &ctx->prot_info;
	size_t pkt_len, iv_size = prot->iv_size;

	pkt_len = plaintext_len + prot->tag_size;
	if (prot->version != TLS_1_3_VERSION &&
	    prot->cipher_type != TLS_CIPHER_CHACHA20_POLY1305) {
		pkt_len += iv_size;

		memcpy(buf + TLS_NONCE_OFFSET,
		       ctx->tx.iv + prot->salt_size, iv_size);
	}

	/* we cover nonce explicit here as well, so buf should be of
	 * size KTLS_DTLS_HEADER_SIZE + KTLS_DTLS_NONCE_EXPLICIT_SIZE
	 */
	buf[0] = prot->version == TLS_1_3_VERSION ?
		   TLS_RECORD_TYPE_DATA : record_type;
	/* Note that VERSION must be TLS_1_2 for both TLS1.2 and TLS1.3 */
	buf[1] = TLS_1_2_VERSION_MINOR;
	buf[2] = TLS_1_2_VERSION_MAJOR;
	/* we can use IV for nonce explicit according to spec */
	buf[3] = pkt_len >> 8;
	buf[4] = pkt_len & 0xFF;
}

static inline
void tls_make_aad(char *buf, size_t size, char *record_sequence,
		  unsigned char record_type, struct tls_prot_info *prot)
{
	if (prot->version != TLS_1_3_VERSION) {
		memcpy(buf, record_sequence, prot->rec_seq_size);
		buf += 8;
	} else {
		size += prot->tag_size;
	}

	buf[0] = prot->version == TLS_1_3_VERSION ?
		  TLS_RECORD_TYPE_DATA : record_type;
	buf[1] = TLS_1_2_VERSION_MAJOR;
	buf[2] = TLS_1_2_VERSION_MINOR;
	buf[3] = size >> 8;
	buf[4] = size & 0xFF;
}

#endif
