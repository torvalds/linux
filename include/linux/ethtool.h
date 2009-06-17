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

#ifndef _LINUX_ETHTOOL_H
#define _LINUX_ETHTOOL_H

#include <linux/types.h>

/* This should work for both 32 and 64 bit userland. */
struct ethtool_cmd {
	__u32	cmd;
	__u32	supported;	/* Features this interface supports */
	__u32	advertising;	/* Features this interface advertises */
	__u16	speed;		/* The forced speed, 10Mb, 100Mb, gigabit */
	__u8	duplex;		/* Duplex, half or full */
	__u8	port;		/* Which connector port */
	__u8	phy_address;
	__u8	transceiver;	/* Which transceiver to use */
	__u8	autoneg;	/* Enable or disable autonegotiation */
	__u8	mdio_support;
	__u32	maxtxpkt;	/* Tx pkts before generating tx int */
	__u32	maxrxpkt;	/* Rx pkts before generating rx int */
	__u16	speed_hi;
	__u8	eth_tp_mdix;
	__u8	reserved2;
	__u32	lp_advertising;	/* Features the link partner advertises */
	__u32	reserved[2];
};

static inline void ethtool_cmd_speed_set(struct ethtool_cmd *ep,
						__u32 speed)
{

	ep->speed = (__u16)speed;
	ep->speed_hi = (__u16)(speed >> 16);
}

static inline __u32 ethtool_cmd_speed(struct ethtool_cmd *ep)
{
	return (ep->speed_hi << 16) | ep->speed;
}

#define ETHTOOL_BUSINFO_LEN	32
/* these strings are set to whatever the driver author decides... */
struct ethtool_drvinfo {
	__u32	cmd;
	char	driver[32];	/* driver short name, "tulip", "eepro100" */
	char	version[32];	/* driver version string */
	char	fw_version[32];	/* firmware version string, if applicable */
	char	bus_info[ETHTOOL_BUSINFO_LEN];	/* Bus info for this IF. */
				/* For PCI devices, use pci_name(pci_dev). */
	char	reserved1[32];
	char	reserved2[12];
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

/* for configuring coalescing parameters of chip */
struct ethtool_coalesce {
	__u32	cmd;	/* ETHTOOL_{G,S}COALESCE */

	/* How many usecs to delay an RX interrupt after
	 * a packet arrives.  If 0, only rx_max_coalesced_frames
	 * is used.
	 */
	__u32	rx_coalesce_usecs;

	/* How many packets to delay an RX interrupt after
	 * a packet arrives.  If 0, only rx_coalesce_usecs is
	 * used.  It is illegal to set both usecs and max frames
	 * to zero as this would cause RX interrupts to never be
	 * generated.
	 */
	__u32	rx_max_coalesced_frames;

	/* Same as above two parameters, except that these values
	 * apply while an IRQ is being serviced by the host.  Not
	 * all cards support this feature and the values are ignored
	 * in that case.
	 */
	__u32	rx_coalesce_usecs_irq;
	__u32	rx_max_coalesced_frames_irq;

	/* How many usecs to delay a TX interrupt after
	 * a packet is sent.  If 0, only tx_max_coalesced_frames
	 * is used.
	 */
	__u32	tx_coalesce_usecs;

	/* How many packets to delay a TX interrupt after
	 * a packet is sent.  If 0, only tx_coalesce_usecs is
	 * used.  It is illegal to set both usecs and max frames
	 * to zero as this would cause TX interrupts to never be
	 * generated.
	 */
	__u32	tx_max_coalesced_frames;

	/* Same as above two parameters, except that these values
	 * apply while an IRQ is being serviced by the host.  Not
	 * all cards support this feature and the values are ignored
	 * in that case.
	 */
	__u32	tx_coalesce_usecs_irq;
	__u32	tx_max_coalesced_frames_irq;

	/* How many usecs to delay in-memory statistics
	 * block updates.  Some drivers do not have an in-memory
	 * statistic block, and in such cases this value is ignored.
	 * This value must not be zero.
	 */
	__u32	stats_block_coalesce_usecs;

	/* Adaptive RX/TX coalescing is an algorithm implemented by
	 * some drivers to improve latency under low packet rates and
	 * improve throughput under high packet rates.  Some drivers
	 * only implement one of RX or TX adaptive coalescing.  Anything
	 * not implemented by the driver causes these values to be
	 * silently ignored.
	 */
	__u32	use_adaptive_rx_coalesce;
	__u32	use_adaptive_tx_coalesce;

