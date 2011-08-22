/*
 * mac80211 <-> driver interface
 *
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MAC80211_H
#define MAC80211_H

#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
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
 *
 * Finally, for received frames, the driver is able to indicate that it has
 * filled a radiotap header and put that in front of the frame; if it does
 * not do so then mac80211 may add this under certain circumstances.
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
 * enum ieee80211_max_queues - maximum number of queues
 *
 * @IEEE80211_MAX_QUEUES: Maximum number of regular device queues.
 */
enum ieee80211_max_queues {
	IEEE80211_MAX_QUEUES =		4,
};

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
 * @uapsd: is U-APSD mode enabled for the queue
 */
struct ieee80211_tx_queue_params {
	u16 txop;
	u16 cw_min;
	u16 cw_max;
	u8 aifs;
	bool uapsd;
};

struct ieee80211_low_level_stats {
	unsigned int dot11ACKFailureCount;
	unsigned int dot11RTSFailureCount;
	unsigned int dot11FCSErrorCount;
	unsigned int dot11RTSSuccessCount;
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
 */
enum ieee80211_bss_change {
	BSS_CHANGED_ASSOC		= 1<<0,
	BSS_CHANGED_ERP_CTS_PROT	= 1<<1,
	BSS_CHANGED_ERP_PREAMBLE	= 1<<2,
	BSS_CHANGED_ERP_SLOT		= 1<<3,
	BSS_CHANGED_HT                  = 1<<4,
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

	/* when adding here, make sure to change ieee80211_reconfig */
};

/*
 * The maximum number of IPv4 addresses listed for ARP filtering. If the number
 * of addresses for an interface increase beyond this value, hardware ARP
 * filtering will be disabled.
 */
#define IEEE80211_BSS_ARP_ADDR_LIST_LEN 4

/**
 * enum ieee80211_rssi_event - RSSI threshold event
 * An indicator for when RSSI goes below/above a certain threshold.
 * @RSSI_EVENT_HIGH: AP's rssi crossed the high threshold set by the driver.
 * @RSSI_EVENT_LOW: AP's rssi crossed the low threshold set by the driver.
 */
enum ieee80211_rssi_event {
	RSSI_EVENT_HIGH,
	RSSI_EVENT_LOW,
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
 * @aid: association ID number, valid only when @assoc is true
 * @use_cts_prot: use CTS protection
 * @use_short_preamble: use 802.11b short preamble;
 *	if the hardware cannot handle this it must set the
 *	IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE hardware flag
 * @use_short_slot: use short slot time (only relevant for ERP);
 *	if the hardware cannot handle this it must set the
 *	IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE hardware flag
 * @dtim_period: num of beacons before the next DTIM, for beaconing,
 *	valid in station mode only while @assoc is true and if also
 *	requested by %IEEE80211_HW_NEED_DTIM_PERIOD (cf. also hw conf
 *	@ps_dtim_period)
 * @timestamp: beacon timestamp
 * @beacon_int: beacon interval
 * @assoc_capability: capabilities taken from assoc resp
 * @basic_rates: bitmap of basic rates, each bit stands for an
 *	index into the rate table configured by the driver in
 *	the current band.
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @bssid: The BSSID for this BSS
 * @enable_beacon: whether beaconing should be enabled or not
 * @channel_type: Channel type for this BSS -- the hardware might be
 *	configured for HT40+ while this BSS only uses no-HT, for
 *	example.
 * @ht_operation_mode: HT operation mode (like in &struct ieee80211_ht_info).
 *	This field is only valid when the channel type is one of the HT types.
 * @cqm_rssi_thold: Connection quality monitor RSSI threshold, a zero value
 *	implies disabled
 * @cqm_rssi_hyst: Connection quality monitor RSSI hysteresis
 * @arp_addr_list: List of IPv4 addresses for hardware ARP filtering. The
 *	may filter ARP queries targeted for other addresses than listed here.
 *	The driver must allow ARP queries targeted for all address listed here
 *	to pass through. An empty list implies no ARP queries need to pass.
 * @arp_addr_cnt: Number of addresses currently on the list.
 * @arp_filter_enabled: Enable ARP filtering - if enabled, the hardware may
 *	filter ARP queries based on the @arp_addr_list, if disabled, the
 *	hardware must not perform any ARP filtering. Note, that the filter will
 *	be enabled also in promiscuous mode.
 * @qos: This is a QoS-enabled BSS.
 * @idle: This interface is idle. There's also a global idle flag in the
 *	hardware config which may be more appropriate depending on what
 *	your driver/device needs to do.
 */
struct ieee80211_bss_conf {
	const u8 *bssid;
	/* association related data */
	bool assoc, ibss_joined;
	u16 aid;
	/* erp related data */
	bool use_cts_prot;
	bool use_short_preamble;
	bool use_short_slot;
	bool enable_beacon;
	u8 dtim_period;
	u16 beacon_int;
	u16 assoc_capability;
	u64 timestamp;
	u32 basic_rates;
	int mcast_rate[IEEE80211_NUM_BANDS];
	u16 ht_operation_mode;
	s32 cqm_rssi_thold;
	u32 cqm_rssi_hyst;
	enum nl80211_channel_type channel_type;
	__be32 arp_addr_list[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	u8 arp_addr_cnt;
	bool arp_filter_enabled;
	bool qos;
	bool idle;
};

/**
 * enum mac80211_tx_control_flags - flags to describe transmission information/status
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
 * @IEEE80211_TX_INTFL_NEED_TXPROCESSING: completely internal to mac80211,
 *	used to indicate that a pending frame requires TX processing before
 *	it can be sent out.
 * @IEEE80211_TX_INTFL_RETRIED: completely internal to mac80211,
 *	used to indicate that a frame was already retried due to PS
 * @IEEE80211_TX_INTFL_DONT_ENCRYPT: completely internal to mac80211,
 *	used to indicate frame should not be encrypted
 * @IEEE80211_TX_CTL_PSPOLL_RESPONSE: (internal?)
 *	This frame is a response to a PS-poll frame and should be sent
 *	although the station is in powersave mode.
 * @IEEE80211_TX_CTL_MORE_FRAMES: More frames will be passed to the
 *	transmit function after the current frame, this can be used
 *	by drivers to kick the DMA queue only if unset or when the
 *	queue gets full.
 * @IEEE80211_TX_INTFL_RETRANSMISSION: This frame is being retransmitted
 *	after TX status because the destination was asleep, it must not
 *	be modified again (no seqno assignment, crypto, etc.)
 * @IEEE80211_TX_INTFL_HAS_RADIOTAP: This frame was injected and still
 *	has a radiotap header at skb->data.
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
 *
 * Note: If you have to add new flags to the enumeration, then don't
 *	 forget to update %IEEE80211_TX_TEMPORARY_FLAGS when necessary.
 */
enum mac80211_tx_control_flags {
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
	IEEE80211_TX_INTFL_NEED_TXPROCESSING	= BIT(14),
	IEEE80211_TX_INTFL_RETRIED		= BIT(15),
	IEEE80211_TX_INTFL_DONT_ENCRYPT		= BIT(16),
	IEEE80211_TX_CTL_PSPOLL_RESPONSE	= BIT(17),
	IEEE80211_TX_CTL_MORE_FRAMES		= BIT(18),
	IEEE80211_TX_INTFL_RETRANSMISSION	= BIT(19),
	IEEE80211_TX_INTFL_HAS_RADIOTAP		= BIT(20),
	IEEE80211_TX_INTFL_NL80211_FRAME_TX	= BIT(21),
	IEEE80211_TX_CTL_LDPC			= BIT(22),
	IEEE80211_TX_CTL_STBC			= BIT(23) | BIT(24),
	IEEE80211_TX_CTL_TX_OFFCHAN		= BIT(25),
	IEEE80211_TX_INTFL_TKIP_MIC_FAILURE	= BIT(26),
};

#define IEEE80211_TX_CTL_STBC_SHIFT		23

/*
 * This definition is used as a mask to clear all temporary flags, which are
 * set by the tx handlers for each transmission attempt by the mac80211 stack.
 */
#define IEEE80211_TX_TEMPORARY_FLAGS (IEEE80211_TX_CTL_NO_ACK |		      \
	IEEE80211_TX_CTL_CLEAR_PS_FILT | IEEE80211_TX_CTL_FIRST_FRAGMENT |    \
	IEEE80211_TX_CTL_SEND_AFTER_DTIM | IEEE80211_TX_CTL_AMPDU |	      \
	IEEE80211_TX_STAT_TX_FILTERED |	IEEE80211_TX_STAT_ACK |		      \
	IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_STAT_AMPDU_NO_BACK |	      \
	IEEE80211_TX_CTL_RATE_CTRL_PROBE | IEEE80211_TX_CTL_PSPOLL_RESPONSE | \
	IEEE80211_TX_CTL_MORE_FRAMES | IEEE80211_TX_CTL_LDPC |		      \
	IEEE80211_TX_CTL_STBC)

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
 * @IEEE80211_TX_RC_GREEN_FIELD: Indicates whether this rate should be used in
 *	Greenfield mode.
 * @IEEE80211_TX_RC_40_MHZ_WIDTH: Indicates if the Channel Width should be 40 MHz.
 * @IEEE80211_TX_RC_DUP_DATA: The frame should be transmitted on both of the
 *	adjacent 20 MHz channels, if the current channel type is
 *	NL80211_CHAN_HT40MINUS or NL80211_CHAN_HT40PLUS.
 * @IEEE80211_TX_RC_SHORT_GI: Short Guard interval should be used for this rate.
 */
enum mac80211_rate_control_flags {
	IEEE80211_TX_RC_USE_RTS_CTS		= BIT(0),
	IEEE80211_TX_RC_USE_CTS_PROTECT		= BIT(1),
	IEEE80211_TX_RC_USE_SHORT_PREAMBLE	= BIT(2),

