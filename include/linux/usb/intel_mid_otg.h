/*
 * Intel MID (Langwell/Penwell) USB OTG Transceiver driver
 * Copyright (C) 2008 - 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef __INTEL_MID_OTG_H
#define __INTEL_MID_OTG_H

#include <linux/pm.h>
#include <linux/usb/otg.h>
#include <linux/notifier.h>

struct intel_mid_otg_xceiv;

/* This is a common data structure for Intel MID platform to
 * save values of the OTG state machine */
struct otg_hsm {
	/* Input */
	int a_bus_resume;
	int a_bus_suspend;
	int a_conn;
	int a_sess_vld;
	int a_srp_det;
	int a_vbus_vld;
	int b_bus_resume;
	int b_bus_suspend;
	int b_conn;
	int b_se0_srp;
	int b_ssend_srp;
	int b_sess_end;
	int b_sess_vld;
	int id;
/* id values */
#define ID_B		0x05
#define ID_A		0x04
#define ID_ACA_C	0x03
#define ID_ACA_B	0x02
#define ID_ACA_A	0x01
	int power_up;
	int adp_change;
	int test_device;

	/* Internal variables */
	int a_set_b_hnp_en;
	int b_srp_done;
	int b_hnp_enable;
	int hnp_poll_enable;

	/* Timeout indicator for timers */
	int a_wait_vrise_tmout;
	int a_wait_bcon_tmout;
	int a_aidl_bdis_tmout;
	int a_bidl_adis_tmout;
	int a_bidl_adis_tmr;
	int a_wait_vfall_tmout;
	int b_ase0_brst_tmout;
	int b_bus_suspend_tmout;
	int b_srp_init_tmout;
	int b_srp_fail_tmout;
	int b_srp_fail_tmr;
	int b_adp_sense_tmout;

	/* Informative variables */
	int a_bus_drop;
	int a_bus_req;
	int a_clr_err;
	int b_bus_req;
	int a_suspend_req;
	int b_bus_suspend_vld;

	/* Output */
	int drv_vbus;
	int loc_conn;
	int loc_sof;

	/* Others */
	int vbus_srp_up;
};

/* must provide ULPI access function to read/write registers implemented in
 * ULPI address space */
struct iotg_ulpi_access_ops {
	int	(*read)(struct intel_mid_otg_xceiv *iotg, u8 reg, u8 *val);
	int	(*write)(struct intel_mid_otg_xceiv *iotg, u8 reg, u8 val);
};

#define OTG_A_DEVICE	0x0
#define OTG_B_DEVICE	0x1

/*
 * the Intel MID (Langwell/Penwell) otg transceiver driver needs to interact
 * with device and host drivers to implement the USB OTG related feature. More
 * function members are added based on usb_phy data structure for this
 * purpose.
 */
struct intel_mid_otg_xceiv {
	struct usb_phy		otg;
	struct otg_hsm		hsm;

	/* base address */
	void __iomem		*base;

	/* ops to access ulpi */
	struct iotg_ulpi_access_ops	ulpi_ops;

	/* atomic notifier for interrupt context */
	struct atomic_notifier_head	iotg_notifier;

	/* start/stop USB Host function */
	int	(*start_host)(struct intel_mid_otg_xceiv *iotg);
	int	(*stop_host)(struct intel_mid_otg_xceiv *iotg);

	/* start/stop USB Peripheral function */
	int	(*start_peripheral)(struct intel_mid_otg_xceiv *iotg);
	int	(*stop_peripheral)(struct intel_mid_otg_xceiv *iotg);

	/* start/stop ADP sense/probe function */
	int	(*set_adp_probe)(struct intel_mid_otg_xceiv *iotg,
					bool enabled, int dev);
	int	(*set_adp_sense)(struct intel_mid_otg_xceiv *iotg,
					bool enabled);

#ifdef CONFIG_PM
	/* suspend/resume USB host function */
	int	(*suspend_host)(struct intel_mid_otg_xceiv *iotg,
					pm_message_t message);
	int	(*resume_host)(struct intel_mid_otg_xceiv *iotg);

	int	(*suspend_peripheral)(struct intel_mid_otg_xceiv *iotg,
					pm_message_t message);
	int	(*resume_peripheral)(struct intel_mid_otg_xceiv *iotg);
#endif

};
static inline
struct intel_mid_otg_xceiv *otg_to_mid_xceiv(struct usb_phy *otg)
{
	return container_of(otg, struct intel_mid_otg_xceiv, otg);
}

#define MID_OTG_NOTIFY_CONNECT		0x0001
#define MID_OTG_NOTIFY_DISCONN		0x0002
#define MID_OTG_NOTIFY_HSUSPEND		0x0003
#define MID_OTG_NOTIFY_HRESUME		0x0004
#define MID_OTG_NOTIFY_CSUSPEND		0x0005
#define MID_OTG_NOTIFY_CRESUME		0x0006
#define MID_OTG_NOTIFY_HOSTADD		0x0007
#define MID_OTG_NOTIFY_HOSTREMOVE	0x0008
#define MID_OTG_NOTIFY_CLIENTADD	0x0009
#define MID_OTG_NOTIFY_CLIENTREMOVE	0x000a

static inline int
intel_mid_otg_register_notifier(struct intel_mid_otg_xceiv *iotg,
				struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&iotg->iotg_notifier, nb);
}

static inline void
intel_mid_otg_unregister_notifier(struct intel_mid_otg_xceiv *iotg,
				struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&iotg->iotg_notifier, nb);
}

#endif /* __INTEL_MID_OTG_H */
