/*
 * Low-level hardware driver -- IEEE 802.11 driver (80211.o) interface
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
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

/* Note! Only ieee80211_tx_status_irqsafe() and ieee80211_rx_irqsafe() can be
 * called in hardware interrupt context. The low-level driver must not call any
 * other functions in hardware interrupt context. If there is a need for such
 * call, the low-level driver should first ACK the interrupt and perform the
 * IEEE 802.11 code call after this, e.g., from a scheduled tasklet (in
 * software interrupt context).
 */

/*
 * Frame format used when passing frame between low-level hardware drivers
 * and IEEE 802.11 driver the same as used in the wireless media, i.e.,
 * buffers start with IEEE 802.11 header and include the same octets that
 * are sent over air.
 *
 * If hardware uses IEEE 802.3 headers (and perform 802.3 <-> 802.11
 * conversion in firmware), upper layer 802.11 code needs to be changed to
 * support this.
 *
 * If the receive frame format is not the same as the real frame sent
 * on the wireless media (e.g., due to padding etc.), upper layer 802.11 code
 * could be updated to provide support for such format assuming this would
 * optimize the performance, e.g., by removing need to re-allocation and
 * copying of the data.
 */

#define IEEE80211_CHAN_W_SCAN 0x00000001
#define IEEE80211_CHAN_W_ACTIVE_SCAN 0x00000002
#define IEEE80211_CHAN_W_IBSS 0x00000004

/* Channel information structure. Low-level driver is expected to fill in chan,
 * freq, and val fields. Other fields will be filled in by 80211.o based on
 * hostapd information and low-level driver does not need to use them. The
 * limits for each channel will be provided in 'struct ieee80211_conf' when
 * configuring the low-level driver with hw->config callback. If a device has
 * a default regulatory domain, IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED
 * can be set to let the driver configure all fields */
struct ieee80211_channel {
	short chan; /* channel number (IEEE 802.11) */
	short freq; /* frequency in MHz */
	int val; /* hw specific value for the channel */
	int flag; /* flag for hostapd use (IEEE80211_CHAN_*) */
	unsigned char power_level;
	unsigned char antenna_max;
};

#define IEEE80211_RATE_ERP 0x00000001
#define IEEE80211_RATE_BASIC 0x00000002
#define IEEE80211_RATE_PREAMBLE2 0x00000004
#define IEEE80211_RATE_SUPPORTED 0x00000010
#define IEEE80211_RATE_OFDM 0x00000020
#define IEEE80211_RATE_CCK 0x00000040
#define IEEE80211_RATE_TURBO 0x00000080
#define IEEE80211_RATE_MANDATORY 0x00000100

#define IEEE80211_RATE_CCK_2 (IEEE80211_RATE_CCK | IEEE80211_RATE_PREAMBLE2)
#define IEEE80211_RATE_MODULATION(f) \
	(f & (IEEE80211_RATE_CCK | IEEE80211_RATE_OFDM))

/* Low-level driver should set PREAMBLE2, OFDM, CCK, and TURBO flags.
 * BASIC, SUPPORTED, ERP, and MANDATORY flags are set in 80211.o based on the
 * configuration. */
struct ieee80211_rate {
	int rate; /* rate in 100 kbps */
	int val; /* hw specific value for the rate */
	int flags; /* IEEE80211_RATE_ flags */
	int val2; /* hw specific value for the rate when using short preamble
		   * (only when IEEE80211_RATE_PREAMBLE2 flag is set, i.e., for
		   * 2, 5.5, and 11 Mbps) */
	signed char min_rssi_ack;
	unsigned char min_rssi_ack_delta;

	/* following fields are set by 80211.o and need not be filled by the
	 * low-level driver */
	int rate_inv; /* inverse of the rate (LCM(all rates) / rate) for
		       * optimizing channel utilization estimates */
};

/* 802.11g is backwards-compatible with 802.11b, so a wlan card can
 * actually be both in 11b and 11g modes at the same time. */
enum {
	MODE_IEEE80211A, /* IEEE 802.11a */
	MODE_IEEE80211B, /* IEEE 802.11b only */
	MODE_ATHEROS_TURBO, /* Atheros Turbo mode (2x.11a at 5 GHz) */
	MODE_IEEE80211G, /* IEEE 802.11g (and 802.11b compatibility) */
	MODE_ATHEROS_TURBOG, /* Atheros Turbo mode (2x.11g at 2.4 GHz) */

	/* keep last */
	NUM_IEEE80211_MODES
};

struct ieee80211_hw_mode {
	int mode; /* MODE_IEEE80211... */
	int num_channels; /* Number of channels (below) */
	struct ieee80211_channel *channels; /* Array of supported channels */
	int num_rates; /* Number of rates (below) */
	struct ieee80211_rate *rates; /* Array of supported rates */

	struct list_head list; /* Internal, don't touch */
};

