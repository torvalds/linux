/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __GH_ERRNO_H
#define __GH_ERRNO_H

#include <linux/errno.h>

#define GH_ERROR_OK			0
#define GH_ERROR_UNIMPLEMENTED		-1
#define GH_ERROR_RETRY			-2

#define GH_ERROR_ARG_INVAL		1
#define GH_ERROR_ARG_SIZE		2
#define GH_ERROR_ARG_ALIGN		3

#define GH_ERROR_NOMEM			10

#define GH_ERROR_ADDR_OVFL		20
#define	GH_ERROR_ADDR_UNFL		21
#define GH_ERROR_ADDR_INVAL		22

#define GH_ERROR_DENIED			30
#define GH_ERROR_BUSY			31
#define GH_ERROR_IDLE			32

#define GH_ERROR_IRQ_BOUND		40
#define GH_ERROR_IRQ_UNBOUND		41

#define GH_ERROR_CSPACE_CAP_NULL	50
#define GH_ERROR_CSPACE_CAP_REVOKED	51
#define GH_ERROR_CSPACE_WRONG_OBJ_TYPE	52
#define GH_ERROR_CSPACE_INSUF_RIGHTS	53
#define GH_ERROR_CSPACE_FULL		54

#define GH_ERROR_MSGQUEUE_EMPTY		60
#define GH_ERROR_MSGQUEUE_FULL		61

static inline int gh_remap_error(int gh_error)
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
		return -EPERM;
	case GH_ERROR_UNIMPLEMENTED:
	case GH_ERROR_ARG_INVAL:
	case GH_ERROR_ARG_SIZE:
	case GH_ERROR_ARG_ALIGN:
	case GH_ERROR_ADDR_OVFL:
	case GH_ERROR_ADDR_UNFL:
	case GH_ERROR_ADDR_INVAL:
	default:
		return -EINVAL;
	}
}

#endif
