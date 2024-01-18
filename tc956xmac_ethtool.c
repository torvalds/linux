/*
 * TC956X ethernet driver.
 *
 * tc956xmac_ethtool.c - Ethtool support
 *
 * Copyright (C) 2007-2009  STMicroelectronics Ltd
 * Copyright (C) 2023 Toshiba Electronic Devices & Storage Corporation
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
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  14 Oct 2021 : 1. Returning error on disabling Receive Flow Control via ethtool for speed other than 10G in XFI mode.
 *  VERSION     : 01-00-16
 *  19 Oct 2021 : 1. Adding M3 SRAM Debug counters to ethtool statistics
 *                2. Adding MTL RX Overflow/packet miss count, TX underflow counts,Rx Watchdog value to ethtool statistics.
 *  VERSION     : 01-00-17
 *  26 Oct 2021 : 1. Added set_wol and get_wol support using ethtool.
 *  VERSION     : 01-00-19
 *  24 Nov 2021 : 1. EEE update for runtime configuration through ethtool.
		  2. ethtool driver name display corrected
 *  VERSION     : 01-00-24
 *  10 Dec 2021 : 1. Added link partner pause frame count debug counters to ethtool statistics.
 *  VERSION     : 01-00-31
 *  04 Feb 2022 : 1. Ethtool statistics added to print doorbell SRAM area for all the channels.
 *  VERSION     : 01-00-41
 *  22 Mar 2022 : 1. PCI bus info updated for ethtool get driver version
 *  VERSION     : 01-00-46
 *  10 Nov 2023 : 1. Kernel 6.1 Porting changes
 *  VERSION     : 01-02-59
 */

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/mii.h>
#ifndef TC956X_SRIOV_VF
#include <linux/phylink.h>
#endif  /* TC956X_SRIOV_VF */
#include <linux/net_tstamp.h>
#include <asm/io.h>
#include <linux/iopoll.h>

#include "tc956xmac.h"
#include "dwxgmac2.h"

#define REG_SPACE_SIZE	11512/*Total Reg Len*/
#define MAC100_ETHTOOL_NAME	"tc956x_mac100"
#define GMAC_ETHTOOL_NAME	"tc956x_gmac"
#ifdef TC956X_SRIOV_PF
#define XGMAC_ETHTOOL_NAME	TC956X_RESOURCE_NAME
#elif defined TC956X_SRIOV_VF
#define XGMAC_ETHTOOL_NAME	"tc956x_vf_pcie_eth"
#endif
#define ETHTOOL_DMA_OFFSET	55
#ifdef TC956X_SRIOV_DEBUG
extern void tc956x_filter_debug(struct tc956xmac_priv *priv);
#endif
#ifndef TC956X_SRIOV_VF
void tc956xmac_get_pauseparam(struct net_device *netdev, struct ethtool_pauseparam *pause);
int tc956xmac_ethtool_op_get_eee(struct net_device *dev, struct ethtool_eee *edata);
#endif
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
#define TC956X_ADVERTISED_2500baseT_Full ETHTOOL_LINK_MODE_2500baseT_Full_BIT
#define TC956X_ADVERTISED_5000baseT_Full ETHTOOL_LINK_MODE_5000baseT_Full_BIT
#endif
struct tc956xmac_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define TC956XMAC_STAT(m)	\
	{ #m, sizeof_field(struct tc956xmac_extra_stats, m),	\
	offsetof(struct tc956xmac_priv, xstats.m)}

