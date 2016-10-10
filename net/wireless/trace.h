#undef TRACE_SYSTEM
#define TRACE_SYSTEM cfg80211

#if !defined(__RDEV_OPS_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __RDEV_OPS_TRACE

#include <linux/tracepoint.h>

#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include "core.h"

#define MAC_ENTRY(entry_mac) __array(u8, entry_mac, ETH_ALEN)
#define MAC_ASSIGN(entry_mac, given_mac) do {			     \
	if (given_mac)						     \
		memcpy(__entry->entry_mac, given_mac, ETH_ALEN);     \
	else							     \
		eth_zero_addr(__entry->entry_mac);		     \
	} while (0)
#define MAC_PR_FMT "%pM"
#define MAC_PR_ARG(entry_mac) (__entry->entry_mac)

#define MAXNAME		32
#define WIPHY_ENTRY	__array(char, wiphy_name, 32)
#define WIPHY_ASSIGN	strlcpy(__entry->wiphy_name, wiphy_name(wiphy), MAXNAME)
#define WIPHY_PR_FMT	"%s"
#define WIPHY_PR_ARG	__entry->wiphy_name

#define WDEV_ENTRY	__field(u32, id)
#define WDEV_ASSIGN	(__entry->id) = (!IS_ERR_OR_NULL(wdev)	\
					 ? wdev->identifier : 0)
#define WDEV_PR_FMT	"wdev(%u)"
#define WDEV_PR_ARG	(__entry->id)

#define NETDEV_ENTRY	__array(char, name, IFNAMSIZ) \
			__field(int, ifindex)
#define NETDEV_ASSIGN					       \
	do {						       \
		memcpy(__entry->name, netdev->name, IFNAMSIZ); \
		(__entry->ifindex) = (netdev->ifindex);	       \
	} while (0)
#define NETDEV_PR_FMT	"netdev:%s(%d)"
#define NETDEV_PR_ARG	__entry->name, __entry->ifindex

#define MESH_CFG_ENTRY __field(u16, dot11MeshRetryTimeout)		   \
		       __field(u16, dot11MeshConfirmTimeout)		   \
		       __field(u16, dot11MeshHoldingTimeout)		   \
		       __field(u16, dot11MeshMaxPeerLinks)		   \
		       __field(u8, dot11MeshMaxRetries)			   \
		       __field(u8, dot11MeshTTL)			   \
		       __field(u8, element_ttl)				   \
		       __field(bool, auto_open_plinks)			   \
		       __field(u32, dot11MeshNbrOffsetMaxNeighbor)	   \
		       __field(u8, dot11MeshHWMPmaxPREQretries)		   \
		       __field(u32, path_refresh_time)			   \
		       __field(u32, dot11MeshHWMPactivePathTimeout)	   \
		       __field(u16, min_discovery_timeout)		   \
		       __field(u16, dot11MeshHWMPpreqMinInterval)	   \
		       __field(u16, dot11MeshHWMPperrMinInterval)	   \
		       __field(u16, dot11MeshHWMPnetDiameterTraversalTime) \
		       __field(u8, dot11MeshHWMPRootMode)		   \
		       __field(u16, dot11MeshHWMPRannInterval)		   \
		       __field(bool, dot11MeshGateAnnouncementProtocol)	   \
		       __field(bool, dot11MeshForwarding)		   \
		       __field(s32, rssi_threshold)			   \
		       __field(u16, ht_opmode)				   \
		       __field(u32, dot11MeshHWMPactivePathToRootTimeout)  \
		       __field(u16, dot11MeshHWMProotInterval)		   \
		       __field(u16, dot11MeshHWMPconfirmationInterval)
#define MESH_CFG_ASSIGN							      \
	do {								      \
		__entry->dot11MeshRetryTimeout = conf->dot11MeshRetryTimeout; \
		__entry->dot11MeshConfirmTimeout =			      \
				conf->dot11MeshConfirmTimeout;		      \
		__entry->dot11MeshHoldingTimeout =			      \
				conf->dot11MeshHoldingTimeout;		      \
		__entry->dot11MeshMaxPeerLinks = conf->dot11MeshMaxPeerLinks; \
		__entry->dot11MeshMaxRetries = conf->dot11MeshMaxRetries;     \
		__entry->dot11MeshTTL = conf->dot11MeshTTL;		      \
		__entry->element_ttl = conf->element_ttl;		      \
		__entry->auto_open_plinks = conf->auto_open_plinks;	      \
		__entry->dot11MeshNbrOffsetMaxNeighbor =		      \
				conf->dot11MeshNbrOffsetMaxNeighbor;	      \
		__entry->dot11MeshHWMPmaxPREQretries =			      \
				conf->dot11MeshHWMPmaxPREQretries;	      \
		__entry->path_refresh_time = conf->path_refresh_time;	      \
		__entry->dot11MeshHWMPactivePathTimeout =		      \
				conf->dot11MeshHWMPactivePathTimeout;	      \
		__entry->min_discovery_timeout = conf->min_discovery_timeout; \
		__entry->dot11MeshHWMPpreqMinInterval =			      \
				conf->dot11MeshHWMPpreqMinInterval;	      \
		__entry->dot11MeshHWMPperrMinInterval =			      \
				conf->dot11MeshHWMPperrMinInterval;	      \
		__entry->dot11MeshHWMPnetDiameterTraversalTime =	      \
				conf->dot11MeshHWMPnetDiameterTraversalTime;  \
		__entry->dot11MeshHWMPRootMode = conf->dot11MeshHWMPRootMode; \
		__entry->dot11MeshHWMPRannInterval =			      \
				conf->dot11MeshHWMPRannInterval;	      \
		__entry->dot11MeshGateAnnouncementProtocol =		      \
				conf->dot11MeshGateAnnouncementProtocol;      \
		__entry->dot11MeshForwarding = conf->dot11MeshForwarding;     \
		__entry->rssi_threshold = conf->rssi_threshold;		      \
		__entry->ht_opmode = conf->ht_opmode;			      \
		__entry->dot11MeshHWMPactivePathToRootTimeout =		      \
				conf->dot11MeshHWMPactivePathToRootTimeout;   \
		__entry->dot11MeshHWMProotInterval =			      \
				conf->dot11MeshHWMProotInterval;	      \
		__entry->dot11MeshHWMPconfirmationInterval =		      \
				conf->dot11MeshHWMPconfirmationInterval;      \
	} while (0)

#define CHAN_ENTRY __field(enum nl80211_band, band) \
		   __field(u16, center_freq)
#define CHAN_ASSIGN(chan)					  \
	do {							  \
		if (chan) {					  \
			__entry->band = chan->band;		  \
			__entry->center_freq = chan->center_freq; \
		} else {					  \
			__entry->band = 0;			  \
			__entry->center_freq = 0;		  \
		}						  \
	} while (0)
#define CHAN_PR_FMT "band: %d, freq: %u"
#define CHAN_PR_ARG __entry->band, __entry->center_freq

#define CHAN_DEF_ENTRY __field(enum nl80211_band, band)		\
		       __field(u32, control_freq)			\
		       __field(u32, width)				\
		       __field(u32, center_freq1)			\
		       __field(u32, center_freq2)
#define CHAN_DEF_ASSIGN(chandef)					\
	do {								\
		if ((chandef) && (chandef)->chan) {			\
			__entry->band = (chandef)->chan->band;		\
			__entry->control_freq =				\
				(chandef)->chan->center_freq;		\
			__entry->width = (chandef)->width;		\
			__entry->center_freq1 = (chandef)->center_freq1;\
			__entry->center_freq2 = (chandef)->center_freq2;\
		} else {						\
			__entry->band = 0;				\
			__entry->control_freq = 0;			\
			__entry->width = 0;				\
			__entry->center_freq1 = 0;			\
			__entry->center_freq2 = 0;			\
		}							\
	} while (0)
#define CHAN_DEF_PR_FMT							\
	"band: %d, control freq: %u, width: %d, cf1: %u, cf2: %u"
#define CHAN_DEF_PR_ARG __entry->band, __entry->control_freq,		\
			__entry->width, __entry->center_freq1,		\
			__entry->center_freq2

#define SINFO_ENTRY __field(int, generation)	    \
		    __field(u32, connected_time)    \
		    __field(u32, inactive_time)	    \
		    __field(u32, rx_bytes)	    \
		    __field(u32, tx_bytes)	    \
		    __field(u32, rx_packets)	    \
		    __field(u32, tx_packets)	    \
		    __field(u32, tx_retries)	    \
		    __field(u32, tx_failed)	    \
		    __field(u32, rx_dropped_misc)   \
		    __field(u32, beacon_loss_count) \
		    __field(u16, llid)		    \
		    __field(u16, plid)		    \
		    __field(u8, plink_state)
#define SINFO_ASSIGN						       \
	do {							       \
		__entry->generation = sinfo->generation;	       \
		__entry->connected_time = sinfo->connected_time;       \
		__entry->inactive_time = sinfo->inactive_time;	       \
		__entry->rx_bytes = sinfo->rx_bytes;		       \
		__entry->tx_bytes = sinfo->tx_bytes;		       \
		__entry->rx_packets = sinfo->rx_packets;	       \
		__entry->tx_packets = sinfo->tx_packets;	       \
		__entry->tx_retries = sinfo->tx_retries;	       \
		__entry->tx_failed = sinfo->tx_failed;		       \
		__entry->rx_dropped_misc = sinfo->rx_dropped_misc;     \
		__entry->beacon_loss_count = sinfo->beacon_loss_count; \
		__entry->llid = sinfo->llid;			       \
		__entry->plid = sinfo->plid;			       \
		__entry->plink_state = sinfo->plink_state;	       \
	} while (0)

#define BOOL_TO_STR(bo) (bo) ? "true" : "false"

#define QOS_MAP_ENTRY __field(u8, num_des)			\
		      __array(u8, dscp_exception,		\
			      2 * IEEE80211_QOS_MAP_MAX_EX)	\
		      __array(u8, up, IEEE80211_QOS_MAP_LEN_MIN)
#define QOS_MAP_ASSIGN(qos_map)					\
	do {							\
		if ((qos_map)) {				\
			__entry->num_des = (qos_map)->num_des;	\
			memcpy(__entry->dscp_exception,		\
			       &(qos_map)->dscp_exception,	\
			       2 * IEEE80211_QOS_MAP_MAX_EX);	\
			memcpy(__entry->up, &(qos_map)->up,	\
			       IEEE80211_QOS_MAP_LEN_MIN);	\
		} else {					\
			__entry->num_des = 0;			\
			memset(__entry->dscp_exception, 0,	\
			       2 * IEEE80211_QOS_MAP_MAX_EX);	\
			memset(__entry->up, 0,			\
			       IEEE80211_QOS_MAP_LEN_MIN);	\
		}						\
	} while (0)

