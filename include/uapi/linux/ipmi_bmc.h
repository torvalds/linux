/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#ifndef _UAPI_LINUX_IPMI_BMC_H
#define _UAPI_LINUX_IPMI_BMC_H

#include <linux/ioctl.h>

#define __IPMI_BMC_IOCTL_MAGIC        0xB1
#define IPMI_BMC_IOCTL_SET_SMS_ATN    _IO(__IPMI_BMC_IOCTL_MAGIC, 0x00)
#define IPMI_BMC_IOCTL_CLEAR_SMS_ATN  _IO(__IPMI_BMC_IOCTL_MAGIC, 0x01)
#define IPMI_BMC_IOCTL_FORCE_ABORT    _IO(__IPMI_BMC_IOCTL_MAGIC, 0x02)

#endif /* _UAPI_LINUX_IPMI_BMC_H */
