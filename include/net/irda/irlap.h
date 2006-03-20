/*********************************************************************
 *                
 * Filename:      irlap.h
 * Version:       0.8
 * Description:   An IrDA LAP driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Fri Dec 10 13:21:17 1999
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

#ifndef IRLAP_H
#define IRLAP_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/timer.h>

#include <net/irda/irqueue.h>		/* irda_queue_t */
#include <net/irda/qos.h>		/* struct qos_info */
#include <net/irda/discovery.h>		/* discovery_t */
#include <net/irda/irlap_event.h>	/* IRLAP_STATE, ... */
#include <net/irda/irmod.h>		/* struct notify_t */

#define CONFIG_IRDA_DYNAMIC_WINDOW 1

#define LAP_RELIABLE   1
#define LAP_UNRELIABLE 0

#define LAP_ADDR_HEADER 1  /* IrLAP Address Header */
#define LAP_CTRL_HEADER 1  /* IrLAP Control Header */

/* May be different when we get VFIR */
#define LAP_MAX_HEADER (LAP_ADDR_HEADER + LAP_CTRL_HEADER)

/* Each IrDA device gets a random 32 bits IRLAP device address */
#define LAP_ALEN 4

#define BROADCAST  0xffffffff /* Broadcast device address */
#define CBROADCAST 0xfe       /* Connection broadcast address */
#define XID_FORMAT 0x01       /* Discovery XID format */

/* Nobody seems to use this constant. */
#define LAP_WINDOW_SIZE 8
/* We keep the LAP queue very small to minimise the amount of buffering.
 * this improve latency and reduce resource consumption.
 * This work only because we have synchronous refilling of IrLAP through
 * the flow control mechanism (via scheduler and IrTTP).
 * 2 buffers is the minimum we can work with, one that we send while polling
 * IrTTP, and another to know that we should not send the pf bit.
 * Jean II */
#define LAP_HIGH_THRESHOLD     2
/* Some rare non TTP clients don't implement flow control, and
 * so don't comply with the above limit (and neither with this one).
 * For IAP and management, it doesn't matter, because they never transmit much.
 *.For IrLPT, this should be fixed.
 * - Jean II */
#define LAP_MAX_QUEUE 10
/* Please note that all IrDA management frames (LMP/TTP conn req/disc and
 * IAS queries) fall in the second category and are sent to LAP even if TTP
 * is stopped. This means that those frames will wait only a maximum of
 * two (2) data frames before beeing sent on the "wire", which speed up
 * new socket setup when the link is saturated.
 * Same story for two sockets competing for the medium : if one saturates
 * the LAP, when the other want to transmit it only has to wait for
 * maximum three (3) packets (2 + one scheduling), which improve performance
 * of delay sensitive applications.
 * Jean II */

#define NR_EXPECTED     1
#define NR_UNEXPECTED   0
#define NR_INVALID     -1

#define NS_EXPECTED     1
#define NS_UNEXPECTED   0
#define NS_INVALID     -1

/*
 *  Meta information passed within the IrLAP state machine
 */
struct irlap_info {
	__u8 caddr;   /* Connection address */
	__u8 control; /* Frame type */
        __u8 cmd;

	__u32 saddr;
	__u32 daddr;
	
	int pf;        /* Poll/final bit set */

	__u8  nr;      /* Sequence number of next frame expected */
	__u8  ns;      /* Sequence number of frame sent */

	int  S;        /* Number of slots */
	int  slot;     /* Random chosen slot */
	int  s;        /* Current slot */

	discovery_t *discovery; /* Discovery information */
};

/* Main structure of IrLAP */
struct irlap_cb {
	irda_queue_t q;     /* Must be first */
	magic_t magic;

	/* Device we are attached to */
	struct net_device  *netdev;
	char		hw_name[2*IFNAMSIZ + 1];

	/* Connection state */
	volatile IRLAP_STATE state;       /* Current state */

	/* Timers used by IrLAP */
	struct timer_list query_timer;
	struct timer_list slot_timer;
	struct timer_list discovery_timer;
	struct timer_list final_timer;
	struct timer_list poll_timer;
	struct timer_list wd_timer;
	struct timer_list backoff_timer;

	/* Media busy stuff */
	struct timer_list media_busy_timer;
	int media_busy;

	/* Timeouts which will be different with different turn time */
	int slot_timeout;
	int poll_timeout;
	int final_timeout;
	int wd_timeout;

	struct sk_buff_head txq;  /* Frames to be transmitted */
	struct sk_buff_head txq_ultra;

 	__u8    caddr;        /* Connection address */
	__u32   saddr;        /* Source device address */
	__u32   daddr;        /* Destination device address */

	int     retry_count;  /* Times tried to establish connection */
	int     add_wait;     /* True if we are waiting for frame */

	__u8    connect_pending;
	__u8    disconnect_pending;

