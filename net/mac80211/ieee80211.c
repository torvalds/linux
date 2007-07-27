/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/rtnetlink.h>
#include <net/iw_handler.h>
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include <net/cfg80211.h>

#include "ieee80211_common.h"
#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"
#include "aes_ccm.h"
#include "ieee80211_led.h"
#include "ieee80211_cfg.h"
#include "debugfs.h"
#include "debugfs_netdev.h"
#include "debugfs_key.h"

/* privid for wiphys to determine whether they belong to us or not */
void *mac80211_wiphy_privid = &mac80211_wiphy_privid;

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
const unsigned char rfc1042_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

/* No encapsulation header if EtherType < 0x600 (=length) */
static const unsigned char eapol_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };


/*
 * For seeing transmitted packets on monitor interfaces
 * we have a radiotap header too.
 */
struct ieee80211_tx_status_rtap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le16 tx_flags;
	u8 data_retries;
} __attribute__ ((packed));


struct ieee80211_key_conf *
ieee80211_key_data2conf(struct ieee80211_local *local,
			const struct ieee80211_key *data)
{
	struct ieee80211_key_conf *conf;

	conf = kmalloc(sizeof(*conf) + data->keylen, GFP_ATOMIC);
	if (!conf)
		return NULL;

	conf->hw_key_idx = data->hw_key_idx;
	conf->alg = data->alg;
	conf->keylen = data->keylen;
	conf->flags = 0;
	if (data->force_sw_encrypt)
		conf->flags |= IEEE80211_KEY_FORCE_SW_ENCRYPT;
	conf->keyidx = data->keyidx;
	if (data->default_tx_key)
		conf->flags |= IEEE80211_KEY_DEFAULT_TX_KEY;
	if (local->default_wep_only)
		conf->flags |= IEEE80211_KEY_DEFAULT_WEP_ONLY;
	memcpy(conf->key, data->key, data->keylen);

	return conf;
}

struct ieee80211_key *ieee80211_key_alloc(struct ieee80211_sub_if_data *sdata,
					  int idx, size_t key_len, gfp_t flags)
{
	struct ieee80211_key *key;

	key = kzalloc(sizeof(struct ieee80211_key) + key_len, flags);
	if (!key)
		return NULL;
	kref_init(&key->kref);
	return key;
}

static void ieee80211_key_release(struct kref *kref)
{
	struct ieee80211_key *key;

	key = container_of(kref, struct ieee80211_key, kref);
	if (key->alg == ALG_CCMP)
		ieee80211_aes_key_free(key->u.ccmp.tfm);
	ieee80211_debugfs_key_remove(key);
	kfree(key);
}

void ieee80211_key_free(struct ieee80211_key *key)
{
	if (key)
		kref_put(&key->kref, ieee80211_key_release);
}

static int rate_list_match(const int *rate_list, int rate)
{
	int i;

	if (!rate_list)
		return 0;

	for (i = 0; rate_list[i] >= 0; i++)
		if (rate_list[i] == rate)
			return 1;

	return 0;
}


void ieee80211_prepare_rates(struct ieee80211_local *local,
			     struct ieee80211_hw_mode *mode)
{
	int i;

	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];

		rate->flags &= ~(IEEE80211_RATE_SUPPORTED |
				 IEEE80211_RATE_BASIC);

		if (local->supp_rates[mode->mode]) {
			if (!rate_list_match(local->supp_rates[mode->mode],
					     rate->rate))
				continue;
		}

		rate->flags |= IEEE80211_RATE_SUPPORTED;

		/* Use configured basic rate set if it is available. If not,
		 * use defaults that are sane for most cases. */
		if (local->basic_rates[mode->mode]) {
			if (rate_list_match(local->basic_rates[mode->mode],
					    rate->rate))
				rate->flags |= IEEE80211_RATE_BASIC;
		} else switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10 || rate->rate == 20)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_ATHEROS_TURBO:
			if (rate->rate == 120 || rate->rate == 240 ||
			    rate->rate == 480)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		}

		/* Set ERP and MANDATORY flags based on phymode */
		switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_ATHEROS_TURBO:
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110 ||
			    rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		}
		if (ieee80211_is_erp_rate(mode->mode, rate->rate))
			rate->flags |= IEEE80211_RATE_ERP;
	}
}


void ieee80211_key_threshold_notify(struct net_device *dev,
				    struct ieee80211_key *key,
				    struct sta_info *sta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_msg_key_notification *msg;

	/* if no one will get it anyway, don't even allocate it.
	 * unlikely because this is only relevant for APs
	 * where the device must be open... */
	if (unlikely(!local->apdev))
		return;

	skb = dev_alloc_skb(sizeof(struct ieee80211_frame_info) +
			    sizeof(struct ieee80211_msg_key_notification));
	if (!skb)
		return;

	skb_reserve(skb, sizeof(struct ieee80211_frame_info));
	msg = (struct ieee80211_msg_key_notification *)
		skb_put(skb, sizeof(struct ieee80211_msg_key_notification));
	msg->tx_rx_count = key->tx_rx_count;
	memcpy(msg->ifname, dev->name, IFNAMSIZ);
	if (sta)
		memcpy(msg->addr, sta->addr, ETH_ALEN);
	else
		memset(msg->addr, 0xff, ETH_ALEN);

	key->tx_rx_count = 0;

	ieee80211_rx_mgmt(local, skb, NULL,
			  ieee80211_msg_key_threshold_notification);
}


u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len)
{
	u16 fc;

	if (len < 24)
		return NULL;

	fc = le16_to_cpu(hdr->frame_control);

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		switch (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
		case IEEE80211_FCTL_TODS:
			return hdr->addr1;
		case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
			return NULL;
		case IEEE80211_FCTL_FROMDS:
			return hdr->addr2;
		case 0:
			return hdr->addr3;
		}
		break;
	case IEEE80211_FTYPE_MGMT:
		return hdr->addr3;
	case IEEE80211_FTYPE_CTL:
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)
			return hdr->addr1;
		else
			return NULL;
	}

	return NULL;
}

int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = 30; /* Addr4 */
		/*
		 * The QoS Control field is two bytes and its presence is
		 * indicated by the IEEE80211_STYPE_QOS_DATA bit. Add 2 to
		 * hdrlen if that bit is set.
		 * This works by masking out the bit and shifting it to
		 * bit position 1 so the result has the value 0 or 2.
		 */
		hdrlen += (fc & IEEE80211_STYPE_QOS_DATA)
				>> (ilog2(IEEE80211_STYPE_QOS_DATA)-1);
		break;
	case IEEE80211_FTYPE_CTL:
		/*
		 * ACK and CTS are 10 bytes, all others 16. To see how
		 * to get this condition consider
		 *   subtype mask:   0b0000000011110000 (0x00F0)
		 *   ACK subtype:    0b0000000011010000 (0x00D0)
		 *   CTS subtype:    0b0000000011000000 (0x00C0)
		 *   bits that matter:         ^^^      (0x00E0)
		 *   value of those: 0b0000000011000000 (0x00C0)
		 */
		if ((fc & 0xE0) == 0xC0)
			hdrlen = 10;
		else
			hdrlen = 16;
		break;
	}

	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen);

