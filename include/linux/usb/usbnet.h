// SPDX-License-Identifier: GPL-2.0+
/*
 * USB Networking Link Interface
 *
 * Copyright (C) 2000-2005 by David Brownell <dbrownell@users.sourceforge.net>
 * Copyright (C) 2003-2005 David Hollis <dhollis@davehollis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef	__LINUX_USB_USBNET_H
#define	__LINUX_USB_USBNET_H

/* interface from usbnet core to each USB networking link we handle */
struct usbnet {
	/* housekeeping */
	struct usb_device	*udev;
	struct usb_interface	*intf;
	const struct driver_info *driver_info;
	const char		*driver_name;
	void			*driver_priv;
	wait_queue_head_t	wait;
	struct mutex		phy_mutex;
	unsigned char		suspend_count;
	unsigned char		pkt_cnt, pkt_err;
	unsigned short		rx_qlen, tx_qlen;
	unsigned		can_dma_sg:1;

	/* i/o info: pipes etc */
	unsigned		in, out;
	struct usb_host_endpoint *status;
	unsigned		maxpacket;
	struct timer_list	delay;
	const char		*padding_pkt;

	/* protocol/interface state */
	struct net_device	*net;
	int			msg_enable;
	unsigned long		data[5];
	u32			xid;
	u32			hard_mtu;	/* count any extra framing */
	size_t			rx_urb_size;	/* size for rx urbs */
	struct mii_if_info	mii;

	/* various kinds of pending driver work */
	struct sk_buff_head	rxq;
	struct sk_buff_head	txq;
	struct sk_buff_head	done;
	struct sk_buff_head	rxq_pause;
	struct urb		*interrupt;
	unsigned		interrupt_count;
	struct mutex		interrupt_mutex;
	struct usb_anchor	deferred;
	struct tasklet_struct	bh;

	struct pcpu_sw_netstats __percpu *stats64;

	struct work_struct	kevent;
	unsigned long		flags;
#		define EVENT_TX_HALT	0
#		define EVENT_RX_HALT	1
#		define EVENT_RX_MEMORY	2
#		define EVENT_STS_SPLIT	3
#		define EVENT_LINK_RESET	4
#		define EVENT_RX_PAUSED	5
#		define EVENT_DEV_ASLEEP 6
#		define EVENT_DEV_OPEN	7
#		define EVENT_DEVICE_REPORT_IDLE	8
#		define EVENT_NO_RUNTIME_PM	9
#		define EVENT_RX_KILL	10
#		define EVENT_LINK_CHANGE	11
#		define EVENT_SET_RX_MODE	12
#		define EVENT_NO_IP_ALIGN	13
	u32			rx_speed;	/* in bps - NOT Mbps */
	u32			tx_speed;	/* in bps - NOT Mbps */
};

static inline struct usb_driver *driver_of(struct usb_interface *intf)
{
	return to_usb_driver(intf->dev.driver);
}

/* interface from the device/framing level "minidriver" to core */
struct driver_info {
	char		*description;

	int		flags;
/* framing is CDC Ethernet, not writing ZLPs (hw issues), or optionally: */
#define FLAG_FRAMING_NC	0x0001		/* guard against device dropouts */
#define FLAG_FRAMING_GL	0x0002		/* genelink batches packets */
#define FLAG_FRAMING_Z	0x0004		/* zaurus adds a trailer */
#define FLAG_FRAMING_RN	0x0008		/* RNDIS batches, plus huge header */

#define FLAG_NO_SETINT	0x0010		/* device can't set_interface() */
#define FLAG_ETHER	0x0020		/* maybe use "eth%d" names */

#define FLAG_FRAMING_AX 0x0040		/* AX88772/178 packets */
#define FLAG_WLAN	0x0080		/* use "wlan%d" names */
#define FLAG_AVOID_UNLINK_URBS 0x0100	/* don't unlink urbs at usbnet_stop() */
#define FLAG_SEND_ZLP	0x0200		/* hw requires ZLPs are sent */
#define FLAG_WWAN	0x0400		/* use "wwan%d" names */

#define FLAG_LINK_INTR	0x0800		/* updates link (carrier) status */

#define FLAG_POINTTOPOINT 0x1000	/* possibly use "usb%d" names */

/*
 * Indicates to usbnet, that USB driver accumulates multiple IP packets.
 * Affects statistic (counters) and short packet handling.
 */
#define FLAG_MULTI_PACKET	0x2000
#define FLAG_RX_ASSEMBLE	0x4000	/* rx packets may span >1 frames */
#define FLAG_NOARP		0x8000	/* device can't do ARP */

