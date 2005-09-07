/*
 * Original code based Host AP (software wireless LAN access point) driver
 * for Intersil Prism2/2.5/3 - hostap.o module, common routines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2004, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/compiler.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>
#include <linux/ctype.h>

#include <net/ieee80211.h>

static inline void ieee80211_monitor_rx(struct ieee80211_device *ieee,
					struct sk_buff *skb,
					struct ieee80211_rx_stats *rx_stats)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u16 fc = le16_to_cpu(hdr->frame_ctl);

	skb->dev = ieee->dev;
	skb->mac.raw = skb->data;
	skb_pull(skb, ieee80211_get_hdrlen(fc));
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = __constant_htons(ETH_P_80211_RAW);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

/* Called only as a tasklet (software IRQ) */
static struct ieee80211_frag_entry *ieee80211_frag_cache_find(struct
							      ieee80211_device
							      *ieee,
							      unsigned int seq,
							      unsigned int frag,
							      u8 * src,
							      u8 * dst)
{
	struct ieee80211_frag_entry *entry;
	int i;

	for (i = 0; i < IEEE80211_FRAG_CACHE_LEN; i++) {
		entry = &ieee->frag_cache[i];
		if (entry->skb != NULL &&
		    time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			IEEE80211_DEBUG_FRAG("expiring fragment cache entry "
					     "seq=%u last_frag=%u\n",
					     entry->seq, entry->last_frag);
			dev_kfree_skb_any(entry->skb);
			entry->skb = NULL;
		}

		if (entry->skb != NULL && entry->seq == seq &&
		    (entry->last_frag + 1 == frag || frag == -1) &&
		    memcmp(entry->src_addr, src, ETH_ALEN) == 0 &&
		    memcmp(entry->dst_addr, dst, ETH_ALEN) == 0)
			return entry;
	}

	return NULL;
}

/* Called only as a tasklet (software IRQ) */
static struct sk_buff *ieee80211_frag_cache_get(struct ieee80211_device *ieee,
						struct ieee80211_hdr *hdr)
{
	struct sk_buff *skb = NULL;
	u16 sc;
	unsigned int frag, seq;
	struct ieee80211_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	seq = WLAN_GET_SEQ_SEQ(sc);

	if (frag == 0) {
		/* Reserve enough space to fit maximum frame length */
		skb = dev_alloc_skb(ieee->dev->mtu +
				    sizeof(struct ieee80211_hdr) +
				    8 /* LLC */  +
				    2 /* alignment */  +
				    8 /* WEP */  + ETH_ALEN /* WDS */ );
		if (skb == NULL)
			return NULL;

		entry = &ieee->frag_cache[ieee->frag_next_idx];
		ieee->frag_next_idx++;
		if (ieee->frag_next_idx >= IEEE80211_FRAG_CACHE_LEN)
			ieee->frag_next_idx = 0;

		if (entry->skb != NULL)
			dev_kfree_skb_any(entry->skb);

		entry->first_frag_time = jiffies;
		entry->seq = seq;
		entry->last_frag = frag;
		entry->skb = skb;
		memcpy(entry->src_addr, hdr->addr2, ETH_ALEN);
		memcpy(entry->dst_addr, hdr->addr1, ETH_ALEN);
	} else {
		/* received a fragment of a frame for which the head fragment
		 * should have already been received */
		entry = ieee80211_frag_cache_find(ieee, seq, frag, hdr->addr2,
						  hdr->addr1);
		if (entry != NULL) {
			entry->last_frag = frag;
			skb = entry->skb;
		}
	}

	return skb;
}

/* Called only as a tasklet (software IRQ) */
static int ieee80211_frag_cache_invalidate(struct ieee80211_device *ieee,
					   struct ieee80211_hdr *hdr)
{
	u16 sc;
	unsigned int seq;
	struct ieee80211_frag_entry *entry;

	sc = le16_to_cpu(hdr->seq_ctl);
	seq = WLAN_GET_SEQ_SEQ(sc);

	entry = ieee80211_frag_cache_find(ieee, seq, -1, hdr->addr2,
					  hdr->addr1);

	if (entry == NULL) {
		IEEE80211_DEBUG_FRAG("could not invalidate fragment cache "
				     "entry (seq=%u)\n", seq);
		return -1;
	}

	entry->skb = NULL;
	return 0;
}

#ifdef NOT_YET
/* ieee80211_rx_frame_mgtmt
 *
 * Responsible for handling management control frames
 *
 * Called by ieee80211_rx */
