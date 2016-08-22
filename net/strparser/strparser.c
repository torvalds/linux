/*
 * Stream Parser
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
#include <net/strparser.h>
#include <net/netns/generic.h>
#include <net/sock.h>
#include <net/tcp.h>

static struct workqueue_struct *strp_wq;

struct _strp_rx_msg {
	/* Internal cb structure. struct strp_rx_msg must be first for passing
	 * to upper layer.
	 */
	struct strp_rx_msg strp;
	int accum_len;
	int early_eaten;
};

static inline struct _strp_rx_msg *_strp_rx_msg(struct sk_buff *skb)
{
	return (struct _strp_rx_msg *)((void *)skb->cb +
		offsetof(struct qdisc_skb_cb, data));
}

/* Lower lock held */
static void strp_abort_rx_strp(struct strparser *strp, int err)
{
	struct sock *csk = strp->sk;

	/* Unrecoverable error in receive */

	del_timer(&strp->rx_msg_timer);

	if (strp->rx_stopped)
		return;

	strp->rx_stopped = 1;

	/* Report an error on the lower socket */
	csk->sk_err = err;
	csk->sk_error_report(csk);
}

static void strp_start_rx_timer(struct strparser *strp)
{
	if (strp->sk->sk_rcvtimeo)
		mod_timer(&strp->rx_msg_timer, strp->sk->sk_rcvtimeo);
}

/* Lower lock held */
static void strp_parser_err(struct strparser *strp, int err,
			    read_descriptor_t *desc)
{
	desc->error = err;
	kfree_skb(strp->rx_skb_head);
	strp->rx_skb_head = NULL;
	strp->cb.abort_parser(strp, err);
}