	/* init device ... can sleep, or cause probe() failure */
	int	(*bind)(struct usbnet *, struct usb_interface *);

	/* cleanup device ... can sleep, but can't fail */
	void	(*unbind)(struct usbnet *, struct usb_interface *);

	/* reset device ... can sleep */
	int	(*reset)(struct usbnet *);

	/* stop device ... can sleep */
	int	(*stop)(struct usbnet *);

	/* see if peer is connected ... can sleep */
	int	(*check_connect)(struct usbnet *);

	/* (dis)activate runtime power management */
	int	(*manage_power)(struct usbnet *, int);

	/* for status polling */
	void	(*status)(struct usbnet *, struct urb *);

	/* link reset handling, called from defer_kevent */
	int	(*link_reset)(struct usbnet *);

	/* fixup rx packet (strip framing) */
	int	(*rx_fixup)(struct usbnet *dev, struct sk_buff *skb);

	/* fixup tx packet (add framing) */
	struct sk_buff	*(*tx_fixup)(struct usbnet *dev,
				struct sk_buff *skb, gfp_t flags);

	/* recover from timeout */
	void	(*recover)(struct usbnet *dev);

	/* early initialization code, can sleep. This is for minidrivers
	 * having 'subminidrivers' that need to do extra initialization
	 * right after minidriver have initialized hardware. */
	int	(*early_init)(struct usbnet *dev);

	/* called by minidriver when receiving indication */
	void	(*indication)(struct usbnet *dev, void *ind, int indlen);

	/* rx mode change (device changes address list filtering) */
	void	(*set_rx_mode)(struct usbnet *dev);

	/* for new devices, use the descriptor-reading code instead */
	int		in;		/* rx endpoint */
	int		out;		/* tx endpoint */

	unsigned long	data;		/* Misc driver specific data */
};

/* Minidrivers are just drivers using the "usbnet" core as a powerful
 * network-specific subroutine library ... that happens to do pretty
 * much everything except custom framing and chip-specific stuff.
 */
extern int usbnet_probe(struct usb_interface *, const struct usb_device_id *);
extern int usbnet_suspend(struct usb_interface *, pm_message_t);
extern int usbnet_resume(struct usb_interface *);
extern void usbnet_disconnect(struct usb_interface *);
extern void usbnet_device_suggests_idle(struct usbnet *dev);

extern int usbnet_read_cmd(struct usbnet *dev, u8 cmd, u8 reqtype,
		    u16 value, u16 index, void *data, u16 size);
extern int usbnet_write_cmd(struct usbnet *dev, u8 cmd, u8 reqtype,
		    u16 value, u16 index, const void *data, u16 size);
extern int usbnet_read_cmd_nopm(struct usbnet *dev, u8 cmd, u8 reqtype,
		    u16 value, u16 index, void *data, u16 size);
extern int usbnet_write_cmd_nopm(struct usbnet *dev, u8 cmd, u8 reqtype,
		    u16 value, u16 index, const void *data, u16 size);
extern int usbnet_write_cmd_async(struct usbnet *dev, u8 cmd, u8 reqtype,
		    u16 value, u16 index, const void *data, u16 size);

/* Drivers that reuse some of the standard USB CDC infrastructure
 * (notably, using multiple interfaces according to the CDC
 * union descriptor) get some helper code.
 */