struct ieee80211_tx_queue_params {
	int aifs; /* 0 .. 255; -1 = use default */
	int cw_min; /* 2^n-1: 1, 3, 7, .. , 1023; 0 = use default */
	int cw_max; /* 2^n-1: 1, 3, 7, .. , 1023; 0 = use default */
	int burst_time; /* maximum burst time in 0.1 ms (i.e., 10 = 1 ms);
			 * 0 = disabled */
};

struct ieee80211_tx_queue_stats_data {
	unsigned int len; /* num packets in queue */
	unsigned int limit; /* queue len (soft) limit */
	unsigned int count; /* total num frames sent */
};

enum {
	IEEE80211_TX_QUEUE_DATA0,
	IEEE80211_TX_QUEUE_DATA1,
	IEEE80211_TX_QUEUE_DATA2,
	IEEE80211_TX_QUEUE_DATA3,
	IEEE80211_TX_QUEUE_DATA4,
	IEEE80211_TX_QUEUE_SVP,

	NUM_TX_DATA_QUEUES,

/* due to stupidity in the sub-ioctl userspace interface, the items in
 * this struct need to have fixed values. As soon as it is removed, we can
 * fix these entries. */
	IEEE80211_TX_QUEUE_AFTER_BEACON = 6,
	IEEE80211_TX_QUEUE_BEACON = 7
};

struct ieee80211_tx_queue_stats {
	struct ieee80211_tx_queue_stats_data data[NUM_TX_DATA_QUEUES];
};

struct ieee80211_low_level_stats {
	unsigned int dot11ACKFailureCount;
	unsigned int dot11RTSFailureCount;
	unsigned int dot11FCSErrorCount;
	unsigned int dot11RTSSuccessCount;
};

/* Transmit control fields. This data structure is passed to low-level driver
 * with each TX frame. The low-level driver is responsible for configuring
 * the hardware to use given values (depending on what is supported). */
#define HW_KEY_IDX_INVALID -1

struct ieee80211_tx_control {
	int tx_rate; /* Transmit rate, given as the hw specific value for the
		      * rate (from struct ieee80211_rate) */
	int rts_cts_rate; /* Transmit rate for RTS/CTS frame, given as the hw
			   * specific value for the rate (from
			   * struct ieee80211_rate) */

#define IEEE80211_TXCTL_REQ_TX_STATUS	(1<<0)/* request TX status callback for
						* this frame */
#define IEEE80211_TXCTL_DO_NOT_ENCRYPT	(1<<1) /* send this frame without
						* encryption; e.g., for EAPOL
						* frames */
#define IEEE80211_TXCTL_USE_RTS_CTS	(1<<2) /* use RTS-CTS before sending
						* frame */
#define IEEE80211_TXCTL_USE_CTS_PROTECT	(1<<3) /* use CTS protection for the
						* frame (e.g., for combined
						* 802.11g / 802.11b networks) */
#define IEEE80211_TXCTL_NO_ACK		(1<<4) /* tell the low level not to
						* wait for an ack */
#define IEEE80211_TXCTL_RATE_CTRL_PROBE	(1<<5)
#define IEEE80211_TXCTL_CLEAR_DST_MASK	(1<<6)
#define IEEE80211_TXCTL_REQUEUE		(1<<7)
#define IEEE80211_TXCTL_FIRST_FRAGMENT	(1<<8) /* this is a first fragment of
						* the frame */
#define IEEE80211_TXCTL_TKIP_NEW_PHASE1_KEY (1<<9)
	u32 flags;			       /* tx control flags defined
						* above */
	u8 retry_limit;		/* 1 = only first attempt, 2 = one retry, .. */
	u8 power_level;		/* per-packet transmit power level, in dBm */
	u8 antenna_sel_tx; 	/* 0 = default/diversity, 1 = Ant0, 2 = Ant1 */
	s8 key_idx;		/* -1 = do not encrypt, >= 0 keyidx from
				 * hw->set_key() */
	u8 icv_len;		/* length of the ICV/MIC field in octets */
	u8 iv_len;		/* length of the IV field in octets */
	u8 tkip_key[16];	/* generated phase2/phase1 key for hw TKIP */
	u8 queue;		/* hardware queue to use for this frame;
				 * 0 = highest, hw->queues-1 = lowest */
	u8 sw_retry_attempt;	/* number of times hw has tried to
				 * transmit frame (not incl. hw retries) */

	struct ieee80211_rate *rate;		/* internal 80211.o rate */
	struct ieee80211_rate *rts_rate;	/* internal 80211.o rate
						 * for RTS/CTS */
	int alt_retry_rate; /* retry rate for the last retries, given as the
			     * hw specific value for the rate (from
			     * struct ieee80211_rate). To be used to limit
			     * packet dropping when probing higher rates, if hw
			     * supports multiple retry rates. -1 = not used */
	int type;	/* internal */
	int ifindex;	/* internal */
};

/* Receive status. The low-level driver should provide this information
 * (the subset supported by hardware) to the 802.11 code with each received
 * frame. */
struct ieee80211_rx_status {
	u64 mactime;
	int freq; /* receive frequency in Mhz */
	int channel;
	int phymode;
	int ssi;
	int signal; /* used as qual in statistics reporting */
	int noise;
	int antenna;
	int rate;
#define RX_FLAG_MMIC_ERROR	(1<<0)
#define RX_FLAG_DECRYPTED	(1<<1)
#define RX_FLAG_RADIOTAP	(1<<2)
	int flag;
};

