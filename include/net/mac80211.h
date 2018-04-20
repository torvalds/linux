/*
 * mac80211 <-> driver interface
 *
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018        Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MAC80211_H
#define MAC80211_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/codel.h>
#include <asm/unaligned.h>

/**
 * DOC: Introduction
 *
 * mac80211 is the Linux stack for 802.11 hardware that implements
 * only partial functionality in hard- or firmware. This document
 * defines the interface between mac80211 and low-level hardware
 * drivers.
 */

/**
 * DOC: Calling mac80211 from interrupts
 *
 * Only ieee80211_tx_status_irqsafe() and ieee80211_rx_irqsafe() can be
 * called in hardware interrupt context. The low-level driver must not call any
 * other functions in hardware interrupt context. If there is a need for such
 * call, the low-level driver should first ACK the interrupt and perform the
 * IEEE 802.11 code call after this, e.g. from a scheduled workqueue or even
 * tasklet function.
 *
 * NOTE: If the driver opts to use the _irqsafe() functions, it may not also
 *	 use the non-IRQ-safe functions!
 */

/**
 * DOC: Warning
 *
 * If you're reading this document and not the header file itself, it will
 * be incomplete because not all documentation has been converted yet.
 */

/**
 * DOC: Frame format
 *
 * As a general rule, when frames are passed between mac80211 and the driver,
 * they start with the IEEE 802.11 header and include the same octets that are
 * sent over the air except for the FCS which should be calculated by the
 * hardware.
 *
 * There are, however, various exceptions to this rule for advanced features:
 *
 * The first exception is for hardware encryption and decryption offload
 * where the IV/ICV may or may not be generated in hardware.
 *
 * Secondly, when the hardware handles fragmentation, the frame handed to
 * the driver from mac80211 is the MSDU, not the MPDU.
 */

/**
 * DOC: mac80211 workqueue
 *
 * mac80211 provides its own workqueue for drivers and internal mac80211 use.
 * The workqueue is a single threaded workqueue and can only be accessed by
 * helpers for sanity checking. Drivers must ensure all work added onto the
 * mac80211 workqueue should be cancelled on the driver stop() callback.
 *
 * mac80211 will flushed the workqueue upon interface removal and during
 * suspend.
 *
 * All work performed on the mac80211 workqueue must not acquire the RTNL lock.
 *
 */

/**
 * DOC: mac80211 software tx queueing
 *
 * mac80211 provides an optional intermediate queueing implementation designed
 * to allow the driver to keep hardware queues short and provide some fairness
 * between different stations/interfaces.
 * In this model, the driver pulls data frames from the mac80211 queue instead
 * of letting mac80211 push them via drv_tx().
 * Other frames (e.g. control or management) are still pushed using drv_tx().
 *
 * Drivers indicate that they use this model by implementing the .wake_tx_queue
 * driver operation.
 *
 * Intermediate queues (struct ieee80211_txq) are kept per-sta per-tid, with a
 * single per-vif queue for multicast data frames.
 *
 * The driver is expected to initialize its private per-queue data for stations
 * and interfaces in the .add_interface and .sta_add ops.
 *
 * The driver can't access the queue directly. To dequeue a frame, it calls
 * ieee80211_tx_dequeue(). Whenever mac80211 adds a new frame to a queue, it
 * calls the .wake_tx_queue driver op.
 *
 * For AP powersave TIM handling, the driver only needs to indicate if it has
 * buffered packets in the driver specific data structures by calling
 * ieee80211_sta_set_buffered(). For frames buffered in the ieee80211_txq
 * struct, mac80211 sets the appropriate TIM PVB bits and calls
 * .release_buffered_frames().
 * In that callback the driver is therefore expected to release its own
 * buffered frames and afterwards also frames from the ieee80211_txq (obtained
 * via the usual ieee80211_tx_dequeue).
 */

struct device;

/**
 * enum ieee80211_max_queues - maximum number of queues
 *
 * @IEEE80211_MAX_QUEUES: Maximum number of regular device queues.
 * @IEEE80211_MAX_QUEUE_MAP: bitmap with maximum queues set
 */
enum ieee80211_max_queues {
	IEEE80211_MAX_QUEUES =		16,
	IEEE80211_MAX_QUEUE_MAP =	BIT(IEEE80211_MAX_QUEUES) - 1,
};

#define IEEE80211_INVAL_HW_QUEUE	0xff

/**
 * enum ieee80211_ac_numbers - AC numbers as used in mac80211
 * @IEEE80211_AC_VO: voice
 * @IEEE80211_AC_VI: video
 * @IEEE80211_AC_BE: best effort
 * @IEEE80211_AC_BK: background
 */
enum ieee80211_ac_numbers {
	IEEE80211_AC_VO		= 0,
	IEEE80211_AC_VI		= 1,
	IEEE80211_AC_BE		= 2,
	IEEE80211_AC_BK		= 3,
};

/**
 * struct ieee80211_tx_queue_params - transmit queue configuration
 *
 * The information provided in this structure is required for QoS
 * transmit queue configuration. Cf. IEEE 802.11 7.3.2.29.
 *
 * @aifs: arbitration interframe space [0..255]
 * @cw_min: minimum contention window [a value of the form
 *	2^n-1 in the range 1..32767]
 * @cw_max: maximum contention window [like @cw_min]
 * @txop: maximum burst time in units of 32 usecs, 0 meaning disabled
 * @acm: is mandatory admission control required for the access category
 * @uapsd: is U-APSD mode enabled for the queue
 */
struct ieee80211_tx_queue_params {
	u16 txop;
	u16 cw_min;
	u16 cw_max;
	u8 aifs;
	bool acm;
	bool uapsd;
};

struct ieee80211_low_level_stats {
	unsigned int dot11ACKFailureCount;
	unsigned int dot11RTSFailureCount;
	unsigned int dot11FCSErrorCount;
	unsigned int dot11RTSSuccessCount;
};

/**
 * enum ieee80211_chanctx_change - change flag for channel context
 * @IEEE80211_CHANCTX_CHANGE_WIDTH: The channel width changed
 * @IEEE80211_CHANCTX_CHANGE_RX_CHAINS: The number of RX chains changed
 * @IEEE80211_CHANCTX_CHANGE_RADAR: radar detection flag changed
 * @IEEE80211_CHANCTX_CHANGE_CHANNEL: switched to another operating channel,
 *	this is used only with channel switching with CSA
 * @IEEE80211_CHANCTX_CHANGE_MIN_WIDTH: The min required channel width changed
 */
enum ieee80211_chanctx_change {
	IEEE80211_CHANCTX_CHANGE_WIDTH		= BIT(0),
	IEEE80211_CHANCTX_CHANGE_RX_CHAINS	= BIT(1),
	IEEE80211_CHANCTX_CHANGE_RADAR		= BIT(2),
	IEEE80211_CHANCTX_CHANGE_CHANNEL	= BIT(3),
	IEEE80211_CHANCTX_CHANGE_MIN_WIDTH	= BIT(4),
};

/**
 * struct ieee80211_chanctx_conf - channel context that vifs may be tuned to
 *
 * This is the driver-visible part. The ieee80211_chanctx
 * that contains it is visible in mac80211 only.
 *
 * @def: the channel definition
 * @min_def: the minimum channel definition currently required.
 * @rx_chains_static: The number of RX chains that must always be
 *	active on the channel to receive MIMO transmissions
 * @rx_chains_dynamic: The number of RX chains that must be enabled
 *	after RTS/CTS handshake to receive SMPS MIMO transmissions;
 *	this will always be >= @rx_chains_static.
 * @radar_enabled: whether radar detection is enabled on this channel.
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *), size is determined in hw information.
 */
struct ieee80211_chanctx_conf {
	struct cfg80211_chan_def def;
	struct cfg80211_chan_def min_def;

	u8 rx_chains_static, rx_chains_dynamic;

	bool radar_enabled;

	u8 drv_priv[0] __aligned(sizeof(void *));
};

/**
 * enum ieee80211_chanctx_switch_mode - channel context switch mode
 * @CHANCTX_SWMODE_REASSIGN_VIF: Both old and new contexts already
 *	exist (and will continue to exist), but the virtual interface
 *	needs to be switched from one to the other.
 * @CHANCTX_SWMODE_SWAP_CONTEXTS: The old context exists but will stop
 *      to exist with this call, the new context doesn't exist but
 *      will be active after this call, the virtual interface switches
 *      from the old to the new (note that the driver may of course
 *      implement this as an on-the-fly chandef switch of the existing
 *      hardware context, but the mac80211 pointer for the old context
 *      will cease to exist and only the new one will later be used
 *      for changes/removal.)
 */
enum ieee80211_chanctx_switch_mode {
	CHANCTX_SWMODE_REASSIGN_VIF,
	CHANCTX_SWMODE_SWAP_CONTEXTS,
};

/**
 * struct ieee80211_vif_chanctx_switch - vif chanctx switch information
 *
 * This is structure is used to pass information about a vif that
 * needs to switch from one chanctx to another.  The
 * &ieee80211_chanctx_switch_mode defines how the switch should be
 * done.
 *
 * @vif: the vif that should be switched from old_ctx to new_ctx
 * @old_ctx: the old context to which the vif was assigned
 * @new_ctx: the new context to which the vif must be assigned
 */
struct ieee80211_vif_chanctx_switch {
	struct ieee80211_vif *vif;
	struct ieee80211_chanctx_conf *old_ctx;
	struct ieee80211_chanctx_conf *new_ctx;
};

/**
 * enum ieee80211_bss_change - BSS change notification flags
 *
 * These flags are used with the bss_info_changed() callback
 * to indicate which BSS parameter changed.
 *
 * @BSS_CHANGED_ASSOC: association status changed (associated/disassociated),
 *	also implies a change in the AID.
 * @BSS_CHANGED_ERP_CTS_PROT: CTS protection changed
 * @BSS_CHANGED_ERP_PREAMBLE: preamble changed
 * @BSS_CHANGED_ERP_SLOT: slot timing changed
 * @BSS_CHANGED_HT: 802.11n parameters changed
 * @BSS_CHANGED_BASIC_RATES: Basic rateset changed
 * @BSS_CHANGED_BEACON_INT: Beacon interval changed
 * @BSS_CHANGED_BSSID: BSSID changed, for whatever
 *	reason (IBSS and managed mode)
 * @BSS_CHANGED_BEACON: Beacon data changed, retrieve
 *	new beacon (beaconing modes)
 * @BSS_CHANGED_BEACON_ENABLED: Beaconing should be
 *	enabled/disabled (beaconing modes)
 * @BSS_CHANGED_CQM: Connection quality monitor config changed
 * @BSS_CHANGED_IBSS: IBSS join status changed
 * @BSS_CHANGED_ARP_FILTER: Hardware ARP filter address list or state changed.
 * @BSS_CHANGED_QOS: QoS for this association was enabled/disabled. Note
 *	that it is only ever disabled for station mode.
 * @BSS_CHANGED_IDLE: Idle changed for this BSS/interface.
 * @BSS_CHANGED_SSID: SSID changed for this BSS (AP and IBSS mode)
 * @BSS_CHANGED_AP_PROBE_RESP: Probe Response changed for this BSS (AP mode)
 * @BSS_CHANGED_PS: PS changed for this BSS (STA mode)
 * @BSS_CHANGED_TXPOWER: TX power setting changed for this interface
 * @BSS_CHANGED_P2P_PS: P2P powersave settings (CTWindow, opportunistic PS)
 *	changed
 * @BSS_CHANGED_BEACON_INFO: Data from the AP's beacon became available:
 *	currently dtim_period only is under consideration.
 * @BSS_CHANGED_BANDWIDTH: The bandwidth used by this interface changed,
 *	note that this is only called when it changes after the channel
 *	context had been assigned.
 * @BSS_CHANGED_OCB: OCB join status changed
 * @BSS_CHANGED_MU_GROUPS: VHT MU-MIMO group id or user position changed
 * @BSS_CHANGED_KEEP_ALIVE: keep alive options (idle period or protected
 *	keep alive) changed.
 * @BSS_CHANGED_MCAST_RATE: Multicast Rate setting changed for this interface
 *
 */
enum ieee80211_bss_change {
	BSS_CHANGED_ASSOC		= 1<<0,
	BSS_CHANGED_ERP_CTS_PROT	= 1<<1,
	BSS_CHANGED_ERP_PREAMBLE	= 1<<2,
	BSS_CHANGED_ERP_SLOT		= 1<<3,
	BSS_CHANGED_HT			= 1<<4,
	BSS_CHANGED_BASIC_RATES		= 1<<5,
	BSS_CHANGED_BEACON_INT		= 1<<6,
	BSS_CHANGED_BSSID		= 1<<7,
	BSS_CHANGED_BEACON		= 1<<8,
	BSS_CHANGED_BEACON_ENABLED	= 1<<9,
	BSS_CHANGED_CQM			= 1<<10,
	BSS_CHANGED_IBSS		= 1<<11,
	BSS_CHANGED_ARP_FILTER		= 1<<12,
	BSS_CHANGED_QOS			= 1<<13,
	BSS_CHANGED_IDLE		= 1<<14,
	BSS_CHANGED_SSID		= 1<<15,
	BSS_CHANGED_AP_PROBE_RESP	= 1<<16,
	BSS_CHANGED_PS			= 1<<17,
	BSS_CHANGED_TXPOWER		= 1<<18,
	BSS_CHANGED_P2P_PS		= 1<<19,
	BSS_CHANGED_BEACON_INFO		= 1<<20,
	BSS_CHANGED_BANDWIDTH		= 1<<21,
	BSS_CHANGED_OCB                 = 1<<22,
	BSS_CHANGED_MU_GROUPS		= 1<<23,
	BSS_CHANGED_KEEP_ALIVE		= 1<<24,
	BSS_CHANGED_MCAST_RATE		= 1<<25,

	/* when adding here, make sure to change ieee80211_reconfig */
};

/*
 * The maximum number of IPv4 addresses listed for ARP filtering. If the number
 * of addresses for an interface increase beyond this value, hardware ARP
 * filtering will be disabled.
 */
#define IEEE80211_BSS_ARP_ADDR_LIST_LEN 4

/**
 * enum ieee80211_event_type - event to be notified to the low level driver
 * @RSSI_EVENT: AP's rssi crossed the a threshold set by the driver.
 * @MLME_EVENT: event related to MLME
 * @BAR_RX_EVENT: a BAR was received
 * @BA_FRAME_TIMEOUT: Frames were released from the reordering buffer because
 *	they timed out. This won't be called for each frame released, but only
 *	once each time the timeout triggers.
 */
enum ieee80211_event_type {
	RSSI_EVENT,
	MLME_EVENT,
	BAR_RX_EVENT,
	BA_FRAME_TIMEOUT,
};

/**
 * enum ieee80211_rssi_event_data - relevant when event type is %RSSI_EVENT
 * @RSSI_EVENT_HIGH: AP's rssi went below the threshold set by the driver.
 * @RSSI_EVENT_LOW: AP's rssi went above the threshold set by the driver.
 */
enum ieee80211_rssi_event_data {
	RSSI_EVENT_HIGH,
	RSSI_EVENT_LOW,
};

/**
 * struct ieee80211_rssi_event - data attached to an %RSSI_EVENT
 * @data: See &enum ieee80211_rssi_event_data
 */
struct ieee80211_rssi_event {
	enum ieee80211_rssi_event_data data;
};

/**
 * enum ieee80211_mlme_event_data - relevant when event type is %MLME_EVENT
 * @AUTH_EVENT: the MLME operation is authentication
 * @ASSOC_EVENT: the MLME operation is association
 * @DEAUTH_RX_EVENT: deauth received..
 * @DEAUTH_TX_EVENT: deauth sent.
 */
enum ieee80211_mlme_event_data {
	AUTH_EVENT,
	ASSOC_EVENT,
	DEAUTH_RX_EVENT,
	DEAUTH_TX_EVENT,
};

/**
 * enum ieee80211_mlme_event_status - relevant when event type is %MLME_EVENT
 * @MLME_SUCCESS: the MLME operation completed successfully.
 * @MLME_DENIED: the MLME operation was denied by the peer.
 * @MLME_TIMEOUT: the MLME operation timed out.
 */
enum ieee80211_mlme_event_status {
	MLME_SUCCESS,
	MLME_DENIED,
	MLME_TIMEOUT,
};

/**
 * struct ieee80211_mlme_event - data attached to an %MLME_EVENT
 * @data: See &enum ieee80211_mlme_event_data
 * @status: See &enum ieee80211_mlme_event_status
 * @reason: the reason code if applicable
 */
struct ieee80211_mlme_event {
	enum ieee80211_mlme_event_data data;
	enum ieee80211_mlme_event_status status;
	u16 reason;
};

/**
 * struct ieee80211_ba_event - data attached for BlockAck related events
 * @sta: pointer to the &ieee80211_sta to which this event relates
 * @tid: the tid
 * @ssn: the starting sequence number (for %BAR_RX_EVENT)
 */
struct ieee80211_ba_event {
	struct ieee80211_sta *sta;
	u16 tid;
	u16 ssn;
};

/**
 * struct ieee80211_event - event to be sent to the driver
 * @type: The event itself. See &enum ieee80211_event_type.
 * @rssi: relevant if &type is %RSSI_EVENT
 * @mlme: relevant if &type is %AUTH_EVENT
 * @ba: relevant if &type is %BAR_RX_EVENT or %BA_FRAME_TIMEOUT
 * @u:union holding the fields above
 */
struct ieee80211_event {
	enum ieee80211_event_type type;
	union {
		struct ieee80211_rssi_event rssi;
		struct ieee80211_mlme_event mlme;
		struct ieee80211_ba_event ba;
	} u;
};

/**
 * struct ieee80211_mu_group_data - STA's VHT MU-MIMO group data
 *
 * This structure describes the group id data of VHT MU-MIMO
 *
 * @membership: 64 bits array - a bit is set if station is member of the group
 * @position: 2 bits per group id indicating the position in the group
 */
struct ieee80211_mu_group_data {
	u8 membership[WLAN_MEMBERSHIP_LEN];
	u8 position[WLAN_USER_POSITION_LEN];
};

/**
 * struct ieee80211_bss_conf - holds the BSS's changing parameters
 *
 * This structure keeps information about a BSS (and an association
 * to that BSS) that can change during the lifetime of the BSS.
 *
 * @assoc: association status
 * @ibss_joined: indicates whether this station is part of an IBSS
 *	or not
 * @ibss_creator: indicates if a new IBSS network is being created
 * @aid: association ID number, valid only when @assoc is true
 * @use_cts_prot: use CTS protection
 * @use_short_preamble: use 802.11b short preamble
 * @use_short_slot: use short slot time (only relevant for ERP)
 * @dtim_period: num of beacons before the next DTIM, for beaconing,
 *	valid in station mode only if after the driver was notified
 *	with the %BSS_CHANGED_BEACON_INFO flag, will be non-zero then.
 * @sync_tsf: last beacon's/probe response's TSF timestamp (could be old
 *	as it may have been received during scanning long ago). If the
 *	HW flag %IEEE80211_HW_TIMING_BEACON_ONLY is set, then this can
 *	only come from a beacon, but might not become valid until after
 *	association when a beacon is received (which is notified with the
 *	%BSS_CHANGED_DTIM flag.). See also sync_dtim_count important notice.
 * @sync_device_ts: the device timestamp corresponding to the sync_tsf,
 *	the driver/device can use this to calculate synchronisation
 *	(see @sync_tsf). See also sync_dtim_count important notice.
 * @sync_dtim_count: Only valid when %IEEE80211_HW_TIMING_BEACON_ONLY
 *	is requested, see @sync_tsf/@sync_device_ts.
 *	IMPORTANT: These three sync_* parameters would possibly be out of sync
 *	by the time the driver will use them. The synchronized view is currently
 *	guaranteed only in certain callbacks.
 * @beacon_int: beacon interval
 * @assoc_capability: capabilities taken from assoc resp
 * @basic_rates: bitmap of basic rates, each bit stands for an
 *	index into the rate table configured by the driver in
 *	the current band.
 * @beacon_rate: associated AP's beacon TX rate
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @bssid: The BSSID for this BSS
 * @enable_beacon: whether beaconing should be enabled or not
 * @chandef: Channel definition for this BSS -- the hardware might be
 *	configured a higher bandwidth than this BSS uses, for example.
 * @mu_group: VHT MU-MIMO group membership data
 * @ht_operation_mode: HT operation mode like in &struct ieee80211_ht_operation.
 *	This field is only valid when the channel is a wide HT/VHT channel.
 *	Note that with TDLS this can be the case (channel is HT, protection must
 *	be used from this field) even when the BSS association isn't using HT.
 * @cqm_rssi_thold: Connection quality monitor RSSI threshold, a zero value
 *	implies disabled. As with the cfg80211 callback, a change here should
 *	cause an event to be sent indicating where the current value is in
 *	relation to the newly configured threshold.
 * @cqm_rssi_low: Connection quality monitor RSSI lower threshold, a zero value
 *	implies disabled.  This is an alternative mechanism to the single
 *	threshold event and can't be enabled simultaneously with it.
 * @cqm_rssi_high: Connection quality monitor RSSI upper threshold.
 * @cqm_rssi_hyst: Connection quality monitor RSSI hysteresis
 * @arp_addr_list: List of IPv4 addresses for hardware ARP filtering. The
 *	may filter ARP queries targeted for other addresses than listed here.
 *	The driver must allow ARP queries targeted for all address listed here
 *	to pass through. An empty list implies no ARP queries need to pass.
 * @arp_addr_cnt: Number of addresses currently on the list. Note that this
 *	may be larger than %IEEE80211_BSS_ARP_ADDR_LIST_LEN (the arp_addr_list
 *	array size), it's up to the driver what to do in that case.
 * @qos: This is a QoS-enabled BSS.
 * @idle: This interface is idle. There's also a global idle flag in the
 *	hardware config which may be more appropriate depending on what
 *	your driver/device needs to do.
 * @ps: power-save mode (STA only). This flag is NOT affected by
 *	offchannel/dynamic_ps operations.
 * @ssid: The SSID of the current vif. Valid in AP and IBSS mode.
 * @ssid_len: Length of SSID given in @ssid.
 * @hidden_ssid: The SSID of the current vif is hidden. Only valid in AP-mode.
 * @txpower: TX power in dBm
 * @txpower_type: TX power adjustment used to control per packet Transmit
 *	Power Control (TPC) in lower driver for the current vif. In particular
 *	TPC is enabled if value passed in %txpower_type is
 *	NL80211_TX_POWER_LIMITED (allow using less than specified from
 *	userspace), whereas TPC is disabled if %txpower_type is set to
 *	NL80211_TX_POWER_FIXED (use value configured from userspace)
 * @p2p_noa_attr: P2P NoA attribute for P2P powersave
 * @allow_p2p_go_ps: indication for AP or P2P GO interface, whether it's allowed
 *	to use P2P PS mechanism or not. AP/P2P GO is not allowed to use P2P PS
 *	if it has associated clients without P2P PS support.
 * @max_idle_period: the time period during which the station can refrain from
 *	transmitting frames to its associated AP without being disassociated.
 *	In units of 1000 TUs. Zero value indicates that the AP did not include
 *	a (valid) BSS Max Idle Period Element.
 * @protected_keep_alive: if set, indicates that the station should send an RSN
 *	protected frame to the AP to reset the idle timer at the AP for the
 *	station.
 */
struct ieee80211_bss_conf {
	const u8 *bssid;
	/* association related data */
	bool assoc, ibss_joined;
	bool ibss_creator;
	u16 aid;
	/* erp related data */
	bool use_cts_prot;
	bool use_short_preamble;
	bool use_short_slot;
	bool enable_beacon;
	u8 dtim_period;
	u16 beacon_int;
	u16 assoc_capability;
	u64 sync_tsf;
	u32 sync_device_ts;
	u8 sync_dtim_count;
	u32 basic_rates;
	struct ieee80211_rate *beacon_rate;
	int mcast_rate[NUM_NL80211_BANDS];
	u16 ht_operation_mode;
	s32 cqm_rssi_thold;
	u32 cqm_rssi_hyst;
	s32 cqm_rssi_low;
	s32 cqm_rssi_high;
	struct cfg80211_chan_def chandef;
	struct ieee80211_mu_group_data mu_group;
	__be32 arp_addr_list[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	int arp_addr_cnt;
	bool qos;
	bool idle;
	bool ps;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;
	bool hidden_ssid;
	int txpower;
	enum nl80211_tx_power_setting txpower_type;
	struct ieee80211_p2p_noa_attr p2p_noa_attr;
	bool allow_p2p_go_ps;
	u16 max_idle_period;
	bool protected_keep_alive;
};

/**
 * enum mac80211_tx_info_flags - flags to describe transmission information/status
 *
 * These flags are used with the @flags member of &ieee80211_tx_info.
 *
 * @IEEE80211_TX_CTL_REQ_TX_STATUS: require TX status callback for this frame.
 * @IEEE80211_TX_CTL_ASSIGN_SEQ: The driver has to assign a sequence
 *	number to this frame, taking care of not overwriting the fragment
 *	number and increasing the sequence number only when the
 *	IEEE80211_TX_CTL_FIRST_FRAGMENT flag is set. mac80211 will properly
 *	assign sequence numbers to QoS-data frames but cannot do so correctly
 *	for non-QoS-data and management frames because beacons need them from
 *	that counter as well and mac80211 cannot guarantee proper sequencing.
 *	If this flag is set, the driver should instruct the hardware to
 *	assign a sequence number to the frame or assign one itself. Cf. IEEE
 *	802.11-2007 7.1.3.4.1 paragraph 3. This flag will always be set for
 *	beacons and always be clear for frames without a sequence number field.
 * @IEEE80211_TX_CTL_NO_ACK: tell the low level not to wait for an ack
 * @IEEE80211_TX_CTL_CLEAR_PS_FILT: clear powersave filter for destination
 *	station
 * @IEEE80211_TX_CTL_FIRST_FRAGMENT: this is a first fragment of the frame
 * @IEEE80211_TX_CTL_SEND_AFTER_DTIM: send this frame after DTIM beacon
 * @IEEE80211_TX_CTL_AMPDU: this frame should be sent as part of an A-MPDU
 * @IEEE80211_TX_CTL_INJECTED: Frame was injected, internal to mac80211.
 * @IEEE80211_TX_STAT_TX_FILTERED: The frame was not transmitted
 *	because the destination STA was in powersave mode. Note that to
 *	avoid race conditions, the filter must be set by the hardware or
 *	firmware upon receiving a frame that indicates that the station
 *	went to sleep (must be done on device to filter frames already on
 *	the queue) and may only be unset after mac80211 gives the OK for
 *	that by setting the IEEE80211_TX_CTL_CLEAR_PS_FILT (see above),
 *	since only then is it guaranteed that no more frames are in the
 *	hardware queue.
 * @IEEE80211_TX_STAT_ACK: Frame was acknowledged
 * @IEEE80211_TX_STAT_AMPDU: The frame was aggregated, so status
 * 	is for the whole aggregation.
 * @IEEE80211_TX_STAT_AMPDU_NO_BACK: no block ack was returned,
 * 	so consider using block ack request (BAR).
 * @IEEE80211_TX_CTL_RATE_CTRL_PROBE: internal to mac80211, can be
 *	set by rate control algorithms to indicate probe rate, will
 *	be cleared for fragmented frames (except on the last fragment)
 * @IEEE80211_TX_INTFL_OFFCHAN_TX_OK: Internal to mac80211. Used to indicate
 *	that a frame can be transmitted while the queues are stopped for
 *	off-channel operation.
 * @IEEE80211_TX_INTFL_NEED_TXPROCESSING: completely internal to mac80211,
 *	used to indicate that a pending frame requires TX processing before
 *	it can be sent out.
 * @IEEE80211_TX_INTFL_RETRIED: completely internal to mac80211,
 *	used to indicate that a frame was already retried due to PS
 * @IEEE80211_TX_INTFL_DONT_ENCRYPT: completely internal to mac80211,
 *	used to indicate frame should not be encrypted
 * @IEEE80211_TX_CTL_NO_PS_BUFFER: This frame is a response to a poll
 *	frame (PS-Poll or uAPSD) or a non-bufferable MMPDU and must
 *	be sent although the station is in powersave mode.
 * @IEEE80211_TX_CTL_MORE_FRAMES: More frames will be passed to the
 *	transmit function after the current frame, this can be used
 *	by drivers to kick the DMA queue only if unset or when the
 *	queue gets full.
 * @IEEE80211_TX_INTFL_RETRANSMISSION: This frame is being retransmitted
 *	after TX status because the destination was asleep, it must not
 *	be modified again (no seqno assignment, crypto, etc.)
 * @IEEE80211_TX_INTFL_MLME_CONN_TX: This frame was transmitted by the MLME
 *	code for connection establishment, this indicates that its status
 *	should kick the MLME state machine.
 * @IEEE80211_TX_INTFL_NL80211_FRAME_TX: Frame was requested through nl80211
 *	MLME command (internal to mac80211 to figure out whether to send TX
 *	status to user space)
 * @IEEE80211_TX_CTL_LDPC: tells the driver to use LDPC for this frame
 * @IEEE80211_TX_CTL_STBC: Enables Space-Time Block Coding (STBC) for this
 *	frame and selects the maximum number of streams that it can use.
 * @IEEE80211_TX_CTL_TX_OFFCHAN: Marks this packet to be transmitted on
 *	the off-channel channel when a remain-on-channel offload is done
 *	in hardware -- normal packets still flow and are expected to be
 *	handled properly by the device.
 * @IEEE80211_TX_INTFL_TKIP_MIC_FAILURE: Marks this packet to be used for TKIP
 *	testing. It will be sent out with incorrect Michael MIC key to allow
 *	TKIP countermeasures to be tested.
 * @IEEE80211_TX_CTL_NO_CCK_RATE: This frame will be sent at non CCK rate.
 *	This flag is actually used for management frame especially for P2P
 *	frames not being sent at CCK rate in 2GHz band.
 * @IEEE80211_TX_STATUS_EOSP: This packet marks the end of service period,
 *	when its status is reported the service period ends. For frames in
 *	an SP that mac80211 transmits, it is already set; for driver frames
 *	the driver may set this flag. It is also used to do the same for
 *	PS-Poll responses.
 * @IEEE80211_TX_CTL_USE_MINRATE: This frame will be sent at lowest rate.
 *	This flag is used to send nullfunc frame at minimum rate when
 *	the nullfunc is used for connection monitoring purpose.
 * @IEEE80211_TX_CTL_DONTFRAG: Don't fragment this packet even if it
 *	would be fragmented by size (this is optional, only used for
 *	monitor injection).
 * @IEEE80211_TX_STAT_NOACK_TRANSMITTED: A frame that was marked with
 *	IEEE80211_TX_CTL_NO_ACK has been successfully transmitted without
 *	any errors (like issues specific to the driver/HW).
 *	This flag must not be set for frames that don't request no-ack
 *	behaviour with IEEE80211_TX_CTL_NO_ACK.
 *
 * Note: If you have to add new flags to the enumeration, then don't
 *	 forget to update %IEEE80211_TX_TEMPORARY_FLAGS when necessary.
 */
enum mac80211_tx_info_flags {
	IEEE80211_TX_CTL_REQ_TX_STATUS		= BIT(0),
	IEEE80211_TX_CTL_ASSIGN_SEQ		= BIT(1),
	IEEE80211_TX_CTL_NO_ACK			= BIT(2),
	IEEE80211_TX_CTL_CLEAR_PS_FILT		= BIT(3),
	IEEE80211_TX_CTL_FIRST_FRAGMENT		= BIT(4),
	IEEE80211_TX_CTL_SEND_AFTER_DTIM	= BIT(5),
	IEEE80211_TX_CTL_AMPDU			= BIT(6),
	IEEE80211_TX_CTL_INJECTED		= BIT(7),
	IEEE80211_TX_STAT_TX_FILTERED		= BIT(8),
	IEEE80211_TX_STAT_ACK			= BIT(9),
	IEEE80211_TX_STAT_AMPDU			= BIT(10),
	IEEE80211_TX_STAT_AMPDU_NO_BACK		= BIT(11),
	IEEE80211_TX_CTL_RATE_CTRL_PROBE	= BIT(12),
	IEEE80211_TX_INTFL_OFFCHAN_TX_OK	= BIT(13),
	IEEE80211_TX_INTFL_NEED_TXPROCESSING	= BIT(14),
	IEEE80211_TX_INTFL_RETRIED		= BIT(15),
	IEEE80211_TX_INTFL_DONT_ENCRYPT		= BIT(16),
	IEEE80211_TX_CTL_NO_PS_BUFFER		= BIT(17),
	IEEE80211_TX_CTL_MORE_FRAMES		= BIT(18),
	IEEE80211_TX_INTFL_RETRANSMISSION	= BIT(19),
	IEEE80211_TX_INTFL_MLME_CONN_TX		= BIT(20),
	IEEE80211_TX_INTFL_NL80211_FRAME_TX	= BIT(21),
	IEEE80211_TX_CTL_LDPC			= BIT(22),
	IEEE80211_TX_CTL_STBC			= BIT(23) | BIT(24),
	IEEE80211_TX_CTL_TX_OFFCHAN		= BIT(25),
	IEEE80211_TX_INTFL_TKIP_MIC_FAILURE	= BIT(26),
	IEEE80211_TX_CTL_NO_CCK_RATE		= BIT(27),
	IEEE80211_TX_STATUS_EOSP		= BIT(28),
	IEEE80211_TX_CTL_USE_MINRATE		= BIT(29),
	IEEE80211_TX_CTL_DONTFRAG		= BIT(30),
	IEEE80211_TX_STAT_NOACK_TRANSMITTED	= BIT(31),
};

#define IEEE80211_TX_CTL_STBC_SHIFT		23

/**
 * enum mac80211_tx_control_flags - flags to describe transmit control
 *
 * @IEEE80211_TX_CTRL_PORT_CTRL_PROTO: this frame is a port control
 *	protocol frame (e.g. EAP)
 * @IEEE80211_TX_CTRL_PS_RESPONSE: This frame is a response to a poll
 *	frame (PS-Poll or uAPSD).
 * @IEEE80211_TX_CTRL_RATE_INJECT: This frame is injected with rate information
 * @IEEE80211_TX_CTRL_AMSDU: This frame is an A-MSDU frame
 * @IEEE80211_TX_CTRL_FAST_XMIT: This frame is going through the fast_xmit path
 *
 * These flags are used in tx_info->control.flags.
 */
enum mac80211_tx_control_flags {
	IEEE80211_TX_CTRL_PORT_CTRL_PROTO	= BIT(0),
	IEEE80211_TX_CTRL_PS_RESPONSE		= BIT(1),
	IEEE80211_TX_CTRL_RATE_INJECT		= BIT(2),
	IEEE80211_TX_CTRL_AMSDU			= BIT(3),
	IEEE80211_TX_CTRL_FAST_XMIT		= BIT(4),
};

/*
 * This definition is used as a mask to clear all temporary flags, which are
 * set by the tx handlers for each transmission attempt by the mac80211 stack.
 */
#define IEEE80211_TX_TEMPORARY_FLAGS (IEEE80211_TX_CTL_NO_ACK |		      \
	IEEE80211_TX_CTL_CLEAR_PS_FILT | IEEE80211_TX_CTL_FIRST_FRAGMENT |    \
	IEEE80211_TX_CTL_SEND_AFTER_DTIM | IEEE80211_TX_CTL_AMPDU |	      \
	IEEE80211_TX_STAT_TX_FILTERED |	IEEE80211_TX_STAT_ACK |		      \
	IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_STAT_AMPDU_NO_BACK |	      \
	IEEE80211_TX_CTL_RATE_CTRL_PROBE | IEEE80211_TX_CTL_NO_PS_BUFFER |    \
	IEEE80211_TX_CTL_MORE_FRAMES | IEEE80211_TX_CTL_LDPC |		      \
	IEEE80211_TX_CTL_STBC | IEEE80211_TX_STATUS_EOSP)