	/* When the packet rate (measured in packets per second)
	 * is below pkt_rate_low, the {rx,tx}_*_low parameters are
	 * used.
	 */
	__u32	pkt_rate_low;
	__u32	rx_coalesce_usecs_low;
	__u32	rx_max_coalesced_frames_low;
	__u32	tx_coalesce_usecs_low;
	__u32	tx_max_coalesced_frames_low;

	/* When the packet rate is below pkt_rate_high but above
	 * pkt_rate_low (both measured in packets per second) the
	 * normal {rx,tx}_* coalescing parameters are used.
	 */

	/* When the packet rate is (measured in packets per second)
	 * is above pkt_rate_high, the {rx,tx}_*_high parameters are
	 * used.
	 */
	__u32	pkt_rate_high;
	__u32	rx_coalesce_usecs_high;
	__u32	rx_max_coalesced_frames_high;
	__u32	tx_coalesce_usecs_high;
	__u32	tx_max_coalesced_frames_high;

	/* How often to do adaptive coalescing packet rate sampling,
	 * measured in seconds.  Must not be zero.
	 */
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

/* for configuring link flow control parameters */
struct ethtool_pauseparam {
	__u32	cmd;	/* ETHTOOL_{G,S}PAUSEPARAM */

	/* If the link is being auto-negotiated (via ethtool_cmd.autoneg
	 * being true) the user may set 'autonet' here non-zero to have the
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
};

/* for passing string sets for data tagging */
struct ethtool_gstrings {
	__u32	cmd;		/* ETHTOOL_GSTRINGS */
	__u32	string_set;	/* string set id e.c. ETH_SS_TEST, etc*/
	__u32	len;		/* number of strings in the string set */
	__u8	data[0];
};

enum ethtool_test_flags {
	ETH_TEST_FL_OFFLINE	= (1 << 0),	/* online / offline */
	ETH_TEST_FL_FAILED	= (1 << 1),	/* test passed / failed */
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
	ETH_FLAG_LRO		= (1 << 15),	/* LRO is enabled */
};

/* The following structures are for supporting RX network flow
 * classification configuration. Note, all multibyte fields, e.g.,
 * ip4src, ip4dst, psrc, pdst, spi, etc. are expected to be in network
 * byte order.
 */
struct ethtool_tcpip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be16	psrc;
	__be16	pdst;
	__u8    tos;
};

struct ethtool_ah_espip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be32	spi;
	__u8    tos;
};

struct ethtool_rawip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__u8	hdata[64];
};

struct ethtool_ether_spec {
	__be16	ether_type;
	__u8	frame_size;
	__u8	eframe[16];
};

#define	ETH_RX_NFC_IP4	1
#define	ETH_RX_NFC_IP6	2

struct ethtool_usrip4_spec {
	__be32	ip4src;
	__be32	ip4dst;
	__be32	l4_4_bytes;
	__u8    tos;
	__u8    ip_ver;
	__u8    proto;
};

struct ethtool_rx_flow_spec {
	__u32		flow_type;
	union {
		struct ethtool_tcpip4_spec		tcp_ip4_spec;
		struct ethtool_tcpip4_spec		udp_ip4_spec;
		struct ethtool_tcpip4_spec		sctp_ip4_spec;
		struct ethtool_ah_espip4_spec		ah_ip4_spec;
		struct ethtool_ah_espip4_spec		esp_ip4_spec;
		struct ethtool_rawip4_spec		raw_ip4_spec;
		struct ethtool_ether_spec		ether_spec;
		struct ethtool_usrip4_spec		usr_ip4_spec;
		__u8					hdata[64];
	} h_u, m_u; /* entry, mask */
	__u64		ring_cookie;
	__u32		location;
};

struct ethtool_rxnfc {
	__u32				cmd;
	__u32				flow_type;
	/* The rx flow hash value or the rule DB size */
	__u64				data;
	struct ethtool_rx_flow_spec	fs;
	__u32				rule_cnt;
	__u32				rule_locs[0];
};

#ifdef __KERNEL__

struct net_device;

/* Some generic methods drivers may use in their ethtool_ops */
u32 ethtool_op_get_link(struct net_device *dev);
u32 ethtool_op_get_tx_csum(struct net_device *dev);
int ethtool_op_set_tx_csum(struct net_device *dev, u32 data);
int ethtool_op_set_tx_hw_csum(struct net_device *dev, u32 data);
int ethtool_op_set_tx_ipv6_csum(struct net_device *dev, u32 data);
u32 ethtool_op_get_sg(struct net_device *dev);
int ethtool_op_set_sg(struct net_device *dev, u32 data);
u32 ethtool_op_get_tso(struct net_device *dev);
int ethtool_op_set_tso(struct net_device *dev, u32 data);
u32 ethtool_op_get_ufo(struct net_device *dev);
int ethtool_op_set_ufo(struct net_device *dev, u32 data);
u32 ethtool_op_get_flags(struct net_device *dev);
int ethtool_op_set_flags(struct net_device *dev, u32 data);