/* Transmit status. The low-level driver should provide this information
 * (the subset supported by hardware) to the 802.11 code for each transmit
 * frame. */
struct ieee80211_tx_status {
	/* copied ieee80211_tx_control structure */
	struct ieee80211_tx_control control;

#define IEEE80211_TX_STATUS_TX_FILTERED	(1<<0)
#define IEEE80211_TX_STATUS_ACK		(1<<1) /* whether the TX frame was ACKed */
	u32 flags;		/* tx staus flags defined above */

	int ack_signal; /* measured signal strength of the ACK frame */
	int excessive_retries;
	int retry_count;

	int queue_length;      /* information about TX queue */
	int queue_number;
};


/**
 * struct ieee80211_conf - configuration of the device
 *
 * This struct indicates how the driver shall configure the hardware.
 *
 * @radio_enabled: when zero, driver is required to switch off the radio.
 */
struct ieee80211_conf {
	int channel;			/* IEEE 802.11 channel number */
	int freq;			/* MHz */
	int channel_val;		/* hw specific value for the channel */

	int phymode;			/* MODE_IEEE80211A, .. */
	struct ieee80211_channel *chan;
	struct ieee80211_hw_mode *mode;
	unsigned int regulatory_domain;
	int radio_enabled;

	int beacon_int;

#define IEEE80211_CONF_SHORT_SLOT_TIME	(1<<0) /* use IEEE 802.11g Short Slot
						* Time */
#define IEEE80211_CONF_SSID_HIDDEN	(1<<1) /* do not broadcast the ssid */
#define IEEE80211_CONF_RADIOTAP		(1<<2) /* use radiotap if supported
						  check this bit at RX time */
	u32 flags;			/* configuration flags defined above */

	u8 power_level;			/* transmit power limit for current
					 * regulatory domain; in dBm */
	u8 antenna_max;			/* maximum antenna gain */
	short tx_power_reduction; /* in 0.1 dBm */

	/* 0 = default/diversity, 1 = Ant0, 2 = Ant1 */
	u8 antenna_sel_tx;
	u8 antenna_sel_rx;

	int antenna_def;
	int antenna_mode;

	/* Following five fields are used for IEEE 802.11H */
	unsigned int radar_detect;
	unsigned int spect_mgmt;
	/* All following fields are currently unused. */
	unsigned int quiet_duration; /* duration of quiet period */
	unsigned int quiet_offset; /* how far into the beacon is the quiet
				    * period */
	unsigned int quiet_period;
	u8 radar_firpwr_threshold;
	u8 radar_rssi_threshold;
	u8 pulse_height_threshold;
	u8 pulse_rssi_threshold;
	u8 pulse_inband_threshold;
};

/**
 * enum ieee80211_if_types - types of 802.11 network interfaces
 *
 * @IEEE80211_IF_TYPE_AP: interface in AP mode.
 * @IEEE80211_IF_TYPE_MGMT: special interface for communication with hostap
 *	daemon. Drivers should never see this type.
 * @IEEE80211_IF_TYPE_STA: interface in STA (client) mode.
 * @IEEE80211_IF_TYPE_IBSS: interface in IBSS (ad-hoc) mode.
 * @IEEE80211_IF_TYPE_MNTR: interface in monitor (rfmon) mode.
 * @IEEE80211_IF_TYPE_WDS: interface in WDS mode.
 * @IEEE80211_IF_TYPE_VLAN: not used.
 */
enum ieee80211_if_types {
	IEEE80211_IF_TYPE_AP = 0x00000000,
	IEEE80211_IF_TYPE_MGMT = 0x00000001,
	IEEE80211_IF_TYPE_STA = 0x00000002,
	IEEE80211_IF_TYPE_IBSS = 0x00000003,
	IEEE80211_IF_TYPE_MNTR = 0x00000004,
	IEEE80211_IF_TYPE_WDS = 0x5A580211,
	IEEE80211_IF_TYPE_VLAN = 0x00080211,
};

