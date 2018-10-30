// SPDX-License-Identifier: GPL-2.0+
/*
 * USB 338x super/high/full speed USB device controller.
 * Unlike many such controllers, this one talks PCI.
 *
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2014 Ricardo Ribalda - Qtechnology/AS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_USB_USB338X_H
#define __LINUX_USB_USB338X_H

#include <linux/usb/net2280.h>

/*
 * Extra defined bits for net2280 registers
 */
#define     SCRATCH			      0x0b

#define     DEFECT7374_FSM_FIELD                28
#define     SUPER_SPEED				 8
#define     DMA_REQUEST_OUTSTANDING              5
#define     DMA_PAUSE_DONE_INTERRUPT            26
#define     SET_ISOCHRONOUS_DELAY               24
#define     SET_SEL                             22
#define     SUPER_SPEED_MODE                     8

/*ep_cfg*/
#define     MAX_BURST_SIZE                      24
#define     EP_FIFO_BYTE_COUNT                  16
#define     IN_ENDPOINT_ENABLE                  14
#define     IN_ENDPOINT_TYPE                    12
#define     OUT_ENDPOINT_ENABLE                 10
#define     OUT_ENDPOINT_TYPE                    8
#define USB3380_EP_CFG_MASK_IN ((0x3 << IN_ENDPOINT_TYPE) | \
				BIT(IN_ENDPOINT_ENABLE))
#define USB3380_EP_CFG_MASK_OUT ((0x3 << OUT_ENDPOINT_TYPE) | \
				BIT(OUT_ENDPOINT_ENABLE))

struct usb338x_usb_ext_regs {
	u32     usbclass;
#define     DEVICE_PROTOCOL                     16
#define     DEVICE_SUB_CLASS                     8
#define     DEVICE_CLASS                         0
	u32     ss_sel;
#define     U2_SYSTEM_EXIT_LATENCY               8
#define     U1_SYSTEM_EXIT_LATENCY               0
	u32     ss_del;
#define     U2_DEVICE_EXIT_LATENCY               8
#define     U1_DEVICE_EXIT_LATENCY               0
	u32     usb2lpm;
#define     USB_L1_LPM_HIRD                      2
#define     USB_L1_LPM_REMOTE_WAKE               1
#define     USB_L1_LPM_SUPPORT                   0
	u32     usb3belt;
#define     BELT_MULTIPLIER                     10
#define     BEST_EFFORT_LATENCY_TOLERANCE        0
	u32     usbctl2;
#define     LTM_ENABLE                           7
#define     U2_ENABLE                            6
#define     U1_ENABLE                            5
#define     FUNCTION_SUSPEND                     4
#define     USB3_CORE_ENABLE                     3
#define     USB2_CORE_ENABLE                     2
#define     SERIAL_NUMBER_STRING_ENABLE          0
	u32     in_timeout;
#define     GPEP3_TIMEOUT                       19
#define     GPEP2_TIMEOUT                       18
#define     GPEP1_TIMEOUT                       17
#define     GPEP0_TIMEOUT                       16
#define     GPEP3_TIMEOUT_VALUE                 13
#define     GPEP3_TIMEOUT_ENABLE                12
#define     GPEP2_TIMEOUT_VALUE                  9
#define     GPEP2_TIMEOUT_ENABLE                 8
#define     GPEP1_TIMEOUT_VALUE                  5
#define     GPEP1_TIMEOUT_ENABLE                 4
#define     GPEP0_TIMEOUT_VALUE                  1
#define     GPEP0_TIMEOUT_ENABLE                 0
	u32     isodelay;
#define     ISOCHRONOUS_DELAY                    0
} __packed;

struct usb338x_fifo_regs {
	/* offset 0x0500, 0x0520, 0x0540, 0x0560, 0x0580 */
	u32     ep_fifo_size_base;
#define     IN_FIFO_BASE_ADDRESS                                22
#define     IN_FIFO_SIZE                                        16
#define     OUT_FIFO_BASE_ADDRESS                               6
#define     OUT_FIFO_SIZE                                       0
	u32     ep_fifo_out_wrptr;
	u32     ep_fifo_out_rdptr;
	u32     ep_fifo_in_wrptr;
	u32     ep_fifo_in_rdptr;
	u32     unused[3];
} __packed;


