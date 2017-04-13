/*
 * Kernel Connection Multiplexor
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/errqueue.h>
#include <linux/file.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/syscalls.h>
#include <linux/sched/signal.h>

#include <net/kcm.h>
#include <net/netns/generic.h>
#include <net/sock.h>
#include <uapi/linux/kcm.h>

unsigned int kcm_net_id;

static struct kmem_cache *kcm_psockp __read_mostly;
static struct kmem_cache *kcm_muxp __read_mostly;
static struct workqueue_struct *kcm_wq;

static inline struct kcm_sock *kcm_sk(const struct sock *sk)
{
	return (struct kcm_sock *)sk;
}

static inline struct kcm_tx_msg *kcm_tx_msg(struct sk_buff *skb)
{
	return (struct kcm_tx_msg *)skb->cb;
}

static void report_csk_error(struct sock *csk, int err)
{
	csk->sk_err = EPIPE;
	csk->sk_error_report(csk);
}

static void kcm_abort_tx_psock(struct kcm_psock *psock, int err,
			       bool wakeup_kcm)
{
	struct sock *csk = psock->sk;
	struct kcm_mux *mux = psock->mux;

	/* Unrecoverable error in transmit */

	spin_lock_bh(&mux->lock);

	if (psock->tx_stopped) {
		spin_unlock_bh(&mux->lock);
		return;
	}

	psock->tx_stopped = 1;
	KCM_STATS_INCR(psock->stats.tx_aborts);

	if (!psock->tx_kcm) {
		/* Take off psocks_avail list */
		list_del(&psock->psock_avail_list);
	} else if (wakeup_kcm) {
		/* In this case psock is being aborted while outside of
		 * write_msgs and psock is reserved. Schedule tx_work
		 * to handle the failure there. Need to commit tx_stopped
		 * before queuing work.
		 */
		smp_mb();

		queue_work(kcm_wq, &psock->tx_kcm->tx_work);
	}

	spin_unlock_bh(&mux->lock);

	/* Report error on lower socket */
	report_csk_error(csk, err);
}

/* RX mux lock held. */
static void kcm_update_rx_mux_stats(struct kcm_mux *mux,
				    struct kcm_psock *psock)
{
	STRP_STATS_ADD(mux->stats.rx_bytes,
		       psock->strp.stats.rx_bytes -
		       psock->saved_rx_bytes);
	mux->stats.rx_msgs +=
		psock->strp.stats.rx_msgs - psock->saved_rx_msgs;
	psock->saved_rx_msgs = psock->strp.stats.rx_msgs;
	psock->saved_rx_bytes = psock->strp.stats.rx_bytes;
}

static void kcm_update_tx_mux_stats(struct kcm_mux *mux,
				    struct kcm_psock *psock)
{
	KCM_STATS_ADD(mux->stats.tx_bytes,
		      psock->stats.tx_bytes - psock->saved_tx_bytes);
	mux->stats.tx_msgs +=
		psock->stats.tx_msgs - psock->saved_tx_msgs;
	psock->saved_tx_msgs = psock->stats.tx_msgs;
	psock->saved_tx_bytes = psock->stats.tx_bytes;
}

static int kcm_queue_rcv_skb(struct sock *sk, struct sk_buff *skb);

/* KCM is ready to receive messages on its queue-- either the KCM is new or
 * has become unblocked after being blocked on full socket buffer. Queue any
 * pending ready messages on a psock. RX mux lock held.
 */
static void kcm_rcv_ready(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;
	struct kcm_psock *psock;
	struct sk_buff *skb;

	if (unlikely(kcm->rx_wait || kcm->rx_psock || kcm->rx_disabled))
		return;

	while (unlikely((skb = __skb_dequeue(&mux->rx_hold_queue)))) {
		if (kcm_queue_rcv_skb(&kcm->sk, skb)) {
			/* Assuming buffer limit has been reached */
			skb_queue_head(&mux->rx_hold_queue, skb);
			WARN_ON(!sk_rmem_alloc_get(&kcm->sk));
			return;
		}
	}

	while (!list_empty(&mux->psocks_ready)) {
		psock = list_first_entry(&mux->psocks_ready, struct kcm_psock,
					 psock_ready_list);

		if (kcm_queue_rcv_skb(&kcm->sk, psock->ready_rx_msg)) {
			/* Assuming buffer limit has been reached */
			WARN_ON(!sk_rmem_alloc_get(&kcm->sk));
			return;
		}

		/* Consumed the ready message on the psock. Schedule rx_work to
		 * get more messages.
		 */
		list_del(&psock->psock_ready_list);
		psock->ready_rx_msg = NULL;
		/* Commit clearing of ready_rx_msg for queuing work */
		smp_mb();

		strp_unpause(&psock->strp);
		strp_check_rcv(&psock->strp);
	}

	/* Buffer limit is okay now, add to ready list */
	list_add_tail(&kcm->wait_rx_list,
		      &kcm->mux->kcm_rx_waiters);
	kcm->rx_wait = true;
}

static void kcm_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct kcm_sock *kcm = kcm_sk(sk);
	struct kcm_mux *mux = kcm->mux;
	unsigned int len = skb->truesize;

	sk_mem_uncharge(sk, len);
	atomic_sub(len, &sk->sk_rmem_alloc);

	/* For reading rx_wait and rx_psock without holding lock */
	smp_mb__after_atomic();

	if (!kcm->rx_wait && !kcm->rx_psock &&
	    sk_rmem_alloc_get(sk) < sk->sk_rcvlowat) {
		spin_lock_bh(&mux->rx_lock);
		kcm_rcv_ready(kcm);
		spin_unlock_bh(&mux->rx_lock);
	}
}

static int kcm_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff_head *list = &sk->sk_receive_queue;

	if (atomic_read(&sk->sk_rmem_alloc) >= sk->sk_rcvbuf)
		return -ENOMEM;

	if (!sk_rmem_schedule(sk, skb, skb->truesize))
		return -ENOBUFS;

	skb->dev = NULL;

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = kcm_rfree;
	atomic_add(skb->truesize, &sk->sk_rmem_alloc);
	sk_mem_charge(sk, skb->truesize);

	skb_queue_tail(list, skb);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk);

	return 0;
}

/* Requeue received messages for a kcm socket to other kcm sockets. This is
 * called with a kcm socket is receive disabled.
 * RX mux lock held.
 */
static void requeue_rx_msgs(struct kcm_mux *mux, struct sk_buff_head *head)
{
	struct sk_buff *skb;
	struct kcm_sock *kcm;

	while ((skb = __skb_dequeue(head))) {
		/* Reset destructor to avoid calling kcm_rcv_ready */
		skb->destructor = sock_rfree;
		skb_orphan(skb);
try_again:
		if (list_empty(&mux->kcm_rx_waiters)) {
			skb_queue_tail(&mux->rx_hold_queue, skb);
			continue;
		}

		kcm = list_first_entry(&mux->kcm_rx_waiters,
				       struct kcm_sock, wait_rx_list);

		if (kcm_queue_rcv_skb(&kcm->sk, skb)) {
			/* Should mean socket buffer full */
			list_del(&kcm->wait_rx_list);
			kcm->rx_wait = false;

			/* Commit rx_wait to read in kcm_free */
			smp_wmb();

			goto try_again;
		}
	}
}

