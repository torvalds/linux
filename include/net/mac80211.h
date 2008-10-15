/*
 * mac80211 <-> driver interface
 *
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
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
#include <linux/wireless.h>
#include <linux/device.h>
#include <linux/ieee80211.h>
#include <net/wireless.h>
#include <net/cfg80211.h>

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
 * enum ieee80211_notification_type - Low level driver notification
 * @IEEE80211_NOTIFY_RE_ASSOC: start the re-association sequence
 */
enum ieee80211_notification_types {
	IEEE80211_NOTIFY_RE_ASSOC,
};

/**
 * struct ieee80211_ht_bss_info - describing BSS's HT characteristics
 *
 * This structure describes most essential parameters needed
 * to describe 802.11n HT characteristics in a BSS.
 *
 * @primary_channel: channel number of primery channel
 * @bss_cap: 802.11n's general BSS capabilities (e.g. channel width)
 * @bss_op_mode: 802.11n's BSS operation modes (e.g. HT protection)
 */
struct ieee80211_ht_bss_info {
	u8 primary_channel;
	u8 bss_cap;  /* use IEEE80211_HT_IE_CHA_ */
	u8 bss_op_mode; /* use IEEE80211_HT_IE_ */
};

/**
 * enum ieee80211_max_queues - maximum number of queues
 *
 * @IEEE80211_MAX_QUEUES: Maximum number of regular device queues.
 * @IEEE80211_MAX_AMPDU_QUEUES: Maximum number of queues usable
 *	for A-MPDU operation.
 */
enum ieee80211_max_queues {
	IEEE80211_MAX_QUEUES =		16,
	IEEE80211_MAX_AMPDU_QUEUES =	16,
};

/**
 * struct ieee80211_tx_queue_params - transmit queue configuration
 *
 * The information provided in this structure is required for QoS
 * transmit queue configuration. Cf. IEEE 802.11 7.3.2.29.
 *
 * @aifs: arbitration interface space [0..255]
 * @cw_min: minimum contention window [a value of the form
 *	2^n-1 in the range 1..32767]
 * @cw_max: maximum contention window [like @cw_min]
 * @txop: maximum burst time in units of 32 usecs, 0 meaning disabled
 */
struct ieee80211_tx_queue_params {
	u16 txop;
	u16 cw_min;
	u16 cw_max;
	u8 aifs;
};

/**
 * struct ieee80211_tx_queue_stats - transmit queue statistics
 *
 * @len: number of packets in queue
 * @limit: queue length limit
 * @count: number of frames sent
 */
