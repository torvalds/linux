/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */
#ifndef __BPF_LIBBPF_H
#define __BPF_LIBBPF_H

#include <stdio.h>

/*
 * In include/linux/compiler-gcc.h, __printf is defined. However
 * it should be better if libbpf.h doesn't depend on Linux header file.
 * So instead of __printf, here we use gcc attribute directly.
 */
typedef int (*libbpf_print_fn_t)(const char *, ...)
	__attribute__((format(printf, 1, 2)));

void libbpf_set_print(libbpf_print_fn_t warn,
		      libbpf_print_fn_t info,
		      libbpf_print_fn_t debug);

/* Hide internal to user */
struct bpf_object;

struct bpf_object *bpf_object__open(const char *path);
void bpf_object__close(struct bpf_object *object);

#endif