/* Lower sock lock held */
static struct kcm_sock *reserve_rx_kcm(struct kcm_psock *psock,
				       struct sk_buff *head)
{
	struct kcm_mux *mux = psock->mux;
	struct kcm_sock *kcm;

	WARN_ON(psock->ready_rx_msg);

	if (psock->rx_kcm)
		return psock->rx_kcm;

	spin_lock_bh(&mux->rx_lock);

	if (psock->rx_kcm) {
		spin_unlock_bh(&mux->rx_lock);
		return psock->rx_kcm;
	}

	kcm_update_rx_mux_stats(mux, psock);

	if (list_empty(&mux->kcm_rx_waiters)) {
		psock->ready_rx_msg = head;
		strp_pause(&psock->strp);
		list_add_tail(&psock->psock_ready_list,
			      &mux->psocks_ready);
		spin_unlock_bh(&mux->rx_lock);
		return NULL;
	}

	kcm = list_first_entry(&mux->kcm_rx_waiters,
			       struct kcm_sock, wait_rx_list);
	list_del(&kcm->wait_rx_list);
	kcm->rx_wait = false;

	psock->rx_kcm = kcm;
	kcm->rx_psock = psock;

	spin_unlock_bh(&mux->rx_lock);

	return kcm;
}

static void kcm_done(struct kcm_sock *kcm);

static void kcm_done_work(struct work_struct *w)
{
	kcm_done(container_of(w, struct kcm_sock, done_work));
}

/* Lower sock held */
static void unreserve_rx_kcm(struct kcm_psock *psock,
			     bool rcv_ready)
{
	struct kcm_sock *kcm = psock->rx_kcm;
	struct kcm_mux *mux = psock->mux;

	if (!kcm)
		return;

	spin_lock_bh(&mux->rx_lock);

	psock->rx_kcm = NULL;
	kcm->rx_psock = NULL;

	/* Commit kcm->rx_psock before sk_rmem_alloc_get to sync with
	 * kcm_rfree
	 */
	smp_mb();

	if (unlikely(kcm->done)) {
		spin_unlock_bh(&mux->rx_lock);

		/* Need to run kcm_done in a task since we need to qcquire
		 * callback locks which may already be held here.
		 */
		INIT_WORK(&kcm->done_work, kcm_done_work);
		schedule_work(&kcm->done_work);
		return;
	}

	if (unlikely(kcm->rx_disabled)) {
		requeue_rx_msgs(mux, &kcm->sk.sk_receive_queue);
	} else if (rcv_ready || unlikely(!sk_rmem_alloc_get(&kcm->sk))) {
		/* Check for degenerative race with rx_wait that all
		 * data was dequeued (accounted for in kcm_rfree).
		 */
		kcm_rcv_ready(kcm);
	}
	spin_unlock_bh(&mux->rx_lock);
}

/* Lower sock lock held */
static void psock_data_ready(struct sock *sk)
{
	struct kcm_psock *psock;

	read_lock_bh(&sk->sk_callback_lock);

	psock = (struct kcm_psock *)sk->sk_user_data;
	if (likely(psock))
		strp_data_ready(&psock->strp);

	read_unlock_bh(&sk->sk_callback_lock);
}

/* Called with lower sock held */
static void kcm_rcv_strparser(struct strparser *strp, struct sk_buff *skb)
{
	struct kcm_psock *psock = container_of(strp, struct kcm_psock, strp);
	struct kcm_sock *kcm;

try_queue:
	kcm = reserve_rx_kcm(psock, skb);
	if (!kcm) {
		 /* Unable to reserve a KCM, message is held in psock and strp
		  * is paused.
		  */
		return;
	}

	if (kcm_queue_rcv_skb(&kcm->sk, skb)) {
		/* Should mean socket buffer full */
		unreserve_rx_kcm(psock, false);
		goto try_queue;
	}
}

static int kcm_parse_func_strparser(struct strparser *strp, struct sk_buff *skb)
{
	struct kcm_psock *psock = container_of(strp, struct kcm_psock, strp);
	struct bpf_prog *prog = psock->bpf_prog;

	return (*prog->bpf_func)(skb, prog->insnsi);
}

static int kcm_read_sock_done(struct strparser *strp, int err)
{
	struct kcm_psock *psock = container_of(strp, struct kcm_psock, strp);

	unreserve_rx_kcm(psock, true);

	return err;
}

static void psock_state_change(struct sock *sk)
{
	/* TCP only does a POLLIN for a half close. Do a POLLHUP here
	 * since application will normally not poll with POLLIN
	 * on the TCP sockets.
	 */

	report_csk_error(sk, EPIPE);
}

