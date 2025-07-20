// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 EIA Electronics,
//                         Kurt Van Dijck <kurt.van.dijck@eia.be>
// Copyright (c) 2018 Protonic,
//                         Robin van der Gracht <robin@protonic.nl>
// Copyright (c) 2017-2019 Pengutronix,
//                         Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2017-2019 Pengutronix,
//                         Oleksij Rempel <kernel@pengutronix.de>

#include <linux/can/skb.h>

#include "j1939-priv.h"

#define J1939_XTP_TX_RETRY_LIMIT 100

#define J1939_ETP_PGN_CTL 0xc800
#define J1939_ETP_PGN_DAT 0xc700
#define J1939_TP_PGN_CTL 0xec00
#define J1939_TP_PGN_DAT 0xeb00

#define J1939_TP_CMD_RTS 0x10
#define J1939_TP_CMD_CTS 0x11
#define J1939_TP_CMD_EOMA 0x13
#define J1939_TP_CMD_BAM 0x20
#define J1939_TP_CMD_ABORT 0xff

#define J1939_ETP_CMD_RTS 0x14
#define J1939_ETP_CMD_CTS 0x15
#define J1939_ETP_CMD_DPO 0x16
#define J1939_ETP_CMD_EOMA 0x17
#define J1939_ETP_CMD_ABORT 0xff

enum j1939_xtp_abort {
	J1939_XTP_NO_ABORT = 0,
	J1939_XTP_ABORT_BUSY = 1,
	/* Already in one or more connection managed sessions and
	 * cannot support another.
	 *
	 * EALREADY:
	 * Operation already in progress
	 */

	J1939_XTP_ABORT_RESOURCE = 2,
	/* System resources were needed for another task so this
	 * connection managed session was terminated.
	 *
	 * EMSGSIZE:
	 * The socket type requires that message be sent atomically,
	 * and the size of the message to be sent made this
	 * impossible.
	 */

	J1939_XTP_ABORT_TIMEOUT = 3,
	/* A timeout occurred and this is the connection abort to
	 * close the session.
	 *
	 * EHOSTUNREACH:
	 * The destination host cannot be reached (probably because
	 * the host is down or a remote router cannot reach it).
	 */

	J1939_XTP_ABORT_GENERIC = 4,
	/* CTS messages received when data transfer is in progress
	 *
	 * EBADMSG:
	 * Not a data message
	 */

	J1939_XTP_ABORT_FAULT = 5,
	/* Maximal retransmit request limit reached
	 *
	 * ENOTRECOVERABLE:
	 * State not recoverable
	 */

	J1939_XTP_ABORT_UNEXPECTED_DATA = 6,
	/* Unexpected data transfer packet
	 *
	 * ENOTCONN:
	 * Transport endpoint is not connected
	 */

	J1939_XTP_ABORT_BAD_SEQ = 7,
	/* Bad sequence number (and software is not able to recover)
	 *
	 * EILSEQ:
	 * Illegal byte sequence
	 */

	J1939_XTP_ABORT_DUP_SEQ = 8,
	/* Duplicate sequence number (and software is not able to
	 * recover)
	 */

	J1939_XTP_ABORT_EDPO_UNEXPECTED = 9,
	/* Unexpected EDPO packet (ETP) or Message size > 1785 bytes
	 * (TP)
	 */

	J1939_XTP_ABORT_BAD_EDPO_PGN = 10,
	/* Unexpected EDPO PGN (PGN in EDPO is bad) */

	J1939_XTP_ABORT_EDPO_OUTOF_CTS = 11,
	/* EDPO number of packets is greater than CTS */

	J1939_XTP_ABORT_BAD_EDPO_OFFSET = 12,
	/* Bad EDPO offset */

	J1939_XTP_ABORT_OTHER_DEPRECATED = 13,
	/* Deprecated. Use 250 instead (Any other reason)  */

	J1939_XTP_ABORT_ECTS_UNXPECTED_PGN = 14,
	/* Unexpected ECTS PGN (PGN in ECTS is bad) */

	J1939_XTP_ABORT_ECTS_TOO_BIG = 15,
	/* ECTS requested packets exceeds message size */

	J1939_XTP_ABORT_OTHER = 250,
	/* Any other reason (if a Connection Abort reason is
	 * identified that is not listed in the table use code 250)
	 */
};

static unsigned int j1939_tp_block = 255;
static unsigned int j1939_tp_packet_delay;
static unsigned int j1939_tp_padding = 1;

/* helpers */
static const char *j1939_xtp_abort_to_str(enum j1939_xtp_abort abort)
{
	switch (abort) {
	case J1939_XTP_ABORT_BUSY:
		return "Already in one or more connection managed sessions and cannot support another.";
	case J1939_XTP_ABORT_RESOURCE:
		return "System resources were needed for another task so this connection managed session was terminated.";
	case J1939_XTP_ABORT_TIMEOUT:
		return "A timeout occurred and this is the connection abort to close the session.";
	case J1939_XTP_ABORT_GENERIC:
		return "CTS messages received when data transfer is in progress";
	case J1939_XTP_ABORT_FAULT:
		return "Maximal retransmit request limit reached";
	case J1939_XTP_ABORT_UNEXPECTED_DATA:
		return "Unexpected data transfer packet";
	case J1939_XTP_ABORT_BAD_SEQ:
		return "Bad sequence number (and software is not able to recover)";
	case J1939_XTP_ABORT_DUP_SEQ:
		return "Duplicate sequence number (and software is not able to recover)";
	case J1939_XTP_ABORT_EDPO_UNEXPECTED:
		return "Unexpected EDPO packet (ETP) or Message size > 1785 bytes (TP)";
	case J1939_XTP_ABORT_BAD_EDPO_PGN:
		return "Unexpected EDPO PGN (PGN in EDPO is bad)";
	case J1939_XTP_ABORT_EDPO_OUTOF_CTS:
		return "EDPO number of packets is greater than CTS";
	case J1939_XTP_ABORT_BAD_EDPO_OFFSET:
		return "Bad EDPO offset";
	case J1939_XTP_ABORT_OTHER_DEPRECATED:
		return "Deprecated. Use 250 instead (Any other reason)";
	case J1939_XTP_ABORT_ECTS_UNXPECTED_PGN:
		return "Unexpected ECTS PGN (PGN in ECTS is bad)";
	case J1939_XTP_ABORT_ECTS_TOO_BIG:
		return "ECTS requested packets exceeds message size";
	case J1939_XTP_ABORT_OTHER:
		return "Any other reason (if a Connection Abort reason is identified that is not listed in the table use code 250)";
	default:
		return "<unknown>";
	}
}

static int j1939_xtp_abort_to_errno(struct j1939_priv *priv,
				    enum j1939_xtp_abort abort)
{
	int err;

	switch (abort) {
	case J1939_XTP_NO_ABORT:
		WARN_ON_ONCE(abort == J1939_XTP_NO_ABORT);
		err = 0;
		break;
	case J1939_XTP_ABORT_BUSY:
		err = EALREADY;
		break;
	case J1939_XTP_ABORT_RESOURCE:
		err = EMSGSIZE;
		break;
	case J1939_XTP_ABORT_TIMEOUT:
		err = EHOSTUNREACH;
		break;
	case J1939_XTP_ABORT_GENERIC:
		err = EBADMSG;
		break;
	case J1939_XTP_ABORT_FAULT:
		err = ENOTRECOVERABLE;
		break;
	case J1939_XTP_ABORT_UNEXPECTED_DATA:
		err = ENOTCONN;
		break;
	case J1939_XTP_ABORT_BAD_SEQ:
		err = EILSEQ;
		break;
	case J1939_XTP_ABORT_DUP_SEQ:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_EDPO_UNEXPECTED:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_BAD_EDPO_PGN:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_EDPO_OUTOF_CTS:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_BAD_EDPO_OFFSET:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_OTHER_DEPRECATED:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_ECTS_UNXPECTED_PGN:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_ECTS_TOO_BIG:
		err = EPROTO;
		break;
	case J1939_XTP_ABORT_OTHER:
		err = EPROTO;
		break;
	default:
		netdev_warn(priv->ndev, "Unknown abort code %i", abort);
		err = EPROTO;
	}

	return err;
}

static inline void j1939_session_list_lock(struct j1939_priv *priv)
{
	spin_lock_bh(&priv->active_session_list_lock);
}

static inline void j1939_session_list_unlock(struct j1939_priv *priv)
{
	spin_unlock_bh(&priv->active_session_list_lock);
}

void j1939_session_get(struct j1939_session *session)
{
	kref_get(&session->kref);
}

/* session completion functions */
static void __j1939_session_drop(struct j1939_session *session)
{
	if (!session->transmission)
		return;

	j1939_sock_pending_del(session->sk);
	sock_put(session->sk);
}

