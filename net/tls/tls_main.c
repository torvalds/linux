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

#include <linux/module.h>

#include <net/tcp.h>
#include <net/inet_common.h>
#include <linux/highmem.h>
#include <linux/netdevice.h>
#include <linux/sched/signal.h>
#include <linux/inetdevice.h>
#include <linux/inet_diag.h>

#include <net/snmp.h>
#include <net/tls.h>
#include <net/tls_toe.h>

#include "tls.h"

MODULE_AUTHOR("Mellanox Technologies");
MODULE_DESCRIPTION("Transport Layer Security Support");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_TCP_ULP("tls");

enum {
	TLSV4,
	TLSV6,
	TLS_NUM_PROTS,
};

#define CHECK_CIPHER_DESC(cipher,ci)				\
	static_assert(cipher ## _IV_SIZE <= MAX_IV_SIZE);		\
	static_assert(cipher ## _REC_SEQ_SIZE <= TLS_MAX_REC_SEQ_SIZE);	\
	static_assert(cipher ## _TAG_SIZE == TLS_TAG_SIZE);		\
	static_assert(sizeof_field(struct ci, iv) == cipher ## _IV_SIZE);	\
	static_assert(sizeof_field(struct ci, key) == cipher ## _KEY_SIZE);	\
	static_assert(sizeof_field(struct ci, salt) == cipher ## _SALT_SIZE);	\
	static_assert(sizeof_field(struct ci, rec_seq) == cipher ## _REC_SEQ_SIZE);

#define __CIPHER_DESC(ci) \
	.iv_offset = offsetof(struct ci, iv), \
	.key_offset = offsetof(struct ci, key), \
	.salt_offset = offsetof(struct ci, salt), \
	.rec_seq_offset = offsetof(struct ci, rec_seq), \
	.crypto_info = sizeof(struct ci)

#define CIPHER_DESC(cipher,ci,algname,_offloadable) [cipher - TLS_CIPHER_MIN] = {	\
	.nonce = cipher ## _IV_SIZE, \
	.iv = cipher ## _IV_SIZE, \
	.key = cipher ## _KEY_SIZE, \
	.salt = cipher ## _SALT_SIZE, \
	.tag = cipher ## _TAG_SIZE, \
	.rec_seq = cipher ## _REC_SEQ_SIZE, \
	.cipher_name = algname,	\
	.offloadable = _offloadable, \
	__CIPHER_DESC(ci), \
}

#define CIPHER_DESC_NONCE0(cipher,ci,algname,_offloadable) [cipher - TLS_CIPHER_MIN] = { \
	.nonce = 0, \
	.iv = cipher ## _IV_SIZE, \
	.key = cipher ## _KEY_SIZE, \
	.salt = cipher ## _SALT_SIZE, \
	.tag = cipher ## _TAG_SIZE, \
	.rec_seq = cipher ## _REC_SEQ_SIZE, \
	.cipher_name = algname,	\
	.offloadable = _offloadable, \
	__CIPHER_DESC(ci), \
}

const struct tls_cipher_desc tls_cipher_desc[TLS_CIPHER_MAX + 1 - TLS_CIPHER_MIN] = {
	CIPHER_DESC(TLS_CIPHER_AES_GCM_128, tls12_crypto_info_aes_gcm_128, "gcm(aes)", true),
	CIPHER_DESC(TLS_CIPHER_AES_GCM_256, tls12_crypto_info_aes_gcm_256, "gcm(aes)", true),
	CIPHER_DESC(TLS_CIPHER_AES_CCM_128, tls12_crypto_info_aes_ccm_128, "ccm(aes)", false),
	CIPHER_DESC_NONCE0(TLS_CIPHER_CHACHA20_POLY1305, tls12_crypto_info_chacha20_poly1305, "rfc7539(chacha20,poly1305)", false),
	CIPHER_DESC(TLS_CIPHER_SM4_GCM, tls12_crypto_info_sm4_gcm, "gcm(sm4)", false),
	CIPHER_DESC(TLS_CIPHER_SM4_CCM, tls12_crypto_info_sm4_ccm, "ccm(sm4)", false),
	CIPHER_DESC(TLS_CIPHER_ARIA_GCM_128, tls12_crypto_info_aria_gcm_128, "gcm(aria)", false),
	CIPHER_DESC(TLS_CIPHER_ARIA_GCM_256, tls12_crypto_info_aria_gcm_256, "gcm(aria)", false),
};

CHECK_CIPHER_DESC(TLS_CIPHER_AES_GCM_128, tls12_crypto_info_aes_gcm_128);
CHECK_CIPHER_DESC(TLS_CIPHER_AES_GCM_256, tls12_crypto_info_aes_gcm_256);
CHECK_CIPHER_DESC(TLS_CIPHER_AES_CCM_128, tls12_crypto_info_aes_ccm_128);
CHECK_CIPHER_DESC(TLS_CIPHER_CHACHA20_POLY1305, tls12_crypto_info_chacha20_poly1305);
CHECK_CIPHER_DESC(TLS_CIPHER_SM4_GCM, tls12_crypto_info_sm4_gcm);
CHECK_CIPHER_DESC(TLS_CIPHER_SM4_CCM, tls12_crypto_info_sm4_ccm);
CHECK_CIPHER_DESC(TLS_CIPHER_ARIA_GCM_128, tls12_crypto_info_aria_gcm_128);
CHECK_CIPHER_DESC(TLS_CIPHER_ARIA_GCM_256, tls12_crypto_info_aria_gcm_256);

static const struct proto *saved_tcpv6_prot;
static DEFINE_MUTEX(tcpv6_prot_mutex);
static const struct proto *saved_tcpv4_prot;
static DEFINE_MUTEX(tcpv4_prot_mutex);
static struct proto tls_prots[TLS_NUM_PROTS][TLS_NUM_CONFIG][TLS_NUM_CONFIG];
static struct proto_ops tls_proto_ops[TLS_NUM_PROTS][TLS_NUM_CONFIG][TLS_NUM_CONFIG];
static void build_protos(struct proto prot[TLS_NUM_CONFIG][TLS_NUM_CONFIG],
			 const struct proto *base);

void update_sk_prot(struct sock *sk, struct tls_context *ctx)
{
	int ip_ver = sk->sk_family == AF_INET6 ? TLSV6 : TLSV4;

	WRITE_ONCE(sk->sk_prot,
		   &tls_prots[ip_ver][ctx->tx_conf][ctx->rx_conf]);
	WRITE_ONCE(sk->sk_socket->ops,
		   &tls_proto_ops[ip_ver][ctx->tx_conf][ctx->rx_conf]);
}

int wait_on_pending_writer(struct sock *sk, long *timeo)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret, rc = 0;

	add_wait_queue(sk_sleep(sk), &wait);
	while (1) {
		if (!*timeo) {
			rc = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			rc = sock_intr_errno(*timeo);
			break;
		}

		ret = sk_wait_event(sk, timeo,
				    !READ_ONCE(sk->sk_write_pending), &wait);
		if (ret) {
			if (ret < 0)
				rc = ret;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
}

int tls_push_sg(struct sock *sk,
		struct tls_context *ctx,
		struct scatterlist *sg,
		u16 first_offset,
		int flags)
{
	struct bio_vec bvec;
	struct msghdr msg = {
		.msg_flags = MSG_SPLICE_PAGES | flags,
	};
	int ret = 0;
	struct page *p;
	size_t size;
	int offset = first_offset;

	size = sg->length - offset;
	offset += sg->offset;

	ctx->splicing_pages = true;
	while (1) {
		/* is sending application-limited? */
		tcp_rate_check_app_limited(sk);
		p = sg_page(sg);
retry:
		bvec_set_page(&bvec, p, size, offset);
		iov_iter_bvec(&msg.msg_iter, ITER_SOURCE, &bvec, 1, size);

		ret = tcp_sendmsg_locked(sk, &msg, size);

		if (ret != size) {
			if (ret > 0) {
				offset += ret;
				size -= ret;
				goto retry;
			}

			offset -= sg->offset;
			ctx->partially_sent_offset = offset;
			ctx->partially_sent_record = (void *)sg;
			ctx->splicing_pages = false;
			return ret;
		}

		put_page(p);
		sk_mem_uncharge(sk, sg->length);
		sg = sg_next(sg);
		if (!sg)
			break;

		offset = sg->offset;
		size = sg->length;
	}

	ctx->splicing_pages = false;

	return 0;
}

static int tls_handle_open_record(struct sock *sk, int flags)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (tls_is_pending_open_record(ctx))
		return ctx->push_pending_record(sk, flags);

	return 0;
}

int tls_process_cmsg(struct sock *sk, struct msghdr *msg,
		     unsigned char *record_type)
{
	struct cmsghdr *cmsg;
	int rc = -EINVAL;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;
		if (cmsg->cmsg_level != SOL_TLS)
			continue;

		switch (cmsg->cmsg_type) {
		case TLS_SET_RECORD_TYPE:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(*record_type)))
				return -EINVAL;

			if (msg->msg_flags & MSG_MORE)
				return -EINVAL;

			rc = tls_handle_open_record(sk, msg->msg_flags);
			if (rc)
				return rc;

			*record_type = *(unsigned char *)CMSG_DATA(cmsg);
			rc = 0;
			break;
		default:
			return -EINVAL;
		}
	}

	return rc;
}

int tls_push_partial_record(struct sock *sk, struct tls_context *ctx,
			    int flags)
{
	struct scatterlist *sg;
	u16 offset;

	sg = ctx->partially_sent_record;
	offset = ctx->partially_sent_offset;

	ctx->partially_sent_record = NULL;
	return tls_push_sg(sk, ctx, sg, offset, flags);
}

void tls_free_partial_record(struct sock *sk, struct tls_context *ctx)
{
	struct scatterlist *sg;

	for (sg = ctx->partially_sent_record; sg; sg = sg_next(sg)) {
		put_page(sg_page(sg));
		sk_mem_uncharge(sk, sg->length);
	}
	ctx->partially_sent_record = NULL;
}

static void tls_write_space(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	/* If splicing_pages call lower protocol write space handler
	 * to ensure we wake up any waiting operations there. For example
	 * if splicing pages where to call sk_wait_event.
	 */
	if (ctx->splicing_pages) {
		ctx->sk_write_space(sk);
		return;
	}

#ifdef CONFIG_TLS_DEVICE
	if (ctx->tx_conf == TLS_HW)
		tls_device_write_space(sk, ctx);
	else
#endif
		tls_sw_write_space(sk, ctx);

	ctx->sk_write_space(sk);
}

/**
 * tls_ctx_free() - free TLS ULP context
 * @sk:  socket to with @ctx is attached
 * @ctx: TLS context structure
 *
 * Free TLS context. If @sk is %NULL caller guarantees that the socket
 * to which @ctx was attached has no outstanding references.
 */
void tls_ctx_free(struct sock *sk, struct tls_context *ctx)
{
	if (!ctx)
		return;

	memzero_explicit(&ctx->crypto_send, sizeof(ctx->crypto_send));
	memzero_explicit(&ctx->crypto_recv, sizeof(ctx->crypto_recv));
	mutex_destroy(&ctx->tx_lock);

	if (sk)
		kfree_rcu(ctx, rcu);
	else
		kfree(ctx);
}

static void tls_sk_proto_cleanup(struct sock *sk,
				 struct tls_context *ctx, long timeo)
{
	if (unlikely(sk->sk_write_pending) &&
	    !wait_on_pending_writer(sk, &timeo))
		tls_handle_open_record(sk, 0);

	/* We need these for tls_sw_fallback handling of other packets */
	if (ctx->tx_conf == TLS_SW) {
		kfree(ctx->tx.rec_seq);
		kfree(ctx->tx.iv);
		tls_sw_release_resources_tx(sk);
		TLS_DEC_STATS(sock_net(sk), LINUX_MIB_TLSCURRTXSW);
	} else if (ctx->tx_conf == TLS_HW) {
		tls_device_free_resources_tx(sk);
		TLS_DEC_STATS(sock_net(sk), LINUX_MIB_TLSCURRTXDEVICE);
	}

	if (ctx->rx_conf == TLS_SW) {
		tls_sw_release_resources_rx(sk);
		TLS_DEC_STATS(sock_net(sk), LINUX_MIB_TLSCURRRXSW);
	} else if (ctx->rx_conf == TLS_HW) {
		tls_device_offload_cleanup_rx(sk);
		TLS_DEC_STATS(sock_net(sk), LINUX_MIB_TLSCURRRXDEVICE);
	}
}

static void tls_sk_proto_close(struct sock *sk, long timeout)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tls_context *ctx = tls_get_ctx(sk);
	long timeo = sock_sndtimeo(sk, 0);
	bool free_ctx;

	if (ctx->tx_conf == TLS_SW)
		tls_sw_cancel_work_tx(ctx);

	lock_sock(sk);
	free_ctx = ctx->tx_conf != TLS_HW && ctx->rx_conf != TLS_HW;

	if (ctx->tx_conf != TLS_BASE || ctx->rx_conf != TLS_BASE)
		tls_sk_proto_cleanup(sk, ctx, timeo);

	write_lock_bh(&sk->sk_callback_lock);
	if (free_ctx)
		rcu_assign_pointer(icsk->icsk_ulp_data, NULL);
	WRITE_ONCE(sk->sk_prot, ctx->sk_proto);
	if (sk->sk_write_space == tls_write_space)
		sk->sk_write_space = ctx->sk_write_space;
	write_unlock_bh(&sk->sk_callback_lock);
	release_sock(sk);
	if (ctx->tx_conf == TLS_SW)
		tls_sw_free_ctx_tx(ctx);
	if (ctx->rx_conf == TLS_SW || ctx->rx_conf == TLS_HW)
		tls_sw_strparser_done(ctx);
	if (ctx->rx_conf == TLS_SW)
		tls_sw_free_ctx_rx(ctx);
	ctx->sk_proto->close(sk, timeout);

	if (free_ctx)
		tls_ctx_free(sk, ctx);
}

static __poll_t tls_sk_poll(struct file *file, struct socket *sock,
			    struct poll_table_struct *wait)
{
	struct tls_sw_context_rx *ctx;
	struct tls_context *tls_ctx;
	struct sock *sk = sock->sk;
	struct sk_psock *psock;
	__poll_t mask = 0;
	u8 shutdown;
	int state;

	mask = tcp_poll(file, sock, wait);

	state = inet_sk_state_load(sk);
	shutdown = READ_ONCE(sk->sk_shutdown);
	if (unlikely(state != TCP_ESTABLISHED || shutdown & RCV_SHUTDOWN))
		return mask;

	tls_ctx = tls_get_ctx(sk);
	ctx = tls_sw_ctx_rx(tls_ctx);
	psock = sk_psock_get(sk);

	if (skb_queue_empty_lockless(&ctx->rx_list) &&
	    !tls_strp_msg_ready(ctx) &&
	    sk_psock_queue_empty(psock))
		mask &= ~(EPOLLIN | EPOLLRDNORM);

	if (psock)
		sk_psock_put(sk, psock);

	return mask;
}

static int do_tls_getsockopt_conf(struct sock *sk, char __user *optval,
				  int __user *optlen, int tx)
{
	int rc = 0;
	const struct tls_cipher_desc *cipher_desc;
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_crypto_info *crypto_info;
	struct cipher_context *cctx;
	int len;

	if (get_user(len, optlen))
		return -EFAULT;

	if (!optval || (len < sizeof(*crypto_info))) {
		rc = -EINVAL;
		goto out;
	}

	if (!ctx) {
		rc = -EBUSY;
		goto out;
	}

	/* get user crypto info */
	if (tx) {
		crypto_info = &ctx->crypto_send.info;
		cctx = &ctx->tx;
	} else {
		crypto_info = &ctx->crypto_recv.info;
		cctx = &ctx->rx;
	}

	if (!TLS_CRYPTO_INFO_READY(crypto_info)) {
		rc = -EBUSY;
		goto out;
	}

	if (len == sizeof(*crypto_info)) {
		if (copy_to_user(optval, crypto_info, sizeof(*crypto_info)))
			rc = -EFAULT;
		goto out;
	}

	cipher_desc = get_cipher_desc(crypto_info->cipher_type);
	if (!cipher_desc || len != cipher_desc->crypto_info) {
		rc = -EINVAL;
		goto out;
	}

	memcpy(crypto_info_iv(crypto_info, cipher_desc),
	       cctx->iv + cipher_desc->salt, cipher_desc->iv);
	memcpy(crypto_info_rec_seq(crypto_info, cipher_desc),
	       cctx->rec_seq, cipher_desc->rec_seq);

	if (copy_to_user(optval, crypto_info, cipher_desc->crypto_info))
		rc = -EFAULT;

out:
	return rc;
}

static int do_tls_getsockopt_tx_zc(struct sock *sk, char __user *optval,
				   int __user *optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	unsigned int value;
	int len;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len != sizeof(value))
		return -EINVAL;

	value = ctx->zerocopy_sendfile;
	if (copy_to_user(optval, &value, sizeof(value)))
		return -EFAULT;

	return 0;
}

