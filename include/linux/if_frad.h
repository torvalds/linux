/*
 * DLCI/FRAD	Definitions for Frame Relay Access Devices.  DLCI devices are
 *		created for each DLCI associated with a FRAD.  The FRAD driver
 *		is not truly a network device, but the lower level device
 *		handler.  This allows other FRAD manufacturers to use the DLCI
 *		code, including its RFC1490 encapsulation alongside the current
 *		implementation for the Sangoma cards.
 *
 * Version:	@(#)if_ifrad.h	0.15	31 Mar 96
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 * Changes:
 *		0.15	Mike McLagan	changed structure defs (packed)
 *					re-arranged flags
 *					added DLCI_RET vars
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _FRAD_H_
#define _FRAD_H_

#include <linux/config.h>
#include <linux/if.h>

#if defined(CONFIG_DLCI) || defined(CONFIG_DLCI_MODULE)

/* Structures and constants associated with the DLCI device driver */

struct dlci_add
{
   char  devname[IFNAMSIZ];
   short dlci;
};

#define DLCI_GET_CONF	(SIOCDEVPRIVATE + 2)
#define DLCI_SET_CONF	(SIOCDEVPRIVATE + 3)

/* 
 * These are related to the Sangoma SDLA and should remain in order. 
 * Code within the SDLA module is based on the specifics of this 
 * structure.  Change at your own peril.
 */
struct dlci_conf {
   short flags;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd; 

/* these are part of the status read */
   short Tc_fwd;
   short Tc_bwd;
   short Tf_max;
   short Tb_max;

/* add any new fields here above is a mirror of sdla_dlci_conf */
};

#define DLCI_GET_SLAVE	(SIOCDEVPRIVATE + 4)

/* configuration flags for DLCI */
#define DLCI_IGNORE_CIR_OUT	0x0001
#define DLCI_ACCOUNT_CIR_IN	0x0002
#define DLCI_BUFFER_IF		0x0008

#define DLCI_VALID_FLAGS	0x000B

/* FRAD driver uses these to indicate what it did with packet */
#define DLCI_RET_OK		0x00
#define DLCI_RET_ERR		0x01
#define DLCI_RET_DROP		0x02

/* defines for the actual Frame Relay hardware */
#define FRAD_GET_CONF	(SIOCDEVPRIVATE)
#define FRAD_SET_CONF	(SIOCDEVPRIVATE + 1)

#define FRAD_LAST_IOCTL	FRAD_SET_CONF

/*
 * Based on the setup for the Sangoma SDLA.  If changes are 
 * necessary to this structure, a routine will need to be 
 * added to that module to copy fields.
 */
struct frad_conf 
{
   short station;
   short flags;
   short kbaud;
   short clocking;
   short mtu;
   short T391;
   short T392;
   short N391;
   short N392;
   short N393;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd;

/* Add new fields here, above is a mirror of the sdla_conf */

};

#define FRAD_STATION_CPE	0x0000
#define FRAD_STATION_NODE	0x0001

#define FRAD_TX_IGNORE_CIR	0x0001
#define FRAD_RX_ACCOUNT_CIR	0x0002
#define FRAD_DROP_ABORTED	0x0004
#define FRAD_BUFFERIF		0x0008
#define FRAD_STATS		0x0010
#define FRAD_MCI		0x0100
#define FRAD_AUTODLCI		0x8000
#define FRAD_VALID_FLAGS	0x811F

#define FRAD_CLOCK_INT		0x0001
#define FRAD_CLOCK_EXT		0x0000

#ifdef __KERNEL__

/* these are the fields of an RFC 1490 header */
struct frhdr
{
   unsigned char  control	__attribute__((packed));

   /* for IP packets, this can be the NLPID */
   unsigned char  pad		__attribute__((packed)); 

   unsigned char  NLPID		__attribute__((packed));
   unsigned char  OUI[3]	__attribute__((packed));
   unsigned short PID		__attribute__((packed));

#define IP_NLPID pad 
};

/* see RFC 1490 for the definition of the following */
#define FRAD_I_UI		0x03

#define FRAD_P_PADDING		0x00
#define FRAD_P_Q933		0x08
#define FRAD_P_SNAP		0x80
#define FRAD_P_CLNP		0x81
#define FRAD_P_IP		0xCC

struct dlci_local
{
   struct net_device_stats stats;
   struct net_device      *master;
   struct net_device      *slave;
   struct dlci_conf       config;
   int                    configured;
   struct list_head	  list;

   /* callback function */
   void              (*receive)(struct sk_buff *skb, struct net_device *);
};

struct frad_local
{
   struct net_device_stats stats;

   /* devices which this FRAD is slaved to */
   struct net_device     *master[CONFIG_DLCI_MAX];
   short             dlci[CONFIG_DLCI_MAX];

   struct frad_conf  config;
   int               configured;	/* has this device been configured */
   int               initialized;	/* mem_start, port, irq set ? */

   /* callback functions */
   int               (*activate)(struct net_device *, struct net_device *);
   int               (*deactivate)(struct net_device *, struct net_device *);
   int               (*assoc)(struct net_device *, struct net_device *);
   int               (*deassoc)(struct net_device *, struct net_device *);
   int               (*dlci_conf)(struct net_device *, struct net_device *, int get);

   /* fields that are used by the Sangoma SDLA cards */
   struct timer_list timer;
   int               type;		/* adapter type */
   int               state;		/* state of the S502/8 control latch */
   int               buffer;		/* current buffer for S508 firmware */
};

#endif /* __KERNEL__ */

#endif /* CONFIG_DLCI || CONFIG_DLCI_MODULE */

#ifdef __KERNEL__
extern void dlci_ioctl_set(int (*hook)(unsigned int, void __user *));
#endif

#endif
