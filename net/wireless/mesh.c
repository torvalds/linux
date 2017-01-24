#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "rdev-ops.h"

/* Default values, timeouts in ms */
#define MESH_TTL 		31
#define MESH_DEFAULT_ELEMENT_TTL 31
#define MESH_MAX_RETR	 	3
#define MESH_RET_T 		100
#define MESH_CONF_T 		100
#define MESH_HOLD_T 		100

#define MESH_PATH_TIMEOUT	5000
#define MESH_RANN_INTERVAL      5000
#define MESH_PATH_TO_ROOT_TIMEOUT      6000
#define MESH_ROOT_INTERVAL     5000
#define MESH_ROOT_CONFIRMATION_INTERVAL 2000
#define MESH_DEFAULT_PLINK_TIMEOUT	1800 /* timeout in seconds */

/*
 * Minimum interval between two consecutive PREQs originated by the same
 * interface
 */
#define MESH_PREQ_MIN_INT	10
#define MESH_PERR_MIN_INT	100
#define MESH_DIAM_TRAVERSAL_TIME 50

#define MESH_RSSI_THRESHOLD	0

/*
 * A path will be refreshed if it is used PATH_REFRESH_TIME milliseconds
 * before timing out.  This way it will remain ACTIVE and no data frames
 * will be unnecessarily held in the pending queue.
 */
#define MESH_PATH_REFRESH_TIME			1000
#define MESH_MIN_DISCOVERY_TIMEOUT (2 * MESH_DIAM_TRAVERSAL_TIME)

/* Default maximum number of established plinks per interface */
#define MESH_MAX_ESTAB_PLINKS	32

#define MESH_MAX_PREQ_RETRIES	4

#define MESH_SYNC_NEIGHBOR_OFFSET_MAX 50

#define MESH_DEFAULT_BEACON_INTERVAL	1000	/* in 1024 us units (=TUs) */
#define MESH_DEFAULT_DTIM_PERIOD	2
#define MESH_DEFAULT_AWAKE_WINDOW	10	/* in 1024 us units (=TUs) */

const struct mesh_config default_mesh_config = {
	.dot11MeshRetryTimeout = MESH_RET_T,
	.dot11MeshConfirmTimeout = MESH_CONF_T,
	.dot11MeshHoldingTimeout = MESH_HOLD_T,
	.dot11MeshMaxRetries = MESH_MAX_RETR,
	.dot11MeshTTL = MESH_TTL,
	.element_ttl = MESH_DEFAULT_ELEMENT_TTL,
	.auto_open_plinks = true,
	.dot11MeshMaxPeerLinks = MESH_MAX_ESTAB_PLINKS,
	.dot11MeshNbrOffsetMaxNeighbor = MESH_SYNC_NEIGHBOR_OFFSET_MAX,
	.dot11MeshHWMPactivePathTimeout = MESH_PATH_TIMEOUT,
	.dot11MeshHWMPpreqMinInterval = MESH_PREQ_MIN_INT,
	.dot11MeshHWMPperrMinInterval = MESH_PERR_MIN_INT,
	.dot11MeshHWMPnetDiameterTraversalTime = MESH_DIAM_TRAVERSAL_TIME,
	.dot11MeshHWMPmaxPREQretries = MESH_MAX_PREQ_RETRIES,
	.path_refresh_time = MESH_PATH_REFRESH_TIME,
	.min_discovery_timeout = MESH_MIN_DISCOVERY_TIMEOUT,
	.dot11MeshHWMPRannInterval = MESH_RANN_INTERVAL,
	.dot11MeshGateAnnouncementProtocol = false,
	.dot11MeshForwarding = true,
	.rssi_threshold = MESH_RSSI_THRESHOLD,
	.ht_opmode = IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED,
	.dot11MeshHWMPactivePathToRootTimeout = MESH_PATH_TO_ROOT_TIMEOUT,
	.dot11MeshHWMProotInterval = MESH_ROOT_INTERVAL,
	.dot11MeshHWMPconfirmationInterval = MESH_ROOT_CONFIRMATION_INTERVAL,
	.power_mode = NL80211_MESH_POWER_ACTIVE,
	.dot11MeshAwakeWindowDuration = MESH_DEFAULT_AWAKE_WINDOW,
	.plink_timeout = MESH_DEFAULT_PLINK_TIMEOUT,
};

const struct mesh_setup default_mesh_setup = {
	/* cfg80211_join_mesh() will pick a channel if needed */
	.sync_method = IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET,
	.path_sel_proto = IEEE80211_PATH_PROTOCOL_HWMP,
	.path_metric = IEEE80211_PATH_METRIC_AIRTIME,
	.auth_id = 0, /* open */
	.ie = NULL,
	.ie_len = 0,
	.is_secure = false,
	.user_mpm = false,
	.beacon_interval = MESH_DEFAULT_BEACON_INTERVAL,
	.dtim_period = MESH_DEFAULT_DTIM_PERIOD,
};

