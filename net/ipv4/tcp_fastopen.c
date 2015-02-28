#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/tcp.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <net/inetpeer.h>
#include <net/tcp.h>

int sysctl_tcp_fastopen __read_mostly = TFO_CLIENT_ENABLE;

struct tcp_fastopen_context __rcu *tcp_fastopen_ctx;

static DEFINE_SPINLOCK(tcp_fastopen_ctx_lock);

void tcp_fastopen_init_key_once(bool publish)
{
	static u8 key[TCP_FASTOPEN_KEY_LENGTH];

	/* tcp_fastopen_reset_cipher publishes the new context
	 * atomically, so we allow this race happening here.
	 *
	 * All call sites of tcp_fastopen_cookie_gen also check
	 * for a valid cookie, so this is an acceptable risk.
	 */
	if (net_get_random_once(key, sizeof(key)) && publish)
		tcp_fastopen_reset_cipher(key, sizeof(key));
}

static void tcp_fastopen_ctx_free(struct rcu_head *head)
{
	struct tcp_fastopen_context *ctx =
	    container_of(head, struct tcp_fastopen_context, rcu);
	crypto_free_cipher(ctx->tfm);
	kfree(ctx);
}

int tcp_fastopen_reset_cipher(void *key, unsigned int len)
{
	int err;
	struct tcp_fastopen_context *ctx, *octx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->tfm = crypto_alloc_cipher("aes", 0, 0);

	if (IS_ERR(ctx->tfm)) {
		err = PTR_ERR(ctx->tfm);
error:		kfree(ctx);
		pr_err("TCP: TFO aes cipher alloc error: %d\n", err);
		return err;
	}
	err = crypto_cipher_setkey(ctx->tfm, key, len);
	if (err) {
		pr_err("TCP: TFO cipher key error: %d\n", err);
		crypto_free_cipher(ctx->tfm);
		goto error;
	}
	memcpy(ctx->key, key, len);

	spin_lock(&tcp_fastopen_ctx_lock);

	octx = rcu_dereference_protected(tcp_fastopen_ctx,
				lockdep_is_held(&tcp_fastopen_ctx_lock));
	rcu_assign_pointer(tcp_fastopen_ctx, ctx);
	spin_unlock(&tcp_fastopen_ctx_lock);

	if (octx)
		call_rcu(&octx->rcu, tcp_fastopen_ctx_free);
	return err;
}

static bool __tcp_fastopen_cookie_gen(const void *path,
				      struct tcp_fastopen_cookie *foc)
{
	struct tcp_fastopen_context *ctx;
	bool ok = false;

	tcp_fastopen_init_key_once(true);

	rcu_read_lock();
	ctx = rcu_dereference(tcp_fastopen_ctx);
	if (ctx) {
		crypto_cipher_encrypt_one(ctx->tfm, foc->val, path);
		foc->len = TCP_FASTOPEN_COOKIE_SIZE;
		ok = true;
	}
	rcu_read_unlock();
	return ok;
}

/* Generate the fastopen cookie by doing aes128 encryption on both
 * the source and destination addresses. Pad 0s for IPv4 or IPv4-mapped-IPv6
 * addresses. For the longer IPv6 addresses use CBC-MAC.
 *
 * XXX (TFO) - refactor when TCP_FASTOPEN_COOKIE_SIZE != AES_BLOCK_SIZE.
 */
