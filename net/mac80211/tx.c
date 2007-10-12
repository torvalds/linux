/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Transmit and frame generation functions.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/bitmap.h>
#include <linux/rcupdate.h>
#include <net/net_namespace.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "ieee80211_led.h"
#include "wep.h"
#include "wpa.h"
#include "wme.h"
#include "ieee80211_rate.h"

#define IEEE80211_TX_OK		0
#define IEEE80211_TX_AGAIN	1
#define IEEE80211_TX_FRAG_AGAIN	2

/* misc utils */

static inline void ieee80211_include_sequence(struct ieee80211_sub_if_data *sdata,
					      struct ieee80211_hdr *hdr)
{
	/* Set the sequence number for this frame. */
	hdr->seq_ctrl = cpu_to_le16(sdata->sequence);

	/* Increase the sequence number. */
	sdata->sequence = (sdata->sequence + 0x10) & IEEE80211_SCTL_SEQ;
}

#ifdef CONFIG_MAC80211_LOWTX_FRAME_DUMP
static void ieee80211_dump_frame(const char *ifname, const char *title,
				 const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 fc;
	int hdrlen;
	DECLARE_MAC_BUF(mac);

	printk(KERN_DEBUG "%s: %s (len=%d)", ifname, title, skb->len);
	if (skb->len < 4) {
		printk("\n");
		return;
	}

	fc = le16_to_cpu(hdr->frame_control);
	hdrlen = ieee80211_get_hdrlen(fc);
	if (hdrlen > skb->len)
		hdrlen = skb->len;
	if (hdrlen >= 4)
		printk(" FC=0x%04x DUR=0x%04x",
		       fc, le16_to_cpu(hdr->duration_id));
	if (hdrlen >= 10)
		printk(" A1=%s", print_mac(mac, hdr->addr1));
	if (hdrlen >= 16)
		printk(" A2=%s", print_mac(mac, hdr->addr2));
	if (hdrlen >= 24)
		printk(" A3=%s", print_mac(mac, hdr->addr3));
	if (hdrlen >= 30)
		printk(" A4=%s", print_mac(mac, hdr->addr4));
	printk("\n");
}
#else /* CONFIG_MAC80211_LOWTX_FRAME_DUMP */
static inline void ieee80211_dump_frame(const char *ifname, const char *title,
					struct sk_buff *skb)
{
}
#endif /* CONFIG_MAC80211_LOWTX_FRAME_DUMP */

static u16 ieee80211_duration(struct ieee80211_txrx_data *tx, int group_addr,
			      int next_frag_len)
{
	int rate, mrate, erp, dur, i;
	struct ieee80211_rate *txrate = tx->u.tx.rate;
	struct ieee80211_local *local = tx->local;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;

	erp = txrate->flags & IEEE80211_RATE_ERP;

	/*
	 * data and mgmt (except PS Poll):
	 * - during CFP: 32768
	 * - during contention period:
	 *   if addr1 is group address: 0
	 *   if more fragments = 0 and addr1 is individual address: time to
	 *      transmit one ACK plus SIFS
	 *   if more fragments = 1 and addr1 is individual address: time to
	 *      transmit next fragment plus 2 x ACK plus 3 x SIFS
	 *
	 * IEEE 802.11, 9.6:
	 * - control response frame (CTS or ACK) shall be transmitted using the
	 *   same rate as the immediately previous frame in the frame exchange
	 *   sequence, if this rate belongs to the PHY mandatory rates, or else
	 *   at the highest possible rate belonging to the PHY rates in the
	 *   BSSBasicRateSet
	 */

	if ((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) {
		/* TODO: These control frames are not currently sent by
		 * 80211.o, but should they be implemented, this function
		 * needs to be updated to support duration field calculation.
		 *
		 * RTS: time needed to transmit pending data/mgmt frame plus
		 *    one CTS frame plus one ACK frame plus 3 x SIFS
		 * CTS: duration of immediately previous RTS minus time
		 *    required to transmit CTS and its SIFS
		 * ACK: 0 if immediately previous directed data/mgmt had
		 *    more=0, with more=1 duration in ACK frame is duration
		 *    from previous frame minus time needed to transmit ACK
		 *    and its SIFS
		 * PS Poll: BIT(15) | BIT(14) | aid
		 */
		return 0;
	}

	/* data/mgmt */
	if (0 /* FIX: data/mgmt during CFP */)
		return 32768;

	if (group_addr) /* Group address as the destination - no ACK */
		return 0;

	/* Individual destination address:
	 * IEEE 802.11, Ch. 9.6 (after IEEE 802.11g changes)
	 * CTS and ACK frames shall be transmitted using the highest rate in
	 * basic rate set that is less than or equal to the rate of the
	 * immediately previous frame and that is using the same modulation
	 * (CCK or OFDM). If no basic rate set matches with these requirements,
	 * the highest mandatory rate of the PHY that is less than or equal to
	 * the rate of the previous frame is used.
	 * Mandatory rates for IEEE 802.11g PHY: 1, 2, 5.5, 11, 6, 12, 24 Mbps
	 */
	rate = -1;
	mrate = 10; /* use 1 Mbps if everything fails */
	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *r = &mode->rates[i];
		if (r->rate > txrate->rate)
			break;

		if (IEEE80211_RATE_MODULATION(txrate->flags) !=
		    IEEE80211_RATE_MODULATION(r->flags))
			continue;

		if (r->flags & IEEE80211_RATE_BASIC)
			rate = r->rate;
		else if (r->flags & IEEE80211_RATE_MANDATORY)
			mrate = r->rate;
	}
	if (rate == -1) {
		/* No matching basic rate found; use highest suitable mandatory
		 * PHY rate */
		rate = mrate;
	}

	/* Time needed to transmit ACK
	 * (10 bytes + 4-byte FCS = 112 bits) plus SIFS; rounded up
	 * to closest integer */

	dur = ieee80211_frame_duration(local, 10, rate, erp,
		       tx->sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE);

	if (next_frag_len) {
		/* Frame is fragmented: duration increases with time needed to
		 * transmit next fragment plus ACK and 2 x SIFS. */
		dur *= 2; /* ACK + SIFS */
		/* next fragment */
		dur += ieee80211_frame_duration(local, next_frag_len,
				txrate->rate, erp,
				tx->sdata->flags &
					IEEE80211_SDATA_SHORT_PREAMBLE);
	}

	return dur;
}

static inline int __ieee80211_queue_stopped(const struct ieee80211_local *local,
					    int queue)
{
	return test_bit(IEEE80211_LINK_STATE_XOFF, &local->state[queue]);
}

static inline int __ieee80211_queue_pending(const struct ieee80211_local *local,
					    int queue)
{
	return test_bit(IEEE80211_LINK_STATE_PENDING, &local->state[queue]);
}

static int inline is_ieee80211_device(struct net_device *dev,
				      struct net_device *master)
{
	return (wdev_priv(dev->ieee80211_ptr) ==
		wdev_priv(master->ieee80211_ptr));
}

/* tx handlers */

