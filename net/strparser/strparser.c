// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stream Parser
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 */

#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/errqueue.h>
#include <linux/file.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
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

static struct workqueue_struct *strp_wq;

struct _strp_msg {
	/* Internal cb structure. struct strp_msg must be first for passing
	 * to upper layer.
	 */
	struct strp_msg strp;
	int accum_len;
};

static inline struct _strp_msg *_strp_msg(struct sk_buff *skb)
{
	return (struct _strp_msg *)((void *)skb->cb +
		offsetof(struct qdisc_skb_cb, data));
}

/* Lower lock held */
static void strp_abort_strp(struct strparser *strp, int err)
{
	/* Unrecoverable error in receive */

	cancel_delayed_work(&strp->msg_timer_work);

	if (strp->stopped)
		return;

	strp->stopped = 1;

	if (strp->sk) {
		struct sock *sk = strp->sk;

		/* Report an error on the lower socket */
		sk->sk_err = -err;
		sk->sk_error_report(sk);
	}
}

static void strp_start_timer(struct strparser *strp, long timeo)
{
	if (timeo && timeo != LONG_MAX)
		mod_delayed_work(strp_wq, &strp->msg_timer_work, timeo);
}

/* Lower lock held */
static void strp_parser_err(struct strparser *strp, int err,
			    read_descriptor_t *desc)
{
	desc->error = err;
	kfree_skb(strp->skb_head);
	strp->skb_head = NULL;
	strp->cb.abort_parser(strp, err);
}

static inline int strp_peek_len(struct strparser *strp)
{
	if (strp->sk) {
		struct socket *sock = strp->sk->sk_socket;

		return sock->ops->peek_len(sock);
	}

	/* If we don't have an associated socket there's nothing to peek.
	 * Return int max to avoid stopping the strparser.
	 */

	return INT_MAX;
}

