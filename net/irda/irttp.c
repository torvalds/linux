/*********************************************************************
 *
 * Filename:      irttp.c
 * Version:       1.2
 * Description:   Tiny Transport Protocol (TTP) implementation
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Wed Jan  5 11:31:27 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>,
 *     All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/parameters.h>
#include <net/irda/irttp.h>

static struct irttp_cb *irttp;

static void __irttp_close_tsap(struct tsap_cb *self);

static int irttp_data_indication(void *instance, void *sap,
				 struct sk_buff *skb);
static int irttp_udata_indication(void *instance, void *sap,
				  struct sk_buff *skb);
static void irttp_disconnect_indication(void *instance, void *sap,
					LM_REASON reason, struct sk_buff *);
static void irttp_connect_indication(void *instance, void *sap,
				     struct qos_info *qos, __u32 max_sdu_size,
				     __u8 header_size, struct sk_buff *skb);
static void irttp_connect_confirm(void *instance, void *sap,
				  struct qos_info *qos, __u32 max_sdu_size,
				  __u8 header_size, struct sk_buff *skb);
static void irttp_run_tx_queue(struct tsap_cb *self);
static void irttp_run_rx_queue(struct tsap_cb *self);

static void irttp_flush_queues(struct tsap_cb *self);
static void irttp_fragment_skb(struct tsap_cb *self, struct sk_buff *skb);
static struct sk_buff *irttp_reassemble_skb(struct tsap_cb *self);
static void irttp_todo_expired(unsigned long data);
static int irttp_param_max_sdu_size(void *instance, irda_param_t *param,
				    int get);

static void irttp_flow_indication(void *instance, void *sap, LOCAL_FLOW flow);
static void irttp_status_indication(void *instance,
				    LINK_STATUS link, LOCK_STATUS lock);

/* Information for parsing parameters in IrTTP */
static pi_minor_info_t pi_minor_call_table[] = {
	{ NULL, 0 },                                             /* 0x00 */
	{ irttp_param_max_sdu_size, PV_INTEGER | PV_BIG_ENDIAN } /* 0x01 */
};
static pi_major_info_t pi_major_call_table[] = {{ pi_minor_call_table, 2 }};
static pi_param_info_t param_info = { pi_major_call_table, 1, 0x0f, 4 };

/************************ GLOBAL PROCEDURES ************************/

/*
 * Function irttp_init (void)
 *
 *    Initialize the IrTTP layer. Called by module initialization code
 *
 */