static ieee80211_txrx_result
ieee80211_tx_h_check_assoc(struct ieee80211_txrx_data *tx)
{
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	u32 sta_flags;

	if (unlikely(tx->flags & IEEE80211_TXRXD_TX_INJECTED))
		return TXRX_CONTINUE;

	if (unlikely(tx->local->sta_scanning != 0) &&
	    ((tx->fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT ||
	     (tx->fc & IEEE80211_FCTL_STYPE) != IEEE80211_STYPE_PROBE_REQ))
		return TXRX_DROP;

	if (tx->flags & IEEE80211_TXRXD_TXPS_BUFFERED)
		return TXRX_CONTINUE;

	sta_flags = tx->sta ? tx->sta->flags : 0;

	if (likely(tx->flags & IEEE80211_TXRXD_TXUNICAST)) {
		if (unlikely(!(sta_flags & WLAN_STA_ASSOC) &&
			     tx->sdata->type != IEEE80211_IF_TYPE_IBSS &&
			     (tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA)) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			DECLARE_MAC_BUF(mac);
			printk(KERN_DEBUG "%s: dropped data frame to not "
			       "associated station %s\n",
			       tx->dev->name, print_mac(mac, hdr->addr1));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
			I802_DEBUG_INC(tx->local->tx_handlers_drop_not_assoc);
			return TXRX_DROP;
		}
	} else {
		if (unlikely((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA &&
			     tx->local->num_sta == 0 &&
			     tx->sdata->type != IEEE80211_IF_TYPE_IBSS)) {
			/*
			 * No associated STAs - no need to send multicast
			 * frames.
			 */
			return TXRX_DROP;
		}
		return TXRX_CONTINUE;
	}

	if (unlikely(/* !injected && */ tx->sdata->ieee802_1x &&
		     !(sta_flags & WLAN_STA_AUTHORIZED))) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		DECLARE_MAC_BUF(mac);
		printk(KERN_DEBUG "%s: dropped frame to %s"
		       " (unauthorized port)\n", tx->dev->name,
		       print_mac(mac, hdr->addr1));
#endif
		I802_DEBUG_INC(tx->local->tx_handlers_drop_unauth_port);
		return TXRX_DROP;
	}

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_tx_h_sequence(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;

	if (ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control)) >= 24)
		ieee80211_include_sequence(tx->sdata, hdr);

	return TXRX_CONTINUE;
}

/* This function is called whenever the AP is about to exceed the maximum limit
 * of buffered frames for power saving STAs. This situation should not really
 * happen often during normal operation, so dropping the oldest buffered packet
 * from each queue should be OK to make some room for new frames. */
static void purge_old_ps_buffers(struct ieee80211_local *local)
{
	int total = 0, purged = 0;
	struct sk_buff *skb;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	/*
	 * virtual interfaces are protected by RCU
	 */
	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		struct ieee80211_if_ap *ap;
		if (sdata->dev == local->mdev ||
		    sdata->type != IEEE80211_IF_TYPE_AP)
			continue;
		ap = &sdata->u.ap;
		skb = skb_dequeue(&ap->ps_bc_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&ap->ps_bc_buf);
	}
	rcu_read_unlock();

	read_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		skb = skb_dequeue(&sta->ps_tx_buf);
		if (skb) {
			purged++;
			dev_kfree_skb(skb);
		}
		total += skb_queue_len(&sta->ps_tx_buf);
	}
	read_unlock_bh(&local->sta_lock);

	local->total_ps_buffered = total;
	printk(KERN_DEBUG "%s: PS buffers full - purged %d frames\n",
	       wiphy_name(local->hw.wiphy), purged);
}

static inline ieee80211_txrx_result
ieee80211_tx_h_multicast_ps_buf(struct ieee80211_txrx_data *tx)
{
	/* broadcast/multicast frame */
	/* If any of the associated stations is in power save mode,
	 * the frame is buffered to be sent after DTIM beacon frame */
	if ((tx->local->hw.flags & IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING) &&
	    tx->sdata->type != IEEE80211_IF_TYPE_WDS &&
	    tx->sdata->bss && atomic_read(&tx->sdata->bss->num_sta_ps) &&
	    !(tx->fc & IEEE80211_FCTL_ORDER)) {
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&tx->sdata->bss->ps_bc_buf) >=
		    AP_MAX_BC_BUFFER) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: BC TX buffer full - "
				       "dropping the oldest frame\n",
				       tx->dev->name);
			}
			dev_kfree_skb(skb_dequeue(&tx->sdata->bss->ps_bc_buf));
		} else
			tx->local->total_ps_buffered++;
		skb_queue_tail(&tx->sdata->bss->ps_bc_buf, tx->skb);
		return TXRX_QUEUED;
	}

	return TXRX_CONTINUE;
}

static inline ieee80211_txrx_result
ieee80211_tx_h_unicast_ps_buf(struct ieee80211_txrx_data *tx)
{
	struct sta_info *sta = tx->sta;
	DECLARE_MAC_BUF(mac);

	if (unlikely(!sta ||
		     ((tx->fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT &&
		      (tx->fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_RESP)))
		return TXRX_CONTINUE;

	if (unlikely((sta->flags & WLAN_STA_PS) && !sta->pspoll)) {
		struct ieee80211_tx_packet_data *pkt_data;
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
		printk(KERN_DEBUG "STA %s aid %d: PS buffer (entries "
		       "before %d)\n",
		       print_mac(mac, sta->addr), sta->aid,
		       skb_queue_len(&sta->ps_tx_buf));
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
		sta->flags |= WLAN_STA_TIM;
		if (tx->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(tx->local);
		if (skb_queue_len(&sta->ps_tx_buf) >= STA_MAX_TX_BUFFER) {
			struct sk_buff *old = skb_dequeue(&sta->ps_tx_buf);
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: STA %s TX "
				       "buffer full - dropping oldest frame\n",
				       tx->dev->name, print_mac(mac, sta->addr));
			}
			dev_kfree_skb(old);
		} else
			tx->local->total_ps_buffered++;
		/* Queue frame to be sent after STA sends an PS Poll frame */
		if (skb_queue_empty(&sta->ps_tx_buf)) {
			if (tx->local->ops->set_tim)
				tx->local->ops->set_tim(local_to_hw(tx->local),
						       sta->aid, 1);
			if (tx->sdata->bss)
				bss_tim_set(tx->local, tx->sdata->bss, sta->aid);
		}
		pkt_data = (struct ieee80211_tx_packet_data *)tx->skb->cb;
		pkt_data->jiffies = jiffies;
		skb_queue_tail(&sta->ps_tx_buf, tx->skb);
		return TXRX_QUEUED;
	}
#ifdef CONFIG_MAC80211_VERBOSE_PS_DEBUG
	else if (unlikely(sta->flags & WLAN_STA_PS)) {
		printk(KERN_DEBUG "%s: STA %s in PS mode, but pspoll "
		       "set -> send frame\n", tx->dev->name,
		       print_mac(mac, sta->addr));
	}
#endif /* CONFIG_MAC80211_VERBOSE_PS_DEBUG */
	sta->pspoll = 0;

	return TXRX_CONTINUE;
}


static ieee80211_txrx_result
ieee80211_tx_h_ps_buf(struct ieee80211_txrx_data *tx)
{
	if (unlikely(tx->flags & IEEE80211_TXRXD_TXPS_BUFFERED))
		return TXRX_CONTINUE;

	if (tx->flags & IEEE80211_TXRXD_TXUNICAST)
		return ieee80211_tx_h_unicast_ps_buf(tx);
	else
		return ieee80211_tx_h_multicast_ps_buf(tx);
}




static ieee80211_txrx_result
ieee80211_tx_h_select_key(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_key *key;

	if (unlikely(tx->u.tx.control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT))
		tx->key = NULL;
	else if (tx->sta && (key = rcu_dereference(tx->sta->key)))
		tx->key = key;
	else if ((key = rcu_dereference(tx->sdata->default_key)))
		tx->key = key;
	else if (tx->sdata->drop_unencrypted &&
		 !(tx->sdata->eapol && ieee80211_is_eapol(tx->skb))) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop_unencrypted);
		return TXRX_DROP;
	} else {
		tx->key = NULL;
		tx->u.tx.control->flags |= IEEE80211_TXCTL_DO_NOT_ENCRYPT;
	}

	if (tx->key) {
		tx->key->tx_rx_count++;
		/* TODO: add threshold stuff again */
	}

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_tx_h_fragment(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	size_t hdrlen, per_fragm, num_fragm, payload_len, left;
	struct sk_buff **frags, *first, *frag;
	int i;
	u16 seq;
	u8 *pos;
	int frag_threshold = tx->local->fragmentation_threshold;

	if (!(tx->flags & IEEE80211_TXRXD_FRAGMENTED))
		return TXRX_CONTINUE;

	first = tx->skb;

	hdrlen = ieee80211_get_hdrlen(tx->fc);
	payload_len = first->len - hdrlen;
	per_fragm = frag_threshold - hdrlen - FCS_LEN;
	num_fragm = DIV_ROUND_UP(payload_len, per_fragm);

	frags = kzalloc(num_fragm * sizeof(struct sk_buff *), GFP_ATOMIC);
	if (!frags)
		goto fail;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);
	seq = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ;
	pos = first->data + hdrlen + per_fragm;
	left = payload_len - per_fragm;
	for (i = 0; i < num_fragm - 1; i++) {
		struct ieee80211_hdr *fhdr;
		size_t copylen;

		if (left <= 0)
			goto fail;

		/* reserve enough extra head and tail room for possible
		 * encryption */
		frag = frags[i] =
			dev_alloc_skb(tx->local->tx_headroom +
				      frag_threshold +
				      IEEE80211_ENCRYPT_HEADROOM +
				      IEEE80211_ENCRYPT_TAILROOM);
		if (!frag)
			goto fail;
		/* Make sure that all fragments use the same priority so
		 * that they end up using the same TX queue */
		frag->priority = first->priority;
		skb_reserve(frag, tx->local->tx_headroom +
				  IEEE80211_ENCRYPT_HEADROOM);
		fhdr = (struct ieee80211_hdr *) skb_put(frag, hdrlen);
		memcpy(fhdr, first->data, hdrlen);
		if (i == num_fragm - 2)
			fhdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_MOREFRAGS);
		fhdr->seq_ctrl = cpu_to_le16(seq | ((i + 1) & IEEE80211_SCTL_FRAG));
		copylen = left > per_fragm ? per_fragm : left;
		memcpy(skb_put(frag, copylen), pos, copylen);

		pos += copylen;
		left -= copylen;
	}
	skb_trim(first, hdrlen + per_fragm);

	tx->u.tx.num_extra_frag = num_fragm - 1;
	tx->u.tx.extra_frag = frags;

	return TXRX_CONTINUE;

 fail:
	printk(KERN_DEBUG "%s: failed to fragment frame\n", tx->dev->name);
	if (frags) {
		for (i = 0; i < num_fragm - 1; i++)
			if (frags[i])
				dev_kfree_skb(frags[i]);
		kfree(frags);
	}
	I802_DEBUG_INC(tx->local->tx_handlers_drop_fragment);
	return TXRX_DROP;
}

