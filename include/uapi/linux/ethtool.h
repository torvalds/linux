/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ethtool.h: Defines for Linux ethtool.
 *
 * Copyright (C) 1998 David S. Miller (davem@redhat.com)
 * Copyright 2001 Jeff Garzik <jgarzik@pobox.com>
 * Portions Copyright 2001 Sun Microsystems (thockin@sun.com)
 * Portions Copyright 2002 Intel (eli.kupermann@intel.com,
 *                                christopher.leech@intel.com,
 *                                scott.feldman@intel.com)
 * Portions Copyright (C) Sun Microsystems 2008
 */

#ifndef _UAPI_LINUX_ETHTOOL_H
#define _UAPI_LINUX_ETHTOOL_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/if_ether.h>

#ifndef __KERNEL__
#include <limits.h> /* for INT_MAX */
#endif

/* All structures exposed to userland should be defined such that they
 * have the same layout for 32-bit and 64-bit userland.
 */

/* Note on reserved space.
 * Reserved fields must not be accessed directly by user space because
 * they may be replaced by a different field in the future. They must
 * be initialized to zero before making the request, e.g. via memset
 * of the entire structure or implicitly by not being set in a structure
 * initializer.
 */

/**
 * struct ethtool_cmd - DEPRECATED, link control and status
 * This structure is DEPRECATED, please use struct ethtool_link_settings.
 * @cmd: Command number = %ETHTOOL_GSET or %ETHTOOL_SSET
 * @supported: Bitmask of %SUPPORTED_* flags for the link modes,
 *	physical connectors and other link features for which the
 *	interface supports autonegotiation or auto-detection.
 *	Read-only.
 * @advertising: Bitmask of %ADVERTISED_* flags for the link modes,
 *	physical connectors and other link features that are
 *	advertised through autonegotiation or enabled for
 *	auto-detection.
 * @speed: Low bits of the speed, 1Mb units, 0 to INT_MAX or SPEED_UNKNOWN
 * @duplex: Duplex mode; one of %DUPLEX_*
 * @port: Physical connector type; one of %PORT_*
 * @phy_address: MDIO address of PHY (transceiver); 0 or 255 if not
 *	applicable.  For clause 45 PHYs this is the PRTAD.
 * @transceiver: Historically used to distinguish different possible
 *	PHY types, but not in a consistent way.  Deprecated.
 * @autoneg: Enable/disable autonegotiation and auto-detection;
 *	either %AUTONEG_DISABLE or %AUTONEG_ENABLE
 * @mdio_support: Bitmask of %ETH_MDIO_SUPPORTS_* flags for the MDIO
 *	protocols supported by the interface; 0 if unknown.
 *	Read-only.
 * @maxtxpkt: Historically used to report TX IRQ coalescing; now
 *	obsoleted by &struct ethtool_coalesce.  Read-only; deprecated.
 * @maxrxpkt: Historically used to report RX IRQ coalescing; now
 *	obsoleted by &struct ethtool_coalesce.  Read-only; deprecated.
 * @speed_hi: High bits of the speed, 1Mb units, 0 to INT_MAX or SPEED_UNKNOWN
 * @eth_tp_mdix: Ethernet twisted-pair MDI(-X) status; one of
 *	%ETH_TP_MDI_*.  If the status is unknown or not applicable, the
 *	value will be %ETH_TP_MDI_INVALID.  Read-only.
 * @eth_tp_mdix_ctrl: Ethernet twisted pair MDI(-X) control; one of
 *	%ETH_TP_MDI_*.  If MDI(-X) control is not implemented, reads
 *	yield %ETH_TP_MDI_INVALID and writes may be ignored or rejected.
 *	When written successfully, the link should be renegotiated if
 *	necessary.
 * @lp_advertising: Bitmask of %ADVERTISED_* flags for the link modes
 *	and other link features that the link partner advertised
 *	through autonegotiation; 0 if unknown or not applicable.
 *	Read-only.
 * @reserved: Reserved for future use; see the note on reserved space.
 *
 * The link speed in Mbps is split between @speed and @speed_hi.  Use
 * the ethtool_cmd_speed() and ethtool_cmd_speed_set() functions to
 * access it.
 *
 * If autonegotiation is disabled, the speed and @duplex represent the
 * fixed link mode and are writable if the driver supports multiple
 * link modes.  If it is enabled then they are read-only; if the link
 * is up they represent the negotiated link mode; if the link is down,
 * the speed is 0, %SPEED_UNKNOWN or the highest enabled speed and
 * @duplex is %DUPLEX_UNKNOWN or the best enabled duplex mode.
 *
 * Some hardware interfaces may have multiple PHYs and/or physical
 * connectors fitted or do not allow the driver to detect which are
 * fitted.  For these interfaces @port and/or @phy_address may be
 * writable, possibly dependent on @autoneg being %AUTONEG_DISABLE.
 * Otherwise, attempts to write different values may be ignored or
 * rejected.
 *
 * Users should assume that all fields not marked read-only are
 * writable and subject to validation by the driver.  They should use
 * %ETHTOOL_GSET to get the current values before making specific
 * changes and then applying them with %ETHTOOL_SSET.
 *
 * Deprecated fields should be ignored by both users and drivers.
 */
struct ethtool_cmd {
	__u32	cmd;
	__u32	supported;
	__u32	advertising;
	__u16	speed;
	__u8	duplex;
	__u8	port;
	__u8	phy_address;
	__u8	transceiver;
	__u8	autoneg;
	__u8	mdio_support;
	__u32	maxtxpkt;
	__u32	maxrxpkt;
	__u16	speed_hi;
	__u8	eth_tp_mdix;
	__u8	eth_tp_mdix_ctrl;
	__u32	lp_advertising;
	__u32	reserved[2];
};

static inline void ethtool_cmd_speed_set(struct ethtool_cmd *ep,
					 __u32 speed)
{
	ep->speed = (__u16)(speed & 0xFFFF);
	ep->speed_hi = (__u16)(speed >> 16);
}

static inline __u32 ethtool_cmd_speed(const struct ethtool_cmd *ep)
{
	return (ep->speed_hi << 16) | ep->speed;
}

/* Device supports clause 22 register access to PHY or peripherals
 * using the interface defined in <linux/mii.h>.  This should not be
 * set if there are known to be no such peripherals present or if
 * the driver only emulates clause 22 registers for compatibility.
 */
#define ETH_MDIO_SUPPORTS_C22	1

/* Device supports clause 45 register access to PHY or peripherals
 * using the interface defined in <linux/mii.h> and <linux/mdio.h>.
 * This should not be set if there are known to be no such peripherals
 * present.
 */
#define ETH_MDIO_SUPPORTS_C45	2

#define ETHTOOL_FWVERS_LEN	32
#define ETHTOOL_BUSINFO_LEN	32
#define ETHTOOL_EROMVERS_LEN	32

/**
 * struct ethtool_drvinfo - general driver and device information
 * @cmd: Command number = %ETHTOOL_GDRVINFO
 * @driver: Driver short name.  This should normally match the name
 *	in its bus driver structure (e.g. pci_driver::name).  Must
 *	not be an empty string.
 * @version: Driver version string; may be an empty string
 * @fw_version: Firmware version string; may be an empty string
 * @erom_version: Expansion ROM version string; may be an empty string
 * @bus_info: Device bus address.  This should match the dev_name()
 *	string for the underlying bus device, if there is one.  May be
 *	an empty string.
 * @reserved2: Reserved for future use; see the note on reserved space.
 * @n_priv_flags: Number of flags valid for %ETHTOOL_GPFLAGS and
 *	%ETHTOOL_SPFLAGS commands; also the number of strings in the
 *	%ETH_SS_PRIV_FLAGS set
 * @n_stats: Number of u64 statistics returned by the %ETHTOOL_GSTATS
 *	command; also the number of strings in the %ETH_SS_STATS set
 * @testinfo_len: Number of results returned by the %ETHTOOL_TEST
 *	command; also the number of strings in the %ETH_SS_TEST set
 * @eedump_len: Size of EEPROM accessible through the %ETHTOOL_GEEPROM
 *	and %ETHTOOL_SEEPROM commands, in bytes
 * @regdump_len: Size of register dump returned by the %ETHTOOL_GREGS
 *	command, in bytes
 *
 * Users can use the %ETHTOOL_GSSET_INFO command to get the number of
 * strings in any string set (from Linux 2.6.34).
 *
 * Drivers should set at most @driver, @version, @fw_version and
 * @bus_info in their get_drvinfo() implementation.  The ethtool
 * core fills in the other fields using other driver operations.
 */
struct ethtool_drvinfo {
	__u32	cmd;
	char	driver[32];
	char	version[32];
	char	fw_version[ETHTOOL_FWVERS_LEN];
	char	bus_info[ETHTOOL_BUSINFO_LEN];
	char	erom_version[ETHTOOL_EROMVERS_LEN];
	char	reserved2[12];
	__u32	n_priv_flags;
	__u32	n_stats;
	__u32	testinfo_len;
	__u32	eedump_len;
	__u32	regdump_len;
};

#define SOPASS_MAX	6

/**
 * struct ethtool_wolinfo - Wake-On-Lan configuration
 * @cmd: Command number = %ETHTOOL_GWOL or %ETHTOOL_SWOL
 * @supported: Bitmask of %WAKE_* flags for supported Wake-On-Lan modes.
 *	Read-only.
 * @wolopts: Bitmask of %WAKE_* flags for enabled Wake-On-Lan modes.
 * @sopass: SecureOn(tm) password; meaningful only if %WAKE_MAGICSECURE
 *	is set in @wolopts.
 */
struct ethtool_wolinfo {
	__u32	cmd;
	__u32	supported;
	__u32	wolopts;
	__u8	sopass[SOPASS_MAX];
};

/* for passing single values */
struct ethtool_value {
	__u32	cmd;
	__u32	data;
};

#define PFC_STORM_PREVENTION_AUTO	0xffff
#define PFC_STORM_PREVENTION_DISABLE	0

enum tunable_id {
	ETHTOOL_ID_UNSPEC,
	ETHTOOL_RX_COPYBREAK,
	ETHTOOL_TX_COPYBREAK,
	ETHTOOL_PFC_PREVENTION_TOUT, /* timeout in msecs */
	/*
	 * Add your fresh new tunable attribute above and remember to update
	 * tunable_strings[] in net/ethtool/common.c
	 */
	__ETHTOOL_TUNABLE_COUNT,
};

enum tunable_type_id {
	ETHTOOL_TUNABLE_UNSPEC,
	ETHTOOL_TUNABLE_U8,
	ETHTOOL_TUNABLE_U16,
	ETHTOOL_TUNABLE_U32,
	ETHTOOL_TUNABLE_U64,
	ETHTOOL_TUNABLE_STRING,
	ETHTOOL_TUNABLE_S8,
	ETHTOOL_TUNABLE_S16,
	ETHTOOL_TUNABLE_S32,
	ETHTOOL_TUNABLE_S64,
};

struct ethtool_tunable {
	__u32	cmd;
	__u32	id;
	__u32	type_id;
	__u32	len;
	void	*data[0];
};

#define DOWNSHIFT_DEV_DEFAULT_COUNT	0xff
#define DOWNSHIFT_DEV_DISABLE		0

/* Time in msecs after which link is reported as down
 * 0 = lowest time supported by the PHY
 * 0xff = off, link down detection according to standard
 */
#define ETHTOOL_PHY_FAST_LINK_DOWN_ON	0
#define ETHTOOL_PHY_FAST_LINK_DOWN_OFF	0xff

/* Energy Detect Power Down (EDPD) is a feature supported by some PHYs, where
 * the PHY's RX & TX blocks are put into a low-power mode when there is no
 * link detected (typically cable is un-plugged). For RX, only a minimal
 * link-detection is available, and for TX the PHY wakes up to send link pulses
 * to avoid any lock-ups in case the peer PHY may also be running in EDPD mode.
 *
 * Some PHYs may support configuration of the wake-up interval for TX pulses,
 * and some PHYs may support only disabling TX pulses entirely. For the latter
 * a special value is required (ETHTOOL_PHY_EDPD_NO_TX) so that this can be
 * configured from userspace (should the user want it).
 *
 * The interval units for TX wake-up are in milliseconds, since this should
 * cover a reasonable range of intervals:
 *  - from 1 millisecond, which does not sound like much of a power-saver
 *  - to ~65 seconds which is quite a lot to wait for a link to come up when
 *    plugging a cable
 */
