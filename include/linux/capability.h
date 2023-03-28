/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is <linux/capability.h>
 *
 * Andrew G. Morgan <morgan@kernel.org>
 * Alexander Kjeldaas <astor@guardian.no>
 * with help from Aleph1, Roland Buresund and Andrew Main.
 *
 * See here for the libcap library ("POSIX draft" compliance):
 *
 * ftp://www.kernel.org/pub/linux/libs/security/linux-privs/kernel-2.6/
 */
#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <uapi/linux/capability.h>
#include <linux/uidgid.h>
#include <linux/bits.h>

#define _KERNEL_CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3

extern int file_caps_enabled;

typedef struct { u64 val; } kernel_cap_t;

/* same as vfs_ns_cap_data but in cpu endian and always filled completely */
struct cpu_vfs_cap_data {
	__u32 magic_etc;
	kuid_t rootid;
	kernel_cap_t permitted;
	kernel_cap_t inheritable;
};

#define _USER_CAP_HEADER_SIZE  (sizeof(struct __user_cap_header_struct))
#define _KERNEL_CAP_T_SIZE     (sizeof(kernel_cap_t))

struct file;
struct inode;
struct dentry;
struct task_struct;
struct user_namespace;
struct mnt_idmap;

/*
 * CAP_FS_MASK and CAP_NFSD_MASKS:
 *
 * The fs mask is all the privileges that fsuid==0 historically meant.
 * At one time in the past, that included CAP_MKNOD and CAP_LINUX_IMMUTABLE.
 *
 * It has never meant setting security.* and trusted.* xattrs.
 *
 * We could also define fsmask as follows:
 *   1. CAP_FS_MASK is the privilege to bypass all fs-related DAC permissions
 *   2. The security.* and trusted.* xattrs are fs-related MAC permissions
 */

# define CAP_FS_MASK     (BIT_ULL(CAP_CHOWN)		\
			| BIT_ULL(CAP_MKNOD)		\
			| BIT_ULL(CAP_DAC_OVERRIDE)	\
			| BIT_ULL(CAP_DAC_READ_SEARCH)	\
			| BIT_ULL(CAP_FOWNER)		\
			| BIT_ULL(CAP_FSETID)		\
			| BIT_ULL(CAP_MAC_OVERRIDE))
#define CAP_VALID_MASK	 (BIT_ULL(CAP_LAST_CAP+1)-1)

# define CAP_EMPTY_SET    ((kernel_cap_t) { 0 })
# define CAP_FULL_SET     ((kernel_cap_t) { CAP_VALID_MASK })
# define CAP_FS_SET       ((kernel_cap_t) { CAP_FS_MASK | BIT_ULL(CAP_LINUX_IMMUTABLE) })
# define CAP_NFSD_SET     ((kernel_cap_t) { CAP_FS_MASK | BIT_ULL(CAP_SYS_RESOURCE) })

# define cap_clear(c)         do { (c).val = 0; } while (0)

#define cap_raise(c, flag)  ((c).val |= BIT_ULL(flag))
#define cap_lower(c, flag)  ((c).val &= ~BIT_ULL(flag))
#define cap_raised(c, flag) (((c).val & BIT_ULL(flag)) != 0)

static inline kernel_cap_t cap_combine(const kernel_cap_t a,
				       const kernel_cap_t b)
{
	return (kernel_cap_t) { a.val | b.val };
}

static inline kernel_cap_t cap_intersect(const kernel_cap_t a,
					 const kernel_cap_t b)
{
	return (kernel_cap_t) { a.val & b.val };
}

static inline kernel_cap_t cap_drop(const kernel_cap_t a,
				    const kernel_cap_t drop)
{
	return (kernel_cap_t) { a.val &~ drop.val };
}

static inline bool cap_isclear(const kernel_cap_t a)
{
	return !a.val;
}

static inline bool cap_isidentical(const kernel_cap_t a, const kernel_cap_t b)
{
	return a.val == b.val;
}