int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *) skb->data;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control));
	if (unlikely(hdrlen > skb->len))
		return 0;
	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen_from_skb);


int ieee80211_is_eapol(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr;
	u16 fc;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;

	hdr = (const struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return 0;

	hdrlen = ieee80211_get_hdrlen(fc);

	if (unlikely(skb->len >= hdrlen + sizeof(eapol_header) &&
		     memcmp(skb->data + hdrlen, eapol_header,
			    sizeof(eapol_header)) == 0))
		return 1;

	return 0;
}


void ieee80211_tx_set_iswep(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	if (tx->u.tx.extra_frag) {
		struct ieee80211_hdr *fhdr;
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			fhdr = (struct ieee80211_hdr *)
				tx->u.tx.extra_frag[i]->data;
			fhdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		}
	}
}


static int ieee80211_frame_duration(struct ieee80211_local *local, size_t len,
				    int rate, int erp, int short_preamble)
{
	int dur;

	/* calculate duration (in microseconds, rounded up to next higher
	 * integer if it includes a fractional microsecond) to send frame of
	 * len bytes (does not include FCS) at the given rate. Duration will
	 * also include SIFS.
	 *
	 * rate is in 100 kbps, so divident is multiplied by 10 in the
	 * DIV_ROUND_UP() operations.
	 */

	if (local->hw.conf.phymode == MODE_IEEE80211A || erp ||
	    local->hw.conf.phymode == MODE_ATHEROS_TURBO) {
		/*
		 * OFDM:
		 *
		 * N_DBPS = DATARATE x 4
		 * N_SYM = Ceiling((16+8xLENGTH+6) / N_DBPS)
		 *	(16 = SIGNAL time, 6 = tail bits)
		 * TXTIME = T_PREAMBLE + T_SIGNAL + T_SYM x N_SYM + Signal Ext
		 *
		 * T_SYM = 4 usec
		 * 802.11a - 17.5.2: aSIFSTime = 16 usec
		 * 802.11g - 19.8.4: aSIFSTime = 10 usec +
		 *	signal ext = 6 usec
		 */
		/* FIX: Atheros Turbo may have different (shorter) duration? */
		dur = 16; /* SIFS + signal ext */
		dur += 16; /* 17.3.2.3: T_PREAMBLE = 16 usec */
		dur += 4; /* 17.3.2.3: T_SIGNAL = 4 usec */
		dur += 4 * DIV_ROUND_UP((16 + 8 * (len + 4) + 6) * 10,
					4 * rate); /* T_SYM x N_SYM */
	} else {
		/*
		 * 802.11b or 802.11g with 802.11b compatibility:
		 * 18.3.4: TXTIME = PreambleLength + PLCPHeaderTime +
		 * Ceiling(((LENGTH+PBCC)x8)/DATARATE). PBCC=0.
		 *
		 * 802.11 (DS): 15.3.3, 802.11b: 18.3.4
		 * aSIFSTime = 10 usec
		 * aPreambleLength = 144 usec or 72 usec with short preamble
		 * aPLCPHeaderLength = 48 usec or 24 usec with short preamble
		 */
		dur = 10; /* aSIFSTime = 10 usec */
		dur += short_preamble ? (72 + 24) : (144 + 48);

		dur += DIV_ROUND_UP(8 * (len + 4) * 10, rate);
	}

	return dur;
}