static ieee80211_txrx_result
ieee80211_tx_h_encrypt(struct ieee80211_txrx_data *tx)
{
	if (!tx->key)
		return TXRX_CONTINUE;

	switch (tx->key->conf.alg) {
	case ALG_WEP:
		return ieee80211_crypto_wep_encrypt(tx);
	case ALG_TKIP:
		return ieee80211_crypto_tkip_encrypt(tx);
	case ALG_CCMP:
		return ieee80211_crypto_ccmp_encrypt(tx);
	}

	/* not reached */
	WARN_ON(1);
	return TXRX_DROP;
}

static ieee80211_txrx_result
ieee80211_tx_h_rate_ctrl(struct ieee80211_txrx_data *tx)
{
	struct rate_control_extra extra;

	if (likely(!tx->u.tx.rate)) {
		memset(&extra, 0, sizeof(extra));
		extra.mode = tx->u.tx.mode;
		extra.ethertype = tx->ethertype;

		tx->u.tx.rate = rate_control_get_rate(tx->local, tx->dev,
						      tx->skb, &extra);
		if (unlikely(extra.probe != NULL)) {
			tx->u.tx.control->flags |=
				IEEE80211_TXCTL_RATE_CTRL_PROBE;
			tx->flags |= IEEE80211_TXRXD_TXPROBE_LAST_FRAG;
			tx->u.tx.control->alt_retry_rate = tx->u.tx.rate->val;
			tx->u.tx.rate = extra.probe;
		} else
			tx->u.tx.control->alt_retry_rate = -1;

		if (!tx->u.tx.rate)
			return TXRX_DROP;
	} else
		tx->u.tx.control->alt_retry_rate = -1;

	if (tx->u.tx.mode->mode == MODE_IEEE80211G &&
	    (tx->sdata->flags & IEEE80211_SDATA_USE_PROTECTION) &&
	    (tx->flags & IEEE80211_TXRXD_FRAGMENTED) && extra.nonerp) {
		tx->u.tx.last_frag_rate = tx->u.tx.rate;
		if (extra.probe)
			tx->flags &= ~IEEE80211_TXRXD_TXPROBE_LAST_FRAG;
		else
			tx->flags |= IEEE80211_TXRXD_TXPROBE_LAST_FRAG;
		tx->u.tx.rate = extra.nonerp;
		tx->u.tx.control->rate = extra.nonerp;
		tx->u.tx.control->flags &= ~IEEE80211_TXCTL_RATE_CTRL_PROBE;
	} else {
		tx->u.tx.last_frag_rate = tx->u.tx.rate;
		tx->u.tx.control->rate = tx->u.tx.rate;
	}
	tx->u.tx.control->tx_rate = tx->u.tx.rate->val;

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_tx_h_misc(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;
	u16 fc = le16_to_cpu(hdr->frame_control);
	u16 dur;
	struct ieee80211_tx_control *control = tx->u.tx.control;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;

	if (!control->retry_limit) {
		if (!is_multicast_ether_addr(hdr->addr1)) {
			if (tx->skb->len + FCS_LEN > tx->local->rts_threshold
			    && tx->local->rts_threshold <
					IEEE80211_MAX_RTS_THRESHOLD) {
				control->flags |=
					IEEE80211_TXCTL_USE_RTS_CTS;
				control->flags |=
					IEEE80211_TXCTL_LONG_RETRY_LIMIT;
				control->retry_limit =
					tx->local->long_retry_limit;
			} else {
				control->retry_limit =
					tx->local->short_retry_limit;
			}
		} else {
			control->retry_limit = 1;
		}
	}

	if (tx->flags & IEEE80211_TXRXD_FRAGMENTED) {
		/* Do not use multiple retry rates when sending fragmented
		 * frames.
		 * TODO: The last fragment could still use multiple retry
		 * rates. */
		control->alt_retry_rate = -1;
	}

	/* Use CTS protection for unicast frames sent using extended rates if
	 * there are associated non-ERP stations and RTS/CTS is not configured
	 * for the frame. */
	if (mode->mode == MODE_IEEE80211G &&
	    (tx->u.tx.rate->flags & IEEE80211_RATE_ERP) &&
	    (tx->flags & IEEE80211_TXRXD_TXUNICAST) &&
	    (tx->sdata->flags & IEEE80211_SDATA_USE_PROTECTION) &&
	    !(control->flags & IEEE80211_TXCTL_USE_RTS_CTS))
		control->flags |= IEEE80211_TXCTL_USE_CTS_PROTECT;

	/* Transmit data frames using short preambles if the driver supports
	 * short preambles at the selected rate and short preambles are
	 * available on the network at the current point in time. */
	if (((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) &&
	    (tx->u.tx.rate->flags & IEEE80211_RATE_PREAMBLE2) &&
	    (tx->sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE) &&
	    (!tx->sta || (tx->sta->flags & WLAN_STA_SHORT_PREAMBLE))) {
		tx->u.tx.control->tx_rate = tx->u.tx.rate->val2;
	}

	/* Setup duration field for the first fragment of the frame. Duration
	 * for remaining fragments will be updated when they are being sent
	 * to low-level driver in ieee80211_tx(). */
	dur = ieee80211_duration(tx, is_multicast_ether_addr(hdr->addr1),
				 (tx->flags & IEEE80211_TXRXD_FRAGMENTED) ?
				 tx->u.tx.extra_frag[0]->len : 0);
	hdr->duration_id = cpu_to_le16(dur);

	if ((control->flags & IEEE80211_TXCTL_USE_RTS_CTS) ||
	    (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)) {
		struct ieee80211_rate *rate;

		/* Do not use multiple retry rates when using RTS/CTS */
		control->alt_retry_rate = -1;

		/* Use min(data rate, max base rate) as CTS/RTS rate */
		rate = tx->u.tx.rate;
		while (rate > mode->rates &&
		       !(rate->flags & IEEE80211_RATE_BASIC))
			rate--;

		control->rts_cts_rate = rate->val;
		control->rts_rate = rate;
	}

	if (tx->sta) {
		tx->sta->tx_packets++;
		tx->sta->tx_fragments++;
		tx->sta->tx_bytes += tx->skb->len;
		if (tx->u.tx.extra_frag) {
			int i;
			tx->sta->tx_fragments += tx->u.tx.num_extra_frag;
			for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
				tx->sta->tx_bytes +=
					tx->u.tx.extra_frag[i]->len;
			}
		}
	}

	/*
	 * Tell hardware to not encrypt when we had sw crypto.
	 * Because we use the same flag to internally indicate that
	 * no (software) encryption should be done, we have to set it
	 * after all crypto handlers.
	 */
	if (tx->key && !(tx->key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
		tx->u.tx.control->flags |= IEEE80211_TXCTL_DO_NOT_ENCRYPT;

	return TXRX_CONTINUE;
}

static ieee80211_txrx_result
ieee80211_tx_h_load_stats(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_local *local = tx->local;
	struct ieee80211_hw_mode *mode = tx->u.tx.mode;
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u32 load = 0, hdrtime;

	/* TODO: this could be part of tx_status handling, so that the number
	 * of retries would be known; TX rate should in that case be stored
	 * somewhere with the packet */

	/* Estimate total channel use caused by this frame */

	/* 1 bit at 1 Mbit/s takes 1 usec; in channel_use values,
	 * 1 usec = 1/8 * (1080 / 10) = 13.5 */

	if (mode->mode == MODE_IEEE80211A ||
	    (mode->mode == MODE_IEEE80211G &&
	     tx->u.tx.rate->flags & IEEE80211_RATE_ERP))
		hdrtime = CHAN_UTIL_HDR_SHORT;
	else
		hdrtime = CHAN_UTIL_HDR_LONG;

	load = hdrtime;
	if (!is_multicast_ether_addr(hdr->addr1))
		load += hdrtime;

	if (tx->u.tx.control->flags & IEEE80211_TXCTL_USE_RTS_CTS)
		load += 2 * hdrtime;
	else if (tx->u.tx.control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		load += hdrtime;

	load += skb->len * tx->u.tx.rate->rate_inv;

	if (tx->u.tx.extra_frag) {
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			load += 2 * hdrtime;
			load += tx->u.tx.extra_frag[i]->len *
				tx->u.tx.rate->rate;
		}
	}

	/* Divide channel_use by 8 to avoid wrapping around the counter */
	load >>= CHAN_UTIL_SHIFT;
	local->channel_use_raw += load;
	if (tx->sta)
		tx->sta->channel_use_raw += load;
	tx->sdata->channel_use_raw += load;

	return TXRX_CONTINUE;
}

/* TODO: implement register/unregister functions for adding TX/RX handlers
 * into ordered list */

ieee80211_tx_handler ieee80211_tx_handlers[] =
{
	ieee80211_tx_h_check_assoc,
	ieee80211_tx_h_sequence,
	ieee80211_tx_h_ps_buf,
	ieee80211_tx_h_select_key,
	ieee80211_tx_h_michael_mic_add,
	ieee80211_tx_h_fragment,
	ieee80211_tx_h_encrypt,
	ieee80211_tx_h_rate_ctrl,
	ieee80211_tx_h_misc,
	ieee80211_tx_h_load_stats,
	NULL
};

/* actual transmit path */

/*
 * deal with packet injection down monitor interface
 * with Radiotap Header -- only called for monitor mode interface
 */
static ieee80211_txrx_result
__ieee80211_parse_tx_radiotap(struct ieee80211_txrx_data *tx,
			      struct sk_buff *skb)
{
	/*
	 * this is the moment to interpret and discard the radiotap header that
	 * must be at the start of the packet injected in Monitor mode
	 *
	 * Need to take some care with endian-ness since radiotap
	 * args are little-endian
	 */

	struct ieee80211_radiotap_iterator iterator;
	struct ieee80211_radiotap_header *rthdr =
		(struct ieee80211_radiotap_header *) skb->data;
	struct ieee80211_hw_mode *mode = tx->local->hw.conf.mode;
	int ret = ieee80211_radiotap_iterator_init(&iterator, rthdr, skb->len);
	struct ieee80211_tx_control *control = tx->u.tx.control;

	control->flags |= IEEE80211_TXCTL_DO_NOT_ENCRYPT;
	tx->flags |= IEEE80211_TXRXD_TX_INJECTED;
	tx->flags &= ~IEEE80211_TXRXD_FRAGMENTED;

	/*
	 * for every radiotap entry that is present
	 * (ieee80211_radiotap_iterator_next returns -ENOENT when no more
	 * entries present, or -EINVAL on error)
	 */

	while (!ret) {
		int i, target_rate;

		ret = ieee80211_radiotap_iterator_next(&iterator);

		if (ret)
			continue;

		/* see if this argument is something we can use */
		switch (iterator.this_arg_index) {
		/*
		 * You must take care when dereferencing iterator.this_arg
		 * for multibyte types... the pointer is not aligned.  Use
		 * get_unaligned((type *)iterator.this_arg) to dereference
		 * iterator.this_arg for type "type" safely on all arches.
		*/
		case IEEE80211_RADIOTAP_RATE:
			/*
			 * radiotap rate u8 is in 500kbps units eg, 0x02=1Mbps
			 * ieee80211 rate int is in 100kbps units eg, 0x0a=1Mbps
			 */
			target_rate = (*iterator.this_arg) * 5;
			for (i = 0; i < mode->num_rates; i++) {
				struct ieee80211_rate *r = &mode->rates[i];

				if (r->rate == target_rate) {
					tx->u.tx.rate = r;
					break;
				}
			}
			break;

		case IEEE80211_RADIOTAP_ANTENNA:
			/*
			 * radiotap uses 0 for 1st ant, mac80211 is 1 for
			 * 1st ant
			 */
			control->antenna_sel_tx = (*iterator.this_arg) + 1;
			break;

		case IEEE80211_RADIOTAP_DBM_TX_POWER:
			control->power_level = *iterator.this_arg;
			break;

		case IEEE80211_RADIOTAP_FLAGS:
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FCS) {
				/*
				 * this indicates that the skb we have been
				 * handed has the 32-bit FCS CRC at the end...
				 * we should react to that by snipping it off
				 * because it will be recomputed and added
				 * on transmission
				 */
				if (skb->len < (iterator.max_length + FCS_LEN))
					return TXRX_DROP;

				skb_trim(skb, skb->len - FCS_LEN);
			}
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_WEP)
				control->flags &=
					~IEEE80211_TXCTL_DO_NOT_ENCRYPT;
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FRAG)
				tx->flags |= IEEE80211_TXRXD_FRAGMENTED;
			break;

		/*
		 * Please update the file
		 * Documentation/networking/mac80211-injection.txt
		 * when parsing new fields here.
		 */

		default:
			break;
		}
	}

	if (ret != -ENOENT) /* ie, if we didn't simply run out of fields */
		return TXRX_DROP;

	/*
	 * remove the radiotap header
	 * iterator->max_length was sanity-checked against
	 * skb->len by iterator init
	 */
	skb_pull(skb, iterator.max_length);

	return TXRX_CONTINUE;
}

