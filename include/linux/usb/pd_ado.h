// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#ifndef __LINUX_USB_PD_ADO_H
#define __LINUX_USB_PD_ADO_H

/* ADO : Alert Data Object */
#define USB_PD_ADO_TYPE_SHIFT			24
#define USB_PD_ADO_TYPE_MASK			0xff
#define USB_PD_ADO_FIXED_BATT_SHIFT		20
#define USB_PD_ADO_FIXED_BATT_MASK		0xf
#define USB_PD_ADO_HOT_SWAP_BATT_SHIFT		16
#define USB_PD_ADO_HOT_SWAP_BATT_MASK		0xf

#define USB_PD_ADO_TYPE_BATT_STATUS_CHANGE	BIT(1)
#define USB_PD_ADO_TYPE_OCP			BIT(2)
#define USB_PD_ADO_TYPE_OTP			BIT(3)
#define USB_PD_ADO_TYPE_OP_COND_CHANGE		BIT(4)
#define USB_PD_ADO_TYPE_SRC_INPUT_CHANGE	BIT(5)
#define USB_PD_ADO_TYPE_OVP			BIT(6)

static inline unsigned int usb_pd_ado_type(u32 ado)
{
	return (ado >> USB_PD_ADO_TYPE_SHIFT) & USB_PD_ADO_TYPE_MASK;
}

static inline unsigned int usb_pd_ado_fixed_batt(u32 ado)
{
	return (ado >> USB_PD_ADO_FIXED_BATT_SHIFT) &
	       USB_PD_ADO_FIXED_BATT_MASK;
}

static inline unsigned int usb_pd_ado_hot_swap_batt(u32 ado)
{
	return (ado >> USB_PD_ADO_HOT_SWAP_BATT_SHIFT) &
	       USB_PD_ADO_HOT_SWAP_BATT_MASK;
}
#endif /* __LINUX_USB_PD_ADO_H */
