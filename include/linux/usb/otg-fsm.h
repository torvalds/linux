// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2007,2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_USB_OTG_FSM_H
#define __LINUX_USB_OTG_FSM_H

#include <linux/mutex.h>
#include <linux/errno.h>

#define PROTO_UNDEF	(0)
#define PROTO_HOST	(1)
#define PROTO_GADGET	(2)

#define OTG_STS_SELECTOR	0xF000	/* OTG status selector, according to
					 * OTG and EH 2.0 Chapter 6.2.3
					 * Table:6-4
					 */

#define HOST_REQUEST_FLAG	1	/* Host request flag, according to
					 * OTG and EH 2.0 Charpter 6.2.3
					 * Table:6-5
					 */

#define T_HOST_REQ_POLL		(1500)	/* 1500ms, HNP polling interval */

enum otg_fsm_timer {
	/* Standard OTG timers */
	A_WAIT_VRISE,
	A_WAIT_VFALL,
	A_WAIT_BCON,
	A_AIDL_BDIS,
	B_ASE0_BRST,
	A_BIDL_ADIS,
	B_AIDL_BDIS,

	/* Auxiliary timers */
	B_SE0_SRP,
	B_SRP_FAIL,
	A_WAIT_ENUM,
	B_DATA_PLS,
	B_SSEND_SRP,

	NUM_OTG_FSM_TIMERS,
};

/**
 * struct otg_fsm - OTG state machine according to the OTG spec
 *
 * OTG hardware Inputs
 *
 *	Common inputs for A and B device
 * @id:		TRUE for B-device, FALSE for A-device.
 * @adp_change: TRUE when current ADP measurement (n) value, compared to the
 *		ADP measurement taken at n-2, differs by more than CADP_THR
 * @power_up:	TRUE when the OTG device first powers up its USB system and
 *		ADP measurement taken if ADP capable
 *
 *	A-Device state inputs
 * @a_srp_det:	TRUE if the A-device detects SRP
 * @a_vbus_vld:	TRUE when VBUS voltage is in regulation
 * @b_conn:	TRUE if the A-device detects connection from the B-device
 * @a_bus_resume: TRUE when the B-device detects that the A-device is signaling
 *		  a resume (K state)
 *	B-Device state inputs
 * @a_bus_suspend: TRUE when the B-device detects that the A-device has put the
 *		bus into suspend
 * @a_conn:	TRUE if the B-device detects a connection from the A-device
 * @b_se0_srp:	TRUE when the line has been at SE0 for more than the minimum
 *		time before generating SRP
 * @b_ssend_srp: TRUE when the VBUS has been below VOTG_SESS_VLD for more than
 *		 the minimum time before generating SRP
 * @b_sess_vld:	TRUE when the B-device detects that the voltage on VBUS is
 *		above VOTG_SESS_VLD
 * @test_device: TRUE when the B-device switches to B-Host and detects an OTG
 *		test device. This must be set by host/hub driver
 *
 *	Application inputs (A-Device)
 * @a_bus_drop:	TRUE when A-device application needs to power down the bus
 * @a_bus_req:	TRUE when A-device application wants to use the bus.
 *		FALSE to suspend the bus
 *
 *	Application inputs (B-Device)
 * @b_bus_req:	TRUE during the time that the Application running on the
 *		B-device wants to use the bus
 *
 *	Auxiliary inputs (OTG v1.3 only. Obsolete now.)
 * @a_sess_vld:	TRUE if the A-device detects that VBUS is above VA_SESS_VLD
 * @b_bus_suspend: TRUE when the A-device detects that the B-device has put
 *		the bus into suspend
 * @b_bus_resume: TRUE when the A-device detects that the B-device is signaling
 *		 resume on the bus
 *
 * OTG Output status. Read only for users. Updated by OTG FSM helpers defined
 * in this file
 *
 *	Outputs for Both A and B device
 * @drv_vbus:	TRUE when A-device is driving VBUS
 * @loc_conn:	TRUE when the local device has signaled that it is connected
 *		to the bus
 * @loc_sof:	TRUE when the local device is generating activity on the bus
 * @adp_prb:	TRUE when the local device is in the process of doing
 *		ADP probing
 *
 *	Outputs for B-device state
 * @adp_sns:	TRUE when the B-device is in the process of carrying out
 *		ADP sensing
 * @data_pulse: TRUE when the B-device is performing data line pulsing
 *
 * Internal Variables
 *
 * a_set_b_hnp_en: TRUE when the A-device has successfully set the
 *		b_hnp_enable bit in the B-device.
 *		   Unused as OTG fsm uses otg->host->b_hnp_enable instead
 * b_srp_done:	TRUE when the B-device has completed initiating SRP
 * b_hnp_enable: TRUE when the B-device has accepted the
 *		SetFeature(b_hnp_enable) B-device.
 *		Unused as OTG fsm uses otg->gadget->b_hnp_enable instead
 * a_clr_err:	Asserted (by application ?) to clear a_vbus_err due to an
 *		overcurrent condition and causes the A-device to transition
 *		to a_wait_vfall
 */
