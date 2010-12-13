#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "core.h"

/* Default values, timeouts in ms */
#define MESH_TTL 		31
#define MESH_DEFAULT_ELEMENT_TTL 31
#define MESH_MAX_RETR	 	3
#define MESH_RET_T 		100
#define MESH_CONF_T 		100
#define MESH_HOLD_T 		100

#define MESH_PATH_TIMEOUT	5000

/*
 * Minimum interval between two consecutive PREQs originated by the same
 * interface
 */
#define MESH_PREQ_MIN_INT	10
#define MESH_DIAM_TRAVERSAL_TIME 50

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


const struct mesh_config default_mesh_config = {
	.dot11MeshRetryTimeout = MESH_RET_T,
	.dot11MeshConfirmTimeout = MESH_CONF_T,
	.dot11MeshHoldingTimeout = MESH_HOLD_T,
	.dot11MeshMaxRetries = MESH_MAX_RETR,
	.dot11MeshTTL = MESH_TTL,
	.element_ttl = MESH_DEFAULT_ELEMENT_TTL,
	.auto_open_plinks = true,
	.dot11MeshMaxPeerLinks = MESH_MAX_ESTAB_PLINKS,
	.dot11MeshHWMPactivePathTimeout = MESH_PATH_TIMEOUT,
	.dot11MeshHWMPpreqMinInterval = MESH_PREQ_MIN_INT,
	.dot11MeshHWMPnetDiameterTraversalTime = MESH_DIAM_TRAVERSAL_TIME,
	.dot11MeshHWMPmaxPREQretries = MESH_MAX_PREQ_RETRIES,
	.path_refresh_time = MESH_PATH_REFRESH_TIME,
	.min_discovery_timeout = MESH_MIN_DISCOVERY_TIMEOUT,
};


int __cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 const u8 *mesh_id, u8 mesh_id_len,
			 const struct mesh_config *conf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct mesh_setup setup = {
		.mesh_id = mesh_id,
		.mesh_id_len = mesh_id_len,
	};
	int err;

	BUILD_BUG_ON(IEEE80211_MAX_SSID_LEN != IEEE80211_MAX_MESH_ID_LEN);

	ASSERT_WDEV_LOCK(wdev);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	if (wdev->mesh_id_len)
		return -EALREADY;

	if (!mesh_id_len)
		return -EINVAL;

	if (!rdev->ops->join_mesh)
		return -EOPNOTSUPP;

	err = rdev->ops->join_mesh(&rdev->wiphy, dev, conf, &setup);
	if (!err) {
		memcpy(wdev->ssid, mesh_id, mesh_id_len);
		wdev->mesh_id_len = mesh_id_len;
	}

	return err;
}

int cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       const u8 *mesh_id, u8 mesh_id_len,
		       const struct mesh_config *conf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_join_mesh(rdev, dev, mesh_id, mesh_id_len, conf);
	wdev_unlock(wdev);

	return err;
}

static int __cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
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

	err = rdev->ops->leave_mesh(&rdev->wiphy, dev);
	if (!err)
		wdev->mesh_id_len = 0;
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
