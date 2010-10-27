/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the ANSI FDDI interface.
 *
 * Version:	@(#)if_fddi.h	1.0.2	Sep 29 2004
 *
 * Author:	Lawrence V. Stefani, <stefani@lkg.dec.com>
 *
 *		if_fddi.h is based on previous if_ether.h and if_tr.h work by
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Donald Becker, <becker@super.org>
 *			Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *			Steve Whitehouse, <gw7rrm@eeshack3.swan.ac.uk>
 *			Peter De Schrijver, <stud11@cc4.kuleuven.ac.be>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_FDDI_H
#define _LINUX_IF_FDDI_H

#include <linux/types.h>

/*
 *  Define max and min legal sizes.  The frame sizes do not include
 *  4 byte FCS/CRC (frame check sequence).
 */
#define FDDI_K_ALEN			6		/* Octets in one FDDI address */
#define FDDI_K_8022_HLEN	16		/* Total octets in 802.2 header */
#define FDDI_K_SNAP_HLEN	21		/* Total octets in 802.2 SNAP header */
#define FDDI_K_8022_ZLEN	16		/* Min octets in 802.2 frame sans FCS */
#define FDDI_K_SNAP_ZLEN	21		/* Min octets in 802.2 SNAP frame sans FCS */
#define FDDI_K_8022_DLEN	4475	/* Max octets in 802.2 payload */
#define FDDI_K_SNAP_DLEN	4470	/* Max octets in 802.2 SNAP payload */
#define FDDI_K_LLC_ZLEN		13		/* Min octets in LLC frame sans FCS */
#define FDDI_K_LLC_LEN		4491	/* Max octets in LLC frame sans FCS */

/* Define FDDI Frame Control (FC) Byte values */
#define FDDI_FC_K_VOID					0x00	
#define FDDI_FC_K_NON_RESTRICTED_TOKEN	0x80	
#define FDDI_FC_K_RESTRICTED_TOKEN		0xC0	
#define FDDI_FC_K_SMT_MIN				0x41
#define FDDI_FC_K_SMT_MAX		   		0x4F
#define FDDI_FC_K_MAC_MIN				0xC1
#define FDDI_FC_K_MAC_MAX		  		0xCF	
#define FDDI_FC_K_ASYNC_LLC_MIN			0x50
#define FDDI_FC_K_ASYNC_LLC_DEF			0x54
#define FDDI_FC_K_ASYNC_LLC_MAX			0x5F
#define FDDI_FC_K_SYNC_LLC_MIN			0xD0
#define FDDI_FC_K_SYNC_LLC_MAX			0xD7
#define FDDI_FC_K_IMPLEMENTOR_MIN		0x60
#define FDDI_FC_K_IMPLEMENTOR_MAX  		0x6F
#define FDDI_FC_K_RESERVED_MIN			0x70
#define FDDI_FC_K_RESERVED_MAX			0x7F

/* Define LLC and SNAP constants */
#define FDDI_EXTENDED_SAP	0xAA
#define FDDI_UI_CMD			0x03

/* Define 802.2 Type 1 header */
struct fddi_8022_1_hdr {
	__u8	dsap;					/* destination service access point */
	__u8	ssap;					/* source service access point */
	__u8	ctrl;					/* control byte #1 */
} __attribute__((packed));

/* Define 802.2 Type 2 header */
struct fddi_8022_2_hdr {
	__u8	dsap;					/* destination service access point */
	__u8	ssap;					/* source service access point */
	__u8	ctrl_1;					/* control byte #1 */
	__u8	ctrl_2;					/* control byte #2 */
} __attribute__((packed));

/* Define 802.2 SNAP header */
#define FDDI_K_OUI_LEN	3
struct fddi_snap_hdr {
	__u8	dsap;					/* always 0xAA */
	__u8	ssap;					/* always 0xAA */
	__u8	ctrl;					/* always 0x03 */
	__u8	oui[FDDI_K_OUI_LEN];	/* organizational universal id */
	__be16	ethertype;				/* packet type ID field */
} __attribute__((packed));

/* Define FDDI LLC frame header */
struct fddihdr {
	__u8	fc;						/* frame control */
	__u8	daddr[FDDI_K_ALEN];		/* destination address */
	__u8	saddr[FDDI_K_ALEN];		/* source address */
	union
		{
		struct fddi_8022_1_hdr		llc_8022_1;
		struct fddi_8022_2_hdr		llc_8022_2;
		struct fddi_snap_hdr		llc_snap;
		} hdr;
} __attribute__((packed));

#ifdef __KERNEL__
#include <linux/netdevice.h>

/* Define FDDI statistics structure */
struct fddi_statistics {

	/* Generic statistics. */

	struct net_device_stats gen;

	/* Detailed FDDI statistics.  Adopted from RFC 1512 */

	__u8	smt_station_id[8];
	__u32	smt_op_version_id;
	__u32	smt_hi_version_id;
	__u32	smt_lo_version_id;
	__u8	smt_user_data[32];
	__u32	smt_mib_version_id;
	__u32	smt_mac_cts;
	__u32	smt_non_master_cts;
	__u32	smt_master_cts;
	__u32	smt_available_paths;
	__u32	smt_config_capabilities;
	__u32	smt_config_policy;
	__u32	smt_connection_policy;
	__u32	smt_t_notify;
	__u32	smt_stat_rpt_policy;
	__u32	smt_trace_max_expiration;
	__u32	smt_bypass_present;
	__u32	smt_ecm_state;
	__u32	smt_cf_state;
	__u32	smt_remote_disconnect_flag;
	__u32	smt_station_status;
	__u32	smt_peer_wrap_flag;
	__u32	smt_time_stamp;
	__u32	smt_transition_time_stamp;
	__u32	mac_frame_status_functions;
	__u32	mac_t_max_capability;
	__u32	mac_tvx_capability;
	__u32	mac_available_paths;
	__u32	mac_current_path;
	__u8	mac_upstream_nbr[FDDI_K_ALEN];
	__u8	mac_downstream_nbr[FDDI_K_ALEN];
	__u8	mac_old_upstream_nbr[FDDI_K_ALEN];
	__u8	mac_old_downstream_nbr[FDDI_K_ALEN];
	__u32	mac_dup_address_test;
	__u32	mac_requested_paths;
	__u32	mac_downstream_port_type;
	__u8	mac_smt_address[FDDI_K_ALEN];
	__u32	mac_t_req;
	__u32	mac_t_neg;
	__u32	mac_t_max;
	__u32	mac_tvx_value;
	__u32	mac_frame_cts;
	__u32	mac_copied_cts;
	__u32	mac_transmit_cts;
	__u32	mac_error_cts;
	__u32	mac_lost_cts;
	__u32	mac_frame_error_threshold;
	__u32	mac_frame_error_ratio;
	__u32	mac_rmt_state;
	__u32	mac_da_flag;
	__u32	mac_una_da_flag;
	__u32	mac_frame_error_flag;
	__u32	mac_ma_unitdata_available;
	__u32	mac_hardware_present;
	__u32	mac_ma_unitdata_enable;
	__u32	path_tvx_lower_bound;
	__u32	path_t_max_lower_bound;
	__u32	path_max_t_req;
	__u32	path_configuration[8];
	__u32	port_my_type[2];
	__u32	port_neighbor_type[2];
	__u32	port_connection_policies[2];
	__u32	port_mac_indicated[2];
	__u32	port_current_path[2];
	__u8	port_requested_paths[3*2];
	__u32	port_mac_placement[2];
	__u32	port_available_paths[2];
	__u32	port_pmd_class[2];
	__u32	port_connection_capabilities[2];
	__u32	port_bs_flag[2];
	__u32	port_lct_fail_cts[2];
	__u32	port_ler_estimate[2];
	__u32	port_lem_reject_cts[2];
	__u32	port_lem_cts[2];
	__u32	port_ler_cutoff[2];
	__u32	port_ler_alarm[2];
	__u32	port_connect_state[2];
	__u32	port_pcm_state[2];
	__u32	port_pc_withhold[2];
	__u32	port_ler_flag[2];
	__u32	port_hardware_present[2];
};
#endif /* __KERNEL__ */

#endif	/* _LINUX_IF_FDDI_H */