static bool tcp_fastopen_cookie_gen(struct request_sock *req,
				    struct sk_buff *syn,
				    struct tcp_fastopen_cookie *foc)
{
	if (req->rsk_ops->family == AF_INET) {
		const struct iphdr *iph = ip_hdr(syn);

		__be32 path[4] = { iph->saddr, iph->daddr, 0, 0 };
		return __tcp_fastopen_cookie_gen(path, foc);
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (req->rsk_ops->family == AF_INET6) {
		const struct ipv6hdr *ip6h = ipv6_hdr(syn);
		struct tcp_fastopen_cookie tmp;

		if (__tcp_fastopen_cookie_gen(&ip6h->saddr, &tmp)) {
			struct in6_addr *buf = (struct in6_addr *) tmp.val;
			int i;

			for (i = 0; i < 4; i++)
				buf->s6_addr32[i] ^= ip6h->daddr.s6_addr32[i];
			return __tcp_fastopen_cookie_gen(buf, foc);
		}
	}
#endif
	return false;
}

static bool tcp_fastopen_create_child(struct sock *sk,
				      struct sk_buff *skb,
				      struct dst_entry *dst,
				      struct request_sock *req)
{
	struct tcp_sock *tp;
	struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;
	struct sock *child;
	u32 end_seq;

	req->num_retrans = 0;
	req->num_timeout = 0;
	req->sk = NULL;

	child = inet_csk(sk)->icsk_af_ops->syn_recv_sock(sk, skb, req, NULL);
	if (child == NULL)
		return false;

	spin_lock(&queue->fastopenq->lock);
	queue->fastopenq->qlen++;
	spin_unlock(&queue->fastopenq->lock);

	/* Initialize the child socket. Have to fix some values to take
	 * into account the child is a Fast Open socket and is created
	 * only out of the bits carried in the SYN packet.
	 */
	tp = tcp_sk(child);

	tp->fastopen_rsk = req;
	/* Do a hold on the listner sk so that if the listener is being
	 * closed, the child that has been accepted can live on and still
	 * access listen_lock.
	 */
	sock_hold(sk);
	tcp_rsk(req)->listener = sk;

	/* RFC1323: The window in SYN & SYN/ACK segments is never
	 * scaled. So correct it appropriately.
	 */
	tp->snd_wnd = ntohs(tcp_hdr(skb)->window);

	/* Activate the retrans timer so that SYNACK can be retransmitted.
	 * The request socket is not added to the SYN table of the parent
	 * because it's been added to the accept queue directly.
	 */
	inet_csk_reset_xmit_timer(child, ICSK_TIME_RETRANS,
				  TCP_TIMEOUT_INIT, TCP_RTO_MAX);

	/* Add the child socket directly into the accept queue */
	inet_csk_reqsk_queue_add(sk, req, child);

	/* Now finish processing the fastopen child socket. */
	inet_csk(child)->icsk_af_ops->rebuild_header(child);
	tcp_init_congestion_control(child);
	tcp_mtup_init(child);
	tcp_init_metrics(child);
	tcp_init_buffer_space(child);

