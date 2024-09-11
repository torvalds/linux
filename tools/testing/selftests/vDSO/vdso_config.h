/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vdso_config.h: Configuration options for vDSO tests.
 * Copyright (c) 2019 Arm Ltd.
 */
#ifndef __VDSO_CONFIG_H__
#define __VDSO_CONFIG_H__

/*
 * Each architecture exports its vDSO implementation with different names
 * and a different version from the others, so we need to handle it as a
 * special case.
 */
#if defined(__arm__)
#define VDSO_VERSION		0
#define VDSO_NAMES		1
#define VDSO_32BIT		1
#elif defined(__aarch64__)
#define VDSO_VERSION		3
#define VDSO_NAMES		0
#elif defined(__powerpc64__)
#define VDSO_VERSION		1
#define VDSO_NAMES		0
#elif defined(__powerpc__)
#define VDSO_VERSION		1
#define VDSO_NAMES		0
#define VDSO_32BIT		1
#elif defined (__s390__) && !defined(__s390x__)
#define VDSO_VERSION		2
#define VDSO_NAMES		0
#define VDSO_32BIT		1
#elif defined (__s390x__)
#define VDSO_VERSION		2
#define VDSO_NAMES		0
#elif defined(__mips__)
#define VDSO_VERSION		0
#define VDSO_NAMES		1
#define VDSO_32BIT		1
#elif defined(__sparc__)
#define VDSO_VERSION		0
#define VDSO_NAMES		1
#define VDSO_32BIT		1
#elif defined(__i386__)
#define VDSO_VERSION		0
#define VDSO_NAMES		1
#define VDSO_32BIT		1
#elif defined(__x86_64__)
#define VDSO_VERSION		0
#define VDSO_NAMES		1
#elif defined(__riscv__) || defined(__riscv)
#define VDSO_VERSION		5
#define VDSO_NAMES		1
#if __riscv_xlen == 32
#define VDSO_32BIT		1
#endif
#elif defined(__loongarch__)
#define VDSO_VERSION		6
#define VDSO_NAMES		1
#endif

static const char *versions[7] = {
	"LINUX_2.6",
	"LINUX_2.6.15",
	"LINUX_2.6.29",
	"LINUX_2.6.39",
	"LINUX_4",
	"LINUX_4.15",
	"LINUX_5.10"
};

static const char *names[2][6] = {
	{
		"__kernel_gettimeofday",
		"__kernel_clock_gettime",
		"__kernel_time",
		"__kernel_clock_getres",
		"__kernel_getcpu",
#if defined(VDSO_32BIT)
		"__kernel_clock_gettime64",
#endif
	},
	{
		"__vdso_gettimeofday",
		"__vdso_clock_gettime",
		"__vdso_time",
		"__vdso_clock_getres",
		"__vdso_getcpu",
#if defined(VDSO_32BIT)
		"__vdso_clock_gettime64",
#endif
	},
};

#endif /* __VDSO_CONFIG_H__ */
