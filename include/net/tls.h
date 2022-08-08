/*
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

#ifndef _TLS_OFFLOAD_H
#define _TLS_OFFLOAD_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/crypto.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include <linux/skmsg.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>

#include <net/net_namespace.h>
#include <net/tcp.h>
#include <net/strparser.h>
#include <crypto/aead.h>
#include <uapi/linux/tls.h>


/* Maximum data size carried in a TLS record */
#define TLS_MAX_PAYLOAD_SIZE		((size_t)1 << 14)

#define TLS_HEADER_SIZE			5
#define TLS_NONCE_OFFSET		TLS_HEADER_SIZE

#define TLS_CRYPTO_INFO_READY(info)	((info)->cipher_type)

#define TLS_RECORD_TYPE_DATA		0x17

#define TLS_AAD_SPACE_SIZE		13

#define MAX_IV_SIZE			16
#define TLS_MAX_REC_SEQ_SIZE		8

/* For AES-CCM, the full 16-bytes of IV is made of '4' fields of given sizes.
 *
 * IV[16] = b0[1] || implicit nonce[4] || explicit nonce[8] || length[3]
 *
 * The field 'length' is encoded in field 'b0' as '(length width - 1)'.
 * Hence b0 contains (3 - 1) = 2.
 */
#define TLS_AES_CCM_IV_B0_BYTE		2

#define __TLS_INC_STATS(net, field)				\
	__SNMP_INC_STATS((net)->mib.tls_statistics, field)
#define TLS_INC_STATS(net, field)				\
	SNMP_INC_STATS((net)->mib.tls_statistics, field)
#define __TLS_DEC_STATS(net, field)				\
	__SNMP_DEC_STATS((net)->mib.tls_statistics, field)
#define TLS_DEC_STATS(net, field)				\
	SNMP_DEC_STATS((net)->mib.tls_statistics, field)

enum {
	TLS_BASE,
	TLS_SW,
	TLS_HW,
	TLS_HW_RECORD,
	TLS_NUM_CONFIG,
};

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

	char aad_space[TLS_AAD_SPACE_SIZE];
	u8 iv_data[MAX_IV_SIZE];
	struct aead_request aead_req;
	u8 aead_req_ctx[];
};

struct tls_msg {
	struct strp_msg rxm;
	u8 control;
};

struct tx_work {
	struct delayed_work work;
	struct sock *sk;
};

struct tls_sw_context_tx {
	struct crypto_aead *aead_send;
	struct crypto_wait async_wait;
	struct tx_work tx_work;
	struct tls_rec *open_rec;
	struct list_head tx_list;
	atomic_t encrypt_pending;
	/* protect crypto_wait with encrypt_pending */
	spinlock_t encrypt_compl_lock;
	int async_notify;
	u8 async_capable:1;

#define BIT_TX_SCHEDULED	0
#define BIT_TX_CLOSING		1
	unsigned long tx_bitmask;
};

struct tls_sw_context_rx {
	struct crypto_aead *aead_recv;
	struct crypto_wait async_wait;
	struct strparser strp;
	struct sk_buff_head rx_list;	/* list of decrypted 'data' records */
	void (*saved_data_ready)(struct sock *sk);

	struct sk_buff *recv_pkt;
	u8 control;
	u8 async_capable:1;
	u8 decrypted:1;
	atomic_t decrypt_pending;
	/* protect crypto_wait with decrypt_pending*/
	spinlock_t decrypt_compl_lock;
	bool async_notify;
};

struct tls_record_info {
	struct list_head list;
	u32 end_seq;
	int len;
	int num_frags;
	skb_frag_t frags[MAX_SKB_FRAGS];
};

struct tls_offload_context_tx {
	struct crypto_aead *aead_send;
	spinlock_t lock;	/* protects records list */
	struct list_head records_list;
	struct tls_record_info *open_record;
	struct tls_record_info *retransmit_hint;
	u64 hint_record_sn;
	u64 unacked_record_sn;

