/* SPDX-License-Identifier: GPL-2.0-only */
/* A pointer that can point to either kernel or userspace memory. */
#ifndef _LINUX_BPFPTR_H
#define _LINUX_BPFPTR_H

#include <linux/mm.h>
#include <linux/sockptr.h>

typedef sockptr_t bpfptr_t;

static inline bool bpfptr_is_kernel(bpfptr_t bpfptr)
{
	return bpfptr.is_kernel;
}

static inline bpfptr_t KERNEL_BPFPTR(void *p)
{
	return (bpfptr_t) { .kernel = p, .is_kernel = true };
}

static inline bpfptr_t USER_BPFPTR(void __user *p)
{
	return (bpfptr_t) { .user = p };
}

static inline bpfptr_t make_bpfptr(u64 addr, bool is_kernel)
{
	if (is_kernel)
		return KERNEL_BPFPTR((void*) (uintptr_t) addr);
	else
		return USER_BPFPTR(u64_to_user_ptr(addr));
}

static inline bool bpfptr_is_null(bpfptr_t bpfptr)
{
	if (bpfptr_is_kernel(bpfptr))
		return !bpfptr.kernel;
	return !bpfptr.user;
}

static inline void bpfptr_add(bpfptr_t *bpfptr, size_t val)
{
	if (bpfptr_is_kernel(*bpfptr))
		bpfptr->kernel += val;
	else
		bpfptr->user += val;
}

static inline int copy_from_bpfptr_offset(void *dst, bpfptr_t src,
					  size_t offset, size_t size)
{
	if (!bpfptr_is_kernel(src))
		return copy_from_user(dst, src.user + offset, size);
	return copy_from_kernel_nofault(dst, src.kernel + offset, size);
}

static inline int copy_from_bpfptr(void *dst, bpfptr_t src, size_t size)
{
	return copy_from_bpfptr_offset(dst, src, 0, size);
}

static inline int copy_to_bpfptr_offset(bpfptr_t dst, size_t offset,
					const void *src, size_t size)
{
	return copy_to_sockptr_offset((sockptr_t) dst, offset, src, size);
}

static inline void *kvmemdup_bpfptr_noprof(bpfptr_t src, size_t len)
{
	void *p = kvmalloc_noprof(len, GFP_USER | __GFP_NOWARN);

	if (!p)
		return ERR_PTR(-ENOMEM);
	if (copy_from_bpfptr(p, src, len)) {
		kvfree(p);
		return ERR_PTR(-EFAULT);
	}
	return p;
}
#define kvmemdup_bpfptr(...)	alloc_hooks(kvmemdup_bpfptr_noprof(__VA_ARGS__))

static inline long strncpy_from_bpfptr(char *dst, bpfptr_t src, size_t count)
{
	if (bpfptr_is_kernel(src))
		return strncpy_from_kernel_nofault(dst, src.kernel, count);
	return strncpy_from_user(dst, src.user, count);
}

#endif /* _LINUX_BPFPTR_H */
