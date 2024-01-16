// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct bpf_testmod_btf_type_tag_1 {
	int a;
};

struct bpf_testmod_btf_type_tag_2 {
	struct bpf_testmod_btf_type_tag_1 *p;
};

int g;

SEC("fentry/bpf_testmod_test_btf_type_tag_user_1")
int BPF_PROG(test_user1, struct bpf_testmod_btf_type_tag_1 *arg)
{
	g = arg->a;
	return 0;
}

SEC("fentry/bpf_testmod_test_btf_type_tag_user_2")
int BPF_PROG(test_user2, struct bpf_testmod_btf_type_tag_2 *arg)
{
	g = arg->p->a;
	return 0;
}

/* int __sys_getsockname(int fd, struct sockaddr __user *usockaddr,
 *                       int __user *usockaddr_len);
 */
SEC("fentry/__sys_getsockname")
int BPF_PROG(test_sys_getsockname, int fd, struct sockaddr *usockaddr,
	     int *usockaddr_len)
{
	g = usockaddr->sa_family;
	return 0;
}
