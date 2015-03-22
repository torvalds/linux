/*
 * NET		Generic infrastructure for Network protocols.
 *
 *		Definitions for request_sock 
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 		From code originally in include/net/tcp.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _REQUEST_SOCK_H
#define _REQUEST_SOCK_H

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/bug.h>

#include <net/sock.h>

struct request_sock;
struct sk_buff;
struct dst_entry;
struct proto;

struct request_sock_ops {
	int		family;
	int		obj_size;
	struct kmem_cache	*slab;
	char		*slab_name;
	int		(*rtx_syn_ack)(struct sock *sk,
				       struct request_sock *req);
	void		(*send_ack)(struct sock *sk, struct sk_buff *skb,
				    struct request_sock *req);
	void		(*send_reset)(struct sock *sk,
				      struct sk_buff *skb);
	void		(*destructor)(struct request_sock *req);
	void		(*syn_ack_timeout)(const struct request_sock *req);
};

int inet_rtx_syn_ack(struct sock *parent, struct request_sock *req);

/* struct request_sock - mini sock to represent a connection request
 */
struct request_sock {
	struct sock_common		__req_common;
#define rsk_refcnt			__req_common.skc_refcnt
#define rsk_hash			__req_common.skc_hash

	struct request_sock		*dl_next;
	struct sock			*rsk_listener;
	u16				mss;
	u8				num_retrans; /* number of retransmits */
	u8				cookie_ts:1; /* syncookie: encode tcpopts in timestamp */
	u8				num_timeout:7; /* number of timeouts */
	/* The following two fields can be easily recomputed I think -AK */
	u32				window_clamp; /* window clamp at creation time */
	u32				rcv_wnd;	  /* rcv_wnd offered first time */
	u32				ts_recent;
	struct timer_list		rsk_timer;
	const struct request_sock_ops	*rsk_ops;
	struct sock			*sk;
	u32				secid;
	u32				peer_secid;
};

static inline struct request_sock *
reqsk_alloc(const struct request_sock_ops *ops, struct sock *sk_listener)
{
	struct request_sock *req = kmem_cache_alloc(ops->slab, GFP_ATOMIC);

	if (req) {
		req->rsk_ops = ops;
		sock_hold(sk_listener);
		req->rsk_listener = sk_listener;

		/* Following is temporary. It is coupled with debugging
		 * helpers in reqsk_put() & reqsk_free()
		 */
		atomic_set(&req->rsk_refcnt, 0);
	}
	return req;
}

static inline struct request_sock *inet_reqsk(struct sock *sk)
{
	return (struct request_sock *)sk;
}

static inline struct sock *req_to_sk(struct request_sock *req)
{
	return (struct sock *)req;
}

static inline void reqsk_free(struct request_sock *req)
{
	/* temporary debugging */
	WARN_ON_ONCE(atomic_read(&req->rsk_refcnt) != 0);

	req->rsk_ops->destructor(req);
	if (req->rsk_listener)
		sock_put(req->rsk_listener);
	kmem_cache_free(req->rsk_ops->slab, req);
}

static inline void reqsk_put(struct request_sock *req)
{
	if (atomic_dec_and_test(&req->rsk_refcnt))
		reqsk_free(req);
}

extern int sysctl_max_syn_backlog;

/** struct listen_sock - listen state
 *
 * @max_qlen_log - log_2 of maximal queued SYNs/REQUESTs
 */
struct listen_sock {
	int			qlen_inc; /* protected by listener lock */
	int			young_inc;/* protected by listener lock */

	/* following fields can be updated by timer */
	atomic_t		qlen_dec; /* qlen = qlen_inc - qlen_dec */
	atomic_t		young_dec;

	u8			max_qlen_log ____cacheline_aligned_in_smp;
	u8			synflood_warned;
	/* 2 bytes hole, try to use */
	u32			hash_rnd;
	u32			nr_table_entries;
	struct request_sock	*syn_table[0];
};

/*
 * For a TCP Fast Open listener -
 *	lock - protects the access to all the reqsk, which is co-owned by
 *		the listener and the child socket.
 *	qlen - pending TFO requests (still in TCP_SYN_RECV).
 *	max_qlen - max TFO reqs allowed before TFO is disabled.
 *
 *	XXX (TFO) - ideally these fields can be made as part of "listen_sock"
 *	structure above. But there is some implementation difficulty due to
 *	listen_sock being part of request_sock_queue hence will be freed when
 *	a listener is stopped. But TFO related fields may continue to be
 *	accessed even after a listener is closed, until its sk_refcnt drops
 *	to 0 implying no more outstanding TFO reqs. One solution is to keep
 *	listen_opt around until	sk_refcnt drops to 0. But there is some other
 *	complexity that needs to be resolved. E.g., a listener can be disabled
 *	temporarily through shutdown()->tcp_disconnect(), and re-enabled later.
 */
struct fastopen_queue {
	struct request_sock	*rskq_rst_head; /* Keep track of past TFO */
	struct request_sock	*rskq_rst_tail; /* requests that caused RST.
						 * This is part of the defense
						 * against spoofing attack.
						 */
	spinlock_t	lock;
	int		qlen;		/* # of pending (TCP_SYN_RECV) reqs */
	int		max_qlen;	/* != 0 iff TFO is currently enabled */
};