static const struct tc956xmac_stats tc956xmac_gstrings_stats[] = {
	/* Transmit errors */
	TC956XMAC_STAT(tx_underflow),
	TC956XMAC_STAT(tx_carrier),
	TC956XMAC_STAT(tx_losscarrier),
	TC956XMAC_STAT(vlan_tag),
	TC956XMAC_STAT(tx_deferred),
	TC956XMAC_STAT(tx_vlan),
	TC956XMAC_STAT(tx_jabber),
	TC956XMAC_STAT(tx_frame_flushed),
	TC956XMAC_STAT(tx_payload_error),
	TC956XMAC_STAT(tx_ip_header_error),
	/* Receive errors */
	TC956XMAC_STAT(rx_desc),
	TC956XMAC_STAT(sa_filter_fail),
	TC956XMAC_STAT(overflow_error),
	TC956XMAC_STAT(ipc_csum_error),
	TC956XMAC_STAT(rx_collision),
	TC956XMAC_STAT(rx_crc_errors),
	TC956XMAC_STAT(dribbling_bit),
	TC956XMAC_STAT(rx_length),
	TC956XMAC_STAT(rx_mii),
	TC956XMAC_STAT(rx_multicast),
	TC956XMAC_STAT(rx_gmac_overflow),
	TC956XMAC_STAT(rx_watchdog),
	TC956XMAC_STAT(da_rx_filter_fail),
	TC956XMAC_STAT(sa_rx_filter_fail),
	TC956XMAC_STAT(rx_missed_cntr),
	TC956XMAC_STAT(rx_overflow_cntr),
	TC956XMAC_STAT(rx_vlan),
	TC956XMAC_STAT(rx_split_hdr_pkt_n),
	/* Tx/Rx IRQ error info */
	TC956XMAC_STAT(tx_undeflow_irq),
	TC956XMAC_STAT(tx_process_stopped_irq[0]),
	TC956XMAC_STAT(tx_process_stopped_irq[1]),
	TC956XMAC_STAT(tx_process_stopped_irq[2]),
	TC956XMAC_STAT(tx_process_stopped_irq[3]),
	TC956XMAC_STAT(tx_process_stopped_irq[4]),
	TC956XMAC_STAT(tx_process_stopped_irq[5]),
	TC956XMAC_STAT(tx_process_stopped_irq[6]),
	TC956XMAC_STAT(tx_process_stopped_irq[7]),
	TC956XMAC_STAT(tx_jabber_irq),
	TC956XMAC_STAT(rx_overflow_irq),
	TC956XMAC_STAT(rx_buf_unav_irq[0]),
	TC956XMAC_STAT(rx_buf_unav_irq[1]),
	TC956XMAC_STAT(rx_buf_unav_irq[2]),
	TC956XMAC_STAT(rx_buf_unav_irq[3]),
	TC956XMAC_STAT(rx_buf_unav_irq[4]),
	TC956XMAC_STAT(rx_buf_unav_irq[5]),
	TC956XMAC_STAT(rx_buf_unav_irq[6]),
	TC956XMAC_STAT(rx_buf_unav_irq[7]),
	TC956XMAC_STAT(rx_process_stopped_irq),
	TC956XMAC_STAT(rx_watchdog_irq),
	TC956XMAC_STAT(tx_early_irq),
	TC956XMAC_STAT(fatal_bus_error_irq[0]),
	TC956XMAC_STAT(fatal_bus_error_irq[1]),
	TC956XMAC_STAT(fatal_bus_error_irq[2]),
	TC956XMAC_STAT(fatal_bus_error_irq[3]),
	TC956XMAC_STAT(fatal_bus_error_irq[4]),
	TC956XMAC_STAT(fatal_bus_error_irq[5]),
	TC956XMAC_STAT(fatal_bus_error_irq[6]),
	TC956XMAC_STAT(fatal_bus_error_irq[7]),
	/* Tx/Rx IRQ Events */
	TC956XMAC_STAT(rx_early_irq),
	TC956XMAC_STAT(threshold),
	TC956XMAC_STAT(tx_pkt_n[0]),
	TC956XMAC_STAT(tx_pkt_n[1]),
	TC956XMAC_STAT(tx_pkt_n[2]),
	TC956XMAC_STAT(tx_pkt_n[3]),
	TC956XMAC_STAT(tx_pkt_n[4]),
	TC956XMAC_STAT(tx_pkt_n[5]),
	TC956XMAC_STAT(tx_pkt_n[6]),
	TC956XMAC_STAT(tx_pkt_n[7]),
	TC956XMAC_STAT(tx_pkt_errors_n[0]),
	TC956XMAC_STAT(tx_pkt_errors_n[1]),
	TC956XMAC_STAT(tx_pkt_errors_n[2]),
	TC956XMAC_STAT(tx_pkt_errors_n[3]),
	TC956XMAC_STAT(tx_pkt_errors_n[4]),
	TC956XMAC_STAT(tx_pkt_errors_n[5]),
	TC956XMAC_STAT(tx_pkt_errors_n[6]),
	TC956XMAC_STAT(tx_pkt_errors_n[7]),
	TC956XMAC_STAT(rx_pkt_n[0]),
	TC956XMAC_STAT(rx_pkt_n[1]),
	TC956XMAC_STAT(rx_pkt_n[2]),
	TC956XMAC_STAT(rx_pkt_n[3]),
	TC956XMAC_STAT(rx_pkt_n[4]),
	TC956XMAC_STAT(rx_pkt_n[5]),
	TC956XMAC_STAT(rx_pkt_n[6]),
	TC956XMAC_STAT(rx_pkt_n[7]),
	TC956XMAC_STAT(normal_irq_n[0]),
	TC956XMAC_STAT(normal_irq_n[1]),
	TC956XMAC_STAT(normal_irq_n[2]),
	TC956XMAC_STAT(normal_irq_n[3]),
	TC956XMAC_STAT(normal_irq_n[4]),
	TC956XMAC_STAT(normal_irq_n[5]),
	TC956XMAC_STAT(normal_irq_n[6]),
	TC956XMAC_STAT(normal_irq_n[7]),
	TC956XMAC_STAT(rx_normal_irq_n[0]),
	TC956XMAC_STAT(rx_normal_irq_n[1]),
	TC956XMAC_STAT(rx_normal_irq_n[2]),
	TC956XMAC_STAT(rx_normal_irq_n[3]),
	TC956XMAC_STAT(rx_normal_irq_n[4]),
	TC956XMAC_STAT(rx_normal_irq_n[5]),
	TC956XMAC_STAT(rx_normal_irq_n[6]),
	TC956XMAC_STAT(rx_normal_irq_n[7]),
	TC956XMAC_STAT(napi_poll_tx[0]),
	TC956XMAC_STAT(napi_poll_tx[1]),
	TC956XMAC_STAT(napi_poll_tx[2]),
	TC956XMAC_STAT(napi_poll_tx[3]),
	TC956XMAC_STAT(napi_poll_tx[4]),
	TC956XMAC_STAT(napi_poll_tx[5]),
	TC956XMAC_STAT(napi_poll_tx[6]),
	TC956XMAC_STAT(napi_poll_tx[7]),
	TC956XMAC_STAT(napi_poll_rx[0]),
	TC956XMAC_STAT(napi_poll_rx[1]),
	TC956XMAC_STAT(napi_poll_rx[2]),
	TC956XMAC_STAT(napi_poll_rx[3]),
	TC956XMAC_STAT(napi_poll_rx[4]),
	TC956XMAC_STAT(napi_poll_rx[5]),
	TC956XMAC_STAT(napi_poll_rx[6]),
	TC956XMAC_STAT(napi_poll_rx[7]),
	TC956XMAC_STAT(tx_normal_irq_n[0]),
	TC956XMAC_STAT(tx_normal_irq_n[1]),
	TC956XMAC_STAT(tx_normal_irq_n[2]),
	TC956XMAC_STAT(tx_normal_irq_n[3]),
	TC956XMAC_STAT(tx_normal_irq_n[4]),
	TC956XMAC_STAT(tx_normal_irq_n[5]),
	TC956XMAC_STAT(tx_normal_irq_n[6]),
	TC956XMAC_STAT(tx_normal_irq_n[7]),
	TC956XMAC_STAT(tx_clean[0]),
	TC956XMAC_STAT(tx_clean[1]),
	TC956XMAC_STAT(tx_clean[2]),
	TC956XMAC_STAT(tx_clean[3]),
	TC956XMAC_STAT(tx_clean[4]),
	TC956XMAC_STAT(tx_clean[5]),
	TC956XMAC_STAT(tx_clean[6]),
	TC956XMAC_STAT(tx_clean[7]),
	TC956XMAC_STAT(tx_set_ic_bit),
	TC956XMAC_STAT(irq_receive_pmt_irq_n),
	/* MMC info */
	TC956XMAC_STAT(mmc_tx_irq_n),
	TC956XMAC_STAT(mmc_rx_irq_n),
	TC956XMAC_STAT(mmc_rx_csum_offload_irq_n),
	/* EEE */
	TC956XMAC_STAT(irq_tx_path_in_lpi_mode_n),
	TC956XMAC_STAT(irq_tx_path_exit_lpi_mode_n),
	TC956XMAC_STAT(irq_rx_path_in_lpi_mode_n),
	TC956XMAC_STAT(irq_rx_path_exit_lpi_mode_n),
	TC956XMAC_STAT(phy_eee_wakeup_error_n),
	/* Extended RDES status */
	TC956XMAC_STAT(ip_hdr_err),
	TC956XMAC_STAT(ip_payload_err),
	TC956XMAC_STAT(ip_csum_bypassed),
	TC956XMAC_STAT(ipv4_pkt_rcvd),
	TC956XMAC_STAT(ipv6_pkt_rcvd),
	TC956XMAC_STAT(no_ptp_rx_msg_type_ext),
	TC956XMAC_STAT(ptp_rx_msg_type_sync),
	TC956XMAC_STAT(ptp_rx_msg_type_follow_up),
	TC956XMAC_STAT(ptp_rx_msg_type_delay_req),
	TC956XMAC_STAT(ptp_rx_msg_type_delay_resp),
	TC956XMAC_STAT(ptp_rx_msg_type_pdelay_req),
	TC956XMAC_STAT(ptp_rx_msg_type_pdelay_resp),
	TC956XMAC_STAT(ptp_rx_msg_type_pdelay_follow_up),
	TC956XMAC_STAT(ptp_rx_msg_type_announce),
	TC956XMAC_STAT(ptp_rx_msg_type_management),
	TC956XMAC_STAT(ptp_rx_msg_pkt_reserved_type),
	TC956XMAC_STAT(ptp_frame_type),
	TC956XMAC_STAT(ptp_ver),
	TC956XMAC_STAT(timestamp_dropped),
	TC956XMAC_STAT(av_pkt_rcvd),
	TC956XMAC_STAT(av_tagged_pkt_rcvd),
	TC956XMAC_STAT(vlan_tag_priority_val),
	TC956XMAC_STAT(l3_filter_match),
	TC956XMAC_STAT(l4_filter_match),
	TC956XMAC_STAT(l3_l4_filter_no_match),
	/* PCS */
	TC956XMAC_STAT(irq_pcs_ane_n),
	TC956XMAC_STAT(irq_pcs_link_n),
	TC956XMAC_STAT(irq_rgmii_n),
	/* DEBUG */
	TC956XMAC_STAT(mtl_tx_status_fifo_full),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[0]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[1]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[2]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[3]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[4]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[5]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[6]),
	TC956XMAC_STAT(mtl_tx_fifo_not_empty[7]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[0]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[1]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[3]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[4]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[5]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[6]),
	TC956XMAC_STAT(mmtl_fifo_ctrl[7]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[0]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[1]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[2]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[3]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[4]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[5]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[6]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_write[7]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[0]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[1]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[2]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[3]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[4]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[5]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[6]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_wait[7]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[0]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[1]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[2]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[3]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[4]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[5]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[6]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_read[7]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[0]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[1]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[2]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[3]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[4]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[5]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[6]),
	TC956XMAC_STAT(mtl_tx_fifo_read_ctrl_idle[7]),
	TC956XMAC_STAT(mac_tx_in_pause[0]),
	TC956XMAC_STAT(mac_tx_in_pause[1]),
	TC956XMAC_STAT(mac_tx_in_pause[2]),
	TC956XMAC_STAT(mac_tx_in_pause[3]),
	TC956XMAC_STAT(mac_tx_in_pause[4]),
	TC956XMAC_STAT(mac_tx_in_pause[5]),
	TC956XMAC_STAT(mac_tx_in_pause[6]),
	TC956XMAC_STAT(mac_tx_in_pause[7]),
	TC956XMAC_STAT(mac_tx_frame_ctrl_xfer),
	TC956XMAC_STAT(mac_tx_frame_ctrl_idle),
	TC956XMAC_STAT(mac_tx_frame_ctrl_wait),
	TC956XMAC_STAT(mac_tx_frame_ctrl_pause),
	TC956XMAC_STAT(mac_gmii_tx_proto_engine),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[0]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[1]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[2]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[3]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[4]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[5]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[6]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_full[7]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[0]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[1]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[2]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[3]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[4]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[5]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[6]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_above_thresh[7]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[0]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[1]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[2]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[3]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[4]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[5]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[6]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_below_thresh[7]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[0]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[1]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[2]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[3]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[4]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[5]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[6]),
	TC956XMAC_STAT(mtl_rx_fifo_fill_level_empty[7]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[0]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[1]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[2]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[3]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[4]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[5]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[6]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_flush[7]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[0]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[1]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[2]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[3]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[4]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[5]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[6]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_read[7]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[0]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[1]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[2]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[3]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[4]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[5]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[6]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_status[7]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[0]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[1]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[2]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[3]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[4]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[5]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[6]),
	TC956XMAC_STAT(mtl_rx_fifo_read_ctrl_idle[7]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[0]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[1]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[2]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[3]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[4]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[5]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[6]),
	TC956XMAC_STAT(mtl_rx_fifo_ctrl_active[7]),
	TC956XMAC_STAT(mac_rx_frame_ctrl_fifo),
	TC956XMAC_STAT(mac_gmii_rx_proto_engine),
	/* TSO */
	TC956XMAC_STAT(tx_tso_frames[0]),
	TC956XMAC_STAT(tx_tso_frames[1]),
	TC956XMAC_STAT(tx_tso_frames[2]),
	TC956XMAC_STAT(tx_tso_frames[3]),
	TC956XMAC_STAT(tx_tso_frames[4]),
	TC956XMAC_STAT(tx_tso_frames[5]),
	TC956XMAC_STAT(tx_tso_frames[6]),
	TC956XMAC_STAT(tx_tso_frames[7]),
	TC956XMAC_STAT(tx_tso_nfrags[0]),
	TC956XMAC_STAT(tx_tso_nfrags[1]),
	TC956XMAC_STAT(tx_tso_nfrags[2]),
	TC956XMAC_STAT(tx_tso_nfrags[3]),
	TC956XMAC_STAT(tx_tso_nfrags[4]),
	TC956XMAC_STAT(tx_tso_nfrags[5]),
	TC956XMAC_STAT(tx_tso_nfrags[6]),
	TC956XMAC_STAT(tx_tso_nfrags[7]),

	/* Tx Desc statistics */
	TC956XMAC_STAT(txch_status[0]),
	TC956XMAC_STAT(txch_status[1]),
	TC956XMAC_STAT(txch_status[2]),
	TC956XMAC_STAT(txch_status[3]),
	TC956XMAC_STAT(txch_status[4]),
	TC956XMAC_STAT(txch_status[5]),
	TC956XMAC_STAT(txch_status[6]),
	TC956XMAC_STAT(txch_status[7]),
	TC956XMAC_STAT(txch_control[0]),
	TC956XMAC_STAT(txch_control[1]),
	TC956XMAC_STAT(txch_control[2]),
	TC956XMAC_STAT(txch_control[3]),
	TC956XMAC_STAT(txch_control[4]),
	TC956XMAC_STAT(txch_control[5]),
	TC956XMAC_STAT(txch_control[6]),
	TC956XMAC_STAT(txch_control[7]),
	TC956XMAC_STAT(txch_desc_list_haddr[0]),
	TC956XMAC_STAT(txch_desc_list_haddr[1]),
	TC956XMAC_STAT(txch_desc_list_haddr[2]),
	TC956XMAC_STAT(txch_desc_list_haddr[3]),
	TC956XMAC_STAT(txch_desc_list_haddr[4]),
	TC956XMAC_STAT(txch_desc_list_haddr[5]),
	TC956XMAC_STAT(txch_desc_list_haddr[6]),
	TC956XMAC_STAT(txch_desc_list_haddr[7]),
	TC956XMAC_STAT(txch_desc_list_laddr[0]),
	TC956XMAC_STAT(txch_desc_list_laddr[1]),
	TC956XMAC_STAT(txch_desc_list_laddr[2]),
	TC956XMAC_STAT(txch_desc_list_laddr[3]),
	TC956XMAC_STAT(txch_desc_list_laddr[4]),
	TC956XMAC_STAT(txch_desc_list_laddr[5]),
	TC956XMAC_STAT(txch_desc_list_laddr[6]),
	TC956XMAC_STAT(txch_desc_list_laddr[7]),
	TC956XMAC_STAT(txch_desc_ring_len[0]),
	TC956XMAC_STAT(txch_desc_ring_len[1]),
	TC956XMAC_STAT(txch_desc_ring_len[2]),
	TC956XMAC_STAT(txch_desc_ring_len[3]),
	TC956XMAC_STAT(txch_desc_ring_len[4]),
	TC956XMAC_STAT(txch_desc_ring_len[5]),
	TC956XMAC_STAT(txch_desc_ring_len[6]),
	TC956XMAC_STAT(txch_desc_ring_len[7]),
	TC956XMAC_STAT(txch_desc_curr_haddr[0]),
	TC956XMAC_STAT(txch_desc_curr_haddr[1]),
	TC956XMAC_STAT(txch_desc_curr_haddr[2]),
	TC956XMAC_STAT(txch_desc_curr_haddr[3]),
	TC956XMAC_STAT(txch_desc_curr_haddr[4]),
	TC956XMAC_STAT(txch_desc_curr_haddr[5]),
	TC956XMAC_STAT(txch_desc_curr_haddr[6]),
	TC956XMAC_STAT(txch_desc_curr_haddr[7]),
	TC956XMAC_STAT(txch_desc_curr_laddr[0]),
	TC956XMAC_STAT(txch_desc_curr_laddr[1]),
	TC956XMAC_STAT(txch_desc_curr_laddr[2]),
	TC956XMAC_STAT(txch_desc_curr_laddr[3]),
	TC956XMAC_STAT(txch_desc_curr_laddr[4]),
	TC956XMAC_STAT(txch_desc_curr_laddr[5]),
	TC956XMAC_STAT(txch_desc_curr_laddr[6]),
	TC956XMAC_STAT(txch_desc_curr_laddr[7]),
	TC956XMAC_STAT(txch_desc_tail[0]),
	TC956XMAC_STAT(txch_desc_tail[1]),
	TC956XMAC_STAT(txch_desc_tail[2]),
	TC956XMAC_STAT(txch_desc_tail[3]),
	TC956XMAC_STAT(txch_desc_tail[4]),
	TC956XMAC_STAT(txch_desc_tail[5]),
	TC956XMAC_STAT(txch_desc_tail[6]),
	TC956XMAC_STAT(txch_desc_tail[7]),
	TC956XMAC_STAT(txch_desc_buf_haddr[0]),
	TC956XMAC_STAT(txch_desc_buf_haddr[1]),
	TC956XMAC_STAT(txch_desc_buf_haddr[2]),
	TC956XMAC_STAT(txch_desc_buf_haddr[3]),
	TC956XMAC_STAT(txch_desc_buf_haddr[4]),
	TC956XMAC_STAT(txch_desc_buf_haddr[5]),
	TC956XMAC_STAT(txch_desc_buf_haddr[6]),
	TC956XMAC_STAT(txch_desc_buf_haddr[7]),
	TC956XMAC_STAT(txch_desc_buf_laddr[0]),
	TC956XMAC_STAT(txch_desc_buf_laddr[1]),
	TC956XMAC_STAT(txch_desc_buf_laddr[2]),
	TC956XMAC_STAT(txch_desc_buf_laddr[3]),
	TC956XMAC_STAT(txch_desc_buf_laddr[4]),
	TC956XMAC_STAT(txch_desc_buf_laddr[5]),
	TC956XMAC_STAT(txch_desc_buf_laddr[6]),
	TC956XMAC_STAT(txch_desc_buf_laddr[7]),
	TC956XMAC_STAT(txch_sw_cur_tx[0]),
	TC956XMAC_STAT(txch_sw_cur_tx[1]),
	TC956XMAC_STAT(txch_sw_cur_tx[2]),
	TC956XMAC_STAT(txch_sw_cur_tx[3]),
	TC956XMAC_STAT(txch_sw_cur_tx[4]),
	TC956XMAC_STAT(txch_sw_cur_tx[5]),
	TC956XMAC_STAT(txch_sw_cur_tx[6]),
	TC956XMAC_STAT(txch_sw_cur_tx[7]),
	TC956XMAC_STAT(txch_sw_dirty_tx[0]),
	TC956XMAC_STAT(txch_sw_dirty_tx[1]),
	TC956XMAC_STAT(txch_sw_dirty_tx[2]),
	TC956XMAC_STAT(txch_sw_dirty_tx[3]),
	TC956XMAC_STAT(txch_sw_dirty_tx[4]),
	TC956XMAC_STAT(txch_sw_dirty_tx[5]),
	TC956XMAC_STAT(txch_sw_dirty_tx[6]),
	TC956XMAC_STAT(txch_sw_dirty_tx[7]),

	/* Rx Desc statistics */
	TC956XMAC_STAT(rxch_status[0]),
	TC956XMAC_STAT(rxch_status[1]),
	TC956XMAC_STAT(rxch_status[2]),
	TC956XMAC_STAT(rxch_status[3]),
	TC956XMAC_STAT(rxch_status[4]),
	TC956XMAC_STAT(rxch_status[5]),
	TC956XMAC_STAT(rxch_status[6]),
	TC956XMAC_STAT(rxch_status[7]),
	TC956XMAC_STAT(rxch_control[0]),
	TC956XMAC_STAT(rxch_control[1]),
	TC956XMAC_STAT(rxch_control[2]),
	TC956XMAC_STAT(rxch_control[3]),
	TC956XMAC_STAT(rxch_control[4]),
	TC956XMAC_STAT(rxch_control[5]),
	TC956XMAC_STAT(rxch_control[6]),
	TC956XMAC_STAT(rxch_control[7]),
	TC956XMAC_STAT(rxch_desc_list_haddr[0]),
	TC956XMAC_STAT(rxch_desc_list_haddr[1]),
	TC956XMAC_STAT(rxch_desc_list_haddr[2]),
	TC956XMAC_STAT(rxch_desc_list_haddr[3]),
	TC956XMAC_STAT(rxch_desc_list_haddr[4]),
	TC956XMAC_STAT(rxch_desc_list_haddr[5]),
	TC956XMAC_STAT(rxch_desc_list_haddr[6]),
	TC956XMAC_STAT(rxch_desc_list_haddr[7]),
	TC956XMAC_STAT(rxch_desc_list_laddr[0]),
	TC956XMAC_STAT(rxch_desc_list_laddr[1]),
	TC956XMAC_STAT(rxch_desc_list_laddr[2]),
	TC956XMAC_STAT(rxch_desc_list_laddr[3]),
	TC956XMAC_STAT(rxch_desc_list_laddr[4]),
	TC956XMAC_STAT(rxch_desc_list_laddr[5]),
	TC956XMAC_STAT(rxch_desc_list_laddr[6]),
	TC956XMAC_STAT(rxch_desc_list_laddr[7]),
	TC956XMAC_STAT(rxch_desc_ring_len[0]),
	TC956XMAC_STAT(rxch_desc_ring_len[1]),
	TC956XMAC_STAT(rxch_desc_ring_len[2]),
	TC956XMAC_STAT(rxch_desc_ring_len[3]),
	TC956XMAC_STAT(rxch_desc_ring_len[4]),
	TC956XMAC_STAT(rxch_desc_ring_len[5]),
	TC956XMAC_STAT(rxch_desc_ring_len[6]),
	TC956XMAC_STAT(rxch_desc_ring_len[7]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[0]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[1]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[2]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[3]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[4]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[5]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[6]),
	TC956XMAC_STAT(rxch_desc_curr_haddr[7]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[0]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[1]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[2]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[3]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[4]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[5]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[6]),
	TC956XMAC_STAT(rxch_desc_curr_laddr[7]),
	TC956XMAC_STAT(rxch_desc_tail[0]),
	TC956XMAC_STAT(rxch_desc_tail[1]),
	TC956XMAC_STAT(rxch_desc_tail[2]),
	TC956XMAC_STAT(rxch_desc_tail[3]),
	TC956XMAC_STAT(rxch_desc_tail[4]),
	TC956XMAC_STAT(rxch_desc_tail[5]),
	TC956XMAC_STAT(rxch_desc_tail[6]),
	TC956XMAC_STAT(rxch_desc_tail[7]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[0]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[1]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[2]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[3]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[4]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[5]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[6]),
	TC956XMAC_STAT(rxch_desc_buf_haddr[7]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[0]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[1]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[2]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[3]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[4]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[5]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[6]),
	TC956XMAC_STAT(rxch_desc_buf_laddr[7]),
	TC956XMAC_STAT(rxch_sw_cur_rx[0]),
	TC956XMAC_STAT(rxch_sw_cur_rx[1]),
	TC956XMAC_STAT(rxch_sw_cur_rx[2]),
	TC956XMAC_STAT(rxch_sw_cur_rx[3]),
	TC956XMAC_STAT(rxch_sw_cur_rx[4]),
	TC956XMAC_STAT(rxch_sw_cur_rx[5]),
	TC956XMAC_STAT(rxch_sw_cur_rx[6]),
	TC956XMAC_STAT(rxch_sw_cur_rx[7]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[0]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[1]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[2]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[3]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[4]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[5]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[6]),
	TC956XMAC_STAT(rxch_sw_dirty_rx[7]),
	TC956XMAC_STAT(total_interrupts),
	TC956XMAC_STAT(lpi_intr_n),
	TC956XMAC_STAT(pmt_intr_n),
	TC956XMAC_STAT(event_intr_n),
	TC956XMAC_STAT(tx_intr_n),
	TC956XMAC_STAT(rx_intr_n),
	TC956XMAC_STAT(xpcs_intr_n),
	TC956XMAC_STAT(phy_intr_n),
	TC956XMAC_STAT(sw_msi_n),
	TC956XMAC_STAT(mtl_tx_underflow[0]),
	TC956XMAC_STAT(mtl_tx_underflow[1]),
	TC956XMAC_STAT(mtl_tx_underflow[3]),
	TC956XMAC_STAT(mtl_tx_underflow[4]),
	TC956XMAC_STAT(mtl_tx_underflow[5]),
	TC956XMAC_STAT(mtl_tx_underflow[6]),
	TC956XMAC_STAT(mtl_tx_underflow[7]),

	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[0]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[1]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[3]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[4]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[5]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[6]),
	TC956XMAC_STAT(mtl_rx_miss_pkt_cnt[7]),

	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[0]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[1]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[3]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[4]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[5]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[6]),
	TC956XMAC_STAT(mtl_rx_overflow_pkt_cnt[7]),

	TC956XMAC_STAT(rxch_watchdog_timer[0]),
	TC956XMAC_STAT(rxch_watchdog_timer[1]),
	TC956XMAC_STAT(rxch_watchdog_timer[2]),
	TC956XMAC_STAT(rxch_watchdog_timer[3]),
	TC956XMAC_STAT(rxch_watchdog_timer[4]),
	TC956XMAC_STAT(rxch_watchdog_timer[5]),
	TC956XMAC_STAT(rxch_watchdog_timer[6]),
	TC956XMAC_STAT(rxch_watchdog_timer[7]),
	TC956XMAC_STAT(link_partner_pause_frame_cnt),

	TC956XMAC_STAT(m3_debug_cnt0),
	TC956XMAC_STAT(m3_debug_cnt1),
	TC956XMAC_STAT(m3_debug_cnt2),
	TC956XMAC_STAT(m3_debug_cnt3),
	TC956XMAC_STAT(m3_debug_cnt4),
	TC956XMAC_STAT(m3_debug_cnt5),
	TC956XMAC_STAT(m3_debug_cnt6),
	TC956XMAC_STAT(m3_debug_cnt7),
	TC956XMAC_STAT(m3_debug_cnt8),
	TC956XMAC_STAT(m3_debug_cnt9),
	TC956XMAC_STAT(m3_debug_cnt10),
	TC956XMAC_STAT(m3_watchdog_exp_cnt),
	TC956XMAC_STAT(m3_watchdog_monitor_cnt),
	TC956XMAC_STAT(m3_debug_cnt13),
	TC956XMAC_STAT(m3_debug_cnt14),
	TC956XMAC_STAT(m3_systick_cnt_upper_value),
	TC956XMAC_STAT(m3_systick_cnt_lower_value),
	TC956XMAC_STAT(m3_tx_timeout_port0),
	TC956XMAC_STAT(m3_tx_timeout_port1),
	TC956XMAC_STAT(m3_debug_cnt19),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[0]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[1]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[2]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[3]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[4]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[5]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[6]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port0[7]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[0]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[1]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[2]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[3]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[4]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[5]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[6]),
	TC956XMAC_STAT(m3_tx_pcie_addr_loc_port1[7]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[0]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[1]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[2]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[3]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[4]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[5]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[6]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port0[7]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[0]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[1]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[2]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[3]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[4]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[5]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[6]),
	TC956XMAC_STAT(m3_rx_pcie_addr_loc_port1[7]),

