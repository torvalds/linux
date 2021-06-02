/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef _QED_NVMETCP_IF_H
#define _QED_NVMETCP_IF_H
#include <linux/types.h>
#include <linux/qed/qed_if.h>

#define QED_NVMETCP_MAX_IO_SIZE	0x800000

typedef int (*nvmetcp_event_cb_t) (void *context,
				   u8 fw_event_code, void *fw_handle);

struct qed_dev_nvmetcp_info {
	struct qed_dev_info common;
	u8 port_id;  /* Physical port */
	u8 num_cqs;
};

#define MAX_TID_BLOCKS_NVMETCP (512)
struct qed_nvmetcp_tid {
	u32 size;		/* In bytes per task */
	u32 num_tids_per_block;
	u8 *blocks[MAX_TID_BLOCKS_NVMETCP];
};

struct qed_nvmetcp_cb_ops {
	struct qed_common_cb_ops common;
};

/**
 * struct qed_nvmetcp_ops - qed NVMeTCP operations.
 * @common:		common operations pointer
 * @ll2:		light L2 operations pointer
 * @fill_dev_info:	fills NVMeTCP specific information
 *			@param cdev
 *			@param info
 *			@return 0 on success, otherwise error value.
 * @register_ops:	register nvmetcp operations
 *			@param cdev
 *			@param ops - specified using qed_nvmetcp_cb_ops
 *			@param cookie - driver private
 * @start:		nvmetcp in FW
 *			@param cdev
 *			@param tasks - qed will fill information about tasks
 *			return 0 on success, otherwise error value.
 * @stop:		nvmetcp in FW
 *			@param cdev
 *			return 0 on success, otherwise error value.
 */
struct qed_nvmetcp_ops {
	const struct qed_common_ops *common;

	const struct qed_ll2_ops *ll2;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_nvmetcp_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_nvmetcp_cb_ops *ops, void *cookie);

	int (*start)(struct qed_dev *cdev,
		     struct qed_nvmetcp_tid *tasks,
		     void *event_context, nvmetcp_event_cb_t async_event_cb);

	int (*stop)(struct qed_dev *cdev);
};

const struct qed_nvmetcp_ops *qed_get_nvmetcp_ops(void);
void qed_put_nvmetcp_ops(void);
#endif