/**
 * struct ieee80211_if_init_conf - initial configuration of an interface
 *
 * @if_id: internal interface ID. This number has no particular meaning to
 *	drivers and the only allowed usage is to pass it to
 *	ieee80211_beacon_get() and ieee80211_get_buffered_bc() functions.
 *	This field is not valid for monitor interfaces
 *	(interfaces of %IEEE80211_IF_TYPE_MNTR type).
 * @type: one of &enum ieee80211_if_types constants. Determines the type of
 *	added/removed interface.
 * @mac_addr: pointer to MAC address of the interface. This pointer is valid
 *	until the interface is removed (i.e. it cannot be used after
 *	remove_interface() callback was called for this interface).
 *	This pointer will be %NULL for monitor interfaces, be careful.
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
	int if_id;
	int type;
	void *mac_addr;
};

/**
 * struct ieee80211_if_conf - configuration of an interface
 *
 * @type: type of the interface. This is always the same as was specified in
 *	&struct ieee80211_if_init_conf. The type of an interface never changes
 *	during the life of the interface; this field is present only for
 *	convenience.
 * @bssid: BSSID of the network we are associated to/creating.
 * @ssid: used (together with @ssid_len) by drivers for hardware that
 *	generate beacons independently. The pointer is valid only during the
 *	config_interface() call, so copy the value somewhere if you need
 *	it.
 * @ssid_len: length of the @ssid field.
 * @generic_elem: used (together with @generic_elem_len) by drivers for
 *	hardware that generate beacons independently. The pointer is valid
 *	only during the config_interface() call, so copy the value somewhere
 *	if you need it.
 * @generic_elem_len: length of the generic element.
 * @beacon: beacon template. Valid only if @host_gen_beacon_template in
 *	&struct ieee80211_hw is set. The driver is responsible of freeing
 *	the sk_buff.
 * @beacon_control: tx_control for the beacon template, this field is only
 *	valid when the @beacon field was set.
 *
 * This structure is passed to the config_interface() callback of
 * &struct ieee80211_hw.
 */
struct ieee80211_if_conf {
	int type;
	u8 *bssid;
	u8 *ssid;
	size_t ssid_len;
	u8 *generic_elem;
	size_t generic_elem_len;
	struct sk_buff *beacon;
	struct ieee80211_tx_control *beacon_control;
};

typedef enum { ALG_NONE, ALG_WEP, ALG_TKIP, ALG_CCMP, ALG_NULL }
ieee80211_key_alg;


struct ieee80211_key_conf {

	int hw_key_idx;			/* filled + used by low-level driver */
	ieee80211_key_alg alg;
	int keylen;

#define IEEE80211_KEY_FORCE_SW_ENCRYPT (1<<0) /* to be cleared by low-level
						 driver */
#define IEEE80211_KEY_DEFAULT_TX_KEY   (1<<1) /* This key is the new default TX
						 key (used only for broadcast
						 keys). */
#define IEEE80211_KEY_DEFAULT_WEP_ONLY (1<<2) /* static WEP is the only
						 configured security policy;
						 this allows some low-level
						 drivers to determine when
						 hwaccel can be used */
	u32 flags; /* key configuration flags defined above */

	s8 keyidx;			/* WEP key index */
	u8 key[0];
};

#define IEEE80211_SEQ_COUNTER_RX	0
#define IEEE80211_SEQ_COUNTER_TX	1

typedef enum {
	SET_KEY, DISABLE_KEY, REMOVE_ALL_KEYS,
} set_key_cmd;

/* This is driver-visible part of the per-hw state the stack keeps. */
struct ieee80211_hw {
	/* points to the cfg80211 wiphy for this piece. Note
	 * that you must fill in the perm_addr and dev fields
	 * of this structure, use the macros provided below. */
	struct wiphy *wiphy;

	/* assigned by mac80211, don't write */
	struct ieee80211_conf conf;

	/* Single thread workqueue available for driver use
	 * Allocated by mac80211 on registration */
	struct workqueue_struct *workqueue;

	/* Pointer to the private area that was
	 * allocated with this struct for you. */
	void *priv;

	/* The rest is information about your hardware */

	/* TODO: frame_type 802.11/802.3, sw_encryption requirements */

	/* Some wireless LAN chipsets generate beacons in the hardware/firmware
	 * and others rely on host generated beacons. This option is used to
	 * configure the upper layer IEEE 802.11 module to generate beacons.
	 * The low-level driver can use ieee80211_beacon_get() to fetch the
	 * next beacon frame. */
#define IEEE80211_HW_HOST_GEN_BEACON (1<<0)

	/* The device needs to be supplied with a beacon template only. */
#define IEEE80211_HW_HOST_GEN_BEACON_TEMPLATE (1<<1)

	/* Some devices handle decryption internally and do not
	 * indicate whether the frame was encrypted (unencrypted frames
	 * will be dropped by the hardware, unless specifically allowed
	 * through) */
#define IEEE80211_HW_DEVICE_HIDES_WEP (1<<2)

	/* Whether RX frames passed to ieee80211_rx() include FCS in the end */
#define IEEE80211_HW_RX_INCLUDES_FCS (1<<3)

	/* Some wireless LAN chipsets buffer broadcast/multicast frames for
	 * power saving stations in the hardware/firmware and others rely on
	 * the host system for such buffering. This option is used to
	 * configure the IEEE 802.11 upper layer to buffer broadcast/multicast
	 * frames when there are power saving stations so that low-level driver
	 * can fetch them with ieee80211_get_buffered_bc(). */
#define IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING (1<<4)

#define IEEE80211_HW_WEP_INCLUDE_IV (1<<5)

	/* will data nullfunc frames get proper TX status callback */
#define IEEE80211_HW_DATA_NULLFUNC_ACK (1<<6)

	/* Force software encryption for TKIP packets if WMM is enabled. */
#define IEEE80211_HW_NO_TKIP_WMM_HWACCEL (1<<7)

