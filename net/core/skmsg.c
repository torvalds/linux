// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017 - 2018 Covalent IO, Inc. http://covalent.io */

#include <linux/skmsg.h>
#include <linux/skbuff.h>
#include <linux/scatterlist.h>

#include <net/sock.h>
#include <net/tcp.h>

static bool sk_msg_try_coalesce_ok(struct sk_msg *msg, int elem_first_coalesce)
{
	if (msg->sg.end > msg->sg.start &&
	    elem_first_coalesce < msg->sg.end)
		return true;

	if (msg->sg.end < msg->sg.start &&
	    (elem_first_coalesce > msg->sg.start ||
	     elem_first_coalesce < msg->sg.end))
		return true;

	return false;
}

int sk_msg_alloc(struct sock *sk, struct sk_msg *msg, int len,
		 int elem_first_coalesce)
{
	struct page_frag *pfrag = sk_page_frag(sk);
	int ret = 0;

	len -= msg->sg.size;
	while (len > 0) {
		struct scatterlist *sge;
		u32 orig_offset;
		int use, i;

		if (!sk_page_frag_refill(sk, pfrag))
			return -ENOMEM;

		orig_offset = pfrag->offset;
		use = min_t(int, len, pfrag->size - orig_offset);
		if (!sk_wmem_schedule(sk, use))
			return -ENOMEM;

		i = msg->sg.end;
		sk_msg_iter_var_prev(i);
		sge = &msg->sg.data[i];

		if (sk_msg_try_coalesce_ok(msg, elem_first_coalesce) &&
		    sg_page(sge) == pfrag->page &&
		    sge->offset + sge->length == orig_offset) {
			sge->length += use;
		} else {
			if (sk_msg_full(msg)) {
				ret = -ENOSPC;
				break;
			}

			sge = &msg->sg.data[msg->sg.end];
			sg_unmark_end(sge);
			sg_set_page(sge, pfrag->page, use, orig_offset);
			get_page(pfrag->page);
			sk_msg_iter_next(msg, end);
		}

		sk_mem_charge(sk, use);
		msg->sg.size += use;
		pfrag->offset += use;
		len -= use;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sk_msg_alloc);

int sk_msg_clone(struct sock *sk, struct sk_msg *dst, struct sk_msg *src,
		 u32 off, u32 len)
{
	int i = src->sg.start;
	struct scatterlist *sge = sk_msg_elem(src, i);
	struct scatterlist *sgd = NULL;
	u32 sge_len, sge_off;

	while (off) {
		if (sge->length > off)
			break;
		off -= sge->length;
		sk_msg_iter_var_next(i);
		if (i == src->sg.end && off)
			return -ENOSPC;
		sge = sk_msg_elem(src, i);
	}

	while (len) {
		sge_len = sge->length - off;
		if (sge_len > len)
			sge_len = len;

		if (dst->sg.end)
			sgd = sk_msg_elem(dst, dst->sg.end - 1);

		if (sgd &&
		    (sg_page(sge) == sg_page(sgd)) &&
		    (sg_virt(sge) + off == sg_virt(sgd) + sgd->length)) {
			sgd->length += sge_len;
			dst->sg.size += sge_len;
		} else if (!sk_msg_full(dst)) {
			sge_off = sge->offset + off;
			sk_msg_page_add(dst, sg_page(sge), sge_len, sge_off);
		} else {
			return -ENOSPC;
		}

		off = 0;
		len -= sge_len;
		sk_mem_charge(sk, sge_len);
		sk_msg_iter_var_next(i);
		if (i == src->sg.end && len)
			return -ENOSPC;
		sge = sk_msg_elem(src, i);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sk_msg_clone);

void sk_msg_return_zero(struct sock *sk, struct sk_msg *msg, int bytes)
{
	int i = msg->sg.start;

	do {
		struct scatterlist *sge = sk_msg_elem(msg, i);

		if (bytes < sge->length) {
			sge->length -= bytes;
			sge->offset += bytes;
			sk_mem_uncharge(sk, bytes);
			break;
		}

		sk_mem_uncharge(sk, sge->length);
		bytes -= sge->length;
		sge->length = 0;
		sge->offset = 0;
		sk_msg_iter_var_next(i);
	} while (bytes && i != msg->sg.end);
	msg->sg.start = i;
}
EXPORT_SYMBOL_GPL(sk_msg_return_zero);

void sk_msg_return(struct sock *sk, struct sk_msg *msg, int bytes)
{
	int i = msg->sg.start;

	do {
		struct scatterlist *sge = &msg->sg.data[i];
		int uncharge = (bytes < sge->length) ? bytes : sge->length;

		sk_mem_uncharge(sk, uncharge);
		bytes -= uncharge;
		sk_msg_iter_var_next(i);
	} while (i != msg->sg.end);
}
EXPORT_SYMBOL_GPL(sk_msg_return);

static int sk_msg_free_elem(struct sock *sk, struct sk_msg *msg, u32 i,
			    bool charge)
{
	struct scatterlist *sge = sk_msg_elem(msg, i);
	u32 len = sge->length;

	if (charge)
		sk_mem_uncharge(sk, len);
	if (!msg->skb)
		put_page(sg_page(sge));
	memset(sge, 0, sizeof(*sge));
	return len;
}

static int __sk_msg_free(struct sock *sk, struct sk_msg *msg, u32 i,
			 bool charge)
{
	struct scatterlist *sge = sk_msg_elem(msg, i);
	int freed = 0;

	while (msg->sg.size) {
		msg->sg.size -= sge->length;
		freed += sk_msg_free_elem(sk, msg, i, charge);
		sk_msg_iter_var_next(i);
		sk_msg_check_to_free(msg, i, msg->sg.size);
		sge = sk_msg_elem(msg, i);
	}
	if (msg->skb)
		consume_skb(msg->skb);
	sk_msg_init(msg);
	return freed;
}

int sk_msg_free_nocharge(struct sock *sk, struct sk_msg *msg)
{
	return __sk_msg_free(sk, msg, msg->sg.start, false);
}
EXPORT_SYMBOL_GPL(sk_msg_free_nocharge);

int sk_msg_free(struct sock *sk, struct sk_msg *msg)
{
	return __sk_msg_free(sk, msg, msg->sg.start, true);
}
EXPORT_SYMBOL_GPL(sk_msg_free);

static void __sk_msg_free_partial(struct sock *sk, struct sk_msg *msg,
				  u32 bytes, bool charge)
{
	struct scatterlist *sge;
	u32 i = msg->sg.start;

	while (bytes) {
		sge = sk_msg_elem(msg, i);
		if (!sge->length)
			break;
		if (bytes < sge->length) {
			if (charge)
				sk_mem_uncharge(sk, bytes);
			sge->length -= bytes;
			sge->offset += bytes;
			msg->sg.size -= bytes;
			break;
		}

		msg->sg.size -= sge->length;
		bytes -= sge->length;
		sk_msg_free_elem(sk, msg, i, charge);
		sk_msg_iter_var_next(i);
		sk_msg_check_to_free(msg, i, bytes);
	}
	msg->sg.start = i;
}

void sk_msg_free_partial(struct sock *sk, struct sk_msg *msg, u32 bytes)
{
	__sk_msg_free_partial(sk, msg, bytes, true);
}
EXPORT_SYMBOL_GPL(sk_msg_free_partial);

void sk_msg_free_partial_nocharge(struct sock *sk, struct sk_msg *msg,
				  u32 bytes)
{
	__sk_msg_free_partial(sk, msg, bytes, false);
}

void sk_msg_trim(struct sock *sk, struct sk_msg *msg, int len)
{
	int trim = msg->sg.size - len;
	u32 i = msg->sg.end;

	if (trim <= 0) {
		WARN_ON(trim < 0);
		return;
	}

	sk_msg_iter_var_prev(i);
	msg->sg.size = len;
	while (msg->sg.data[i].length &&
	       trim >= msg->sg.data[i].length) {
		trim -= msg->sg.data[i].length;
		sk_msg_free_elem(sk, msg, i, true);
		sk_msg_iter_var_prev(i);
		if (!trim)
			goto out;
	}

	msg->sg.data[i].length -= trim;
	sk_mem_uncharge(sk, trim);
out:
	/* If we trim data before curr pointer update copybreak and current
	 * so that any future copy operations start at new copy location.
	 * However trimed data that has not yet been used in a copy op
	 * does not require an update.
	 */
	if (msg->sg.curr >= i) {
		msg->sg.curr = i;
		msg->sg.copybreak = msg->sg.data[i].length;
	}
	sk_msg_iter_var_next(i);
	msg->sg.end = i;
}
EXPORT_SYMBOL_GPL(sk_msg_trim);

int sk_msg_zerocopy_from_iter(struct sock *sk, struct iov_iter *from,
			      struct sk_msg *msg, u32 bytes)
{
	int i, maxpages, ret = 0, num_elems = sk_msg_elem_used(msg);
	const int to_max_pages = MAX_MSG_FRAGS;
	struct page *pages[MAX_MSG_FRAGS];
	ssize_t orig, copied, use, offset;

	orig = msg->sg.size;
	while (bytes > 0) {
		i = 0;
		maxpages = to_max_pages - num_elems;
		if (maxpages == 0) {
			ret = -EFAULT;
			goto out;
		}

		copied = iov_iter_get_pages(from, pages, bytes, maxpages,
					    &offset);
		if (copied <= 0) {
			ret = -EFAULT;
			goto out;
		}

		iov_iter_advance(from, copied);
		bytes -= copied;
		msg->sg.size += copied;

		while (copied) {
			use = min_t(int, copied, PAGE_SIZE - offset);
			sg_set_page(&msg->sg.data[msg->sg.end],
				    pages[i], use, offset);
			sg_unmark_end(&msg->sg.data[msg->sg.end]);
			sk_mem_charge(sk, use);

			offset = 0;
			copied -= use;
			sk_msg_iter_next(msg, end);
			num_elems++;
			i++;
		}
		/* When zerocopy is mixed with sk_msg_*copy* operations we
		 * may have a copybreak set in this case clear and prefer
		 * zerocopy remainder when possible.
		 */
		msg->sg.copybreak = 0;
		msg->sg.curr = msg->sg.end;
	}
out:
	/* Revert iov_iter updates, msg will need to use 'trim' later if it
	 * also needs to be cleared.
	 */
	if (ret)
		iov_iter_revert(from, msg->sg.size - orig);
	return ret;
}
EXPORT_SYMBOL_GPL(sk_msg_zerocopy_from_iter);

int sk_msg_memcopy_from_iter(struct sock *sk, struct iov_iter *from,
			     struct sk_msg *msg, u32 bytes)
{
	int ret = -ENOSPC, i = msg->sg.curr;
	struct scatterlist *sge;
	u32 copy, buf_size;
	void *to;

	do {
		sge = sk_msg_elem(msg, i);
		/* This is possible if a trim operation shrunk the buffer */
		if (msg->sg.copybreak >= sge->length) {
			msg->sg.copybreak = 0;
			sk_msg_iter_var_next(i);
			if (i == msg->sg.end)
				break;
			sge = sk_msg_elem(msg, i);
		}

		buf_size = sge->length - msg->sg.copybreak;
		copy = (buf_size > bytes) ? bytes : buf_size;
		to = sg_virt(sge) + msg->sg.copybreak;
		msg->sg.copybreak += copy;
		if (sk->sk_route_caps & NETIF_F_NOCACHE_COPY)
			ret = copy_from_iter_nocache(to, copy, from);
		else
			ret = copy_from_iter(to, copy, from);
		if (ret != copy) {
			ret = -EFAULT;
			goto out;
		}
		bytes -= copy;
		if (!bytes)
			break;
		msg->sg.copybreak = 0;
		sk_msg_iter_var_next(i);
	} while (i != msg->sg.end);
out:
	msg->sg.curr = i;
	return ret;
}
EXPORT_SYMBOL_GPL(sk_msg_memcopy_from_iter);

static int sk_psock_skb_ingress(struct sk_psock *psock, struct sk_buff *skb)
{
	struct sock *sk = psock->sk;
	int copied = 0, num_sge;
	struct sk_msg *msg;

	msg = kzalloc(sizeof(*msg), __GFP_NOWARN | GFP_ATOMIC);
	if (unlikely(!msg))
		return -EAGAIN;
	if (!sk_rmem_schedule(sk, skb, skb->len)) {
		kfree(msg);
		return -EAGAIN;
	}

	sk_msg_init(msg);
	num_sge = skb_to_sgvec(skb, msg->sg.data, 0, skb->len);
	if (unlikely(num_sge < 0)) {
		kfree(msg);
		return num_sge;
	}

	sk_mem_charge(sk, skb->len);
	copied = skb->len;
	msg->sg.start = 0;
	msg->sg.end = num_sge == MAX_MSG_FRAGS ? 0 : num_sge;
	msg->skb = skb;

	sk_psock_queue_msg(psock, msg);
	sk_psock_data_ready(sk, psock);
	return copied;
}

static int sk_psock_handle_skb(struct sk_psock *psock, struct sk_buff *skb,
			       u32 off, u32 len, bool ingress)
{
	if (ingress)
		return sk_psock_skb_ingress(psock, skb);
	else
		return skb_send_sock_locked(psock->sk, skb, off, len);
}

static void sk_psock_backlog(struct work_struct *work)
{
	struct sk_psock *psock = container_of(work, struct sk_psock, work);
	struct sk_psock_work_state *state = &psock->work_state;
	struct sk_buff *skb;
	bool ingress;
	u32 len, off;
	int ret;

	/* Lock sock to avoid losing sk_socket during loop. */
	lock_sock(psock->sk);
	if (state->skb) {
		skb = state->skb;
		len = state->len;
		off = state->off;
		state->skb = NULL;
		goto start;
	}

	while ((skb = skb_dequeue(&psock->ingress_skb))) {
		len = skb->len;
		off = 0;
start:
		ingress = tcp_skb_bpf_ingress(skb);
		do {
			ret = -EIO;
			if (likely(psock->sk->sk_socket))
				ret = sk_psock_handle_skb(psock, skb, off,
							  len, ingress);
			if (ret <= 0) {
				if (ret == -EAGAIN) {
					state->skb = skb;
					state->len = len;
					state->off = off;
					goto end;
				}
				/* Hard errors break pipe and stop xmit. */
				sk_psock_report_error(psock, ret ? -ret : EPIPE);
				sk_psock_clear_state(psock, SK_PSOCK_TX_ENABLED);
				kfree_skb(skb);
				goto end;
			}
			off += ret;
			len -= ret;
		} while (len);

		if (!ingress)
			kfree_skb(skb);
	}
end:
	release_sock(psock->sk);
}

struct sk_psock *sk_psock_init(struct sock *sk, int node)
{
	struct sk_psock *psock = kzalloc_node(sizeof(*psock),
					      GFP_ATOMIC | __GFP_NOWARN,
					      node);
	if (!psock)
		return NULL;

	psock->sk = sk;
	psock->eval =  __SK_NONE;

	INIT_LIST_HEAD(&psock->link);
	spin_lock_init(&psock->link_lock);

	INIT_WORK(&psock->work, sk_psock_backlog);
	INIT_LIST_HEAD(&psock->ingress_msg);
	skb_queue_head_init(&psock->ingress_skb);

	sk_psock_set_state(psock, SK_PSOCK_TX_ENABLED);
	refcount_set(&psock->refcnt, 1);

	rcu_assign_sk_user_data(sk, psock);
	sock_hold(sk);

	return psock;
}
EXPORT_SYMBOL_GPL(sk_psock_init);

struct sk_psock_link *sk_psock_link_pop(struct sk_psock *psock)
{
	struct sk_psock_link *link;

	spin_lock_bh(&psock->link_lock);
	link = list_first_entry_or_null(&psock->link, struct sk_psock_link,
					list);
	if (link)
		list_del(&link->list);
	spin_unlock_bh(&psock->link_lock);
	return link;
}

void __sk_psock_purge_ingress_msg(struct sk_psock *psock)
{
	struct sk_msg *msg, *tmp;

	list_for_each_entry_safe(msg, tmp, &psock->ingress_msg, list) {
		list_del(&msg->list);
		sk_msg_free(psock->sk, msg);
		kfree(msg);
	}
}

static void sk_psock_zap_ingress(struct sk_psock *psock)
{
	__skb_queue_purge(&psock->ingress_skb);
	__sk_psock_purge_ingress_msg(psock);
}

static void sk_psock_link_destroy(struct sk_psock *psock)
{
	struct sk_psock_link *link, *tmp;

	list_for_each_entry_safe(link, tmp, &psock->link, list) {
		list_del(&link->list);
		sk_psock_free_link(link);
	}
}

static void sk_psock_destroy_deferred(struct work_struct *gc)
{
	struct sk_psock *psock = container_of(gc, struct sk_psock, gc);

	/* No sk_callback_lock since already detached. */
	if (psock->parser.enabled)
		strp_done(&psock->parser.strp);

	cancel_work_sync(&psock->work);

	psock_progs_drop(&psock->progs);

	sk_psock_link_destroy(psock);
	sk_psock_cork_free(psock);
	sk_psock_zap_ingress(psock);

	if (psock->sk_redir)
		sock_put(psock->sk_redir);
	sock_put(psock->sk);
	kfree(psock);
}

void sk_psock_destroy(struct rcu_head *rcu)
{
	struct sk_psock *psock = container_of(rcu, struct sk_psock, rcu);

	INIT_WORK(&psock->gc, sk_psock_destroy_deferred);
	schedule_work(&psock->gc);
}
EXPORT_SYMBOL_GPL(sk_psock_destroy);

void sk_psock_drop(struct sock *sk, struct sk_psock *psock)
{
	rcu_assign_sk_user_data(sk, NULL);
	sk_psock_cork_free(psock);
	sk_psock_zap_ingress(psock);
	sk_psock_restore_proto(sk, psock);

	write_lock_bh(&sk->sk_callback_lock);
	if (psock->progs.skb_parser)
		sk_psock_stop_strp(sk, psock);
	write_unlock_bh(&sk->sk_callback_lock);
	sk_psock_clear_state(psock, SK_PSOCK_TX_ENABLED);

	call_rcu(&psock->rcu, sk_psock_destroy);
}
EXPORT_SYMBOL_GPL(sk_psock_drop);

static int sk_psock_map_verd(int verdict, bool redir)
{
	switch (verdict) {
	case SK_PASS:
		return redir ? __SK_REDIRECT : __SK_PASS;
	case SK_DROP:
	default:
		break;
	}

	return __SK_DROP;
}

int sk_psock_msg_verdict(struct sock *sk, struct sk_psock *psock,
			 struct sk_msg *msg)
{
	struct bpf_prog *prog;
	int ret;

	preempt_disable();
	rcu_read_lock();
	prog = READ_ONCE(psock->progs.msg_parser);
	if (unlikely(!prog)) {
		ret = __SK_PASS;
		goto out;
	}

	sk_msg_compute_data_pointers(msg);
	msg->sk = sk;
	ret = BPF_PROG_RUN(prog, msg);
	ret = sk_psock_map_verd(ret, msg->sk_redir);
	psock->apply_bytes = msg->apply_bytes;
	if (ret == __SK_REDIRECT) {
		if (psock->sk_redir)
			sock_put(psock->sk_redir);
		psock->sk_redir = msg->sk_redir;
		if (!psock->sk_redir) {
			ret = __SK_DROP;
			goto out;
		}
		sock_hold(psock->sk_redir);
	}
out:
	rcu_read_unlock();
	preempt_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(sk_psock_msg_verdict);

static int sk_psock_bpf_run(struct sk_psock *psock, struct bpf_prog *prog,
			    struct sk_buff *skb)
{
	int ret;

	skb->sk = psock->sk;
	bpf_compute_data_end_sk_skb(skb);
	preempt_disable();
	ret = BPF_PROG_RUN(prog, skb);
	preempt_enable();
	/* strparser clones the skb before handing it to a upper layer,
	 * meaning skb_orphan has been called. We NULL sk on the way out
	 * to ensure we don't trigger a BUG_ON() in skb/sk operations
	 * later and because we are not charging the memory of this skb
	 * to any socket yet.
	 */
	skb->sk = NULL;
	return ret;
}

static struct sk_psock *sk_psock_from_strp(struct strparser *strp)
{
	struct sk_psock_parser *parser;

	parser = container_of(strp, struct sk_psock_parser, strp);
	return container_of(parser, struct sk_psock, parser);
}

static void sk_psock_verdict_apply(struct sk_psock *psock,
				   struct sk_buff *skb, int verdict)
{
	struct sk_psock *psock_other;
	struct sock *sk_other;
	bool ingress;

	switch (verdict) {
	case __SK_PASS:
		sk_other = psock->sk;
		if (sock_flag(sk_other, SOCK_DEAD) ||
		    !sk_psock_test_state(psock, SK_PSOCK_TX_ENABLED)) {
			goto out_free;
		}
		if (atomic_read(&sk_other->sk_rmem_alloc) <=
		    sk_other->sk_rcvbuf) {
			struct tcp_skb_cb *tcp = TCP_SKB_CB(skb);

			tcp->bpf.flags |= BPF_F_INGRESS;
			skb_queue_tail(&psock->ingress_skb, skb);
			schedule_work(&psock->work);
			break;
		}
		goto out_free;
	case __SK_REDIRECT:
		sk_other = tcp_skb_bpf_redirect_fetch(skb);
		if (unlikely(!sk_other))
			goto out_free;
		psock_other = sk_psock(sk_other);
		if (!psock_other || sock_flag(sk_other, SOCK_DEAD) ||
		    !sk_psock_test_state(psock_other, SK_PSOCK_TX_ENABLED))
			goto out_free;
		ingress = tcp_skb_bpf_ingress(skb);
		if ((!ingress && sock_writeable(sk_other)) ||
		    (ingress &&
		     atomic_read(&sk_other->sk_rmem_alloc) <=
		     sk_other->sk_rcvbuf)) {
			if (!ingress)
				skb_set_owner_w(skb, sk_other);
			skb_queue_tail(&psock_other->ingress_skb, skb);
			schedule_work(&psock_other->work);
			break;
		}
		/* fall-through */
	case __SK_DROP:
		/* fall-through */
	default:
out_free:
		kfree_skb(skb);
	}
}

static void sk_psock_strp_read(struct strparser *strp, struct sk_buff *skb)
{
	struct sk_psock *psock = sk_psock_from_strp(strp);
	struct bpf_prog *prog;
	int ret = __SK_DROP;

	rcu_read_lock();
	prog = READ_ONCE(psock->progs.skb_verdict);
	if (likely(prog)) {
		skb_orphan(skb);
		tcp_skb_bpf_redirect_clear(skb);
		ret = sk_psock_bpf_run(psock, prog, skb);
		ret = sk_psock_map_verd(ret, tcp_skb_bpf_redirect_fetch(skb));
	}
	rcu_read_unlock();
	sk_psock_verdict_apply(psock, skb, ret);
}

static int sk_psock_strp_read_done(struct strparser *strp, int err)
{
	return err;
}

static int sk_psock_strp_parse(struct strparser *strp, struct sk_buff *skb)
{
	struct sk_psock *psock = sk_psock_from_strp(strp);
	struct bpf_prog *prog;
	int ret = skb->len;

	rcu_read_lock();
	prog = READ_ONCE(psock->progs.skb_parser);
	if (likely(prog))
		ret = sk_psock_bpf_run(psock, prog, skb);
	rcu_read_unlock();
	return ret;
}

/* Called with socket lock held. */
static void sk_psock_strp_data_ready(struct sock *sk)
{
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (likely(psock)) {
		write_lock_bh(&sk->sk_callback_lock);
		strp_data_ready(&psock->parser.strp);
		write_unlock_bh(&sk->sk_callback_lock);
	}
	rcu_read_unlock();
}

static void sk_psock_write_space(struct sock *sk)
{
	struct sk_psock *psock;
	void (*write_space)(struct sock *sk);

	rcu_read_lock();
	psock = sk_psock(sk);
	if (likely(psock && sk_psock_test_state(psock, SK_PSOCK_TX_ENABLED)))
		schedule_work(&psock->work);
	write_space = psock->saved_write_space;
	rcu_read_unlock();
	write_space(sk);
}

int sk_psock_init_strp(struct sock *sk, struct sk_psock *psock)
{
	static const struct strp_callbacks cb = {
		.rcv_msg	= sk_psock_strp_read,
		.read_sock_done	= sk_psock_strp_read_done,
		.parse_msg	= sk_psock_strp_parse,
	};

	psock->parser.enabled = false;
	return strp_init(&psock->parser.strp, sk, &cb);
}

void sk_psock_start_strp(struct sock *sk, struct sk_psock *psock)
{
	struct sk_psock_parser *parser = &psock->parser;

	if (parser->enabled)
		return;

	parser->saved_data_ready = sk->sk_data_ready;
	sk->sk_data_ready = sk_psock_strp_data_ready;
	sk->sk_write_space = sk_psock_write_space;
	parser->enabled = true;
}

void sk_psock_stop_strp(struct sock *sk, struct sk_psock *psock)
{
	struct sk_psock_parser *parser = &psock->parser;

	if (!parser->enabled)
		return;

	sk->sk_data_ready = parser->saved_data_ready;
	parser->saved_data_ready = NULL;
	strp_stop(&parser->strp);
	parser->enabled = false;
}