#define ETHTOOL_PHY_EDPD_DFLT_TX_MSECS		0xffff
#define ETHTOOL_PHY_EDPD_NO_TX			0xfffe
#define ETHTOOL_PHY_EDPD_DISABLE		0

enum phy_tunable_id {
	ETHTOOL_PHY_ID_UNSPEC,
	ETHTOOL_PHY_DOWNSHIFT,
	ETHTOOL_PHY_FAST_LINK_DOWN,
	ETHTOOL_PHY_EDPD,
	/*
	 * Add your fresh new phy tunable attribute above and remember to update
	 * phy_tunable_strings[] in net/ethtool/common.c
	 */
	__ETHTOOL_PHY_TUNABLE_COUNT,
};

/**
 * struct ethtool_regs - hardware register dump
 * @cmd: Command number = %ETHTOOL_GREGS
 * @version: Dump format version.  This is driver-specific and may
 *	distinguish different chips/revisions.  Drivers must use new
 *	version numbers whenever the dump format changes in an
 *	incompatible way.
 * @len: On entry, the real length of @data.  On return, the number of
 *	bytes used.
 * @data: Buffer for the register dump
 *
 * Users should use %ETHTOOL_GDRVINFO to find the maximum length of
 * a register dump for the interface.  They must allocate the buffer
 * immediately following this structure.
 */
struct ethtool_regs {
	__u32	cmd;
	__u32	version;
	__u32	len;
	__u8	data[0];
};

/**
 * struct ethtool_eeprom - EEPROM dump
 * @cmd: Command number = %ETHTOOL_GEEPROM, %ETHTOOL_GMODULEEEPROM or
 *	%ETHTOOL_SEEPROM
 * @magic: A 'magic cookie' value to guard against accidental changes.
 *	The value passed in to %ETHTOOL_SEEPROM must match the value
 *	returned by %ETHTOOL_GEEPROM for the same device.  This is
 *	unused when @cmd is %ETHTOOL_GMODULEEEPROM.
 * @offset: Offset within the EEPROM to begin reading/writing, in bytes
 * @len: On entry, number of bytes to read/write.  On successful
 *	return, number of bytes actually read/written.  In case of
 *	error, this may indicate at what point the error occurred.
 * @data: Buffer to read/write from
 *
 * Users may use %ETHTOOL_GDRVINFO or %ETHTOOL_GMODULEINFO to find
 * the length of an on-board or module EEPROM, respectively.  They
 * must allocate the buffer immediately following this structure.
 */
struct ethtool_eeprom {
	__u32	cmd;
	__u32	magic;
	__u32	offset;
	__u32	len;
	__u8	data[0];
};

/**
 * struct ethtool_eee - Energy Efficient Ethernet information
 * @cmd: ETHTOOL_{G,S}EEE
 * @supported: Mask of %SUPPORTED_* flags for the speed/duplex combinations
 *	for which there is EEE support.
 * @advertised: Mask of %ADVERTISED_* flags for the speed/duplex combinations
 *	advertised as eee capable.
 * @lp_advertised: Mask of %ADVERTISED_* flags for the speed/duplex
 *	combinations advertised by the link partner as eee capable.
 * @eee_active: Result of the eee auto negotiation.
 * @eee_enabled: EEE configured mode (enabled/disabled).
 * @tx_lpi_enabled: Whether the interface should assert its tx lpi, given
 *	that eee was negotiated.
 * @tx_lpi_timer: Time in microseconds the interface delays prior to asserting
 *	its tx lpi (after reaching 'idle' state). Effective only when eee
 *	was negotiated and tx_lpi_enabled was set.
 * @reserved: Reserved for future use; see the note on reserved space.
 */
struct ethtool_eee {
	__u32	cmd;
	__u32	supported;
	__u32	advertised;
	__u32	lp_advertised;
	__u32	eee_active;
	__u32	eee_enabled;
	__u32	tx_lpi_enabled;
	__u32	tx_lpi_timer;
	__u32	reserved[2];
};

/**
 * struct ethtool_modinfo - plugin module eeprom information
 * @cmd: %ETHTOOL_GMODULEINFO
 * @type: Standard the module information conforms to %ETH_MODULE_SFF_xxxx
 * @eeprom_len: Length of the eeprom
 * @reserved: Reserved for future use; see the note on reserved space.
 *
 * This structure is used to return the information to
 * properly size memory for a subsequent call to %ETHTOOL_GMODULEEEPROM.
 * The type code indicates the eeprom data format
 */
struct ethtool_modinfo {
	__u32   cmd;
	__u32   type;
	__u32   eeprom_len;
	__u32   reserved[8];
};

/**
 * struct ethtool_coalesce - coalescing parameters for IRQs and stats updates
 * @cmd: ETHTOOL_{G,S}COALESCE
 * @rx_coalesce_usecs: How many usecs to delay an RX interrupt after
 *	a packet arrives.
 * @rx_max_coalesced_frames: Maximum number of packets to receive
 *	before an RX interrupt.
 * @rx_coalesce_usecs_irq: Same as @rx_coalesce_usecs, except that
 *	this value applies while an IRQ is being serviced by the host.
 * @rx_max_coalesced_frames_irq: Same as @rx_max_coalesced_frames,
 *	except that this value applies while an IRQ is being serviced
 *	by the host.
 * @tx_coalesce_usecs: How many usecs to delay a TX interrupt after
 *	a packet is sent.
 * @tx_max_coalesced_frames: Maximum number of packets to be sent
 *	before a TX interrupt.
 * @tx_coalesce_usecs_irq: Same as @tx_coalesce_usecs, except that
 *	this value applies while an IRQ is being serviced by the host.
 * @tx_max_coalesced_frames_irq: Same as @tx_max_coalesced_frames,
 *	except that this value applies while an IRQ is being serviced
 *	by the host.
 * @stats_block_coalesce_usecs: How many usecs to delay in-memory
 *	statistics block updates.  Some drivers do not have an
 *	in-memory statistic block, and in such cases this value is
 *	ignored.  This value must not be zero.
 * @use_adaptive_rx_coalesce: Enable adaptive RX coalescing.
 * @use_adaptive_tx_coalesce: Enable adaptive TX coalescing.
 * @pkt_rate_low: Threshold for low packet rate (packets per second).
 * @rx_coalesce_usecs_low: How many usecs to delay an RX interrupt after
 *	a packet arrives, when the packet rate is below @pkt_rate_low.
 * @rx_max_coalesced_frames_low: Maximum number of packets to be received
 *	before an RX interrupt, when the packet rate is below @pkt_rate_low.
 * @tx_coalesce_usecs_low: How many usecs to delay a TX interrupt after
 *	a packet is sent, when the packet rate is below @pkt_rate_low.
 * @tx_max_coalesced_frames_low: Maximum nuumber of packets to be sent before
 *	a TX interrupt, when the packet rate is below @pkt_rate_low.
 * @pkt_rate_high: Threshold for high packet rate (packets per second).
 * @rx_coalesce_usecs_high: How many usecs to delay an RX interrupt after
 *	a packet arrives, when the packet rate is above @pkt_rate_high.
 * @rx_max_coalesced_frames_high: Maximum number of packets to be received
 *	before an RX interrupt, when the packet rate is above @pkt_rate_high.
 * @tx_coalesce_usecs_high: How many usecs to delay a TX interrupt after
 *	a packet is sent, when the packet rate is above @pkt_rate_high.
 * @tx_max_coalesced_frames_high: Maximum number of packets to be sent before
 *	a TX interrupt, when the packet rate is above @pkt_rate_high.
 * @rate_sample_interval: How often to do adaptive coalescing packet rate
 *	sampling, measured in seconds.  Must not be zero.
 *
 * Each pair of (usecs, max_frames) fields specifies that interrupts
 * should be coalesced until
 *	(usecs > 0 && time_since_first_completion >= usecs) ||
 *	(max_frames > 0 && completed_frames >= max_frames)
 *
 * It is illegal to set both usecs and max_frames to zero as this
 * would cause interrupts to never be generated.  To disable
 * coalescing, set usecs = 0 and max_frames = 1.
 *
 * Some implementations ignore the value of max_frames and use the
 * condition time_since_first_completion >= usecs
 *
 * This is deprecated.  Drivers for hardware that does not support
 * counting completions should validate that max_frames == !rx_usecs.
 *
 * Adaptive RX/TX coalescing is an algorithm implemented by some
 * drivers to improve latency under low packet rates and improve
 * throughput under high packet rates.  Some drivers only implement
 * one of RX or TX adaptive coalescing.  Anything not implemented by
 * the driver causes these values to be silently ignored.
 *
 * When the packet rate is below @pkt_rate_high but above
 * @pkt_rate_low (both measured in packets per second) the
 * normal {rx,tx}_* coalescing parameters are used.
 */
struct ethtool_coalesce {
	__u32	cmd;
	__u32	rx_coalesce_usecs;
	__u32	rx_max_coalesced_frames;
	__u32	rx_coalesce_usecs_irq;
	__u32	rx_max_coalesced_frames_irq;
	__u32	tx_coalesce_usecs;
	__u32	tx_max_coalesced_frames;
	__u32	tx_coalesce_usecs_irq;
	__u32	tx_max_coalesced_frames_irq;
	__u32	stats_block_coalesce_usecs;
	__u32	use_adaptive_rx_coalesce;
	__u32	use_adaptive_tx_coalesce;
	__u32	pkt_rate_low;
	__u32	rx_coalesce_usecs_low;
	__u32	rx_max_coalesced_frames_low;
	__u32	tx_coalesce_usecs_low;
	__u32	tx_max_coalesced_frames_low;
	__u32	pkt_rate_high;
	__u32	rx_coalesce_usecs_high;
	__u32	rx_max_coalesced_frames_high;
	__u32	tx_coalesce_usecs_high;
	__u32	tx_max_coalesced_frames_high;
	__u32	rate_sample_interval;
};

/**
 * struct ethtool_ringparam - RX/TX ring parameters
 * @cmd: Command number = %ETHTOOL_GRINGPARAM or %ETHTOOL_SRINGPARAM
 * @rx_max_pending: Maximum supported number of pending entries per
 *	RX ring.  Read-only.
 * @rx_mini_max_pending: Maximum supported number of pending entries
 *	per RX mini ring.  Read-only.
 * @rx_jumbo_max_pending: Maximum supported number of pending entries
 *	per RX jumbo ring.  Read-only.
 * @tx_max_pending: Maximum supported number of pending entries per
 *	TX ring.  Read-only.
 * @rx_pending: Current maximum number of pending entries per RX ring
 * @rx_mini_pending: Current maximum number of pending entries per RX
 *	mini ring
 * @rx_jumbo_pending: Current maximum number of pending entries per RX
 *	jumbo ring
 * @tx_pending: Current maximum supported number of pending entries
 *	per TX ring
 *
 * If the interface does not have separate RX mini and/or jumbo rings,
 * @rx_mini_max_pending and/or @rx_jumbo_max_pending will be 0.
 *
 * There may also be driver-dependent minimum values for the number
 * of entries per ring.
 */
struct ethtool_ringparam {
	__u32	cmd;
	__u32	rx_max_pending;
	__u32	rx_mini_max_pending;
	__u32	rx_jumbo_max_pending;
	__u32	tx_max_pending;
	__u32	rx_pending;
	__u32	rx_mini_pending;
	__u32	rx_jumbo_pending;
	__u32	tx_pending;
};

/**
 * struct ethtool_channels - configuring number of network channel
 * @cmd: ETHTOOL_{G,S}CHANNELS
 * @max_rx: Read only. Maximum number of receive channel the driver support.
 * @max_tx: Read only. Maximum number of transmit channel the driver support.
 * @max_other: Read only. Maximum number of other channel the driver support.
 * @max_combined: Read only. Maximum number of combined channel the driver
 *	support. Set of queues RX, TX or other.
 * @rx_count: Valid values are in the range 1 to the max_rx.
 * @tx_count: Valid values are in the range 1 to the max_tx.
 * @other_count: Valid values are in the range 1 to the max_other.
 * @combined_count: Valid values are in the range 1 to the max_combined.
 *
 * This can be used to configure RX, TX and other channels.
 */

struct ethtool_channels {
	__u32	cmd;
	__u32	max_rx;
	__u32	max_tx;
	__u32	max_other;
	__u32	max_combined;
	__u32	rx_count;
	__u32	tx_count;
	__u32	other_count;
	__u32	combined_count;
};

