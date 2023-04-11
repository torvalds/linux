// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/crc-ccitt.h>
#include <asm/unaligned.h>

#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/nl802154.h>

#include "ieee802154_i.h"

static int ieee802154_deliver_skb(struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->protocol = htons(ETH_P_IEEE802154);

	return netif_receive_skb(skb);
}

void mac802154_rx_beacon_worker(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, rx_beacon_work);
	struct cfg802154_mac_pkt *mac_pkt;

	mac_pkt = list_first_entry_or_null(&local->rx_beacon_list,
					   struct cfg802154_mac_pkt, node);
	if (!mac_pkt)
		return;

	mac802154_process_beacon(local, mac_pkt->skb, mac_pkt->page, mac_pkt->channel);

	list_del(&mac_pkt->node);
	kfree_skb(mac_pkt->skb);
	kfree(mac_pkt);
}

static int
ieee802154_subif_frame(struct ieee802154_sub_if_data *sdata,
		       struct sk_buff *skb, const struct ieee802154_hdr *hdr)
{
	struct wpan_phy *wpan_phy = sdata->local->hw.phy;
	struct wpan_dev *wpan_dev = &sdata->wpan_dev;
	struct cfg802154_mac_pkt *mac_pkt;
	__le16 span, sshort;
	int rc;

	pr_debug("getting packet via slave interface %s\n", sdata->dev->name);

	span = wpan_dev->pan_id;
	sshort = wpan_dev->short_addr;

	/* Level 3 filtering: Only beacons are accepted during scans */
	if (sdata->required_filtering == IEEE802154_FILTERING_3_SCAN &&
	    sdata->required_filtering > wpan_phy->filtering) {
		if (mac_cb(skb)->type != IEEE802154_FC_TYPE_BEACON) {
			dev_dbg(&sdata->dev->dev,
				"drop non-beacon frame (0x%x) during scan\n",
				mac_cb(skb)->type);
			goto fail;
		}
	}

	switch (mac_cb(skb)->dest.mode) {
	case IEEE802154_ADDR_NONE:
		if (hdr->source.mode != IEEE802154_ADDR_NONE)
			/* FIXME: check if we are PAN coordinator */
			skb->pkt_type = PACKET_OTHERHOST;
		else
			/* ACK comes with both addresses empty */
			skb->pkt_type = PACKET_HOST;
		break;
	case IEEE802154_ADDR_LONG:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.extended_addr == wpan_dev->extended_addr)
			skb->pkt_type = PACKET_HOST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	case IEEE802154_ADDR_SHORT:
		if (mac_cb(skb)->dest.pan_id != span &&
		    mac_cb(skb)->dest.pan_id != cpu_to_le16(IEEE802154_PANID_BROADCAST))
			skb->pkt_type = PACKET_OTHERHOST;
		else if (mac_cb(skb)->dest.short_addr == sshort)
			skb->pkt_type = PACKET_HOST;
		else if (mac_cb(skb)->dest.short_addr ==
			  cpu_to_le16(IEEE802154_ADDR_BROADCAST))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
		break;
	default:
		pr_debug("invalid dest mode\n");
		goto fail;
	}

	skb->dev = sdata->dev;

	/* TODO this should be moved after netif_receive_skb call, otherwise
	 * wireshark will show a mac header with security fields and the
	 * payload is already decrypted.
	 */
	rc = mac802154_llsec_decrypt(&sdata->sec, skb);
	if (rc) {
		pr_debug("decryption failed: %i\n", rc);
		goto fail;
	}

	sdata->dev->stats.rx_packets++;
	sdata->dev->stats.rx_bytes += skb->len;

	switch (mac_cb(skb)->type) {
	case IEEE802154_FC_TYPE_BEACON:
		dev_dbg(&sdata->dev->dev, "BEACON received\n");
		if (!mac802154_is_scanning(sdata->local))
			goto fail;

		mac_pkt = kzalloc(sizeof(*mac_pkt), GFP_ATOMIC);
		if (!mac_pkt)
			goto fail;

		mac_pkt->skb = skb_get(skb);
		mac_pkt->sdata = sdata;
		mac_pkt->page = sdata->local->scan_page;
		mac_pkt->channel = sdata->local->scan_channel;
		list_add_tail(&mac_pkt->node, &sdata->local->rx_beacon_list);
		queue_work(sdata->local->mac_wq, &sdata->local->rx_beacon_work);
		return NET_RX_SUCCESS;
	case IEEE802154_FC_TYPE_ACK:
	case IEEE802154_FC_TYPE_MAC_CMD:
		goto fail;

	case IEEE802154_FC_TYPE_DATA:
		return ieee802154_deliver_skb(skb);
	default:
		pr_warn_ratelimited("ieee802154: bad frame received "
				    "(type = %d)\n", mac_cb(skb)->type);
		goto fail;
	}

fail:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static void
ieee802154_print_addr(const char *name, const struct ieee802154_addr *addr)
{
	if (addr->mode == IEEE802154_ADDR_NONE) {
		pr_debug("%s not present\n", name);
		return;
	}

	pr_debug("%s PAN ID: %04x\n", name, le16_to_cpu(addr->pan_id));
	if (addr->mode == IEEE802154_ADDR_SHORT) {
		pr_debug("%s is short: %04x\n", name,
			 le16_to_cpu(addr->short_addr));
	} else {
		u64 hw = swab64((__force u64)addr->extended_addr);

		pr_debug("%s is hardware: %8phC\n", name, &hw);
	}
}

