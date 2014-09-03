/*
 * NET		Generic infrastructure for Network protocols.
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

#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tcp.h>
#include <linux/vmalloc.h>

#include <net/request_sock.h>

/*
 * Maximum number of SYN_RECV sockets in queue per LISTEN socket.
 * One SYN_RECV socket costs about 80bytes on a 32bit machine.
 * It would be better to replace it with a global counter for all sockets
 * but then some measure against one socket starving all other sockets
 * would be needed.
 *
 * The minimum value of it is 128. Experiments with real servers show that
 * it is absolutely not enough even at 100conn/sec. 256 cures most
 * of problems.
 * This value is adjusted to 128 for low memory machines,
 * and it will increase in proportion to the memory of machine.
 * Note : Dont forget somaxconn that may limit backlog too.
 */
int sysctl_max_syn_backlog = 256;
EXPORT_SYMBOL(sysctl_max_syn_backlog);

int reqsk_queue_alloc(struct request_sock_queue *queue,
		      unsigned int nr_table_entries)
{
	size_t lopt_size = sizeof(struct listen_sock);
	struct listen_sock *lopt = NULL;

	nr_table_entries = min_t(u32, nr_table_entries, sysctl_max_syn_backlog);
	nr_table_entries = max_t(u32, nr_table_entries, 8);
	nr_table_entries = roundup_pow_of_two(nr_table_entries + 1);
	lopt_size += nr_table_entries * sizeof(struct request_sock *);

	if (lopt_size <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER))
		lopt = kzalloc(lopt_size, GFP_KERNEL |
					  __GFP_NOWARN |
					  __GFP_NORETRY);
	if (!lopt)
		lopt = vzalloc(lopt_size);
	if (!lopt)
		return -ENOMEM;

	get_random_bytes(&lopt->hash_rnd, sizeof(lopt->hash_rnd));
	rwlock_init(&queue->syn_wait_lock);
	queue->rskq_accept_head = NULL;
	lopt->nr_table_entries = nr_table_entries;
	lopt->max_qlen_log = ilog2(nr_table_entries);

	write_lock_bh(&queue->syn_wait_lock);
	queue->listen_opt = lopt;
	write_unlock_bh(&queue->syn_wait_lock);

	return 0;
}

void __reqsk_queue_destroy(struct request_sock_queue *queue)
{
	/* This is an error recovery path only, no locking needed */
	kvfree(queue->listen_opt);
}

static inline struct listen_sock *reqsk_queue_yank_listen_sk(
		struct request_sock_queue *queue)
{
	struct listen_sock *lopt;

	write_lock_bh(&queue->syn_wait_lock);
	lopt = queue->listen_opt;
	queue->listen_opt = NULL;
	write_unlock_bh(&queue->syn_wait_lock);

	return lopt;
}

void reqsk_queue_destroy(struct request_sock_queue *queue)
{
	/* make all the listen_opt local to us */
	struct listen_sock *lopt = reqsk_queue_yank_listen_sk(queue);

	if (lopt->qlen != 0) {
		unsigned int i;

		for (i = 0; i < lopt->nr_table_entries; i++) {
			struct request_sock *req;

			while ((req = lopt->syn_table[i]) != NULL) {
				lopt->syn_table[i] = req->dl_next;
				lopt->qlen--;
				reqsk_free(req);
			}
		}
	}

	WARN_ON(lopt->qlen != 0);
	kvfree(lopt);
}

/*
 * This function is called to set a Fast Open socket's "fastopen_rsk" field
 * to NULL when a TFO socket no longer needs to access the request_sock.
 * This happens only after 3WHS has been either completed or aborted (e.g.,
 * RST is received).
 *
 * Before TFO, a child socket is created only after 3WHS is completed,
 * hence it never needs to access the request_sock. things get a lot more
 * complex with TFO. A child socket, accepted or not, has to access its
 * request_sock for 3WHS processing, e.g., to retransmit SYN-ACK pkts,
 * until 3WHS is either completed or aborted. Afterwards the req will stay
 * until either the child socket is accepted, or in the rare case when the
 * listener is closed before the child is accepted.
 *
 * In short, a request socket is only freed after BOTH 3WHS has completed
 * (or aborted) and the child socket has been accepted (or listener closed).
 * When a child socket is accepted, its corresponding req->sk is set to
 * NULL since it's no longer needed. More importantly, "req->sk == NULL"
 * will be used by the code below to determine if a child socket has been
 * accepted or not, and the check is protected by the fastopenq->lock
 * described below.
 *
 * Note that fastopen_rsk is only accessed from the child socket's context
 * with its socket lock held. But a request_sock (req) can be accessed by
 * both its child socket through fastopen_rsk, and a listener socket through
 * icsk_accept_queue.rskq_accept_head. To protect the access a simple spin
 * lock per listener "icsk->icsk_accept_queue.fastopenq->lock" is created.
 * only in the rare case when both the listener and the child locks are held,
 * e.g., in inet_csk_listen_stop() do we not need to acquire the lock.
 * The lock also protects other fields such as fastopenq->qlen, which is
 * decremented by this function when fastopen_rsk is no longer needed.
 *
 * Note that another solution was to simply use the existing socket lock
 * from the listener. But first socket lock is difficult to use. It is not
 * a simple spin lock - one must consider sock_owned_by_user() and arrange
 * to use sk_add_backlog() stuff. But what really makes it infeasible is the
 * locking hierarchy violation. E.g., inet_csk_listen_stop() may try to
 * acquire a child's lock while holding listener's socket lock. A corner
 * case might also exist in tcp_v4_hnd_req() that will trigger this locking
 * order.
 *
 * When a TFO req is created, it needs to sock_hold its listener to prevent
 * the latter data structure from going away.
 *
 * This function also sets "treq->listener" to NULL and unreference listener
 * socket. treq->listener is used by the listener so it is protected by the
 * fastopenq->lock in this function.
 */
void reqsk_fastopen_remove(struct sock *sk, struct request_sock *req,
			   bool reset)
{
	struct sock *lsk = tcp_rsk(req)->listener;
	struct fastopen_queue *fastopenq =
	    inet_csk(lsk)->icsk_accept_queue.fastopenq;

	tcp_sk(sk)->fastopen_rsk = NULL;
	spin_lock_bh(&fastopenq->lock);
	fastopenq->qlen--;
	tcp_rsk(req)->listener = NULL;
	if (req->sk)	/* the child socket hasn't been accepted yet */
		goto out;

	if (!reset || lsk->sk_state != TCP_LISTEN) {
		/* If the listener has been closed don't bother with the
		 * special RST handling below.
		 */
		spin_unlock_bh(&fastopenq->lock);
		sock_put(lsk);
		reqsk_free(req);
		return;
	}
	/* Wait for 60secs before removing a req that has triggered RST.
	 * This is a simple defense against TFO spoofing attack - by
	 * counting the req against fastopen.max_qlen, and disabling
	 * TFO when the qlen exceeds max_qlen.
	 *
	 * For more details see CoNext'11 "TCP Fast Open" paper.
	 */
	req->expires = jiffies + 60*HZ;
	if (fastopenq->rskq_rst_head == NULL)
		fastopenq->rskq_rst_head = req;
	else
		fastopenq->rskq_rst_tail->dl_next = req;

	req->dl_next = NULL;
	fastopenq->rskq_rst_tail = req;
	fastopenq->qlen++;
out:
	spin_unlock_bh(&fastopenq->lock);
	sock_put(lsk);
}