/* Lower socket lock held */
static int __strp_recv(read_descriptor_t *desc, struct sk_buff *orig_skb,
		       unsigned int orig_offset, size_t orig_len,
		       size_t max_msg_size, long timeo)
{
	struct strparser *strp = (struct strparser *)desc->arg.data;
	struct _strp_msg *stm;
	struct sk_buff *head, *skb;
	size_t eaten = 0, cand_len;
	ssize_t extra;
	int err;
	bool cloned_orig = false;

	if (strp->paused)
		return 0;

	head = strp->skb_head;
	if (head) {
		/* Message already in progress */
		if (unlikely(orig_offset)) {
			/* Getting data with a non-zero offset when a message is
			 * in progress is not expected. If it does happen, we
			 * need to clone and pull since we can't deal with
			 * offsets in the skbs for a message expect in the head.
			 */
			orig_skb = skb_clone(orig_skb, GFP_ATOMIC);
			if (!orig_skb) {
				STRP_STATS_INCR(strp->stats.mem_fail);
				desc->error = -ENOMEM;
				return 0;
			}
			if (!pskb_pull(orig_skb, orig_offset)) {
				STRP_STATS_INCR(strp->stats.mem_fail);
				kfree_skb(orig_skb);
				desc->error = -ENOMEM;
				return 0;
			}
			cloned_orig = true;
			orig_offset = 0;
		}

		if (!strp->skb_nextp) {
			/* We are going to append to the frags_list of head.
			 * Need to unshare the frag_list.
			 */
			err = skb_unclone(head, GFP_ATOMIC);
			if (err) {
				STRP_STATS_INCR(strp->stats.mem_fail);
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
					STRP_STATS_INCR(strp->stats.mem_fail);
					desc->error = -ENOMEM;
					return 0;
				}
				skb->len = head->len;
				skb->data_len = head->len;
				skb->truesize = head->truesize;
				*_strp_msg(skb) = *_strp_msg(head);
				strp->skb_nextp = &head->next;
				skb_shinfo(skb)->frag_list = head;
				strp->skb_head = skb;
				head = skb;
			} else {
				strp->skb_nextp =
				    &skb_shinfo(head)->frag_list;
			}
		}
	}

	while (eaten < orig_len) {
		/* Always clone since we will consume something */
		skb = skb_clone(orig_skb, GFP_ATOMIC);
		if (!skb) {
			STRP_STATS_INCR(strp->stats.mem_fail);
			desc->error = -ENOMEM;
			break;
		}

		cand_len = orig_len - eaten;

		head = strp->skb_head;
		if (!head) {
			head = skb;
			strp->skb_head = head;
			/* Will set skb_nextp on next packet if needed */
			strp->skb_nextp = NULL;
			stm = _strp_msg(head);
			memset(stm, 0, sizeof(*stm));
			stm->strp.offset = orig_offset + eaten;
		} else {
			/* Unclone if we are appending to an skb that we
			 * already share a frag_list with.
			 */
			if (skb_has_frag_list(skb)) {
				err = skb_unclone(skb, GFP_ATOMIC);
				if (err) {
					STRP_STATS_INCR(strp->stats.mem_fail);
					desc->error = err;
					break;
				}
			}

			stm = _strp_msg(head);
			*strp->skb_nextp = skb;
			strp->skb_nextp = &skb->next;
			head->data_len += skb->len;
			head->len += skb->len;
			head->truesize += skb->truesize;
		}

		if (!stm->strp.full_len) {
			ssize_t len;

			len = (*strp->cb.parse_msg)(strp, head);

			if (!len) {
				/* Need more header to determine length */
				if (!stm->accum_len) {
					/* Start RX timer for new message */
					strp_start_timer(strp, timeo);
				}
				stm->accum_len += cand_len;
				eaten += cand_len;
				STRP_STATS_INCR(strp->stats.need_more_hdr);
				WARN_ON(eaten != orig_len);
				break;
			} else if (len < 0) {
				if (len == -ESTRPIPE && stm->accum_len) {
					len = -ENODATA;
					strp->unrecov_intr = 1;
				} else {
					strp->interrupted = 1;
				}
				strp_parser_err(strp, len, desc);
				break;
			} else if (len > max_msg_size) {
				/* Message length exceeds maximum allowed */
				STRP_STATS_INCR(strp->stats.msg_too_big);
				strp_parser_err(strp, -EMSGSIZE, desc);
				break;
			} else if (len <= (ssize_t)head->len -
					  skb->len - stm->strp.offset) {
				/* Length must be into new skb (and also
				 * greater than zero)
				 */
				STRP_STATS_INCR(strp->stats.bad_hdr_len);
				strp_parser_err(strp, -EPROTO, desc);
				break;
			}

			stm->strp.full_len = len;
		}

		extra = (ssize_t)(stm->accum_len + cand_len) -
			stm->strp.full_len;

		if (extra < 0) {
			/* Message not complete yet. */
			if (stm->strp.full_len - stm->accum_len >
			    strp_peek_len(strp)) {
				/* Don't have the whole message in the socket
				 * buffer. Set strp->need_bytes to wait for
				 * the rest of the message. Also, set "early
				 * eaten" since we've already buffered the skb
				 * but don't consume yet per strp_read_sock.
				 */

				if (!stm->accum_len) {
					/* Start RX timer for new message */
					strp_start_timer(strp, timeo);
				}

				stm->accum_len += cand_len;
				eaten += cand_len;
				strp->need_bytes = stm->strp.full_len -
						       stm->accum_len;
				STRP_STATS_ADD(strp->stats.bytes, cand_len);
				desc->count = 0; /* Stop reading socket */
				break;
			}
			stm->accum_len += cand_len;
			eaten += cand_len;
			WARN_ON(eaten != orig_len);
			break;
		}

		/* Positive extra indicates more bytes than needed for the
		 * message
		 */

		WARN_ON(extra > cand_len);

		eaten += (cand_len - extra);

		/* Hurray, we have a new message! */
		cancel_delayed_work(&strp->msg_timer_work);
		strp->skb_head = NULL;
		strp->need_bytes = 0;
		STRP_STATS_INCR(strp->stats.msgs);

		/* Give skb to upper layer */
		strp->cb.rcv_msg(strp, head);

		if (unlikely(strp->paused)) {
			/* Upper layer paused strp */
			break;
		}
	}

	if (cloned_orig)
		kfree_skb(orig_skb);

	STRP_STATS_ADD(strp->stats.bytes, eaten);

	return eaten;
}

int strp_process(struct strparser *strp, struct sk_buff *orig_skb,
		 unsigned int orig_offset, size_t orig_len,
		 size_t max_msg_size, long timeo)
{
	read_descriptor_t desc; /* Dummy arg to strp_recv */

	desc.arg.data = strp;

	return __strp_recv(&desc, orig_skb, orig_offset, orig_len,
			   max_msg_size, timeo);
}
EXPORT_SYMBOL_GPL(strp_process);

static int strp_recv(read_descriptor_t *desc, struct sk_buff *orig_skb,
		     unsigned int orig_offset, size_t orig_len)
{
	struct strparser *strp = (struct strparser *)desc->arg.data;

	return __strp_recv(desc, orig_skb, orig_offset, orig_len,
			   strp->sk->sk_rcvbuf, strp->sk->sk_rcvtimeo);
}

static int default_read_sock_done(struct strparser *strp, int err)
{
	return err;
}

/* Called with lock held on lower socket */
static int strp_read_sock(struct strparser *strp)
{
	struct socket *sock = strp->sk->sk_socket;
	read_descriptor_t desc;

	if (unlikely(!sock || !sock->ops || !sock->ops->read_sock))
		return -EBUSY;

	desc.arg.data = strp;
	desc.error = 0;
	desc.count = 1; /* give more than one skb per call */

	/* sk should be locked here, so okay to do read_sock */
	sock->ops->read_sock(strp->sk, &desc, strp_recv);

	desc.error = strp->cb.read_sock_done(strp, desc.error);

	return desc.error;
}