static inline int
ieee80211_rx_frame_mgmt(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats, u16 type,
			u16 stype)
{
	if (ieee->iw_mode == IW_MODE_MASTER) {
		printk(KERN_DEBUG "%s: Master mode not yet suppported.\n",
		       ieee->dev->name);
		return 0;
/*
  hostap_update_sta_ps(ieee, (struct hostap_ieee80211_hdr *)
  skb->data);*/
	}

	if (ieee->hostapd && type == WLAN_FC_TYPE_MGMT) {
		if (stype == WLAN_FC_STYPE_BEACON &&
		    ieee->iw_mode == IW_MODE_MASTER) {
			struct sk_buff *skb2;
			/* Process beacon frames also in kernel driver to
			 * update STA(AP) table statistics */
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2)
				hostap_rx(skb2->dev, skb2, rx_stats);
		}

		/* send management frames to the user space daemon for
		 * processing */
		ieee->apdevstats.rx_packets++;
		ieee->apdevstats.rx_bytes += skb->len;
		prism2_rx_80211(ieee->apdev, skb, rx_stats, PRISM2_RX_MGMT);
		return 0;
	}

	if (ieee->iw_mode == IW_MODE_MASTER) {
		if (type != WLAN_FC_TYPE_MGMT && type != WLAN_FC_TYPE_CTRL) {
			printk(KERN_DEBUG "%s: unknown management frame "
			       "(type=0x%02x, stype=0x%02x) dropped\n",
			       skb->dev->name, type, stype);
			return -1;
		}

		hostap_rx(skb->dev, skb, rx_stats);
		return 0;
	}

	printk(KERN_DEBUG "%s: hostap_rx_frame_mgmt: management frame "
	       "received in non-Host AP mode\n", skb->dev->name);
	return -1;
}
#endif

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
static unsigned char rfc1042_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static unsigned char bridge_tunnel_header[] =
    { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
/* No encapsulation header if EtherType < 0x600 (=length) */

/* Called by ieee80211_rx_frame_decrypt */
static int ieee80211_is_eapol_frame(struct ieee80211_device *ieee,
				    struct sk_buff *skb)
{
	struct net_device *dev = ieee->dev;
	u16 fc, ethertype;
	struct ieee80211_hdr *hdr;
	u8 *pos;

	if (skb->len < 24)
		return 0;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = le16_to_cpu(hdr->frame_ctl);

	/* check that the frame is unicast frame to us */
	if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_TODS &&
	    memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0 &&
	    memcmp(hdr->addr3, dev->dev_addr, ETH_ALEN) == 0) {
		/* ToDS frame with own addr BSSID and DA */
	} else if ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		   IEEE80211_FCTL_FROMDS &&
		   memcmp(hdr->addr1, dev->dev_addr, ETH_ALEN) == 0) {
		/* FromDS frame with own addr as DA */
	} else
		return 0;

	if (skb->len < 24 + 8)
		return 0;

	/* check for port access entity Ethernet type */
	pos = skb->data + 24;
	ethertype = (pos[6] << 8) | pos[7];
	if (ethertype == ETH_P_PAE)
		return 1;

	return 0;
}

/* Called only as a tasklet (software IRQ), by ieee80211_rx */
static inline int
ieee80211_rx_frame_decrypt(struct ieee80211_device *ieee, struct sk_buff *skb,
			   struct ieee80211_crypt_data *crypt)
{
	struct ieee80211_hdr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_mpdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

#ifdef CONFIG_IEEE80211_CRYPT_TKIP
	if (ieee->tkip_countermeasures && strcmp(crypt->ops->name, "TKIP") == 0) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: TKIP countermeasures: dropped "
			       "received packet from " MAC_FMT "\n",
			       ieee->dev->name, MAC_ARG(hdr->addr2));
		}
		return -1;
	}
#endif

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_mpdu(skb, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		IEEE80211_DEBUG_DROP("decryption failed (SA=" MAC_FMT
				     ") res=%d\n", MAC_ARG(hdr->addr2), res);
		if (res == -2)
			IEEE80211_DEBUG_DROP("Decryption failed ICV "
					     "mismatch (key %d)\n",
					     skb->data[hdrlen + 3] >> 6);
		ieee->ieee_stats.rx_discards_undecryptable++;
		return -1;
	}

	return res;
}

/* Called only as a tasklet (software IRQ), by ieee80211_rx */
static inline int
ieee80211_rx_frame_decrypt_msdu(struct ieee80211_device *ieee,
				struct sk_buff *skb, int keyidx,
				struct ieee80211_crypt_data *crypt)
{
	struct ieee80211_hdr *hdr;
	int res, hdrlen;

	if (crypt == NULL || crypt->ops->decrypt_msdu == NULL)
		return 0;

	hdr = (struct ieee80211_hdr *)skb->data;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl));

	atomic_inc(&crypt->refcnt);
	res = crypt->ops->decrypt_msdu(skb, keyidx, hdrlen, crypt->priv);
	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_DEBUG "%s: MSDU decryption/MIC verification failed"
		       " (SA=" MAC_FMT " keyidx=%d)\n",
		       ieee->dev->name, MAC_ARG(hdr->addr2), keyidx);
		return -1;
	}

	return 0;
}

/* All received frames are sent to this function. @skb contains the frame in
 * IEEE 802.11 format, i.e., in the format it was sent over air.
 * This function is called only as a tasklet (software IRQ). */