/**
 * struct ethtool_pauseparam - Ethernet pause (flow control) parameters
 * @cmd: Command number = %ETHTOOL_GPAUSEPARAM or %ETHTOOL_SPAUSEPARAM
 * @autoneg: Flag to enable autonegotiation of pause frame use
 * @rx_pause: Flag to enable reception of pause frames
 * @tx_pause: Flag to enable transmission of pause frames
 *
 * Drivers should reject a non-zero setting of @autoneg when
 * autoneogotiation is disabled (or not supported) for the link.
 *
 * If the link is autonegotiated, drivers should use
 * mii_advertise_flowctrl() or similar code to set the advertised
 * pause frame capabilities based on the @rx_pause and @tx_pause flags,
 * even if @autoneg is zero.  They should also allow the advertised
 * pause frame capabilities to be controlled directly through the
 * advertising field of &struct ethtool_cmd.
 *
 * If @autoneg is non-zero, the MAC is configured to send and/or
 * receive pause frames according to the result of autonegotiation.
 * Otherwise, it is configured directly based on the @rx_pause and
 * @tx_pause flags.
 */
struct ethtool_pauseparam {
	__u32	cmd;
	__u32	autoneg;
	__u32	rx_pause;
	__u32	tx_pause;
};

/* Link extended state */
enum ethtool_link_ext_state {
	ETHTOOL_LINK_EXT_STATE_AUTONEG,
	ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
	ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
	ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY,
	ETHTOOL_LINK_EXT_STATE_NO_CABLE,
	ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
	ETHTOOL_LINK_EXT_STATE_EEPROM_ISSUE,
	ETHTOOL_LINK_EXT_STATE_CALIBRATION_FAILURE,
	ETHTOOL_LINK_EXT_STATE_POWER_BUDGET_EXCEEDED,
	ETHTOOL_LINK_EXT_STATE_OVERHEAT,
};

/* More information in addition to ETHTOOL_LINK_EXT_STATE_AUTONEG. */
enum ethtool_link_ext_substate_autoneg {
	ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED = 1,
	ETHTOOL_LINK_EXT_SUBSTATE_AN_ACK_NOT_RECEIVED,
	ETHTOOL_LINK_EXT_SUBSTATE_AN_NEXT_PAGE_EXCHANGE_FAILED,
	ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED_FORCE_MODE,
	ETHTOOL_LINK_EXT_SUBSTATE_AN_FEC_MISMATCH_DURING_OVERRIDE,
	ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_HCD,
};

/* More information in addition to ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE.
 */
enum ethtool_link_ext_substate_link_training {
	ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_FRAME_LOCK_NOT_ACQUIRED = 1,
	ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_INHIBIT_TIMEOUT,
	ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_PARTNER_DID_NOT_SET_RECEIVER_READY,
	ETHTOOL_LINK_EXT_SUBSTATE_LT_REMOTE_FAULT,
};

/* More information in addition to ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH.
 */
enum ethtool_link_ext_substate_link_logical_mismatch {
	ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_BLOCK_LOCK = 1,
	ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_AM_LOCK,
	ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_GET_ALIGN_STATUS,
	ETHTOOL_LINK_EXT_SUBSTATE_LLM_FC_FEC_IS_NOT_LOCKED,
	ETHTOOL_LINK_EXT_SUBSTATE_LLM_RS_FEC_IS_NOT_LOCKED,
};

/* More information in addition to ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY.
 */
enum ethtool_link_ext_substate_bad_signal_integrity {
	ETHTOOL_LINK_EXT_SUBSTATE_BSI_LARGE_NUMBER_OF_PHYSICAL_ERRORS = 1,
	ETHTOOL_LINK_EXT_SUBSTATE_BSI_UNSUPPORTED_RATE,
};

/* More information in addition to ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE. */
enum ethtool_link_ext_substate_cable_issue {
	ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE = 1,
	ETHTOOL_LINK_EXT_SUBSTATE_CI_CABLE_TEST_FAILURE,
};

#define ETH_GSTRING_LEN		32

/**
 * enum ethtool_stringset - string set ID
 * @ETH_SS_TEST: Self-test result names, for use with %ETHTOOL_TEST
 * @ETH_SS_STATS: Statistic names, for use with %ETHTOOL_GSTATS
 * @ETH_SS_PRIV_FLAGS: Driver private flag names, for use with
 *	%ETHTOOL_GPFLAGS and %ETHTOOL_SPFLAGS
 * @ETH_SS_NTUPLE_FILTERS: Previously used with %ETHTOOL_GRXNTUPLE;
 *	now deprecated
 * @ETH_SS_FEATURES: Device feature names
 * @ETH_SS_RSS_HASH_FUNCS: RSS hush function names
 * @ETH_SS_TUNABLES: tunable names
 * @ETH_SS_PHY_STATS: Statistic names, for use with %ETHTOOL_GPHYSTATS
 * @ETH_SS_PHY_TUNABLES: PHY tunable names
 * @ETH_SS_LINK_MODES: link mode names
 * @ETH_SS_MSG_CLASSES: debug message class names
 * @ETH_SS_WOL_MODES: wake-on-lan modes
 * @ETH_SS_SOF_TIMESTAMPING: SOF_TIMESTAMPING_* flags
 * @ETH_SS_TS_TX_TYPES: timestamping Tx types
 * @ETH_SS_TS_RX_FILTERS: timestamping Rx filters
 * @ETH_SS_UDP_TUNNEL_TYPES: UDP tunnel types
 * @ETH_SS_STATS_STD: standardized stats
 * @ETH_SS_STATS_ETH_PHY: names of IEEE 802.3 PHY statistics
 * @ETH_SS_STATS_ETH_MAC: names of IEEE 802.3 MAC statistics
 * @ETH_SS_STATS_ETH_CTRL: names of IEEE 802.3 MAC Control statistics
 * @ETH_SS_STATS_RMON: names of RMON statistics
 *
 * @ETH_SS_COUNT: number of defined string sets
 */
enum ethtool_stringset {
	ETH_SS_TEST		= 0,
	ETH_SS_STATS,
	ETH_SS_PRIV_FLAGS,
	ETH_SS_NTUPLE_FILTERS,
	ETH_SS_FEATURES,
	ETH_SS_RSS_HASH_FUNCS,
	ETH_SS_TUNABLES,
	ETH_SS_PHY_STATS,
	ETH_SS_PHY_TUNABLES,
	ETH_SS_LINK_MODES,
	ETH_SS_MSG_CLASSES,
	ETH_SS_WOL_MODES,
	ETH_SS_SOF_TIMESTAMPING,
	ETH_SS_TS_TX_TYPES,
	ETH_SS_TS_RX_FILTERS,
	ETH_SS_UDP_TUNNEL_TYPES,
	ETH_SS_STATS_STD,
	ETH_SS_STATS_ETH_PHY,
	ETH_SS_STATS_ETH_MAC,
	ETH_SS_STATS_ETH_CTRL,
	ETH_SS_STATS_RMON,

	/* add new constants above here */
	ETH_SS_COUNT
};

/**
 * struct ethtool_gstrings - string set for data tagging
 * @cmd: Command number = %ETHTOOL_GSTRINGS
 * @string_set: String set ID; one of &enum ethtool_stringset
 * @len: On return, the number of strings in the string set
 * @data: Buffer for strings.  Each string is null-padded to a size of
 *	%ETH_GSTRING_LEN.
 *
 * Users must use %ETHTOOL_GSSET_INFO to find the number of strings in
 * the string set.  They must allocate a buffer of the appropriate
 * size immediately following this structure.
 */
struct ethtool_gstrings {
	__u32	cmd;
	__u32	string_set;
	__u32	len;
	__u8	data[0];
};

/**
 * struct ethtool_sset_info - string set information
 * @cmd: Command number = %ETHTOOL_GSSET_INFO
 * @reserved: Reserved for future use; see the note on reserved space.
 * @sset_mask: On entry, a bitmask of string sets to query, with bits
 *	numbered according to &enum ethtool_stringset.  On return, a
 *	bitmask of those string sets queried that are supported.
 * @data: Buffer for string set sizes.  On return, this contains the
 *	size of each string set that was queried and supported, in
 *	order of ID.
 *
 * Example: The user passes in @sset_mask = 0x7 (sets 0, 1, 2) and on
 * return @sset_mask == 0x6 (sets 1, 2).  Then @data[0] contains the
 * size of set 1 and @data[1] contains the size of set 2.
 *
 * Users must allocate a buffer of the appropriate size (4 * number of
 * sets queried) immediately following this structure.
 */
struct ethtool_sset_info {
	__u32	cmd;
	__u32	reserved;
	__u64	sset_mask;
	__u32	data[0];
};

/**
 * enum ethtool_test_flags - flags definition of ethtool_test
 * @ETH_TEST_FL_OFFLINE: if set perform online and offline tests, otherwise
 *	only online tests.
 * @ETH_TEST_FL_FAILED: Driver set this flag if test fails.
 * @ETH_TEST_FL_EXTERNAL_LB: Application request to perform external loopback
 *	test.
 * @ETH_TEST_FL_EXTERNAL_LB_DONE: Driver performed the external loopback test
 */

enum ethtool_test_flags {
	ETH_TEST_FL_OFFLINE	= (1 << 0),
	ETH_TEST_FL_FAILED	= (1 << 1),
	ETH_TEST_FL_EXTERNAL_LB	= (1 << 2),
	ETH_TEST_FL_EXTERNAL_LB_DONE	= (1 << 3),
};

/**
 * struct ethtool_test - device self-test invocation
 * @cmd: Command number = %ETHTOOL_TEST
 * @flags: A bitmask of flags from &enum ethtool_test_flags.  Some
 *	flags may be set by the user on entry; others may be set by
 *	the driver on return.
 * @reserved: Reserved for future use; see the note on reserved space.
 * @len: On return, the number of test results
 * @data: Array of test results
 *
 * Users must use %ETHTOOL_GSSET_INFO or %ETHTOOL_GDRVINFO to find the
 * number of test results that will be returned.  They must allocate a
 * buffer of the appropriate size (8 * number of results) immediately
 * following this structure.
 */
struct ethtool_test {
	__u32	cmd;
	__u32	flags;
	__u32	reserved;
	__u32	len;
	__u64	data[0];
};

/**
 * struct ethtool_stats - device-specific statistics
 * @cmd: Command number = %ETHTOOL_GSTATS
 * @n_stats: On return, the number of statistics
 * @data: Array of statistics
 *
 * Users must use %ETHTOOL_GSSET_INFO or %ETHTOOL_GDRVINFO to find the
 * number of statistics that will be returned.  They must allocate a
 * buffer of the appropriate size (8 * number of statistics)
 * immediately following this structure.
 */
struct ethtool_stats {
	__u32	cmd;
	__u32	n_stats;
	__u64	data[0];
};

/**
 * struct ethtool_perm_addr - permanent hardware address
 * @cmd: Command number = %ETHTOOL_GPERMADDR
 * @size: On entry, the size of the buffer.  On return, the size of the
 *	address.  The command fails if the buffer is too small.
 * @data: Buffer for the address
 *
 * Users must allocate the buffer immediately following this structure.
 * A buffer size of %MAX_ADDR_LEN should be sufficient for any address
 * type.
 */
struct ethtool_perm_addr {
	__u32	cmd;
	__u32	size;
	__u8	data[0];
};

/* boolean flags controlling per-interface behavior characteristics.
 * When reading, the flag indicates whether or not a certain behavior
 * is enabled/present.  When writing, the flag indicates whether
 * or not the driver should turn on (set) or off (clear) a behavior.
 *
 * Some behaviors may read-only (unconditionally absent or present).
 * If such is the case, return EINVAL in the set-flags operation if the
 * flag differs from the read-only value.
 */
enum ethtool_flags {
	ETH_FLAG_TXVLAN		= (1 << 7),	/* TX VLAN offload enabled */
	ETH_FLAG_RXVLAN		= (1 << 8),	/* RX VLAN offload enabled */
	ETH_FLAG_LRO		= (1 << 15),	/* LRO is enabled */
	ETH_FLAG_NTUPLE		= (1 << 27),	/* N-tuple filters enabled */
	ETH_FLAG_RXHASH		= (1 << 28),
};

/* The following structures are for supporting RX network flow
 * classification and RX n-tuple configuration. Note, all multibyte
 * fields, e.g., ip4src, ip4dst, psrc, pdst, spi, etc. are expected to
 * be in network byte order.
 */

/**
 * struct ethtool_tcpip4_spec - flow specification for TCP/IPv4 etc.
 * @ip4src: Source host
 * @ip4dst: Destination host
 * @psrc: Source port
 * @pdst: Destination port
 * @tos: Type-of-service
 *
 * This can be used to specify a TCP/IPv4, UDP/IPv4 or SCTP/IPv4 flow.
 */