/* Exported duration function for driver use */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					size_t frame_len, int rate)
{
	struct ieee80211_local *local = hw_to_local(hw);
	u16 dur;
	int erp;

	erp = ieee80211_is_erp_rate(hw->conf.phymode, rate);
	dur = ieee80211_frame_duration(local, frame_len, rate,
				       erp, local->short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_generic_frame_duration);


__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      size_t frame_len,
			      const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int short_preamble = local->short_preamble;
	int erp;
	u16 dur;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* CTS duration */
	dur = ieee80211_frame_duration(local, 10, rate->rate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(local, frame_len, rate->rate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(local, 10, rate->rate,
					erp, short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_rts_duration);


__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    size_t frame_len,
				    const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int short_preamble = local->short_preamble;
	int erp;
	u16 dur;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* Data frame duration */
	dur = ieee80211_frame_duration(local, frame_len, rate->rate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TXCTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(local, 10, rate->rate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

static int __ieee80211_if_config(struct net_device *dev,
				 struct sk_buff *beacon,
				 struct ieee80211_tx_control *control)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_conf conf;
	static u8 scan_bssid[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (!local->ops->config_interface || !netif_running(dev))
		return 0;

	memset(&conf, 0, sizeof(conf));
	conf.type = sdata->type;
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		if (local->sta_scanning &&
		    local->scan_dev == dev)
			conf.bssid = scan_bssid;
		else
			conf.bssid = sdata->u.sta.bssid;
		conf.ssid = sdata->u.sta.ssid;
		conf.ssid_len = sdata->u.sta.ssid_len;
		conf.generic_elem = sdata->u.sta.extra_ie;
		conf.generic_elem_len = sdata->u.sta.extra_ie_len;
	} else if (sdata->type == IEEE80211_IF_TYPE_AP) {
		conf.ssid = sdata->u.ap.ssid;
		conf.ssid_len = sdata->u.ap.ssid_len;
		conf.generic_elem = sdata->u.ap.generic_elem;
		conf.generic_elem_len = sdata->u.ap.generic_elem_len;
		conf.beacon = beacon;
		conf.beacon_control = control;
	}
	return local->ops->config_interface(local_to_hw(local),
					   dev->ifindex, &conf);
}

int ieee80211_if_config(struct net_device *dev)
{
	return __ieee80211_if_config(dev, NULL, NULL);
}

int ieee80211_if_config_beacon(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_control control;
	struct sk_buff *skb;

	if (!(local->hw.flags & IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE))
		return 0;
	skb = ieee80211_beacon_get(local_to_hw(local), dev->ifindex, &control);
	if (!skb)
		return -ENOMEM;
	return __ieee80211_if_config(dev, skb, &control);
}

int ieee80211_hw_config(struct ieee80211_local *local)
{
	struct ieee80211_hw_mode *mode;
	struct ieee80211_channel *chan;
	int ret = 0;

	if (local->sta_scanning) {
		chan = local->scan_channel;
		mode = local->scan_hw_mode;
	} else {
		chan = local->oper_channel;
		mode = local->oper_hw_mode;
	}

	local->hw.conf.channel = chan->chan;
	local->hw.conf.channel_val = chan->val;
	local->hw.conf.power_level = chan->power_level;
	local->hw.conf.freq = chan->freq;
	local->hw.conf.phymode = mode->mode;
	local->hw.conf.antenna_max = chan->antenna_max;
	local->hw.conf.chan = chan;
	local->hw.conf.mode = mode;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "HW CONFIG: channel=%d freq=%d "
	       "phymode=%d\n", local->hw.conf.channel, local->hw.conf.freq,
	       local->hw.conf.phymode);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

	if (local->ops->config)
		ret = local->ops->config(local_to_hw(local), &local->hw.conf);

	return ret;
}


static int ieee80211_change_mtu(struct net_device *dev, int new_mtu)
{
	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.3 frames. */
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN - 24 - 6) {
		printk(KERN_WARNING "%s: invalid MTU %d\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}


static int ieee80211_change_mtu_apdev(struct net_device *dev, int new_mtu)
{
	/* FIX: what would be proper limits for MTU?
	 * This interface uses 802.11 frames. */
	if (new_mtu < 256 || new_mtu > IEEE80211_MAX_DATA_LEN) {
		printk(KERN_WARNING "%s: invalid MTU %d\n",
		       dev->name, new_mtu);
		return -EINVAL;
	}

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	printk(KERN_DEBUG "%s: setting MTU %d\n", dev->name, new_mtu);
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */
	dev->mtu = new_mtu;
	return 0;
}

enum netif_tx_lock_class {
	TX_LOCK_NORMAL,
	TX_LOCK_MASTER,
};

static inline void netif_tx_lock_nested(struct net_device *dev, int subclass)
{
	spin_lock_nested(&dev->_xmit_lock, subclass);
	dev->xmit_lock_owner = smp_processor_id();
}

static void ieee80211_set_multicast_list(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	unsigned short flags;

	netif_tx_lock_nested(local->mdev, TX_LOCK_MASTER);
	if (((dev->flags & IFF_ALLMULTI) != 0) ^ (sdata->allmulti != 0)) {
		if (sdata->allmulti) {
			sdata->allmulti = 0;
			local->iff_allmultis--;
		} else {
			sdata->allmulti = 1;
			local->iff_allmultis++;
		}
	}
	if (((dev->flags & IFF_PROMISC) != 0) ^ (sdata->promisc != 0)) {
		if (sdata->promisc) {
			sdata->promisc = 0;
			local->iff_promiscs--;
		} else {
			sdata->promisc = 1;
			local->iff_promiscs++;
		}
	}
	if (dev->mc_count != sdata->mc_count) {
		local->mc_count = local->mc_count - sdata->mc_count +
				  dev->mc_count;
		sdata->mc_count = dev->mc_count;
	}
	if (local->ops->set_multicast_list) {
		flags = local->mdev->flags;
		if (local->iff_allmultis)
			flags |= IFF_ALLMULTI;
		if (local->iff_promiscs)
			flags |= IFF_PROMISC;
		read_lock(&local->sub_if_lock);
		local->ops->set_multicast_list(local_to_hw(local), flags,
					      local->mc_count);
		read_unlock(&local->sub_if_lock);
	}
	netif_tx_unlock(local->mdev);
}

struct dev_mc_list *ieee80211_get_mc_list_item(struct ieee80211_hw *hw,
					       struct dev_mc_list *prev,
					       void **ptr)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata = *ptr;
	struct dev_mc_list *mc;

	if (!prev) {
		WARN_ON(sdata);
		sdata = NULL;
	}
	if (!prev || !prev->next) {
		if (sdata)
			sdata = list_entry(sdata->list.next,
					   struct ieee80211_sub_if_data, list);
		else
			sdata = list_entry(local->sub_if_list.next,
					   struct ieee80211_sub_if_data, list);
		if (&sdata->list != &local->sub_if_list)
			mc = sdata->dev->mc_list;
		else
			mc = NULL;
	} else
		mc = prev->next;

	*ptr = sdata;
	return mc;
}
EXPORT_SYMBOL(ieee80211_get_mc_list_item);

static struct net_device_stats *ieee80211_get_stats(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	return &(sdata->stats);
}

static void ieee80211_if_shutdown(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ASSERT_RTNL();
	switch (sdata->type) {
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		sdata->u.sta.state = IEEE80211_DISABLED;
		del_timer_sync(&sdata->u.sta.timer);
		skb_queue_purge(&sdata->u.sta.skb_queue);
		if (!local->ops->hw_scan &&
		    local->scan_dev == sdata->dev) {
			local->sta_scanning = 0;
			cancel_delayed_work(&local->scan_work);
		}
		flush_workqueue(local->hw.workqueue);
		break;
	}
}

static inline int identical_mac_addr_allowed(int type1, int type2)
{
	return (type1 == IEEE80211_IF_TYPE_MNTR ||
		type2 == IEEE80211_IF_TYPE_MNTR ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_WDS) ||
		(type1 == IEEE80211_IF_TYPE_WDS &&
		 (type2 == IEEE80211_IF_TYPE_WDS ||
		  type2 == IEEE80211_IF_TYPE_AP)) ||
		(type1 == IEEE80211_IF_TYPE_AP &&
		 type2 == IEEE80211_IF_TYPE_VLAN) ||
		(type1 == IEEE80211_IF_TYPE_VLAN &&
		 (type2 == IEEE80211_IF_TYPE_AP ||
		  type2 == IEEE80211_IF_TYPE_VLAN)));
}

static int ieee80211_master_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int res = -EOPNOTSUPP;

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		if (sdata->dev != dev && netif_running(sdata->dev)) {
			res = 0;
			break;
		}
	}
	read_unlock(&local->sub_if_lock);
	return res;
}

static int ieee80211_master_stop(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list)
		if (sdata->dev != dev && netif_running(sdata->dev))
			dev_close(sdata->dev);
	read_unlock(&local->sub_if_lock);

	return 0;
}

static int ieee80211_mgmt_open(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (!netif_running(local->mdev))
		return -EOPNOTSUPP;
	return 0;
}

static int ieee80211_mgmt_stop(struct net_device *dev)
{
	return 0;
}

/* Check if running monitor interfaces should go to a "soft monitor" mode
 * and switch them if necessary. */
static inline void ieee80211_start_soft_monitor(struct ieee80211_local *local)
{
	struct ieee80211_if_init_conf conf;

	if (local->open_count && local->open_count == local->monitors &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER) &&
	    local->ops->remove_interface) {
		conf.if_id = -1;
		conf.type = IEEE80211_IF_TYPE_MNTR;
		conf.mac_addr = NULL;
		local->ops->remove_interface(local_to_hw(local), &conf);
	}
}

/* Check if running monitor interfaces should go to a "hard monitor" mode
 * and switch them if necessary. */
static void ieee80211_start_hard_monitor(struct ieee80211_local *local)
{
	struct ieee80211_if_init_conf conf;

	if (local->open_count && local->open_count == local->monitors &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER)) {
		conf.if_id = -1;
		conf.type = IEEE80211_IF_TYPE_MNTR;
		conf.mac_addr = NULL;
		local->ops->add_interface(local_to_hw(local), &conf);
	}
}