/**
 * enum mac80211_rate_control_flags - per-rate flags set by the
 *	Rate Control algorithm.
 *
 * These flags are set by the Rate control algorithm for each rate during tx,
 * in the @flags member of struct ieee80211_tx_rate.
 *
 * @IEEE80211_TX_RC_USE_RTS_CTS: Use RTS/CTS exchange for this rate.
 * @IEEE80211_TX_RC_USE_CTS_PROTECT: CTS-to-self protection is required.
 *	This is set if the current BSS requires ERP protection.
 * @IEEE80211_TX_RC_USE_SHORT_PREAMBLE: Use short preamble.
 * @IEEE80211_TX_RC_MCS: HT rate.
 * @IEEE80211_TX_RC_VHT_MCS: VHT MCS rate, in this case the idx field is split
 *	into a higher 4 bits (Nss) and lower 4 bits (MCS number)
 * @IEEE80211_TX_RC_GREEN_FIELD: Indicates whether this rate should be used in
 *	Greenfield mode.
 * @IEEE80211_TX_RC_40_MHZ_WIDTH: Indicates if the Channel Width should be 40 MHz.
 * @IEEE80211_TX_RC_80_MHZ_WIDTH: Indicates 80 MHz transmission
 * @IEEE80211_TX_RC_160_MHZ_WIDTH: Indicates 160 MHz transmission
 *	(80+80 isn't supported yet)
 * @IEEE80211_TX_RC_DUP_DATA: The frame should be transmitted on both of the
 *	adjacent 20 MHz channels, if the current channel type is
 *	NL80211_CHAN_HT40MINUS or NL80211_CHAN_HT40PLUS.
 * @IEEE80211_TX_RC_SHORT_GI: Short Guard interval should be used for this rate.
 */
enum mac80211_rate_control_flags {
	IEEE80211_TX_RC_USE_RTS_CTS		= BIT(0),
	IEEE80211_TX_RC_USE_CTS_PROTECT		= BIT(1),
	IEEE80211_TX_RC_USE_SHORT_PREAMBLE	= BIT(2),

	/* rate index is an HT/VHT MCS instead of an index */
	IEEE80211_TX_RC_MCS			= BIT(3),
	IEEE80211_TX_RC_GREEN_FIELD		= BIT(4),
	IEEE80211_TX_RC_40_MHZ_WIDTH		= BIT(5),
	IEEE80211_TX_RC_DUP_DATA		= BIT(6),
	IEEE80211_TX_RC_SHORT_GI		= BIT(7),
	IEEE80211_TX_RC_VHT_MCS			= BIT(8),
	IEEE80211_TX_RC_80_MHZ_WIDTH		= BIT(9),
	IEEE80211_TX_RC_160_MHZ_WIDTH		= BIT(10),
};


/* there are 40 bytes if you don't need the rateset to be kept */
#define IEEE80211_TX_INFO_DRIVER_DATA_SIZE 40

/* if you do need the rateset, then you have less space */
#define IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE 24

/* maximum number of rate stages */
#define IEEE80211_TX_MAX_RATES	4

/* maximum number of rate table entries */
#define IEEE80211_TX_RATE_TABLE_SIZE	4

/**
 * struct ieee80211_tx_rate - rate selection/status
 *
 * @idx: rate index to attempt to send with
 * @flags: rate control flags (&enum mac80211_rate_control_flags)
 * @count: number of tries in this rate before going to the next rate
 *
 * A value of -1 for @idx indicates an invalid rate and, if used
 * in an array of retry rates, that no more rates should be tried.
 *
 * When used for transmit status reporting, the driver should
 * always report the rate along with the flags it used.
 *
 * &struct ieee80211_tx_info contains an array of these structs
 * in the control information, and it will be filled by the rate
 * control algorithm according to what should be sent. For example,
 * if this array contains, in the format { <idx>, <count> } the
 * information::
 *
 *    { 3, 2 }, { 2, 2 }, { 1, 4 }, { -1, 0 }, { -1, 0 }
 *
 * then this means that the frame should be transmitted
 * up to twice at rate 3, up to twice at rate 2, and up to four
 * times at rate 1 if it doesn't get acknowledged. Say it gets
 * acknowledged by the peer after the fifth attempt, the status
 * information should then contain::
 *
 *   { 3, 2 }, { 2, 2 }, { 1, 1 }, { -1, 0 } ...
 *
 * since it was transmitted twice at rate 3, twice at rate 2
 * and once at rate 1 after which we received an acknowledgement.
 */
struct ieee80211_tx_rate {
	s8 idx;
	u16 count:5,
	    flags:11;
} __packed;

#define IEEE80211_MAX_TX_RETRY		31

static inline void ieee80211_rate_set_vht(struct ieee80211_tx_rate *rate,
					  u8 mcs, u8 nss)
{
	WARN_ON(mcs & ~0xF);
	WARN_ON((nss - 1) & ~0x7);
	rate->idx = ((nss - 1) << 4) | mcs;
}

static inline u8
ieee80211_rate_get_vht_mcs(const struct ieee80211_tx_rate *rate)
{
	return rate->idx & 0xF;
}

static inline u8
ieee80211_rate_get_vht_nss(const struct ieee80211_tx_rate *rate)
{
	return (rate->idx >> 4) + 1;
}

/**
 * struct ieee80211_tx_info - skb transmit information
 *
 * This structure is placed in skb->cb for three uses:
 *  (1) mac80211 TX control - mac80211 tells the driver what to do
 *  (2) driver internal use (if applicable)
 *  (3) TX status information - driver tells mac80211 what happened
 *
 * @flags: transmit info flags, defined above
 * @band: the band to transmit on (use for checking for races)
 * @hw_queue: HW queue to put the frame on, skb_get_queue_mapping() gives the AC
 * @ack_frame_id: internal frame ID for TX status, used internally
 * @control: union for control data
 * @status: union for status data
 * @driver_data: array of driver_data pointers
 * @ampdu_ack_len: number of acked aggregated frames.
 * 	relevant only if IEEE80211_TX_STAT_AMPDU was set.
 * @ampdu_len: number of aggregated frames.
 * 	relevant only if IEEE80211_TX_STAT_AMPDU was set.
 * @ack_signal: signal strength of the ACK frame
 */
struct ieee80211_tx_info {
	/* common information */
	u32 flags;
	u8 band;

	u8 hw_queue;

	u16 ack_frame_id;

	union {
		struct {
			union {
				/* rate control */
				struct {
					struct ieee80211_tx_rate rates[
						IEEE80211_TX_MAX_RATES];
					s8 rts_cts_rate_idx;
					u8 use_rts:1;
					u8 use_cts_prot:1;
					u8 short_preamble:1;
					u8 skip_table:1;
					/* 2 bytes free */
				};
				/* only needed before rate control */
				unsigned long jiffies;
			};
			/* NB: vif can be NULL for injected frames */
			struct ieee80211_vif *vif;
			struct ieee80211_key_conf *hw_key;
			u32 flags;
			codel_time_t enqueue_time;
		} control;
		struct {
			u64 cookie;
		} ack;
		struct {
			struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
			s32 ack_signal;
			u8 ampdu_ack_len;
			u8 ampdu_len;
			u8 antenna;
			u16 tx_time;
			bool is_valid_ack_signal;
			void *status_driver_data[19 / sizeof(void *)];
		} status;
		struct {
			struct ieee80211_tx_rate driver_rates[
				IEEE80211_TX_MAX_RATES];
			u8 pad[4];

			void *rate_driver_data[
				IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE / sizeof(void *)];
		};
		void *driver_data[
			IEEE80211_TX_INFO_DRIVER_DATA_SIZE / sizeof(void *)];
	};
};

/**
 * struct ieee80211_tx_status - extended tx staus info for rate control
 *
 * @sta: Station that the packet was transmitted for
 * @info: Basic tx status information
 * @skb: Packet skb (can be NULL if not provided by the driver)
 */
struct ieee80211_tx_status {
	struct ieee80211_sta *sta;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
};

/**
 * struct ieee80211_scan_ies - descriptors for different blocks of IEs
 *
 * This structure is used to point to different blocks of IEs in HW scan
 * and scheduled scan. These blocks contain the IEs passed by userspace
 * and the ones generated by mac80211.
 *
 * @ies: pointers to band specific IEs.
 * @len: lengths of band_specific IEs.
 * @common_ies: IEs for all bands (especially vendor specific ones)
 * @common_ie_len: length of the common_ies
 */
struct ieee80211_scan_ies {
	const u8 *ies[NUM_NL80211_BANDS];
	size_t len[NUM_NL80211_BANDS];
	const u8 *common_ies;
	size_t common_ie_len;
};


static inline struct ieee80211_tx_info *IEEE80211_SKB_CB(struct sk_buff *skb)
{
	return (struct ieee80211_tx_info *)skb->cb;
}

static inline struct ieee80211_rx_status *IEEE80211_SKB_RXCB(struct sk_buff *skb)
{
	return (struct ieee80211_rx_status *)skb->cb;
}

/**
 * ieee80211_tx_info_clear_status - clear TX status
 *
 * @info: The &struct ieee80211_tx_info to be cleared.
 *
 * When the driver passes an skb back to mac80211, it must report
 * a number of things in TX status. This function clears everything
 * in the TX status but the rate control information (it does clear
 * the count since you need to fill that in anyway).
 *
 * NOTE: You can only use this function if you do NOT use
 *	 info->driver_data! Use info->rate_driver_data
 *	 instead if you need only the less space that allows.
 */
static inline void
ieee80211_tx_info_clear_status(struct ieee80211_tx_info *info)
{
	int i;

	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info, status.rates) !=
		     offsetof(struct ieee80211_tx_info, control.rates));
	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info, status.rates) !=
		     offsetof(struct ieee80211_tx_info, driver_rates));
	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info, status.rates) != 8);
	/* clear the rate counts */
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++)
		info->status.rates[i].count = 0;

	BUILD_BUG_ON(
	    offsetof(struct ieee80211_tx_info, status.ack_signal) != 20);
	memset(&info->status.ampdu_ack_len, 0,
	       sizeof(struct ieee80211_tx_info) -
	       offsetof(struct ieee80211_tx_info, status.ampdu_ack_len));
}


/**
 * enum mac80211_rx_flags - receive flags
 *
 * These flags are used with the @flag member of &struct ieee80211_rx_status.
 * @RX_FLAG_MMIC_ERROR: Michael MIC error was reported on this frame.
 *	Use together with %RX_FLAG_MMIC_STRIPPED.
 * @RX_FLAG_DECRYPTED: This frame was decrypted in hardware.
 * @RX_FLAG_MMIC_STRIPPED: the Michael MIC is stripped off this frame,
 *	verification has been done by the hardware.
 * @RX_FLAG_IV_STRIPPED: The IV and ICV are stripped from this frame.
 *	If this flag is set, the stack cannot do any replay detection
 *	hence the driver or hardware will have to do that.
 * @RX_FLAG_PN_VALIDATED: Currently only valid for CCMP/GCMP frames, this
 *	flag indicates that the PN was verified for replay protection.
 *	Note that this flag is also currently only supported when a frame
 *	is also decrypted (ie. @RX_FLAG_DECRYPTED must be set)
 * @RX_FLAG_DUP_VALIDATED: The driver should set this flag if it did
 *	de-duplication by itself.
 * @RX_FLAG_FAILED_FCS_CRC: Set this flag if the FCS check failed on
 *	the frame.
 * @RX_FLAG_FAILED_PLCP_CRC: Set this flag if the PCLP check failed on
 *	the frame.
 * @RX_FLAG_MACTIME_START: The timestamp passed in the RX status (@mactime
 *	field) is valid and contains the time the first symbol of the MPDU
 *	was received. This is useful in monitor mode and for proper IBSS
 *	merging.
 * @RX_FLAG_MACTIME_END: The timestamp passed in the RX status (@mactime
 *	field) is valid and contains the time the last symbol of the MPDU
 *	(including FCS) was received.
 * @RX_FLAG_MACTIME_PLCP_START: The timestamp passed in the RX status (@mactime
 *	field) is valid and contains the time the SYNC preamble was received.
 * @RX_FLAG_NO_SIGNAL_VAL: The signal strength value is not present.
 *	Valid only for data frames (mainly A-MPDU)
 * @RX_FLAG_AMPDU_DETAILS: A-MPDU details are known, in particular the reference
 *	number (@ampdu_reference) must be populated and be a distinct number for
 *	each A-MPDU
 * @RX_FLAG_AMPDU_LAST_KNOWN: last subframe is known, should be set on all
 *	subframes of a single A-MPDU
 * @RX_FLAG_AMPDU_IS_LAST: this subframe is the last subframe of the A-MPDU
 * @RX_FLAG_AMPDU_DELIM_CRC_ERROR: A delimiter CRC error has been detected
 *	on this subframe
 * @RX_FLAG_AMPDU_DELIM_CRC_KNOWN: The delimiter CRC field is known (the CRC
 *	is stored in the @ampdu_delimiter_crc field)
 * @RX_FLAG_MIC_STRIPPED: The mic was stripped of this packet. Decryption was
 *	done by the hardware
 * @RX_FLAG_ONLY_MONITOR: Report frame only to monitor interfaces without
 *	processing it in any regular way.
 *	This is useful if drivers offload some frames but still want to report
 *	them for sniffing purposes.
 * @RX_FLAG_SKIP_MONITOR: Process and report frame to all interfaces except
 *	monitor interfaces.
 *	This is useful if drivers offload some frames but still want to report
 *	them for sniffing purposes.
 * @RX_FLAG_AMSDU_MORE: Some drivers may prefer to report separate A-MSDU
 *	subframes instead of a one huge frame for performance reasons.
 *	All, but the last MSDU from an A-MSDU should have this flag set. E.g.
 *	if an A-MSDU has 3 frames, the first 2 must have the flag set, while
 *	the 3rd (last) one must not have this flag set. The flag is used to
 *	deal with retransmission/duplication recovery properly since A-MSDU
 *	subframes share the same sequence number. Reported subframes can be
 *	either regular MSDU or singly A-MSDUs. Subframes must not be
 *	interleaved with other frames.
 * @RX_FLAG_RADIOTAP_VENDOR_DATA: This frame contains vendor-specific
 *	radiotap data in the skb->data (before the frame) as described by
 *	the &struct ieee80211_vendor_radiotap.
 * @RX_FLAG_ALLOW_SAME_PN: Allow the same PN as same packet before.
 *	This is used for AMSDU subframes which can have the same PN as
 *	the first subframe.
 * @RX_FLAG_ICV_STRIPPED: The ICV is stripped from this frame. CRC checking must
 *	be done in the hardware.
 * @RX_FLAG_AMPDU_EOF_BIT: Value of the EOF bit in the A-MPDU delimiter for this
 *	frame
 * @RX_FLAG_AMPDU_EOF_BIT_KNOWN: The EOF value is known
 */
enum mac80211_rx_flags {
	RX_FLAG_MMIC_ERROR		= BIT(0),
	RX_FLAG_DECRYPTED		= BIT(1),
	RX_FLAG_MACTIME_PLCP_START	= BIT(2),
	RX_FLAG_MMIC_STRIPPED		= BIT(3),
	RX_FLAG_IV_STRIPPED		= BIT(4),
	RX_FLAG_FAILED_FCS_CRC		= BIT(5),
	RX_FLAG_FAILED_PLCP_CRC 	= BIT(6),
	RX_FLAG_MACTIME_START		= BIT(7),
	RX_FLAG_NO_SIGNAL_VAL		= BIT(8),
	RX_FLAG_AMPDU_DETAILS		= BIT(9),
	RX_FLAG_PN_VALIDATED		= BIT(10),
	RX_FLAG_DUP_VALIDATED		= BIT(11),
	RX_FLAG_AMPDU_LAST_KNOWN	= BIT(12),
	RX_FLAG_AMPDU_IS_LAST		= BIT(13),
	RX_FLAG_AMPDU_DELIM_CRC_ERROR	= BIT(14),
	RX_FLAG_AMPDU_DELIM_CRC_KNOWN	= BIT(15),
	RX_FLAG_MACTIME_END		= BIT(16),
	RX_FLAG_ONLY_MONITOR		= BIT(17),
	RX_FLAG_SKIP_MONITOR		= BIT(18),
	RX_FLAG_AMSDU_MORE		= BIT(19),
	RX_FLAG_RADIOTAP_VENDOR_DATA	= BIT(20),
	RX_FLAG_MIC_STRIPPED		= BIT(21),
	RX_FLAG_ALLOW_SAME_PN		= BIT(22),
	RX_FLAG_ICV_STRIPPED		= BIT(23),
	RX_FLAG_AMPDU_EOF_BIT		= BIT(24),
	RX_FLAG_AMPDU_EOF_BIT_KNOWN	= BIT(25),
};

/**
 * enum mac80211_rx_encoding_flags - MCS & bandwidth flags
 *
 * @RX_ENC_FLAG_SHORTPRE: Short preamble was used for this frame
 * @RX_ENC_FLAG_SHORT_GI: Short guard interval was used
 * @RX_ENC_FLAG_HT_GF: This frame was received in a HT-greenfield transmission,
 *	if the driver fills this value it should add
 *	%IEEE80211_RADIOTAP_MCS_HAVE_FMT
 *	to hw.radiotap_mcs_details to advertise that fact
 * @RX_ENC_FLAG_LDPC: LDPC was used
 * @RX_ENC_FLAG_STBC_MASK: STBC 2 bit bitmask. 1 - Nss=1, 2 - Nss=2, 3 - Nss=3
 * @RX_ENC_FLAG_BF: packet was beamformed
 */
enum mac80211_rx_encoding_flags {
	RX_ENC_FLAG_SHORTPRE		= BIT(0),
	RX_ENC_FLAG_SHORT_GI		= BIT(2),
	RX_ENC_FLAG_HT_GF		= BIT(3),
	RX_ENC_FLAG_STBC_MASK		= BIT(4) | BIT(5),
	RX_ENC_FLAG_LDPC		= BIT(6),
	RX_ENC_FLAG_BF			= BIT(7),
};

#define RX_ENC_FLAG_STBC_SHIFT		4

enum mac80211_rx_encoding {
	RX_ENC_LEGACY = 0,
	RX_ENC_HT,
	RX_ENC_VHT,
};

/**
 * struct ieee80211_rx_status - receive status
 *
 * The low-level driver should provide this information (the subset
 * supported by hardware) to the 802.11 code with each received
 * frame, in the skb's control buffer (cb).
 *
 * @mactime: value in microseconds of the 64-bit Time Synchronization Function
 * 	(TSF) timer when the first data symbol (MPDU) arrived at the hardware.
 * @boottime_ns: CLOCK_BOOTTIME timestamp the frame was received at, this is
 *	needed only for beacons and probe responses that update the scan cache.
 * @device_timestamp: arbitrary timestamp for the device, mac80211 doesn't use
 *	it but can store it and pass it back to the driver for synchronisation
 * @band: the active band when this frame was received
 * @freq: frequency the radio was tuned to when receiving this frame, in MHz
 *	This field must be set for management frames, but isn't strictly needed
 *	for data (other) frames - for those it only affects radiotap reporting.
 * @signal: signal strength when receiving this frame, either in dBm, in dB or
 *	unspecified depending on the hardware capabilities flags
 *	@IEEE80211_HW_SIGNAL_*
 * @chains: bitmask of receive chains for which separate signal strength
 *	values were filled.
 * @chain_signal: per-chain signal strength, in dBm (unlike @signal, doesn't
 *	support dB or unspecified units)
 * @antenna: antenna used
 * @rate_idx: index of data rate into band's supported rates or MCS index if
 *	HT or VHT is used (%RX_FLAG_HT/%RX_FLAG_VHT)
 * @nss: number of streams (VHT and HE only)
 * @flag: %RX_FLAG_\*
 * @encoding: &enum mac80211_rx_encoding
 * @bw: &enum rate_info_bw
 * @enc_flags: uses bits from &enum mac80211_rx_encoding_flags
 * @rx_flags: internal RX flags for mac80211
 * @ampdu_reference: A-MPDU reference number, must be a different value for
 *	each A-MPDU but the same for each subframe within one A-MPDU
 * @ampdu_delimiter_crc: A-MPDU delimiter CRC
 */
struct ieee80211_rx_status {
	u64 mactime;
	u64 boottime_ns;
	u32 device_timestamp;
	u32 ampdu_reference;
	u32 flag;
	u16 freq;
	u8 enc_flags;
	u8 encoding:2, bw:3;
	u8 rate_idx;
	u8 nss;
	u8 rx_flags;
	u8 band;
	u8 antenna;
	s8 signal;
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
	u8 ampdu_delimiter_crc;
};

/**
 * struct ieee80211_vendor_radiotap - vendor radiotap data information
 * @present: presence bitmap for this vendor namespace
 *	(this could be extended in the future if any vendor needs more
 *	 bits, the radiotap spec does allow for that)
 * @align: radiotap vendor namespace alignment. This defines the needed
 *	alignment for the @data field below, not for the vendor namespace
 *	description itself (which has a fixed 2-byte alignment)
 *	Must be a power of two, and be set to at least 1!
 * @oui: radiotap vendor namespace OUI
 * @subns: radiotap vendor sub namespace
 * @len: radiotap vendor sub namespace skip length, if alignment is done
 *	then that's added to this, i.e. this is only the length of the
 *	@data field.
 * @pad: number of bytes of padding after the @data, this exists so that
 *	the skb data alignment can be preserved even if the data has odd
 *	length
 * @data: the actual vendor namespace data
 *
 * This struct, including the vendor data, goes into the skb->data before
 * the 802.11 header. It's split up in mac80211 using the align/oui/subns
 * data.
 */
struct ieee80211_vendor_radiotap {
	u32 present;
	u8 align;
	u8 oui[3];
	u8 subns;
	u8 pad;
	u16 len;
	u8 data[];
} __packed;

/**
 * enum ieee80211_conf_flags - configuration flags
 *
 * Flags to define PHY configuration options
 *
 * @IEEE80211_CONF_MONITOR: there's a monitor interface present -- use this
 *	to determine for example whether to calculate timestamps for packets
 *	or not, do not use instead of filter flags!
 * @IEEE80211_CONF_PS: Enable 802.11 power save mode (managed mode only).
 *	This is the power save mode defined by IEEE 802.11-2007 section 11.2,
 *	meaning that the hardware still wakes up for beacons, is able to
 *	transmit frames and receive the possible acknowledgment frames.
 *	Not to be confused with hardware specific wakeup/sleep states,
 *	driver is responsible for that. See the section "Powersave support"
 *	for more.
 * @IEEE80211_CONF_IDLE: The device is running, but idle; if the flag is set
 *	the driver should be prepared to handle configuration requests but
 *	may turn the device off as much as possible. Typically, this flag will
 *	be set when an interface is set UP but not associated or scanning, but
 *	it can also be unset in that case when monitor interfaces are active.
 * @IEEE80211_CONF_OFFCHANNEL: The device is currently not on its main
 *	operating channel.
 */
enum ieee80211_conf_flags {
	IEEE80211_CONF_MONITOR		= (1<<0),
	IEEE80211_CONF_PS		= (1<<1),
	IEEE80211_CONF_IDLE		= (1<<2),
	IEEE80211_CONF_OFFCHANNEL	= (1<<3),
};