	struct scatterlist sg_tx_data[MAX_SKB_FRAGS];
	void (*sk_destruct)(struct sock *sk);
	u8 driver_state[] __aligned(8);
	/* The TLS layer reserves room for driver specific state
	 * Currently the belief is that there is not enough
	 * driver specific state to justify another layer of indirection
	 */
#define TLS_DRIVER_STATE_SIZE_TX	16
};

#define TLS_OFFLOAD_CONTEXT_SIZE_TX                                            \
	(sizeof(struct tls_offload_context_tx) + TLS_DRIVER_STATE_SIZE_TX)

enum tls_context_flags {
	/* tls_device_down was called after the netdev went down, device state
	 * was released, and kTLS works in software, even though rx_conf is
	 * still TLS_HW (needed for transition).
	 */
	TLS_RX_DEV_DEGRADED = 0,
	/* Unlike RX where resync is driven entirely by the core in TX only
	 * the driver knows when things went out of sync, so we need the flag
	 * to be atomic.
	 */
	TLS_TX_SYNC_SCHED = 1,
	/* tls_dev_del was called for the RX side, device state was released,
	 * but tls_ctx->netdev might still be kept, because TX-side driver
	 * resources might not be released yet. Used to prevent the second
	 * tls_dev_del call in tls_device_down if it happens simultaneously.
	 */
	TLS_RX_DEV_CLOSED = 2,
};

struct cipher_context {
	char *iv;
	char *rec_seq;
};

union tls_crypto_context {
	struct tls_crypto_info info;
	union {
		struct tls12_crypto_info_aes_gcm_128 aes_gcm_128;
		struct tls12_crypto_info_aes_gcm_256 aes_gcm_256;
		struct tls12_crypto_info_chacha20_poly1305 chacha20_poly1305;
	};
};

struct tls_prot_info {
	u16 version;
	u16 cipher_type;
	u16 prepend_size;
	u16 tag_size;
	u16 overhead_size;
	u16 iv_size;
	u16 salt_size;
	u16 rec_seq_size;
	u16 aad_size;
	u16 tail_size;
};

struct tls_context {
	/* read-only cache line */
	struct tls_prot_info prot_info;

	u8 tx_conf:3;
	u8 rx_conf:3;

	int (*push_pending_record)(struct sock *sk, int flags);
	void (*sk_write_space)(struct sock *sk);

	void *priv_ctx_tx;
	void *priv_ctx_rx;

	struct net_device *netdev;

	/* rw cache line */
	struct cipher_context tx;
	struct cipher_context rx;

	struct scatterlist *partially_sent_record;
	u16 partially_sent_offset;

	bool in_tcp_sendpages;
	bool pending_open_record_frags;

	struct mutex tx_lock; /* protects partially_sent_* fields and
			       * per-type TX fields
			       */
	unsigned long flags;

	/* cache cold stuff */
	struct proto *sk_proto;
	struct sock *sk;

	void (*sk_destruct)(struct sock *sk);

	union tls_crypto_context crypto_send;
	union tls_crypto_context crypto_recv;

	struct list_head list;
	refcount_t refcount;
	struct rcu_head rcu;
};

enum tls_offload_ctx_dir {
	TLS_OFFLOAD_CTX_DIR_RX,
	TLS_OFFLOAD_CTX_DIR_TX,
};

struct tlsdev_ops {
	int (*tls_dev_add)(struct net_device *netdev, struct sock *sk,
			   enum tls_offload_ctx_dir direction,
			   struct tls_crypto_info *crypto_info,
			   u32 start_offload_tcp_sn);
	void (*tls_dev_del)(struct net_device *netdev,
			    struct tls_context *ctx,
			    enum tls_offload_ctx_dir direction);
	int (*tls_dev_resync)(struct net_device *netdev,
			      struct sock *sk, u32 seq, u8 *rcd_sn,
			      enum tls_offload_ctx_dir direction);
};

enum tls_offload_sync_type {
	TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ = 0,
	TLS_OFFLOAD_SYNC_TYPE_CORE_NEXT_HINT = 1,
	TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ_ASYNC = 2,
};

#define TLS_DEVICE_RESYNC_NH_START_IVAL		2
#define TLS_DEVICE_RESYNC_NH_MAX_IVAL		128

