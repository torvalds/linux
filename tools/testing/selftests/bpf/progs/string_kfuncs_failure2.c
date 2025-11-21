// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Red Hat, Inc.*/
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <linux/limits.h>

char long_str[XATTR_SIZE_MAX + 1];

SEC("syscall") int test_strcmp_too_long(void *ctx) { return bpf_strcmp(long_str, long_str); }
SEC("syscall") int test_strcasecmp_too_long(void *ctx) { return bpf_strcasecmp(long_str, long_str); }
SEC("syscall") int test_strchr_too_long(void *ctx) { return bpf_strchr(long_str, 'b'); }
SEC("syscall") int test_strchrnul_too_long(void *ctx) { return bpf_strchrnul(long_str, 'b'); }
SEC("syscall") int test_strnchr_too_long(void *ctx) { return bpf_strnchr(long_str, sizeof(long_str), 'b'); }
SEC("syscall") int test_strrchr_too_long(void *ctx) { return bpf_strrchr(long_str, 'b'); }
SEC("syscall") int test_strlen_too_long(void *ctx) { return bpf_strlen(long_str); }
SEC("syscall") int test_strnlen_too_long(void *ctx) { return bpf_strnlen(long_str, sizeof(long_str)); }
SEC("syscall") int test_strspn_str_too_long(void *ctx) { return bpf_strspn(long_str, "a"); }
SEC("syscall") int test_strspn_accept_too_long(void *ctx) { return bpf_strspn("b", long_str); }
SEC("syscall") int test_strcspn_str_too_long(void *ctx) { return bpf_strcspn(long_str, "b"); }
SEC("syscall") int test_strcspn_reject_too_long(void *ctx) { return bpf_strcspn("b", long_str); }
SEC("syscall") int test_strstr_too_long(void *ctx) { return bpf_strstr(long_str, "hello"); }
SEC("syscall") int test_strnstr_too_long(void *ctx) { return bpf_strnstr(long_str, "hello", sizeof(long_str)); }

char _license[] SEC("license") = "GPL";