	/* rate index is an MCS rate number instead of an index */
	IEEE80211_TX_RC_MCS			= BIT(3),
	IEEE80211_TX_RC_GREEN_FIELD		= BIT(4),
	IEEE80211_TX_RC_40_MHZ_WIDTH		= BIT(5),
	IEEE80211_TX_RC_DUP_DATA		= BIT(6),
	IEEE80211_TX_RC_SHORT_GI		= BIT(7),
};


/* there are 40 bytes if you don't need the rateset to be kept */
#define IEEE80211_TX_INFO_DRIVER_DATA_SIZE 40

/* if you do need the rateset, then you have less space */
#define IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE 24

/* maximum number of rate stages */
#define IEEE80211_TX_MAX_RATES	5

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
 * information
 *    { 3, 2 }, { 2, 2 }, { 1, 4 }, { -1, 0 }, { -1, 0 }
 * then this means that the frame should be transmitted
 * up to twice at rate 3, up to twice at rate 2, and up to four
 * times at rate 1 if it doesn't get acknowledged. Say it gets
 * acknowledged by the peer after the fifth attempt, the status
 * information should then contain
 *   { 3, 2 }, { 2, 2 }, { 1, 1 }, { -1, 0 } ...
 * since it was transmitted twice at rate 3, twice at rate 2
 * and once at rate 1 after which we received an acknowledgement.
 */
struct ieee80211_tx_rate {
	s8 idx;
	u8 count;
	u8 flags;
} __packed;

/**
 * struct ieee80211_tx_info - skb transmit information
 *
 * This structure is placed in skb->cb for three uses:
 *  (1) mac80211 TX control - mac80211 tells the driver what to do
 *  (2) driver internal use (if applicable)
 *  (3) TX status information - driver tells mac80211 what happened
 *
 * The TX control's sta pointer is only valid during the ->tx call,
 * it may be NULL.
 *
 * @flags: transmit info flags, defined above
 * @band: the band to transmit on (use for checking for races)
 * @antenna_sel_tx: antenna to use, 0 for automatic diversity
 * @pad: padding, ignore
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

	u8 antenna_sel_tx;

	/* 2 byte hole */
	u8 pad[2];