struct ethtool_tcpip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be16	psrc;
	__be16	pdst;
	__u8    tos;
};

/**
 * struct ethtool_ah_espip4_spec - flow specification for IPsec/IPv4
 * @ip4src: Source host
 * @ip4dst: Destination host
 * @spi: Security parameters index
 * @tos: Type-of-service
 *
 * This can be used to specify an IPsec transport or tunnel over IPv4.
 */
struct ethtool_ah_espip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be32	spi;
	__u8    tos;
};

#define	ETH_RX_NFC_IP4	1

/**
 * struct ethtool_usrip4_spec - general flow specification for IPv4
 * @ip4src: Source host
 * @ip4dst: Destination host
 * @l4_4_bytes: First 4 bytes of transport (layer 4) header
 * @tos: Type-of-service
 * @ip_ver: Value must be %ETH_RX_NFC_IP4; mask must be 0
 * @proto: Transport protocol number; mask must be 0
 */
struct ethtool_usrip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be32	l4_4_bytes;
	__u8    tos;
	__u8    ip_ver;
	__u8    proto;
};

/**
 * struct ethtool_tcpip6_spec - flow specification for TCP/IPv6 etc.
 * @ip6src: Source host
 * @ip6dst: Destination host
 * @psrc: Source port
 * @pdst: Destination port
 * @tclass: Traffic Class
 *
 * This can be used to specify a TCP/IPv6, UDP/IPv6 or SCTP/IPv6 flow.
 */
struct ethtool_tcpip6_spec {
	__be32	ip6src[4];
	__be32	ip6dst[4];
	__be16	psrc;
	__be16	pdst;
	__u8    tclass;
};

/**
 * struct ethtool_ah_espip6_spec - flow specification for IPsec/IPv6
 * @ip6src: Source host
 * @ip6dst: Destination host
 * @spi: Security parameters index
 * @tclass: Traffic Class
 *
 * This can be used to specify an IPsec transport or tunnel over IPv6.
 */
struct ethtool_ah_espip6_spec {
	__be32	ip6src[4];
	__be32	ip6dst[4];
	__be32	spi;
	__u8    tclass;
};

/**
 * struct ethtool_usrip6_spec - general flow specification for IPv6
 * @ip6src: Source host
 * @ip6dst: Destination host
 * @l4_4_bytes: First 4 bytes of transport (layer 4) header
 * @tclass: Traffic Class
 * @l4_proto: Transport protocol number (nexthdr after any Extension Headers)
 */
struct ethtool_usrip6_spec {
	__be32	ip6src[4];
	__be32	ip6dst[4];
	__be32	l4_4_bytes;
	__u8    tclass;
	__u8    l4_proto;
};

union ethtool_flow_union {
	struct ethtool_tcpip4_spec		tcp_ip4_spec;
	struct ethtool_tcpip4_spec		udp_ip4_spec;
	struct ethtool_tcpip4_spec		sctp_ip4_spec;
	struct ethtool_ah_espip4_spec		ah_ip4_spec;
	struct ethtool_ah_espip4_spec		esp_ip4_spec;
	struct ethtool_usrip4_spec		usr_ip4_spec;
	struct ethtool_tcpip6_spec		tcp_ip6_spec;
	struct ethtool_tcpip6_spec		udp_ip6_spec;
	struct ethtool_tcpip6_spec		sctp_ip6_spec;
	struct ethtool_ah_espip6_spec		ah_ip6_spec;
	struct ethtool_ah_espip6_spec		esp_ip6_spec;
	struct ethtool_usrip6_spec		usr_ip6_spec;
	struct ethhdr				ether_spec;
	__u8					hdata[52];
};

/**
 * struct ethtool_flow_ext - additional RX flow fields
 * @h_dest: destination MAC address
 * @vlan_etype: VLAN EtherType
 * @vlan_tci: VLAN tag control information
 * @data: user defined data
 * @padding: Reserved for future use; see the note on reserved space.
 *
 * Note, @vlan_etype, @vlan_tci, and @data are only valid if %FLOW_EXT
 * is set in &struct ethtool_rx_flow_spec @flow_type.
 * @h_dest is valid if %FLOW_MAC_EXT is set.
 */
struct ethtool_flow_ext {
	__u8		padding[2];
	unsigned char	h_dest[ETH_ALEN];
	__be16		vlan_etype;
	__be16		vlan_tci;
	__be32		data[2];
};

/**
 * struct ethtool_rx_flow_spec - classification rule for RX flows
 * @flow_type: Type of match to perform, e.g. %TCP_V4_FLOW
 * @h_u: Flow fields to match (dependent on @flow_type)
 * @h_ext: Additional fields to match
 * @m_u: Masks for flow field bits to be matched
 * @m_ext: Masks for additional field bits to be matched
 *	Note, all additional fields must be ignored unless @flow_type
 *	includes the %FLOW_EXT or %FLOW_MAC_EXT flag
 *	(see &struct ethtool_flow_ext description).
 * @ring_cookie: RX ring/queue index to deliver to, or %RX_CLS_FLOW_DISC
 *	if packets should be discarded, or %RX_CLS_FLOW_WAKE if the
 *	packets should be used for Wake-on-LAN with %WAKE_FILTER
 * @location: Location of rule in the table.  Locations must be
 *	numbered such that a flow matching multiple rules will be
 *	classified according to the first (lowest numbered) rule.
 */
struct ethtool_rx_flow_spec {
	__u32		flow_type;
	union ethtool_flow_union h_u;
	struct ethtool_flow_ext h_ext;
	union ethtool_flow_union m_u;
	struct ethtool_flow_ext m_ext;
	__u64		ring_cookie;
	__u32		location;
};

/* How rings are laid out when accessing virtual functions or
 * offloaded queues is device specific. To allow users to do flow
 * steering and specify these queues the ring cookie is partitioned
 * into a 32bit queue index with an 8 bit virtual function id.
 * This also leaves the 3bytes for further specifiers. It is possible
 * future devices may support more than 256 virtual functions if
 * devices start supporting PCIe w/ARI. However at the moment I
 * do not know of any devices that support this so I do not reserve
 * space for this at this time. If a future patch consumes the next
 * byte it should be aware of this possibility.
 */
#define ETHTOOL_RX_FLOW_SPEC_RING	0x00000000FFFFFFFFLL
#define ETHTOOL_RX_FLOW_SPEC_RING_VF	0x000000FF00000000LL
#define ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF 32
static inline __u64 ethtool_get_flow_spec_ring(__u64 ring_cookie)
{
	return ETHTOOL_RX_FLOW_SPEC_RING & ring_cookie;
}

static inline __u64 ethtool_get_flow_spec_ring_vf(__u64 ring_cookie)
{
	return (ETHTOOL_RX_FLOW_SPEC_RING_VF & ring_cookie) >>
				ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
}

/**
 * struct ethtool_rxnfc - command to get or set RX flow classification rules
 * @cmd: Specific command number - %ETHTOOL_GRXFH, %ETHTOOL_SRXFH,
 *	%ETHTOOL_GRXRINGS, %ETHTOOL_GRXCLSRLCNT, %ETHTOOL_GRXCLSRULE,
 *	%ETHTOOL_GRXCLSRLALL, %ETHTOOL_SRXCLSRLDEL or %ETHTOOL_SRXCLSRLINS
 * @flow_type: Type of flow to be affected, e.g. %TCP_V4_FLOW
 * @data: Command-dependent value
 * @fs: Flow classification rule
 * @rss_context: RSS context to be affected
 * @rule_cnt: Number of rules to be affected
 * @rule_locs: Array of used rule locations
 *
 * For %ETHTOOL_GRXFH and %ETHTOOL_SRXFH, @data is a bitmask indicating
 * the fields included in the flow hash, e.g. %RXH_IP_SRC.  The following
 * structure fields must not be used, except that if @flow_type includes
 * the %FLOW_RSS flag, then @rss_context determines which RSS context to
 * act on.
 *
 * For %ETHTOOL_GRXRINGS, @data is set to the number of RX rings/queues
 * on return.
 *
 * For %ETHTOOL_GRXCLSRLCNT, @rule_cnt is set to the number of defined
 * rules on return.  If @data is non-zero on return then it is the
 * size of the rule table, plus the flag %RX_CLS_LOC_SPECIAL if the
 * driver supports any special location values.  If that flag is not
 * set in @data then special location values should not be used.
 *
 * For %ETHTOOL_GRXCLSRULE, @fs.@location specifies the location of an
 * existing rule on entry and @fs contains the rule on return; if
 * @fs.@flow_type includes the %FLOW_RSS flag, then @rss_context is
 * filled with the RSS context ID associated with the rule.
 *
 * For %ETHTOOL_GRXCLSRLALL, @rule_cnt specifies the array size of the
 * user buffer for @rule_locs on entry.  On return, @data is the size
 * of the rule table, @rule_cnt is the number of defined rules, and
 * @rule_locs contains the locations of the defined rules.  Drivers
 * must use the second parameter to get_rxnfc() instead of @rule_locs.
 *
 * For %ETHTOOL_SRXCLSRLINS, @fs specifies the rule to add or update.
 * @fs.@location either specifies the location to use or is a special
 * location value with %RX_CLS_LOC_SPECIAL flag set.  On return,
 * @fs.@location is the actual rule location.  If @fs.@flow_type
 * includes the %FLOW_RSS flag, @rss_context is the RSS context ID to
 * use for flow spreading traffic which matches this rule.  The value
 * from the rxfh indirection table will be added to @fs.@ring_cookie
 * to choose which ring to deliver to.
 *
 * For %ETHTOOL_SRXCLSRLDEL, @fs.@location specifies the location of an
 * existing rule on entry.
 *
 * A driver supporting the special location values for
 * %ETHTOOL_SRXCLSRLINS may add the rule at any suitable unused
 * location, and may remove a rule at a later location (lower
 * priority) that matches exactly the same set of flows.  The special
 * values are %RX_CLS_LOC_ANY, selecting any location;
 * %RX_CLS_LOC_FIRST, selecting the first suitable location (maximum
 * priority); and %RX_CLS_LOC_LAST, selecting the last suitable
 * location (minimum priority).  Additional special values may be
 * defined in future and drivers must return -%EINVAL for any
 * unrecognised value.
 */
struct ethtool_rxnfc {
	__u32				cmd;
	__u32				flow_type;
	__u64				data;
	struct ethtool_rx_flow_spec	fs;
	union {
		__u32			rule_cnt;
		__u32			rss_context;
	};
	__u32				rule_locs[0];
};


/**
 * struct ethtool_rxfh_indir - command to get or set RX flow hash indirection
 * @cmd: Specific command number - %ETHTOOL_GRXFHINDIR or %ETHTOOL_SRXFHINDIR
 * @size: On entry, the array size of the user buffer, which may be zero.
 *	On return from %ETHTOOL_GRXFHINDIR, the array size of the hardware
 *	indirection table.
 * @ring_index: RX ring/queue index for each hash value
 *
 * For %ETHTOOL_GRXFHINDIR, a @size of zero means that only the size
 * should be returned.  For %ETHTOOL_SRXFHINDIR, a @size of zero means
 * the table should be reset to default values.  This last feature
 * is not supported by the original implementations.
 */
struct ethtool_rxfh_indir {
	__u32	cmd;
	__u32	size;
	__u32	ring_index[0];
};

/**
 * struct ethtool_rxfh - command to get/set RX flow hash indir or/and hash key.
 * @cmd: Specific command number - %ETHTOOL_GRSSH or %ETHTOOL_SRSSH
 * @rss_context: RSS context identifier.  Context 0 is the default for normal
 *	traffic; other contexts can be referenced as the destination for RX flow
 *	classification rules.  %ETH_RXFH_CONTEXT_ALLOC is used with command
 *	%ETHTOOL_SRSSH to allocate a new RSS context; on return this field will
 *	contain the ID of the newly allocated context.
 * @indir_size: On entry, the array size of the user buffer for the
 *	indirection table, which may be zero, or (for %ETHTOOL_SRSSH),
 *	%ETH_RXFH_INDIR_NO_CHANGE.  On return from %ETHTOOL_GRSSH,
 *	the array size of the hardware indirection table.
 * @key_size: On entry, the array size of the user buffer for the hash key,
 *	which may be zero.  On return from %ETHTOOL_GRSSH, the size of the
 *	hardware hash key.
 * @hfunc: Defines the current RSS hash function used by HW (or to be set to).
 *	Valid values are one of the %ETH_RSS_HASH_*.
 * @rsvd8: Reserved for future use; see the note on reserved space.
 * @rsvd32: Reserved for future use; see the note on reserved space.
 * @rss_config: RX ring/queue index for each hash value i.e., indirection table
 *	of @indir_size __u32 elements, followed by hash key of @key_size
 *	bytes.
 *
 * For %ETHTOOL_GRSSH, a @indir_size and key_size of zero means that only the
 * size should be returned.  For %ETHTOOL_SRSSH, an @indir_size of
 * %ETH_RXFH_INDIR_NO_CHANGE means that indir table setting is not requested
 * and a @indir_size of zero means the indir table should be reset to default
 * values (if @rss_context == 0) or that the RSS context should be deleted.
 * An hfunc of zero means that hash function setting is not requested.
 */
