/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __UAPI_LINUX_USB_CTRL_QTI_H
#define __UAPI_LINUX_USB_CTRL_QTI_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define MAX_QTI_PKT_SIZE 2048

#define QTI_CTRL_IOCTL_MAGIC	'r'
#define QTI_CTRL_GET_LINE_STATE	_IOR(QTI_CTRL_IOCTL_MAGIC, 2, int)
#define QTI_CTRL_EP_LOOKUP _IOR(QTI_CTRL_IOCTL_MAGIC, 3, struct ep_info)
#define QTI_CTRL_MODEM_OFFLINE _IO(QTI_CTRL_IOCTL_MAGIC, 4)
#define QTI_CTRL_MODEM_ONLINE _IO(QTI_CTRL_IOCTL_MAGIC, 5)
#define QTI_CTRL_DATA_BUF_INFO \
	_IOR(QTI_CTRL_IOCTL_MAGIC, 6, struct data_buf_info)

#define GSI_MBIM_IOCTL_MAGIC 'o'
#define GSI_MBIM_GET_NTB_SIZE  _IOR(GSI_MBIM_IOCTL_MAGIC, 2, __u32)
#define GSI_MBIM_GET_DATAGRAM_COUNT  _IOR(GSI_MBIM_IOCTL_MAGIC, 3, __u16)
#define GSI_MBIM_EP_LOOKUP _IOR(GSI_MBIM_IOCTL_MAGIC, 4, struct ep_info)
#define GSI_MBIM_GPS_USB_STATUS _IOR(GSI_MBIM_IOCTL_MAGIC, 5, int)

enum peripheral_ep_type {
	DATA_EP_TYPE_RESERVED	= 0x0,
	DATA_EP_TYPE_HSIC	= 0x1,
	DATA_EP_TYPE_HSUSB	= 0x2,
	DATA_EP_TYPE_PCIE	= 0x3,
	DATA_EP_TYPE_EMBEDDED	= 0x4,
	DATA_EP_TYPE_BAM_DMUX	= 0x5,
};

struct peripheral_ep_info {
	enum peripheral_ep_type		ep_type;
	__u32				peripheral_iface_id;
};

struct ipa_ep_pair {
	__u32 cons_pipe_num;
	__u32 prod_pipe_num;
};

struct ep_info {
	struct peripheral_ep_info	ph_ep_info;
	struct ipa_ep_pair		ipa_ep_pair;

};

struct data_buf_info {
	__u32 epout_buf_len;
	__u32 epout_total_buf_len;
	__u32 epin_buf_len;
	__u32 epin_total_buf_len;
};

#endif
