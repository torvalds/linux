/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 */

#ifndef _UAPI_LINUX_ASPEED_OTP_H
#define _UAPI_LINUX_ASPEED_OTP_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct otp_read {
	unsigned int offset;
	unsigned int len;
	unsigned int *data;
};

struct otp_prog {
	unsigned int dw_offset;
	unsigned int bit_offset;
	unsigned int value;
};

#define OTP_A0	0
#define OTP_A1	1
#define OTP_A2	2
#define OTP_A3	3

#define OTPIOC_BASE 'O'

#define ASPEED_OTP_READ_DATA _IOR(OTPIOC_BASE, 0, struct otp_read)
#define ASPEED_OTP_READ_CONF _IOR(OTPIOC_BASE, 1, struct otp_read)
#define ASPEED_OTP_PROG_DATA _IOW(OTPIOC_BASE, 2, struct otp_prog)
#define ASPEED_OTP_PROG_CONF _IOW(OTPIOC_BASE, 3, struct otp_prog)
#define ASPEED_OTP_VER _IOR(OTPIOC_BASE, 4, unsigned int)
#define ASPEED_OTP_SW_RID _IOR(OTPIOC_BASE, 5, u32*)
#define ASPEED_SEC_KEY_NUM _IOR(OTPIOC_BASE, 6, u32*)

#endif /* _UAPI_LINUX_ASPEED_JTAG_H */
