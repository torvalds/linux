// SPDX-License-Identifier: GPL-2.0
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Manage RMBE
 * copy new RMBE data into user space
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>

#include <net/sock.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_cdc.h"
#include "smc_tx.h" /* smc_tx_consumer_update() */
#include "smc_rx.h"

/* callback implementation to wakeup consumers blocked with smc_rx_wait().
 * indirectly called by smc_cdc_msg_recv_action().
 */
static void smc_rx_wake_up(struct sock *sk)
{
	struct socket_wq *wq;

	/* derived from sock_def_readable() */
	/* called already in smc_listen_work() */
	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, EPOLLIN | EPOLLPRI |
						EPOLLRDNORM | EPOLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	if ((sk->sk_shutdown == SHUTDOWN_MASK) ||
	    (sk->sk_state == SMC_CLOSED))
		sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
	rcu_read_unlock();
}

/* Update consumer cursor
 *   @conn   connection to update
 *   @cons   consumer cursor
 *   @len    number of Bytes consumed
 */
static void smc_rx_update_consumer(struct smc_connection *conn,
				   union smc_host_cursor cons, size_t len)
{
	smc_curs_add(conn->rmbe_size, &cons, len);
	smc_curs_write(&conn->local_tx_ctrl.cons, smc_curs_read(&cons, conn),
		       conn);
	/* send consumer cursor update if required */
	/* similar to advertising new TCP rcv_wnd if required */
	smc_tx_consumer_update(conn);
}

struct smc_spd_priv {
	struct smc_sock *smc;
	size_t		 len;
};

static void smc_rx_pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	struct smc_spd_priv *priv = (struct smc_spd_priv *)buf->private;
	struct smc_sock *smc = priv->smc;
	struct smc_connection *conn;
	union smc_host_cursor cons;
	struct sock *sk = &smc->sk;

	if (sk->sk_state == SMC_CLOSED ||
	    sk->sk_state == SMC_PEERFINCLOSEWAIT ||
	    sk->sk_state == SMC_APPFINCLOSEWAIT)
		goto out;
	conn = &smc->conn;
	lock_sock(sk);
	smc_curs_write(&cons, smc_curs_read(&conn->local_tx_ctrl.cons, conn),
		       conn);
	smc_rx_update_consumer(conn, cons, priv->len);
	release_sock(sk);
	if (atomic_sub_and_test(priv->len, &conn->splice_pending))
		smc_rx_wake_up(sk);
out:
	kfree(priv);
	put_page(buf->page);
	sock_put(sk);
}

static int smc_rx_pipe_buf_nosteal(struct pipe_inode_info *pipe,
				   struct pipe_buffer *buf)
{
	return 1;
}

static const struct pipe_buf_operations smc_pipe_ops = {
	.can_merge = 0,
	.confirm = generic_pipe_buf_confirm,
	.release = smc_rx_pipe_buf_release,
	.steal = smc_rx_pipe_buf_nosteal,
	.get = generic_pipe_buf_get
};

static void smc_rx_spd_release(struct splice_pipe_desc *spd,
			       unsigned int i)
{
	put_page(spd->pages[i]);
}

static int smc_rx_splice(struct pipe_inode_info *pipe, char *src, size_t len,
			 struct smc_sock *smc)
{
	struct splice_pipe_desc spd;
	struct partial_page partial;
	struct smc_spd_priv *priv;
	struct page *page;
	int bytes;

	page = virt_to_page(smc->conn.rmb_desc->cpu_addr);
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->len = len;
	priv->smc = smc;
	partial.offset = src - (char *)smc->conn.rmb_desc->cpu_addr;
	partial.len = len;
	partial.private = (unsigned long)priv;

	spd.nr_pages_max = 1;
	spd.nr_pages = 1;
	spd.pages = &page;
	spd.partial = &partial;
	spd.ops = &smc_pipe_ops;
	spd.spd_release = smc_rx_spd_release;

	bytes = splice_to_pipe(pipe, &spd);
	if (bytes > 0) {
		sock_hold(&smc->sk);
		get_page(smc->conn.rmb_desc->pages);
		atomic_add(bytes, &smc->conn.splice_pending);
	}

	return bytes;
}

static int smc_rx_data_available_and_no_splice_pend(struct smc_connection *conn)
{
	return atomic_read(&conn->bytes_to_rcv) &&
	       !atomic_read(&conn->splice_pending);
}

/* blocks rcvbuf consumer until >=len bytes available or timeout or interrupted
 *   @smc    smc socket
 *   @timeo  pointer to max seconds to wait, pointer to value 0 for no timeout
 *   @fcrit  add'l criterion to evaluate as function pointer
 * Returns:
 * 1 if at least 1 byte available in rcvbuf or if socket error/shutdown.
 * 0 otherwise (nothing in rcvbuf nor timeout, e.g. interrupted).
 */
int smc_rx_wait(struct smc_sock *smc, long *timeo,
		int (*fcrit)(struct smc_connection *conn))
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct smc_connection *conn = &smc->conn;
	struct sock *sk = &smc->sk;
	int rc;

	if (fcrit(conn))
		return 1;
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	add_wait_queue(sk_sleep(sk), &wait);
	rc = sk_wait_event(sk, timeo,
			   sk->sk_err ||
			   sk->sk_shutdown & RCV_SHUTDOWN ||
			   fcrit(conn) ||
			   smc_cdc_rxed_any_close_or_senddone(conn),
			   &wait);
	remove_wait_queue(sk_sleep(sk), &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	return rc;
}

