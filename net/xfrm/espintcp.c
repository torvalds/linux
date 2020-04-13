// SPDX-License-Identifier: GPL-2.0
#include <net/tcp.h>
#include <net/strparser.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <net/espintcp.h>
#include <linux/skmsg.h>
#include <net/inet_common.h>

static void handle_nonesp(struct espintcp_ctx *ctx, struct sk_buff *skb,
			  struct sock *sk)
{
	if (atomic_read(&sk->sk_rmem_alloc) >= sk->sk_rcvbuf ||
	    !sk_rmem_schedule(sk, skb, skb->truesize)) {
		kfree_skb(skb);
		return;
	}

	skb_set_owner_r(skb, sk);

	memset(skb->cb, 0, sizeof(skb->cb));
	skb_queue_tail(&ctx->ike_queue, skb);
	ctx->saved_data_ready(sk);
}

static void handle_esp(struct sk_buff *skb, struct sock *sk)
{
	skb_reset_transport_header(skb);
	memset(skb->cb, 0, sizeof(skb->cb));

	rcu_read_lock();
	skb->dev = dev_get_by_index_rcu(sock_net(sk), skb->skb_iif);
	local_bh_disable();
	xfrm4_rcv_encap(skb, IPPROTO_ESP, 0, TCP_ENCAP_ESPINTCP);
	local_bh_enable();
	rcu_read_unlock();
}

static void espintcp_rcv(struct strparser *strp, struct sk_buff *skb)
{
	struct espintcp_ctx *ctx = container_of(strp, struct espintcp_ctx,
						strp);
	struct strp_msg *rxm = strp_msg(skb);
	u32 nonesp_marker;
	int err;

	err = skb_copy_bits(skb, rxm->offset + 2, &nonesp_marker,
			    sizeof(nonesp_marker));
	if (err < 0) {
		kfree_skb(skb);
		return;
	}

	/* remove header, leave non-ESP marker/SPI */
	if (!__pskb_pull(skb, rxm->offset + 2)) {
		kfree_skb(skb);
		return;
	}

	if (pskb_trim(skb, rxm->full_len - 2) != 0) {
		kfree_skb(skb);
		return;
	}

	if (nonesp_marker == 0)
		handle_nonesp(ctx, skb, strp->sk);
	else
		handle_esp(skb, strp->sk);
}

static int espintcp_parse(struct strparser *strp, struct sk_buff *skb)
{
	struct strp_msg *rxm = strp_msg(skb);
	__be16 blen;
	u16 len;
	int err;

	if (skb->len < rxm->offset + 2)
		return 0;

	err = skb_copy_bits(skb, rxm->offset, &blen, sizeof(blen));
	if (err < 0)
		return err;

	len = be16_to_cpu(blen);
	if (len < 6)
		return -EINVAL;

	return len;
}

static int espintcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			    int nonblock, int flags, int *addr_len)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct sk_buff *skb;
	int err = 0;
	int copied;
	int off = 0;

	flags |= nonblock ? MSG_DONTWAIT : 0;

	skb = __skb_recv_datagram(sk, &ctx->ike_queue, flags, &off, &err);
	if (!skb)
		return err;

	copied = len;
	if (copied > skb->len)
		copied = skb->len;
	else if (copied < skb->len)
		msg->msg_flags |= MSG_TRUNC;

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (unlikely(err)) {
		kfree_skb(skb);
		return err;
	}

	if (flags & MSG_TRUNC)
		copied = skb->len;
	kfree_skb(skb);
	return copied;
}

int espintcp_queue_out(struct sock *sk, struct sk_buff *skb)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);

	if (skb_queue_len(&ctx->out_queue) >= netdev_max_backlog)
		return -ENOBUFS;

	__skb_queue_tail(&ctx->out_queue, skb);

	return 0;
}
EXPORT_SYMBOL_GPL(espintcp_queue_out);

/* espintcp length field is 2B and length includes the length field's size */
#define MAX_ESPINTCP_MSG (((1 << 16) - 1) - 2)

static int espintcp_sendskb_locked(struct sock *sk, struct espintcp_msg *emsg,
				   int flags)
{
	do {
		int ret;

		ret = skb_send_sock_locked(sk, emsg->skb,
					   emsg->offset, emsg->len);
		if (ret < 0)
			return ret;

		emsg->len -= ret;
		emsg->offset += ret;
	} while (emsg->len > 0);

	kfree_skb(emsg->skb);
	memset(emsg, 0, sizeof(*emsg));

	return 0;
}

