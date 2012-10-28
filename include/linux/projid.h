#ifndef _LINUX_PROJID_H
#define _LINUX_PROJID_H

/*
 * A set of types for the internal kernel types representing project ids.
 *
 * The types defined in this header allow distinguishing which project ids in
 * the kernel are values used by userspace and which project id values are
 * the internal kernel values.  With the addition of user namespaces the values
 * can be different.  Using the type system makes it possible for the compiler
 * to detect when we overlook these differences.
 *
 */
#include <linux/types.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

typedef __kernel_uid32_t projid_t;

#ifdef CONFIG_UIDGID_STRICT_TYPE_CHECKS

typedef struct {
	projid_t val;
} kprojid_t;

static inline projid_t __kprojid_val(kprojid_t projid)
{
	return projid.val;
}

#define KPROJIDT_INIT(value) (kprojid_t){ value }

#else

typedef projid_t kprojid_t;

static inline projid_t __kprojid_val(kprojid_t projid)
{
	return projid;
}

#define KPROJIDT_INIT(value) ((kprojid_t) value )

#endif

#define INVALID_PROJID KPROJIDT_INIT(-1)
#define OVERFLOW_PROJID 65534

static inline bool projid_eq(kprojid_t left, kprojid_t right)
{
	return __kprojid_val(left) == __kprojid_val(right);
}

static inline bool projid_lt(kprojid_t left, kprojid_t right)
{
	return __kprojid_val(left) < __kprojid_val(right);
}

static inline bool projid_valid(kprojid_t projid)
{
	return !projid_eq(projid, INVALID_PROJID);
}

#ifdef CONFIG_USER_NS

extern kprojid_t make_kprojid(struct user_namespace *from, projid_t projid);

extern projid_t from_kprojid(struct user_namespace *to, kprojid_t projid);
extern projid_t from_kprojid_munged(struct user_namespace *to, kprojid_t projid);

static inline bool kprojid_has_mapping(struct user_namespace *ns, kprojid_t projid)
{
	return from_kprojid(ns, projid) != (projid_t)-1;
}

#else

static inline kprojid_t make_kprojid(struct user_namespace *from, projid_t projid)
{
	return KPROJIDT_INIT(projid);
}

static inline projid_t from_kprojid(struct user_namespace *to, kprojid_t kprojid)
{
	return __kprojid_val(kprojid);
}

static inline projid_t from_kprojid_munged(struct user_namespace *to, kprojid_t kprojid)
{
	projid_t projid = from_kprojid(to, kprojid);
	if (projid == (projid_t)-1)
		projid = OVERFLOW_PROJID;
	return projid;
}

static inline bool kprojid_has_mapping(struct user_namespace *ns, kprojid_t projid)
{
	return true;
}

#endif /* CONFIG_USER_NS */

#endif /* _LINUX_PROJID_H */