int ieee80211_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
		 struct ieee80211_rx_stats *rx_stats)
{
	struct net_device *dev = ieee->dev;
	struct ieee80211_hdr *hdr;
	size_t hdrlen;
	u16 fc, type, stype, sc;
	struct net_device_stats *stats;
	unsigned int frag;
	u8 *payload;
	u16 ethertype;
#ifdef NOT_YET
	struct net_device *wds = NULL;
	struct sk_buff *skb2 = NULL;
	struct net_device *wds = NULL;
	int frame_authorized = 0;
	int from_assoc_ap = 0;
	void *sta = NULL;
#endif
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	struct ieee80211_crypt_data *crypt = NULL;
	int keyidx = 0;

	hdr = (struct ieee80211_hdr *)skb->data;
	stats = &ieee->stats;

	if (skb->len < 10) {
		printk(KERN_INFO "%s: SKB length < 10\n", dev->name);
		goto rx_dropped;
	}

	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	hdrlen = ieee80211_get_hdrlen(fc);

#ifdef NOT_YET
#if WIRELESS_EXT > 15
	/* Put this code here so that we avoid duplicating it in all
	 * Rx paths. - Jean II */
#ifdef IW_WIRELESS_SPY		/* defined in iw_handler.h */
	/* If spy monitoring on */
	if (iface->spy_data.spy_number > 0) {
		struct iw_quality wstats;
		wstats.level = rx_stats->signal;
		wstats.noise = rx_stats->noise;
		wstats.updated = 6;	/* No qual value */
		/* Update spy records */
		wireless_spy_update(dev, hdr->addr2, &wstats);
	}
#endif				/* IW_WIRELESS_SPY */
#endif				/* WIRELESS_EXT > 15 */
	hostap_update_rx_stats(local->ap, hdr, rx_stats);
#endif

#if WIRELESS_EXT > 15
	if (ieee->iw_mode == IW_MODE_MONITOR) {
		ieee80211_monitor_rx(ieee, skb, rx_stats);
		stats->rx_packets++;
		stats->rx_bytes += skb->len;
		return 1;
	}
#endif

	if (ieee->host_decrypt) {
		int idx = 0;
		if (skb->len >= hdrlen + 3)
			idx = skb->data[hdrlen + 3] >> 6;
		crypt = ieee->crypt[idx];
#ifdef NOT_YET
		sta = NULL;

		/* Use station specific key to override default keys if the
		 * receiver address is a unicast address ("individual RA"). If
		 * bcrx_sta_key parameter is set, station specific key is used
		 * even with broad/multicast targets (this is against IEEE
		 * 802.11, but makes it easier to use different keys with
		 * stations that do not support WEP key mapping). */

		if (!(hdr->addr1[0] & 0x01) || local->bcrx_sta_key)
			(void)hostap_handle_sta_crypto(local, hdr, &crypt,
						       &sta);
#endif

		/* allow NULL decrypt to indicate an station specific override
		 * for default encryption */
		if (crypt && (crypt->ops == NULL ||
			      crypt->ops->decrypt_mpdu == NULL))
			crypt = NULL;

		if (!crypt && (fc & IEEE80211_FCTL_PROTECTED)) {
			/* This seems to be triggered by some (multicast?)
			 * frames from other than current BSS, so just drop the
			 * frames silently instead of filling system log with
			 * these reports. */
			IEEE80211_DEBUG_DROP("Decryption failed (not set)"
					     " (SA=" MAC_FMT ")\n",
					     MAC_ARG(hdr->addr2));
			ieee->ieee_stats.rx_discards_undecryptable++;
			goto rx_dropped;
		}
	}
#ifdef NOT_YET
	if (type != WLAN_FC_TYPE_DATA) {
		if (type == WLAN_FC_TYPE_MGMT && stype == WLAN_FC_STYPE_AUTH &&
		    fc & IEEE80211_FCTL_PROTECTED && ieee->host_decrypt &&
		    (keyidx = hostap_rx_frame_decrypt(ieee, skb, crypt)) < 0) {
			printk(KERN_DEBUG "%s: failed to decrypt mgmt::auth "
			       "from " MAC_FMT "\n", dev->name,
			       MAC_ARG(hdr->addr2));
			/* TODO: could inform hostapd about this so that it
			 * could send auth failure report */
			goto rx_dropped;
		}

		if (ieee80211_rx_frame_mgmt(ieee, skb, rx_stats, type, stype))
			goto rx_dropped;
		else
			goto rx_exit;
	}
#endif

	/* Data frame - extract src/dst addresses */
	if (skb->len < IEEE80211_3ADDR_LEN)
		goto rx_dropped;

	switch (fc & (IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
	case IEEE80211_FCTL_FROMDS:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr3, ETH_ALEN);
		break;
	case IEEE80211_FCTL_TODS:
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		break;
	case IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS:
		if (skb->len < IEEE80211_4ADDR_LEN)
			goto rx_dropped;
		memcpy(dst, hdr->addr3, ETH_ALEN);
		memcpy(src, hdr->addr4, ETH_ALEN);
		break;
	case 0:
		memcpy(dst, hdr->addr1, ETH_ALEN);
		memcpy(src, hdr->addr2, ETH_ALEN);
		break;
	}

#ifdef NOT_YET
	if (hostap_rx_frame_wds(ieee, hdr, fc, &wds))
		goto rx_dropped;
	if (wds) {
		skb->dev = dev = wds;
		stats = hostap_get_stats(dev);
	}