	union {
		struct {
			union {
				/* rate control */
				struct {
					struct ieee80211_tx_rate rates[
						IEEE80211_TX_MAX_RATES];
					s8 rts_cts_rate_idx;
				};
				/* only needed before rate control */
				unsigned long jiffies;
			};
			/* NB: vif can be NULL for injected frames */
			struct ieee80211_vif *vif;
			struct ieee80211_key_conf *hw_key;
			struct ieee80211_sta *sta;
		} control;
		struct {
			struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
			u8 ampdu_ack_len;
			int ack_signal;
			u8 ampdu_len;
			/* 15 bytes free */
		} status;
		struct {
			struct ieee80211_tx_rate driver_rates[
				IEEE80211_TX_MAX_RATES];
			void *rate_driver_data[
				IEEE80211_TX_INFO_RATE_DRIVER_DATA_SIZE / sizeof(void *)];
		};
		void *driver_data[
			IEEE80211_TX_INFO_DRIVER_DATA_SIZE / sizeof(void *)];
	};
};

/**
 * struct ieee80211_sched_scan_ies - scheduled scan IEs
 *
 * This structure is used to pass the appropriate IEs to be used in scheduled
 * scans for all bands.  It contains both the IEs passed from the userspace
 * and the ones generated by mac80211.
 *
 * @ie: array with the IEs for each supported band
 * @len: array with the total length of the IEs for each band
 */
struct ieee80211_sched_scan_ies {
	u8 *ie[IEEE80211_NUM_BANDS];
	size_t len[IEEE80211_NUM_BANDS];
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
	    offsetof(struct ieee80211_tx_info, status.ampdu_ack_len) != 23);
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
 * @RX_FLAG_IV_STRIPPED: The IV/ICV are stripped from this frame.
 *	If this flag is set, the stack cannot do any replay detection
 *	hence the driver or hardware will have to do that.
 * @RX_FLAG_FAILED_FCS_CRC: Set this flag if the FCS check failed on
 *	the frame.
 * @RX_FLAG_FAILED_PLCP_CRC: Set this flag if the PCLP check failed on
 *	the frame.
 * @RX_FLAG_MACTIME_MPDU: The timestamp passed in the RX status (@mactime
 *	field) is valid and contains the time the first symbol of the MPDU
 *	was received. This is useful in monitor mode and for proper IBSS
 *	merging.
 * @RX_FLAG_SHORTPRE: Short preamble was used for this frame
 * @RX_FLAG_HT: HT MCS was used and rate_idx is MCS index
 * @RX_FLAG_40MHZ: HT40 (40 MHz) was used
 * @RX_FLAG_SHORT_GI: Short guard interval was used
 */
enum mac80211_rx_flags {
	RX_FLAG_MMIC_ERROR	= 1<<0,
	RX_FLAG_DECRYPTED	= 1<<1,
	RX_FLAG_MMIC_STRIPPED	= 1<<3,
	RX_FLAG_IV_STRIPPED	= 1<<4,
	RX_FLAG_FAILED_FCS_CRC	= 1<<5,
	RX_FLAG_FAILED_PLCP_CRC = 1<<6,
	RX_FLAG_MACTIME_MPDU	= 1<<7,
	RX_FLAG_SHORTPRE	= 1<<8,
	RX_FLAG_HT		= 1<<9,
	RX_FLAG_40MHZ		= 1<<10,
	RX_FLAG_SHORT_GI	= 1<<11,
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
 * @band: the active band when this frame was received
 * @freq: frequency the radio was tuned to when receiving this frame, in MHz
 * @signal: signal strength when receiving this frame, either in dBm, in dB or
 *	unspecified depending on the hardware capabilities flags
 *	@IEEE80211_HW_SIGNAL_*
 * @antenna: antenna used
 * @rate_idx: index of data rate into band's supported rates or MCS index if
 *	HT rates are use (RX_FLAG_HT)
 * @flag: %RX_FLAG_*
 * @rx_flags: internal RX flags for mac80211
 */
struct ieee80211_rx_status {
	u64 mactime;
	enum ieee80211_band band;
	int freq;
	int signal;
	int antenna;
	int rate_idx;
	int flag;
	unsigned int rx_flags;
};

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
 * @max_sleep_period: the maximum number of beacon intervals to sleep for
 *	before checking the beacon for a TIM bit (managed mode only); this
 *	value will be only achievable between DTIM frames, the hardware
 *	needs to check for the multicast traffic bit in DTIM beacons.
 *	This variable is valid only when the CONF_PS flag is set.
 * @ps_dtim_period: The DTIM period of the AP we're connected to, for use
 *	in power saving. Power saving will not be enabled until a beacon
 *	has been received and the DTIM period is known.
 * @dynamic_ps_timeout: The dynamic powersave timeout (in ms), see the
 *	powersave documentation below. This variable is valid only when
 *	the CONF_PS flag is set.
 *
 * @power_level: requested transmit power (in dBm)
 *
 * @channel: the channel to tune to
 * @channel_type: the channel (HT) type
 *
 * @long_frame_max_tx_count: Maximum number of transmissions for a "long" frame
 *    (a frame not RTS protected), called "dot11LongRetryLimit" in 802.11,
 *    but actually means the number of transmissions not the number of retries
 * @short_frame_max_tx_count: Maximum number of transmissions for a "short"
 *    frame, called "dot11ShortRetryLimit" in 802.11, but actually means the
 *    number of transmissions not the number of retries
 *
 * @smps_mode: spatial multiplexing powersave mode; note that
 *	%IEEE80211_SMPS_STATIC is used when the device is not
 *	configured for an HT channel
 */
struct ieee80211_conf {
	u32 flags;
	int power_level, dynamic_ps_timeout;
	int max_sleep_period;

	u16 listen_interval;
	u8 ps_dtim_period;

	u8 long_frame_max_tx_count, short_frame_max_tx_count;

	struct ieee80211_channel *channel;
	enum nl80211_channel_type channel_type;
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
 * @block_tx: Indicates whether transmission must be blocked before the
 *	scheduled channel switch, as indicated by the AP.
 * @channel: the new channel to switch to
 * @count: the number of TBTT's until the channel switch event
 */
struct ieee80211_channel_switch {
	u64 timestamp;
	bool block_tx;
	struct ieee80211_channel *channel;
	u8 count;
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
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *).
 */
struct ieee80211_vif {
	enum nl80211_iftype type;
	struct ieee80211_bss_conf bss_conf;
	u8 addr[ETH_ALEN];
	bool p2p;
	/* must be last */
	u8 drv_priv[0] __attribute__((__aligned__(sizeof(void *))));
};

static inline bool ieee80211_vif_is_mesh(struct ieee80211_vif *vif)
{
#ifdef CONFIG_MAC80211_MESH
	return vif->type == NL80211_IFTYPE_MESH_POINT;
#endif
	return false;
}

/**
 * enum ieee80211_key_flags - key flags
 *
 * These flags are used for communication about keys between the driver
 * and mac80211, with the @flags parameter of &struct ieee80211_key_conf.
 *
 * @IEEE80211_KEY_FLAG_WMM_STA: Set by mac80211, this flag indicates
 *	that the STA this key will be used with could be using QoS.
 * @IEEE80211_KEY_FLAG_GENERATE_IV: This flag should be set by the
 *	driver to indicate that it requires IV generation for this
 *	particular key.
 * @IEEE80211_KEY_FLAG_GENERATE_MMIC: This flag should be set by
 *	the driver for a TKIP key if it requires Michael MIC
 *	generation in software.
 * @IEEE80211_KEY_FLAG_PAIRWISE: Set by mac80211, this flag indicates
 *	that the key is pairwise rather then a shared key.
 * @IEEE80211_KEY_FLAG_SW_MGMT: This flag should be set by the driver for a
 *	CCMP key if it requires CCMP encryption of management frames (MFP) to
 *	be done in software.
 */
enum ieee80211_key_flags {
	IEEE80211_KEY_FLAG_WMM_STA	= 1<<0,
	IEEE80211_KEY_FLAG_GENERATE_IV	= 1<<1,
	IEEE80211_KEY_FLAG_GENERATE_MMIC= 1<<2,
	IEEE80211_KEY_FLAG_PAIRWISE	= 1<<3,
	IEEE80211_KEY_FLAG_SW_MGMT	= 1<<4,
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
	u32 cipher;
	u8 icv_len;
	u8 iv_len;
	u8 hw_key_idx;
	u8 flags;
	s8 keyidx;
	u8 keylen;
	u8 key[0];
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
 * @ht_cap: HT capabilities of this STA; restricted to our own TX capabilities
 * @wme: indicates whether the STA supports WME. Only valid during AP-mode.
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *), size is determined in hw information.
 */
struct ieee80211_sta {
	u32 supp_rates[IEEE80211_NUM_BANDS];
	u8 addr[ETH_ALEN];
	u16 aid;
	struct ieee80211_sta_ht_cap ht_cap;
	bool wme;

