/*
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) "shdlc: %s: " fmt, __func__

#include <linux/sched.h>
#include <linux/export.h>
#include <linux/wait.h>
#include <linux/crc-ccitt.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

#include <net/nfc/hci.h>
#include <net/nfc/shdlc.h>

#define SHDLC_LLC_HEAD_ROOM	2
#define SHDLC_LLC_TAIL_ROOM	2

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
static bool nfc_shdlc_x_lt_y_lteq_z(int x, int y, int z)
{
	if (x < z)
		return ((x < y) && (y <= z)) ? true : false;
	else
		return ((y > x) || (y <= z)) ? true : false;
}

/* checks x <= y < z modulo 8 */
static bool nfc_shdlc_x_lteq_y_lt_z(int x, int y, int z)
{
	if (x <= z)
		return ((x <= y) && (y < z)) ? true : false;
	else			/* x > z -> z+8 > x */
		return ((y >= x) || (y < z)) ? true : false;
}

static struct sk_buff *nfc_shdlc_alloc_skb(struct nfc_shdlc *shdlc,
					   int payload_len)
{
	struct sk_buff *skb;

	skb = alloc_skb(shdlc->client_headroom + SHDLC_LLC_HEAD_ROOM +
			shdlc->client_tailroom + SHDLC_LLC_TAIL_ROOM +
			payload_len, GFP_KERNEL);
	if (skb)
		skb_reserve(skb, shdlc->client_headroom + SHDLC_LLC_HEAD_ROOM);

	return skb;
}

static void nfc_shdlc_add_len_crc(struct sk_buff *skb)
{
	u16 crc;
	int len;

	len = skb->len + 2;
	*skb_push(skb, 1) = len;

	crc = crc_ccitt(0xffff, skb->data, skb->len);
	crc = ~crc;
	*skb_put(skb, 1) = crc & 0xff;
	*skb_put(skb, 1) = crc >> 8;
}

/* immediately sends an S frame. */
static int nfc_shdlc_send_s_frame(struct nfc_shdlc *shdlc,
				  enum sframe_type sframe_type, int nr)
{
	int r;
	struct sk_buff *skb;

	pr_debug("sframe_type=%d nr=%d\n", sframe_type, nr);

	skb = nfc_shdlc_alloc_skb(shdlc, 0);
	if (skb == NULL)
		return -ENOMEM;

	*skb_push(skb, 1) = SHDLC_CONTROL_HEAD_S | (sframe_type << 3) | nr;

	nfc_shdlc_add_len_crc(skb);

	r = shdlc->ops->xmit(shdlc, skb);

	kfree_skb(skb);

	return r;
}

/* immediately sends an U frame. skb may contain optional payload */
static int nfc_shdlc_send_u_frame(struct nfc_shdlc *shdlc,
				  struct sk_buff *skb,
				  enum uframe_modifier uframe_modifier)
{
	int r;

	pr_debug("uframe_modifier=%d\n", uframe_modifier);

	*skb_push(skb, 1) = SHDLC_CONTROL_HEAD_U | uframe_modifier;

	nfc_shdlc_add_len_crc(skb);

	r = shdlc->ops->xmit(shdlc, skb);

	kfree_skb(skb);

	return r;
}

/*
 * Free ack_pending frames until y_nr - 1, and reset t2 according to
 * the remaining oldest ack_pending frame sent time
 */
static void nfc_shdlc_reset_t2(struct nfc_shdlc *shdlc, int y_nr)
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
static void nfc_shdlc_rcv_i_frame(struct nfc_shdlc *shdlc,
				  struct sk_buff *skb, int ns, int nr)
{
	int x_ns = ns;
	int y_nr = nr;

	pr_debug("recvd I-frame %d, remote waiting frame %d\n", ns, nr);

	if (shdlc->state != SHDLC_CONNECTED)
		goto exit;

	if (x_ns != shdlc->nr) {
		nfc_shdlc_send_s_frame(shdlc, S_FRAME_REJ, shdlc->nr);
		goto exit;
	}

	if (shdlc->t1_active == false) {
		shdlc->t1_active = true;
		mod_timer(&shdlc->t1_timer,
			  msecs_to_jiffies(SHDLC_T1_VALUE_MS(shdlc->w)));
		pr_debug("(re)Start T1(send ack)\n");
	}

