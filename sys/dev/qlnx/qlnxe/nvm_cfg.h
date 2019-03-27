/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */


/****************************************************************************
 *
 * Name:        nvm_cfg.h
 *
 * Description: NVM config file - Generated file from nvm cfg excel.
 *              DO NOT MODIFY !!!
 *
 * Created:     12/4/2017
 *
 ****************************************************************************/

#ifndef NVM_CFG_H
#define NVM_CFG_H


#define NVM_CFG_version 0x83306

#define NVM_CFG_new_option_seq 26

#define NVM_CFG_removed_option_seq 2

#define NVM_CFG_updated_value_seq 5

struct nvm_cfg_mac_address
{
	u32 mac_addr_hi;
		#define NVM_CFG_MAC_ADDRESS_HI_MASK                             0x0000FFFF
		#define NVM_CFG_MAC_ADDRESS_HI_OFFSET                           0
	u32 mac_addr_lo;
};

/******************************************
 * nvm_cfg1 structs
 ******************************************/
struct nvm_cfg1_glob
{
	u32 generic_cont0;                                                  /* 0x0 */
		#define NVM_CFG1_GLOB_BOARD_SWAP_MASK                           0x0000000F
		#define NVM_CFG1_GLOB_BOARD_SWAP_OFFSET                         0
		#define NVM_CFG1_GLOB_BOARD_SWAP_NONE                           0x0
		#define NVM_CFG1_GLOB_BOARD_SWAP_PATH                           0x1
		#define NVM_CFG1_GLOB_BOARD_SWAP_PORT                           0x2
		#define NVM_CFG1_GLOB_BOARD_SWAP_BOTH                           0x3
		#define NVM_CFG1_GLOB_MF_MODE_MASK                              0x00000FF0
		#define NVM_CFG1_GLOB_MF_MODE_OFFSET                            4
		#define NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED                        0x0
		#define NVM_CFG1_GLOB_MF_MODE_DEFAULT                           0x1
		#define NVM_CFG1_GLOB_MF_MODE_SPIO4                             0x2
		#define NVM_CFG1_GLOB_MF_MODE_NPAR1_0                           0x3
		#define NVM_CFG1_GLOB_MF_MODE_NPAR1_5                           0x4
		#define NVM_CFG1_GLOB_MF_MODE_NPAR2_0                           0x5
		#define NVM_CFG1_GLOB_MF_MODE_BD                                0x6
		#define NVM_CFG1_GLOB_MF_MODE_UFP                               0x7
		#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_MASK              0x00001000
		#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_OFFSET            12
		#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_DISABLED          0x0
		#define NVM_CFG1_GLOB_FAN_FAILURE_ENFORCEMENT_ENABLED           0x1
		#define NVM_CFG1_GLOB_AVS_MARGIN_LOW_MASK                       0x001FE000
		#define NVM_CFG1_GLOB_AVS_MARGIN_LOW_OFFSET                     13
		#define NVM_CFG1_GLOB_AVS_MARGIN_HIGH_MASK                      0x1FE00000
		#define NVM_CFG1_GLOB_AVS_MARGIN_HIGH_OFFSET                    21
		#define NVM_CFG1_GLOB_ENABLE_SRIOV_MASK                         0x20000000
		#define NVM_CFG1_GLOB_ENABLE_SRIOV_OFFSET                       29
		#define NVM_CFG1_GLOB_ENABLE_SRIOV_DISABLED                     0x0
		#define NVM_CFG1_GLOB_ENABLE_SRIOV_ENABLED                      0x1
		#define NVM_CFG1_GLOB_ENABLE_ATC_MASK                           0x40000000
		#define NVM_CFG1_GLOB_ENABLE_ATC_OFFSET                         30
		#define NVM_CFG1_GLOB_ENABLE_ATC_DISABLED                       0x0
		#define NVM_CFG1_GLOB_ENABLE_ATC_ENABLED                        0x1
		#define NVM_CFG1_GLOB_RESERVED__M_WAS_CLOCK_SLOWDOWN_MASK       0x80000000
		#define NVM_CFG1_GLOB_RESERVED__M_WAS_CLOCK_SLOWDOWN_OFFSET     31
		#define NVM_CFG1_GLOB_RESERVED__M_WAS_CLOCK_SLOWDOWN_DISABLED   0x0
		#define NVM_CFG1_GLOB_RESERVED__M_WAS_CLOCK_SLOWDOWN_ENABLED    0x1
	u32 engineering_change[3];                                          /* 0x4 */
	u32 manufacturing_id;                                              /* 0x10 */
	u32 serial_number[4];                                              /* 0x14 */
	u32 pcie_cfg;                                                      /* 0x24 */
		#define NVM_CFG1_GLOB_PCI_GEN_MASK                              0x00000003
		#define NVM_CFG1_GLOB_PCI_GEN_OFFSET                            0
		#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN1                          0x0
		#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN2                          0x1
		#define NVM_CFG1_GLOB_PCI_GEN_PCI_GEN3                          0x2
		#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_MASK                   0x00000004
		#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_OFFSET                 2
		#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_DISABLED               0x0
		#define NVM_CFG1_GLOB_BEACON_WOL_ENABLED_ENABLED                0x1
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_MASK                         0x00000018
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_OFFSET                       3
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_L1_ENABLED               0x0
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_DISABLED                 0x1
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_L1_DISABLED                  0x2
		#define NVM_CFG1_GLOB_ASPM_SUPPORT_L0S_L1_DISABLED              0x3
		#define NVM_CFG1_GLOB_RESERVED_MPREVENT_PCIE_L1_MENTRY_MASK     0x00000020
		#define NVM_CFG1_GLOB_RESERVED_MPREVENT_PCIE_L1_MENTRY_OFFSET   5
		#define NVM_CFG1_GLOB_PCIE_G2_TX_AMPLITUDE_MASK                 0x000003C0
		#define NVM_CFG1_GLOB_PCIE_G2_TX_AMPLITUDE_OFFSET               6
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_MASK                     0x00001C00
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_OFFSET                   10
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_HW                       0x0
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_0DB                      0x1
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_3_5DB                    0x2
		#define NVM_CFG1_GLOB_PCIE_PREEMPHASIS_6_0DB                    0x3
		#define NVM_CFG1_GLOB_WWN_NODE_PREFIX0_MASK                     0x001FE000
		#define NVM_CFG1_GLOB_WWN_NODE_PREFIX0_OFFSET                   13
		#define NVM_CFG1_GLOB_WWN_NODE_PREFIX1_MASK                     0x1FE00000
		#define NVM_CFG1_GLOB_WWN_NODE_PREFIX1_OFFSET                   21
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_MASK                      0x60000000
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_OFFSET                    29
	/*  Set the duration, in seconds, fan failure signal should be
          sampled */
		#define NVM_CFG1_GLOB_RESERVED_FAN_FAILURE_DURATION_MASK        0x80000000
		#define NVM_CFG1_GLOB_RESERVED_FAN_FAILURE_DURATION_OFFSET      31
	u32 mgmt_traffic;                                                  /* 0x28 */
		#define NVM_CFG1_GLOB_RESERVED60_MASK                           0x00000001
		#define NVM_CFG1_GLOB_RESERVED60_OFFSET                         0
		#define NVM_CFG1_GLOB_WWN_PORT_PREFIX0_MASK                     0x000001FE
		#define NVM_CFG1_GLOB_WWN_PORT_PREFIX0_OFFSET                   1
		#define NVM_CFG1_GLOB_WWN_PORT_PREFIX1_MASK                     0x0001FE00
		#define NVM_CFG1_GLOB_WWN_PORT_PREFIX1_OFFSET                   9
		#define NVM_CFG1_GLOB_SMBUS_ADDRESS_MASK                        0x01FE0000
		#define NVM_CFG1_GLOB_SMBUS_ADDRESS_OFFSET                      17
		#define NVM_CFG1_GLOB_SIDEBAND_MODE_MASK                        0x06000000
		#define NVM_CFG1_GLOB_SIDEBAND_MODE_OFFSET                      25
		#define NVM_CFG1_GLOB_SIDEBAND_MODE_DISABLED                    0x0
		#define NVM_CFG1_GLOB_SIDEBAND_MODE_RMII                        0x1
		#define NVM_CFG1_GLOB_SIDEBAND_MODE_SGMII                       0x2
		#define NVM_CFG1_GLOB_AUX_MODE_MASK                             0x78000000
		#define NVM_CFG1_GLOB_AUX_MODE_OFFSET                           27
		#define NVM_CFG1_GLOB_AUX_MODE_DEFAULT                          0x0
		#define NVM_CFG1_GLOB_AUX_MODE_SMBUS_ONLY                       0x1
	/*  Indicates whether external thermal sonsor is available */
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_MASK              0x80000000
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_OFFSET            31
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_DISABLED          0x0
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_ENABLED           0x1
	u32 core_cfg;                                                      /* 0x2C */
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK                    0x000000FF
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET                  0
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G                0x0
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G                   0x1
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G               0x2
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F                 0x3
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E              0x4
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G                0x5
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G                   0xB
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G                   0xC
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G                   0xD
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X25G                   0xE
		#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X10G                   0xF
		#define NVM_CFG1_GLOB_MPS10_ENFORCE_TX_FIR_CFG_MASK             0x00000100
		#define NVM_CFG1_GLOB_MPS10_ENFORCE_TX_FIR_CFG_OFFSET           8
		#define NVM_CFG1_GLOB_MPS10_ENFORCE_TX_FIR_CFG_DISABLED         0x0
		#define NVM_CFG1_GLOB_MPS10_ENFORCE_TX_FIR_CFG_ENABLED          0x1
		#define NVM_CFG1_GLOB_MPS25_ENFORCE_TX_FIR_CFG_MASK             0x00000200
		#define NVM_CFG1_GLOB_MPS25_ENFORCE_TX_FIR_CFG_OFFSET           9
		#define NVM_CFG1_GLOB_MPS25_ENFORCE_TX_FIR_CFG_DISABLED         0x0
		#define NVM_CFG1_GLOB_MPS25_ENFORCE_TX_FIR_CFG_ENABLED          0x1
		#define NVM_CFG1_GLOB_MPS10_CORE_ADDR_MASK                      0x0003FC00
		#define NVM_CFG1_GLOB_MPS10_CORE_ADDR_OFFSET                    10
		#define NVM_CFG1_GLOB_MPS25_CORE_ADDR_MASK                      0x03FC0000
		#define NVM_CFG1_GLOB_MPS25_CORE_ADDR_OFFSET                    18
		#define NVM_CFG1_GLOB_AVS_MODE_MASK                             0x1C000000
		#define NVM_CFG1_GLOB_AVS_MODE_OFFSET                           26
		#define NVM_CFG1_GLOB_AVS_MODE_CLOSE_LOOP                       0x0
		#define NVM_CFG1_GLOB_AVS_MODE_OPEN_LOOP_CFG                    0x1
		#define NVM_CFG1_GLOB_AVS_MODE_OPEN_LOOP_OTP                    0x2
		#define NVM_CFG1_GLOB_AVS_MODE_DISABLED                         0x3
		#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_MASK                 0x60000000
		#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_OFFSET               29
		#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_DISABLED             0x0
		#define NVM_CFG1_GLOB_OVERRIDE_SECURE_MODE_ENABLED              0x1
		#define NVM_CFG1_GLOB_DCI_SUPPORT_MASK                          0x80000000
		#define NVM_CFG1_GLOB_DCI_SUPPORT_OFFSET                        31
		#define NVM_CFG1_GLOB_DCI_SUPPORT_DISABLED                      0x0
		#define NVM_CFG1_GLOB_DCI_SUPPORT_ENABLED                       0x1
	u32 e_lane_cfg1;                                                   /* 0x30 */
		#define NVM_CFG1_GLOB_RX_LANE0_SWAP_MASK                        0x0000000F
		#define NVM_CFG1_GLOB_RX_LANE0_SWAP_OFFSET                      0
		#define NVM_CFG1_GLOB_RX_LANE1_SWAP_MASK                        0x000000F0
		#define NVM_CFG1_GLOB_RX_LANE1_SWAP_OFFSET                      4
		#define NVM_CFG1_GLOB_RX_LANE2_SWAP_MASK                        0x00000F00
		#define NVM_CFG1_GLOB_RX_LANE2_SWAP_OFFSET                      8
		#define NVM_CFG1_GLOB_RX_LANE3_SWAP_MASK                        0x0000F000
		#define NVM_CFG1_GLOB_RX_LANE3_SWAP_OFFSET                      12
		#define NVM_CFG1_GLOB_TX_LANE0_SWAP_MASK                        0x000F0000
		#define NVM_CFG1_GLOB_TX_LANE0_SWAP_OFFSET                      16
		#define NVM_CFG1_GLOB_TX_LANE1_SWAP_MASK                        0x00F00000
		#define NVM_CFG1_GLOB_TX_LANE1_SWAP_OFFSET                      20
		#define NVM_CFG1_GLOB_TX_LANE2_SWAP_MASK                        0x0F000000
		#define NVM_CFG1_GLOB_TX_LANE2_SWAP_OFFSET                      24
		#define NVM_CFG1_GLOB_TX_LANE3_SWAP_MASK                        0xF0000000
		#define NVM_CFG1_GLOB_TX_LANE3_SWAP_OFFSET                      28
	u32 e_lane_cfg2;                                                   /* 0x34 */
		#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_MASK                    0x00000001
		#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_OFFSET                  0
		#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_MASK                    0x00000002
		#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_OFFSET                  1
		#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_MASK                    0x00000004
		#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_OFFSET                  2
		#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_MASK                    0x00000008
		#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_OFFSET                  3
		#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_MASK                    0x00000010
		#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_OFFSET                  4
		#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_MASK                    0x00000020
		#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_OFFSET                  5
		#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_MASK                    0x00000040
		#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_OFFSET                  6
		#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_MASK                    0x00000080
		#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_OFFSET                  7
		#define NVM_CFG1_GLOB_SMBUS_MODE_MASK                           0x00000F00
		#define NVM_CFG1_GLOB_SMBUS_MODE_OFFSET                         8
		#define NVM_CFG1_GLOB_SMBUS_MODE_DISABLED                       0x0
		#define NVM_CFG1_GLOB_SMBUS_MODE_100KHZ                         0x1
		#define NVM_CFG1_GLOB_SMBUS_MODE_400KHZ                         0x2
		#define NVM_CFG1_GLOB_NCSI_MASK                                 0x0000F000
		#define NVM_CFG1_GLOB_NCSI_OFFSET                               12
		#define NVM_CFG1_GLOB_NCSI_DISABLED                             0x0
		#define NVM_CFG1_GLOB_NCSI_ENABLED                              0x1
	/*  Maximum advertised pcie link width */
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_MASK                       0x000F0000
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_OFFSET                     16
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_BB_16_LANES                0x0
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_1_LANE                     0x1
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_2_LANES                    0x2
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_4_LANES                    0x3
		#define NVM_CFG1_GLOB_MAX_LINK_WIDTH_8_LANES                    0x4
	/*  ASPM L1 mode */
		#define NVM_CFG1_GLOB_ASPM_L1_MODE_MASK                         0x00300000
		#define NVM_CFG1_GLOB_ASPM_L1_MODE_OFFSET                       20
		#define NVM_CFG1_GLOB_ASPM_L1_MODE_FORCED                       0x0
		#define NVM_CFG1_GLOB_ASPM_L1_MODE_DYNAMIC_LOW_LATENCY          0x1
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_MASK                  0x01C00000
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_OFFSET                22
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_DISABLED              0x0
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_INT_EXT_I2C           0x1
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_INT_ONLY              0x2
		#define NVM_CFG1_GLOB_ON_CHIP_SENSOR_MODE_INT_EXT_SMBUS         0x3
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_MASK          0x06000000
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_OFFSET        25
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_DISABLE       0x0
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_INTERNAL      0x1
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_EXTERNAL      0x2
		#define NVM_CFG1_GLOB_TEMPERATURE_MONITORING_MODE_BOTH          0x3
	/*  Set the PLDM sensor modes */
		#define NVM_CFG1_GLOB_PLDM_SENSOR_MODE_MASK                     0x38000000
		#define NVM_CFG1_GLOB_PLDM_SENSOR_MODE_OFFSET                   27
		#define NVM_CFG1_GLOB_PLDM_SENSOR_MODE_INTERNAL                 0x0
		#define NVM_CFG1_GLOB_PLDM_SENSOR_MODE_EXTERNAL                 0x1
		#define NVM_CFG1_GLOB_PLDM_SENSOR_MODE_BOTH                     0x2
	/*  Enable VDM interface */
		#define NVM_CFG1_GLOB_PCIE_VDM_ENABLED_MASK                     0x40000000
		#define NVM_CFG1_GLOB_PCIE_VDM_ENABLED_OFFSET                   30
		#define NVM_CFG1_GLOB_PCIE_VDM_ENABLED_DISABLED                 0x0
		#define NVM_CFG1_GLOB_PCIE_VDM_ENABLED_ENABLED                  0x1
	/*  ROL enable */
		#define NVM_CFG1_GLOB_RESET_ON_LAN_MASK                         0x80000000
		#define NVM_CFG1_GLOB_RESET_ON_LAN_OFFSET                       31
		#define NVM_CFG1_GLOB_RESET_ON_LAN_DISABLED                     0x0
		#define NVM_CFG1_GLOB_RESET_ON_LAN_ENABLED                      0x1
	u32 f_lane_cfg1;                                                   /* 0x38 */
		#define NVM_CFG1_GLOB_RX_LANE0_SWAP_MASK                        0x0000000F
		#define NVM_CFG1_GLOB_RX_LANE0_SWAP_OFFSET                      0
		#define NVM_CFG1_GLOB_RX_LANE1_SWAP_MASK                        0x000000F0
		#define NVM_CFG1_GLOB_RX_LANE1_SWAP_OFFSET                      4
		#define NVM_CFG1_GLOB_RX_LANE2_SWAP_MASK                        0x00000F00
		#define NVM_CFG1_GLOB_RX_LANE2_SWAP_OFFSET                      8
		#define NVM_CFG1_GLOB_RX_LANE3_SWAP_MASK                        0x0000F000
		#define NVM_CFG1_GLOB_RX_LANE3_SWAP_OFFSET                      12
		#define NVM_CFG1_GLOB_TX_LANE0_SWAP_MASK                        0x000F0000
		#define NVM_CFG1_GLOB_TX_LANE0_SWAP_OFFSET                      16
		#define NVM_CFG1_GLOB_TX_LANE1_SWAP_MASK                        0x00F00000
		#define NVM_CFG1_GLOB_TX_LANE1_SWAP_OFFSET                      20
		#define NVM_CFG1_GLOB_TX_LANE2_SWAP_MASK                        0x0F000000
		#define NVM_CFG1_GLOB_TX_LANE2_SWAP_OFFSET                      24
		#define NVM_CFG1_GLOB_TX_LANE3_SWAP_MASK                        0xF0000000
		#define NVM_CFG1_GLOB_TX_LANE3_SWAP_OFFSET                      28
	u32 f_lane_cfg2;                                                   /* 0x3C */
		#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_MASK                    0x00000001
		#define NVM_CFG1_GLOB_RX_LANE0_POL_FLIP_OFFSET                  0
		#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_MASK                    0x00000002
		#define NVM_CFG1_GLOB_RX_LANE1_POL_FLIP_OFFSET                  1
		#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_MASK                    0x00000004
		#define NVM_CFG1_GLOB_RX_LANE2_POL_FLIP_OFFSET                  2
		#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_MASK                    0x00000008
		#define NVM_CFG1_GLOB_RX_LANE3_POL_FLIP_OFFSET                  3
		#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_MASK                    0x00000010
		#define NVM_CFG1_GLOB_TX_LANE0_POL_FLIP_OFFSET                  4
		#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_MASK                    0x00000020
		#define NVM_CFG1_GLOB_TX_LANE1_POL_FLIP_OFFSET                  5
		#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_MASK                    0x00000040
		#define NVM_CFG1_GLOB_TX_LANE2_POL_FLIP_OFFSET                  6
		#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_MASK                    0x00000080
		#define NVM_CFG1_GLOB_TX_LANE3_POL_FLIP_OFFSET                  7
	/*  Control the period between two successive checks */
		#define NVM_CFG1_GLOB_TEMPERATURE_PERIOD_BETWEEN_CHECKS_MASK    0x0000FF00
		#define NVM_CFG1_GLOB_TEMPERATURE_PERIOD_BETWEEN_CHECKS_OFFSET  8
	/*  Set shutdown temperature */
		#define NVM_CFG1_GLOB_SHUTDOWN_THRESHOLD_TEMPERATURE_MASK       0x00FF0000
		#define NVM_CFG1_GLOB_SHUTDOWN_THRESHOLD_TEMPERATURE_OFFSET     16
	/*  Set max. count for over operational temperature */
		#define NVM_CFG1_GLOB_MAX_COUNT_OPER_THRESHOLD_MASK             0xFF000000
		#define NVM_CFG1_GLOB_MAX_COUNT_OPER_THRESHOLD_OFFSET           24
	u32 mps10_preemphasis;                                             /* 0x40 */
		#define NVM_CFG1_GLOB_LANE0_PREEMP_MASK                         0x000000FF
		#define NVM_CFG1_GLOB_LANE0_PREEMP_OFFSET                       0
		#define NVM_CFG1_GLOB_LANE1_PREEMP_MASK                         0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_PREEMP_OFFSET                       8
		#define NVM_CFG1_GLOB_LANE2_PREEMP_MASK                         0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_PREEMP_OFFSET                       16
		#define NVM_CFG1_GLOB_LANE3_PREEMP_MASK                         0xFF000000
		#define NVM_CFG1_GLOB_LANE3_PREEMP_OFFSET                       24
	u32 mps10_driver_current;                                          /* 0x44 */
		#define NVM_CFG1_GLOB_LANE0_AMP_MASK                            0x000000FF
		#define NVM_CFG1_GLOB_LANE0_AMP_OFFSET                          0
		#define NVM_CFG1_GLOB_LANE1_AMP_MASK                            0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_AMP_OFFSET                          8
		#define NVM_CFG1_GLOB_LANE2_AMP_MASK                            0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_AMP_OFFSET                          16
		#define NVM_CFG1_GLOB_LANE3_AMP_MASK                            0xFF000000
		#define NVM_CFG1_GLOB_LANE3_AMP_OFFSET                          24
	u32 mps25_preemphasis;                                             /* 0x48 */
		#define NVM_CFG1_GLOB_LANE0_PREEMP_MASK                         0x000000FF
		#define NVM_CFG1_GLOB_LANE0_PREEMP_OFFSET                       0
		#define NVM_CFG1_GLOB_LANE1_PREEMP_MASK                         0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_PREEMP_OFFSET                       8
		#define NVM_CFG1_GLOB_LANE2_PREEMP_MASK                         0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_PREEMP_OFFSET                       16
		#define NVM_CFG1_GLOB_LANE3_PREEMP_MASK                         0xFF000000
		#define NVM_CFG1_GLOB_LANE3_PREEMP_OFFSET                       24
	u32 mps25_driver_current;                                          /* 0x4C */
		#define NVM_CFG1_GLOB_LANE0_AMP_MASK                            0x000000FF
		#define NVM_CFG1_GLOB_LANE0_AMP_OFFSET                          0
		#define NVM_CFG1_GLOB_LANE1_AMP_MASK                            0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_AMP_OFFSET                          8
		#define NVM_CFG1_GLOB_LANE2_AMP_MASK                            0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_AMP_OFFSET                          16
		#define NVM_CFG1_GLOB_LANE3_AMP_MASK                            0xFF000000
		#define NVM_CFG1_GLOB_LANE3_AMP_OFFSET                          24
	u32 pci_id;                                                        /* 0x50 */
		#define NVM_CFG1_GLOB_VENDOR_ID_MASK                            0x0000FFFF
		#define NVM_CFG1_GLOB_VENDOR_ID_OFFSET                          0
	/*  Set caution temperature */
		#define NVM_CFG1_GLOB_DEAD_TEMP_TH_TEMPERATURE_MASK             0x00FF0000
		#define NVM_CFG1_GLOB_DEAD_TEMP_TH_TEMPERATURE_OFFSET           16
	/*  Set external thermal sensor I2C address */
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_ADDRESS_MASK      0xFF000000
		#define NVM_CFG1_GLOB_EXTERNAL_THERMAL_SENSOR_ADDRESS_OFFSET    24
	u32 pci_subsys_id;                                                 /* 0x54 */
		#define NVM_CFG1_GLOB_SUBSYSTEM_VENDOR_ID_MASK                  0x0000FFFF
		#define NVM_CFG1_GLOB_SUBSYSTEM_VENDOR_ID_OFFSET                0
		#define NVM_CFG1_GLOB_SUBSYSTEM_DEVICE_ID_MASK                  0xFFFF0000
		#define NVM_CFG1_GLOB_SUBSYSTEM_DEVICE_ID_OFFSET                16
	u32 bar;                                                           /* 0x58 */
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_MASK                   0x0000000F
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_OFFSET                 0
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_DISABLED               0x0
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_2K                     0x1
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_4K                     0x2
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_8K                     0x3
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_16K                    0x4
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_32K                    0x5
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_64K                    0x6
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_128K                   0x7
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_256K                   0x8
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_512K                   0x9
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_1M                     0xA
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_2M                     0xB
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_4M                     0xC
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_8M                     0xD
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_16M                    0xE
		#define NVM_CFG1_GLOB_EXPANSION_ROM_SIZE_32M                    0xF
	/*  BB VF BAR2 size */
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_MASK                     0x000000F0
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_OFFSET                   4
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_DISABLED                 0x0
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_4K                       0x1
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_8K                       0x2
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_16K                      0x3
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_32K                      0x4
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_64K                      0x5
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_128K                     0x6
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_256K                     0x7
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_512K                     0x8
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_1M                       0x9
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_2M                       0xA
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_4M                       0xB
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_8M                       0xC
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_16M                      0xD
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_32M                      0xE
		#define NVM_CFG1_GLOB_VF_PCI_BAR2_SIZE_64M                      0xF
	/*  BB BAR2 size (global) */
		#define NVM_CFG1_GLOB_BAR2_SIZE_MASK                            0x00000F00
		#define NVM_CFG1_GLOB_BAR2_SIZE_OFFSET                          8
		#define NVM_CFG1_GLOB_BAR2_SIZE_DISABLED                        0x0
		#define NVM_CFG1_GLOB_BAR2_SIZE_64K                             0x1
		#define NVM_CFG1_GLOB_BAR2_SIZE_128K                            0x2
		#define NVM_CFG1_GLOB_BAR2_SIZE_256K                            0x3
		#define NVM_CFG1_GLOB_BAR2_SIZE_512K                            0x4
		#define NVM_CFG1_GLOB_BAR2_SIZE_1M                              0x5
		#define NVM_CFG1_GLOB_BAR2_SIZE_2M                              0x6
		#define NVM_CFG1_GLOB_BAR2_SIZE_4M                              0x7
		#define NVM_CFG1_GLOB_BAR2_SIZE_8M                              0x8
		#define NVM_CFG1_GLOB_BAR2_SIZE_16M                             0x9
		#define NVM_CFG1_GLOB_BAR2_SIZE_32M                             0xA
		#define NVM_CFG1_GLOB_BAR2_SIZE_64M                             0xB
		#define NVM_CFG1_GLOB_BAR2_SIZE_128M                            0xC
		#define NVM_CFG1_GLOB_BAR2_SIZE_256M                            0xD
		#define NVM_CFG1_GLOB_BAR2_SIZE_512M                            0xE
		#define NVM_CFG1_GLOB_BAR2_SIZE_1G                              0xF
	/*  Set the duration, in seconds, fan failure signal should be
          sampled */
		#define NVM_CFG1_GLOB_FAN_FAILURE_DURATION_MASK                 0x0000F000
		#define NVM_CFG1_GLOB_FAN_FAILURE_DURATION_OFFSET               12
	/*  This field defines the board total budget  for bar2 when disabled
          the regular bar size is used. */
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_MASK                    0x00FF0000
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_OFFSET                  16
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_DISABLED                0x0
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_64K                     0x1
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_128K                    0x2
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_256K                    0x3
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_512K                    0x4
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_1M                      0x5
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_2M                      0x6
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_4M                      0x7
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_8M                      0x8
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_16M                     0x9
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_32M                     0xA
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_64M                     0xB
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_128M                    0xC
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_256M                    0xD
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_512M                    0xE
		#define NVM_CFG1_GLOB_BAR2_TOTAL_BUDGET_1G                      0xF
	/*  Enable/Disable Crash dump triggers */
		#define NVM_CFG1_GLOB_CRASH_DUMP_TRIGGER_ENABLE_MASK            0xFF000000
		#define NVM_CFG1_GLOB_CRASH_DUMP_TRIGGER_ENABLE_OFFSET          24
	u32 mps10_txfir_main;                                              /* 0x5C */
		#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_MASK                     0x000000FF
		#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_OFFSET                   0
		#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_MASK                     0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_OFFSET                   8
		#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_MASK                     0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_OFFSET                   16
		#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_MASK                     0xFF000000
		#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_OFFSET                   24
	u32 mps10_txfir_post;                                              /* 0x60 */
		#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_MASK                     0x000000FF
		#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_OFFSET                   0
		#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_MASK                     0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_OFFSET                   8
		#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_MASK                     0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_OFFSET                   16
		#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_MASK                     0xFF000000
		#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_OFFSET                   24
	u32 mps25_txfir_main;                                              /* 0x64 */
		#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_MASK                     0x000000FF
		#define NVM_CFG1_GLOB_LANE0_TXFIR_MAIN_OFFSET                   0
		#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_MASK                     0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_TXFIR_MAIN_OFFSET                   8
		#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_MASK                     0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_TXFIR_MAIN_OFFSET                   16
		#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_MASK                     0xFF000000
		#define NVM_CFG1_GLOB_LANE3_TXFIR_MAIN_OFFSET                   24
	u32 mps25_txfir_post;                                              /* 0x68 */
		#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_MASK                     0x000000FF
		#define NVM_CFG1_GLOB_LANE0_TXFIR_POST_OFFSET                   0
		#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_MASK                     0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_TXFIR_POST_OFFSET                   8
		#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_MASK                     0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_TXFIR_POST_OFFSET                   16
		#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_MASK                     0xFF000000
		#define NVM_CFG1_GLOB_LANE3_TXFIR_POST_OFFSET                   24
	u32 manufacture_ver;                                               /* 0x6C */
		#define NVM_CFG1_GLOB_MANUF0_VER_MASK                           0x0000003F
		#define NVM_CFG1_GLOB_MANUF0_VER_OFFSET                         0
		#define NVM_CFG1_GLOB_MANUF1_VER_MASK                           0x00000FC0
		#define NVM_CFG1_GLOB_MANUF1_VER_OFFSET                         6
		#define NVM_CFG1_GLOB_MANUF2_VER_MASK                           0x0003F000
		#define NVM_CFG1_GLOB_MANUF2_VER_OFFSET                         12
		#define NVM_CFG1_GLOB_MANUF3_VER_MASK                           0x00FC0000
		#define NVM_CFG1_GLOB_MANUF3_VER_OFFSET                         18
		#define NVM_CFG1_GLOB_MANUF4_VER_MASK                           0x3F000000
		#define NVM_CFG1_GLOB_MANUF4_VER_OFFSET                         24
	/*  Select package id method */
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_IO_MASK                   0x40000000
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_IO_OFFSET                 30
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_IO_NVRAM                  0x0
		#define NVM_CFG1_GLOB_NCSI_PACKAGE_ID_IO_IO_PINS                0x1
		#define NVM_CFG1_GLOB_RECOVERY_MODE_MASK                        0x80000000
		#define NVM_CFG1_GLOB_RECOVERY_MODE_OFFSET                      31
		#define NVM_CFG1_GLOB_RECOVERY_MODE_DISABLED                    0x0
		#define NVM_CFG1_GLOB_RECOVERY_MODE_ENABLED                     0x1
	u32 manufacture_time;                                              /* 0x70 */
		#define NVM_CFG1_GLOB_MANUF0_TIME_MASK                          0x0000003F
		#define NVM_CFG1_GLOB_MANUF0_TIME_OFFSET                        0
		#define NVM_CFG1_GLOB_MANUF1_TIME_MASK                          0x00000FC0
		#define NVM_CFG1_GLOB_MANUF1_TIME_OFFSET                        6
		#define NVM_CFG1_GLOB_MANUF2_TIME_MASK                          0x0003F000
		#define NVM_CFG1_GLOB_MANUF2_TIME_OFFSET                        12
	/*  Max MSIX for Ethernet in default mode */
		#define NVM_CFG1_GLOB_MAX_MSIX_MASK                             0x03FC0000
		#define NVM_CFG1_GLOB_MAX_MSIX_OFFSET                           18
	/*  PF Mapping */
		#define NVM_CFG1_GLOB_PF_MAPPING_MASK                           0x0C000000
		#define NVM_CFG1_GLOB_PF_MAPPING_OFFSET                         26
		#define NVM_CFG1_GLOB_PF_MAPPING_CONTINUOUS                     0x0
		#define NVM_CFG1_GLOB_PF_MAPPING_FIXED                          0x1
		#define NVM_CFG1_GLOB_VOLTAGE_REGULATOR_TYPE_MASK               0x30000000
		#define NVM_CFG1_GLOB_VOLTAGE_REGULATOR_TYPE_OFFSET             28
		#define NVM_CFG1_GLOB_VOLTAGE_REGULATOR_TYPE_DISABLED           0x0
		#define NVM_CFG1_GLOB_VOLTAGE_REGULATOR_TYPE_TI                 0x1
	/*  Enable/Disable PCIE Relaxed Ordering */
		#define NVM_CFG1_GLOB_PCIE_RELAXED_ORDERING_MASK                0x40000000
		#define NVM_CFG1_GLOB_PCIE_RELAXED_ORDERING_OFFSET              30
		#define NVM_CFG1_GLOB_PCIE_RELAXED_ORDERING_DISABLED            0x0
		#define NVM_CFG1_GLOB_PCIE_RELAXED_ORDERING_ENABLED             0x1
	u32 led_global_settings;                                           /* 0x74 */
		#define NVM_CFG1_GLOB_LED_SWAP_0_MASK                           0x0000000F
		#define NVM_CFG1_GLOB_LED_SWAP_0_OFFSET                         0
		#define NVM_CFG1_GLOB_LED_SWAP_1_MASK                           0x000000F0
		#define NVM_CFG1_GLOB_LED_SWAP_1_OFFSET                         4
		#define NVM_CFG1_GLOB_LED_SWAP_2_MASK                           0x00000F00
		#define NVM_CFG1_GLOB_LED_SWAP_2_OFFSET                         8
		#define NVM_CFG1_GLOB_LED_SWAP_3_MASK                           0x0000F000
		#define NVM_CFG1_GLOB_LED_SWAP_3_OFFSET                         12
	/*  Max. continues operating temperature */
		#define NVM_CFG1_GLOB_MAX_CONT_OPERATING_TEMP_MASK              0x00FF0000
		#define NVM_CFG1_GLOB_MAX_CONT_OPERATING_TEMP_OFFSET            16
	/*  GPIO which triggers run-time port swap according to the map
          specified in option 205 */
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_MASK               0xFF000000
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_OFFSET             24
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_NA                 0x0
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO0              0x1
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO1              0x2
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO2              0x3
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO3              0x4
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO4              0x5
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO5              0x6
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO6              0x7
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO7              0x8
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO8              0x9
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO9              0xA
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO10             0xB
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO11             0xC
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO12             0xD
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO13             0xE
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO14             0xF
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO15             0x10
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO16             0x11
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO17             0x12
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO18             0x13
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO19             0x14
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO20             0x15
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO21             0x16
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO22             0x17
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO23             0x18
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO24             0x19
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO25             0x1A
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO26             0x1B
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO27             0x1C
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO28             0x1D
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO29             0x1E
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO30             0x1F
		#define NVM_CFG1_GLOB_RUNTIME_PORT_SWAP_GPIO_GPIO31             0x20
	u32 generic_cont1;                                                 /* 0x78 */
		#define NVM_CFG1_GLOB_AVS_DAC_CODE_MASK                         0x000003FF
		#define NVM_CFG1_GLOB_AVS_DAC_CODE_OFFSET                       0
		#define NVM_CFG1_GLOB_LANE0_SWAP_MASK                           0x00000C00
		#define NVM_CFG1_GLOB_LANE0_SWAP_OFFSET                         10
		#define NVM_CFG1_GLOB_LANE1_SWAP_MASK                           0x00003000
		#define NVM_CFG1_GLOB_LANE1_SWAP_OFFSET                         12
		#define NVM_CFG1_GLOB_LANE2_SWAP_MASK                           0x0000C000
		#define NVM_CFG1_GLOB_LANE2_SWAP_OFFSET                         14
		#define NVM_CFG1_GLOB_LANE3_SWAP_MASK                           0x00030000
		#define NVM_CFG1_GLOB_LANE3_SWAP_OFFSET                         16
	/*  Enable option 195 - Overriding the PCIe Preset value */
		#define NVM_CFG1_GLOB_OVERRIDE_PCIE_PRESET_EQUAL_MASK           0x00040000
		#define NVM_CFG1_GLOB_OVERRIDE_PCIE_PRESET_EQUAL_OFFSET         18
		#define NVM_CFG1_GLOB_OVERRIDE_PCIE_PRESET_EQUAL_DISABLED       0x0
		#define NVM_CFG1_GLOB_OVERRIDE_PCIE_PRESET_EQUAL_ENABLED        0x1
	/*  PCIe Preset value - applies only if option 194 is enabled */
		#define NVM_CFG1_GLOB_PCIE_PRESET_VALUE_MASK                    0x00780000
		#define NVM_CFG1_GLOB_PCIE_PRESET_VALUE_OFFSET                  19
	/*  Port mapping to be used when the run-time GPIO for port-swap is
          defined and set. */
		#define NVM_CFG1_GLOB_RUNTIME_PORT0_SWAP_MAP_MASK               0x01800000
		#define NVM_CFG1_GLOB_RUNTIME_PORT0_SWAP_MAP_OFFSET             23
		#define NVM_CFG1_GLOB_RUNTIME_PORT1_SWAP_MAP_MASK               0x06000000
		#define NVM_CFG1_GLOB_RUNTIME_PORT1_SWAP_MAP_OFFSET             25
		#define NVM_CFG1_GLOB_RUNTIME_PORT2_SWAP_MAP_MASK               0x18000000
		#define NVM_CFG1_GLOB_RUNTIME_PORT2_SWAP_MAP_OFFSET             27
		#define NVM_CFG1_GLOB_RUNTIME_PORT3_SWAP_MAP_MASK               0x60000000
		#define NVM_CFG1_GLOB_RUNTIME_PORT3_SWAP_MAP_OFFSET             29
	u32 mbi_version;                                                   /* 0x7C */
		#define NVM_CFG1_GLOB_MBI_VERSION_0_MASK                        0x000000FF
		#define NVM_CFG1_GLOB_MBI_VERSION_0_OFFSET                      0
		#define NVM_CFG1_GLOB_MBI_VERSION_1_MASK                        0x0000FF00
		#define NVM_CFG1_GLOB_MBI_VERSION_1_OFFSET                      8
		#define NVM_CFG1_GLOB_MBI_VERSION_2_MASK                        0x00FF0000
		#define NVM_CFG1_GLOB_MBI_VERSION_2_OFFSET                      16
	/*  If set to other than NA, 0 - Normal operation, 1 - Thermal event
          occurred */
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_MASK                   0xFF000000
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_OFFSET                 24
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_NA                     0x0
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO0                  0x1
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO1                  0x2
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO2                  0x3
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO3                  0x4
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO4                  0x5
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO5                  0x6
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO6                  0x7
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO7                  0x8
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO8                  0x9
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO9                  0xA
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO10                 0xB
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO11                 0xC
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO12                 0xD
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO13                 0xE
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO14                 0xF
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO15                 0x10
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO16                 0x11
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO17                 0x12
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO18                 0x13
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO19                 0x14
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO20                 0x15
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO21                 0x16
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO22                 0x17
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO23                 0x18
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO24                 0x19
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO25                 0x1A
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO26                 0x1B
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO27                 0x1C
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO28                 0x1D
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO29                 0x1E
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO30                 0x1F
		#define NVM_CFG1_GLOB_THERMAL_EVENT_GPIO_GPIO31                 0x20
	u32 mbi_date;                                                      /* 0x80 */
	u32 misc_sig;                                                      /* 0x84 */
	/*  Define the GPIO mapping to switch i2c mux */
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_0_MASK                   0x000000FF
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_0_OFFSET                 0
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_1_MASK                   0x0000FF00
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO_1_OFFSET                 8
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__NA                      0x0
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO0                   0x1
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO1                   0x2
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO2                   0x3
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO3                   0x4
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO4                   0x5
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO5                   0x6
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO6                   0x7
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO7                   0x8
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO8                   0x9
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO9                   0xA
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO10                  0xB
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO11                  0xC
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO12                  0xD
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO13                  0xE
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO14                  0xF
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO15                  0x10
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO16                  0x11
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO17                  0x12
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO18                  0x13
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO19                  0x14
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO20                  0x15
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO21                  0x16
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO22                  0x17
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO23                  0x18
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO24                  0x19
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO25                  0x1A
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO26                  0x1B
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO27                  0x1C
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO28                  0x1D
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO29                  0x1E
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO30                  0x1F
		#define NVM_CFG1_GLOB_I2C_MUX_SEL_GPIO__GPIO31                  0x20
	/*  Interrupt signal used for SMBus/I2C management interface
        
           0 = Interrupt event occurred
          1 = Normal
           */
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_MASK                   0x00FF0000
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_OFFSET                 16
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_NA                     0x0
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO0                  0x1
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO1                  0x2
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO2                  0x3
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO3                  0x4
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO4                  0x5
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO5                  0x6
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO6                  0x7
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO7                  0x8
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO8                  0x9
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO9                  0xA
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO10                 0xB
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO11                 0xC
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO12                 0xD
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO13                 0xE
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO14                 0xF
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO15                 0x10
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO16                 0x11
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO17                 0x12
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO18                 0x13
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO19                 0x14
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO20                 0x15
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO21                 0x16
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO22                 0x17
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO23                 0x18
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO24                 0x19
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO25                 0x1A
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO26                 0x1B
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO27                 0x1C
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO28                 0x1D
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO29                 0x1E
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO30                 0x1F
		#define NVM_CFG1_GLOB_I2C_INTERRUPT_GPIO_GPIO31                 0x20
	/*  Set aLOM FAN on GPIO */
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_MASK                 0xFF000000
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_OFFSET               24
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_NA                   0x0
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO0                0x1
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO1                0x2
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO2                0x3
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO3                0x4
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO4                0x5
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO5                0x6
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO6                0x7
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO7                0x8
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO8                0x9
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO9                0xA
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO10               0xB
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO11               0xC
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO12               0xD
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO13               0xE
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO14               0xF
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO15               0x10
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO16               0x11
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO17               0x12
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO18               0x13
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO19               0x14
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO20               0x15
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO21               0x16
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO22               0x17
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO23               0x18
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO24               0x19
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO25               0x1A
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO26               0x1B
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO27               0x1C
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO28               0x1D
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO29               0x1E
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO30               0x1F
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_GPIO_GPIO31               0x20
	u32 device_capabilities;                                           /* 0x88 */
		#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET              0x1
		#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_FCOE                  0x2
		#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI                 0x4
		#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE                  0x8
		#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_IWARP                 0x10
	u32 power_dissipated;                                              /* 0x8C */
		#define NVM_CFG1_GLOB_POWER_DIS_D0_MASK                         0x000000FF
		#define NVM_CFG1_GLOB_POWER_DIS_D0_OFFSET                       0
		#define NVM_CFG1_GLOB_POWER_DIS_D1_MASK                         0x0000FF00
		#define NVM_CFG1_GLOB_POWER_DIS_D1_OFFSET                       8
		#define NVM_CFG1_GLOB_POWER_DIS_D2_MASK                         0x00FF0000
		#define NVM_CFG1_GLOB_POWER_DIS_D2_OFFSET                       16
		#define NVM_CFG1_GLOB_POWER_DIS_D3_MASK                         0xFF000000
		#define NVM_CFG1_GLOB_POWER_DIS_D3_OFFSET                       24
	u32 power_consumed;                                                /* 0x90 */
		#define NVM_CFG1_GLOB_POWER_CONS_D0_MASK                        0x000000FF
		#define NVM_CFG1_GLOB_POWER_CONS_D0_OFFSET                      0
		#define NVM_CFG1_GLOB_POWER_CONS_D1_MASK                        0x0000FF00
		#define NVM_CFG1_GLOB_POWER_CONS_D1_OFFSET                      8
		#define NVM_CFG1_GLOB_POWER_CONS_D2_MASK                        0x00FF0000
		#define NVM_CFG1_GLOB_POWER_CONS_D2_OFFSET                      16
		#define NVM_CFG1_GLOB_POWER_CONS_D3_MASK                        0xFF000000
		#define NVM_CFG1_GLOB_POWER_CONS_D3_OFFSET                      24
	u32 efi_version;                                                   /* 0x94 */
	u32 multi_network_modes_capability;                                /* 0x98 */
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_4X10G      0x1
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_1X25G      0x2
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_2X25G      0x4
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_4X25G      0x8
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_1X40G      0x10
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_2X40G      0x20
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_2X50G      0x40
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_BB_1X100G  0x80
		#define NVM_CFG1_GLOB_MULTI_NETWORK_MODES_CAPABILITY_2X10G      0x100
	u32 nvm_cfg_version;                                               /* 0x9C */
	u32 nvm_cfg_new_option_seq;                                        /* 0xA0 */
	u32 nvm_cfg_removed_option_seq;                                    /* 0xA4 */
	u32 nvm_cfg_updated_value_seq;                                     /* 0xA8 */
	u32 extended_serial_number[8];                                     /* 0xAC */
	u32 oem1_number[8];                                                /* 0xCC */
	u32 oem2_number[8];                                                /* 0xEC */
	u32 mps25_active_txfir_pre;                                       /* 0x10C */
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_PRE_MASK                  0x000000FF
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_PRE_OFFSET                0
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_PRE_MASK                  0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_PRE_OFFSET                8
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_PRE_MASK                  0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_PRE_OFFSET                16
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_PRE_MASK                  0xFF000000
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_PRE_OFFSET                24
	u32 mps25_active_txfir_main;                                      /* 0x110 */
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_MAIN_MASK                 0x000000FF
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_MAIN_OFFSET               0
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_MAIN_MASK                 0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_MAIN_OFFSET               8
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_MAIN_MASK                 0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_MAIN_OFFSET               16
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_MAIN_MASK                 0xFF000000
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_MAIN_OFFSET               24
	u32 mps25_active_txfir_post;                                      /* 0x114 */
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_POST_MASK                 0x000000FF
		#define NVM_CFG1_GLOB_LANE0_ACT_TXFIR_POST_OFFSET               0
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_POST_MASK                 0x0000FF00
		#define NVM_CFG1_GLOB_LANE1_ACT_TXFIR_POST_OFFSET               8
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_POST_MASK                 0x00FF0000
		#define NVM_CFG1_GLOB_LANE2_ACT_TXFIR_POST_OFFSET               16
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_POST_MASK                 0xFF000000
		#define NVM_CFG1_GLOB_LANE3_ACT_TXFIR_POST_OFFSET               24
	u32 features;                                                     /* 0x118 */
	/*  Set the Aux Fan on temperature  */
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_VALUE_MASK                0x000000FF
		#define NVM_CFG1_GLOB_ALOM_FAN_ON_AUX_VALUE_OFFSET              0
	/*  Set NC-SI package ID */
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_MASK                         0x0000FF00
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_OFFSET                       8
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_NA                           0x0
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO0                        0x1
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO1                        0x2
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO2                        0x3
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO3                        0x4
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO4                        0x5
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO5                        0x6
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO6                        0x7
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO7                        0x8
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO8                        0x9
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO9                        0xA
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO10                       0xB
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO11                       0xC
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO12                       0xD
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO13                       0xE
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO14                       0xF
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO15                       0x10
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO16                       0x11
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO17                       0x12
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO18                       0x13
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO19                       0x14
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO20                       0x15
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO21                       0x16
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO22                       0x17
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO23                       0x18
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO24                       0x19
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO25                       0x1A
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO26                       0x1B
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO27                       0x1C
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO28                       0x1D
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO29                       0x1E
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO30                       0x1F
		#define NVM_CFG1_GLOB_SLOT_ID_GPIO_GPIO31                       0x20
	/*  PMBUS Clock GPIO */
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_MASK                       0x00FF0000
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_OFFSET                     16
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_NA                         0x0
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO0                      0x1
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO1                      0x2
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO2                      0x3
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO3                      0x4
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO4                      0x5
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO5                      0x6
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO6                      0x7
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO7                      0x8
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO8                      0x9
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO9                      0xA
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO10                     0xB
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO11                     0xC
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO12                     0xD
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO13                     0xE
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO14                     0xF
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO15                     0x10
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO16                     0x11
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO17                     0x12
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO18                     0x13
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO19                     0x14
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO20                     0x15
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO21                     0x16
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO22                     0x17
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO23                     0x18
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO24                     0x19
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO25                     0x1A
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO26                     0x1B
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO27                     0x1C
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO28                     0x1D
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO29                     0x1E
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO30                     0x1F
		#define NVM_CFG1_GLOB_PMBUS_SCL_GPIO_GPIO31                     0x20
	/*  PMBUS Data GPIO */
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_MASK                       0xFF000000
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_OFFSET                     24
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_NA                         0x0
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO0                      0x1
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO1                      0x2
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO2                      0x3
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO3                      0x4
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO4                      0x5
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO5                      0x6
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO6                      0x7
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO7                      0x8
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO8                      0x9
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO9                      0xA
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO10                     0xB
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO11                     0xC
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO12                     0xD
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO13                     0xE
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO14                     0xF
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO15                     0x10
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO16                     0x11
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO17                     0x12
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO18                     0x13
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO19                     0x14
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO20                     0x15
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO21                     0x16
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO22                     0x17
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO23                     0x18
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO24                     0x19
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO25                     0x1A
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO26                     0x1B
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO27                     0x1C
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO28                     0x1D
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO29                     0x1E
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO30                     0x1F
		#define NVM_CFG1_GLOB_PMBUS_SDA_GPIO_GPIO31                     0x20
	u32 tx_rx_eq_25g_hlpc;                                            /* 0x11C */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_HLPC_MASK             0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_HLPC_OFFSET           0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_HLPC_MASK             0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_HLPC_OFFSET           8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_HLPC_MASK             0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_HLPC_OFFSET           16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_HLPC_MASK             0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_HLPC_OFFSET           24
	u32 tx_rx_eq_25g_llpc;                                            /* 0x120 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_LLPC_MASK             0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_LLPC_OFFSET           0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_LLPC_MASK             0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_LLPC_OFFSET           8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_LLPC_MASK             0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_LLPC_OFFSET           16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_LLPC_MASK             0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_LLPC_OFFSET           24
	u32 tx_rx_eq_25g_ac;                                              /* 0x124 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_AC_MASK               0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_AC_OFFSET             0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_AC_MASK               0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_AC_OFFSET             8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_AC_MASK               0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_AC_OFFSET             16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_AC_MASK               0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_AC_OFFSET             24
	u32 tx_rx_eq_10g_pc;                                              /* 0x128 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_PC_MASK               0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_PC_OFFSET             0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_PC_MASK               0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_PC_OFFSET             8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_PC_MASK               0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_PC_OFFSET             16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_PC_MASK               0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_PC_OFFSET             24
	u32 tx_rx_eq_10g_ac;                                              /* 0x12C */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_AC_MASK               0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_AC_OFFSET             0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_AC_MASK               0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_AC_OFFSET             8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_AC_MASK               0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_AC_OFFSET             16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_AC_MASK               0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_AC_OFFSET             24
	u32 tx_rx_eq_1g;                                                  /* 0x130 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_1G_MASK                   0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_1G_OFFSET                 0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_1G_MASK                   0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_1G_OFFSET                 8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_1G_MASK                   0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_1G_OFFSET                 16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_1G_MASK                   0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_1G_OFFSET                 24
	u32 tx_rx_eq_25g_bt;                                              /* 0x134 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_BT_MASK               0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_25G_BT_OFFSET             0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_BT_MASK               0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_25G_BT_OFFSET             8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_BT_MASK               0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_25G_BT_OFFSET             16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_BT_MASK               0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_25G_BT_OFFSET             24
	u32 tx_rx_eq_10g_bt;                                              /* 0x138 */
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_BT_MASK               0x000000FF
		#define NVM_CFG1_GLOB_INDEX0_RX_TX_EQ_10G_BT_OFFSET             0
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_BT_MASK               0x0000FF00
		#define NVM_CFG1_GLOB_INDEX1_RX_TX_EQ_10G_BT_OFFSET             8
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_BT_MASK               0x00FF0000
		#define NVM_CFG1_GLOB_INDEX2_RX_TX_EQ_10G_BT_OFFSET             16
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_BT_MASK               0xFF000000
		#define NVM_CFG1_GLOB_INDEX3_RX_TX_EQ_10G_BT_OFFSET             24
	u32 generic_cont4;                                                /* 0x13C */
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_MASK                   0x000000FF
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_OFFSET                 0
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_NA                     0x0
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO0                  0x1
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO1                  0x2
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO2                  0x3
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO3                  0x4
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO4                  0x5
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO5                  0x6
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO6                  0x7
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO7                  0x8
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO8                  0x9
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO9                  0xA
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO10                 0xB
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO11                 0xC
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO12                 0xD
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO13                 0xE
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO14                 0xF
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO15                 0x10
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO16                 0x11
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO17                 0x12
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO18                 0x13
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO19                 0x14
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO20                 0x15
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO21                 0x16
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO22                 0x17
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO23                 0x18
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO24                 0x19
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO25                 0x1A
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO26                 0x1B
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO27                 0x1C
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO28                 0x1D
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO29                 0x1E
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO30                 0x1F
		#define NVM_CFG1_GLOB_THERMAL_ALARM_GPIO_GPIO31                 0x20
	u32 preboot_debug_mode_std;                                       /* 0x140 */
	u32 preboot_debug_mode_ext;                                       /* 0x144 */
	u32 ext_phy_cfg1;                                                 /* 0x148 */
	/*  Ext PHY MDI pair swap value */
		#define NVM_CFG1_GLOB_RESERVED_244_MASK                         0x0000FFFF
		#define NVM_CFG1_GLOB_RESERVED_244_OFFSET                       0
	u32 clocks;                                                       /* 0x14C */
	/*  Sets core clock frequency */
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MASK                 0x000000FF
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_OFFSET               0
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_DEFAULT     0x0
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_375         0x1
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_350         0x2
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_325         0x3
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_300         0x4
		#define NVM_CFG1_GLOB_MAIN_CLOCK_FREQUENCY_MAIN_CLK_280         0x5
	/*  Sets MAC clock frequency */
		#define NVM_CFG1_GLOB_MAC_CLOCK_FREQUENCY_MASK                  0x0000FF00
		#define NVM_CFG1_GLOB_MAC_CLOCK_FREQUENCY_OFFSET                8
		#define NVM_CFG1_GLOB_MAC_CLOCK_FREQUENCY_MAC_CLK_DEFAULT       0x0
		#define NVM_CFG1_GLOB_MAC_CLOCK_FREQUENCY_MAC_CLK_782           0x1
		#define NVM_CFG1_GLOB_MAC_CLOCK_FREQUENCY_MAC_CLK_516           0x2
	/*  Sets storm clock frequency */
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_MASK                0x00FF0000
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_OFFSET              16
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_STORM_CLK_DEFAULT   0x0
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_STORM_CLK_1200      0x1
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_STORM_CLK_1000      0x2
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_STORM_CLK_900       0x3
		#define NVM_CFG1_GLOB_STORM_CLOCK_FREQUENCY_STORM_CLK_1100      0x4
	u32 reserved[54];                                                 /* 0x150 */
};

