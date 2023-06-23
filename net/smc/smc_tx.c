// SPDX-License-Identifier: GPL-2.0
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Manage send buffer.
 * Producer:
 * Copy user space data into send buffer, if send buffer space available.
 * Consumer:
 * Trigger RDMA write into RMBE of peer and send CDC, if RMBE space available.
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/tcp.h>

#include "smc.h"
#include "smc_wr.h"
#include "smc_cdc.h"
#include "smc_close.h"
#include "smc_ism.h"
#include "smc_tx.h"
#include "smc_stats.h"
#include "smc_tracepoint.h"

#define SMC_TX_WORK_DELAY	0

/***************************** sndbuf producer *******************************/

/* callback implementation for sk.sk_write_space()
 * to wakeup sndbuf producers that blocked with smc_tx_wait().
 * called under sk_socket lock.
 */
static void smc_tx_write_space(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;
	struct smc_sock *smc = smc_sk(sk);
	struct socket_wq *wq;

	/* similar to sk_stream_write_space */
	if (atomic_read(&smc->conn.sndbuf_space) && sock) {
		if (test_bit(SOCK_NOSPACE, &sock->flags))
			SMC_STAT_RMB_TX_FULL(smc, !smc->conn.lnk);
		clear_bit(SOCK_NOSPACE, &sock->flags);
		rcu_read_lock();
		wq = rcu_dereference(sk->sk_wq);
		if (skwq_has_sleeper(wq))
			wake_up_interruptible_poll(&wq->wait,
						   EPOLLOUT | EPOLLWRNORM |
						   EPOLLWRBAND);
		if (wq && wq->fasync_list && !(sk->sk_shutdown & SEND_SHUTDOWN))
			sock_wake_async(wq, SOCK_WAKE_SPACE, POLL_OUT);
		rcu_read_unlock();
	}
}

/* Wakeup sndbuf producers that blocked with smc_tx_wait().
 * Cf. tcp_data_snd_check()=>tcp_check_space()=>tcp_new_space().
 */
void smc_tx_sndbuf_nonfull(struct smc_sock *smc)
{
	if (smc->sk.sk_socket &&
	    test_bit(SOCK_NOSPACE, &smc->sk.sk_socket->flags))
		smc->sk.sk_write_space(&smc->sk);
}

/* blocks sndbuf producer until at least one byte of free space available
 * or urgent Byte was consumed
 */
