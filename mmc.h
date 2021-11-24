/*
 * TC956X ethernet driver.
 *
 * mmc.h
 *
 * Copyright (C) 2011 STMicroelectronics Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  26 Oct 2021 : 1. Added EEE mmc counters for MAC COntrolled mode.
 *  VERSION     : 01-00-19
 *  24 Nov 2021 : 1. EEE update for runtime configuration and LPI interrupt disabled.
 *  VERSION     : 01-00-24 
 */

#ifndef __MMC_H__
#define __MMC_H__

/* MMC control register */
/* When set, all counter are reset */
#define MMC_CNTRL_COUNTER_RESET		0x1
/* When set, do not roll over zero after reaching the max value*/
#define MMC_CNTRL_COUNTER_STOP_ROLLOVER	0x2
#define MMC_CNTRL_RESET_ON_READ		0x4	/* Reset after reading */
#define MMC_CNTRL_COUNTER_FREEZER	0x8	/* Freeze counter values to the
						 * current value.
						 */
#define MMC_CNTRL_PRESET		0x10
#define MMC_CNTRL_FULL_HALF_PRESET	0x20

#define MMC_GMAC4_OFFSET		(MAC_OFFSET + 0x700)
#define MMC_GMAC3_X_OFFSET		(MAC_OFFSET + 0x100)
#define MMC_XGMAC_OFFSET		(MAC_OFFSET + 0x800)

#define MMC_GMAC4_OFFSET_BASE		(0x700)
#define MMC_GMAC3_X_OFFSET_BASE	(0x100)
#define MMC_XGMAC_OFFSET_BASE		(0x800)

struct tc956xmac_counters {
	u64 mmc_tx_broadcastframe_g;
	u64 mmc_tx_multicastframe_g;
	u64 mmc_tx_64_octets_gb;
	u64 mmc_tx_framecount_gb;
	u64 mmc_tx_65_to_127_octets_gb;
	u64 mmc_tx_128_to_255_octets_gb;
	u64 mmc_tx_256_to_511_octets_gb;
	u64 mmc_tx_octetcount_gb;
	u64 mmc_tx_512_to_1023_octets_gb;
	u64 mmc_tx_1024_to_max_octets_gb;
	u64 mmc_tx_unicast_gb;
	u64 mmc_tx_multicast_gb;
	u64 mmc_tx_broadcast_gb;
	u64 mmc_tx_underflow_error;
	u64 mmc_tx_singlecol_g;
	u64 mmc_tx_multicol_g;
	u64 mmc_tx_deferred;
	u64 mmc_tx_latecol;
	u64 mmc_tx_exesscol;
	u64 mmc_tx_carrier_error;
	u64 mmc_tx_octetcount_g;
	u64 mmc_tx_framecount_g;
	u64 mmc_tx_excessdef;
	u64 mmc_tx_pause_frame;
	u64 mmc_tx_vlan_frame_g;
	u64 mmc_tx_lpi_us_cntr;
	u64 mmc_tx_lpi_tran_cntr;
	u64 mmc_rx_lpi_us_cntr;
	u64 mmc_rx_lpi_tran_cntr;
	u64 mmc_tx_per_priority_pkt;
	u64 mmc_tx_per_priority_pfc_pkt;
	u64 mmc_tx_per_priority_gpfc_pkt;
	u64 mmc_tx_per_priority_octet_gb;

	/* MMC RX counter registers */
	u64 mmc_rx_framecount_gb;
	u64 mmc_rx_octetcount_gb;
	u64 mmc_rx_octetcount_g;
	u64 mmc_rx_broadcastframe_g;
	u64 mmc_rx_multicastframe_g;
	u64 mmc_rx_crc_error;
	u64 mmc_rx_align_error;
	u64 mmc_rx_run_error;
	u64 mmc_rx_jabber_error;
	u64 mmc_rx_undersize_g;
	u64 mmc_rx_oversize_g;
	u64 mmc_rx_64_octets_gb;
	u64 mmc_rx_65_to_127_octets_gb;
	u64 mmc_rx_128_to_255_octets_gb;
	u64 mmc_rx_256_to_511_octets_gb;
	u64 mmc_rx_512_to_1023_octets_gb;
	u64 mmc_rx_1024_to_max_octets_gb;
	u64 mmc_rx_unicast_g;
	u64 mmc_rx_length_error;
	u64 mmc_rx_autofrangetype;
	u64 mmc_rx_pause_frames;
	u64 mmc_rx_fifo_overflow;
	u64 mmc_rx_vlan_frames_gb;
	u64 mmc_rx_watchdog_error;
	u64 mmc_rx_per_priority_pkt;
	u64 mmc_rx_per_priority_pkt_bad;
	u64 mmc_rx_per_priority_pkt_pfc;
	u64 mmc_rx_per_priority_octet;
	/* IPC */
	u64 mmc_rx_ipc_intr_mask;
	u64 mmc_rx_ipc_intr;
	/* IPv4 */
	u64 mmc_rx_ipv4_gd;
	u64 mmc_rx_ipv4_hderr;
	u64 mmc_rx_ipv4_nopay;
	u64 mmc_rx_ipv4_frag;
	u64 mmc_rx_ipv4_udsbl;

	u64 mmc_rx_ipv4_gd_octets;
	u64 mmc_rx_ipv4_hderr_octets;
	u64 mmc_rx_ipv4_nopay_octets;
	u64 mmc_rx_ipv4_frag_octets;
	u64 mmc_rx_ipv4_udsbl_octets;

	/* IPV6 */
	u64 mmc_rx_ipv6_gd_octets;
	u64 mmc_rx_ipv6_hderr_octets;
	u64 mmc_rx_ipv6_nopay_octets;

	u64 mmc_rx_ipv6_gd;
	u64 mmc_rx_ipv6_hderr;
	u64 mmc_rx_ipv6_nopay;

	/* Protocols */
	u64 mmc_rx_udp_gd;
	u64 mmc_rx_udp_err;
	u64 mmc_rx_tcp_gd;
	u64 mmc_rx_tcp_err;
	u64 mmc_rx_icmp_gd;
	u64 mmc_rx_icmp_err;

	u64 mmc_rx_udp_gd_octets;
	u64 mmc_rx_udp_err_octets;
	u64 mmc_rx_tcp_gd_octets;
	u64 mmc_rx_tcp_err_octets;
	u64 mmc_rx_icmp_gd_octets;
	u64 mmc_rx_icmp_err_octets;

	/* FPE */
	u64 mmc_tx_fpe_fragment_cntr;
	u64 mmc_tx_hold_req_cntr;
	u64 mmc_rx_packet_assembly_err_cntr;
	u64 mmc_rx_packet_smd_err_cntr;
	u64 mmc_rx_packet_assembly_ok_cntr;
	u64 mmc_rx_fpe_fragment_cntr;
};

#endif /* __MMC_H__ */
