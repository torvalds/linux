/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_HISI_QM_H
#define _UAPI_HISI_QM_H

#include <linux/types.h>

/**
 * struct hisi_qp_ctx - User data for hisi qp.
 * @id: qp_index return to user space
 * @qc_type: Accelerator algorithm type
 */
struct hisi_qp_ctx {
	__u16 id;
	__u16 qc_type;
};

#define HISI_QM_API_VER_BASE "hisi_qm_v1"
#define HISI_QM_API_VER2_BASE "hisi_qm_v2"

/* UACCE_CMD_QM_SET_QP_CTX: Set qp algorithm type */
#define UACCE_CMD_QM_SET_QP_CTX	_IOWR('H', 10, struct hisi_qp_ctx)

#endif
