/*
 * Contains some basic softmac functions along with module registration code etc.
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
#include <linux/sort.h>
#include <linux/etherdevice.h>

struct net_device *alloc_ieee80211softmac(int sizeof_priv)
{
	struct ieee80211softmac_device *softmac;
	struct net_device *dev;

	dev = alloc_ieee80211(sizeof(struct ieee80211softmac_device) + sizeof_priv);
	softmac = ieee80211_priv(dev);
	softmac->dev = dev;
	softmac->ieee = netdev_priv(dev);
	spin_lock_init(&softmac->lock);

	softmac->ieee->handle_auth = ieee80211softmac_auth_resp;
	softmac->ieee->handle_deauth = ieee80211softmac_deauth_resp;
	softmac->ieee->handle_assoc_response = ieee80211softmac_handle_assoc_response;
	softmac->ieee->handle_reassoc_request = ieee80211softmac_handle_reassoc_req;
	softmac->ieee->handle_disassoc = ieee80211softmac_handle_disassoc;
	softmac->ieee->handle_beacon = ieee80211softmac_handle_beacon;
	softmac->scaninfo = NULL;

	softmac->associnfo.scan_retry = IEEE80211SOFTMAC_ASSOC_SCAN_RETRY_LIMIT;

	/* TODO: initialise all the other callbacks in the ieee struct
	 *	 (once they're written)
	 */

	INIT_LIST_HEAD(&softmac->auth_queue);
	INIT_LIST_HEAD(&softmac->network_list);
	INIT_LIST_HEAD(&softmac->events);

	mutex_init(&softmac->associnfo.mutex);
	INIT_DELAYED_WORK(&softmac->associnfo.work, ieee80211softmac_assoc_work);
	INIT_DELAYED_WORK(&softmac->associnfo.timeout, ieee80211softmac_assoc_timeout);
	softmac->start_scan = ieee80211softmac_start_scan_implementation;
	softmac->wait_for_scan = ieee80211softmac_wait_for_scan_implementation;
	softmac->stop_scan = ieee80211softmac_stop_scan_implementation;

	/* to start with, we can't send anything ... */
	netif_carrier_off(dev);

	return dev;
}
EXPORT_SYMBOL_GPL(alloc_ieee80211softmac);

/* Clears the pending work queue items, stops all scans, etc. */
void
ieee80211softmac_clear_pending_work(struct ieee80211softmac_device *sm)
{
	unsigned long flags;
	struct ieee80211softmac_event *eventptr, *eventtmp;
	struct ieee80211softmac_auth_queue_item *authptr, *authtmp;
	struct ieee80211softmac_network *netptr, *nettmp;

	ieee80211softmac_stop_scan(sm);
	ieee80211softmac_wait_for_scan(sm);

	spin_lock_irqsave(&sm->lock, flags);
	sm->running = 0;

	/* Free all pending assoc work items */
	cancel_delayed_work(&sm->associnfo.work);

	/* Free all pending scan work items */
	if(sm->scaninfo != NULL)
		cancel_delayed_work(&sm->scaninfo->softmac_scan);

	/* Free all pending auth work items */
	list_for_each_entry(authptr, &sm->auth_queue, list)
		cancel_delayed_work(&authptr->work);

	/* delete all pending event calls and work items */
	list_for_each_entry_safe(eventptr, eventtmp, &sm->events, list)
		cancel_delayed_work(&eventptr->work);

	spin_unlock_irqrestore(&sm->lock, flags);
	flush_scheduled_work();

	/* now we should be save and no longer need locking... */
	spin_lock_irqsave(&sm->lock, flags);
	/* Free all pending auth work items */
	list_for_each_entry_safe(authptr, authtmp, &sm->auth_queue, list) {
		list_del(&authptr->list);
		kfree(authptr);
	}

	/* delete all pending event calls and work items */
	list_for_each_entry_safe(eventptr, eventtmp, &sm->events, list) {
		list_del(&eventptr->list);
		kfree(eventptr);
	}

	/* Free all networks */
	list_for_each_entry_safe(netptr, nettmp, &sm->network_list, list) {
		ieee80211softmac_del_network_locked(sm, netptr);
		if(netptr->challenge != NULL)
			kfree(netptr->challenge);
		kfree(netptr);
	}

	spin_unlock_irqrestore(&sm->lock, flags);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_clear_pending_work);