static int
ieee802154_parse_frame_start(struct sk_buff *skb, struct ieee802154_hdr *hdr)
{
	int hlen;
	struct ieee802154_mac_cb *cb = mac_cb(skb);

	skb_reset_mac_header(skb);

	hlen = ieee802154_hdr_pull(skb, hdr);
	if (hlen < 0)
		return -EINVAL;

	skb->mac_len = hlen;

	pr_debug("fc: %04x dsn: %02x\n", le16_to_cpup((__le16 *)&hdr->fc),
		 hdr->seq);

	cb->type = hdr->fc.type;
	cb->ackreq = hdr->fc.ack_request;
	cb->secen = hdr->fc.security_enabled;

	ieee802154_print_addr("destination", &hdr->dest);
	ieee802154_print_addr("source", &hdr->source);

	cb->source = hdr->source;
	cb->dest = hdr->dest;

	if (hdr->fc.security_enabled) {
		u64 key;

		pr_debug("seclevel %i\n", hdr->sec.level);

		switch (hdr->sec.key_id_mode) {
		case IEEE802154_SCF_KEY_IMPLICIT:
			pr_debug("implicit key\n");
			break;

		case IEEE802154_SCF_KEY_INDEX:
			pr_debug("key %02x\n", hdr->sec.key_id);
			break;

		case IEEE802154_SCF_KEY_SHORT_INDEX:
			pr_debug("key %04x:%04x %02x\n",
				 le32_to_cpu(hdr->sec.short_src) >> 16,
				 le32_to_cpu(hdr->sec.short_src) & 0xffff,
				 hdr->sec.key_id);
			break;

		case IEEE802154_SCF_KEY_HW_INDEX:
			key = swab64((__force u64)hdr->sec.extended_src);
			pr_debug("key source %8phC %02x\n", &key,
				 hdr->sec.key_id);
			break;
		}
	}

	return 0;
}

static void
__ieee802154_rx_handle_packet(struct ieee802154_local *local,
			      struct sk_buff *skb)
{
	int ret;
	struct ieee802154_sub_if_data *sdata;
	struct ieee802154_hdr hdr;
	struct sk_buff *skb2;

	ret = ieee802154_parse_frame_start(skb, &hdr);
	if (ret) {
		pr_debug("got invalid frame\n");
		return;
	}

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->wpan_dev.iftype == NL802154_IFTYPE_MONITOR)
			continue;

		if (!ieee802154_sdata_running(sdata))
			continue;

		/* Do not deliver packets received on interfaces expecting
		 * AACK=1 if the address filters where disabled.
		 */
		if (local->hw.phy->filtering < IEEE802154_FILTERING_4_FRAME_FIELDS &&
		    sdata->required_filtering == IEEE802154_FILTERING_4_FRAME_FIELDS)
			continue;

		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2) {
			skb2->dev = sdata->dev;
			ieee802154_subif_frame(sdata, skb2, &hdr);
		}
	}
}

static void
ieee802154_monitors_rx(struct ieee802154_local *local, struct sk_buff *skb)
{
	struct sk_buff *skb2;
	struct ieee802154_sub_if_data *sdata;

	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_IEEE802154);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->wpan_dev.iftype != NL802154_IFTYPE_MONITOR)
			continue;

		if (!ieee802154_sdata_running(sdata))
			continue;

		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2) {
			skb2->dev = sdata->dev;
			ieee802154_deliver_skb(skb2);

			sdata->dev->stats.rx_packets++;
			sdata->dev->stats.rx_bytes += skb->len;
		}
	}
}

void ieee802154_rx(struct ieee802154_local *local, struct sk_buff *skb)
{
	u16 crc;

	WARN_ON_ONCE(softirq_count() == 0);

	if (local->suspended)
		goto free_skb;

	/* TODO: When a transceiver omits the checksum here, we
	 * add an own calculated one. This is currently an ugly
	 * solution because the monitor needs a crc here.
	 */
	if (local->hw.flags & IEEE802154_HW_RX_OMIT_CKSUM) {
		crc = crc_ccitt(0, skb->data, skb->len);
		put_unaligned_le16(crc, skb_put(skb, 2));
	}

	rcu_read_lock();

	ieee802154_monitors_rx(local, skb);

	/* Level 1 filtering: Check the FCS by software when relevant */
	if (local->hw.phy->filtering == IEEE802154_FILTERING_NONE) {
		crc = crc_ccitt(0, skb->data, skb->len);
		if (crc)
			goto drop;
	}
	/* remove crc */
	skb_trim(skb, skb->len - 2);

	__ieee802154_rx_handle_packet(local, skb);

drop:
	rcu_read_unlock();
free_skb:
	kfree_skb(skb);
}

void
ieee802154_rx_irqsafe(struct ieee802154_hw *hw, struct sk_buff *skb, u8 lqi)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_mac_cb *cb = mac_cb_init(skb);

	cb->lqi = lqi;
	skb->pkt_type = IEEE802154_RX_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee802154_rx_irqsafe);
