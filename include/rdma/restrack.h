/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_RESTRACK_H_
#define _RDMA_RESTRACK_H_

#include <linux/typecheck.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/sched/task.h>

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
	 * @RDMA_RESTRACK_MAX: Last entry, used for array dclarations
	 */
	RDMA_RESTRACK_MAX
};

#define RDMA_RESTRACK_HASH_BITS	8
/**
 * struct rdma_restrack_root - main resource tracking management
 * entity, per-device
 */
struct rdma_restrack_root {
	/*
	 * @rwsem: Read/write lock to protect lists
	 */
	struct rw_semaphore	rwsem;
	/**
	 * @hash: global database for all resources per-device
	 */
	DECLARE_HASHTABLE(hash, RDMA_RESTRACK_HASH_BITS);
};

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
	 * @node: hash table entry
	 */
	struct hlist_node	node;
	/**
	 * @type: various objects in restrack database
	 */
	enum rdma_restrack_type	type;
};

/**
 * rdma_restrack_init() - initialize resource tracking
 * @res:  resource tracking root
 */
void rdma_restrack_init(struct rdma_restrack_root *res);

/**
 * rdma_restrack_clean() - clean resource tracking
 * @res:  resource tracking root
 */
void rdma_restrack_clean(struct rdma_restrack_root *res);

/**
 * rdma_restrack_count() - the current usage of specific object
 * @res:  resource entry
 * @type: actual type of object to operate
 * @ns:   PID namespace
 */
int rdma_restrack_count(struct rdma_restrack_root *res,
			enum rdma_restrack_type type,
			struct pid_namespace *ns);

/**
 * rdma_restrack_add() - add object to the reource tracking database
 * @res:  resource entry
 */
void rdma_restrack_add(struct rdma_restrack_entry *res);

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
	return !res->task;
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
 * @task: task struct
 */
static inline void rdma_restrack_set_task(struct rdma_restrack_entry *res,
					  struct task_struct *task)
{
	if (res->task)
		put_task_struct(res->task);
	get_task_struct(task);
	res->task = task;
}

#endif /* _RDMA_RESTRACK_H_ */