void free_ieee80211softmac(struct net_device *dev)
{
	struct ieee80211softmac_device *sm = ieee80211_priv(dev);
	ieee80211softmac_clear_pending_work(sm);
	kfree(sm->scaninfo);
	kfree(sm->wpa.IE);
	free_ieee80211(dev);
}
EXPORT_SYMBOL_GPL(free_ieee80211softmac);

static void ieee80211softmac_start_check_rates(struct ieee80211softmac_device *mac)
{
	struct ieee80211softmac_ratesinfo *ri = &mac->ratesinfo;
	/* I took out the sorting check, we're seperating by modulation now. */
	if (ri->count)
		return;
	/* otherwise assume we hav'em all! */
	if (mac->ieee->modulation & IEEE80211_CCK_MODULATION) {
		ri->rates[ri->count++] = IEEE80211_CCK_RATE_1MB;
		ri->rates[ri->count++] = IEEE80211_CCK_RATE_2MB;
		ri->rates[ri->count++] = IEEE80211_CCK_RATE_5MB;
		ri->rates[ri->count++] = IEEE80211_CCK_RATE_11MB;
	}
	if (mac->ieee->modulation & IEEE80211_OFDM_MODULATION) {
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_6MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_9MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_12MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_18MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_24MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_36MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_48MB;
		ri->rates[ri->count++] = IEEE80211_OFDM_RATE_54MB;
	}
}

int ieee80211softmac_ratesinfo_rate_supported(struct ieee80211softmac_ratesinfo *ri, u8 rate)
{
	int search;
	u8 search_rate;

	for (search = 0; search < ri->count; search++) {
		search_rate = ri->rates[search];
		search_rate &= ~IEEE80211_BASIC_RATE_MASK;
		if (rate == search_rate)
			return 1;
	}

	return 0;
}

u8 ieee80211softmac_highest_supported_rate(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_ratesinfo *ri, int basic_only)
{
	u8 user_rate = mac->txrates.user_rate;
	int i;

	if (ri->count == 0)
		return IEEE80211_CCK_RATE_1MB;

	for (i = ri->count - 1; i >= 0; i--) {
		u8 rate = ri->rates[i];
		if (basic_only && !(rate & IEEE80211_BASIC_RATE_MASK))
			continue;
		rate &= ~IEEE80211_BASIC_RATE_MASK;
		if (rate > user_rate)
			continue;
		if (ieee80211softmac_ratesinfo_rate_supported(&mac->ratesinfo, rate))
			return rate;
	}

	/* If we haven't found a suitable rate by now, just trust the user */
	return user_rate;
}
EXPORT_SYMBOL_GPL(ieee80211softmac_highest_supported_rate);

void ieee80211softmac_process_erp(struct ieee80211softmac_device *mac,
	u8 erp_value)
{
	int use_protection;
	int short_preamble;
	u32 changes = 0;

	/* Barker preamble mode */
	short_preamble = ((erp_value & WLAN_ERP_BARKER_PREAMBLE) == 0
			  && mac->associnfo.short_preamble_available) ? 1 : 0;

	/* Protection needed? */
	use_protection = (erp_value & WLAN_ERP_USE_PROTECTION) != 0;

	if (mac->bssinfo.short_preamble != short_preamble) {
		changes |= IEEE80211SOFTMAC_BSSINFOCHG_SHORT_PREAMBLE;
		mac->bssinfo.short_preamble = short_preamble;
	}

	if (mac->bssinfo.use_protection != use_protection) {
		changes |= IEEE80211SOFTMAC_BSSINFOCHG_PROTECTION;
		mac->bssinfo.use_protection = use_protection;
	}

	if (mac->bssinfo_change && changes)
		mac->bssinfo_change(mac->dev, changes);
}

