/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Connection Data Control (CDC)
 * handles flow control
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/spinlock.h>

#include "smc.h"
#include "smc_wr.h"
#include "smc_cdc.h"
#include "smc_tx.h"
#include "smc_rx.h"
#include "smc_close.h"

/********************************** send *************************************/

struct smc_cdc_tx_pend {
	struct smc_connection	*conn;		/* socket connection */
	union smc_host_cursor	cursor;	/* tx sndbuf cursor sent */
	union smc_host_cursor	p_cursor;	/* rx RMBE cursor produced */
	u16			ctrl_seq;	/* conn. tx sequence # */
};

/* handler for send/transmission completion of a CDC msg */
static void smc_cdc_tx_handler(struct smc_wr_tx_pend_priv *pnd_snd,
			       struct smc_link *link,
			       enum ib_wc_status wc_status)
{
	struct smc_cdc_tx_pend *cdcpend = (struct smc_cdc_tx_pend *)pnd_snd;
	struct smc_sock *smc;
	int diff;

	if (!cdcpend->conn)
		/* already dismissed */
		return;

	smc = container_of(cdcpend->conn, struct smc_sock, conn);
	bh_lock_sock(&smc->sk);
	if (!wc_status) {
		diff = smc_curs_diff(cdcpend->conn->sndbuf_size,
				     &cdcpend->conn->tx_curs_fin,
				     &cdcpend->cursor);
		/* sndbuf_space is decreased in smc_sendmsg */
		smp_mb__before_atomic();
		atomic_add(diff, &cdcpend->conn->sndbuf_space);
		/* guarantee 0 <= sndbuf_space <= sndbuf_size */
		smp_mb__after_atomic();
		smc_curs_write(&cdcpend->conn->tx_curs_fin,
			       smc_curs_read(&cdcpend->cursor, cdcpend->conn),
			       cdcpend->conn);
	}
	smc_tx_sndbuf_nonfull(smc);
	if (smc->sk.sk_state != SMC_ACTIVE)
		/* wake up smc_close_wait_tx_pends() */
		smc->sk.sk_state_change(&smc->sk);
	bh_unlock_sock(&smc->sk);
}

int smc_cdc_get_free_slot(struct smc_link *link,
			  struct smc_wr_buf **wr_buf,
			  struct smc_cdc_tx_pend **pend)
{
	return smc_wr_tx_get_free_slot(link, smc_cdc_tx_handler, wr_buf,
				       (struct smc_wr_tx_pend_priv **)pend);
}

static inline void smc_cdc_add_pending_send(struct smc_connection *conn,
					    struct smc_cdc_tx_pend *pend)
{
	BUILD_BUG_ON_MSG(
		sizeof(struct smc_cdc_msg) > SMC_WR_BUF_SIZE,
		"must increase SMC_WR_BUF_SIZE to at least sizeof(struct smc_cdc_msg)");
	BUILD_BUG_ON_MSG(
		offsetof(struct smc_cdc_msg, reserved) > SMC_WR_TX_SIZE,
		"must adapt SMC_WR_TX_SIZE to sizeof(struct smc_cdc_msg); if not all smc_wr upper layer protocols use the same message size any more, must start to set link->wr_tx_sges[i].length on each individual smc_wr_tx_send()");
	BUILD_BUG_ON_MSG(
		sizeof(struct smc_cdc_tx_pend) > SMC_WR_TX_PEND_PRIV_SIZE,
		"must increase SMC_WR_TX_PEND_PRIV_SIZE to at least sizeof(struct smc_cdc_tx_pend)");
	pend->conn = conn;
	pend->cursor = conn->tx_curs_sent;
	pend->p_cursor = conn->local_tx_ctrl.prod;
	pend->ctrl_seq = conn->tx_cdc_seq;
}

int smc_cdc_msg_send(struct smc_connection *conn,
		     struct smc_wr_buf *wr_buf,
		     struct smc_cdc_tx_pend *pend)
{
	struct smc_link *link;
	int rc;

	link = &conn->lgr->lnk[SMC_SINGLE_LINK];

	smc_cdc_add_pending_send(conn, pend);

	conn->tx_cdc_seq++;
	conn->local_tx_ctrl.seqno = conn->tx_cdc_seq;
	smc_host_msg_to_cdc((struct smc_cdc_msg *)wr_buf,
			    &conn->local_tx_ctrl, conn);
	rc = smc_wr_tx_send(link, (struct smc_wr_tx_pend_priv *)pend);
	if (!rc)
		smc_curs_write(&conn->rx_curs_confirmed,
			       smc_curs_read(&conn->local_tx_ctrl.cons, conn),
			       conn);

	return rc;
}

int smc_cdc_get_slot_and_msg_send(struct smc_connection *conn)
{
	struct smc_cdc_tx_pend *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_cdc_get_free_slot(&conn->lgr->lnk[SMC_SINGLE_LINK], &wr_buf,
				   &pend);
	if (rc)
		return rc;

	return smc_cdc_msg_send(conn, wr_buf, pend);
}

static bool smc_cdc_tx_filter(struct smc_wr_tx_pend_priv *tx_pend,
			      unsigned long data)
{
	struct smc_connection *conn = (struct smc_connection *)data;
	struct smc_cdc_tx_pend *cdc_pend =
		(struct smc_cdc_tx_pend *)tx_pend;

	return cdc_pend->conn == conn;
}

static void smc_cdc_tx_dismisser(struct smc_wr_tx_pend_priv *tx_pend)
{
	struct smc_cdc_tx_pend *cdc_pend =
		(struct smc_cdc_tx_pend *)tx_pend;

	cdc_pend->conn = NULL;
}

