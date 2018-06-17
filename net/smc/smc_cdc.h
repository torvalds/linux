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
} __packed;					/* format defined in RFC7609 */

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

static inline u64 smc_curs_read_net(union smc_cdc_cursor *curs,
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

static inline void smc_curs_write(union smc_host_cursor *curs, u64 val,
				  struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	curs->acurs = val;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
#else
	atomic64_set(&curs->acurs, val);
#endif
}

static inline void smc_curs_write_net(union smc_cdc_cursor *curs, u64 val,
				      struct smc_connection *conn)
{
#ifndef KERNEL_HAS_ATOMIC64
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	curs->acurs = val;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
#else
	atomic64_set(&curs->acurs, val);
#endif
}

/* calculate cursor difference between old and new, where old <= new */
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

static inline void smc_host_cursor_to_cdc(union smc_cdc_cursor *peer,
					  union smc_host_cursor *local,
					  struct smc_connection *conn)
{
	union smc_host_cursor temp;

	smc_curs_write(&temp, smc_curs_read(local, conn), conn);
	peer->count = htonl(temp.count);
	peer->wrap = htons(temp.wrap);
	/* peer->reserved = htons(0); must be ensured by caller */
}

static inline void smc_host_msg_to_cdc(struct smc_cdc_msg *peer,
				       struct smc_host_cdc_msg *local,
				       struct smc_connection *conn)
{
	peer->common.type = local->common.type;
	peer->len = local->len;
	peer->seqno = htons(local->seqno);
	peer->token = htonl(local->token);
	smc_host_cursor_to_cdc(&peer->prod, &local->prod, conn);
	smc_host_cursor_to_cdc(&peer->cons, &local->cons, conn);
	peer->prod_flags = local->prod_flags;
	peer->conn_state_flags = local->conn_state_flags;
}

static inline void smc_cdc_cursor_to_host(union smc_host_cursor *local,
					  union smc_cdc_cursor *peer,
					  struct smc_connection *conn)
{
	union smc_host_cursor temp, old;
	union smc_cdc_cursor net;

	smc_curs_write(&old, smc_curs_read(local, conn), conn);
	smc_curs_write_net(&net, smc_curs_read_net(peer, conn), conn);
	temp.count = ntohl(net.count);
	temp.wrap = ntohs(net.wrap);
	if ((old.wrap > temp.wrap) && temp.wrap)
		return;
	if ((old.wrap == temp.wrap) &&
	    (old.count > temp.count))
		return;
	smc_curs_write(local, smc_curs_read(&temp, conn), conn);
}

static inline void smc_cdc_msg_to_host(struct smc_host_cdc_msg *local,
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

struct smc_cdc_tx_pend;

int smc_cdc_get_free_slot(struct smc_connection *conn,
			  struct smc_wr_buf **wr_buf,
			  struct smc_cdc_tx_pend **pend);
void smc_cdc_tx_dismiss_slots(struct smc_connection *conn);
int smc_cdc_msg_send(struct smc_connection *conn, struct smc_wr_buf *wr_buf,
		     struct smc_cdc_tx_pend *pend);
int smc_cdc_get_slot_and_msg_send(struct smc_connection *conn);
int smc_cdc_init(void) __init;

#endif /* SMC_CDC_H */
