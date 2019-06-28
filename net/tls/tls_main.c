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

#include <net/tls.h>

MODULE_AUTHOR("Mellanox Technologies");
MODULE_DESCRIPTION("Transport Layer Security Support");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_TCP_ULP("tls");

enum {
	TLSV4,
	TLSV6,
	TLS_NUM_PROTS,
};

static struct proto *saved_tcpv6_prot;
static DEFINE_MUTEX(tcpv6_prot_mutex);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_mutex);
static struct proto tls_prots[TLS_NUM_PROTS][TLS_NUM_CONFIG][TLS_NUM_CONFIG];
static struct proto_ops tls_sw_proto_ops;

static void update_sk_prot(struct sock *sk, struct tls_context *ctx)
{
	int ip_ver = sk->sk_family == AF_INET6 ? TLSV6 : TLSV4;

	sk->sk_prot = &tls_prots[ip_ver][ctx->tx_conf][ctx->rx_conf];
}

int wait_on_pending_writer(struct sock *sk, long *timeo)
{
	int rc = 0;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

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

		if (sk_wait_event(sk, timeo, !sk->sk_write_pending, &wait))
			break;
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
	int sendpage_flags = flags | MSG_SENDPAGE_NOTLAST;
	int ret = 0;
	struct page *p;
	size_t size;
	int offset = first_offset;

	size = sg->length - offset;
	offset += sg->offset;

	ctx->in_tcp_sendpages = true;
	while (1) {
		if (sg_is_last(sg))
			sendpage_flags = flags;

		/* is sending application-limited? */
		tcp_rate_check_app_limited(sk);
		p = sg_page(sg);
retry:
		ret = do_tcp_sendpages(sk, p, offset, size, sendpage_flags);

		if (ret != size) {
			if (ret > 0) {
				offset += ret;
				size -= ret;
				goto retry;
			}

			offset -= sg->offset;
			ctx->partially_sent_offset = offset;
			ctx->partially_sent_record = (void *)sg;
			ctx->in_tcp_sendpages = false;
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

	clear_bit(TLS_PENDING_CLOSED_RECORD, &ctx->flags);
	ctx->in_tcp_sendpages = false;
	ctx->sk_write_space(sk);

	return 0;
}

static int tls_handle_open_record(struct sock *sk, int flags)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (tls_is_pending_open_record(ctx))
		return ctx->push_pending_record(sk, flags);

	return 0;
}

int tls_proccess_cmsg(struct sock *sk, struct msghdr *msg,
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

int tls_push_pending_closed_record(struct sock *sk, struct tls_context *ctx,
				   int flags, long *timeo)
{
	struct scatterlist *sg;
	u16 offset;

	if (!tls_is_partially_sent_record(ctx))
		return ctx->push_pending_record(sk, flags);

	sg = ctx->partially_sent_record;
	offset = ctx->partially_sent_offset;

	ctx->partially_sent_record = NULL;
	return tls_push_sg(sk, ctx, sg, offset, flags);
}

static void tls_write_space(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	/* If in_tcp_sendpages call lower protocol write space handler
	 * to ensure we wake up any waiting operations there. For example
	 * if do_tcp_sendpages where to call sk_wait_event.
	 */
	if (ctx->in_tcp_sendpages) {
		ctx->sk_write_space(sk);
		return;
	}

	if (!sk->sk_write_pending && tls_is_pending_closed_record(ctx)) {
		gfp_t sk_allocation = sk->sk_allocation;
		int rc;
		long timeo = 0;

		sk->sk_allocation = GFP_ATOMIC;
		rc = tls_push_pending_closed_record(sk, ctx,
						    MSG_DONTWAIT |
						    MSG_NOSIGNAL,
						    &timeo);
		sk->sk_allocation = sk_allocation;

		if (rc < 0)
			return;
	}

	ctx->sk_write_space(sk);
}

void tls_ctx_free(struct tls_context *ctx)
{
	if (!ctx)
		return;

	memzero_explicit(&ctx->crypto_send, sizeof(ctx->crypto_send));
	memzero_explicit(&ctx->crypto_recv, sizeof(ctx->crypto_recv));
	kfree(ctx);
}

static void tls_sk_proto_close(struct sock *sk, long timeout)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	long timeo = sock_sndtimeo(sk, 0);
	void (*sk_proto_close)(struct sock *sk, long timeout);
	bool free_ctx = false;

	lock_sock(sk);
	sk_proto_close = ctx->sk_proto_close;

	if ((ctx->tx_conf == TLS_HW_RECORD && ctx->rx_conf == TLS_HW_RECORD) ||
	    (ctx->tx_conf == TLS_BASE && ctx->rx_conf == TLS_BASE)) {
		free_ctx = true;
		goto skip_tx_cleanup;
	}

	if (!tls_complete_pending_work(sk, ctx, 0, &timeo))
		tls_handle_open_record(sk, 0);

	if (ctx->partially_sent_record) {
		struct scatterlist *sg = ctx->partially_sent_record;

		while (1) {
			put_page(sg_page(sg));
			sk_mem_uncharge(sk, sg->length);

			if (sg_is_last(sg))
				break;
			sg++;
		}
	}

	/* We need these for tls_sw_fallback handling of other packets */
	if (ctx->tx_conf == TLS_SW) {
		kfree(ctx->tx.rec_seq);
		kfree(ctx->tx.iv);
		tls_sw_free_resources_tx(sk);
	}

	if (ctx->rx_conf == TLS_SW)
		tls_sw_free_resources_rx(sk);

#ifdef CONFIG_TLS_DEVICE
	if (ctx->rx_conf == TLS_HW)
		tls_device_offload_cleanup_rx(sk);

	if (ctx->tx_conf != TLS_HW && ctx->rx_conf != TLS_HW) {
#else
	{
#endif
		tls_ctx_free(ctx);
		ctx = NULL;
	}

skip_tx_cleanup:
	release_sock(sk);
	sk_proto_close(sk, timeout);
	/* free ctx for TLS_HW_RECORD, used by tcp_set_state
	 * for sk->sk_prot->unhash [tls_hw_unhash]
	 */
	if (free_ctx)
		tls_ctx_free(ctx);
}

static int do_tls_getsockopt_tx(struct sock *sk, char __user *optval,
				int __user *optlen)
{
	int rc = 0;
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_crypto_info *crypto_info;
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
	crypto_info = &ctx->crypto_send.info;

	if (!TLS_CRYPTO_INFO_READY(crypto_info)) {
		rc = -EBUSY;
		goto out;
	}

	if (len == sizeof(*crypto_info)) {
		if (copy_to_user(optval, crypto_info, sizeof(*crypto_info)))
			rc = -EFAULT;
		goto out;
	}

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128: {
		struct tls12_crypto_info_aes_gcm_128 *
		  crypto_info_aes_gcm_128 =
		  container_of(crypto_info,
			       struct tls12_crypto_info_aes_gcm_128,
			       info);

		if (len != sizeof(*crypto_info_aes_gcm_128)) {
			rc = -EINVAL;
			goto out;
		}
		lock_sock(sk);
		memcpy(crypto_info_aes_gcm_128->iv,
		       ctx->tx.iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
		       TLS_CIPHER_AES_GCM_128_IV_SIZE);
		memcpy(crypto_info_aes_gcm_128->rec_seq, ctx->tx.rec_seq,
		       TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE);
		release_sock(sk);
		if (copy_to_user(optval,
				 crypto_info_aes_gcm_128,
				 sizeof(*crypto_info_aes_gcm_128)))
			rc = -EFAULT;
		break;
	}
	default:
		rc = -EINVAL;
	}

out:
	return rc;
}

static int do_tls_getsockopt(struct sock *sk, int optname,
			     char __user *optval, int __user *optlen)
{
	int rc = 0;

	switch (optname) {
	case TLS_TX:
		rc = do_tls_getsockopt_tx(sk, optval, optlen);
		break;
	default:
		rc = -ENOPROTOOPT;
		break;
	}
	return rc;
}

static int tls_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (level != SOL_TLS)
		return ctx->getsockopt(sk, level, optname, optval, optlen);

	return do_tls_getsockopt(sk, optname, optval, optlen);
}