static int espintcp_sendskmsg_locked(struct sock *sk,
				     struct espintcp_msg *emsg, int flags)
{
	struct sk_msg *skmsg = &emsg->skmsg;
	struct scatterlist *sg;
	int done = 0;
	int ret;

	flags |= MSG_SENDPAGE_NOTLAST;
	sg = &skmsg->sg.data[skmsg->sg.start];
	do {
		size_t size = sg->length - emsg->offset;
		int offset = sg->offset + emsg->offset;
		struct page *p;

		emsg->offset = 0;

		if (sg_is_last(sg))
			flags &= ~MSG_SENDPAGE_NOTLAST;

		p = sg_page(sg);
retry:
		ret = do_tcp_sendpages(sk, p, offset, size, flags);
		if (ret < 0) {
			emsg->offset = offset - sg->offset;
			skmsg->sg.start += done;
			return ret;
		}

		if (ret != size) {
			offset += ret;
			size -= ret;
			goto retry;
		}

		done++;
		put_page(p);
		sk_mem_uncharge(sk, sg->length);
		sg = sg_next(sg);
	} while (sg);

	memset(emsg, 0, sizeof(*emsg));

	return 0;
}

static int espintcp_push_msgs(struct sock *sk)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct espintcp_msg *emsg = &ctx->partial;
	int err;

	if (!emsg->len)
		return 0;

	if (ctx->tx_running)
		return -EAGAIN;
	ctx->tx_running = 1;

	if (emsg->skb)
		err = espintcp_sendskb_locked(sk, emsg, 0);
	else
		err = espintcp_sendskmsg_locked(sk, emsg, 0);
	if (err == -EAGAIN) {
		ctx->tx_running = 0;
		return 0;
	}
	if (!err)
		memset(emsg, 0, sizeof(*emsg));

	ctx->tx_running = 0;

	return err;
}

int espintcp_push_skb(struct sock *sk, struct sk_buff *skb)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct espintcp_msg *emsg = &ctx->partial;
	unsigned int len;
	int offset;

	if (sk->sk_state != TCP_ESTABLISHED) {
		kfree_skb(skb);
		return -ECONNRESET;
	}

	offset = skb_transport_offset(skb);
	len = skb->len - offset;

	espintcp_push_msgs(sk);

	if (emsg->len) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	skb_set_owner_w(skb, sk);

	emsg->offset = offset;
	emsg->len = len;
	emsg->skb = skb;

	espintcp_push_msgs(sk);

	return 0;
}
EXPORT_SYMBOL_GPL(espintcp_push_skb);

static int espintcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	long timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct espintcp_msg *emsg = &ctx->partial;
	struct iov_iter pfx_iter;
	struct kvec pfx_iov = {};
	size_t msglen = size + 2;
	char buf[2] = {0};
	int err, end;

	if (msg->msg_flags)
		return -EOPNOTSUPP;

	if (size > MAX_ESPINTCP_MSG)
		return -EMSGSIZE;

	if (msg->msg_controllen)
		return -EOPNOTSUPP;

	lock_sock(sk);

	err = espintcp_push_msgs(sk);
	if (err < 0) {
		err = -ENOBUFS;
		goto unlock;
	}

	sk_msg_init(&emsg->skmsg);
	while (1) {
		/* only -ENOMEM is possible since we don't coalesce */
		err = sk_msg_alloc(sk, &emsg->skmsg, msglen, 0);
		if (!err)
			break;

		err = sk_stream_wait_memory(sk, &timeo);
		if (err)
			goto fail;
	}

	*((__be16 *)buf) = cpu_to_be16(msglen);
	pfx_iov.iov_base = buf;
	pfx_iov.iov_len = sizeof(buf);
	iov_iter_kvec(&pfx_iter, WRITE, &pfx_iov, 1, pfx_iov.iov_len);

	err = sk_msg_memcopy_from_iter(sk, &pfx_iter, &emsg->skmsg,
				       pfx_iov.iov_len);
	if (err < 0)
		goto fail;

	err = sk_msg_memcopy_from_iter(sk, &msg->msg_iter, &emsg->skmsg, size);
	if (err < 0)
		goto fail;

	end = emsg->skmsg.sg.end;
	emsg->len = size;
	sk_msg_iter_var_prev(end);
	sg_mark_end(sk_msg_elem(&emsg->skmsg, end));

	tcp_rate_check_app_limited(sk);

	err = espintcp_push_msgs(sk);
	/* this message could be partially sent, keep it */
	if (err < 0)
		goto unlock;
	release_sock(sk);

	return size;

fail:
	sk_msg_free(sk, &emsg->skmsg);
	memset(emsg, 0, sizeof(*emsg));
