/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_SYSCALLS_H
#define SELFTEST_KVM_SYSCALLS_H

#include <sys/syscall.h>

#define MAP_ARGS0(m,...)
#define MAP_ARGS1(m,t,a,...) m(t,a)
#define MAP_ARGS2(m,t,a,...) m(t,a), MAP_ARGS1(m,__VA_ARGS__)
#define MAP_ARGS3(m,t,a,...) m(t,a), MAP_ARGS2(m,__VA_ARGS__)
#define MAP_ARGS4(m,t,a,...) m(t,a), MAP_ARGS3(m,__VA_ARGS__)
#define MAP_ARGS5(m,t,a,...) m(t,a), MAP_ARGS4(m,__VA_ARGS__)
#define MAP_ARGS6(m,t,a,...) m(t,a), MAP_ARGS5(m,__VA_ARGS__)
#define MAP_ARGS(n,...) MAP_ARGS##n(__VA_ARGS__)

#define __DECLARE_ARGS(t, a)	t a
#define __UNPACK_ARGS(t, a)	a

#define DECLARE_ARGS(nr_args, args...) MAP_ARGS(nr_args, __DECLARE_ARGS, args)
#define UNPACK_ARGS(nr_args, args...) MAP_ARGS(nr_args, __UNPACK_ARGS, args)

#define __KVM_SYSCALL_ERROR(_name, _ret) \
	"%s failed, rc: %i errno: %i (%s)", (_name), (_ret), errno, strerror(errno)

/* Define a kvm_<syscall>() API to assert success. */
#define __KVM_SYSCALL_DEFINE(name, nr_args, args...)			\
static inline void kvm_##name(DECLARE_ARGS(nr_args, args))		\
{									\
	int r;								\
									\
	r = name(UNPACK_ARGS(nr_args, args));				\
	TEST_ASSERT(!r, __KVM_SYSCALL_ERROR(#name, r));			\
}

/*
 * Macro to define syscall APIs, either because KVM selftests doesn't link to
 * the standard library, e.g. libnuma, or because there is no library that yet
 * provides the syscall.  These
 */
#define KVM_SYSCALL_DEFINE(name, nr_args, args...)			\
static inline long name(DECLARE_ARGS(nr_args, args))			\
{									\
	return syscall(__NR_##name, UNPACK_ARGS(nr_args, args));	\
}									\
__KVM_SYSCALL_DEFINE(name, nr_args, args)

/*
 * Special case mmap(), as KVM selftest rarely/never specific an address,
 * rarely specify an offset, and because the unique return code requires
 * special handling anyways.
 */
static inline void *__kvm_mmap(size_t size, int prot, int flags, int fd,
			       off_t offset)
{
	void *mem;

	mem = mmap(NULL, size, prot, flags, fd, offset);
	TEST_ASSERT(mem != MAP_FAILED, __KVM_SYSCALL_ERROR("mmap()",
		    (int)(unsigned long)MAP_FAILED));
	return mem;
}

static inline void *kvm_mmap(size_t size, int prot, int flags, int fd)
{
	return __kvm_mmap(size, prot, flags, fd, 0);
}

static inline int kvm_dup(int fd)
{
	int new_fd = dup(fd);

	TEST_ASSERT(new_fd >= 0, __KVM_SYSCALL_ERROR("dup()", new_fd));
	return new_fd;
}

__KVM_SYSCALL_DEFINE(munmap, 2, void *, mem, size_t, size);
__KVM_SYSCALL_DEFINE(close, 1, int, fd);
__KVM_SYSCALL_DEFINE(fallocate, 4, int, fd, int, mode, loff_t, offset, loff_t, len);
__KVM_SYSCALL_DEFINE(ftruncate, 2, unsigned int, fd, off_t, length);

#endif /* SELFTEST_KVM_SYSCALLS_H */