static void psock_write_space(struct sock *sk)
{
	struct kcm_psock *psock;
	struct kcm_mux *mux;
	struct kcm_sock *kcm;

	read_lock_bh(&sk->sk_callback_lock);

	psock = (struct kcm_psock *)sk->sk_user_data;
	if (unlikely(!psock))
		goto out;
	mux = psock->mux;

	spin_lock_bh(&mux->lock);

	/* Check if the socket is reserved so someone is waiting for sending. */
	kcm = psock->tx_kcm;
	if (kcm && !unlikely(kcm->tx_stopped))
		queue_work(kcm_wq, &kcm->tx_work);

	spin_unlock_bh(&mux->lock);
out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void unreserve_psock(struct kcm_sock *kcm);

/* kcm sock is locked. */
static struct kcm_psock *reserve_psock(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;
	struct kcm_psock *psock;

	psock = kcm->tx_psock;

	smp_rmb(); /* Must read tx_psock before tx_wait */

	if (psock) {
		WARN_ON(kcm->tx_wait);
		if (unlikely(psock->tx_stopped))
			unreserve_psock(kcm);
		else
			return kcm->tx_psock;
	}

	spin_lock_bh(&mux->lock);

	/* Check again under lock to see if psock was reserved for this
	 * psock via psock_unreserve.
	 */
	psock = kcm->tx_psock;
	if (unlikely(psock)) {
		WARN_ON(kcm->tx_wait);
		spin_unlock_bh(&mux->lock);
		return kcm->tx_psock;
	}

	if (!list_empty(&mux->psocks_avail)) {
		psock = list_first_entry(&mux->psocks_avail,
					 struct kcm_psock,
					 psock_avail_list);
		list_del(&psock->psock_avail_list);
		if (kcm->tx_wait) {
			list_del(&kcm->wait_psock_list);
			kcm->tx_wait = false;
		}
		kcm->tx_psock = psock;
		psock->tx_kcm = kcm;
		KCM_STATS_INCR(psock->stats.reserved);
	} else if (!kcm->tx_wait) {
		list_add_tail(&kcm->wait_psock_list,
			      &mux->kcm_tx_waiters);
		kcm->tx_wait = true;
	}

	spin_unlock_bh(&mux->lock);

	return psock;
}

/* mux lock held */
static void psock_now_avail(struct kcm_psock *psock)
{
	struct kcm_mux *mux = psock->mux;
	struct kcm_sock *kcm;

	if (list_empty(&mux->kcm_tx_waiters)) {
		list_add_tail(&psock->psock_avail_list,
			      &mux->psocks_avail);
	} else {
		kcm = list_first_entry(&mux->kcm_tx_waiters,
				       struct kcm_sock,
				       wait_psock_list);
		list_del(&kcm->wait_psock_list);
		kcm->tx_wait = false;
		psock->tx_kcm = kcm;

		/* Commit before changing tx_psock since that is read in
		 * reserve_psock before queuing work.
		 */
		smp_mb();

		kcm->tx_psock = psock;
		KCM_STATS_INCR(psock->stats.reserved);
		queue_work(kcm_wq, &kcm->tx_work);
	}
}

/* kcm sock is locked. */
static void unreserve_psock(struct kcm_sock *kcm)
{
	struct kcm_psock *psock;
	struct kcm_mux *mux = kcm->mux;

	spin_lock_bh(&mux->lock);

	psock = kcm->tx_psock;

	if (WARN_ON(!psock)) {
		spin_unlock_bh(&mux->lock);
		return;
	}

	smp_rmb(); /* Read tx_psock before tx_wait */

	kcm_update_tx_mux_stats(mux, psock);

	WARN_ON(kcm->tx_wait);

	kcm->tx_psock = NULL;
	psock->tx_kcm = NULL;
	KCM_STATS_INCR(psock->stats.unreserved);

	if (unlikely(psock->tx_stopped)) {
		if (psock->done) {
			/* Deferred free */
			list_del(&psock->psock_list);
			mux->psocks_cnt--;
			sock_put(psock->sk);
			fput(psock->sk->sk_socket->file);
			kmem_cache_free(kcm_psockp, psock);
		}

		/* Don't put back on available list */

		spin_unlock_bh(&mux->lock);

		return;
	}

	psock_now_avail(psock);

	spin_unlock_bh(&mux->lock);
}

static void kcm_report_tx_retry(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;

	spin_lock_bh(&mux->lock);
	KCM_STATS_INCR(mux->stats.tx_retries);
	spin_unlock_bh(&mux->lock);
}

/* Write any messages ready on the kcm socket.  Called with kcm sock lock
 * held.  Return bytes actually sent or error.
 */
static int kcm_write_msgs(struct kcm_sock *kcm)
{
	struct sock *sk = &kcm->sk;
	struct kcm_psock *psock;
	struct sk_buff *skb, *head;
	struct kcm_tx_msg *txm;
	unsigned short fragidx, frag_offset;
	unsigned int sent, total_sent = 0;
	int ret = 0;

	kcm->tx_wait_more = false;
	psock = kcm->tx_psock;
	if (unlikely(psock && psock->tx_stopped)) {
		/* A reserved psock was aborted asynchronously. Unreserve
		 * it and we'll retry the message.
		 */
		unreserve_psock(kcm);
		kcm_report_tx_retry(kcm);
		if (skb_queue_empty(&sk->sk_write_queue))
			return 0;

		kcm_tx_msg(skb_peek(&sk->sk_write_queue))->sent = 0;

	} else if (skb_queue_empty(&sk->sk_write_queue)) {
		return 0;
	}

	head = skb_peek(&sk->sk_write_queue);
	txm = kcm_tx_msg(head);

	if (txm->sent) {
		/* Send of first skbuff in queue already in progress */
		if (WARN_ON(!psock)) {
			ret = -EINVAL;
			goto out;
		}
		sent = txm->sent;
		frag_offset = txm->frag_offset;
		fragidx = txm->fragidx;
		skb = txm->frag_skb;

		goto do_frag;
	}

try_again:
	psock = reserve_psock(kcm);
	if (!psock)
		goto out;

	do {
		skb = head;
		txm = kcm_tx_msg(head);
		sent = 0;

do_frag_list:
		if (WARN_ON(!skb_shinfo(skb)->nr_frags)) {
			ret = -EINVAL;
			goto out;
		}

		for (fragidx = 0; fragidx < skb_shinfo(skb)->nr_frags;
		     fragidx++) {
			skb_frag_t *frag;

			frag_offset = 0;
do_frag:
			frag = &skb_shinfo(skb)->frags[fragidx];
			if (WARN_ON(!frag->size)) {
				ret = -EINVAL;
				goto out;
			}

			ret = kernel_sendpage(psock->sk->sk_socket,
					      frag->page.p,
					      frag->page_offset + frag_offset,
					      frag->size - frag_offset,
					      MSG_DONTWAIT);
			if (ret <= 0) {
				if (ret == -EAGAIN) {
					/* Save state to try again when there's
					 * write space on the socket
					 */
					txm->sent = sent;
					txm->frag_offset = frag_offset;
					txm->fragidx = fragidx;
					txm->frag_skb = skb;

					ret = 0;
					goto out;
				}

				/* Hard failure in sending message, abort this
				 * psock since it has lost framing
				 * synchonization and retry sending the
				 * message from the beginning.
				 */
				kcm_abort_tx_psock(psock, ret ? -ret : EPIPE,
						   true);
				unreserve_psock(kcm);

				txm->sent = 0;
				kcm_report_tx_retry(kcm);
				ret = 0;

				goto try_again;
			}

			sent += ret;
			frag_offset += ret;
			KCM_STATS_ADD(psock->stats.tx_bytes, ret);
			if (frag_offset < frag->size) {
				/* Not finished with this frag */
				goto do_frag;
			}
		}

		if (skb == head) {
			if (skb_has_frag_list(skb)) {
				skb = skb_shinfo(skb)->frag_list;
				goto do_frag_list;
			}
		} else if (skb->next) {
			skb = skb->next;
			goto do_frag_list;
		}

		/* Successfully sent the whole packet, account for it. */
		skb_dequeue(&sk->sk_write_queue);
		kfree_skb(head);
		sk->sk_wmem_queued -= sent;
		total_sent += sent;
		KCM_STATS_INCR(psock->stats.tx_msgs);
	} while ((head = skb_peek(&sk->sk_write_queue)));
out:
	if (!head) {
		/* Done with all queued messages. */
		WARN_ON(!skb_queue_empty(&sk->sk_write_queue));
		unreserve_psock(kcm);
	}

	/* Check if write space is available */
	sk->sk_write_space(sk);

	return total_sent ? : ret;
}

static void kcm_tx_work(struct work_struct *w)
{
	struct kcm_sock *kcm = container_of(w, struct kcm_sock, tx_work);
	struct sock *sk = &kcm->sk;
	int err;

	lock_sock(sk);

	/* Primarily for SOCK_DGRAM sockets, also handle asynchronous tx
	 * aborts
	 */
	err = kcm_write_msgs(kcm);
	if (err < 0) {
		/* Hard failure in write, report error on KCM socket */
		pr_warn("KCM: Hard failure on kcm_write_msgs %d\n", err);
		report_csk_error(&kcm->sk, -err);
		goto out;
	}

	/* Primarily for SOCK_SEQPACKET sockets */
	if (likely(sk->sk_socket) &&
	    test_bit(SOCK_NOSPACE, &sk->sk_socket->flags)) {
		clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		sk->sk_write_space(sk);
	}

out:
	release_sock(sk);
}

static void kcm_push(struct kcm_sock *kcm)
{
	if (kcm->tx_wait_more)
		kcm_write_msgs(kcm);
}

static ssize_t kcm_sendpage(struct socket *sock, struct page *page,
			    int offset, size_t size, int flags)

{
	struct sock *sk = sock->sk;
	struct kcm_sock *kcm = kcm_sk(sk);
	struct sk_buff *skb = NULL, *head = NULL;
	long timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	bool eor;
	int err = 0;
	int i;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	/* No MSG_EOR from splice, only look at MSG_MORE */
	eor = !(flags & MSG_MORE);

	lock_sock(sk);

	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	err = -EPIPE;
	if (sk->sk_err)
		goto out_error;

	if (kcm->seq_skb) {
		/* Previously opened message */
		head = kcm->seq_skb;
		skb = kcm_tx_msg(head)->last_skb;
		i = skb_shinfo(skb)->nr_frags;

		if (skb_can_coalesce(skb, i, page, offset)) {
			skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], size);
			skb_shinfo(skb)->tx_flags |= SKBTX_SHARED_FRAG;
			goto coalesced;
		}

		if (i >= MAX_SKB_FRAGS) {
			struct sk_buff *tskb;

			tskb = alloc_skb(0, sk->sk_allocation);
			while (!tskb) {
				kcm_push(kcm);
				err = sk_stream_wait_memory(sk, &timeo);
				if (err)
					goto out_error;
			}

			if (head == skb)
				skb_shinfo(head)->frag_list = tskb;
			else
				skb->next = tskb;

			skb = tskb;
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			i = 0;
		}
	} else {
		/* Call the sk_stream functions to manage the sndbuf mem. */
		if (!sk_stream_memory_free(sk)) {
			kcm_push(kcm);
			set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			err = sk_stream_wait_memory(sk, &timeo);
			if (err)
				goto out_error;
		}

		head = alloc_skb(0, sk->sk_allocation);
		while (!head) {
			kcm_push(kcm);
			err = sk_stream_wait_memory(sk, &timeo);
			if (err)
				goto out_error;
		}

		skb = head;
		i = 0;
	}

	get_page(page);
	skb_fill_page_desc(skb, i, page, offset, size);
	skb_shinfo(skb)->tx_flags |= SKBTX_SHARED_FRAG;