/**
 * enum ieee80211_conf_changed - denotes which configuration changed
 *
 * @IEEE80211_CONF_CHANGE_LISTEN_INTERVAL: the listen interval changed
 * @IEEE80211_CONF_CHANGE_MONITOR: the monitor flag changed
 * @IEEE80211_CONF_CHANGE_PS: the PS flag or dynamic PS timeout changed
 * @IEEE80211_CONF_CHANGE_POWER: the TX power changed
 * @IEEE80211_CONF_CHANGE_CHANNEL: the channel/channel_type changed
 * @IEEE80211_CONF_CHANGE_RETRY_LIMITS: retry limits changed
 * @IEEE80211_CONF_CHANGE_IDLE: Idle flag changed
 * @IEEE80211_CONF_CHANGE_SMPS: Spatial multiplexing powersave mode changed
 *	Note that this is only valid if channel contexts are not used,
 *	otherwise each channel context has the number of chains listed.
 */
enum ieee80211_conf_changed {
	IEEE80211_CONF_CHANGE_SMPS		= BIT(1),
	IEEE80211_CONF_CHANGE_LISTEN_INTERVAL	= BIT(2),
	IEEE80211_CONF_CHANGE_MONITOR		= BIT(3),
	IEEE80211_CONF_CHANGE_PS		= BIT(4),
	IEEE80211_CONF_CHANGE_POWER		= BIT(5),
	IEEE80211_CONF_CHANGE_CHANNEL		= BIT(6),
	IEEE80211_CONF_CHANGE_RETRY_LIMITS	= BIT(7),
	IEEE80211_CONF_CHANGE_IDLE		= BIT(8),
};

/**
 * enum ieee80211_smps_mode - spatial multiplexing power save mode
 *
 * @IEEE80211_SMPS_AUTOMATIC: automatic
 * @IEEE80211_SMPS_OFF: off
 * @IEEE80211_SMPS_STATIC: static
 * @IEEE80211_SMPS_DYNAMIC: dynamic
 * @IEEE80211_SMPS_NUM_MODES: internal, don't use
 */
enum ieee80211_smps_mode {
	IEEE80211_SMPS_AUTOMATIC,
	IEEE80211_SMPS_OFF,
	IEEE80211_SMPS_STATIC,
	IEEE80211_SMPS_DYNAMIC,

	/* keep last */
	IEEE80211_SMPS_NUM_MODES,
};

/**
 * struct ieee80211_conf - configuration of the device
 *
 * This struct indicates how the driver shall configure the hardware.
 *
 * @flags: configuration flags defined above
 *
 * @listen_interval: listen interval in units of beacon interval
 * @ps_dtim_period: The DTIM period of the AP we're connected to, for use
 *	in power saving. Power saving will not be enabled until a beacon
 *	has been received and the DTIM period is known.
 * @dynamic_ps_timeout: The dynamic powersave timeout (in ms), see the
 *	powersave documentation below. This variable is valid only when
 *	the CONF_PS flag is set.
 *
 * @power_level: requested transmit power (in dBm), backward compatibility
 *	value only that is set to the minimum of all interfaces
 *
 * @chandef: the channel definition to tune to
 * @radar_enabled: whether radar detection is enabled
 *
 * @long_frame_max_tx_count: Maximum number of transmissions for a "long" frame
 *	(a frame not RTS protected), called "dot11LongRetryLimit" in 802.11,
 *	but actually means the number of transmissions not the number of retries
 * @short_frame_max_tx_count: Maximum number of transmissions for a "short"
 *	frame, called "dot11ShortRetryLimit" in 802.11, but actually means the
 *	number of transmissions not the number of retries
 *
 * @smps_mode: spatial multiplexing powersave mode; note that
 *	%IEEE80211_SMPS_STATIC is used when the device is not
 *	configured for an HT channel.
 *	Note that this is only valid if channel contexts are not used,
 *	otherwise each channel context has the number of chains listed.
 */
struct ieee80211_conf {
	u32 flags;
	int power_level, dynamic_ps_timeout;

	u16 listen_interval;
	u8 ps_dtim_period;

	u8 long_frame_max_tx_count, short_frame_max_tx_count;

	struct cfg80211_chan_def chandef;
	bool radar_enabled;
	enum ieee80211_smps_mode smps_mode;
};

/**
 * struct ieee80211_channel_switch - holds the channel switch data
 *
 * The information provided in this structure is required for channel switch
 * operation.
 *
 * @timestamp: value in microseconds of the 64-bit Time Synchronization
 *	Function (TSF) timer when the frame containing the channel switch
 *	announcement was received. This is simply the rx.mactime parameter
 *	the driver passed into mac80211.
 * @device_timestamp: arbitrary timestamp for the device, this is the
 *	rx.device_timestamp parameter the driver passed to mac80211.
 * @block_tx: Indicates whether transmission must be blocked before the
 *	scheduled channel switch, as indicated by the AP.
 * @chandef: the new channel to switch to
 * @count: the number of TBTT's until the channel switch event
 */
struct ieee80211_channel_switch {
	u64 timestamp;
	u32 device_timestamp;
	bool block_tx;
	struct cfg80211_chan_def chandef;
	u8 count;
};

/**
 * enum ieee80211_vif_flags - virtual interface flags
 *
 * @IEEE80211_VIF_BEACON_FILTER: the device performs beacon filtering
 *	on this virtual interface to avoid unnecessary CPU wakeups
 * @IEEE80211_VIF_SUPPORTS_CQM_RSSI: the device can do connection quality
 *	monitoring on this virtual interface -- i.e. it can monitor
 *	connection quality related parameters, such as the RSSI level and
 *	provide notifications if configured trigger levels are reached.
 * @IEEE80211_VIF_SUPPORTS_UAPSD: The device can do U-APSD for this
 *	interface. This flag should be set during interface addition,
 *	but may be set/cleared as late as authentication to an AP. It is
 *	only valid for managed/station mode interfaces.
 * @IEEE80211_VIF_GET_NOA_UPDATE: request to handle NOA attributes
 *	and send P2P_PS notification to the driver if NOA changed, even
 *	this is not pure P2P vif.
 */
enum ieee80211_vif_flags {
	IEEE80211_VIF_BEACON_FILTER		= BIT(0),
	IEEE80211_VIF_SUPPORTS_CQM_RSSI		= BIT(1),
	IEEE80211_VIF_SUPPORTS_UAPSD		= BIT(2),
	IEEE80211_VIF_GET_NOA_UPDATE		= BIT(3),
};

/**
 * struct ieee80211_vif - per-interface data
 *
 * Data in this structure is continually present for driver
 * use during the life of a virtual interface.
 *
 * @type: type of this virtual interface
 * @bss_conf: BSS configuration for this interface, either our own
 *	or the BSS we're associated to
 * @addr: address of this interface
 * @p2p: indicates whether this AP or STA interface is a p2p
 *	interface, i.e. a GO or p2p-sta respectively
 * @csa_active: marks whether a channel switch is going on. Internally it is
 *	write-protected by sdata_lock and local->mtx so holding either is fine
 *	for read access.
 * @mu_mimo_owner: indicates interface owns MU-MIMO capability
 * @driver_flags: flags/capabilities the driver has for this interface,
 *	these need to be set (or cleared) when the interface is added
 *	or, if supported by the driver, the interface type is changed
 *	at runtime, mac80211 will never touch this field
 * @hw_queue: hardware queue for each AC
 * @cab_queue: content-after-beacon (DTIM beacon really) queue, AP mode only
 * @chanctx_conf: The channel context this interface is assigned to, or %NULL
 *	when it is not assigned. This pointer is RCU-protected due to the TX
 *	path needing to access it; even though the netdev carrier will always
 *	be off when it is %NULL there can still be races and packets could be
 *	processed after it switches back to %NULL.
 * @debugfs_dir: debugfs dentry, can be used by drivers to create own per
 *	interface debug files. Note that it will be NULL for the virtual
 *	monitor interface (if that is requested.)
 * @probe_req_reg: probe requests should be reported to mac80211 for this
 *	interface.
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void \*).
 * @txq: the multicast data TX queue (if driver uses the TXQ abstraction)
 */
struct ieee80211_vif {
	enum nl80211_iftype type;
	struct ieee80211_bss_conf bss_conf;
	u8 addr[ETH_ALEN] __aligned(2);
	bool p2p;
	bool csa_active;
	bool mu_mimo_owner;

	u8 cab_queue;
	u8 hw_queue[IEEE80211_NUM_ACS];

	struct ieee80211_txq *txq;

	struct ieee80211_chanctx_conf __rcu *chanctx_conf;

	u32 driver_flags;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *debugfs_dir;
#endif

	unsigned int probe_req_reg;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

static inline bool ieee80211_vif_is_mesh(struct ieee80211_vif *vif)
{
#ifdef CONFIG_MAC80211_MESH
	return vif->type == NL80211_IFTYPE_MESH_POINT;
#endif
	return false;
}

/**
 * wdev_to_ieee80211_vif - return a vif struct from a wdev
 * @wdev: the wdev to get the vif for
 *
 * This can be used by mac80211 drivers with direct cfg80211 APIs
 * (like the vendor commands) that get a wdev.
 *
 * Note that this function may return %NULL if the given wdev isn't
 * associated with a vif that the driver knows about (e.g. monitor
 * or AP_VLAN interfaces.)
 */
struct ieee80211_vif *wdev_to_ieee80211_vif(struct wireless_dev *wdev);

/**
 * ieee80211_vif_to_wdev - return a wdev struct from a vif
 * @vif: the vif to get the wdev for
 *
 * This can be used by mac80211 drivers with direct cfg80211 APIs
 * (like the vendor commands) that needs to get the wdev for a vif.
 *
 * Note that this function may return %NULL if the given wdev isn't
 * associated with a vif that the driver knows about (e.g. monitor
 * or AP_VLAN interfaces.)
 */
struct wireless_dev *ieee80211_vif_to_wdev(struct ieee80211_vif *vif);

/**
 * enum ieee80211_key_flags - key flags
 *
 * These flags are used for communication about keys between the driver
 * and mac80211, with the @flags parameter of &struct ieee80211_key_conf.
 *
 * @IEEE80211_KEY_FLAG_GENERATE_IV: This flag should be set by the
 *	driver to indicate that it requires IV generation for this
 *	particular key. Setting this flag does not necessarily mean that SKBs
 *	will have sufficient tailroom for ICV or MIC.
 * @IEEE80211_KEY_FLAG_GENERATE_MMIC: This flag should be set by
 *	the driver for a TKIP key if it requires Michael MIC
 *	generation in software.
 * @IEEE80211_KEY_FLAG_PAIRWISE: Set by mac80211, this flag indicates
 *	that the key is pairwise rather then a shared key.
 * @IEEE80211_KEY_FLAG_SW_MGMT_TX: This flag should be set by the driver for a
 *	CCMP/GCMP key if it requires CCMP/GCMP encryption of management frames
 *	(MFP) to be done in software.
 * @IEEE80211_KEY_FLAG_PUT_IV_SPACE: This flag should be set by the driver
 *	if space should be prepared for the IV, but the IV
 *	itself should not be generated. Do not set together with
 *	@IEEE80211_KEY_FLAG_GENERATE_IV on the same key. Setting this flag does
 *	not necessarily mean that SKBs will have sufficient tailroom for ICV or
 *	MIC.
 * @IEEE80211_KEY_FLAG_RX_MGMT: This key will be used to decrypt received
 *	management frames. The flag can help drivers that have a hardware
 *	crypto implementation that doesn't deal with management frames
 *	properly by allowing them to not upload the keys to hardware and
 *	fall back to software crypto. Note that this flag deals only with
 *	RX, if your crypto engine can't deal with TX you can also set the
 *	%IEEE80211_KEY_FLAG_SW_MGMT_TX flag to encrypt such frames in SW.
 * @IEEE80211_KEY_FLAG_GENERATE_IV_MGMT: This flag should be set by the
 *	driver for a CCMP/GCMP key to indicate that is requires IV generation
 *	only for managment frames (MFP).
 * @IEEE80211_KEY_FLAG_RESERVE_TAILROOM: This flag should be set by the
 *	driver for a key to indicate that sufficient tailroom must always
 *	be reserved for ICV or MIC, even when HW encryption is enabled.
 * @IEEE80211_KEY_FLAG_PUT_MIC_SPACE: This flag should be set by the driver for
 *	a TKIP key if it only requires MIC space. Do not set together with
 *	@IEEE80211_KEY_FLAG_GENERATE_MMIC on the same key.
 */
enum ieee80211_key_flags {
	IEEE80211_KEY_FLAG_GENERATE_IV_MGMT	= BIT(0),
	IEEE80211_KEY_FLAG_GENERATE_IV		= BIT(1),
	IEEE80211_KEY_FLAG_GENERATE_MMIC	= BIT(2),
	IEEE80211_KEY_FLAG_PAIRWISE		= BIT(3),
	IEEE80211_KEY_FLAG_SW_MGMT_TX		= BIT(4),
	IEEE80211_KEY_FLAG_PUT_IV_SPACE		= BIT(5),
	IEEE80211_KEY_FLAG_RX_MGMT		= BIT(6),
	IEEE80211_KEY_FLAG_RESERVE_TAILROOM	= BIT(7),
	IEEE80211_KEY_FLAG_PUT_MIC_SPACE	= BIT(8),
};

/**
 * struct ieee80211_key_conf - key information
 *
 * This key information is given by mac80211 to the driver by
 * the set_key() callback in &struct ieee80211_ops.
 *
 * @hw_key_idx: To be set by the driver, this is the key index the driver
 *	wants to be given when a frame is transmitted and needs to be
 *	encrypted in hardware.
 * @cipher: The key's cipher suite selector.
 * @tx_pn: PN used for TX keys, may be used by the driver as well if it
 *	needs to do software PN assignment by itself (e.g. due to TSO)
 * @flags: key flags, see &enum ieee80211_key_flags.
 * @keyidx: the key index (0-3)
 * @keylen: key material length
 * @key: key material. For ALG_TKIP the key is encoded as a 256-bit (32 byte)
 * 	data block:
 * 	- Temporal Encryption Key (128 bits)
 * 	- Temporal Authenticator Tx MIC Key (64 bits)
 * 	- Temporal Authenticator Rx MIC Key (64 bits)
 * @icv_len: The ICV length for this key type
 * @iv_len: The IV length for this key type
 */
struct ieee80211_key_conf {
	atomic64_t tx_pn;
	u32 cipher;
	u8 icv_len;
	u8 iv_len;
	u8 hw_key_idx;
	s8 keyidx;
	u16 flags;
	u8 keylen;
	u8 key[0];
};

#define IEEE80211_MAX_PN_LEN	16

#define TKIP_PN_TO_IV16(pn) ((u16)(pn & 0xffff))
#define TKIP_PN_TO_IV32(pn) ((u32)((pn >> 16) & 0xffffffff))

/**
 * struct ieee80211_key_seq - key sequence counter
 *
 * @tkip: TKIP data, containing IV32 and IV16 in host byte order
 * @ccmp: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
 * @aes_cmac: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
 * @aes_gmac: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
 * @gcmp: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
 * @hw: data for HW-only (e.g. cipher scheme) keys
 */
struct ieee80211_key_seq {
	union {
		struct {
			u32 iv32;
			u16 iv16;
		} tkip;
		struct {
			u8 pn[6];
		} ccmp;
		struct {
			u8 pn[6];
		} aes_cmac;
		struct {
			u8 pn[6];
		} aes_gmac;
		struct {
			u8 pn[6];
		} gcmp;
		struct {
			u8 seq[IEEE80211_MAX_PN_LEN];
			u8 seq_len;
		} hw;
	};
};

/**
 * struct ieee80211_cipher_scheme - cipher scheme
 *
 * This structure contains a cipher scheme information defining
 * the secure packet crypto handling.
 *
 * @cipher: a cipher suite selector
 * @iftype: a cipher iftype bit mask indicating an allowed cipher usage
 * @hdr_len: a length of a security header used the cipher
 * @pn_len: a length of a packet number in the security header
 * @pn_off: an offset of pn from the beginning of the security header
 * @key_idx_off: an offset of key index byte in the security header
 * @key_idx_mask: a bit mask of key_idx bits
 * @key_idx_shift: a bit shift needed to get key_idx
 *     key_idx value calculation:
 *      (sec_header_base[key_idx_off] & key_idx_mask) >> key_idx_shift
 * @mic_len: a mic length in bytes
 */
struct ieee80211_cipher_scheme {
	u32 cipher;
	u16 iftype;
	u8 hdr_len;
	u8 pn_len;
	u8 pn_off;
	u8 key_idx_off;
	u8 key_idx_mask;
	u8 key_idx_shift;
	u8 mic_len;
};

/**
 * enum set_key_cmd - key command
 *
 * Used with the set_key() callback in &struct ieee80211_ops, this
 * indicates whether a key is being removed or added.
 *
 * @SET_KEY: a key is set
 * @DISABLE_KEY: a key must be disabled
 */
enum set_key_cmd {
	SET_KEY, DISABLE_KEY,
};

/**
 * enum ieee80211_sta_state - station state
 *
 * @IEEE80211_STA_NOTEXIST: station doesn't exist at all,
 *	this is a special state for add/remove transitions
 * @IEEE80211_STA_NONE: station exists without special state
 * @IEEE80211_STA_AUTH: station is authenticated
 * @IEEE80211_STA_ASSOC: station is associated
 * @IEEE80211_STA_AUTHORIZED: station is authorized (802.1X)
 */
enum ieee80211_sta_state {
	/* NOTE: These need to be ordered correctly! */
	IEEE80211_STA_NOTEXIST,
	IEEE80211_STA_NONE,
	IEEE80211_STA_AUTH,
	IEEE80211_STA_ASSOC,
	IEEE80211_STA_AUTHORIZED,
};

/**
 * enum ieee80211_sta_rx_bandwidth - station RX bandwidth
 * @IEEE80211_STA_RX_BW_20: station can only receive 20 MHz
 * @IEEE80211_STA_RX_BW_40: station can receive up to 40 MHz
 * @IEEE80211_STA_RX_BW_80: station can receive up to 80 MHz
 * @IEEE80211_STA_RX_BW_160: station can receive up to 160 MHz
 *	(including 80+80 MHz)
 *
 * Implementation note: 20 must be zero to be initialized
 *	correctly, the values must be sorted.
 */
enum ieee80211_sta_rx_bandwidth {
	IEEE80211_STA_RX_BW_20 = 0,
	IEEE80211_STA_RX_BW_40,
	IEEE80211_STA_RX_BW_80,
	IEEE80211_STA_RX_BW_160,
};

/**
 * struct ieee80211_sta_rates - station rate selection table
 *
 * @rcu_head: RCU head used for freeing the table on update
 * @rate: transmit rates/flags to be used by default.
 *	Overriding entries per-packet is possible by using cb tx control.
 */
struct ieee80211_sta_rates {
	struct rcu_head rcu_head;
	struct {
		s8 idx;
		u8 count;
		u8 count_cts;
		u8 count_rts;
		u16 flags;
	} rate[IEEE80211_TX_RATE_TABLE_SIZE];
};

/**
 * struct ieee80211_sta - station table entry
 *
 * A station table entry represents a station we are possibly
 * communicating with. Since stations are RCU-managed in
 * mac80211, any ieee80211_sta pointer you get access to must
 * either be protected by rcu_read_lock() explicitly or implicitly,
 * or you must take good care to not use such a pointer after a
 * call to your sta_remove callback that removed it.
 *
 * @addr: MAC address
 * @aid: AID we assigned to the station if we're an AP
 * @supp_rates: Bitmap of supported rates (per band)
 * @ht_cap: HT capabilities of this STA; restricted to our own capabilities
 * @vht_cap: VHT capabilities of this STA; restricted to our own capabilities
 * @max_rx_aggregation_subframes: maximal amount of frames in a single AMPDU
 *	that this station is allowed to transmit to us.
 *	Can be modified by driver.
 * @wme: indicates whether the STA supports QoS/WME (if local devices does,
 *	otherwise always false)
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void \*), size is determined in hw information.
 * @uapsd_queues: bitmap of queues configured for uapsd. Only valid
 *	if wme is supported. The bits order is like in
 *	IEEE80211_WMM_IE_STA_QOSINFO_AC_*.
 * @max_sp: max Service Period. Only valid if wme is supported.
 * @bandwidth: current bandwidth the station can receive with
 * @rx_nss: in HT/VHT, the maximum number of spatial streams the
 *	station can receive at the moment, changed by operating mode
 *	notifications and capabilities. The value is only valid after
 *	the station moves to associated state.
 * @smps_mode: current SMPS mode (off, static or dynamic)
 * @rates: rate control selection table
 * @tdls: indicates whether the STA is a TDLS peer
 * @tdls_initiator: indicates the STA is an initiator of the TDLS link. Only
 *	valid if the STA is a TDLS peer in the first place.
 * @mfp: indicates whether the STA uses management frame protection or not.
 * @max_amsdu_subframes: indicates the maximal number of MSDUs in a single
 *	A-MSDU. Taken from the Extended Capabilities element. 0 means
 *	unlimited.
 * @support_p2p_ps: indicates whether the STA supports P2P PS mechanism or not.
 * @max_rc_amsdu_len: Maximum A-MSDU size in bytes recommended by rate control.
 * @txq: per-TID data TX queues (if driver uses the TXQ abstraction)
 */
struct ieee80211_sta {
	u32 supp_rates[NUM_NL80211_BANDS];
	u8 addr[ETH_ALEN];
	u16 aid;
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap;
	u8 max_rx_aggregation_subframes;
	bool wme;
	u8 uapsd_queues;
	u8 max_sp;
	u8 rx_nss;
	enum ieee80211_sta_rx_bandwidth bandwidth;
	enum ieee80211_smps_mode smps_mode;
	struct ieee80211_sta_rates __rcu *rates;
	bool tdls;
	bool tdls_initiator;
	bool mfp;
	u8 max_amsdu_subframes;

	/**
	 * @max_amsdu_len:
	 * indicates the maximal length of an A-MSDU in bytes.
	 * This field is always valid for packets with a VHT preamble.
	 * For packets with a HT preamble, additional limits apply:
	 *
	 * * If the skb is transmitted as part of a BA agreement, the
	 *   A-MSDU maximal size is min(max_amsdu_len, 4065) bytes.
	 * * If the skb is not part of a BA aggreement, the A-MSDU maximal
	 *   size is min(max_amsdu_len, 7935) bytes.
	 *
	 * Both additional HT limits must be enforced by the low level
	 * driver. This is defined by the spec (IEEE 802.11-2012 section
	 * 8.3.2.2 NOTE 2).
	 */
	u16 max_amsdu_len;
	bool support_p2p_ps;
	u16 max_rc_amsdu_len;

	struct ieee80211_txq *txq[IEEE80211_NUM_TIDS];

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

/**
 * enum sta_notify_cmd - sta notify command
 *
 * Used with the sta_notify() callback in &struct ieee80211_ops, this
 * indicates if an associated station made a power state transition.
 *
 * @STA_NOTIFY_SLEEP: a station is now sleeping
 * @STA_NOTIFY_AWAKE: a sleeping station woke up
 */
enum sta_notify_cmd {
	STA_NOTIFY_SLEEP, STA_NOTIFY_AWAKE,
};

/**
 * struct ieee80211_tx_control - TX control data
 *
 * @sta: station table entry, this sta pointer may be NULL and
 * 	it is not allowed to copy the pointer, due to RCU.
 */
struct ieee80211_tx_control {
	struct ieee80211_sta *sta;
};

/**
 * struct ieee80211_txq - Software intermediate tx queue
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @sta: station table entry, %NULL for per-vif queue
 * @tid: the TID for this queue (unused for per-vif queue)
 * @ac: the AC for this queue
 * @drv_priv: driver private area, sized by hw->txq_data_size
 *
 * The driver can obtain packets from this queue by calling
 * ieee80211_tx_dequeue().
 */
struct ieee80211_txq {
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	u8 tid;
	u8 ac;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

/**
 * enum ieee80211_hw_flags - hardware flags
 *
 * These flags are used to indicate hardware capabilities to
 * the stack. Generally, flags here should have their meaning
 * done in a way that the simplest hardware doesn't need setting
 * any particular flags. There are some exceptions to this rule,
 * however, so you are advised to review these flags carefully.
 *
 * @IEEE80211_HW_HAS_RATE_CONTROL:
 *	The hardware or firmware includes rate control, and cannot be
 *	controlled by the stack. As such, no rate control algorithm
 *	should be instantiated, and the TX rate reported to userspace
 *	will be taken from the TX status instead of the rate control
 *	algorithm.
 *	Note that this requires that the driver implement a number of
 *	callbacks so it has the correct information, it needs to have
 *	the @set_rts_threshold callback and must look at the BSS config
 *	@use_cts_prot for G/N protection, @use_short_slot for slot
 *	timing in 2.4 GHz and @use_short_preamble for preambles for
 *	CCK frames.
 *
 * @IEEE80211_HW_RX_INCLUDES_FCS:
 *	Indicates that received frames passed to the stack include
 *	the FCS at the end.
 *
 * @IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING:
 *	Some wireless LAN chipsets buffer broadcast/multicast frames
 *	for power saving stations in the hardware/firmware and others
 *	rely on the host system for such buffering. This option is used
 *	to configure the IEEE 802.11 upper layer to buffer broadcast and
 *	multicast frames when there are power saving stations so that
 *	the driver can fetch them with ieee80211_get_buffered_bc().
 *
 * @IEEE80211_HW_SIGNAL_UNSPEC:
 *	Hardware can provide signal values but we don't know its units. We
 *	expect values between 0 and @max_signal.
 *	If possible please provide dB or dBm instead.
 *
 * @IEEE80211_HW_SIGNAL_DBM:
 *	Hardware gives signal values in dBm, decibel difference from
 *	one milliwatt. This is the preferred method since it is standardized
 *	between different devices. @max_signal does not need to be set.
 *
 * @IEEE80211_HW_SPECTRUM_MGMT:
 * 	Hardware supports spectrum management defined in 802.11h
 * 	Measurement, Channel Switch, Quieting, TPC
 *
 * @IEEE80211_HW_AMPDU_AGGREGATION:
 *	Hardware supports 11n A-MPDU aggregation.
 *
 * @IEEE80211_HW_SUPPORTS_PS:
 *	Hardware has power save support (i.e. can go to sleep).
 *
 * @IEEE80211_HW_PS_NULLFUNC_STACK:
 *	Hardware requires nullfunc frame handling in stack, implies
 *	stack support for dynamic PS.
 *
 * @IEEE80211_HW_SUPPORTS_DYNAMIC_PS:
 *	Hardware has support for dynamic PS.
 *
 * @IEEE80211_HW_MFP_CAPABLE:
 *	Hardware supports management frame protection (MFP, IEEE 802.11w).
 *
 * @IEEE80211_HW_REPORTS_TX_ACK_STATUS:
 *	Hardware can provide ack status reports of Tx frames to
 *	the stack.
 *
 * @IEEE80211_HW_CONNECTION_MONITOR:
 *	The hardware performs its own connection monitoring, including
 *	periodic keep-alives to the AP and probing the AP on beacon loss.
 *
 * @IEEE80211_HW_NEED_DTIM_BEFORE_ASSOC:
 *	This device needs to get data from beacon before association (i.e.
 *	dtim_period).
 *
 * @IEEE80211_HW_SUPPORTS_PER_STA_GTK: The device's crypto engine supports
 *	per-station GTKs as used by IBSS RSN or during fast transition. If
 *	the device doesn't support per-station GTKs, but can be asked not
 *	to decrypt group addressed frames, then IBSS RSN support is still
 *	possible but software crypto will be used. Advertise the wiphy flag
 *	only in that case.
 *
 * @IEEE80211_HW_AP_LINK_PS: When operating in AP mode the device
 *	autonomously manages the PS status of connected stations. When
 *	this flag is set mac80211 will not trigger PS mode for connected
 *	stations based on the PM bit of incoming frames.
 *	Use ieee80211_start_ps()/ieee8021_end_ps() to manually configure
 *	the PS mode of connected stations.
 *
 * @IEEE80211_HW_TX_AMPDU_SETUP_IN_HW: The device handles TX A-MPDU session
 *	setup strictly in HW. mac80211 should not attempt to do this in
 *	software.
 *
 * @IEEE80211_HW_WANT_MONITOR_VIF: The driver would like to be informed of
 *	a virtual monitor interface when monitor interfaces are the only
 *	active interfaces.
 *
 * @IEEE80211_HW_NO_AUTO_VIF: The driver would like for no wlanX to
 *	be created.  It is expected user-space will create vifs as
 *	desired (and thus have them named as desired).
 *
 * @IEEE80211_HW_SW_CRYPTO_CONTROL: The driver wants to control which of the
 *	crypto algorithms can be done in software - so don't automatically
 *	try to fall back to it if hardware crypto fails, but do so only if
 *	the driver returns 1. This also forces the driver to advertise its
 *	supported cipher suites.
 *
 * @IEEE80211_HW_SUPPORT_FAST_XMIT: The driver/hardware supports fast-xmit,
 *	this currently requires only the ability to calculate the duration
 *	for frames.
 *
 * @IEEE80211_HW_QUEUE_CONTROL: The driver wants to control per-interface
 *	queue mapping in order to use different queues (not just one per AC)
 *	for different virtual interfaces. See the doc section on HW queue
 *	control for more details.
 *
 * @IEEE80211_HW_SUPPORTS_RC_TABLE: The driver supports using a rate
 *	selection table provided by the rate control algorithm.
 *
 * @IEEE80211_HW_P2P_DEV_ADDR_FOR_INTF: Use the P2P Device address for any
 *	P2P Interface. This will be honoured even if more than one interface
 *	is supported.
 *
 * @IEEE80211_HW_TIMING_BEACON_ONLY: Use sync timing from beacon frames
 *	only, to allow getting TBTT of a DTIM beacon.
 *
 * @IEEE80211_HW_SUPPORTS_HT_CCK_RATES: Hardware supports mixing HT/CCK rates
 *	and can cope with CCK rates in an aggregation session (e.g. by not
 *	using aggregation for such frames.)
 *
 * @IEEE80211_HW_CHANCTX_STA_CSA: Support 802.11h based channel-switch (CSA)
 *	for a single active channel while using channel contexts. When support
 *	is not enabled the default action is to disconnect when getting the
 *	CSA frame.
 *
 * @IEEE80211_HW_SUPPORTS_CLONED_SKBS: The driver will never modify the payload
 *	or tailroom of TX skbs without copying them first.
 *
 * @IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS: The HW supports scanning on all bands
 *	in one command, mac80211 doesn't have to run separate scans per band.
 *
 * @IEEE80211_HW_TDLS_WIDER_BW: The device/driver supports wider bandwidth
 *	than then BSS bandwidth for a TDLS link on the base channel.
 *
 * @IEEE80211_HW_SUPPORTS_AMSDU_IN_AMPDU: The driver supports receiving A-MSDUs
 *	within A-MPDU.
 *
 * @IEEE80211_HW_BEACON_TX_STATUS: The device/driver provides TX status
 *	for sent beacons.
 *
 * @IEEE80211_HW_NEEDS_UNIQUE_STA_ADDR: Hardware (or driver) requires that each
 *	station has a unique address, i.e. each station entry can be identified
 *	by just its MAC address; this prevents, for example, the same station
 *	from connecting to two virtual AP interfaces at the same time.
 *
 * @IEEE80211_HW_SUPPORTS_REORDERING_BUFFER: Hardware (or driver) manages the
 *	reordering buffer internally, guaranteeing mac80211 receives frames in
 *	order and does not need to manage its own reorder buffer or BA session
 *	timeout.
 *
 * @IEEE80211_HW_USES_RSS: The device uses RSS and thus requires parallel RX,
 *	which implies using per-CPU station statistics.
 *
 * @IEEE80211_HW_TX_AMSDU: Hardware (or driver) supports software aggregated
 *	A-MSDU frames. Requires software tx queueing and fast-xmit support.
 *	When not using minstrel/minstrel_ht rate control, the driver must
 *	limit the maximum A-MSDU size based on the current tx rate by setting
 *	max_rc_amsdu_len in struct ieee80211_sta.
 *
 * @IEEE80211_HW_TX_FRAG_LIST: Hardware (or driver) supports sending frag_list
 *	skbs, needed for zero-copy software A-MSDU.
 *
 * @IEEE80211_HW_REPORTS_LOW_ACK: The driver (or firmware) reports low ack event
 *	by ieee80211_report_low_ack() based on its own algorithm. For such
 *	drivers, mac80211 packet loss mechanism will not be triggered and driver
 *	is completely depending on firmware event for station kickout.
 *
 * @IEEE80211_HW_SUPPORTS_TX_FRAG: Hardware does fragmentation by itself.
 *	The stack will not do fragmentation.
 *	The callback for @set_frag_threshold should be set as well.
 *
 * @IEEE80211_HW_SUPPORTS_TDLS_BUFFER_STA: Hardware supports buffer STA on
 *	TDLS links.
 *
 * @IEEE80211_HW_DEAUTH_NEED_MGD_TX_PREP: The driver requires the
 *	mgd_prepare_tx() callback to be called before transmission of a
 *	deauthentication frame in case the association was completed but no
 *	beacon was heard. This is required in multi-channel scenarios, where the
 *	virtual interface might not be given air time for the transmission of
 *	the frame, as it is not synced with the AP/P2P GO yet, and thus the
 *	deauthentication frame might not be transmitted.
 >
 * @IEEE80211_HW_DOESNT_SUPPORT_QOS_NDP: The driver (or firmware) doesn't
 *	support QoS NDP for AP probing - that's most likely a driver bug.
 *
 * @NUM_IEEE80211_HW_FLAGS: number of hardware flags, used for sizing arrays
 */
enum ieee80211_hw_flags {
	IEEE80211_HW_HAS_RATE_CONTROL,
	IEEE80211_HW_RX_INCLUDES_FCS,
	IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING,
	IEEE80211_HW_SIGNAL_UNSPEC,
	IEEE80211_HW_SIGNAL_DBM,
	IEEE80211_HW_NEED_DTIM_BEFORE_ASSOC,
	IEEE80211_HW_SPECTRUM_MGMT,
	IEEE80211_HW_AMPDU_AGGREGATION,
	IEEE80211_HW_SUPPORTS_PS,
	IEEE80211_HW_PS_NULLFUNC_STACK,
	IEEE80211_HW_SUPPORTS_DYNAMIC_PS,
	IEEE80211_HW_MFP_CAPABLE,
	IEEE80211_HW_WANT_MONITOR_VIF,
	IEEE80211_HW_NO_AUTO_VIF,
	IEEE80211_HW_SW_CRYPTO_CONTROL,
	IEEE80211_HW_SUPPORT_FAST_XMIT,
	IEEE80211_HW_REPORTS_TX_ACK_STATUS,
	IEEE80211_HW_CONNECTION_MONITOR,
	IEEE80211_HW_QUEUE_CONTROL,
	IEEE80211_HW_SUPPORTS_PER_STA_GTK,
	IEEE80211_HW_AP_LINK_PS,
	IEEE80211_HW_TX_AMPDU_SETUP_IN_HW,
	IEEE80211_HW_SUPPORTS_RC_TABLE,
	IEEE80211_HW_P2P_DEV_ADDR_FOR_INTF,
	IEEE80211_HW_TIMING_BEACON_ONLY,
	IEEE80211_HW_SUPPORTS_HT_CCK_RATES,
	IEEE80211_HW_CHANCTX_STA_CSA,
	IEEE80211_HW_SUPPORTS_CLONED_SKBS,
	IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS,
	IEEE80211_HW_TDLS_WIDER_BW,
	IEEE80211_HW_SUPPORTS_AMSDU_IN_AMPDU,
	IEEE80211_HW_BEACON_TX_STATUS,
	IEEE80211_HW_NEEDS_UNIQUE_STA_ADDR,
	IEEE80211_HW_SUPPORTS_REORDERING_BUFFER,
	IEEE80211_HW_USES_RSS,
	IEEE80211_HW_TX_AMSDU,
	IEEE80211_HW_TX_FRAG_LIST,
	IEEE80211_HW_REPORTS_LOW_ACK,
	IEEE80211_HW_SUPPORTS_TX_FRAG,
	IEEE80211_HW_SUPPORTS_TDLS_BUFFER_STA,
	IEEE80211_HW_DEAUTH_NEED_MGD_TX_PREP,
	IEEE80211_HW_DOESNT_SUPPORT_QOS_NDP,

