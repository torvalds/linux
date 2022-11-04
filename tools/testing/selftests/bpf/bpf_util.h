/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_UTIL__
#define __BPF_UTIL__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/libbpf.h> /* libbpf_num_possible_cpus */

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

/* Copy up to sz - 1 bytes from zero-terminated src string and ensure that dst
 * is zero-terminated string no matter what (unless sz == 0, in which case
 * it's a no-op). It's conceptually close to FreeBSD's strlcpy(), but differs
 * in what is returned. Given this is internal helper, it's trivial to extend
 * this, when necessary. Use this instead of strncpy inside libbpf source code.
 */
static inline void bpf_strlcpy(char *dst, const char *src, size_t sz)
{
	size_t i;

	if (sz == 0)
		return;

	sz--;
	for (i = 0; i < sz && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

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

#endif /* __BPF_UTIL__ */