struct cdc_state {
	struct usb_cdc_header_desc	*header;
	struct usb_cdc_union_desc	*u;
	struct usb_cdc_ether_desc	*ether;
	struct usb_interface		*control;
	struct usb_interface		*data;
};

extern void usbnet_cdc_update_filter(struct usbnet *dev);
extern int usbnet_generic_cdc_bind(struct usbnet *, struct usb_interface *);
extern int usbnet_ether_cdc_bind(struct usbnet *dev, struct usb_interface *intf);
extern int usbnet_cdc_bind(struct usbnet *, struct usb_interface *);
extern void usbnet_cdc_unbind(struct usbnet *, struct usb_interface *);
extern void usbnet_cdc_status(struct usbnet *, struct urb *);

/* CDC and RNDIS support the same host-chosen packet filters for IN transfers */
#define	DEFAULT_FILTER	(USB_CDC_PACKET_TYPE_BROADCAST \
			|USB_CDC_PACKET_TYPE_ALL_MULTICAST \
			|USB_CDC_PACKET_TYPE_PROMISCUOUS \
			|USB_CDC_PACKET_TYPE_DIRECTED)


/* we record the state for each of our queued skbs */
enum skb_state {
	illegal = 0,
	tx_start, tx_done,
	rx_start, rx_done, rx_cleanup,
	unlink_start
};

struct skb_data {	/* skb->cb is one of these */
	struct urb		*urb;
	struct usbnet		*dev;
	enum skb_state		state;
	long			length;
	unsigned long		packets;
};

/* Drivers that set FLAG_MULTI_PACKET must call this in their
 * tx_fixup method before returning an skb.
 */
static inline void
usbnet_set_skb_tx_stats(struct sk_buff *skb,
			unsigned long packets, long bytes_delta)
{
	struct skb_data *entry = (struct skb_data *) skb->cb;

	entry->packets = packets;
	entry->length = bytes_delta;
}

extern int usbnet_open(struct net_device *net);
extern int usbnet_stop(struct net_device *net);
extern netdev_tx_t usbnet_start_xmit(struct sk_buff *skb,
				     struct net_device *net);
extern void usbnet_tx_timeout(struct net_device *net, unsigned int txqueue);
extern int usbnet_change_mtu(struct net_device *net, int new_mtu);

extern int usbnet_get_endpoints(struct usbnet *, struct usb_interface *);
extern int usbnet_get_ethernet_addr(struct usbnet *, int);
extern void usbnet_defer_kevent(struct usbnet *, int);
extern void usbnet_skb_return(struct usbnet *, struct sk_buff *);
extern void usbnet_unlink_rx_urbs(struct usbnet *);

extern void usbnet_pause_rx(struct usbnet *);
extern void usbnet_resume_rx(struct usbnet *);
extern void usbnet_purge_paused_rxq(struct usbnet *);

extern int usbnet_get_link_ksettings(struct net_device *net,
				     struct ethtool_link_ksettings *cmd);
extern int usbnet_set_link_ksettings(struct net_device *net,
				     const struct ethtool_link_ksettings *cmd);
extern u32 usbnet_get_link(struct net_device *net);
extern u32 usbnet_get_msglevel(struct net_device *);
extern void usbnet_set_msglevel(struct net_device *, u32);
extern void usbnet_set_rx_mode(struct net_device *net);
extern void usbnet_get_drvinfo(struct net_device *, struct ethtool_drvinfo *);
extern int usbnet_nway_reset(struct net_device *net);

extern int usbnet_manage_power(struct usbnet *, int);
extern void usbnet_link_change(struct usbnet *, bool, bool);

extern int usbnet_status_start(struct usbnet *dev, gfp_t mem_flags);
extern void usbnet_status_stop(struct usbnet *dev);

extern void usbnet_update_max_qlen(struct usbnet *dev);
extern void usbnet_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats);

#endif /* __LINUX_USB_USBNET_H */