struct ieee80211_tx_queue_stats {
	unsigned int len;
	unsigned int limit;
	unsigned int count;
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
 */
enum ieee80211_bss_change {
	BSS_CHANGED_ASSOC		= 1<<0,
	BSS_CHANGED_ERP_CTS_PROT	= 1<<1,
	BSS_CHANGED_ERP_PREAMBLE	= 1<<2,
	BSS_CHANGED_ERP_SLOT		= 1<<3,
	BSS_CHANGED_HT                  = 1<<4,
	BSS_CHANGED_BASIC_RATES		= 1<<5,
};

/**
 * struct ieee80211_bss_conf - holds the BSS's changing parameters
 *
 * This structure keeps information about a BSS (and an association
 * to that BSS) that can change during the lifetime of the BSS.
 *
 * @assoc: association status
 * @aid: association ID number, valid only when @assoc is true
 * @use_cts_prot: use CTS protection
 * @use_short_preamble: use 802.11b short preamble
 * @use_short_slot: use short slot time (only relevant for ERP)
 * @dtim_period: num of beacons before the next DTIM, for PSM
 * @timestamp: beacon timestamp
 * @beacon_int: beacon interval
 * @assoc_capability: capabilities taken from assoc resp
 * @assoc_ht: association in HT mode
 * @ht_conf: ht capabilities
 * @ht_bss_conf: ht extended capabilities
 * @basic_rates: bitmap of basic rates, each bit stands for an
 *	index into the rate table configured by the driver in
 *	the current band.
 */
struct ieee80211_bss_conf {
	/* association related data */
	bool assoc;
	u16 aid;
	/* erp related data */
	bool use_cts_prot;
	bool use_short_preamble;
	bool use_short_slot;
	u8 dtim_period;
	u16 beacon_int;
	u16 assoc_capability;
	u64 timestamp;
	u64 basic_rates;
	/* ht related data */
	bool assoc_ht;
	struct ieee80211_ht_info *ht_conf;
	struct ieee80211_ht_bss_info *ht_bss_conf;
};

/**
 * enum mac80211_tx_control_flags - flags to describe transmission information/status
 *
 * These flags are used with the @flags member of &ieee80211_tx_info.
 *
 * @IEEE80211_TX_CTL_REQ_TX_STATUS: request TX status callback for this frame.
 * @IEEE80211_TX_CTL_USE_RTS_CTS: use RTS-CTS before sending frame
 * @IEEE80211_TX_CTL_USE_CTS_PROTECT: use CTS protection for the frame (e.g.,
 *	for combined 802.11g / 802.11b networks)
 * @IEEE80211_TX_CTL_NO_ACK: tell the low level not to wait for an ack
 * @IEEE80211_TX_CTL_RATE_CTRL_PROBE: TBD
 * @IEEE80211_TX_CTL_CLEAR_PS_FILT: clear powersave filter for destination
 *	station
 * @IEEE80211_TX_CTL_REQUEUE: TBD
 * @IEEE80211_TX_CTL_FIRST_FRAGMENT: this is a first fragment of the frame
 * @IEEE80211_TX_CTL_SHORT_PREAMBLE: TBD
 * @IEEE80211_TX_CTL_LONG_RETRY_LIMIT: this frame should be send using the
 *	through set_retry_limit configured long retry value
 * @IEEE80211_TX_CTL_SEND_AFTER_DTIM: send this frame after DTIM beacon
 * @IEEE80211_TX_CTL_AMPDU: this frame should be sent as part of an A-MPDU
 * @IEEE80211_TX_CTL_OFDM_HT: this frame can be sent in HT OFDM rates. number
 *	of streams when this flag is on can be extracted from antenna_sel_tx,
 *	so if 1 antenna is marked use SISO, 2 antennas marked use MIMO, n
 *	antennas marked use MIMO_n.
 * @IEEE80211_TX_CTL_GREEN_FIELD: use green field protection for this frame
 * @IEEE80211_TX_CTL_40_MHZ_WIDTH: send this frame using 40 Mhz channel width
 * @IEEE80211_TX_CTL_DUP_DATA: duplicate data frame on both 20 Mhz channels
 * @IEEE80211_TX_CTL_SHORT_GI: send this frame using short guard interval
 * @IEEE80211_TX_CTL_INJECTED: TBD
 * @IEEE80211_TX_STAT_TX_FILTERED: The frame was not transmitted
 *	because the destination STA was in powersave mode.
 * @IEEE80211_TX_STAT_ACK: Frame was acknowledged
 * @IEEE80211_TX_STAT_AMPDU: The frame was aggregated, so status
 * 	is for the whole aggregation.
 * @IEEE80211_TX_STAT_AMPDU_NO_BACK: no block ack was returned,
 * 	so consider using block ack request (BAR).
 * @IEEE80211_TX_CTL_ASSIGN_SEQ: The driver has to assign a sequence
 *	number to this frame, taking care of not overwriting the fragment
 *	number and increasing the sequence number only when the
 *	IEEE80211_TX_CTL_FIRST_FRAGMENT flags is set. mac80211 will properly
 *	assign sequence numbers to QoS-data frames but cannot do so correctly
 *	for non-QoS-data and management frames because beacons need them from
 *	that counter as well and mac80211 cannot guarantee proper sequencing.
 *	If this flag is set, the driver should instruct the hardware to
 *	assign a sequence number to the frame or assign one itself. Cf. IEEE
 *	802.11-2007 7.1.3.4.1 paragraph 3. This flag will always be set for
 *	beacons always be clear for frames without a sequence number field.
 */
enum mac80211_tx_control_flags {
	IEEE80211_TX_CTL_REQ_TX_STATUS		= BIT(0),
	IEEE80211_TX_CTL_USE_RTS_CTS		= BIT(2),
	IEEE80211_TX_CTL_USE_CTS_PROTECT	= BIT(3),
	IEEE80211_TX_CTL_NO_ACK			= BIT(4),
	IEEE80211_TX_CTL_RATE_CTRL_PROBE	= BIT(5),
	IEEE80211_TX_CTL_CLEAR_PS_FILT		= BIT(6),
	IEEE80211_TX_CTL_REQUEUE		= BIT(7),
	IEEE80211_TX_CTL_FIRST_FRAGMENT		= BIT(8),
	IEEE80211_TX_CTL_SHORT_PREAMBLE		= BIT(9),
	IEEE80211_TX_CTL_LONG_RETRY_LIMIT	= BIT(10),
	IEEE80211_TX_CTL_SEND_AFTER_DTIM	= BIT(12),
	IEEE80211_TX_CTL_AMPDU			= BIT(13),
	IEEE80211_TX_CTL_OFDM_HT		= BIT(14),
	IEEE80211_TX_CTL_GREEN_FIELD		= BIT(15),
	IEEE80211_TX_CTL_40_MHZ_WIDTH		= BIT(16),
	IEEE80211_TX_CTL_DUP_DATA		= BIT(17),
	IEEE80211_TX_CTL_SHORT_GI		= BIT(18),
	IEEE80211_TX_CTL_INJECTED		= BIT(19),
	IEEE80211_TX_STAT_TX_FILTERED		= BIT(20),
	IEEE80211_TX_STAT_ACK			= BIT(21),
	IEEE80211_TX_STAT_AMPDU			= BIT(22),
	IEEE80211_TX_STAT_AMPDU_NO_BACK		= BIT(23),
	IEEE80211_TX_CTL_ASSIGN_SEQ		= BIT(24),
};


#define IEEE80211_TX_INFO_DRIVER_DATA_SIZE \
	(sizeof(((struct sk_buff *)0)->cb) - 8)
#define IEEE80211_TX_INFO_DRIVER_DATA_PTRS \
	(IEEE80211_TX_INFO_DRIVER_DATA_SIZE / sizeof(void *))

/* maximum number of alternate rate retry stages */
#define IEEE80211_TX_MAX_ALTRATE	3

/**
 * struct ieee80211_tx_altrate - alternate rate selection/status
 *
 * @rate_idx: rate index to attempt to send with
 * @limit: number of retries before fallback
 */
struct ieee80211_tx_altrate {
	s8 rate_idx;
	u8 limit;
};

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
 * @band: TBD
 * @tx_rate_idx: TBD
 * @antenna_sel_tx: TBD
 * @control: union for control data
 * @status: union for status data
 * @driver_data: array of driver_data pointers
 * @retry_count: number of retries
 * @excessive_retries: set to 1 if the frame was retried many times
 *	but not acknowledged
 * @ampdu_ack_len: number of aggregated frames.
 * 	relevant only if IEEE80211_TX_STATUS_AMPDU was set.
 * @ampdu_ack_map: block ack bit map for the aggregation.
 * 	relevant only if IEEE80211_TX_STATUS_AMPDU was set.
 * @ack_signal: signal strength of the ACK frame
 */
struct ieee80211_tx_info {
	/* common information */
	u32 flags;
	u8 band;
	s8 tx_rate_idx;
	u8 antenna_sel_tx;

	/* 1 byte hole */

