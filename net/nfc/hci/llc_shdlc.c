// SPDX-License-Identifier: GPL-2.0-only
/*
 * shdlc Link Layer Control
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#define pr_fmt(fmt) "shdlc: %s: " fmt, __func__

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

#include "llc.h"

enum shdlc_state {
	SHDLC_DISCONNECTED = 0,
	SHDLC_CONNECTING = 1,
	SHDLC_NEGOTIATING = 2,
	SHDLC_HALF_CONNECTED = 3,
	SHDLC_CONNECTED = 4
};

struct llc_shdlc {
	struct nfc_hci_dev *hdev;
	xmit_to_drv_t xmit_to_drv;
	rcv_to_hci_t rcv_to_hci;

	struct mutex state_mutex;
	enum shdlc_state state;
	int hard_fault;

	wait_queue_head_t *connect_wq;
	int connect_tries;
	int connect_result;
	struct timer_list connect_timer;/* aka T3 in spec 10.6.1 */

	u8 w;				/* window size */
	bool srej_support;

	struct timer_list t1_timer;	/* send ack timeout */
	bool t1_active;

	struct timer_list t2_timer;	/* guard/retransmit timeout */
	bool t2_active;

	int ns;				/* next seq num for send */
	int nr;				/* next expected seq num for receive */
	int dnr;			/* oldest sent unacked seq num */

	struct sk_buff_head rcv_q;

	struct sk_buff_head send_q;
	bool rnr;			/* other side is not ready to receive */

	struct sk_buff_head ack_pending_q;

	struct work_struct sm_work;

	int tx_headroom;
	int tx_tailroom;

	llc_failure_t llc_failure;
};

#define SHDLC_LLC_HEAD_ROOM	2

#define SHDLC_MAX_WINDOW	4
#define SHDLC_SREJ_SUPPORT	false

#define SHDLC_CONTROL_HEAD_MASK	0xe0
#define SHDLC_CONTROL_HEAD_I	0x80
#define SHDLC_CONTROL_HEAD_I2	0xa0
#define SHDLC_CONTROL_HEAD_S	0xc0
#define SHDLC_CONTROL_HEAD_U	0xe0

#define SHDLC_CONTROL_NS_MASK	0x38
#define SHDLC_CONTROL_NR_MASK	0x07
#define SHDLC_CONTROL_TYPE_MASK	0x18

#define SHDLC_CONTROL_M_MASK	0x1f

enum sframe_type {
	S_FRAME_RR = 0x00,
	S_FRAME_REJ = 0x01,
	S_FRAME_RNR = 0x02,
	S_FRAME_SREJ = 0x03
};

enum uframe_modifier {
	U_FRAME_UA = 0x06,
	U_FRAME_RSET = 0x19
};

#define SHDLC_CONNECT_VALUE_MS	5
#define SHDLC_T1_VALUE_MS(w)	((5 * w) / 4)
#define SHDLC_T2_VALUE_MS	300

#define SHDLC_DUMP_SKB(info, skb)				  \
do {								  \
	pr_debug("%s:\n", info);				  \
	print_hex_dump(KERN_DEBUG, "shdlc: ", DUMP_PREFIX_OFFSET, \
		       16, 1, skb->data, skb->len, 0);		  \
} while (0)

/* checks x < y <= z modulo 8 */
static bool llc_shdlc_x_lt_y_lteq_z(int x, int y, int z)
{
	if (x < z)
		return ((x < y) && (y <= z)) ? true : false;
	else
		return ((y > x) || (y <= z)) ? true : false;
}

/* checks x <= y < z modulo 8 */
static bool llc_shdlc_x_lteq_y_lt_z(int x, int y, int z)
{
	if (x <= z)
		return ((x <= y) && (y < z)) ? true : false;
	else			/* x > z -> z+8 > x */
		return ((y >= x) || (y < z)) ? true : false;
}

