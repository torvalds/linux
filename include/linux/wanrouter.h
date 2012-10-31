/*****************************************************************************
* wanrouter.h	Definitions for the WAN Multiprotocol Router Module.
*		This module provides API and common services for WAN Link
*		Drivers and is completely hardware-independent.
*
* Author: 	Nenad Corbic <ncorbic@sangoma.com>
*		Gideon Hack 	
* Additions:	Arnaldo Melo
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jul 21, 2000  Nenad Corbic	Added WAN_FT1_READY State
* Feb 24, 2000  Nenad Corbic    Added support for socket based x25api
* Jan 28, 2000  Nenad Corbic    Added support for the ASYNC protocol.
* Oct 04, 1999  Nenad Corbic 	Updated for 2.1.0 release
* Jun 02, 1999  Gideon Hack	Added support for the S514 adapter.
* May 23, 1999	Arnaldo Melo	Added local_addr to wanif_conf_t
*				WAN_DISCONNECTING state added
* Jul 20, 1998	David Fong	Added Inverse ARP options to 'wanif_conf_t'
* Jun 12, 1998	David Fong	Added Cisco HDLC support.
* Dec 16, 1997	Jaspreet Singh	Moved 'enable_IPX' and 'network_number' to
*				'wanif_conf_t'
* Dec 05, 1997	Jaspreet Singh	Added 'pap', 'chap' to 'wanif_conf_t'
*				Added 'authenticator' to 'wan_ppp_conf_t'
* Nov 06, 1997	Jaspreet Singh	Changed Router Driver version to 1.1 from 1.0
* Oct 20, 1997	Jaspreet Singh	Added 'cir','bc','be' and 'mc' to 'wanif_conf_t'
*				Added 'enable_IPX' and 'network_number' to 
*				'wan_device_t'.  Also added defines for
*				UDP PACKET TYPE, Interrupt test, critical values
*				for RACE conditions.
* Oct 05, 1997	Jaspreet Singh	Added 'dlci_num' and 'dlci[100]' to 
*				'wan_fr_conf_t' to configure a list of dlci(s)
*				for a NODE 
* Jul 07, 1997	Jaspreet Singh	Added 'ttl' to 'wandev_conf_t' & 'wan_device_t'
* May 29, 1997 	Jaspreet Singh	Added 'tx_int_enabled' to 'wan_device_t'
* May 21, 1997	Jaspreet Singh	Added 'udp_port' to 'wan_device_t'
* Apr 25, 1997  Farhan Thawar   Added 'udp_port' to 'wandev_conf_t'
* Jan 16, 1997	Gene Kozin	router_devlist made public
* Jan 02, 1997	Gene Kozin	Initial version (based on wanpipe.h).
*****************************************************************************/
#ifndef	_ROUTER_H
#define	_ROUTER_H

#include <uapi/linux/wanrouter.h>

/****** Kernel Interface ****************************************************/

#include <linux/fs.h>		/* support for device drivers */
#include <linux/proc_fs.h>	/* proc filesystem pragmatics */
#include <linux/netdevice.h>	/* support for network drivers */
#include <linux/spinlock.h>     /* Support for SMP Locking */

/*----------------------------------------------------------------------------
 * WAN device data space.
 */
struct wan_device {
	unsigned magic;			/* magic number */
	char* name;			/* -> WAN device name (ASCIIZ) */
	void* private;			/* -> driver private data */
	unsigned config_id;		/* Configuration ID */
					/****** hardware configuration ******/
	unsigned ioport;		/* adapter I/O port base #1 */
	char S514_cpu_no[1];		/* PCI CPU Number */
	unsigned char S514_slot_no;	/* PCI Slot Number */
	unsigned long maddr;		/* dual-port memory address */
	unsigned msize;			/* dual-port memory size */
	int irq;			/* interrupt request level */
	int dma;			/* DMA request level */
	unsigned bps;			/* data transfer rate */
	unsigned mtu;			/* max physical transmit unit size */
	unsigned udp_port;              /* UDP port for management */
        unsigned char ttl;		/* Time To Live for UDP security */
	unsigned enable_tx_int; 	/* Transmit Interrupt enabled or not */
	char interface;			/* RS-232/V.35, etc. */
	char clocking;			/* external/internal */
	char line_coding;		/* NRZ/NRZI/FM0/FM1, etc. */
	char station;			/* DTE/DCE, primary/secondary, etc. */
	char connection;		/* permanent/switched/on-demand */
	char signalling;		/* Signalling RS232 or V35 */
	char read_mode;			/* read mode: Polling or interrupt */
	char new_if_cnt;                /* Number of interfaces per wanpipe */ 
	char del_if_cnt;		/* Number of times del_if() gets called */
	unsigned char piggyback;        /* Piggibacking a port */
	unsigned hw_opt[4];		/* other hardware options */
					/****** status and statistics *******/
	char state;			/* device state */
	char api_status;		/* device api status */
	struct net_device_stats stats; 	/* interface statistics */
	unsigned reserved[16];		/* reserved for future use */
	unsigned long critical;		/* critical section flag */
	spinlock_t lock;                /* Support for SMP Locking */

					/****** device management methods ***/
	int (*setup) (struct wan_device *wandev, wandev_conf_t *conf);
	int (*shutdown) (struct wan_device *wandev);
	int (*update) (struct wan_device *wandev);
	int (*ioctl) (struct wan_device *wandev, unsigned cmd,
		unsigned long arg);
	int (*new_if)(struct wan_device *wandev, struct net_device *dev,
		      wanif_conf_t *conf);
	int (*del_if)(struct wan_device *wandev, struct net_device *dev);
					/****** maintained by the router ****/
	struct wan_device* next;	/* -> next device */
	struct net_device* dev;		/* list of network interfaces */
	unsigned ndev;			/* number of interfaces */
	struct proc_dir_entry *dent;	/* proc filesystem entry */
};

/* Public functions available for device drivers */
extern int register_wan_device(struct wan_device *wandev);
extern int unregister_wan_device(char *name);

/* Proc interface functions. These must not be called by the drivers! */
extern int wanrouter_proc_init(void);
extern void wanrouter_proc_cleanup(void);
extern int wanrouter_proc_add(struct wan_device *wandev);
extern int wanrouter_proc_delete(struct wan_device *wandev);
extern long wanrouter_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* Public Data */
/* list of registered devices */
extern struct wan_device *wanrouter_router_devlist;

#endif	/* _ROUTER_H */
