// SPDX-License-Identifier: LGPL-2.1
/*
 * rseq.c
 *
 * Copyright (C) 2016 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>
#include <assert.h>
#include <signal.h>
#include <limits.h>
#include <dlfcn.h>
#include <stddef.h>

#include <linux/compiler.h>

#include "../kselftest.h"
#include "rseq.h"

/*
 * Define weak versions to play nice with binaries that are statically linked
 * against a libc that doesn't support registering its own rseq.
 */
__weak ptrdiff_t __rseq_offset;
__weak unsigned int __rseq_size;
__weak unsigned int __rseq_flags;

static const ptrdiff_t *libc_rseq_offset_p = &__rseq_offset;
static const unsigned int *libc_rseq_size_p = &__rseq_size;
static const unsigned int *libc_rseq_flags_p = &__rseq_flags;

/* Offset from the thread pointer to the rseq area.  */
ptrdiff_t rseq_offset;

/* Size of the registered rseq area.  0 if the registration was
   unsuccessful.  */
unsigned int rseq_size = -1U;

/* Flags used during rseq registration.  */
unsigned int rseq_flags;

static int rseq_ownership;

static
__thread struct rseq_abi __rseq_abi __attribute__((tls_model("initial-exec"))) = {
	.cpu_id = RSEQ_ABI_CPU_ID_UNINITIALIZED,
};

static int sys_rseq(struct rseq_abi *rseq_abi, uint32_t rseq_len,
		    int flags, uint32_t sig)
{
	return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}

int rseq_available(void)
{
	int rc;

	rc = sys_rseq(NULL, 0, 0, 0);
	if (rc != -1)
		abort();
	switch (errno) {
	case ENOSYS:
		return 0;
	case EINVAL:
		return 1;
	default:
		abort();
	}
}

int rseq_register_current_thread(void)
{
	int rc;

	if (!rseq_ownership) {
		/* Treat libc's ownership as a successful registration. */
		return 0;
	}
	rc = sys_rseq(&__rseq_abi, sizeof(struct rseq_abi), 0, RSEQ_SIG);
	if (rc)
		return -1;
	assert(rseq_current_cpu_raw() >= 0);
	return 0;
}

int rseq_unregister_current_thread(void)
{
	int rc;

	if (!rseq_ownership) {
		/* Treat libc's ownership as a successful unregistration. */
		return 0;
	}
	rc = sys_rseq(&__rseq_abi, sizeof(struct rseq_abi), RSEQ_ABI_FLAG_UNREGISTER, RSEQ_SIG);
	if (rc)
		return -1;
	return 0;
}

static __attribute__((constructor))
void rseq_init(void)
{
	/*
	 * If the libc's registered rseq size isn't already valid, it may be
	 * because the binary is dynamically linked and not necessarily due to
	 * libc not having registered a restartable sequence.  Try to find the
	 * symbols if that's the case.
	 */
	if (!*libc_rseq_size_p) {
		libc_rseq_offset_p = dlsym(RTLD_NEXT, "__rseq_offset");
		libc_rseq_size_p = dlsym(RTLD_NEXT, "__rseq_size");
		libc_rseq_flags_p = dlsym(RTLD_NEXT, "__rseq_flags");
	}
	if (libc_rseq_size_p && libc_rseq_offset_p && libc_rseq_flags_p &&
			*libc_rseq_size_p != 0) {
		/* rseq registration owned by glibc */
		rseq_offset = *libc_rseq_offset_p;
		rseq_size = *libc_rseq_size_p;
		rseq_flags = *libc_rseq_flags_p;
		return;
	}
	if (!rseq_available())
		return;
	rseq_ownership = 1;
	rseq_offset = (void *)&__rseq_abi - rseq_thread_pointer();
	rseq_size = sizeof(struct rseq_abi);
	rseq_flags = 0;
}

static __attribute__((destructor))
void rseq_exit(void)
{
	if (!rseq_ownership)
		return;
	rseq_offset = 0;
	rseq_size = -1U;
	rseq_ownership = 0;
}

int32_t rseq_fallback_current_cpu(void)
{
	int32_t cpu;

	cpu = sched_getcpu();
	if (cpu < 0) {
		perror("sched_getcpu()");
		abort();
	}
	return cpu;
}