static void j1939_session_destroy(struct j1939_session *session)
{
	struct sk_buff *skb;

	if (session->transmission) {
		if (session->err)
			j1939_sk_errqueue(session, J1939_ERRQUEUE_TX_ABORT);
		else
			j1939_sk_errqueue(session, J1939_ERRQUEUE_TX_ACK);
	} else if (session->err) {
			j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_ABORT);
	}

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	WARN_ON_ONCE(!list_empty(&session->sk_session_queue_entry));
	WARN_ON_ONCE(!list_empty(&session->active_session_list_entry));

	while ((skb = skb_dequeue(&session->skb_queue)) != NULL) {
		/* drop ref taken in j1939_session_skb_queue() */
		skb_unref(skb);
		kfree_skb(skb);
	}
	__j1939_session_drop(session);
	j1939_priv_put(session->priv);
	kfree(session);
}

static void __j1939_session_release(struct kref *kref)
{
	struct j1939_session *session = container_of(kref, struct j1939_session,
						     kref);

	j1939_session_destroy(session);
}

void j1939_session_put(struct j1939_session *session)
{
	kref_put(&session->kref, __j1939_session_release);
}

static void j1939_session_txtimer_cancel(struct j1939_session *session)
{
	if (hrtimer_cancel(&session->txtimer))
		j1939_session_put(session);
}

static void j1939_session_rxtimer_cancel(struct j1939_session *session)
{
	if (hrtimer_cancel(&session->rxtimer))
		j1939_session_put(session);
}

void j1939_session_timers_cancel(struct j1939_session *session)
{
	j1939_session_txtimer_cancel(session);
	j1939_session_rxtimer_cancel(session);
}

static inline bool j1939_cb_is_broadcast(const struct j1939_sk_buff_cb *skcb)
{
	return (!skcb->addr.dst_name && (skcb->addr.da == 0xff));
}

static void j1939_session_skb_drop_old(struct j1939_session *session)
{
	struct sk_buff *do_skb;
	struct j1939_sk_buff_cb *do_skcb;
	unsigned int offset_start;
	unsigned long flags;

	if (skb_queue_len(&session->skb_queue) < 2)
		return;

	offset_start = session->pkt.tx_acked * 7;

	spin_lock_irqsave(&session->skb_queue.lock, flags);
	do_skb = skb_peek(&session->skb_queue);
	do_skcb = j1939_skb_to_cb(do_skb);

	if ((do_skcb->offset + do_skb->len) < offset_start) {
		__skb_unlink(do_skb, &session->skb_queue);
		/* drop ref taken in j1939_session_skb_queue() */
		skb_unref(do_skb);
		spin_unlock_irqrestore(&session->skb_queue.lock, flags);

		kfree_skb(do_skb);
	} else {
		spin_unlock_irqrestore(&session->skb_queue.lock, flags);
	}
}

void j1939_session_skb_queue(struct j1939_session *session,
			     struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_priv *priv = session->priv;

	j1939_ac_fixup(priv, skb);

	if (j1939_address_is_unicast(skcb->addr.da) &&
	    priv->ents[skcb->addr.da].nusers)
		skcb->flags |= J1939_ECU_LOCAL_DST;

	skcb->flags |= J1939_ECU_LOCAL_SRC;

	skb_get(skb);
	skb_queue_tail(&session->skb_queue, skb);
}

static struct
sk_buff *j1939_session_skb_get_by_offset(struct j1939_session *session,
					 unsigned int offset_start)
{
	struct j1939_priv *priv = session->priv;
	struct j1939_sk_buff_cb *do_skcb;
	struct sk_buff *skb = NULL;
	struct sk_buff *do_skb;
	unsigned long flags;

	spin_lock_irqsave(&session->skb_queue.lock, flags);
	skb_queue_walk(&session->skb_queue, do_skb) {
		do_skcb = j1939_skb_to_cb(do_skb);

		if ((offset_start >= do_skcb->offset &&
		     offset_start < (do_skcb->offset + do_skb->len)) ||
		     (offset_start == 0 && do_skcb->offset == 0 && do_skb->len == 0)) {
			skb = do_skb;
		}
	}

	if (skb)
		skb_get(skb);

	spin_unlock_irqrestore(&session->skb_queue.lock, flags);

	if (!skb)
		netdev_dbg(priv->ndev, "%s: 0x%p: no skb found for start: %i, queue size: %i\n",
			   __func__, session, offset_start,
			   skb_queue_len(&session->skb_queue));

	return skb;
}

static struct sk_buff *j1939_session_skb_get(struct j1939_session *session)
{
	unsigned int offset_start;

	offset_start = session->pkt.dpo * 7;
	return j1939_session_skb_get_by_offset(session, offset_start);
}

/* see if we are receiver
 * returns 0 for broadcasts, although we will receive them
 */
static inline int j1939_tp_im_receiver(const struct j1939_sk_buff_cb *skcb)
{
	return skcb->flags & J1939_ECU_LOCAL_DST;
}

/* see if we are sender */
static inline int j1939_tp_im_transmitter(const struct j1939_sk_buff_cb *skcb)
{
	return skcb->flags & J1939_ECU_LOCAL_SRC;
}

/* see if we are involved as either receiver or transmitter */
static int j1939_tp_im_involved(const struct j1939_sk_buff_cb *skcb, bool swap)
{
	if (swap)
		return j1939_tp_im_receiver(skcb);
	else
		return j1939_tp_im_transmitter(skcb);
}

static int j1939_tp_im_involved_anydir(struct j1939_sk_buff_cb *skcb)
{
	return skcb->flags & (J1939_ECU_LOCAL_SRC | J1939_ECU_LOCAL_DST);
}

/* extract pgn from flow-ctl message */
static inline pgn_t j1939_xtp_ctl_to_pgn(const u8 *dat)
{
	pgn_t pgn;

	pgn = (dat[7] << 16) | (dat[6] << 8) | (dat[5] << 0);
	if (j1939_pgn_is_pdu1(pgn))
		pgn &= 0xffff00;
	return pgn;
}

static inline unsigned int j1939_tp_ctl_to_size(const u8 *dat)
{
	return (dat[2] << 8) + (dat[1] << 0);
}

static inline unsigned int j1939_etp_ctl_to_packet(const u8 *dat)
{
	return (dat[4] << 16) | (dat[3] << 8) | (dat[2] << 0);
}

static inline unsigned int j1939_etp_ctl_to_size(const u8 *dat)
{
	return (dat[4] << 24) | (dat[3] << 16) |
		(dat[2] << 8) | (dat[1] << 0);
}

/* find existing session:
 * reverse: swap cb's src & dst
 * there is no problem with matching broadcasts, since
 * broadcasts (no dst, no da) would never call this
 * with reverse == true
 */
static bool j1939_session_match(struct j1939_addr *se_addr,
				struct j1939_addr *sk_addr, bool reverse)
{
	if (se_addr->type != sk_addr->type)
		return false;

	if (reverse) {
		if (se_addr->src_name) {
			if (se_addr->src_name != sk_addr->dst_name)
				return false;
		} else if (se_addr->sa != sk_addr->da) {
			return false;
		}

		if (se_addr->dst_name) {
			if (se_addr->dst_name != sk_addr->src_name)
				return false;
		} else if (se_addr->da != sk_addr->sa) {
			return false;
		}
	} else {
		if (se_addr->src_name) {
			if (se_addr->src_name != sk_addr->src_name)
				return false;
		} else if (se_addr->sa != sk_addr->sa) {
			return false;
		}

		if (se_addr->dst_name) {
			if (se_addr->dst_name != sk_addr->dst_name)
				return false;
		} else if (se_addr->da != sk_addr->da) {
			return false;
		}
	}

	return true;
}

static struct
j1939_session *j1939_session_get_by_addr_locked(struct j1939_priv *priv,
						struct list_head *root,
						struct j1939_addr *addr,
						bool reverse, bool transmitter)
{
	struct j1939_session *session;

	lockdep_assert_held(&priv->active_session_list_lock);

	list_for_each_entry(session, root, active_session_list_entry) {
		j1939_session_get(session);
		if (j1939_session_match(&session->skcb.addr, addr, reverse) &&
		    session->transmission == transmitter)
			return session;
		j1939_session_put(session);
	}

	return NULL;
}

static struct
j1939_session *j1939_session_get_simple(struct j1939_priv *priv,
					struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;

	lockdep_assert_held(&priv->active_session_list_lock);

	list_for_each_entry(session, &priv->active_session_list,
			    active_session_list_entry) {
		j1939_session_get(session);
		if (session->skcb.addr.type == J1939_SIMPLE &&
		    session->tskey == skcb->tskey && session->sk == skb->sk)
			return session;
		j1939_session_put(session);
	}

	return NULL;
}

