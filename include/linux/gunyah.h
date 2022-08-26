/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_GUNYAH_H
#define _LINUX_GUNYAH_H

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/types.h>

/******************************************************************************/
/* Common arch-independent definitions for Gunyah hypercalls                  */
#define GH_CAPID_INVAL	U64_MAX
#define GH_VMID_ROOT_VM	0xff

enum gh_error {
	GH_ERROR_OK			= 0,
	GH_ERROR_UNIMPLEMENTED		= -1,
	GH_ERROR_RETRY			= -2,

	GH_ERROR_ARG_INVAL		= 1,
	GH_ERROR_ARG_SIZE		= 2,
	GH_ERROR_ARG_ALIGN		= 3,

	GH_ERROR_NOMEM			= 10,

	GH_ERROR_ADDR_OVFL		= 20,
	GH_ERROR_ADDR_UNFL		= 21,
	GH_ERROR_ADDR_INVAL		= 22,

	GH_ERROR_DENIED			= 30,
	GH_ERROR_BUSY			= 31,
	GH_ERROR_IDLE			= 32,

	GH_ERROR_IRQ_BOUND		= 40,
	GH_ERROR_IRQ_UNBOUND		= 41,

	GH_ERROR_CSPACE_CAP_NULL	= 50,
	GH_ERROR_CSPACE_CAP_REVOKED	= 51,
	GH_ERROR_CSPACE_WRONG_OBJ_TYPE	= 52,
	GH_ERROR_CSPACE_INSUF_RIGHTS	= 53,
	GH_ERROR_CSPACE_FULL		= 54,

	GH_ERROR_MSGQUEUE_EMPTY		= 60,
	GH_ERROR_MSGQUEUE_FULL		= 61,
};

/**
 * gh_remap_error() - Remap Gunyah hypervisor errors into a Linux error code
 * @gh_error: Gunyah hypercall return value
 */
static inline int gh_remap_error(enum gh_error gh_error)
{
	switch (gh_error) {
	case GH_ERROR_OK:
		return 0;
	case GH_ERROR_NOMEM:
		return -ENOMEM;
	case GH_ERROR_DENIED:
	case GH_ERROR_CSPACE_CAP_NULL:
	case GH_ERROR_CSPACE_CAP_REVOKED:
	case GH_ERROR_CSPACE_WRONG_OBJ_TYPE:
	case GH_ERROR_CSPACE_INSUF_RIGHTS:
	case GH_ERROR_CSPACE_FULL:
		return -EACCES;
	case GH_ERROR_BUSY:
	case GH_ERROR_IDLE:
		return -EBUSY;
	case GH_ERROR_IRQ_BOUND:
	case GH_ERROR_IRQ_UNBOUND:
	case GH_ERROR_MSGQUEUE_FULL:
	case GH_ERROR_MSGQUEUE_EMPTY:
		return -EIO;
	case GH_ERROR_UNIMPLEMENTED:
	case GH_ERROR_RETRY:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

enum gh_api_feature {
	GH_FEATURE_DOORBELL = 1,
	GH_FEATURE_MSGQUEUE = 2,
	GH_FEATURE_VCPU = 5,
	GH_FEATURE_MEMEXTENT = 6,
};

bool arch_is_gh_guest(void);

u16 gh_api_version(void);
bool gh_api_has_feature(enum gh_api_feature feature);

#define GH_API_V1			1

#define GH_API_INFO_API_VERSION_MASK	GENMASK_ULL(13, 0)
#define GH_API_INFO_BIG_ENDIAN		BIT_ULL(14)
#define GH_API_INFO_IS_64BIT		BIT_ULL(15)
#define GH_API_INFO_VARIANT_MASK	GENMASK_ULL(63, 56)

struct gh_hypercall_hyp_identify_resp {
	u64 api_info;
	u64 flags[3];
};

void gh_hypercall_hyp_identify(struct gh_hypercall_hyp_identify_resp *hyp_identity);

#define GH_HYPERCALL_MSGQ_TX_FLAGS_PUSH		BIT(0)

enum gh_error gh_hypercall_msgq_send(u64 capid, size_t size, void *buff, int tx_flags, bool *ready);
enum gh_error gh_hypercall_msgq_recv(u64 capid, void *buff, size_t size, size_t *recv_size,
					bool *ready);

#endif