static int do_tls_setsockopt_conf(struct sock *sk, char __user *optval,
				  unsigned int optlen, int tx)
{
	struct tls_crypto_info *crypto_info;
	struct tls_context *ctx = tls_get_ctx(sk);
	int rc = 0;
	int conf;

	if (!optval || (optlen < sizeof(*crypto_info))) {
		rc = -EINVAL;
		goto out;
	}

	if (tx)
		crypto_info = &ctx->crypto_send.info;
	else
		crypto_info = &ctx->crypto_recv.info;

	/* Currently we don't support set crypto info more than one time */
	if (TLS_CRYPTO_INFO_READY(crypto_info)) {
		rc = -EBUSY;
		goto out;
	}

	rc = copy_from_user(crypto_info, optval, sizeof(*crypto_info));
	if (rc) {
		rc = -EFAULT;
		goto err_crypto_info;
	}

	/* check version */
	if (crypto_info->version != TLS_1_2_VERSION) {
		rc = -ENOTSUPP;
		goto err_crypto_info;
	}

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128: {
		if (optlen != sizeof(struct tls12_crypto_info_aes_gcm_128)) {
			rc = -EINVAL;
			goto err_crypto_info;
		}
		rc = copy_from_user(crypto_info + 1, optval + sizeof(*crypto_info),
				    optlen - sizeof(*crypto_info));
		if (rc) {
			rc = -EFAULT;
			goto err_crypto_info;
		}
		break;
	}
	default:
		rc = -EINVAL;
		goto err_crypto_info;
	}

	if (tx) {
#ifdef CONFIG_TLS_DEVICE
		rc = tls_set_device_offload(sk, ctx);
		conf = TLS_HW;
		if (rc) {
#else
		{
#endif
			rc = tls_set_sw_offload(sk, ctx, 1);
			conf = TLS_SW;
		}
	} else {
#ifdef CONFIG_TLS_DEVICE
		rc = tls_set_device_offload_rx(sk, ctx);
		conf = TLS_HW;
		if (rc) {
#else
		{
#endif
			rc = tls_set_sw_offload(sk, ctx, 0);
			conf = TLS_SW;
		}
	}

	if (rc)
		goto err_crypto_info;

	if (tx)
		ctx->tx_conf = conf;
	else
		ctx->rx_conf = conf;
	update_sk_prot(sk, ctx);
	if (tx) {
		ctx->sk_write_space = sk->sk_write_space;
		sk->sk_write_space = tls_write_space;
	} else {
		sk->sk_socket->ops = &tls_sw_proto_ops;
	}
	goto out;

err_crypto_info:
	memzero_explicit(crypto_info, sizeof(union tls_crypto_context));
out:
	return rc;
}

static int do_tls_setsockopt(struct sock *sk, int optname,
			     char __user *optval, unsigned int optlen)
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
	default:
		rc = -ENOPROTOOPT;
		break;
	}
	return rc;
}