static struct
j1939_session *j1939_session_get_by_addr(struct j1939_priv *priv,
					 struct j1939_addr *addr,
					 bool reverse, bool transmitter)
{
	struct j1939_session *session;

	j1939_session_list_lock(priv);
	session = j1939_session_get_by_addr_locked(priv,
						   &priv->active_session_list,
						   addr, reverse, transmitter);
	j1939_session_list_unlock(priv);

	return session;
}

static void j1939_skbcb_swap(struct j1939_sk_buff_cb *skcb)
{
	u8 tmp = 0;

	swap(skcb->addr.dst_name, skcb->addr.src_name);
	swap(skcb->addr.da, skcb->addr.sa);

	/* swap SRC and DST flags, leave other untouched */
	if (skcb->flags & J1939_ECU_LOCAL_SRC)
		tmp |= J1939_ECU_LOCAL_DST;
	if (skcb->flags & J1939_ECU_LOCAL_DST)
		tmp |= J1939_ECU_LOCAL_SRC;
	skcb->flags &= ~(J1939_ECU_LOCAL_SRC | J1939_ECU_LOCAL_DST);
	skcb->flags |= tmp;
}

static struct
sk_buff *j1939_tp_tx_dat_new(struct j1939_priv *priv,
			     const struct j1939_sk_buff_cb *re_skcb,
			     bool ctl,
			     bool swap_src_dst)
{
	struct sk_buff *skb;
	struct j1939_sk_buff_cb *skcb;

	skb = alloc_skb(sizeof(struct can_frame) + sizeof(struct can_skb_priv),
			GFP_ATOMIC);
	if (unlikely(!skb))
		return ERR_PTR(-ENOMEM);

	skb->dev = priv->ndev;
	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = priv->ndev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;
	/* reserve CAN header */
	skb_reserve(skb, offsetof(struct can_frame, data));

	/* skb->cb must be large enough to hold a j1939_sk_buff_cb structure */
	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(*re_skcb));

	memcpy(skb->cb, re_skcb, sizeof(*re_skcb));
	skcb = j1939_skb_to_cb(skb);
	if (swap_src_dst)
		j1939_skbcb_swap(skcb);

	if (ctl) {
		if (skcb->addr.type == J1939_ETP)
			skcb->addr.pgn = J1939_ETP_PGN_CTL;
		else
			skcb->addr.pgn = J1939_TP_PGN_CTL;
	} else {
		if (skcb->addr.type == J1939_ETP)
			skcb->addr.pgn = J1939_ETP_PGN_DAT;
		else
			skcb->addr.pgn = J1939_TP_PGN_DAT;
	}

	return skb;
}

/* TP transmit packet functions */
static int j1939_tp_tx_dat(struct j1939_session *session,
			   const u8 *dat, int len)
{
	struct j1939_priv *priv = session->priv;
	struct sk_buff *skb;

	skb = j1939_tp_tx_dat_new(priv, &session->skcb,
				  false, false);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	skb_put_data(skb, dat, len);
	if (j1939_tp_padding && len < 8)
		memset(skb_put(skb, 8 - len), 0xff, 8 - len);

	return j1939_send_one(priv, skb);
}

static int j1939_xtp_do_tx_ctl(struct j1939_priv *priv,
			       const struct j1939_sk_buff_cb *re_skcb,
			       bool swap_src_dst, pgn_t pgn, const u8 *dat)
{
	struct sk_buff *skb;
	u8 *skdat;

	if (!j1939_tp_im_involved(re_skcb, swap_src_dst))
		return 0;

	skb = j1939_tp_tx_dat_new(priv, re_skcb, true, swap_src_dst);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	skdat = skb_put(skb, 8);
	memcpy(skdat, dat, 5);
	skdat[5] = (pgn >> 0);
	skdat[6] = (pgn >> 8);
	skdat[7] = (pgn >> 16);

	return j1939_send_one(priv, skb);
}

static inline int j1939_tp_tx_ctl(struct j1939_session *session,
				  bool swap_src_dst, const u8 *dat)
{
	struct j1939_priv *priv = session->priv;

	return j1939_xtp_do_tx_ctl(priv, &session->skcb,
				   swap_src_dst,
				   session->skcb.addr.pgn, dat);
}

static int j1939_xtp_tx_abort(struct j1939_priv *priv,
			      const struct j1939_sk_buff_cb *re_skcb,
			      bool swap_src_dst,
			      enum j1939_xtp_abort err,
			      pgn_t pgn)
{
	u8 dat[5];

	if (!j1939_tp_im_involved(re_skcb, swap_src_dst))
		return 0;

	memset(dat, 0xff, sizeof(dat));
	dat[0] = J1939_TP_CMD_ABORT;
	dat[1] = err;
	return j1939_xtp_do_tx_ctl(priv, re_skcb, swap_src_dst, pgn, dat);
}

void j1939_tp_schedule_txtimer(struct j1939_session *session, int msec)
{
	j1939_session_get(session);
	hrtimer_start(&session->txtimer, ms_to_ktime(msec),
		      HRTIMER_MODE_REL_SOFT);
}

static inline void j1939_tp_set_rxtimeout(struct j1939_session *session,
					  int msec)
{
	j1939_session_rxtimer_cancel(session);
	j1939_session_get(session);
	hrtimer_start(&session->rxtimer, ms_to_ktime(msec),
		      HRTIMER_MODE_REL_SOFT);
}

static int j1939_session_tx_rts(struct j1939_session *session)
{
	u8 dat[8];
	int ret;

	memset(dat, 0xff, sizeof(dat));

	dat[1] = (session->total_message_size >> 0);
	dat[2] = (session->total_message_size >> 8);
	dat[3] = session->pkt.total;

	if (session->skcb.addr.type == J1939_ETP) {
		dat[0] = J1939_ETP_CMD_RTS;
		dat[1] = (session->total_message_size >> 0);
		dat[2] = (session->total_message_size >> 8);
		dat[3] = (session->total_message_size >> 16);
		dat[4] = (session->total_message_size >> 24);
	} else if (j1939_cb_is_broadcast(&session->skcb)) {
		dat[0] = J1939_TP_CMD_BAM;
		/* fake cts for broadcast */
		session->pkt.tx = 0;
	} else {
		dat[0] = J1939_TP_CMD_RTS;
		dat[4] = dat[3];
	}

	if (dat[0] == session->last_txcmd)
		/* done already */
		return 0;

	ret = j1939_tp_tx_ctl(session, false, dat);
	if (ret < 0)
		return ret;

	session->last_txcmd = dat[0];
	if (dat[0] == J1939_TP_CMD_BAM) {
		j1939_tp_schedule_txtimer(session, 50);
		j1939_tp_set_rxtimeout(session, 250);
	} else {
		j1939_tp_set_rxtimeout(session, 1250);
	}

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	return 0;
}

static int j1939_session_tx_dpo(struct j1939_session *session)
{
	unsigned int pkt;
	u8 dat[8];
	int ret;

	memset(dat, 0xff, sizeof(dat));

	dat[0] = J1939_ETP_CMD_DPO;
	session->pkt.dpo = session->pkt.tx_acked;
	pkt = session->pkt.dpo;
	dat[1] = session->pkt.last - session->pkt.tx_acked;
	dat[2] = (pkt >> 0);
	dat[3] = (pkt >> 8);
	dat[4] = (pkt >> 16);

	ret = j1939_tp_tx_ctl(session, false, dat);
	if (ret < 0)
		return ret;

	session->last_txcmd = dat[0];
	j1939_tp_set_rxtimeout(session, 1250);
	session->pkt.tx = session->pkt.tx_acked;

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	return 0;
}

