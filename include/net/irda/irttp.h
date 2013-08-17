/*********************************************************************
 *                
 * Filename:      irttp.h
 * Version:       1.0
 * Description:   Tiny Transport Protocol (TTP) definitions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Sun Dec 12 13:09:07 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     Copyright (c) 2000-2002 Jean Tourrilhes <jt@hpl.hp.com>
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

#ifndef IRTTP_H
#define IRTTP_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>		/* struct lsap_cb */
#include <net/irda/qos.h>		/* struct qos_info */
#include <net/irda/irqueue.h>

#define TTP_MAX_CONNECTIONS    LM_MAX_CONNECTIONS
#define TTP_HEADER             1
#define TTP_MAX_HEADER         (TTP_HEADER + LMP_MAX_HEADER)
#define TTP_SAR_HEADER         5
#define TTP_PARAMETERS         0x80
#define TTP_MORE               0x80

/* Transmission queue sizes */
/* Worst case scenario, two window of data - Jean II */
#define TTP_TX_MAX_QUEUE	14
/* We need to keep at least 5 frames to make sure that we can refill
 * appropriately the LAP layer. LAP keeps only two buffers, and we need
 * to have 7 to make a full window - Jean II */
#define TTP_TX_LOW_THRESHOLD	5
/* Most clients are synchronous with respect to flow control, so we can
 * keep a low number of Tx buffers in TTP - Jean II */
#define TTP_TX_HIGH_THRESHOLD	7

/* Receive queue sizes */
/* Minimum of credit that the peer should hold.
 * If the peer has less credits than 9 frames, we will explicitly send
 * him some credits (through irttp_give_credit() and a specific frame).
 * Note that when we give credits it's likely that it won't be sent in
 * this LAP window, but in the next one. So, we make sure that the peer
 * has something to send while waiting for credits (one LAP window == 7
 * + 1 frames while he process the credits). - Jean II */
#define TTP_RX_MIN_CREDIT	8
/* This is the default maximum number of credits held by the peer, so the
 * default maximum number of frames he can send us before needing flow
 * control answer from us (this may be negociated differently at TSAP setup).
 * We want to minimise the number of times we have to explicitly send some
 * credit to the peer, hoping we can piggyback it on the return data. In
 * particular, it doesn't make sense for us to send credit more than once
 * per LAP window.
 * Moreover, giving credits has some latency, so we need strictly more than
 * a LAP window, otherwise we may already have credits in our Tx queue.
 * But on the other hand, we don't want to keep too many Rx buffer here
 * before starting to flow control the other end, so make it exactly one
 * LAP window + 1 + MIN_CREDITS. - Jean II */
#define TTP_RX_DEFAULT_CREDIT	16
/* Maximum number of credits we can allow the peer to have, and therefore
 * maximum Rx queue size.
 * Note that we try to deliver packets to the higher layer every time we
 * receive something, so in normal mode the Rx queue will never contains
 * more than one or two packets. - Jean II */
#define TTP_RX_MAX_CREDIT	21

/* What clients should use when calling ttp_open_tsap() */
#define DEFAULT_INITIAL_CREDIT	TTP_RX_DEFAULT_CREDIT

/* Some priorities for disconnect requests */
#define P_NORMAL    0
#define P_HIGH      1

#define TTP_SAR_DISABLE 0
#define TTP_SAR_UNBOUND 0xffffffff

/* Parameters */
#define TTP_MAX_SDU_SIZE 0x01

/*
 *  This structure contains all data associated with one instance of a TTP 
 *  connection.
 */
struct tsap_cb {
	irda_queue_t q;            /* Must be first */
	magic_t magic;        /* Just in case */

	__u8 stsap_sel;       /* Source TSAP */
	__u8 dtsap_sel;       /* Destination TSAP */

	struct lsap_cb *lsap; /* Corresponding LSAP to this TSAP */

	__u8 connected;       /* TSAP connected */
	 
	__u8 initial_credit;  /* Initial credit to give peer */

        int avail_credit;    /* Available credit to return to peer */
	int remote_credit;   /* Credit held by peer TTP entity */
	int send_credit;     /* Credit held by local TTP entity */
	
	struct sk_buff_head tx_queue; /* Frames to be transmitted */
	struct sk_buff_head rx_queue; /* Received frames */
	struct sk_buff_head rx_fragments;
	int tx_queue_lock;
	int rx_queue_lock;
	spinlock_t lock;

	notify_t notify;       /* Callbacks to client layer */

	struct net_device_stats stats;
	struct timer_list todo_timer; 

	__u32 max_seg_size;     /* Max data that fit into an IrLAP frame */
	__u8  max_header_size;

	int   rx_sdu_busy;     /* RxSdu.busy */
	__u32 rx_sdu_size;     /* Current size of a partially received frame */
	__u32 rx_max_sdu_size; /* Max receive user data size */

	int tx_sdu_busy;       /* TxSdu.busy */
	__u32 tx_max_sdu_size; /* Max transmit user data size */

	int close_pend;        /* Close, but disconnect_pend */
	unsigned long disconnect_pend; /* Disconnect, but still data to send */
	struct sk_buff *disconnect_skb;
};

struct irttp_cb {
	magic_t    magic;	
	hashbin_t *tsaps;
};

int  irttp_init(void);
void irttp_cleanup(void);

struct tsap_cb *irttp_open_tsap(__u8 stsap_sel, int credit, notify_t *notify);
int irttp_close_tsap(struct tsap_cb *self);

int irttp_data_request(struct tsap_cb *self, struct sk_buff *skb);
int irttp_udata_request(struct tsap_cb *self, struct sk_buff *skb);

int irttp_connect_request(struct tsap_cb *self, __u8 dtsap_sel, 
			  __u32 saddr, __u32 daddr,
			  struct qos_info *qos, __u32 max_sdu_size, 
			  struct sk_buff *userdata);
int irttp_connect_response(struct tsap_cb *self, __u32 max_sdu_size, 
			    struct sk_buff *userdata);
int irttp_disconnect_request(struct tsap_cb *self, struct sk_buff *skb,
			     int priority);
void irttp_flow_request(struct tsap_cb *self, LOCAL_FLOW flow);
struct tsap_cb *irttp_dup(struct tsap_cb *self, void *instance);

static inline __u32 irttp_get_saddr(struct tsap_cb *self)
{
	return irlmp_get_saddr(self->lsap);
}

static inline __u32 irttp_get_daddr(struct tsap_cb *self)
{
	return irlmp_get_daddr(self->lsap);
}

static inline __u32 irttp_get_max_seg_size(struct tsap_cb *self)
{
	return self->max_seg_size;
}

/* After doing a irttp_dup(), this get one of the two socket back into
 * a state where it's waiting incomming connections.
 * Note : this can be used *only* if the socket is not yet connected
 * (i.e. NO irttp_connect_response() done on this socket).
 * - Jean II */
static inline void irttp_listen(struct tsap_cb *self)
{
	irlmp_listen(self->lsap);
	self->dtsap_sel = LSAP_ANY;
}

/* Return TRUE if the node is in primary mode (i.e. master)
 * - Jean II */
static inline int irttp_is_primary(struct tsap_cb *self)
{
	if ((self == NULL) ||
	    (self->lsap == NULL) ||
	    (self->lsap->lap == NULL) ||
	    (self->lsap->lap->irlap == NULL))
		return -2;
	return irlap_is_primary(self->lsap->lap->irlap);
}

#endif /* IRTTP_H */