static int smc_tx_wait(struct smc_sock *smc, int flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct smc_connection *conn = &smc->conn;
	struct sock *sk = &smc->sk;
	long timeo;
	int rc = 0;

	/* similar to sk_stream_wait_memory */
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	add_wait_queue(sk_sleep(sk), &wait);
	while (1) {
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		if (sk->sk_err ||
		    (sk->sk_shutdown & SEND_SHUTDOWN) ||
		    conn->killed ||
		    conn->local_tx_ctrl.conn_state_flags.peer_done_writing) {
			rc = -EPIPE;
			break;
		}
		if (smc_cdc_rxed_any_close(conn)) {
			rc = -ECONNRESET;
			break;
		}
		if (!timeo) {
			/* ensure EPOLLOUT is subsequently generated */
			set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			rc = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			rc = sock_intr_errno(timeo);
			break;
		}
		sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		if (atomic_read(&conn->sndbuf_space) && !conn->urg_tx_pend)
			break; /* at least 1 byte of free & no urgent data */
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		sk_wait_event(sk, &timeo,
			      READ_ONCE(sk->sk_err) ||
			      (READ_ONCE(sk->sk_shutdown) & SEND_SHUTDOWN) ||
			      smc_cdc_rxed_any_close(conn) ||
			      (atomic_read(&conn->sndbuf_space) &&
			       !conn->urg_tx_pend),
			      &wait);
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
}

static bool smc_tx_is_corked(struct smc_sock *smc)
{
	struct tcp_sock *tp = tcp_sk(smc->clcsock->sk);

	return (tp->nonagle & TCP_NAGLE_CORK) ? true : false;
}

/* If we have pending CDC messages, do not send:
 * Because CQE of this CDC message will happen shortly, it gives
 * a chance to coalesce future sendmsg() payload in to one RDMA Write,
 * without need for a timer, and with no latency trade off.
 * Algorithm here:
 *  1. First message should never cork
 *  2. If we have pending Tx CDC messages, wait for the first CDC
 *     message's completion
 *  3. Don't cork to much data in a single RDMA Write to prevent burst
 *     traffic, total corked message should not exceed sendbuf/2
 */
static bool smc_should_autocork(struct smc_sock *smc)
{
	struct smc_connection *conn = &smc->conn;
	int corking_size;

	corking_size = min_t(unsigned int, conn->sndbuf_desc->len >> 1,
			     sock_net(&smc->sk)->smc.sysctl_autocorking_size);

	if (atomic_read(&conn->cdc_pend_tx_wr) == 0 ||
	    smc_tx_prepared_sends(conn) > corking_size)
		return false;
	return true;
}

static bool smc_tx_should_cork(struct smc_sock *smc, struct msghdr *msg)
{
	struct smc_connection *conn = &smc->conn;

	if (smc_should_autocork(smc))
		return true;

	/* for a corked socket defer the RDMA writes if
	 * sndbuf_space is still available. The applications
	 * should known how/when to uncork it.
	 */
	if ((msg->msg_flags & MSG_MORE ||
	     smc_tx_is_corked(smc)) &&
	    atomic_read(&conn->sndbuf_space))
		return true;

	return false;
}

/* sndbuf producer: main API called by socket layer.
 * called under sock lock.
 */
int smc_tx_sendmsg(struct smc_sock *smc, struct msghdr *msg, size_t len)
{
	size_t copylen, send_done = 0, send_remaining = len;
	size_t chunk_len, chunk_off, chunk_len_sum;
	struct smc_connection *conn = &smc->conn;
	union smc_host_cursor prep;
	struct sock *sk = &smc->sk;
	char *sndbuf_base;
	int tx_cnt_prep;
	int writespace;
	int rc, chunk;

	/* This should be in poll */
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN)) {
		rc = -EPIPE;
		goto out_err;
	}

	if (sk->sk_state == SMC_INIT)
		return -ENOTCONN;

	if (len > conn->sndbuf_desc->len)
		SMC_STAT_RMB_TX_SIZE_SMALL(smc, !conn->lnk);

	if (len > conn->peer_rmbe_size)
		SMC_STAT_RMB_TX_PEER_SIZE_SMALL(smc, !conn->lnk);

	if (msg->msg_flags & MSG_OOB)
		SMC_STAT_INC(smc, urg_data_cnt);

	while (msg_data_left(msg)) {
		if (smc->sk.sk_shutdown & SEND_SHUTDOWN ||
		    (smc->sk.sk_err == ECONNABORTED) ||
		    conn->killed)
			return -EPIPE;
		if (smc_cdc_rxed_any_close(conn))
			return send_done ?: -ECONNRESET;

		if (msg->msg_flags & MSG_OOB)
			conn->local_tx_ctrl.prod_flags.urg_data_pending = 1;

		if (!atomic_read(&conn->sndbuf_space) || conn->urg_tx_pend) {
			if (send_done)
				return send_done;
			rc = smc_tx_wait(smc, msg->msg_flags);
			if (rc)
				goto out_err;
			continue;
		}

		/* initialize variables for 1st iteration of subsequent loop */
		/* could be just 1 byte, even after smc_tx_wait above */
		writespace = atomic_read(&conn->sndbuf_space);
		/* not more than what user space asked for */
		copylen = min_t(size_t, send_remaining, writespace);
		/* determine start of sndbuf */
		sndbuf_base = conn->sndbuf_desc->cpu_addr;
		smc_curs_copy(&prep, &conn->tx_curs_prep, conn);
		tx_cnt_prep = prep.count;
		/* determine chunks where to write into sndbuf */
		/* either unwrapped case, or 1st chunk of wrapped case */
		chunk_len = min_t(size_t, copylen, conn->sndbuf_desc->len -
				  tx_cnt_prep);
		chunk_len_sum = chunk_len;
		chunk_off = tx_cnt_prep;
		for (chunk = 0; chunk < 2; chunk++) {
			rc = memcpy_from_msg(sndbuf_base + chunk_off,
					     msg, chunk_len);
			if (rc) {
				smc_sndbuf_sync_sg_for_device(conn);
				if (send_done)
					return send_done;
				goto out_err;
			}
			send_done += chunk_len;
			send_remaining -= chunk_len;

			if (chunk_len_sum == copylen)
				break; /* either on 1st or 2nd iteration */
			/* prepare next (== 2nd) iteration */
			chunk_len = copylen - chunk_len; /* remainder */
			chunk_len_sum += chunk_len;
			chunk_off = 0; /* modulo offset in send ring buffer */
		}
		smc_sndbuf_sync_sg_for_device(conn);
		/* update cursors */
		smc_curs_add(conn->sndbuf_desc->len, &prep, copylen);
		smc_curs_copy(&conn->tx_curs_prep, &prep, conn);
		/* increased in send tasklet smc_cdc_tx_handler() */
		smp_mb__before_atomic();
		atomic_sub(copylen, &conn->sndbuf_space);
		/* guarantee 0 <= sndbuf_space <= sndbuf_desc->len */
		smp_mb__after_atomic();
		/* since we just produced more new data into sndbuf,
		 * trigger sndbuf consumer: RDMA write into peer RMBE and CDC
		 */
		if ((msg->msg_flags & MSG_OOB) && !send_remaining)
			conn->urg_tx_pend = true;
		/* If we need to cork, do nothing and wait for the next
		 * sendmsg() call or push on tx completion
		 */
		if (!smc_tx_should_cork(smc, msg))
			smc_tx_sndbuf_nonempty(conn);

		trace_smc_tx_sendmsg(smc, copylen);
	} /* while (msg_data_left(msg)) */

	return send_done;