/*************************************************************
 *			rdev->ops traces		     *
 *************************************************************/

TRACE_EVENT(rdev_suspend,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_wowlan *wow),
	TP_ARGS(wiphy, wow),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(bool, any)
		__field(bool, disconnect)
		__field(bool, magic_pkt)
		__field(bool, gtk_rekey_failure)
		__field(bool, eap_identity_req)
		__field(bool, four_way_handshake)
		__field(bool, rfkill_release)
		__field(bool, valid_wow)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		if (wow) {
			__entry->any = wow->any;
			__entry->disconnect = wow->disconnect;
			__entry->magic_pkt = wow->magic_pkt;
			__entry->gtk_rekey_failure = wow->gtk_rekey_failure;
			__entry->eap_identity_req = wow->eap_identity_req;
			__entry->four_way_handshake = wow->four_way_handshake;
			__entry->rfkill_release = wow->rfkill_release;
			__entry->valid_wow = true;
		} else {
			__entry->valid_wow = false;
		}
	),
	TP_printk(WIPHY_PR_FMT ", wow%s - any: %d, disconnect: %d, "
		  "magic pkt: %d, gtk rekey failure: %d, eap identify req: %d, "
		  "four way handshake: %d, rfkill release: %d.",
		  WIPHY_PR_ARG, __entry->valid_wow ? "" : "(Not configured!)",
		  __entry->any, __entry->disconnect, __entry->magic_pkt,
		  __entry->gtk_rekey_failure, __entry->eap_identity_req,
		  __entry->four_way_handshake, __entry->rfkill_release)
);

TRACE_EVENT(rdev_return_int,
	TP_PROTO(struct wiphy *wiphy, int ret),
	TP_ARGS(wiphy, ret),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(WIPHY_PR_FMT ", returned: %d", WIPHY_PR_ARG, __entry->ret)
);

TRACE_EVENT(rdev_scan,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_scan_request *request),
	TP_ARGS(wiphy, request),
	TP_STRUCT__entry(
		WIPHY_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT, WIPHY_PR_ARG)
);

DECLARE_EVENT_CLASS(wiphy_only_evt,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy),
	TP_STRUCT__entry(
		WIPHY_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT, WIPHY_PR_ARG)
);

DEFINE_EVENT(wiphy_only_evt, rdev_resume,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

DEFINE_EVENT(wiphy_only_evt, rdev_return_void,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

DEFINE_EVENT(wiphy_only_evt, rdev_get_antenna,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

DEFINE_EVENT(wiphy_only_evt, rdev_rfkill_poll,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

DECLARE_EVENT_CLASS(wiphy_enabled_evt,
	TP_PROTO(struct wiphy *wiphy, bool enabled),
	TP_ARGS(wiphy, enabled),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(bool, enabled)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->enabled = enabled;
	),
	TP_printk(WIPHY_PR_FMT ", %senabled ",
		  WIPHY_PR_ARG, __entry->enabled ? "" : "not ")
);

DEFINE_EVENT(wiphy_enabled_evt, rdev_set_wakeup,
	TP_PROTO(struct wiphy *wiphy, bool enabled),
	TP_ARGS(wiphy, enabled)
);

TRACE_EVENT(rdev_add_virtual_intf,
	TP_PROTO(struct wiphy *wiphy, char *name, enum nl80211_iftype type),
	TP_ARGS(wiphy, name, type),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__string(vir_intf_name, name ? name : "<noname>")
		__field(enum nl80211_iftype, type)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__assign_str(vir_intf_name, name ? name : "<noname>");
		__entry->type = type;
	),
	TP_printk(WIPHY_PR_FMT ", virtual intf name: %s, type: %d",
		  WIPHY_PR_ARG, __get_str(vir_intf_name), __entry->type)
);

DECLARE_EVENT_CLASS(wiphy_wdev_evt,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT, WIPHY_PR_ARG, WDEV_PR_ARG)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_return_wdev,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_del_virtual_intf,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_change_virtual_intf,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 enum nl80211_iftype type),
	TP_ARGS(wiphy, netdev, type),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(enum nl80211_iftype, type)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->type = type;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", type: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->type)
);

DECLARE_EVENT_CLASS(key_handle,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		 bool pairwise, const u8 *mac_addr),
	TP_ARGS(wiphy, netdev, key_index, pairwise, mac_addr),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(mac_addr)
		__field(u8, key_index)
		__field(bool, pairwise)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(mac_addr, mac_addr);
		__entry->key_index = key_index;
		__entry->pairwise = pairwise;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", key_index: %u, pairwise: %s, mac addr: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->key_index,
		  BOOL_TO_STR(__entry->pairwise), MAC_PR_ARG(mac_addr))
);

DEFINE_EVENT(key_handle, rdev_add_key,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		 bool pairwise, const u8 *mac_addr),
	TP_ARGS(wiphy, netdev, key_index, pairwise, mac_addr)
);

DEFINE_EVENT(key_handle, rdev_get_key,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		 bool pairwise, const u8 *mac_addr),
	TP_ARGS(wiphy, netdev, key_index, pairwise, mac_addr)
);

DEFINE_EVENT(key_handle, rdev_del_key,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		 bool pairwise, const u8 *mac_addr),
	TP_ARGS(wiphy, netdev, key_index, pairwise, mac_addr)
);

TRACE_EVENT(rdev_set_default_key,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index,
		 bool unicast, bool multicast),
	TP_ARGS(wiphy, netdev, key_index, unicast, multicast),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u8, key_index)
		__field(bool, unicast)
		__field(bool, multicast)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->key_index = key_index;
		__entry->unicast = unicast;
		__entry->multicast = multicast;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", key index: %u, unicast: %s, multicast: %s",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->key_index,
		  BOOL_TO_STR(__entry->unicast),
		  BOOL_TO_STR(__entry->multicast))
);

TRACE_EVENT(rdev_set_default_mgmt_key,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 key_index),
	TP_ARGS(wiphy, netdev, key_index),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u8, key_index)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->key_index = key_index;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", key index: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->key_index)
);

TRACE_EVENT(rdev_start_ap,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_ap_settings *settings),
	TP_ARGS(wiphy, netdev, settings),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
		__field(int, beacon_interval)
		__field(int, dtim_period)
		__array(char, ssid, IEEE80211_MAX_SSID_LEN + 1)
		__field(enum nl80211_hidden_ssid, hidden_ssid)
		__field(u32, wpa_ver)
		__field(bool, privacy)
		__field(enum nl80211_auth_type, auth_type)
		__field(int, inactivity_timeout)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(&settings->chandef);
		__entry->beacon_interval = settings->beacon_interval;
		__entry->dtim_period = settings->dtim_period;
		__entry->hidden_ssid = settings->hidden_ssid;
		__entry->wpa_ver = settings->crypto.wpa_versions;
		__entry->privacy = settings->privacy;
		__entry->auth_type = settings->auth_type;
		__entry->inactivity_timeout = settings->inactivity_timeout;
		memset(__entry->ssid, 0, IEEE80211_MAX_SSID_LEN + 1);
		memcpy(__entry->ssid, settings->ssid, settings->ssid_len);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", AP settings - ssid: %s, "
		  CHAN_DEF_PR_FMT ", beacon interval: %d, dtim period: %d, "
		  "hidden ssid: %d, wpa versions: %u, privacy: %s, "
		  "auth type: %d, inactivity timeout: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->ssid, CHAN_DEF_PR_ARG,
		  __entry->beacon_interval, __entry->dtim_period,
		  __entry->hidden_ssid, __entry->wpa_ver,
		  BOOL_TO_STR(__entry->privacy), __entry->auth_type,
		  __entry->inactivity_timeout)
);

TRACE_EVENT(rdev_change_beacon,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_beacon_data *info),
	TP_ARGS(wiphy, netdev, info),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__dynamic_array(u8, head, info ? info->head_len : 0)
		__dynamic_array(u8, tail, info ? info->tail_len : 0)
		__dynamic_array(u8, beacon_ies, info ? info->beacon_ies_len : 0)
		__dynamic_array(u8, proberesp_ies,
				info ? info->proberesp_ies_len : 0)
		__dynamic_array(u8, assocresp_ies,
				info ? info->assocresp_ies_len : 0)
		__dynamic_array(u8, probe_resp, info ? info->probe_resp_len : 0)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		if (info) {
			if (info->head)
				memcpy(__get_dynamic_array(head), info->head,
				       info->head_len);
			if (info->tail)
				memcpy(__get_dynamic_array(tail), info->tail,
				       info->tail_len);
			if (info->beacon_ies)
				memcpy(__get_dynamic_array(beacon_ies),
				       info->beacon_ies, info->beacon_ies_len);
			if (info->proberesp_ies)
				memcpy(__get_dynamic_array(proberesp_ies),
				       info->proberesp_ies,
				       info->proberesp_ies_len);
			if (info->assocresp_ies)
				memcpy(__get_dynamic_array(assocresp_ies),
				       info->assocresp_ies,
				       info->assocresp_ies_len);
			if (info->probe_resp)
				memcpy(__get_dynamic_array(probe_resp),
				       info->probe_resp, info->probe_resp_len);
		}
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT, WIPHY_PR_ARG, NETDEV_PR_ARG)
);

DECLARE_EVENT_CLASS(wiphy_netdev_evt,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT, WIPHY_PR_ARG, NETDEV_PR_ARG)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_stop_ap,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_sched_scan_stop,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_set_rekey_data,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_get_mesh_config,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_leave_mesh,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_leave_ibss,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_leave_ocb,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DEFINE_EVENT(wiphy_netdev_evt, rdev_flush_pmksa,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev),
	TP_ARGS(wiphy, netdev)
);