	if (skb->len) {
		nfc_hci_recv_frame(shdlc->hdev, skb);
		skb = NULL;
	}

	shdlc->nr = (shdlc->nr + 1) % 8;

	if (nfc_shdlc_x_lt_y_lteq_z(shdlc->dnr, y_nr, shdlc->ns)) {
		nfc_shdlc_reset_t2(shdlc, y_nr);

		shdlc->dnr = y_nr;
	}

exit:
	if (skb)
		kfree_skb(skb);
}

static void nfc_shdlc_rcv_ack(struct nfc_shdlc *shdlc, int y_nr)
{
	pr_debug("remote acked up to frame %d excluded\n", y_nr);

	if (nfc_shdlc_x_lt_y_lteq_z(shdlc->dnr, y_nr, shdlc->ns)) {
		nfc_shdlc_reset_t2(shdlc, y_nr);
		shdlc->dnr = y_nr;
	}
}

static void nfc_shdlc_requeue_ack_pending(struct nfc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("ns reset to %d\n", shdlc->dnr);

	while ((skb = skb_dequeue_tail(&shdlc->ack_pending_q))) {
		skb_pull(skb, 2);	/* remove len+control */
		skb_trim(skb, skb->len - 2);	/* remove crc */
		skb_queue_head(&shdlc->send_q, skb);
	}
	shdlc->ns = shdlc->dnr;
}

static void nfc_shdlc_rcv_rej(struct nfc_shdlc *shdlc, int y_nr)
{
	struct sk_buff *skb;

	pr_debug("remote asks retransmition from frame %d\n", y_nr);

	if (nfc_shdlc_x_lteq_y_lt_z(shdlc->dnr, y_nr, shdlc->ns)) {
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

		nfc_shdlc_requeue_ack_pending(shdlc);
	}
}

/* See spec RR:10.8.3 REJ:10.8.4 */
static void nfc_shdlc_rcv_s_frame(struct nfc_shdlc *shdlc,
				  enum sframe_type s_frame_type, int nr)
{
	struct sk_buff *skb;

	if (shdlc->state != SHDLC_CONNECTED)
		return;

	switch (s_frame_type) {
	case S_FRAME_RR:
		nfc_shdlc_rcv_ack(shdlc, nr);
		if (shdlc->rnr == true) {	/* see SHDLC 10.7.7 */
			shdlc->rnr = false;
			if (shdlc->send_q.qlen == 0) {
				skb = nfc_shdlc_alloc_skb(shdlc, 0);
				if (skb)
					skb_queue_tail(&shdlc->send_q, skb);
			}
		}
		break;
	case S_FRAME_REJ:
		nfc_shdlc_rcv_rej(shdlc, nr);
		break;
	case S_FRAME_RNR:
		nfc_shdlc_rcv_ack(shdlc, nr);
		shdlc->rnr = true;
		break;
	default:
		break;
	}
}

static void nfc_shdlc_connect_complete(struct nfc_shdlc *shdlc, int r)
{
	pr_debug("result=%d\n", r);

	del_timer_sync(&shdlc->connect_timer);

	if (r == 0) {
		shdlc->ns = 0;
		shdlc->nr = 0;
		shdlc->dnr = 0;

		shdlc->state = SHDLC_CONNECTED;
	} else {
		shdlc->state = SHDLC_DISCONNECTED;

		/*
		 * TODO: Could it be possible that there are pending
		 * executing commands that are waiting for connect to complete
		 * before they can be carried? As connect is a blocking
		 * operation, it would require that the userspace process can
		 * send commands on the same device from a second thread before
		 * the device is up. I don't think that is possible, is it?
		 */
	}

	shdlc->connect_result = r;

	wake_up(shdlc->connect_wq);
}

static int nfc_shdlc_connect_initiate(struct nfc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("\n");

	skb = nfc_shdlc_alloc_skb(shdlc, 2);
	if (skb == NULL)
		return -ENOMEM;

	*skb_put(skb, 1) = SHDLC_MAX_WINDOW;
	*skb_put(skb, 1) = SHDLC_SREJ_SUPPORT ? 1 : 0;

	return nfc_shdlc_send_u_frame(shdlc, skb, U_FRAME_RSET);
}