static int ieee80211_open(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata, *nsdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_init_conf conf;
	int res;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	read_lock(&local->sub_if_lock);
	list_for_each_entry(nsdata, &local->sub_if_list, list) {
		struct net_device *ndev = nsdata->dev;

		if (ndev != dev && ndev != local->mdev && netif_running(ndev) &&
		    compare_ether_addr(dev->dev_addr, ndev->dev_addr) == 0 &&
		    !identical_mac_addr_allowed(sdata->type, nsdata->type)) {
			read_unlock(&local->sub_if_lock);
			return -ENOTUNIQ;
		}
	}
	read_unlock(&local->sub_if_lock);

	if (sdata->type == IEEE80211_IF_TYPE_WDS &&
	    is_zero_ether_addr(sdata->u.wds.remote_addr))
		return -ENOLINK;

	if (sdata->type == IEEE80211_IF_TYPE_MNTR && local->open_count &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER)) {
		/* run the interface in a "soft monitor" mode */
		local->monitors++;
		local->open_count++;
		local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
		return 0;
	}
	ieee80211_start_soft_monitor(local);

	conf.if_id = dev->ifindex;
	conf.type = sdata->type;
	conf.mac_addr = dev->dev_addr;
	res = local->ops->add_interface(local_to_hw(local), &conf);
	if (res) {
		if (sdata->type == IEEE80211_IF_TYPE_MNTR)
			ieee80211_start_hard_monitor(local);
		return res;
	}

	if (local->open_count == 0) {
		res = 0;
		tasklet_enable(&local->tx_pending_tasklet);
		tasklet_enable(&local->tasklet);
		if (local->ops->open)
			res = local->ops->open(local_to_hw(local));
		if (res == 0) {
			res = dev_open(local->mdev);
			if (res) {
				if (local->ops->stop)
					local->ops->stop(local_to_hw(local));
			} else {
				res = ieee80211_hw_config(local);
				if (res && local->ops->stop)
					local->ops->stop(local_to_hw(local));
				else if (!res && local->apdev)
					dev_open(local->apdev);
			}
		}
		if (res) {
			if (local->ops->remove_interface)
				local->ops->remove_interface(local_to_hw(local),
							    &conf);
			return res;
		}
	}
	local->open_count++;

	if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
		local->monitors++;
		local->hw.conf.flags |= IEEE80211_CONF_RADIOTAP;
	} else
		ieee80211_if_config(dev);

	if (sdata->type == IEEE80211_IF_TYPE_STA &&
	    !local->user_space_mlme)
		netif_carrier_off(dev);
	else
		netif_carrier_on(dev);

	netif_start_queue(dev);
	return 0;
}


static int ieee80211_stop(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->type == IEEE80211_IF_TYPE_MNTR &&
	    local->open_count > 1 &&
	    !(local->hw.flags & IEEE80211_HW_MONITOR_DURING_OPER)) {
		/* remove "soft monitor" interface */
		local->open_count--;
		local->monitors--;
		if (!local->monitors)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;
		return 0;
	}

	netif_stop_queue(dev);
	ieee80211_if_shutdown(dev);

	if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
		local->monitors--;
		if (!local->monitors)
			local->hw.conf.flags &= ~IEEE80211_CONF_RADIOTAP;
	}

	local->open_count--;
	if (local->open_count == 0) {
		if (netif_running(local->mdev))
			dev_close(local->mdev);
		if (local->apdev)
			dev_close(local->apdev);
		if (local->ops->stop)
			local->ops->stop(local_to_hw(local));
		tasklet_disable(&local->tx_pending_tasklet);
		tasklet_disable(&local->tasklet);
	}
	if (local->ops->remove_interface) {
		struct ieee80211_if_init_conf conf;

		conf.if_id = dev->ifindex;
		conf.type = sdata->type;
		conf.mac_addr = dev->dev_addr;
		local->ops->remove_interface(local_to_hw(local), &conf);
	}

	ieee80211_start_hard_monitor(local);

	return 0;
}


static int header_parse_80211(struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN); /* addr2 */
	return ETH_ALEN;
}

struct ieee80211_rate *
ieee80211_get_rate(struct ieee80211_local *local, int phymode, int hw_rate)
{
	struct ieee80211_hw_mode *mode;
	int r;

	list_for_each_entry(mode, &local->modes_list, list) {
		if (mode->mode != phymode)
			continue;
		for (r = 0; r < mode->num_rates; r++) {
			struct ieee80211_rate *rate = &mode->rates[r];
			if (rate->val == hw_rate ||
			    (rate->flags & IEEE80211_RATE_PREAMBLE2 &&
			     rate->val2 == hw_rate))
				return rate;
		}
	}

	return NULL;
}

static void
ieee80211_fill_frame_info(struct ieee80211_local *local,
			  struct ieee80211_frame_info *fi,
			  struct ieee80211_rx_status *status)
{
	if (status) {
		struct timespec ts;
		struct ieee80211_rate *rate;

		jiffies_to_timespec(jiffies, &ts);
		fi->hosttime = cpu_to_be64((u64) ts.tv_sec * 1000000 +
					   ts.tv_nsec / 1000);
		fi->mactime = cpu_to_be64(status->mactime);
		switch (status->phymode) {
		case MODE_IEEE80211A:
			fi->phytype = htonl(ieee80211_phytype_ofdm_dot11_a);
			break;
		case MODE_IEEE80211B:
			fi->phytype = htonl(ieee80211_phytype_dsss_dot11_b);
			break;
		case MODE_IEEE80211G:
			fi->phytype = htonl(ieee80211_phytype_pbcc_dot11_g);
			break;
		case MODE_ATHEROS_TURBO:
			fi->phytype =
				htonl(ieee80211_phytype_dsss_dot11_turbo);
			break;
		default:
			fi->phytype = htonl(0xAAAAAAAA);
			break;
		}
		fi->channel = htonl(status->channel);
		rate = ieee80211_get_rate(local, status->phymode,
					  status->rate);
		if (rate) {
			fi->datarate = htonl(rate->rate);
			if (rate->flags & IEEE80211_RATE_PREAMBLE2) {
				if (status->rate == rate->val)
					fi->preamble = htonl(2); /* long */
				else if (status->rate == rate->val2)
					fi->preamble = htonl(1); /* short */
			} else
				fi->preamble = htonl(0);
		} else {
			fi->datarate = htonl(0);
			fi->preamble = htonl(0);
		}

		fi->antenna = htonl(status->antenna);
		fi->priority = htonl(0xffffffff); /* no clue */
		fi->ssi_type = htonl(ieee80211_ssi_raw);
		fi->ssi_signal = htonl(status->ssi);
		fi->ssi_noise = 0x00000000;
		fi->encoding = 0;
	} else {
		/* clear everything because we really don't know.
		 * the msg_type field isn't present on monitor frames
		 * so we don't know whether it will be present or not,
		 * but it's ok to not clear it since it'll be assigned
		 * anyway */
		memset(fi, 0, sizeof(*fi) - sizeof(fi->msg_type));

		fi->ssi_type = htonl(ieee80211_ssi_none);
	}
	fi->version = htonl(IEEE80211_FI_VERSION);
	fi->length = cpu_to_be32(sizeof(*fi) - sizeof(fi->msg_type));
}

/* this routine is actually not just for this, but also
 * for pushing fake 'management' frames into userspace.
 * it shall be replaced by a netlink-based system. */
void
ieee80211_rx_mgmt(struct ieee80211_local *local, struct sk_buff *skb,
		  struct ieee80211_rx_status *status, u32 msg_type)
{
	struct ieee80211_frame_info *fi;
	const size_t hlen = sizeof(struct ieee80211_frame_info);
	struct ieee80211_sub_if_data *sdata;

	skb->dev = local->apdev;

	sdata = IEEE80211_DEV_TO_SUB_IF(local->apdev);