static int do_tls_getsockopt_no_pad(struct sock *sk, char __user *optval,
				    int __user *optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	int value, len;

	if (ctx->prot_info.version != TLS_1_3_VERSION)
		return -EINVAL;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < sizeof(value))
		return -EINVAL;

	value = -EINVAL;
	if (ctx->rx_conf == TLS_SW || ctx->rx_conf == TLS_HW)
		value = ctx->rx_no_pad;
	if (value < 0)
		return value;

	if (put_user(sizeof(value), optlen))
		return -EFAULT;
	if (copy_to_user(optval, &value, sizeof(value)))
		return -EFAULT;

	return 0;
}

static int do_tls_getsockopt(struct sock *sk, int optname,
			     char __user *optval, int __user *optlen)
{
	int rc = 0;

	lock_sock(sk);

	switch (optname) {
	case TLS_TX:
	case TLS_RX:
		rc = do_tls_getsockopt_conf(sk, optval, optlen,
					    optname == TLS_TX);
		break;
	case TLS_TX_ZEROCOPY_RO:
		rc = do_tls_getsockopt_tx_zc(sk, optval, optlen);
		break;
	case TLS_RX_EXPECT_NO_PAD:
		rc = do_tls_getsockopt_no_pad(sk, optval, optlen);
		break;
	default:
		rc = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);

	return rc;
}

