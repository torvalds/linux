/******************************************************************************

  Copyright(c) 2003 - 2005 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/
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

#include <net/ieee80211.h>

/*

802.11 Data Frame

      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  Frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `--------------------------------------------------|         |------'
Total: 28 non-data bytes                                 `----.----'
                                                              |
       .- 'Frame data' expands to <---------------------------'
       |
       V
      ,---------------------------------------------------.
Bytes |  1   |  1   |    1    |    3     |  2   |  0-2304 |
      |------|------|---------|----------|------|---------|
Desc. | SNAP | SNAP | Control |Eth Tunnel| Type | IP      |
      | DSAP | SSAP |         |          |      | Packet  |
      | 0xAA | 0xAA |0x03 (UI)|0x00-00-F8|      |         |
      `-----------------------------------------|         |
Total: 8 non-data bytes                         `----.----'
                                                     |
       .- 'IP Packet' expands, if WEP enabled, to <--'
       |
       V
      ,-----------------------.
Bytes |  4  |   0-2296  |  4  |
      |-----|-----------|-----|
Desc. | IV  | Encrypted | ICV |
      |     | IP Packet |     |
      `-----------------------'
Total: 8 non-data bytes

802.3 Ethernet Data Frame

      ,-----------------------------------------.
Bytes |   6   |   6   |  2   |  Variable |   4  |
      |-------|-------|------|-----------|------|
Desc. | Dest. | Source| Type | IP Packet |  fcs |
      |  MAC  |  MAC  |      |           |      |
      `-----------------------------------------'
Total: 18 non-data bytes

In the event that fragmentation is required, the incoming payload is split into
N parts of size ieee->fts.  The first fragment contains the SNAP header and the
remaining packets are just data.

If encryption is enabled, each fragment payload size is reduced by enough space
to add the prefix and postfix (IV and ICV totalling 8 bytes in the case of WEP)
So if you have 1500 bytes of payload with ieee->fts set to 500 without
encryption it will take 3 frames.  With WEP it will take 4 frames as the
payload of each frame is reduced to 492 bytes.

* SKB visualization
*
*  ,- skb->data
* |
* |    ETHERNET HEADER        ,-<-- PAYLOAD
* |                           |     14 bytes from skb->data
* |  2 bytes for Type --> ,T. |     (sizeof ethhdr)
* |                       | | |
* |,-Dest.--. ,--Src.---. | | |
* |  6 bytes| | 6 bytes | | | |
* v         | |         | | | |
* 0         | v       1 | v | v           2
* 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
*     ^     | ^         | ^ |
*     |     | |         | | |
*     |     | |         | `T' <---- 2 bytes for Type
*     |     | |         |
*     |     | '---SNAP--' <-------- 6 bytes for SNAP
*     |     |
*     `-IV--' <-------------------- 4 bytes for IV (WEP)
*
*      SNAP HEADER
*
*/

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static inline int ieee80211_copy_snap(u8 * data, u16 h_proto)
{
	struct ieee80211_snap_hdr *snap;
	u8 *oui;

	snap = (struct ieee80211_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	*(u16 *) (data + SNAP_SIZE) = htons(h_proto);

	return SNAP_SIZE + sizeof(u16);
}

static inline int ieee80211_encrypt_fragment(struct ieee80211_device *ieee,
					     struct sk_buff *frag, int hdr_len)
{
	struct ieee80211_crypt_data *crypt = ieee->crypt[ieee->tx_keyidx];
	int res;

	if (crypt == NULL)
		return -1;

	/* To encrypt, frame format is:
	 * IV (4 bytes), clear payload (including SNAP), ICV (4 bytes) */
	atomic_inc(&crypt->refcnt);
	res = 0;
	if (crypt->ops && crypt->ops->encrypt_mpdu)
		res = crypt->ops->encrypt_mpdu(frag, hdr_len, crypt->priv);

	atomic_dec(&crypt->refcnt);
	if (res < 0) {
		printk(KERN_INFO "%s: Encryption failed: len=%d.\n",
		       ieee->dev->name, frag->len);
		ieee->ieee_stats.tx_discards++;
		return -1;
	}

	return 0;
}

void ieee80211_txb_free(struct ieee80211_txb *txb)
{
	int i;
	if (unlikely(!txb))
		return;
	for (i = 0; i < txb->nr_frags; i++)
		if (txb->fragments[i])
			dev_kfree_skb_any(txb->fragments[i]);
	kfree(txb);
}

static struct ieee80211_txb *ieee80211_alloc_txb(int nr_frags, int txb_size,
						 int headroom, gfp_t gfp_mask)
{
	struct ieee80211_txb *txb;
	int i;
	txb = kmalloc(sizeof(struct ieee80211_txb) + (sizeof(u8 *) * nr_frags),
		      gfp_mask);
	if (!txb)
		return NULL;

	memset(txb, 0, sizeof(struct ieee80211_txb));
	txb->nr_frags = nr_frags;
	txb->frag_size = txb_size;

	for (i = 0; i < nr_frags; i++) {
		txb->fragments[i] = __dev_alloc_skb(txb_size + headroom,
						    gfp_mask);
		if (unlikely(!txb->fragments[i])) {
			i--;
			break;
		}
		skb_reserve(txb->fragments[i], headroom);
	}
	if (unlikely(i != nr_frags)) {
		while (i >= 0)
			dev_kfree_skb_any(txb->fragments[i--]);
		kfree(txb);
		return NULL;
	}
	return txb;
}

/* Incoming skb is converted to a txb which consists of
 * a block of 802.11 fragment packets (stored as skbs) */
int ieee80211_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_device *ieee = netdev_priv(dev);
	struct ieee80211_txb *txb = NULL;
	struct ieee80211_hdr_3addr *frag_hdr;
	int i, bytes_per_frag, nr_frags, bytes_last_frag, frag_size,
	    rts_required;
	unsigned long flags;
	struct net_device_stats *stats = &ieee->stats;
	int ether_type, encrypt, host_encrypt, host_encrypt_msdu, host_build_iv;
	int bytes, fc, hdr_len;
	struct sk_buff *skb_frag;
	struct ieee80211_hdr_3addr header = {	/* Ensure zero initialized */
		.duration_id = 0,
		.seq_ctl = 0
	};
	u8 dest[ETH_ALEN], src[ETH_ALEN];
	struct ieee80211_crypt_data *crypt;
	int priority = skb->priority;
	int snapped = 0;

	if (ieee->is_queue_full && (*ieee->is_queue_full) (dev, priority))
		return NETDEV_TX_BUSY;

	spin_lock_irqsave(&ieee->lock, flags);

	/* If there is no driver handler to take the TXB, dont' bother
	 * creating it... */
	if (!ieee->hard_start_xmit) {
		printk(KERN_WARNING "%s: No xmit handler.\n", ieee->dev->name);
		goto success;
	}

	if (unlikely(skb->len < SNAP_SIZE + sizeof(u16))) {
		printk(KERN_WARNING "%s: skb too small (%d).\n",
		       ieee->dev->name, skb->len);
		goto success;
	}

	ether_type = ntohs(((struct ethhdr *)skb->data)->h_proto);

	crypt = ieee->crypt[ieee->tx_keyidx];

	encrypt = !(ether_type == ETH_P_PAE && ieee->ieee802_1x) &&
	    ieee->sec.encrypt;

	host_encrypt = ieee->host_encrypt && encrypt && crypt;
	host_encrypt_msdu = ieee->host_encrypt_msdu && encrypt && crypt;
	host_build_iv = ieee->host_build_iv && encrypt && crypt;

	if (!encrypt && ieee->ieee802_1x &&
	    ieee->drop_unencrypted && ether_type != ETH_P_PAE) {
		stats->tx_dropped++;
		goto success;
	}

	/* Save source and destination addresses */
	memcpy(dest, skb->data, ETH_ALEN);
	memcpy(src, skb->data + ETH_ALEN, ETH_ALEN);

	/* Advance the SKB to the start of the payload */
	skb_pull(skb, sizeof(struct ethhdr));

	/* Determine total amount of storage required for TXB packets */
	bytes = skb->len + SNAP_SIZE + sizeof(u16);

	if (host_encrypt)
		fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA |
		    IEEE80211_FCTL_PROTECTED;
	else
		fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA;

	if (ieee->iw_mode == IW_MODE_INFRA) {
		fc |= IEEE80211_FCTL_TODS;
		/* To DS: Addr1 = BSSID, Addr2 = SA, Addr3 = DA */
		memcpy(header.addr1, ieee->bssid, ETH_ALEN);
		memcpy(header.addr2, src, ETH_ALEN);
		memcpy(header.addr3, dest, ETH_ALEN);
	} else if (ieee->iw_mode == IW_MODE_ADHOC) {
		/* not From/To DS: Addr1 = DA, Addr2 = SA, Addr3 = BSSID */
		memcpy(header.addr1, dest, ETH_ALEN);
		memcpy(header.addr2, src, ETH_ALEN);
		memcpy(header.addr3, ieee->bssid, ETH_ALEN);
	}
	header.frame_ctl = cpu_to_le16(fc);
	hdr_len = IEEE80211_3ADDR_LEN;

	/* Encrypt msdu first on the whole data packet. */
	if ((host_encrypt || host_encrypt_msdu) &&
	    crypt && crypt->ops && crypt->ops->encrypt_msdu) {
		int res = 0;
		int len = bytes + hdr_len + crypt->ops->extra_msdu_prefix_len +
		    crypt->ops->extra_msdu_postfix_len;
		struct sk_buff *skb_new = dev_alloc_skb(len);

		if (unlikely(!skb_new))
			goto failed;

		skb_reserve(skb_new, crypt->ops->extra_msdu_prefix_len);
		memcpy(skb_put(skb_new, hdr_len), &header, hdr_len);
		snapped = 1;
		ieee80211_copy_snap(skb_put(skb_new, SNAP_SIZE + sizeof(u16)),
				    ether_type);
		memcpy(skb_put(skb_new, skb->len), skb->data, skb->len);
		res = crypt->ops->encrypt_msdu(skb_new, hdr_len, crypt->priv);
		if (res < 0) {
			IEEE80211_ERROR("msdu encryption failed\n");
			dev_kfree_skb_any(skb_new);
			goto failed;
		}
		dev_kfree_skb_any(skb);
		skb = skb_new;
		bytes += crypt->ops->extra_msdu_prefix_len +
		    crypt->ops->extra_msdu_postfix_len;
		skb_pull(skb, hdr_len);
	}

	if (host_encrypt || ieee->host_open_frag) {
		/* Determine fragmentation size based on destination (multicast
		 * and broadcast are not fragmented) */
		if (is_multicast_ether_addr(dest) ||
		    is_broadcast_ether_addr(dest))
			frag_size = MAX_FRAG_THRESHOLD;
		else
			frag_size = ieee->fts;

		/* Determine amount of payload per fragment.  Regardless of if
		 * this stack is providing the full 802.11 header, one will
		 * eventually be affixed to this fragment -- so we must account
		 * for it when determining the amount of payload space. */
		bytes_per_frag = frag_size - IEEE80211_3ADDR_LEN;
		if (ieee->config &
		    (CFG_IEEE80211_COMPUTE_FCS | CFG_IEEE80211_RESERVE_FCS))
			bytes_per_frag -= IEEE80211_FCS_LEN;

		/* Each fragment may need to have room for encryptiong
		 * pre/postfix */
		if (host_encrypt)
			bytes_per_frag -= crypt->ops->extra_mpdu_prefix_len +
			    crypt->ops->extra_mpdu_postfix_len;

		/* Number of fragments is the total
		 * bytes_per_frag / payload_per_fragment */
		nr_frags = bytes / bytes_per_frag;
		bytes_last_frag = bytes % bytes_per_frag;
		if (bytes_last_frag)
			nr_frags++;
		else
			bytes_last_frag = bytes_per_frag;
	} else {
		nr_frags = 1;
		bytes_per_frag = bytes_last_frag = bytes;
		frag_size = bytes + IEEE80211_3ADDR_LEN;
	}

	rts_required = (frag_size > ieee->rts
			&& ieee->config & CFG_IEEE80211_RTS);
	if (rts_required)
		nr_frags++;

	/* When we allocate the TXB we allocate enough space for the reserve
	 * and full fragment bytes (bytes_per_frag doesn't include prefix,
	 * postfix, header, FCS, etc.) */
	txb = ieee80211_alloc_txb(nr_frags, frag_size,
				  ieee->tx_headroom, GFP_ATOMIC);
	if (unlikely(!txb)) {
		printk(KERN_WARNING "%s: Could not allocate TXB\n",
		       ieee->dev->name);
		goto failed;
	}
	txb->encrypted = encrypt;
	if (host_encrypt)
		txb->payload_size = frag_size * (nr_frags - 1) +
		    bytes_last_frag;
	else
		txb->payload_size = bytes;

	if (rts_required) {
		skb_frag = txb->fragments[0];
		frag_hdr =
		    (struct ieee80211_hdr_3addr *)skb_put(skb_frag, hdr_len);

		/*
		 * Set header frame_ctl to the RTS.
		 */
		header.frame_ctl =
		    cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
		memcpy(frag_hdr, &header, hdr_len);

		/*
		 * Restore header frame_ctl to the original data setting.
		 */
		header.frame_ctl = cpu_to_le16(fc);

		if (ieee->config &
		    (CFG_IEEE80211_COMPUTE_FCS | CFG_IEEE80211_RESERVE_FCS))
			skb_put(skb_frag, 4);

		txb->rts_included = 1;
		i = 1;
	} else
		i = 0;

	for (; i < nr_frags; i++) {
		skb_frag = txb->fragments[i];

		if (host_encrypt || host_build_iv)
			skb_reserve(skb_frag,
				    crypt->ops->extra_mpdu_prefix_len);

		frag_hdr =
		    (struct ieee80211_hdr_3addr *)skb_put(skb_frag, hdr_len);
		memcpy(frag_hdr, &header, hdr_len);

		/* If this is not the last fragment, then add the MOREFRAGS
		 * bit to the frame control */
		if (i != nr_frags - 1) {
			frag_hdr->frame_ctl =
			    cpu_to_le16(fc | IEEE80211_FCTL_MOREFRAGS);
			bytes = bytes_per_frag;
		} else {
			/* The last fragment takes the remaining length */
			bytes = bytes_last_frag;
		}

		if (i == 0 && !snapped) {
			ieee80211_copy_snap(skb_put
					    (skb_frag, SNAP_SIZE + sizeof(u16)),
					    ether_type);
			bytes -= SNAP_SIZE + sizeof(u16);
		}

		memcpy(skb_put(skb_frag, bytes), skb->data, bytes);

		/* Advance the SKB... */
		skb_pull(skb, bytes);

		/* Encryption routine will move the header forward in order
		 * to insert the IV between the header and the payload */
		if (host_encrypt)
			ieee80211_encrypt_fragment(ieee, skb_frag, hdr_len);
		else if (host_build_iv) {
			struct ieee80211_crypt_data *crypt;

			crypt = ieee->crypt[ieee->tx_keyidx];
			atomic_inc(&crypt->refcnt);
			if (crypt->ops->build_iv)
				crypt->ops->build_iv(skb_frag, hdr_len,
						     crypt->priv);
			atomic_dec(&crypt->refcnt);
		}

		if (ieee->config &
		    (CFG_IEEE80211_COMPUTE_FCS | CFG_IEEE80211_RESERVE_FCS))
			skb_put(skb_frag, 4);
	}

      success:
	spin_unlock_irqrestore(&ieee->lock, flags);

	dev_kfree_skb_any(skb);

	if (txb) {
		int ret = (*ieee->hard_start_xmit) (txb, dev, priority);
		if (ret == 0) {
			stats->tx_packets++;
			stats->tx_bytes += txb->payload_size;
			return 0;
		}

		if (ret == NETDEV_TX_BUSY) {
			printk(KERN_ERR "%s: NETDEV_TX_BUSY returned; "
			       "driver should report queue full via "
			       "ieee_device->is_queue_full.\n",
			       ieee->dev->name);
		}

		ieee80211_txb_free(txb);
	}

	return 0;

      failed:
	spin_unlock_irqrestore(&ieee->lock, flags);
	netif_stop_queue(dev);
	stats->tx_errors++;
	return 1;
}