	union {
		struct {
			/* NB: vif can be NULL for injected frames */
			struct ieee80211_vif *vif;
			struct ieee80211_key_conf *hw_key;
			struct ieee80211_sta *sta;
			unsigned long jiffies;
			s8 rts_cts_rate_idx;
			u8 retry_limit;
			struct ieee80211_tx_altrate retries[IEEE80211_TX_MAX_ALTRATE];
		} control;
		struct {
			u64 ampdu_ack_map;
			int ack_signal;
			struct ieee80211_tx_altrate retries[IEEE80211_TX_MAX_ALTRATE + 1];
			u8 retry_count;
			bool excessive_retries;
			u8 ampdu_ack_len;
		} status;
		void *driver_data[IEEE80211_TX_INFO_DRIVER_DATA_PTRS];
	};
};

static inline struct ieee80211_tx_info *IEEE80211_SKB_CB(struct sk_buff *skb)
{
	return (struct ieee80211_tx_info *)skb->cb;
}


/**
 * enum mac80211_rx_flags - receive flags
 *
 * These flags are used with the @flag member of &struct ieee80211_rx_status.
 * @RX_FLAG_MMIC_ERROR: Michael MIC error was reported on this frame.
 *	Use together with %RX_FLAG_MMIC_STRIPPED.
 * @RX_FLAG_DECRYPTED: This frame was decrypted in hardware.
 * @RX_FLAG_RADIOTAP: This frame starts with a radiotap header.
 * @RX_FLAG_MMIC_STRIPPED: the Michael MIC is stripped off this frame,
 *	verification has been done by the hardware.
 * @RX_FLAG_IV_STRIPPED: The IV/ICV are stripped from this frame.
 *	If this flag is set, the stack cannot do any replay detection
 *	hence the driver or hardware will have to do that.
 * @RX_FLAG_FAILED_FCS_CRC: Set this flag if the FCS check failed on
 *	the frame.
 * @RX_FLAG_FAILED_PLCP_CRC: Set this flag if the PCLP check failed on
 *	the frame.
 * @RX_FLAG_TSFT: The timestamp passed in the RX status (@mactime field)
 *	is valid. This is useful in monitor mode and necessary for beacon frames
 *	to enable IBSS merging.
 * @RX_FLAG_SHORTPRE: Short preamble was used for this frame
 */
enum mac80211_rx_flags {
	RX_FLAG_MMIC_ERROR	= 1<<0,
	RX_FLAG_DECRYPTED	= 1<<1,
	RX_FLAG_RADIOTAP	= 1<<2,
	RX_FLAG_MMIC_STRIPPED	= 1<<3,
	RX_FLAG_IV_STRIPPED	= 1<<4,
	RX_FLAG_FAILED_FCS_CRC	= 1<<5,
	RX_FLAG_FAILED_PLCP_CRC = 1<<6,
	RX_FLAG_TSFT		= 1<<7,
	RX_FLAG_SHORTPRE	= 1<<8
};

/**
 * struct ieee80211_rx_status - receive status
 *
 * The low-level driver should provide this information (the subset
 * supported by hardware) to the 802.11 code with each received
 * frame.
 *
 * @mactime: value in microseconds of the 64-bit Time Synchronization Function
 * 	(TSF) timer when the first data symbol (MPDU) arrived at the hardware.
 * @band: the active band when this frame was received
 * @freq: frequency the radio was tuned to when receiving this frame, in MHz
 * @signal: signal strength when receiving this frame, either in dBm, in dB or
 *	unspecified depending on the hardware capabilities flags
 *	@IEEE80211_HW_SIGNAL_*
 * @noise: noise when receiving this frame, in dBm.
 * @qual: overall signal quality indication, in percent (0-100).
 * @antenna: antenna used
 * @rate_idx: index of data rate into band's supported rates
 * @flag: %RX_FLAG_*
 */
struct ieee80211_rx_status {
	u64 mactime;
	enum ieee80211_band band;
	int freq;
	int signal;
	int noise;
	int qual;
	int antenna;
	int rate_idx;
	int flag;
};

/**
 * enum ieee80211_conf_flags - configuration flags
 *
 * Flags to define PHY configuration options
 *
 * @IEEE80211_CONF_SHORT_SLOT_TIME: use 802.11g short slot time
 * @IEEE80211_CONF_RADIOTAP: add radiotap header at receive time (if supported)
 * @IEEE80211_CONF_SUPPORT_HT_MODE: use 802.11n HT capabilities (if supported)
 * @IEEE80211_CONF_PS: Enable 802.11 power save mode
 */
enum ieee80211_conf_flags {
	/*
	 * TODO: IEEE80211_CONF_SHORT_SLOT_TIME will be removed once drivers
	 * have been converted to use bss_info_changed() for slot time
	 * configuration
	 */
	IEEE80211_CONF_SHORT_SLOT_TIME	= (1<<0),
	IEEE80211_CONF_RADIOTAP		= (1<<1),
	IEEE80211_CONF_SUPPORT_HT_MODE	= (1<<2),
	IEEE80211_CONF_PS		= (1<<3),
};

/**
 * struct ieee80211_conf - configuration of the device
 *
 * This struct indicates how the driver shall configure the hardware.
 *
 * @radio_enabled: when zero, driver is required to switch off the radio.
 *	TODO make a flag
 * @beacon_int: beacon interval (TODO make interface config)
 * @listen_interval: listen interval in units of beacon interval
 * @flags: configuration flags defined above
 * @power_level: requested transmit power (in dBm)
 * @max_antenna_gain: maximum antenna gain (in dBi)
 * @antenna_sel_tx: transmit antenna selection, 0: default/diversity,
 *	1/2: antenna 0/1
 * @antenna_sel_rx: receive antenna selection, like @antenna_sel_tx
 * @ht_conf: describes current self configuration of 802.11n HT capabilies
 * @ht_bss_conf: describes current BSS configuration of 802.11n HT parameters
 * @channel: the channel to tune to
 */
struct ieee80211_conf {
	int radio_enabled;