/*
 * initialises @tx
 */
static ieee80211_txrx_result
__ieee80211_tx_prepare(struct ieee80211_txrx_data *tx,
		       struct sk_buff *skb,
		       struct net_device *dev,
		       struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr;
	struct ieee80211_sub_if_data *sdata;
	ieee80211_txrx_result res = TXRX_CONTINUE;

	int hdrlen;

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->dev = dev; /* use original interface */
	tx->local = local;
	tx->sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	tx->u.tx.control = control;
	/*
	 * Set this flag (used below to indicate "automatic fragmentation"),
	 * it will be cleared/left by radiotap as desired.
	 */
	tx->flags |= IEEE80211_TXRXD_FRAGMENTED;

	/* process and remove the injection radiotap header */
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (unlikely(sdata->type == IEEE80211_IF_TYPE_MNTR)) {
		if (__ieee80211_parse_tx_radiotap(tx, skb) == TXRX_DROP)
			return TXRX_DROP;

		/*
		 * __ieee80211_parse_tx_radiotap has now removed
		 * the radiotap header that was present and pre-filled
		 * 'tx' with tx control information.
		 */
	}

	hdr = (struct ieee80211_hdr *) skb->data;

	tx->sta = sta_info_get(local, hdr->addr1);
	tx->fc = le16_to_cpu(hdr->frame_control);

	if (is_multicast_ether_addr(hdr->addr1)) {
		tx->flags &= ~IEEE80211_TXRXD_TXUNICAST;
		control->flags |= IEEE80211_TXCTL_NO_ACK;
	} else {
		tx->flags |= IEEE80211_TXRXD_TXUNICAST;
		control->flags &= ~IEEE80211_TXCTL_NO_ACK;
	}

	if (tx->flags & IEEE80211_TXRXD_FRAGMENTED) {
		if ((tx->flags & IEEE80211_TXRXD_TXUNICAST) &&
		    skb->len + FCS_LEN > local->fragmentation_threshold &&
		    !local->ops->set_frag_threshold)
			tx->flags |= IEEE80211_TXRXD_FRAGMENTED;
		else
			tx->flags &= ~IEEE80211_TXRXD_FRAGMENTED;
	}

	if (!tx->sta)
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
	else if (tx->sta->clear_dst_mask) {
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
		tx->sta->clear_dst_mask = 0;
	}

	hdrlen = ieee80211_get_hdrlen(tx->fc);
	if (skb->len > hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *pos = &skb->data[hdrlen + sizeof(rfc1042_header)];
		tx->ethertype = (pos[0] << 8) | pos[1];
	}
	control->flags |= IEEE80211_TXCTL_FIRST_FRAGMENT;

	return res;
}