	if (ieee->iw_mode == IW_MODE_MASTER && !wds &&
	    (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
	    IEEE80211_FCTL_FROMDS && ieee->stadev
	    && memcmp(hdr->addr2, ieee->assoc_ap_addr, ETH_ALEN) == 0) {
		/* Frame from BSSID of the AP for which we are a client */
		skb->dev = dev = ieee->stadev;
		stats = hostap_get_stats(dev);
		from_assoc_ap = 1;
	}
#endif

	dev->last_rx = jiffies;

#ifdef NOT_YET
	if ((ieee->iw_mode == IW_MODE_MASTER ||
	     ieee->iw_mode == IW_MODE_REPEAT) && !from_assoc_ap) {
		switch (hostap_handle_sta_rx(ieee, dev, skb, rx_stats,
					     wds != NULL)) {
		case AP_RX_CONTINUE_NOT_AUTHORIZED:
			frame_authorized = 0;
			break;
		case AP_RX_CONTINUE:
			frame_authorized = 1;
			break;
		case AP_RX_DROP:
			goto rx_dropped;
		case AP_RX_EXIT:
			goto rx_exit;
		}
	}
#endif

	/* Nullfunc frames may have PS-bit set, so they must be passed to
	 * hostap_handle_sta_rx() before being dropped here. */
	if (stype != IEEE80211_STYPE_DATA &&
	    stype != IEEE80211_STYPE_DATA_CFACK &&
	    stype != IEEE80211_STYPE_DATA_CFPOLL &&
	    stype != IEEE80211_STYPE_DATA_CFACKPOLL) {
		if (stype != IEEE80211_STYPE_NULLFUNC)
			IEEE80211_DEBUG_DROP("RX: dropped data frame "
					     "with no data (type=0x%02x, "
					     "subtype=0x%02x, len=%d)\n",
					     type, stype, skb->len);
		goto rx_dropped;
	}

	/* skb: hdr + (possibly fragmented, possibly encrypted) payload */

	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	    (keyidx = ieee80211_rx_frame_decrypt(ieee, skb, crypt)) < 0)
		goto rx_dropped;

	hdr = (struct ieee80211_hdr *)skb->data;

	/* skb: hdr + (possibly fragmented) plaintext payload */
	// PR: FIXME: hostap has additional conditions in the "if" below:
	// ieee->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	if ((frag != 0 || (fc & IEEE80211_FCTL_MOREFRAGS))) {
		int flen;
		struct sk_buff *frag_skb = ieee80211_frag_cache_get(ieee, hdr);
		IEEE80211_DEBUG_FRAG("Rx Fragment received (%u)\n", frag);

		if (!frag_skb) {
			IEEE80211_DEBUG(IEEE80211_DL_RX | IEEE80211_DL_FRAG,
					"Rx cannot get skb from fragment "
					"cache (morefrag=%d seq=%u frag=%u)\n",
					(fc & IEEE80211_FCTL_MOREFRAGS) != 0,
					WLAN_GET_SEQ_SEQ(sc), frag);
			goto rx_dropped;
		}

		flen = skb->len;
		if (frag != 0)
			flen -= hdrlen;

		if (frag_skb->tail + flen > frag_skb->end) {
			printk(KERN_WARNING "%s: host decrypted and "
			       "reassembled frame did not fit skb\n",
			       dev->name);
			ieee80211_frag_cache_invalidate(ieee, hdr);
			goto rx_dropped;
		}

		if (frag == 0) {
			/* copy first fragment (including full headers) into
			 * beginning of the fragment cache skb */
			memcpy(skb_put(frag_skb, flen), skb->data, flen);
		} else {
			/* append frame payload to the end of the fragment
			 * cache skb */
			memcpy(skb_put(frag_skb, flen), skb->data + hdrlen,
			       flen);
		}
		dev_kfree_skb_any(skb);
		skb = NULL;

		if (fc & IEEE80211_FCTL_MOREFRAGS) {
			/* more fragments expected - leave the skb in fragment
			 * cache for now; it will be delivered to upper layers
			 * after all fragments have been received */
			goto rx_exit;
		}