	/* must be last */
	u8 drv_priv[0] __attribute__((__aligned__(sizeof(void *))));
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
 * @IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE:
 *	Hardware is not capable of short slot operation on the 2.4 GHz band.
 *
 * @IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE:
 *	Hardware is not capable of receiving frames with short preamble on
 *	the 2.4 GHz band.
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
 * @IEEE80211_HW_BEACON_FILTER:
 *	Hardware supports dropping of irrelevant beacon frames to
 *	avoid waking up cpu.
 *
 * @IEEE80211_HW_SUPPORTS_STATIC_SMPS:
 *	Hardware supports static spatial multiplexing powersave,
 *	ie. can turn off all but one chain even on HT connections
 *	that should be using more chains.
 *
 * @IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS:
 *	Hardware supports dynamic spatial multiplexing powersave,
 *	ie. can turn off all but one chain and then wake the rest
 *	up as required after, for example, rts/cts handshake.
 *
 * @IEEE80211_HW_SUPPORTS_UAPSD:
 *	Hardware supports Unscheduled Automatic Power Save Delivery
 *	(U-APSD) in managed mode. The mode is configured with
 *	conf_tx() operation.
 *
 * @IEEE80211_HW_REPORTS_TX_ACK_STATUS:
 *	Hardware can provide ack status reports of Tx frames to
 *	the stack.
 *
 * @IEEE80211_HW_CONNECTION_MONITOR:
 *      The hardware performs its own connection monitoring, including
 *      periodic keep-alives to the AP and probing the AP on beacon loss.
 *      When this flag is set, signaling beacon-loss will cause an immediate
 *      change to disassociated state.
 *
 * @IEEE80211_HW_SUPPORTS_CQM_RSSI:
 *	Hardware can do connection quality monitoring - i.e. it can monitor
 *	connection quality related parameters, such as the RSSI level and
 *	provide notifications if configured trigger levels are reached.
 *
 * @IEEE80211_HW_NEED_DTIM_PERIOD:
 *	This device needs to know the DTIM period for the BSS before
 *	associating.
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
 */
enum ieee80211_hw_flags {
	IEEE80211_HW_HAS_RATE_CONTROL			= 1<<0,
	IEEE80211_HW_RX_INCLUDES_FCS			= 1<<1,
	IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING	= 1<<2,
	IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE		= 1<<3,
	IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE	= 1<<4,
	IEEE80211_HW_SIGNAL_UNSPEC			= 1<<5,
	IEEE80211_HW_SIGNAL_DBM				= 1<<6,
	IEEE80211_HW_NEED_DTIM_PERIOD			= 1<<7,
	IEEE80211_HW_SPECTRUM_MGMT			= 1<<8,
	IEEE80211_HW_AMPDU_AGGREGATION			= 1<<9,
	IEEE80211_HW_SUPPORTS_PS			= 1<<10,
	IEEE80211_HW_PS_NULLFUNC_STACK			= 1<<11,
	IEEE80211_HW_SUPPORTS_DYNAMIC_PS		= 1<<12,
	IEEE80211_HW_MFP_CAPABLE			= 1<<13,
	IEEE80211_HW_BEACON_FILTER			= 1<<14,
	IEEE80211_HW_SUPPORTS_STATIC_SMPS		= 1<<15,
	IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS		= 1<<16,
	IEEE80211_HW_SUPPORTS_UAPSD			= 1<<17,
	IEEE80211_HW_REPORTS_TX_ACK_STATUS		= 1<<18,
	IEEE80211_HW_CONNECTION_MONITOR			= 1<<19,
	IEEE80211_HW_SUPPORTS_CQM_RSSI			= 1<<20,
	IEEE80211_HW_SUPPORTS_PER_STA_GTK		= 1<<21,
	IEEE80211_HW_AP_LINK_PS				= 1<<22,
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
 * @channel_change_time: time (in microseconds) it takes to change channels.
 *
 * @max_signal: Maximum value for signal (rssi) in RX information, used
 *     only when @IEEE80211_HW_SIGNAL_UNSPEC or @IEEE80211_HW_SIGNAL_DB
 *
 * @max_listen_interval: max listen interval in units of beacon interval
 *     that HW supports
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
 *
 * @max_rates: maximum number of alternate rate retry stages the hw
 *	can handle.
 * @max_report_rates: maximum number of alternate rate retry stages
 *	the hw can report back.
 * @max_rate_tries: maximum number of tries for each stage
 *
 * @napi_weight: weight used for NAPI polling.  You must specify an
 *	appropriate value here if a napi_poll operation is provided
 *	by your driver.
 *
 * @max_rx_aggregation_subframes: maximum buffer size (number of
 *	sub-frames) to be used for A-MPDU block ack receiver
 *	aggregation.
 *	This is only relevant if the device has restrictions on the
 *	number of subframes, if it relies on mac80211 to do reordering
 *	it shouldn't be set.
 *
 * @max_tx_aggregation_subframes: maximum number of subframes in an
 *	aggregate an HT driver will transmit, used by the peer as a
 *	hint to size its reorder buffer.
 */
struct ieee80211_hw {
	struct ieee80211_conf conf;
	struct wiphy *wiphy;
	const char *rate_control_algorithm;
	void *priv;
	u32 flags;
	unsigned int extra_tx_headroom;
	int channel_change_time;
	int vif_data_size;
	int sta_data_size;
	int napi_weight;
	u16 queues;
	u16 max_listen_interval;
	s8 max_signal;
	u8 max_rates;
	u8 max_report_rates;
	u8 max_rate_tries;
	u8 max_rx_aggregation_subframes;
	u8 max_tx_aggregation_subframes;
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
static inline void SET_IEEE80211_PERM_ADDR(struct ieee80211_hw *hw, u8 *addr)
{
	memcpy(hw->wiphy->perm_addr, addr, ETH_ALEN);
}

static inline struct ieee80211_rate *
ieee80211_get_tx_rate(const struct ieee80211_hw *hw,
		      const struct ieee80211_tx_info *c)
{
	if (WARN_ON(c->control.rates[0].idx < 0))
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
 * Some hardware need to toggle a single shared antenna between WLAN and
 * Bluetooth to facilitate co-existence. These types of hardware set
 * limitations on the use of host controlled dynamic powersave whenever there
 * is simultaneous WLAN and Bluetooth traffic. For these types of hardware, the
 * driver may request temporarily going into full power save, in order to
 * enable toggling the antenna between BT and WLAN. If the driver requests
 * disabling dynamic powersave, the @dynamic_ps_timeout value will be
 * temporarily set to zero until the driver re-enables dynamic powersave.
 *
 * Driver informs U-APSD client support by enabling
 * %IEEE80211_HW_SUPPORTS_UAPSD flag. The mode is configured through the
 * uapsd paramater in conf_tx() operation. Hardware needs to send the QoS
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
 * which will reduce system power consumption. It usuallly works so that
 * the firmware creates a checksum of the beacon but omits all constantly
 * changing elements (TSF, TIM etc). Whenever the checksum changes the
 * beacon is forwarded to the host, otherwise it will be just dropped. That
 * way the host will only receive beacons where some relevant information
 * (for example ERP protection or WMM settings) have changed.
 *
 * Beacon filter support is advertised with the %IEEE80211_HW_BEACON_FILTER
 * hardware capability. The driver needs to enable beacon filter support
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
 * enum ieee80211_filter_flags - hardware filter flags
 *
 * These flags determine what the filter in hardware should be
 * programmed to let through and what should not be passed to the
 * stack. It is always safe to pass more frames than requested,
 * but this has negative impact on power consumption.
 *
 * @FIF_PROMISC_IN_BSS: promiscuous mode within your BSS,
 *	think of the BSS as your network segment and then this corresponds
 *	to the regular ethernet device promiscuous mode.
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
 * @FIF_CONTROL: pass control frames (except for PS Poll), if PROMISC_IN_BSS
 * 	is not set then only those addressed to this station.
 *
 * @FIF_OTHER_BSS: pass frames destined to other BSSes
 *
 * @FIF_PSPOLL: pass PS Poll frames, if PROMISC_IN_BSS is not set then only
 * 	those addressed to this station.
 *
 * @FIF_PROBE_REQ: pass probe request frames
 */
enum ieee80211_filter_flags {
	FIF_PROMISC_IN_BSS	= 1<<0,
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
 * @IEEE80211_AMPDU_RX_START: start Rx aggregation
 * @IEEE80211_AMPDU_RX_STOP: stop Rx aggregation
 * @IEEE80211_AMPDU_TX_START: start Tx aggregation
 * @IEEE80211_AMPDU_TX_STOP: stop Tx aggregation
 * @IEEE80211_AMPDU_TX_OPERATIONAL: TX aggregation has become operational
 */
enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP,
	IEEE80211_AMPDU_TX_OPERATIONAL,
};

/**
 * enum ieee80211_tx_sync_type - TX sync type
 * @IEEE80211_TX_SYNC_AUTH: sync TX for authentication
 *	(and possibly also before direct probe)
 * @IEEE80211_TX_SYNC_ASSOC: sync TX for association
 * @IEEE80211_TX_SYNC_ACTION: sync TX for action frame
 *	(not implemented yet)
 */
enum ieee80211_tx_sync_type {
	IEEE80211_TX_SYNC_AUTH,
	IEEE80211_TX_SYNC_ASSOC,
	IEEE80211_TX_SYNC_ACTION,
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
 *	preferably, never fail and stop queues appropriately, more
 *	importantly, however, it must never fail for A-MPDU-queues.
 *	This function should return NETDEV_TX_OK except in very
 *	limited cases.
 *	Must be implemented and atomic.
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
 * @tx_sync: Called before a frame is sent to an AP/GO. In the GO case, the
 *	driver should sync with the GO's powersaving so the device doesn't
 *	transmit the frame while the GO is asleep. In the regular AP case
 *	it may be used by drivers for devices implementing other restrictions
 *	on talking to APs, e.g. due to regulatory enforcement or just HW
 *	restrictions.
 *	This function is called for every authentication, association and
 *	action frame separately since applications might attempt to auth
 *	with multiple APs before chosing one to associate to. If it returns
 *	an error, the corresponding authentication, association or frame
 *	transmission is aborted and reported as having failed. It is always
 *	called after tuning to the correct channel.
 *	The callback might be called multiple times before @finish_tx_sync
 *	(but @finish_tx_sync will be called once for each) but in practice
 *	this is unlikely to happen. It can also refuse in that case if the
 *	driver cannot handle that situation.
 *	This callback can sleep.
 * @finish_tx_sync: Called as a counterpart to @tx_sync, unless that returned
 *	an error. This callback can sleep.
 *
 * @prepare_multicast: Prepare for multicast filter configuration.
 *	This callback is optional, and its return value is passed
 *	to configure_filter(). This callback must be atomic.
 *
 * @configure_filter: Configure the device's RX filter.
 *	See the section "Frame filtering" for more information.
 *	This callback must be implemented and can sleep.
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
 *
 * @sw_scan_start: Notifier function that is called just before a software scan
 *	is started. Can be NULL, if the driver doesn't need this notification.
 *	The callback can sleep.
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
 * @get_tkip_seq: If your device implements TKIP encryption in hardware this
 *	callback should be provided to read the TKIP transmit IVs (both IV32
 *	and IV16) for the given key from hardware.
 *	The callback must be atomic.
 *
 * @set_frag_threshold: Configuration of fragmentation threshold. Assign this
 *	if the device does fragmentation by itself; if this callback is
 *	implemented then the stack will not do fragmentation.
 *	The callback can sleep.
 *
 * @set_rts_threshold: Configuration of RTS threshold (if device needs it)
 *	The callback can sleep.
 *
 * @sta_add: Notifies low level driver about addition of an associated station,
 *	AP, IBSS/WDS/mesh peer etc. This callback can sleep.
 *
 * @sta_remove: Notifies low level driver about removal of an associated
 *	station, AP, IBSS/WDS/mesh peer etc. This callback can sleep.
 *
 * @sta_notify: Notifies low level driver about power state transition of an
 *	associated station, AP,  IBSS/WDS/mesh peer etc. For a VIF operating
 *	in AP mode, this callback will not be called when the flag
 *	%IEEE80211_HW_AP_LINK_PS is set. Must be atomic.
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
 *      Currently, this is only used for IBSS mode debugging. Is not a
 *	required function.
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
 * @ampdu_action: Perform a certain A-MPDU action
 * 	The RA/TID combination determines the destination and TID we want
 * 	the ampdu action to be performed for. The action is defined through
 * 	ieee80211_ampdu_mlme_action. Starting sequence number (@ssn)
 * 	is the first frame we expect to perform the action on. Notice
 * 	that TX/RX_STOP can pass NULL for this parameter.
 *	The @buf_size parameter is only valid when the action is set to
 *	%IEEE80211_AMPDU_TX_OPERATIONAL and indicates the peer's reorder
 *	buffer size (number of subframes) for this session -- the driver
 *	may neither send aggregates containing more subframes than this
 *	nor send aggregates in a way that lost frames would exceed the
 *	buffer size. If just limiting the aggregate size, this would be
 *	possible with a buf_size of 8:
 *	 - TX: 1.....7
 *	 - RX:  2....7 (lost frame #1)
 *	 - TX:        8..1...
 *	which is invalid since #1 was now re-transmitted well past the
 *	buffer size of 8. Correct ways to retransmit #1 would be:
 *	 - TX:       1 or 18 or 81
 *	Even "189" would be wrong since 1 could be lost again.
 *
 *	Returns a negative error code on failure.
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
 *	accordingly. This callback is not required and may sleep.
 *
 * @testmode_cmd: Implement a cfg80211 test mode command.
 *	The callback can sleep.
 * @testmode_dump: Implement a cfg80211 test mode dump. The callback can sleep.
 *
 * @flush: Flush all pending frames from the hardware queue, making sure
 *	that the hardware queues are empty. If the parameter @drop is set
 *	to %true, pending frames may be dropped. The callback can sleep.
 *
 * @channel_switch: Drivers that need (or want) to offload the channel
 *	switch operation for CSAs received from the AP may implement this
 *	callback. They must then call ieee80211_chswitch_done() to indicate
 *	completion of the channel switch.
 *
 * @napi_poll: Poll Rx queue for incoming data frames.
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
 *	ieee80211_remain_on_channel_expired(). This callback may sleep.
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
 * @rssi_callback: Notify driver when the average RSSI goes above/below
 *	thresholds that were registered previously. The callback can sleep.
 */
struct ieee80211_ops {
	void (*tx)(struct ieee80211_hw *hw, struct sk_buff *skb);
	int (*start)(struct ieee80211_hw *hw);
	void (*stop)(struct ieee80211_hw *hw);
#ifdef CONFIG_PM
	int (*suspend)(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan);
	int (*resume)(struct ieee80211_hw *hw);
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

	int (*tx_sync)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       const u8 *bssid, enum ieee80211_tx_sync_type type);
	void (*finish_tx_sync)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       const u8 *bssid,
			       enum ieee80211_tx_sync_type type);

	u64 (*prepare_multicast)(struct ieee80211_hw *hw,
				 struct netdev_hw_addr_list *mc_list);
	void (*configure_filter)(struct ieee80211_hw *hw,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 u64 multicast);
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
	int (*hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct cfg80211_scan_request *req);
	void (*cancel_hw_scan)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif);
	int (*sched_scan_start)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct cfg80211_sched_scan_request *req,
				struct ieee80211_sched_scan_ies *ies);
	void (*sched_scan_stop)(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif);
	void (*sw_scan_start)(struct ieee80211_hw *hw);
	void (*sw_scan_complete)(struct ieee80211_hw *hw);
	int (*get_stats)(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats);
	void (*get_tkip_seq)(struct ieee80211_hw *hw, u8 hw_key_idx,
			     u32 *iv32, u16 *iv16);
	int (*set_frag_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*set_rts_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
	int (*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
	void (*sta_notify)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			enum sta_notify_cmd, struct ieee80211_sta *sta);
	int (*conf_tx)(struct ieee80211_hw *hw, u16 queue,
		       const struct ieee80211_tx_queue_params *params);
	u64 (*get_tsf)(struct ieee80211_hw *hw);
	void (*set_tsf)(struct ieee80211_hw *hw, u64 tsf);
	void (*reset_tsf)(struct ieee80211_hw *hw);
	int (*tx_last_beacon)(struct ieee80211_hw *hw);
	int (*ampdu_action)(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			    u8 buf_size);
	int (*get_survey)(struct ieee80211_hw *hw, int idx,
		struct survey_info *survey);
	void (*rfkill_poll)(struct ieee80211_hw *hw);
	void (*set_coverage_class)(struct ieee80211_hw *hw, u8 coverage_class);
#ifdef CONFIG_NL80211_TESTMODE
	int (*testmode_cmd)(struct ieee80211_hw *hw, void *data, int len);
	int (*testmode_dump)(struct ieee80211_hw *hw, struct sk_buff *skb,
			     struct netlink_callback *cb,
			     void *data, int len);
#endif
	void (*flush)(struct ieee80211_hw *hw, bool drop);
	void (*channel_switch)(struct ieee80211_hw *hw,
			       struct ieee80211_channel_switch *ch_switch);
	int (*napi_poll)(struct ieee80211_hw *hw, int budget);
	int (*set_antenna)(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant);
	int (*get_antenna)(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant);

	int (*remain_on_channel)(struct ieee80211_hw *hw,
				 struct ieee80211_channel *chan,
				 enum nl80211_channel_type channel_type,
				 int duration);
	int (*cancel_remain_on_channel)(struct ieee80211_hw *hw);
	int (*set_ringparam)(struct ieee80211_hw *hw, u32 tx, u32 rx);
	void (*get_ringparam)(struct ieee80211_hw *hw,
			      u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max);
	bool (*tx_frames_pending)(struct ieee80211_hw *hw);
	int (*set_bitrate_mask)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask);
	void (*rssi_callback)(struct ieee80211_hw *hw,
			      enum ieee80211_rssi_event rssi_event);
};

/**
 * ieee80211_alloc_hw -  Allocate a new hardware device
 *
 * This must be called once for each hardware device. The returned pointer
 * must be used to refer to this device when calling other functions.
 * mac80211 allocates a private data area for the driver pointed to by
 * @priv in &struct ieee80211_hw, the size of this area is given as
 * @priv_data_len.
 *
 * @priv_data_len: length of private data
 * @ops: callbacks for this device
 */
struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops);

/**
 * ieee80211_register_hw - Register hardware device
 *
 * You must call this function before any other functions in
 * mac80211. Note that before a hardware can be registered, you
 * need to fill the contained wiphy's information.
 *
 * @hw: the device to register as returned by ieee80211_alloc_hw()
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
extern char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_create_tpt_led_trigger(
				struct ieee80211_hw *hw, unsigned int flags,
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
 */
static inline char *ieee80211_get_tx_led_name(struct ieee80211_hw *hw)
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
 */
static inline char *ieee80211_get_rx_led_name(struct ieee80211_hw *hw)
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
 */
static inline char *ieee80211_get_assoc_led_name(struct ieee80211_hw *hw)
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
 */
static inline char *ieee80211_get_radio_led_name(struct ieee80211_hw *hw)
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
 * This function returns %NULL (in case of error, or if no LED
 * triggers are configured) or the name of the new trigger.
 * This function must be called before ieee80211_register_hw().
 */
static inline char *
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

/** ieee80211_napi_schedule - schedule NAPI poll
 *
 * Use this function to schedule NAPI polling on a device.
 *
 * @hw: the hardware to start polling
 */
void ieee80211_napi_schedule(struct ieee80211_hw *hw);

/** ieee80211_napi_complete - complete NAPI polling
 *
 * Use this function to finish NAPI polling on a device.
 *
 * @hw: the hardware to stop polling
 */
void ieee80211_napi_complete(struct ieee80211_hw *hw);

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
 * mixed for a single hardware.
 *
 * In process context use instead ieee80211_rx_ni().
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 */
void ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb);

/**
 * ieee80211_rx_irqsafe - receive frame
 *
 * Like ieee80211_rx() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * Calls to this function, ieee80211_rx() or ieee80211_rx_ni() may not
 * be mixed for a single hardware.
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
 * not be mixed for a single hardware.
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
 * The function returns -EINVAL when the requested PS mode is already set.
 *
 * @sta: currently connected sta
 * @start: start or stop PS
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

/*
 * The TX headroom reserved by mac80211 for its own tx_status functions.
 * This is enough for the radiotap header.
 */
#define IEEE80211_TX_STATUS_HEADROOM	13

/**
 * ieee80211_sta_set_tim - set the TIM bit for a sleeping station
 * @sta: &struct ieee80211_sta pointer for the sleeping station
 *
 * If a driver buffers frames for a powersave station instead of passing
 * them back to mac80211 for retransmission, the station needs to be told
 * to wake up using the TIM bitmap in the beacon.
 *
 * This function sets the station's TIM bit - it will be cleared when the
 * station wakes up.
 */
void ieee80211_sta_set_tim(struct ieee80211_sta *sta);

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
 * may not be mixed for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
void ieee80211_tx_status(struct ieee80211_hw *hw,
			 struct sk_buff *skb);

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
 * obtain the beacon frame/template.
 *
 * If the beacon frames are generated by the host system (i.e., not in
 * hardware/firmware), the driver uses this function to get each beacon
 * frame from mac80211 -- it is responsible for calling this function
 * before the beacon is needed (e.g. based on hardware interrupt).
 *
 * If the beacon frames are generated by the device, then the driver
 * must use the returned beacon as the template and change the TIM IE
 * according to the current DTIM parameters/TIM bitmap.
 *
 * The driver is responsible for freeing the returned skb.
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
 */
static inline struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
						   struct ieee80211_vif *vif)
{
	return ieee80211_beacon_get_tim(hw, vif, NULL, NULL);
}

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
 */
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif);

/**
 * ieee80211_nullfunc_get - retrieve a nullfunc template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Creates a Nullfunc template which can, for example, uploaded to
 * hardware. The template must be updated after association so that correct
 * BSSID and address is used.
 *
 * Note: Caller (or hardware) is responsible for setting the
 * &IEEE80211_FCTL_PM bit as well as Duration and Sequence Control fields.
 */
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif);