static struct sk_buff *llc_shdlc_alloc_skb(const struct llc_shdlc *shdlc,
					   int payload_len)
{
	struct sk_buff *skb;

	skb = alloc_skb(shdlc->tx_headroom + SHDLC_LLC_HEAD_ROOM +
			shdlc->tx_tailroom + payload_len, GFP_KERNEL);
	if (skb)
		skb_reserve(skb, shdlc->tx_headroom + SHDLC_LLC_HEAD_ROOM);

	return skb;
}

/* immediately sends an S frame. */
static int llc_shdlc_send_s_frame(const struct llc_shdlc *shdlc,
				  enum sframe_type sframe_type, int nr)
{
	int r;
	struct sk_buff *skb;

	pr_debug("sframe_type=%d nr=%d\n", sframe_type, nr);

	skb = llc_shdlc_alloc_skb(shdlc, 0);
	if (skb == NULL)
		return -ENOMEM;

	*(u8 *)skb_push(skb, 1) = SHDLC_CONTROL_HEAD_S | (sframe_type << 3) | nr;

	r = shdlc->xmit_to_drv(shdlc->hdev, skb);

	kfree_skb(skb);

	return r;
}

/* immediately sends an U frame. skb may contain optional payload */
static int llc_shdlc_send_u_frame(const struct llc_shdlc *shdlc,
				  struct sk_buff *skb,
				  enum uframe_modifier uframe_modifier)
{
	int r;

	pr_debug("uframe_modifier=%d\n", uframe_modifier);

	*(u8 *)skb_push(skb, 1) = SHDLC_CONTROL_HEAD_U | uframe_modifier;

	r = shdlc->xmit_to_drv(shdlc->hdev, skb);

	kfree_skb(skb);

	return r;
}

/*
 * Free ack_pending frames until y_nr - 1, and reset t2 according to
 * the remaining oldest ack_pending frame sent time
 */
static void llc_shdlc_reset_t2(struct llc_shdlc *shdlc, int y_nr)
{
	struct sk_buff *skb;
	int dnr = shdlc->dnr;	/* MUST initially be < y_nr */

	pr_debug("release ack pending up to frame %d excluded\n", y_nr);

	while (dnr != y_nr) {
		pr_debug("release ack pending frame %d\n", dnr);

		skb = skb_dequeue(&shdlc->ack_pending_q);
		kfree_skb(skb);

		dnr = (dnr + 1) % 8;
	}

	if (skb_queue_empty(&shdlc->ack_pending_q)) {
		if (shdlc->t2_active) {
			del_timer_sync(&shdlc->t2_timer);
			shdlc->t2_active = false;

			pr_debug
			    ("All sent frames acked. Stopped T2(retransmit)\n");
		}
	} else {
		skb = skb_peek(&shdlc->ack_pending_q);

		mod_timer(&shdlc->t2_timer, *(unsigned long *)skb->cb +
			  msecs_to_jiffies(SHDLC_T2_VALUE_MS));
		shdlc->t2_active = true;

		pr_debug
		    ("Start T2(retransmit) for remaining unacked sent frames\n");
	}
}

/*
 * Receive validated frames from lower layer. skb contains HCI payload only.
 * Handle according to algorithm at spec:10.8.2
 */
static void llc_shdlc_rcv_i_frame(struct llc_shdlc *shdlc,
				  struct sk_buff *skb, int ns, int nr)
{
	int x_ns = ns;
	int y_nr = nr;

	pr_debug("recvd I-frame %d, remote waiting frame %d\n", ns, nr);

	if (shdlc->state != SHDLC_CONNECTED)
		goto exit;

	if (x_ns != shdlc->nr) {
		llc_shdlc_send_s_frame(shdlc, S_FRAME_REJ, shdlc->nr);
		goto exit;
	}

	if (!shdlc->t1_active) {
		shdlc->t1_active = true;
		mod_timer(&shdlc->t1_timer, jiffies +
			  msecs_to_jiffies(SHDLC_T1_VALUE_MS(shdlc->w)));
		pr_debug("(re)Start T1(send ack)\n");
	}

