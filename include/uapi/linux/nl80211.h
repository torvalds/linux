#ifndef __LINUX_NL80211_H
#define __LINUX_NL80211_H
/*
 * 802.11 netlink interface public header
 *
 * Copyright 2006-2010 Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2008 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2008 Luis Carlos Cobo <luisca@cozybit.com>
 * Copyright 2008 Michael Buesch <m@bues.ch>
 * Copyright 2008, 2009 Luis R. Rodriguez <lrodriguez@atheros.com>
 * Copyright 2008 Jouni Malinen <jouni.malinen@atheros.com>
 * Copyright 2008 Colin McCabe <colin@cozybit.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/types.h>

/**
 * DOC: Station handling
 *
 * Stations are added per interface, but a special case exists with VLAN
 * interfaces. When a station is bound to an AP interface, it may be moved
 * into a VLAN identified by a VLAN interface index (%NL80211_ATTR_STA_VLAN).
 * The station is still assumed to belong to the AP interface it was added
 * to.
 *
 * Station handling varies per interface type and depending on the driver's
 * capabilities.
 *
 * For drivers supporting TDLS with external setup (WIPHY_FLAG_SUPPORTS_TDLS
 * and WIPHY_FLAG_TDLS_EXTERNAL_SETUP), the station lifetime is as follows:
 *  - a setup station entry is added, not yet authorized, without any rate
 *    or capability information, this just exists to avoid race conditions
 *  - when the TDLS setup is done, a single NL80211_CMD_SET_STATION is valid
 *    to add rate and capability information to the station and at the same
 *    time mark it authorized.
 *  - %NL80211_TDLS_ENABLE_LINK is then used
 *  - after this, the only valid operation is to remove it by tearing down
 *    the TDLS link (%NL80211_TDLS_DISABLE_LINK)
 *
 * TODO: need more info for other interface types
 */

/**
 * DOC: Frame transmission/registration support
 *
 * Frame transmission and registration support exists to allow userspace
 * management entities such as wpa_supplicant react to management frames
 * that are not being handled by the kernel. This includes, for example,
 * certain classes of action frames that cannot be handled in the kernel
 * for various reasons.
 *
 * Frame registration is done on a per-interface basis and registrations
 * cannot be removed other than by closing the socket. It is possible to
 * specify a registration filter to register, for example, only for a
 * certain type of action frame. In particular with action frames, those
 * that userspace registers for will not be returned as unhandled by the
 * driver, so that the registered application has to take responsibility
 * for doing that.
 *
 * The type of frame that can be registered for is also dependent on the
 * driver and interface type. The frame types are advertised in wiphy
 * attributes so applications know what to expect.
 *
 * NOTE: When an interface changes type while registrations are active,
 *       these registrations are ignored until the interface type is
 *       changed again. This means that changing the interface type can
 *       lead to a situation that couldn't otherwise be produced, but
 *       any such registrations will be dormant in the sense that they
 *       will not be serviced, i.e. they will not receive any frames.
 *
 * Frame transmission allows userspace to send for example the required
 * responses to action frames. It is subject to some sanity checking,
 * but many frames can be transmitted. When a frame was transmitted, its
 * status is indicated to the sending socket.
 *
 * For more technical details, see the corresponding command descriptions
 * below.
 */

/**
 * DOC: Virtual interface / concurrency capabilities
 *
 * Some devices are able to operate with virtual MACs, they can have
 * more than one virtual interface. The capability handling for this
 * is a bit complex though, as there may be a number of restrictions
 * on the types of concurrency that are supported.
 *
 * To start with, each device supports the interface types listed in
 * the %NL80211_ATTR_SUPPORTED_IFTYPES attribute, but by listing the
 * types there no concurrency is implied.
 *
 * Once concurrency is desired, more attributes must be observed:
 * To start with, since some interface types are purely managed in
 * software, like the AP-VLAN type in mac80211 for example, there's
 * an additional list of these, they can be added at any time and
 * are only restricted by some semantic restrictions (e.g. AP-VLAN
 * cannot be added without a corresponding AP interface). This list
 * is exported in the %NL80211_ATTR_SOFTWARE_IFTYPES attribute.
 *
 * Further, the list of supported combinations is exported. This is
 * in the %NL80211_ATTR_INTERFACE_COMBINATIONS attribute. Basically,
 * it exports a list of "groups", and at any point in time the
 * interfaces that are currently active must fall into any one of
 * the advertised groups. Within each group, there are restrictions
 * on the number of interfaces of different types that are supported
 * and also the number of different channels, along with potentially
 * some other restrictions. See &enum nl80211_if_combination_attrs.
 *
 * All together, these attributes define the concurrency of virtual
 * interfaces that a given device supports.
 */

/**
 * enum nl80211_commands - supported nl80211 commands
 *
 * @NL80211_CMD_UNSPEC: unspecified command to catch errors
 *
 * @NL80211_CMD_GET_WIPHY: request information about a wiphy or dump request
 *	to get a list of all present wiphys.
 * @NL80211_CMD_SET_WIPHY: set wiphy parameters, needs %NL80211_ATTR_WIPHY or
 *	%NL80211_ATTR_IFINDEX; can be used to set %NL80211_ATTR_WIPHY_NAME,
 *	%NL80211_ATTR_WIPHY_TXQ_PARAMS, %NL80211_ATTR_WIPHY_FREQ (and the
 *	attributes determining the channel width; this is used for setting
 *	monitor mode channel),  %NL80211_ATTR_WIPHY_RETRY_SHORT,
 *	%NL80211_ATTR_WIPHY_RETRY_LONG, %NL80211_ATTR_WIPHY_FRAG_THRESHOLD,
 *	and/or %NL80211_ATTR_WIPHY_RTS_THRESHOLD.
 *	However, for setting the channel, see %NL80211_CMD_SET_CHANNEL
 *	instead, the support here is for backward compatibility only.
 * @NL80211_CMD_NEW_WIPHY: Newly created wiphy, response to get request
 *	or rename notification. Has attributes %NL80211_ATTR_WIPHY and
 *	%NL80211_ATTR_WIPHY_NAME.
 * @NL80211_CMD_DEL_WIPHY: Wiphy deleted. Has attributes
 *	%NL80211_ATTR_WIPHY and %NL80211_ATTR_WIPHY_NAME.
 *
 * @NL80211_CMD_GET_INTERFACE: Request an interface's configuration;
 *	either a dump request on a %NL80211_ATTR_WIPHY or a specific get
 *	on an %NL80211_ATTR_IFINDEX is supported.
 * @NL80211_CMD_SET_INTERFACE: Set type of a virtual interface, requires
 *	%NL80211_ATTR_IFINDEX and %NL80211_ATTR_IFTYPE.
 * @NL80211_CMD_NEW_INTERFACE: Newly created virtual interface or response
 *	to %NL80211_CMD_GET_INTERFACE. Has %NL80211_ATTR_IFINDEX,
 *	%NL80211_ATTR_WIPHY and %NL80211_ATTR_IFTYPE attributes. Can also
 *	be sent from userspace to request creation of a new virtual interface,
 *	then requires attributes %NL80211_ATTR_WIPHY, %NL80211_ATTR_IFTYPE and
 *	%NL80211_ATTR_IFNAME.
 * @NL80211_CMD_DEL_INTERFACE: Virtual interface was deleted, has attributes
 *	%NL80211_ATTR_IFINDEX and %NL80211_ATTR_WIPHY. Can also be sent from
 *	userspace to request deletion of a virtual interface, then requires
 *	attribute %NL80211_ATTR_IFINDEX.
 *
 * @NL80211_CMD_GET_KEY: Get sequence counter information for a key specified
 *	by %NL80211_ATTR_KEY_IDX and/or %NL80211_ATTR_MAC.
 * @NL80211_CMD_SET_KEY: Set key attributes %NL80211_ATTR_KEY_DEFAULT,
 *	%NL80211_ATTR_KEY_DEFAULT_MGMT, or %NL80211_ATTR_KEY_THRESHOLD.
 * @NL80211_CMD_NEW_KEY: add a key with given %NL80211_ATTR_KEY_DATA,
 *	%NL80211_ATTR_KEY_IDX, %NL80211_ATTR_MAC, %NL80211_ATTR_KEY_CIPHER,
 *	and %NL80211_ATTR_KEY_SEQ attributes.
 * @NL80211_CMD_DEL_KEY: delete a key identified by %NL80211_ATTR_KEY_IDX
 *	or %NL80211_ATTR_MAC.
 *
 * @NL80211_CMD_GET_BEACON: (not used)
 * @NL80211_CMD_SET_BEACON: change the beacon on an access point interface
 *	using the %NL80211_ATTR_BEACON_HEAD and %NL80211_ATTR_BEACON_TAIL
 *	attributes. For drivers that generate the beacon and probe responses
 *	internally, the following attributes must be provided: %NL80211_ATTR_IE,
 *	%NL80211_ATTR_IE_PROBE_RESP and %NL80211_ATTR_IE_ASSOC_RESP.
 * @NL80211_CMD_START_AP: Start AP operation on an AP interface, parameters
 *	are like for %NL80211_CMD_SET_BEACON, and additionally parameters that
 *	do not change are used, these include %NL80211_ATTR_BEACON_INTERVAL,
 *	%NL80211_ATTR_DTIM_PERIOD, %NL80211_ATTR_SSID,
 *	%NL80211_ATTR_HIDDEN_SSID, %NL80211_ATTR_CIPHERS_PAIRWISE,
 *	%NL80211_ATTR_CIPHER_GROUP, %NL80211_ATTR_WPA_VERSIONS,
 *	%NL80211_ATTR_AKM_SUITES, %NL80211_ATTR_PRIVACY,
 *	%NL80211_ATTR_AUTH_TYPE, %NL80211_ATTR_INACTIVITY_TIMEOUT,
 *	%NL80211_ATTR_ACL_POLICY and %NL80211_ATTR_MAC_ADDRS.
 *	The channel to use can be set on the interface or be given using the
 *	%NL80211_ATTR_WIPHY_FREQ and the attributes determining channel width.
 * @NL80211_CMD_NEW_BEACON: old alias for %NL80211_CMD_START_AP
 * @NL80211_CMD_STOP_AP: Stop AP operation on the given interface
 * @NL80211_CMD_DEL_BEACON: old alias for %NL80211_CMD_STOP_AP
 *
 * @NL80211_CMD_GET_STATION: Get station attributes for station identified by
 *	%NL80211_ATTR_MAC on the interface identified by %NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_SET_STATION: Set station attributes for station identified by
 *	%NL80211_ATTR_MAC on the interface identified by %NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_NEW_STATION: Add a station with given attributes to the
 *	the interface identified by %NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_DEL_STATION: Remove a station identified by %NL80211_ATTR_MAC
 *	or, if no MAC address given, all stations, on the interface identified
 *	by %NL80211_ATTR_IFINDEX.
 *
 * @NL80211_CMD_GET_MPATH: Get mesh path attributes for mesh path to
 * 	destination %NL80211_ATTR_MAC on the interface identified by
 * 	%NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_SET_MPATH:  Set mesh path attributes for mesh path to
 * 	destination %NL80211_ATTR_MAC on the interface identified by
 * 	%NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_NEW_MPATH: Create a new mesh path for the destination given by
 *	%NL80211_ATTR_MAC via %NL80211_ATTR_MPATH_NEXT_HOP.
 * @NL80211_CMD_DEL_MPATH: Delete a mesh path to the destination given by
 *	%NL80211_ATTR_MAC.
 * @NL80211_CMD_NEW_PATH: Add a mesh path with given attributes to the
 *	the interface identified by %NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_DEL_PATH: Remove a mesh path identified by %NL80211_ATTR_MAC
 *	or, if no MAC address given, all mesh paths, on the interface identified
 *	by %NL80211_ATTR_IFINDEX.
 * @NL80211_CMD_SET_BSS: Set BSS attributes for BSS identified by
 *	%NL80211_ATTR_IFINDEX.
 *
 * @NL80211_CMD_GET_REG: ask the wireless core to send us its currently set
 * 	regulatory domain.
 * @NL80211_CMD_SET_REG: Set current regulatory domain. CRDA sends this command
 *	after being queried by the kernel. CRDA replies by sending a regulatory
 *	domain structure which consists of %NL80211_ATTR_REG_ALPHA set to our
 *	current alpha2 if it found a match. It also provides
 * 	NL80211_ATTR_REG_RULE_FLAGS, and a set of regulatory rules. Each
 * 	regulatory rule is a nested set of attributes  given by
 * 	%NL80211_ATTR_REG_RULE_FREQ_[START|END] and
 * 	%NL80211_ATTR_FREQ_RANGE_MAX_BW with an attached power rule given by
 * 	%NL80211_ATTR_REG_RULE_POWER_MAX_ANT_GAIN and
 * 	%NL80211_ATTR_REG_RULE_POWER_MAX_EIRP.
 * @NL80211_CMD_REQ_SET_REG: ask the wireless core to set the regulatory domain
 * 	to the specified ISO/IEC 3166-1 alpha2 country code. The core will
 * 	store this as a valid request and then query userspace for it.
 *
 * @NL80211_CMD_GET_MESH_CONFIG: Get mesh networking properties for the
 *	interface identified by %NL80211_ATTR_IFINDEX
 *
 * @NL80211_CMD_SET_MESH_CONFIG: Set mesh networking properties for the
 *      interface identified by %NL80211_ATTR_IFINDEX
 *
 * @NL80211_CMD_SET_MGMT_EXTRA_IE: Set extra IEs for management frames. The
 *	interface is identified with %NL80211_ATTR_IFINDEX and the management
 *	frame subtype with %NL80211_ATTR_MGMT_SUBTYPE. The extra IE data to be
 *	added to the end of the specified management frame is specified with
 *	%NL80211_ATTR_IE. If the command succeeds, the requested data will be
 *	added to all specified management frames generated by
 *	kernel/firmware/driver.
 *	Note: This command has been removed and it is only reserved at this
 *	point to avoid re-using existing command number. The functionality this
 *	command was planned for has been provided with cleaner design with the
 *	option to specify additional IEs in NL80211_CMD_TRIGGER_SCAN,
 *	NL80211_CMD_AUTHENTICATE, NL80211_CMD_ASSOCIATE,
 *	NL80211_CMD_DEAUTHENTICATE, and NL80211_CMD_DISASSOCIATE.
 *
 * @NL80211_CMD_GET_SCAN: get scan results
 * @NL80211_CMD_TRIGGER_SCAN: trigger a new scan with the given parameters
 *	%NL80211_ATTR_TX_NO_CCK_RATE is used to decide whether to send the
 *	probe requests at CCK rate or not.
 * @NL80211_CMD_NEW_SCAN_RESULTS: scan notification (as a reply to
 *	NL80211_CMD_GET_SCAN and on the "scan" multicast group)
 * @NL80211_CMD_SCAN_ABORTED: scan was aborted, for unspecified reasons,
 *	partial scan results may be available
 *
 * @NL80211_CMD_START_SCHED_SCAN: start a scheduled scan at certain
 *	intervals, as specified by %NL80211_ATTR_SCHED_SCAN_INTERVAL.
 *	Like with normal scans, if SSIDs (%NL80211_ATTR_SCAN_SSIDS)
 *	are passed, they are used in the probe requests.  For
 *	broadcast, a broadcast SSID must be passed (ie. an empty
 *	string).  If no SSID is passed, no probe requests are sent and
 *	a passive scan is performed.  %NL80211_ATTR_SCAN_FREQUENCIES,
 *	if passed, define which channels should be scanned; if not
 *	passed, all channels allowed for the current regulatory domain
 *	are used.  Extra IEs can also be passed from the userspace by
 *	using the %NL80211_ATTR_IE attribute.
 * @NL80211_CMD_STOP_SCHED_SCAN: stop a scheduled scan.  Returns -ENOENT
 *	if scheduled scan is not running.
 * @NL80211_CMD_SCHED_SCAN_RESULTS: indicates that there are scheduled scan
 *	results available.
 * @NL80211_CMD_SCHED_SCAN_STOPPED: indicates that the scheduled scan has
 *	stopped.  The driver may issue this event at any time during a
 *	scheduled scan.  One reason for stopping the scan is if the hardware
 *	does not support starting an association or a normal scan while running
 *	a scheduled scan.  This event is also sent when the
 *	%NL80211_CMD_STOP_SCHED_SCAN command is received or when the interface
 *	is brought down while a scheduled scan was running.
 *
 * @NL80211_CMD_GET_SURVEY: get survey resuls, e.g. channel occupation
 *      or noise level
 * @NL80211_CMD_NEW_SURVEY_RESULTS: survey data notification (as a reply to
 *	NL80211_CMD_GET_SURVEY and on the "scan" multicast group)
 *
 * @NL80211_CMD_SET_PMKSA: Add a PMKSA cache entry, using %NL80211_ATTR_MAC
 *	(for the BSSID) and %NL80211_ATTR_PMKID.
 * @NL80211_CMD_DEL_PMKSA: Delete a PMKSA cache entry, using %NL80211_ATTR_MAC
 *	(for the BSSID) and %NL80211_ATTR_PMKID.
 * @NL80211_CMD_FLUSH_PMKSA: Flush all PMKSA cache entries.
 *
 * @NL80211_CMD_REG_CHANGE: indicates to userspace the regulatory domain
 * 	has been changed and provides details of the request information
 * 	that caused the change such as who initiated the regulatory request
 * 	(%NL80211_ATTR_REG_INITIATOR), the wiphy_idx
 * 	(%NL80211_ATTR_REG_ALPHA2) on which the request was made from if
 * 	the initiator was %NL80211_REGDOM_SET_BY_COUNTRY_IE or
 * 	%NL80211_REGDOM_SET_BY_DRIVER, the type of regulatory domain
 * 	set (%NL80211_ATTR_REG_TYPE), if the type of regulatory domain is
 * 	%NL80211_REG_TYPE_COUNTRY the alpha2 to which we have moved on
 * 	to (%NL80211_ATTR_REG_ALPHA2).
 * @NL80211_CMD_REG_BEACON_HINT: indicates to userspace that an AP beacon
 * 	has been found while world roaming thus enabling active scan or
 * 	any mode of operation that initiates TX (beacons) on a channel
 * 	where we would not have been able to do either before. As an example
 * 	if you are world roaming (regulatory domain set to world or if your
 * 	driver is using a custom world roaming regulatory domain) and while
 * 	doing a passive scan on the 5 GHz band you find an AP there (if not
 * 	on a DFS channel) you will now be able to actively scan for that AP
 * 	or use AP mode on your card on that same channel. Note that this will
 * 	never be used for channels 1-11 on the 2 GHz band as they are always
 * 	enabled world wide. This beacon hint is only sent if your device had
 * 	either disabled active scanning or beaconing on a channel. We send to
 * 	userspace the wiphy on which we removed a restriction from
 * 	(%NL80211_ATTR_WIPHY) and the channel on which this occurred
 * 	before (%NL80211_ATTR_FREQ_BEFORE) and after (%NL80211_ATTR_FREQ_AFTER)
 * 	the beacon hint was processed.
 *
 * @NL80211_CMD_AUTHENTICATE: authentication request and notification.
 *	This command is used both as a command (request to authenticate) and
 *	as an event on the "mlme" multicast group indicating completion of the
 *	authentication process.
 *	When used as a command, %NL80211_ATTR_IFINDEX is used to identify the
 *	interface. %NL80211_ATTR_MAC is used to specify PeerSTAAddress (and
 *	BSSID in case of station mode). %NL80211_ATTR_SSID is used to specify
 *	the SSID (mainly for association, but is included in authentication
 *	request, too, to help BSS selection. %NL80211_ATTR_WIPHY_FREQ is used
 *	to specify the frequence of the channel in MHz. %NL80211_ATTR_AUTH_TYPE
 *	is used to specify the authentication type. %NL80211_ATTR_IE is used to
 *	define IEs (VendorSpecificInfo, but also including RSN IE and FT IEs)
 *	to be added to the frame.
 *	When used as an event, this reports reception of an Authentication
 *	frame in station and IBSS modes when the local MLME processed the
 *	frame, i.e., it was for the local STA and was received in correct
 *	state. This is similar to MLME-AUTHENTICATE.confirm primitive in the
 *	MLME SAP interface (kernel providing MLME, userspace SME). The
 *	included %NL80211_ATTR_FRAME attribute contains the management frame
 *	(including both the header and frame body, but not FCS). This event is
 *	also used to indicate if the authentication attempt timed out. In that
 *	case the %NL80211_ATTR_FRAME attribute is replaced with a
 *	%NL80211_ATTR_TIMED_OUT flag (and %NL80211_ATTR_MAC to indicate which
 *	pending authentication timed out).
 * @NL80211_CMD_ASSOCIATE: association request and notification; like
 *	NL80211_CMD_AUTHENTICATE but for Association and Reassociation
 *	(similar to MLME-ASSOCIATE.request, MLME-REASSOCIATE.request,
 *	MLME-ASSOCIATE.confirm or MLME-REASSOCIATE.confirm primitives).
 * @NL80211_CMD_DEAUTHENTICATE: deauthentication request and notification; like
 *	NL80211_CMD_AUTHENTICATE but for Deauthentication frames (similar to
 *	MLME-DEAUTHENTICATION.request and MLME-DEAUTHENTICATE.indication
 *	primitives).
 * @NL80211_CMD_DISASSOCIATE: disassociation request and notification; like
 *	NL80211_CMD_AUTHENTICATE but for Disassociation frames (similar to
 *	MLME-DISASSOCIATE.request and MLME-DISASSOCIATE.indication primitives).
 *
 * @NL80211_CMD_MICHAEL_MIC_FAILURE: notification of a locally detected Michael
 *	MIC (part of TKIP) failure; sent on the "mlme" multicast group; the
 *	event includes %NL80211_ATTR_MAC to describe the source MAC address of
 *	the frame with invalid MIC, %NL80211_ATTR_KEY_TYPE to show the key
 *	type, %NL80211_ATTR_KEY_IDX to indicate the key identifier, and
 *	%NL80211_ATTR_KEY_SEQ to indicate the TSC value of the frame; this
 *	event matches with MLME-MICHAELMICFAILURE.indication() primitive
 *
 * @NL80211_CMD_JOIN_IBSS: Join a new IBSS -- given at least an SSID and a
 *	FREQ attribute (for the initial frequency if no peer can be found)
 *	and optionally a MAC (as BSSID) and FREQ_FIXED attribute if those
 *	should be fixed rather than automatically determined. Can only be
 *	executed on a network interface that is UP, and fixed BSSID/FREQ
 *	may be rejected. Another optional parameter is the beacon interval,
 *	given in the %NL80211_ATTR_BEACON_INTERVAL attribute, which if not
 *	given defaults to 100 TU (102.4ms).
 * @NL80211_CMD_LEAVE_IBSS: Leave the IBSS -- no special arguments, the IBSS is
 *	determined by the network interface.
 *
 * @NL80211_CMD_TESTMODE: testmode command, takes a wiphy (or ifindex) attribute
 *	to identify the device, and the TESTDATA blob attribute to pass through
 *	to the driver.
 *
 * @NL80211_CMD_CONNECT: connection request and notification; this command
 *	requests to connect to a specified network but without separating
 *	auth and assoc steps. For this, you need to specify the SSID in a
 *	%NL80211_ATTR_SSID attribute, and can optionally specify the association
 *	IEs in %NL80211_ATTR_IE, %NL80211_ATTR_AUTH_TYPE, %NL80211_ATTR_USE_MFP,
 *	%NL80211_ATTR_MAC, %NL80211_ATTR_WIPHY_FREQ, %NL80211_ATTR_CONTROL_PORT,
 *	%NL80211_ATTR_CONTROL_PORT_ETHERTYPE and
 *	%NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT.
 *	Background scan period can optionally be
 *	specified in %NL80211_ATTR_BG_SCAN_PERIOD,
 *	if not specified default background scan configuration
 *	in driver is used and if period value is 0, bg scan will be disabled.
 *	This attribute is ignored if driver does not support roam scan.
 *	It is also sent as an event, with the BSSID and response IEs when the
 *	connection is established or failed to be established. This can be
 *	determined by the STATUS_CODE attribute.
 * @NL80211_CMD_ROAM: request that the card roam (currently not implemented),
 *	sent as an event when the card/driver roamed by itself.
 * @NL80211_CMD_DISCONNECT: drop a given connection; also used to notify
 *	userspace that a connection was dropped by the AP or due to other
 *	reasons, for this the %NL80211_ATTR_DISCONNECTED_BY_AP and
 *	%NL80211_ATTR_REASON_CODE attributes are used.
 *
 * @NL80211_CMD_SET_WIPHY_NETNS: Set a wiphy's netns. Note that all devices
 *	associated with this wiphy must be down and will follow.
 *
 * @NL80211_CMD_REMAIN_ON_CHANNEL: Request to remain awake on the specified
 *	channel for the specified amount of time. This can be used to do
 *	off-channel operations like transmit a Public Action frame and wait for
 *	a response while being associated to an AP on another channel.
 *	%NL80211_ATTR_IFINDEX is used to specify which interface (and thus
 *	radio) is used. %NL80211_ATTR_WIPHY_FREQ is used to specify the
 *	frequency for the operation.
 *	%NL80211_ATTR_DURATION is used to specify the duration in milliseconds
 *	to remain on the channel. This command is also used as an event to
 *	notify when the requested duration starts (it may take a while for the
 *	driver to schedule this time due to other concurrent needs for the
 *	radio).
 *	When called, this operation returns a cookie (%NL80211_ATTR_COOKIE)
 *	that will be included with any events pertaining to this request;
 *	the cookie is also used to cancel the request.
 * @NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL: This command can be used to cancel a
 *	pending remain-on-channel duration if the desired operation has been
 *	completed prior to expiration of the originally requested duration.
 *	%NL80211_ATTR_WIPHY or %NL80211_ATTR_IFINDEX is used to specify the
 *	radio. The %NL80211_ATTR_COOKIE attribute must be given as well to
 *	uniquely identify the request.
 *	This command is also used as an event to notify when a requested
 *	remain-on-channel duration has expired.
 *
 * @NL80211_CMD_SET_TX_BITRATE_MASK: Set the mask of rates to be used in TX
 *	rate selection. %NL80211_ATTR_IFINDEX is used to specify the interface
 *	and @NL80211_ATTR_TX_RATES the set of allowed rates.
 *
 * @NL80211_CMD_REGISTER_FRAME: Register for receiving certain mgmt frames
 *	(via @NL80211_CMD_FRAME) for processing in userspace. This command
 *	requires an interface index, a frame type attribute (optional for
 *	backward compatibility reasons, if not given assumes action frames)
 *	and a match attribute containing the first few bytes of the frame
 *	that should match, e.g. a single byte for only a category match or
 *	four bytes for vendor frames including the OUI. The registration
 *	cannot be dropped, but is removed automatically when the netlink
 *	socket is closed. Multiple registrations can be made.
 * @NL80211_CMD_REGISTER_ACTION: Alias for @NL80211_CMD_REGISTER_FRAME for
 *	backward compatibility
 * @NL80211_CMD_FRAME: Management frame TX request and RX notification. This
 *	command is used both as a request to transmit a management frame and
 *	as an event indicating reception of a frame that was not processed in
 *	kernel code, but is for us (i.e., which may need to be processed in a
 *	user space application). %NL80211_ATTR_FRAME is used to specify the
 *	frame contents (including header). %NL80211_ATTR_WIPHY_FREQ is used
 *	to indicate on which channel the frame is to be transmitted or was
 *	received. If this channel is not the current channel (remain-on-channel
 *	or the operational channel) the device will switch to the given channel
 *	and transmit the frame, optionally waiting for a response for the time
 *	specified using %NL80211_ATTR_DURATION. When called, this operation
 *	returns a cookie (%NL80211_ATTR_COOKIE) that will be included with the
 *	TX status event pertaining to the TX request.
 *	%NL80211_ATTR_TX_NO_CCK_RATE is used to decide whether to send the
 *	management frames at CCK rate or not in 2GHz band.
 * @NL80211_CMD_FRAME_WAIT_CANCEL: When an off-channel TX was requested, this
 *	command may be used with the corresponding cookie to cancel the wait
 *	time if it is known that it is no longer necessary.
 * @NL80211_CMD_ACTION: Alias for @NL80211_CMD_FRAME for backward compatibility.
 * @NL80211_CMD_FRAME_TX_STATUS: Report TX status of a management frame
 *	transmitted with %NL80211_CMD_FRAME. %NL80211_ATTR_COOKIE identifies
 *	the TX command and %NL80211_ATTR_FRAME includes the contents of the
 *	frame. %NL80211_ATTR_ACK flag is included if the recipient acknowledged
 *	the frame.
 * @NL80211_CMD_ACTION_TX_STATUS: Alias for @NL80211_CMD_FRAME_TX_STATUS for
 *	backward compatibility.
 *
 * @NL80211_CMD_SET_POWER_SAVE: Set powersave, using %NL80211_ATTR_PS_STATE
 * @NL80211_CMD_GET_POWER_SAVE: Get powersave status in %NL80211_ATTR_PS_STATE
 *
 * @NL80211_CMD_SET_CQM: Connection quality monitor configuration. This command
 *	is used to configure connection quality monitoring notification trigger
 *	levels.
 * @NL80211_CMD_NOTIFY_CQM: Connection quality monitor notification. This
 *	command is used as an event to indicate the that a trigger level was
 *	reached.
 * @NL80211_CMD_SET_CHANNEL: Set the channel (using %NL80211_ATTR_WIPHY_FREQ
 *	and the attributes determining channel width) the given interface
 *	(identifed by %NL80211_ATTR_IFINDEX) shall operate on.
 *	In case multiple channels are supported by the device, the mechanism
 *	with which it switches channels is implementation-defined.
 *	When a monitor interface is given, it can only switch channel while
 *	no other interfaces are operating to avoid disturbing the operation
 *	of any other interfaces, and other interfaces will again take
 *	precedence when they are used.
 *
 * @NL80211_CMD_SET_WDS_PEER: Set the MAC address of the peer on a WDS interface.
 *
 * @NL80211_CMD_JOIN_MESH: Join a mesh. The mesh ID must be given, and initial
 *	mesh config parameters may be given.
 * @NL80211_CMD_LEAVE_MESH: Leave the mesh network -- no special arguments, the
 *	network is determined by the network interface.
 *
 * @NL80211_CMD_UNPROT_DEAUTHENTICATE: Unprotected deauthentication frame
 *	notification. This event is used to indicate that an unprotected
 *	deauthentication frame was dropped when MFP is in use.
 * @NL80211_CMD_UNPROT_DISASSOCIATE: Unprotected disassociation frame
 *	notification. This event is used to indicate that an unprotected
 *	disassociation frame was dropped when MFP is in use.
 *
 * @NL80211_CMD_NEW_PEER_CANDIDATE: Notification on the reception of a
 *      beacon or probe response from a compatible mesh peer.  This is only
 *      sent while no station information (sta_info) exists for the new peer
 *      candidate and when @NL80211_MESH_SETUP_USERSPACE_AUTH is set.  On
 *      reception of this notification, userspace may decide to create a new
 *      station (@NL80211_CMD_NEW_STATION).  To stop this notification from
 *      reoccurring, the userspace authentication daemon may want to create the
 *      new station with the AUTHENTICATED flag unset and maybe change it later
 *      depending on the authentication result.
 *
 * @NL80211_CMD_GET_WOWLAN: get Wake-on-Wireless-LAN (WoWLAN) settings.
 * @NL80211_CMD_SET_WOWLAN: set Wake-on-Wireless-LAN (WoWLAN) settings.
 *	Since wireless is more complex than wired ethernet, it supports
 *	various triggers. These triggers can be configured through this
 *	command with the %NL80211_ATTR_WOWLAN_TRIGGERS attribute. For
 *	more background information, see
 *	http://wireless.kernel.org/en/users/Documentation/WoWLAN.
 *	The @NL80211_CMD_SET_WOWLAN command can also be used as a notification
 *	from the driver reporting the wakeup reason. In this case, the
 *	@NL80211_ATTR_WOWLAN_TRIGGERS attribute will contain the reason
 *	for the wakeup, if it was caused by wireless. If it is not present
 *	in the wakeup notification, the wireless device didn't cause the
 *	wakeup but reports that it was woken up.
 *
 * @NL80211_CMD_SET_REKEY_OFFLOAD: This command is used give the driver
 *	the necessary information for supporting GTK rekey offload. This
 *	feature is typically used during WoWLAN. The configuration data
 *	is contained in %NL80211_ATTR_REKEY_DATA (which is nested and
 *	contains the data in sub-attributes). After rekeying happened,
 *	this command may also be sent by the driver as an MLME event to
 *	inform userspace of the new replay counter.
 *
 * @NL80211_CMD_PMKSA_CANDIDATE: This is used as an event to inform userspace
 *	of PMKSA caching dandidates.
 *
 * @NL80211_CMD_TDLS_OPER: Perform a high-level TDLS command (e.g. link setup).
 *	In addition, this can be used as an event to request userspace to take
 *	actions on TDLS links (set up a new link or tear down an existing one).
 *	In such events, %NL80211_ATTR_TDLS_OPERATION indicates the requested
 *	operation, %NL80211_ATTR_MAC contains the peer MAC address, and
 *	%NL80211_ATTR_REASON_CODE the reason code to be used (only with
 *	%NL80211_TDLS_TEARDOWN).
 * @NL80211_CMD_TDLS_MGMT: Send a TDLS management frame.
 *
 * @NL80211_CMD_UNEXPECTED_FRAME: Used by an application controlling an AP
 *	(or GO) interface (i.e. hostapd) to ask for unexpected frames to
 *	implement sending deauth to stations that send unexpected class 3
 *	frames. Also used as the event sent by the kernel when such a frame
 *	is received.
 *	For the event, the %NL80211_ATTR_MAC attribute carries the TA and
 *	other attributes like the interface index are present.
 *	If used as the command it must have an interface index and you can
 *	only unsubscribe from the event by closing the socket. Subscription
 *	is also for %NL80211_CMD_UNEXPECTED_4ADDR_FRAME events.
 *
 * @NL80211_CMD_UNEXPECTED_4ADDR_FRAME: Sent as an event indicating that the
 *	associated station identified by %NL80211_ATTR_MAC sent a 4addr frame
 *	and wasn't already in a 4-addr VLAN. The event will be sent similarly
 *	to the %NL80211_CMD_UNEXPECTED_FRAME event, to the same listener.
 *
 * @NL80211_CMD_PROBE_CLIENT: Probe an associated station on an AP interface
 *	by sending a null data frame to it and reporting when the frame is
 *	acknowleged. This is used to allow timing out inactive clients. Uses
 *	%NL80211_ATTR_IFINDEX and %NL80211_ATTR_MAC. The command returns a
 *	direct reply with an %NL80211_ATTR_COOKIE that is later used to match
 *	up the event with the request. The event includes the same data and
 *	has %NL80211_ATTR_ACK set if the frame was ACKed.
 *
 * @NL80211_CMD_REGISTER_BEACONS: Register this socket to receive beacons from
 *	other BSSes when any interfaces are in AP mode. This helps implement
 *	OLBC handling in hostapd. Beacons are reported in %NL80211_CMD_FRAME
 *	messages. Note that per PHY only one application may register.
 *
 * @NL80211_CMD_SET_NOACK_MAP: sets a bitmap for the individual TIDs whether
 *      No Acknowledgement Policy should be applied.
 *
 * @NL80211_CMD_CH_SWITCH_NOTIFY: An AP or GO may decide to switch channels
 *	independently of the userspace SME, send this event indicating
 *	%NL80211_ATTR_IFINDEX is now on %NL80211_ATTR_WIPHY_FREQ and the
 *	attributes determining channel width.
 *
 * @NL80211_CMD_START_P2P_DEVICE: Start the given P2P Device, identified by
 *	its %NL80211_ATTR_WDEV identifier. It must have been created with
 *	%NL80211_CMD_NEW_INTERFACE previously. After it has been started, the
 *	P2P Device can be used for P2P operations, e.g. remain-on-channel and
 *	public action frame TX.
 * @NL80211_CMD_STOP_P2P_DEVICE: Stop the given P2P Device, identified by
 *	its %NL80211_ATTR_WDEV identifier.
 *
 * @NL80211_CMD_CONN_FAILED: connection request to an AP failed; used to
 *	notify userspace that AP has rejected the connection request from a
 *	station, due to particular reason. %NL80211_ATTR_CONN_FAILED_REASON
 *	is used for this.
 *
 * @NL80211_CMD_SET_MCAST_RATE: Change the rate used to send multicast frames
 *	for IBSS or MESH vif.
 *
 * @NL80211_CMD_SET_MAC_ACL: sets ACL for MAC address based access control.
 *	This is to be used with the drivers advertising the support of MAC
 *	address based access control. List of MAC addresses is passed in
 *	%NL80211_ATTR_MAC_ADDRS and ACL policy is passed in
 *	%NL80211_ATTR_ACL_POLICY. Driver will enable ACL with this list, if it
 *	is not already done. The new list will replace any existing list. Driver
 *	will clear its ACL when the list of MAC addresses passed is empty. This
 *	command is used in AP/P2P GO mode. Driver has to make sure to clear its
 *	ACL list during %NL80211_CMD_STOP_AP.
 *
 * @NL80211_CMD_RADAR_DETECT: Start a Channel availability check (CAC). Once
 *	a radar is detected or the channel availability scan (CAC) has finished
 *	or was aborted, or a radar was detected, usermode will be notified with
 *	this event. This command is also used to notify userspace about radars
 *	while operating on this channel.
 *	%NL80211_ATTR_RADAR_EVENT is used to inform about the type of the
 *	event.
 *
 * @NL80211_CMD_GET_PROTOCOL_FEATURES: Get global nl80211 protocol features,
 *	i.e. features for the nl80211 protocol rather than device features.
 *	Returns the features in the %NL80211_ATTR_PROTOCOL_FEATURES bitmap.
 *
 * @NL80211_CMD_MAX: highest used command number
 * @__NL80211_CMD_AFTER_LAST: internal use
 */
