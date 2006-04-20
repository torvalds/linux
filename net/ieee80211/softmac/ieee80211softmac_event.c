/*
 * Event system
 * Also see comments in public header file and longer explanation below.
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
 * Each event has associated to it
 *  - an event type (see constants in public header)
 *  - an event context (see below)
 *  - the function to be called
 *  - a context (extra parameter to call the function with)
 *  - and the softmac struct
 *
 * The event context is private and can only be used from
 * within this module. Its meaning varies with the event
 * type:
 *  SCAN_FINISHED,
 *  DISASSOCIATED:	NULL
 *  ASSOCIATED,
 *  ASSOCIATE_FAILED,
 *  ASSOCIATE_TIMEOUT,
 *  AUTHENTICATED,
 *  AUTH_FAILED,
 *  AUTH_TIMEOUT:	a pointer to the network struct
 * ...
 * Code within this module can use the event context to be only
 * called when the event is true for that specific context
 * as per above table.
 * If the event context is NULL, then the notification is always called,
 * regardless of the event context. The event context is not passed to
 * the callback, it is assumed that the context suffices.
 *
 * You can also use the event context only by setting the event type
 * to -1 (private use only), in which case you'll be notified
 * whenever the event context matches.
 */

static char *event_descriptions[IEEE80211SOFTMAC_EVENT_LAST+1] = {
	NULL, /* scan finished */
	NULL, /* associated */
	"associating failed",
	"associating timed out",
	"authenticated",
	"authenticating failed",
	"authenticating timed out",
	"associating failed because no suitable network was found",
	NULL, /* disassociated */
};


static void
ieee80211softmac_notify_callback(void *d)
{
	struct ieee80211softmac_event event = *(struct ieee80211softmac_event*) d;
	kfree(d);
	
	event.fun(event.mac->dev, event.context);
}

int
ieee80211softmac_notify_internal(struct ieee80211softmac_device *mac,
	int event, void *event_context, notify_function_ptr fun, void *context, gfp_t gfp_mask)
{
	struct ieee80211softmac_event *eventptr;
	unsigned long flags;

	if (event < -1 || event > IEEE80211SOFTMAC_EVENT_LAST)
		return -ENOSYS;
	
	if (!fun)
		return -EINVAL;
	
	eventptr = kmalloc(sizeof(struct ieee80211softmac_event), gfp_mask);
	if (!eventptr)
		return -ENOMEM;
	
	eventptr->event_type = event;
	INIT_WORK(&eventptr->work, ieee80211softmac_notify_callback, eventptr);
	eventptr->fun = fun;
	eventptr->context = context;
	eventptr->mac = mac;
	eventptr->event_context = event_context;

	spin_lock_irqsave(&mac->lock, flags);
	list_add(&eventptr->list, &mac->events);
	spin_unlock_irqrestore(&mac->lock, flags);

	return 0;
}

int
ieee80211softmac_notify_gfp(struct net_device *dev,
	int event, notify_function_ptr fun, void *context, gfp_t gfp_mask)
{
	struct ieee80211softmac_device *mac = ieee80211_priv(dev);

	if (event < 0 || event > IEEE80211SOFTMAC_EVENT_LAST)
		return -ENOSYS;
	
	return ieee80211softmac_notify_internal(mac, event, NULL, fun, context, gfp_mask);
}
EXPORT_SYMBOL_GPL(ieee80211softmac_notify_gfp);

/* private -- calling all callbacks that were specified */
void
ieee80211softmac_call_events_locked(struct ieee80211softmac_device *mac, int event, void *event_ctx)
{
	struct ieee80211softmac_event *eventptr, *tmp;
	struct ieee80211softmac_network *network;
	
	if (event >= 0) {
		union iwreq_data wrqu;
		int we_event;
		char *msg = NULL;

		memset(&wrqu, '\0', sizeof (union iwreq_data));

		switch(event) {
		case IEEE80211SOFTMAC_EVENT_ASSOCIATED:
			network = (struct ieee80211softmac_network *)event_ctx;
			memcpy(wrqu.ap_addr.sa_data, &network->bssid[0], ETH_ALEN);
			/* fall through */
		case IEEE80211SOFTMAC_EVENT_DISASSOCIATED:
			wrqu.ap_addr.sa_family = ARPHRD_ETHER;
			we_event = SIOCGIWAP;
			break;
		case IEEE80211SOFTMAC_EVENT_SCAN_FINISHED:
			we_event = SIOCGIWSCAN;
			break;
		default:
			msg = event_descriptions[event];
			if (!msg)
				msg = "SOFTMAC EVENT BUG";
			wrqu.data.length = strlen(msg);
			we_event = IWEVCUSTOM;
			break;
		}
		wireless_send_event(mac->dev, we_event, &wrqu, msg);
	}

	if (!list_empty(&mac->events))
		list_for_each_entry_safe(eventptr, tmp, &mac->events, list) {
			if ((eventptr->event_type == event || eventptr->event_type == -1)
				&& (eventptr->event_context == NULL || eventptr->event_context == event_ctx)) {
				list_del(&eventptr->list);
				schedule_work(&eventptr->work);
			}
		}
}

void
ieee80211softmac_call_events(struct ieee80211softmac_device *mac, int event, void *event_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&mac->lock, flags);
	ieee80211softmac_call_events_locked(mac, event, event_ctx);

	spin_unlock_irqrestore(&mac->lock, flags);
}
