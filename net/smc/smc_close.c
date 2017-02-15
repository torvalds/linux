/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Socket Closing - normal and abnormal
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/workqueue.h>
#include <net/sock.h>

#include "smc.h"
#include "smc_tx.h"
#include "smc_cdc.h"
#include "smc_close.h"

#define SMC_CLOSE_WAIT_TX_PENDS_TIME		(5 * HZ)

static void smc_close_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	/* Close non-accepted connections */
	while ((sk = smc_accept_dequeue(parent, NULL)))
		smc_close_non_accepted(sk);
}

static void smc_close_wait_tx_pends(struct smc_sock *smc)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct sock *sk = &smc->sk;
	signed long timeout;

	timeout = SMC_CLOSE_WAIT_TX_PENDS_TIME;
	add_wait_queue(sk_sleep(sk), &wait);
	while (!signal_pending(current) && timeout) {
		int rc;

		rc = sk_wait_event(sk, &timeout,
				   !smc_cdc_tx_has_pending(&smc->conn),
				   &wait);
		if (rc)
			break;
	}
	remove_wait_queue(sk_sleep(sk), &wait);
}

/* wait for sndbuf data being transmitted */
static void smc_close_stream_wait(struct smc_sock *smc, long timeout)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct sock *sk = &smc->sk;

	if (!timeout)
		return;

	if (!smc_tx_prepared_sends(&smc->conn))
		return;

	smc->wait_close_tx_prepared = 1;
	add_wait_queue(sk_sleep(sk), &wait);
	while (!signal_pending(current) && timeout) {
		int rc;

		rc = sk_wait_event(sk, &timeout,
				   !smc_tx_prepared_sends(&smc->conn) ||
				   (sk->sk_err == ECONNABORTED) ||
				   (sk->sk_err == ECONNRESET),
				   &wait);
		if (rc)
			break;
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	smc->wait_close_tx_prepared = 0;
}

void smc_close_wake_tx_prepared(struct smc_sock *smc)
{
	if (smc->wait_close_tx_prepared)
		/* wake up socket closing */
		smc->sk.sk_state_change(&smc->sk);
}

static int smc_close_wr(struct smc_connection *conn)
{
	conn->local_tx_ctrl.conn_state_flags.peer_done_writing = 1;

	return smc_cdc_get_slot_and_msg_send(conn);
}

static int smc_close_final(struct smc_connection *conn)
{
	if (atomic_read(&conn->bytes_to_rcv))
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
	else
		conn->local_tx_ctrl.conn_state_flags.peer_conn_closed = 1;

	return smc_cdc_get_slot_and_msg_send(conn);
}

static int smc_close_abort(struct smc_connection *conn)
{
	conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;

	return smc_cdc_get_slot_and_msg_send(conn);
}

/* terminate smc socket abnormally - active abort
 * RDMA communication no longer possible
 */
void smc_close_active_abort(struct smc_sock *smc)
{
	struct smc_cdc_conn_state_flags *txflags =
		&smc->conn.local_tx_ctrl.conn_state_flags;

	bh_lock_sock(&smc->sk);
	smc->sk.sk_err = ECONNABORTED;
	if (smc->clcsock && smc->clcsock->sk) {
		smc->clcsock->sk->sk_err = ECONNABORTED;
		smc->clcsock->sk->sk_state_change(smc->clcsock->sk);
	}
	switch (smc->sk.sk_state) {
	case SMC_INIT:
		smc->sk.sk_state = SMC_PEERABORTWAIT;
		break;
	case SMC_APPCLOSEWAIT1:
	case SMC_APPCLOSEWAIT2:
		txflags->peer_conn_abort = 1;
		sock_release(smc->clcsock);
		if (!smc_cdc_rxed_any_close(&smc->conn))
			smc->sk.sk_state = SMC_PEERABORTWAIT;
		else
			smc->sk.sk_state = SMC_CLOSED;
		break;
	case SMC_PEERCLOSEWAIT1:
	case SMC_PEERCLOSEWAIT2:
		if (!txflags->peer_conn_closed) {
			smc->sk.sk_state = SMC_PEERABORTWAIT;
			txflags->peer_conn_abort = 1;
			sock_release(smc->clcsock);
		} else {
			smc->sk.sk_state = SMC_CLOSED;
		}
		break;
	case SMC_PROCESSABORT:
	case SMC_APPFINCLOSEWAIT:
		if (!txflags->peer_conn_closed) {
			txflags->peer_conn_abort = 1;
			sock_release(smc->clcsock);
		}
		smc->sk.sk_state = SMC_CLOSED;
		break;
	case SMC_PEERFINCLOSEWAIT:
	case SMC_PEERABORTWAIT:
	case SMC_CLOSED:
		break;
	}

	sock_set_flag(&smc->sk, SOCK_DEAD);
	bh_unlock_sock(&smc->sk);
	smc->sk.sk_state_change(&smc->sk);
}