void smc_cdc_tx_dismiss_slots(struct smc_connection *conn)
{
	struct smc_link *link = &conn->lgr->lnk[SMC_SINGLE_LINK];

	smc_wr_tx_dismiss_slots(link, SMC_CDC_MSG_TYPE,
				smc_cdc_tx_filter, smc_cdc_tx_dismisser,
				(unsigned long)conn);
}

bool smc_cdc_tx_has_pending(struct smc_connection *conn)
{
	struct smc_link *link = &conn->lgr->lnk[SMC_SINGLE_LINK];

	return smc_wr_tx_has_pending(link, SMC_CDC_MSG_TYPE,
				     smc_cdc_tx_filter, (unsigned long)conn);
}

/********************************* receive ***********************************/

static inline bool smc_cdc_before(u16 seq1, u16 seq2)
{
	return (s16)(seq1 - seq2) < 0;
}

static void smc_cdc_msg_recv_action(struct smc_sock *smc,
				    struct smc_link *link,
				    struct smc_cdc_msg *cdc)
{
	union smc_host_cursor cons_old, prod_old;
	struct smc_connection *conn = &smc->conn;
	int diff_cons, diff_prod;

	if (!cdc->prod_flags.failover_validation) {
		if (smc_cdc_before(ntohs(cdc->seqno),
				   conn->local_rx_ctrl.seqno))
			/* received seqno is old */
			return;
	}
	smc_curs_write(&prod_old,
		       smc_curs_read(&conn->local_rx_ctrl.prod, conn),
		       conn);
	smc_curs_write(&cons_old,
		       smc_curs_read(&conn->local_rx_ctrl.cons, conn),
		       conn);
	smc_cdc_msg_to_host(&conn->local_rx_ctrl, cdc, conn);

	diff_cons = smc_curs_diff(conn->peer_rmbe_size, &cons_old,
				  &conn->local_rx_ctrl.cons);
	if (diff_cons) {
		/* peer_rmbe_space is decreased during data transfer with RDMA
		 * write
		 */
		smp_mb__before_atomic();
		atomic_add(diff_cons, &conn->peer_rmbe_space);
		/* guarantee 0 <= peer_rmbe_space <= peer_rmbe_size */
		smp_mb__after_atomic();
	}

	diff_prod = smc_curs_diff(conn->rmbe_size, &prod_old,
				  &conn->local_rx_ctrl.prod);
	if (diff_prod) {
		/* bytes_to_rcv is decreased in smc_recvmsg */
		smp_mb__before_atomic();
		atomic_add(diff_prod, &conn->bytes_to_rcv);
		/* guarantee 0 <= bytes_to_rcv <= rmbe_size */
		smp_mb__after_atomic();
		smc->sk.sk_data_ready(&smc->sk);
	}

	if (conn->local_rx_ctrl.conn_state_flags.peer_conn_abort) {
		smc->sk.sk_err = ECONNRESET;
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
	}
	if (smc_cdc_rxed_any_close_or_senddone(conn))
		smc_close_passive_received(smc);

	/* piggy backed tx info */
	/* trigger sndbuf consumer: RDMA write into peer RMBE and CDC */
	if (diff_cons && smc_tx_prepared_sends(conn)) {
		smc_tx_sndbuf_nonempty(conn);
		/* trigger socket release if connection closed */
		smc_close_wake_tx_prepared(smc);
	}

	/* subsequent patch: trigger socket release if connection closed */

	/* socket connected but not accepted */
	if (!smc->sk.sk_socket)
		return;

	/* data available */
	if ((conn->local_rx_ctrl.prod_flags.write_blocked) ||
	    (conn->local_rx_ctrl.prod_flags.cons_curs_upd_req))
		smc_tx_consumer_update(conn);
}

/* called under tasklet context */
static inline void smc_cdc_msg_recv(struct smc_cdc_msg *cdc,
				    struct smc_link *link, u64 wr_id)
{
	struct smc_link_group *lgr = container_of(link, struct smc_link_group,
						  lnk[SMC_SINGLE_LINK]);
	struct smc_connection *connection;
	struct smc_sock *smc;

	/* lookup connection */
	read_lock_bh(&lgr->conns_lock);
	connection = smc_lgr_find_conn(ntohl(cdc->token), lgr);
	if (!connection) {
		read_unlock_bh(&lgr->conns_lock);
		return;
	}
	smc = container_of(connection, struct smc_sock, conn);
	sock_hold(&smc->sk);
	read_unlock_bh(&lgr->conns_lock);
	bh_lock_sock(&smc->sk);
	smc_cdc_msg_recv_action(smc, link, cdc);
	bh_unlock_sock(&smc->sk);
	sock_put(&smc->sk); /* no free sk in softirq-context */
}

/***************************** init, exit, misc ******************************/

static void smc_cdc_rx_handler(struct ib_wc *wc, void *buf)
{
	struct smc_link *link = (struct smc_link *)wc->qp->qp_context;
	struct smc_cdc_msg *cdc = buf;

	if (wc->byte_len < offsetof(struct smc_cdc_msg, reserved))
		return; /* short message */
	if (cdc->len != sizeof(*cdc))
		return; /* invalid message */
	smc_cdc_msg_recv(cdc, link, wc->wr_id);
}

static struct smc_wr_rx_handler smc_cdc_rx_handlers[] = {
	{
		.handler	= smc_cdc_rx_handler,
		.type		= SMC_CDC_MSG_TYPE
	},
	{
		.handler	= NULL,
	}
};

int __init smc_cdc_init(void)
{
	struct smc_wr_rx_handler *handler;
	int rc = 0;

	for (handler = smc_cdc_rx_handlers; handler->handler; handler++) {
		INIT_HLIST_NODE(&handler->list);
		rc = smc_wr_rx_register_handler(handler);
		if (rc)
			break;
	}
	return rc;
}