static int j1939_session_tx_dat(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	struct j1939_sk_buff_cb *se_skcb;
	int offset, pkt_done, pkt_end;
	unsigned int len, pdelay;
	struct sk_buff *se_skb;
	const u8 *tpdat;
	int ret = 0;
	u8 dat[8];

	se_skb = j1939_session_skb_get_by_offset(session, session->pkt.tx * 7);
	if (!se_skb)
		return -ENOBUFS;

	se_skcb = j1939_skb_to_cb(se_skb);
	tpdat = se_skb->data;
	ret = 0;
	pkt_done = 0;
	if (session->skcb.addr.type != J1939_ETP &&
	    j1939_cb_is_broadcast(&session->skcb))
		pkt_end = session->pkt.total;
	else
		pkt_end = session->pkt.last;

	while (session->pkt.tx < pkt_end) {
		dat[0] = session->pkt.tx - session->pkt.dpo + 1;
		offset = (session->pkt.tx * 7) - se_skcb->offset;
		len =  se_skb->len - offset;
		if (len > 7)
			len = 7;

		if (offset + len > se_skb->len) {
			netdev_err_once(priv->ndev,
					"%s: 0x%p: requested data outside of queued buffer: offset %i, len %i, pkt.tx: %i\n",
					__func__, session, se_skcb->offset,
					se_skb->len , session->pkt.tx);
			ret = -EOVERFLOW;
			goto out_free;
		}

		if (!len) {
			ret = -ENOBUFS;
			break;
		}

		memcpy(&dat[1], &tpdat[offset], len);
		ret = j1939_tp_tx_dat(session, dat, len + 1);
		if (ret < 0) {
			/* ENOBUFS == CAN interface TX queue is full */
			if (ret != -ENOBUFS)
				netdev_alert(priv->ndev,
					     "%s: 0x%p: queue data error: %i\n",
					     __func__, session, ret);
			break;
		}

		session->last_txcmd = 0xff;
		pkt_done++;
		session->pkt.tx++;
		pdelay = j1939_cb_is_broadcast(&session->skcb) ? 50 :
			j1939_tp_packet_delay;

		if (session->pkt.tx < session->pkt.total && pdelay) {
			j1939_tp_schedule_txtimer(session, pdelay);
			break;
		}
	}

	if (pkt_done)
		j1939_tp_set_rxtimeout(session, 250);

 out_free:
	if (ret)
		kfree_skb(se_skb);
	else
		consume_skb(se_skb);

	return ret;
}

static int j1939_xtp_txnext_transmiter(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	int ret = 0;

	if (!j1939_tp_im_transmitter(&session->skcb)) {
		netdev_alert(priv->ndev, "%s: 0x%p: called by not transmitter!\n",
			     __func__, session);
		return -EINVAL;
	}

	switch (session->last_cmd) {
	case 0:
		ret = j1939_session_tx_rts(session);
		break;

	case J1939_ETP_CMD_CTS:
		if (session->last_txcmd != J1939_ETP_CMD_DPO) {
			ret = j1939_session_tx_dpo(session);
			if (ret)
				return ret;
		}

		fallthrough;
	case J1939_TP_CMD_CTS:
	case 0xff: /* did some data */
	case J1939_ETP_CMD_DPO:
	case J1939_TP_CMD_BAM:
		ret = j1939_session_tx_dat(session);

		break;
	default:
		netdev_alert(priv->ndev, "%s: 0x%p: unexpected last_cmd: %x\n",
			     __func__, session, session->last_cmd);
	}

	return ret;
}

static int j1939_session_tx_cts(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	unsigned int pkt, len;
	int ret;
	u8 dat[8];

	if (!j1939_sk_recv_match(priv, &session->skcb))
		return -ENOENT;

	len = session->pkt.total - session->pkt.rx;
	len = min3(len, session->pkt.block, j1939_tp_block ?: 255);
	memset(dat, 0xff, sizeof(dat));

	if (session->skcb.addr.type == J1939_ETP) {
		pkt = session->pkt.rx + 1;
		dat[0] = J1939_ETP_CMD_CTS;
		dat[1] = len;
		dat[2] = (pkt >> 0);
		dat[3] = (pkt >> 8);
		dat[4] = (pkt >> 16);
	} else {
		dat[0] = J1939_TP_CMD_CTS;
		dat[1] = len;
		dat[2] = session->pkt.rx + 1;
	}

	if (dat[0] == session->last_txcmd)
		/* done already */
		return 0;

	ret = j1939_tp_tx_ctl(session, true, dat);
	if (ret < 0)
		return ret;

	if (len)
		/* only mark cts done when len is set */
		session->last_txcmd = dat[0];
	j1939_tp_set_rxtimeout(session, 1250);

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	return 0;
}

static int j1939_session_tx_eoma(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	u8 dat[8];
	int ret;

	if (!j1939_sk_recv_match(priv, &session->skcb))
		return -ENOENT;

	memset(dat, 0xff, sizeof(dat));

	if (session->skcb.addr.type == J1939_ETP) {
		dat[0] = J1939_ETP_CMD_EOMA;
		dat[1] = session->total_message_size >> 0;
		dat[2] = session->total_message_size >> 8;
		dat[3] = session->total_message_size >> 16;
		dat[4] = session->total_message_size >> 24;
	} else {
		dat[0] = J1939_TP_CMD_EOMA;
		dat[1] = session->total_message_size;
		dat[2] = session->total_message_size >> 8;
		dat[3] = session->pkt.total;
	}

	if (dat[0] == session->last_txcmd)
		/* done already */
		return 0;

	ret = j1939_tp_tx_ctl(session, true, dat);
	if (ret < 0)
		return ret;

	session->last_txcmd = dat[0];

	/* wait for the EOMA packet to come in */
	j1939_tp_set_rxtimeout(session, 1250);

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	return 0;
}

static int j1939_xtp_txnext_receiver(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	int ret = 0;

	if (!j1939_tp_im_receiver(&session->skcb)) {
		netdev_alert(priv->ndev, "%s: 0x%p: called by not receiver!\n",
			     __func__, session);
		return -EINVAL;
	}

	switch (session->last_cmd) {
	case J1939_TP_CMD_RTS:
	case J1939_ETP_CMD_RTS:
		ret = j1939_session_tx_cts(session);
		break;

	case J1939_ETP_CMD_CTS:
	case J1939_TP_CMD_CTS:
	case 0xff: /* did some data */
	case J1939_ETP_CMD_DPO:
		if ((session->skcb.addr.type == J1939_TP &&
		     j1939_cb_is_broadcast(&session->skcb)))
			break;

		if (session->pkt.rx >= session->pkt.total) {
			ret = j1939_session_tx_eoma(session);
		} else if (session->pkt.rx >= session->pkt.last) {
			session->last_txcmd = 0;
			ret = j1939_session_tx_cts(session);
		}
		break;
	default:
		netdev_alert(priv->ndev, "%s: 0x%p: unexpected last_cmd: %x\n",
			     __func__, session, session->last_cmd);
	}

	return ret;
}

static int j1939_simple_txnext(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	struct sk_buff *se_skb = j1939_session_skb_get(session);
	struct sk_buff *skb;
	int ret;

	if (!se_skb)
		return 0;

	skb = skb_clone(se_skb, GFP_ATOMIC);
	if (!skb) {
		ret = -ENOMEM;
		goto out_free;
	}

	can_skb_set_owner(skb, se_skb->sk);

	j1939_tp_set_rxtimeout(session, J1939_SIMPLE_ECHO_TIMEOUT_MS);

	ret = j1939_send_one(priv, skb);
	if (ret)
		goto out_free;

	j1939_sk_errqueue(session, J1939_ERRQUEUE_TX_SCHED);
	j1939_sk_queue_activate_next(session);

 out_free:
	if (ret)
		kfree_skb(se_skb);
	else
		consume_skb(se_skb);

	return ret;
}

static bool j1939_session_deactivate_locked(struct j1939_session *session)
{
	bool active = false;

	lockdep_assert_held(&session->priv->active_session_list_lock);

	if (session->state >= J1939_SESSION_ACTIVE &&
	    session->state < J1939_SESSION_ACTIVE_MAX) {
		active = true;

		list_del_init(&session->active_session_list_entry);
		session->state = J1939_SESSION_DONE;
		j1939_session_put(session);
	}

	return active;
}

static bool j1939_session_deactivate(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	bool active;

	j1939_session_list_lock(priv);
	active = j1939_session_deactivate_locked(session);
	j1939_session_list_unlock(priv);

	return active;
}

static void
j1939_session_deactivate_activate_next(struct j1939_session *session)
{
	if (j1939_session_deactivate(session))
		j1939_sk_queue_activate_next(session);
}

static void __j1939_session_cancel(struct j1939_session *session,
				   enum j1939_xtp_abort err)
{
	struct j1939_priv *priv = session->priv;

	WARN_ON_ONCE(!err);
	lockdep_assert_held(&session->priv->active_session_list_lock);

	session->err = j1939_xtp_abort_to_errno(priv, err);
	session->state = J1939_SESSION_WAITING_ABORT;
	/* do not send aborts on incoming broadcasts */
	if (!j1939_cb_is_broadcast(&session->skcb)) {
		j1939_xtp_tx_abort(priv, &session->skcb,
				   !session->transmission,
				   err, session->skcb.addr.pgn);
	}

	if (session->sk)
		j1939_sk_send_loop_abort(session->sk, session->err);
}

static void j1939_session_cancel(struct j1939_session *session,
				 enum j1939_xtp_abort err)
{
	j1939_session_list_lock(session->priv);