static int tls_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (level != SOL_TLS)
		return ctx->sk_proto->getsockopt(sk, level,
						 optname, optval, optlen);

	return do_tls_getsockopt(sk, optname, optval, optlen);
}

static int do_tls_setsockopt_conf(struct sock *sk, sockptr_t optval,
				  unsigned int optlen, int tx)
{
	struct tls_crypto_info *crypto_info;
	struct tls_crypto_info *alt_crypto_info;
	struct tls_context *ctx = tls_get_ctx(sk);
	const struct tls_cipher_desc *cipher_desc;
	int rc = 0;
	int conf;

	if (sockptr_is_null(optval) || (optlen < sizeof(*crypto_info)))
		return -EINVAL;

	if (tx) {
		crypto_info = &ctx->crypto_send.info;
		alt_crypto_info = &ctx->crypto_recv.info;
	} else {
		crypto_info = &ctx->crypto_recv.info;
		alt_crypto_info = &ctx->crypto_send.info;
	}

	/* Currently we don't support set crypto info more than one time */
	if (TLS_CRYPTO_INFO_READY(crypto_info))
		return -EBUSY;

	rc = copy_from_sockptr(crypto_info, optval, sizeof(*crypto_info));
	if (rc) {
		rc = -EFAULT;
		goto err_crypto_info;
	}

	/* check version */
	if (crypto_info->version != TLS_1_2_VERSION &&
	    crypto_info->version != TLS_1_3_VERSION) {
		rc = -EINVAL;
		goto err_crypto_info;
	}

	/* Ensure that TLS version and ciphers are same in both directions */
	if (TLS_CRYPTO_INFO_READY(alt_crypto_info)) {
		if (alt_crypto_info->version != crypto_info->version ||
		    alt_crypto_info->cipher_type != crypto_info->cipher_type) {
			rc = -EINVAL;
			goto err_crypto_info;
		}
	}

	cipher_desc = get_cipher_desc(crypto_info->cipher_type);
	if (!cipher_desc) {
		rc = -EINVAL;
		goto err_crypto_info;
	}

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_ARIA_GCM_128:
	case TLS_CIPHER_ARIA_GCM_256:
		if (crypto_info->version != TLS_1_2_VERSION) {
			rc = -EINVAL;
			goto err_crypto_info;
		}
		break;
	}

	if (optlen != cipher_desc->crypto_info) {
		rc = -EINVAL;
		goto err_crypto_info;
	}

	rc = copy_from_sockptr_offset(crypto_info + 1, optval,
				      sizeof(*crypto_info),
				      optlen - sizeof(*crypto_info));
	if (rc) {
		rc = -EFAULT;
		goto err_crypto_info;
	}

	if (tx) {
		rc = tls_set_device_offload(sk, ctx);
		conf = TLS_HW;
		if (!rc) {
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSTXDEVICE);
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSCURRTXDEVICE);
		} else {
			rc = tls_set_sw_offload(sk, ctx, 1);
			if (rc)
				goto err_crypto_info;
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSTXSW);
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSCURRTXSW);
			conf = TLS_SW;
		}
	} else {
		rc = tls_set_device_offload_rx(sk, ctx);
		conf = TLS_HW;
		if (!rc) {
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSRXDEVICE);
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSCURRRXDEVICE);
		} else {
			rc = tls_set_sw_offload(sk, ctx, 0);
			if (rc)
				goto err_crypto_info;
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSRXSW);
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSCURRRXSW);
			conf = TLS_SW;
		}
		tls_sw_strparser_arm(sk, ctx);
	}

	if (tx)
		ctx->tx_conf = conf;
	else
		ctx->rx_conf = conf;
	update_sk_prot(sk, ctx);
	if (tx) {
		ctx->sk_write_space = sk->sk_write_space;
		sk->sk_write_space = tls_write_space;
	} else {
		struct tls_sw_context_rx *rx_ctx = tls_sw_ctx_rx(ctx);

		tls_strp_check_rcv(&rx_ctx->strp);
	}
	return 0;

