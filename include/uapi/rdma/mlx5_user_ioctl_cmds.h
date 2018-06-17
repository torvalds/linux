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

#ifndef MLX5_USER_IOCTL_CMDS_H
#define MLX5_USER_IOCTL_CMDS_H

#include <rdma/ib_user_ioctl_cmds.h>

enum mlx5_ib_create_flow_action_attrs {
	/* This attribute belong to the driver namespace */
	MLX5_IB_ATTR_CREATE_FLOW_ACTION_FLAGS = (1U << UVERBS_ID_NS_SHIFT),
};

enum mlx5_ib_alloc_dm_attrs {
	MLX5_IB_ATTR_ALLOC_DM_RESP_START_OFFSET = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_ATTR_ALLOC_DM_RESP_PAGE_INDEX,
};

enum mlx5_ib_devx_methods {
	MLX5_IB_METHOD_DEVX_OTHER  = (1U << UVERBS_ID_NS_SHIFT),
};

enum  mlx5_ib_devx_other_attrs {
	MLX5_IB_ATTR_DEVX_OTHER_CMD_IN = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_ATTR_DEVX_OTHER_CMD_OUT,
};

enum mlx5_ib_devx_obj_create_attrs {
	MLX5_IB_ATTR_DEVX_OBJ_CREATE_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_IN,
	MLX5_IB_ATTR_DEVX_OBJ_CREATE_CMD_OUT,
};

enum mlx5_ib_devx_obj_destroy_attrs {
	MLX5_IB_ATTR_DEVX_OBJ_DESTROY_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
};

enum mlx5_ib_devx_obj_modify_attrs {
	MLX5_IB_ATTR_DEVX_OBJ_MODIFY_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_IN,
	MLX5_IB_ATTR_DEVX_OBJ_MODIFY_CMD_OUT,
};

enum mlx5_ib_devx_obj_query_attrs {
	MLX5_IB_ATTR_DEVX_OBJ_QUERY_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_IN,
	MLX5_IB_ATTR_DEVX_OBJ_QUERY_CMD_OUT,
};

enum mlx5_ib_devx_obj_methods {
	MLX5_IB_METHOD_DEVX_OBJ_CREATE = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_METHOD_DEVX_OBJ_DESTROY,
	MLX5_IB_METHOD_DEVX_OBJ_MODIFY,
	MLX5_IB_METHOD_DEVX_OBJ_QUERY,
};

enum mlx5_ib_devx_objects {
	MLX5_IB_OBJECT_DEVX = (1U << UVERBS_ID_NS_SHIFT),
	MLX5_IB_OBJECT_DEVX_OBJ,
};

#endif
