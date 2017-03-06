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

#include <uapi/linux/if_frad.h>


#if defined(CONFIG_DLCI) || defined(CONFIG_DLCI_MODULE)

/* these are the fields of an RFC 1490 header */
struct frhdr
{
   unsigned char  control;

   /* for IP packets, this can be the NLPID */
   unsigned char  pad;

   unsigned char  NLPID;
   unsigned char  OUI[3];
   __be16 PID;

#define IP_NLPID pad 
} __packed;

/* see RFC 1490 for the definition of the following */
#define FRAD_I_UI		0x03

#define FRAD_P_PADDING		0x00
#define FRAD_P_Q933		0x08
#define FRAD_P_SNAP		0x80
#define FRAD_P_CLNP		0x81
#define FRAD_P_IP		0xCC

struct dlci_local
{
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

#endif /* CONFIG_DLCI || CONFIG_DLCI_MODULE */

extern void dlci_ioctl_set(int (*hook)(unsigned int, void __user *));

#endif