	int beacon_int;
	u16 listen_interval;
	u32 flags;
	int power_level;
	int max_antenna_gain;
	u8 antenna_sel_tx;
	u8 antenna_sel_rx;

	struct ieee80211_channel *channel;

	struct ieee80211_ht_info ht_conf;
	struct ieee80211_ht_bss_info ht_bss_conf;
};

/**
 * struct ieee80211_vif - per-interface data
 *
 * Data in this structure is continually present for driver
 * use during the life of a virtual interface.
 *
 * @type: type of this virtual interface
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *).
 */
struct ieee80211_vif {
	enum nl80211_iftype type;
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
 * struct ieee80211_if_init_conf - initial configuration of an interface
 *
 * @vif: pointer to a driver-use per-interface structure. The pointer
 *	itself is also used for various functions including
 *	ieee80211_beacon_get() and ieee80211_get_buffered_bc().
 * @type: one of &enum nl80211_iftype constants. Determines the type of
 *	added/removed interface.
 * @mac_addr: pointer to MAC address of the interface. This pointer is valid
 *	until the interface is removed (i.e. it cannot be used after
 *	remove_interface() callback was called for this interface).
 *
 * This structure is used in add_interface() and remove_interface()
 * callbacks of &struct ieee80211_hw.
 *
 * When you allow multiple interfaces to be added to your PHY, take care
 * that the hardware can actually handle multiple MAC addresses. However,
 * also take care that when there's no interface left with mac_addr != %NULL
 * you remove the MAC address from the device to avoid acknowledging packets
 * in pure monitor mode.
 */
struct ieee80211_if_init_conf {
	enum nl80211_iftype type;
	struct ieee80211_vif *vif;
	void *mac_addr;
};

/**
 * enum ieee80211_if_conf_change - interface config change flags
 *
 * @IEEE80211_IFCC_BSSID: The BSSID changed.
 * @IEEE80211_IFCC_SSID: The SSID changed.
 * @IEEE80211_IFCC_BEACON: The beacon for this interface changed
 *	(currently AP and MESH only), use ieee80211_beacon_get().
 */
enum ieee80211_if_conf_change {
	IEEE80211_IFCC_BSSID	= BIT(0),
	IEEE80211_IFCC_SSID	= BIT(1),
	IEEE80211_IFCC_BEACON	= BIT(2),
};

/**
 * struct ieee80211_if_conf - configuration of an interface
 *
 * @changed: parameters that have changed, see &enum ieee80211_if_conf_change.
 * @bssid: BSSID of the network we are associated to/creating.
 * @ssid: used (together with @ssid_len) by drivers for hardware that
 *	generate beacons independently. The pointer is valid only during the
 *	config_interface() call, so copy the value somewhere if you need
 *	it.
 * @ssid_len: length of the @ssid field.
 *
 * This structure is passed to the config_interface() callback of
 * &struct ieee80211_hw.
 */
struct ieee80211_if_conf {
	u32 changed;
	u8 *bssid;
	u8 *ssid;
	size_t ssid_len;
};

/**
 * enum ieee80211_key_alg - key algorithm
 * @ALG_WEP: WEP40 or WEP104
 * @ALG_TKIP: TKIP
 * @ALG_CCMP: CCMP (AES)
 */
enum ieee80211_key_alg {
	ALG_WEP,
	ALG_TKIP,
	ALG_CCMP,
};

/**
 * enum ieee80211_key_len - key length
 * @LEN_WEP40: WEP 5-byte long key
 * @LEN_WEP104: WEP 13-byte long key
 */
enum ieee80211_key_len {
	LEN_WEP40 = 5,
	LEN_WEP104 = 13,
};

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
 */
enum ieee80211_key_flags {
	IEEE80211_KEY_FLAG_WMM_STA	= 1<<0,
	IEEE80211_KEY_FLAG_GENERATE_IV	= 1<<1,
	IEEE80211_KEY_FLAG_GENERATE_MMIC= 1<<2,
	IEEE80211_KEY_FLAG_PAIRWISE	= 1<<3,
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
 * @alg: The key algorithm.
 * @flags: key flags, see &enum ieee80211_key_flags.
 * @keyidx: the key index (0-3)
 * @keylen: key material length
 * @key: key material. For ALG_TKIP the key is encoded as a 256-bit (32 byte)
 * 	data block:
 * 	- Temporal Encryption Key (128 bits)
 * 	- Temporal Authenticator Tx MIC Key (64 bits)
 * 	- Temporal Authenticator Rx MIC Key (64 bits)
 *
 */
struct ieee80211_key_conf {
	enum ieee80211_key_alg alg;
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
 * call to your sta_notify callback that removed it.
 *
 * @addr: MAC address
 * @aid: AID we assigned to the station if we're an AP
 * @supp_rates: Bitmap of supported rates (per band)
 * @ht_info: HT capabilities of this STA
 * @drv_priv: data area for driver use, will always be aligned to
 *	sizeof(void *), size is determined in hw information.
 */
struct ieee80211_sta {
	u64 supp_rates[IEEE80211_NUM_BANDS];
	u8 addr[ETH_ALEN];
	u16 aid;
	struct ieee80211_ht_info ht_info;