/* Incoming 802.11 strucure is converted to a TXB
 * a block of 802.11 fragment packets (stored as skbs) */
int ieee80211_tx_frame(struct ieee80211_device *ieee,
		       struct ieee80211_hdr *frame, int len)
{
	struct ieee80211_txb *txb = NULL;
	unsigned long flags;
	struct net_device_stats *stats = &ieee->stats;
	struct sk_buff *skb_frag;
	int priority = -1;

	spin_lock_irqsave(&ieee->lock, flags);

	/* If there is no driver handler to take the TXB, dont' bother
	 * creating it... */
	if (!ieee->hard_start_xmit) {
		printk(KERN_WARNING "%s: No xmit handler.\n", ieee->dev->name);
		goto success;
	}

	if (unlikely(len < 24)) {
		printk(KERN_WARNING "%s: skb too small (%d).\n",
		       ieee->dev->name, len);
		goto success;
	}

	/* When we allocate the TXB we allocate enough space for the reserve
	 * and full fragment bytes (bytes_per_frag doesn't include prefix,
	 * postfix, header, FCS, etc.) */
	txb = ieee80211_alloc_txb(1, len, ieee->tx_headroom, GFP_ATOMIC);
	if (unlikely(!txb)) {
		printk(KERN_WARNING "%s: Could not allocate TXB\n",
		       ieee->dev->name);
		goto failed;
	}
	txb->encrypted = 0;
	txb->payload_size = len;

	skb_frag = txb->fragments[0];

	memcpy(skb_put(skb_frag, len), frame, len);

	if (ieee->config &
	    (CFG_IEEE80211_COMPUTE_FCS | CFG_IEEE80211_RESERVE_FCS))
		skb_put(skb_frag, 4);

      success:
	spin_unlock_irqrestore(&ieee->lock, flags);

	if (txb) {
		if ((*ieee->hard_start_xmit) (txb, ieee->dev, priority) == 0) {
			stats->tx_packets++;
			stats->tx_bytes += txb->payload_size;
			return 0;
		}
		ieee80211_txb_free(txb);
	}
	return 0;

      failed:
	spin_unlock_irqrestore(&ieee->lock, flags);
	stats->tx_errors++;
	return 1;
}

EXPORT_SYMBOL(ieee80211_tx_frame);
EXPORT_SYMBOL(ieee80211_txb_free);
