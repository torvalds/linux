/*
 * This file contains the softmac's association logic.
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

/*
 * Overview
 *
 * Before you can associate, you have to authenticate.
 * 
 */

/* Sends out an association request to the desired AP */
static void
ieee80211softmac_assoc(struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net)
{
	unsigned long flags;

	/* Switch to correct channel for this network */
	mac->set_channel(mac->dev, net->channel);
	
	/* Send association request */
	ieee80211softmac_send_mgt_frame(mac, net, IEEE80211_STYPE_ASSOC_REQ, 0);
	
	dprintk(KERN_INFO PFX "sent association request!\n");

	spin_lock_irqsave(&mac->lock, flags);
	mac->associnfo.associated = 0; /* just to make sure */

	/* Set a timer for timeout */
	/* FIXME: make timeout configurable */
	if (likely(mac->running))
		schedule_delayed_work(&mac->associnfo.timeout, 5 * HZ);
	spin_unlock_irqrestore(&mac->lock, flags);
}

void
ieee80211softmac_assoc_timeout(struct work_struct *work)
{
	struct ieee80211softmac_device *mac =
		container_of(work, struct ieee80211softmac_device,
			     associnfo.timeout.work);
	struct ieee80211softmac_network *n;

	mutex_lock(&mac->associnfo.mutex);
	/* we might race against ieee80211softmac_handle_assoc_response,
	 * so make sure only one of us does something */
	if (!mac->associnfo.associating)
		goto out;
	mac->associnfo.associating = 0;
	mac->associnfo.bssvalid = 0;
	mac->associnfo.associated = 0;

	n = ieee80211softmac_get_network_by_bssid_locked(mac, mac->associnfo.bssid);

	dprintk(KERN_INFO PFX "assoc request timed out!\n");
	ieee80211softmac_call_events(mac, IEEE80211SOFTMAC_EVENT_ASSOCIATE_TIMEOUT, n);
out:
	mutex_unlock(&mac->associnfo.mutex);
}

void
ieee80211softmac_disassoc(struct ieee80211softmac_device *mac)
{
	unsigned long flags;

	spin_lock_irqsave(&mac->lock, flags);
	if (mac->associnfo.associating)
		cancel_delayed_work(&mac->associnfo.timeout);

	netif_carrier_off(mac->dev);

	mac->associnfo.associated = 0;
	mac->associnfo.bssvalid = 0;
	mac->associnfo.associating = 0;
	ieee80211softmac_init_bss(mac);
	ieee80211softmac_call_events_locked(mac, IEEE80211SOFTMAC_EVENT_DISASSOCIATED, NULL);
	spin_unlock_irqrestore(&mac->lock, flags);
}

/* Sends out a disassociation request to the desired AP */
void
ieee80211softmac_send_disassoc_req(struct ieee80211softmac_device *mac, u16 reason)
{
	struct ieee80211softmac_network *found;

	if (mac->associnfo.bssvalid && mac->associnfo.associated) {
		found = ieee80211softmac_get_network_by_bssid(mac, mac->associnfo.bssid);
		if (found)
			ieee80211softmac_send_mgt_frame(mac, found, IEEE80211_STYPE_DISASSOC, reason);
	}

	ieee80211softmac_disassoc(mac);
}

static inline int
we_support_all_basic_rates(struct ieee80211softmac_device *mac, u8 *from, u8 from_len)
{
	int idx;
	u8 rate;

	for (idx = 0; idx < (from_len); idx++) {
		rate = (from)[idx];
		if (!(rate & IEEE80211_BASIC_RATE_MASK))
			continue;
		rate &= ~IEEE80211_BASIC_RATE_MASK;
		if (!ieee80211softmac_ratesinfo_rate_supported(&mac->ratesinfo, rate))
			return 0;
	}
	return 1;
}