#ifdef TC956X_SRIOV_PF
	TC956XMAC_STAT(mbx_pf_sent_vf[0]),
	TC956XMAC_STAT(mbx_pf_sent_vf[1]),
	TC956XMAC_STAT(mbx_pf_sent_vf[2]),
	TC956XMAC_STAT(mbx_pf_rcvd_vf[0]),
	TC956XMAC_STAT(mbx_pf_rcvd_vf[1]),
	TC956XMAC_STAT(mbx_pf_rcvd_vf[2]),
#else
	TC956XMAC_STAT(mbx_vf_sent_pf),
	TC956XMAC_STAT(mbx_vf_rcvd_pf),
#endif

};
#define TC956XMAC_STATS_LEN ARRAY_SIZE(tc956xmac_gstrings_stats)

/* HW MAC Management counters (if supported) */
#define TC956XMAC_MMC_STAT(m)	\
	{ #m, sizeof_field(struct tc956xmac_counters, m),	\
	offsetof(struct tc956xmac_priv, mmc.m)}

static const struct tc956xmac_stats tc956xmac_mmc[] = {
	TC956XMAC_MMC_STAT(mmc_tx_octetcount_gb),
	TC956XMAC_MMC_STAT(mmc_tx_framecount_gb),
	TC956XMAC_MMC_STAT(mmc_tx_broadcastframe_g),
	TC956XMAC_MMC_STAT(mmc_tx_multicastframe_g),
	TC956XMAC_MMC_STAT(mmc_tx_64_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_65_to_127_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_128_to_255_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_256_to_511_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_512_to_1023_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_1024_to_max_octets_gb),
	TC956XMAC_MMC_STAT(mmc_tx_unicast_gb),
	TC956XMAC_MMC_STAT(mmc_tx_multicast_gb),
	TC956XMAC_MMC_STAT(mmc_tx_broadcast_gb),
	TC956XMAC_MMC_STAT(mmc_tx_underflow_error),
	TC956XMAC_MMC_STAT(mmc_tx_singlecol_g),
	TC956XMAC_MMC_STAT(mmc_tx_multicol_g),
	TC956XMAC_MMC_STAT(mmc_tx_deferred),
	TC956XMAC_MMC_STAT(mmc_tx_latecol),
	TC956XMAC_MMC_STAT(mmc_tx_exesscol),
	TC956XMAC_MMC_STAT(mmc_tx_carrier_error),
	TC956XMAC_MMC_STAT(mmc_tx_octetcount_g),
	TC956XMAC_MMC_STAT(mmc_tx_framecount_g),
	TC956XMAC_MMC_STAT(mmc_tx_excessdef),
	TC956XMAC_MMC_STAT(mmc_tx_pause_frame),
	TC956XMAC_MMC_STAT(mmc_tx_vlan_frame_g),
	TC956XMAC_MMC_STAT(mmc_tx_lpi_tran_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_lpi_tran_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_framecount_gb),
	TC956XMAC_MMC_STAT(mmc_rx_octetcount_gb),
	TC956XMAC_MMC_STAT(mmc_rx_octetcount_g),
	TC956XMAC_MMC_STAT(mmc_rx_broadcastframe_g),
	TC956XMAC_MMC_STAT(mmc_rx_multicastframe_g),
	TC956XMAC_MMC_STAT(mmc_rx_crc_error),
	TC956XMAC_MMC_STAT(mmc_rx_align_error),
	TC956XMAC_MMC_STAT(mmc_rx_run_error),
	TC956XMAC_MMC_STAT(mmc_rx_jabber_error),
	TC956XMAC_MMC_STAT(mmc_rx_undersize_g),
	TC956XMAC_MMC_STAT(mmc_rx_oversize_g),
	TC956XMAC_MMC_STAT(mmc_rx_64_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_65_to_127_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_128_to_255_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_256_to_511_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_512_to_1023_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_1024_to_max_octets_gb),
	TC956XMAC_MMC_STAT(mmc_rx_unicast_g),
	TC956XMAC_MMC_STAT(mmc_rx_length_error),
	TC956XMAC_MMC_STAT(mmc_rx_autofrangetype),
	TC956XMAC_MMC_STAT(mmc_rx_pause_frames),
	TC956XMAC_MMC_STAT(mmc_rx_fifo_overflow),
	TC956XMAC_MMC_STAT(mmc_rx_vlan_frames_gb),
	TC956XMAC_MMC_STAT(mmc_rx_watchdog_error),
	TC956XMAC_MMC_STAT(mmc_rx_ipc_intr_mask),
	TC956XMAC_MMC_STAT(mmc_rx_ipc_intr),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_gd),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_hderr),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_nopay),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_frag),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_udsbl),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_gd_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_hderr_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_nopay_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_frag_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv4_udsbl_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_gd_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_hderr_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_nopay_octets),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_gd),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_hderr),
	TC956XMAC_MMC_STAT(mmc_rx_ipv6_nopay),
	TC956XMAC_MMC_STAT(mmc_rx_udp_gd),
	TC956XMAC_MMC_STAT(mmc_rx_udp_err),
	TC956XMAC_MMC_STAT(mmc_rx_tcp_gd),
	TC956XMAC_MMC_STAT(mmc_rx_tcp_err),
	TC956XMAC_MMC_STAT(mmc_rx_icmp_gd),
	TC956XMAC_MMC_STAT(mmc_rx_icmp_err),
	TC956XMAC_MMC_STAT(mmc_rx_udp_gd_octets),
	TC956XMAC_MMC_STAT(mmc_rx_udp_err_octets),
	TC956XMAC_MMC_STAT(mmc_rx_tcp_gd_octets),
	TC956XMAC_MMC_STAT(mmc_rx_tcp_err_octets),
	TC956XMAC_MMC_STAT(mmc_rx_icmp_gd_octets),
	TC956XMAC_MMC_STAT(mmc_rx_icmp_err_octets),
	TC956XMAC_MMC_STAT(mmc_tx_fpe_fragment_cntr),
	TC956XMAC_MMC_STAT(mmc_tx_hold_req_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_packet_assembly_err_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_packet_smd_err_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_packet_assembly_ok_cntr),
	TC956XMAC_MMC_STAT(mmc_rx_fpe_fragment_cntr),
};
#define TC956XMAC_MMC_STATS_LEN ARRAY_SIZE(tc956xmac_mmc)

