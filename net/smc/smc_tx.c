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

#include "smc.h"
#include "smc_wr.h"
#include "smc_cdc.h"
#include "smc_tx.h"

/***************************** sndbuf producer *******************************/

/* callback implementation for sk.sk_write_space()
 * to wakeup sndbuf producers that blocked with smc_tx_wait_memory().
 * called under sk_socket lock.
 */
static void smc_tx_write_space(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;
	struct smc_sock *smc = smc_sk(sk);
	struct socket_wq *wq;

	/* similar to sk_stream_write_space */
	if (atomic_read(&smc->conn.sndbuf_space) && sock) {
		clear_bit(SOCK_NOSPACE, &sock->flags);
		rcu_read_lock();
		wq = rcu_dereference(sk->sk_wq);
		if (skwq_has_sleeper(wq))
			wake_up_interruptible_poll(&wq->wait,
						   POLLOUT | POLLWRNORM |
						   POLLWRBAND);
		if (wq && wq->fasync_list && !(sk->sk_shutdown & SEND_SHUTDOWN))
			sock_wake_async(wq, SOCK_WAKE_SPACE, POLL_OUT);
		rcu_read_unlock();
	}
}

/* Wakeup sndbuf producers that blocked with smc_tx_wait_memory().
 * Cf. tcp_data_snd_check()=>tcp_check_space()=>tcp_new_space().
 */
void smc_tx_sndbuf_nonfull(struct smc_sock *smc)
{
	if (smc->sk.sk_socket &&
	    test_bit(SOCK_NOSPACE, &smc->sk.sk_socket->flags))
		smc->sk.sk_write_space(&smc->sk);
}