/* Device in tx->dev has a reference added; use dev_put(tx->dev) when
 * finished with it.
 *
 * NB: @tx is uninitialised when passed in here
 */
static int ieee80211_tx_prepare(struct ieee80211_txrx_data *tx,
				struct sk_buff *skb,
				struct net_device *mdev,
				struct ieee80211_tx_control *control)
{
	struct ieee80211_tx_packet_data *pkt_data;
	struct net_device *dev;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	dev = dev_get_by_index(&init_net, pkt_data->ifindex);
	if (unlikely(dev && !is_ieee80211_device(dev, mdev))) {
		dev_put(dev);
		dev = NULL;
	}
	if (unlikely(!dev))
		return -ENODEV;
	/* initialises tx with control */
	__ieee80211_tx_prepare(tx, skb, dev, control);
	return 0;
}

static int __ieee80211_tx(struct ieee80211_local *local, struct sk_buff *skb,
			  struct ieee80211_txrx_data *tx)
{
	struct ieee80211_tx_control *control = tx->u.tx.control;
	int ret, i;

	if (!ieee80211_qdisc_installed(local->mdev) &&
	    __ieee80211_queue_stopped(local, 0)) {
		netif_stop_queue(local->mdev);
		return IEEE80211_TX_AGAIN;
	}
	if (skb) {
		ieee80211_dump_frame(wiphy_name(local->hw.wiphy),
				     "TX to low-level driver", skb);
		ret = local->ops->tx(local_to_hw(local), skb, control);
		if (ret)
			return IEEE80211_TX_AGAIN;
		local->mdev->trans_start = jiffies;
		ieee80211_led_tx(local, 1);
	}
	if (tx->u.tx.extra_frag) {
		control->flags &= ~(IEEE80211_TXCTL_USE_RTS_CTS |
				    IEEE80211_TXCTL_USE_CTS_PROTECT |
				    IEEE80211_TXCTL_CLEAR_DST_MASK |
				    IEEE80211_TXCTL_FIRST_FRAGMENT);
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			if (!tx->u.tx.extra_frag[i])
				continue;
			if (__ieee80211_queue_stopped(local, control->queue))
				return IEEE80211_TX_FRAG_AGAIN;
			if (i == tx->u.tx.num_extra_frag) {
				control->tx_rate = tx->u.tx.last_frag_hwrate;
				control->rate = tx->u.tx.last_frag_rate;
				if (tx->flags & IEEE80211_TXRXD_TXPROBE_LAST_FRAG)
					control->flags |=
						IEEE80211_TXCTL_RATE_CTRL_PROBE;
				else
					control->flags &=
						~IEEE80211_TXCTL_RATE_CTRL_PROBE;
			}

			ieee80211_dump_frame(wiphy_name(local->hw.wiphy),
					     "TX to low-level driver",
					     tx->u.tx.extra_frag[i]);
			ret = local->ops->tx(local_to_hw(local),
					    tx->u.tx.extra_frag[i],
					    control);
			if (ret)
				return IEEE80211_TX_FRAG_AGAIN;
			local->mdev->trans_start = jiffies;
			ieee80211_led_tx(local, 1);
			tx->u.tx.extra_frag[i] = NULL;
		}
		kfree(tx->u.tx.extra_frag);
		tx->u.tx.extra_frag = NULL;
	}
	return IEEE80211_TX_OK;
}

static int ieee80211_tx(struct net_device *dev, struct sk_buff *skb,
			struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	ieee80211_tx_handler *handler;
	struct ieee80211_txrx_data tx;
	ieee80211_txrx_result res = TXRX_DROP, res_prepare;
	int ret, i;

	WARN_ON(__ieee80211_queue_pending(local, control->queue));

	if (unlikely(skb->len < 10)) {
		dev_kfree_skb(skb);
		return 0;
	}

	/* initialises tx */
	res_prepare = __ieee80211_tx_prepare(&tx, skb, dev, control);

	if (res_prepare == TXRX_DROP) {
		dev_kfree_skb(skb);
		return 0;
	}

	/*
	 * key references are protected using RCU and this requires that
	 * we are in a read-site RCU section during receive processing
	 */
	rcu_read_lock();

	sta = tx.sta;
	tx.u.tx.mode = local->hw.conf.mode;

	for (handler = local->tx_handlers; *handler != NULL;
	     handler++) {
		res = (*handler)(&tx);
		if (res != TXRX_CONTINUE)
			break;
	}

	skb = tx.skb; /* handlers are allowed to change skb */

	if (sta)
		sta_info_put(sta);

	if (unlikely(res == TXRX_DROP)) {
		I802_DEBUG_INC(local->tx_handlers_drop);
		goto drop;
	}

	if (unlikely(res == TXRX_QUEUED)) {
		I802_DEBUG_INC(local->tx_handlers_queued);
		rcu_read_unlock();
		return 0;
	}

	if (tx.u.tx.extra_frag) {
		for (i = 0; i < tx.u.tx.num_extra_frag; i++) {
			int next_len, dur;
			struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *)
				tx.u.tx.extra_frag[i]->data;

			if (i + 1 < tx.u.tx.num_extra_frag) {
				next_len = tx.u.tx.extra_frag[i + 1]->len;
			} else {
				next_len = 0;
				tx.u.tx.rate = tx.u.tx.last_frag_rate;
				tx.u.tx.last_frag_hwrate = tx.u.tx.rate->val;
			}
			dur = ieee80211_duration(&tx, 0, next_len);
			hdr->duration_id = cpu_to_le16(dur);
		}
	}

