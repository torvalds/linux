// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#if __has_attribute(btf_type_tag)
#define __tag1 __attribute__((btf_type_tag("tag1")))
#define __tag2 __attribute__((btf_type_tag("tag2")))
volatile const bool skip_tests = false;
#else
#define __tag1
#define __tag2
volatile const bool skip_tests = true;
#endif

struct btf_type_tag_test {
	int __tag1 * __tag1 __tag2 *p;
} g;

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(sub, int x)
{
  return 0;
}