/* Lower socket lock held */
static int strp_tcp_recv(read_descriptor_t *desc, struct sk_buff *orig_skb,
			 unsigned int orig_offset, size_t orig_len)
{
	struct strparser *strp = (struct strparser *)desc->arg.data;
	struct _strp_rx_msg *rxm;
	struct sk_buff *head, *skb;
	size_t eaten = 0, cand_len;
	ssize_t extra;
	int err;
	bool cloned_orig = false;

	if (strp->rx_paused)
		return 0;

	head = strp->rx_skb_head;
	if (head) {
		/* Message already in progress */

		rxm = _strp_rx_msg(head);
		if (unlikely(rxm->early_eaten)) {
			/* Already some number of bytes on the receive sock
			 * data saved in rx_skb_head, just indicate they
			 * are consumed.
			 */
			eaten = orig_len <= rxm->early_eaten ?
				orig_len : rxm->early_eaten;
			rxm->early_eaten -= eaten;

			return eaten;
		}

		if (unlikely(orig_offset)) {
			/* Getting data with a non-zero offset when a message is
			 * in progress is not expected. If it does happen, we
			 * need to clone and pull since we can't deal with
			 * offsets in the skbs for a message expect in the head.
			 */
			orig_skb = skb_clone(orig_skb, GFP_ATOMIC);
			if (!orig_skb) {
				STRP_STATS_INCR(strp->stats.rx_mem_fail);
				desc->error = -ENOMEM;
				return 0;
			}
			if (!pskb_pull(orig_skb, orig_offset)) {
				STRP_STATS_INCR(strp->stats.rx_mem_fail);
				kfree_skb(orig_skb);
				desc->error = -ENOMEM;
				return 0;
			}
			cloned_orig = true;
			orig_offset = 0;
		}

		if (!strp->rx_skb_nextp) {
			/* We are going to append to the frags_list of head.
			 * Need to unshare the frag_list.
			 */
			err = skb_unclone(head, GFP_ATOMIC);
			if (err) {
				STRP_STATS_INCR(strp->stats.rx_mem_fail);
				desc->error = err;
				return 0;
			}

			if (unlikely(skb_shinfo(head)->frag_list)) {
				/* We can't append to an sk_buff that already
				 * has a frag_list. We create a new head, point
				 * the frag_list of that to the old head, and
				 * then are able to use the old head->next for
				 * appending to the message.
				 */
				if (WARN_ON(head->next)) {
					desc->error = -EINVAL;
					return 0;
				}

				skb = alloc_skb(0, GFP_ATOMIC);
				if (!skb) {
					STRP_STATS_INCR(strp->stats.rx_mem_fail);
					desc->error = -ENOMEM;
					return 0;
				}
				skb->len = head->len;
				skb->data_len = head->len;
				skb->truesize = head->truesize;
				*_strp_rx_msg(skb) = *_strp_rx_msg(head);
				strp->rx_skb_nextp = &head->next;
				skb_shinfo(skb)->frag_list = head;
				strp->rx_skb_head = skb;
				head = skb;
			} else {
				strp->rx_skb_nextp =
				    &skb_shinfo(head)->frag_list;
			}
		}
	}

	while (eaten < orig_len) {
		/* Always clone since we will consume something */
		skb = skb_clone(orig_skb, GFP_ATOMIC);
		if (!skb) {
			STRP_STATS_INCR(strp->stats.rx_mem_fail);
			desc->error = -ENOMEM;
			break;
		}

		cand_len = orig_len - eaten;

		head = strp->rx_skb_head;
		if (!head) {
			head = skb;
			strp->rx_skb_head = head;
			/* Will set rx_skb_nextp on next packet if needed */
			strp->rx_skb_nextp = NULL;
			rxm = _strp_rx_msg(head);
			memset(rxm, 0, sizeof(*rxm));
			rxm->strp.offset = orig_offset + eaten;
		} else {
			/* Unclone since we may be appending to an skb that we
			 * already share a frag_list with.
			 */
			err = skb_unclone(skb, GFP_ATOMIC);
			if (err) {
				STRP_STATS_INCR(strp->stats.rx_mem_fail);
				desc->error = err;
				break;
			}

			rxm = _strp_rx_msg(head);
			*strp->rx_skb_nextp = skb;
			strp->rx_skb_nextp = &skb->next;
			head->data_len += skb->len;
			head->len += skb->len;
			head->truesize += skb->truesize;
		}

		if (!rxm->strp.full_len) {
			ssize_t len;

			len = (*strp->cb.parse_msg)(strp, head);

			if (!len) {
				/* Need more header to determine length */
				if (!rxm->accum_len) {
					/* Start RX timer for new message */
					strp_start_rx_timer(strp);
				}
				rxm->accum_len += cand_len;
				eaten += cand_len;
				STRP_STATS_INCR(strp->stats.rx_need_more_hdr);
				WARN_ON(eaten != orig_len);
				break;
			} else if (len < 0) {
				if (len == -ESTRPIPE && rxm->accum_len) {
					len = -ENODATA;
					strp->rx_unrecov_intr = 1;
				} else {
					strp->rx_interrupted = 1;
				}
				strp_parser_err(strp, err, desc);
				break;
			} else if (len > strp->sk->sk_rcvbuf) {
				/* Message length exceeds maximum allowed */
				STRP_STATS_INCR(strp->stats.rx_msg_too_big);
				strp_parser_err(strp, -EMSGSIZE, desc);
				break;
			} else if (len <= (ssize_t)head->len -
					  skb->len - rxm->strp.offset) {
				/* Length must be into new skb (and also
				 * greater than zero)
				 */
				STRP_STATS_INCR(strp->stats.rx_bad_hdr_len);
				strp_parser_err(strp, -EPROTO, desc);
				break;
			}

			rxm->strp.full_len = len;
		}

		extra = (ssize_t)(rxm->accum_len + cand_len) -
			rxm->strp.full_len;

		if (extra < 0) {
			/* Message not complete yet. */
			if (rxm->strp.full_len - rxm->accum_len >
			    tcp_inq(strp->sk)) {
				/* Don't have the whole messages in the socket
				 * buffer. Set strp->rx_need_bytes to wait for
				 * the rest of the message. Also, set "early
				 * eaten" since we've already buffered the skb
				 * but don't consume yet per tcp_read_sock.
				 */

				if (!rxm->accum_len) {
					/* Start RX timer for new message */
					strp_start_rx_timer(strp);
				}

				strp->rx_need_bytes = rxm->strp.full_len -
						       rxm->accum_len;
				rxm->accum_len += cand_len;
				rxm->early_eaten = cand_len;
				STRP_STATS_ADD(strp->stats.rx_bytes, cand_len);
				desc->count = 0; /* Stop reading socket */
				break;
			}
			rxm->accum_len += cand_len;
			eaten += cand_len;
			WARN_ON(eaten != orig_len);
			break;
		}

		/* Positive extra indicates ore bytes than needed for the
		 * message
		 */

		WARN_ON(extra > cand_len);

		eaten += (cand_len - extra);

		/* Hurray, we have a new message! */
		del_timer(&strp->rx_msg_timer);
		strp->rx_skb_head = NULL;
		STRP_STATS_INCR(strp->stats.rx_msgs);

		/* Give skb to upper layer */
		strp->cb.rcv_msg(strp, head);

		if (unlikely(strp->rx_paused)) {
			/* Upper layer paused strp */
			break;
		}
	}

	if (cloned_orig)
		kfree_skb(orig_skb);

	STRP_STATS_ADD(strp->stats.rx_bytes, eaten);

	return eaten;
}

static int default_read_sock_done(struct strparser *strp, int err)
{
	return err;
}