retry:
	ret = __ieee80211_tx(local, skb, &tx);
	if (ret) {
		struct ieee80211_tx_stored_packet *store =
			&local->pending_packet[control->queue];

		if (ret == IEEE80211_TX_FRAG_AGAIN)
			skb = NULL;
		set_bit(IEEE80211_LINK_STATE_PENDING,
			&local->state[control->queue]);
		smp_mb();
		/* When the driver gets out of buffers during sending of
		 * fragments and calls ieee80211_stop_queue, there is
		 * a small window between IEEE80211_LINK_STATE_XOFF and
		 * IEEE80211_LINK_STATE_PENDING flags are set. If a buffer
		 * gets available in that window (i.e. driver calls
		 * ieee80211_wake_queue), we would end up with ieee80211_tx
		 * called with IEEE80211_LINK_STATE_PENDING. Prevent this by
		 * continuing transmitting here when that situation is
		 * possible to have happened. */
		if (!__ieee80211_queue_stopped(local, control->queue)) {
			clear_bit(IEEE80211_LINK_STATE_PENDING,
				  &local->state[control->queue]);
			goto retry;
		}
		memcpy(&store->control, control,
		       sizeof(struct ieee80211_tx_control));
		store->skb = skb;
		store->extra_frag = tx.u.tx.extra_frag;
		store->num_extra_frag = tx.u.tx.num_extra_frag;
		store->last_frag_hwrate = tx.u.tx.last_frag_hwrate;
		store->last_frag_rate = tx.u.tx.last_frag_rate;
		store->last_frag_rate_ctrl_probe =
			!!(tx.flags & IEEE80211_TXRXD_TXPROBE_LAST_FRAG);
	}
	rcu_read_unlock();
	return 0;

 drop:
	if (skb)
		dev_kfree_skb(skb);
	for (i = 0; i < tx.u.tx.num_extra_frag; i++)
		if (tx.u.tx.extra_frag[i])
			dev_kfree_skb(tx.u.tx.extra_frag[i]);
	kfree(tx.u.tx.extra_frag);
	rcu_read_unlock();
	return 0;
}

/* device xmit handlers */

int ieee80211_master_start_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct ieee80211_tx_control control;
	struct ieee80211_tx_packet_data *pkt_data;
	struct net_device *odev = NULL;
	struct ieee80211_sub_if_data *osdata;
	int headroom;
	int ret;

	/*
	 * copy control out of the skb so other people can use skb->cb
	 */
	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	memset(&control, 0, sizeof(struct ieee80211_tx_control));

	if (pkt_data->ifindex)
		odev = dev_get_by_index(&init_net, pkt_data->ifindex);
	if (unlikely(odev && !is_ieee80211_device(odev, dev))) {
		dev_put(odev);
		odev = NULL;
	}
	if (unlikely(!odev)) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		printk(KERN_DEBUG "%s: Discarded packet with nonexistent "
		       "originating device\n", dev->name);
#endif
		dev_kfree_skb(skb);
		return 0;
	}
	osdata = IEEE80211_DEV_TO_SUB_IF(odev);

	headroom = osdata->local->tx_headroom + IEEE80211_ENCRYPT_HEADROOM;
	if (skb_headroom(skb) < headroom) {
		if (pskb_expand_head(skb, headroom, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			dev_put(odev);
			return 0;
		}
	}

	control.ifindex = odev->ifindex;
	control.type = osdata->type;
	if (pkt_data->flags & IEEE80211_TXPD_REQ_TX_STATUS)
		control.flags |= IEEE80211_TXCTL_REQ_TX_STATUS;
	if (pkt_data->flags & IEEE80211_TXPD_DO_NOT_ENCRYPT)
		control.flags |= IEEE80211_TXCTL_DO_NOT_ENCRYPT;
	if (pkt_data->flags & IEEE80211_TXPD_REQUEUE)
		control.flags |= IEEE80211_TXCTL_REQUEUE;
	control.queue = pkt_data->queue;

	ret = ieee80211_tx(odev, skb, &control);
	dev_put(odev);

	return ret;
}

int ieee80211_monitor_start_xmit(struct sk_buff *skb,
				 struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_packet_data *pkt_data;
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	u16 len_rthdr;

	/* check for not even having the fixed radiotap header part */
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail; /* too short to be possibly valid */

	/* is it a header version we can trust to find length from? */
	if (unlikely(prthdr->it_version))
		goto fail; /* only version 0 is supported */

	/* then there must be a radiotap header with a length we can use */
	len_rthdr = ieee80211_get_radiotap_len(skb->data);

	/* does the skb contain enough to deliver on the alleged length? */
	if (unlikely(skb->len < len_rthdr))
		goto fail; /* skb too short for claimed rt header extent */

	skb->dev = local->mdev;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	memset(pkt_data, 0, sizeof(*pkt_data));
	/* needed because we set skb device to master */
	pkt_data->ifindex = dev->ifindex;

	pkt_data->flags |= IEEE80211_TXPD_DO_NOT_ENCRYPT;

	/*
	 * fix up the pointers accounting for the radiotap
	 * header still being in there.  We are being given
	 * a precooked IEEE80211 header so no need for
	 * normal processing
	 */
	skb_set_mac_header(skb, len_rthdr);
	/*
	 * these are just fixed to the end of the rt area since we
	 * don't have any better information and at this point, nobody cares
	 */
	skb_set_network_header(skb, len_rthdr);
	skb_set_transport_header(skb, len_rthdr);

	/* pass the radiotap header up to the next stage intact */
	dev_queue_xmit(skb);
	return NETDEV_TX_OK;

fail:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK; /* meaning, we dealt with the skb */
}

/**
 * ieee80211_subif_start_xmit - netif start_xmit function for Ethernet-type
 * subinterfaces (wlan#, WDS, and VLAN interfaces)
 * @skb: packet to be sent
 * @dev: incoming interface
 *
 * Returns: 0 on success (and frees skb in this case) or 1 on failure (skb will
 * not be freed, and caller is responsible for either retrying later or freeing
 * skb).
 *
 * This function takes in an Ethernet header and encapsulates it with suitable
 * IEEE 802.11 header based on which interface the packet is coming in. The
 * encapsulated packet will then be passed to master interface, wlan#.11, for
 * transmission (through low-level driver).
 */