	/* Some devices handle Michael MIC internally and do not include MIC in
	 * the received packets passed up. device_strips_mic must be set
	 * for such devices. The 'encryption' frame control bit is expected to
	 * be still set in the IEEE 802.11 header with this option unlike with
	 * the device_hides_wep configuration option.
	 */
#define IEEE80211_HW_DEVICE_STRIPS_MIC (1<<8)

	/* Device is capable of performing full monitor mode even during
	 * normal operation. */
#define IEEE80211_HW_MONITOR_DURING_OPER (1<<9)

	/* Device does not need BSSID filter set to broadcast in order to
	 * receive all probe responses while scanning */
#define IEEE80211_HW_NO_PROBE_FILTERING (1<<10)

	/* Channels are already configured to the default regulatory domain
	 * specified in the device's EEPROM */
#define IEEE80211_HW_DEFAULT_REG_DOMAIN_CONFIGURED (1<<11)

	/* calculate Michael MIC for an MSDU when doing hwcrypto */
#define IEEE80211_HW_TKIP_INCLUDE_MMIC (1<<12)
	/* Do TKIP phase1 key mixing in stack to support cards only do
	 * phase2 key mixing when doing hwcrypto */
#define IEEE80211_HW_TKIP_REQ_PHASE1_KEY (1<<13)
	/* Do TKIP phase1 and phase2 key mixing in stack and send the generated
	 * per-packet RC4 key with each TX frame when doing hwcrypto */
#define IEEE80211_HW_TKIP_REQ_PHASE2_KEY (1<<14)

	u32 flags;			/* hardware flags defined above */

	/* Set to the size of a needed device specific skb headroom for TX skbs. */
	unsigned int extra_tx_headroom;

	/* This is the time in us to change channels
	 */
	int channel_change_time;
	/* Maximum values for various statistics.
	 * Leave at 0 to indicate no support. Use negative numbers for dBm. */
	s8 max_rssi;
	s8 max_signal;
	s8 max_noise;

	/* Number of available hardware TX queues for data packets.
	 * WMM requires at least four queues. */
	int queues;
};

static inline void SET_IEEE80211_DEV(struct ieee80211_hw *hw, struct device *dev)
{
	set_wiphy_dev(hw->wiphy, dev);
}

static inline void SET_IEEE80211_PERM_ADDR(struct ieee80211_hw *hw, u8 *addr)
{
	memcpy(hw->wiphy->perm_addr, addr, ETH_ALEN);
}

/* Configuration block used by the low-level driver to tell the 802.11 code
 * about supported hardware features and to pass function pointers to callback
 * functions. */
struct ieee80211_ops {
	/* Handler that 802.11 module calls for each transmitted frame.
	 * skb contains the buffer starting from the IEEE 802.11 header.
	 * The low-level driver should send the frame out based on
	 * configuration in the TX control data.
	 * Must be atomic. */
	int (*tx)(struct ieee80211_hw *hw, struct sk_buff *skb,
		  struct ieee80211_tx_control *control);

	/* Handler for performing hardware reset. */
	int (*reset)(struct ieee80211_hw *hw);

	/* Handler that is called when any netdevice attached to the hardware
	 * device is set UP for the first time. This can be used, e.g., to
	 * enable interrupts and beacon sending. */
	int (*open)(struct ieee80211_hw *hw);

	/* Handler that is called when the last netdevice attached to the
	 * hardware device is set DOWN. This can be used, e.g., to disable
	 * interrupts and beacon sending. */
	int (*stop)(struct ieee80211_hw *hw);

	/* Handler for asking a driver if a new interface can be added (or,
	 * more exactly, set UP). If the handler returns zero, the interface
	 * is added. Driver should perform any initialization it needs prior
	 * to returning zero. By returning non-zero addition of the interface
	 * is inhibited. Unless monitor_during_oper is set, it is guaranteed
	 * that monitor interfaces and normal interfaces are mutually
	 * exclusive. If assigned, the open() handler is called after
	 * add_interface() if this is the first device added. The
	 * add_interface() callback has to be assigned because it is the only
	 * way to obtain the requested MAC address for any interface.
	 */
	int (*add_interface)(struct ieee80211_hw *hw,
			     struct ieee80211_if_init_conf *conf);

	/* Notify a driver that an interface is going down. The stop() handler
	 * is called prior to this if this is a last interface. */
	void (*remove_interface)(struct ieee80211_hw *hw,
				 struct ieee80211_if_init_conf *conf);

	/* Handler for configuration requests. IEEE 802.11 code calls this
	 * function to change hardware configuration, e.g., channel. */
	int (*config)(struct ieee80211_hw *hw, struct ieee80211_conf *conf);

	/* Handler for configuration requests related to interfaces (e.g.
	 * BSSID). */
	int (*config_interface)(struct ieee80211_hw *hw,
				int if_id, struct ieee80211_if_conf *conf);