	if (skb_headroom(skb) < hlen) {
		I802_DEBUG_INC(local->rx_expand_skb_head);
		if (pskb_expand_head(skb, hlen, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return;
		}
	}

	fi = (struct ieee80211_frame_info *) skb_push(skb, hlen);

	ieee80211_fill_frame_info(local, fi, status);
	fi->msg_type = htonl(msg_type);

	sdata->stats.rx_packets++;
	sdata->stats.rx_bytes += skb->len;

	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

int ieee80211_radar_status(struct ieee80211_hw *hw, int channel,
			   int radar, int radar_type)
{
	struct sk_buff *skb;
	struct ieee80211_radar_info *msg;
	struct ieee80211_local *local = hw_to_local(hw);

	if (!local->apdev)
		return 0;

	skb = dev_alloc_skb(sizeof(struct ieee80211_frame_info) +
			    sizeof(struct ieee80211_radar_info));

	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, sizeof(struct ieee80211_frame_info));

	msg = (struct ieee80211_radar_info *)
		skb_put(skb, sizeof(struct ieee80211_radar_info));
	msg->channel = channel;
	msg->radar = radar;
	msg->radar_type = radar_type;

	ieee80211_rx_mgmt(local, skb, NULL, ieee80211_msg_radar);
	return 0;
}
EXPORT_SYMBOL(ieee80211_radar_status);


static void ieee80211_stat_refresh(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;

	if (!local->stat_time)
		return;

	/* go through all stations */
	spin_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		sta->channel_use = (sta->channel_use_raw / local->stat_time) /
			CHAN_UTIL_PER_10MS;
		sta->channel_use_raw = 0;
	}
	spin_unlock_bh(&local->sta_lock);

	/* go through all subinterfaces */
	read_lock(&local->sub_if_lock);
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		sdata->channel_use = (sdata->channel_use_raw /
				      local->stat_time) / CHAN_UTIL_PER_10MS;
		sdata->channel_use_raw = 0;
	}
	read_unlock(&local->sub_if_lock);

	/* hardware interface */
	local->channel_use = (local->channel_use_raw /
			      local->stat_time) / CHAN_UTIL_PER_10MS;
	local->channel_use_raw = 0;

	local->stat_timer.expires = jiffies + HZ * local->stat_time / 100;
	add_timer(&local->stat_timer);
}


void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb,
				 struct ieee80211_tx_status *status)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_status *saved;
	int tmp;

	skb->dev = local->mdev;
	saved = kmalloc(sizeof(struct ieee80211_tx_status), GFP_ATOMIC);
	if (unlikely(!saved)) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Not enough memory, "
			       "dropping tx status", skb->dev->name);
		/* should be dev_kfree_skb_irq, but due to this function being
		 * named _irqsafe instead of just _irq we can't be sure that
		 * people won't call it from non-irq contexts */
		dev_kfree_skb_any(skb);
		return;
	}
	memcpy(saved, status, sizeof(struct ieee80211_tx_status));
	/* copy pointer to saved status into skb->cb for use by tasklet */
	memcpy(skb->cb, &saved, sizeof(saved));

	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	skb_queue_tail(status->control.flags & IEEE80211_TXCTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = skb_queue_len(&local->skb_queue) +
		skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		memcpy(&saved, skb->cb, sizeof(saved));
		kfree(saved);
		dev_kfree_skb_irq(skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);

static void ieee80211_tasklet_handler(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *) data;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_tx_status *tx_status;

	while ((skb = skb_dequeue(&local->skb_queue)) ||
	       (skb = skb_dequeue(&local->skb_queue_unreliable))) {
		switch (skb->pkt_type) {
		case IEEE80211_RX_MSG:
			/* status is in skb->cb */
			memcpy(&rx_status, skb->cb, sizeof(rx_status));
			/* Clear skb->type in order to not confuse kernel
			 * netstack. */
			skb->pkt_type = 0;
			__ieee80211_rx(local_to_hw(local), skb, &rx_status);
			break;
		case IEEE80211_TX_STATUS_MSG:
			/* get pointer to saved status out of skb->cb */
			memcpy(&tx_status, skb->cb, sizeof(tx_status));
			skb->pkt_type = 0;
			ieee80211_tx_status(local_to_hw(local),
					    skb, tx_status);
			kfree(tx_status);
			break;
		default: /* should never get here! */
			printk(KERN_ERR "%s: Unknown message type (%d)\n",
			       local->mdev->name, skb->pkt_type);
			dev_kfree_skb(skb);
			break;
		}
	}
}


/* Remove added headers (e.g., QoS control), encryption header/MIC, etc. to
 * make a prepared TX frame (one that has been given to hw) to look like brand
 * new IEEE 802.11 frame that is ready to go through TX processing again.
 * Also, tx_packet_data in cb is restored from tx_control. */
static void ieee80211_remove_tx_extra(struct ieee80211_local *local,
				      struct ieee80211_key *key,
				      struct sk_buff *skb,
				      struct ieee80211_tx_control *control)
{
	int hdrlen, iv_len, mic_len;
	struct ieee80211_tx_packet_data *pkt_data;

	pkt_data = (struct ieee80211_tx_packet_data *)skb->cb;
	pkt_data->ifindex = control->ifindex;
	pkt_data->mgmt_iface = (control->type == IEEE80211_IF_TYPE_MGMT);
	pkt_data->req_tx_status = !!(control->flags & IEEE80211_TXCTL_REQ_TX_STATUS);
	pkt_data->do_not_encrypt = !!(control->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT);
	pkt_data->requeue = !!(control->flags & IEEE80211_TXCTL_REQUEUE);
	pkt_data->queue = control->queue;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);

	if (!key)
		goto no_key;

	switch (key->alg) {
	case ALG_WEP:
		iv_len = WEP_IV_LEN;
		mic_len = WEP_ICV_LEN;
		break;
	case ALG_TKIP:
		iv_len = TKIP_IV_LEN;
		mic_len = TKIP_ICV_LEN;
		break;
	case ALG_CCMP:
		iv_len = CCMP_HDR_LEN;
		mic_len = CCMP_MIC_LEN;
		break;
	default:
		goto no_key;
	}

	if (skb->len >= mic_len && key->force_sw_encrypt)
		skb_trim(skb, skb->len - mic_len);
	if (skb->len >= iv_len && skb->len > hdrlen) {
		memmove(skb->data + iv_len, skb->data, hdrlen);
		skb_pull(skb, iv_len);
	}

no_key:
	{
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		u16 fc = le16_to_cpu(hdr->frame_control);
		if ((fc & 0x8C) == 0x88) /* QoS Control Field */ {
			fc &= ~IEEE80211_STYPE_QOS_DATA;
			hdr->frame_control = cpu_to_le16(fc);
			memmove(skb->data + 2, skb->data, hdrlen - 2);
			skb_pull(skb, 2);
		}
	}
}


void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb,
			 struct ieee80211_tx_status *status)
{
	struct sk_buff *skb2;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = hw_to_local(hw);
	u16 frag, type;
	u32 msg_type;
	struct ieee80211_tx_status_rtap_hdr *rthdr;
	struct ieee80211_sub_if_data *sdata;
	int monitors;

	if (!status) {
		printk(KERN_ERR
		       "%s: ieee80211_tx_status called with NULL status\n",
		       local->mdev->name);
		dev_kfree_skb(skb);
		return;
	}