	if (session->state >= J1939_SESSION_ACTIVE &&
	    session->state < J1939_SESSION_WAITING_ABORT) {
		j1939_tp_set_rxtimeout(session, J1939_XTP_ABORT_TIMEOUT_MS);
		__j1939_session_cancel(session, err);
	}

	j1939_session_list_unlock(session->priv);

	if (!session->sk)
		j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_ABORT);
}

static enum hrtimer_restart j1939_tp_txtimer(struct hrtimer *hrtimer)
{
	struct j1939_session *session =
		container_of(hrtimer, struct j1939_session, txtimer);
	struct j1939_priv *priv = session->priv;
	int ret = 0;

	if (session->skcb.addr.type == J1939_SIMPLE) {
		ret = j1939_simple_txnext(session);
	} else {
		if (session->transmission)
			ret = j1939_xtp_txnext_transmiter(session);
		else
			ret = j1939_xtp_txnext_receiver(session);
	}

	switch (ret) {
	case -ENOBUFS:
		/* Retry limit is currently arbitrary chosen */
		if (session->tx_retry < J1939_XTP_TX_RETRY_LIMIT) {
			session->tx_retry++;
			j1939_tp_schedule_txtimer(session,
						  10 + get_random_u32_below(16));
		} else {
			netdev_alert(priv->ndev, "%s: 0x%p: tx retry count reached\n",
				     __func__, session);
			session->err = -ENETUNREACH;
			j1939_session_rxtimer_cancel(session);
			j1939_session_deactivate_activate_next(session);
		}
		break;
	case -ENETDOWN:
		/* In this case we should get a netdev_event(), all active
		 * sessions will be cleared by j1939_cancel_active_session().
		 * So handle this as an error, but let
		 * j1939_cancel_active_session() do the cleanup including
		 * propagation of the error to user space.
		 */
		break;
	case -EOVERFLOW:
		j1939_session_cancel(session, J1939_XTP_ABORT_ECTS_TOO_BIG);
		break;
	case 0:
		session->tx_retry = 0;
		break;
	default:
		netdev_alert(priv->ndev, "%s: 0x%p: tx aborted with unknown reason: %i\n",
			     __func__, session, ret);
		if (session->skcb.addr.type != J1939_SIMPLE) {
			j1939_session_cancel(session, J1939_XTP_ABORT_OTHER);
		} else {
			session->err = ret;
			j1939_session_rxtimer_cancel(session);
			j1939_session_deactivate_activate_next(session);
		}
	}

	j1939_session_put(session);

	return HRTIMER_NORESTART;
}

static void j1939_session_completed(struct j1939_session *session)
{
	struct sk_buff *se_skb;

	if (!session->transmission) {
		se_skb = j1939_session_skb_get(session);
		/* distribute among j1939 receivers */
		j1939_sk_recv(session->priv, se_skb);
		consume_skb(se_skb);
	}

	j1939_session_deactivate_activate_next(session);
}

static enum hrtimer_restart j1939_tp_rxtimer(struct hrtimer *hrtimer)
{
	struct j1939_session *session = container_of(hrtimer,
						     struct j1939_session,
						     rxtimer);
	struct j1939_priv *priv = session->priv;

	if (session->state == J1939_SESSION_WAITING_ABORT) {
		netdev_alert(priv->ndev, "%s: 0x%p: abort rx timeout. Force session deactivation\n",
			     __func__, session);

		j1939_session_deactivate_activate_next(session);

	} else if (session->skcb.addr.type == J1939_SIMPLE) {
		netdev_alert(priv->ndev, "%s: 0x%p: Timeout. Failed to send simple message.\n",
			     __func__, session);

		/* The message is probably stuck in the CAN controller and can
		 * be send as soon as CAN bus is in working state again.
		 */
		session->err = -ETIME;
		j1939_session_deactivate(session);
	} else {
		j1939_session_list_lock(session->priv);
		if (session->state >= J1939_SESSION_ACTIVE &&
		    session->state < J1939_SESSION_ACTIVE_MAX) {
			netdev_alert(priv->ndev, "%s: 0x%p: rx timeout, send abort\n",
				     __func__, session);
			j1939_session_get(session);
			hrtimer_start(&session->rxtimer,
				      ms_to_ktime(J1939_XTP_ABORT_TIMEOUT_MS),
				      HRTIMER_MODE_REL_SOFT);
			__j1939_session_cancel(session, J1939_XTP_ABORT_TIMEOUT);
		}
		j1939_session_list_unlock(session->priv);

		if (!session->sk)
			j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_ABORT);
	}

	j1939_session_put(session);

	return HRTIMER_NORESTART;
}

static bool j1939_xtp_rx_cmd_bad_pgn(struct j1939_session *session,
				     const struct sk_buff *skb)
{
	const struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	pgn_t pgn = j1939_xtp_ctl_to_pgn(skb->data);
	struct j1939_priv *priv = session->priv;
	enum j1939_xtp_abort abort = J1939_XTP_NO_ABORT;
	u8 cmd = skb->data[0];

	if (session->skcb.addr.pgn == pgn)
		return false;

	switch (cmd) {
	case J1939_TP_CMD_BAM:
		abort = J1939_XTP_NO_ABORT;
		break;

	case J1939_ETP_CMD_RTS:
		fallthrough;
	case J1939_TP_CMD_RTS:
		abort = J1939_XTP_ABORT_BUSY;
		break;

	case J1939_ETP_CMD_CTS:
		fallthrough;
	case J1939_TP_CMD_CTS:
		abort = J1939_XTP_ABORT_ECTS_UNXPECTED_PGN;
		break;

	case J1939_ETP_CMD_DPO:
		abort = J1939_XTP_ABORT_BAD_EDPO_PGN;
		break;

	case J1939_ETP_CMD_EOMA:
		fallthrough;
	case J1939_TP_CMD_EOMA:
		abort = J1939_XTP_ABORT_OTHER;
		break;

	case J1939_ETP_CMD_ABORT: /* && J1939_TP_CMD_ABORT */
		abort = J1939_XTP_NO_ABORT;
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}

	netdev_warn(priv->ndev, "%s: 0x%p: CMD 0x%02x with PGN 0x%05x for running session with different PGN 0x%05x.\n",
		    __func__, session, cmd, pgn, session->skcb.addr.pgn);
	if (abort != J1939_XTP_NO_ABORT)
		j1939_xtp_tx_abort(priv, skcb, true, abort, pgn);

	return true;
}

static void j1939_xtp_rx_abort_one(struct j1939_priv *priv, struct sk_buff *skb,
				   bool reverse, bool transmitter)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;
	u8 abort = skb->data[1];

	session = j1939_session_get_by_addr(priv, &skcb->addr, reverse,
					    transmitter);
	if (!session)
		return;

	if (j1939_xtp_rx_cmd_bad_pgn(session, skb))
		goto abort_put;

	netdev_info(priv->ndev, "%s: 0x%p: 0x%05x: (%u) %s\n", __func__,
		    session, j1939_xtp_ctl_to_pgn(skb->data), abort,
		    j1939_xtp_abort_to_str(abort));

	j1939_session_timers_cancel(session);
	session->err = j1939_xtp_abort_to_errno(priv, abort);
	if (session->sk)
		j1939_sk_send_loop_abort(session->sk, session->err);
	else
		j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_ABORT);
	j1939_session_deactivate_activate_next(session);

abort_put:
	j1939_session_put(session);
}

/* abort packets may come in 2 directions */
static void
j1939_xtp_rx_abort(struct j1939_priv *priv, struct sk_buff *skb,
		   bool transmitter)
{
	j1939_xtp_rx_abort_one(priv, skb, false, transmitter);
	j1939_xtp_rx_abort_one(priv, skb, true, transmitter);
}

static void
j1939_xtp_rx_eoma_one(struct j1939_session *session, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	const u8 *dat;
	int len;

	if (j1939_xtp_rx_cmd_bad_pgn(session, skb))
		return;

	dat = skb->data;

	if (skcb->addr.type == J1939_ETP)
		len = j1939_etp_ctl_to_size(dat);
	else
		len = j1939_tp_ctl_to_size(dat);

	if (session->total_message_size != len) {
		netdev_warn_once(session->priv->ndev,
				 "%s: 0x%p: Incorrect size. Expected: %i; got: %i.\n",
				 __func__, session, session->total_message_size,
				 len);
	}

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	session->pkt.tx_acked = session->pkt.total;
	j1939_session_timers_cancel(session);
	/* transmitted without problems */
	j1939_session_completed(session);
}

static void
j1939_xtp_rx_eoma(struct j1939_priv *priv, struct sk_buff *skb,
		  bool transmitter)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;

	session = j1939_session_get_by_addr(priv, &skcb->addr, true,
					    transmitter);
	if (!session)
		return;

	j1939_xtp_rx_eoma_one(session, skb);
	j1939_session_put(session);
}