/**
 * &ethtool_ops - Alter and report network device settings
 * get_settings: Get device-specific settings
 * set_settings: Set device-specific settings
 * get_drvinfo: Report driver information
 * get_regs: Get device registers
 * get_wol: Report whether Wake-on-Lan is enabled
 * set_wol: Turn Wake-on-Lan on or off
 * get_msglevel: Report driver message level
 * set_msglevel: Set driver message level
 * nway_reset: Restart autonegotiation
 * get_link: Get link status
 * get_eeprom: Read data from the device EEPROM
 * set_eeprom: Write data to the device EEPROM
 * get_coalesce: Get interrupt coalescing parameters
 * set_coalesce: Set interrupt coalescing parameters
 * get_ringparam: Report ring sizes
 * set_ringparam: Set ring sizes
 * get_pauseparam: Report pause parameters
 * set_pauseparam: Set pause parameters
 * get_rx_csum: Report whether receive checksums are turned on or off
 * set_rx_csum: Turn receive checksum on or off
 * get_tx_csum: Report whether transmit checksums are turned on or off
 * set_tx_csum: Turn transmit checksums on or off
 * get_sg: Report whether scatter-gather is enabled
 * set_sg: Turn scatter-gather on or off
 * get_tso: Report whether TCP segmentation offload is enabled
 * set_tso: Turn TCP segmentation offload on or off
 * get_ufo: Report whether UDP fragmentation offload is enabled
 * set_ufo: Turn UDP fragmentation offload on or off
 * self_test: Run specified self-tests
 * get_strings: Return a set of strings that describe the requested objects 
 * phys_id: Identify the device
 * get_stats: Return statistics about the device
 * get_flags: get 32-bit flags bitmap
 * set_flags: set 32-bit flags bitmap
 * 
 * Description:
 *
 * get_settings:
 *	@get_settings is passed an &ethtool_cmd to fill in.  It returns
 *	an negative errno or zero.
 *
 * set_settings:
 *	@set_settings is passed an &ethtool_cmd and should attempt to set
 *	all the settings this device supports.  It may return an error value
 *	if something goes wrong (otherwise 0).
 *
 * get_eeprom:
 *	Should fill in the magic field.  Don't need to check len for zero
 *	or wraparound.  Fill in the data argument with the eeprom values
 *	from offset to offset + len.  Update len to the amount read.
 *	Returns an error or zero.
 *
 * set_eeprom:
 *	Should validate the magic field.  Don't need to check len for zero
 *	or wraparound.  Update len to the amount written.  Returns an error
 *	or zero.
 */