int __cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct mesh_setup *setup,
			 const struct mesh_config *conf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	BUILD_BUG_ON(IEEE80211_MAX_SSID_LEN != IEEE80211_MAX_MESH_ID_LEN);

	ASSERT_WDEV_LOCK(wdev);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_MESH_AUTH) &&
	      setup->is_secure)
		return -EOPNOTSUPP;

	if (wdev->mesh_id_len)
		return -EALREADY;

	if (!setup->mesh_id_len)
		return -EINVAL;

	if (!rdev->ops->join_mesh)
		return -EOPNOTSUPP;

	if (!setup->chandef.chan) {
		/* if no channel explicitly given, use preset channel */
		setup->chandef = wdev->preset_chandef;
	}

	if (!setup->chandef.chan) {
		/* if we don't have that either, use the first usable channel */
		enum nl80211_band band;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			struct ieee80211_supported_band *sband;
			struct ieee80211_channel *chan;
			int i;

			sband = rdev->wiphy.bands[band];
			if (!sband)
				continue;

			for (i = 0; i < sband->n_channels; i++) {
				chan = &sband->channels[i];
				if (chan->flags & (IEEE80211_CHAN_NO_IR |
						   IEEE80211_CHAN_DISABLED |
						   IEEE80211_CHAN_RADAR))
					continue;
				setup->chandef.chan = chan;
				break;
			}

			if (setup->chandef.chan)
				break;
		}

		/* no usable channel ... */
		if (!setup->chandef.chan)
			return -EINVAL;

		setup->chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		setup->chandef.center_freq1 = setup->chandef.chan->center_freq;
	}

	/*
	 * check if basic rates are available otherwise use mandatory rates as
	 * basic rates
	 */
	if (!setup->basic_rates) {
		enum nl80211_bss_scan_width scan_width;
		struct ieee80211_supported_band *sband =
				rdev->wiphy.bands[setup->chandef.chan->band];
		scan_width = cfg80211_chandef_to_scan_width(&setup->chandef);
		setup->basic_rates = ieee80211_mandatory_rates(sband,
							       scan_width);
	}

	if (!cfg80211_reg_can_beacon(&rdev->wiphy, &setup->chandef,
				     NL80211_IFTYPE_MESH_POINT))
		return -EINVAL;

	err = rdev_join_mesh(rdev, dev, conf, setup);
	if (!err) {
		memcpy(wdev->ssid, setup->mesh_id, setup->mesh_id_len);
		wdev->mesh_id_len = setup->mesh_id_len;
		wdev->chandef = setup->chandef;
		wdev->beacon_interval = setup->beacon_interval;
	}

	return err;
}

int cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct mesh_setup *setup,
		       const struct mesh_config *conf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_join_mesh(rdev, dev, setup, conf);
	wdev_unlock(wdev);

	return err;
}

int cfg80211_set_mesh_channel(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev,
			      struct cfg80211_chan_def *chandef)
{
	int err;

	/*
	 * Workaround for libertas (only!), it puts the interface
	 * into mesh mode but doesn't implement join_mesh. Instead,
	 * it is configured via sysfs and then joins the mesh when
	 * you set the channel. Note that the libertas mesh isn't
	 * compatible with 802.11 mesh.
	 */
	if (rdev->ops->libertas_set_mesh_channel) {
		if (chandef->width != NL80211_CHAN_WIDTH_20_NOHT)
			return -EINVAL;

		if (!netif_running(wdev->netdev))
			return -ENETDOWN;

		err = rdev_libertas_set_mesh_channel(rdev, wdev->netdev,
						     chandef->chan);
		if (!err)
			wdev->chandef = *chandef;

		return err;
	}

	if (wdev->mesh_id_len)
		return -EBUSY;

	wdev->preset_chandef = *chandef;
	return 0;
}

int __cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
			  struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	if (!rdev->ops->leave_mesh)
		return -EOPNOTSUPP;

	if (!wdev->mesh_id_len)
		return -ENOTCONN;

	err = rdev_leave_mesh(rdev, dev);
	if (!err) {
		wdev->mesh_id_len = 0;
		wdev->beacon_interval = 0;
		memset(&wdev->chandef, 0, sizeof(wdev->chandef));
		rdev_set_qos_map(rdev, dev, NULL);
	}

	return err;
}

int cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
			struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_leave_mesh(rdev, dev);
	wdev_unlock(wdev);

	return err;
}
