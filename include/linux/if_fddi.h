/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */
#ifndef _LINUX_IF_FDDI_H
#define _LINUX_IF_FDDI_H

#include <linux/netdevice.h>
#include <uapi/linux/if_fddi.h>

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
#endif	/* _LINUX_IF_FDDI_H */