static int nfc_shdlc_connect_send_ua(struct nfc_shdlc *shdlc)
{
	struct sk_buff *skb;

	pr_debug("\n");

	skb = nfc_shdlc_alloc_skb(shdlc, 0);
	if (skb == NULL)
		return -ENOMEM;

	return nfc_shdlc_send_u_frame(shdlc, skb, U_FRAME_UA);
}

static void nfc_shdlc_rcv_u_frame(struct nfc_shdlc *shdlc,
				  struct sk_buff *skb,
				  enum uframe_modifier u_frame_modifier)
{
	u8 w = SHDLC_MAX_WINDOW;
	bool srej_support = SHDLC_SREJ_SUPPORT;
	int r;

	pr_debug("u_frame_modifier=%d\n", u_frame_modifier);

	switch (u_frame_modifier) {
	case U_FRAME_RSET:
		if (shdlc->state == SHDLC_NEGOCIATING) {
			/* we sent RSET, but chip wants to negociate */
			if (skb->len > 0)
				w = skb->data[0];

			if (skb->len > 1)
				srej_support = skb->data[1] & 0x01 ? true :
					       false;

			if ((w <= SHDLC_MAX_WINDOW) &&
			    (SHDLC_SREJ_SUPPORT || (srej_support == false))) {
				shdlc->w = w;
				shdlc->srej_support = srej_support;
				r = nfc_shdlc_connect_send_ua(shdlc);
				nfc_shdlc_connect_complete(shdlc, r);
			}
		} else if (shdlc->state > SHDLC_NEGOCIATING) {
			/*
			 * TODO: Chip wants to reset link
			 * send ua, empty skb lists, reset counters
			 * propagate info to HCI layer
			 */
		}
		break;
	case U_FRAME_UA:
		if ((shdlc->state == SHDLC_CONNECTING &&
		     shdlc->connect_tries > 0) ||
		    (shdlc->state == SHDLC_NEGOCIATING))
			nfc_shdlc_connect_complete(shdlc, 0);
		break;
	default:
		break;
	}

	kfree_skb(skb);
}

static void nfc_shdlc_handle_rcv_queue(struct nfc_shdlc *shdlc)
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
			ns = (control & SHDLC_CONTROL_NS_MASK) >> 3;
			nr = control & SHDLC_CONTROL_NR_MASK;
			nfc_shdlc_rcv_i_frame(shdlc, skb, ns, nr);
			break;
		case SHDLC_CONTROL_HEAD_S:
			s_frame_type = (control & SHDLC_CONTROL_TYPE_MASK) >> 3;
			nr = control & SHDLC_CONTROL_NR_MASK;
			nfc_shdlc_rcv_s_frame(shdlc, s_frame_type, nr);
			kfree_skb(skb);
			break;
		case SHDLC_CONTROL_HEAD_U:
			u_frame_modifier = control & SHDLC_CONTROL_M_MASK;
			nfc_shdlc_rcv_u_frame(shdlc, skb, u_frame_modifier);
			break;
		default:
			pr_err("UNKNOWN Control=%d\n", control);
			kfree_skb(skb);
			break;
		}
	}
}

static int nfc_shdlc_w_used(int ns, int dnr)
{
	int unack_count;

	if (dnr <= ns)
		unack_count = ns - dnr;
	else
		unack_count = 8 - dnr + ns;

	return unack_count;
}

