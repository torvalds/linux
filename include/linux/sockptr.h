/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 Christoph Hellwig.
 *
 * Support for "universal" pointers that can point to either kernel or userspace
 * memory.
 */
#ifndef _LINUX_SOCKPTR_H
#define _LINUX_SOCKPTR_H

#include <linux/slab.h>
#include <linux/uaccess.h>

typedef struct {
	union {
		void		*kernel;
		void __user	*user;
	};
	bool		is_kernel : 1;
} sockptr_t;

static inline bool sockptr_is_kernel(sockptr_t sockptr)
{
	return sockptr.is_kernel;
}

static inline sockptr_t KERNEL_SOCKPTR(void *p)
{
	return (sockptr_t) { .kernel = p, .is_kernel = true };
}

static inline sockptr_t USER_SOCKPTR(void __user *p)
{
	return (sockptr_t) { .user = p };
}

static inline bool sockptr_is_null(sockptr_t sockptr)
{
	if (sockptr_is_kernel(sockptr))
		return !sockptr.kernel;
	return !sockptr.user;
}

static inline int copy_from_sockptr_offset(void *dst, sockptr_t src,
		size_t offset, size_t size)
{
	if (!sockptr_is_kernel(src))
		return copy_from_user(dst, src.user + offset, size);
	memcpy(dst, src.kernel + offset, size);
	return 0;
}

/* Deprecated.
 * This is unsafe, unless caller checked user provided optlen.
 * Prefer copy_safe_from_sockptr() instead.
 *
 * Returns 0 for success, or number of bytes not copied on error.
 */
static inline int copy_from_sockptr(void *dst, sockptr_t src, size_t size)
{
	return copy_from_sockptr_offset(dst, src, 0, size);
}

/**
 * copy_safe_from_sockptr: copy a struct from sockptr
 * @dst:   Destination address, in kernel space. This buffer must be @ksize
 *         bytes long.
 * @ksize: Size of @dst struct.
 * @optval: Source address. (in user or kernel space)
 * @optlen: Size of @optval data.
 *
 * Returns:
 *  * -EINVAL: @optlen < @ksize
 *  * -EFAULT: access to userspace failed.
 *  * 0 : @ksize bytes were copied
 */
static inline int copy_safe_from_sockptr(void *dst, size_t ksize,
					 sockptr_t optval, unsigned int optlen)
{
	if (optlen < ksize)
		return -EINVAL;
	if (copy_from_sockptr(dst, optval, ksize))
		return -EFAULT;
	return 0;
}

static inline int copy_struct_from_sockptr(void *dst, size_t ksize,
		sockptr_t src, size_t usize)
{
	size_t size = min(ksize, usize);
	size_t rest = max(ksize, usize) - size;

	if (!sockptr_is_kernel(src))
		return copy_struct_from_user(dst, ksize, src.user, size);

	if (usize < ksize) {
		memset(dst + size, 0, rest);
	} else if (usize > ksize) {
		char *p = src.kernel;

		while (rest--) {
			if (*p++)
				return -E2BIG;
		}
	}
	memcpy(dst, src.kernel, size);
	return 0;
}

static inline int copy_to_sockptr_offset(sockptr_t dst, size_t offset,
		const void *src, size_t size)
{
	if (!sockptr_is_kernel(dst))
		return copy_to_user(dst.user + offset, src, size);
	memcpy(dst.kernel + offset, src, size);
	return 0;
}

static inline int copy_to_sockptr(sockptr_t dst, const void *src, size_t size)
{
	return copy_to_sockptr_offset(dst, 0, src, size);
}

static inline void *memdup_sockptr_noprof(sockptr_t src, size_t len)
{
	void *p = kmalloc_track_caller_noprof(len, GFP_USER | __GFP_NOWARN);

	if (!p)
		return ERR_PTR(-ENOMEM);
	if (copy_from_sockptr(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	return p;
}
#define memdup_sockptr(...)	alloc_hooks(memdup_sockptr_noprof(__VA_ARGS__))

static inline void *memdup_sockptr_nul_noprof(sockptr_t src, size_t len)
{
	char *p = kmalloc_track_caller_noprof(len + 1, GFP_KERNEL);

	if (!p)
		return ERR_PTR(-ENOMEM);
	if (copy_from_sockptr(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	p[len] = '\0';
	return p;
}
#define memdup_sockptr_nul(...)	alloc_hooks(memdup_sockptr_nul_noprof(__VA_ARGS__))

static inline long strncpy_from_sockptr(char *dst, sockptr_t src, size_t count)
{
	if (sockptr_is_kernel(src)) {
		size_t len = min(strnlen(src.kernel, count - 1) + 1, count);

		memcpy(dst, src.kernel, len);
		return len;
	}
	return strncpy_from_user(dst, src.user, count);
}

static inline int check_zeroed_sockptr(sockptr_t src, size_t offset,
				       size_t size)
{
	if (!sockptr_is_kernel(src))
		return check_zeroed_user(src.user + offset, size);
	return memchr_inv(src.kernel + offset, 0, size) == NULL;
}

#endif /* _LINUX_SOCKPTR_H */
