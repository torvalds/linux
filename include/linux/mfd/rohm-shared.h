/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2020 ROHM Semiconductors */


#ifndef __LINUX_MFD_ROHM_SHARED_H__
#define __LINUX_MFD_ROHM_SHARED_H__

/* RTC definitions shared between BD70528 and BD71828 */

#define BD70528_MASK_RTC_SEC		0x7f
#define BD70528_MASK_RTC_MINUTE		0x7f
#define BD70528_MASK_RTC_HOUR_24H	0x80
#define BD70528_MASK_RTC_HOUR_PM	0x20
#define BD70528_MASK_RTC_HOUR		0x3f
#define BD70528_MASK_RTC_DAY		0x3f
#define BD70528_MASK_RTC_WEEK		0x07
#define BD70528_MASK_RTC_MONTH		0x1f
#define BD70528_MASK_RTC_YEAR		0xff
#define BD70528_MASK_ALM_EN		0x7

#endif /* __LINUX_MFD_ROHM_SHARED_H__ */