err_crypto_info:
	memzero_explicit(crypto_info, sizeof(union tls_crypto_context));
	return rc;
}

static int do_tls_setsockopt_tx_zc(struct sock *sk, sockptr_t optval,
				   unsigned int optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	unsigned int value;

	if (sockptr_is_null(optval) || optlen != sizeof(value))
		return -EINVAL;

	if (copy_from_sockptr(&value, optval, sizeof(value)))
		return -EFAULT;

	if (value > 1)
		return -EINVAL;

	ctx->zerocopy_sendfile = value;

	return 0;
}

static int do_tls_setsockopt_no_pad(struct sock *sk, sockptr_t optval,
				    unsigned int optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	u32 val;
	int rc;

	if (ctx->prot_info.version != TLS_1_3_VERSION ||
	    sockptr_is_null(optval) || optlen < sizeof(val))
		return -EINVAL;

	rc = copy_from_sockptr(&val, optval, sizeof(val));
	if (rc)
		return -EFAULT;
	if (val > 1)
		return -EINVAL;
	rc = check_zeroed_sockptr(optval, sizeof(val), optlen - sizeof(val));
	if (rc < 1)
		return rc == 0 ? -EINVAL : rc;

	lock_sock(sk);
	rc = -EINVAL;
	if (ctx->rx_conf == TLS_SW || ctx->rx_conf == TLS_HW) {
		ctx->rx_no_pad = val;
		tls_update_rx_zc_capable(ctx);
		rc = 0;
	}
	release_sock(sk);

	return rc;
}