static void
j1939_xtp_rx_cts_one(struct j1939_session *session, struct sk_buff *skb)
{
	enum j1939_xtp_abort err = J1939_XTP_ABORT_FAULT;
	unsigned int pkt;
	const u8 *dat;

	dat = skb->data;

	if (j1939_xtp_rx_cmd_bad_pgn(session, skb))
		return;

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	if (session->last_cmd == dat[0]) {
		err = J1939_XTP_ABORT_DUP_SEQ;
		goto out_session_cancel;
	}

	if (session->skcb.addr.type == J1939_ETP)
		pkt = j1939_etp_ctl_to_packet(dat);
	else
		pkt = dat[2];

	if (!pkt)
		goto out_session_cancel;
	else if (dat[1] > session->pkt.block /* 0xff for etp */)
		goto out_session_cancel;

	/* set packet counters only when not CTS(0) */
	session->pkt.tx_acked = pkt - 1;
	j1939_session_skb_drop_old(session);
	session->pkt.last = session->pkt.tx_acked + dat[1];
	if (session->pkt.last > session->pkt.total)
		/* safety measure */
		session->pkt.last = session->pkt.total;
	/* TODO: do not set tx here, do it in txtimer */
	session->pkt.tx = session->pkt.tx_acked;

	session->last_cmd = dat[0];
	if (dat[1]) {
		j1939_tp_set_rxtimeout(session, 1250);
		if (session->transmission) {
			if (session->pkt.tx_acked)
				j1939_sk_errqueue(session,
						  J1939_ERRQUEUE_TX_SCHED);
			j1939_session_txtimer_cancel(session);
			j1939_tp_schedule_txtimer(session, 0);
		}
	} else {
		/* CTS(0) */
		j1939_tp_set_rxtimeout(session, 550);
	}
	return;

 out_session_cancel:
	j1939_session_timers_cancel(session);
	j1939_session_cancel(session, err);
}

static void
j1939_xtp_rx_cts(struct j1939_priv *priv, struct sk_buff *skb, bool transmitter)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;

	session = j1939_session_get_by_addr(priv, &skcb->addr, true,
					    transmitter);
	if (!session)
		return;
	j1939_xtp_rx_cts_one(session, skb);
	j1939_session_put(session);
}

static struct j1939_session *j1939_session_new(struct j1939_priv *priv,
					       struct sk_buff *skb, size_t size)
{
	struct j1939_session *session;
	struct j1939_sk_buff_cb *skcb;

	session = kzalloc(sizeof(*session), gfp_any());
	if (!session)
		return NULL;

	INIT_LIST_HEAD(&session->active_session_list_entry);
	INIT_LIST_HEAD(&session->sk_session_queue_entry);
	kref_init(&session->kref);

	j1939_priv_get(priv);
	session->priv = priv;
	session->total_message_size = size;
	session->state = J1939_SESSION_NEW;

	skb_queue_head_init(&session->skb_queue);
	skb_queue_tail(&session->skb_queue, skb_get(skb));

	skcb = j1939_skb_to_cb(skb);
	memcpy(&session->skcb, skcb, sizeof(session->skcb));

	hrtimer_setup(&session->txtimer, j1939_tp_txtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	hrtimer_setup(&session->rxtimer, j1939_tp_rxtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);

	netdev_dbg(priv->ndev, "%s: 0x%p: sa: %02x, da: %02x\n",
		   __func__, session, skcb->addr.sa, skcb->addr.da);

	return session;
}

static struct
j1939_session *j1939_session_fresh_new(struct j1939_priv *priv,
				       int size,
				       const struct j1939_sk_buff_cb *rel_skcb)
{
	struct sk_buff *skb;
	struct j1939_sk_buff_cb *skcb;
	struct j1939_session *session;

	skb = alloc_skb(size + sizeof(struct can_skb_priv), GFP_ATOMIC);
	if (unlikely(!skb))
		return NULL;

	skb->dev = priv->ndev;
	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = priv->ndev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;
	skcb = j1939_skb_to_cb(skb);
	memcpy(skcb, rel_skcb, sizeof(*skcb));

	session = j1939_session_new(priv, skb, size);
	if (!session) {
		kfree_skb(skb);
		return NULL;
	}

	/* alloc data area */
	skb_put(skb, size);
	/* skb is recounted in j1939_session_new() */
	return session;
}

int j1939_session_activate(struct j1939_session *session)
{
	struct j1939_priv *priv = session->priv;
	struct j1939_session *active = NULL;
	int ret = 0;

	j1939_session_list_lock(priv);
	if (session->skcb.addr.type != J1939_SIMPLE)
		active = j1939_session_get_by_addr_locked(priv,
							  &priv->active_session_list,
							  &session->skcb.addr, false,
							  session->transmission);
	if (active) {
		j1939_session_put(active);
		ret = -EAGAIN;
	} else {
		WARN_ON_ONCE(session->state != J1939_SESSION_NEW);
		list_add_tail(&session->active_session_list_entry,
			      &priv->active_session_list);
		j1939_session_get(session);
		session->state = J1939_SESSION_ACTIVE;

		netdev_dbg(session->priv->ndev, "%s: 0x%p\n",
			   __func__, session);
	}
	j1939_session_list_unlock(priv);

	return ret;
}

static struct
j1939_session *j1939_xtp_rx_rts_session_new(struct j1939_priv *priv,
					    struct sk_buff *skb)
{
	enum j1939_xtp_abort abort = J1939_XTP_NO_ABORT;
	struct j1939_sk_buff_cb skcb = *j1939_skb_to_cb(skb);
	struct j1939_session *session;
	const u8 *dat;
	int len, ret;
	pgn_t pgn;

	netdev_dbg(priv->ndev, "%s\n", __func__);

	dat = skb->data;
	pgn = j1939_xtp_ctl_to_pgn(dat);
	skcb.addr.pgn = pgn;

	if (!j1939_sk_recv_match(priv, &skcb))
		return NULL;

	if (skcb.addr.type == J1939_ETP) {
		len = j1939_etp_ctl_to_size(dat);
		if (len > J1939_MAX_ETP_PACKET_SIZE)
			abort = J1939_XTP_ABORT_FAULT;
		else if (len > priv->tp_max_packet_size)
			abort = J1939_XTP_ABORT_RESOURCE;
		else if (len <= J1939_MAX_TP_PACKET_SIZE)
			abort = J1939_XTP_ABORT_FAULT;
	} else {
		len = j1939_tp_ctl_to_size(dat);
		if (len > J1939_MAX_TP_PACKET_SIZE)
			abort = J1939_XTP_ABORT_FAULT;
		else if (len > priv->tp_max_packet_size)
			abort = J1939_XTP_ABORT_RESOURCE;
		else if (len < J1939_MIN_TP_PACKET_SIZE)
			abort = J1939_XTP_ABORT_FAULT;
	}

	if (abort != J1939_XTP_NO_ABORT) {
		j1939_xtp_tx_abort(priv, &skcb, true, abort, pgn);
		return NULL;
	}

	session = j1939_session_fresh_new(priv, len, &skcb);
	if (!session) {
		j1939_xtp_tx_abort(priv, &skcb, true,
				   J1939_XTP_ABORT_RESOURCE, pgn);
		return NULL;
	}

	/* initialize the control buffer: plain copy */
	session->pkt.total = (len + 6) / 7;
	session->pkt.block = 0xff;
	if (skcb.addr.type != J1939_ETP) {
		if (dat[3] != session->pkt.total)
			netdev_alert(priv->ndev, "%s: 0x%p: strange total, %u != %u\n",
				     __func__, session, session->pkt.total,
				     dat[3]);
		session->pkt.total = dat[3];
		session->pkt.block = min(dat[3], dat[4]);
	}

	session->pkt.rx = 0;
	session->pkt.tx = 0;

	session->tskey = priv->rx_tskey++;
	j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_RTS);

	ret = j1939_session_activate(session);
	if (ret) {
		/* Entering this scope indicates an issue with the J1939 bus.
		 * Possible scenarios include:
		 * - A time lapse occurred, and a new session was initiated
		 *   due to another packet being sent correctly. This could
		 *   have been caused by too long interrupt, debugger, or being
		 *   out-scheduled by another task.
		 * - The bus is receiving numerous erroneous packets, either
		 *   from a malfunctioning device or during a test scenario.
		 */
		netdev_alert(priv->ndev, "%s: 0x%p: concurrent session with same addr (%02x %02x) is already active.\n",
			     __func__, session, skcb.addr.sa, skcb.addr.da);
		j1939_session_put(session);
		return NULL;
	}