#define TLS_DEVICE_RESYNC_ASYNC_LOGMAX		13
struct tls_offload_resync_async {
	atomic64_t req;
	u16 loglen;
	u16 rcd_delta;
	u32 log[TLS_DEVICE_RESYNC_ASYNC_LOGMAX];
};

struct tls_offload_context_rx {
	/* sw must be the first member of tls_offload_context_rx */
	struct tls_sw_context_rx sw;
	enum tls_offload_sync_type resync_type;
	/* this member is set regardless of resync_type, to avoid branches */
	u8 resync_nh_reset:1;
	/* CORE_NEXT_HINT-only member, but use the hole here */
	u8 resync_nh_do_now:1;
	union {
		/* TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ */
		struct {
			atomic64_t resync_req;
		};
		/* TLS_OFFLOAD_SYNC_TYPE_CORE_NEXT_HINT */
		struct {
			u32 decrypted_failed;
			u32 decrypted_tgt;
		} resync_nh;
		/* TLS_OFFLOAD_SYNC_TYPE_DRIVER_REQ_ASYNC */
		struct {
			struct tls_offload_resync_async *resync_async;
		};
	};
	u8 driver_state[] __aligned(8);
	/* The TLS layer reserves room for driver specific state
	 * Currently the belief is that there is not enough
	 * driver specific state to justify another layer of indirection
	 */
#define TLS_DRIVER_STATE_SIZE_RX	8
};

#define TLS_OFFLOAD_CONTEXT_SIZE_RX					\
	(sizeof(struct tls_offload_context_rx) + TLS_DRIVER_STATE_SIZE_RX)

struct tls_context *tls_ctx_create(struct sock *sk);
void tls_ctx_free(struct sock *sk, struct tls_context *ctx);
void update_sk_prot(struct sock *sk, struct tls_context *ctx);

int wait_on_pending_writer(struct sock *sk, long *timeo);
int tls_sk_query(struct sock *sk, int optname, char __user *optval,
		int __user *optlen);
int tls_sk_attach(struct sock *sk, int optname, char __user *optval,
		  unsigned int optlen);

int tls_set_sw_offload(struct sock *sk, struct tls_context *ctx, int tx);
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
		   int nonblock, int flags, int *addr_len);
bool tls_sw_stream_read(const struct sock *sk);
ssize_t tls_sw_splice_read(struct socket *sock, loff_t *ppos,
			   struct pipe_inode_info *pipe,
			   size_t len, unsigned int flags);

int tls_device_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int tls_device_sendpage(struct sock *sk, struct page *page,
			int offset, size_t size, int flags);
int tls_tx_records(struct sock *sk, int flags);

struct tls_record_info *tls_get_record(struct tls_offload_context_tx *context,
				       u32 seq, u64 *p_record_sn);

static inline bool tls_record_is_start_marker(struct tls_record_info *rec)
{
	return rec->len == 0;
}

static inline u32 tls_record_start_seq(struct tls_record_info *rec)
{
	return rec->end_seq - rec->len;
}

int tls_push_sg(struct sock *sk, struct tls_context *ctx,
		struct scatterlist *sg, u16 first_offset,
		int flags);
int tls_push_partial_record(struct sock *sk, struct tls_context *ctx,
			    int flags);
void tls_free_partial_record(struct sock *sk, struct tls_context *ctx);

static inline struct tls_msg *tls_msg(struct sk_buff *skb)
{
	return (struct tls_msg *)strp_msg(skb);
}

static inline bool tls_is_partially_sent_record(struct tls_context *ctx)
{
	return !!ctx->partially_sent_record;
}

static inline bool tls_is_pending_open_record(struct tls_context *tls_ctx)
{
	return tls_ctx->pending_open_record_frags;
}

static inline bool is_tx_ready(struct tls_sw_context_tx *ctx)
{
	struct tls_rec *rec;

	rec = list_first_entry(&ctx->tx_list, struct tls_rec, list);
	if (!rec)
		return false;

	return READ_ONCE(rec->tx_ready);
}