coalesced:
	skb->len += size;
	skb->data_len += size;
	skb->truesize += size;
	sk->sk_wmem_queued += size;
	sk_mem_charge(sk, size);

	if (head != skb) {
		head->len += size;
		head->data_len += size;
		head->truesize += size;
	}

	if (eor) {
		bool not_busy = skb_queue_empty(&sk->sk_write_queue);

		/* Message complete, queue it on send buffer */
		__skb_queue_tail(&sk->sk_write_queue, head);
		kcm->seq_skb = NULL;
		KCM_STATS_INCR(kcm->stats.tx_msgs);

		if (flags & MSG_BATCH) {
			kcm->tx_wait_more = true;
		} else if (kcm->tx_wait_more || not_busy) {
			err = kcm_write_msgs(kcm);
			if (err < 0) {
				/* We got a hard error in write_msgs but have
				 * already queued this message. Report an error
				 * in the socket, but don't affect return value
				 * from sendmsg
				 */
				pr_warn("KCM: Hard failure on kcm_write_msgs\n");
				report_csk_error(&kcm->sk, -err);
			}
		}
	} else {
		/* Message not complete, save state */
		kcm->seq_skb = head;
		kcm_tx_msg(head)->last_skb = skb;
	}

	KCM_STATS_ADD(kcm->stats.tx_bytes, size);

	release_sock(sk);
	return size;

out_error:
	kcm_push(kcm);

	err = sk_stream_error(sk, flags, err);

	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(skb_queue_len(&sk->sk_write_queue) == 0 && err == -EAGAIN))
		sk->sk_write_space(sk);

	release_sock(sk);
	return err;
}

static int kcm_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct kcm_sock *kcm = kcm_sk(sk);
	struct sk_buff *skb = NULL, *head = NULL;
	size_t copy, copied = 0;
	long timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	int eor = (sock->type == SOCK_DGRAM) ?
		  !(msg->msg_flags & MSG_MORE) : !!(msg->msg_flags & MSG_EOR);
	int err = -EPIPE;

	lock_sock(sk);

	/* Per tcp_sendmsg this should be in poll */
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	if (sk->sk_err)
		goto out_error;

	if (kcm->seq_skb) {
		/* Previously opened message */
		head = kcm->seq_skb;
		skb = kcm_tx_msg(head)->last_skb;
		goto start;
	}

	/* Call the sk_stream functions to manage the sndbuf mem. */
	if (!sk_stream_memory_free(sk)) {
		kcm_push(kcm);
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		err = sk_stream_wait_memory(sk, &timeo);
		if (err)
			goto out_error;
	}

	if (msg_data_left(msg)) {
		/* New message, alloc head skb */
		head = alloc_skb(0, sk->sk_allocation);
		while (!head) {
			kcm_push(kcm);
			err = sk_stream_wait_memory(sk, &timeo);
			if (err)
				goto out_error;

			head = alloc_skb(0, sk->sk_allocation);
		}

		skb = head;

		/* Set ip_summed to CHECKSUM_UNNECESSARY to avoid calling
		 * csum_and_copy_from_iter from skb_do_copy_data_nocache.
		 */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

start:
	while (msg_data_left(msg)) {
		bool merge = true;
		int i = skb_shinfo(skb)->nr_frags;
		struct page_frag *pfrag = sk_page_frag(sk);

		if (!sk_page_frag_refill(sk, pfrag))
			goto wait_for_memory;

		if (!skb_can_coalesce(skb, i, pfrag->page,
				      pfrag->offset)) {
			if (i == MAX_SKB_FRAGS) {
				struct sk_buff *tskb;

				tskb = alloc_skb(0, sk->sk_allocation);
				if (!tskb)
					goto wait_for_memory;

				if (head == skb)
					skb_shinfo(head)->frag_list = tskb;
				else
					skb->next = tskb;

				skb = tskb;
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				continue;
			}
			merge = false;
		}

		copy = min_t(int, msg_data_left(msg),
			     pfrag->size - pfrag->offset);

		if (!sk_wmem_schedule(sk, copy))
			goto wait_for_memory;

		err = skb_copy_to_page_nocache(sk, &msg->msg_iter, skb,
					       pfrag->page,
					       pfrag->offset,
					       copy);
		if (err)
			goto out_error;

		/* Update the skb. */
		if (merge) {
			skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy);
		} else {
			skb_fill_page_desc(skb, i, pfrag->page,
					   pfrag->offset, copy);
			get_page(pfrag->page);
		}

		pfrag->offset += copy;
		copied += copy;
		if (head != skb) {
			head->len += copy;
			head->data_len += copy;
		}

		continue;

wait_for_memory:
		kcm_push(kcm);
		err = sk_stream_wait_memory(sk, &timeo);
		if (err)
			goto out_error;
	}

	if (eor) {
		bool not_busy = skb_queue_empty(&sk->sk_write_queue);

		if (head) {
			/* Message complete, queue it on send buffer */
			__skb_queue_tail(&sk->sk_write_queue, head);
			kcm->seq_skb = NULL;
			KCM_STATS_INCR(kcm->stats.tx_msgs);
		}

		if (msg->msg_flags & MSG_BATCH) {
			kcm->tx_wait_more = true;
		} else if (kcm->tx_wait_more || not_busy) {
			err = kcm_write_msgs(kcm);
			if (err < 0) {
				/* We got a hard error in write_msgs but have
				 * already queued this message. Report an error
				 * in the socket, but don't affect return value
				 * from sendmsg
				 */
				pr_warn("KCM: Hard failure on kcm_write_msgs\n");
				report_csk_error(&kcm->sk, -err);
			}
		}
	} else {
		/* Message not complete, save state */
partial_message:
		if (head) {
			kcm->seq_skb = head;
			kcm_tx_msg(head)->last_skb = skb;
		}
	}

	KCM_STATS_ADD(kcm->stats.tx_bytes, copied);

	release_sock(sk);
	return copied;

