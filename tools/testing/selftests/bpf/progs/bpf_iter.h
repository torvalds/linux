/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__bpf_map bpf_iter__bpf_map___not_used
#define bpf_iter__ipv6_route bpf_iter__ipv6_route___not_used
#define bpf_iter__netlink bpf_iter__netlink___not_used
#define bpf_iter__task bpf_iter__task___not_used
#define bpf_iter__task_file bpf_iter__task_file___not_used
#define bpf_iter__task_vma bpf_iter__task_vma___not_used
#define bpf_iter__tcp bpf_iter__tcp___not_used
#define tcp6_sock tcp6_sock___not_used
#define bpf_iter__udp bpf_iter__udp___not_used
#define udp6_sock udp6_sock___not_used
#define bpf_iter__unix bpf_iter__unix___not_used
#define bpf_iter__bpf_map_elem bpf_iter__bpf_map_elem___not_used
#define bpf_iter__bpf_sk_storage_map bpf_iter__bpf_sk_storage_map___not_used
#define bpf_iter__sockmap bpf_iter__sockmap___not_used
#define btf_ptr btf_ptr___not_used
#define BTF_F_COMPACT BTF_F_COMPACT___not_used
#define BTF_F_NONAME BTF_F_NONAME___not_used
#define BTF_F_PTR_RAW BTF_F_PTR_RAW___not_used
#define BTF_F_ZERO BTF_F_ZERO___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__bpf_map
#undef bpf_iter__ipv6_route
#undef bpf_iter__netlink
#undef bpf_iter__task
#undef bpf_iter__task_file
#undef bpf_iter__task_vma
#undef bpf_iter__tcp
#undef tcp6_sock
#undef bpf_iter__udp
#undef udp6_sock
#undef bpf_iter__unix
#undef bpf_iter__bpf_map_elem
#undef bpf_iter__bpf_sk_storage_map
#undef bpf_iter__sockmap
#undef btf_ptr
#undef BTF_F_COMPACT
#undef BTF_F_NONAME
#undef BTF_F_PTR_RAW
#undef BTF_F_ZERO

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__ipv6_route {
	struct bpf_iter_meta *meta;
	struct fib6_info *rt;
} __attribute__((preserve_access_index));

struct bpf_iter__netlink {
	struct bpf_iter_meta *meta;
	struct netlink_sock *sk;
} __attribute__((preserve_access_index));

struct bpf_iter__task {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
} __attribute__((preserve_access_index));

struct bpf_iter__task_file {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
	__u32 fd;
	struct file *file;
} __attribute__((preserve_access_index));

struct bpf_iter__task_vma {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
	struct vm_area_struct *vma;
} __attribute__((preserve_access_index));

struct bpf_iter__bpf_map {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
} __attribute__((preserve_access_index));

struct bpf_iter__tcp {
	struct bpf_iter_meta *meta;
	struct sock_common *sk_common;
	uid_t uid;
} __attribute__((preserve_access_index));

struct tcp6_sock {
	struct tcp_sock	tcp;
	struct ipv6_pinfo inet6;
} __attribute__((preserve_access_index));

struct bpf_iter__udp {
	struct bpf_iter_meta *meta;
	struct udp_sock *udp_sk;
	uid_t uid __attribute__((aligned(8)));
	int bucket __attribute__((aligned(8)));
} __attribute__((preserve_access_index));

struct udp6_sock {
	struct udp_sock	udp;
	struct ipv6_pinfo inet6;
} __attribute__((preserve_access_index));

struct bpf_iter__unix {
	struct bpf_iter_meta *meta;
	struct unix_sock *unix_sk;
	uid_t uid;
} __attribute__((preserve_access_index));

struct bpf_iter__bpf_map_elem {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
	void *key;
	void *value;
};

struct bpf_iter__bpf_sk_storage_map {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
	struct sock *sk;
	void *value;
};

struct bpf_iter__sockmap {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
	void *key;
	struct sock *sk;
};

struct btf_ptr {
	void *ptr;
	__u32 type_id;
	__u32 flags;
};

enum {
	BTF_F_COMPACT	=	(1ULL << 0),
	BTF_F_NONAME	=	(1ULL << 1),
	BTF_F_PTR_RAW	=	(1ULL << 2),
	BTF_F_ZERO	=	(1ULL << 3),
};
