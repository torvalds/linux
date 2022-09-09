/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __GH_MSGQ_H
#define __GH_MSGQ_H

#include <linux/types.h>
#include <linux/platform_device.h>

#include "gh_common.h"

enum gh_msgq_label {
	GH_MSGQ_LABEL_RM,
	GH_MSGQ_LABEL_MEMBUF,
	GH_MSGQ_LABEL_DISPLAY,
	GH_MSGQ_LABEL_VSOCK,
	GH_MSGQ_LABEL_TEST_TUIVM,
	GH_MSGQ_LABEL_TEST_OEMVM,
	GH_MSGQ_LABEL_MMRM,
	GH_MSGQ_LABEL_EVA,
	GH_MSGQ_VCPU_SCHED_TEST,
	GH_MSGQ_VCPU_SCHED_TEST_OEMVM,
	GH_MSGQ_LABEL_MAX
};

#define GH_MSGQ_MAX_MSG_SIZE_BYTES 240

#define GH_MSGQ_DIRECTION_TX	0
#define GH_MSGQ_DIRECTION_RX	1

/* Possible flags to pass for Tx or Rx */
#define GH_MSGQ_TX_PUSH		BIT(0)
#define GH_MSGQ_NONBLOCK	BIT(32)

#if IS_ENABLED(CONFIG_GH_MSGQ)
void *gh_msgq_register(int label);
int gh_msgq_unregister(void *msgq_client_desc);
int gh_msgq_send(void *msgq_client_desc,
			void *buff, size_t size, unsigned long flags);
int gh_msgq_recv(void *msgq_client_desc,
			void *buff, size_t buff_size,
			size_t *recv_size, unsigned long flags);

int gh_msgq_populate_cap_info(int label, u64 cap_id,
				int direction, int irq);
int gh_msgq_probe(struct platform_device *pdev, int label);
int gh_msgq_reset_cap_info(enum gh_msgq_label label, int direction, int *irq);
#else
static inline void *gh_msgq_register(int label)
{
	return ERR_PTR(-ENODEV);
}

static inline int gh_msgq_unregister(void *msgq_client_desc)
{
	return -EINVAL;
}

static inline int gh_msgq_send(void *msgq_client_desc,
			void *buff, size_t size, unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_msgq_recv(void *msgq_client_desc,
			void *buff, size_t buff_size,
			size_t *recv_size, unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_msgq_populate_cap_info(int label, u64 cap_id,
					    int direction, int irq)
{
	return -EINVAL;
}

static inline
int gh_msgq_reset_cap_info(enum gh_msgq_label label, int direction, int *irq)
{
	return -EINVAL;
}

static inline int gh_msgq_probe(struct platform_device *pdev, int label)
{
	return -ENODEV;
}
#endif
#endif
