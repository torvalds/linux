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
#include <net/tcp.h>
#include <net/strparser.h>

#include <uapi/linux/tls.h>


/* Maximum data size carried in a TLS record */
#define TLS_MAX_PAYLOAD_SIZE		((size_t)1 << 14)

#define TLS_HEADER_SIZE			5
#define TLS_NONCE_OFFSET		TLS_HEADER_SIZE

#define TLS_CRYPTO_INFO_READY(info)	((info)->cipher_type)

#define TLS_RECORD_TYPE_DATA		0x17

#define TLS_AAD_SPACE_SIZE		13
#define TLS_DEVICE_NAME_MAX		32

/*
 * This structure defines the routines for Inline TLS driver.
 * The following routines are optional and filled with a
 * null pointer if not defined.
 *
 * @name: Its the name of registered Inline tls device
 * @dev_list: Inline tls device list
 * int (*feature)(struct tls_device *device);
 *     Called to return Inline TLS driver capability
 *
 * int (*hash)(struct tls_device *device, struct sock *sk);
 *     This function sets Inline driver for listen and program
 *     device specific functioanlity as required
 *
 * void (*unhash)(struct tls_device *device, struct sock *sk);
 *     This function cleans listen state set by Inline TLS driver
 */
struct tls_device {
	char name[TLS_DEVICE_NAME_MAX];
	struct list_head dev_list;
	int  (*feature)(struct tls_device *device);
	int  (*hash)(struct tls_device *device, struct sock *sk);
	void (*unhash)(struct tls_device *device, struct sock *sk);
};

enum {
	TLS_BASE,
	TLS_SW,
#ifdef CONFIG_TLS_DEVICE
	TLS_HW,
#endif
	TLS_HW_RECORD,
	TLS_NUM_CONFIG,
};

struct tls_sw_context_tx {
	struct crypto_aead *aead_send;
	struct crypto_wait async_wait;

	char aad_space[TLS_AAD_SPACE_SIZE];

	unsigned int sg_plaintext_size;
	int sg_plaintext_num_elem;
	struct scatterlist sg_plaintext_data[MAX_SKB_FRAGS];

	unsigned int sg_encrypted_size;
	int sg_encrypted_num_elem;
	struct scatterlist sg_encrypted_data[MAX_SKB_FRAGS];

	/* AAD | sg_plaintext_data | sg_tag */
	struct scatterlist sg_aead_in[2];
	/* AAD | sg_encrypted_data (data contain overhead for hdr&iv&tag) */
	struct scatterlist sg_aead_out[2];
};

struct tls_sw_context_rx {
	struct crypto_aead *aead_recv;
	struct crypto_wait async_wait;

	struct strparser strp;
	void (*saved_data_ready)(struct sock *sk);
	unsigned int (*sk_poll)(struct file *file, struct socket *sock,
				struct poll_table_struct *wait);
	struct sk_buff *recv_pkt;
	u8 control;
	bool decrypted;
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
	u8 driver_state[];
	/* The TLS layer reserves room for driver specific state
	 * Currently the belief is that there is not enough
	 * driver specific state to justify another layer of indirection
	 */
#define TLS_DRIVER_STATE_SIZE (max_t(size_t, 8, sizeof(void *)))
};

#define TLS_OFFLOAD_CONTEXT_SIZE_TX                                            \
	(ALIGN(sizeof(struct tls_offload_context_tx), sizeof(void *)) +        \
	 TLS_DRIVER_STATE_SIZE)

enum {
	TLS_PENDING_CLOSED_RECORD
};

struct cipher_context {
	u16 prepend_size;
	u16 tag_size;
	u16 overhead_size;
	u16 iv_size;
	char *iv;
	u16 rec_seq_size;
	char *rec_seq;
};

struct tls_context {
	union {
		struct tls_crypto_info crypto_send;
		struct tls12_crypto_info_aes_gcm_128 crypto_send_aes_gcm_128;
	};
	union {
		struct tls_crypto_info crypto_recv;
		struct tls12_crypto_info_aes_gcm_128 crypto_recv_aes_gcm_128;
	};