/* Called with lock held on lower socket */
static int strp_tcp_read_sock(struct strparser *strp)
{
	read_descriptor_t desc;

	desc.arg.data = strp;
	desc.error = 0;
	desc.count = 1; /* give more than one skb per call */

	/* sk should be locked here, so okay to do tcp_read_sock */
	tcp_read_sock(strp->sk, &desc, strp_tcp_recv);

	desc.error = strp->cb.read_sock_done(strp, desc.error);

	return desc.error;
}

/* Lower sock lock held */
void strp_tcp_data_ready(struct strparser *strp)
{
	struct sock *csk = strp->sk;

	if (unlikely(strp->rx_stopped))
		return;

	/* This check is needed to synchronize with do_strp_rx_work.
	 * do_strp_rx_work acquires a process lock (lock_sock) whereas
	 * the lock held here is bh_lock_sock. The two locks can be
	 * held by different threads at the same time, but bh_lock_sock
	 * allows a thread in BH context to safely check if the process
	 * lock is held. In this case, if the lock is held, queue work.
	 */
	if (sock_owned_by_user(csk)) {
		queue_work(strp_wq, &strp->rx_work);
		return;
	}

	if (strp->rx_paused)
		return;

	if (strp->rx_need_bytes) {
		if (tcp_inq(csk) >= strp->rx_need_bytes)
			strp->rx_need_bytes = 0;
		else
			return;
	}

	if (strp_tcp_read_sock(strp) == -ENOMEM)
		queue_work(strp_wq, &strp->rx_work);
}
EXPORT_SYMBOL_GPL(strp_tcp_data_ready);

static void do_strp_rx_work(struct strparser *strp)
{
	read_descriptor_t rd_desc;
	struct sock *csk = strp->sk;

	/* We need the read lock to synchronize with strp_tcp_data_ready. We
	 * need the socket lock for calling tcp_read_sock.
	 */
	lock_sock(csk);

	if (unlikely(strp->rx_stopped))
		goto out;

	if (strp->rx_paused)
		goto out;

	rd_desc.arg.data = strp;

	if (strp_tcp_read_sock(strp) == -ENOMEM)
		queue_work(strp_wq, &strp->rx_work);

out:
	release_sock(csk);
}

static void strp_rx_work(struct work_struct *w)
{
	do_strp_rx_work(container_of(w, struct strparser, rx_work));
}

static void strp_rx_msg_timeout(unsigned long arg)
{
	struct strparser *strp = (struct strparser *)arg;

	/* Message assembly timed out */
	STRP_STATS_INCR(strp->stats.rx_msg_timeouts);
	lock_sock(strp->sk);
	strp->cb.abort_parser(strp, ETIMEDOUT);
	release_sock(strp->sk);
}

int strp_init(struct strparser *strp, struct sock *csk,
	      struct strp_callbacks *cb)
{
	if (!cb || !cb->rcv_msg || !cb->parse_msg)
		return -EINVAL;

	memset(strp, 0, sizeof(*strp));

	strp->sk = csk;

	setup_timer(&strp->rx_msg_timer, strp_rx_msg_timeout,
		    (unsigned long)strp);

	INIT_WORK(&strp->rx_work, strp_rx_work);

	strp->cb.rcv_msg = cb->rcv_msg;
	strp->cb.parse_msg = cb->parse_msg;
	strp->cb.read_sock_done = cb->read_sock_done ? : default_read_sock_done;
	strp->cb.abort_parser = cb->abort_parser ? : strp_abort_rx_strp;

	return 0;
}
EXPORT_SYMBOL_GPL(strp_init);

/* strp must already be stopped so that strp_tcp_recv will no longer be called.
 * Note that strp_done is not called with the lower socket held.
 */
void strp_done(struct strparser *strp)
{
	WARN_ON(!strp->rx_stopped);

	del_timer_sync(&strp->rx_msg_timer);
	cancel_work_sync(&strp->rx_work);

	if (strp->rx_skb_head) {
		kfree_skb(strp->rx_skb_head);
		strp->rx_skb_head = NULL;
	}
}
EXPORT_SYMBOL_GPL(strp_done);

void strp_stop(struct strparser *strp)
{
	strp->rx_stopped = 1;
}
EXPORT_SYMBOL_GPL(strp_stop);

void strp_check_rcv(struct strparser *strp)
{
	queue_work(strp_wq, &strp->rx_work);
}
EXPORT_SYMBOL_GPL(strp_check_rcv);

static int __init strp_mod_init(void)
{
	strp_wq = create_singlethread_workqueue("kstrp");

	return 0;
}

static void __exit strp_mod_exit(void)
{
}
module_init(strp_mod_init);
module_exit(strp_mod_exit);
MODULE_LICENSE("GPL");