struct ethtool_rxfh {
	__u32   cmd;
	__u32	rss_context;
	__u32   indir_size;
	__u32   key_size;
	__u8	hfunc;
	__u8	rsvd8[3];
	__u32	rsvd32;
	__u32   rss_config[0];
};
#define ETH_RXFH_CONTEXT_ALLOC		0xffffffff
#define ETH_RXFH_INDIR_NO_CHANGE	0xffffffff

/**
 * struct ethtool_rx_ntuple_flow_spec - specification for RX flow filter
 * @flow_type: Type of match to perform, e.g. %TCP_V4_FLOW
 * @h_u: Flow field values to match (dependent on @flow_type)
 * @m_u: Masks for flow field value bits to be ignored
 * @vlan_tag: VLAN tag to match
 * @vlan_tag_mask: Mask for VLAN tag bits to be ignored
 * @data: Driver-dependent data to match
 * @data_mask: Mask for driver-dependent data bits to be ignored
 * @action: RX ring/queue index to deliver to (non-negative) or other action
 *	(negative, e.g. %ETHTOOL_RXNTUPLE_ACTION_DROP)
 *
 * For flow types %TCP_V4_FLOW, %UDP_V4_FLOW and %SCTP_V4_FLOW, where
 * a field value and mask are both zero this is treated as if all mask
 * bits are set i.e. the field is ignored.
 */
struct ethtool_rx_ntuple_flow_spec {
	__u32		 flow_type;
	union {
		struct ethtool_tcpip4_spec		tcp_ip4_spec;
		struct ethtool_tcpip4_spec		udp_ip4_spec;
		struct ethtool_tcpip4_spec		sctp_ip4_spec;
		struct ethtool_ah_espip4_spec		ah_ip4_spec;
		struct ethtool_ah_espip4_spec		esp_ip4_spec;
		struct ethtool_usrip4_spec		usr_ip4_spec;
		struct ethhdr				ether_spec;
		__u8					hdata[72];
	} h_u, m_u;

	__u16	        vlan_tag;
	__u16	        vlan_tag_mask;
	__u64		data;
	__u64		data_mask;

	__s32		action;
#define ETHTOOL_RXNTUPLE_ACTION_DROP	(-1)	/* drop packet */
#define ETHTOOL_RXNTUPLE_ACTION_CLEAR	(-2)	/* clear filter */
};

/**
 * struct ethtool_rx_ntuple - command to set or clear RX flow filter
 * @cmd: Command number - %ETHTOOL_SRXNTUPLE
 * @fs: Flow filter specification
 */
struct ethtool_rx_ntuple {
	__u32					cmd;
	struct ethtool_rx_ntuple_flow_spec	fs;
};

#define ETHTOOL_FLASH_MAX_FILENAME	128
enum ethtool_flash_op_type {
	ETHTOOL_FLASH_ALL_REGIONS	= 0,
};

/* for passing firmware flashing related parameters */
struct ethtool_flash {
	__u32	cmd;
	__u32	region;
	char	data[ETHTOOL_FLASH_MAX_FILENAME];
};

/**
 * struct ethtool_dump - used for retrieving, setting device dump
 * @cmd: Command number - %ETHTOOL_GET_DUMP_FLAG, %ETHTOOL_GET_DUMP_DATA, or
 * 	%ETHTOOL_SET_DUMP
 * @version: FW version of the dump, filled in by driver
 * @flag: driver dependent flag for dump setting, filled in by driver during
 *        get and filled in by ethtool for set operation.
 *        flag must be initialized by macro ETH_FW_DUMP_DISABLE value when
 *        firmware dump is disabled.
 * @len: length of dump data, used as the length of the user buffer on entry to
 * 	 %ETHTOOL_GET_DUMP_DATA and this is returned as dump length by driver
 * 	 for %ETHTOOL_GET_DUMP_FLAG command
 * @data: data collected for get dump data operation
 */
struct ethtool_dump {
	__u32	cmd;
	__u32	version;
	__u32	flag;
	__u32	len;
	__u8	data[0];
};

#define ETH_FW_DUMP_DISABLE 0

/* for returning and changing feature sets */

/**
 * struct ethtool_get_features_block - block with state of 32 features
 * @available: mask of changeable features
 * @requested: mask of features requested to be enabled if possible
 * @active: mask of currently enabled features
 * @never_changed: mask of features not changeable for any device
 */
struct ethtool_get_features_block {
	__u32	available;
	__u32	requested;
	__u32	active;
	__u32	never_changed;
};

/**
 * struct ethtool_gfeatures - command to get state of device's features
 * @cmd: command number = %ETHTOOL_GFEATURES
 * @size: On entry, the number of elements in the features[] array;
 *	on return, the number of elements in features[] needed to hold
 *	all features
 * @features: state of features
 */
struct ethtool_gfeatures {
	__u32	cmd;
	__u32	size;
	struct ethtool_get_features_block features[0];
};

/**
 * struct ethtool_set_features_block - block with request for 32 features
 * @valid: mask of features to be changed
 * @requested: values of features to be changed
 */
struct ethtool_set_features_block {
	__u32	valid;
	__u32	requested;
};

/**
 * struct ethtool_sfeatures - command to request change in device's features
 * @cmd: command number = %ETHTOOL_SFEATURES
 * @size: array size of the features[] array
 * @features: feature change masks
 */
struct ethtool_sfeatures {
	__u32	cmd;
	__u32	size;
	struct ethtool_set_features_block features[0];
};

/**
 * struct ethtool_ts_info - holds a device's timestamping and PHC association
 * @cmd: command number = %ETHTOOL_GET_TS_INFO
 * @so_timestamping: bit mask of the sum of the supported SO_TIMESTAMPING flags
 * @phc_index: device index of the associated PHC, or -1 if there is none
 * @tx_types: bit mask of the supported hwtstamp_tx_types enumeration values
 * @tx_reserved: Reserved for future use; see the note on reserved space.
 * @rx_filters: bit mask of the supported hwtstamp_rx_filters enumeration values
 * @rx_reserved: Reserved for future use; see the note on reserved space.
 *
 * The bits in the 'tx_types' and 'rx_filters' fields correspond to
 * the 'hwtstamp_tx_types' and 'hwtstamp_rx_filters' enumeration values,
 * respectively.  For example, if the device supports HWTSTAMP_TX_ON,
 * then (1 << HWTSTAMP_TX_ON) in 'tx_types' will be set.
 *
 * Drivers should only report the filters they actually support without
 * upscaling in the SIOCSHWTSTAMP ioctl. If the SIOCSHWSTAMP request for
 * HWTSTAMP_FILTER_V1_SYNC is supported by HWTSTAMP_FILTER_V1_EVENT, then the
 * driver should only report HWTSTAMP_FILTER_V1_EVENT in this op.
 */
struct ethtool_ts_info {
	__u32	cmd;
	__u32	so_timestamping;
	__s32	phc_index;
	__u32	tx_types;
	__u32	tx_reserved[3];
	__u32	rx_filters;
	__u32	rx_reserved[3];
};

/*
 * %ETHTOOL_SFEATURES changes features present in features[].valid to the
 * values of corresponding bits in features[].requested. Bits in .requested
 * not set in .valid or not changeable are ignored.
 *
 * Returns %EINVAL when .valid contains undefined or never-changeable bits
 * or size is not equal to required number of features words (32-bit blocks).
 * Returns >= 0 if request was completed; bits set in the value mean:
 *   %ETHTOOL_F_UNSUPPORTED - there were bits set in .valid that are not
 *	changeable (not present in %ETHTOOL_GFEATURES' features[].available)
 *	those bits were ignored.
 *   %ETHTOOL_F_WISH - some or all changes requested were recorded but the
 *      resulting state of bits masked by .valid is not equal to .requested.
 *      Probably there are other device-specific constraints on some features
 *      in the set. When %ETHTOOL_F_UNSUPPORTED is set, .valid is considered
 *      here as though ignored bits were cleared.
 *   %ETHTOOL_F_COMPAT - some or all changes requested were made by calling
 *      compatibility functions. Requested offload state cannot be properly
 *      managed by kernel.
 *
 * Meaning of bits in the masks are obtained by %ETHTOOL_GSSET_INFO (number of
 * bits in the arrays - always multiple of 32) and %ETHTOOL_GSTRINGS commands
 * for ETH_SS_FEATURES string set. First entry in the table corresponds to least
 * significant bit in features[0] fields. Empty strings mark undefined features.
 */
enum ethtool_sfeatures_retval_bits {
	ETHTOOL_F_UNSUPPORTED__BIT,
	ETHTOOL_F_WISH__BIT,
	ETHTOOL_F_COMPAT__BIT,
};

#define ETHTOOL_F_UNSUPPORTED   (1 << ETHTOOL_F_UNSUPPORTED__BIT)
#define ETHTOOL_F_WISH          (1 << ETHTOOL_F_WISH__BIT)
#define ETHTOOL_F_COMPAT        (1 << ETHTOOL_F_COMPAT__BIT)

#define MAX_NUM_QUEUE		4096

/**
 * struct ethtool_per_queue_op - apply sub command to the queues in mask.
 * @cmd: ETHTOOL_PERQUEUE
 * @sub_command: the sub command which apply to each queues
 * @queue_mask: Bitmap of the queues which sub command apply to
 * @data: A complete command structure following for each of the queues addressed
 */
struct ethtool_per_queue_op {
	__u32	cmd;
	__u32	sub_command;
	__u32	queue_mask[__KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32)];
	char	data[];
};

/**
 * struct ethtool_fecparam - Ethernet Forward Error Correction parameters
 * @cmd: Command number = %ETHTOOL_GFECPARAM or %ETHTOOL_SFECPARAM
 * @active_fec: FEC mode which is active on the port, single bit set, GET only.
 * @fec: Bitmask of configured FEC modes.
 * @reserved: Reserved for future extensions, ignore on GET, write 0 for SET.
 *
 * Note that @reserved was never validated on input and ethtool user space
 * left it uninitialized when calling SET. Hence going forward it can only be
 * used to return a value to userspace with GET.
 *
 * FEC modes supported by the device can be read via %ETHTOOL_GLINKSETTINGS.
 * FEC settings are configured by link autonegotiation whenever it's enabled.
 * With autoneg on %ETHTOOL_GFECPARAM can be used to read the current mode.
 *
 * When autoneg is disabled %ETHTOOL_SFECPARAM controls the FEC settings.
 * It is recommended that drivers only accept a single bit set in @fec.
 * When multiple bits are set in @fec drivers may pick mode in an implementation
 * dependent way. Drivers should reject mixing %ETHTOOL_FEC_AUTO_BIT with other
 * FEC modes, because it's unclear whether in this case other modes constrain
 * AUTO or are independent choices.
 * Drivers must reject SET requests if they support none of the requested modes.
 *
 * If device does not support FEC drivers may use %ETHTOOL_FEC_NONE instead
 * of returning %EOPNOTSUPP from %ETHTOOL_GFECPARAM.
 *
 * See enum ethtool_fec_config_bits for definition of valid bits for both
 * @fec and @active_fec.
 */
struct ethtool_fecparam {
	__u32   cmd;
	/* bitmask of FEC modes */
	__u32   active_fec;
	__u32   fec;
	__u32   reserved;
};

