/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_TYPES_H
#define _LINUX_NS_COMMON_TYPES_H

#include <linux/atomic.h>
#include <linux/ns/nstree_types.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/types.h>

struct cgroup_namespace;
struct dentry;
struct ipc_namespace;
struct mnt_namespace;
struct net;
struct pid_namespace;
struct proc_ns_operations;
struct time_namespace;
struct user_namespace;
struct uts_namespace;

extern struct cgroup_namespace init_cgroup_ns;
extern struct ipc_namespace init_ipc_ns;
extern struct mnt_namespace init_mnt_ns;
extern struct net init_net;
extern struct pid_namespace init_pid_ns;
extern struct time_namespace init_time_ns;
extern struct user_namespace init_user_ns;
extern struct uts_namespace init_uts_ns;

extern const struct proc_ns_operations cgroupns_operations;
extern const struct proc_ns_operations ipcns_operations;
extern const struct proc_ns_operations mntns_operations;
extern const struct proc_ns_operations netns_operations;
extern const struct proc_ns_operations pidns_operations;
extern const struct proc_ns_operations pidns_for_children_operations;
extern const struct proc_ns_operations timens_operations;
extern const struct proc_ns_operations timens_for_children_operations;
extern const struct proc_ns_operations userns_operations;
extern const struct proc_ns_operations utsns_operations;

/*
 * Namespace lifetimes are managed via a two-tier reference counting model:
 *
 * (1) __ns_ref (refcount_t): Main reference count tracking memory
 *     lifetime. Controls when the namespace structure itself is freed.
 *     It also pins the namespace on the namespace trees whereas (2)
 *     only regulates their visibility to userspace.
 *
 * (2) __ns_ref_active (atomic_t): Reference count tracking active users.
 *     Controls visibility of the namespace in the namespace trees.
 *     Any live task that uses the namespace (via nsproxy or cred) holds
 *     an active reference. Any open file descriptor or bind-mount of
 *     the namespace holds an active reference. Once all tasks have
 *     called exited their namespaces and all file descriptors and
 *     bind-mounts have been released the active reference count drops
 *     to zero and the namespace becomes inactive. IOW, the namespace
 *     cannot be listed or opened via file handles anymore.
 *
 *     Note that it is valid to transition from active to inactive and
 *     back from inactive to active e.g., when resurrecting an inactive
 *     namespace tree via the SIOCGSKNS ioctl().
 *
 * Relationship and lifecycle states:
 *
 * - Active (__ns_ref_active > 0):
 *   Namespace is actively used and visible to userspace. The namespace
 *   can be reopened via /proc/<pid>/ns/<ns_type>, via namespace file
 *   handles, or discovered via listns().
 *
 * - Inactive (__ns_ref_active == 0, __ns_ref > 0):
 *   No tasks are actively using the namespace and it isn't pinned by
 *   any bind-mounts or open file descriptors anymore. But the namespace
 *   is still kept alive by internal references. For example, the user
 *   namespace could be pinned by an open file through file->f_cred
 *   references when one of the now defunct tasks had opened a file and
 *   handed the file descriptor off to another process via a UNIX
 *   sockets. Such references keep the namespace structure alive through
 *   __ns_ref but will not hold an active reference.
 *
 * - Destroyed (__ns_ref == 0):
 *   No references remain. The namespace is removed from the tree and freed.
 *
 * State transitions:
 *
 * Active -> Inactive:
 *   When the last task using the namespace exits it drops its active
 *   references to all namespaces. However, user and pid namespaces
 *   remain accessible until the task has been reaped.
 *
 * Inactive -> Active:
 *   An inactive namespace tree might be resurrected due to e.g., the
 *   SIOCGSKNS ioctl() on a socket.
 *
 * Inactive -> Destroyed:
 *   When __ns_ref drops to zero the namespace is removed from the
 *   namespaces trees and the memory is freed (after RCU grace period).
 *
 * Initial namespaces:
 *   Boot-time namespaces (init_net, init_pid_ns, etc.) start with
 *   __ns_ref_active = 1 and remain active forever.
 *
 * @ns_type: type of namespace (e.g., CLONE_NEWNET)
 * @stashed: cached dentry to be used by the vfs
 * @ops: namespace operations
 * @inum: namespace inode number (quickly recycled for non-initial namespaces)
 * @__ns_ref: main reference count (do not use directly)
 * @ns_tree: namespace tree nodes and active reference count
 */
struct ns_common {
	struct {
		refcount_t __ns_ref; /* do not use directly */
	} ____cacheline_aligned_in_smp;
	u32 ns_type;
	struct dentry *stashed;
	const struct proc_ns_operations *ops;
	unsigned int inum;
	union {
		struct ns_tree;
		struct rcu_head ns_rcu;
	};
};