	if (status->excessive_retries) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			if (sta->flags & WLAN_STA_PS) {
				/* The STA is in power save mode, so assume
				 * that this TX packet failed because of that.
				 */
				status->excessive_retries = 0;
				status->flags |= IEEE80211_TX_STATUS_TX_FILTERED;
			}
			sta_info_put(sta);
		}
	}

	if (status->flags & IEEE80211_TX_STATUS_TX_FILTERED) {
		struct sta_info *sta;
		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			sta->tx_filtered_count++;

			/* Clear the TX filter mask for this STA when sending
			 * the next packet. If the STA went to power save mode,
			 * this will happen when it is waking up for the next
			 * time. */
			sta->clear_dst_mask = 1;

			/* TODO: Is the WLAN_STA_PS flag always set here or is
			 * the race between RX and TX status causing some
			 * packets to be filtered out before 80211.o gets an
			 * update for PS status? This seems to be the case, so
			 * no changes are likely to be needed. */
			if (sta->flags & WLAN_STA_PS &&
			    skb_queue_len(&sta->tx_filtered) <
			    STA_MAX_TX_BUFFER) {
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				skb_queue_tail(&sta->tx_filtered, skb);
			} else if (!(sta->flags & WLAN_STA_PS) &&
				   !(status->control.flags & IEEE80211_TXCTL_REQUEUE)) {
				/* Software retry the packet once */
				status->control.flags |= IEEE80211_TXCTL_REQUEUE;
				ieee80211_remove_tx_extra(local, sta->key,
							  skb,
							  &status->control);
				dev_queue_xmit(skb);
			} else {
				if (net_ratelimit()) {
					printk(KERN_DEBUG "%s: dropped TX "
					       "filtered frame queue_len=%d "
					       "PS=%d @%lu\n",
					       local->mdev->name,
					       skb_queue_len(
						       &sta->tx_filtered),
					       !!(sta->flags & WLAN_STA_PS),
					       jiffies);
				}
				dev_kfree_skb(skb);
			}
			sta_info_put(sta);
			return;
		}
	} else {
		/* FIXME: STUPID to call this with both local and local->mdev */
		rate_control_tx_status(local, local->mdev, skb, status);
	}

	ieee80211_led_tx(local, 0);

	/* SNMP counters
	 * Fragments are passed to low-level drivers as separate skbs, so these
	 * are actually fragments, not frames. Update frame counters only for
	 * the first fragment of the frame. */

	frag = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;

	if (status->flags & IEEE80211_TX_STATUS_ACK) {
		if (frag == 0) {
			local->dot11TransmittedFrameCount++;
			if (is_multicast_ether_addr(hdr->addr1))
				local->dot11MulticastTransmittedFrameCount++;
			if (status->retry_count > 0)
				local->dot11RetryCount++;
			if (status->retry_count > 1)
				local->dot11MultipleRetryCount++;
		}

		/* This counter shall be incremented for an acknowledged MPDU
		 * with an individual address in the address 1 field or an MPDU
		 * with a multicast address in the address 1 field of type Data
		 * or Management. */
		if (!is_multicast_ether_addr(hdr->addr1) ||
		    type == IEEE80211_FTYPE_DATA ||
		    type == IEEE80211_FTYPE_MGMT)
			local->dot11TransmittedFragmentCount++;
	} else {
		if (frag == 0)
			local->dot11FailedCount++;
	}

	msg_type = (status->flags & IEEE80211_TX_STATUS_ACK) ?
		ieee80211_msg_tx_callback_ack : ieee80211_msg_tx_callback_fail;

	/* this was a transmitted frame, but now we want to reuse it */
	skb_orphan(skb);

	if ((status->control.flags & IEEE80211_TXCTL_REQ_TX_STATUS) &&
	    local->apdev) {
		if (local->monitors) {
			skb2 = skb_clone(skb, GFP_ATOMIC);
		} else {
			skb2 = skb;
			skb = NULL;
		}

		if (skb2)
			/* Send frame to hostapd */
			ieee80211_rx_mgmt(local, skb2, NULL, msg_type);

		if (!skb)
			return;
	}

	if (!local->monitors) {
		dev_kfree_skb(skb);
		return;
	}

	/* send frame to monitor interfaces now */

	if (skb_headroom(skb) < sizeof(*rthdr)) {
		printk(KERN_ERR "ieee80211_tx_status: headroom too small\n");
		dev_kfree_skb(skb);
		return;
	}

	rthdr = (struct ieee80211_tx_status_rtap_hdr*)
				skb_push(skb, sizeof(*rthdr));

	memset(rthdr, 0, sizeof(*rthdr));
	rthdr->hdr.it_len = cpu_to_le16(sizeof(*rthdr));
	rthdr->hdr.it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));

	if (!(status->flags & IEEE80211_TX_STATUS_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_FAIL);

	if ((status->control.flags & IEEE80211_TXCTL_USE_RTS_CTS) &&
	    (status->control.flags & IEEE80211_TXCTL_USE_CTS_PROTECT))
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_CTS);
	else if (status->control.flags & IEEE80211_TXCTL_USE_RTS_CTS)
		rthdr->tx_flags |= cpu_to_le16(IEEE80211_RADIOTAP_F_TX_RTS);

	rthdr->data_retries = status->retry_count;

	read_lock(&local->sub_if_lock);
	monitors = local->monitors;
	list_for_each_entry(sdata, &local->sub_if_list, list) {
		/*
		 * Using the monitors counter is possibly racy, but
		 * if the value is wrong we simply either clone the skb
		 * once too much or forget sending it to one monitor iface
		 * The latter case isn't nice but fixing the race is much
		 * more complicated.
		 */
		if (!monitors || !skb)
			goto out;

		if (sdata->type == IEEE80211_IF_TYPE_MNTR) {
			if (!netif_running(sdata->dev))
				continue;
			monitors--;
			if (monitors)
				skb2 = skb_clone(skb, GFP_KERNEL);
			else
				skb2 = NULL;
			skb->dev = sdata->dev;
			/* XXX: is this sufficient for BPF? */
			skb_set_mac_header(skb, 0);
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->pkt_type = PACKET_OTHERHOST;
			skb->protocol = htons(ETH_P_802_2);
			memset(skb->cb, 0, sizeof(skb->cb));
			netif_rx(skb);
			skb = skb2;
		}
	}
 out:
	read_unlock(&local->sub_if_lock);
	if (skb)
		dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_tx_status);


int ieee80211_if_update_wds(struct net_device *dev, u8 *remote_addr)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;

	if (compare_ether_addr(remote_addr, sdata->u.wds.remote_addr) == 0)
		return 0;

	/* Create STA entry for the new peer */
	sta = sta_info_add(local, dev, remote_addr, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;
	sta_info_put(sta);

	/* Remove STA entry for the old peer */
	sta = sta_info_get(local, sdata->u.wds.remote_addr);
	if (sta) {
		sta_info_put(sta);
		sta_info_free(sta, 0);
	} else {
		printk(KERN_DEBUG "%s: could not find STA entry for WDS link "
		       "peer " MAC_FMT "\n",
		       dev->name, MAC_ARG(sdata->u.wds.remote_addr));
	}

	/* Update WDS link data */
	memcpy(&sdata->u.wds.remote_addr, remote_addr, ETH_ALEN);

	return 0;
}