enum nl80211_commands {
/* don't change the order or add anything between, this is ABI! */
	NL80211_CMD_UNSPEC,

	NL80211_CMD_GET_WIPHY,		/* can dump */
	NL80211_CMD_SET_WIPHY,
	NL80211_CMD_NEW_WIPHY,
	NL80211_CMD_DEL_WIPHY,

	NL80211_CMD_GET_INTERFACE,	/* can dump */
	NL80211_CMD_SET_INTERFACE,
	NL80211_CMD_NEW_INTERFACE,
	NL80211_CMD_DEL_INTERFACE,

	NL80211_CMD_GET_KEY,
	NL80211_CMD_SET_KEY,
	NL80211_CMD_NEW_KEY,
	NL80211_CMD_DEL_KEY,

	NL80211_CMD_GET_BEACON,
	NL80211_CMD_SET_BEACON,
	NL80211_CMD_START_AP,
	NL80211_CMD_NEW_BEACON = NL80211_CMD_START_AP,
	NL80211_CMD_STOP_AP,
	NL80211_CMD_DEL_BEACON = NL80211_CMD_STOP_AP,

	NL80211_CMD_GET_STATION,
	NL80211_CMD_SET_STATION,
	NL80211_CMD_NEW_STATION,
	NL80211_CMD_DEL_STATION,

	NL80211_CMD_GET_MPATH,
	NL80211_CMD_SET_MPATH,
	NL80211_CMD_NEW_MPATH,
	NL80211_CMD_DEL_MPATH,

	NL80211_CMD_SET_BSS,

	NL80211_CMD_SET_REG,
	NL80211_CMD_REQ_SET_REG,

	NL80211_CMD_GET_MESH_CONFIG,
	NL80211_CMD_SET_MESH_CONFIG,

	NL80211_CMD_SET_MGMT_EXTRA_IE /* reserved; not used */,

	NL80211_CMD_GET_REG,

	NL80211_CMD_GET_SCAN,
	NL80211_CMD_TRIGGER_SCAN,
	NL80211_CMD_NEW_SCAN_RESULTS,
	NL80211_CMD_SCAN_ABORTED,

	NL80211_CMD_REG_CHANGE,

	NL80211_CMD_AUTHENTICATE,
	NL80211_CMD_ASSOCIATE,
	NL80211_CMD_DEAUTHENTICATE,
	NL80211_CMD_DISASSOCIATE,

	NL80211_CMD_MICHAEL_MIC_FAILURE,

	NL80211_CMD_REG_BEACON_HINT,

	NL80211_CMD_JOIN_IBSS,
	NL80211_CMD_LEAVE_IBSS,

	NL80211_CMD_TESTMODE,

	NL80211_CMD_CONNECT,
	NL80211_CMD_ROAM,
	NL80211_CMD_DISCONNECT,

	NL80211_CMD_SET_WIPHY_NETNS,

	NL80211_CMD_GET_SURVEY,
	NL80211_CMD_NEW_SURVEY_RESULTS,

	NL80211_CMD_SET_PMKSA,
	NL80211_CMD_DEL_PMKSA,
	NL80211_CMD_FLUSH_PMKSA,

	NL80211_CMD_REMAIN_ON_CHANNEL,
	NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL,

	NL80211_CMD_SET_TX_BITRATE_MASK,

	NL80211_CMD_REGISTER_FRAME,
	NL80211_CMD_REGISTER_ACTION = NL80211_CMD_REGISTER_FRAME,
	NL80211_CMD_FRAME,
	NL80211_CMD_ACTION = NL80211_CMD_FRAME,
	NL80211_CMD_FRAME_TX_STATUS,
	NL80211_CMD_ACTION_TX_STATUS = NL80211_CMD_FRAME_TX_STATUS,

	NL80211_CMD_SET_POWER_SAVE,
	NL80211_CMD_GET_POWER_SAVE,

	NL80211_CMD_SET_CQM,
	NL80211_CMD_NOTIFY_CQM,

	NL80211_CMD_SET_CHANNEL,
	NL80211_CMD_SET_WDS_PEER,

	NL80211_CMD_FRAME_WAIT_CANCEL,

	NL80211_CMD_JOIN_MESH,
	NL80211_CMD_LEAVE_MESH,

	NL80211_CMD_UNPROT_DEAUTHENTICATE,
	NL80211_CMD_UNPROT_DISASSOCIATE,

	NL80211_CMD_NEW_PEER_CANDIDATE,

	NL80211_CMD_GET_WOWLAN,
	NL80211_CMD_SET_WOWLAN,

	NL80211_CMD_START_SCHED_SCAN,
	NL80211_CMD_STOP_SCHED_SCAN,
	NL80211_CMD_SCHED_SCAN_RESULTS,
	NL80211_CMD_SCHED_SCAN_STOPPED,

	NL80211_CMD_SET_REKEY_OFFLOAD,

	NL80211_CMD_PMKSA_CANDIDATE,

	NL80211_CMD_TDLS_OPER,
	NL80211_CMD_TDLS_MGMT,

	NL80211_CMD_UNEXPECTED_FRAME,

	NL80211_CMD_PROBE_CLIENT,

	NL80211_CMD_REGISTER_BEACONS,

	NL80211_CMD_UNEXPECTED_4ADDR_FRAME,

	NL80211_CMD_SET_NOACK_MAP,

	NL80211_CMD_CH_SWITCH_NOTIFY,

	NL80211_CMD_START_P2P_DEVICE,
	NL80211_CMD_STOP_P2P_DEVICE,

	NL80211_CMD_CONN_FAILED,

	NL80211_CMD_SET_MCAST_RATE,

	NL80211_CMD_SET_MAC_ACL,

	NL80211_CMD_RADAR_DETECT,

	NL80211_CMD_GET_PROTOCOL_FEATURES,

	/* add new commands above here */

	/* used to define NL80211_CMD_MAX below */
	__NL80211_CMD_AFTER_LAST,
	NL80211_CMD_MAX = __NL80211_CMD_AFTER_LAST - 1
};

/*
 * Allow user space programs to use #ifdef on new commands by defining them
 * here
 */
#define NL80211_CMD_SET_BSS NL80211_CMD_SET_BSS
#define NL80211_CMD_SET_MGMT_EXTRA_IE NL80211_CMD_SET_MGMT_EXTRA_IE
#define NL80211_CMD_REG_CHANGE NL80211_CMD_REG_CHANGE
#define NL80211_CMD_AUTHENTICATE NL80211_CMD_AUTHENTICATE
#define NL80211_CMD_ASSOCIATE NL80211_CMD_ASSOCIATE
#define NL80211_CMD_DEAUTHENTICATE NL80211_CMD_DEAUTHENTICATE
#define NL80211_CMD_DISASSOCIATE NL80211_CMD_DISASSOCIATE
#define NL80211_CMD_REG_BEACON_HINT NL80211_CMD_REG_BEACON_HINT

#define NL80211_ATTR_FEATURE_FLAGS NL80211_ATTR_FEATURE_FLAGS

/* source-level API compatibility */
#define NL80211_CMD_GET_MESH_PARAMS NL80211_CMD_GET_MESH_CONFIG
#define NL80211_CMD_SET_MESH_PARAMS NL80211_CMD_SET_MESH_CONFIG
#define NL80211_MESH_SETUP_VENDOR_PATH_SEL_IE NL80211_MESH_SETUP_IE