/* Link layer */
struct usb338x_ll_regs {
	/* offset 0x700 */
	u32   ll_ltssm_ctrl1;
	u32   ll_ltssm_ctrl2;
	u32   ll_ltssm_ctrl3;
	u32   unused[2];
	u32   ll_general_ctrl0;
	u32   ll_general_ctrl1;
#define     PM_U3_AUTO_EXIT                                     29
#define     PM_U2_AUTO_EXIT                                     28
#define     PM_U1_AUTO_EXIT                                     27
#define     PM_FORCE_U2_ENTRY                                   26
#define     PM_FORCE_U1_ENTRY                                   25
#define     PM_LGO_COLLISION_SEND_LAU                           24
#define     PM_DIR_LINK_REJECT                                  23
#define     PM_FORCE_LINK_ACCEPT                                22
#define     PM_DIR_ENTRY_U3                                     20
#define     PM_DIR_ENTRY_U2                                     19
#define     PM_DIR_ENTRY_U1                                     18
#define     PM_U2_ENABLE                                        17
#define     PM_U1_ENABLE                                        16
#define     SKP_THRESHOLD_ADJUST_FMW                            8
#define     RESEND_DPP_ON_LRTY_FMW                              7
#define     DL_BIT_VALUE_FMW                                    6
#define     FORCE_DL_BIT                                        5
	u32   ll_general_ctrl2;
#define     SELECT_INVERT_LANE_POLARITY                         7
#define     FORCE_INVERT_LANE_POLARITY                          6
	u32   ll_general_ctrl3;
	u32   ll_general_ctrl4;
	u32   ll_error_gen;
} __packed;

struct usb338x_ll_lfps_regs {
	/* offset 0x748 */
	u32   ll_lfps_5;
#define     TIMER_LFPS_6US                                      16
	u32   ll_lfps_6;
#define     TIMER_LFPS_80US                                     0
} __packed;

struct usb338x_ll_tsn_regs {
	/* offset 0x77C */
	u32   ll_tsn_counters_2;
#define     HOT_TX_NORESET_TS2                                  24
	u32   ll_tsn_counters_3;
#define     HOT_RX_RESET_TS2                                    0
} __packed;

struct usb338x_ll_chi_regs {
	/* offset 0x79C */
	u32   ll_tsn_chicken_bit;
#define     RECOVERY_IDLE_TO_RECOVER_FMW                        3
} __packed;

/* protocol layer */
struct usb338x_pl_regs {
	/* offset 0x800 */
	u32   pl_reg_1;
	u32   pl_reg_2;
	u32   pl_reg_3;
	u32   pl_reg_4;
	u32   pl_ep_ctrl;
	/* Protocol Layer Endpoint Control*/
#define     PL_EP_CTRL                                  0x810
#define     ENDPOINT_SELECT                             0
	/* [4:0] */
#define     EP_INITIALIZED                              16
#define     SEQUENCE_NUMBER_RESET                       17
#define     CLEAR_ACK_ERROR_CODE                        20
	u32   pl_reg_6;
	u32   pl_reg_7;
	u32   pl_reg_8;
	u32   pl_ep_status_1;
	/* Protocol Layer Endpoint Status 1*/
#define     PL_EP_STATUS_1                              0x820
#define     STATE                                       16
#define     ACK_GOOD_NORMAL                             0x11
#define     ACK_GOOD_MORE_ACKS_TO_COME                  0x16
	u32   pl_ep_status_2;
	u32   pl_ep_status_3;
	/* Protocol Layer Endpoint Status 3*/
#define     PL_EP_STATUS_3                              0x828
#define     SEQUENCE_NUMBER                             0
	u32   pl_ep_status_4;
	/* Protocol Layer Endpoint Status 4*/
#define     PL_EP_STATUS_4                              0x82c
	u32   pl_ep_cfg_4;
	/* Protocol Layer Endpoint Configuration 4*/
#define     PL_EP_CFG_4                                 0x830
#define     NON_CTRL_IN_TOLERATE_BAD_DIR                6
} __packed;

#endif /* __LINUX_USB_USB338X_H */
