/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2017 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef ECORE_MFW_REQ_H
#define ECORE_MFW_REQ_H



#define PORT_0              0
#define PORT_1              1
#define PORT_MAX            2
#define NVM_PATH_MAX        2

/* FCoE capabilities required from the driver */
struct fcoe_capabilities {
	uint32_t capability1;
	/* Maximum number of I/Os per connection */
	#define FCOE_IOS_PER_CONNECTION_MASK    0x0000ffff
	#define FCOE_IOS_PER_CONNECTION_SHIFT   0
	/* Maximum number of Logins per port */
	#define FCOE_LOGINS_PER_PORT_MASK       0xffff0000
	#define FCOE_LOGINS_PER_PORT_SHIFT   16

	uint32_t capability2;
	/* Maximum number of exchanges */
	#define FCOE_NUMBER_OF_EXCHANGES_MASK   0x0000ffff
	#define FCOE_NUMBER_OF_EXCHANGES_SHIFT  0
	/* Maximum NPIV WWN per port */
	#define FCOE_NPIV_WWN_PER_PORT_MASK     0xffff0000
	#define FCOE_NPIV_WWN_PER_PORT_SHIFT    16

	uint32_t capability3;
	/* Maximum number of targets supported */
	#define FCOE_TARGETS_SUPPORTED_MASK     0x0000ffff
	#define FCOE_TARGETS_SUPPORTED_SHIFT    0
	/* Maximum number of outstanding commands across all connections */
	#define FCOE_OUTSTANDING_COMMANDS_MASK  0xffff0000
	#define FCOE_OUTSTANDING_COMMANDS_SHIFT 16

	uint32_t capability4;
	#define FCOE_CAPABILITY4_STATEFUL       		0x00000001
	#define FCOE_CAPABILITY4_STATELESS      		0x00000002
	#define FCOE_CAPABILITY4_CAPABILITIES_REPORTED_VALID   	0x00000004
};

struct glob_ncsi_oem_data
{
	uint32_t driver_version;
	uint32_t unused[3];
	struct fcoe_capabilities fcoe_features[NVM_PATH_MAX][PORT_MAX];
};

/* current drv_info version */
#define DRV_INFO_CUR_VER 2

/* drv_info op codes supported */
enum drv_info_opcode {
	ETH_STATS_OPCODE,
	FCOE_STATS_OPCODE,
	ISCSI_STATS_OPCODE
};

#define ETH_STAT_INFO_VERSION_LEN	12
/*  Per PCI Function Ethernet Statistics required from the driver */
struct eth_stats_info {
	/* Function's Driver Version. padded to 12 */
	uint8_t version[ETH_STAT_INFO_VERSION_LEN];
	/* Locally Admin Addr. BigEndian EIU48. Actual size is 6 bytes */
	uint8_t mac_local[8];
	uint8_t mac_add1[8];		/* Additional Programmed MAC Addr 1. */
	uint8_t mac_add2[8];		/* Additional Programmed MAC Addr 2. */
	uint32_t mtu_size;		/* MTU Size. Note   : Negotiated MTU */
	uint32_t feature_flags;	/* Feature_Flags. */
#define FEATURE_ETH_CHKSUM_OFFLOAD_MASK		0x01
#define FEATURE_ETH_LSO_MASK			0x02
#define FEATURE_ETH_BOOTMODE_MASK		0x1C
#define FEATURE_ETH_BOOTMODE_SHIFT		2
#define FEATURE_ETH_BOOTMODE_NONE		(0x0 << 2)
#define FEATURE_ETH_BOOTMODE_PXE		(0x1 << 2)
#define FEATURE_ETH_BOOTMODE_ISCSI		(0x2 << 2)
#define FEATURE_ETH_BOOTMODE_FCOE		(0x3 << 2)
#define FEATURE_ETH_TOE_MASK			0x20
	uint32_t lso_max_size;	/* LSO MaxOffloadSize. */
	uint32_t lso_min_seg_cnt;	/* LSO MinSegmentCount. */
	/* Num Offloaded Connections TCP_IPv4. */
	uint32_t ipv4_ofld_cnt;
	/* Num Offloaded Connections TCP_IPv6. */
	uint32_t ipv6_ofld_cnt;
	uint32_t promiscuous_mode;	/* Promiscuous Mode. non-zero true */
	uint32_t txq_size;		/* TX Descriptors Queue Size */
	uint32_t rxq_size;		/* RX Descriptors Queue Size */
	/* TX Descriptor Queue Avg Depth. % Avg Queue Depth since last poll */
	uint32_t txq_avg_depth;
	/* RX Descriptors Queue Avg Depth. % Avg Queue Depth since last poll */
	uint32_t rxq_avg_depth;
	/* IOV_Offload. 0=none; 1=MultiQueue, 2=VEB 3= VEPA*/
	uint32_t iov_offload;
	/* Number of NetQueue/VMQ Config'd. */
	uint32_t netq_cnt;
	uint32_t vf_cnt;		/* Num VF assigned to this PF. */
};