#ifdef TC956X_SRIOV_VF
/* SW counters */
#define TC956X_SW_STAT(m)	\
	{ #m, sizeof_field(struct tc956x_sw_counters, m),	\
	offsetof(struct tc956xmac_priv, sw_stats.m)}

static const struct tc956xmac_stats tc956x_sw[] = {
	TC956X_SW_STAT(tx_frame_count_good_bad),
	TC956X_SW_STAT(rx_frame_count_good_bad),
	TC956X_SW_STAT(rx_frame_count_good),
	TC956X_SW_STAT(rx_fame_count_bad),
	TC956X_SW_STAT(rx_packet_good_octets),
	TC956X_SW_STAT(rx_header_good_octets),
	TC956X_SW_STAT(rx_av_tagged_datapacket_count),
	TC956X_SW_STAT(rx_av_tagged_controlpacket_count),
	TC956X_SW_STAT(rx_nonav_packet_count),
	TC956X_SW_STAT(rx_tunnel_packet_count),
	TC956X_SW_STAT(rx_non_ip_pkt_count),
	TC956X_SW_STAT(rx_ipv4_tcp_pkt_count),
	TC956X_SW_STAT(rx_ipv4_udp_pkt_count),
	TC956X_SW_STAT(rx_ipv4_icmp_pkt_count),
	TC956X_SW_STAT(rx_ipv4_igmp_pkt_count),
	TC956X_SW_STAT(rx_ipv4_unkown_pkt_count),
	TC956X_SW_STAT(rx_ipv6_tcp_pkt_count),
	TC956X_SW_STAT(rx_ipv6_udp_pkt_count),
	TC956X_SW_STAT(rx_ipv6_icmp_pkt_count),
	TC956X_SW_STAT(rx_ipv6_unkown_pkt_count),
	TC956X_SW_STAT(rx_err_wd_timeout_count),
	TC956X_SW_STAT(rx_err_gmii_inv_count),
	TC956X_SW_STAT(rx_err_crc_count),
	TC956X_SW_STAT(rx_err_giant_count),
	TC956X_SW_STAT(rx_err_checksum_count),
	TC956X_SW_STAT(rx_err_overflow_count),
	TC956X_SW_STAT(rx_err_bus_count),
	TC956X_SW_STAT(rx_err_pkt_len_count),
	TC956X_SW_STAT(rx_err_runt_pkt_count),
	TC956X_SW_STAT(rx_err_dribble_count),
	TC956X_SW_STAT(rx_err_t_out_ip_header_count),
	TC956X_SW_STAT(rx_err_t_out_ip_pl_l4_csum_count),
	TC956X_SW_STAT(rx_err_t_in_ip_header_count),
	TC956X_SW_STAT(rx_err_t_in_ip_pl_l4_csum_count),
	TC956X_SW_STAT(rx_err_t_invalid_vlan_header),
	TC956X_SW_STAT(rx_l2_len_pkt_count),
	TC956X_SW_STAT(rx_l2_mac_control_pkt_count),
	TC956X_SW_STAT(rx_l2_dcb_control_pkt_count),
	TC956X_SW_STAT(rx_l2_arp_pkt_count),
	TC956X_SW_STAT(rx_l2_oam_type_pkt_count),
	TC956X_SW_STAT(rx_l2_untg_typ_match_pkt_count),
	TC956X_SW_STAT(rx_l2_other_type_pkt_count),
	TC956X_SW_STAT(rx_l2_single_svlan_pkt_count),
	TC956X_SW_STAT(rx_l2_single_cvlan_pkt_count),
	TC956X_SW_STAT(rx_l2_d_cvlan_cvlan_pkt_count),
	TC956X_SW_STAT(rx_l2_d_svlan_svlan_pkt_count),
	TC956X_SW_STAT(rx_l2_d_svlan_cvlan_pkt_count),
	TC956X_SW_STAT(rx_l2_d_cvlan_svlan_pkt_count),
	TC956X_SW_STAT(rx_l2_untg_av_control_pkt_count),
	TC956X_SW_STAT(rx_ptp_no_msg),
	TC956X_SW_STAT(rx_ptp_msg_type_sync),
	TC956X_SW_STAT(rx_ptp_msg_type_follow_up),
	TC956X_SW_STAT(rx_ptp_msg_type_delay_req),
	TC956X_SW_STAT(rx_ptp_msg_type_delay_resp),
	TC956X_SW_STAT(rx_ptp_msg_type_pdelay_req),
	TC956X_SW_STAT(rx_ptp_msg_type_pdelay_resp),
	TC956X_SW_STAT(rx_ptp_msg_type_pdelay_follow_up),
	TC956X_SW_STAT(rx_ptp_msg_type_announce),
	TC956X_SW_STAT(rx_ptp_msg_type_management),
	TC956X_SW_STAT(rx_ptp_msg_pkt_signaling),
	TC956X_SW_STAT(rx_ptp_msg_pkt_reserved_type),
};
#define TC956X_SW_STATS_LEN ARRAY_SIZE(tc956x_sw)
#endif
static const char tc956x_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define TC956XMAC_TX_FCS	BIT(0)
"tx-fcs",
};

#define TC956X_PRIV_FLAGS_STR_LEN ARRAY_SIZE(tc956x_priv_flags_strings)

static void tc956xmac_ethtool_getdrvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *info)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct pci_dev *pdev = to_pci_dev(priv->device);
	struct tc956x_version *fw_version;
	int reg = 0;
	char fw_version_str[32];

#ifdef TC956X
	reg = readl(priv->tc956x_SRAM_pci_base_addr + TC956X_M3_DBG_VER_START);
