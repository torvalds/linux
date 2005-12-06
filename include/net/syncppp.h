/*
 * Defines for synchronous PPP/Cisco link level subroutines.
 *
 * Copyright (C) 1994 Cronyx Ltd.
 * Author: Serge Vakulenko, <vak@zebub.msk.su>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organizations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Version 1.7, Wed Jun  7 22:12:02 MSD 1995
 *
 *
 *
 */

#ifndef _SYNCPPP_H_
#define _SYNCPPP_H_ 1

#ifdef __KERNEL__
struct slcp {
	u16	state;          /* state machine */
	u32	magic;          /* local magic number */
	u_char	echoid;         /* id of last keepalive echo request */
	u_char	confid;         /* id of last configuration request */
};

struct sipcp {
	u16	state;          /* state machine */
	u_char  confid;         /* id of last configuration request */
};

struct sppp 
{
	struct sppp *	pp_next;	/* next interface in keepalive list */
	u32		pp_flags;	/* use Cisco protocol instead of PPP */
	u16		pp_alivecnt;	/* keepalive packets counter */
	u16		pp_loopcnt;	/* loopback detection counter */
	u32		pp_seq;		/* local sequence number */
	u32		pp_rseq;	/* remote sequence number */
	struct slcp	lcp;		/* LCP params */
	struct sipcp	ipcp;		/* IPCP params */
	u32		ibytes,obytes;	/* Bytes in/out */
	u32		ipkts,opkts;	/* Packets in/out */
	struct timer_list	pp_timer;
	struct net_device	*pp_if;
	char		pp_link_state;	/* Link status */
	spinlock_t      lock;
};

struct ppp_device
{	
	struct net_device *dev;	/* Network device pointer */
	struct sppp sppp;	/* Synchronous PPP */
};

static inline struct sppp *sppp_of(struct net_device *dev) 
{
	struct ppp_device **ppp = dev->priv;
	BUG_ON((*ppp)->dev != dev);
	return &(*ppp)->sppp;
}

#define PP_KEEPALIVE    0x01    /* use keepalive protocol */
#define PP_CISCO        0x02    /* use Cisco protocol instead of PPP */
#define PP_TIMO         0x04    /* cp_timeout routine active */
#define PP_DEBUG	0x08

#define PPP_MTU          1500    /* max. transmit unit */

#define LCP_STATE_CLOSED        0       /* LCP state: closed (conf-req sent) */
#define LCP_STATE_ACK_RCVD      1       /* LCP state: conf-ack received */
#define LCP_STATE_ACK_SENT      2       /* LCP state: conf-ack sent */
#define LCP_STATE_OPENED        3       /* LCP state: opened */

#define IPCP_STATE_CLOSED       0       /* IPCP state: closed (conf-req sent) */
#define IPCP_STATE_ACK_RCVD     1       /* IPCP state: conf-ack received */
#define IPCP_STATE_ACK_SENT     2       /* IPCP state: conf-ack sent */
#define IPCP_STATE_OPENED       3       /* IPCP state: opened */

#define SPPP_LINK_DOWN		0	/* link down - no keepalive */
#define SPPP_LINK_UP		1	/* link is up - keepalive ok */

void sppp_attach (struct ppp_device *pd);
void sppp_detach (struct net_device *dev);
int sppp_do_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd);
struct sk_buff *sppp_dequeue (struct net_device *dev);
int sppp_isempty (struct net_device *dev);
void sppp_flush (struct net_device *dev);
int sppp_open (struct net_device *dev);
int sppp_reopen (struct net_device *dev);
int sppp_close (struct net_device *dev);
#endif

#define SPPPIOCCISCO	(SIOCDEVPRIVATE)
#define SPPPIOCPPP	(SIOCDEVPRIVATE+1)
#define SPPPIOCDEBUG	(SIOCDEVPRIVATE+2)
#define SPPPIOCSFLAGS	(SIOCDEVPRIVATE+3)
#define SPPPIOCGFLAGS	(SIOCDEVPRIVATE+4)

#endif /* _SYNCPPP_H_ */