out_err:
	rc = sk_stream_error(sk, msg->msg_flags, rc);
	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(rc == -EAGAIN))
		sk->sk_write_space(sk);
	return rc;
}

int smc_tx_sendpage(struct smc_sock *smc, struct page *page, int offset,
		    size_t size, int flags)
{
	struct msghdr msg = {.msg_flags = flags};
	char *kaddr = kmap(page);
	struct kvec iov;
	int rc;

	if (flags & MSG_SENDPAGE_NOTLAST)
		msg.msg_flags |= MSG_MORE;

	iov.iov_base = kaddr + offset;
	iov.iov_len = size;
	iov_iter_kvec(&msg.msg_iter, ITER_SOURCE, &iov, 1, size);
	rc = smc_tx_sendmsg(smc, &msg, size);
	kunmap(page);
	return rc;
}

/***************************** sndbuf consumer *******************************/

/* sndbuf consumer: actual data transfer of one target chunk with ISM write */
int smcd_tx_ism_write(struct smc_connection *conn, void *data, size_t len,
		      u32 offset, int signal)
{
	int rc;

	rc = smc_ism_write(conn->lgr->smcd, conn->peer_token,
			   conn->peer_rmbe_idx, signal, conn->tx_off + offset,
			   data, len);
	if (rc)
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
	return rc;
}

/* sndbuf consumer: actual data transfer of one target chunk with RDMA write */
static int smc_tx_rdma_write(struct smc_connection *conn, int peer_rmbe_offset,
			     int num_sges, struct ib_rdma_wr *rdma_wr)
{
	struct smc_link_group *lgr = conn->lgr;
	struct smc_link *link = conn->lnk;
	int rc;

	rdma_wr->wr.wr_id = smc_wr_tx_get_next_wr_id(link);
	rdma_wr->wr.num_sge = num_sges;
	rdma_wr->remote_addr =
		lgr->rtokens[conn->rtoken_idx][link->link_idx].dma_addr +
		/* RMBE within RMB */
		conn->tx_off +
		/* offset within RMBE */
		peer_rmbe_offset;
	rdma_wr->rkey = lgr->rtokens[conn->rtoken_idx][link->link_idx].rkey;
	rc = ib_post_send(link->roce_qp, &rdma_wr->wr, NULL);
	if (rc)
		smcr_link_down_cond_sched(link);
	return rc;
}