/**
 * ieee80211_probereq_get - retrieve a Probe Request template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @ssid: SSID buffer
 * @ssid_len: length of SSID
 * @ie: buffer containing all IEs except SSID for the template
 * @ie_len: length of the IE buffer
 *
 * Creates a Probe Request template which can, for example, be uploaded to
 * hardware.
 */
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       const u8 *ie, size_t ie_len);

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
 */
__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    size_t frame_len,
				    const struct ieee80211_tx_info *frame_txctl);

/**
 * ieee80211_generic_frame_duration - Calculate the duration field for a frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @frame_len: the length of the frame.
 * @rate: the rate at which the frame is going to be transmitted.
 *
 * Calculate the duration field of some generic frame, given its
 * length and transmission rate (in 100kbps).
 */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
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
 * frame. In most cases, this is used when generating beacon frame. This
 * function returns a pointer to the next buffered skb or NULL if no more
 * buffered frames are available.
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
 * struct ieee80211_key_seq - key sequence counter
 *
 * @tkip: TKIP data, containing IV32 and IV16 in host byte order
 * @ccmp: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
 * @aes_cmac: PN data, most significant byte first (big endian,
 *	reverse order than in packet)
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
	};
};

/**
 * ieee80211_get_key_tx_seq - get key TX sequence counter
 *
 * @keyconf: the parameter passed with the set key
 * @seq: buffer to receive the sequence data
 *
 * This function allows a driver to retrieve the current TX IV/PN
 * for the given key. It must not be called if IV generation is
 * offloaded to the device.
 *
 * Note that this function may only be called when no TX processing
 * can be done concurrently, for example when queues are stopped
 * and the stop has been synchronized.
 */
