/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999-2005 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef __HDLC_H
#define __HDLC_H

#define GENERIC_HDLC_VERSION 4	/* For synchronization with sethdlc utility */

#define CLOCK_DEFAULT   0	/* Default setting */
#define CLOCK_EXT	1	/* External TX and RX clock - DTE */
#define CLOCK_INT	2	/* Internal TX and RX clock - DCE */
#define CLOCK_TXINT	3	/* Internal TX and external RX clock */
#define CLOCK_TXFROMRX	4	/* TX clock derived from external RX clock */


#define ENCODING_DEFAULT	0 /* Default setting */
#define ENCODING_NRZ		1
#define ENCODING_NRZI		2
#define ENCODING_FM_MARK	3
#define ENCODING_FM_SPACE	4
#define ENCODING_MANCHESTER	5


#define PARITY_DEFAULT		0 /* Default setting */
#define PARITY_NONE		1 /* No parity */
#define PARITY_CRC16_PR0	2 /* CRC16, initial value 0x0000 */
#define PARITY_CRC16_PR1	3 /* CRC16, initial value 0xFFFF */
#define PARITY_CRC16_PR0_CCITT	4 /* CRC16, initial 0x0000, ITU-T version */
#define PARITY_CRC16_PR1_CCITT	5 /* CRC16, initial 0xFFFF, ITU-T version */
#define PARITY_CRC32_PR0_CCITT	6 /* CRC32, initial value 0x00000000 */
#define PARITY_CRC32_PR1_CCITT	7 /* CRC32, initial value 0xFFFFFFFF */

#define LMI_DEFAULT		0 /* Default setting */
#define LMI_NONE		1 /* No LMI, all PVCs are static */
#define LMI_ANSI		2 /* ANSI Annex D */
#define LMI_CCITT		3 /* ITU-T Annex A */
#define LMI_CISCO		4 /* The "original" LMI, aka Gang of Four */

#define HDLC_MAX_MTU 1500	/* Ethernet 1500 bytes */
#define HDLC_MAX_MRU (HDLC_MAX_MTU + 10 + 14 + 4) /* for ETH+VLAN over FR */


#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/syncppp.h>
#include <linux/hdlc/ioctl.h>


typedef struct {		/* Used in Cisco and PPP mode */
	u8 address;
	u8 control;
	u16 protocol;
}__attribute__ ((packed)) hdlc_header;



typedef struct {
	u32 type;		/* code */
	u32 par1;
	u32 par2;
	u16 rel;		/* reliability */
	u32 time;
}__attribute__ ((packed)) cisco_packet;
#define	CISCO_PACKET_LEN	18
#define	CISCO_BIG_PACKET_LEN	20



typedef struct pvc_device_struct {
	struct net_device *master;
	struct net_device *main;
	struct net_device *ether; /* bridged Ethernet interface */
	struct pvc_device_struct *next;	/* Sorted in ascending DLCI order */
	int dlci;
	int open_count;

	struct {
		unsigned int new: 1;
		unsigned int active: 1;
		unsigned int exist: 1;
		unsigned int deleted: 1;
		unsigned int fecn: 1;
		unsigned int becn: 1;
		unsigned int bandwidth;	/* Cisco LMI reporting only */
	}state;
}pvc_device;