#endif

	fw_version = (struct tc956x_version *)(&reg);
	scnprintf(fw_version_str, sizeof(fw_version_str), "Firmware Version %s_%d.%d-%d", (fw_version->rel_dbg == 'D')?"DBG":"REL",
								fw_version->major, fw_version->minor,
								fw_version->sub_minor);

	strlcpy(info->fw_version, fw_version_str, sizeof(info->fw_version));

	if (priv->plat->has_gmac || priv->plat->has_gmac4)
		strlcpy(info->driver, GMAC_ETHTOOL_NAME, sizeof(info->driver));
	else if (priv->plat->has_xgmac)
		strlcpy(info->driver, XGMAC_ETHTOOL_NAME, sizeof(info->driver));
	else
		strlcpy(info->driver, MAC100_ETHTOOL_NAME,
			sizeof(info->driver));

	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(pdev), sizeof(info->bus_info));

	info->n_priv_flags = TC956X_PRIV_FLAGS_STR_LEN;
}
#ifndef TC956X_SRIOV_VF
static int tc956xmac_ethtool_get_link_ksettings(struct net_device *dev,
					     struct ethtool_link_ksettings *cmd)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (priv->hw->pcs & TC956XMAC_PCS_RGMII ||
	    priv->hw->pcs & TC956XMAC_PCS_SGMII ||
	    priv->hw->pcs & TC956XMAC_PCS_USXGMII) {
		struct rgmii_adv adv;
		u32 supported, advertising, lp_advertising;

		if (!priv->xstats.pcs_link) {
			cmd->base.speed = SPEED_UNKNOWN;
			cmd->base.duplex = DUPLEX_UNKNOWN;
			return 0;
		}
		cmd->base.duplex = priv->xstats.pcs_duplex;

		cmd->base.speed = priv->xstats.pcs_speed;

		/* Get and convert ADV/LP_ADV from the HW AN registers */
		if (tc956xmac_pcs_get_adv_lp(priv, priv->ioaddr, &adv))
			return -EOPNOTSUPP;	/* should never happen indeed */

		/* Encoding of PSE bits is defined in 802.3z, 37.2.1.4 */

		ethtool_convert_link_mode_to_legacy_u32(
			&supported, cmd->link_modes.supported);
		ethtool_convert_link_mode_to_legacy_u32(
			&advertising, cmd->link_modes.advertising);
		ethtool_convert_link_mode_to_legacy_u32(
			&lp_advertising, cmd->link_modes.lp_advertising);

		if (adv.pause & TC956XMAC_PCS_PAUSE)
			advertising |= ADVERTISED_Pause;
		if (adv.pause & TC956XMAC_PCS_ASYM_PAUSE)
			advertising |= ADVERTISED_Asym_Pause;
		if (adv.lp_pause & TC956XMAC_PCS_PAUSE)
			lp_advertising |= ADVERTISED_Pause;
		if (adv.lp_pause & TC956XMAC_PCS_ASYM_PAUSE)
			lp_advertising |= ADVERTISED_Asym_Pause;

		/* Reg49[3] always set because ANE is always supported */
		cmd->base.autoneg = ADVERTISED_Autoneg;
		supported |= SUPPORTED_Autoneg;
		advertising |= ADVERTISED_Autoneg;
		lp_advertising |= ADVERTISED_Autoneg;

		if (adv.duplex) {
			supported |= (SUPPORTED_1000baseT_Full |
				      SUPPORTED_100baseT_Full |
				      SUPPORTED_10baseT_Full);
			advertising |= (ADVERTISED_1000baseT_Full |
					ADVERTISED_100baseT_Full |
					ADVERTISED_10baseT_Full);
		} else {
			supported |= (SUPPORTED_1000baseT_Half |
				      SUPPORTED_100baseT_Half |
				      SUPPORTED_10baseT_Half);
			advertising |= (ADVERTISED_1000baseT_Half |
					ADVERTISED_100baseT_Half |
					ADVERTISED_10baseT_Half);
		}
		if (adv.lp_duplex)
			lp_advertising |= (ADVERTISED_1000baseT_Full |
					   ADVERTISED_100baseT_Full |
					   ADVERTISED_10baseT_Full);
		else
			lp_advertising |= (ADVERTISED_1000baseT_Half |
					   ADVERTISED_100baseT_Half |
					   ADVERTISED_10baseT_Half);
		cmd->base.port = PORT_OTHER;

		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.supported, supported);
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.advertising, advertising);
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.lp_advertising, lp_advertising);

		return 0;
	}

	if (!netif_running(dev))
		return -EBUSY;

	if (dev->phydev == NULL)
		return -ENODEV;

	return phylink_ethtool_ksettings_get(priv->phylink, cmd);
}

static int
tc956xmac_ethtool_set_link_ksettings(struct net_device *dev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (priv->hw->pcs & TC956XMAC_PCS_RGMII ||
	    priv->hw->pcs & TC956XMAC_PCS_SGMII ||
	    priv->hw->pcs & TC956XMAC_PCS_USXGMII) {
		u32 mask = ADVERTISED_Autoneg | ADVERTISED_Pause;

		/* Only support ANE */
		if (cmd->base.autoneg != AUTONEG_ENABLE)
			return -EINVAL;

		mask &= (ADVERTISED_1000baseT_Half |
			ADVERTISED_1000baseT_Full |
			ADVERTISED_100baseT_Half |
			ADVERTISED_100baseT_Full |
			ADVERTISED_10baseT_Half |
			ADVERTISED_10baseT_Full);

		mutex_lock(&priv->lock);
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
		tc956x_xpcs_ctrl_ane(priv, 1);
#else
		tc956xmac_pcs_ctrl_ane(priv, priv->ioaddr, 1, priv->hw->ps, 0);
#endif
#endif /* TC956X_SRIOV_VF */
		mutex_unlock(&priv->lock);

		return 0;
	}
	/* Return if Autonegotiation disabled */
	if (priv->port_num == RM_PF0_ID) {
		if (cmd->base.autoneg != AUTONEG_ENABLE)
			return -EINVAL;
	}

	if (!dev->phydev)
		return -ENODEV;
	return phylink_ethtool_ksettings_set(priv->phylink, cmd);
}
#endif  /* TC956X_SRIOV_VF */
static u32 tc956xmac_ethtool_getmsglevel(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

static void tc956xmac_ethtool_setmsglevel(struct net_device *dev, u32 level)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	priv->msg_enable = level;

}

static int tc956xmac_check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EBUSY;
	return 0;
}

static int tc956xmac_ethtool_get_regs_len(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (priv->plat->has_xgmac)
		return XGMAC_REGSIZE * 4;
	return REG_SPACE_SIZE * sizeof(u32);
}

#ifdef TC956X_SRIOV_PF
#ifdef TC956X_SRIOV_DEBUG
static u32 rxp_read_frp_stat(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					    int pos)
{
	int ret;

	u32 val;

	/* Wait for ready */
	ret = readl_poll_timeout(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST,
			val, !(val & XGMAC_STARTBUSY), 1, 10000);
	if (ret)
		return ret;

	/* Write pos */
	val = pos & XGMAC_ADDR;
	val |= XGMAC_ACCSEL;
	writel(val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

	/* Start Read */
	val |= XGMAC_STARTBUSY;
	writel(val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

	/* Wait for done */
	ret = readl_poll_timeout(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST,
			val, !(val & XGMAC_STARTBUSY), 1, 10000);
	if (ret) {
		netdev_err(priv->dev, "timeout error\n");
		return ret;
	} else
		return readl(ioaddr + XGMAC_MTL_RXP_IACC_DATA);

}

static void tc956x_read_frp_stats(struct tc956xmac_priv *priv)
{
	u32 ch;

	netdev_info(priv->dev, "For MTL_RXP_Drop_Cnt %d", (0x7FFFFFFF & rxp_read_frp_stat(priv, priv->ioaddr, 0)));
	netdev_info(priv->dev, "For MTL_RXP_Error_Cnt %d", (0x7FFFFFFF & rxp_read_frp_stat(priv, priv->ioaddr, 1)));
	netdev_info(priv->dev, "For MTL_RXP_Bypass_Cnt %d", (0x7FFFFFFF & rxp_read_frp_stat(priv, priv->ioaddr, 2)));

	for (ch = 0; ch < TC956XMAC_CH_MAX; ch++) {
		u32 read_pos = (0x40 + (0x10*ch));

		netdev_info(priv->dev, "For DMA_CH%d_RXP_Accept_Cnt %d", ch, (0x7FFFFFFF & rxp_read_frp_stat(priv, priv->ioaddr, read_pos)));
	}
}
#endif /* TC956X_SRIOV_DEBUG */
#endif /* TC956X_SRIOV_PF */

static void tc956xmac_ethtool_gregs(struct net_device *dev,
			  struct ethtool_regs *regs, void *space)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 *reg_space = (u32 *) space;

	tc956xmac_dump_mac_regs(priv, priv->hw, reg_space);
	tc956xmac_dump_dma_regs(priv, priv->ioaddr, reg_space);
#ifdef TC956X_SRIOV_PF
#ifdef TC956X_SRIOV_DEBUG
	tc956x_read_frp_stats(priv);
	tc956x_filter_debug(priv);
#endif
#endif
#ifndef TC956X
	if (!priv->plat->has_xgmac && !priv->plat->has_gmac4) {
		/* Copy DMA registers to where ethtool expects them */
		memcpy(&reg_space[ETHTOOL_DMA_OFFSET],
		       &reg_space[DMA_BUS_MODE / 4],
		       NUM_DWMAC1000_DMA_REGS * 4);
	}
#endif
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
#ifndef TC956X_SRIOV_VF
static int tc956xmac_nway_reset(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	return phylink_ethtool_nway_reset(priv->phylink);
}
#endif  /* TC956X_SRIOV_VF */
#endif

#ifdef TC956X_SRIOV_VF
static void
tc956xmac_get_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);

	tc956xmac_ethtool_get_pauseparam(priv, pause);
}
#endif
#ifndef TC956X_SRIOV_VF
void
tc956xmac_get_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);
	struct rgmii_adv adv_lp;

	if (priv->hw->pcs &&
	    !tc956xmac_pcs_get_adv_lp(priv, priv->ioaddr, &adv_lp)) {
		pause->autoneg = 1;
		if (!adv_lp.pause)
			return;
	} else {
		phylink_ethtool_get_pauseparam(priv->phylink, pause);
		pause->rx_pause = (priv->flow_ctrl & FLOW_RX);
		pause->tx_pause = (priv->flow_ctrl & FLOW_TX);
	}
}
#endif

static int
tc956xmac_set_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);
	int new_pause = FLOW_OFF;
	struct rgmii_adv adv_lp;
	u32 tx_cnt = priv->plat->tx_queues_to_use;
	struct phy_device *phy = netdev->phydev;

	if ((priv->plat->port_interface == ENABLE_XFI_INTERFACE) && (priv->speed != SPEED_10000) && (!(pause->rx_pause))) {
		KPRINT_ERR("RX Flow ctrl shouldn't be disabled for 10G lower speed in XFI Interface\n");
		return -EOPNOTSUPP;
	}

	if (priv->hw->pcs &&
	    !tc956xmac_pcs_get_adv_lp(priv, priv->ioaddr, &adv_lp)) {
		pause->autoneg = 1;
		if (!adv_lp.pause)
			return -EOPNOTSUPP;
		return 0;
	} else {
#ifndef TC956X_SRIOV_VF
		phylink_ethtool_set_pauseparam(priv->phylink, pause);
#endif  /* TC956X_SRIOV_VF */
	}
	if (pause->rx_pause)
		new_pause |= FLOW_RX;
	if (pause->tx_pause)
		new_pause |= FLOW_TX;
	priv->flow_ctrl = new_pause;

	tc956xmac_flow_ctrl(priv, priv->hw, phy->duplex, priv->flow_ctrl,
				 priv->pause, tx_cnt);
	return 0;
}

