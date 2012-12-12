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

#include <linux/types.h>
#include <linux/if_ether.h>

/* This should work for both 32 and 64 bit userland. */
struct ethtool_cmd {
	__u32	cmd;
	__u32	supported;	/* Features this interface supports */
	__u32	advertising;	/* Features this interface advertises */
	__u16	speed;	        /* The forced speed (lower bits) in
				 * Mbps. Please use
				 * ethtool_cmd_speed()/_set() to
				 * access it */
	__u8	duplex;		/* Duplex, half or full */
	__u8	port;		/* Which connector port */
	__u8	phy_address;	/* MDIO PHY address (PRTAD for clause 45).
				 * May be read-only or read-write
				 * depending on the driver.
				 */
	__u8	transceiver;	/* Which transceiver to use */
	__u8	autoneg;	/* Enable or disable autonegotiation */
	__u8	mdio_support;	/* MDIO protocols supported.  Read-only.
				 * Not set by all drivers.
				 */
	__u32	maxtxpkt;	/* Tx pkts before generating tx int */
	__u32	maxrxpkt;	/* Rx pkts before generating rx int */
	__u16	speed_hi;       /* The forced speed (upper
				 * bits) in Mbps. Please use
				 * ethtool_cmd_speed()/_set() to
				 * access it */
	__u8	eth_tp_mdix;	/* twisted pair MDI-X status */
	__u8    eth_tp_mdix_ctrl; /* twisted pair MDI-X control, when set,
				   * link should be renegotiated if necessary
				   */
	__u32	lp_advertising;	/* Features the link partner advertises */
	__u32	reserved[2];
};

