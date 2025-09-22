/*	$OpenBSD: igc_phy.h,v 1.3 2024/06/09 05:18:12 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_PHY_H_
#define _IGC_PHY_H_

void	igc_init_phy_ops_generic(struct igc_hw *);
int	igc_null_read_reg(struct igc_hw *, uint32_t, uint16_t *);
void	igc_null_phy_generic(struct igc_hw *);
int	igc_null_lplu_state(struct igc_hw *, bool);
int	igc_null_write_reg(struct igc_hw *, uint32_t, uint16_t);
int	igc_null_set_page(struct igc_hw *, uint16_t);
int	igc_check_downshift_generic(struct igc_hw *);
int	igc_check_reset_block_generic(struct igc_hw *);
int	igc_get_phy_id(struct igc_hw *);
int	igc_phy_hw_reset_generic(struct igc_hw *);
int	igc_setup_copper_link_generic(struct igc_hw *);
int	igc_phy_has_link_generic(struct igc_hw *, uint32_t, uint32_t, bool *);
void	igc_power_up_phy_copper(struct igc_hw *);
void	igc_power_down_phy_copper(struct igc_hw *);
int	igc_read_phy_reg_mdic(struct igc_hw *, uint32_t offset, uint16_t *);
int	igc_write_phy_reg_mdic(struct igc_hw *, uint32_t offset, uint16_t);
int	igc_read_xmdio_reg(struct igc_hw *, uint16_t, uint8_t, uint16_t *);
int	igc_write_xmdio_reg(struct igc_hw *, uint16_t, uint8_t, uint16_t);
int	igc_write_phy_reg_gpy(struct igc_hw *, uint32_t, uint16_t);
int	igc_read_phy_reg_gpy(struct igc_hw *, uint32_t, uint16_t *);
int	igc_wait_autoneg(struct igc_hw *);

/* IGP01IGC Specific Registers */
#define IGP01IGC_PHY_PORT_CONFIG	0x10 /* Port Config */
#define IGP01IGC_PHY_PORT_STATUS	0x11 /* Status */
#define IGP01IGC_PHY_PORT_CTRL		0x12 /* Control */
#define IGP01IGC_PHY_LINK_HEALTH	0x13 /* PHY Link Health */
#define IGP02IGC_PHY_POWER_MGMT		0x19 /* Power Management */
#define IGP01IGC_PHY_PAGE_SELECT	0x1F /* Page Select */
#define BM_PHY_PAGE_SELECT		22   /* Page Select for BM */
#define IGP_PAGE_SHIFT			5
#define PHY_REG_MASK			0x1F
#define IGC_I225_PHPM			0x0E14 /* I225 PHY Power Management */
#define IGC_I225_PHPM_DIS_1000_D3	0x0008 /* Disable 1G in D3 */
#define IGC_I225_PHPM_LINK_ENERGY	0x0010 /* Link Energy Detect */
#define IGC_I225_PHPM_GO_LINKD		0x0020 /* Go Link Disconnect */
#define IGC_I225_PHPM_DIS_1000		0x0040 /* Disable 1G globally */
#define IGC_I225_PHPM_SPD_B2B_EN	0x0080 /* Smart Power Down Back2Back */
#define IGC_I225_PHPM_RST_COMPL		0x0100 /* PHY Reset Completed */
#define IGC_I225_PHPM_DIS_100_D3	0x0200 /* Disable 100M in D3 */
#define IGC_I225_PHPM_ULP		0x0400 /* Ultra Low-Power Mode */
#define IGC_I225_PHPM_DIS_2500		0x0800 /* Disable 2.5G globally */
#define IGC_I225_PHPM_DIS_2500_D3	0x1000 /* Disable 2.5G in D3 */
/* GPY211 - I225 defines */
#define GPY_MMD_MASK			0xFFFF0000
#define GPY_MMD_SHIFT			16
#define GPY_REG_MASK			0x0000FFFF
#define IGP01IGC_PHY_PCS_INIT_REG	0x00B4
#define IGP01IGC_PHY_POLARITY_MASK	0x0078