static int tls_setsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct tls_context *ctx = tls_get_ctx(sk);

	if (level != SOL_TLS)
		return ctx->setsockopt(sk, level, optname, optval, optlen);

	return do_tls_setsockopt(sk, optname, optval, optlen);
}

static struct tls_context *create_ctx(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tls_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return NULL;

	icsk->icsk_ulp_data = ctx;
	ctx->setsockopt = sk->sk_prot->setsockopt;
	ctx->getsockopt = sk->sk_prot->getsockopt;
	ctx->sk_proto_close = sk->sk_prot->close;
	return ctx;
}

static int tls_hw_prot(struct sock *sk)
{
	struct tls_context *ctx;
	struct tls_device *dev;
	int rc = 0;

	mutex_lock(&device_mutex);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->feature && dev->feature(dev)) {
			ctx = create_ctx(sk);
			if (!ctx)
				goto out;

			ctx->hash = sk->sk_prot->hash;
			ctx->unhash = sk->sk_prot->unhash;
			ctx->sk_proto_close = sk->sk_prot->close;
			ctx->rx_conf = TLS_HW_RECORD;
			ctx->tx_conf = TLS_HW_RECORD;
			update_sk_prot(sk, ctx);
			rc = 1;
			break;
		}
	}
out:
	mutex_unlock(&device_mutex);
	return rc;
}

static void tls_hw_unhash(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_device *dev;

	mutex_lock(&device_mutex);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->unhash)
			dev->unhash(dev, sk);
	}
	mutex_unlock(&device_mutex);
	ctx->unhash(sk);
}

static int tls_hw_hash(struct sock *sk)
{
	struct tls_context *ctx = tls_get_ctx(sk);
	struct tls_device *dev;
	int err;

	err = ctx->hash(sk);
	mutex_lock(&device_mutex);
	list_for_each_entry(dev, &device_list, dev_list) {
		if (dev->hash)
			err |= dev->hash(dev, sk);
	}
	mutex_unlock(&device_mutex);

	if (err)
		tls_hw_unhash(sk);
	return err;
}

static void build_protos(struct proto prot[TLS_NUM_CONFIG][TLS_NUM_CONFIG],
			 struct proto *base)
{
	prot[TLS_BASE][TLS_BASE] = *base;
	prot[TLS_BASE][TLS_BASE].setsockopt	= tls_setsockopt;
	prot[TLS_BASE][TLS_BASE].getsockopt	= tls_getsockopt;
	prot[TLS_BASE][TLS_BASE].close		= tls_sk_proto_close;