	if (skb->len) {
		shdlc->rcv_to_hci(shdlc->hdev, skb);
		skb = NULL;
	}

	shdlc->nr = (shdlc->nr + 1) % 8;

	if (llc_shdlc_x_lt_y_lteq_z(shdlc->dnr, y_nr, shdlc->ns)) {
		llc_shdlc_reset_t2(shdlc, y_nr);

		shdlc->dnr = y_nr;
	}

exit:
	kfree_skb(skb);
}

static void llc_shdlc_rcv_ack(struct llc_shdlc *shdlc, int y_nr)
{
	pr_debug("remote acked up to frame %d excluded\n", y_nr);

	if (llc_shdlc_x_lt_y_lteq_z(shdlc->dnr, y_nr, shdlc->ns)) {
		llc_shdlc_reset_t2(shdlc, y_nr);
		shdlc->dnr = y_nr;
	}
}

static void llc_shdlc_requeue_ack_pending(struct llc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("ns reset to %d\n", shdlc->dnr);

	while ((skb = skb_dequeue_tail(&shdlc->ack_pending_q))) {
		skb_pull(skb, 1);	/* remove control field */
		skb_queue_head(&shdlc->send_q, skb);
	}
	shdlc->ns = shdlc->dnr;
}

static void llc_shdlc_rcv_rej(struct llc_shdlc *shdlc, int y_nr)
{
	struct sk_buff *skb;

	pr_debug("remote asks retransmission from frame %d\n", y_nr);

	if (llc_shdlc_x_lteq_y_lt_z(shdlc->dnr, y_nr, shdlc->ns)) {
		if (shdlc->t2_active) {
			del_timer_sync(&shdlc->t2_timer);
			shdlc->t2_active = false;
			pr_debug("Stopped T2(retransmit)\n");
		}

		if (shdlc->dnr != y_nr) {
			while ((shdlc->dnr = ((shdlc->dnr + 1) % 8)) != y_nr) {
				skb = skb_dequeue(&shdlc->ack_pending_q);
				kfree_skb(skb);
			}
		}

		llc_shdlc_requeue_ack_pending(shdlc);
	}
}

/* See spec RR:10.8.3 REJ:10.8.4 */
static void llc_shdlc_rcv_s_frame(struct llc_shdlc *shdlc,
				  enum sframe_type s_frame_type, int nr)
{
	struct sk_buff *skb;

	if (shdlc->state != SHDLC_CONNECTED)
		return;

	switch (s_frame_type) {
	case S_FRAME_RR:
		llc_shdlc_rcv_ack(shdlc, nr);
		if (shdlc->rnr == true) {	/* see SHDLC 10.7.7 */
			shdlc->rnr = false;
			if (shdlc->send_q.qlen == 0) {
				skb = llc_shdlc_alloc_skb(shdlc, 0);
				if (skb)
					skb_queue_tail(&shdlc->send_q, skb);
			}
		}
		break;
	case S_FRAME_REJ:
		llc_shdlc_rcv_rej(shdlc, nr);
		break;
	case S_FRAME_RNR:
		llc_shdlc_rcv_ack(shdlc, nr);
		shdlc->rnr = true;
		break;
	default:
		break;
	}
}

static void llc_shdlc_connect_complete(struct llc_shdlc *shdlc, int r)
{
	pr_debug("result=%d\n", r);

	del_timer_sync(&shdlc->connect_timer);

	if (r == 0) {
		shdlc->ns = 0;
		shdlc->nr = 0;
		shdlc->dnr = 0;

		shdlc->state = SHDLC_HALF_CONNECTED;
	} else {
		shdlc->state = SHDLC_DISCONNECTED;
	}

	shdlc->connect_result = r;

	wake_up(shdlc->connect_wq);
}