	/* keep last, obviously */
	NUM_IEEE80211_HW_FLAGS
};

/**
 * struct ieee80211_hw - hardware information and state
 *
 * This structure contains the configuration and hardware
 * information for an 802.11 PHY.
 *
 * @wiphy: This points to the &struct wiphy allocated for this
 *	802.11 PHY. You must fill in the @perm_addr and @dev
 *	members of this structure using SET_IEEE80211_DEV()
 *	and SET_IEEE80211_PERM_ADDR(). Additionally, all supported
 *	bands (with channels, bitrates) are registered here.
 *
 * @conf: &struct ieee80211_conf, device configuration, don't use.
 *
 * @priv: pointer to private area that was allocated for driver use
 *	along with this structure.
 *
 * @flags: hardware flags, see &enum ieee80211_hw_flags.
 *
 * @extra_tx_headroom: headroom to reserve in each transmit skb
 *	for use by the driver (e.g. for transmit headers.)
 *
 * @extra_beacon_tailroom: tailroom to reserve in each beacon tx skb.
 *	Can be used by drivers to add extra IEs.
 *
 * @max_signal: Maximum value for signal (rssi) in RX information, used
 *	only when @IEEE80211_HW_SIGNAL_UNSPEC or @IEEE80211_HW_SIGNAL_DB
 *
 * @max_listen_interval: max listen interval in units of beacon interval
 *	that HW supports
 *
 * @queues: number of available hardware transmit queues for
 *	data packets. WMM/QoS requires at least four, these
 *	queues need to have configurable access parameters.
 *
 * @rate_control_algorithm: rate control algorithm for this hardware.
 *	If unset (NULL), the default algorithm will be used. Must be
 *	set before calling ieee80211_register_hw().
 *
 * @vif_data_size: size (in bytes) of the drv_priv data area
 *	within &struct ieee80211_vif.
 * @sta_data_size: size (in bytes) of the drv_priv data area
 *	within &struct ieee80211_sta.
 * @chanctx_data_size: size (in bytes) of the drv_priv data area
 *	within &struct ieee80211_chanctx_conf.
 * @txq_data_size: size (in bytes) of the drv_priv data area
 *	within @struct ieee80211_txq.
 *
 * @max_rates: maximum number of alternate rate retry stages the hw
 *	can handle.
 * @max_report_rates: maximum number of alternate rate retry stages
 *	the hw can report back.
 * @max_rate_tries: maximum number of tries for each stage
 *
 * @max_rx_aggregation_subframes: maximum buffer size (number of
 *	sub-frames) to be used for A-MPDU block ack receiver
 *	aggregation.
 *	This is only relevant if the device has restrictions on the
 *	number of subframes, if it relies on mac80211 to do reordering
 *	it shouldn't be set.
 *
 * @max_tx_aggregation_subframes: maximum number of subframes in an
 *	aggregate an HT driver will transmit. Though ADDBA will advertise
 *	a constant value of 64 as some older APs can crash if the window
 *	size is smaller (an example is LinkSys WRT120N with FW v1.0.07
 *	build 002 Jun 18 2012).
 *
 * @max_tx_fragments: maximum number of tx buffers per (A)-MSDU, sum
 *	of 1 + skb_shinfo(skb)->nr_frags for each skb in the frag_list.
 *
 * @offchannel_tx_hw_queue: HW queue ID to use for offchannel TX
 *	(if %IEEE80211_HW_QUEUE_CONTROL is set)
 *
 * @radiotap_mcs_details: lists which MCS information can the HW
 *	reports, by default it is set to _MCS, _GI and _BW but doesn't
 *	include _FMT. Use %IEEE80211_RADIOTAP_MCS_HAVE_\* values, only
 *	adding _BW is supported today.
 *
 * @radiotap_vht_details: lists which VHT MCS information the HW reports,
 *	the default is _GI | _BANDWIDTH.
 *	Use the %IEEE80211_RADIOTAP_VHT_KNOWN_\* values.
 *
 * @radiotap_timestamp: Information for the radiotap timestamp field; if the
 *	'units_pos' member is set to a non-negative value it must be set to
 *	a combination of a IEEE80211_RADIOTAP_TIMESTAMP_UNIT_* and a
 *	IEEE80211_RADIOTAP_TIMESTAMP_SPOS_* value, and then the timestamp
 *	field will be added and populated from the &struct ieee80211_rx_status
 *	device_timestamp. If the 'accuracy' member is non-negative, it's put
 *	into the accuracy radiotap field and the accuracy known flag is set.
 *
 * @netdev_features: netdev features to be set in each netdev created
 *	from this HW. Note that not all features are usable with mac80211,
 *	other features will be rejected during HW registration.
 *
 * @uapsd_queues: This bitmap is included in (re)association frame to indicate
 *	for each access category if it is uAPSD trigger-enabled and delivery-
 *	enabled. Use IEEE80211_WMM_IE_STA_QOSINFO_AC_* to set this bitmap.
 *	Each bit corresponds to different AC. Value '1' in specific bit means
 *	that corresponding AC is both trigger- and delivery-enabled. '0' means
 *	neither enabled.
 *
 * @uapsd_max_sp_len: maximum number of total buffered frames the WMM AP may
 *	deliver to a WMM STA during any Service Period triggered by the WMM STA.
 *	Use IEEE80211_WMM_IE_STA_QOSINFO_SP_* for correct values.
 *
 * @n_cipher_schemes: a size of an array of cipher schemes definitions.
 * @cipher_schemes: a pointer to an array of cipher scheme definitions
 *	supported by HW.
 * @max_nan_de_entries: maximum number of NAN DE functions supported by the
 *	device.
 */
struct ieee80211_hw {
	struct ieee80211_conf conf;
	struct wiphy *wiphy;
	const char *rate_control_algorithm;
	void *priv;
	unsigned long flags[BITS_TO_LONGS(NUM_IEEE80211_HW_FLAGS)];
	unsigned int extra_tx_headroom;
	unsigned int extra_beacon_tailroom;
	int vif_data_size;
	int sta_data_size;
	int chanctx_data_size;
	int txq_data_size;
	u16 queues;
	u16 max_listen_interval;
	s8 max_signal;
	u8 max_rates;
	u8 max_report_rates;
	u8 max_rate_tries;
	u8 max_rx_aggregation_subframes;
	u8 max_tx_aggregation_subframes;
	u8 max_tx_fragments;
	u8 offchannel_tx_hw_queue;
	u8 radiotap_mcs_details;
	u16 radiotap_vht_details;
	struct {
		int units_pos;
		s16 accuracy;
	} radiotap_timestamp;
	netdev_features_t netdev_features;
	u8 uapsd_queues;
	u8 uapsd_max_sp_len;
	u8 n_cipher_schemes;
	const struct ieee80211_cipher_scheme *cipher_schemes;
	u8 max_nan_de_entries;
};

static inline bool _ieee80211_hw_check(struct ieee80211_hw *hw,
				       enum ieee80211_hw_flags flg)
{
	return test_bit(flg, hw->flags);
}
#define ieee80211_hw_check(hw, flg)	_ieee80211_hw_check(hw, IEEE80211_HW_##flg)

static inline void _ieee80211_hw_set(struct ieee80211_hw *hw,
				     enum ieee80211_hw_flags flg)
{
	return __set_bit(flg, hw->flags);
}
#define ieee80211_hw_set(hw, flg)	_ieee80211_hw_set(hw, IEEE80211_HW_##flg)

/**
 * struct ieee80211_scan_request - hw scan request
 *
 * @ies: pointers different parts of IEs (in req.ie)
 * @req: cfg80211 request.
 */
struct ieee80211_scan_request {
	struct ieee80211_scan_ies ies;

	/* Keep last */
	struct cfg80211_scan_request req;
};

/**
 * struct ieee80211_tdls_ch_sw_params - TDLS channel switch parameters
 *
 * @sta: peer this TDLS channel-switch request/response came from
 * @chandef: channel referenced in a TDLS channel-switch request
 * @action_code: see &enum ieee80211_tdls_actioncode
 * @status: channel-switch response status
 * @timestamp: time at which the frame was received
 * @switch_time: switch-timing parameter received in the frame
 * @switch_timeout: switch-timing parameter received in the frame
 * @tmpl_skb: TDLS switch-channel response template
 * @ch_sw_tm_ie: offset of the channel-switch timing IE inside @tmpl_skb
 */
struct ieee80211_tdls_ch_sw_params {
	struct ieee80211_sta *sta;
	struct cfg80211_chan_def *chandef;
	u8 action_code;
	u32 status;
	u32 timestamp;
	u16 switch_time;
	u16 switch_timeout;
	struct sk_buff *tmpl_skb;
	u32 ch_sw_tm_ie;
};

/**
 * wiphy_to_ieee80211_hw - return a mac80211 driver hw struct from a wiphy
 *
 * @wiphy: the &struct wiphy which we want to query
 *
 * mac80211 drivers can use this to get to their respective
 * &struct ieee80211_hw. Drivers wishing to get to their own private
 * structure can then access it via hw->priv. Note that mac802111 drivers should
 * not use wiphy_priv() to try to get their private driver structure as this
 * is already used internally by mac80211.
 *
 * Return: The mac80211 driver hw struct of @wiphy.
 */
struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy);

/**
 * SET_IEEE80211_DEV - set device for 802.11 hardware
 *
 * @hw: the &struct ieee80211_hw to set the device for
 * @dev: the &struct device of this 802.11 device
 */
static inline void SET_IEEE80211_DEV(struct ieee80211_hw *hw, struct device *dev)
{
	set_wiphy_dev(hw->wiphy, dev);
}

/**
 * SET_IEEE80211_PERM_ADDR - set the permanent MAC address for 802.11 hardware
 *
 * @hw: the &struct ieee80211_hw to set the MAC address for
 * @addr: the address to set
 */
static inline void SET_IEEE80211_PERM_ADDR(struct ieee80211_hw *hw, const u8 *addr)
{
	memcpy(hw->wiphy->perm_addr, addr, ETH_ALEN);
}

static inline struct ieee80211_rate *
ieee80211_get_tx_rate(const struct ieee80211_hw *hw,
		      const struct ieee80211_tx_info *c)
{
	if (WARN_ON_ONCE(c->control.rates[0].idx < 0))
		return NULL;
	return &hw->wiphy->bands[c->band]->bitrates[c->control.rates[0].idx];
}

static inline struct ieee80211_rate *
ieee80211_get_rts_cts_rate(const struct ieee80211_hw *hw,
			   const struct ieee80211_tx_info *c)
{
	if (c->control.rts_cts_rate_idx < 0)
		return NULL;
	return &hw->wiphy->bands[c->band]->bitrates[c->control.rts_cts_rate_idx];
}

static inline struct ieee80211_rate *
ieee80211_get_alt_retry_rate(const struct ieee80211_hw *hw,
			     const struct ieee80211_tx_info *c, int idx)
{
	if (c->control.rates[idx + 1].idx < 0)
		return NULL;
	return &hw->wiphy->bands[c->band]->bitrates[c->control.rates[idx + 1].idx];
}

/**
 * ieee80211_free_txskb - free TX skb
 * @hw: the hardware
 * @skb: the skb
 *
 * Free a transmit skb. Use this funtion when some failure
 * to transmit happened and thus status cannot be reported.
 */
void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb);

/**
 * DOC: Hardware crypto acceleration
 *
 * mac80211 is capable of taking advantage of many hardware
 * acceleration designs for encryption and decryption operations.
 *
 * The set_key() callback in the &struct ieee80211_ops for a given
 * device is called to enable hardware acceleration of encryption and
 * decryption. The callback takes a @sta parameter that will be NULL
 * for default keys or keys used for transmission only, or point to
 * the station information for the peer for individual keys.
 * Multiple transmission keys with the same key index may be used when
 * VLANs are configured for an access point.
 *
 * When transmitting, the TX control data will use the @hw_key_idx
 * selected by the driver by modifying the &struct ieee80211_key_conf
 * pointed to by the @key parameter to the set_key() function.
 *
 * The set_key() call for the %SET_KEY command should return 0 if
 * the key is now in use, -%EOPNOTSUPP or -%ENOSPC if it couldn't be
 * added; if you return 0 then hw_key_idx must be assigned to the
 * hardware key index, you are free to use the full u8 range.
 *
 * Note that in the case that the @IEEE80211_HW_SW_CRYPTO_CONTROL flag is
 * set, mac80211 will not automatically fall back to software crypto if
 * enabling hardware crypto failed. The set_key() call may also return the
 * value 1 to permit this specific key/algorithm to be done in software.
 *
 * When the cmd is %DISABLE_KEY then it must succeed.
 *
 * Note that it is permissible to not decrypt a frame even if a key
 * for it has been uploaded to hardware, the stack will not make any
 * decision based on whether a key has been uploaded or not but rather
 * based on the receive flags.
 *
 * The &struct ieee80211_key_conf structure pointed to by the @key
 * parameter is guaranteed to be valid until another call to set_key()
 * removes it, but it can only be used as a cookie to differentiate
 * keys.
 *
 * In TKIP some HW need to be provided a phase 1 key, for RX decryption
 * acceleration (i.e. iwlwifi). Those drivers should provide update_tkip_key
 * handler.
 * The update_tkip_key() call updates the driver with the new phase 1 key.
 * This happens every time the iv16 wraps around (every 65536 packets). The
 * set_key() call will happen only once for each key (unless the AP did
 * rekeying), it will not include a valid phase 1 key. The valid phase 1 key is
 * provided by update_tkip_key only. The trigger that makes mac80211 call this
 * handler is software decryption with wrap around of iv16.
 *
 * The set_default_unicast_key() call updates the default WEP key index
 * configured to the hardware for WEP encryption type. This is required
 * for devices that support offload of data packets (e.g. ARP responses).
 */

/**
 * DOC: Powersave support
 *
 * mac80211 has support for various powersave implementations.
 *
 * First, it can support hardware that handles all powersaving by itself,
 * such hardware should simply set the %IEEE80211_HW_SUPPORTS_PS hardware
 * flag. In that case, it will be told about the desired powersave mode
 * with the %IEEE80211_CONF_PS flag depending on the association status.
 * The hardware must take care of sending nullfunc frames when necessary,
 * i.e. when entering and leaving powersave mode. The hardware is required
 * to look at the AID in beacons and signal to the AP that it woke up when
 * it finds traffic directed to it.
 *
 * %IEEE80211_CONF_PS flag enabled means that the powersave mode defined in
 * IEEE 802.11-2007 section 11.2 is enabled. This is not to be confused
 * with hardware wakeup and sleep states. Driver is responsible for waking
 * up the hardware before issuing commands to the hardware and putting it
 * back to sleep at appropriate times.
 *
 * When PS is enabled, hardware needs to wakeup for beacons and receive the
 * buffered multicast/broadcast frames after the beacon. Also it must be
 * possible to send frames and receive the acknowledment frame.
 *
 * Other hardware designs cannot send nullfunc frames by themselves and also
 * need software support for parsing the TIM bitmap. This is also supported
 * by mac80211 by combining the %IEEE80211_HW_SUPPORTS_PS and
 * %IEEE80211_HW_PS_NULLFUNC_STACK flags. The hardware is of course still
 * required to pass up beacons. The hardware is still required to handle
 * waking up for multicast traffic; if it cannot the driver must handle that
 * as best as it can, mac80211 is too slow to do that.
 *
 * Dynamic powersave is an extension to normal powersave in which the
 * hardware stays awake for a user-specified period of time after sending a
 * frame so that reply frames need not be buffered and therefore delayed to
 * the next wakeup. It's compromise of getting good enough latency when
 * there's data traffic and still saving significantly power in idle
 * periods.
 *
 * Dynamic powersave is simply supported by mac80211 enabling and disabling
 * PS based on traffic. Driver needs to only set %IEEE80211_HW_SUPPORTS_PS
 * flag and mac80211 will handle everything automatically. Additionally,
 * hardware having support for the dynamic PS feature may set the
 * %IEEE80211_HW_SUPPORTS_DYNAMIC_PS flag to indicate that it can support
 * dynamic PS mode itself. The driver needs to look at the
 * @dynamic_ps_timeout hardware configuration value and use it that value
 * whenever %IEEE80211_CONF_PS is set. In this case mac80211 will disable
 * dynamic PS feature in stack and will just keep %IEEE80211_CONF_PS
 * enabled whenever user has enabled powersave.
 *
 * Driver informs U-APSD client support by enabling
 * %IEEE80211_VIF_SUPPORTS_UAPSD flag. The mode is configured through the
 * uapsd parameter in conf_tx() operation. Hardware needs to send the QoS
 * Nullfunc frames and stay awake until the service period has ended. To
 * utilize U-APSD, dynamic powersave is disabled for voip AC and all frames
 * from that AC are transmitted with powersave enabled.
 *
 * Note: U-APSD client mode is not yet supported with
 * %IEEE80211_HW_PS_NULLFUNC_STACK.
 */

/**
 * DOC: Beacon filter support
 *
 * Some hardware have beacon filter support to reduce host cpu wakeups
 * which will reduce system power consumption. It usually works so that
 * the firmware creates a checksum of the beacon but omits all constantly
 * changing elements (TSF, TIM etc). Whenever the checksum changes the
 * beacon is forwarded to the host, otherwise it will be just dropped. That
 * way the host will only receive beacons where some relevant information
 * (for example ERP protection or WMM settings) have changed.
 *
 * Beacon filter support is advertised with the %IEEE80211_VIF_BEACON_FILTER
 * interface capability. The driver needs to enable beacon filter support
 * whenever power save is enabled, that is %IEEE80211_CONF_PS is set. When
 * power save is enabled, the stack will not check for beacon loss and the
 * driver needs to notify about loss of beacons with ieee80211_beacon_loss().
 *
 * The time (or number of beacons missed) until the firmware notifies the
 * driver of a beacon loss event (which in turn causes the driver to call
 * ieee80211_beacon_loss()) should be configurable and will be controlled
 * by mac80211 and the roaming algorithm in the future.
 *
 * Since there may be constantly changing information elements that nothing
 * in the software stack cares about, we will, in the future, have mac80211
 * tell the driver which information elements are interesting in the sense
 * that we want to see changes in them. This will include
 *
 *  - a list of information element IDs
 *  - a list of OUIs for the vendor information element
 *
 * Ideally, the hardware would filter out any beacons without changes in the
 * requested elements, but if it cannot support that it may, at the expense
 * of some efficiency, filter out only a subset. For example, if the device
 * doesn't support checking for OUIs it should pass up all changes in all
 * vendor information elements.
 *
 * Note that change, for the sake of simplification, also includes information
 * elements appearing or disappearing from the beacon.
 *
 * Some hardware supports an "ignore list" instead, just make sure nothing
 * that was requested is on the ignore list, and include commonly changing
 * information element IDs in the ignore list, for example 11 (BSS load) and
 * the various vendor-assigned IEs with unknown contents (128, 129, 133-136,
 * 149, 150, 155, 156, 173, 176, 178, 179, 219); for forward compatibility
 * it could also include some currently unused IDs.
 *
 *
 * In addition to these capabilities, hardware should support notifying the
 * host of changes in the beacon RSSI. This is relevant to implement roaming
 * when no traffic is flowing (when traffic is flowing we see the RSSI of
 * the received data packets). This can consist in notifying the host when
 * the RSSI changes significantly or when it drops below or rises above
 * configurable thresholds. In the future these thresholds will also be
 * configured by mac80211 (which gets them from userspace) to implement
 * them as the roaming algorithm requires.
 *
 * If the hardware cannot implement this, the driver should ask it to
 * periodically pass beacon frames to the host so that software can do the
 * signal strength threshold checking.
 */

/**
 * DOC: Spatial multiplexing power save
 *
 * SMPS (Spatial multiplexing power save) is a mechanism to conserve
 * power in an 802.11n implementation. For details on the mechanism
 * and rationale, please refer to 802.11 (as amended by 802.11n-2009)
 * "11.2.3 SM power save".
 *
 * The mac80211 implementation is capable of sending action frames
 * to update the AP about the station's SMPS mode, and will instruct
 * the driver to enter the specific mode. It will also announce the
 * requested SMPS mode during the association handshake. Hardware
 * support for this feature is required, and can be indicated by
 * hardware flags.
 *
 * The default mode will be "automatic", which nl80211/cfg80211
 * defines to be dynamic SMPS in (regular) powersave, and SMPS
 * turned off otherwise.
 *
 * To support this feature, the driver must set the appropriate
 * hardware support flags, and handle the SMPS flag to the config()
 * operation. It will then with this mechanism be instructed to
 * enter the requested SMPS mode while associated to an HT AP.
 */

/**
 * DOC: Frame filtering
 *
 * mac80211 requires to see many management frames for proper
 * operation, and users may want to see many more frames when
 * in monitor mode. However, for best CPU usage and power consumption,
 * having as few frames as possible percolate through the stack is
 * desirable. Hence, the hardware should filter as much as possible.
 *
 * To achieve this, mac80211 uses filter flags (see below) to tell
 * the driver's configure_filter() function which frames should be
 * passed to mac80211 and which should be filtered out.
 *
 * Before configure_filter() is invoked, the prepare_multicast()
 * callback is invoked with the parameters @mc_count and @mc_list
 * for the combined multicast address list of all virtual interfaces.
 * It's use is optional, and it returns a u64 that is passed to
 * configure_filter(). Additionally, configure_filter() has the
 * arguments @changed_flags telling which flags were changed and
 * @total_flags with the new flag states.
 *
 * If your device has no multicast address filters your driver will
 * need to check both the %FIF_ALLMULTI flag and the @mc_count
 * parameter to see whether multicast frames should be accepted
 * or dropped.
 *
 * All unsupported flags in @total_flags must be cleared.
 * Hardware does not support a flag if it is incapable of _passing_
 * the frame to the stack. Otherwise the driver must ignore
 * the flag, but not clear it.
 * You must _only_ clear the flag (announce no support for the
 * flag to mac80211) if you are not able to pass the packet type
 * to the stack (so the hardware always filters it).
 * So for example, you should clear @FIF_CONTROL, if your hardware
 * always filters control frames. If your hardware always passes
 * control frames to the kernel and is incapable of filtering them,
 * you do _not_ clear the @FIF_CONTROL flag.
 * This rule applies to all other FIF flags as well.
 */

/**
 * DOC: AP support for powersaving clients
 *
 * In order to implement AP and P2P GO modes, mac80211 has support for
 * client powersaving, both "legacy" PS (PS-Poll/null data) and uAPSD.
 * There currently is no support for sAPSD.
 *
 * There is one assumption that mac80211 makes, namely that a client
 * will not poll with PS-Poll and trigger with uAPSD at the same time.
 * Both are supported, and both can be used by the same client, but
 * they can't be used concurrently by the same client. This simplifies
 * the driver code.
 *
 * The first thing to keep in mind is that there is a flag for complete
 * driver implementation: %IEEE80211_HW_AP_LINK_PS. If this flag is set,
 * mac80211 expects the driver to handle most of the state machine for
 * powersaving clients and will ignore the PM bit in incoming frames.
 * Drivers then use ieee80211_sta_ps_transition() to inform mac80211 of
 * stations' powersave transitions. In this mode, mac80211 also doesn't
 * handle PS-Poll/uAPSD.
 *
 * In the mode without %IEEE80211_HW_AP_LINK_PS, mac80211 will check the
 * PM bit in incoming frames for client powersave transitions. When a
 * station goes to sleep, we will stop transmitting to it. There is,
 * however, a race condition: a station might go to sleep while there is
 * data buffered on hardware queues. If the device has support for this
 * it will reject frames, and the driver should give the frames back to
 * mac80211 with the %IEEE80211_TX_STAT_TX_FILTERED flag set which will
 * cause mac80211 to retry the frame when the station wakes up. The
 * driver is also notified of powersave transitions by calling its
 * @sta_notify callback.
 *
 * When the station is asleep, it has three choices: it can wake up,
 * it can PS-Poll, or it can possibly start a uAPSD service period.
 * Waking up is implemented by simply transmitting all buffered (and
 * filtered) frames to the station. This is the easiest case. When
 * the station sends a PS-Poll or a uAPSD trigger frame, mac80211
 * will inform the driver of this with the @allow_buffered_frames
 * callback; this callback is optional. mac80211 will then transmit
 * the frames as usual and set the %IEEE80211_TX_CTL_NO_PS_BUFFER
 * on each frame. The last frame in the service period (or the only
 * response to a PS-Poll) also has %IEEE80211_TX_STATUS_EOSP set to
 * indicate that it ends the service period; as this frame must have
 * TX status report it also sets %IEEE80211_TX_CTL_REQ_TX_STATUS.
 * When TX status is reported for this frame, the service period is
 * marked has having ended and a new one can be started by the peer.
 *
 * Additionally, non-bufferable MMPDUs can also be transmitted by
 * mac80211 with the %IEEE80211_TX_CTL_NO_PS_BUFFER set in them.
 *
 * Another race condition can happen on some devices like iwlwifi
 * when there are frames queued for the station and it wakes up
 * or polls; the frames that are already queued could end up being
 * transmitted first instead, causing reordering and/or wrong
 * processing of the EOSP. The cause is that allowing frames to be
 * transmitted to a certain station is out-of-band communication to
 * the device. To allow this problem to be solved, the driver can
 * call ieee80211_sta_block_awake() if frames are buffered when it
 * is notified that the station went to sleep. When all these frames
 * have been filtered (see above), it must call the function again
 * to indicate that the station is no longer blocked.
 *
 * If the driver buffers frames in the driver for aggregation in any
 * way, it must use the ieee80211_sta_set_buffered() call when it is
 * notified of the station going to sleep to inform mac80211 of any
 * TIDs that have frames buffered. Note that when a station wakes up
 * this information is reset (hence the requirement to call it when
 * informed of the station going to sleep). Then, when a service
 * period starts for any reason, @release_buffered_frames is called
 * with the number of frames to be released and which TIDs they are
 * to come from. In this case, the driver is responsible for setting
 * the EOSP (for uAPSD) and MORE_DATA bits in the released frames,
 * to help the @more_data parameter is passed to tell the driver if
 * there is more data on other TIDs -- the TIDs to release frames
 * from are ignored since mac80211 doesn't know how many frames the
 * buffers for those TIDs contain.
 *
 * If the driver also implement GO mode, where absence periods may
 * shorten service periods (or abort PS-Poll responses), it must
 * filter those response frames except in the case of frames that
 * are buffered in the driver -- those must remain buffered to avoid
 * reordering. Because it is possible that no frames are released
 * in this case, the driver must call ieee80211_sta_eosp()
 * to indicate to mac80211 that the service period ended anyway.
 *
 * Finally, if frames from multiple TIDs are released from mac80211
 * but the driver might reorder them, it must clear & set the flags
 * appropriately (only the last frame may have %IEEE80211_TX_STATUS_EOSP)
 * and also take care of the EOSP and MORE_DATA bits in the frame.
 * The driver may also use ieee80211_sta_eosp() in this case.
 *
 * Note that if the driver ever buffers frames other than QoS-data
 * frames, it must take care to never send a non-QoS-data frame as
 * the last frame in a service period, adding a QoS-nulldata frame
 * after a non-QoS-data frame if needed.
 */

/**
 * DOC: HW queue control
 *
 * Before HW queue control was introduced, mac80211 only had a single static
 * assignment of per-interface AC software queues to hardware queues. This
 * was problematic for a few reasons:
 * 1) off-channel transmissions might get stuck behind other frames
 * 2) multiple virtual interfaces couldn't be handled correctly
 * 3) after-DTIM frames could get stuck behind other frames
 *
 * To solve this, hardware typically uses multiple different queues for all
 * the different usages, and this needs to be propagated into mac80211 so it
 * won't have the same problem with the software queues.
 *
 * Therefore, mac80211 now offers the %IEEE80211_HW_QUEUE_CONTROL capability
 * flag that tells it that the driver implements its own queue control. To do
 * so, the driver will set up the various queues in each &struct ieee80211_vif
 * and the offchannel queue in &struct ieee80211_hw. In response, mac80211 will
 * use those queue IDs in the hw_queue field of &struct ieee80211_tx_info and
 * if necessary will queue the frame on the right software queue that mirrors
 * the hardware queue.
 * Additionally, the driver has to then use these HW queue IDs for the queue
 * management functions (ieee80211_stop_queue() et al.)
 *
 * The driver is free to set up the queue mappings as needed, multiple virtual
 * interfaces may map to the same hardware queues if needed. The setup has to
 * happen during add_interface or change_interface callbacks. For example, a
 * driver supporting station+station and station+AP modes might decide to have
 * 10 hardware queues to handle different scenarios:
 *
 * 4 AC HW queues for 1st vif: 0, 1, 2, 3
 * 4 AC HW queues for 2nd vif: 4, 5, 6, 7
 * after-DTIM queue for AP:   8
 * off-channel queue:         9
 *
 * It would then set up the hardware like this:
 *   hw.offchannel_tx_hw_queue = 9
 *
 * and the first virtual interface that is added as follows:
 *   vif.hw_queue[IEEE80211_AC_VO] = 0
 *   vif.hw_queue[IEEE80211_AC_VI] = 1
 *   vif.hw_queue[IEEE80211_AC_BE] = 2
 *   vif.hw_queue[IEEE80211_AC_BK] = 3
 *   vif.cab_queue = 8 // if AP mode, otherwise %IEEE80211_INVAL_HW_QUEUE
 * and the second virtual interface with 4-7.
 *
 * If queue 6 gets full, for example, mac80211 would only stop the second
 * virtual interface's BE queue since virtual interface queues are per AC.
 *
 * Note that the vif.cab_queue value should be set to %IEEE80211_INVAL_HW_QUEUE
 * whenever the queue is not used (i.e. the interface is not in AP mode) if the
 * queue could potentially be shared since mac80211 will look at cab_queue when
 * a queue is stopped/woken even if the interface is not in AP mode.
 */

/**
 * enum ieee80211_filter_flags - hardware filter flags
 *
 * These flags determine what the filter in hardware should be
 * programmed to let through and what should not be passed to the
 * stack. It is always safe to pass more frames than requested,
 * but this has negative impact on power consumption.
 *
 * @FIF_ALLMULTI: pass all multicast frames, this is used if requested
 *	by the user or if the hardware is not capable of filtering by
 *	multicast address.
 *
 * @FIF_FCSFAIL: pass frames with failed FCS (but you need to set the
 *	%RX_FLAG_FAILED_FCS_CRC for them)
 *
 * @FIF_PLCPFAIL: pass frames with failed PLCP CRC (but you need to set
 *	the %RX_FLAG_FAILED_PLCP_CRC for them
 *
 * @FIF_BCN_PRBRESP_PROMISC: This flag is set during scanning to indicate
 *	to the hardware that it should not filter beacons or probe responses
 *	by BSSID. Filtering them can greatly reduce the amount of processing
 *	mac80211 needs to do and the amount of CPU wakeups, so you should
 *	honour this flag if possible.
 *
 * @FIF_CONTROL: pass control frames (except for PS Poll) addressed to this
 *	station
 *
 * @FIF_OTHER_BSS: pass frames destined to other BSSes
 *
 * @FIF_PSPOLL: pass PS Poll frames
 *
 * @FIF_PROBE_REQ: pass probe request frames
 */
enum ieee80211_filter_flags {
	FIF_ALLMULTI		= 1<<1,
	FIF_FCSFAIL		= 1<<2,
	FIF_PLCPFAIL		= 1<<3,
	FIF_BCN_PRBRESP_PROMISC	= 1<<4,
	FIF_CONTROL		= 1<<5,
	FIF_OTHER_BSS		= 1<<6,
	FIF_PSPOLL		= 1<<7,
	FIF_PROBE_REQ		= 1<<8,
};

/**
 * enum ieee80211_ampdu_mlme_action - A-MPDU actions
 *
 * These flags are used with the ampdu_action() callback in
 * &struct ieee80211_ops to indicate which action is needed.
 *
 * Note that drivers MUST be able to deal with a TX aggregation
 * session being stopped even before they OK'ed starting it by
 * calling ieee80211_start_tx_ba_cb_irqsafe, because the peer
 * might receive the addBA frame and send a delBA right away!
 *
 * @IEEE80211_AMPDU_RX_START: start RX aggregation
 * @IEEE80211_AMPDU_RX_STOP: stop RX aggregation
 * @IEEE80211_AMPDU_TX_START: start TX aggregation
 * @IEEE80211_AMPDU_TX_OPERATIONAL: TX aggregation has become operational
 * @IEEE80211_AMPDU_TX_STOP_CONT: stop TX aggregation but continue transmitting
 *	queued packets, now unaggregated. After all packets are transmitted the
 *	driver has to call ieee80211_stop_tx_ba_cb_irqsafe().
 * @IEEE80211_AMPDU_TX_STOP_FLUSH: stop TX aggregation and flush all packets,
 *	called when the station is removed. There's no need or reason to call
 *	ieee80211_stop_tx_ba_cb_irqsafe() in this case as mac80211 assumes the
 *	session is gone and removes the station.
 * @IEEE80211_AMPDU_TX_STOP_FLUSH_CONT: called when TX aggregation is stopped
 *	but the driver hasn't called ieee80211_stop_tx_ba_cb_irqsafe() yet and
 *	now the connection is dropped and the station will be removed. Drivers
 *	should clean up and drop remaining packets when this is called.
 */
enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP_CONT,
	IEEE80211_AMPDU_TX_STOP_FLUSH,
	IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
	IEEE80211_AMPDU_TX_OPERATIONAL,
};