#define IGP01IGC_PSCR_AUTO_MDIX	0x1000
#define IGP01IGC_PSCR_FORCE_MDI_MDIX	0x2000 /* 0=MDI, 1=MDIX */

#define IGP01IGC_PSCFR_SMART_SPEED	0x0080

#define IGP02IGC_PM_SPD			0x0001 /* Smart Power Down */
#define IGP02IGC_PM_D0_LPLU		0x0002 /* For D0a states */
#define IGP02IGC_PM_D3_LPLU		0x0004 /* For all other states */

#define IGP01IGC_PLHR_SS_DOWNGRADE	0x8000

#define IGP01IGC_PSSR_POLARITY_REVERSED	0x0002
#define IGP01IGC_PSSR_MDIX		0x0800
#define IGP01IGC_PSSR_SPEED_MASK	0xC000
#define IGP01IGC_PSSR_SPEED_1000MBPS	0xC000

#define IGP02IGC_PHY_CHANNEL_NUM	4
#define IGP02IGC_PHY_AGC_A		0x11B1
#define IGP02IGC_PHY_AGC_B		0x12B1
#define IGP02IGC_PHY_AGC_C		0x14B1
#define IGP02IGC_PHY_AGC_D		0x18B1

#define IGP02IGC_AGC_LENGTH_SHIFT	9	/* Course=15:13, Fine=12:9 */
#define IGP02IGC_AGC_LENGTH_MASK	0x7F
#define IGP02IGC_AGC_RANGE		15

#define IGC_CABLE_LENGTH_UNDEFINED	0xFF

#define IGC_KMRNCTRLSTA_OFFSET		0x001F0000
#define IGC_KMRNCTRLSTA_OFFSET_SHIFT	16
#define IGC_KMRNCTRLSTA_REN		0x00200000
#define IGC_KMRNCTRLSTA_DIAG_OFFSET	0x3    /* Kumeran Diagnostic */
#define IGC_KMRNCTRLSTA_TIMEOUTS	0x4    /* Kumeran Timeouts */
#define IGC_KMRNCTRLSTA_INBAND_PARAM	0x9    /* Kumeran InBand Parameters */
#define IGC_KMRNCTRLSTA_IBIST_DISABLE	0x0200 /* Kumeran IBIST Disable */
#define IGC_KMRNCTRLSTA_DIAG_NELPBK	0x1000 /* Nearend Loopback mode */

#define IFE_PHY_EXTENDED_STATUS_CONTROL	0x10
#define IFE_PHY_SPECIAL_CONTROL		0x11 /* 100BaseTx PHY Special Ctrl */
#define IFE_PHY_SPECIAL_CONTROL_LED	0x1B /* PHY Special and LED Ctrl */
#define IFE_PHY_MDIX_CONTROL		0x1C /* MDI/MDI-X Control */

/* IFE PHY Extended Status Control */
#define IFE_PESC_POLARITY_REVERSED	0x0100

/* IFE PHY Special Control */
#define IFE_PSC_AUTO_POLARITY_DISABLE	0x0010
#define IFE_PSC_FORCE_POLARITY		0x0020

/* IFE PHY Special Control and LED Control */
#define IFE_PSCL_PROBE_MODE		0x0020
#define IFE_PSCL_PROBE_LEDS_OFF		0x0006 /* Force LEDs 0 and 2 off */
#define IFE_PSCL_PROBE_LEDS_ON		0x0007 /* Force LEDs 0 and 2 on */

/* IFE PHY MDIX Control */
#define IFE_PMC_MDIX_STATUS		0x0020 /* 1=MDI-X, 0=MDI */
#define IFE_PMC_FORCE_MDIX		0x0040 /* 1=force MDI-X, 0=force MDI */
#define IFE_PMC_AUTO_MDIX		0x0080 /* 1=enable auto, 0=disable */

#endif	/* _IGC_PHY_H_ */