/*  Per PCI Function FCOE Statistics required from the driver */
struct fcoe_stats_info {
	uint8_t version[12];		/* Function's Driver Version. */
	uint8_t mac_local[8];	/* Locally Admin Addr. */
	uint8_t mac_add1[8];		/* Additional Programmed MAC Addr 1. */
	uint8_t mac_add2[8];		/* Additional Programmed MAC Addr 2. */
	/* QoS Priority (per 802.1p). 0-7255 */
	uint32_t qos_priority;
	uint32_t txq_size;		/* FCoE TX Descriptors Queue Size. */
	uint32_t rxq_size;		/* FCoE RX Descriptors Queue Size. */
	/* FCoE TX Descriptor Queue Avg Depth. */
	uint32_t txq_avg_depth;
	/* FCoE RX Descriptors Queue Avg Depth. */
	uint32_t rxq_avg_depth;
	uint32_t rx_frames_lo;	/* FCoE RX Frames received. */
	uint32_t rx_frames_hi;	/* FCoE RX Frames received. */
	uint32_t rx_bytes_lo;	/* FCoE RX Bytes received. */
	uint32_t rx_bytes_hi;	/* FCoE RX Bytes received. */
	uint32_t tx_frames_lo;	/* FCoE TX Frames sent. */
	uint32_t tx_frames_hi;	/* FCoE TX Frames sent. */
	uint32_t tx_bytes_lo;	/* FCoE TX Bytes sent. */
	uint32_t tx_bytes_hi;	/* FCoE TX Bytes sent. */
	uint32_t rx_fcs_errors;	/* number of receive packets with FCS errors */
	uint32_t rx_fc_crc_errors;	/* number of FC frames with CRC errors*/
	uint32_t fip_login_failures;	/* number of FCoE/FIP Login failures */
};

/* Per PCI  Function iSCSI Statistics required from the driver*/
struct iscsi_stats_info {
	uint8_t version[12];		/* Function's Driver Version. */
	uint8_t mac_local[8];	/* Locally Admin iSCSI MAC Addr. */
	uint8_t mac_add1[8];		/* Additional Programmed MAC Addr 1. */
	/* QoS Priority (per 802.1p). 0-7255 */
	uint32_t qos_priority;

	uint8_t initiator_name[64];	/* iSCSI Boot Initiator Node name. */

	uint8_t ww_port_name[64];	/* iSCSI World wide port name */

	uint8_t boot_target_name[64];/* iSCSI Boot Target Name. */

	uint8_t boot_target_ip[16];	/* iSCSI Boot Target IP. */
	uint32_t boot_target_portal;	/* iSCSI Boot Target Portal. */
	uint8_t boot_init_ip[16];	/* iSCSI Boot Initiator IP Address. */
	uint32_t max_frame_size;	/* Max Frame Size. bytes */
	uint32_t txq_size;		/* PDU TX Descriptors Queue Size. */
	uint32_t rxq_size;		/* PDU RX Descriptors Queue Size. */

	uint32_t txq_avg_depth;	/*PDU TX Descriptor Queue Avg Depth. */
	uint32_t rxq_avg_depth;	/*PDU RX Descriptors Queue Avg Depth. */
	uint32_t rx_pdus_lo;		/* iSCSI PDUs received. */
	uint32_t rx_pdus_hi;		/* iSCSI PDUs received. */

	uint32_t rx_bytes_lo;	/* iSCSI RX Bytes received. */
	uint32_t rx_bytes_hi;	/* iSCSI RX Bytes received. */
	uint32_t tx_pdus_lo;		/* iSCSI PDUs sent. */
	uint32_t tx_pdus_hi;		/* iSCSI PDUs sent. */

	uint32_t tx_bytes_lo;	/* iSCSI PDU TX Bytes sent. */
	uint32_t tx_bytes_hi;	/* iSCSI PDU TX Bytes sent. */
	uint32_t pcp_prior_map_tbl;	/*C-PCP to S-PCP Priority MapTable.
				9 nibbles, the position of each nibble
				represents the C-PCP value, the value
				of the nibble = S-PCP value.*/
};

union drv_info_to_mcp {
	struct eth_stats_info		ether_stat;
	struct fcoe_stats_info		fcoe_stat;
	struct iscsi_stats_info		iscsi_stat;
};


#endif /* ECORE_MFW_REQ_H */