DECLARE_EVENT_CLASS(station_add_change,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *mac,
		 struct station_parameters *params),
	TP_ARGS(wiphy, netdev, mac, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(sta_mac)
		__field(u32, sta_flags_mask)
		__field(u32, sta_flags_set)
		__field(u32, sta_modify_mask)
		__field(int, listen_interval)
		__field(u16, capability)
		__field(u16, aid)
		__field(u8, plink_action)
		__field(u8, plink_state)
		__field(u8, uapsd_queues)
		__field(u8, max_sp)
		__field(u8, opmode_notif)
		__field(bool, opmode_notif_used)
		__array(u8, ht_capa, (int)sizeof(struct ieee80211_ht_cap))
		__array(u8, vht_capa, (int)sizeof(struct ieee80211_vht_cap))
		__array(char, vlan, IFNAMSIZ)
		__dynamic_array(u8, supported_rates,
				params->supported_rates_len)
		__dynamic_array(u8, ext_capab, params->ext_capab_len)
		__dynamic_array(u8, supported_channels,
				params->supported_channels_len)
		__dynamic_array(u8, supported_oper_classes,
				params->supported_oper_classes_len)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(sta_mac, mac);
		__entry->sta_flags_mask = params->sta_flags_mask;
		__entry->sta_flags_set = params->sta_flags_set;
		__entry->sta_modify_mask = params->sta_modify_mask;
		__entry->listen_interval = params->listen_interval;
		__entry->aid = params->aid;
		__entry->plink_action = params->plink_action;
		__entry->plink_state = params->plink_state;
		__entry->uapsd_queues = params->uapsd_queues;
		memset(__entry->ht_capa, 0, sizeof(struct ieee80211_ht_cap));
		if (params->ht_capa)
			memcpy(__entry->ht_capa, params->ht_capa,
			       sizeof(struct ieee80211_ht_cap));
		memset(__entry->vht_capa, 0, sizeof(struct ieee80211_vht_cap));
		if (params->vht_capa)
			memcpy(__entry->vht_capa, params->vht_capa,
			       sizeof(struct ieee80211_vht_cap));
		memset(__entry->vlan, 0, sizeof(__entry->vlan));
		if (params->vlan)
			memcpy(__entry->vlan, params->vlan->name, IFNAMSIZ);
		if (params->supported_rates && params->supported_rates_len)
			memcpy(__get_dynamic_array(supported_rates),
			       params->supported_rates,
			       params->supported_rates_len);
		if (params->ext_capab && params->ext_capab_len)
			memcpy(__get_dynamic_array(ext_capab),
			       params->ext_capab,
			       params->ext_capab_len);
		if (params->supported_channels &&
		    params->supported_channels_len)
			memcpy(__get_dynamic_array(supported_channels),
			       params->supported_channels,
			       params->supported_channels_len);
		if (params->supported_oper_classes &&
		    params->supported_oper_classes_len)
			memcpy(__get_dynamic_array(supported_oper_classes),
			       params->supported_oper_classes,
			       params->supported_oper_classes_len);
		__entry->max_sp = params->max_sp;
		__entry->capability = params->capability;
		__entry->opmode_notif = params->opmode_notif;
		__entry->opmode_notif_used = params->opmode_notif_used;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", station mac: " MAC_PR_FMT
		  ", station flags mask: %u, station flags set: %u, "
		  "station modify mask: %u, listen interval: %d, aid: %u, "
		  "plink action: %u, plink state: %u, uapsd queues: %u, vlan:%s",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(sta_mac),
		  __entry->sta_flags_mask, __entry->sta_flags_set,
		  __entry->sta_modify_mask, __entry->listen_interval,
		  __entry->aid, __entry->plink_action, __entry->plink_state,
		  __entry->uapsd_queues, __entry->vlan)
);

DEFINE_EVENT(station_add_change, rdev_add_station,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *mac,
		 struct station_parameters *params),
	TP_ARGS(wiphy, netdev, mac, params)
);

DEFINE_EVENT(station_add_change, rdev_change_station,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *mac,
		 struct station_parameters *params),
	TP_ARGS(wiphy, netdev, mac, params)
);

DECLARE_EVENT_CLASS(wiphy_netdev_mac_evt,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, const u8 *mac),
	TP_ARGS(wiphy, netdev, mac),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(sta_mac)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(sta_mac, mac);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", mac: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(sta_mac))
);

DECLARE_EVENT_CLASS(station_del,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct station_del_parameters *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(sta_mac)
		__field(u8, subtype)
		__field(u16, reason_code)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(sta_mac, params->mac);
		__entry->subtype = params->subtype;
		__entry->reason_code = params->reason_code;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", station mac: " MAC_PR_FMT
		  ", subtype: %u, reason_code: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(sta_mac),
		  __entry->subtype, __entry->reason_code)
);

DEFINE_EVENT(station_del, rdev_del_station,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct station_del_parameters *params),
	TP_ARGS(wiphy, netdev, params)
);

DEFINE_EVENT(wiphy_netdev_mac_evt, rdev_get_station,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, const u8 *mac),
	TP_ARGS(wiphy, netdev, mac)
);

DEFINE_EVENT(wiphy_netdev_mac_evt, rdev_del_mpath,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, const u8 *mac),
	TP_ARGS(wiphy, netdev, mac)
);

DEFINE_EVENT(wiphy_netdev_mac_evt, rdev_set_wds_peer,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, const u8 *mac),
	TP_ARGS(wiphy, netdev, mac)
);

TRACE_EVENT(rdev_dump_station,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, int idx,
		 u8 *mac),
	TP_ARGS(wiphy, netdev, idx, mac),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(sta_mac)
		__field(int, idx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(sta_mac, mac);
		__entry->idx = idx;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", station mac: " MAC_PR_FMT ", idx: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(sta_mac),
		  __entry->idx)
);

TRACE_EVENT(rdev_return_int_station_info,
	TP_PROTO(struct wiphy *wiphy, int ret, struct station_info *sinfo),
	TP_ARGS(wiphy, ret, sinfo),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
		SINFO_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->ret = ret;
		SINFO_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", returned %d" ,
		  WIPHY_PR_ARG, __entry->ret)
);

DECLARE_EVENT_CLASS(mpath_evt,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *dst,
		 u8 *next_hop),
	TP_ARGS(wiphy, netdev, dst, next_hop),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(dst)
		MAC_ENTRY(next_hop)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(dst, dst);
		MAC_ASSIGN(next_hop, next_hop);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", destination: " MAC_PR_FMT ", next hop: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(dst),
		  MAC_PR_ARG(next_hop))
);

DEFINE_EVENT(mpath_evt, rdev_add_mpath,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *dst,
		 u8 *next_hop),
	TP_ARGS(wiphy, netdev, dst, next_hop)
);

DEFINE_EVENT(mpath_evt, rdev_change_mpath,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *dst,
		 u8 *next_hop),
	TP_ARGS(wiphy, netdev, dst, next_hop)
);

DEFINE_EVENT(mpath_evt, rdev_get_mpath,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u8 *dst,
		 u8 *next_hop),
	TP_ARGS(wiphy, netdev, dst, next_hop)
);

TRACE_EVENT(rdev_dump_mpath,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, int idx,
		 u8 *dst, u8 *next_hop),
	TP_ARGS(wiphy, netdev, idx, dst, next_hop),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(dst)
		MAC_ENTRY(next_hop)
		__field(int, idx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(dst, dst);
		MAC_ASSIGN(next_hop, next_hop);
		__entry->idx = idx;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", index: %d, destination: "
		  MAC_PR_FMT ", next hop: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->idx, MAC_PR_ARG(dst),
		  MAC_PR_ARG(next_hop))
);

TRACE_EVENT(rdev_get_mpp,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u8 *dst, u8 *mpp),
	TP_ARGS(wiphy, netdev, dst, mpp),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(dst)
		MAC_ENTRY(mpp)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(dst, dst);
		MAC_ASSIGN(mpp, mpp);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", destination: " MAC_PR_FMT
		  ", mpp: " MAC_PR_FMT, WIPHY_PR_ARG, NETDEV_PR_ARG,
		  MAC_PR_ARG(dst), MAC_PR_ARG(mpp))
);

TRACE_EVENT(rdev_dump_mpp,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, int idx,
		 u8 *dst, u8 *mpp),
	TP_ARGS(wiphy, netdev, idx, mpp, dst),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(dst)
		MAC_ENTRY(mpp)
		__field(int, idx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(dst, dst);
		MAC_ASSIGN(mpp, mpp);
		__entry->idx = idx;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", index: %d, destination: "
		  MAC_PR_FMT ", mpp: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->idx, MAC_PR_ARG(dst),
		  MAC_PR_ARG(mpp))
);

TRACE_EVENT(rdev_return_int_mpath_info,
	TP_PROTO(struct wiphy *wiphy, int ret, struct mpath_info *pinfo),
	TP_ARGS(wiphy, ret, pinfo),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
		__field(int, generation)
		__field(u32, filled)
		__field(u32, frame_qlen)
		__field(u32, sn)
		__field(u32, metric)
		__field(u32, exptime)
		__field(u32, discovery_timeout)
		__field(u8, discovery_retries)
		__field(u8, flags)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->ret = ret;
		__entry->generation = pinfo->generation;
		__entry->filled = pinfo->filled;
		__entry->frame_qlen = pinfo->frame_qlen;
		__entry->sn = pinfo->sn;
		__entry->metric = pinfo->metric;
		__entry->exptime = pinfo->exptime;
		__entry->discovery_timeout = pinfo->discovery_timeout;
		__entry->discovery_retries = pinfo->discovery_retries;
		__entry->flags = pinfo->flags;
	),
	TP_printk(WIPHY_PR_FMT ", returned %d. mpath info - generation: %d, "
		  "filled: %u, frame qlen: %u, sn: %u, metric: %u, exptime: %u,"
		  " discovery timeout: %u, discovery retries: %u, flags: %u",
		  WIPHY_PR_ARG, __entry->ret, __entry->generation,
		  __entry->filled, __entry->frame_qlen, __entry->sn,
		  __entry->metric, __entry->exptime, __entry->discovery_timeout,
		  __entry->discovery_retries, __entry->flags)
);

TRACE_EVENT(rdev_return_int_mesh_config,
	TP_PROTO(struct wiphy *wiphy, int ret, struct mesh_config *conf),
	TP_ARGS(wiphy, ret, conf),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		MESH_CFG_ENTRY
		__field(int, ret)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		MESH_CFG_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(WIPHY_PR_FMT ", returned: %d",
		  WIPHY_PR_ARG, __entry->ret)
);

TRACE_EVENT(rdev_update_mesh_config,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u32 mask,
		 const struct mesh_config *conf),
	TP_ARGS(wiphy, netdev, mask, conf),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MESH_CFG_ENTRY
		__field(u32, mask)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MESH_CFG_ASSIGN;
		__entry->mask = mask;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", mask: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->mask)
);