void ieee80211_get_key_tx_seq(struct ieee80211_key_conf *keyconf,
			      struct ieee80211_key_seq *seq);

/**
 * ieee80211_get_key_rx_seq - get key RX sequence counter
 *
 * @keyconf: the parameter passed with the set key
 * @tid: The TID, or -1 for the management frame value (CCMP only);
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
 * @aborted: set to true if scan was aborted
 */
void ieee80211_scan_completed(struct ieee80211_hw *hw, bool aborted);

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
 * ieee80211_iterate_active_interfaces - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This function allows the iterator function to sleep, when the iterator
 * function is atomic @ieee80211_iterate_active_interfaces_atomic can
 * be used.
 * Does not iterate over a new interface during add_interface()
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iterator: the iterator function to call
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_active_interfaces(struct ieee80211_hw *hw,
					 void (*iterator)(void *data, u8 *mac,
						struct ieee80211_vif *vif),
					 void *data);

/**
 * ieee80211_iterate_active_interfaces_atomic - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This function requires the iterator callback function to be atomic,
 * if that is not desired, use @ieee80211_iterate_active_interfaces instead.
 * Does not iterate over a new interface during add_interface()
 *
 * @hw: the hardware struct of which the interfaces should be iterated over
 * @iterator: the iterator function to call, cannot sleep
 * @data: first argument of the iterator function
 */
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
						void (*iterator)(void *data,
						    u8 *mac,
						    struct ieee80211_vif *vif),
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
 * This function must be called under RCU lock and the
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
 * This function must be called under RCU lock and the
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
 * ieee80211_ap_probereq_get - retrieve a Probe Request template
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Creates a Probe Request template which can, for example, be uploaded to
 * hardware. The template is filled with bssid, ssid and supported rate
 * information. This function must only be called from within the
 * .bss_info_changed callback function and only in managed mode. The function
 * is only useful when the interface is associated, otherwise it will return
 * NULL.
 */