/* Send frames according to algorithm at spec:10.8.1 */
static void nfc_shdlc_handle_send_queue(struct nfc_shdlc *shdlc)
{
	struct sk_buff *skb;
	int r;
	unsigned long time_sent;

	if (shdlc->send_q.qlen)
		pr_debug
		    ("sendQlen=%d ns=%d dnr=%d rnr=%s w_room=%d unackQlen=%d\n",
		     shdlc->send_q.qlen, shdlc->ns, shdlc->dnr,
		     shdlc->rnr == false ? "false" : "true",
		     shdlc->w - nfc_shdlc_w_used(shdlc->ns, shdlc->dnr),
		     shdlc->ack_pending_q.qlen);

	while (shdlc->send_q.qlen && shdlc->ack_pending_q.qlen < shdlc->w &&
	       (shdlc->rnr == false)) {

		if (shdlc->t1_active) {
			del_timer_sync(&shdlc->t1_timer);
			shdlc->t1_active = false;
			pr_debug("Stopped T1(send ack)\n");
		}

		skb = skb_dequeue(&shdlc->send_q);

		*skb_push(skb, 1) = SHDLC_CONTROL_HEAD_I | (shdlc->ns << 3) |
				    shdlc->nr;

		pr_debug("Sending I-Frame %d, waiting to rcv %d\n", shdlc->ns,
			 shdlc->nr);
	/*	SHDLC_DUMP_SKB("shdlc frame written", skb); */

		nfc_shdlc_add_len_crc(skb);

		r = shdlc->ops->xmit(shdlc, skb);
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

static void nfc_shdlc_connect_timeout(unsigned long data)
{
	struct nfc_shdlc *shdlc = (struct nfc_shdlc *)data;

	pr_debug("\n");

	queue_work(shdlc->sm_wq, &shdlc->sm_work);
}

static void nfc_shdlc_t1_timeout(unsigned long data)
{
	struct nfc_shdlc *shdlc = (struct nfc_shdlc *)data;

	pr_debug("SoftIRQ: need to send ack\n");

	queue_work(shdlc->sm_wq, &shdlc->sm_work);
}

static void nfc_shdlc_t2_timeout(unsigned long data)
{
	struct nfc_shdlc *shdlc = (struct nfc_shdlc *)data;

	pr_debug("SoftIRQ: need to retransmit\n");

	queue_work(shdlc->sm_wq, &shdlc->sm_work);
}

static void nfc_shdlc_sm_work(struct work_struct *work)
{
	struct nfc_shdlc *shdlc = container_of(work, struct nfc_shdlc, sm_work);
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
			nfc_shdlc_connect_complete(shdlc, shdlc->hard_fault);
			break;
		}

		if (shdlc->connect_tries++ < 5)
			r = nfc_shdlc_connect_initiate(shdlc);
		else
			r = -ETIME;
		if (r < 0)
			nfc_shdlc_connect_complete(shdlc, r);
		else {
			mod_timer(&shdlc->connect_timer, jiffies +
				  msecs_to_jiffies(SHDLC_CONNECT_VALUE_MS));

			shdlc->state = SHDLC_NEGOCIATING;
		}
		break;
	case SHDLC_NEGOCIATING:
		if (timer_pending(&shdlc->connect_timer) == 0) {
			shdlc->state = SHDLC_CONNECTING;
			queue_work(shdlc->sm_wq, &shdlc->sm_work);
		}

		nfc_shdlc_handle_rcv_queue(shdlc);

		if (shdlc->hard_fault) {
			nfc_shdlc_connect_complete(shdlc, shdlc->hard_fault);
			break;
		}
		break;
	case SHDLC_CONNECTED:
		nfc_shdlc_handle_rcv_queue(shdlc);
		nfc_shdlc_handle_send_queue(shdlc);

		if (shdlc->t1_active && timer_pending(&shdlc->t1_timer) == 0) {
			pr_debug
			    ("Handle T1(send ack) elapsed (T1 now inactive)\n");

			shdlc->t1_active = false;
			r = nfc_shdlc_send_s_frame(shdlc, S_FRAME_RR,
						   shdlc->nr);
			if (r < 0)
				shdlc->hard_fault = r;
		}

		if (shdlc->t2_active && timer_pending(&shdlc->t2_timer) == 0) {
			pr_debug
			    ("Handle T2(retransmit) elapsed (T2 inactive)\n");

			shdlc->t2_active = false;

			nfc_shdlc_requeue_ack_pending(shdlc);
			nfc_shdlc_handle_send_queue(shdlc);
		}

		if (shdlc->hard_fault) {
			nfc_hci_driver_failure(shdlc->hdev, shdlc->hard_fault);
		}
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
static int nfc_shdlc_connect(struct nfc_shdlc *shdlc)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(connect_wq);

	pr_debug("\n");

	mutex_lock(&shdlc->state_mutex);

	shdlc->state = SHDLC_CONNECTING;
	shdlc->connect_wq = &connect_wq;
	shdlc->connect_tries = 0;
	shdlc->connect_result = 1;

	mutex_unlock(&shdlc->state_mutex);

	queue_work(shdlc->sm_wq, &shdlc->sm_work);

	wait_event(connect_wq, shdlc->connect_result != 1);

	return shdlc->connect_result;
}

static void nfc_shdlc_disconnect(struct nfc_shdlc *shdlc)
{
	pr_debug("\n");

	mutex_lock(&shdlc->state_mutex);

	shdlc->state = SHDLC_DISCONNECTED;

	mutex_unlock(&shdlc->state_mutex);

	queue_work(shdlc->sm_wq, &shdlc->sm_work);
}

/*
 * Receive an incoming shdlc frame. Frame has already been crc-validated.
 * skb contains only LLC header and payload.
 * If skb == NULL, it is a notification that the link below is dead.
 */
void nfc_shdlc_recv_frame(struct nfc_shdlc *shdlc, struct sk_buff *skb)
{
	if (skb == NULL) {
		pr_err("NULL Frame -> link is dead\n");
		shdlc->hard_fault = -EREMOTEIO;
	} else {
		SHDLC_DUMP_SKB("incoming frame", skb);
		skb_queue_tail(&shdlc->rcv_q, skb);
	}

	queue_work(shdlc->sm_wq, &shdlc->sm_work);
}
EXPORT_SYMBOL(nfc_shdlc_recv_frame);

static int nfc_shdlc_open(struct nfc_hci_dev *hdev)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);
	int r;

	pr_debug("\n");

	if (shdlc->ops->open) {
		r = shdlc->ops->open(shdlc);
		if (r < 0)
			return r;
	}

	r = nfc_shdlc_connect(shdlc);
	if (r < 0 && shdlc->ops->close)
		shdlc->ops->close(shdlc);

	return r;
}