static int do_tls_setsockopt(struct sock *sk, int optname, sockptr_t optval,
			     unsigned int optlen)
{
	int rc = 0;

	switch (optname) {
	case TLS_TX:
	case TLS_RX:
		lock_sock(sk);
		rc = do_tls_setsockopt_conf(sk, optval, optlen,
					    optname == TLS_TX);
		release_sock(sk);
		break;
	case TLS_TX_ZEROCOPY_RO:
		lock_sock(sk);
		rc = do_tls_setsockopt_tx_zc(sk, optval, optlen);
		release_sock(sk);
		break;
	case TLS_RX_EXPECT_NO_PAD:
		rc = do_tls_setsockopt_no_pad(sk, optval, optlen);
		break;
	default:
		rc = -ENOPROTOOPT;
		break;
	}
	return rc;
}

static int tls_setsockopt(struct sock *sk, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (level != SOL_TLS)
		return ctx->sk_proto->setsockopt(sk, level, optname, optval,
						 optlen);

	return do_tls_setsockopt(sk, optname, optval, optlen);
}

struct tls_context *tls_ctx_create(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tls_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return NULL;

	mutex_init(&ctx->tx_lock);
	rcu_assign_pointer(icsk->icsk_ulp_data, ctx);
	ctx->sk_proto = READ_ONCE(sk->sk_prot);
	ctx->sk = sk;
	return ctx;
}