	struct list_head list;
	struct net_device *netdev;
	refcount_t refcount;

	void *priv_ctx_tx;
	void *priv_ctx_rx;

	u8 tx_conf:3;
	u8 rx_conf:3;

	struct cipher_context tx;
	struct cipher_context rx;

	struct scatterlist *partially_sent_record;
	u16 partially_sent_offset;
	unsigned long flags;
	bool in_tcp_sendpages;

	u16 pending_open_record_frags;
	int (*push_pending_record)(struct sock *sk, int flags);

	void (*sk_write_space)(struct sock *sk);
	void (*sk_destruct)(struct sock *sk);
	void (*sk_proto_close)(struct sock *sk, long timeout);

	int  (*setsockopt)(struct sock *sk, int level,
			   int optname, char __user *optval,
			   unsigned int optlen);
	int  (*getsockopt)(struct sock *sk, int level,
			   int optname, char __user *optval,
			   int __user *optlen);
	int  (*hash)(struct sock *sk);
	void (*unhash)(struct sock *sk);
};

struct tls_offload_context_rx {
	/* sw must be the first member of tls_offload_context_rx */
	struct tls_sw_context_rx sw;
	atomic64_t resync_req;
	u8 driver_state[];
	/* The TLS layer reserves room for driver specific state
	 * Currently the belief is that there is not enough
	 * driver specific state to justify another layer of indirection
	 */
};

#define TLS_OFFLOAD_CONTEXT_SIZE_RX					\
	(ALIGN(sizeof(struct tls_offload_context_rx), sizeof(void *)) + \
	 TLS_DRIVER_STATE_SIZE)

int wait_on_pending_writer(struct sock *sk, long *timeo);
int tls_sk_query(struct sock *sk, int optname, char __user *optval,
		int __user *optlen);
int tls_sk_attach(struct sock *sk, int optname, char __user *optval,
		  unsigned int optlen);

int tls_set_sw_offload(struct sock *sk, struct tls_context *ctx, int tx);
int tls_sw_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int tls_sw_sendpage(struct sock *sk, struct page *page,
		    int offset, size_t size, int flags);
void tls_sw_close(struct sock *sk, long timeout);
void tls_sw_free_resources_tx(struct sock *sk);
void tls_sw_free_resources_rx(struct sock *sk);
void tls_sw_release_resources_rx(struct sock *sk);
int tls_sw_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		   int nonblock, int flags, int *addr_len);
unsigned int tls_sw_poll(struct file *file, struct socket *sock,
			 struct poll_table_struct *wait);
ssize_t tls_sw_splice_read(struct socket *sock, loff_t *ppos,
			   struct pipe_inode_info *pipe,
			   size_t len, unsigned int flags);

int tls_set_device_offload(struct sock *sk, struct tls_context *ctx);
int tls_device_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
int tls_device_sendpage(struct sock *sk, struct page *page,
			int offset, size_t size, int flags);
void tls_device_sk_destruct(struct sock *sk);
void tls_device_init(void);
void tls_device_cleanup(void);

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

void tls_sk_destruct(struct sock *sk, struct tls_context *ctx);
int tls_push_sg(struct sock *sk, struct tls_context *ctx,
		struct scatterlist *sg, u16 first_offset,
		int flags);
int tls_push_pending_closed_record(struct sock *sk, struct tls_context *ctx,
				   int flags, long *timeo);

static inline bool tls_is_pending_closed_record(struct tls_context *ctx)
{
	return test_bit(TLS_PENDING_CLOSED_RECORD, &ctx->flags);
}

static inline int tls_complete_pending_work(struct sock *sk,
					    struct tls_context *ctx,
					    int flags, long *timeo)
{
	int rc = 0;

	if (unlikely(sk->sk_write_pending))
		rc = wait_on_pending_writer(sk, timeo);

	if (!rc && tls_is_pending_closed_record(ctx))
		rc = tls_push_pending_closed_record(sk, ctx, flags, timeo);

	return rc;
}