static int
network_matches_request(struct ieee80211softmac_device *mac, struct ieee80211_network *net)
{
	/* we cannot associate to networks whose name we don't know */
	if (ieee80211_is_empty_essid(net->ssid, net->ssid_len))
		return 0;
	/* do not associate to a network whose BSSBasicRateSet we cannot support */
	if (!we_support_all_basic_rates(mac, net->rates, net->rates_len))
		return 0;
	/* do we really need to check the ex rates? */
	if (!we_support_all_basic_rates(mac, net->rates_ex, net->rates_ex_len))
		return 0;

	/* assume that users know what they're doing ...
	 * (note we don't let them select a net we're incompatible with) */
	if (mac->associnfo.bssfixed) {
		return !memcmp(mac->associnfo.bssid, net->bssid, ETH_ALEN);
	}

	/* if 'ANY' network requested, take any that doesn't have privacy enabled */
	if (mac->associnfo.req_essid.len == 0 
	    && !(net->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	if (net->ssid_len != mac->associnfo.req_essid.len)
		return 0;
	if (!memcmp(net->ssid, mac->associnfo.req_essid.data, mac->associnfo.req_essid.len))
		return 1;
	return 0;
}

static void
ieee80211softmac_assoc_notify_scan(struct net_device *dev, int event_type, void *context)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	ieee80211softmac_assoc_work((void*)mac);
}

static void
ieee80211softmac_assoc_notify_auth(struct net_device *dev, int event_type, void *context)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);

	switch (event_type) {
	case IEEE80211SOFTMAC_EVENT_AUTHENTICATED:
		ieee80211softmac_assoc_work((void*)mac);
		break;
	case IEEE80211SOFTMAC_EVENT_AUTH_FAILED:
	case IEEE80211SOFTMAC_EVENT_AUTH_TIMEOUT:
		ieee80211softmac_disassoc(mac);
		break;
	}
}