static inline void ethtool_cmd_speed_set(struct ethtool_cmd *ep,
					 __u32 speed)
{

	ep->speed = (__u16)speed;
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
/* these strings are set to whatever the driver author decides... */
struct ethtool_drvinfo {
	__u32	cmd;
	char	driver[32];	/* driver short name, "tulip", "eepro100" */
	char	version[32];	/* driver version string */
	char	fw_version[ETHTOOL_FWVERS_LEN];	/* firmware version string */
	char	bus_info[ETHTOOL_BUSINFO_LEN];	/* Bus info for this IF. */
				/* For PCI devices, use pci_name(pci_dev). */
	char	reserved1[32];
	char	reserved2[12];
				/*
				 * Some struct members below are filled in
				 * using ops->get_sset_count().  Obtaining
				 * this info from ethtool_drvinfo is now
				 * deprecated; Use ETHTOOL_GSSET_INFO
				 * instead.
				 */
	__u32	n_priv_flags;	/* number of flags valid in ETHTOOL_GPFLAGS */
	__u32	n_stats;	/* number of u64's from ETHTOOL_GSTATS */
	__u32	testinfo_len;
	__u32	eedump_len;	/* Size of data from ETHTOOL_GEEPROM (bytes) */
	__u32	regdump_len;	/* Size of data from ETHTOOL_GREGS (bytes) */
};

#define SOPASS_MAX	6
/* wake-on-lan settings */
struct ethtool_wolinfo {
	__u32	cmd;
	__u32	supported;
	__u32	wolopts;
	__u8	sopass[SOPASS_MAX]; /* SecureOn(tm) password */
};

/* for passing single values */
struct ethtool_value {
	__u32	cmd;
	__u32	data;
};

/* for passing big chunks of data */
struct ethtool_regs {
	__u32	cmd;
	__u32	version; /* driver-specific, indicates different chips/revs */
	__u32	len; /* bytes */
	__u8	data[0];
};

/* for passing EEPROM chunks */
struct ethtool_eeprom {
	__u32	cmd;
	__u32	magic;
	__u32	offset; /* in bytes */
	__u32	len; /* in bytes */
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
 * Each pair of (usecs, max_frames) fields specifies this exit
 * condition for interrupt coalescing:
 *	(usecs > 0 && time_since_first_completion >= usecs) ||
 *	(max_frames > 0 && completed_frames >= max_frames)
 * It is illegal to set both usecs and max_frames to zero as this
 * would cause interrupts to never be generated.  To disable
 * coalescing, set usecs = 0 and max_frames = 1.
 *
 * Some implementations ignore the value of max_frames and use the
 * condition:
 *	time_since_first_completion >= usecs
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

/* for configuring RX/TX ring parameters */
struct ethtool_ringparam {
	__u32	cmd;	/* ETHTOOL_{G,S}RINGPARAM */

	/* Read only attributes.  These indicate the maximum number
	 * of pending RX/TX ring entries the driver will allow the
	 * user to set.
	 */
	__u32	rx_max_pending;
	__u32	rx_mini_max_pending;
	__u32	rx_jumbo_max_pending;
	__u32	tx_max_pending;

	/* Values changeable by the user.  The valid values are
	 * in the range 1 to the "*_max_pending" counterpart above.
	 */
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

/* for configuring link flow control parameters */
struct ethtool_pauseparam {
	__u32	cmd;	/* ETHTOOL_{G,S}PAUSEPARAM */

	/* If the link is being auto-negotiated (via ethtool_cmd.autoneg
	 * being true) the user may set 'autoneg' here non-zero to have the
	 * pause parameters be auto-negotiated too.  In such a case, the
	 * {rx,tx}_pause values below determine what capabilities are
	 * advertised.
	 *
	 * If 'autoneg' is zero or the link is not being auto-negotiated,
	 * then {rx,tx}_pause force the driver to use/not-use pause
	 * flow control.
	 */
	__u32	autoneg;
	__u32	rx_pause;
	__u32	tx_pause;
};

#define ETH_GSTRING_LEN		32
enum ethtool_stringset {
	ETH_SS_TEST		= 0,
	ETH_SS_STATS,
	ETH_SS_PRIV_FLAGS,
	ETH_SS_NTUPLE_FILTERS,	/* Do not use, GRXNTUPLE is now deprecated */
	ETH_SS_FEATURES,
};

/* for passing string sets for data tagging */
struct ethtool_gstrings {
	__u32	cmd;		/* ETHTOOL_GSTRINGS */
	__u32	string_set;	/* string set id e.c. ETH_SS_TEST, etc*/
	__u32	len;		/* number of strings in the string set */
	__u8	data[0];
};

struct ethtool_sset_info {
	__u32	cmd;		/* ETHTOOL_GSSET_INFO */
	__u32	reserved;
	__u64	sset_mask;	/* input: each bit selects an sset to query */
				/* output: each bit a returned sset */
	__u32	data[0];	/* ETH_SS_xxx count, in order, based on bits
				   in sset_mask.  One bit implies one
				   __u32, two bits implies two
				   __u32's, etc. */
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

/* for requesting NIC test and getting results*/
struct ethtool_test {
	__u32	cmd;		/* ETHTOOL_TEST */
	__u32	flags;		/* ETH_TEST_FL_xxx */
	__u32	reserved;
	__u32	len;		/* result length, in number of u64 elements */
	__u64	data[0];
};

/* for dumping NIC-specific statistics */
struct ethtool_stats {
	__u32	cmd;		/* ETHTOOL_GSTATS */
	__u32	n_stats;	/* number of u64's being returned */
	__u64	data[0];
};

struct ethtool_perm_addr {
	__u32	cmd;		/* ETHTOOL_GPERMADDR */
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

union ethtool_flow_union {
	struct ethtool_tcpip4_spec		tcp_ip4_spec;
	struct ethtool_tcpip4_spec		udp_ip4_spec;
	struct ethtool_tcpip4_spec		sctp_ip4_spec;
	struct ethtool_ah_espip4_spec		ah_ip4_spec;
	struct ethtool_ah_espip4_spec		esp_ip4_spec;
	struct ethtool_usrip4_spec		usr_ip4_spec;
	struct ethhdr				ether_spec;
	__u8					hdata[60];
};

struct ethtool_flow_ext {
	__be16	vlan_etype;
	__be16	vlan_tci;
	__be32	data[2];
};

/**
 * struct ethtool_rx_flow_spec - classification rule for RX flows
 * @flow_type: Type of match to perform, e.g. %TCP_V4_FLOW
 * @h_u: Flow fields to match (dependent on @flow_type)
 * @h_ext: Additional fields to match
 * @m_u: Masks for flow field bits to be matched
 * @m_ext: Masks for additional field bits to be matched
 *	Note, all additional fields must be ignored unless @flow_type
 *	includes the %FLOW_EXT flag.
 * @ring_cookie: RX ring/queue index to deliver to, or %RX_CLS_FLOW_DISC
 *	if packets should be discarded
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

/**
 * struct ethtool_rxnfc - command to get or set RX flow classification rules
 * @cmd: Specific command number - %ETHTOOL_GRXFH, %ETHTOOL_SRXFH,
 *	%ETHTOOL_GRXRINGS, %ETHTOOL_GRXCLSRLCNT, %ETHTOOL_GRXCLSRULE,
 *	%ETHTOOL_GRXCLSRLALL, %ETHTOOL_SRXCLSRLDEL or %ETHTOOL_SRXCLSRLINS
 * @flow_type: Type of flow to be affected, e.g. %TCP_V4_FLOW
 * @data: Command-dependent value
 * @fs: Flow classification rule
 * @rule_cnt: Number of rules to be affected
 * @rule_locs: Array of used rule locations
 *
 * For %ETHTOOL_GRXFH and %ETHTOOL_SRXFH, @data is a bitmask indicating
 * the fields included in the flow hash, e.g. %RXH_IP_SRC.  The following
 * structure fields must not be used.
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
 * existing rule on entry and @fs contains the rule on return.
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
 * @fs.@location is the actual rule location.
 *
 * For %ETHTOOL_SRXCLSRLDEL, @fs.@location specifies the location of an
 * existing rule on entry.
 *
 * A driver supporting the special location values for
 * %ETHTOOL_SRXCLSRLINS may add the rule at any suitable unused
 * location, and may remove a rule at a later location (lower
 * priority) that matches exactly the same set of flows.  The special
 * values are: %RX_CLS_LOC_ANY, selecting any location;
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
	__u32				rule_cnt;
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

#define ETH_FW_DUMP_DISABLE 0

struct ethtool_dump {
	__u32	cmd;
	__u32	version;
	__u32	flag;
	__u32	len;
	__u8	data[0];
};

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
 * @size: in: number of elements in the features[] array;
 *       out: number of elements in features[] needed to hold all features
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
 * @rx_filters: bit mask of the supported hwtstamp_rx_filters enumeration values
 *
 * The bits in the 'tx_types' and 'rx_filters' fields correspond to
 * the 'hwtstamp_tx_types' and 'hwtstamp_rx_filters' enumeration values,
 * respectively.  For example, if the device supports HWTSTAMP_TX_ON,
 * then (1 << HWTSTAMP_TX_ON) in 'tx_types' will be set.
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


/* CMDs currently supported */
#define ETHTOOL_GSET		0x00000001 /* Get settings. */
#define ETHTOOL_SSET		0x00000002 /* Set settings. */
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

/* compatibility with older code */
#define SPARC_ETH_GSET		ETHTOOL_GSET
#define SPARC_ETH_SSET		ETHTOOL_SSET

/* Indicates what features are supported by the interface. */
#define SUPPORTED_10baseT_Half		(1 << 0)
#define SUPPORTED_10baseT_Full		(1 << 1)
#define SUPPORTED_100baseT_Half		(1 << 2)
#define SUPPORTED_100baseT_Full		(1 << 3)
#define SUPPORTED_1000baseT_Half	(1 << 4)
#define SUPPORTED_1000baseT_Full	(1 << 5)
#define SUPPORTED_Autoneg		(1 << 6)
#define SUPPORTED_TP			(1 << 7)
#define SUPPORTED_AUI			(1 << 8)
#define SUPPORTED_MII			(1 << 9)
#define SUPPORTED_FIBRE			(1 << 10)
#define SUPPORTED_BNC			(1 << 11)
#define SUPPORTED_10000baseT_Full	(1 << 12)
#define SUPPORTED_Pause			(1 << 13)
#define SUPPORTED_Asym_Pause		(1 << 14)
#define SUPPORTED_2500baseX_Full	(1 << 15)
#define SUPPORTED_Backplane		(1 << 16)
#define SUPPORTED_1000baseKX_Full	(1 << 17)
#define SUPPORTED_10000baseKX4_Full	(1 << 18)
#define SUPPORTED_10000baseKR_Full	(1 << 19)
#define SUPPORTED_10000baseR_FEC	(1 << 20)
#define SUPPORTED_20000baseMLD2_Full	(1 << 21)
#define SUPPORTED_20000baseKR2_Full	(1 << 22)
#define SUPPORTED_40000baseKR4_Full	(1 << 23)
#define SUPPORTED_40000baseCR4_Full	(1 << 24)
#define SUPPORTED_40000baseSR4_Full	(1 << 25)
#define SUPPORTED_40000baseLR4_Full	(1 << 26)

/* Indicates what features are advertised by the interface. */
#define ADVERTISED_10baseT_Half		(1 << 0)
#define ADVERTISED_10baseT_Full		(1 << 1)
#define ADVERTISED_100baseT_Half	(1 << 2)
#define ADVERTISED_100baseT_Full	(1 << 3)
#define ADVERTISED_1000baseT_Half	(1 << 4)
#define ADVERTISED_1000baseT_Full	(1 << 5)
#define ADVERTISED_Autoneg		(1 << 6)
#define ADVERTISED_TP			(1 << 7)
#define ADVERTISED_AUI			(1 << 8)
#define ADVERTISED_MII			(1 << 9)
#define ADVERTISED_FIBRE		(1 << 10)
#define ADVERTISED_BNC			(1 << 11)
#define ADVERTISED_10000baseT_Full	(1 << 12)
#define ADVERTISED_Pause		(1 << 13)
#define ADVERTISED_Asym_Pause		(1 << 14)
#define ADVERTISED_2500baseX_Full	(1 << 15)
#define ADVERTISED_Backplane		(1 << 16)
#define ADVERTISED_1000baseKX_Full	(1 << 17)
#define ADVERTISED_10000baseKX4_Full	(1 << 18)
#define ADVERTISED_10000baseKR_Full	(1 << 19)
#define ADVERTISED_10000baseR_FEC	(1 << 20)
#define ADVERTISED_20000baseMLD2_Full	(1 << 21)
#define ADVERTISED_20000baseKR2_Full	(1 << 22)
#define ADVERTISED_40000baseKR4_Full	(1 << 23)
#define ADVERTISED_40000baseCR4_Full	(1 << 24)
#define ADVERTISED_40000baseSR4_Full	(1 << 25)
#define ADVERTISED_40000baseLR4_Full	(1 << 26)

/* The following are all involved in forcing a particular link
 * mode for the device for setting things.  When getting the
 * devices settings, these indicate the current mode and whether
 * it was forced up into this mode or autonegotiated.
 */

/* The forced speed, 10Mb, 100Mb, gigabit, 2.5Gb, 10GbE. */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000
#define SPEED_2500		2500
#define SPEED_10000		10000
#define SPEED_UNKNOWN		-1

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01
#define DUPLEX_UNKNOWN		0xff

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
#define XCVR_INTERNAL		0x00
#define XCVR_EXTERNAL		0x01
#define XCVR_DUMMY1		0x02
#define XCVR_DUMMY2		0x03
#define XCVR_DUMMY3		0x04

/* Enable or disable autonegotiation.  If this is set to enable,
 * the forced link modes above are completely ignored.
 */
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

/* L2-L4 network traffic flow types */
#define	TCP_V4_FLOW	0x01	/* hash or spec (tcp_ip4_spec) */
#define	UDP_V4_FLOW	0x02	/* hash or spec (udp_ip4_spec) */
#define	SCTP_V4_FLOW	0x03	/* hash or spec (sctp_ip4_spec) */
#define	AH_ESP_V4_FLOW	0x04	/* hash only */
#define	TCP_V6_FLOW	0x05	/* hash only */
#define	UDP_V6_FLOW	0x06	/* hash only */
#define	SCTP_V6_FLOW	0x07	/* hash only */
#define	AH_ESP_V6_FLOW	0x08	/* hash only */
#define	AH_V4_FLOW	0x09	/* hash or spec (ah_ip4_spec) */
#define	ESP_V4_FLOW	0x0a	/* hash or spec (esp_ip4_spec) */
#define	AH_V6_FLOW	0x0b	/* hash only */
#define	ESP_V6_FLOW	0x0c	/* hash only */
#define	IP_USER_FLOW	0x0d	/* spec only (usr_ip4_spec) */
#define	IPV4_FLOW	0x10	/* hash only */
#define	IPV6_FLOW	0x11	/* hash only */
#define	ETHER_FLOW	0x12	/* spec only (ether_spec) */
/* Flag to enable additional fields in struct ethtool_rx_flow_spec */
#define	FLOW_EXT	0x80000000

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

	ETH_RESET_DEDICATED	= 0x0000ffff,	/* All components dedicated to
						 * this interface */
	ETH_RESET_ALL		= 0xffffffff,	/* All components used by this
						 * interface, even if shared */
};
#define ETH_RESET_SHARED_SHIFT	16

#endif /* _UAPI_LINUX_ETHTOOL_H */