/* blocks sndbuf producer until at least one byte of free space available */
static int smc_tx_wait_memory(struct smc_sock *smc, int flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct smc_connection *conn = &smc->conn;
	struct sock *sk = &smc->sk;
	bool noblock;
	long timeo;
	int rc = 0;

	/* similar to sk_stream_wait_memory */
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	noblock = timeo ? false : true;
	add_wait_queue(sk_sleep(sk), &wait);
	while (1) {
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		if (sk->sk_err ||
		    (sk->sk_shutdown & SEND_SHUTDOWN) ||
		    conn->local_tx_ctrl.conn_state_flags.peer_done_writing) {
			rc = -EPIPE;
			break;
		}
		if (conn->local_rx_ctrl.conn_state_flags.peer_conn_abort) {
			rc = -ECONNRESET;
			break;
		}
		if (!timeo) {
			if (noblock)
				set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			rc = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			rc = sock_intr_errno(timeo);
			break;
		}
		sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		if (atomic_read(&conn->sndbuf_space))
			break; /* at least 1 byte of free space available */
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		sk->sk_write_pending++;
		sk_wait_event(sk, &timeo,
			      sk->sk_err ||
			      (sk->sk_shutdown & SEND_SHUTDOWN) ||
			      smc_cdc_rxed_any_close_or_senddone(conn) ||
			      atomic_read(&conn->sndbuf_space),
			      &wait);
		sk->sk_write_pending--;
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
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

	while (msg_data_left(msg)) {
		if (sk->sk_state == SMC_INIT)
			return -ENOTCONN;
		if (smc->sk.sk_shutdown & SEND_SHUTDOWN ||
		    (smc->sk.sk_err == ECONNABORTED) ||
		    conn->local_tx_ctrl.conn_state_flags.peer_conn_abort)
			return -EPIPE;
		if (smc_cdc_rxed_any_close(conn))
			return send_done ?: -ECONNRESET;

		if (!atomic_read(&conn->sndbuf_space)) {
			rc = smc_tx_wait_memory(smc, msg->msg_flags);
			if (rc) {
				if (send_done)
					return send_done;
				goto out_err;
			}
			continue;
		}

		/* initialize variables for 1st iteration of subsequent loop */
		/* could be just 1 byte, even after smc_tx_wait_memory above */
		writespace = atomic_read(&conn->sndbuf_space);
		/* not more than what user space asked for */
		copylen = min_t(size_t, send_remaining, writespace);
		/* determine start of sndbuf */
		sndbuf_base = conn->sndbuf_desc->cpu_addr;
		smc_curs_write(&prep,
			       smc_curs_read(&conn->tx_curs_prep, conn),
			       conn);
		tx_cnt_prep = prep.count;
		/* determine chunks where to write into sndbuf */
		/* either unwrapped case, or 1st chunk of wrapped case */
		chunk_len = min_t(size_t,
				  copylen, conn->sndbuf_size - tx_cnt_prep);
		chunk_len_sum = chunk_len;
		chunk_off = tx_cnt_prep;
		smc_sndbuf_sync_sg_for_cpu(conn);
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
		smc_curs_add(conn->sndbuf_size, &prep, copylen);
		smc_curs_write(&conn->tx_curs_prep,
			       smc_curs_read(&prep, conn),
			       conn);
		/* increased in send tasklet smc_cdc_tx_handler() */
		smp_mb__before_atomic();
		atomic_sub(copylen, &conn->sndbuf_space);
		/* guarantee 0 <= sndbuf_space <= sndbuf_size */
		smp_mb__after_atomic();
		/* since we just produced more new data into sndbuf,
		 * trigger sndbuf consumer: RDMA write into peer RMBE and CDC
		 */
		smc_tx_sndbuf_nonempty(conn);
	} /* while (msg_data_left(msg)) */

	return send_done;

out_err:
	rc = sk_stream_error(sk, msg->msg_flags, rc);
	/* make sure we wake any epoll edge trigger waiter */
	if (unlikely(rc == -EAGAIN))
		sk->sk_write_space(sk);
	return rc;
}

/***************************** sndbuf consumer *******************************/

/* sndbuf consumer: actual data transfer of one target chunk with RDMA write */
static int smc_tx_rdma_write(struct smc_connection *conn, int peer_rmbe_offset,
			     int num_sges, struct ib_sge sges[])
{
	struct smc_link_group *lgr = conn->lgr;
	struct ib_send_wr *failed_wr = NULL;
	struct ib_rdma_wr rdma_wr;
	struct smc_link *link;
	int rc;

	memset(&rdma_wr, 0, sizeof(rdma_wr));
	link = &lgr->lnk[SMC_SINGLE_LINK];
	rdma_wr.wr.wr_id = smc_wr_tx_get_next_wr_id(link);
	rdma_wr.wr.sg_list = sges;
	rdma_wr.wr.num_sge = num_sges;
	rdma_wr.wr.opcode = IB_WR_RDMA_WRITE;
	rdma_wr.remote_addr =
		lgr->rtokens[conn->rtoken_idx][SMC_SINGLE_LINK].dma_addr +
		/* RMBE within RMB */
		((conn->peer_conn_idx - 1) * conn->peer_rmbe_size) +
		/* offset within RMBE */
		peer_rmbe_offset;
	rdma_wr.rkey = lgr->rtokens[conn->rtoken_idx][SMC_SINGLE_LINK].rkey;
	rc = ib_post_send(link->roce_qp, &rdma_wr.wr, &failed_wr);
	if (rc)
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
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
	smc_curs_add(conn->sndbuf_size, sent, len);
}

/* sndbuf consumer: prepare all necessary (src&dst) chunks of data transmit;
 * usable snd_wnd as max transmit
 */
static int smc_tx_rdma_writes(struct smc_connection *conn)
{
	size_t src_off, src_len, dst_off, dst_len; /* current chunk values */
	size_t len, dst_len_sum, src_len_sum, dstchunk, srcchunk;
	union smc_host_cursor sent, prep, prod, cons;
	struct ib_sge sges[SMC_IB_MAX_SEND_SGE];
	struct smc_link_group *lgr = conn->lgr;
	int to_send, rmbespace;
	struct smc_link *link;
	dma_addr_t dma_addr;
	int num_sges;
	int rc;

	/* source: sndbuf */
	smc_curs_write(&sent, smc_curs_read(&conn->tx_curs_sent, conn), conn);
	smc_curs_write(&prep, smc_curs_read(&conn->tx_curs_prep, conn), conn);
	/* cf. wmem_alloc - (snd_max - snd_una) */
	to_send = smc_curs_diff(conn->sndbuf_size, &sent, &prep);
	if (to_send <= 0)
		return 0;

	/* destination: RMBE */
	/* cf. snd_wnd */
	rmbespace = atomic_read(&conn->peer_rmbe_space);
	if (rmbespace <= 0)
		return 0;
	smc_curs_write(&prod,
		       smc_curs_read(&conn->local_tx_ctrl.prod, conn),
		       conn);
	smc_curs_write(&cons,
		       smc_curs_read(&conn->local_rx_ctrl.cons, conn),
		       conn);

	/* if usable snd_wnd closes ask peer to advertise once it opens again */
	conn->local_tx_ctrl.prod_flags.write_blocked = (to_send >= rmbespace);
	/* cf. usable snd_wnd */
	len = min(to_send, rmbespace);

	/* initialize variables for first iteration of subsequent nested loop */
	link = &lgr->lnk[SMC_SINGLE_LINK];
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
	dst_len_sum = dst_len;
	src_off = sent.count;
	/* dst_len determines the maximum src_len */
	if (sent.count + dst_len <= conn->sndbuf_size) {
		/* unwrapped src case: single chunk of entire dst_len */
		src_len = dst_len;
	} else {
		/* wrapped src case: 2 chunks of sum dst_len; start with 1st: */
		src_len = conn->sndbuf_size - sent.count;
	}
	src_len_sum = src_len;
	dma_addr = sg_dma_address(conn->sndbuf_desc->sgt[SMC_SINGLE_LINK].sgl);
	for (dstchunk = 0; dstchunk < 2; dstchunk++) {
		num_sges = 0;
		for (srcchunk = 0; srcchunk < 2; srcchunk++) {
			sges[srcchunk].addr = dma_addr + src_off;
			sges[srcchunk].length = src_len;
			sges[srcchunk].lkey = link->roce_pd->local_dma_lkey;
			num_sges++;
			src_off += src_len;
			if (src_off >= conn->sndbuf_size)
				src_off -= conn->sndbuf_size;
						/* modulo in send ring */
			if (src_len_sum == dst_len)
				break; /* either on 1st or 2nd iteration */
			/* prepare next (== 2nd) iteration */
			src_len = dst_len - src_len; /* remainder */
			src_len_sum += src_len;
		}
		rc = smc_tx_rdma_write(conn, dst_off, num_sges, sges);
		if (rc)
			return rc;
		if (dst_len_sum == len)
			break; /* either on 1st or 2nd iteration */
		/* prepare next (== 2nd) iteration */
		dst_off = 0; /* modulo offset in RMBE ring buffer */
		dst_len = len - dst_len; /* remainder */
		dst_len_sum += dst_len;
		src_len = min_t(int,
				dst_len, conn->sndbuf_size - sent.count);
		src_len_sum = src_len;
	}

	smc_tx_advance_cursors(conn, &prod, &sent, len);
	/* update connection's cursors with advanced local cursors */
	smc_curs_write(&conn->local_tx_ctrl.prod,
		       smc_curs_read(&prod, conn),
		       conn);
							/* dst: peer RMBE */
	smc_curs_write(&conn->tx_curs_sent,
		       smc_curs_read(&sent, conn),
		       conn);
							/* src: local sndbuf */

	return 0;
}

/* Wakeup sndbuf consumers from any context (IRQ or process)
 * since there is more data to transmit; usable snd_wnd as max transmit
 */
int smc_tx_sndbuf_nonempty(struct smc_connection *conn)
{
	struct smc_cdc_tx_pend *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	spin_lock_bh(&conn->send_lock);
	rc = smc_cdc_get_free_slot(&conn->lgr->lnk[SMC_SINGLE_LINK], &wr_buf,
				   &pend);
	if (rc < 0) {
		if (rc == -EBUSY) {
			struct smc_sock *smc =
				container_of(conn, struct smc_sock, conn);

			if (smc->sk.sk_err == ECONNABORTED) {
				rc = sock_error(&smc->sk);
				goto out_unlock;
			}
			rc = 0;
			schedule_work(&conn->tx_work);
		}
		goto out_unlock;
	}

	rc = smc_tx_rdma_writes(conn);
	if (rc) {
		smc_wr_tx_put_slot(&conn->lgr->lnk[SMC_SINGLE_LINK],
				   (struct smc_wr_tx_pend_priv *)pend);
		goto out_unlock;
	}

	rc = smc_cdc_msg_send(conn, wr_buf, pend);

out_unlock:
	spin_unlock_bh(&conn->send_lock);
	return rc;
}

/* Wakeup sndbuf consumers from process context
 * since there is more data to transmit
 */
static void smc_tx_work(struct work_struct *work)
{
	struct smc_connection *conn = container_of(work,
						   struct smc_connection,
						   tx_work);
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);
	int rc;

	lock_sock(&smc->sk);
	rc = smc_tx_sndbuf_nonempty(conn);
	if (!rc && conn->local_rx_ctrl.prod_flags.write_blocked &&
	    !atomic_read(&conn->bytes_to_rcv))
		conn->local_rx_ctrl.prod_flags.write_blocked = 0;
	release_sock(&smc->sk);
}

