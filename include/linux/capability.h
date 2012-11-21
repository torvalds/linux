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


#define _KERNEL_CAPABILITY_VERSION _LINUX_CAPABILITY_VERSION_3
#define _KERNEL_CAPABILITY_U32S    _LINUX_CAPABILITY_U32S_3

extern int file_caps_enabled;

typedef struct kernel_cap_struct {
	__u32 cap[_KERNEL_CAPABILITY_U32S];
} kernel_cap_t;

/* exact same as vfs_cap_data but in cpu endian and always filled completely */
struct cpu_vfs_cap_data {
	__u32 magic_etc;
	kernel_cap_t permitted;
	kernel_cap_t inheritable;
};

#define _USER_CAP_HEADER_SIZE  (sizeof(struct __user_cap_header_struct))
#define _KERNEL_CAP_T_SIZE     (sizeof(kernel_cap_t))


struct inode;
struct dentry;
struct user_namespace;

struct user_namespace *current_user_ns(void);

extern const kernel_cap_t __cap_empty_set;
extern const kernel_cap_t __cap_init_eff_set;

/*
 * Internal kernel functions only
 */

#define CAP_FOR_EACH_U32(__capi)  \
	for (__capi = 0; __capi < _KERNEL_CAPABILITY_U32S; ++__capi)

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

# define CAP_FS_MASK_B0     (CAP_TO_MASK(CAP_CHOWN)		\
			    | CAP_TO_MASK(CAP_MKNOD)		\
			    | CAP_TO_MASK(CAP_DAC_OVERRIDE)	\
			    | CAP_TO_MASK(CAP_DAC_READ_SEARCH)	\
			    | CAP_TO_MASK(CAP_FOWNER)		\
			    | CAP_TO_MASK(CAP_FSETID))

# define CAP_FS_MASK_B1     (CAP_TO_MASK(CAP_MAC_OVERRIDE))

#if _KERNEL_CAPABILITY_U32S != 2
# error Fix up hand-coded capability macro initializers
#else /* HAND-CODED capability initializers */

# define CAP_EMPTY_SET    ((kernel_cap_t){{ 0, 0 }})
# define CAP_FULL_SET     ((kernel_cap_t){{ ~0, ~0 }})
# define CAP_FS_SET       ((kernel_cap_t){{ CAP_FS_MASK_B0 \
				    | CAP_TO_MASK(CAP_LINUX_IMMUTABLE), \
				    CAP_FS_MASK_B1 } })
# define CAP_NFSD_SET     ((kernel_cap_t){{ CAP_FS_MASK_B0 \
				    | CAP_TO_MASK(CAP_SYS_RESOURCE), \
				    CAP_FS_MASK_B1 } })

#endif /* _KERNEL_CAPABILITY_U32S != 2 */

# define cap_clear(c)         do { (c) = __cap_empty_set; } while (0)

#define cap_raise(c, flag)  ((c).cap[CAP_TO_INDEX(flag)] |= CAP_TO_MASK(flag))
#define cap_lower(c, flag)  ((c).cap[CAP_TO_INDEX(flag)] &= ~CAP_TO_MASK(flag))
#define cap_raised(c, flag) ((c).cap[CAP_TO_INDEX(flag)] & CAP_TO_MASK(flag))

#define CAP_BOP_ALL(c, a, b, OP)                                    \
do {                                                                \
	unsigned __capi;                                            \
	CAP_FOR_EACH_U32(__capi) {                                  \
		c.cap[__capi] = a.cap[__capi] OP b.cap[__capi];     \
	}                                                           \
} while (0)

#define CAP_UOP_ALL(c, a, OP)                                       \
do {                                                                \
	unsigned __capi;                                            \
	CAP_FOR_EACH_U32(__capi) {                                  \
		c.cap[__capi] = OP a.cap[__capi];                   \
	}                                                           \
} while (0)

static inline kernel_cap_t cap_combine(const kernel_cap_t a,
				       const kernel_cap_t b)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, b, |);
	return dest;
}

static inline kernel_cap_t cap_intersect(const kernel_cap_t a,
					 const kernel_cap_t b)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, b, &);
	return dest;
}

static inline kernel_cap_t cap_drop(const kernel_cap_t a,
				    const kernel_cap_t drop)
{
	kernel_cap_t dest;
	CAP_BOP_ALL(dest, a, drop, &~);
	return dest;
}

static inline kernel_cap_t cap_invert(const kernel_cap_t c)
{
	kernel_cap_t dest;
	CAP_UOP_ALL(dest, c, ~);
	return dest;
}

static inline int cap_isclear(const kernel_cap_t a)
{
	unsigned __capi;
	CAP_FOR_EACH_U32(__capi) {
		if (a.cap[__capi] != 0)
			return 0;
	}
	return 1;
}

/*
 * Check if "a" is a subset of "set".
 * return 1 if ALL of the capabilities in "a" are also in "set"
 *	cap_issubset(0101, 1111) will return 1
 * return 0 if ANY of the capabilities in "a" are not in "set"
 *	cap_issubset(1111, 0101) will return 0
 */
static inline int cap_issubset(const kernel_cap_t a, const kernel_cap_t set)
{
	kernel_cap_t dest;
	dest = cap_drop(a, set);
	return cap_isclear(dest);
}

/* Used to decide between falling back on the old suser() or fsuser(). */

static inline int cap_is_fs_cap(int cap)
{
	const kernel_cap_t __cap_fs_set = CAP_FS_SET;
	return !!(CAP_TO_MASK(cap) & __cap_fs_set.cap[CAP_TO_INDEX(cap)]);
}

static inline kernel_cap_t cap_drop_fs_set(const kernel_cap_t a)
{
	const kernel_cap_t __cap_fs_set = CAP_FS_SET;
	return cap_drop(a, __cap_fs_set);
}

static inline kernel_cap_t cap_raise_fs_set(const kernel_cap_t a,
					    const kernel_cap_t permitted)
{
	const kernel_cap_t __cap_fs_set = CAP_FS_SET;
	return cap_combine(a,
			   cap_intersect(permitted, __cap_fs_set));
}

static inline kernel_cap_t cap_drop_nfsd_set(const kernel_cap_t a)
{
	const kernel_cap_t __cap_fs_set = CAP_NFSD_SET;
	return cap_drop(a, __cap_fs_set);
}

static inline kernel_cap_t cap_raise_nfsd_set(const kernel_cap_t a,
					      const kernel_cap_t permitted)
{
	const kernel_cap_t __cap_nfsd_set = CAP_NFSD_SET;
	return cap_combine(a,
			   cap_intersect(permitted, __cap_nfsd_set));
}

extern bool has_capability(struct task_struct *t, int cap);
extern bool has_ns_capability(struct task_struct *t,
			      struct user_namespace *ns, int cap);
extern bool has_capability_noaudit(struct task_struct *t, int cap);
extern bool has_ns_capability_noaudit(struct task_struct *t,
				      struct user_namespace *ns, int cap);
extern bool capable(int cap);
extern bool ns_capable(struct user_namespace *ns, int cap);
extern bool nsown_capable(int cap);
extern bool inode_capable(const struct inode *inode, int cap);

/* audit system wants to get cap info from files as well */
extern int get_vfs_caps_from_disk(const struct dentry *dentry, struct cpu_vfs_cap_data *cpu_caps);

#endif /* !_LINUX_CAPABILITY_H */