#ifndef TC956X_SRIOV_VF
static void tc956xmac_m3fw_stats_read(struct tc956xmac_priv *priv)
{
	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 chno, reg_val = 0;

	for (chno = 0; chno < tx_queues_count; chno++) {
		/* Tx Underflow count may not match with actual value, as it is 11bit value
		accumulation happening only when reading ethool statistics, not after overflow of counter*/
		priv->xstats.mtl_tx_underflow[chno] +=
			readl(priv->ioaddr + XGMAC_MTL_TXQ_UFPKT_CNT(chno));
	}
	for (chno = 0; chno < rx_queues_count; chno++) {
		/* Rx overflow/missed pkt count may not match with actual values, as these are 11bit values
		accumulation happening only when reading ethool statistics, not after overflow of counters*/
		reg_val = readl(priv->ioaddr + XGMAC_MTL_RXQ_MISS_PKT_OF_CNT_OFFSET(chno));

		priv->xstats.mtl_rx_miss_pkt_cnt[chno] += ((reg_val & XGMAC_MISPKTCNT_MASK) >>
								XGMAC_MISPKTCNT_SHIFT);

		priv->xstats.mtl_rx_overflow_pkt_cnt[chno] += (reg_val & XGMAC_OVFPKTCNT_MASK);

		priv->xstats.rxch_watchdog_timer[chno] =
			readl(priv->ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(chno));
	}
	/* Reading M3 Debug Counters*/
	priv->xstats.m3_debug_cnt0 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT0)));
	priv->xstats.m3_debug_cnt1 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT1)));
	priv->xstats.m3_debug_cnt2 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT2)));
	priv->xstats.m3_debug_cnt3 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT3)));
	priv->xstats.m3_debug_cnt4 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT4)));
	priv->xstats.m3_debug_cnt5 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT5)));
	priv->xstats.m3_debug_cnt6 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT6)));
	priv->xstats.m3_debug_cnt7 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT7)));
	priv->xstats.m3_debug_cnt8 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT8)));
	priv->xstats.m3_debug_cnt9 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT9)));
	priv->xstats.m3_debug_cnt10 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT10)));
	priv->xstats.m3_watchdog_exp_cnt = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT11)));
	priv->xstats.m3_watchdog_monitor_cnt = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT12)));
	priv->xstats.m3_debug_cnt13 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT13)));
	priv->xstats.m3_debug_cnt14 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT14)));
	priv->xstats.m3_systick_cnt_upper_value = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT16)));
	priv->xstats.m3_systick_cnt_lower_value = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT15)));
	priv->xstats.m3_tx_timeout_port0 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT17)));
	priv->xstats.m3_tx_timeout_port1 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT18)));
	priv->xstats.m3_debug_cnt19 = readl(priv->tc956x_SRAM_pci_base_addr +
				(TC956X_M3_SRAM_DEBUG_CNTS_OFFSET + (DB_CNT_LEN * DB_CNT19)));
	for (chno = 0; chno < tx_queues_count; chno++) {
		priv->xstats.m3_tx_pcie_addr_loc_port0[chno] = readl(priv->tc956x_SRAM_pci_base_addr +
				(SRAM_TX_PCIE_ADDR_LOC + (chno * 4)));
	}
	for (chno = 0; chno < tx_queues_count; chno++) {
		priv->xstats.m3_tx_pcie_addr_loc_port1[chno] = readl(priv->tc956x_SRAM_pci_base_addr +
				(SRAM_TX_PCIE_ADDR_LOC + (TC956XMAC_CH_MAX * 4) + (chno * 4)));
	}
	for (chno = 0; chno < rx_queues_count; chno++) {
		priv->xstats.m3_rx_pcie_addr_loc_port0[chno] = readl(priv->tc956x_SRAM_pci_base_addr +
				(SRAM_RX_PCIE_ADDR_LOC + (chno * 4)));
	}
	for (chno = 0; chno < rx_queues_count; chno++) {
		priv->xstats.m3_rx_pcie_addr_loc_port1[chno] = readl(priv->tc956x_SRAM_pci_base_addr +
				(SRAM_RX_PCIE_ADDR_LOC + (TC956XMAC_CH_MAX * 4) + (chno * 4)));
	}

}
#endif

static void tc956xmac_get_ethtool_stats(struct net_device *dev,
				 struct ethtool_stats *dummy, u64 *data)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	u32 rx_queues_count = priv->plat->rx_queues_to_use;
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
#ifndef TC956X_SRIOV_VF
	unsigned long count;
#endif
	int i, j = 0;
#ifndef TC956X_SRIOV_VF
	int ret;
#endif

#ifdef TC956X_SRIOV_VF

	/* Copy only the SW stats for VF driver */
	for (i = 0; i < TC956X_SW_STATS_LEN; i++) {
		char *p;

		p = (char *)priv + tc956x_sw[i].stat_offset;

		data[j++] = (tc956x_sw[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) :
			     (*(u32 *)p);
	}

	if (priv->synopsys_id >= DWMAC_CORE_3_50 ||
			priv->synopsys_id == DWXGMAC_CORE_3_01) {
		tc956xmac_mac_debug(priv, priv->ioaddr,
				(void *)&priv->xstats,
				rx_queues_count, tx_queues_count);

		tc956xmac_dma_desc_stats(priv, priv->ioaddr);
	}

	for (i = 0; i < TC956XMAC_STATS_LEN; i++) {
		char *p = (char *)priv + tc956xmac_gstrings_stats[i].stat_offset;

		data[j++] = (tc956xmac_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) : (*(u32 *)p);
	}
#else
	if (priv->dma_cap.asp) {
		for (i = 0; i < TC956XMAC_SAFETY_FEAT_SIZE; i++) {
			if (!tc956xmac_safety_feat_dump(priv, &priv->sstats, i,
						&count, NULL))
				data[j++] = count;
		}
	}

	/* Update the DMA HW counters for dwmac10/100 */
	ret = tc956xmac_dma_diagnostic_fr(priv, &dev->stats,
					(void *)&priv->xstats, priv->ioaddr);
	if (ret) {
		/* If supported, for new GMAC chips expose the MMC counters */
		if (priv->dma_cap.rmon) {
			tc956xmac_mmc_read(priv, priv->mmcaddr, &priv->mmc);

			for (i = 0; i < TC956XMAC_MMC_STATS_LEN; i++) {
				char *p;

				p = (char *)priv + tc956xmac_mmc[i].stat_offset;

				data[j++] = (tc956xmac_mmc[i].sizeof_stat ==
					     sizeof(u64)) ? (*(u64 *)p) :
					     (*(u32 *)p);
			}
		}
#ifndef TC956X_SRIOV_VF
		if (priv->eee_enabled) {
			int val = phylink_get_eee_err(priv->phylink);

			if (val)
				priv->xstats.phy_eee_wakeup_error_n = val;
		}
#endif  /* TC956X_SRIOV_VF */
		if (priv->synopsys_id >= DWMAC_CORE_3_50 ||
			priv->synopsys_id == DWXGMAC_CORE_3_01) {
			tc956xmac_mac_debug(priv, priv->ioaddr,
					(void *)&priv->xstats,
					rx_queues_count, tx_queues_count);

			tc956xmac_dma_desc_stats(priv, priv->ioaddr);
		}
	}
	tc956xmac_m3fw_stats_read(priv);
	for (i = 0; i < TC956XMAC_STATS_LEN; i++) {
		char *p = (char *)priv + tc956xmac_gstrings_stats[i].stat_offset;

		data[j++] = (tc956xmac_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *)p) : (*(u32 *)p);
	}
#endif
}