/**
 * enum nl80211_attrs - nl80211 netlink attributes
 *
 * @NL80211_ATTR_UNSPEC: unspecified attribute to catch errors
 *
 * @NL80211_ATTR_WIPHY: index of wiphy to operate on, cf.
 *	/sys/class/ieee80211/<phyname>/index
 * @NL80211_ATTR_WIPHY_NAME: wiphy name (used for renaming)
 * @NL80211_ATTR_WIPHY_TXQ_PARAMS: a nested array of TX queue parameters
 * @NL80211_ATTR_WIPHY_FREQ: frequency of the selected channel in MHz,
 *	defines the channel together with the (deprecated)
 *	%NL80211_ATTR_WIPHY_CHANNEL_TYPE attribute or the attributes
 *	%NL80211_ATTR_CHANNEL_WIDTH and if needed %NL80211_ATTR_CENTER_FREQ1
 *	and %NL80211_ATTR_CENTER_FREQ2
 * @NL80211_ATTR_CHANNEL_WIDTH: u32 attribute containing one of the values
 *	of &enum nl80211_chan_width, describing the channel width. See the
 *	documentation of the enum for more information.
 * @NL80211_ATTR_CENTER_FREQ1: Center frequency of the first part of the
 *	channel, used for anything but 20 MHz bandwidth
 * @NL80211_ATTR_CENTER_FREQ2: Center frequency of the second part of the
 *	channel, used only for 80+80 MHz bandwidth
 * @NL80211_ATTR_WIPHY_CHANNEL_TYPE: included with NL80211_ATTR_WIPHY_FREQ
 *	if HT20 or HT40 are to be used (i.e., HT disabled if not included):
 *	NL80211_CHAN_NO_HT = HT not allowed (i.e., same as not including
 *		this attribute)
 *	NL80211_CHAN_HT20 = HT20 only
 *	NL80211_CHAN_HT40MINUS = secondary channel is below the primary channel
 *	NL80211_CHAN_HT40PLUS = secondary channel is above the primary channel
 *	This attribute is now deprecated.
 * @NL80211_ATTR_WIPHY_RETRY_SHORT: TX retry limit for frames whose length is
 *	less than or equal to the RTS threshold; allowed range: 1..255;
 *	dot11ShortRetryLimit; u8
 * @NL80211_ATTR_WIPHY_RETRY_LONG: TX retry limit for frames whose length is
 *	greater than the RTS threshold; allowed range: 1..255;
 *	dot11ShortLongLimit; u8
 * @NL80211_ATTR_WIPHY_FRAG_THRESHOLD: fragmentation threshold, i.e., maximum
 *	length in octets for frames; allowed range: 256..8000, disable
 *	fragmentation with (u32)-1; dot11FragmentationThreshold; u32
 * @NL80211_ATTR_WIPHY_RTS_THRESHOLD: RTS threshold (TX frames with length
 *	larger than or equal to this use RTS/CTS handshake); allowed range:
 *	0..65536, disable with (u32)-1; dot11RTSThreshold; u32
 * @NL80211_ATTR_WIPHY_COVERAGE_CLASS: Coverage Class as defined by IEEE 802.11
 *	section 7.3.2.9; dot11CoverageClass; u8
 *
 * @NL80211_ATTR_IFINDEX: network interface index of the device to operate on
 * @NL80211_ATTR_IFNAME: network interface name
 * @NL80211_ATTR_IFTYPE: type of virtual interface, see &enum nl80211_iftype
 *
 * @NL80211_ATTR_WDEV: wireless device identifier, used for pseudo-devices
 *	that don't have a netdev (u64)
 *
 * @NL80211_ATTR_MAC: MAC address (various uses)
 *
 * @NL80211_ATTR_KEY_DATA: (temporal) key data; for TKIP this consists of
 *	16 bytes encryption key followed by 8 bytes each for TX and RX MIC
 *	keys
 * @NL80211_ATTR_KEY_IDX: key ID (u8, 0-3)
 * @NL80211_ATTR_KEY_CIPHER: key cipher suite (u32, as defined by IEEE 802.11
 *	section 7.3.2.25.1, e.g. 0x000FAC04)
 * @NL80211_ATTR_KEY_SEQ: transmit key sequence number (IV/PN) for TKIP and
 *	CCMP keys, each six bytes in little endian
 * @NL80211_ATTR_KEY_DEFAULT: Flag attribute indicating the key is default key
 * @NL80211_ATTR_KEY_DEFAULT_MGMT: Flag attribute indicating the key is the
 *	default management key
 * @NL80211_ATTR_CIPHER_SUITES_PAIRWISE: For crypto settings for connect or
 *	other commands, indicates which pairwise cipher suites are used
 * @NL80211_ATTR_CIPHER_SUITE_GROUP: For crypto settings for connect or
 *	other commands, indicates which group cipher suite is used
 *
 * @NL80211_ATTR_BEACON_INTERVAL: beacon interval in TU
 * @NL80211_ATTR_DTIM_PERIOD: DTIM period for beaconing
 * @NL80211_ATTR_BEACON_HEAD: portion of the beacon before the TIM IE
 * @NL80211_ATTR_BEACON_TAIL: portion of the beacon after the TIM IE
 *
 * @NL80211_ATTR_STA_AID: Association ID for the station (u16)
 * @NL80211_ATTR_STA_FLAGS: flags, nested element with NLA_FLAG attributes of
 *	&enum nl80211_sta_flags (deprecated, use %NL80211_ATTR_STA_FLAGS2)
 * @NL80211_ATTR_STA_LISTEN_INTERVAL: listen interval as defined by
 *	IEEE 802.11 7.3.1.6 (u16).
 * @NL80211_ATTR_STA_SUPPORTED_RATES: supported rates, array of supported
 *	rates as defined by IEEE 802.11 7.3.2.2 but without the length
 *	restriction (at most %NL80211_MAX_SUPP_RATES).
 * @NL80211_ATTR_STA_VLAN: interface index of VLAN interface to move station
 *	to, or the AP interface the station was originally added to to.
 * @NL80211_ATTR_STA_INFO: information about a station, part of station info
 *	given for %NL80211_CMD_GET_STATION, nested attribute containing
 *	info as possible, see &enum nl80211_sta_info.
 *
 * @NL80211_ATTR_WIPHY_BANDS: Information about an operating bands,
 *	consisting of a nested array.
 *
 * @NL80211_ATTR_MESH_ID: mesh id (1-32 bytes).
 * @NL80211_ATTR_STA_PLINK_ACTION: action to perform on the mesh peer link
 *	(see &enum nl80211_plink_action).
 * @NL80211_ATTR_MPATH_NEXT_HOP: MAC address of the next hop for a mesh path.
 * @NL80211_ATTR_MPATH_INFO: information about a mesh_path, part of mesh path
 * 	info given for %NL80211_CMD_GET_MPATH, nested attribute described at
 *	&enum nl80211_mpath_info.
 *
 * @NL80211_ATTR_MNTR_FLAGS: flags, nested element with NLA_FLAG attributes of
 *      &enum nl80211_mntr_flags.
 *
 * @NL80211_ATTR_REG_ALPHA2: an ISO-3166-alpha2 country code for which the
 * 	current regulatory domain should be set to or is already set to.
 * 	For example, 'CR', for Costa Rica. This attribute is used by the kernel
 * 	to query the CRDA to retrieve one regulatory domain. This attribute can
 * 	also be used by userspace to query the kernel for the currently set
 * 	regulatory domain. We chose an alpha2 as that is also used by the
 * 	IEEE-802.11d country information element to identify a country.
 * 	Users can also simply ask the wireless core to set regulatory domain
 * 	to a specific alpha2.
 * @NL80211_ATTR_REG_RULES: a nested array of regulatory domain regulatory
 *	rules.
 *
 * @NL80211_ATTR_BSS_CTS_PROT: whether CTS protection is enabled (u8, 0 or 1)
 * @NL80211_ATTR_BSS_SHORT_PREAMBLE: whether short preamble is enabled
 *	(u8, 0 or 1)
 * @NL80211_ATTR_BSS_SHORT_SLOT_TIME: whether short slot time enabled
 *	(u8, 0 or 1)
 * @NL80211_ATTR_BSS_BASIC_RATES: basic rates, array of basic
 *	rates in format defined by IEEE 802.11 7.3.2.2 but without the length
 *	restriction (at most %NL80211_MAX_SUPP_RATES).
 *
 * @NL80211_ATTR_HT_CAPABILITY: HT Capability information element (from
 *	association request when used with NL80211_CMD_NEW_STATION)
 *
 * @NL80211_ATTR_SUPPORTED_IFTYPES: nested attribute containing all
 *	supported interface types, each a flag attribute with the number
 *	of the interface mode.
 *
 * @NL80211_ATTR_MGMT_SUBTYPE: Management frame subtype for
 *	%NL80211_CMD_SET_MGMT_EXTRA_IE.
 *
 * @NL80211_ATTR_IE: Information element(s) data (used, e.g., with
 *	%NL80211_CMD_SET_MGMT_EXTRA_IE).
 *
 * @NL80211_ATTR_MAX_NUM_SCAN_SSIDS: number of SSIDs you can scan with
 *	a single scan request, a wiphy attribute.
 * @NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS: number of SSIDs you can
 *	scan with a single scheduled scan request, a wiphy attribute.
 * @NL80211_ATTR_MAX_SCAN_IE_LEN: maximum length of information elements
 *	that can be added to a scan request
 * @NL80211_ATTR_MAX_SCHED_SCAN_IE_LEN: maximum length of information
 *	elements that can be added to a scheduled scan request
 * @NL80211_ATTR_MAX_MATCH_SETS: maximum number of sets that can be
 *	used with @NL80211_ATTR_SCHED_SCAN_MATCH, a wiphy attribute.
 *
 * @NL80211_ATTR_SCAN_FREQUENCIES: nested attribute with frequencies (in MHz)
 * @NL80211_ATTR_SCAN_SSIDS: nested attribute with SSIDs, leave out for passive
 *	scanning and include a zero-length SSID (wildcard) for wildcard scan
 * @NL80211_ATTR_BSS: scan result BSS
 *
 * @NL80211_ATTR_REG_INITIATOR: indicates who requested the regulatory domain
 * 	currently in effect. This could be any of the %NL80211_REGDOM_SET_BY_*
 * @NL80211_ATTR_REG_TYPE: indicates the type of the regulatory domain currently
 * 	set. This can be one of the nl80211_reg_type (%NL80211_REGDOM_TYPE_*)
 *
 * @NL80211_ATTR_SUPPORTED_COMMANDS: wiphy attribute that specifies
 *	an array of command numbers (i.e. a mapping index to command number)
 *	that the driver for the given wiphy supports.
 *
 * @NL80211_ATTR_FRAME: frame data (binary attribute), including frame header
 *	and body, but not FCS; used, e.g., with NL80211_CMD_AUTHENTICATE and
 *	NL80211_CMD_ASSOCIATE events
 * @NL80211_ATTR_SSID: SSID (binary attribute, 0..32 octets)
 * @NL80211_ATTR_AUTH_TYPE: AuthenticationType, see &enum nl80211_auth_type,
 *	represented as a u32
 * @NL80211_ATTR_REASON_CODE: ReasonCode for %NL80211_CMD_DEAUTHENTICATE and
 *	%NL80211_CMD_DISASSOCIATE, u16
 *
 * @NL80211_ATTR_KEY_TYPE: Key Type, see &enum nl80211_key_type, represented as
 *	a u32
 *
 * @NL80211_ATTR_FREQ_BEFORE: A channel which has suffered a regulatory change
 * 	due to considerations from a beacon hint. This attribute reflects
 * 	the state of the channel _before_ the beacon hint processing. This
 * 	attributes consists of a nested attribute containing
 * 	NL80211_FREQUENCY_ATTR_*
 * @NL80211_ATTR_FREQ_AFTER: A channel which has suffered a regulatory change
 * 	due to considerations from a beacon hint. This attribute reflects
 * 	the state of the channel _after_ the beacon hint processing. This
 * 	attributes consists of a nested attribute containing
 * 	NL80211_FREQUENCY_ATTR_*
 *
 * @NL80211_ATTR_CIPHER_SUITES: a set of u32 values indicating the supported
 *	cipher suites
 *
 * @NL80211_ATTR_FREQ_FIXED: a flag indicating the IBSS should not try to look
 *	for other networks on different channels
 *
 * @NL80211_ATTR_TIMED_OUT: a flag indicating than an operation timed out; this
 *	is used, e.g., with %NL80211_CMD_AUTHENTICATE event
 *
 * @NL80211_ATTR_USE_MFP: Whether management frame protection (IEEE 802.11w) is
 *	used for the association (&enum nl80211_mfp, represented as a u32);
 *	this attribute can be used
 *	with %NL80211_CMD_ASSOCIATE and %NL80211_CMD_CONNECT requests
 *
 * @NL80211_ATTR_STA_FLAGS2: Attribute containing a
 *	&struct nl80211_sta_flag_update.
 *
 * @NL80211_ATTR_CONTROL_PORT: A flag indicating whether user space controls
 *	IEEE 802.1X port, i.e., sets/clears %NL80211_STA_FLAG_AUTHORIZED, in
 *	station mode. If the flag is included in %NL80211_CMD_ASSOCIATE
 *	request, the driver will assume that the port is unauthorized until
 *	authorized by user space. Otherwise, port is marked authorized by
 *	default in station mode.
 * @NL80211_ATTR_CONTROL_PORT_ETHERTYPE: A 16-bit value indicating the
 *	ethertype that will be used for key negotiation. It can be
 *	specified with the associate and connect commands. If it is not
 *	specified, the value defaults to 0x888E (PAE, 802.1X). This
 *	attribute is also used as a flag in the wiphy information to
 *	indicate that protocols other than PAE are supported.
 * @NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT: When included along with
 *	%NL80211_ATTR_CONTROL_PORT_ETHERTYPE, indicates that the custom
 *	ethertype frames used for key negotiation must not be encrypted.
 *
 * @NL80211_ATTR_TESTDATA: Testmode data blob, passed through to the driver.
 *	We recommend using nested, driver-specific attributes within this.
 *
 * @NL80211_ATTR_DISCONNECTED_BY_AP: A flag indicating that the DISCONNECT
 *	event was due to the AP disconnecting the station, and not due to
 *	a local disconnect request.
 * @NL80211_ATTR_STATUS_CODE: StatusCode for the %NL80211_CMD_CONNECT
 *	event (u16)
 * @NL80211_ATTR_PRIVACY: Flag attribute, used with connect(), indicating
 *	that protected APs should be used. This is also used with NEW_BEACON to
 *	indicate that the BSS is to use protection.
 *
 * @NL80211_ATTR_CIPHERS_PAIRWISE: Used with CONNECT, ASSOCIATE, and NEW_BEACON
 *	to indicate which unicast key ciphers will be used with the connection
 *	(an array of u32).
 * @NL80211_ATTR_CIPHER_GROUP: Used with CONNECT, ASSOCIATE, and NEW_BEACON to
 *	indicate which group key cipher will be used with the connection (a
 *	u32).
 * @NL80211_ATTR_WPA_VERSIONS: Used with CONNECT, ASSOCIATE, and NEW_BEACON to
 *	indicate which WPA version(s) the AP we want to associate with is using
 *	(a u32 with flags from &enum nl80211_wpa_versions).
 * @NL80211_ATTR_AKM_SUITES: Used with CONNECT, ASSOCIATE, and NEW_BEACON to
 *	indicate which key management algorithm(s) to use (an array of u32).
 *
 * @NL80211_ATTR_REQ_IE: (Re)association request information elements as
 *	sent out by the card, for ROAM and successful CONNECT events.
 * @NL80211_ATTR_RESP_IE: (Re)association response information elements as
 *	sent by peer, for ROAM and successful CONNECT events.
 *
 * @NL80211_ATTR_PREV_BSSID: previous BSSID, to be used by in ASSOCIATE
 *	commands to specify using a reassociate frame
 *
 * @NL80211_ATTR_KEY: key information in a nested attribute with
 *	%NL80211_KEY_* sub-attributes
 * @NL80211_ATTR_KEYS: array of keys for static WEP keys for connect()
 *	and join_ibss(), key information is in a nested attribute each
 *	with %NL80211_KEY_* sub-attributes
 *
 * @NL80211_ATTR_PID: Process ID of a network namespace.
 *
 * @NL80211_ATTR_GENERATION: Used to indicate consistent snapshots for
 *	dumps. This number increases whenever the object list being
 *	dumped changes, and as such userspace can verify that it has
 *	obtained a complete and consistent snapshot by verifying that
 *	all dump messages contain the same generation number. If it
 *	changed then the list changed and the dump should be repeated
 *	completely from scratch.
 *
 * @NL80211_ATTR_4ADDR: Use 4-address frames on a virtual interface
 *
 * @NL80211_ATTR_SURVEY_INFO: survey information about a channel, part of
 *      the survey response for %NL80211_CMD_GET_SURVEY, nested attribute
 *      containing info as possible, see &enum survey_info.
 *
 * @NL80211_ATTR_PMKID: PMK material for PMKSA caching.
 * @NL80211_ATTR_MAX_NUM_PMKIDS: maximum number of PMKIDs a firmware can
 *	cache, a wiphy attribute.
 *
 * @NL80211_ATTR_DURATION: Duration of an operation in milliseconds, u32.
 * @NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION: Device attribute that
 *	specifies the maximum duration that can be requested with the
 *	remain-on-channel operation, in milliseconds, u32.
 *
 * @NL80211_ATTR_COOKIE: Generic 64-bit cookie to identify objects.
 *
 * @NL80211_ATTR_TX_RATES: Nested set of attributes
 *	(enum nl80211_tx_rate_attributes) describing TX rates per band. The
 *	enum nl80211_band value is used as the index (nla_type() of the nested
 *	data. If a band is not included, it will be configured to allow all
 *	rates based on negotiated supported rates information. This attribute
 *	is used with %NL80211_CMD_SET_TX_BITRATE_MASK.
 *
 * @NL80211_ATTR_FRAME_MATCH: A binary attribute which typically must contain
 *	at least one byte, currently used with @NL80211_CMD_REGISTER_FRAME.
 * @NL80211_ATTR_FRAME_TYPE: A u16 indicating the frame type/subtype for the
 *	@NL80211_CMD_REGISTER_FRAME command.
 * @NL80211_ATTR_TX_FRAME_TYPES: wiphy capability attribute, which is a
 *	nested attribute of %NL80211_ATTR_FRAME_TYPE attributes, containing
 *	information about which frame types can be transmitted with
 *	%NL80211_CMD_FRAME.
 * @NL80211_ATTR_RX_FRAME_TYPES: wiphy capability attribute, which is a
 *	nested attribute of %NL80211_ATTR_FRAME_TYPE attributes, containing
 *	information about which frame types can be registered for RX.
 *
 * @NL80211_ATTR_ACK: Flag attribute indicating that the frame was
 *	acknowledged by the recipient.
 *
 * @NL80211_ATTR_PS_STATE: powersave state, using &enum nl80211_ps_state values.
 *
 * @NL80211_ATTR_CQM: connection quality monitor configuration in a
 *	nested attribute with %NL80211_ATTR_CQM_* sub-attributes.
 *
 * @NL80211_ATTR_LOCAL_STATE_CHANGE: Flag attribute to indicate that a command
 *	is requesting a local authentication/association state change without
 *	invoking actual management frame exchange. This can be used with
 *	NL80211_CMD_AUTHENTICATE, NL80211_CMD_DEAUTHENTICATE,
 *	NL80211_CMD_DISASSOCIATE.
 *
 * @NL80211_ATTR_AP_ISOLATE: (AP mode) Do not forward traffic between stations
 *	connected to this BSS.
 *
 * @NL80211_ATTR_WIPHY_TX_POWER_SETTING: Transmit power setting type. See
 *      &enum nl80211_tx_power_setting for possible values.
 * @NL80211_ATTR_WIPHY_TX_POWER_LEVEL: Transmit power level in signed mBm units.
 *      This is used in association with @NL80211_ATTR_WIPHY_TX_POWER_SETTING
 *      for non-automatic settings.
 *
 * @NL80211_ATTR_SUPPORT_IBSS_RSN: The device supports IBSS RSN, which mostly
 *	means support for per-station GTKs.
 *
 * @NL80211_ATTR_WIPHY_ANTENNA_TX: Bitmap of allowed antennas for transmitting.
 *	This can be used to mask out antennas which are not attached or should
 *	not be used for transmitting. If an antenna is not selected in this
 *	bitmap the hardware is not allowed to transmit on this antenna.
 *
 *	Each bit represents one antenna, starting with antenna 1 at the first
 *	bit. Depending on which antennas are selected in the bitmap, 802.11n
 *	drivers can derive which chainmasks to use (if all antennas belonging to
 *	a particular chain are disabled this chain should be disabled) and if
 *	a chain has diversity antennas wether diversity should be used or not.
 *	HT capabilities (STBC, TX Beamforming, Antenna selection) can be
 *	derived from the available chains after applying the antenna mask.
 *	Non-802.11n drivers can derive wether to use diversity or not.
 *	Drivers may reject configurations or RX/TX mask combinations they cannot
 *	support by returning -EINVAL.
 *
 * @NL80211_ATTR_WIPHY_ANTENNA_RX: Bitmap of allowed antennas for receiving.
 *	This can be used to mask out antennas which are not attached or should
 *	not be used for receiving. If an antenna is not selected in this bitmap
 *	the hardware should not be configured to receive on this antenna.
 *	For a more detailed description see @NL80211_ATTR_WIPHY_ANTENNA_TX.
 *
 * @NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX: Bitmap of antennas which are available
 *	for configuration as TX antennas via the above parameters.
 *
 * @NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX: Bitmap of antennas which are available
 *	for configuration as RX antennas via the above parameters.
 *
 * @NL80211_ATTR_MCAST_RATE: Multicast tx rate (in 100 kbps) for IBSS
 *
 * @NL80211_ATTR_OFFCHANNEL_TX_OK: For management frame TX, the frame may be
 *	transmitted on another channel when the channel given doesn't match
 *	the current channel. If the current channel doesn't match and this
 *	flag isn't set, the frame will be rejected. This is also used as an
 *	nl80211 capability flag.
 *
 * @NL80211_ATTR_BSS_HT_OPMODE: HT operation mode (u16)
 *
 * @NL80211_ATTR_KEY_DEFAULT_TYPES: A nested attribute containing flags
 *	attributes, specifying what a key should be set as default as.
 *	See &enum nl80211_key_default_types.
 *
 * @NL80211_ATTR_MESH_SETUP: Optional mesh setup parameters.  These cannot be
 *	changed once the mesh is active.
 * @NL80211_ATTR_MESH_CONFIG: Mesh configuration parameters, a nested attribute
 *	containing attributes from &enum nl80211_meshconf_params.
 * @NL80211_ATTR_SUPPORT_MESH_AUTH: Currently, this means the underlying driver
 *	allows auth frames in a mesh to be passed to userspace for processing via
 *	the @NL80211_MESH_SETUP_USERSPACE_AUTH flag.
 * @NL80211_ATTR_STA_PLINK_STATE: The state of a mesh peer link as
 *	defined in &enum nl80211_plink_state. Used when userspace is
 *	driving the peer link management state machine.
 *	@NL80211_MESH_SETUP_USERSPACE_AMPE must be enabled.
 *
 * @NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED: indicates, as part of the wiphy
 *	capabilities, the supported WoWLAN triggers
 * @NL80211_ATTR_WOWLAN_TRIGGERS: used by %NL80211_CMD_SET_WOWLAN to
 *	indicate which WoW triggers should be enabled. This is also
 *	used by %NL80211_CMD_GET_WOWLAN to get the currently enabled WoWLAN
 *	triggers.
 *
 * @NL80211_ATTR_SCHED_SCAN_INTERVAL: Interval between scheduled scan
 *	cycles, in msecs.
 *
 * @NL80211_ATTR_SCHED_SCAN_MATCH: Nested attribute with one or more
 *	sets of attributes to match during scheduled scans.  Only BSSs
 *	that match any of the sets will be reported.  These are
 *	pass-thru filter rules.
 *	For a match to succeed, the BSS must match all attributes of a
 *	set.  Since not every hardware supports matching all types of
 *	attributes, there is no guarantee that the reported BSSs are
 *	fully complying with the match sets and userspace needs to be
 *	able to ignore them by itself.
 *	Thus, the implementation is somewhat hardware-dependent, but
 *	this is only an optimization and the userspace application
 *	needs to handle all the non-filtered results anyway.
 *	If the match attributes don't make sense when combined with
 *	the values passed in @NL80211_ATTR_SCAN_SSIDS (eg. if an SSID
 *	is included in the probe request, but the match attributes
 *	will never let it go through), -EINVAL may be returned.
 *	If ommited, no filtering is done.
 *
 * @NL80211_ATTR_INTERFACE_COMBINATIONS: Nested attribute listing the supported
 *	interface combinations. In each nested item, it contains attributes
 *	defined in &enum nl80211_if_combination_attrs.
 * @NL80211_ATTR_SOFTWARE_IFTYPES: Nested attribute (just like
 *	%NL80211_ATTR_SUPPORTED_IFTYPES) containing the interface types that
 *	are managed in software: interfaces of these types aren't subject to
 *	any restrictions in their number or combinations.
 *
 * @NL80211_ATTR_REKEY_DATA: nested attribute containing the information
 *	necessary for GTK rekeying in the device, see &enum nl80211_rekey_data.
 *
 * @NL80211_ATTR_SCAN_SUPP_RATES: rates per to be advertised as supported in scan,
 *	nested array attribute containing an entry for each band, with the entry
 *	being a list of supported rates as defined by IEEE 802.11 7.3.2.2 but
 *	without the length restriction (at most %NL80211_MAX_SUPP_RATES).
 *
 * @NL80211_ATTR_HIDDEN_SSID: indicates whether SSID is to be hidden from Beacon
 *	and Probe Response (when response to wildcard Probe Request); see
 *	&enum nl80211_hidden_ssid, represented as a u32
 *
 * @NL80211_ATTR_IE_PROBE_RESP: Information element(s) for Probe Response frame.
 *	This is used with %NL80211_CMD_NEW_BEACON and %NL80211_CMD_SET_BEACON to
 *	provide extra IEs (e.g., WPS/P2P IE) into Probe Response frames when the
 *	driver (or firmware) replies to Probe Request frames.
 * @NL80211_ATTR_IE_ASSOC_RESP: Information element(s) for (Re)Association
 *	Response frames. This is used with %NL80211_CMD_NEW_BEACON and
 *	%NL80211_CMD_SET_BEACON to provide extra IEs (e.g., WPS/P2P IE) into
 *	(Re)Association Response frames when the driver (or firmware) replies to
 *	(Re)Association Request frames.
 *
 * @NL80211_ATTR_STA_WME: Nested attribute containing the wme configuration
 *	of the station, see &enum nl80211_sta_wme_attr.
 * @NL80211_ATTR_SUPPORT_AP_UAPSD: the device supports uapsd when working
 *	as AP.
 *
 * @NL80211_ATTR_ROAM_SUPPORT: Indicates whether the firmware is capable of
 *	roaming to another AP in the same ESS if the signal lever is low.
 *
 * @NL80211_ATTR_PMKSA_CANDIDATE: Nested attribute containing the PMKSA caching
 *	candidate information, see &enum nl80211_pmksa_candidate_attr.
 *
 * @NL80211_ATTR_TX_NO_CCK_RATE: Indicates whether to use CCK rate or not
 *	for management frames transmission. In order to avoid p2p probe/action
 *	frames are being transmitted at CCK rate in 2GHz band, the user space
 *	applications use this attribute.
 *	This attribute is used with %NL80211_CMD_TRIGGER_SCAN and
 *	%NL80211_CMD_FRAME commands.
 *
 * @NL80211_ATTR_TDLS_ACTION: Low level TDLS action code (e.g. link setup
 *	request, link setup confirm, link teardown, etc.). Values are
 *	described in the TDLS (802.11z) specification.
 * @NL80211_ATTR_TDLS_DIALOG_TOKEN: Non-zero token for uniquely identifying a
 *	TDLS conversation between two devices.
 * @NL80211_ATTR_TDLS_OPERATION: High level TDLS operation; see
 *	&enum nl80211_tdls_operation, represented as a u8.
 * @NL80211_ATTR_TDLS_SUPPORT: A flag indicating the device can operate
 *	as a TDLS peer sta.
 * @NL80211_ATTR_TDLS_EXTERNAL_SETUP: The TDLS discovery/setup and teardown
 *	procedures should be performed by sending TDLS packets via
 *	%NL80211_CMD_TDLS_MGMT. Otherwise %NL80211_CMD_TDLS_OPER should be
 *	used for asking the driver to perform a TDLS operation.
 *
 * @NL80211_ATTR_DEVICE_AP_SME: This u32 attribute may be listed for devices
 *	that have AP support to indicate that they have the AP SME integrated
 *	with support for the features listed in this attribute, see
 *	&enum nl80211_ap_sme_features.
 *
 * @NL80211_ATTR_DONT_WAIT_FOR_ACK: Used with %NL80211_CMD_FRAME, this tells
 *	the driver to not wait for an acknowledgement. Note that due to this,
 *	it will also not give a status callback nor return a cookie. This is
 *	mostly useful for probe responses to save airtime.
 *
 * @NL80211_ATTR_FEATURE_FLAGS: This u32 attribute contains flags from
 *	&enum nl80211_feature_flags and is advertised in wiphy information.
 * @NL80211_ATTR_PROBE_RESP_OFFLOAD: Indicates that the HW responds to probe
 *	requests while operating in AP-mode.
 *	This attribute holds a bitmap of the supported protocols for
 *	offloading (see &enum nl80211_probe_resp_offload_support_attr).
 *
 * @NL80211_ATTR_PROBE_RESP: Probe Response template data. Contains the entire
 *	probe-response frame. The DA field in the 802.11 header is zero-ed out,
 *	to be filled by the FW.
 * @NL80211_ATTR_DISABLE_HT:  Force HT capable interfaces to disable
 *      this feature.  Currently, only supported in mac80211 drivers.
 * @NL80211_ATTR_HT_CAPABILITY_MASK: Specify which bits of the
 *      ATTR_HT_CAPABILITY to which attention should be paid.
 *      Currently, only mac80211 NICs support this feature.
 *      The values that may be configured are:
 *       MCS rates, MAX-AMSDU, HT-20-40 and HT_CAP_SGI_40
 *       AMPDU density and AMPDU factor.
 *      All values are treated as suggestions and may be ignored
 *      by the driver as required.  The actual values may be seen in
 *      the station debugfs ht_caps file.
 *
 * @NL80211_ATTR_DFS_REGION: region for regulatory rules which this country
 *    abides to when initiating radiation on DFS channels. A country maps
 *    to one DFS region.
 *
 * @NL80211_ATTR_NOACK_MAP: This u16 bitmap contains the No Ack Policy of
 *      up to 16 TIDs.
 *
 * @NL80211_ATTR_INACTIVITY_TIMEOUT: timeout value in seconds, this can be
 *	used by the drivers which has MLME in firmware and does not have support
 *	to report per station tx/rx activity to free up the staion entry from
 *	the list. This needs to be used when the driver advertises the
 *	capability to timeout the stations.
 *
 * @NL80211_ATTR_RX_SIGNAL_DBM: signal strength in dBm (as a 32-bit int);
 *	this attribute is (depending on the driver capabilities) added to
 *	received frames indicated with %NL80211_CMD_FRAME.
 *
 * @NL80211_ATTR_BG_SCAN_PERIOD: Background scan period in seconds
 *      or 0 to disable background scan.
 *
 * @NL80211_ATTR_USER_REG_HINT_TYPE: type of regulatory hint passed from
 *	userspace. If unset it is assumed the hint comes directly from
 *	a user. If set code could specify exactly what type of source
 *	was used to provide the hint. For the different types of
 *	allowed user regulatory hints see nl80211_user_reg_hint_type.
 *
 * @NL80211_ATTR_CONN_FAILED_REASON: The reason for which AP has rejected
 *	the connection request from a station. nl80211_connect_failed_reason
 *	enum has different reasons of connection failure.
 *
 * @NL80211_ATTR_SAE_DATA: SAE elements in Authentication frames. This starts
 *	with the Authentication transaction sequence number field.
 *
 * @NL80211_ATTR_VHT_CAPABILITY: VHT Capability information element (from
 *	association request when used with NL80211_CMD_NEW_STATION)
 *
 * @NL80211_ATTR_SCAN_FLAGS: scan request control flags (u32)
 *
 * @NL80211_ATTR_P2P_CTWINDOW: P2P GO Client Traffic Window (u8), used with
 *	the START_AP and SET_BSS commands
 * @NL80211_ATTR_P2P_OPPPS: P2P GO opportunistic PS (u8), used with the
 *	START_AP and SET_BSS commands. This can have the values 0 or 1;
 *	if not given in START_AP 0 is assumed, if not given in SET_BSS
 *	no change is made.
 *
 * @NL80211_ATTR_LOCAL_MESH_POWER_MODE: local mesh STA link-specific power mode
 *	defined in &enum nl80211_mesh_power_mode.
 *
 * @NL80211_ATTR_ACL_POLICY: ACL policy, see &enum nl80211_acl_policy,
 *	carried in a u32 attribute
 *
 * @NL80211_ATTR_MAC_ADDRS: Array of nested MAC addresses, used for
 *	MAC ACL.
 *
 * @NL80211_ATTR_MAC_ACL_MAX: u32 attribute to advertise the maximum
 *	number of MAC addresses that a device can support for MAC
 *	ACL.
 *
 * @NL80211_ATTR_RADAR_EVENT: Type of radar event for notification to userspace,
 *	contains a value of enum nl80211_radar_event (u32).
 *
 * @NL80211_ATTR_EXT_CAPA: 802.11 extended capabilities that the kernel driver
 *	has and handles. The format is the same as the IE contents. See
 *	802.11-2012 8.4.2.29 for more information.
 * @NL80211_ATTR_EXT_CAPA_MASK: Extended capabilities that the kernel driver
 *	has set in the %NL80211_ATTR_EXT_CAPA value, for multibit fields.
 *
 * @NL80211_ATTR_STA_CAPABILITY: Station capabilities (u16) are advertised to
 *	the driver, e.g., to enable TDLS power save (PU-APSD).
 *
 * @NL80211_ATTR_STA_EXT_CAPABILITY: Station extended capabilities are
 *	advertised to the driver, e.g., to enable TDLS off channel operations
 *	and PU-APSD.
 *
 * @NL80211_ATTR_PROTOCOL_FEATURES: global nl80211 feature flags, see
 *	&enum nl80211_protocol_features, the attribute is a u32.
 *
 * @NL80211_ATTR_SPLIT_WIPHY_DUMP: flag attribute, userspace supports
 *	receiving the data for a single wiphy split across multiple
 *	messages, given with wiphy dump message
 *
 * @NL80211_ATTR_MAX: highest attribute number currently defined
 * @__NL80211_ATTR_AFTER_LAST: internal use
 */