struct ethtool_ops {
	int	(*get_settings)(struct net_device *, struct ethtool_cmd *);
	int	(*set_settings)(struct net_device *, struct ethtool_cmd *);
	void	(*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
	int	(*get_regs_len)(struct net_device *);
	void	(*get_regs)(struct net_device *, struct ethtool_regs *, void *);
	void	(*get_wol)(struct net_device *, struct ethtool_wolinfo *);
	int	(*set_wol)(struct net_device *, struct ethtool_wolinfo *);
	u32	(*get_msglevel)(struct net_device *);
	void	(*set_msglevel)(struct net_device *, u32);
	int	(*nway_reset)(struct net_device *);
	u32	(*get_link)(struct net_device *);
	int	(*get_eeprom_len)(struct net_device *);
	int	(*get_eeprom)(struct net_device *, struct ethtool_eeprom *, u8 *);
	int	(*set_eeprom)(struct net_device *, struct ethtool_eeprom *, u8 *);
	int	(*get_coalesce)(struct net_device *, struct ethtool_coalesce *);
	int	(*set_coalesce)(struct net_device *, struct ethtool_coalesce *);
	void	(*get_ringparam)(struct net_device *, struct ethtool_ringparam *);
	int	(*set_ringparam)(struct net_device *, struct ethtool_ringparam *);
	void	(*get_pauseparam)(struct net_device *, struct ethtool_pauseparam*);
	int	(*set_pauseparam)(struct net_device *, struct ethtool_pauseparam*);
	u32	(*get_rx_csum)(struct net_device *);
	int	(*set_rx_csum)(struct net_device *, u32);
	u32	(*get_tx_csum)(struct net_device *);
	int	(*set_tx_csum)(struct net_device *, u32);
	u32	(*get_sg)(struct net_device *);
	int	(*set_sg)(struct net_device *, u32);
	u32	(*get_tso)(struct net_device *);
	int	(*set_tso)(struct net_device *, u32);
	void	(*self_test)(struct net_device *, struct ethtool_test *, u64 *);
	void	(*get_strings)(struct net_device *, u32 stringset, u8 *);
	int	(*phys_id)(struct net_device *, u32);
	void	(*get_ethtool_stats)(struct net_device *, struct ethtool_stats *, u64 *);
	int	(*begin)(struct net_device *);
	void	(*complete)(struct net_device *);
	u32     (*get_ufo)(struct net_device *);
	int     (*set_ufo)(struct net_device *, u32);
	u32     (*get_flags)(struct net_device *);
	int     (*set_flags)(struct net_device *, u32);
	u32     (*get_priv_flags)(struct net_device *);
	int     (*set_priv_flags)(struct net_device *, u32);
	int	(*get_sset_count)(struct net_device *, int);

	/* the following hooks are obsolete */
	int	(*self_test_count)(struct net_device *);/* use get_sset_count */
	int	(*get_stats_count)(struct net_device *);/* use get_sset_count */
	int	(*get_rxnfc)(struct net_device *, struct ethtool_rxnfc *, void *);
	int	(*set_rxnfc)(struct net_device *, struct ethtool_rxnfc *);
};
#endif /* __KERNEL__ */

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
#define ETHTOOL_GLINK		0x0000000a /* Get link status (ethtool_value) */
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

#define	ETHTOOL_GRXFH		0x00000029 /* Get RX flow hash configuration */
#define	ETHTOOL_SRXFH		0x0000002a /* Set RX flow hash configuration */
#define ETHTOOL_GGRO		0x0000002b /* Get GRO enable (ethtool_value) */
#define ETHTOOL_SGRO		0x0000002c /* Set GRO enable (ethtool_value) */
#define	ETHTOOL_GRXRINGS	0x0000002d /* Get RX rings available for LB */
#define	ETHTOOL_GRXCLSRLCNT	0x0000002e /* Get RX class rule count */
#define	ETHTOOL_GRXCLSRULE	0x0000002f /* Get RX classification rule */
#define	ETHTOOL_GRXCLSRLALL	0x00000030 /* Get all RX classification rule */
#define	ETHTOOL_SRXCLSRLDEL	0x00000031 /* Delete RX classification rule */
#define	ETHTOOL_SRXCLSRLINS	0x00000032 /* Insert RX classification rule */

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

/* The following are all involved in forcing a particular link
 * mode for the device for setting things.  When getting the
 * devices settings, these indicate the current mode and whether
 * it was foced up into this mode or autonegotiated.
 */

/* The forced speed, 10Mb, 100Mb, gigabit, 2.5Gb, 10GbE. */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000
#define SPEED_2500		2500
#define SPEED_10000		10000

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01

/* Which connector port. */
#define PORT_TP			0x00
#define PORT_AUI		0x01
#define PORT_MII		0x02
#define PORT_FIBRE		0x03
#define PORT_BNC		0x04
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

/* Mode MDI or MDI-X */
#define ETH_TP_MDI_INVALID	0x00
#define ETH_TP_MDI		0x01
#define ETH_TP_MDI_X		0x02

/* Wake-On-Lan options. */
#define WAKE_PHY		(1 << 0)
#define WAKE_UCAST		(1 << 1)
#define WAKE_MCAST		(1 << 2)
#define WAKE_BCAST		(1 << 3)
#define WAKE_ARP		(1 << 4)
#define WAKE_MAGIC		(1 << 5)
#define WAKE_MAGICSECURE	(1 << 6) /* only meaningful if WAKE_MAGIC */

/* L3-L4 network traffic flow types */
#define	TCP_V4_FLOW	0x01
#define	UDP_V4_FLOW	0x02
#define	SCTP_V4_FLOW	0x03
#define	AH_ESP_V4_FLOW	0x04
#define	TCP_V6_FLOW	0x05
#define	UDP_V6_FLOW	0x06
#define	SCTP_V6_FLOW	0x07
#define	AH_ESP_V6_FLOW	0x08
#define	AH_V4_FLOW	0x09
#define	ESP_V4_FLOW	0x0a
#define	AH_V6_FLOW	0x0b
#define	ESP_V6_FLOW	0x0c
#define	IP_USER_FLOW	0x0d

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

#endif /* _LINUX_ETHTOOL_H */
