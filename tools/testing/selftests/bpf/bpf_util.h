/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_UTIL__
#define __BPF_UTIL__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syscall.h>
#include <bpf/libbpf.h> /* libbpf_num_possible_cpus */
#include <linux/args.h>

static inline unsigned int bpf_num_possible_cpus(void)
{
	int possible_cpus = libbpf_num_possible_cpus();

	if (possible_cpus < 0) {
		printf("Failed to get # of possible cpus: '%s'!\n",
		       strerror(-possible_cpus));
		exit(1);
	}
	return possible_cpus;
}

/*
 * Simplified strscpy() implementation. The kernel one is in lib/string.c
 */
static inline ssize_t sized_strscpy(char *dest, const char *src, size_t count)
{
	long res = 0;

	if (count == 0)
		return -E2BIG;

	while (count > 1) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Force NUL-termination. */
	dest[res] = '\0';

	/* Return E2BIG if the source didn't stop */
	return src[res] ? -E2BIG : res;
}

#define __strscpy0(dst, src, ...)	\
	sized_strscpy(dst, src, sizeof(dst))
#define __strscpy1(dst, src, size)	\
	sized_strscpy(dst, src, size)

#undef strscpy /* Redefine the placeholder from tools/include/linux/string.h */
#define strscpy(dst, src, ...)	\
	CONCATENATE(__strscpy, COUNT_ARGS(__VA_ARGS__))(dst, src, __VA_ARGS__)

#define __bpf_percpu_val_align	__attribute__((__aligned__(8)))

#define BPF_DECLARE_PERCPU(type, name)				\
	struct { type v; /* padding */ } __bpf_percpu_val_align	\
		name[bpf_num_possible_cpus()]
#define bpf_percpu(name, cpu) name[(cpu)].v

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#ifndef offsetofend
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof_field(TYPE, MEMBER))
#endif

/* Availability of gettid across glibc versions is hit-and-miss, therefore
 * fallback to syscall in this macro and use it everywhere.
 */
#ifndef sys_gettid
#define sys_gettid() syscall(SYS_gettid)
#endif

/* and poison usage to ensure it does not creep back in. */
#pragma GCC poison gettid

#ifndef ENOTSUPP
#define ENOTSUPP 524
#endif

#endif /* __BPF_UTIL__ */