/**
 * enum ethtool_fec_config_bits - flags definition of ethtool_fec_configuration
 * @ETHTOOL_FEC_NONE_BIT: FEC mode configuration is not supported. Should not
 *			be used together with other bits. GET only.
 * @ETHTOOL_FEC_AUTO_BIT: Select default/best FEC mode automatically, usually
 *			based link mode and SFP parameters read from module's
 *			EEPROM. This bit does _not_ mean autonegotiation.
 * @ETHTOOL_FEC_OFF_BIT: No FEC Mode
 * @ETHTOOL_FEC_RS_BIT: Reed-Solomon FEC Mode
 * @ETHTOOL_FEC_BASER_BIT: Base-R/Reed-Solomon FEC Mode
 * @ETHTOOL_FEC_LLRS_BIT: Low Latency Reed Solomon FEC Mode (25G/50G Ethernet
 *			Consortium)
 */
enum ethtool_fec_config_bits {
	ETHTOOL_FEC_NONE_BIT,
	ETHTOOL_FEC_AUTO_BIT,
	ETHTOOL_FEC_OFF_BIT,
	ETHTOOL_FEC_RS_BIT,
	ETHTOOL_FEC_BASER_BIT,
	ETHTOOL_FEC_LLRS_BIT,
};

#define ETHTOOL_FEC_NONE		(1 << ETHTOOL_FEC_NONE_BIT)
#define ETHTOOL_FEC_AUTO		(1 << ETHTOOL_FEC_AUTO_BIT)
#define ETHTOOL_FEC_OFF			(1 << ETHTOOL_FEC_OFF_BIT)
#define ETHTOOL_FEC_RS			(1 << ETHTOOL_FEC_RS_BIT)
#define ETHTOOL_FEC_BASER		(1 << ETHTOOL_FEC_BASER_BIT)
#define ETHTOOL_FEC_LLRS		(1 << ETHTOOL_FEC_LLRS_BIT)

/* CMDs currently supported */
#define ETHTOOL_GSET		0x00000001 /* DEPRECATED, Get settings.
					    * Please use ETHTOOL_GLINKSETTINGS
					    */
#define ETHTOOL_SSET		0x00000002 /* DEPRECATED, Set settings.
					    * Please use ETHTOOL_SLINKSETTINGS
					    */
#define ETHTOOL_GDRVINFO	0x00000003 /* Get driver info. */
#define ETHTOOL_GREGS		0x00000004 /* Get NIC registers. */
#define ETHTOOL_GWOL		0x00000005 /* Get wake-on-lan options. */
#define ETHTOOL_SWOL		0x00000006 /* Set wake-on-lan options. */
#define ETHTOOL_GMSGLVL		0x00000007 /* Get driver message level */
#define ETHTOOL_SMSGLVL		0x00000008 /* Set driver msg level. */
#define ETHTOOL_NWAY_RST	0x00000009 /* Restart autonegotiation. */
/* Get link status for host, i.e. whether the interface *and* the
 * physical port (if there is one) are up (ethtool_value). */
#define ETHTOOL_GLINK		0x0000000a
#define ETHTOOL_GEEPROM		0x0000000b /* Get EEPROM data */
#define ETHTOOL_SEEPROM		0x0000000c /* Set EEPROM data. */
#define ETHTOOL_GCOALESCE	0x0000000e /* Get coalesce config */
#define ETHTOOL_SCOALESCE	0x0000000f /* Set coalesce config. */
#define ETHTOOL_GRINGPARAM	0x00000010 /* Get ring parameters */
#define ETHTOOL_SRINGPARAM	0x00000011 /* Set ring parameters. */
#define ETHTOOL_GPAUSEPARAM	0x00000012 /* Get pause parameters */
#define ETHTOOL_SPAUSEPARAM	0x00000013 /* Set pause parameters. */
#define ETHTOOL_GRXCSUM		0x00000014 /* Get RX hw csum enable (ethtool_value) */
#define ETHTOOL_SRXCSUM		0x00000015 /* Set RX hw csum enable (ethtool_value) */
#define ETHTOOL_GTXCSUM		0x00000016 /* Get TX hw csum enable (ethtool_value) */
#define ETHTOOL_STXCSUM		0x00000017 /* Set TX hw csum enable (ethtool_value) */
#define ETHTOOL_GSG		0x00000018 /* Get scatter-gather enable
					    * (ethtool_value) */
#define ETHTOOL_SSG		0x00000019 /* Set scatter-gather enable
					    * (ethtool_value). */
#define ETHTOOL_TEST		0x0000001a /* execute NIC self-test. */
#define ETHTOOL_GSTRINGS	0x0000001b /* get specified string set */
#define ETHTOOL_PHYS_ID		0x0000001c /* identify the NIC */
#define ETHTOOL_GSTATS		0x0000001d /* get NIC-specific statistics */
#define ETHTOOL_GTSO		0x0000001e /* Get TSO enable (ethtool_value) */
#define ETHTOOL_STSO		0x0000001f /* Set TSO enable (ethtool_value) */
#define ETHTOOL_GPERMADDR	0x00000020 /* Get permanent hardware address */
#define ETHTOOL_GUFO		0x00000021 /* Get UFO enable (ethtool_value) */
#define ETHTOOL_SUFO		0x00000022 /* Set UFO enable (ethtool_value) */
#define ETHTOOL_GGSO		0x00000023 /* Get GSO enable (ethtool_value) */
#define ETHTOOL_SGSO		0x00000024 /* Set GSO enable (ethtool_value) */
#define ETHTOOL_GFLAGS		0x00000025 /* Get flags bitmap(ethtool_value) */
#define ETHTOOL_SFLAGS		0x00000026 /* Set flags bitmap(ethtool_value) */
#define ETHTOOL_GPFLAGS		0x00000027 /* Get driver-private flags bitmap */
#define ETHTOOL_SPFLAGS		0x00000028 /* Set driver-private flags bitmap */

#define ETHTOOL_GRXFH		0x00000029 /* Get RX flow hash configuration */
#define ETHTOOL_SRXFH		0x0000002a /* Set RX flow hash configuration */
#define ETHTOOL_GGRO		0x0000002b /* Get GRO enable (ethtool_value) */
#define ETHTOOL_SGRO		0x0000002c /* Set GRO enable (ethtool_value) */
#define ETHTOOL_GRXRINGS	0x0000002d /* Get RX rings available for LB */
#define ETHTOOL_GRXCLSRLCNT	0x0000002e /* Get RX class rule count */
#define ETHTOOL_GRXCLSRULE	0x0000002f /* Get RX classification rule */
#define ETHTOOL_GRXCLSRLALL	0x00000030 /* Get all RX classification rule */
#define ETHTOOL_SRXCLSRLDEL	0x00000031 /* Delete RX classification rule */
#define ETHTOOL_SRXCLSRLINS	0x00000032 /* Insert RX classification rule */
#define ETHTOOL_FLASHDEV	0x00000033 /* Flash firmware to device */
#define ETHTOOL_RESET		0x00000034 /* Reset hardware */
#define ETHTOOL_SRXNTUPLE	0x00000035 /* Add an n-tuple filter to device */
#define ETHTOOL_GRXNTUPLE	0x00000036 /* deprecated */
#define ETHTOOL_GSSET_INFO	0x00000037 /* Get string set info */
#define ETHTOOL_GRXFHINDIR	0x00000038 /* Get RX flow hash indir'n table */
#define ETHTOOL_SRXFHINDIR	0x00000039 /* Set RX flow hash indir'n table */

#define ETHTOOL_GFEATURES	0x0000003a /* Get device offload settings */
#define ETHTOOL_SFEATURES	0x0000003b /* Change device offload settings */
#define ETHTOOL_GCHANNELS	0x0000003c /* Get no of channels */
#define ETHTOOL_SCHANNELS	0x0000003d /* Set no of channels */
#define ETHTOOL_SET_DUMP	0x0000003e /* Set dump settings */
#define ETHTOOL_GET_DUMP_FLAG	0x0000003f /* Get dump settings */
#define ETHTOOL_GET_DUMP_DATA	0x00000040 /* Get dump data */
#define ETHTOOL_GET_TS_INFO	0x00000041 /* Get time stamping and PHC info */
#define ETHTOOL_GMODULEINFO	0x00000042 /* Get plug-in module information */
#define ETHTOOL_GMODULEEEPROM	0x00000043 /* Get plug-in module eeprom */
#define ETHTOOL_GEEE		0x00000044 /* Get EEE settings */
#define ETHTOOL_SEEE		0x00000045 /* Set EEE settings */

#define ETHTOOL_GRSSH		0x00000046 /* Get RX flow hash configuration */
#define ETHTOOL_SRSSH		0x00000047 /* Set RX flow hash configuration */
#define ETHTOOL_GTUNABLE	0x00000048 /* Get tunable configuration */
#define ETHTOOL_STUNABLE	0x00000049 /* Set tunable configuration */
#define ETHTOOL_GPHYSTATS	0x0000004a /* get PHY-specific statistics */

#define ETHTOOL_PERQUEUE	0x0000004b /* Set per queue options */

#define ETHTOOL_GLINKSETTINGS	0x0000004c /* Get ethtool_link_settings */
#define ETHTOOL_SLINKSETTINGS	0x0000004d /* Set ethtool_link_settings */
#define ETHTOOL_PHY_GTUNABLE	0x0000004e /* Get PHY tunable configuration */
#define ETHTOOL_PHY_STUNABLE	0x0000004f /* Set PHY tunable configuration */
#define ETHTOOL_GFECPARAM	0x00000050 /* Get FEC settings */
#define ETHTOOL_SFECPARAM	0x00000051 /* Set FEC settings */

/* compatibility with older code */
#define SPARC_ETH_GSET		ETHTOOL_GSET
#define SPARC_ETH_SSET		ETHTOOL_SSET

/* Link mode bit indices */
enum ethtool_link_mode_bit_indices {
	ETHTOOL_LINK_MODE_10baseT_Half_BIT	= 0,
	ETHTOOL_LINK_MODE_10baseT_Full_BIT	= 1,
	ETHTOOL_LINK_MODE_100baseT_Half_BIT	= 2,
	ETHTOOL_LINK_MODE_100baseT_Full_BIT	= 3,
	ETHTOOL_LINK_MODE_1000baseT_Half_BIT	= 4,
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT	= 5,
	ETHTOOL_LINK_MODE_Autoneg_BIT		= 6,
	ETHTOOL_LINK_MODE_TP_BIT		= 7,
	ETHTOOL_LINK_MODE_AUI_BIT		= 8,
	ETHTOOL_LINK_MODE_MII_BIT		= 9,
	ETHTOOL_LINK_MODE_FIBRE_BIT		= 10,
	ETHTOOL_LINK_MODE_BNC_BIT		= 11,
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT	= 12,
	ETHTOOL_LINK_MODE_Pause_BIT		= 13,
	ETHTOOL_LINK_MODE_Asym_Pause_BIT	= 14,
	ETHTOOL_LINK_MODE_2500baseX_Full_BIT	= 15,
	ETHTOOL_LINK_MODE_Backplane_BIT		= 16,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT	= 17,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT	= 18,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT	= 19,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT	= 20,
	ETHTOOL_LINK_MODE_20000baseMLD2_Full_BIT = 21,
	ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT	= 22,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT	= 23,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT	= 24,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT	= 25,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT	= 26,
	ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT	= 27,
	ETHTOOL_LINK_MODE_56000baseCR4_Full_BIT	= 28,
	ETHTOOL_LINK_MODE_56000baseSR4_Full_BIT	= 29,
	ETHTOOL_LINK_MODE_56000baseLR4_Full_BIT	= 30,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT	= 31,

	/* Last allowed bit for __ETHTOOL_LINK_MODE_LEGACY_MASK is bit
	 * 31. Please do NOT define any SUPPORTED_* or ADVERTISED_*
	 * macro for bits > 31. The only way to use indices > 31 is to
	 * use the new ETHTOOL_GLINKSETTINGS/ETHTOOL_SLINKSETTINGS API.
	 */

	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT	= 32,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT	= 33,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT	= 34,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT	= 35,
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT	= 36,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT	= 37,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT	= 38,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT	= 39,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT		= 40,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT	= 41,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT	= 42,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT	= 43,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT	= 44,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT	= 45,
	ETHTOOL_LINK_MODE_10000baseER_Full_BIT	= 46,
	ETHTOOL_LINK_MODE_2500baseT_Full_BIT	= 47,
	ETHTOOL_LINK_MODE_5000baseT_Full_BIT	= 48,

