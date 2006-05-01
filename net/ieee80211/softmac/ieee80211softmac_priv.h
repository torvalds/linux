/*
 * Internal softmac API definitions.
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

#ifndef IEEE80211SOFTMAC_PRIV_H_
#define IEEE80211SOFTMAC_PRIV_H_

#include <net/ieee80211softmac.h>
#include <net/ieee80211softmac_wx.h>
#include <linux/kernel.h>
#include <linux/stringify.h>


#define PFX				"SoftMAC: "

#ifdef assert
# undef assert
#endif
#ifdef CONFIG_IEEE80211_SOFTMAC_DEBUG
#define assert(expr) \
	do {										\
		if (unlikely(!(expr))) {						\
		printkl(KERN_ERR PFX "ASSERTION FAILED (%s) at: %s:%d:%s()\n", #expr,	\
			__FILE__, __LINE__, __FUNCTION__);				\
		}									\
	} while (0)
#else
#define assert(expr) do {} while (0)
#endif

/* rate limited printk(). */
#ifdef printkl
# undef printkl
#endif
#define printkl(f, x...)  do { if (printk_ratelimit()) printk(f ,##x); } while (0)
/* rate limited printk() for debugging */
#ifdef dprintkl
# undef dprintkl
#endif
#ifdef CONFIG_IEEE80211_SOFTMAC_DEBUG
# define dprintkl		printkl
#else
# define dprintkl(f, x...)	do { /* nothing */ } while (0)
#endif

/* debugging printk() */
#ifdef dprintk
# undef dprintk
#endif
#ifdef CONFIG_IEEE80211_SOFTMAC_DEBUG
# define dprintk(f, x...)  do { printk(f ,##x); } while (0)
#else
# define dprintk(f, x...)  do { /* nothing */ } while (0)
#endif

/* private definitions and prototypes */

/*** prototypes from _scan.c */
void ieee80211softmac_scan(void *sm);
/* for internal use if scanning is needed */
int ieee80211softmac_start_scan(struct ieee80211softmac_device *mac);
void ieee80211softmac_stop_scan(struct ieee80211softmac_device *mac);
void ieee80211softmac_wait_for_scan(struct ieee80211softmac_device *mac);

/* for use by _module.c to assign to the callbacks */
int ieee80211softmac_start_scan_implementation(struct net_device *dev);
void ieee80211softmac_stop_scan_implementation(struct net_device *dev);
void ieee80211softmac_wait_for_scan_implementation(struct net_device *dev);

/*** Network prototypes from _module.c */
struct ieee80211softmac_network * ieee80211softmac_create_network(
	struct ieee80211softmac_device *mac, struct ieee80211_network *net);
void ieee80211softmac_add_network_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net);
void ieee80211softmac_add_network(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net);
void ieee80211softmac_del_network_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net);
void ieee80211softmac_del_network(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_network *net);
struct ieee80211softmac_network * ieee80211softmac_get_network_by_bssid_locked(
	struct ieee80211softmac_device *mac, u8 *ea);
struct ieee80211softmac_network * ieee80211softmac_get_network_by_bssid(
	struct ieee80211softmac_device *mac, u8 *ea);
struct ieee80211softmac_network * ieee80211softmac_get_network_by_ssid_locked(
	struct ieee80211softmac_device *mac, u8 *ssid, u8 ssid_len);
struct ieee80211softmac_network * ieee80211softmac_get_network_by_ssid(
	struct ieee80211softmac_device *mac, u8 *ssid, u8 ssid_len);
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_essid_locked(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_essid *essid);
struct ieee80211softmac_network *
ieee80211softmac_get_network_by_essid(struct ieee80211softmac_device *mac,
	struct ieee80211softmac_essid *essid);

/* Rates related */
u8 ieee80211softmac_lower_rate_delta(struct ieee80211softmac_device *mac, u8 rate, int delta);
static inline u8 lower_rate(struct ieee80211softmac_device *mac, u8 rate) {
	return ieee80211softmac_lower_rate_delta(mac, rate, 1);
}

static inline u8 get_fallback_rate(struct ieee80211softmac_device *mac, u8 rate)
{
	return ieee80211softmac_lower_rate_delta(mac, rate, 2);
}
                

/*** prototypes from _io.c */
int ieee80211softmac_send_mgt_frame(struct ieee80211softmac_device *mac,
	void* ptrarg, u32 type, u32 arg);