/**
 * struct ieee80211_ampdu_params - AMPDU action parameters
 *
 * @action: the ampdu action, value from %ieee80211_ampdu_mlme_action.
 * @sta: peer of this AMPDU session
 * @tid: tid of the BA session
 * @ssn: start sequence number of the session. TX/RX_STOP can pass 0. When
 *	action is set to %IEEE80211_AMPDU_RX_START the driver passes back the
 *	actual ssn value used to start the session and writes the value here.
 * @buf_size: reorder buffer size  (number of subframes). Valid only when the
 *	action is set to %IEEE80211_AMPDU_RX_START or
 *	%IEEE80211_AMPDU_TX_OPERATIONAL
 * @amsdu: indicates the peer's ability to receive A-MSDU within A-MPDU.
 *	valid when the action is set to %IEEE80211_AMPDU_TX_OPERATIONAL
 * @timeout: BA session timeout. Valid only when the action is set to
 *	%IEEE80211_AMPDU_RX_START
 */
struct ieee80211_ampdu_params {
	enum ieee80211_ampdu_mlme_action action;
	struct ieee80211_sta *sta;
	u16 tid;
	u16 ssn;
	u8 buf_size;
	bool amsdu;
	u16 timeout;
};

/**
 * enum ieee80211_frame_release_type - frame release reason
 * @IEEE80211_FRAME_RELEASE_PSPOLL: frame released for PS-Poll
 * @IEEE80211_FRAME_RELEASE_UAPSD: frame(s) released due to
 *	frame received on trigger-enabled AC
 */
enum ieee80211_frame_release_type {
	IEEE80211_FRAME_RELEASE_PSPOLL,
	IEEE80211_FRAME_RELEASE_UAPSD,
};

/**
 * enum ieee80211_rate_control_changed - flags to indicate what changed
 *
 * @IEEE80211_RC_BW_CHANGED: The bandwidth that can be used to transmit
 *	to this station changed. The actual bandwidth is in the station
 *	information -- for HT20/40 the IEEE80211_HT_CAP_SUP_WIDTH_20_40
 *	flag changes, for HT and VHT the bandwidth field changes.
 * @IEEE80211_RC_SMPS_CHANGED: The SMPS state of the station changed.
 * @IEEE80211_RC_SUPP_RATES_CHANGED: The supported rate set of this peer
 *	changed (in IBSS mode) due to discovering more information about
 *	the peer.
 * @IEEE80211_RC_NSS_CHANGED: N_SS (number of spatial streams) was changed
 *	by the peer
 */
enum ieee80211_rate_control_changed {
	IEEE80211_RC_BW_CHANGED		= BIT(0),
	IEEE80211_RC_SMPS_CHANGED	= BIT(1),
	IEEE80211_RC_SUPP_RATES_CHANGED	= BIT(2),
	IEEE80211_RC_NSS_CHANGED	= BIT(3),
};

/**
 * enum ieee80211_roc_type - remain on channel type
 *
 * With the support for multi channel contexts and multi channel operations,
 * remain on channel operations might be limited/deferred/aborted by other
 * flows/operations which have higher priority (and vise versa).
 * Specifying the ROC type can be used by devices to prioritize the ROC
 * operations compared to other operations/flows.
 *
 * @IEEE80211_ROC_TYPE_NORMAL: There are no special requirements for this ROC.
 * @IEEE80211_ROC_TYPE_MGMT_TX: The remain on channel request is required
 *	for sending managment frames offchannel.
 */
enum ieee80211_roc_type {
	IEEE80211_ROC_TYPE_NORMAL = 0,
	IEEE80211_ROC_TYPE_MGMT_TX,
};

/**
 * enum ieee80211_reconfig_complete_type - reconfig type
 *
 * This enum is used by the reconfig_complete() callback to indicate what
 * reconfiguration type was completed.
 *
 * @IEEE80211_RECONFIG_TYPE_RESTART: hw restart type
 *	(also due to resume() callback returning 1)
 * @IEEE80211_RECONFIG_TYPE_SUSPEND: suspend type (regardless
 *	of wowlan configuration)
 */
enum ieee80211_reconfig_type {
	IEEE80211_RECONFIG_TYPE_RESTART,
	IEEE80211_RECONFIG_TYPE_SUSPEND,
};

/**
 * struct ieee80211_ops - callbacks from mac80211 to the driver
 *
 * This structure contains various callbacks that the driver may
 * handle or, in some cases, must handle, for example to configure
 * the hardware to a new channel or to transmit a frame.
 *
 * @tx: Handler that 802.11 module calls for each transmitted frame.
 *	skb contains the buffer starting from the IEEE 802.11 header.
 *	The low-level driver should send the frame out based on
 *	configuration in the TX control data. This handler should,
 *	preferably, never fail and stop queues appropriately.
 *	Must be atomic.
 *
 * @start: Called before the first netdevice attached to the hardware
 *	is enabled. This should turn on the hardware and must turn on
 *	frame reception (for possibly enabled monitor interfaces.)
 *	Returns negative error codes, these may be seen in userspace,
 *	or zero.
 *	When the device is started it should not have a MAC address
 *	to avoid acknowledging frames before a non-monitor device
 *	is added.
 *	Must be implemented and can sleep.
 *
 * @stop: Called after last netdevice attached to the hardware
 *	is disabled. This should turn off the hardware (at least
 *	it must turn off frame reception.)
 *	May be called right after add_interface if that rejects
 *	an interface. If you added any work onto the mac80211 workqueue
 *	you should ensure to cancel it on this callback.
 *	Must be implemented and can sleep.
 *
 * @suspend: Suspend the device; mac80211 itself will quiesce before and
 *	stop transmitting and doing any other configuration, and then
 *	ask the device to suspend. This is only invoked when WoWLAN is
 *	configured, otherwise the device is deconfigured completely and
 *	reconfigured at resume time.
 *	The driver may also impose special conditions under which it
 *	wants to use the "normal" suspend (deconfigure), say if it only
 *	supports WoWLAN when the device is associated. In this case, it
 *	must return 1 from this function.
 *
 * @resume: If WoWLAN was configured, this indicates that mac80211 is
 *	now resuming its operation, after this the device must be fully
 *	functional again. If this returns an error, the only way out is
 *	to also unregister the device. If it returns 1, then mac80211
 *	will also go through the regular complete restart on resume.
 *
 * @set_wakeup: Enable or disable wakeup when WoWLAN configuration is
 *	modified. The reason is that device_set_wakeup_enable() is
 *	supposed to be called when the configuration changes, not only
 *	in suspend().
 *
 * @add_interface: Called when a netdevice attached to the hardware is
 *	enabled. Because it is not called for monitor mode devices, @start
 *	and @stop must be implemented.
 *	The driver should perform any initialization it needs before
 *	the device can be enabled. The initial configuration for the
 *	interface is given in the conf parameter.
 *	The callback may refuse to add an interface by returning a
 *	negative error code (which will be seen in userspace.)
 *	Must be implemented and can sleep.
 *
 * @change_interface: Called when a netdevice changes type. This callback
 *	is optional, but only if it is supported can interface types be
 *	switched while the interface is UP. The callback may sleep.
 *	Note that while an interface is being switched, it will not be
 *	found by the interface iteration callbacks.
 *
 * @remove_interface: Notifies a driver that an interface is going down.
 *	The @stop callback is called after this if it is the last interface
 *	and no monitor interfaces are present.
 *	When all interfaces are removed, the MAC address in the hardware
 *	must be cleared so the device no longer acknowledges packets,
 *	the mac_addr member of the conf structure is, however, set to the
 *	MAC address of the device going away.
 *	Hence, this callback must be implemented. It can sleep.
 *
 * @config: Handler for configuration requests. IEEE 802.11 code calls this
 *	function to change hardware configuration, e.g., channel.
 *	This function should never fail but returns a negative error code
 *	if it does. The callback can sleep.
 *
 * @bss_info_changed: Handler for configuration requests related to BSS
 *	parameters that may vary during BSS's lifespan, and may affect low
 *	level driver (e.g. assoc/disassoc status, erp parameters).
 *	This function should not be used if no BSS has been set, unless
 *	for association indication. The @changed parameter indicates which
 *	of the bss parameters has changed when a call is made. The callback
 *	can sleep.
 *
 * @prepare_multicast: Prepare for multicast filter configuration.
 *	This callback is optional, and its return value is passed
 *	to configure_filter(). This callback must be atomic.
 *
 * @configure_filter: Configure the device's RX filter.
 *	See the section "Frame filtering" for more information.
 *	This callback must be implemented and can sleep.
 *
 * @config_iface_filter: Configure the interface's RX filter.
 *	This callback is optional and is used to configure which frames
 *	should be passed to mac80211. The filter_flags is the combination
 *	of FIF_* flags. The changed_flags is a bit mask that indicates
 *	which flags are changed.
 *	This callback can sleep.
 *
 * @set_tim: Set TIM bit. mac80211 calls this function when a TIM bit
 * 	must be set or cleared for a given STA. Must be atomic.
 *
 * @set_key: See the section "Hardware crypto acceleration"
 *	This callback is only called between add_interface and
 *	remove_interface calls, i.e. while the given virtual interface
 *	is enabled.
 *	Returns a negative error code if the key can't be added.
 *	The callback can sleep.
 *
 * @update_tkip_key: See the section "Hardware crypto acceleration"
 * 	This callback will be called in the context of Rx. Called for drivers
 * 	which set IEEE80211_KEY_FLAG_TKIP_REQ_RX_P1_KEY.
 *	The callback must be atomic.
 *
 * @set_rekey_data: If the device supports GTK rekeying, for example while the
 *	host is suspended, it can assign this callback to retrieve the data
 *	necessary to do GTK rekeying, this is the KEK, KCK and replay counter.
 *	After rekeying was done it should (for example during resume) notify
 *	userspace of the new replay counter using ieee80211_gtk_rekey_notify().
 *
 * @set_default_unicast_key: Set the default (unicast) key index, useful for
 *	WEP when the device sends data packets autonomously, e.g. for ARP
 *	offloading. The index can be 0-3, or -1 for unsetting it.
 *
 * @hw_scan: Ask the hardware to service the scan request, no need to start
 *	the scan state machine in stack. The scan must honour the channel
 *	configuration done by the regulatory agent in the wiphy's
 *	registered bands. The hardware (or the driver) needs to make sure
 *	that power save is disabled.
 *	The @req ie/ie_len members are rewritten by mac80211 to contain the
 *	entire IEs after the SSID, so that drivers need not look at these
 *	at all but just send them after the SSID -- mac80211 includes the
 *	(extended) supported rates and HT information (where applicable).
 *	When the scan finishes, ieee80211_scan_completed() must be called;
 *	note that it also must be called when the scan cannot finish due to
 *	any error unless this callback returned a negative error code.
 *	The callback can sleep.
 *
 * @cancel_hw_scan: Ask the low-level tp cancel the active hw scan.
 *	The driver should ask the hardware to cancel the scan (if possible),
 *	but the scan will be completed only after the driver will call
 *	ieee80211_scan_completed().
 *	This callback is needed for wowlan, to prevent enqueueing a new
 *	scan_work after the low-level driver was already suspended.
 *	The callback can sleep.
 *
 * @sched_scan_start: Ask the hardware to start scanning repeatedly at
 *	specific intervals.  The driver must call the
 *	ieee80211_sched_scan_results() function whenever it finds results.
 *	This process will continue until sched_scan_stop is called.
 *
 * @sched_scan_stop: Tell the hardware to stop an ongoing scheduled scan.
 *	In this case, ieee80211_sched_scan_stopped() must not be called.
 *
 * @sw_scan_start: Notifier function that is called just before a software scan
 *	is started. Can be NULL, if the driver doesn't need this notification.
 *	The mac_addr parameter allows supporting NL80211_SCAN_FLAG_RANDOM_ADDR,
 *	the driver may set the NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR flag if it
 *	can use this parameter. The callback can sleep.
 *
 * @sw_scan_complete: Notifier function that is called just after a
 *	software scan finished. Can be NULL, if the driver doesn't need
 *	this notification.
 *	The callback can sleep.
 *
 * @get_stats: Return low-level statistics.
 * 	Returns zero if statistics are available.
 *	The callback can sleep.
 *
 * @get_key_seq: If your device implements encryption in hardware and does
 *	IV/PN assignment then this callback should be provided to read the
 *	IV/PN for the given key from hardware.
 *	The callback must be atomic.
 *
 * @set_frag_threshold: Configuration of fragmentation threshold. Assign this
 *	if the device does fragmentation by itself. Note that to prevent the
 *	stack from doing fragmentation IEEE80211_HW_SUPPORTS_TX_FRAG
 *	should be set as well.
 *	The callback can sleep.
 *
 * @set_rts_threshold: Configuration of RTS threshold (if device needs it)
 *	The callback can sleep.
 *
 * @sta_add: Notifies low level driver about addition of an associated station,
 *	AP, IBSS/WDS/mesh peer etc. This callback can sleep.
 *
 * @sta_remove: Notifies low level driver about removal of an associated
 *	station, AP, IBSS/WDS/mesh peer etc. Note that after the callback
 *	returns it isn't safe to use the pointer, not even RCU protected;
 *	no RCU grace period is guaranteed between returning here and freeing
 *	the station. See @sta_pre_rcu_remove if needed.
 *	This callback can sleep.
 *
 * @sta_add_debugfs: Drivers can use this callback to add debugfs files
 *	when a station is added to mac80211's station list. This callback
 *	should be within a CONFIG_MAC80211_DEBUGFS conditional. This
 *	callback can sleep.
 *
 * @sta_notify: Notifies low level driver about power state transition of an
 *	associated station, AP,  IBSS/WDS/mesh peer etc. For a VIF operating
 *	in AP mode, this callback will not be called when the flag
 *	%IEEE80211_HW_AP_LINK_PS is set. Must be atomic.
 *
 * @sta_state: Notifies low level driver about state transition of a
 *	station (which can be the AP, a client, IBSS/WDS/mesh peer etc.)
 *	This callback is mutually exclusive with @sta_add/@sta_remove.
 *	It must not fail for down transitions but may fail for transitions
 *	up the list of states. Also note that after the callback returns it
 *	isn't safe to use the pointer, not even RCU protected - no RCU grace
 *	period is guaranteed between returning here and freeing the station.
 *	See @sta_pre_rcu_remove if needed.
 *	The callback can sleep.
 *
 * @sta_pre_rcu_remove: Notify driver about station removal before RCU
 *	synchronisation. This is useful if a driver needs to have station
 *	pointers protected using RCU, it can then use this call to clear
 *	the pointers instead of waiting for an RCU grace period to elapse
 *	in @sta_state.
 *	The callback can sleep.
 *
 * @sta_rc_update: Notifies the driver of changes to the bitrates that can be
 *	used to transmit to the station. The changes are advertised with bits
 *	from &enum ieee80211_rate_control_changed and the values are reflected
 *	in the station data. This callback should only be used when the driver
 *	uses hardware rate control (%IEEE80211_HW_HAS_RATE_CONTROL) since
 *	otherwise the rate control algorithm is notified directly.
 *	Must be atomic.
 * @sta_rate_tbl_update: Notifies the driver that the rate table changed. This
 *	is only used if the configured rate control algorithm actually uses
 *	the new rate table API, and is therefore optional. Must be atomic.
 *
 * @sta_statistics: Get statistics for this station. For example with beacon
 *	filtering, the statistics kept by mac80211 might not be accurate, so
 *	let the driver pre-fill the statistics. The driver can fill most of
 *	the values (indicating which by setting the filled bitmap), but not
 *	all of them make sense - see the source for which ones are possible.
 *	Statistics that the driver doesn't fill will be filled by mac80211.
 *	The callback can sleep.
 *
 * @conf_tx: Configure TX queue parameters (EDCF (aifs, cw_min, cw_max),
 *	bursting) for a hardware TX queue.
 *	Returns a negative error code on failure.
 *	The callback can sleep.
 *
 * @get_tsf: Get the current TSF timer value from firmware/hardware. Currently,
 *	this is only used for IBSS mode BSSID merging and debugging. Is not a
 *	required function.
 *	The callback can sleep.
 *
 * @set_tsf: Set the TSF timer to the specified value in the firmware/hardware.
 *	Currently, this is only used for IBSS mode debugging. Is not a
 *	required function.
 *	The callback can sleep.
 *
 * @offset_tsf: Offset the TSF timer by the specified value in the
 *	firmware/hardware.  Preferred to set_tsf as it avoids delay between
 *	calling set_tsf() and hardware getting programmed, which will show up
 *	as TSF delay. Is not a required function.
 *	The callback can sleep.
 *
 * @reset_tsf: Reset the TSF timer and allow firmware/hardware to synchronize
 *	with other STAs in the IBSS. This is only used in IBSS mode. This
 *	function is optional if the firmware/hardware takes full care of
 *	TSF synchronization.
 *	The callback can sleep.
 *
 * @tx_last_beacon: Determine whether the last IBSS beacon was sent by us.
 *	This is needed only for IBSS mode and the result of this function is
 *	used to determine whether to reply to Probe Requests.
 *	Returns non-zero if this device sent the last beacon.
 *	The callback can sleep.
 *
 * @get_survey: Return per-channel survey information
 *
 * @rfkill_poll: Poll rfkill hardware state. If you need this, you also
 *	need to set wiphy->rfkill_poll to %true before registration,
 *	and need to call wiphy_rfkill_set_hw_state() in the callback.
 *	The callback can sleep.
 *
 * @set_coverage_class: Set slot time for given coverage class as specified
 *	in IEEE 802.11-2007 section 17.3.8.6 and modify ACK timeout
 *	accordingly; coverage class equals to -1 to enable ACK timeout
 *	estimation algorithm (dynack). To disable dynack set valid value for
 *	coverage class. This callback is not required and may sleep.
 *
 * @testmode_cmd: Implement a cfg80211 test mode command. The passed @vif may
 *	be %NULL. The callback can sleep.
 * @testmode_dump: Implement a cfg80211 test mode dump. The callback can sleep.
 *
 * @flush: Flush all pending frames from the hardware queue, making sure
 *	that the hardware queues are empty. The @queues parameter is a bitmap
 *	of queues to flush, which is useful if different virtual interfaces
 *	use different hardware queues; it may also indicate all queues.
 *	If the parameter @drop is set to %true, pending frames may be dropped.
 *	Note that vif can be NULL.
 *	The callback can sleep.
 *
 * @channel_switch: Drivers that need (or want) to offload the channel
 *	switch operation for CSAs received from the AP may implement this
 *	callback. They must then call ieee80211_chswitch_done() to indicate
 *	completion of the channel switch.
 *
 * @set_antenna: Set antenna configuration (tx_ant, rx_ant) on the device.
 *	Parameters are bitmaps of allowed antennas to use for TX/RX. Drivers may
 *	reject TX/RX mask combinations they cannot support by returning -EINVAL
 *	(also see nl80211.h @NL80211_ATTR_WIPHY_ANTENNA_TX).
 *
 * @get_antenna: Get current antenna configuration from device (tx_ant, rx_ant).
 *
 * @remain_on_channel: Starts an off-channel period on the given channel, must
 *	call back to ieee80211_ready_on_channel() when on that channel. Note
 *	that normal channel traffic is not stopped as this is intended for hw
 *	offload. Frames to transmit on the off-channel channel are transmitted
 *	normally except for the %IEEE80211_TX_CTL_TX_OFFCHAN flag. When the
 *	duration (which will always be non-zero) expires, the driver must call
 *	ieee80211_remain_on_channel_expired().
 *	Note that this callback may be called while the device is in IDLE and
 *	must be accepted in this case.
 *	This callback may sleep.
 * @cancel_remain_on_channel: Requests that an ongoing off-channel period is
 *	aborted before it expires. This callback may sleep.
 *
 * @set_ringparam: Set tx and rx ring sizes.
 *
 * @get_ringparam: Get tx and rx ring current and maximum sizes.
 *
 * @tx_frames_pending: Check if there is any pending frame in the hardware
 *	queues before entering power save.
 *
 * @set_bitrate_mask: Set a mask of rates to be used for rate control selection
 *	when transmitting a frame. Currently only legacy rates are handled.
 *	The callback can sleep.
 * @event_callback: Notify driver about any event in mac80211. See
 *	&enum ieee80211_event_type for the different types.
 *	The callback must be atomic.
 *
 * @release_buffered_frames: Release buffered frames according to the given
 *	parameters. In the case where the driver buffers some frames for
 *	sleeping stations mac80211 will use this callback to tell the driver
 *	to release some frames, either for PS-poll or uAPSD.
 *	Note that if the @more_data parameter is %false the driver must check
 *	if there are more frames on the given TIDs, and if there are more than
 *	the frames being released then it must still set the more-data bit in
 *	the frame. If the @more_data parameter is %true, then of course the
 *	more-data bit must always be set.
 *	The @tids parameter tells the driver which TIDs to release frames
 *	from, for PS-poll it will always have only a single bit set.
 *	In the case this is used for a PS-poll initiated release, the
 *	@num_frames parameter will always be 1 so code can be shared. In
 *	this case the driver must also set %IEEE80211_TX_STATUS_EOSP flag
 *	on the TX status (and must report TX status) so that the PS-poll
 *	period is properly ended. This is used to avoid sending multiple
 *	responses for a retried PS-poll frame.
 *	In the case this is used for uAPSD, the @num_frames parameter may be
 *	bigger than one, but the driver may send fewer frames (it must send
 *	at least one, however). In this case it is also responsible for
 *	setting the EOSP flag in the QoS header of the frames. Also, when the
 *	service period ends, the driver must set %IEEE80211_TX_STATUS_EOSP
 *	on the last frame in the SP. Alternatively, it may call the function
 *	ieee80211_sta_eosp() to inform mac80211 of the end of the SP.
 *	This callback must be atomic.
 * @allow_buffered_frames: Prepare device to allow the given number of frames
 *	to go out to the given station. The frames will be sent by mac80211
 *	via the usual TX path after this call. The TX information for frames
 *	released will also have the %IEEE80211_TX_CTL_NO_PS_BUFFER flag set
 *	and the last one will also have %IEEE80211_TX_STATUS_EOSP set. In case
 *	frames from multiple TIDs are released and the driver might reorder
 *	them between the TIDs, it must set the %IEEE80211_TX_STATUS_EOSP flag
 *	on the last frame and clear it on all others and also handle the EOSP
 *	bit in the QoS header correctly. Alternatively, it can also call the
 *	ieee80211_sta_eosp() function.
 *	The @tids parameter is a bitmap and tells the driver which TIDs the
 *	frames will be on; it will at most have two bits set.
 *	This callback must be atomic.
 *
 * @get_et_sset_count:  Ethtool API to get string-set count.
 *
 * @get_et_stats:  Ethtool API to get a set of u64 stats.
 *
 * @get_et_strings:  Ethtool API to get a set of strings to describe stats
 *	and perhaps other supported types of ethtool data-sets.
 *
 * @mgd_prepare_tx: Prepare for transmitting a management frame for association
 *	before associated. In multi-channel scenarios, a virtual interface is
 *	bound to a channel before it is associated, but as it isn't associated
 *	yet it need not necessarily be given airtime, in particular since any
 *	transmission to a P2P GO needs to be synchronized against the GO's
 *	powersave state. mac80211 will call this function before transmitting a
 *	management frame prior to having successfully associated to allow the
 *	driver to give it channel time for the transmission, to get a response
 *	and to be able to synchronize with the GO.
 *	For drivers that set %IEEE80211_HW_DEAUTH_NEED_MGD_TX_PREP, mac80211
 *	would also call this function before transmitting a deauthentication
 *	frame in case that no beacon was heard from the AP/P2P GO.
 *	The callback will be called before each transmission and upon return
 *	mac80211 will transmit the frame right away.
 *	The callback is optional and can (should!) sleep.
 *
 * @mgd_protect_tdls_discover: Protect a TDLS discovery session. After sending
 *	a TDLS discovery-request, we expect a reply to arrive on the AP's
 *	channel. We must stay on the channel (no PSM, scan, etc.), since a TDLS
 *	setup-response is a direct packet not buffered by the AP.
 *	mac80211 will call this function just before the transmission of a TDLS
 *	discovery-request. The recommended period of protection is at least
 *	2 * (DTIM period).
 *	The callback is optional and can sleep.
 *
 * @add_chanctx: Notifies device driver about new channel context creation.
 *	This callback may sleep.
 * @remove_chanctx: Notifies device driver about channel context destruction.
 *	This callback may sleep.
 * @change_chanctx: Notifies device driver about channel context changes that
 *	may happen when combining different virtual interfaces on the same
 *	channel context with different settings
 *	This callback may sleep.
 * @assign_vif_chanctx: Notifies device driver about channel context being bound
 *	to vif. Possible use is for hw queue remapping.
 *	This callback may sleep.
 * @unassign_vif_chanctx: Notifies device driver about channel context being
 *	unbound from vif.
 *	This callback may sleep.
 * @switch_vif_chanctx: switch a number of vifs from one chanctx to
 *	another, as specified in the list of
 *	@ieee80211_vif_chanctx_switch passed to the driver, according
 *	to the mode defined in &ieee80211_chanctx_switch_mode.
 *	This callback may sleep.
 *
 * @start_ap: Start operation on the AP interface, this is called after all the
 *	information in bss_conf is set and beacon can be retrieved. A channel
 *	context is bound before this is called. Note that if the driver uses
 *	software scan or ROC, this (and @stop_ap) isn't called when the AP is
 *	just "paused" for scanning/ROC, which is indicated by the beacon being
 *	disabled/enabled via @bss_info_changed.
 * @stop_ap: Stop operation on the AP interface.
 *
 * @reconfig_complete: Called after a call to ieee80211_restart_hw() and
 *	during resume, when the reconfiguration has completed.
 *	This can help the driver implement the reconfiguration step (and
 *	indicate mac80211 is ready to receive frames).
 *	This callback may sleep.
 *
 * @ipv6_addr_change: IPv6 address assignment on the given interface changed.
 *	Currently, this is only called for managed or P2P client interfaces.
 *	This callback is optional; it must not sleep.
 *
 * @channel_switch_beacon: Starts a channel switch to a new channel.
 *	Beacons are modified to include CSA or ECSA IEs before calling this
 *	function. The corresponding count fields in these IEs must be
 *	decremented, and when they reach 1 the driver must call
 *	ieee80211_csa_finish(). Drivers which use ieee80211_beacon_get()
 *	get the csa counter decremented by mac80211, but must check if it is
 *	1 using ieee80211_csa_is_complete() after the beacon has been
 *	transmitted and then call ieee80211_csa_finish().
 *	If the CSA count starts as zero or 1, this function will not be called,
 *	since there won't be any time to beacon before the switch anyway.
 * @pre_channel_switch: This is an optional callback that is called
 *	before a channel switch procedure is started (ie. when a STA
 *	gets a CSA or a userspace initiated channel-switch), allowing
 *	the driver to prepare for the channel switch.
 * @post_channel_switch: This is an optional callback that is called
 *	after a channel switch procedure is completed, allowing the
 *	driver to go back to a normal configuration.
 *
 * @join_ibss: Join an IBSS (on an IBSS interface); this is called after all
 *	information in bss_conf is set up and the beacon can be retrieved. A
 *	channel context is bound before this is called.
 * @leave_ibss: Leave the IBSS again.
 *
 * @get_expected_throughput: extract the expected throughput towards the
 *	specified station. The returned value is expressed in Kbps. It returns 0
 *	if the RC algorithm does not have proper data to provide.
 *
 * @get_txpower: get current maximum tx power (in dBm) based on configuration
 *	and hardware limits.
 *
 * @tdls_channel_switch: Start channel-switching with a TDLS peer. The driver
 *	is responsible for continually initiating channel-switching operations
 *	and returning to the base channel for communication with the AP. The
 *	driver receives a channel-switch request template and the location of
 *	the switch-timing IE within the template as part of the invocation.
 *	The template is valid only within the call, and the driver can
 *	optionally copy the skb for further re-use.
 * @tdls_cancel_channel_switch: Stop channel-switching with a TDLS peer. Both
 *	peers must be on the base channel when the call completes.
 * @tdls_recv_channel_switch: a TDLS channel-switch related frame (request or
 *	response) has been received from a remote peer. The driver gets
 *	parameters parsed from the incoming frame and may use them to continue
 *	an ongoing channel-switch operation. In addition, a channel-switch
 *	response template is provided, together with the location of the
 *	switch-timing IE within the template. The skb can only be used within
 *	the function call.
 *
 * @wake_tx_queue: Called when new packets have been added to the queue.
 * @sync_rx_queues: Process all pending frames in RSS queues. This is a
 *	synchronization which is needed in case driver has in its RSS queues
 *	pending frames that were received prior to the control path action
 *	currently taken (e.g. disassociation) but are not processed yet.
 *
 * @start_nan: join an existing NAN cluster, or create a new one.
 * @stop_nan: leave the NAN cluster.
 * @nan_change_conf: change NAN configuration. The data in cfg80211_nan_conf
 *	contains full new configuration and changes specify which parameters
 *	are changed with respect to the last NAN config.
 *	The driver gets both full configuration and the changed parameters since
 *	some devices may need the full configuration while others need only the
 *	changed parameters.
 * @add_nan_func: Add a NAN function. Returns 0 on success. The data in
 *	cfg80211_nan_func must not be referenced outside the scope of
 *	this call.
 * @del_nan_func: Remove a NAN function. The driver must call
 *	ieee80211_nan_func_terminated() with
 *	NL80211_NAN_FUNC_TERM_REASON_USER_REQUEST reason code upon removal.
 */