enum nl80211_attrs {
/* don't change the order or add anything between, this is ABI! */
	NL80211_ATTR_UNSPEC,

	NL80211_ATTR_WIPHY,
	NL80211_ATTR_WIPHY_NAME,

	NL80211_ATTR_IFINDEX,
	NL80211_ATTR_IFNAME,
	NL80211_ATTR_IFTYPE,

	NL80211_ATTR_MAC,

	NL80211_ATTR_KEY_DATA,
	NL80211_ATTR_KEY_IDX,
	NL80211_ATTR_KEY_CIPHER,
	NL80211_ATTR_KEY_SEQ,
	NL80211_ATTR_KEY_DEFAULT,

	NL80211_ATTR_BEACON_INTERVAL,
	NL80211_ATTR_DTIM_PERIOD,
	NL80211_ATTR_BEACON_HEAD,
	NL80211_ATTR_BEACON_TAIL,

	NL80211_ATTR_STA_AID,
	NL80211_ATTR_STA_FLAGS,
	NL80211_ATTR_STA_LISTEN_INTERVAL,
	NL80211_ATTR_STA_SUPPORTED_RATES,
	NL80211_ATTR_STA_VLAN,
	NL80211_ATTR_STA_INFO,

	NL80211_ATTR_WIPHY_BANDS,

	NL80211_ATTR_MNTR_FLAGS,

	NL80211_ATTR_MESH_ID,
	NL80211_ATTR_STA_PLINK_ACTION,
	NL80211_ATTR_MPATH_NEXT_HOP,
	NL80211_ATTR_MPATH_INFO,

	NL80211_ATTR_BSS_CTS_PROT,
	NL80211_ATTR_BSS_SHORT_PREAMBLE,
	NL80211_ATTR_BSS_SHORT_SLOT_TIME,

	NL80211_ATTR_HT_CAPABILITY,

	NL80211_ATTR_SUPPORTED_IFTYPES,

	NL80211_ATTR_REG_ALPHA2,
	NL80211_ATTR_REG_RULES,

	NL80211_ATTR_MESH_CONFIG,

	NL80211_ATTR_BSS_BASIC_RATES,

	NL80211_ATTR_WIPHY_TXQ_PARAMS,
	NL80211_ATTR_WIPHY_FREQ,
	NL80211_ATTR_WIPHY_CHANNEL_TYPE,

	NL80211_ATTR_KEY_DEFAULT_MGMT,

	NL80211_ATTR_MGMT_SUBTYPE,
	NL80211_ATTR_IE,

	NL80211_ATTR_MAX_NUM_SCAN_SSIDS,

	NL80211_ATTR_SCAN_FREQUENCIES,
	NL80211_ATTR_SCAN_SSIDS,
	NL80211_ATTR_GENERATION, /* replaces old SCAN_GENERATION */
	NL80211_ATTR_BSS,

	NL80211_ATTR_REG_INITIATOR,
	NL80211_ATTR_REG_TYPE,

	NL80211_ATTR_SUPPORTED_COMMANDS,

	NL80211_ATTR_FRAME,
	NL80211_ATTR_SSID,
	NL80211_ATTR_AUTH_TYPE,
	NL80211_ATTR_REASON_CODE,

	NL80211_ATTR_KEY_TYPE,

	NL80211_ATTR_MAX_SCAN_IE_LEN,
	NL80211_ATTR_CIPHER_SUITES,

	NL80211_ATTR_FREQ_BEFORE,
	NL80211_ATTR_FREQ_AFTER,

	NL80211_ATTR_FREQ_FIXED,


	NL80211_ATTR_WIPHY_RETRY_SHORT,
	NL80211_ATTR_WIPHY_RETRY_LONG,
	NL80211_ATTR_WIPHY_FRAG_THRESHOLD,
	NL80211_ATTR_WIPHY_RTS_THRESHOLD,

	NL80211_ATTR_TIMED_OUT,

	NL80211_ATTR_USE_MFP,

	NL80211_ATTR_STA_FLAGS2,

	NL80211_ATTR_CONTROL_PORT,

	NL80211_ATTR_TESTDATA,

	NL80211_ATTR_PRIVACY,

	NL80211_ATTR_DISCONNECTED_BY_AP,
	NL80211_ATTR_STATUS_CODE,

	NL80211_ATTR_CIPHER_SUITES_PAIRWISE,
	NL80211_ATTR_CIPHER_SUITE_GROUP,
	NL80211_ATTR_WPA_VERSIONS,
	NL80211_ATTR_AKM_SUITES,

	NL80211_ATTR_REQ_IE,
	NL80211_ATTR_RESP_IE,

	NL80211_ATTR_PREV_BSSID,

	NL80211_ATTR_KEY,
	NL80211_ATTR_KEYS,

	NL80211_ATTR_PID,

	NL80211_ATTR_4ADDR,

	NL80211_ATTR_SURVEY_INFO,

	NL80211_ATTR_PMKID,
	NL80211_ATTR_MAX_NUM_PMKIDS,

	NL80211_ATTR_DURATION,

	NL80211_ATTR_COOKIE,

	NL80211_ATTR_WIPHY_COVERAGE_CLASS,

	NL80211_ATTR_TX_RATES,

	NL80211_ATTR_FRAME_MATCH,

	NL80211_ATTR_ACK,

	NL80211_ATTR_PS_STATE,

	NL80211_ATTR_CQM,

	NL80211_ATTR_LOCAL_STATE_CHANGE,

	NL80211_ATTR_AP_ISOLATE,

	NL80211_ATTR_WIPHY_TX_POWER_SETTING,
	NL80211_ATTR_WIPHY_TX_POWER_LEVEL,

	NL80211_ATTR_TX_FRAME_TYPES,
	NL80211_ATTR_RX_FRAME_TYPES,
	NL80211_ATTR_FRAME_TYPE,

	NL80211_ATTR_CONTROL_PORT_ETHERTYPE,
	NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT,

	NL80211_ATTR_SUPPORT_IBSS_RSN,

	NL80211_ATTR_WIPHY_ANTENNA_TX,
	NL80211_ATTR_WIPHY_ANTENNA_RX,

	NL80211_ATTR_MCAST_RATE,

	NL80211_ATTR_OFFCHANNEL_TX_OK,

	NL80211_ATTR_BSS_HT_OPMODE,

	NL80211_ATTR_KEY_DEFAULT_TYPES,

	NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION,

	NL80211_ATTR_MESH_SETUP,

	NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX,
	NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX,

	NL80211_ATTR_SUPPORT_MESH_AUTH,
	NL80211_ATTR_STA_PLINK_STATE,

	NL80211_ATTR_WOWLAN_TRIGGERS,
	NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED,

	NL80211_ATTR_SCHED_SCAN_INTERVAL,

	NL80211_ATTR_INTERFACE_COMBINATIONS,
	NL80211_ATTR_SOFTWARE_IFTYPES,

	NL80211_ATTR_REKEY_DATA,

	NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS,
	NL80211_ATTR_MAX_SCHED_SCAN_IE_LEN,

	NL80211_ATTR_SCAN_SUPP_RATES,

	NL80211_ATTR_HIDDEN_SSID,

	NL80211_ATTR_IE_PROBE_RESP,
	NL80211_ATTR_IE_ASSOC_RESP,

	NL80211_ATTR_STA_WME,
	NL80211_ATTR_SUPPORT_AP_UAPSD,

	NL80211_ATTR_ROAM_SUPPORT,

	NL80211_ATTR_SCHED_SCAN_MATCH,
	NL80211_ATTR_MAX_MATCH_SETS,

	NL80211_ATTR_PMKSA_CANDIDATE,

	NL80211_ATTR_TX_NO_CCK_RATE,

	NL80211_ATTR_TDLS_ACTION,
	NL80211_ATTR_TDLS_DIALOG_TOKEN,
	NL80211_ATTR_TDLS_OPERATION,
	NL80211_ATTR_TDLS_SUPPORT,
	NL80211_ATTR_TDLS_EXTERNAL_SETUP,

	NL80211_ATTR_DEVICE_AP_SME,

	NL80211_ATTR_DONT_WAIT_FOR_ACK,

	NL80211_ATTR_FEATURE_FLAGS,

	NL80211_ATTR_PROBE_RESP_OFFLOAD,

	NL80211_ATTR_PROBE_RESP,

	NL80211_ATTR_DFS_REGION,

	NL80211_ATTR_DISABLE_HT,
	NL80211_ATTR_HT_CAPABILITY_MASK,

	NL80211_ATTR_NOACK_MAP,

	NL80211_ATTR_INACTIVITY_TIMEOUT,

	NL80211_ATTR_RX_SIGNAL_DBM,

	NL80211_ATTR_BG_SCAN_PERIOD,

	NL80211_ATTR_WDEV,

	NL80211_ATTR_USER_REG_HINT_TYPE,

	NL80211_ATTR_CONN_FAILED_REASON,

	NL80211_ATTR_SAE_DATA,

	NL80211_ATTR_VHT_CAPABILITY,

	NL80211_ATTR_SCAN_FLAGS,

	NL80211_ATTR_CHANNEL_WIDTH,
	NL80211_ATTR_CENTER_FREQ1,
	NL80211_ATTR_CENTER_FREQ2,

	NL80211_ATTR_P2P_CTWINDOW,
	NL80211_ATTR_P2P_OPPPS,

	NL80211_ATTR_LOCAL_MESH_POWER_MODE,

	NL80211_ATTR_ACL_POLICY,

	NL80211_ATTR_MAC_ADDRS,

	NL80211_ATTR_MAC_ACL_MAX,

	NL80211_ATTR_RADAR_EVENT,

	NL80211_ATTR_EXT_CAPA,
	NL80211_ATTR_EXT_CAPA_MASK,

	NL80211_ATTR_STA_CAPABILITY,
	NL80211_ATTR_STA_EXT_CAPABILITY,

	NL80211_ATTR_PROTOCOL_FEATURES,
	NL80211_ATTR_SPLIT_WIPHY_DUMP,

	/* add attributes here, update the policy in nl80211.c */

	__NL80211_ATTR_AFTER_LAST,
	NL80211_ATTR_MAX = __NL80211_ATTR_AFTER_LAST - 1
};

/* source-level API compatibility */
#define NL80211_ATTR_SCAN_GENERATION NL80211_ATTR_GENERATION
#define	NL80211_ATTR_MESH_PARAMS NL80211_ATTR_MESH_CONFIG

/*
 * Allow user space programs to use #ifdef on new attributes by defining them
 * here
 */
#define NL80211_CMD_CONNECT NL80211_CMD_CONNECT
#define NL80211_ATTR_HT_CAPABILITY NL80211_ATTR_HT_CAPABILITY
#define NL80211_ATTR_BSS_BASIC_RATES NL80211_ATTR_BSS_BASIC_RATES
#define NL80211_ATTR_WIPHY_TXQ_PARAMS NL80211_ATTR_WIPHY_TXQ_PARAMS
#define NL80211_ATTR_WIPHY_FREQ NL80211_ATTR_WIPHY_FREQ
#define NL80211_ATTR_WIPHY_CHANNEL_TYPE NL80211_ATTR_WIPHY_CHANNEL_TYPE
#define NL80211_ATTR_MGMT_SUBTYPE NL80211_ATTR_MGMT_SUBTYPE
#define NL80211_ATTR_IE NL80211_ATTR_IE
#define NL80211_ATTR_REG_INITIATOR NL80211_ATTR_REG_INITIATOR
#define NL80211_ATTR_REG_TYPE NL80211_ATTR_REG_TYPE
#define NL80211_ATTR_FRAME NL80211_ATTR_FRAME
#define NL80211_ATTR_SSID NL80211_ATTR_SSID
#define NL80211_ATTR_AUTH_TYPE NL80211_ATTR_AUTH_TYPE
#define NL80211_ATTR_REASON_CODE NL80211_ATTR_REASON_CODE
#define NL80211_ATTR_CIPHER_SUITES_PAIRWISE NL80211_ATTR_CIPHER_SUITES_PAIRWISE
#define NL80211_ATTR_CIPHER_SUITE_GROUP NL80211_ATTR_CIPHER_SUITE_GROUP
#define NL80211_ATTR_WPA_VERSIONS NL80211_ATTR_WPA_VERSIONS
#define NL80211_ATTR_AKM_SUITES NL80211_ATTR_AKM_SUITES
#define NL80211_ATTR_KEY NL80211_ATTR_KEY
#define NL80211_ATTR_KEYS NL80211_ATTR_KEYS
#define NL80211_ATTR_FEATURE_FLAGS NL80211_ATTR_FEATURE_FLAGS

#define NL80211_MAX_SUPP_RATES			32
#define NL80211_MAX_SUPP_HT_RATES		77
#define NL80211_MAX_SUPP_REG_RULES		32
#define NL80211_TKIP_DATA_OFFSET_ENCR_KEY	0
#define NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY	16
#define NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY	24
#define NL80211_HT_CAPABILITY_LEN		26
#define NL80211_VHT_CAPABILITY_LEN		12

#define NL80211_MAX_NR_CIPHER_SUITES		5
#define NL80211_MAX_NR_AKM_SUITES		2

#define NL80211_MIN_REMAIN_ON_CHANNEL_TIME	10

/* default RSSI threshold for scan results if none specified. */
#define NL80211_SCAN_RSSI_THOLD_OFF		-300

#define NL80211_CQM_TXE_MAX_INTVL		1800

/**
 * enum nl80211_iftype - (virtual) interface types
 *
 * @NL80211_IFTYPE_UNSPECIFIED: unspecified type, driver decides
 * @NL80211_IFTYPE_ADHOC: independent BSS member
 * @NL80211_IFTYPE_STATION: managed BSS member
 * @NL80211_IFTYPE_AP: access point
 * @NL80211_IFTYPE_AP_VLAN: VLAN interface for access points; VLAN interfaces
 *	are a bit special in that they must always be tied to a pre-existing
 *	AP type interface.
 * @NL80211_IFTYPE_WDS: wireless distribution interface
 * @NL80211_IFTYPE_MONITOR: monitor interface receiving all frames
 * @NL80211_IFTYPE_MESH_POINT: mesh point
 * @NL80211_IFTYPE_P2P_CLIENT: P2P client
 * @NL80211_IFTYPE_P2P_GO: P2P group owner
 * @NL80211_IFTYPE_P2P_DEVICE: P2P device interface type, this is not a netdev
 *	and therefore can't be created in the normal ways, use the
 *	%NL80211_CMD_START_P2P_DEVICE and %NL80211_CMD_STOP_P2P_DEVICE
 *	commands to create and destroy one
 * @NL80211_IFTYPE_MAX: highest interface type number currently defined
 * @NUM_NL80211_IFTYPES: number of defined interface types
 *
 * These values are used with the %NL80211_ATTR_IFTYPE
 * to set the type of an interface.
 *
 */
enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
	NL80211_IFTYPE_ADHOC,
	NL80211_IFTYPE_STATION,
	NL80211_IFTYPE_AP,
	NL80211_IFTYPE_AP_VLAN,
	NL80211_IFTYPE_WDS,
	NL80211_IFTYPE_MONITOR,
	NL80211_IFTYPE_MESH_POINT,
	NL80211_IFTYPE_P2P_CLIENT,
	NL80211_IFTYPE_P2P_GO,
	NL80211_IFTYPE_P2P_DEVICE,

	/* keep last */
	NUM_NL80211_IFTYPES,
	NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1
};

/**
 * enum nl80211_sta_flags - station flags
 *
 * Station flags. When a station is added to an AP interface, it is
 * assumed to be already associated (and hence authenticated.)
 *
 * @__NL80211_STA_FLAG_INVALID: attribute number 0 is reserved
 * @NL80211_STA_FLAG_AUTHORIZED: station is authorized (802.1X)
 * @NL80211_STA_FLAG_SHORT_PREAMBLE: station is capable of receiving frames
 *	with short barker preamble
 * @NL80211_STA_FLAG_WME: station is WME/QoS capable
 * @NL80211_STA_FLAG_MFP: station uses management frame protection
 * @NL80211_STA_FLAG_AUTHENTICATED: station is authenticated
 * @NL80211_STA_FLAG_TDLS_PEER: station is a TDLS peer -- this flag should
 *	only be used in managed mode (even in the flags mask). Note that the
 *	flag can't be changed, it is only valid while adding a station, and
 *	attempts to change it will silently be ignored (rather than rejected
 *	as errors.)
 * @NL80211_STA_FLAG_ASSOCIATED: station is associated; used with drivers
 *	that support %NL80211_FEATURE_FULL_AP_CLIENT_STATE to transition a
 *	previously added station into associated state
 * @NL80211_STA_FLAG_MAX: highest station flag number currently defined
 * @__NL80211_STA_FLAG_AFTER_LAST: internal use
 */
enum nl80211_sta_flags {
	__NL80211_STA_FLAG_INVALID,
	NL80211_STA_FLAG_AUTHORIZED,
	NL80211_STA_FLAG_SHORT_PREAMBLE,
	NL80211_STA_FLAG_WME,
	NL80211_STA_FLAG_MFP,
	NL80211_STA_FLAG_AUTHENTICATED,
	NL80211_STA_FLAG_TDLS_PEER,
	NL80211_STA_FLAG_ASSOCIATED,

	/* keep last */
	__NL80211_STA_FLAG_AFTER_LAST,
	NL80211_STA_FLAG_MAX = __NL80211_STA_FLAG_AFTER_LAST - 1
};

#define NL80211_STA_FLAG_MAX_OLD_API	NL80211_STA_FLAG_TDLS_PEER

/**
 * struct nl80211_sta_flag_update - station flags mask/set
 * @mask: mask of station flags to set
 * @set: which values to set them to
 *
 * Both mask and set contain bits as per &enum nl80211_sta_flags.
 */
struct nl80211_sta_flag_update {
	__u32 mask;
	__u32 set;
} __attribute__((packed));

/**
 * enum nl80211_rate_info - bitrate information
 *
 * These attribute types are used with %NL80211_STA_INFO_TXRATE
 * when getting information about the bitrate of a station.
 * There are 2 attributes for bitrate, a legacy one that represents
 * a 16-bit value, and new one that represents a 32-bit value.
 * If the rate value fits into 16 bit, both attributes are reported
 * with the same value. If the rate is too high to fit into 16 bits
 * (>6.5535Gbps) only 32-bit attribute is included.
 * User space tools encouraged to use the 32-bit attribute and fall
 * back to the 16-bit one for compatibility with older kernels.
 *
 * @__NL80211_RATE_INFO_INVALID: attribute number 0 is reserved
 * @NL80211_RATE_INFO_BITRATE: total bitrate (u16, 100kbit/s)
 * @NL80211_RATE_INFO_MCS: mcs index for 802.11n (u8)
 * @NL80211_RATE_INFO_40_MHZ_WIDTH: 40 MHz dualchannel bitrate
 * @NL80211_RATE_INFO_SHORT_GI: 400ns guard interval
 * @NL80211_RATE_INFO_BITRATE32: total bitrate (u32, 100kbit/s)
 * @NL80211_RATE_INFO_MAX: highest rate_info number currently defined
 * @NL80211_RATE_INFO_VHT_MCS: MCS index for VHT (u8)
 * @NL80211_RATE_INFO_VHT_NSS: number of streams in VHT (u8)
 * @NL80211_RATE_INFO_80_MHZ_WIDTH: 80 MHz VHT rate
 * @NL80211_RATE_INFO_80P80_MHZ_WIDTH: 80+80 MHz VHT rate
 * @NL80211_RATE_INFO_160_MHZ_WIDTH: 160 MHz VHT rate
 * @__NL80211_RATE_INFO_AFTER_LAST: internal use
 */
enum nl80211_rate_info {
	__NL80211_RATE_INFO_INVALID,
	NL80211_RATE_INFO_BITRATE,
	NL80211_RATE_INFO_MCS,
	NL80211_RATE_INFO_40_MHZ_WIDTH,
	NL80211_RATE_INFO_SHORT_GI,
	NL80211_RATE_INFO_BITRATE32,
	NL80211_RATE_INFO_VHT_MCS,
	NL80211_RATE_INFO_VHT_NSS,
	NL80211_RATE_INFO_80_MHZ_WIDTH,
	NL80211_RATE_INFO_80P80_MHZ_WIDTH,
	NL80211_RATE_INFO_160_MHZ_WIDTH,

	/* keep last */
	__NL80211_RATE_INFO_AFTER_LAST,
	NL80211_RATE_INFO_MAX = __NL80211_RATE_INFO_AFTER_LAST - 1
};

/**
 * enum nl80211_sta_bss_param - BSS information collected by STA
 *
 * These attribute types are used with %NL80211_STA_INFO_BSS_PARAM
 * when getting information about the bitrate of a station.
 *
 * @__NL80211_STA_BSS_PARAM_INVALID: attribute number 0 is reserved
 * @NL80211_STA_BSS_PARAM_CTS_PROT: whether CTS protection is enabled (flag)
 * @NL80211_STA_BSS_PARAM_SHORT_PREAMBLE:  whether short preamble is enabled
 *	(flag)
 * @NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME:  whether short slot time is enabled
 *	(flag)
 * @NL80211_STA_BSS_PARAM_DTIM_PERIOD: DTIM period for beaconing (u8)
 * @NL80211_STA_BSS_PARAM_BEACON_INTERVAL: Beacon interval (u16)
 * @NL80211_STA_BSS_PARAM_MAX: highest sta_bss_param number currently defined
 * @__NL80211_STA_BSS_PARAM_AFTER_LAST: internal use
 */
enum nl80211_sta_bss_param {
	__NL80211_STA_BSS_PARAM_INVALID,
	NL80211_STA_BSS_PARAM_CTS_PROT,
	NL80211_STA_BSS_PARAM_SHORT_PREAMBLE,
	NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME,
	NL80211_STA_BSS_PARAM_DTIM_PERIOD,
	NL80211_STA_BSS_PARAM_BEACON_INTERVAL,

	/* keep last */
	__NL80211_STA_BSS_PARAM_AFTER_LAST,
	NL80211_STA_BSS_PARAM_MAX = __NL80211_STA_BSS_PARAM_AFTER_LAST - 1
};

/**
 * enum nl80211_sta_info - station information
 *
 * These attribute types are used with %NL80211_ATTR_STA_INFO
 * when getting information about a station.
 *
 * @__NL80211_STA_INFO_INVALID: attribute number 0 is reserved
 * @NL80211_STA_INFO_INACTIVE_TIME: time since last activity (u32, msecs)
 * @NL80211_STA_INFO_RX_BYTES: total received bytes (u32, from this station)
 * @NL80211_STA_INFO_TX_BYTES: total transmitted bytes (u32, to this station)
 * @NL80211_STA_INFO_RX_BYTES64: total received bytes (u64, from this station)
 * @NL80211_STA_INFO_TX_BYTES64: total transmitted bytes (u64, to this station)
 * @NL80211_STA_INFO_SIGNAL: signal strength of last received PPDU (u8, dBm)
 * @NL80211_STA_INFO_TX_BITRATE: current unicast tx rate, nested attribute
 * 	containing info as possible, see &enum nl80211_rate_info
 * @NL80211_STA_INFO_RX_PACKETS: total received packet (u32, from this station)
 * @NL80211_STA_INFO_TX_PACKETS: total transmitted packets (u32, to this
 *	station)
 * @NL80211_STA_INFO_TX_RETRIES: total retries (u32, to this station)
 * @NL80211_STA_INFO_TX_FAILED: total failed packets (u32, to this station)
 * @NL80211_STA_INFO_SIGNAL_AVG: signal strength average (u8, dBm)
 * @NL80211_STA_INFO_LLID: the station's mesh LLID
 * @NL80211_STA_INFO_PLID: the station's mesh PLID
 * @NL80211_STA_INFO_PLINK_STATE: peer link state for the station
 *	(see %enum nl80211_plink_state)
 * @NL80211_STA_INFO_RX_BITRATE: last unicast data frame rx rate, nested
 *	attribute, like NL80211_STA_INFO_TX_BITRATE.
 * @NL80211_STA_INFO_BSS_PARAM: current station's view of BSS, nested attribute
 *     containing info as possible, see &enum nl80211_sta_bss_param
 * @NL80211_STA_INFO_CONNECTED_TIME: time since the station is last connected
 * @NL80211_STA_INFO_STA_FLAGS: Contains a struct nl80211_sta_flag_update.
 * @NL80211_STA_INFO_BEACON_LOSS: count of times beacon loss was detected (u32)
 * @NL80211_STA_INFO_T_OFFSET: timing offset with respect to this STA (s64)
 * @NL80211_STA_INFO_LOCAL_PM: local mesh STA link-specific power mode
 * @NL80211_STA_INFO_PEER_PM: peer mesh STA link-specific power mode
 * @NL80211_STA_INFO_NONPEER_PM: neighbor mesh STA power save mode towards
 *	non-peer STA
 * @__NL80211_STA_INFO_AFTER_LAST: internal
 * @NL80211_STA_INFO_MAX: highest possible station info attribute
 */
enum nl80211_sta_info {
	__NL80211_STA_INFO_INVALID,
	NL80211_STA_INFO_INACTIVE_TIME,
	NL80211_STA_INFO_RX_BYTES,
	NL80211_STA_INFO_TX_BYTES,
	NL80211_STA_INFO_LLID,
	NL80211_STA_INFO_PLID,
	NL80211_STA_INFO_PLINK_STATE,
	NL80211_STA_INFO_SIGNAL,
	NL80211_STA_INFO_TX_BITRATE,
	NL80211_STA_INFO_RX_PACKETS,
	NL80211_STA_INFO_TX_PACKETS,
	NL80211_STA_INFO_TX_RETRIES,
	NL80211_STA_INFO_TX_FAILED,
	NL80211_STA_INFO_SIGNAL_AVG,
	NL80211_STA_INFO_RX_BITRATE,
	NL80211_STA_INFO_BSS_PARAM,
	NL80211_STA_INFO_CONNECTED_TIME,
	NL80211_STA_INFO_STA_FLAGS,
	NL80211_STA_INFO_BEACON_LOSS,
	NL80211_STA_INFO_T_OFFSET,
	NL80211_STA_INFO_LOCAL_PM,
	NL80211_STA_INFO_PEER_PM,
	NL80211_STA_INFO_NONPEER_PM,
	NL80211_STA_INFO_RX_BYTES64,
	NL80211_STA_INFO_TX_BYTES64,

	/* keep last */
	__NL80211_STA_INFO_AFTER_LAST,
	NL80211_STA_INFO_MAX = __NL80211_STA_INFO_AFTER_LAST - 1
};

/**
 * enum nl80211_mpath_flags - nl80211 mesh path flags
 *
 * @NL80211_MPATH_FLAG_ACTIVE: the mesh path is active
 * @NL80211_MPATH_FLAG_RESOLVING: the mesh path discovery process is running
 * @NL80211_MPATH_FLAG_SN_VALID: the mesh path contains a valid SN
 * @NL80211_MPATH_FLAG_FIXED: the mesh path has been manually set
 * @NL80211_MPATH_FLAG_RESOLVED: the mesh path discovery process succeeded
 */
enum nl80211_mpath_flags {
	NL80211_MPATH_FLAG_ACTIVE =	1<<0,
	NL80211_MPATH_FLAG_RESOLVING =	1<<1,
	NL80211_MPATH_FLAG_SN_VALID =	1<<2,
	NL80211_MPATH_FLAG_FIXED =	1<<3,
	NL80211_MPATH_FLAG_RESOLVED =	1<<4,
};

/**
 * enum nl80211_mpath_info - mesh path information
 *
 * These attribute types are used with %NL80211_ATTR_MPATH_INFO when getting
 * information about a mesh path.
 *
 * @__NL80211_MPATH_INFO_INVALID: attribute number 0 is reserved
 * @NL80211_MPATH_INFO_FRAME_QLEN: number of queued frames for this destination
 * @NL80211_MPATH_INFO_SN: destination sequence number
 * @NL80211_MPATH_INFO_METRIC: metric (cost) of this mesh path
 * @NL80211_MPATH_INFO_EXPTIME: expiration time for the path, in msec from now
 * @NL80211_MPATH_INFO_FLAGS: mesh path flags, enumerated in
 * 	&enum nl80211_mpath_flags;
 * @NL80211_MPATH_INFO_DISCOVERY_TIMEOUT: total path discovery timeout, in msec
 * @NL80211_MPATH_INFO_DISCOVERY_RETRIES: mesh path discovery retries
 * @NL80211_MPATH_INFO_MAX: highest mesh path information attribute number
 *	currently defind
 * @__NL80211_MPATH_INFO_AFTER_LAST: internal use
 */
enum nl80211_mpath_info {
	__NL80211_MPATH_INFO_INVALID,
	NL80211_MPATH_INFO_FRAME_QLEN,
	NL80211_MPATH_INFO_SN,
	NL80211_MPATH_INFO_METRIC,
	NL80211_MPATH_INFO_EXPTIME,
	NL80211_MPATH_INFO_FLAGS,
	NL80211_MPATH_INFO_DISCOVERY_TIMEOUT,
	NL80211_MPATH_INFO_DISCOVERY_RETRIES,

	/* keep last */
	__NL80211_MPATH_INFO_AFTER_LAST,
	NL80211_MPATH_INFO_MAX = __NL80211_MPATH_INFO_AFTER_LAST - 1
};

/**
 * enum nl80211_band_attr - band attributes
 * @__NL80211_BAND_ATTR_INVALID: attribute number 0 is reserved
 * @NL80211_BAND_ATTR_FREQS: supported frequencies in this band,
 *	an array of nested frequency attributes
 * @NL80211_BAND_ATTR_RATES: supported bitrates in this band,
 *	an array of nested bitrate attributes
 * @NL80211_BAND_ATTR_HT_MCS_SET: 16-byte attribute containing the MCS set as
 *	defined in 802.11n
 * @NL80211_BAND_ATTR_HT_CAPA: HT capabilities, as in the HT information IE
 * @NL80211_BAND_ATTR_HT_AMPDU_FACTOR: A-MPDU factor, as in 11n
 * @NL80211_BAND_ATTR_HT_AMPDU_DENSITY: A-MPDU density, as in 11n
 * @NL80211_BAND_ATTR_VHT_MCS_SET: 32-byte attribute containing the MCS set as
 *	defined in 802.11ac
 * @NL80211_BAND_ATTR_VHT_CAPA: VHT capabilities, as in the HT information IE
 * @NL80211_BAND_ATTR_MAX: highest band attribute currently defined
 * @__NL80211_BAND_ATTR_AFTER_LAST: internal use
 */
enum nl80211_band_attr {
	__NL80211_BAND_ATTR_INVALID,
	NL80211_BAND_ATTR_FREQS,
	NL80211_BAND_ATTR_RATES,

	NL80211_BAND_ATTR_HT_MCS_SET,
	NL80211_BAND_ATTR_HT_CAPA,
	NL80211_BAND_ATTR_HT_AMPDU_FACTOR,
	NL80211_BAND_ATTR_HT_AMPDU_DENSITY,

	NL80211_BAND_ATTR_VHT_MCS_SET,
	NL80211_BAND_ATTR_VHT_CAPA,

	/* keep last */
	__NL80211_BAND_ATTR_AFTER_LAST,
	NL80211_BAND_ATTR_MAX = __NL80211_BAND_ATTR_AFTER_LAST - 1
};

#define NL80211_BAND_ATTR_HT_CAPA NL80211_BAND_ATTR_HT_CAPA

/**
 * enum nl80211_frequency_attr - frequency attributes
 * @__NL80211_FREQUENCY_ATTR_INVALID: attribute number 0 is reserved
 * @NL80211_FREQUENCY_ATTR_FREQ: Frequency in MHz
 * @NL80211_FREQUENCY_ATTR_DISABLED: Channel is disabled in current
 *	regulatory domain.
 * @NL80211_FREQUENCY_ATTR_PASSIVE_SCAN: Only passive scanning is
 *	permitted on this channel in current regulatory domain.
 * @NL80211_FREQUENCY_ATTR_NO_IBSS: IBSS networks are not permitted
 *	on this channel in current regulatory domain.
 * @NL80211_FREQUENCY_ATTR_RADAR: Radar detection is mandatory
 *	on this channel in current regulatory domain.
 * @NL80211_FREQUENCY_ATTR_MAX_TX_POWER: Maximum transmission power in mBm
 *	(100 * dBm).
 * @NL80211_FREQUENCY_ATTR_DFS_STATE: current state for DFS
 *	(enum nl80211_dfs_state)
 * @NL80211_FREQUENCY_ATTR_DFS_TIME: time in miliseconds for how long
 *	this channel is in this DFS state.
 * @NL80211_FREQUENCY_ATTR_NO_HT40_MINUS: HT40- isn't possible with this
 *	channel as the control channel
 * @NL80211_FREQUENCY_ATTR_NO_HT40_PLUS: HT40+ isn't possible with this
 *	channel as the control channel
 * @NL80211_FREQUENCY_ATTR_NO_80MHZ: any 80 MHz channel using this channel
 *	as the primary or any of the secondary channels isn't possible,
 *	this includes 80+80 channels
 * @NL80211_FREQUENCY_ATTR_NO_160MHZ: any 160 MHz (but not 80+80) channel
 *	using this channel as the primary or any of the secondary channels
 *	isn't possible
 * @NL80211_FREQUENCY_ATTR_MAX: highest frequency attribute number
 *	currently defined
 * @__NL80211_FREQUENCY_ATTR_AFTER_LAST: internal use
 */
enum nl80211_frequency_attr {
	__NL80211_FREQUENCY_ATTR_INVALID,
	NL80211_FREQUENCY_ATTR_FREQ,
	NL80211_FREQUENCY_ATTR_DISABLED,
	NL80211_FREQUENCY_ATTR_PASSIVE_SCAN,
	NL80211_FREQUENCY_ATTR_NO_IBSS,
	NL80211_FREQUENCY_ATTR_RADAR,
	NL80211_FREQUENCY_ATTR_MAX_TX_POWER,
	NL80211_FREQUENCY_ATTR_DFS_STATE,
	NL80211_FREQUENCY_ATTR_DFS_TIME,
	NL80211_FREQUENCY_ATTR_NO_HT40_MINUS,
	NL80211_FREQUENCY_ATTR_NO_HT40_PLUS,
	NL80211_FREQUENCY_ATTR_NO_80MHZ,
	NL80211_FREQUENCY_ATTR_NO_160MHZ,

	/* keep last */
	__NL80211_FREQUENCY_ATTR_AFTER_LAST,
	NL80211_FREQUENCY_ATTR_MAX = __NL80211_FREQUENCY_ATTR_AFTER_LAST - 1
};

#define NL80211_FREQUENCY_ATTR_MAX_TX_POWER NL80211_FREQUENCY_ATTR_MAX_TX_POWER

/**
 * enum nl80211_bitrate_attr - bitrate attributes
 * @__NL80211_BITRATE_ATTR_INVALID: attribute number 0 is reserved
 * @NL80211_BITRATE_ATTR_RATE: Bitrate in units of 100 kbps
 * @NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE: Short preamble supported
 *	in 2.4 GHz band.
 * @NL80211_BITRATE_ATTR_MAX: highest bitrate attribute number
 *	currently defined
 * @__NL80211_BITRATE_ATTR_AFTER_LAST: internal use
 */
enum nl80211_bitrate_attr {
	__NL80211_BITRATE_ATTR_INVALID,
	NL80211_BITRATE_ATTR_RATE,
	NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE,

	/* keep last */
	__NL80211_BITRATE_ATTR_AFTER_LAST,
	NL80211_BITRATE_ATTR_MAX = __NL80211_BITRATE_ATTR_AFTER_LAST - 1
};

/**
 * enum nl80211_initiator - Indicates the initiator of a reg domain request
 * @NL80211_REGDOM_SET_BY_CORE: Core queried CRDA for a dynamic world
 * 	regulatory domain.
 * @NL80211_REGDOM_SET_BY_USER: User asked the wireless core to set the
 * 	regulatory domain.
 * @NL80211_REGDOM_SET_BY_DRIVER: a wireless drivers has hinted to the
 * 	wireless core it thinks its knows the regulatory domain we should be in.
 * @NL80211_REGDOM_SET_BY_COUNTRY_IE: the wireless core has received an
 * 	802.11 country information element with regulatory information it
 * 	thinks we should consider. cfg80211 only processes the country
 *	code from the IE, and relies on the regulatory domain information
 *	structure passed by userspace (CRDA) from our wireless-regdb.
 *	If a channel is enabled but the country code indicates it should
 *	be disabled we disable the channel and re-enable it upon disassociation.
 */
enum nl80211_reg_initiator {
	NL80211_REGDOM_SET_BY_CORE,
	NL80211_REGDOM_SET_BY_USER,
	NL80211_REGDOM_SET_BY_DRIVER,
	NL80211_REGDOM_SET_BY_COUNTRY_IE,
};

/**
 * enum nl80211_reg_type - specifies the type of regulatory domain
 * @NL80211_REGDOM_TYPE_COUNTRY: the regulatory domain set is one that pertains
 *	to a specific country. When this is set you can count on the
 *	ISO / IEC 3166 alpha2 country code being valid.
 * @NL80211_REGDOM_TYPE_WORLD: the regulatory set domain is the world regulatory
 * 	domain.
 * @NL80211_REGDOM_TYPE_CUSTOM_WORLD: the regulatory domain set is a custom
 * 	driver specific world regulatory domain. These do not apply system-wide
 * 	and are only applicable to the individual devices which have requested
 * 	them to be applied.
 * @NL80211_REGDOM_TYPE_INTERSECTION: the regulatory domain set is the product
 *	of an intersection between two regulatory domains -- the previously
 *	set regulatory domain on the system and the last accepted regulatory
 *	domain request to be processed.
 */
enum nl80211_reg_type {
	NL80211_REGDOM_TYPE_COUNTRY,
	NL80211_REGDOM_TYPE_WORLD,
	NL80211_REGDOM_TYPE_CUSTOM_WORLD,
	NL80211_REGDOM_TYPE_INTERSECTION,
};