		/* this was the last fragment and the frame will be
		 * delivered, so remove skb from fragment cache */
		skb = frag_skb;
		hdr = (struct ieee80211_hdr *)skb->data;
		ieee80211_frag_cache_invalidate(ieee, hdr);
	}

	/* skb: hdr + (possible reassembled) full MSDU payload; possibly still
	 * encrypted/authenticated */
	if (ieee->host_decrypt && (fc & IEEE80211_FCTL_PROTECTED) &&
	    ieee80211_rx_frame_decrypt_msdu(ieee, skb, keyidx, crypt))
		goto rx_dropped;

	hdr = (struct ieee80211_hdr *)skb->data;
	if (crypt && !(fc & IEEE80211_FCTL_PROTECTED) && !ieee->open_wep) {
		if (		/*ieee->ieee802_1x && */
			   ieee80211_is_eapol_frame(ieee, skb)) {
			/* pass unencrypted EAPOL frames even if encryption is
			 * configured */
		} else {
			IEEE80211_DEBUG_DROP("encryption configured, but RX "
					     "frame not encrypted (SA=" MAC_FMT
					     ")\n", MAC_ARG(hdr->addr2));
			goto rx_dropped;
		}
	}

	if (crypt && !(fc & IEEE80211_FCTL_PROTECTED) && !ieee->open_wep &&
	    !ieee80211_is_eapol_frame(ieee, skb)) {
		IEEE80211_DEBUG_DROP("dropped unencrypted RX data "
				     "frame from " MAC_FMT
				     " (drop_unencrypted=1)\n",
				     MAC_ARG(hdr->addr2));
		goto rx_dropped;
	}

	/* skb: hdr + (possible reassembled) full plaintext payload */

	payload = skb->data + hdrlen;
	ethertype = (payload[6] << 8) | payload[7];

#ifdef NOT_YET
	/* If IEEE 802.1X is used, check whether the port is authorized to send
	 * the received frame. */
	if (ieee->ieee802_1x && ieee->iw_mode == IW_MODE_MASTER) {
		if (ethertype == ETH_P_PAE) {
			printk(KERN_DEBUG "%s: RX: IEEE 802.1X frame\n",
			       dev->name);
			if (ieee->hostapd && ieee->apdev) {
				/* Send IEEE 802.1X frames to the user
				 * space daemon for processing */
				prism2_rx_80211(ieee->apdev, skb, rx_stats,
						PRISM2_RX_MGMT);
				ieee->apdevstats.rx_packets++;
				ieee->apdevstats.rx_bytes += skb->len;
				goto rx_exit;
			}
		} else if (!frame_authorized) {
			printk(KERN_DEBUG "%s: dropped frame from "
			       "unauthorized port (IEEE 802.1X): "
			       "ethertype=0x%04x\n", dev->name, ethertype);
			goto rx_dropped;
		}
	}
#endif

	/* convert hdr + possible LLC headers into Ethernet header */
	if (skb->len - hdrlen >= 8 &&
	    ((memcmp(payload, rfc1042_header, SNAP_SIZE) == 0 &&
	      ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
	     memcmp(payload, bridge_tunnel_header, SNAP_SIZE) == 0)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + SNAP_SIZE);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	} else {
		u16 len;
		/* Leave Ethernet header part of hdr and full payload */
		skb_pull(skb, hdrlen);
		len = htons(skb->len);
		memcpy(skb_push(skb, 2), &len, 2);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	}

#ifdef NOT_YET
	if (wds && ((fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
		    IEEE80211_FCTL_TODS) && skb->len >= ETH_HLEN + ETH_ALEN) {
		/* Non-standard frame: get addr4 from its bogus location after
		 * the payload */
		memcpy(skb->data + ETH_ALEN,
		       skb->data + skb->len - ETH_ALEN, ETH_ALEN);
		skb_trim(skb, skb->len - ETH_ALEN);
	}
#endif

	stats->rx_packets++;
	stats->rx_bytes += skb->len;

#ifdef NOT_YET
	if (ieee->iw_mode == IW_MODE_MASTER && !wds && ieee->ap->bridge_packets) {
		if (dst[0] & 0x01) {
			/* copy multicast frame both to the higher layers and
			 * to the wireless media */
			ieee->ap->bridged_multicast++;
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2 == NULL)
				printk(KERN_DEBUG "%s: skb_clone failed for "
				       "multicast frame\n", dev->name);
		} else if (hostap_is_sta_assoc(ieee->ap, dst)) {
			/* send frame directly to the associated STA using
			 * wireless media and not passing to higher layers */
			ieee->ap->bridged_unicast++;
			skb2 = skb;
			skb = NULL;
		}
	}

	if (skb2 != NULL) {
		/* send to wireless media */
		skb2->protocol = __constant_htons(ETH_P_802_3);
		skb2->mac.raw = skb2->nh.raw = skb2->data;
		/* skb2->nh.raw = skb2->data + ETH_HLEN; */
		skb2->dev = dev;
		dev_queue_xmit(skb2);
	}
#endif

	if (skb) {
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		skb->dev = dev;
		skb->ip_summed = CHECKSUM_NONE;	/* 802.11 crc not sufficient */
		netif_rx(skb);
	}

      rx_exit:
#ifdef NOT_YET
	if (sta)
		hostap_handle_sta_release(sta);
#endif
	return 1;

      rx_dropped:
	stats->rx_dropped++;

	/* Returning 0 indicates to caller that we have not handled the SKB--
	 * so it is still allocated and can be used again by underlying
	 * hardware as a DMA target */
	return 0;
}

#define MGMT_FRAME_FIXED_PART_LENGTH		0x24