	/*  To send a faster RR if tx queue empty */
#ifdef CONFIG_IRDA_FAST_RR
	int     fast_RR_timeout;
	int     fast_RR;      
#endif /* CONFIG_IRDA_FAST_RR */
	
	int N1; /* N1 * F-timer = Negitiated link disconnect warning threshold */
	int N2; /* N2 * F-timer = Negitiated link disconnect time */
	int N3; /* Connection retry count */

	int     local_busy;
	int     remote_busy;
	int     xmitflag;

	__u8    vs;            /* Next frame to be sent */
	__u8    vr;            /* Next frame to be received */
	__u8    va;            /* Last frame acked */
 	int     window;        /* Nr of I-frames allowed to send */
	int     window_size;   /* Current negotiated window size */

#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
	__u32   line_capacity; /* Number of bytes allowed to send */
	__u32   bytes_left;    /* Number of bytes still allowed to transmit */
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */

	struct sk_buff_head wx_list;

	__u8    ack_required;
	
	/* XID parameters */
 	__u8    S;           /* Number of slots */
	__u8    slot;        /* Random chosen slot */
 	__u8    s;           /* Current slot */
	int     frame_sent;  /* Have we sent reply? */

	hashbin_t   *discovery_log;
 	discovery_t *discovery_cmd;

	__u32 speed;		/* Link speed */

	struct qos_info  qos_tx;   /* QoS requested by peer */
	struct qos_info  qos_rx;   /* QoS requested by self */
	struct qos_info *qos_dev;  /* QoS supported by device */

	notify_t notify; /* Callbacks to IrLMP */

	int    mtt_required;  /* Minumum turnaround time required */
	int    xbofs_delay;   /* Nr of XBOF's used to MTT */
	int    bofs_count;    /* Negotiated extra BOFs */
	int    next_bofs;     /* Negotiated extra BOFs after next frame */
};

/* 
 *  Function prototypes 
 */
int irlap_init(void);
void irlap_cleanup(void);

struct irlap_cb *irlap_open(struct net_device *dev, struct qos_info *qos,
			    const char *hw_name);
void irlap_close(struct irlap_cb *self);

void irlap_connect_request(struct irlap_cb *self, __u32 daddr, 
			   struct qos_info *qos, int sniff);
void irlap_connect_response(struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_indication(struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_confirm(struct irlap_cb *, struct sk_buff *skb);

void irlap_data_indication(struct irlap_cb *, struct sk_buff *, int unreliable);
void irlap_data_request(struct irlap_cb *, struct sk_buff *, int unreliable);

#ifdef CONFIG_IRDA_ULTRA
void irlap_unitdata_request(struct irlap_cb *, struct sk_buff *);
void irlap_unitdata_indication(struct irlap_cb *, struct sk_buff *);
#endif /* CONFIG_IRDA_ULTRA */

void irlap_disconnect_request(struct irlap_cb *);
void irlap_disconnect_indication(struct irlap_cb *, LAP_REASON reason);

void irlap_status_indication(struct irlap_cb *, int quality_of_link);

void irlap_test_request(__u8 *info, int len);

void irlap_discovery_request(struct irlap_cb *, discovery_t *discovery);
void irlap_discovery_confirm(struct irlap_cb *, hashbin_t *discovery_log);
void irlap_discovery_indication(struct irlap_cb *, discovery_t *discovery);

void irlap_reset_indication(struct irlap_cb *self);
void irlap_reset_confirm(void);

void irlap_update_nr_received(struct irlap_cb *, int nr);
int irlap_validate_nr_received(struct irlap_cb *, int nr);
int irlap_validate_ns_received(struct irlap_cb *, int ns);

int  irlap_generate_rand_time_slot(int S, int s);
void irlap_initiate_connection_state(struct irlap_cb *);
void irlap_flush_all_queues(struct irlap_cb *);
void irlap_wait_min_turn_around(struct irlap_cb *, struct qos_info *);

void irlap_apply_default_connection_parameters(struct irlap_cb *self);
void irlap_apply_connection_parameters(struct irlap_cb *self, int now);

#define IRLAP_GET_HEADER_SIZE(self) (LAP_MAX_HEADER)
#define IRLAP_GET_TX_QUEUE_LEN(self) skb_queue_len(&self->txq)

/* Return TRUE if the node is in primary mode (i.e. master)
 * - Jean II */
static inline int irlap_is_primary(struct irlap_cb *self)
{
	int ret;
	switch(self->state) {
	case LAP_XMIT_P:
	case LAP_NRM_P:
		ret = 1;
		break;
	case LAP_XMIT_S:
	case LAP_NRM_S:
		ret = 0;
		break;
	default:
		ret = -1;
	}
	return(ret);
}

/* Clear a pending IrLAP disconnect. - Jean II */
static inline void irlap_clear_disconnect(struct irlap_cb *self)
{
	self->disconnect_pending = FALSE;
}

#endif