int ieee80211_subif_start_xmit(struct sk_buff *skb,
			       struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_packet_data *pkt_data;
	struct ieee80211_sub_if_data *sdata;
	int ret = 1, head_need;
	u16 ethertype, hdrlen, fc;
	struct ieee80211_hdr hdr;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	int nh_pos, h_pos;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (unlikely(skb->len < ETH_HLEN)) {
		printk(KERN_DEBUG "%s: short skb (len=%d)\n",
		       dev->name, skb->len);
		ret = 0;
		goto fail;
	}

	nh_pos = skb_network_header(skb) - skb->data;
	h_pos = skb_transport_header(skb) - skb->data;

	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];
	/* TODO: handling for 802.1x authorized/unauthorized port */
	fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA;

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_AP:
	case IEEE80211_IF_TYPE_VLAN:
		fc |= IEEE80211_FCTL_FROMDS;
		/* DA BSSID SA */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 24;
		break;
	case IEEE80211_IF_TYPE_WDS:
		fc |= IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS;
		/* RA TA DA SA */
		memcpy(hdr.addr1, sdata->u.wds.remote_addr, ETH_ALEN);
		memcpy(hdr.addr2, dev->dev_addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		memcpy(hdr.addr4, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 30;
		break;
	case IEEE80211_IF_TYPE_STA:
		fc |= IEEE80211_FCTL_TODS;
		/* BSSID SA DA */
		memcpy(hdr.addr1, sdata->u.sta.bssid, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		hdrlen = 24;
		break;
	case IEEE80211_IF_TYPE_IBSS:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, sdata->u.sta.bssid, ETH_ALEN);
		hdrlen = 24;
		break;
	default:
		ret = 0;
		goto fail;
	}

	/* receiver is QoS enabled, use a QoS type frame */
	sta = sta_info_get(local, hdr.addr1);
	if (sta) {
		if (sta->flags & WLAN_STA_WME) {
			fc |= IEEE80211_STYPE_QOS_DATA;
			hdrlen += 2;
		}
		sta_info_put(sta);
	}

	hdr.frame_control = cpu_to_le16(fc);
	hdr.duration_id = 0;
	hdr.seq_ctrl = 0;

	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= 0x600) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	} else {
		encaps_data = NULL;
		encaps_len = 0;
	}

	skb_pull(skb, skip_header_bytes);
	nh_pos -= skip_header_bytes;
	h_pos -= skip_header_bytes;

	/* TODO: implement support for fragments so that there is no need to
	 * reallocate and copy payload; it might be enough to support one
	 * extra fragment that would be copied in the beginning of the frame
	 * data.. anyway, it would be nice to include this into skb structure
	 * somehow
	 *
	 * There are few options for this:
	 * use skb->cb as an extra space for 802.11 header
	 * allocate new buffer if not enough headroom
	 * make sure that there is enough headroom in every skb by increasing
	 * build in headroom in __dev_alloc_skb() (linux/skbuff.h) and
	 * alloc_skb() (net/core/skbuff.c)
	 */
	head_need = hdrlen + encaps_len + local->tx_headroom;
	head_need -= skb_headroom(skb);

	/* We are going to modify skb data, so make a copy of it if happens to
	 * be cloned. This could happen, e.g., with Linux bridge code passing
	 * us broadcast frames. */

	if (head_need > 0 || skb_cloned(skb)) {
#if 0
		printk(KERN_DEBUG "%s: need to reallocate buffer for %d bytes "
		       "of headroom\n", dev->name, head_need);
#endif

		if (skb_cloned(skb))
			I802_DEBUG_INC(local->tx_expand_skb_head_cloned);
		else
			I802_DEBUG_INC(local->tx_expand_skb_head);
		/* Since we have to reallocate the buffer, make sure that there
		 * is enough room for possible WEP IV/ICV and TKIP (8 bytes
		 * before payload and 12 after). */
		if (pskb_expand_head(skb, (head_need > 0 ? head_need + 8 : 8),
				     12, GFP_ATOMIC)) {
			printk(KERN_DEBUG "%s: failed to reallocate TX buffer"
			       "\n", dev->name);
			goto fail;
		}
	}

	if (encaps_data) {
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);
		nh_pos += encaps_len;
		h_pos += encaps_len;
	}

	if (fc & IEEE80211_STYPE_QOS_DATA) {
		__le16 *qos_control;

		qos_control = (__le16*) skb_push(skb, 2);
		memcpy(skb_push(skb, hdrlen - 2), &hdr, hdrlen - 2);
		/*
		 * Maybe we could actually set some fields here, for now just
		 * initialise to zero to indicate no special operation.
		 */
		*qos_control = 0;
	} else
		memcpy(skb_push(skb, hdrlen), &hdr, hdrlen);

	nh_pos += hdrlen;
	h_pos += hdrlen;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	memset(pkt_data, 0, sizeof(struct ieee80211_tx_packet_data));
	pkt_data->ifindex = dev->ifindex;

	skb->dev = local->mdev;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* Update skb pointers to various headers since this modified frame
	 * is going to go through Linux networking code that may potentially
	 * need things like pointer to IP header. */
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, nh_pos);
	skb_set_transport_header(skb, h_pos);

	dev->trans_start = jiffies;
	dev_queue_xmit(skb);

	return 0;

 fail:
	if (!ret)
		dev_kfree_skb(skb);

	return ret;
}

/*
 * This is the transmit routine for the 802.11 type interfaces
 * called by upper layers of the linux networking
 * stack when it has a frame to transmit
 */
int ieee80211_mgmt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_packet_data *pkt_data;
	struct ieee80211_hdr *hdr;
	u16 fc;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (skb->len < 10) {
		dev_kfree_skb(skb);
		return 0;
	}

	if (skb_headroom(skb) < sdata->local->tx_headroom) {
		if (pskb_expand_head(skb, sdata->local->tx_headroom,
				     0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return 0;
		}
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
	memset(pkt_data, 0, sizeof(struct ieee80211_tx_packet_data));
	pkt_data->ifindex = sdata->dev->ifindex;

	skb->priority = 20; /* use hardcoded priority for mgmt TX queue */
	skb->dev = sdata->local->mdev;

	/*
	 * We're using the protocol field of the the frame control header
	 * to request TX callback for hostapd. BIT(1) is checked.
	 */
	if ((fc & BIT(1)) == BIT(1)) {
		pkt_data->flags |= IEEE80211_TXPD_REQ_TX_STATUS;
		fc &= ~BIT(1);
		hdr->frame_control = cpu_to_le16(fc);
	}

	if (!(fc & IEEE80211_FCTL_PROTECTED))
		pkt_data->flags |= IEEE80211_TXPD_DO_NOT_ENCRYPT;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	dev_queue_xmit(skb);

	return 0;
}

/* helper functions for pending packets for when queues are stopped */

void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	int i, j;
	struct ieee80211_tx_stored_packet *store;

	for (i = 0; i < local->hw.queues; i++) {
		if (!__ieee80211_queue_pending(local, i))
			continue;
		store = &local->pending_packet[i];
		kfree_skb(store->skb);
		for (j = 0; j < store->num_extra_frag; j++)
			kfree_skb(store->extra_frag[j]);
		kfree(store->extra_frag);
		clear_bit(IEEE80211_LINK_STATE_PENDING, &local->state[i]);
	}
}

void ieee80211_tx_pending(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *)data;
	struct net_device *dev = local->mdev;
	struct ieee80211_tx_stored_packet *store;
	struct ieee80211_txrx_data tx;
	int i, ret, reschedule = 0;

	netif_tx_lock_bh(dev);
	for (i = 0; i < local->hw.queues; i++) {
		if (__ieee80211_queue_stopped(local, i))
			continue;
		if (!__ieee80211_queue_pending(local, i)) {
			reschedule = 1;
			continue;
		}
		store = &local->pending_packet[i];
		tx.u.tx.control = &store->control;
		tx.u.tx.extra_frag = store->extra_frag;
		tx.u.tx.num_extra_frag = store->num_extra_frag;
		tx.u.tx.last_frag_hwrate = store->last_frag_hwrate;
		tx.u.tx.last_frag_rate = store->last_frag_rate;
		tx.flags = 0;
		if (store->last_frag_rate_ctrl_probe)
			tx.flags |= IEEE80211_TXRXD_TXPROBE_LAST_FRAG;
		ret = __ieee80211_tx(local, store->skb, &tx);
		if (ret) {
			if (ret == IEEE80211_TX_FRAG_AGAIN)
				store->skb = NULL;
		} else {
			clear_bit(IEEE80211_LINK_STATE_PENDING,
				  &local->state[i]);
			reschedule = 1;
		}
	}
	netif_tx_unlock_bh(dev);
	if (reschedule) {
		if (!ieee80211_qdisc_installed(dev)) {
			if (!__ieee80211_queue_stopped(local, 0))
				netif_wake_queue(dev);
		} else
			netif_schedule(dev);
	}
}

/* functions for drivers to get certain frames */

static void ieee80211_beacon_add_tim(struct ieee80211_local *local,
				     struct ieee80211_if_ap *bss,
				     struct sk_buff *skb)
{
	u8 *pos, *tim;
	int aid0 = 0;
	int i, have_bits = 0, n1, n2;