struct ieee80211_ops {
	void (*tx)(struct ieee80211_hw *hw,
		   struct ieee80211_tx_control *control,
		   struct sk_buff *skb);
	int (*start)(struct ieee80211_hw *hw);
	void (*stop)(struct ieee80211_hw *hw);
#ifdef CONFIG_PM
	int (*suspend)(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan);
	int (*resume)(struct ieee80211_hw *hw);
	void (*set_wakeup)(struct ieee80211_hw *hw, bool enabled);
#endif
	int (*add_interface)(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
	int (*change_interface)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				enum nl80211_iftype new_type, bool p2p);
	void (*remove_interface)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif);
	int (*config)(struct ieee80211_hw *hw, u32 changed);
	void (*bss_info_changed)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info,
				 u32 changed);

	int (*start_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
	void (*stop_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);

	u64 (*prepare_multicast)(struct ieee80211_hw *hw,
				 struct netdev_hw_addr_list *mc_list);
	void (*configure_filter)(struct ieee80211_hw *hw,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 u64 multicast);
	void (*config_iface_filter)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    unsigned int filter_flags,
				    unsigned int changed_flags);
	int (*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       bool set);
	int (*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key);
	void (*update_tkip_key)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_key_conf *conf,
				struct ieee80211_sta *sta,
				u32 iv32, u16 *phase1key);
	void (*set_rekey_data)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct cfg80211_gtk_rekey_data *data);
	void (*set_default_unicast_key)(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif, int idx);
	int (*hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *req);
	void (*cancel_hw_scan)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif);
	int (*sched_scan_start)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct cfg80211_sched_scan_request *req,
				struct ieee80211_scan_ies *ies);
	int (*sched_scan_stop)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif);
	void (*sw_scan_start)(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      const u8 *mac_addr);
	void (*sw_scan_complete)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif);
	int (*get_stats)(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats);
	void (*get_key_seq)(struct ieee80211_hw *hw,
			    struct ieee80211_key_conf *key,
			    struct ieee80211_key_seq *seq);
	int (*set_frag_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*set_rts_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
	int (*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
#ifdef CONFIG_MAC80211_DEBUGFS
	void (*sta_add_debugfs)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct dentry *dir);
#endif
	void (*sta_notify)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			enum sta_notify_cmd, struct ieee80211_sta *sta);
	int (*sta_state)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 enum ieee80211_sta_state old_state,
			 enum ieee80211_sta_state new_state);
	void (*sta_pre_rcu_remove)(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta);
	void (*sta_rc_update)(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      u32 changed);
	void (*sta_rate_tbl_update)(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta);
	void (*sta_statistics)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct station_info *sinfo);
	int (*conf_tx)(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif, u16 ac,
		       const struct ieee80211_tx_queue_params *params);
	u64 (*get_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
	void (*set_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			u64 tsf);
	void (*offset_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   s64 offset);
	void (*reset_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
	int (*tx_last_beacon)(struct ieee80211_hw *hw);

	/**
	 * @ampdu_action:
	 * Perform a certain A-MPDU action.
	 * The RA/TID combination determines the destination and TID we want
	 * the ampdu action to be performed for. The action is defined through
	 * ieee80211_ampdu_mlme_action.
	 * When the action is set to %IEEE80211_AMPDU_TX_OPERATIONAL the driver
	 * may neither send aggregates containing more subframes than @buf_size
	 * nor send aggregates in a way that lost frames would exceed the
	 * buffer size. If just limiting the aggregate size, this would be
	 * possible with a buf_size of 8:
	 *
	 * - ``TX: 1.....7``
	 * - ``RX:  2....7`` (lost frame #1)
	 * - ``TX:        8..1...``
	 *
	 * which is invalid since #1 was now re-transmitted well past the
	 * buffer size of 8. Correct ways to retransmit #1 would be:
	 *
	 * - ``TX:        1   or``
	 * - ``TX:        18  or``
	 * - ``TX:        81``
	 *
	 * Even ``189`` would be wrong since 1 could be lost again.
	 *
	 * Returns a negative error code on failure.
	 * The callback can sleep.
	 */
	int (*ampdu_action)(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_ampdu_params *params);
	int (*get_survey)(struct ieee80211_hw *hw, int idx,
		struct survey_info *survey);
	void (*rfkill_poll)(struct ieee80211_hw *hw);
	void (*set_coverage_class)(struct ieee80211_hw *hw, s16 coverage_class);
#ifdef CONFIG_NL80211_TESTMODE
	int (*testmode_cmd)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    void *data, int len);
	int (*testmode_dump)(struct ieee80211_hw *hw, struct sk_buff *skb,
			     struct netlink_callback *cb,
			     void *data, int len);
#endif
	void (*flush)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      u32 queues, bool drop);
	void (*channel_switch)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_channel_switch *ch_switch);
	int (*set_antenna)(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant);
	int (*get_antenna)(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant);

	int (*remain_on_channel)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_channel *chan,
				 int duration,
				 enum ieee80211_roc_type type);
	int (*cancel_remain_on_channel)(struct ieee80211_hw *hw);
	int (*set_ringparam)(struct ieee80211_hw *hw, u32 tx, u32 rx);
	void (*get_ringparam)(struct ieee80211_hw *hw,
			      u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max);
	bool (*tx_frames_pending)(struct ieee80211_hw *hw);
	int (*set_bitrate_mask)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask);
	void (*event_callback)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       const struct ieee80211_event *event);

	void (*allow_buffered_frames)(struct ieee80211_hw *hw,
				      struct ieee80211_sta *sta,
				      u16 tids, int num_frames,
				      enum ieee80211_frame_release_type reason,
				      bool more_data);
	void (*release_buffered_frames)(struct ieee80211_hw *hw,
					struct ieee80211_sta *sta,
					u16 tids, int num_frames,
					enum ieee80211_frame_release_type reason,
					bool more_data);

	int	(*get_et_sset_count)(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif, int sset);
	void	(*get_et_stats)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ethtool_stats *stats, u64 *data);
	void	(*get_et_strings)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  u32 sset, u8 *data);

	void	(*mgd_prepare_tx)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif);

	void	(*mgd_protect_tdls_discover)(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif);

	int (*add_chanctx)(struct ieee80211_hw *hw,
			   struct ieee80211_chanctx_conf *ctx);
	void (*remove_chanctx)(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx);
	void (*change_chanctx)(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx,
			       u32 changed);
	int (*assign_vif_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_chanctx_conf *ctx);
	void (*unassign_vif_chanctx)(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_chanctx_conf *ctx);
	int (*switch_vif_chanctx)(struct ieee80211_hw *hw,
				  struct ieee80211_vif_chanctx_switch *vifs,
				  int n_vifs,
				  enum ieee80211_chanctx_switch_mode mode);

	void (*reconfig_complete)(struct ieee80211_hw *hw,
				  enum ieee80211_reconfig_type reconfig_type);

#if IS_ENABLED(CONFIG_IPV6)
	void (*ipv6_addr_change)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct inet6_dev *idev);
#endif
	void (*channel_switch_beacon)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_chan_def *chandef);
	int (*pre_channel_switch)(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_channel_switch *ch_switch);

	int (*post_channel_switch)(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif);

	int (*join_ibss)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
	void (*leave_ibss)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
	u32 (*get_expected_throughput)(struct ieee80211_hw *hw,
				       struct ieee80211_sta *sta);
	int (*get_txpower)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   int *dbm);

	int (*tdls_channel_switch)(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta, u8 oper_class,
				   struct cfg80211_chan_def *chandef,
				   struct sk_buff *tmpl_skb, u32 ch_sw_tm_ie);
	void (*tdls_cancel_channel_switch)(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta);
	void (*tdls_recv_channel_switch)(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_tdls_ch_sw_params *params);

	void (*wake_tx_queue)(struct ieee80211_hw *hw,
			      struct ieee80211_txq *txq);
	void (*sync_rx_queues)(struct ieee80211_hw *hw);

	int (*start_nan)(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct cfg80211_nan_conf *conf);
	int (*stop_nan)(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif);
	int (*nan_change_conf)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct cfg80211_nan_conf *conf, u32 changes);
	int (*add_nan_func)(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    const struct cfg80211_nan_func *nan_func);
	void (*del_nan_func)(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    u8 instance_id);
};

/**
 * ieee80211_alloc_hw_nm - Allocate a new hardware device
 *
 * This must be called once for each hardware device. The returned pointer
 * must be used to refer to this device when calling other functions.
 * mac80211 allocates a private data area for the driver pointed to by
 * @priv in &struct ieee80211_hw, the size of this area is given as
 * @priv_data_len.
 *
 * @priv_data_len: length of private data
 * @ops: callbacks for this device
 * @requested_name: Requested name for this device.
 *	NULL is valid value, and means use the default naming (phy%d)
 *
 * Return: A pointer to the new hardware device, or %NULL on error.
 */
struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
					   const struct ieee80211_ops *ops,
					   const char *requested_name);

/**
 * ieee80211_alloc_hw - Allocate a new hardware device
 *
 * This must be called once for each hardware device. The returned pointer
 * must be used to refer to this device when calling other functions.
 * mac80211 allocates a private data area for the driver pointed to by
 * @priv in &struct ieee80211_hw, the size of this area is given as
 * @priv_data_len.
 *
 * @priv_data_len: length of private data
 * @ops: callbacks for this device
 *
 * Return: A pointer to the new hardware device, or %NULL on error.
 */
static inline
struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops)
{
	return ieee80211_alloc_hw_nm(priv_data_len, ops, NULL);
}

/**
 * ieee80211_register_hw - Register hardware device
 *
 * You must call this function before any other functions in
 * mac80211. Note that before a hardware can be registered, you
 * need to fill the contained wiphy's information.
 *
 * @hw: the device to register as returned by ieee80211_alloc_hw()
 *
 * Return: 0 on success. An error code otherwise.
 */
int ieee80211_register_hw(struct ieee80211_hw *hw);

/**
 * struct ieee80211_tpt_blink - throughput blink description
 * @throughput: throughput in Kbit/sec
 * @blink_time: blink time in milliseconds
 *	(full cycle, ie. one off + one on period)
 */
struct ieee80211_tpt_blink {
	int throughput;
	int blink_time;
};

/**
 * enum ieee80211_tpt_led_trigger_flags - throughput trigger flags
 * @IEEE80211_TPT_LEDTRIG_FL_RADIO: enable blinking with radio
 * @IEEE80211_TPT_LEDTRIG_FL_WORK: enable blinking when working
 * @IEEE80211_TPT_LEDTRIG_FL_CONNECTED: enable blinking when at least one
 *	interface is connected in some way, including being an AP
 */
enum ieee80211_tpt_led_trigger_flags {
	IEEE80211_TPT_LEDTRIG_FL_RADIO		= BIT(0),
	IEEE80211_TPT_LEDTRIG_FL_WORK		= BIT(1),
	IEEE80211_TPT_LEDTRIG_FL_CONNECTED	= BIT(2),
};

#ifdef CONFIG_MAC80211_LEDS
const char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw);
const char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw);
const char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw);
const char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw);
const char *
__ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw,
				   unsigned int flags,
				   const struct ieee80211_tpt_blink *blink_table,
				   unsigned int blink_table_len);
#endif
/**
 * ieee80211_get_tx_led_name - get name of TX LED
 *
 * mac80211 creates a transmit LED trigger for each wireless hardware
 * that can be used to drive LEDs if your driver registers a LED device.
 * This function returns the name (or %NULL if not configured for LEDs)
 * of the trigger so you can automatically link the LED device.
 *
 * @hw: the hardware to get the LED trigger name for
 *
 * Return: The name of the LED trigger. %NULL if not configured for LEDs.
 */
static inline const char *ieee80211_get_tx_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_tx_led_name(hw);
#else
	return NULL;
#endif
}

/**
 * ieee80211_get_rx_led_name - get name of RX LED
 *
 * mac80211 creates a receive LED trigger for each wireless hardware
 * that can be used to drive LEDs if your driver registers a LED device.
 * This function returns the name (or %NULL if not configured for LEDs)
 * of the trigger so you can automatically link the LED device.
 *
 * @hw: the hardware to get the LED trigger name for
 *
 * Return: The name of the LED trigger. %NULL if not configured for LEDs.
 */
static inline const char *ieee80211_get_rx_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_rx_led_name(hw);
#else
	return NULL;
#endif
}

/**
 * ieee80211_get_assoc_led_name - get name of association LED
 *
 * mac80211 creates a association LED trigger for each wireless hardware
 * that can be used to drive LEDs if your driver registers a LED device.
 * This function returns the name (or %NULL if not configured for LEDs)
 * of the trigger so you can automatically link the LED device.
 *
 * @hw: the hardware to get the LED trigger name for
 *
 * Return: The name of the LED trigger. %NULL if not configured for LEDs.
 */
static inline const char *ieee80211_get_assoc_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_assoc_led_name(hw);
#else
	return NULL;
#endif
}

/**
 * ieee80211_get_radio_led_name - get name of radio LED
 *
 * mac80211 creates a radio change LED trigger for each wireless hardware
 * that can be used to drive LEDs if your driver registers a LED device.
 * This function returns the name (or %NULL if not configured for LEDs)
 * of the trigger so you can automatically link the LED device.
 *
 * @hw: the hardware to get the LED trigger name for
 *
 * Return: The name of the LED trigger. %NULL if not configured for LEDs.
 */
static inline const char *ieee80211_get_radio_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_radio_led_name(hw);
#else
	return NULL;
#endif
}

/**
 * ieee80211_create_tpt_led_trigger - create throughput LED trigger
 * @hw: the hardware to create the trigger for
 * @flags: trigger flags, see &enum ieee80211_tpt_led_trigger_flags
 * @blink_table: the blink table -- needs to be ordered by throughput
 * @blink_table_len: size of the blink table
 *
 * Return: %NULL (in case of error, or if no LED triggers are
 * configured) or the name of the new trigger.
 *
 * Note: This function must be called before ieee80211_register_hw().
 */
static inline const char *
ieee80211_create_tpt_led_trigger(struct ieee80211_hw *hw, unsigned int flags,
				 const struct ieee80211_tpt_blink *blink_table,
				 unsigned int blink_table_len)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_create_tpt_led_trigger(hw, flags, blink_table,
						  blink_table_len);
#else
	return NULL;
#endif
}

/**
 * ieee80211_unregister_hw - Unregister a hardware device
 *
 * This function instructs mac80211 to free allocated resources
 * and unregister netdevices from the networking subsystem.
 *
 * @hw: the hardware to unregister
 */
void ieee80211_unregister_hw(struct ieee80211_hw *hw);

/**
 * ieee80211_free_hw - free hardware descriptor
 *
 * This function frees everything that was allocated, including the
 * private data for the driver. You must call ieee80211_unregister_hw()
 * before calling this function.
 *
 * @hw: the hardware to free
 */
void ieee80211_free_hw(struct ieee80211_hw *hw);

/**
 * ieee80211_restart_hw - restart hardware completely
 *
 * Call this function when the hardware was restarted for some reason
 * (hardware error, ...) and the driver is unable to restore its state
 * by itself. mac80211 assumes that at this point the driver/hardware
 * is completely uninitialised and stopped, it starts the process by
 * calling the ->start() operation. The driver will need to reset all
 * internal state that it has prior to calling this function.
 *
 * @hw: the hardware to restart
 */
void ieee80211_restart_hw(struct ieee80211_hw *hw);

/**
 * ieee80211_rx_napi - receive frame from NAPI context
 *
 * Use this function to hand received frames to mac80211. The receive
 * buffer in @skb must start with an IEEE 802.11 header. In case of a
 * paged @skb is used, the driver is recommended to put the ieee80211
 * header of the frame on the linear part of the @skb to avoid memory
 * allocation and/or memcpy by the stack.
 *
 * This function may not be called in IRQ context. Calls to this function
 * for a single hardware must be synchronized against each other. Calls to
 * this function, ieee80211_rx_ni() and ieee80211_rx_irqsafe() may not be
 * mixed for a single hardware. Must not run concurrently with
 * ieee80211_tx_status() or ieee80211_tx_status_ni().
 *
 * This function must be called with BHs disabled.
 *
 * @hw: the hardware this frame came in on
 * @sta: the station the frame was received from, or %NULL
 * @skb: the buffer to receive, owned by mac80211 after this call
 * @napi: the NAPI context
 */
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       struct sk_buff *skb, struct napi_struct *napi);

/**
 * ieee80211_rx - receive frame
 *
 * Use this function to hand received frames to mac80211. The receive
 * buffer in @skb must start with an IEEE 802.11 header. In case of a
 * paged @skb is used, the driver is recommended to put the ieee80211
 * header of the frame on the linear part of the @skb to avoid memory
 * allocation and/or memcpy by the stack.
 *
 * This function may not be called in IRQ context. Calls to this function
 * for a single hardware must be synchronized against each other. Calls to
 * this function, ieee80211_rx_ni() and ieee80211_rx_irqsafe() may not be
 * mixed for a single hardware. Must not run concurrently with
 * ieee80211_tx_status() or ieee80211_tx_status_ni().
 *
 * In process context use instead ieee80211_rx_ni().
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 */
static inline void ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	ieee80211_rx_napi(hw, NULL, skb, NULL);
}

/**
 * ieee80211_rx_irqsafe - receive frame
 *
 * Like ieee80211_rx() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * Calls to this function, ieee80211_rx() or ieee80211_rx_ni() may not
 * be mixed for a single hardware.Must not run concurrently with
 * ieee80211_tx_status() or ieee80211_tx_status_ni().
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 */
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);

/**
 * ieee80211_rx_ni - receive frame (in process context)
 *
 * Like ieee80211_rx() but can be called in process context
 * (internally disables bottom halves).
 *
 * Calls to this function, ieee80211_rx() and ieee80211_rx_irqsafe() may
 * not be mixed for a single hardware. Must not run concurrently with
 * ieee80211_tx_status() or ieee80211_tx_status_ni().
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 */
static inline void ieee80211_rx_ni(struct ieee80211_hw *hw,
				   struct sk_buff *skb)
{
	local_bh_disable();
	ieee80211_rx(hw, skb);
	local_bh_enable();
}

/**
 * ieee80211_sta_ps_transition - PS transition for connected sta
 *
 * When operating in AP mode with the %IEEE80211_HW_AP_LINK_PS
 * flag set, use this function to inform mac80211 about a connected station
 * entering/leaving PS mode.
 *
 * This function may not be called in IRQ context or with softirqs enabled.
 *
 * Calls to this function for a single hardware must be synchronized against
 * each other.
 *
 * @sta: currently connected sta
 * @start: start or stop PS
 *
 * Return: 0 on success. -EINVAL when the requested PS mode is already set.
 */
int ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start);

/**
 * ieee80211_sta_ps_transition_ni - PS transition for connected sta
 *                                  (in process context)
 *
 * Like ieee80211_sta_ps_transition() but can be called in process context
 * (internally disables bottom halves). Concurrent call restriction still
 * applies.
 *
 * @sta: currently connected sta
 * @start: start or stop PS
 *
 * Return: Like ieee80211_sta_ps_transition().
 */
static inline int ieee80211_sta_ps_transition_ni(struct ieee80211_sta *sta,
						  bool start)
{
	int ret;

	local_bh_disable();
	ret = ieee80211_sta_ps_transition(sta, start);
	local_bh_enable();

	return ret;
}

/**
 * ieee80211_sta_pspoll - PS-Poll frame received
 * @sta: currently connected station
 *
 * When operating in AP mode with the %IEEE80211_HW_AP_LINK_PS flag set,
 * use this function to inform mac80211 that a PS-Poll frame from a
 * connected station was received.
 * This must be used in conjunction with ieee80211_sta_ps_transition()
 * and possibly ieee80211_sta_uapsd_trigger(); calls to all three must
 * be serialized.
 */