	/* Queue the data carried in the SYN packet. We need to first
	 * bump skb's refcnt because the caller will attempt to free it.
	 * Note that IPv6 might also have used skb_get() trick
	 * in tcp_v6_conn_request() to keep this SYN around (treq->pktopts)
	 * So we need to eventually get a clone of the packet,
	 * before inserting it in sk_receive_queue.
	 *
	 * XXX (TFO) - we honor a zero-payload TFO request for now,
	 * (any reason not to?) but no need to queue the skb since
	 * there is no data. How about SYN+FIN?
	 */
	end_seq = TCP_SKB_CB(skb)->end_seq;
	if (end_seq != TCP_SKB_CB(skb)->seq + 1) {
		struct sk_buff *skb2;

		if (unlikely(skb_shared(skb)))
			skb2 = skb_clone(skb, GFP_ATOMIC);
		else
			skb2 = skb_get(skb);

		if (likely(skb2)) {
			skb_dst_drop(skb2);
			__skb_pull(skb2, tcp_hdrlen(skb));
			skb_set_owner_r(skb2, child);
			__skb_queue_tail(&child->sk_receive_queue, skb2);
			tp->syn_data_acked = 1;
		} else {
			end_seq = TCP_SKB_CB(skb)->seq + 1;
		}
	}
	tcp_rsk(req)->rcv_nxt = tp->rcv_nxt = end_seq;
	sk->sk_data_ready(sk);
	bh_unlock_sock(child);
	sock_put(child);
	WARN_ON(req->sk == NULL);
	return true;
}
EXPORT_SYMBOL(tcp_fastopen_create_child);

static bool tcp_fastopen_queue_check(struct sock *sk)
{
	struct fastopen_queue *fastopenq;

	/* Make sure the listener has enabled fastopen, and we don't
	 * exceed the max # of pending TFO requests allowed before trying
	 * to validating the cookie in order to avoid burning CPU cycles
	 * unnecessarily.
	 *
	 * XXX (TFO) - The implication of checking the max_qlen before
	 * processing a cookie request is that clients can't differentiate
	 * between qlen overflow causing Fast Open to be disabled
	 * temporarily vs a server not supporting Fast Open at all.
	 */
	fastopenq = inet_csk(sk)->icsk_accept_queue.fastopenq;
	if (fastopenq == NULL || fastopenq->max_qlen == 0)
		return false;

	if (fastopenq->qlen >= fastopenq->max_qlen) {
		struct request_sock *req1;
		spin_lock(&fastopenq->lock);
		req1 = fastopenq->rskq_rst_head;
		if ((req1 == NULL) || time_after(req1->expires, jiffies)) {
			spin_unlock(&fastopenq->lock);
			NET_INC_STATS_BH(sock_net(sk),
					 LINUX_MIB_TCPFASTOPENLISTENOVERFLOW);
			return false;
		}
		fastopenq->rskq_rst_head = req1->dl_next;
		fastopenq->qlen--;
		spin_unlock(&fastopenq->lock);
		reqsk_free(req1);
	}
	return true;
}

/* Returns true if we should perform Fast Open on the SYN. The cookie (foc)
 * may be updated and return the client in the SYN-ACK later. E.g., Fast Open
 * cookie request (foc->len == 0).
 */
bool tcp_try_fastopen(struct sock *sk, struct sk_buff *skb,
		      struct request_sock *req,
		      struct tcp_fastopen_cookie *foc,
		      struct dst_entry *dst)
{
	struct tcp_fastopen_cookie valid_foc = { .len = -1 };
	bool syn_data = TCP_SKB_CB(skb)->end_seq != TCP_SKB_CB(skb)->seq + 1;

	if (foc->len == 0) /* Client requests a cookie */
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPFASTOPENCOOKIEREQD);

	if (!((sysctl_tcp_fastopen & TFO_SERVER_ENABLE) &&
	      (syn_data || foc->len >= 0) &&
	      tcp_fastopen_queue_check(sk))) {
		foc->len = -1;
		return false;
	}

	if (syn_data && (sysctl_tcp_fastopen & TFO_SERVER_COOKIE_NOT_REQD))
		goto fastopen;

	if (foc->len >= 0 &&  /* Client presents or requests a cookie */
	    tcp_fastopen_cookie_gen(req, skb, &valid_foc) &&
	    foc->len == TCP_FASTOPEN_COOKIE_SIZE &&
	    foc->len == valid_foc.len &&
	    !memcmp(foc->val, valid_foc.val, foc->len)) {
		/* Cookie is valid. Create a (full) child socket to accept
		 * the data in SYN before returning a SYN-ACK to ack the
		 * data. If we fail to create the socket, fall back and
		 * ack the ISN only but includes the same cookie.
		 *
		 * Note: Data-less SYN with valid cookie is allowed to send
		 * data in SYN_RECV state.
		 */
fastopen:
		if (tcp_fastopen_create_child(sk, skb, dst, req)) {
			foc->len = -1;
			NET_INC_STATS_BH(sock_net(sk),
					 LINUX_MIB_TCPFASTOPENPASSIVE);
			return true;
		}
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPFASTOPENPASSIVEFAIL);
	} else if (foc->len > 0) /* Client presents an invalid cookie */
		NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPFASTOPENPASSIVEFAIL);

	*foc = valid_foc;
	return false;
}
EXPORT_SYMBOL(tcp_try_fastopen);