	ETHTOOL_LINK_MODE_FEC_NONE_BIT	= 49,
	ETHTOOL_LINK_MODE_FEC_RS_BIT	= 50,
	ETHTOOL_LINK_MODE_FEC_BASER_BIT	= 51,
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT		 = 52,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT		 = 53,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT		 = 54,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT	 = 55,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT		 = 56,
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT	 = 57,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT	 = 58,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT	 = 59,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT = 60,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT	 = 61,
	ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT	 = 62,
	ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT	 = 63,
	ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT = 64,
	ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT	 = 65,
	ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT	 = 66,
	ETHTOOL_LINK_MODE_100baseT1_Full_BIT		 = 67,
	ETHTOOL_LINK_MODE_1000baseT1_Full_BIT		 = 68,
	ETHTOOL_LINK_MODE_400000baseKR8_Full_BIT	 = 69,
	ETHTOOL_LINK_MODE_400000baseSR8_Full_BIT	 = 70,
	ETHTOOL_LINK_MODE_400000baseLR8_ER8_FR8_Full_BIT = 71,
	ETHTOOL_LINK_MODE_400000baseDR8_Full_BIT	 = 72,
	ETHTOOL_LINK_MODE_400000baseCR8_Full_BIT	 = 73,
	ETHTOOL_LINK_MODE_FEC_LLRS_BIT			 = 74,
	ETHTOOL_LINK_MODE_100000baseKR_Full_BIT		 = 75,
	ETHTOOL_LINK_MODE_100000baseSR_Full_BIT		 = 76,
	ETHTOOL_LINK_MODE_100000baseLR_ER_FR_Full_BIT	 = 77,
	ETHTOOL_LINK_MODE_100000baseCR_Full_BIT		 = 78,
	ETHTOOL_LINK_MODE_100000baseDR_Full_BIT		 = 79,
	ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT	 = 80,
	ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT	 = 81,
	ETHTOOL_LINK_MODE_200000baseLR2_ER2_FR2_Full_BIT = 82,
	ETHTOOL_LINK_MODE_200000baseDR2_Full_BIT	 = 83,
	ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT	 = 84,
	ETHTOOL_LINK_MODE_400000baseKR4_Full_BIT	 = 85,
	ETHTOOL_LINK_MODE_400000baseSR4_Full_BIT	 = 86,
	ETHTOOL_LINK_MODE_400000baseLR4_ER4_FR4_Full_BIT = 87,
	ETHTOOL_LINK_MODE_400000baseDR4_Full_BIT	 = 88,
	ETHTOOL_LINK_MODE_400000baseCR4_Full_BIT	 = 89,
	ETHTOOL_LINK_MODE_100baseFX_Half_BIT		 = 90,
	ETHTOOL_LINK_MODE_100baseFX_Full_BIT		 = 91,
	/* must be last entry */
	__ETHTOOL_LINK_MODE_MASK_NBITS
};