TRACE_EVENT(rdev_join_mesh,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const struct mesh_config *conf,
		 const struct mesh_setup *setup),
	TP_ARGS(wiphy, netdev, conf, setup),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MESH_CFG_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MESH_CFG_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG)
);

TRACE_EVENT(rdev_change_bss,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct bss_parameters *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(int, use_cts_prot)
		__field(int, use_short_preamble)
		__field(int, use_short_slot_time)
		__field(int, ap_isolate)
		__field(int, ht_opmode)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->use_cts_prot = params->use_cts_prot;
		__entry->use_short_preamble = params->use_short_preamble;
		__entry->use_short_slot_time = params->use_short_slot_time;
		__entry->ap_isolate = params->ap_isolate;
		__entry->ht_opmode = params->ht_opmode;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", use cts prot: %d, "
		  "use short preamble: %d, use short slot time: %d, "
		  "ap isolate: %d, ht opmode: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->use_cts_prot,
		  __entry->use_short_preamble, __entry->use_short_slot_time,
		  __entry->ap_isolate, __entry->ht_opmode)
);

TRACE_EVENT(rdev_set_txq_params,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct ieee80211_txq_params *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(enum nl80211_ac, ac)
		__field(u16, txop)
		__field(u16, cwmin)
		__field(u16, cwmax)
		__field(u8, aifs)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->ac = params->ac;
		__entry->txop = params->txop;
		__entry->cwmin = params->cwmin;
		__entry->cwmax = params->cwmax;
		__entry->aifs = params->aifs;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", ac: %d, txop: %u, cwmin: %u, cwmax: %u, aifs: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->ac, __entry->txop,
		  __entry->cwmin, __entry->cwmax, __entry->aifs)
);

TRACE_EVENT(rdev_libertas_set_mesh_channel,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct ieee80211_channel *chan),
	TP_ARGS(wiphy, netdev, chan),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		CHAN_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		CHAN_ASSIGN(chan);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " CHAN_PR_FMT, WIPHY_PR_ARG,
		  NETDEV_PR_ARG, CHAN_PR_ARG)
);

TRACE_EVENT(rdev_set_monitor_channel,
	TP_PROTO(struct wiphy *wiphy,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_DEF_PR_FMT,
		  WIPHY_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(rdev_auth,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_auth_request *req),
	TP_ARGS(wiphy, netdev, req),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		__field(enum nl80211_auth_type, auth_type)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		if (req->bss)
			MAC_ASSIGN(bssid, req->bss->bssid);
		else
			eth_zero_addr(__entry->bssid);
		__entry->auth_type = req->auth_type;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", auth type: %d, bssid: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->auth_type,
		  MAC_PR_ARG(bssid))
);

TRACE_EVENT(rdev_assoc,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_assoc_request *req),
	TP_ARGS(wiphy, netdev, req),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		MAC_ENTRY(prev_bssid)
		__field(bool, use_mfp)
		__field(u32, flags)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		if (req->bss)
			MAC_ASSIGN(bssid, req->bss->bssid);
		else
			eth_zero_addr(__entry->bssid);
		MAC_ASSIGN(prev_bssid, req->prev_bssid);
		__entry->use_mfp = req->use_mfp;
		__entry->flags = req->flags;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT
		  ", previous bssid: " MAC_PR_FMT ", use mfp: %s, flags: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid),
		  MAC_PR_ARG(prev_bssid), BOOL_TO_STR(__entry->use_mfp),
		  __entry->flags)
);

TRACE_EVENT(rdev_deauth,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_deauth_request *req),
	TP_ARGS(wiphy, netdev, req),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		__field(u16, reason_code)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, req->bssid);
		__entry->reason_code = req->reason_code;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT ", reason: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid),
		  __entry->reason_code)
);

TRACE_EVENT(rdev_disassoc,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_disassoc_request *req),
	TP_ARGS(wiphy, netdev, req),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		__field(u16, reason_code)
		__field(bool, local_state_change)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		if (req->bss)
			MAC_ASSIGN(bssid, req->bss->bssid);
		else
			eth_zero_addr(__entry->bssid);
		__entry->reason_code = req->reason_code;
		__entry->local_state_change = req->local_state_change;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT
		  ", reason: %u, local state change: %s",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid),
		  __entry->reason_code,
		  BOOL_TO_STR(__entry->local_state_change))
);

TRACE_EVENT(rdev_mgmt_tx_cancel_wait,
	TP_PROTO(struct wiphy *wiphy,
		 struct wireless_dev *wdev, u64 cookie),
	TP_ARGS(wiphy, wdev, cookie),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u64, cookie)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->cookie = cookie;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", cookie: %llu ",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->cookie)
);

TRACE_EVENT(rdev_set_power_mgmt,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 bool enabled, int timeout),
	TP_ARGS(wiphy, netdev, enabled, timeout),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(bool, enabled)
		__field(int, timeout)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->enabled = enabled;
		__entry->timeout = timeout;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", %senabled, timeout: %d ",
		  WIPHY_PR_ARG, NETDEV_PR_ARG,
		  __entry->enabled ? "" : "not ", __entry->timeout)
);

TRACE_EVENT(rdev_connect,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_connect_params *sme),
	TP_ARGS(wiphy, netdev, sme),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		__array(char, ssid, IEEE80211_MAX_SSID_LEN + 1)
		__field(enum nl80211_auth_type, auth_type)
		__field(bool, privacy)
		__field(u32, wpa_versions)
		__field(u32, flags)
		MAC_ENTRY(prev_bssid)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, sme->bssid);
		memset(__entry->ssid, 0, IEEE80211_MAX_SSID_LEN + 1);
		memcpy(__entry->ssid, sme->ssid, sme->ssid_len);
		__entry->auth_type = sme->auth_type;
		__entry->privacy = sme->privacy;
		__entry->wpa_versions = sme->crypto.wpa_versions;
		__entry->flags = sme->flags;
		MAC_ASSIGN(prev_bssid, sme->prev_bssid);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT
		  ", ssid: %s, auth type: %d, privacy: %s, wpa versions: %u, "
		  "flags: %u, previous bssid: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid), __entry->ssid,
		  __entry->auth_type, BOOL_TO_STR(__entry->privacy),
		  __entry->wpa_versions, __entry->flags, MAC_PR_ARG(prev_bssid))
);

TRACE_EVENT(rdev_set_cqm_rssi_config,
	TP_PROTO(struct wiphy *wiphy,
		 struct net_device *netdev, s32 rssi_thold,
		 u32 rssi_hyst),
	TP_ARGS(wiphy, netdev, rssi_thold, rssi_hyst),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(s32, rssi_thold)
		__field(u32, rssi_hyst)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->rssi_thold = rssi_thold;
		__entry->rssi_hyst = rssi_hyst;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT
		  ", rssi_thold: %d, rssi_hyst: %u ",
		  WIPHY_PR_ARG, NETDEV_PR_ARG,
		 __entry->rssi_thold, __entry->rssi_hyst)
);

TRACE_EVENT(rdev_set_cqm_txe_config,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, u32 rate,
		 u32 pkts, u32 intvl),
	TP_ARGS(wiphy, netdev, rate, pkts, intvl),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u32, rate)
		__field(u32, pkts)
		__field(u32, intvl)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->rate = rate;
		__entry->pkts = pkts;
		__entry->intvl = intvl;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", rate: %u, packets: %u, interval: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->rate, __entry->pkts,
		  __entry->intvl)
);

TRACE_EVENT(rdev_disconnect,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u16 reason_code),
	TP_ARGS(wiphy, netdev, reason_code),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u16, reason_code)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->reason_code = reason_code;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", reason code: %u", WIPHY_PR_ARG,
		  NETDEV_PR_ARG, __entry->reason_code)
);

TRACE_EVENT(rdev_join_ibss,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_ibss_params *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		__array(char, ssid, IEEE80211_MAX_SSID_LEN + 1)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, params->bssid);
		memset(__entry->ssid, 0, IEEE80211_MAX_SSID_LEN + 1);
		memcpy(__entry->ssid, params->ssid, params->ssid_len);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT ", ssid: %s",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid), __entry->ssid)
);

TRACE_EVENT(rdev_join_ocb,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const struct ocb_setup *setup),
	TP_ARGS(wiphy, netdev, setup),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG)
);

TRACE_EVENT(rdev_set_wiphy_params,
	TP_PROTO(struct wiphy *wiphy, u32 changed),
	TP_ARGS(wiphy, changed),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(u32, changed)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->changed = changed;
	),
	TP_printk(WIPHY_PR_FMT ", changed: %u",
		  WIPHY_PR_ARG, __entry->changed)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_get_tx_power,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_set_tx_power,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 enum nl80211_tx_power_setting type, int mbm),
	TP_ARGS(wiphy, wdev, type, mbm),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(enum nl80211_tx_power_setting, type)
		__field(int, mbm)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->type = type;
		__entry->mbm = mbm;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", type: %u, mbm: %d",
		  WIPHY_PR_ARG, WDEV_PR_ARG,__entry->type, __entry->mbm)
);

TRACE_EVENT(rdev_return_int_int,
	TP_PROTO(struct wiphy *wiphy, int func_ret, int func_fill),
	TP_ARGS(wiphy, func_ret, func_fill),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, func_ret)
		__field(int, func_fill)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->func_ret = func_ret;
		__entry->func_fill = func_fill;
	),
	TP_printk(WIPHY_PR_FMT ", function returns: %d, function filled: %d",
		  WIPHY_PR_ARG, __entry->func_ret, __entry->func_fill)
);

#ifdef CONFIG_NL80211_TESTMODE
TRACE_EVENT(rdev_testmode_cmd,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT WDEV_PR_FMT, WIPHY_PR_ARG, WDEV_PR_ARG)
);

TRACE_EVENT(rdev_testmode_dump,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy),
	TP_STRUCT__entry(
		WIPHY_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT, WIPHY_PR_ARG)
);
#endif /* CONFIG_NL80211_TESTMODE */

TRACE_EVENT(rdev_set_bitrate_mask,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const u8 *peer, const struct cfg80211_bitrate_mask *mask),
	TP_ARGS(wiphy, netdev, peer, mask),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", peer: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer))
);

