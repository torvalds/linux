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

struct ib_device;
struct sk_buff;

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
	 * @RDMA_RESTRACK_SRQ: Shared receive queue (SRQ)
	 */
	RDMA_RESTRACK_SRQ,
	/**
	 * @RDMA_RESTRACK_MAX: Last entry, used for array dclarations
	 */
	RDMA_RESTRACK_MAX
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
	/**
	 * @no_track: don't add this entry to restrack DB
	 *
	 * This field is used to mark an entry that doesn't need to be added to
	 * internal restrack DB and presented later to the users at the nldev
	 * query stage.
	 */
	u8			no_track : 1;
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
			enum rdma_restrack_type type);
/**
 * rdma_is_kernel_res() - check the owner of resource
 * @res:  resource entry
 */
static inline bool rdma_is_kernel_res(const struct rdma_restrack_entry *res)
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
int rdma_nl_put_driver_string(struct sk_buff *msg, const char *name,
			      const char *str);
int rdma_nl_stat_hwcounter_entry(struct sk_buff *msg, const char *name,
				 u64 value);

struct rdma_restrack_entry *rdma_restrack_get_byid(struct ib_device *dev,
						   enum rdma_restrack_type type,
						   u32 id);

/**
 * rdma_restrack_no_track() - don't add resource to the DB
 * @res: resource entry
 *
 * Every user of this API should be cross examined.
 * Probably you don't need to use this function.
 */
static inline void rdma_restrack_no_track(struct rdma_restrack_entry *res)
{
	res->no_track = true;
}
static inline bool rdma_restrack_is_tracked(struct rdma_restrack_entry *res)
{
	return !res->no_track;
}
#endif /* _RDMA_RESTRACK_H_ */
