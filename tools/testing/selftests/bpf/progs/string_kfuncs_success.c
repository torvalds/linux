// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Red Hat, Inc.*/
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "errno.h"

char str[] = "hello world";

#define __test(retval) SEC("syscall") __success __retval(retval)

/* Functional tests */
__test(0) int test_strcmp_eq(void *ctx) { return bpf_strcmp(str, "hello world"); }
__test(1) int test_strcmp_neq(void *ctx) { return bpf_strcmp(str, "hello"); }
__test(0) int test_strcasecmp_eq1(void *ctx) { return bpf_strcasecmp(str, "hello world"); }
__test(0) int test_strcasecmp_eq2(void *ctx) { return bpf_strcasecmp(str, "HELLO WORLD"); }
__test(0) int test_strcasecmp_eq3(void *ctx) { return bpf_strcasecmp(str, "HELLO world"); }
__test(1) int test_strcasecmp_neq1(void *ctx) { return bpf_strcasecmp(str, "hello"); }
__test(1) int test_strcasecmp_neq2(void *ctx) { return bpf_strcasecmp(str, "HELLO"); }
__test(1) int test_strchr_found(void *ctx) { return bpf_strchr(str, 'e'); }
__test(11) int test_strchr_null(void *ctx) { return bpf_strchr(str, '\0'); }
__test(-ENOENT) int test_strchr_notfound(void *ctx) { return bpf_strchr(str, 'x'); }
__test(1) int test_strchrnul_found(void *ctx) { return bpf_strchrnul(str, 'e'); }
__test(11) int test_strchrnul_notfound(void *ctx) { return bpf_strchrnul(str, 'x'); }
__test(1) int test_strnchr_found(void *ctx) { return bpf_strnchr(str, 5, 'e'); }
__test(11) int test_strnchr_null(void *ctx) { return bpf_strnchr(str, 12, '\0'); }
__test(-ENOENT) int test_strnchr_notfound(void *ctx) { return bpf_strnchr(str, 5, 'w'); }
__test(9) int test_strrchr_found(void *ctx) { return bpf_strrchr(str, 'l'); }
__test(11) int test_strrchr_null(void *ctx) { return bpf_strrchr(str, '\0'); }
__test(-ENOENT) int test_strrchr_notfound(void *ctx) { return bpf_strrchr(str, 'x'); }
__test(11) int test_strlen(void *ctx) { return bpf_strlen(str); }
__test(11) int test_strnlen(void *ctx) { return bpf_strnlen(str, 12); }
__test(5) int test_strspn(void *ctx) { return bpf_strspn(str, "ehlo"); }
__test(2) int test_strcspn(void *ctx) { return bpf_strcspn(str, "lo"); }
__test(6) int test_strstr_found(void *ctx) { return bpf_strstr(str, "world"); }
__test(-ENOENT) int test_strstr_notfound(void *ctx) { return bpf_strstr(str, "hi"); }
__test(0) int test_strstr_empty(void *ctx) { return bpf_strstr(str, ""); }
__test(0) int test_strnstr_found1(void *ctx) { return bpf_strnstr("", "", 0); }
__test(0) int test_strnstr_found2(void *ctx) { return bpf_strnstr(str, "hello", 5); }
__test(0) int test_strnstr_found3(void *ctx) { return bpf_strnstr(str, "hello", 6); }
__test(-ENOENT) int test_strnstr_notfound1(void *ctx) { return bpf_strnstr(str, "hi", 10); }
__test(-ENOENT) int test_strnstr_notfound2(void *ctx) { return bpf_strnstr(str, "hello", 4); }
__test(-ENOENT) int test_strnstr_notfound3(void *ctx) { return bpf_strnstr("", "a", 0); }
__test(0) int test_strnstr_empty(void *ctx) { return bpf_strnstr(str, "", 1); }

char _license[] SEC("license") = "GPL";