	/* Generate bitmap for TIM only if there are any STAs in power save
	 * mode. */
	read_lock_bh(&local->sta_lock);
	if (atomic_read(&bss->num_sta_ps) > 0)
		/* in the hope that this is faster than
		 * checking byte-for-byte */
		have_bits = !bitmap_empty((unsigned long*)bss->tim,
					  IEEE80211_MAX_AID+1);

	if (bss->dtim_count == 0)
		bss->dtim_count = bss->dtim_period - 1;
	else
		bss->dtim_count--;

	tim = pos = (u8 *) skb_put(skb, 6);
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = bss->dtim_count;
	*pos++ = bss->dtim_period;

	if (bss->dtim_count == 0 && !skb_queue_empty(&bss->ps_bc_buf))
		aid0 = 1;

	if (have_bits) {
		/* Find largest even number N1 so that bits numbered 1 through
		 * (N1 x 8) - 1 in the bitmap are 0 and number N2 so that bits
		 * (N2 + 1) x 8 through 2007 are 0. */
		n1 = 0;
		for (i = 0; i < IEEE80211_MAX_TIM_LEN; i++) {
			if (bss->tim[i]) {
				n1 = i & 0xfe;
				break;
			}
		}
		n2 = n1;
		for (i = IEEE80211_MAX_TIM_LEN - 1; i >= n1; i--) {
			if (bss->tim[i]) {
				n2 = i;
				break;
			}
		}

		/* Bitmap control */
		*pos++ = n1 | aid0;
		/* Part Virt Bitmap */
		memcpy(pos, bss->tim + n1, n2 - n1 + 1);

		tim[1] = n2 - n1 + 4;
		skb_put(skb, n2 - n1);
	} else {
		*pos++ = aid0; /* Bitmap control */
		*pos++ = 0; /* Part Virt Bitmap */
	}
	read_unlock_bh(&local->sta_lock);
}

struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw, int if_id,
				     struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct ieee80211_rate *rate;
	struct rate_control_extra extra;
	u8 *b_head, *b_tail;
	int bh_len, bt_len;

	bdev = dev_get_by_index(&init_net, if_id);
	if (bdev) {
		sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
		ap = &sdata->u.ap;
		dev_put(bdev);
	}

	if (!ap || sdata->type != IEEE80211_IF_TYPE_AP ||
	    !ap->beacon_head) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit())
			printk(KERN_DEBUG "no beacon data avail for idx=%d "
			       "(%s)\n", if_id, bdev ? bdev->name : "N/A");
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
		return NULL;
	}

	/* Assume we are generating the normal beacon locally */
	b_head = ap->beacon_head;
	b_tail = ap->beacon_tail;
	bh_len = ap->beacon_head_len;
	bt_len = ap->beacon_tail_len;

	skb = dev_alloc_skb(local->tx_headroom +
		bh_len + bt_len + 256 /* maximum TIM len */);
	if (!skb)
		return NULL;

	skb_reserve(skb, local->tx_headroom);
	memcpy(skb_put(skb, bh_len), b_head, bh_len);

	ieee80211_include_sequence(sdata, (struct ieee80211_hdr *)skb->data);

	ieee80211_beacon_add_tim(local, ap, skb);

	if (b_tail) {
		memcpy(skb_put(skb, bt_len), b_tail, bt_len);
	}

	if (control) {
		memset(&extra, 0, sizeof(extra));
		extra.mode = local->oper_hw_mode;

		rate = rate_control_get_rate(local, local->mdev, skb, &extra);
		if (!rate) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "%s: ieee80211_beacon_get: no rate "
				       "found\n", wiphy_name(local->hw.wiphy));
			}
			dev_kfree_skb(skb);
			return NULL;
		}

		control->tx_rate =
			((sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE) &&
			(rate->flags & IEEE80211_RATE_PREAMBLE2)) ?
			rate->val2 : rate->val;
		control->antenna_sel_tx = local->hw.conf.antenna_sel_tx;
		control->power_level = local->hw.conf.power_level;
		control->flags |= IEEE80211_TXCTL_NO_ACK;
		control->retry_limit = 1;
		control->flags |= IEEE80211_TXCTL_CLEAR_DST_MASK;
	}

	ap->num_beacons++;
	return skb;
}
EXPORT_SYMBOL(ieee80211_beacon_get);

void ieee80211_rts_get(struct ieee80211_hw *hw, int if_id,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_control *frame_txctl,
		       struct ieee80211_rts *rts)
{
	const struct ieee80211_hdr *hdr = frame;
	u16 fctl;

	fctl = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS;
	rts->frame_control = cpu_to_le16(fctl);
	rts->duration = ieee80211_rts_duration(hw, if_id, frame_len, frame_txctl);
	memcpy(rts->ra, hdr->addr1, sizeof(rts->ra));
	memcpy(rts->ta, hdr->addr2, sizeof(rts->ta));
}
EXPORT_SYMBOL(ieee80211_rts_get);

void ieee80211_ctstoself_get(struct ieee80211_hw *hw, int if_id,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_control *frame_txctl,
			     struct ieee80211_cts *cts)
{
	const struct ieee80211_hdr *hdr = frame;
	u16 fctl;

	fctl = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS;
	cts->frame_control = cpu_to_le16(fctl);
	cts->duration = ieee80211_ctstoself_duration(hw, if_id, frame_len, frame_txctl);
	memcpy(cts->ra, hdr->addr1, sizeof(cts->ra));
}
EXPORT_SYMBOL(ieee80211_ctstoself_get);

struct sk_buff *
ieee80211_get_buffered_bc(struct ieee80211_hw *hw, int if_id,
			  struct ieee80211_tx_control *control)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb;
	struct sta_info *sta;
	ieee80211_tx_handler *handler;
	struct ieee80211_txrx_data tx;
	ieee80211_txrx_result res = TXRX_DROP;
	struct net_device *bdev;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_ap *bss = NULL;

	bdev = dev_get_by_index(&init_net, if_id);
	if (bdev) {
		sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
		bss = &sdata->u.ap;
		dev_put(bdev);
	}
	if (!bss || sdata->type != IEEE80211_IF_TYPE_AP || !bss->beacon_head)
		return NULL;

	if (bss->dtim_count != 0)
		return NULL; /* send buffered bc/mc only after DTIM beacon */
	memset(control, 0, sizeof(*control));
	while (1) {
		skb = skb_dequeue(&bss->ps_bc_buf);
		if (!skb)
			return NULL;
		local->total_ps_buffered--;

		if (!skb_queue_empty(&bss->ps_bc_buf) && skb->len >= 2) {
			struct ieee80211_hdr *hdr =
				(struct ieee80211_hdr *) skb->data;
			/* more buffered multicast/broadcast frames ==> set
			 * MoreData flag in IEEE 802.11 header to inform PS
			 * STAs */
			hdr->frame_control |=
				cpu_to_le16(IEEE80211_FCTL_MOREDATA);
		}

		if (!ieee80211_tx_prepare(&tx, skb, local->mdev, control))
			break;
		dev_kfree_skb_any(skb);
	}
	sta = tx.sta;
	tx.flags |= IEEE80211_TXRXD_TXPS_BUFFERED;
	tx.u.tx.mode = local->hw.conf.mode;

	for (handler = local->tx_handlers; *handler != NULL; handler++) {
		res = (*handler)(&tx);
		if (res == TXRX_DROP || res == TXRX_QUEUED)
			break;
	}
	dev_put(tx.dev);
	skb = tx.skb; /* handlers are allowed to change skb */

	if (res == TXRX_DROP) {
		I802_DEBUG_INC(local->tx_handlers_drop);
		dev_kfree_skb(skb);
		skb = NULL;
	} else if (res == TXRX_QUEUED) {
		I802_DEBUG_INC(local->tx_handlers_queued);
		skb = NULL;
	}

	if (sta)
		sta_info_put(sta);

	return skb;
}
EXPORT_SYMBOL(ieee80211_get_buffered_bc);
