/*
 * This file contains the softmac's authentication logic.
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

static void ieee80211softmac_auth_queue(void *data);

/* Queues an auth request to the desired AP */
int
ieee80211softmac_auth_req(struct ieee80211softmac_device *mac, 
	struct ieee80211softmac_network *net)
{
	struct ieee80211softmac_auth_queue_item *auth;
	unsigned long flags;
	
	if (net->authenticating || net->authenticated)
		return 0;
	net->authenticating = 1;

	/* Add the network if it's not already added */
	ieee80211softmac_add_network(mac, net);

	dprintk(KERN_NOTICE PFX "Queueing Authentication Request to "MAC_FMT"\n", MAC_ARG(net->bssid));
	/* Queue the auth request */
	auth = (struct ieee80211softmac_auth_queue_item *)
		kmalloc(sizeof(struct ieee80211softmac_auth_queue_item), GFP_KERNEL);
	if(auth == NULL)
		return -ENOMEM;

	auth->net = net;
	auth->mac = mac;
	auth->retry = IEEE80211SOFTMAC_AUTH_RETRY_LIMIT;
	auth->state = IEEE80211SOFTMAC_AUTH_OPEN_REQUEST;
	INIT_WORK(&auth->work, &ieee80211softmac_auth_queue, (void *)auth);
	
	/* Lock (for list) */
	spin_lock_irqsave(&mac->lock, flags);

	/* add to list */
	list_add_tail(&auth->list, &mac->auth_queue);
	schedule_work(&auth->work);
	spin_unlock_irqrestore(&mac->lock, flags);
	
	return 0;
}


/* Sends an auth request to the desired AP and handles timeouts */
static void
ieee80211softmac_auth_queue(void *data)
{
	struct ieee80211softmac_device *mac;
	struct ieee80211softmac_auth_queue_item *auth;
	struct ieee80211softmac_network *net;
	unsigned long flags;

	auth = (struct ieee80211softmac_auth_queue_item *)data;
	net = auth->net;
	mac = auth->mac;

	if(auth->retry > 0) {
		/* Switch to correct channel for this network */
		mac->set_channel(mac->dev, net->channel);
		
		/* Lock and set flags */
		spin_lock_irqsave(&mac->lock, flags);
		if (unlikely(!mac->running)) {
			/* Prevent reschedule on workqueue flush */
			spin_unlock_irqrestore(&mac->lock, flags);
			return;
		}
		net->authenticated = 0;
		/* add a timeout call so we eventually give up waiting for an auth reply */
		schedule_delayed_work(&auth->work, IEEE80211SOFTMAC_AUTH_TIMEOUT);
		auth->retry--;
		spin_unlock_irqrestore(&mac->lock, flags);
		if (ieee80211softmac_send_mgt_frame(mac, auth->net, IEEE80211_STYPE_AUTH, auth->state))
			dprintk(KERN_NOTICE PFX "Sending Authentication Request to "MAC_FMT" failed (this shouldn't happen, wait for the timeout).\n", MAC_ARG(net->bssid));
		else
			dprintk(KERN_NOTICE PFX "Sent Authentication Request to "MAC_FMT".\n", MAC_ARG(net->bssid));
		return;
	}

	printkl(KERN_WARNING PFX "Authentication timed out with "MAC_FMT"\n", MAC_ARG(net->bssid));
	/* Remove this item from the queue */
	spin_lock_irqsave(&mac->lock, flags);
	net->authenticating = 0;
	ieee80211softmac_call_events_locked(mac, IEEE80211SOFTMAC_EVENT_AUTH_TIMEOUT, net);
	cancel_delayed_work(&auth->work); /* just to make sure... */
	list_del(&auth->list);
	spin_unlock_irqrestore(&mac->lock, flags);
	/* Free it */
	kfree(auth);
}

/* Sends a response to an auth challenge (for shared key auth). */
static void
ieee80211softmac_auth_challenge_response(void *_aq)
{
	struct ieee80211softmac_auth_queue_item *aq = _aq;

	/* Send our response */
	ieee80211softmac_send_mgt_frame(aq->mac, aq->net, IEEE80211_STYPE_AUTH, aq->state);
}

/* Handle the auth response from the AP
 * This should be registered with ieee80211 as handle_auth 
 */