	return session;
}

static int j1939_xtp_rx_rts_session_active(struct j1939_session *session,
					   struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_priv *priv = session->priv;

	if (!session->transmission) {
		if (j1939_xtp_rx_cmd_bad_pgn(session, skb))
			return -EBUSY;

		/* RTS on active session */
		j1939_session_timers_cancel(session);
		j1939_session_cancel(session, J1939_XTP_ABORT_BUSY);
	}

	if (session->last_cmd != 0) {
		/* we received a second rts on the same connection */
		netdev_alert(priv->ndev, "%s: 0x%p: connection exists (%02x %02x). last cmd: %x\n",
			     __func__, session, skcb->addr.sa, skcb->addr.da,
			     session->last_cmd);

		j1939_session_timers_cancel(session);
		j1939_session_cancel(session, J1939_XTP_ABORT_BUSY);
		if (session->transmission)
			j1939_session_deactivate_activate_next(session);

		return -EBUSY;
	}

	if (session->skcb.addr.sa != skcb->addr.sa ||
	    session->skcb.addr.da != skcb->addr.da)
		netdev_warn(priv->ndev, "%s: 0x%p: session->skcb.addr.sa=0x%02x skcb->addr.sa=0x%02x session->skcb.addr.da=0x%02x skcb->addr.da=0x%02x\n",
			    __func__, session,
			    session->skcb.addr.sa, skcb->addr.sa,
			    session->skcb.addr.da, skcb->addr.da);
	/* make sure 'sa' & 'da' are correct !
	 * They may be 'not filled in yet' for sending
	 * skb's, since they did not pass the Address Claim ever.
	 */
	session->skcb.addr.sa = skcb->addr.sa;
	session->skcb.addr.da = skcb->addr.da;

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	return 0;
}

static void j1939_xtp_rx_rts(struct j1939_priv *priv, struct sk_buff *skb,
			     bool transmitter)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;
	u8 cmd = skb->data[0];

	session = j1939_session_get_by_addr(priv, &skcb->addr, false,
					    transmitter);

	if (!session) {
		if (transmitter) {
			/* If we're the transmitter and this function is called,
			 * we received our own RTS. A session has already been
			 * created.
			 *
			 * For some reasons however it might have been destroyed
			 * already. So don't create a new one here (using
			 * "j1939_xtp_rx_rts_session_new()") as this will be a
			 * receiver session.
			 *
			 * The reasons the session is already destroyed might
			 * be:
			 * - user space closed socket was and the session was
			 *   aborted
			 * - session was aborted due to external abort message
			 */
			return;
		}
		session = j1939_xtp_rx_rts_session_new(priv, skb);
		if (!session) {
			if (cmd == J1939_TP_CMD_BAM && j1939_sk_recv_match(priv, skcb))
				netdev_info(priv->ndev, "%s: failed to create TP BAM session\n",
					    __func__);
			return;
		}
	} else {
		if (j1939_xtp_rx_rts_session_active(session, skb)) {
			j1939_session_put(session);
			return;
		}
	}
	session->last_cmd = cmd;

	if (cmd == J1939_TP_CMD_BAM) {
		if (!session->transmission)
			j1939_tp_set_rxtimeout(session, 750);
	} else {
		if (!session->transmission) {
			j1939_session_txtimer_cancel(session);
			j1939_tp_schedule_txtimer(session, 0);
		}
		j1939_tp_set_rxtimeout(session, 1250);
	}

	j1939_session_put(session);
}

static void j1939_xtp_rx_dpo_one(struct j1939_session *session,
				 struct sk_buff *skb)
{
	const u8 *dat = skb->data;

	if (j1939_xtp_rx_cmd_bad_pgn(session, skb))
		return;

	netdev_dbg(session->priv->ndev, "%s: 0x%p\n", __func__, session);

	/* transmitted without problems */
	session->pkt.dpo = j1939_etp_ctl_to_packet(skb->data);
	session->last_cmd = dat[0];
	j1939_tp_set_rxtimeout(session, 750);

	if (!session->transmission)
		j1939_sk_errqueue(session, J1939_ERRQUEUE_RX_DPO);
}

static void j1939_xtp_rx_dpo(struct j1939_priv *priv, struct sk_buff *skb,
			     bool transmitter)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;

	session = j1939_session_get_by_addr(priv, &skcb->addr, false,
					    transmitter);
	if (!session) {
		netdev_info(priv->ndev,
			    "%s: no connection found\n", __func__);
		return;
	}

	j1939_xtp_rx_dpo_one(session, skb);
	j1939_session_put(session);
}

static void j1939_xtp_rx_dat_one(struct j1939_session *session,
				 struct sk_buff *skb)
{
	enum j1939_xtp_abort abort = J1939_XTP_ABORT_FAULT;
	struct j1939_priv *priv = session->priv;
	struct j1939_sk_buff_cb *skcb, *se_skcb;
	struct sk_buff *se_skb = NULL;
	const u8 *dat;
	u8 *tpdat;
	int offset;
	int nbytes;
	bool final = false;
	bool remain = false;
	bool do_cts_eoma = false;
	int packet;

	skcb = j1939_skb_to_cb(skb);
	dat = skb->data;
	if (skb->len != 8) {
		/* makes no sense */
		abort = J1939_XTP_ABORT_UNEXPECTED_DATA;
		goto out_session_cancel;
	}

	switch (session->last_cmd) {
	case 0xff:
		break;
	case J1939_ETP_CMD_DPO:
		if (skcb->addr.type == J1939_ETP)
			break;
		fallthrough;
	case J1939_TP_CMD_BAM:
		fallthrough;
	case J1939_TP_CMD_CTS:
		if (skcb->addr.type != J1939_ETP)
			break;
		fallthrough;
	default:
		netdev_info(priv->ndev, "%s: 0x%p: last %02x\n", __func__,
			    session, session->last_cmd);
		goto out_session_cancel;
	}

	packet = (dat[0] - 1 + session->pkt.dpo);
	if (packet > session->pkt.total ||
	    (session->pkt.rx + 1) > session->pkt.total) {
		netdev_info(priv->ndev, "%s: 0x%p: should have been completed\n",
			    __func__, session);
		goto out_session_cancel;
	}

	se_skb = j1939_session_skb_get_by_offset(session, packet * 7);
	if (!se_skb) {
		netdev_warn(priv->ndev, "%s: 0x%p: no skb found\n", __func__,
			    session);
		goto out_session_cancel;
	}

	se_skcb = j1939_skb_to_cb(se_skb);
	offset = packet * 7 - se_skcb->offset;
	nbytes = se_skb->len - offset;
	if (nbytes > 7)
		nbytes = 7;
	if (nbytes <= 0 || (nbytes + 1) > skb->len) {
		netdev_info(priv->ndev, "%s: 0x%p: nbytes %i, len %i\n",
			    __func__, session, nbytes, skb->len);
		goto out_session_cancel;
	}

	tpdat = se_skb->data;
	if (!session->transmission) {
		memcpy(&tpdat[offset], &dat[1], nbytes);
	} else {
		int err;

		err = memcmp(&tpdat[offset], &dat[1], nbytes);
		if (err)
			netdev_err_once(priv->ndev,
					"%s: 0x%p: Data of RX-looped back packet (%*ph) doesn't match TX data (%*ph)!\n",
					__func__, session,
					nbytes, &dat[1],
					nbytes, &tpdat[offset]);
	}

	if (packet == session->pkt.rx)
		session->pkt.rx++;

	if (se_skcb->addr.type != J1939_ETP &&
	    j1939_cb_is_broadcast(&session->skcb)) {
		if (session->pkt.rx >= session->pkt.total)
			final = true;
		else
			remain = true;
	} else {
		/* never final, an EOMA must follow */
		if (session->pkt.rx >= session->pkt.last)
			do_cts_eoma = true;
	}

	if (final) {
		j1939_session_timers_cancel(session);
		j1939_session_completed(session);
	} else if (remain) {
		if (!session->transmission)
			j1939_tp_set_rxtimeout(session, 750);
	} else if (do_cts_eoma) {
		j1939_tp_set_rxtimeout(session, 1250);
		if (!session->transmission)
			j1939_tp_schedule_txtimer(session, 0);
	} else {
		j1939_tp_set_rxtimeout(session, 750);
	}
	session->last_cmd = 0xff;
	consume_skb(se_skb);
	j1939_session_put(session);

	return;

 out_session_cancel:
	kfree_skb(se_skb);
	j1939_session_timers_cancel(session);
	j1939_session_cancel(session, abort);
	j1939_session_put(session);
}

