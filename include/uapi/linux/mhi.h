/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_MHI_H
#define _UAPI_MHI_H

#include <linux/types.h>
#include <linux/ioctl.h>

enum peripheral_ep_type {
	DATA_EP_TYPE_RESERVED,
	DATA_EP_TYPE_HSIC,
	DATA_EP_TYPE_HSUSB,
	DATA_EP_TYPE_PCIE,
	DATA_EP_TYPE_EMBEDDED,
	DATA_EP_TYPE_BAM_DMUX,
};

struct peripheral_ep_info {
	enum peripheral_ep_type		ep_type;
	__u32				peripheral_iface_id;
};

struct ipa_ep_pair {
	__u32				cons_pipe_num;
	__u32				prod_pipe_num;
};

struct ep_info {
	struct peripheral_ep_info	ph_ep_info;
	struct ipa_ep_pair		ipa_ep_pair;

};

#define MHI_UCI_IOCTL_MAGIC	'm'

#define MHI_UCI_EP_LOOKUP _IOR(MHI_UCI_IOCTL_MAGIC, 2, struct ep_info)
#define MHI_UCI_DPL_EP_LOOKUP _IOR(MHI_UCI_IOCTL_MAGIC, 3, struct ep_info)
#define MHI_UCI_CV2X_EP_LOOKUP _IOR(MHI_UCI_IOCTL_MAGIC, 4, struct ep_info)

#endif /* _UAPI_MHI_H */