void ieee80211softmac_recalc_txrates(struct ieee80211softmac_device *mac)
{
	struct ieee80211softmac_txrates *txrates = &mac->txrates;
	u32 change = 0;

	change |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT;
	txrates->default_rate = ieee80211softmac_highest_supported_rate(mac, &mac->bssinfo.supported_rates, 0);

	change |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT_FBACK;
	txrates->default_fallback = lower_rate(mac, txrates->default_rate);

	change |= IEEE80211SOFTMAC_TXRATECHG_MCAST;
	txrates->mcast_rate = ieee80211softmac_highest_supported_rate(mac, &mac->bssinfo.supported_rates, 1);

	if (mac->txrates_change)
		mac->txrates_change(mac->dev, change);

}

void ieee80211softmac_init_bss(struct ieee80211softmac_device *mac)
{
	struct ieee80211_device *ieee = mac->ieee;
	u32 change = 0;
	struct ieee80211softmac_txrates *txrates = &mac->txrates;
	struct ieee80211softmac_bss_info *bssinfo = &mac->bssinfo;

	/* TODO: We need some kind of state machine to lower the default rates
	 *       if we loose too many packets.
	 */
	/* Change the default txrate to the highest possible value.
	 * The txrate machine will lower it, if it is too high.
	 */
	if (ieee->modulation & IEEE80211_OFDM_MODULATION)
		txrates->user_rate = IEEE80211_OFDM_RATE_24MB;
	else
		txrates->user_rate = IEEE80211_CCK_RATE_11MB;

	txrates->default_rate = IEEE80211_CCK_RATE_1MB;
	change |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT;

	txrates->default_fallback = IEEE80211_CCK_RATE_1MB;
	change |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT_FBACK;

	txrates->mcast_rate = IEEE80211_CCK_RATE_1MB;
	change |= IEEE80211SOFTMAC_TXRATECHG_MCAST;

	txrates->mgt_mcast_rate = IEEE80211_CCK_RATE_1MB;
	change |= IEEE80211SOFTMAC_TXRATECHG_MGT_MCAST;

	if (mac->txrates_change)
		mac->txrates_change(mac->dev, change);

	change = 0;

	bssinfo->supported_rates.count = 0;
	memset(bssinfo->supported_rates.rates, 0,
		sizeof(bssinfo->supported_rates.rates));
	change |= IEEE80211SOFTMAC_BSSINFOCHG_RATES;

	bssinfo->short_preamble = 0;
	change |= IEEE80211SOFTMAC_BSSINFOCHG_SHORT_PREAMBLE;

	bssinfo->use_protection = 0;
	change |= IEEE80211SOFTMAC_BSSINFOCHG_PROTECTION;

	if (mac->bssinfo_change)
		mac->bssinfo_change(mac->dev, change);

	mac->running = 1;
}

void ieee80211softmac_start(struct net_device *dev)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);

	ieee80211softmac_start_check_rates(mac);
	ieee80211softmac_init_bss(mac);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_start);

void ieee80211softmac_stop(struct net_device *dev)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);

	ieee80211softmac_clear_pending_work(mac);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_stop);

void ieee80211softmac_set_rates(struct net_device *dev, u8 count, u8 *rates)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&mac->lock, flags);
	memcpy(mac->ratesinfo.rates, rates, count);
	mac->ratesinfo.count = count;
	spin_unlock_irqrestore(&mac->lock, flags);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_set_rates);

