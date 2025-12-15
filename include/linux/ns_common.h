/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

#include <linux/ns/ns_common_types.h>
#include <linux/refcount.h>
#include <linux/vfsdebug.h>
#include <uapi/linux/sched.h>
#include <uapi/linux/nsfs.h>

bool is_current_namespace(struct ns_common *ns);
int __ns_common_init(struct ns_common *ns, u32 ns_type, const struct proc_ns_operations *ops, int inum);
void __ns_common_free(struct ns_common *ns);
struct ns_common *__must_check ns_owner(struct ns_common *ns);

static __always_inline bool is_ns_init_inum(const struct ns_common *ns)
{
	VFS_WARN_ON_ONCE(ns->inum == 0);
	return unlikely(in_range(ns->inum, MNT_NS_INIT_INO,
				 IPC_NS_INIT_INO - MNT_NS_INIT_INO + 1));
}

static __always_inline bool is_ns_init_id(const struct ns_common *ns)
{
	VFS_WARN_ON_ONCE(ns->ns_id == 0);
	return ns->ns_id <= NS_LAST_INIT_ID;
}

#define NS_COMMON_INIT(nsname)										\
{													\
	.ns_type			= ns_common_type(&nsname),					\
	.ns_id				= ns_init_id(&nsname),						\
	.inum				= ns_init_inum(&nsname),					\
	.ops				= to_ns_operations(&nsname),					\
	.stashed			= NULL,								\
	.__ns_ref			= REFCOUNT_INIT(1),						\
	.__ns_ref_active		= ATOMIC_INIT(1),						\
	.ns_unified_node.ns_list_entry	= LIST_HEAD_INIT(nsname.ns.ns_unified_node.ns_list_entry),	\
	.ns_tree_node.ns_list_entry	= LIST_HEAD_INIT(nsname.ns.ns_tree_node.ns_list_entry),		\
	.ns_owner_node.ns_list_entry	= LIST_HEAD_INIT(nsname.ns.ns_owner_node.ns_list_entry),	\
	.ns_owner_root.ns_list_head	= LIST_HEAD_INIT(nsname.ns.ns_owner_root.ns_list_head),		\
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

static __always_inline __must_check int __ns_ref_read(const struct ns_common *ns)
{
	return refcount_read(&ns->__ns_ref);
}

static __always_inline __must_check bool __ns_ref_put(struct ns_common *ns)
{
	if (is_ns_init_id(ns)) {
		VFS_WARN_ON_ONCE(__ns_ref_read(ns) != 1);
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) != 1);
		return false;
	}
	if (refcount_dec_and_test(&ns->__ns_ref)) {
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns));
		return true;
	}
	return false;
}

static __always_inline __must_check bool __ns_ref_get(struct ns_common *ns)
{
	if (is_ns_init_id(ns)) {
		VFS_WARN_ON_ONCE(__ns_ref_read(ns) != 1);
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) != 1);
		return true;
	}
	if (refcount_inc_not_zero(&ns->__ns_ref))
		return true;
	VFS_WARN_ON_ONCE(__ns_ref_active_read(ns));
	return false;
}

static __always_inline void __ns_ref_inc(struct ns_common *ns)
{
	if (is_ns_init_id(ns)) {
		VFS_WARN_ON_ONCE(__ns_ref_read(ns) != 1);
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) != 1);
		return;
	}
	refcount_inc(&ns->__ns_ref);
}

static __always_inline __must_check bool __ns_ref_dec_and_lock(struct ns_common *ns,
							       spinlock_t *ns_lock)
{
	if (is_ns_init_id(ns)) {
		VFS_WARN_ON_ONCE(__ns_ref_read(ns) != 1);
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) != 1);
		return false;
	}
	return refcount_dec_and_lock(&ns->__ns_ref, ns_lock);
}

#define ns_ref_read(__ns) __ns_ref_read(to_ns_common((__ns)))
#define ns_ref_inc(__ns) \
	do { if (__ns) __ns_ref_inc(to_ns_common((__ns))); } while (0)
#define ns_ref_get(__ns) \
	((__ns) ? __ns_ref_get(to_ns_common((__ns))) : false)
#define ns_ref_put(__ns) \
	((__ns) ? __ns_ref_put(to_ns_common((__ns))) : false)
#define ns_ref_put_and_lock(__ns, __ns_lock) \
	((__ns) ? __ns_ref_dec_and_lock(to_ns_common((__ns)), __ns_lock) : false)

#define ns_ref_active_read(__ns) \
	((__ns) ? __ns_ref_active_read(to_ns_common(__ns)) : 0)

void __ns_ref_active_put(struct ns_common *ns);

#define ns_ref_active_put(__ns) \
	do { if (__ns) __ns_ref_active_put(to_ns_common(__ns)); } while (0)

static __always_inline struct ns_common *__must_check ns_get_unless_inactive(struct ns_common *ns)
{
	if (!__ns_ref_active_read(ns)) {
		VFS_WARN_ON_ONCE(is_ns_init_id(ns));
		return NULL;
	}
	if (!__ns_ref_get(ns))
		return NULL;
	return ns;
}

void __ns_ref_active_get(struct ns_common *ns);

#define ns_ref_active_get(__ns) \
	do { if (__ns) __ns_ref_active_get(to_ns_common(__ns)); } while (0)

#endif