int smc_close_active(struct smc_sock *smc)
{
	struct smc_cdc_conn_state_flags *txflags =
		&smc->conn.local_tx_ctrl.conn_state_flags;
	long timeout = SMC_MAX_STREAM_WAIT_TIMEOUT;
	struct smc_connection *conn = &smc->conn;
	struct sock *sk = &smc->sk;
	int old_state;
	int rc = 0;

	if (sock_flag(sk, SOCK_LINGER) &&
	    !(current->flags & PF_EXITING))
		timeout = sk->sk_lingertime;

again:
	old_state = sk->sk_state;
	switch (old_state) {
	case SMC_INIT:
		sk->sk_state = SMC_CLOSED;
		if (smc->smc_listen_work.func)
			flush_work(&smc->smc_listen_work);
		sock_put(sk);
		break;
	case SMC_LISTEN:
		sk->sk_state = SMC_CLOSED;
		sk->sk_state_change(sk); /* wake up accept */
		if (smc->clcsock && smc->clcsock->sk) {
			rc = kernel_sock_shutdown(smc->clcsock, SHUT_RDWR);
			/* wake up kernel_accept of smc_tcp_listen_worker */
			smc->clcsock->sk->sk_data_ready(smc->clcsock->sk);
		}
		release_sock(sk);
		smc_close_cleanup_listen(sk);
		flush_work(&smc->tcp_listen_work);
		lock_sock(sk);
		break;
	case SMC_ACTIVE:
		smc_close_stream_wait(smc, timeout);
		release_sock(sk);
		cancel_work_sync(&conn->tx_work);
		lock_sock(sk);
		if (sk->sk_state == SMC_ACTIVE) {
			/* send close request */
			rc = smc_close_final(conn);
			sk->sk_state = SMC_PEERCLOSEWAIT1;
		} else {
			/* peer event has changed the state */
			goto again;
		}
		break;
	case SMC_APPFINCLOSEWAIT:
		/* socket already shutdown wr or both (active close) */
		if (txflags->peer_done_writing &&
		    !txflags->peer_conn_closed) {
			/* just shutdown wr done, send close request */
			rc = smc_close_final(conn);
		}
		sk->sk_state = SMC_CLOSED;
		smc_close_wait_tx_pends(smc);
		break;
	case SMC_APPCLOSEWAIT1:
	case SMC_APPCLOSEWAIT2:
		if (!smc_cdc_rxed_any_close(conn))
			smc_close_stream_wait(smc, timeout);
		release_sock(sk);
		cancel_work_sync(&conn->tx_work);
		lock_sock(sk);
		if (sk->sk_err != ECONNABORTED) {
			/* confirm close from peer */
			rc = smc_close_final(conn);
			if (rc)
				break;
		}
		if (smc_cdc_rxed_any_close(conn))
			/* peer has closed the socket already */
			sk->sk_state = SMC_CLOSED;
		else
			/* peer has just issued a shutdown write */
			sk->sk_state = SMC_PEERFINCLOSEWAIT;
		smc_close_wait_tx_pends(smc);
		break;
	case SMC_PEERCLOSEWAIT1:
	case SMC_PEERCLOSEWAIT2:
	case SMC_PEERFINCLOSEWAIT:
		/* peer sending PeerConnectionClosed will cause transition */
		break;
	case SMC_PROCESSABORT:
		cancel_work_sync(&conn->tx_work);
		smc_close_abort(conn);
		sk->sk_state = SMC_CLOSED;
		smc_close_wait_tx_pends(smc);
		break;
	case SMC_PEERABORTWAIT:
	case SMC_CLOSED:
		/* nothing to do, add tracing in future patch */
		break;
	}

	if (old_state != sk->sk_state)
		sk->sk_state_change(&smc->sk);
	return rc;
}

static void smc_close_passive_abort_received(struct smc_sock *smc)
{
	struct smc_cdc_conn_state_flags *txflags =
		&smc->conn.local_tx_ctrl.conn_state_flags;
	struct sock *sk = &smc->sk;

	switch (sk->sk_state) {
	case SMC_ACTIVE:
	case SMC_APPFINCLOSEWAIT:
	case SMC_APPCLOSEWAIT1:
	case SMC_APPCLOSEWAIT2:
		smc_close_abort(&smc->conn);
		sk->sk_state = SMC_PROCESSABORT;
		break;
	case SMC_PEERCLOSEWAIT1:
	case SMC_PEERCLOSEWAIT2:
		if (txflags->peer_done_writing &&
		    !txflags->peer_conn_closed) {
			/* just shutdown, but not yet closed locally */
			smc_close_abort(&smc->conn);
			sk->sk_state = SMC_PROCESSABORT;
		} else {
			sk->sk_state = SMC_CLOSED;
		}
		break;
	case SMC_PEERFINCLOSEWAIT:
	case SMC_PEERABORTWAIT:
		sk->sk_state = SMC_CLOSED;
		break;
	case SMC_INIT:
	case SMC_PROCESSABORT:
	/* nothing to do, add tracing in future patch */
		break;
	}
}

/* Some kind of closing has been received: peer_conn_closed, peer_conn_abort,
 * or peer_done_writing.
 * Called under tasklet context.
 */
