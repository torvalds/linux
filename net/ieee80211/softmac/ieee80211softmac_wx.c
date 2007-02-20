/*
 * This file contains our _wx handlers. Make sure you EXPORT_SYMBOL_GPL them
 *
 * Copyright (c) 2005, 2006 Johannes Berg <johannes@sipsolutions.net>
 *                          Joseph Jezak <josejx@gentoo.org>
 *                          Larry Finger <Larry.Finger@lwfinger.net>
 *                          Danny van Dyk <kugelfang@gentoo.org>
 *                          Michael Buesch <mbuesch@freenet.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

#include "ieee80211softmac_priv.h"

#include <net/iw_handler.h>
/* for is_broadcast_ether_addr and is_zero_ether_addr */
#include <linux/etherdevice.h>

int
ieee80211softmac_wx_trigger_scan(struct net_device *net_dev,
				 struct iw_request_info *info,
				 union iwreq_data *data,
				 char *extra)
{
	struct ieee80211softmac_device *sm = ieee80211_priv(net_dev);
	return ieee80211softmac_start_scan(sm);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_trigger_scan);


/* if we're still scanning, return -EAGAIN so that userspace tools
 * can get the complete scan results, otherwise return 0. */
int
ieee80211softmac_wx_get_scan_results(struct net_device *net_dev,
				     struct iw_request_info *info,
				     union iwreq_data *data,
				     char *extra)
{
	unsigned long flags;
	struct ieee80211softmac_device *sm = ieee80211_priv(net_dev);

	spin_lock_irqsave(&sm->lock, flags);
	if (sm->scanning) {
		spin_unlock_irqrestore(&sm->lock, flags);
		return -EAGAIN;
	}
	spin_unlock_irqrestore(&sm->lock, flags);
	return ieee80211_wx_get_scan(sm->ieee, info, data, extra);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_get_scan_results);