/* sndbuf consumer */
static inline void smc_tx_advance_cursors(struct smc_connection *conn,
					  union smc_host_cursor *prod,
					  union smc_host_cursor *sent,
					  size_t len)
{
	smc_curs_add(conn->peer_rmbe_size, prod, len);
	/* increased in recv tasklet smc_cdc_msg_rcv() */
	smp_mb__before_atomic();
	/* data in flight reduces usable snd_wnd */
	atomic_sub(len, &conn->peer_rmbe_space);
	/* guarantee 0 <= peer_rmbe_space <= peer_rmbe_size */
	smp_mb__after_atomic();
	smc_curs_add(conn->sndbuf_desc->len, sent, len);
}

/* SMC-R helper for smc_tx_rdma_writes() */
static int smcr_tx_rdma_writes(struct smc_connection *conn, size_t len,
			       size_t src_off, size_t src_len,
			       size_t dst_off, size_t dst_len,
			       struct smc_rdma_wr *wr_rdma_buf)
{
	struct smc_link *link = conn->lnk;

	dma_addr_t dma_addr =
		sg_dma_address(conn->sndbuf_desc->sgt[link->link_idx].sgl);
	u64 virt_addr = (uintptr_t)conn->sndbuf_desc->cpu_addr;
	int src_len_sum = src_len, dst_len_sum = dst_len;
	int sent_count = src_off;
	int srcchunk, dstchunk;
	int num_sges;
	int rc;

	for (dstchunk = 0; dstchunk < 2; dstchunk++) {
		struct ib_rdma_wr *wr = &wr_rdma_buf->wr_tx_rdma[dstchunk];
		struct ib_sge *sge = wr->wr.sg_list;
		u64 base_addr = dma_addr;

		if (dst_len < link->qp_attr.cap.max_inline_data) {
			base_addr = virt_addr;
			wr->wr.send_flags |= IB_SEND_INLINE;
		} else {
			wr->wr.send_flags &= ~IB_SEND_INLINE;
		}

		num_sges = 0;
		for (srcchunk = 0; srcchunk < 2; srcchunk++) {
			sge[srcchunk].addr = conn->sndbuf_desc->is_vm ?
				(virt_addr + src_off) : (base_addr + src_off);
			sge[srcchunk].length = src_len;
			if (conn->sndbuf_desc->is_vm)
				sge[srcchunk].lkey =
					conn->sndbuf_desc->mr[link->link_idx]->lkey;
			num_sges++;

			src_off += src_len;
			if (src_off >= conn->sndbuf_desc->len)
				src_off -= conn->sndbuf_desc->len;
						/* modulo in send ring */
			if (src_len_sum == dst_len)
				break; /* either on 1st or 2nd iteration */
			/* prepare next (== 2nd) iteration */
			src_len = dst_len - src_len; /* remainder */
			src_len_sum += src_len;
		}
		rc = smc_tx_rdma_write(conn, dst_off, num_sges, wr);
		if (rc)
			return rc;
		if (dst_len_sum == len)
			break; /* either on 1st or 2nd iteration */
		/* prepare next (== 2nd) iteration */
		dst_off = 0; /* modulo offset in RMBE ring buffer */
		dst_len = len - dst_len; /* remainder */
		dst_len_sum += dst_len;
		src_len = min_t(int, dst_len, conn->sndbuf_desc->len -
				sent_count);
		src_len_sum = src_len;
	}
	return 0;
}