static u8 raise_rate(struct ieee80211softmac_device *mac, u8 rate)
{
	int i;
	struct ieee80211softmac_ratesinfo *ri = &mac->ratesinfo;

	for (i=0; i<ri->count-1; i++) {
		if (ri->rates[i] == rate)
			return ri->rates[i+1];
	}
	/* I guess we can't go any higher... */
	return ri->rates[ri->count];
}

u8 ieee80211softmac_lower_rate_delta(struct ieee80211softmac_device *mac, u8 rate, int delta)
{
	int i;
	struct ieee80211softmac_ratesinfo *ri = &mac->ratesinfo;

	for (i=delta; i<ri->count; i++) {
		if (ri->rates[i] == rate)
			return ri->rates[i-delta];
	}
	/* I guess we can't go any lower... */
	return ri->rates[0];
}

static void ieee80211softmac_add_txrates_badness(struct ieee80211softmac_device *mac,
						 int amount)
{
	u8 default_rate = mac->txrates.default_rate;
	u8 default_fallback = mac->txrates.default_fallback;
	u32 changes = 0;

	//TODO: This is highly experimental code.
	//      Maybe the dynamic rate selection does not work
	//      and it has to be removed again.

printk("badness %d\n", mac->txrate_badness);
	mac->txrate_badness += amount;
	if (mac->txrate_badness <= -1000) {
		/* Very small badness. Try a faster bitrate. */
		default_rate = raise_rate(mac, default_rate);
		changes |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT;
		default_fallback = get_fallback_rate(mac, default_rate);
		changes |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT_FBACK;
		mac->txrate_badness = 0;
printk("Bitrate raised to %u\n", default_rate);
	} else if (mac->txrate_badness >= 10000) {
		/* Very high badness. Try a slower bitrate. */
		default_rate = lower_rate(mac, default_rate);
		changes |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT;
		default_fallback = get_fallback_rate(mac, default_rate);
		changes |= IEEE80211SOFTMAC_TXRATECHG_DEFAULT_FBACK;
		mac->txrate_badness = 0;
printk("Bitrate lowered to %u\n", default_rate);
	}

	mac->txrates.default_rate = default_rate;
	mac->txrates.default_fallback = default_fallback;

	if (changes && mac->txrates_change)
		mac->txrates_change(mac->dev, changes);
}

void ieee80211softmac_fragment_lost(struct net_device *dev,
				    u16 wl_seq)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&mac->lock, flags);
	ieee80211softmac_add_txrates_badness(mac, 1000);
	//TODO

	spin_unlock_irqrestore(&mac->lock, flags);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_fragment_lost);

static int rate_cmp(const void *a_, const void *b_) {
	u8 *a, *b;
	a = (u8*)a_;
	b = (u8*)b_;
	return ((*a & ~IEEE80211_BASIC_RATE_MASK) - (*b & ~IEEE80211_BASIC_RATE_MASK));
}

/* Allocate a softmac network struct and fill it from a network */
struct ieee80211softmac_network *
ieee80211softmac_create_network(struct ieee80211softmac_device *mac,
	struct ieee80211_network *net)
{
	struct ieee80211softmac_network *softnet;
	softnet = kzalloc(sizeof(struct ieee80211softmac_network), GFP_ATOMIC);
	if(softnet == NULL)
		return NULL;
	memcpy(softnet->bssid, net->bssid, ETH_ALEN);
	softnet->channel = net->channel;
	softnet->essid.len = net->ssid_len;
	memcpy(softnet->essid.data, net->ssid, softnet->essid.len);

	/* copy rates over */
	softnet->supported_rates.count = net->rates_len;
	memcpy(&softnet->supported_rates.rates[0], net->rates, net->rates_len);
	memcpy(&softnet->supported_rates.rates[softnet->supported_rates.count], net->rates_ex, net->rates_ex_len);
	softnet->supported_rates.count += net->rates_ex_len;
	sort(softnet->supported_rates.rates, softnet->supported_rates.count, sizeof(softnet->supported_rates.rates[0]), rate_cmp, NULL);