/**
 * enum nl80211_reg_rule_attr - regulatory rule attributes
 * @__NL80211_REG_RULE_ATTR_INVALID: attribute number 0 is reserved
 * @NL80211_ATTR_REG_RULE_FLAGS: a set of flags which specify additional
 * 	considerations for a given frequency range. These are the
 * 	&enum nl80211_reg_rule_flags.
 * @NL80211_ATTR_FREQ_RANGE_START: starting frequencry for the regulatory
 * 	rule in KHz. This is not a center of frequency but an actual regulatory
 * 	band edge.
 * @NL80211_ATTR_FREQ_RANGE_END: ending frequency for the regulatory rule
 * 	in KHz. This is not a center a frequency but an actual regulatory
 * 	band edge.
 * @NL80211_ATTR_FREQ_RANGE_MAX_BW: maximum allowed bandwidth for this
 * 	frequency range, in KHz.
 * @NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN: the maximum allowed antenna gain
 * 	for a given frequency range. The value is in mBi (100 * dBi).
 * 	If you don't have one then don't send this.
 * @NL80211_ATTR_POWER_RULE_MAX_EIRP: the maximum allowed EIRP for
 * 	a given frequency range. The value is in mBm (100 * dBm).
 * @NL80211_REG_RULE_ATTR_MAX: highest regulatory rule attribute number
 *	currently defined
 * @__NL80211_REG_RULE_ATTR_AFTER_LAST: internal use
 */
enum nl80211_reg_rule_attr {
	__NL80211_REG_RULE_ATTR_INVALID,
	NL80211_ATTR_REG_RULE_FLAGS,

	NL80211_ATTR_FREQ_RANGE_START,
	NL80211_ATTR_FREQ_RANGE_END,
	NL80211_ATTR_FREQ_RANGE_MAX_BW,

	NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN,
	NL80211_ATTR_POWER_RULE_MAX_EIRP,

	/* keep last */
	__NL80211_REG_RULE_ATTR_AFTER_LAST,
	NL80211_REG_RULE_ATTR_MAX = __NL80211_REG_RULE_ATTR_AFTER_LAST - 1
};

/**
 * enum nl80211_sched_scan_match_attr - scheduled scan match attributes
 * @__NL80211_SCHED_SCAN_MATCH_ATTR_INVALID: attribute number 0 is reserved
 * @NL80211_SCHED_SCAN_MATCH_ATTR_SSID: SSID to be used for matching,
 * only report BSS with matching SSID.
 * @NL80211_SCHED_SCAN_MATCH_ATTR_RSSI: RSSI threshold (in dBm) for reporting a
 *	BSS in scan results. Filtering is turned off if not specified.
 * @NL80211_SCHED_SCAN_MATCH_ATTR_MAX: highest scheduled scan filter
 *	attribute number currently defined
 * @__NL80211_SCHED_SCAN_MATCH_ATTR_AFTER_LAST: internal use
 */
enum nl80211_sched_scan_match_attr {
	__NL80211_SCHED_SCAN_MATCH_ATTR_INVALID,

	NL80211_SCHED_SCAN_MATCH_ATTR_SSID,
	NL80211_SCHED_SCAN_MATCH_ATTR_RSSI,

	/* keep last */
	__NL80211_SCHED_SCAN_MATCH_ATTR_AFTER_LAST,
	NL80211_SCHED_SCAN_MATCH_ATTR_MAX =
		__NL80211_SCHED_SCAN_MATCH_ATTR_AFTER_LAST - 1
};

/* only for backward compatibility */
#define NL80211_ATTR_SCHED_SCAN_MATCH_SSID NL80211_SCHED_SCAN_MATCH_ATTR_SSID

/**
 * enum nl80211_reg_rule_flags - regulatory rule flags
 *
 * @NL80211_RRF_NO_OFDM: OFDM modulation not allowed
 * @NL80211_RRF_NO_CCK: CCK modulation not allowed
 * @NL80211_RRF_NO_INDOOR: indoor operation not allowed
 * @NL80211_RRF_NO_OUTDOOR: outdoor operation not allowed
 * @NL80211_RRF_DFS: DFS support is required to be used
 * @NL80211_RRF_PTP_ONLY: this is only for Point To Point links
 * @NL80211_RRF_PTMP_ONLY: this is only for Point To Multi Point links
 * @NL80211_RRF_PASSIVE_SCAN: passive scan is required
 * @NL80211_RRF_NO_IBSS: no IBSS is allowed
 */
enum nl80211_reg_rule_flags {
	NL80211_RRF_NO_OFDM		= 1<<0,
	NL80211_RRF_NO_CCK		= 1<<1,
	NL80211_RRF_NO_INDOOR		= 1<<2,
	NL80211_RRF_NO_OUTDOOR		= 1<<3,
	NL80211_RRF_DFS			= 1<<4,
	NL80211_RRF_PTP_ONLY		= 1<<5,
	NL80211_RRF_PTMP_ONLY		= 1<<6,
	NL80211_RRF_PASSIVE_SCAN	= 1<<7,
	NL80211_RRF_NO_IBSS		= 1<<8,
};

/**
 * enum nl80211_dfs_regions - regulatory DFS regions
 *
 * @NL80211_DFS_UNSET: Country has no DFS master region specified
 * @NL80211_DFS_FCC: Country follows DFS master rules from FCC
 * @NL80211_DFS_ETSI: Country follows DFS master rules from ETSI
 * @NL80211_DFS_JP: Country follows DFS master rules from JP/MKK/Telec
 */
enum nl80211_dfs_regions {
	NL80211_DFS_UNSET	= 0,
	NL80211_DFS_FCC		= 1,
	NL80211_DFS_ETSI	= 2,
	NL80211_DFS_JP		= 3,
};

/**
 * enum nl80211_user_reg_hint_type - type of user regulatory hint
 *
 * @NL80211_USER_REG_HINT_USER: a user sent the hint. This is always
 *	assumed if the attribute is not set.
 * @NL80211_USER_REG_HINT_CELL_BASE: the hint comes from a cellular
 *	base station. Device drivers that have been tested to work
 *	properly to support this type of hint can enable these hints
 *	by setting the NL80211_FEATURE_CELL_BASE_REG_HINTS feature
 *	capability on the struct wiphy. The wireless core will
 *	ignore all cell base station hints until at least one device
 *	present has been registered with the wireless core that
 *	has listed NL80211_FEATURE_CELL_BASE_REG_HINTS as a
 *	supported feature.
 */
enum nl80211_user_reg_hint_type {
	NL80211_USER_REG_HINT_USER	= 0,
	NL80211_USER_REG_HINT_CELL_BASE = 1,
};

/**
 * enum nl80211_survey_info - survey information
 *
 * These attribute types are used with %NL80211_ATTR_SURVEY_INFO
 * when getting information about a survey.
 *
 * @__NL80211_SURVEY_INFO_INVALID: attribute number 0 is reserved
 * @NL80211_SURVEY_INFO_FREQUENCY: center frequency of channel
 * @NL80211_SURVEY_INFO_NOISE: noise level of channel (u8, dBm)
 * @NL80211_SURVEY_INFO_IN_USE: channel is currently being used
 * @NL80211_SURVEY_INFO_CHANNEL_TIME: amount of time (in ms) that the radio
 *	spent on this channel
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY: amount of the time the primary
 *	channel was sensed busy (either due to activity or energy detect)
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY: amount of time the extension
 *	channel was sensed busy
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_RX: amount of time the radio spent
 *	receiving data
 * @NL80211_SURVEY_INFO_CHANNEL_TIME_TX: amount of time the radio spent
 *	transmitting data
 * @NL80211_SURVEY_INFO_MAX: highest survey info attribute number
 *	currently defined
 * @__NL80211_SURVEY_INFO_AFTER_LAST: internal use
 */
enum nl80211_survey_info {
	__NL80211_SURVEY_INFO_INVALID,
	NL80211_SURVEY_INFO_FREQUENCY,
	NL80211_SURVEY_INFO_NOISE,
	NL80211_SURVEY_INFO_IN_USE,
	NL80211_SURVEY_INFO_CHANNEL_TIME,
	NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY,
	NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY,
	NL80211_SURVEY_INFO_CHANNEL_TIME_RX,
	NL80211_SURVEY_INFO_CHANNEL_TIME_TX,

	/* keep last */
	__NL80211_SURVEY_INFO_AFTER_LAST,
	NL80211_SURVEY_INFO_MAX = __NL80211_SURVEY_INFO_AFTER_LAST - 1
};

/**
 * enum nl80211_mntr_flags - monitor configuration flags
 *
 * Monitor configuration flags.
 *
 * @__NL80211_MNTR_FLAG_INVALID: reserved
 *
 * @NL80211_MNTR_FLAG_FCSFAIL: pass frames with bad FCS
 * @NL80211_MNTR_FLAG_PLCPFAIL: pass frames with bad PLCP
 * @NL80211_MNTR_FLAG_CONTROL: pass control frames
 * @NL80211_MNTR_FLAG_OTHER_BSS: disable BSSID filtering
 * @NL80211_MNTR_FLAG_COOK_FRAMES: report frames after processing.
 *	overrides all other flags.
 *
 * @__NL80211_MNTR_FLAG_AFTER_LAST: internal use
 * @NL80211_MNTR_FLAG_MAX: highest possible monitor flag
 */
enum nl80211_mntr_flags {
	__NL80211_MNTR_FLAG_INVALID,
	NL80211_MNTR_FLAG_FCSFAIL,
	NL80211_MNTR_FLAG_PLCPFAIL,
	NL80211_MNTR_FLAG_CONTROL,
	NL80211_MNTR_FLAG_OTHER_BSS,
	NL80211_MNTR_FLAG_COOK_FRAMES,

	/* keep last */
	__NL80211_MNTR_FLAG_AFTER_LAST,
	NL80211_MNTR_FLAG_MAX = __NL80211_MNTR_FLAG_AFTER_LAST - 1
};

/**
 * enum nl80211_mesh_power_mode - mesh power save modes
 *
 * @NL80211_MESH_POWER_UNKNOWN: The mesh power mode of the mesh STA is
 *	not known or has not been set yet.
 * @NL80211_MESH_POWER_ACTIVE: Active mesh power mode. The mesh STA is
 *	in Awake state all the time.
 * @NL80211_MESH_POWER_LIGHT_SLEEP: Light sleep mode. The mesh STA will
 *	alternate between Active and Doze states, but will wake up for
 *	neighbor's beacons.
 * @NL80211_MESH_POWER_DEEP_SLEEP: Deep sleep mode. The mesh STA will
 *	alternate between Active and Doze states, but may not wake up
 *	for neighbor's beacons.
 *
 * @__NL80211_MESH_POWER_AFTER_LAST - internal use
 * @NL80211_MESH_POWER_MAX - highest possible power save level
 */

enum nl80211_mesh_power_mode {
	NL80211_MESH_POWER_UNKNOWN,
	NL80211_MESH_POWER_ACTIVE,
	NL80211_MESH_POWER_LIGHT_SLEEP,
	NL80211_MESH_POWER_DEEP_SLEEP,

	__NL80211_MESH_POWER_AFTER_LAST,
	NL80211_MESH_POWER_MAX = __NL80211_MESH_POWER_AFTER_LAST - 1
};

/**
 * enum nl80211_meshconf_params - mesh configuration parameters
 *
 * Mesh configuration parameters. These can be changed while the mesh is
 * active.
 *
 * @__NL80211_MESHCONF_INVALID: internal use
 *
 * @NL80211_MESHCONF_RETRY_TIMEOUT: specifies the initial retry timeout in
 *	millisecond units, used by the Peer Link Open message
 *
 * @NL80211_MESHCONF_CONFIRM_TIMEOUT: specifies the initial confirm timeout, in
 *	millisecond units, used by the peer link management to close a peer link
 *
 * @NL80211_MESHCONF_HOLDING_TIMEOUT: specifies the holding timeout, in
 *	millisecond units
 *
 * @NL80211_MESHCONF_MAX_PEER_LINKS: maximum number of peer links allowed
 *	on this mesh interface
 *
 * @NL80211_MESHCONF_MAX_RETRIES: specifies the maximum number of peer link
 *	open retries that can be sent to establish a new peer link instance in a
 *	mesh
 *
 * @NL80211_MESHCONF_TTL: specifies the value of TTL field set at a source mesh
 *	point.
 *
 * @NL80211_MESHCONF_AUTO_OPEN_PLINKS: whether we should automatically
 *	open peer links when we detect compatible mesh peers.
 *
 * @NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES: the number of action frames
 *	containing a PREQ that an MP can send to a particular destination (path
 *	target)
 *
 * @NL80211_MESHCONF_PATH_REFRESH_TIME: how frequently to refresh mesh paths
 *	(in milliseconds)
 *
 * @NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT: minimum length of time to wait
 *	until giving up on a path discovery (in milliseconds)
 *
 * @NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT: The time (in TUs) for which mesh
 *	points receiving a PREQ shall consider the forwarding information from
 *	the root to be valid. (TU = time unit)
 *
 * @NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL: The minimum interval of time (in
 *	TUs) during which an MP can send only one action frame containing a PREQ
 *	reference element
 *
 * @NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME: The interval of time (in TUs)
 *	that it takes for an HWMP information element to propagate across the
 *	mesh
 *
 * @NL80211_MESHCONF_HWMP_ROOTMODE: whether root mode is enabled or not
 *
 * @NL80211_MESHCONF_ELEMENT_TTL: specifies the value of TTL field set at a
 *	source mesh point for path selection elements.
 *
 * @NL80211_MESHCONF_HWMP_RANN_INTERVAL:  The interval of time (in TUs) between
 *	root announcements are transmitted.
 *
 * @NL80211_MESHCONF_GATE_ANNOUNCEMENTS: Advertise that this mesh station has
 *	access to a broader network beyond the MBSS.  This is done via Root
 *	Announcement frames.
 *
 * @NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL: The minimum interval of time (in
 *	TUs) during which a mesh STA can send only one Action frame containing a
 *	PERR element.
 *
 * @NL80211_MESHCONF_FORWARDING: set Mesh STA as forwarding or non-forwarding
 *	or forwarding entity (default is TRUE - forwarding entity)
 *
 * @NL80211_MESHCONF_RSSI_THRESHOLD: RSSI threshold in dBm. This specifies the
 *	threshold for average signal strength of candidate station to establish
 *	a peer link.
 *
 * @NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR: maximum number of neighbors
 *	to synchronize to for 11s default synchronization method
 *	(see 11C.12.2.2)
 *
 * @NL80211_MESHCONF_HT_OPMODE: set mesh HT protection mode.
 *
 * @NL80211_MESHCONF_ATTR_MAX: highest possible mesh configuration attribute
 *
 * @NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT: The time (in TUs) for
 *	which mesh STAs receiving a proactive PREQ shall consider the forwarding
 *	information to the root mesh STA to be valid.
 *
 * @NL80211_MESHCONF_HWMP_ROOT_INTERVAL: The interval of time (in TUs) between
 *	proactive PREQs are transmitted.
 *
 * @NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL: The minimum interval of time
 *	(in TUs) during which a mesh STA can send only one Action frame
 *	containing a PREQ element for root path confirmation.
 *
 * @NL80211_MESHCONF_POWER_MODE: Default mesh power mode for new peer links.
 *	type &enum nl80211_mesh_power_mode (u32)
 *
 * @NL80211_MESHCONF_AWAKE_WINDOW: awake window duration (in TUs)
 *
 * @__NL80211_MESHCONF_ATTR_AFTER_LAST: internal use
 */
enum nl80211_meshconf_params {
	__NL80211_MESHCONF_INVALID,
	NL80211_MESHCONF_RETRY_TIMEOUT,
	NL80211_MESHCONF_CONFIRM_TIMEOUT,
	NL80211_MESHCONF_HOLDING_TIMEOUT,
	NL80211_MESHCONF_MAX_PEER_LINKS,
	NL80211_MESHCONF_MAX_RETRIES,
	NL80211_MESHCONF_TTL,
	NL80211_MESHCONF_AUTO_OPEN_PLINKS,
	NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES,
	NL80211_MESHCONF_PATH_REFRESH_TIME,
	NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT,
	NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT,
	NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL,
	NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
	NL80211_MESHCONF_HWMP_ROOTMODE,
	NL80211_MESHCONF_ELEMENT_TTL,
	NL80211_MESHCONF_HWMP_RANN_INTERVAL,
	NL80211_MESHCONF_GATE_ANNOUNCEMENTS,
	NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL,
	NL80211_MESHCONF_FORWARDING,
	NL80211_MESHCONF_RSSI_THRESHOLD,
	NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR,
	NL80211_MESHCONF_HT_OPMODE,
	NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT,
	NL80211_MESHCONF_HWMP_ROOT_INTERVAL,
	NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL,
	NL80211_MESHCONF_POWER_MODE,
	NL80211_MESHCONF_AWAKE_WINDOW,

	/* keep last */
	__NL80211_MESHCONF_ATTR_AFTER_LAST,
	NL80211_MESHCONF_ATTR_MAX = __NL80211_MESHCONF_ATTR_AFTER_LAST - 1
};

/**
 * enum nl80211_mesh_setup_params - mesh setup parameters
 *
 * Mesh setup parameters.  These are used to start/join a mesh and cannot be
 * changed while the mesh is active.
 *
 * @__NL80211_MESH_SETUP_INVALID: Internal use
 *
 * @NL80211_MESH_SETUP_ENABLE_VENDOR_PATH_SEL: Enable this option to use a
 *	vendor specific path selection algorithm or disable it to use the
 *	default HWMP.
 *
 * @NL80211_MESH_SETUP_ENABLE_VENDOR_METRIC: Enable this option to use a
 *	vendor specific path metric or disable it to use the default Airtime
 *	metric.
 *
 * @NL80211_MESH_SETUP_IE: Information elements for this mesh, for instance, a
 *	robust security network ie, or a vendor specific information element
 *	that vendors will use to identify the path selection methods and
 *	metrics in use.
 *
 * @NL80211_MESH_SETUP_USERSPACE_AUTH: Enable this option if an authentication
 *	daemon will be authenticating mesh candidates.
 *
 * @NL80211_MESH_SETUP_USERSPACE_AMPE: Enable this option if an authentication
 *	daemon will be securing peer link frames.  AMPE is a secured version of
 *	Mesh Peering Management (MPM) and is implemented with the assistance of
 *	a userspace daemon.  When this flag is set, the kernel will send peer
 *	management frames to a userspace daemon that will implement AMPE
 *	functionality (security capabilities selection, key confirmation, and
 *	key management).  When the flag is unset (default), the kernel can
 *	autonomously complete (unsecured) mesh peering without the need of a
 *	userspace daemon.
 *
 * @NL80211_MESH_SETUP_ENABLE_VENDOR_SYNC: Enable this option to use a
 *	vendor specific synchronization method or disable it to use the default
 *	neighbor offset synchronization
 *
 * @NL80211_MESH_SETUP_ATTR_MAX: highest possible mesh setup attribute number
 *
 * @__NL80211_MESH_SETUP_ATTR_AFTER_LAST: Internal use
 */
enum nl80211_mesh_setup_params {
	__NL80211_MESH_SETUP_INVALID,
	NL80211_MESH_SETUP_ENABLE_VENDOR_PATH_SEL,
	NL80211_MESH_SETUP_ENABLE_VENDOR_METRIC,
	NL80211_MESH_SETUP_IE,
	NL80211_MESH_SETUP_USERSPACE_AUTH,
	NL80211_MESH_SETUP_USERSPACE_AMPE,
	NL80211_MESH_SETUP_ENABLE_VENDOR_SYNC,

	/* keep last */
	__NL80211_MESH_SETUP_ATTR_AFTER_LAST,
	NL80211_MESH_SETUP_ATTR_MAX = __NL80211_MESH_SETUP_ATTR_AFTER_LAST - 1
};

/**
 * enum nl80211_txq_attr - TX queue parameter attributes
 * @__NL80211_TXQ_ATTR_INVALID: Attribute number 0 is reserved
 * @NL80211_TXQ_ATTR_AC: AC identifier (NL80211_AC_*)
 * @NL80211_TXQ_ATTR_TXOP: Maximum burst time in units of 32 usecs, 0 meaning
 *	disabled
 * @NL80211_TXQ_ATTR_CWMIN: Minimum contention window [a value of the form
 *	2^n-1 in the range 1..32767]
 * @NL80211_TXQ_ATTR_CWMAX: Maximum contention window [a value of the form
 *	2^n-1 in the range 1..32767]
 * @NL80211_TXQ_ATTR_AIFS: Arbitration interframe space [0..255]
 * @__NL80211_TXQ_ATTR_AFTER_LAST: Internal
 * @NL80211_TXQ_ATTR_MAX: Maximum TXQ attribute number
 */
enum nl80211_txq_attr {
	__NL80211_TXQ_ATTR_INVALID,
	NL80211_TXQ_ATTR_AC,
	NL80211_TXQ_ATTR_TXOP,
	NL80211_TXQ_ATTR_CWMIN,
	NL80211_TXQ_ATTR_CWMAX,
	NL80211_TXQ_ATTR_AIFS,

	/* keep last */
	__NL80211_TXQ_ATTR_AFTER_LAST,
	NL80211_TXQ_ATTR_MAX = __NL80211_TXQ_ATTR_AFTER_LAST - 1
};

enum nl80211_ac {
	NL80211_AC_VO,
	NL80211_AC_VI,
	NL80211_AC_BE,
	NL80211_AC_BK,
	NL80211_NUM_ACS
};

/* backward compat */
#define NL80211_TXQ_ATTR_QUEUE	NL80211_TXQ_ATTR_AC
#define NL80211_TXQ_Q_VO	NL80211_AC_VO
#define NL80211_TXQ_Q_VI	NL80211_AC_VI
#define NL80211_TXQ_Q_BE	NL80211_AC_BE
#define NL80211_TXQ_Q_BK	NL80211_AC_BK

/**
 * enum nl80211_channel_type - channel type
 * @NL80211_CHAN_NO_HT: 20 MHz, non-HT channel
 * @NL80211_CHAN_HT20: 20 MHz HT channel
 * @NL80211_CHAN_HT40MINUS: HT40 channel, secondary channel
 *	below the control channel
 * @NL80211_CHAN_HT40PLUS: HT40 channel, secondary channel
 *	above the control channel
 */
enum nl80211_channel_type {
	NL80211_CHAN_NO_HT,
	NL80211_CHAN_HT20,
	NL80211_CHAN_HT40MINUS,
	NL80211_CHAN_HT40PLUS
};

/**
 * enum nl80211_chan_width - channel width definitions
 *
 * These values are used with the %NL80211_ATTR_CHANNEL_WIDTH
 * attribute.
 *
 * @NL80211_CHAN_WIDTH_20_NOHT: 20 MHz, non-HT channel
 * @NL80211_CHAN_WIDTH_20: 20 MHz HT channel
 * @NL80211_CHAN_WIDTH_40: 40 MHz channel, the %NL80211_ATTR_CENTER_FREQ1
 *	attribute must be provided as well
 * @NL80211_CHAN_WIDTH_80: 80 MHz channel, the %NL80211_ATTR_CENTER_FREQ1
 *	attribute must be provided as well
 * @NL80211_CHAN_WIDTH_80P80: 80+80 MHz channel, the %NL80211_ATTR_CENTER_FREQ1
 *	and %NL80211_ATTR_CENTER_FREQ2 attributes must be provided as well
 * @NL80211_CHAN_WIDTH_160: 160 MHz channel, the %NL80211_ATTR_CENTER_FREQ1
 *	attribute must be provided as well
 */