struct nvm_cfg1_path
{
	u32 reserved[1];                                                    /* 0x0 */
};

struct nvm_cfg1_port
{
	u32 reserved__m_relocated_to_option_123;                            /* 0x0 */
	u32 reserved__m_relocated_to_option_124;                            /* 0x4 */
	u32 generic_cont0;                                                  /* 0x8 */
		#define NVM_CFG1_PORT_LED_MODE_MASK                             0x000000FF
		#define NVM_CFG1_PORT_LED_MODE_OFFSET                           0
		#define NVM_CFG1_PORT_LED_MODE_MAC1                             0x0
		#define NVM_CFG1_PORT_LED_MODE_PHY1                             0x1
		#define NVM_CFG1_PORT_LED_MODE_PHY2                             0x2
		#define NVM_CFG1_PORT_LED_MODE_PHY3                             0x3
		#define NVM_CFG1_PORT_LED_MODE_MAC2                             0x4
		#define NVM_CFG1_PORT_LED_MODE_PHY4                             0x5
		#define NVM_CFG1_PORT_LED_MODE_PHY5                             0x6
		#define NVM_CFG1_PORT_LED_MODE_PHY6                             0x7
		#define NVM_CFG1_PORT_LED_MODE_MAC3                             0x8
		#define NVM_CFG1_PORT_LED_MODE_PHY7                             0x9
		#define NVM_CFG1_PORT_LED_MODE_PHY8                             0xA
		#define NVM_CFG1_PORT_LED_MODE_PHY9                             0xB
		#define NVM_CFG1_PORT_LED_MODE_MAC4                             0xC
		#define NVM_CFG1_PORT_LED_MODE_PHY10                            0xD
		#define NVM_CFG1_PORT_LED_MODE_PHY11                            0xE
		#define NVM_CFG1_PORT_LED_MODE_PHY12                            0xF
		#define NVM_CFG1_PORT_LED_MODE_BREAKOUT                         0x10
		#define NVM_CFG1_PORT_ROCE_PRIORITY_MASK                        0x0000FF00
		#define NVM_CFG1_PORT_ROCE_PRIORITY_OFFSET                      8
		#define NVM_CFG1_PORT_DCBX_MODE_MASK                            0x000F0000
		#define NVM_CFG1_PORT_DCBX_MODE_OFFSET                          16
		#define NVM_CFG1_PORT_DCBX_MODE_DISABLED                        0x0
		#define NVM_CFG1_PORT_DCBX_MODE_IEEE                            0x1
		#define NVM_CFG1_PORT_DCBX_MODE_CEE                             0x2
		#define NVM_CFG1_PORT_DCBX_MODE_DYNAMIC                         0x3
		#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_MASK            0x00F00000
		#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_OFFSET          20
		#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ETHERNET        0x1
		#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_FCOE            0x2
		#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ISCSI           0x4
	/*  GPIO for HW reset the PHY. In case it is the same for all ports,
          need to set same value for all ports */
		#define NVM_CFG1_PORT_EXT_PHY_RESET_MASK                        0xFF000000
		#define NVM_CFG1_PORT_EXT_PHY_RESET_OFFSET                      24
		#define NVM_CFG1_PORT_EXT_PHY_RESET_NA                          0x0
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO0                       0x1
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO1                       0x2
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO2                       0x3
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO3                       0x4
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO4                       0x5
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO5                       0x6
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO6                       0x7
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO7                       0x8
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO8                       0x9
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO9                       0xA
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO10                      0xB
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO11                      0xC
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO12                      0xD
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO13                      0xE
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO14                      0xF
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO15                      0x10
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO16                      0x11
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO17                      0x12
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO18                      0x13
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO19                      0x14
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO20                      0x15
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO21                      0x16
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO22                      0x17
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO23                      0x18
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO24                      0x19
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO25                      0x1A
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO26                      0x1B
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO27                      0x1C
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO28                      0x1D
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO29                      0x1E
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO30                      0x1F
		#define NVM_CFG1_PORT_EXT_PHY_RESET_GPIO31                      0x20
	u32 pcie_cfg;                                                       /* 0xC */
		#define NVM_CFG1_PORT_RESERVED15_MASK                           0x00000007
		#define NVM_CFG1_PORT_RESERVED15_OFFSET                         0
	u32 features;                                                      /* 0x10 */
		#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_MASK           0x00000001
		#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_OFFSET         0
		#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_DISABLED       0x0
		#define NVM_CFG1_PORT_ENABLE_WOL_ON_ACPI_PATTERN_ENABLED        0x1
		#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_MASK                     0x00000002
		#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_OFFSET                   1
		#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_DISABLED                 0x0
		#define NVM_CFG1_PORT_MAGIC_PACKET_WOL_ENABLED                  0x1
	u32 speed_cap_mask;                                                /* 0x14 */
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK            0x0000FFFF
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_OFFSET          0
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G              0x1
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G             0x2
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G             0x4
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G             0x8
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G             0x10
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G             0x20
		#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G         0x40
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_MASK            0xFFFF0000
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_OFFSET          16
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_1G              0x1
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_10G             0x2
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_20G             0x4
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_25G             0x8
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_40G             0x10
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_50G             0x20
		#define NVM_CFG1_PORT_MFW_SPEED_CAPABILITY_MASK_BB_100G         0x40
	u32 link_settings;                                                 /* 0x18 */
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_MASK                       0x0000000F
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET                     0
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG                    0x0
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_1G                         0x1
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_10G                        0x2
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_20G                        0x3
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_25G                        0x4
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_40G                        0x5
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_50G                        0x6
		#define NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G                    0x7
		#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK                     0x00000070
		#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET                   4
		#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG                  0x1
		#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX                       0x2
		#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX                       0x4
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_MASK                       0x00000780
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_OFFSET                     7
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_AUTONEG                    0x0
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_1G                         0x1
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_10G                        0x2
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_20G                        0x3
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_25G                        0x4
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_40G                        0x5
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_50G                        0x6
		#define NVM_CFG1_PORT_MFW_LINK_SPEED_BB_100G                    0x7
		#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_MASK                     0x00003800
		#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_OFFSET                   11
		#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_AUTONEG                  0x1
		#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_RX                       0x2
		#define NVM_CFG1_PORT_MFW_FLOW_CONTROL_TX                       0x4
		#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_MASK      0x00004000
		#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_OFFSET    14
		#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_DISABLED  0x0
		#define NVM_CFG1_PORT_OPTIC_MODULE_VENDOR_ENFORCEMENT_ENABLED   0x1
		#define NVM_CFG1_PORT_AN_25G_50G_OUI_MASK                       0x00018000
		#define NVM_CFG1_PORT_AN_25G_50G_OUI_OFFSET                     15
		#define NVM_CFG1_PORT_AN_25G_50G_OUI_CONSORTIUM                 0x0
		#define NVM_CFG1_PORT_AN_25G_50G_OUI_BAM                        0x1
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_MASK                       0x000E0000
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_OFFSET                     17
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_NONE                       0x0
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_FIRECODE                   0x1
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_RS                         0x2
		#define NVM_CFG1_PORT_FEC_FORCE_MODE_AUTO                       0x7
		#define NVM_CFG1_PORT_FEC_AN_MODE_MASK                          0x00700000
		#define NVM_CFG1_PORT_FEC_AN_MODE_OFFSET                        20
		#define NVM_CFG1_PORT_FEC_AN_MODE_NONE                          0x0
		#define NVM_CFG1_PORT_FEC_AN_MODE_10G_FIRECODE                  0x1
		#define NVM_CFG1_PORT_FEC_AN_MODE_25G_FIRECODE                  0x2
		#define NVM_CFG1_PORT_FEC_AN_MODE_10G_AND_25G_FIRECODE          0x3
		#define NVM_CFG1_PORT_FEC_AN_MODE_25G_RS                        0x4
		#define NVM_CFG1_PORT_FEC_AN_MODE_25G_FIRECODE_AND_RS           0x5
		#define NVM_CFG1_PORT_FEC_AN_MODE_ALL                           0x6
		#define NVM_CFG1_PORT_SMARTLINQ_MODE_MASK                       0x00800000
		#define NVM_CFG1_PORT_SMARTLINQ_MODE_OFFSET                     23
		#define NVM_CFG1_PORT_SMARTLINQ_MODE_DISABLED                   0x0
		#define NVM_CFG1_PORT_SMARTLINQ_MODE_ENABLED                    0x1
		#define NVM_CFG1_PORT_RESERVED_WAS_MFW_SMARTLINQ_MASK           0x01000000
		#define NVM_CFG1_PORT_RESERVED_WAS_MFW_SMARTLINQ_OFFSET         24
		#define NVM_CFG1_PORT_RESERVED_WAS_MFW_SMARTLINQ_DISABLED       0x0
		#define NVM_CFG1_PORT_RESERVED_WAS_MFW_SMARTLINQ_ENABLED        0x1
	u32 phy_cfg;                                                       /* 0x1C */
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_MASK                  0x0000FFFF
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_OFFSET                0
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_HIGIG                 0x1
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_SCRAMBLER             0x2
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_FIBER                 0x4
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_DISABLE_CL72_AN       0x8
		#define NVM_CFG1_PORT_OPTIONAL_LINK_MODES_DISABLE_FEC_AN        0x10
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_MASK                 0x00FF0000
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_OFFSET               16
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_BYPASS               0x0
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR                   0x2
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR2                  0x3
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_KR4                  0x4
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XFI                  0x8
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_SFI                  0x9
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_1000X                0xB
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_SGMII                0xC
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XLAUI                0x11
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_XLPPI                0x12
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_CAUI                 0x21
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_CPPI                 0x22
		#define NVM_CFG1_PORT_SERDES_NET_INTERFACE_25GAUI               0x31
		#define NVM_CFG1_PORT_AN_MODE_MASK                              0xFF000000
		#define NVM_CFG1_PORT_AN_MODE_OFFSET                            24
		#define NVM_CFG1_PORT_AN_MODE_NONE                              0x0
		#define NVM_CFG1_PORT_AN_MODE_CL73                              0x1
		#define NVM_CFG1_PORT_AN_MODE_CL37                              0x2
		#define NVM_CFG1_PORT_AN_MODE_CL73_BAM                          0x3
		#define NVM_CFG1_PORT_AN_MODE_BB_CL37_BAM                       0x4
		#define NVM_CFG1_PORT_AN_MODE_BB_HPAM                           0x5
		#define NVM_CFG1_PORT_AN_MODE_BB_SGMII                          0x6
	u32 mgmt_traffic;                                                  /* 0x20 */
		#define NVM_CFG1_PORT_RESERVED61_MASK                           0x0000000F
		#define NVM_CFG1_PORT_RESERVED61_OFFSET                         0
	u32 ext_phy;                                                       /* 0x24 */
		#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_MASK                    0x000000FF
		#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_OFFSET                  0
		#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_NONE                    0x0
		#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_BCM8485X                0x1
		#define NVM_CFG1_PORT_EXTERNAL_PHY_TYPE_BCM5422X                0x2
		#define NVM_CFG1_PORT_EXTERNAL_PHY_ADDRESS_MASK                 0x0000FF00
		#define NVM_CFG1_PORT_EXTERNAL_PHY_ADDRESS_OFFSET               8
	/*  EEE power saving mode */
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_MASK                0x00FF0000
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_OFFSET              16
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_DISABLED            0x0
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_BALANCED            0x1
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_AGGRESSIVE          0x2
		#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_LOW_LATENCY         0x3
	u32 mba_cfg1;                                                      /* 0x28 */
		#define NVM_CFG1_PORT_PREBOOT_OPROM_MASK                        0x00000001
		#define NVM_CFG1_PORT_PREBOOT_OPROM_OFFSET                      0
		#define NVM_CFG1_PORT_PREBOOT_OPROM_DISABLED                    0x0
		#define NVM_CFG1_PORT_PREBOOT_OPROM_ENABLED                     0x1
		#define NVM_CFG1_PORT_RESERVED__M_MBA_BOOT_TYPE_MASK            0x00000006
		#define NVM_CFG1_PORT_RESERVED__M_MBA_BOOT_TYPE_OFFSET          1
		#define NVM_CFG1_PORT_MBA_DELAY_TIME_MASK                       0x00000078
		#define NVM_CFG1_PORT_MBA_DELAY_TIME_OFFSET                     3
		#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_MASK                    0x00000080
		#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_OFFSET                  7
		#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_CTRL_S                  0x0
		#define NVM_CFG1_PORT_MBA_SETUP_HOT_KEY_CTRL_B                  0x1
		#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_MASK                0x00000100
		#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_OFFSET              8
		#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_DISABLED            0x0
		#define NVM_CFG1_PORT_MBA_HIDE_SETUP_PROMPT_ENABLED             0x1
		#define NVM_CFG1_PORT_RESERVED5_MASK                            0x0001FE00
		#define NVM_CFG1_PORT_RESERVED5_OFFSET                          9
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_MASK                   0x001E0000
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_OFFSET                 17
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_AUTONEG                0x0
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_1G                     0x1
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_10G                    0x2
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_20G                    0x3
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_25G                    0x4
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_40G                    0x5
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_50G                    0x6
		#define NVM_CFG1_PORT_PREBOOT_LINK_SPEED_BB_100G                0x7
		#define NVM_CFG1_PORT_RESERVED__M_MBA_BOOT_RETRY_COUNT_MASK     0x00E00000
		#define NVM_CFG1_PORT_RESERVED__M_MBA_BOOT_RETRY_COUNT_OFFSET   21
		#define NVM_CFG1_PORT_RESERVED_WAS_PREBOOT_SMARTLINQ_MASK       0x01000000
		#define NVM_CFG1_PORT_RESERVED_WAS_PREBOOT_SMARTLINQ_OFFSET     24
		#define NVM_CFG1_PORT_RESERVED_WAS_PREBOOT_SMARTLINQ_DISABLED   0x0
		#define NVM_CFG1_PORT_RESERVED_WAS_PREBOOT_SMARTLINQ_ENABLED    0x1
	u32 mba_cfg2;                                                      /* 0x2C */
		#define NVM_CFG1_PORT_RESERVED65_MASK                           0x0000FFFF
		#define NVM_CFG1_PORT_RESERVED65_OFFSET                         0
		#define NVM_CFG1_PORT_RESERVED66_MASK                           0x00010000
		#define NVM_CFG1_PORT_RESERVED66_OFFSET                         16
		#define NVM_CFG1_PORT_PREBOOT_LINK_UP_DELAY_MASK                0x01FE0000
		#define NVM_CFG1_PORT_PREBOOT_LINK_UP_DELAY_OFFSET              17
	u32 vf_cfg;                                                        /* 0x30 */
		#define NVM_CFG1_PORT_RESERVED8_MASK                            0x0000FFFF
		#define NVM_CFG1_PORT_RESERVED8_OFFSET                          0
		#define NVM_CFG1_PORT_RESERVED6_MASK                            0x000F0000
		#define NVM_CFG1_PORT_RESERVED6_OFFSET                          16
	struct nvm_cfg_mac_address lldp_mac_address;                       /* 0x34 */
	u32 led_port_settings;                                             /* 0x3C */
		#define NVM_CFG1_PORT_LANE_LED_SPD_0_SEL_MASK                   0x000000FF
		#define NVM_CFG1_PORT_LANE_LED_SPD_0_SEL_OFFSET                 0
		#define NVM_CFG1_PORT_LANE_LED_SPD_1_SEL_MASK                   0x0000FF00
		#define NVM_CFG1_PORT_LANE_LED_SPD_1_SEL_OFFSET                 8
		#define NVM_CFG1_PORT_LANE_LED_SPD_2_SEL_MASK                   0x00FF0000
		#define NVM_CFG1_PORT_LANE_LED_SPD_2_SEL_OFFSET                 16
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_1G                      0x1
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_10G                     0x2
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_AH_25G                  0x4
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_BB_25G                  0x8
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_AH_40G                  0x8
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_BB_40G                  0x10
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_AH_50G                  0x10
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_BB_50G                  0x20
		#define NVM_CFG1_PORT_LANE_LED_SPD__SEL_BB_100G                 0x40
	/*  UID LED Blink Mode Settings */
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_MASK                    0x0F000000
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_OFFSET                  24
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_ACTIVITY_LED            0x1
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_LINK_LED0               0x2
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_LINK_LED1               0x4
		#define NVM_CFG1_PORT_UID_LED_MODE_MASK_LINK_LED2               0x8
	u32 transceiver_00;                                                /* 0x40 */
	/*  Define for mapping of transceiver signal module absent */
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_MASK                     0x000000FF
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_OFFSET                   0
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_NA                       0x0
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO0                    0x1
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO1                    0x2
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO2                    0x3
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO3                    0x4
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO4                    0x5
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO5                    0x6
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO6                    0x7
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO7                    0x8
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO8                    0x9
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO9                    0xA
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO10                   0xB
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO11                   0xC
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO12                   0xD
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO13                   0xE
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO14                   0xF
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO15                   0x10
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO16                   0x11
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO17                   0x12
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO18                   0x13
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO19                   0x14
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO20                   0x15
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO21                   0x16
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO22                   0x17
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO23                   0x18
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO24                   0x19
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO25                   0x1A
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO26                   0x1B
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO27                   0x1C
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO28                   0x1D
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO29                   0x1E
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO30                   0x1F
		#define NVM_CFG1_PORT_TRANS_MODULE_ABS_GPIO31                   0x20
	/*  Define the GPIO mux settings  to switch i2c mux to this port */
		#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_0_MASK                  0x00000F00
		#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_0_OFFSET                8
		#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_1_MASK                  0x0000F000
		#define NVM_CFG1_PORT_I2C_MUX_SEL_VALUE_1_OFFSET                12
	u32 device_ids;                                                    /* 0x44 */
		#define NVM_CFG1_PORT_ETH_DID_SUFFIX_MASK                       0x000000FF
		#define NVM_CFG1_PORT_ETH_DID_SUFFIX_OFFSET                     0
		#define NVM_CFG1_PORT_FCOE_DID_SUFFIX_MASK                      0x0000FF00
		#define NVM_CFG1_PORT_FCOE_DID_SUFFIX_OFFSET                    8
		#define NVM_CFG1_PORT_ISCSI_DID_SUFFIX_MASK                     0x00FF0000
		#define NVM_CFG1_PORT_ISCSI_DID_SUFFIX_OFFSET                   16
		#define NVM_CFG1_PORT_RESERVED_DID_SUFFIX_MASK                  0xFF000000
		#define NVM_CFG1_PORT_RESERVED_DID_SUFFIX_OFFSET                24
	u32 board_cfg;                                                     /* 0x48 */
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_PORT_TYPE_MASK                            0x000000FF
		#define NVM_CFG1_PORT_PORT_TYPE_OFFSET                          0
		#define NVM_CFG1_PORT_PORT_TYPE_UNDEFINED                       0x0
		#define NVM_CFG1_PORT_PORT_TYPE_MODULE                          0x1
		#define NVM_CFG1_PORT_PORT_TYPE_BACKPLANE                       0x2
		#define NVM_CFG1_PORT_PORT_TYPE_EXT_PHY                         0x3
		#define NVM_CFG1_PORT_PORT_TYPE_MODULE_SLAVE                    0x4
	/*  This field defines the GPIO mapped to tx_disable signal in SFP */
		#define NVM_CFG1_PORT_TX_DISABLE_MASK                           0x0000FF00
		#define NVM_CFG1_PORT_TX_DISABLE_OFFSET                         8
		#define NVM_CFG1_PORT_TX_DISABLE_NA                             0x0
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO0                          0x1
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO1                          0x2
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO2                          0x3
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO3                          0x4
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO4                          0x5
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO5                          0x6
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO6                          0x7
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO7                          0x8
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO8                          0x9
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO9                          0xA
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO10                         0xB
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO11                         0xC
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO12                         0xD
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO13                         0xE
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO14                         0xF
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO15                         0x10
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO16                         0x11
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO17                         0x12
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO18                         0x13
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO19                         0x14
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO20                         0x15
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO21                         0x16
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO22                         0x17
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO23                         0x18
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO24                         0x19
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO25                         0x1A
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO26                         0x1B
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO27                         0x1C
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO28                         0x1D
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO29                         0x1E
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO30                         0x1F
		#define NVM_CFG1_PORT_TX_DISABLE_GPIO31                         0x20
	u32 mnm_10g_cap;                                                   /* 0x4C */
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_MASK    0x0000FFFF
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_OFFSET  0
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_10G_DRV_SPEED_CAPABILITY_MASK_BB_100G 0x40
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_MASK    0xFFFF0000
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_OFFSET  16
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_10G_MFW_SPEED_CAPABILITY_MASK_BB_100G 0x40
	u32 mnm_10g_ctrl;                                                  /* 0x50 */
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_MASK               0x0000000F
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_OFFSET             0
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_10G_DRV_LINK_SPEED_BB_100G            0x7
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_MASK               0x000000F0
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_OFFSET             4
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_10G_MFW_LINK_SPEED_BB_100G            0x7
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_MASK                    0x0000FF00
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_OFFSET                  8
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_UNDEFINED               0x0
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_MODULE                  0x1
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_BACKPLANE               0x2
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_EXT_PHY                 0x3
		#define NVM_CFG1_PORT_MNM_10G_PORT_TYPE_MODULE_SLAVE            0x4
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_MASK         0x00FF0000
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_OFFSET       16
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_BYPASS       0x0
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_KR           0x2
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_KR2          0x3
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_KR4          0x4
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_XFI          0x8
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_SFI          0x9
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_1000X        0xB
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_SGMII        0xC
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_XLAUI        0x11
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_XLPPI        0x12
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_CAUI         0x21
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_CPPI         0x22
		#define NVM_CFG1_PORT_MNM_10G_SERDES_NET_INTERFACE_25GAUI       0x31
		#define NVM_CFG1_PORT_MNM_10G_ETH_DID_SUFFIX_MASK               0xFF000000
		#define NVM_CFG1_PORT_MNM_10G_ETH_DID_SUFFIX_OFFSET             24
	u32 mnm_10g_misc;                                                  /* 0x54 */
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_MASK               0x00000007
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_OFFSET             0
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_NONE               0x0
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_FIRECODE           0x1
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_RS                 0x2
		#define NVM_CFG1_PORT_MNM_10G_FEC_FORCE_MODE_AUTO               0x7
	u32 mnm_25g_cap;                                                   /* 0x58 */
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_MASK    0x0000FFFF
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_OFFSET  0
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_25G_DRV_SPEED_CAPABILITY_MASK_BB_100G 0x40
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_MASK    0xFFFF0000
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_OFFSET  16
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_25G_MFW_SPEED_CAPABILITY_MASK_BB_100G 0x40
	u32 mnm_25g_ctrl;                                                  /* 0x5C */
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_MASK               0x0000000F
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_OFFSET             0
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_25G_DRV_LINK_SPEED_BB_100G            0x7
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_MASK               0x000000F0
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_OFFSET             4
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_25G_MFW_LINK_SPEED_BB_100G            0x7
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_MASK                    0x0000FF00
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_OFFSET                  8
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_UNDEFINED               0x0
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_MODULE                  0x1
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_BACKPLANE               0x2
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_EXT_PHY                 0x3
		#define NVM_CFG1_PORT_MNM_25G_PORT_TYPE_MODULE_SLAVE            0x4
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_MASK         0x00FF0000
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_OFFSET       16
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_BYPASS       0x0
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_KR           0x2
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_KR2          0x3
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_KR4          0x4
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_XFI          0x8
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_SFI          0x9
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_1000X        0xB
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_SGMII        0xC
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_XLAUI        0x11
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_XLPPI        0x12
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_CAUI         0x21
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_CPPI         0x22
		#define NVM_CFG1_PORT_MNM_25G_SERDES_NET_INTERFACE_25GAUI       0x31
		#define NVM_CFG1_PORT_MNM_25G_ETH_DID_SUFFIX_MASK               0xFF000000
		#define NVM_CFG1_PORT_MNM_25G_ETH_DID_SUFFIX_OFFSET             24
	u32 mnm_25g_misc;                                                  /* 0x60 */
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_MASK               0x00000007
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_OFFSET             0
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_NONE               0x0
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_FIRECODE           0x1
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_RS                 0x2
		#define NVM_CFG1_PORT_MNM_25G_FEC_FORCE_MODE_AUTO               0x7
	u32 mnm_40g_cap;                                                   /* 0x64 */
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_MASK    0x0000FFFF
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_OFFSET  0
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_40G_DRV_SPEED_CAPABILITY_MASK_BB_100G 0x40
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_MASK    0xFFFF0000
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_OFFSET  16
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_40G_MFW_SPEED_CAPABILITY_MASK_BB_100G 0x40
	u32 mnm_40g_ctrl;                                                  /* 0x68 */
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_MASK               0x0000000F
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_OFFSET             0
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_40G_DRV_LINK_SPEED_BB_100G            0x7
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_MASK               0x000000F0
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_OFFSET             4
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_40G_MFW_LINK_SPEED_BB_100G            0x7
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_MASK                    0x0000FF00
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_OFFSET                  8
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_UNDEFINED               0x0
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_MODULE                  0x1
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_BACKPLANE               0x2
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_EXT_PHY                 0x3
		#define NVM_CFG1_PORT_MNM_40G_PORT_TYPE_MODULE_SLAVE            0x4
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_MASK         0x00FF0000
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_OFFSET       16
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_BYPASS       0x0
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_KR           0x2
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_KR2          0x3
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_KR4          0x4
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_XFI          0x8
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_SFI          0x9
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_1000X        0xB
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_SGMII        0xC
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_XLAUI        0x11
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_XLPPI        0x12
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_CAUI         0x21
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_CPPI         0x22
		#define NVM_CFG1_PORT_MNM_40G_SERDES_NET_INTERFACE_25GAUI       0x31
		#define NVM_CFG1_PORT_MNM_40G_ETH_DID_SUFFIX_MASK               0xFF000000
		#define NVM_CFG1_PORT_MNM_40G_ETH_DID_SUFFIX_OFFSET             24
	u32 mnm_40g_misc;                                                  /* 0x6C */
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_MASK               0x00000007
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_OFFSET             0
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_NONE               0x0
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_FIRECODE           0x1
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_RS                 0x2
		#define NVM_CFG1_PORT_MNM_40G_FEC_FORCE_MODE_AUTO               0x7
	u32 mnm_50g_cap;                                                   /* 0x70 */
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_MASK    0x0000FFFF
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_OFFSET  0
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_50G_DRV_SPEED_CAPABILITY_MASK_BB_100G 0x40
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_MASK    0xFFFF0000
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_OFFSET  16
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_1G      0x1
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_10G     0x2
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_20G     0x4
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_25G     0x8
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_40G     0x10
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_50G     0x20
		#define NVM_CFG1_PORT_MNM_50G_MFW_SPEED_CAPABILITY_MASK_BB_100G 0x40
	u32 mnm_50g_ctrl;                                                  /* 0x74 */
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_MASK               0x0000000F
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_OFFSET             0
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_50G_DRV_LINK_SPEED_BB_100G            0x7
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_MASK               0x000000F0
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_OFFSET             4
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_AUTONEG            0x0
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_1G                 0x1
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_10G                0x2
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_20G                0x3
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_25G                0x4
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_40G                0x5
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_50G                0x6
		#define NVM_CFG1_PORT_MNM_50G_MFW_LINK_SPEED_BB_100G            0x7
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_MASK                    0x0000FF00
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_OFFSET                  8
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_UNDEFINED               0x0
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_MODULE                  0x1
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_BACKPLANE               0x2
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_EXT_PHY                 0x3
		#define NVM_CFG1_PORT_MNM_50G_PORT_TYPE_MODULE_SLAVE            0x4
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_MASK         0x00FF0000
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_OFFSET       16
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_BYPASS       0x0
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_KR           0x2
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_KR2          0x3
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_KR4          0x4
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_XFI          0x8
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_SFI          0x9
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_1000X        0xB
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_SGMII        0xC
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_XLAUI        0x11
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_XLPPI        0x12
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_CAUI         0x21
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_CPPI         0x22
		#define NVM_CFG1_PORT_MNM_50G_SERDES_NET_INTERFACE_25GAUI       0x31
		#define NVM_CFG1_PORT_MNM_50G_ETH_DID_SUFFIX_MASK               0xFF000000
		#define NVM_CFG1_PORT_MNM_50G_ETH_DID_SUFFIX_OFFSET             24
	u32 mnm_50g_misc;                                                  /* 0x78 */
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_MASK               0x00000007
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_OFFSET             0
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_NONE               0x0
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_FIRECODE           0x1
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_RS                 0x2
		#define NVM_CFG1_PORT_MNM_50G_FEC_FORCE_MODE_AUTO               0x7
	u32 mnm_100g_cap;                                                  /* 0x7C */
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_MASK          0x0000FFFF
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_OFFSET        0
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_1G            0x1
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_10G           0x2
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_20G           0x4
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_25G           0x8
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_40G           0x10
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_50G           0x20
		#define NVM_CFG1_PORT_MNM_100G_DRV_SPEED_CAP_MASK_BB_100G       0x40
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_MASK          0xFFFF0000
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_OFFSET        16
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_1G            0x1
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_10G           0x2
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_20G           0x4
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_25G           0x8
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_40G           0x10
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_50G           0x20
		#define NVM_CFG1_PORT_MNM_100G_MFW_SPEED_CAP_MASK_BB_100G       0x40
	u32 mnm_100g_ctrl;                                                 /* 0x80 */
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_MASK              0x0000000F
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_OFFSET            0
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_AUTONEG           0x0
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_1G                0x1
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_10G               0x2
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_20G               0x3
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_25G               0x4
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_40G               0x5
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_50G               0x6
		#define NVM_CFG1_PORT_MNM_100G_DRV_LINK_SPEED_BB_100G           0x7
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_MASK              0x000000F0
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_OFFSET            4
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_AUTONEG           0x0
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_1G                0x1
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_10G               0x2
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_20G               0x3
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_25G               0x4
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_40G               0x5
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_50G               0x6
		#define NVM_CFG1_PORT_MNM_100G_MFW_LINK_SPEED_BB_100G           0x7
	/*  This field defines the board technology
          (backpane,transceiver,external PHY) */
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_MASK                   0x0000FF00
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_OFFSET                 8
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_UNDEFINED              0x0
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_MODULE                 0x1
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_BACKPLANE              0x2
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_EXT_PHY                0x3
		#define NVM_CFG1_PORT_MNM_100G_PORT_TYPE_MODULE_SLAVE           0x4
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_MASK        0x00FF0000
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_OFFSET      16
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_BYPASS      0x0
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_KR          0x2
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_KR2         0x3
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_KR4         0x4
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_XFI         0x8
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_SFI         0x9
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_1000X       0xB
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_SGMII       0xC
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_XLAUI       0x11
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_XLPPI       0x12
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_CAUI        0x21
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_CPPI        0x22
		#define NVM_CFG1_PORT_MNM_100G_SERDES_NET_INTERFACE_25GAUI      0x31
		#define NVM_CFG1_PORT_MNM_100G_ETH_DID_SUFFIX_MASK              0xFF000000
		#define NVM_CFG1_PORT_MNM_100G_ETH_DID_SUFFIX_OFFSET            24
	u32 mnm_100g_misc;                                                 /* 0x84 */
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_MASK              0x00000007
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_OFFSET            0
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_NONE              0x0
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_FIRECODE          0x1
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_RS                0x2
		#define NVM_CFG1_PORT_MNM_100G_FEC_FORCE_MODE_AUTO              0x7
	u32 temperature;                                                   /* 0x88 */
		#define NVM_CFG1_PORT_PHY_MODULE_DEAD_TEMP_TH_MASK              0x000000FF
		#define NVM_CFG1_PORT_PHY_MODULE_DEAD_TEMP_TH_OFFSET            0
		#define NVM_CFG1_PORT_PHY_MODULE_ALOM_FAN_ON_TEMP_TH_MASK       0x0000FF00
		#define NVM_CFG1_PORT_PHY_MODULE_ALOM_FAN_ON_TEMP_TH_OFFSET     8
	u32 ext_phy_cfg1;                                                  /* 0x8C */
	/*  Ext PHY MDI pair swap value */
		#define NVM_CFG1_PORT_EXT_PHY_MDI_PAIR_SWAP_MASK                0x0000FFFF
		#define NVM_CFG1_PORT_EXT_PHY_MDI_PAIR_SWAP_OFFSET              0
	u32 reserved[114];                                                 /* 0x90 */
};