typedef struct hdlc_device_struct {
	/* To be initialized by hardware driver */
	struct net_device_stats stats;

	/* used by HDLC layer to take control over HDLC device from hw driver*/
	int (*attach)(struct net_device *dev,
		      unsigned short encoding, unsigned short parity);

	/* hardware driver must handle this instead of dev->hard_start_xmit */
	int (*xmit)(struct sk_buff *skb, struct net_device *dev);


	/* Things below are for HDLC layer internal use only */
	struct {
		int (*open)(struct net_device *dev);
		void (*close)(struct net_device *dev);

		/* if open & DCD */
		void (*start)(struct net_device *dev);
		/* if open & !DCD */
		void (*stop)(struct net_device *dev);

		void (*detach)(struct hdlc_device_struct *hdlc);
		int (*netif_rx)(struct sk_buff *skb);
		unsigned short (*type_trans)(struct sk_buff *skb,
					     struct net_device *dev);
		int id;		/* IF_PROTO_HDLC/CISCO/FR/etc. */
	}proto;

	int carrier;
	int open;
	spinlock_t state_lock;

	union {
		struct {
			fr_proto settings;
			pvc_device *first_pvc;
			int dce_pvc_count;

			struct timer_list timer;
			unsigned long last_poll;
			int reliable;
			int dce_changed;
			int request;
			int fullrep_sent;
			u32 last_errors; /* last errors bit list */
			u8 n391cnt;
			u8 txseq; /* TX sequence number */
			u8 rxseq; /* RX sequence number */
		}fr;

		struct {
			cisco_proto settings;

			struct timer_list timer;
			unsigned long last_poll;
			int up;
			int request_sent;
			u32 txseq; /* TX sequence number */
			u32 rxseq; /* RX sequence number */
		}cisco;

		struct {
			raw_hdlc_proto settings;
		}raw_hdlc;

		struct {
			struct ppp_device pppdev;
			struct ppp_device *syncppp_ptr;
			int (*old_change_mtu)(struct net_device *dev,
					      int new_mtu);
		}ppp;
	}state;
	void *priv;
}hdlc_device;



int hdlc_raw_ioctl(struct net_device *dev, struct ifreq *ifr);
int hdlc_raw_eth_ioctl(struct net_device *dev, struct ifreq *ifr);
int hdlc_cisco_ioctl(struct net_device *dev, struct ifreq *ifr);
int hdlc_ppp_ioctl(struct net_device *dev, struct ifreq *ifr);
int hdlc_fr_ioctl(struct net_device *dev, struct ifreq *ifr);
int hdlc_x25_ioctl(struct net_device *dev, struct ifreq *ifr);


/* Exported from hdlc.o */

/* Called by hardware driver when a user requests HDLC service */
int hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

/* Must be used by hardware driver on module startup/exit */
#define register_hdlc_device(dev)	register_netdev(dev)
void unregister_hdlc_device(struct net_device *dev);

struct net_device *alloc_hdlcdev(void *priv);

static __inline__ hdlc_device* dev_to_hdlc(struct net_device *dev)
{
	return netdev_priv(dev);
}


static __inline__ pvc_device* dev_to_pvc(struct net_device *dev)
{
	return (pvc_device*)dev->priv;
}


static __inline__ void debug_frame(const struct sk_buff *skb)
{
	int i;

	for (i=0; i < skb->len; i++) {
		if (i == 100) {
			printk("...\n");
			return;
		}
		printk(" %02X", skb->data[i]);
	}
	printk("\n");
}


/* Must be called by hardware driver when HDLC device is being opened */
int hdlc_open(struct net_device *dev);
/* Must be called by hardware driver when HDLC device is being closed */
void hdlc_close(struct net_device *dev);

/* May be used by hardware driver to gain control over HDLC device */
static __inline__ void hdlc_proto_detach(hdlc_device *hdlc)
{
	if (hdlc->proto.detach)
		hdlc->proto.detach(hdlc);
	hdlc->proto.detach = NULL;
}


static __inline__ struct net_device_stats *hdlc_stats(struct net_device *dev)
{
	return &dev_to_hdlc(dev)->stats;
}


static __inline__ __be16 hdlc_type_trans(struct sk_buff *skb,
					 struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	skb->mac.raw  = skb->data;
	skb->dev      = dev;

	if (hdlc->proto.type_trans)
		return hdlc->proto.type_trans(skb, dev);
	else
		return htons(ETH_P_HDLC);
}

#endif /* __KERNEL */
#endif /* __HDLC_H */
