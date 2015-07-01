/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/bpf.h>

#include "libbpf.h"

#define __printf(a, b)	__attribute__((format(printf, a, b)))

__printf(1, 2)
static int __base_pr(const char *format, ...)
{
	va_list args;
	int err;

	va_start(args, format);
	err = vfprintf(stderr, format, args);
	va_end(args);
	return err;
}

static __printf(1, 2) libbpf_print_fn_t __pr_warning = __base_pr;
static __printf(1, 2) libbpf_print_fn_t __pr_info = __base_pr;
static __printf(1, 2) libbpf_print_fn_t __pr_debug;

#define __pr(func, fmt, ...)	\
do {				\
	if ((func))		\
		(func)("libbpf: " fmt, ##__VA_ARGS__); \
} while (0)

#define pr_warning(fmt, ...)	__pr(__pr_warning, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	__pr(__pr_info, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)	__pr(__pr_debug, fmt, ##__VA_ARGS__)

void libbpf_set_print(libbpf_print_fn_t warn,
		      libbpf_print_fn_t info,
		      libbpf_print_fn_t debug)
{
	__pr_warning = warn;
	__pr_info = info;
	__pr_debug = debug;
}