static int llc_shdlc_connect_initiate(const struct llc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("\n");

	skb = llc_shdlc_alloc_skb(shdlc, 2);
	if (skb == NULL)
		return -ENOMEM;

	skb_put_u8(skb, SHDLC_MAX_WINDOW);
	skb_put_u8(skb, SHDLC_SREJ_SUPPORT ? 1 : 0);

	return llc_shdlc_send_u_frame(shdlc, skb, U_FRAME_RSET);
}

static int llc_shdlc_connect_send_ua(const struct llc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("\n");

	skb = llc_shdlc_alloc_skb(shdlc, 0);
	if (skb == NULL)
		return -ENOMEM;

	return llc_shdlc_send_u_frame(shdlc, skb, U_FRAME_UA);
}

static void llc_shdlc_rcv_u_frame(struct llc_shdlc *shdlc,
				  struct sk_buff *skb,
				  enum uframe_modifier u_frame_modifier)
{
	u8 w = SHDLC_MAX_WINDOW;
	bool srej_support = SHDLC_SREJ_SUPPORT;
	int r;

	pr_debug("u_frame_modifier=%d\n", u_frame_modifier);

	switch (u_frame_modifier) {
	case U_FRAME_RSET:
		switch (shdlc->state) {
		case SHDLC_NEGOTIATING:
		case SHDLC_CONNECTING:
			/*
			 * We sent RSET, but chip wants to negotiate or we
			 * got RSET before we managed to send out our.
			 */
			if (skb->len > 0)
				w = skb->data[0];

			if (skb->len > 1)
				srej_support = skb->data[1] & 0x01 ? true :
					       false;

			if ((w <= SHDLC_MAX_WINDOW) &&
			    (SHDLC_SREJ_SUPPORT || (srej_support == false))) {
				shdlc->w = w;
				shdlc->srej_support = srej_support;
				r = llc_shdlc_connect_send_ua(shdlc);
				llc_shdlc_connect_complete(shdlc, r);
			}
			break;
		case SHDLC_HALF_CONNECTED:
			/*
			 * Chip resent RSET due to its timeout - Ignote it
			 * as we already sent UA.
			 */
			break;
		case SHDLC_CONNECTED:
			/*
			 * Chip wants to reset link. This is unexpected and
			 * unsupported.
			 */
			shdlc->hard_fault = -ECONNRESET;
			break;
		default:
			break;
		}
		break;
	case U_FRAME_UA:
		if ((shdlc->state == SHDLC_CONNECTING &&
		     shdlc->connect_tries > 0) ||
		    (shdlc->state == SHDLC_NEGOTIATING)) {
			llc_shdlc_connect_complete(shdlc, 0);
			shdlc->state = SHDLC_CONNECTED;
		}
		break;
	default:
		break;
	}

	kfree_skb(skb);
}

static void llc_shdlc_handle_rcv_queue(struct llc_shdlc *shdlc)
{
	struct sk_buff *skb;
	u8 control;
	int nr;
	int ns;
	enum sframe_type s_frame_type;
	enum uframe_modifier u_frame_modifier;

	if (shdlc->rcv_q.qlen)
		pr_debug("rcvQlen=%d\n", shdlc->rcv_q.qlen);

	while ((skb = skb_dequeue(&shdlc->rcv_q)) != NULL) {
		control = skb->data[0];
		skb_pull(skb, 1);
		switch (control & SHDLC_CONTROL_HEAD_MASK) {
		case SHDLC_CONTROL_HEAD_I:
		case SHDLC_CONTROL_HEAD_I2:
			if (shdlc->state == SHDLC_HALF_CONNECTED)
				shdlc->state = SHDLC_CONNECTED;

			ns = (control & SHDLC_CONTROL_NS_MASK) >> 3;
			nr = control & SHDLC_CONTROL_NR_MASK;
			llc_shdlc_rcv_i_frame(shdlc, skb, ns, nr);
			break;
		case SHDLC_CONTROL_HEAD_S:
			if (shdlc->state == SHDLC_HALF_CONNECTED)
				shdlc->state = SHDLC_CONNECTED;

			s_frame_type = (control & SHDLC_CONTROL_TYPE_MASK) >> 3;
			nr = control & SHDLC_CONTROL_NR_MASK;
			llc_shdlc_rcv_s_frame(shdlc, s_frame_type, nr);
			kfree_skb(skb);
			break;
		case SHDLC_CONTROL_HEAD_U:
			u_frame_modifier = control & SHDLC_CONTROL_M_MASK;
			llc_shdlc_rcv_u_frame(shdlc, skb, u_frame_modifier);
			break;
		default:
			pr_err("UNKNOWN Control=%d\n", control);
			kfree_skb(skb);
			break;
		}
	}
}