static void build_proto_ops(struct proto_ops ops[TLS_NUM_CONFIG][TLS_NUM_CONFIG],
			    const struct proto_ops *base)
{
	ops[TLS_BASE][TLS_BASE] = *base;

	ops[TLS_SW  ][TLS_BASE] = ops[TLS_BASE][TLS_BASE];
	ops[TLS_SW  ][TLS_BASE].splice_eof	= tls_sw_splice_eof;

	ops[TLS_BASE][TLS_SW  ] = ops[TLS_BASE][TLS_BASE];
	ops[TLS_BASE][TLS_SW  ].splice_read	= tls_sw_splice_read;
	ops[TLS_BASE][TLS_SW  ].poll		= tls_sk_poll;
	ops[TLS_BASE][TLS_SW  ].read_sock	= tls_sw_read_sock;

	ops[TLS_SW  ][TLS_SW  ] = ops[TLS_SW  ][TLS_BASE];
	ops[TLS_SW  ][TLS_SW  ].splice_read	= tls_sw_splice_read;
	ops[TLS_SW  ][TLS_SW  ].poll		= tls_sk_poll;
	ops[TLS_SW  ][TLS_SW  ].read_sock	= tls_sw_read_sock;

#ifdef CONFIG_TLS_DEVICE
	ops[TLS_HW  ][TLS_BASE] = ops[TLS_BASE][TLS_BASE];

	ops[TLS_HW  ][TLS_SW  ] = ops[TLS_BASE][TLS_SW  ];

	ops[TLS_BASE][TLS_HW  ] = ops[TLS_BASE][TLS_SW  ];

	ops[TLS_SW  ][TLS_HW  ] = ops[TLS_SW  ][TLS_SW  ];

	ops[TLS_HW  ][TLS_HW  ] = ops[TLS_HW  ][TLS_SW  ];
#endif
#ifdef CONFIG_TLS_TOE
	ops[TLS_HW_RECORD][TLS_HW_RECORD] = *base;
#endif
}