static void nfc_shdlc_close(struct nfc_hci_dev *hdev)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	pr_debug("\n");

	nfc_shdlc_disconnect(shdlc);

	if (shdlc->ops->close)
		shdlc->ops->close(shdlc);
}

static int nfc_shdlc_hci_ready(struct nfc_hci_dev *hdev)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);
	int r = 0;

	pr_debug("\n");

	if (shdlc->ops->hci_ready)
		r = shdlc->ops->hci_ready(shdlc);

	return r;
}

static int nfc_shdlc_xmit(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	SHDLC_DUMP_SKB("queuing HCP packet to shdlc", skb);

	skb_queue_tail(&shdlc->send_q, skb);

	queue_work(shdlc->sm_wq, &shdlc->sm_work);

	return 0;
}

static int nfc_shdlc_start_poll(struct nfc_hci_dev *hdev,
				u32 im_protocols, u32 tm_protocols)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	pr_debug("\n");

	if (shdlc->ops->start_poll)
		return shdlc->ops->start_poll(shdlc,
					      im_protocols, tm_protocols);

	return 0;
}

static int nfc_shdlc_target_from_gate(struct nfc_hci_dev *hdev, u8 gate,
				      struct nfc_target *target)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	if (shdlc->ops->target_from_gate)
		return shdlc->ops->target_from_gate(shdlc, gate, target);

	return -EPERM;
}

static int nfc_shdlc_complete_target_discovered(struct nfc_hci_dev *hdev,
						u8 gate,
						struct nfc_target *target)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	pr_debug("\n");

	if (shdlc->ops->complete_target_discovered)
		return shdlc->ops->complete_target_discovered(shdlc, gate,
							      target);

	return 0;
}

static int nfc_shdlc_data_exchange(struct nfc_hci_dev *hdev,
				   struct nfc_target *target,
				   struct sk_buff *skb,
				   struct sk_buff **res_skb)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	if (shdlc->ops->data_exchange)
		return shdlc->ops->data_exchange(shdlc, target, skb, res_skb);

	return -EPERM;
}

static int nfc_shdlc_check_presence(struct nfc_hci_dev *hdev,
				    struct nfc_target *target)
{
	struct nfc_shdlc *shdlc = nfc_hci_get_clientdata(hdev);

	if (shdlc->ops->check_presence)
		return shdlc->ops->check_presence(shdlc, target);

	return 0;
}

static struct nfc_hci_ops shdlc_ops = {
	.open = nfc_shdlc_open,
	.close = nfc_shdlc_close,
	.hci_ready = nfc_shdlc_hci_ready,
	.xmit = nfc_shdlc_xmit,
	.start_poll = nfc_shdlc_start_poll,
	.target_from_gate = nfc_shdlc_target_from_gate,
	.complete_target_discovered = nfc_shdlc_complete_target_discovered,
	.data_exchange = nfc_shdlc_data_exchange,
	.check_presence = nfc_shdlc_check_presence,
};