static inline int ieee80211_is_ofdm_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_OFDM_RATE_6MB:
	case IEEE80211_OFDM_RATE_9MB:
	case IEEE80211_OFDM_RATE_12MB:
	case IEEE80211_OFDM_RATE_18MB:
	case IEEE80211_OFDM_RATE_24MB:
	case IEEE80211_OFDM_RATE_36MB:
	case IEEE80211_OFDM_RATE_48MB:
	case IEEE80211_OFDM_RATE_54MB:
		return 1;
	}
	return 0;
}

static inline int ieee80211_network_init(struct ieee80211_device *ieee,
					 struct ieee80211_probe_response
					 *beacon,
					 struct ieee80211_network *network,
					 struct ieee80211_rx_stats *stats)
{
#ifdef CONFIG_IEEE80211_DEBUG
	char rates_str[64];
	char *p;
#endif
	struct ieee80211_info_element *info_element;
	u16 left;
	u8 i;

	/* Pull out fixed field data */
	memcpy(network->bssid, beacon->header.addr3, ETH_ALEN);
	network->capability = beacon->capability;
	network->last_scanned = jiffies;
	network->time_stamp[0] = beacon->time_stamp[0];
	network->time_stamp[1] = beacon->time_stamp[1];
	network->beacon_interval = beacon->beacon_interval;
	/* Where to pull this? beacon->listen_interval; */
	network->listen_interval = 0x0A;
	network->rates_len = network->rates_ex_len = 0;
	network->last_associate = 0;
	network->ssid_len = 0;
	network->flags = 0;
	network->atim_window = 0;

	if (stats->freq == IEEE80211_52GHZ_BAND) {
		/* for A band (No DS info) */
		network->channel = stats->received_channel;
	} else
		network->flags |= NETWORK_HAS_CCK;

	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;

	info_element = &beacon->info_element;
	left = stats->len - ((void *)info_element - (void *)beacon);
	while (left >= sizeof(struct ieee80211_info_element_hdr)) {
		if (sizeof(struct ieee80211_info_element_hdr) +
		    info_element->len > left) {
			IEEE80211_DEBUG_SCAN
			    ("SCAN: parse failed: info_element->len + 2 > left : info_element->len+2=%Zd left=%d.\n",
			     info_element->len +
			     sizeof(struct ieee80211_info_element), left);
			return 1;
		}

		switch (info_element->id) {
		case MFIE_TYPE_SSID:
			if (ieee80211_is_empty_essid(info_element->data,
						     info_element->len)) {
				network->flags |= NETWORK_EMPTY_ESSID;
				break;
			}

			network->ssid_len = min(info_element->len,
						(u8) IW_ESSID_MAX_SIZE);
			memcpy(network->ssid, info_element->data,
			       network->ssid_len);
			if (network->ssid_len < IW_ESSID_MAX_SIZE)
				memset(network->ssid + network->ssid_len, 0,
				       IW_ESSID_MAX_SIZE - network->ssid_len);

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_SSID: '%s' len=%d.\n",
					     network->ssid, network->ssid_len);
			break;

		case MFIE_TYPE_RATES:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_len =
			    min(info_element->len, MAX_RATES_LENGTH);
			for (i = 0; i < network->rates_len; i++) {
				network->rates[i] = info_element->data[i];
#ifdef CONFIG_IEEE80211_DEBUG
				p += snprintf(p,
					      sizeof(rates_str) - (p -
								   rates_str),
					      "%02X ", network->rates[i]);
#endif
				if (ieee80211_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RATES: '%s' (%d)\n",
					     rates_str, network->rates_len);
			break;

		case MFIE_TYPE_RATES_EX:
#ifdef CONFIG_IEEE80211_DEBUG
			p = rates_str;
#endif
			network->rates_ex_len =
			    min(info_element->len, MAX_RATES_EX_LENGTH);
			for (i = 0; i < network->rates_ex_len; i++) {
				network->rates_ex[i] = info_element->data[i];
#ifdef CONFIG_IEEE80211_DEBUG
				p += snprintf(p,
					      sizeof(rates_str) - (p -
								   rates_str),
					      "%02X ", network->rates[i]);
#endif
				if (ieee80211_is_ofdm_rate
				    (info_element->data[i])) {
					network->flags |= NETWORK_HAS_OFDM;
					if (info_element->data[i] &
					    IEEE80211_BASIC_RATE_MASK)
						network->flags &=
						    ~NETWORK_HAS_CCK;
				}
			}

			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RATES_EX: '%s' (%d)\n",
					     rates_str, network->rates_ex_len);
			break;

		case MFIE_TYPE_DS_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_DS_SET: %d\n",
					     info_element->data[0]);
			if (stats->freq == IEEE80211_24GHZ_BAND)
				network->channel = info_element->data[0];
			break;

		case MFIE_TYPE_FH_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_FH_SET: ignored\n");
			break;

		case MFIE_TYPE_CF_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_CF_SET: ignored\n");
			break;

		case MFIE_TYPE_TIM:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_TIM: ignored\n");
			break;

		case MFIE_TYPE_IBSS_SET:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_IBSS_SET: ignored\n");
			break;

		case MFIE_TYPE_CHALLENGE:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_CHALLENGE: ignored\n");
			break;