void ieee80211_sta_pspoll(struct ieee80211_sta *sta);

/**
 * ieee80211_sta_uapsd_trigger - (potential) U-APSD trigger frame received
 * @sta: currently connected station
 * @tid: TID of the received (potential) trigger frame
 *
 * When operating in AP mode with the %IEEE80211_HW_AP_LINK_PS flag set,
 * use this function to inform mac80211 that a (potential) trigger frame
 * from a connected station was received.
 * This must be used in conjunction with ieee80211_sta_ps_transition()
 * and possibly ieee80211_sta_pspoll(); calls to all three must be
 * serialized.
 * %IEEE80211_NUM_TIDS can be passed as the tid if the tid is unknown.
 * In this case, mac80211 will not check that this tid maps to an AC
 * that is trigger enabled and assume that the caller did the proper
 * checks.
 */
void ieee80211_sta_uapsd_trigger(struct ieee80211_sta *sta, u8 tid);

/*
 * The TX headroom reserved by mac80211 for its own tx_status functions.
 * This is enough for the radiotap header.
 */
#define IEEE80211_TX_STATUS_HEADROOM	ALIGN(14, 4)

/**
 * ieee80211_sta_set_buffered - inform mac80211 about driver-buffered frames
 * @sta: &struct ieee80211_sta pointer for the sleeping station
 * @tid: the TID that has buffered frames
 * @buffered: indicates whether or not frames are buffered for this TID
 *
 * If a driver buffers frames for a powersave station instead of passing
 * them back to mac80211 for retransmission, the station may still need
 * to be told that there are buffered frames via the TIM bit.
 *
 * This function informs mac80211 whether or not there are frames that are
 * buffered in the driver for a given TID; mac80211 can then use this data
 * to set the TIM bit (NOTE: This may call back into the driver's set_tim
 * call! Beware of the locking!)
 *
 * If all frames are released to the station (due to PS-poll or uAPSD)
 * then the driver needs to inform mac80211 that there no longer are
 * frames buffered. However, when the station wakes up mac80211 assumes
 * that all buffered frames will be transmitted and clears this data,
 * drivers need to make sure they inform mac80211 about all buffered
 * frames on the sleep transition (sta_notify() with %STA_NOTIFY_SLEEP).
 *
 * Note that technically mac80211 only needs to know this per AC, not per
 * TID, but since driver buffering will inevitably happen per TID (since
 * it is related to aggregation) it is easier to make mac80211 map the
 * TID to the AC as required instead of keeping track in all drivers that
 * use this API.
 */
void ieee80211_sta_set_buffered(struct ieee80211_sta *sta,
				u8 tid, bool buffered);

/**
 * ieee80211_get_tx_rates - get the selected transmit rates for a packet
 *
 * Call this function in a driver with per-packet rate selection support
 * to combine the rate info in the packet tx info with the most recent
 * rate selection table for the station entry.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @sta: the receiver station to which this packet is sent.
 * @skb: the frame to be transmitted.
 * @dest: buffer for extracted rate/retry information
 * @max_rates: maximum number of rates to fetch
 */
void ieee80211_get_tx_rates(struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    struct ieee80211_tx_rate *dest,
			    int max_rates);

/**
 * ieee80211_sta_set_expected_throughput - set the expected tpt for a station
 *
 * Call this function to notify mac80211 about a change in expected throughput
 * to a station. A driver for a device that does rate control in firmware can
 * call this function when the expected throughput estimate towards a station
 * changes. The information is used to tune the CoDel AQM applied to traffic
 * going towards that station (which can otherwise be too aggressive and cause
 * slow stations to starve).
 *
 * @pubsta: the station to set throughput for.
 * @thr: the current expected throughput in kbps.
 */
void ieee80211_sta_set_expected_throughput(struct ieee80211_sta *pubsta,
					   u32 thr);

/**
 * ieee80211_tx_status - transmit status callback
 *
 * Call this function for all transmitted frames after they have been
 * transmitted. It is permissible to not call this function for
 * multicast frames but this can affect statistics.
 *
 * This function may not be called in IRQ context. Calls to this function
 * for a single hardware must be synchronized against each other. Calls
 * to this function, ieee80211_tx_status_ni() and ieee80211_tx_status_irqsafe()
 * may not be mixed for a single hardware. Must not run concurrently with
 * ieee80211_rx() or ieee80211_rx_ni().
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
void ieee80211_tx_status(struct ieee80211_hw *hw,
			 struct sk_buff *skb);

/**
 * ieee80211_tx_status_ext - extended transmit status callback
 *
 * This function can be used as a replacement for ieee80211_tx_status
 * in drivers that may want to provide extra information that does not
 * fit into &struct ieee80211_tx_info.
 *
 * Calls to this function for a single hardware must be synchronized
 * against each other. Calls to this function, ieee80211_tx_status_ni()
 * and ieee80211_tx_status_irqsafe() may not be mixed for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @status: tx status information
 */
void ieee80211_tx_status_ext(struct ieee80211_hw *hw,
			     struct ieee80211_tx_status *status);

/**
 * ieee80211_tx_status_noskb - transmit status callback without skb
 *
 * This function can be used as a replacement for ieee80211_tx_status
 * in drivers that cannot reliably map tx status information back to
 * specific skbs.
 *
 * Calls to this function for a single hardware must be synchronized
 * against each other. Calls to this function, ieee80211_tx_status_ni()
 * and ieee80211_tx_status_irqsafe() may not be mixed for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @sta: the receiver station to which this packet is sent
 *	(NULL for multicast packets)
 * @info: tx status information
 */
static inline void ieee80211_tx_status_noskb(struct ieee80211_hw *hw,
					     struct ieee80211_sta *sta,
					     struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_status status = {
		.sta = sta,
		.info = info,
	};

	ieee80211_tx_status_ext(hw, &status);
}

/**
 * ieee80211_tx_status_ni - transmit status callback (in process context)
 *
 * Like ieee80211_tx_status() but can be called in process context.
 *
 * Calls to this function, ieee80211_tx_status() and
 * ieee80211_tx_status_irqsafe() may not be mixed
 * for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
static inline void ieee80211_tx_status_ni(struct ieee80211_hw *hw,
					  struct sk_buff *skb)
{
	local_bh_disable();
	ieee80211_tx_status(hw, skb);
	local_bh_enable();
}

/**
 * ieee80211_tx_status_irqsafe - IRQ-safe transmit status callback
 *
 * Like ieee80211_tx_status() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * Calls to this function, ieee80211_tx_status() and
 * ieee80211_tx_status_ni() may not be mixed for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb);

/**
 * ieee80211_report_low_ack - report non-responding station
 *
 * When operating in AP-mode, call this function to report a non-responding
 * connected STA.
 *
 * @sta: the non-responding connected sta
 * @num_packets: number of packets sent to @sta without a response
 */
void ieee80211_report_low_ack(struct ieee80211_sta *sta, u32 num_packets);

#define IEEE80211_MAX_CSA_COUNTERS_NUM 2

/**
 * struct ieee80211_mutable_offsets - mutable beacon offsets
 * @tim_offset: position of TIM element
 * @tim_length: size of TIM element
 * @csa_counter_offs: array of IEEE80211_MAX_CSA_COUNTERS_NUM offsets
 *	to CSA counters.  This array can contain zero values which
 *	should be ignored.
 */
struct ieee80211_mutable_offsets {
	u16 tim_offset;
	u16 tim_length;

	u16 csa_counter_offs[IEEE80211_MAX_CSA_COUNTERS_NUM];
};

/**
 * ieee80211_beacon_get_template - beacon template generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @offs: &struct ieee80211_mutable_offsets pointer to struct that will
 *	receive the offsets that may be updated by the driver.
 *
 * If the driver implements beaconing modes, it must use this function to
 * obtain the beacon template.
 *
 * This function should be used if the beacon frames are generated by the
 * device, and then the driver must use the returned beacon as the template
 * The driver or the device are responsible to update the DTIM and, when
 * applicable, the CSA count.
 *
 * The driver is responsible for freeing the returned skb.
 *
 * Return: The beacon template. %NULL on error.
 */
struct sk_buff *
ieee80211_beacon_get_template(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_mutable_offsets *offs);

/**
 * ieee80211_beacon_get_tim - beacon generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @tim_offset: pointer to variable that will receive the TIM IE offset.
 *	Set to 0 if invalid (in non-AP modes).
 * @tim_length: pointer to variable that will receive the TIM IE length,
 *	(including the ID and length bytes!).
 *	Set to 0 if invalid (in non-AP modes).
 *
 * If the driver implements beaconing modes, it must use this function to
 * obtain the beacon frame.
 *
 * If the beacon frames are generated by the host system (i.e., not in
 * hardware/firmware), the driver uses this function to get each beacon
 * frame from mac80211 -- it is responsible for calling this function exactly
 * once before the beacon is needed (e.g. based on hardware interrupt).
 *
 * The driver is responsible for freeing the returned skb.
 *
 * Return: The beacon template. %NULL on error.
 */
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 u16 *tim_offset, u16 *tim_length);

/**
 * ieee80211_beacon_get - beacon generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * See ieee80211_beacon_get_tim().
 *
 * Return: See ieee80211_beacon_get_tim().
 */
static inline struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
						   struct ieee80211_vif *vif)
{
	return ieee80211_beacon_get_tim(hw, vif, NULL, NULL);
}

/**
 * ieee80211_csa_update_counter - request mac80211 to decrement the csa counter
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * The csa counter should be updated after each beacon transmission.
 * This function is called implicitly when
 * ieee80211_beacon_get/ieee80211_beacon_get_tim are called, however if the
 * beacon frames are generated by the device, the driver should call this
 * function after each beacon transmission to sync mac80211's csa counters.
 *
 * Return: new csa counter value
 */
u8 ieee80211_csa_update_counter(struct ieee80211_vif *vif);

/**
 * ieee80211_csa_set_counter - request mac80211 to set csa counter
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @counter: the new value for the counter
 *
 * The csa counter can be changed by the device, this API should be
 * used by the device driver to update csa counter in mac80211.
 *
 * It should never be used together with ieee80211_csa_update_counter(),
 * as it will cause a race condition around the counter value.
 */
void ieee80211_csa_set_counter(struct ieee80211_vif *vif, u8 counter);

/**
 * ieee80211_csa_finish - notify mac80211 about channel switch
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * After a channel switch announcement was scheduled and the counter in this
 * announcement hits 1, this function must be called by the driver to
 * notify mac80211 that the channel can be changed.
 */
void ieee80211_csa_finish(struct ieee80211_vif *vif);

/**
 * ieee80211_csa_is_complete - find out if counters reached 1
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * This function returns whether the channel switch counters reached zero.
 */
bool ieee80211_csa_is_complete(struct ieee80211_vif *vif);


/**
 * ieee80211_proberesp_get - retrieve a Probe Response template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Creates a Probe Response template which can, for example, be uploaded to
 * hardware. The destination address should be set by the caller.
 *
 * Can only be called in AP mode.
 *
 * Return: The Probe Response template. %NULL on error.
 */
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif);

/**
 * ieee80211_pspoll_get - retrieve a PS Poll template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Creates a PS Poll a template which can, for example, uploaded to
 * hardware. The template must be updated after association so that correct
 * AID, BSSID and MAC address is used.
 *
 * Note: Caller (or hardware) is responsible for setting the
 * &IEEE80211_FCTL_PM bit.
 *
 * Return: The PS Poll template. %NULL on error.
 */
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif);

/**
 * ieee80211_nullfunc_get - retrieve a nullfunc template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @qos_ok: QoS NDP is acceptable to the caller, this should be set
 *	if at all possible
 *
 * Creates a Nullfunc template which can, for example, uploaded to
 * hardware. The template must be updated after association so that correct
 * BSSID and address is used.
 *
 * If @qos_ndp is set and the association is to an AP with QoS/WMM, the
 * returned packet will be QoS NDP.
 *
 * Note: Caller (or hardware) is responsible for setting the
 * &IEEE80211_FCTL_PM bit as well as Duration and Sequence Control fields.
 *
 * Return: The nullfunc template. %NULL on error.
 */
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       bool qos_ok);

/**
 * ieee80211_probereq_get - retrieve a Probe Request template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @src_addr: source MAC address
 * @ssid: SSID buffer
 * @ssid_len: length of SSID
 * @tailroom: tailroom to reserve at end of SKB for IEs
 *
 * Creates a Probe Request template which can, for example, be uploaded to
 * hardware.
 *
 * Return: The Probe Request template. %NULL on error.
 */
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
				       const u8 *src_addr,
				       const u8 *ssid, size_t ssid_len,
				       size_t tailroom);

/**
 * ieee80211_rts_get - RTS frame generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @frame: pointer to the frame that is going to be protected by the RTS.
 * @frame_len: the frame length (in octets).
 * @frame_txctl: &struct ieee80211_tx_info of the frame.
 * @rts: The buffer where to store the RTS frame.
 *
 * If the RTS frames are generated by the host system (i.e., not in
 * hardware/firmware), the low-level driver uses this function to receive
 * the next RTS frame from the 802.11 code. The low-level is responsible
 * for calling this function before and RTS frame is needed.
 */
void ieee80211_rts_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_info *frame_txctl,
		       struct ieee80211_rts *rts);

/**
 * ieee80211_rts_duration - Get the duration field for an RTS frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @frame_len: the length of the frame that is going to be protected by the RTS.
 * @frame_txctl: &struct ieee80211_tx_info of the frame.
 *
 * If the RTS is generated in firmware, but the host system must provide
 * the duration field, the low-level driver uses this function to receive
 * the duration field value in little-endian byteorder.
 *
 * Return: The duration.
 */
__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, size_t frame_len,
			      const struct ieee80211_tx_info *frame_txctl);

/**
 * ieee80211_ctstoself_get - CTS-to-self frame generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @frame: pointer to the frame that is going to be protected by the CTS-to-self.
 * @frame_len: the frame length (in octets).
 * @frame_txctl: &struct ieee80211_tx_info of the frame.
 * @cts: The buffer where to store the CTS-to-self frame.
 *
 * If the CTS-to-self frames are generated by the host system (i.e., not in
 * hardware/firmware), the low-level driver uses this function to receive
 * the next CTS-to-self frame from the 802.11 code. The low-level is responsible
 * for calling this function before and CTS-to-self frame is needed.
 */
void ieee80211_ctstoself_get(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_info *frame_txctl,
			     struct ieee80211_cts *cts);

/**
 * ieee80211_ctstoself_duration - Get the duration field for a CTS-to-self frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @frame_len: the length of the frame that is going to be protected by the CTS-to-self.
 * @frame_txctl: &struct ieee80211_tx_info of the frame.
 *
 * If the CTS-to-self is generated in firmware, but the host system must provide
 * the duration field, the low-level driver uses this function to receive
 * the duration field value in little-endian byteorder.
 *
 * Return: The duration.
 */
__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    size_t frame_len,
				    const struct ieee80211_tx_info *frame_txctl);

/**
 * ieee80211_generic_frame_duration - Calculate the duration field for a frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @band: the band to calculate the frame duration on
 * @frame_len: the length of the frame.
 * @rate: the rate at which the frame is going to be transmitted.
 *
 * Calculate the duration field of some generic frame, given its
 * length and transmission rate (in 100kbps).
 *
 * Return: The duration.
 */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					enum nl80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate);

/**
 * ieee80211_get_buffered_bc - accessing buffered broadcast and multicast frames
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Function for accessing buffered broadcast and multicast frames. If
 * hardware/firmware does not implement buffering of broadcast/multicast
 * frames when power saving is used, 802.11 code buffers them in the host
 * memory. The low-level driver uses this function to fetch next buffered
 * frame. In most cases, this is used when generating beacon frame.
 *
 * Return: A pointer to the next buffered skb or NULL if no more buffered
 * frames are available.
 *
 * Note: buffered frames are returned only after DTIM beacon frame was
 * generated with ieee80211_beacon_get() and the low-level driver must thus
 * call ieee80211_beacon_get() first. ieee80211_get_buffered_bc() returns
 * NULL if the previous generated beacon was not DTIM, so the low-level driver
 * does not need to check for DTIM beacons separately and should be able to
 * use common code for all beacons.
 */
struct sk_buff *
ieee80211_get_buffered_bc(struct ieee80211_hw *hw, struct ieee80211_vif *vif);

/**
 * ieee80211_get_tkip_p1k_iv - get a TKIP phase 1 key for IV32
 *
 * This function returns the TKIP phase 1 key for the given IV32.
 *
 * @keyconf: the parameter passed with the set key
 * @iv32: IV32 to get the P1K for
 * @p1k: a buffer to which the key will be written, as 5 u16 values
 */
void ieee80211_get_tkip_p1k_iv(struct ieee80211_key_conf *keyconf,
			       u32 iv32, u16 *p1k);

/**
 * ieee80211_get_tkip_p1k - get a TKIP phase 1 key
 *
 * This function returns the TKIP phase 1 key for the IV32 taken
 * from the given packet.
 *
 * @keyconf: the parameter passed with the set key
 * @skb: the packet to take the IV32 value from that will be encrypted
 *	with this P1K
 * @p1k: a buffer to which the key will be written, as 5 u16 values
 */
static inline void ieee80211_get_tkip_p1k(struct ieee80211_key_conf *keyconf,
					  struct sk_buff *skb, u16 *p1k)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	const u8 *data = (u8 *)hdr + ieee80211_hdrlen(hdr->frame_control);
	u32 iv32 = get_unaligned_le32(&data[4]);

	ieee80211_get_tkip_p1k_iv(keyconf, iv32, p1k);
}

/**
 * ieee80211_get_tkip_rx_p1k - get a TKIP phase 1 key for RX
 *
 * This function returns the TKIP phase 1 key for the given IV32
 * and transmitter address.
 *
 * @keyconf: the parameter passed with the set key
 * @ta: TA that will be used with the key
 * @iv32: IV32 to get the P1K for
 * @p1k: a buffer to which the key will be written, as 5 u16 values
 */
void ieee80211_get_tkip_rx_p1k(struct ieee80211_key_conf *keyconf,
			       const u8 *ta, u32 iv32, u16 *p1k);

/**
 * ieee80211_get_tkip_p2k - get a TKIP phase 2 key
 *
 * This function computes the TKIP RC4 key for the IV values
 * in the packet.
 *
 * @keyconf: the parameter passed with the set key
 * @skb: the packet to take the IV32/IV16 values from that will be
 *	encrypted with this key
 * @p2k: a buffer to which the key will be written, 16 bytes
 */
void ieee80211_get_tkip_p2k(struct ieee80211_key_conf *keyconf,
			    struct sk_buff *skb, u8 *p2k);

/**
 * ieee80211_tkip_add_iv - write TKIP IV and Ext. IV to pos
 *
 * @pos: start of crypto header
 * @keyconf: the parameter passed with the set key
 * @pn: PN to add
 *
 * Returns: pointer to the octet following IVs (i.e. beginning of
 * the packet payload)
 *
 * This function writes the tkip IV value to pos (which should
 * point to the crypto header)
 */
u8 *ieee80211_tkip_add_iv(u8 *pos, struct ieee80211_key_conf *keyconf, u64 pn);

/**
 * ieee80211_get_key_rx_seq - get key RX sequence counter
 *
 * @keyconf: the parameter passed with the set key
 * @tid: The TID, or -1 for the management frame value (CCMP/GCMP only);
 *	the value on TID 0 is also used for non-QoS frames. For
 *	CMAC, only TID 0 is valid.
 * @seq: buffer to receive the sequence data
 *
 * This function allows a driver to retrieve the current RX IV/PNs
 * for the given key. It must not be called if IV checking is done
 * by the device and not by mac80211.
 *
 * Note that this function may only be called when no RX processing
 * can be done concurrently.
 */
void ieee80211_get_key_rx_seq(struct ieee80211_key_conf *keyconf,
			      int tid, struct ieee80211_key_seq *seq);

/**
 * ieee80211_set_key_rx_seq - set key RX sequence counter
 *
 * @keyconf: the parameter passed with the set key
 * @tid: The TID, or -1 for the management frame value (CCMP/GCMP only);
 *	the value on TID 0 is also used for non-QoS frames. For
 *	CMAC, only TID 0 is valid.
 * @seq: new sequence data
 *
 * This function allows a driver to set the current RX IV/PNs for the
 * given key. This is useful when resuming from WoWLAN sleep and GTK
 * rekey may have been done while suspended. It should not be called
 * if IV checking is done by the device and not by mac80211.
 *
 * Note that this function may only be called when no RX processing
 * can be done concurrently.
 */
void ieee80211_set_key_rx_seq(struct ieee80211_key_conf *keyconf,
			      int tid, struct ieee80211_key_seq *seq);

/**
 * ieee80211_remove_key - remove the given key
 * @keyconf: the parameter passed with the set key
 *
 * Remove the given key. If the key was uploaded to the hardware at the
 * time this function is called, it is not deleted in the hardware but
 * instead assumed to have been removed already.
 *
 * Note that due to locking considerations this function can (currently)
 * only be called during key iteration (ieee80211_iter_keys().)
 */
void ieee80211_remove_key(struct ieee80211_key_conf *keyconf);

/**
 * ieee80211_gtk_rekey_add - add a GTK key from rekeying during WoWLAN
 * @vif: the virtual interface to add the key on
 * @keyconf: new key data
 *
 * When GTK rekeying was done while the system was suspended, (a) new
 * key(s) will be available. These will be needed by mac80211 for proper
 * RX processing, so this function allows setting them.
 *
 * The function returns the newly allocated key structure, which will
 * have similar contents to the passed key configuration but point to
 * mac80211-owned memory. In case of errors, the function returns an
 * ERR_PTR(), use IS_ERR() etc.
 *
 * Note that this function assumes the key isn't added to hardware
 * acceleration, so no TX will be done with the key. Since it's a GTK
 * on managed (station) networks, this is true anyway. If the driver
 * calls this function from the resume callback and subsequently uses
 * the return code 1 to reconfigure the device, this key will be part
 * of the reconfiguration.
 *
 * Note that the driver should also call ieee80211_set_key_rx_seq()
 * for the new key for each TID to set up sequence counters properly.
 *
 * IMPORTANT: If this replaces a key that is present in the hardware,
 * then it will attempt to remove it during this call. In many cases
 * this isn't what you want, so call ieee80211_remove_key() first for
 * the key that's being replaced.
 */
struct ieee80211_key_conf *
ieee80211_gtk_rekey_add(struct ieee80211_vif *vif,
			struct ieee80211_key_conf *keyconf);

/**
 * ieee80211_gtk_rekey_notify - notify userspace supplicant of rekeying
 * @vif: virtual interface the rekeying was done on
 * @bssid: The BSSID of the AP, for checking association
 * @replay_ctr: the new replay counter after GTK rekeying
 * @gfp: allocation flags
 */
void ieee80211_gtk_rekey_notify(struct ieee80211_vif *vif, const u8 *bssid,
				const u8 *replay_ctr, gfp_t gfp);

/**
 * ieee80211_wake_queue - wake specific queue
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @queue: queue number (counted from zero).
 *
 * Drivers should use this function instead of netif_wake_queue.
 */
void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue);

/**
 * ieee80211_stop_queue - stop specific queue
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @queue: queue number (counted from zero).
 *
 * Drivers should use this function instead of netif_stop_queue.
 */
void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue);

/**
 * ieee80211_queue_stopped - test status of the queue
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @queue: queue number (counted from zero).
 *
 * Drivers should use this function instead of netif_stop_queue.
 *
 * Return: %true if the queue is stopped. %false otherwise.
 */

int ieee80211_queue_stopped(struct ieee80211_hw *hw, int queue);

/**
 * ieee80211_stop_queues - stop all queues
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 *
 * Drivers should use this function instead of netif_stop_queue.
 */
void ieee80211_stop_queues(struct ieee80211_hw *hw);

/**
 * ieee80211_wake_queues - wake all queues
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 *
 * Drivers should use this function instead of netif_wake_queue.
 */
void ieee80211_wake_queues(struct ieee80211_hw *hw);

/**
 * ieee80211_scan_completed - completed hardware scan
 *
 * When hardware scan offload is used (i.e. the hw_scan() callback is
 * assigned) this function needs to be called by the driver to notify
 * mac80211 that the scan finished. This function can be called from
 * any context, including hardirq context.
 *
 * @hw: the hardware that finished the scan
 * @info: information about the completed scan
 */
void ieee80211_scan_completed(struct ieee80211_hw *hw,
			      struct cfg80211_scan_info *info);

/**
 * ieee80211_sched_scan_results - got results from scheduled scan
 *
 * When a scheduled scan is running, this function needs to be called by the
 * driver whenever there are new scan results available.
 *
 * @hw: the hardware that is performing scheduled scans
 */
void ieee80211_sched_scan_results(struct ieee80211_hw *hw);

/**
 * ieee80211_sched_scan_stopped - inform that the scheduled scan has stopped
 *
 * When a scheduled scan is running, this function can be called by
 * the driver if it needs to stop the scan to perform another task.
 * Usual scenarios are drivers that cannot continue the scheduled scan
 * while associating, for instance.
 *
 * @hw: the hardware that is performing scheduled scans
 */
void ieee80211_sched_scan_stopped(struct ieee80211_hw *hw);

/**
 * enum ieee80211_interface_iteration_flags - interface iteration flags
 * @IEEE80211_IFACE_ITER_NORMAL: Iterate over all interfaces that have
 *	been added to the driver; However, note that during hardware
 *	reconfiguration (after restart_hw) it will iterate over a new
 *	interface and over all the existing interfaces even if they
 *	haven't been re-added to the driver yet.
 * @IEEE80211_IFACE_ITER_RESUME_ALL: During resume, iterate over all
 *	interfaces, even if they haven't been re-added to the driver yet.
 * @IEEE80211_IFACE_ITER_ACTIVE: Iterate only active interfaces (netdev is up).
 */
enum ieee80211_interface_iteration_flags {
	IEEE80211_IFACE_ITER_NORMAL	= 0,
	IEEE80211_IFACE_ITER_RESUME_ALL	= BIT(0),
	IEEE80211_IFACE_ITER_ACTIVE	= BIT(1),
};

/**
 * ieee80211_iterate_interfaces - iterate interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware and calls the callback for them. This includes active as well as
 * inactive interfaces. This function allows the iterator function to sleep.
 * Will iterate over a new interface during add_interface().
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iter_flags: iteration flags, see &enum ieee80211_interface_iteration_flags
 * @iterator: the iterator function to call
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_interfaces(struct ieee80211_hw *hw, u32 iter_flags,
				  void (*iterator)(void *data, u8 *mac,
						   struct ieee80211_vif *vif),
				  void *data);

/**
 * ieee80211_iterate_active_interfaces - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This function allows the iterator function to sleep, when the iterator
 * function is atomic @ieee80211_iterate_active_interfaces_atomic can
 * be used.
 * Does not iterate over a new interface during add_interface().
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iter_flags: iteration flags, see &enum ieee80211_interface_iteration_flags
 * @iterator: the iterator function to call
 * @data: first argument of the iterator function
 */
static inline void
ieee80211_iterate_active_interfaces(struct ieee80211_hw *hw, u32 iter_flags,
				    void (*iterator)(void *data, u8 *mac,
						     struct ieee80211_vif *vif),
				    void *data)
{
	ieee80211_iterate_interfaces(hw,
				     iter_flags | IEEE80211_IFACE_ITER_ACTIVE,
				     iterator, data);
}

/**
 * ieee80211_iterate_active_interfaces_atomic - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This function requires the iterator callback function to be atomic,
 * if that is not desired, use @ieee80211_iterate_active_interfaces instead.
 * Does not iterate over a new interface during add_interface().
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iter_flags: iteration flags, see &enum ieee80211_interface_iteration_flags
 * @iterator: the iterator function to call, cannot sleep
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
						u32 iter_flags,
						void (*iterator)(void *data,
						    u8 *mac,
						    struct ieee80211_vif *vif),
						void *data);

/**
 * ieee80211_iterate_active_interfaces_rtnl - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This version can only be used while holding the RTNL.
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iter_flags: iteration flags, see &enum ieee80211_interface_iteration_flags
 * @iterator: the iterator function to call, cannot sleep
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_active_interfaces_rtnl(struct ieee80211_hw *hw,
					      u32 iter_flags,
					      void (*iterator)(void *data,
						u8 *mac,
						struct ieee80211_vif *vif),
					      void *data);

/**
 * ieee80211_iterate_stations_atomic - iterate stations
 *
 * This function iterates over all stations associated with a given
 * hardware that are currently uploaded to the driver and calls the callback
 * function for them.
 * This function requires the iterator callback function to be atomic,
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iterator: the iterator function to call, cannot sleep
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
				       void (*iterator)(void *data,
						struct ieee80211_sta *sta),
				       void *data);
/**
 * ieee80211_queue_work - add work onto the mac80211 workqueue
 *
 * Drivers and mac80211 use this to add work onto the mac80211 workqueue.
 * This helper ensures drivers are not queueing work when they should not be.
 *
 * @hw: the hardware struct for the interface we are adding work for
 * @work: the work we want to add onto the mac80211 workqueue
 */
void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work);

/**
 * ieee80211_queue_delayed_work - add work onto the mac80211 workqueue
 *
 * Drivers and mac80211 use this to queue delayed work onto the mac80211
 * workqueue.
 *
 * @hw: the hardware struct for the interface we are adding work for
 * @dwork: delayable work to queue onto the mac80211 workqueue
 * @delay: number of jiffies to wait before queueing
 */
void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct delayed_work *dwork,
				  unsigned long delay);

