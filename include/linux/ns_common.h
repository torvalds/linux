/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

#include <linux/refcount.h>
#include <linux/rbtree.h>
#include <uapi/linux/sched.h>

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

struct ns_common {
	u32 ns_type;
	struct dentry *stashed;
	const struct proc_ns_operations *ops;
	unsigned int inum;
	refcount_t __ns_ref; /* do not use directly */
	union {
		struct {
			u64 ns_id;
			struct rb_node ns_tree_node;
			struct list_head ns_list_node;
		};
		struct rcu_head ns_rcu;
	};
};

int __ns_common_init(struct ns_common *ns, u32 ns_type, const struct proc_ns_operations *ops, int inum);
void __ns_common_free(struct ns_common *ns);

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

static __always_inline __must_check bool __ns_ref_put(struct ns_common *ns)
{
	return refcount_dec_and_test(&ns->__ns_ref);
}

static __always_inline __must_check bool __ns_ref_get(struct ns_common *ns)
{
	return refcount_inc_not_zero(&ns->__ns_ref);
}

#define ns_ref_read(__ns) refcount_read(&to_ns_common((__ns))->__ns_ref)
#define ns_ref_inc(__ns) refcount_inc(&to_ns_common((__ns))->__ns_ref)
#define ns_ref_get(__ns) __ns_ref_get(to_ns_common((__ns)))
#define ns_ref_put(__ns) __ns_ref_put(to_ns_common((__ns)))
#define ns_ref_put_and_lock(__ns, __lock) \
	refcount_dec_and_lock(&to_ns_common((__ns))->__ns_ref, (__lock))

#endif
