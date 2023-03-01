// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAIF USB handler
 * Copyright (C) ST-Ericsson AB 2011
 * Author:	Sjur Brendeland
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/usbnet.h>
#include <linux/etherdevice.h>
#include <net/netns/generic.h>
#include <net/caif/caif_dev.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfcnfg.h>

MODULE_LICENSE("GPL");

#define CFUSB_PAD_DESCR_SZ 1	/* Alignment descriptor length */
#define CFUSB_ALIGNMENT 4	/* Number of bytes to align. */
#define CFUSB_MAX_HEADLEN (CFUSB_PAD_DESCR_SZ + CFUSB_ALIGNMENT-1)
#define STE_USB_VID 0x04cc	/* USB Product ID for ST-Ericsson */
#define STE_USB_PID_CAIF 0x230f	/* Product id for CAIF Modems */

struct cfusbl {
	struct cflayer layer;
	u8 tx_eth_hdr[ETH_HLEN];
};

static bool pack_added;

static int cfusbl_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u8 hpad;

	/* Remove padding. */
	cfpkt_extr_head(pkt, &hpad, 1);
	cfpkt_extr_head(pkt, NULL, hpad);
	return layr->up->receive(layr->up, pkt);
}

static int cfusbl_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	struct caif_payload_info *info;
	u8 hpad;
	u8 zeros[CFUSB_ALIGNMENT];
	struct sk_buff *skb;
	struct cfusbl *usbl = container_of(layr, struct cfusbl, layer);

	skb = cfpkt_tonative(pkt);

	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_IP);

	info = cfpkt_info(pkt);
	hpad = (info->hdr_len + CFUSB_PAD_DESCR_SZ) & (CFUSB_ALIGNMENT - 1);

	if (skb_headroom(skb) < ETH_HLEN + CFUSB_PAD_DESCR_SZ + hpad) {
		pr_warn("Headroom too small\n");
		kfree_skb(skb);
		return -EIO;
	}
	memset(zeros, 0, hpad);

	cfpkt_add_head(pkt, zeros, hpad);
	cfpkt_add_head(pkt, &hpad, 1);
	cfpkt_add_head(pkt, usbl->tx_eth_hdr, sizeof(usbl->tx_eth_hdr));
	return layr->dn->transmit(layr->dn, pkt);
}

static void cfusbl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
			   int phyid)
{
	if (layr->up && layr->up->ctrlcmd)
		layr->up->ctrlcmd(layr->up, ctrl, layr->id);
}

static struct cflayer *cfusbl_create(int phyid, u8 ethaddr[ETH_ALEN],
				      u8 braddr[ETH_ALEN])
{
	struct cfusbl *this = kmalloc(sizeof(struct cfusbl), GFP_ATOMIC);

	if (!this)
		return NULL;

	caif_assert(offsetof(struct cfusbl, layer) == 0);

	memset(&this->layer, 0, sizeof(this->layer));
	this->layer.receive = cfusbl_receive;
	this->layer.transmit = cfusbl_transmit;
	this->layer.ctrlcmd = cfusbl_ctrlcmd;
	snprintf(this->layer.name, CAIF_LAYER_NAME_SZ, "usb%d", phyid);
	this->layer.id = phyid;

	/*
	 * Construct TX ethernet header:
	 *	0-5	destination address
	 *	5-11	source address
	 *	12-13	protocol type
	 */
	ether_addr_copy(&this->tx_eth_hdr[ETH_ALEN], braddr);
	ether_addr_copy(&this->tx_eth_hdr[ETH_ALEN], ethaddr);
	this->tx_eth_hdr[12] = cpu_to_be16(ETH_P_802_EX1) & 0xff;
	this->tx_eth_hdr[13] = (cpu_to_be16(ETH_P_802_EX1) >> 8) & 0xff;
	pr_debug("caif ethernet TX-header dst:%pM src:%pM type:%02x%02x\n",
			this->tx_eth_hdr, this->tx_eth_hdr + ETH_ALEN,
			this->tx_eth_hdr[12], this->tx_eth_hdr[13]);

	return (struct cflayer *) this;
}

static void cfusbl_release(struct cflayer *layer)
{
	kfree(layer);
}

static struct packet_type caif_usb_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_802_EX1),
};

static int cfusbl_device_notify(struct notifier_block *me, unsigned long what,
				void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct caif_dev_common common;
	struct cflayer *layer, *link_support;
	struct usbnet *usbnet;
	struct usb_device *usbdev;
	int res;

	if (what == NETDEV_UNREGISTER && dev->reg_state >= NETREG_UNREGISTERED)
		return 0;

	/* Check whether we have a NCM device, and find its VID/PID. */
	if (!(dev->dev.parent && dev->dev.parent->driver &&
	      strcmp(dev->dev.parent->driver->name, "cdc_ncm") == 0))
		return 0;

	usbnet = netdev_priv(dev);
	usbdev = usbnet->udev;

	pr_debug("USB CDC NCM device VID:0x%4x PID:0x%4x\n",
		le16_to_cpu(usbdev->descriptor.idVendor),
		le16_to_cpu(usbdev->descriptor.idProduct));

	/* Check for VID/PID that supports CAIF */
	if (!(le16_to_cpu(usbdev->descriptor.idVendor) == STE_USB_VID &&
		le16_to_cpu(usbdev->descriptor.idProduct) == STE_USB_PID_CAIF))
		return 0;

	if (what == NETDEV_UNREGISTER)
		module_put(THIS_MODULE);

	if (what != NETDEV_REGISTER)
		return 0;

	__module_get(THIS_MODULE);

	memset(&common, 0, sizeof(common));
	common.use_frag = false;
	common.use_fcs = false;
	common.use_stx = false;
	common.link_select = CAIF_LINK_HIGH_BANDW;
	common.flowctrl = NULL;

	link_support = cfusbl_create(dev->ifindex, dev->dev_addr,
					dev->broadcast);

	if (!link_support)
		return -ENOMEM;

	if (dev->num_tx_queues > 1)
		pr_warn("USB device uses more than one tx queue\n");

	res = caif_enroll_dev(dev, &common, link_support, CFUSB_MAX_HEADLEN,
			&layer, &caif_usb_type.func);
	if (res)
		goto err;

	if (!pack_added)
		dev_add_pack(&caif_usb_type);
	pack_added = true;

	strlcpy(layer->name, dev->name, sizeof(layer->name));

	return 0;
err:
	cfusbl_release(link_support);
	return res;
}

static struct notifier_block caif_device_notifier = {
	.notifier_call = cfusbl_device_notify,
	.priority = 0,
};

static int __init cfusbl_init(void)
{
	return register_netdevice_notifier(&caif_device_notifier);
}

static void __exit cfusbl_exit(void)
{
	unregister_netdevice_notifier(&caif_device_notifier);
	dev_remove_pack(&caif_usb_type);
}

module_init(cfusbl_init);
module_exit(cfusbl_exit);
