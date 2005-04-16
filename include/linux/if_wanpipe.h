/*****************************************************************************
* if_wanpipe.h	Header file for the Sangoma AF_WANPIPE Socket 	
*
* Author: 	Nenad Corbic 	
*
* Copyright:	(c) 2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
*
* Jan 28, 2000	Nenad Corbic 	Initial Version
*
*****************************************************************************/

#ifndef __LINUX_IF_WAN_PACKET_H
#define __LINUX_IF_WAN_PACKET_H

struct wan_sockaddr_ll
{
	unsigned short	sll_family;
	unsigned short	sll_protocol;
	int		sll_ifindex;
	unsigned short	sll_hatype;
	unsigned char	sll_pkttype;
	unsigned char	sll_halen;
	unsigned char	sll_addr[8];
	unsigned char   sll_device[14];
	unsigned char 	sll_card[14];
};

typedef struct 
{
	unsigned char free;
	unsigned char state_sk;
	int rcvbuf;
	int sndbuf;
	int rmem;
	int wmem;
	int sk_count;
	unsigned char bound;
	char name[14];
	unsigned char d_state;
	unsigned char svc;
	unsigned short lcn;
	unsigned char mbox;
	unsigned char cmd_busy;
	unsigned char command;
	unsigned poll;
	unsigned poll_cnt;
	int rblock;	
} wan_debug_hdr_t;

#define MAX_NUM_DEBUG  10
#define X25_PROT       0x16
#define PVC_PROT       0x17	

typedef struct
{
	wan_debug_hdr_t debug[MAX_NUM_DEBUG];
}wan_debug_t;

#define	SIOC_WANPIPE_GET_CALL_DATA	(SIOCPROTOPRIVATE + 0)
#define	SIOC_WANPIPE_SET_CALL_DATA	(SIOCPROTOPRIVATE + 1)
#define SIOC_WANPIPE_ACCEPT_CALL	(SIOCPROTOPRIVATE + 2)
#define SIOC_WANPIPE_CLEAR_CALL	        (SIOCPROTOPRIVATE + 3)
#define SIOC_WANPIPE_RESET_CALL	        (SIOCPROTOPRIVATE + 4)
#define SIOC_WANPIPE_DEBUG	        (SIOCPROTOPRIVATE + 5)
#define SIOC_WANPIPE_SET_NONBLOCK	(SIOCPROTOPRIVATE + 6)
#define SIOC_WANPIPE_CHECK_TX		(SIOCPROTOPRIVATE + 7)
#define SIOC_WANPIPE_SOCK_STATE		(SIOCPROTOPRIVATE + 8)

/* Packet types */

#define WAN_PACKET_HOST		0		/* To us		*/
#define WAN_PACKET_BROADCAST	1		/* To all		*/
#define WAN_PACKET_MULTICAST	2		/* To group		*/
#define WAN_PACKET_OTHERHOST	3		/* To someone else 	*/
#define WAN_PACKET_OUTGOING		4		/* Outgoing of any type */
/* These ones are invisible by user level */
#define WAN_PACKET_LOOPBACK		5		/* MC/BRD frame looped back */
#define WAN_PACKET_FASTROUTE	6		/* Fastrouted frame	*/


/* X25 specific */
#define WAN_PACKET_DATA 	7
#define WAN_PACKET_CMD 		8
#define WAN_PACKET_ASYNC	9
#define WAN_PACKET_ERR	       10

/* Packet socket options */

#define WAN_PACKET_ADD_MEMBERSHIP		1
#define WAN_PACKET_DROP_MEMBERSHIP		2

#define WAN_PACKET_MR_MULTICAST	0
#define WAN_PACKET_MR_PROMISC	1
#define WAN_PACKET_MR_ALLMULTI	2

#ifdef __KERNEL__

/* Private wanpipe socket structures. */
struct wanpipe_opt
{
	void   *mbox;		/* Mail box  */
	void   *card; 		/* Card bouded to */
	struct net_device *dev;	/* Bounded device */
	unsigned short lcn;	/* Binded LCN */
	unsigned char  svc;	/* 0=pvc, 1=svc */
	unsigned char  timer;   /* flag for delayed transmit*/	
	struct timer_list tx_timer;
	unsigned poll_cnt;
	unsigned char force;	/* Used to force sock release */
	atomic_t packet_sent;   
	unsigned short num; 
};

#define wp_sk(__sk) ((struct wanpipe_opt *)(__sk)->sk_protinfo)

#endif

#endif
