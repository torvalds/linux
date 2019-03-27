/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 *  @{
 * @file   al_hal_eth_ec_regs.h
 *
 * @brief Ethernet controller registers
 *
 */

#ifndef __AL_HAL_EC_REG_H
#define __AL_HAL_EC_REG_H

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/



struct al_ec_gen {
	/* [0x0] Ethernet controller Version */
	uint32_t version;
	/* [0x4] Enable modules operation. */
	uint32_t en;
	/* [0x8] Enable FIFO operation on the EC side. */
	uint32_t fifo_en;
	/* [0xc] General L2 configuration for the Ethernet controlle ... */
	uint32_t l2;
	/* [0x10] Configure protocol index values */
	uint32_t cfg_i;
	/* [0x14] Configure protocol index values (extended protocols ... */
	uint32_t cfg_i_ext;
	/* [0x18] Enable modules operation (extended operations). */
	uint32_t en_ext;
	uint32_t rsrvd[9];
};
struct al_ec_mac {
	/* [0x0] General configuration of the MAC side of the Ethern ... */
	uint32_t gen;
	/* [0x4] Minimum packet size  */
	uint32_t min_pkt;
	/* [0x8] Maximum packet size  */
	uint32_t max_pkt;
	uint32_t rsrvd[13];
};
struct al_ec_rxf {
	/* [0x0] Rx FIFO input controller configuration 1 */
	uint32_t cfg_1;
	/* [0x4] Rx FIFO input controller configuration 2 */
	uint32_t cfg_2;
	/* [0x8] Threshold to start reading packet from the Rx FIFO */
	uint32_t rd_fifo;
	/* [0xc] Threshold to stop writing packet to the Rx FIFO */
	uint32_t wr_fifo;
	/* [0x10] Threshold to stop writing packet to the loopback FI ... */
	uint32_t lb_fifo;
	/* [0x14] Rx FIFO input controller loopback FIFO configuratio ... */
	uint32_t cfg_lb;
	/* [0x18] Configuration for dropping packet at the FIFO outpu ... */
	uint32_t out_drop;
	uint32_t rsrvd[25];
};
struct al_ec_epe {
	/* [0x0] Ethernet parsing engine configuration 1 */
	uint32_t parse_cfg;
	/* [0x4] Protocol index action table address */
	uint32_t act_table_addr;
	/* [0x8] Protocol index action table data */
	uint32_t act_table_data_1;
	/* [0xc] Protocol index action table data */
	uint32_t act_table_data_2;
	/* [0x10] Protocol index action table data */
	uint32_t act_table_data_3;
	/* [0x14] Protocol index action table data */
	uint32_t act_table_data_4;
	/* [0x18] Protocol index action table data */
	uint32_t act_table_data_5;
	/* [0x1c] Protocol index action table data */
	uint32_t act_table_data_6;
	/* [0x20] Input result vector, default values for parser inpu ... */
	uint32_t res_def;
	/* [0x24] Result input vector selection */
	uint32_t res_in;
	uint32_t rsrvd[6];
};
struct al_ec_epe_res {
	/* [0x0] Parser result vector pointer */
	uint32_t p1;
	/* [0x4] Parser result vector pointer */
	uint32_t p2;
	/* [0x8] Parser result vector pointer */
	uint32_t p3;
	/* [0xc] Parser result vector pointer */
	uint32_t p4;
	/* [0x10] Parser result vector pointer */
	uint32_t p5;
	/* [0x14] Parser result vector pointer */
	uint32_t p6;
	/* [0x18] Parser result vector pointer */
	uint32_t p7;
	/* [0x1c] Parser result vector pointer */
	uint32_t p8;
	/* [0x20] Parser result vector pointer */
	uint32_t p9;
	/* [0x24] Parser result vector pointer */
	uint32_t p10;
	/* [0x28] Parser result vector pointer */
	uint32_t p11;
	/* [0x2c] Parser result vector pointer */
	uint32_t p12;
	/* [0x30] Parser result vector pointer */
	uint32_t p13;
	/* [0x34] Parser result vector pointer */
	uint32_t p14;
	/* [0x38] Parser result vector pointer */
	uint32_t p15;
	/* [0x3c] Parser result vector pointer */
	uint32_t p16;
	/* [0x40] Parser result vector pointer */
	uint32_t p17;
	/* [0x44] Parser result vector pointer */
	uint32_t p18;
	/* [0x48] Parser result vector pointer */
	uint32_t p19;
	/* [0x4c] Parser result vector pointer */
	uint32_t p20;
	uint32_t rsrvd[12];
};
struct al_ec_epe_h {
	/* [0x0] Header length, support for header length table for  ... */
	uint32_t hdr_len;
};
struct al_ec_epe_p {
	/* [0x0] Data  for comparison */
	uint32_t comp_data;
	/* [0x4] Mask for comparison */
	uint32_t comp_mask;
	/* [0x8] Compare control */
	uint32_t comp_ctrl;
	uint32_t rsrvd[4];
};
struct al_ec_epe_a {
	/* [0x0] Protocol index action register */
	uint32_t prot_act;
};
struct al_ec_rfw {
	/* [0x0] Tuple (4/2) Hash configuration */
	uint32_t thash_cfg_1;
	/* [0x4] Tuple (4/2) Hash configuration */
	uint32_t thash_cfg_2;
	/* [0x8] MAC Hash configuration */
	uint32_t mhash_cfg_1;
	/* [0xc] MAC Hash configuration */
	uint32_t mhash_cfg_2;
	/* [0x10] MAC Hash configuration */
	uint32_t hdr_split;
	/* [0x14] Masking the errors described in  register rxf_drop  ... */
	uint32_t meta_err;
	/* [0x18] Configuration for generating the MetaData for the R ... */
	uint32_t meta;
	/* [0x1c] Configuration for generating the MetaData for the R ... */
	uint32_t filter;
	/* [0x20] 4 tupple hash table address */
	uint32_t thash_table_addr;
	/* [0x24] 4 tupple hash table data */
	uint32_t thash_table_data;
	/* [0x28] MAC hash table address */
	uint32_t mhash_table_addr;
	/* [0x2c] MAC hash table data */
	uint32_t mhash_table_data;
	/* [0x30] VLAN table address */
	uint32_t vid_table_addr;
	/* [0x34] VLAN table data */
	uint32_t vid_table_data;
	/* [0x38] VLAN p-bits table address */
	uint32_t pbits_table_addr;
	/* [0x3c] VLAN p-bits table data */
	uint32_t pbits_table_data;
	/* [0x40] DSCP table address */
	uint32_t dscp_table_addr;
	/* [0x44] DSCP table data */
	uint32_t dscp_table_data;
	/* [0x48] TC table address */
	uint32_t tc_table_addr;
	/* [0x4c] TC table data */
	uint32_t tc_table_data;
	/* [0x50] Control table address */
	uint32_t ctrl_table_addr;
	/* [0x54] Control table data */
	uint32_t ctrl_table_data;
	/* [0x58] Forwarding output configuration */
	uint32_t out_cfg;
	/* [0x5c] Flow steering mechanism,
Table address */
	uint32_t fsm_table_addr;
	/* [0x60] Flow steering mechanism,
Table data */
	uint32_t fsm_table_data;
	/* [0x64] Selection of data to be used in packet forwarding0  ... */
	uint32_t ctrl_sel;
	/* [0x68] Default VLAN data, used for untagged packets */
	uint32_t default_vlan;
	/* [0x6c] Default HASH output values */
	uint32_t default_hash;
	/* [0x70] Default override values, if a packet was filtered b ... */
	uint32_t default_or;
	/* [0x74] Latched information when a drop condition occurred */
	uint32_t drop_latch;
	/* [0x78] Check sum calculation configuration */
	uint32_t checksum;
	/* [0x7c] LRO offload engine configuration register */
	uint32_t lro_cfg_1;
	/* [0x80] LRO offload engine Check rules configurations for I ... */
	uint32_t lro_check_ipv4;
	/* [0x84] LRO offload engine IPv4 values configuration */
	uint32_t lro_ipv4;
	/* [0x88] LRO offload engine Check rules configurations for I ... */
	uint32_t lro_check_ipv6;
	/* [0x8c] LRO offload engine IPv6 values configuration */
	uint32_t lro_ipv6;
	/* [0x90] LRO offload engine Check rules configurations for T ... */
	uint32_t lro_check_tcp;
	/* [0x94] LRO offload engine IPv6 values configuration */
	uint32_t lro_tcp;
	/* [0x98] LRO offload engine Check rules configurations for U ... */
	uint32_t lro_check_udp;
	/* [0x9c] LRO offload engine Check rules configurations for U ... */
	uint32_t lro_check_l2;
	/* [0xa0] LRO offload engine Check rules configurations for U ... */
	uint32_t lro_check_gen;
	/* [0xa4] Rules for storing packet information into the cache ... */
	uint32_t lro_store;
	/* [0xa8] VLAN table default */
	uint32_t vid_table_def;
	/* [0xac] Control table default */
	uint32_t ctrl_table_def;
	/* [0xb0] Additional configuration 0 */
	uint32_t cfg_a_0;
	/* [0xb4] Tuple (4/2) Hash configuration (extended for RoCE a ... */
	uint32_t thash_cfg_3;
	/* [0xb8] Tuple (4/2) Hash configuration , mask for the input ... */
	uint32_t thash_mask_outer_ipv6;
	/* [0xbc] Tuple (4/2) Hash configuration , mask for the input ... */
	uint32_t thash_mask_outer;
	/* [0xc0] Tuple (4/2) Hash configuration , mask for the input ... */
	uint32_t thash_mask_inner_ipv6;
	/* [0xc4] Tuple (4/2) Hash configuration , mask for the input ... */
	uint32_t thash_mask_inner;
	uint32_t rsrvd[10];
};
struct al_ec_rfw_udma {
	/* [0x0] Per UDMA default configuration */
	uint32_t def_cfg;
};
struct al_ec_rfw_hash {
	/* [0x0] key configuration (320 bits) */
	uint32_t key;
};
struct al_ec_rfw_priority {
	/* [0x0] Priority to queue mapping configuration */
	uint32_t queue;
};
struct al_ec_rfw_default {
	/* [0x0] Default forwarding configuration options */
	uint32_t opt_1;
};
struct al_ec_fwd_mac {
	/* [0x0] MAC address data [31:0] */
	uint32_t data_l;
	/* [0x4] MAC address data [15:0] */
	uint32_t data_h;
	/* [0x8] MAC address mask [31:0] */
	uint32_t mask_l;
	/* [0xc] MAC address mask [15:0] */
	uint32_t mask_h;
	/* [0x10] MAC compare control */
	uint32_t ctrl;
};
struct al_ec_msw {
	/* [0x0] Configuration for unicast packets */
	uint32_t uc;
	/* [0x4] Configuration for multicast packets */
	uint32_t mc;
	/* [0x8] Configuration for broadcast packets */
	uint32_t bc;
	uint32_t rsrvd[3];
};
struct al_ec_tso {
	/* [0x0] Input configuration */
	uint32_t in_cfg;
	/* [0x4] MetaData default cache table address */
	uint32_t cache_table_addr;
	/* [0x8] MetaData default cache table data */
	uint32_t cache_table_data_1;
	/* [0xc] MetaData default cache table data */
	uint32_t cache_table_data_2;
	/* [0x10] MetaData default cache table data */
	uint32_t cache_table_data_3;
	/* [0x14] MetaData default cache table data */
	uint32_t cache_table_data_4;
	/* [0x18] TCP control bit operation for first segment */
	uint32_t ctrl_first;
	/* [0x1c] TCP control bit operation for middle segments  */
	uint32_t ctrl_middle;
	/* [0x20] TCP control bit operation for last segment */
	uint32_t ctrl_last;
	/* [0x24] Additional TSO configurations */
	uint32_t cfg_add_0;
	/* [0x28] TSO configuration for tunnelled packets */
	uint32_t cfg_tunnel;
	uint32_t rsrvd[13];
};
struct al_ec_tso_sel {
	/* [0x0] MSS value */
	uint32_t mss;
};
struct al_ec_tpe {
	/* [0x0] Parsing configuration */
	uint32_t parse;
	uint32_t rsrvd[15];
};
struct al_ec_tpm_udma {
	/* [0x0] Default VLAN data */
	uint32_t vlan_data;
	/* [0x4] UDMA MAC SA information for spoofing */
	uint32_t mac_sa_1;
	/* [0x8] UDMA MAC SA information for spoofing */
	uint32_t mac_sa_2;
};
struct al_ec_tpm_sel {
	/* [0x0] Ethertype values for VLAN modification */
	uint32_t etype;
};
struct al_ec_tfw {
	/* [0x0] Tx FIFO Wr configuration */
	uint32_t tx_wr_fifo;
	/* [0x4] VLAN table address */
	uint32_t tx_vid_table_addr;
	/* [0x8] VLAN table data */
	uint32_t tx_vid_table_data;
	/* [0xc] Tx FIFO Rd configuration */
	uint32_t tx_rd_fifo;
	/* [0x10] Tx FIFO Rd configuration, checksum insertion */
	uint32_t tx_checksum;
	/* [0x14] Tx forwarding general configuration register */
	uint32_t tx_gen;
	/* [0x18] Tx spoofing configuration */
	uint32_t tx_spf;
	/* [0x1c] TX data FIFO status */
	uint32_t data_fifo;
	/* [0x20] Tx control FIFO status */
	uint32_t ctrl_fifo;
	/* [0x24] Tx header FIFO status */
	uint32_t hdr_fifo;
	uint32_t rsrvd[14];
};
struct al_ec_tfw_udma {
	/* [0x0] Default GMDA output bitmap for unicast packet */
	uint32_t uc_udma;
	/* [0x4] Default GMDA output bitmap for multicast packet */
	uint32_t mc_udma;
	/* [0x8] Default GMDA output bitmap for broadcast packet */
	uint32_t bc_udma;
	/* [0xc] Tx spoofing configuration */
	uint32_t spf_cmd;
	/* [0x10] Forwarding decision control */
	uint32_t fwd_dec;
	uint32_t rsrvd;
};
struct al_ec_tmi {
	/* [0x0] Forward packets back to the Rx data path for local  ... */
	uint32_t tx_cfg;
	uint32_t rsrvd[3];
};
struct al_ec_efc {
	/* [0x0] Mask of pause_on  [7:0] for the Ethernet controller ... */
	uint32_t ec_pause;
	/* [0x4] Mask of Ethernet controller Almost Full indication  ... */
	uint32_t ec_xoff;
	/* [0x8] Mask for generating XON indication pulse */
	uint32_t xon;
	/* [0xc] Mask for generating GPIO output XOFF indication fro ... */
	uint32_t gpio;
	/* [0x10] Rx FIFO threshold for generating the Almost Full in ... */
	uint32_t rx_fifo_af;
	/* [0x14] Rx FIFO threshold for generating the Almost Full in ... */
	uint32_t rx_fifo_hyst;
	/* [0x18] Rx FIFO threshold for generating the Almost Full in ... */
	uint32_t stat;
	/* [0x1c] XOFF timer for the 1G MACSets the interval (in SB_C ... */
	uint32_t xoff_timer_1g;
	/* [0x20] PFC force flow control generation */
	uint32_t ec_pfc;
	uint32_t rsrvd[3];
};
struct al_ec_fc_udma {
	/* [0x0] Mask of "pause_on"  [0] for all queues */
	uint32_t q_pause_0;
	/* [0x4] Mask of "pause_on"  [1] for all queues */
	uint32_t q_pause_1;
	/* [0x8] Mask of "pause_on"  [2] for all queues */
	uint32_t q_pause_2;
	/* [0xc] Mask of "pause_on"  [3] for all queues */
	uint32_t q_pause_3;
	/* [0x10] Mask of "pause_on"  [4] for all queues */
	uint32_t q_pause_4;
	/* [0x14] Mask of "pause_on"  [5] for all queues */
	uint32_t q_pause_5;
	/* [0x18] Mask of "pause_on"  [6] for all queues */
	uint32_t q_pause_6;
	/* [0x1c] Mask of "pause_on"  [7] for all queues */
	uint32_t q_pause_7;
	/* [0x20] Mask of external GPIO input pause [0] for all queue ... */
	uint32_t q_gpio_0;
	/* [0x24] Mask of external GPIO input pause [1] for all queue ... */
	uint32_t q_gpio_1;
	/* [0x28] Mask of external GPIO input pause [2] for all queue ... */
	uint32_t q_gpio_2;
	/* [0x2c] Mask of external GPIO input pause [3] for all queue ... */
	uint32_t q_gpio_3;
	/* [0x30] Mask of external GPIO input [4] for all queues */
	uint32_t q_gpio_4;
	/* [0x34] Mask of external GPIO input [5] for all queues */
	uint32_t q_gpio_5;
	/* [0x38] Mask of external GPIO input [6] for all queues */
	uint32_t q_gpio_6;
	/* [0x3c] Mask of external GPIO input [7] for all queues */
	uint32_t q_gpio_7;
	/* [0x40] Mask of "pause_on"  [7:0] for the UDMA stream inter ... */
	uint32_t s_pause;
	/* [0x44] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_0;
	/* [0x48] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_1;
	/* [0x4c] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_2;
	/* [0x50] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_3;
	/* [0x54] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_4;
	/* [0x58] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_5;
	/* [0x5c] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_6;
	/* [0x60] Mask of Rx Almost Full indication for generating XO ... */
	uint32_t q_xoff_7;
	uint32_t rsrvd[7];
};
struct al_ec_tpg_rpa_res {
	/* [0x0] NOT used */
	uint32_t not_used;
	uint32_t rsrvd[63];
};
struct al_ec_eee {
	/* [0x0] EEE configuration */
	uint32_t cfg_e;
	/* [0x4] Number of clocks to get into EEE mode. */
	uint32_t pre_cnt;
	/* [0x8] Number of clocks to stop MAC EEE mode after getting ... */
	uint32_t post_cnt;
	/* [0xc] Number of clocks to stop the Tx MAC interface after ... */
	uint32_t stop_cnt;
	/* [0x10] EEE status */
	uint32_t stat_eee;
	uint32_t rsrvd[59];
};
struct al_ec_stat {
	/* [0x0] Rx Frequency adjust FIFO input  packets */
	uint32_t faf_in_rx_pkt;
	/* [0x4] Rx Frequency adjust FIFO input short error packets */
	uint32_t faf_in_rx_short;
	/* [0x8] Rx Frequency adjust FIFO input  long error packets */
	uint32_t faf_in_rx_long;
	/* [0xc] Rx Frequency adjust FIFO output  packets */
	uint32_t faf_out_rx_pkt;
	/* [0x10] Rx Frequency adjust FIFO output short error packets ... */
	uint32_t faf_out_rx_short;
	/* [0x14] Rx Frequency adjust FIFO output long error packets */
	uint32_t faf_out_rx_long;
	/* [0x18] Rx Frequency adjust FIFO output  drop packets */
	uint32_t faf_out_drop;
	/* [0x1c] Number of packets written into the Rx FIFO (without ... */
	uint32_t rxf_in_rx_pkt;
	/* [0x20] Number of error packets written into the Rx FIFO (w ... */
	uint32_t rxf_in_fifo_err;
	/* [0x24] Number of packets written into the loopback FIFO (w ... */
	uint32_t lbf_in_rx_pkt;
	/* [0x28] Number of error packets written into the loopback F ... */
	uint32_t lbf_in_fifo_err;
	/* [0x2c] Number of packets read from Rx FIFO 1 */
	uint32_t rxf_out_rx_1_pkt;
	/* [0x30] Number of packets read from Rx FIFO 2 (loopback FIF ... */
	uint32_t rxf_out_rx_2_pkt;
	/* [0x34] Rx FIFO output drop packets from FIFO 1 */
	uint32_t rxf_out_drop_1_pkt;
	/* [0x38] Rx FIFO output drop packets from FIFO 2 (loopback) */
	uint32_t rxf_out_drop_2_pkt;
	/* [0x3c] Rx Parser 1, input packet counter */
	uint32_t rpe_1_in_rx_pkt;
	/* [0x40] Rx Parser 1, output packet counter */
	uint32_t rpe_1_out_rx_pkt;
	/* [0x44] Rx Parser 2, input packet counter */
	uint32_t rpe_2_in_rx_pkt;
	/* [0x48] Rx Parser 2, output packet counter */
	uint32_t rpe_2_out_rx_pkt;
	/* [0x4c] Rx Parser 3 (MACsec), input packet counter */
	uint32_t rpe_3_in_rx_pkt;
	/* [0x50] Rx Parser 3 (MACsec), output packet counter */
	uint32_t rpe_3_out_rx_pkt;
	/* [0x54] Tx parser, input packet counter */
	uint32_t tpe_in_tx_pkt;
	/* [0x58] Tx parser, output packet counter */
	uint32_t tpe_out_tx_pkt;
	/* [0x5c] Tx packet modification, input packet counter */
	uint32_t tpm_tx_pkt;
	/* [0x60] Tx forwarding input packet counter */
	uint32_t tfw_in_tx_pkt;
	/* [0x64] Tx forwarding input packet counter */
	uint32_t tfw_out_tx_pkt;
	/* [0x68] Rx forwarding input packet counter */
	uint32_t rfw_in_rx_pkt;
	/* [0x6c] Rx Forwarding, packet with VLAN command drop indica ... */
	uint32_t rfw_in_vlan_drop;
	/* [0x70] Rx Forwarding, packets with parse drop indication */
	uint32_t rfw_in_parse_drop;
	/* [0x74] Rx Forwarding, multicast packets */
	uint32_t rfw_in_mc;
	/* [0x78] Rx Forwarding, broadcast packets */
	uint32_t rfw_in_bc;
	/* [0x7c] Rx Forwarding, tagged packets */
	uint32_t rfw_in_vlan_exist;
	/* [0x80] Rx Forwarding, untagged packets */
	uint32_t rfw_in_vlan_nexist;
	/* [0x84] Rx Forwarding, packets with MAC address drop indica ... */
	uint32_t rfw_in_mac_drop;
	/* [0x88] Rx Forwarding, packets with undetected MAC address */
	uint32_t rfw_in_mac_ndet_drop;
	/* [0x8c] Rx Forwarding, packets with drop indication from th ... */
	uint32_t rfw_in_ctrl_drop;
	/* [0x90] Rx Forwarding, packets with L3_protocol_index drop  ... */
	uint32_t rfw_in_prot_i_drop;
	/* [0x94] EEE, number of times the system went into EEE state ... */
	uint32_t eee_in;
	uint32_t rsrvd[90];
};
struct al_ec_stat_udma {
	/* [0x0] Rx forwarding output packet counter */
	uint32_t rfw_out_rx_pkt;
	/* [0x4] Rx forwarding output drop packet counter */
	uint32_t rfw_out_drop;
	/* [0x8] Multi-stream write, number of Rx packets */
	uint32_t msw_in_rx_pkt;
	/* [0xc] Multi-stream write, number of dropped packets at SO ... */
	uint32_t msw_drop_q_full;
	/* [0x10] Multi-stream write, number of dropped packets at SO ... */
	uint32_t msw_drop_sop;
	/* [0x14] Multi-stream write, number of dropped packets at EO ... */
	uint32_t msw_drop_eop;
	/* [0x18] Multi-stream write, number of packets written to th ... */
	uint32_t msw_wr_eop;
	/* [0x1c] Multi-stream write, number of packets read from the ... */
	uint32_t msw_out_rx_pkt;
	/* [0x20] Number of transmitted packets without TSO enabled */
	uint32_t tso_no_tso_pkt;
	/* [0x24] Number of transmitted packets with TSO enabled */
	uint32_t tso_tso_pkt;
	/* [0x28] Number of TSO segments that were generated */
	uint32_t tso_seg_pkt;
	/* [0x2c] Number of TSO segments that required padding */
	uint32_t tso_pad_pkt;
	/* [0x30] Tx Packet modification, MAC SA spoof error  */
	uint32_t tpm_tx_spoof;
	/* [0x34] Tx MAC interface, input packet counter */
	uint32_t tmi_in_tx_pkt;
	/* [0x38] Tx MAC interface, number of packets forwarded to th ... */
	uint32_t tmi_out_to_mac;
	/* [0x3c] Tx MAC interface, number of packets forwarded to th ... */
	uint32_t tmi_out_to_rx;
	/* [0x40] Tx MAC interface, number of transmitted bytes */
	uint32_t tx_q0_bytes;
	/* [0x44] Tx MAC interface, number of transmitted bytes */
	uint32_t tx_q1_bytes;
	/* [0x48] Tx MAC interface, number of transmitted bytes */
	uint32_t tx_q2_bytes;
	/* [0x4c] Tx MAC interface, number of transmitted bytes */
	uint32_t tx_q3_bytes;
	/* [0x50] Tx MAC interface, number of transmitted packets */
	uint32_t tx_q0_pkts;
	/* [0x54] Tx MAC interface, number of transmitted packets */
	uint32_t tx_q1_pkts;
	/* [0x58] Tx MAC interface, number of transmitted packets */
	uint32_t tx_q2_pkts;
	/* [0x5c] Tx MAC interface, number of transmitted packets */
	uint32_t tx_q3_pkts;
	uint32_t rsrvd[40];
};
struct al_ec_msp {
	/* [0x0] Ethernet parsing engine configuration 1 */
	uint32_t p_parse_cfg;
	/* [0x4] Protocol index action table address */
	uint32_t p_act_table_addr;
	/* [0x8] Protocol index action table data */
	uint32_t p_act_table_data_1;
	/* [0xc] Protocol index action table data */
	uint32_t p_act_table_data_2;
	/* [0x10] Protocol index action table data */
	uint32_t p_act_table_data_3;
	/* [0x14] Protocol index action table data */
	uint32_t p_act_table_data_4;
	/* [0x18] Protocol index action table data */
	uint32_t p_act_table_data_5;
	/* [0x1c] Protocol index action table data */
	uint32_t p_act_table_data_6;
	/* [0x20] Input result vector, default values for parser inpu ... */
	uint32_t p_res_def;
	/* [0x24] Result input vector selection */
	uint32_t p_res_in;
	uint32_t rsrvd[6];
};
struct al_ec_msp_p {
	/* [0x0] Header length, support for header length table for  ... */
	uint32_t h_hdr_len;
};
struct al_ec_msp_c {
	/* [0x0] Data  for comparison */
	uint32_t p_comp_data;
	/* [0x4] Mask for comparison */
	uint32_t p_comp_mask;
	/* [0x8] Compare control */
	uint32_t p_comp_ctrl;
	uint32_t rsrvd[4];
};
struct al_ec_wol {
	/* [0x0] WoL enable configuration,Packet forwarding and inte ... */
	uint32_t wol_en;
	/* [0x4] Password for magic_password packet detection - bits ... */
	uint32_t magic_pswd_l;
	/* [0x8] Password for magic+password packet detection -  47: ... */
	uint32_t magic_pswd_h;
	/* [0xc] Configured L3 Destination IP address for WoL IPv6 p ... */
	uint32_t ipv6_dip_word0;
	/* [0x10] Configured L3 Destination IP address for WoL IPv6 p ... */
	uint32_t ipv6_dip_word1;
	/* [0x14] Configured L3 Destination IP address for WoL IPv6 p ... */
	uint32_t ipv6_dip_word2;
	/* [0x18] Configured L3 Destination IP address for WoL IPv6 p ... */
	uint32_t ipv6_dip_word3;
	/* [0x1c] Configured L3 Destination IP address for WoL IPv4 p ... */
	uint32_t ipv4_dip;
	/* [0x20] Configured EtherType for WoL EtherType_da/EtherType ... */
	uint32_t ethertype;
	uint32_t rsrvd[7];
};
struct al_ec_pth {
	/* [0x0] System time counter (Time of Day) */
	uint32_t system_time_seconds;
	/* [0x4] System time subseconds in a second (MSBs) */
	uint32_t system_time_subseconds_msb;
	/* [0x8] System time subseconds in a second (LSBs) */
	uint32_t system_time_subseconds_lsb;
	/* [0xc] Clock period in femtoseconds (MSB) */
	uint32_t clock_period_msb;
	/* [0x10] Clock period in femtoseconds (LSB) */
	uint32_t clock_period_lsb;
	/* [0x14] Control register for internal updates to the system ... */
	uint32_t int_update_ctrl;
	/* [0x18] Value to update system_time_seconds with */
	uint32_t int_update_seconds;
	/* [0x1c] Value to update system_time_subseconds_msb with */
	uint32_t int_update_subseconds_msb;
	/* [0x20] Value to update system_time_subseconds_lsb with */
	uint32_t int_update_subseconds_lsb;
	/* [0x24] Control register for external updates to the system ... */
	uint32_t ext_update_ctrl;
	/* [0x28] Value to update system_time_seconds with */
	uint32_t ext_update_seconds;
	/* [0x2c] Value to update system_time_subseconds_msb with */
	uint32_t ext_update_subseconds_msb;
	/* [0x30] Value to update system_time_subseconds_lsb with */
	uint32_t ext_update_subseconds_lsb;
	/* [0x34] This value represents the APB transaction delay fro ... */
	uint32_t read_compensation_subseconds_msb;
	/* [0x38] This value represents the APB transaction delay fro ... */
	uint32_t read_compensation_subseconds_lsb;
	/* [0x3c] This value is used for two purposes:1 */
	uint32_t int_write_compensation_subseconds_msb;
	/* [0x40] This value is used for two purposes:1 */
	uint32_t int_write_compensation_subseconds_lsb;
	/* [0x44] This value represents the number of cycles it for a ... */
	uint32_t ext_write_compensation_subseconds_msb;
	/* [0x48] This value represents the number of cycles it for a ... */
	uint32_t ext_write_compensation_subseconds_lsb;
	/* [0x4c] Value to be added to system_time before transferrin ... */
	uint32_t sync_compensation_subseconds_msb;
	/* [0x50] Value to be added to system_time before transferrin ... */
	uint32_t sync_compensation_subseconds_lsb;
	uint32_t rsrvd[11];
};
struct al_ec_pth_egress {
	/* [0x0] Control register for egress trigger #k */
	uint32_t trigger_ctrl;
	/* [0x4] threshold for next egress trigger (#k) - secondsWri ... */
	uint32_t trigger_seconds;
	/* [0x8] Threshold for next egress trigger (#k) - subseconds ... */
	uint32_t trigger_subseconds_msb;
	/* [0xc] threshold for next egress trigger (#k) - subseconds ... */
	uint32_t trigger_subseconds_lsb;
	/* [0x10] External output pulse width (subseconds_msb)(Atomic ... */
	uint32_t pulse_width_subseconds_msb;
	/* [0x14] External output pulse width (subseconds_lsb)(Atomic ... */
	uint32_t pulse_width_subseconds_lsb;
	uint32_t rsrvd[2];
};
struct al_ec_pth_db {
	/* [0x0] timestamp[k], in resolution of 2^18 femtosec =~ 0 */
	uint32_t ts;
	/* [0x4] Timestamp entry is valid */
	uint32_t qual;
	uint32_t rsrvd[4];
};
struct al_ec_gen_v3 {
	/* [0x0] Bypass enable */
	uint32_t bypass;
	/* [0x4] Rx Completion descriptor */
	uint32_t rx_comp_desc;
	/* [0x8] general configuration */
	uint32_t conf;
	uint32_t rsrvd[13];
};
struct al_ec_tfw_v3 {
	/* [0x0] Generic protocol detect Cam compare table address */
	uint32_t tx_gpd_cam_addr;
	/* [0x4] Tx Generic protocol detect Cam compare data_1 (low) ... */
	uint32_t tx_gpd_cam_data_1;
	/* [0x8] Tx Generic protocol detect Cam compare data_2 (high ... */
	uint32_t tx_gpd_cam_data_2;
	/* [0xc] Tx Generic protocol detect Cam compare mask_1 (low) ... */
	uint32_t tx_gpd_cam_mask_1;
	/* [0x10] Tx Generic protocol detect Cam compare mask_1 (high ... */
	uint32_t tx_gpd_cam_mask_2;
	/* [0x14] Tx Generic protocol detect Cam compare control */
	uint32_t tx_gpd_cam_ctrl;
	/* [0x18] Tx Generic crc parameters legacy */
	uint32_t tx_gcp_legacy;
	/* [0x1c] Tx Generic crc prameters table address */
	uint32_t tx_gcp_table_addr;
	/* [0x20] Tx Generic crc prameters table general */
	uint32_t tx_gcp_table_gen;
	/* [0x24] Tx Generic crc parametrs tabel mask word 1 */
	uint32_t tx_gcp_table_mask_1;
	/* [0x28] Tx Generic crc parametrs tabel mask word 2 */
	uint32_t tx_gcp_table_mask_2;
	/* [0x2c] Tx Generic crc parametrs tabel mask word 3 */
	uint32_t tx_gcp_table_mask_3;
	/* [0x30] Tx Generic crc parametrs tabel mask word 4 */
	uint32_t tx_gcp_table_mask_4;
	/* [0x34] Tx Generic crc parametrs tabel mask word 5 */
	uint32_t tx_gcp_table_mask_5;
	/* [0x38] Tx Generic crc parametrs tabel mask word 6 */
	uint32_t tx_gcp_table_mask_6;
	/* [0x3c] Tx Generic crc parametrs tabel crc init */
	uint32_t tx_gcp_table_crc_init;
	/* [0x40] Tx Generic crc parametrs tabel result configuration ... */
	uint32_t tx_gcp_table_res;
	/* [0x44] Tx Generic crc parameters table alu opcode */
	uint32_t tx_gcp_table_alu_opcode;
	/* [0x48] Tx Generic crc parameters table alu opsel */
	uint32_t tx_gcp_table_alu_opsel;
	/* [0x4c] Tx Generic crc parameters table alu constant value */
	uint32_t tx_gcp_table_alu_val;
	/* [0x50] Tx CRC/Checksum replace */
	uint32_t crc_csum_replace;
	/* [0x54] CRC/Checksum replace table address */
	uint32_t crc_csum_replace_table_addr;
	/* [0x58] CRC/Checksum replace table */
	uint32_t crc_csum_replace_table;
	uint32_t rsrvd[9];
};

struct al_ec_rfw_v3 {
	/* [0x0] Rx Generic protocol detect Cam compare table addres ... */
	uint32_t rx_gpd_cam_addr;
	/* [0x4] Rx Generic protocol detect Cam compare data_1 (low) ... */
	uint32_t rx_gpd_cam_data_1;
	/* [0x8] Rx Generic protocol detect Cam compare data_2 (high ... */
	uint32_t rx_gpd_cam_data_2;
	/* [0xc] Rx Generic protocol detect Cam compare mask_1 (low) ... */
	uint32_t rx_gpd_cam_mask_1;
	/* [0x10] Rx Generic protocol detect Cam compare mask_1 (high ... */
	uint32_t rx_gpd_cam_mask_2;
	/* [0x14] Rx Generic protocol detect Cam compare control */
	uint32_t rx_gpd_cam_ctrl;
	/* [0x18] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p1;
	/* [0x1c] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p2;
	/* [0x20] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p3;
	/* [0x24] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p4;
	/* [0x28] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p5;
	/* [0x2c] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p6;
	/* [0x30] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p7;
	/* [0x34] Generic protocol detect Parser result vector pointe ... */
	uint32_t gpd_p8;
	/* [0x38] Rx Generic crc parameters legacy */
	uint32_t rx_gcp_legacy;
	/* [0x3c] Rx Generic crc prameters table address */
	uint32_t rx_gcp_table_addr;
	/* [0x40] Rx Generic crc prameters table general */
	uint32_t rx_gcp_table_gen;
	/* [0x44] Rx Generic crc parametrs tabel mask word 1 */
	uint32_t rx_gcp_table_mask_1;
	/* [0x48] Rx Generic crc parametrs tabel mask word 2 */
	uint32_t rx_gcp_table_mask_2;
	/* [0x4c] Rx Generic crc parametrs tabel mask word 3 */
	uint32_t rx_gcp_table_mask_3;
	/* [0x50] Rx Generic crc parametrs tabel mask word 4 */
	uint32_t rx_gcp_table_mask_4;
	/* [0x54] Rx Generic crc parametrs tabel mask word 5 */
	uint32_t rx_gcp_table_mask_5;
	/* [0x58] Rx Generic crc parametrs tabel mask word 6 */
	uint32_t rx_gcp_table_mask_6;
	/* [0x5c] Rx Generic crc parametrs tabel crc init */
	uint32_t rx_gcp_table_crc_init;
	/* [0x60] Rx Generic crc parametrs tabel result configuration ... */
	uint32_t rx_gcp_table_res;
	/* [0x64] Rx Generic crc  parameters table alu opcode */
	uint32_t rx_gcp_table_alu_opcode;
	/* [0x68] Rx Generic crc  parameters table alu opsel */
	uint32_t rx_gcp_table_alu_opsel;
	/* [0x6c] Rx Generic crc  parameters table alu constant value ... */
	uint32_t rx_gcp_table_alu_val;
	/* [0x70] Generic crc engin parameters alu Parser result vect ... */
	uint32_t rx_gcp_alu_p1;
	/* [0x74] Generic crc engine parameters alu Parser result vec ... */
	uint32_t rx_gcp_alu_p2;
	/* [0x78] Header split control table address */
	uint32_t hs_ctrl_table_addr;
	/* [0x7c] Header split control table */
	uint32_t hs_ctrl_table;
	/* [0x80] Header split control alu opcode */
	uint32_t hs_ctrl_table_alu_opcode;
	/* [0x84] Header split control alu opsel */
	uint32_t hs_ctrl_table_alu_opsel;
	/* [0x88] Header split control alu constant value */
	uint32_t hs_ctrl_table_alu_val;
	/* [0x8c] Header split control configuration */
	uint32_t hs_ctrl_cfg;
	/* [0x90] Header split control alu Parser result vector point ... */
	uint32_t hs_ctrl_alu_p1;
	/* [0x94] Header split control alu Parser result vector point ... */
	uint32_t hs_ctrl_alu_p2;
	uint32_t rsrvd[26];
};
struct al_ec_crypto {
	/* [0x0] Tx inline crypto configuration */
	uint32_t tx_config;
	/* [0x4] Rx inline crypto configuration */
	uint32_t rx_config;
	/* [0x8] reserved FFU */
	uint32_t tx_override;
	/* [0xc] reserved FFU */
	uint32_t rx_override;
	/* [0x10] inline XTS alpha [31:0] */
	uint32_t xts_alpha_1;
	/* [0x14] inline XTS alpha [63:32] */
	uint32_t xts_alpha_2;
	/* [0x18] inline XTS alpha [95:64] */
	uint32_t xts_alpha_3;
	/* [0x1c] inline XTS alpha [127:96] */
	uint32_t xts_alpha_4;
	/* [0x20] inline XTS sector ID increment [31:0] */
	uint32_t xts_sector_id_1;
	/* [0x24] inline XTS sector ID increment [63:32] */
	uint32_t xts_sector_id_2;
	/* [0x28] inline XTS sector ID increment [95:64] */
	uint32_t xts_sector_id_3;
	/* [0x2c] inline XTS sector ID increment [127:96] */
	uint32_t xts_sector_id_4;
	/* [0x30] IV formation configuration */
	uint32_t tx_enc_iv_construction;
	/* [0x34] IV formation configuration */
	uint32_t rx_enc_iv_construction;
	/* [0x38] IV formation configuration */
	uint32_t rx_enc_iv_map;
	/*
	[0x3c] effectively shorten shift-registers used for
	eop-pkt-trim, in order to improve performance.
	Each value must be built of consecutive 1's (bypassed regs),
	and then consecutive 0's (non-bypassed regs)
	*/
	uint32_t tx_pkt_trim_len;
	/*
	[0x40] effectively shorten shift-registers used for
	eop-pkt-trim, in order to improve performance.
	Each value must be built of consecutive 1's (bypassed regs),
	and then consecutive 0's (non-bypassed regs)
	*/
	uint32_t rx_pkt_trim_len;
	/* [0x44] reserved FFU */
	uint32_t tx_reserved;
	/* [0x48] reserved FFU */
	uint32_t rx_reserved;
	uint32_t rsrvd[13];
};
struct al_ec_crypto_perf_cntr {
	/* [0x0]  */
	uint32_t total_tx_pkts;
	/* [0x4]  */
	uint32_t total_rx_pkts;
	/* [0x8]  */
	uint32_t total_tx_secured_pkts;
	/* [0xc]  */
	uint32_t total_rx_secured_pkts;
	/* [0x10]  */
	uint32_t total_tx_secured_pkts_cipher_mode;
	/* [0x14]  */
	uint32_t total_tx_secured_pkts_cipher_mode_cmpr;
	/* [0x18]  */
	uint32_t total_rx_secured_pkts_cipher_mode;
	/* [0x1c]  */
	uint32_t total_rx_secured_pkts_cipher_mode_cmpr;
	/* [0x20]  */
	uint32_t total_tx_secured_bytes_low;
	/* [0x24]  */
	uint32_t total_tx_secured_bytes_high;
	/* [0x28]  */
	uint32_t total_rx_secured_bytes_low;
	/* [0x2c]  */
	uint32_t total_rx_secured_bytes_high;
	/* [0x30]  */
	uint32_t total_tx_sign_calcs;
	/* [0x34]  */
	uint32_t total_rx_sign_calcs;
	/* [0x38]  */
	uint32_t total_tx_sign_errs;
	/* [0x3c]  */
	uint32_t total_rx_sign_errs;
};
struct al_ec_crypto_tx_tid {
	/* [0x0] tid_default_entry */
	uint32_t def_val;
};

struct al_ec_regs {
	uint32_t rsrvd_0[32];
	struct al_ec_gen gen;                                /* [0x80] */
	struct al_ec_mac mac;                                /* [0xc0] */
	struct al_ec_rxf rxf;                                /* [0x100] */
	struct al_ec_epe epe[2];                             /* [0x180] */
	struct al_ec_epe_res epe_res;                        /* [0x200] */
	struct al_ec_epe_h epe_h[32];                        /* [0x280] */
	struct al_ec_epe_p epe_p[32];                        /* [0x300] */
	struct al_ec_epe_a epe_a[32];                        /* [0x680] */
	struct al_ec_rfw rfw;                                /* [0x700] */
	struct al_ec_rfw_udma rfw_udma[4];                   /* [0x7f0] */
	struct al_ec_rfw_hash rfw_hash[10];                  /* [0x800] */
	struct al_ec_rfw_priority rfw_priority[8];           /* [0x828] */
	struct al_ec_rfw_default rfw_default[8];             /* [0x848] */
	struct al_ec_fwd_mac fwd_mac[32];                    /* [0x868] */
	struct al_ec_msw msw;                                /* [0xae8] */
	struct al_ec_tso tso;                                /* [0xb00] */
	struct al_ec_tso_sel tso_sel[8];                     /* [0xb60] */
	struct al_ec_tpe tpe;                                /* [0xb80] */
	struct al_ec_tpm_udma tpm_udma[4];                   /* [0xbc0] */
	struct al_ec_tpm_sel tpm_sel[4];                     /* [0xbf0] */
	struct al_ec_tfw tfw;                                /* [0xc00] */
	struct al_ec_tfw_udma tfw_udma[4];                   /* [0xc60] */
	struct al_ec_tmi tmi;                                /* [0xcc0] */
	struct al_ec_efc efc;                                /* [0xcd0] */
	struct al_ec_fc_udma fc_udma[4];                     /* [0xd00] */
	struct al_ec_tpg_rpa_res tpg_rpa_res;                /* [0xf00] */
	struct al_ec_eee eee;                                /* [0x1000] */
	struct al_ec_stat stat;                              /* [0x1100] */
	struct al_ec_stat_udma stat_udma[4];                 /* [0x1300] */
	struct al_ec_msp msp;                                /* [0x1700] */
	struct al_ec_msp_p msp_p[32];                        /* [0x1740] */
	struct al_ec_msp_c msp_c[32];                        /* [0x17c0] */
	uint32_t rsrvd_1[16];
	struct al_ec_wol wol;                                /* [0x1b80] */
	uint32_t rsrvd_2[80];
	struct al_ec_pth pth;                                /* [0x1d00] */
	struct al_ec_pth_egress pth_egress[8];               /* [0x1d80] */
	struct al_ec_pth_db pth_db[16];                      /* [0x1e80] */
	uint32_t rsrvd_3[416];
	struct al_ec_gen_v3 gen_v3;                             /* [0x2680] */
	struct al_ec_tfw_v3 tfw_v3;                             /* [0x26c0] */
	struct al_ec_rfw_v3 rfw_v3;                             /* [0x2740] */
	struct al_ec_crypto crypto;                             /* [0x2840] */
	struct al_ec_crypto_perf_cntr crypto_perf_cntr[2];      /* [0x28c0] */
	uint32_t rsrvd_4[48];
	struct al_ec_crypto_tx_tid crypto_tx_tid[8];            /* [0x2a00] */
};


/*
* Registers Fields
*/


/**** version register ****/
/* Revision number (Minor) */
#define EC_GEN_VERSION_RELEASE_NUM_MINOR_MASK 0x000000FF
#define EC_GEN_VERSION_RELEASE_NUM_MINOR_SHIFT 0
/* Revision number (Major) */
#define EC_GEN_VERSION_RELEASE_NUM_MAJOR_MASK 0x0000FF00
#define EC_GEN_VERSION_RELEASE_NUM_MAJOR_SHIFT 8
/* Day of release */
#define EC_GEN_VERSION_DATE_DAY_MASK     0x001F0000
#define EC_GEN_VERSION_DATE_DAY_SHIFT    16
/* Month of release */
#define EC_GEN_VERSION_DATA_MONTH_MASK   0x01E00000
#define EC_GEN_VERSION_DATA_MONTH_SHIFT  21
/* Year of release (starting from 2000) */
#define EC_GEN_VERSION_DATE_YEAR_MASK    0x3E000000
#define EC_GEN_VERSION_DATE_YEAR_SHIFT   25
/* Reserved */
#define EC_GEN_VERSION_RESERVED_MASK     0xC0000000
#define EC_GEN_VERSION_RESERVED_SHIFT    30

/**** en register ****/
/* Enable Frequency adjust FIFO input controller operation. */
#define EC_GEN_EN_FAF_IN                 (1 << 0)
/* Enable Frequency adjust FIFO output controller operation. */
#define EC_GEN_EN_FAF_OUT                (1 << 1)
/* Enable Rx FIFO input controller 1 operation. */
#define EC_GEN_EN_RXF_IN                 (1 << 2)
/* Enable Rx FIFO output controller  operation. */
#define EC_GEN_EN_RXF_OUT                (1 << 3)
/* Enable Rx forwarding input controller operation. */
#define EC_GEN_EN_RFW_IN                 (1 << 4)
/* Enable Rx forwarding output controller operation. */
#define EC_GEN_EN_RFW_OUT                (1 << 5)
/* Enable Rx multi-stream write controller operation. */
#define EC_GEN_EN_MSW_IN                 (1 << 6)
/* Enable Rx first parsing engine output operation. */
#define EC_GEN_EN_RPE_1_OUT              (1 << 7)
/* Enable Rx first parsing engine input operation. */
#define EC_GEN_EN_RPE_1_IN               (1 << 8)
/* Enable Rx second parsing engine output operation. */
#define EC_GEN_EN_RPE_2_OUT              (1 << 9)
/* Enable Rx second parsing engine input operation. */
#define EC_GEN_EN_RPE_2_IN               (1 << 10)
/* Enable Rx MACsec parsing engine output operation. */
#define EC_GEN_EN_RPE_3_OUT              (1 << 11)
/* Enable Rx MACsec parsing engine input operation. */
#define EC_GEN_EN_RPE_3_IN               (1 << 12)
/* Enable Loopback FIFO input controller 1 operation. */
#define EC_GEN_EN_LBF_IN                 (1 << 13)
/* Enable Rx packet analyzer operation. */
#define EC_GEN_EN_RPA                    (1 << 14)

#define EC_GEN_EN_RESERVED_15            (1 << 15)
/* Enable Tx stream interface operation. */
#define EC_GEN_EN_TSO                    (1 << 16)
/* Enable Tx parser input controller operation. */
#define EC_GEN_EN_TPE_IN                 (1 << 17)
/* Enable Tx parser output controller operation. */
#define EC_GEN_EN_TPE_OUT                (1 << 18)
/* Enable Tx packet modification operation. */
#define EC_GEN_EN_TPM                    (1 << 19)
/* Enable Tx forwarding input controller operation. */
#define EC_GEN_EN_TFW_IN                 (1 << 20)
/* Enable Tx forwarding output controller operation. */
#define EC_GEN_EN_TFW_OUT                (1 << 21)
/* Enable Tx MAC interface controller operation. */
#define EC_GEN_EN_TMI                    (1 << 22)
/* Enable Tx packet generator operation. */
#define EC_GEN_EN_TPG                    (1 << 23)

#define EC_GEN_EN_RESERVED_31_MASK       0xFF000000
#define EC_GEN_EN_RESERVED_31_SHIFT      24

/**** fifo_en register ****/
/* Enable Frequency adjust FIFO operation (input). */
#define EC_GEN_FIFO_EN_FAF_IN            (1 << 0)
/* Enable Frequency adjust FIFO operation (output). */
#define EC_GEN_FIFO_EN_FAF_OUT           (1 << 1)
/* Enable Rx FIFO operation. */
#define EC_GEN_FIFO_EN_RX_FIFO           (1 << 2)
/* Enable Rx forwarding FIFO operation. */
#define EC_GEN_FIFO_EN_RFW_FIFO          (1 << 3)
/* Enable Rx multi-stream write FIFO operation */
#define EC_GEN_FIFO_EN_MSW_FIFO          (1 << 4)
/* Enable Rx first parser FIFO operation. */
#define EC_GEN_FIFO_EN_RPE_1_FIFO        (1 << 5)
/* Enable Rx second parser FIFO operation. */
#define EC_GEN_FIFO_EN_RPE_2_FIFO        (1 << 6)
/* Enable Rx MACsec parser FIFO operation. */
#define EC_GEN_FIFO_EN_RPE_3_FIFO        (1 << 7)
/* Enable Loopback FIFO operation. */
#define EC_GEN_FIFO_EN_LB_FIFO           (1 << 8)

#define EC_GEN_FIFO_EN_RESERVED_15_9_MASK 0x0000FE00
#define EC_GEN_FIFO_EN_RESERVED_15_9_SHIFT 9
/* Enable Tx parser FIFO operation. */
#define EC_GEN_FIFO_EN_TPE_FIFO          (1 << 16)
/* Enable Tx forwarding FIFO operation. */
#define EC_GEN_FIFO_EN_TFW_FIFO          (1 << 17)

#define EC_GEN_FIFO_EN_RESERVED_31_18_MASK 0xFFFC0000
#define EC_GEN_FIFO_EN_RESERVED_31_18_SHIFT 18

/**** l2 register ****/
/* Size of a 802.3 Ethernet header (DA+SA) */
#define EC_GEN_L2_SIZE_802_3_MASK        0x0000003F
#define EC_GEN_L2_SIZE_802_3_SHIFT       0
/* Size of a 802.3 + MACsec 8 byte header */
#define EC_GEN_L2_SIZE_802_3_MS_8_MASK   0x00003F00
#define EC_GEN_L2_SIZE_802_3_MS_8_SHIFT  8
/* Offset of the L2 header from the beginning of the packet. */
#define EC_GEN_L2_OFFSET_MASK            0x7F000000
#define EC_GEN_L2_OFFSET_SHIFT           24

/**** cfg_i register ****/
/* IPv4 protocol index */
#define EC_GEN_CFG_I_IPV4_INDEX_MASK     0x0000001F
#define EC_GEN_CFG_I_IPV4_INDEX_SHIFT    0
/* IPv6 protocol index */
#define EC_GEN_CFG_I_IPV6_INDEX_MASK     0x000003E0
#define EC_GEN_CFG_I_IPV6_INDEX_SHIFT    5
/* TCP protocol index */
#define EC_GEN_CFG_I_TCP_INDEX_MASK      0x00007C00
#define EC_GEN_CFG_I_TCP_INDEX_SHIFT     10
/* UDP protocol index */
#define EC_GEN_CFG_I_UDP_INDEX_MASK      0x000F8000
#define EC_GEN_CFG_I_UDP_INDEX_SHIFT     15
/* MACsec with 8 bytes SecTAG */
#define EC_GEN_CFG_I_MACSEC_8_INDEX_MASK 0x01F00000
#define EC_GEN_CFG_I_MACSEC_8_INDEX_SHIFT 20
/* MACsec with 16 bytes SecTAG */
#define EC_GEN_CFG_I_MACSEC_16_INDEX_MASK 0x3E000000
#define EC_GEN_CFG_I_MACSEC_16_INDEX_SHIFT 25

/**** cfg_i_ext register ****/
/* FcoE protocol index */
#define EC_GEN_CFG_I_EXT_FCOE_INDEX_MASK 0x0000001F
#define EC_GEN_CFG_I_EXT_FCOE_INDEX_SHIFT 0
/* RoCE protocol index */
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L3_1_MASK 0x000003E0
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L3_1_SHIFT 5
/* RoCE protocol index */
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L3_2_MASK 0x00007C00
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L3_2_SHIFT 10
/* RoCE protocol index */
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L4_MASK 0x000F8000
#define EC_GEN_CFG_I_EXT_ROCE_INDEX_L4_SHIFT 15

/**** en_ext register ****/
/* Enable Usage of Ethernet port memories for testing */
#define EC_GEN_EN_EXT_MEM_FOR_TEST_MASK  0x0000000F
#define EC_GEN_EN_EXT_MEM_FOR_TEST_SHIFT 0
#define EC_GEN_EN_EXT_MEM_FOR_TEST_VAL_EN	\
	(0xa << EC_GEN_EN_EXT_MEM_FOR_TEST_SHIFT)
#define EC_GEN_EN_EXT_MEM_FOR_TEST_VAL_DIS	\
	(0x0 << EC_GEN_EN_EXT_MEM_FOR_TEST_SHIFT)
/* Enable MAC loop back (Rx --> Tx, after MAC layer) for 802 */
#define EC_GEN_EN_EXT_MAC_LB             (1 << 4)
/* CRC forward value for the MAC Tx when working in loopback mod ... */
#define EC_GEN_EN_EXT_MAC_LB_CRC_FWD     (1 << 5)
/* Ready signal configuration when in loopback mode:00 - Ready f ... */
#define EC_GEN_EN_EXT_MAC_LB_READY_CFG_MASK 0x000000C0
#define EC_GEN_EN_EXT_MAC_LB_READY_CFG_SHIFT 6
/* Bypass the PTH completion update. */
#define EC_GEN_EN_EXT_PTH_COMPLETION_BYPASS (1 << 16)
/* Selection between the 1G and 10G MAC:
0 - 1G
1 - 10G */
#define EC_GEN_EN_EXT_PTH_1_10_SEL       (1 << 17)
/* avoid timestamping every pkt in 1G */
#define EC_GEN_EN_EXT_PTH_CFG_1G_TIMESTAMP_OPT (1 << 18)
/* Selection between descriptor caching options (WORD selection) ... */
#define EC_GEN_EN_EXT_CACHE_WORD_SPLIT   (1 << 20)

/**** gen register ****/
/* Enable swap of input byte order */
#define EC_MAC_GEN_SWAP_IN_BYTE          (1 << 0)

/**** min_pkt register ****/
/* Minimum packet size  */
#define EC_MAC_MIN_PKT_SIZE_MASK         0x000FFFFF
#define EC_MAC_MIN_PKT_SIZE_SHIFT        0

/**** max_pkt register ****/
/* Maximum packet size  */
#define EC_MAC_MAX_PKT_SIZE_MASK         0x000FFFFF
#define EC_MAC_MAX_PKT_SIZE_SHIFT        0

/**** cfg_1 register ****/
/* Drop packet at the ingress0 - Packets are not dropped at the  ... */
#define EC_RXF_CFG_1_DROP_AT_INGRESS     (1 << 0)
/* Accept packet criteria at start of packet indication */
#define EC_RXF_CFG_1_SOP_ACCEPT          (1 << 1)
/* Select the arbiter between Rx packets and Tx packets (packets ... */
#define EC_RXF_CFG_1_ARB_SEL             (1 << 2)
/* Arbiter priority when strict priority is selected in arb_sel0 ... */
#define EC_RXF_CFG_1_ARB_P               (1 << 3)
/* Force loopback operation */
#define EC_RXF_CFG_1_FORCE_LB            (1 << 4)
/* Forwarding selection between Rx path and/or packet analyzer */
#define EC_RXF_CFG_1_FWD_SEL_MASK        0x00000300
#define EC_RXF_CFG_1_FWD_SEL_SHIFT       8

/**** cfg_2 register ****/
/* FIFO USED threshold for accepting new packets, low threshold  ... */
#define EC_RXF_CFG_2_FIFO_USED_TH_L_MASK 0x0000FFFF
#define EC_RXF_CFG_2_FIFO_USED_TH_L_SHIFT 0
/* FIFO USED threshold for accepting new packets, high threshold ... */
#define EC_RXF_CFG_2_FIFO_USED_TH_H_MASK 0xFFFF0000
#define EC_RXF_CFG_2_FIFO_USED_TH_H_SHIFT 16

/**** rd_fifo register ****/
/* Minimum number of entries in the data FIFO to start reading p ... */
#define EC_RXF_RD_FIFO_TH_DATA_MASK      0x0000FFFF
#define EC_RXF_RD_FIFO_TH_DATA_SHIFT     0
/* Enable cut through operation */
#define EC_RXF_RD_FIFO_EN_CUT_TH         (1 << 16)

/**** wr_fifo register ****/

#define EC_RXF_WR_FIFO_TH_DATA_MASK      0x0000FFFF
#define EC_RXF_WR_FIFO_TH_DATA_SHIFT     0

#define EC_RXF_WR_FIFO_TH_INFO_MASK      0xFFFF0000
#define EC_RXF_WR_FIFO_TH_INFO_SHIFT     16

/**** lb_fifo register ****/

#define EC_RXF_LB_FIFO_TH_DATA_MASK      0x0000FFFF
#define EC_RXF_LB_FIFO_TH_DATA_SHIFT     0

#define EC_RXF_LB_FIFO_TH_INFO_MASK      0xFFFF0000
#define EC_RXF_LB_FIFO_TH_INFO_SHIFT     16

/**** cfg_lb register ****/
/* FIFO USED threshold for accepting new packets */
#define EC_RXF_CFG_LB_FIFO_USED_TH_INT_MASK 0x0000FFFF
#define EC_RXF_CFG_LB_FIFO_USED_TH_INT_SHIFT 0
/* FIFO USED threshold for generating ready for the Tx path */
#define EC_RXF_CFG_LB_FIFO_USED_TH_EXT_MASK 0xFFFF0000
#define EC_RXF_CFG_LB_FIFO_USED_TH_EXT_SHIFT 16

/**** out_drop register ****/

#define EC_RXF_OUT_DROP_MAC_ERR          (1 << 0)

#define EC_RXF_OUT_DROP_MAC_COL          (1 << 1)

#define EC_RXF_OUT_DROP_MAC_DEC          (1 << 2)

#define EC_RXF_OUT_DROP_MAC_LEN          (1 << 3)

#define EC_RXF_OUT_DROP_MAC_PHY          (1 << 4)

#define EC_RXF_OUT_DROP_MAC_FIFO         (1 << 5)

#define EC_RXF_OUT_DROP_MAC_FCS          (1 << 6)

#define EC_RXF_OUT_DROP_MAC_ETYPE        (1 << 7)

#define EC_RXF_OUT_DROP_EC_LEN           (1 << 8)

#define EC_RXF_OUT_DROP_EC_FIFO          (1 << 9)

/**** parse_cfg register ****/
/* MAX number of beats for packet parsing */
#define EC_EPE_PARSE_CFG_MAX_BEATS_MASK  0x000000FF
#define EC_EPE_PARSE_CFG_MAX_BEATS_SHIFT 0
/* MAX number of parsing iterations for packet parsing */
#define EC_EPE_PARSE_CFG_MAX_ITER_MASK   0x0000FF00
#define EC_EPE_PARSE_CFG_MAX_ITER_SHIFT  8

/**** act_table_addr register ****/
/* Address for accessing the table */
#define EC_EPE_ACT_TABLE_ADDR_VAL_MASK   0x0000001F
#define EC_EPE_ACT_TABLE_ADDR_VAL_SHIFT  0

/**** act_table_data_1 register ****/
/* Table data[5:0] - Offset to next protocol [bytes][6] - Next p ... */
#define EC_EPE_ACT_TABLE_DATA_1_VAL_MASK 0x03FFFFFF
#define EC_EPE_ACT_TABLE_DATA_1_VAL_SHIFT 0

/**** act_table_data_2 register ****/
/* Table Data [8:0] - Offset to data in the packet [bits][17:9]  ... */
#define EC_EPE_ACT_TABLE_DATA_2_VAL_MASK 0x1FFFFFFF
#define EC_EPE_ACT_TABLE_DATA_2_VAL_SHIFT 0

/**** act_table_data_3 register ****/
/* Table Data  [8:0] - Offset to data in the packet [bits] [17:9 ... */
#define EC_EPE_ACT_TABLE_DATA_3_VAL_MASK 0x1FFFFFFF
#define EC_EPE_ACT_TABLE_DATA_3_VAL_SHIFT 0

/**** act_table_data_4 register ****/
/* Table data[7:0] - Offset to header length location in the pac ... */
#define EC_EPE_ACT_TABLE_DATA_4_VAL_MASK 0x0FFFFFFF
#define EC_EPE_ACT_TABLE_DATA_4_VAL_SHIFT 0

/**** act_table_data_6 register ****/
/* Table data[0] - WR header length[10:1] - Write header length  ... */
#define EC_EPE_ACT_TABLE_DATA_6_VAL_MASK 0x007FFFFF
#define EC_EPE_ACT_TABLE_DATA_6_VAL_SHIFT 0

/**** res_in register ****/
/* Selector for input parse_en0 - Input vector1 - Default value  ... */
#define EC_EPE_RES_IN_SEL_PARSE_EN       (1 << 0)
/* Selector for input protocol_index 0 - Input vector 1 - Defaul ... */
#define EC_EPE_RES_IN_SEL_PROT_INDEX     (1 << 1)
/* Selector for input hdr_offset 0 - Input vector 1 - Default va ... */
#define EC_EPE_RES_IN_SEL_HDR_OFFSET     (1 << 2)

/**** p1 register ****/
/* Location of the input protocol index in the parser result vec ... */
#define EC_EPE_RES_P1_IN_PROT_INDEX_MASK 0x000003FF
#define EC_EPE_RES_P1_IN_PROT_INDEX_SHIFT 0

/**** p2 register ****/
/* Location of the input offset in the parser result vector */
#define EC_EPE_RES_P2_IN_OFFSET_MASK     0x000003FF
#define EC_EPE_RES_P2_IN_OFFSET_SHIFT    0

/**** p3 register ****/
/* Location of the input parse enable in the parser result vecto ... */
#define EC_EPE_RES_P3_IN_PARSE_EN_MASK   0x000003FF
#define EC_EPE_RES_P3_IN_PARSE_EN_SHIFT  0

/**** p4 register ****/
/* Location of the control bits in the parser result vector */
#define EC_EPE_RES_P4_CTRL_BITS_MASK     0x000003FF
#define EC_EPE_RES_P4_CTRL_BITS_SHIFT    0

/**** p5 register ****/
/* Location of the MAC DA in the parser result vector */
#define EC_EPE_RES_P5_DA_MASK            0x000003FF
#define EC_EPE_RES_P5_DA_SHIFT           0

/**** p6 register ****/
/* Location of the MAC SA in the parser result vector */
#define EC_EPE_RES_P6_SA_MASK            0x000003FF
#define EC_EPE_RES_P6_SA_SHIFT           0

/**** p7 register ****/
/* Location of the first VLAN in the parser result vector */
#define EC_EPE_RES_P7_VLAN_1_MASK        0x000003FF
#define EC_EPE_RES_P7_VLAN_1_SHIFT       0

/**** p8 register ****/
/* Location of the second VLAN in the parser result vector */
#define EC_EPE_RES_P8_VLAN_2_MASK        0x000003FF
#define EC_EPE_RES_P8_VLAN_2_SHIFT       0

/**** p9 register ****/
/* Location of the L3 protocol index in the parser result vector ... */
#define EC_EPE_RES_P9_L3_PROT_INDEX_MASK 0x000003FF
#define EC_EPE_RES_P9_L3_PROT_INDEX_SHIFT 0

/**** p10 register ****/
/* Location of the L3 offset in the parser result vector */
#define EC_EPE_RES_P10_L3_OFFSET_MASK    0x000003FF
#define EC_EPE_RES_P10_L3_OFFSET_SHIFT   0

/**** p11 register ****/
/* Location of the L3 SIP in the parser result vector */
#define EC_EPE_RES_P11_L3_SIP_MASK       0x000003FF
#define EC_EPE_RES_P11_L3_SIP_SHIFT      0

/**** p12 register ****/
/* Location of the L3 DIP in the parser result vector */
#define EC_EPE_RES_P12_L3_DIP_MASK       0x000003FF
#define EC_EPE_RES_P12_L3_DIP_SHIFT      0

/**** p13 register ****/
/* Location of the L3 priority in the parser result vector */
#define EC_EPE_RES_P13_L3_PRIORITY_MASK  0x000003FF
#define EC_EPE_RES_P13_L3_PRIORITY_SHIFT 0

/**** p14 register ****/
/* Location of the L3 header length in the parser result vector */
#define EC_EPE_RES_P14_L3_HDR_LEN_MASK   0x000003FF
#define EC_EPE_RES_P14_L3_HDR_LEN_SHIFT  0

/**** p15 register ****/
/* Location of the L4 protocol index in the parser result vector ... */
#define EC_EPE_RES_P15_L4_PROT_INDEX_MASK 0x000003FF
#define EC_EPE_RES_P15_L4_PROT_INDEX_SHIFT 0

/**** p16 register ****/
/* Location of the L4 source port in the parser result vector */
#define EC_EPE_RES_P16_L4_SRC_PORT_MASK  0x000003FF
#define EC_EPE_RES_P16_L4_SRC_PORT_SHIFT 0

/**** p17 register ****/
/* Location of the L4 destination port in the parser result vect ... */
#define EC_EPE_RES_P17_L4_DST_PORT_MASK  0x000003FF
#define EC_EPE_RES_P17_L4_DST_PORT_SHIFT 0

/**** p18 register ****/
/* Location of the L4 offset in the parser result vector */
#define EC_EPE_RES_P18_L4_OFFSET_MASK    0x000003FF
#define EC_EPE_RES_P18_L4_OFFSET_SHIFT   0

/**** p19 register ****/
/* Location of the Ether type in the parser result vector when w ... */
#define EC_EPE_RES_P19_WOL_ETYPE_MASK    0x000003FF
#define EC_EPE_RES_P19_WOL_ETYPE_SHIFT   0

/**** p20 register ****/
/* Location of the RoCE QP number field in the parser result vec ... */
#define EC_EPE_RES_P20_ROCE_QPN_MASK     0x000003FF
#define EC_EPE_RES_P20_ROCE_QPN_SHIFT    0

/**** hdr_len register ****/
/* Value for selecting table 1 */
#define EC_EPE_H_HDR_LEN_TABLE_1_MASK    0x000000FF
#define EC_EPE_H_HDR_LEN_TABLE_1_SHIFT   0
/* Value for selecting table 2 */
#define EC_EPE_H_HDR_LEN_TABLE_2_MASK    0x00FF0000
#define EC_EPE_H_HDR_LEN_TABLE_2_SHIFT   16

/**** comp_data register ****/
/* Data 1 for comparison */
#define EC_EPE_P_COMP_DATA_DATA_1_MASK   0x0000FFFF
#define EC_EPE_P_COMP_DATA_DATA_1_SHIFT  0
/* Data 2 for comparison
[18:16] - Stage
[24:19] - Branch ID */
#define EC_EPE_P_COMP_DATA_DATA_2_MASK   0x01FF0000
#define EC_EPE_P_COMP_DATA_DATA_2_SHIFT  16

/**** comp_mask register ****/
/* Data 1 for comparison */
#define EC_EPE_P_COMP_MASK_DATA_1_MASK   0x0000FFFF
#define EC_EPE_P_COMP_MASK_DATA_1_SHIFT  0
/* Data 2 for comparison
[18:16] - Stage
[24:19] - Branch ID */
#define EC_EPE_P_COMP_MASK_DATA_2_MASK   0x01FF0000
#define EC_EPE_P_COMP_MASK_DATA_2_SHIFT  16

/**** comp_ctrl register ****/
/* Output result value */
#define EC_EPE_P_COMP_CTRL_RES_MASK      0x0000001F
#define EC_EPE_P_COMP_CTRL_RES_SHIFT     0
/* Compare command for the data_1 field00 - Compare01 - <=10 - > ... */
#define EC_EPE_P_COMP_CTRL_CMD_1_MASK    0x00030000
#define EC_EPE_P_COMP_CTRL_CMD_1_SHIFT   16
/* Compare command for the data_2 field 00 - Compare 01 - <= 10  ... */
#define EC_EPE_P_COMP_CTRL_CMD_2_MASK    0x000C0000
#define EC_EPE_P_COMP_CTRL_CMD_2_SHIFT   18
/* Entry is valid */
#define EC_EPE_P_COMP_CTRL_VALID         (1 << 31)

/**** prot_act register ****/
/* Drop indication for the selected protocol index */
#define EC_EPE_A_PROT_ACT_DROP           (1 << 0)
/* Mapping value Used when mapping the entire protocol index ran ... */
#define EC_EPE_A_PROT_ACT_MAP_MASK       0x00000F00
#define EC_EPE_A_PROT_ACT_MAP_SHIFT      8

/**** thash_cfg_1 register ****/
/* Hash function output selection:000 - [7:0]001 - [15:8]010 - [ ... */
#define EC_RFW_THASH_CFG_1_OUT_SEL_MASK  0x00000007
#define EC_RFW_THASH_CFG_1_OUT_SEL_SHIFT 0
/* Selects between hash functions00 - toeplitz01 - CRC-3210 - 0x ... */
#define EC_RFW_THASH_CFG_1_FUNC_SEL_MASK 0x00000300
#define EC_RFW_THASH_CFG_1_FUNC_SEL_SHIFT 8
/* Enable SIP/DIP swap if SIP<DIP */
#define EC_RFW_THASH_CFG_1_ENABLE_IP_SWAP (1 << 16)
/* Enable PORT swap if SPORT<DPORT */
#define EC_RFW_THASH_CFG_1_ENABLE_PORT_SWAP (1 << 17)

/**** mhash_cfg_1 register ****/
/* Hash function output selection:000 - [7:0]001 - [15:8]010 - [ ... */
#define EC_RFW_MHASH_CFG_1_OUT_SEL_MASK  0x00000007
#define EC_RFW_MHASH_CFG_1_OUT_SEL_SHIFT 0
/* Selects the input to the MAC hash function0 - DA1 - DA + SA ... */
#define EC_RFW_MHASH_CFG_1_INPUT_SEL     (1 << 4)
/* Selects between hash functions00 - toeplitz01 - CRC-3210 - 0x ... */
#define EC_RFW_MHASH_CFG_1_FUNC_SEL_MASK 0x00000300
#define EC_RFW_MHASH_CFG_1_FUNC_SEL_SHIFT 8

/**** hdr_split register ****/
/* Default header length for header split */
#define EC_RFW_HDR_SPLIT_DEF_LEN_MASK    0x0000FFFF
#define EC_RFW_HDR_SPLIT_DEF_LEN_SHIFT   0
/* Enable header split operation */
#define EC_RFW_HDR_SPLIT_EN              (1 << 16)

/**** meta_err register ****/
/* Mask for error 1 in the Rx descriptor */
#define EC_RFW_META_ERR_MASK_1_MASK      0x000003FF
#define EC_RFW_META_ERR_MASK_1_SHIFT     0
/* Mask for error 2 in the Rx descriptor */
#define EC_RFW_META_ERR_MASK_2_MASK      0x03FF0000
#define EC_RFW_META_ERR_MASK_2_SHIFT     16

/**** meta register ****/
/* Selection of the L3 offset source: 1 - Inner packet 0 - Outer ... */
#define EC_RFW_META_L3_LEN_SEL           (1 << 0)
/* Selection of the L3 offset source:1 - Inner packet0 - Outer p ... */
#define EC_RFW_META_L3_OFFSET_SEL        (1 << 1)
/* Selection of the l3 protocol index source: 1 - Inner packet 0 ... */
#define EC_RFW_META_L3_PROT_SEL          (1 << 2)
/* Selection of the l4 protocol index source:  1 - Inner packet  ... */
#define EC_RFW_META_L4_PROT_SEL          (1 << 3)
/* Selects how to calculate the L3 header length when L3 is IpPv ... */
#define EC_RFW_META_L3_LEN_CALC          (1 << 4)
/* Selection of the IPv4 fragment indication source:  1 - Inner  ... */
#define EC_RFW_META_FRAG_SEL             (1 << 5)
/* Selection of the L4 offset source:1 - Inner packet0 - Outer p ... */
#define EC_RFW_META_L4_OFFSET_SEL        (1 << 6)

/**** filter register ****/
/* Filter undetected MAC DA */
#define EC_RFW_FILTER_UNDET_MAC          (1 << 0)
/* Filter specific MAC DA based on MAC table output. */
#define EC_RFW_FILTER_DET_MAC            (1 << 1)
/* Filter all tagged. */
#define EC_RFW_FILTER_TAGGED             (1 << 2)
/* Filter all untagged. */
#define EC_RFW_FILTER_UNTAGGED           (1 << 3)
/* Filter all broadcast. */
#define EC_RFW_FILTER_BC                 (1 << 4)
/* Filter all multicast. */
#define EC_RFW_FILTER_MC                 (1 << 5)
/* Filter based on parsing output (used to drop selected protoco ... */
#define EC_RFW_FILTER_PARSE              (1 << 6)
/* Filter packet based on VLAN table output. */
#define EC_RFW_FILTER_VLAN_VID           (1 << 7)
/* Filter packet based on control table output. */
#define EC_RFW_FILTER_CTRL_TABLE         (1 << 8)
/* Filter packet based on protocol index action register. */
#define EC_RFW_FILTER_PROT_INDEX         (1 << 9)
/* Filter packet based on WoL decision */
#define EC_RFW_FILTER_WOL                (1 << 10)
/* Override filter decision and forward to default UDMA/queue;dr ... */
#define EC_RFW_FILTER_OR_UNDET_MAC       (1 << 16)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_DET_MAC         (1 << 17)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_TAGGED          (1 << 18)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_UNTAGGED        (1 << 19)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_BC              (1 << 20)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_MC              (1 << 21)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_PARSE           (1 << 22)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_VLAN_VID        (1 << 23)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_CTRL_TABLE      (1 << 24)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_PROT_INDEX      (1 << 25)
/* Override filter decision and forward to default UDMA/queue;Dr ... */
#define EC_RFW_FILTER_OR_WOL             (1 << 26)

/**** thash_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_THASH_TABLE_ADDR_VAL_MASK 0x000000FF
#define EC_RFW_THASH_TABLE_ADDR_VAL_SHIFT 0

/**** thash_table_data register ****/
/* Table data (valid only after configuring the table address re ... */
#define EC_RFW_THASH_TABLE_DATA_VAL_MASK 0x00003FFF
#define EC_RFW_THASH_TABLE_DATA_VAL_SHIFT 0

/**** mhash_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_MHASH_TABLE_ADDR_VAL_MASK 0x000000FF
#define EC_RFW_MHASH_TABLE_ADDR_VAL_SHIFT 0

/**** mhash_table_data register ****/
/* Table data (valid only after configuring the table address re ... */
#define EC_RFW_MHASH_TABLE_DATA_VAL_MASK 0x0000003F
#define EC_RFW_MHASH_TABLE_DATA_VAL_SHIFT 0

/**** vid_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_VID_TABLE_ADDR_VAL_MASK   0x00000FFF
#define EC_RFW_VID_TABLE_ADDR_VAL_SHIFT  0

/**** vid_table_data register ****/
/* Table data (valid only after configuring the table address re ... */
#define EC_RFW_VID_TABLE_DATA_VAL_MASK   0x0000003F
#define EC_RFW_VID_TABLE_DATA_VAL_SHIFT  0

/**** pbits_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_PBITS_TABLE_ADDR_VAL_MASK 0x00000007
#define EC_RFW_PBITS_TABLE_ADDR_VAL_SHIFT 0

/**** pbits_table_data register ****/
/* VLAN P-bits to internal priority mapping */
#define EC_RFW_PBITS_TABLE_DATA_VAL_MASK 0x00000007
#define EC_RFW_PBITS_TABLE_DATA_VAL_SHIFT 0

/**** dscp_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_DSCP_TABLE_ADDR_VAL_MASK  0x000000FF
#define EC_RFW_DSCP_TABLE_ADDR_VAL_SHIFT 0

/**** dscp_table_data register ****/
/* IPv4 DSCP to internal priority mapping */
#define EC_RFW_DSCP_TABLE_DATA_VAL_MASK  0x00000007
#define EC_RFW_DSCP_TABLE_DATA_VAL_SHIFT 0

/**** tc_table_addr register ****/
/* Address for accessing the table */
#define EC_RFW_TC_TABLE_ADDR_VAL_MASK    0x000000FF
#define EC_RFW_TC_TABLE_ADDR_VAL_SHIFT   0

/**** tc_table_data register ****/
/* IPv6 TC to internal priority mapping */
#define EC_RFW_TC_TABLE_DATA_VAL_MASK    0x00000007
#define EC_RFW_TC_TABLE_DATA_VAL_SHIFT   0

/**** ctrl_table_addr register ****/
/* Address for accessing the table[0] - VLAN table control out[1 ... */
#define EC_RFW_CTRL_TABLE_ADDR_VAL_MASK  0x000007FF
#define EC_RFW_CTRL_TABLE_ADDR_VAL_SHIFT 0

/**** ctrl_table_data register ****/
/* Control table output for selecting the forwarding MUXs[3:0] - ... */
#define EC_RFW_CTRL_TABLE_DATA_VAL_MASK  0x000FFFFF
#define EC_RFW_CTRL_TABLE_DATA_VAL_SHIFT 0

/**** out_cfg register ****/
/* Number of MetaData at the end of the packet1 - One MetaData b ... */
#define EC_RFW_OUT_CFG_META_CNT_MASK     0x00000003
#define EC_RFW_OUT_CFG_META_CNT_SHIFT    0
/* Enable packet drop */
#define EC_RFW_OUT_CFG_DROP_EN           (1 << 2)
/* Swap output byte order */
#define EC_RFW_OUT_CFG_SWAP_OUT_BYTE     (1 << 3)
/* Enable the insertion of the MACsec decoding result into the M ... */
#define EC_RFW_OUT_CFG_EN_MACSEC_DEC     (1 << 4)
/* Sample time of the time stamp:0 - SOP (for 10G MAC)1 - EOP (f ... */
#define EC_RFW_OUT_CFG_TIMESTAMP_SAMPLE  (1 << 5)
/* Determines which queue to write into the packet header0 - Ori ... */
#define EC_RFW_OUT_CFG_QUEUE_OR_SEL   (1 << 6)
/* Determines the logic of the drop indication:0 - Sample the dr ... */
#define EC_RFW_OUT_CFG_DROP_LOGIC_SEL (1 << 7)
/* Determines the logic of the drop indication:0 - Sample the dr ... */
#define EC_RFW_OUT_CFG_PKT_TYPE_DEF   (1 << 8)

/**** fsm_table_addr register ****/
/* Address for accessing the table :[2:0] - Outer header control ... */
#define EC_RFW_FSM_TABLE_ADDR_VAL_MASK   0x0000007F
#define EC_RFW_FSM_TABLE_ADDR_VAL_SHIFT  0

/**** fsm_table_data register ****/
/* Flow steering mechanism output selectors:[1:0] - Input select ... */
#define EC_RFW_FSM_TABLE_DATA_VAL_MASK   0x00000007
#define EC_RFW_FSM_TABLE_DATA_VAL_SHIFT  0

/**** ctrl_sel register ****/
/* Packet type (UC/MC/BC) for the control table */
#define EC_RFW_CTRL_SEL_PKT_TYPE         (1 << 0)
/* L3 protocol index for the control table */
#define EC_RFW_CTRL_SEL_L3_PROTOCOL      (1 << 1)
/* Selects the content and structure of the control table addres ... */
#define EC_RFW_CTRL_SEL_ADDR_MASK        0x0000000C
#define EC_RFW_CTRL_SEL_ADDR_SHIFT       2

/**** default_vlan register ****/
/* Default VLAN data, used for untagged packets */
#define EC_RFW_DEFAULT_VLAN_DATA_MASK    0x0000FFFF
#define EC_RFW_DEFAULT_VLAN_DATA_SHIFT   0

/**** default_hash register ****/
/* Default UDMA */
#define EC_RFW_DEFAULT_HASH_UDMA_MASK    0x0000000F
#define EC_RFW_DEFAULT_HASH_UDMA_SHIFT   0
/* Default queue */
#define EC_RFW_DEFAULT_HASH_QUEUE_MASK   0x00030000
#define EC_RFW_DEFAULT_HASH_QUEUE_SHIFT  16

/**** default_or register ****/
/* Default UDMA */
#define EC_RFW_DEFAULT_OR_UDMA_MASK      0x0000000F
#define EC_RFW_DEFAULT_OR_UDMA_SHIFT     0
/* Default queue */
#define EC_RFW_DEFAULT_OR_QUEUE_MASK     0x00030000
#define EC_RFW_DEFAULT_OR_QUEUE_SHIFT    16

/**** checksum register ****/
/* Check that the length in the UDP header matches the length in ... */
#define EC_RFW_CHECKSUM_UDP_LEN          (1 << 0)
/* Select the header that will be used for the checksum when a t ... */
#define EC_RFW_CHECKSUM_HDR_SEL          (1 << 1)
/* Enable L4 checksum when L3 fragmentation is detected */
#define EC_RFW_CHECKSUM_L4_FRAG_EN       (1 << 2)
/* L3 Checksum result selection for the Metadata descriptor0 - O ... */
#define EC_RFW_CHECKSUM_L3_CKS_SEL       (1 << 4)
/* L4 Checksum result selection for the Metadata descriptor0 - O ... */
#define EC_RFW_CHECKSUM_L4_CKS_SEL       (1 << 5)

/**** lro_cfg_1 register ****/
/* Select the header that will be used for the LRO offload engin ... */
#define EC_RFW_LRO_CFG_1_HDR_SEL         (1 << 0)
/* Select the L2 header that will be used for the LRO offload en ... */
#define EC_RFW_LRO_CFG_1_HDR_L2_SEL      (1 << 1)

/**** lro_check_ipv4 register ****/
/* Check version field. */
#define EC_RFW_LRO_CHECK_IPV4_VER        (1 << 0)
/* Check IHL field == 5. */
#define EC_RFW_LRO_CHECK_IPV4_IHL_0      (1 << 1)
/* Check IHL field >= 5. */
#define EC_RFW_LRO_CHECK_IPV4_IHL_1      (1 << 2)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_IPV4_IHL_2      (1 << 3)
/* Compare DSCP to previous packet. */
#define EC_RFW_LRO_CHECK_IPV4_DSCP       (1 << 4)
/* Check that Total length >= lro_ipv4_tlen_val. */
#define EC_RFW_LRO_CHECK_IPV4_TLEN       (1 << 5)
/* Compare to previous packet value +1. */
#define EC_RFW_LRO_CHECK_IPV4_ID         (1 << 6)
/* Compare to lro_ipv4_flags_val with lro_ipv4_flags_mask_0. */
#define EC_RFW_LRO_CHECK_IPV4_FLAGS_0    (1 << 7)
/* Compare to previous packet flags with lro_ipv4_flags_mask_1. */
#define EC_RFW_LRO_CHECK_IPV4_FLAGS_1    (1 << 8)
/* Verify that the fragment offset field is 0. */
#define EC_RFW_LRO_CHECK_IPV4_FRAG       (1 << 9)
/* Verify that the TTL value >0. */
#define EC_RFW_LRO_CHECK_IPV4_TTL_0      (1 << 10)
/* Compare TTL value to previous packet. */
#define EC_RFW_LRO_CHECK_IPV4_TTL_1      (1 << 11)
/* Compare to previous packet protocol field. */
#define EC_RFW_LRO_CHECK_IPV4_PROT_0     (1 << 12)
/* Verify that the protocol is TCP or UDP. */
#define EC_RFW_LRO_CHECK_IPV4_PROT_1     (1 << 13)
/* Verify that the check sum is correct. */
#define EC_RFW_LRO_CHECK_IPV4_CHECKSUM   (1 << 14)
/* Compare SIP to previous packet. */
#define EC_RFW_LRO_CHECK_IPV4_SIP        (1 << 15)
/* Compare DIP to previous packet. */
#define EC_RFW_LRO_CHECK_IPV4_DIP        (1 << 16)

/**** lro_ipv4 register ****/
/* Total length minimum value */
#define EC_RFW_LRO_IPV4_TLEN_VAL_MASK    0x0000FFFF
#define EC_RFW_LRO_IPV4_TLEN_VAL_SHIFT   0
/* Flags value  */
#define EC_RFW_LRO_IPV4_FLAGS_VAL_MASK   0x00070000
#define EC_RFW_LRO_IPV4_FLAGS_VAL_SHIFT  16
/* Flags mask */
#define EC_RFW_LRO_IPV4_FLAGS_MASK_0_MASK 0x00380000
#define EC_RFW_LRO_IPV4_FLAGS_MASK_0_SHIFT 19
/* Flags mask */
#define EC_RFW_LRO_IPV4_FLAGS_MASK_1_MASK 0x01C00000
#define EC_RFW_LRO_IPV4_FLAGS_MASK_1_SHIFT 22
/* Version value */
#define EC_RFW_LRO_IPV4_VER_MASK         0xF0000000
#define EC_RFW_LRO_IPV4_VER_SHIFT        28

/**** lro_check_ipv6 register ****/
/* Check version field */
#define EC_RFW_LRO_CHECK_IPV6_VER        (1 << 0)
/* Compare TC to previous packet. */
#define EC_RFW_LRO_CHECK_IPV6_TC         (1 << 1)
/* Compare flow label field to previous packet. */
#define EC_RFW_LRO_CHECK_IPV6_FLOW       (1 << 2)
/* Check that Total length >= lro_ipv6_pen_val. */
#define EC_RFW_LRO_CHECK_IPV6_PLEN       (1 << 3)
/* Compare to previous packet next header field. */
#define EC_RFW_LRO_CHECK_IPV6_NEXT_0     (1 << 4)
/* Verify that the next header is TCP or UDP. */
#define EC_RFW_LRO_CHECK_IPV6_NEXT_1     (1 << 5)
/* Verify that hop limit is >0. */
#define EC_RFW_LRO_CHECK_IPV6_HOP_0      (1 << 6)
/* Compare hop limit to previous packet. */
#define EC_RFW_LRO_CHECK_IPV6_HOP_1      (1 << 7)
/* Compare SIP to previous packet. */
#define EC_RFW_LRO_CHECK_IPV6_SIP        (1 << 8)
/* Compare DIP to previous packet. */
#define EC_RFW_LRO_CHECK_IPV6_DIP        (1 << 9)

/**** lro_ipv6 register ****/
/* Payload length minimum value */
#define EC_RFW_LRO_IPV6_PLEN_VAL_MASK    0x0000FFFF
#define EC_RFW_LRO_IPV6_PLEN_VAL_SHIFT   0
/* Version value */
#define EC_RFW_LRO_IPV6_VER_MASK         0x0F000000
#define EC_RFW_LRO_IPV6_VER_SHIFT        24

/**** lro_check_tcp register ****/
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_TCP_SRC_PORT    (1 << 0)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_TCP_DST_PORT    (1 << 1)
/* If (SYN == 1), don't check  */
#define EC_RFW_LRO_CHECK_TCP_SN          (1 << 2)
/* Check data offset field == 5. */
#define EC_RFW_LRO_CHECK_TCP_OFFSET_0    (1 << 3)
/* Check data offset field >= 5. */
#define EC_RFW_LRO_CHECK_TCP_OFFSET_1    (1 << 4)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_TCP_OFFSET_2    (1 << 5)
/* Compare reserved field to lro_tcp_res. */
#define EC_RFW_LRO_CHECK_TCP_RES         (1 << 6)
/* Compare to lro_tcp_ecn_val and lro_tcp_ecn_mask_0. */
#define EC_RFW_LRO_CHECK_TCP_ECN_0       (1 << 7)
/* Compare to previous packet ECN field with lro_tcp_ecn_mask_1 */
#define EC_RFW_LRO_CHECK_TCP_ECN_1       (1 << 8)
/* Compare to lro_tcp_ctrl_val and lro_tcp_ctrl_mask_0. */
#define EC_RFW_LRO_CHECK_TCP_CTRL_0      (1 << 9)
/* Compare to previous packet ECN field with lro_tcp_ctrl_mask_1 */
#define EC_RFW_LRO_CHECK_TCP_CTRL_1      (1 << 10)
/* Verify that check sum is correct. */
#define EC_RFW_LRO_CHECK_TCP_CHECKSUM    (1 << 11)

/**** lro_tcp register ****/
/* Reserved field default value */
#define EC_RFW_LRO_TCP_RES_MASK          0x00000007
#define EC_RFW_LRO_TCP_RES_SHIFT         0
/* ECN field value */
#define EC_RFW_LRO_TCP_ECN_VAL_MASK      0x00000038
#define EC_RFW_LRO_TCP_ECN_VAL_SHIFT     3
/* ECN field mask */
#define EC_RFW_LRO_TCP_ECN_MASK_0_MASK   0x000001C0
#define EC_RFW_LRO_TCP_ECN_MASK_0_SHIFT  6
/* ECN field mask */
#define EC_RFW_LRO_TCP_ECN_MASK_1_MASK   0x00000E00
#define EC_RFW_LRO_TCP_ECN_MASK_1_SHIFT  9
/* Control field value */
#define EC_RFW_LRO_TCP_CTRL_VAL_MASK     0x0003F000
#define EC_RFW_LRO_TCP_CTRL_VAL_SHIFT    12
/* Control field mask */
#define EC_RFW_LRO_TCP_CTRL_MASK_0_MASK  0x00FC0000
#define EC_RFW_LRO_TCP_CTRL_MASK_0_SHIFT 18
/* Control field mask */
#define EC_RFW_LRO_TCP_CTRL_MASK_1_MASK  0x3F000000
#define EC_RFW_LRO_TCP_CTRL_MASK_1_SHIFT 24

/**** lro_check_udp register ****/
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_UDP_SRC_PORT    (1 << 0)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_UDP_DST_PORT    (1 << 1)
/* Verify that check sum is correct. */
#define EC_RFW_LRO_CHECK_UDP_CHECKSUM    (1 << 2)

/**** lro_check_l2 register ****/
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_MAC_DA       (1 << 0)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_MAC_SA       (1 << 1)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_1_EXIST (1 << 2)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_1_VID   (1 << 3)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_1_CFI   (1 << 4)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_1_PBITS (1 << 5)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_2_EXIST (1 << 6)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_2_VID   (1 << 7)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_2_CFI   (1 << 8)
/* Compare to previous packet. */
#define EC_RFW_LRO_CHECK_L2_VLAN_2_PBITS (1 << 9)
/* Verify that the FCS is correct. */
#define EC_RFW_LRO_CHECK_L2_FCS          (1 << 10)

/**** lro_check_gen register ****/
/* Compare to previous packet */
#define EC_RFW_LRO_CHECK_GEN_UDMA        (1 << 0)
/* Compare to previous packet */
#define EC_RFW_LRO_CHECK_GEN_QUEUE       (1 << 1)

/**** lro_store register ****/
/* Store packet information if protocol match. */
#define EC_RFW_LRO_STORE_IPV4            (1 << 0)
/* Store packet information if protocol match. */
#define EC_RFW_LRO_STORE_IPV6            (1 << 1)
/* Store packet information if protocol match. */
#define EC_RFW_LRO_STORE_TCP             (1 << 2)
/* Store packet information if protocol match. */
#define EC_RFW_LRO_STORE_UDP             (1 << 3)
/* Store packet if IPv4 flags match the register value with mask */
#define EC_RFW_LRO_STORE_IPV4_FLAGS_VAL_MASK 0x00000070
#define EC_RFW_LRO_STORE_IPV4_FLAGS_VAL_SHIFT 4
/* Mask for IPv4 flags */
#define EC_RFW_LRO_STORE_IPV4_FLAGS_MASK_MASK 0x00000380
#define EC_RFW_LRO_STORE_IPV4_FLAGS_MASK_SHIFT 7
/* Store packet if TCP control and ECN match the register value  ... */
#define EC_RFW_LRO_STORE_TCP_CTRL_VAL_MASK 0x0007FC00
#define EC_RFW_LRO_STORE_TCP_CTRL_VAL_SHIFT 10
/* Mask for TCP control */
#define EC_RFW_LRO_STORE_TCP_CTRL_MASK_MASK 0x0FF80000
#define EC_RFW_LRO_STORE_TCP_CTRL_MASK_SHIFT 19

/**** vid_table_def register ****/
/* Table default data (valid only after configuring the table ad ... */
#define EC_RFW_VID_TABLE_DEF_VAL_MASK    0x0000003F
#define EC_RFW_VID_TABLE_DEF_VAL_SHIFT   0
/* Default data selection
0 - Default value
1 - Table data out */
#define EC_RFW_VID_TABLE_DEF_SEL         (1 << 6)

/**** ctrl_table_def register ****/
/* Control table output for selecting the forwarding MUXs [3:0]  ... */
#define EC_RFW_CTRL_TABLE_DEF_VAL_MASK   0x000FFFFF
#define EC_RFW_CTRL_TABLE_DEF_VAL_SHIFT  0
/* Default data selection 0 - Default value 1 - Table data out ... */
#define EC_RFW_CTRL_TABLE_DEF_SEL        (1 << 20)

/**** cfg_a_0 register ****/
/* Selection of the L3 checksum result in the Metadata00 - L3 ch ... */
#define EC_RFW_CFG_A_0_META_L3_CHK_RES_SEL_MASK 0x00000003
#define EC_RFW_CFG_A_0_META_L3_CHK_RES_SEL_SHIFT 0
/* Selection of the L4 checksum result in the Metadata0 - L4 che ... */
#define EC_RFW_CFG_A_0_META_L4_CHK_RES_SEL (1 << 2)
/* Selection of the LRO_context_value result in the Metadata0 -  ... */
#define EC_RFW_CFG_A_0_LRO_CONTEXT_SEL   (1 << 4)

/**** thash_cfg_3 register ****/
/* Enable Hash value for RoCE packets in outer packet. */
#define EC_RFW_THASH_CFG_3_ENABLE_OUTER_ROCE (1 << 0)
/* Enable Hash value for RoCE packets in inner packet. */
#define EC_RFW_THASH_CFG_3_ENABLE_INNER_ROCE (1 << 1)
/* Enable Hash value for FcoE packets in outer packet. */
#define EC_RFW_THASH_CFG_3_ENABLE_OUTER_FCOE (1 << 2)
/* Enable Hash value for FcoE packets in inner packet. */
#define EC_RFW_THASH_CFG_3_ENABLE_INNER_FCOE (1 << 3)

/**** thash_mask_outer_ipv6 register ****/
/* IPv6 source IP address */
#define EC_RFW_THASH_MASK_OUTER_IPV6_SRC_MASK 0x0000FFFF
#define EC_RFW_THASH_MASK_OUTER_IPV6_SRC_SHIFT 0
/* IPv6 destination IP address */
#define EC_RFW_THASH_MASK_OUTER_IPV6_DST_MASK 0xFFFF0000
#define EC_RFW_THASH_MASK_OUTER_IPV6_DST_SHIFT 16

/**** thash_mask_outer register ****/
/* IPv4 source IP address */
#define EC_RFW_THASH_MASK_OUTER_IPV4_SRC_MASK 0x0000000F
#define EC_RFW_THASH_MASK_OUTER_IPV4_SRC_SHIFT 0
/* IPv4 destination IP address */
#define EC_RFW_THASH_MASK_OUTER_IPV4_DST_MASK 0x000000F0
#define EC_RFW_THASH_MASK_OUTER_IPV4_DST_SHIFT 4
/* TCP source port */
#define EC_RFW_THASH_MASK_OUTER_TCP_SRC_PORT_MASK 0x00000300
#define EC_RFW_THASH_MASK_OUTER_TCP_SRC_PORT_SHIFT 8
/* TCP destination port */
#define EC_RFW_THASH_MASK_OUTER_TCP_DST_PORT_MASK 0x00000C00
#define EC_RFW_THASH_MASK_OUTER_TCP_DST_PORT_SHIFT 10
/* UDP source port */
#define EC_RFW_THASH_MASK_OUTER_UDP_SRC_PORT_MASK 0x00003000
#define EC_RFW_THASH_MASK_OUTER_UDP_SRC_PORT_SHIFT 12
/* UDP destination port */
#define EC_RFW_THASH_MASK_OUTER_UDP_DST_PORT_MASK 0x0000C000
#define EC_RFW_THASH_MASK_OUTER_UDP_DST_PORT_SHIFT 14

/**** thash_mask_inner_ipv6 register ****/
/* IPv6 source IP address */
#define EC_RFW_THASH_MASK_INNER_IPV6_SRC_MASK 0x0000FFFF
#define EC_RFW_THASH_MASK_INNER_IPV6_SRC_SHIFT 0
/* IPv6 destination IP address */
#define EC_RFW_THASH_MASK_INNER_IPV6_DST_MASK 0xFFFF0000
#define EC_RFW_THASH_MASK_INNER_IPV6_DST_SHIFT 16

/**** thash_mask_inner register ****/
/* IPv4 source IP address */
#define EC_RFW_THASH_MASK_INNER_IPV4_SRC_MASK 0x0000000F
#define EC_RFW_THASH_MASK_INNER_IPV4_SRC_SHIFT 0
/* IPv4 destination IP address */
#define EC_RFW_THASH_MASK_INNER_IPV4_DST_MASK 0x000000F0
#define EC_RFW_THASH_MASK_INNER_IPV4_DST_SHIFT 4
/* TCP source port */
#define EC_RFW_THASH_MASK_INNER_TCP_SRC_PORT_MASK 0x00000300
#define EC_RFW_THASH_MASK_INNER_TCP_SRC_PORT_SHIFT 8
/* TCP destination port */
#define EC_RFW_THASH_MASK_INNER_TCP_DST_PORT_MASK 0x00000C00
#define EC_RFW_THASH_MASK_INNER_TCP_DST_PORT_SHIFT 10
/* UDP source port */
#define EC_RFW_THASH_MASK_INNER_UDP_SRC_PORT_MASK 0x00003000
#define EC_RFW_THASH_MASK_INNER_UDP_SRC_PORT_SHIFT 12
/* UDP destination port */
#define EC_RFW_THASH_MASK_INNER_UDP_DST_PORT_MASK 0x0000C000
#define EC_RFW_THASH_MASK_INNER_UDP_DST_PORT_SHIFT 14

/**** def_cfg register ****/
/* Number of padding bytes to add at the beginning of each Ether ... */
#define EC_RFW_UDMA_DEF_CFG_RX_PAD_MASK  0x0000003F
#define EC_RFW_UDMA_DEF_CFG_RX_PAD_SHIFT 0

/**** queue register ****/
/* Mapping between priority and queue number */
#define EC_RFW_PRIORITY_QUEUE_MAP_MASK   0x00000003
#define EC_RFW_PRIORITY_QUEUE_MAP_SHIFT  0

/**** opt_1 register ****/
/* Default UDMA for forwarding  */
#define EC_RFW_DEFAULT_OPT_1_UDMA_MASK   0x0000000F
#define EC_RFW_DEFAULT_OPT_1_UDMA_SHIFT  0
/* Default priority for forwarding */
#define EC_RFW_DEFAULT_OPT_1_PRIORITY_MASK 0x00000700
#define EC_RFW_DEFAULT_OPT_1_PRIORITY_SHIFT 8
/* Default queue for forwarding */
#define EC_RFW_DEFAULT_OPT_1_QUEUE_MASK  0x00030000
#define EC_RFW_DEFAULT_OPT_1_QUEUE_SHIFT 16

/**** data_h register ****/
/* MAC address data  */
#define EC_FWD_MAC_DATA_H_VAL_MASK       0x0000FFFF
#define EC_FWD_MAC_DATA_H_VAL_SHIFT      0

/**** mask_h register ****/
/* MAC address mask  */
#define EC_FWD_MAC_MASK_H_VAL_MASK       0x0000FFFF
#define EC_FWD_MAC_MASK_H_VAL_SHIFT      0

/**** ctrl register ****/
/* Control value for Rx forwarding engine[0] - Drop indication[2 ... */
#define EC_FWD_MAC_CTRL_RX_VAL_MASK      0x000001FF
#define EC_FWD_MAC_CTRL_RX_VAL_SHIFT     0

/* Drop indication */
#define EC_FWD_MAC_CTRL_RX_VAL_DROP		(1 << 0)

/* control table command input */
#define EC_FWD_MAC_CTRL_RX_VAL_CTRL_CMD_MASK	0x00000006
#define EC_FWD_MAC_CTRL_RX_VAL_CTRL_CMD_SHIFT	1

/* UDMA selection */
#define EC_FWD_MAC_CTRL_RX_VAL_UDMA_MASK	0x000000078
#define EC_FWD_MAC_CTRL_RX_VAL_UDMA_SHIFT	3

/* queue number */
#define EC_FWD_MAC_CTRL_RX_VAL_QID_MASK		0x00000180
#define EC_FWD_MAC_CTRL_RX_VAL_QID_SHIFT	7

/* Entry is valid for Rx forwarding engine. */
#define EC_FWD_MAC_CTRL_RX_VALID         (1 << 15)
/* Control value for Tx forwarding engine */
#define EC_FWD_MAC_CTRL_TX_VAL_MASK      0x001F0000
#define EC_FWD_MAC_CTRL_TX_VAL_SHIFT     16
/* Entry is valid for Tx forwarding engine. */
#define EC_FWD_MAC_CTRL_TX_VALID         (1 << 31)

/**** uc register ****/
/* timer max value for waiting for a stream to be ready to accep ... */
#define EC_MSW_UC_TIMER_MASK             0x0000FFFF
#define EC_MSW_UC_TIMER_SHIFT            0
/* Drop packet if target queue in the UDMA is full */
#define EC_MSW_UC_Q_FULL_DROP_MASK       0x000F0000
#define EC_MSW_UC_Q_FULL_DROP_SHIFT      16
/* Drop packet if timer expires. */
#define EC_MSW_UC_TIMER_DROP_MASK        0x0F000000
#define EC_MSW_UC_TIMER_DROP_SHIFT       24

/**** mc register ****/
/* Timer max value for waiting for a stream to be ready to accep ... */
#define EC_MSW_MC_TIMER_MASK             0x0000FFFF
#define EC_MSW_MC_TIMER_SHIFT            0
/* Drop packet if target queue in UDMA is full. */
#define EC_MSW_MC_Q_FULL_DROP_MASK       0x000F0000
#define EC_MSW_MC_Q_FULL_DROP_SHIFT      16
/* Drop packet if timer expires. */
#define EC_MSW_MC_TIMER_DROP_MASK        0x0F000000
#define EC_MSW_MC_TIMER_DROP_SHIFT       24

/**** bc register ****/
/* Timer max value for waiting for a stream to be ready to accep ... */
#define EC_MSW_BC_TIMER_MASK             0x0000FFFF
#define EC_MSW_BC_TIMER_SHIFT            0
/* Drop packet if target queue in UDMA is full. */
#define EC_MSW_BC_Q_FULL_DROP_MASK       0x000F0000
#define EC_MSW_BC_Q_FULL_DROP_SHIFT      16
/* Drop packet if timer expires. */
#define EC_MSW_BC_TIMER_DROP_MASK        0x0F000000
#define EC_MSW_BC_TIMER_DROP_SHIFT       24

/**** in_cfg register ****/
/* Swap input bytes order */
#define EC_TSO_IN_CFG_SWAP_BYTES         (1 << 0)
/* Selects strict priority or round robin scheduling between GDM ... */
#define EC_TSO_IN_CFG_SEL_SP_RR          (1 << 1)
/* Selects scheduler numbering direction */
#define EC_TSO_IN_CFG_SEL_SCH_DIR        (1 << 2)
/* Minimum L2 packet size (not including FCS) */
#define EC_TSO_IN_CFG_L2_MIN_SIZE_MASK   0x00007F00
#define EC_TSO_IN_CFG_L2_MIN_SIZE_SHIFT  8
/* Swap input bytes order */
#define EC_TSO_IN_CFG_SP_INIT_VAL_MASK   0x000F0000
#define EC_TSO_IN_CFG_SP_INIT_VAL_SHIFT  16

/**** cache_table_addr register ****/
/* Address for accessing the table */
#define EC_TSO_CACHE_TABLE_ADDR_VAL_MASK 0x0000000F
#define EC_TSO_CACHE_TABLE_ADDR_VAL_SHIFT 0

/**** ctrl_first register ****/
/* Data to be written into the control BIS. */
#define EC_TSO_CTRL_FIRST_DATA_MASK      0x000001FF
#define EC_TSO_CTRL_FIRST_DATA_SHIFT     0
/* Mask for control bits */
#define EC_TSO_CTRL_FIRST_MASK_MASK      0x01FF0000
#define EC_TSO_CTRL_FIRST_MASK_SHIFT     16

/**** ctrl_middle register ****/
/* Data to be written into the control BIS. */
#define EC_TSO_CTRL_MIDDLE_DATA_MASK     0x000001FF
#define EC_TSO_CTRL_MIDDLE_DATA_SHIFT    0
/* Mask for the control bits */
#define EC_TSO_CTRL_MIDDLE_MASK_MASK     0x01FF0000
#define EC_TSO_CTRL_MIDDLE_MASK_SHIFT    16

/**** ctrl_last register ****/
/* Data to be written into the control BIS. */
#define EC_TSO_CTRL_LAST_DATA_MASK       0x000001FF
#define EC_TSO_CTRL_LAST_DATA_SHIFT      0
/* Mask for the control bits */
#define EC_TSO_CTRL_LAST_MASK_MASK       0x01FF0000
#define EC_TSO_CTRL_LAST_MASK_SHIFT      16

/**** cfg_add_0 register ****/
/* MSS selection option:0 - MSS value is selected using MSS_sel  ... */
#define EC_TSO_CFG_ADD_0_MSS_SEL         (1 << 0)

/**** cfg_tunnel register ****/
/* Enable TSO with tunnelling */
#define EC_TSO_CFG_TUNNEL_EN_TUNNEL_TSO  (1 << 0)
/* Enable outer UDP checksum update */
#define EC_TSO_CFG_TUNNEL_EN_UDP_CHKSUM  (1 << 8)
/* Enable outer UDP length update */
#define EC_TSO_CFG_TUNNEL_EN_UDP_LEN     (1 << 9)
/* Enable outer Ip6  length update */
#define EC_TSO_CFG_TUNNEL_EN_IPV6_PLEN   (1 << 10)
/* Enable outer IPv4 checksum update */
#define EC_TSO_CFG_TUNNEL_EN_IPV4_CHKSUM (1 << 11)
/* Enable outer IPv4 Identification update */
#define EC_TSO_CFG_TUNNEL_EN_IPV4_IDEN   (1 << 12)
/* Enable outer IPv4 length update */
#define EC_TSO_CFG_TUNNEL_EN_IPV4_TLEN   (1 << 13)

/**** mss register ****/
/* MSS value */
#define EC_TSO_SEL_MSS_VAL_MASK          0x000FFFFF
#define EC_TSO_SEL_MSS_VAL_SHIFT         0

/**** parse register ****/
/* Max number of bus beats for parsing */
#define EC_TPE_PARSE_MAX_BEATS_MASK      0x0000FFFF
#define EC_TPE_PARSE_MAX_BEATS_SHIFT     0

/**** vlan_data register ****/
/* UDMA default VLAN 1 data */
#define EC_TPM_UDMA_VLAN_DATA_DEF_1_MASK 0x0000FFFF
#define EC_TPM_UDMA_VLAN_DATA_DEF_1_SHIFT 0
/* UDMA default VLAN 2 data */
#define EC_TPM_UDMA_VLAN_DATA_DEF_2_MASK 0xFFFF0000
#define EC_TPM_UDMA_VLAN_DATA_DEF_2_SHIFT 16

/**** mac_sa_2 register ****/
/* MAC source address data [47:32] */
#define EC_TPM_UDMA_MAC_SA_2_H_VAL_MASK  0x0000FFFF
#define EC_TPM_UDMA_MAC_SA_2_H_VAL_SHIFT 0
/* Drop indication for MAC SA spoofing0 – Don't drop */
#define EC_TPM_UDMA_MAC_SA_2_DROP        (1 << 16)
/* Replace indication for MAC SA spoofing 0 - Don't replace */
#define EC_TPM_UDMA_MAC_SA_2_REPLACE     (1 << 17)

/**** etype register ****/
/* Ether type value  */
#define EC_TPM_SEL_ETYPE_VAL_MASK        0x0000FFFF
#define EC_TPM_SEL_ETYPE_VAL_SHIFT       0

/**** tx_wr_fifo register ****/
/* Max data beats that can be used in the Tx FIFO */
#define EC_TFW_TX_WR_FIFO_DATA_TH_MASK   0x0000FFFF
#define EC_TFW_TX_WR_FIFO_DATA_TH_SHIFT  0
/* Max packets that can be stored in the Tx FIFO */
#define EC_TFW_TX_WR_FIFO_INFO_TH_MASK   0xFFFF0000
#define EC_TFW_TX_WR_FIFO_INFO_TH_SHIFT  16

/**** tx_vid_table_addr register ****/
/* Address for accessing the table */
#define EC_TFW_TX_VID_TABLE_ADDR_VAL_MASK 0x00000FFF
#define EC_TFW_TX_VID_TABLE_ADDR_VAL_SHIFT 0

/**** tx_vid_table_data register ****/
/* Table data (valid only after configuring the table address re ... */
#define EC_TFW_TX_VID_TABLE_DATA_VAL_MASK 0x0000001F
#define EC_TFW_TX_VID_TABLE_DATA_VAL_SHIFT 0

/**** tx_rd_fifo register ****/
/* Read data threshold when cut through mode is enabled. */
#define EC_TFW_TX_RD_FIFO_READ_TH_MASK   0x0000FFFF
#define EC_TFW_TX_RD_FIFO_READ_TH_SHIFT  0
/* Enable cut through operation of the Tx FIFO. */
#define EC_TFW_TX_RD_FIFO_EN_CUT_THROUGH (1 << 16)

/**** tx_checksum register ****/
/* Enable L3 checksum insertion. */
#define EC_TFW_TX_CHECKSUM_L3_EN         (1 << 0)
/* Enable L4 checksum insertion. */
#define EC_TFW_TX_CHECKSUM_L4_EN         (1 << 1)
/* Enable L4 checksum when L3 fragmentation is detected. */
#define EC_TFW_TX_CHECKSUM_L4_FRAG_EN    (1 << 2)

/**** tx_gen register ****/
/* Force forward of all Tx packets to MAC. */
#define EC_TFW_TX_GEN_FWD_ALL_TO_MAC     (1 << 0)
/* Select the Packet generator as the source of Tx packets0 - Tx ... */
#define EC_TFW_TX_GEN_SELECT_PKT_GEN     (1 << 1)

/**** tx_spf register ****/
/* Select the VID for spoofing check:[0] - Packet VID[1] - Forwa ... */
#define EC_TFW_TX_SPF_VID_SEL            (1 << 0)

/**** data_fifo register ****/
/* FIFO used value (number of entries) */
#define EC_TFW_DATA_FIFO_USED_MASK       0x0000FFFF
#define EC_TFW_DATA_FIFO_USED_SHIFT      0
/* FIFO FULL status */
#define EC_TFW_DATA_FIFO_FULL            (1 << 16)
/* FIFO EMPTY status */
#define EC_TFW_DATA_FIFO_EMPTY           (1 << 17)

/**** ctrl_fifo register ****/
/* FIFO used value (number of entries) */
#define EC_TFW_CTRL_FIFO_USED_MASK       0x0000FFFF
#define EC_TFW_CTRL_FIFO_USED_SHIFT      0
/* FIFO FULL status */
#define EC_TFW_CTRL_FIFO_FULL            (1 << 16)
/* FIFO EMPTY status */
#define EC_TFW_CTRL_FIFO_EMPTY           (1 << 17)

/**** hdr_fifo register ****/
/* FIFO used value (number of entries) */
#define EC_TFW_HDR_FIFO_USED_MASK        0x0000FFFF
#define EC_TFW_HDR_FIFO_USED_SHIFT       0
/* FIFO FULL status */
#define EC_TFW_HDR_FIFO_FULL             (1 << 16)
/* FIFO EMPTY status */
#define EC_TFW_HDR_FIFO_EMPTY            (1 << 17)

/**** uc_udma register ****/
/* Default UDMA bitmap
(MSB represents physical port) */
#define EC_TFW_UDMA_UC_UDMA_DEF_MASK     0x0000001F
#define EC_TFW_UDMA_UC_UDMA_DEF_SHIFT    0

/**** mc_udma register ****/
/* Default UDMA bitmap (MSB represents physical port.) */
#define EC_TFW_UDMA_MC_UDMA_DEF_MASK     0x0000001F
#define EC_TFW_UDMA_MC_UDMA_DEF_SHIFT    0

/**** bc_udma register ****/
/* Default UDMA bitmap (MSB represents physical port.) */
#define EC_TFW_UDMA_BC_UDMA_DEF_MASK     0x0000001F
#define EC_TFW_UDMA_BC_UDMA_DEF_SHIFT    0

/**** spf_cmd register ****/
/* Command for the VLAN spoofing00 – Ignore  mismatch */
#define EC_TFW_UDMA_SPF_CMD_VID_MASK     0x00000003
#define EC_TFW_UDMA_SPF_CMD_VID_SHIFT    0
/* Command for VLAN spoofing 00 - Ignore  mismatch */
#define EC_TFW_UDMA_SPF_CMD_MAC_MASK     0x0000000C
#define EC_TFW_UDMA_SPF_CMD_MAC_SHIFT    2

/**** fwd_dec register ****/
/* Forwarding decision control:[0] – Enable internal switch */
#define EC_TFW_UDMA_FWD_DEC_CTRL_MASK    0x000003FF
#define EC_TFW_UDMA_FWD_DEC_CTRL_SHIFT   0

/**** tx_cfg register ****/
/* Swap output byte order */
#define EC_TMI_TX_CFG_SWAP_BYTES         (1 << 0)
/* Enable forwarding to the Rx data path. */
#define EC_TMI_TX_CFG_EN_FWD_TO_RX       (1 << 1)
/* Force forwarding all packets to the MAC. */
#define EC_TMI_TX_CFG_FORCE_FWD_MAC      (1 << 2)
/* Force forwarding all packets to the MAC. */
#define EC_TMI_TX_CFG_FORCE_FWD_RX       (1 << 3)
/* Force loop back operation */
#define EC_TMI_TX_CFG_FORCE_LB           (1 << 4)

/**** ec_pause register ****/
/* Mask of pause_on [7:0] */
#define EC_EFC_EC_PAUSE_MASK_MAC_MASK    0x000000FF
#define EC_EFC_EC_PAUSE_MASK_MAC_SHIFT   0
/* Mask of GPIO input [7:0] */
#define EC_EFC_EC_PAUSE_MASK_GPIO_MASK   0x0000FF00
#define EC_EFC_EC_PAUSE_MASK_GPIO_SHIFT  8

/**** ec_xoff register ****/
/* Mask 1 for XOFF [7:0]
Mask 1 for Almost Full indication, */
#define EC_EFC_EC_XOFF_MASK_1_MASK       0x000000FF
#define EC_EFC_EC_XOFF_MASK_1_SHIFT      0
/* Mask 2 for XOFF [7:0] Mask 2 for sampled Almost Full indicati ... */
#define EC_EFC_EC_XOFF_MASK_2_MASK       0x0000FF00
#define EC_EFC_EC_XOFF_MASK_2_SHIFT      8

/**** xon register ****/
/* Mask 1 for generating XON pulse, masking XOFF [0] */
#define EC_EFC_XON_MASK_1                (1 << 0)
/* Mask 2 for generating XON pulse, masking Almost Full indicati ... */
#define EC_EFC_XON_MASK_2                (1 << 1)

/**** gpio register ****/
/* Mask for generating GPIO output XOFF indication from XOFF[0] */
#define EC_EFC_GPIO_MASK_1               (1 << 0)

/**** rx_fifo_af register ****/
/* Threshold */
#define EC_EFC_RX_FIFO_AF_TH_MASK        0x0000FFFF
#define EC_EFC_RX_FIFO_AF_TH_SHIFT       0

/**** rx_fifo_hyst register ****/
/* Threshold low */
#define EC_EFC_RX_FIFO_HYST_TH_LOW_MASK  0x0000FFFF
#define EC_EFC_RX_FIFO_HYST_TH_LOW_SHIFT 0
/* Threshold high */
#define EC_EFC_RX_FIFO_HYST_TH_HIGH_MASK 0xFFFF0000
#define EC_EFC_RX_FIFO_HYST_TH_HIGH_SHIFT 16

/**** stat register ****/
/* 10G MAC PFC mode, input from the 10 MAC */
#define EC_EFC_STAT_PFC_MODE             (1 << 0)

/**** ec_pfc register ****/
/* Force PFC flow control */
#define EC_EFC_EC_PFC_FORCE_MASK         0x000000FF
#define EC_EFC_EC_PFC_FORCE_SHIFT        0

/**** q_pause_0 register ****/
/* [i] – Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_0_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_0_MASK_SHIFT  0

/**** q_pause_1 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_1_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_1_MASK_SHIFT  0

/**** q_pause_2 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_2_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_2_MASK_SHIFT  0

/**** q_pause_3 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_3_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_3_MASK_SHIFT  0

/**** q_pause_4 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_4_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_4_MASK_SHIFT  0

/**** q_pause_5 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_5_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_5_MASK_SHIFT  0

/**** q_pause_6 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_6_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_6_MASK_SHIFT  0

/**** q_pause_7 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_PAUSE_7_MASK_MASK   0x0000000F
#define EC_FC_UDMA_Q_PAUSE_7_MASK_SHIFT  0

/**** q_gpio_0 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_0_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_0_MASK_SHIFT   0

/**** q_gpio_1 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_1_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_1_MASK_SHIFT   0

/**** q_gpio_2 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_2_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_2_MASK_SHIFT   0

/**** q_gpio_3 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_3_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_3_MASK_SHIFT   0

/**** q_gpio_4 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_4_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_4_MASK_SHIFT   0

/**** q_gpio_5 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_5_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_5_MASK_SHIFT   0

/**** q_gpio_6 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_6_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_6_MASK_SHIFT   0

/**** q_gpio_7 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_GPIO_7_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_GPIO_7_MASK_SHIFT   0

/**** s_pause register ****/
/* Mask of pause_on [7:0] */
#define EC_FC_UDMA_S_PAUSE_MASK_MAC_MASK 0x000000FF
#define EC_FC_UDMA_S_PAUSE_MASK_MAC_SHIFT 0
/* Mask of GPIO input  [7:0] */
#define EC_FC_UDMA_S_PAUSE_MASK_GPIO_MASK 0x0000FF00
#define EC_FC_UDMA_S_PAUSE_MASK_GPIO_SHIFT 8

/**** q_xoff_0 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_0_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_0_MASK_SHIFT   0

/**** q_xoff_1 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_1_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_1_MASK_SHIFT   0

/**** q_xoff_2 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_2_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_2_MASK_SHIFT   0

/**** q_xoff_3 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_3_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_3_MASK_SHIFT   0

/**** q_xoff_4 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_4_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_4_MASK_SHIFT   0

/**** q_xoff_5 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_5_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_5_MASK_SHIFT   0

/**** q_xoff_6 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_6_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_6_MASK_SHIFT   0

/**** q_xoff_7 register ****/
/* [i] - Mask for Q[i] */
#define EC_FC_UDMA_Q_XOFF_7_MASK_MASK    0x0000000F
#define EC_FC_UDMA_Q_XOFF_7_MASK_SHIFT   0

/**** cfg_e register ****/
/* Use MAC Tx FIFO empty status for EEE control. */
#define EC_EEE_CFG_E_USE_MAC_TX_FIFO     (1 << 0)
/* Use MAC Rx FIFO empty status for EEE control. */
#define EC_EEE_CFG_E_USE_MAC_RX_FIFO     (1 << 1)
/* Use Ethernet controller Tx FIFO empty status for EEE control */
#define EC_EEE_CFG_E_USE_EC_TX_FIFO      (1 << 2)
/* Use Ethernet controller Rx FIFO empty status for EEE control */
#define EC_EEE_CFG_E_USE_EC_RX_FIFO      (1 << 3)
/* Enable Low power signalling. */
#define EC_EEE_CFG_E_ENABLE              (1 << 4)
/* Mask output to MAC.  */
#define EC_EEE_CFG_E_MASK_MAC_EEE        (1 << 8)
/* Mask output to stop MAC interface. */
#define EC_EEE_CFG_E_MASK_EC_TMI_STOP    (1 << 9)

/**** stat_eee register ****/
/* EEE state */
#define EC_EEE_STAT_EEE_STATE_MASK       0x0000000F
#define EC_EEE_STAT_EEE_STATE_SHIFT      0
/* EEE detected */
#define EC_EEE_STAT_EEE_DET              (1 << 4)

/**** p_parse_cfg register ****/
/* MAX number of beats for packet parsing */
#define EC_MSP_P_PARSE_CFG_MAX_BEATS_MASK 0x000000FF
#define EC_MSP_P_PARSE_CFG_MAX_BEATS_SHIFT 0
/* MAX number of parsing iterations for packet parsing */
#define EC_MSP_P_PARSE_CFG_MAX_ITER_MASK 0x0000FF00
#define EC_MSP_P_PARSE_CFG_MAX_ITER_SHIFT 8

/**** p_act_table_addr register ****/
/* Address for accessing the table */
#define EC_MSP_P_ACT_TABLE_ADDR_VAL_MASK 0x0000001F
#define EC_MSP_P_ACT_TABLE_ADDR_VAL_SHIFT 0

/**** p_act_table_data_1 register ****/
/* Table data[5:0] - Offset to next protocol [bytes] [6] - Next  ... */
#define EC_MSP_P_ACT_TABLE_DATA_1_VAL_MASK 0x03FFFFFF
#define EC_MSP_P_ACT_TABLE_DATA_1_VAL_SHIFT 0

/**** p_act_table_data_2 register ****/
/* Table data  [8:0] - Offset to data in the packet [bits][17:9] ... */
#define EC_MSP_P_ACT_TABLE_DATA_2_VAL_MASK 0x1FFFFFFF
#define EC_MSP_P_ACT_TABLE_DATA_2_VAL_SHIFT 0

/**** p_act_table_data_3 register ****/
/* Table data   [8:0] - Offset to data in the packet [bits]  [17 ... */
#define EC_MSP_P_ACT_TABLE_DATA_3_VAL_MASK 0x1FFFFFFF
#define EC_MSP_P_ACT_TABLE_DATA_3_VAL_SHIFT 0

/**** p_act_table_data_4 register ****/
/* Table data [7:0] - Offset to the header length location in th ... */
#define EC_MSP_P_ACT_TABLE_DATA_4_VAL_MASK 0x0FFFFFFF
#define EC_MSP_P_ACT_TABLE_DATA_4_VAL_SHIFT 0

/**** p_act_table_data_6 register ****/
/* Table data [0] - Wr header length [10:1] - Write header lengt ... */
#define EC_MSP_P_ACT_TABLE_DATA_6_VAL_MASK 0x007FFFFF
#define EC_MSP_P_ACT_TABLE_DATA_6_VAL_SHIFT 0

/**** p_res_in register ****/
/* Selector for input parse_en 0 - Input vector 1 - Default valu ... */
#define EC_MSP_P_RES_IN_SEL_PARSE_EN     (1 << 0)
/* Selector for input protocol_index  0 - Input vector  1 - Defa ... */
#define EC_MSP_P_RES_IN_SEL_PROT_INDEX   (1 << 1)
/* Selector for input hdr_offset  0 - Input vector 1 - Default v ... */
#define EC_MSP_P_RES_IN_SEL_HDR_OFFSET   (1 << 2)

/**** h_hdr_len register ****/
/* Value for selecting table 1 */
#define EC_MSP_P_H_HDR_LEN_TABLE_1_MASK  0x000000FF
#define EC_MSP_P_H_HDR_LEN_TABLE_1_SHIFT 0
/* Value for selecting table 2 */
#define EC_MSP_P_H_HDR_LEN_TABLE_2_MASK  0x00FF0000
#define EC_MSP_P_H_HDR_LEN_TABLE_2_SHIFT 16

/**** p_comp_data register ****/
/* Data 1 for comparison */
#define EC_MSP_C_P_COMP_DATA_DATA_1_MASK 0x0000FFFF
#define EC_MSP_C_P_COMP_DATA_DATA_1_SHIFT 0
/* Data 2 for comparison
[18:16] - Stage
[24:19] - Branch ID */
#define EC_MSP_C_P_COMP_DATA_DATA_2_MASK 0x01FF0000
#define EC_MSP_C_P_COMP_DATA_DATA_2_SHIFT 16

/**** p_comp_mask register ****/
/* Data 1 for comparison */
#define EC_MSP_C_P_COMP_MASK_DATA_1_MASK 0x0000FFFF
#define EC_MSP_C_P_COMP_MASK_DATA_1_SHIFT 0
/* Data 2 for comparison
[18:16] - Stage
[24:19] - Branch ID */
#define EC_MSP_C_P_COMP_MASK_DATA_2_MASK 0x01FF0000
#define EC_MSP_C_P_COMP_MASK_DATA_2_SHIFT 16

/**** p_comp_ctrl register ****/
/* Output result value */
#define EC_MSP_C_P_COMP_CTRL_RES_MASK    0x0000001F
#define EC_MSP_C_P_COMP_CTRL_RES_SHIFT   0
/* Compare command for the data_1 field 00 - Compare 01 - <= 10  ... */
#define EC_MSP_C_P_COMP_CTRL_CMD_1_MASK  0x00030000
#define EC_MSP_C_P_COMP_CTRL_CMD_1_SHIFT 16
/* Compare command for the data_2 field 00 - Compare 01 - <= 10  ... */
#define EC_MSP_C_P_COMP_CTRL_CMD_2_MASK  0x000C0000
#define EC_MSP_C_P_COMP_CTRL_CMD_2_SHIFT 18
/* Entry is valid */
#define EC_MSP_C_P_COMP_CTRL_VALID       (1 << 31)

/**** wol_en register ****/
/* Interrupt enable WoL MAC DA Unicast detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_UNICAST  (1 << 0)
/* Interrupt enable WoL L2 Multicast detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_MULTICAST (1 << 1)
/* Interrupt enable WoL L2 Broadcast detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_BROADCAST (1 << 2)
/* Interrupt enable WoL IPv4 detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_IPV4     (1 << 3)
/* Interrupt enable WoL IPv6 detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_IPV6     (1 << 4)
/* Interrupt enable WoL EtherType+MAC DA detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_ETHERTYPE_DA (1 << 5)
/* Interrupt enable WoL EtherType+L2 Broadcast detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_ETHERTYPE_BC (1 << 6)
/* Interrupt enable WoL parser detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_PARSER   (1 << 7)
/* Interrupt enable WoL magic detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_MAGIC    (1 << 8)
/* Interrupt enable WoL magic+password detected  packet */
#define EC_WOL_WOL_EN_INTRPT_EN_MAGIC_PSWD (1 << 9)
/* Forward enable WoL MAC DA Unicast detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_UNICAST    (1 << 16)
/* Forward enable WoL L2 Multicast detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_MULTICAST  (1 << 17)
/* Forward enable WoL L2 Broadcast detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_BROADCAST  (1 << 18)
/* Forward enable WoL IPv4 detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_IPV4       (1 << 19)
/* Forward enable WoL IPv6 detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_IPV6       (1 << 20)
/* Forward enable WoL EtherType+MAC DA detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_ETHERTYPE_DA (1 << 21)
/* Forward enable WoL EtherType+L2 Broadcast detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_ETHERTYPE_BC (1 << 22)
/* Forward enable WoL parser detected  packet */
#define EC_WOL_WOL_EN_FWRD_EN_PARSER     (1 << 23)

/**** magic_pswd_h register ****/
/* Password for magic_password packet detection - bits 47:32 */
#define EC_WOL_MAGIC_PSWD_H_VAL_MASK     0x0000FFFF
#define EC_WOL_MAGIC_PSWD_H_VAL_SHIFT    0

/**** ethertype register ****/
/* Configured EtherType 1 for WoL EtherType_da/EtherType_bc pack ... */
#define EC_WOL_ETHERTYPE_VAL_1_MASK      0x0000FFFF
#define EC_WOL_ETHERTYPE_VAL_1_SHIFT     0
/* Configured EtherType 2 for WoL EtherType_da/EtherType_bc pack ... */
#define EC_WOL_ETHERTYPE_VAL_2_MASK      0xFFFF0000
#define EC_WOL_ETHERTYPE_VAL_2_SHIFT     16

#define EC_PTH_SYSTEM_TIME_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_SYSTEM_TIME_SUBSECONDS_LSB_VAL_SHIFT 14

#define EC_PTH_CLOCK_PERIOD_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_CLOCK_PERIOD_LSB_VAL_SHIFT 14

/**** int_update_ctrl register ****/
/* This field chooses between two methods for SW to update the s ... */
#define EC_PTH_INT_UPDATE_CTRL_UPDATE_TRIG (1 << 0)
/* 3'b000 - Set system time according to the value in {int_updat ... */
#define EC_PTH_INT_UPDATE_CTRL_UPDATE_METHOD_MASK 0x0000000E
#define EC_PTH_INT_UPDATE_CTRL_UPDATE_METHOD_SHIFT 1
/* 1'b1 - Next update writes to system_time_subseconds1'b0 - Nex ... */
#define EC_PTH_INT_UPDATE_CTRL_SUBSECOND_MASK (1 << 4)
/* 1'b1 - Next update writes to system_time_seconds1'b0 - Next u ... */
#define EC_PTH_INT_UPDATE_CTRL_SECOND_MASK (1 << 5)
/* Enabling / disabling the internal ingress trigger (ingress_tr ... */
#define EC_PTH_INT_UPDATE_CTRL_INT_TRIG_EN (1 << 16)
/* Determines if internal ingress trigger (ingress_trigger #0) s ... */
#define EC_PTH_INT_UPDATE_CTRL_PULSE_LEVEL_N (1 << 17)
/* Internal ingress trigger polarity (ingress_trigger #0)1'b0 -  ... */
#define EC_PTH_INT_UPDATE_CTRL_POLARITY  (1 << 18)

/**** int_update_subseconds_lsb register ****/

#define EC_PTH_INT_UPDATE_SUBSECONDS_LSB_RESERVED_13_0_MASK 0x00003FFF
#define EC_PTH_INT_UPDATE_SUBSECONDS_LSB_RESERVED_13_0_SHIFT 0

#define EC_PTH_INT_UPDATE_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_INT_UPDATE_SUBSECONDS_LSB_VAL_SHIFT 14
/* 3'b000 - Set system time according to the value in {int_updat ... */
#define EC_PTH_EXT_UPDATE_CTRL_UPDATE_METHOD_MASK 0x0000000E
#define EC_PTH_EXT_UPDATE_CTRL_UPDATE_METHOD_SHIFT 1
/* 1'b1 - next update writes to system_time_subseconds1'b0 - nex ... */
#define EC_PTH_EXT_UPDATE_CTRL_SUBSECOND_MASK (1 << 4)
/* 1'b1 - Next update writes to system_time_seconds1'b0 - Next u ... */
#define EC_PTH_EXT_UPDATE_CTRL_SECOND_MASK (1 << 5)
/* Enabling / disabling the external ingress triggers (ingress_t ... */
#define EC_PTH_EXT_UPDATE_CTRL_EXT_TRIG_EN_MASK 0x00001F00
#define EC_PTH_EXT_UPDATE_CTRL_EXT_TRIG_EN_SHIFT 8
/* Determines if external ingress triggers (ingress_triggers #1- ... */
#define EC_PTH_EXT_UPDATE_CTRL_PULSE_LEVEL_N_MASK 0x001F0000
#define EC_PTH_EXT_UPDATE_CTRL_PULSE_LEVEL_N_SHIFT 16
/* bit-field configurations of external ingress trigger polarity ... */
#define EC_PTH_EXT_UPDATE_CTRL_POLARITY_MASK 0x1F000000
#define EC_PTH_EXT_UPDATE_CTRL_POLARITY_SHIFT 24

/**** ext_update_subseconds_lsb register ****/

#define EC_PTH_EXT_UPDATE_SUBSECONDS_LSB_RESERVED_13_0_MASK 0x00003FFF
#define EC_PTH_EXT_UPDATE_SUBSECONDS_LSB_RESERVED_13_0_SHIFT 0

#define EC_PTH_EXT_UPDATE_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_EXT_UPDATE_SUBSECONDS_LSB_VAL_SHIFT 14

#define EC_PTH_READ_COMPENSATION_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_READ_COMPENSATION_SUBSECONDS_LSB_VAL_SHIFT 14

#define EC_PTH_INT_WRITE_COMPENSATION_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_INT_WRITE_COMPENSATION_SUBSECONDS_LSB_VAL_SHIFT 14

#define EC_PTH_EXT_WRITE_COMPENSATION_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_EXT_WRITE_COMPENSATION_SUBSECONDS_LSB_VAL_SHIFT 14

#define EC_PTH_SYNC_COMPENSATION_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_SYNC_COMPENSATION_SUBSECONDS_LSB_VAL_SHIFT 14

/**** trigger_ctrl register ****/
/* Enabling / disabling the egress trigger1'b1 - Enabled1'b0 - D ... */
#define EC_PTH_EGRESS_TRIGGER_CTRL_EN    (1 << 0)
/* Configuration that determines if the egress trigger is a peri ... */
#define EC_PTH_EGRESS_TRIGGER_CTRL_PERIODIC (1 << 1)
/* Configuration of egress trigger polarity */
#define EC_PTH_EGRESS_TRIGGER_CTRL_POLARITY (1 << 2)
/* If the pulse is marked as periodic (see periodic field), this ... */
#define EC_PTH_EGRESS_TRIGGER_CTRL_PERIOD_SUBSEC_MASK 0x00FFFFF0
#define EC_PTH_EGRESS_TRIGGER_CTRL_PERIOD_SUBSEC_SHIFT 4
/* If the pulse is marked as periodic (see periodic field), this ... */
#define EC_PTH_EGRESS_TRIGGER_CTRL_PERIOD_SEC_MASK 0xFF000000
#define EC_PTH_EGRESS_TRIGGER_CTRL_PERIOD_SEC_SHIFT 24

/**** trigger_subseconds_lsb register ****/

#define EC_PTH_EGRESS_TRIGGER_SUBSECONDS_LSB_RESERVED_13_0_MASK 0x00003FFF
#define EC_PTH_EGRESS_TRIGGER_SUBSECONDS_LSB_RESERVED_13_0_SHIFT 0

#define EC_PTH_EGRESS_TRIGGER_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_EGRESS_TRIGGER_SUBSECONDS_LSB_VAL_SHIFT 14

/**** pulse_width_subseconds_lsb register ****/

#define EC_PTH_EGRESS_PULSE_WIDTH_SUBSECONDS_LSB_RESERVED_13_0_MASK 0x00003FFF
#define EC_PTH_EGRESS_PULSE_WIDTH_SUBSECONDS_LSB_RESERVED_13_0_SHIFT 0

#define EC_PTH_EGRESS_PULSE_WIDTH_SUBSECONDS_LSB_VAL_MASK 0xFFFFC000
#define EC_PTH_EGRESS_PULSE_WIDTH_SUBSECONDS_LSB_VAL_SHIFT 14

/**** qual register ****/

#define EC_PTH_DB_QUAL_TS_VALID       (1 << 0)

#define EC_PTH_DB_QUAL_RESERVED_31_1_MASK 0xFFFFFFFE
#define EC_PTH_DB_QUAL_RESERVED_31_1_SHIFT 1

/**** rx_comp_desc register ****/
/* Selection for word0[13]:0- legacy SR-A01- per generic protoco ... */
#define EC_GEN_V3_RX_COMP_DESC_W0_L3_CKS_RES_SEL (1 << 0)
/* Selection for word0[14]:0- legacy SR-A01- per generic protoco ... */
#define EC_GEN_V3_RX_COMP_DESC_W0_L4_CKS_RES_SEL (1 << 1)
/* Selection for word3[29]:0-macsec decryption status[13] (legac ... */
#define EC_GEN_V3_RX_COMP_DESC_W3_DEC_STAT_13_L4_CKS_RES_SEL (1 << 8)
/* Selection for word3[30]:0-macsec decryption status[14] (legac ... */
#define EC_GEN_V3_RX_COMP_DESC_W3_DEC_STAT_14_L3_CKS_RES_SEL (1 << 9)
/* Selection for word3[31]:0-macsec decryption status[15] (legac ... */
#define EC_GEN_V3_RX_COMP_DESC_W3_DEC_STAT_15_CRC_RES_SEL (1 << 10)
/* Selection for word 0 [6:5], source VLAN count0- source vlan c ... */
#define EC_GEN_V3_RX_COMP_DESC_W0_SRC_VLAN_CNT (1 << 12)
/* Selection for word 0 [4:0], l3 protocol index0-  l3 protocol  ... */
#define EC_GEN_V3_RX_COMP_DESC_W0_L3_PROT_INDEX (1 << 13)
/* Selection for word 1 [31:16], lP fragment checksum0-  IP frag ... */
#define EC_GEN_V3_RX_COMP_DESC_W1_IP_FRAG_CHECKSUM (1 << 14)
/* Selection for word 2 [15:9], L3 offset0-  LL3 offset1- CRC re ... */
#define EC_GEN_V3_RX_COMP_DESC_W2_L3_OFFSET (1 << 15)
/* Selection for word 2 [8:0], tunnel offset0-  tunnel offset1-  ... */
#define EC_GEN_V3_RX_COMP_DESC_W2_TUNNEL_OFFSET (1 << 16)

/**** conf register ****/
/* Valid signal configuration when in loopback mode:00 - valid f ... */
#define EC_GEN_V3_CONF_MAC_LB_EC_OUT_S_VALID_CFG_MASK 0x00000003
#define EC_GEN_V3_CONF_MAC_LB_EC_OUT_S_VALID_CFG_SHIFT 0
/* Valid signal configuration when in loopback mode:00 – valid f ... */
#define EC_GEN_V3_CONF_MAC_LB_EC_IN_S_VALID_CFG_MASK 0x0000000C
#define EC_GEN_V3_CONF_MAC_LB_EC_IN_S_VALID_CFG_SHIFT 2

/**** tx_gpd_cam_addr register ****/
/* Cam compare table address */
#define EC_TFW_V3_TX_GPD_CAM_ADDR_VAL_MASK 0x0000001F
#define EC_TFW_V3_TX_GPD_CAM_ADDR_VAL_SHIFT 0
/* cam entry is valid */
#define EC_TFW_V3_TX_GPD_CAM_CTRL_VALID  (1 << 31)

/**** tx_gcp_legacy register ****/
/* 0-choose parameters from table1- choose legacy crce roce para ... */
#define EC_TFW_V3_TX_GCP_LEGACY_PARAM_SEL (1 << 0)

/**** tx_gcp_table_addr register ****/
/* parametrs table address */
#define EC_TFW_V3_TX_GCP_TABLE_ADDR_VAL_MASK 0x0000001F
#define EC_TFW_V3_TX_GCP_TABLE_ADDR_VAL_SHIFT 0

/**** tx_gcp_table_gen register ****/
/* polynomial selcet
0-crc32(0x104C11DB7)
1-crc32c(0x11EDC6F41) */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_POLY_SEL (1 << 0)
/* Enable bit complement on crc result */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_CRC32_BIT_COMP (1 << 1)
/* Enable bit swap on crc result */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_CRC32_BIT_SWAP (1 << 2)
/* Enable byte swap on crc result */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_CRC32_BYTE_SWAP (1 << 3)
/* Enable bit swap on input data */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_DATA_BIT_SWAP (1 << 4)
/* Enable byte swap on input data */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_DATA_BYTE_SWAP (1 << 5)
/* Number of bytes in trailer which are not part of crc calculat ... */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_TRAIL_SIZE_MASK 0x000003C0
#define EC_TFW_V3_TX_GCP_TABLE_GEN_TRAIL_SIZE_SHIFT 6
/* Number of bytes in header which are not part of crc calculati ... */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_HEAD_SIZE_MASK 0x00FF0000
#define EC_TFW_V3_TX_GCP_TABLE_GEN_HEAD_SIZE_SHIFT 16
/* corrected offset calculation0- subtract head_size (roce)1- ad ... */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_HEAD_CALC (1 << 24)
/* 0-replace masked bits with 01-replace masked bits with 1 (roc ... */
#define EC_TFW_V3_TX_GCP_TABLE_GEN_MASK_POLARITY (1 << 25)

/**** tx_gcp_table_res register ****/
/* Not in use */
#define EC_TFW_V3_TX_GCP_TABLE_RES_SEL_MASK 0x0000001F
#define EC_TFW_V3_TX_GCP_TABLE_RES_SEL_SHIFT 0
/* Not in use */
#define EC_TFW_V3_TX_GCP_TABLE_RES_EN    (1 << 5)
/* Not in use */
#define EC_TFW_V3_TX_GCP_TABLE_RES_DEF   (1 << 6)

/**** tx_gcp_table_alu_opcode register ****/
/* first opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_1_MASK 0x0000003F
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_1_SHIFT 0
/* second opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_2_MASK 0x00000FC0
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_2_SHIFT 6
/* third opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_3_MASK 0x0003F000
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPCODE_OPCODE_3_SHIFT 12

/**** tx_gcp_table_alu_opsel register ****/
/* frst opsel, input selection */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_1_MASK 0x0000000F
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_1_SHIFT 0
/* second opsel, input selection */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_2_MASK 0x000000F0
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_2_SHIFT 4
/* third opsel, input selction */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_3_MASK 0x00000F00
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_3_SHIFT 8
/* fourth opsel, input selction */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_4_MASK 0x0000F000
#define EC_TFW_V3_TX_GCP_TABLE_ALU_OPSEL_OPSEL_4_SHIFT 12

/**** tx_gcp_table_alu_val register ****/
/* value for alu input */
#define EC_TFW_V3_TX_GCP_TABLE_ALU_VAL_VAL_MASK 0x000001FF
#define EC_TFW_V3_TX_GCP_TABLE_ALU_VAL_VAL_SHIFT 0

/**** crc_csum_replace register ****/
/* 0- use table
1- legacy SR-A0 */
#define EC_TFW_V3_CRC_CSUM_REPLACE_L3_CSUM_LEGACY_SEL (1 << 0)
/* 0- use table
1- legacy SR-A0 */
#define EC_TFW_V3_CRC_CSUM_REPLACE_L4_CSUM_LEGACY_SEL (1 << 1)
/* 0- use table
1- legacy SR-A0 */
#define EC_TFW_V3_CRC_CSUM_REPLACE_CRC_LEGACY_SEL (1 << 2)

/**** crc_csum_replace_table_addr register ****/
/* parametrs table address */
#define EC_TFW_V3_CRC_CSUM_REPLACE_TABLE_ADDR_VAL_MASK 0x0000007F
#define EC_TFW_V3_CRC_CSUM_REPLACE_TABLE_ADDR_VAL_SHIFT 0

/**** crc_csum_replace_table register ****/
/* L3 Checksum replace enable */
#define EC_TFW_V3_CRC_CSUM_REPLACE_TABLE_L3_CSUM_EN (1 << 0)
/* L4 Checksum replace enable */
#define EC_TFW_V3_CRC_CSUM_REPLACE_TABLE_L4_CSUM_EN (1 << 1)
/* CRC replace enable */
#define EC_TFW_V3_CRC_CSUM_REPLACE_TABLE_CRC_EN (1 << 2)

/**** rx_gpd_cam_addr register ****/
/* Cam compare table address */
#define EC_RFW_V3_RX_GPD_CAM_ADDR_VAL_MASK 0x0000001F
#define EC_RFW_V3_RX_GPD_CAM_ADDR_VAL_SHIFT 0
/* cam entry is valid */
#define EC_RFW_V3_RX_GPD_CAM_CTRL_VALID  (1 << 31)

/**** gpd_p1 register ****/
/* Location in bytes of the gpd cam data1 in the parser result v ... */
#define EC_RFW_V3_GPD_P1_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P1_OFFSET_SHIFT    0

/**** gpd_p2 register ****/
/* Location in bytes of the gpd cam data2 in the parser result v ... */
#define EC_RFW_V3_GPD_P2_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P2_OFFSET_SHIFT    0

/**** gpd_p3 register ****/
/* Location in bytes of the gpd cam data3 in the parser result v ... */
#define EC_RFW_V3_GPD_P3_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P3_OFFSET_SHIFT    0

/**** gpd_p4 register ****/
/* Location in bytes of the gpd cam data4 in the parser result v ... */
#define EC_RFW_V3_GPD_P4_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P4_OFFSET_SHIFT    0

/**** gpd_p5 register ****/
/* Location in bytes of the gpd cam data5 in the parser result v ... */
#define EC_RFW_V3_GPD_P5_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P5_OFFSET_SHIFT    0

/**** gpd_p6 register ****/
/* Location in bytes of the gpd cam data6 in the parser result v ... */
#define EC_RFW_V3_GPD_P6_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P6_OFFSET_SHIFT    0

/**** gpd_p7 register ****/
/* Location in bytes of the gpd cam data7 in the parser result v ... */
#define EC_RFW_V3_GPD_P7_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P7_OFFSET_SHIFT    0

/**** gpd_p8 register ****/
/* Location in bytes of the gpd cam data8 in the parser result v ... */
#define EC_RFW_V3_GPD_P8_OFFSET_MASK     0x000003FF
#define EC_RFW_V3_GPD_P8_OFFSET_SHIFT    0

/**** rx_gcp_legacy register ****/
/* 0-choose parameters from table1- choose legacy crce roce para ... */
#define EC_RFW_V3_RX_GCP_LEGACY_PARAM_SEL (1 << 0)

/**** rx_gcp_table_addr register ****/
/* parametrs table address */
#define EC_RFW_V3_RX_GCP_TABLE_ADDR_VAL_MASK 0x0000001F
#define EC_RFW_V3_RX_GCP_TABLE_ADDR_VAL_SHIFT 0

/**** rx_gcp_table_gen register ****/
/* polynomial selcet
0-crc32(0x104C11DB7)
1-crc32c(0x11EDC6F41) */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_POLY_SEL (1 << 0)
/* Enable bit complement on crc result */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_CRC32_BIT_COMP (1 << 1)
/* Enable bit swap on crc result */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_CRC32_BIT_SWAP (1 << 2)
/* Enable byte swap on crc result */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_CRC32_BYTE_SWAP (1 << 3)
/* Enable bit swap on input data */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_DATA_BIT_SWAP (1 << 4)
/* Enable byte swap on input data */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_DATA_BYTE_SWAP (1 << 5)
/* Number of bytes in trailer which are not part of crc calculat ... */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_TRAIL_SIZE_MASK 0x000003C0
#define EC_RFW_V3_RX_GCP_TABLE_GEN_TRAIL_SIZE_SHIFT 6
/* Number of bytes in header which are not part of crc calculati ... */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_HEAD_SIZE_MASK 0x00FF0000
#define EC_RFW_V3_RX_GCP_TABLE_GEN_HEAD_SIZE_SHIFT 16
/* corrected offset calculation0- subtract head_size (roce)1- ad ... */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_HEAD_CALC (1 << 24)
/* 0-replace masked bits with 01-replace masked bits with 1 (roc ... */
#define EC_RFW_V3_RX_GCP_TABLE_GEN_MASK_POLARITY (1 << 25)

/**** rx_gcp_table_res register ****/
/* Bit mask for crc/checksum result options for metadata W0[13][ ... */
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_0_MASK 0x0000001F
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_0_SHIFT 0
/* Bit mask for crc/checksum result options for metadata W0[14][ ... */
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_1_MASK 0x000003E0
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_1_SHIFT 5
/* Bit mask for crc/checksum result options for metadata W3[29][ ... */
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_2_MASK 0x00007C00
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_2_SHIFT 10
/* Bit mask for crc/checksum result options for metadata W3[30][ ... */
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_3_MASK 0x000F8000
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_3_SHIFT 15
/* Bit mask for crc/checksum result options for metadata W3[31][ ... */
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_4_MASK 0x01F00000
#define EC_RFW_V3_RX_GCP_TABLE_RES_SEL_4_SHIFT 20
/* enable crc result check */
#define EC_RFW_V3_RX_GCP_TABLE_RES_EN    (1 << 25)
/* default value for crc check for non-crc protocol */
#define EC_RFW_V3_RX_GCP_TABLE_RES_DEF   (1 << 26)

/**** rx_gcp_table_alu_opcode register ****/
/* first opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_1_MASK 0x0000003F
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_1_SHIFT 0
/* second opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_2_MASK 0x00000FC0
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_2_SHIFT 6
/* third opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_3_MASK 0x0003F000
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPCODE_OPCODE_3_SHIFT 12

/**** rx_gcp_table_alu_opsel register ****/
/* frst opsel, input selection */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_1_MASK 0x0000000F
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_1_SHIFT 0
/* second opsel, input selection */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_2_MASK 0x000000F0
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_2_SHIFT 4
/* third opsel, input selction */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_3_MASK 0x00000F00
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_3_SHIFT 8
/* fourth opsel, input selction */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_4_MASK 0x0000F000
#define EC_RFW_V3_RX_GCP_TABLE_ALU_OPSEL_OPSEL_4_SHIFT 12

/**** rx_gcp_table_alu_val register ****/
/* value for alu input */
#define EC_RFW_V3_RX_GCP_TABLE_ALU_VAL_VAL_MASK 0x000001FF
#define EC_RFW_V3_RX_GCP_TABLE_ALU_VAL_VAL_SHIFT 0

/**** rx_gcp_alu_p1 register ****/
/* Location in bytes of field 1 in the parser result vector */
#define EC_RFW_V3_RX_GCP_ALU_P1_OFFSET_MASK 0x000003FF
#define EC_RFW_V3_RX_GCP_ALU_P1_OFFSET_SHIFT 0
/* Right shift for field 1 in the parser result vector */
#define EC_RFW_V3_RX_GCP_ALU_P1_SHIFT_MASK 0x000F0000
#define EC_RFW_V3_RX_GCP_ALU_P1_SHIFT_SHIFT 16

/**** rx_gcp_alu_p2 register ****/
/* Location in bytes of field 2 in the parser result vector */
#define EC_RFW_V3_RX_GCP_ALU_P2_OFFSET_MASK 0x000003FF
#define EC_RFW_V3_RX_GCP_ALU_P2_OFFSET_SHIFT 0
/* Right shift for field 2 in the parser result vector */
#define EC_RFW_V3_RX_GCP_ALU_P2_SHIFT_MASK 0x000F0000
#define EC_RFW_V3_RX_GCP_ALU_P2_SHIFT_SHIFT 16

/**** hs_ctrl_table_addr register ****/
/* Header split control table address */
#define EC_RFW_V3_HS_CTRL_TABLE_ADDR_VAL_MASK 0x000000FF
#define EC_RFW_V3_HS_CTRL_TABLE_ADDR_VAL_SHIFT 0

/**** hs_ctrl_table register ****/
/* Header split length select */
#define EC_RFW_V3_HS_CTRL_TABLE_SEL_MASK 0x00000003
#define EC_RFW_V3_HS_CTRL_TABLE_SEL_SHIFT 0
/* enable header split */
#define EC_RFW_V3_HS_CTRL_TABLE_ENABLE   (1 << 2)

/**** hs_ctrl_table_alu_opcode register ****/
/* first opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_1_MASK 0x0000003F
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_1_SHIFT 0
/* second opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_2_MASK 0x00000FC0
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_2_SHIFT 6
/* third opcode
e.g. (A op1 B) op3 (C op2 D) */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_3_MASK 0x0003F000
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPCODE_OPCODE_3_SHIFT 12

/**** hs_ctrl_table_alu_opsel register ****/
/* frst opsel, input selection */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_1_MASK 0x0000000F
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_1_SHIFT 0
/* second opsel, input selection */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_2_MASK 0x000000F0
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_2_SHIFT 4
/* third opsel, input selction */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_3_MASK 0x00000F00
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_3_SHIFT 8
/* fourth opsel, input selction */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_4_MASK 0x0000F000
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_OPSEL_OPSEL_4_SHIFT 12

/**** hs_ctrl_table_alu_val register ****/
/* value for alu input */
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_VAL_VAL_MASK 0x0000FFFF
#define EC_RFW_V3_HS_CTRL_TABLE_ALU_VAL_VAL_SHIFT 0

/**** hs_ctrl_cfg register ****/
/* Header split enable static selction0 – legacy1 – header split ... */
#define EC_RFW_V3_HS_CTRL_CFG_ENABLE_SEL (1 << 0)
/* Header split length static selction0 – legacy1 – header split ... */
#define EC_RFW_V3_HS_CTRL_CFG_LENGTH_SEL (1 << 1)

/**** hs_ctrl_alu_p1 register ****/
/* Location in bytes of field 1 in the parser result vector */
#define EC_RFW_V3_HS_CTRL_ALU_P1_OFFSET_MASK 0x000003FF
#define EC_RFW_V3_HS_CTRL_ALU_P1_OFFSET_SHIFT 0
/* Right shift for field 1 in the parser result vector */
#define EC_RFW_V3_HS_CTRL_ALU_P1_SHIFT_MASK 0x000F0000
#define EC_RFW_V3_HS_CTRL_ALU_P1_SHIFT_SHIFT 16

/**** hs_ctrl_alu_p2 register ****/
/* Location in bytes of field 2 in the parser result vector */
#define EC_RFW_V3_HS_CTRL_ALU_P2_OFFSET_MASK 0x000003FF
#define EC_RFW_V3_HS_CTRL_ALU_P2_OFFSET_SHIFT 0
/* Right shift for field 2 in the parser result vector */
#define EC_RFW_V3_HS_CTRL_ALU_P2_SHIFT_MASK 0x000F0000
#define EC_RFW_V3_HS_CTRL_ALU_P2_SHIFT_SHIFT 16

/**** tx_config register ****/
/* [0] pre increment word swap[1] pre increment byte swap[2] pre ... */
#define EC_CRYPTO_TX_CONFIG_TWEAK_ENDIANITY_SWAP_MASK 0x0000003F
#define EC_CRYPTO_TX_CONFIG_TWEAK_ENDIANITY_SWAP_SHIFT 0
/* [0] pre encryption word swap[1] pre encryption byte swap[2] p ... */
#define EC_CRYPTO_TX_CONFIG_DATA_ENDIANITY_SWAP_MASK 0x00003F00
#define EC_CRYPTO_TX_CONFIG_DATA_ENDIANITY_SWAP_SHIFT 8
/* direction flip, used in order to use same TID entry for both TX & RX traffic */
#define EC_CRYPTO_TX_CONFIG_CRYPTO_DIR_FLIP (1 << 14)
/* Enabling pipe line optimization */
#define EC_CRYPTO_TX_CONFIG_PIPE_CALC_EN (1 << 16)
/* enable performance counters */
#define EC_CRYPTO_TX_CONFIG_PERF_CNT_EN  (1 << 17)
/* [0] pre aes word swap[1] pre aes byte swap[2] pre aes bit swa ... */
#define EC_CRYPTO_TX_CONFIG_AES_ENDIANITY_SWAP_MASK 0x03F00000
#define EC_CRYPTO_TX_CONFIG_AES_ENDIANITY_SWAP_SHIFT 20
/* [0] pre aes key word swap[1] pre aes key byte swap[2] pre aes ... */
#define EC_CRYPTO_TX_CONFIG_AES_KEY_ENDIANITY_SWAP_MASK 0xFC000000
#define EC_CRYPTO_TX_CONFIG_AES_KEY_ENDIANITY_SWAP_SHIFT 26

/**** rx_config register ****/
/* [0] pre increment word swap[1] pre increment byte swap[2] pre ... */
#define EC_CRYPTO_RX_CONFIG_TWEAK_ENDIANITY_SWAP_MASK 0x0000003F
#define EC_CRYPTO_RX_CONFIG_TWEAK_ENDIANITY_SWAP_SHIFT 0
/* [0] pre encryption word swap[1] pre encryption byte swap[2] p ... */
#define EC_CRYPTO_RX_CONFIG_DATA_ENDIANITY_SWAP_MASK 0x00003F00
#define EC_CRYPTO_RX_CONFIG_DATA_ENDIANITY_SWAP_SHIFT 8
/* direction flip, used in order to use same TID entry for both TX & RX traffic */
#define EC_CRYPTO_RX_CONFIG_CRYPTO_DIR_FLIP (1 << 14)
/* Enabling pipe line optimization */
#define EC_CRYPTO_RX_CONFIG_PIPE_CALC_EN (1 << 16)
/* enable performance counters */
#define EC_CRYPTO_RX_CONFIG_PERF_CNT_EN  (1 << 17)
/* [0] pre aes word swap[1] pre aes byte swap[2] pre aes bit swa ... */
#define EC_CRYPTO_RX_CONFIG_AES_ENDIANITY_SWAP_MASK 0x03F00000
#define EC_CRYPTO_RX_CONFIG_AES_ENDIANITY_SWAP_SHIFT 20
/* [0] data aes key word swap[1] data aes key byte swap[2] data  ... */
#define EC_CRYPTO_RX_CONFIG_AES_KEY_ENDIANITY_SWAP_MASK 0xFC000000
#define EC_CRYPTO_RX_CONFIG_AES_KEY_ENDIANITY_SWAP_SHIFT 26

/**** tx_override register ****/
/* all transactions are encrypted */
#define EC_CRYPTO_TX_OVERRIDE_ENCRYPT_ONLY (1 << 0)
/* all transactions are decrypted */
#define EC_CRYPTO_TX_OVERRIDE_DECRYPT_ONLY (1 << 1)
/* all pkts use IV */
#define EC_CRYPTO_TX_OVERRIDE_ALWAYS_DRIVE_IV (1 << 2)
/* no pkt uses IV */
#define EC_CRYPTO_TX_OVERRIDE_NEVER_DRIVE_IV (1 << 3)
/* all pkts perform authentication calculation */
#define EC_CRYPTO_TX_OVERRIDE_ALWAYS_PERFORM_SIGN (1 << 4)
/* no pkt performs authentication calculation */
#define EC_CRYPTO_TX_OVERRIDE_NEVER_PERFORM_SIGN (1 << 5)
/* all pkts perform encryption calculation */
#define EC_CRYPTO_TX_OVERRIDE_ALWAYS_PERFORM_ENC (1 << 6)
/* no pkt performs encryption calculation */
#define EC_CRYPTO_TX_OVERRIDE_NEVER_PERFORM_ENC (1 << 7)
/* Enforce pkt trimming
bit[0] relates to metadata_pkt_trim
bit[1] relates to trailer_pkt_trime
bit[2] relates to sign_trim
bit[3] relates to aes_padding_trim */
#define EC_CRYPTO_TX_OVERRIDE_ALWAYS_BYPASS_PKT_TRIM_MASK 0x00000F00
#define EC_CRYPTO_TX_OVERRIDE_ALWAYS_BYPASS_PKT_TRIM_SHIFT 8
/* Enforce no pkt trimming
bit[0] relates to metadata_pkt_trim
bit[1] relates to trailer_pkt_trime
bit[2] relates to sign_trim
bit[3] relates to aes_padding_trim */
#define EC_CRYPTO_TX_OVERRIDE_NEVER_BYPASS_PKT_TRIM_MASK 0x0000F000
#define EC_CRYPTO_TX_OVERRIDE_NEVER_BYPASS_PKT_TRIM_SHIFT 12
/* chicken bit to disable metadata handling optimization */
#define EC_CRYPTO_TX_OVERRIDE_EXPLICIT_METADATA_STAGE (1 << 16)

/**** rx_override register ****/
/* all transactions are encrypted */
#define EC_CRYPTO_RX_OVERRIDE_ENCRYPT_ONLY (1 << 0)
/* all transactions are decrypted */
#define EC_CRYPTO_RX_OVERRIDE_DECRYPT_ONLY (1 << 1)
/* all pkts use IV */
#define EC_CRYPTO_RX_OVERRIDE_ALWAYS_DRIVE_IV (1 << 2)
/* no pkt uses IV */
#define EC_CRYPTO_RX_OVERRIDE_NEVER_DRIVE_IV (1 << 3)
/* all pkts perform authentication calculation */
#define EC_CRYPTO_RX_OVERRIDE_ALWAYS_PERFORM_SIGN (1 << 4)
/* no pkt performs authentication calculation */
#define EC_CRYPTO_RX_OVERRIDE_NEVER_PERFORM_SIGN (1 << 5)
/* all pkts perform encryption calculation */
#define EC_CRYPTO_RX_OVERRIDE_ALWAYS_PERFORM_ENC (1 << 6)
/* no pkt performs encryption calculation */
#define EC_CRYPTO_RX_OVERRIDE_NEVER_PERFORM_ENC (1 << 7)
/* Enforce pkt trimming
bit[0] relates to metadata_pkt_trim
bit[1] relates to trailer_pkt_trime
bit[2] relates to sign_trim
bit[3] relates to aes_padding_trim */
#define EC_CRYPTO_RX_OVERRIDE_ALWAYS_BYPASS_PKT_TRIM_MASK 0x00000F00
#define EC_CRYPTO_RX_OVERRIDE_ALWAYS_BYPASS_PKT_TRIM_SHIFT 8
/* Enforce no pkt trimming
bit[0] relates to metadata_pkt_trim
bit[1] relates to trailer_pkt_trime
bit[2] relates to sign_trim
bit[3] relates to aes_padding_trim */
#define EC_CRYPTO_RX_OVERRIDE_NEVER_BYPASS_PKT_TRIM_MASK 0x0000F000
#define EC_CRYPTO_RX_OVERRIDE_NEVER_BYPASS_PKT_TRIM_SHIFT 12
/* bit enable for writing to rx_cmpl metadata info */
#define EC_CRYPTO_RX_OVERRIDE_META_DATA_WRITE_EN_MASK 0x00070000
#define EC_CRYPTO_RX_OVERRIDE_META_DATA_WRITE_EN_SHIFT 16
/* chicken bit to disable metadata handling optimization */
#define EC_CRYPTO_RX_OVERRIDE_EXPLICIT_METADATA_STAGE (1 << 19)
/* crypto metadata offset in the rx cmpl_desc */
#define EC_CRYPTO_RX_OVERRIDE_META_DATA_BASE_MASK 0x07F00000
#define EC_CRYPTO_RX_OVERRIDE_META_DATA_BASE_SHIFT 20

/**** tx_enc_iv_construction register ****/
/* for each IV byte, select between src1 & src2. Src1 & src2 ... */
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MUX_SEL_MASK 0x0000FFFF
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MUX_SEL_SHIFT 0
/* configure meaning of mux_sel=1'b0 (2'b00 – zeros, 2'b01 f...  */
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MAP_0_MASK 0x00030000
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MAP_0_SHIFT 16
/* configure meaning of mux_sel=1'b1 (2'b00 – zeros, 2'b01 ...  */
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MAP_1_MASK 0x000C0000
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_MAP_1_SHIFT 18
/* Per-byte mux select taken from Crypto table (otherwise ...  */
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_SEL_FROM_TABLE (1 << 20)
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_ENDIANITY_SWAP_MASK 0x00E00000
#define EC_CRYPTO_TX_ENC_IV_CONSTRUCTION_ENDIANITY_SWAP_SHIFT 21

/**** rx_enc_iv_construction register ****/
/* for each IV byte, select between src1 & src2. Src1 & src2 ...  */
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MUX_SEL_MASK 0x0000FFFF
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MUX_SEL_SHIFT 0
/* configure meaning of mux_sel=1'b0 (2'b00 – zeros, 2'b01 – ...  */
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MAP_0_MASK 0x00030000
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MAP_0_SHIFT 16
/* configure meaning of mux_sel=1'b1 (2'b00 – zeros, 2'b01 – ...  */
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MAP_1_MASK 0x000C0000
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_MAP_1_SHIFT 18
/* Per-byte mux select taken from Crypto table (otherwise from ...  */
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_SEL_FROM_TABLE (1 << 20)
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_ENDIANITY_SWAP_MASK 0x00E00000
#define EC_CRYPTO_RX_ENC_IV_CONSTRUCTION_ENDIANITY_SWAP_SHIFT 21

/**** rx_enc_iv_map register ****/
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_0_OFFSET_MASK 0x0000001F
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_0_OFFSET_SHIFT 0
/* number of valid bytes in word, as generated by field extract ... */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_0_LENGTH_MASK 0x000000E0
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_0_LENGTH_SHIFT 5
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_1_OFFSET_MASK 0x00001F00
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_1_OFFSET_SHIFT 8
/* number of valid bytes in word, as generated by field extract ... */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_1_LENGTH_MASK 0x0000E000
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_1_LENGTH_SHIFT 13
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_2_OFFSET_MASK 0x001F0000
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_2_OFFSET_SHIFT 16
/* number of valid bytes in word, as generated by field extract ... */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_2_LENGTH_MASK 0x00E00000
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_2_LENGTH_SHIFT 21
/* [0] word swap en
[1] byte swap en
[2] bit swap en */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_3_OFFSET_MASK 0x1F000000
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_3_OFFSET_SHIFT 24
/* number of valid bytes in word, as generated by field extract ... */
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_3_LENGTH_MASK 0xE0000000
#define EC_CRYPTO_RX_ENC_IV_MAP_FIELD_EXTRACT_3_LENGTH_SHIFT 29

/**** tx_pkt_trim_len register ****/
/* metadata shift-reg length */
#define EC_CRYPTO_TX_PKT_TRIM_LEN_META_MASK 0x00000007
#define EC_CRYPTO_TX_PKT_TRIM_LEN_META_SHIFT 0
/* pkt trailer shift-reg length */
#define EC_CRYPTO_TX_PKT_TRIM_LEN_TRAIL_MASK 0x000000F0
#define EC_CRYPTO_TX_PKT_TRIM_LEN_TRAIL_SHIFT 4
/* sign shift-reg length */
#define EC_CRYPTO_TX_PKT_TRIM_LEN_SIGN_MASK 0x00000300
#define EC_CRYPTO_TX_PKT_TRIM_LEN_SIGN_SHIFT 8
/* crypto padding shift-reg length */
#define EC_CRYPTO_TX_PKT_TRIM_LEN_CRYPTO_PADDING_MASK 0x00003000
#define EC_CRYPTO_TX_PKT_TRIM_LEN_CRYPTO_PADDING_SHIFT 12
/* hardware chooses shift-registers configurations automatically – no need for sw configuration */
#define EC_CRYPTO_TX_PKT_TRIM_LEN_AUTO_MODE (1 << 16)

/**** rx_pkt_trim_len register ****/
/* metadata shift-reg length */
#define EC_CRYPTO_RX_PKT_TRIM_LEN_META_MASK 0x00000007
#define EC_CRYPTO_RX_PKT_TRIM_LEN_META_SHIFT 0
/* pkt trailer shift-reg length */
#define EC_CRYPTO_RX_PKT_TRIM_LEN_TRAIL_MASK 0x000000F0
#define EC_CRYPTO_RX_PKT_TRIM_LEN_TRAIL_SHIFT 4
/* sign shift-reg length */
#define EC_CRYPTO_RX_PKT_TRIM_LEN_SIGN_MASK 0x00000300
#define EC_CRYPTO_RX_PKT_TRIM_LEN_SIGN_SHIFT 8
/* crypto padding shift-reg length */
#define EC_CRYPTO_RX_PKT_TRIM_LEN_CRYPTO_PADDING_MASK 0x00003000
#define EC_CRYPTO_RX_PKT_TRIM_LEN_CRYPTO_PADDING_SHIFT 12
/* hardware chooses shift-registers configurations automatically – no need for sw configuration */
#define EC_CRYPTO_RX_PKT_TRIM_LEN_AUTO_MODE (1 << 16)

/**** total_tx_secured_pkts_cipher_mode_cmpr register ****/

#define EC_CRYPTO_PERF_CNTR_TOTAL_TX_SECURED_PKTS_CIPHER_MODE_CMPR_MODE_MASK 0x0000000F
#define EC_CRYPTO_PERF_CNTR_TOTAL_TX_SECURED_PKTS_CIPHER_MODE_CMPR_MODE_SHIFT 0

/**** total_rx_secured_pkts_cipher_mode_cmpr register ****/

#define EC_CRYPTO_PERF_CNTR_TOTAL_RX_SECURED_PKTS_CIPHER_MODE_CMPR_MODE_MASK 0x0000000F
#define EC_CRYPTO_PERF_CNTR_TOTAL_RX_SECURED_PKTS_CIPHER_MODE_CMPR_MODE_SHIFT 0

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_EC_REG_H */

/** @} end of ... group */


