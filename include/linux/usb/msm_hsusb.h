/* linux/include/asm-arm/arch-msm/hsusb.h
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 * Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_MSM_HSUSB_H
#define __ASM_ARCH_MSM_HSUSB_H

#include <linux/types.h>
#include <linux/usb/otg.h>

/**
 * Supported USB modes
 *
 * USB_PERIPHERAL       Only peripheral mode is supported.
 * USB_HOST             Only host mode is supported.
 * USB_OTG              OTG mode is supported.
 *
 */
enum usb_mode_type {
	USB_NONE = 0,
	USB_PERIPHERAL,
	USB_HOST,
	USB_OTG,
};

/**
 * OTG control
 *
 * OTG_NO_CONTROL	Id/VBUS notifications not required. Useful in host
 *                      only configuration.
 * OTG_PHY_CONTROL	Id/VBUS notifications comes form USB PHY.
 * OTG_PMIC_CONTROL	Id/VBUS notifications comes from PMIC hardware.
 * OTG_USER_CONTROL	Id/VBUS notifcations comes from User via sysfs.
 *
 */
enum otg_control_type {
	OTG_NO_CONTROL = 0,
	OTG_PHY_CONTROL,
	OTG_PMIC_CONTROL,
	OTG_USER_CONTROL,
};

/**
 * PHY used in
 *
 * INVALID_PHY			Unsupported PHY
 * CI_45NM_INTEGRATED_PHY	Chipidea 45nm integrated PHY
 * SNPS_28NM_INTEGRATED_PHY	Synopsis 28nm integrated PHY
 *
 */
enum msm_usb_phy_type {
	INVALID_PHY = 0,
	CI_45NM_INTEGRATED_PHY,
	SNPS_28NM_INTEGRATED_PHY,
};

#define IDEV_CHG_MAX	1500
#define IUNIT		100

/**
 * Different states involved in USB charger detection.
 *
 * USB_CHG_STATE_UNDEFINED	USB charger is not connected or detection
 *                              process is not yet started.
 * USB_CHG_STATE_WAIT_FOR_DCD	Waiting for Data pins contact.
 * USB_CHG_STATE_DCD_DONE	Data pin contact is detected.
 * USB_CHG_STATE_PRIMARY_DONE	Primary detection is completed (Detects
 *                              between SDP and DCP/CDP).
 * USB_CHG_STATE_SECONDARY_DONE	Secondary detection is completed (Detects
 *                              between DCP and CDP).
 * USB_CHG_STATE_DETECTED	USB charger type is determined.
 *
 */
enum usb_chg_state {
	USB_CHG_STATE_UNDEFINED = 0,
	USB_CHG_STATE_WAIT_FOR_DCD,
	USB_CHG_STATE_DCD_DONE,
	USB_CHG_STATE_PRIMARY_DONE,
	USB_CHG_STATE_SECONDARY_DONE,
	USB_CHG_STATE_DETECTED,
};

/**
 * USB charger types
 *
 * USB_INVALID_CHARGER	Invalid USB charger.
 * USB_SDP_CHARGER	Standard downstream port. Refers to a downstream port
 *                      on USB2.0 compliant host/hub.
 * USB_DCP_CHARGER	Dedicated charger port (AC charger/ Wall charger).
 * USB_CDP_CHARGER	Charging downstream port. Enumeration can happen and
 *                      IDEV_CHG_MAX can be drawn irrespective of USB state.
 *
 */
enum usb_chg_type {
	USB_INVALID_CHARGER = 0,
	USB_SDP_CHARGER,
	USB_DCP_CHARGER,
	USB_CDP_CHARGER,
};

/**
 * struct msm_otg_platform_data - platform device data
 *              for msm_otg driver.
 * @phy_init_seq: PHY configuration sequence. val, reg pairs
 *              terminated by -1.
 * @vbus_power: VBUS power on/off routine.
 * @power_budget: VBUS power budget in mA (0 will be treated as 500mA).
 * @mode: Supported mode (OTG/peripheral/host).
 * @otg_control: OTG switch controlled by user/Id pin
 * @default_mode: Default operational mode. Applicable only if
 *              OTG switch is controller by user.
 * @pclk_src_name: pclk is derived from ebi1_usb_clk in case of 7x27 and 8k
 *              dfab_usb_hs_clk in case of 8660 and 8960.
 */
struct msm_otg_platform_data {
	int *phy_init_seq;
	void (*vbus_power)(bool on);
	unsigned power_budget;
	enum usb_mode_type mode;
	enum otg_control_type otg_control;
	enum usb_mode_type default_mode;
	enum msm_usb_phy_type phy_type;
	void (*setup_gpio)(enum usb_otg_state state);
	char *pclk_src_name;
};

/**
 * struct msm_otg: OTG driver data. Shared by HCD and DCD.
 * @otg: USB OTG Transceiver structure.
 * @pdata: otg device platform data.
 * @irq: IRQ number assigned for HSUSB controller.
 * @clk: clock struct of usb_hs_clk.
 * @pclk: clock struct of usb_hs_pclk.
 * @pclk_src: pclk source for voting.
 * @phy_reset_clk: clock struct of usb_phy_clk.
 * @core_clk: clock struct of usb_hs_core_clk.
 * @regs: ioremapped register base address.
 * @inputs: OTG state machine inputs(Id, SessValid etc).
 * @sm_work: OTG state machine work.
 * @in_lpm: indicates low power mode (LPM) state.
 * @async_int: Async interrupt arrived.
 * @cur_power: The amount of mA available from downstream port.
 * @chg_work: Charger detection work.
 * @chg_state: The state of charger detection process.
 * @chg_type: The type of charger attached.
 * @dcd_retires: The retry count used to track Data contact
 *               detection process.
 */
struct msm_otg {
	struct otg_transceiver otg;
	struct msm_otg_platform_data *pdata;
	int irq;
	struct clk *clk;
	struct clk *pclk;
	struct clk *pclk_src;
	struct clk *phy_reset_clk;
	struct clk *core_clk;
	void __iomem *regs;
#define ID		0
#define B_SESS_VLD	1
	unsigned long inputs;
	struct work_struct sm_work;
	atomic_t in_lpm;
	int async_int;
	unsigned cur_power;
	struct delayed_work chg_work;
	enum usb_chg_state chg_state;
	enum usb_chg_type chg_type;
	u8 dcd_retries;
};

#endif