static inline bool tls_is_partially_sent_record(struct tls_context *ctx)
{
	return !!ctx->partially_sent_record;
}

static inline bool tls_is_pending_open_record(struct tls_context *tls_ctx)
{
	return tls_ctx->pending_open_record_frags;
}

struct sk_buff *
tls_validate_xmit_skb(struct sock *sk, struct net_device *dev,
		      struct sk_buff *skb);

static inline bool tls_is_sk_tx_device_offloaded(struct sock *sk)
{
#ifdef CONFIG_SOCK_VALIDATE_XMIT
	return sk_fullsock(sk) &
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

static inline void tls_advance_record_sn(struct sock *sk,
					 struct cipher_context *ctx)
{
	if (tls_bigint_increment(ctx->rec_seq, ctx->rec_seq_size))
		tls_err_abort(sk, EBADMSG);
	tls_bigint_increment(ctx->iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
			     ctx->iv_size);
}

static inline void tls_fill_prepend(struct tls_context *ctx,
			     char *buf,
			     size_t plaintext_len,
			     unsigned char record_type)
{
	size_t pkt_len, iv_size = ctx->tx.iv_size;

	pkt_len = plaintext_len + iv_size + ctx->tx.tag_size;

	/* we cover nonce explicit here as well, so buf should be of
	 * size KTLS_DTLS_HEADER_SIZE + KTLS_DTLS_NONCE_EXPLICIT_SIZE
	 */
	buf[0] = record_type;
	buf[1] = TLS_VERSION_MINOR(ctx->crypto_send.version);
	buf[2] = TLS_VERSION_MAJOR(ctx->crypto_send.version);
	/* we can use IV for nonce explicit according to spec */
	buf[3] = pkt_len >> 8;
	buf[4] = pkt_len & 0xFF;
	memcpy(buf + TLS_NONCE_OFFSET,
	       ctx->tx.iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE, iv_size);
}

static inline void tls_make_aad(char *buf,
				size_t size,
				char *record_sequence,
				int record_sequence_size,
				unsigned char record_type)
{
	memcpy(buf, record_sequence, record_sequence_size);

	buf[8] = record_type;
	buf[9] = TLS_1_2_VERSION_MAJOR;
	buf[10] = TLS_1_2_VERSION_MINOR;
	buf[11] = size >> 8;
	buf[12] = size & 0xFF;
}

static inline struct tls_context *tls_get_ctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	return icsk->icsk_ulp_data;
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

static inline struct tls_offload_context_rx *
tls_offload_ctx_rx(const struct tls_context *tls_ctx)
{
	return (struct tls_offload_context_rx *)tls_ctx->priv_ctx_rx;
}

/* The TLS context is valid until sk_destruct is called */
static inline void tls_offload_rx_resync_request(struct sock *sk, __be32 seq)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_offload_context_rx *rx_ctx = tls_offload_ctx_rx(tls_ctx);

	atomic64_set(&rx_ctx->resync_req, ((((uint64_t)seq) << 32) | 1));
}


int tls_proccess_cmsg(struct sock *sk, struct msghdr *msg,
		      unsigned char *record_type);
void tls_register_device(struct tls_device *device);
void tls_unregister_device(struct tls_device *device);
int tls_device_decrypted(struct sock *sk, struct sk_buff *skb);
int decrypt_skb(struct sock *sk, struct sk_buff *skb,
		struct scatterlist *sgout);

struct sk_buff *tls_validate_xmit_skb(struct sock *sk,
				      struct net_device *dev,
				      struct sk_buff *skb);

int tls_sw_fallback_init(struct sock *sk,
			 struct tls_offload_context_tx *offload_ctx,
			 struct tls_crypto_info *crypto_info);

int tls_set_device_offload_rx(struct sock *sk, struct tls_context *ctx);

void tls_device_offload_cleanup_rx(struct sock *sk);
void handle_device_resync(struct sock *sk, u32 seq, u64 rcd_sn);

#endif /* _TLS_OFFLOAD_H */
