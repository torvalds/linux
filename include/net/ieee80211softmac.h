/*
 * ieee80211softmac.h - public interface to the softmac
 *
 * Copyright (c) 2005 Johannes Berg <johannes@sipsolutions.net>
 *                    Joseph Jezak <josejx@gentoo.org>
 *                    Larry Finger <Larry.Finger@lwfinger.net>
 *                    Danny van Dyk <kugelfang@gentoo.org>
 *                    Michael Buesch <mbuesch@freenet.de>
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

#ifndef IEEE80211SOFTMAC_H_
#define IEEE80211SOFTMAC_H_

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <net/ieee80211.h>

/* Once the API is considered more or less stable,
 * this should be incremented on API incompatible changes.
 */
#define IEEE80211SOFTMAC_API	0

#define IEEE80211SOFTMAC_MAX_RATES_LEN		8
#define IEEE80211SOFTMAC_MAX_EX_RATES_LEN	255

struct ieee80211softmac_ratesinfo {
	u8 count;
	u8 rates[IEEE80211SOFTMAC_MAX_RATES_LEN + IEEE80211SOFTMAC_MAX_EX_RATES_LEN];
};

/* internal structures */
struct ieee80211softmac_network;
struct ieee80211softmac_scaninfo;

struct ieee80211softmac_essid {
	u8 len;
	char data[IW_ESSID_MAX_SIZE+1];
};

struct ieee80211softmac_wpa {
	char *IE;
	int IElen;
	int IEbuflen;
};

/*
 * Information about association
 *
 * Do we need a lock for this?
 * We only ever use this structure inlined
 * into our global struct. I've used its lock,
 * but maybe we need a local one here?
 */
struct ieee80211softmac_assoc_info {
	/*
	 * This is the requested ESSID. It is written
	 * only by the WX handlers.
	 *
	 */
	struct ieee80211softmac_essid req_essid;
	/*
	 * the ESSID of the network we're currently
	 * associated (or trying) to. This is
	 * updated to the network's actual ESSID
	 * even if the requested ESSID was 'ANY'
	 */
	struct ieee80211softmac_essid associate_essid;
	
	/* BSSID we're trying to associate to */
	char bssid[ETH_ALEN];

	/* Rates supported by the network */
	struct ieee80211softmac_ratesinfo supported_rates;
	
	/* some flags.
	 * static_essid is valid if the essid is constant,
	 * this is for use by the wx handlers only.
	 *
	 * associating is true, if the network has been
	 * auth'ed on and we are in the process of associating.
	 *
	 * bssvalid is true if we found a matching network
	 * and saved it's BSSID into the bssid above.
	 *
	 * bssfixed is used for SIOCSIWAP.
	 */
	u8 static_essid:1,
	   associating:1,
	   bssvalid:1,
	   bssfixed:1;

	/* Scan retries remaining */
	int scan_retry;

	struct work_struct work;
	struct work_struct timeout;
};

enum {
	IEEE80211SOFTMAC_AUTH_OPEN_REQUEST	= 1,
	IEEE80211SOFTMAC_AUTH_OPEN_RESPONSE	= 2,
};

enum {
	IEEE80211SOFTMAC_AUTH_SHARED_REQUEST	= 1,
	IEEE80211SOFTMAC_AUTH_SHARED_CHALLENGE = 2,
	IEEE80211SOFTMAC_AUTH_SHARED_RESPONSE	= 3,
	IEEE80211SOFTMAC_AUTH_SHARED_PASS	= 4,
};

/* We should make these tunable
 * AUTH_TIMEOUT seems really long, but that's what it is in BSD */
#define IEEE80211SOFTMAC_AUTH_TIMEOUT		(12 * HZ)
#define IEEE80211SOFTMAC_AUTH_RETRY_LIMIT	5
#define IEEE80211SOFTMAC_ASSOC_SCAN_RETRY_LIMIT	3