out_error:
	kcm_push(kcm);

	if (copied && sock->type == SOCK_SEQPACKET) {
		/* Wrote some bytes before encountering an
		 * error, return partial success.
		 */
		goto partial_message;
	}

	if (head != kcm->seq_skb)
		kfree_skb(head);

	err = sk_stream_error(sk, msg->msg_flags, err);

	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(skb_queue_len(&sk->sk_write_queue) == 0 && err == -EAGAIN))
		sk->sk_write_space(sk);

	release_sock(sk);
	return err;
}

static struct sk_buff *kcm_wait_data(struct sock *sk, int flags,
				     long timeo, int *err)
{
	struct sk_buff *skb;

	while (!(skb = skb_peek(&sk->sk_receive_queue))) {
		if (sk->sk_err) {
			*err = sock_error(sk);
			return NULL;
		}

		if (sock_flag(sk, SOCK_DONE))
			return NULL;

		if ((flags & MSG_DONTWAIT) || !timeo) {
			*err = -EAGAIN;
			return NULL;
		}

		sk_wait_data(sk, &timeo, NULL);

		/* Handle signals */
		if (signal_pending(current)) {
			*err = sock_intr_errno(timeo);
			return NULL;
		}
	}

	return skb;
}

static int kcm_recvmsg(struct socket *sock, struct msghdr *msg,
		       size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct kcm_sock *kcm = kcm_sk(sk);
	int err = 0;
	long timeo;
	struct strp_rx_msg *rxm;
	int copied = 0;
	struct sk_buff *skb;

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	lock_sock(sk);

	skb = kcm_wait_data(sk, flags, timeo, &err);
	if (!skb)
		goto out;

	/* Okay, have a message on the receive queue */

	rxm = strp_rx_msg(skb);

	if (len > rxm->full_len)
		len = rxm->full_len;

	err = skb_copy_datagram_msg(skb, rxm->offset, msg, len);
	if (err < 0)
		goto out;

	copied = len;
	if (likely(!(flags & MSG_PEEK))) {
		KCM_STATS_ADD(kcm->stats.rx_bytes, copied);
		if (copied < rxm->full_len) {
			if (sock->type == SOCK_DGRAM) {
				/* Truncated message */
				msg->msg_flags |= MSG_TRUNC;
				goto msg_finished;
			}
			rxm->offset += copied;
			rxm->full_len -= copied;
		} else {
msg_finished:
			/* Finished with message */
			msg->msg_flags |= MSG_EOR;
			KCM_STATS_INCR(kcm->stats.rx_msgs);
			skb_unlink(skb, &sk->sk_receive_queue);
			kfree_skb(skb);
		}
	}

out:
	release_sock(sk);

	return copied ? : err;
}

static ssize_t kcm_splice_read(struct socket *sock, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
			       unsigned int flags)
{
	struct sock *sk = sock->sk;
	struct kcm_sock *kcm = kcm_sk(sk);
	long timeo;
	struct strp_rx_msg *rxm;
	int err = 0;
	ssize_t copied;
	struct sk_buff *skb;

	/* Only support splice for SOCKSEQPACKET */

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	lock_sock(sk);

	skb = kcm_wait_data(sk, flags, timeo, &err);
	if (!skb)
		goto err_out;

	/* Okay, have a message on the receive queue */

	rxm = strp_rx_msg(skb);

	if (len > rxm->full_len)
		len = rxm->full_len;

	copied = skb_splice_bits(skb, sk, rxm->offset, pipe, len, flags);
	if (copied < 0) {
		err = copied;
		goto err_out;
	}

	KCM_STATS_ADD(kcm->stats.rx_bytes, copied);

	rxm->offset += copied;
	rxm->full_len -= copied;

	/* We have no way to return MSG_EOR. If all the bytes have been
	 * read we still leave the message in the receive socket buffer.
	 * A subsequent recvmsg needs to be done to return MSG_EOR and
	 * finish reading the message.
	 */

	release_sock(sk);

	return copied;

err_out:
	release_sock(sk);

	return err;
}

/* kcm sock lock held */
static void kcm_recv_disable(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;

	if (kcm->rx_disabled)
		return;

	spin_lock_bh(&mux->rx_lock);

	kcm->rx_disabled = 1;

	/* If a psock is reserved we'll do cleanup in unreserve */
	if (!kcm->rx_psock) {
		if (kcm->rx_wait) {
			list_del(&kcm->wait_rx_list);
			kcm->rx_wait = false;
		}

		requeue_rx_msgs(mux, &kcm->sk.sk_receive_queue);
	}

	spin_unlock_bh(&mux->rx_lock);
}

/* kcm sock lock held */
static void kcm_recv_enable(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;

	if (!kcm->rx_disabled)
		return;

	spin_lock_bh(&mux->rx_lock);

	kcm->rx_disabled = 0;
	kcm_rcv_ready(kcm);

	spin_unlock_bh(&mux->rx_lock);
}

static int kcm_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct kcm_sock *kcm = kcm_sk(sock->sk);
	int val, valbool;
	int err = 0;

	if (level != SOL_KCM)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EINVAL;

	valbool = val ? 1 : 0;

	switch (optname) {
	case KCM_RECV_DISABLE:
		lock_sock(&kcm->sk);
		if (valbool)
			kcm_recv_disable(kcm);
		else
			kcm_recv_enable(kcm);
		release_sock(&kcm->sk);
		break;
	default:
		err = -ENOPROTOOPT;
	}

	return err;
}