TRACE_EVENT(rdev_mgmt_frame_register,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 u16 frame_type, bool reg),
	TP_ARGS(wiphy, wdev, frame_type, reg),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u16, frame_type)
		__field(bool, reg)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->frame_type = frame_type;
		__entry->reg = reg;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", frame_type: 0x%.2x, reg: %s ",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->frame_type,
		  __entry->reg ? "true" : "false")
);

TRACE_EVENT(rdev_return_int_tx_rx,
	TP_PROTO(struct wiphy *wiphy, int ret, u32 tx, u32 rx),
	TP_ARGS(wiphy, ret, tx, rx),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
		__field(u32, tx)
		__field(u32, rx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->ret = ret;
		__entry->tx = tx;
		__entry->rx = rx;
	),
	TP_printk(WIPHY_PR_FMT ", returned %d, tx: %u, rx: %u",
		  WIPHY_PR_ARG, __entry->ret, __entry->tx, __entry->rx)
);

TRACE_EVENT(rdev_return_void_tx_rx,
	TP_PROTO(struct wiphy *wiphy, u32 tx, u32 tx_max,
		 u32 rx, u32 rx_max),
	TP_ARGS(wiphy, tx, tx_max, rx, rx_max),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(u32, tx)
		__field(u32, tx_max)
		__field(u32, rx)
		__field(u32, rx_max)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->tx = tx;
		__entry->tx_max = tx_max;
		__entry->rx = rx;
		__entry->rx_max = rx_max;
	),
	TP_printk(WIPHY_PR_FMT ", tx: %u, tx_max: %u, rx: %u, rx_max: %u ",
		  WIPHY_PR_ARG, __entry->tx, __entry->tx_max, __entry->rx,
		  __entry->rx_max)
);

DECLARE_EVENT_CLASS(tx_rx_evt,
	TP_PROTO(struct wiphy *wiphy, u32 tx, u32 rx),
	TP_ARGS(wiphy, rx, tx),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(u32, tx)
		__field(u32, rx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->tx = tx;
		__entry->rx = rx;
	),
	TP_printk(WIPHY_PR_FMT ", tx: %u, rx: %u ",
		  WIPHY_PR_ARG, __entry->tx, __entry->rx)
);

DEFINE_EVENT(tx_rx_evt, rdev_set_antenna,
	TP_PROTO(struct wiphy *wiphy, u32 tx, u32 rx),
	TP_ARGS(wiphy, rx, tx)
);

TRACE_EVENT(rdev_sched_scan_start,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_sched_scan_request *request),
	TP_ARGS(wiphy, netdev, request),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG)
);

TRACE_EVENT(rdev_tdls_mgmt,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u8 *peer, u8 action_code, u8 dialog_token,
		 u16 status_code, u32 peer_capability,
		 bool initiator, const u8 *buf, size_t len),
	TP_ARGS(wiphy, netdev, peer, action_code, dialog_token, status_code,
		peer_capability, initiator, buf, len),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(u8, action_code)
		__field(u8, dialog_token)
		__field(u16, status_code)
		__field(u32, peer_capability)
		__field(bool, initiator)
		__dynamic_array(u8, buf, len)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->action_code = action_code;
		__entry->dialog_token = dialog_token;
		__entry->status_code = status_code;
		__entry->peer_capability = peer_capability;
		__entry->initiator = initiator;
		memcpy(__get_dynamic_array(buf), buf, len);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT ", action_code: %u, "
		  "dialog_token: %u, status_code: %u, peer_capability: %u "
		  "initiator: %s buf: %#.2x ",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer),
		  __entry->action_code, __entry->dialog_token,
		  __entry->status_code, __entry->peer_capability,
		  BOOL_TO_STR(__entry->initiator),
		  ((u8 *)__get_dynamic_array(buf))[0])
);

TRACE_EVENT(rdev_dump_survey,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, int idx),
	TP_ARGS(wiphy, netdev, idx),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(int, idx)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->idx = idx;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", index: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->idx)
);

TRACE_EVENT(rdev_return_int_survey_info,
	TP_PROTO(struct wiphy *wiphy, int ret, struct survey_info *info),
	TP_ARGS(wiphy, ret, info),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_ENTRY
		__field(int, ret)
		__field(u64, time)
		__field(u64, time_busy)
		__field(u64, time_ext_busy)
		__field(u64, time_rx)
		__field(u64, time_tx)
		__field(u64, time_scan)
		__field(u32, filled)
		__field(s8, noise)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_ASSIGN(info->channel);
		__entry->ret = ret;
		__entry->time = info->time;
		__entry->time_busy = info->time_busy;
		__entry->time_ext_busy = info->time_ext_busy;
		__entry->time_rx = info->time_rx;
		__entry->time_tx = info->time_tx;
		__entry->time_scan = info->time_scan;
		__entry->filled = info->filled;
		__entry->noise = info->noise;
	),
	TP_printk(WIPHY_PR_FMT ", returned: %d, " CHAN_PR_FMT
		  ", channel time: %llu, channel time busy: %llu, "
		  "channel time extension busy: %llu, channel time rx: %llu, "
		  "channel time tx: %llu, scan time: %llu, filled: %u, noise: %d",
		  WIPHY_PR_ARG, __entry->ret, CHAN_PR_ARG,
		  __entry->time, __entry->time_busy,
		  __entry->time_ext_busy, __entry->time_rx,
		  __entry->time_tx, __entry->time_scan,
		  __entry->filled, __entry->noise)
);

TRACE_EVENT(rdev_tdls_oper,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u8 *peer, enum nl80211_tdls_operation oper),
	TP_ARGS(wiphy, netdev, peer, oper),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(enum nl80211_tdls_operation, oper)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->oper = oper;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT ", oper: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer), __entry->oper)
);

DECLARE_EVENT_CLASS(rdev_pmksa,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_pmksa *pmksa),
	TP_ARGS(wiphy, netdev, pmksa),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, pmksa->bssid);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", bssid: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(bssid))
);

TRACE_EVENT(rdev_probe_client,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const u8 *peer),
	TP_ARGS(wiphy, netdev, peer),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer))
);

DEFINE_EVENT(rdev_pmksa, rdev_set_pmksa,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_pmksa *pmksa),
	TP_ARGS(wiphy, netdev, pmksa)
);

DEFINE_EVENT(rdev_pmksa, rdev_del_pmksa,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_pmksa *pmksa),
	TP_ARGS(wiphy, netdev, pmksa)
);

TRACE_EVENT(rdev_remain_on_channel,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 struct ieee80211_channel *chan,
		 unsigned int duration),
	TP_ARGS(wiphy, wdev, chan, duration),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		CHAN_ENTRY
		__field(unsigned int, duration)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		CHAN_ASSIGN(chan);
		__entry->duration = duration;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", " CHAN_PR_FMT ", duration: %u",
		  WIPHY_PR_ARG, WDEV_PR_ARG, CHAN_PR_ARG, __entry->duration)
);

TRACE_EVENT(rdev_return_int_cookie,
	TP_PROTO(struct wiphy *wiphy, int ret, u64 cookie),
	TP_ARGS(wiphy, ret, cookie),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
		__field(u64, cookie)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->ret = ret;
		__entry->cookie = cookie;
	),
	TP_printk(WIPHY_PR_FMT ", returned %d, cookie: %llu",
		  WIPHY_PR_ARG, __entry->ret, __entry->cookie)
);

TRACE_EVENT(rdev_cancel_remain_on_channel,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev, u64 cookie),
	TP_ARGS(wiphy, wdev, cookie),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u64, cookie)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->cookie = cookie;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", cookie: %llu",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->cookie)
);

TRACE_EVENT(rdev_mgmt_tx,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 struct cfg80211_mgmt_tx_params *params),
	TP_ARGS(wiphy, wdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		CHAN_ENTRY
		__field(bool, offchan)
		__field(unsigned int, wait)
		__field(bool, no_cck)
		__field(bool, dont_wait_for_ack)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		CHAN_ASSIGN(params->chan);
		__entry->offchan = params->offchan;
		__entry->wait = params->wait;
		__entry->no_cck = params->no_cck;
		__entry->dont_wait_for_ack = params->dont_wait_for_ack;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", " CHAN_PR_FMT ", offchan: %s,"
		  " wait: %u, no cck: %s, dont wait for ack: %s",
		  WIPHY_PR_ARG, WDEV_PR_ARG, CHAN_PR_ARG,
		  BOOL_TO_STR(__entry->offchan), __entry->wait,
		  BOOL_TO_STR(__entry->no_cck),
		  BOOL_TO_STR(__entry->dont_wait_for_ack))
);

TRACE_EVENT(rdev_set_noack_map,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u16 noack_map),
	TP_ARGS(wiphy, netdev, noack_map),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u16, noack_map)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->noack_map = noack_map;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", noack_map: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->noack_map)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_get_channel,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_return_chandef,
	TP_PROTO(struct wiphy *wiphy, int ret,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, ret, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, ret)
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		if (ret == 0)
			CHAN_DEF_ASSIGN(chandef);
		else
			CHAN_DEF_ASSIGN((struct cfg80211_chan_def *)NULL);
		__entry->ret = ret;
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_DEF_PR_FMT ", ret: %d",
		  WIPHY_PR_ARG, CHAN_DEF_PR_ARG, __entry->ret)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_start_p2p_device,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_stop_p2p_device,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_start_nan,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 struct cfg80211_nan_conf *conf),
	TP_ARGS(wiphy, wdev, conf),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u8, master_pref)
		__field(u8, dual);
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->master_pref = conf->master_pref;
		__entry->dual = conf->dual;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT
		  ", master preference: %u, dual: %d",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->master_pref,
		  __entry->dual)
);

TRACE_EVENT(rdev_nan_change_conf,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 struct cfg80211_nan_conf *conf, u32 changes),
	TP_ARGS(wiphy, wdev, conf, changes),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u8, master_pref)
		__field(u8, dual);
		__field(u32, changes);
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->master_pref = conf->master_pref;
		__entry->dual = conf->dual;
		__entry->changes = changes;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT
		  ", master preference: %u, dual: %d, changes: %x",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->master_pref,
		  __entry->dual, __entry->changes)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_stop_nan,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_add_nan_func,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 const struct cfg80211_nan_func *func),
	TP_ARGS(wiphy, wdev, func),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u8, func_type)
		__field(u64, cookie)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->func_type = func->type;
		__entry->cookie = func->cookie
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", type=%u, cookie=%llu",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->func_type,
		  __entry->cookie)
);

