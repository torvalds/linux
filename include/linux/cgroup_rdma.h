/*
 * Copyright (C) 2016 Parav Pandit <pandit.parav@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of the GNU
 * General Public License. See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */

#ifndef _CGROUP_RDMA_H
#define _CGROUP_RDMA_H

#include <linux/cgroup.h>

enum rdmacg_resource_type {
	RDMACG_RESOURCE_HCA_HANDLE,
	RDMACG_RESOURCE_HCA_OBJECT,
	RDMACG_RESOURCE_MAX,
};

#ifdef CONFIG_CGROUP_RDMA

struct rdma_cgroup {
	struct cgroup_subsys_state	css;

	/*
	 * head to keep track of all resource pools
	 * that belongs to this cgroup.
	 */
	struct list_head		rpools;
};

struct rdmacg_device {
	struct list_head	dev_node;
	struct list_head	rpools;
	char			*name;
};

/*
 * APIs for RDMA/IB stack to publish when a device wants to
 * participate in resource accounting
 */
int rdmacg_register_device(struct rdmacg_device *device);
void rdmacg_unregister_device(struct rdmacg_device *device);

/* APIs for RDMA/IB stack to charge/uncharge pool specific resources */
int rdmacg_try_charge(struct rdma_cgroup **rdmacg,
		      struct rdmacg_device *device,
		      enum rdmacg_resource_type index);
void rdmacg_uncharge(struct rdma_cgroup *cg,
		     struct rdmacg_device *device,
		     enum rdmacg_resource_type index);
#endif	/* CONFIG_CGROUP_RDMA */
#endif	/* _CGROUP_RDMA_H */