#define to_ns_common(__ns)                                    \
	_Generic((__ns),                                      \
		struct cgroup_namespace *:       &(__ns)->ns, \
		const struct cgroup_namespace *: &(__ns)->ns, \
		struct ipc_namespace *:          &(__ns)->ns, \
		const struct ipc_namespace *:    &(__ns)->ns, \
		struct mnt_namespace *:          &(__ns)->ns, \
		const struct mnt_namespace *:    &(__ns)->ns, \
		struct net *:                    &(__ns)->ns, \
		const struct net *:              &(__ns)->ns, \
		struct pid_namespace *:          &(__ns)->ns, \
		const struct pid_namespace *:    &(__ns)->ns, \
		struct time_namespace *:         &(__ns)->ns, \
		const struct time_namespace *:   &(__ns)->ns, \
		struct user_namespace *:         &(__ns)->ns, \
		const struct user_namespace *:   &(__ns)->ns, \
		struct uts_namespace *:          &(__ns)->ns, \
		const struct uts_namespace *:    &(__ns)->ns)

#define ns_init_inum(__ns)                                     \
	_Generic((__ns),                                       \
		struct cgroup_namespace *: CGROUP_NS_INIT_INO, \
		struct ipc_namespace *:    IPC_NS_INIT_INO,    \
		struct mnt_namespace *:    MNT_NS_INIT_INO,    \
		struct net *:              NET_NS_INIT_INO,    \
		struct pid_namespace *:    PID_NS_INIT_INO,    \
		struct time_namespace *:   TIME_NS_INIT_INO,   \
		struct user_namespace *:   USER_NS_INIT_INO,   \
		struct uts_namespace *:    UTS_NS_INIT_INO)

#define ns_init_ns(__ns)                                    \
	_Generic((__ns),                                    \
		struct cgroup_namespace *: &init_cgroup_ns, \
		struct ipc_namespace *:    &init_ipc_ns,    \
		struct mnt_namespace *:    &init_mnt_ns,     \
		struct net *:              &init_net,       \
		struct pid_namespace *:    &init_pid_ns,    \
		struct time_namespace *:   &init_time_ns,   \
		struct user_namespace *:   &init_user_ns,   \
		struct uts_namespace *:    &init_uts_ns)

#define ns_init_id(__ns)						\
	_Generic((__ns),						\
		struct cgroup_namespace *:	CGROUP_NS_INIT_ID,	\
		struct ipc_namespace *:		IPC_NS_INIT_ID,		\
		struct mnt_namespace *:		MNT_NS_INIT_ID,		\
		struct net *:			NET_NS_INIT_ID,		\
		struct pid_namespace *:		PID_NS_INIT_ID,		\
		struct time_namespace *:	TIME_NS_INIT_ID,	\
		struct user_namespace *:	USER_NS_INIT_ID,	\
		struct uts_namespace *:		UTS_NS_INIT_ID)

#define to_ns_operations(__ns)                                                                         \
	_Generic((__ns),                                                                               \
		struct cgroup_namespace *: (IS_ENABLED(CONFIG_CGROUPS) ? &cgroupns_operations : NULL), \
		struct ipc_namespace *:    (IS_ENABLED(CONFIG_IPC_NS)  ? &ipcns_operations    : NULL), \
		struct mnt_namespace *:    &mntns_operations,                                          \
		struct net *:              (IS_ENABLED(CONFIG_NET_NS)  ? &netns_operations    : NULL), \
		struct pid_namespace *:    (IS_ENABLED(CONFIG_PID_NS)  ? &pidns_operations    : NULL), \
		struct time_namespace *:   (IS_ENABLED(CONFIG_TIME_NS) ? &timens_operations   : NULL), \
		struct user_namespace *:   (IS_ENABLED(CONFIG_USER_NS) ? &userns_operations   : NULL), \
		struct uts_namespace *:    (IS_ENABLED(CONFIG_UTS_NS)  ? &utsns_operations    : NULL))

#define ns_common_type(__ns)                                \
	_Generic((__ns),                                    \
		struct cgroup_namespace *: CLONE_NEWCGROUP, \
		struct ipc_namespace *:    CLONE_NEWIPC,    \
		struct mnt_namespace *:    CLONE_NEWNS,     \
		struct net *:              CLONE_NEWNET,    \
		struct pid_namespace *:    CLONE_NEWPID,    \
		struct time_namespace *:   CLONE_NEWTIME,   \
		struct user_namespace *:   CLONE_NEWUSER,   \
		struct uts_namespace *:    CLONE_NEWUTS)

#endif /* _LINUX_NS_COMMON_TYPES_H */