TRACE_EVENT(rdev_del_nan_func,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 u64 cookie),
	TP_ARGS(wiphy, wdev, cookie),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u64, cookie)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->cookie = cookie;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", cookie=%llu",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->cookie)
);

TRACE_EVENT(rdev_set_mac_acl,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_acl_data *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u32, acl_policy)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->acl_policy = params->acl_policy;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", acl policy: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->acl_policy)
);

TRACE_EVENT(rdev_update_ft_ies,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_update_ft_ies_params *ftie),
	TP_ARGS(wiphy, netdev, ftie),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(u16, md)
		__dynamic_array(u8, ie, ftie->ie_len)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->md = ftie->md;
		memcpy(__get_dynamic_array(ie), ftie->ie, ftie->ie_len);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", md: 0x%x",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->md)
);

TRACE_EVENT(rdev_crit_proto_start,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 enum nl80211_crit_proto_id protocol, u16 duration),
	TP_ARGS(wiphy, wdev, protocol, duration),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(u16, proto)
		__field(u16, duration)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->proto = protocol;
		__entry->duration = duration;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT ", proto=%x, duration=%u",
		  WIPHY_PR_ARG, WDEV_PR_ARG, __entry->proto, __entry->duration)
);

TRACE_EVENT(rdev_crit_proto_stop,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT,
		  WIPHY_PR_ARG, WDEV_PR_ARG)
);

TRACE_EVENT(rdev_channel_switch,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_csa_settings *params),
	TP_ARGS(wiphy, netdev, params),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
		__field(bool, radar_required)
		__field(bool, block_tx)
		__field(u8, count)
		__dynamic_array(u16, bcn_ofs, params->n_counter_offsets_beacon)
		__dynamic_array(u16, pres_ofs, params->n_counter_offsets_presp)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(&params->chandef);
		__entry->radar_required = params->radar_required;
		__entry->block_tx = params->block_tx;
		__entry->count = params->count;
		memcpy(__get_dynamic_array(bcn_ofs),
		       params->counter_offsets_beacon,
		       params->n_counter_offsets_beacon * sizeof(u16));

		/* probe response offsets are optional */
		if (params->n_counter_offsets_presp)
			memcpy(__get_dynamic_array(pres_ofs),
			       params->counter_offsets_presp,
			       params->n_counter_offsets_presp * sizeof(u16));
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " CHAN_DEF_PR_FMT
		  ", block_tx: %d, count: %u, radar_required: %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, CHAN_DEF_PR_ARG,
		  __entry->block_tx, __entry->count, __entry->radar_required)
);

TRACE_EVENT(rdev_set_qos_map,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_qos_map *qos_map),
	TP_ARGS(wiphy, netdev, qos_map),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		QOS_MAP_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		QOS_MAP_ASSIGN(qos_map);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", num_des: %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, __entry->num_des)
);

TRACE_EVENT(rdev_set_ap_chanwidth,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, netdev, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " CHAN_DEF_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(rdev_add_tx_ts,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u8 tsid, const u8 *peer, u8 user_prio, u16 admitted_time),
	TP_ARGS(wiphy, netdev, tsid, peer, user_prio, admitted_time),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(u8, tsid)
		__field(u8, user_prio)
		__field(u16, admitted_time)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->tsid = tsid;
		__entry->user_prio = user_prio;
		__entry->admitted_time = admitted_time;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT ", TSID %d, UP %d, time %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer),
		  __entry->tsid, __entry->user_prio, __entry->admitted_time)
);

TRACE_EVENT(rdev_del_tx_ts,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 u8 tsid, const u8 *peer),
	TP_ARGS(wiphy, netdev, tsid, peer),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(u8, tsid)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->tsid = tsid;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT ", TSID %d",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer), __entry->tsid)
);

TRACE_EVENT(rdev_tdls_channel_switch,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const u8 *addr, u8 oper_class,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, netdev, addr, oper_class, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(addr)
		__field(u8, oper_class)
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(addr, addr);
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT
		  " oper class %d, " CHAN_DEF_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(addr),
		  __entry->oper_class, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(rdev_tdls_cancel_channel_switch,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const u8 *addr),
	TP_ARGS(wiphy, netdev, addr),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(addr)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(addr, addr);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(addr))
);

/*************************************************************
 *	     cfg80211 exported functions traces		     *
 *************************************************************/

TRACE_EVENT(cfg80211_return_bool,
	TP_PROTO(bool ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(bool, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("returned %s", BOOL_TO_STR(__entry->ret))
);

DECLARE_EVENT_CLASS(cfg80211_netdev_mac_evt,
	TP_PROTO(struct net_device *netdev, const u8 *macaddr),
	TP_ARGS(netdev, macaddr),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(macaddr)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(macaddr, macaddr);
	),
	TP_printk(NETDEV_PR_FMT ", mac: " MAC_PR_FMT,
		  NETDEV_PR_ARG, MAC_PR_ARG(macaddr))
);

DEFINE_EVENT(cfg80211_netdev_mac_evt, cfg80211_notify_new_peer_candidate,
	TP_PROTO(struct net_device *netdev, const u8 *macaddr),
	TP_ARGS(netdev, macaddr)
);

DECLARE_EVENT_CLASS(netdev_evt_only,
	TP_PROTO(struct net_device *netdev),
	TP_ARGS(netdev),
	TP_STRUCT__entry(
		NETDEV_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
	),
	TP_printk(NETDEV_PR_FMT , NETDEV_PR_ARG)
);

DEFINE_EVENT(netdev_evt_only, cfg80211_send_rx_auth,
	TP_PROTO(struct net_device *netdev),
	TP_ARGS(netdev)
);

TRACE_EVENT(cfg80211_send_rx_assoc,
	TP_PROTO(struct net_device *netdev, struct cfg80211_bss *bss),
	TP_ARGS(netdev, bss),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		CHAN_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, bss->bssid);
		CHAN_ASSIGN(bss->channel);
	),
	TP_printk(NETDEV_PR_FMT ", " MAC_PR_FMT ", " CHAN_PR_FMT,
		  NETDEV_PR_ARG, MAC_PR_ARG(bssid), CHAN_PR_ARG)
);

DECLARE_EVENT_CLASS(netdev_frame_event,
	TP_PROTO(struct net_device *netdev, const u8 *buf, int len),
	TP_ARGS(netdev, buf, len),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		__dynamic_array(u8, frame, len)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		memcpy(__get_dynamic_array(frame), buf, len);
	),
	TP_printk(NETDEV_PR_FMT ", ftype:0x%.2x",
		  NETDEV_PR_ARG,
		  le16_to_cpup((__le16 *)__get_dynamic_array(frame)))
);

DEFINE_EVENT(netdev_frame_event, cfg80211_rx_unprot_mlme_mgmt,
	TP_PROTO(struct net_device *netdev, const u8 *buf, int len),
	TP_ARGS(netdev, buf, len)
);

DEFINE_EVENT(netdev_frame_event, cfg80211_rx_mlme_mgmt,
	TP_PROTO(struct net_device *netdev, const u8 *buf, int len),
	TP_ARGS(netdev, buf, len)
);

TRACE_EVENT(cfg80211_tx_mlme_mgmt,
	TP_PROTO(struct net_device *netdev, const u8 *buf, int len),
	TP_ARGS(netdev, buf, len),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		__dynamic_array(u8, frame, len)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		memcpy(__get_dynamic_array(frame), buf, len);
	),
	TP_printk(NETDEV_PR_FMT ", ftype:0x%.2x",
		  NETDEV_PR_ARG,
		  le16_to_cpup((__le16 *)__get_dynamic_array(frame)))
);

DECLARE_EVENT_CLASS(netdev_mac_evt,
	TP_PROTO(struct net_device *netdev, const u8 *mac),
	TP_ARGS(netdev, mac),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(mac)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(mac, mac)
	),
	TP_printk(NETDEV_PR_FMT ", mac: " MAC_PR_FMT,
		  NETDEV_PR_ARG, MAC_PR_ARG(mac))
);

DEFINE_EVENT(netdev_mac_evt, cfg80211_send_auth_timeout,
	TP_PROTO(struct net_device *netdev, const u8 *mac),
	TP_ARGS(netdev, mac)
);

DEFINE_EVENT(netdev_mac_evt, cfg80211_send_assoc_timeout,
	TP_PROTO(struct net_device *netdev, const u8 *mac),
	TP_ARGS(netdev, mac)
);

TRACE_EVENT(cfg80211_michael_mic_failure,
	TP_PROTO(struct net_device *netdev, const u8 *addr,
		 enum nl80211_key_type key_type, int key_id, const u8 *tsc),
	TP_ARGS(netdev, addr, key_type, key_id, tsc),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(addr)
		__field(enum nl80211_key_type, key_type)
		__field(int, key_id)
		__array(u8, tsc, 6)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(addr, addr);
		__entry->key_type = key_type;
		__entry->key_id = key_id;
		if (tsc)
			memcpy(__entry->tsc, tsc, 6);
	),
	TP_printk(NETDEV_PR_FMT ", " MAC_PR_FMT ", key type: %d, key id: %d, tsc: %pm",
		  NETDEV_PR_ARG, MAC_PR_ARG(addr), __entry->key_type,
		  __entry->key_id, __entry->tsc)
);

TRACE_EVENT(cfg80211_ready_on_channel,
	TP_PROTO(struct wireless_dev *wdev, u64 cookie,
		 struct ieee80211_channel *chan,
		 unsigned int duration),
	TP_ARGS(wdev, cookie, chan, duration),
	TP_STRUCT__entry(
		WDEV_ENTRY
		__field(u64, cookie)
		CHAN_ENTRY
		__field(unsigned int, duration)
	),
	TP_fast_assign(
		WDEV_ASSIGN;
		__entry->cookie = cookie;
		CHAN_ASSIGN(chan);
		__entry->duration = duration;
	),
	TP_printk(WDEV_PR_FMT ", cookie: %llu, " CHAN_PR_FMT ", duration: %u",
		  WDEV_PR_ARG, __entry->cookie, CHAN_PR_ARG,
		  __entry->duration)
);

TRACE_EVENT(cfg80211_ready_on_channel_expired,
	TP_PROTO(struct wireless_dev *wdev, u64 cookie,
		 struct ieee80211_channel *chan),
	TP_ARGS(wdev, cookie, chan),
	TP_STRUCT__entry(
		WDEV_ENTRY
		__field(u64, cookie)
		CHAN_ENTRY
	),
	TP_fast_assign(
		WDEV_ASSIGN;
		__entry->cookie = cookie;
		CHAN_ASSIGN(chan);
	),
	TP_printk(WDEV_PR_FMT ", cookie: %llu, " CHAN_PR_FMT,
		  WDEV_PR_ARG, __entry->cookie, CHAN_PR_ARG)
);