enum nl80211_chan_width {
	NL80211_CHAN_WIDTH_20_NOHT,
	NL80211_CHAN_WIDTH_20,
	NL80211_CHAN_WIDTH_40,
	NL80211_CHAN_WIDTH_80,
	NL80211_CHAN_WIDTH_80P80,
	NL80211_CHAN_WIDTH_160,
};

/**
 * enum nl80211_bss - netlink attributes for a BSS
 *
 * @__NL80211_BSS_INVALID: invalid
 * @NL80211_BSS_BSSID: BSSID of the BSS (6 octets)
 * @NL80211_BSS_FREQUENCY: frequency in MHz (u32)
 * @NL80211_BSS_TSF: TSF of the received probe response/beacon (u64)
 * @NL80211_BSS_BEACON_INTERVAL: beacon interval of the (I)BSS (u16)
 * @NL80211_BSS_CAPABILITY: capability field (CPU order, u16)
 * @NL80211_BSS_INFORMATION_ELEMENTS: binary attribute containing the
 *	raw information elements from the probe response/beacon (bin);
 *	if the %NL80211_BSS_BEACON_IES attribute is present, the IEs here are
 *	from a Probe Response frame; otherwise they are from a Beacon frame.
 *	However, if the driver does not indicate the source of the IEs, these
 *	IEs may be from either frame subtype.
 * @NL80211_BSS_SIGNAL_MBM: signal strength of probe response/beacon
 *	in mBm (100 * dBm) (s32)
 * @NL80211_BSS_SIGNAL_UNSPEC: signal strength of the probe response/beacon
 *	in unspecified units, scaled to 0..100 (u8)
 * @NL80211_BSS_STATUS: status, if this BSS is "used"
 * @NL80211_BSS_SEEN_MS_AGO: age of this BSS entry in ms
 * @NL80211_BSS_BEACON_IES: binary attribute containing the raw information
 *	elements from a Beacon frame (bin); not present if no Beacon frame has
 *	yet been received
 * @__NL80211_BSS_AFTER_LAST: internal
 * @NL80211_BSS_MAX: highest BSS attribute
 */
enum nl80211_bss {
	__NL80211_BSS_INVALID,
	NL80211_BSS_BSSID,
	NL80211_BSS_FREQUENCY,
	NL80211_BSS_TSF,
	NL80211_BSS_BEACON_INTERVAL,
	NL80211_BSS_CAPABILITY,
	NL80211_BSS_INFORMATION_ELEMENTS,
	NL80211_BSS_SIGNAL_MBM,
	NL80211_BSS_SIGNAL_UNSPEC,
	NL80211_BSS_STATUS,
	NL80211_BSS_SEEN_MS_AGO,
	NL80211_BSS_BEACON_IES,

	/* keep last */
	__NL80211_BSS_AFTER_LAST,
	NL80211_BSS_MAX = __NL80211_BSS_AFTER_LAST - 1
};

/**
 * enum nl80211_bss_status - BSS "status"
 * @NL80211_BSS_STATUS_AUTHENTICATED: Authenticated with this BSS.
 * @NL80211_BSS_STATUS_ASSOCIATED: Associated with this BSS.
 * @NL80211_BSS_STATUS_IBSS_JOINED: Joined to this IBSS.
 *
 * The BSS status is a BSS attribute in scan dumps, which
 * indicates the status the interface has wrt. this BSS.
 */
enum nl80211_bss_status {
	NL80211_BSS_STATUS_AUTHENTICATED,
	NL80211_BSS_STATUS_ASSOCIATED,
	NL80211_BSS_STATUS_IBSS_JOINED,
};

/**
 * enum nl80211_auth_type - AuthenticationType
 *
 * @NL80211_AUTHTYPE_OPEN_SYSTEM: Open System authentication
 * @NL80211_AUTHTYPE_SHARED_KEY: Shared Key authentication (WEP only)
 * @NL80211_AUTHTYPE_FT: Fast BSS Transition (IEEE 802.11r)
 * @NL80211_AUTHTYPE_NETWORK_EAP: Network EAP (some Cisco APs and mainly LEAP)
 * @NL80211_AUTHTYPE_SAE: Simultaneous authentication of equals
 * @__NL80211_AUTHTYPE_NUM: internal
 * @NL80211_AUTHTYPE_MAX: maximum valid auth algorithm
 * @NL80211_AUTHTYPE_AUTOMATIC: determine automatically (if necessary by
 *	trying multiple times); this is invalid in netlink -- leave out
 *	the attribute for this on CONNECT commands.
 */
enum nl80211_auth_type {
	NL80211_AUTHTYPE_OPEN_SYSTEM,
	NL80211_AUTHTYPE_SHARED_KEY,
	NL80211_AUTHTYPE_FT,
	NL80211_AUTHTYPE_NETWORK_EAP,
	NL80211_AUTHTYPE_SAE,

	/* keep last */
	__NL80211_AUTHTYPE_NUM,
	NL80211_AUTHTYPE_MAX = __NL80211_AUTHTYPE_NUM - 1,
	NL80211_AUTHTYPE_AUTOMATIC
};

/**
 * enum nl80211_key_type - Key Type
 * @NL80211_KEYTYPE_GROUP: Group (broadcast/multicast) key
 * @NL80211_KEYTYPE_PAIRWISE: Pairwise (unicast/individual) key
 * @NL80211_KEYTYPE_PEERKEY: PeerKey (DLS)
 * @NUM_NL80211_KEYTYPES: number of defined key types
 */
enum nl80211_key_type {
	NL80211_KEYTYPE_GROUP,
	NL80211_KEYTYPE_PAIRWISE,
	NL80211_KEYTYPE_PEERKEY,

	NUM_NL80211_KEYTYPES
};

/**
 * enum nl80211_mfp - Management frame protection state
 * @NL80211_MFP_NO: Management frame protection not used
 * @NL80211_MFP_REQUIRED: Management frame protection required
 */
enum nl80211_mfp {
	NL80211_MFP_NO,
	NL80211_MFP_REQUIRED,
};

enum nl80211_wpa_versions {
	NL80211_WPA_VERSION_1 = 1 << 0,
	NL80211_WPA_VERSION_2 = 1 << 1,
};

/**
 * enum nl80211_key_default_types - key default types
 * @__NL80211_KEY_DEFAULT_TYPE_INVALID: invalid
 * @NL80211_KEY_DEFAULT_TYPE_UNICAST: key should be used as default
 *	unicast key
 * @NL80211_KEY_DEFAULT_TYPE_MULTICAST: key should be used as default
 *	multicast key
 * @NUM_NL80211_KEY_DEFAULT_TYPES: number of default types
 */
enum nl80211_key_default_types {
	__NL80211_KEY_DEFAULT_TYPE_INVALID,
	NL80211_KEY_DEFAULT_TYPE_UNICAST,
	NL80211_KEY_DEFAULT_TYPE_MULTICAST,

	NUM_NL80211_KEY_DEFAULT_TYPES
};

/**
 * enum nl80211_key_attributes - key attributes
 * @__NL80211_KEY_INVALID: invalid
 * @NL80211_KEY_DATA: (temporal) key data; for TKIP this consists of
 *	16 bytes encryption key followed by 8 bytes each for TX and RX MIC
 *	keys
 * @NL80211_KEY_IDX: key ID (u8, 0-3)
 * @NL80211_KEY_CIPHER: key cipher suite (u32, as defined by IEEE 802.11
 *	section 7.3.2.25.1, e.g. 0x000FAC04)
 * @NL80211_KEY_SEQ: transmit key sequence number (IV/PN) for TKIP and
 *	CCMP keys, each six bytes in little endian
 * @NL80211_KEY_DEFAULT: flag indicating default key
 * @NL80211_KEY_DEFAULT_MGMT: flag indicating default management key
 * @NL80211_KEY_TYPE: the key type from enum nl80211_key_type, if not
 *	specified the default depends on whether a MAC address was
 *	given with the command using the key or not (u32)
 * @NL80211_KEY_DEFAULT_TYPES: A nested attribute containing flags
 *	attributes, specifying what a key should be set as default as.
 *	See &enum nl80211_key_default_types.
 * @__NL80211_KEY_AFTER_LAST: internal
 * @NL80211_KEY_MAX: highest key attribute
 */
enum nl80211_key_attributes {
	__NL80211_KEY_INVALID,
	NL80211_KEY_DATA,
	NL80211_KEY_IDX,
	NL80211_KEY_CIPHER,
	NL80211_KEY_SEQ,
	NL80211_KEY_DEFAULT,
	NL80211_KEY_DEFAULT_MGMT,
	NL80211_KEY_TYPE,
	NL80211_KEY_DEFAULT_TYPES,

	/* keep last */
	__NL80211_KEY_AFTER_LAST,
	NL80211_KEY_MAX = __NL80211_KEY_AFTER_LAST - 1
};

/**
 * enum nl80211_tx_rate_attributes - TX rate set attributes
 * @__NL80211_TXRATE_INVALID: invalid
 * @NL80211_TXRATE_LEGACY: Legacy (non-MCS) rates allowed for TX rate selection
 *	in an array of rates as defined in IEEE 802.11 7.3.2.2 (u8 values with
 *	1 = 500 kbps) but without the IE length restriction (at most
 *	%NL80211_MAX_SUPP_RATES in a single array).
 * @NL80211_TXRATE_MCS: HT (MCS) rates allowed for TX rate selection
 *	in an array of MCS numbers.
 * @__NL80211_TXRATE_AFTER_LAST: internal
 * @NL80211_TXRATE_MAX: highest TX rate attribute
 */
enum nl80211_tx_rate_attributes {
	__NL80211_TXRATE_INVALID,
	NL80211_TXRATE_LEGACY,
	NL80211_TXRATE_MCS,

	/* keep last */
	__NL80211_TXRATE_AFTER_LAST,
	NL80211_TXRATE_MAX = __NL80211_TXRATE_AFTER_LAST - 1
};

/**
 * enum nl80211_band - Frequency band
 * @NL80211_BAND_2GHZ: 2.4 GHz ISM band
 * @NL80211_BAND_5GHZ: around 5 GHz band (4.9 - 5.7 GHz)
 * @NL80211_BAND_60GHZ: around 60 GHz band (58.32 - 64.80 GHz)
 */
enum nl80211_band {
	NL80211_BAND_2GHZ,
	NL80211_BAND_5GHZ,
	NL80211_BAND_60GHZ,
};

/**
 * enum nl80211_ps_state - powersave state
 * @NL80211_PS_DISABLED: powersave is disabled
 * @NL80211_PS_ENABLED: powersave is enabled
 */
enum nl80211_ps_state {
	NL80211_PS_DISABLED,
	NL80211_PS_ENABLED,
};

/**
 * enum nl80211_attr_cqm - connection quality monitor attributes
 * @__NL80211_ATTR_CQM_INVALID: invalid
 * @NL80211_ATTR_CQM_RSSI_THOLD: RSSI threshold in dBm. This value specifies
 *	the threshold for the RSSI level at which an event will be sent. Zero
 *	to disable.
 * @NL80211_ATTR_CQM_RSSI_HYST: RSSI hysteresis in dBm. This value specifies
 *	the minimum amount the RSSI level must change after an event before a
 *	new event may be issued (to reduce effects of RSSI oscillation).
 * @NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT: RSSI threshold event
 * @NL80211_ATTR_CQM_PKT_LOSS_EVENT: a u32 value indicating that this many
 *	consecutive packets were not acknowledged by the peer
 * @NL80211_ATTR_CQM_TXE_RATE: TX error rate in %. Minimum % of TX failures
 *	during the given %NL80211_ATTR_CQM_TXE_INTVL before an
 *	%NL80211_CMD_NOTIFY_CQM with reported %NL80211_ATTR_CQM_TXE_RATE and
 *	%NL80211_ATTR_CQM_TXE_PKTS is generated.
 * @NL80211_ATTR_CQM_TXE_PKTS: number of attempted packets in a given
 *	%NL80211_ATTR_CQM_TXE_INTVL before %NL80211_ATTR_CQM_TXE_RATE is
 *	checked.
 * @NL80211_ATTR_CQM_TXE_INTVL: interval in seconds. Specifies the periodic
 *	interval in which %NL80211_ATTR_CQM_TXE_PKTS and
 *	%NL80211_ATTR_CQM_TXE_RATE must be satisfied before generating an
 *	%NL80211_CMD_NOTIFY_CQM. Set to 0 to turn off TX error reporting.
 * @__NL80211_ATTR_CQM_AFTER_LAST: internal
 * @NL80211_ATTR_CQM_MAX: highest key attribute
 */
enum nl80211_attr_cqm {
	__NL80211_ATTR_CQM_INVALID,
	NL80211_ATTR_CQM_RSSI_THOLD,
	NL80211_ATTR_CQM_RSSI_HYST,
	NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT,
	NL80211_ATTR_CQM_PKT_LOSS_EVENT,
	NL80211_ATTR_CQM_TXE_RATE,
	NL80211_ATTR_CQM_TXE_PKTS,
	NL80211_ATTR_CQM_TXE_INTVL,

	/* keep last */
	__NL80211_ATTR_CQM_AFTER_LAST,
	NL80211_ATTR_CQM_MAX = __NL80211_ATTR_CQM_AFTER_LAST - 1
};

/**
 * enum nl80211_cqm_rssi_threshold_event - RSSI threshold event
 * @NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW: The RSSI level is lower than the
 *      configured threshold
 * @NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH: The RSSI is higher than the
 *      configured threshold
 * @NL80211_CQM_RSSI_BEACON_LOSS_EVENT: The device experienced beacon loss.
 *	(Note that deauth/disassoc will still follow if the AP is not
 *	available. This event might get used as roaming event, etc.)
 */
enum nl80211_cqm_rssi_threshold_event {
	NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
	NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
	NL80211_CQM_RSSI_BEACON_LOSS_EVENT,
};


/**
 * enum nl80211_tx_power_setting - TX power adjustment
 * @NL80211_TX_POWER_AUTOMATIC: automatically determine transmit power
 * @NL80211_TX_POWER_LIMITED: limit TX power by the mBm parameter
 * @NL80211_TX_POWER_FIXED: fix TX power to the mBm parameter
 */
enum nl80211_tx_power_setting {
	NL80211_TX_POWER_AUTOMATIC,
	NL80211_TX_POWER_LIMITED,
	NL80211_TX_POWER_FIXED,
};

/**
 * enum nl80211_wowlan_packet_pattern_attr - WoWLAN packet pattern attribute
 * @__NL80211_WOWLAN_PKTPAT_INVALID: invalid number for nested attribute
 * @NL80211_WOWLAN_PKTPAT_PATTERN: the pattern, values where the mask has
 *	a zero bit are ignored
 * @NL80211_WOWLAN_PKTPAT_MASK: pattern mask, must be long enough to have
 *	a bit for each byte in the pattern. The lowest-order bit corresponds
 *	to the first byte of the pattern, but the bytes of the pattern are
 *	in a little-endian-like format, i.e. the 9th byte of the pattern
 *	corresponds to the lowest-order bit in the second byte of the mask.
 *	For example: The match 00:xx:00:00:xx:00:00:00:00:xx:xx:xx (where
 *	xx indicates "don't care") would be represented by a pattern of
 *	twelve zero bytes, and a mask of "0xed,0x01".
 *	Note that the pattern matching is done as though frames were not
 *	802.11 frames but 802.3 frames, i.e. the frame is fully unpacked
 *	first (including SNAP header unpacking) and then matched.
 * @NL80211_WOWLAN_PKTPAT_OFFSET: packet offset, pattern is matched after
 *	these fixed number of bytes of received packet
 * @NUM_NL80211_WOWLAN_PKTPAT: number of attributes
 * @MAX_NL80211_WOWLAN_PKTPAT: max attribute number
 */
enum nl80211_wowlan_packet_pattern_attr {
	__NL80211_WOWLAN_PKTPAT_INVALID,
	NL80211_WOWLAN_PKTPAT_MASK,
	NL80211_WOWLAN_PKTPAT_PATTERN,
	NL80211_WOWLAN_PKTPAT_OFFSET,

	NUM_NL80211_WOWLAN_PKTPAT,
	MAX_NL80211_WOWLAN_PKTPAT = NUM_NL80211_WOWLAN_PKTPAT - 1,
};

/**
 * struct nl80211_wowlan_pattern_support - pattern support information
 * @max_patterns: maximum number of patterns supported
 * @min_pattern_len: minimum length of each pattern
 * @max_pattern_len: maximum length of each pattern
 * @max_pkt_offset: maximum Rx packet offset
 *
 * This struct is carried in %NL80211_WOWLAN_TRIG_PKT_PATTERN when
 * that is part of %NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED in the
 * capability information given by the kernel to userspace.
 */
struct nl80211_wowlan_pattern_support {
	__u32 max_patterns;
	__u32 min_pattern_len;
	__u32 max_pattern_len;
	__u32 max_pkt_offset;
} __attribute__((packed));

/**
 * enum nl80211_wowlan_triggers - WoWLAN trigger definitions
 * @__NL80211_WOWLAN_TRIG_INVALID: invalid number for nested attributes
 * @NL80211_WOWLAN_TRIG_ANY: wake up on any activity, do not really put
 *	the chip into a special state -- works best with chips that have
 *	support for low-power operation already (flag)
 * @NL80211_WOWLAN_TRIG_DISCONNECT: wake up on disconnect, the way disconnect
 *	is detected is implementation-specific (flag)
 * @NL80211_WOWLAN_TRIG_MAGIC_PKT: wake up on magic packet (6x 0xff, followed
 *	by 16 repetitions of MAC addr, anywhere in payload) (flag)
 * @NL80211_WOWLAN_TRIG_PKT_PATTERN: wake up on the specified packet patterns
 *	which are passed in an array of nested attributes, each nested attribute
 *	defining a with attributes from &struct nl80211_wowlan_trig_pkt_pattern.
 *	Each pattern defines a wakeup packet. Packet offset is associated with
 *	each pattern which is used while matching the pattern. The matching is
 *	done on the MSDU, i.e. as though the packet was an 802.3 packet, so the
 *	pattern matching is done after the packet is converted to the MSDU.
 *
 *	In %NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED, it is a binary attribute
 *	carrying a &struct nl80211_wowlan_pattern_support.
 *
 *	When reporting wakeup. it is a u32 attribute containing the 0-based
 *	index of the pattern that caused the wakeup, in the patterns passed
 *	to the kernel when configuring.
 * @NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED: Not a real trigger, and cannot be
 *	used when setting, used only to indicate that GTK rekeying is supported
 *	by the device (flag)
 * @NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE: wake up on GTK rekey failure (if
 *	done by the device) (flag)
 * @NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST: wake up on EAP Identity Request
 *	packet (flag)
 * @NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE: wake up on 4-way handshake (flag)
 * @NL80211_WOWLAN_TRIG_RFKILL_RELEASE: wake up when rfkill is released
 *	(on devices that have rfkill in the device) (flag)
 * @NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211: For wakeup reporting only, contains
 *	the 802.11 packet that caused the wakeup, e.g. a deauth frame. The frame
 *	may be truncated, the @NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211_LEN
 *	attribute contains the original length.
 * @NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211_LEN: Original length of the 802.11
 *	packet, may be bigger than the @NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211
 *	attribute if the packet was truncated somewhere.
 * @NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023: For wakeup reporting only, contains the
 *	802.11 packet that caused the wakeup, e.g. a magic packet. The frame may
 *	be truncated, the @NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023_LEN attribute
 *	contains the original length.
 * @NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023_LEN: Original length of the 802.3
 *	packet, may be bigger than the @NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023
 *	attribute if the packet was truncated somewhere.
 * @NL80211_WOWLAN_TRIG_TCP_CONNECTION: TCP connection wake, see DOC section
 *	"TCP connection wakeup" for more details. This is a nested attribute
 *	containing the exact information for establishing and keeping alive
 *	the TCP connection.
 * @NL80211_WOWLAN_TRIG_TCP_WAKEUP_MATCH: For wakeup reporting only, the
 *	wakeup packet was received on the TCP connection
 * @NL80211_WOWLAN_TRIG_WAKEUP_TCP_CONNLOST: For wakeup reporting only, the
 *	TCP connection was lost or failed to be established
 * @NL80211_WOWLAN_TRIG_WAKEUP_TCP_NOMORETOKENS: For wakeup reporting only,
 *	the TCP connection ran out of tokens to use for data to send to the
 *	service
 * @NUM_NL80211_WOWLAN_TRIG: number of wake on wireless triggers
 * @MAX_NL80211_WOWLAN_TRIG: highest wowlan trigger attribute number
 *
 * These nested attributes are used to configure the wakeup triggers and
 * to report the wakeup reason(s).
 */
enum nl80211_wowlan_triggers {
	__NL80211_WOWLAN_TRIG_INVALID,
	NL80211_WOWLAN_TRIG_ANY,
	NL80211_WOWLAN_TRIG_DISCONNECT,
	NL80211_WOWLAN_TRIG_MAGIC_PKT,
	NL80211_WOWLAN_TRIG_PKT_PATTERN,
	NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED,
	NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE,
	NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST,
	NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE,
	NL80211_WOWLAN_TRIG_RFKILL_RELEASE,
	NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211,
	NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211_LEN,
	NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023,
	NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023_LEN,
	NL80211_WOWLAN_TRIG_TCP_CONNECTION,
	NL80211_WOWLAN_TRIG_WAKEUP_TCP_MATCH,
	NL80211_WOWLAN_TRIG_WAKEUP_TCP_CONNLOST,
	NL80211_WOWLAN_TRIG_WAKEUP_TCP_NOMORETOKENS,

	/* keep last */
	NUM_NL80211_WOWLAN_TRIG,
	MAX_NL80211_WOWLAN_TRIG = NUM_NL80211_WOWLAN_TRIG - 1
};

/**
 * DOC: TCP connection wakeup
 *
 * Some devices can establish a TCP connection in order to be woken up by a
 * packet coming in from outside their network segment, or behind NAT. If
 * configured, the device will establish a TCP connection to the given
 * service, and periodically send data to that service. The first data
 * packet is usually transmitted after SYN/ACK, also ACKing the SYN/ACK.
 * The data packets can optionally include a (little endian) sequence
 * number (in the TCP payload!) that is generated by the device, and, also
 * optionally, a token from a list of tokens. This serves as a keep-alive
 * with the service, and for NATed connections, etc.
 *
 * During this keep-alive period, the server doesn't send any data to the
 * client. When receiving data, it is compared against the wakeup pattern
 * (and mask) and if it matches, the host is woken up. Similarly, if the
 * connection breaks or cannot be established to start with, the host is
 * also woken up.
 *
 * Developer's note: ARP offload is required for this, otherwise TCP
 * response packets might not go through correctly.
 */

/**
 * struct nl80211_wowlan_tcp_data_seq - WoWLAN TCP data sequence
 * @start: starting value
 * @offset: offset of sequence number in packet
 * @len: length of the sequence value to write, 1 through 4
 *
 * Note: don't confuse with the TCP sequence number(s), this is for the
 * keepalive packet payload. The actual value is written into the packet
 * in little endian.
 */
struct nl80211_wowlan_tcp_data_seq {
	__u32 start, offset, len;
};

/**
 * struct nl80211_wowlan_tcp_data_token - WoWLAN TCP data token config
 * @offset: offset of token in packet
 * @len: length of each token
 * @token_stream: stream of data to be used for the tokens, the length must
 *	be a multiple of @len for this to make sense
 */
struct nl80211_wowlan_tcp_data_token {
	__u32 offset, len;
	__u8 token_stream[];
};

/**
 * struct nl80211_wowlan_tcp_data_token_feature - data token features
 * @min_len: minimum token length
 * @max_len: maximum token length
 * @bufsize: total available token buffer size (max size of @token_stream)
 */
struct nl80211_wowlan_tcp_data_token_feature {
	__u32 min_len, max_len, bufsize;
};

