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
#include <sys/auxv.h>
#include <linux/auxvec.h>

#include "../kselftest.h"
#include "rseq.h"

static const ptrdiff_t *libc_rseq_offset_p;
static const unsigned int *libc_rseq_size_p;
static const unsigned int *libc_rseq_flags_p;

/* Offset from the thread pointer to the rseq area. */
ptrdiff_t rseq_offset;

/*
 * Size of the registered rseq area. 0 if the registration was
 * unsuccessful.
 */
unsigned int rseq_size = -1U;

/* Flags used during rseq registration.  */
unsigned int rseq_flags;

/*
 * rseq feature size supported by the kernel. 0 if the registration was
 * unsuccessful.
 */
unsigned int rseq_feature_size = -1U;

static int rseq_ownership;
static int rseq_reg_success;	/* At least one rseq registration has succeded. */

/* Allocate a large area for the TLS. */
#define RSEQ_THREAD_AREA_ALLOC_SIZE	1024

/* Original struct rseq feature size is 20 bytes. */
#define ORIG_RSEQ_FEATURE_SIZE		20

/* Original struct rseq allocation size is 32 bytes. */
#define ORIG_RSEQ_ALLOC_SIZE		32

static
__thread struct rseq_abi __rseq_abi __attribute__((tls_model("initial-exec"), aligned(RSEQ_THREAD_AREA_ALLOC_SIZE))) = {
	.cpu_id = RSEQ_ABI_CPU_ID_UNINITIALIZED,
};

static int sys_rseq(struct rseq_abi *rseq_abi, uint32_t rseq_len,
		    int flags, uint32_t sig)
{
	return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}

static int sys_getcpu(unsigned *cpu, unsigned *node)
{
	return syscall(__NR_getcpu, cpu, node, NULL);
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
	rc = sys_rseq(&__rseq_abi, rseq_size, 0, RSEQ_SIG);
	if (rc) {
		if (RSEQ_READ_ONCE(rseq_reg_success)) {
			/* Incoherent success/failure within process. */
			abort();
		}
		return -1;
	}
	assert(rseq_current_cpu_raw() >= 0);
	RSEQ_WRITE_ONCE(rseq_reg_success, 1);
	return 0;
}

int rseq_unregister_current_thread(void)
{
	int rc;

	if (!rseq_ownership) {
		/* Treat libc's ownership as a successful unregistration. */
		return 0;
	}
	rc = sys_rseq(&__rseq_abi, rseq_size, RSEQ_ABI_FLAG_UNREGISTER, RSEQ_SIG);
	if (rc)
		return -1;
	return 0;
}

static
unsigned int get_rseq_feature_size(void)
{
	unsigned long auxv_rseq_feature_size, auxv_rseq_align;

	auxv_rseq_align = getauxval(AT_RSEQ_ALIGN);
	assert(!auxv_rseq_align || auxv_rseq_align <= RSEQ_THREAD_AREA_ALLOC_SIZE);

	auxv_rseq_feature_size = getauxval(AT_RSEQ_FEATURE_SIZE);
	assert(!auxv_rseq_feature_size || auxv_rseq_feature_size <= RSEQ_THREAD_AREA_ALLOC_SIZE);
	if (auxv_rseq_feature_size)
		return auxv_rseq_feature_size;
	else
		return ORIG_RSEQ_FEATURE_SIZE;
}

static __attribute__((constructor))
void rseq_init(void)
{
	libc_rseq_offset_p = dlsym(RTLD_NEXT, "__rseq_offset");
	libc_rseq_size_p = dlsym(RTLD_NEXT, "__rseq_size");
	libc_rseq_flags_p = dlsym(RTLD_NEXT, "__rseq_flags");
	if (libc_rseq_size_p && libc_rseq_offset_p && libc_rseq_flags_p &&
			*libc_rseq_size_p != 0) {
		/* rseq registration owned by glibc */
		rseq_offset = *libc_rseq_offset_p;
		rseq_size = *libc_rseq_size_p;
		rseq_flags = *libc_rseq_flags_p;
		rseq_feature_size = get_rseq_feature_size();
		if (rseq_feature_size > rseq_size)
			rseq_feature_size = rseq_size;
		return;
	}
	rseq_ownership = 1;
	if (!rseq_available()) {
		rseq_size = 0;
		rseq_feature_size = 0;
		return;
	}
	rseq_offset = (void *)&__rseq_abi - rseq_thread_pointer();
	rseq_flags = 0;
	rseq_feature_size = get_rseq_feature_size();
	if (rseq_feature_size == ORIG_RSEQ_FEATURE_SIZE)
		rseq_size = ORIG_RSEQ_ALLOC_SIZE;
	else
		rseq_size = RSEQ_THREAD_AREA_ALLOC_SIZE;
}

static __attribute__((destructor))
void rseq_exit(void)
{
	if (!rseq_ownership)
		return;
	rseq_offset = 0;
	rseq_size = -1U;
	rseq_feature_size = -1U;
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

int32_t rseq_fallback_current_node(void)
{
	uint32_t cpu_id, node_id;
	int ret;

	ret = sys_getcpu(&cpu_id, &node_id);
	if (ret) {
		perror("sys_getcpu()");
		return ret;
	}
	return (int32_t) node_id;
}