unlock:
	release_sock(sk);
	return err;
}

static struct proto espintcp_prot __ro_after_init;
static struct proto_ops espintcp_ops __ro_after_init;

static void espintcp_data_ready(struct sock *sk)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);

	strp_data_ready(&ctx->strp);
}

static void espintcp_tx_work(struct work_struct *work)
{
	struct espintcp_ctx *ctx = container_of(work,
						struct espintcp_ctx, work);
	struct sock *sk = ctx->strp.sk;

	lock_sock(sk);
	if (!ctx->tx_running)
		espintcp_push_msgs(sk);
	release_sock(sk);
}

static void espintcp_write_space(struct sock *sk)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);

	schedule_work(&ctx->work);
	ctx->saved_write_space(sk);
}

static void espintcp_destruct(struct sock *sk)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);

	kfree(ctx);
}

bool tcp_is_ulp_esp(struct sock *sk)
{
	return sk->sk_prot == &espintcp_prot;
}
EXPORT_SYMBOL_GPL(tcp_is_ulp_esp);

static int espintcp_init_sk(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct strp_callbacks cb = {
		.rcv_msg = espintcp_rcv,
		.parse_msg = espintcp_parse,
	};
	struct espintcp_ctx *ctx;
	int err;

	/* sockmap is not compatible with espintcp */
	if (sk->sk_user_data)
		return -EBUSY;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	err = strp_init(&ctx->strp, sk, &cb);
	if (err)
		goto free;

	__sk_dst_reset(sk);

	strp_check_rcv(&ctx->strp);
	skb_queue_head_init(&ctx->ike_queue);
	skb_queue_head_init(&ctx->out_queue);
	sk->sk_prot = &espintcp_prot;
	sk->sk_socket->ops = &espintcp_ops;
	ctx->saved_data_ready = sk->sk_data_ready;
	ctx->saved_write_space = sk->sk_write_space;
	sk->sk_data_ready = espintcp_data_ready;
	sk->sk_write_space = espintcp_write_space;
	sk->sk_destruct = espintcp_destruct;
	rcu_assign_pointer(icsk->icsk_ulp_data, ctx);
	INIT_WORK(&ctx->work, espintcp_tx_work);

	/* avoid using task_frag */
	sk->sk_allocation = GFP_ATOMIC;

	return 0;

free:
	kfree(ctx);
	return err;
}

static void espintcp_release(struct sock *sk)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct sk_buff_head queue;
	struct sk_buff *skb;

	__skb_queue_head_init(&queue);
	skb_queue_splice_init(&ctx->out_queue, &queue);

	while ((skb = __skb_dequeue(&queue)))
		espintcp_push_skb(sk, skb);

	tcp_release_cb(sk);
}

static void espintcp_close(struct sock *sk, long timeout)
{
	struct espintcp_ctx *ctx = espintcp_getctx(sk);
	struct espintcp_msg *emsg = &ctx->partial;

	strp_stop(&ctx->strp);

	sk->sk_prot = &tcp_prot;
	barrier();

	cancel_work_sync(&ctx->work);
	strp_done(&ctx->strp);

	skb_queue_purge(&ctx->out_queue);
	skb_queue_purge(&ctx->ike_queue);

	if (emsg->len) {
		if (emsg->skb)
			kfree_skb(emsg->skb);
		else
			sk_msg_free(sk, &emsg->skmsg);
	}

	tcp_close(sk, timeout);
}

static __poll_t espintcp_poll(struct file *file, struct socket *sock,
			      poll_table *wait)
{
	__poll_t mask = datagram_poll(file, sock, wait);
	struct sock *sk = sock->sk;
	struct espintcp_ctx *ctx = espintcp_getctx(sk);

	if (!skb_queue_empty(&ctx->ike_queue))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static struct tcp_ulp_ops espintcp_ulp __read_mostly = {
	.name = "espintcp",
	.owner = THIS_MODULE,
	.init = espintcp_init_sk,
};

void __init espintcp_init(void)
{
	memcpy(&espintcp_prot, &tcp_prot, sizeof(tcp_prot));
	memcpy(&espintcp_ops, &inet_stream_ops, sizeof(inet_stream_ops));
	espintcp_prot.sendmsg = espintcp_sendmsg;
	espintcp_prot.recvmsg = espintcp_recvmsg;
	espintcp_prot.close = espintcp_close;
	espintcp_prot.release_cb = espintcp_release;
	espintcp_ops.poll = espintcp_poll;

	tcp_register_ulp(&espintcp_ulp);
}