static int tc956xmac_get_sset_count(struct net_device *netdev, int sset)
{
	struct tc956xmac_priv *priv = netdev_priv(netdev);
#ifdef TC956X_SRIOV_VF
	int len;
#else
	int i, len, safety_len = 0;
#endif

	switch (sset) {
	case ETH_SS_STATS:
#ifdef TC956X_SRIOV_VF
		len = TC956X_SW_STATS_LEN + TC956XMAC_STATS_LEN;
#else
		len = TC956XMAC_STATS_LEN;

		if (priv->dma_cap.rmon)
			len += TC956XMAC_MMC_STATS_LEN;
		if (priv->dma_cap.asp) {
			for (i = 0; i < TC956XMAC_SAFETY_FEAT_SIZE; i++) {
				if (!tc956xmac_safety_feat_dump(priv,
							&priv->sstats, i,
							NULL, NULL))
					safety_len++;
			}

			len += safety_len;
		}
#endif
		return len;
	case ETH_SS_TEST:
		return tc956xmac_selftest_get_count(priv);
	case ETH_SS_PRIV_FLAGS:
		return TC956X_PRIV_FLAGS_STR_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void tc956xmac_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;
	u8 *p = data;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_STATS:
#ifdef TC956X_SRIOV_VF
		for (i = 0; i < TC956X_SW_STATS_LEN; i++) {
			memcpy(p, tc956x_sw[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
#else
		if (priv->dma_cap.asp) {
			for (i = 0; i < TC956XMAC_SAFETY_FEAT_SIZE; i++) {
				const char *desc;

				if (!tc956xmac_safety_feat_dump(priv,
							&priv->sstats, i,
							NULL, &desc)) {
					memcpy(p, desc, ETH_GSTRING_LEN);
					p += ETH_GSTRING_LEN;
				}
			}
		}
		if (priv->dma_cap.rmon)
			for (i = 0; i < TC956XMAC_MMC_STATS_LEN; i++) {
				memcpy(p, tc956xmac_mmc[i].stat_string,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
#endif
		for (i = 0; i < TC956XMAC_STATS_LEN; i++) {
			memcpy(p, tc956xmac_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		break;
	case ETH_SS_TEST:
		tc956xmac_selftest_get_strings(priv, p);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, tc956x_priv_flags_strings,
				TC956X_PRIV_FLAGS_STR_LEN * ETH_GSTRING_LEN);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

#ifndef TC956X_SRIOV_VF

static void tc956xmac_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (device_can_wakeup(priv->device))
		phylink_ethtool_get_wol(priv->phylink, wol);
}

static int tc956xmac_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 support = (WAKE_MAGIC | WAKE_PHY);
	int ret;

	if (!device_can_wakeup(priv->device))
		return -EINVAL;

	if (wol->wolopts & ~support)
		return -EINVAL;

	ret = phylink_ethtool_set_wol(priv->phylink, wol);
	if (!ret)
		device_set_wakeup_enable(priv->device, wol->wolopts);
	else
		return ret;

	mutex_lock(&priv->lock);
	priv->wolopts = wol->wolopts;
	mutex_unlock(&priv->lock);

	return ret;
}

#ifdef DEBUG_EEE
int phy_ethtool_get_eee_local(struct phy_device *phydev, struct ethtool_eee *data)
{
	int val;

	if (!phydev->drv)
		return -EIO;

	/* Get Supported EEE */
	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
	KPRINT_INFO("%s --- cap: 0x%x\n", __func__, val);
	if (val < 0)
		return val;
	data->supported = mmd_eee_cap_to_ethtool_sup_t(val);

	/* Get advertisement EEE */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
	if (val < 0)
		return val;

	KPRINT_INFO("%s --- adv: 0x%x\n", __func__, val);

	data->advertised = mmd_eee_adv_to_ethtool_adv_t(val);
	data->eee_enabled = !!data->advertised;

	/* Get LP advertisement EEE */
	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
	if (val < 0)
		return val;
	KPRINT_INFO("%s --- lp_adv: 0x%x\n", __func__, val);

	data->lp_advertised = mmd_eee_adv_to_ethtool_adv_t(val);

	KPRINT_INFO("%s --- data->advertised: 0x%x\n", __func__, data->advertised);
	KPRINT_INFO("%s --- data->lp_advertised: 0x%x\n", __func__, data->lp_advertised);

	data->eee_active = !!(data->advertised & data->lp_advertised);


	KPRINT_INFO("%s --- data->eee_enabled: 0x%x\n", __func__, data->eee_enabled);
	KPRINT_INFO("%s --- data->eee_active: 0x%x\n", __func__, data->eee_active);

	return 0;
}
int phy_ethtool_set_eee_local(struct phy_device *phydev, struct ethtool_eee *data)
{
	int cap, old_adv, adv = 0, ret;
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
	int cap2p5, old_adv_2p5, adv_2p5 = 0;
#endif
	if (!phydev->drv)
		return -EIO;

	/* Get Supported EEE */
	cap = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
	KPRINT_INFO("%s --- cap: 0x%x\n", __func__, cap);
	if (cap < 0)
		return cap;


	old_adv = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
	KPRINT_INFO("%s --- old_adv:0x%x\n", __func__, old_adv);
	if (old_adv < 0)
		return old_adv;


	if (data->eee_enabled) {
		adv = !data->advertised ? cap :
		      ethtool_adv_to_mmd_eee_adv_t(data->advertised) & cap;
		/* Mask prohibited EEE modes */
		adv &= ~phydev->eee_broken_modes;
	}
	KPRINT_INFO("%s --- adv:0x%x\n", __func__, adv);

	if (old_adv != adv) {
		ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, adv);
		if (ret < 0)
			return ret;

		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
		KPRINT_INFO("%s --- readback adv:0x%x\n", __func__, ret);
		if (ret < 0)
			return ret;


		/* Restart autonegotiation so the new modes get sent to the
		 * link partner.
		 */
		if (phydev->autoneg == AUTONEG_ENABLE) {
			ret = phy_restart_aneg(phydev);
			if (ret < 0)
				return ret;
		}
	}
#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT
	cap2p5 = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE2);
	KPRINT_INFO("%s --- cap2p5: 0x%x\n", __func__, cap2p5);
	if (cap < 0)
		return cap;

	old_adv_2p5 = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2);
	KPRINT_INFO("%s --- old_adv_2p5:0x%x\n", __func__, old_adv_2p5);
	if (old_adv_2p5 < 0)
		return old_adv_2p5;

	if (data->eee_enabled) {
		adv_2p5 = !data->advertised ? cap2p5 :
		      ethtool_adv_to_mmd_eee_adv_t(data->advertised) & cap2p5;
		/* Mask prohibited EEE modes */
		adv_2p5 &= ~phydev->eee_broken_modes;
	}
	KPRINT_INFO("%s --- adv_2p5:0x%x\n", __func__, adv_2p5);

	if (old_adv_2p5 != adv_2p5) {
		ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2, adv_2p5);
		if (ret < 0)
			return ret;

		/* Restart autonegotiation so the new modes get sent to the
		 * link partner.
		 */
		if (phydev->autoneg == AUTONEG_ENABLE) {
			ret = phy_restart_aneg(phydev);
			if (ret < 0)
				return ret;
		}
	}
#endif
	return 0;
}

#endif

#ifdef TC956X_5_G_2_5_G_EEE_SUPPORT

static inline u16 tc956x_ethtool_adv_to_mmd_eee_adv2_t(u32 adv)
{
	u16 reg = 0;

	if (adv & TC956X_ADVERTISED_2500baseT_Full)
		reg |= MDIO_EEE_2_5GT;
	if (adv & TC956X_ADVERTISED_5000baseT_Full)
		reg |= MDIO_EEE_5GT;

	return reg;
}

int phy_ethtool_set_eee_2p5(struct phy_device *phydev, struct ethtool_eee *data)
{
	int ret;
	int cap2p5, old_adv_2p5, adv_2p5 = 0;

	if (!phydev->drv)
		return -EIO;

	cap2p5 = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE2);
	KPRINT_INFO("%s --- cap2p5: 0x%x\n", __func__, cap2p5);
	if (cap2p5 < 0)
		return cap2p5;

	old_adv_2p5 = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2);
	KPRINT_INFO("%s --- old_adv_2p5:0x%x\n", __func__, old_adv_2p5);
	if (old_adv_2p5 < 0)
		return old_adv_2p5;
	/* EEE advertise checking API corrected for 2.5G and 5G speeds. */
	if (data->eee_enabled) {
		adv_2p5 = !data->advertised ? cap2p5 :
		      tc956x_ethtool_adv_to_mmd_eee_adv2_t(data->advertised) & cap2p5;
		/* Mask prohibited EEE modes */
		adv_2p5 &= ~phydev->eee_broken_modes;
	}
	KPRINT_INFO("%s --- adv_2p5:0x%x\n", __func__, adv_2p5);

	if (old_adv_2p5 != adv_2p5) {
		ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV2, adv_2p5);
		if (ret < 0)
			return ret;

		/* Restart autonegotiation so the new modes get sent to the
		 * link partner.
		 */
		if (phydev->autoneg == AUTONEG_ENABLE) {
			ret = phy_restart_aneg(phydev);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}
#endif

int tc956xmac_ethtool_op_get_eee(struct net_device *dev,
				     struct ethtool_eee *edata)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret;

	if (!priv->dma_cap.eee)
		return -EOPNOTSUPP;

	edata->eee_enabled = priv->eee_enabled;
	edata->eee_active = priv->eee_active;
	edata->tx_lpi_timer = priv->tx_lpi_timer;

	DBGPR_FUNC(priv->device, "1--> %s edata->eee_active: %d\n", __func__, edata->eee_active);
#ifndef DEBUG_EEE
	ret = phylink_ethtool_get_eee(priv->phylink, edata);
#else
	ret = phy_ethtool_get_eee_local(priv->dev->phydev, edata);
#endif

	edata->eee_enabled = priv->eee_enabled;
	edata->eee_active = priv->eee_active;
	edata->tx_lpi_timer = priv->tx_lpi_timer;
	edata->tx_lpi_enabled = edata->eee_enabled;

	DBGPR_FUNC(priv->device, "2--> %s edata->eee_active: %d\n", __func__, edata->eee_active);

	return ret;
}
#endif
#ifdef TC956X_SRIOV_VF
static int tc956xmac_ethtool_op_get_eee(struct net_device *dev,
				     struct ethtool_eee *edata)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956xmac_ethtool_get_eee(priv, edata);

	return 0;
}
#endif
static int tc956xmac_ethtool_op_set_eee(struct net_device *dev,
				     struct ethtool_eee *edata)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
#ifndef TC956X_SRIOV_VF
	int ret;
#endif
	if (!edata->eee_enabled) {
		DBGPR_FUNC(priv->device, "%s Disable EEE\n", __func__);
		tc956xmac_disable_eee_mode(priv);
	} else {
		DBGPR_FUNC(priv->device, "%s Enable EEE\n", __func__);
		/* We are asking for enabling the EEE but it is safe
		 * to verify all by invoking the eee_init function.
		 * In case of failure it will return an error.
		 */
		if (priv->tx_lpi_timer != edata->tx_lpi_timer) {
			if (edata->tx_lpi_timer > TC956X_MAX_LPI_AUTO_ENTRY_TIMER) {
				DBGPR_FUNC(priv->device, "%s Error : Maximum LPI Auto Entry Time Supported %d\n",
					__func__, TC956X_MAX_LPI_AUTO_ENTRY_TIMER);
				return -EINVAL;
			}
			priv->tx_lpi_timer = edata->tx_lpi_timer;
		}

		edata->eee_enabled = tc956xmac_eee_init(priv);
		if (!edata->eee_enabled)
			return -EOPNOTSUPP;
	}
#ifndef TC956X_SRIOV_VF
#ifndef DEBUG_EEE
	ret = phylink_ethtool_set_eee(priv->phylink, edata);

	ret |= phy_ethtool_set_eee_2p5(priv->dev->phydev, edata);
#else
	ret = phy_ethtool_set_eee_local(priv->dev->phydev, edata);
#endif
	if (ret)
		return ret;
#endif  /* TC956X_SRIOV_VF */
	priv->eee_enabled = edata->eee_enabled;
	priv->tx_lpi_timer = edata->tx_lpi_timer;

	DBGPR_FUNC(priv->device, "1--> %s priv->eee_enabled: %d\n", __func__, priv->eee_enabled);

	return 0;
}

static u32 tc956xmac_usec2riwt(u32 usec, struct tc956xmac_priv *priv)
{
	unsigned long clk = clk_get_rate(priv->plat->tc956xmac_clk);
	u32 value, mult = 256;

	if (!clk) {
		clk = TC956X_PTP_SYSCLOCK;
		if (!clk)
			return 0;
	}

	for (mult = 256; mult <= 2048; mult *= 2) {
		value = (usec * (clk / 1000000)) / mult;
		if (value <= 0xff)
			break;
	}

	return value;
}

static u32 tc956xmac_riwt2usec(u32 riwt, struct tc956xmac_priv *priv)
{
	unsigned long clk = clk_get_rate(priv->plat->tc956xmac_clk);
	u32 mult = 256;

	if (!clk) {
		clk = TC956X_PTP_SYSCLOCK;
		if (!clk)
			return 0;
	}

	if (riwt > (1024 * 0xff))
		mult = 2048;
	else if (riwt > (512 * 0xff))
		mult = 1024;
	else if (riwt > (256 * 0xff))
		mult = 512;
	else
		mult = 256;

	return (riwt * mult) / (clk / 1000000);
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
static int __tc956xmac_get_coalesce(struct net_device *dev,
				 struct ethtool_coalesce *ec,
				 int queue)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 max_cnt;
	u32 rx_cnt;
	u32 tx_cnt;

	rx_cnt = priv->plat->rx_queues_to_use;
	tx_cnt = priv->plat->tx_queues_to_use;
	max_cnt = max(rx_cnt, tx_cnt);

	if (queue < 0)
		queue = 0;
	else if (queue >= max_cnt)
		return -EINVAL;

	if (queue < tx_cnt) {
		ec->tx_coalesce_usecs = priv->tx_coal_timer[queue];
		ec->tx_max_coalesced_frames = priv->tx_coal_frames[queue];
	} else {
		ec->tx_coalesce_usecs = 0;
		ec->tx_max_coalesced_frames = 0;
	}

	if (priv->use_riwt && queue < rx_cnt) {
		ec->rx_max_coalesced_frames = priv->rx_coal_frames[queue];
		ec->rx_coalesce_usecs = tc956xmac_riwt2usec(priv->rx_riwt[queue],
							 priv);
	} else {
		ec->rx_max_coalesced_frames = 0;
		ec->rx_coalesce_usecs = 0;
	}

	return 0;
}

static int tc956xmac_get_coalesce(struct net_device *dev,
			       struct ethtool_coalesce *ec,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack)
{
	return __tc956xmac_get_coalesce(dev, ec, -1);
}