static int kcm_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct kcm_sock *kcm = kcm_sk(sock->sk);
	int val, len;

	if (level != SOL_KCM)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));
	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case KCM_RECV_DISABLE:
		val = kcm->rx_disabled;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

static void init_kcm_sock(struct kcm_sock *kcm, struct kcm_mux *mux)
{
	struct kcm_sock *tkcm;
	struct list_head *head;
	int index = 0;

	/* For SOCK_SEQPACKET sock type, datagram_poll checks the sk_state, so
	 * we set sk_state, otherwise epoll_wait always returns right away with
	 * POLLHUP
	 */
	kcm->sk.sk_state = TCP_ESTABLISHED;

	/* Add to mux's kcm sockets list */
	kcm->mux = mux;
	spin_lock_bh(&mux->lock);

	head = &mux->kcm_socks;
	list_for_each_entry(tkcm, &mux->kcm_socks, kcm_sock_list) {
		if (tkcm->index != index)
			break;
		head = &tkcm->kcm_sock_list;
		index++;
	}

	list_add(&kcm->kcm_sock_list, head);
	kcm->index = index;

	mux->kcm_socks_cnt++;
	spin_unlock_bh(&mux->lock);

	INIT_WORK(&kcm->tx_work, kcm_tx_work);

	spin_lock_bh(&mux->rx_lock);
	kcm_rcv_ready(kcm);
	spin_unlock_bh(&mux->rx_lock);
}

static int kcm_attach(struct socket *sock, struct socket *csock,
		      struct bpf_prog *prog)
{
	struct kcm_sock *kcm = kcm_sk(sock->sk);
	struct kcm_mux *mux = kcm->mux;
	struct sock *csk;
	struct kcm_psock *psock = NULL, *tpsock;
	struct list_head *head;
	int index = 0;
	struct strp_callbacks cb;
	int err;

	csk = csock->sk;
	if (!csk)
		return -EINVAL;

	psock = kmem_cache_zalloc(kcm_psockp, GFP_KERNEL);
	if (!psock)
		return -ENOMEM;

	psock->mux = mux;
	psock->sk = csk;
	psock->bpf_prog = prog;

	cb.rcv_msg = kcm_rcv_strparser;
	cb.abort_parser = NULL;
	cb.parse_msg = kcm_parse_func_strparser;
	cb.read_sock_done = kcm_read_sock_done;

	err = strp_init(&psock->strp, csk, &cb);
	if (err) {
		kmem_cache_free(kcm_psockp, psock);
		return err;
	}

	sock_hold(csk);

	write_lock_bh(&csk->sk_callback_lock);
	psock->save_data_ready = csk->sk_data_ready;
	psock->save_write_space = csk->sk_write_space;
	psock->save_state_change = csk->sk_state_change;
	csk->sk_user_data = psock;
	csk->sk_data_ready = psock_data_ready;
	csk->sk_write_space = psock_write_space;
	csk->sk_state_change = psock_state_change;
	write_unlock_bh(&csk->sk_callback_lock);

	/* Finished initialization, now add the psock to the MUX. */
	spin_lock_bh(&mux->lock);
	head = &mux->psocks;
	list_for_each_entry(tpsock, &mux->psocks, psock_list) {
		if (tpsock->index != index)
			break;
		head = &tpsock->psock_list;
		index++;
	}

	list_add(&psock->psock_list, head);
	psock->index = index;

	KCM_STATS_INCR(mux->stats.psock_attach);
	mux->psocks_cnt++;
	psock_now_avail(psock);
	spin_unlock_bh(&mux->lock);

	/* Schedule RX work in case there are already bytes queued */
	strp_check_rcv(&psock->strp);

	return 0;
}

static int kcm_attach_ioctl(struct socket *sock, struct kcm_attach *info)
{
	struct socket *csock;
	struct bpf_prog *prog;
	int err;

	csock = sockfd_lookup(info->fd, &err);
	if (!csock)
		return -ENOENT;

	prog = bpf_prog_get_type(info->bpf_fd, BPF_PROG_TYPE_SOCKET_FILTER);
	if (IS_ERR(prog)) {
		err = PTR_ERR(prog);
		goto out;
	}

	err = kcm_attach(sock, csock, prog);
	if (err) {
		bpf_prog_put(prog);
		goto out;
	}

	/* Keep reference on file also */

	return 0;
out:
	fput(csock->file);
	return err;
}

static void kcm_unattach(struct kcm_psock *psock)
{
	struct sock *csk = psock->sk;
	struct kcm_mux *mux = psock->mux;

	lock_sock(csk);

	/* Stop getting callbacks from TCP socket. After this there should
	 * be no way to reserve a kcm for this psock.
	 */
	write_lock_bh(&csk->sk_callback_lock);
	csk->sk_user_data = NULL;
	csk->sk_data_ready = psock->save_data_ready;
	csk->sk_write_space = psock->save_write_space;
	csk->sk_state_change = psock->save_state_change;
	strp_stop(&psock->strp);

	if (WARN_ON(psock->rx_kcm)) {
		write_unlock_bh(&csk->sk_callback_lock);
		return;
	}

	spin_lock_bh(&mux->rx_lock);

	/* Stop receiver activities. After this point psock should not be
	 * able to get onto ready list either through callbacks or work.
	 */
	if (psock->ready_rx_msg) {
		list_del(&psock->psock_ready_list);
		kfree_skb(psock->ready_rx_msg);
		psock->ready_rx_msg = NULL;
		KCM_STATS_INCR(mux->stats.rx_ready_drops);
	}

	spin_unlock_bh(&mux->rx_lock);

	write_unlock_bh(&csk->sk_callback_lock);

	/* Call strp_done without sock lock */
	release_sock(csk);
	strp_done(&psock->strp);
	lock_sock(csk);

	bpf_prog_put(psock->bpf_prog);

	spin_lock_bh(&mux->lock);

	aggregate_psock_stats(&psock->stats, &mux->aggregate_psock_stats);
	save_strp_stats(&psock->strp, &mux->aggregate_strp_stats);

	KCM_STATS_INCR(mux->stats.psock_unattach);

	if (psock->tx_kcm) {
		/* psock was reserved.  Just mark it finished and we will clean
		 * up in the kcm paths, we need kcm lock which can not be
		 * acquired here.
		 */
		KCM_STATS_INCR(mux->stats.psock_unattach_rsvd);
		spin_unlock_bh(&mux->lock);

		/* We are unattaching a socket that is reserved. Abort the
		 * socket since we may be out of sync in sending on it. We need
		 * to do this without the mux lock.
		 */
		kcm_abort_tx_psock(psock, EPIPE, false);

		spin_lock_bh(&mux->lock);
		if (!psock->tx_kcm) {
			/* psock now unreserved in window mux was unlocked */
			goto no_reserved;
		}
		psock->done = 1;

		/* Commit done before queuing work to process it */
		smp_mb();

		/* Queue tx work to make sure psock->done is handled */
		queue_work(kcm_wq, &psock->tx_kcm->tx_work);
		spin_unlock_bh(&mux->lock);
	} else {
no_reserved:
		if (!psock->tx_stopped)
			list_del(&psock->psock_avail_list);
		list_del(&psock->psock_list);
		mux->psocks_cnt--;
		spin_unlock_bh(&mux->lock);

		sock_put(csk);
		fput(csk->sk_socket->file);
		kmem_cache_free(kcm_psockp, psock);
	}

	release_sock(csk);
}