/*** prototypes from _auth.c */
/* do these have to go into the public header? */
int ieee80211softmac_auth_req(struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net);
int ieee80211softmac_deauth_req(struct ieee80211softmac_device *mac, struct ieee80211softmac_network *net, int reason);

/* for use by _module.c to assign to the callbacks */
int ieee80211softmac_auth_resp(struct net_device *dev, struct ieee80211_auth *auth);
int ieee80211softmac_deauth_resp(struct net_device *dev, struct ieee80211_deauth *deauth);

/*** prototypes from _assoc.c */
void ieee80211softmac_assoc_work(void *d);
int ieee80211softmac_handle_assoc_response(struct net_device * dev,
					   struct ieee80211_assoc_response * resp,
					   struct ieee80211_network * network);
int ieee80211softmac_handle_disassoc(struct net_device * dev,
				     struct ieee80211_disassoc * disassoc);
int ieee80211softmac_handle_reassoc_req(struct net_device * dev,
				        struct ieee80211_reassoc_request * reassoc);
void ieee80211softmac_assoc_timeout(void *d);
void ieee80211softmac_send_disassoc_req(struct ieee80211softmac_device *mac, u16 reason);
void ieee80211softmac_disassoc(struct ieee80211softmac_device *mac);

/* some helper functions */
static inline int ieee80211softmac_scan_handlers_check_self(struct ieee80211softmac_device *sm)
{
	return (sm->start_scan == ieee80211softmac_start_scan_implementation) && 
		(sm->stop_scan == ieee80211softmac_stop_scan_implementation) && 
		(sm->wait_for_scan == ieee80211softmac_wait_for_scan_implementation);
}

static inline int ieee80211softmac_scan_sanity_check(struct ieee80211softmac_device *sm)
{
	return ((sm->start_scan != ieee80211softmac_start_scan_implementation) && 
		(sm->stop_scan != ieee80211softmac_stop_scan_implementation) && 
		(sm->wait_for_scan != ieee80211softmac_wait_for_scan_implementation)
		) || ieee80211softmac_scan_handlers_check_self(sm);
}

#define IEEE80211SOFTMAC_PROBE_DELAY		HZ/50
#define IEEE80211SOFTMAC_WORKQUEUE_NAME_LEN	(17 + IFNAMSIZ)

struct ieee80211softmac_network {
	struct list_head		list;	/* List */
	/* Network information copied from ieee80211_network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	struct ieee80211softmac_essid essid;

	struct ieee80211softmac_ratesinfo supported_rates;

	/* SoftMAC specific */
	u16 authenticating:1,			/* Status Flags */
	    authenticated:1,
	    auth_desynced_once:1;

	u16 capabilities;			/* Capabilities bitfield */
	u8 challenge_len;			/* Auth Challenge length */
	char *challenge;			/* Challenge Text */
};

/* structure used to keep track of networks we're auth'ing to */
struct ieee80211softmac_auth_queue_item {
	struct list_head		list;	/* List head */
	struct ieee80211softmac_network	*net;	/* Network to auth */
	struct ieee80211softmac_device	*mac;	/* SoftMAC device */
	u8 retry;				/* Retry limit */
	u8 state;				/* Auth State */
	struct work_struct		work;	/* Work queue */
};

/* scanning information */
struct ieee80211softmac_scaninfo {
	u8 current_channel_idx,
	   number_channels;
	struct ieee80211_channel *channels;
	u8 started:1,
	   stop:1;
	u8 skip_flags;
	struct completion finished;
	struct work_struct softmac_scan;
};

/* private event struct */
struct ieee80211softmac_event {
	struct list_head list;
	int event_type;
	void *event_context;
	struct work_struct work;
	notify_function_ptr fun;
	void *context;
	struct ieee80211softmac_device *mac;
};

void ieee80211softmac_call_events(struct ieee80211softmac_device *mac, int event, void *event_context);
void ieee80211softmac_call_events_locked(struct ieee80211softmac_device *mac, int event, void *event_context);
int ieee80211softmac_notify_internal(struct ieee80211softmac_device *mac,
	int event, void *event_context, notify_function_ptr fun, void *context, gfp_t gfp_mask);

#endif /* IEEE80211SOFTMAC_PRIV_H_ */