/* smc_rx_recvmsg - receive data from RMBE
 * @msg:	copy data to receive buffer
 * @pipe:	copy data to pipe if set - indicates splice() call
 *
 * rcvbuf consumer: main API called by socket layer.
 * Called under sk lock.
 */
int smc_rx_recvmsg(struct smc_sock *smc, struct msghdr *msg,
		   struct pipe_inode_info *pipe, size_t len, int flags)
{
	size_t copylen, read_done = 0, read_remaining = len;
	size_t chunk_len, chunk_off, chunk_len_sum;
	struct smc_connection *conn = &smc->conn;
	int (*func)(struct smc_connection *conn);
	union smc_host_cursor cons;
	int readable, chunk;
	char *rcvbuf_base;
	struct sock *sk;
	int splbytes;
	long timeo;
	int target;		/* Read at least these many bytes */
	int rc;

	if (unlikely(flags & MSG_ERRQUEUE))
		return -EINVAL; /* future work for sk.sk_family == AF_SMC */
	if (flags & MSG_OOB)
		return -EINVAL; /* future work */

	sk = &smc->sk;
	if (sk->sk_state == SMC_LISTEN)
		return -ENOTCONN;
	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	/* we currently use 1 RMBE per RMB, so RMBE == RMB base addr */
	rcvbuf_base = conn->rmb_desc->cpu_addr;

	do { /* while (read_remaining) */
		if (read_done >= target || (pipe && read_done))
			break;

		if (atomic_read(&conn->bytes_to_rcv))
			goto copy;

		if (sk->sk_shutdown & RCV_SHUTDOWN ||
		    smc_cdc_rxed_any_close_or_senddone(conn) ||
		    conn->local_tx_ctrl.conn_state_flags.peer_conn_abort)
			break;

		if (read_done) {
			if (sk->sk_err ||
			    sk->sk_state == SMC_CLOSED ||
			    !timeo ||
			    signal_pending(current))
				break;
		} else {
			if (sk->sk_err) {
				read_done = sock_error(sk);
				break;
			}
			if (sk->sk_state == SMC_CLOSED) {
				if (!sock_flag(sk, SOCK_DONE)) {
					/* This occurs when user tries to read
					 * from never connected socket.
					 */
					read_done = -ENOTCONN;
					break;
				}
				break;
			}
			if (signal_pending(current)) {
				read_done = sock_intr_errno(timeo);
				break;
			}
			if (!timeo)
				return -EAGAIN;
		}

		if (!smc_rx_data_available(conn)) {
			smc_rx_wait(smc, &timeo, smc_rx_data_available);
			continue;
		}

copy:
		/* initialize variables for 1st iteration of subsequent loop */
		/* could be just 1 byte, even after waiting on data above */
		readable = atomic_read(&conn->bytes_to_rcv);
		splbytes = atomic_read(&conn->splice_pending);
		if (!readable || (msg && splbytes)) {
			if (splbytes)
				func = smc_rx_data_available_and_no_splice_pend;
			else
				func = smc_rx_data_available;
			smc_rx_wait(smc, &timeo, func);
			continue;
		}

		/* not more than what user space asked for */
		copylen = min_t(size_t, read_remaining, readable);
		smc_curs_write(&cons,
			       smc_curs_read(&conn->local_tx_ctrl.cons, conn),
			       conn);
		/* subsequent splice() calls pick up where previous left */
		if (splbytes)
			smc_curs_add(conn->rmbe_size, &cons, splbytes);
		/* determine chunks where to read from rcvbuf */
		/* either unwrapped case, or 1st chunk of wrapped case */
		chunk_len = min_t(size_t,
				  copylen, conn->rmbe_size - cons.count);
		chunk_len_sum = chunk_len;
		chunk_off = cons.count;
		smc_rmb_sync_sg_for_cpu(conn);
		for (chunk = 0; chunk < 2; chunk++) {
			if (!(flags & MSG_TRUNC)) {
				if (msg) {
					rc = memcpy_to_msg(msg, rcvbuf_base +
							   chunk_off,
							   chunk_len);
				} else {
					rc = smc_rx_splice(pipe, rcvbuf_base +
							chunk_off, chunk_len,
							smc);
				}
				if (rc < 0) {
					if (!read_done)
						read_done = -EFAULT;
					smc_rmb_sync_sg_for_device(conn);
					goto out;
				}
			}
			read_remaining -= chunk_len;
			read_done += chunk_len;

			if (chunk_len_sum == copylen)
				break; /* either on 1st or 2nd iteration */
			/* prepare next (== 2nd) iteration */
			chunk_len = copylen - chunk_len; /* remainder */
			chunk_len_sum += chunk_len;
			chunk_off = 0; /* modulo offset in recv ring buffer */
		}
		smc_rmb_sync_sg_for_device(conn);

		/* update cursors */
		if (!(flags & MSG_PEEK)) {
			/* increased in recv tasklet smc_cdc_msg_rcv() */
			smp_mb__before_atomic();
			atomic_sub(copylen, &conn->bytes_to_rcv);
			/* guarantee 0 <= bytes_to_rcv <= rmbe_size */
			smp_mb__after_atomic();
			if (msg)
				smc_rx_update_consumer(conn, cons, copylen);
		}
	} while (read_remaining);
out:
	return read_done;
}

/* Initialize receive properties on connection establishment. NB: not __init! */
void smc_rx_init(struct smc_sock *smc)
{
	smc->sk.sk_data_ready = smc_rx_wake_up;
	atomic_set(&smc->conn.splice_pending, 0);
}