	/* we save the ERP value because it is needed at association time, and
	 * many AP's do not include an ERP IE in the association response. */
	softnet->erp_value = net->erp_value;

	softnet->capabilities = net->capability;
	return softnet;
}


/* Add a network to the list, while locked */
void
ieee80211softmac_add_network_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *add_net)
{
	struct list_head *list_ptr;
	struct ieee80211softmac_network *softmac_net = NULL;

	list_for_each(list_ptr, &mac->network_list) {
		softmac_net = list_entry(list_ptr, struct ieee80211softmac_network, list);
		if(!memcmp(softmac_net->bssid, add_net->bssid, ETH_ALEN))
			break;
		else
			softmac_net = NULL;
	}
	if(softmac_net == NULL)
		list_add(&(add_net->list), &mac->network_list);
}

/* Add a network to the list, with locking */
void
ieee80211softmac_add_network(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *add_net)
{
	unsigned long flags;
	spin_lock_irqsave(&mac->lock, flags);
	ieee80211softmac_add_network_locked(mac, add_net);
	spin_unlock_irqrestore(&mac->lock, flags);
}


/* Delete a network from the list, while locked*/
void
ieee80211softmac_del_network_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *del_net)
{
	list_del(&(del_net->list));
}

/* Delete a network from the list with locking */
void
ieee80211softmac_del_network(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *del_net)
{
	unsigned long flags;
	spin_lock_irqsave(&mac->lock, flags);
	ieee80211softmac_del_network_locked(mac, del_net);
	spin_unlock_irqrestore(&mac->lock, flags);
}

/* Get a network from the list by MAC while locked */
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_bssid_locked(struct ieee80211softmac_device *mac,
	u8 *bssid)
{
	struct list_head *list_ptr;
	struct ieee80211softmac_network *softmac_net = NULL;
	list_for_each(list_ptr, &mac->network_list) {
		softmac_net = list_entry(list_ptr, struct ieee80211softmac_network, list);
		if(!memcmp(softmac_net->bssid, bssid, ETH_ALEN))
			break;
		else
			softmac_net = NULL;
	}
	return softmac_net;
}

/* Get a network from the list by BSSID with locking */
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_bssid(struct ieee80211softmac_device *mac,
	u8 *bssid)
{
	unsigned long flags;
	struct ieee80211softmac_network *softmac_net;

	spin_lock_irqsave(&mac->lock, flags);
	softmac_net = ieee80211softmac_get_network_by_bssid_locked(mac, bssid);
	spin_unlock_irqrestore(&mac->lock, flags);
	return softmac_net;
}

/* Get a network from the list by ESSID while locked */
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_essid_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_essid *essid)
{
	struct list_head *list_ptr;
	struct ieee80211softmac_network *softmac_net = NULL;

	list_for_each(list_ptr, &mac->network_list) {
		softmac_net = list_entry(list_ptr, struct ieee80211softmac_network, list);
		if (softmac_net->essid.len == essid->len &&
			!memcmp(softmac_net->essid.data, essid->data, essid->len))
			return softmac_net;
	}
	return NULL;
}

/* Get a network from the list by ESSID with locking */
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_essid(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_essid *essid)
{
	unsigned long flags;
	struct ieee80211softmac_network *softmac_net = NULL;

	spin_lock_irqsave(&mac->lock, flags);
	softmac_net = ieee80211softmac_get_network_by_essid_locked(mac, essid);
	spin_unlock_irqrestore(&mac->lock, flags);
	return softmac_net;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes Berg");
MODULE_AUTHOR("Joseph Jezak");
MODULE_AUTHOR("Larry Finger");
MODULE_AUTHOR("Danny van Dyk");
MODULE_AUTHOR("Michael Buesch");
MODULE_DESCRIPTION("802.11 software MAC");
