#ifndef __BPF_SYS__
#define __BPF_SYS__

#include <stdint.h>
#include <stdlib.h>

#include <sys/syscall.h>

#include <linux/bpf.h>

static inline __u64 bpf_ptr_to_u64(const void *ptr)
{
	return (__u64)(unsigned long) ptr;
}

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
#ifdef __NR_bpf
	return syscall(__NR_bpf, cmd, attr, size);
#else
	fprintf(stderr, "No bpf syscall, kernel headers too old?\n");
	errno = ENOSYS;
	return -1;
#endif
}

#endif /* __BPF_SYS__ */