void smc_close_passive_received(struct smc_sock *smc)
{
	struct smc_cdc_conn_state_flags *rxflags =
		&smc->conn.local_rx_ctrl.conn_state_flags;
	struct sock *sk = &smc->sk;
	int old_state;

	sk->sk_shutdown |= RCV_SHUTDOWN;
	if (smc->clcsock && smc->clcsock->sk)
		smc->clcsock->sk->sk_shutdown |= RCV_SHUTDOWN;
	sock_set_flag(&smc->sk, SOCK_DONE);

	old_state = sk->sk_state;

	if (rxflags->peer_conn_abort) {
		smc_close_passive_abort_received(smc);
		goto wakeup;
	}

	switch (sk->sk_state) {
	case SMC_INIT:
		if (atomic_read(&smc->conn.bytes_to_rcv) ||
		    (rxflags->peer_done_writing &&
		     !rxflags->peer_conn_closed))
			sk->sk_state = SMC_APPCLOSEWAIT1;
		else
			sk->sk_state = SMC_CLOSED;
		break;
	case SMC_ACTIVE:
		sk->sk_state = SMC_APPCLOSEWAIT1;
		break;
	case SMC_PEERCLOSEWAIT1:
		if (rxflags->peer_done_writing)
			sk->sk_state = SMC_PEERCLOSEWAIT2;
		/* fall through to check for closing */
	case SMC_PEERCLOSEWAIT2:
	case SMC_PEERFINCLOSEWAIT:
		if (!smc_cdc_rxed_any_close(&smc->conn))
			break;
		if (sock_flag(sk, SOCK_DEAD) &&
		    (sk->sk_shutdown == SHUTDOWN_MASK)) {
			/* smc_release has already been called locally */
			sk->sk_state = SMC_CLOSED;
		} else {
			/* just shutdown, but not yet closed locally */
			sk->sk_state = SMC_APPFINCLOSEWAIT;
		}
		break;
	case SMC_APPCLOSEWAIT1:
	case SMC_APPCLOSEWAIT2:
	case SMC_APPFINCLOSEWAIT:
	case SMC_PEERABORTWAIT:
	case SMC_PROCESSABORT:
	case SMC_CLOSED:
		/* nothing to do, add tracing in future patch */
		break;
	}

wakeup:
	if (old_state != sk->sk_state)
		sk->sk_state_change(sk);
	sk->sk_data_ready(sk); /* wakeup blocked rcvbuf consumers */
	sk->sk_write_space(sk); /* wakeup blocked sndbuf producers */

	if ((sk->sk_state == SMC_CLOSED) &&
	    (sock_flag(sk, SOCK_DEAD) || (old_state == SMC_INIT))) {
		smc_conn_free(&smc->conn);
		schedule_delayed_work(&smc->sock_put_work,
				      SMC_CLOSE_SOCK_PUT_DELAY);
	}
}

void smc_close_sock_put_work(struct work_struct *work)
{
	struct smc_sock *smc = container_of(to_delayed_work(work),
					    struct smc_sock,
					    sock_put_work);

	smc->sk.sk_prot->unhash(&smc->sk);
	sock_put(&smc->sk);
}

int smc_close_shutdown_write(struct smc_sock *smc)
{
	struct smc_connection *conn = &smc->conn;
	long timeout = SMC_MAX_STREAM_WAIT_TIMEOUT;
	struct sock *sk = &smc->sk;
	int old_state;
	int rc = 0;

	if (sock_flag(sk, SOCK_LINGER))
		timeout = sk->sk_lingertime;

again:
	old_state = sk->sk_state;
	switch (old_state) {
	case SMC_ACTIVE:
		smc_close_stream_wait(smc, timeout);
		release_sock(sk);
		cancel_work_sync(&conn->tx_work);
		lock_sock(sk);
		/* send close wr request */
		rc = smc_close_wr(conn);
		if (sk->sk_state == SMC_ACTIVE)
			sk->sk_state = SMC_PEERCLOSEWAIT1;
		else
			goto again;
		break;
	case SMC_APPCLOSEWAIT1:
		/* passive close */
		if (!smc_cdc_rxed_any_close(conn))
			smc_close_stream_wait(smc, timeout);
		release_sock(sk);
		cancel_work_sync(&conn->tx_work);
		lock_sock(sk);
		/* confirm close from peer */
		rc = smc_close_wr(conn);
		sk->sk_state = SMC_APPCLOSEWAIT2;
		break;
	case SMC_APPCLOSEWAIT2:
	case SMC_PEERFINCLOSEWAIT:
	case SMC_PEERCLOSEWAIT1:
	case SMC_PEERCLOSEWAIT2:
	case SMC_APPFINCLOSEWAIT:
	case SMC_PROCESSABORT:
	case SMC_PEERABORTWAIT:
		/* nothing to do, add tracing in future patch */
		break;
	}

	if (old_state != sk->sk_state)
		sk->sk_state_change(&smc->sk);
	return rc;
}