struct nvm_cfg1_func
{
	struct nvm_cfg_mac_address mac_address;                             /* 0x0 */
	u32 rsrv1;                                                          /* 0x8 */
		#define NVM_CFG1_FUNC_RESERVED1_MASK                            0x0000FFFF
		#define NVM_CFG1_FUNC_RESERVED1_OFFSET                          0
		#define NVM_CFG1_FUNC_RESERVED2_MASK                            0xFFFF0000
		#define NVM_CFG1_FUNC_RESERVED2_OFFSET                          16
	u32 rsrv2;                                                          /* 0xC */
		#define NVM_CFG1_FUNC_RESERVED3_MASK                            0x0000FFFF
		#define NVM_CFG1_FUNC_RESERVED3_OFFSET                          0
		#define NVM_CFG1_FUNC_RESERVED4_MASK                            0xFFFF0000
		#define NVM_CFG1_FUNC_RESERVED4_OFFSET                          16
	u32 device_id;                                                     /* 0x10 */
		#define NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_MASK                  0x0000FFFF
		#define NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_OFFSET                0
		#define NVM_CFG1_FUNC_RESERVED77_MASK                           0xFFFF0000
		#define NVM_CFG1_FUNC_RESERVED77_OFFSET                         16
	u32 cmn_cfg;                                                       /* 0x14 */
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_MASK                0x00000007
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_OFFSET              0
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_PXE                 0x0
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_ISCSI_BOOT          0x3
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_FCOE_BOOT           0x4
		#define NVM_CFG1_FUNC_PREBOOT_BOOT_PROTOCOL_NONE                0x7
		#define NVM_CFG1_FUNC_VF_PCI_DEVICE_ID_MASK                     0x0007FFF8
		#define NVM_CFG1_FUNC_VF_PCI_DEVICE_ID_OFFSET                   3
		#define NVM_CFG1_FUNC_PERSONALITY_MASK                          0x00780000
		#define NVM_CFG1_FUNC_PERSONALITY_OFFSET                        19
		#define NVM_CFG1_FUNC_PERSONALITY_ETHERNET                      0x0
		#define NVM_CFG1_FUNC_PERSONALITY_ISCSI                         0x1
		#define NVM_CFG1_FUNC_PERSONALITY_FCOE                          0x2
		#define NVM_CFG1_FUNC_PERSONALITY_ROCE                          0x3
		#define NVM_CFG1_FUNC_BANDWIDTH_WEIGHT_MASK                     0x7F800000
		#define NVM_CFG1_FUNC_BANDWIDTH_WEIGHT_OFFSET                   23
		#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_MASK                   0x80000000
		#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_OFFSET                 31
		#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_DISABLED               0x0
		#define NVM_CFG1_FUNC_PAUSE_ON_HOST_RING_ENABLED                0x1
	u32 pci_cfg;                                                       /* 0x18 */
		#define NVM_CFG1_FUNC_NUMBER_OF_VFS_PER_PF_MASK                 0x0000007F
		#define NVM_CFG1_FUNC_NUMBER_OF_VFS_PER_PF_OFFSET               0
	/*  AH VF BAR2 size */
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_MASK                     0x00003F80
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_OFFSET                   7
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_DISABLED                 0x0
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_4K                       0x1
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_8K                       0x2
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_16K                      0x3
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_32K                      0x4
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_64K                      0x5
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_128K                     0x6
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_256K                     0x7
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_512K                     0x8
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_1M                       0x9
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_2M                       0xA
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_4M                       0xB
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_8M                       0xC
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_16M                      0xD
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_32M                      0xE
		#define NVM_CFG1_FUNC_VF_PCI_BAR2_SIZE_64M                      0xF
		#define NVM_CFG1_FUNC_BAR1_SIZE_MASK                            0x0003C000
		#define NVM_CFG1_FUNC_BAR1_SIZE_OFFSET                          14
		#define NVM_CFG1_FUNC_BAR1_SIZE_DISABLED                        0x0
		#define NVM_CFG1_FUNC_BAR1_SIZE_64K                             0x1
		#define NVM_CFG1_FUNC_BAR1_SIZE_128K                            0x2
		#define NVM_CFG1_FUNC_BAR1_SIZE_256K                            0x3
		#define NVM_CFG1_FUNC_BAR1_SIZE_512K                            0x4
		#define NVM_CFG1_FUNC_BAR1_SIZE_1M                              0x5
		#define NVM_CFG1_FUNC_BAR1_SIZE_2M                              0x6
		#define NVM_CFG1_FUNC_BAR1_SIZE_4M                              0x7
		#define NVM_CFG1_FUNC_BAR1_SIZE_8M                              0x8
		#define NVM_CFG1_FUNC_BAR1_SIZE_16M                             0x9
		#define NVM_CFG1_FUNC_BAR1_SIZE_32M                             0xA
		#define NVM_CFG1_FUNC_BAR1_SIZE_64M                             0xB
		#define NVM_CFG1_FUNC_BAR1_SIZE_128M                            0xC
		#define NVM_CFG1_FUNC_BAR1_SIZE_256M                            0xD
		#define NVM_CFG1_FUNC_BAR1_SIZE_512M                            0xE
		#define NVM_CFG1_FUNC_BAR1_SIZE_1G                              0xF
		#define NVM_CFG1_FUNC_MAX_BANDWIDTH_MASK                        0x03FC0000
		#define NVM_CFG1_FUNC_MAX_BANDWIDTH_OFFSET                      18
	/*  Hide function in npar mode */
		#define NVM_CFG1_FUNC_FUNCTION_HIDE_MASK                        0x04000000
		#define NVM_CFG1_FUNC_FUNCTION_HIDE_OFFSET                      26
		#define NVM_CFG1_FUNC_FUNCTION_HIDE_DISABLED                    0x0
		#define NVM_CFG1_FUNC_FUNCTION_HIDE_ENABLED                     0x1
	/*  AH BAR2 size (per function) */
		#define NVM_CFG1_FUNC_BAR2_SIZE_MASK                            0x78000000
		#define NVM_CFG1_FUNC_BAR2_SIZE_OFFSET                          27
		#define NVM_CFG1_FUNC_BAR2_SIZE_DISABLED                        0x0
		#define NVM_CFG1_FUNC_BAR2_SIZE_1M                              0x5
		#define NVM_CFG1_FUNC_BAR2_SIZE_2M                              0x6
		#define NVM_CFG1_FUNC_BAR2_SIZE_4M                              0x7
		#define NVM_CFG1_FUNC_BAR2_SIZE_8M                              0x8
		#define NVM_CFG1_FUNC_BAR2_SIZE_16M                             0x9
		#define NVM_CFG1_FUNC_BAR2_SIZE_32M                             0xA
		#define NVM_CFG1_FUNC_BAR2_SIZE_64M                             0xB
		#define NVM_CFG1_FUNC_BAR2_SIZE_128M                            0xC
		#define NVM_CFG1_FUNC_BAR2_SIZE_256M                            0xD
		#define NVM_CFG1_FUNC_BAR2_SIZE_512M                            0xE
		#define NVM_CFG1_FUNC_BAR2_SIZE_1G                              0xF
	struct nvm_cfg_mac_address fcoe_node_wwn_mac_addr;                 /* 0x1C */
	struct nvm_cfg_mac_address fcoe_port_wwn_mac_addr;                 /* 0x24 */
	u32 preboot_generic_cfg;                                           /* 0x2C */
		#define NVM_CFG1_FUNC_PREBOOT_VLAN_VALUE_MASK                   0x0000FFFF
		#define NVM_CFG1_FUNC_PREBOOT_VLAN_VALUE_OFFSET                 0
		#define NVM_CFG1_FUNC_PREBOOT_VLAN_MASK                         0x00010000
		#define NVM_CFG1_FUNC_PREBOOT_VLAN_OFFSET                       16
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_MASK                0x001E0000
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_OFFSET              17
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_ETHERNET            0x1
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_FCOE                0x2
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_ISCSI               0x4
		#define NVM_CFG1_FUNC_NPAR_ENABLED_PROTOCOL_RDMA                0x8
	u32 features;                                                      /* 0x30 */
	/*  RDMA protocol enablement  */
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_MASK                      0x00000003
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_OFFSET                    0
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_NONE                      0x0
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_ROCE                      0x1
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_IWARP                     0x2
		#define NVM_CFG1_FUNC_RDMA_ENABLEMENT_BOTH                      0x3
	u32 reserved[7];                                                   /* 0x34 */
};

struct nvm_cfg1
{
	struct nvm_cfg1_glob glob;                                          /* 0x0 */
	struct nvm_cfg1_path path[MCP_GLOB_PATH_MAX];                     /* 0x228 */
	struct nvm_cfg1_port port[MCP_GLOB_PORT_MAX];                     /* 0x230 */
	struct nvm_cfg1_func func[MCP_GLOB_FUNC_MAX];                     /* 0xB90 */
};

/******************************************
 * nvm_cfg structs
 ******************************************/

struct board_info
{
  u16 vendor_id;
  u16 eth_did_suffix;
  u16 sub_vendor_id;
  u16 sub_device_id;
  char *board_name;
  char *friendly_name;
};

enum nvm_cfg_sections
{
	NVM_CFG_SECTION_NVM_CFG1,
	NVM_CFG_SECTION_MAX
};

struct nvm_cfg
{
	u32 num_sections;
	u32 sections_offset[NVM_CFG_SECTION_MAX];
	struct nvm_cfg1 cfg1;
};

#endif /* NVM_CFG_H */