/**
 * enum nl80211_wowlan_tcp_attrs - WoWLAN TCP connection parameters
 * @__NL80211_WOWLAN_TCP_INVALID: invalid number for nested attributes
 * @NL80211_WOWLAN_TCP_SRC_IPV4: source IPv4 address (in network byte order)
 * @NL80211_WOWLAN_TCP_DST_IPV4: destination IPv4 address
 *	(in network byte order)
 * @NL80211_WOWLAN_TCP_DST_MAC: destination MAC address, this is given because
 *	route lookup when configured might be invalid by the time we suspend,
 *	and doing a route lookup when suspending is no longer possible as it
 *	might require ARP querying.
 * @NL80211_WOWLAN_TCP_SRC_PORT: source port (u16); optional, if not given a
 *	socket and port will be allocated
 * @NL80211_WOWLAN_TCP_DST_PORT: destination port (u16)
 * @NL80211_WOWLAN_TCP_DATA_PAYLOAD: data packet payload, at least one byte.
 *	For feature advertising, a u32 attribute holding the maximum length
 *	of the data payload.
 * @NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ: data packet sequence configuration
 *	(if desired), a &struct nl80211_wowlan_tcp_data_seq. For feature
 *	advertising it is just a flag
 * @NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN: data packet token configuration,
 *	see &struct nl80211_wowlan_tcp_data_token and for advertising see
 *	&struct nl80211_wowlan_tcp_data_token_feature.
 * @NL80211_WOWLAN_TCP_DATA_INTERVAL: data interval in seconds, maximum
 *	interval in feature advertising (u32)
 * @NL80211_WOWLAN_TCP_WAKE_PAYLOAD: wake packet payload, for advertising a
 *	u32 attribute holding the maximum length
 * @NL80211_WOWLAN_TCP_WAKE_MASK: Wake packet payload mask, not used for
 *	feature advertising. The mask works like @NL80211_WOWLAN_PKTPAT_MASK
 *	but on the TCP payload only.
 * @NUM_NL80211_WOWLAN_TCP: number of TCP attributes
 * @MAX_NL80211_WOWLAN_TCP: highest attribute number
 */
enum nl80211_wowlan_tcp_attrs {
	__NL80211_WOWLAN_TCP_INVALID,
	NL80211_WOWLAN_TCP_SRC_IPV4,
	NL80211_WOWLAN_TCP_DST_IPV4,
	NL80211_WOWLAN_TCP_DST_MAC,
	NL80211_WOWLAN_TCP_SRC_PORT,
	NL80211_WOWLAN_TCP_DST_PORT,
	NL80211_WOWLAN_TCP_DATA_PAYLOAD,
	NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ,
	NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN,
	NL80211_WOWLAN_TCP_DATA_INTERVAL,
	NL80211_WOWLAN_TCP_WAKE_PAYLOAD,
	NL80211_WOWLAN_TCP_WAKE_MASK,

	/* keep last */
	NUM_NL80211_WOWLAN_TCP,
	MAX_NL80211_WOWLAN_TCP = NUM_NL80211_WOWLAN_TCP - 1
};

/**
 * enum nl80211_iface_limit_attrs - limit attributes
 * @NL80211_IFACE_LIMIT_UNSPEC: (reserved)
 * @NL80211_IFACE_LIMIT_MAX: maximum number of interfaces that
 *	can be chosen from this set of interface types (u32)
 * @NL80211_IFACE_LIMIT_TYPES: nested attribute containing a
 *	flag attribute for each interface type in this set
 * @NUM_NL80211_IFACE_LIMIT: number of attributes
 * @MAX_NL80211_IFACE_LIMIT: highest attribute number
 */
enum nl80211_iface_limit_attrs {
	NL80211_IFACE_LIMIT_UNSPEC,
	NL80211_IFACE_LIMIT_MAX,
	NL80211_IFACE_LIMIT_TYPES,

	/* keep last */
	NUM_NL80211_IFACE_LIMIT,
	MAX_NL80211_IFACE_LIMIT = NUM_NL80211_IFACE_LIMIT - 1
};

/**
 * enum nl80211_if_combination_attrs -- interface combination attributes
 *
 * @NL80211_IFACE_COMB_UNSPEC: (reserved)
 * @NL80211_IFACE_COMB_LIMITS: Nested attributes containing the limits
 *	for given interface types, see &enum nl80211_iface_limit_attrs.
 * @NL80211_IFACE_COMB_MAXNUM: u32 attribute giving the total number of
 *	interfaces that can be created in this group. This number doesn't
 *	apply to interfaces purely managed in software, which are listed
 *	in a separate attribute %NL80211_ATTR_INTERFACES_SOFTWARE.
 * @NL80211_IFACE_COMB_STA_AP_BI_MATCH: flag attribute specifying that
 *	beacon intervals within this group must be all the same even for
 *	infrastructure and AP/GO combinations, i.e. the GO(s) must adopt
 *	the infrastructure network's beacon interval.
 * @NL80211_IFACE_COMB_NUM_CHANNELS: u32 attribute specifying how many
 *	different channels may be used within this group.
 * @NL80211_IFACE_COMB_RADAR_DETECT_WIDTHS: u32 attribute containing the bitmap
 *	of supported channel widths for radar detection.
 * @NUM_NL80211_IFACE_COMB: number of attributes
 * @MAX_NL80211_IFACE_COMB: highest attribute number
 *
 * Examples:
 *	limits = [ #{STA} <= 1, #{AP} <= 1 ], matching BI, channels = 1, max = 2
 *	=> allows an AP and a STA that must match BIs
 *
 *	numbers = [ #{AP, P2P-GO} <= 8 ], channels = 1, max = 8
 *	=> allows 8 of AP/GO
 *
 *	numbers = [ #{STA} <= 2 ], channels = 2, max = 2
 *	=> allows two STAs on different channels
 *
 *	numbers = [ #{STA} <= 1, #{P2P-client,P2P-GO} <= 3 ], max = 4
 *	=> allows a STA plus three P2P interfaces
 *
 * The list of these four possiblities could completely be contained
 * within the %NL80211_ATTR_INTERFACE_COMBINATIONS attribute to indicate
 * that any of these groups must match.
 *
 * "Combinations" of just a single interface will not be listed here,
 * a single interface of any valid interface type is assumed to always
 * be possible by itself. This means that implicitly, for each valid
 * interface type, the following group always exists:
 *	numbers = [ #{<type>} <= 1 ], channels = 1, max = 1
 */
enum nl80211_if_combination_attrs {
	NL80211_IFACE_COMB_UNSPEC,
	NL80211_IFACE_COMB_LIMITS,
	NL80211_IFACE_COMB_MAXNUM,
	NL80211_IFACE_COMB_STA_AP_BI_MATCH,
	NL80211_IFACE_COMB_NUM_CHANNELS,
	NL80211_IFACE_COMB_RADAR_DETECT_WIDTHS,

	/* keep last */
	NUM_NL80211_IFACE_COMB,
	MAX_NL80211_IFACE_COMB = NUM_NL80211_IFACE_COMB - 1
};


/**
 * enum nl80211_plink_state - state of a mesh peer link finite state machine
 *
 * @NL80211_PLINK_LISTEN: initial state, considered the implicit
 *	state of non existant mesh peer links
 * @NL80211_PLINK_OPN_SNT: mesh plink open frame has been sent to
 *	this mesh peer
 * @NL80211_PLINK_OPN_RCVD: mesh plink open frame has been received
 *	from this mesh peer
 * @NL80211_PLINK_CNF_RCVD: mesh plink confirm frame has been
 *	received from this mesh peer
 * @NL80211_PLINK_ESTAB: mesh peer link is established
 * @NL80211_PLINK_HOLDING: mesh peer link is being closed or cancelled
 * @NL80211_PLINK_BLOCKED: all frames transmitted from this mesh
 *	plink are discarded
 * @NUM_NL80211_PLINK_STATES: number of peer link states
 * @MAX_NL80211_PLINK_STATES: highest numerical value of plink states
 */
enum nl80211_plink_state {
	NL80211_PLINK_LISTEN,
	NL80211_PLINK_OPN_SNT,
	NL80211_PLINK_OPN_RCVD,
	NL80211_PLINK_CNF_RCVD,
	NL80211_PLINK_ESTAB,
	NL80211_PLINK_HOLDING,
	NL80211_PLINK_BLOCKED,

	/* keep last */
	NUM_NL80211_PLINK_STATES,
	MAX_NL80211_PLINK_STATES = NUM_NL80211_PLINK_STATES - 1
};

/**
 * enum nl80211_plink_action - actions to perform in mesh peers
 *
 * @NL80211_PLINK_ACTION_NO_ACTION: perform no action
 * @NL80211_PLINK_ACTION_OPEN: start mesh peer link establishment
 * @NL80211_PLINK_ACTION_BLOCK: block traffic from this mesh peer
 * @NUM_NL80211_PLINK_ACTIONS: number of possible actions
 */
enum plink_actions {
	NL80211_PLINK_ACTION_NO_ACTION,
	NL80211_PLINK_ACTION_OPEN,
	NL80211_PLINK_ACTION_BLOCK,

	NUM_NL80211_PLINK_ACTIONS,
};


#define NL80211_KCK_LEN			16
#define NL80211_KEK_LEN			16
#define NL80211_REPLAY_CTR_LEN		8

/**
 * enum nl80211_rekey_data - attributes for GTK rekey offload
 * @__NL80211_REKEY_DATA_INVALID: invalid number for nested attributes
 * @NL80211_REKEY_DATA_KEK: key encryption key (binary)
 * @NL80211_REKEY_DATA_KCK: key confirmation key (binary)
 * @NL80211_REKEY_DATA_REPLAY_CTR: replay counter (binary)
 * @NUM_NL80211_REKEY_DATA: number of rekey attributes (internal)
 * @MAX_NL80211_REKEY_DATA: highest rekey attribute (internal)
 */
enum nl80211_rekey_data {
	__NL80211_REKEY_DATA_INVALID,
	NL80211_REKEY_DATA_KEK,
	NL80211_REKEY_DATA_KCK,
	NL80211_REKEY_DATA_REPLAY_CTR,

	/* keep last */
	NUM_NL80211_REKEY_DATA,
	MAX_NL80211_REKEY_DATA = NUM_NL80211_REKEY_DATA - 1
};

/**
 * enum nl80211_hidden_ssid - values for %NL80211_ATTR_HIDDEN_SSID
 * @NL80211_HIDDEN_SSID_NOT_IN_USE: do not hide SSID (i.e., broadcast it in
 *	Beacon frames)
 * @NL80211_HIDDEN_SSID_ZERO_LEN: hide SSID by using zero-length SSID element
 *	in Beacon frames
 * @NL80211_HIDDEN_SSID_ZERO_CONTENTS: hide SSID by using correct length of SSID
 *	element in Beacon frames but zero out each byte in the SSID
 */
enum nl80211_hidden_ssid {
	NL80211_HIDDEN_SSID_NOT_IN_USE,
	NL80211_HIDDEN_SSID_ZERO_LEN,
	NL80211_HIDDEN_SSID_ZERO_CONTENTS
};

/**
 * enum nl80211_sta_wme_attr - station WME attributes
 * @__NL80211_STA_WME_INVALID: invalid number for nested attribute
 * @NL80211_STA_WME_UAPSD_QUEUES: bitmap of uapsd queues. the format
 *	is the same as the AC bitmap in the QoS info field.
 * @NL80211_STA_WME_MAX_SP: max service period. the format is the same
 *	as the MAX_SP field in the QoS info field (but already shifted down).
 * @__NL80211_STA_WME_AFTER_LAST: internal
 * @NL80211_STA_WME_MAX: highest station WME attribute
 */
enum nl80211_sta_wme_attr {
	__NL80211_STA_WME_INVALID,
	NL80211_STA_WME_UAPSD_QUEUES,
	NL80211_STA_WME_MAX_SP,

	/* keep last */
	__NL80211_STA_WME_AFTER_LAST,
	NL80211_STA_WME_MAX = __NL80211_STA_WME_AFTER_LAST - 1
};

/**
 * enum nl80211_pmksa_candidate_attr - attributes for PMKSA caching candidates
 * @__NL80211_PMKSA_CANDIDATE_INVALID: invalid number for nested attributes
 * @NL80211_PMKSA_CANDIDATE_INDEX: candidate index (u32; the smaller, the higher
 *	priority)
 * @NL80211_PMKSA_CANDIDATE_BSSID: candidate BSSID (6 octets)
 * @NL80211_PMKSA_CANDIDATE_PREAUTH: RSN pre-authentication supported (flag)
 * @NUM_NL80211_PMKSA_CANDIDATE: number of PMKSA caching candidate attributes
 *	(internal)
 * @MAX_NL80211_PMKSA_CANDIDATE: highest PMKSA caching candidate attribute
 *	(internal)
 */
enum nl80211_pmksa_candidate_attr {
	__NL80211_PMKSA_CANDIDATE_INVALID,
	NL80211_PMKSA_CANDIDATE_INDEX,
	NL80211_PMKSA_CANDIDATE_BSSID,
	NL80211_PMKSA_CANDIDATE_PREAUTH,

	/* keep last */
	NUM_NL80211_PMKSA_CANDIDATE,
	MAX_NL80211_PMKSA_CANDIDATE = NUM_NL80211_PMKSA_CANDIDATE - 1
};

/**
 * enum nl80211_tdls_operation - values for %NL80211_ATTR_TDLS_OPERATION
 * @NL80211_TDLS_DISCOVERY_REQ: Send a TDLS discovery request
 * @NL80211_TDLS_SETUP: Setup TDLS link
 * @NL80211_TDLS_TEARDOWN: Teardown a TDLS link which is already established
 * @NL80211_TDLS_ENABLE_LINK: Enable TDLS link
 * @NL80211_TDLS_DISABLE_LINK: Disable TDLS link
 */
enum nl80211_tdls_operation {
	NL80211_TDLS_DISCOVERY_REQ,
	NL80211_TDLS_SETUP,
	NL80211_TDLS_TEARDOWN,
	NL80211_TDLS_ENABLE_LINK,
	NL80211_TDLS_DISABLE_LINK,
};

/*
 * enum nl80211_ap_sme_features - device-integrated AP features
 * Reserved for future use, no bits are defined in
 * NL80211_ATTR_DEVICE_AP_SME yet.
enum nl80211_ap_sme_features {
};
 */

/**
 * enum nl80211_feature_flags - device/driver features
 * @NL80211_FEATURE_SK_TX_STATUS: This driver supports reflecting back
 *	TX status to the socket error queue when requested with the
 *	socket option.
 * @NL80211_FEATURE_HT_IBSS: This driver supports IBSS with HT datarates.
 * @NL80211_FEATURE_INACTIVITY_TIMER: This driver takes care of freeing up
 *	the connected inactive stations in AP mode.
 * @NL80211_FEATURE_CELL_BASE_REG_HINTS: This driver has been tested
 *	to work properly to suppport receiving regulatory hints from
 *	cellular base stations.
 * @NL80211_FEATURE_P2P_DEVICE_NEEDS_CHANNEL: If this is set, an active
 *	P2P Device (%NL80211_IFTYPE_P2P_DEVICE) requires its own channel
 *	in the interface combinations, even when it's only used for scan
 *	and remain-on-channel. This could be due to, for example, the
 *	remain-on-channel implementation requiring a channel context.
 * @NL80211_FEATURE_SAE: This driver supports simultaneous authentication of
 *	equals (SAE) with user space SME (NL80211_CMD_AUTHENTICATE) in station
 *	mode
 * @NL80211_FEATURE_LOW_PRIORITY_SCAN: This driver supports low priority scan
 * @NL80211_FEATURE_SCAN_FLUSH: Scan flush is supported
 * @NL80211_FEATURE_AP_SCAN: Support scanning using an AP vif
 * @NL80211_FEATURE_VIF_TXPOWER: The driver supports per-vif TX power setting
 * @NL80211_FEATURE_NEED_OBSS_SCAN: The driver expects userspace to perform
 *	OBSS scans and generate 20/40 BSS coex reports. This flag is used only
 *	for drivers implementing the CONNECT API, for AUTH/ASSOC it is implied.
 * @NL80211_FEATURE_P2P_GO_CTWIN: P2P GO implementation supports CT Window
 *	setting
 * @NL80211_FEATURE_P2P_GO_OPPPS: P2P GO implementation supports opportunistic
 *	powersave
 * @NL80211_FEATURE_FULL_AP_CLIENT_STATE: The driver supports full state
 *	transitions for AP clients. Without this flag (and if the driver
 *	doesn't have the AP SME in the device) the driver supports adding
 *	stations only when they're associated and adds them in associated
 *	state (to later be transitioned into authorized), with this flag
 *	they should be added before even sending the authentication reply
 *	and then transitioned into authenticated, associated and authorized
 *	states using station flags.
 *	Note that even for drivers that support this, the default is to add
 *	stations in authenticated/associated state, so to add unauthenticated
 *	stations the authenticated/associated bits have to be set in the mask.
 * @NL80211_FEATURE_ADVERTISE_CHAN_LIMITS: cfg80211 advertises channel limits
 *	(HT40, VHT 80/160 MHz) if this flag is set
 */
enum nl80211_feature_flags {
	NL80211_FEATURE_SK_TX_STATUS			= 1 << 0,
	NL80211_FEATURE_HT_IBSS				= 1 << 1,
	NL80211_FEATURE_INACTIVITY_TIMER		= 1 << 2,
	NL80211_FEATURE_CELL_BASE_REG_HINTS		= 1 << 3,
	NL80211_FEATURE_P2P_DEVICE_NEEDS_CHANNEL	= 1 << 4,
	NL80211_FEATURE_SAE				= 1 << 5,
	NL80211_FEATURE_LOW_PRIORITY_SCAN		= 1 << 6,
	NL80211_FEATURE_SCAN_FLUSH			= 1 << 7,
	NL80211_FEATURE_AP_SCAN				= 1 << 8,
	NL80211_FEATURE_VIF_TXPOWER			= 1 << 9,
	NL80211_FEATURE_NEED_OBSS_SCAN			= 1 << 10,
	NL80211_FEATURE_P2P_GO_CTWIN			= 1 << 11,
	NL80211_FEATURE_P2P_GO_OPPPS			= 1 << 12,
	/* bit 13 is reserved */
	NL80211_FEATURE_ADVERTISE_CHAN_LIMITS		= 1 << 14,
	NL80211_FEATURE_FULL_AP_CLIENT_STATE		= 1 << 15,
};

/**
 * enum nl80211_probe_resp_offload_support_attr - optional supported
 *	protocols for probe-response offloading by the driver/FW.
 *	To be used with the %NL80211_ATTR_PROBE_RESP_OFFLOAD attribute.
 *	Each enum value represents a bit in the bitmap of supported
 *	protocols. Typically a subset of probe-requests belonging to a
 *	supported protocol will be excluded from offload and uploaded
 *	to the host.
 *
 * @NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS: Support for WPS ver. 1
 * @NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2: Support for WPS ver. 2
 * @NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P: Support for P2P
 * @NL80211_PROBE_RESP_OFFLOAD_SUPPORT_80211U: Support for 802.11u
 */
enum nl80211_probe_resp_offload_support_attr {
	NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS =	1<<0,
	NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 =	1<<1,
	NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P =	1<<2,
	NL80211_PROBE_RESP_OFFLOAD_SUPPORT_80211U =	1<<3,
};

/**
 * enum nl80211_connect_failed_reason - connection request failed reasons
 * @NL80211_CONN_FAIL_MAX_CLIENTS: Maximum number of clients that can be
 *	handled by the AP is reached.
 * @NL80211_CONN_FAIL_BLOCKED_CLIENT: Connection request is rejected due to ACL.
 */
enum nl80211_connect_failed_reason {
	NL80211_CONN_FAIL_MAX_CLIENTS,
	NL80211_CONN_FAIL_BLOCKED_CLIENT,
};

/**
 * enum nl80211_scan_flags -  scan request control flags
 *
 * Scan request control flags are used to control the handling
 * of NL80211_CMD_TRIGGER_SCAN and NL80211_CMD_START_SCHED_SCAN
 * requests.
 *
 * @NL80211_SCAN_FLAG_LOW_PRIORITY: scan request has low priority
 * @NL80211_SCAN_FLAG_FLUSH: flush cache before scanning
 * @NL80211_SCAN_FLAG_AP: force a scan even if the interface is configured
 *	as AP and the beaconing has already been configured. This attribute is
 *	dangerous because will destroy stations performance as a lot of frames
 *	will be lost while scanning off-channel, therefore it must be used only
 *	when really needed
 */
enum nl80211_scan_flags {
	NL80211_SCAN_FLAG_LOW_PRIORITY			= 1<<0,
	NL80211_SCAN_FLAG_FLUSH				= 1<<1,
	NL80211_SCAN_FLAG_AP				= 1<<2,
};

/**
 * enum nl80211_acl_policy - access control policy
 *
 * Access control policy is applied on a MAC list set by
 * %NL80211_CMD_START_AP and %NL80211_CMD_SET_MAC_ACL, to
 * be used with %NL80211_ATTR_ACL_POLICY.
 *
 * @NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED: Deny stations which are
 *	listed in ACL, i.e. allow all the stations which are not listed
 *	in ACL to authenticate.
 * @NL80211_ACL_POLICY_DENY_UNLESS_LISTED: Allow the stations which are listed
 *	in ACL, i.e. deny all the stations which are not listed in ACL.
 */
enum nl80211_acl_policy {
	NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED,
	NL80211_ACL_POLICY_DENY_UNLESS_LISTED,
};

/**
 * enum nl80211_radar_event - type of radar event for DFS operation
 *
 * Type of event to be used with NL80211_ATTR_RADAR_EVENT to inform userspace
 * about detected radars or success of the channel available check (CAC)
 *
 * @NL80211_RADAR_DETECTED: A radar pattern has been detected. The channel is
 *	now unusable.
 * @NL80211_RADAR_CAC_FINISHED: Channel Availability Check has been finished,
 *	the channel is now available.
 * @NL80211_RADAR_CAC_ABORTED: Channel Availability Check has been aborted, no
 *	change to the channel status.
 * @NL80211_RADAR_NOP_FINISHED: The Non-Occupancy Period for this channel is
 *	over, channel becomes usable.
 */
enum nl80211_radar_event {
	NL80211_RADAR_DETECTED,
	NL80211_RADAR_CAC_FINISHED,
	NL80211_RADAR_CAC_ABORTED,
	NL80211_RADAR_NOP_FINISHED,
};

/**
 * enum nl80211_dfs_state - DFS states for channels
 *
 * Channel states used by the DFS code.
 *
 * @IEEE80211_DFS_USABLE: The channel can be used, but channel availability
 *	check (CAC) must be performed before using it for AP or IBSS.
 * @IEEE80211_DFS_UNAVAILABLE: A radar has been detected on this channel, it
 *	is therefore marked as not available.
 * @IEEE80211_DFS_AVAILABLE: The channel has been CAC checked and is available.
 */

enum nl80211_dfs_state {
	NL80211_DFS_USABLE,
	NL80211_DFS_UNAVAILABLE,
	NL80211_DFS_AVAILABLE,
};

/**
 * enum enum nl80211_protocol_features - nl80211 protocol features
 * @NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP: nl80211 supports splitting
 *	wiphy dumps (if requested by the application with the attribute
 *	%NL80211_ATTR_SPLIT_WIPHY_DUMP. Also supported is filtering the
 *	wiphy dump by %NL80211_ATTR_WIPHY, %NL80211_ATTR_IFINDEX or
 *	%NL80211_ATTR_WDEV.
 */
enum nl80211_protocol_features {
	NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP =	1 << 0,
};

#endif /* __LINUX_NL80211_H */