	/* ieee80211 drivers do not have access to the &struct net_device
	 * that is (are) connected with their device. Hence (and because
	 * we need to combine the multicast lists and flags for multiple
	 * virtual interfaces), they cannot assign set_multicast_list.
	 * The parameters here replace dev->flags and dev->mc_count,
	 * dev->mc_list is replaced by calling ieee80211_get_mc_list_item.
	 * Must be atomic. */
	void (*set_multicast_list)(struct ieee80211_hw *hw,
				   unsigned short flags, int mc_count);

	/* Set TIM bit handler. If the hardware/firmware takes care of beacon
	 * generation, IEEE 802.11 code uses this function to tell the
	 * low-level to set (or clear if set==0) TIM bit for the given aid. If
	 * host system is used to generate beacons, this handler is not used
	 * and low-level driver should set it to NULL.
	 * Must be atomic. */
	int (*set_tim)(struct ieee80211_hw *hw, int aid, int set);

	/* Set encryption key. IEEE 802.11 module calls this function to set
	 * encryption keys. addr is ff:ff:ff:ff:ff:ff for default keys and
	 * station hwaddr for individual keys. aid of the station is given
	 * to help low-level driver in selecting which key->hw_key_idx to use
	 * for this key. TX control data will use the hw_key_idx selected by
	 * the low-level driver.
	 * Must be atomic. */
	int (*set_key)(struct ieee80211_hw *hw, set_key_cmd cmd,
		       u8 *addr, struct ieee80211_key_conf *key, int aid);

	/* Set TX key index for default/broadcast keys. This is needed in cases
	 * where wlan card is doing full WEP/TKIP encapsulation (wep_include_iv
	 * is not set), in other cases, this function pointer can be set to
	 * NULL since the IEEE 802. 11 module takes care of selecting the key
	 * index for each TX frame. */
	int (*set_key_idx)(struct ieee80211_hw *hw, int idx);

	/* Enable/disable IEEE 802.1X. This item requests wlan card to pass
	 * unencrypted EAPOL-Key frames even when encryption is configured.
	 * If the wlan card does not require such a configuration, this
	 * function pointer can be set to NULL. */
	int (*set_ieee8021x)(struct ieee80211_hw *hw, int use_ieee8021x);

	/* Set port authorization state (IEEE 802.1X PAE) to be authorized
	 * (authorized=1) or unauthorized (authorized=0). This function can be
	 * used if the wlan hardware or low-level driver implements PAE.
	 * 80211.o module will anyway filter frames based on authorization
	 * state, so this function pointer can be NULL if low-level driver does
	 * not require event notification about port state changes.
	 * Currently unused. */
	int (*set_port_auth)(struct ieee80211_hw *hw, u8 *addr,
			     int authorized);

	/* Ask the hardware to service the scan request, no need to start
	 * the scan state machine in stack. */
	int (*hw_scan)(struct ieee80211_hw *hw, u8 *ssid, size_t len);

	/* return low-level statistics */
	int (*get_stats)(struct ieee80211_hw *hw,
			 struct ieee80211_low_level_stats *stats);

	/* For devices that generate their own beacons and probe response
	 * or association responses this updates the state of privacy_invoked
	 * returns 0 for success or an error number */
	int (*set_privacy_invoked)(struct ieee80211_hw *hw,
				   int privacy_invoked);

	/* For devices that have internal sequence counters, allow 802.11
	 * code to access the current value of a counter */
	int (*get_sequence_counter)(struct ieee80211_hw *hw,
				    u8* addr, u8 keyidx, u8 txrx,
				    u32* iv32, u16* iv16);

	/* Configuration of RTS threshold (if device needs it) */
	int (*set_rts_threshold)(struct ieee80211_hw *hw, u32 value);

	/* Configuration of fragmentation threshold.
	 * Assign this if the device does fragmentation by itself,
	 * if this method is assigned then the stack will not do
	 * fragmentation. */
	int (*set_frag_threshold)(struct ieee80211_hw *hw, u32 value);

	/* Configuration of retry limits (if device needs it) */
	int (*set_retry_limit)(struct ieee80211_hw *hw,
			       u32 short_retry, u32 long_retr);

	/* Number of STAs in STA table notification (NULL = disabled).
	 * Must be atomic. */
	void (*sta_table_notification)(struct ieee80211_hw *hw,
				       int num_sta);

	/* Configure TX queue parameters (EDCF (aifs, cw_min, cw_max),
	 * bursting) for a hardware TX queue.
	 * queue = IEEE80211_TX_QUEUE_*.
	 * Must be atomic. */
	int (*conf_tx)(struct ieee80211_hw *hw, int queue,
		       const struct ieee80211_tx_queue_params *params);

	/* Get statistics of the current TX queue status. This is used to get
	 * number of currently queued packets (queue length), maximum queue
	 * size (limit), and total number of packets sent using each TX queue
	 * (count).
	 * Currently unused. */
	int (*get_tx_stats)(struct ieee80211_hw *hw,
			    struct ieee80211_tx_queue_stats *stats);

	/* Get the current TSF timer value from firmware/hardware. Currently,
	 * this is only used for IBSS mode debugging and, as such, is not a
	 * required function.
	 * Must be atomic. */
	u64 (*get_tsf)(struct ieee80211_hw *hw);