static int kcm_unattach_ioctl(struct socket *sock, struct kcm_unattach *info)
{
	struct kcm_sock *kcm = kcm_sk(sock->sk);
	struct kcm_mux *mux = kcm->mux;
	struct kcm_psock *psock;
	struct socket *csock;
	struct sock *csk;
	int err;

	csock = sockfd_lookup(info->fd, &err);
	if (!csock)
		return -ENOENT;

	csk = csock->sk;
	if (!csk) {
		err = -EINVAL;
		goto out;
	}

	err = -ENOENT;

	spin_lock_bh(&mux->lock);

	list_for_each_entry(psock, &mux->psocks, psock_list) {
		if (psock->sk != csk)
			continue;

		/* Found the matching psock */

		if (psock->unattaching || WARN_ON(psock->done)) {
			err = -EALREADY;
			break;
		}

		psock->unattaching = 1;

		spin_unlock_bh(&mux->lock);

		/* Lower socket lock should already be held */
		kcm_unattach(psock);

		err = 0;
		goto out;
	}

	spin_unlock_bh(&mux->lock);

out:
	fput(csock->file);
	return err;
}

static struct proto kcm_proto = {
	.name	= "KCM",
	.owner	= THIS_MODULE,
	.obj_size = sizeof(struct kcm_sock),
};

/* Clone a kcm socket. */
static int kcm_clone(struct socket *osock, struct kcm_clone *info,
		     struct socket **newsockp)
{
	struct socket *newsock;
	struct sock *newsk;
	struct file *newfile;
	int err, newfd;

	err = -ENFILE;
	newsock = sock_alloc();
	if (!newsock)
		goto out;

	newsock->type = osock->type;
	newsock->ops = osock->ops;

	__module_get(newsock->ops->owner);

	newfd = get_unused_fd_flags(0);
	if (unlikely(newfd < 0)) {
		err = newfd;
		goto out_fd_fail;
	}

	newfile = sock_alloc_file(newsock, 0, osock->sk->sk_prot_creator->name);
	if (unlikely(IS_ERR(newfile))) {
		err = PTR_ERR(newfile);
		goto out_sock_alloc_fail;
	}

	newsk = sk_alloc(sock_net(osock->sk), PF_KCM, GFP_KERNEL,
			 &kcm_proto, true);
	if (!newsk) {
		err = -ENOMEM;
		goto out_sk_alloc_fail;
	}

	sock_init_data(newsock, newsk);
	init_kcm_sock(kcm_sk(newsk), kcm_sk(osock->sk)->mux);

	fd_install(newfd, newfile);
	*newsockp = newsock;
	info->fd = newfd;

	return 0;

out_sk_alloc_fail:
	fput(newfile);
out_sock_alloc_fail:
	put_unused_fd(newfd);
out_fd_fail:
	sock_release(newsock);
out:
	return err;
}

static int kcm_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case SIOCKCMATTACH: {
		struct kcm_attach info;

		if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
			return -EFAULT;

		err = kcm_attach_ioctl(sock, &info);

		break;
	}
	case SIOCKCMUNATTACH: {
		struct kcm_unattach info;

		if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
			return -EFAULT;

		err = kcm_unattach_ioctl(sock, &info);

		break;
	}
	case SIOCKCMCLONE: {
		struct kcm_clone info;
		struct socket *newsock = NULL;

		err = kcm_clone(sock, &info, &newsock);
		if (!err) {
			if (copy_to_user((void __user *)arg, &info,
					 sizeof(info))) {
				err = -EFAULT;
				sys_close(info.fd);
			}
		}

		break;
	}
	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}

static void free_mux(struct rcu_head *rcu)
{
	struct kcm_mux *mux = container_of(rcu,
	    struct kcm_mux, rcu);

	kmem_cache_free(kcm_muxp, mux);
}

static void release_mux(struct kcm_mux *mux)
{
	struct kcm_net *knet = mux->knet;
	struct kcm_psock *psock, *tmp_psock;

	/* Release psocks */
	list_for_each_entry_safe(psock, tmp_psock,
				 &mux->psocks, psock_list) {
		if (!WARN_ON(psock->unattaching))
			kcm_unattach(psock);
	}

	if (WARN_ON(mux->psocks_cnt))
		return;

	__skb_queue_purge(&mux->rx_hold_queue);

	mutex_lock(&knet->mutex);
	aggregate_mux_stats(&mux->stats, &knet->aggregate_mux_stats);
	aggregate_psock_stats(&mux->aggregate_psock_stats,
			      &knet->aggregate_psock_stats);
	aggregate_strp_stats(&mux->aggregate_strp_stats,
			     &knet->aggregate_strp_stats);
	list_del_rcu(&mux->kcm_mux_list);
	knet->count--;
	mutex_unlock(&knet->mutex);

	call_rcu(&mux->rcu, free_mux);
}

static void kcm_done(struct kcm_sock *kcm)
{
	struct kcm_mux *mux = kcm->mux;
	struct sock *sk = &kcm->sk;
	int socks_cnt;

	spin_lock_bh(&mux->rx_lock);
	if (kcm->rx_psock) {
		/* Cleanup in unreserve_rx_kcm */
		WARN_ON(kcm->done);
		kcm->rx_disabled = 1;
		kcm->done = 1;
		spin_unlock_bh(&mux->rx_lock);
		return;
	}

	if (kcm->rx_wait) {
		list_del(&kcm->wait_rx_list);
		kcm->rx_wait = false;
	}
	/* Move any pending receive messages to other kcm sockets */
	requeue_rx_msgs(mux, &sk->sk_receive_queue);

	spin_unlock_bh(&mux->rx_lock);

	if (WARN_ON(sk_rmem_alloc_get(sk)))
		return;

	/* Detach from MUX */
	spin_lock_bh(&mux->lock);

	list_del(&kcm->kcm_sock_list);
	mux->kcm_socks_cnt--;
	socks_cnt = mux->kcm_socks_cnt;

	spin_unlock_bh(&mux->lock);

	if (!socks_cnt) {
		/* We are done with the mux now. */
		release_mux(mux);
	}

	WARN_ON(kcm->rx_wait);

	sock_put(&kcm->sk);
}

/* Called by kcm_release to close a KCM socket.
 * If this is the last KCM socket on the MUX, destroy the MUX.
 */