#define __ETHTOOL_LINK_MODE_LEGACY_MASK(base_name)	\
	(1UL << (ETHTOOL_LINK_MODE_ ## base_name ## _BIT))

/* DEPRECATED macros. Please migrate to
 * ETHTOOL_GLINKSETTINGS/ETHTOOL_SLINKSETTINGS API. Please do NOT
 * define any new SUPPORTED_* macro for bits > 31.
 */
#define SUPPORTED_10baseT_Half		__ETHTOOL_LINK_MODE_LEGACY_MASK(10baseT_Half)
#define SUPPORTED_10baseT_Full		__ETHTOOL_LINK_MODE_LEGACY_MASK(10baseT_Full)
#define SUPPORTED_100baseT_Half		__ETHTOOL_LINK_MODE_LEGACY_MASK(100baseT_Half)
#define SUPPORTED_100baseT_Full		__ETHTOOL_LINK_MODE_LEGACY_MASK(100baseT_Full)
#define SUPPORTED_1000baseT_Half	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseT_Half)
#define SUPPORTED_1000baseT_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseT_Full)
#define SUPPORTED_Autoneg		__ETHTOOL_LINK_MODE_LEGACY_MASK(Autoneg)
#define SUPPORTED_TP			__ETHTOOL_LINK_MODE_LEGACY_MASK(TP)
#define SUPPORTED_AUI			__ETHTOOL_LINK_MODE_LEGACY_MASK(AUI)
#define SUPPORTED_MII			__ETHTOOL_LINK_MODE_LEGACY_MASK(MII)
#define SUPPORTED_FIBRE			__ETHTOOL_LINK_MODE_LEGACY_MASK(FIBRE)
#define SUPPORTED_BNC			__ETHTOOL_LINK_MODE_LEGACY_MASK(BNC)
#define SUPPORTED_10000baseT_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseT_Full)
#define SUPPORTED_Pause			__ETHTOOL_LINK_MODE_LEGACY_MASK(Pause)
#define SUPPORTED_Asym_Pause		__ETHTOOL_LINK_MODE_LEGACY_MASK(Asym_Pause)
#define SUPPORTED_2500baseX_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(2500baseX_Full)
#define SUPPORTED_Backplane		__ETHTOOL_LINK_MODE_LEGACY_MASK(Backplane)
#define SUPPORTED_1000baseKX_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseKX_Full)
#define SUPPORTED_10000baseKX4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseKX4_Full)
#define SUPPORTED_10000baseKR_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseKR_Full)
#define SUPPORTED_10000baseR_FEC	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseR_FEC)
#define SUPPORTED_20000baseMLD2_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(20000baseMLD2_Full)
#define SUPPORTED_20000baseKR2_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(20000baseKR2_Full)
#define SUPPORTED_40000baseKR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseKR4_Full)
#define SUPPORTED_40000baseCR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseCR4_Full)
#define SUPPORTED_40000baseSR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseSR4_Full)
#define SUPPORTED_40000baseLR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseLR4_Full)
#define SUPPORTED_56000baseKR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseKR4_Full)
#define SUPPORTED_56000baseCR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseCR4_Full)
#define SUPPORTED_56000baseSR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseSR4_Full)
#define SUPPORTED_56000baseLR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseLR4_Full)
/* Please do not define any new SUPPORTED_* macro for bits > 31, see
 * notice above.
 */

/*
 * DEPRECATED macros. Please migrate to
 * ETHTOOL_GLINKSETTINGS/ETHTOOL_SLINKSETTINGS API. Please do NOT
 * define any new ADERTISE_* macro for bits > 31.
 */
#define ADVERTISED_10baseT_Half		__ETHTOOL_LINK_MODE_LEGACY_MASK(10baseT_Half)
#define ADVERTISED_10baseT_Full		__ETHTOOL_LINK_MODE_LEGACY_MASK(10baseT_Full)
#define ADVERTISED_100baseT_Half	__ETHTOOL_LINK_MODE_LEGACY_MASK(100baseT_Half)
#define ADVERTISED_100baseT_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(100baseT_Full)
#define ADVERTISED_1000baseT_Half	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseT_Half)
#define ADVERTISED_1000baseT_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseT_Full)
#define ADVERTISED_Autoneg		__ETHTOOL_LINK_MODE_LEGACY_MASK(Autoneg)
#define ADVERTISED_TP			__ETHTOOL_LINK_MODE_LEGACY_MASK(TP)
#define ADVERTISED_AUI			__ETHTOOL_LINK_MODE_LEGACY_MASK(AUI)
#define ADVERTISED_MII			__ETHTOOL_LINK_MODE_LEGACY_MASK(MII)
#define ADVERTISED_FIBRE		__ETHTOOL_LINK_MODE_LEGACY_MASK(FIBRE)
#define ADVERTISED_BNC			__ETHTOOL_LINK_MODE_LEGACY_MASK(BNC)
#define ADVERTISED_10000baseT_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseT_Full)
#define ADVERTISED_Pause		__ETHTOOL_LINK_MODE_LEGACY_MASK(Pause)
#define ADVERTISED_Asym_Pause		__ETHTOOL_LINK_MODE_LEGACY_MASK(Asym_Pause)
#define ADVERTISED_2500baseX_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(2500baseX_Full)
#define ADVERTISED_Backplane		__ETHTOOL_LINK_MODE_LEGACY_MASK(Backplane)
#define ADVERTISED_1000baseKX_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(1000baseKX_Full)
#define ADVERTISED_10000baseKX4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseKX4_Full)
#define ADVERTISED_10000baseKR_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseKR_Full)
#define ADVERTISED_10000baseR_FEC	__ETHTOOL_LINK_MODE_LEGACY_MASK(10000baseR_FEC)
#define ADVERTISED_20000baseMLD2_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(20000baseMLD2_Full)
#define ADVERTISED_20000baseKR2_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(20000baseKR2_Full)
#define ADVERTISED_40000baseKR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseKR4_Full)
#define ADVERTISED_40000baseCR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseCR4_Full)
#define ADVERTISED_40000baseSR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseSR4_Full)
#define ADVERTISED_40000baseLR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(40000baseLR4_Full)
#define ADVERTISED_56000baseKR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseKR4_Full)
#define ADVERTISED_56000baseCR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseCR4_Full)
#define ADVERTISED_56000baseSR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseSR4_Full)
#define ADVERTISED_56000baseLR4_Full	__ETHTOOL_LINK_MODE_LEGACY_MASK(56000baseLR4_Full)
/* Please do not define any new ADVERTISED_* macro for bits > 31, see
 * notice above.
 */

/* The following are all involved in forcing a particular link
 * mode for the device for setting things.  When getting the
 * devices settings, these indicate the current mode and whether
 * it was forced up into this mode or autonegotiated.
 */

/* The forced speed, in units of 1Mb. All values 0 to INT_MAX are legal.
 * Update drivers/net/phy/phy.c:phy_speed_to_str() and
 * drivers/net/bonding/bond_3ad.c:__get_link_speed() when adding new values.
 */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000
#define SPEED_2500		2500
#define SPEED_5000		5000
#define SPEED_10000		10000
#define SPEED_14000		14000
#define SPEED_20000		20000
#define SPEED_25000		25000
#define SPEED_40000		40000
#define SPEED_50000		50000
#define SPEED_56000		56000
#define SPEED_100000		100000
#define SPEED_200000		200000
#define SPEED_400000		400000

#define SPEED_UNKNOWN		-1

static inline int ethtool_validate_speed(__u32 speed)
{
	return speed <= INT_MAX || speed == (__u32)SPEED_UNKNOWN;
}

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01
#define DUPLEX_UNKNOWN		0xff

static inline int ethtool_validate_duplex(__u8 duplex)
{
	switch (duplex) {
	case DUPLEX_HALF:
	case DUPLEX_FULL:
	case DUPLEX_UNKNOWN:
		return 1;
	}

	return 0;
}

#define MASTER_SLAVE_CFG_UNSUPPORTED		0
#define MASTER_SLAVE_CFG_UNKNOWN		1
#define MASTER_SLAVE_CFG_MASTER_PREFERRED	2
#define MASTER_SLAVE_CFG_SLAVE_PREFERRED	3
#define MASTER_SLAVE_CFG_MASTER_FORCE		4
#define MASTER_SLAVE_CFG_SLAVE_FORCE		5
#define MASTER_SLAVE_STATE_UNSUPPORTED		0
#define MASTER_SLAVE_STATE_UNKNOWN		1
#define MASTER_SLAVE_STATE_MASTER		2
#define MASTER_SLAVE_STATE_SLAVE		3
#define MASTER_SLAVE_STATE_ERR			4

/* Which connector port. */
#define PORT_TP			0x00
#define PORT_AUI		0x01
#define PORT_MII		0x02
#define PORT_FIBRE		0x03
#define PORT_BNC		0x04
#define PORT_DA			0x05
#define PORT_NONE		0xef
#define PORT_OTHER		0xff

/* Which transceiver to use. */
#define XCVR_INTERNAL		0x00 /* PHY and MAC are in the same package */
#define XCVR_EXTERNAL		0x01 /* PHY and MAC are in different packages */
#define XCVR_DUMMY1		0x02
#define XCVR_DUMMY2		0x03
#define XCVR_DUMMY3		0x04

/* Enable or disable autonegotiation. */
#define AUTONEG_DISABLE		0x00
#define AUTONEG_ENABLE		0x01

/* MDI or MDI-X status/control - if MDI/MDI_X/AUTO is set then
 * the driver is required to renegotiate link
 */
#define ETH_TP_MDI_INVALID	0x00 /* status: unknown; control: unsupported */
#define ETH_TP_MDI		0x01 /* status: MDI;     control: force MDI */
#define ETH_TP_MDI_X		0x02 /* status: MDI-X;   control: force MDI-X */
#define ETH_TP_MDI_AUTO		0x03 /*                  control: auto-select */

/* Wake-On-Lan options. */
#define WAKE_PHY		(1 << 0)
#define WAKE_UCAST		(1 << 1)
#define WAKE_MCAST		(1 << 2)
#define WAKE_BCAST		(1 << 3)
#define WAKE_ARP		(1 << 4)
#define WAKE_MAGIC		(1 << 5)
#define WAKE_MAGICSECURE	(1 << 6) /* only meaningful if WAKE_MAGIC */
#define WAKE_FILTER		(1 << 7)

#define WOL_MODE_COUNT		8

/* L2-L4 network traffic flow types */
#define	TCP_V4_FLOW	0x01	/* hash or spec (tcp_ip4_spec) */
#define	UDP_V4_FLOW	0x02	/* hash or spec (udp_ip4_spec) */
#define	SCTP_V4_FLOW	0x03	/* hash or spec (sctp_ip4_spec) */
#define	AH_ESP_V4_FLOW	0x04	/* hash only */
#define	TCP_V6_FLOW	0x05	/* hash or spec (tcp_ip6_spec; nfc only) */
#define	UDP_V6_FLOW	0x06	/* hash or spec (udp_ip6_spec; nfc only) */
#define	SCTP_V6_FLOW	0x07	/* hash or spec (sctp_ip6_spec; nfc only) */
#define	AH_ESP_V6_FLOW	0x08	/* hash only */
#define	AH_V4_FLOW	0x09	/* hash or spec (ah_ip4_spec) */
#define	ESP_V4_FLOW	0x0a	/* hash or spec (esp_ip4_spec) */
#define	AH_V6_FLOW	0x0b	/* hash or spec (ah_ip6_spec; nfc only) */
#define	ESP_V6_FLOW	0x0c	/* hash or spec (esp_ip6_spec; nfc only) */
#define	IPV4_USER_FLOW	0x0d	/* spec only (usr_ip4_spec) */
#define	IP_USER_FLOW	IPV4_USER_FLOW
#define	IPV6_USER_FLOW	0x0e	/* spec only (usr_ip6_spec; nfc only) */
#define	IPV4_FLOW	0x10	/* hash only */
#define	IPV6_FLOW	0x11	/* hash only */
#define	ETHER_FLOW	0x12	/* spec only (ether_spec) */
/* Flag to enable additional fields in struct ethtool_rx_flow_spec */
#define	FLOW_EXT	0x80000000
#define	FLOW_MAC_EXT	0x40000000
/* Flag to enable RSS spreading of traffic matching rule (nfc only) */
#define	FLOW_RSS	0x20000000

/* L3-L4 network traffic flow hash options */
#define	RXH_L2DA	(1 << 1)
#define	RXH_VLAN	(1 << 2)
#define	RXH_L3_PROTO	(1 << 3)
#define	RXH_IP_SRC	(1 << 4)
#define	RXH_IP_DST	(1 << 5)
#define	RXH_L4_B_0_1	(1 << 6) /* src port in case of TCP/UDP/SCTP */
#define	RXH_L4_B_2_3	(1 << 7) /* dst port in case of TCP/UDP/SCTP */
#define	RXH_DISCARD	(1 << 31)

#define	RX_CLS_FLOW_DISC	0xffffffffffffffffULL
#define RX_CLS_FLOW_WAKE	0xfffffffffffffffeULL

/* Special RX classification rule insert location values */
#define RX_CLS_LOC_SPECIAL	0x80000000	/* flag */
#define RX_CLS_LOC_ANY		0xffffffff
#define RX_CLS_LOC_FIRST	0xfffffffe
#define RX_CLS_LOC_LAST		0xfffffffd

/* EEPROM Standards for plug in modules */
#define ETH_MODULE_SFF_8079		0x1
#define ETH_MODULE_SFF_8079_LEN		256
#define ETH_MODULE_SFF_8472		0x2
#define ETH_MODULE_SFF_8472_LEN		512
#define ETH_MODULE_SFF_8636		0x3
#define ETH_MODULE_SFF_8636_LEN		256
#define ETH_MODULE_SFF_8436		0x4
#define ETH_MODULE_SFF_8436_LEN		256

#define ETH_MODULE_SFF_8636_MAX_LEN     640
#define ETH_MODULE_SFF_8436_MAX_LEN     640

/* Reset flags */
/* The reset() operation must clear the flags for the components which
 * were actually reset.  On successful return, the flags indicate the
 * components which were not reset, either because they do not exist
 * in the hardware or because they cannot be reset independently.  The
 * driver must never reset any components that were not requested.
 */
enum ethtool_reset_flags {
	/* These flags represent components dedicated to the interface
	 * the command is addressed to.  Shift any flag left by
	 * ETH_RESET_SHARED_SHIFT to reset a shared component of the
	 * same type.
	 */
	ETH_RESET_MGMT		= 1 << 0,	/* Management processor */
	ETH_RESET_IRQ		= 1 << 1,	/* Interrupt requester */
	ETH_RESET_DMA		= 1 << 2,	/* DMA engine */
	ETH_RESET_FILTER	= 1 << 3,	/* Filtering/flow direction */
	ETH_RESET_OFFLOAD	= 1 << 4,	/* Protocol offload */
	ETH_RESET_MAC		= 1 << 5,	/* Media access controller */
	ETH_RESET_PHY		= 1 << 6,	/* Transceiver/PHY */
	ETH_RESET_RAM		= 1 << 7,	/* RAM shared between
						 * multiple components */
	ETH_RESET_AP		= 1 << 8,	/* Application processor */

	ETH_RESET_DEDICATED	= 0x0000ffff,	/* All components dedicated to
						 * this interface */
	ETH_RESET_ALL		= 0xffffffff,	/* All components used by this
						 * interface, even if shared */
};
#define ETH_RESET_SHARED_SHIFT	16


/**
 * struct ethtool_link_settings - link control and status
 *
 * IMPORTANT, Backward compatibility notice: When implementing new
 *	user-space tools, please first try %ETHTOOL_GLINKSETTINGS, and
 *	if it succeeds use %ETHTOOL_SLINKSETTINGS to change link
 *	settings; do not use %ETHTOOL_SSET if %ETHTOOL_GLINKSETTINGS
 *	succeeded: stick to %ETHTOOL_GLINKSETTINGS/%SLINKSETTINGS in
 *	that case.  Conversely, if %ETHTOOL_GLINKSETTINGS fails, use
 *	%ETHTOOL_GSET to query and %ETHTOOL_SSET to change link
 *	settings; do not use %ETHTOOL_SLINKSETTINGS if
 *	%ETHTOOL_GLINKSETTINGS failed: stick to
 *	%ETHTOOL_GSET/%ETHTOOL_SSET in that case.
 *
 * @cmd: Command number = %ETHTOOL_GLINKSETTINGS or %ETHTOOL_SLINKSETTINGS
 * @speed: Link speed (Mbps)
 * @duplex: Duplex mode; one of %DUPLEX_*
 * @port: Physical connector type; one of %PORT_*
 * @phy_address: MDIO address of PHY (transceiver); 0 or 255 if not
 *	applicable.  For clause 45 PHYs this is the PRTAD.
 * @autoneg: Enable/disable autonegotiation and auto-detection;
 *	either %AUTONEG_DISABLE or %AUTONEG_ENABLE
 * @mdio_support: Bitmask of %ETH_MDIO_SUPPORTS_* flags for the MDIO
 *	protocols supported by the interface; 0 if unknown.
 *	Read-only.
 * @eth_tp_mdix: Ethernet twisted-pair MDI(-X) status; one of
 *	%ETH_TP_MDI_*.  If the status is unknown or not applicable, the
 *	value will be %ETH_TP_MDI_INVALID.  Read-only.
 * @eth_tp_mdix_ctrl: Ethernet twisted pair MDI(-X) control; one of
 *	%ETH_TP_MDI_*.  If MDI(-X) control is not implemented, reads
 *	yield %ETH_TP_MDI_INVALID and writes may be ignored or rejected.
 *	When written successfully, the link should be renegotiated if
 *	necessary.
 * @link_mode_masks_nwords: Number of 32-bit words for each of the
 *	supported, advertising, lp_advertising link mode bitmaps. For
 *	%ETHTOOL_GLINKSETTINGS: on entry, number of words passed by user
 *	(>= 0); on return, if handshake in progress, negative if
 *	request size unsupported by kernel: absolute value indicates
 *	kernel expected size and all the other fields but cmd
 *	are 0; otherwise (handshake completed), strictly positive
 *	to indicate size used by kernel and cmd field stays
 *	%ETHTOOL_GLINKSETTINGS, all other fields populated by driver. For
 *	%ETHTOOL_SLINKSETTINGS: must be valid on entry, ie. a positive
 *	value returned previously by %ETHTOOL_GLINKSETTINGS, otherwise
 *	refused. For drivers: ignore this field (use kernel's
 *	__ETHTOOL_LINK_MODE_MASK_NBITS instead), any change to it will
 *	be overwritten by kernel.
 * @supported: Bitmap with each bit meaning given by
 *	%ethtool_link_mode_bit_indices for the link modes, physical
 *	connectors and other link features for which the interface
 *	supports autonegotiation or auto-detection.  Read-only.
 * @advertising: Bitmap with each bit meaning given by
 *	%ethtool_link_mode_bit_indices for the link modes, physical
 *	connectors and other link features that are advertised through
 *	autonegotiation or enabled for auto-detection.
 * @lp_advertising: Bitmap with each bit meaning given by
 *	%ethtool_link_mode_bit_indices for the link modes, and other
 *	link features that the link partner advertised through
 *	autonegotiation; 0 if unknown or not applicable.  Read-only.
 * @transceiver: Used to distinguish different possible PHY types,
 *	reported consistently by PHYLIB.  Read-only.
 * @master_slave_cfg: Master/slave port mode.
 * @master_slave_state: Master/slave port state.
 * @reserved: Reserved for future use; see the note on reserved space.
 * @reserved1: Reserved for future use; see the note on reserved space.
 * @link_mode_masks: Variable length bitmaps.
 *
 * If autonegotiation is disabled, the speed and @duplex represent the
 * fixed link mode and are writable if the driver supports multiple
 * link modes.  If it is enabled then they are read-only; if the link
 * is up they represent the negotiated link mode; if the link is down,
 * the speed is 0, %SPEED_UNKNOWN or the highest enabled speed and
 * @duplex is %DUPLEX_UNKNOWN or the best enabled duplex mode.
 *
 * Some hardware interfaces may have multiple PHYs and/or physical
 * connectors fitted or do not allow the driver to detect which are
 * fitted.  For these interfaces @port and/or @phy_address may be
 * writable, possibly dependent on @autoneg being %AUTONEG_DISABLE.
 * Otherwise, attempts to write different values may be ignored or
 * rejected.
 *
 * Deprecated %ethtool_cmd fields transceiver, maxtxpkt and maxrxpkt
 * are not available in %ethtool_link_settings. These fields will be
 * always set to zero in %ETHTOOL_GSET reply and %ETHTOOL_SSET will
 * fail if any of them is set to non-zero value.
 *
 * Users should assume that all fields not marked read-only are
 * writable and subject to validation by the driver.  They should use
 * %ETHTOOL_GLINKSETTINGS to get the current values before making specific
 * changes and then applying them with %ETHTOOL_SLINKSETTINGS.
 *
 * Drivers that implement %get_link_ksettings and/or
 * %set_link_ksettings should ignore the @cmd
 * and @link_mode_masks_nwords fields (any change to them overwritten
 * by kernel), and rely only on kernel's internal
 * %__ETHTOOL_LINK_MODE_MASK_NBITS and
 * %ethtool_link_mode_mask_t. Drivers that implement
 * %set_link_ksettings() should validate all fields other than @cmd
 * and @link_mode_masks_nwords that are not described as read-only or
 * deprecated, and must ignore all fields described as read-only.
 */
struct ethtool_link_settings {
	__u32	cmd;
	__u32	speed;
	__u8	duplex;
	__u8	port;
	__u8	phy_address;
	__u8	autoneg;
	__u8	mdio_support;
	__u8	eth_tp_mdix;
	__u8	eth_tp_mdix_ctrl;
	__s8	link_mode_masks_nwords;
	__u8	transceiver;
	__u8	master_slave_cfg;
	__u8	master_slave_state;
	__u8	reserved1[1];
	__u32	reserved[7];
	__u32	link_mode_masks[0];
	/* layout of link_mode_masks fields:
	 * __u32 map_supported[link_mode_masks_nwords];
	 * __u32 map_advertising[link_mode_masks_nwords];
	 * __u32 map_lp_advertising[link_mode_masks_nwords];
	 */
};
#endif /* _UAPI_LINUX_ETHTOOL_H */
