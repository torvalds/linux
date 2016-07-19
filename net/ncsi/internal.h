/*
 * Copyright Gavin Shan, IBM Corporation 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NCSI_INTERNAL_H__
#define __NCSI_INTERNAL_H__

enum {
	NCSI_CAP_BASE		= 0,
	NCSI_CAP_GENERIC	= 0,
	NCSI_CAP_BC,
	NCSI_CAP_MC,
	NCSI_CAP_BUFFER,
	NCSI_CAP_AEN,
	NCSI_CAP_VLAN,
	NCSI_CAP_MAX
};

enum {
	NCSI_CAP_GENERIC_HWA             = 0x01, /* HW arbitration           */
	NCSI_CAP_GENERIC_HDS             = 0x02, /* HNC driver status change */
	NCSI_CAP_GENERIC_FC              = 0x04, /* HNC to MC flow control   */
	NCSI_CAP_GENERIC_FC1             = 0x08, /* MC to HNC flow control   */
	NCSI_CAP_GENERIC_MC              = 0x10, /* Global MC filtering      */
	NCSI_CAP_GENERIC_HWA_UNKNOWN     = 0x00, /* Unknown HW arbitration   */
	NCSI_CAP_GENERIC_HWA_SUPPORT     = 0x20, /* Supported HW arbitration */
	NCSI_CAP_GENERIC_HWA_NOT_SUPPORT = 0x40, /* No HW arbitration        */
	NCSI_CAP_GENERIC_HWA_RESERVED    = 0x60, /* Reserved HW arbitration  */
	NCSI_CAP_GENERIC_HWA_MASK        = 0x60, /* Mask for HW arbitration  */
	NCSI_CAP_GENERIC_MASK            = 0x7f,
	NCSI_CAP_BC_ARP                  = 0x01, /* ARP packet filtering     */
	NCSI_CAP_BC_DHCPC                = 0x02, /* DHCP client filtering    */
	NCSI_CAP_BC_DHCPS                = 0x04, /* DHCP server filtering    */
	NCSI_CAP_BC_NETBIOS              = 0x08, /* NetBIOS packet filtering */
	NCSI_CAP_BC_MASK                 = 0x0f,
	NCSI_CAP_MC_IPV6_NEIGHBOR        = 0x01, /* IPv6 neighbor filtering  */
	NCSI_CAP_MC_IPV6_ROUTER          = 0x02, /* IPv6 router filering     */
	NCSI_CAP_MC_DHCPV6_RELAY         = 0x04, /* DHCPv6 relay / server MC */
	NCSI_CAP_MC_DHCPV6_WELL_KNOWN    = 0x08, /* DHCPv6 well-known MC     */
	NCSI_CAP_MC_IPV6_MLD             = 0x10, /* IPv6 MLD filtering       */
	NCSI_CAP_MC_IPV6_NEIGHBOR_S      = 0x20, /* IPv6 neighbour filtering */
	NCSI_CAP_MC_MASK                 = 0x3f,
	NCSI_CAP_AEN_LSC                 = 0x01, /* Link status change       */
	NCSI_CAP_AEN_CR                  = 0x02, /* Configuration required   */
	NCSI_CAP_AEN_HDS                 = 0x04, /* HNC driver status        */
	NCSI_CAP_AEN_MASK                = 0x07,
	NCSI_CAP_VLAN_ONLY               = 0x01, /* Filter VLAN packet only  */
	NCSI_CAP_VLAN_NO                 = 0x02, /* Filter VLAN and non-VLAN */
	NCSI_CAP_VLAN_ANY                = 0x04, /* Filter Any-and-non-VLAN  */
	NCSI_CAP_VLAN_MASK               = 0x07
};

enum {
	NCSI_MODE_BASE		= 0,
	NCSI_MODE_ENABLE	= 0,
	NCSI_MODE_TX_ENABLE,
	NCSI_MODE_LINK,
	NCSI_MODE_VLAN,
	NCSI_MODE_BC,
	NCSI_MODE_MC,
	NCSI_MODE_AEN,
	NCSI_MODE_FC,
	NCSI_MODE_MAX
};

enum {
	NCSI_FILTER_BASE	= 0,
	NCSI_FILTER_VLAN	= 0,
	NCSI_FILTER_UC,
	NCSI_FILTER_MC,
	NCSI_FILTER_MIXED,
	NCSI_FILTER_MAX
};

struct ncsi_channel_version {
	u32 version;		/* Supported BCD encoded NCSI version */
	u32 alpha2;		/* Supported BCD encoded NCSI version */
	u8  fw_name[12];	/* Firware name string                */
	u32 fw_version;		/* Firmware version                   */
	u16 pci_ids[4];		/* PCI identification                 */
	u32 mf_id;		/* Manufacture ID                     */
};

struct ncsi_channel_cap {
	u32 index;	/* Index of channel capabilities */
	u32 cap;	/* NCSI channel capability       */
};

struct ncsi_channel_mode {
	u32 index;	/* Index of channel modes      */
	u32 enable;	/* Enabled or disabled         */
	u32 size;	/* Valid entries in ncm_data[] */
	u32 data[8];	/* Data entries                */
};

