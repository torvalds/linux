/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Connection Data Control (CDC)
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_CDC_H
#define SMC_CDC_H

#include <linux/kernel.h> /* max_t */
#include <linux/atomic.h>
#include <linux/in.h>
#include <linux/compiler.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_wr.h"

#define	SMC_CDC_MSG_TYPE		0xFE

/* in network byte order */
union smc_cdc_cursor {		/* SMC cursor */
	struct {
		__be16	reserved;
		__be16	wrap;
		__be32	count;
	};
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_t	acurs;		/* for atomic processing */
#else
	u64		acurs;		/* for atomic processing */
#endif
} __aligned(8);

/* in network byte order */
struct smc_cdc_msg {
	struct smc_wr_rx_hdr		common; /* .type = 0xFE */
	u8				len;	/* 44 */
	__be16				seqno;
	__be32				token;
	union smc_cdc_cursor		prod;
	union smc_cdc_cursor		cons;	/* piggy backed "ack" */
	struct smc_cdc_producer_flags	prod_flags;
	struct smc_cdc_conn_state_flags	conn_state_flags;
	u8				reserved[18];
};

/* SMC-D cursor format */
union smcd_cdc_cursor {
	struct {
		u16	wrap;
		u32	count;
		struct smc_cdc_producer_flags	prod_flags;
		struct smc_cdc_conn_state_flags	conn_state_flags;
	} __packed;
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_t		acurs;		/* for atomic processing */
#else
	u64			acurs;		/* for atomic processing */
#endif
} __aligned(8);

/* CDC message for SMC-D */
struct smcd_cdc_msg {
	struct smc_wr_rx_hdr common;	/* Type = 0xFE */
	u8 res1[7];
	union smcd_cdc_cursor	prod;
	union smcd_cdc_cursor	cons;
	u8 res3[8];
} __aligned(8);

static inline bool smc_cdc_rxed_any_close(struct smc_connection *conn)
{
	return conn->local_rx_ctrl.conn_state_flags.peer_conn_abort ||
	       conn->local_rx_ctrl.conn_state_flags.peer_conn_closed;
}

static inline bool smc_cdc_rxed_any_close_or_senddone(
	struct smc_connection *conn)
{
	return smc_cdc_rxed_any_close(conn) ||
	       conn->local_rx_ctrl.conn_state_flags.peer_done_writing;
}

static inline void smc_curs_add(int size, union smc_host_cursor *curs,
				int value)
{
	curs->count += value;
	if (curs->count >= size) {
		curs->wrap++;
		curs->count -= size;
	}
}

/* SMC cursors are 8 bytes long and require atomic reading and writing */
static inline u64 smc_curs_read(union smc_host_cursor *curs,
				struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;
	u64 ret;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	ret = curs->acurs;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
	return ret;
#else
	return atomic64_read(&curs->acurs);
#endif
}

/* Copy cursor src into tgt */
static inline void smc_curs_copy(union smc_host_cursor *tgt,
				 union smc_host_cursor *src,
				 struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	tgt->acurs = src->acurs;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
#else
	atomic64_set(&tgt->acurs, atomic64_read(&src->acurs));
#endif
}

static inline void smc_curs_copy_net(union smc_cdc_cursor *tgt,
				     union smc_cdc_cursor *src,
				     struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	tgt->acurs = src->acurs;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
#else
	atomic64_set(&tgt->acurs, atomic64_read(&src->acurs));
#endif
}

static inline void smcd_curs_copy(union smcd_cdc_cursor *tgt,
				  union smcd_cdc_cursor *src,
				  struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	tgt->acurs = src->acurs;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
#else
	atomic64_set(&tgt->acurs, atomic64_read(&src->acurs));
#endif
}

/* calculate cursor difference between old and new, where old <= new and
 * difference cannot exceed size
 */
static inline int smc_curs_diff(unsigned int size,
				union smc_host_cursor *old,
				union smc_host_cursor *new)
{
	if (old->wrap != new->wrap)
		return max_t(int, 0,
			     ((size - old->count) + new->count));

	return max_t(int, 0, (new->count - old->count));
}

/* calculate cursor difference between old and new - returns negative
 * value in case old > new
 */
static inline int smc_curs_comp(unsigned int size,
				union smc_host_cursor *old,
				union smc_host_cursor *new)
{
	if (old->wrap > new->wrap ||
	    (old->wrap == new->wrap && old->count > new->count))
		return -smc_curs_diff(size, new, old);
	return smc_curs_diff(size, old, new);
}

/* calculate cursor difference between old and new, where old <= new and
 * difference may exceed size
 */
static inline int smc_curs_diff_large(unsigned int size,
				      union smc_host_cursor *old,
				      union smc_host_cursor *new)
{
	if (old->wrap < new->wrap)
		return min_t(int,
			     (size - old->count) + new->count +
			     (new->wrap - old->wrap - 1) * size,
			     size);

