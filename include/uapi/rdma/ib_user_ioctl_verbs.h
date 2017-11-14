/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_USER_IOCTL_VERBS_H
#define IB_USER_IOCTL_VERBS_H

#include <rdma/rdma_user_ioctl.h>

#define UVERBS_UDATA_DRIVER_DATA_NS	1
#define UVERBS_UDATA_DRIVER_DATA_FLAG	(1UL << UVERBS_ID_NS_SHIFT)

enum uverbs_default_objects {
	UVERBS_OBJECT_DEVICE, /* No instances of DEVICE are allowed */
	UVERBS_OBJECT_PD,
	UVERBS_OBJECT_COMP_CHANNEL,
	UVERBS_OBJECT_CQ,
	UVERBS_OBJECT_QP,
	UVERBS_OBJECT_SRQ,
	UVERBS_OBJECT_AH,
	UVERBS_OBJECT_MR,
	UVERBS_OBJECT_MW,
	UVERBS_OBJECT_FLOW,
	UVERBS_OBJECT_XRCD,
	UVERBS_OBJECT_RWQ_IND_TBL,
	UVERBS_OBJECT_WQ,
	UVERBS_OBJECT_LAST,
};

enum {
	UVERBS_UHW_IN = UVERBS_UDATA_DRIVER_DATA_FLAG,
	UVERBS_UHW_OUT,
};

enum uverbs_create_cq_cmd_attr_ids {
	CREATE_CQ_HANDLE,
	CREATE_CQ_CQE,
	CREATE_CQ_USER_HANDLE,
	CREATE_CQ_COMP_CHANNEL,
	CREATE_CQ_COMP_VECTOR,
	CREATE_CQ_FLAGS,
	CREATE_CQ_RESP_CQE,
};

enum uverbs_destroy_cq_cmd_attr_ids {
	DESTROY_CQ_HANDLE,
	DESTROY_CQ_RESP,
};

enum uverbs_actions_cq_ops {
	UVERBS_CQ_CREATE,
	UVERBS_CQ_DESTROY,
};

#endif