struct nfc_shdlc *nfc_shdlc_allocate(struct nfc_shdlc_ops *ops,
				     struct nfc_hci_init_data *init_data,
				     u32 protocols,
				     int tx_headroom, int tx_tailroom,
				     int max_link_payload, const char *devname)
{
	struct nfc_shdlc *shdlc;
	int r;
	char name[32];

	if (ops->xmit == NULL)
		return NULL;

	shdlc = kzalloc(sizeof(struct nfc_shdlc), GFP_KERNEL);
	if (shdlc == NULL)
		return NULL;

	mutex_init(&shdlc->state_mutex);
	shdlc->ops = ops;
	shdlc->state = SHDLC_DISCONNECTED;

	init_timer(&shdlc->connect_timer);
	shdlc->connect_timer.data = (unsigned long)shdlc;
	shdlc->connect_timer.function = nfc_shdlc_connect_timeout;

	init_timer(&shdlc->t1_timer);
	shdlc->t1_timer.data = (unsigned long)shdlc;
	shdlc->t1_timer.function = nfc_shdlc_t1_timeout;

	init_timer(&shdlc->t2_timer);
	shdlc->t2_timer.data = (unsigned long)shdlc;
	shdlc->t2_timer.function = nfc_shdlc_t2_timeout;

	shdlc->w = SHDLC_MAX_WINDOW;
	shdlc->srej_support = SHDLC_SREJ_SUPPORT;

	skb_queue_head_init(&shdlc->rcv_q);
	skb_queue_head_init(&shdlc->send_q);
	skb_queue_head_init(&shdlc->ack_pending_q);

	INIT_WORK(&shdlc->sm_work, nfc_shdlc_sm_work);
	snprintf(name, sizeof(name), "%s_shdlc_sm_wq", devname);
	shdlc->sm_wq = alloc_workqueue(name, WQ_NON_REENTRANT | WQ_UNBOUND |
				       WQ_MEM_RECLAIM, 1);
	if (shdlc->sm_wq == NULL)
		goto err_allocwq;

	shdlc->client_headroom = tx_headroom;
	shdlc->client_tailroom = tx_tailroom;

	shdlc->hdev = nfc_hci_allocate_device(&shdlc_ops, init_data, protocols,
					      tx_headroom + SHDLC_LLC_HEAD_ROOM,
					      tx_tailroom + SHDLC_LLC_TAIL_ROOM,
					      max_link_payload);
	if (shdlc->hdev == NULL)
		goto err_allocdev;

	nfc_hci_set_clientdata(shdlc->hdev, shdlc);

	r = nfc_hci_register_device(shdlc->hdev);
	if (r < 0)
		goto err_regdev;

	return shdlc;

err_regdev:
	nfc_hci_free_device(shdlc->hdev);

err_allocdev:
	destroy_workqueue(shdlc->sm_wq);

err_allocwq:
	kfree(shdlc);

	return NULL;
}
EXPORT_SYMBOL(nfc_shdlc_allocate);

void nfc_shdlc_free(struct nfc_shdlc *shdlc)
{
	pr_debug("\n");

	/* TODO: Check that this cannot be called while still in use */

	nfc_hci_unregister_device(shdlc->hdev);
	nfc_hci_free_device(shdlc->hdev);

	destroy_workqueue(shdlc->sm_wq);

	skb_queue_purge(&shdlc->rcv_q);
	skb_queue_purge(&shdlc->send_q);
	skb_queue_purge(&shdlc->ack_pending_q);

	kfree(shdlc);
}
EXPORT_SYMBOL(nfc_shdlc_free);

void nfc_shdlc_set_clientdata(struct nfc_shdlc *shdlc, void *clientdata)
{
	pr_debug("\n");

	shdlc->clientdata = clientdata;
}
EXPORT_SYMBOL(nfc_shdlc_set_clientdata);

void *nfc_shdlc_get_clientdata(struct nfc_shdlc *shdlc)
{
	return shdlc->clientdata;
}
EXPORT_SYMBOL(nfc_shdlc_get_clientdata);

struct nfc_hci_dev *nfc_shdlc_get_hci_dev(struct nfc_shdlc *shdlc)
{
	return shdlc->hdev;
}
EXPORT_SYMBOL(nfc_shdlc_get_hci_dev);