	/* must be last */
	u8 drv_priv[0] __attribute__((__aligned__(sizeof(void *))));
};

/**
 * enum sta_notify_cmd - sta notify command
 *
 * Used with the sta_notify() callback in &struct ieee80211_ops, this
 * indicates addition and removal of a station to station table.
 *
 * @STA_NOTIFY_ADD: a station was added to the station table
 * @STA_NOTIFY_REMOVE: a station being removed from the station table
 */
enum sta_notify_cmd {
	STA_NOTIFY_ADD, STA_NOTIFY_REMOVE
};

/**
 * enum ieee80211_tkip_key_type - get tkip key
 *
 * Used by drivers which need to get a tkip key for skb. Some drivers need a
 * phase 1 key, others need a phase 2 key. A single function allows the driver
 * to get the key, this enum indicates what type of key is required.
 *
 * @IEEE80211_TKIP_P1_KEY: the driver needs a phase 1 key
 * @IEEE80211_TKIP_P2_KEY: the driver needs a phase 2 key
 */
enum ieee80211_tkip_key_type {
	IEEE80211_TKIP_P1_KEY,
	IEEE80211_TKIP_P2_KEY,
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
 * @IEEE80211_HW_SIGNAL_DB:
 *	Hardware gives signal values in dB, decibel difference from an
 *	arbitrary, fixed reference. We expect values between 0 and @max_signal.
 *	If possible please provide dBm instead.
 *
 * @IEEE80211_HW_SIGNAL_DBM:
 *	Hardware gives signal values in dBm, decibel difference from
 *	one milliwatt. This is the preferred method since it is standardized
 *	between different devices. @max_signal does not need to be set.
 *
 * @IEEE80211_HW_NOISE_DBM:
 *	Hardware can provide noise (radio interference) values in units dBm,
 *      decibel difference from one milliwatt.
 *
 * @IEEE80211_HW_SPECTRUM_MGMT:
 * 	Hardware supports spectrum management defined in 802.11h
 * 	Measurement, Channel Switch, Quieting, TPC
 */
enum ieee80211_hw_flags {
	IEEE80211_HW_RX_INCLUDES_FCS			= 1<<1,
	IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING	= 1<<2,
	IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE		= 1<<3,
	IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE	= 1<<4,
	IEEE80211_HW_SIGNAL_UNSPEC			= 1<<5,
	IEEE80211_HW_SIGNAL_DB				= 1<<6,
	IEEE80211_HW_SIGNAL_DBM				= 1<<7,
	IEEE80211_HW_NOISE_DBM				= 1<<8,
	IEEE80211_HW_SPECTRUM_MGMT			= 1<<9,
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
 * @workqueue: single threaded workqueue available for driver use,
 *	allocated by mac80211 on registration and flushed when an
 *	interface is removed.
 *	NOTICE: All work performed on this workqueue should NEVER
 *	acquire the RTNL lock (i.e. Don't use the function
 *	ieee80211_iterate_active_interfaces())
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
 * @ampdu_queues: number of available hardware transmit queues
 *	for A-MPDU packets, these have no access parameters
 *	because they're used only for A-MPDU frames. Note that
 *	mac80211 will not currently use any of the regular queues
 *	for aggregation.
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
 * @max_altrates: maximum number of alternate rate retry stages
 * @max_altrate_tries: maximum number of tries for each stage
 */
struct ieee80211_hw {
	struct ieee80211_conf conf;
	struct wiphy *wiphy;
	struct workqueue_struct *workqueue;
	const char *rate_control_algorithm;
	void *priv;
	u32 flags;
	unsigned int extra_tx_headroom;
	int channel_change_time;
	int vif_data_size;
	int sta_data_size;
	u16 queues;
	u16 ampdu_queues;
	u16 max_listen_interval;
	s8 max_signal;
	u8 max_altrates;
	u8 max_altrate_tries;
};

struct ieee80211_hw *wiphy_to_hw(struct wiphy *wiphy);

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
 * SET_IEEE80211_PERM_ADDR - set the permanenet MAC address for 802.11 hardware
 *
 * @hw: the &struct ieee80211_hw to set the MAC address for
 * @addr: the address to set
 */
static inline void SET_IEEE80211_PERM_ADDR(struct ieee80211_hw *hw, u8 *addr)
{
	memcpy(hw->wiphy->perm_addr, addr, ETH_ALEN);
}

static inline int ieee80211_num_regular_queues(struct ieee80211_hw *hw)
{
	return hw->queues;
}

static inline int ieee80211_num_queues(struct ieee80211_hw *hw)
{
	return hw->queues + hw->ampdu_queues;
}

static inline struct ieee80211_rate *
ieee80211_get_tx_rate(const struct ieee80211_hw *hw,
		      const struct ieee80211_tx_info *c)
{
	if (WARN_ON(c->tx_rate_idx < 0))
		return NULL;
	return &hw->wiphy->bands[c->band]->bitrates[c->tx_rate_idx];
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
	if (c->control.retries[idx].rate_idx < 0)
		return NULL;
	return &hw->wiphy->bands[c->band]->bitrates[c->control.retries[idx].rate_idx];
}

/**
 * DOC: Hardware crypto acceleration
 *
 * mac80211 is capable of taking advantage of many hardware
 * acceleration designs for encryption and decryption operations.
 *
 * The set_key() callback in the &struct ieee80211_ops for a given
 * device is called to enable hardware acceleration of encryption and
 * decryption. The callback takes an @address parameter that will be
 * the broadcast address for default keys, the other station's hardware
 * address for individual keys or the zero address for keys that will
 * be used only for transmission.
 * Multiple transmission keys with the same key index may be used when
 * VLANs are configured for an access point.
 *
 * The @local_address parameter will always be set to our own address,
 * this is only relevant if you support multiple local addresses.
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
 * This happens everytime the iv16 wraps around (every 65536 packets). The
 * set_key() call will happen only once for each key (unless the AP did
 * rekeying), it will not include a valid phase 1 key. The valid phase 1 key is
 * provided by udpate_tkip_key only. The trigger that makes mac80211 call this
 * handler is software decryption with wrap around of iv16.
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
 * The configure_filter() callback is invoked with the parameters
 * @mc_count and @mc_list for the combined multicast address list
 * of all virtual interfaces, @changed_flags telling which flags
 * were changed and @total_flags with the new flag states.
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
 * @FIF_CONTROL: pass control frames, if PROMISC_IN_BSS is not set then
 *	only those addressed to this station
 *
 * @FIF_OTHER_BSS: pass frames destined to other BSSes
 */
enum ieee80211_filter_flags {
	FIF_PROMISC_IN_BSS	= 1<<0,
	FIF_ALLMULTI		= 1<<1,
	FIF_FCSFAIL		= 1<<2,
	FIF_PLCPFAIL		= 1<<3,
	FIF_BCN_PRBRESP_PROMISC	= 1<<4,
	FIF_CONTROL		= 1<<5,
	FIF_OTHER_BSS		= 1<<6,
};

/**
 * enum ieee80211_ampdu_mlme_action - A-MPDU actions
 *
 * These flags are used with the ampdu_action() callback in
 * &struct ieee80211_ops to indicate which action is needed.
 * @IEEE80211_AMPDU_RX_START: start Rx aggregation
 * @IEEE80211_AMPDU_RX_STOP: stop Rx aggregation
 * @IEEE80211_AMPDU_TX_START: start Tx aggregation
 * @IEEE80211_AMPDU_TX_STOP: stop Tx aggregation
 */
enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP,
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
 *	Must be implemented.
 *
 * @stop: Called after last netdevice attached to the hardware
 *	is disabled. This should turn off the hardware (at least
 *	it must turn off frame reception.)
 *	May be called right after add_interface if that rejects
 *	an interface.
 *	Must be implemented.
 *
 * @add_interface: Called when a netdevice attached to the hardware is
 *	enabled. Because it is not called for monitor mode devices, @open
 *	and @stop must be implemented.
 *	The driver should perform any initialization it needs before
 *	the device can be enabled. The initial configuration for the
 *	interface is given in the conf parameter.
 *	The callback may refuse to add an interface by returning a
 *	negative error code (which will be seen in userspace.)
 *	Must be implemented.
 *
 * @remove_interface: Notifies a driver that an interface is going down.
 *	The @stop callback is called after this if it is the last interface
 *	and no monitor interfaces are present.
 *	When all interfaces are removed, the MAC address in the hardware
 *	must be cleared so the device no longer acknowledges packets,
 *	the mac_addr member of the conf structure is, however, set to the
 *	MAC address of the device going away.
 *	Hence, this callback must be implemented.
 *
 * @config: Handler for configuration requests. IEEE 802.11 code calls this
 *	function to change hardware configuration, e.g., channel.
 *
 * @config_interface: Handler for configuration requests related to interfaces
 *	(e.g. BSSID changes.)
 *
 * @bss_info_changed: Handler for configuration requests related to BSS
 *	parameters that may vary during BSS's lifespan, and may affect low
 *	level driver (e.g. assoc/disassoc status, erp parameters).
 *	This function should not be used if no BSS has been set, unless
 *	for association indication. The @changed parameter indicates which
 *	of the bss parameters has changed when a call is made.
 *
 * @configure_filter: Configure the device's RX filter.
 *	See the section "Frame filtering" for more information.
 *	This callback must be implemented and atomic.
 *
 * @set_tim: Set TIM bit. mac80211 calls this function when a TIM bit
 * 	must be set or cleared for a given STA. Must be atomic.
 *
 * @set_key: See the section "Hardware crypto acceleration"
 *	This callback can sleep, and is only called between add_interface
 *	and remove_interface calls, i.e. while the interface with the
 *	given local_address is enabled.
 *
 * @update_tkip_key: See the section "Hardware crypto acceleration"
 * 	This callback will be called in the context of Rx. Called for drivers
 * 	which set IEEE80211_KEY_FLAG_TKIP_REQ_RX_P1_KEY.
 *
 * @hw_scan: Ask the hardware to service the scan request, no need to start
 *	the scan state machine in stack. The scan must honour the channel
 *	configuration done by the regulatory agent in the wiphy's registered
 *	bands. When the scan finishes, ieee80211_scan_completed() must be
 *	called; note that it also must be called when the scan cannot finish
 *	because the hardware is turned off! Anything else is a bug!
 *
 * @get_stats: return low-level statistics
 *
 * @get_tkip_seq: If your device implements TKIP encryption in hardware this
 *	callback should be provided to read the TKIP transmit IVs (both IV32
 *	and IV16) for the given key from hardware.
 *
 * @set_rts_threshold: Configuration of RTS threshold (if device needs it)
 *
 * @set_frag_threshold: Configuration of fragmentation threshold. Assign this if
 *	the device does fragmentation by itself; if this method is assigned then
 *	the stack will not do fragmentation.
 *
 * @set_retry_limit: Configuration of retry limits (if device needs it)
 *
 * @sta_notify: Notifies low level driver about addition or removal
 *	of assocaited station or AP.
 *
 * @conf_tx: Configure TX queue parameters (EDCF (aifs, cw_min, cw_max),
 *	bursting) for a hardware TX queue.
 *
 * @get_tx_stats: Get statistics of the current TX queue status. This is used
 *	to get number of currently queued packets (queue length), maximum queue
 *	size (limit), and total number of packets sent using each TX queue
 *	(count). The 'stats' pointer points to an array that has hw->queues +
 *	hw->ampdu_queues items.
 *
 * @get_tsf: Get the current TSF timer value from firmware/hardware. Currently,
 *	this is only used for IBSS mode debugging and, as such, is not a
 *	required function. Must be atomic.
 *
 * @reset_tsf: Reset the TSF timer and allow firmware/hardware to synchronize
 *	with other STAs in the IBSS. This is only used in IBSS mode. This
 *	function is optional if the firmware/hardware takes full care of
 *	TSF synchronization.
 *
 * @tx_last_beacon: Determine whether the last IBSS beacon was sent by us.
 *	This is needed only for IBSS mode and the result of this function is
 *	used to determine whether to reply to Probe Requests.
 *
 * @conf_ht: Configures low level driver with 802.11n HT data. Must be atomic.
 *
 * @ampdu_action: Perform a certain A-MPDU action
 * 	The RA/TID combination determines the destination and TID we want
 * 	the ampdu action to be performed for. The action is defined through
 * 	ieee80211_ampdu_mlme_action. Starting sequence number (@ssn)
 * 	is the first frame we expect to perform the action on. notice
 * 	that TX/RX_STOP can pass NULL for this parameter.
 */
struct ieee80211_ops {
	int (*tx)(struct ieee80211_hw *hw, struct sk_buff *skb);
	int (*start)(struct ieee80211_hw *hw);
	void (*stop)(struct ieee80211_hw *hw);
	int (*add_interface)(struct ieee80211_hw *hw,
			     struct ieee80211_if_init_conf *conf);
	void (*remove_interface)(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf);
	int (*config)(struct ieee80211_hw *hw, struct ieee80211_conf *conf);
	int (*config_interface)(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_if_conf *conf);
	void (*bss_info_changed)(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *info,
				 u32 changed);
	void (*configure_filter)(struct ieee80211_hw *hw,
				 unsigned int changed_flags,
				 unsigned int *total_flags,
				 int mc_count, struct dev_addr_list *mc_list);
	int (*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       bool set);
	int (*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		       const u8 *local_address, const u8 *address,
		       struct ieee80211_key_conf *key);
	void (*update_tkip_key)(struct ieee80211_hw *hw,
			struct ieee80211_key_conf *conf, const u8 *address,
			u32 iv32, u16 *phase1key);
	int (*hw_scan)(struct ieee80211_hw *hw, u8 *ssid, size_t len);
	int (*get_stats)(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats);
	void (*get_tkip_seq)(struct ieee80211_hw *hw, u8 hw_key_idx,
			     u32 *iv32, u16 *iv16);
	int (*set_rts_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*set_frag_threshold)(struct ieee80211_hw *hw, u32 value);
	int (*set_retry_limit)(struct ieee80211_hw *hw,
			       u32 short_retry, u32 long_retr);
	void (*sta_notify)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			enum sta_notify_cmd, struct ieee80211_sta *sta);
	int (*conf_tx)(struct ieee80211_hw *hw, u16 queue,
		       const struct ieee80211_tx_queue_params *params);
	int (*get_tx_stats)(struct ieee80211_hw *hw,
			    struct ieee80211_tx_queue_stats *stats);
	u64 (*get_tsf)(struct ieee80211_hw *hw);
	void (*reset_tsf)(struct ieee80211_hw *hw);
	int (*tx_last_beacon)(struct ieee80211_hw *hw);
	int (*ampdu_action)(struct ieee80211_hw *hw,
			    enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn);
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

#ifdef CONFIG_MAC80211_LEDS
extern char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw);
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

/* trick to avoid symbol clashes with the ieee80211 subsystem */
void __ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb,
		    struct ieee80211_rx_status *status);

/**
 * ieee80211_rx - receive frame
 *
 * Use this function to hand received frames to mac80211. The receive
 * buffer in @skb must start with an IEEE 802.11 header or a radiotap
 * header if %RX_FLAG_RADIOTAP is set in the @status flags.
 *
 * This function may not be called in IRQ context. Calls to this function
 * for a single hardware must be synchronized against each other. Calls
 * to this function and ieee80211_rx_irqsafe() may not be mixed for a
 * single hardware.
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 * @status: status of this frame; the status pointer need not be valid
 *	after this function returns
 */
static inline void ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb,
				struct ieee80211_rx_status *status)
{
	__ieee80211_rx(hw, skb, status);
}

/**
 * ieee80211_rx_irqsafe - receive frame
 *
 * Like ieee80211_rx() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * Calls to this function and ieee80211_rx() may not be mixed for a
 * single hardware.
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac80211 after this call
 * @status: status of this frame; the status pointer need not be valid
 *	after this function returns and is not freed by mac80211,
 *	it is recommended that it points to a stack area
 */
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw,
			  struct sk_buff *skb,
			  struct ieee80211_rx_status *status);

/**
 * ieee80211_tx_status - transmit status callback
 *
 * Call this function for all transmitted frames after they have been
 * transmitted. It is permissible to not call this function for
 * multicast frames but this can affect statistics.
 *
 * This function may not be called in IRQ context. Calls to this function
 * for a single hardware must be synchronized against each other. Calls
 * to this function and ieee80211_tx_status_irqsafe() may not be mixed
 * for a single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
void ieee80211_tx_status(struct ieee80211_hw *hw,
			 struct sk_buff *skb);

/**
 * ieee80211_tx_status_irqsafe - IRQ-safe transmit status callback
 *
 * Like ieee80211_tx_status() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * Calls to this function and ieee80211_tx_status() may not be mixed for a
 * single hardware.
 *
 * @hw: the hardware the frame was transmitted by
 * @skb: the frame that was transmitted, owned by mac80211 after this call
 */
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb);

/**
 * ieee80211_beacon_get - beacon generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
 * @control: will be filled with information needed to send this beacon.
 *
 * If the beacon frames are generated by the host system (i.e., not in
 * hardware/firmware), the low-level driver uses this function to receive
 * the next beacon frame from the 802.11 code. The low-level is responsible
 * for calling this function before beacon data is needed (e.g., based on
 * hardware interrupt). Returned skb is used only once and low-level driver
 * is responsible of freeing it.
 */
struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif);