struct otg_fsm {
	/* Input */
	int id;
	int adp_change;
	int power_up;
	int a_srp_det;
	int a_vbus_vld;
	int b_conn;
	int a_bus_resume;
	int a_bus_suspend;
	int a_conn;
	int b_se0_srp;
	int b_ssend_srp;
	int b_sess_vld;
	int test_device;
	int a_bus_drop;
	int a_bus_req;
	int b_bus_req;

	/* Auxiliary inputs */
	int a_sess_vld;
	int b_bus_resume;
	int b_bus_suspend;

	/* Output */
	int drv_vbus;
	int loc_conn;
	int loc_sof;
	int adp_prb;
	int adp_sns;
	int data_pulse;

	/* Internal variables */
	int a_set_b_hnp_en;
	int b_srp_done;
	int b_hnp_enable;
	int a_clr_err;

	/* Informative variables. All unused as of now */
	int a_bus_drop_inf;
	int a_bus_req_inf;
	int a_clr_err_inf;
	int b_bus_req_inf;
	/* Auxiliary informative variables */
	int a_suspend_req_inf;

	/* Timeout indicator for timers */
	int a_wait_vrise_tmout;
	int a_wait_vfall_tmout;
	int a_wait_bcon_tmout;
	int a_aidl_bdis_tmout;
	int b_ase0_brst_tmout;
	int a_bidl_adis_tmout;

	struct otg_fsm_ops *ops;
	struct usb_otg *otg;

	/* Current usb protocol used: 0:undefine; 1:host; 2:client */
	int protocol;
	struct mutex lock;
	u8 *host_req_flag;
	struct delayed_work hnp_polling_work;
	bool state_changed;
};

struct otg_fsm_ops {
	void	(*chrg_vbus)(struct otg_fsm *fsm, int on);
	void	(*drv_vbus)(struct otg_fsm *fsm, int on);
	void	(*loc_conn)(struct otg_fsm *fsm, int on);
	void	(*loc_sof)(struct otg_fsm *fsm, int on);
	void	(*start_pulse)(struct otg_fsm *fsm);
	void	(*start_adp_prb)(struct otg_fsm *fsm);
	void	(*start_adp_sns)(struct otg_fsm *fsm);
	void	(*add_timer)(struct otg_fsm *fsm, enum otg_fsm_timer timer);
	void	(*del_timer)(struct otg_fsm *fsm, enum otg_fsm_timer timer);
	int	(*start_host)(struct otg_fsm *fsm, int on);
	int	(*start_gadget)(struct otg_fsm *fsm, int on);
};


static inline int otg_chrg_vbus(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->chrg_vbus)
		return -EOPNOTSUPP;
	fsm->ops->chrg_vbus(fsm, on);
	return 0;
}

static inline int otg_drv_vbus(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->drv_vbus)
		return -EOPNOTSUPP;
	if (fsm->drv_vbus != on) {
		fsm->drv_vbus = on;
		fsm->ops->drv_vbus(fsm, on);
	}
	return 0;
}

static inline int otg_loc_conn(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->loc_conn)
		return -EOPNOTSUPP;
	if (fsm->loc_conn != on) {
		fsm->loc_conn = on;
		fsm->ops->loc_conn(fsm, on);
	}
	return 0;
}

static inline int otg_loc_sof(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->loc_sof)
		return -EOPNOTSUPP;
	if (fsm->loc_sof != on) {
		fsm->loc_sof = on;
		fsm->ops->loc_sof(fsm, on);
	}
	return 0;
}

static inline int otg_start_pulse(struct otg_fsm *fsm)
{
	if (!fsm->ops->start_pulse)
		return -EOPNOTSUPP;
	if (!fsm->data_pulse) {
		fsm->data_pulse = 1;
		fsm->ops->start_pulse(fsm);
	}
	return 0;
}

static inline int otg_start_adp_prb(struct otg_fsm *fsm)
{
	if (!fsm->ops->start_adp_prb)
		return -EOPNOTSUPP;
	if (!fsm->adp_prb) {
		fsm->adp_sns = 0;
		fsm->adp_prb = 1;
		fsm->ops->start_adp_prb(fsm);
	}
	return 0;
}

static inline int otg_start_adp_sns(struct otg_fsm *fsm)
{
	if (!fsm->ops->start_adp_sns)
		return -EOPNOTSUPP;
	if (!fsm->adp_sns) {
		fsm->adp_sns = 1;
		fsm->ops->start_adp_sns(fsm);
	}
	return 0;
}

static inline int otg_add_timer(struct otg_fsm *fsm, enum otg_fsm_timer timer)
{
	if (!fsm->ops->add_timer)
		return -EOPNOTSUPP;
	fsm->ops->add_timer(fsm, timer);
	return 0;
}

static inline int otg_del_timer(struct otg_fsm *fsm, enum otg_fsm_timer timer)
{
	if (!fsm->ops->del_timer)
		return -EOPNOTSUPP;
	fsm->ops->del_timer(fsm, timer);
	return 0;
}

static inline int otg_start_host(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->start_host)
		return -EOPNOTSUPP;
	return fsm->ops->start_host(fsm, on);
}

static inline int otg_start_gadget(struct otg_fsm *fsm, int on)
{
	if (!fsm->ops->start_gadget)
		return -EOPNOTSUPP;
	return fsm->ops->start_gadget(fsm, on);
}

int otg_statemachine(struct otg_fsm *fsm);

#endif /* __LINUX_USB_OTG_FSM_H */