int __init irttp_init(void)
{
	irttp = kzalloc(sizeof(struct irttp_cb), GFP_KERNEL);
	if (irttp == NULL)
		return -ENOMEM;

	irttp->magic = TTP_MAGIC;

	irttp->tsaps = hashbin_new(HB_LOCK);
	if (!irttp->tsaps) {
		IRDA_ERROR("%s: can't allocate IrTTP hashbin!\n",
			   __func__);
		kfree(irttp);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Function irttp_cleanup (void)
 *
 *    Called by module destruction/cleanup code
 *
 */
void irttp_cleanup(void)
{
	/* Check for main structure */
	IRDA_ASSERT(irttp->magic == TTP_MAGIC, return;);

	/*
	 *  Delete hashbin and close all TSAP instances in it
	 */
	hashbin_delete(irttp->tsaps, (FREE_FUNC) __irttp_close_tsap);

	irttp->magic = 0;

	/* De-allocate main structure */
	kfree(irttp);

	irttp = NULL;
}

/*************************** SUBROUTINES ***************************/

/*
 * Function irttp_start_todo_timer (self, timeout)
 *
 *    Start todo timer.
 *
 * Made it more effient and unsensitive to race conditions - Jean II
 */
static inline void irttp_start_todo_timer(struct tsap_cb *self, int timeout)
{
	/* Set new value for timer */
	mod_timer(&self->todo_timer, jiffies + timeout);
}

/*
 * Function irttp_todo_expired (data)
 *
 *    Todo timer has expired!
 *
 * One of the restriction of the timer is that it is run only on the timer
 * interrupt which run every 10ms. This mean that even if you set the timer
 * with a delay of 0, it may take up to 10ms before it's run.
 * So, to minimise latency and keep cache fresh, we try to avoid using
 * it as much as possible.
 * Note : we can't use tasklets, because they can't be asynchronously
 * killed (need user context), and we can't guarantee that here...
 * Jean II
 */
static void irttp_todo_expired(unsigned long data)
{
	struct tsap_cb *self = (struct tsap_cb *) data;

	/* Check that we still exist */
	if (!self || self->magic != TTP_TSAP_MAGIC)
		return;

	IRDA_DEBUG(4, "%s(instance=%p)\n", __func__, self);

	/* Try to make some progress, especially on Tx side - Jean II */
	irttp_run_rx_queue(self);
	irttp_run_tx_queue(self);

	/* Check if time for disconnect */
	if (test_bit(0, &self->disconnect_pend)) {
		/* Check if it's possible to disconnect yet */
		if (skb_queue_empty(&self->tx_queue)) {
			/* Make sure disconnect is not pending anymore */
			clear_bit(0, &self->disconnect_pend);	/* FALSE */

			/* Note : self->disconnect_skb may be NULL */
			irttp_disconnect_request(self, self->disconnect_skb,
						 P_NORMAL);
			self->disconnect_skb = NULL;
		} else {
			/* Try again later */
			irttp_start_todo_timer(self, HZ/10);

			/* No reason to try and close now */
			return;
		}
	}

	/* Check if it's closing time */
	if (self->close_pend)
		/* Finish cleanup */
		irttp_close_tsap(self);
}

/*
 * Function irttp_flush_queues (self)
 *
 *     Flushes (removes all frames) in transitt-buffer (tx_list)
 */
static void irttp_flush_queues(struct tsap_cb *self)
{
	struct sk_buff* skb;

	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	/* Deallocate frames waiting to be sent */
	while ((skb = skb_dequeue(&self->tx_queue)) != NULL)
		dev_kfree_skb(skb);

	/* Deallocate received frames */
	while ((skb = skb_dequeue(&self->rx_queue)) != NULL)
		dev_kfree_skb(skb);

	/* Deallocate received fragments */
	while ((skb = skb_dequeue(&self->rx_fragments)) != NULL)
		dev_kfree_skb(skb);
}

/*
 * Function irttp_reassemble (self)
 *
 *    Makes a new (continuous) skb of all the fragments in the fragment
 *    queue
 *
 */
static struct sk_buff *irttp_reassemble_skb(struct tsap_cb *self)
{
	struct sk_buff *skb, *frag;
	int n = 0;  /* Fragment index */

	IRDA_ASSERT(self != NULL, return NULL;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return NULL;);

	IRDA_DEBUG(2, "%s(), self->rx_sdu_size=%d\n", __func__,
		   self->rx_sdu_size);

	skb = dev_alloc_skb(TTP_HEADER + self->rx_sdu_size);
	if (!skb)
		return NULL;

	/*
	 * Need to reserve space for TTP header in case this skb needs to
	 * be requeued in case delivery failes
	 */
	skb_reserve(skb, TTP_HEADER);
	skb_put(skb, self->rx_sdu_size);

	/*
	 *  Copy all fragments to a new buffer
	 */
	while ((frag = skb_dequeue(&self->rx_fragments)) != NULL) {
		skb_copy_to_linear_data_offset(skb, n, frag->data, frag->len);
		n += frag->len;

		dev_kfree_skb(frag);
	}

	IRDA_DEBUG(2,
		   "%s(), frame len=%d, rx_sdu_size=%d, rx_max_sdu_size=%d\n",
		   __func__, n, self->rx_sdu_size, self->rx_max_sdu_size);
	/* Note : irttp_run_rx_queue() calculate self->rx_sdu_size
	 * by summing the size of all fragments, so we should always
	 * have n == self->rx_sdu_size, except in cases where we
	 * droped the last fragment (when self->rx_sdu_size exceed
	 * self->rx_max_sdu_size), where n < self->rx_sdu_size.
	 * Jean II */
	IRDA_ASSERT(n <= self->rx_sdu_size, n = self->rx_sdu_size;);

	/* Set the new length */
	skb_trim(skb, n);

	self->rx_sdu_size = 0;

	return skb;
}

/*
 * Function irttp_fragment_skb (skb)
 *
 *    Fragments a frame and queues all the fragments for transmission
 *
 */
static inline void irttp_fragment_skb(struct tsap_cb *self,
				      struct sk_buff *skb)
{
	struct sk_buff *frag;
	__u8 *frame;

	IRDA_DEBUG(2, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	/*
	 *  Split frame into a number of segments
	 */
	while (skb->len > self->max_seg_size) {
		IRDA_DEBUG(2, "%s(), fragmenting ...\n", __func__);

		/* Make new segment */
		frag = alloc_skb(self->max_seg_size+self->max_header_size,
				 GFP_ATOMIC);
		if (!frag)
			return;

		skb_reserve(frag, self->max_header_size);

		/* Copy data from the original skb into this fragment. */
		skb_copy_from_linear_data(skb, skb_put(frag, self->max_seg_size),
			      self->max_seg_size);

		/* Insert TTP header, with the more bit set */
		frame = skb_push(frag, TTP_HEADER);
		frame[0] = TTP_MORE;

		/* Hide the copied data from the original skb */
		skb_pull(skb, self->max_seg_size);

		/* Queue fragment */
		skb_queue_tail(&self->tx_queue, frag);
	}
	/* Queue what is left of the original skb */
	IRDA_DEBUG(2, "%s(), queuing last segment\n", __func__);

	frame = skb_push(skb, TTP_HEADER);
	frame[0] = 0x00; /* Clear more bit */

	/* Queue fragment */
	skb_queue_tail(&self->tx_queue, skb);
}

/*
 * Function irttp_param_max_sdu_size (self, param)
 *
 *    Handle the MaxSduSize parameter in the connect frames, this function
 *    will be called both when this parameter needs to be inserted into, and
 *    extracted from the connect frames
 */
static int irttp_param_max_sdu_size(void *instance, irda_param_t *param,
				    int get)
{
	struct tsap_cb *self;

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->tx_max_sdu_size;
	else
		self->tx_max_sdu_size = param->pv.i;

	IRDA_DEBUG(1, "%s(), MaxSduSize=%d\n", __func__, param->pv.i);

	return 0;
}

/*************************** CLIENT CALLS ***************************/
/************************** LMP CALLBACKS **************************/
/* Everything is happily mixed up. Waiting for next clean up - Jean II */

/*
 * Initialization, that has to be done on new tsap
 * instance allocation and on duplication
 */
static void irttp_init_tsap(struct tsap_cb *tsap)
{
	spin_lock_init(&tsap->lock);
	init_timer(&tsap->todo_timer);

	skb_queue_head_init(&tsap->rx_queue);
	skb_queue_head_init(&tsap->tx_queue);
	skb_queue_head_init(&tsap->rx_fragments);
}

/*
 * Function irttp_open_tsap (stsap, notify)
 *
 *    Create TSAP connection endpoint,
 */
struct tsap_cb *irttp_open_tsap(__u8 stsap_sel, int credit, notify_t *notify)
{
	struct tsap_cb *self;
	struct lsap_cb *lsap;
	notify_t ttp_notify;

	IRDA_ASSERT(irttp->magic == TTP_MAGIC, return NULL;);

	/* The IrLMP spec (IrLMP 1.1 p10) says that we have the right to
	 * use only 0x01-0x6F. Of course, we can use LSAP_ANY as well.
	 * JeanII */
	if((stsap_sel != LSAP_ANY) &&
	   ((stsap_sel < 0x01) || (stsap_sel >= 0x70))) {
		IRDA_DEBUG(0, "%s(), invalid tsap!\n", __func__);
		return NULL;
	}

	self = kzalloc(sizeof(struct tsap_cb), GFP_ATOMIC);
	if (self == NULL) {
		IRDA_DEBUG(0, "%s(), unable to kmalloc!\n", __func__);
		return NULL;
	}

	/* Initialize internal objects */
	irttp_init_tsap(self);

	/* Initialise todo timer */
	self->todo_timer.data     = (unsigned long) self;
	self->todo_timer.function = &irttp_todo_expired;

	/* Initialize callbacks for IrLMP to use */
	irda_notify_init(&ttp_notify);
	ttp_notify.connect_confirm = irttp_connect_confirm;
	ttp_notify.connect_indication = irttp_connect_indication;
	ttp_notify.disconnect_indication = irttp_disconnect_indication;
	ttp_notify.data_indication = irttp_data_indication;
	ttp_notify.udata_indication = irttp_udata_indication;
	ttp_notify.flow_indication = irttp_flow_indication;
	if(notify->status_indication != NULL)
		ttp_notify.status_indication = irttp_status_indication;
	ttp_notify.instance = self;
	strncpy(ttp_notify.name, notify->name, NOTIFY_MAX_NAME);

	self->magic = TTP_TSAP_MAGIC;
	self->connected = FALSE;

	/*
	 *  Create LSAP at IrLMP layer
	 */
	lsap = irlmp_open_lsap(stsap_sel, &ttp_notify, 0);
	if (lsap == NULL) {
		IRDA_WARNING("%s: unable to allocate LSAP!!\n", __func__);
		return NULL;
	}

	/*
	 *  If user specified LSAP_ANY as source TSAP selector, then IrLMP
	 *  will replace it with whatever source selector which is free, so
	 *  the stsap_sel we have might not be valid anymore
	 */
	self->stsap_sel = lsap->slsap_sel;
	IRDA_DEBUG(4, "%s(), stsap_sel=%02x\n", __func__, self->stsap_sel);

	self->notify = *notify;
	self->lsap = lsap;

	hashbin_insert(irttp->tsaps, (irda_queue_t *) self, (long) self, NULL);

	if (credit > TTP_RX_MAX_CREDIT)
		self->initial_credit = TTP_RX_MAX_CREDIT;
	else
		self->initial_credit = credit;

	return self;
}
EXPORT_SYMBOL(irttp_open_tsap);

/*
 * Function irttp_close (handle)
 *
 *    Remove an instance of a TSAP. This function should only deal with the
 *    deallocation of the TSAP, and resetting of the TSAPs values;
 *
 */
static void __irttp_close_tsap(struct tsap_cb *self)
{
	/* First make sure we're connected. */
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	irttp_flush_queues(self);

	del_timer(&self->todo_timer);

	/* This one won't be cleaned up if we are disconnect_pend + close_pend
	 * and we receive a disconnect_indication */
	if (self->disconnect_skb)
		dev_kfree_skb(self->disconnect_skb);

	self->connected = FALSE;
	self->magic = ~TTP_TSAP_MAGIC;

	kfree(self);
}

/*
 * Function irttp_close (self)
 *
 *    Remove TSAP from list of all TSAPs and then deallocate all resources
 *    associated with this TSAP
 *
 * Note : because we *free* the tsap structure, it is the responsibility
 * of the caller to make sure we are called only once and to deal with
 * possible race conditions. - Jean II
 */
int irttp_close_tsap(struct tsap_cb *self)
{
	struct tsap_cb *tsap;

	IRDA_DEBUG(4, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	/* Make sure tsap has been disconnected */
	if (self->connected) {
		/* Check if disconnect is not pending */
		if (!test_bit(0, &self->disconnect_pend)) {
			IRDA_WARNING("%s: TSAP still connected!\n",
				     __func__);
			irttp_disconnect_request(self, NULL, P_NORMAL);
		}
		self->close_pend = TRUE;
		irttp_start_todo_timer(self, HZ/10);

		return 0; /* Will be back! */
	}

	tsap = hashbin_remove(irttp->tsaps, (long) self, NULL);

	IRDA_ASSERT(tsap == self, return -1;);

	/* Close corresponding LSAP */
	if (self->lsap) {
		irlmp_close_lsap(self->lsap);
		self->lsap = NULL;
	}

	__irttp_close_tsap(self);

	return 0;
}
EXPORT_SYMBOL(irttp_close_tsap);

/*
 * Function irttp_udata_request (self, skb)
 *
 *    Send unreliable data on this TSAP
 *
 */
int irttp_udata_request(struct tsap_cb *self, struct sk_buff *skb)
{
	int ret;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	IRDA_DEBUG(4, "%s()\n", __func__);

	/* Take shortcut on zero byte packets */
	if (skb->len == 0) {
		ret = 0;
		goto err;
	}

	/* Check that nothing bad happens */
	if (!self->connected) {
		IRDA_WARNING("%s(), Not connected\n", __func__);
		ret = -ENOTCONN;
		goto err;
	}

	if (skb->len > self->max_seg_size) {
		IRDA_ERROR("%s(), UData is too large for IrLAP!\n", __func__);
		ret = -EMSGSIZE;
		goto err;
	}

	irlmp_udata_request(self->lsap, skb);
	self->stats.tx_packets++;

	return 0;

err:
	dev_kfree_skb(skb);
	return ret;
}
EXPORT_SYMBOL(irttp_udata_request);


/*
 * Function irttp_data_request (handle, skb)
 *
 *    Queue frame for transmission. If SAR is enabled, fragement the frame
 *    and queue the fragments for transmission
 */
int irttp_data_request(struct tsap_cb *self, struct sk_buff *skb)
{
	__u8 *frame;
	int ret;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	IRDA_DEBUG(2, "%s() : queue len = %d\n", __func__,
		   skb_queue_len(&self->tx_queue));

	/* Take shortcut on zero byte packets */
	if (skb->len == 0) {
		ret = 0;
		goto err;
	}

	/* Check that nothing bad happens */
	if (!self->connected) {
		IRDA_WARNING("%s: Not connected\n", __func__);
		ret = -ENOTCONN;
		goto err;
	}

	/*
	 *  Check if SAR is disabled, and the frame is larger than what fits
	 *  inside an IrLAP frame
	 */
	if ((self->tx_max_sdu_size == 0) && (skb->len > self->max_seg_size)) {
		IRDA_ERROR("%s: SAR disabled, and data is too large for IrLAP!\n",
			   __func__);
		ret = -EMSGSIZE;
		goto err;
	}

	/*
	 *  Check if SAR is enabled, and the frame is larger than the
	 *  TxMaxSduSize
	 */
	if ((self->tx_max_sdu_size != 0) &&
	    (self->tx_max_sdu_size != TTP_SAR_UNBOUND) &&
	    (skb->len > self->tx_max_sdu_size))
	{
		IRDA_ERROR("%s: SAR enabled, but data is larger than TxMaxSduSize!\n",
			   __func__);
		ret = -EMSGSIZE;
		goto err;
	}
	/*
	 *  Check if transmit queue is full
	 */
	if (skb_queue_len(&self->tx_queue) >= TTP_TX_MAX_QUEUE) {
		/*
		 *  Give it a chance to empty itself
		 */
		irttp_run_tx_queue(self);

		/* Drop packet. This error code should trigger the caller
		 * to resend the data in the client code - Jean II */
		ret = -ENOBUFS;
		goto err;
	}

	/* Queue frame, or queue frame segments */
	if ((self->tx_max_sdu_size == 0) || (skb->len < self->max_seg_size)) {
		/* Queue frame */
		IRDA_ASSERT(skb_headroom(skb) >= TTP_HEADER, return -1;);
		frame = skb_push(skb, TTP_HEADER);
		frame[0] = 0x00; /* Clear more bit */

		skb_queue_tail(&self->tx_queue, skb);
	} else {
		/*
		 *  Fragment the frame, this function will also queue the
		 *  fragments, we don't care about the fact the transmit
		 *  queue may be overfilled by all the segments for a little
		 *  while
		 */
		irttp_fragment_skb(self, skb);
	}

	/* Check if we can accept more data from client */
	if ((!self->tx_sdu_busy) &&
	    (skb_queue_len(&self->tx_queue) > TTP_TX_HIGH_THRESHOLD)) {
		/* Tx queue filling up, so stop client. */
		if (self->notify.flow_indication) {
			self->notify.flow_indication(self->notify.instance,
						     self, FLOW_STOP);
		}
		/* self->tx_sdu_busy is the state of the client.
		 * Update state after notifying client to avoid
		 * race condition with irttp_flow_indication().
		 * If the queue empty itself after our test but before
		 * we set the flag, we will fix ourselves below in
		 * irttp_run_tx_queue().
		 * Jean II */
		self->tx_sdu_busy = TRUE;
	}

	/* Try to make some progress */
	irttp_run_tx_queue(self);

	return 0;

err:
	dev_kfree_skb(skb);
	return ret;
}
EXPORT_SYMBOL(irttp_data_request);

/*
 * Function irttp_run_tx_queue (self)
 *
 *    Transmit packets queued for transmission (if possible)
 *
 */
static void irttp_run_tx_queue(struct tsap_cb *self)
{
	struct sk_buff *skb;
	unsigned long flags;
	int n;

	IRDA_DEBUG(2, "%s() : send_credit = %d, queue_len = %d\n",
		   __func__,
		   self->send_credit, skb_queue_len(&self->tx_queue));

	/* Get exclusive access to the tx queue, otherwise don't touch it */
	if (irda_lock(&self->tx_queue_lock) == FALSE)
		return;

	/* Try to send out frames as long as we have credits
	 * and as long as LAP is not full. If LAP is full, it will
	 * poll us through irttp_flow_indication() - Jean II */
	while ((self->send_credit > 0) &&
	       (!irlmp_lap_tx_queue_full(self->lsap)) &&
	       (skb = skb_dequeue(&self->tx_queue)))
	{
		/*
		 *  Since we can transmit and receive frames concurrently,
		 *  the code below is a critical region and we must assure that
		 *  nobody messes with the credits while we update them.
		 */
		spin_lock_irqsave(&self->lock, flags);

		n = self->avail_credit;
		self->avail_credit = 0;

		/* Only room for 127 credits in frame */
		if (n > 127) {
			self->avail_credit = n-127;
			n = 127;
		}
		self->remote_credit += n;
		self->send_credit--;

		spin_unlock_irqrestore(&self->lock, flags);

		/*
		 *  More bit must be set by the data_request() or fragment()
		 *  functions
		 */
		skb->data[0] |= (n & 0x7f);

		/* Detach from socket.
		 * The current skb has a reference to the socket that sent
		 * it (skb->sk). When we pass it to IrLMP, the skb will be
		 * stored in in IrLAP (self->wx_list). When we are within
		 * IrLAP, we lose the notion of socket, so we should not
		 * have a reference to a socket. So, we drop it here.
		 *
		 * Why does it matter ?
		 * When the skb is freed (kfree_skb), if it is associated
		 * with a socket, it release buffer space on the socket
		 * (through sock_wfree() and sock_def_write_space()).
		 * If the socket no longer exist, we may crash. Hard.
		 * When we close a socket, we make sure that associated packets
		 * in IrTTP are freed. However, we have no way to cancel
		 * the packet that we have passed to IrLAP. So, if a packet
		 * remains in IrLAP (retry on the link or else) after we
		 * close the socket, we are dead !
		 * Jean II */
		if (skb->sk != NULL) {
			/* IrSOCK application, IrOBEX, ... */
			skb_orphan(skb);
		}
			/* IrCOMM over IrTTP, IrLAN, ... */

		/* Pass the skb to IrLMP - done */
		irlmp_data_request(self->lsap, skb);
		self->stats.tx_packets++;
	}

	/* Check if we can accept more frames from client.
	 * We don't want to wait until the todo timer to do that, and we
	 * can't use tasklets (grr...), so we are obliged to give control
	 * to client. That's ok, this test will be true not too often
	 * (max once per LAP window) and we are called from places
	 * where we can spend a bit of time doing stuff. - Jean II */
	if ((self->tx_sdu_busy) &&
	    (skb_queue_len(&self->tx_queue) < TTP_TX_LOW_THRESHOLD) &&
	    (!self->close_pend))
	{
		if (self->notify.flow_indication)
			self->notify.flow_indication(self->notify.instance,
						     self, FLOW_START);

		/* self->tx_sdu_busy is the state of the client.
		 * We don't really have a race here, but it's always safer
		 * to update our state after the client - Jean II */
		self->tx_sdu_busy = FALSE;
	}

	/* Reset lock */
	self->tx_queue_lock = 0;
}

/*
 * Function irttp_give_credit (self)
 *
 *    Send a dataless flowdata TTP-PDU and give available credit to peer
 *    TSAP
 */
static inline void irttp_give_credit(struct tsap_cb *self)
{
	struct sk_buff *tx_skb = NULL;
	unsigned long flags;
	int n;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	IRDA_DEBUG(4, "%s() send=%d,avail=%d,remote=%d\n",
		   __func__,
		   self->send_credit, self->avail_credit, self->remote_credit);

	/* Give credit to peer */
	tx_skb = alloc_skb(TTP_MAX_HEADER, GFP_ATOMIC);
	if (!tx_skb)
		return;

	/* Reserve space for LMP, and LAP header */
	skb_reserve(tx_skb, LMP_MAX_HEADER);

	/*
	 *  Since we can transmit and receive frames concurrently,
	 *  the code below is a critical region and we must assure that
	 *  nobody messes with the credits while we update them.
	 */
	spin_lock_irqsave(&self->lock, flags);

	n = self->avail_credit;
	self->avail_credit = 0;

	/* Only space for 127 credits in frame */
	if (n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}
	self->remote_credit += n;

	spin_unlock_irqrestore(&self->lock, flags);

	skb_put(tx_skb, 1);
	tx_skb->data[0] = (__u8) (n & 0x7f);

	irlmp_data_request(self->lsap, tx_skb);
	self->stats.tx_packets++;
}

/*
 * Function irttp_udata_indication (instance, sap, skb)
 *
 *    Received some unit-data (unreliable)
 *
 */
static int irttp_udata_indication(void *instance, void *sap,
				  struct sk_buff *skb)
{
	struct tsap_cb *self;
	int err;

	IRDA_DEBUG(4, "%s()\n", __func__);

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	self->stats.rx_packets++;

	/* Just pass data to layer above */
	if (self->notify.udata_indication) {
		err = self->notify.udata_indication(self->notify.instance,
						    self,skb);
		/* Same comment as in irttp_do_data_indication() */
		if (!err)
			return 0;
	}
	/* Either no handler, or handler returns an error */
	dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irttp_data_indication (instance, sap, skb)
 *
 *    Receive segment from IrLMP.
 *
 */
static int irttp_data_indication(void *instance, void *sap,
				 struct sk_buff *skb)
{
	struct tsap_cb *self;
	unsigned long flags;
	int n;

	self = (struct tsap_cb *) instance;

	n = skb->data[0] & 0x7f;     /* Extract the credits */

	self->stats.rx_packets++;

	/*  Deal with inbound credit
	 *  Since we can transmit and receive frames concurrently,
	 *  the code below is a critical region and we must assure that
	 *  nobody messes with the credits while we update them.
	 */
	spin_lock_irqsave(&self->lock, flags);
	self->send_credit += n;
	if (skb->len > 1)
		self->remote_credit--;
	spin_unlock_irqrestore(&self->lock, flags);

	/*
	 *  Data or dataless packet? Dataless frames contains only the
	 *  TTP_HEADER.
	 */
	if (skb->len > 1) {
		/*
		 *  We don't remove the TTP header, since we must preserve the
		 *  more bit, so the defragment routing knows what to do
		 */
		skb_queue_tail(&self->rx_queue, skb);
	} else {
		/* Dataless flowdata TTP-PDU */
		dev_kfree_skb(skb);
	}


	/* Push data to the higher layer.
	 * We do it synchronously because running the todo timer for each
	 * receive packet would be too much overhead and latency.
	 * By passing control to the higher layer, we run the risk that
	 * it may take time or grab a lock. Most often, the higher layer
	 * will only put packet in a queue.
	 * Anyway, packets are only dripping through the IrDA, so we can
	 * have time before the next packet.
	 * Further, we are run from NET_BH, so the worse that can happen is
	 * us missing the optimal time to send back the PF bit in LAP.
	 * Jean II */
	irttp_run_rx_queue(self);

	/* We now give credits to peer in irttp_run_rx_queue().
	 * We need to send credit *NOW*, otherwise we are going
	 * to miss the next Tx window. The todo timer may take
	 * a while before it's run... - Jean II */

	/*
	 * If the peer device has given us some credits and we didn't have
	 * anyone from before, then we need to shedule the tx queue.
	 * We need to do that because our Tx have stopped (so we may not
	 * get any LAP flow indication) and the user may be stopped as
	 * well. - Jean II
	 */
	if (self->send_credit == n) {
		/* Restart pushing stuff to LAP */
		irttp_run_tx_queue(self);
		/* Note : we don't want to schedule the todo timer
		 * because it has horrible latency. No tasklets
		 * because the tasklet API is broken. - Jean II */
	}

	return 0;
}

/*
 * Function irttp_status_indication (self, reason)
 *
 *    Status_indication, just pass to the higher layer...
 *
 */
static void irttp_status_indication(void *instance,
				    LINK_STATUS link, LOCK_STATUS lock)
{
	struct tsap_cb *self;

	IRDA_DEBUG(4, "%s()\n", __func__);

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	/* Check if client has already closed the TSAP and gone away */
	if (self->close_pend)
		return;

	/*
	 *  Inform service user if he has requested it
	 */
	if (self->notify.status_indication != NULL)
		self->notify.status_indication(self->notify.instance,
					       link, lock);
	else
		IRDA_DEBUG(2, "%s(), no handler\n", __func__);
}

/*
 * Function irttp_flow_indication (self, reason)
 *
 *    Flow_indication : IrLAP tells us to send more data.
 *
 */
static void irttp_flow_indication(void *instance, void *sap, LOCAL_FLOW flow)
{
	struct tsap_cb *self;

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	IRDA_DEBUG(4, "%s(instance=%p)\n", __func__, self);

	/* We are "polled" directly from LAP, and the LAP want to fill
	 * its Tx window. We want to do our best to send it data, so that
	 * we maximise the window. On the other hand, we want to limit the
	 * amount of work here so that LAP doesn't hang forever waiting
	 * for packets. - Jean II */

	/* Try to send some packets. Currently, LAP calls us every time
	 * there is one free slot, so we will send only one packet.
	 * This allow the scheduler to do its round robin - Jean II */
	irttp_run_tx_queue(self);

	/* Note regarding the interraction with higher layer.
	 * irttp_run_tx_queue() may call the client when its queue
	 * start to empty, via notify.flow_indication(). Initially.
	 * I wanted this to happen in a tasklet, to avoid client
	 * grabbing the CPU, but we can't use tasklets safely. And timer
	 * is definitely too slow.
	 * This will happen only once per LAP window, and usually at
	 * the third packet (unless window is smaller). LAP is still
	 * doing mtt and sending first packet so it's sort of OK
	 * to do that. Jean II */

	/* If we need to send disconnect. try to do it now */
	if(self->disconnect_pend)
		irttp_start_todo_timer(self, 0);
}

/*
 * Function irttp_flow_request (self, command)
 *
 *    This function could be used by the upper layers to tell IrTTP to stop
 *    delivering frames if the receive queues are starting to get full, or
 *    to tell IrTTP to start delivering frames again.
 */
void irttp_flow_request(struct tsap_cb *self, LOCAL_FLOW flow)
{
	IRDA_DEBUG(1, "%s()\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	switch (flow) {
	case FLOW_STOP:
		IRDA_DEBUG(1, "%s(), flow stop\n", __func__);
		self->rx_sdu_busy = TRUE;
		break;
	case FLOW_START:
		IRDA_DEBUG(1, "%s(), flow start\n", __func__);
		self->rx_sdu_busy = FALSE;

		/* Client say he can accept more data, try to free our
		 * queues ASAP - Jean II */
		irttp_run_rx_queue(self);

		break;
	default:
		IRDA_DEBUG(1, "%s(), Unknown flow command!\n", __func__);
	}
}
EXPORT_SYMBOL(irttp_flow_request);

/*
 * Function irttp_connect_request (self, dtsap_sel, daddr, qos)
 *
 *    Try to connect to remote destination TSAP selector
 *
 */
int irttp_connect_request(struct tsap_cb *self, __u8 dtsap_sel,
			  __u32 saddr, __u32 daddr,
			  struct qos_info *qos, __u32 max_sdu_size,
			  struct sk_buff *userdata)
{
	struct sk_buff *tx_skb;
	__u8 *frame;
	__u8 n;

	IRDA_DEBUG(4, "%s(), max_sdu_size=%d\n", __func__, max_sdu_size);

	IRDA_ASSERT(self != NULL, return -EBADR;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -EBADR;);

	if (self->connected) {
		if(userdata)
			dev_kfree_skb(userdata);
		return -EISCONN;
	}

	/* Any userdata supplied? */
	if (userdata == NULL) {
		tx_skb = alloc_skb(TTP_MAX_HEADER + TTP_SAR_HEADER,
				   GFP_ATOMIC);
		if (!tx_skb)
			return -ENOMEM;

		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve(tx_skb, TTP_MAX_HEADER + TTP_SAR_HEADER);
	} else {
		tx_skb = userdata;
		/*
		 *  Check that the client has reserved enough space for
		 *  headers
		 */
		IRDA_ASSERT(skb_headroom(userdata) >= TTP_MAX_HEADER,
			{ dev_kfree_skb(userdata); return -1; } );
	}

	/* Initialize connection parameters */
	self->connected = FALSE;
	self->avail_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;
	self->dtsap_sel = dtsap_sel;

	n = self->initial_credit;

	self->remote_credit = 0;
	self->send_credit = 0;

	/*
	 *  Give away max 127 credits for now
	 */
	if (n > 127) {
		self->avail_credit=n-127;
		n = 127;
	}

	self->remote_credit = n;

	/* SAR enabled? */
	if (max_sdu_size > 0) {
		IRDA_ASSERT(skb_headroom(tx_skb) >= (TTP_MAX_HEADER + TTP_SAR_HEADER),
			{ dev_kfree_skb(tx_skb); return -1; } );

		/* Insert SAR parameters */
		frame = skb_push(tx_skb, TTP_HEADER+TTP_SAR_HEADER);

		frame[0] = TTP_PARAMETERS | n;
		frame[1] = 0x04; /* Length */
		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */

		put_unaligned(cpu_to_be16((__u16) max_sdu_size),
			      (__be16 *)(frame+4));
	} else {
		/* Insert plain TTP header */
		frame = skb_push(tx_skb, TTP_HEADER);

		/* Insert initial credit in frame */
		frame[0] = n & 0x7f;
	}

	/* Connect with IrLMP. No QoS parameters for now */
	return irlmp_connect_request(self->lsap, dtsap_sel, saddr, daddr, qos,
				     tx_skb);
}
EXPORT_SYMBOL(irttp_connect_request);

/*
 * Function irttp_connect_confirm (handle, qos, skb)
 *
 *    Sevice user confirms TSAP connection with peer.
 *
 */
static void irttp_connect_confirm(void *instance, void *sap,
				  struct qos_info *qos, __u32 max_seg_size,
				  __u8 max_header_size, struct sk_buff *skb)
{
	struct tsap_cb *self;
	int parameters;
	int ret;
	__u8 plen;
	__u8 n;

	IRDA_DEBUG(4, "%s()\n", __func__);

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	self->max_seg_size = max_seg_size - TTP_HEADER;
	self->max_header_size = max_header_size + TTP_HEADER;

	/*
	 *  Check if we have got some QoS parameters back! This should be the
	 *  negotiated QoS for the link.
	 */
	if (qos) {
		IRDA_DEBUG(4, "IrTTP, Negotiated BAUD_RATE: %02x\n",
		       qos->baud_rate.bits);
		IRDA_DEBUG(4, "IrTTP, Negotiated BAUD_RATE: %d bps.\n",
		       qos->baud_rate.value);
	}

	n = skb->data[0] & 0x7f;

	IRDA_DEBUG(4, "%s(), Initial send_credit=%d\n", __func__, n);

	self->send_credit = n;
	self->tx_max_sdu_size = 0;
	self->connected = TRUE;

	parameters = skb->data[0] & 0x80;

	IRDA_ASSERT(skb->len >= TTP_HEADER, return;);
	skb_pull(skb, TTP_HEADER);

	if (parameters) {
		plen = skb->data[0];

		ret = irda_param_extract_all(self, skb->data+1,
					     IRDA_MIN(skb->len-1, plen),
					     &param_info);

		/* Any errors in the parameter list? */
		if (ret < 0) {
			IRDA_WARNING("%s: error extracting parameters\n",
				     __func__);
			dev_kfree_skb(skb);

			/* Do not accept this connection attempt */
			return;
		}
		/* Remove parameters */
		skb_pull(skb, IRDA_MIN(skb->len, plen+1));
	}

	IRDA_DEBUG(4, "%s() send=%d,avail=%d,remote=%d\n", __func__,
	      self->send_credit, self->avail_credit, self->remote_credit);

	IRDA_DEBUG(2, "%s(), MaxSduSize=%d\n", __func__,
		   self->tx_max_sdu_size);

	if (self->notify.connect_confirm) {
		self->notify.connect_confirm(self->notify.instance, self, qos,
					     self->tx_max_sdu_size,
					     self->max_header_size, skb);
	} else
		dev_kfree_skb(skb);
}

/*
 * Function irttp_connect_indication (handle, skb)
 *
 *    Some other device is connecting to this TSAP
 *
 */
static void irttp_connect_indication(void *instance, void *sap,
		struct qos_info *qos, __u32 max_seg_size, __u8 max_header_size,
		struct sk_buff *skb)
{
	struct tsap_cb *self;
	struct lsap_cb *lsap;
	int parameters;
	int ret;
	__u8 plen;
	__u8 n;

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	IRDA_ASSERT(skb != NULL, return;);

	lsap = (struct lsap_cb *) sap;

	self->max_seg_size = max_seg_size - TTP_HEADER;
	self->max_header_size = max_header_size+TTP_HEADER;

	IRDA_DEBUG(4, "%s(), TSAP sel=%02x\n", __func__, self->stsap_sel);

	/* Need to update dtsap_sel if its equal to LSAP_ANY */
	self->dtsap_sel = lsap->dlsap_sel;

	n = skb->data[0] & 0x7f;

	self->send_credit = n;
	self->tx_max_sdu_size = 0;

	parameters = skb->data[0] & 0x80;

	IRDA_ASSERT(skb->len >= TTP_HEADER, return;);
	skb_pull(skb, TTP_HEADER);

	if (parameters) {
		plen = skb->data[0];

		ret = irda_param_extract_all(self, skb->data+1,
					     IRDA_MIN(skb->len-1, plen),
					     &param_info);

		/* Any errors in the parameter list? */
		if (ret < 0) {
			IRDA_WARNING("%s: error extracting parameters\n",
				     __func__);
			dev_kfree_skb(skb);

			/* Do not accept this connection attempt */
			return;
		}

		/* Remove parameters */
		skb_pull(skb, IRDA_MIN(skb->len, plen+1));
	}

	if (self->notify.connect_indication) {
		self->notify.connect_indication(self->notify.instance, self,
						qos, self->tx_max_sdu_size,
						self->max_header_size, skb);
	} else
		dev_kfree_skb(skb);
}

/*
 * Function irttp_connect_response (handle, userdata)
 *
 *    Service user is accepting the connection, just pass it down to
 *    IrLMP!
 *
 */
int irttp_connect_response(struct tsap_cb *self, __u32 max_sdu_size,
			   struct sk_buff *userdata)
{
	struct sk_buff *tx_skb;
	__u8 *frame;
	int ret;
	__u8 n;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	IRDA_DEBUG(4, "%s(), Source TSAP selector=%02x\n", __func__,
		   self->stsap_sel);

	/* Any userdata supplied? */
	if (userdata == NULL) {
		tx_skb = alloc_skb(TTP_MAX_HEADER + TTP_SAR_HEADER,
				   GFP_ATOMIC);
		if (!tx_skb)
			return -ENOMEM;

		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve(tx_skb, TTP_MAX_HEADER + TTP_SAR_HEADER);
	} else {
		tx_skb = userdata;
		/*
		 *  Check that the client has reserved enough space for
		 *  headers
		 */
		IRDA_ASSERT(skb_headroom(userdata) >= TTP_MAX_HEADER,
			{ dev_kfree_skb(userdata); return -1; } );
	}

	self->avail_credit = 0;
	self->remote_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;

	n = self->initial_credit;

	/* Frame has only space for max 127 credits (7 bits) */
	if (n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}

	self->remote_credit = n;
	self->connected = TRUE;

	/* SAR enabled? */
	if (max_sdu_size > 0) {
		IRDA_ASSERT(skb_headroom(tx_skb) >= (TTP_MAX_HEADER + TTP_SAR_HEADER),
			{ dev_kfree_skb(tx_skb); return -1; } );

		/* Insert TTP header with SAR parameters */
		frame = skb_push(tx_skb, TTP_HEADER+TTP_SAR_HEADER);

		frame[0] = TTP_PARAMETERS | n;
		frame[1] = 0x04; /* Length */

		/* irda_param_insert(self, IRTTP_MAX_SDU_SIZE, frame+1,  */
/*				  TTP_SAR_HEADER, &param_info) */

		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */

		put_unaligned(cpu_to_be16((__u16) max_sdu_size),
			      (__be16 *)(frame+4));
	} else {
		/* Insert TTP header */
		frame = skb_push(tx_skb, TTP_HEADER);

		frame[0] = n & 0x7f;
	}

	ret = irlmp_connect_response(self->lsap, tx_skb);

	return ret;
}
EXPORT_SYMBOL(irttp_connect_response);

/*
 * Function irttp_dup (self, instance)
 *
 *    Duplicate TSAP, can be used by servers to confirm a connection on a
 *    new TSAP so it can keep listening on the old one.
 */
struct tsap_cb *irttp_dup(struct tsap_cb *orig, void *instance)
{
	struct tsap_cb *new;
	unsigned long flags;

	IRDA_DEBUG(1, "%s()\n", __func__);

	/* Protect our access to the old tsap instance */
	spin_lock_irqsave(&irttp->tsaps->hb_spinlock, flags);

	/* Find the old instance */
	if (!hashbin_find(irttp->tsaps, (long) orig, NULL)) {
		IRDA_DEBUG(0, "%s(), unable to find TSAP\n", __func__);
		spin_unlock_irqrestore(&irttp->tsaps->hb_spinlock, flags);
		return NULL;
	}

	/* Allocate a new instance */
	new = kmalloc(sizeof(struct tsap_cb), GFP_ATOMIC);
	if (!new) {
		IRDA_DEBUG(0, "%s(), unable to kmalloc\n", __func__);
		spin_unlock_irqrestore(&irttp->tsaps->hb_spinlock, flags);
		return NULL;
	}
	/* Dup */
	memcpy(new, orig, sizeof(struct tsap_cb));
	spin_lock_init(&new->lock);

	/* We don't need the old instance any more */
	spin_unlock_irqrestore(&irttp->tsaps->hb_spinlock, flags);

	/* Try to dup the LSAP (may fail if we were too slow) */
	new->lsap = irlmp_dup(orig->lsap, new);
	if (!new->lsap) {
		IRDA_DEBUG(0, "%s(), dup failed!\n", __func__);
		kfree(new);
		return NULL;
	}

	/* Not everything should be copied */
	new->notify.instance = instance;

	/* Initialize internal objects */
	irttp_init_tsap(new);

	/* This is locked */
	hashbin_insert(irttp->tsaps, (irda_queue_t *) new, (long) new, NULL);

	return new;
}
EXPORT_SYMBOL(irttp_dup);

/*
 * Function irttp_disconnect_request (self)
 *
 *    Close this connection please! If priority is high, the queued data
 *    segments, if any, will be deallocated first
 *
 */
int irttp_disconnect_request(struct tsap_cb *self, struct sk_buff *userdata,
			     int priority)
{
	int ret;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	/* Already disconnected? */
	if (!self->connected) {
		IRDA_DEBUG(4, "%s(), already disconnected!\n", __func__);
		if (userdata)
			dev_kfree_skb(userdata);
		return -1;
	}

	/* Disconnect already pending ?
	 * We need to use an atomic operation to prevent reentry. This
	 * function may be called from various context, like user, timer
	 * for following a disconnect_indication() (i.e. net_bh).
	 * Jean II */
	if(test_and_set_bit(0, &self->disconnect_pend)) {
		IRDA_DEBUG(0, "%s(), disconnect already pending\n",
			   __func__);
		if (userdata)
			dev_kfree_skb(userdata);

		/* Try to make some progress */
		irttp_run_tx_queue(self);
		return -1;
	}

	/*
	 *  Check if there is still data segments in the transmit queue
	 */
	if (!skb_queue_empty(&self->tx_queue)) {
		if (priority == P_HIGH) {
			/*
			 *  No need to send the queued data, if we are
			 *  disconnecting right now since the data will
			 *  not have any usable connection to be sent on
			 */
			IRDA_DEBUG(1, "%s(): High priority!!()\n", __func__);
			irttp_flush_queues(self);
		} else if (priority == P_NORMAL) {
			/*
			 *  Must delay disconnect until after all data segments
			 *  have been sent and the tx_queue is empty
			 */
			/* We'll reuse this one later for the disconnect */
			self->disconnect_skb = userdata;  /* May be NULL */

			irttp_run_tx_queue(self);

			irttp_start_todo_timer(self, HZ/10);
			return -1;
		}
	}
	/* Note : we don't need to check if self->rx_queue is full and the
	 * state of self->rx_sdu_busy because the disconnect response will
	 * be sent at the LMP level (so even if the peer has its Tx queue
	 * full of data). - Jean II */

	IRDA_DEBUG(1, "%s(), Disconnecting ...\n", __func__);
	self->connected = FALSE;

	if (!userdata) {
		struct sk_buff *tx_skb;
		tx_skb = alloc_skb(LMP_MAX_HEADER, GFP_ATOMIC);
		if (!tx_skb)
			return -ENOMEM;

		/*
		 *  Reserve space for MUX and LAP header
		 */
		skb_reserve(tx_skb, LMP_MAX_HEADER);

		userdata = tx_skb;
	}
	ret = irlmp_disconnect_request(self->lsap, userdata);

	/* The disconnect is no longer pending */
	clear_bit(0, &self->disconnect_pend);	/* FALSE */

	return ret;
}
EXPORT_SYMBOL(irttp_disconnect_request);

/*
 * Function irttp_disconnect_indication (self, reason)
 *
 *    Disconnect indication, TSAP disconnected by peer?
 *
 */
static void irttp_disconnect_indication(void *instance, void *sap,
		LM_REASON reason, struct sk_buff *skb)
{
	struct tsap_cb *self;

	IRDA_DEBUG(4, "%s()\n", __func__);

	self = (struct tsap_cb *) instance;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	/* Prevent higher layer to send more data */
	self->connected = FALSE;

	/* Check if client has already tried to close the TSAP */
	if (self->close_pend) {
		/* In this case, the higher layer is probably gone. Don't
		 * bother it and clean up the remains - Jean II */
		if (skb)
			dev_kfree_skb(skb);
		irttp_close_tsap(self);
		return;
	}

	/* If we are here, we assume that is the higher layer is still
	 * waiting for the disconnect notification and able to process it,
	 * even if he tried to disconnect. Otherwise, it would have already
	 * attempted to close the tsap and self->close_pend would be TRUE.
	 * Jean II */

	/* No need to notify the client if has already tried to disconnect */
	if(self->notify.disconnect_indication)
		self->notify.disconnect_indication(self->notify.instance, self,
						   reason, skb);
	else
		if (skb)
			dev_kfree_skb(skb);
}

/*
 * Function irttp_do_data_indication (self, skb)
 *
 *    Try to deliver reassembled skb to layer above, and requeue it if that
 *    for some reason should fail. We mark rx sdu as busy to apply back
 *    pressure is necessary.
 */
static void irttp_do_data_indication(struct tsap_cb *self, struct sk_buff *skb)
{
	int err;

	/* Check if client has already closed the TSAP and gone away */
	if (self->close_pend) {
		dev_kfree_skb(skb);
		return;
	}

	err = self->notify.data_indication(self->notify.instance, self, skb);

	/* Usually the layer above will notify that it's input queue is
	 * starting to get filled by using the flow request, but this may
	 * be difficult, so it can instead just refuse to eat it and just
	 * give an error back
	 */
	if (err) {
		IRDA_DEBUG(0, "%s() requeueing skb!\n", __func__);

		/* Make sure we take a break */
		self->rx_sdu_busy = TRUE;

		/* Need to push the header in again */
		skb_push(skb, TTP_HEADER);
		skb->data[0] = 0x00; /* Make sure MORE bit is cleared */

		/* Put skb back on queue */
		skb_queue_head(&self->rx_queue, skb);
	}
}

/*
 * Function irttp_run_rx_queue (self)
 *
 *     Check if we have any frames to be transmitted, or if we have any
 *     available credit to give away.
 */
static void irttp_run_rx_queue(struct tsap_cb *self)
{
	struct sk_buff *skb;
	int more = 0;

	IRDA_DEBUG(2, "%s() send=%d,avail=%d,remote=%d\n", __func__,
		   self->send_credit, self->avail_credit, self->remote_credit);

	/* Get exclusive access to the rx queue, otherwise don't touch it */
	if (irda_lock(&self->rx_queue_lock) == FALSE)
		return;

	/*
	 *  Reassemble all frames in receive queue and deliver them
	 */
	while (!self->rx_sdu_busy && (skb = skb_dequeue(&self->rx_queue))) {
		/* This bit will tell us if it's the last fragment or not */
		more = skb->data[0] & 0x80;

		/* Remove TTP header */
		skb_pull(skb, TTP_HEADER);

		/* Add the length of the remaining data */
		self->rx_sdu_size += skb->len;

		/*
		 * If SAR is disabled, or user has requested no reassembly
		 * of received fragments then we just deliver them
		 * immediately. This can be requested by clients that
		 * implements byte streams without any message boundaries
		 */
		if (self->rx_max_sdu_size == TTP_SAR_DISABLE) {
			irttp_do_data_indication(self, skb);
			self->rx_sdu_size = 0;

			continue;
		}

		/* Check if this is a fragment, and not the last fragment */
		if (more) {
			/*
			 *  Queue the fragment if we still are within the
			 *  limits of the maximum size of the rx_sdu
			 */
			if (self->rx_sdu_size <= self->rx_max_sdu_size) {
				IRDA_DEBUG(4, "%s(), queueing frag\n",
					   __func__);
				skb_queue_tail(&self->rx_fragments, skb);
			} else {
				/* Free the part of the SDU that is too big */
				dev_kfree_skb(skb);
			}
			continue;
		}
		/*
		 *  This is the last fragment, so time to reassemble!
		 */
		if ((self->rx_sdu_size <= self->rx_max_sdu_size) ||
		    (self->rx_max_sdu_size == TTP_SAR_UNBOUND))
		{
			/*
			 * A little optimizing. Only queue the fragment if
			 * there are other fragments. Since if this is the
			 * last and only fragment, there is no need to
			 * reassemble :-)
			 */
			if (!skb_queue_empty(&self->rx_fragments)) {
				skb_queue_tail(&self->rx_fragments,
					       skb);

				skb = irttp_reassemble_skb(self);
			}

			/* Now we can deliver the reassembled skb */
			irttp_do_data_indication(self, skb);
		} else {
			IRDA_DEBUG(1, "%s(), Truncated frame\n", __func__);

			/* Free the part of the SDU that is too big */
			dev_kfree_skb(skb);

			/* Deliver only the valid but truncated part of SDU */
			skb = irttp_reassemble_skb(self);

			irttp_do_data_indication(self, skb);
		}
		self->rx_sdu_size = 0;
	}

	/*
	 * It's not trivial to keep track of how many credits are available
	 * by incrementing at each packet, because delivery may fail
	 * (irttp_do_data_indication() may requeue the frame) and because
	 * we need to take care of fragmentation.
	 * We want the other side to send up to initial_credit packets.
	 * We have some frames in our queues, and we have already allowed it
	 * to send remote_credit.
	 * No need to spinlock, write is atomic and self correcting...
	 * Jean II
	 */
	self->avail_credit = (self->initial_credit -
			      (self->remote_credit +
			       skb_queue_len(&self->rx_queue) +
			       skb_queue_len(&self->rx_fragments)));

	/* Do we have too much credits to send to peer ? */
	if ((self->remote_credit <= TTP_RX_MIN_CREDIT) &&
	    (self->avail_credit > 0)) {
		/* Send explicit credit frame */
		irttp_give_credit(self);
		/* Note : do *NOT* check if tx_queue is non-empty, that
		 * will produce deadlocks. I repeat : send a credit frame
		 * even if we have something to send in our Tx queue.
		 * If we have credits, it means that our Tx queue is blocked.
		 *
		 * Let's suppose the peer can't keep up with our Tx. He will
		 * flow control us by not sending us any credits, and we
		 * will stop Tx and start accumulating credits here.
		 * Up to the point where the peer will stop its Tx queue,
		 * for lack of credits.
		 * Let's assume the peer application is single threaded.
		 * It will block on Tx and never consume any Rx buffer.
		 * Deadlock. Guaranteed. - Jean II
		 */
	}

	/* Reset lock */
	self->rx_queue_lock = 0;
}

#ifdef CONFIG_PROC_FS
struct irttp_iter_state {
	int id;
};

static void *irttp_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct irttp_iter_state *iter = seq->private;
	struct tsap_cb *self;

	/* Protect our access to the tsap list */
	spin_lock_irq(&irttp->tsaps->hb_spinlock);
	iter->id = 0;

	for (self = (struct tsap_cb *) hashbin_get_first(irttp->tsaps);
	     self != NULL;
	     self = (struct tsap_cb *) hashbin_get_next(irttp->tsaps)) {
		if (iter->id == *pos)
			break;
		++iter->id;
	}

	return self;
}

static void *irttp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct irttp_iter_state *iter = seq->private;

	++*pos;
	++iter->id;
	return (void *) hashbin_get_next(irttp->tsaps);
}

static void irttp_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_irq(&irttp->tsaps->hb_spinlock);
}

static int irttp_seq_show(struct seq_file *seq, void *v)
{
	const struct irttp_iter_state *iter = seq->private;
	const struct tsap_cb *self = v;

	seq_printf(seq, "TSAP %d, ", iter->id);
	seq_printf(seq, "stsap_sel: %02x, ",
		   self->stsap_sel);
	seq_printf(seq, "dtsap_sel: %02x\n",
		   self->dtsap_sel);
	seq_printf(seq, "  connected: %s, ",
		   self->connected? "TRUE":"FALSE");
	seq_printf(seq, "avail credit: %d, ",
		   self->avail_credit);
	seq_printf(seq, "remote credit: %d, ",
		   self->remote_credit);
	seq_printf(seq, "send credit: %d\n",
		   self->send_credit);
	seq_printf(seq, "  tx packets: %lu, ",
		   self->stats.tx_packets);
	seq_printf(seq, "rx packets: %lu, ",
		   self->stats.rx_packets);
	seq_printf(seq, "tx_queue len: %u ",
		   skb_queue_len(&self->tx_queue));
	seq_printf(seq, "rx_queue len: %u\n",
		   skb_queue_len(&self->rx_queue));
	seq_printf(seq, "  tx_sdu_busy: %s, ",
		   self->tx_sdu_busy? "TRUE":"FALSE");
	seq_printf(seq, "rx_sdu_busy: %s\n",
		   self->rx_sdu_busy? "TRUE":"FALSE");
	seq_printf(seq, "  max_seg_size: %u, ",
		   self->max_seg_size);
	seq_printf(seq, "tx_max_sdu_size: %u, ",
		   self->tx_max_sdu_size);
	seq_printf(seq, "rx_max_sdu_size: %u\n",
		   self->rx_max_sdu_size);

	seq_printf(seq, "  Used by (%s)\n\n",
		   self->notify.name);
	return 0;
}

static const struct seq_operations irttp_seq_ops = {
	.start  = irttp_seq_start,
	.next   = irttp_seq_next,
	.stop   = irttp_seq_stop,
	.show   = irttp_seq_show,
};

static int irttp_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &irttp_seq_ops,
			sizeof(struct irttp_iter_state));
}

const struct file_operations irttp_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = irttp_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release_private,
};

#endif /* PROC_FS */
