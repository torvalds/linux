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

#ifndef ECORE_HSI_H
#define ECORE_HSI_H

#define FW_ENCODE_32BIT_PATTERN 0x1e1e1e1e

struct license_key {
    uint32_t reserved[6];

    uint32_t max_iscsi_conn;
#define LICENSE_MAX_ISCSI_TRGT_CONN_MASK  0xFFFF
#define LICENSE_MAX_ISCSI_TRGT_CONN_SHIFT 0
#define LICENSE_MAX_ISCSI_INIT_CONN_MASK  0xFFFF0000
#define LICENSE_MAX_ISCSI_INIT_CONN_SHIFT 16

    uint32_t reserved_a;

    uint32_t max_fcoe_conn;
#define LICENSE_MAX_FCOE_TRGT_CONN_MASK  0xFFFF
#define LICENSE_MAX_FCOE_TRGT_CONN_SHIFT 0
#define LICENSE_MAX_FCOE_INIT_CONN_MASK  0xFFFF0000
#define LICENSE_MAX_FCOE_INIT_CONN_SHIFT 16

    uint32_t reserved_b[4];
};

typedef struct license_key license_key_t;


/****************************************************************************
 * Shared HW configuration                                                  *
 ****************************************************************************/
#define PIN_CFG_NA                          0x00000000
#define PIN_CFG_GPIO0_P0                    0x00000001
#define PIN_CFG_GPIO1_P0                    0x00000002
#define PIN_CFG_GPIO2_P0                    0x00000003
#define PIN_CFG_GPIO3_P0                    0x00000004
#define PIN_CFG_GPIO0_P1                    0x00000005
#define PIN_CFG_GPIO1_P1                    0x00000006
#define PIN_CFG_GPIO2_P1                    0x00000007
#define PIN_CFG_GPIO3_P1                    0x00000008
#define PIN_CFG_EPIO0                       0x00000009
#define PIN_CFG_EPIO1                       0x0000000a
#define PIN_CFG_EPIO2                       0x0000000b
#define PIN_CFG_EPIO3                       0x0000000c
#define PIN_CFG_EPIO4                       0x0000000d
#define PIN_CFG_EPIO5                       0x0000000e
#define PIN_CFG_EPIO6                       0x0000000f
#define PIN_CFG_EPIO7                       0x00000010
#define PIN_CFG_EPIO8                       0x00000011
#define PIN_CFG_EPIO9                       0x00000012
#define PIN_CFG_EPIO10                      0x00000013
#define PIN_CFG_EPIO11                      0x00000014
#define PIN_CFG_EPIO12                      0x00000015
#define PIN_CFG_EPIO13                      0x00000016
#define PIN_CFG_EPIO14                      0x00000017
#define PIN_CFG_EPIO15                      0x00000018
#define PIN_CFG_EPIO16                      0x00000019
#define PIN_CFG_EPIO17                      0x0000001a
#define PIN_CFG_EPIO18                      0x0000001b
#define PIN_CFG_EPIO19                      0x0000001c
#define PIN_CFG_EPIO20                      0x0000001d
#define PIN_CFG_EPIO21                      0x0000001e
#define PIN_CFG_EPIO22                      0x0000001f
#define PIN_CFG_EPIO23                      0x00000020
#define PIN_CFG_EPIO24                      0x00000021
#define PIN_CFG_EPIO25                      0x00000022
#define PIN_CFG_EPIO26                      0x00000023
#define PIN_CFG_EPIO27                      0x00000024
#define PIN_CFG_EPIO28                      0x00000025
#define PIN_CFG_EPIO29                      0x00000026
#define PIN_CFG_EPIO30                      0x00000027
#define PIN_CFG_EPIO31                      0x00000028

/* EPIO definition */
#define EPIO_CFG_NA                         0x00000000
#define EPIO_CFG_EPIO0                      0x00000001
#define EPIO_CFG_EPIO1                      0x00000002
#define EPIO_CFG_EPIO2                      0x00000003
#define EPIO_CFG_EPIO3                      0x00000004
#define EPIO_CFG_EPIO4                      0x00000005
#define EPIO_CFG_EPIO5                      0x00000006
#define EPIO_CFG_EPIO6                      0x00000007
#define EPIO_CFG_EPIO7                      0x00000008
#define EPIO_CFG_EPIO8                      0x00000009
#define EPIO_CFG_EPIO9                      0x0000000a
#define EPIO_CFG_EPIO10                     0x0000000b
#define EPIO_CFG_EPIO11                     0x0000000c
#define EPIO_CFG_EPIO12                     0x0000000d
#define EPIO_CFG_EPIO13                     0x0000000e
#define EPIO_CFG_EPIO14                     0x0000000f
#define EPIO_CFG_EPIO15                     0x00000010
#define EPIO_CFG_EPIO16                     0x00000011
#define EPIO_CFG_EPIO17                     0x00000012
#define EPIO_CFG_EPIO18                     0x00000013
#define EPIO_CFG_EPIO19                     0x00000014
#define EPIO_CFG_EPIO20                     0x00000015
#define EPIO_CFG_EPIO21                     0x00000016
#define EPIO_CFG_EPIO22                     0x00000017
#define EPIO_CFG_EPIO23                     0x00000018
#define EPIO_CFG_EPIO24                     0x00000019
#define EPIO_CFG_EPIO25                     0x0000001a
#define EPIO_CFG_EPIO26                     0x0000001b
#define EPIO_CFG_EPIO27                     0x0000001c
#define EPIO_CFG_EPIO28                     0x0000001d
#define EPIO_CFG_EPIO29                     0x0000001e
#define EPIO_CFG_EPIO30                     0x0000001f
#define EPIO_CFG_EPIO31                     0x00000020

struct mac_addr {
	uint32_t upper;
	uint32_t lower;
};


struct shared_hw_cfg {			 /* NVRAM Offset */
	/* Up to 16 bytes of NULL-terminated string */
	uint8_t  part_num[16];		    /* 0x104 */

	uint32_t config;			/* 0x114 */
	#define SHARED_HW_CFG_MDIO_VOLTAGE_MASK             0x00000001
		#define SHARED_HW_CFG_MDIO_VOLTAGE_SHIFT             0
		#define SHARED_HW_CFG_MDIO_VOLTAGE_1_2V              0x00000000
		#define SHARED_HW_CFG_MDIO_VOLTAGE_2_5V              0x00000001

	#define SHARED_HW_CFG_PORT_SWAP                     0x00000004

	    #define SHARED_HW_CFG_BEACON_WOL_EN                  0x00000008

	    #define SHARED_HW_CFG_PCIE_GEN3_DISABLED            0x00000000
	    #define SHARED_HW_CFG_PCIE_GEN3_ENABLED             0x00000010

	#define SHARED_HW_CFG_MFW_SELECT_MASK               0x00000700
		#define SHARED_HW_CFG_MFW_SELECT_SHIFT               8
	/* Whatever MFW found in NVM
	   (if multiple found, priority order is: NC-SI, UMP, IPMI) */
		#define SHARED_HW_CFG_MFW_SELECT_DEFAULT             0x00000000
		#define SHARED_HW_CFG_MFW_SELECT_NC_SI               0x00000100
		#define SHARED_HW_CFG_MFW_SELECT_UMP                 0x00000200
		#define SHARED_HW_CFG_MFW_SELECT_IPMI                0x00000300
	/* Use SPIO4 as an arbiter between: 0-NC_SI, 1-IPMI
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
		#define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_IPMI    0x00000400
	/* Use SPIO4 as an arbiter between: 0-UMP, 1-IPMI
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
		#define SHARED_HW_CFG_MFW_SELECT_SPIO4_UMP_IPMI      0x00000500
	/* Use SPIO4 as an arbiter between: 0-NC-SI, 1-UMP
	  (can only be used when an add-in board, not BMC, pulls-down SPIO4) */
		#define SHARED_HW_CFG_MFW_SELECT_SPIO4_NC_SI_UMP     0x00000600

	/* Adjust the PCIe G2 Tx amplitude driver for all Tx lanes. For
	   backwards compatibility, value of 0 is disabling this feature.
	    That means that though 0 is a valid value, it cannot be
	    configured. */
	#define SHARED_HW_CFG_G2_TX_DRIVE_MASK                        0x0000F000
	#define SHARED_HW_CFG_G2_TX_DRIVE_SHIFT                       12

	#define SHARED_HW_CFG_LED_MODE_MASK                 0x000F0000
		#define SHARED_HW_CFG_LED_MODE_SHIFT                 16
		#define SHARED_HW_CFG_LED_MAC1                       0x00000000
		#define SHARED_HW_CFG_LED_PHY1                       0x00010000
		#define SHARED_HW_CFG_LED_PHY2                       0x00020000
		#define SHARED_HW_CFG_LED_PHY3                       0x00030000
		#define SHARED_HW_CFG_LED_MAC2                       0x00040000
		#define SHARED_HW_CFG_LED_PHY4                       0x00050000
		#define SHARED_HW_CFG_LED_PHY5                       0x00060000
		#define SHARED_HW_CFG_LED_PHY6                       0x00070000
		#define SHARED_HW_CFG_LED_MAC3                       0x00080000
		#define SHARED_HW_CFG_LED_PHY7                       0x00090000
		#define SHARED_HW_CFG_LED_PHY9                       0x000a0000
		#define SHARED_HW_CFG_LED_PHY11                      0x000b0000
		#define SHARED_HW_CFG_LED_MAC4                       0x000c0000
		#define SHARED_HW_CFG_LED_PHY8                       0x000d0000
		#define SHARED_HW_CFG_LED_EXTPHY1                    0x000e0000
		#define SHARED_HW_CFG_LED_EXTPHY2                    0x000f0000

    #define SHARED_HW_CFG_SRIOV_MASK                    0x40000000
		#define SHARED_HW_CFG_SRIOV_DISABLED                 0x00000000
		#define SHARED_HW_CFG_SRIOV_ENABLED                  0x40000000

	#define SHARED_HW_CFG_ATC_MASK                      0x80000000
		#define SHARED_HW_CFG_ATC_DISABLED                   0x00000000
		#define SHARED_HW_CFG_ATC_ENABLED                    0x80000000

	uint32_t config2;			    /* 0x118 */

	#define SHARED_HW_CFG_PCIE_GEN2_MASK                0x00000100
	    #define SHARED_HW_CFG_PCIE_GEN2_SHIFT                8
	    #define SHARED_HW_CFG_PCIE_GEN2_DISABLED             0x00000000
	#define SHARED_HW_CFG_PCIE_GEN2_ENABLED              0x00000100

	#define SHARED_HW_CFG_SMBUS_TIMING_MASK             0x00001000
		#define SHARED_HW_CFG_SMBUS_TIMING_100KHZ            0x00000000
		#define SHARED_HW_CFG_SMBUS_TIMING_400KHZ            0x00001000

	#define SHARED_HW_CFG_HIDE_PORT1                    0x00002000


		/* Output low when PERST is asserted */
	#define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_MASK       0x00008000
		#define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_DISABLED    0x00000000
		#define SHARED_HW_CFG_SPIO4_FOLLOW_PERST_ENABLED     0x00008000

	#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_MASK    0x00070000
		#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_SHIFT    16
		#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_HW       0x00000000
		#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_0DB      0x00010000
		#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_3_5DB    0x00020000
		#define SHARED_HW_CFG_PCIE_GEN2_PREEMPHASIS_6_0DB    0x00030000

	/*  The fan failure mechanism is usually related to the PHY type
	      since the power consumption of the board is determined by the PHY.
	      Currently, fan is required for most designs with SFX7101, BCM8727
	      and BCM8481. If a fan is not required for a board which uses one
	      of those PHYs, this field should be set to "Disabled". If a fan is
	      required for a different PHY type, this option should be set to
	      "Enabled". The fan failure indication is expected on SPIO5 */
	#define SHARED_HW_CFG_FAN_FAILURE_MASK              0x00180000
		#define SHARED_HW_CFG_FAN_FAILURE_SHIFT              19
		#define SHARED_HW_CFG_FAN_FAILURE_PHY_TYPE           0x00000000
		#define SHARED_HW_CFG_FAN_FAILURE_DISABLED           0x00080000
		#define SHARED_HW_CFG_FAN_FAILURE_ENABLED            0x00100000

		/* ASPM Power Management support */
	#define SHARED_HW_CFG_ASPM_SUPPORT_MASK             0x00600000
		#define SHARED_HW_CFG_ASPM_SUPPORT_SHIFT             21
		#define SHARED_HW_CFG_ASPM_SUPPORT_L0S_L1_ENABLED    0x00000000
		#define SHARED_HW_CFG_ASPM_SUPPORT_L0S_DISABLED      0x00200000
		#define SHARED_HW_CFG_ASPM_SUPPORT_L1_DISABLED       0x00400000
		#define SHARED_HW_CFG_ASPM_SUPPORT_L0S_L1_DISABLED   0x00600000

	/* The value of PM_TL_IGNORE_REQS (bit0) in PCI register
	   tl_control_0 (register 0x2800) */
	#define SHARED_HW_CFG_PREVENT_L1_ENTRY_MASK         0x00800000
		#define SHARED_HW_CFG_PREVENT_L1_ENTRY_DISABLED      0x00000000
		#define SHARED_HW_CFG_PREVENT_L1_ENTRY_ENABLED       0x00800000


	/*  Set the MDC/MDIO access for the first external phy */
	#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_MASK         0x1C000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_SHIFT         26
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_PHY_TYPE      0x00000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_EMAC0         0x04000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_EMAC1         0x08000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_BOTH          0x0c000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS1_SWAPPED       0x10000000

	/*  Set the MDC/MDIO access for the second external phy */
	#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_MASK         0xE0000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_SHIFT         29
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_PHY_TYPE      0x00000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_EMAC0         0x20000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_EMAC1         0x40000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_BOTH          0x60000000
		#define SHARED_HW_CFG_MDC_MDIO_ACCESS2_SWAPPED       0x80000000

	/*  Max number of PF MSIX vectors */
	uint32_t config_3;                                       /* 0x11C */
	#define SHARED_HW_CFG_PF_MSIX_MAX_NUM_MASK                    0x0000007F
	#define SHARED_HW_CFG_PF_MSIX_MAX_NUM_SHIFT                   0

	/*  This field extends the mf mode chosen in nvm cfg #73 (as we ran
          out of bits) */
	#define SHARED_HW_CFG_EXTENDED_MF_MODE_MASK         0x00000F00
		#define SHARED_HW_CFG_EXTENDED_MF_MODE_SHIFT              8
		#define SHARED_HW_CFG_EXTENDED_MF_MODE_NPAR1_DOT_5        0x00000000
		#define SHARED_HW_CFG_EXTENDED_MF_MODE_NPAR2_DOT_0        0x00000100

	uint32_t ump_nc_si_config;			/* 0x120 */
	#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MASK       0x00000003
		#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_SHIFT       0
		#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MAC         0x00000000
		#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_PHY         0x00000001
		#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_MII         0x00000000
		#define SHARED_HW_CFG_UMP_NC_SI_MII_MODE_RMII        0x00000002

	/* Reserved bits: 226-230 */

	/*  The output pin template BSC_SEL which selects the I2C for this
	port in the I2C Mux */
	uint32_t board;			/* 0x124 */
	#define SHARED_HW_CFG_E3_I2C_MUX0_MASK              0x0000003F
	    #define SHARED_HW_CFG_E3_I2C_MUX0_SHIFT              0

	#define SHARED_HW_CFG_E3_I2C_MUX1_MASK              0x00000FC0
	#define SHARED_HW_CFG_E3_I2C_MUX1_SHIFT                      6
	/* Use the PIN_CFG_XXX defines on top */
	#define SHARED_HW_CFG_BOARD_REV_MASK                0x00FF0000
	#define SHARED_HW_CFG_BOARD_REV_SHIFT                        16

	#define SHARED_HW_CFG_BOARD_MAJOR_VER_MASK          0x0F000000
	#define SHARED_HW_CFG_BOARD_MAJOR_VER_SHIFT                  24

	#define SHARED_HW_CFG_BOARD_MINOR_VER_MASK          0xF0000000
	#define SHARED_HW_CFG_BOARD_MINOR_VER_SHIFT                  28

	uint32_t wc_lane_config;				    /* 0x128 */
	#define SHARED_HW_CFG_LANE_SWAP_CFG_MASK            0x0000FFFF
		#define SHARED_HW_CFG_LANE_SWAP_CFG_SHIFT            0
		#define SHARED_HW_CFG_LANE_SWAP_CFG_32103210         0x00001b1b
		#define SHARED_HW_CFG_LANE_SWAP_CFG_32100123         0x00001be4
		#define SHARED_HW_CFG_LANE_SWAP_CFG_31200213         0x000027d8
		#define SHARED_HW_CFG_LANE_SWAP_CFG_02133120         0x0000d827
		#define SHARED_HW_CFG_LANE_SWAP_CFG_01233210         0x0000e41b
		#define SHARED_HW_CFG_LANE_SWAP_CFG_01230123         0x0000e4e4
	#define SHARED_HW_CFG_LANE_SWAP_CFG_TX_MASK         0x000000FF
	#define SHARED_HW_CFG_LANE_SWAP_CFG_TX_SHIFT                 0
	#define SHARED_HW_CFG_LANE_SWAP_CFG_RX_MASK         0x0000FF00
	#define SHARED_HW_CFG_LANE_SWAP_CFG_RX_SHIFT                 8

	/* TX lane Polarity swap */
	#define SHARED_HW_CFG_TX_LANE0_POL_FLIP_ENABLED     0x00010000
	#define SHARED_HW_CFG_TX_LANE1_POL_FLIP_ENABLED     0x00020000
	#define SHARED_HW_CFG_TX_LANE2_POL_FLIP_ENABLED     0x00040000
	#define SHARED_HW_CFG_TX_LANE3_POL_FLIP_ENABLED     0x00080000
	/* TX lane Polarity swap */
	#define SHARED_HW_CFG_RX_LANE0_POL_FLIP_ENABLED     0x00100000
	#define SHARED_HW_CFG_RX_LANE1_POL_FLIP_ENABLED     0x00200000
	#define SHARED_HW_CFG_RX_LANE2_POL_FLIP_ENABLED     0x00400000
	#define SHARED_HW_CFG_RX_LANE3_POL_FLIP_ENABLED     0x00800000

	/*  Selects the port layout of the board */
	#define SHARED_HW_CFG_E3_PORT_LAYOUT_MASK           0x0F000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_SHIFT           24
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_2P_01           0x00000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_2P_10           0x01000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_0123         0x02000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_1032         0x03000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_2301         0x04000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_4P_3210         0x05000000
		#define SHARED_HW_CFG_E3_PORT_LAYOUT_2P_01_SIG       0x06000000
};


/****************************************************************************
 * Port HW configuration                                                    *
 ****************************************************************************/
struct port_hw_cfg {		    /* port 0: 0x12c  port 1: 0x2bc */

	uint32_t pci_id;
	#define PORT_HW_CFG_PCI_DEVICE_ID_MASK              0x0000FFFF
	#define PORT_HW_CFG_PCI_DEVICE_ID_SHIFT             0

	#define PORT_HW_CFG_PCI_VENDOR_ID_MASK              0xFFFF0000
	#define PORT_HW_CFG_PCI_VENDOR_ID_SHIFT             16

	uint32_t pci_sub_id;
	#define PORT_HW_CFG_PCI_SUBSYS_VENDOR_ID_MASK       0x0000FFFF
	#define PORT_HW_CFG_PCI_SUBSYS_VENDOR_ID_SHIFT      0

	#define PORT_HW_CFG_PCI_SUBSYS_DEVICE_ID_MASK       0xFFFF0000
	#define PORT_HW_CFG_PCI_SUBSYS_DEVICE_ID_SHIFT      16

	uint32_t power_dissipated;
	#define PORT_HW_CFG_POWER_DIS_D0_MASK               0x000000FF
	#define PORT_HW_CFG_POWER_DIS_D0_SHIFT                       0
	#define PORT_HW_CFG_POWER_DIS_D1_MASK               0x0000FF00
	#define PORT_HW_CFG_POWER_DIS_D1_SHIFT                       8
	#define PORT_HW_CFG_POWER_DIS_D2_MASK               0x00FF0000
	#define PORT_HW_CFG_POWER_DIS_D2_SHIFT                       16
	#define PORT_HW_CFG_POWER_DIS_D3_MASK               0xFF000000
	#define PORT_HW_CFG_POWER_DIS_D3_SHIFT                       24

	uint32_t power_consumed;
	#define PORT_HW_CFG_POWER_CONS_D0_MASK              0x000000FF
	#define PORT_HW_CFG_POWER_CONS_D0_SHIFT                      0
	#define PORT_HW_CFG_POWER_CONS_D1_MASK              0x0000FF00
	#define PORT_HW_CFG_POWER_CONS_D1_SHIFT                      8
	#define PORT_HW_CFG_POWER_CONS_D2_MASK              0x00FF0000
	#define PORT_HW_CFG_POWER_CONS_D2_SHIFT                      16
	#define PORT_HW_CFG_POWER_CONS_D3_MASK              0xFF000000
	#define PORT_HW_CFG_POWER_CONS_D3_SHIFT                      24

	uint32_t mac_upper;
	uint32_t mac_lower;                                      /* 0x140 */
	#define PORT_HW_CFG_UPPERMAC_MASK                   0x0000FFFF
	#define PORT_HW_CFG_UPPERMAC_SHIFT                           0


	uint32_t iscsi_mac_upper;  /* Upper 16 bits are always zeroes */
	uint32_t iscsi_mac_lower;

	uint32_t rdma_mac_upper;   /* Upper 16 bits are always zeroes */
	uint32_t rdma_mac_lower;

	uint32_t serdes_config;
	#define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_MASK 0x0000FFFF
	#define PORT_HW_CFG_SERDES_TX_DRV_PRE_EMPHASIS_SHIFT         0

	#define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_MASK    0xFFFF0000
	#define PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_SHIFT            16


	/*  Default values: 2P-64, 4P-32 */
	uint32_t reserved;

	uint32_t vf_config;					    /* 0x15C */
	#define PORT_HW_CFG_VF_PCI_DEVICE_ID_MASK           0xFFFF0000
	#define PORT_HW_CFG_VF_PCI_DEVICE_ID_SHIFT                   16

	uint32_t mf_pci_id;					    /* 0x160 */
	#define PORT_HW_CFG_MF_PCI_DEVICE_ID_MASK           0x0000FFFF
	#define PORT_HW_CFG_MF_PCI_DEVICE_ID_SHIFT                   0

	/*  Controls the TX laser of the SFP+ module */
	uint32_t sfp_ctrl;					    /* 0x164 */
	#define PORT_HW_CFG_TX_LASER_MASK                   0x000000FF
		#define PORT_HW_CFG_TX_LASER_SHIFT                   0
		#define PORT_HW_CFG_TX_LASER_MDIO                    0x00000000
		#define PORT_HW_CFG_TX_LASER_GPIO0                   0x00000001
		#define PORT_HW_CFG_TX_LASER_GPIO1                   0x00000002
		#define PORT_HW_CFG_TX_LASER_GPIO2                   0x00000003
		#define PORT_HW_CFG_TX_LASER_GPIO3                   0x00000004

	/*  Controls the fault module LED of the SFP+ */
	#define PORT_HW_CFG_FAULT_MODULE_LED_MASK           0x0000FF00
		#define PORT_HW_CFG_FAULT_MODULE_LED_SHIFT           8
		#define PORT_HW_CFG_FAULT_MODULE_LED_GPIO0           0x00000000
		#define PORT_HW_CFG_FAULT_MODULE_LED_GPIO1           0x00000100
		#define PORT_HW_CFG_FAULT_MODULE_LED_GPIO2           0x00000200
		#define PORT_HW_CFG_FAULT_MODULE_LED_GPIO3           0x00000300
		#define PORT_HW_CFG_FAULT_MODULE_LED_DISABLED        0x00000400

	/*  The output pin TX_DIS that controls the TX laser of the SFP+
	  module. Use the PIN_CFG_XXX defines on top */
	uint32_t e3_sfp_ctrl;				    /* 0x168 */
	#define PORT_HW_CFG_E3_TX_LASER_MASK                0x000000FF
	#define PORT_HW_CFG_E3_TX_LASER_SHIFT                        0

	/*  The output pin for SFPP_TYPE which turns on the Fault module LED */
	#define PORT_HW_CFG_E3_FAULT_MDL_LED_MASK           0x0000FF00
	#define PORT_HW_CFG_E3_FAULT_MDL_LED_SHIFT                   8

	/*  The input pin MOD_ABS that indicates whether SFP+ module is
	  present or not. Use the PIN_CFG_XXX defines on top */
	#define PORT_HW_CFG_E3_MOD_ABS_MASK                 0x00FF0000
	#define PORT_HW_CFG_E3_MOD_ABS_SHIFT                         16

	/*  The output pin PWRDIS_SFP_X which disable the power of the SFP+
	  module. Use the PIN_CFG_XXX defines on top */
	#define PORT_HW_CFG_E3_PWR_DIS_MASK                 0xFF000000
	#define PORT_HW_CFG_E3_PWR_DIS_SHIFT                         24

	/*
	 * The input pin which signals module transmit fault. Use the
	 * PIN_CFG_XXX defines on top
	 */
	uint32_t e3_cmn_pin_cfg;				    /* 0x16C */
	#define PORT_HW_CFG_E3_TX_FAULT_MASK                0x000000FF
	#define PORT_HW_CFG_E3_TX_FAULT_SHIFT                        0

	/*  The output pin which reset the PHY. Use the PIN_CFG_XXX defines on
	 top */
	#define PORT_HW_CFG_E3_PHY_RESET_MASK               0x0000FF00
	#define PORT_HW_CFG_E3_PHY_RESET_SHIFT                       8

	/*
	 * The output pin which powers down the PHY. Use the PIN_CFG_XXX
	 * defines on top
	 */
	#define PORT_HW_CFG_E3_PWR_DOWN_MASK                0x00FF0000
	#define PORT_HW_CFG_E3_PWR_DOWN_SHIFT                        16

	/*  The output pin values BSC_SEL which selects the I2C for this port
	  in the I2C Mux */
	#define PORT_HW_CFG_E3_I2C_MUX0_MASK                0x01000000
	#define PORT_HW_CFG_E3_I2C_MUX1_MASK                0x02000000


	/*
	 * The input pin I_FAULT which indicate over-current has occurred.
	 * Use the PIN_CFG_XXX defines on top
	 */
	uint32_t e3_cmn_pin_cfg1;				    /* 0x170 */
	#define PORT_HW_CFG_E3_OVER_CURRENT_MASK            0x000000FF
	#define PORT_HW_CFG_E3_OVER_CURRENT_SHIFT                    0

	/*  pause on host ring */
	uint32_t generic_features;                               /* 0x174 */
	#define PORT_HW_CFG_PAUSE_ON_HOST_RING_MASK                   0x00000001
	#define PORT_HW_CFG_PAUSE_ON_HOST_RING_SHIFT                  0
	#define PORT_HW_CFG_PAUSE_ON_HOST_RING_DISABLED               0x00000000
	#define PORT_HW_CFG_PAUSE_ON_HOST_RING_ENABLED                0x00000001

	/* SFP+ Tx Equalization: NIC recommended and tested value is 0xBEB2
	 * LOM recommended and tested value is 0xBEB2. Using a different
	 * value means using a value not tested by BRCM
	 */
	uint32_t sfi_tap_values;                                 /* 0x178 */
	#define PORT_HW_CFG_TX_EQUALIZATION_MASK                      0x0000FFFF
	#define PORT_HW_CFG_TX_EQUALIZATION_SHIFT                     0

	/* SFP+ Tx driver broadcast IDRIVER: NIC recommended and tested
	 * value is 0x2. LOM recommended and tested value is 0x2. Using a
	 * different value means using a value not tested by BRCM
	 */
	#define PORT_HW_CFG_TX_DRV_BROADCAST_MASK                     0x000F0000
	#define PORT_HW_CFG_TX_DRV_BROADCAST_SHIFT                    16
	/*  Set non-default values for TXFIR in SFP mode. */
	#define PORT_HW_CFG_TX_DRV_IFIR_MASK                          0x00F00000
	#define PORT_HW_CFG_TX_DRV_IFIR_SHIFT                         20

	/*  Set non-default values for IPREDRIVER in SFP mode. */
	#define PORT_HW_CFG_TX_DRV_IPREDRIVER_MASK                    0x0F000000
	#define PORT_HW_CFG_TX_DRV_IPREDRIVER_SHIFT                   24

	/*  Set non-default values for POST2 in SFP mode. */
	#define PORT_HW_CFG_TX_DRV_POST2_MASK                         0xF0000000
	#define PORT_HW_CFG_TX_DRV_POST2_SHIFT                        28

	uint32_t reserved0[5];				    /* 0x17c */

	uint32_t aeu_int_mask;				    /* 0x190 */

	uint32_t media_type;					    /* 0x194 */
	#define PORT_HW_CFG_MEDIA_TYPE_PHY0_MASK            0x000000FF
	#define PORT_HW_CFG_MEDIA_TYPE_PHY0_SHIFT                    0

	#define PORT_HW_CFG_MEDIA_TYPE_PHY1_MASK            0x0000FF00
	#define PORT_HW_CFG_MEDIA_TYPE_PHY1_SHIFT                    8

	#define PORT_HW_CFG_MEDIA_TYPE_PHY2_MASK            0x00FF0000
	#define PORT_HW_CFG_MEDIA_TYPE_PHY2_SHIFT                    16

	/*  4 times 16 bits for all 4 lanes. In case external PHY is present
	      (not direct mode), those values will not take effect on the 4 XGXS
	      lanes. For some external PHYs (such as 8706 and 8726) the values
	      will be used to configure the external PHY  in those cases, not
	      all 4 values are needed. */
	uint16_t xgxs_config_rx[4];			/* 0x198 */
	uint16_t xgxs_config_tx[4];			/* 0x1A0 */


	/* For storing FCOE mac on shared memory */
	uint32_t fcoe_fip_mac_upper;
	#define PORT_HW_CFG_FCOE_UPPERMAC_MASK              0x0000ffff
	#define PORT_HW_CFG_FCOE_UPPERMAC_SHIFT                      0
	uint32_t fcoe_fip_mac_lower;

	uint32_t fcoe_wwn_port_name_upper;
	uint32_t fcoe_wwn_port_name_lower;

	uint32_t fcoe_wwn_node_name_upper;
	uint32_t fcoe_wwn_node_name_lower;

	/*  wwpn for npiv enabled */
	uint32_t wwpn_for_npiv_config;                           /* 0x1C0 */
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ENABLED_MASK                0x00000001
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ENABLED_SHIFT               0
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ENABLED_DISABLED            0x00000000
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ENABLED_ENABLED             0x00000001

	/*  wwpn for npiv valid addresses */
	uint32_t wwpn_for_npiv_valid_addresses;                  /* 0x1C4 */
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ADDRESS_BITMAP_MASK         0x0000FFFF
	#define PORT_HW_CFG_WWPN_FOR_NPIV_ADDRESS_BITMAP_SHIFT        0

	struct mac_addr wwpn_for_niv_macs[16];

	/* Reserved bits: 2272-2336 For storing FCOE mac on shared memory */
	uint32_t Reserved1[14];

	uint32_t pf_allocation;                                  /* 0x280 */
	/* number of vfs per PF, if 0 - sriov disabled */
	#define PORT_HW_CFG_NUMBER_OF_VFS_MASK                        0x000000FF
	#define PORT_HW_CFG_NUMBER_OF_VFS_SHIFT                       0

	/*  Enable RJ45 magjack pair swapping on 10GBase-T PHY (0=default),
	      84833 only */
	uint32_t xgbt_phy_cfg;				    /* 0x284 */
	#define PORT_HW_CFG_RJ45_PAIR_SWAP_MASK             0x000000FF
	#define PORT_HW_CFG_RJ45_PAIR_SWAP_SHIFT                     0

		uint32_t default_cfg;			    /* 0x288 */
	#define PORT_HW_CFG_GPIO0_CONFIG_MASK               0x00000003
		#define PORT_HW_CFG_GPIO0_CONFIG_SHIFT               0
		#define PORT_HW_CFG_GPIO0_CONFIG_NA                  0x00000000
		#define PORT_HW_CFG_GPIO0_CONFIG_LOW                 0x00000001
		#define PORT_HW_CFG_GPIO0_CONFIG_HIGH                0x00000002
		#define PORT_HW_CFG_GPIO0_CONFIG_INPUT               0x00000003

	#define PORT_HW_CFG_GPIO1_CONFIG_MASK               0x0000000C
		#define PORT_HW_CFG_GPIO1_CONFIG_SHIFT               2
		#define PORT_HW_CFG_GPIO1_CONFIG_NA                  0x00000000
		#define PORT_HW_CFG_GPIO1_CONFIG_LOW                 0x00000004
		#define PORT_HW_CFG_GPIO1_CONFIG_HIGH                0x00000008
		#define PORT_HW_CFG_GPIO1_CONFIG_INPUT               0x0000000c

	#define PORT_HW_CFG_GPIO2_CONFIG_MASK               0x00000030
		#define PORT_HW_CFG_GPIO2_CONFIG_SHIFT               4
		#define PORT_HW_CFG_GPIO2_CONFIG_NA                  0x00000000
		#define PORT_HW_CFG_GPIO2_CONFIG_LOW                 0x00000010
		#define PORT_HW_CFG_GPIO2_CONFIG_HIGH                0x00000020
		#define PORT_HW_CFG_GPIO2_CONFIG_INPUT               0x00000030

	#define PORT_HW_CFG_GPIO3_CONFIG_MASK               0x000000C0
		#define PORT_HW_CFG_GPIO3_CONFIG_SHIFT               6
		#define PORT_HW_CFG_GPIO3_CONFIG_NA                  0x00000000
		#define PORT_HW_CFG_GPIO3_CONFIG_LOW                 0x00000040
		#define PORT_HW_CFG_GPIO3_CONFIG_HIGH                0x00000080
		#define PORT_HW_CFG_GPIO3_CONFIG_INPUT               0x000000c0

	/*  When KR link is required to be set to force which is not
	      KR-compliant, this parameter determine what is the trigger for it.
	      When GPIO is selected, low input will force the speed. Currently
	      default speed is 1G. In the future, it may be widen to select the
	      forced speed in with another parameter. Note when force-1G is
	      enabled, it override option 56: Link Speed option. */
	#define PORT_HW_CFG_FORCE_KR_ENABLER_MASK           0x00000F00
		#define PORT_HW_CFG_FORCE_KR_ENABLER_SHIFT           8
		#define PORT_HW_CFG_FORCE_KR_ENABLER_NOT_FORCED      0x00000000
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO0_P0        0x00000100
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO1_P0        0x00000200
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO2_P0        0x00000300
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO3_P0        0x00000400
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO0_P1        0x00000500
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO1_P1        0x00000600
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO2_P1        0x00000700
		#define PORT_HW_CFG_FORCE_KR_ENABLER_GPIO3_P1        0x00000800
		#define PORT_HW_CFG_FORCE_KR_ENABLER_FORCED          0x00000900
	/*  Enable to determine with which GPIO to reset the external phy */
	#define PORT_HW_CFG_EXT_PHY_GPIO_RST_MASK           0x000F0000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_SHIFT           16
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_PHY_TYPE        0x00000000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO0_P0        0x00010000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO1_P0        0x00020000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO2_P0        0x00030000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO3_P0        0x00040000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO0_P1        0x00050000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO1_P1        0x00060000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO2_P1        0x00070000
		#define PORT_HW_CFG_EXT_PHY_GPIO_RST_GPIO3_P1        0x00080000

	/*  Enable BAM on KR */
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_MASK           0x00100000
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_SHIFT                   20
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_DISABLED                0x00000000
	#define PORT_HW_CFG_ENABLE_BAM_ON_KR_ENABLED                 0x00100000

	/*  Enable Common Mode Sense */
	#define PORT_HW_CFG_ENABLE_CMS_MASK                 0x00200000
	#define PORT_HW_CFG_ENABLE_CMS_SHIFT                         21
	#define PORT_HW_CFG_ENABLE_CMS_DISABLED                      0x00000000
	#define PORT_HW_CFG_ENABLE_CMS_ENABLED                       0x00200000

	/*  Determine the Serdes electrical interface   */
	#define PORT_HW_CFG_NET_SERDES_IF_MASK              0x0F000000
	#define PORT_HW_CFG_NET_SERDES_IF_SHIFT                      24
	#define PORT_HW_CFG_NET_SERDES_IF_SGMII                      0x00000000
	#define PORT_HW_CFG_NET_SERDES_IF_XFI                        0x01000000
	#define PORT_HW_CFG_NET_SERDES_IF_SFI                        0x02000000
	#define PORT_HW_CFG_NET_SERDES_IF_KR                         0x03000000
	#define PORT_HW_CFG_NET_SERDES_IF_DXGXS                      0x04000000
	#define PORT_HW_CFG_NET_SERDES_IF_KR2                        0x05000000

	/*  SFP+ main TAP and post TAP volumes */
	#define PORT_HW_CFG_TAP_LEVELS_MASK                           0x70000000
	#define PORT_HW_CFG_TAP_LEVELS_SHIFT                          28
	#define PORT_HW_CFG_TAP_LEVELS_POST_15_MAIN_43                0x00000000
	#define PORT_HW_CFG_TAP_LEVELS_POST_14_MAIN_44                0x10000000
	#define PORT_HW_CFG_TAP_LEVELS_POST_13_MAIN_45                0x20000000
	#define PORT_HW_CFG_TAP_LEVELS_POST_12_MAIN_46                0x30000000
	#define PORT_HW_CFG_TAP_LEVELS_POST_11_MAIN_47                0x40000000
	#define PORT_HW_CFG_TAP_LEVELS_POST_10_MAIN_48                0x50000000

	uint32_t speed_capability_mask2;			    /* 0x28C */
	#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_MASK       0x0000FFFF
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_SHIFT       0
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_10M_FULL    0x00000001
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_10M_HALF    0x00000002
	    #define PORT_HW_CFG_SPEED_CAPABILITY2_D3_100M_HALF   0x00000004
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_100M_FULL   0x00000008
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_1G          0x00000010
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_2_5G        0x00000020
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_10G         0x00000040
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D3_20G         0x00000080

	#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_MASK       0xFFFF0000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_SHIFT       16
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_10M_FULL    0x00010000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_10M_HALF    0x00020000
	    #define PORT_HW_CFG_SPEED_CAPABILITY2_D0_100M_HALF   0x00040000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_100M_FULL   0x00080000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_1G          0x00100000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_2_5G        0x00200000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_10G         0x00400000
		#define PORT_HW_CFG_SPEED_CAPABILITY2_D0_20G         0x00800000


	/*  In the case where two media types (e.g. copper and fiber) are
	      present and electrically active at the same time, PHY Selection
	      will determine which of the two PHYs will be designated as the
	      Active PHY and used for a connection to the network.  */
	uint32_t multi_phy_config;				    /* 0x290 */
	#define PORT_HW_CFG_PHY_SELECTION_MASK              0x00000007
		#define PORT_HW_CFG_PHY_SELECTION_SHIFT              0
		#define PORT_HW_CFG_PHY_SELECTION_HARDWARE_DEFAULT   0x00000000
		#define PORT_HW_CFG_PHY_SELECTION_FIRST_PHY          0x00000001
		#define PORT_HW_CFG_PHY_SELECTION_SECOND_PHY         0x00000002
		#define PORT_HW_CFG_PHY_SELECTION_FIRST_PHY_PRIORITY 0x00000003
		#define PORT_HW_CFG_PHY_SELECTION_SECOND_PHY_PRIORITY 0x00000004

	/*  When enabled, all second phy nvram parameters will be swapped
	      with the first phy parameters */
	#define PORT_HW_CFG_PHY_SWAPPED_MASK                0x00000008
		#define PORT_HW_CFG_PHY_SWAPPED_SHIFT                3
		#define PORT_HW_CFG_PHY_SWAPPED_DISABLED             0x00000000
		#define PORT_HW_CFG_PHY_SWAPPED_ENABLED              0x00000008


	/*  Address of the second external phy */
	uint32_t external_phy_config2;			    /* 0x294 */
	#define PORT_HW_CFG_XGXS_EXT_PHY2_ADDR_MASK         0x000000FF
	#define PORT_HW_CFG_XGXS_EXT_PHY2_ADDR_SHIFT                 0

	/*  The second XGXS external PHY type */
	#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_MASK         0x0000FF00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_SHIFT         8
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_DIRECT        0x00000000
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8071       0x00000100
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8072       0x00000200
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8073       0x00000300
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8705       0x00000400
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8706       0x00000500
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8726       0x00000600
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8481       0x00000700
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_SFX7101       0x00000800
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8727       0x00000900
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8727_NOC   0x00000a00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84823      0x00000b00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM54640      0x00000c00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84833      0x00000d00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM54618SE    0x00000e00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM8722       0x00000f00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM54616      0x00001000
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84834      0x00001100
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_BCM84858      0x00001200
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_FAILURE       0x0000fd00
		#define PORT_HW_CFG_XGXS_EXT_PHY2_TYPE_NOT_CONN      0x0000ff00


	/*  4 times 16 bits for all 4 lanes. For some external PHYs (such as
	      8706, 8726 and 8727) not all 4 values are needed. */
	uint16_t xgxs_config2_rx[4];				    /* 0x296 */
	uint16_t xgxs_config2_tx[4];				    /* 0x2A0 */

	uint32_t lane_config;
	#define PORT_HW_CFG_LANE_SWAP_CFG_MASK              0x0000FFFF
		#define PORT_HW_CFG_LANE_SWAP_CFG_SHIFT              0
		/* AN and forced */
		#define PORT_HW_CFG_LANE_SWAP_CFG_01230123           0x00001b1b
		/* forced only */
		#define PORT_HW_CFG_LANE_SWAP_CFG_01233210           0x00001be4
		/* forced only */
		#define PORT_HW_CFG_LANE_SWAP_CFG_31203120           0x0000d8d8
		/* forced only */
		#define PORT_HW_CFG_LANE_SWAP_CFG_32103210           0x0000e4e4
	#define PORT_HW_CFG_LANE_SWAP_CFG_TX_MASK           0x000000FF
	#define PORT_HW_CFG_LANE_SWAP_CFG_TX_SHIFT                   0
	#define PORT_HW_CFG_LANE_SWAP_CFG_RX_MASK           0x0000FF00
	#define PORT_HW_CFG_LANE_SWAP_CFG_RX_SHIFT                   8
	#define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK       0x0000C000
	#define PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT               14

	/*  Indicate whether to swap the external phy polarity */
	#define PORT_HW_CFG_SWAP_PHY_POLARITY_MASK          0x00010000
		#define PORT_HW_CFG_SWAP_PHY_POLARITY_DISABLED       0x00000000
		#define PORT_HW_CFG_SWAP_PHY_POLARITY_ENABLED        0x00010000


	uint32_t external_phy_config;
	#define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK          0x000000FF
	#define PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT                  0

	#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK          0x0000FF00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SHIFT          8
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT         0x00000000
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8071        0x00000100
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072        0x00000200
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073        0x00000300
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705        0x00000400
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706        0x00000500
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8726        0x00000600
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8481        0x00000700
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101        0x00000800
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727        0x00000900
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8727_NOC    0x00000a00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84823       0x00000b00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM54640       0x00000c00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833       0x00000d00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM54618SE     0x00000e00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8722        0x00000f00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM54616       0x00001000
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84834       0x00001100
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84858       0x00001200
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT_WC      0x0000fc00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE        0x0000fd00
		#define PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN       0x0000ff00

	#define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_MASK        0x00FF0000
	#define PORT_HW_CFG_SERDES_EXT_PHY_ADDR_SHIFT                16

	#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK        0xFF000000
		#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_SHIFT        24
		#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT       0x00000000
		#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482      0x01000000
		#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT_SD    0x02000000
		#define PORT_HW_CFG_SERDES_EXT_PHY_TYPE_NOT_CONN     0xff000000

	uint32_t speed_capability_mask;
	#define PORT_HW_CFG_SPEED_CAPABILITY_D3_MASK        0x0000FFFF
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_SHIFT        0
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_FULL     0x00000001
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10M_HALF     0x00000002
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_HALF    0x00000004
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_100M_FULL    0x00000008
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_1G           0x00000010
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_2_5G         0x00000020
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_10G          0x00000040
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_20G          0x00000080
		#define PORT_HW_CFG_SPEED_CAPABILITY_D3_RESERVED     0x0000f000

	#define PORT_HW_CFG_SPEED_CAPABILITY_D0_MASK        0xFFFF0000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_SHIFT        16
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL     0x00010000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF     0x00020000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF    0x00040000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL    0x00080000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_1G           0x00100000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G         0x00200000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_10G          0x00400000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_20G          0x00800000
		#define PORT_HW_CFG_SPEED_CAPABILITY_D0_RESERVED     0xf0000000

	/*  A place to hold the original MAC address as a backup */
	uint32_t backup_mac_upper;			/* 0x2B4 */
	uint32_t backup_mac_lower;			/* 0x2B8 */

};


/****************************************************************************
 * Shared Feature configuration                                             *
 ****************************************************************************/
struct shared_feat_cfg {		 /* NVRAM Offset */

	uint32_t config;			/* 0x450 */
	#define SHARED_FEATURE_BMC_ECHO_MODE_EN             0x00000001

	/* Use NVRAM values instead of HW default values */
	#define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_MASK \
							    0x00000002
		#define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_DISABLED \
								     0x00000000
		#define SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_ENABLED \
								     0x00000002

	#define SHARED_FEAT_CFG_NCSI_ID_METHOD_MASK         0x00000008
		#define SHARED_FEAT_CFG_NCSI_ID_METHOD_SPIO          0x00000000
		#define SHARED_FEAT_CFG_NCSI_ID_METHOD_NVRAM         0x00000008

	#define SHARED_FEAT_CFG_NCSI_ID_MASK                0x00000030
	#define SHARED_FEAT_CFG_NCSI_ID_SHIFT                        4

	/*  Override the OTP back to single function mode. When using GPIO,
	      high means only SF, 0 is according to CLP configuration */
	#define SHARED_FEAT_CFG_FORCE_SF_MODE_MASK          0x00000700
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_SHIFT          8
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_MF_ALLOWED     0x00000000
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_FORCED_SF      0x00000100
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_SPIO4          0x00000200
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_SWITCH_INDEPT  0x00000300
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_AFEX_MODE      0x00000400
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_BD_MODE        0x00000500
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_UFP_MODE       0x00000600
		#define SHARED_FEAT_CFG_FORCE_SF_MODE_EXTENDED_MODE  0x00000700

	/*  Act as if the FCoE license is invalid */
	#define SHARED_FEAT_CFG_PREVENT_FCOE                0x00001000

    /*  Force FLR capability to all ports */
	#define SHARED_FEAT_CFG_FORCE_FLR_CAPABILITY        0x00002000

	/*  Act as if the iSCSI license is invalid */
	#define SHARED_FEAT_CFG_PREVENT_ISCSI_MASK                    0x00004000
	#define SHARED_FEAT_CFG_PREVENT_ISCSI_SHIFT                   14
	#define SHARED_FEAT_CFG_PREVENT_ISCSI_DISABLED                0x00000000
	#define SHARED_FEAT_CFG_PREVENT_ISCSI_ENABLED                 0x00004000

	/* The interval in seconds between sending LLDP packets. Set to zero
	   to disable the feature */
	#define SHARED_FEAT_CFG_LLDP_XMIT_INTERVAL_MASK     0x00FF0000
	#define SHARED_FEAT_CFG_LLDP_XMIT_INTERVAL_SHIFT             16

	/* The assigned device type ID for LLDP usage */
	#define SHARED_FEAT_CFG_LLDP_DEVICE_TYPE_ID_MASK    0xFF000000
	#define SHARED_FEAT_CFG_LLDP_DEVICE_TYPE_ID_SHIFT            24

};


/****************************************************************************
 * Port Feature configuration                                               *
 ****************************************************************************/
struct port_feat_cfg {		    /* port 0: 0x454  port 1: 0x4c8 */

	uint32_t config;
	#define PORT_FEAT_CFG_BAR1_SIZE_MASK                 0x0000000F
		#define PORT_FEAT_CFG_BAR1_SIZE_SHIFT                 0
		#define PORT_FEAT_CFG_BAR1_SIZE_DISABLED              0x00000000
		#define PORT_FEAT_CFG_BAR1_SIZE_64K                   0x00000001
		#define PORT_FEAT_CFG_BAR1_SIZE_128K                  0x00000002
		#define PORT_FEAT_CFG_BAR1_SIZE_256K                  0x00000003
		#define PORT_FEAT_CFG_BAR1_SIZE_512K                  0x00000004
		#define PORT_FEAT_CFG_BAR1_SIZE_1M                    0x00000005
		#define PORT_FEAT_CFG_BAR1_SIZE_2M                    0x00000006
		#define PORT_FEAT_CFG_BAR1_SIZE_4M                    0x00000007
		#define PORT_FEAT_CFG_BAR1_SIZE_8M                    0x00000008
		#define PORT_FEAT_CFG_BAR1_SIZE_16M                   0x00000009
		#define PORT_FEAT_CFG_BAR1_SIZE_32M                   0x0000000a
		#define PORT_FEAT_CFG_BAR1_SIZE_64M                   0x0000000b
		#define PORT_FEAT_CFG_BAR1_SIZE_128M                  0x0000000c
		#define PORT_FEAT_CFG_BAR1_SIZE_256M                  0x0000000d
		#define PORT_FEAT_CFG_BAR1_SIZE_512M                  0x0000000e
		#define PORT_FEAT_CFG_BAR1_SIZE_1G                    0x0000000f
	#define PORT_FEAT_CFG_BAR2_SIZE_MASK                 0x000000F0
		#define PORT_FEAT_CFG_BAR2_SIZE_SHIFT                 4
		#define PORT_FEAT_CFG_BAR2_SIZE_DISABLED              0x00000000
		#define PORT_FEAT_CFG_BAR2_SIZE_64K                   0x00000010
		#define PORT_FEAT_CFG_BAR2_SIZE_128K                  0x00000020
		#define PORT_FEAT_CFG_BAR2_SIZE_256K                  0x00000030
		#define PORT_FEAT_CFG_BAR2_SIZE_512K                  0x00000040
		#define PORT_FEAT_CFG_BAR2_SIZE_1M                    0x00000050
		#define PORT_FEAT_CFG_BAR2_SIZE_2M                    0x00000060
		#define PORT_FEAT_CFG_BAR2_SIZE_4M                    0x00000070
		#define PORT_FEAT_CFG_BAR2_SIZE_8M                    0x00000080
		#define PORT_FEAT_CFG_BAR2_SIZE_16M                   0x00000090
		#define PORT_FEAT_CFG_BAR2_SIZE_32M                   0x000000a0
		#define PORT_FEAT_CFG_BAR2_SIZE_64M                   0x000000b0
		#define PORT_FEAT_CFG_BAR2_SIZE_128M                  0x000000c0
		#define PORT_FEAT_CFG_BAR2_SIZE_256M                  0x000000d0
		#define PORT_FEAT_CFG_BAR2_SIZE_512M                  0x000000e0
		#define PORT_FEAT_CFG_BAR2_SIZE_1G                    0x000000f0

	#define PORT_FEAT_CFG_DCBX_MASK                     0x00000100
		#define PORT_FEAT_CFG_DCBX_DISABLED                  0x00000000
		#define PORT_FEAT_CFG_DCBX_ENABLED                   0x00000100

    #define PORT_FEAT_CFG_AUTOGREEEN_MASK               0x00000200
	    #define PORT_FEAT_CFG_AUTOGREEEN_SHIFT               9
	    #define PORT_FEAT_CFG_AUTOGREEEN_DISABLED            0x00000000
	    #define PORT_FEAT_CFG_AUTOGREEEN_ENABLED             0x00000200

	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_MASK                0x00000C00
	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_SHIFT               10
	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_DEFAULT             0x00000000
	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_FCOE                0x00000400
	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_ISCSI               0x00000800
	#define PORT_FEAT_CFG_STORAGE_PERSONALITY_BOTH                0x00000c00

	#define PORT_FEATURE_EN_SIZE_MASK                   0x0f000000
	#define PORT_FEATURE_EN_SIZE_SHIFT                       24
	#define PORT_FEATURE_WOL_ENABLED                         0x01000000
	#define PORT_FEATURE_MBA_ENABLED                         0x02000000
	#define PORT_FEATURE_MFW_ENABLED                         0x04000000

	/* Advertise expansion ROM even if MBA is disabled */
	#define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_MASK        0x08000000
		#define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_DISABLED     0x00000000
		#define PORT_FEAT_CFG_FORCE_EXP_ROM_ADV_ENABLED      0x08000000

	/* Check the optic vendor via i2c against a list of approved modules
	   in a separate nvram image */
	#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_MASK         0xE0000000
		#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_SHIFT         29
		#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_NO_ENFORCEMENT \
								     0x00000000
		#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_DISABLE_TX_LASER \
								     0x20000000
		#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_WARNING_MSG   0x40000000
		#define PORT_FEAT_CFG_OPT_MDL_ENFRCMNT_POWER_DOWN    0x60000000

	uint32_t wol_config;
	/* Default is used when driver sets to "auto" mode */
	#define PORT_FEATURE_WOL_ACPI_UPON_MGMT             0x00000010

	uint32_t mba_config;
	#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK       0x00000007
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_SHIFT       0
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_PXE         0x00000000
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_RPL         0x00000001
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_BOOTP       0x00000002
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_ISCSIB      0x00000003
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_FCOE_BOOT   0x00000004
		#define PORT_FEATURE_MBA_BOOT_AGENT_TYPE_NONE        0x00000007

	#define PORT_FEATURE_MBA_BOOT_RETRY_MASK            0x00000038
	#define PORT_FEATURE_MBA_BOOT_RETRY_SHIFT                    3

    #define PORT_FEATURE_MBA_SETUP_PROMPT_ENABLE        0x00000400
	#define PORT_FEATURE_MBA_HOTKEY_MASK                0x00000800
		#define PORT_FEATURE_MBA_HOTKEY_CTRL_S               0x00000000
		#define PORT_FEATURE_MBA_HOTKEY_CTRL_B               0x00000800

	#define PORT_FEATURE_MBA_EXP_ROM_SIZE_MASK          0x000FF000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_SHIFT          12
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_DISABLED       0x00000000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_2K             0x00001000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_4K             0x00002000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_8K             0x00003000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_16K            0x00004000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_32K            0x00005000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_64K            0x00006000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_128K           0x00007000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_256K           0x00008000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_512K           0x00009000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_1M             0x0000a000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_2M             0x0000b000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_4M             0x0000c000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_8M             0x0000d000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_16M            0x0000e000
		#define PORT_FEATURE_MBA_EXP_ROM_SIZE_32M            0x0000f000
	#define PORT_FEATURE_MBA_MSG_TIMEOUT_MASK           0x00F00000
	#define PORT_FEATURE_MBA_MSG_TIMEOUT_SHIFT                   20
	#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_MASK        0x03000000
		#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_SHIFT        24
		#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_AUTO         0x00000000
		#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_BBS          0x01000000
		#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT18H       0x02000000
		#define PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT19H       0x03000000
	#define PORT_FEATURE_MBA_LINK_SPEED_MASK            0x3C000000
		#define PORT_FEATURE_MBA_LINK_SPEED_SHIFT            26
		#define PORT_FEATURE_MBA_LINK_SPEED_AUTO             0x00000000
		#define PORT_FEATURE_MBA_LINK_SPEED_10M_HALF         0x04000000
		#define PORT_FEATURE_MBA_LINK_SPEED_10M_FULL         0x08000000
		#define PORT_FEATURE_MBA_LINK_SPEED_100M_HALF        0x0c000000
		#define PORT_FEATURE_MBA_LINK_SPEED_100M_FULL        0x10000000
		#define PORT_FEATURE_MBA_LINK_SPEED_1G               0x14000000
		#define PORT_FEATURE_MBA_LINK_SPEED_2_5G             0x18000000
		#define PORT_FEATURE_MBA_LINK_SPEED_10G              0x1c000000
		#define PORT_FEATURE_MBA_LINK_SPEED_20G              0x20000000

	uint32_t Reserved0;                                      /* 0x460 */

	uint32_t mba_vlan_cfg;
	#define PORT_FEATURE_MBA_VLAN_TAG_MASK              0x0000FFFF
	#define PORT_FEATURE_MBA_VLAN_TAG_SHIFT                      0
	#define PORT_FEATURE_MBA_VLAN_EN                    0x00010000
	#define PORT_FEATUTE_BOFM_CFGD_EN                   0x00020000
	#define PORT_FEATURE_BOFM_CFGD_FTGT                 0x00040000
	#define PORT_FEATURE_BOFM_CFGD_VEN                  0x00080000

	uint32_t Reserved1;
	uint32_t smbus_config;
	#define PORT_FEATURE_SMBUS_ADDR_MASK                0x000000fe
	#define PORT_FEATURE_SMBUS_ADDR_SHIFT                        1

	uint32_t vf_config;
	#define PORT_FEAT_CFG_VF_BAR2_SIZE_MASK             0x0000000F
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_SHIFT             0
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_DISABLED          0x00000000
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_4K                0x00000001
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_8K                0x00000002
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_16K               0x00000003
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_32K               0x00000004
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_64K               0x00000005
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_128K              0x00000006
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_256K              0x00000007
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_512K              0x00000008
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_1M                0x00000009
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_2M                0x0000000a
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_4M                0x0000000b
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_8M                0x0000000c
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_16M               0x0000000d
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_32M               0x0000000e
		#define PORT_FEAT_CFG_VF_BAR2_SIZE_64M               0x0000000f

	uint32_t link_config;    /* Used as HW defaults for the driver */

    #define PORT_FEATURE_FLOW_CONTROL_MASK              0x00000700
		#define PORT_FEATURE_FLOW_CONTROL_SHIFT              8
		#define PORT_FEATURE_FLOW_CONTROL_AUTO               0x00000000
		#define PORT_FEATURE_FLOW_CONTROL_TX                 0x00000100
		#define PORT_FEATURE_FLOW_CONTROL_RX                 0x00000200
		#define PORT_FEATURE_FLOW_CONTROL_BOTH               0x00000300
		#define PORT_FEATURE_FLOW_CONTROL_NONE               0x00000400
		#define PORT_FEATURE_FLOW_CONTROL_SAFC_RX            0x00000500
		#define PORT_FEATURE_FLOW_CONTROL_SAFC_TX            0x00000600
		#define PORT_FEATURE_FLOW_CONTROL_SAFC_BOTH          0x00000700

    #define PORT_FEATURE_LINK_SPEED_MASK                0x000F0000
		#define PORT_FEATURE_LINK_SPEED_SHIFT                16
		#define PORT_FEATURE_LINK_SPEED_AUTO                 0x00000000
		#define PORT_FEATURE_LINK_SPEED_10M_HALF             0x00010000
		#define PORT_FEATURE_LINK_SPEED_10M_FULL             0x00020000
		#define PORT_FEATURE_LINK_SPEED_100M_HALF            0x00030000
		#define PORT_FEATURE_LINK_SPEED_100M_FULL            0x00040000
		#define PORT_FEATURE_LINK_SPEED_1G                   0x00050000
		#define PORT_FEATURE_LINK_SPEED_2_5G                 0x00060000
		#define PORT_FEATURE_LINK_SPEED_10G_CX4              0x00070000
		#define PORT_FEATURE_LINK_SPEED_20G                  0x00080000

	#define PORT_FEATURE_CONNECTED_SWITCH_MASK          0x03000000
		#define PORT_FEATURE_CONNECTED_SWITCH_SHIFT          24
		/* (forced) low speed switch (< 10G) */
		#define PORT_FEATURE_CON_SWITCH_1G_SWITCH            0x00000000
		/* (forced) high speed switch (>= 10G) */
		#define PORT_FEATURE_CON_SWITCH_10G_SWITCH           0x01000000
		#define PORT_FEATURE_CON_SWITCH_AUTO_DETECT          0x02000000
		#define PORT_FEATURE_CON_SWITCH_ONE_TIME_DETECT      0x03000000


	/* The default for MCP link configuration,
	   uses the same defines as link_config */
	uint32_t mfw_wol_link_cfg;

	/* The default for the driver of the second external phy,
	   uses the same defines as link_config */
	uint32_t link_config2;				    /* 0x47C */

	/* The default for MCP of the second external phy,
	   uses the same defines as link_config */
	uint32_t mfw_wol_link_cfg2;				    /* 0x480 */


	/*  EEE power saving mode */
	uint32_t eee_power_mode;                                 /* 0x484 */
	#define PORT_FEAT_CFG_EEE_POWER_MODE_MASK                     0x000000FF
	#define PORT_FEAT_CFG_EEE_POWER_MODE_SHIFT                    0
	#define PORT_FEAT_CFG_EEE_POWER_MODE_DISABLED                 0x00000000
	#define PORT_FEAT_CFG_EEE_POWER_MODE_BALANCED                 0x00000001
	#define PORT_FEAT_CFG_EEE_POWER_MODE_AGGRESSIVE               0x00000002
	#define PORT_FEAT_CFG_EEE_POWER_MODE_LOW_LATENCY              0x00000003


	uint32_t Reserved2[16];                                  /* 0x48C */
};

/****************************************************************************
 * Device Information                                                       *
 ****************************************************************************/
struct shm_dev_info {				/* size */

	uint32_t    bc_rev; /* 8 bits each: major, minor, build */	       /* 4 */

	struct shared_hw_cfg     shared_hw_config;	      /* 40 */

	struct port_hw_cfg       port_hw_config[PORT_MAX];     /* 400*2=800 */

	struct shared_feat_cfg   shared_feature_config;		   /* 4 */

	struct port_feat_cfg     port_feature_config[PORT_MAX];/* 116*2=232 */

};

struct extended_dev_info_shared_cfg {             /* NVRAM OFFSET */

	/*  Threshold in celcius to start using the fan */
	uint32_t temperature_monitor1;                           /* 0x4000 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_THRESH_MASK     0x0000007F
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_THRESH_SHIFT    0

	/*  Threshold in celcius to shut down the board */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_THRESH_MASK    0x00007F00
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_THRESH_SHIFT   8

	/*  EPIO of fan temperature status */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_MASK       0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_SHIFT      16
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_NA         0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO0      0x00010000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO1      0x00020000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO2      0x00030000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO3      0x00040000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO4      0x00050000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO5      0x00060000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO6      0x00070000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO7      0x00080000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO8      0x00090000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO9      0x000a0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO10     0x000b0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO11     0x000c0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO12     0x000d0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO13     0x000e0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO14     0x000f0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO15     0x00100000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO16     0x00110000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO17     0x00120000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO18     0x00130000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO19     0x00140000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO20     0x00150000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO21     0x00160000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO22     0x00170000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO23     0x00180000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO24     0x00190000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO25     0x001a0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO26     0x001b0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO27     0x001c0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO28     0x001d0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO29     0x001e0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO30     0x001f0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_FAN_EPIO_EPIO31     0x00200000

	/*  EPIO of shut down temperature status */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_MASK      0xFF000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_SHIFT     24
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_NA        0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO0     0x01000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO1     0x02000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO2     0x03000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO3     0x04000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO4     0x05000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO5     0x06000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO6     0x07000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO7     0x08000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO8     0x09000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO9     0x0a000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO10    0x0b000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO11    0x0c000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO12    0x0d000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO13    0x0e000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO14    0x0f000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO15    0x10000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO16    0x11000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO17    0x12000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO18    0x13000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO19    0x14000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO20    0x15000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO21    0x16000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO22    0x17000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO23    0x18000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO24    0x19000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO25    0x1a000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO26    0x1b000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO27    0x1c000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO28    0x1d000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO29    0x1e000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO30    0x1f000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SHUT_EPIO_EPIO31    0x20000000


	/*  EPIO of shut down temperature status */
	uint32_t temperature_monitor2;                           /* 0x4004 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_PERIOD_MASK         0x0000FFFF
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_PERIOD_SHIFT        0

	/*  Sensor interface - Disabled / BSC / In the future - SMBUS */
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_INTERFACE_MASK    0x00030000
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_INTERFACE_SHIFT   16
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_INTERFACE_DISABLED 0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_INTERFACE_BSC     0x00010000

	/*  On Board Sensor Address */
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_ADDR_MASK         0x03FC0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_SENSOR_ADDR_SHIFT        18

	/*  MFW flavor to be used */
	uint32_t mfw_cfg;                                        /* 0x4008 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_MFW_FLAVOR_MASK          0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_MFW_FLAVOR_SHIFT         0
	#define EXTENDED_DEV_INFO_SHARED_CFG_MFW_FLAVOR_NA            0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_MFW_FLAVOR_A             0x00000001

	/*  Should NIC data query remain enabled upon last drv unload */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_EN_LAST_DRV_MASK     0x00000100
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_EN_LAST_DRV_SHIFT    8
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_EN_LAST_DRV_DISABLED 0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_EN_LAST_DRV_ENABLED  0x00000100

	/*  Prevent OCBB feature */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_PREVENT_MASK        0x00000200
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_PREVENT_SHIFT       9
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_PREVENT_DISABLED    0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OCBB_PREVENT_ENABLED     0x00000200

	/*  Enable DCi support */
	#define EXTENDED_DEV_INFO_SHARED_CFG_DCI_SUPPORT_MASK         0x00000400
	#define EXTENDED_DEV_INFO_SHARED_CFG_DCI_SUPPORT_SHIFT        10
	#define EXTENDED_DEV_INFO_SHARED_CFG_DCI_SUPPORT_DISABLED     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DCI_SUPPORT_ENABLED      0x00000400

	/*  Reserved bits: 75-76 */

	/*  Hide DCBX feature in CCM/BACS menus */
	#define EXTENDED_DEV_INFO_SHARED_CFG_HIDE_DCBX_FEAT_MASK      0x00010000
	#define EXTENDED_DEV_INFO_SHARED_CFG_HIDE_DCBX_FEAT_SHIFT     16
	#define EXTENDED_DEV_INFO_SHARED_CFG_HIDE_DCBX_FEAT_DISABLED  0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_HIDE_DCBX_FEAT_ENABLED   0x00010000

	uint32_t smbus_config;                                   /* 0x400C */
	#define EXTENDED_DEV_INFO_SHARED_CFG_SMBUS_ADDR_MASK          0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_SMBUS_ADDR_SHIFT         0

	/*  Switching regulator loop gain */
	uint32_t board_cfg;                                      /* 0x4010 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_MASK           0x0000000F
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_SHIFT          0
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_HW_DEFAULT     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_X2             0x00000008
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_X4             0x00000009
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_X8             0x0000000a
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_X16            0x0000000b
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_DIV8           0x0000000c
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_DIV4           0x0000000d
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_DIV2           0x0000000e
	#define EXTENDED_DEV_INFO_SHARED_CFG_LOOP_GAIN_X1             0x0000000f

	/*  whether shadow swim feature is supported */
	#define EXTENDED_DEV_INFO_SHARED_CFG_SHADOW_SWIM_MASK         0x00000100
	#define EXTENDED_DEV_INFO_SHARED_CFG_SHADOW_SWIM_SHIFT        8
	#define EXTENDED_DEV_INFO_SHARED_CFG_SHADOW_SWIM_DISABLED     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_SHADOW_SWIM_ENABLED      0x00000100

    /*  whether to show/hide SRIOV menu in CCM */
	#define EXTENDED_DEV_INFO_SHARED_CFG_SRIOV_SHOW_MENU_MASK     0x00000200
	#define EXTENDED_DEV_INFO_SHARED_CFG_SRIOV_SHOW_MENU_SHIFT    9
	#define EXTENDED_DEV_INFO_SHARED_CFG_SRIOV_SHOW_MENU          0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_SRIOV_HIDE_MENU          0x00000200

	/*  Overide PCIE revision ID when enabled the,
	    revision ID will set to B1=='0x11' */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVR_REV_ID_MASK          0x00000400
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVR_REV_ID_SHIFT         10
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVR_REV_ID_DISABLED      0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVR_REV_ID_ENABLED       0x00000400

	/*  Bypass slicer offset tuning */
	#define EXTENDED_DEV_INFO_SHARED_CFG_BYPASS_SLICER_MASK       0x00000800
	#define EXTENDED_DEV_INFO_SHARED_CFG_BYPASS_SLICER_SHIFT      11
	#define EXTENDED_DEV_INFO_SHARED_CFG_BYPASS_SLICER_DISABLED   0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_BYPASS_SLICER_ENABLED    0x00000800
	/*  Control Revision ID */
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_MASK         0x00003000
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_SHIFT        12
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_PRESERVE     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_ACTUAL       0x00001000
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_FORCE_B0     0x00002000
	#define EXTENDED_DEV_INFO_SHARED_CFG_REV_ID_CTRL_FORCE_B1     0x00003000
	/*  Threshold in celcius for max continuous operation */
	uint32_t temperature_report;                             /* 0x4014 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_MCOT_MASK           0x0000007F
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_MCOT_SHIFT          0

	/*  Threshold in celcius for sensor caution */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SCT_MASK            0x00007F00
	#define EXTENDED_DEV_INFO_SHARED_CFG_TEMP_SCT_SHIFT           8

	/*  wwn node prefix to be used (unless value is 0) */
	uint32_t wwn_prefix;                                     /* 0x4018 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_NODE_PREFIX0_MASK    0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_NODE_PREFIX0_SHIFT   0

	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_NODE_PREFIX1_MASK    0x0000FF00
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_NODE_PREFIX1_SHIFT   8

	/*  wwn port prefix to be used (unless value is 0) */
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_PORT_PREFIX0_MASK    0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_PORT_PREFIX0_SHIFT   16

	/*  wwn port prefix to be used (unless value is 0) */
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_PORT_PREFIX1_MASK    0xFF000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_WWN_PORT_PREFIX1_SHIFT   24

	/*  General debug nvm cfg */
	uint32_t dbg_cfg_flags;                                  /* 0x401C */
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_MASK                 0x000FFFFF
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SHIFT                0
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_ENABLE               0x00000001
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_EN_SIGDET_FILTER     0x00000002
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SET_LP_TX_PRESET7    0x00000004
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SET_TX_ANA_DEFAULT   0x00000008
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SET_PLL_ANA_DEFAULT  0x00000010
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_FORCE_G1PLL_RETUNE   0x00000020
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SET_RX_ANA_DEFAULT   0x00000040
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_FORCE_SERDES_RX_CLK  0x00000080
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_DIS_RX_LP_EIEOS      0x00000100
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_FINALIZE_UCODE       0x00000200
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_HOLDOFF_REQ          0x00000400
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_RX_SIGDET_OVERRIDE   0x00000800
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_GP_PORG_UC_RESET     0x00001000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SUPPRESS_COMPEN_EVT  0x00002000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_ADJ_TXEQ_P0_P1       0x00004000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_G3_PLL_RETUNE        0x00008000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_SET_MAC_PHY_CTL8     0x00010000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_DIS_MAC_G3_FRM_ERR   0x00020000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_INFERRED_EI          0x00040000
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_GEN3_COMPLI_ENA      0x00080000

	/*  Override Rx signal detect threshold when enabled the threshold
	 * will be set staticaly
	 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_RX_SIG_MASK     0x00100000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_RX_SIG_SHIFT    20
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_RX_SIG_DISABLED 0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_RX_SIG_ENABLED  0x00100000

	/*  Debug signet rx threshold */
	uint32_t dbg_rx_sigdet_threshold;                        /* 0x4020 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_RX_SIGDET_MASK       0x00000007
	#define EXTENDED_DEV_INFO_SHARED_CFG_DBG_RX_SIGDET_SHIFT      0

    /*  Enable IFFE feature */
	uint32_t iffe_features;                                  /* 0x4024 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_ENABLE_IFFE_MASK         0x00000001
	#define EXTENDED_DEV_INFO_SHARED_CFG_ENABLE_IFFE_SHIFT        0
	#define EXTENDED_DEV_INFO_SHARED_CFG_ENABLE_IFFE_DISABLED     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_ENABLE_IFFE_ENABLED      0x00000001

	/*  Allowable port enablement (bitmask for ports 3-1) */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_PORT_MASK       0x0000000E
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_PORT_SHIFT      1

	/*  Allow iSCSI offload override */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_ISCSI_MASK      0x00000010
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_ISCSI_SHIFT     4
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_ISCSI_DISABLED  0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_ISCSI_ENABLED   0x00000010

	/*  Allow FCoE offload override */
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_FCOE_MASK       0x00000020
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_FCOE_SHIFT      5
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_FCOE_DISABLED   0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_OVERRIDE_FCOE_ENABLED    0x00000020

	/*  Tie to adaptor */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TIE_ADAPTOR_MASK         0x00008000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TIE_ADAPTOR_SHIFT        15
	#define EXTENDED_DEV_INFO_SHARED_CFG_TIE_ADAPTOR_DISABLED     0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TIE_ADAPTOR_ENABLED      0x00008000

	/*  Currently enabled port(s) (bitmask for ports 3-1) */
	uint32_t current_iffe_mask;                              /* 0x4028 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_CFG_MASK         0x0000000E
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_CFG_SHIFT        1

	/*  Current iSCSI offload  */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_ISCSI_MASK       0x00000010
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_ISCSI_SHIFT      4
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_ISCSI_DISABLED   0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_ISCSI_ENABLED    0x00000010

	/*  Current FCoE offload  */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_FCOE_MASK        0x00000020
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_FCOE_SHIFT       5
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_FCOE_DISABLED    0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_CURRENT_FCOE_ENABLED     0x00000020

	/* FW set this pin to "0" (assert) these signal if either of its MAC
	 * or PHY specific threshold values is exceeded.
	 * Values are standard GPIO/EPIO pins.
	 */
	uint32_t threshold_pin;                                  /* 0x402C */
	#define EXTENDED_DEV_INFO_SHARED_CFG_TCONTROL_PIN_MASK        0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_TCONTROL_PIN_SHIFT       0
	#define EXTENDED_DEV_INFO_SHARED_CFG_TWARNING_PIN_MASK        0x0000FF00
	#define EXTENDED_DEV_INFO_SHARED_CFG_TWARNING_PIN_SHIFT       8
	#define EXTENDED_DEV_INFO_SHARED_CFG_TCRITICAL_PIN_MASK       0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_TCRITICAL_PIN_SHIFT      16

	/* MAC die temperature threshold in Celsius. */
	uint32_t mac_threshold_val;                              /* 0x4030 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CONTROL_MAC_THRESH_MASK  0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_CONTROL_MAC_THRESH_SHIFT 0
	#define EXTENDED_DEV_INFO_SHARED_CFG_WARNING_MAC_THRESH_MASK  0x0000FF00
	#define EXTENDED_DEV_INFO_SHARED_CFG_WARNING_MAC_THRESH_SHIFT 8
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRITICAL_MAC_THRESH_MASK 0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRITICAL_MAC_THRESH_SHIFT 16

	/*  PHY die temperature threshold in Celsius. */
	uint32_t phy_threshold_val;                              /* 0x4034 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CONTROL_PHY_THRESH_MASK  0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_CONTROL_PHY_THRESH_SHIFT 0
	#define EXTENDED_DEV_INFO_SHARED_CFG_WARNING_PHY_THRESH_MASK  0x0000FF00
	#define EXTENDED_DEV_INFO_SHARED_CFG_WARNING_PHY_THRESH_SHIFT 8
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRITICAL_PHY_THRESH_MASK 0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRITICAL_PHY_THRESH_SHIFT 16

	/* External pins to communicate with host.
	 * Values are standard GPIO/EPIO pins.
	 */
	uint32_t host_pin;                                       /* 0x4038 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_I2C_ISOLATE_MASK         0x000000FF
	#define EXTENDED_DEV_INFO_SHARED_CFG_I2C_ISOLATE_SHIFT        0
	#define EXTENDED_DEV_INFO_SHARED_CFG_MEZZ_FAULT_MASK          0x0000FF00
	#define EXTENDED_DEV_INFO_SHARED_CFG_MEZZ_FAULT_SHIFT         8
	#define EXTENDED_DEV_INFO_SHARED_CFG_MEZZ_VPD_UPDATE_MASK     0x00FF0000
	#define EXTENDED_DEV_INFO_SHARED_CFG_MEZZ_VPD_UPDATE_SHIFT    16
	#define EXTENDED_DEV_INFO_SHARED_CFG_VPD_CACHE_COMP_MASK      0xFF000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_VPD_CACHE_COMP_SHIFT     24

	/*  Manufacture kit version */
	uint32_t manufacture_ver;                                /* 0x403C */

	/*  Manufacture timestamp */
	uint32_t manufacture_data;                               /* 0x4040 */

	/*  Number of ISCSI/FCOE cfg images */
	#define EXTENDED_DEV_INFO_SHARED_CFG_NUM_ISCSI_FCOE_CFGS_MASK 0x00040000
	#define EXTENDED_DEV_INFO_SHARED_CFG_NUM_ISCSI_FCOE_CFGS_SHIFT18
	#define EXTENDED_DEV_INFO_SHARED_CFG_NUM_ISCSI_FCOE_CFGS_2    0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_NUM_ISCSI_FCOE_CFGS_4    0x00040000

	/*  MCP crash dump trigger */
	uint32_t mcp_crash_dump;                                 /* 0x4044 */
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRASH_DUMP_MASK          0x7FFFFFFF
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRASH_DUMP_SHIFT         0
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRASH_DUMP_DISABLED      0x00000000
	#define EXTENDED_DEV_INFO_SHARED_CFG_CRASH_DUMP_ENABLED       0x00000001

	/*  MBI version */
	uint32_t mbi_version;                                    /* 0x4048 */

	/*  MBI date */
	uint32_t mbi_date;                                       /* 0x404C */
};


#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
	#error "Missing either LITTLE_ENDIAN or BIG_ENDIAN definition."
#endif

#define FUNC_0              0
#define FUNC_1              1
#define FUNC_2              2
#define FUNC_3              3
#define FUNC_4              4
#define FUNC_5              5
#define FUNC_6              6
#define FUNC_7              7
#define E1_FUNC_MAX         2
#define E1H_FUNC_MAX            8
#define E2_FUNC_MAX         4   /* per path */

#define VN_0                0
#define VN_1                1
#define VN_2                2
#define VN_3                3
#define E1VN_MAX            1
#define E1HVN_MAX           4

#define E2_VF_MAX           64  /* HC_REG_VF_CONFIGURATION_SIZE */
/* This value (in milliseconds) determines the frequency of the driver
 * issuing the PULSE message code.  The firmware monitors this periodic
 * pulse to determine when to switch to an OS-absent mode. */
#define DRV_PULSE_PERIOD_MS     250

/* This value (in milliseconds) determines how long the driver should
 * wait for an acknowledgement from the firmware before timing out.  Once
 * the firmware has timed out, the driver will assume there is no firmware
 * running and there won't be any firmware-driver synchronization during a
 * driver reset. */
#define FW_ACK_TIME_OUT_MS      5000

#define FW_ACK_POLL_TIME_MS     1

#define FW_ACK_NUM_OF_POLL  (FW_ACK_TIME_OUT_MS/FW_ACK_POLL_TIME_MS)

#define MFW_TRACE_SIGNATURE     0x54524342

/****************************************************************************
 * Driver <-> FW Mailbox                                                    *
 ****************************************************************************/
struct drv_port_mb {

	uint32_t link_status;
	/* Driver should update this field on any link change event */

	#define LINK_STATUS_NONE				(0<<0)
	#define LINK_STATUS_LINK_FLAG_MASK			0x00000001
	#define LINK_STATUS_LINK_UP				0x00000001
	#define LINK_STATUS_SPEED_AND_DUPLEX_MASK		0x0000001E
	#define LINK_STATUS_SPEED_AND_DUPLEX_AN_NOT_COMPLETE	(0<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10THD		(1<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10TFD		(2<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100TXHD		(3<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100T4		(4<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_100TXFD		(5<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		(6<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(7<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_1000XFD		(7<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500THD		(8<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500TFD		(9<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_2500XFD		(9<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10GTFD		(10<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_10GXFD		(10<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_20GTFD		(11<<1)
	#define LINK_STATUS_SPEED_AND_DUPLEX_20GXFD		(11<<1)

	#define LINK_STATUS_AUTO_NEGOTIATE_FLAG_MASK		0x00000020
	#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED		0x00000020

	#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE		0x00000040
	#define LINK_STATUS_PARALLEL_DETECTION_FLAG_MASK	0x00000080
	#define LINK_STATUS_PARALLEL_DETECTION_USED		0x00000080

	#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE	0x00000200
	#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE	0x00000400
	#define LINK_STATUS_LINK_PARTNER_100T4_CAPABLE		0x00000800
	#define LINK_STATUS_LINK_PARTNER_100TXFD_CAPABLE	0x00001000
	#define LINK_STATUS_LINK_PARTNER_100TXHD_CAPABLE	0x00002000
	#define LINK_STATUS_LINK_PARTNER_10TFD_CAPABLE		0x00004000
	#define LINK_STATUS_LINK_PARTNER_10THD_CAPABLE		0x00008000

	#define LINK_STATUS_TX_FLOW_CONTROL_FLAG_MASK		0x00010000
	#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED		0x00010000

	#define LINK_STATUS_RX_FLOW_CONTROL_FLAG_MASK		0x00020000
	#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED		0x00020000

	#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK	0x000C0000
	#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE	(0<<18)
	#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	(1<<18)
	#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE	(2<<18)
	#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE		(3<<18)

	#define LINK_STATUS_SERDES_LINK				0x00100000

	#define LINK_STATUS_LINK_PARTNER_2500XFD_CAPABLE	0x00200000
	#define LINK_STATUS_LINK_PARTNER_2500XHD_CAPABLE	0x00400000
	#define LINK_STATUS_LINK_PARTNER_10GXFD_CAPABLE		0x00800000
	#define LINK_STATUS_LINK_PARTNER_20GXFD_CAPABLE		0x10000000

	#define LINK_STATUS_PFC_ENABLED				0x20000000

	#define LINK_STATUS_PHYSICAL_LINK_FLAG			0x40000000
	#define LINK_STATUS_SFP_TX_FAULT			0x80000000

	uint32_t port_stx;

	uint32_t stat_nig_timer;

	/* MCP firmware does not use this field */
	uint32_t ext_phy_fw_version;

};


struct drv_func_mb {

	uint32_t drv_mb_header;
	#define DRV_MSG_CODE_MASK                       0xffff0000
	#define DRV_MSG_CODE_LOAD_REQ                   0x10000000
	#define DRV_MSG_CODE_LOAD_DONE                  0x11000000
	#define DRV_MSG_CODE_UNLOAD_REQ_WOL_EN          0x20000000
	#define DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS         0x20010000
	#define DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP         0x20020000
	#define DRV_MSG_CODE_UNLOAD_DONE                0x21000000
	#define DRV_MSG_CODE_DCC_OK                     0x30000000
	#define DRV_MSG_CODE_DCC_FAILURE                0x31000000
	#define DRV_MSG_CODE_DIAG_ENTER_REQ             0x50000000
	#define DRV_MSG_CODE_DIAG_EXIT_REQ              0x60000000
	#define DRV_MSG_CODE_VALIDATE_KEY               0x70000000
	#define DRV_MSG_CODE_GET_CURR_KEY               0x80000000
	#define DRV_MSG_CODE_GET_UPGRADE_KEY            0x81000000
	#define DRV_MSG_CODE_GET_MANUF_KEY              0x82000000
	#define DRV_MSG_CODE_LOAD_L2B_PRAM              0x90000000
	#define DRV_MSG_CODE_OEM_OK			0x00010000
	#define DRV_MSG_CODE_OEM_FAILURE		0x00020000
	#define DRV_MSG_CODE_OEM_UPDATE_SVID_OK		0x00030000
	#define DRV_MSG_CODE_OEM_UPDATE_SVID_FAILURE	0x00040000

	/*
	 * The optic module verification command requires bootcode
	 * v5.0.6 or later, te specific optic module verification command
	 * requires bootcode v5.2.12 or later
	 */
	#define DRV_MSG_CODE_VRFY_FIRST_PHY_OPT_MDL     0xa0000000
	#define REQ_BC_VER_4_VRFY_FIRST_PHY_OPT_MDL     0x00050006
	#define DRV_MSG_CODE_VRFY_SPECIFIC_PHY_OPT_MDL  0xa1000000
	#define REQ_BC_VER_4_VRFY_SPECIFIC_PHY_OPT_MDL  0x00050234
	#define DRV_MSG_CODE_VRFY_AFEX_SUPPORTED        0xa2000000
	#define REQ_BC_VER_4_VRFY_AFEX_SUPPORTED        0x00070002
	#define REQ_BC_VER_4_SFP_TX_DISABLE_SUPPORTED   0x00070014
	#define REQ_BC_VER_4_MT_SUPPORTED               0x00070201
	#define REQ_BC_VER_4_PFC_STATS_SUPPORTED        0x00070201
	#define REQ_BC_VER_4_FCOE_FEATURES              0x00070209

	#define DRV_MSG_CODE_DCBX_ADMIN_PMF_MSG         0xb0000000
	#define DRV_MSG_CODE_DCBX_PMF_DRV_OK            0xb2000000
	#define REQ_BC_VER_4_DCBX_ADMIN_MSG_NON_PMF     0x00070401

	#define DRV_MSG_CODE_VF_DISABLED_DONE           0xc0000000

	#define DRV_MSG_CODE_AFEX_DRIVER_SETMAC         0xd0000000
	#define DRV_MSG_CODE_AFEX_LISTGET_ACK           0xd1000000
	#define DRV_MSG_CODE_AFEX_LISTSET_ACK           0xd2000000
	#define DRV_MSG_CODE_AFEX_STATSGET_ACK          0xd3000000
	#define DRV_MSG_CODE_AFEX_VIFSET_ACK            0xd4000000

	#define DRV_MSG_CODE_DRV_INFO_ACK               0xd8000000
	#define DRV_MSG_CODE_DRV_INFO_NACK              0xd9000000

	#define DRV_MSG_CODE_EEE_RESULTS_ACK            0xda000000

	#define DRV_MSG_CODE_RMMOD                      0xdb000000
	#define REQ_BC_VER_4_RMMOD_CMD                  0x0007080f

	#define DRV_MSG_CODE_SET_MF_BW                  0xe0000000
	#define REQ_BC_VER_4_SET_MF_BW                  0x00060202
	#define DRV_MSG_CODE_SET_MF_BW_ACK              0xe1000000

	#define DRV_MSG_CODE_LINK_STATUS_CHANGED        0x01000000

	#define DRV_MSG_CODE_INITIATE_FLR               0x02000000
	#define REQ_BC_VER_4_INITIATE_FLR               0x00070213

	#define BIOS_MSG_CODE_LIC_CHALLENGE             0xff010000
	#define BIOS_MSG_CODE_LIC_RESPONSE              0xff020000
	#define BIOS_MSG_CODE_VIRT_MAC_PRIM             0xff030000
	#define BIOS_MSG_CODE_VIRT_MAC_ISCSI            0xff040000

	#define DRV_MSG_CODE_IMG_OFFSET_REQ             0xe2000000
	#define DRV_MSG_CODE_IMG_SIZE_REQ               0xe3000000

	#define DRV_MSG_CODE_UFP_CONFIG_ACK             0xe4000000

	#define DRV_MSG_SEQ_NUMBER_MASK                 0x0000ffff

	#define DRV_MSG_CODE_CONFIG_CHANGE              0xC1000000

	uint32_t drv_mb_param;
	#define DRV_MSG_CODE_SET_MF_BW_MIN_MASK         0x00ff0000
	#define DRV_MSG_CODE_SET_MF_BW_MAX_MASK         0xff000000

	#define DRV_MSG_CODE_UNLOAD_NON_D3_POWER        0x00000001
	#define DRV_MSG_CODE_UNLOAD_SKIP_LINK_RESET     0x00000002

	#define DRV_MSG_CODE_LOAD_REQ_WITH_LFA          0x0000100a
	#define DRV_MSG_CODE_LOAD_REQ_FORCE_LFA         0x00002000

	#define DRV_MSG_CODE_USR_BLK_IMAGE_REQ          0x00000001
	#define DRV_MSG_CODE_ISCSI_PERS_IMAGE_REQ       0x00000002
	#define DRV_MSG_CODE_VPD_IMAGE_REQ              0x00000003

	#define DRV_MSG_CODE_CONFIG_CHANGE_MTU_SIZE     0x00000001
	#define DRV_MSG_CODE_CONFIG_CHANGE_MAC_ADD      0x00000002
	#define DRV_MSG_CODE_CONFIG_CHANGE_WOL_ENA      0x00000003
	#define DRV_MSG_CODE_CONFIG_CHANGE_ISCI_BOOT    0x00000004
	#define DRV_MSG_CODE_CONFIG_CHANGE_FCOE_BOOT    0x00000005

	uint32_t fw_mb_header;
	#define FW_MSG_CODE_MASK                        0xffff0000
	#define FW_MSG_CODE_DRV_LOAD_COMMON             0x10100000
	#define FW_MSG_CODE_DRV_LOAD_PORT               0x10110000
	#define FW_MSG_CODE_DRV_LOAD_FUNCTION           0x10120000
	/* Load common chip is supported from bc 6.0.0  */
	#define REQ_BC_VER_4_DRV_LOAD_COMMON_CHIP       0x00060000
	#define FW_MSG_CODE_DRV_LOAD_COMMON_CHIP        0x10130000

	#define FW_MSG_CODE_DRV_LOAD_REFUSED            0x10200000
	#define FW_MSG_CODE_DRV_LOAD_DONE               0x11100000
	#define FW_MSG_CODE_DRV_UNLOAD_COMMON           0x20100000
	#define FW_MSG_CODE_DRV_UNLOAD_PORT             0x20110000
	#define FW_MSG_CODE_DRV_UNLOAD_FUNCTION         0x20120000
	#define FW_MSG_CODE_DRV_UNLOAD_DONE             0x21100000
	#define FW_MSG_CODE_DCC_DONE                    0x30100000
	#define FW_MSG_CODE_LLDP_DONE                   0x40100000
	#define FW_MSG_CODE_DIAG_ENTER_DONE             0x50100000
	#define FW_MSG_CODE_DIAG_REFUSE                 0x50200000
	#define FW_MSG_CODE_DIAG_EXIT_DONE              0x60100000
	#define FW_MSG_CODE_VALIDATE_KEY_SUCCESS        0x70100000
	#define FW_MSG_CODE_VALIDATE_KEY_FAILURE        0x70200000
	#define FW_MSG_CODE_GET_KEY_DONE                0x80100000
	#define FW_MSG_CODE_NO_KEY                      0x80f00000
	#define FW_MSG_CODE_LIC_INFO_NOT_READY          0x80f80000
	#define FW_MSG_CODE_L2B_PRAM_LOADED             0x90100000
	#define FW_MSG_CODE_L2B_PRAM_T_LOAD_FAILURE     0x90210000
	#define FW_MSG_CODE_L2B_PRAM_C_LOAD_FAILURE     0x90220000
	#define FW_MSG_CODE_L2B_PRAM_X_LOAD_FAILURE     0x90230000
	#define FW_MSG_CODE_L2B_PRAM_U_LOAD_FAILURE     0x90240000
	#define FW_MSG_CODE_VRFY_OPT_MDL_SUCCESS        0xa0100000
	#define FW_MSG_CODE_VRFY_OPT_MDL_INVLD_IMG      0xa0200000
	#define FW_MSG_CODE_VRFY_OPT_MDL_UNAPPROVED     0xa0300000
	#define FW_MSG_CODE_VF_DISABLED_DONE            0xb0000000
	#define FW_MSG_CODE_HW_SET_INVALID_IMAGE        0xb0100000

	#define FW_MSG_CODE_AFEX_DRIVER_SETMAC_DONE     0xd0100000
	#define FW_MSG_CODE_AFEX_LISTGET_ACK            0xd1100000
	#define FW_MSG_CODE_AFEX_LISTSET_ACK            0xd2100000
	#define FW_MSG_CODE_AFEX_STATSGET_ACK           0xd3100000
	#define FW_MSG_CODE_AFEX_VIFSET_ACK             0xd4100000

	#define FW_MSG_CODE_DRV_INFO_ACK                0xd8100000
	#define FW_MSG_CODE_DRV_INFO_NACK               0xd9100000

	#define FW_MSG_CODE_EEE_RESULS_ACK              0xda100000

	#define FW_MSG_CODE_RMMOD_ACK                   0xdb100000

	#define FW_MSG_CODE_SET_MF_BW_SENT              0xe0000000
	#define FW_MSG_CODE_SET_MF_BW_DONE              0xe1000000

	#define FW_MSG_CODE_LINK_CHANGED_ACK            0x01100000

	#define FW_MSG_CODE_FLR_ACK                     0x02000000
	#define FW_MSG_CODE_FLR_NACK                    0x02100000

	#define FW_MSG_CODE_LIC_CHALLENGE               0xff010000
	#define FW_MSG_CODE_LIC_RESPONSE                0xff020000
	#define FW_MSG_CODE_VIRT_MAC_PRIM               0xff030000
	#define FW_MSG_CODE_VIRT_MAC_ISCSI              0xff040000

	#define FW_MSG_CODE_IMG_OFFSET_RESPONSE         0xe2100000
	#define FW_MSG_CODE_IMG_SIZE_RESPONSE           0xe3100000

	#define FW_MSG_CODE_OEM_ACK			0x00010000
	#define DRV_MSG_CODE_OEM_UPDATE_SVID_ACK	0x00020000

	#define FW_MSG_CODE_CONFIG_CHANGE_DONE          0xC2000000

	#define FW_MSG_SEQ_NUMBER_MASK                  0x0000ffff

	uint32_t fw_mb_param;

	#define FW_PARAM_INVALID_IMG                    0xffffffff

	uint32_t drv_pulse_mb;
	#define DRV_PULSE_SEQ_MASK                      0x00007fff
	#define DRV_PULSE_SYSTEM_TIME_MASK              0xffff0000
	/*
	 * The system time is in the format of
	 * (year-2001)*12*32 + month*32 + day.
	 */
	#define DRV_PULSE_ALWAYS_ALIVE                  0x00008000
	/*
	 * Indicate to the firmware not to go into the
	 * OS-absent when it is not getting driver pulse.
	 * This is used for debugging as well for PXE(MBA).
	 */

	uint32_t mcp_pulse_mb;
	#define MCP_PULSE_SEQ_MASK                      0x00007fff
	#define MCP_PULSE_ALWAYS_ALIVE                  0x00008000
	/* Indicates to the driver not to assert due to lack
	 * of MCP response */
	#define MCP_EVENT_MASK                          0xffff0000
	#define MCP_EVENT_OTHER_DRIVER_RESET_REQ        0x00010000

	uint32_t iscsi_boot_signature;
	uint32_t iscsi_boot_block_offset;

	uint32_t drv_status;
	#define DRV_STATUS_PMF                          0x00000001
	#define DRV_STATUS_VF_DISABLED                  0x00000002
	#define DRV_STATUS_SET_MF_BW                    0x00000004
	#define DRV_STATUS_LINK_EVENT                   0x00000008

	#define DRV_STATUS_OEM_EVENT_MASK               0x00000070
	#define DRV_STATUS_OEM_DISABLE_ENABLE_PF        0x00000010
	#define DRV_STATUS_OEM_BANDWIDTH_ALLOCATION     0x00000020
	#define DRV_STATUS_OEM_FC_NPIV_UPDATE           0x00000040

	#define DRV_STATUS_OEM_UPDATE_SVID              0x00000080

	#define DRV_STATUS_DCC_EVENT_MASK               0x0000ff00
	#define DRV_STATUS_DCC_DISABLE_ENABLE_PF        0x00000100
	#define DRV_STATUS_DCC_BANDWIDTH_ALLOCATION     0x00000200
	#define DRV_STATUS_DCC_CHANGE_MAC_ADDRESS       0x00000400
	#define DRV_STATUS_DCC_RESERVED1                0x00000800
	#define DRV_STATUS_DCC_SET_PROTOCOL             0x00001000
	#define DRV_STATUS_DCC_SET_PRIORITY             0x00002000

	#define DRV_STATUS_DCBX_EVENT_MASK              0x000f0000
	#define DRV_STATUS_DCBX_NEGOTIATION_RESULTS     0x00010000
	#define DRV_STATUS_AFEX_EVENT_MASK              0x03f00000
	#define DRV_STATUS_AFEX_LISTGET_REQ             0x00100000
	#define DRV_STATUS_AFEX_LISTSET_REQ             0x00200000
	#define DRV_STATUS_AFEX_STATSGET_REQ            0x00400000
	#define DRV_STATUS_AFEX_VIFSET_REQ              0x00800000

	#define DRV_STATUS_DRV_INFO_REQ                 0x04000000

	#define DRV_STATUS_EEE_NEGOTIATION_RESULTS      0x08000000

	uint32_t virt_mac_upper;
	#define VIRT_MAC_SIGN_MASK                      0xffff0000
	#define VIRT_MAC_SIGNATURE                      0x564d0000
	uint32_t virt_mac_lower;

};


/****************************************************************************
 * Management firmware state                                                *
 ****************************************************************************/
/* Allocate 440 bytes for management firmware */
#define MGMTFW_STATE_WORD_SIZE                          110

struct mgmtfw_state {
	uint32_t opaque[MGMTFW_STATE_WORD_SIZE];
};


/****************************************************************************
 * Multi-Function configuration                                             *
 ****************************************************************************/
struct shared_mf_cfg {

	uint32_t clp_mb;
	#define SHARED_MF_CLP_SET_DEFAULT               0x00000000
	/* set by CLP */
	#define SHARED_MF_CLP_EXIT                      0x00000001
	/* set by MCP */
	#define SHARED_MF_CLP_EXIT_DONE                 0x00010000

};

struct port_mf_cfg {

	uint32_t dynamic_cfg;    /* device control channel */
	#define PORT_MF_CFG_E1HOV_TAG_MASK              0x0000ffff
	#define PORT_MF_CFG_E1HOV_TAG_SHIFT             0
	#define PORT_MF_CFG_E1HOV_TAG_DEFAULT         PORT_MF_CFG_E1HOV_TAG_MASK

	uint32_t reserved[1];

};

struct func_mf_cfg {

	uint32_t config;
	/* E/R/I/D */
	/* function 0 of each port cannot be hidden */
	#define FUNC_MF_CFG_FUNC_HIDE                   0x00000001

	#define FUNC_MF_CFG_PROTOCOL_MASK               0x00000006
	#define FUNC_MF_CFG_PROTOCOL_FCOE               0x00000000
	#define FUNC_MF_CFG_PROTOCOL_ETHERNET           0x00000002
	#define FUNC_MF_CFG_PROTOCOL_ETHERNET_WITH_RDMA 0x00000004
	#define FUNC_MF_CFG_PROTOCOL_ISCSI              0x00000006
	#define FUNC_MF_CFG_PROTOCOL_DEFAULT \
				FUNC_MF_CFG_PROTOCOL_ETHERNET_WITH_RDMA

	#define FUNC_MF_CFG_FUNC_DISABLED               0x00000008
	#define FUNC_MF_CFG_FUNC_DELETED                0x00000010

	#define FUNC_MF_CFG_FUNC_BOOT_MASK              0x00000060
	#define FUNC_MF_CFG_FUNC_BOOT_BIOS_CTRL         0x00000000
	#define FUNC_MF_CFG_FUNC_BOOT_VCM_DISABLED      0x00000020
	#define FUNC_MF_CFG_FUNC_BOOT_VCM_ENABLED       0x00000040

	/* PRI */
	/* 0 - low priority, 3 - high priority */
	#define FUNC_MF_CFG_TRANSMIT_PRIORITY_MASK      0x00000300
	#define FUNC_MF_CFG_TRANSMIT_PRIORITY_SHIFT     8
	#define FUNC_MF_CFG_TRANSMIT_PRIORITY_DEFAULT   0x00000000

	/* MINBW, MAXBW */
	/* value range - 0..100, increments in 100Mbps */
	#define FUNC_MF_CFG_MIN_BW_MASK                 0x00ff0000
	#define FUNC_MF_CFG_MIN_BW_SHIFT                16
	#define FUNC_MF_CFG_MIN_BW_DEFAULT              0x00000000
	#define FUNC_MF_CFG_MAX_BW_MASK                 0xff000000
	#define FUNC_MF_CFG_MAX_BW_SHIFT                24
	#define FUNC_MF_CFG_MAX_BW_DEFAULT              0x64000000

	uint32_t mac_upper;	    /* MAC */
	#define FUNC_MF_CFG_UPPERMAC_MASK               0x0000ffff
	#define FUNC_MF_CFG_UPPERMAC_SHIFT              0
	#define FUNC_MF_CFG_UPPERMAC_DEFAULT           FUNC_MF_CFG_UPPERMAC_MASK
	uint32_t mac_lower;
	#define FUNC_MF_CFG_LOWERMAC_DEFAULT            0xffffffff

	uint32_t e1hov_tag;	/* VNI */
	#define FUNC_MF_CFG_E1HOV_TAG_MASK              0x0000ffff
	#define FUNC_MF_CFG_E1HOV_TAG_SHIFT             0
	#define FUNC_MF_CFG_E1HOV_TAG_DEFAULT         FUNC_MF_CFG_E1HOV_TAG_MASK

	/* afex default VLAN ID - 12 bits */
	#define FUNC_MF_CFG_AFEX_VLAN_MASK              0x0fff0000
	#define FUNC_MF_CFG_AFEX_VLAN_SHIFT             16

	uint32_t afex_config;
	#define FUNC_MF_CFG_AFEX_COS_FILTER_MASK                     0x000000ff
	#define FUNC_MF_CFG_AFEX_COS_FILTER_SHIFT                    0
	#define FUNC_MF_CFG_AFEX_MBA_ENABLED_MASK                    0x0000ff00
	#define FUNC_MF_CFG_AFEX_MBA_ENABLED_SHIFT                   8
	#define FUNC_MF_CFG_AFEX_MBA_ENABLED_VAL                     0x00000100
	#define FUNC_MF_CFG_AFEX_VLAN_MODE_MASK                      0x000f0000
	#define FUNC_MF_CFG_AFEX_VLAN_MODE_SHIFT                     16

	uint32_t pf_allocation;
	/* number of vfs in function, if 0 - sriov disabled */
	#define FUNC_MF_CFG_NUMBER_OF_VFS_MASK                      0x000000FF
	#define FUNC_MF_CFG_NUMBER_OF_VFS_SHIFT                     0
};

enum mf_cfg_afex_vlan_mode {
	FUNC_MF_CFG_AFEX_VLAN_TRUNK_MODE = 0,
	FUNC_MF_CFG_AFEX_VLAN_ACCESS_MODE,
	FUNC_MF_CFG_AFEX_VLAN_TRUNK_TAG_NATIVE_MODE
};

/* This structure is not applicable and should not be accessed on 57711 */
struct func_ext_cfg {
	uint32_t func_cfg;
	#define MACP_FUNC_CFG_FLAGS_MASK                0x0000007F
	#define MACP_FUNC_CFG_FLAGS_SHIFT               0
	#define MACP_FUNC_CFG_FLAGS_ENABLED             0x00000001
	#define MACP_FUNC_CFG_FLAGS_ETHERNET            0x00000002
	#define MACP_FUNC_CFG_FLAGS_ISCSI_OFFLOAD       0x00000004
	#define MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD        0x00000008
    #define MACP_FUNC_CFG_PAUSE_ON_HOST_RING        0x00000080

	uint32_t iscsi_mac_addr_upper;
	uint32_t iscsi_mac_addr_lower;

	uint32_t fcoe_mac_addr_upper;
	uint32_t fcoe_mac_addr_lower;

	uint32_t fcoe_wwn_port_name_upper;
	uint32_t fcoe_wwn_port_name_lower;

	uint32_t fcoe_wwn_node_name_upper;
	uint32_t fcoe_wwn_node_name_lower;

	uint32_t preserve_data;
	#define MF_FUNC_CFG_PRESERVE_L2_MAC             (1<<0)
	#define MF_FUNC_CFG_PRESERVE_ISCSI_MAC          (1<<1)
	#define MF_FUNC_CFG_PRESERVE_FCOE_MAC           (1<<2)
	#define MF_FUNC_CFG_PRESERVE_FCOE_WWN_P         (1<<3)
	#define MF_FUNC_CFG_PRESERVE_FCOE_WWN_N         (1<<4)
	#define MF_FUNC_CFG_PRESERVE_TX_BW              (1<<5)
};

struct mf_cfg {

	struct shared_mf_cfg    shared_mf_config;       /* 0x4 */
	struct port_mf_cfg  port_mf_config[NVM_PATH_MAX][PORT_MAX];
    /* 0x10*2=0x20 */
	/* for all chips, there are 8 mf functions */
	struct func_mf_cfg  func_mf_config[E1H_FUNC_MAX]; /* 0x18 * 8 = 0xc0 */
	/*
	 * Extended configuration per function  - this array does not exist and
	 * should not be accessed on 57711
	 */
	struct func_ext_cfg func_ext_config[E1H_FUNC_MAX]; /* 0x28 * 8 = 0x140*/
}; /* 0x224 */

/****************************************************************************
 * Shared Memory Region                                                     *
 ****************************************************************************/
struct shmem_region {		       /*   SharedMem Offset (size) */

	uint32_t         validity_map[PORT_MAX];  /* 0x0 (4*2 = 0x8) */
	#define SHR_MEM_FORMAT_REV_MASK                     0xff000000
	#define SHR_MEM_FORMAT_REV_ID                       ('A'<<24)
	/* validity bits */
	#define SHR_MEM_VALIDITY_PCI_CFG                    0x00100000
	#define SHR_MEM_VALIDITY_MB                         0x00200000
	#define SHR_MEM_VALIDITY_DEV_INFO                   0x00400000
	#define SHR_MEM_VALIDITY_RESERVED                   0x00000007
	/* One licensing bit should be set */
	#define SHR_MEM_VALIDITY_LIC_KEY_IN_EFFECT_MASK     0x00000038
	#define SHR_MEM_VALIDITY_LIC_MANUF_KEY_IN_EFFECT    0x00000008
	#define SHR_MEM_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT  0x00000010
	#define SHR_MEM_VALIDITY_LIC_NO_KEY_IN_EFFECT       0x00000020
	/* Active MFW */
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_UNKNOWN         0x00000000
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_MASK            0x000001c0
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_IPMI            0x00000040
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_UMP             0x00000080
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_NCSI            0x000000c0
	#define SHR_MEM_VALIDITY_ACTIVE_MFW_NONE            0x000001c0

	struct shm_dev_info dev_info;	     /* 0x8     (0x438) */

	license_key_t       drv_lic_key[PORT_MAX]; /* 0x440 (52*2=0x68) */

	/* FW information (for internal FW use) */
	uint32_t         fw_info_fio_offset;		/* 0x4a8       (0x4) */
	struct mgmtfw_state mgmtfw_state;	/* 0x4ac     (0x1b8) */

	struct drv_port_mb  port_mb[PORT_MAX];	/* 0x664 (16*2=0x20) */


#ifdef BMAPI
	/* This is a variable length array */
	/* the number of function depends on the chip type */
	struct drv_func_mb func_mb[1];	/* 0x684 (44*2/4/8=0x58/0xb0/0x160) */
#else
	/* the number of function depends on the chip type */
	struct drv_func_mb  func_mb[];	/* 0x684 (44*2/4/8=0x58/0xb0/0x160) */
#endif /* BMAPI */

}; /* 57710 = 0x6dc | 57711 = 0x7E4 | 57712 = 0x734 */

/****************************************************************************
 * Shared Memory 2 Region                                                   *
 ****************************************************************************/
/* The fw_flr_ack is actually built in the following way:                   */
/* 8 bit:  PF ack                                                           */
/* 64 bit: VF ack                                                           */
/* 8 bit:  ios_dis_ack                                                      */
/* In order to maintain endianity in the mailbox hsi, we want to keep using */
/* uint32_t. The fw must have the VF right after the PF since this is how it     */
/* access arrays(it expects always the VF to reside after the PF, and that  */
/* makes the calculation much easier for it. )                              */
/* In order to answer both limitations, and keep the struct small, the code */
/* will abuse the structure defined here to achieve the actual partition    */
/* above                                                                    */
/****************************************************************************/
struct fw_flr_ack {
	uint32_t         pf_ack;
	uint32_t         vf_ack;
	uint32_t         iov_dis_ack;
};

struct fw_flr_mb {
	uint32_t         aggint;
	uint32_t         opgen_addr;
	struct fw_flr_ack ack;
};

struct eee_remote_vals {
	uint32_t         tx_tw;
	uint32_t         rx_tw;
};

/**** SUPPORT FOR SHMEM ARRRAYS ***
 * The SHMEM HSI is aligned on 32 bit boundaries which makes it difficult to
 * define arrays with storage types smaller then unsigned dwords.
 * The macros below add generic support for SHMEM arrays with numeric elements
 * that can span 2,4,8 or 16 bits. The array underlying type is a 32 bit dword
 * array with individual bit-filed elements accessed using shifts and masks.
 *
 */

/* eb is the bitwidth of a single element */
#define SHMEM_ARRAY_MASK(eb)		((1<<(eb))-1)
#define SHMEM_ARRAY_ENTRY(i, eb)	((i)/(32/(eb)))

/* the bit-position macro allows the used to flip the order of the arrays
 * elements on a per byte or word boundary.
 *
 * example: an array with 8 entries each 4 bit wide. This array will fit into
 * a single dword. The diagrmas below show the array order of the nibbles.
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 4) defines the stadard ordering:
 *
 *                |                |                |               |
 *   0    |   1   |   2    |   3   |   4    |   5   |   6   |   7   |
 *                |                |                |               |
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 8) defines a flip ordering per byte:
 *
 *                |                |                |               |
 *   1   |   0    |   3    |   2   |   5    |   4   |   7   |   6   |
 *                |                |                |               |
 *
 * SHMEM_ARRAY_BITPOS(i, 4, 16) defines a flip ordering per word:
 *
 *                |                |                |               |
 *   3   |   2    |   1   |   0    |   7   |   6    |   5   |   4   |
 *                |                |                |               |
 */
#define SHMEM_ARRAY_BITPOS(i, eb, fb)	\
	((((32/(fb)) - 1 - ((i)/((fb)/(eb))) % (32/(fb))) * (fb)) + \
	(((i)%((fb)/(eb))) * (eb)))

#define SHMEM_ARRAY_GET(a, i, eb, fb)					\
	((a[SHMEM_ARRAY_ENTRY(i, eb)] >> SHMEM_ARRAY_BITPOS(i, eb, fb)) &  \
	SHMEM_ARRAY_MASK(eb))

#define SHMEM_ARRAY_SET(a, i, eb, fb, val)				\
do {									   \
	a[SHMEM_ARRAY_ENTRY(i, eb)] &= ~(SHMEM_ARRAY_MASK(eb) <<	   \
	SHMEM_ARRAY_BITPOS(i, eb, fb));					   \
	a[SHMEM_ARRAY_ENTRY(i, eb)] |= (((val) & SHMEM_ARRAY_MASK(eb)) <<  \
	SHMEM_ARRAY_BITPOS(i, eb, fb));					   \
} while (0)


/****START OF DCBX STRUCTURES DECLARATIONS****/
#define DCBX_MAX_NUM_PRI_PG_ENTRIES	8
#define DCBX_PRI_PG_BITWIDTH		4
#define DCBX_PRI_PG_FBITS		8
#define DCBX_PRI_PG_GET(a, i)		\
	SHMEM_ARRAY_GET(a, i, DCBX_PRI_PG_BITWIDTH, DCBX_PRI_PG_FBITS)
#define DCBX_PRI_PG_SET(a, i, val)	\
	SHMEM_ARRAY_SET(a, i, DCBX_PRI_PG_BITWIDTH, DCBX_PRI_PG_FBITS, val)
#define DCBX_MAX_NUM_PG_BW_ENTRIES	8
#define DCBX_BW_PG_BITWIDTH		8
#define DCBX_PG_BW_GET(a, i)		\
	SHMEM_ARRAY_GET(a, i, DCBX_BW_PG_BITWIDTH, DCBX_BW_PG_BITWIDTH)
#define DCBX_PG_BW_SET(a, i, val)	\
	SHMEM_ARRAY_SET(a, i, DCBX_BW_PG_BITWIDTH, DCBX_BW_PG_BITWIDTH, val)
#define DCBX_STRICT_PRI_PG		15
#define DCBX_MAX_APP_PROTOCOL		16
#define DCBX_MAX_APP_LOCAL	    32
#define FCOE_APP_IDX			0
#define ISCSI_APP_IDX			1
#define PREDEFINED_APP_IDX_MAX		2


/* Big/Little endian have the same representation. */
struct dcbx_ets_feature {
	/*
	 * For Admin MIB - is this feature supported by the
	 * driver | For Local MIB - should this feature be enabled.
	 */
	uint32_t enabled;
	uint32_t  pg_bw_tbl[2];
	uint32_t  pri_pg_tbl[1];
};

/* Driver structure in LE */
struct dcbx_pfc_feature {
#ifdef __BIG_ENDIAN
	uint8_t pri_en_bitmap;
	#define DCBX_PFC_PRI_0 0x01
	#define DCBX_PFC_PRI_1 0x02
	#define DCBX_PFC_PRI_2 0x04
	#define DCBX_PFC_PRI_3 0x08
	#define DCBX_PFC_PRI_4 0x10
	#define DCBX_PFC_PRI_5 0x20
	#define DCBX_PFC_PRI_6 0x40
	#define DCBX_PFC_PRI_7 0x80
	uint8_t pfc_caps;
	uint8_t reserved;
	uint8_t enabled;
#elif defined(__LITTLE_ENDIAN)
	uint8_t enabled;
	uint8_t reserved;
	uint8_t pfc_caps;
	uint8_t pri_en_bitmap;
	#define DCBX_PFC_PRI_0 0x01
	#define DCBX_PFC_PRI_1 0x02
	#define DCBX_PFC_PRI_2 0x04
	#define DCBX_PFC_PRI_3 0x08
	#define DCBX_PFC_PRI_4 0x10
	#define DCBX_PFC_PRI_5 0x20
	#define DCBX_PFC_PRI_6 0x40
	#define DCBX_PFC_PRI_7 0x80
#endif
};

struct dcbx_app_priority_entry {
#ifdef __BIG_ENDIAN
	uint16_t  app_id;
	uint8_t  pri_bitmap;
	uint8_t  appBitfield;
	#define DCBX_APP_ENTRY_VALID         0x01
	#define DCBX_APP_ENTRY_SF_MASK       0x30
	#define DCBX_APP_ENTRY_SF_SHIFT      4
	#define DCBX_APP_SF_ETH_TYPE         0x10
	#define DCBX_APP_SF_PORT             0x20
	#define DCBX_APP_PRI_0               0x01
	#define DCBX_APP_PRI_1               0x02
	#define DCBX_APP_PRI_2               0x04
	#define DCBX_APP_PRI_3               0x08
	#define DCBX_APP_PRI_4               0x10
	#define DCBX_APP_PRI_5               0x20
	#define DCBX_APP_PRI_6               0x40
	#define DCBX_APP_PRI_7               0x80
#elif defined(__LITTLE_ENDIAN)
	uint8_t appBitfield;
	#define DCBX_APP_ENTRY_VALID         0x01
	#define DCBX_APP_ENTRY_SF_MASK       0x30
	#define DCBX_APP_ENTRY_SF_SHIFT      4
	#define DCBX_APP_SF_ETH_TYPE         0x10
	#define DCBX_APP_SF_PORT             0x20
	uint8_t  pri_bitmap;
	uint16_t  app_id;
#endif
};


/* FW structure in BE */
struct dcbx_app_priority_feature {
#ifdef __BIG_ENDIAN
	uint8_t reserved;
	uint8_t default_pri;
	uint8_t tc_supported;
	uint8_t enabled;
#elif defined(__LITTLE_ENDIAN)
	uint8_t enabled;
	uint8_t tc_supported;
	uint8_t default_pri;
	uint8_t reserved;
#endif
	struct dcbx_app_priority_entry  app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

/* FW structure in BE */
struct dcbx_features {
	/* PG feature */
	struct dcbx_ets_feature ets;
	/* PFC feature */
	struct dcbx_pfc_feature pfc;
	/* APP feature */
	struct dcbx_app_priority_feature app;
};

/* LLDP protocol parameters */
/* FW structure in BE */
struct lldp_params {
#ifdef __BIG_ENDIAN
	uint8_t  msg_fast_tx_interval;
	uint8_t  msg_tx_hold;
	uint8_t  msg_tx_interval;
	uint8_t  admin_status;
	#define LLDP_TX_ONLY  0x01
	#define LLDP_RX_ONLY  0x02
	#define LLDP_TX_RX    0x03
	#define LLDP_DISABLED 0x04
	uint8_t  reserved1;
	uint8_t  tx_fast;
	uint8_t  tx_crd_max;
	uint8_t  tx_crd;
#elif defined(__LITTLE_ENDIAN)
	uint8_t  admin_status;
	#define LLDP_TX_ONLY  0x01
	#define LLDP_RX_ONLY  0x02
	#define LLDP_TX_RX    0x03
	#define LLDP_DISABLED 0x04
	uint8_t  msg_tx_interval;
	uint8_t  msg_tx_hold;
	uint8_t  msg_fast_tx_interval;
	uint8_t  tx_crd;
	uint8_t  tx_crd_max;
	uint8_t  tx_fast;
	uint8_t  reserved1;
#endif
	#define REM_CHASSIS_ID_STAT_LEN 4
	#define REM_PORT_ID_STAT_LEN 4
	/* Holds remote Chassis ID TLV header, subtype and 9B of payload. */
	uint32_t peer_chassis_id[REM_CHASSIS_ID_STAT_LEN];
	/* Holds remote Port ID TLV header, subtype and 9B of payload. */
	uint32_t peer_port_id[REM_PORT_ID_STAT_LEN];
};

struct lldp_dcbx_stat {
	#define LOCAL_CHASSIS_ID_STAT_LEN 2
	#define LOCAL_PORT_ID_STAT_LEN 2
	/* Holds local Chassis ID 8B payload of constant subtype 4. */
	uint32_t local_chassis_id[LOCAL_CHASSIS_ID_STAT_LEN];
	/* Holds local Port ID 8B payload of constant subtype 3. */
	uint32_t local_port_id[LOCAL_PORT_ID_STAT_LEN];
	/* Number of DCBX frames transmitted. */
	uint32_t num_tx_dcbx_pkts;
	/* Number of DCBX frames received. */
	uint32_t num_rx_dcbx_pkts;
};

/* ADMIN MIB - DCBX local machine default configuration. */
struct lldp_admin_mib {
	uint32_t     ver_cfg_flags;
	#define DCBX_ETS_CONFIG_TX_ENABLED       0x00000001
	#define DCBX_PFC_CONFIG_TX_ENABLED       0x00000002
	#define DCBX_APP_CONFIG_TX_ENABLED       0x00000004
	#define DCBX_ETS_RECO_TX_ENABLED         0x00000008
	#define DCBX_ETS_RECO_VALID              0x00000010
	#define DCBX_ETS_WILLING                 0x00000020
	#define DCBX_PFC_WILLING                 0x00000040
	#define DCBX_APP_WILLING                 0x00000080
	#define DCBX_VERSION_CEE                 0x00000100
	#define DCBX_VERSION_IEEE                0x00000200
	#define DCBX_DCBX_ENABLED                0x00000400
	#define DCBX_CEE_VERSION_MASK            0x0000f000
	#define DCBX_CEE_VERSION_SHIFT           12
	#define DCBX_CEE_MAX_VERSION_MASK        0x000f0000
	#define DCBX_CEE_MAX_VERSION_SHIFT       16
	struct dcbx_features     features;
};

/* REMOTE MIB - remote machine DCBX configuration. */
struct lldp_remote_mib {
	uint32_t prefix_seq_num;
	uint32_t flags;
	#define DCBX_ETS_TLV_RX                  0x00000001
	#define DCBX_PFC_TLV_RX                  0x00000002
	#define DCBX_APP_TLV_RX                  0x00000004
	#define DCBX_ETS_RX_ERROR                0x00000010
	#define DCBX_PFC_RX_ERROR                0x00000020
	#define DCBX_APP_RX_ERROR                0x00000040
	#define DCBX_ETS_REM_WILLING             0x00000100
	#define DCBX_PFC_REM_WILLING             0x00000200
	#define DCBX_APP_REM_WILLING             0x00000400
	#define DCBX_REMOTE_ETS_RECO_VALID       0x00001000
	#define DCBX_REMOTE_MIB_VALID            0x00002000
	struct dcbx_features features;
	uint32_t suffix_seq_num;
};

/* LOCAL MIB - operational DCBX configuration - transmitted on Tx LLDPDU. */
struct lldp_local_mib {
	uint32_t prefix_seq_num;
	/* Indicates if there is mismatch with negotiation results. */
	uint32_t error;
	#define DCBX_LOCAL_ETS_ERROR             0x00000001
	#define DCBX_LOCAL_PFC_ERROR             0x00000002
	#define DCBX_LOCAL_APP_ERROR             0x00000004
	#define DCBX_LOCAL_PFC_MISMATCH          0x00000010
	#define DCBX_LOCAL_APP_MISMATCH          0x00000020
	#define DCBX_REMOTE_MIB_ERROR            0x00000040
	#define DCBX_REMOTE_ETS_TLV_NOT_FOUND    0x00000080
	#define DCBX_REMOTE_PFC_TLV_NOT_FOUND    0x00000100
	#define DCBX_REMOTE_APP_TLV_NOT_FOUND    0x00000200
	struct dcbx_features   features;
	uint32_t suffix_seq_num;
};

struct lldp_local_mib_ext {
	uint32_t prefix_seq_num;
	/* APP TLV extension - 16 more entries for negotiation results*/
	struct dcbx_app_priority_entry  app_pri_tbl_ext[DCBX_MAX_APP_PROTOCOL];
	uint32_t suffix_seq_num;
};
/***END OF DCBX STRUCTURES DECLARATIONS***/

/***********************************************************/
/*                         Elink section                   */
/***********************************************************/
#define SHMEM_LINK_CONFIG_SIZE 2
struct shmem_lfa {
	uint32_t req_duplex;
	#define REQ_DUPLEX_PHY0_MASK        0x0000ffff
	#define REQ_DUPLEX_PHY0_SHIFT       0
	#define REQ_DUPLEX_PHY1_MASK        0xffff0000
	#define REQ_DUPLEX_PHY1_SHIFT       16
	uint32_t req_flow_ctrl;
	#define REQ_FLOW_CTRL_PHY0_MASK     0x0000ffff
	#define REQ_FLOW_CTRL_PHY0_SHIFT    0
	#define REQ_FLOW_CTRL_PHY1_MASK     0xffff0000
	#define REQ_FLOW_CTRL_PHY1_SHIFT    16
	uint32_t req_line_speed; /* Also determine AutoNeg */
	#define REQ_LINE_SPD_PHY0_MASK      0x0000ffff
	#define REQ_LINE_SPD_PHY0_SHIFT     0
	#define REQ_LINE_SPD_PHY1_MASK      0xffff0000
	#define REQ_LINE_SPD_PHY1_SHIFT     16
	uint32_t speed_cap_mask[SHMEM_LINK_CONFIG_SIZE];
	uint32_t additional_config;
	#define REQ_FC_AUTO_ADV_MASK        0x0000ffff
	#define REQ_FC_AUTO_ADV0_SHIFT      0
	#define NO_LFA_DUE_TO_DCC_MASK      0x00010000
	uint32_t lfa_sts;
	#define LFA_LINK_FLAP_REASON_OFFSET		0
	#define LFA_LINK_FLAP_REASON_MASK		0x000000ff
		#define LFA_LINK_DOWN			    0x1
		#define LFA_LOOPBACK_ENABLED		0x2
		#define LFA_DUPLEX_MISMATCH		    0x3
		#define LFA_MFW_IS_TOO_OLD		    0x4
		#define LFA_LINK_SPEED_MISMATCH		0x5
		#define LFA_FLOW_CTRL_MISMATCH		0x6
		#define LFA_SPEED_CAP_MISMATCH		0x7
		#define LFA_DCC_LFA_DISABLED		0x8
		#define LFA_EEE_MISMATCH		0x9

	#define LINK_FLAP_AVOIDANCE_COUNT_OFFSET	8
	#define LINK_FLAP_AVOIDANCE_COUNT_MASK		0x0000ff00

	#define LINK_FLAP_COUNT_OFFSET			16
	#define LINK_FLAP_COUNT_MASK			0x00ff0000

	#define LFA_FLAGS_MASK				0xff000000
	#define SHMEM_LFA_DONT_CLEAR_STAT		(1<<24)

};

/*
Used to suppoert NSCI get OS driver version
On driver load the version value will be set
On driver unload driver value of 0x0 will be set
*/
struct os_drv_ver{
	#define DRV_VER_NOT_LOADED                      0
	/*personalites orrder is importent */
	#define DRV_PERS_ETHERNET                       0
	#define DRV_PERS_ISCSI                          1
	#define DRV_PERS_FCOE                           2
	/*shmem2 struct is constatnt can't add more personalites here*/
	#define MAX_DRV_PERS                            3
	uint32_t  versions[MAX_DRV_PERS];
};

#define OEM_I2C_UUID_STR_ADDR 0x9f
#define OEM_I2C_CARD_SKU_STR_ADDR 0x3c
#define OEM_I2C_CARD_FN_STR_ADDR 0x48
#define OEM_I2C_CARD_NAME_STR_ADDR 0x10e

#define OEM_I2C_UUID_STR_LEN 16
#define OEM_I2C_CARD_SKU_STR_LEN 12
#define OEM_I2C_CARD_FN_STR_LEN 12
#define OEM_I2C_CARD_NAME_STR_LEN 128
#define OEM_I2C_CARD_VERSION_STR_LEN 36

struct oem_i2c_data_t {
	uint32_t size;
	uint8_t uuid[OEM_I2C_UUID_STR_LEN];
	uint8_t card_sku[OEM_I2C_CARD_SKU_STR_LEN];
	uint8_t card_name[OEM_I2C_CARD_NAME_STR_LEN];
	uint8_t card_ver[OEM_I2C_CARD_VERSION_STR_LEN];
	uint8_t card_fn[OEM_I2C_CARD_FN_STR_LEN];
};

enum curr_cfg_method_e {
	CURR_CFG_MET_NONE = 0,  /* default config */
	CURR_CFG_MET_OS = 1,
	CURR_CFG_MET_VENDOR_SPEC = 2,/* e.g. Option ROM, NPAR, O/S Cfg Utils */
	CURR_CFG_MET_HP_OTHER = 3,
	CURR_CFG_MET_VC_CLP = 4,  /* C-Class SM-CLP */
	CURR_CFG_MET_HP_CNU = 5,  /*  Converged Network Utility */
	CURR_CFG_MET_HP_DCI = 6,  /* DCi (BD) changes */
};

#define FC_NPIV_WWPN_SIZE 8
#define FC_NPIV_WWNN_SIZE 8
struct bdn_npiv_settings {
	uint8_t npiv_wwpn[FC_NPIV_WWPN_SIZE];
	uint8_t npiv_wwnn[FC_NPIV_WWNN_SIZE];
};

struct bdn_fc_npiv_cfg {
	/* hdr used internally by the MFW */
	uint32_t hdr;
	uint32_t num_of_npiv;
};

#define MAX_NUMBER_NPIV 64
struct bdn_fc_npiv_tbl {
	struct bdn_fc_npiv_cfg fc_npiv_cfg;
	struct bdn_npiv_settings settings[MAX_NUMBER_NPIV];
};

struct mdump_driver_info {
	uint32_t epoc;
	uint32_t drv_ver;
	uint32_t fw_ver;

	uint32_t valid_dump;
	#define FIRST_DUMP_VALID        (1 << 0)
	#define SECOND_DUMP_VALID       (1 << 1)

	uint32_t flags;
	#define ENABLE_ALL_TRIGGERS     (0x7fffffff)
	#define TRIGGER_MDUMP_ONCE      (1 << 31)
};

struct shmem2_region {

	uint32_t size;					/* 0x0000 */

	uint32_t dcc_support;				/* 0x0004 */
	#define SHMEM_DCC_SUPPORT_NONE                      0x00000000
	#define SHMEM_DCC_SUPPORT_DISABLE_ENABLE_PF_TLV     0x00000001
	#define SHMEM_DCC_SUPPORT_BANDWIDTH_ALLOCATION_TLV  0x00000004
	#define SHMEM_DCC_SUPPORT_CHANGE_MAC_ADDRESS_TLV    0x00000008
	#define SHMEM_DCC_SUPPORT_SET_PROTOCOL_TLV          0x00000040
	#define SHMEM_DCC_SUPPORT_SET_PRIORITY_TLV          0x00000080

	uint32_t ext_phy_fw_version2[PORT_MAX];		/* 0x0008 */
	/*
	 * For backwards compatibility, if the mf_cfg_addr does not exist
	 * (the size filed is smaller than 0xc) the mf_cfg resides at the
	 * end of struct shmem_region
	 */
	uint32_t mf_cfg_addr;				/* 0x0010 */
	#define SHMEM_MF_CFG_ADDR_NONE                  0x00000000

	struct fw_flr_mb flr_mb;			/* 0x0014 */
	uint32_t dcbx_lldp_params_offset;			/* 0x0028 */
	#define SHMEM_LLDP_DCBX_PARAMS_NONE             0x00000000
	uint32_t dcbx_neg_res_offset;			/* 0x002c */
	#define SHMEM_DCBX_NEG_RES_NONE			0x00000000
	uint32_t dcbx_remote_mib_offset;			/* 0x0030 */
	#define SHMEM_DCBX_REMOTE_MIB_NONE              0x00000000
	/*
	 * The other shmemX_base_addr holds the other path's shmem address
	 * required for example in case of common phy init, or for path1 to know
	 * the address of mcp debug trace which is located in offset from shmem
	 * of path0
	 */
	uint32_t other_shmem_base_addr;			/* 0x0034 */
	uint32_t other_shmem2_base_addr;			/* 0x0038 */
	/*
	 * mcp_vf_disabled is set by the MCP to indicate the driver about VFs
	 * which were disabled/flred
	 */
	uint32_t mcp_vf_disabled[E2_VF_MAX / 32];		/* 0x003c */

	/*
	 * drv_ack_vf_disabled is set by the PF driver to ack handled disabled
	 * VFs
	 */
	uint32_t drv_ack_vf_disabled[E2_FUNC_MAX][E2_VF_MAX / 32]; /* 0x0044 */

	uint32_t dcbx_lldp_dcbx_stat_offset;			/* 0x0064 */
	#define SHMEM_LLDP_DCBX_STAT_NONE               0x00000000

	/*
	 * edebug_driver_if field is used to transfer messages between edebug
	 * app to the driver through shmem2.
	 *
	 * message format:
	 * bits 0-2 -  function number / instance of driver to perform request
	 * bits 3-5 -  op code / is_ack?
	 * bits 6-63 - data
	 */
	uint32_t edebug_driver_if[2];			/* 0x0068 */
	#define EDEBUG_DRIVER_IF_OP_CODE_GET_PHYS_ADDR  1
	#define EDEBUG_DRIVER_IF_OP_CODE_GET_BUS_ADDR   2
	#define EDEBUG_DRIVER_IF_OP_CODE_DISABLE_STAT   3

	uint32_t nvm_retain_bitmap_addr;			/* 0x0070 */

	/* afex support of that driver */
	uint32_t afex_driver_support;			/* 0x0074 */
	#define SHMEM_AFEX_VERSION_MASK                  0x100f
	#define SHMEM_AFEX_SUPPORTED_VERSION_ONE         0x1001
	#define SHMEM_AFEX_REDUCED_DRV_LOADED            0x8000

	/* driver receives addr in scratchpad to which it should respond */
	uint32_t afex_scratchpad_addr_to_write[E2_FUNC_MAX];

	/*
	 * generic params from MCP to driver (value depends on the msg sent
	 * to driver
	 */
	uint32_t afex_param1_to_driver[E2_FUNC_MAX];		/* 0x0088 */
	uint32_t afex_param2_to_driver[E2_FUNC_MAX];		/* 0x0098 */

	uint32_t swim_base_addr;				/* 0x00a8 */
	uint32_t swim_funcs;					/* 0x00ac */
	uint32_t swim_main_cb;				/* 0x00b0 */

	/*
	 * bitmap notifying which VIF profiles stored in nvram are enabled by
	 * switch
	 */
	uint32_t afex_profiles_enabled[2];			/* 0x00b4 */

	/* generic flags controlled by the driver */
	uint32_t drv_flags;					/* 0x00bc */
	#define DRV_FLAGS_DCB_CONFIGURED		0x0
	#define DRV_FLAGS_DCB_CONFIGURATION_ABORTED	0x1
	#define DRV_FLAGS_DCB_MFW_CONFIGURED	0x2

    #define DRV_FLAGS_PORT_MASK	((1 << DRV_FLAGS_DCB_CONFIGURED) | \
			(1 << DRV_FLAGS_DCB_CONFIGURATION_ABORTED) | \
			(1 << DRV_FLAGS_DCB_MFW_CONFIGURED))
	/* Port offset*/
	#define DRV_FLAGS_P0_OFFSET		0
	#define DRV_FLAGS_P1_OFFSET		16
	#define DRV_FLAGS_GET_PORT_OFFSET(_port)	((0 == _port) ? \
						DRV_FLAGS_P0_OFFSET : \
						DRV_FLAGS_P1_OFFSET)

	#define DRV_FLAGS_GET_PORT_MASK(_port)	(DRV_FLAGS_PORT_MASK << \
	DRV_FLAGS_GET_PORT_OFFSET(_port))

	#define DRV_FLAGS_FILED_BY_PORT(_field_bit, _port)	(1 << ( \
	(_field_bit) + DRV_FLAGS_GET_PORT_OFFSET(_port)))

	/* pointer to extended dev_info shared data copied from nvm image */
	uint32_t extended_dev_info_shared_addr;		/* 0x00c0 */
	uint32_t ncsi_oem_data_addr;				/* 0x00c4 */

	uint32_t sensor_data_addr;				/* 0x00c8 */
	uint32_t buffer_block_addr;				/* 0x00cc */
	uint32_t sensor_data_req_update_interval;		/* 0x00d0 */
	uint32_t temperature_in_half_celsius;		/* 0x00d4 */
	uint32_t glob_struct_in_host;			/* 0x00d8 */

	uint32_t dcbx_neg_res_ext_offset;			/* 0x00dc */
	#define SHMEM_DCBX_NEG_RES_EXT_NONE			0x00000000

	uint32_t drv_capabilities_flag[E2_FUNC_MAX];		/* 0x00e0 */
	#define DRV_FLAGS_CAPABILITIES_LOADED_SUPPORTED 0x00000001
	#define DRV_FLAGS_CAPABILITIES_LOADED_L2        0x00000002
	#define DRV_FLAGS_CAPABILITIES_LOADED_FCOE      0x00000004
	#define DRV_FLAGS_CAPABILITIES_LOADED_ISCSI     0x00000008
	#define DRV_FLAGS_MTU_MASK			0xffff0000
	#define DRV_FLAGS_MTU_SHIFT				16

	uint32_t extended_dev_info_shared_cfg_size;		/* 0x00f0 */

	uint32_t dcbx_en[PORT_MAX];				/* 0x00f4 */

	/* The offset points to the multi threaded meta structure */
	uint32_t multi_thread_data_offset;			/* 0x00fc */

	/* address of DMAable host address holding values from the drivers */
	uint32_t drv_info_host_addr_lo;			/* 0x0100 */
	uint32_t drv_info_host_addr_hi;			/* 0x0104 */

	/* general values written by the MFW (such as current version) */
	uint32_t drv_info_control;				/* 0x0108 */
	#define DRV_INFO_CONTROL_VER_MASK          0x000000ff
	#define DRV_INFO_CONTROL_VER_SHIFT         0
	#define DRV_INFO_CONTROL_OP_CODE_MASK      0x0000ff00
	#define DRV_INFO_CONTROL_OP_CODE_SHIFT     8
	uint32_t ibft_host_addr; /* initialized by option ROM */     /* 0x010c */

	struct eee_remote_vals eee_remote_vals[PORT_MAX];	/* 0x0110 */
	uint32_t pf_allocation[E2_FUNC_MAX];				/* 0x0120 */
	#define PF_ALLOACTION_MSIX_VECTORS_MASK    0x000000ff /* real value, as PCI config space can show only maximum of 64 vectors */
	#define PF_ALLOACTION_MSIX_VECTORS_SHIFT   0

	/* the status of EEE auto-negotiation
	 * bits 15:0 the configured tx-lpi entry timer value. Depends on bit 31.
	 * bits 19:16 the supported modes for EEE.
	 * bits 23:20 the speeds advertised for EEE.
	 * bits 27:24 the speeds the Link partner advertised for EEE.
	 * The supported/adv. modes in bits 27:19 originate from the
	 * SHMEM_EEE_XXX_ADV definitions (where XXX is replaced by speed).
	 * bit 28 when 1'b1 EEE was requested.
	 * bit 29 when 1'b1 tx lpi was requested.
	 * bit 30 when 1'b1 EEE was negotiated. Tx lpi will be asserted iff
	 * 30:29 are 2'b11.
	 * bit 31 when 1'b0 bits 15:0 contain a PORT_FEAT_CFG_EEE_ define as
	 * value. When 1'b1 those bits contains a value times 16 microseconds.
	 */
	uint32_t eee_status[PORT_MAX];				/* 0x0130 */
	#define SHMEM_EEE_TIMER_MASK		   0x0000ffff
	#define SHMEM_EEE_SUPPORTED_MASK	   0x000f0000
	#define SHMEM_EEE_SUPPORTED_SHIFT	   16
	#define SHMEM_EEE_ADV_STATUS_MASK	   0x00f00000
		#define SHMEM_EEE_100M_ADV	   (1<<0)
		#define SHMEM_EEE_1G_ADV	   (1<<1)
		#define SHMEM_EEE_10G_ADV	   (1<<2)
	#define SHMEM_EEE_ADV_STATUS_SHIFT	   20
	#define	SHMEM_EEE_LP_ADV_STATUS_MASK	   0x0f000000
	#define SHMEM_EEE_LP_ADV_STATUS_SHIFT	   24
	#define SHMEM_EEE_REQUESTED_BIT		   0x10000000
	#define SHMEM_EEE_LPI_REQUESTED_BIT	   0x20000000
	#define SHMEM_EEE_ACTIVE_BIT		   0x40000000
	#define SHMEM_EEE_TIME_OUTPUT_BIT	   0x80000000

	uint32_t sizeof_port_stats;					/* 0x0138 */

	/* Link Flap Avoidance */
	uint32_t lfa_host_addr[PORT_MAX];				/* 0x013c */

    /* External PHY temperature in deg C. */
	uint32_t extphy_temps_in_celsius;				/* 0x0144 */
	#define EXTPHY1_TEMP_MASK                  0x0000ffff
	#define EXTPHY1_TEMP_SHIFT                 0
	#define ON_BOARD_TEMP_MASK                 0xffff0000
	#define ON_BOARD_TEMP_SHIFT                16

	uint32_t ocdata_info_addr;			/* Offset 0x148 */
	uint32_t drv_func_info_addr;			/* Offset 0x14C */
	uint32_t drv_func_info_size;			/* Offset 0x150 */
	uint32_t link_attr_sync[PORT_MAX];		/* Offset 0x154 */
	#define LINK_ATTR_SYNC_KR2_ENABLE	0x00000001
	#define LINK_ATTR_84858			0x00000002
	#define LINK_SFP_EEPROM_COMP_CODE_MASK	0x0000ff00
	#define LINK_SFP_EEPROM_COMP_CODE_SHIFT		 8
	#define LINK_SFP_EEPROM_COMP_CODE_SR	0x00001000
	#define LINK_SFP_EEPROM_COMP_CODE_LR	0x00002000
	#define LINK_SFP_EEPROM_COMP_CODE_LRM	0x00004000

	uint32_t ibft_host_addr_hi;  /* Initialize by uEFI ROM Offset 0x158 */
	uint32_t fcode_ver;                          /* Offset 0x15c */
	uint32_t link_change_count[PORT_MAX];        /* Offset 0x160-0x164 */
	#define LINK_CHANGE_COUNT_MASK 0xff     /* Offset 0x168 */
        /* driver version for each personality*/
        struct os_drv_ver func_os_drv_ver[E2_FUNC_MAX]; /* Offset 0x16c */

	/* Flag to the driver that PF's drv_info_host_addr buffer was read  */
	uint32_t mfw_drv_indication;				/* Offset 0x19c */

	/* We use inidcation for each PF (0..3) */
	#define MFW_DRV_IND_READ_DONE_OFFSET(_pf_)  (1 << _pf_)

	union { /* For various OEMs */			/* Offset 0x1a0 */
		uint8_t storage_boot_prog[E2_FUNC_MAX];
	#define STORAGE_BOOT_PROG_MASK				0x000000FF
	#define STORAGE_BOOT_PROG_NONE				0x00000000
	#define STORAGE_BOOT_PROG_ISCSI_IP_ACQUIRED		0x00000002
	#define STORAGE_BOOT_PROG_FCOE_FABRIC_LOGIN_SUCCESS	0x00000002
	#define STORAGE_BOOT_PROG_TARGET_FOUND			0x00000004
	#define STORAGE_BOOT_PROG_ISCSI_CHAP_SUCCESS		0x00000008
	#define STORAGE_BOOT_PROG_FCOE_LUN_FOUND		0x00000008
	#define STORAGE_BOOT_PROG_LOGGED_INTO_TGT		0x00000010
	#define STORAGE_BOOT_PROG_IMG_DOWNLOADED		0x00000020
	#define STORAGE_BOOT_PROG_OS_HANDOFF			0x00000040
	#define STORAGE_BOOT_PROG_COMPLETED			0x00000080

		uint32_t oem_i2c_data_addr;
	}u;

	/* 9 entires for the C2S PCP map for each inner VLAN PCP + 1 default */
	/* For PCP values 0-3 use the map lower */
	/* 0xFF000000 - PCP 0, 0x00FF0000 - PCP 1,
	 * 0x0000FF00 - PCP 2, 0x000000FF PCP 3
	 */
	uint32_t c2s_pcp_map_lower[E2_FUNC_MAX];			/* 0x1a4 */

	/* For PCP values 4-7 use the map upper */
	/* 0xFF000000 - PCP 4, 0x00FF0000 - PCP 5,
	 * 0x0000FF00 - PCP 6, 0x000000FF PCP 7
	 */
	uint32_t c2s_pcp_map_upper[E2_FUNC_MAX];			/* 0x1b4 */

	/* For PCP default value get the MSB byte of the map default */
	uint32_t c2s_pcp_map_default[E2_FUNC_MAX];			/* 0x1c4 */

	/* FC_NPIV table offset in NVRAM */
	uint32_t fc_npiv_nvram_tbl_addr[PORT_MAX];			/* 0x1d4 */

	/* Shows last method that changed configuration of this device */
	enum curr_cfg_method_e curr_cfg;			/* 0x1dc */

	/* Storm FW version, shold be kept in the format 0xMMmmbbdd:
	 * MM - Major, mm - Minor, bb - Build ,dd - Drop
	 */
	uint32_t netproc_fw_ver;					/* 0x1e0 */

	/* Option ROM SMASH CLP version */
	uint32_t clp_ver;						/* 0x1e4 */

	uint32_t pcie_bus_num;					/* 0x1e8 */

	uint32_t sriov_switch_mode;					/* 0x1ec */
	#define SRIOV_SWITCH_MODE_NONE		0x0
	#define SRIOV_SWITCH_MODE_VEB		0x1
	#define SRIOV_SWITCH_MODE_VEPA		0x2

	uint8_t  rsrv2[E2_FUNC_MAX];					/* 0x1f0 */

	uint32_t img_inv_table_addr;	/* Address to INV_TABLE_P */	/* 0x1f4 */

	uint32_t mtu_size[E2_FUNC_MAX];				/* 0x1f8 */

	uint32_t os_driver_state[E2_FUNC_MAX];			/* 0x208 */
	#define OS_DRIVER_STATE_NOT_LOADED	0 /* not installed */
	#define OS_DRIVER_STATE_LOADING		1 /* transition state */
	#define OS_DRIVER_STATE_DISABLED	2 /* installed but disabled */
	#define OS_DRIVER_STATE_ACTIVE		3 /* installed and active */

	/* mini dump driver info */
	struct mdump_driver_info drv_info;			/* 0x218 */

								/* 0x22c */
};


struct emac_stats {
	uint32_t     rx_stat_ifhcinoctets;
	uint32_t     rx_stat_ifhcinbadoctets;
	uint32_t     rx_stat_etherstatsfragments;
	uint32_t     rx_stat_ifhcinucastpkts;
	uint32_t     rx_stat_ifhcinmulticastpkts;
	uint32_t     rx_stat_ifhcinbroadcastpkts;
	uint32_t     rx_stat_dot3statsfcserrors;
	uint32_t     rx_stat_dot3statsalignmenterrors;
	uint32_t     rx_stat_dot3statscarriersenseerrors;
	uint32_t     rx_stat_xonpauseframesreceived;
	uint32_t     rx_stat_xoffpauseframesreceived;
	uint32_t     rx_stat_maccontrolframesreceived;
	uint32_t     rx_stat_xoffstateentered;
	uint32_t     rx_stat_dot3statsframestoolong;
	uint32_t     rx_stat_etherstatsjabbers;
	uint32_t     rx_stat_etherstatsundersizepkts;
	uint32_t     rx_stat_etherstatspkts64octets;
	uint32_t     rx_stat_etherstatspkts65octetsto127octets;
	uint32_t     rx_stat_etherstatspkts128octetsto255octets;
	uint32_t     rx_stat_etherstatspkts256octetsto511octets;
	uint32_t     rx_stat_etherstatspkts512octetsto1023octets;
	uint32_t     rx_stat_etherstatspkts1024octetsto1522octets;
	uint32_t     rx_stat_etherstatspktsover1522octets;

	uint32_t     rx_stat_falsecarriererrors;

	uint32_t     tx_stat_ifhcoutoctets;
	uint32_t     tx_stat_ifhcoutbadoctets;
	uint32_t     tx_stat_etherstatscollisions;
	uint32_t     tx_stat_outxonsent;
	uint32_t     tx_stat_outxoffsent;
	uint32_t     tx_stat_flowcontroldone;
	uint32_t     tx_stat_dot3statssinglecollisionframes;
	uint32_t     tx_stat_dot3statsmultiplecollisionframes;
	uint32_t     tx_stat_dot3statsdeferredtransmissions;
	uint32_t     tx_stat_dot3statsexcessivecollisions;
	uint32_t     tx_stat_dot3statslatecollisions;
	uint32_t     tx_stat_ifhcoutucastpkts;
	uint32_t     tx_stat_ifhcoutmulticastpkts;
	uint32_t     tx_stat_ifhcoutbroadcastpkts;
	uint32_t     tx_stat_etherstatspkts64octets;
	uint32_t     tx_stat_etherstatspkts65octetsto127octets;
	uint32_t     tx_stat_etherstatspkts128octetsto255octets;
	uint32_t     tx_stat_etherstatspkts256octetsto511octets;
	uint32_t     tx_stat_etherstatspkts512octetsto1023octets;
	uint32_t     tx_stat_etherstatspkts1024octetsto1522octets;
	uint32_t     tx_stat_etherstatspktsover1522octets;
	uint32_t     tx_stat_dot3statsinternalmactransmiterrors;
};


struct bmac1_stats {
	uint32_t	tx_stat_gtpkt_lo;
	uint32_t	tx_stat_gtpkt_hi;
	uint32_t	tx_stat_gtxpf_lo;
	uint32_t	tx_stat_gtxpf_hi;
	uint32_t	tx_stat_gtfcs_lo;
	uint32_t	tx_stat_gtfcs_hi;
	uint32_t	tx_stat_gtmca_lo;
	uint32_t	tx_stat_gtmca_hi;
	uint32_t	tx_stat_gtbca_lo;
	uint32_t	tx_stat_gtbca_hi;
	uint32_t	tx_stat_gtfrg_lo;
	uint32_t	tx_stat_gtfrg_hi;
	uint32_t	tx_stat_gtovr_lo;
	uint32_t	tx_stat_gtovr_hi;
	uint32_t	tx_stat_gt64_lo;
	uint32_t	tx_stat_gt64_hi;
	uint32_t	tx_stat_gt127_lo;
	uint32_t	tx_stat_gt127_hi;
	uint32_t	tx_stat_gt255_lo;
	uint32_t	tx_stat_gt255_hi;
	uint32_t	tx_stat_gt511_lo;
	uint32_t	tx_stat_gt511_hi;
	uint32_t	tx_stat_gt1023_lo;
	uint32_t	tx_stat_gt1023_hi;
	uint32_t	tx_stat_gt1518_lo;
	uint32_t	tx_stat_gt1518_hi;
	uint32_t	tx_stat_gt2047_lo;
	uint32_t	tx_stat_gt2047_hi;
	uint32_t	tx_stat_gt4095_lo;
	uint32_t	tx_stat_gt4095_hi;
	uint32_t	tx_stat_gt9216_lo;
	uint32_t	tx_stat_gt9216_hi;
	uint32_t	tx_stat_gt16383_lo;
	uint32_t	tx_stat_gt16383_hi;
	uint32_t	tx_stat_gtmax_lo;
	uint32_t	tx_stat_gtmax_hi;
	uint32_t	tx_stat_gtufl_lo;
	uint32_t	tx_stat_gtufl_hi;
	uint32_t	tx_stat_gterr_lo;
	uint32_t	tx_stat_gterr_hi;
	uint32_t	tx_stat_gtbyt_lo;
	uint32_t	tx_stat_gtbyt_hi;

	uint32_t	rx_stat_gr64_lo;
	uint32_t	rx_stat_gr64_hi;
	uint32_t	rx_stat_gr127_lo;
	uint32_t	rx_stat_gr127_hi;
	uint32_t	rx_stat_gr255_lo;
	uint32_t	rx_stat_gr255_hi;
	uint32_t	rx_stat_gr511_lo;
	uint32_t	rx_stat_gr511_hi;
	uint32_t	rx_stat_gr1023_lo;
	uint32_t	rx_stat_gr1023_hi;
	uint32_t	rx_stat_gr1518_lo;
	uint32_t	rx_stat_gr1518_hi;
	uint32_t	rx_stat_gr2047_lo;
	uint32_t	rx_stat_gr2047_hi;
	uint32_t	rx_stat_gr4095_lo;
	uint32_t	rx_stat_gr4095_hi;
	uint32_t	rx_stat_gr9216_lo;
	uint32_t	rx_stat_gr9216_hi;
	uint32_t	rx_stat_gr16383_lo;
	uint32_t	rx_stat_gr16383_hi;
	uint32_t	rx_stat_grmax_lo;
	uint32_t	rx_stat_grmax_hi;
	uint32_t	rx_stat_grpkt_lo;
	uint32_t	rx_stat_grpkt_hi;
	uint32_t	rx_stat_grfcs_lo;
	uint32_t	rx_stat_grfcs_hi;
	uint32_t	rx_stat_grmca_lo;
	uint32_t	rx_stat_grmca_hi;
	uint32_t	rx_stat_grbca_lo;
	uint32_t	rx_stat_grbca_hi;
	uint32_t	rx_stat_grxcf_lo;
	uint32_t	rx_stat_grxcf_hi;
	uint32_t	rx_stat_grxpf_lo;
	uint32_t	rx_stat_grxpf_hi;
	uint32_t	rx_stat_grxuo_lo;
	uint32_t	rx_stat_grxuo_hi;
	uint32_t	rx_stat_grjbr_lo;
	uint32_t	rx_stat_grjbr_hi;
	uint32_t	rx_stat_grovr_lo;
	uint32_t	rx_stat_grovr_hi;
	uint32_t	rx_stat_grflr_lo;
	uint32_t	rx_stat_grflr_hi;
	uint32_t	rx_stat_grmeg_lo;
	uint32_t	rx_stat_grmeg_hi;
	uint32_t	rx_stat_grmeb_lo;
	uint32_t	rx_stat_grmeb_hi;
	uint32_t	rx_stat_grbyt_lo;
	uint32_t	rx_stat_grbyt_hi;
	uint32_t	rx_stat_grund_lo;
	uint32_t	rx_stat_grund_hi;
	uint32_t	rx_stat_grfrg_lo;
	uint32_t	rx_stat_grfrg_hi;
	uint32_t	rx_stat_grerb_lo;
	uint32_t	rx_stat_grerb_hi;
	uint32_t	rx_stat_grfre_lo;
	uint32_t	rx_stat_grfre_hi;
	uint32_t	rx_stat_gripj_lo;
	uint32_t	rx_stat_gripj_hi;
};

struct bmac2_stats {
	uint32_t	tx_stat_gtpk_lo; /* gtpok */
	uint32_t	tx_stat_gtpk_hi; /* gtpok */
	uint32_t	tx_stat_gtxpf_lo; /* gtpf */
	uint32_t	tx_stat_gtxpf_hi; /* gtpf */
	uint32_t	tx_stat_gtpp_lo; /* NEW BMAC2 */
	uint32_t	tx_stat_gtpp_hi; /* NEW BMAC2 */
	uint32_t	tx_stat_gtfcs_lo;
	uint32_t	tx_stat_gtfcs_hi;
	uint32_t	tx_stat_gtuca_lo; /* NEW BMAC2 */
	uint32_t	tx_stat_gtuca_hi; /* NEW BMAC2 */
	uint32_t	tx_stat_gtmca_lo;
	uint32_t	tx_stat_gtmca_hi;
	uint32_t	tx_stat_gtbca_lo;
	uint32_t	tx_stat_gtbca_hi;
	uint32_t	tx_stat_gtovr_lo;
	uint32_t	tx_stat_gtovr_hi;
	uint32_t	tx_stat_gtfrg_lo;
	uint32_t	tx_stat_gtfrg_hi;
	uint32_t	tx_stat_gtpkt1_lo; /* gtpkt */
	uint32_t	tx_stat_gtpkt1_hi; /* gtpkt */
	uint32_t	tx_stat_gt64_lo;
	uint32_t	tx_stat_gt64_hi;
	uint32_t	tx_stat_gt127_lo;
	uint32_t	tx_stat_gt127_hi;
	uint32_t	tx_stat_gt255_lo;
	uint32_t	tx_stat_gt255_hi;
	uint32_t	tx_stat_gt511_lo;
	uint32_t	tx_stat_gt511_hi;
	uint32_t	tx_stat_gt1023_lo;
	uint32_t	tx_stat_gt1023_hi;
	uint32_t	tx_stat_gt1518_lo;
	uint32_t	tx_stat_gt1518_hi;
	uint32_t	tx_stat_gt2047_lo;
	uint32_t	tx_stat_gt2047_hi;
	uint32_t	tx_stat_gt4095_lo;
	uint32_t	tx_stat_gt4095_hi;
	uint32_t	tx_stat_gt9216_lo;
	uint32_t	tx_stat_gt9216_hi;
	uint32_t	tx_stat_gt16383_lo;
	uint32_t	tx_stat_gt16383_hi;
	uint32_t	tx_stat_gtmax_lo;
	uint32_t	tx_stat_gtmax_hi;
	uint32_t	tx_stat_gtufl_lo;
	uint32_t	tx_stat_gtufl_hi;
	uint32_t	tx_stat_gterr_lo;
	uint32_t	tx_stat_gterr_hi;
	uint32_t	tx_stat_gtbyt_lo;
	uint32_t	tx_stat_gtbyt_hi;

	uint32_t	rx_stat_gr64_lo;
	uint32_t	rx_stat_gr64_hi;
	uint32_t	rx_stat_gr127_lo;
	uint32_t	rx_stat_gr127_hi;
	uint32_t	rx_stat_gr255_lo;
	uint32_t	rx_stat_gr255_hi;
	uint32_t	rx_stat_gr511_lo;
	uint32_t	rx_stat_gr511_hi;
	uint32_t	rx_stat_gr1023_lo;
	uint32_t	rx_stat_gr1023_hi;
	uint32_t	rx_stat_gr1518_lo;
	uint32_t	rx_stat_gr1518_hi;
	uint32_t	rx_stat_gr2047_lo;
	uint32_t	rx_stat_gr2047_hi;
	uint32_t	rx_stat_gr4095_lo;
	uint32_t	rx_stat_gr4095_hi;
	uint32_t	rx_stat_gr9216_lo;
	uint32_t	rx_stat_gr9216_hi;
	uint32_t	rx_stat_gr16383_lo;
	uint32_t	rx_stat_gr16383_hi;
	uint32_t	rx_stat_grmax_lo;
	uint32_t	rx_stat_grmax_hi;
	uint32_t	rx_stat_grpkt_lo;
	uint32_t	rx_stat_grpkt_hi;
	uint32_t	rx_stat_grfcs_lo;
	uint32_t	rx_stat_grfcs_hi;
	uint32_t	rx_stat_gruca_lo;
	uint32_t	rx_stat_gruca_hi;
	uint32_t	rx_stat_grmca_lo;
	uint32_t	rx_stat_grmca_hi;
	uint32_t	rx_stat_grbca_lo;
	uint32_t	rx_stat_grbca_hi;
	uint32_t	rx_stat_grxpf_lo; /* grpf */
	uint32_t	rx_stat_grxpf_hi; /* grpf */
	uint32_t	rx_stat_grpp_lo;
	uint32_t	rx_stat_grpp_hi;
	uint32_t	rx_stat_grxuo_lo; /* gruo */
	uint32_t	rx_stat_grxuo_hi; /* gruo */
	uint32_t	rx_stat_grjbr_lo;
	uint32_t	rx_stat_grjbr_hi;
	uint32_t	rx_stat_grovr_lo;
	uint32_t	rx_stat_grovr_hi;
	uint32_t	rx_stat_grxcf_lo; /* grcf */
	uint32_t	rx_stat_grxcf_hi; /* grcf */
	uint32_t	rx_stat_grflr_lo;
	uint32_t	rx_stat_grflr_hi;
	uint32_t	rx_stat_grpok_lo;
	uint32_t	rx_stat_grpok_hi;
	uint32_t	rx_stat_grmeg_lo;
	uint32_t	rx_stat_grmeg_hi;
	uint32_t	rx_stat_grmeb_lo;
	uint32_t	rx_stat_grmeb_hi;
	uint32_t	rx_stat_grbyt_lo;
	uint32_t	rx_stat_grbyt_hi;
	uint32_t	rx_stat_grund_lo;
	uint32_t	rx_stat_grund_hi;
	uint32_t	rx_stat_grfrg_lo;
	uint32_t	rx_stat_grfrg_hi;
	uint32_t	rx_stat_grerb_lo; /* grerrbyt */
	uint32_t	rx_stat_grerb_hi; /* grerrbyt */
	uint32_t	rx_stat_grfre_lo; /* grfrerr */
	uint32_t	rx_stat_grfre_hi; /* grfrerr */
	uint32_t	rx_stat_gripj_lo;
	uint32_t	rx_stat_gripj_hi;
};

struct mstat_stats {
	struct {
		/* OTE MSTAT on E3 has a bug where this register's contents are
		 * actually tx_gtxpok + tx_gtxpf + (possibly)tx_gtxpp
		 */
		uint32_t tx_gtxpok_lo;
		uint32_t tx_gtxpok_hi;
		uint32_t tx_gtxpf_lo;
		uint32_t tx_gtxpf_hi;
		uint32_t tx_gtxpp_lo;
		uint32_t tx_gtxpp_hi;
		uint32_t tx_gtfcs_lo;
		uint32_t tx_gtfcs_hi;
		uint32_t tx_gtuca_lo;
		uint32_t tx_gtuca_hi;
		uint32_t tx_gtmca_lo;
		uint32_t tx_gtmca_hi;
		uint32_t tx_gtgca_lo;
		uint32_t tx_gtgca_hi;
		uint32_t tx_gtpkt_lo;
		uint32_t tx_gtpkt_hi;
		uint32_t tx_gt64_lo;
		uint32_t tx_gt64_hi;
		uint32_t tx_gt127_lo;
		uint32_t tx_gt127_hi;
		uint32_t tx_gt255_lo;
		uint32_t tx_gt255_hi;
		uint32_t tx_gt511_lo;
		uint32_t tx_gt511_hi;
		uint32_t tx_gt1023_lo;
		uint32_t tx_gt1023_hi;
		uint32_t tx_gt1518_lo;
		uint32_t tx_gt1518_hi;
		uint32_t tx_gt2047_lo;
		uint32_t tx_gt2047_hi;
		uint32_t tx_gt4095_lo;
		uint32_t tx_gt4095_hi;
		uint32_t tx_gt9216_lo;
		uint32_t tx_gt9216_hi;
		uint32_t tx_gt16383_lo;
		uint32_t tx_gt16383_hi;
		uint32_t tx_gtufl_lo;
		uint32_t tx_gtufl_hi;
		uint32_t tx_gterr_lo;
		uint32_t tx_gterr_hi;
		uint32_t tx_gtbyt_lo;
		uint32_t tx_gtbyt_hi;
		uint32_t tx_collisions_lo;
		uint32_t tx_collisions_hi;
		uint32_t tx_singlecollision_lo;
		uint32_t tx_singlecollision_hi;
		uint32_t tx_multiplecollisions_lo;
		uint32_t tx_multiplecollisions_hi;
		uint32_t tx_deferred_lo;
		uint32_t tx_deferred_hi;
		uint32_t tx_excessivecollisions_lo;
		uint32_t tx_excessivecollisions_hi;
		uint32_t tx_latecollisions_lo;
		uint32_t tx_latecollisions_hi;
	} stats_tx;

	struct {
		uint32_t rx_gr64_lo;
		uint32_t rx_gr64_hi;
		uint32_t rx_gr127_lo;
		uint32_t rx_gr127_hi;
		uint32_t rx_gr255_lo;
		uint32_t rx_gr255_hi;
		uint32_t rx_gr511_lo;
		uint32_t rx_gr511_hi;
		uint32_t rx_gr1023_lo;
		uint32_t rx_gr1023_hi;
		uint32_t rx_gr1518_lo;
		uint32_t rx_gr1518_hi;
		uint32_t rx_gr2047_lo;
		uint32_t rx_gr2047_hi;
		uint32_t rx_gr4095_lo;
		uint32_t rx_gr4095_hi;
		uint32_t rx_gr9216_lo;
		uint32_t rx_gr9216_hi;
		uint32_t rx_gr16383_lo;
		uint32_t rx_gr16383_hi;
		uint32_t rx_grpkt_lo;
		uint32_t rx_grpkt_hi;
		uint32_t rx_grfcs_lo;
		uint32_t rx_grfcs_hi;
		uint32_t rx_gruca_lo;
		uint32_t rx_gruca_hi;
		uint32_t rx_grmca_lo;
		uint32_t rx_grmca_hi;
		uint32_t rx_grbca_lo;
		uint32_t rx_grbca_hi;
		uint32_t rx_grxpf_lo;
		uint32_t rx_grxpf_hi;
		uint32_t rx_grxpp_lo;
		uint32_t rx_grxpp_hi;
		uint32_t rx_grxuo_lo;
		uint32_t rx_grxuo_hi;
		uint32_t rx_grovr_lo;
		uint32_t rx_grovr_hi;
		uint32_t rx_grxcf_lo;
		uint32_t rx_grxcf_hi;
		uint32_t rx_grflr_lo;
		uint32_t rx_grflr_hi;
		uint32_t rx_grpok_lo;
		uint32_t rx_grpok_hi;
		uint32_t rx_grbyt_lo;
		uint32_t rx_grbyt_hi;
		uint32_t rx_grund_lo;
		uint32_t rx_grund_hi;
		uint32_t rx_grfrg_lo;
		uint32_t rx_grfrg_hi;
		uint32_t rx_grerb_lo;
		uint32_t rx_grerb_hi;
		uint32_t rx_grfre_lo;
		uint32_t rx_grfre_hi;

		uint32_t rx_alignmenterrors_lo;
		uint32_t rx_alignmenterrors_hi;
		uint32_t rx_falsecarrier_lo;
		uint32_t rx_falsecarrier_hi;
		uint32_t rx_llfcmsgcnt_lo;
		uint32_t rx_llfcmsgcnt_hi;
	} stats_rx;
};

union mac_stats {
	struct emac_stats	emac_stats;
	struct bmac1_stats	bmac1_stats;
	struct bmac2_stats	bmac2_stats;
	struct mstat_stats	mstat_stats;
};


struct mac_stx {
	/* in_bad_octets */
	uint32_t     rx_stat_ifhcinbadoctets_hi;
	uint32_t     rx_stat_ifhcinbadoctets_lo;

	/* out_bad_octets */
	uint32_t     tx_stat_ifhcoutbadoctets_hi;
	uint32_t     tx_stat_ifhcoutbadoctets_lo;

	/* crc_receive_errors */
	uint32_t     rx_stat_dot3statsfcserrors_hi;
	uint32_t     rx_stat_dot3statsfcserrors_lo;
	/* alignment_errors */
	uint32_t     rx_stat_dot3statsalignmenterrors_hi;
	uint32_t     rx_stat_dot3statsalignmenterrors_lo;
	/* carrier_sense_errors */
	uint32_t     rx_stat_dot3statscarriersenseerrors_hi;
	uint32_t     rx_stat_dot3statscarriersenseerrors_lo;
	/* false_carrier_detections */
	uint32_t     rx_stat_falsecarriererrors_hi;
	uint32_t     rx_stat_falsecarriererrors_lo;

	/* runt_packets_received */
	uint32_t     rx_stat_etherstatsundersizepkts_hi;
	uint32_t     rx_stat_etherstatsundersizepkts_lo;
	/* jabber_packets_received */
	uint32_t     rx_stat_dot3statsframestoolong_hi;
	uint32_t     rx_stat_dot3statsframestoolong_lo;

	/* error_runt_packets_received */
	uint32_t     rx_stat_etherstatsfragments_hi;
	uint32_t     rx_stat_etherstatsfragments_lo;
	/* error_jabber_packets_received */
	uint32_t     rx_stat_etherstatsjabbers_hi;
	uint32_t     rx_stat_etherstatsjabbers_lo;

	/* control_frames_received */
	uint32_t     rx_stat_maccontrolframesreceived_hi;
	uint32_t     rx_stat_maccontrolframesreceived_lo;
	uint32_t     rx_stat_mac_xpf_hi;
	uint32_t     rx_stat_mac_xpf_lo;
	uint32_t     rx_stat_mac_xcf_hi;
	uint32_t     rx_stat_mac_xcf_lo;

	/* xoff_state_entered */
	uint32_t     rx_stat_xoffstateentered_hi;
	uint32_t     rx_stat_xoffstateentered_lo;
	/* pause_xon_frames_received */
	uint32_t     rx_stat_xonpauseframesreceived_hi;
	uint32_t     rx_stat_xonpauseframesreceived_lo;
	/* pause_xoff_frames_received */
	uint32_t     rx_stat_xoffpauseframesreceived_hi;
	uint32_t     rx_stat_xoffpauseframesreceived_lo;
	/* pause_xon_frames_transmitted */
	uint32_t     tx_stat_outxonsent_hi;
	uint32_t     tx_stat_outxonsent_lo;
	/* pause_xoff_frames_transmitted */
	uint32_t     tx_stat_outxoffsent_hi;
	uint32_t     tx_stat_outxoffsent_lo;
	/* flow_control_done */
	uint32_t     tx_stat_flowcontroldone_hi;
	uint32_t     tx_stat_flowcontroldone_lo;

	/* ether_stats_collisions */
	uint32_t     tx_stat_etherstatscollisions_hi;
	uint32_t     tx_stat_etherstatscollisions_lo;
	/* single_collision_transmit_frames */
	uint32_t     tx_stat_dot3statssinglecollisionframes_hi;
	uint32_t     tx_stat_dot3statssinglecollisionframes_lo;
	/* multiple_collision_transmit_frames */
	uint32_t     tx_stat_dot3statsmultiplecollisionframes_hi;
	uint32_t     tx_stat_dot3statsmultiplecollisionframes_lo;
	/* deferred_transmissions */
	uint32_t     tx_stat_dot3statsdeferredtransmissions_hi;
	uint32_t     tx_stat_dot3statsdeferredtransmissions_lo;
	/* excessive_collision_frames */
	uint32_t     tx_stat_dot3statsexcessivecollisions_hi;
	uint32_t     tx_stat_dot3statsexcessivecollisions_lo;
	/* late_collision_frames */
	uint32_t     tx_stat_dot3statslatecollisions_hi;
	uint32_t     tx_stat_dot3statslatecollisions_lo;

	/* frames_transmitted_64_bytes */
	uint32_t     tx_stat_etherstatspkts64octets_hi;
	uint32_t     tx_stat_etherstatspkts64octets_lo;
	/* frames_transmitted_65_127_bytes */
	uint32_t     tx_stat_etherstatspkts65octetsto127octets_hi;
	uint32_t     tx_stat_etherstatspkts65octetsto127octets_lo;
	/* frames_transmitted_128_255_bytes */
	uint32_t     tx_stat_etherstatspkts128octetsto255octets_hi;
	uint32_t     tx_stat_etherstatspkts128octetsto255octets_lo;
	/* frames_transmitted_256_511_bytes */
	uint32_t     tx_stat_etherstatspkts256octetsto511octets_hi;
	uint32_t     tx_stat_etherstatspkts256octetsto511octets_lo;
	/* frames_transmitted_512_1023_bytes */
	uint32_t     tx_stat_etherstatspkts512octetsto1023octets_hi;
	uint32_t     tx_stat_etherstatspkts512octetsto1023octets_lo;
	/* frames_transmitted_1024_1522_bytes */
	uint32_t     tx_stat_etherstatspkts1024octetsto1522octets_hi;
	uint32_t     tx_stat_etherstatspkts1024octetsto1522octets_lo;
	/* frames_transmitted_1523_9022_bytes */
	uint32_t     tx_stat_etherstatspktsover1522octets_hi;
	uint32_t     tx_stat_etherstatspktsover1522octets_lo;
	uint32_t     tx_stat_mac_2047_hi;
	uint32_t     tx_stat_mac_2047_lo;
	uint32_t     tx_stat_mac_4095_hi;
	uint32_t     tx_stat_mac_4095_lo;
	uint32_t     tx_stat_mac_9216_hi;
	uint32_t     tx_stat_mac_9216_lo;
	uint32_t     tx_stat_mac_16383_hi;
	uint32_t     tx_stat_mac_16383_lo;

	/* internal_mac_transmit_errors */
	uint32_t     tx_stat_dot3statsinternalmactransmiterrors_hi;
	uint32_t     tx_stat_dot3statsinternalmactransmiterrors_lo;

	/* if_out_discards */
	uint32_t     tx_stat_mac_ufl_hi;
	uint32_t     tx_stat_mac_ufl_lo;
};


#define MAC_STX_IDX_MAX                     2

struct host_port_stats {
	uint32_t            host_port_stats_counter;

	struct mac_stx mac_stx[MAC_STX_IDX_MAX];

	uint32_t            brb_drop_hi;
	uint32_t            brb_drop_lo;

	uint32_t            not_used; /* obsolete as of MFW 7.2.1 */

	uint32_t            pfc_frames_tx_hi;
	uint32_t            pfc_frames_tx_lo;
	uint32_t            pfc_frames_rx_hi;
	uint32_t            pfc_frames_rx_lo;

	uint32_t            eee_lpi_count_hi;
	uint32_t            eee_lpi_count_lo;
};


struct host_func_stats {
	uint32_t     host_func_stats_start;

	uint32_t     total_bytes_received_hi;
	uint32_t     total_bytes_received_lo;

	uint32_t     total_bytes_transmitted_hi;
	uint32_t     total_bytes_transmitted_lo;

	uint32_t     total_unicast_packets_received_hi;
	uint32_t     total_unicast_packets_received_lo;

	uint32_t     total_multicast_packets_received_hi;
	uint32_t     total_multicast_packets_received_lo;

	uint32_t     total_broadcast_packets_received_hi;
	uint32_t     total_broadcast_packets_received_lo;

	uint32_t     total_unicast_packets_transmitted_hi;
	uint32_t     total_unicast_packets_transmitted_lo;

	uint32_t     total_multicast_packets_transmitted_hi;
	uint32_t     total_multicast_packets_transmitted_lo;

	uint32_t     total_broadcast_packets_transmitted_hi;
	uint32_t     total_broadcast_packets_transmitted_lo;

	uint32_t     valid_bytes_received_hi;
	uint32_t     valid_bytes_received_lo;

	uint32_t     host_func_stats_end;
};

/* VIC definitions */
#define VICSTATST_UIF_INDEX 2

/*
 * stats collected for afex.
 * NOTE: structure is exactly as expected to be received by the switch.
 *       order must remain exactly as is unless protocol changes !
 */
struct afex_stats {
	uint32_t tx_unicast_frames_hi;
	uint32_t tx_unicast_frames_lo;
	uint32_t tx_unicast_bytes_hi;
	uint32_t tx_unicast_bytes_lo;
	uint32_t tx_multicast_frames_hi;
	uint32_t tx_multicast_frames_lo;
	uint32_t tx_multicast_bytes_hi;
	uint32_t tx_multicast_bytes_lo;
	uint32_t tx_broadcast_frames_hi;
	uint32_t tx_broadcast_frames_lo;
	uint32_t tx_broadcast_bytes_hi;
	uint32_t tx_broadcast_bytes_lo;
	uint32_t tx_frames_discarded_hi;
	uint32_t tx_frames_discarded_lo;
	uint32_t tx_frames_dropped_hi;
	uint32_t tx_frames_dropped_lo;

	uint32_t rx_unicast_frames_hi;
	uint32_t rx_unicast_frames_lo;
	uint32_t rx_unicast_bytes_hi;
	uint32_t rx_unicast_bytes_lo;
	uint32_t rx_multicast_frames_hi;
	uint32_t rx_multicast_frames_lo;
	uint32_t rx_multicast_bytes_hi;
	uint32_t rx_multicast_bytes_lo;
	uint32_t rx_broadcast_frames_hi;
	uint32_t rx_broadcast_frames_lo;
	uint32_t rx_broadcast_bytes_hi;
	uint32_t rx_broadcast_bytes_lo;
	uint32_t rx_frames_discarded_hi;
	uint32_t rx_frames_discarded_lo;
	uint32_t rx_frames_dropped_hi;
	uint32_t rx_frames_dropped_lo;
};

/* To maintain backward compatibility between FW and drivers, new elements */
/* should be added to the end of the structure. */

/* Per  Port Statistics    */
struct port_info {
	uint32_t size; /* size of this structure (i.e. sizeof(port_info))  */
	uint32_t enabled;      /* 0 =Disabled, 1= Enabled */
	uint32_t link_speed;   /* multiplier of 100Mb */
	uint32_t wol_support;  /* WoL Support (i.e. Non-Zero if WOL supported ) */
	uint32_t flow_control; /* 802.3X Flow Ctrl. 0=off 1=RX 2=TX 3=RX&TX.*/
	uint32_t flex10;     /* Flex10 mode enabled. non zero = yes */
	uint32_t rx_drops;  /* RX Discards. Counters roll over, never reset */
	uint32_t rx_errors; /* RX Errors. Physical Port Stats L95, All PFs and NC-SI.
				   This is flagged by Consumer as an error. */
	uint32_t rx_uncast_lo;   /* RX Unicast Packets. Free running counters: */
	uint32_t rx_uncast_hi;   /* RX Unicast Packets. Free running counters: */
	uint32_t rx_mcast_lo;    /* RX Multicast Packets  */
	uint32_t rx_mcast_hi;    /* RX Multicast Packets  */
	uint32_t rx_bcast_lo;    /* RX Broadcast Packets  */
	uint32_t rx_bcast_hi;    /* RX Broadcast Packets  */
	uint32_t tx_uncast_lo;   /* TX Unicast Packets   */
	uint32_t tx_uncast_hi;   /* TX Unicast Packets   */
	uint32_t tx_mcast_lo;    /* TX Multicast Packets  */
	uint32_t tx_mcast_hi;    /* TX Multicast Packets  */
	uint32_t tx_bcast_lo;    /* TX Broadcast Packets  */
	uint32_t tx_bcast_hi;    /* TX Broadcast Packets  */
	uint32_t tx_errors;      /* TX Errors              */
	uint32_t tx_discards;    /* TX Discards          */
	uint32_t rx_frames_lo;   /* RX Frames received  */
	uint32_t rx_frames_hi;   /* RX Frames received  */
	uint32_t rx_bytes_lo;    /* RX Bytes received    */
	uint32_t rx_bytes_hi;    /* RX Bytes received    */
	uint32_t tx_frames_lo;   /* TX Frames sent      */
	uint32_t tx_frames_hi;   /* TX Frames sent      */
	uint32_t tx_bytes_lo;    /* TX Bytes sent        */
	uint32_t tx_bytes_hi;    /* TX Bytes sent        */
	uint32_t link_status;  /* Port P Link Status. 1:0 bit for port enabled.
				1:1 bit for link good,
				2:1 Set if link changed between last poll. */
	uint32_t tx_pfc_frames_lo;   /* PFC Frames sent.    */
	uint32_t tx_pfc_frames_hi;   /* PFC Frames sent.    */
	uint32_t rx_pfc_frames_lo;   /* PFC Frames Received. */
	uint32_t rx_pfc_frames_hi;   /* PFC Frames Received. */
};


#define BCM_5710_FW_MAJOR_VERSION			7
#define BCM_5710_FW_MINOR_VERSION			13
#define BCM_5710_FW_REVISION_VERSION		1
#define BCM_5710_FW_ENGINEERING_VERSION		0
#define BCM_5710_FW_COMPILE_FLAGS			1


/*
 * attention bits $$KEEP_ENDIANNESS$$
 */
struct atten_sp_status_block
{
	uint32_t attn_bits /* 16 bit of attention signal lines */;
	uint32_t attn_bits_ack /* 16 bit of attention signal ack */;
	uint8_t status_block_id /* status block id */;
	uint8_t reserved0 /* resreved for padding */;
	uint16_t attn_bits_index /* attention bits running index */;
	uint32_t reserved1 /* resreved for padding */;
};


/*
 * The eth aggregative context of Cstorm
 */
struct cstorm_eth_ag_context
{
	uint32_t __reserved0[10];
};


/*
 * The iscsi aggregative context of Cstorm
 */
struct cstorm_iscsi_ag_context
{
	uint32_t agg_vars1;
		#define CSTORM_ISCSI_AG_CONTEXT_STATE                                                (0xFF<<0) /* BitField agg_vars1Various aggregative variables	The state of the connection */
		#define CSTORM_ISCSI_AG_CONTEXT_STATE_SHIFT                                          0
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<8) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                8
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                      (0x1<<9) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                9
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                      (0x1<<10) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                10
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                      (0x1<<11) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                11
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN                           (0x1<<12) /* BitField agg_vars1Various aggregative variables	ULP Rx SE counter flag enable */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN_SHIFT                     12
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN                          (0x1<<13) /* BitField agg_vars1Various aggregative variables	ULP Rx invalidate counter flag enable */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN_SHIFT                    13
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF                                            (0x3<<14) /* BitField agg_vars1Various aggregative variables	Aux 4 counter flag */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_SHIFT                                      14
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66                                         (0x3<<16) /* BitField agg_vars1Various aggregative variables	The connection QOS */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66_SHIFT                                   16
		#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN                                 (0x1<<18) /* BitField agg_vars1Various aggregative variables	Enable decision rule for fin_received_cf */
		#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN_SHIFT                           18
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<19) /* BitField agg_vars1Various aggregative variables	Enable decision rule for auxiliary counter flag 1 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   19
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX2_CF_EN                                         (0x1<<20) /* BitField agg_vars1Various aggregative variables	Enable decision rule for auxiliary counter flag 2 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX2_CF_EN_SHIFT                                   20
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<21) /* BitField agg_vars1Various aggregative variables	Enable decision rule for auxiliary counter flag 3 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   21
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_EN                                         (0x1<<22) /* BitField agg_vars1Various aggregative variables	Enable decision rule for auxiliary counter flag 4 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_EN_SHIFT                                   22
		#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE                                       (0x7<<23) /* BitField agg_vars1Various aggregative variables	0-NOP, 1-EQ, 2-NEQ, 3-GT, 4-GE, 5-LS, 6-LE */
		#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE_SHIFT                                 23
		#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE                                         (0x3<<26) /* BitField agg_vars1Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE_SHIFT                                   26
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52                                         (0x3<<28) /* BitField agg_vars1Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52_SHIFT                                   28
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53                                         (0x3<<30) /* BitField agg_vars1Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53_SHIFT                                   30
#if defined(__BIG_ENDIAN)
	uint8_t __aux1_th /* Aux1 threhsold for the decision */;
	uint8_t __aux1_val /* Aux1 aggregation value */;
	uint16_t __agg_vars2 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_vars2 /* Various aggregative variables*/;
	uint8_t __aux1_val /* Aux1 aggregation value */;
	uint8_t __aux1_th /* Aux1 threhsold for the decision */;
#endif
	uint32_t rel_seq /* The sequence to release */;
	uint32_t rel_seq_th /* The threshold for the released sequence */;
#if defined(__BIG_ENDIAN)
	uint16_t hq_cons /* The HQ Consumer */;
	uint16_t hq_prod /* The HQ producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hq_prod /* The HQ producer */;
	uint16_t hq_cons /* The HQ Consumer */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
	uint8_t __reserved61 /* General flags */;
	uint8_t __reserved60 /* ORQ consumer updated by the completor */;
	uint8_t __reserved59 /* ORQ ULP Rx consumer */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __reserved59 /* ORQ ULP Rx consumer */;
	uint8_t __reserved60 /* ORQ consumer updated by the completor */;
	uint8_t __reserved61 /* General flags */;
	uint8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __reserved64 /* RQ consumer kept by the completor */;
	uint16_t cq_u_prod /* Ustorm producer of CQ */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_u_prod /* Ustorm producer of CQ */;
	uint16_t __reserved64 /* RQ consumer kept by the completor */;
#endif
	uint32_t __cq_u_prod1 /* Ustorm producer of CQ 1 */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_vars3 /* Various aggregative variables*/;
	uint16_t cq_u_pend /* Ustorm pending completions of CQ */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_u_pend /* Ustorm pending completions of CQ */;
	uint16_t __agg_vars3 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __aux2_th /* Aux2 threhsold for the decision */;
	uint16_t aux2_val /* Aux2 aggregation value */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t aux2_val /* Aux2 aggregation value */;
	uint16_t __aux2_th /* Aux2 threhsold for the decision */;
#endif
};


/*
 * The toe aggregative context of Cstorm
 */
struct cstorm_toe_ag_context
{
	uint32_t __agg_vars1 /* Various aggregative variables*/;
#if defined(__BIG_ENDIAN)
	uint8_t __aux1_th /* Aux1 threhsold for the decision */;
	uint8_t __aux1_val /* Aux1 aggregation value */;
	uint16_t __agg_vars2 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_vars2 /* Various aggregative variables*/;
	uint8_t __aux1_val /* Aux1 aggregation value */;
	uint8_t __aux1_th /* Aux1 threhsold for the decision */;
#endif
	uint32_t rel_seq /* The sequence to release */;
	uint32_t __rel_seq_threshold /* The threshold for the released sequence */;
#if defined(__BIG_ENDIAN)
	uint16_t __reserved58 /* The HQ Consumer */;
	uint16_t bd_prod /* The HQ producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t bd_prod /* The HQ producer */;
	uint16_t __reserved58 /* The HQ Consumer */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
	uint8_t __reserved61 /* General flags */;
	uint8_t __reserved60 /* ORQ consumer updated by the completor */;
	uint8_t __completion_opcode /* ORQ ULP Rx consumer */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __completion_opcode /* ORQ ULP Rx consumer */;
	uint8_t __reserved60 /* ORQ consumer updated by the completor */;
	uint8_t __reserved61 /* General flags */;
	uint8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __reserved64 /* RQ consumer kept by the completor */;
	uint16_t __reserved63 /* RQ consumer updated by the ULP RX */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __reserved63 /* RQ consumer updated by the ULP RX */;
	uint16_t __reserved64 /* RQ consumer kept by the completor */;
#endif
	uint32_t snd_max /* The ACK sequence number received in the last completed DDP */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_vars3 /* Various aggregative variables*/;
	uint16_t __reserved67 /* A counter for the number of RQ WQEs with invalidate the USTORM encountered */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __reserved67 /* A counter for the number of RQ WQEs with invalidate the USTORM encountered */;
	uint16_t __agg_vars3 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __aux2_th /* Aux2 threhsold for the decision */;
	uint16_t __aux2_val /* Aux2 aggregation value */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __aux2_val /* Aux2 aggregation value */;
	uint16_t __aux2_th /* Aux2 threhsold for the decision */;
#endif
};


/*
 * dmae command structure
 */
struct dmae_cmd
{
	uint32_t opcode;
		#define DMAE_CMD_SRC                                                                 (0x1<<0) /* BitField opcode	Whether the source is the PCIe or the GRC. 0- The source is the PCIe 1- The source is the GRC. */
		#define DMAE_CMD_SRC_SHIFT                                                           0
		#define DMAE_CMD_DST                                                                 (0x3<<1) /* BitField opcode	The destination of the DMA can be: 0-None 1-PCIe 2-GRC 3-None  */
		#define DMAE_CMD_DST_SHIFT                                                           1
		#define DMAE_CMD_C_DST                                                               (0x1<<3) /* BitField opcode	The destination of the completion: 0-PCIe 1-GRC */
		#define DMAE_CMD_C_DST_SHIFT                                                         3
		#define DMAE_CMD_C_TYPE_ENABLE                                                       (0x1<<4) /* BitField opcode	Whether to write a completion word to the completion destination: 0-Do not write a completion word 1-Write the completion word  */
		#define DMAE_CMD_C_TYPE_ENABLE_SHIFT                                                 4
		#define DMAE_CMD_C_TYPE_CRC_ENABLE                                                   (0x1<<5) /* BitField opcode	Whether to write a CRC word to the completion destination 0-Do not write a CRC word 1-Write a CRC word  */
		#define DMAE_CMD_C_TYPE_CRC_ENABLE_SHIFT                                             5
		#define DMAE_CMD_C_TYPE_CRC_OFFSET                                                   (0x7<<6) /* BitField opcode	The CRC word should be taken from the DMAE GRC space from address 9+X, where X is the value in these bits. */
		#define DMAE_CMD_C_TYPE_CRC_OFFSET_SHIFT                                             6
		#define DMAE_CMD_ENDIANITY                                                           (0x3<<9) /* BitField opcode	swapping mode. */
		#define DMAE_CMD_ENDIANITY_SHIFT                                                     9
		#define DMAE_CMD_PORT                                                                (0x1<<11) /* BitField opcode	Which network port ID to present to the PCI request interface */
		#define DMAE_CMD_PORT_SHIFT                                                          11
		#define DMAE_CMD_CRC_RESET                                                           (0x1<<12) /* BitField opcode	reset crc result */
		#define DMAE_CMD_CRC_RESET_SHIFT                                                     12
		#define DMAE_CMD_SRC_RESET                                                           (0x1<<13) /* BitField opcode	reset source address in next go */
		#define DMAE_CMD_SRC_RESET_SHIFT                                                     13
		#define DMAE_CMD_DST_RESET                                                           (0x1<<14) /* BitField opcode	reset dest address in next go */
		#define DMAE_CMD_DST_RESET_SHIFT                                                     14
		#define DMAE_CMD_E1HVN                                                               (0x3<<15) /* BitField opcode	vnic number E2 and onwards source vnic */
		#define DMAE_CMD_E1HVN_SHIFT                                                         15
		#define DMAE_CMD_DST_VN                                                              (0x3<<17) /* BitField opcode	E2 and onwards dest vnic */
		#define DMAE_CMD_DST_VN_SHIFT                                                        17
		#define DMAE_CMD_C_FUNC                                                              (0x1<<19) /* BitField opcode	E2 and onwards which function gets the completion src_vn(e1hvn)-0 dst_vn-1 */
		#define DMAE_CMD_C_FUNC_SHIFT                                                        19
		#define DMAE_CMD_ERR_POLICY                                                          (0x3<<20) /* BitField opcode	E2 and onwards what to do when theres a completion and a PCI error regular-0 error indication-1 no completion-2 */
		#define DMAE_CMD_ERR_POLICY_SHIFT                                                    20
		#define DMAE_CMD_RESERVED0                                                           (0x3FF<<22) /* BitField opcode	 */
		#define DMAE_CMD_RESERVED0_SHIFT                                                     22
	uint32_t src_addr_lo /* source address low/grc address */;
	uint32_t src_addr_hi /* source address hi */;
	uint32_t dst_addr_lo /* dest address low/grc address */;
	uint32_t dst_addr_hi /* dest address hi */;
#if defined(__BIG_ENDIAN)
	uint16_t opcode_iov;
		#define DMAE_CMD_SRC_VFID                                                            (0x3F<<0) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	source VF id */
		#define DMAE_CMD_SRC_VFID_SHIFT                                                      0
		#define DMAE_CMD_SRC_VFPF                                                            (0x1<<6) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	selects the source function PF-0, VF-1 */
		#define DMAE_CMD_SRC_VFPF_SHIFT                                                      6
		#define DMAE_CMD_RESERVED1                                                           (0x1<<7) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED1_SHIFT                                                     7
		#define DMAE_CMD_DST_VFID                                                            (0x3F<<8) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	destination VF id */
		#define DMAE_CMD_DST_VFID_SHIFT                                                      8
		#define DMAE_CMD_DST_VFPF                                                            (0x1<<14) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	selects the destination function PF-0, VF-1 */
		#define DMAE_CMD_DST_VFPF_SHIFT                                                      14
		#define DMAE_CMD_RESERVED2                                                           (0x1<<15) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED2_SHIFT                                                     15
	uint16_t len /* copy length */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t len /* copy length */;
	uint16_t opcode_iov;
		#define DMAE_CMD_SRC_VFID                                                            (0x3F<<0) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	source VF id */
		#define DMAE_CMD_SRC_VFID_SHIFT                                                      0
		#define DMAE_CMD_SRC_VFPF                                                            (0x1<<6) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	selects the source function PF-0, VF-1 */
		#define DMAE_CMD_SRC_VFPF_SHIFT                                                      6
		#define DMAE_CMD_RESERVED1                                                           (0x1<<7) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED1_SHIFT                                                     7
		#define DMAE_CMD_DST_VFID                                                            (0x3F<<8) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	destination VF id */
		#define DMAE_CMD_DST_VFID_SHIFT                                                      8
		#define DMAE_CMD_DST_VFPF                                                            (0x1<<14) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	selects the destination function PF-0, VF-1 */
		#define DMAE_CMD_DST_VFPF_SHIFT                                                      14
		#define DMAE_CMD_RESERVED2                                                           (0x1<<15) /* BitField opcode_iovE2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED2_SHIFT                                                     15
#endif
	uint32_t comp_addr_lo /* completion address low/grc address */;
	uint32_t comp_addr_hi /* completion address hi */;
	uint32_t comp_val /* value to write to completion address */;
	uint32_t crc32 /* crc32 result */;
	uint32_t crc32_c /* crc32_c result */;
#if defined(__BIG_ENDIAN)
	uint16_t crc16_c /* crc16_c result */;
	uint16_t crc16 /* crc16 result */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t crc16 /* crc16 result */;
	uint16_t crc16_c /* crc16_c result */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved3;
	uint16_t crc_t10 /* crc_t10 result */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t crc_t10 /* crc_t10 result */;
	uint16_t reserved3;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t xsum8 /* checksum8 result */;
	uint16_t xsum16 /* checksum16 result */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t xsum16 /* checksum16 result */;
	uint16_t xsum8 /* checksum8 result */;
#endif
};


/*
 * common data for all protocols
 */
struct doorbell_hdr_t
{
	uint8_t data;
		#define DOORBELL_HDR_T_RX                                                            (0x1<<0) /* BitField data	1 for rx doorbell, 0 for tx doorbell */
		#define DOORBELL_HDR_T_RX_SHIFT                                                      0
		#define DOORBELL_HDR_T_DB_TYPE                                                       (0x1<<1) /* BitField data	0 for normal doorbell, 1 for advertise wnd doorbell */
		#define DOORBELL_HDR_T_DB_TYPE_SHIFT                                                 1
		#define DOORBELL_HDR_T_DPM_SIZE                                                      (0x3<<2) /* BitField data	rdma tx only: DPM transaction size specifier (64/128/256/512 bytes) */
		#define DOORBELL_HDR_T_DPM_SIZE_SHIFT                                                2
		#define DOORBELL_HDR_T_CONN_TYPE                                                     (0xF<<4) /* BitField data	connection type */
		#define DOORBELL_HDR_T_CONN_TYPE_SHIFT                                               4
};

/*
 * Ethernet doorbell
 */
struct eth_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t npackets /* number of data bytes that were added in the doorbell */;
	uint8_t params;
		#define ETH_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define ETH_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                         (0x1<<6) /* BitField params	tx fin command flag */
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                   6
		#define ETH_TX_DOORBELL_SPARE                                                        (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define ETH_TX_DOORBELL_SPARE_SHIFT                                                  7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define ETH_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define ETH_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                         (0x1<<6) /* BitField params	tx fin command flag */
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                   6
		#define ETH_TX_DOORBELL_SPARE                                                        (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define ETH_TX_DOORBELL_SPARE_SHIFT                                                  7
	uint16_t npackets /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * 3 lines. status block $$KEEP_ENDIANNESS$$
 */
struct hc_status_block_e1x
{
	uint16_t index_values[HC_SB_MAX_INDICES_E1X] /* indices reported by cstorm */;
	uint16_t running_index[HC_SB_MAX_SM] /* Status Block running indices */;
	uint32_t rsrv[11];
};

/*
 * host status block
 */
struct host_hc_status_block_e1x
{
	struct hc_status_block_e1x sb /* fast path indices */;
};


/*
 * 3 lines. status block $$KEEP_ENDIANNESS$$
 */
struct hc_status_block_e2
{
	uint16_t index_values[HC_SB_MAX_INDICES_E2] /* indices reported by cstorm */;
	uint16_t running_index[HC_SB_MAX_SM] /* Status Block running indices */;
	uint32_t reserved[11];
};

/*
 * host status block
 */
struct host_hc_status_block_e2
{
	struct hc_status_block_e2 sb /* fast path indices */;
};


/*
 * 5 lines. slow-path status block $$KEEP_ENDIANNESS$$
 */
struct hc_sp_status_block
{
	uint16_t index_values[HC_SP_SB_MAX_INDICES] /* indices reported by cstorm */;
	uint16_t running_index /* Status Block running index */;
	uint16_t rsrv;
	uint32_t rsrv1;
};

/*
 * host status block
 */
struct host_sp_status_block
{
	struct atten_sp_status_block atten_status_block /* attention bits section */;
	struct hc_sp_status_block sp_sb /* slow path indices */;
};


/*
 * IGU driver acknowledgment register
 */
struct igu_ack_register
{
#if defined(__BIG_ENDIAN)
	uint16_t sb_id_and_flags;
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID                                             (0x1F<<0) /* BitField sb_id_and_flags	0-15: non default status blocks, 16: default status block */
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT                                       0
		#define IGU_ACK_REGISTER_STORM_ID                                                    (0x7<<5) /* BitField sb_id_and_flags	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_ACK_REGISTER_STORM_ID_SHIFT                                              5
		#define IGU_ACK_REGISTER_UPDATE_INDEX                                                (0x1<<8) /* BitField sb_id_and_flags	if set, acknowledges status block index */
		#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT                                          8
		#define IGU_ACK_REGISTER_INTERRUPT_MODE                                              (0x3<<9) /* BitField sb_id_and_flags	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT                                        9
		#define IGU_ACK_REGISTER_RESERVED                                                    (0x1F<<11) /* BitField sb_id_and_flags	 */
		#define IGU_ACK_REGISTER_RESERVED_SHIFT                                              11
	uint16_t status_block_index /* status block index acknowledgement */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t status_block_index /* status block index acknowledgement */;
	uint16_t sb_id_and_flags;
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID                                             (0x1F<<0) /* BitField sb_id_and_flags	0-15: non default status blocks, 16: default status block */
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT                                       0
		#define IGU_ACK_REGISTER_STORM_ID                                                    (0x7<<5) /* BitField sb_id_and_flags	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_ACK_REGISTER_STORM_ID_SHIFT                                              5
		#define IGU_ACK_REGISTER_UPDATE_INDEX                                                (0x1<<8) /* BitField sb_id_and_flags	if set, acknowledges status block index */
		#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT                                          8
		#define IGU_ACK_REGISTER_INTERRUPT_MODE                                              (0x3<<9) /* BitField sb_id_and_flags	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT                                        9
		#define IGU_ACK_REGISTER_RESERVED                                                    (0x1F<<11) /* BitField sb_id_and_flags	 */
		#define IGU_ACK_REGISTER_RESERVED_SHIFT                                              11
#endif
};


/*
 * IGU driver acknowledgement register
 */
struct igu_backward_compatible
{
	uint32_t sb_id_and_flags;
		#define IGU_BACKWARD_COMPATIBLE_SB_INDEX                                             (0xFFFF<<0) /* BitField sb_id_and_flags	 */
		#define IGU_BACKWARD_COMPATIBLE_SB_INDEX_SHIFT                                       0
		#define IGU_BACKWARD_COMPATIBLE_SB_SELECT                                            (0x1F<<16) /* BitField sb_id_and_flags	 */
		#define IGU_BACKWARD_COMPATIBLE_SB_SELECT_SHIFT                                      16
		#define IGU_BACKWARD_COMPATIBLE_SEGMENT_ACCESS                                       (0x7<<21) /* BitField sb_id_and_flags	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_BACKWARD_COMPATIBLE_SEGMENT_ACCESS_SHIFT                                 21
		#define IGU_BACKWARD_COMPATIBLE_BUPDATE                                              (0x1<<24) /* BitField sb_id_and_flags	if set, acknowledges status block index */
		#define IGU_BACKWARD_COMPATIBLE_BUPDATE_SHIFT                                        24
		#define IGU_BACKWARD_COMPATIBLE_ENABLE_INT                                           (0x3<<25) /* BitField sb_id_and_flags	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_BACKWARD_COMPATIBLE_ENABLE_INT_SHIFT                                     25
		#define IGU_BACKWARD_COMPATIBLE_RESERVED_0                                           (0x1F<<27) /* BitField sb_id_and_flags	 */
		#define IGU_BACKWARD_COMPATIBLE_RESERVED_0_SHIFT                                     27
	uint32_t reserved_2;
};


/*
 * IGU driver acknowledgement register
 */
struct igu_regular
{
	uint32_t sb_id_and_flags;
		#define IGU_REGULAR_SB_INDEX                                                         (0xFFFFF<<0) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_SB_INDEX_SHIFT                                                   0
		#define IGU_REGULAR_RESERVED0                                                        (0x1<<20) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_RESERVED0_SHIFT                                                  20
		#define IGU_REGULAR_SEGMENT_ACCESS                                                   (0x7<<21) /* BitField sb_id_and_flags	21-23 (use enum igu_seg_access) */
		#define IGU_REGULAR_SEGMENT_ACCESS_SHIFT                                             21
		#define IGU_REGULAR_BUPDATE                                                          (0x1<<24) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_BUPDATE_SHIFT                                                    24
		#define IGU_REGULAR_ENABLE_INT                                                       (0x3<<25) /* BitField sb_id_and_flags	interrupt enable/disable/nop (use enum igu_int_cmd) */
		#define IGU_REGULAR_ENABLE_INT_SHIFT                                                 25
		#define IGU_REGULAR_RESERVED_1                                                       (0x1<<27) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_RESERVED_1_SHIFT                                                 27
		#define IGU_REGULAR_CLEANUP_TYPE                                                     (0x3<<28) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_CLEANUP_TYPE_SHIFT                                               28
		#define IGU_REGULAR_CLEANUP_SET                                                      (0x1<<30) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_CLEANUP_SET_SHIFT                                                30
		#define IGU_REGULAR_BCLEANUP                                                         (0x1<<31) /* BitField sb_id_and_flags	 */
		#define IGU_REGULAR_BCLEANUP_SHIFT                                                   31
	uint32_t reserved_2;
};

/*
 * IGU driver acknowledgement register
 */
union igu_consprod_reg
{
	struct igu_regular regular;
	struct igu_backward_compatible backward_compatible;
};


/*
 * Igu control commands
 */
enum igu_ctrl_cmd
{
	IGU_CTRL_CMD_TYPE_RD,
	IGU_CTRL_CMD_TYPE_WR,
	MAX_IGU_CTRL_CMD};


/*
 * Control register for the IGU command register
 */
struct igu_ctrl_reg
{
	uint32_t ctrl_data;
		#define IGU_CTRL_REG_ADDRESS                                                         (0xFFF<<0) /* BitField ctrl_data	 */
		#define IGU_CTRL_REG_ADDRESS_SHIFT                                                   0
		#define IGU_CTRL_REG_FID                                                             (0x7F<<12) /* BitField ctrl_data	 */
		#define IGU_CTRL_REG_FID_SHIFT                                                       12
		#define IGU_CTRL_REG_RESERVED                                                        (0x1<<19) /* BitField ctrl_data	 */
		#define IGU_CTRL_REG_RESERVED_SHIFT                                                  19
		#define IGU_CTRL_REG_TYPE                                                            (0x1<<20) /* BitField ctrl_data	 (use enum igu_ctrl_cmd) */
		#define IGU_CTRL_REG_TYPE_SHIFT                                                      20
		#define IGU_CTRL_REG_UNUSED                                                          (0x7FF<<21) /* BitField ctrl_data	 */
		#define IGU_CTRL_REG_UNUSED_SHIFT                                                    21
};


/*
 * Igu interrupt command
 */
enum igu_int_cmd
{
	IGU_INT_ENABLE,
	IGU_INT_DISABLE,
	IGU_INT_NOP,
	IGU_INT_NOP2,
	MAX_IGU_INT_CMD};


/*
 * Igu segments
 */
enum igu_seg_access
{
	IGU_SEG_ACCESS_NORM,
	IGU_SEG_ACCESS_DEF,
	IGU_SEG_ACCESS_ATTN,
	MAX_IGU_SEG_ACCESS};


/*
 * iscsi doorbell
 */
struct iscsi_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved /* number of data bytes that were added in the doorbell */;
	uint8_t params;
		#define ISCSI_TX_DOORBELL_NUM_WQES                                                   (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define ISCSI_TX_DOORBELL_NUM_WQES_SHIFT                                             0
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                       (0x1<<6) /* BitField params	tx fin command flag */
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                 6
		#define ISCSI_TX_DOORBELL_SPARE                                                      (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define ISCSI_TX_DOORBELL_SPARE_SHIFT                                                7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define ISCSI_TX_DOORBELL_NUM_WQES                                                   (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define ISCSI_TX_DOORBELL_NUM_WQES_SHIFT                                             0
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                       (0x1<<6) /* BitField params	tx fin command flag */
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                 6
		#define ISCSI_TX_DOORBELL_SPARE                                                      (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define ISCSI_TX_DOORBELL_SPARE_SHIFT                                                7
	uint16_t reserved /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * Parser parsing flags field
 */
struct parsing_flags
{
	uint16_t flags;
		#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE                                          (0x1<<0) /* BitField flagscontext flags	0=non-unicast, 1=unicast (use enum prs_flags_eth_addr_type) */
		#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE_SHIFT                                    0
		#define PARSING_FLAGS_INNER_VLAN_EXIST                                               (0x1<<1) /* BitField flagscontext flags	0 or 1 */
		#define PARSING_FLAGS_INNER_VLAN_EXIST_SHIFT                                         1
		#define PARSING_FLAGS_OUTER_VLAN_EXIST                                               (0x1<<2) /* BitField flagscontext flags	0 or 1 */
		#define PARSING_FLAGS_OUTER_VLAN_EXIST_SHIFT                                         2
		#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL                                         (0x3<<3) /* BitField flagscontext flags	0=un-known, 1=Ipv4, 2=Ipv6,3=LLC SNAP un-known. LLC SNAP here refers only to LLC/SNAP packets that do not have Ipv4 or Ipv6 above them. Ipv4 and Ipv6 indications are even if they are over LLC/SNAP and not directly over Ethernet (use enum prs_flags_over_eth) */
		#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT                                   3
		#define PARSING_FLAGS_IP_OPTIONS                                                     (0x1<<5) /* BitField flagscontext flags	0=no IP options / extension headers. 1=IP options / extension header exist */
		#define PARSING_FLAGS_IP_OPTIONS_SHIFT                                               5
		#define PARSING_FLAGS_FRAGMENTATION_STATUS                                           (0x1<<6) /* BitField flagscontext flags	0=non-fragmented, 1=fragmented */
		#define PARSING_FLAGS_FRAGMENTATION_STATUS_SHIFT                                     6
		#define PARSING_FLAGS_OVER_IP_PROTOCOL                                               (0x3<<7) /* BitField flagscontext flags	0=un-known, 1=TCP, 2=UDP (use enum prs_flags_over_ip) */
		#define PARSING_FLAGS_OVER_IP_PROTOCOL_SHIFT                                         7
		#define PARSING_FLAGS_PURE_ACK_INDICATION                                            (0x1<<9) /* BitField flagscontext flags	0=packet with data, 1=pure-ACK (use enum prs_flags_ack_type) */
		#define PARSING_FLAGS_PURE_ACK_INDICATION_SHIFT                                      9
		#define PARSING_FLAGS_TCP_OPTIONS_EXIST                                              (0x1<<10) /* BitField flagscontext flags	0=no TCP options. 1=TCP options */
		#define PARSING_FLAGS_TCP_OPTIONS_EXIST_SHIFT                                        10
		#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG                                          (0x1<<11) /* BitField flagscontext flags	According to the TCP header options parsing */
		#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG_SHIFT                                    11
		#define PARSING_FLAGS_CONNECTION_MATCH                                               (0x1<<12) /* BitField flagscontext flags	connection match in searcher indication */
		#define PARSING_FLAGS_CONNECTION_MATCH_SHIFT                                         12
		#define PARSING_FLAGS_LLC_SNAP                                                       (0x1<<13) /* BitField flagscontext flags	LLC SNAP indication */
		#define PARSING_FLAGS_LLC_SNAP_SHIFT                                                 13
		#define PARSING_FLAGS_RESERVED0                                                      (0x3<<14) /* BitField flagscontext flags	 */
		#define PARSING_FLAGS_RESERVED0_SHIFT                                                14
};


/*
 * Parsing flags for TCP ACK type
 */
enum prs_flags_ack_type
{
	PRS_FLAG_PUREACK_PIGGY,
	PRS_FLAG_PUREACK_PURE,
	MAX_PRS_FLAGS_ACK_TYPE};


/*
 * Parsing flags for Ethernet address type
 */
enum prs_flags_eth_addr_type
{
	PRS_FLAG_ETHTYPE_NON_UNICAST,
	PRS_FLAG_ETHTYPE_UNICAST,
	MAX_PRS_FLAGS_ETH_ADDR_TYPE};


/*
 * Parsing flags for over-ethernet protocol
 */
enum prs_flags_over_eth
{
	PRS_FLAG_OVERETH_UNKNOWN,
	PRS_FLAG_OVERETH_IPV4,
	PRS_FLAG_OVERETH_IPV6,
	PRS_FLAG_OVERETH_LLCSNAP_UNKNOWN,
	MAX_PRS_FLAGS_OVER_ETH};


/*
 * Parsing flags for over-IP protocol
 */
enum prs_flags_over_ip
{
	PRS_FLAG_OVERIP_UNKNOWN,
	PRS_FLAG_OVERIP_TCP,
	PRS_FLAG_OVERIP_UDP,
	MAX_PRS_FLAGS_OVER_IP};


/*
 * SDM operation gen command (generate aggregative interrupt)
 */
struct sdm_op_gen
{
	uint32_t command;
		#define SDM_OP_GEN_COMP_PARAM                                                        (0x1F<<0) /* BitField commandcomp_param and comp_type	thread ID/aggr interrupt number/counter depending on the completion type */
		#define SDM_OP_GEN_COMP_PARAM_SHIFT                                                  0
		#define SDM_OP_GEN_COMP_TYPE                                                         (0x7<<5) /* BitField commandcomp_param and comp_type	Direct messages to CM / PCI switch are not supported in operation_gen completion */
		#define SDM_OP_GEN_COMP_TYPE_SHIFT                                                   5
		#define SDM_OP_GEN_AGG_VECT_IDX                                                      (0xFF<<8) /* BitField commandcomp_param and comp_type	bit index in aggregated interrupt vector */
		#define SDM_OP_GEN_AGG_VECT_IDX_SHIFT                                                8
		#define SDM_OP_GEN_AGG_VECT_IDX_VALID                                                (0x1<<16) /* BitField commandcomp_param and comp_type	 */
		#define SDM_OP_GEN_AGG_VECT_IDX_VALID_SHIFT                                          16
		#define SDM_OP_GEN_RESERVED                                                          (0x7FFF<<17) /* BitField commandcomp_param and comp_type	 */
		#define SDM_OP_GEN_RESERVED_SHIFT                                                    17
};


/*
 * Timers connection context
 */
struct timers_block_context
{
	uint32_t __client0 /* data of client 0 of the timers block*/;
	uint32_t __client1 /* data of client 1 of the timers block*/;
	uint32_t __client2 /* data of client 2 of the timers block*/;
	uint32_t flags;
		#define __TIMERS_BLOCK_CONTEXT_NUM_OF_ACTIVE_TIMERS                                  (0x3<<0) /* BitField flagscontext flags	number of active timers running */
		#define __TIMERS_BLOCK_CONTEXT_NUM_OF_ACTIVE_TIMERS_SHIFT                            0
		#define TIMERS_BLOCK_CONTEXT_CONN_VALID_FLG                                          (0x1<<2) /* BitField flagscontext flags	flag: is connection valid (should be set by driver to 1 in toe/iscsi connections) */
		#define TIMERS_BLOCK_CONTEXT_CONN_VALID_FLG_SHIFT                                    2
		#define __TIMERS_BLOCK_CONTEXT_RESERVED0                                             (0x1FFFFFFF<<3) /* BitField flagscontext flags	 */
		#define __TIMERS_BLOCK_CONTEXT_RESERVED0_SHIFT                                       3
};


/*
 * advertise window doorbell
 */
struct toe_adv_wnd_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t wnd_sz_lsb /* Less significant bits of advertise window update value */;
	uint8_t wnd_sz_msb /* Most significant bits of advertise window update value */;
	struct doorbell_hdr_t hdr /* See description of the appropriate type */;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr /* See description of the appropriate type */;
	uint8_t wnd_sz_msb /* Most significant bits of advertise window update value */;
	uint16_t wnd_sz_lsb /* Less significant bits of advertise window update value */;
#endif
};


/*
 * toe rx BDs update doorbell
 */
struct toe_rx_bds_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t nbds /* BDs update value */;
	uint8_t params;
		#define TOE_RX_BDS_DOORBELL_RESERVED                                                 (0x1F<<0) /* BitField params	reserved */
		#define TOE_RX_BDS_DOORBELL_RESERVED_SHIFT                                           0
		#define TOE_RX_BDS_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params	BDs update doorbell opcode (2) */
		#define TOE_RX_BDS_DOORBELL_OPCODE_SHIFT                                             5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define TOE_RX_BDS_DOORBELL_RESERVED                                                 (0x1F<<0) /* BitField params	reserved */
		#define TOE_RX_BDS_DOORBELL_RESERVED_SHIFT                                           0
		#define TOE_RX_BDS_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params	BDs update doorbell opcode (2) */
		#define TOE_RX_BDS_DOORBELL_OPCODE_SHIFT                                             5
	uint16_t nbds /* BDs update value */;
#endif
};


/*
 * toe rx bytes and BDs update doorbell
 */
struct toe_rx_bytes_and_bds_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t nbytes /* nbytes */;
	uint8_t params;
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS                                           (0x1F<<0) /* BitField params	producer delta from the last doorbell */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS_SHIFT                                     0
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE                                         (0x7<<5) /* BitField params	rx bytes and BDs update doorbell opcode (1) */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE_SHIFT                                   5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS                                           (0x1F<<0) /* BitField params	producer delta from the last doorbell */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS_SHIFT                                     0
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE                                         (0x7<<5) /* BitField params	rx bytes and BDs update doorbell opcode (1) */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE_SHIFT                                   5
	uint16_t nbytes /* nbytes */;
#endif
};


/*
 * toe rx bytes doorbell
 */
struct toe_rx_byte_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t nbytes_lsb /* bits [0:15] of nbytes */;
	uint8_t params;
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB                                              (0x1F<<0) /* BitField params	bits [20:16] of nbytes */
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB_SHIFT                                        0
		#define TOE_RX_BYTE_DOORBELL_OPCODE                                                  (0x7<<5) /* BitField params	rx bytes doorbell opcode (0) */
		#define TOE_RX_BYTE_DOORBELL_OPCODE_SHIFT                                            5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB                                              (0x1F<<0) /* BitField params	bits [20:16] of nbytes */
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB_SHIFT                                        0
		#define TOE_RX_BYTE_DOORBELL_OPCODE                                                  (0x7<<5) /* BitField params	rx bytes doorbell opcode (0) */
		#define TOE_RX_BYTE_DOORBELL_OPCODE_SHIFT                                            5
	uint16_t nbytes_lsb /* bits [0:15] of nbytes */;
#endif
};


/*
 * toe rx consume GRQ doorbell
 */
struct toe_rx_grq_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t nbytes_lsb /* bits [0:15] of nbytes */;
	uint8_t params;
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB                                               (0x1F<<0) /* BitField params	bits [20:16] of nbytes */
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB_SHIFT                                         0
		#define TOE_RX_GRQ_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params	rx GRQ doorbell opcode (4) */
		#define TOE_RX_GRQ_DOORBELL_OPCODE_SHIFT                                             5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB                                               (0x1F<<0) /* BitField params	bits [20:16] of nbytes */
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB_SHIFT                                         0
		#define TOE_RX_GRQ_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params	rx GRQ doorbell opcode (4) */
		#define TOE_RX_GRQ_DOORBELL_OPCODE_SHIFT                                             5
	uint16_t nbytes_lsb /* bits [0:15] of nbytes */;
#endif
};


/*
 * toe doorbell
 */
struct toe_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t nbytes /* number of data bytes that were added in the doorbell */;
	uint8_t params;
		#define TOE_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define TOE_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define TOE_TX_DOORBELL_TX_FIN_FLAG                                                  (0x1<<6) /* BitField params	tx fin command flag */
		#define TOE_TX_DOORBELL_TX_FIN_FLAG_SHIFT                                            6
		#define TOE_TX_DOORBELL_FLUSH                                                        (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define TOE_TX_DOORBELL_FLUSH_SHIFT                                                  7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	uint8_t params;
		#define TOE_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params	number of buffer descriptors that were added in the doorbell */
		#define TOE_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define TOE_TX_DOORBELL_TX_FIN_FLAG                                                  (0x1<<6) /* BitField params	tx fin command flag */
		#define TOE_TX_DOORBELL_TX_FIN_FLAG_SHIFT                                            6
		#define TOE_TX_DOORBELL_FLUSH                                                        (0x1<<7) /* BitField params	doorbell queue spare flag */
		#define TOE_TX_DOORBELL_FLUSH_SHIFT                                                  7
	uint16_t nbytes /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * The eth aggregative context of Tstorm
 */
struct tstorm_eth_ag_context
{
	uint32_t __reserved0[14];
};


/*
 * The fcoe extra aggregative context section of Tstorm
 */
struct tstorm_fcoe_extra_ag_context_section
{
	uint32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val2 /* aggregated value 2 */;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val5;
	uint8_t __agg_val6;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __agg_val6;
	uint16_t __agg_val5;
#endif
	uint32_t __lcq_prod /* Next sequence number to transmit, given by Tx */;
	uint32_t rtt_seq /* Rtt recording   sequence number */;
	uint32_t rtt_time /* Rtt recording   real time clock */;
	uint32_t __reserved66;
	uint32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	uint32_t tcp_agg_vars1;
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG                           (0x1<<0) /* BitField tcp_agg_vars1Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                     0
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                    (0x1<<1) /* BitField tcp_agg_vars1Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT              1
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF                              (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT                        2
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF                              (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                        4
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN                           (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT                     6
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                           (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                     7
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                       (0x1<<8) /* BitField tcp_agg_vars1Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                 8
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN                            (0x1<<9) /* BitField tcp_agg_vars1Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN_SHIFT                      9
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                               (0x1<<10) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                         10
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG                               (0x1<<11) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT                         11
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN                              (0x1<<12) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT                        12
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN                              (0x1<<13) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT                        13
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF                                 (0x3<<14) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_SHIFT                           14
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF                                 (0x3<<16) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_SHIFT                           16
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED                              (0x1<<18) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                        18
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN                           (0x1<<19) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                     19
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN                           (0x1<<20) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                     20
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN                           (0x1<<21) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                     21
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1                             (0x3<<22) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1_SHIFT                       22
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                     (0xF<<24) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT               24
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                     (0xF<<28) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT               28
	uint32_t snd_max /* Maximum sequence number that was ever transmitted */;
	uint32_t __lcq_cons /* Last ACK sequence number sent by the Tx */;
	uint32_t __reserved2;
};

/*
 * The fcoe aggregative context of Tstorm
 */
struct tstorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t ulp_credit;
	uint8_t agg_vars1;
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                         (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                   0
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF                                     (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT                               4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG                                           (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                     6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG                                           (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                     7
	uint8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* The state of the connection */;
	uint8_t agg_vars1;
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                         (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                   0
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF                                     (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT                               4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG                                           (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                     6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG                                           (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                     7
	uint16_t ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val4;
	uint16_t agg_vars2;
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG                                           (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                     0
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG                                           (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                     1
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF                                             (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT                                       2
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF                                             (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT                                       4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF                                             (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT                                       6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF                                             (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT                                       8
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG                                           (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                     10
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN                                  (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT                            11
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN                                            (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT                                      12
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN                                            (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT                                      13
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN                                            (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT                                      14
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN                                            (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT                                      15
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_vars2;
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG                                           (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                     0
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG                                           (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                     1
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF                                             (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT                                       2
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF                                             (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT                                       4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF                                             (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT                                       6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF                                             (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT                                       8
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG                                           (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                     10
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN                                  (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT                            11
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN                                            (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT                                      12
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN                                            (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT                                      13
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN                                            (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT                                      14
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN                                            (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT                                      15
	uint16_t __agg_val4;
#endif
	struct tstorm_fcoe_extra_ag_context_section __extra_section /* Extra context section */;
};


/*
 * The iscsi aggregative context section of Tstorm
 */
struct tstorm_iscsi_tcp_ag_context_section
{
	uint32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val2 /* aggregated value 2 */;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val5;
	uint8_t __agg_val6;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __agg_val6;
	uint16_t __agg_val5;
#endif
	uint32_t snd_nxt /* Next sequence number to transmit, given by Tx */;
	uint32_t rtt_seq /* Rtt recording   sequence number */;
	uint32_t rtt_time /* Rtt recording   real time clock */;
	uint32_t wnd_right_edge_local;
	uint32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	uint32_t tcp_agg_vars1;
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG                            (0x1<<0) /* BitField tcp_agg_vars1Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                      0
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                     (0x1<<1) /* BitField tcp_agg_vars1Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT               1
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_WND_UPD_CF                               (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT                         2
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF                               (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                         4
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN                            (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT                      6
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                            (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                      7
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                        (0x1<<8) /* BitField tcp_agg_vars1Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                  8
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_SND_NXT_EN                               (0x1<<9) /* BitField tcp_agg_vars1Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT                         9
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<10) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          10
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_FLAG                                (0x1<<11) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT                          11
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN                               (0x1<<12) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT                         12
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN                               (0x1<<13) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT                         13
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_CF                                  (0x3<<14) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX1_CF_SHIFT                            14
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_CF                                  (0x3<<16) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX2_CF_SHIFT                            16
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TX_BLOCKED                               (0x1<<18) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                         18
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                            (0x1<<19) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                      19
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN                            (0x1<<20) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                      20
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN                            (0x1<<21) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                      21
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RESERVED1                              (0x3<<22) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT                        22
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                      (0xF<<24) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT                24
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                      (0xF<<28) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_ISCSI_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT                28
	uint32_t snd_max /* Maximum sequence number that was ever transmitted */;
	uint32_t snd_una /* Last ACK sequence number sent by the Tx */;
	uint32_t __reserved2;
};

/*
 * The iscsi aggregative context of Tstorm
 */
struct tstorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t ulp_credit;
	uint8_t agg_vars1;
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                 (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                           4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG                                          (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT                                    6
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG                               (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT                         7
	uint8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* The state of the connection */;
	uint8_t agg_vars1;
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                 (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                           4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG                                          (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT                                    6
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG                               (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT                         7
	uint16_t ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val4;
	uint16_t agg_vars2;
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG                                 (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT                           0
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG                                (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT                          1
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF                                        (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT                                  2
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF                                     (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT                               4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF                                            (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT                                      6
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF                                            (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT                                      8
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG                                          (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT                                    10
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        11
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN                                     (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT                               12
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN                                  (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT                            13
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN                                           (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT                                     14
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN                                           (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT                                     15
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_vars2;
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG                                 (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT                           0
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG                                (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT                          1
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF                                        (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT                                  2
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF                                     (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT                               4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF                                            (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT                                      6
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF                                            (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT                                      8
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG                                          (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT                                    10
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        11
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN                                     (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT                               12
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN                                  (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT                            13
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN                                           (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT                                     14
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN                                           (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT                                     15
	uint16_t __agg_val4;
#endif
	struct tstorm_iscsi_tcp_ag_context_section tcp /* TCP context section, shared in TOE and iSCSI */;
};


/*
 * The tcp aggregative context section of Tstorm
 */
struct tstorm_tcp_tcp_ag_context_section
{
	uint32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val2 /* aggregated value 2 */;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val5;
	uint8_t __agg_val6;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __agg_val6;
	uint16_t __agg_val5;
#endif
	uint32_t snd_nxt /* Next sequence number to transmit, given by Tx */;
	uint32_t rtt_seq /* Rtt recording   sequence number */;
	uint32_t rtt_time /* Rtt recording   real time clock */;
	uint32_t __reserved66;
	uint32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	uint32_t tcp_agg_vars1;
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG                              (0x1<<0) /* BitField tcp_agg_vars1Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                        0
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                       (0x1<<1) /* BitField tcp_agg_vars1Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT                 1
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF                                 (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT                           2
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF                                 (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                           4
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN                              (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT                        6
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                              (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                        7
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                          (0x1<<8) /* BitField tcp_agg_vars1Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                    8
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN                                 (0x1<<9) /* BitField tcp_agg_vars1Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT                           9
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                  (0x1<<10) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                            10
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG                                  (0x1<<11) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT                            11
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN                                 (0x1<<12) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT                           12
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN                                 (0x1<<13) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT                           13
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF                                    (0x3<<14) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_SHIFT                              14
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF                                    (0x3<<16) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_SHIFT                              16
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED                                 (0x1<<18) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                           18
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<19) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        19
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN                              (0x1<<20) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                        20
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN                              (0x1<<21) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                        21
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1                                (0x3<<22) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT                          22
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                        (0xF<<24) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT                  24
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                        (0xF<<28) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT                  28
	uint32_t snd_max /* Maximum sequence number that was ever transmitted */;
	uint32_t snd_una /* Last ACK sequence number sent by the Tx */;
	uint32_t __reserved2;
};


/*
 * The toe aggregative context section of Tstorm
 */
struct tstorm_toe_tcp_ag_context_section
{
	uint32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val2 /* aggregated value 2 */;
	uint8_t __agg_val3 /* aggregated value 3 */;
	uint8_t __tcp_agg_vars2 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val5;
	uint8_t __agg_val6;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __agg_val6;
	uint16_t __agg_val5;
#endif
	uint32_t snd_nxt /* Next sequence number to transmit, given by Tx */;
	uint32_t rtt_seq /* Rtt recording   sequence number */;
	uint32_t rtt_time /* Rtt recording   real time clock */;
	uint32_t __reserved66;
	uint32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	uint32_t tcp_agg_vars1;
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG                              (0x1<<0) /* BitField tcp_agg_vars1Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                        0
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                       (0x1<<1) /* BitField tcp_agg_vars1Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT                 1
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED52                                 (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED52_SHIFT                           2
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF                                 (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                           4
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_WND_UPD_CF_EN                     (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_WND_UPD_CF_EN_SHIFT               6
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                              (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                        7
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                          (0x1<<8) /* BitField tcp_agg_vars1Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                    8
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_SND_NXT_EN                                 (0x1<<9) /* BitField tcp_agg_vars1Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT                           9
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_NEWRTTSAMPLE                               (0x1<<10) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_NEWRTTSAMPLE_SHIFT                         10
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED55                                 (0x1<<11) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED55_SHIFT                           11
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX1_CF_EN                        (0x1<<12) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX1_CF_EN_SHIFT                  12
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX2_CF_EN                        (0x1<<13) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX2_CF_EN_SHIFT                  13
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED56                                 (0x3<<14) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED56_SHIFT                           14
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED57                                 (0x3<<16) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED57_SHIFT                           16
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_BLOCKED                                 (0x1<<18) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                           18
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<19) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        19
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN                              (0x1<<20) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                        20
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN                              (0x1<<21) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                        21
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED1                                (0x3<<22) /* BitField tcp_agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT                          22
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                        (0xF<<24) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT                  24
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                        (0xF<<28) /* BitField tcp_agg_vars1Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT                  28
	uint32_t snd_max /* Maximum sequence number that was ever transmitted */;
	uint32_t snd_una /* Last ACK sequence number sent by the Tx */;
	uint32_t __reserved2;
};

/*
 * The toe aggregative context of Tstorm
 */
struct tstorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved54;
	uint8_t agg_vars1;
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                          (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                    0
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51                                             (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                       1
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52                                             (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                       2
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       3
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                   (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                             4
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG                                            (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                      6
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG                                            (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                      7
	uint8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __state /* The state of the connection */;
	uint8_t agg_vars1;
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                          (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                    0
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51                                             (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                       1
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52                                             (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                       2
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       3
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                   (0x3<<4) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                             4
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG                                            (0x1<<6) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                      6
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG                                            (0x1<<7) /* BitField agg_vars1Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                      7
	uint16_t reserved54;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val4;
	uint16_t agg_vars2;
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG                                            (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                      0
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG                                            (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                      1
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF                                              (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF_SHIFT                                        2
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF                                              (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF_SHIFT                                        4
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF                                              (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF_SHIFT                                        6
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF                                              (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF_SHIFT                                        8
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG                                            (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                      10
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                                (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                          11
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN                                    (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN_SHIFT                              12
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN                                    (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN_SHIFT                              13
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN                                    (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN_SHIFT                              14
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN                                    (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN_SHIFT                              15
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_vars2;
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG                                            (0x1<<0) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                      0
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG                                            (0x1<<1) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                      1
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF                                              (0x3<<2) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF_SHIFT                                        2
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF                                              (0x3<<4) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF_SHIFT                                        4
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF                                              (0x3<<6) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF_SHIFT                                        6
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF                                              (0x3<<8) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF_SHIFT                                        8
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG                                            (0x1<<10) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                      10
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                                (0x1<<11) /* BitField agg_vars2Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                          11
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN                                    (0x1<<12) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN_SHIFT                              12
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN                                    (0x1<<13) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN_SHIFT                              13
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN                                    (0x1<<14) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN_SHIFT                              14
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN                                    (0x1<<15) /* BitField agg_vars2Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN_SHIFT                              15
	uint16_t __agg_val4;
#endif
	struct tstorm_toe_tcp_ag_context_section tcp /* TCP context section, shared in TOE and iSCSI */;
};


/*
 * The eth aggregative context of Ustorm
 */
struct ustorm_eth_ag_context
{
	uint32_t __reserved0;
#if defined(__BIG_ENDIAN)
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	uint8_t __reserved2;
	uint16_t __reserved1;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __reserved1;
	uint8_t __reserved2;
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	uint32_t __reserved3[6];
};


/*
 * The fcoe aggregative context of Ustorm
 */
struct ustorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
	uint8_t agg_vars2;
		#define USTORM_FCOE_AG_CONTEXT_TX_CF                                                 (0x3<<0) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT                                           0
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF                                            (0x3<<2) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT                                      2
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE                                        (0x7<<4) /* BitField agg_vars2various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                  4
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK                                       (0x1<<7) /* BitField agg_vars2various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                 7
	uint8_t agg_vars1;
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define USTORM_FCOE_AG_CONTEXT_INV_CF                                                (0x3<<4) /* BitField agg_vars1various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT                                          4
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF                                         (0x3<<6) /* BitField agg_vars1various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT                                   6
	uint8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* The state of the connection */;
	uint8_t agg_vars1;
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define USTORM_FCOE_AG_CONTEXT_INV_CF                                                (0x3<<4) /* BitField agg_vars1various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT                                          4
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF                                         (0x3<<6) /* BitField agg_vars1various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT                                   6
	uint8_t agg_vars2;
		#define USTORM_FCOE_AG_CONTEXT_TX_CF                                                 (0x3<<0) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT                                           0
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF                                            (0x3<<2) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT                                      2
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE                                        (0x7<<4) /* BitField agg_vars2various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                  4
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK                                       (0x1<<7) /* BitField agg_vars2various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                 7
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	uint8_t agg_misc2;
	uint16_t pbf_tx_seq_ack /* Sequence number of the last sequence transmitted by PBF. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t pbf_tx_seq_ack /* Sequence number of the last sequence transmitted by PBF. */;
	uint8_t agg_misc2;
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	uint32_t agg_misc4;
#if defined(__BIG_ENDIAN)
	uint8_t agg_val3_th;
	uint8_t agg_val3;
	uint16_t agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_misc3;
	uint8_t agg_val3;
	uint8_t agg_val3_th;
#endif
	uint32_t expired_task_id /* Timer expiration task id */;
	uint32_t agg_misc4_th;
#if defined(__BIG_ENDIAN)
	uint16_t cq_prod /* CQ producer updated by FW */;
	uint16_t cq_cons /* CQ consumer updated by driver via doorbell */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_cons /* CQ consumer updated by driver via doorbell */;
	uint16_t cq_prod /* CQ producer updated by FW */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __reserved2;
	uint8_t decision_rules;
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE                                           (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT                                     0
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE                                       (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                 3
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG                                         (0x1<<6) /* BitField decision_rulesVarious decision rules	CQ negative arm indication updated via doorbell */
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT                                   6
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1                                           (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT                                     7
	uint8_t decision_rule_enable_bits;
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN                                  (0x1<<0) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT                            0
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN                                      (0x1<<1) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                                1
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN                                              (0x1<<2) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT                                        2
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN                                         (0x1<<3) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT                                   3
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<4) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    4
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN                                        (0x1<<5) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT                                  5
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN                                          (0x1<<6) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT                                    6
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
#elif defined(__LITTLE_ENDIAN)
	uint8_t decision_rule_enable_bits;
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN                                  (0x1<<0) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT                            0
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN                                      (0x1<<1) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                                1
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN                                              (0x1<<2) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT                                        2
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN                                         (0x1<<3) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT                                   3
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<4) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    4
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN                                        (0x1<<5) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT                                  5
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN                                          (0x1<<6) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT                                    6
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
	uint8_t decision_rules;
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE                                           (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT                                     0
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE                                       (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                 3
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG                                         (0x1<<6) /* BitField decision_rulesVarious decision rules	CQ negative arm indication updated via doorbell */
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT                                   6
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1                                           (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT                                     7
	uint16_t __reserved2;
#endif
};


/*
 * The iscsi aggregative context of Ustorm
 */
struct ustorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
	uint8_t agg_vars2;
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF                                                (0x3<<0) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT                                          0
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF                                           (0x3<<2) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT                                     2
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE                                       (0x7<<4) /* BitField agg_vars2various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                 4
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK                                      (0x1<<7) /* BitField agg_vars2various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                7
	uint8_t agg_vars1;
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF                                               (0x3<<4) /* BitField agg_vars1various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT                                         4
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF                                        (0x3<<6) /* BitField agg_vars1various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT                                  6
	uint8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* The state of the connection */;
	uint8_t agg_vars1;
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF                                               (0x3<<4) /* BitField agg_vars1various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT                                         4
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF                                        (0x3<<6) /* BitField agg_vars1various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT                                  6
	uint8_t agg_vars2;
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF                                                (0x3<<0) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT                                          0
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF                                           (0x3<<2) /* BitField agg_vars2various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT                                     2
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE                                       (0x7<<4) /* BitField agg_vars2various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                 4
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK                                      (0x1<<7) /* BitField agg_vars2various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                7
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	uint8_t agg_misc2;
	uint16_t __cq_local_comp_itt_val /* The local completion ITT to complete. Set by the CMP STORM RO for USTORM. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __cq_local_comp_itt_val /* The local completion ITT to complete. Set by the CMP STORM RO for USTORM. */;
	uint8_t agg_misc2;
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	uint32_t agg_misc4;
#if defined(__BIG_ENDIAN)
	uint8_t agg_val3_th;
	uint8_t agg_val3;
	uint16_t agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_misc3;
	uint8_t agg_val3;
	uint8_t agg_val3_th;
#endif
	uint32_t agg_val1;
	uint32_t agg_misc4_th;
#if defined(__BIG_ENDIAN)
	uint16_t agg_val2_th;
	uint16_t agg_val2;
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_val2;
	uint16_t agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __reserved2;
	uint8_t decision_rules;
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE                                      (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                3
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                  (0x1<<6) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                            6
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1                                          (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT                                    7
	uint8_t decision_rule_enable_bits;
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN                                            (0x1<<0) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT                                      0
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN                                     (0x1<<1) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                               1
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN                                             (0x1<<2) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT                                       2
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN                                        (0x1<<3) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT                                  3
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN                                (0x1<<4) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The local completion counter flag enable. Enabled by USTORM at the beginning. */
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT                          4
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<5) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        5
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<6) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   6
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
#elif defined(__LITTLE_ENDIAN)
	uint8_t decision_rule_enable_bits;
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN                                            (0x1<<0) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT                                      0
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN                                     (0x1<<1) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                               1
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN                                             (0x1<<2) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT                                       2
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN                                        (0x1<<3) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT                                  3
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN                                (0x1<<4) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The local completion counter flag enable. Enabled by USTORM at the beginning. */
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT                          4
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<5) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        5
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<6) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   6
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField decision_rule_enable_bitsEnable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
	uint8_t decision_rules;
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE                                      (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                3
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                  (0x1<<6) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                            6
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1                                          (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT                                    7
	uint16_t __reserved2;
#endif
};


/*
 * The toe aggregative context of Ustorm
 */
struct ustorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
	uint8_t __agg_vars2 /* various aggregation variables*/;
	uint8_t __agg_vars1 /* various aggregation variables*/;
	uint8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __state /* The state of the connection */;
	uint8_t __agg_vars1 /* various aggregation variables*/;
	uint8_t __agg_vars2 /* various aggregation variables*/;
	uint8_t __aux_counter_flags /* auxiliary counter flags*/;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	uint8_t __agg_misc2;
	uint16_t __agg_misc1;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_misc1;
	uint8_t __agg_misc2;
	uint8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	uint32_t __agg_misc4;
#if defined(__BIG_ENDIAN)
	uint8_t __agg_val3_th;
	uint8_t __agg_val3;
	uint16_t __agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_misc3;
	uint8_t __agg_val3;
	uint8_t __agg_val3_th;
#endif
	uint32_t driver_doorbell_info_ptr_lo /* the host pointer that consist the struct of info updated */;
	uint32_t driver_doorbell_info_ptr_hi /* the host pointer that consist the struct of info updated */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val2_th;
	uint16_t rq_prod /* The RQ producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rq_prod /* The RQ producer */;
	uint16_t __agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __reserved2;
	uint8_t decision_rules;
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE                                        (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                  3
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                    (0x1<<6) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                              6
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1                                            (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1_SHIFT                                      7
	uint8_t __decision_rule_enable_bits /* Enable bits for various decision rules*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __decision_rule_enable_bits /* Enable bits for various decision rules*/;
	uint8_t decision_rules;
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE                                        (0x7<<3) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                  3
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                    (0x1<<6) /* BitField decision_rulesVarious decision rules	 */
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                              6
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1                                            (0x1<<7) /* BitField decision_rulesVarious decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1_SHIFT                                      7
	uint16_t __reserved2;
#endif
};


/*
 * The eth aggregative context of Xstorm
 */
struct xstorm_eth_ag_context
{
	uint32_t reserved0;
#if defined(__BIG_ENDIAN)
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	uint8_t reserved2;
	uint16_t reserved1;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved1;
	uint8_t reserved2;
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	uint32_t reserved3[30];
};


/*
 * The fcoe aggregative context section of Xstorm
 */
struct xstorm_fcoe_extra_ag_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t tcp_agg_vars1;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51                            (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                     (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT               2
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                        (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                  4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN            (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT      6
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG           (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT     7
	uint8_t __reserved_da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint16_t __mtu /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __mtu /* MSS used for nagle algorithm and for transmission */;
	uint8_t __reserved_da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint8_t tcp_agg_vars1;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51                            (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                     (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT               2
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                        (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                  4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN            (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT      6
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG           (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT     7
#endif
	uint32_t snd_nxt /* The current sequence number to send */;
	uint32_t __xfrqe_bd_addr_lo /* The Current transmission window in bytes */;
	uint32_t __xfrqe_bd_addr_hi /* The current Send UNA sequence number */;
	uint32_t __xfrqe_data1 /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
	uint8_t __tx_dest /* aggregated value 8 */;
	uint16_t tcp_agg_vars2;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57                            (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58                            (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT                      1
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59                            (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT                      2
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG                             (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                       3
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG                             (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                       4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60                            (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT                      5
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN         (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT   6
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                     (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT               7
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN               (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT         8
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                             (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                       9
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF                            (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                      10
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                 (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT           12
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                    (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT              14
#elif defined(__LITTLE_ENDIAN)
	uint16_t tcp_agg_vars2;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57                            (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58                            (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT                      1
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59                            (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT                      2
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG                             (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                       3
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG                             (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                       4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60                            (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT                      5
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN         (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT   6
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                     (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT               7
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN               (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT         8
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                             (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                       9
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF                            (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                      10
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                 (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT           12
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                    (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT              14
	uint8_t __tx_dest /* aggregated value 8 */;
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	uint32_t __sq_base_addr_lo /* The low page address which the SQ resides in host memory */;
	uint32_t __sq_base_addr_hi /* The high page address which the SQ resides in host memory */;
	uint32_t __xfrq_base_addr_lo /* The low page address which the XFRQ resides in host memory */;
	uint32_t __xfrq_base_addr_hi /* The high page address which the XFRQ resides in host memory */;
#if defined(__BIG_ENDIAN)
	uint16_t __xfrq_cons /* The XFRQ consumer */;
	uint16_t __xfrq_prod /* The XFRQ producer, updated by Ustorm */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __xfrq_prod /* The XFRQ producer, updated by Ustorm */;
	uint16_t __xfrq_cons /* The XFRQ consumer */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __reserved_force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __reserved_force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
#endif
	uint32_t __tcp_agg_vars6 /* Various aggregative variables*/;
#if defined(__BIG_ENDIAN)
	uint16_t __xfrqe_mng /* Misc aggregated variable 6 */;
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
	uint16_t __xfrqe_mng /* Misc aggregated variable 6 */;
#endif
	uint32_t __xfrqe_data0 /* aggregated value 10 */;
	uint32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	uint16_t __reserved3;
	uint8_t __reserved2;
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
	uint8_t __reserved2;
	uint16_t __reserved3;
#endif
};

/*
 * The fcoe aggregative context of Xstorm
 */
struct xstorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t agg_val1 /* aggregated value 1 */;
	uint8_t agg_vars1;
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                       (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                 1
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51                                          (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT                                    2
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52                                          (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN                                     (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                               4
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN                                              (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT                                        5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG                                       (0x1<<6) /* BitField agg_vars1Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                 6
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN                              (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT                        7
	uint8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __state /* The state of the connection */;
	uint8_t agg_vars1;
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                       (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                 1
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51                                          (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT                                    2
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52                                          (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN                                     (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                               4
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN                                              (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT                                        5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG                                       (0x1<<6) /* BitField agg_vars1Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                 6
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN                              (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT                        7
	uint16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t agg_vars3;
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                   (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                             0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF                                            (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT                                      6
	uint8_t agg_vars2;
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF                                               (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT                                         0
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN                                    (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                              2
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG                                           (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                     3
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG                                           (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1                                        (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT                                  5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_vars2;
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF                                               (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT                                         0
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN                                    (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                              2
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG                                           (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                     3
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG                                           (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1                                        (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT                                  5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
	uint8_t agg_vars3;
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                   (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                             0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF                                            (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT                                      6
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	uint32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	uint16_t agg_vars5;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5                                        (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT                                  0
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                   (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                             2
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                   (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                             8
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE                                      (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT                                14
	uint16_t sq_cons /* The SQ consumer updated by Xstorm after consuming aother WQE */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_cons /* The SQ consumer updated by Xstorm after consuming aother WQE */;
	uint16_t agg_vars5;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5                                        (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT                                  0
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                   (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                             2
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                   (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                             8
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE                                      (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT                                14
#endif
	struct xstorm_fcoe_extra_ag_context_section __extra_section /* Extra context section */;
#if defined(__BIG_ENDIAN)
	uint16_t agg_vars7;
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE                             (0x7<<0) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                       0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG                                          (0x1<<3) /* BitField agg_vars7Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF                                           (0x3<<4) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 18 */
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3                                        (0x3<<6) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT                                  6
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF                                               (0x3<<8) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT                                         8
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62                                          (0x1<<10) /* BitField agg_vars7Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT                                    10
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<11) /* BitField agg_vars7Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    11
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG                                          (0x1<<12) /* BitField agg_vars7Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT                                    12
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG                                          (0x1<<13) /* BitField agg_vars7Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT                                    13
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG                                          (0x1<<14) /* BitField agg_vars7Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT                                    14
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG                                           (0x1<<15) /* BitField agg_vars7Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT                                     15
	uint8_t agg_val3_th /* Aggregated value 3 - threshold */;
	uint8_t agg_vars6;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6                                        (0x7<<0) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT                                  0
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE                                       (0x7<<3) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT                                 3
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE                                         (0x3<<6) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT                                   6
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_vars6;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6                                        (0x7<<0) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT                                  0
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE                                       (0x7<<3) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT                                 3
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE                                         (0x3<<6) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT                                   6
	uint8_t agg_val3_th /* Aggregated value 3 - threshold */;
	uint16_t agg_vars7;
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE                             (0x7<<0) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                       0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG                                          (0x1<<3) /* BitField agg_vars7Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF                                           (0x3<<4) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 18 */
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3                                        (0x3<<6) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT                                  6
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF                                               (0x3<<8) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT                                         8
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62                                          (0x1<<10) /* BitField agg_vars7Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT                                    10
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<11) /* BitField agg_vars7Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    11
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG                                          (0x1<<12) /* BitField agg_vars7Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT                                    12
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG                                          (0x1<<13) /* BitField agg_vars7Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT                                    13
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG                                          (0x1<<14) /* BitField agg_vars7Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT                                    14
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG                                           (0x1<<15) /* BitField agg_vars7Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT                                     15
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
	uint16_t __agg_val11 /* aggregated value 11 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val11 /* aggregated value 11 */;
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __reserved1;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val9 /* aggregated value 9 */;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t confq_cons /* CONFQ Consumer */;
	uint16_t confq_prod /* CONFQ Producer, updated by Ustorm - AggVal2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t confq_prod /* CONFQ Producer, updated by Ustorm - AggVal2 */;
	uint16_t confq_cons /* CONFQ Consumer */;
#endif
	uint32_t agg_varint8_t;
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC2                                             (0xFFFFFF<<0) /* BitField agg_varint8_tVarious aggregative variables	Misc aggregated variable 2 */
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC2_SHIFT                                       0
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3                                             (0xFF<<24) /* BitField agg_varint8_tVarious aggregative variables	Misc aggregated variable 3 */
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3_SHIFT                                       24
#if defined(__BIG_ENDIAN)
	uint16_t __cache_wqe_db /* Misc aggregated variable 0 */;
	uint16_t sq_prod /* The SQ Producer updated by Xstorm after reading a bunch of WQEs into the context */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_prod /* The SQ Producer updated by Xstorm after reading a bunch of WQEs into the context */;
	uint16_t __cache_wqe_db /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t agg_val3 /* Aggregated value 3 */;
	uint8_t agg_val6 /* Aggregated value 6 */;
	uint8_t agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_val5 /* Aggregated value 5 */;
	uint8_t agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t agg_val6 /* Aggregated value 6 */;
	uint8_t agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	uint16_t agg_limit1 /* aggregated limit 1 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_limit1 /* aggregated limit 1 */;
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	uint32_t completion_seq /* The sequence number of the start completion point (BD) */;
	uint32_t confq_pbl_base_lo /* The CONFQ PBL base low address resides in host memory */;
	uint32_t confq_pbl_base_hi /* The CONFQ PBL base hihj address resides in host memory */;
};


/*
 * The tcp aggregative context section of Xstorm
 */
struct xstorm_tcp_tcp_ag_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t tcp_agg_vars1;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
	uint8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint16_t mss /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t mss /* MSS used for nagle algorithm and for transmission */;
	uint8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint8_t tcp_agg_vars1;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
#endif
	uint32_t snd_nxt /* The current sequence number to send */;
	uint32_t tx_wnd /* The Current transmission window in bytes */;
	uint32_t snd_una /* The current Send UNA sequence number */;
	uint32_t local_adv_wnd /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
	uint8_t __tx_dest /* aggregated value 8 */;
	uint16_t tcp_agg_vars2;
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
#elif defined(__LITTLE_ENDIAN)
	uint16_t tcp_agg_vars2;
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
	uint8_t __tx_dest /* aggregated value 8 */;
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	uint32_t ack_to_far_end /* The ACK sequence to send to far end */;
	uint32_t rto_timer /* The RTO timer value */;
	uint32_t ka_timer /* The KA timer value */;
	uint32_t ts_to_echo /* The time stamp value to echo to far end */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val7_th /* aggregated value 7 - threshold */;
	uint16_t __agg_val7 /* aggregated value 7 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val7 /* aggregated value 7 */;
	uint16_t __agg_val7_th /* aggregated value 7 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
#endif
	uint32_t tcp_agg_vars6;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN                         (0x1<<0) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux7_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN_SHIFT                   0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN                    (0x1<<1) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux8_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN_SHIFT              1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN                               (0x1<<2) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux9_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN_SHIFT                         2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<3) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux10_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG                                (0x1<<4) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary flag 6 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG_SHIFT                          4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG                                (0x1<<5) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary flag 7 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG_SHIFT                          5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF                                  (0x3<<6) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 5 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF_SHIFT                            6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF                                  (0x3<<8) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 9 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_SHIFT                            8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF                                 (0x3<<10) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 10 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_SHIFT                           10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF                                 (0x3<<12) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 11 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_SHIFT                           12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF                                 (0x3<<14) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 12 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_SHIFT                           14
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF                                 (0x3<<16) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 13 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF_SHIFT                           16
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF                                 (0x3<<18) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 14 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF_SHIFT                           18
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF                                 (0x3<<20) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 15 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF_SHIFT                           20
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF                                 (0x3<<22) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 16 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF_SHIFT                           22
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF                                 (0x3<<24) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 17 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF_SHIFT                           24
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG                                   (0x1<<26) /* BitField tcp_agg_vars6Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG_SHIFT                             26
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71                               (0x1<<27) /* BitField tcp_agg_vars6Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71_SHIFT                         27
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY                 (0x1<<28) /* BitField tcp_agg_vars6Various aggregative variables	This flag is set if the Force ACK count is set by the TSTORM. On QM output it is cleared. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY_SHIFT           28
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG                       (0x1<<29) /* BitField tcp_agg_vars6Various aggregative variables	Indicates that the connection is in autostop mode */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG_SHIFT                 29
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG                        (0x1<<30) /* BitField tcp_agg_vars6Various aggregative variables	This bit uses like a one shot that the TSTORM fires and the XSTORM arms. Used to allow a single TS update for each transmission */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG_SHIFT                  30
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG                   (0x1<<31) /* BitField tcp_agg_vars6Various aggregative variables	This bit is set by the TSTORM when need to cancel precious fast retransmit */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG_SHIFT             31
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc6 /* Misc aggregated variable 6 */;
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
	uint16_t __agg_misc6 /* Misc aggregated variable 6 */;
#endif
	uint32_t __agg_val10 /* aggregated value 10 */;
	uint32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	uint16_t __reserved3;
	uint8_t __reserved2;
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
	uint8_t __reserved2;
	uint16_t __reserved3;
#endif
};

/*
 * The iscsi aggregative context of Xstorm
 */
struct xstorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t agg_val1 /* aggregated value 1 */;
	uint8_t agg_vars1;
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN                                    (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                              4
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN                                             (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT                                       5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG                                      (0x1<<6) /* BitField agg_vars1Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                6
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN                                      (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                7
	uint8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* The state of the connection */;
	uint8_t agg_vars1;
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN                                    (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                              4
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN                                             (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT                                       5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG                                      (0x1<<6) /* BitField agg_vars1Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                6
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN                                      (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                7
	uint16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t agg_vars3;
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                  (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                            0
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF                                        (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT                                  6
	uint8_t agg_vars2;
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF                                              (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT                                        0
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN                                   (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                             2
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG                                          (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT                                    3
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG                                          (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT                                    4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1                                       (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT                                 5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_vars2;
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF                                              (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT                                        0
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN                                   (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                             2
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG                                          (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT                                    3
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG                                          (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT                                    4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1                                       (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT                                 5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
	uint8_t agg_vars3;
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                  (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                            0
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF                                        (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT                                  6
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	uint32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	uint16_t agg_vars5;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5                                       (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                  (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                            2
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                  (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                            8
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2                                       (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT                                 14
	uint16_t sq_cons /* aggregated value 4 - threshold */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_cons /* aggregated value 4 - threshold */;
	uint16_t agg_vars5;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5                                       (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                  (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                            2
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                  (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                            8
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2                                       (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT                                 14
#endif
	struct xstorm_tcp_tcp_ag_context_section tcp /* TCP context section, shared in TOE and ISCSI */;
#if defined(__BIG_ENDIAN)
	uint16_t agg_vars7;
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE                            (0x7<<0) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                      0
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG                                         (0x1<<3) /* BitField agg_vars7Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT                                   3
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF                                     (0x3<<4) /* BitField agg_vars7Various aggregative variables	Sync Tstorm and Xstorm */
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT                               4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3                                       (0x3<<6) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT                                 6
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF                                              (0x3<<8) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT                                        8
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK                       (0x1<<10) /* BitField agg_vars7Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT                 10
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<11) /* BitField agg_vars7Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   11
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG                                         (0x1<<12) /* BitField agg_vars7Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT                                   12
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG                                         (0x1<<13) /* BitField agg_vars7Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT                                   13
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG                                         (0x1<<14) /* BitField agg_vars7Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT                                   14
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN                                      (0x1<<15) /* BitField agg_vars7Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT                                15
	uint8_t agg_val3_th /* Aggregated value 3 - threshold */;
	uint8_t agg_vars6;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6                                       (0x7<<0) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7                                       (0x7<<3) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT                                 3
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4                                       (0x3<<6) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT                                 6
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_vars6;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6                                       (0x7<<0) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7                                       (0x7<<3) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT                                 3
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4                                       (0x3<<6) /* BitField agg_vars6Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT                                 6
	uint8_t agg_val3_th /* Aggregated value 3 - threshold */;
	uint16_t agg_vars7;
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE                            (0x7<<0) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                      0
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG                                         (0x1<<3) /* BitField agg_vars7Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT                                   3
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF                                     (0x3<<4) /* BitField agg_vars7Various aggregative variables	Sync Tstorm and Xstorm */
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT                               4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3                                       (0x3<<6) /* BitField agg_vars7Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT                                 6
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF                                              (0x3<<8) /* BitField agg_vars7Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT                                        8
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK                       (0x1<<10) /* BitField agg_vars7Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT                 10
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<11) /* BitField agg_vars7Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   11
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG                                         (0x1<<12) /* BitField agg_vars7Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT                                   12
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG                                         (0x1<<13) /* BitField agg_vars7Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT                                   13
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG                                         (0x1<<14) /* BitField agg_vars7Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT                                   14
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN                                      (0x1<<15) /* BitField agg_vars7Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT                                15
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
	uint16_t __gen_data /* Used for Iscsi. In connection establishment, it uses as rxMss, and in connection termination, it uses as command Id: 1=L5CM_TX_ACK_ON_FIN_CMD 2=L5CM_SET_MSL_TIMER_CMD 3=L5CM_TX_RST_CMD */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __gen_data /* Used for Iscsi. In connection establishment, it uses as rxMss, and in connection termination, it uses as command Id: 1=L5CM_TX_ACK_ON_FIN_CMD 2=L5CM_SET_MSL_TIMER_CMD 3=L5CM_TX_RST_CMD */;
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __reserved1;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val9 /* aggregated value 9 */;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t hq_prod /* The HQ producer threashold to compare the HQ consumer, which is the current HQ producer +1 - AggVal2Th */;
	uint16_t hq_cons /* HQ Consumer, updated by Cstorm - AggVal2 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hq_cons /* HQ Consumer, updated by Cstorm - AggVal2 */;
	uint16_t hq_prod /* The HQ producer threashold to compare the HQ consumer, which is the current HQ producer +1 - AggVal2Th */;
#endif
	uint32_t agg_varint8_t;
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2                                            (0xFFFFFF<<0) /* BitField agg_varint8_tVarious aggregative variables	Misc aggregated variable 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2_SHIFT                                      0
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3                                            (0xFF<<24) /* BitField agg_varint8_tVarious aggregative variables	Misc aggregated variable 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3_SHIFT                                      24
#if defined(__BIG_ENDIAN)
	uint16_t r2tq_prod /* Misc aggregated variable 0 */;
	uint16_t sq_prod /* SQ Producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_prod /* SQ Producer */;
	uint16_t r2tq_prod /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t agg_val3 /* Aggregated value 3 */;
	uint8_t agg_val6 /* Aggregated value 6 */;
	uint8_t agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_val5 /* Aggregated value 5 */;
	uint8_t agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t agg_val6 /* Aggregated value 6 */;
	uint8_t agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	uint16_t agg_limit1 /* aggregated limit 1 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t agg_limit1 /* aggregated limit 1 */;
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	uint32_t hq_cons_tcp_seq /* TCP sequence of the HQ BD pointed by hq_cons */;
	uint32_t exp_stat_sn /* expected status SN, updated by Ustorm */;
	uint32_t rst_seq_num /* spare aggregated variable 5 */;
};


/*
 * The toe aggregative context section of Xstorm
 */
struct xstorm_toe_tcp_ag_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t tcp_agg_vars1;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
	uint8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint16_t mss /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t mss /* MSS used for nagle algorithm and for transmission */;
	uint8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	uint8_t tcp_agg_vars1;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
#endif
	uint32_t snd_nxt /* The current sequence number to send */;
	uint32_t tx_wnd /* The Current transmission window in bytes */;
	uint32_t snd_una /* The current Send UNA sequence number */;
	uint32_t local_adv_wnd /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
	uint8_t __tx_dest /* aggregated value 8 */;
	uint16_t tcp_agg_vars2;
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
#elif defined(__LITTLE_ENDIAN)
	uint16_t tcp_agg_vars2;
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
	uint8_t __tx_dest /* aggregated value 8 */;
	uint8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	uint32_t ack_to_far_end /* The ACK sequence to send to far end */;
	uint32_t rto_timer /* The RTO timer value */;
	uint32_t ka_timer /* The KA timer value */;
	uint32_t ts_to_echo /* The time stamp value to echo to far end */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val7_th /* aggregated value 7 - threshold */;
	uint16_t __agg_val7 /* aggregated value 7 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val7 /* aggregated value 7 */;
	uint16_t __agg_val7_th /* aggregated value 7 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	uint8_t __tcp_agg_vars3 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars4 /* Various aggregative variables*/;
	uint8_t __tcp_agg_vars5 /* Various aggregative variables*/;
#endif
	uint32_t tcp_agg_vars6;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN                         (0x1<<0) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux7_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN_SHIFT                   0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN                    (0x1<<1) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux8_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN_SHIFT              1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN                               (0x1<<2) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux9_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN_SHIFT                         2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<3) /* BitField tcp_agg_vars6Various aggregative variables	Enable decision rules based on aux10_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX6_FLAG                                (0x1<<4) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary flag 6 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX6_FLAG_SHIFT                          4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX7_FLAG                                (0x1<<5) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary flag 7 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX7_FLAG_SHIFT                          5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX5_CF                                  (0x3<<6) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 5 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX5_CF_SHIFT                            6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF                                  (0x3<<8) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 9 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_SHIFT                            8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF                                 (0x3<<10) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 10 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_SHIFT                           10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF                                 (0x3<<12) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 11 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_SHIFT                           12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF                                 (0x3<<14) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 12 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_SHIFT                           14
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX13_CF                                 (0x3<<16) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 13 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX13_CF_SHIFT                           16
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX14_CF                                 (0x3<<18) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 14 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX14_CF_SHIFT                           18
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX15_CF                                 (0x3<<20) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 15 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX15_CF_SHIFT                           20
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX16_CF                                 (0x3<<22) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 16 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX16_CF_SHIFT                           22
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX17_CF                                 (0x3<<24) /* BitField tcp_agg_vars6Various aggregative variables	auxiliary counter flag 17 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX17_CF_SHIFT                           24
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ECE_FLAG                                   (0x1<<26) /* BitField tcp_agg_vars6Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ECE_FLAG_SHIFT                             26
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED71                               (0x1<<27) /* BitField tcp_agg_vars6Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED71_SHIFT                         27
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY                 (0x1<<28) /* BitField tcp_agg_vars6Various aggregative variables	This flag is set if the Force ACK count is set by the TSTORM. On QM output it is cleared. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY_SHIFT           28
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG                       (0x1<<29) /* BitField tcp_agg_vars6Various aggregative variables	Indicates that the connection is in autostop mode */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG_SHIFT                 29
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG                        (0x1<<30) /* BitField tcp_agg_vars6Various aggregative variables	This bit uses like a one shot that the TSTORM fires and the XSTORM arms. Used to allow a single TS update for each transmission */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG_SHIFT                  30
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG                   (0x1<<31) /* BitField tcp_agg_vars6Various aggregative variables	This bit is set by the TSTORM when need to cancel precious fast retransmit */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG_SHIFT             31
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc6 /* Misc aggregated variable 6 */;
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __tcp_agg_vars7 /* Various aggregative variables*/;
	uint16_t __agg_misc6 /* Misc aggregated variable 6 */;
#endif
	uint32_t __agg_val10 /* aggregated value 10 */;
	uint32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	uint16_t __reserved3;
	uint8_t __reserved2;
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __da_only_cnt /* counts delayed acks and not window updates */;
	uint8_t __reserved2;
	uint16_t __reserved3;
#endif
};

/*
 * The toe aggregative context of Xstorm
 */
struct xstorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	uint16_t agg_val1 /* aggregated value 1 */;
	uint8_t agg_vars1;
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50                                           (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50_SHIFT                                     1
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51                                           (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                     2
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52                                           (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                     3
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN                                      (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                                4
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN                                               (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN_SHIFT                                         5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG                                        (0x1<<6) /* BitField agg_vars1Various aggregative variables	used to indicate last doorbell for specific connection */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_SHIFT                                  6
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN                                        (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                  7
	uint8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __state /* The state of the connection */;
	uint8_t agg_vars1;
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50                                           (0x1<<1) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50_SHIFT                                     1
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51                                           (0x1<<2) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                     2
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52                                           (0x1<<3) /* BitField agg_vars1Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                     3
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN                                      (0x1<<4) /* BitField agg_vars1Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                                4
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN                                               (0x1<<5) /* BitField agg_vars1Various aggregative variables	Enables the nagle decision */
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN_SHIFT                                         5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG                                        (0x1<<6) /* BitField agg_vars1Various aggregative variables	used to indicate last doorbell for specific connection */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_SHIFT                                  6
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN                                        (0x1<<7) /* BitField agg_vars1Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                  7
	uint16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t agg_vars3;
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                    (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                              0
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF                                   (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF_SHIFT                             6
	uint8_t agg_vars2;
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF                                                (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_SHIFT                                          0
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN                                     (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN_SHIFT                               2
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG                                            (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                      3
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG                                            (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                      4
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN                                             (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                       7
#elif defined(__LITTLE_ENDIAN)
	uint8_t agg_vars2;
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF                                                (0x3<<0) /* BitField agg_vars2Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_SHIFT                                          0
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN                                     (0x1<<2) /* BitField agg_vars2Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN_SHIFT                               2
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG                                            (0x1<<3) /* BitField agg_vars2Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                      3
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG                                            (0x1<<4) /* BitField agg_vars2Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                      4
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x3<<5) /* BitField agg_vars2Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN                                             (0x1<<7) /* BitField agg_vars2Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                       7
	uint8_t agg_vars3;
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                    (0x3F<<0) /* BitField agg_vars3Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                              0
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF                                   (0x3<<6) /* BitField agg_vars3Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF_SHIFT                             6
	uint8_t __agg_vars4 /* Various aggregative variables*/;
	uint8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	uint32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	uint16_t agg_vars5;
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54                                           (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54_SHIFT                                     0
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                    (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                              2
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                    (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                              8
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56                                           (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56_SHIFT                                     14
	uint16_t __agg_val4_th /* aggregated value 4 - threshold */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val4_th /* aggregated value 4 - threshold */;
	uint16_t agg_vars5;
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54                                           (0x3<<0) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54_SHIFT                                     0
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                    (0x3F<<2) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                              2
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                    (0x3F<<8) /* BitField agg_vars5Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                              8
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56                                           (0x3<<14) /* BitField agg_vars5Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56_SHIFT                                     14
#endif
	struct xstorm_toe_tcp_ag_context_section tcp /* TCP context section, shared in TOE and ISCSI */;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_vars7 /* Various aggregative variables*/;
	uint8_t __agg_val3_th /* Aggregated value 3 - threshold */;
	uint8_t __agg_vars6 /* Various aggregative variables*/;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __agg_vars6 /* Various aggregative variables*/;
	uint8_t __agg_val3_th /* Aggregated value 3 - threshold */;
	uint16_t __agg_vars7 /* Various aggregative variables*/;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
	uint16_t __agg_val11 /* aggregated value 11 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val11 /* aggregated value 11 */;
	uint16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __reserved1;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val9 /* aggregated value 9 */;
	uint8_t __agg_val6_th /* aggregated value 6 - threshold */;
	uint8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_val2_th /* Aggregated value 2 - threshold */;
	uint16_t cmp_bd_cons /* BD Consumer from the Completor */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cmp_bd_cons /* BD Consumer from the Completor */;
	uint16_t __agg_val2_th /* Aggregated value 2 - threshold */;
#endif
	uint32_t __agg_varint8_t /* Various aggregative variables*/;
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc0 /* Misc aggregated variable 0 */;
	uint16_t __agg_val4 /* aggregated value 4 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __agg_val4 /* aggregated value 4 */;
	uint16_t __agg_misc0 /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __agg_val3 /* Aggregated value 3 */;
	uint8_t __agg_val6 /* Aggregated value 6 */;
	uint8_t __agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t __agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __agg_val5 /* Aggregated value 5 */;
	uint8_t __agg_val5_th /* Aggregated value 5 - threshold */;
	uint8_t __agg_val6 /* Aggregated value 6 */;
	uint8_t __agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	uint16_t __bd_ind_max_val /* modulo value for bd_prod */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __bd_ind_max_val /* modulo value for bd_prod */;
	uint16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	uint32_t cmp_bd_start_seq /* The sequence number of the start completion point (BD) */;
	uint32_t cmp_bd_page_0_to_31 /* Misc aggregated variable 4 */;
	uint32_t cmp_bd_page_32_to_63 /* spare aggregated variable 5 */;
};


/*
 * doorbell message sent to the chip
 */
struct doorbell
{
#if defined(__BIG_ENDIAN)
	uint16_t zero_fill2 /* driver must zero this field! */;
	uint8_t zero_fill1 /* driver must zero this field! */;
	struct doorbell_hdr_t header;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t header;
	uint8_t zero_fill1 /* driver must zero this field! */;
	uint16_t zero_fill2 /* driver must zero this field! */;
#endif
};


/*
 * doorbell message sent to the chip
 */
struct doorbell_set_prod
{
#if defined(__BIG_ENDIAN)
	uint16_t prod /* Producer index to be set */;
	uint8_t zero_fill1 /* driver must zero this field! */;
	struct doorbell_hdr_t header;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t header;
	uint8_t zero_fill1 /* driver must zero this field! */;
	uint16_t prod /* Producer index to be set */;
#endif
};


struct regpair_native_t
{
	uint32_t lo /* low word for reg-pair */;
	uint32_t hi /* high word for reg-pair */;
};


struct regpair_t
{
	uint32_t lo /* low word for reg-pair */;
	uint32_t hi /* high word for reg-pair */;
};


/*
 * Classify rule opcodes in E2/E3
 */
enum classify_rule
{
	CLASSIFY_RULE_OPCODE_MAC /* Add/remove a MAC address */,
	CLASSIFY_RULE_OPCODE_VLAN /* Add/remove a VLAN */,
	CLASSIFY_RULE_OPCODE_PAIR /* Add/remove a MAC-VLAN pair */,
	CLASSIFY_RULE_OPCODE_IMAC_VNI /* Add/remove an Inner MAC-VNI pair entry */,
	MAX_CLASSIFY_RULE};


/*
 * Classify rule types in E2/E3
 */
enum classify_rule_action_type
{
	CLASSIFY_RULE_REMOVE,
	CLASSIFY_RULE_ADD,
	MAX_CLASSIFY_RULE_ACTION_TYPE};


/*
 * client init ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_init_general_data
{
	uint8_t client_id /* client_id */;
	uint8_t statistics_counter_id /* statistics counter id */;
	uint8_t statistics_en_flg /* statistics en flg */;
	uint8_t is_fcoe_flg /* is this an fcoe connection. (1 bit is used) */;
	uint8_t activate_flg /* if 0 - the client is deactivate else the client is activate client (1 bit is used) */;
	uint8_t sp_client_id /* the slow path rings client Id. */;
	uint16_t mtu /* Host MTU from client config */;
	uint8_t statistics_zero_flg /* if set FW will reset the statistic counter of this client */;
	uint8_t func_id /* PCI function ID (0-71) */;
	uint8_t cos /* The connection cos, if applicable */;
	uint8_t traffic_type;
	uint8_t fp_hsi_ver /* Hsi version */;
	uint8_t reserved0[3];
};


/*
 * client init rx data $$KEEP_ENDIANNESS$$
 */
struct client_init_rx_data
{
	uint8_t tpa_en;
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV4                                              (0x1<<0) /* BitField tpa_entpa_enable	tpa enable flg ipv4 */
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV4_SHIFT                                        0
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV6                                              (0x1<<1) /* BitField tpa_entpa_enable	tpa enable flg ipv6 */
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV6_SHIFT                                        1
		#define CLIENT_INIT_RX_DATA_TPA_MODE                                                 (0x1<<2) /* BitField tpa_entpa_enable	tpa mode (LRO or GRO) (use enum tpa_mode) */
		#define CLIENT_INIT_RX_DATA_TPA_MODE_SHIFT                                           2
		#define CLIENT_INIT_RX_DATA_RESERVED5                                                (0x1F<<3) /* BitField tpa_entpa_enable	 */
		#define CLIENT_INIT_RX_DATA_RESERVED5_SHIFT                                          3
	uint8_t vmqueue_mode_en_flg /* If set, working in VMQueue mode (always consume one sge) */;
	uint8_t extra_data_over_sgl_en_flg /* if set, put over sgl data from end of input message */;
	uint8_t cache_line_alignment_log_size /* The log size of cache line alignment in bytes. Must be a power of 2. */;
	uint8_t enable_dynamic_hc /* If set, dynamic HC is enabled */;
	uint8_t max_sges_for_packet /* The maximal number of SGEs that can be used for one packet. depends on MTU and SGE size. must be 0 if SGEs are disabled */;
	uint8_t client_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this client rx producers */;
	uint8_t drop_ip_cs_err_flg /* If set, this client drops packets with IP checksum error */;
	uint8_t drop_tcp_cs_err_flg /* If set, this client drops packets with TCP checksum error */;
	uint8_t drop_ttl0_flg /* If set, this client drops packets with TTL=0 */;
	uint8_t drop_udp_cs_err_flg /* If set, this client drops packets with UDP checksum error */;
	uint8_t inner_vlan_removal_enable_flg /* If set, inner VLAN removal is enabled for this client */;
	uint8_t outer_vlan_removal_enable_flg /* If set, outer VLAN removal is enabled for this client */;
	uint8_t status_block_id /* rx status block id */;
	uint8_t rx_sb_index_number /* status block indices */;
	uint8_t dont_verify_rings_pause_thr_flg /* If set, the rings pause thresholds will not be verified by firmware. */;
	uint8_t max_tpa_queues /* maximal TPA queues allowed for this client */;
	uint8_t silent_vlan_removal_flg /* if set, and the vlan is equal to requested vlan according to mask, the vlan will be remove without notifying the driver */;
	uint16_t max_bytes_on_bd /* Maximum bytes that can be placed on a BD. The BD allocated size should include 2 more bytes (ip alignment) and alignment size (in case the address is not aligned) */;
	uint16_t sge_buff_size /* Size of the buffers pointed by SGEs */;
	uint8_t approx_mcast_engine_id /* In Everest2, if is_approx_mcast is set, this field specified which approximate multicast engine is associate with this client */;
	uint8_t rss_engine_id /* In Everest2, if rss_mode is set, this field specified which RSS engine is associate with this client */;
	struct regpair_t bd_page_base /* BD page base address at the host */;
	struct regpair_t sge_page_base /* SGE page base address at the host */;
	struct regpair_t cqe_page_base /* Completion queue base address */;
	uint8_t is_leading_rss;
	uint8_t is_approx_mcast;
	uint16_t max_agg_size /* maximal size for the aggregated TPA packets, reprted by the host */;
	uint16_t state;
		#define CLIENT_INIT_RX_DATA_UCAST_DROP_ALL                                           (0x1<<0) /* BitField staterx filters state	drop all unicast packets */
		#define CLIENT_INIT_RX_DATA_UCAST_DROP_ALL_SHIFT                                     0
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_ALL                                         (0x1<<1) /* BitField staterx filters state	accept all unicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_ALL_SHIFT                                   1
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_UNMATCHED                                   (0x1<<2) /* BitField staterx filters state	accept all unmatched unicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_UNMATCHED_SHIFT                             2
		#define CLIENT_INIT_RX_DATA_MCAST_DROP_ALL                                           (0x1<<3) /* BitField staterx filters state	drop all multicast packets */
		#define CLIENT_INIT_RX_DATA_MCAST_DROP_ALL_SHIFT                                     3
		#define CLIENT_INIT_RX_DATA_MCAST_ACCEPT_ALL                                         (0x1<<4) /* BitField staterx filters state	accept all multicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_MCAST_ACCEPT_ALL_SHIFT                                   4
		#define CLIENT_INIT_RX_DATA_BCAST_ACCEPT_ALL                                         (0x1<<5) /* BitField staterx filters state	accept all broadcast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_BCAST_ACCEPT_ALL_SHIFT                                   5
		#define CLIENT_INIT_RX_DATA_ACCEPT_ANY_VLAN                                          (0x1<<6) /* BitField staterx filters state	accept packets matched only by MAC (without checking vlan) */
		#define CLIENT_INIT_RX_DATA_ACCEPT_ANY_VLAN_SHIFT                                    6
		#define CLIENT_INIT_RX_DATA_RESERVED2                                                (0x1FF<<7) /* BitField staterx filters state	 */
		#define CLIENT_INIT_RX_DATA_RESERVED2_SHIFT                                          7
	uint16_t cqe_pause_thr_low /* number of remaining cqes under which, we send pause message */;
	uint16_t cqe_pause_thr_high /* number of remaining cqes above which, we send un-pause message */;
	uint16_t bd_pause_thr_low /* number of remaining bds under which, we send pause message */;
	uint16_t bd_pause_thr_high /* number of remaining bds above which, we send un-pause message */;
	uint16_t sge_pause_thr_low /* number of remaining sges under which, we send pause message */;
	uint16_t sge_pause_thr_high /* number of remaining sges above which, we send un-pause message */;
	uint16_t rx_cos_mask /* the bits that will be set on pfc/ safc paket whith will be genratet when this ring is full. for regular flow control set this to 1 */;
	uint16_t silent_vlan_value /* The vlan to compare, in case, silent vlan is set */;
	uint16_t silent_vlan_mask /* The vlan mask, in case, silent vlan is set */;
	uint8_t handle_ptp_pkts_flg /* If set, this client handles PTP Packets */;
	uint8_t reserved6[3];
	uint32_t reserved7;
};

/*
 * client init tx data $$KEEP_ENDIANNESS$$
 */
struct client_init_tx_data
{
	uint8_t enforce_security_flg /* if set, security checks will be made for this connection */;
	uint8_t tx_status_block_id /* the number of status block to update */;
	uint8_t tx_sb_index_number /* the index to use inside the status block */;
	uint8_t tss_leading_client_id /* client ID of the leading TSS client, for TX classification source knock out */;
	uint8_t tx_switching_flg /* if set, tx switching will be done to packets on this connection */;
	uint8_t anti_spoofing_flg /* if set, anti spoofing check will be done to packets on this connection */;
	uint16_t default_vlan /* default vlan tag (id+pri). (valid if default_vlan_flg is set) */;
	struct regpair_t tx_bd_page_base /* BD page base address at the host for TxBdCons */;
	uint16_t state;
		#define CLIENT_INIT_TX_DATA_UCAST_ACCEPT_ALL                                         (0x1<<0) /* BitField statetx filters state	accept all unicast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_UCAST_ACCEPT_ALL_SHIFT                                   0
		#define CLIENT_INIT_TX_DATA_MCAST_ACCEPT_ALL                                         (0x1<<1) /* BitField statetx filters state	accept all multicast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_MCAST_ACCEPT_ALL_SHIFT                                   1
		#define CLIENT_INIT_TX_DATA_BCAST_ACCEPT_ALL                                         (0x1<<2) /* BitField statetx filters state	accept all broadcast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_BCAST_ACCEPT_ALL_SHIFT                                   2
		#define CLIENT_INIT_TX_DATA_ACCEPT_ANY_VLAN                                          (0x1<<3) /* BitField statetx filters state	accept packets matched only by MAC (without checking vlan) */
		#define CLIENT_INIT_TX_DATA_ACCEPT_ANY_VLAN_SHIFT                                    3
		#define CLIENT_INIT_TX_DATA_RESERVED0                                                (0xFFF<<4) /* BitField statetx filters state	 */
		#define CLIENT_INIT_TX_DATA_RESERVED0_SHIFT                                          4
	uint8_t default_vlan_flg /* is default vlan valid for this client. */;
	uint8_t force_default_pri_flg /* if set, force default priority */;
	uint8_t tunnel_lso_inc_ip_id /* In case of LSO over IPv4 tunnel, whether to increment IP ID on external IP header or internal IP header */;
	uint8_t refuse_outband_vlan_flg /* if set, the FW will not add outband vlan on packet (even if will exist on BD). */;
	uint8_t tunnel_non_lso_pcsum_location /* In case of non-Lso encapsulated packets with L4 checksum offload, the pseudo checksum location - on packet or on BD. */;
	uint8_t tunnel_non_lso_outer_ip_csum_location /* In case of non-Lso encapsulated packets with outer L3 ip checksum offload, the pseudo checksum location - on packet or on BD. */;
};

/*
 * client init ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_init_ramrod_data
{
	struct client_init_general_data general /* client init general data */;
	struct client_init_rx_data rx /* client init rx data */;
	struct client_init_tx_data tx /* client init tx data */;
};


/*
 * client update ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_update_ramrod_data
{
	uint8_t client_id /* the client to update */;
	uint8_t func_id /* PCI function ID this client belongs to (0-71) */;
	uint8_t inner_vlan_removal_enable_flg /* If set, inner VLAN removal is enabled for this client, will be change according to change flag */;
	uint8_t inner_vlan_removal_change_flg /* If set, inner VLAN removal flag will be set according to the enable flag */;
	uint8_t outer_vlan_removal_enable_flg /* If set, outer VLAN removal is enabled for this client, will be change according to change flag */;
	uint8_t outer_vlan_removal_change_flg /* If set, outer VLAN removal flag will be set according to the enable flag */;
	uint8_t anti_spoofing_enable_flg /* If set, anti spoofing is enabled for this client, will be change according to change flag */;
	uint8_t anti_spoofing_change_flg /* If set, anti spoofing flag will be set according to anti spoofing flag */;
	uint8_t activate_flg /* if 0 - the client is deactivate else the client is activate client (1 bit is used) */;
	uint8_t activate_change_flg /* If set, activate_flg will be checked */;
	uint16_t default_vlan /* default vlan tag (id+pri). (valid if default_vlan_flg is set) */;
	uint8_t default_vlan_enable_flg;
	uint8_t default_vlan_change_flg;
	uint16_t silent_vlan_value /* The vlan to compare, in case, silent vlan is set */;
	uint16_t silent_vlan_mask /* The vlan mask, in case, silent vlan is set */;
	uint8_t silent_vlan_removal_flg /* if set, and the vlan is equal to requested vlan according to mask, the vlan will be remove without notifying the driver */;
	uint8_t silent_vlan_change_flg;
	uint8_t refuse_outband_vlan_flg /* If set, the FW will not add outband vlan on packet (even if will exist on BD). */;
	uint8_t refuse_outband_vlan_change_flg /* If set, refuse_outband_vlan_flg will be updated. */;
	uint8_t tx_switching_flg /* If set, tx switching will be done to packets on this connection. */;
	uint8_t tx_switching_change_flg /* If set, tx_switching_flg will be updated. */;
	uint8_t handle_ptp_pkts_flg /* If set, this client handles PTP Packets */;
	uint8_t handle_ptp_pkts_change_flg /* If set, handle_ptp_pkts_flg will be updated. */;
	uint16_t reserved1;
	uint32_t echo /* echo value to be sent to driver on event ring */;
};


/*
 * The eth storm context of Cstorm
 */
struct cstorm_eth_st_context
{
	uint32_t __reserved0[4];
};


struct double_regpair
{
	uint32_t regpair0_lo /* low word for reg-pair0 */;
	uint32_t regpair0_hi /* high word for reg-pair0 */;
	uint32_t regpair1_lo /* low word for reg-pair1 */;
	uint32_t regpair1_hi /* high word for reg-pair1 */;
};


/*
 * 2nd parse bd type used in ethernet tx BDs
 */
enum eth_2nd_parse_bd_type
{
	ETH_2ND_PARSE_BD_TYPE_LSO_TUNNEL,
	MAX_ETH_2ND_PARSE_BD_TYPE};


/*
 * Ethernet address typesm used in ethernet tx BDs
 */
enum eth_addr_type
{
	UNKNOWN_ADDRESS,
	UNICAST_ADDRESS,
	MULTICAST_ADDRESS,
	BROADCAST_ADDRESS,
	MAX_ETH_ADDR_TYPE};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct eth_classify_cmd_header
{
	uint8_t cmd_general_data;
		#define ETH_CLASSIFY_CMD_HEADER_RX_CMD                                               (0x1<<0) /* BitField cmd_general_data	should this cmd be applied for Rx */
		#define ETH_CLASSIFY_CMD_HEADER_RX_CMD_SHIFT                                         0
		#define ETH_CLASSIFY_CMD_HEADER_TX_CMD                                               (0x1<<1) /* BitField cmd_general_data	should this cmd be applied for Tx */
		#define ETH_CLASSIFY_CMD_HEADER_TX_CMD_SHIFT                                         1
		#define ETH_CLASSIFY_CMD_HEADER_OPCODE                                               (0x3<<2) /* BitField cmd_general_data	command opcode for MAC/VLAN/PAIR/IMAC_VNI (use enum classify_rule) */
		#define ETH_CLASSIFY_CMD_HEADER_OPCODE_SHIFT                                         2
		#define ETH_CLASSIFY_CMD_HEADER_IS_ADD                                               (0x1<<4) /* BitField cmd_general_data	 (use enum classify_rule_action_type) */
		#define ETH_CLASSIFY_CMD_HEADER_IS_ADD_SHIFT                                         4
		#define ETH_CLASSIFY_CMD_HEADER_RESERVED0                                            (0x7<<5) /* BitField cmd_general_data	 */
		#define ETH_CLASSIFY_CMD_HEADER_RESERVED0_SHIFT                                      5
	uint8_t func_id /* the function id */;
	uint8_t client_id;
	uint8_t reserved1;
};


/*
 * header for eth classification config ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_classify_header
{
	uint8_t rule_cnt /* number of rules in classification config ramrod */;
	uint8_t reserved0;
	uint16_t reserved1;
	uint32_t echo /* echo value to be sent to driver on event ring */;
};


/*
 * Command for adding/removing a Inner-MAC/VNI classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_imac_vni_cmd
{
	struct eth_classify_cmd_header header;
	uint32_t vni;
	uint16_t imac_lsb;
	uint16_t imac_mid;
	uint16_t imac_msb;
	uint16_t reserved1;
};


/*
 * Command for adding/removing a MAC classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_mac_cmd
{
	struct eth_classify_cmd_header header;
	uint16_t reserved0;
	uint16_t inner_mac;
	uint16_t mac_lsb;
	uint16_t mac_mid;
	uint16_t mac_msb;
	uint16_t reserved1;
};


/*
 * Command for adding/removing a MAC-VLAN pair classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_pair_cmd
{
	struct eth_classify_cmd_header header;
	uint16_t reserved0;
	uint16_t inner_mac;
	uint16_t mac_lsb;
	uint16_t mac_mid;
	uint16_t mac_msb;
	uint16_t vlan;
};


/*
 * Command for adding/removing a VLAN classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_vlan_cmd
{
	struct eth_classify_cmd_header header;
	uint32_t reserved0;
	uint32_t reserved1;
	uint16_t reserved2;
	uint16_t vlan;
};

/*
 * union for eth classification rule $$KEEP_ENDIANNESS$$
 */
union eth_classify_rule_cmd
{
	struct eth_classify_mac_cmd mac;
	struct eth_classify_vlan_cmd vlan;
	struct eth_classify_pair_cmd pair;
	struct eth_classify_imac_vni_cmd imac_vni;
};

/*
 * parameters for eth classification configuration ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_classify_rules_ramrod_data
{
	struct eth_classify_header header;
	union eth_classify_rule_cmd rules[CLASSIFY_RULES_COUNT];
};


/*
 * The data contain client ID need to the ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_common_ramrod_data
{
	uint32_t client_id /* id of this client. (5 bits are used) */;
	uint32_t reserved1;
};


/*
 * The eth storm context of Ustorm
 */
struct ustorm_eth_st_context
{
	uint32_t reserved0[52];
};

/*
 * The eth storm context of Tstorm
 */
struct tstorm_eth_st_context
{
	uint32_t __reserved0[28];
};

/*
 * The eth storm context of Xstorm
 */
struct xstorm_eth_st_context
{
	uint32_t reserved0[60];
};

/*
 * Ethernet connection context
 */
struct eth_context
{
	struct ustorm_eth_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_eth_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_eth_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_eth_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_eth_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_eth_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_eth_st_context xstorm_st_context /* Xstorm storm context */;
	struct cstorm_eth_st_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * union for sgl and raw data.
 */
union eth_sgl_or_raw_data
{
	uint16_t sgl[8] /* Scatter-gather list of SGEs used by this packet. This list includes the indices of the SGEs. */;
	uint32_t raw_data[4] /* raw data from Tstorm to the driver. */;
};

/*
 * eth FP end aggregation CQE parameters struct $$KEEP_ENDIANNESS$$
 */
struct eth_end_agg_rx_cqe
{
	uint8_t type_error_flags;
		#define ETH_END_AGG_RX_CQE_TYPE                                                      (0x3<<0) /* BitField type_error_flags	 (use enum eth_rx_cqe_type) */
		#define ETH_END_AGG_RX_CQE_TYPE_SHIFT                                                0
		#define ETH_END_AGG_RX_CQE_SGL_RAW_SEL                                               (0x1<<2) /* BitField type_error_flags	 (use enum eth_rx_fp_sel) */
		#define ETH_END_AGG_RX_CQE_SGL_RAW_SEL_SHIFT                                         2
		#define ETH_END_AGG_RX_CQE_RESERVED0                                                 (0x1F<<3) /* BitField type_error_flags	 */
		#define ETH_END_AGG_RX_CQE_RESERVED0_SHIFT                                           3
	uint8_t reserved1;
	uint8_t queue_index /* The aggregation queue index of this packet */;
	uint8_t reserved2;
	uint32_t timestamp_delta /* timestamp delta between first packet to last packet in aggregation */;
	uint16_t num_of_coalesced_segs /* Num of coalesced segments. */;
	uint16_t pkt_len /* Packet length */;
	uint8_t pure_ack_count /* Number of pure acks coalesced. */;
	uint8_t reserved3;
	uint16_t reserved4;
	union eth_sgl_or_raw_data sgl_or_raw_data /* union for sgl and raw data. */;
	uint32_t padding[8];
};


/*
 * regular eth FP CQE parameters struct $$KEEP_ENDIANNESS$$
 */
struct eth_fast_path_rx_cqe
{
	uint8_t type_error_flags;
		#define ETH_FAST_PATH_RX_CQE_TYPE                                                    (0x3<<0) /* BitField type_error_flags	 (use enum eth_rx_cqe_type) */
		#define ETH_FAST_PATH_RX_CQE_TYPE_SHIFT                                              0
		#define ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL                                             (0x1<<2) /* BitField type_error_flags	 (use enum eth_rx_fp_sel) */
		#define ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL_SHIFT                                       2
		#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG                                      (0x1<<3) /* BitField type_error_flags	Physical layer errors */
		#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG_SHIFT                                3
		#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG                                         (0x1<<4) /* BitField type_error_flags	IP checksum error */
		#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG_SHIFT                                   4
		#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG                                         (0x1<<5) /* BitField type_error_flags	TCP/UDP checksum error */
		#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG_SHIFT                                   5
		#define ETH_FAST_PATH_RX_CQE_PTP_PKT                                                 (0x1<<6) /* BitField type_error_flags	Is a PTP Timesync Packet */
		#define ETH_FAST_PATH_RX_CQE_PTP_PKT_SHIFT                                           6
		#define ETH_FAST_PATH_RX_CQE_RESERVED0                                               (0x1<<7) /* BitField type_error_flags	 */
		#define ETH_FAST_PATH_RX_CQE_RESERVED0_SHIFT                                         7
	uint8_t status_flags;
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE                                           (0x7<<0) /* BitField status_flags	 (use enum eth_rss_hash_type) */
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE_SHIFT                                     0
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG                                            (0x1<<3) /* BitField status_flags	RSS hashing on/off */
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG_SHIFT                                      3
		#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG                                           (0x1<<4) /* BitField status_flags	if set to 1, this is a broadcast packet */
		#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG_SHIFT                                     4
		#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG                                           (0x1<<5) /* BitField status_flags	if set to 1, the MAC address was matched in the tstorm CAM search */
		#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG_SHIFT                                     5
		#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG                               (0x1<<6) /* BitField status_flags	IP checksum validation was not performed (if packet is not IPv4) */
		#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG_SHIFT                         6
		#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG                               (0x1<<7) /* BitField status_flags	TCP/UDP checksum validation was not performed (if packet is not TCP/UDP or IPv6 extheaders exist) */
		#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG_SHIFT                         7
	uint8_t queue_index /* The aggregation queue index of this packet */;
	uint8_t placement_offset /* Placement offset from the start of the BD, in bytes */;
	uint32_t rss_hash_result /* RSS toeplitz hash result */;
	uint16_t vlan_tag /* Ethernet VLAN tag field */;
	uint16_t pkt_len_or_gro_seg_len /* Packet length (for non-TPA CQE) or GRO Segment Length (for TPA in GRO Mode) otherwise 0 */;
	uint16_t len_on_bd /* Number of bytes placed on the BD */;
	struct parsing_flags pars_flags;
	union eth_sgl_or_raw_data sgl_or_raw_data /* union for sgl and raw data. */;
	uint8_t tunn_type /* packet tunneling type */;
	uint8_t tunn_inner_hdrs_offset /* Offset to Inner Headers (for tunn_type != TUNN_TYPE_NONE) */;
	uint16_t reserved1;
	uint32_t tunn_tenant_id /* Tenant ID (for tunn_type != TUNN_TYPE_NONE */;
	uint32_t padding[5];
	uint32_t marker /* Used internally by the driver */;
};


/*
 * Command for setting classification flags for a client $$KEEP_ENDIANNESS$$
 */
struct eth_filter_rules_cmd
{
	uint8_t cmd_general_data;
		#define ETH_FILTER_RULES_CMD_RX_CMD                                                  (0x1<<0) /* BitField cmd_general_data	should this cmd be applied for Rx */
		#define ETH_FILTER_RULES_CMD_RX_CMD_SHIFT                                            0
		#define ETH_FILTER_RULES_CMD_TX_CMD                                                  (0x1<<1) /* BitField cmd_general_data	should this cmd be applied for Tx */
		#define ETH_FILTER_RULES_CMD_TX_CMD_SHIFT                                            1
		#define ETH_FILTER_RULES_CMD_RESERVED0                                               (0x3F<<2) /* BitField cmd_general_data	 */
		#define ETH_FILTER_RULES_CMD_RESERVED0_SHIFT                                         2
	uint8_t func_id /* the function id */;
	uint8_t client_id /* the client id */;
	uint8_t reserved1;
	uint16_t state;
		#define ETH_FILTER_RULES_CMD_UCAST_DROP_ALL                                          (0x1<<0) /* BitField state	drop all unicast packets */
		#define ETH_FILTER_RULES_CMD_UCAST_DROP_ALL_SHIFT                                    0
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL                                        (0x1<<1) /* BitField state	accept all unicast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL_SHIFT                                  1
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED                                  (0x1<<2) /* BitField state	accept all unmatched unicast packets */
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED_SHIFT                            2
		#define ETH_FILTER_RULES_CMD_MCAST_DROP_ALL                                          (0x1<<3) /* BitField state	drop all multicast packets */
		#define ETH_FILTER_RULES_CMD_MCAST_DROP_ALL_SHIFT                                    3
		#define ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL                                        (0x1<<4) /* BitField state	accept all multicast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL_SHIFT                                  4
		#define ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL                                        (0x1<<5) /* BitField state	accept all broadcast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL_SHIFT                                  5
		#define ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN                                         (0x1<<6) /* BitField state	accept packets matched only by MAC (without checking vlan) */
		#define ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN_SHIFT                                   6
		#define ETH_FILTER_RULES_CMD_RESERVED2                                               (0x1FF<<7) /* BitField state	 */
		#define ETH_FILTER_RULES_CMD_RESERVED2_SHIFT                                         7
	uint16_t reserved3;
	struct regpair_t reserved4;
};


/*
 * parameters for eth classification filters ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_filter_rules_ramrod_data
{
	struct eth_classify_header header;
	struct eth_filter_rules_cmd rules[FILTER_RULES_COUNT];
};


/*
 * Hsi version
 */
enum eth_fp_hsi_ver
{
	ETH_FP_HSI_VER_0 /* Hsi which does not support tunnelling */,
	ETH_FP_HSI_VER_1 /* Hsi does support tunnelling */,
	ETH_FP_HSI_VER_2 /* Hsi which supports tunneling and UFP */,
	MAX_ETH_FP_HSI_VER};


/*
 * parameters for eth classification configuration ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_general_rules_ramrod_data
{
	struct eth_classify_header header;
	union eth_classify_rule_cmd rules[CLASSIFY_RULES_COUNT];
};


/*
 * The data for Halt ramrod
 */
struct eth_halt_ramrod_data
{
	uint32_t client_id /* id of this client. (5 bits are used) */;
	uint32_t reserved0;
};


/*
 * destination and source mac address.
 */
struct eth_mac_addresses
{
#if defined(__BIG_ENDIAN)
	uint16_t dst_mid /* destination mac address 16 middle bits */;
	uint16_t dst_lo /* destination mac address 16 low bits */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t dst_lo /* destination mac address 16 low bits */;
	uint16_t dst_mid /* destination mac address 16 middle bits */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t src_lo /* source mac address 16 low bits */;
	uint16_t dst_hi /* destination mac address 16 high bits */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t dst_hi /* destination mac address 16 high bits */;
	uint16_t src_lo /* source mac address 16 low bits */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t src_hi /* source mac address 16 high bits */;
	uint16_t src_mid /* source mac address 16 middle bits */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t src_mid /* source mac address 16 middle bits */;
	uint16_t src_hi /* source mac address 16 high bits */;
#endif
};


/*
 * tunneling related data. $$KEEP_ENDIANNESS$$
 */
struct eth_tunnel_data
{
	uint16_t dst_lo /* destination mac address 16 low bits */;
	uint16_t dst_mid /* destination mac address 16 middle bits */;
	uint16_t dst_hi /* destination mac address 16 high bits */;
	uint16_t fw_ip_hdr_csum /* Fw Ip header checksum (with ALL ip header fields) for the outer IP header */;
	uint16_t pseudo_csum /* Pseudo checksum with  length  field=0 */;
	uint8_t ip_hdr_start_inner_w /* Inner IP header offset in WORDs (16-bit) from start of packet */;
	uint8_t flags;
		#define ETH_TUNNEL_DATA_IPV6_OUTER                                                   (0x1<<0) /* BitField flags	Set in case outer IP header is ipV6 */
		#define ETH_TUNNEL_DATA_IPV6_OUTER_SHIFT                                             0
		#define ETH_TUNNEL_DATA_RESERVED                                                     (0x7F<<1) /* BitField flags	Should be set with 0 */
		#define ETH_TUNNEL_DATA_RESERVED_SHIFT                                               1
};

/*
 * union for mac addresses and for tunneling data. considered as tunneling data only if (tunnel_exist == 1).
 */
union eth_mac_addr_or_tunnel_data
{
	struct eth_mac_addresses mac_addr /* destination and source mac addresses. */;
	struct eth_tunnel_data tunnel_data /* tunneling related data. */;
};


/*
 * Command for setting multicast classification for a client $$KEEP_ENDIANNESS$$
 */
struct eth_multicast_rules_cmd
{
	uint8_t cmd_general_data;
		#define ETH_MULTICAST_RULES_CMD_RX_CMD                                               (0x1<<0) /* BitField cmd_general_data	should this cmd be applied for Rx */
		#define ETH_MULTICAST_RULES_CMD_RX_CMD_SHIFT                                         0
		#define ETH_MULTICAST_RULES_CMD_TX_CMD                                               (0x1<<1) /* BitField cmd_general_data	should this cmd be applied for Tx */
		#define ETH_MULTICAST_RULES_CMD_TX_CMD_SHIFT                                         1
		#define ETH_MULTICAST_RULES_CMD_IS_ADD                                               (0x1<<2) /* BitField cmd_general_data	1 for add rule, 0 for remove rule */
		#define ETH_MULTICAST_RULES_CMD_IS_ADD_SHIFT                                         2
		#define ETH_MULTICAST_RULES_CMD_RESERVED0                                            (0x1F<<3) /* BitField cmd_general_data	 */
		#define ETH_MULTICAST_RULES_CMD_RESERVED0_SHIFT                                      3
	uint8_t func_id /* the function id */;
	uint8_t bin_id /* the bin to add this function to (0-255) */;
	uint8_t engine_id /* the approximate multicast engine id */;
	uint32_t reserved2;
	struct regpair_t reserved3;
};


/*
 * parameters for multicast classification ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_multicast_rules_ramrod_data
{
	struct eth_classify_header header;
	struct eth_multicast_rules_cmd rules[MULTICAST_RULES_COUNT];
};


/*
 * Place holder for ramrods protocol specific data
 */
struct ramrod_data
{
	uint32_t data_lo;
	uint32_t data_hi;
};

/*
 * union for ramrod data for Ethernet protocol (CQE) (force size of 16 bits)
 */
union eth_ramrod_data
{
	struct ramrod_data general;
};


/*
 * RSS toeplitz hash type, as reported in CQE
 */
enum eth_rss_hash_type
{
	DEFAULT_HASH_TYPE,
	IPV4_HASH_TYPE,
	TCP_IPV4_HASH_TYPE,
	IPV6_HASH_TYPE,
	TCP_IPV6_HASH_TYPE,
	VLAN_PRI_HASH_TYPE,
	E1HOV_PRI_HASH_TYPE,
	DSCP_HASH_TYPE,
	MAX_ETH_RSS_HASH_TYPE};


/*
 * Ethernet RSS mode
 */
enum eth_rss_mode
{
	ETH_RSS_MODE_DISABLED,
	ETH_RSS_MODE_REGULAR /* Regular (ndis-like) RSS */,
	ETH_RSS_MODE_ESX51 /* RSS mode for Vmware ESX 5.1 (Only do RSS for VXLAN packets) */,
	ETH_RSS_MODE_VLAN_PRI /* RSS based on inner-vlan priority field (E1/E1h Only) */,
	ETH_RSS_MODE_E1HOV_PRI /* RSS based on outer-vlan priority field (E1/E1h Only) */,
	ETH_RSS_MODE_IP_DSCP /* RSS based on IPv4 DSCP field (E1/E1h Only) */,
	MAX_ETH_RSS_MODE};


/*
 * parameters for RSS update ramrod (E2) $$KEEP_ENDIANNESS$$
 */
struct eth_rss_update_ramrod_data
{
	uint8_t rss_engine_id;
	uint8_t rss_mode /* The RSS mode for this function */;
	uint16_t capabilities;
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY                                   (0x1<<0) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV4 2-tuple capability */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY_SHIFT                             0
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY                               (0x1<<1) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV4 4-tuple capability for TCP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY_SHIFT                         1
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_UDP_CAPABILITY                               (0x1<<2) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV4 4-tuple capability for UDP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_UDP_CAPABILITY_SHIFT                         2
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_VXLAN_CAPABILITY                             (0x1<<3) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV4 4-tuple capability for VXLAN Tunnels */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_VXLAN_CAPABILITY_SHIFT                       3
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY                                   (0x1<<4) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV6 2-tuple capability */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY_SHIFT                             4
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY                               (0x1<<5) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV6 4-tuple capability for TCP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY_SHIFT                         5
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_UDP_CAPABILITY                               (0x1<<6) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV6 4-tuple capability for UDP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_UDP_CAPABILITY_SHIFT                         6
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_VXLAN_CAPABILITY                             (0x1<<7) /* BitField capabilitiesFunction RSS capabilities	configuration of the IpV6 4-tuple capability for VXLAN Tunnels */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_VXLAN_CAPABILITY_SHIFT                       7
		#define ETH_RSS_UPDATE_RAMROD_DATA_TUNN_INNER_HDRS_CAPABILITY                        (0x1<<8) /* BitField capabilitiesFunction RSS capabilities	configuration of Tunnel Inner Headers capability. */
		#define ETH_RSS_UPDATE_RAMROD_DATA_TUNN_INNER_HDRS_CAPABILITY_SHIFT                  8
		#define ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY                                    (0x1<<9) /* BitField capabilitiesFunction RSS capabilities	if set update the rss keys */
		#define ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY_SHIFT                              9
		#define ETH_RSS_UPDATE_RAMROD_DATA_RESERVED                                          (0x3F<<10) /* BitField capabilitiesFunction RSS capabilities	 */
		#define ETH_RSS_UPDATE_RAMROD_DATA_RESERVED_SHIFT                                    10
	uint8_t rss_result_mask /* The mask for the lower byte of RSS result - defines which section of the indirection table will be used. To enable all table put here 0x7F */;
	uint8_t reserved3;
	uint16_t reserved4;
	uint8_t indirection_table[T_ETH_INDIRECTION_TABLE_SIZE] /* RSS indirection table */;
	uint32_t rss_key[T_ETH_RSS_KEY] /* RSS key supplied as by OS */;
	uint32_t echo;
	uint32_t reserved5;
};


/*
 * The eth Rx Buffer Descriptor
 */
struct eth_rx_bd
{
	uint32_t addr_lo /* Single continuous buffer low pointer */;
	uint32_t addr_hi /* Single continuous buffer high pointer */;
};


struct eth_rx_bd_next_page
{
	uint32_t addr_lo /* Next page low pointer */;
	uint32_t addr_hi /* Next page high pointer */;
	uint8_t reserved[8];
};


/*
 * Eth Rx Cqe structure- general structure for ramrods $$KEEP_ENDIANNESS$$
 */
struct common_ramrod_eth_rx_cqe
{
	uint8_t ramrod_type;
		#define COMMON_RAMROD_ETH_RX_CQE_TYPE                                                (0x3<<0) /* BitField ramrod_type	 (use enum eth_rx_cqe_type) */
		#define COMMON_RAMROD_ETH_RX_CQE_TYPE_SHIFT                                          0
		#define COMMON_RAMROD_ETH_RX_CQE_ERROR                                               (0x1<<2) /* BitField ramrod_type	 */
		#define COMMON_RAMROD_ETH_RX_CQE_ERROR_SHIFT                                         2
		#define COMMON_RAMROD_ETH_RX_CQE_RESERVED0                                           (0x1F<<3) /* BitField ramrod_type	 */
		#define COMMON_RAMROD_ETH_RX_CQE_RESERVED0_SHIFT                                     3
	uint8_t conn_type /* only 3 bits are used */;
	uint16_t reserved1 /* protocol specific data */;
	uint32_t conn_and_cmd_data;
		#define COMMON_RAMROD_ETH_RX_CQE_CID                                                 (0xFFFFFF<<0) /* BitField conn_and_cmd_data	 */
		#define COMMON_RAMROD_ETH_RX_CQE_CID_SHIFT                                           0
		#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID                                              (0xFF<<24) /* BitField conn_and_cmd_data	command id of the ramrod- use RamrodCommandIdEnum */
		#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT                                        24
	struct ramrod_data protocol_data /* protocol specific data */;
	uint32_t echo;
	uint32_t reserved2[11];
};

/*
 * Rx Last CQE in page (in ETH)
 */
struct eth_rx_cqe_next_page
{
	uint32_t addr_lo /* Next page low pointer */;
	uint32_t addr_hi /* Next page high pointer */;
	uint32_t reserved[14];
};

/*
 * union for all eth rx cqe types (fix their sizes)
 */
union eth_rx_cqe
{
	struct eth_fast_path_rx_cqe fast_path_cqe;
	struct common_ramrod_eth_rx_cqe ramrod_cqe;
	struct eth_rx_cqe_next_page next_page_cqe;
	struct eth_end_agg_rx_cqe end_agg_cqe;
};


/*
 * Values for RX ETH CQE type field
 */
enum eth_rx_cqe_type
{
	RX_ETH_CQE_TYPE_ETH_FASTPATH /* Fast path CQE */,
	RX_ETH_CQE_TYPE_ETH_RAMROD /* Slow path CQE */,
	RX_ETH_CQE_TYPE_ETH_START_AGG /* Fast path CQE */,
	RX_ETH_CQE_TYPE_ETH_STOP_AGG /* Slow path CQE */,
	MAX_ETH_RX_CQE_TYPE};


/*
 * Type of SGL/Raw field in ETH RX fast path CQE
 */
enum eth_rx_fp_sel
{
	ETH_FP_CQE_REGULAR /* Regular CQE- no extra data */,
	ETH_FP_CQE_RAW /* Extra data is raw data- iscsi OOO */,
	MAX_ETH_RX_FP_SEL};


/*
 * The eth Rx SGE Descriptor
 */
struct eth_rx_sge
{
	uint32_t addr_lo /* Single continuous buffer low pointer */;
	uint32_t addr_hi /* Single continuous buffer high pointer */;
};


/*
 * common data for all protocols $$KEEP_ENDIANNESS$$
 */
struct spe_hdr_t
{
	uint32_t conn_and_cmd_data;
		#define SPE_HDR_T_CID                                                                (0xFFFFFF<<0) /* BitField conn_and_cmd_data	 */
		#define SPE_HDR_T_CID_SHIFT                                                          0
		#define SPE_HDR_T_CMD_ID                                                             (0xFFUL<<24) /* BitField conn_and_cmd_data	command id of the ramrod- use enum common_spqe_cmd_id/eth_spqe_cmd_id/toe_spqe_cmd_id  */
		#define SPE_HDR_T_CMD_ID_SHIFT                                                       24
	uint16_t type;
		#define SPE_HDR_T_CONN_TYPE                                                          (0xFF<<0) /* BitField type	connection type. (3 bits are used) (use enum connection_type) */
		#define SPE_HDR_T_CONN_TYPE_SHIFT                                                    0
		#define SPE_HDR_T_FUNCTION_ID                                                        (0xFF<<8) /* BitField type	 */
		#define SPE_HDR_T_FUNCTION_ID_SHIFT                                                  8
	uint16_t reserved1;
};

/*
 * specific data for ethernet slow path element
 */
union eth_specific_data
{
	uint8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t client_update_ramrod_data /* The address of the data for client update ramrod */;
	struct regpair_t client_init_ramrod_init_data /* The data for client setup ramrod */;
	struct eth_halt_ramrod_data halt_ramrod_data /* Includes the client id to be deleted */;
	struct regpair_t update_data_addr /* physical address of the eth_rss_update_ramrod_data struct, as allocated by the driver */;
	struct eth_common_ramrod_data common_ramrod_data /* The data contain client ID need to the ramrod */;
	struct regpair_t classify_cfg_addr /* physical address of the eth_classify_rules_ramrod_data struct, as allocated by the driver */;
	struct regpair_t filter_cfg_addr /* physical address of the eth_filter_cfg_ramrod_data struct, as allocated by the driver */;
	struct regpair_t mcast_cfg_addr /* physical address of the eth_mcast_cfg_ramrod_data struct, as allocated by the driver */;
};

/*
 * Ethernet slow path element
 */
struct eth_spe
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	union eth_specific_data data /* data specific to ethernet protocol */;
};


/*
 * Ethernet command ID for slow path elements
 */
enum eth_spqe_cmd_id
{
	RAMROD_CMD_ID_ETH_UNUSED,
	RAMROD_CMD_ID_ETH_CLIENT_SETUP /* Setup a new L2 client */,
	RAMROD_CMD_ID_ETH_HALT /* Halt an L2 client */,
	RAMROD_CMD_ID_ETH_FORWARD_SETUP /* Setup a new FW channel */,
	RAMROD_CMD_ID_ETH_TX_QUEUE_SETUP /* Setup a new Tx only queue */,
	RAMROD_CMD_ID_ETH_CLIENT_UPDATE /* Update an L2 client configuration */,
	RAMROD_CMD_ID_ETH_EMPTY /* Empty ramrod - used to synchronize iSCSI OOO */,
	RAMROD_CMD_ID_ETH_TERMINATE /* Terminate an L2 client */,
	RAMROD_CMD_ID_ETH_TPA_UPDATE /* update the tpa roles in L2 client */,
	RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_FILTER_RULES /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_MULTICAST_RULES /* Add/remove multicast classification bin (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_RSS_UPDATE /* Update RSS configuration */,
	RAMROD_CMD_ID_ETH_SET_MAC /* Update RSS configuration */,
	MAX_ETH_SPQE_CMD_ID};


/*
 * eth tpa update command
 */
enum eth_tpa_update_command
{
	TPA_UPDATE_NONE_COMMAND /* nop command */,
	TPA_UPDATE_ENABLE_COMMAND /* enable command */,
	TPA_UPDATE_DISABLE_COMMAND /* disable command */,
	MAX_ETH_TPA_UPDATE_COMMAND};


/*
 * In case of LSO over IPv4 tunnel, whether to increment IP ID on external IP header or internal IP header
 */
enum eth_tunnel_lso_inc_ip_id
{
	EXT_HEADER /* Increment IP ID of external header (HW works on external, FW works on internal */,
	INT_HEADER /* Increment IP ID of internal header (HW works on internal, FW works on external */,
	MAX_ETH_TUNNEL_LSO_INC_IP_ID};


/*
 * In case tunnel exist and L4 checksum offload (or outer ip header checksum), the pseudo checksum location, on packet or on BD.
 */
enum eth_tunnel_non_lso_csum_location
{
	CSUM_ON_PKT /* checksum is on the packet. */,
	CSUM_ON_BD /* checksum is on the BD. */,
	MAX_ETH_TUNNEL_NON_LSO_CSUM_LOCATION};


/*
 * Packet Tunneling Type
 */
enum eth_tunn_type
{
	TUNN_TYPE_NONE,
	TUNN_TYPE_VXLAN,
	TUNN_TYPE_L2_GRE /* Ethernet over GRE */,
	TUNN_TYPE_IPV4_GRE /* IPv4 over GRE */,
	TUNN_TYPE_IPV6_GRE /* IPv6 over GRE */,
	TUNN_TYPE_L2_GENEVE /* Ethernet over GENEVE */,
	TUNN_TYPE_IPV4_GENEVE /* IPv4 over GENEVE */,
	TUNN_TYPE_IPV6_GENEVE /* IPv6 over GENEVE */,
	MAX_ETH_TUNN_TYPE};


/*
 * Tx regular BD structure $$KEEP_ENDIANNESS$$
 */
struct eth_tx_bd
{
	uint32_t addr_lo /* Single continuous buffer low pointer */;
	uint32_t addr_hi /* Single continuous buffer high pointer */;
	uint16_t total_pkt_bytes /* Size of the entire packet, valid for non-LSO packets */;
	uint16_t nbytes /* Size of the data represented by the BD */;
	uint8_t reserved[4] /* keeps same size as other eth tx bd types */;
};


/*
 * structure for easy accessibility to assembler
 */
struct eth_tx_bd_flags
{
	uint8_t as_bitfield;
		#define ETH_TX_BD_FLAGS_IP_CSUM                                                      (0x1<<0) /* BitField as_bitfield	IP CKSUM flag,Relevant in START */
		#define ETH_TX_BD_FLAGS_IP_CSUM_SHIFT                                                0
		#define ETH_TX_BD_FLAGS_L4_CSUM                                                      (0x1<<1) /* BitField as_bitfield	L4 CKSUM flag,Relevant in START */
		#define ETH_TX_BD_FLAGS_L4_CSUM_SHIFT                                                1
		#define ETH_TX_BD_FLAGS_VLAN_MODE                                                    (0x3<<2) /* BitField as_bitfield	00 - no vlan; 01 - inband Vlan; 10 outband Vlan (use enum eth_tx_vlan_type) */
		#define ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT                                              2
		#define ETH_TX_BD_FLAGS_START_BD                                                     (0x1<<4) /* BitField as_bitfield	Start of packet BD */
		#define ETH_TX_BD_FLAGS_START_BD_SHIFT                                               4
		#define ETH_TX_BD_FLAGS_IS_UDP                                                       (0x1<<5) /* BitField as_bitfield	flag that indicates that the current packet is a udp packet */
		#define ETH_TX_BD_FLAGS_IS_UDP_SHIFT                                                 5
		#define ETH_TX_BD_FLAGS_SW_LSO                                                       (0x1<<6) /* BitField as_bitfield	LSO flag, Relevant in START */
		#define ETH_TX_BD_FLAGS_SW_LSO_SHIFT                                                 6
		#define ETH_TX_BD_FLAGS_IPV6                                                         (0x1<<7) /* BitField as_bitfield	set in case ipV6 packet, Relevant in START */
		#define ETH_TX_BD_FLAGS_IPV6_SHIFT                                                   7
};

/*
 * The eth Tx Buffer Descriptor $$KEEP_ENDIANNESS$$
 */
struct eth_tx_start_bd
{
	uint32_t addr_lo /* Single continuous buffer low pointer */;
	uint32_t addr_hi /* Single continuous buffer high pointer */;
	uint16_t nbd /* Num of BDs in packet: include parsInfoBD, Relevant in START(only in Everest) */;
	uint16_t nbytes /* Size of the data represented by the BD */;
	uint16_t vlan_or_ethertype /* Vlan structure: vlan_id is in lsb, then cfi and then priority vlan_id 12 bits (lsb), cfi 1 bit, priority 3 bits. In E2, this field should be set with etherType for VFs with no vlan */;
	struct eth_tx_bd_flags bd_flags;
	uint8_t general_data;
		#define ETH_TX_START_BD_HDR_NBDS                                                     (0x7<<0) /* BitField general_data	contains the number of BDs that contain Ethernet/IP/TCP headers, for full/partial LSO modes */
		#define ETH_TX_START_BD_HDR_NBDS_SHIFT                                               0
		#define ETH_TX_START_BD_NO_ADDED_TAGS                                                (0x1<<3) /* BitField general_data	If set, do not add any additional tags to the packet including MF Tags, Default VLAN or VLAN for the sake of DCB */
		#define ETH_TX_START_BD_NO_ADDED_TAGS_SHIFT                                          3
		#define ETH_TX_START_BD_FORCE_VLAN_MODE                                              (0x1<<4) /* BitField general_data	force vlan mode according to bds (vlan mode can change accroding to global configuration) */
		#define ETH_TX_START_BD_FORCE_VLAN_MODE_SHIFT                                        4
		#define ETH_TX_START_BD_PARSE_NBDS                                                   (0x3<<5) /* BitField general_data	Determines the number of parsing BDs in packet. Number of parsing BDs in packet is (parse_nbds+1). */
		#define ETH_TX_START_BD_PARSE_NBDS_SHIFT                                             5
		#define ETH_TX_START_BD_TUNNEL_EXIST                                                 (0x1<<7) /* BitField general_data	set in case of tunneling encapsulated packet */
		#define ETH_TX_START_BD_TUNNEL_EXIST_SHIFT                                           7
};

/*
 * Tx parsing BD structure for ETH E1/E1h $$KEEP_ENDIANNESS$$
 */
struct eth_tx_parse_bd_e1x
{
	uint16_t global_data;
		#define ETH_TX_PARSE_BD_E1X_IP_HDR_START_OFFSET_W                                    (0xF<<0) /* BitField global_data	IP header Offset in WORDs from start of packet */
		#define ETH_TX_PARSE_BD_E1X_IP_HDR_START_OFFSET_W_SHIFT                              0
		#define ETH_TX_PARSE_BD_E1X_ETH_ADDR_TYPE                                            (0x3<<4) /* BitField global_data	marks ethernet address type (use enum eth_addr_type) */
		#define ETH_TX_PARSE_BD_E1X_ETH_ADDR_TYPE_SHIFT                                      4
		#define ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN                                    (0x1<<6) /* BitField global_data	 */
		#define ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN_SHIFT                              6
		#define ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN                                              (0x1<<7) /* BitField global_data	 */
		#define ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN_SHIFT                                        7
		#define ETH_TX_PARSE_BD_E1X_NS_FLG                                                   (0x1<<8) /* BitField global_data	an optional addition to ECN that protects against accidental or malicious concealment of marked packets from the TCP sender. */
		#define ETH_TX_PARSE_BD_E1X_NS_FLG_SHIFT                                             8
		#define ETH_TX_PARSE_BD_E1X_RESERVED0                                                (0x7F<<9) /* BitField global_data	reserved bit, should be set with 0 */
		#define ETH_TX_PARSE_BD_E1X_RESERVED0_SHIFT                                          9
	uint8_t tcp_flags;
		#define ETH_TX_PARSE_BD_E1X_FIN_FLG                                                  (0x1<<0) /* BitField tcp_flagsState flags	End of data flag */
		#define ETH_TX_PARSE_BD_E1X_FIN_FLG_SHIFT                                            0
		#define ETH_TX_PARSE_BD_E1X_SYN_FLG                                                  (0x1<<1) /* BitField tcp_flagsState flags	Synchronize sequence numbers flag */
		#define ETH_TX_PARSE_BD_E1X_SYN_FLG_SHIFT                                            1
		#define ETH_TX_PARSE_BD_E1X_RST_FLG                                                  (0x1<<2) /* BitField tcp_flagsState flags	Reset connection flag */
		#define ETH_TX_PARSE_BD_E1X_RST_FLG_SHIFT                                            2
		#define ETH_TX_PARSE_BD_E1X_PSH_FLG                                                  (0x1<<3) /* BitField tcp_flagsState flags	Push flag */
		#define ETH_TX_PARSE_BD_E1X_PSH_FLG_SHIFT                                            3
		#define ETH_TX_PARSE_BD_E1X_ACK_FLG                                                  (0x1<<4) /* BitField tcp_flagsState flags	Acknowledgment number valid flag */
		#define ETH_TX_PARSE_BD_E1X_ACK_FLG_SHIFT                                            4
		#define ETH_TX_PARSE_BD_E1X_URG_FLG                                                  (0x1<<5) /* BitField tcp_flagsState flags	Urgent pointer valid flag */
		#define ETH_TX_PARSE_BD_E1X_URG_FLG_SHIFT                                            5
		#define ETH_TX_PARSE_BD_E1X_ECE_FLG                                                  (0x1<<6) /* BitField tcp_flagsState flags	ECN-Echo */
		#define ETH_TX_PARSE_BD_E1X_ECE_FLG_SHIFT                                            6
		#define ETH_TX_PARSE_BD_E1X_CWR_FLG                                                  (0x1<<7) /* BitField tcp_flagsState flags	Congestion Window Reduced */
		#define ETH_TX_PARSE_BD_E1X_CWR_FLG_SHIFT                                            7
	uint8_t ip_hlen_w /* IP header length in WORDs */;
	uint16_t total_hlen_w /* IP+TCP+ETH */;
	uint16_t tcp_pseudo_csum /* Checksum of pseudo header with  length  field=0 */;
	uint16_t lso_mss /* for LSO mode */;
	uint16_t ip_id /* for LSO mode */;
	uint32_t tcp_send_seq /* for LSO mode */;
};

/*
 * Tx parsing BD structure for ETH E2 $$KEEP_ENDIANNESS$$
 */
struct eth_tx_parse_bd_e2
{
	union eth_mac_addr_or_tunnel_data data /* union for mac addresses and for tunneling data. considered as tunneling data only if (tunnel_exist == 1). */;
	uint32_t parsing_data;
		#define ETH_TX_PARSE_BD_E2_L4_HDR_START_OFFSET_W                                     (0x7FF<<0) /* BitField parsing_data	TCP/UDP header Offset in WORDs from start of packet */
		#define ETH_TX_PARSE_BD_E2_L4_HDR_START_OFFSET_W_SHIFT                               0
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW                                         (0xF<<11) /* BitField parsing_data	TCP header size in DOUBLE WORDS */
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW_SHIFT                                   11
		#define ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR                                         (0x1<<15) /* BitField parsing_data	a flag to indicate an ipv6 packet with extension headers. If set on LSO packet, pseudo CS should be placed in TCP CS field without length field */
		#define ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR_SHIFT                                   15
		#define ETH_TX_PARSE_BD_E2_LSO_MSS                                                   (0x3FFF<<16) /* BitField parsing_data	for LSO mode */
		#define ETH_TX_PARSE_BD_E2_LSO_MSS_SHIFT                                             16
		#define ETH_TX_PARSE_BD_E2_ETH_ADDR_TYPE                                             (0x3<<30) /* BitField parsing_data	marks ethernet address type (use enum eth_addr_type) */
		#define ETH_TX_PARSE_BD_E2_ETH_ADDR_TYPE_SHIFT                                       30
};

/*
 * Tx 2nd parsing BD structure for ETH packet $$KEEP_ENDIANNESS$$
 */
struct eth_tx_parse_2nd_bd
{
	uint16_t global_data;
		#define ETH_TX_PARSE_2ND_BD_IP_HDR_START_OUTER_W                                     (0xF<<0) /* BitField global_data	Outer IP header offset in WORDs (16-bit) from start of packet */
		#define ETH_TX_PARSE_2ND_BD_IP_HDR_START_OUTER_W_SHIFT                               0
		#define ETH_TX_PARSE_2ND_BD_RESERVED0                                                (0x1<<4) /* BitField global_data	should be set with 0 */
		#define ETH_TX_PARSE_2ND_BD_RESERVED0_SHIFT                                          4
		#define ETH_TX_PARSE_2ND_BD_LLC_SNAP_EN                                              (0x1<<5) /* BitField global_data	 */
		#define ETH_TX_PARSE_2ND_BD_LLC_SNAP_EN_SHIFT                                        5
		#define ETH_TX_PARSE_2ND_BD_NS_FLG                                                   (0x1<<6) /* BitField global_data	an optional addition to ECN that protects against accidental or malicious concealment of marked packets from the TCP sender. */
		#define ETH_TX_PARSE_2ND_BD_NS_FLG_SHIFT                                             6
		#define ETH_TX_PARSE_2ND_BD_TUNNEL_UDP_EXIST                                         (0x1<<7) /* BitField global_data	Set in case UDP header exists in tunnel outer hedears. */
		#define ETH_TX_PARSE_2ND_BD_TUNNEL_UDP_EXIST_SHIFT                                   7
		#define ETH_TX_PARSE_2ND_BD_IP_HDR_LEN_OUTER_W                                       (0x1F<<8) /* BitField global_data	Outer IP header length in WORDs (16-bit). Valid only for IpV4. */
		#define ETH_TX_PARSE_2ND_BD_IP_HDR_LEN_OUTER_W_SHIFT                                 8
		#define ETH_TX_PARSE_2ND_BD_RESERVED1                                                (0x7<<13) /* BitField global_data	should be set with 0 */
		#define ETH_TX_PARSE_2ND_BD_RESERVED1_SHIFT                                          13
	uint8_t bd_type;
		#define ETH_TX_PARSE_2ND_BD_TYPE                                                     (0xF<<0) /* BitField bd_type	Type of bd (use enum eth_2nd_parse_bd_type) */
		#define ETH_TX_PARSE_2ND_BD_TYPE_SHIFT                                               0
		#define ETH_TX_PARSE_2ND_BD_RESERVED2                                                (0xF<<4) /* BitField bd_type	 */
		#define ETH_TX_PARSE_2ND_BD_RESERVED2_SHIFT                                          4
	uint8_t reserved3;
	uint8_t tcp_flags;
		#define ETH_TX_PARSE_2ND_BD_FIN_FLG                                                  (0x1<<0) /* BitField tcp_flagsState flags	End of data flag */
		#define ETH_TX_PARSE_2ND_BD_FIN_FLG_SHIFT                                            0
		#define ETH_TX_PARSE_2ND_BD_SYN_FLG                                                  (0x1<<1) /* BitField tcp_flagsState flags	Synchronize sequence numbers flag */
		#define ETH_TX_PARSE_2ND_BD_SYN_FLG_SHIFT                                            1
		#define ETH_TX_PARSE_2ND_BD_RST_FLG                                                  (0x1<<2) /* BitField tcp_flagsState flags	Reset connection flag */
		#define ETH_TX_PARSE_2ND_BD_RST_FLG_SHIFT                                            2
		#define ETH_TX_PARSE_2ND_BD_PSH_FLG                                                  (0x1<<3) /* BitField tcp_flagsState flags	Push flag */
		#define ETH_TX_PARSE_2ND_BD_PSH_FLG_SHIFT                                            3
		#define ETH_TX_PARSE_2ND_BD_ACK_FLG                                                  (0x1<<4) /* BitField tcp_flagsState flags	Acknowledgment number valid flag */
		#define ETH_TX_PARSE_2ND_BD_ACK_FLG_SHIFT                                            4
		#define ETH_TX_PARSE_2ND_BD_URG_FLG                                                  (0x1<<5) /* BitField tcp_flagsState flags	Urgent pointer valid flag */
		#define ETH_TX_PARSE_2ND_BD_URG_FLG_SHIFT                                            5
		#define ETH_TX_PARSE_2ND_BD_ECE_FLG                                                  (0x1<<6) /* BitField tcp_flagsState flags	ECN-Echo */
		#define ETH_TX_PARSE_2ND_BD_ECE_FLG_SHIFT                                            6
		#define ETH_TX_PARSE_2ND_BD_CWR_FLG                                                  (0x1<<7) /* BitField tcp_flagsState flags	Congestion Window Reduced */
		#define ETH_TX_PARSE_2ND_BD_CWR_FLG_SHIFT                                            7
	uint8_t reserved4;
	uint8_t tunnel_udp_hdr_start_w /* Offset (in WORDs) from start of packet to tunnel UDP header. (if exist) */;
	uint8_t fw_ip_hdr_to_payload_w /* In IpV4, the length (in WORDs) from the FW IpV4 header start to the payload start. In IpV6, the length (in WORDs) from the FW IpV6 header end to the payload start. However, if extension headers are included, their length is counted here as well. */;
	uint16_t fw_ip_csum_wo_len_flags_frag /* For the IP header which is set by the FW, the IP checksum without length, flags and fragment offset. */;
	uint16_t hw_ip_id /* The IP ID to be set by HW for LSO packets in tunnel mode. */;
	uint32_t tcp_send_seq /* The TCP sequence number for LSO packets. */;
};

/*
 * The last BD in the BD memory will hold a pointer to the next BD memory
 */
struct eth_tx_next_bd
{
	uint32_t addr_lo /* Single continuous buffer low pointer */;
	uint32_t addr_hi /* Single continuous buffer high pointer */;
	uint8_t reserved[8] /* keeps same size as other eth tx bd types */;
};

/*
 * union for 4 Bd types
 */
union eth_tx_bd_types
{
	struct eth_tx_start_bd start_bd /* the first bd in a packets */;
	struct eth_tx_bd reg_bd /* the common bd */;
	struct eth_tx_parse_bd_e1x parse_bd_e1x /* parsing info BD for e1/e1h */;
	struct eth_tx_parse_bd_e2 parse_bd_e2 /* parsing info BD for e2 */;
	struct eth_tx_parse_2nd_bd parse_2nd_bd /* 2nd parsing info BD */;
	struct eth_tx_next_bd next_bd /* Bd that contains the address of the next page */;
};

/*
 * array of 13 bds as appears in the eth xstorm context
 */
struct eth_tx_bds_array
{
	union eth_tx_bd_types bds[13];
};


/*
 * VLAN mode on TX BDs
 */
enum eth_tx_vlan_type
{
	X_ETH_NO_VLAN,
	X_ETH_OUTBAND_VLAN,
	X_ETH_INBAND_VLAN,
	X_ETH_FW_ADDED_VLAN /* Driver should not use this! */,
	MAX_ETH_TX_VLAN_TYPE};


/*
 * Ethernet VLAN filtering mode in E1x
 */
enum eth_vlan_filter_mode
{
	ETH_VLAN_FILTER_ANY_VLAN /* Dont filter by vlan */,
	ETH_VLAN_FILTER_SPECIFIC_VLAN /* Only the vlan_id is allowed */,
	ETH_VLAN_FILTER_CLASSIFY /* Vlan will be added to CAM for classification */,
	MAX_ETH_VLAN_FILTER_MODE};


/*
 * MAC filtering configuration command header $$KEEP_ENDIANNESS$$
 */
struct mac_configuration_hdr
{
	uint8_t length /* number of entries valid in this command (6 bits) */;
	uint8_t offset /* offset of the first entry in the list */;
	uint16_t client_id /* the client id which this ramrod is sent on. 5b is used. */;
	uint32_t echo /* echo value to be sent to driver on event ring */;
};

/*
 * MAC address in list for ramrod $$KEEP_ENDIANNESS$$
 */
struct mac_configuration_entry
{
	uint16_t lsb_mac_addr /* 2 LSB of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	uint16_t middle_mac_addr /* 2 middle bytes of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	uint16_t msb_mac_addr /* 2 MSB of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	uint16_t vlan_id /* The inner vlan id (12b). Used either in vlan_in_cam for mac_valn pair or for vlan filtering */;
	uint8_t pf_id /* The pf id, for multi function mode */;
	uint8_t flags;
		#define MAC_CONFIGURATION_ENTRY_ACTION_TYPE                                          (0x1<<0) /* BitField flags	configures the action to be done in cam (used only is slow path handlers) (use enum set_mac_action_type) */
		#define MAC_CONFIGURATION_ENTRY_ACTION_TYPE_SHIFT                                    0
		#define MAC_CONFIGURATION_ENTRY_RDMA_MAC                                             (0x1<<1) /* BitField flags	If set, this MAC also belongs to RDMA client */
		#define MAC_CONFIGURATION_ENTRY_RDMA_MAC_SHIFT                                       1
		#define MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE                                  (0x3<<2) /* BitField flags	 (use enum eth_vlan_filter_mode) */
		#define MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE_SHIFT                            2
		#define MAC_CONFIGURATION_ENTRY_OVERRIDE_VLAN_REMOVAL                                (0x1<<4) /* BitField flags	BitField flags  0 - cant remove vlan 1 - can remove vlan. relevant only to everest1 */
		#define MAC_CONFIGURATION_ENTRY_OVERRIDE_VLAN_REMOVAL_SHIFT                          4
		#define MAC_CONFIGURATION_ENTRY_BROADCAST                                            (0x1<<5) /* BitField flags	BitField flags   0 - not broadcast 1 - broadcast. relevant only to everest1 */
		#define MAC_CONFIGURATION_ENTRY_BROADCAST_SHIFT                                      5
		#define MAC_CONFIGURATION_ENTRY_RESERVED1                                            (0x3<<6) /* BitField flags	 */
		#define MAC_CONFIGURATION_ENTRY_RESERVED1_SHIFT                                      6
	uint16_t reserved0;
	uint32_t clients_bit_vector /* Bit vector for the clients which should receive this MAC. */;
};

/*
 * MAC filtering configuration command
 */
struct mac_configuration_cmd
{
	struct mac_configuration_hdr hdr /* header */;
	struct mac_configuration_entry config_table[64] /* table of 64 MAC configuration entries: addresses and target table entries */;
};


/*
 * Set-MAC command type (in E1x)
 */
enum set_mac_action_type
{
	T_ETH_MAC_COMMAND_INVALIDATE,
	T_ETH_MAC_COMMAND_SET,
	MAX_SET_MAC_ACTION_TYPE};


/*
 * Ethernet TPA Modes
 */
enum tpa_mode
{
	TPA_LRO /* LRO mode TPA */,
	TPA_GRO /* GRO mode TPA */,
	MAX_TPA_MODE};


/*
 * tpa update ramrod data $$KEEP_ENDIANNESS$$
 */
struct tpa_update_ramrod_data
{
	uint8_t update_ipv4 /* none, enable or disable */;
	uint8_t update_ipv6 /* none, enable or disable */;
	uint8_t client_id /* client init flow control data */;
	uint8_t max_tpa_queues /* maximal TPA queues allowed for this client */;
	uint8_t max_sges_for_packet /* The maximal number of SGEs that can be used for one packet. depends on MTU and SGE size. must be 0 if SGEs are disabled */;
	uint8_t complete_on_both_clients /* If set and the client has different sp_client, completion will be sent to both rings */;
	uint8_t dont_verify_rings_pause_thr_flg /* If set, the rings pause thresholds will not be verified by firmware. */;
	uint8_t tpa_mode /* TPA mode to use (LRO or GRO) */;
	uint16_t sge_buff_size /* Size of the buffers pointed by SGEs */;
	uint16_t max_agg_size /* maximal size for the aggregated TPA packets, reprted by the host */;
	uint32_t sge_page_base_lo /* The address to fetch the next sges from (low) */;
	uint32_t sge_page_base_hi /* The address to fetch the next sges from (high) */;
	uint16_t sge_pause_thr_low /* number of remaining sges under which, we send pause message */;
	uint16_t sge_pause_thr_high /* number of remaining sges above which, we send un-pause message */;
};


/*
 * approximate-match multicast filtering for E1H per function in Tstorm
 */
struct tstorm_eth_approximate_match_multicast_filtering
{
	uint32_t mcast_add_hash_bit_array[8] /* Bit array for multicast hash filtering.Each bit supports a hash function result if to accept this multicast dst address. */;
};


/*
 * Common configuration parameters per function in Tstorm $$KEEP_ENDIANNESS$$
 */
struct tstorm_eth_function_common_config
{
	uint16_t config_flags;
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY                        (0x1<<0) /* BitField config_flagsGeneral configuration flags	configuration of the port RSS IpV4 2-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY_SHIFT                  0
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY                    (0x1<<1) /* BitField config_flagsGeneral configuration flags	configuration of the port RSS IpV4 4-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY_SHIFT              1
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY                        (0x1<<2) /* BitField config_flagsGeneral configuration flags	configuration of the port RSS IpV4 2-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY_SHIFT                  2
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY                    (0x1<<3) /* BitField config_flagsGeneral configuration flags	configuration of the port RSS IpV6 4-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY_SHIFT              3
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE                                   (0x7<<4) /* BitField config_flagsGeneral configuration flags	RSS mode of operation (use enum eth_rss_mode) */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE_SHIFT                             4
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_VLAN_FILTERING_ENABLE                      (0x1<<7) /* BitField config_flagsGeneral configuration flags	0 - Dont filter by vlan, 1 - Filter according to the vlans specificied in mac_filter_config */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_VLAN_FILTERING_ENABLE_SHIFT                7
		#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0                                (0xFF<<8) /* BitField config_flagsGeneral configuration flags	 */
		#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0_SHIFT                          8
	uint8_t rss_result_mask /* The mask for the lower byte of RSS result - defines which section of the indirection table will be used. To enable all table put here 0x7F */;
	uint8_t reserved1;
	uint16_t vlan_id[2] /* VLANs of this function. VLAN filtering is determine according to vlan_filtering_enable. */;
};


/*
 * MAC filtering configuration parameters per port in Tstorm $$KEEP_ENDIANNESS$$
 */
struct tstorm_eth_mac_filter_config
{
	uint32_t ucast_drop_all /* bit vector in which the clients which drop all unicast packets are set */;
	uint32_t ucast_accept_all /* bit vector in which clients that accept all unicast packets are set */;
	uint32_t mcast_drop_all /* bit vector in which the clients which drop all multicast packets are set */;
	uint32_t mcast_accept_all /* bit vector in which clients that accept all multicast packets are set */;
	uint32_t bcast_accept_all /* bit vector in which clients that accept all broadcast packets are set */;
	uint32_t vlan_filter[2] /* bit vector for VLAN filtering. Clients which enforce filtering of vlan[x] should be marked in vlan_filter[x]. In E1 only vlan_filter[1] is checked. The primary vlan is taken from the CAM target table. */;
	uint32_t unmatched_unicast /* bit vector in which clients that accept unmatched unicast packets are set */;
};


/*
 * tx only queue init ramrod data $$KEEP_ENDIANNESS$$
 */
struct tx_queue_init_ramrod_data
{
	struct client_init_general_data general /* client init general data */;
	struct client_init_tx_data tx /* client init tx data */;
};


/*
 * Three RX producers for ETH
 */
struct ustorm_eth_rx_producers
{
#if defined(__BIG_ENDIAN)
	uint16_t bd_prod /* Producer of the RX BD ring */;
	uint16_t cqe_prod /* Producer of the RX CQE ring */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cqe_prod /* Producer of the RX CQE ring */;
	uint16_t bd_prod /* Producer of the RX BD ring */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved;
	uint16_t sge_prod /* Producer of the RX SGE ring */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sge_prod /* Producer of the RX SGE ring */;
	uint16_t reserved;
#endif
};


/*
 * ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_info
{
	uint16_t aborted_task_id /* Task ID to be aborted */;
	uint16_t reserved0;
	uint32_t reserved1;
};


/*
 * Fixed size structure in order to plant it in Union structure $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_rsp_union
{
	uint8_t r_ctl /* Only R_CTL part of the FC header in ABTS ACC or BA_RJT messages is placed */;
	uint8_t rsrv[3];
	uint32_t abts_rsp_payload[7] /* The payload of  the ABTS ACC (12B) or the BA_RJT (4B) */;
};


/*
 * 4 regs size $$KEEP_ENDIANNESS$$
 */
struct fcoe_bd_ctx
{
	uint32_t buf_addr_hi /* Higher buffer host address */;
	uint32_t buf_addr_lo /* Lower buffer host address */;
	uint16_t buf_len /* Buffer length (in bytes) */;
	uint16_t rsrv0;
	uint16_t flags /* BD flags */;
	uint16_t rsrv1;
};


/*
 * FCoE cached sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_cached_sge_ctx
{
	struct regpair_t cur_buf_addr /* Current buffer address (in initialization it is the first cached buffer) */;
	uint16_t cur_buf_rem /* Remaining data in current buffer (in bytes) */;
	uint16_t second_buf_rem /* Remaining data in second buffer (in bytes) */;
	struct regpair_t second_buf_addr /* Second cached buffer address */;
};


/*
 * Cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_cleanup_info
{
	uint16_t cleaned_task_id /* Task ID to be cleaned */;
	uint16_t rolled_tx_seq_cnt /* Tx sequence count */;
	uint32_t rolled_tx_data_offset /* Tx data offset */;
};


/*
 * Fcp RSP flags $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_flags
{
	uint8_t flags;
		#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID                                         (0x1<<0) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_SHIFT                                   0
		#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID                                         (0x1<<1) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_SHIFT                                   1
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER                                            (0x1<<2) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_SHIFT                                      2
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER                                           (0x1<<3) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_SHIFT                                     3
		#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ                                              (0x1<<4) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_SHIFT                                        4
		#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS                                            (0x7<<5) /* BitField flags	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_SHIFT                                      5
};

/*
 * Fcp RSP payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_payload
{
	struct regpair_t reserved0;
	uint32_t fcp_resid;
	uint8_t scsi_status_code;
	struct fcoe_fcp_rsp_flags fcp_flags;
	uint16_t retry_delay_timer;
	uint32_t fcp_rsp_len;
	uint32_t fcp_sns_len;
};

/*
 * Fixed size structure in order to plant it in Union structure $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_union
{
	struct fcoe_fcp_rsp_payload payload;
	struct regpair_t reserved0;
};

/*
 * FC header $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_hdr
{
	uint8_t s_id[3];
	uint8_t cs_ctl;
	uint8_t d_id[3];
	uint8_t r_ctl;
	uint16_t seq_cnt;
	uint8_t df_ctl;
	uint8_t seq_id;
	uint8_t f_ctl[3];
	uint8_t type;
	uint32_t parameters;
	uint16_t rx_id;
	uint16_t ox_id;
};

/*
 * FC header union $$KEEP_ENDIANNESS$$
 */
struct fcoe_mp_rsp_union
{
	struct fcoe_fc_hdr fc_hdr /* FC header copied into task context (middle path flows) */;
	uint32_t mp_payload_len /* Length of the MP payload that was placed */;
	uint32_t rsrv;
};

/*
 * Completion information $$KEEP_ENDIANNESS$$
 */
union fcoe_comp_flow_info
{
	struct fcoe_fcp_rsp_union fcp_rsp /* FCP_RSP payload */;
	struct fcoe_abts_rsp_union abts_rsp /* ABTS ACC R_CTL part of the FC header ABTS ACC or BA_RJT payload frame */;
	struct fcoe_mp_rsp_union mp_rsp /* FC header copied into task context (middle path flows) */;
	uint32_t opaque[8];
};


/*
 * External ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_abts_info
{
	uint32_t rsrv0[6];
	struct fcoe_abts_info ctx /* ABTS information. Initialized by Xstorm */;
};


/*
 * External cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_cleanup_info
{
	uint32_t rsrv0[6];
	struct fcoe_cleanup_info ctx /* Cleanup information */;
};


/*
 * Fcoe FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_fw_tx_seq_ctx
{
	uint32_t data_offset /* The amount of data transmitted so far (equal to FCP_DATA PARAMETER field) */;
	uint16_t seq_cnt /* The last SEQ_CNT transmitted */;
	uint16_t rsrv0;
};

/*
 * Fcoe external FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_fw_tx_seq_ctx
{
	uint32_t rsrv0[6];
	struct fcoe_fw_tx_seq_ctx ctx /* TX sequence context */;
};


/*
 * FCoE multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_mul_sges_ctx
{
	struct regpair_t cur_sge_addr /* Current BD address */;
	uint16_t cur_sge_off /* Offset in current BD (in bytes) */;
	uint8_t cur_sge_idx /* Current BD index in BD list */;
	uint8_t sgl_size /* Total number of BDs */;
};

/*
 * FCoE external multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_mul_sges_ctx
{
	struct fcoe_mul_sges_ctx mul_sgl /* SGL context */;
	struct regpair_t rsrv0;
};


/*
 * FCP CMD payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_cmd_payload
{
	uint32_t opaque[8];
};


/*
 * Fcp xfr rdy payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_xfr_rdy_payload
{
	uint32_t burst_len;
	uint32_t data_ro;
};


/*
 * FC frame $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_frame
{
	struct fcoe_fc_hdr fc_hdr;
	uint32_t reserved0[2];
};


/*
 * FCoE KCQ CQE parameters $$KEEP_ENDIANNESS$$
 */
union fcoe_kcqe_params
{
	uint32_t reserved0[4];
};

/*
 * FCoE KCQ CQE $$KEEP_ENDIANNESS$$
 */
struct fcoe_kcqe
{
	uint32_t fcoe_conn_id /* Drivers connection ID (only 16 bits are used) */;
	uint32_t completion_status /* 0=command completed successfully, 1=command failed */;
	uint32_t fcoe_conn_context_id /* Context ID of the FCoE connection */;
	union fcoe_kcqe_params params /* command-specific parameters */;
	uint16_t qe_self_seq /* Self identifying sequence number */;
	uint8_t op_code /* FCoE KCQ opcode */;
	uint8_t flags;
		#define FCOE_KCQE_RESERVED0                                                          (0x7<<0) /* BitField flags	 */
		#define FCOE_KCQE_RESERVED0_SHIFT                                                    0
		#define FCOE_KCQE_RAMROD_COMPLETION                                                  (0x1<<3) /* BitField flags	Everest only - indicates whether this KCQE is a ramrod completion */
		#define FCOE_KCQE_RAMROD_COMPLETION_SHIFT                                            3
		#define FCOE_KCQE_LAYER_CODE                                                         (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5,iSCSI,FCoE) */
		#define FCOE_KCQE_LAYER_CODE_SHIFT                                                   4
		#define FCOE_KCQE_LINKED_WITH_NEXT                                                   (0x1<<7) /* BitField flags	Indicates whether this KCQE is linked with the next KCQE */
		#define FCOE_KCQE_LINKED_WITH_NEXT_SHIFT                                             7
};


/*
 * FCoE KWQE header $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_header
{
	uint8_t op_code /* FCoE KWQE opcode */;
	uint8_t flags;
		#define FCOE_KWQE_HEADER_RESERVED0                                                   (0xF<<0) /* BitField flags	 */
		#define FCOE_KWQE_HEADER_RESERVED0_SHIFT                                             0
		#define FCOE_KWQE_HEADER_LAYER_CODE                                                  (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5) */
		#define FCOE_KWQE_HEADER_LAYER_CODE_SHIFT                                            4
		#define FCOE_KWQE_HEADER_RESERVED1                                                   (0x1<<7) /* BitField flags	 */
		#define FCOE_KWQE_HEADER_RESERVED1_SHIFT                                             7
};

/*
 * FCoE firmware init request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init1
{
	uint16_t num_tasks /* Number of tasks in global task list */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t task_list_pbl_addr_lo /* Lower 32-bit of Task List page table */;
	uint32_t task_list_pbl_addr_hi /* Higher 32-bit of Task List page table */;
	uint32_t dummy_buffer_addr_lo /* Lower 32-bit of dummy buffer */;
	uint32_t dummy_buffer_addr_hi /* Higher 32-bit of dummy buffer */;
	uint16_t sq_num_wqes /* Number of entries in the Send Queue */;
	uint16_t rq_num_wqes /* Number of entries in the Receive Queue */;
	uint16_t rq_buffer_log_size /* Log of the size of a single buffer (entry) in the RQ */;
	uint16_t cq_num_wqes /* Number of entries in the Completion Queue */;
	uint16_t mtu /* Max transmission unit */;
	uint8_t num_sessions_log /* Log of the number of sessions */;
	uint8_t flags;
		#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE                                                (0xF<<0) /* BitField flags	log of page size value */
		#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE_SHIFT                                          0
		#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC                                     (0x7<<4) /* BitField flags	 */
		#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC_SHIFT                               4
		#define FCOE_KWQE_INIT1_CLASSIFY_FAILED_ALLOWED                                      (0x1<<7) /* BitField flags	Special MF mode where classification failure indication from HW is allowed */
		#define FCOE_KWQE_INIT1_CLASSIFY_FAILED_ALLOWED_SHIFT                                7
};

/*
 * FCoE firmware init request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init2
{
	uint8_t hsi_major_version /* Implies on a change broken previous HSI */;
	uint8_t hsi_minor_version /* Implies on a change which does not broken previous HSI */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t hash_tbl_pbl_addr_lo /* Lower 32-bit of Hash table PBL */;
	uint32_t hash_tbl_pbl_addr_hi /* Higher 32-bit of Hash table PBL */;
	uint32_t t2_hash_tbl_addr_lo /* Lower 32-bit of T2 Hash table */;
	uint32_t t2_hash_tbl_addr_hi /* Higher 32-bit of T2 Hash table */;
	uint32_t t2_ptr_hash_tbl_addr_lo /* Lower 32-bit of T2 ptr Hash table */;
	uint32_t t2_ptr_hash_tbl_addr_hi /* Higher 32-bit of T2 ptr Hash table */;
	uint32_t free_list_count /* T2 free list count */;
};

/*
 * FCoE firmware init request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init3
{
	uint16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t error_bit_map_lo /* 32 lower bits of error bitmap: 1=error, 0=warning */;
	uint32_t error_bit_map_hi /* 32 upper bits of error bitmap: 1=error, 0=warning */;
	uint8_t perf_config /* 0= no performance acceleration, 1=cached connection, 2=cached tasks, 3=both */;
	uint8_t reserved21[3];
	uint32_t reserved2[4];
};

/*
 * FCoE connection offload request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload1
{
	uint16_t fcoe_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t sq_addr_lo /* Lower 32-bit of SQ */;
	uint32_t sq_addr_hi /* Higher 32-bit of SQ */;
	uint32_t rq_pbl_addr_lo /* Lower 32-bit of RQ page table */;
	uint32_t rq_pbl_addr_hi /* Higher 32-bit of RQ page table */;
	uint32_t rq_first_pbe_addr_lo /* Lower 32-bit of first RQ pbe */;
	uint32_t rq_first_pbe_addr_hi /* Higher 32-bit of first RQ pbe */;
	uint16_t rq_prod /* Initial RQ producer */;
	uint16_t reserved0;
};

/*
 * FCoE connection offload request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload2
{
	uint16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t cq_addr_lo /* Lower 32-bit of CQ */;
	uint32_t cq_addr_hi /* Higher 32-bit of CQ */;
	uint32_t xferq_addr_lo /* Lower 32-bit of XFERQ */;
	uint32_t xferq_addr_hi /* Higher 32-bit of XFERQ */;
	uint32_t conn_db_addr_lo /* Lower 32-bit of Conn DB (RQ prod and CQ arm bit) */;
	uint32_t conn_db_addr_hi /* Higher 32-bit of Conn DB (RQ prod and CQ arm bit) */;
	uint32_t reserved1;
};

/*
 * FCoE connection offload request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload3
{
	uint16_t vlan_tag;
		#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID                                              (0xFFF<<0) /* BitField vlan_tag	Vlan id */
		#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID_SHIFT                                        0
		#define FCOE_KWQE_CONN_OFFLOAD3_CFI                                                  (0x1<<12) /* BitField vlan_tag	Canonical format indicator */
		#define FCOE_KWQE_CONN_OFFLOAD3_CFI_SHIFT                                            12
		#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY                                             (0x7<<13) /* BitField vlan_tag	Vlan priority */
		#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY_SHIFT                                       13
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint8_t s_id[3] /* Source ID, received during FLOGI */;
	uint8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by target, received during PLOGI */;
	uint8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	uint8_t flags;
		#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS                                     (0x1<<0) /* BitField flags	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS_SHIFT                               0
		#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES                                        (0x1<<1) /* BitField flags	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES_SHIFT                                  1
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT                                  (0x1<<2) /* BitField flags	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT_SHIFT                            2
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ                                           (0x1<<3) /* BitField flags	Confirmation request supported */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ_SHIFT                                     3
		#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID                                          (0x1<<4) /* BitField flags	REC allowed */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID_SHIFT                                    4
		#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID                                           (0x1<<5) /* BitField flags	Class 2 valid, received during PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID_SHIFT                                     5
		#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0                                              (0x1<<6) /* BitField flags	ACK_0 capability supporting by target, received furing PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0_SHIFT                                        6
		#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG                                          (0x1<<7) /* BitField flags	Is inner vlan exist */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG_SHIFT                                    7
	uint32_t reserved;
	uint32_t confq_first_pbe_addr_lo /* The first page used when handling CONFQ - low address */;
	uint32_t confq_first_pbe_addr_hi /* The first page used when handling CONFQ - high address */;
	uint16_t tx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by target, received during PLOGI */;
	uint16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
	uint16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
	uint8_t rx_max_conc_seqs_c3 /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
	uint8_t rx_open_seqs_exch_c3 /* Maximum Open Sequences per Exchange for Class 3 supported by us, sent during PLOGI */;
};

/*
 * FCoE connection offload request 4 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload4
{
	uint8_t e_d_tov_timer_val /* E_D_TOV timer value in milliseconds/20, negotiated in PLOGI */;
	uint8_t reserved2;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint8_t src_mac_addr_lo[2] /* Lower 16-bit of source MAC address  */;
	uint8_t src_mac_addr_mid[2] /* Mid 16-bit of source MAC address  */;
	uint8_t src_mac_addr_hi[2] /* Higher 16-bit of source MAC address */;
	uint8_t dst_mac_addr_hi[2] /* Higher 16-bit of destination MAC address */;
	uint8_t dst_mac_addr_lo[2] /* Lower 16-bit destination MAC address */;
	uint8_t dst_mac_addr_mid[2] /* Mid 16-bit destination MAC address */;
	uint32_t lcq_addr_lo /* Lower 32-bit of LCQ */;
	uint32_t lcq_addr_hi /* Higher 32-bit of LCQ */;
	uint32_t confq_pbl_base_addr_lo /* CONFQ PBL low address */;
	uint32_t confq_pbl_base_addr_hi /* CONFQ PBL high address */;
};

/*
 * FCoE connection enable request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_enable_disable
{
	uint16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint8_t src_mac_addr_lo[2] /* Lower 16-bit of source MAC address (HBAs MAC address) */;
	uint8_t src_mac_addr_mid[2] /* Mid 16-bit of source MAC address (HBAs MAC address) */;
	uint8_t src_mac_addr_hi[2] /* Higher 16-bit of source MAC address (HBAs MAC address) */;
	uint16_t vlan_tag;
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID                                        (0xFFF<<0) /* BitField vlan_tagVlan tag	Vlan id */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID_SHIFT                                  0
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI                                            (0x1<<12) /* BitField vlan_tagVlan tag	Canonical format indicator */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI_SHIFT                                      12
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY                                       (0x7<<13) /* BitField vlan_tagVlan tag	Vlan priority */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY_SHIFT                                 13
	uint8_t dst_mac_addr_lo[2] /* Lower 16-bit of destination MAC address (FCFs MAC address) */;
	uint8_t dst_mac_addr_mid[2] /* Mid 16-bit of destination MAC address (FCFs MAC address) */;
	uint8_t dst_mac_addr_hi[2] /* Higher 16-bit of destination MAC address (FCFs MAC address) */;
	uint16_t reserved1;
	uint8_t s_id[3] /* Source ID, received during FLOGI */;
	uint8_t vlan_flag /* Vlan flag */;
	uint8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	uint8_t reserved3;
	uint32_t context_id /* Context ID (cid) of the connection */;
	uint32_t conn_id /* FCoE Connection ID */;
	uint32_t reserved4;
};

/*
 * FCoE connection destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_destroy
{
	uint16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t context_id /* Context ID (cid) of the connection */;
	uint32_t conn_id /* FCoE Connection ID */;
	uint32_t reserved1[5];
};

/*
 * FCoe destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_destroy
{
	uint16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t reserved1[7];
};

/*
 * FCoe statistics request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_stat
{
	uint16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	uint32_t stat_params_addr_lo /* Statistics host address */;
	uint32_t stat_params_addr_hi /* Statistics host address */;
	uint32_t reserved1[5];
};

/*
 * FCoE KWQ WQE $$KEEP_ENDIANNESS$$
 */
union fcoe_kwqe
{
	struct fcoe_kwqe_init1 init1;
	struct fcoe_kwqe_init2 init2;
	struct fcoe_kwqe_init3 init3;
	struct fcoe_kwqe_conn_offload1 conn_offload1;
	struct fcoe_kwqe_conn_offload2 conn_offload2;
	struct fcoe_kwqe_conn_offload3 conn_offload3;
	struct fcoe_kwqe_conn_offload4 conn_offload4;
	struct fcoe_kwqe_conn_enable_disable conn_enable_disable;
	struct fcoe_kwqe_conn_destroy conn_destroy;
	struct fcoe_kwqe_destroy destroy;
	struct fcoe_kwqe_stat statistics;
};


/*
 * TX SGL context $$KEEP_ENDIANNESS$$
 */
union fcoe_sgl_union_ctx
{
	struct fcoe_cached_sge_ctx cached_sge /* Cached SGEs context */;
	struct fcoe_ext_mul_sges_ctx sgl /* SGL context */;
	uint32_t opaque[5];
};

/*
 * Data-In/ELS/BLS information $$KEEP_ENDIANNESS$$
 */
struct fcoe_read_flow_info
{
	union fcoe_sgl_union_ctx sgl_ctx /* The SGL that would be used for data placement (20 bytes) */;
	uint32_t rsrv0[3];
};


/*
 * Fcoe stat context $$KEEP_ENDIANNESS$$
 */
struct fcoe_s_stat_ctx
{
	uint8_t flags;
		#define FCOE_S_STAT_CTX_ACTIVE                                                       (0x1<<0) /* BitField flags	Active Sequence indication (0 - not avtive; 1 - active) */
		#define FCOE_S_STAT_CTX_ACTIVE_SHIFT                                                 0
		#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND                                           (0x1<<1) /* BitField flags	Abort Sequence requested indication */
		#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND_SHIFT                                     1
		#define FCOE_S_STAT_CTX_ABTS_PERFORMED                                               (0x1<<2) /* BitField flags	ABTS (on Sequence) protocol complete indication (0 - not completed; 1 -completed by Recipient) */
		#define FCOE_S_STAT_CTX_ABTS_PERFORMED_SHIFT                                         2
		#define FCOE_S_STAT_CTX_SEQ_TIMEOUT                                                  (0x1<<3) /* BitField flags	E_D_TOV timeout indication */
		#define FCOE_S_STAT_CTX_SEQ_TIMEOUT_SHIFT                                            3
		#define FCOE_S_STAT_CTX_P_RJT                                                        (0x1<<4) /* BitField flags	P_RJT transmitted indication */
		#define FCOE_S_STAT_CTX_P_RJT_SHIFT                                                  4
		#define FCOE_S_STAT_CTX_ACK_EOFT                                                     (0x1<<5) /* BitField flags	ACK (EOFt) transmitted indication (0 - not tranmitted; 1 - transmitted) */
		#define FCOE_S_STAT_CTX_ACK_EOFT_SHIFT                                               5
		#define FCOE_S_STAT_CTX_RSRV1                                                        (0x3<<6) /* BitField flags	 */
		#define FCOE_S_STAT_CTX_RSRV1_SHIFT                                                  6
};

/*
 * Fcoe rx seq context $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_seq_ctx
{
	uint8_t seq_id /* The Sequence ID */;
	struct fcoe_s_stat_ctx s_stat /* The Sequence status */;
	uint16_t seq_cnt /* The lowest SEQ_CNT received for the Sequence */;
	uint32_t low_exp_ro /* Report on the offset at the beginning of the Sequence */;
	uint32_t high_exp_ro /* The highest expected relative offset. The next buffer offset to be received in case of XFER_RDY or in FCP_DATA */;
};


/*
 * FCoE RX statistics parameters section#0 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section0
{
	uint32_t fcoe_rx_pkt_cnt /* Number of FCoE packets that were legally received */;
	uint32_t fcoe_rx_byte_cnt /* Number of FCoE bytes that were legally received */;
};


/*
 * FCoE RX statistics parameters section#1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section1
{
	uint32_t fcoe_ver_cnt /* Number of packets with wrong FCoE version */;
	uint32_t fcoe_rx_drop_pkt_cnt /* Number of FCoE packets that were dropped */;
};


/*
 * FCoE RX statistics parameters section#2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_stat_params_section2
{
	uint32_t fc_crc_cnt /* Number of packets with FC CRC error */;
	uint32_t eofa_del_cnt /* Number of packets with EOFa delimiter */;
	uint32_t miss_frame_cnt /* Number of missing packets */;
	uint32_t seq_timeout_cnt /* Number of sequence timeout expirations (E_D_TOV) */;
	uint32_t drop_seq_cnt /* Number of Sequences that were sropped */;
	uint32_t fcoe_rx_drop_pkt_cnt /* Number of FCoE packets that were dropped */;
	uint32_t fcp_rx_pkt_cnt /* Number of FCP packets that were legally received */;
	uint32_t reserved0;
};


/*
 * Fcoe rx_wr union context $$KEEP_ENDIANNESS$$
 */
union fcoe_rx_wr_union_ctx
{
	struct fcoe_read_flow_info read_info /* Data-In/ELS/BLS information */;
	union fcoe_comp_flow_info comp_info /* Completion information */;
	uint32_t opaque[8];
};


/*
 * FCoE SQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_sqe
{
	uint16_t wqe;
		#define FCOE_SQE_TASK_ID                                                             (0x7FFF<<0) /* BitField wqe	The task ID (OX_ID) to be processed */
		#define FCOE_SQE_TASK_ID_SHIFT                                                       0
		#define FCOE_SQE_TOGGLE_BIT                                                          (0x1<<15) /* BitField wqe	Toggle bit updated by the driver */
		#define FCOE_SQE_TOGGLE_BIT_SHIFT                                                    15
};


/*
 * FCoE TX statistics parameters $$KEEP_ENDIANNESS$$
 */
struct fcoe_tx_stat_params
{
	uint32_t fcoe_tx_pkt_cnt /* Number of transmitted FCoE packets */;
	uint32_t fcoe_tx_byte_cnt /* Number of transmitted FCoE bytes */;
	uint32_t fcp_tx_pkt_cnt /* Number of transmitted FCP packets */;
	uint32_t reserved0;
};

/*
 * FCoE statistics parameters $$KEEP_ENDIANNESS$$
 */
struct fcoe_statistics_params
{
	struct fcoe_tx_stat_params tx_stat /* FCoE TX statistics parameters */;
	struct fcoe_rx_stat_params_section0 rx_stat0 /* FCoE RX statistics parameters section#0 */;
	struct fcoe_rx_stat_params_section1 rx_stat1 /* FCoE RX statistics parameters section#1 */;
	struct fcoe_rx_stat_params_section2 rx_stat2 /* FCoE RX statistics parameters section#2 */;
};


/*
 * 14 regs $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_only
{
	union fcoe_sgl_union_ctx sgl_ctx /* TX SGL context */;
	uint32_t rsrv0;
};

/*
 * 32 bytes (8 regs) used for TX only purposes $$KEEP_ENDIANNESS$$
 */
union fcoe_tx_wr_rx_rd_union_ctx
{
	struct fcoe_fc_frame tx_frame /* Middle-path/ABTS/Data-Out information */;
	struct fcoe_fcp_cmd_payload fcp_cmd /* FCP_CMD payload */;
	struct fcoe_ext_cleanup_info cleanup /* Task ID to be cleaned */;
	struct fcoe_ext_abts_info abts /* Task ID to be aborted */;
	struct fcoe_ext_fw_tx_seq_ctx tx_seq /* TX sequence information */;
	uint32_t opaque[8];
};

/*
 * tce_tx_wr_rx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd_const
{
	uint8_t init_flags;
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE                                         (0x7<<0) /* BitField init_flags	Task type - Write / Read / Middle / Unsolicited / ABTS / Cleanup */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE_SHIFT                                   0
		#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE                                          (0x1<<3) /* BitField init_flags	Tape/Disk device indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE_SHIFT                                    3
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE                                        (0x1<<4) /* BitField init_flags	Class 3/2 indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE_SHIFT                                  4
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE                                        (0x3<<5) /* BitField init_flags	Num of cached sge (0 - not cached sge) */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE_SHIFT                                  5
		#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV                                   (0x1<<7) /* BitField init_flags	Support REC_TOV flag, for FW use only */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV_SHIFT                             7
	uint8_t tx_flags;
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID                                          (0x1<<0) /* BitField tx_flagsBoth TX and RX processing could read but only the TX could write	Indication of TX valid task */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID_SHIFT                                    0
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE                                          (0xF<<1) /* BitField tx_flagsBoth TX and RX processing could read but only the TX could write	The TX state of the task */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE_SHIFT                                    1
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1                                             (0x1<<5) /* BitField tx_flagsBoth TX and RX processing could read but only the TX could write	 */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1_SHIFT                                       5
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT                                       (0x1<<6) /* BitField tx_flagsBoth TX and RX processing could read but only the TX could write	TX Sequence initiative indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT_SHIFT                                 6
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_COMP_TRNS                                      (0x1<<7) /* BitField tx_flagsBoth TX and RX processing could read but only the TX could write	Compelted full tranmission of this task */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_COMP_TRNS_SHIFT                                7
	uint16_t rsrv3;
	uint32_t verify_tx_seq /* Sequence counter snapshot in order to verify target did not send FCP_RSP before the actual transmission of PBF from the SGL */;
};

/*
 * tce_tx_wr_rx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd
{
	union fcoe_tx_wr_rx_rd_union_ctx union_ctx /* 32 (8 regs) bytes used for TX only purposes */;
	struct fcoe_tce_tx_wr_rx_rd_const const_ctx /* Constant TX_WR_RX_RD */;
};

/*
 * tce_rx_wr_tx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_const
{
	uint32_t data_2_trns /* The maximum amount of data that would be transferred in this task */;
	uint32_t init_flags;
		#define FCOE_TCE_RX_WR_TX_RD_CONST_CID                                               (0xFFFFFF<<0) /* BitField init_flags	The CID of the connection (used by the CHIP) */
		#define FCOE_TCE_RX_WR_TX_RD_CONST_CID_SHIFT                                         0
		#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0                                             (0xFF<<24) /* BitField init_flags	 */
		#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0_SHIFT                                       24
};

/*
 * tce_rx_wr_tx_rd_var $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_var
{
	uint16_t rx_flags;
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1                                               (0xF<<0) /* BitField rx_flags	 */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1_SHIFT                                         0
		#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE                                          (0x7<<4) /* BitField rx_flags	The number of RQ WQEs that were consumed (for sense data only) */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE_SHIFT                                    4
		#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ                                            (0x1<<7) /* BitField rx_flags	Confirmation request indication */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ_SHIFT                                      7
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE                                            (0xF<<8) /* BitField rx_flags	The RX state of the task */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE_SHIFT                                      8
		#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME                                     (0x1<<12) /* BitField rx_flags	Indication on expecting to receive the first frame from target */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME_SHIFT                               12
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT                                         (0x1<<13) /* BitField rx_flags	RX Sequence initiative indication */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT_SHIFT                                   13
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2                                               (0x1<<14) /* BitField rx_flags	 */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2_SHIFT                                         14
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID                                            (0x1<<15) /* BitField rx_flags	Indication of RX valid task */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID_SHIFT                                      15
	uint16_t rx_id /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
	struct fcoe_fcp_xfr_rdy_payload fcp_xfr_rdy /* Data-In/ELS/BLS information */;
};

/*
 * tce_rx_wr_tx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd
{
	struct fcoe_tce_rx_wr_tx_rd_const const_ctx /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
	struct fcoe_tce_rx_wr_tx_rd_var var_ctx /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
};

/*
 * tce_rx_only $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_only
{
	struct fcoe_rx_seq_ctx rx_seq_ctx /* The context of current receiving Sequence */;
	union fcoe_rx_wr_union_ctx union_ctx /* Read flow info/ Completion flow info */;
};

/*
 * task_ctx_entry $$KEEP_ENDIANNESS$$
 */
struct fcoe_task_ctx_entry
{
	struct fcoe_tce_tx_only txwr_only /* TX processing shall be the only one to read/write to this section */;
	struct fcoe_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX shall read from this section */;
	struct fcoe_tce_rx_wr_tx_rd rxwr_txrd /* RX processing shall write and TX shall read from this section */;
	struct fcoe_tce_rx_only rxwr_only /* RX processing shall be the only one to read/write to this section */;
};


/*
 * FCoE XFRQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_xfrqe
{
	uint16_t wqe;
		#define FCOE_XFRQE_TASK_ID                                                           (0x7FFF<<0) /* BitField wqe	The task ID (OX_ID) to be processed */
		#define FCOE_XFRQE_TASK_ID_SHIFT                                                     0
		#define FCOE_XFRQE_TOGGLE_BIT                                                        (0x1<<15) /* BitField wqe	Toggle bit updated by the driver */
		#define FCOE_XFRQE_TOGGLE_BIT_SHIFT                                                  15
};


/*
 * Cached SGEs $$KEEP_ENDIANNESS$$
 */
struct common_fcoe_sgl
{
	struct fcoe_bd_ctx sge[3];
};


/*
 * FCoE SQ\XFRQ element
 */
struct fcoe_cached_wqe
{
	struct fcoe_sqe sqe /* SQ WQE */;
	struct fcoe_xfrqe xfrqe /* XFRQ WQE */;
};


/*
 * FCoE connection enable\disable params passed by driver to FW in FCoE enable ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_conn_enable_disable_ramrod_params
{
	struct fcoe_kwqe_conn_enable_disable enable_disable_kwqe;
};


/*
 * FCoE connection offload params passed by driver to FW in FCoE offload ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_conn_offload_ramrod_params
{
	struct fcoe_kwqe_conn_offload1 offload_kwqe1;
	struct fcoe_kwqe_conn_offload2 offload_kwqe2;
	struct fcoe_kwqe_conn_offload3 offload_kwqe3;
	struct fcoe_kwqe_conn_offload4 offload_kwqe4;
};


struct ustorm_fcoe_mng_ctx
{
#if defined(__BIG_ENDIAN)
	uint8_t mid_seq_proc_flag /* Middle Sequence received processing */;
	uint8_t tce_in_cam_flag /* TCE in CAM indication */;
	uint8_t tce_on_ior_flag /* TCE on IOR indication (TCE on IORs but not necessarily in CAM) */;
	uint8_t en_cached_tce_flag /* TCE cached functionality enabled indication */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t en_cached_tce_flag /* TCE cached functionality enabled indication */;
	uint8_t tce_on_ior_flag /* TCE on IOR indication (TCE on IORs but not necessarily in CAM) */;
	uint8_t tce_in_cam_flag /* TCE in CAM indication */;
	uint8_t mid_seq_proc_flag /* Middle Sequence received processing */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t tce_cam_addr /* CAM address of task context */;
	uint8_t cached_conn_flag /* Cached locked connection indication */;
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t cached_conn_flag /* Cached locked connection indication */;
	uint8_t tce_cam_addr /* CAM address of task context */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t dma_tce_ram_addr /* RAM address of task context when executing DMA operations (read/write) */;
	uint16_t tce_ram_addr /* RAM address of task context (might be in cached table or in scratchpad) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t tce_ram_addr /* RAM address of task context (might be in cached table or in scratchpad) */;
	uint16_t dma_tce_ram_addr /* RAM address of task context when executing DMA operations (read/write) */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t ox_id /* Last OX_ID that has been used */;
	uint16_t wr_done_seq /* Last task write done in the specific connection */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t wr_done_seq /* Last task write done in the specific connection */;
	uint16_t ox_id /* Last OX_ID that has been used */;
#endif
	struct regpair_t task_addr /* Last task address in used */;
};

/*
 * Parameters initialized during offloaded according to FLOGI/PLOGI/PRLI and used in FCoE context section
 */
struct ustorm_fcoe_params
{
#if defined(__BIG_ENDIAN)
	uint16_t fcoe_conn_id /* The connection ID that would be used by driver to identify the conneciton */;
	uint16_t flags;
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS                                          (0x1<<0) /* BitField flags	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT                                    0
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES                                             (0x1<<1) /* BitField flags	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT                                       1
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT                                       (0x1<<2) /* BitField flags	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT                                 2
		#define USTORM_FCOE_PARAMS_B_CONF_REQ                                                (0x1<<3) /* BitField flags	Confirmation request supported */
		#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT                                          3
		#define USTORM_FCOE_PARAMS_B_REC_VALID                                               (0x1<<4) /* BitField flags	REC allowed */
		#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT                                         4
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT                                           (0x1<<5) /* BitField flags	CQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT                                     5
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT                                         (0x1<<6) /* BitField flags	XFRQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT                                   6
		#define USTORM_FCOE_PARAMS_B_CONFQ_TOGGLE_BIT                                        (0x1<<7) /* BitField flags	CONFQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CONFQ_TOGGLE_BIT_SHIFT                                  7
		#define USTORM_FCOE_PARAMS_RSRV0                                                     (0xFF<<8) /* BitField flags	 */
		#define USTORM_FCOE_PARAMS_RSRV0_SHIFT                                               8
#elif defined(__LITTLE_ENDIAN)
	uint16_t flags;
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS                                          (0x1<<0) /* BitField flags	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT                                    0
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES                                             (0x1<<1) /* BitField flags	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT                                       1
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT                                       (0x1<<2) /* BitField flags	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT                                 2
		#define USTORM_FCOE_PARAMS_B_CONF_REQ                                                (0x1<<3) /* BitField flags	Confirmation request supported */
		#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT                                          3
		#define USTORM_FCOE_PARAMS_B_REC_VALID                                               (0x1<<4) /* BitField flags	REC allowed */
		#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT                                         4
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT                                           (0x1<<5) /* BitField flags	CQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT                                     5
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT                                         (0x1<<6) /* BitField flags	XFRQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT                                   6
		#define USTORM_FCOE_PARAMS_B_CONFQ_TOGGLE_BIT                                        (0x1<<7) /* BitField flags	CONFQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CONFQ_TOGGLE_BIT_SHIFT                                  7
		#define USTORM_FCOE_PARAMS_RSRV0                                                     (0xFF<<8) /* BitField flags	 */
		#define USTORM_FCOE_PARAMS_RSRV0_SHIFT                                               8
	uint16_t fcoe_conn_id /* The connection ID that would be used by driver to identify the conneciton */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t hc_csdm_byte_en /* Host coalescing Cstorm RAM address byte enable */;
	uint8_t func_id /* Function id */;
	uint8_t port_id /* Port id */;
	uint8_t vnic_id /* Vnic id */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t vnic_id /* Vnic id */;
	uint8_t port_id /* Port id */;
	uint8_t func_id /* Function id */;
	uint8_t hc_csdm_byte_en /* Host coalescing Cstorm RAM address byte enable */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
	uint16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
	uint16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t task_pbe_idx_off /* The first PBE for this specific task list in RAM */;
	uint8_t task_in_page_log_size /* Number of tasks in page (log 2) */;
	uint16_t rx_max_conc_seqs /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rx_max_conc_seqs /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
	uint8_t task_in_page_log_size /* Number of tasks in page (log 2) */;
	uint8_t task_pbe_idx_off /* The first PBE for this specific task list in RAM */;
#endif
};

/*
 * FCoE 16-bits index structure
 */
struct fcoe_idx16_fields
{
	uint16_t fields;
		#define FCOE_IDX16_FIELDS_IDX                                                        (0x7FFF<<0) /* BitField fields	 */
		#define FCOE_IDX16_FIELDS_IDX_SHIFT                                                  0
		#define FCOE_IDX16_FIELDS_MSB                                                        (0x1<<15) /* BitField fields	 */
		#define FCOE_IDX16_FIELDS_MSB_SHIFT                                                  15
};

/*
 * FCoE 16-bits index union
 */
union fcoe_idx16_field_union
{
	struct fcoe_idx16_fields fields /* Parameters field */;
	uint16_t val /* Global value */;
};

/*
 * Parameters required for placement according to SGL
 */
struct ustorm_fcoe_data_place_mng
{
#if defined(__BIG_ENDIAN)
	uint16_t sge_off;
	uint8_t num_sges /* Number of SGEs left to be used on context */;
	uint8_t sge_idx /* 0xFF value indicated loading SGL */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t sge_idx /* 0xFF value indicated loading SGL */;
	uint8_t num_sges /* Number of SGEs left to be used on context */;
	uint16_t sge_off;
#endif
};

/*
 * Parameters required for placement according to SGL
 */
struct ustorm_fcoe_data_place
{
	struct ustorm_fcoe_data_place_mng cached_mng /* 0xFF value indicated loading SGL */;
	struct fcoe_bd_ctx cached_sge[2];
};

/*
 * TX processing shall write and RX processing shall read from this section
 */
union fcoe_u_tce_tx_wr_rx_rd_union
{
	struct fcoe_abts_info abts /* ABTS information */;
	struct fcoe_cleanup_info cleanup /* Cleanup information */;
	struct fcoe_fw_tx_seq_ctx tx_seq_ctx /* TX sequence context */;
	uint32_t opaque[2];
};

/*
 * TX processing shall write and RX processing shall read from this section
 */
struct fcoe_u_tce_tx_wr_rx_rd
{
	union fcoe_u_tce_tx_wr_rx_rd_union union_ctx /* FW DATA_OUT/CLEANUP information */;
	struct fcoe_tce_tx_wr_rx_rd_const const_ctx /* TX processing shall write and RX shall read from this section */;
};

struct ustorm_fcoe_tce
{
	struct fcoe_u_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX shall read from this section */;
	struct fcoe_tce_rx_wr_tx_rd rxwr_txrd /* RX processing shall write and TX shall read from this section */;
	struct fcoe_tce_rx_only rxwr /* RX processing shall be the only one to read/write to this section */;
};

struct ustorm_fcoe_cache_ctx
{
	uint32_t rsrv0;
	struct ustorm_fcoe_data_place data_place;
	struct ustorm_fcoe_tce tce /* Task context */;
};

/*
 * Ustorm FCoE Storm Context
 */
struct ustorm_fcoe_st_context
{
	struct ustorm_fcoe_mng_ctx mng_ctx /* Managing the processing of the flow */;
	struct ustorm_fcoe_params fcoe_params /* Align to 128 bytes */;
	struct regpair_t cq_base_addr /* CQ current page host address */;
	struct regpair_t rq_pbl_base /* PBL host address for RQ */;
	struct regpair_t rq_cur_page_addr /* RQ current page host address */;
	struct regpair_t confq_pbl_base_addr /* Base address of the CONFQ page list */;
	struct regpair_t conn_db_base /* Connection data base address in host memory where RQ producer and CQ arm bit reside in */;
	struct regpair_t xfrq_base_addr /* XFRQ base host address */;
	struct regpair_t lcq_base_addr /* LCQ base host address */;
#if defined(__BIG_ENDIAN)
	union fcoe_idx16_field_union rq_cons /* RQ consumer advance for each RQ WQE consuming */;
	union fcoe_idx16_field_union rq_prod /* RQ producer update by driver and read by FW (should be initialized to RQ size)  */;
#elif defined(__LITTLE_ENDIAN)
	union fcoe_idx16_field_union rq_prod /* RQ producer update by driver and read by FW (should be initialized to RQ size)  */;
	union fcoe_idx16_field_union rq_cons /* RQ consumer advance for each RQ WQE consuming */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t xfrq_prod /* XFRQ producer (No consumer is needed since Q can not be overloaded) */;
	uint16_t cq_cons /* CQ consumer copy of last update from driver (Q can not be overloaded) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_cons /* CQ consumer copy of last update from driver (Q can not be overloaded) */;
	uint16_t xfrq_prod /* XFRQ producer (No consumer is needed since Q can not be overloaded) */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t lcq_cons /* lcq consumer */;
	uint16_t hc_cram_address /* Host coalescing Cstorm RAM address */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hc_cram_address /* Host coalescing Cstorm RAM address */;
	uint16_t lcq_cons /* lcq consumer */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
	uint16_t confq_prod /* CONFQ producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t confq_prod /* CONFQ producer */;
	uint16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t hc_csdm_agg_int /* Host coalescing CSDM aggregative interrupts */;
	uint8_t rsrv2;
	uint8_t available_rqes /* Available RQEs */;
	uint8_t sp_q_flush_cnt /* The remain number of queues to be flushed (in QM) */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t sp_q_flush_cnt /* The remain number of queues to be flushed (in QM) */;
	uint8_t available_rqes /* Available RQEs */;
	uint8_t rsrv2;
	uint8_t hc_csdm_agg_int /* Host coalescing CSDM aggregative interrupts */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t num_pend_tasks /* Number of pending tasks */;
	uint16_t pbf_ack_ram_addr /* PBF TX sequence ACK ram address */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t pbf_ack_ram_addr /* PBF TX sequence ACK ram address */;
	uint16_t num_pend_tasks /* Number of pending tasks */;
#endif
	struct ustorm_fcoe_cache_ctx cache_ctx /* Cached context */;
};

/*
 * The FCoE non-aggregative context of Tstorm
 */
struct tstorm_fcoe_st_context
{
	struct regpair_t reserved0;
	struct regpair_t reserved1;
};

/*
 * Ethernet context section
 */
struct xstorm_fcoe_eth_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
	uint16_t params;
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID                                      (0xFFF<<0) /* BitField params	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                0
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI                                          (0x1<<12) /* BitField params	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT                                    12
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY                                     (0x7<<13) /* BitField params	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                               13
#elif defined(__LITTLE_ENDIAN)
	uint16_t params;
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID                                      (0xFFF<<0) /* BitField params	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                0
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI                                          (0x1<<12) /* BitField params	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT                                    12
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY                                     (0x7<<13) /* BitField params	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                               13
	uint16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
#endif
};

/*
 * Flags used in FCoE context section - 1 byte
 */
struct xstorm_fcoe_context_flags
{
	uint8_t flags;
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q                                           (0x3<<0) /* BitField flags	The current queue in process */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q_SHIFT                                     0
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ                                          (0x1<<2) /* BitField flags	Middle of Sequence indication */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ_SHIFT                                    2
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_BLOCK_SQ                                         (0x1<<3) /* BitField flags	Indicates whether the SQ is blocked since we are in the middle of ABTS/Cleanup procedure */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_BLOCK_SQ_SHIFT                                   3
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT                                      (0x1<<4) /* BitField flags	REC support */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT_SHIFT                                4
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE                                        (0x1<<5) /* BitField flags	SQ toggle bit */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE_SHIFT                                  5
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE                                      (0x1<<6) /* BitField flags	XFRQ toggle bit */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE_SHIFT                                6
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_VNTAG_VLAN                                       (0x1<<7) /* BitField flags	Are we using VNTag inner vlan - in this case we have to read it on every VNTag version change */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_VNTAG_VLAN_SHIFT                                 7
};

struct xstorm_fcoe_tce
{
	struct fcoe_tce_tx_only txwr /* TX processing shall be the only one to read/write to this section */;
	struct fcoe_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX processing shall read from this section */;
};

/*
 * FCP_DATA parameters required for transmission
 */
struct xstorm_fcoe_fcp_data
{
	uint32_t io_rem /* IO remainder */;
#if defined(__BIG_ENDIAN)
	uint16_t cached_sge_off;
	uint8_t cached_num_sges /* Number of SGEs on context */;
	uint8_t cached_sge_idx /* 0xFF value indicated loading SGL */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t cached_sge_idx /* 0xFF value indicated loading SGL */;
	uint8_t cached_num_sges /* Number of SGEs on context */;
	uint16_t cached_sge_off;
#endif
	uint32_t buf_addr_hi_0 /* Higher buffer host address */;
	uint32_t buf_addr_lo_0 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	uint16_t num_of_pending_tasks /* Num of pending tasks */;
	uint16_t buf_len_0 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t buf_len_0 /* Buffer length (in bytes) */;
	uint16_t num_of_pending_tasks /* Num of pending tasks */;
#endif
	uint32_t buf_addr_hi_1 /* Higher buffer host address */;
	uint32_t buf_addr_lo_1 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	uint16_t task_pbe_idx_off /* Task pbe index offset */;
	uint16_t buf_len_1 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t buf_len_1 /* Buffer length (in bytes) */;
	uint16_t task_pbe_idx_off /* Task pbe index offset */;
#endif
	uint32_t buf_addr_hi_2 /* Higher buffer host address */;
	uint32_t buf_addr_lo_2 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	uint16_t ox_id /* OX_ID */;
	uint16_t buf_len_2 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t buf_len_2 /* Buffer length (in bytes) */;
	uint16_t ox_id /* OX_ID */;
#endif
};

/*
 * Continuation of Flags used in FCoE context section - 1 byte
 */
struct xstorm_fcoe_context_flags_cont
{
	uint8_t flags;
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_B_CONFQ_TOGGLE                                (0x1<<0) /* BitField flags	CONFQ toggle bit */
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_B_CONFQ_TOGGLE_SHIFT                          0
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_VLAN_FLAG                                     (0x1<<1) /* BitField flags	Is any inner vlan exist */
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_VLAN_FLAG_SHIFT                               1
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_RESERVED                                      (0x3F<<2) /* BitField flags	 */
		#define XSTORM_FCOE_CONTEXT_FLAGS_CONT_RESERVED_SHIFT                                2
};

/*
 * vlan configuration
 */
struct xstorm_fcoe_vlan_conf
{
	uint8_t vlan_conf;
		#define XSTORM_FCOE_VLAN_CONF_INNER_VLAN_PRIORITY                                    (0x7<<0) /* BitField vlan_conf	Original inner vlan priority */
		#define XSTORM_FCOE_VLAN_CONF_INNER_VLAN_PRIORITY_SHIFT                              0
		#define XSTORM_FCOE_VLAN_CONF_INNER_VLAN_FLAG                                        (0x1<<3) /* BitField vlan_conf	Original inner vlan flag */
		#define XSTORM_FCOE_VLAN_CONF_INNER_VLAN_FLAG_SHIFT                                  3
		#define XSTORM_FCOE_VLAN_CONF_OUTER_VLAN_PRIORITY                                    (0x7<<4) /* BitField vlan_conf	Original outer vlan priority */
		#define XSTORM_FCOE_VLAN_CONF_OUTER_VLAN_PRIORITY_SHIFT                              4
		#define XSTORM_FCOE_VLAN_CONF_RESERVED                                               (0x1<<7) /* BitField vlan_conf	 */
		#define XSTORM_FCOE_VLAN_CONF_RESERVED_SHIFT                                         7
};

/*
 * FCoE 16-bits vlan structure
 */
struct fcoe_vlan_fields
{
	uint16_t fields;
		#define FCOE_VLAN_FIELDS_VID                                                         (0xFFF<<0) /* BitField fields	 */
		#define FCOE_VLAN_FIELDS_VID_SHIFT                                                   0
		#define FCOE_VLAN_FIELDS_CLI                                                         (0x1<<12) /* BitField fields	 */
		#define FCOE_VLAN_FIELDS_CLI_SHIFT                                                   12
		#define FCOE_VLAN_FIELDS_PRI                                                         (0x7<<13) /* BitField fields	 */
		#define FCOE_VLAN_FIELDS_PRI_SHIFT                                                   13
};

/*
 * FCoE 16-bits vlan union
 */
union fcoe_vlan_field_union
{
	struct fcoe_vlan_fields fields /* Parameters field */;
	uint16_t val /* Global value */;
};

/*
 * FCoE 16-bits vlan, vif union
 */
union fcoe_vlan_vif_field_union
{
	union fcoe_vlan_field_union vlan /* Vlan */;
	uint16_t vif /* VIF */;
};

/*
 * FCoE context section
 */
struct xstorm_fcoe_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t cs_ctl /* cs ctl */;
	uint8_t s_id[3] /* Source ID, received during FLOGI */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t s_id[3] /* Source ID, received during FLOGI */;
	uint8_t cs_ctl /* cs ctl */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t rctl /* rctl */;
	uint8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	uint8_t rctl /* rctl */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
	uint16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
	uint16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
#endif
	uint32_t lcq_prod /* LCQ producer value */;
#if defined(__BIG_ENDIAN)
	uint8_t port_id /* Port ID */;
	uint8_t func_id /* Function ID */;
	uint8_t seq_id /* SEQ ID counter to be used in transmitted FC header */;
	struct xstorm_fcoe_context_flags tx_flags;
#elif defined(__LITTLE_ENDIAN)
	struct xstorm_fcoe_context_flags tx_flags;
	uint8_t seq_id /* SEQ ID counter to be used in transmitted FC header */;
	uint8_t func_id /* Function ID */;
	uint8_t port_id /* Port ID */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t mtu /* MTU */;
	uint8_t func_mode /* Function mode */;
	uint8_t vnic_id /* Vnic ID */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t vnic_id /* Vnic ID */;
	uint8_t func_mode /* Function mode */;
	uint16_t mtu /* MTU */;
#endif
	struct regpair_t confq_curr_page_addr /* The current page of CONFQ to be processed */;
	struct fcoe_cached_wqe cached_wqe[8] /* Up to 8 SQ/XFRQ WQEs read in one shot */;
	struct regpair_t lcq_base_addr /* The page address which the LCQ resides in host memory */;
	struct xstorm_fcoe_tce tce /* TX section task context */;
	struct xstorm_fcoe_fcp_data fcp_data /* The parameters required for FCP_DATA Sequences transmission */;
#if defined(__BIG_ENDIAN)
	uint8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by traget, received during PLOGI */;
	struct xstorm_fcoe_context_flags_cont tx_flags_cont;
	uint8_t dcb_val /* DCB val - let us know if dcb info changes */;
	uint8_t data_pb_cmd_size /* Data pb cmd size */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t data_pb_cmd_size /* Data pb cmd size */;
	uint8_t dcb_val /* DCB val - let us know if dcb info changes */;
	struct xstorm_fcoe_context_flags_cont tx_flags_cont;
	uint8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by traget, received during PLOGI */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t fcoe_tx_stat_params_ram_addr /* stat Ram Addr */;
	uint16_t fcoe_tx_fc_seq_ram_addr /* Tx FC sequence Ram Addr */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t fcoe_tx_fc_seq_ram_addr /* Tx FC sequence Ram Addr */;
	uint16_t fcoe_tx_stat_params_ram_addr /* stat Ram Addr */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t fcp_cmd_line_credit;
	uint8_t eth_hdr_size /* Ethernet header size without eth type */;
	uint16_t pbf_addr /* PBF addr */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t pbf_addr /* PBF addr */;
	uint8_t eth_hdr_size /* Ethernet header size without eth type */;
	uint8_t fcp_cmd_line_credit;
#endif
#if defined(__BIG_ENDIAN)
	union fcoe_vlan_vif_field_union multi_func_val /* Outer vlan vif union */;
	uint8_t page_log_size /* Page log size */;
	struct xstorm_fcoe_vlan_conf orig_vlan_conf /* original vlan configuration, used when we switch from dcb enable to dcb disabled */;
#elif defined(__LITTLE_ENDIAN)
	struct xstorm_fcoe_vlan_conf orig_vlan_conf /* original vlan configuration, used when we switch from dcb enable to dcb disabled */;
	uint8_t page_log_size /* Page log size */;
	union fcoe_vlan_vif_field_union multi_func_val /* Outer vlan vif union */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t fcp_cmd_frame_size /* FCP_CMD frame size */;
	uint16_t pbf_addr_ff /* PBF addr with ff */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t pbf_addr_ff /* PBF addr with ff */;
	uint16_t fcp_cmd_frame_size /* FCP_CMD frame size */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t vlan_num /* Vlan number */;
	uint8_t cos /* Cos */;
	uint8_t cache_xfrq_cons /* Cache xferq consumer */;
	uint8_t cache_sq_cons /* Cache sq consumer */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t cache_sq_cons /* Cache sq consumer */;
	uint8_t cache_xfrq_cons /* Cache xferq consumer */;
	uint8_t cos /* Cos */;
	uint8_t vlan_num /* Vlan number */;
#endif
	uint32_t verify_tx_seq /* Sequence number of last transmitted sequence in order to verify target did not send FCP_RSP before the actual transmission of PBF from the SGL */;
};

/*
 * Xstorm FCoE Storm Context
 */
struct xstorm_fcoe_st_context
{
	struct xstorm_fcoe_eth_context_section eth;
	struct xstorm_fcoe_context_section fcoe;
};

/*
 * Fcoe connection context
 */
struct fcoe_context
{
	struct ustorm_fcoe_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_fcoe_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_fcoe_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_fcoe_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct ustorm_fcoe_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_fcoe_st_context xstorm_st_context /* Xstorm storm context */;
};


/*
 * FCoE init params passed by driver to FW in FCoE init ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_init_ramrod_params
{
	struct fcoe_kwqe_init1 init_kwqe1;
	struct fcoe_kwqe_init2 init_kwqe2;
	struct fcoe_kwqe_init3 init_kwqe3;
	struct regpair_t eq_pbl_base /* Physical address of PBL */;
	uint32_t eq_pbl_size /* PBL size */;
	uint32_t reserved2;
	uint16_t eq_prod /* EQ prdocuer */;
	uint16_t sb_num /* Status block number */;
	uint8_t sb_id /* Status block id (EQ consumer) */;
	uint8_t reserved0;
	uint16_t reserved1;
};


/*
 * FCoE statistics params buffer passed by driver to FW in FCoE statistics ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_stat_ramrod_params
{
	struct fcoe_kwqe_stat stat_kwqe;
};


/*
 * CQ DB CQ producer and pending completion counter
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt
{
#if defined(__BIG_ENDIAN)
	uint16_t cntr /* CQ pending completion counter */;
	uint16_t prod /* Ustorm CQ producer , updated by Ustorm */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t prod /* Ustorm CQ producer , updated by Ustorm */;
	uint16_t cntr /* CQ pending completion counter */;
#endif
};

/*
 * CQ DB pending completion ITT array
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt prod_pend_comp[8] /* CQ pending completion ITT array */;
};

/*
 * CQ DB pending completion ITT array
 */
struct iscsi_cq_db_pnd_comp_itt_arr
{
	uint16_t itt[8] /* CQ pending completion ITT array */;
};

/*
 * Cstorm CQ sequence to notify array, updated by driver
 */
struct iscsi_cq_db_sqn_2_notify_arr
{
	uint16_t sqn[8] /* Cstorm CQ sequence to notify array, updated by driver */;
};

/*
 * CQ DB
 */
struct iscsi_cq_db
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr cq_u_prod_pend_comp_ctr_arr /* Ustorm CQ producer and pending completion counter array, updated by Ustorm */;
	struct iscsi_cq_db_pnd_comp_itt_arr cq_c_pend_comp_itt_arr /* Cstorm CQ pending completion ITT array, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_drv_sqn_2_notify_arr /* Cstorm CQ sequence to notify array, updated by driver */;
	uint32_t reserved[4] /* 16 byte allignment */;
};


/*
 * iSCSI KCQ CQE parameters
 */
union iscsi_kcqe_params
{
	uint32_t reserved0[4];
};

/*
 * iSCSI KCQ CQE
 */
struct iscsi_kcqe
{
	uint32_t iscsi_conn_id /* Drivers connection ID (only 16 bits are used) */;
	uint32_t completion_status /* 0=command completed successfully, 1=command failed */;
	uint32_t iscsi_conn_context_id /* Context ID of the iSCSI connection */;
	union iscsi_kcqe_params params /* command-specific parameters */;
#if defined(__BIG_ENDIAN)
	uint8_t flags;
		#define ISCSI_KCQE_RESERVED0                                                         (0x7<<0) /* BitField flags	 */
		#define ISCSI_KCQE_RESERVED0_SHIFT                                                   0
		#define ISCSI_KCQE_RAMROD_COMPLETION                                                 (0x1<<3) /* BitField flags	Everest only - indicates whether this KCQE is a ramrod completion */
		#define ISCSI_KCQE_RAMROD_COMPLETION_SHIFT                                           3
		#define ISCSI_KCQE_LAYER_CODE                                                        (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KCQE_LAYER_CODE_SHIFT                                                  4
		#define ISCSI_KCQE_LINKED_WITH_NEXT                                                  (0x1<<7) /* BitField flags	Indicates whether this KCQE is linked with the next KCQE */
		#define ISCSI_KCQE_LINKED_WITH_NEXT_SHIFT                                            7
	uint8_t op_code /* iSCSI KCQ opcode */;
	uint16_t qe_self_seq /* Self identifying sequence number */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t qe_self_seq /* Self identifying sequence number */;
	uint8_t op_code /* iSCSI KCQ opcode */;
	uint8_t flags;
		#define ISCSI_KCQE_RESERVED0                                                         (0x7<<0) /* BitField flags	 */
		#define ISCSI_KCQE_RESERVED0_SHIFT                                                   0
		#define ISCSI_KCQE_RAMROD_COMPLETION                                                 (0x1<<3) /* BitField flags	Everest only - indicates whether this KCQE is a ramrod completion */
		#define ISCSI_KCQE_RAMROD_COMPLETION_SHIFT                                           3
		#define ISCSI_KCQE_LAYER_CODE                                                        (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KCQE_LAYER_CODE_SHIFT                                                  4
		#define ISCSI_KCQE_LINKED_WITH_NEXT                                                  (0x1<<7) /* BitField flags	Indicates whether this KCQE is linked with the next KCQE */
		#define ISCSI_KCQE_LINKED_WITH_NEXT_SHIFT                                            7
#endif
};


/*
 * iSCSI KWQE header
 */
struct iscsi_kwqe_header
{
#if defined(__BIG_ENDIAN)
	uint8_t flags;
		#define ISCSI_KWQE_HEADER_RESERVED0                                                  (0xF<<0) /* BitField flags	 */
		#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT                                            0
		#define ISCSI_KWQE_HEADER_LAYER_CODE                                                 (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT                                           4
		#define ISCSI_KWQE_HEADER_RESERVED1                                                  (0x1<<7) /* BitField flags	 */
		#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT                                            7
	uint8_t op_code /* iSCSI KWQE opcode */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t op_code /* iSCSI KWQE opcode */;
	uint8_t flags;
		#define ISCSI_KWQE_HEADER_RESERVED0                                                  (0xF<<0) /* BitField flags	 */
		#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT                                            0
		#define ISCSI_KWQE_HEADER_LAYER_CODE                                                 (0x7<<4) /* BitField flags	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT                                           4
		#define ISCSI_KWQE_HEADER_RESERVED1                                                  (0x1<<7) /* BitField flags	 */
		#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT                                            7
#endif
};

/*
 * iSCSI firmware init request 1
 */
struct iscsi_kwqe_init1
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	uint8_t hsi_version /* HSI version number */;
	uint8_t num_cqs /* Number of completion queues */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t num_cqs /* Number of completion queues */;
	uint8_t hsi_version /* HSI version number */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	uint32_t dummy_buffer_addr_lo /* Lower 32-bit of dummy buffer - Teton only */;
	uint32_t dummy_buffer_addr_hi /* Higher 32-bit of dummy buffer - Teton only */;
#if defined(__BIG_ENDIAN)
	uint16_t num_ccells_per_conn /* Number of ccells per connection */;
	uint16_t num_tasks_per_conn /* Number of tasks per connection */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t num_tasks_per_conn /* Number of tasks per connection */;
	uint16_t num_ccells_per_conn /* Number of ccells per connection */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t sq_wqes_per_page /* Number of work entries in a single page of SQ */;
	uint16_t sq_num_wqes /* Number of entries in the Send Queue */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_num_wqes /* Number of entries in the Send Queue */;
	uint16_t sq_wqes_per_page /* Number of work entries in a single page of SQ */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t cq_log_wqes_per_page /* Log of number of work entries in a single page of CQ */;
	uint8_t flags;
		#define ISCSI_KWQE_INIT1_PAGE_SIZE                                                   (0xF<<0) /* BitField flags	page size code */
		#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT                                             0
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE                                          (0x1<<4) /* BitField flags	if set, delayed ack is enabled */
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT                                    4
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE                                           (0x1<<5) /* BitField flags	if set, keep alive is enabled */
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT                                     5
		#define ISCSI_KWQE_INIT1_RESERVED1                                                   (0x3<<6) /* BitField flags	 */
		#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT                                             6
	uint16_t cq_num_wqes /* Number of entries in the Completion Queue */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_num_wqes /* Number of entries in the Completion Queue */;
	uint8_t flags;
		#define ISCSI_KWQE_INIT1_PAGE_SIZE                                                   (0xF<<0) /* BitField flags	page size code */
		#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT                                             0
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE                                          (0x1<<4) /* BitField flags	if set, delayed ack is enabled */
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT                                    4
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE                                           (0x1<<5) /* BitField flags	if set, keep alive is enabled */
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT                                     5
		#define ISCSI_KWQE_INIT1_RESERVED1                                                   (0x3<<6) /* BitField flags	 */
		#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT                                             6
	uint8_t cq_log_wqes_per_page /* Log of number of work entries in a single page of CQ */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t cq_num_pages /* Number of pages in CQ page table */;
	uint16_t sq_num_pages /* Number of pages in SQ page table */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_num_pages /* Number of pages in SQ page table */;
	uint16_t cq_num_pages /* Number of pages in CQ page table */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t rq_buffer_size /* Size of a single buffer (entry) in the RQ */;
	uint16_t rq_num_wqes /* Number of entries in the Receive Queue */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rq_num_wqes /* Number of entries in the Receive Queue */;
	uint16_t rq_buffer_size /* Size of a single buffer (entry) in the RQ */;
#endif
};

/*
 * iSCSI firmware init request 2
 */
struct iscsi_kwqe_init2
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	uint16_t max_cq_sqn /* CQ wraparound value */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t max_cq_sqn /* CQ wraparound value */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	uint32_t error_bit_map[2] /* bit per error type, 0=error, 1=warning */;
	uint32_t tcp_keepalive /* TCP keepalive time in seconds */;
	uint32_t reserved1[4];
};

/*
 * Initial iSCSI connection offload request 1
 */
struct iscsi_kwqe_conn_offload1
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	uint16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	uint32_t sq_page_table_addr_lo /* Lower 32-bit of the SQs page table address */;
	uint32_t sq_page_table_addr_hi /* Higher 32-bit of the SQs page table address */;
	uint32_t cq_page_table_addr_lo /* Lower 32-bit of the CQs page table address */;
	uint32_t cq_page_table_addr_hi /* Higher 32-bit of the CQs page table address */;
	uint32_t reserved0[3];
};

/*
 * iSCSI Page Table Entry (PTE)
 */
struct iscsi_pte
{
	uint32_t hi /* Higher 32 bits of address */;
	uint32_t lo /* Lower 32 bits of address */;
};

/*
 * Initial iSCSI connection offload request 2
 */
struct iscsi_kwqe_conn_offload2
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	uint16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
	uint32_t rq_page_table_addr_lo /* Lower 32-bits of the RQs page table address */;
	uint32_t rq_page_table_addr_hi /* Higher 32-bits of the RQs page table address */;
	struct iscsi_pte sq_first_pte /* first SQ page table entry (for FW caching) */;
	struct iscsi_pte cq_first_pte /* first CQ page table entry (for FW caching) */;
	uint32_t num_additional_wqes /* Everest specific - number of offload3 KWQEs that will follow this KWQE */;
};

/*
 * Everest specific - Initial iSCSI connection offload request 3
 */
struct iscsi_kwqe_conn_offload3
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	uint16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
	uint32_t reserved1;
	struct iscsi_pte qp_first_pte[3] /* first page table entry of some iSCSI ring (for FW caching) */;
};

/*
 * iSCSI connection update request
 */
struct iscsi_kwqe_conn_update
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	uint16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t session_error_recovery_level /* iSCSI Error Recovery Level negotiated on this connection */;
	uint8_t max_outstanding_r2ts /* Maximum number of outstanding R2ts that a target can send for a command */;
	uint8_t reserved2;
	uint8_t conn_flags;
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST                                         (0x1<<0) /* BitField conn_flags	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT                                   0
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST                                           (0x1<<1) /* BitField conn_flags	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT                                     1
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T                                           (0x1<<2) /* BitField conn_flags	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT                                     2
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA                                        (0x1<<3) /* BitField conn_flags	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT                                  3
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE                                      (0x3<<4) /* BitField conn_flags	 (use enum tcp_tstorm_ooo) */
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE_SHIFT                                4
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1                                             (0x3<<6) /* BitField conn_flags	 */
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT                                       6
#elif defined(__LITTLE_ENDIAN)
	uint8_t conn_flags;
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST                                         (0x1<<0) /* BitField conn_flags	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT                                   0
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST                                           (0x1<<1) /* BitField conn_flags	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT                                     1
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T                                           (0x1<<2) /* BitField conn_flags	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT                                     2
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA                                        (0x1<<3) /* BitField conn_flags	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT                                  3
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE                                      (0x3<<4) /* BitField conn_flags	 (use enum tcp_tstorm_ooo) */
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE_SHIFT                                4
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1                                             (0x3<<6) /* BitField conn_flags	 */
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT                                       6
	uint8_t reserved2;
	uint8_t max_outstanding_r2ts /* Maximum number of outstanding R2ts that a target can send for a command */;
	uint8_t session_error_recovery_level /* iSCSI Error Recovery Level negotiated on this connection */;
#endif
	uint32_t context_id /* Context ID of the iSCSI connection */;
	uint32_t max_send_pdu_length /* Maximum length of a PDU that the target can receive */;
	uint32_t max_recv_pdu_length /* Maximum length of a PDU that the Initiator can receive */;
	uint32_t first_burst_length /* Maximum length of the immediate and unsolicited data that Initiator can send */;
	uint32_t max_burst_length /* Maximum length of the data that Initiator and target can send in one burst */;
	uint32_t exp_stat_sn /* Expected Status Serial Number */;
};

/*
 * iSCSI destroy connection request
 */
struct iscsi_kwqe_conn_destroy
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	uint16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	uint32_t context_id /* Context ID of the iSCSI connection */;
	uint32_t reserved1[6];
};

/*
 * iSCSI KWQ WQE
 */
union iscsi_kwqe
{
	struct iscsi_kwqe_init1 init1;
	struct iscsi_kwqe_init2 init2;
	struct iscsi_kwqe_conn_offload1 conn_offload1;
	struct iscsi_kwqe_conn_offload2 conn_offload2;
	struct iscsi_kwqe_conn_offload3 conn_offload3;
	struct iscsi_kwqe_conn_update conn_update;
	struct iscsi_kwqe_conn_destroy conn_destroy;
};


struct iscsi_rq_db
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved1;
	uint16_t rq_prod;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rq_prod;
	uint16_t reserved1;
#endif
	uint32_t __fw_hdr[15] /* Used by FW for partial header placement */;
};


struct iscsi_sq_db
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved0 /* Pad structure size to 16 bytes */;
	uint16_t sq_prod;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sq_prod;
	uint16_t reserved0 /* Pad structure size to 16 bytes */;
#endif
	uint32_t reserved1[3] /* Pad structure size to 16 bytes */;
};


/*
 * Tstorm Tcp flags
 */
struct tstorm_l5cm_tcp_flags
{
	uint16_t flags;
		#define TSTORM_L5CM_TCP_FLAGS_VLAN_ID                                                (0xFFF<<0) /* BitField flags	 */
		#define TSTORM_L5CM_TCP_FLAGS_VLAN_ID_SHIFT                                          0
		#define TSTORM_L5CM_TCP_FLAGS_DELAYED_ACK_EN                                         (0x1<<12) /* BitField flags	 */
		#define TSTORM_L5CM_TCP_FLAGS_DELAYED_ACK_EN_SHIFT                                   12
		#define TSTORM_L5CM_TCP_FLAGS_TS_ENABLED                                             (0x1<<13) /* BitField flags	 */
		#define TSTORM_L5CM_TCP_FLAGS_TS_ENABLED_SHIFT                                       13
		#define TSTORM_L5CM_TCP_FLAGS_RSRV1                                                  (0x3<<14) /* BitField flags	 */
		#define TSTORM_L5CM_TCP_FLAGS_RSRV1_SHIFT                                            14
};


/*
 * Cstorm iSCSI Storm Context
 */
struct cstorm_iscsi_st_context
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr cq_c_prod_pend_comp_ctr_arr /* Cstorm CQ producer and CQ pending completion array, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_prod_sqn_arr /* Cstorm CQ producer sequence, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_sqn_2_notify_arr /* Event Coalescing CQ sequence to notify driver, copied by Cstorm from CQ DB that is updated by Driver */;
	struct regpair_t hq_pbl_base /* HQ PBL base */;
	struct regpair_t hq_curr_pbe /* HQ current PBE */;
	struct regpair_t task_pbl_base /* Task Context Entry PBL base */;
	struct regpair_t cq_db_base /* pointer to CQ DB array. each CQ DB entry consists of CQ PBL, arm bit and idx to notify */;
#if defined(__BIG_ENDIAN)
	uint16_t hq_bd_itt /* copied from HQ BD */;
	uint16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	uint16_t iscsi_conn_id;
	uint16_t hq_bd_itt /* copied from HQ BD */;
#endif
	uint32_t hq_bd_data_segment_len /* copied from HQ BD */;
	uint32_t hq_bd_buffer_offset /* copied from HQ BD */;
#if defined(__BIG_ENDIAN)
	uint8_t rsrv;
	uint8_t cq_proc_en_bit_map /* CQ processing enable bit map, 1 bit per CQ */;
	uint8_t cq_pend_comp_itt_valid_bit_map /* CQ pending completion ITT valid bit map, 1 bit per CQ */;
	uint8_t hq_bd_opcode /* copied from HQ BD */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t hq_bd_opcode /* copied from HQ BD */;
	uint8_t cq_pend_comp_itt_valid_bit_map /* CQ pending completion ITT valid bit map, 1 bit per CQ */;
	uint8_t cq_proc_en_bit_map /* CQ processing enable bit map, 1 bit per CQ */;
	uint8_t rsrv;
#endif
	uint32_t hq_tcp_seq /* TCP sequence of next BD to release */;
#if defined(__BIG_ENDIAN)
	uint16_t flags;
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN                                       (0x1<<0) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT                                 0
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN                                        (0x1<<1) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT                                  1
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID                                     (0x1<<2) /* BitField flags	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT                               2
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG                                  (0x1<<3) /* BitField flags	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT                            3
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK                                     (0x1<<4) /* BitField flags	calculated using HQ BD opcode and write flag */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT                               4
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV                                      (0x7FF<<5) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT                                5
	uint16_t hq_cons /* HQ consumer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hq_cons /* HQ consumer */;
	uint16_t flags;
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN                                       (0x1<<0) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT                                 0
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN                                        (0x1<<1) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT                                  1
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID                                     (0x1<<2) /* BitField flags	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT                               2
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG                                  (0x1<<3) /* BitField flags	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT                            3
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK                                     (0x1<<4) /* BitField flags	calculated using HQ BD opcode and write flag */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT                               4
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV                                      (0x7FF<<5) /* BitField flags	 */
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT                                5
#endif
	struct regpair_t rsrv1;
};


/*
 * SCSI read/write SQ WQE
 */
struct iscsi_cmd_pdu_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES                                   (0x7<<0) /* BitField op_attr	Attributes of the SCSI command. To be sent with the outgoing command PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES_SHIFT                             0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1                                        (0x3<<3) /* BitField op_attr	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  3
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG                                   (0x1<<5) /* BitField op_attr	Write bit. Initiator is expected to send the data to the target */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG_SHIFT                             5
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG                                    (0x1<<6) /* BitField op_attr	Read bit. Data from target is expected */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG_SHIFT                              6
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                   (0x1<<7) /* BitField op_attr	Final bit. Firmware can change this bit based on the command before putting it into the outgoing PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                             7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES                                   (0x7<<0) /* BitField op_attr	Attributes of the SCSI command. To be sent with the outgoing command PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES_SHIFT                             0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1                                        (0x3<<3) /* BitField op_attr	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  3
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG                                   (0x1<<5) /* BitField op_attr	Write bit. Initiator is expected to send the data to the target */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG_SHIFT                             5
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG                                    (0x1<<6) /* BitField op_attr	Read bit. Data from target is expected */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG_SHIFT                              6
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                   (0x1<<7) /* BitField op_attr	Final bit. Firmware can change this bit based on the command before putting it into the outgoing PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                             7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	uint32_t itt;
	uint32_t expected_data_transfer_length;
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t scsi_command_block[4];
};


/*
 * Buffer per connection, used in Tstorm
 */
struct iscsi_conn_buf
{
	struct regpair_t reserved[8];
};


/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_rq_db
{
	struct regpair_t pbl_base /* Pointer to the rq page base list. */;
	struct regpair_t curr_pbe /* Pointer to the current rq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_r2tq_db
{
	struct regpair_t pbl_base /* Pointer to the r2tq page base list. */;
	struct regpair_t curr_pbe /* Pointer to the current r2tq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_cq_db
{
#if defined(__BIG_ENDIAN)
	uint16_t cq_sn /* CQ serial number */;
	uint16_t prod /* CQ producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t prod /* CQ producer */;
	uint16_t cq_sn /* CQ serial number */;
#endif
	struct regpair_t curr_pbe /* Pointer to the current cq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct rings_db
{
	struct ustorm_iscsi_rq_db rq /* RQ db. */;
	struct ustorm_iscsi_r2tq_db r2tq /* R2TQ db. */;
	struct ustorm_iscsi_cq_db cq[8] /* CQ db. */;
#if defined(__BIG_ENDIAN)
	uint16_t rq_prod /* RQ prod */;
	uint16_t r2tq_prod /* R2TQ producer. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t r2tq_prod /* R2TQ producer. */;
	uint16_t rq_prod /* RQ prod */;
#endif
	struct regpair_t cq_pbl_base /* Pointer to the cq page base list. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_placement_db
{
	uint32_t sgl_base_lo /* SGL base address lo */;
	uint32_t sgl_base_hi /* SGL base address hi */;
	uint32_t local_sge_0_address_hi /* SGE address hi */;
	uint32_t local_sge_0_address_lo /* SGE address lo */;
#if defined(__BIG_ENDIAN)
	uint16_t curr_sge_offset /* Current offset in the SGE */;
	uint16_t local_sge_0_size /* SGE size */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t local_sge_0_size /* SGE size */;
	uint16_t curr_sge_offset /* Current offset in the SGE */;
#endif
	uint32_t local_sge_1_address_hi /* SGE address hi */;
	uint32_t local_sge_1_address_lo /* SGE address lo */;
#if defined(__BIG_ENDIAN)
	uint8_t exp_padding_2b /* Number of padding bytes not yet processed */;
	uint8_t nal_len_3b /* Non 4 byte aligned bytes in the previous iteration */;
	uint16_t local_sge_1_size /* SGE size */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t local_sge_1_size /* SGE size */;
	uint8_t nal_len_3b /* Non 4 byte aligned bytes in the previous iteration */;
	uint8_t exp_padding_2b /* Number of padding bytes not yet processed */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t sgl_size /* Number of SGEs remaining till end of SGL */;
	uint8_t local_sge_index_2b /* Index to the local SGE currently used */;
	uint16_t reserved7;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved7;
	uint8_t local_sge_index_2b /* Index to the local SGE currently used */;
	uint8_t sgl_size /* Number of SGEs remaining till end of SGL */;
#endif
	uint32_t rem_pdu /* Number of bytes remaining in PDU */;
	uint32_t place_db_bitfield_1;
		#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD                                    (0xFFFFFF<<0) /* BitField place_db_bitfield_1place_db_bitfield_1	Number of bytes remaining in PDU payload */
		#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD_SHIFT                              0
		#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID                                              (0xFF<<24) /* BitField place_db_bitfield_1place_db_bitfield_1	Temp task context - determines the CQ index for CQE placement */
		#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID_SHIFT                                        24
	uint32_t place_db_bitfield_2;
		#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE                                   (0xFFFFFF<<0) /* BitField place_db_bitfield_2place_db_bitfield_2	Bytes to truncate from the payload. */
		#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE_SHIFT                             0
		#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX                                     (0xFF<<24) /* BitField place_db_bitfield_2place_db_bitfield_2	Sge index on host */
		#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX_SHIFT                               24
	uint32_t nal;
		#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE                                       (0xFFFFFF<<0) /* BitField nalNon aligned db	Number of bytes remaining in local SGEs */
		#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE_SHIFT                                 0
		#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B                                      (0xFF<<24) /* BitField nalNon aligned db	Number of digest bytes not yet processed */
		#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B_SHIFT                                24
};

/*
 * Ustorm iSCSI Storm Context
 */
struct ustorm_iscsi_st_context
{
	uint32_t exp_stat_sn /* Expected status sequence number, incremented with each response/middle path/unsolicited received. */;
	uint32_t exp_data_sn /* Expected Data sequence number, incremented with each data in */;
	struct rings_db ring /* rq, r2tq ,cq */;
	struct regpair_t task_pbl_base /* Task PBL base will be read from RAM to context */;
	struct regpair_t tce_phy_addr /* Pointer to the task context physical address */;
	struct ustorm_iscsi_placement_db place_db;
	uint32_t reserved8 /* reserved */;
	uint32_t rem_rcv_len /* Temp task context - Remaining bytes to end of task */;
#if defined(__BIG_ENDIAN)
	uint16_t hdr_itt /* field copied from PDU header */;
	uint16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	uint16_t iscsi_conn_id;
	uint16_t hdr_itt /* field copied from PDU header */;
#endif
	uint32_t nal_bytes /* nal bytes read from BRB */;
#if defined(__BIG_ENDIAN)
	uint8_t hdr_second_byte_union /* field copied from PDU header */;
	uint8_t bitfield_0;
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU                                         (0x1<<0) /* BitField bitfield_0bitfield_0	marks that processing of payload has started */
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT                                   0
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE                                            (0x1<<1) /* BitField bitfield_0bitfield_0	marks that fence is need on the next CQE */
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT                                      1
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC                                            (0x1<<2) /* BitField bitfield_0bitfield_0	marks that a RESET should be sent to CRC machine. Used in NAL condition in the beginning of a PDU. */
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT                                      2
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1                                            (0x1F<<3) /* BitField bitfield_0bitfield_0	reserved */
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT                                      3
	uint8_t task_pdu_cache_index;
	uint8_t task_pbe_cache_index;
#elif defined(__LITTLE_ENDIAN)
	uint8_t task_pbe_cache_index;
	uint8_t task_pdu_cache_index;
	uint8_t bitfield_0;
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU                                         (0x1<<0) /* BitField bitfield_0bitfield_0	marks that processing of payload has started */
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT                                   0
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE                                            (0x1<<1) /* BitField bitfield_0bitfield_0	marks that fence is need on the next CQE */
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT                                      1
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC                                            (0x1<<2) /* BitField bitfield_0bitfield_0	marks that a RESET should be sent to CRC machine. Used in NAL condition in the beginning of a PDU. */
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT                                      2
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1                                            (0x1F<<3) /* BitField bitfield_0bitfield_0	reserved */
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT                                      3
	uint8_t hdr_second_byte_union /* field copied from PDU header */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved3 /* reserved */;
	uint8_t reserved2 /* reserved */;
	uint8_t acDecrement /* Manage the AC decrement that should be done by USDM */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t acDecrement /* Manage the AC decrement that should be done by USDM */;
	uint8_t reserved2 /* reserved */;
	uint16_t reserved3 /* reserved */;
#endif
	uint32_t task_stat /* counts dataIn for read and holds data outs, r2t for write */;
#if defined(__BIG_ENDIAN)
	uint8_t hdr_opcode /* field copied from PDU header */;
	uint8_t num_cqs /* Number of CQs supported by this connection */;
	uint16_t reserved5 /* reserved */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t reserved5 /* reserved */;
	uint8_t num_cqs /* Number of CQs supported by this connection */;
	uint8_t hdr_opcode /* field copied from PDU header */;
#endif
	uint32_t negotiated_rx;
		#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH                                  (0xFFFFFF<<0) /* BitField negotiated_rx	 */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH_SHIFT                            0
		#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS                                 (0xFF<<24) /* BitField negotiated_rx	 */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS_SHIFT                           24
	uint32_t negotiated_rx_and_flags;
		#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH                                     (0xFFFFFF<<0) /* BitField negotiated_rx_and_flags	Negotiated maximum length of sequence */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH_SHIFT                               0
		#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED                        (0x1<<24) /* BitField negotiated_rx_and_flags	Marks that unvalid CQE was already posted or PDU header was cachaed in RAM */
		#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED_SHIFT                  24
		#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN                                      (0x1<<25) /* BitField negotiated_rx_and_flags	Header digest support enable */
		#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN_SHIFT                                25
		#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN                                     (0x1<<26) /* BitField negotiated_rx_and_flags	Data digest support enable */
		#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN_SHIFT                               26
		#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR                                     (0x1<<27) /* BitField negotiated_rx_and_flags	 */
		#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR_SHIFT                               27
		#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID                                         (0x1<<28) /* BitField negotiated_rx_and_flags	temp task context */
		#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID_SHIFT                                   28
		#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE                                            (0x3<<29) /* BitField negotiated_rx_and_flags	Task type: 0 = slow-path (non-RW) 1 = read 2 = write */
		#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE_SHIFT                                      29
		#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED                                     (0x1<<31) /* BitField negotiated_rx_and_flags	Set if all data is acked */
		#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED_SHIFT                               31
};

/*
 * TCP context region, shared in TOE, RDMA and ISCSI
 */
struct tstorm_tcp_st_context_section
{
	uint32_t flags1;
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT                                       (0xFFFFFF<<0) /* BitField flags1various state flags	20b only, Smoothed Rount Trip Time */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT_SHIFT                                 0
		#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID                                   (0x1<<24) /* BitField flags1various state flags	PAWS asserted as invalid in KA flow */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID_SHIFT                             24
		#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS                               (0x1<<25) /* BitField flags1various state flags	Timestamps supported on this connection */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS_SHIFT                         25
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0                                      (0x1<<26) /* BitField flags1various state flags	 */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0_SHIFT                                26
		#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD                                (0x1<<27) /* BitField flags1various state flags	stop receiving rx payload */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD_SHIFT                          27
		#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED                                     (0x1<<28) /* BitField flags1various state flags	Keep Alive enabled */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED_SHIFT                               28
		#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE                             (0x1<<29) /* BitField flags1various state flags	First Retransmition Timout Estimation */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE_SHIFT                       29
		#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN                          (0x1<<30) /* BitField flags1various state flags	per connection flag, signals whether to check if rt count exceeds max_seg_retransmit */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN_SHIFT                    30
		#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN                              (0x1<<31) /* BitField flags1various state flags	last isle ends with FIN. FIN is counted as 1 byte for isle end sequence */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN_SHIFT                        31
	uint32_t flags2;
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION                                  (0xFFFFFF<<0) /* BitField flags2various state flags	20b only, Round Trip Time variation */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION_SHIFT                            0
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN                                          (0x1<<24) /* BitField flags2various state flags	 */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN_SHIFT                                    24
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN                                  (0x1<<25) /* BitField flags2various state flags	per GOS flags, but duplicated for each context */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN_SHIFT                            25
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT                                (0x1<<26) /* BitField flags2various state flags	keep alive packet was sent */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT_SHIFT                          26
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT                           (0x1<<27) /* BitField flags2various state flags	persist packet was sent */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT_SHIFT                     27
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS                            (0x1<<28) /* BitField flags2various state flags	determines wheather or not to update l2 statistics */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                      28
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS                            (0x1<<29) /* BitField flags2various state flags	determines wheather or not to update l4 statistics */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                      29
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK                         (0x1<<30) /* BitField flags2various state flags	possible blind-in-window RST attack detected */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK_SHIFT                   30
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK                         (0x1<<31) /* BitField flags2various state flags	possible blind-in-window SYN attack detected */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK_SHIFT                   31
#if defined(__BIG_ENDIAN)
	uint16_t mss;
	uint8_t tcp_sm_state /* 3b only, Tcp state machine state */;
	uint8_t rto_exp /* 3b only, Exponential Backoff index */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t rto_exp /* 3b only, Exponential Backoff index */;
	uint8_t tcp_sm_state /* 3b only, Tcp state machine state */;
	uint16_t mss;
#endif
	uint32_t rcv_nxt /* Receive sequence: next expected */;
	uint32_t timestamp_recent /* last timestamp from segTS */;
	uint32_t timestamp_recent_time /* time at which timestamp_recent has been set */;
	uint32_t cwnd /* Congestion window */;
	uint32_t ss_thresh /* Slow Start Threshold */;
	uint32_t cwnd_accum /* Congestion window accumilation */;
	uint32_t prev_seg_seq /* Sequence number used for last sndWnd update (was: snd_wnd_l1) */;
	uint32_t expected_rel_seq /* the last update of rel_seq */;
	uint32_t recover /* Recording of sndMax when we enter retransmit */;
#if defined(__BIG_ENDIAN)
	uint8_t retransmit_count /* Number of times a packet was retransmitted */;
	uint8_t ka_max_probe_count /* Keep Alive maximum probe counter */;
	uint8_t persist_probe_count /* Persist probe counter */;
	uint8_t ka_probe_count /* Keep Alive probe counter */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t ka_probe_count /* Keep Alive probe counter */;
	uint8_t persist_probe_count /* Persist probe counter */;
	uint8_t ka_max_probe_count /* Keep Alive maximum probe counter */;
	uint8_t retransmit_count /* Number of times a packet was retransmitted */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
	uint8_t ooo_support_mode;
	uint8_t snd_wnd_scale /* 4b only, Far-end window (Snd.Wind.Scale) scale */;
	uint8_t dup_ack_count /* Duplicate Ack Counter */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t dup_ack_count /* Duplicate Ack Counter */;
	uint8_t snd_wnd_scale /* 4b only, Far-end window (Snd.Wind.Scale) scale */;
	uint8_t ooo_support_mode;
	uint8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
#endif
	uint32_t retransmit_start_time /* Used by retransmit as a recording of start time */;
	uint32_t ka_timeout /* Keep Alive timeout */;
	uint32_t ka_interval /* Keep Alive interval */;
	uint32_t isle_start_seq /* First Out-of-order isle start sequence */;
	uint32_t isle_end_seq /* First Out-of-order isle end sequence */;
#if defined(__BIG_ENDIAN)
	uint16_t second_isle_address /* address of the second isle (if exists) in internal RAM */;
	uint16_t recent_seg_wnd /* Last far end window received (not scaled!) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t recent_seg_wnd /* Last far end window received (not scaled!) */;
	uint16_t second_isle_address /* address of the second isle (if exists) in internal RAM */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t max_isles_ever_happened /* for statistics only - max number of isles ever happened on this connection */;
	uint8_t isles_number /* number of isles */;
	uint16_t last_isle_address /* address of the last isle (if exists) in internal RAM */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t last_isle_address /* address of the last isle (if exists) in internal RAM */;
	uint8_t isles_number /* number of isles */;
	uint8_t max_isles_ever_happened /* for statistics only - max number of isles ever happened on this connection */;
#endif
	uint32_t max_rt_time;
#if defined(__BIG_ENDIAN)
	uint16_t lsb_mac_address /* TX source MAC LSB-16 */;
	uint16_t vlan_id /* Connection-configured VLAN ID */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t vlan_id /* Connection-configured VLAN ID */;
	uint16_t lsb_mac_address /* TX source MAC LSB-16 */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t msb_mac_address /* TX source MAC MSB-16 */;
	uint16_t mid_mac_address /* TX source MAC MID-16 */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t mid_mac_address /* TX source MAC MID-16 */;
	uint16_t msb_mac_address /* TX source MAC MSB-16 */;
#endif
	uint32_t rightmost_received_seq /* The maximum sequence ever received - used for The New Patent */;
};

/*
 * Termination variables
 */
struct iscsi_term_vars
{
	uint8_t BitMap;
		#define ISCSI_TERM_VARS_TCP_STATE                                                    (0xF<<0) /* BitField BitMap	tcp state for the termination process */
		#define ISCSI_TERM_VARS_TCP_STATE_SHIFT                                              0
		#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT                                            (0x1<<4) /* BitField BitMap	fin received sticky bit */
		#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT_SHIFT                                      4
		#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT                                     (0x1<<5) /* BitField BitMap	ack on fin received stick bit */
		#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT_SHIFT                               5
		#define ISCSI_TERM_VARS_TERM_ON_CHIP                                                 (0x1<<6) /* BitField BitMap	termination on chip ( option2 ) */
		#define ISCSI_TERM_VARS_TERM_ON_CHIP_SHIFT                                           6
		#define ISCSI_TERM_VARS_RSRV                                                         (0x1<<7) /* BitField BitMap	 */
		#define ISCSI_TERM_VARS_RSRV_SHIFT                                                   7
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct tstorm_iscsi_st_context_section
{
	uint32_t nalPayload /* Non-aligned payload */;
	uint32_t b2nh /* Number of bytes to next iSCSI header */;
#if defined(__BIG_ENDIAN)
	uint16_t rq_cons /* RQ consumer */;
	uint8_t flags;
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN                              (0x1<<0) /* BitField flags	header digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT                        0
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN                             (0x1<<1) /* BitField flags	data digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT                       1
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER                             (0x1<<2) /* BitField flags	partial header flow indication */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT                       2
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE                               (0x1<<3) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT                         3
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS                              (0x1<<4) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT                        4
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN                                       (0x3<<5) /* BitField flags	Non-aligned length */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN_SHIFT                                 5
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0                                        (0x1<<7) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0_SHIFT                                  7
	uint8_t hdr_bytes_2_fetch /* Number of bytes left to fetch to complete iSCSI header */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t hdr_bytes_2_fetch /* Number of bytes left to fetch to complete iSCSI header */;
	uint8_t flags;
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN                              (0x1<<0) /* BitField flags	header digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT                        0
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN                             (0x1<<1) /* BitField flags	data digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT                       1
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER                             (0x1<<2) /* BitField flags	partial header flow indication */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT                       2
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE                               (0x1<<3) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT                         3
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS                              (0x1<<4) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT                        4
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN                                       (0x3<<5) /* BitField flags	Non-aligned length */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN_SHIFT                                 5
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0                                        (0x1<<7) /* BitField flags	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0_SHIFT                                  7
	uint16_t rq_cons /* RQ consumer */;
#endif
	struct regpair_t rq_db_phy_addr;
#if defined(__BIG_ENDIAN)
	struct iscsi_term_vars term_vars /* Termination variables */;
	uint8_t rsrv1;
	uint16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	uint16_t iscsi_conn_id;
	uint8_t rsrv1;
	struct iscsi_term_vars term_vars /* Termination variables */;
#endif
	uint32_t process_nxt /* next TCP sequence to be processed by the iSCSI layer. */;
};

/*
 * The iSCSI non-aggregative context of Tstorm
 */
struct tstorm_iscsi_st_context
{
	struct tstorm_tcp_st_context_section tcp /* TCP  context region, shared in TOE, RDMA and iSCSI */;
	struct tstorm_iscsi_st_context_section iscsi /* iSCSI context region, used only in iSCSI */;
};

/*
 * Ethernet context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_eth_context_section
{
#if defined(__BIG_ENDIAN)
	uint8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	uint8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
	uint16_t vlan_params;
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID                                           (0xFFF<<0) /* BitField vlan_params	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                     0
		#define XSTORM_ETH_CONTEXT_SECTION_CFI                                               (0x1<<12) /* BitField vlan_params	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT                                         12
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY                                          (0x7<<13) /* BitField vlan_params	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                                    13
#elif defined(__LITTLE_ENDIAN)
	uint16_t vlan_params;
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID                                           (0xFFF<<0) /* BitField vlan_params	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                     0
		#define XSTORM_ETH_CONTEXT_SECTION_CFI                                               (0x1<<12) /* BitField vlan_params	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT                                         12
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY                                          (0x7<<13) /* BitField vlan_params	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                                    13
	uint16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	uint8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
#endif
};

/*
 * IpV4 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v4_context_section
{
#if defined(__BIG_ENDIAN)
	uint16_t __pbf_hdr_cmd_rsvd_id;
	uint16_t __pbf_hdr_cmd_rsvd_flags_offset;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __pbf_hdr_cmd_rsvd_flags_offset;
	uint16_t __pbf_hdr_cmd_rsvd_id;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t __pbf_hdr_cmd_rsvd_ver_ihl;
	uint8_t tos /* Type Of Service, used in PBF Header Builder Command */;
	uint16_t __pbf_hdr_cmd_rsvd_length;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __pbf_hdr_cmd_rsvd_length;
	uint8_t tos /* Type Of Service, used in PBF Header Builder Command */;
	uint8_t __pbf_hdr_cmd_rsvd_ver_ihl;
#endif
	uint32_t ip_local_addr /* used in PBF Header Builder Command */;
#if defined(__BIG_ENDIAN)
	uint8_t ttl /* Time to live, used in PBF Header Builder Command */;
	uint8_t __pbf_hdr_cmd_rsvd_protocol;
	uint16_t __pbf_hdr_cmd_rsvd_csum;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __pbf_hdr_cmd_rsvd_csum;
	uint8_t __pbf_hdr_cmd_rsvd_protocol;
	uint8_t ttl /* Time to live, used in PBF Header Builder Command */;
#endif
	uint32_t __pbf_hdr_cmd_rsvd_1 /* places the ip_remote_addr field in the proper place in the regpair */;
	uint32_t ip_remote_addr /* used in PBF Header Builder Command */;
};

/*
 * context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_padded_ip_v4_context_section
{
	struct xstorm_ip_v4_context_section ip_v4;
	uint32_t reserved1[4];
};

/*
 * IpV6 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v6_context_section
{
#if defined(__BIG_ENDIAN)
	uint16_t pbf_hdr_cmd_rsvd_payload_len;
	uint8_t pbf_hdr_cmd_rsvd_nxt_hdr;
	uint8_t hop_limit /* used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t hop_limit /* used in PBF Header Builder Command */;
	uint8_t pbf_hdr_cmd_rsvd_nxt_hdr;
	uint16_t pbf_hdr_cmd_rsvd_payload_len;
#endif
	uint32_t priority_flow_label;
		#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL                                      (0xFFFFF<<0) /* BitField priority_flow_label	used in PBF Header Builder Command */
		#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL_SHIFT                                0
		#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS                                   (0xFF<<20) /* BitField priority_flow_label	used in PBF Header Builder Command */
		#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS_SHIFT                             20
		#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER                            (0xF<<28) /* BitField priority_flow_label	 */
		#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER_SHIFT                      28
	uint32_t ip_local_addr_lo_hi /* second 32 bits of Ip local Address, used in PBF Header Builder Command */;
	uint32_t ip_local_addr_lo_lo /* first 32 bits of Ip local Address, used in PBF Header Builder Command */;
	uint32_t ip_local_addr_hi_hi /* fourth 32 bits of Ip local Address, used in PBF Header Builder Command */;
	uint32_t ip_local_addr_hi_lo /* third 32 bits of Ip local Address, used in PBF Header Builder Command */;
	uint32_t ip_remote_addr_lo_hi /* second 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	uint32_t ip_remote_addr_lo_lo /* first 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	uint32_t ip_remote_addr_hi_hi /* fourth 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	uint32_t ip_remote_addr_hi_lo /* third 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
};

union xstorm_ip_context_section_types
{
	struct xstorm_padded_ip_v4_context_section padded_ip_v4;
	struct xstorm_ip_v6_context_section ip_v6;
};

/*
 * TCP context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_tcp_context_section
{
	uint32_t snd_max;
#if defined(__BIG_ENDIAN)
	uint16_t remote_port /* used in PBF Header Builder Command */;
	uint16_t local_port /* used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t local_port /* used in PBF Header Builder Command */;
	uint16_t remote_port /* used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	uint8_t original_nagle_1b;
	uint8_t ts_enabled /* Only 1 bit is used */;
	uint16_t tcp_params;
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE                                 (0xFF<<0) /* BitField tcp_paramsTcp parameters	for ease of pbf command construction */
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT                           0
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT                                         (0x1<<8) /* BitField tcp_paramsTcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT                                   8
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED                                     (0x1<<9) /* BitField tcp_paramsTcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT                               9
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED                                      (0x1<<10) /* BitField tcp_paramsTcp parameters	Selective Ack Enabled */
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT                                10
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV                                     (0x1<<11) /* BitField tcp_paramsTcp parameters	window smaller than initial window was advertised to far end */
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT                               11
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG                                     (0x1<<12) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                               12
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED                                  (0x1<<13) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT                            13
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER                     (0x3<<14) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT               14
#elif defined(__LITTLE_ENDIAN)
	uint16_t tcp_params;
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE                                 (0xFF<<0) /* BitField tcp_paramsTcp parameters	for ease of pbf command construction */
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT                           0
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT                                         (0x1<<8) /* BitField tcp_paramsTcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT                                   8
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED                                     (0x1<<9) /* BitField tcp_paramsTcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT                               9
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED                                      (0x1<<10) /* BitField tcp_paramsTcp parameters	Selective Ack Enabled */
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT                                10
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV                                     (0x1<<11) /* BitField tcp_paramsTcp parameters	window smaller than initial window was advertised to far end */
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT                               11
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG                                     (0x1<<12) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                               12
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED                                  (0x1<<13) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT                            13
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER                     (0x3<<14) /* BitField tcp_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT               14
	uint8_t ts_enabled /* Only 1 bit is used */;
	uint8_t original_nagle_1b;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t pseudo_csum /* the precaluclated pseudo checksum header for pbf command construction */;
	uint16_t window_scaling_factor /*  local_adv_wnd by this variable to reach the advertised window to far end */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t window_scaling_factor /*  local_adv_wnd by this variable to reach the advertised window to far end */;
	uint16_t pseudo_csum /* the precaluclated pseudo checksum header for pbf command construction */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved2 /* The ID of the statistics client for counting common/L2 statistics */;
	uint8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
	uint8_t statistics_params;
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L2_STATSTICS                               (0x1<<0) /* BitField statistics_paramsTcp parameters	set by the driver, determines wheather or not to update l2 statistics */
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                         0
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L4_STATSTICS                               (0x1<<1) /* BitField statistics_paramsTcp parameters	set by the driver, determines wheather or not to update l4 statistics */
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                         1
		#define XSTORM_TCP_CONTEXT_SECTION_RESERVED                                          (0x3F<<2) /* BitField statistics_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_RESERVED_SHIFT                                    2
#elif defined(__LITTLE_ENDIAN)
	uint8_t statistics_params;
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L2_STATSTICS                               (0x1<<0) /* BitField statistics_paramsTcp parameters	set by the driver, determines wheather or not to update l2 statistics */
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                         0
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L4_STATSTICS                               (0x1<<1) /* BitField statistics_paramsTcp parameters	set by the driver, determines wheather or not to update l4 statistics */
		#define XSTORM_TCP_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                         1
		#define XSTORM_TCP_CONTEXT_SECTION_RESERVED                                          (0x3F<<2) /* BitField statistics_paramsTcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_RESERVED_SHIFT                                    2
	uint8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
	uint16_t reserved2 /* The ID of the statistics client for counting common/L2 statistics */;
#endif
	uint32_t ts_time_diff /* Time Stamp Offload, used in PBF Header Builder Command */;
	uint32_t __next_timer_expir /* Last Packet Real Time Clock Stamp */;
};

/*
 * Common context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_common_context_section
{
	struct xstorm_eth_context_section ethernet;
	union xstorm_ip_context_section_types ip_union;
	struct xstorm_tcp_context_section tcp;
#if defined(__BIG_ENDIAN)
	uint8_t __dcb_val;
	uint8_t flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED                              (0x1<<0) /* BitField flagsTcp parameters	part of the tx switching state machine */
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED_SHIFT                        0
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT                                       (0x7<<1) /* BitField flagsTcp parameters	determines to which voq credit will be returned */
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT_SHIFT                                 1
		#define XSTORM_COMMON_CONTEXT_SECTION_VLAN_MODE                                      (0x1<<4) /* BitField flagsTcp parameters	Flag that states wether inner valn was provided by the OS */
		#define XSTORM_COMMON_CONTEXT_SECTION_VLAN_MODE_SHIFT                                4
		#define XSTORM_COMMON_CONTEXT_SECTION_ORIGINAL_PRIORITY                              (0x7<<5) /* BitField flagsTcp parameters	original priority given from the OS */
		#define XSTORM_COMMON_CONTEXT_SECTION_ORIGINAL_PRIORITY_SHIFT                        5
	uint8_t outer_tag_flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_OUTER_PRI                                  (0x7<<0) /* BitField outer_tag_flagsTcp parameters	Priority of outer tag in case of DCB enabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_OUTER_PRI_SHIFT                            0
		#define XSTORM_COMMON_CONTEXT_SECTION_OUTER_PRI                                      (0x7<<3) /* BitField outer_tag_flagsTcp parameters	Priority of outer tag in case of DCB disabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_OUTER_PRI_SHIFT                                3
		#define XSTORM_COMMON_CONTEXT_SECTION_RESERVED                                       (0x3<<6) /* BitField outer_tag_flagsTcp parameters	 */
		#define XSTORM_COMMON_CONTEXT_SECTION_RESERVED_SHIFT                                 6
	uint8_t ip_version_1b;
#elif defined(__LITTLE_ENDIAN)
	uint8_t ip_version_1b;
	uint8_t outer_tag_flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_OUTER_PRI                                  (0x7<<0) /* BitField outer_tag_flagsTcp parameters	Priority of outer tag in case of DCB enabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_OUTER_PRI_SHIFT                            0
		#define XSTORM_COMMON_CONTEXT_SECTION_OUTER_PRI                                      (0x7<<3) /* BitField outer_tag_flagsTcp parameters	Priority of outer tag in case of DCB disabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_OUTER_PRI_SHIFT                                3
		#define XSTORM_COMMON_CONTEXT_SECTION_RESERVED                                       (0x3<<6) /* BitField outer_tag_flagsTcp parameters	 */
		#define XSTORM_COMMON_CONTEXT_SECTION_RESERVED_SHIFT                                 6
	uint8_t flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED                              (0x1<<0) /* BitField flagsTcp parameters	part of the tx switching state machine */
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED_SHIFT                        0
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT                                       (0x7<<1) /* BitField flagsTcp parameters	determines to which voq credit will be returned */
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT_SHIFT                                 1
		#define XSTORM_COMMON_CONTEXT_SECTION_VLAN_MODE                                      (0x1<<4) /* BitField flagsTcp parameters	Flag that states wether inner valn was provided by the OS */
		#define XSTORM_COMMON_CONTEXT_SECTION_VLAN_MODE_SHIFT                                4
		#define XSTORM_COMMON_CONTEXT_SECTION_ORIGINAL_PRIORITY                              (0x7<<5) /* BitField flagsTcp parameters	original priority given from the OS */
		#define XSTORM_COMMON_CONTEXT_SECTION_ORIGINAL_PRIORITY_SHIFT                        5
	uint8_t __dcb_val;
#endif
};

/*
 * Flags used in ISCSI context section
 */
struct xstorm_iscsi_context_flags
{
	uint8_t flags;
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA                                  (0x1<<0) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA_SHIFT                            0
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T                                     (0x1<<1) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T_SHIFT                               1
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST                                (0x1<<2) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST_SHIFT                          2
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST                                  (0x1<<3) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST_SHIFT                            3
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN                                   (0x1<<4) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN_SHIFT                             4
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ                                      (0x1<<5) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ_SHIFT                                5
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT                                  (0x1<<6) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT_SHIFT                            6
		#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4                                         (0x1<<7) /* BitField flags	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4_SHIFT                                   7
};

struct iscsi_task_context_entry_x
{
	uint32_t data_out_buffer_offset;
	uint32_t itt;
	uint32_t data_sn;
};

struct iscsi_task_context_entry_xuc_x_write_only
{
	uint32_t tx_r2t_sn /* Xstorm increments for every data-out seq sent. */;
};

struct iscsi_task_context_entry_xuc_xu_write_both
{
	uint32_t sgl_base_lo;
	uint32_t sgl_base_hi;
#if defined(__BIG_ENDIAN)
	uint8_t sgl_size;
	uint8_t sge_index;
	uint16_t sge_offset;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sge_offset;
	uint8_t sge_index;
	uint8_t sgl_size;
#endif
};

/*
 * iSCSI context section
 */
struct xstorm_iscsi_context_section
{
	uint32_t first_burst_length;
	uint32_t max_send_pdu_length;
	struct regpair_t sq_pbl_base;
	struct regpair_t sq_curr_pbe;
	struct regpair_t hq_pbl_base;
	struct regpair_t hq_curr_pbe_base;
	struct regpair_t r2tq_pbl_base;
	struct regpair_t r2tq_curr_pbe_base;
	struct regpair_t task_pbl_base;
#if defined(__BIG_ENDIAN)
	uint16_t data_out_count;
	struct xstorm_iscsi_context_flags flags;
	uint8_t task_pbl_cache_idx /* All-ones value stands for PBL not cached */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t task_pbl_cache_idx /* All-ones value stands for PBL not cached */;
	struct xstorm_iscsi_context_flags flags;
	uint16_t data_out_count;
#endif
	uint32_t seq_more_2_send;
	uint32_t pdu_more_2_send;
	struct iscsi_task_context_entry_x temp_tce_x;
	struct iscsi_task_context_entry_xuc_x_write_only temp_tce_x_wr;
	struct iscsi_task_context_entry_xuc_xu_write_both temp_tce_xu_wr;
	struct regpair_t lun;
	uint32_t exp_data_transfer_len_ttt /* Overloaded with ttt in multi-pdu sequences flow. */;
	uint32_t pdu_data_2_rxmit;
	uint32_t rxmit_bytes_2_dr;
#if defined(__BIG_ENDIAN)
	uint16_t rxmit_sge_offset;
	uint16_t hq_rxmit_cons;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hq_rxmit_cons;
	uint16_t rxmit_sge_offset;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t r2tq_cons;
	uint8_t rxmit_flags;
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD                                     (0x1<<0) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT                               0
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR                                 (0x1<<1) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT                           1
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU                                 (0x1<<2) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT                           2
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR                                      (0x1<<3) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT                                3
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR                                (0x1<<4) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT                          4
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING                                 (0x3<<5) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT                           5
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT                         (0x1<<7) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT                   7
	uint8_t rxmit_sge_idx;
#elif defined(__LITTLE_ENDIAN)
	uint8_t rxmit_sge_idx;
	uint8_t rxmit_flags;
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD                                     (0x1<<0) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT                               0
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR                                 (0x1<<1) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT                           1
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU                                 (0x1<<2) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT                           2
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR                                      (0x1<<3) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT                                3
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR                                (0x1<<4) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT                          4
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING                                 (0x3<<5) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT                           5
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT                         (0x1<<7) /* BitField rxmit_flags	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT                   7
	uint16_t r2tq_cons;
#endif
	uint32_t hq_rxmit_tcp_seq;
};

/*
 * Xstorm iSCSI Storm Context
 */
struct xstorm_iscsi_st_context
{
	struct xstorm_common_context_section common;
	struct xstorm_iscsi_context_section iscsi;
};

/*
 * Iscsi connection context
 */
struct iscsi_context
{
	struct ustorm_iscsi_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_iscsi_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_iscsi_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_iscsi_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_iscsi_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_iscsi_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct regpair_t upb_context /* UPb context */;
	struct xstorm_iscsi_st_context xstorm_st_context /* Xstorm storm context */;
	struct regpair_t xpb_context /* XPb context (inside the PBF) */;
	struct cstorm_iscsi_st_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * PDU header of an iSCSI DATA-OUT
 */
struct iscsi_data_pdu_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1                                       (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                  (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                            7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1                                       (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                  (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                            7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                         (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                   0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                            (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                      24
	struct regpair_t lun;
	uint32_t itt;
	uint32_t ttt;
	uint32_t rsrv2;
	uint32_t exp_stat_sn;
	uint32_t rsrv3;
	uint32_t data_sn;
	uint32_t buffer_offset;
	uint32_t rsrv4;
};


/*
 * PDU header of an iSCSI login request
 */
struct iscsi_login_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG                                        (0x3<<0) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG_SHIFT                                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG                                        (0x3<<2) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG_SHIFT                                  2
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0                                      (0x3<<4) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0_SHIFT                                4
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                               (0x1<<6) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                         6
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT                                    (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT_SHIFT                              7
	uint8_t version_max;
	uint8_t version_min;
#elif defined(__LITTLE_ENDIAN)
	uint8_t version_min;
	uint8_t version_max;
	uint8_t op_attr;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG                                        (0x3<<0) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG_SHIFT                                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG                                        (0x3<<2) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG_SHIFT                                  2
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0                                      (0x3<<4) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0_SHIFT                                4
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                               (0x1<<6) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                         6
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT                                    (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT_SHIFT                              7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                        (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                           (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                     24
	uint32_t isid_lo;
#if defined(__BIG_ENDIAN)
	uint16_t isid_hi;
	uint16_t tsih;
#elif defined(__LITTLE_ENDIAN)
	uint16_t tsih;
	uint16_t isid_hi;
#endif
	uint32_t itt;
#if defined(__BIG_ENDIAN)
	uint16_t cid;
	uint16_t rsrv1;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv1;
	uint16_t cid;
#endif
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t rsrv2[4];
};

/*
 * PDU header of an iSCSI logout request
 */
struct iscsi_logout_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE                               (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE_SHIFT                         0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                   (0x1<<7) /* BitField op_attr	this value must be 1 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                             7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE                               (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE_SHIFT                         0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                   (0x1<<7) /* BitField op_attr	this value must be 1 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                             7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                       (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                 0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                          (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                    24
	uint32_t rsrv2[2];
	uint32_t itt;
#if defined(__BIG_ENDIAN)
	uint16_t cid;
	uint16_t rsrv1;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv1;
	uint16_t cid;
#endif
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t rsrv3[4];
};

/*
 * PDU header of an iSCSI TMF request
 */
struct iscsi_tmf_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION                                     (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION_SHIFT                               0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                      (0x1<<7) /* BitField op_attr	this value must be 1 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                                7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION                                     (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION_SHIFT                               0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                      (0x1<<7) /* BitField op_attr	this value must be 1 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                                7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	uint32_t itt;
	uint32_t referenced_task_tag;
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t ref_cmd_sn;
	uint32_t exp_data_sn;
	uint32_t rsrv2[2];
};

/*
 * PDU header of an iSCSI Text request
 */
struct iscsi_text_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1                                       (0x3F<<0) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                                (0x1<<6) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                          6
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL                                       (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL_SHIFT                                 7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1                                       (0x3F<<0) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                                (0x1<<6) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                          6
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL                                       (0x1<<7) /* BitField op_attr	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL_SHIFT                                 7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                         (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                   0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                            (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                      24
	struct regpair_t lun;
	uint32_t itt;
	uint32_t ttt;
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t rsrv3[4];
};

/*
 * PDU header of an iSCSI Nop-Out
 */
struct iscsi_nop_out_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	uint8_t opcode;
	uint8_t op_attr;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1                                        (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1                                      (0x1<<7) /* BitField op_attr	this reserved bit must be set to 1 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1_SHIFT                                7
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint8_t op_attr;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1                                        (0x7F<<0) /* BitField op_attr	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1                                      (0x1<<7) /* BitField op_attr	this reserved bit must be set to 1 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1_SHIFT                                7
	uint8_t opcode;
#endif
	uint32_t data_fields;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	uint32_t itt;
	uint32_t ttt;
	uint32_t cmd_sn;
	uint32_t exp_stat_sn;
	uint32_t rsrv3[4];
};

/*
 * iscsi pdu headers in little endian form.
 */
union iscsi_pdu_headers_little_endian
{
	uint32_t fullHeaderSize[12] /* The full size of the header. protects the union size */;
	struct iscsi_cmd_pdu_hdr_little_endian command_pdu_hdr /* PDU header of an iSCSI command - read,write  */;
	struct iscsi_data_pdu_hdr_little_endian data_out_pdu_hdr /* PDU header of an iSCSI DATA-IN and DATA-OUT PDU  */;
	struct iscsi_login_req_hdr_little_endian login_req_pdu_hdr /* PDU header of an iSCSI Login request */;
	struct iscsi_logout_req_hdr_little_endian logout_req_pdu_hdr /* PDU header of an iSCSI Logout request */;
	struct iscsi_tmf_req_hdr_little_endian tmf_req_pdu_hdr /* PDU header of an iSCSI TMF request */;
	struct iscsi_text_req_hdr_little_endian text_req_pdu_hdr /* PDU header of an iSCSI Text request */;
	struct iscsi_nop_out_hdr_little_endian nop_out_pdu_hdr /* PDU header of an iSCSI Nop-Out */;
};

struct iscsi_hq_bd
{
	union iscsi_pdu_headers_little_endian pdu_header;
#if defined(__BIG_ENDIAN)
	uint16_t reserved1;
	uint16_t lcl_cmp_flg;
#elif defined(__LITTLE_ENDIAN)
	uint16_t lcl_cmp_flg;
	uint16_t reserved1;
#endif
	uint32_t sgl_base_lo;
	uint32_t sgl_base_hi;
#if defined(__BIG_ENDIAN)
	uint8_t sgl_size;
	uint8_t sge_index;
	uint16_t sge_offset;
#elif defined(__LITTLE_ENDIAN)
	uint16_t sge_offset;
	uint8_t sge_index;
	uint8_t sgl_size;
#endif
};


/*
 * CQE data for L2 OOO connection $$KEEP_ENDIANNESS$$
 */
struct iscsi_l2_ooo_data
{
	uint32_t iscsi_cid /* iSCSI context ID  */;
	uint8_t drop_isle /* isle number of the first isle to drop */;
	uint8_t drop_size /* number of isles to drop */;
	uint8_t ooo_opcode /* Out Of Order opcode (use enum tcp_ooo_event */;
	uint8_t ooo_isle /* OOO isle number to add the packet to */;
	uint8_t reserved[8];
};


struct iscsi_task_context_entry_xuc_c_write_only
{
	uint32_t total_data_acked /* Xstorm inits to zero. C increments. U validates  */;
};

struct iscsi_task_context_r2t_table_entry
{
	uint32_t ttt;
	uint32_t desired_data_len;
};

struct iscsi_task_context_entry_xuc_u_write_only
{
	uint32_t exp_r2t_sn /* Xstorm inits to zero. U increments. */;
	struct iscsi_task_context_r2t_table_entry r2t_table[4] /* U updates. X reads */;
#if defined(__BIG_ENDIAN)
	uint16_t data_in_count /* X inits to zero. U increments. */;
	uint8_t cq_id /* X inits to zero. U uses. */;
	uint8_t valid_1b /* X sets. U resets. */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t valid_1b /* X sets. U resets. */;
	uint8_t cq_id /* X inits to zero. U uses. */;
	uint16_t data_in_count /* X inits to zero. U increments. */;
#endif
};

struct iscsi_task_context_entry_xuc
{
	struct iscsi_task_context_entry_xuc_c_write_only write_c /* Cstorm only inits data here, without further change by any storm. */;
	uint32_t exp_data_transfer_len /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_x_write_only write_x /* only Xstorm writes data here. */;
	uint32_t lun_lo /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_xu_write_both write_xu /* Both X and U update this struct, but in different flow. */;
	uint32_t lun_hi /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_u_write_only write_u /* Ustorm only inits data here, without further change by any storm. */;
};

struct iscsi_task_context_entry_u
{
	uint32_t exp_r2t_buff_offset;
	uint32_t rem_rcv_len;
	uint32_t exp_data_sn;
};

struct iscsi_task_context_entry
{
	struct iscsi_task_context_entry_x tce_x;
#if defined(__BIG_ENDIAN)
	uint16_t data_out_count;
	uint16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rsrv0;
	uint16_t data_out_count;
#endif
	struct iscsi_task_context_entry_xuc tce_xuc;
	struct iscsi_task_context_entry_u tce_u;
	uint32_t rsrv1[7] /* increase the size to 128 bytes */;
};


struct iscsi_task_context_entry_xuc_x_init_only
{
	struct regpair_t lun /* X inits. U validates */;
	uint32_t exp_data_transfer_len /* Xstorm inits to SQ WQE data. U validates */;
};


/*
 * The data afex vif list ramrod need $$KEEP_ENDIANNESS$$
 */
struct afex_vif_list_ramrod_data
{
	uint8_t afex_vif_list_command /* set get, clear all a VIF list id defined by enum vif_list_rule_kind */;
	uint8_t func_bit_map /* the function bit map to set */;
	uint16_t vif_list_index /* the VIF list, in a per pf vector  to add this function to */;
	uint8_t func_to_clear /* the func id to clear in case of clear func mode */;
	uint8_t echo;
	uint16_t reserved1;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct c2s_pri_trans_table_entry
{
	uint8_t val[MAX_VLAN_PRIORITIES] /* Inner to outer vlan priority translation table entry for current PF */;
};


/*
 * cfc delete event data  $$KEEP_ENDIANNESS$$
 */
struct cfc_del_event_data
{
	uint32_t cid /* cid of deleted connection */;
	uint32_t reserved0;
	uint32_t reserved1;
};


/*
 * per-port SAFC demo variables
 */
struct cmng_flags_per_port
{
	uint32_t cmng_enables;
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_VN                                              (0x1<<0) /* BitField cmng_enablesenables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable fairness between vnics */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_VN_SHIFT                                        0
		#define CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN                                          (0x1<<1) /* BitField cmng_enablesenables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable rate shaping between vnics */
		#define CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN_SHIFT                                    1
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS                                             (0x1<<2) /* BitField cmng_enablesenables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable fairness between COSes */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_SHIFT                                       2
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_MODE                                        (0x1<<3) /* BitField cmng_enablesenables flag for fairness and rate shaping between protocols, vnics and COSes	 (use enum fairness_mode) */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_MODE_SHIFT                                  3
		#define __CMNG_FLAGS_PER_PORT_RESERVED0                                              (0xFFFFFFF<<4) /* BitField cmng_enablesenables flag for fairness and rate shaping between protocols, vnics and COSes	reserved */
		#define __CMNG_FLAGS_PER_PORT_RESERVED0_SHIFT                                        4
	uint32_t __reserved1;
};


/*
 * per-port rate shaping variables
 */
struct rate_shaping_vars_per_port
{
	uint32_t rs_periodic_timeout /* timeout of periodic timer */;
	uint32_t rs_threshold /* threshold, below which we start to stop queues */;
};

/*
 * per-port fairness variables
 */
struct fairness_vars_per_port
{
	uint32_t upper_bound /* Quota for a protocol/vnic */;
	uint32_t fair_threshold /* almost-empty threshold */;
	uint32_t fairness_timeout /* timeout of fairness timer */;
	uint32_t reserved0;
};

/*
 * per-port SAFC variables
 */
struct safc_struct_per_port
{
#if defined(__BIG_ENDIAN)
	uint16_t __reserved1;
	uint8_t __reserved0;
	uint8_t safc_timeout_usec /* timeout to stop queues on SAFC pause command */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t safc_timeout_usec /* timeout to stop queues on SAFC pause command */;
	uint8_t __reserved0;
	uint16_t __reserved1;
#endif
	uint8_t cos_to_traffic_types[MAX_COS_NUMBER] /* translate cos to service traffics types */;
	uint16_t cos_to_pause_mask[NUM_OF_SAFC_BITS] /* QM pause mask for each class of service in the SAFC frame */;
};

/*
 * Per-port congestion management variables
 */
struct cmng_struct_per_port
{
	struct rate_shaping_vars_per_port rs_vars;
	struct fairness_vars_per_port fair_vars;
	struct safc_struct_per_port safc_vars;
	struct cmng_flags_per_port flags;
};

/*
 * a single rate shaping counter. can be used as protocol or vnic counter
 */
struct rate_shaping_counter
{
	uint32_t quota /* Quota for a protocol/vnic */;
#if defined(__BIG_ENDIAN)
	uint16_t __reserved0;
	uint16_t rate /* Vnic/Protocol rate in units of Mega-bits/sec */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rate /* Vnic/Protocol rate in units of Mega-bits/sec */;
	uint16_t __reserved0;
#endif
};

/*
 * per-vnic rate shaping variables
 */
struct rate_shaping_vars_per_vn
{
	struct rate_shaping_counter vn_counter /* per-vnic counter */;
};

/*
 * per-vnic fairness variables
 */
struct fairness_vars_per_vn
{
	uint32_t cos_credit_delta[MAX_COS_NUMBER] /* used for incrementing the credit */;
	uint32_t vn_credit_delta /* used for incrementing the credit */;
	uint32_t __reserved0;
};

/*
 * cmng port init state
 */
struct cmng_vnic
{
	struct rate_shaping_vars_per_vn vnic_max_rate[4];
	struct fairness_vars_per_vn vnic_min_rate[4];
};

/*
 * cmng port init state
 */
struct cmng_init
{
	struct cmng_struct_per_port port;
	struct cmng_vnic vnic;
};


/*
 * driver parameters for congestion management init, all rates are in Mbps
 */
struct cmng_init_input
{
	uint32_t port_rate;
	uint16_t vnic_min_rate[4] /* rates are in Mbps */;
	uint16_t vnic_max_rate[4] /* rates are in Mbps */;
	uint16_t cos_min_rate[MAX_COS_NUMBER] /* rates are in Mbps */;
	uint16_t cos_to_pause_mask[MAX_COS_NUMBER];
	struct cmng_flags_per_port flags;
};


/*
 * Protocol-common command ID for slow path elements
 */
enum common_spqe_cmd_id
{
	RAMROD_CMD_ID_COMMON_UNUSED,
	RAMROD_CMD_ID_COMMON_FUNCTION_START /* Start a function (for PFs only) */,
	RAMROD_CMD_ID_COMMON_FUNCTION_STOP /* Stop a function (for PFs only) */,
	RAMROD_CMD_ID_COMMON_FUNCTION_UPDATE /* niv update function */,
	RAMROD_CMD_ID_COMMON_CFC_DEL /* Delete a connection from CFC */,
	RAMROD_CMD_ID_COMMON_CFC_DEL_WB /* Delete a connection from CFC (with write back) */,
	RAMROD_CMD_ID_COMMON_STAT_QUERY /* Collect statistics counters */,
	RAMROD_CMD_ID_COMMON_STOP_TRAFFIC /* Stop Tx traffic (before DCB updates) */,
	RAMROD_CMD_ID_COMMON_START_TRAFFIC /* Start Tx traffic (after DCB updates) */,
	RAMROD_CMD_ID_COMMON_AFEX_VIF_LISTS /* niv vif lists */,
	RAMROD_CMD_ID_COMMON_SET_TIMESYNC /* Set Timesync Parameters (E3 Only) */,
	MAX_COMMON_SPQE_CMD_ID};


/*
 * Per-protocol connection types
 */
enum connection_type
{
	ETH_CONNECTION_TYPE /* Ethernet */,
	TOE_CONNECTION_TYPE /* TOE */,
	RDMA_CONNECTION_TYPE /* RDMA */,
	ISCSI_CONNECTION_TYPE /* iSCSI */,
	FCOE_CONNECTION_TYPE /* FCoE */,
	RESERVED_CONNECTION_TYPE_0,
	RESERVED_CONNECTION_TYPE_1,
	RESERVED_CONNECTION_TYPE_2,
	NONE_CONNECTION_TYPE /* General- used for common slow path */,
	MAX_CONNECTION_TYPE};


/*
 * Cos modes
 */
enum cos_mode
{
	OVERRIDE_COS /* Firmware deduce cos according to DCB */,
	STATIC_COS /* Firmware has constant queues per CoS */,
	FW_WRR /* Firmware keep fairness between different CoSes */,
	MAX_COS_MODE};


/*
 * Dynamic HC counters set by the driver
 */
struct hc_dynamic_drv_counter
{
	uint32_t val[HC_SB_MAX_DYNAMIC_INDICES] /* 4 bytes * 4 indices = 2 lines */;
};

/*
 * zone A per-queue data
 */
struct cstorm_queue_zone_data
{
	struct hc_dynamic_drv_counter hc_dyn_drv_cnt /* 4 bytes * 4 indices = 2 lines */;
	struct regpair_t reserved[2];
};


/*
 * Vf-PF channel data in cstorm ram (non-triggered zone)
 */
struct vf_pf_channel_zone_data
{
	uint32_t msg_addr_lo /* the message address on VF memory */;
	uint32_t msg_addr_hi /* the message address on VF memory */;
};

/*
 * zone for VF non-triggered data
 */
struct non_trigger_vf_zone
{
	struct vf_pf_channel_zone_data vf_pf_channel /* vf-pf channel zone data */;
};

/*
 * Vf-PF channel trigger zone in cstorm ram
 */
struct vf_pf_channel_zone_trigger
{
	uint8_t addr_valid /* indicates that a vf-pf message is pending. MUST be set AFTER the message address.  */;
};

/*
 * zone that triggers the in-bound interrupt
 */
struct trigger_vf_zone
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved1;
	uint8_t reserved0;
	struct vf_pf_channel_zone_trigger vf_pf_channel;
#elif defined(__LITTLE_ENDIAN)
	struct vf_pf_channel_zone_trigger vf_pf_channel;
	uint8_t reserved0;
	uint16_t reserved1;
#endif
	uint32_t reserved2;
};

/*
 * zone B per-VF data
 */
struct cstorm_vf_zone_data
{
	struct non_trigger_vf_zone non_trigger /* zone for VF non-triggered data */;
	struct trigger_vf_zone trigger /* zone that triggers the in-bound interrupt */;
};


/*
 * Dynamic host coalescing init parameters, per state machine
 */
struct dynamic_hc_sm_config
{
	uint32_t threshold[3] /* thresholds of number of outstanding bytes */;
	uint8_t shift_per_protocol[HC_SB_MAX_DYNAMIC_INDICES] /* bytes difference of each protocol is shifted right by this value */;
	uint8_t hc_timeout0[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 0 for each protocol, in units of usec */;
	uint8_t hc_timeout1[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 1 for each protocol, in units of usec */;
	uint8_t hc_timeout2[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 2 for each protocol, in units of usec */;
	uint8_t hc_timeout3[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 3 for each protocol, in units of usec */;
};

/*
 * Dynamic host coalescing init parameters
 */
struct dynamic_hc_config
{
	struct dynamic_hc_sm_config sm_config[HC_SB_MAX_SM] /* Configuration per state machine */;
};


struct e2_integ_data
{
#if defined(__BIG_ENDIAN)
	uint8_t flags;
		#define E2_INTEG_DATA_TESTING_EN                                                     (0x1<<0) /* BitField flags	integration testing enabled */
		#define E2_INTEG_DATA_TESTING_EN_SHIFT                                               0
		#define E2_INTEG_DATA_LB_TX                                                          (0x1<<1) /* BitField flags	flag indicating this connection will transmit on loopback */
		#define E2_INTEG_DATA_LB_TX_SHIFT                                                    1
		#define E2_INTEG_DATA_COS_TX                                                         (0x1<<2) /* BitField flags	flag indicating this connection will transmit according to cos field */
		#define E2_INTEG_DATA_COS_TX_SHIFT                                                   2
		#define E2_INTEG_DATA_OPPORTUNISTICQM                                                (0x1<<3) /* BitField flags	flag indicating this connection will activate the opportunistic QM credit flow */
		#define E2_INTEG_DATA_OPPORTUNISTICQM_SHIFT                                          3
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ                                               (0x1<<4) /* BitField flags	flag indicating this connection will release the door bell queue (DQ) */
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ_SHIFT                                         4
		#define E2_INTEG_DATA_RESERVED                                                       (0x7<<5) /* BitField flags	 */
		#define E2_INTEG_DATA_RESERVED_SHIFT                                                 5
	uint8_t cos /* cos of the connection (relevant only in cos transmitting connections, when cosTx is set */;
	uint8_t voq /* voq to return credit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	uint8_t pbf_queue /* pbf queue to transmit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t pbf_queue /* pbf queue to transmit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	uint8_t voq /* voq to return credit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	uint8_t cos /* cos of the connection (relevant only in cos transmitting connections, when cosTx is set */;
	uint8_t flags;
		#define E2_INTEG_DATA_TESTING_EN                                                     (0x1<<0) /* BitField flags	integration testing enabled */
		#define E2_INTEG_DATA_TESTING_EN_SHIFT                                               0
		#define E2_INTEG_DATA_LB_TX                                                          (0x1<<1) /* BitField flags	flag indicating this connection will transmit on loopback */
		#define E2_INTEG_DATA_LB_TX_SHIFT                                                    1
		#define E2_INTEG_DATA_COS_TX                                                         (0x1<<2) /* BitField flags	flag indicating this connection will transmit according to cos field */
		#define E2_INTEG_DATA_COS_TX_SHIFT                                                   2
		#define E2_INTEG_DATA_OPPORTUNISTICQM                                                (0x1<<3) /* BitField flags	flag indicating this connection will activate the opportunistic QM credit flow */
		#define E2_INTEG_DATA_OPPORTUNISTICQM_SHIFT                                          3
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ                                               (0x1<<4) /* BitField flags	flag indicating this connection will release the door bell queue (DQ) */
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ_SHIFT                                         4
		#define E2_INTEG_DATA_RESERVED                                                       (0x7<<5) /* BitField flags	 */
		#define E2_INTEG_DATA_RESERVED_SHIFT                                                 5
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved3;
	uint8_t reserved2;
	uint8_t ramEn /* context area reserved for reading enable bit from ram */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t ramEn /* context area reserved for reading enable bit from ram */;
	uint8_t reserved2;
	uint16_t reserved3;
#endif
};


/*
 * set mac event data  $$KEEP_ENDIANNESS$$
 */
struct eth_event_data
{
	uint32_t echo /* set mac echo data to return to driver */;
	uint32_t reserved0;
	uint32_t reserved1;
};


/*
 * pf-vf event data  $$KEEP_ENDIANNESS$$
 */
struct vf_pf_event_data
{
	uint8_t vf_id /* VF ID (0-63) */;
	uint8_t reserved0;
	uint16_t reserved1;
	uint32_t msg_addr_lo /* message address on Vf (low 32 bits) */;
	uint32_t msg_addr_hi /* message address on Vf (high 32 bits) */;
};

/*
 * VF FLR event data  $$KEEP_ENDIANNESS$$
 */
struct vf_flr_event_data
{
	uint8_t vf_id /* VF ID (0-63) */;
	uint8_t reserved0;
	uint16_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
};

/*
 * malicious VF event data  $$KEEP_ENDIANNESS$$
 */
struct malicious_vf_event_data
{
	uint8_t vf_id /* VF ID (0-63) */;
	uint8_t err_id /* reason for malicious notification */;
	uint16_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
};

/*
 * vif list event data  $$KEEP_ENDIANNESS$$
 */
struct vif_list_event_data
{
	uint8_t func_bit_map /* bit map of pf indice */;
	uint8_t echo;
	uint16_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
};

/*
 * function update event data  $$KEEP_ENDIANNESS$$
 */
struct function_update_event_data
{
	uint8_t echo;
	uint8_t reserved;
	uint16_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
};

/*
 * union for all event ring message types
 */
union event_data
{
	struct vf_pf_event_data vf_pf_event /* vf-pf event data */;
	struct eth_event_data eth_event /* set mac event data */;
	struct cfc_del_event_data cfc_del_event /* cfc delete event data */;
	struct vf_flr_event_data vf_flr_event /* vf flr event data */;
	struct malicious_vf_event_data malicious_vf_event /* malicious vf event data */;
	struct vif_list_event_data vif_list_event /* vif list event data */;
	struct function_update_event_data function_update_event /* function update event data */;
};


/*
 * per PF event ring data
 */
struct event_ring_data
{
	struct regpair_native_t base_addr /* ring base address */;
#if defined(__BIG_ENDIAN)
	uint8_t index_id /* index ID within the status block */;
	uint8_t sb_id /* status block ID */;
	uint16_t producer /* event ring producer */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t producer /* event ring producer */;
	uint8_t sb_id /* status block ID */;
	uint8_t index_id /* index ID within the status block */;
#endif
	uint32_t reserved0;
};


/*
 * event ring message element (each element is 128 bits) $$KEEP_ENDIANNESS$$
 */
struct event_ring_msg
{
	uint8_t opcode;
	uint8_t error /* error on the mesasage */;
	uint16_t reserved1;
	union event_data data /* message data (96 bits data) */;
};

/*
 * event ring next page element (128 bits)
 */
struct event_ring_next
{
	struct regpair_t addr /* Address of the next page of the ring */;
	uint32_t reserved[2];
};

/*
 * union for event ring element types (each element is 128 bits)
 */
union event_ring_elem
{
	struct event_ring_msg message /* event ring message */;
	struct event_ring_next next_page /* event ring next page */;
};


/*
 * Common event ring opcodes
 */
enum event_ring_opcode
{
	EVENT_RING_OPCODE_VF_PF_CHANNEL,
	EVENT_RING_OPCODE_FUNCTION_START /* Start a function (for PFs only) */,
	EVENT_RING_OPCODE_FUNCTION_STOP /* Stop a function (for PFs only) */,
	EVENT_RING_OPCODE_CFC_DEL /* Delete a connection from CFC */,
	EVENT_RING_OPCODE_CFC_DEL_WB /* Delete a connection from CFC (with write back) */,
	EVENT_RING_OPCODE_STAT_QUERY /* Collect statistics counters */,
	EVENT_RING_OPCODE_STOP_TRAFFIC /* Stop Tx traffic (before DCB updates) */,
	EVENT_RING_OPCODE_START_TRAFFIC /* Start Tx traffic (after DCB updates) */,
	EVENT_RING_OPCODE_VF_FLR /* VF FLR indication for PF */,
	EVENT_RING_OPCODE_MALICIOUS_VF /* Malicious VF operation detected */,
	EVENT_RING_OPCODE_FORWARD_SETUP /* Initialize forward channel */,
	EVENT_RING_OPCODE_RSS_UPDATE_RULES /* Update RSS configuration */,
	EVENT_RING_OPCODE_FUNCTION_UPDATE /* function update */,
	EVENT_RING_OPCODE_AFEX_VIF_LISTS /* event ring opcode niv vif lists */,
	EVENT_RING_OPCODE_SET_MAC /* Add/remove MAC (in E1x only) */,
	EVENT_RING_OPCODE_CLASSIFICATION_RULES /* Add/remove MAC or VLAN (in E2/E3 only) */,
	EVENT_RING_OPCODE_FILTERS_RULES /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	EVENT_RING_OPCODE_MULTICAST_RULES /* Add/remove multicast classification bin (in E2/E3 only) */,
	EVENT_RING_OPCODE_SET_TIMESYNC /* Set Timesync Parameters (E3 Only) */,
	MAX_EVENT_RING_OPCODE};


/*
 * Modes for fairness algorithm
 */
enum fairness_mode
{
	FAIRNESS_COS_WRR_MODE /* Weighted round robin mode (used in Google) */,
	FAIRNESS_COS_ETS_MODE /* ETS mode (used in FCoE) */,
	MAX_FAIRNESS_MODE};


/*
 * Priority and cos $$KEEP_ENDIANNESS$$
 */
struct priority_cos
{
	uint8_t priority /* Priority */;
	uint8_t cos /* Cos */;
	uint16_t reserved1;
};

/*
 * The data for flow control configuration $$KEEP_ENDIANNESS$$
 */
struct flow_control_configuration
{
	struct priority_cos traffic_type_to_priority_cos[MAX_TRAFFIC_TYPES] /* traffic_type to priority cos */;
	uint8_t dcb_enabled /* If DCB mode is enabled then traffic class to priority array is fully initialized and there must be inner VLAN */;
	uint8_t dcb_version /* DCB version Increase by one on each DCB update */;
	uint8_t dont_add_pri_0 /* In case, the priority is 0, and the packet has no vlan, the firmware wont add vlan */;
	uint8_t reserved1;
	uint32_t reserved2;
	uint8_t dcb_outer_pri[MAX_TRAFFIC_TYPES] /* Indicates the updated DCB outer tag priority per protocol */;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct function_start_data
{
	uint8_t function_mode /* the function mode */;
	uint8_t allow_npar_tx_switching /* If set, inter-pf tx switching is allowed in Switch Independent function mode. (E2/E3 Only) */;
	uint16_t sd_vlan_tag /* value of Vlan in case of switch depended multi-function mode */;
	uint16_t vif_id /* value of VIF id in case of NIV multi-function mode */;
	uint8_t path_id;
	uint8_t network_cos_mode /* The cos mode for network traffic. */;
	uint8_t dmae_cmd_id /* The DMAE command id to use for FW DMAE transactions */;
	uint8_t no_added_tags /* If set, the mfTag length is always zero (used in UFP) */;
	uint16_t reserved0;
	uint32_t reserved1;
	uint8_t inner_clss_vxlan /* Classification type for VXLAN */;
	uint8_t inner_clss_l2gre /* If set, classification on the inner MAC/VLAN of L2GRE tunneled packets is enabled */;
	uint8_t inner_clss_l2geneve /* If set, classification on the inner MAC/(VLAN or VNI) of L2GENEVE tunneled packets is enabled */;
	uint8_t inner_rss /* If set, RSS on the inner headers of tunneled packets is enabled */;
	uint16_t vxlan_dst_port /* UDP Destination Port to be recognised as VXLAN tunneled packets (0 is disabled) */;
	uint16_t geneve_dst_port /* UDP Destination Port to be recognised as GENEVE tunneled packets (0 is disabled) */;
	uint8_t sd_accept_mf_clss_fail /* If set, accept packets that fail Multi-Function Switch-Dependent classification. Only one VNIC on the port can have this set to 1 */;
	uint8_t sd_accept_mf_clss_fail_match_ethtype /* If set, accepted packets must match the ethertype of sd_clss_fail_ethtype */;
	uint16_t sd_accept_mf_clss_fail_ethtype /* Ethertype to match in the case of sd_accept_mf_clss_fail_match_ethtype */;
	uint16_t sd_vlan_eth_type /* Value of ether-type to use in the case of switch dependent multi-function mode. Setting this to 0 uses the default value of 0x8100 */;
	uint8_t sd_vlan_force_pri_flg /* If set, the SD Vlan Priority is forced to the value of the sd_vlan_pri_force_val field regardless of the DCB or inband VLAN priority. */;
	uint8_t sd_vlan_force_pri_val /* value to force SD Vlan Priority if sd_vlan_pri_force_flg is set */;
	uint8_t c2s_pri_tt_valid /* When set, c2s_pri_trans_table is valid */;
	uint8_t c2s_pri_default /* This value will be the sVlan pri value in case no Cvlan is present */;
	uint8_t reserved2[6];
	struct c2s_pri_trans_table_entry c2s_pri_trans_table /* Inner to outer vlan priority translation table entry for current PF */;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct function_update_data
{
	uint8_t vif_id_change_flg /* If set, vif_id will be checked */;
	uint8_t afex_default_vlan_change_flg /* If set, afex_default_vlan will be checked */;
	uint8_t allowed_priorities_change_flg /* If set, allowed_priorities will be checked */;
	uint8_t network_cos_mode_change_flg /* If set, network_cos_mode will be checked */;
	uint16_t vif_id /* value of VIF id in case of NIV multi-function mode */;
	uint16_t afex_default_vlan /* value of default Vlan in case of NIV mf */;
	uint8_t allowed_priorities /* bit vector of allowed Vlan priorities for this VIF */;
	uint8_t network_cos_mode /* The cos mode for network traffic. */;
	uint8_t lb_mode_en_change_flg /* If set, lb_mode_en will be checked */;
	uint8_t lb_mode_en /* If set, niv loopback mode will be enabled */;
	uint8_t tx_switch_suspend_change_flg /* If set, tx_switch_suspend will be checked */;
	uint8_t tx_switch_suspend /* If set, TX switching TO this function will be disabled and packets will be dropped */;
	uint8_t echo;
	uint8_t update_tunn_cfg_flg /* If set, tunneling config for the function will be updated according to the following fields */;
	uint8_t inner_clss_vxlan /* Classification type for VXLAN */;
	uint8_t inner_clss_l2gre /* If set, classification on the inner MAC/VLAN of L2GRE tunneled packets is enabled */;
	uint8_t inner_clss_l2geneve /* If set, classification on the inner MAC/(VLAN or VNI) of L2GENEVE tunneled packets is enabled */;
	uint8_t inner_rss /* If set, RSS on the inner headers of tunneled packets is enabled */;
	uint16_t vxlan_dst_port /* UDP Destination Port to be recognised as VXLAN tunneled packets (0 is disabled) */;
	uint16_t geneve_dst_port /* UDP Destination Port to be recognised as GENEVE tunneled packets (0 is disabled) */;
	uint8_t sd_vlan_force_pri_change_flg /* If set, the SD VLAN Priority Fixed configuration is updated from fields sd_vlan_pri_force_flg and sd_vlan_pri_force_val */;
	uint8_t sd_vlan_force_pri_flg /* If set, the SD Vlan Priority is forced to the value of the sd_vlan_pri_force_val field regardless of the DCB or inband VLAN priority. */;
	uint8_t sd_vlan_force_pri_val /* value to force SD Vlan Priority if sd_vlan_pri_force_flg is set */;
	uint8_t sd_vlan_tag_change_flg /* If set, the SD VLAN Tag is changed according to the field sd_vlan_tag */;
	uint8_t sd_vlan_eth_type_change_flg /* If set, the SD VLAN Ethertype is changed according to the field sd_vlan_eth_type */;
	uint8_t reserved1;
	uint16_t sd_vlan_tag /* New value of Outer Vlan in case of switch depended multi-function mode */;
	uint16_t sd_vlan_eth_type /* New value of ether-type in the case of switch dependent multi-function mode. Setting this to 0 restores the default value of 0x8100 */;
	uint16_t reserved0;
	uint32_t reserved2;
};


/*
 * FW version stored in the Xstorm RAM
 */
struct fw_version
{
#if defined(__BIG_ENDIAN)
	uint8_t engineering /* firmware current engineering version */;
	uint8_t revision /* firmware current revision version */;
	uint8_t minor /* firmware current minor version */;
	uint8_t major /* firmware current major version */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t major /* firmware current major version */;
	uint8_t minor /* firmware current minor version */;
	uint8_t revision /* firmware current revision version */;
	uint8_t engineering /* firmware current engineering version */;
#endif
	uint32_t flags;
		#define FW_VERSION_OPTIMIZED                                                         (0x1<<0) /* BitField flags	if set, this is optimized ASM */
		#define FW_VERSION_OPTIMIZED_SHIFT                                                   0
		#define FW_VERSION_BIG_ENDIEN                                                        (0x1<<1) /* BitField flags	if set, this is big-endien ASM */
		#define FW_VERSION_BIG_ENDIEN_SHIFT                                                  1
		#define FW_VERSION_CHIP_VERSION                                                      (0x3<<2) /* BitField flags	0 - E1, 1 - E1H */
		#define FW_VERSION_CHIP_VERSION_SHIFT                                                2
		#define __FW_VERSION_RESERVED                                                        (0xFFFFFFF<<4) /* BitField flags	 */
		#define __FW_VERSION_RESERVED_SHIFT                                                  4
};


/*
 * Dynamic Host-Coalescing - Driver(host) counters
 */
struct hc_dynamic_sb_drv_counters
{
	uint32_t dynamic_hc_drv_counter[HC_SB_MAX_DYNAMIC_INDICES] /* Dynamic HC counters written by drivers */;
};


/*
 * 2 bytes. configuration/state parameters for a single protocol index
 */
struct hc_index_data
{
#if defined(__BIG_ENDIAN)
	uint8_t flags;
		#define HC_INDEX_DATA_SM_ID                                                          (0x1<<0) /* BitField flags	Index to a state machine. Can be 0 or 1 */
		#define HC_INDEX_DATA_SM_ID_SHIFT                                                    0
		#define HC_INDEX_DATA_HC_ENABLED                                                     (0x1<<1) /* BitField flags	if set, host coalescing would be done for this index */
		#define HC_INDEX_DATA_HC_ENABLED_SHIFT                                               1
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED                                             (0x1<<2) /* BitField flags	if set, dynamic HC will be done for this index */
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED_SHIFT                                       2
		#define HC_INDEX_DATA_RESERVE                                                        (0x1F<<3) /* BitField flags	 */
		#define HC_INDEX_DATA_RESERVE_SHIFT                                                  3
	uint8_t timeout /* the timeout values for this index. Units are 4 usec */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t timeout /* the timeout values for this index. Units are 4 usec */;
	uint8_t flags;
		#define HC_INDEX_DATA_SM_ID                                                          (0x1<<0) /* BitField flags	Index to a state machine. Can be 0 or 1 */
		#define HC_INDEX_DATA_SM_ID_SHIFT                                                    0
		#define HC_INDEX_DATA_HC_ENABLED                                                     (0x1<<1) /* BitField flags	if set, host coalescing would be done for this index */
		#define HC_INDEX_DATA_HC_ENABLED_SHIFT                                               1
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED                                             (0x1<<2) /* BitField flags	if set, dynamic HC will be done for this index */
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED_SHIFT                                       2
		#define HC_INDEX_DATA_RESERVE                                                        (0x1F<<3) /* BitField flags	 */
		#define HC_INDEX_DATA_RESERVE_SHIFT                                                  3
#endif
};


/*
 * HC state-machine
 */
struct hc_status_block_sm
{
#if defined(__BIG_ENDIAN)
	uint8_t igu_seg_id;
	uint8_t igu_sb_id /* sb_id within the IGU */;
	uint8_t timer_value /* Determines the time_to_expire */;
	uint8_t __flags;
#elif defined(__LITTLE_ENDIAN)
	uint8_t __flags;
	uint8_t timer_value /* Determines the time_to_expire */;
	uint8_t igu_sb_id /* sb_id within the IGU */;
	uint8_t igu_seg_id;
#endif
	uint32_t time_to_expire /* The time in which it expects to wake up */;
};

/*
 * hold PCI identification variables- used in various places in firmware
 */
struct pci_entity
{
#if defined(__BIG_ENDIAN)
	uint8_t vf_valid /* If set, this is a VF, otherwise it is PF */;
	uint8_t vf_id /* VF ID (0-63). Value of 0xFF means VF not valid */;
	uint8_t vnic_id /* Virtual NIC ID (0-3) */;
	uint8_t pf_id /* PCI physical function number (0-7). The LSB of this field is the port ID */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t pf_id /* PCI physical function number (0-7). The LSB of this field is the port ID */;
	uint8_t vnic_id /* Virtual NIC ID (0-3) */;
	uint8_t vf_id /* VF ID (0-63). Value of 0xFF means VF not valid */;
	uint8_t vf_valid /* If set, this is a VF, otherwise it is PF */;
#endif
};

/*
 * The fast-path status block meta-data, common to all chips
 */
struct hc_sb_data
{
	struct regpair_native_t host_sb_addr /* Host status block address */;
	struct hc_status_block_sm state_machine[HC_SB_MAX_SM] /* Holds the state machines of the status block */;
	struct pci_entity p_func /* vnic / port of the status block to be set by the driver */;
#if defined(__BIG_ENDIAN)
	uint8_t rsrv0;
	uint8_t state;
	uint8_t dhc_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this status block dynamic HC counters */;
	uint8_t same_igu_sb_1b /* Indicate that both state-machines acts like single sm */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t same_igu_sb_1b /* Indicate that both state-machines acts like single sm */;
	uint8_t dhc_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this status block dynamic HC counters */;
	uint8_t state;
	uint8_t rsrv0;
#endif
	struct regpair_native_t rsrv1[2];
};


/*
 * Segment types for host coaslescing
 */
enum hc_segment
{
	HC_REGULAR_SEGMENT,
	HC_DEFAULT_SEGMENT,
	MAX_HC_SEGMENT};


/*
 * The fast-path status block meta-data
 */
struct hc_sp_status_block_data
{
	struct regpair_native_t host_sb_addr /* Host status block address */;
#if defined(__BIG_ENDIAN)
	uint8_t rsrv1;
	uint8_t state;
	uint8_t igu_seg_id /* segment id of the IGU */;
	uint8_t igu_sb_id /* sb_id within the IGU */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t igu_sb_id /* sb_id within the IGU */;
	uint8_t igu_seg_id /* segment id of the IGU */;
	uint8_t state;
	uint8_t rsrv1;
#endif
	struct pci_entity p_func /* vnic / port of the status block to be set by the driver */;
};


/*
 * The fast-path status block meta-data
 */
struct hc_status_block_data_e1x
{
	struct hc_index_data index_data[HC_SB_MAX_INDICES_E1X] /* configuration/state parameters for a single protocol index */;
	struct hc_sb_data common /* The fast-path status block meta-data, common to all chips */;
};


/*
 * The fast-path status block meta-data
 */
struct hc_status_block_data_e2
{
	struct hc_index_data index_data[HC_SB_MAX_INDICES_E2] /* configuration/state parameters for a single protocol index */;
	struct hc_sb_data common /* The fast-path status block meta-data, common to all chips */;
};


/*
 * IGU block operartion modes (in Everest2)
 */
enum igu_mode
{
	HC_IGU_BC_MODE /* Backward compatible mode */,
	HC_IGU_NBC_MODE /* Non-backward compatible mode */,
	MAX_IGU_MODE};


/*
 * Inner Headers Classification Type
 */
enum inner_clss_type
{
	INNER_CLSS_DISABLED /* Inner Classification Disabled */,
	INNER_CLSS_USE_VLAN /* Inner Classification using MAC/Inner VLAN */,
	INNER_CLSS_USE_VNI /* Inner Classification using MAC/VNI (Only for VXLAN and GENEVE) */,
	MAX_INNER_CLSS_TYPE};


/*
 * IP versions
 */
enum ip_ver
{
	IP_V4,
	IP_V6,
	MAX_IP_VER};


/*
 * Malicious VF error ID
 */
enum malicious_vf_error_id
{
	MALICIOUS_VF_NO_ERROR /* Zero placeholder value */,
	VF_PF_CHANNEL_NOT_READY /* Writing to VF/PF channel when it is not ready */,
	ETH_ILLEGAL_BD_LENGTHS /* TX BD lengths error was detected */,
	ETH_PACKET_TOO_SHORT /* TX packet is shorter then reported on BDs */,
	ETH_PAYLOAD_TOO_BIG /* TX packet is greater then MTU */,
	ETH_ILLEGAL_ETH_TYPE /* TX packet reported without VLAN but eth type is 0x8100 */,
	ETH_ILLEGAL_LSO_HDR_LEN /* LSO header length on BDs and on hdr_nbd do not match */,
	ETH_TOO_MANY_BDS /* Tx packet has too many BDs */,
	ETH_ZERO_HDR_NBDS /* hdr_nbds field is zero */,
	ETH_START_BD_NOT_SET /* start_bd should be set on first TX BD in packet */,
	ETH_ILLEGAL_PARSE_NBDS /* Tx packet with parse_nbds field which is not legal */,
	ETH_IPV6_AND_CHECKSUM /* Tx packet with IP checksum on IPv6 */,
	ETH_VLAN_FLG_INCORRECT /* Tx packet with incorrect VLAN flag */,
	ETH_ILLEGAL_LSO_MSS /* Tx LSO packet with illegal MSS value */,
	ETH_TUNNEL_NOT_SUPPORTED /* Tunneling packets are not supported in current connection */,
	MAX_MALICIOUS_VF_ERROR_ID};


/*
 * Multi-function modes
 */
enum mf_mode
{
	SINGLE_FUNCTION,
	MULTI_FUNCTION_SD /* Switch dependent (vlan based) */,
	MULTI_FUNCTION_SI /* Switch independent (mac based) */,
	MULTI_FUNCTION_AFEX /* Switch dependent (niv based) */,
	MAX_MF_MODE};


/*
 * Protocol-common statistics collected by the Tstorm (per pf) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_pf_stats
{
	struct regpair_t rcv_error_bytes /* number of bytes received with errors */;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_pf_stats
{
	struct tstorm_per_pf_stats tstorm_pf_statistics;
};


/*
 * Protocol-common statistics collected by the Tstorm (per port) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_port_stats
{
	uint32_t mac_discard /* number of packets with mac errors */;
	uint32_t mac_filter_discard /* the number of good frames dropped because of no perfect match to MAC/VLAN address */;
	uint32_t brb_truncate_discard /* the number of packtes that were dropped because they were truncated in BRB */;
	uint32_t mf_tag_discard /* the number of good frames dropped because of no match to the outer vlan/VNtag */;
	uint32_t packet_drop /* general packet drop conter- incremented for every packet drop */;
	uint32_t reserved;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_port_stats
{
	struct tstorm_per_port_stats tstorm_port_statistics;
};


/*
 * Protocol-common statistics collected by the Tstorm (per client) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_queue_stats
{
	struct regpair_t rcv_ucast_bytes /* number of bytes in unicast packets received without errors and pass the filter */;
	uint32_t rcv_ucast_pkts /* number of unicast packets received without errors and pass the filter */;
	uint32_t checksum_discard /* number of total packets received with checksum error */;
	struct regpair_t rcv_bcast_bytes /* number of bytes in broadcast packets received without errors and pass the filter */;
	uint32_t rcv_bcast_pkts /* number of packets in broadcast packets received without errors and pass the filter */;
	uint32_t pkts_too_big_discard /* number of too long packets received */;
	struct regpair_t rcv_mcast_bytes /* number of bytes in multicast packets received without errors and pass the filter */;
	uint32_t rcv_mcast_pkts /* number of packets in multicast packets received without errors and pass the filter */;
	uint32_t ttl0_discard /* the number of good frames dropped because of TTL=0 */;
	uint16_t no_buff_discard;
	uint16_t reserved0;
	uint32_t reserved1;
};

/*
 * Protocol-common statistics collected by the Ustorm (per client) $$KEEP_ENDIANNESS$$
 */
struct ustorm_per_queue_stats
{
	struct regpair_t ucast_no_buff_bytes /* the number of unicast bytes received from network dropped because of no buffer at host */;
	struct regpair_t mcast_no_buff_bytes /* the number of multicast bytes received from network dropped because of no buffer at host */;
	struct regpair_t bcast_no_buff_bytes /* the number of broadcast bytes received from network dropped because of no buffer at host */;
	uint32_t ucast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	uint32_t mcast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	uint32_t bcast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	uint32_t coalesced_pkts /* the number of packets coalesced in all aggregations */;
	struct regpair_t coalesced_bytes /* the number of bytes coalesced in all aggregations */;
	uint32_t coalesced_events /* the number of aggregations */;
	uint32_t coalesced_aborts /* the number of exception which avoid aggregation */;
};

/*
 * Protocol-common statistics collected by the Xstorm (per client)  $$KEEP_ENDIANNESS$$
 */
struct xstorm_per_queue_stats
{
	struct regpair_t ucast_bytes_sent /* number of total bytes sent without errors */;
	struct regpair_t mcast_bytes_sent /* number of total bytes sent without errors */;
	struct regpair_t bcast_bytes_sent /* number of total bytes sent without errors */;
	uint32_t ucast_pkts_sent /* number of total packets sent without errors */;
	uint32_t mcast_pkts_sent /* number of total packets sent without errors */;
	uint32_t bcast_pkts_sent /* number of total packets sent without errors */;
	uint32_t error_drop_pkts /* number of total packets drooped due to errors */;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_queue_stats
{
	struct tstorm_per_queue_stats tstorm_queue_statistics;
	struct ustorm_per_queue_stats ustorm_queue_statistics;
	struct xstorm_per_queue_stats xstorm_queue_statistics;
};


/*
 * FW version stored in first line of pram $$KEEP_ENDIANNESS$$
 */
struct pram_fw_version
{
	uint8_t major /* firmware current major version */;
	uint8_t minor /* firmware current minor version */;
	uint8_t revision /* firmware current revision version */;
	uint8_t engineering /* firmware current engineering version */;
	uint8_t flags;
		#define PRAM_FW_VERSION_OPTIMIZED                                                    (0x1<<0) /* BitField flags	if set, this is optimized ASM */
		#define PRAM_FW_VERSION_OPTIMIZED_SHIFT                                              0
		#define PRAM_FW_VERSION_STORM_ID                                                     (0x3<<1) /* BitField flags	storm_id identification */
		#define PRAM_FW_VERSION_STORM_ID_SHIFT                                               1
		#define PRAM_FW_VERSION_BIG_ENDIEN                                                   (0x1<<3) /* BitField flags	if set, this is big-endien ASM */
		#define PRAM_FW_VERSION_BIG_ENDIEN_SHIFT                                             3
		#define PRAM_FW_VERSION_CHIP_VERSION                                                 (0x3<<4) /* BitField flags	0 - E1, 1 - E1H */
		#define PRAM_FW_VERSION_CHIP_VERSION_SHIFT                                           4
		#define __PRAM_FW_VERSION_RESERVED0                                                  (0x3<<6) /* BitField flags	 */
		#define __PRAM_FW_VERSION_RESERVED0_SHIFT                                            6
};


/*
 * Ethernet slow path element
 */
union protocol_common_specific_data
{
	uint8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t phy_address /* SPE physical address */;
	struct regpair_t mac_config_addr /* physical address of the MAC configuration command, as allocated by the driver */;
	struct afex_vif_list_ramrod_data afex_vif_list_data /* The data afex vif list ramrod need */;
};

/*
 * The send queue element
 */
struct protocol_common_spe
{
	struct spe_hdr_t hdr /* SPE header */;
	union protocol_common_specific_data data /* data specific to common protocol */;
};


/*
 * The data for the Set Timesync Ramrod $$KEEP_ENDIANNESS$$
 */
struct set_timesync_ramrod_data
{
	uint8_t drift_adjust_cmd /* Timesync Drift Adjust Command */;
	uint8_t offset_cmd /* Timesync Offset Command */;
	uint8_t add_sub_drift_adjust_value /* Whether to add(1)/subtract(0) Drift Adjust Value from the Offset */;
	uint8_t drift_adjust_value /* Drift Adjust Value (in ns) */;
	uint32_t drift_adjust_period /* Drift Adjust Period (in us) */;
	struct regpair_t offset_delta /* Timesync Offset Delta (in ns) */;
};


/*
 * The send queue element
 */
struct slow_path_element
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	struct regpair_t protocol_data /* additional data specific to the protocol */;
};


/*
 * Protocol-common statistics counter $$KEEP_ENDIANNESS$$
 */
struct stats_counter
{
	uint16_t xstats_counter /* xstorm statistics counter */;
	uint16_t reserved0;
	uint32_t reserved1;
	uint16_t tstats_counter /* tstorm statistics counter */;
	uint16_t reserved2;
	uint32_t reserved3;
	uint16_t ustats_counter /* ustorm statistics counter */;
	uint16_t reserved4;
	uint32_t reserved5;
	uint16_t cstats_counter /* ustorm statistics counter */;
	uint16_t reserved6;
	uint32_t reserved7;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct stats_query_entry
{
	uint8_t kind;
	uint8_t index /* queue index */;
	uint16_t funcID /* the func the statistic will send to */;
	uint32_t reserved;
	struct regpair_t address /* pxp address */;
};

/*
 * statistic command $$KEEP_ENDIANNESS$$
 */
struct stats_query_cmd_group
{
	struct stats_query_entry query[STATS_QUERY_CMD_COUNT];
};


/*
 * statistic command header $$KEEP_ENDIANNESS$$
 */
struct stats_query_header
{
	uint8_t cmd_num /* command number */;
	uint8_t reserved0;
	uint16_t drv_stats_counter;
	uint32_t reserved1;
	struct regpair_t stats_counters_addrs /* stats counter */;
};


/*
 * Types of statistcis query entry
 */
enum stats_query_type
{
	STATS_TYPE_QUEUE,
	STATS_TYPE_PORT,
	STATS_TYPE_PF,
	STATS_TYPE_TOE,
	STATS_TYPE_FCOE,
	MAX_STATS_QUERY_TYPE};


/*
 * Indicate of the function status block state
 */
enum status_block_state
{
	SB_DISABLED,
	SB_ENABLED,
	SB_CLEANED,
	MAX_STATUS_BLOCK_STATE};


/*
 * Storm IDs (including attentions for IGU related enums)
 */
enum storm_id
{
	USTORM_ID,
	CSTORM_ID,
	XSTORM_ID,
	TSTORM_ID,
	ATTENTION_ID,
	MAX_STORM_ID};


/*
 * Taffic types used in ETS and flow control algorithms
 */
enum traffic_type
{
	LLFC_TRAFFIC_TYPE_NW /* Networking */,
	LLFC_TRAFFIC_TYPE_FCOE /* FCoE */,
	LLFC_TRAFFIC_TYPE_ISCSI /* iSCSI */,
	MAX_TRAFFIC_TYPE};


/*
 * zone A per-queue data
 */
struct tstorm_queue_zone_data
{
	struct regpair_t reserved[4];
};


/*
 * zone B per-VF data
 */
struct tstorm_vf_zone_data
{
	struct regpair_t reserved;
};


/*
 * Add or Subtract Value for Set Timesync Ramrod
 */
enum ts_add_sub_value
{
	TS_SUB_VALUE /* Subtract Value */,
	TS_ADD_VALUE /* Add Value */,
	MAX_TS_ADD_SUB_VALUE};


/*
 * Drift-Adjust Commands for Set Timesync Ramrod
 */
enum ts_drift_adjust_cmd
{
	TS_DRIFT_ADJUST_KEEP /* Keep Drift-Adjust at current values */,
	TS_DRIFT_ADJUST_SET /* Set Drift-Adjust */,
	TS_DRIFT_ADJUST_RESET /* Reset Drift-Adjust */,
	MAX_TS_DRIFT_ADJUST_CMD};


/*
 * Offset Commands for Set Timesync Ramrod
 */
enum ts_offset_cmd
{
	TS_OFFSET_KEEP /* Keep Offset at current values */,
	TS_OFFSET_INC /* Increase Offset by Offset Delta */,
	TS_OFFSET_DEC /* Decrease Offset by Offset Delta */,
	MAX_TS_OFFSET_CMD};


/*
 * Input for measuring Pci Latency
 */
struct t_measure_pci_latency_ctrl
{
	struct regpair_t read_addr /* Address to read from */;
#if defined(__BIG_ENDIAN)
	uint8_t sleep /* Measure including a thread sleep */;
	uint8_t enable /* Enable PCI Latency measurements */;
	uint8_t func_id /* Function ID */;
	uint8_t read_size /* Amount of bytes to read */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t read_size /* Amount of bytes to read */;
	uint8_t func_id /* Function ID */;
	uint8_t enable /* Enable PCI Latency measurements */;
	uint8_t sleep /* Measure including a thread sleep */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t num_meas /* Number of measurements to make */;
	uint8_t reserved;
	uint8_t period_10us /* Number of 10s of microseconds to wait between measurements */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t period_10us /* Number of 10s of microseconds to wait between measurements */;
	uint8_t reserved;
	uint16_t num_meas /* Number of measurements to make */;
#endif
};


/*
 * Input for measuring Pci Latency
 */
struct t_measure_pci_latency_data
{
#if defined(__BIG_ENDIAN)
	uint16_t max_time_ns /* Maximum Time for a read (in ns) */;
	uint16_t min_time_ns /* Minimum Time for a read (in ns) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t min_time_ns /* Minimum Time for a read (in ns) */;
	uint16_t max_time_ns /* Maximum Time for a read (in ns) */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t reserved;
	uint16_t num_reads /* Number of reads - Used for Average */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t num_reads /* Number of reads - Used for Average */;
	uint16_t reserved;
#endif
	struct regpair_t sum_time_ns /* Sum of all the reads (in ns) - Used for Average */;
};


/*
 * zone A per-queue data
 */
struct ustorm_queue_zone_data
{
	struct ustorm_eth_rx_producers eth_rx_producers /* ETH RX rings producers */;
	struct regpair_t reserved[3];
};


/*
 * zone B per-VF data
 */
struct ustorm_vf_zone_data
{
	struct regpair_t reserved;
};


/*
 * data per VF-PF channel
 */
struct vf_pf_channel_data
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved0;
	uint8_t valid /* flag for channel validity. (cleared when identify a VF as malicious) */;
	uint8_t state /* channel state (ready / waiting for ack) */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t state /* channel state (ready / waiting for ack) */;
	uint8_t valid /* flag for channel validity. (cleared when identify a VF as malicious) */;
	uint16_t reserved0;
#endif
	uint32_t reserved1;
};


/*
 * State of VF-PF channel
 */
enum vf_pf_channel_state
{
	VF_PF_CHANNEL_STATE_READY /* Channel is ready to accept a message from VF */,
	VF_PF_CHANNEL_STATE_WAITING_FOR_ACK /* Channel waits for an ACK from PF */,
	MAX_VF_PF_CHANNEL_STATE};


/*
 * vif_list_rule_kind
 */
enum vif_list_rule_kind
{
	VIF_LIST_RULE_SET,
	VIF_LIST_RULE_GET,
	VIF_LIST_RULE_CLEAR_ALL,
	VIF_LIST_RULE_CLEAR_FUNC,
	MAX_VIF_LIST_RULE_KIND};


/*
 * zone A per-queue data
 */
struct xstorm_queue_zone_data
{
	struct regpair_t reserved[4];
};


/*
 * zone B per-VF data
 */
struct xstorm_vf_zone_data
{
	struct regpair_t reserved;
};


/*
 * Out-of-order states
 */
enum tcp_ooo_event
{
	TCP_EVENT_ADD_PEN=0,
	TCP_EVENT_ADD_NEW_ISLE=1,
	TCP_EVENT_ADD_ISLE_RIGHT=2,
	TCP_EVENT_ADD_ISLE_LEFT=3,
	TCP_EVENT_JOIN=4,
	TCP_EVENT_NOP=5,
	MAX_TCP_OOO_EVENT};


/*
 * OOO support modes
 */
enum tcp_tstorm_ooo
{
	TCP_TSTORM_OOO_DROP_AND_PROC_ACK,
	TCP_TSTORM_OOO_SEND_PURE_ACK,
	TCP_TSTORM_OOO_SUPPORTED,
	MAX_TCP_TSTORM_OOO};


/*
 * toe statistics collected by the Cstorm (per port)
 */
struct cstorm_toe_stats
{
	uint32_t no_tx_cqes /* count the number of time storm find that there are no more CQEs */;
	uint32_t reserved;
};


/*
 * The toe storm context of Cstorm
 */
struct cstorm_toe_st_context
{
	uint32_t bds_ring_page_base_addr_lo /* Base address of next page in host bds ring */;
	uint32_t bds_ring_page_base_addr_hi /* Base address of next page in host bds ring */;
	uint32_t free_seq /* Sequnce number of the last byte that was free including */;
	uint32_t __last_rel_to_notify /* Accumulated release size for the next Chimney completion msg */;
#if defined(__BIG_ENDIAN)
	uint16_t __rss_params_ram_line /* The ram line containing the rss params */;
	uint16_t bd_cons /* The bd s ring consumer  */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t bd_cons /* The bd s ring consumer  */;
	uint16_t __rss_params_ram_line /* The ram line containing the rss params */;
#endif
	uint32_t cpu_id /* CPU id for sending completion for TSS (only 8 bits are used) */;
	uint32_t prev_snd_max /* last snd_max that was used for dynamic HC producer update */;
	uint32_t __reserved4 /* reserved */;
};

/*
 * Cstorm Toe Storm Aligned Context
 */
struct cstorm_toe_st_aligned_context
{
	struct cstorm_toe_st_context context /* context */;
};


/*
 * prefetched isle bd
 */
struct ustorm_toe_prefetched_isle_bd
{
	uint32_t __addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	uint32_t __addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	uint8_t __reserved1 /* reserved */;
	uint8_t __isle_num /* isle_number of the pre-fetched BD */;
	uint16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd   0 size for buffer is not valid */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd   0 size for buffer is not valid */;
	uint8_t __isle_num /* isle_number of the pre-fetched BD */;
	uint8_t __reserved1 /* reserved */;
#endif
};

/*
 * ring params
 */
struct ustorm_toe_ring_params
{
	uint32_t rq_cons_addr_lo /* A pointer to the next to consume application bd */;
	uint32_t rq_cons_addr_hi /* A pointer to the next to consume application bd */;
#if defined(__BIG_ENDIAN)
	uint8_t __rq_local_cons /* consumer of the local rq ring */;
	uint8_t __rq_local_prod /* producer of the local rq ring */;
	uint16_t rq_cons /* RQ consumer is the index of the next to consume application bd */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rq_cons /* RQ consumer is the index of the next to consume application bd */;
	uint8_t __rq_local_prod /* producer of the local rq ring */;
	uint8_t __rq_local_cons /* consumer of the local rq ring */;
#endif
};

/*
 * prefetched bd
 */
struct ustorm_toe_prefetched_bd
{
	uint32_t __addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	uint32_t __addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	uint16_t flags;
		#define __USTORM_TOE_PREFETCHED_BD_START                                             (0x1<<0) /* BitField flagsbd command flags	this bd is the beginning of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_START_SHIFT                                       0
		#define __USTORM_TOE_PREFETCHED_BD_END                                               (0x1<<1) /* BitField flagsbd command flags	this bd is the end of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_END_SHIFT                                         1
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH                                           (0x1<<2) /* BitField flagsbd command flags	this application buffer must not be partially completed */
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH_SHIFT                                     2
		#define USTORM_TOE_PREFETCHED_BD_SPLIT                                               (0x1<<3) /* BitField flagsbd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define USTORM_TOE_PREFETCHED_BD_SPLIT_SHIFT                                         3
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1                                         (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1_SHIFT                                   4
	uint16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd   0 size for buffer is not valid */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd   0 size for buffer is not valid */;
	uint16_t flags;
		#define __USTORM_TOE_PREFETCHED_BD_START                                             (0x1<<0) /* BitField flagsbd command flags	this bd is the beginning of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_START_SHIFT                                       0
		#define __USTORM_TOE_PREFETCHED_BD_END                                               (0x1<<1) /* BitField flagsbd command flags	this bd is the end of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_END_SHIFT                                         1
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH                                           (0x1<<2) /* BitField flagsbd command flags	this application buffer must not be partially completed */
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH_SHIFT                                     2
		#define USTORM_TOE_PREFETCHED_BD_SPLIT                                               (0x1<<3) /* BitField flagsbd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define USTORM_TOE_PREFETCHED_BD_SPLIT_SHIFT                                         3
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1                                         (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1_SHIFT                                   4
#endif
};

/*
 * Ustorm Toe Storm Context
 */
struct ustorm_toe_st_context
{
	uint32_t __pen_rq_placed /* Number of bytes that were placed in the RQ and not completed yet. */;
	uint32_t pen_grq_placed_bytes /* The number of in-order bytes (peninsula) that were placed in the GRQ (excluding bytes that were already  copied  to RQ BDs or RQ dummy BDs) */;
#if defined(__BIG_ENDIAN)
	uint8_t flags2;
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH                                        (0x1<<0) /* BitField flags2various state flags	we will ignore grq push unless it is ping pong test */
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH_SHIFT                                  0
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG                                              (0x1<<1) /* BitField flags2various state flags	indicates if push timer is set */
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG_SHIFT                                        1
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED                                     (0x1<<2) /* BitField flags2various state flags	indicates if RSS update is supported */
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED_SHIFT                               2
		#define USTORM_TOE_ST_CONTEXT_RESERVED0                                              (0x1F<<3) /* BitField flags2various state flags	 */
		#define USTORM_TOE_ST_CONTEXT_RESERVED0_SHIFT                                        3
	uint8_t __indirection_shift /* Offset in bits of the cupid of this connection on the 64Bits fetched from internal memoy */;
	uint16_t indirection_ram_offset /* address offset in internal memory  from the beginning of the table  consisting the cpu id of this connection (Only 12 bits are used) */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t indirection_ram_offset /* address offset in internal memory  from the beginning of the table  consisting the cpu id of this connection (Only 12 bits are used) */;
	uint8_t __indirection_shift /* Offset in bits of the cupid of this connection on the 64Bits fetched from internal memoy */;
	uint8_t flags2;
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH                                        (0x1<<0) /* BitField flags2various state flags	we will ignore grq push unless it is ping pong test */
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH_SHIFT                                  0
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG                                              (0x1<<1) /* BitField flags2various state flags	indicates if push timer is set */
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG_SHIFT                                        1
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED                                     (0x1<<2) /* BitField flags2various state flags	indicates if RSS update is supported */
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED_SHIFT                               2
		#define USTORM_TOE_ST_CONTEXT_RESERVED0                                              (0x1F<<3) /* BitField flags2various state flags	 */
		#define USTORM_TOE_ST_CONTEXT_RESERVED0_SHIFT                                        3
#endif
	uint32_t __rq_available_bytes;
#if defined(__BIG_ENDIAN)
	uint8_t isles_counter /* signals that dca is enabled */;
	uint8_t __push_timer_state /* indicates if push timer is set */;
	uint16_t rcv_indication_size /* The chip will release the current GRQ buffer to the driver when it knows that the driver has no knowledge of other GRQ payload that it can indicate and the current GRQ buffer has at least RcvIndicationSize bytes. */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t rcv_indication_size /* The chip will release the current GRQ buffer to the driver when it knows that the driver has no knowledge of other GRQ payload that it can indicate and the current GRQ buffer has at least RcvIndicationSize bytes. */;
	uint8_t __push_timer_state /* indicates if push timer is set */;
	uint8_t isles_counter /* signals that dca is enabled */;
#endif
	uint32_t __min_expiration_time /* if the timer will expire before this time it will be considered as a race */;
	uint32_t initial_rcv_wnd /* the maximal advertized window */;
	uint32_t __bytes_cons /* the last rq_available_bytes producer that was read from host - used to know how many bytes were added */;
	uint32_t __prev_consumed_grq_bytes /* the last rq_available_bytes producer that was read from host - used to know how many bytes were added */;
	uint32_t prev_rcv_win_right_edge /* siquence of the last bytes that can be received - used to know how many bytes were added */;
	uint32_t rcv_nxt /* Receive sequence: next expected - of the right most received packet */;
	struct ustorm_toe_prefetched_isle_bd __isle_bd /* prefetched bd for the isle */;
	struct ustorm_toe_ring_params pen_ring_params /* peninsula ring params */;
	struct ustorm_toe_prefetched_bd __pen_bd_0 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_1 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_2 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_3 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_4 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_5 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_6 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_7 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_8 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_9 /* peninsula prefetched bd for the peninsula */;
	uint32_t __reserved3 /* reserved */;
};

/*
 * Ustorm Toe Storm Aligned Context
 */
struct ustorm_toe_st_aligned_context
{
	struct ustorm_toe_st_context context /* context */;
};

/*
 * TOE context region, used only in TOE
 */
struct tstorm_toe_st_context_section
{
	uint32_t reserved0[3];
};

/*
 * The TOE non-aggregative context of Tstorm
 */
struct tstorm_toe_st_context
{
	struct tstorm_tcp_st_context_section tcp /* TCP context region, shared in TOE, RDMA and ISCSI */;
	struct tstorm_toe_st_context_section toe /* TOE context region, used only in TOE */;
};

/*
 * The TOE non-aggregative aligned context of Tstorm
 */
struct tstorm_toe_st_aligned_context
{
	struct tstorm_toe_st_context context /* context */;
	uint8_t padding[16] /* padding to 64 byte aligned */;
};

/*
 * TOE context section
 */
struct xstorm_toe_context_section
{
	uint32_t tx_bd_page_base_lo /* BD page base address at the host for TxBdCons */;
	uint32_t tx_bd_page_base_hi /* BD page base address at the host for TxBdCons */;
#if defined(__BIG_ENDIAN)
	uint16_t tx_bd_offset /* The offset within the BD */;
	uint16_t tx_bd_cons /* The transmit BD cons pointer to the host ring */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t tx_bd_cons /* The transmit BD cons pointer to the host ring */;
	uint16_t tx_bd_offset /* The offset within the BD */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t bd_prod;
	uint16_t seqMismatchCnt;
#elif defined(__LITTLE_ENDIAN)
	uint16_t seqMismatchCnt;
	uint16_t bd_prod;
#endif
	uint32_t driver_doorbell_info_ptr_lo;
	uint32_t driver_doorbell_info_ptr_hi;
};

/*
 * Xstorm Toe Storm Context
 */
struct xstorm_toe_st_context
{
	struct xstorm_common_context_section common;
	struct xstorm_toe_context_section toe;
};

/*
 * Xstorm Toe Storm Aligned Context
 */
struct xstorm_toe_st_aligned_context
{
	struct xstorm_toe_st_context context /* context */;
};

/*
 * Ethernet connection context
 */
struct toe_context
{
	struct ustorm_toe_st_aligned_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_toe_st_aligned_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_toe_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_toe_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_toe_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_toe_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_toe_st_aligned_context xstorm_st_context /* Xstorm storm context */;
	struct cstorm_toe_st_aligned_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * ramrod data for toe protocol initiate offload ramrod (CQE)
 */
struct toe_initiate_offload_ramrod_data
{
	uint32_t flags;
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_SEARCH_CONFIG_FAILED                        (0x1<<0) /* BitField flags	error in searcher configuration */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_SEARCH_CONFIG_FAILED_SHIFT                  0
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_LICENSE_FAILURE                             (0x1<<1) /* BitField flags	license errors */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_LICENSE_FAILURE_SHIFT                       1
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_RESERVED0                                   (0x3FFFFFFF<<2) /* BitField flags	 */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_RESERVED0_SHIFT                             2
	uint32_t reserved1;
};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
struct toe_init_ramrod_data
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved1;
	uint8_t reserved0;
	uint8_t rss_num /* the rss num in its rqr to complete this ramrod */;
#elif defined(__LITTLE_ENDIAN)
	uint8_t rss_num /* the rss num in its rqr to complete this ramrod */;
	uint8_t reserved0;
	uint16_t reserved1;
#endif
	uint32_t reserved2;
};


/*
 * next page pointer bd used in toe CQs and tx/rx bd chains
 */
struct toe_page_addr_bd
{
	uint32_t addr_lo /* page pointer */;
	uint32_t addr_hi /* page pointer */;
	uint8_t reserved[8] /* resereved for driver use */;
};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
union toe_ramrod_data
{
	struct ramrod_data general;
	struct toe_initiate_offload_ramrod_data initiate_offload;
};


/*
 * TOE_RX_CQES_OPCODE_RSS_UPD results
 */
enum toe_rss_update_opcode
{
	TOE_RSS_UPD_QUIET,
	TOE_RSS_UPD_SLEEPING,
	TOE_RSS_UPD_DELAYED,
	MAX_TOE_RSS_UPDATE_OPCODE};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
struct toe_rss_update_ramrod_data
{
	uint8_t indirection_table[128] /* RSS indirection table */;
#if defined(__BIG_ENDIAN)
	uint16_t reserved0;
	uint16_t toe_rss_bitmap /* The bitmap specifies which toe rss chains to complete the ramrod on (0 bitmap is not valid option). The port is gleaned from the CID */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t toe_rss_bitmap /* The bitmap specifies which toe rss chains to complete the ramrod on (0 bitmap is not valid option). The port is gleaned from the CID */;
	uint16_t reserved0;
#endif
	uint32_t reserved1;
};


/*
 * The toe Rx Buffer Descriptor
 */
struct toe_rx_bd
{
	uint32_t addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	uint32_t addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	uint16_t flags;
		#define TOE_RX_BD_START                                                              (0x1<<0) /* BitField flagsbd command flags	this bd is the beginning of an application buffer */
		#define TOE_RX_BD_START_SHIFT                                                        0
		#define TOE_RX_BD_END                                                                (0x1<<1) /* BitField flagsbd command flags	this bd is the end of an application buffer */
		#define TOE_RX_BD_END_SHIFT                                                          1
		#define TOE_RX_BD_NO_PUSH                                                            (0x1<<2) /* BitField flagsbd command flags	this application buffer must not be partially completed */
		#define TOE_RX_BD_NO_PUSH_SHIFT                                                      2
		#define TOE_RX_BD_SPLIT                                                              (0x1<<3) /* BitField flagsbd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define TOE_RX_BD_SPLIT_SHIFT                                                        3
		#define TOE_RX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define TOE_RX_BD_RESERVED1_SHIFT                                                    4
	uint16_t size /* Size of the buffer pointed by the BD */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t size /* Size of the buffer pointed by the BD */;
	uint16_t flags;
		#define TOE_RX_BD_START                                                              (0x1<<0) /* BitField flagsbd command flags	this bd is the beginning of an application buffer */
		#define TOE_RX_BD_START_SHIFT                                                        0
		#define TOE_RX_BD_END                                                                (0x1<<1) /* BitField flagsbd command flags	this bd is the end of an application buffer */
		#define TOE_RX_BD_END_SHIFT                                                          1
		#define TOE_RX_BD_NO_PUSH                                                            (0x1<<2) /* BitField flagsbd command flags	this application buffer must not be partially completed */
		#define TOE_RX_BD_NO_PUSH_SHIFT                                                      2
		#define TOE_RX_BD_SPLIT                                                              (0x1<<3) /* BitField flagsbd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define TOE_RX_BD_SPLIT_SHIFT                                                        3
		#define TOE_RX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define TOE_RX_BD_RESERVED1_SHIFT                                                    4
#endif
	uint32_t dbg_bytes_prod /* a cyclic parameter that caounts how many byte were available for placement till no not including this bd */;
};


/*
 * ramrod data for toe protocol General rx completion
 */
struct toe_rx_completion_ramrod_data
{
#if defined(__BIG_ENDIAN)
	uint16_t reserved0;
	uint16_t hash_value /* information for ustorm to use in completion */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t hash_value /* information for ustorm to use in completion */;
	uint16_t reserved0;
#endif
	uint32_t reserved1;
};


/*
 * OOO params in union for TOE rx cqe data
 */
struct toe_rx_cqe_ooo_params
{
	uint32_t ooo_params;
		#define TOE_RX_CQE_OOO_PARAMS_NBYTES                                                 (0xFFFFFF<<0) /* BitField ooo_paramsdata params for OOO cqe	connection nbytes */
		#define TOE_RX_CQE_OOO_PARAMS_NBYTES_SHIFT                                           0
		#define TOE_RX_CQE_OOO_PARAMS_ISLE_NUM                                               (0xFF<<24) /* BitField ooo_paramsdata params for OOO cqe	isle number for OOO completions */
		#define TOE_RX_CQE_OOO_PARAMS_ISLE_NUM_SHIFT                                         24
};

/*
 * in order params in union for TOE rx cqe data
 */
struct toe_rx_cqe_in_order_params
{
	uint32_t in_order_params;
		#define TOE_RX_CQE_IN_ORDER_PARAMS_NBYTES                                            (0xFFFFFFFF<<0) /* BitField in_order_paramsdata params for in order cqe	connection nbytes */
		#define TOE_RX_CQE_IN_ORDER_PARAMS_NBYTES_SHIFT                                      0
};

/*
 * union for TOE rx cqe data
 */
union toe_rx_cqe_data_union
{
	struct toe_rx_cqe_ooo_params ooo_params /* data params for OOO cqe - nbytes and isle number */;
	struct toe_rx_cqe_in_order_params in_order_params /* data params for in order cqe - nbytes */;
	uint32_t raw_data /* global data param */;
};

/*
 * The toe Rx cq element
 */
struct toe_rx_cqe
{
	uint32_t params1;
		#define TOE_RX_CQE_CID                                                               (0xFFFFFF<<0) /* BitField params1completion cid and opcode	connection id */
		#define TOE_RX_CQE_CID_SHIFT                                                         0
		#define TOE_RX_CQE_COMPLETION_OPCODE                                                 (0xFF<<24) /* BitField params1completion cid and opcode	completion opcode - use enum toe_rx_cqe_type or toe_rss_update_opcode */
		#define TOE_RX_CQE_COMPLETION_OPCODE_SHIFT                                           24
	union toe_rx_cqe_data_union data /* completion cid and opcode */;
};


/*
 * toe rx doorbell data in host memory
 */
struct toe_rx_db_data
{
	uint32_t rcv_win_right_edge /* siquence of the last bytes that can be received */;
	uint32_t bytes_prod /* cyclic counter of posted bytes */;
#if defined(__BIG_ENDIAN)
	uint8_t reserved1 /* reserved */;
	uint8_t flags;
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES                                            (0x1<<0) /* BitField flags	ustorm ignores window updates when this flag is set */
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES_SHIFT                                      0
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF                                            (0x1<<1) /* BitField flags	indicates if to set push timer due to partially filled receive request after offload */
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF_SHIFT                                      1
		#define TOE_RX_DB_DATA_RESERVED0                                                     (0x3F<<2) /* BitField flags	 */
		#define TOE_RX_DB_DATA_RESERVED0_SHIFT                                               2
	uint16_t bds_prod /* cyclic counter of bds to post */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t bds_prod /* cyclic counter of bds to post */;
	uint8_t flags;
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES                                            (0x1<<0) /* BitField flags	ustorm ignores window updates when this flag is set */
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES_SHIFT                                      0
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF                                            (0x1<<1) /* BitField flags	indicates if to set push timer due to partially filled receive request after offload */
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF_SHIFT                                      1
		#define TOE_RX_DB_DATA_RESERVED0                                                     (0x3F<<2) /* BitField flags	 */
		#define TOE_RX_DB_DATA_RESERVED0_SHIFT                                               2
	uint8_t reserved1 /* reserved */;
#endif
	uint32_t consumed_grq_bytes /* cyclic counter of consumed grq bytes */;
};


/*
 * The toe Rx Generic Buffer Descriptor
 */
struct toe_rx_grq_bd
{
	uint32_t addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	uint32_t addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
};


/*
 * toe slow path element
 */
union toe_spe_data
{
	uint8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t phys_addr /* used in initiate offload ramrod */;
	struct toe_rx_completion_ramrod_data rx_completion /* used in all ramrods that have a general rx completion */;
	struct toe_init_ramrod_data toe_init /* used in toe init ramrod */;
};

/*
 * toe slow path element
 */
struct toe_spe
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	union toe_spe_data toe_data /* data specific to toe protocol */;
};


/*
 * TOE slow path opcodes (opcode 0 is illegal) - includes commands and completions
 */
enum toe_sq_opcode_type
{
	CMP_OPCODE_TOE_GA=1,
	CMP_OPCODE_TOE_GR=2,
	CMP_OPCODE_TOE_GNI=3,
	CMP_OPCODE_TOE_GAIR=4,
	CMP_OPCODE_TOE_GAIL=5,
	CMP_OPCODE_TOE_GRI=6,
	CMP_OPCODE_TOE_GJ=7,
	CMP_OPCODE_TOE_DGI=8,
	CMP_OPCODE_TOE_CMP=9,
	CMP_OPCODE_TOE_REL=10,
	CMP_OPCODE_TOE_SKP=11,
	CMP_OPCODE_TOE_URG=12,
	CMP_OPCODE_TOE_RT_TO=13,
	CMP_OPCODE_TOE_KA_TO=14,
	CMP_OPCODE_TOE_MAX_RT=15,
	CMP_OPCODE_TOE_DBT_RE=16,
	CMP_OPCODE_TOE_SYN=17,
	CMP_OPCODE_TOE_OPT_ERR=18,
	CMP_OPCODE_TOE_FW2_TO=19,
	CMP_OPCODE_TOE_2WY_CLS=20,
	CMP_OPCODE_TOE_TX_CMP=21,
	RAMROD_OPCODE_TOE_INIT=32,
	RAMROD_OPCODE_TOE_RSS_UPDATE=33,
	RAMROD_OPCODE_TOE_TERMINATE_RING=34,
	CMP_OPCODE_TOE_RST_RCV=48,
	CMP_OPCODE_TOE_FIN_RCV=49,
	CMP_OPCODE_TOE_FIN_UPL=50,
	CMP_OPCODE_TOE_SRC_ERR=51,
	CMP_OPCODE_TOE_LCN_ERR=52,
	RAMROD_OPCODE_TOE_INITIATE_OFFLOAD=80,
	RAMROD_OPCODE_TOE_SEARCHER_DELETE=81,
	RAMROD_OPCODE_TOE_TERMINATE=82,
	RAMROD_OPCODE_TOE_QUERY=83,
	RAMROD_OPCODE_TOE_RESET_SEND=84,
	RAMROD_OPCODE_TOE_INVALIDATE=85,
	RAMROD_OPCODE_TOE_EMPTY_RAMROD=86,
	RAMROD_OPCODE_TOE_UPDATE=87,
	MAX_TOE_SQ_OPCODE_TYPE};


/*
 * Toe statistics collected by the Xstorm (per port)
 */
struct xstorm_toe_stats_section
{
	uint32_t tcp_out_segments;
	uint32_t tcp_retransmitted_segments;
	struct regpair_t ip_out_octets;
	uint32_t ip_out_requests;
	uint32_t reserved;
};

/*
 * Toe statistics collected by the Xstorm (per port)
 */
struct xstorm_toe_stats
{
	struct xstorm_toe_stats_section statistics[2] /* 0 - ipv4 , 1 - ipv6 */;
	uint32_t reserved[2];
};

/*
 * Toe statistics collected by the Tstorm (per port)
 */
struct tstorm_toe_stats_section
{
	uint32_t ip_in_receives;
	uint32_t ip_in_delivers;
	struct regpair_t ip_in_octets;
	uint32_t tcp_in_errors /* all discards except discards already counted by Ipv4 stats */;
	uint32_t ip_in_header_errors /* IP checksum */;
	uint32_t ip_in_discards /* no resources */;
	uint32_t ip_in_truncated_packets;
};

/*
 * Toe statistics collected by the Tstorm (per port)
 */
struct tstorm_toe_stats
{
	struct tstorm_toe_stats_section statistics[2] /* 0 - ipv4 , 1 - ipv6 */;
	uint32_t reserved[2];
};

/*
 * Eth statistics query structure for the eth_stats_query ramrod
 */
struct toe_stats_query
{
	struct xstorm_toe_stats xstorm_toe /* Xstorm Toe statistics structure */;
	struct tstorm_toe_stats tstorm_toe /* Tstorm Toe statistics structure */;
	struct cstorm_toe_stats cstorm_toe /* Cstorm Toe statistics structure */;
};


/*
 * The toe Tx Buffer Descriptor
 */
struct toe_tx_bd
{
	uint32_t addr_lo /* tranasmit payload base address  - Single continuous buffer (page) pointer */;
	uint32_t addr_hi /* tranasmit payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	uint16_t flags;
		#define TOE_TX_BD_PUSH                                                               (0x1<<0) /* BitField flagsbd command flags	End of data flag */
		#define TOE_TX_BD_PUSH_SHIFT                                                         0
		#define TOE_TX_BD_NOTIFY                                                             (0x1<<1) /* BitField flagsbd command flags	notify driver with released data bytes including this bd */
		#define TOE_TX_BD_NOTIFY_SHIFT                                                       1
		#define TOE_TX_BD_FIN                                                                (0x1<<2) /* BitField flagsbd command flags	send fin request */
		#define TOE_TX_BD_FIN_SHIFT                                                          2
		#define TOE_TX_BD_LARGE_IO                                                           (0x1<<3) /* BitField flagsbd command flags	this bd is part of an application buffer larger than mss */
		#define TOE_TX_BD_LARGE_IO_SHIFT                                                     3
		#define TOE_TX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define TOE_TX_BD_RESERVED1_SHIFT                                                    4
	uint16_t size /* Size of the data represented by the BD */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t size /* Size of the data represented by the BD */;
	uint16_t flags;
		#define TOE_TX_BD_PUSH                                                               (0x1<<0) /* BitField flagsbd command flags	End of data flag */
		#define TOE_TX_BD_PUSH_SHIFT                                                         0
		#define TOE_TX_BD_NOTIFY                                                             (0x1<<1) /* BitField flagsbd command flags	notify driver with released data bytes including this bd */
		#define TOE_TX_BD_NOTIFY_SHIFT                                                       1
		#define TOE_TX_BD_FIN                                                                (0x1<<2) /* BitField flagsbd command flags	send fin request */
		#define TOE_TX_BD_FIN_SHIFT                                                          2
		#define TOE_TX_BD_LARGE_IO                                                           (0x1<<3) /* BitField flagsbd command flags	this bd is part of an application buffer larger than mss */
		#define TOE_TX_BD_LARGE_IO_SHIFT                                                     3
		#define TOE_TX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flagsbd command flags	reserved */
		#define TOE_TX_BD_RESERVED1_SHIFT                                                    4
#endif
	uint32_t nextBdStartSeq;
};


/*
 * The toe Tx cqe
 */
struct toe_tx_cqe
{
	uint32_t params;
		#define TOE_TX_CQE_CID                                                               (0xFFFFFF<<0) /* BitField paramscompletion cid and opcode	connection id */
		#define TOE_TX_CQE_CID_SHIFT                                                         0
		#define TOE_TX_CQE_COMPLETION_OPCODE                                                 (0xFF<<24) /* BitField paramscompletion cid and opcode	completion opcode (use enum toe_tx_cqe_type) */
		#define TOE_TX_CQE_COMPLETION_OPCODE_SHIFT                                           24
	uint32_t len /* the more2release in Bytes */;
};


/*
 * toe tx doorbell data in host memory
 */
struct toe_tx_db_data
{
	uint32_t bytes_prod_seq /* greatest sequence the chip can transmit */;
#if defined(__BIG_ENDIAN)
	uint16_t flags;
		#define TOE_TX_DB_DATA_FIN                                                           (0x1<<0) /* BitField flags	flag for post FIN request */
		#define TOE_TX_DB_DATA_FIN_SHIFT                                                     0
		#define TOE_TX_DB_DATA_FLUSH                                                         (0x1<<1) /* BitField flags	flag for last doorbell - flushing doorbell queue */
		#define TOE_TX_DB_DATA_FLUSH_SHIFT                                                   1
		#define TOE_TX_DB_DATA_RESERVE                                                       (0x3FFF<<2) /* BitField flags	 */
		#define TOE_TX_DB_DATA_RESERVE_SHIFT                                                 2
	uint16_t bds_prod /* cyclic counter of posted bds */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t bds_prod /* cyclic counter of posted bds */;
	uint16_t flags;
		#define TOE_TX_DB_DATA_FIN                                                           (0x1<<0) /* BitField flags	flag for post FIN request */
		#define TOE_TX_DB_DATA_FIN_SHIFT                                                     0
		#define TOE_TX_DB_DATA_FLUSH                                                         (0x1<<1) /* BitField flags	flag for last doorbell - flushing doorbell queue */
		#define TOE_TX_DB_DATA_FLUSH_SHIFT                                                   1
		#define TOE_TX_DB_DATA_RESERVE                                                       (0x3FFF<<2) /* BitField flags	 */
		#define TOE_TX_DB_DATA_RESERVE_SHIFT                                                 2
#endif
};


/*
 * sturct used in update ramrod. Driver notifies chip which fields have changed via the bitmap  $$KEEP_ENDIANNESS$$
 */
struct toe_update_ramrod_cached_params
{
	uint16_t changed_fields;
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_DEST_ADDR_CHANGED                            (0x1<<0) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_DEST_ADDR_CHANGED_SHIFT                      0
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MSS_CHANGED                                  (0x1<<1) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MSS_CHANGED_SHIFT                            1
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_TIMEOUT_CHANGED                           (0x1<<2) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_TIMEOUT_CHANGED_SHIFT                     2
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_INTERVAL_CHANGED                          (0x1<<3) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_INTERVAL_CHANGED_SHIFT                    3
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MAX_RT_CHANGED                               (0x1<<4) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MAX_RT_CHANGED_SHIFT                         4
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_RCV_INDICATION_SIZE_CHANGED                  (0x1<<5) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_RCV_INDICATION_SIZE_CHANGED_SHIFT            5
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_FLOW_LABEL_CHANGED                           (0x1<<6) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_FLOW_LABEL_CHANGED_SHIFT                     6
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_KEEPALIVE_CHANGED                     (0x1<<7) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_KEEPALIVE_CHANGED_SHIFT               7
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_NAGLE_CHANGED                         (0x1<<8) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_NAGLE_CHANGED_SHIFT                   8
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TTL_CHANGED                                  (0x1<<9) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TTL_CHANGED_SHIFT                            9
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_HOP_LIMIT_CHANGED                            (0x1<<10) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_HOP_LIMIT_CHANGED_SHIFT                      10
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TOS_CHANGED                                  (0x1<<11) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TOS_CHANGED_SHIFT                            11
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TRAFFIC_CLASS_CHANGED                        (0x1<<12) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TRAFFIC_CLASS_CHANGED_SHIFT                  12
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_MAX_PROBE_COUNT_CHANGED                   (0x1<<13) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_MAX_PROBE_COUNT_CHANGED_SHIFT             13
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_USER_PRIORITY_CHANGED                        (0x1<<14) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_USER_PRIORITY_CHANGED_SHIFT                  14
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_INITIAL_RCV_WND_CHANGED                      (0x1<<15) /* BitField changed_fieldsbitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_INITIAL_RCV_WND_CHANGED_SHIFT                15
	uint8_t ka_restart /* Only 1 bit is used */;
	uint8_t retransmit_restart /* Only 1 bit is used */;
	uint8_t dest_addr[6];
	uint16_t mss;
	uint32_t ka_timeout;
	uint32_t ka_interval;
	uint32_t max_rt;
	uint32_t flow_label /* Only 20 bits are used */;
	uint16_t rcv_indication_size;
	uint8_t enable_keepalive /* Only 1 bit is used */;
	uint8_t enable_nagle /* Only 1 bit is used */;
	uint8_t ttl;
	uint8_t hop_limit;
	uint8_t tos;
	uint8_t traffic_class;
	uint8_t ka_max_probe_count;
	uint8_t user_priority /* Only 4 bits are used */;
	uint16_t reserved2;
	uint32_t initial_rcv_wnd;
	uint32_t reserved1;
};


/*
 * rx rings pause data for E1h only
 */
struct ustorm_toe_rx_pause_data_e1h
{
#if defined(__BIG_ENDIAN)
	uint16_t grq_thr_low /* number of remaining grqes under which, we send pause message */;
	uint16_t cq_thr_low /* number of remaining cqes under which, we send pause message */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_thr_low /* number of remaining cqes under which, we send pause message */;
	uint16_t grq_thr_low /* number of remaining grqes under which, we send pause message */;
#endif
#if defined(__BIG_ENDIAN)
	uint16_t grq_thr_high /* number of remaining grqes above which, we send un-pause message */;
	uint16_t cq_thr_high /* number of remaining cqes above which, we send un-pause message */;
#elif defined(__LITTLE_ENDIAN)
	uint16_t cq_thr_high /* number of remaining cqes above which, we send un-pause message */;
	uint16_t grq_thr_high /* number of remaining grqes above which, we send un-pause message */;
#endif
};


#endif /* ECORE_HSI_H */