/* SMC-D helper for smc_tx_rdma_writes() */
static int smcd_tx_rdma_writes(struct smc_connection *conn, size_t len,
			       size_t src_off, size_t src_len,
			       size_t dst_off, size_t dst_len)
{
	int src_len_sum = src_len, dst_len_sum = dst_len;
	int srcchunk, dstchunk;
	int rc;

	for (dstchunk = 0; dstchunk < 2; dstchunk++) {
		for (srcchunk = 0; srcchunk < 2; srcchunk++) {
			void *data = conn->sndbuf_desc->cpu_addr + src_off;

			rc = smcd_tx_ism_write(conn, data, src_len, dst_off +
					       sizeof(struct smcd_cdc_msg), 0);
			if (rc)
				return rc;
			dst_off += src_len;
			src_off += src_len;
			if (src_off >= conn->sndbuf_desc->len)
				src_off -= conn->sndbuf_desc->len;
						/* modulo in send ring */
			if (src_len_sum == dst_len)
				break; /* either on 1st or 2nd iteration */
			/* prepare next (== 2nd) iteration */
			src_len = dst_len - src_len; /* remainder */
			src_len_sum += src_len;
		}
		if (dst_len_sum == len)
			break; /* either on 1st or 2nd iteration */
		/* prepare next (== 2nd) iteration */
		dst_off = 0; /* modulo offset in RMBE ring buffer */
		dst_len = len - dst_len; /* remainder */
		dst_len_sum += dst_len;
		src_len = min_t(int, dst_len, conn->sndbuf_desc->len - src_off);
		src_len_sum = src_len;
	}
	return 0;
}

/* sndbuf consumer: prepare all necessary (src&dst) chunks of data transmit;
 * usable snd_wnd as max transmit
 */
static int smc_tx_rdma_writes(struct smc_connection *conn,
			      struct smc_rdma_wr *wr_rdma_buf)
{
	size_t len, src_len, dst_off, dst_len; /* current chunk values */
	union smc_host_cursor sent, prep, prod, cons;
	struct smc_cdc_producer_flags *pflags;
	int to_send, rmbespace;
	int rc;

	/* source: sndbuf */
	smc_curs_copy(&sent, &conn->tx_curs_sent, conn);
	smc_curs_copy(&prep, &conn->tx_curs_prep, conn);
	/* cf. wmem_alloc - (snd_max - snd_una) */
	to_send = smc_curs_diff(conn->sndbuf_desc->len, &sent, &prep);
	if (to_send <= 0)
		return 0;

	/* destination: RMBE */
	/* cf. snd_wnd */
	rmbespace = atomic_read(&conn->peer_rmbe_space);
	if (rmbespace <= 0) {
		struct smc_sock *smc = container_of(conn, struct smc_sock,
						    conn);
		SMC_STAT_RMB_TX_PEER_FULL(smc, !conn->lnk);
		return 0;
	}
	smc_curs_copy(&prod, &conn->local_tx_ctrl.prod, conn);
	smc_curs_copy(&cons, &conn->local_rx_ctrl.cons, conn);

	/* if usable snd_wnd closes ask peer to advertise once it opens again */
	pflags = &conn->local_tx_ctrl.prod_flags;
	pflags->write_blocked = (to_send >= rmbespace);
	/* cf. usable snd_wnd */
	len = min(to_send, rmbespace);

	/* initialize variables for first iteration of subsequent nested loop */
	dst_off = prod.count;
	if (prod.wrap == cons.wrap) {
		/* the filled destination area is unwrapped,
		 * hence the available free destination space is wrapped
		 * and we need 2 destination chunks of sum len; start with 1st
		 * which is limited by what's available in sndbuf
		 */
		dst_len = min_t(size_t,
				conn->peer_rmbe_size - prod.count, len);
	} else {
		/* the filled destination area is wrapped,
		 * hence the available free destination space is unwrapped
		 * and we need a single destination chunk of entire len
		 */
		dst_len = len;
	}
	/* dst_len determines the maximum src_len */
	if (sent.count + dst_len <= conn->sndbuf_desc->len) {
		/* unwrapped src case: single chunk of entire dst_len */
		src_len = dst_len;
	} else {
		/* wrapped src case: 2 chunks of sum dst_len; start with 1st: */
		src_len = conn->sndbuf_desc->len - sent.count;
	}