int 
ieee80211softmac_auth_resp(struct net_device *dev, struct ieee80211_auth *auth)
{	

	struct list_head *list_ptr;
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	struct ieee80211softmac_auth_queue_item *aq = NULL;
	struct ieee80211softmac_network *net = NULL;
	unsigned long flags;
	u8 * data;
	
	if (unlikely(!mac->running))
		return -ENODEV;

	/* Find correct auth queue item */
	spin_lock_irqsave(&mac->lock, flags);
	list_for_each(list_ptr, &mac->auth_queue) {
		aq = list_entry(list_ptr, struct ieee80211softmac_auth_queue_item, list);
		net = aq->net;
		if (!memcmp(net->bssid, auth->header.addr2, ETH_ALEN))
			break;
		else
			aq = NULL;
	}
	spin_unlock_irqrestore(&mac->lock, flags);
	
	/* Make sure that we've got an auth queue item for this request */
	if(aq == NULL)
	{
		dprintkl(KERN_DEBUG PFX "Authentication response received from "MAC_FMT" but no queue item exists.\n", MAC_ARG(auth->header.addr2));
		/* Error #? */
		return -1;
	}			
	
	/* Check for out of order authentication */
	if(!net->authenticating)
	{
		dprintkl(KERN_DEBUG PFX "Authentication response received from "MAC_FMT" but did not request authentication.\n",MAC_ARG(auth->header.addr2));
		return -1;
	}

	/* Parse the auth packet */
	switch(auth->algorithm) {
	case WLAN_AUTH_OPEN:
		/* Check the status code of the response */

		switch(auth->status) {
		case WLAN_STATUS_SUCCESS:
			/* Update the status to Authenticated */
			spin_lock_irqsave(&mac->lock, flags);
		        net->authenticating = 0;
			net->authenticated = 1;
			spin_unlock_irqrestore(&mac->lock, flags);
			
			/* Send event */
			printkl(KERN_NOTICE PFX "Open Authentication completed with "MAC_FMT"\n", MAC_ARG(net->bssid));
			ieee80211softmac_call_events(mac, IEEE80211SOFTMAC_EVENT_AUTHENTICATED, net);
			break;
		default:
			/* Lock and reset flags */
			spin_lock_irqsave(&mac->lock, flags);
			net->authenticated = 0;
			net->authenticating = 0;
			spin_unlock_irqrestore(&mac->lock, flags);
			
			printkl(KERN_NOTICE PFX "Open Authentication with "MAC_FMT" failed, error code: %i\n", 
				MAC_ARG(net->bssid), le16_to_cpup(&auth->status));
			/* Count the error? */
			break;
		}
		goto free_aq;
		break;
	case WLAN_AUTH_SHARED_KEY:
		/* Figure out where we are in the process */
		switch(auth->transaction) {
		case IEEE80211SOFTMAC_AUTH_SHARED_CHALLENGE:
			/* Check to make sure we have a challenge IE */
			data = (u8 *)auth->info_element;
			if (*data++ != MFIE_TYPE_CHALLENGE) {
				printkl(KERN_NOTICE PFX "Shared Key Authentication failed due to a missing challenge.\n");
				break;	
			}
			/* Save the challenge */
			spin_lock_irqsave(&mac->lock, flags);
			net->challenge_len = *data++; 	
			if (net->challenge_len > WLAN_AUTH_CHALLENGE_LEN)
				net->challenge_len = WLAN_AUTH_CHALLENGE_LEN;
			kfree(net->challenge);
			net->challenge = kmemdup(data, net->challenge_len,
						 GFP_ATOMIC);
			if (net->challenge == NULL) {
				printkl(KERN_NOTICE PFX "Shared Key "
					"Authentication failed due to "
					"memory shortage.\n");
				spin_unlock_irqrestore(&mac->lock, flags);
				break;
			}
			aq->state = IEEE80211SOFTMAC_AUTH_SHARED_RESPONSE; 

			/* We reuse the work struct from the auth request here.
			 * It is safe to do so as each one is per-request, and
			 * at this point (dealing with authentication response)
			 * we have obviously already sent the initial auth
			 * request. */
			cancel_delayed_work(&aq->work);
			INIT_WORK(&aq->work, &ieee80211softmac_auth_challenge_response, (void *)aq);
			schedule_work(&aq->work);
			spin_unlock_irqrestore(&mac->lock, flags);
			return 0;
		case IEEE80211SOFTMAC_AUTH_SHARED_PASS:
			kfree(net->challenge);
			net->challenge = NULL;
			net->challenge_len = 0;
			/* Check the status code of the response */
			switch(auth->status) {
			case WLAN_STATUS_SUCCESS:
				/* Update the status to Authenticated */	
				spin_lock_irqsave(&mac->lock, flags);
				net->authenticating = 0;
				net->authenticated = 1;
				spin_unlock_irqrestore(&mac->lock, flags);
				printkl(KERN_NOTICE PFX "Shared Key Authentication completed with "MAC_FMT"\n", 
					MAC_ARG(net->bssid));
				ieee80211softmac_call_events(mac, IEEE80211SOFTMAC_EVENT_AUTHENTICATED, net);
				break;
			default:
				printkl(KERN_NOTICE PFX "Shared Key Authentication with "MAC_FMT" failed, error code: %i\n", 
					MAC_ARG(net->bssid), le16_to_cpup(&auth->status));
				/* Lock and reset flags */
				spin_lock_irqsave(&mac->lock, flags);
 				net->authenticating = 0;
 				net->authenticated = 0;
				spin_unlock_irqrestore(&mac->lock, flags);
				/* Count the error? */
				break;
			}
			goto free_aq;
			break;
		default:
			printkl(KERN_WARNING PFX "Unhandled Authentication Step: %i\n", auth->transaction);
			break;
		}
		goto free_aq;
		break;
	default:
		/* ERROR */	
		goto free_aq;
		break;
	}
	return 0;
free_aq:
	/* Cancel the timeout */
	spin_lock_irqsave(&mac->lock, flags);
	cancel_delayed_work(&aq->work);
	/* Remove this item from the queue */
	list_del(&aq->list);
	spin_unlock_irqrestore(&mac->lock, flags);

	/* Free it */
	kfree(aq);
	return 0;
}