/**
 * ieee80211_rts_get - RTS frame generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
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
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
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
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
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
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
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
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
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
 * @vif: &struct ieee80211_vif pointer from &struct ieee80211_if_init_conf.
 * @control: will be filled with information needed to send returned frame.
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
 * ieee80211_get_hdrlen_from_skb - get header length from data
 *
 * Given an skb with a raw 802.11 header at the data pointer this function
 * returns the 802.11 header length in bytes (not including encryption
 * headers). If the data in the sk_buff is too short to contain a valid 802.11
 * header the function returns 0.
 *
 * @skb: the frame
 */
unsigned int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb);

/**
 * ieee80211_hdrlen - get header length in bytes from frame control
 * @fc: frame control field in little-endian format
 */
unsigned int ieee80211_hdrlen(__le16 fc);

/**
 * ieee80211_get_tkip_key - get a TKIP rc4 for skb
 *
 * This function computes a TKIP rc4 key for an skb. It computes
 * a phase 1 key if needed (iv16 wraps around). This function is to
 * be used by drivers which can do HW encryption but need to compute
 * to phase 1/2 key in SW.
 *
 * @keyconf: the parameter passed with the set key
 * @skb: the skb for which the key is needed
 * @rc4key: a buffer to which the key will be written
 * @type: TBD
 * @key: TBD
 */