static int llc_shdlc_w_used(int ns, int dnr)
{
	int unack_count;

	if (dnr <= ns)
		unack_count = ns - dnr;
	else
		unack_count = 8 - dnr + ns;

	return unack_count;
}

/* Send frames according to algorithm at spec:10.8.1 */
static void llc_shdlc_handle_send_queue(struct llc_shdlc *shdlc)
{
	struct sk_buff *skb;
	int r;
	unsigned long time_sent;

	if (shdlc->send_q.qlen)
		pr_debug
		    ("sendQlen=%d ns=%d dnr=%d rnr=%s w_room=%d unackQlen=%d\n",
		     shdlc->send_q.qlen, shdlc->ns, shdlc->dnr,
		     shdlc->rnr == false ? "false" : "true",
		     shdlc->w - llc_shdlc_w_used(shdlc->ns, shdlc->dnr),
		     shdlc->ack_pending_q.qlen);

	while (shdlc->send_q.qlen && shdlc->ack_pending_q.qlen < shdlc->w &&
	       (shdlc->rnr == false)) {

		if (shdlc->t1_active) {
			del_timer_sync(&shdlc->t1_timer);
			shdlc->t1_active = false;
			pr_debug("Stopped T1(send ack)\n");
		}

		skb = skb_dequeue(&shdlc->send_q);

		*(u8 *)skb_push(skb, 1) = SHDLC_CONTROL_HEAD_I | (shdlc->ns << 3) |
					shdlc->nr;

		pr_debug("Sending I-Frame %d, waiting to rcv %d\n", shdlc->ns,
			 shdlc->nr);
		SHDLC_DUMP_SKB("shdlc frame written", skb);

		r = shdlc->xmit_to_drv(shdlc->hdev, skb);
		if (r < 0) {
			shdlc->hard_fault = r;
			break;
		}

		shdlc->ns = (shdlc->ns + 1) % 8;

		time_sent = jiffies;
		*(unsigned long *)skb->cb = time_sent;

		skb_queue_tail(&shdlc->ack_pending_q, skb);

		if (shdlc->t2_active == false) {
			shdlc->t2_active = true;
			mod_timer(&shdlc->t2_timer, time_sent +
				  msecs_to_jiffies(SHDLC_T2_VALUE_MS));
			pr_debug("Started T2 (retransmit)\n");
		}
	}
}

static void llc_shdlc_connect_timeout(struct timer_list *t)
{
	struct llc_shdlc *shdlc = from_timer(shdlc, t, connect_timer);

	pr_debug("\n");

	schedule_work(&shdlc->sm_work);
}

static void llc_shdlc_t1_timeout(struct timer_list *t)
{
	struct llc_shdlc *shdlc = from_timer(shdlc, t, t1_timer);

	pr_debug("SoftIRQ: need to send ack\n");

	schedule_work(&shdlc->sm_work);
}

static void llc_shdlc_t2_timeout(struct timer_list *t)
{
	struct llc_shdlc *shdlc = from_timer(shdlc, t, t2_timer);

	pr_debug("SoftIRQ: need to retransmit\n");

	schedule_work(&shdlc->sm_work);
}