	if (conn->lgr->is_smcd)
		rc = smcd_tx_rdma_writes(conn, len, sent.count, src_len,
					 dst_off, dst_len);
	else
		rc = smcr_tx_rdma_writes(conn, len, sent.count, src_len,
					 dst_off, dst_len, wr_rdma_buf);
	if (rc)
		return rc;

	if (conn->urg_tx_pend && len == to_send)
		pflags->urg_data_present = 1;
	smc_tx_advance_cursors(conn, &prod, &sent, len);
	/* update connection's cursors with advanced local cursors */
	smc_curs_copy(&conn->local_tx_ctrl.prod, &prod, conn);
							/* dst: peer RMBE */
	smc_curs_copy(&conn->tx_curs_sent, &sent, conn);/* src: local sndbuf */

	return 0;
}

/* Wakeup sndbuf consumers from any context (IRQ or process)
 * since there is more data to transmit; usable snd_wnd as max transmit
 */
static int smcr_tx_sndbuf_nonempty(struct smc_connection *conn)
{
	struct smc_cdc_producer_flags *pflags = &conn->local_tx_ctrl.prod_flags;
	struct smc_link *link = conn->lnk;
	struct smc_rdma_wr *wr_rdma_buf;
	struct smc_cdc_tx_pend *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!link || !smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_cdc_get_free_slot(conn, link, &wr_buf, &wr_rdma_buf, &pend);
	if (rc < 0) {
		smc_wr_tx_link_put(link);
		if (rc == -EBUSY) {
			struct smc_sock *smc =
				container_of(conn, struct smc_sock, conn);

			if (smc->sk.sk_err == ECONNABORTED)
				return sock_error(&smc->sk);
			if (conn->killed)
				return -EPIPE;
			rc = 0;
			mod_delayed_work(conn->lgr->tx_wq, &conn->tx_work,
					 SMC_TX_WORK_DELAY);
		}
		return rc;
	}

	spin_lock_bh(&conn->send_lock);
	if (link != conn->lnk) {
		/* link of connection changed, tx_work will restart */
		smc_wr_tx_put_slot(link,
				   (struct smc_wr_tx_pend_priv *)pend);
		rc = -ENOLINK;
		goto out_unlock;
	}
	if (!pflags->urg_data_present) {
		rc = smc_tx_rdma_writes(conn, wr_rdma_buf);
		if (rc) {
			smc_wr_tx_put_slot(link,
					   (struct smc_wr_tx_pend_priv *)pend);
			goto out_unlock;
		}
	}

	rc = smc_cdc_msg_send(conn, wr_buf, pend);
	if (!rc && pflags->urg_data_present) {
		pflags->urg_data_pending = 0;
		pflags->urg_data_present = 0;
	}

out_unlock:
	spin_unlock_bh(&conn->send_lock);
	smc_wr_tx_link_put(link);
	return rc;
}

static int smcd_tx_sndbuf_nonempty(struct smc_connection *conn)
{
	struct smc_cdc_producer_flags *pflags = &conn->local_tx_ctrl.prod_flags;
	int rc = 0;

	spin_lock_bh(&conn->send_lock);
	if (!pflags->urg_data_present)
		rc = smc_tx_rdma_writes(conn, NULL);
	if (!rc)
		rc = smcd_cdc_msg_send(conn);

	if (!rc && pflags->urg_data_present) {
		pflags->urg_data_pending = 0;
		pflags->urg_data_present = 0;
	}
	spin_unlock_bh(&conn->send_lock);
	return rc;
}

static int __smc_tx_sndbuf_nonempty(struct smc_connection *conn)
{
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);
	int rc = 0;

	/* No data in the send queue */
	if (unlikely(smc_tx_prepared_sends(conn) <= 0))
		goto out;

	/* Peer don't have RMBE space */
	if (unlikely(atomic_read(&conn->peer_rmbe_space) <= 0)) {
		SMC_STAT_RMB_TX_PEER_FULL(smc, !conn->lnk);
		goto out;
	}

	if (conn->killed ||
	    conn->local_rx_ctrl.conn_state_flags.peer_conn_abort) {
		rc = -EPIPE;    /* connection being aborted */
		goto out;
	}
	if (conn->lgr->is_smcd)
		rc = smcd_tx_sndbuf_nonempty(conn);
	else
		rc = smcr_tx_sndbuf_nonempty(conn);

	if (!rc) {
		/* trigger socket release if connection is closing */
		smc_close_wake_tx_prepared(smc);
	}