struct ncsi_channel_filter {
	u32 index;	/* Index of channel filters          */
	u32 total;	/* Total entries in the filter table */
	u64 bitmap;	/* Bitmap of valid entries           */
	u32 data[];	/* Data for the valid entries        */
};

struct ncsi_channel_stats {
	u32 hnc_cnt_hi;		/* Counter cleared            */
	u32 hnc_cnt_lo;		/* Counter cleared            */
	u32 hnc_rx_bytes;	/* Rx bytes                   */
	u32 hnc_tx_bytes;	/* Tx bytes                   */
	u32 hnc_rx_uc_pkts;	/* Rx UC packets              */
	u32 hnc_rx_mc_pkts;     /* Rx MC packets              */
	u32 hnc_rx_bc_pkts;	/* Rx BC packets              */
	u32 hnc_tx_uc_pkts;	/* Tx UC packets              */
	u32 hnc_tx_mc_pkts;	/* Tx MC packets              */
	u32 hnc_tx_bc_pkts;	/* Tx BC packets              */
	u32 hnc_fcs_err;	/* FCS errors                 */
	u32 hnc_align_err;	/* Alignment errors           */
	u32 hnc_false_carrier;	/* False carrier detection    */
	u32 hnc_runt_pkts;	/* Rx runt packets            */
	u32 hnc_jabber_pkts;	/* Rx jabber packets          */
	u32 hnc_rx_pause_xon;	/* Rx pause XON frames        */
	u32 hnc_rx_pause_xoff;	/* Rx XOFF frames             */
	u32 hnc_tx_pause_xon;	/* Tx XON frames              */
	u32 hnc_tx_pause_xoff;	/* Tx XOFF frames             */
	u32 hnc_tx_s_collision;	/* Single collision frames    */
	u32 hnc_tx_m_collision;	/* Multiple collision frames  */
	u32 hnc_l_collision;	/* Late collision frames      */
	u32 hnc_e_collision;	/* Excessive collision frames */
	u32 hnc_rx_ctl_frames;	/* Rx control frames          */
	u32 hnc_rx_64_frames;	/* Rx 64-bytes frames         */
	u32 hnc_rx_127_frames;	/* Rx 65-127 bytes frames     */
	u32 hnc_rx_255_frames;	/* Rx 128-255 bytes frames    */
	u32 hnc_rx_511_frames;	/* Rx 256-511 bytes frames    */
	u32 hnc_rx_1023_frames;	/* Rx 512-1023 bytes frames   */
	u32 hnc_rx_1522_frames;	/* Rx 1024-1522 bytes frames  */
	u32 hnc_rx_9022_frames;	/* Rx 1523-9022 bytes frames  */
	u32 hnc_tx_64_frames;	/* Tx 64-bytes frames         */
	u32 hnc_tx_127_frames;	/* Tx 65-127 bytes frames     */
	u32 hnc_tx_255_frames;	/* Tx 128-255 bytes frames    */
	u32 hnc_tx_511_frames;	/* Tx 256-511 bytes frames    */
	u32 hnc_tx_1023_frames;	/* Tx 512-1023 bytes frames   */
	u32 hnc_tx_1522_frames;	/* Tx 1024-1522 bytes frames  */
	u32 hnc_tx_9022_frames;	/* Tx 1523-9022 bytes frames  */
	u32 hnc_rx_valid_bytes;	/* Rx valid bytes             */
	u32 hnc_rx_runt_pkts;	/* Rx error runt packets      */
	u32 hnc_rx_jabber_pkts;	/* Rx error jabber packets    */
	u32 ncsi_rx_cmds;	/* Rx NCSI commands           */
	u32 ncsi_dropped_cmds;	/* Dropped commands           */
	u32 ncsi_cmd_type_errs;	/* Command type errors        */
	u32 ncsi_cmd_csum_errs;	/* Command checksum errors    */
	u32 ncsi_rx_pkts;	/* Rx NCSI packets            */
	u32 ncsi_tx_pkts;	/* Tx NCSI packets            */
	u32 ncsi_tx_aen_pkts;	/* Tx AEN packets             */
	u32 pt_tx_pkts;		/* Tx packets                 */
	u32 pt_tx_dropped;	/* Tx dropped packets         */
	u32 pt_tx_channel_err;	/* Tx channel errors          */
	u32 pt_tx_us_err;	/* Tx undersize errors        */
	u32 pt_rx_pkts;		/* Rx packets                 */
	u32 pt_rx_dropped;	/* Rx dropped packets         */
	u32 pt_rx_channel_err;	/* Rx channel errors          */
	u32 pt_rx_us_err;	/* Rx undersize errors        */
	u32 pt_rx_os_err;	/* Rx oversize errors         */
};

struct ncsi_dev_priv;
struct ncsi_package;