static void tls_build_proto(struct sock *sk)
{
	int ip_ver = sk->sk_family == AF_INET6 ? TLSV6 : TLSV4;
	struct proto *prot = READ_ONCE(sk->sk_prot);

	/* Build IPv6 TLS whenever the address of tcpv6 _prot changes */
	if (ip_ver == TLSV6 &&
	    unlikely(prot != smp_load_acquire(&saved_tcpv6_prot))) {
		mutex_lock(&tcpv6_prot_mutex);
		if (likely(prot != saved_tcpv6_prot)) {
			build_protos(tls_prots[TLSV6], prot);
			build_proto_ops(tls_proto_ops[TLSV6],
					sk->sk_socket->ops);
			smp_store_release(&saved_tcpv6_prot, prot);
		}
		mutex_unlock(&tcpv6_prot_mutex);
	}

	if (ip_ver == TLSV4 &&
	    unlikely(prot != smp_load_acquire(&saved_tcpv4_prot))) {
		mutex_lock(&tcpv4_prot_mutex);
		if (likely(prot != saved_tcpv4_prot)) {
			build_protos(tls_prots[TLSV4], prot);
			build_proto_ops(tls_proto_ops[TLSV4],
					sk->sk_socket->ops);
			smp_store_release(&saved_tcpv4_prot, prot);
		}
		mutex_unlock(&tcpv4_prot_mutex);
	}
}

static void build_protos(struct proto prot[TLS_NUM_CONFIG][TLS_NUM_CONFIG],
			 const struct proto *base)
{
	prot[TLS_BASE][TLS_BASE] = *base;
	prot[TLS_BASE][TLS_BASE].setsockopt	= tls_setsockopt;
	prot[TLS_BASE][TLS_BASE].getsockopt	= tls_getsockopt;
	prot[TLS_BASE][TLS_BASE].close		= tls_sk_proto_close;

	prot[TLS_SW][TLS_BASE] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_SW][TLS_BASE].sendmsg		= tls_sw_sendmsg;
	prot[TLS_SW][TLS_BASE].splice_eof	= tls_sw_splice_eof;

	prot[TLS_BASE][TLS_SW] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_BASE][TLS_SW].recvmsg		  = tls_sw_recvmsg;
	prot[TLS_BASE][TLS_SW].sock_is_readable   = tls_sw_sock_is_readable;
	prot[TLS_BASE][TLS_SW].close		  = tls_sk_proto_close;

	prot[TLS_SW][TLS_SW] = prot[TLS_SW][TLS_BASE];
	prot[TLS_SW][TLS_SW].recvmsg		= tls_sw_recvmsg;
	prot[TLS_SW][TLS_SW].sock_is_readable   = tls_sw_sock_is_readable;
	prot[TLS_SW][TLS_SW].close		= tls_sk_proto_close;

#ifdef CONFIG_TLS_DEVICE
	prot[TLS_HW][TLS_BASE] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_HW][TLS_BASE].sendmsg		= tls_device_sendmsg;
	prot[TLS_HW][TLS_BASE].splice_eof	= tls_device_splice_eof;

	prot[TLS_HW][TLS_SW] = prot[TLS_BASE][TLS_SW];
	prot[TLS_HW][TLS_SW].sendmsg		= tls_device_sendmsg;
	prot[TLS_HW][TLS_SW].splice_eof		= tls_device_splice_eof;

	prot[TLS_BASE][TLS_HW] = prot[TLS_BASE][TLS_SW];

	prot[TLS_SW][TLS_HW] = prot[TLS_SW][TLS_SW];

	prot[TLS_HW][TLS_HW] = prot[TLS_HW][TLS_SW];
#endif
#ifdef CONFIG_TLS_TOE
	prot[TLS_HW_RECORD][TLS_HW_RECORD] = *base;
	prot[TLS_HW_RECORD][TLS_HW_RECORD].hash		= tls_toe_hash;
	prot[TLS_HW_RECORD][TLS_HW_RECORD].unhash	= tls_toe_unhash;
#endif
}

static int tls_init(struct sock *sk)
{
	struct tls_context *ctx;
	int rc = 0;

	tls_build_proto(sk);

#ifdef CONFIG_TLS_TOE
	if (tls_toe_bypass(sk))
		return 0;
#endif

	/* The TLS ulp is currently supported only for TCP sockets
	 * in ESTABLISHED state.
	 * Supporting sockets in LISTEN state will require us
	 * to modify the accept implementation to clone rather then
	 * share the ulp context.
	 */
	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* allocate tls context */
	write_lock_bh(&sk->sk_callback_lock);
	ctx = tls_ctx_create(sk);
	if (!ctx) {
		rc = -ENOMEM;
		goto out;
	}

	ctx->tx_conf = TLS_BASE;
	ctx->rx_conf = TLS_BASE;
	update_sk_prot(sk, ctx);
out:
	write_unlock_bh(&sk->sk_callback_lock);
	return rc;
}