static void llc_shdlc_sm_work(struct work_struct *work)
{
	struct llc_shdlc *shdlc = container_of(work, struct llc_shdlc, sm_work);
	int r;

	pr_debug("\n");

	mutex_lock(&shdlc->state_mutex);

	switch (shdlc->state) {
	case SHDLC_DISCONNECTED:
		skb_queue_purge(&shdlc->rcv_q);
		skb_queue_purge(&shdlc->send_q);
		skb_queue_purge(&shdlc->ack_pending_q);
		break;
	case SHDLC_CONNECTING:
		if (shdlc->hard_fault) {
			llc_shdlc_connect_complete(shdlc, shdlc->hard_fault);
			break;
		}

		if (shdlc->connect_tries++ < 5)
			r = llc_shdlc_connect_initiate(shdlc);
		else
			r = -ETIME;
		if (r < 0) {
			llc_shdlc_connect_complete(shdlc, r);
		} else {
			mod_timer(&shdlc->connect_timer, jiffies +
				  msecs_to_jiffies(SHDLC_CONNECT_VALUE_MS));

			shdlc->state = SHDLC_NEGOTIATING;
		}
		break;
	case SHDLC_NEGOTIATING:
		if (timer_pending(&shdlc->connect_timer) == 0) {
			shdlc->state = SHDLC_CONNECTING;
			schedule_work(&shdlc->sm_work);
		}

		llc_shdlc_handle_rcv_queue(shdlc);

		if (shdlc->hard_fault) {
			llc_shdlc_connect_complete(shdlc, shdlc->hard_fault);
			break;
		}
		break;
	case SHDLC_HALF_CONNECTED:
	case SHDLC_CONNECTED:
		llc_shdlc_handle_rcv_queue(shdlc);
		llc_shdlc_handle_send_queue(shdlc);

		if (shdlc->t1_active && timer_pending(&shdlc->t1_timer) == 0) {
			pr_debug
			    ("Handle T1(send ack) elapsed (T1 now inactive)\n");

			shdlc->t1_active = false;
			r = llc_shdlc_send_s_frame(shdlc, S_FRAME_RR,
						   shdlc->nr);
			if (r < 0)
				shdlc->hard_fault = r;
		}

		if (shdlc->t2_active && timer_pending(&shdlc->t2_timer) == 0) {
			pr_debug
			    ("Handle T2(retransmit) elapsed (T2 inactive)\n");

			shdlc->t2_active = false;

			llc_shdlc_requeue_ack_pending(shdlc);
			llc_shdlc_handle_send_queue(shdlc);
		}

		if (shdlc->hard_fault)
			shdlc->llc_failure(shdlc->hdev, shdlc->hard_fault);
		break;
	default:
		break;
	}
	mutex_unlock(&shdlc->state_mutex);
}

/*
 * Called from syscall context to establish shdlc link. Sleeps until
 * link is ready or failure.
 */
static int llc_shdlc_connect(struct llc_shdlc *shdlc)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(connect_wq);

	pr_debug("\n");

	mutex_lock(&shdlc->state_mutex);

	shdlc->state = SHDLC_CONNECTING;
	shdlc->connect_wq = &connect_wq;
	shdlc->connect_tries = 0;
	shdlc->connect_result = 1;

	mutex_unlock(&shdlc->state_mutex);

	schedule_work(&shdlc->sm_work);

	wait_event(connect_wq, shdlc->connect_result != 1);

	return shdlc->connect_result;
}

static void llc_shdlc_disconnect(struct llc_shdlc *shdlc)
{
	pr_debug("\n");

	mutex_lock(&shdlc->state_mutex);

	shdlc->state = SHDLC_DISCONNECTED;

	mutex_unlock(&shdlc->state_mutex);

	schedule_work(&shdlc->sm_work);
}

/*
 * Receive an incoming shdlc frame. Frame has already been crc-validated.
 * skb contains only LLC header and payload.
 * If skb == NULL, it is a notification that the link below is dead.
 */