#define NCSI_PACKAGE_SHIFT	5
#define NCSI_PACKAGE_INDEX(c)	(((c) >> NCSI_PACKAGE_SHIFT) & 0x7)
#define NCSI_CHANNEL_INDEX(c)	((c) & ((1 << NCSI_PACKAGE_SHIFT) - 1))
#define NCSI_TO_CHANNEL(p, c)	(((p) << NCSI_PACKAGE_SHIFT) | (c))

struct ncsi_channel {
	unsigned char               id;
	int                         state;
#define NCSI_CHANNEL_INACTIVE		1
#define NCSI_CHANNEL_ACTIVE		2
	spinlock_t                  lock;	/* Protect filters etc */
	struct ncsi_package         *package;
	struct ncsi_channel_version version;
	struct ncsi_channel_cap	    caps[NCSI_CAP_MAX];
	struct ncsi_channel_mode    modes[NCSI_MODE_MAX];
	struct ncsi_channel_filter  *filters[NCSI_FILTER_MAX];
	struct ncsi_channel_stats   stats;
	struct list_head            node;
};

struct ncsi_package {
	unsigned char        id;          /* NCSI 3-bits package ID */
	unsigned char        uuid[16];    /* UUID                   */
	struct ncsi_dev_priv *ndp;        /* NCSI device            */
	spinlock_t           lock;        /* Protect the package    */
	unsigned int         channel_num; /* Number of channels     */
	struct list_head     channels;    /* List of chanels        */
	struct list_head     node;        /* Form list of packages  */
};

struct ncsi_request {
	unsigned char        id;      /* Request ID - 0 to 255           */
	bool                 used;    /* Request that has been assigned  */
	bool                 driven;  /* Drive state machine             */
	struct ncsi_dev_priv *ndp;    /* Associated NCSI device          */
	struct sk_buff       *cmd;    /* Associated NCSI command packet  */
	struct sk_buff       *rsp;    /* Associated NCSI response packet */
	struct timer_list    timer;   /* Timer on waiting for response   */
	bool                 enabled; /* Time has been enabled or not    */
};

struct ncsi_dev_priv {
	struct ncsi_dev     ndev;            /* Associated NCSI device     */
	unsigned int        flags;           /* NCSI device flags          */
	spinlock_t          lock;            /* Protect the NCSI device    */
	unsigned int        package_num;     /* Number of packages         */
	struct list_head    packages;        /* List of packages           */
	struct ncsi_request requests[256];   /* Request table              */
	unsigned int        request_id;      /* Last used request ID       */
	struct list_head    node;            /* Form NCSI device list      */
};

struct ncsi_cmd_arg {
	struct ncsi_dev_priv *ndp;        /* Associated NCSI device        */
	unsigned char        type;        /* Command in the NCSI packet    */
	unsigned char        id;          /* Request ID (sequence number)  */
	unsigned char        package;     /* Destination package ID        */
	unsigned char        channel;     /* Detination channel ID or 0x1f */
	unsigned short       payload;     /* Command packet payload length */
	bool                 driven;      /* Drive the state machine?      */
	union {
		unsigned char  bytes[16]; /* Command packet specific data  */
		unsigned short words[8];
		unsigned int   dwords[4];
	};
};

extern struct list_head ncsi_dev_list;
extern spinlock_t ncsi_dev_lock;

#define TO_NCSI_DEV_PRIV(nd) \
	container_of(nd, struct ncsi_dev_priv, ndev)
#define NCSI_FOR_EACH_DEV(ndp) \
	list_for_each_entry_rcu(ndp, &ncsi_dev_list, node)
#define NCSI_FOR_EACH_PACKAGE(ndp, np) \
	list_for_each_entry_rcu(np, &ndp->packages, node)
#define NCSI_FOR_EACH_CHANNEL(np, nc) \
	list_for_each_entry_rcu(nc, &np->channels, node)

/* Resources */
int ncsi_find_filter(struct ncsi_channel *nc, int table, void *data);
int ncsi_add_filter(struct ncsi_channel *nc, int table, void *data);
int ncsi_remove_filter(struct ncsi_channel *nc, int table, int index);
struct ncsi_channel *ncsi_find_channel(struct ncsi_package *np,
				       unsigned char id);
struct ncsi_channel *ncsi_add_channel(struct ncsi_package *np,
				      unsigned char id);
struct ncsi_package *ncsi_find_package(struct ncsi_dev_priv *ndp,
				       unsigned char id);
struct ncsi_package *ncsi_add_package(struct ncsi_dev_priv *ndp,
				      unsigned char id);
void ncsi_remove_package(struct ncsi_package *np);
void ncsi_find_package_and_channel(struct ncsi_dev_priv *ndp,
				   unsigned char id,
				   struct ncsi_package **np,
				   struct ncsi_channel **nc);
struct ncsi_request *ncsi_alloc_request(struct ncsi_dev_priv *ndp, bool driven);
void ncsi_free_request(struct ncsi_request *nr);
struct ncsi_dev *ncsi_find_dev(struct net_device *dev);

/* Packet handlers */
u32 ncsi_calculate_checksum(unsigned char *data, int len);
int ncsi_xmit_cmd(struct ncsi_cmd_arg *nca);

#endif /* __NCSI_INTERNAL_H__ */