		case MFIE_TYPE_GENERIC:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_GENERIC: %d bytes\n",
					     info_element->len);
			if (info_element->len >= 4 &&
			    info_element->data[0] == 0x00 &&
			    info_element->data[1] == 0x50 &&
			    info_element->data[2] == 0xf2 &&
			    info_element->data[3] == 0x01) {
				network->wpa_ie_len = min(info_element->len + 2,
							  MAX_WPA_IE_LEN);
				memcpy(network->wpa_ie, info_element,
				       network->wpa_ie_len);
			}
			break;

		case MFIE_TYPE_RSN:
			IEEE80211_DEBUG_SCAN("MFIE_TYPE_RSN: %d bytes\n",
					     info_element->len);
			network->rsn_ie_len = min(info_element->len + 2,
						  MAX_WPA_IE_LEN);
			memcpy(network->rsn_ie, info_element,
			       network->rsn_ie_len);
			break;

		default:
			IEEE80211_DEBUG_SCAN("unsupported IE %d\n",
					     info_element->id);
			break;
		}

		left -= sizeof(struct ieee80211_info_element_hdr) +
		    info_element->len;
		info_element = (struct ieee80211_info_element *)
		    &info_element->data[info_element->len];
	}

	network->mode = 0;
	if (stats->freq == IEEE80211_52GHZ_BAND)
		network->mode = IEEE_A;
	else {
		if (network->flags & NETWORK_HAS_OFDM)
			network->mode |= IEEE_G;
		if (network->flags & NETWORK_HAS_CCK)
			network->mode |= IEEE_B;
	}

	if (network->mode == 0) {
		IEEE80211_DEBUG_SCAN("Filtered out '%s (" MAC_FMT ")' "
				     "network.\n",
				     escape_essid(network->ssid,
						  network->ssid_len),
				     MAC_ARG(network->bssid));
		return 1;
	}

	if (ieee80211_is_empty_essid(network->ssid, network->ssid_len))
		network->flags |= NETWORK_EMPTY_ESSID;

	memcpy(&network->stats, stats, sizeof(network->stats));

	return 0;
}

static inline int is_same_network(struct ieee80211_network *src,
				  struct ieee80211_network *dst)
{
	/* A network is only a duplicate if the channel, BSSID, and ESSID
	 * all match.  We treat all <hidden> with the same BSSID and channel
	 * as one network */
	return ((src->ssid_len == dst->ssid_len) &&
		(src->channel == dst->channel) &&
		!memcmp(src->bssid, dst->bssid, ETH_ALEN) &&
		!memcmp(src->ssid, dst->ssid, src->ssid_len));
}

static inline void update_network(struct ieee80211_network *dst,
				  struct ieee80211_network *src)
{
	memcpy(&dst->stats, &src->stats, sizeof(struct ieee80211_rx_stats));
	dst->capability = src->capability;
	memcpy(dst->rates, src->rates, src->rates_len);
	dst->rates_len = src->rates_len;
	memcpy(dst->rates_ex, src->rates_ex, src->rates_ex_len);
	dst->rates_ex_len = src->rates_ex_len;

	dst->mode = src->mode;
	dst->flags = src->flags;
	dst->time_stamp[0] = src->time_stamp[0];
	dst->time_stamp[1] = src->time_stamp[1];

	dst->beacon_interval = src->beacon_interval;
	dst->listen_interval = src->listen_interval;
	dst->atim_window = src->atim_window;

	memcpy(dst->wpa_ie, src->wpa_ie, src->wpa_ie_len);
	dst->wpa_ie_len = src->wpa_ie_len;
	memcpy(dst->rsn_ie, src->rsn_ie, src->rsn_ie_len);
	dst->rsn_ie_len = src->rsn_ie_len;

	dst->last_scanned = jiffies;
	/* dst->last_associate is not overwritten */
}