/*
 * Handle deauthorization
 */
static void
ieee80211softmac_deauth_from_net(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net)
{
	struct ieee80211softmac_auth_queue_item *aq = NULL;
	struct list_head *list_ptr;
	unsigned long flags;

	/* deauthentication implies disassociation */
	ieee80211softmac_disassoc(mac);

	/* Lock and reset status flags */
	spin_lock_irqsave(&mac->lock, flags);
	net->authenticating = 0;
	net->authenticated = 0;
	
	/* Find correct auth queue item, if it exists */
	list_for_each(list_ptr, &mac->auth_queue) {
		aq = list_entry(list_ptr, struct ieee80211softmac_auth_queue_item, list);
		if (!memcmp(net->bssid, aq->net->bssid, ETH_ALEN))
			break;
		else
			aq = NULL;
	}
	
	/* Cancel pending work */
	if(aq != NULL)
		/* Not entirely safe?  What about running work? */
		cancel_delayed_work(&aq->work);

	/* Free our network ref */
	ieee80211softmac_del_network_locked(mac, net);
	if(net->challenge != NULL)
		kfree(net->challenge);
	kfree(net);
	
	/* can't transmit data right now... */
	netif_carrier_off(mac->dev);
	spin_unlock_irqrestore(&mac->lock, flags);
}

/* 
 * Sends a deauth request to the desired AP
 */
int 
ieee80211softmac_deauth_req(struct ieee80211softmac_device *mac, 
	struct ieee80211softmac_network *net, int reason)
{
	int ret;
	
	/* Make sure the network is authenticated */
	if (!net->authenticated)
	{
		dprintkl(KERN_DEBUG PFX "Can't send deauthentication packet, network is not authenticated.\n");
		/* Error okay? */
		return -EPERM;
	}
	
	/* Send the de-auth packet */
	if((ret = ieee80211softmac_send_mgt_frame(mac, net, IEEE80211_STYPE_DEAUTH, reason)))
		return ret;
	
	ieee80211softmac_deauth_from_net(mac, net);
	return 0;
}
 
/*
 * This should be registered with ieee80211 as handle_deauth
 */
int 
ieee80211softmac_deauth_resp(struct net_device *dev, struct ieee80211_deauth *deauth)
{
	
	struct ieee80211softmac_network *net = NULL;
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);
	
	if (unlikely(!mac->running))
		return -ENODEV;

	if (!deauth) {
		dprintk("deauth without deauth packet. eek!\n");
		return 0;
	}

	net = ieee80211softmac_get_network_by_bssid(mac, deauth->header.addr2);
	
	if (net == NULL) {
		dprintkl(KERN_DEBUG PFX "Received deauthentication packet from "MAC_FMT", but that network is unknown.\n",
			MAC_ARG(deauth->header.addr2));
		return 0;
	}

	/* Make sure the network is authenticated */
	if(!net->authenticated)
	{
		dprintkl(KERN_DEBUG PFX "Can't perform deauthentication, network is not authenticated.\n");
		/* Error okay? */
		return -EPERM;
	}

	ieee80211softmac_deauth_from_net(mac, net);

	/* let's try to re-associate */
	schedule_work(&mac->associnfo.work);
	return 0;
}