	if (old->wrap > new->wrap) /* wrap has switched from 0xffff to 0x0000 */
		return min_t(int,
			     (size - old->count) + new->count +
			     (new->wrap + 0xffff - old->wrap) * size,
			     size);

	return max_t(int, 0, (new->count - old->count));
}

static inline void smc_host_cursor_to_cdc(union smc_cdc_cursor *peer,
					  union smc_host_cursor *local,
					  union smc_host_cursor *save,
					  struct smc_connection *conn)
{
	smc_curs_copy(save, local, conn);
	peer->count = htonl(save->count);
	peer->wrap = htons(save->wrap);
	/* peer->reserved = htons(0); must be ensured by caller */
}

static inline void smc_host_msg_to_cdc(struct smc_cdc_msg *peer,
				       struct smc_connection *conn,
				       union smc_host_cursor *save)
{
	struct smc_host_cdc_msg *local = &conn->local_tx_ctrl;

	peer->common.type = local->common.type;
	peer->len = local->len;
	peer->seqno = htons(local->seqno);
	peer->token = htonl(local->token);
	smc_host_cursor_to_cdc(&peer->prod, &local->prod, save, conn);
	smc_host_cursor_to_cdc(&peer->cons, &local->cons, save, conn);
	peer->prod_flags = local->prod_flags;
	peer->conn_state_flags = local->conn_state_flags;
}

static inline void smc_cdc_cursor_to_host(union smc_host_cursor *local,
					  union smc_cdc_cursor *peer,
					  struct smc_connection *conn)
{
	union smc_host_cursor temp, old;
	union smc_cdc_cursor net;

	smc_curs_copy(&old, local, conn);
	smc_curs_copy_net(&net, peer, conn);
	temp.count = ntohl(net.count);
	temp.wrap = ntohs(net.wrap);
	if ((old.wrap > temp.wrap) && temp.wrap)
		return;
	if ((old.wrap == temp.wrap) &&
	    (old.count > temp.count))
		return;
	smc_curs_copy(local, &temp, conn);
}

static inline void smcr_cdc_msg_to_host(struct smc_host_cdc_msg *local,
					struct smc_cdc_msg *peer,
					struct smc_connection *conn)
{
	local->common.type = peer->common.type;
	local->len = peer->len;
	local->seqno = ntohs(peer->seqno);
	local->token = ntohl(peer->token);
	smc_cdc_cursor_to_host(&local->prod, &peer->prod, conn);
	smc_cdc_cursor_to_host(&local->cons, &peer->cons, conn);
	local->prod_flags = peer->prod_flags;
	local->conn_state_flags = peer->conn_state_flags;
}

static inline void smcd_cdc_msg_to_host(struct smc_host_cdc_msg *local,
					struct smcd_cdc_msg *peer)
{
	union smc_host_cursor temp;

	temp.wrap = peer->prod.wrap;
	temp.count = peer->prod.count;
	atomic64_set(&local->prod.acurs, atomic64_read(&temp.acurs));

	temp.wrap = peer->cons.wrap;
	temp.count = peer->cons.count;
	atomic64_set(&local->cons.acurs, atomic64_read(&temp.acurs));
	local->prod_flags = peer->cons.prod_flags;
	local->conn_state_flags = peer->cons.conn_state_flags;
}

static inline void smc_cdc_msg_to_host(struct smc_host_cdc_msg *local,
				       struct smc_cdc_msg *peer,
				       struct smc_connection *conn)
{
	if (conn->lgr->is_smcd)
		smcd_cdc_msg_to_host(local, (struct smcd_cdc_msg *)peer);
	else
		smcr_cdc_msg_to_host(local, peer, conn);
}

struct smc_cdc_tx_pend {
	struct smc_connection	*conn;		/* socket connection */
	union smc_host_cursor	cursor;		/* tx sndbuf cursor sent */
	union smc_host_cursor	p_cursor;	/* rx RMBE cursor produced */
	u16			ctrl_seq;	/* conn. tx sequence # */
};

int smc_cdc_get_free_slot(struct smc_connection *conn,
			  struct smc_wr_buf **wr_buf,
			  struct smc_rdma_wr **wr_rdma_buf,
			  struct smc_cdc_tx_pend **pend);
void smc_cdc_tx_dismiss_slots(struct smc_connection *conn);
int smc_cdc_msg_send(struct smc_connection *conn, struct smc_wr_buf *wr_buf,
		     struct smc_cdc_tx_pend *pend);
int smc_cdc_get_slot_and_msg_send(struct smc_connection *conn);
int smcd_cdc_msg_send(struct smc_connection *conn);
int smc_cdc_init(void) __init;
void smcd_cdc_rx_init(struct smc_connection *conn);

#endif /* SMC_CDC_H */