void smc_tx_consumer_update(struct smc_connection *conn)
{
	union smc_host_cursor cfed, cons;
	struct smc_cdc_tx_pend *pend;
	struct smc_wr_buf *wr_buf;
	int to_confirm, rc;

	smc_curs_write(&cons,
		       smc_curs_read(&conn->local_tx_ctrl.cons, conn),
		       conn);
	smc_curs_write(&cfed,
		       smc_curs_read(&conn->rx_curs_confirmed, conn),
		       conn);
	to_confirm = smc_curs_diff(conn->rmbe_size, &cfed, &cons);

	if (conn->local_rx_ctrl.prod_flags.cons_curs_upd_req ||
	    ((to_confirm > conn->rmbe_update_limit) &&
	     ((to_confirm > (conn->rmbe_size / 2)) ||
	      conn->local_rx_ctrl.prod_flags.write_blocked))) {
		rc = smc_cdc_get_free_slot(&conn->lgr->lnk[SMC_SINGLE_LINK],
					   &wr_buf, &pend);
		if (!rc)
			rc = smc_cdc_msg_send(conn, wr_buf, pend);
		if (rc < 0) {
			schedule_work(&conn->tx_work);
			return;
		}
		smc_curs_write(&conn->rx_curs_confirmed,
			       smc_curs_read(&conn->local_tx_ctrl.cons, conn),
			       conn);
		conn->local_rx_ctrl.prod_flags.cons_curs_upd_req = 0;
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
	INIT_WORK(&smc->conn.tx_work, smc_tx_work);
	spin_lock_init(&smc->conn.send_lock);
}
