/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024 Intel Corporation. */
#ifndef __CXL_MBOX_H__
#define __CXL_MBOX_H__
#include <linux/rcuwait.h>

struct cxl_mbox_cmd;

/**
 * struct cxl_mailbox - context for CXL mailbox operations
 * @host: device that hosts the mailbox
 * @payload_size: Size of space for payload
 *                (CXL 3.1 8.2.8.4.3 Mailbox Capabilities Register)
 * @mbox_mutex: mutex protects device mailbox and firmware
 * @mbox_wait: rcuwait for mailbox
 * @mbox_send: @dev specific transport for transmitting mailbox commands
 */
struct cxl_mailbox {
	struct device *host;
	size_t payload_size;
	struct mutex mbox_mutex; /* lock to protect mailbox context */
	struct rcuwait mbox_wait;
	int (*mbox_send)(struct cxl_mailbox *cxl_mbox, struct cxl_mbox_cmd *cmd);
};

int cxl_mailbox_init(struct cxl_mailbox *cxl_mbox, struct device *host);

#endif
