/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Philippe Peurichard <philippe.peurichard@st.com>,
 * Pascal Paillet <p.paillet@st.com> for STMicroelectronics.
 */

#ifndef __DT_BINDINGS_STPMIC1_H__
#define __DT_BINDINGS_STPMIC1_H__

/* IRQ definitions */
#define IT_PONKEY_F	0
#define IT_PONKEY_R	1
#define IT_WAKEUP_F	2
#define IT_WAKEUP_R	3
#define IT_VBUS_OTG_F	4
#define IT_VBUS_OTG_R	5
#define IT_SWOUT_F	6
#define IT_SWOUT_R	7

#define IT_CURLIM_BUCK1	8
#define IT_CURLIM_BUCK2	9
#define IT_CURLIM_BUCK3	10
#define IT_CURLIM_BUCK4	11
#define IT_OCP_OTG	12
#define IT_OCP_SWOUT	13
#define IT_OCP_BOOST	14
#define IT_OVP_BOOST	15

#define IT_CURLIM_LDO1	16
#define IT_CURLIM_LDO2	17
#define IT_CURLIM_LDO3	18
#define IT_CURLIM_LDO4	19
#define IT_CURLIM_LDO5	20
#define IT_CURLIM_LDO6	21
#define IT_SHORT_SWOTG	22
#define IT_SHORT_SWOUT	23

#define IT_TWARN_F	24
#define IT_TWARN_R	25
#define IT_VINLOW_F	26
#define IT_VINLOW_R	27
#define IT_SWIN_F	30
#define IT_SWIN_R	31

/* BUCK MODES definitions */
#define STPMIC1_BUCK_MODE_NORMAL 0
#define STPMIC1_BUCK_MODE_LP 2

#endif /* __DT_BINDINGS_STPMIC1_H__ */