TRACE_EVENT(cfg80211_new_sta,
	TP_PROTO(struct net_device *netdev, const u8 *mac_addr,
		 struct station_info *sinfo),
	TP_ARGS(netdev, mac_addr, sinfo),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(mac_addr)
		SINFO_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(mac_addr, mac_addr);
		SINFO_ASSIGN;
	),
	TP_printk(NETDEV_PR_FMT ", " MAC_PR_FMT,
		  NETDEV_PR_ARG, MAC_PR_ARG(mac_addr))
);

DEFINE_EVENT(cfg80211_netdev_mac_evt, cfg80211_del_sta,
	TP_PROTO(struct net_device *netdev, const u8 *macaddr),
	TP_ARGS(netdev, macaddr)
);

TRACE_EVENT(cfg80211_rx_mgmt,
	TP_PROTO(struct wireless_dev *wdev, int freq, int sig_mbm),
	TP_ARGS(wdev, freq, sig_mbm),
	TP_STRUCT__entry(
		WDEV_ENTRY
		__field(int, freq)
		__field(int, sig_mbm)
	),
	TP_fast_assign(
		WDEV_ASSIGN;
		__entry->freq = freq;
		__entry->sig_mbm = sig_mbm;
	),
	TP_printk(WDEV_PR_FMT ", freq: %d, sig mbm: %d",
		  WDEV_PR_ARG, __entry->freq, __entry->sig_mbm)
);

TRACE_EVENT(cfg80211_mgmt_tx_status,
	TP_PROTO(struct wireless_dev *wdev, u64 cookie, bool ack),
	TP_ARGS(wdev, cookie, ack),
	TP_STRUCT__entry(
		WDEV_ENTRY
		__field(u64, cookie)
		__field(bool, ack)
	),
	TP_fast_assign(
		WDEV_ASSIGN;
		__entry->cookie = cookie;
		__entry->ack = ack;
	),
	TP_printk(WDEV_PR_FMT", cookie: %llu, ack: %s",
		  WDEV_PR_ARG, __entry->cookie, BOOL_TO_STR(__entry->ack))
);

TRACE_EVENT(cfg80211_cqm_rssi_notify,
	TP_PROTO(struct net_device *netdev,
		 enum nl80211_cqm_rssi_threshold_event rssi_event),
	TP_ARGS(netdev, rssi_event),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		__field(enum nl80211_cqm_rssi_threshold_event, rssi_event)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		__entry->rssi_event = rssi_event;
	),
	TP_printk(NETDEV_PR_FMT ", rssi event: %d",
		  NETDEV_PR_ARG, __entry->rssi_event)
);

TRACE_EVENT(cfg80211_reg_can_beacon,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_chan_def *chandef,
		 enum nl80211_iftype iftype, bool check_no_ir),
	TP_ARGS(wiphy, chandef, iftype, check_no_ir),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_DEF_ENTRY
		__field(enum nl80211_iftype, iftype)
		__field(bool, check_no_ir)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
		__entry->iftype = iftype;
		__entry->check_no_ir = check_no_ir;
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_DEF_PR_FMT ", iftype=%d check_no_ir=%s",
		  WIPHY_PR_ARG, CHAN_DEF_PR_ARG, __entry->iftype,
		  BOOL_TO_STR(__entry->check_no_ir))
);

TRACE_EVENT(cfg80211_chandef_dfs_required,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_DEF_PR_FMT,
		  WIPHY_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(cfg80211_ch_switch_notify,
	TP_PROTO(struct net_device *netdev,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(netdev, chandef),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(NETDEV_PR_FMT ", " CHAN_DEF_PR_FMT,
		  NETDEV_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(cfg80211_ch_switch_started_notify,
	TP_PROTO(struct net_device *netdev,
		 struct cfg80211_chan_def *chandef),
	TP_ARGS(netdev, chandef),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(NETDEV_PR_FMT ", " CHAN_DEF_PR_FMT,
		  NETDEV_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(cfg80211_radar_event,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_chan_def *chandef),
	TP_ARGS(wiphy, chandef),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_DEF_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_DEF_PR_FMT,
		  WIPHY_PR_ARG, CHAN_DEF_PR_ARG)
);

TRACE_EVENT(cfg80211_cac_event,
	TP_PROTO(struct net_device *netdev, enum nl80211_radar_event evt),
	TP_ARGS(netdev, evt),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		__field(enum nl80211_radar_event, evt)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		__entry->evt = evt;
	),
	TP_printk(NETDEV_PR_FMT ",  event: %d",
		  NETDEV_PR_ARG, __entry->evt)
);

DECLARE_EVENT_CLASS(cfg80211_rx_evt,
	TP_PROTO(struct net_device *netdev, const u8 *addr),
	TP_ARGS(netdev, addr),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(addr)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(addr, addr);
	),
	TP_printk(NETDEV_PR_FMT ", " MAC_PR_FMT, NETDEV_PR_ARG, MAC_PR_ARG(addr))
);

DEFINE_EVENT(cfg80211_rx_evt, cfg80211_rx_spurious_frame,
	TP_PROTO(struct net_device *netdev, const u8 *addr),
	TP_ARGS(netdev, addr)
);

DEFINE_EVENT(cfg80211_rx_evt, cfg80211_rx_unexpected_4addr_frame,
	TP_PROTO(struct net_device *netdev, const u8 *addr),
	TP_ARGS(netdev, addr)
);

TRACE_EVENT(cfg80211_ibss_joined,
	TP_PROTO(struct net_device *netdev, const u8 *bssid,
		 struct ieee80211_channel *channel),
	TP_ARGS(netdev, bssid, channel),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(bssid)
		CHAN_ENTRY
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(bssid, bssid);
		CHAN_ASSIGN(channel);
	),
	TP_printk(NETDEV_PR_FMT ", bssid: " MAC_PR_FMT ", " CHAN_PR_FMT,
		  NETDEV_PR_ARG, MAC_PR_ARG(bssid), CHAN_PR_ARG)
);

TRACE_EVENT(cfg80211_probe_status,
	TP_PROTO(struct net_device *netdev, const u8 *addr, u64 cookie,
		 bool acked),
	TP_ARGS(netdev, addr, cookie, acked),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(addr)
		__field(u64, cookie)
		__field(bool, acked)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(addr, addr);
		__entry->cookie = cookie;
		__entry->acked = acked;
	),
	TP_printk(NETDEV_PR_FMT " addr:" MAC_PR_FMT ", cookie: %llu, acked: %s",
		  NETDEV_PR_ARG, MAC_PR_ARG(addr), __entry->cookie,
		  BOOL_TO_STR(__entry->acked))
);

TRACE_EVENT(cfg80211_cqm_pktloss_notify,
	TP_PROTO(struct net_device *netdev, const u8 *peer, u32 num_packets),
	TP_ARGS(netdev, peer, num_packets),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(u32, num_packets)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->num_packets = num_packets;
	),
	TP_printk(NETDEV_PR_FMT ", peer: " MAC_PR_FMT ", num of lost packets: %u",
		  NETDEV_PR_ARG, MAC_PR_ARG(peer), __entry->num_packets)
);

DEFINE_EVENT(cfg80211_netdev_mac_evt, cfg80211_gtk_rekey_notify,
	TP_PROTO(struct net_device *netdev, const u8 *macaddr),
	TP_ARGS(netdev, macaddr)
);

TRACE_EVENT(cfg80211_pmksa_candidate_notify,
	TP_PROTO(struct net_device *netdev, int index, const u8 *bssid,
		 bool preauth),
	TP_ARGS(netdev, index, bssid, preauth),
	TP_STRUCT__entry(
		NETDEV_ENTRY
		__field(int, index)
		MAC_ENTRY(bssid)
		__field(bool, preauth)
	),
	TP_fast_assign(
		NETDEV_ASSIGN;
		__entry->index = index;
		MAC_ASSIGN(bssid, bssid);
		__entry->preauth = preauth;
	),
	TP_printk(NETDEV_PR_FMT ", index:%d, bssid: " MAC_PR_FMT ", pre auth: %s",
		  NETDEV_PR_ARG, __entry->index, MAC_PR_ARG(bssid),
		  BOOL_TO_STR(__entry->preauth))
);

TRACE_EVENT(cfg80211_report_obss_beacon,
	TP_PROTO(struct wiphy *wiphy, const u8 *frame, size_t len,
		 int freq, int sig_dbm),
	TP_ARGS(wiphy, frame, len, freq, sig_dbm),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, freq)
		__field(int, sig_dbm)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->freq = freq;
		__entry->sig_dbm = sig_dbm;
	),
	TP_printk(WIPHY_PR_FMT ", freq: %d, sig_dbm: %d",
		  WIPHY_PR_ARG, __entry->freq, __entry->sig_dbm)
);

TRACE_EVENT(cfg80211_tdls_oper_request,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev, const u8 *peer,
		 enum nl80211_tdls_operation oper, u16 reason_code),
	TP_ARGS(wiphy, netdev, peer, oper, reason_code),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		MAC_ENTRY(peer)
		__field(enum nl80211_tdls_operation, oper)
		__field(u16, reason_code)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		MAC_ASSIGN(peer, peer);
		__entry->oper = oper;
		__entry->reason_code = reason_code;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", peer: " MAC_PR_FMT ", oper: %d, reason_code %u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(peer), __entry->oper,
		  __entry->reason_code)
	);

TRACE_EVENT(cfg80211_scan_done,
	TP_PROTO(struct cfg80211_scan_request *request,
		 struct cfg80211_scan_info *info),
	TP_ARGS(request, info),
	TP_STRUCT__entry(
		__field(u32, n_channels)
		__dynamic_array(u8, ie, request ? request->ie_len : 0)
		__array(u32, rates, NUM_NL80211_BANDS)
		__field(u32, wdev_id)
		MAC_ENTRY(wiphy_mac)
		__field(bool, no_cck)
		__field(bool, aborted)
		__field(u64, scan_start_tsf)
		MAC_ENTRY(tsf_bssid)
	),
	TP_fast_assign(
		if (request) {
			memcpy(__get_dynamic_array(ie), request->ie,
			       request->ie_len);
			memcpy(__entry->rates, request->rates,
			       NUM_NL80211_BANDS);
			__entry->wdev_id = request->wdev ?
					request->wdev->identifier : 0;
			if (request->wiphy)
				MAC_ASSIGN(wiphy_mac,
					   request->wiphy->perm_addr);
			__entry->no_cck = request->no_cck;
		}
		if (info) {
			__entry->aborted = info->aborted;
			__entry->scan_start_tsf = info->scan_start_tsf;
			MAC_ASSIGN(tsf_bssid, info->tsf_bssid);
		}
	),
	TP_printk("aborted: %s, scan start (TSF): %llu, tsf_bssid: " MAC_PR_FMT,
		  BOOL_TO_STR(__entry->aborted),
		  (unsigned long long)__entry->scan_start_tsf,
		  MAC_PR_ARG(tsf_bssid))
);