static void j1939_xtp_rx_dat(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb;
	struct j1939_session *session;

	skcb = j1939_skb_to_cb(skb);

	if (j1939_tp_im_transmitter(skcb)) {
		session = j1939_session_get_by_addr(priv, &skcb->addr, false,
						    true);
		if (!session)
			netdev_info(priv->ndev, "%s: no tx connection found\n",
				    __func__);
		else
			j1939_xtp_rx_dat_one(session, skb);
	}

	if (j1939_tp_im_receiver(skcb)) {
		session = j1939_session_get_by_addr(priv, &skcb->addr, false,
						    false);
		if (!session)
			netdev_info(priv->ndev, "%s: no rx connection found\n",
				    __func__);
		else
			j1939_xtp_rx_dat_one(session, skb);
	}

	if (j1939_cb_is_broadcast(skcb)) {
		session = j1939_session_get_by_addr(priv, &skcb->addr, false,
						    false);
		if (session)
			j1939_xtp_rx_dat_one(session, skb);
	}
}

/* j1939 main intf */
struct j1939_session *j1939_tp_send(struct j1939_priv *priv,
				    struct sk_buff *skb, size_t size)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct j1939_session *session;
	int ret;

	if (skcb->addr.pgn == J1939_TP_PGN_DAT ||
	    skcb->addr.pgn == J1939_TP_PGN_CTL ||
	    skcb->addr.pgn == J1939_ETP_PGN_DAT ||
	    skcb->addr.pgn == J1939_ETP_PGN_CTL)
		/* avoid conflict */
		return ERR_PTR(-EDOM);

	if (size > priv->tp_max_packet_size)
		return ERR_PTR(-EMSGSIZE);

	if (size <= 8)
		skcb->addr.type = J1939_SIMPLE;
	else if (size > J1939_MAX_TP_PACKET_SIZE)
		skcb->addr.type = J1939_ETP;
	else
		skcb->addr.type = J1939_TP;

	if (skcb->addr.type == J1939_ETP &&
	    j1939_cb_is_broadcast(skcb))
		return ERR_PTR(-EDESTADDRREQ);

	/* fill in addresses from names */
	ret = j1939_ac_fixup(priv, skb);
	if (unlikely(ret))
		return ERR_PTR(ret);

	/* fix DST flags, it may be used there soon */
	if (j1939_address_is_unicast(skcb->addr.da) &&
	    priv->ents[skcb->addr.da].nusers)
		skcb->flags |= J1939_ECU_LOCAL_DST;

	/* src is always local, I'm sending ... */
	skcb->flags |= J1939_ECU_LOCAL_SRC;

	/* prepare new session */
	session = j1939_session_new(priv, skb, size);
	if (!session)
		return ERR_PTR(-ENOMEM);

	/* skb is recounted in j1939_session_new() */
	sock_hold(skb->sk);
	session->sk = skb->sk;
	session->transmission = true;
	session->pkt.total = (size + 6) / 7;
	session->pkt.block = skcb->addr.type == J1939_ETP ? 255 :
		min(j1939_tp_block ?: 255, session->pkt.total);

	if (j1939_cb_is_broadcast(&session->skcb))
		/* set the end-packet for broadcast */
		session->pkt.last = session->pkt.total;

	skcb->tskey = atomic_inc_return(&session->sk->sk_tskey) - 1;
	session->tskey = skcb->tskey;

	return session;
}

static void j1939_tp_cmd_recv(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	int extd = J1939_TP;
	u8 cmd = skb->data[0];

	switch (cmd) {
	case J1939_ETP_CMD_RTS:
		extd = J1939_ETP;
		fallthrough;
	case J1939_TP_CMD_BAM:
		if (cmd == J1939_TP_CMD_BAM && !j1939_cb_is_broadcast(skcb)) {
			netdev_err_once(priv->ndev, "%s: BAM to unicast (%02x), ignoring!\n",
					__func__, skcb->addr.sa);
			return;
		}
		fallthrough;
	case J1939_TP_CMD_RTS:
		if (skcb->addr.type != extd)
			return;

		if (cmd == J1939_TP_CMD_RTS && j1939_cb_is_broadcast(skcb)) {
			netdev_alert(priv->ndev, "%s: rts without destination (%02x)\n",
				     __func__, skcb->addr.sa);
			return;
		}

		if (j1939_tp_im_transmitter(skcb))
			j1939_xtp_rx_rts(priv, skb, true);

		if (j1939_tp_im_receiver(skcb) || j1939_cb_is_broadcast(skcb))
			j1939_xtp_rx_rts(priv, skb, false);

		break;

	case J1939_ETP_CMD_CTS:
		extd = J1939_ETP;
		fallthrough;
	case J1939_TP_CMD_CTS:
		if (skcb->addr.type != extd)
			return;

		if (j1939_tp_im_transmitter(skcb))
			j1939_xtp_rx_cts(priv, skb, false);

		if (j1939_tp_im_receiver(skcb))
			j1939_xtp_rx_cts(priv, skb, true);

		break;

	case J1939_ETP_CMD_DPO:
		if (skcb->addr.type != J1939_ETP)
			return;

		if (j1939_tp_im_transmitter(skcb))
			j1939_xtp_rx_dpo(priv, skb, true);

		if (j1939_tp_im_receiver(skcb))
			j1939_xtp_rx_dpo(priv, skb, false);

		break;

	case J1939_ETP_CMD_EOMA:
		extd = J1939_ETP;
		fallthrough;
	case J1939_TP_CMD_EOMA:
		if (skcb->addr.type != extd)
			return;

		if (j1939_tp_im_transmitter(skcb))
			j1939_xtp_rx_eoma(priv, skb, false);

		if (j1939_tp_im_receiver(skcb))
			j1939_xtp_rx_eoma(priv, skb, true);

		break;

	case J1939_ETP_CMD_ABORT: /* && J1939_TP_CMD_ABORT */
		if (j1939_cb_is_broadcast(skcb)) {
			netdev_err_once(priv->ndev, "%s: abort to broadcast (%02x), ignoring!\n",
					__func__, skcb->addr.sa);
			return;
		}

		if (j1939_tp_im_transmitter(skcb))
			j1939_xtp_rx_abort(priv, skb, true);

		if (j1939_tp_im_receiver(skcb))
			j1939_xtp_rx_abort(priv, skb, false);

		break;
	default:
		return;
	}
}

int j1939_tp_recv(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);

	if (!j1939_tp_im_involved_anydir(skcb) && !j1939_cb_is_broadcast(skcb))
		return 0;

	switch (skcb->addr.pgn) {
	case J1939_ETP_PGN_DAT:
		skcb->addr.type = J1939_ETP;
		fallthrough;
	case J1939_TP_PGN_DAT:
		j1939_xtp_rx_dat(priv, skb);
		break;

	case J1939_ETP_PGN_CTL:
		skcb->addr.type = J1939_ETP;
		fallthrough;
	case J1939_TP_PGN_CTL:
		if (skb->len < 8)
			return 0; /* Don't care. Nothing to extract here */

		j1939_tp_cmd_recv(priv, skb);
		break;
	default:
		return 0; /* no problem */
	}
	return 1; /* "I processed the message" */
}

void j1939_simple_recv(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_session *session;

	if (!skb->sk)
		return;

	if (skb->sk->sk_family != AF_CAN ||
	    skb->sk->sk_protocol != CAN_J1939)
		return;

	j1939_session_list_lock(priv);
	session = j1939_session_get_simple(priv, skb);
	j1939_session_list_unlock(priv);
	if (!session) {
		netdev_warn(priv->ndev,
			    "%s: Received already invalidated message\n",
			    __func__);
		return;
	}

	j1939_session_timers_cancel(session);
	j1939_session_deactivate(session);
	j1939_session_put(session);
}

int j1939_cancel_active_session(struct j1939_priv *priv, struct sock *sk)
{
	struct j1939_session *session, *saved;

	netdev_dbg(priv->ndev, "%s, sk: %p\n", __func__, sk);
	j1939_session_list_lock(priv);
	list_for_each_entry_safe(session, saved,
				 &priv->active_session_list,
				 active_session_list_entry) {
		if (!sk || sk == session->sk) {
			if (hrtimer_try_to_cancel(&session->txtimer) == 1)
				j1939_session_put(session);
			if (hrtimer_try_to_cancel(&session->rxtimer) == 1)
				j1939_session_put(session);

			session->err = ESHUTDOWN;
			j1939_session_deactivate_locked(session);
		}
	}
	j1939_session_list_unlock(priv);
	return NOTIFY_DONE;
}

void j1939_tp_init(struct j1939_priv *priv)
{
	spin_lock_init(&priv->active_session_list_lock);
	INIT_LIST_HEAD(&priv->active_session_list);
	priv->tp_max_packet_size = J1939_MAX_ETP_PACKET_SIZE;
}