void ieee80211_get_tkip_key(struct ieee80211_key_conf *keyconf,
				struct sk_buff *skb,
				enum ieee80211_tkip_key_type type, u8 *key);
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
 * mac80211 that the scan finished.
 *
 * @hw: the hardware that finished the scan
 */
void ieee80211_scan_completed(struct ieee80211_hw *hw);

/**
 * ieee80211_iterate_active_interfaces - iterate active interfaces
 *
 * This function iterates over the interfaces associated with a given
 * hardware that are currently active and calls the callback for them.
 * This function allows the iterator function to sleep, when the iterator
 * function is atomic @ieee80211_iterate_active_interfaces_atomic can
 * be used.
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
 * ieee80211_start_tx_ba_session - Start a tx Block Ack session.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient
 * @tid: the TID to BA on.
 * @return: success if addBA request was sent, failure otherwise
 *
 * Although mac80211/low level driver/user space application can estimate
 * the need to start aggregation on a certain RA/TID, the session level
 * will be managed by the mac80211.
 */
int ieee80211_start_tx_ba_session(struct ieee80211_hw *hw, u8 *ra, u16 tid);

/**
 * ieee80211_start_tx_ba_cb - low level driver ready to aggregate.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient.
 * @tid: the TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session.
 */
void ieee80211_start_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u16 tid);