/* Must not be called for mdev and apdev */
void ieee80211_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_subif_start_xmit;
	dev->wireless_handlers = &ieee80211_iw_handler_def;
	dev->set_multicast_list = ieee80211_set_multicast_list;
	dev->change_mtu = ieee80211_change_mtu;
	dev->get_stats = ieee80211_get_stats;
	dev->open = ieee80211_open;
	dev->stop = ieee80211_stop;
	dev->uninit = ieee80211_if_reinit;
	dev->destructor = ieee80211_if_free;
}

void ieee80211_if_mgmt_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->hard_start_xmit = ieee80211_mgmt_start_xmit;
	dev->change_mtu = ieee80211_change_mtu_apdev;
	dev->get_stats = ieee80211_get_stats;
	dev->open = ieee80211_mgmt_open;
	dev->stop = ieee80211_mgmt_stop;
	dev->type = ARPHRD_IEEE80211_PRISM;
	dev->hard_header_parse = header_parse_80211;
	dev->uninit = ieee80211_if_reinit;
	dev->destructor = ieee80211_if_free;
}

int ieee80211_init_rate_ctrl_alg(struct ieee80211_local *local,
				 const char *name)
{
	struct rate_control_ref *ref, *old;

	ASSERT_RTNL();
	if (local->open_count || netif_running(local->mdev) ||
	    (local->apdev && netif_running(local->apdev)))
		return -EBUSY;

	ref = rate_control_alloc(name, local);
	if (!ref) {
		printk(KERN_WARNING "%s: Failed to select rate control "
		       "algorithm\n", local->mdev->name);
		return -ENOENT;
	}

	old = local->rate_ctrl;
	local->rate_ctrl = ref;
	if (old) {
		rate_control_put(old);
		sta_info_flush(local, NULL);
	}

	printk(KERN_DEBUG "%s: Selected rate control "
	       "algorithm '%s'\n", local->mdev->name,
	       ref->ops->name);


	return 0;
}

static void rate_control_deinitialize(struct ieee80211_local *local)
{
	struct rate_control_ref *ref;

	ref = local->rate_ctrl;
	local->rate_ctrl = NULL;
	rate_control_put(ref);
}

struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	struct net_device *mdev;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	int priv_size;
	struct wiphy *wiphy;

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wiphy priv data for both our ieee80211_local and for
	 * the driver's private data
	 *
	 * In memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wiphy	    |
	 * +-------------------------+
	 * | struct ieee80211_local  |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 */
	priv_size = ((sizeof(struct ieee80211_local) +
		      NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST) +
		    priv_data_len;

	wiphy = wiphy_new(&mac80211_config_ops, priv_size);

	if (!wiphy)
		return NULL;

	wiphy->privid = mac80211_wiphy_privid;

	local = wiphy_priv(wiphy);
	local->hw.wiphy = wiphy;

	local->hw.priv = (char *)local +
			 ((sizeof(struct ieee80211_local) +
			   NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST);

	BUG_ON(!ops->tx);
	BUG_ON(!ops->config);
	BUG_ON(!ops->add_interface);
	local->ops = ops;

	/* for now, mdev needs sub_if_data :/ */
	mdev = alloc_netdev(sizeof(struct ieee80211_sub_if_data),
			    "wmaster%d", ether_setup);
	if (!mdev) {
		wiphy_free(wiphy);
		return NULL;
	}

	sdata = IEEE80211_DEV_TO_SUB_IF(mdev);
	mdev->ieee80211_ptr = &sdata->wdev;
	sdata->wdev.wiphy = wiphy;

	local->hw.queues = 1; /* default */

	local->mdev = mdev;
	local->rx_pre_handlers = ieee80211_rx_pre_handlers;
	local->rx_handlers = ieee80211_rx_handlers;
	local->tx_handlers = ieee80211_tx_handlers;

	local->bridge_packets = 1;

	local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	local->short_retry_limit = 7;
	local->long_retry_limit = 4;
	local->hw.conf.radio_enabled = 1;

	local->enabled_modes = (unsigned int) -1;

	INIT_LIST_HEAD(&local->modes_list);

	rwlock_init(&local->sub_if_lock);
	INIT_LIST_HEAD(&local->sub_if_list);

	INIT_DELAYED_WORK(&local->scan_work, ieee80211_sta_scan_work);
	init_timer(&local->stat_timer);
	local->stat_timer.function = ieee80211_stat_refresh;
	local->stat_timer.data = (unsigned long) local;
	ieee80211_rx_bss_list_init(mdev);

	sta_info_init(local);

	mdev->hard_start_xmit = ieee80211_master_start_xmit;
	mdev->open = ieee80211_master_open;
	mdev->stop = ieee80211_master_stop;
	mdev->type = ARPHRD_IEEE80211;
	mdev->hard_header_parse = header_parse_80211;

	sdata->type = IEEE80211_IF_TYPE_AP;
	sdata->dev = mdev;
	sdata->local = local;
	sdata->u.ap.force_unicast_rateidx = -1;
	sdata->u.ap.max_ratectrl_rateidx = -1;
	ieee80211_if_sdata_init(sdata);
	list_add_tail(&sdata->list, &local->sub_if_list);

	tasklet_init(&local->tx_pending_tasklet, ieee80211_tx_pending,
		     (unsigned long)local);
	tasklet_disable(&local->tx_pending_tasklet);

	tasklet_init(&local->tasklet,
		     ieee80211_tasklet_handler,
		     (unsigned long) local);
	tasklet_disable(&local->tasklet);

	skb_queue_head_init(&local->skb_queue);
	skb_queue_head_init(&local->skb_queue_unreliable);

	return local_to_hw(local);
}
EXPORT_SYMBOL(ieee80211_alloc_hw);

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	const char *name;
	int result;

	result = wiphy_register(local->hw.wiphy);
	if (result < 0)
		return result;

	name = wiphy_dev(local->hw.wiphy)->driver->name;
	local->hw.workqueue = create_singlethread_workqueue(name);
	if (!local->hw.workqueue) {
		result = -ENOMEM;
		goto fail_workqueue;
	}

	/*
	 * The hardware needs headroom for sending the frame,
	 * and we need some headroom for passing the frame to monitor
	 * interfaces, but never both at the same time.
	 */
	local->tx_headroom = max_t(unsigned int , local->hw.extra_tx_headroom,
				   sizeof(struct ieee80211_tx_status_rtap_hdr));

	debugfs_hw_add(local);

	local->hw.conf.beacon_int = 1000;

	local->wstats_flags |= local->hw.max_rssi ?
			       IW_QUAL_LEVEL_UPDATED : IW_QUAL_LEVEL_INVALID;
	local->wstats_flags |= local->hw.max_signal ?
			       IW_QUAL_QUAL_UPDATED : IW_QUAL_QUAL_INVALID;
	local->wstats_flags |= local->hw.max_noise ?
			       IW_QUAL_NOISE_UPDATED : IW_QUAL_NOISE_INVALID;
	if (local->hw.max_rssi < 0 || local->hw.max_noise < 0)
		local->wstats_flags |= IW_QUAL_DBM;

	result = sta_info_start(local);
	if (result < 0)
		goto fail_sta_info;

	rtnl_lock();
	result = dev_alloc_name(local->mdev, local->mdev->name);
	if (result < 0)
		goto fail_dev;

	memcpy(local->mdev->dev_addr, local->hw.wiphy->perm_addr, ETH_ALEN);
	SET_NETDEV_DEV(local->mdev, wiphy_dev(local->hw.wiphy));

	result = register_netdevice(local->mdev);
	if (result < 0)
		goto fail_dev;

	ieee80211_debugfs_add_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));

	result = ieee80211_init_rate_ctrl_alg(local, NULL);
	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize rate control "
		       "algorithm\n", local->mdev->name);
		goto fail_rate;
	}

	result = ieee80211_wep_init(local);

	if (result < 0) {
		printk(KERN_DEBUG "%s: Failed to initialize wep\n",
		       local->mdev->name);
		goto fail_wep;
	}

	ieee80211_install_qdisc(local->mdev);

	/* add one default STA interface */
	result = ieee80211_if_add(local->mdev, "wlan%d", NULL,
				  IEEE80211_IF_TYPE_STA);
	if (result)
		printk(KERN_WARNING "%s: Failed to add default virtual iface\n",
		       local->mdev->name);

	local->reg_state = IEEE80211_DEV_REGISTERED;
	rtnl_unlock();

	ieee80211_led_init(local);

	return 0;

