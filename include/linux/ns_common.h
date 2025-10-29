/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

#include <linux/refcount.h>
#include <linux/rbtree.h>
#include <linux/vfsdebug.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/nsfs.h>

struct proc_ns_operations;

struct cgroup_namespace;
struct ipc_namespace;
struct mnt_namespace;
struct net;
struct pid_namespace;
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

extern const struct proc_ns_operations netns_operations;
extern const struct proc_ns_operations utsns_operations;
extern const struct proc_ns_operations ipcns_operations;
extern const struct proc_ns_operations pidns_operations;
extern const struct proc_ns_operations pidns_for_children_operations;
extern const struct proc_ns_operations userns_operations;
extern const struct proc_ns_operations mntns_operations;
extern const struct proc_ns_operations cgroupns_operations;
extern const struct proc_ns_operations timens_operations;
extern const struct proc_ns_operations timens_for_children_operations;

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
 */
struct ns_common {
	u32 ns_type;
	struct dentry *stashed;
	const struct proc_ns_operations *ops;
	unsigned int inum;
	refcount_t __ns_ref; /* do not use directly */
	union {
		struct {
			u64 ns_id;
			struct /* global namespace rbtree and list */ {
				struct rb_node ns_unified_tree_node;
				struct list_head ns_unified_list_node;
			};
			struct /* per type rbtree and list */ {
				struct rb_node ns_tree_node;
				struct list_head ns_list_node;
			};
			struct /* namespace ownership rbtree and list */ {
				struct rb_root ns_owner_tree; /* rbtree of namespaces owned by this namespace */
				struct list_head ns_owner; /* list of namespaces owned by this namespace */
				struct rb_node ns_owner_tree_node; /* node in the owner namespace's rbtree */
				struct list_head ns_owner_entry; /* node in the owner namespace's ns_owned list */
			};
			atomic_t __ns_ref_active; /* do not use directly */
		};
		struct rcu_head ns_rcu;
	};
};

bool is_current_namespace(struct ns_common *ns);
int __ns_common_init(struct ns_common *ns, u32 ns_type, const struct proc_ns_operations *ops, int inum);
void __ns_common_free(struct ns_common *ns);
struct ns_common *__must_check ns_owner(struct ns_common *ns);

static __always_inline bool is_initial_namespace(struct ns_common *ns)
{
	VFS_WARN_ON_ONCE(ns->inum == 0);
	return unlikely(in_range(ns->inum, MNT_NS_INIT_INO,
				 IPC_NS_INIT_INO - MNT_NS_INIT_INO + 1));
}

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

#define NS_COMMON_INIT(nsname, refs)							\
{											\
	.ns_type		= ns_common_type(&nsname),				\
	.ns_id			= ns_init_id(&nsname),					\
	.inum			= ns_init_inum(&nsname),				\
	.ops			= to_ns_operations(&nsname),				\
	.stashed		= NULL,							\
	.__ns_ref		= REFCOUNT_INIT(refs),					\
	.__ns_ref_active	= ATOMIC_INIT(1),					\
	.ns_list_node		= LIST_HEAD_INIT(nsname.ns.ns_list_node),		\
	.ns_owner_entry		= LIST_HEAD_INIT(nsname.ns.ns_owner_entry),		\
	.ns_owner		= LIST_HEAD_INIT(nsname.ns.ns_owner),			\
	.ns_unified_list_node	= LIST_HEAD_INIT(nsname.ns.ns_unified_list_node),	\
}

#define ns_common_init(__ns)                     \
	__ns_common_init(to_ns_common(__ns),     \
			 ns_common_type(__ns),   \
			 to_ns_operations(__ns), \
			 (((__ns) == ns_init_ns(__ns)) ? ns_init_inum(__ns) : 0))

#define ns_common_init_inum(__ns, __inum)        \
	__ns_common_init(to_ns_common(__ns),     \
			 ns_common_type(__ns),   \
			 to_ns_operations(__ns), \
			 __inum)

#define ns_common_free(__ns) __ns_common_free(to_ns_common((__ns)))

static __always_inline __must_check int __ns_ref_active_read(const struct ns_common *ns)
{
	return atomic_read(&ns->__ns_ref_active);
}

static __always_inline __must_check bool __ns_ref_put(struct ns_common *ns)
{
	if (refcount_dec_and_test(&ns->__ns_ref)) {
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns));
		return true;
	}
	return false;
}

static __always_inline __must_check bool __ns_ref_get(struct ns_common *ns)
{
	if (refcount_inc_not_zero(&ns->__ns_ref))
		return true;
	VFS_WARN_ON_ONCE(__ns_ref_active_read(ns));
	return false;
}

static __always_inline __must_check int __ns_ref_read(const struct ns_common *ns)
{
	return refcount_read(&ns->__ns_ref);
}

#define ns_ref_read(__ns) __ns_ref_read(to_ns_common((__ns)))
#define ns_ref_inc(__ns) refcount_inc(&to_ns_common((__ns))->__ns_ref)
#define ns_ref_get(__ns) __ns_ref_get(to_ns_common((__ns)))
#define ns_ref_put(__ns) __ns_ref_put(to_ns_common((__ns)))
#define ns_ref_put_and_lock(__ns, __lock) \
	refcount_dec_and_lock(&to_ns_common((__ns))->__ns_ref, (__lock))

#define ns_ref_active_read(__ns) \
	((__ns) ? __ns_ref_active_read(to_ns_common(__ns)) : 0)

void __ns_ref_active_get_owner(struct ns_common *ns);

static __always_inline void __ns_ref_active_get(struct ns_common *ns)
{
	WARN_ON_ONCE(atomic_add_negative(1, &ns->__ns_ref_active));
	VFS_WARN_ON_ONCE(is_initial_namespace(ns) && __ns_ref_active_read(ns) <= 0);
}
#define ns_ref_active_get(__ns) \
	do { if (__ns) __ns_ref_active_get(to_ns_common(__ns)); } while (0)

static __always_inline bool __ns_ref_active_get_not_zero(struct ns_common *ns)
{
	if (atomic_inc_not_zero(&ns->__ns_ref_active)) {
		VFS_WARN_ON_ONCE(!__ns_ref_read(ns));
		return true;
	}
	return false;
}

#define ns_ref_active_get_owner(__ns) \
	do { if (__ns) __ns_ref_active_get_owner(to_ns_common(__ns)); } while (0)

void __ns_ref_active_put_owner(struct ns_common *ns);

static __always_inline void __ns_ref_active_put(struct ns_common *ns)
{
	if (atomic_dec_and_test(&ns->__ns_ref_active)) {
		VFS_WARN_ON_ONCE(is_initial_namespace(ns));
		VFS_WARN_ON_ONCE(!__ns_ref_read(ns));
		__ns_ref_active_put_owner(ns);
	}
}
#define ns_ref_active_put(__ns) \
	do { if (__ns) __ns_ref_active_put(to_ns_common(__ns)); } while (0)

static __always_inline struct ns_common *__must_check ns_get_unless_inactive(struct ns_common *ns)
{
	VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) && !__ns_ref_read(ns));
	if (!__ns_ref_active_read(ns))
		return NULL;
	if (!__ns_ref_get(ns))
		return NULL;
	return ns;
}

void __ns_ref_active_resurrect(struct ns_common *ns);

#define ns_ref_active_resurrect(__ns) \
	do { if (__ns) __ns_ref_active_resurrect(to_ns_common(__ns)); } while (0)

#endif