/**
 * ieee80211_start_tx_ba_cb_irqsafe - low level driver ready to aggregate.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient.
 * @tid: the TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session.
 * This version of the function is IRQ-safe.
 */
void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_hw *hw, const u8 *ra,
				      u16 tid);

/**
 * ieee80211_stop_tx_ba_session - Stop a Block Ack session.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient
 * @tid: the TID to stop BA.
 * @initiator: if indicates initiator DELBA frame will be sent.
 * @return: error if no sta with matching da found, success otherwise
 *
 * Although mac80211/low level driver/user space application can estimate
 * the need to stop aggregation on a certain RA/TID, the session level
 * will be managed by the mac80211.
 */
int ieee80211_stop_tx_ba_session(struct ieee80211_hw *hw,
				 u8 *ra, u16 tid,
				 enum ieee80211_back_parties initiator);

/**
 * ieee80211_stop_tx_ba_cb - low level driver ready to stop aggregate.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient.
 * @tid: the desired TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session tear down.
 */
void ieee80211_stop_tx_ba_cb(struct ieee80211_hw *hw, u8 *ra, u8 tid);

/**
 * ieee80211_stop_tx_ba_cb_irqsafe - low level driver ready to stop aggregate.
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @ra: receiver address of the BA session recipient.
 * @tid: the desired TID to BA on.
 *
 * This function must be called by low level driver once it has
 * finished with preparations for the BA session tear down.
 * This version of the function is IRQ-safe.
 */
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_hw *hw, const u8 *ra,
				     u16 tid);

/**
 * ieee80211_notify_mac - low level driver notification
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @notif_type: enum ieee80211_notification_types
 *
 * This function must be called by low level driver to inform mac80211 of
 * low level driver status change or force mac80211 to re-assoc for low
 * level driver internal error that require re-assoc.
 */
void ieee80211_notify_mac(struct ieee80211_hw *hw,
			  enum ieee80211_notification_types  notif_type);

/**
 * ieee80211_find_sta - find a station
 *
 * @hw: pointer as obtained from ieee80211_alloc_hw()
 * @addr: station's address
 *
 * This function must be called under RCU lock and the
 * resulting pointer is only valid under RCU lock as well.
 */
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_hw *hw,
					 const u8 *addr);


/* Rate control API */
/**
 * struct rate_selection - rate information for/from rate control algorithms
 *
 * @rate_idx: selected transmission rate index
 * @nonerp_idx: Non-ERP rate to use instead if ERP cannot be used
 * @probe_idx: rate for probing (or -1)
 * @max_rate_idx: maximum rate index that can be used, this is
 *	input to the algorithm and will be enforced
 */
struct rate_selection {
	s8 rate_idx, nonerp_idx, probe_idx, max_rate_idx;
};

struct rate_control_ops {
	struct module *module;
	const char *name;
	void *(*alloc)(struct ieee80211_hw *hw, struct dentry *debugfsdir);
	void (*clear)(void *priv);
	void (*free)(void *priv);

	void *(*alloc_sta)(void *priv, struct ieee80211_sta *sta, gfp_t gfp);
	void (*rate_init)(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta);
	void (*free_sta)(void *priv, struct ieee80211_sta *sta,
			 void *priv_sta);

	void (*tx_status)(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb);
	void (*get_rate)(void *priv, struct ieee80211_supported_band *sband,
			 struct ieee80211_sta *sta, void *priv_sta,
			 struct sk_buff *skb,
			 struct rate_selection *sel);

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


int ieee80211_rate_control_register(struct rate_control_ops *ops);
void ieee80211_rate_control_unregister(struct rate_control_ops *ops);

#endif /* MAC80211_H */