/** struct request_sock_queue - queue of request_socks
 *
 * @rskq_accept_head - FIFO head of established children
 * @rskq_accept_tail - FIFO tail of established children
 * @rskq_defer_accept - User waits for some data after accept()
 * @syn_wait_lock - serializer
 *
 * %syn_wait_lock is necessary only to avoid proc interface having to grab the main
 * lock sock while browsing the listening hash (otherwise it's deadlock prone).
 *
 * This lock is acquired in read mode only from listening_get_next() seq_file
 * op and it's acquired in write mode _only_ from code that is actively
 * changing rskq_accept_head. All readers that are holding the master sock lock
 * don't need to grab this lock in read mode too as rskq_accept_head. writes
 * are always protected from the main sock lock.
 */
struct request_sock_queue {
	struct request_sock	*rskq_accept_head;
	struct request_sock	*rskq_accept_tail;
	u8			rskq_defer_accept;
	struct listen_sock	*listen_opt;
	struct fastopen_queue	*fastopenq; /* This is non-NULL iff TFO has been
					     * enabled on this listener. Check
					     * max_qlen != 0 in fastopen_queue
					     * to determine if TFO is enabled
					     * right at this moment.
					     */

	/* temporary alignment, our goal is to get rid of this lock */
	rwlock_t		syn_wait_lock ____cacheline_aligned_in_smp;
};

int reqsk_queue_alloc(struct request_sock_queue *queue,
		      unsigned int nr_table_entries);

void __reqsk_queue_destroy(struct request_sock_queue *queue);
void reqsk_queue_destroy(struct request_sock_queue *queue);
void reqsk_fastopen_remove(struct sock *sk, struct request_sock *req,
			   bool reset);

static inline struct request_sock *
	reqsk_queue_yank_acceptq(struct request_sock_queue *queue)
{
	struct request_sock *req = queue->rskq_accept_head;

	queue->rskq_accept_head = NULL;
	return req;
}

static inline int reqsk_queue_empty(struct request_sock_queue *queue)
{
	return queue->rskq_accept_head == NULL;
}

static inline void reqsk_queue_unlink(struct request_sock_queue *queue,
				      struct request_sock *req)
{
	struct listen_sock *lopt = queue->listen_opt;
	struct request_sock **prev;

	write_lock(&queue->syn_wait_lock);

	prev = &lopt->syn_table[req->rsk_hash];
	while (*prev != req)
		prev = &(*prev)->dl_next;
	*prev = req->dl_next;

	write_unlock(&queue->syn_wait_lock);
	if (del_timer(&req->rsk_timer))
		reqsk_put(req);
}

static inline void reqsk_queue_add(struct request_sock_queue *queue,
				   struct request_sock *req,
				   struct sock *parent,
				   struct sock *child)
{
	req->sk = child;
	sk_acceptq_added(parent);

	if (queue->rskq_accept_head == NULL)
		queue->rskq_accept_head = req;
	else
		queue->rskq_accept_tail->dl_next = req;

	queue->rskq_accept_tail = req;
	req->dl_next = NULL;
}

static inline struct request_sock *reqsk_queue_remove(struct request_sock_queue *queue)
{
	struct request_sock *req = queue->rskq_accept_head;

	WARN_ON(req == NULL);

	queue->rskq_accept_head = req->dl_next;
	if (queue->rskq_accept_head == NULL)
		queue->rskq_accept_tail = NULL;

	return req;
}

static inline void reqsk_queue_removed(struct request_sock_queue *queue,
				       const struct request_sock *req)
{
	struct listen_sock *lopt = queue->listen_opt;

	if (req->num_timeout == 0)
		atomic_inc(&lopt->young_dec);
	atomic_inc(&lopt->qlen_dec);
}

static inline void reqsk_queue_added(struct request_sock_queue *queue)
{
	struct listen_sock *lopt = queue->listen_opt;

	lopt->young_inc++;
	lopt->qlen_inc++;
}

static inline int listen_sock_qlen(const struct listen_sock *lopt)
{
	return lopt->qlen_inc - atomic_read(&lopt->qlen_dec);
}

static inline int listen_sock_young(const struct listen_sock *lopt)
{
	return lopt->young_inc - atomic_read(&lopt->young_dec);
}

static inline int reqsk_queue_len(const struct request_sock_queue *queue)
{
	const struct listen_sock *lopt = queue->listen_opt;

	return lopt ? listen_sock_qlen(lopt) : 0;
}

static inline int reqsk_queue_len_young(const struct request_sock_queue *queue)
{
	return listen_sock_young(queue->listen_opt);
}

static inline int reqsk_queue_is_full(const struct request_sock_queue *queue)
{
	return reqsk_queue_len(queue) >> queue->listen_opt->max_qlen_log;
}

void reqsk_queue_hash_req(struct request_sock_queue *queue,
			  u32 hash, struct request_sock *req,
			  unsigned long timeout);

#endif /* _REQUEST_SOCK_H */