/* This function is called to handle userspace requests (asynchronously) */
void
ieee80211softmac_assoc_work(struct work_struct *work)
{
	struct ieee80211softmac_device *mac =
		container_of(work, struct ieee80211softmac_device,
			     associnfo.work.work);
	struct ieee80211softmac_network *found = NULL;
	struct ieee80211_network *net = NULL, *best = NULL;
	int bssvalid;
	unsigned long flags;

	mutex_lock(&mac->associnfo.mutex);

	if (!mac->associnfo.associating)
		goto out;

	/* ieee80211_disassoc might clear this */
	bssvalid = mac->associnfo.bssvalid;

	/* meh */
	if (mac->associnfo.associated)
		ieee80211softmac_send_disassoc_req(mac, WLAN_REASON_DISASSOC_STA_HAS_LEFT);

	/* try to find the requested network in our list, if we found one already */
	if (bssvalid || mac->associnfo.bssfixed)
		found = ieee80211softmac_get_network_by_bssid(mac, mac->associnfo.bssid);	
	
	/* Search the ieee80211 networks for this network if we didn't find it by bssid,
	 * but only if we've scanned at least once (to get a better list of networks to
	 * select from). If we have not scanned before, the !found logic below will be
	 * invoked and will scan. */
	if (!found && (mac->associnfo.scan_retry < IEEE80211SOFTMAC_ASSOC_SCAN_RETRY_LIMIT))
	{
		s8 rssi = -128;	/* if I don't initialise, gcc emits an invalid warning
				   because it cannot follow the best pointer logic. */
		spin_lock_irqsave(&mac->ieee->lock, flags);
		list_for_each_entry(net, &mac->ieee->network_list, list) {
			/* we're supposed to find the network with
			 * the best signal here, as we're asked to join
			 * any network with a specific ESSID, and many
			 * different ones could have that.
			 *
			 * I'll for now just go with the reported rssi.
			 *
			 * We also should take into account the rateset
			 * here to find the best BSSID to try.
			 */
			if (network_matches_request(mac, net)) {
				if (!best) {
					best = net;
					rssi = best->stats.rssi;
					continue;
				}
				/* we already had a matching network, so
				 * compare their properties to get the
				 * better of the two ... (see above)
				 */
				if (rssi < net->stats.rssi) {
					best = net;
					rssi = best->stats.rssi;
				}
			}
		}
		/* if we unlock here, we might get interrupted and the `best'
		 * pointer could go stale */
		if (best) {
			found = ieee80211softmac_create_network(mac, best);
			/* if found is still NULL, then we got -ENOMEM somewhere */
			if (found)
				ieee80211softmac_add_network(mac, found);
		}
		spin_unlock_irqrestore(&mac->ieee->lock, flags);
	}

	if (!found) {
		if (mac->associnfo.scan_retry > 0) {
			mac->associnfo.scan_retry--;

			/* We know of no such network. Let's scan. 
			 * NB: this also happens if we had no memory to copy the network info...
			 * Maybe we can hope to have more memory after scanning finishes ;)
			 */
			dprintk(KERN_INFO PFX "Associate: Scanning for networks first.\n");
			ieee80211softmac_notify(mac->dev, IEEE80211SOFTMAC_EVENT_SCAN_FINISHED, ieee80211softmac_assoc_notify_scan, NULL);
			if (ieee80211softmac_start_scan(mac))
				dprintk(KERN_INFO PFX "Associate: failed to initiate scan. Is device up?\n");
			goto out;
		} else {
			mac->associnfo.associating = 0;
			mac->associnfo.associated = 0;

			dprintk(KERN_INFO PFX "Unable to find matching network after scan!\n");
			/* reset the retry counter for the next user request since we
			 * break out and don't reschedule ourselves after this point. */
			mac->associnfo.scan_retry = IEEE80211SOFTMAC_ASSOC_SCAN_RETRY_LIMIT;
			ieee80211softmac_call_events(mac, IEEE80211SOFTMAC_EVENT_ASSOCIATE_NET_NOT_FOUND, NULL);
			goto out;
		}
	}

	/* reset the retry counter for the next user request since we
	 * now found a net and will try to associate to it, but not
	 * schedule this function again. */
	mac->associnfo.scan_retry = IEEE80211SOFTMAC_ASSOC_SCAN_RETRY_LIMIT;
	mac->associnfo.bssvalid = 1;
	memcpy(mac->associnfo.bssid, found->bssid, ETH_ALEN);
	/* copy the ESSID for displaying it */
	mac->associnfo.associate_essid.len = found->essid.len;
	memcpy(mac->associnfo.associate_essid.data, found->essid.data, IW_ESSID_MAX_SIZE + 1);

	/* we found a network! authenticate (if necessary) and associate to it. */
	if (found->authenticating) {
		dprintk(KERN_INFO PFX "Already requested authentication, waiting...\n");
		if(!mac->associnfo.assoc_wait) {
			mac->associnfo.assoc_wait = 1;
			ieee80211softmac_notify_internal(mac, IEEE80211SOFTMAC_EVENT_ANY, found, ieee80211softmac_assoc_notify_auth, NULL, GFP_KERNEL);
		}
		goto out;
	}
	if (!found->authenticated && !found->authenticating) {
		/* This relies on the fact that _auth_req only queues the work,
		 * otherwise adding the notification would be racy. */
		if (!ieee80211softmac_auth_req(mac, found)) {
			if(!mac->associnfo.assoc_wait) {
				dprintk(KERN_INFO PFX "Cannot associate without being authenticated, requested authentication\n");
				mac->associnfo.assoc_wait = 1;
				ieee80211softmac_notify_internal(mac, IEEE80211SOFTMAC_EVENT_ANY, found, ieee80211softmac_assoc_notify_auth, NULL, GFP_KERNEL);
			}
		} else {
			printkl(KERN_WARNING PFX "Not authenticated, but requesting authentication failed. Giving up to associate\n");
			mac->associnfo.assoc_wait = 0;
			ieee80211softmac_call_events(mac, IEEE80211SOFTMAC_EVENT_ASSOCIATE_FAILED, found);
		}
		goto out;
	}
	/* finally! now we can start associating */
	mac->associnfo.assoc_wait = 0;
	ieee80211softmac_assoc(mac, found);

out:
	mutex_unlock(&mac->associnfo.mutex);
}

/* call this to do whatever is necessary when we're associated */
static void
ieee80211softmac_associated(struct ieee80211softmac_device *mac,
	struct ieee80211_assoc_response * resp,
	struct ieee80211softmac_network *net)
{
	u16 cap = le16_to_cpu(resp->capability);
	u8 erp_value = net->erp_value;

	mac->associnfo.associating = 0;
	mac->bssinfo.supported_rates = net->supported_rates;
	ieee80211softmac_recalc_txrates(mac);

	mac->associnfo.associated = 1;

	mac->associnfo.short_preamble_available =
		(cap & WLAN_CAPABILITY_SHORT_PREAMBLE) != 0;
	ieee80211softmac_process_erp(mac, erp_value);

	if (mac->set_bssid_filter)
		mac->set_bssid_filter(mac->dev, net->bssid);
	memcpy(mac->ieee->bssid, net->bssid, ETH_ALEN);
	netif_carrier_on(mac->dev);
	
	mac->association_id = le16_to_cpup(&resp->aid);
}