struct ieee80211softmac_txrates {
	/* The Bit-Rate to be used for multicast frames. */
	u8 mcast_rate;

	/* The Bit-Rate to be used for multicast management frames. */
	u8 mgt_mcast_rate;

	/* The Bit-Rate to be used for any other (normal) data packet. */
	u8 default_rate;
	/* The Bit-Rate to be used for default fallback
	 * (If the device supports fallback and hardware-retry)
	 */
	u8 default_fallback;

	/* This is the rate that the user asked for */
	u8 user_rate;
};

/* Bits for txrates_change callback. */
#define IEEE80211SOFTMAC_TXRATECHG_DEFAULT		(1 << 0) /* default_rate */
#define IEEE80211SOFTMAC_TXRATECHG_DEFAULT_FBACK	(1 << 1) /* default_fallback */
#define IEEE80211SOFTMAC_TXRATECHG_MCAST		(1 << 2) /* mcast_rate */
#define IEEE80211SOFTMAC_TXRATECHG_MGT_MCAST		(1 << 3) /* mgt_mcast_rate */

struct ieee80211softmac_device {
	/* 802.11 structure for data stuff */
	struct ieee80211_device *ieee;
	struct net_device *dev;

	/* only valid if associated, then holds the Association ID */
	u16 association_id;
	
	/* the following methods are callbacks that the driver
	 * using this framework has to assign
	 */

	/* always assign these */
	void (*set_bssid_filter)(struct net_device *dev, const u8 *bssid);
	void (*set_channel)(struct net_device *dev, u8 channel);

	/* assign if you need it, informational only */
	void (*link_change)(struct net_device *dev);

	/* If the hardware can do scanning, assign _all_ three of these callbacks.
	 * When the scan finishes, call ieee80211softmac_scan_finished().
	 */
	
	/* when called, start_scan is guaranteed to not be called again
	 * until you call ieee80211softmac_scan_finished.
	 * Return 0 if scanning could start, error otherwise.
	 * SOFTMAC AUTHORS: don't call this, use ieee80211softmac_start_scan */
	int (*start_scan)(struct net_device *dev);
	/* this should block until after ieee80211softmac_scan_finished was called
	 * SOFTMAC AUTHORS: don't call this, use ieee80211softmac_wait_for_scan */
	void (*wait_for_scan)(struct net_device *dev);
	/* stop_scan aborts a scan, but is asynchronous.
	 * if you want to wait for it too, use wait_for_scan
	 * SOFTMAC AUTHORS: don't call this, use ieee80211softmac_stop_scan */
	void (*stop_scan)(struct net_device *dev);

	/* we'll need something about beacons here too, for AP or ad-hoc modes */

	/* Transmission rates to be used by the driver.
	 * The SoftMAC figures out the best possible rates.
	 * The driver just needs to read them.
	 */
	struct ieee80211softmac_txrates txrates;
	/* If the driver needs to do stuff on TX rate changes, assign this callback. */
	void (*txrates_change)(struct net_device *dev,
			       u32 changes, /* see IEEE80211SOFTMAC_TXRATECHG flags */
			       const struct ieee80211softmac_txrates *rates_before_change);

	/* private stuff follows */
	/* this lock protects this structure */
	spinlock_t lock;
	
	/* couple of flags */
	u8 scanning:1, /* protects scanning from being done multiple times at once */
	   associated:1,
	   running:1;
	
	struct ieee80211softmac_scaninfo *scaninfo;
	struct ieee80211softmac_assoc_info associnfo;

	struct list_head auth_queue;
	struct list_head events;

 	struct ieee80211softmac_ratesinfo ratesinfo;
	int txrate_badness;
	
	/* WPA stuff */
	struct ieee80211softmac_wpa wpa;

	/* we need to keep a list of network structs we copied */
	struct list_head network_list;
	
	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_ieee80211 */
	u8 priv[0];
};

extern void ieee80211softmac_scan_finished(struct ieee80211softmac_device *sm);