	/* Reset the TSF timer and allow firmware/hardware to synchronize with
	 * other STAs in the IBSS. This is only used in IBSS mode. This
	 * function is optional if the firmware/hardware takes full care of
	 * TSF synchronization. */
	void (*reset_tsf)(struct ieee80211_hw *hw);

	/* Setup beacon data for IBSS beacons. Unlike access point (Master),
	 * IBSS uses a fixed beacon frame which is configured using this
	 * function. This handler is required only for IBSS mode. */
	int (*beacon_update)(struct ieee80211_hw *hw,
			     struct sk_buff *skb,
			     struct ieee80211_tx_control *control);

	/* Determine whether the last IBSS beacon was sent by us. This is
	 * needed only for IBSS mode and the result of this function is used to
	 * determine whether to reply to Probe Requests. */
	int (*tx_last_beacon)(struct ieee80211_hw *hw);
};

/* Allocate a new hardware device. This must be called once for each
 * hardware device. The returned pointer must be used to refer to this
 * device when calling other functions. 802.11 code allocates a private data
 * area for the low-level driver. The size of this area is given as
 * priv_data_len.
 */
struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
					const struct ieee80211_ops *ops);

/* Register hardware device to the IEEE 802.11 code and kernel. Low-level
 * drivers must call this function before using any other IEEE 802.11
 * function except ieee80211_register_hwmode. */
int ieee80211_register_hw(struct ieee80211_hw *hw);

/* driver can use this and ieee80211_get_rx_led_name to get the
 * name of the registered LEDs after ieee80211_register_hw
 * was called.
 * This is useful to set the default trigger on the LED class
 * device that your driver should export for each LED the device
 * has, that way the default behaviour will be as expected but
 * the user can still change it/turn off the LED etc.
 */
#ifdef CONFIG_MAC80211_LEDS
extern char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw);
extern char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw);
#endif
static inline char *ieee80211_get_tx_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_tx_led_name(hw);
#else
	return NULL;
#endif
}

static inline char *ieee80211_get_rx_led_name(struct ieee80211_hw *hw)
{
#ifdef CONFIG_MAC80211_LEDS
	return __ieee80211_get_rx_led_name(hw);
#else
	return NULL;
#endif
}

/* Register a new hardware PHYMODE capability to the stack. */
int ieee80211_register_hwmode(struct ieee80211_hw *hw,
			      struct ieee80211_hw_mode *mode);

/* Unregister a hardware device. This function instructs 802.11 code to free
 * allocated resources and unregister netdevices from the kernel. */
void ieee80211_unregister_hw(struct ieee80211_hw *hw);

/* Free everything that was allocated including private data of a driver. */
void ieee80211_free_hw(struct ieee80211_hw *hw);

/* Receive frame callback function. The low-level driver uses this function to
 * send received frames to the IEEE 802.11 code. Receive buffer (skb) must
 * start with IEEE 802.11 header. */
void __ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb,
		    struct ieee80211_rx_status *status);
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw,
			  struct sk_buff *skb,
			  struct ieee80211_rx_status *status);

/* Transmit status callback function. The low-level driver must call this
 * function to report transmit status for all the TX frames that had
 * req_tx_status set in the transmit control fields. In addition, this should
 * be called at least for all unicast frames to provide information for TX rate
 * control algorithm. In order to maintain all statistics, this function is
 * recommended to be called after each frame, including multicast/broadcast, is
 * sent. */
void ieee80211_tx_status(struct ieee80211_hw *hw,
			 struct sk_buff *skb,
			 struct ieee80211_tx_status *status);
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb,
				 struct ieee80211_tx_status *status);

/**
 * ieee80211_beacon_get - beacon generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @if_id: interface ID from &struct ieee80211_if_init_conf.
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
				     int if_id,
				     struct ieee80211_tx_control *control);

/**
 * ieee80211_rts_get - RTS frame generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @frame: pointer to the frame that is going to be protected by the RTS.
 * @frame_len: the frame length (in octets).
 * @frame_txctl: &struct ieee80211_tx_control of the frame.
 * @rts: The buffer where to store the RTS frame.
 *
 * If the RTS frames are generated by the host system (i.e., not in
 * hardware/firmware), the low-level driver uses this function to receive
 * the next RTS frame from the 802.11 code. The low-level is responsible
 * for calling this function before and RTS frame is needed.
 */
void ieee80211_rts_get(struct ieee80211_hw *hw,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_control *frame_txctl,
		       struct ieee80211_rts *rts);

/**
 * ieee80211_rts_duration - Get the duration field for an RTS frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @frame_len: the length of the frame that is going to be protected by the RTS.
 * @frame_txctl: &struct ieee80211_tx_control of the frame.
 *
 * If the RTS is generated in firmware, but the host system must provide
 * the duration field, the low-level driver uses this function to receive
 * the duration field value in little-endian byteorder.
 */
__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      size_t frame_len,
			      const struct ieee80211_tx_control *frame_txctl);

