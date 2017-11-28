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

/* callback implementation for sk.sk_data_ready()
 * to wakeup rcvbuf consumers that blocked with smc_rx_wait_data().
 * indirectly called by smc_cdc_msg_recv_action().
 */
static void smc_rx_data_ready(struct sock *sk)
{
	struct socket_wq *wq;

	/* derived from sock_def_readable() */
	/* called already in smc_listen_work() */
	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLIN | POLLPRI |
						POLLRDNORM | POLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	if ((sk->sk_shutdown == SHUTDOWN_MASK) ||
	    (sk->sk_state == SMC_CLOSED))
		sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
	rcu_read_unlock();
}

/* blocks rcvbuf consumer until >=len bytes available or timeout or interrupted
 *   @smc    smc socket
 *   @timeo  pointer to max seconds to wait, pointer to value 0 for no timeout
 * Returns:
 * 1 if at least 1 byte available in rcvbuf or if socket error/shutdown.
 * 0 otherwise (nothing in rcvbuf nor timeout, e.g. interrupted).
 */
static int smc_rx_wait_data(struct smc_sock *smc, long *timeo)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct smc_connection *conn = &smc->conn;
	struct sock *sk = &smc->sk;
	int rc;

	if (atomic_read(&conn->bytes_to_rcv))
		return 1;
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	add_wait_queue(sk_sleep(sk), &wait);
	rc = sk_wait_event(sk, timeo,
			   sk->sk_err ||
			   sk->sk_shutdown & RCV_SHUTDOWN ||
			   sock_flag(sk, SOCK_DONE) ||
			   atomic_read(&conn->bytes_to_rcv) ||
			   smc_cdc_rxed_any_close_or_senddone(conn),
			   &wait);
	remove_wait_queue(sk_sleep(sk), &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	return rc;
}

/* rcvbuf consumer: main API called by socket layer.
 * called under sk lock.
 */
int smc_rx_recvmsg(struct smc_sock *smc, struct msghdr *msg, size_t len,
		   int flags)
{
	size_t copylen, read_done = 0, read_remaining = len;
	size_t chunk_len, chunk_off, chunk_len_sum;
	struct smc_connection *conn = &smc->conn;
	union smc_host_cursor cons;
	int readable, chunk;
	char *rcvbuf_base;
	struct sock *sk;
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

	msg->msg_namelen = 0;
	/* we currently use 1 RMBE per RMB, so RMBE == RMB base addr */
	rcvbuf_base = conn->rmb_desc->cpu_addr;

	do { /* while (read_remaining) */
		if (read_done >= target)
			break;

		if (atomic_read(&conn->bytes_to_rcv))
			goto copy;

		if (read_done) {
			if (sk->sk_err ||
			    sk->sk_state == SMC_CLOSED ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    !timeo ||
			    signal_pending(current) ||
			    smc_cdc_rxed_any_close_or_senddone(conn) ||
			    conn->local_tx_ctrl.conn_state_flags.
			    peer_conn_abort)
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;
			if (sk->sk_err) {
				read_done = sock_error(sk);
				break;
			}
			if (sk->sk_shutdown & RCV_SHUTDOWN ||
			    smc_cdc_rxed_any_close_or_senddone(conn) ||
			    conn->local_tx_ctrl.conn_state_flags.
			    peer_conn_abort)
				break;
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

		if (!atomic_read(&conn->bytes_to_rcv)) {
			smc_rx_wait_data(smc, &timeo);
			continue;
		}

copy:
		/* initialize variables for 1st iteration of subsequent loop */
		/* could be just 1 byte, even after smc_rx_wait_data above */
		readable = atomic_read(&conn->bytes_to_rcv);
		/* not more than what user space asked for */
		copylen = min_t(size_t, read_remaining, readable);
		smc_curs_write(&cons,
			       smc_curs_read(&conn->local_tx_ctrl.cons, conn),
			       conn);
		/* determine chunks where to read from rcvbuf */
		/* either unwrapped case, or 1st chunk of wrapped case */
		chunk_len = min_t(size_t,
				  copylen, conn->rmbe_size - cons.count);
		chunk_len_sum = chunk_len;
		chunk_off = cons.count;
		smc_rmb_sync_sg_for_cpu(conn);
		for (chunk = 0; chunk < 2; chunk++) {
			if (!(flags & MSG_TRUNC)) {
				rc = memcpy_to_msg(msg, rcvbuf_base + chunk_off,
						   chunk_len);
				if (rc) {
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
			smc_curs_add(conn->rmbe_size, &cons, copylen);
			/* increased in recv tasklet smc_cdc_msg_rcv() */
			smp_mb__before_atomic();
			atomic_sub(copylen, &conn->bytes_to_rcv);
			/* guarantee 0 <= bytes_to_rcv <= rmbe_size */
			smp_mb__after_atomic();
			smc_curs_write(&conn->local_tx_ctrl.cons,
				       smc_curs_read(&cons, conn),
				       conn);
			/* send consumer cursor update if required */
			/* similar to advertising new TCP rcv_wnd if required */
			smc_tx_consumer_update(conn);
		}
	} while (read_remaining);
out:
	return read_done;
}

/* Initialize receive properties on connection establishment. NB: not __init! */
void smc_rx_init(struct smc_sock *smc)
{
	smc->sk.sk_data_ready = smc_rx_data_ready;
}