DEFINE_EVENT(wiphy_only_evt, cfg80211_sched_scan_results,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

DEFINE_EVENT(wiphy_only_evt, cfg80211_sched_scan_stopped,
	TP_PROTO(struct wiphy *wiphy),
	TP_ARGS(wiphy)
);

TRACE_EVENT(cfg80211_get_bss,
	TP_PROTO(struct wiphy *wiphy, struct ieee80211_channel *channel,
		 const u8 *bssid, const u8 *ssid, size_t ssid_len,
		 enum ieee80211_bss_type bss_type,
		 enum ieee80211_privacy privacy),
	TP_ARGS(wiphy, channel, bssid, ssid, ssid_len, bss_type, privacy),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_ENTRY
		MAC_ENTRY(bssid)
		__dynamic_array(u8, ssid, ssid_len)
		__field(enum ieee80211_bss_type, bss_type)
		__field(enum ieee80211_privacy, privacy)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_ASSIGN(channel);
		MAC_ASSIGN(bssid, bssid);
		memcpy(__get_dynamic_array(ssid), ssid, ssid_len);
		__entry->bss_type = bss_type;
		__entry->privacy = privacy;
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_PR_FMT ", " MAC_PR_FMT
		  ", buf: %#.2x, bss_type: %d, privacy: %d",
		  WIPHY_PR_ARG, CHAN_PR_ARG, MAC_PR_ARG(bssid),
		  ((u8 *)__get_dynamic_array(ssid))[0], __entry->bss_type,
		  __entry->privacy)
);

TRACE_EVENT(cfg80211_inform_bss_frame,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_inform_bss *data,
		 struct ieee80211_mgmt *mgmt, size_t len),
	TP_ARGS(wiphy, data, mgmt, len),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		CHAN_ENTRY
		__field(enum nl80211_bss_scan_width, scan_width)
		__dynamic_array(u8, mgmt, len)
		__field(s32, signal)
		__field(u64, ts_boottime)
		__field(u64, parent_tsf)
		MAC_ENTRY(parent_bssid)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		CHAN_ASSIGN(data->chan);
		__entry->scan_width = data->scan_width;
		if (mgmt)
			memcpy(__get_dynamic_array(mgmt), mgmt, len);
		__entry->signal = data->signal;
		__entry->ts_boottime = data->boottime_ns;
		__entry->parent_tsf = data->parent_tsf;
		MAC_ASSIGN(parent_bssid, data->parent_bssid);
	),
	TP_printk(WIPHY_PR_FMT ", " CHAN_PR_FMT
		  "(scan_width: %d) signal: %d, tsb:%llu, detect_tsf:%llu, tsf_bssid: "
		  MAC_PR_FMT, WIPHY_PR_ARG, CHAN_PR_ARG, __entry->scan_width,
		  __entry->signal, (unsigned long long)__entry->ts_boottime,
		  (unsigned long long)__entry->parent_tsf,
		  MAC_PR_ARG(parent_bssid))
);

DECLARE_EVENT_CLASS(cfg80211_bss_evt,
	TP_PROTO(struct cfg80211_bss *pub),
	TP_ARGS(pub),
	TP_STRUCT__entry(
		MAC_ENTRY(bssid)
		CHAN_ENTRY
	),
	TP_fast_assign(
		MAC_ASSIGN(bssid, pub->bssid);
		CHAN_ASSIGN(pub->channel);
	),
	TP_printk(MAC_PR_FMT ", " CHAN_PR_FMT, MAC_PR_ARG(bssid), CHAN_PR_ARG)
);

DEFINE_EVENT(cfg80211_bss_evt, cfg80211_return_bss,
	TP_PROTO(struct cfg80211_bss *pub),
	TP_ARGS(pub)
);

TRACE_EVENT(cfg80211_return_uint,
	TP_PROTO(unsigned int ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(unsigned int, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("ret: %d", __entry->ret)
);

TRACE_EVENT(cfg80211_return_u32,
	TP_PROTO(u32 ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(u32, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("ret: %u", __entry->ret)
);

TRACE_EVENT(cfg80211_report_wowlan_wakeup,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev,
		 struct cfg80211_wowlan_wakeup *wakeup),
	TP_ARGS(wiphy, wdev, wakeup),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
		__field(bool, non_wireless)
		__field(bool, disconnect)
		__field(bool, magic_pkt)
		__field(bool, gtk_rekey_failure)
		__field(bool, eap_identity_req)
		__field(bool, four_way_handshake)
		__field(bool, rfkill_release)
		__field(s32, pattern_idx)
		__field(u32, packet_len)
		__dynamic_array(u8, packet,
				wakeup ? wakeup->packet_present_len : 0)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
		__entry->non_wireless = !wakeup;
		__entry->disconnect = wakeup ? wakeup->disconnect : false;
		__entry->magic_pkt = wakeup ? wakeup->magic_pkt : false;
		__entry->gtk_rekey_failure = wakeup ? wakeup->gtk_rekey_failure : false;
		__entry->eap_identity_req = wakeup ? wakeup->eap_identity_req : false;
		__entry->four_way_handshake = wakeup ? wakeup->four_way_handshake : false;
		__entry->rfkill_release = wakeup ? wakeup->rfkill_release : false;
		__entry->pattern_idx = wakeup ? wakeup->pattern_idx : false;
		__entry->packet_len = wakeup ? wakeup->packet_len : false;
		if (wakeup && wakeup->packet && wakeup->packet_present_len)
			memcpy(__get_dynamic_array(packet), wakeup->packet,
			       wakeup->packet_present_len);
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT, WIPHY_PR_ARG, WDEV_PR_ARG)
);

TRACE_EVENT(cfg80211_ft_event,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_ft_event_params *ft_event),
	TP_ARGS(wiphy, netdev, ft_event),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__dynamic_array(u8, ies, ft_event->ies_len)
		MAC_ENTRY(target_ap)
		__dynamic_array(u8, ric_ies, ft_event->ric_ies_len)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		if (ft_event->ies)
			memcpy(__get_dynamic_array(ies), ft_event->ies,
			       ft_event->ies_len);
		MAC_ASSIGN(target_ap, ft_event->target_ap);
		if (ft_event->ric_ies)
			memcpy(__get_dynamic_array(ric_ies), ft_event->ric_ies,
			       ft_event->ric_ies_len);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", target_ap: " MAC_PR_FMT,
		  WIPHY_PR_ARG, NETDEV_PR_ARG, MAC_PR_ARG(target_ap))
);

TRACE_EVENT(cfg80211_stop_iface,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		WDEV_ENTRY
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		WDEV_ASSIGN;
	),
	TP_printk(WIPHY_PR_FMT ", " WDEV_PR_FMT,
		  WIPHY_PR_ARG, WDEV_PR_ARG)
);

TRACE_EVENT(rdev_start_radar_detection,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 struct cfg80211_chan_def *chandef,
		 u32 cac_time_ms),
	TP_ARGS(wiphy, netdev, chandef, cac_time_ms),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		CHAN_DEF_ENTRY
		__field(u32, cac_time_ms)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		CHAN_DEF_ASSIGN(chandef);
		__entry->cac_time_ms = cac_time_ms;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", " CHAN_DEF_PR_FMT
		  ", cac_time_ms=%u",
		  WIPHY_PR_ARG, NETDEV_PR_ARG, CHAN_DEF_PR_ARG,
		  __entry->cac_time_ms)
);

TRACE_EVENT(rdev_set_mcast_rate,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 int mcast_rate[NUM_NL80211_BANDS]),
	TP_ARGS(wiphy, netdev, mcast_rate),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__array(int, mcast_rate, NUM_NL80211_BANDS)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		memcpy(__entry->mcast_rate, mcast_rate,
		       sizeof(int) * NUM_NL80211_BANDS);
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", "
		  "mcast_rates [2.4GHz=0x%x, 5.2GHz=0x%x, 60GHz=0x%x]",
		  WIPHY_PR_ARG, NETDEV_PR_ARG,
		  __entry->mcast_rate[NL80211_BAND_2GHZ],
		  __entry->mcast_rate[NL80211_BAND_5GHZ],
		  __entry->mcast_rate[NL80211_BAND_60GHZ])
);

TRACE_EVENT(rdev_set_coalesce,
	TP_PROTO(struct wiphy *wiphy, struct cfg80211_coalesce *coalesce),
	TP_ARGS(wiphy, coalesce),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		__field(int, n_rules)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		__entry->n_rules = coalesce ? coalesce->n_rules : 0;
	),
	TP_printk(WIPHY_PR_FMT ", n_rules=%d",
		  WIPHY_PR_ARG, __entry->n_rules)
);

DEFINE_EVENT(wiphy_wdev_evt, rdev_abort_scan,
	TP_PROTO(struct wiphy *wiphy, struct wireless_dev *wdev),
	TP_ARGS(wiphy, wdev)
);

TRACE_EVENT(rdev_set_multicast_to_unicast,
	TP_PROTO(struct wiphy *wiphy, struct net_device *netdev,
		 const bool enabled),
	TP_ARGS(wiphy, netdev, enabled),
	TP_STRUCT__entry(
		WIPHY_ENTRY
		NETDEV_ENTRY
		__field(bool, enabled)
	),
	TP_fast_assign(
		WIPHY_ASSIGN;
		NETDEV_ASSIGN;
		__entry->enabled = enabled;
	),
	TP_printk(WIPHY_PR_FMT ", " NETDEV_PR_FMT ", unicast: %s",
		  WIPHY_PR_ARG, NETDEV_PR_ARG,
		  BOOL_TO_STR(__entry->enabled))
);
#endif /* !__RDEV_OPS_TRACE || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