out:
	return rc;
}

int smc_tx_sndbuf_nonempty(struct smc_connection *conn)
{
	int rc;

	/* This make sure only one can send simultaneously to prevent wasting
	 * of CPU and CDC slot.
	 * Record whether someone has tried to push while we are pushing.
	 */
	if (atomic_inc_return(&conn->tx_pushing) > 1)
		return 0;

again:
	atomic_set(&conn->tx_pushing, 1);
	smp_wmb(); /* Make sure tx_pushing is 1 before real send */
	rc = __smc_tx_sndbuf_nonempty(conn);

	/* We need to check whether someone else have added some data into
	 * the send queue and tried to push but failed after the atomic_set()
	 * when we are pushing.
	 * If so, we need to push again to prevent those data hang in the send
	 * queue.
	 */
	if (unlikely(!atomic_dec_and_test(&conn->tx_pushing)))
		goto again;

	return rc;
}

/* Wakeup sndbuf consumers from process context
 * since there is more data to transmit. The caller
 * must hold sock lock.
 */
void smc_tx_pending(struct smc_connection *conn)
{
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);
	int rc;

	if (smc->sk.sk_err)
		return;

	rc = smc_tx_sndbuf_nonempty(conn);
	if (!rc && conn->local_rx_ctrl.prod_flags.write_blocked &&
	    !atomic_read(&conn->bytes_to_rcv))
		conn->local_rx_ctrl.prod_flags.write_blocked = 0;
}

/* Wakeup sndbuf consumers from process context
 * since there is more data to transmit in locked
 * sock.
 */
void smc_tx_work(struct work_struct *work)
{
	struct smc_connection *conn = container_of(to_delayed_work(work),
						   struct smc_connection,
						   tx_work);
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);

	lock_sock(&smc->sk);
	smc_tx_pending(conn);
	release_sock(&smc->sk);
}

void smc_tx_consumer_update(struct smc_connection *conn, bool force)
{
	union smc_host_cursor cfed, cons, prod;
	int sender_free = conn->rmb_desc->len;
	int to_confirm;

	smc_curs_copy(&cons, &conn->local_tx_ctrl.cons, conn);
	smc_curs_copy(&cfed, &conn->rx_curs_confirmed, conn);
	to_confirm = smc_curs_diff(conn->rmb_desc->len, &cfed, &cons);
	if (to_confirm > conn->rmbe_update_limit) {
		smc_curs_copy(&prod, &conn->local_rx_ctrl.prod, conn);
		sender_free = conn->rmb_desc->len -
			      smc_curs_diff_large(conn->rmb_desc->len,
						  &cfed, &prod);
	}

	if (conn->local_rx_ctrl.prod_flags.cons_curs_upd_req ||
	    force ||
	    ((to_confirm > conn->rmbe_update_limit) &&
	     ((sender_free <= (conn->rmb_desc->len / 2)) ||
	      conn->local_rx_ctrl.prod_flags.write_blocked))) {
		if (conn->killed ||
		    conn->local_rx_ctrl.conn_state_flags.peer_conn_abort)
			return;
		if ((smc_cdc_get_slot_and_msg_send(conn) < 0) &&
		    !conn->killed) {
			queue_delayed_work(conn->lgr->tx_wq, &conn->tx_work,
					   SMC_TX_WORK_DELAY);
			return;
		}
	}
	if (conn->local_rx_ctrl.prod_flags.write_blocked &&
	    !atomic_read(&conn->bytes_to_rcv))
		conn->local_rx_ctrl.prod_flags.write_blocked = 0;
}

/***************************** send initialize *******************************/

/* Initialize send properties on connection establishment. NB: not __init! */
void smc_tx_init(struct smc_sock *smc)
{
	smc->sk.sk_write_space = smc_tx_write_space;
}
