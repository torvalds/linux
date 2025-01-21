/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#ifndef _UPTR_TEST_COMMON_H
#define _UPTR_TEST_COMMON_H

#define MAGIC_VALUE 0xabcd1234
#define PAGE_SIZE 4096

#ifdef __BPF__
/* Avoid fwd btf type being generated for the following struct */
struct large_data *dummy_large_data;
struct empty_data *dummy_empty_data;
struct user_data *dummy_data;
struct cgroup *dummy_cgrp;
#else
#define __uptr
#define __kptr
#endif

struct user_data {
	int a;
	int b;
	int result;
	int nested_result;
};

struct nested_udata {
	struct user_data __uptr *udata;
};

struct value_type {
	struct user_data __uptr *udata;
	struct cgroup __kptr *cgrp;
	struct nested_udata nested;
};

struct value_lock_type {
	struct user_data __uptr *udata;
	struct bpf_spin_lock lock;
};

struct large_data {
	__u8 one_page[PAGE_SIZE];
	int a;
};

struct large_uptr {
	struct large_data __uptr *udata;
};

struct empty_data {
};

struct empty_uptr {
	struct empty_data __uptr *udata;
};

struct kstruct_uptr {
	struct cgroup __uptr *cgrp;
};

#endif