static inline u16 tls_user_config(struct tls_context *ctx, bool tx)
{
	u16 config = tx ? ctx->tx_conf : ctx->rx_conf;

	switch (config) {
	case TLS_BASE:
		return TLS_CONF_BASE;
	case TLS_SW:
		return TLS_CONF_SW;
	case TLS_HW:
		return TLS_CONF_HW;
	case TLS_HW_RECORD:
		return TLS_CONF_HW_RECORD;
	}
	return 0;
}

struct sk_buff *
tls_validate_xmit_skb(struct sock *sk, struct net_device *dev,
		      struct sk_buff *skb);
struct sk_buff *
tls_validate_xmit_skb_sw(struct sock *sk, struct net_device *dev,
			 struct sk_buff *skb);

static inline bool tls_is_sk_tx_device_offloaded(struct sock *sk)
{
#ifdef CONFIG_SOCK_VALIDATE_XMIT
	return sk_fullsock(sk) &&
	       (smp_load_acquire(&sk->sk_validate_xmit_skb) ==
	       &tls_validate_xmit_skb);
#else
	return false;
#endif
}

static inline void tls_err_abort(struct sock *sk, int err)
{
	sk->sk_err = err;
	sk->sk_error_report(sk);
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

static inline struct tls_context *tls_get_ctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	/* Use RCU on icsk_ulp_data only for sock diag code,
	 * TLS data path doesn't need rcu_dereference().
	 */
	return (__force void *)icsk->icsk_ulp_data;
}

static inline void tls_advance_record_sn(struct sock *sk,
					 struct tls_prot_info *prot,
					 struct cipher_context *ctx)
{
	if (tls_bigint_increment(ctx->rec_seq, prot->rec_seq_size))
		tls_err_abort(sk, EBADMSG);

	if (prot->version != TLS_1_3_VERSION &&
	    prot->cipher_type != TLS_CIPHER_CHACHA20_POLY1305)
		tls_bigint_increment(ctx->iv + prot->salt_size,
				     prot->iv_size);
}

static inline void tls_fill_prepend(struct tls_context *ctx,
			     char *buf,
			     size_t plaintext_len,
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

static inline void tls_make_aad(char *buf,
				size_t size,
				char *record_sequence,
				unsigned char record_type,
				struct tls_prot_info *prot)
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

static inline void xor_iv_with_seq(struct tls_prot_info *prot, char *iv, char *seq)
{
	int i;

	if (prot->version == TLS_1_3_VERSION ||
	    prot->cipher_type == TLS_CIPHER_CHACHA20_POLY1305) {
		for (i = 0; i < 8; i++)
			iv[i + 4] ^= seq[i];
	}
}


static inline struct tls_sw_context_rx *tls_sw_ctx_rx(
		const struct tls_context *tls_ctx)
{
	return (struct tls_sw_context_rx *)tls_ctx->priv_ctx_rx;
}

static inline struct tls_sw_context_tx *tls_sw_ctx_tx(
		const struct tls_context *tls_ctx)
{
	return (struct tls_sw_context_tx *)tls_ctx->priv_ctx_tx;
}

static inline struct tls_offload_context_tx *
tls_offload_ctx_tx(const struct tls_context *tls_ctx)
{
	return (struct tls_offload_context_tx *)tls_ctx->priv_ctx_tx;
}

static inline bool tls_sw_has_ctx_tx(const struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (!ctx)
		return false;
	return !!tls_sw_ctx_tx(ctx);
}

static inline bool tls_sw_has_ctx_rx(const struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (!ctx)
		return false;
	return !!tls_sw_ctx_rx(ctx);
}

void tls_sw_write_space(struct sock *sk, struct tls_context *ctx);
void tls_device_write_space(struct sock *sk, struct tls_context *ctx);

static inline struct tls_offload_context_rx *
tls_offload_ctx_rx(const struct tls_context *tls_ctx)
{
	return (struct tls_offload_context_rx *)tls_ctx->priv_ctx_rx;
}

#if IS_ENABLED(CONFIG_TLS_DEVICE)
static inline void *__tls_driver_ctx(struct tls_context *tls_ctx,
				     enum tls_offload_ctx_dir direction)
{
	if (direction == TLS_OFFLOAD_CTX_DIR_TX)
		return tls_offload_ctx_tx(tls_ctx)->driver_state;
	else
		return tls_offload_ctx_rx(tls_ctx)->driver_state;
}

static inline void *
tls_driver_ctx(const struct sock *sk, enum tls_offload_ctx_dir direction)
{
	return __tls_driver_ctx(tls_get_ctx(sk), direction);
}
#endif

#define RESYNC_REQ BIT(0)
#define RESYNC_REQ_ASYNC BIT(1)
/* The TLS context is valid until sk_destruct is called */
static inline void tls_offload_rx_resync_request(struct sock *sk, __be32 seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_rx *rx_ctx = tls_offload_ctx_rx(tls_ctx);

	atomic64_set(&rx_ctx->resync_req, ((u64)ntohl(seq) << 32) | RESYNC_REQ);
}

/* Log all TLS record header TCP sequences in [seq, seq+len] */
static inline void
tls_offload_rx_resync_async_request_start(struct sock *sk, __be32 seq, u16 len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_rx *rx_ctx = tls_offload_ctx_rx(tls_ctx);

	atomic64_set(&rx_ctx->resync_async->req, ((u64)ntohl(seq) << 32) |
		     ((u64)len << 16) | RESYNC_REQ | RESYNC_REQ_ASYNC);
	rx_ctx->resync_async->loglen = 0;
	rx_ctx->resync_async->rcd_delta = 0;
}

static inline void
tls_offload_rx_resync_async_request_end(struct sock *sk, __be32 seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_rx *rx_ctx = tls_offload_ctx_rx(tls_ctx);

	atomic64_set(&rx_ctx->resync_async->req,
		     ((u64)ntohl(seq) << 32) | RESYNC_REQ);
}

static inline void
tls_offload_rx_resync_set_type(struct sock *sk, enum tls_offload_sync_type type)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);

	tls_offload_ctx_rx(tls_ctx)->resync_type = type;
}