/*
 * Check if "a" is a subset of "set".
 * return true if ALL of the capabilities in "a" are also in "set"
 *	cap_issubset(0101, 1111) will return true
 * return false if ANY of the capabilities in "a" are not in "set"
 *	cap_issubset(1111, 0101) will return false
 */
static inline bool cap_issubset(const kernel_cap_t a, const kernel_cap_t set)
{
	return !(a.val & ~set.val);
}

/* Used to decide between falling back on the old suser() or fsuser(). */

static inline kernel_cap_t cap_drop_fs_set(const kernel_cap_t a)
{
	return cap_drop(a, CAP_FS_SET);
}

static inline kernel_cap_t cap_raise_fs_set(const kernel_cap_t a,
					    const kernel_cap_t permitted)
{
	return cap_combine(a, cap_intersect(permitted, CAP_FS_SET));
}

static inline kernel_cap_t cap_drop_nfsd_set(const kernel_cap_t a)
{
	return cap_drop(a, CAP_NFSD_SET);
}

static inline kernel_cap_t cap_raise_nfsd_set(const kernel_cap_t a,
					      const kernel_cap_t permitted)
{
	return cap_combine(a, cap_intersect(permitted, CAP_NFSD_SET));
}

#ifdef CONFIG_MULTIUSER
extern bool has_capability(struct task_struct *t, int cap);
extern bool has_ns_capability(struct task_struct *t,
			      struct user_namespace *ns, int cap);
extern bool has_capability_noaudit(struct task_struct *t, int cap);
extern bool has_ns_capability_noaudit(struct task_struct *t,
				      struct user_namespace *ns, int cap);
extern bool capable(int cap);
extern bool ns_capable(struct user_namespace *ns, int cap);
extern bool ns_capable_noaudit(struct user_namespace *ns, int cap);
extern bool ns_capable_setid(struct user_namespace *ns, int cap);
#else
static inline bool has_capability(struct task_struct *t, int cap)
{
	return true;
}
static inline bool has_ns_capability(struct task_struct *t,
			      struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool has_capability_noaudit(struct task_struct *t, int cap)
{
	return true;
}
static inline bool has_ns_capability_noaudit(struct task_struct *t,
				      struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool capable(int cap)
{
	return true;
}
static inline bool ns_capable(struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool ns_capable_noaudit(struct user_namespace *ns, int cap)
{
	return true;
}
static inline bool ns_capable_setid(struct user_namespace *ns, int cap)
{
	return true;
}
#endif /* CONFIG_MULTIUSER */
bool privileged_wrt_inode_uidgid(struct user_namespace *ns,
				 struct mnt_idmap *idmap,
				 const struct inode *inode);
bool capable_wrt_inode_uidgid(struct mnt_idmap *idmap,
			      const struct inode *inode, int cap);
extern bool file_ns_capable(const struct file *file, struct user_namespace *ns, int cap);
extern bool ptracer_capable(struct task_struct *tsk, struct user_namespace *ns);
static inline bool perfmon_capable(void)
{
	return capable(CAP_PERFMON) || capable(CAP_SYS_ADMIN);
}

static inline bool bpf_capable(void)
{
	return capable(CAP_BPF) || capable(CAP_SYS_ADMIN);
}

static inline bool checkpoint_restore_ns_capable(struct user_namespace *ns)
{
	return ns_capable(ns, CAP_CHECKPOINT_RESTORE) ||
		ns_capable(ns, CAP_SYS_ADMIN);
}

/* audit system wants to get cap info from files as well */
int get_vfs_caps_from_disk(struct mnt_idmap *idmap,
			   const struct dentry *dentry,
			   struct cpu_vfs_cap_data *cpu_caps);

int cap_convert_nscap(struct mnt_idmap *idmap, struct dentry *dentry,
		      const void **ivalue, size_t size);

#endif /* !_LINUX_CAPABILITY_H */