struct sk_buff *ieee80211_ap_probereq_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif);

/**
 * ieee80211_beacon_loss - inform hardware does not receive beacons
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * When beacon filtering is enabled with %IEEE80211_HW_BEACON_FILTER and
 * %IEEE80211_CONF_PS is set, the driver needs to inform whenever the
 * hardware is not receiving beacons with this function.
 */
void ieee80211_beacon_loss(struct ieee80211_vif *vif);

/**
 * ieee80211_connection_loss - inform hardware has lost connection to the AP
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * When beacon filtering is enabled with %IEEE80211_HW_BEACON_FILTER, and
 * %IEEE80211_CONF_PS and %IEEE80211_HW_CONNECTION_MONITOR are set, the driver
 * needs to inform if the connection to the AP has been lost.
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
 * ieee80211_disable_dyn_ps - force mac80211 to temporarily disable dynamic psm
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * Some hardware require full power save to manage simultaneous BT traffic
 * on the WLAN frequency. Full PSM is required periodically, whenever there are
 * burst of BT traffic. The hardware gets information of BT traffic via
 * hardware co-existence lines, and consequentially requests mac80211 to
 * (temporarily) enter full psm.
 * This function will only temporarily disable dynamic PS, not enable PSM if
 * it was not already enabled.
 * The driver must make sure to re-enable dynamic PS using
 * ieee80211_enable_dyn_ps() if the driver has disabled it.
 *
 */