/* Driver's seq tracking has to be disabled until resync succeeded */
static inline bool tls_offload_tx_resync_pending(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	bool ret;

	ret = test_bit(TLS_TX_SYNC_SCHED, &tls_ctx->flags);
	smp_mb__after_atomic();
	return ret;
}

int __net_init tls_proc_init(struct net *net);
void __net_exit tls_proc_fini(struct net *net);

int tls_proccess_cmsg(struct sock *sk, struct msghdr *msg,
		      unsigned char *record_type);
int decrypt_skb(struct sock *sk, struct sk_buff *skb,
		struct scatterlist *sgout);
struct sk_buff *tls_encrypt_skb(struct sk_buff *skb);

int tls_sw_fallback_init(struct sock *sk,
			 struct tls_offload_context_tx *offload_ctx,
			 struct tls_crypto_info *crypto_info);

#ifdef CONFIG_TLS_DEVICE
void tls_device_init(void);
void tls_device_cleanup(void);
void tls_device_sk_destruct(struct sock *sk);
int tls_set_device_offload(struct sock *sk, struct tls_context *ctx);
void tls_device_free_resources_tx(struct sock *sk);
int tls_set_device_offload_rx(struct sock *sk, struct tls_context *ctx);
void tls_device_offload_cleanup_rx(struct sock *sk);
void tls_device_rx_resync_new_rec(struct sock *sk, u32 rcd_len, u32 seq);
void tls_offload_tx_resync_request(struct sock *sk, u32 got_seq, u32 exp_seq);
int tls_device_decrypted(struct sock *sk, struct tls_context *tls_ctx,
			 struct sk_buff *skb, struct strp_msg *rxm);

static inline bool tls_is_sk_rx_device_offloaded(struct sock *sk)
{
	if (!sk_fullsock(sk) ||
	    smp_load_acquire(&sk->sk_destruct) != tls_device_sk_destruct)
		return false;
	return tls_get_ctx(sk)->rx_conf == TLS_HW;
}
#else
static inline void tls_device_init(void) {}
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
tls_device_decrypted(struct sock *sk, struct tls_context *tls_ctx,
		     struct sk_buff *skb, struct strp_msg *rxm)
{
	return 0;
}
#endif
#endif /* _TLS_OFFLOAD_H */