	prot[TLS_SW][TLS_BASE] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_SW][TLS_BASE].sendmsg		= tls_sw_sendmsg;
	prot[TLS_SW][TLS_BASE].sendpage		= tls_sw_sendpage;

	prot[TLS_BASE][TLS_SW] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_BASE][TLS_SW].recvmsg		= tls_sw_recvmsg;
	prot[TLS_BASE][TLS_SW].close		= tls_sk_proto_close;

	prot[TLS_SW][TLS_SW] = prot[TLS_SW][TLS_BASE];
	prot[TLS_SW][TLS_SW].recvmsg	= tls_sw_recvmsg;
	prot[TLS_SW][TLS_SW].close	= tls_sk_proto_close;

#ifdef CONFIG_TLS_DEVICE
	prot[TLS_HW][TLS_BASE] = prot[TLS_BASE][TLS_BASE];
	prot[TLS_HW][TLS_BASE].sendmsg		= tls_device_sendmsg;
	prot[TLS_HW][TLS_BASE].sendpage		= tls_device_sendpage;

	prot[TLS_HW][TLS_SW] = prot[TLS_BASE][TLS_SW];
	prot[TLS_HW][TLS_SW].sendmsg		= tls_device_sendmsg;
	prot[TLS_HW][TLS_SW].sendpage		= tls_device_sendpage;

	prot[TLS_BASE][TLS_HW] = prot[TLS_BASE][TLS_SW];

	prot[TLS_SW][TLS_HW] = prot[TLS_SW][TLS_SW];

	prot[TLS_HW][TLS_HW] = prot[TLS_HW][TLS_SW];
#endif

	prot[TLS_HW_RECORD][TLS_HW_RECORD] = *base;
	prot[TLS_HW_RECORD][TLS_HW_RECORD].hash		= tls_hw_hash;
	prot[TLS_HW_RECORD][TLS_HW_RECORD].unhash	= tls_hw_unhash;
	prot[TLS_HW_RECORD][TLS_HW_RECORD].close	= tls_sk_proto_close;
}

static int tls_init(struct sock *sk)
{
	int ip_ver = sk->sk_family == AF_INET6 ? TLSV6 : TLSV4;
	struct tls_context *ctx;
	int rc = 0;

	if (tls_hw_prot(sk))
		goto out;

	/* The TLS ulp is currently supported only for TCP sockets
	 * in ESTABLISHED state.
	 * Supporting sockets in LISTEN state will require us
	 * to modify the accept implementation to clone rather then
	 * share the ulp context.
	 */
	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTSUPP;

	/* allocate tls context */
	ctx = create_ctx(sk);
	if (!ctx) {
		rc = -ENOMEM;
		goto out;
	}

	/* Build IPv6 TLS whenever the address of tcpv6	_prot changes */
	if (ip_ver == TLSV6 &&
	    unlikely(sk->sk_prot != smp_load_acquire(&saved_tcpv6_prot))) {
		mutex_lock(&tcpv6_prot_mutex);
		if (likely(sk->sk_prot != saved_tcpv6_prot)) {
			build_protos(tls_prots[TLSV6], sk->sk_prot);
			smp_store_release(&saved_tcpv6_prot, sk->sk_prot);
		}
		mutex_unlock(&tcpv6_prot_mutex);
	}

	ctx->tx_conf = TLS_BASE;
	ctx->rx_conf = TLS_BASE;
	update_sk_prot(sk, ctx);
out:
	return rc;
}

void tls_register_device(struct tls_device *device)
{
	mutex_lock(&device_mutex);
	list_add_tail(&device->dev_list, &device_list);
	mutex_unlock(&device_mutex);
}
EXPORT_SYMBOL(tls_register_device);

void tls_unregister_device(struct tls_device *device)
{
	mutex_lock(&device_mutex);
	list_del(&device->dev_list);
	mutex_unlock(&device_mutex);
}
EXPORT_SYMBOL(tls_unregister_device);

static struct tcp_ulp_ops tcp_tls_ulp_ops __read_mostly = {
	.name			= "tls",
	.uid			= TCP_ULP_TLS,
	.user_visible		= true,
	.owner			= THIS_MODULE,
	.init			= tls_init,
};

static int __init tls_register(void)
{
	build_protos(tls_prots[TLSV4], &tcp_prot);

	tls_sw_proto_ops = inet_stream_ops;
	tls_sw_proto_ops.poll = tls_sw_poll;
	tls_sw_proto_ops.splice_read = tls_sw_splice_read;

#ifdef CONFIG_TLS_DEVICE
	tls_device_init();
#endif
	tcp_register_ulp(&tcp_tls_ulp_ops);

	return 0;
}

static void __exit tls_unregister(void)
{
	tcp_unregister_ulp(&tcp_tls_ulp_ops);
#ifdef CONFIG_TLS_DEVICE
	tls_device_cleanup();
#endif
}

module_init(tls_register);
module_exit(tls_unregister);