static int __tc956xmac_set_coalesce(struct net_device *dev,
				 struct ethtool_coalesce *ec,
				 int queue)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	bool all_queues = false;
	unsigned int rx_riwt;
	u32 max_cnt;
	u32 rx_cnt;
	u32 tx_cnt;

	rx_cnt = priv->plat->rx_queues_to_use;
	tx_cnt = priv->plat->tx_queues_to_use;
	max_cnt = max(rx_cnt, tx_cnt);

	if (queue < 0)
		all_queues = true;
	else if (queue >= max_cnt)
		return -EINVAL;

	if (priv->use_riwt && (ec->rx_coalesce_usecs > 0)) {
		rx_riwt = tc956xmac_usec2riwt(ec->rx_coalesce_usecs, priv);

		if ((rx_riwt > MAX_DMA_RIWT) || (rx_riwt < MIN_DMA_RIWT))
			return -EINVAL;

		if (all_queues) {
			int i;

			for (i = 0; i < rx_cnt; i++) {
				priv->rx_riwt[i] = rx_riwt;
				tc956xmac_rx_watchdog(priv, priv->ioaddr,
						   rx_riwt, i);
				priv->rx_coal_frames[i] =
					ec->rx_max_coalesced_frames;
			}
		} else if (queue < rx_cnt) {
			priv->rx_riwt[queue] = rx_riwt;
			tc956xmac_rx_watchdog(priv, priv->ioaddr,
					   rx_riwt, queue);
			priv->rx_coal_frames[queue] =
				ec->rx_max_coalesced_frames;
		}
	}

	if ((ec->tx_coalesce_usecs == 0) &&
	    (ec->tx_max_coalesced_frames == 0))
		return -EINVAL;

	if ((ec->tx_coalesce_usecs > TC956XMAC_MAX_COAL_TX_TICK) ||
	    (ec->tx_max_coalesced_frames > TC956XMAC_TX_MAX_FRAMES))
		return -EINVAL;

	if (all_queues) {
		int i;

		for (i = 0; i < tx_cnt; i++) {
			priv->tx_coal_frames[i] =
				ec->tx_max_coalesced_frames;
			priv->tx_coal_timer[i] =
				ec->tx_coalesce_usecs;
		}
	} else if (queue < tx_cnt) {
		priv->tx_coal_frames[queue] =
			ec->tx_max_coalesced_frames;
		priv->tx_coal_timer[queue] =
			ec->tx_coalesce_usecs;
	}

	return 0;
}

static int tc956xmac_set_coalesce(struct net_device *dev,
			       struct ethtool_coalesce *ec,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack)
{
	return __tc956xmac_set_coalesce(dev, ec, -1);
}
#else
static int tc956xmac_get_coalesce(struct net_device *dev,
			       struct ethtool_coalesce *ec)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	ec->tx_coalesce_usecs = priv->tx_coal_timer;
	ec->tx_max_coalesced_frames = priv->tx_coal_frames;

	if (priv->use_riwt) {
		ec->rx_max_coalesced_frames = priv->rx_coal_frames;
		ec->rx_coalesce_usecs = tc956xmac_riwt2usec(priv->rx_riwt, priv);
	}

	return 0;
}

static int tc956xmac_set_coalesce(struct net_device *dev,
			       struct ethtool_coalesce *ec)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 rx_cnt = priv->plat->rx_queues_to_use;
	unsigned int rx_riwt;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0))
	/* Check not supported parameters  */
	if ((ec->rx_coalesce_usecs_irq) ||
	    (ec->rx_max_coalesced_frames_irq) || (ec->tx_coalesce_usecs_irq) ||
	    (ec->use_adaptive_rx_coalesce) || (ec->use_adaptive_tx_coalesce) ||
	    (ec->pkt_rate_low) || (ec->rx_coalesce_usecs_low) ||
	    (ec->rx_max_coalesced_frames_low) || (ec->tx_coalesce_usecs_high) ||
	    (ec->tx_max_coalesced_frames_low) || (ec->pkt_rate_high) ||
	    (ec->tx_coalesce_usecs_low) || (ec->rx_coalesce_usecs_high) ||
	    (ec->rx_max_coalesced_frames_high) ||
	    (ec->tx_max_coalesced_frames_irq) ||
	    (ec->stats_block_coalesce_usecs) ||
	    (ec->tx_max_coalesced_frames_high) || (ec->rate_sample_interval))
		return -EOPNOTSUPP;
#endif

	if (priv->use_riwt && (ec->rx_coalesce_usecs > 0)) {
		rx_riwt = tc956xmac_usec2riwt(ec->rx_coalesce_usecs, priv);

		if ((rx_riwt > MAX_DMA_RIWT) || (rx_riwt < MIN_DMA_RIWT)) {
			KPRINT_DEBUG1("Invalid rx_usecs value 0x%X\n", ec->rx_coalesce_usecs);
			return -EINVAL;
		}

		priv->rx_riwt = rx_riwt;
		tc956xmac_rx_watchdog(priv, priv->ioaddr, priv->rx_riwt, rx_cnt);
	}

	if (ec->rx_max_coalesced_frames > TC956XMAC_RX_MAX_FRAMES) {
		KPRINT_DEBUG1("Invalid rx_frames value 0x%X\n", ec->rx_max_coalesced_frames);
		return -EINVAL;
	}

	if ((ec->tx_coalesce_usecs == 0) &&
	    (ec->tx_max_coalesced_frames == 0))
		return -EINVAL;

	if ((ec->tx_coalesce_usecs > TC956XMAC_MAX_COAL_TX_TICK) ||
	    (ec->tx_max_coalesced_frames > TC956XMAC_TX_MAX_FRAMES))
		return -EINVAL;

	/* Only copy relevant parameters, ignore all others. */
	priv->tx_coal_frames = ec->tx_max_coalesced_frames;
	priv->tx_coal_timer = ec->tx_coalesce_usecs;
	priv->rx_coal_frames = ec->rx_max_coalesced_frames;
	return 0;
}
#endif
#ifndef TC956X
static int tc956xmac_get_rxnfc(struct net_device *dev,
			    struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXRINGS:
		rxnfc->data = priv->plat->rx_queues_to_use;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static u32 tc956xmac_get_rxfh_key_size(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	return sizeof(priv->rss.key);
}

static u32 tc956xmac_get_rxfh_indir_size(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	return ARRAY_SIZE(priv->rss.table);
}

static int tc956xmac_get_rxfh(struct net_device *dev, u32 *indir, u8 *key,
			   u8 *hfunc)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int i;

	if (indir) {
		for (i = 0; i < ARRAY_SIZE(priv->rss.table); i++)
			indir[i] = priv->rss.table[i];
	}

	if (key)
		memcpy(key, priv->rss.key, sizeof(priv->rss.key));
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int tc956xmac_set_rxfh(struct net_device *dev, const u32 *indir,
			   const u8 *key, const u8 hfunc)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int i;

	if ((hfunc != ETH_RSS_HASH_NO_CHANGE) && (hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;

	if (indir) {
		for (i = 0; i < ARRAY_SIZE(priv->rss.table); i++)
			priv->rss.table[i] = indir[i];
	}

	if (key)
		memcpy(priv->rss.key, key, sizeof(priv->rss.key));

	return tc956xmac_rss_configure(priv, priv->hw, &priv->rss,
				    priv->plat->rx_queues_to_use);
}
#endif

static int tc956xmac_get_ts_info(struct net_device *dev,
			      struct ethtool_ts_info *info)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if ((priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp)) {
#ifdef TC956X_SRIOV_PF
		info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_TX_HARDWARE |
					SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_SOFTWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE;
#else
		info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_SOFTWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE;
#endif

		if (priv->ptp_clock)
			info->phc_index = ptp_clock_index(priv->ptp_clock);
#ifdef TC956X_SRIOV_PF
		info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
#else
		info->tx_types = (1 << HWTSTAMP_TX_OFF);
#endif

#ifdef TC956X_SRIOV_PF
		info->rx_filters = ((1 << HWTSTAMP_FILTER_NONE) |
				    (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
				    (1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
				    (1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
				    (1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
				    (1 << HWTSTAMP_FILTER_ALL));
#else
		info->rx_filters = (1 << HWTSTAMP_FILTER_ALL);
#endif

		return 0;
	} else
		return ethtool_op_get_ts_info(dev, info);
}

#ifndef TC956X
static int tc956xmac_get_tunable(struct net_device *dev,
			      const struct ethtool_tunable *tuna, void *data)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = priv->rx_copybreak;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int tc956xmac_set_tunable(struct net_device *dev,
			      const struct ethtool_tunable *tuna,
			      const void *data)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		priv->rx_copybreak = *(u32 *)data;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

#ifdef TC956X
static int tc956x_set_priv_flag(struct net_device *dev, u32 priv_flag)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	if (priv_flag & TC956XMAC_TX_FCS)
		priv->tx_crc_pad_state = TC956X_TX_CRC_PAD_INSERT;
	else
		priv->tx_crc_pad_state = TC956X_TX_CRC_PAD_DISABLE;
	KPRINT_INFO("tx_crc_pad_state : %x", priv->tx_crc_pad_state);

	return 0;
}

static u32 tc956x_get_priv_flag(struct net_device *dev)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	u32 ret;

	if (priv->tx_crc_pad_state == TC956X_TX_CRC_PAD_INSERT)
		ret = 1;
	else
		ret = 0;
	KPRINT_INFO("tx_crc_pad_state : %x", priv->tx_crc_pad_state);
	return ret;
}
#endif

static const struct ethtool_ops tc956xmac_ethtool_ops = {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
	    ETHTOOL_COALESCE_MAX_FRAMES,
#endif
	.begin = tc956xmac_check_if_running,
	.get_drvinfo = tc956xmac_ethtool_getdrvinfo,
	.get_msglevel = tc956xmac_ethtool_getmsglevel,
	.set_msglevel = tc956xmac_ethtool_setmsglevel,
	.get_regs = tc956xmac_ethtool_gregs,
	.get_regs_len = tc956xmac_ethtool_get_regs_len,
	.get_link = ethtool_op_get_link,
#ifndef TC956X_SRIOV_VF
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.nway_reset = tc956xmac_nway_reset,
#endif
#endif  /* TC956X_SRIOV_VF */
	.get_pauseparam = tc956xmac_get_pauseparam,
	.set_pauseparam = tc956xmac_set_pauseparam,
	.self_test = tc956xmac_selftest_run,
	.get_ethtool_stats = tc956xmac_get_ethtool_stats,
	.get_strings = tc956xmac_get_strings,
#ifndef TC956X_SRIOV_VF
	.get_wol = tc956xmac_get_wol,
	.set_wol = tc956xmac_set_wol,
#endif
	.get_eee = tc956xmac_ethtool_op_get_eee,
	.set_eee = tc956xmac_ethtool_op_set_eee,
	.get_sset_count	= tc956xmac_get_sset_count,
#ifndef TC956X
	.get_rxnfc = tc956xmac_get_rxnfc,
	.get_rxfh_key_size = tc956xmac_get_rxfh_key_size,
	.get_rxfh_indir_size = tc956xmac_get_rxfh_indir_size,
	.get_rxfh = tc956xmac_get_rxfh,
	.set_rxfh = tc956xmac_set_rxfh,
#endif
	.get_ts_info = tc956xmac_get_ts_info,
	.get_coalesce = tc956xmac_get_coalesce,
	.set_coalesce = tc956xmac_set_coalesce,
#ifndef TC956X
	.get_tunable = tc956xmac_get_tunable,
	.set_tunable = tc956xmac_set_tunable,
#endif
#ifndef TC956X_SRIOV_VF
	.get_link_ksettings = tc956xmac_ethtool_get_link_ksettings,
	.set_link_ksettings = tc956xmac_ethtool_set_link_ksettings,
#endif  /* TC956X_SRIOV_VF */
#ifdef TC956X
	.set_priv_flags = tc956x_set_priv_flag,
	.get_priv_flags = tc956x_get_priv_flag,
#endif
};

void tc956xmac_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &tc956xmac_ethtool_ops;
}
