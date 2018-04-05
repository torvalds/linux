/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
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

#ifndef IB_USER_IOCTL_CMDS_H
#define IB_USER_IOCTL_CMDS_H

#define UVERBS_ID_NS_MASK 0xF000
#define UVERBS_ID_NS_SHIFT 12

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
	UVERBS_OBJECT_FLOW_ACTION,
	UVERBS_OBJECT_DM,
};

enum {
	UVERBS_ATTR_UHW_IN = UVERBS_UDATA_DRIVER_DATA_FLAG,
	UVERBS_ATTR_UHW_OUT,
};

enum uverbs_attrs_create_cq_cmd_attr_ids {
	UVERBS_ATTR_CREATE_CQ_HANDLE,
	UVERBS_ATTR_CREATE_CQ_CQE,
	UVERBS_ATTR_CREATE_CQ_USER_HANDLE,
	UVERBS_ATTR_CREATE_CQ_COMP_CHANNEL,
	UVERBS_ATTR_CREATE_CQ_COMP_VECTOR,
	UVERBS_ATTR_CREATE_CQ_FLAGS,
	UVERBS_ATTR_CREATE_CQ_RESP_CQE,
};

enum uverbs_attrs_destroy_cq_cmd_attr_ids {
	UVERBS_ATTR_DESTROY_CQ_HANDLE,
	UVERBS_ATTR_DESTROY_CQ_RESP,
};

enum uverbs_attrs_create_flow_action_esp {
	UVERBS_ATTR_FLOW_ACTION_ESP_HANDLE,
	UVERBS_ATTR_FLOW_ACTION_ESP_ATTRS,
	UVERBS_ATTR_FLOW_ACTION_ESP_ESN,
	UVERBS_ATTR_FLOW_ACTION_ESP_KEYMAT,
	UVERBS_ATTR_FLOW_ACTION_ESP_REPLAY,
	UVERBS_ATTR_FLOW_ACTION_ESP_ENCAP,
};

enum uverbs_attrs_destroy_flow_action_esp {
	UVERBS_ATTR_DESTROY_FLOW_ACTION_HANDLE,
};

enum uverbs_methods_cq {
	UVERBS_METHOD_CQ_CREATE,
	UVERBS_METHOD_CQ_DESTROY,
};

enum uverbs_methods_actions_flow_action_ops {
	UVERBS_METHOD_FLOW_ACTION_ESP_CREATE,
	UVERBS_METHOD_FLOW_ACTION_DESTROY,
	UVERBS_METHOD_FLOW_ACTION_ESP_MODIFY,
};

enum uverbs_attrs_alloc_dm_cmd_attr_ids {
	UVERBS_ATTR_ALLOC_DM_HANDLE,
	UVERBS_ATTR_ALLOC_DM_LENGTH,
	UVERBS_ATTR_ALLOC_DM_ALIGNMENT,
};

enum uverbs_attrs_free_dm_cmd_attr_ids {
	UVERBS_ATTR_FREE_DM_HANDLE,
};

enum uverbs_methods_dm {
	UVERBS_METHOD_DM_ALLOC,
	UVERBS_METHOD_DM_FREE,
};
#endif