static void tls_update(struct sock *sk, struct proto *p,
		       void (*write_space)(struct sock *sk))
{
	struct tls_context *ctx;

	WARN_ON_ONCE(sk->sk_prot == p);

	ctx = tls_get_ctx(sk);
	if (likely(ctx)) {
		ctx->sk_write_space = write_space;
		ctx->sk_proto = p;
	} else {
		/* Pairs with lockless read in sk_clone_lock(). */
		WRITE_ONCE(sk->sk_prot, p);
		sk->sk_write_space = write_space;
	}
}

static u16 tls_user_config(struct tls_context *ctx, bool tx)
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

static int tls_get_info(const struct sock *sk, struct sk_buff *skb)
{
	u16 version, cipher_type;
	struct tls_context *ctx;
	struct nlattr *start;
	int err;

	start = nla_nest_start_noflag(skb, INET_ULP_INFO_TLS);
	if (!start)
		return -EMSGSIZE;

	rcu_read_lock();
	ctx = rcu_dereference(inet_csk(sk)->icsk_ulp_data);
	if (!ctx) {
		err = 0;
		goto nla_failure;
	}
	version = ctx->prot_info.version;
	if (version) {
		err = nla_put_u16(skb, TLS_INFO_VERSION, version);
		if (err)
			goto nla_failure;
	}
	cipher_type = ctx->prot_info.cipher_type;
	if (cipher_type) {
		err = nla_put_u16(skb, TLS_INFO_CIPHER, cipher_type);
		if (err)
			goto nla_failure;
	}
	err = nla_put_u16(skb, TLS_INFO_TXCONF, tls_user_config(ctx, true));
	if (err)
		goto nla_failure;

	err = nla_put_u16(skb, TLS_INFO_RXCONF, tls_user_config(ctx, false));
	if (err)
		goto nla_failure;

	if (ctx->tx_conf == TLS_HW && ctx->zerocopy_sendfile) {
		err = nla_put_flag(skb, TLS_INFO_ZC_RO_TX);
		if (err)
			goto nla_failure;
	}
	if (ctx->rx_no_pad) {
		err = nla_put_flag(skb, TLS_INFO_RX_NO_PAD);
		if (err)
			goto nla_failure;
	}

	rcu_read_unlock();
	nla_nest_end(skb, start);
	return 0;

nla_failure:
	rcu_read_unlock();
	nla_nest_cancel(skb, start);
	return err;
}

static size_t tls_get_info_size(const struct sock *sk)
{
	size_t size = 0;

	size += nla_total_size(0) +		/* INET_ULP_INFO_TLS */
		nla_total_size(sizeof(u16)) +	/* TLS_INFO_VERSION */
		nla_total_size(sizeof(u16)) +	/* TLS_INFO_CIPHER */
		nla_total_size(sizeof(u16)) +	/* TLS_INFO_RXCONF */
		nla_total_size(sizeof(u16)) +	/* TLS_INFO_TXCONF */
		nla_total_size(0) +		/* TLS_INFO_ZC_RO_TX */
		nla_total_size(0) +		/* TLS_INFO_RX_NO_PAD */
		0;

	return size;
}

static int __net_init tls_init_net(struct net *net)
{
	int err;

	net->mib.tls_statistics = alloc_percpu(struct linux_tls_mib);
	if (!net->mib.tls_statistics)
		return -ENOMEM;

	err = tls_proc_init(net);
	if (err)
		goto err_free_stats;

	return 0;
err_free_stats:
	free_percpu(net->mib.tls_statistics);
	return err;
}

static void __net_exit tls_exit_net(struct net *net)
{
	tls_proc_fini(net);
	free_percpu(net->mib.tls_statistics);
}

static struct pernet_operations tls_proc_ops = {
	.init = tls_init_net,
	.exit = tls_exit_net,
};

static struct tcp_ulp_ops tcp_tls_ulp_ops __read_mostly = {
	.name			= "tls",
	.owner			= THIS_MODULE,
	.init			= tls_init,
	.update			= tls_update,
	.get_info		= tls_get_info,
	.get_info_size		= tls_get_info_size,
};

static int __init tls_register(void)
{
	int err;

	err = register_pernet_subsys(&tls_proc_ops);
	if (err)
		return err;

	err = tls_strp_dev_init();
	if (err)
		goto err_pernet;

	err = tls_device_init();
	if (err)
		goto err_strp;

	tcp_register_ulp(&tcp_tls_ulp_ops);

	return 0;
err_strp:
	tls_strp_dev_exit();
err_pernet:
	unregister_pernet_subsys(&tls_proc_ops);
	return err;
}

static void __exit tls_unregister(void)
{
	tcp_unregister_ulp(&tcp_tls_ulp_ops);
	tls_strp_dev_exit();
	tls_device_cleanup();
	unregister_pernet_subsys(&tls_proc_ops);
}

module_init(tls_register);
module_exit(tls_unregister);