fail_wep:
	rate_control_deinitialize(local);
fail_rate:
	ieee80211_debugfs_remove_netdev(IEEE80211_DEV_TO_SUB_IF(local->mdev));
	unregister_netdevice(local->mdev);
fail_dev:
	rtnl_unlock();
	sta_info_stop(local);
fail_sta_info:
	debugfs_hw_del(local);
	destroy_workqueue(local->hw.workqueue);
fail_workqueue:
	wiphy_unregister(local->hw.wiphy);
	return result;
}
EXPORT_SYMBOL(ieee80211_register_hw);

int ieee80211_register_hwmode(struct ieee80211_hw *hw,
			      struct ieee80211_hw_mode *mode)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	int i;

	INIT_LIST_HEAD(&mode->list);
	list_add_tail(&mode->list, &local->modes_list);

	local->hw_modes |= (1 << mode->mode);
	for (i = 0; i < mode->num_rates; i++) {
		rate = &(mode->rates[i]);
		rate->rate_inv = CHAN_UTIL_RATE_LCM / rate->rate;
	}
	ieee80211_prepare_rates(local, mode);

	if (!local->oper_hw_mode) {
		/* Default to this mode */
		local->hw.conf.phymode = mode->mode;
		local->oper_hw_mode = local->scan_hw_mode = mode;
		local->oper_channel = local->scan_channel = &mode->channels[0];
		local->hw.conf.mode = local->oper_hw_mode;
		local->hw.conf.chan = local->oper_channel;
	}

	if (!(hw->flags & IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED))
		ieee80211_set_default_regdomain(mode);

	return 0;
}
EXPORT_SYMBOL(ieee80211_register_hwmode);

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata, *tmp;
	struct list_head tmp_list;
	int i;

	tasklet_kill(&local->tx_pending_tasklet);
	tasklet_kill(&local->tasklet);

	rtnl_lock();

	BUG_ON(local->reg_state != IEEE80211_DEV_REGISTERED);

	local->reg_state = IEEE80211_DEV_UNREGISTERED;
	if (local->apdev)
		ieee80211_if_del_mgmt(local);

	write_lock_bh(&local->sub_if_lock);
	list_replace_init(&local->sub_if_list, &tmp_list);
	write_unlock_bh(&local->sub_if_lock);

	list_for_each_entry_safe(sdata, tmp, &tmp_list, list)
		__ieee80211_if_del(local, sdata);

	rtnl_unlock();

	if (local->stat_time)
		del_timer_sync(&local->stat_timer);

	ieee80211_rx_bss_list_deinit(local->mdev);
	ieee80211_clear_tx_pending(local);
	sta_info_stop(local);
	rate_control_deinitialize(local);
	debugfs_hw_del(local);

	for (i = 0; i < NUM_IEEE80211_MODES; i++) {
		kfree(local->supp_rates[i]);
		kfree(local->basic_rates[i]);
	}

	if (skb_queue_len(&local->skb_queue)
			|| skb_queue_len(&local->skb_queue_unreliable))
		printk(KERN_WARNING "%s: skb_queue not empty\n",
		       local->mdev->name);
	skb_queue_purge(&local->skb_queue);
	skb_queue_purge(&local->skb_queue_unreliable);

	destroy_workqueue(local->hw.workqueue);
	wiphy_unregister(local->hw.wiphy);
	ieee80211_wep_free(local);
	ieee80211_led_exit(local);
}
EXPORT_SYMBOL(ieee80211_unregister_hw);

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	ieee80211_if_free(local->mdev);
	wiphy_free(local->hw.wiphy);
}
EXPORT_SYMBOL(ieee80211_free_hw);

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (test_and_clear_bit(IEEE80211_LINK_STATE_XOFF,
			       &local->state[queue])) {
		if (test_bit(IEEE80211_LINK_STATE_PENDING,
			     &local->state[queue]))
			tasklet_schedule(&local->tx_pending_tasklet);
		else
			if (!ieee80211_qdisc_installed(local->mdev)) {
				if (queue == 0)
					netif_wake_queue(local->mdev);
			} else
				__netif_schedule(local->mdev);
	}
}
EXPORT_SYMBOL(ieee80211_wake_queue);

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_qdisc_installed(local->mdev) && queue == 0)
		netif_stop_queue(local->mdev);
	set_bit(IEEE80211_LINK_STATE_XOFF, &local->state[queue]);
}
EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_start_queues(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	int i;

	for (i = 0; i < local->hw.queues; i++)
		clear_bit(IEEE80211_LINK_STATE_XOFF, &local->state[i]);
	if (!ieee80211_qdisc_installed(local->mdev))
		netif_start_queue(local->mdev);
}
EXPORT_SYMBOL(ieee80211_start_queues);

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_stop_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_stop_queues);

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_wake_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_wake_queues);

struct net_device_stats *ieee80211_dev_stats(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	return &sdata->stats;
}

static int __init ieee80211_init(void)
{
	struct sk_buff *skb;
	int ret;

	BUILD_BUG_ON(sizeof(struct ieee80211_tx_packet_data) > sizeof(skb->cb));

	ret = ieee80211_wme_register();
	if (ret) {
		printk(KERN_DEBUG "ieee80211_init: failed to "
		       "initialize WME (err=%d)\n", ret);
		return ret;
	}

	ieee80211_debugfs_netdev_init();
	ieee80211_regdomain_init();

	return 0;
}


static void __exit ieee80211_exit(void)
{
	ieee80211_wme_unregister();
	ieee80211_debugfs_netdev_exit();
}


subsys_initcall(ieee80211_init);
module_exit(ieee80211_exit);

MODULE_DESCRIPTION("IEEE 802.11 subsystem");
MODULE_LICENSE("GPL");
