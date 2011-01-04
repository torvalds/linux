/*********************************************************************
 *                
 * Filename:      irlan_common.h
 * Version:       0.8
 * Description:   IrDA LAN access layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sun Oct 31 19:41:24 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
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

#ifndef IRLAN_H
#define IRLAN_H

#include <asm/param.h>  /* for HZ */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/irda/irttp.h>

#define IRLAN_MTU        1518
#define IRLAN_TIMEOUT    10*HZ /* 10 seconds */

/* Command packet types */
#define CMD_GET_PROVIDER_INFO   0
#define CMD_GET_MEDIA_CHAR      1
#define CMD_OPEN_DATA_CHANNEL   2
#define CMD_CLOSE_DATA_CHAN     3
#define CMD_RECONNECT_DATA_CHAN 4
#define CMD_FILTER_OPERATION    5

/* Some responses */
#define RSP_SUCCESS                 0
#define RSP_INSUFFICIENT_RESOURCES  1
#define RSP_INVALID_COMMAND_FORMAT  2
#define RSP_COMMAND_NOT_SUPPORTED   3
#define RSP_PARAM_NOT_SUPPORTED     4
#define RSP_VALUE_NOT_SUPPORTED     5
#define RSP_NOT_OPEN                6
#define RSP_AUTHENTICATION_REQUIRED 7
#define RSP_INVALID_PASSWORD        8
#define RSP_PROTOCOL_ERROR          9
#define RSP_ASYNCHRONOUS_ERROR    255

/* Media types */
#define MEDIA_802_3 1
#define MEDIA_802_5 2

/* Filter parameters */
#define DATA_CHAN   1
#define FILTER_TYPE 2
#define FILTER_MODE 3

/* Filter types */
#define IRLAN_DIRECTED   0x01
#define IRLAN_FUNCTIONAL 0x02
#define IRLAN_GROUP      0x04
#define IRLAN_MAC_FRAME  0x08
#define IRLAN_MULTICAST  0x10
#define IRLAN_BROADCAST  0x20
#define IRLAN_IPX_SOCKET 0x40

/* Filter modes */
#define ALL     1
#define FILTER  2
#define NONE    3

/* Filter operations */
#define GET     1
#define CLEAR   2
#define ADD     3
#define REMOVE  4
#define DYNAMIC 5

/* Access types */
#define ACCESS_DIRECT  1
#define ACCESS_PEER    2
#define ACCESS_HOSTED  3

#define IRLAN_BYTE   0
#define IRLAN_SHORT  1
#define IRLAN_ARRAY  2

/* IrLAN sits on top if IrTTP */
#define IRLAN_MAX_HEADER (TTP_HEADER+LMP_HEADER)
/* 1 byte for the command code and 1 byte for the parameter count */
#define IRLAN_CMD_HEADER 2

#define IRLAN_STRING_PARAMETER_LEN(name, value) (1 + strlen((name)) + 2 \
						+ strlen ((value)))
#define IRLAN_BYTE_PARAMETER_LEN(name)          (1 + strlen((name)) + 2 + 1)
#define IRLAN_SHORT_PARAMETER_LEN(name)         (1 + strlen((name)) + 2 + 2)

/*
 *  IrLAN client
 */
struct irlan_client_cb {
	int state;

	int open_retries;

	struct tsap_cb *tsap_ctrl;
	__u32 max_sdu_size;
	__u8  max_header_size;
	
	int access_type;         /* Access type of provider */
	__u8 reconnect_key[255];
	__u8 key_len;
	
	__u16 recv_arb_val;
	__u16 max_frame;
	int filter_type;

	int unicast_open;
	int broadcast_open;

	int tx_busy;
	struct sk_buff_head txq; /* Transmit control queue */

	struct iriap_cb *iriap;

	struct timer_list kick_timer;
};

/*
 * IrLAN provider
 */
struct irlan_provider_cb {
	int state;
	
	struct tsap_cb *tsap_ctrl;
	__u32 max_sdu_size;
	__u8  max_header_size;

	/*
	 *  Store some values here which are used by the provider to parse
	 *  the filter operations
	 */
	int data_chan;
	int filter_type;
	int filter_mode;
	int filter_operation;
	int filter_entry;
	int access_type;     /* Access type */
	__u16 send_arb_val;

	__u8 mac_address[6]; /* Generated MAC address for peer device */
};

/*
 *  IrLAN control block
 */
struct irlan_cb {
	int    magic;
	struct list_head  dev_list;
	struct net_device *dev;        /* Ethernet device structure*/

	__u32 saddr;               /* Source device address */
	__u32 daddr;               /* Destination device address */
	int disconnect_reason;     /* Why we got disconnected */
	
	int media;                 /* Media type */
	__u8 version[2];           /* IrLAN version */
	
	struct tsap_cb *tsap_data; /* Data TSAP */

	int  use_udata;            /* Use Unit Data transfers */

	__u8 stsap_sel_data;       /* Source data TSAP selector */
	__u8 dtsap_sel_data;       /* Destination data TSAP selector */
	__u8 dtsap_sel_ctrl;       /* Destination ctrl TSAP selector */

	struct irlan_client_cb   client;   /* Client specific fields */
	struct irlan_provider_cb provider; /* Provider specific fields */

	__u32 max_sdu_size;
	__u8  max_header_size;
	
	wait_queue_head_t open_wait;
	struct timer_list watchdog_timer;
};

void irlan_close(struct irlan_cb *self);
void irlan_close_tsaps(struct irlan_cb *self);

int  irlan_register_netdev(struct irlan_cb *self);
void irlan_ias_register(struct irlan_cb *self, __u8 tsap_sel);
void irlan_start_watchdog_timer(struct irlan_cb *self, int timeout);

void irlan_open_data_tsap(struct irlan_cb *self);

int irlan_run_ctrl_tx_queue(struct irlan_cb *self);

struct irlan_cb *irlan_get_any(void);
void irlan_get_provider_info(struct irlan_cb *self);
void irlan_get_media_char(struct irlan_cb *self);
void irlan_open_data_channel(struct irlan_cb *self);
void irlan_close_data_channel(struct irlan_cb *self);
void irlan_set_multicast_filter(struct irlan_cb *self, int status);
void irlan_set_broadcast_filter(struct irlan_cb *self, int status);

int irlan_insert_byte_param(struct sk_buff *skb, char *param, __u8 value);
int irlan_insert_short_param(struct sk_buff *skb, char *param, __u16 value);
int irlan_insert_string_param(struct sk_buff *skb, char *param, char *value);
int irlan_insert_array_param(struct sk_buff *skb, char *name, __u8 *value, 
			     __u16 value_len);

int irlan_extract_param(__u8 *buf, char *name, char *value, __u16 *len);

#endif