/* Lower sock lock held */
void strp_data_ready(struct strparser *strp)
{
	if (unlikely(strp->stopped) || strp->paused)
		return;

	/* This check is needed to synchronize with do_strp_work.
	 * do_strp_work acquires a process lock (lock_sock) whereas
	 * the lock held here is bh_lock_sock. The two locks can be
	 * held by different threads at the same time, but bh_lock_sock
	 * allows a thread in BH context to safely check if the process
	 * lock is held. In this case, if the lock is held, queue work.
	 */
	if (sock_owned_by_user_nocheck(strp->sk)) {
		queue_work(strp_wq, &strp->work);
		return;
	}

	if (strp->need_bytes) {
		if (strp_peek_len(strp) < strp->need_bytes)
			return;
	}

	if (strp_read_sock(strp) == -ENOMEM)
		queue_work(strp_wq, &strp->work);
}
EXPORT_SYMBOL_GPL(strp_data_ready);

static void do_strp_work(struct strparser *strp)
{
	/* We need the read lock to synchronize with strp_data_ready. We
	 * need the socket lock for calling strp_read_sock.
	 */
	strp->cb.lock(strp);

	if (unlikely(strp->stopped))
		goto out;

	if (strp->paused)
		goto out;

	if (strp_read_sock(strp) == -ENOMEM)
		queue_work(strp_wq, &strp->work);

out:
	strp->cb.unlock(strp);
}

static void strp_work(struct work_struct *w)
{
	do_strp_work(container_of(w, struct strparser, work));
}

static void strp_msg_timeout(struct work_struct *w)
{
	struct strparser *strp = container_of(w, struct strparser,
					      msg_timer_work.work);

	/* Message assembly timed out */
	STRP_STATS_INCR(strp->stats.msg_timeouts);
	strp->cb.lock(strp);
	strp->cb.abort_parser(strp, -ETIMEDOUT);
	strp->cb.unlock(strp);
}

static void strp_sock_lock(struct strparser *strp)
{
	lock_sock(strp->sk);
}

static void strp_sock_unlock(struct strparser *strp)
{
	release_sock(strp->sk);
}

int strp_init(struct strparser *strp, struct sock *sk,
	      const struct strp_callbacks *cb)
{

	if (!cb || !cb->rcv_msg || !cb->parse_msg)
		return -EINVAL;

	/* The sk (sock) arg determines the mode of the stream parser.
	 *
	 * If the sock is set then the strparser is in receive callback mode.
	 * The upper layer calls strp_data_ready to kick receive processing
	 * and strparser calls the read_sock function on the socket to
	 * get packets.
	 *
	 * If the sock is not set then the strparser is in general mode.
	 * The upper layer calls strp_process for each skb to be parsed.
	 */

	if (!sk) {
		if (!cb->lock || !cb->unlock)
			return -EINVAL;
	}

	memset(strp, 0, sizeof(*strp));

	strp->sk = sk;

	strp->cb.lock = cb->lock ? : strp_sock_lock;
	strp->cb.unlock = cb->unlock ? : strp_sock_unlock;
	strp->cb.rcv_msg = cb->rcv_msg;
	strp->cb.parse_msg = cb->parse_msg;
	strp->cb.read_sock_done = cb->read_sock_done ? : default_read_sock_done;
	strp->cb.abort_parser = cb->abort_parser ? : strp_abort_strp;

	INIT_DELAYED_WORK(&strp->msg_timer_work, strp_msg_timeout);
	INIT_WORK(&strp->work, strp_work);

	return 0;
}
EXPORT_SYMBOL_GPL(strp_init);

/* Sock process lock held (lock_sock) */
void __strp_unpause(struct strparser *strp)
{
	strp->paused = 0;

	if (strp->need_bytes) {
		if (strp_peek_len(strp) < strp->need_bytes)
			return;
	}
	strp_read_sock(strp);
}
EXPORT_SYMBOL_GPL(__strp_unpause);

void strp_unpause(struct strparser *strp)
{
	strp->paused = 0;

	/* Sync setting paused with RX work */
	smp_mb();

	queue_work(strp_wq, &strp->work);
}
EXPORT_SYMBOL_GPL(strp_unpause);

/* strp must already be stopped so that strp_recv will no longer be called.
 * Note that strp_done is not called with the lower socket held.
 */
void strp_done(struct strparser *strp)
{
	WARN_ON(!strp->stopped);

	cancel_delayed_work_sync(&strp->msg_timer_work);
	cancel_work_sync(&strp->work);

	if (strp->skb_head) {
		kfree_skb(strp->skb_head);
		strp->skb_head = NULL;
	}
}
EXPORT_SYMBOL_GPL(strp_done);

void strp_stop(struct strparser *strp)
{
	strp->stopped = 1;
}
EXPORT_SYMBOL_GPL(strp_stop);

void strp_check_rcv(struct strparser *strp)
{
	queue_work(strp_wq, &strp->work);
}
EXPORT_SYMBOL_GPL(strp_check_rcv);

static int __init strp_dev_init(void)
{
	strp_wq = create_singlethread_workqueue("kstrp");
	if (unlikely(!strp_wq))
		return -ENOMEM;

	return 0;
}
device_initcall(strp_dev_init);