/**
 * ieee80211_ctstoself_get - CTS-to-self frame generation function
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @frame: pointer to the frame that is going to be protected by the CTS-to-self.
 * @frame_len: the frame length (in octets).
 * @frame_txctl: &struct ieee80211_tx_control of the frame.
 * @cts: The buffer where to store the CTS-to-self frame.
 *
 * If the CTS-to-self frames are generated by the host system (i.e., not in
 * hardware/firmware), the low-level driver uses this function to receive
 * the next CTS-to-self frame from the 802.11 code. The low-level is responsible
 * for calling this function before and CTS-to-self frame is needed.
 */
void ieee80211_ctstoself_get(struct ieee80211_hw *hw,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_control *frame_txctl,
			     struct ieee80211_cts *cts);

/**
 * ieee80211_ctstoself_duration - Get the duration field for a CTS-to-self frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @frame_len: the length of the frame that is going to be protected by the CTS-to-self.
 * @frame_txctl: &struct ieee80211_tx_control of the frame.
 *
 * If the CTS-to-self is generated in firmware, but the host system must provide
 * the duration field, the low-level driver uses this function to receive
 * the duration field value in little-endian byteorder.
 */
__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    size_t frame_len,
				    const struct ieee80211_tx_control *frame_txctl);

/**
 * ieee80211_generic_frame_duration - Calculate the duration field for a frame
 * @hw: pointer obtained from ieee80211_alloc_hw().
 * @frame_len: the length of the frame.
 * @rate: the rate (in 100kbps) at which the frame is going to be transmitted.
 *
 * Calculate the duration field of some generic frame, given its
 * length and transmission rate (in 100kbps).
 */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					size_t frame_len,
					int rate);

/**
 * ieee80211_get_buffered_bc - accessing buffered broadcast and multicast frames
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @if_id: interface ID from &struct ieee80211_if_init_conf.
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
ieee80211_get_buffered_bc(struct ieee80211_hw *hw, int if_id,
			  struct ieee80211_tx_control *control);

/* Given an sk_buff with a raw 802.11 header at the data pointer this function
 * returns the 802.11 header length in bytes (not including encryption
 * headers). If the data in the sk_buff is too short to contain a valid 802.11
 * header the function returns 0.
 */
int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb);

/* Like ieee80211_get_hdrlen_from_skb() but takes a FC in CPU order. */
int ieee80211_get_hdrlen(u16 fc);

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
 * ieee80211_start_queues - start all queues
 * @hw: pointer to as obtained from ieee80211_alloc_hw().
 *
 * Drivers should use this function instead of netif_start_queue.
 */
void ieee80211_start_queues(struct ieee80211_hw *hw);

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
 * ieee80211_get_mc_list_item - iteration over items in multicast list
 * @hw: pointer as obtained from ieee80211_alloc_hw().
 * @prev: value returned by previous call to ieee80211_get_mc_list_item() or
 *	NULL to start a new iteration.
 * @ptr: pointer to buffer of void * type for internal usage of
 *	ieee80211_get_mc_list_item().
 *
 * Iterates over items in multicast list of given device. To get the first
 * item, pass NULL in @prev and in *@ptr. In subsequent calls, pass the
 * value returned by previous call in @prev. Don't alter *@ptr during
 * iteration. When there are no more items, NULL is returned.
 */
struct dev_mc_list *
ieee80211_get_mc_list_item(struct ieee80211_hw *hw,
			   struct dev_mc_list *prev,
			   void **ptr);

/* called by driver to notify scan status completed */
void ieee80211_scan_completed(struct ieee80211_hw *hw);

/* Function to indicate Radar Detection. The low level driver must call this
 * function to indicate the presence of radar in the current channel.
 * Additionally the radar type also could be sent */
int  ieee80211_radar_status(struct ieee80211_hw *hw, int channel,
			    int radar, int radar_type);

/* return a pointer to the source address (SA) */
static inline u8 *ieee80211_get_SA(struct ieee80211_hdr *hdr)
{
	u8 *raw = (u8 *) hdr;
	u8 tofrom = (*(raw+1)) & 3; /* get the TODS and FROMDS bits */

	switch (tofrom) {
		case 2:
			return hdr->addr3;
		case 3:
			return hdr->addr4;
	}
	return hdr->addr2;
}

/* return a pointer to the destination address (DA) */
static inline u8 *ieee80211_get_DA(struct ieee80211_hdr *hdr)
{
	u8 *raw = (u8 *) hdr;
	u8 to_ds = (*(raw+1)) & 1; /* get the TODS bit */

	if (to_ds)
		return hdr->addr3;
	return hdr->addr1;
}

static inline int ieee80211_get_morefrag(struct ieee80211_hdr *hdr)
{
	return (le16_to_cpu(hdr->frame_control) &
		IEEE80211_FCTL_MOREFRAGS) != 0;
}

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARG(x) ((u8*)(x))[0], ((u8*)(x))[1], ((u8*)(x))[2], \
		   ((u8*)(x))[3], ((u8*)(x))[4], ((u8*)(x))[5]

#endif /* MAC80211_H */