int
ieee80211softmac_wx_set_essid(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra)
{
	struct ieee80211softmac_device *sm = ieee80211_priv(net_dev);
	struct ieee80211softmac_network *n;
	struct ieee80211softmac_auth_queue_item *authptr;
	int length = 0;

	mutex_lock(&sm->associnfo.mutex);

	/* Check if we're already associating to this or another network
	 * If it's another network, cancel and start over with our new network
	 * If it's our network, ignore the change, we're already doing it!
	 */
	if((sm->associnfo.associating || sm->associnfo.associated) &&
	   (data->essid.flags && data->essid.length)) {
		/* Get the associating network */
		n = ieee80211softmac_get_network_by_bssid(sm, sm->associnfo.bssid);
		if(n && n->essid.len == data->essid.length &&
		   !memcmp(n->essid.data, extra, n->essid.len)) {
			dprintk(KERN_INFO PFX "Already associating or associated to "MAC_FMT"\n",
				MAC_ARG(sm->associnfo.bssid));
			goto out;
		} else {
			dprintk(KERN_INFO PFX "Canceling existing associate request!\n");
			/* Cancel assoc work */
			cancel_delayed_work(&sm->associnfo.work);
			/* We don't have to do this, but it's a little cleaner */
			list_for_each_entry(authptr, &sm->auth_queue, list)
				cancel_delayed_work(&authptr->work);
			sm->associnfo.bssvalid = 0;
			sm->associnfo.bssfixed = 0;
			flush_scheduled_work();
			sm->associnfo.associating = 0;
			sm->associnfo.associated = 0;
		}
	}


	sm->associnfo.static_essid = 0;
	sm->associnfo.assoc_wait = 0;

	if (data->essid.flags && data->essid.length) {
		length = min((int)data->essid.length, IW_ESSID_MAX_SIZE);
		if (length) {
			memcpy(sm->associnfo.req_essid.data, extra, length);
			sm->associnfo.static_essid = 1;
		}
	}

	/* set our requested ESSID length.
	 * If applicable, we have already copied the data in */
	sm->associnfo.req_essid.len = length;

	sm->associnfo.associating = 1;
	/* queue lower level code to do work (if necessary) */
	schedule_delayed_work(&sm->associnfo.work, 0);
out:
	mutex_unlock(&sm->associnfo.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_set_essid);

int
ieee80211softmac_wx_get_essid(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra)
{
	struct ieee80211softmac_device *sm = ieee80211_priv(net_dev);

	mutex_lock(&sm->associnfo.mutex);
	/* If all fails, return ANY (empty) */
	data->essid.length = 0;
	data->essid.flags = 0;  /* active */

	/* If we have a statically configured ESSID then return it */
	if (sm->associnfo.static_essid) {
		data->essid.length = sm->associnfo.req_essid.len;
		data->essid.flags = 1;  /* active */
		memcpy(extra, sm->associnfo.req_essid.data, sm->associnfo.req_essid.len);
	}

	/* If we're associating/associated, return that */
	if (sm->associnfo.associated || sm->associnfo.associating) {
		data->essid.length = sm->associnfo.associate_essid.len;
		data->essid.flags = 1;  /* active */
		memcpy(extra, sm->associnfo.associate_essid.data, sm->associnfo.associate_essid.len);
	}
	mutex_unlock(&sm->associnfo.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_get_essid);

int
ieee80211softmac_wx_set_rate(struct net_device *net_dev,
			     struct iw_request_info *info,
			     union iwreq_data *data,
			     char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(net_dev);
	struct ieee80211_device *ieee = mac->ieee;
	unsigned long flags;
	s32 in_rate = data->bitrate.value;
	u8 rate;
	int is_ofdm = 0;
	int err = -EINVAL;

	if (in_rate == -1) {
		if (ieee->modulation & IEEE80211_OFDM_MODULATION)
			in_rate = 24000000;
		else
			in_rate = 11000000;
	}

	switch (in_rate) {
	case 1000000:
		rate = IEEE80211_CCK_RATE_1MB;
		break;
	case 2000000:
		rate = IEEE80211_CCK_RATE_2MB;
		break;
	case 5500000:
		rate = IEEE80211_CCK_RATE_5MB;
		break;
	case 11000000:
		rate = IEEE80211_CCK_RATE_11MB;
		break;
	case 6000000:
		rate = IEEE80211_OFDM_RATE_6MB;
		is_ofdm = 1;
		break;
	case 9000000:
		rate = IEEE80211_OFDM_RATE_9MB;
		is_ofdm = 1;
		break;
	case 12000000:
		rate = IEEE80211_OFDM_RATE_12MB;
		is_ofdm = 1;
		break;
	case 18000000:
		rate = IEEE80211_OFDM_RATE_18MB;
		is_ofdm = 1;
		break;
	case 24000000:
		rate = IEEE80211_OFDM_RATE_24MB;
		is_ofdm = 1;
		break;
	case 36000000:
		rate = IEEE80211_OFDM_RATE_36MB;
		is_ofdm = 1;
		break;
	case 48000000:
		rate = IEEE80211_OFDM_RATE_48MB;
		is_ofdm = 1;
		break;
	case 54000000:
		rate = IEEE80211_OFDM_RATE_54MB;
		is_ofdm = 1;
		break;
	default:
		goto out;
	}

	spin_lock_irqsave(&mac->lock, flags);

	/* Check if correct modulation for this PHY. */
	if (is_ofdm && !(ieee->modulation & IEEE80211_OFDM_MODULATION))
		goto out_unlock;

	mac->txrates.user_rate = rate;
	ieee80211softmac_recalc_txrates(mac);
	err = 0;

out_unlock:
	spin_unlock_irqrestore(&mac->lock, flags);
out:
	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_set_rate);

int
ieee80211softmac_wx_get_rate(struct net_device *net_dev,
			     struct iw_request_info *info,
			     union iwreq_data *data,
			     char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(net_dev);
	unsigned long flags;
	int err = -EINVAL;

	spin_lock_irqsave(&mac->lock, flags);

	if (unlikely(!mac->running)) {
		err = -ENODEV;
		goto out_unlock;
	}

	switch (mac->txrates.default_rate) {
	case IEEE80211_CCK_RATE_1MB:
		data->bitrate.value = 1000000;
		break;
	case IEEE80211_CCK_RATE_2MB:
		data->bitrate.value = 2000000;
		break;
	case IEEE80211_CCK_RATE_5MB:
		data->bitrate.value = 5500000;
		break;
	case IEEE80211_CCK_RATE_11MB:
		data->bitrate.value = 11000000;
		break;
	case IEEE80211_OFDM_RATE_6MB:
		data->bitrate.value = 6000000;
		break;
	case IEEE80211_OFDM_RATE_9MB:
		data->bitrate.value = 9000000;
		break;
	case IEEE80211_OFDM_RATE_12MB:
		data->bitrate.value = 12000000;
		break;
	case IEEE80211_OFDM_RATE_18MB:
		data->bitrate.value = 18000000;
		break;
	case IEEE80211_OFDM_RATE_24MB:
		data->bitrate.value = 24000000;
		break;
	case IEEE80211_OFDM_RATE_36MB:
		data->bitrate.value = 36000000;
		break;
	case IEEE80211_OFDM_RATE_48MB:
		data->bitrate.value = 48000000;
		break;
	case IEEE80211_OFDM_RATE_54MB:
		data->bitrate.value = 54000000;
		break;
	default:
		assert(0);
		goto out_unlock;
	}
	err = 0;
out_unlock:
	spin_unlock_irqrestore(&mac->lock, flags);

	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_get_rate);

int
ieee80211softmac_wx_get_wap(struct net_device *net_dev,
			    struct iw_request_info *info,
			    union iwreq_data *data,
			    char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(net_dev);
	int err = 0;

	mutex_lock(&mac->associnfo.mutex);
	if (mac->associnfo.bssvalid)
		memcpy(data->ap_addr.sa_data, mac->associnfo.bssid, ETH_ALEN);
	else
		memset(data->ap_addr.sa_data, 0xff, ETH_ALEN);
	data->ap_addr.sa_family = ARPHRD_ETHER;
	mutex_unlock(&mac->associnfo.mutex);

	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_get_wap);

int
ieee80211softmac_wx_set_wap(struct net_device *net_dev,
			    struct iw_request_info *info,
			    union iwreq_data *data,
			    char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(net_dev);

	/* sanity check */
	if (data->ap_addr.sa_family != ARPHRD_ETHER) {
		return -EINVAL;
	}

	mutex_lock(&mac->associnfo.mutex);
	if (is_broadcast_ether_addr(data->ap_addr.sa_data)) {
		/* the bssid we have is not to be fixed any longer,
		 * and we should reassociate to the best AP. */
		mac->associnfo.bssfixed = 0;
		/* force reassociation */
		mac->associnfo.bssvalid = 0;
		if (mac->associnfo.associated)
			schedule_delayed_work(&mac->associnfo.work, 0);
	} else if (is_zero_ether_addr(data->ap_addr.sa_data)) {
		/* the bssid we have is no longer fixed */
		mac->associnfo.bssfixed = 0;
	} else {
		if (!memcmp(mac->associnfo.bssid, data->ap_addr.sa_data, ETH_ALEN)) {
			if (mac->associnfo.associating || mac->associnfo.associated) {
			/* bssid unchanged and associated or associating - just return */
				goto out;
			}
		} else {
			/* copy new value in data->ap_addr.sa_data to bssid */
			memcpy(mac->associnfo.bssid, data->ap_addr.sa_data, ETH_ALEN);
		}
		/* tell the other code that this bssid should be used no matter what */
		mac->associnfo.bssfixed = 1;
		/* queue associate if new bssid or (old one again and not associated) */
		schedule_delayed_work(&mac->associnfo.work, 0);
	}

 out:
	mutex_unlock(&mac->associnfo.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_set_wap);

int
ieee80211softmac_wx_set_genie(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	unsigned long flags;
	int err = 0;
	char *buf;
	int i;

	mutex_lock(&mac->associnfo.mutex);
	spin_lock_irqsave(&mac->lock, flags);
	/* bleh. shouldn't be locked for that kmalloc... */

	if (wrqu->data.length) {
		if ((wrqu->data.length < 2) || (extra[1]+2 != wrqu->data.length)) {
			/* this is an IE, so the length must be
			 * correct. Is it possible though that
			 * more than one IE is passed in?
			 */
			err = -EINVAL;
			goto out;
		}
		if (mac->wpa.IEbuflen <= wrqu->data.length) {
			buf = kmalloc(wrqu->data.length, GFP_ATOMIC);
			if (!buf) {
				err = -ENOMEM;
				goto out;
			}
			kfree(mac->wpa.IE);
			mac->wpa.IE = buf;
			mac->wpa.IEbuflen = wrqu->data.length;
		}
		memcpy(mac->wpa.IE, extra, wrqu->data.length);
		dprintk(KERN_INFO PFX "generic IE set to ");
		for (i=0;i<wrqu->data.length;i++)
			dprintk("%.2x", (u8)mac->wpa.IE[i]);
		dprintk("\n");
		mac->wpa.IElen = wrqu->data.length;
	} else {
		kfree(mac->wpa.IE);
		mac->wpa.IE = NULL;
		mac->wpa.IElen = 0;
		mac->wpa.IEbuflen = 0;
	}

 out:
	spin_unlock_irqrestore(&mac->lock, flags);
	mutex_unlock(&mac->associnfo.mutex);

	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_set_genie);

int
ieee80211softmac_wx_get_genie(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu,
			      char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	unsigned long flags;
	int err = 0;
	int space = wrqu->data.length;

	mutex_lock(&mac->associnfo.mutex);
	spin_lock_irqsave(&mac->lock, flags);

	wrqu->data.length = 0;

	if (mac->wpa.IE && mac->wpa.IElen) {
		wrqu->data.length = mac->wpa.IElen;
		if (mac->wpa.IElen <= space)
			memcpy(extra, mac->wpa.IE, mac->wpa.IElen);
		else
			err = -E2BIG;
	}
	spin_unlock_irqrestore(&mac->lock, flags);
	mutex_unlock(&mac->associnfo.mutex);

	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_get_genie);

int
ieee80211softmac_wx_set_mlme(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu,
			     char *extra)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	u16 reason = cpu_to_le16(mlme->reason_code);
	struct ieee80211softmac_network *net;
	int err = -EINVAL;

	mutex_lock(&mac->associnfo.mutex);

	if (memcmp(mac->associnfo.bssid, mlme->addr.sa_data, ETH_ALEN)) {
		printk(KERN_DEBUG PFX "wx_set_mlme: requested operation on net we don't use\n");
		goto out;
	}

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		net = ieee80211softmac_get_network_by_bssid_locked(mac, mlme->addr.sa_data);
		if (!net) {
			printk(KERN_DEBUG PFX "wx_set_mlme: we should know the net here...\n");
			goto out;
		}
		err =  ieee80211softmac_deauth_req(mac, net, reason);
		goto out;
	case IW_MLME_DISASSOC:
		ieee80211softmac_send_disassoc_req(mac, reason);
		mac->associnfo.associated = 0;
		mac->associnfo.associating = 0;
		err = 0;
		goto out;
	default:
		err = -EOPNOTSUPP;
	}

out:
	mutex_unlock(&mac->associnfo.mutex);

	return err;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_wx_set_mlme);
