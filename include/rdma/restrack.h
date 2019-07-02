/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_RESTRACK_H_
#define _RDMA_RESTRACK_H_

#include <linux/typecheck.h>
#include <linux/sched.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/sched/task.h>
#include <uapi/rdma/rdma_netlink.h>
#include <linux/xarray.h>

/**
 * enum rdma_restrack_type - HW objects to track
 */
enum rdma_restrack_type {
	/**
	 * @RDMA_RESTRACK_PD: Protection domain (PD)
	 */
	RDMA_RESTRACK_PD,
	/**
	 * @RDMA_RESTRACK_CQ: Completion queue (CQ)
	 */
	RDMA_RESTRACK_CQ,
	/**
	 * @RDMA_RESTRACK_QP: Queue pair (QP)
	 */
	RDMA_RESTRACK_QP,
	/**
	 * @RDMA_RESTRACK_CM_ID: Connection Manager ID (CM_ID)
	 */
	RDMA_RESTRACK_CM_ID,
	/**
	 * @RDMA_RESTRACK_MR: Memory Region (MR)
	 */
	RDMA_RESTRACK_MR,
	/**
	 * @RDMA_RESTRACK_CTX: Verbs contexts (CTX)
	 */
	RDMA_RESTRACK_CTX,
	/**
	 * @RDMA_RESTRACK_COUNTER: Statistic Counter
	 */
	RDMA_RESTRACK_COUNTER,
	/**
	 * @RDMA_RESTRACK_MAX: Last entry, used for array dclarations
	 */
	RDMA_RESTRACK_MAX
};

struct ib_device;

/**
 * struct rdma_restrack_entry - metadata per-entry
 */
struct rdma_restrack_entry {
	/**
	 * @valid: validity indicator
	 *
	 * The entries are filled during rdma_restrack_add,
	 * can be attempted to be free during rdma_restrack_del.
	 *
	 * As an example for that, see mlx5 QPs with type MLX5_IB_QPT_HW_GSI
	 */
	bool			valid;
	/*
	 * @kref: Protect destroy of the resource
	 */
	struct kref		kref;
	/*
	 * @comp: Signal that all consumers of resource are completed their work
	 */
	struct completion	comp;
	/**
	 * @task: owner of resource tracking entity
	 *
	 * There are two types of entities: created by user and created
	 * by kernel.
	 *
	 * This is relevant for the entities created by users.
	 * For the entities created by kernel, this pointer will be NULL.
	 */
	struct task_struct	*task;
	/**
	 * @kern_name: name of owner for the kernel created entities.
	 */
	const char		*kern_name;
	/**
	 * @type: various objects in restrack database
	 */
	enum rdma_restrack_type	type;
	/**
	 * @user: user resource
	 */
	bool			user;
	/**
	 * @id: ID to expose to users
	 */
	u32 id;
};

int rdma_restrack_count(struct ib_device *dev,
			enum rdma_restrack_type type,
			struct pid_namespace *ns);

void rdma_restrack_kadd(struct rdma_restrack_entry *res);
void rdma_restrack_uadd(struct rdma_restrack_entry *res);

/**
 * rdma_restrack_del() - delete object from the reource tracking database
 * @res:  resource entry
 * @type: actual type of object to operate
 */
void rdma_restrack_del(struct rdma_restrack_entry *res);

/**
 * rdma_is_kernel_res() - check the owner of resource
 * @res:  resource entry
 */
static inline bool rdma_is_kernel_res(struct rdma_restrack_entry *res)
{
	return !res->user;
}

/**
 * rdma_restrack_get() - grab to protect resource from release
 * @res:  resource entry
 */
int __must_check rdma_restrack_get(struct rdma_restrack_entry *res);

/**
 * rdma_restrack_put() - release resource
 * @res:  resource entry
 */
int rdma_restrack_put(struct rdma_restrack_entry *res);

/**
 * rdma_restrack_set_task() - set the task for this resource
 * @res:  resource entry
 * @caller: kernel name, the current task will be used if the caller is NULL.
 */
void rdma_restrack_set_task(struct rdma_restrack_entry *res,
			    const char *caller);

/*
 * Helper functions for rdma drivers when filling out
 * nldev driver attributes.
 */
int rdma_nl_put_driver_u32(struct sk_buff *msg, const char *name, u32 value);
int rdma_nl_put_driver_u32_hex(struct sk_buff *msg, const char *name,
			       u32 value);
int rdma_nl_put_driver_u64(struct sk_buff *msg, const char *name, u64 value);
int rdma_nl_put_driver_u64_hex(struct sk_buff *msg, const char *name,
			       u64 value);
struct rdma_restrack_entry *rdma_restrack_get_byid(struct ib_device *dev,
						   enum rdma_restrack_type type,
						   u32 id);
#endif /* _RDMA_RESTRACK_H_ */