/* received frame handling functions */
int
ieee80211softmac_handle_assoc_response(struct net_device * dev,
				       struct ieee80211_assoc_response * resp,
				       struct ieee80211_network * _ieee80211_network)
{
	/* NOTE: the network parameter has to be mostly ignored by
	 *       this code because it is the ieee80211's pointer
	 *       to the struct, not ours (we made a copy)
	 */
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	u16 status = le16_to_cpup(&resp->status);
	struct ieee80211softmac_network *network = NULL;
	unsigned long flags;

	if (unlikely(!mac->running))
		return -ENODEV;
	
	spin_lock_irqsave(&mac->lock, flags);

	if (!mac->associnfo.associating) {
		/* we race against the timeout function, so make sure
		 * only one of us can do work */
		spin_unlock_irqrestore(&mac->lock, flags);
		return 0;
	}
	network = ieee80211softmac_get_network_by_bssid_locked(mac, resp->header.addr3);

	/* someone sending us things without us knowing him? Ignore. */
	if (!network) {
		dprintk(KERN_INFO PFX "Received unrequested assocation response from " MAC_FMT "\n", MAC_ARG(resp->header.addr3));
		spin_unlock_irqrestore(&mac->lock, flags);
		return 0;
	}

	/* now that we know it was for us, we can cancel the timeout */
	cancel_delayed_work(&mac->associnfo.timeout);

	/* if the association response included an ERP IE, update our saved
	 * copy */
	if (_ieee80211_network->flags & NETWORK_HAS_ERP_VALUE)
		network->erp_value = _ieee80211_network->erp_value;

	switch (status) {
		case 0:
			dprintk(KERN_INFO PFX "associated!\n");
			ieee80211softmac_associated(mac, resp, network);
			ieee80211softmac_call_events_locked(mac, IEEE80211SOFTMAC_EVENT_ASSOCIATED, network);
			break;
		case WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH:
			if (!network->auth_desynced_once) {
				/* there seem to be a few rare cases where our view of
				 * the world is obscured, or buggy APs that don't DEAUTH
				 * us properly. So we handle that, but allow it only once.
				 */
				printkl(KERN_INFO PFX "We were not authenticated during association, retrying...\n");
				network->authenticated = 0;
				/* we don't want to do this more than once ... */
				network->auth_desynced_once = 1;
				schedule_delayed_work(&mac->associnfo.work, 0);
				break;
			}
		default:
			dprintk(KERN_INFO PFX "associating failed (reason: 0x%x)!\n", status);
			mac->associnfo.associating = 0;
			mac->associnfo.bssvalid = 0;
			mac->associnfo.associated = 0;
			ieee80211softmac_call_events_locked(mac, IEEE80211SOFTMAC_EVENT_ASSOCIATE_FAILED, network);
	}
	
	spin_unlock_irqrestore(&mac->lock, flags);
	return 0;
}

void
ieee80211softmac_try_reassoc(struct ieee80211softmac_device *mac)
{
	unsigned long flags;

	spin_lock_irqsave(&mac->lock, flags);
	mac->associnfo.associating = 1;
	schedule_delayed_work(&mac->associnfo.work, 0);
	spin_unlock_irqrestore(&mac->lock, flags);
}

int
ieee80211softmac_handle_disassoc(struct net_device * dev,
				 struct ieee80211_disassoc *disassoc)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);

	if (unlikely(!mac->running))
		return -ENODEV;

	if (memcmp(disassoc->header.addr2, mac->associnfo.bssid, ETH_ALEN))
		return 0;

	if (memcmp(disassoc->header.addr1, mac->dev->dev_addr, ETH_ALEN))
		return 0;

	dprintk(KERN_INFO PFX "got disassoc frame\n");
	ieee80211softmac_disassoc(mac);

	ieee80211softmac_try_reassoc(mac);

	return 0;
}

int
ieee80211softmac_handle_reassoc_req(struct net_device * dev,
				    struct ieee80211_reassoc_request * resp)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	struct ieee80211softmac_network *network;

	if (unlikely(!mac->running))
		return -ENODEV;

	network = ieee80211softmac_get_network_by_bssid(mac, resp->header.addr3);
	if (!network) {
		dprintkl(KERN_INFO PFX "reassoc request from unknown network\n");
		return 0;
	}
	schedule_delayed_work(&mac->associnfo.work, 0);

	return 0;
}