static int kcm_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct kcm_sock *kcm;
	struct kcm_mux *mux;
	struct kcm_psock *psock;

	if (!sk)
		return 0;

	kcm = kcm_sk(sk);
	mux = kcm->mux;

	sock_orphan(sk);
	kfree_skb(kcm->seq_skb);

	lock_sock(sk);
	/* Purge queue under lock to avoid race condition with tx_work trying
	 * to act when queue is nonempty. If tx_work runs after this point
	 * it will just return.
	 */
	__skb_queue_purge(&sk->sk_write_queue);

	/* Set tx_stopped. This is checked when psock is bound to a kcm and we
	 * get a writespace callback. This prevents further work being queued
	 * from the callback (unbinding the psock occurs after canceling work.
	 */
	kcm->tx_stopped = 1;

	release_sock(sk);

	spin_lock_bh(&mux->lock);
	if (kcm->tx_wait) {
		/* Take of tx_wait list, after this point there should be no way
		 * that a psock will be assigned to this kcm.
		 */
		list_del(&kcm->wait_psock_list);
		kcm->tx_wait = false;
	}
	spin_unlock_bh(&mux->lock);

	/* Cancel work. After this point there should be no outside references
	 * to the kcm socket.
	 */
	cancel_work_sync(&kcm->tx_work);

	lock_sock(sk);
	psock = kcm->tx_psock;
	if (psock) {
		/* A psock was reserved, so we need to kill it since it
		 * may already have some bytes queued from a message. We
		 * need to do this after removing kcm from tx_wait list.
		 */
		kcm_abort_tx_psock(psock, EPIPE, false);
		unreserve_psock(kcm);
	}
	release_sock(sk);

	WARN_ON(kcm->tx_wait);
	WARN_ON(kcm->tx_psock);

	sock->sk = NULL;

	kcm_done(kcm);

	return 0;
}

static const struct proto_ops kcm_dgram_ops = {
	.family =	PF_KCM,
	.owner =	THIS_MODULE,
	.release =	kcm_release,
	.bind =		sock_no_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	sock_no_getname,
	.poll =		datagram_poll,
	.ioctl =	kcm_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	kcm_setsockopt,
	.getsockopt =	kcm_getsockopt,
	.sendmsg =	kcm_sendmsg,
	.recvmsg =	kcm_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	kcm_sendpage,
};

static const struct proto_ops kcm_seqpacket_ops = {
	.family =	PF_KCM,
	.owner =	THIS_MODULE,
	.release =	kcm_release,
	.bind =		sock_no_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	sock_no_getname,
	.poll =		datagram_poll,
	.ioctl =	kcm_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	kcm_setsockopt,
	.getsockopt =	kcm_getsockopt,
	.sendmsg =	kcm_sendmsg,
	.recvmsg =	kcm_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	kcm_sendpage,
	.splice_read =	kcm_splice_read,
};

/* Create proto operation for kcm sockets */
static int kcm_create(struct net *net, struct socket *sock,
		      int protocol, int kern)
{
	struct kcm_net *knet = net_generic(net, kcm_net_id);
	struct sock *sk;
	struct kcm_mux *mux;

	switch (sock->type) {
	case SOCK_DGRAM:
		sock->ops = &kcm_dgram_ops;
		break;
	case SOCK_SEQPACKET:
		sock->ops = &kcm_seqpacket_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	if (protocol != KCMPROTO_CONNECTED)
		return -EPROTONOSUPPORT;

	sk = sk_alloc(net, PF_KCM, GFP_KERNEL, &kcm_proto, kern);
	if (!sk)
		return -ENOMEM;

	/* Allocate a kcm mux, shared between KCM sockets */
	mux = kmem_cache_zalloc(kcm_muxp, GFP_KERNEL);
	if (!mux) {
		sk_free(sk);
		return -ENOMEM;
	}

	spin_lock_init(&mux->lock);
	spin_lock_init(&mux->rx_lock);
	INIT_LIST_HEAD(&mux->kcm_socks);
	INIT_LIST_HEAD(&mux->kcm_rx_waiters);
	INIT_LIST_HEAD(&mux->kcm_tx_waiters);

	INIT_LIST_HEAD(&mux->psocks);
	INIT_LIST_HEAD(&mux->psocks_ready);
	INIT_LIST_HEAD(&mux->psocks_avail);

	mux->knet = knet;

	/* Add new MUX to list */
	mutex_lock(&knet->mutex);
	list_add_rcu(&mux->kcm_mux_list, &knet->mux_list);
	knet->count++;
	mutex_unlock(&knet->mutex);

	skb_queue_head_init(&mux->rx_hold_queue);

	/* Init KCM socket */
	sock_init_data(sock, sk);
	init_kcm_sock(kcm_sk(sk), mux);

	return 0;
}

static struct net_proto_family kcm_family_ops = {
	.family = PF_KCM,
	.create = kcm_create,
	.owner  = THIS_MODULE,
};

static __net_init int kcm_init_net(struct net *net)
{
	struct kcm_net *knet = net_generic(net, kcm_net_id);

	INIT_LIST_HEAD_RCU(&knet->mux_list);
	mutex_init(&knet->mutex);

	return 0;
}

static __net_exit void kcm_exit_net(struct net *net)
{
	struct kcm_net *knet = net_generic(net, kcm_net_id);

	/* All KCM sockets should be closed at this point, which should mean
	 * that all multiplexors and psocks have been destroyed.
	 */
	WARN_ON(!list_empty(&knet->mux_list));
}

static struct pernet_operations kcm_net_ops = {
	.init = kcm_init_net,
	.exit = kcm_exit_net,
	.id   = &kcm_net_id,
	.size = sizeof(struct kcm_net),
};

static int __init kcm_init(void)
{
	int err = -ENOMEM;

	kcm_muxp = kmem_cache_create("kcm_mux_cache",
				     sizeof(struct kcm_mux), 0,
				     SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);
	if (!kcm_muxp)
		goto fail;

	kcm_psockp = kmem_cache_create("kcm_psock_cache",
				       sizeof(struct kcm_psock), 0,
					SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);
	if (!kcm_psockp)
		goto fail;

	kcm_wq = create_singlethread_workqueue("kkcmd");
	if (!kcm_wq)
		goto fail;

	err = proto_register(&kcm_proto, 1);
	if (err)
		goto fail;

	err = sock_register(&kcm_family_ops);
	if (err)
		goto sock_register_fail;

	err = register_pernet_device(&kcm_net_ops);
	if (err)
		goto net_ops_fail;

	err = kcm_proc_init();
	if (err)
		goto proc_init_fail;

	return 0;

proc_init_fail:
	unregister_pernet_device(&kcm_net_ops);

net_ops_fail:
	sock_unregister(PF_KCM);

sock_register_fail:
	proto_unregister(&kcm_proto);

fail:
	kmem_cache_destroy(kcm_muxp);
	kmem_cache_destroy(kcm_psockp);

	if (kcm_wq)
		destroy_workqueue(kcm_wq);

	return err;
}

static void __exit kcm_exit(void)
{
	kcm_proc_exit();
	unregister_pernet_device(&kcm_net_ops);
	sock_unregister(PF_KCM);
	proto_unregister(&kcm_proto);
	destroy_workqueue(kcm_wq);

	kmem_cache_destroy(kcm_muxp);
	kmem_cache_destroy(kcm_psockp);
}

module_init(kcm_init);
module_exit(kcm_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_KCM);