/**
 * ieee80211_start_tx_ba_session - Start a tx Block Ack session.
 * @sta: the station for which to start a BA session
 * @tid: the TID to BA on.
 * @timeout: session timeout value (in TUs)
 *
 * Return: success if addBA request was sent, failure otherwise
 *
 * Although mac80211/low level driver/user space application can estimate
 * the need to start aggregation on a certain RA/TID, the session level
 * will be managed by the mac80211.
 */
int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid,
				  u16 timeout);

/**
 * ieee80211_start_tx_ba_cb_irqsafe - low level driver ready to aggregate.
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @ra: receiver address of the BA session recipient.
 * @tid: the TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session. It can be called
 * from any context.
 */
void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
				      u16 tid);

/**
 * ieee80211_stop_tx_ba_session - Stop a Block Ack session.
 * @sta: the station whose BA session to stop
 * @tid: the TID to stop BA.
 *
 * Return: negative error if the TID is invalid, or no aggregation active
 *
 * Although mac80211/low level driver/user space application can estimate
 * the need to stop aggregation on a certain RA/TID, the session level
 * will be managed by the mac80211.
 */
int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid);

/**
 * ieee80211_stop_tx_ba_cb_irqsafe - low level driver ready to stop aggregate.
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @ra: receiver address of the BA session recipient.
 * @tid: the desired TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session tear down. It
 * can be called from any context.
 */
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
				     u16 tid);

/**
 * ieee80211_find_sta - find a station
 *
 * @vif: virtual interface to look for station on
 * @addr: station's address
 *
 * Return: The station, if found. %NULL otherwise.
 *
 * Note: This function must be called under RCU lock and the
 * resulting pointer is only valid under RCU lock as well.
 */
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr);

/**
 * ieee80211_find_sta_by_ifaddr - find a station on hardware
 *
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 * @addr: remote station's address
 * @localaddr: local address (vif->sdata->vif.addr). Use NULL for 'any'.
 *
 * Return: The station, if found. %NULL otherwise.
 *
 * Note: This function must be called under RCU lock and the
 * resulting pointer is only valid under RCU lock as well.
 *
 * NOTE: You may pass NULL for localaddr, but then you will just get
 *      the first STA that matches the remote address 'addr'.
 *      We can have multiple STA associated with multiple
 *      logical stations (e.g. consider a station connecting to another
 *      BSSID on the same AP hardware without disconnecting first).
 *      In this case, the result of this method with localaddr NULL
 *      is not reliable.
 *
 * DO NOT USE THIS FUNCTION with localaddr NULL if at all possible.
 */
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
					       const u8 *addr,
					       const u8 *localaddr);

/**
 * ieee80211_sta_block_awake - block station from waking up
 * @hw: the hardware
 * @pubsta: the station
 * @block: whether to block or unblock
 *
 * Some devices require that all frames that are on the queues
 * for a specific station that went to sleep are flushed before
 * a poll response or frames after the station woke up can be
 * delivered to that it. Note that such frames must be rejected
 * by the driver as filtered, with the appropriate status flag.
 *
 * This function allows implementing this mode in a race-free
 * manner.
 *
 * To do this, a driver must keep track of the number of frames
 * still enqueued for a specific station. If this number is not
 * zero when the station goes to sleep, the driver must call
 * this function to force mac80211 to consider the station to
 * be asleep regardless of the station's actual state. Once the
 * number of outstanding frames reaches zero, the driver must
 * call this function again to unblock the station. That will
 * cause mac80211 to be able to send ps-poll responses, and if
 * the station queried in the meantime then frames will also
 * be sent out as a result of this. Additionally, the driver
 * will be notified that the station woke up some time after
 * it is unblocked, regardless of whether the station actually
 * woke up while blocked or not.
 */
void ieee80211_sta_block_awake(struct ieee80211_hw *hw,
			       struct ieee80211_sta *pubsta, bool block);

/**
 * ieee80211_sta_eosp - notify mac80211 about end of SP
 * @pubsta: the station
 *
 * When a device transmits frames in a way that it can't tell
 * mac80211 in the TX status about the EOSP, it must clear the
 * %IEEE80211_TX_STATUS_EOSP bit and call this function instead.
 * This applies for PS-Poll as well as uAPSD.
 *
 * Note that just like with _tx_status() and _rx() drivers must
 * not mix calls to irqsafe/non-irqsafe versions, this function
 * must not be mixed with those either. Use the all irqsafe, or
 * all non-irqsafe, don't mix!
 *
 * NB: the _irqsafe version of this function doesn't exist, no
 *     driver needs it right now. Don't call this function if
 *     you'd need the _irqsafe version, look at the git history
 *     and restore the _irqsafe version!
 */
void ieee80211_sta_eosp(struct ieee80211_sta *pubsta);

/**
 * ieee80211_send_eosp_nullfunc - ask mac80211 to send NDP with EOSP
 * @pubsta: the station
 * @tid: the tid of the NDP
 *
 * Sometimes the device understands that it needs to close
 * the Service Period unexpectedly. This can happen when
 * sending frames that are filling holes in the BA window.
 * In this case, the device can ask mac80211 to send a
 * Nullfunc frame with EOSP set. When that happens, the
 * driver must have called ieee80211_sta_set_buffered() to
 * let mac80211 know that there are no buffered frames any
 * more, otherwise mac80211 will get the more_data bit wrong.
 * The low level driver must have made sure that the frame
 * will be sent despite the station being in power-save.
 * Mac80211 won't call allow_buffered_frames().
 * Note that calling this function, doesn't exempt the driver
 * from closing the EOSP properly, it will still have to call
 * ieee80211_sta_eosp when the NDP is sent.
 */
void ieee80211_send_eosp_nullfunc(struct ieee80211_sta *pubsta, int tid);

/**
 * ieee80211_iter_keys - iterate keys programmed into the device
 * @hw: pointer obtained from ieee80211_alloc_hw()
 * @vif: virtual interface to iterate, may be %NULL for all
 * @iter: iterator function that will be called for each key
 * @iter_data: custom data to pass to the iterator function
 *
 * This function can be used to iterate all the keys known to
 * mac80211, even those that weren't previously programmed into
 * the device. This is intended for use in WoWLAN if the device
 * needs reprogramming of the keys during suspend. Note that due
 * to locking reasons, it is also only safe to call this at few
 * spots since it must hold the RTNL and be able to sleep.
 *
 * The order in which the keys are iterated matches the order
 * in which they were originally installed and handed to the
 * set_key callback.
 */
void ieee80211_iter_keys(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 void (*iter)(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      struct ieee80211_key_conf *key,
				      void *data),
			 void *iter_data);

/**
 * ieee80211_iter_keys_rcu - iterate keys programmed into the device
 * @hw: pointer obtained from ieee80211_alloc_hw()
 * @vif: virtual interface to iterate, may be %NULL for all
 * @iter: iterator function that will be called for each key
 * @iter_data: custom data to pass to the iterator function
 *
 * This function can be used to iterate all the keys known to
 * mac80211, even those that weren't previously programmed into
 * the device. Note that due to locking reasons, keys of station
 * in removal process will be skipped.
 *
 * This function requires being called in an RCU critical section,
 * and thus iter must be atomic.
 */
void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     void (*iter)(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_key_conf *key,
					  void *data),
			     void *iter_data);

/**
 * ieee80211_iter_chan_contexts_atomic - iterate channel contexts
 * @hw: pointre obtained from ieee80211_alloc_hw().
 * @iter: iterator function
 * @iter_data: data passed to iterator function
 *
 * Iterate all active channel contexts. This function is atomic and
 * doesn't acquire any locks internally that might be held in other
 * places while calling into the driver.
 *
 * The iterator will not find a context that's being added (during
 * the driver callback to add it) but will find it while it's being
 * removed.
 *
 * Note that during hardware restart, all contexts that existed
 * before the restart are considered already present so will be
 * found while iterating, whether they've been re-added already
 * or not.
 */
void ieee80211_iter_chan_contexts_atomic(
	struct ieee80211_hw *hw,
	void (*iter)(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *chanctx_conf,
		     void *data),
	void *iter_data);

/**
 * ieee80211_ap_probereq_get - retrieve a Probe Request template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Creates a Probe Request template which can, for example, be uploaded to
 * hardware. The template is filled with bssid, ssid and supported rate
 * information. This function must only be called from within the
 * .bss_info_changed callback function and only in managed mode. The function
 * is only useful when the interface is associated, otherwise it will return
 * %NULL.
 *
 * Return: The Probe Request template. %NULL on error.
 */
struct sk_buff *ieee80211_ap_probereq_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif);

/**
 * ieee80211_beacon_loss - inform hardware does not receive beacons
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * When beacon filtering is enabled with %IEEE80211_VIF_BEACON_FILTER and
 * %IEEE80211_CONF_PS is set, the driver needs to inform whenever the
 * hardware is not receiving beacons with this function.
 */
void ieee80211_beacon_loss(struct ieee80211_vif *vif);

/**
 * ieee80211_connection_loss - inform hardware has lost connection to the AP
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * When beacon filtering is enabled with %IEEE80211_VIF_BEACON_FILTER, and
 * %IEEE80211_CONF_PS and %IEEE80211_HW_CONNECTION_MONITOR are set, the driver
 * needs to inform if the connection to the AP has been lost.
 * The function may also be called if the connection needs to be terminated
 * for some other reason, even if %IEEE80211_HW_CONNECTION_MONITOR isn't set.
 *
 * This function will cause immediate change to disassociated state,
 * without connection recovery attempts.
 */
void ieee80211_connection_loss(struct ieee80211_vif *vif);

/**
 * ieee80211_resume_disconnect - disconnect from AP after resume
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Instructs mac80211 to disconnect from the AP after resume.
 * Drivers can use this after WoWLAN if they know that the
 * connection cannot be kept up, for example because keys were
 * used while the device was asleep but the replay counters or
 * similar cannot be retrieved from the device during resume.
 *
 * Note that due to implementation issues, if the driver uses
 * the reconfiguration functionality during resume the interface
 * will still be added as associated first during resume and then
 * disconnect normally later.
 *
 * This function can only be called from the resume callback and
 * the driver must not be holding any of its own locks while it
 * calls this function, or at least not any locks it needs in the
 * key configuration paths (if it supports HW crypto).
 */
void ieee80211_resume_disconnect(struct ieee80211_vif *vif);

/**
 * ieee80211_cqm_rssi_notify - inform a configured connection quality monitoring
 *	rssi threshold triggered
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @rssi_event: the RSSI trigger event type
 * @rssi_level: new RSSI level value or 0 if not available
 * @gfp: context flags
 *
 * When the %IEEE80211_VIF_SUPPORTS_CQM_RSSI is set, and a connection quality
 * monitoring is configured with an rssi threshold, the driver will inform
 * whenever the rssi level reaches the threshold.
 */
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       s32 rssi_level,
			       gfp_t gfp);

/**
 * ieee80211_cqm_beacon_loss_notify - inform CQM of beacon loss
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @gfp: context flags
 */
void ieee80211_cqm_beacon_loss_notify(struct ieee80211_vif *vif, gfp_t gfp);

/**
 * ieee80211_radar_detected - inform that a radar was detected
 *
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 */
void ieee80211_radar_detected(struct ieee80211_hw *hw);

/**
 * ieee80211_chswitch_done - Complete channel switch process
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @success: make the channel switch successful or not
 *
 * Complete the channel switch post-process: set the new operational channel
 * and wake up the suspended queues.
 */
void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success);

/**
 * ieee80211_request_smps - request SM PS transition
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @smps_mode: new SM PS mode
 *
 * This allows the driver to request an SM PS transition in managed
 * mode. This is useful when the driver has more information than
 * the stack about possible interference, for example by bluetooth.
 */
void ieee80211_request_smps(struct ieee80211_vif *vif,
			    enum ieee80211_smps_mode smps_mode);

/**
 * ieee80211_ready_on_channel - notification of remain-on-channel start
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 */
void ieee80211_ready_on_channel(struct ieee80211_hw *hw);

/**
 * ieee80211_remain_on_channel_expired - remain_on_channel duration expired
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 */
void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw);

/**
 * ieee80211_stop_rx_ba_session - callback to stop existing BA sessions
 *
 * in order not to harm the system performance and user experience, the device
 * may request not to allow any rx ba session and tear down existing rx ba
 * sessions based on system constraints such as periodic BT activity that needs
 * to limit wlan activity (eg.sco or a2dp)."
 * in such cases, the intention is to limit the duration of the rx ppdu and
 * therefore prevent the peer device to use a-mpdu aggregation.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @ba_rx_bitmap: Bit map of open rx ba per tid
 * @addr: & to bssid mac address
 */
void ieee80211_stop_rx_ba_session(struct ieee80211_vif *vif, u16 ba_rx_bitmap,
				  const u8 *addr);

/**
 * ieee80211_mark_rx_ba_filtered_frames - move RX BA window and mark filtered
 * @pubsta: station struct
 * @tid: the session's TID
 * @ssn: starting sequence number of the bitmap, all frames before this are
 *	assumed to be out of the window after the call
 * @filtered: bitmap of filtered frames, BIT(0) is the @ssn entry etc.
 * @received_mpdus: number of received mpdus in firmware
 *
 * This function moves the BA window and releases all frames before @ssn, and
 * marks frames marked in the bitmap as having been filtered. Afterwards, it
 * checks if any frames in the window starting from @ssn can now be released
 * (in case they were only waiting for frames that were filtered.)
 */
void ieee80211_mark_rx_ba_filtered_frames(struct ieee80211_sta *pubsta, u8 tid,
					  u16 ssn, u64 filtered,
					  u16 received_mpdus);

/**
 * ieee80211_send_bar - send a BlockAckReq frame
 *
 * can be used to flush pending frames from the peer's aggregation reorder
 * buffer.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @ra: the peer's destination address
 * @tid: the TID of the aggregation session
 * @ssn: the new starting sequence number for the receiver
 */
void ieee80211_send_bar(struct ieee80211_vif *vif, u8 *ra, u16 tid, u16 ssn);

/**
 * ieee80211_manage_rx_ba_offl - helper to queue an RX BA work
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @addr: station mac address
 * @tid: the rx tid
 */
void ieee80211_manage_rx_ba_offl(struct ieee80211_vif *vif, const u8 *addr,
				 unsigned int tid);

/**
 * ieee80211_start_rx_ba_session_offl - start a Rx BA session
 *
 * Some device drivers may offload part of the Rx aggregation flow including
 * AddBa/DelBa negotiation but may otherwise be incapable of full Rx
 * reordering.
 *
 * Create structures responsible for reordering so device drivers may call here
 * when they complete AddBa negotiation.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @addr: station mac address
 * @tid: the rx tid
 */
static inline void ieee80211_start_rx_ba_session_offl(struct ieee80211_vif *vif,
						      const u8 *addr, u16 tid)
{
	if (WARN_ON(tid >= IEEE80211_NUM_TIDS))
		return;
	ieee80211_manage_rx_ba_offl(vif, addr, tid);
}

/**
 * ieee80211_stop_rx_ba_session_offl - stop a Rx BA session
 *
 * Some device drivers may offload part of the Rx aggregation flow including
 * AddBa/DelBa negotiation but may otherwise be incapable of full Rx
 * reordering.
 *
 * Destroy structures responsible for reordering so device drivers may call here
 * when they complete DelBa negotiation.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @addr: station mac address
 * @tid: the rx tid
 */
static inline void ieee80211_stop_rx_ba_session_offl(struct ieee80211_vif *vif,
						     const u8 *addr, u16 tid)
{
	if (WARN_ON(tid >= IEEE80211_NUM_TIDS))
		return;
	ieee80211_manage_rx_ba_offl(vif, addr, tid + IEEE80211_NUM_TIDS);
}

/**
 * ieee80211_rx_ba_timer_expired - stop a Rx BA session due to timeout
 *
 * Some device drivers do not offload AddBa/DelBa negotiation, but handle rx
 * buffer reording internally, and therefore also handle the session timer.
 *
 * Trigger the timeout flow, which sends a DelBa.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback
 * @addr: station mac address
 * @tid: the rx tid
 */
void ieee80211_rx_ba_timer_expired(struct ieee80211_vif *vif,
				   const u8 *addr, unsigned int tid);

/* Rate control API */

/**
 * struct ieee80211_tx_rate_control - rate control information for/from RC algo
 *
 * @hw: The hardware the algorithm is invoked for.
 * @sband: The band this frame is being transmitted on.
 * @bss_conf: the current BSS configuration
 * @skb: the skb that will be transmitted, the control information in it needs
 *	to be filled in
 * @reported_rate: The rate control algorithm can fill this in to indicate
 *	which rate should be reported to userspace as the current rate and
 *	used for rate calculations in the mesh network.
 * @rts: whether RTS will be used for this frame because it is longer than the
 *	RTS threshold
 * @short_preamble: whether mac80211 will request short-preamble transmission
 *	if the selected rate supports it
 * @rate_idx_mask: user-requested (legacy) rate mask
 * @rate_idx_mcs_mask: user-requested MCS rate mask (NULL if not in use)
 * @bss: whether this frame is sent out in AP or IBSS mode
 */
struct ieee80211_tx_rate_control {
	struct ieee80211_hw *hw;
	struct ieee80211_supported_band *sband;
	struct ieee80211_bss_conf *bss_conf;
	struct sk_buff *skb;
	struct ieee80211_tx_rate reported_rate;
	bool rts, short_preamble;
	u32 rate_idx_mask;
	u8 *rate_idx_mcs_mask;
	bool bss;
};

struct rate_control_ops {
	const char *name;
	void *(*alloc)(struct ieee80211_hw *hw, struct dentry *debugfsdir);
	void (*free)(void *priv);

	void *(*alloc_sta)(void *priv, struct ieee80211_sta *sta, gfp_t gfp);
	void (*rate_init)(void *priv, struct ieee80211_supported_band *sband,
			  struct cfg80211_chan_def *chandef,
			  struct ieee80211_sta *sta, void *priv_sta);
	void (*rate_update)(void *priv, struct ieee80211_supported_band *sband,
			    struct cfg80211_chan_def *chandef,
			    struct ieee80211_sta *sta, void *priv_sta,
			    u32 changed);
	void (*free_sta)(void *priv, struct ieee80211_sta *sta,
			 void *priv_sta);

	void (*tx_status_ext)(void *priv,
			      struct ieee80211_supported_band *sband,
			      void *priv_sta, struct ieee80211_tx_status *st);
	void (*tx_status)(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb);
	void (*get_rate)(void *priv, struct ieee80211_sta *sta, void *priv_sta,
			 struct ieee80211_tx_rate_control *txrc);

	void (*add_sta_debugfs)(void *priv, void *priv_sta,
				struct dentry *dir);
	void (*remove_sta_debugfs)(void *priv, void *priv_sta);

	u32 (*get_expected_throughput)(void *priv_sta);
};

static inline int rate_supported(struct ieee80211_sta *sta,
				 enum nl80211_band band,
				 int index)
{
	return (sta == NULL || sta->supp_rates[band] & BIT(index));
}

/**
 * rate_control_send_low - helper for drivers for management/no-ack frames
 *
 * Rate control algorithms that agree to use the lowest rate to
 * send management frames and NO_ACK data with the respective hw
 * retries should use this in the beginning of their mac80211 get_rate
 * callback. If true is returned the rate control can simply return.
 * If false is returned we guarantee that sta and sta and priv_sta is
 * not null.
 *
 * Rate control algorithms wishing to do more intelligent selection of
 * rate for multicast/broadcast frames may choose to not use this.
 *
 * @sta: &struct ieee80211_sta pointer to the target destination. Note
 * 	that this may be null.
 * @priv_sta: private rate control structure. This may be null.
 * @txrc: rate control information we sholud populate for mac80211.
 */
bool rate_control_send_low(struct ieee80211_sta *sta,
			   void *priv_sta,
			   struct ieee80211_tx_rate_control *txrc);


static inline s8
rate_lowest_index(struct ieee80211_supported_band *sband,
		  struct ieee80211_sta *sta)
{
	int i;

	for (i = 0; i < sband->n_bitrates; i++)
		if (rate_supported(sta, sband->band, i))
			return i;

	/* warn when we cannot find a rate. */
	WARN_ON_ONCE(1);

	/* and return 0 (the lowest index) */
	return 0;
}

static inline
bool rate_usable_index_exists(struct ieee80211_supported_band *sband,
			      struct ieee80211_sta *sta)
{
	unsigned int i;

	for (i = 0; i < sband->n_bitrates; i++)
		if (rate_supported(sta, sband->band, i))
			return true;
	return false;
}

/**
 * rate_control_set_rates - pass the sta rate selection to mac80211/driver
 *
 * When not doing a rate control probe to test rates, rate control should pass
 * its rate selection to mac80211. If the driver supports receiving a station
 * rate table, it will use it to ensure that frames are always sent based on
 * the most recent rate control module decision.
 *
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 * @pubsta: &struct ieee80211_sta pointer to the target destination.
 * @rates: new tx rate set to be used for this station.
 */
int rate_control_set_rates(struct ieee80211_hw *hw,
			   struct ieee80211_sta *pubsta,
			   struct ieee80211_sta_rates *rates);

int ieee80211_rate_control_register(const struct rate_control_ops *ops);
void ieee80211_rate_control_unregister(const struct rate_control_ops *ops);

static inline bool
conf_is_ht20(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_20;
}

static inline bool
conf_is_ht40_minus(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40 &&
	       conf->chandef.center_freq1 < conf->chandef.chan->center_freq;
}

static inline bool
conf_is_ht40_plus(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40 &&
	       conf->chandef.center_freq1 > conf->chandef.chan->center_freq;
}

static inline bool
conf_is_ht40(struct ieee80211_conf *conf)
{
	return conf->chandef.width == NL80211_CHAN_WIDTH_40;
}

static inline bool
conf_is_ht(struct ieee80211_conf *conf)
{
	return (conf->chandef.width != NL80211_CHAN_WIDTH_5) &&
		(conf->chandef.width != NL80211_CHAN_WIDTH_10) &&
		(conf->chandef.width != NL80211_CHAN_WIDTH_20_NOHT);
}

static inline enum nl80211_iftype
ieee80211_iftype_p2p(enum nl80211_iftype type, bool p2p)
{
	if (p2p) {
		switch (type) {
		case NL80211_IFTYPE_STATION:
			return NL80211_IFTYPE_P2P_CLIENT;
		case NL80211_IFTYPE_AP:
			return NL80211_IFTYPE_P2P_GO;
		default:
			break;
		}
	}
	return type;
}

static inline enum nl80211_iftype
ieee80211_vif_type_p2p(struct ieee80211_vif *vif)
{
	return ieee80211_iftype_p2p(vif->type, vif->p2p);
}

/**
 * ieee80211_update_mu_groups - set the VHT MU-MIMO groud data
 *
 * @vif: the specified virtual interface
 * @membership: 64 bits array - a bit is set if station is member of the group
 * @position: 2 bits per group id indicating the position in the group
 *
 * Note: This function assumes that the given vif is valid and the position and
 * membership data is of the correct size and are in the same byte order as the
 * matching GroupId management frame.
 * Calls to this function need to be serialized with RX path.
 */
void ieee80211_update_mu_groups(struct ieee80211_vif *vif,
				const u8 *membership, const u8 *position);

void ieee80211_enable_rssi_reports(struct ieee80211_vif *vif,
				   int rssi_min_thold,
				   int rssi_max_thold);

void ieee80211_disable_rssi_reports(struct ieee80211_vif *vif);

/**
 * ieee80211_ave_rssi - report the average RSSI for the specified interface
 *
 * @vif: the specified virtual interface
 *
 * Note: This function assumes that the given vif is valid.
 *
 * Return: The average RSSI value for the requested interface, or 0 if not
 * applicable.
 */
int ieee80211_ave_rssi(struct ieee80211_vif *vif);

/**
 * ieee80211_report_wowlan_wakeup - report WoWLAN wakeup
 * @vif: virtual interface
 * @wakeup: wakeup reason(s)
 * @gfp: allocation flags
 *
 * See cfg80211_report_wowlan_wakeup().
 */
void ieee80211_report_wowlan_wakeup(struct ieee80211_vif *vif,
				    struct cfg80211_wowlan_wakeup *wakeup,
				    gfp_t gfp);

/**
 * ieee80211_tx_prepare_skb - prepare an 802.11 skb for transmission
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 * @vif: virtual interface
 * @skb: frame to be sent from within the driver
 * @band: the band to transmit on
 * @sta: optional pointer to get the station to send the frame to
 *
 * Note: must be called under RCU lock
 */
bool ieee80211_tx_prepare_skb(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, struct sk_buff *skb,
			      int band, struct ieee80211_sta **sta);

/**
 * struct ieee80211_noa_data - holds temporary data for tracking P2P NoA state
 *
 * @next_tsf: TSF timestamp of the next absent state change
 * @has_next_tsf: next absent state change event pending
 *
 * @absent: descriptor bitmask, set if GO is currently absent
 *
 * private:
 *
 * @count: count fields from the NoA descriptors
 * @desc: adjusted data from the NoA
 */
struct ieee80211_noa_data {
	u32 next_tsf;
	bool has_next_tsf;

	u8 absent;

	u8 count[IEEE80211_P2P_NOA_DESC_MAX];
	struct {
		u32 start;
		u32 duration;
		u32 interval;
	} desc[IEEE80211_P2P_NOA_DESC_MAX];
};

/**
 * ieee80211_parse_p2p_noa - initialize NoA tracking data from P2P IE
 *
 * @attr: P2P NoA IE
 * @data: NoA tracking data
 * @tsf: current TSF timestamp
 *
 * Return: number of successfully parsed descriptors
 */
int ieee80211_parse_p2p_noa(const struct ieee80211_p2p_noa_attr *attr,
			    struct ieee80211_noa_data *data, u32 tsf);

/**
 * ieee80211_update_p2p_noa - get next pending P2P GO absent state change
 *
 * @data: NoA tracking data
 * @tsf: current TSF timestamp
 */
void ieee80211_update_p2p_noa(struct ieee80211_noa_data *data, u32 tsf);

/**
 * ieee80211_tdls_oper - request userspace to perform a TDLS operation
 * @vif: virtual interface
 * @peer: the peer's destination address
 * @oper: the requested TDLS operation
 * @reason_code: reason code for the operation, valid for TDLS teardown
 * @gfp: allocation flags
 *
 * See cfg80211_tdls_oper_request().
 */
void ieee80211_tdls_oper_request(struct ieee80211_vif *vif, const u8 *peer,
				 enum nl80211_tdls_operation oper,
				 u16 reason_code, gfp_t gfp);

/**
 * ieee80211_reserve_tid - request to reserve a specific TID
 *
 * There is sometimes a need (such as in TDLS) for blocking the driver from
 * using a specific TID so that the FW can use it for certain operations such
 * as sending PTI requests. To make sure that the driver doesn't use that TID,
 * this function must be called as it flushes out packets on this TID and marks
 * it as blocked, so that any transmit for the station on this TID will be
 * redirected to the alternative TID in the same AC.
 *
 * Note that this function blocks and may call back into the driver, so it
 * should be called without driver locks held. Also note this function should
 * only be called from the driver's @sta_state callback.
 *
 * @sta: the station to reserve the TID for
 * @tid: the TID to reserve
 *
 * Returns: 0 on success, else on failure
 */
int ieee80211_reserve_tid(struct ieee80211_sta *sta, u8 tid);

/**
 * ieee80211_unreserve_tid - request to unreserve a specific TID
 *
 * Once there is no longer any need for reserving a certain TID, this function
 * should be called, and no longer will packets have their TID modified for
 * preventing use of this TID in the driver.
 *
 * Note that this function blocks and acquires a lock, so it should be called
 * without driver locks held. Also note this function should only be called
 * from the driver's @sta_state callback.
 *
 * @sta: the station
 * @tid: the TID to unreserve
 */
void ieee80211_unreserve_tid(struct ieee80211_sta *sta, u8 tid);

/**
 * ieee80211_tx_dequeue - dequeue a packet from a software tx queue
 *
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 * @txq: pointer obtained from station or virtual interface
 *
 * Returns the skb if successful, %NULL if no frame was available.
 */
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
				     struct ieee80211_txq *txq);

/**
 * ieee80211_txq_get_depth - get pending frame/byte count of given txq
 *
 * The values are not guaranteed to be coherent with regard to each other, i.e.
 * txq state can change half-way of this function and the caller may end up
 * with "new" frame_cnt and "old" byte_cnt or vice-versa.
 *
 * @txq: pointer obtained from station or virtual interface
 * @frame_cnt: pointer to store frame count
 * @byte_cnt: pointer to store byte count
 */
void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
			     unsigned long *frame_cnt,
			     unsigned long *byte_cnt);

/**
 * ieee80211_nan_func_terminated - notify about NAN function termination.
 *
 * This function is used to notify mac80211 about NAN function termination.
 * Note that this function can't be called from hard irq.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @inst_id: the local instance id
 * @reason: termination reason (one of the NL80211_NAN_FUNC_TERM_REASON_*)
 * @gfp: allocation flags
 */
void ieee80211_nan_func_terminated(struct ieee80211_vif *vif,
				   u8 inst_id,
				   enum nl80211_nan_func_term_reason reason,
				   gfp_t gfp);

/**
 * ieee80211_nan_func_match - notify about NAN function match event.
 *
 * This function is used to notify mac80211 about NAN function match. The
 * cookie inside the match struct will be assigned by mac80211.
 * Note that this function can't be called from hard irq.
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @match: match event information
 * @gfp: allocation flags
 */
void ieee80211_nan_func_match(struct ieee80211_vif *vif,
			      struct cfg80211_nan_match_params *match,
			      gfp_t gfp);

#endif /* MAC80211_H */