static inline void ieee80211_process_probe_response(struct ieee80211_device
						    *ieee,
						    struct
						    ieee80211_probe_response
						    *beacon,
						    struct ieee80211_rx_stats
						    *stats)
{
	struct ieee80211_network network;
	struct ieee80211_network *target;
	struct ieee80211_network *oldest = NULL;
#ifdef CONFIG_IEEE80211_DEBUG
	struct ieee80211_info_element *info_element = &beacon->info_element;
#endif
	unsigned long flags;

	IEEE80211_DEBUG_SCAN("'%s' (" MAC_FMT
			     "): %c%c%c%c %c%c%c%c-%c%c%c%c %c%c%c%c\n",
			     escape_essid(info_element->data,
					  info_element->len),
			     MAC_ARG(beacon->header.addr3),
			     (beacon->capability & (1 << 0xf)) ? '1' : '0',
			     (beacon->capability & (1 << 0xe)) ? '1' : '0',
			     (beacon->capability & (1 << 0xd)) ? '1' : '0',
			     (beacon->capability & (1 << 0xc)) ? '1' : '0',
			     (beacon->capability & (1 << 0xb)) ? '1' : '0',
			     (beacon->capability & (1 << 0xa)) ? '1' : '0',
			     (beacon->capability & (1 << 0x9)) ? '1' : '0',
			     (beacon->capability & (1 << 0x8)) ? '1' : '0',
			     (beacon->capability & (1 << 0x7)) ? '1' : '0',
			     (beacon->capability & (1 << 0x6)) ? '1' : '0',
			     (beacon->capability & (1 << 0x5)) ? '1' : '0',
			     (beacon->capability & (1 << 0x4)) ? '1' : '0',
			     (beacon->capability & (1 << 0x3)) ? '1' : '0',
			     (beacon->capability & (1 << 0x2)) ? '1' : '0',
			     (beacon->capability & (1 << 0x1)) ? '1' : '0',
			     (beacon->capability & (1 << 0x0)) ? '1' : '0');

	if (ieee80211_network_init(ieee, beacon, &network, stats)) {
		IEEE80211_DEBUG_SCAN("Dropped '%s' (" MAC_FMT ") via %s.\n",
				     escape_essid(info_element->data,
						  info_element->len),
				     MAC_ARG(beacon->header.addr3),
				     WLAN_FC_GET_STYPE(beacon->header.
						       frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
		return;
	}

	/* The network parsed correctly -- so now we scan our known networks
	 * to see if we can find it in our list.
	 *
	 * NOTE:  This search is definitely not optimized.  Once its doing
	 *        the "right thing" we'll optimize it for efficiency if
	 *        necessary */

	/* Search for this entry in the list and update it if it is
	 * already there. */

	spin_lock_irqsave(&ieee->lock, flags);

	list_for_each_entry(target, &ieee->network_list, list) {
		if (is_same_network(target, &network))
			break;

		if ((oldest == NULL) ||
		    (target->last_scanned < oldest->last_scanned))
			oldest = target;
	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (&target->list == &ieee->network_list) {
		if (list_empty(&ieee->network_free_list)) {
			/* If there are no more slots, expire the oldest */
			list_del(&oldest->list);
			target = oldest;
			IEEE80211_DEBUG_SCAN("Expired '%s' (" MAC_FMT ") from "
					     "network list.\n",
					     escape_essid(target->ssid,
							  target->ssid_len),
					     MAC_ARG(target->bssid));
		} else {
			/* Otherwise just pull from the free list */
			target = list_entry(ieee->network_free_list.next,
					    struct ieee80211_network, list);
			list_del(ieee->network_free_list.next);
		}

#ifdef CONFIG_IEEE80211_DEBUG
		IEEE80211_DEBUG_SCAN("Adding '%s' (" MAC_FMT ") via %s.\n",
				     escape_essid(network.ssid,
						  network.ssid_len),
				     MAC_ARG(network.bssid),
				     WLAN_FC_GET_STYPE(beacon->header.
						       frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
#endif
		memcpy(target, &network, sizeof(*target));
		list_add_tail(&target->list, &ieee->network_list);
	} else {
		IEEE80211_DEBUG_SCAN("Updating '%s' (" MAC_FMT ") via %s.\n",
				     escape_essid(target->ssid,
						  target->ssid_len),
				     MAC_ARG(target->bssid),
				     WLAN_FC_GET_STYPE(beacon->header.
						       frame_ctl) ==
				     IEEE80211_STYPE_PROBE_RESP ?
				     "PROBE RESPONSE" : "BEACON");
		update_network(target, &network);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_rx_mgt(struct ieee80211_device *ieee,
		      struct ieee80211_hdr *header,
		      struct ieee80211_rx_stats *stats)
{
	switch (WLAN_FC_GET_STYPE(header->frame_ctl)) {
	case IEEE80211_STYPE_ASSOC_RESP:
		IEEE80211_DEBUG_MGMT("received ASSOCIATION RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		break;

	case IEEE80211_STYPE_REASSOC_RESP:
		IEEE80211_DEBUG_MGMT("received REASSOCIATION RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		break;

	case IEEE80211_STYPE_PROBE_RESP:
		IEEE80211_DEBUG_MGMT("received PROBE RESPONSE (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		IEEE80211_DEBUG_SCAN("Probe response\n");
		ieee80211_process_probe_response(ieee,
						 (struct
						  ieee80211_probe_response *)
						 header, stats);
		break;

	case IEEE80211_STYPE_BEACON:
		IEEE80211_DEBUG_MGMT("received BEACON (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		IEEE80211_DEBUG_SCAN("Beacon\n");
		ieee80211_process_probe_response(ieee,
						 (struct
						  ieee80211_probe_response *)
						 header, stats);
		break;

	default:
		IEEE80211_DEBUG_MGMT("received UNKNOWN (%d)\n",
				     WLAN_FC_GET_STYPE(header->frame_ctl));
		IEEE80211_WARNING("%s: Unknown management packet: %d\n",
				  ieee->dev->name,
				  WLAN_FC_GET_STYPE(header->frame_ctl));
		break;
	}
}

EXPORT_SYMBOL(ieee80211_rx_mgt);
EXPORT_SYMBOL(ieee80211_rx);