static void llc_shdlc_recv_frame(struct llc_shdlc *shdlc, struct sk_buff *skb)
{
	if (skb == NULL) {
		pr_err("NULL Frame -> link is dead\n");
		shdlc->hard_fault = -EREMOTEIO;
	} else {
		SHDLC_DUMP_SKB("incoming frame", skb);
		skb_queue_tail(&shdlc->rcv_q, skb);
	}

	schedule_work(&shdlc->sm_work);
}

static void *llc_shdlc_init(struct nfc_hci_dev *hdev, xmit_to_drv_t xmit_to_drv,
			    rcv_to_hci_t rcv_to_hci, int tx_headroom,
			    int tx_tailroom, int *rx_headroom, int *rx_tailroom,
			    llc_failure_t llc_failure)
{
	struct llc_shdlc *shdlc;

	*rx_headroom = SHDLC_LLC_HEAD_ROOM;
	*rx_tailroom = 0;

	shdlc = kzalloc(sizeof(struct llc_shdlc), GFP_KERNEL);
	if (shdlc == NULL)
		return NULL;

	mutex_init(&shdlc->state_mutex);
	shdlc->state = SHDLC_DISCONNECTED;

	timer_setup(&shdlc->connect_timer, llc_shdlc_connect_timeout, 0);
	timer_setup(&shdlc->t1_timer, llc_shdlc_t1_timeout, 0);
	timer_setup(&shdlc->t2_timer, llc_shdlc_t2_timeout, 0);

	shdlc->w = SHDLC_MAX_WINDOW;
	shdlc->srej_support = SHDLC_SREJ_SUPPORT;

	skb_queue_head_init(&shdlc->rcv_q);
	skb_queue_head_init(&shdlc->send_q);
	skb_queue_head_init(&shdlc->ack_pending_q);

	INIT_WORK(&shdlc->sm_work, llc_shdlc_sm_work);

	shdlc->hdev = hdev;
	shdlc->xmit_to_drv = xmit_to_drv;
	shdlc->rcv_to_hci = rcv_to_hci;
	shdlc->tx_headroom = tx_headroom;
	shdlc->tx_tailroom = tx_tailroom;
	shdlc->llc_failure = llc_failure;

	return shdlc;
}

static void llc_shdlc_deinit(struct nfc_llc *llc)
{
	struct llc_shdlc *shdlc = nfc_llc_get_data(llc);

	skb_queue_purge(&shdlc->rcv_q);
	skb_queue_purge(&shdlc->send_q);
	skb_queue_purge(&shdlc->ack_pending_q);

	kfree(shdlc);
}

static int llc_shdlc_start(struct nfc_llc *llc)
{
	struct llc_shdlc *shdlc = nfc_llc_get_data(llc);

	return llc_shdlc_connect(shdlc);
}

static int llc_shdlc_stop(struct nfc_llc *llc)
{
	struct llc_shdlc *shdlc = nfc_llc_get_data(llc);

	llc_shdlc_disconnect(shdlc);

	return 0;
}

static void llc_shdlc_rcv_from_drv(struct nfc_llc *llc, struct sk_buff *skb)
{
	struct llc_shdlc *shdlc = nfc_llc_get_data(llc);

	llc_shdlc_recv_frame(shdlc, skb);
}

static int llc_shdlc_xmit_from_hci(struct nfc_llc *llc, struct sk_buff *skb)
{
	struct llc_shdlc *shdlc = nfc_llc_get_data(llc);

	skb_queue_tail(&shdlc->send_q, skb);

	schedule_work(&shdlc->sm_work);

	return 0;
}

static const struct nfc_llc_ops llc_shdlc_ops = {
	.init = llc_shdlc_init,
	.deinit = llc_shdlc_deinit,
	.start = llc_shdlc_start,
	.stop = llc_shdlc_stop,
	.rcv_from_drv = llc_shdlc_rcv_from_drv,
	.xmit_from_hci = llc_shdlc_xmit_from_hci,
};

int nfc_llc_shdlc_register(void)
{
	return nfc_llc_register(LLC_SHDLC_NAME, &llc_shdlc_ops);
}
