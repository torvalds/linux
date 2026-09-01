/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NET_SHAPER_H_
#define _NET_SHAPER_H_

#include <linux/types.h>

#include <uapi/linux/net_shaper.h>

struct net_device;
struct devlink;
struct netlink_ext_ack;

enum net_shaper_binding_type {
	NET_SHAPER_BINDING_TYPE_NETDEV,
	/* NET_SHAPER_BINDING_TYPE_DEVLINK_PORT */
};

struct net_shaper_binding {
	enum net_shaper_binding_type type;
	union {
		struct net_device *netdev;
		struct devlink *devlink;
	};
};

struct net_shaper_handle {
	enum net_shaper_scope scope;
	u32 id;
};

/**
 * struct net_shaper - represents a shaping node on the NIC H/W
 * zeroed field are considered not set.
 * @parent: Unique identifier for the shaper parent, usually implied
 * @handle: Unique identifier for this shaper
 * @metric: Specify if the rate limits refers to PPS or BPS
 * @bw_min: Minimum guaranteed rate for this shaper
 * @bw_max: Maximum peak rate allowed for this shaper
 * @burst: Maximum burst for the peek rate of this shaper
 * @priority: Scheduling priority for this shaper
 * @weight: Scheduling weight for this shaper
 */
struct net_shaper {
	struct net_shaper_handle parent;
	struct net_shaper_handle handle;
	enum net_shaper_metric metric;
	u64 bw_min;
	u64 bw_max;
	u64 burst;
	u32 priority;
	u32 weight;

	/* private: */
	u32 leaves; /* accounted only for NODE scope */
	bool valid;
	struct rcu_head rcu;
};

/**
 * struct net_shaper_ops - Operations on device H/W shapers
 *
 * The operations applies to either net_device and devlink objects.
 * The initial shaping configuration at device initialization is empty:
 * does not constraint the rate in any way.
 * The network core keeps track of the applied user-configuration in
 * the net_device or devlink structure.
 * The operations are serialized via a per device lock.
 *
 * Device not supporting any kind of nesting should not provide the
 * @group operation.
 *
 * Each shaper is uniquely identified within the device with a 'handle'
 * comprising the shaper scope and a scope-specific id.
 *
 * Driver ops vs uAPI
 * ------------------
 * Members of the driver ops mirror the Netlink uAPI but driver calls do not
 * map 1:1 to user calls. Drivers need to be careful when assuming that calls
 * disallowed at the uAPI level will never be made at the driver level.
 * The shaper core performs automatic reparenting and cleanup, generating
 * additional calls. Notably:
 *
 * - @group calls in the driver facing API may have nodes as leaves (user is
 *   only allowed to construct groups with queues as leaves)
 * - @group calls may update leaf's parent if the parent is about
 *   to be removed (re-parenting nodes explicitly is not supported in the uAPI)
 *
 * Implicit creation
 * -----------------
 * Shapers are created implicitly, meaning that @set and @group operations
 * are called both for existing and new shapers. The driver has to infer
 * whether the operation is an update or a creation by tracking the handles.
 * Removal of shapers is explicit and done with a @delete call.
 *
 * The @set operation implicitly creates NET_SHAPER_SCOPE_NETDEV and
 * NET_SHAPER_SCOPE_QUEUE shapers.
 * The @group operation implicitly creates NET_SHAPER_SCOPE_NETDEV and
 * NET_SHAPER_SCOPE_NODE shapers (the group shaper itself), as well as
 * NET_SHAPER_SCOPE_QUEUE shapers (leaves).
 */
struct net_shaper_ops {
	/**
	 * @group: create a scheduling group or add leaves
	 *
	 * Nest the @leaves shapers identified under the @node shaper.
	 * All the shapers belong to the device specified by @binding.
	 * The @leaves array's size is specified by @leaves_count.
	 *
	 * @node and @leaves may or may not already exist
	 * (see the "Implicit creation" note). If @node already exists,
	 * the @leaves should be *added* to its children. In this case,
	 * the @leaves array only holds new/modified leaves, not the full list.
	 *
	 * Re-parenting @leaves is implemented by a @group call on a new parent.
	 * There's no explicit call to remove the children from the old parent.
	 */
	int (*group)(struct net_shaper_binding *binding, int leaves_count,
		     const struct net_shaper *leaves,
		     const struct net_shaper *node,
		     struct netlink_ext_ack *extack);

	/**
	 * @set: Updates the specified shaper
	 *
	 * Updates or creates the @shaper on the device specified by @binding.
	 */
	int (*set)(struct net_shaper_binding *binding,
		   const struct net_shaper *shaper,
		   struct netlink_ext_ack *extack);

	/**
	 * @delete: Removes the specified shaper
	 *
	 * Removes the shaper configuration as identified by the given @handle
	 * on the device specified by @binding, restoring the default behavior.
	 *
	 * Note that a @delete call on a NET_SHAPER_SCOPE_QUEUE shaper also
	 * implicitly removes the associated queue from the scheduling
	 * hierarchy. The driver must take care of that step.
	 * @delete calls on NET_SHAPER_SCOPE_NODE should not require any
	 * implicit re-parenting in the driver as core will re-parent the leaves
	 * first, before deleting the SCOPE_NODE shaper.
	 */
	int (*delete)(struct net_shaper_binding *binding,
		      const struct net_shaper_handle *handle,
		      struct netlink_ext_ack *extack);

	/**
	 * @capabilities: get the shaper features supported by the device
	 *
	 * Fills the bitmask @cap with the supported capabilities for the
	 * specified @scope and device specified by @binding.
	 */
	void (*capabilities)(struct net_shaper_binding *binding,
			     enum net_shaper_scope scope, unsigned long *cap);
};

#endif
