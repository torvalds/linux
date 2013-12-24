/*
 * Xilinx PS USB OTG Driver Header file.
 *
 * Copyright 2011 Xilinx, Inc.
 *
 * This file is based on langwell_otg.h file with few minor modifications
 * to support Xilinx PS USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __XILINX_XUSBPS_OTG_H
#define __XILINX_XUSBPS_OTG_H

#define CI_USBCMD		0x140
#	define USBCMD_RST		BIT(1)
#	define USBCMD_RS		BIT(0)
#define CI_USBSTS		0x144
#	define USBSTS_SLI		BIT(8)
#	define USBSTS_URI		BIT(6)
#	define USBSTS_PCI		BIT(2)
#define CI_PORTSC1		0x184
#	define PORTSC_PP		BIT(12)
#	define PORTSC_LS		(BIT(11) | BIT(10))
#	define PORTSC_SUSP		BIT(7)
#	define PORTSC_CCS		BIT(0)
#define CI_OTGSC		0x1a4
#	define OTGSC_DPIE		BIT(30)
#	define OTGSC_1MSE		BIT(29)
#	define OTGSC_BSEIE		BIT(28)
#	define OTGSC_BSVIE		BIT(27)
#	define OTGSC_ASVIE		BIT(26)
#	define OTGSC_AVVIE		BIT(25)
#	define OTGSC_IDIE		BIT(24)
#	define OTGSC_DPIS		BIT(22)
#	define OTGSC_1MSS		BIT(21)
#	define OTGSC_BSEIS		BIT(20)
#	define OTGSC_BSVIS		BIT(19)
#	define OTGSC_ASVIS		BIT(18)
#	define OTGSC_AVVIS		BIT(17)
#	define OTGSC_IDIS		BIT(16)
#	define OTGSC_DPS		BIT(14)
#	define OTGSC_1MST		BIT(13)
#	define OTGSC_BSE		BIT(12)
#	define OTGSC_BSV		BIT(11)
#	define OTGSC_ASV		BIT(10)
#	define OTGSC_AVV		BIT(9)
#	define OTGSC_ID			BIT(8)
#	define OTGSC_HABA		BIT(7)
#	define OTGSC_HADP		BIT(6)
#	define OTGSC_IDPU		BIT(5)
#	define OTGSC_DP			BIT(4)
#	define OTGSC_OT			BIT(3)
#	define OTGSC_HAAR		BIT(2)
#	define OTGSC_VC			BIT(1)
#	define OTGSC_VD			BIT(0)
#	define OTGSC_INTEN_MASK		(0x7f << 24)
#	define OTGSC_INT_MASK		(0x5f << 24)
#	define OTGSC_INTSTS_MASK	(0x7f << 16)
#define CI_USBMODE		0x1a8
#	define USBMODE_CM		(BIT(1) | BIT(0))
#	define USBMODE_IDLE		0
#	define USBMODE_DEVICE		0x2
#	define USBMODE_HOST		0x3

#define INTR_DUMMY_MASK (USBSTS_SLI | USBSTS_URI | USBSTS_PCI)

enum xusbps_otg_timer_type {
	TA_WAIT_VRISE_TMR,
	TA_WAIT_BCON_TMR,
	TA_AIDL_BDIS_TMR,
	TB_ASE0_BRST_TMR,
	TB_SE0_SRP_TMR,
	TB_SRP_INIT_TMR,
	TB_SRP_FAIL_TMR,
	TB_BUS_SUSPEND_TMR
};

#define TA_WAIT_VRISE	100
#define TA_WAIT_BCON	30000
#define TA_AIDL_BDIS	15000
#define TB_ASE0_BRST	5000
#define TB_SE0_SRP	2
#define TB_SRP_INIT	100
#define TB_SRP_FAIL	5500
#define TB_BUS_SUSPEND	500

struct xusbps_otg_timer {
	unsigned long expires;	/* Number of count increase to timeout */
	unsigned long count;	/* Tick counter */
	void (*function)(unsigned long);	/* Timeout function */
	unsigned long data;	/* Data passed to function */
	struct list_head list;
};

/* This is a common data structure to
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

struct xusbps_otg {
	struct usb_phy		otg;
	struct usb_phy		*ulpi;

	struct otg_hsm		hsm;

	/* base address */
	void __iomem		*base;

	/* irq */
	int			irq;

	/* clk */
	struct clk		*clk;
	struct notifier_block	clk_rate_change_nb;

	/* atomic notifier for interrupt context */
	struct atomic_notifier_head	otg_notifier;

	/* start/stop USB Host function */
	int	(*start_host)(struct usb_phy *otg);
	int	(*stop_host)(struct usb_phy *otg);

	/* start/stop USB Peripheral function */
	int	(*start_peripheral)(struct usb_phy *otg);
	int	(*stop_peripheral)(struct usb_phy *otg);

	struct device			*dev;

	unsigned			region;

	struct work_struct		work;
	struct workqueue_struct		*qwork;
	struct timer_list		hsm_timer;

	spinlock_t			lock;
	spinlock_t			wq_lock;

	struct notifier_block		xotg_notifier;
};

static inline
struct xusbps_otg *xceiv_to_xotg(struct usb_phy *otg)
{
	return container_of(otg, struct xusbps_otg, otg);
}

void xusbps_update_transceiver(void);

#endif /* __XILINX_XUSBPS_OTG_H__ */