static inline void * ieee80211softmac_priv(struct net_device *dev)
{
	return ((struct ieee80211softmac_device *)ieee80211_priv(dev))->priv;
}

extern struct net_device * alloc_ieee80211softmac(int sizeof_priv);
extern void free_ieee80211softmac(struct net_device *dev);

/* Call this function if you detect a lost TX fragment.
 * (If the device indicates failure of ACK RX, for example.)
 * It is wise to call this function if you are able to detect lost packets,
 * because it contributes to the TX Rates auto adjustment.
 */
extern void ieee80211softmac_fragment_lost(struct net_device *dev,
					   u16 wireless_sequence_number);
/* Call this function before _start to tell the softmac what rates
 * the hw supports. The rates parameter is copied, so you can
 * free it right after calling this function. 
 * Note that the rates need to be sorted. */
extern void ieee80211softmac_set_rates(struct net_device *dev, u8 count, u8 *rates);

/* Helper function which advises you the rate at which a frame should be
 * transmitted at. */
static inline u8 ieee80211softmac_suggest_txrate(struct ieee80211softmac_device *mac,
						 int is_multicast,
						 int is_mgt)
{
	struct ieee80211softmac_txrates *txrates = &mac->txrates;

	if (!mac->associated)
		return txrates->mgt_mcast_rate;

	/* We are associated, sending unicast frame */
	if (!is_multicast)
		return txrates->default_rate;

	/* We are associated, sending multicast frame */
	if (is_mgt)
		return txrates->mgt_mcast_rate;
	else
		return txrates->mcast_rate;
}

/* Start the SoftMAC. Call this after you initialized the device
 * and it is ready to run.
 */
extern void ieee80211softmac_start(struct net_device *dev);
/* Stop the SoftMAC. Call this before you shutdown the device. */
extern void ieee80211softmac_stop(struct net_device *dev);

/*
 * Event system
 */

/* valid event types */
#define IEEE80211SOFTMAC_EVENT_ANY			-1 /*private use only*/
#define IEEE80211SOFTMAC_EVENT_SCAN_FINISHED		0
#define IEEE80211SOFTMAC_EVENT_ASSOCIATED		1
#define IEEE80211SOFTMAC_EVENT_ASSOCIATE_FAILED		2
#define IEEE80211SOFTMAC_EVENT_ASSOCIATE_TIMEOUT	3
#define IEEE80211SOFTMAC_EVENT_AUTHENTICATED		4
#define IEEE80211SOFTMAC_EVENT_AUTH_FAILED		5
#define IEEE80211SOFTMAC_EVENT_AUTH_TIMEOUT		6
#define IEEE80211SOFTMAC_EVENT_ASSOCIATE_NET_NOT_FOUND	7
#define IEEE80211SOFTMAC_EVENT_DISASSOCIATED		8
/* keep this updated! */
#define IEEE80211SOFTMAC_EVENT_LAST			8
/*
 * If you want to be notified of certain events, you can call
 * ieee80211softmac_notify[_atomic] with
 * 	- event set to one of the constants below
 * 	- fun set to a function pointer of the appropriate type
 *	- context set to the context data you want passed
 * The return value is 0, or an error.
 */
typedef void (*notify_function_ptr)(struct net_device *dev, void *context);

#define ieee80211softmac_notify(dev, event, fun, context) ieee80211softmac_notify_gfp(dev, event, fun, context, GFP_KERNEL);
#define ieee80211softmac_notify_atomic(dev, event, fun, context) ieee80211softmac_notify_gfp(dev, event, fun, context, GFP_ATOMIC);

extern int ieee80211softmac_notify_gfp(struct net_device *dev,
	int event, notify_function_ptr fun, void *context, gfp_t gfp_mask);

/* To clear pending work (for ifconfig down, etc.) */
extern void
ieee80211softmac_clear_pending_work(struct ieee80211softmac_device *sm);

#endif /* IEEE80211SOFTMAC_H_ */