void ieee80211_disable_dyn_ps(struct ieee80211_vif *vif);

/**
 * ieee80211_enable_dyn_ps - restore dynamic psm after being disabled
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * This function restores dynamic PS after being temporarily disabled via
 * ieee80211_disable_dyn_ps(). Each ieee80211_disable_dyn_ps() call must
 * be coupled with an eventual call to this function.
 *
 */
void ieee80211_enable_dyn_ps(struct ieee80211_vif *vif);

/**
 * ieee80211_cqm_rssi_notify - inform a configured connection quality monitoring
 *	rssi threshold triggered
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 * @rssi_event: the RSSI trigger event type
 * @gfp: context flags
 *
 * When the %IEEE80211_HW_SUPPORTS_CQM_RSSI is set, and a connection quality
 * monitoring is configured with an rssi threshold, the driver will inform
 * whenever the rssi level reaches the threshold.
 */
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       gfp_t gfp);

/**
 * ieee80211_get_operstate - get the operstate of the vif
 *
 * @vif: &struct ieee80211_vif pointer from the add_interface callback.
 *
 * The driver might need to know the operstate of the net_device
 * (specifically, whether the link is IF_OPER_UP after resume)
 */
unsigned char ieee80211_get_operstate(struct ieee80211_vif *vif);

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
 * ieee80211_key_removed - disable hw acceleration for key
 * @key_conf: The key hw acceleration should be disabled for
 *
 * This allows drivers to indicate that the given key has been
 * removed from hardware acceleration, due to a new key that
 * was added. Don't use this if the key can continue to be used
 * for TX, if the key restriction is on RX only it is permitted
 * to keep the key for TX only and not call this function.
 *
 * Due to locking constraints, it may only be called during
 * @set_key. This function must be allowed to sleep, and the
 * key it tries to disable may still be used until it returns.
 */
void ieee80211_key_removed(struct ieee80211_key_conf *key_conf);

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

/* Rate control API */

/**
 * enum rate_control_changed - flags to indicate which parameter changed
 *
 * @IEEE80211_RC_HT_CHANGED: The HT parameters of the operating channel have
 *	changed, rate control algorithm can update its internal state if needed.
 */
enum rate_control_changed {
	IEEE80211_RC_HT_CHANGED = BIT(0)
};

/**
 * struct ieee80211_tx_rate_control - rate control information for/from RC algo
 *
 * @hw: The hardware the algorithm is invoked for.
 * @sband: The band this frame is being transmitted on.
 * @bss_conf: the current BSS configuration
 * @reported_rate: The rate control algorithm can fill this in to indicate
 *	which rate should be reported to userspace as the current rate and
 *	used for rate calculations in the mesh network.
 * @rts: whether RTS will be used for this frame because it is longer than the
 *	RTS threshold
 * @short_preamble: whether mac80211 will request short-preamble transmission
 *	if the selected rate supports it
 * @max_rate_idx: user-requested maximum rate (not MCS for now)
 *	(deprecated; this will be removed once drivers get updated to use
 *	rate_idx_mask)
 * @rate_idx_mask: user-requested rate mask (not MCS for now)
 * @skb: the skb that will be transmitted, the control information in it needs
 *	to be filled in
 * @bss: whether this frame is sent out in AP or IBSS mode
 */
struct ieee80211_tx_rate_control {
	struct ieee80211_hw *hw;
	struct ieee80211_supported_band *sband;
	struct ieee80211_bss_conf *bss_conf;
	struct sk_buff *skb;
	struct ieee80211_tx_rate reported_rate;
	bool rts, short_preamble;
	u8 max_rate_idx;
	u32 rate_idx_mask;
	bool bss;
};

struct rate_control_ops {
	struct module *module;
	const char *name;
	void *(*alloc)(struct ieee80211_hw *hw, struct dentry *debugfsdir);
	void (*free)(void *priv);

	void *(*alloc_sta)(void *priv, struct ieee80211_sta *sta, gfp_t gfp);
	void (*rate_init)(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta);
	void (*rate_update)(void *priv, struct ieee80211_supported_band *sband,
			    struct ieee80211_sta *sta,
			    void *priv_sta, u32 changed,
			    enum nl80211_channel_type oper_chan_type);
	void (*free_sta)(void *priv, struct ieee80211_sta *sta,
			 void *priv_sta);

	void (*tx_status)(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb);
	void (*get_rate)(void *priv, struct ieee80211_sta *sta, void *priv_sta,
			 struct ieee80211_tx_rate_control *txrc);

	void (*add_sta_debugfs)(void *priv, void *priv_sta,
				struct dentry *dir);
	void (*remove_sta_debugfs)(void *priv, void *priv_sta);
};

static inline int rate_supported(struct ieee80211_sta *sta,
				 enum ieee80211_band band,
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
	WARN_ON(1);

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

int ieee80211_rate_control_register(struct rate_control_ops *ops);
void ieee80211_rate_control_unregister(struct rate_control_ops *ops);

static inline bool
conf_is_ht20(struct ieee80211_conf *conf)
{
	return conf->channel_type == NL80211_CHAN_HT20;
}

static inline bool
conf_is_ht40_minus(struct ieee80211_conf *conf)
{
	return conf->channel_type == NL80211_CHAN_HT40MINUS;
}

static inline bool
conf_is_ht40_plus(struct ieee80211_conf *conf)
{
	return conf->channel_type == NL80211_CHAN_HT40PLUS;
}

static inline bool
conf_is_ht40(struct ieee80211_conf *conf)
{
	return conf_is_ht40_minus(conf) || conf_is_ht40_plus(conf);
}

static inline bool
conf_is_ht(struct ieee80211_conf *conf)
{
	return conf->channel_type != NL80211_CHAN_NO_HT;
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

void ieee80211_enable_rssi_reports(struct ieee80211_vif *vif,
				   int rssi_min_thold,
				   int rssi_max_thold);

void ieee80211_disable_rssi_reports(struct ieee80211_vif *vif);
#endif /* MAC80211_H */
