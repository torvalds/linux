// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Red Hat, Inc.*/
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <linux/limits.h>
#include "bpf_misc.h"
#include "errno.h"

char *user_ptr = (char *)1;
char *invalid_kern_ptr = (char *)-1;

/*
 * When passing userspace pointers, the error code differs based on arch:
 *   -ERANGE on arches with non-overlapping address spaces
 *   -EFAULT on other arches
 */
#if defined(__TARGET_ARCH_arm) || defined(__TARGET_ARCH_loongarch) || \
    defined(__TARGET_ARCH_powerpc) || defined(__TARGET_ARCH_x86)
#define USER_PTR_ERR -ERANGE
#else
#define USER_PTR_ERR -EFAULT
#endif

/*
 * On s390, __get_kernel_nofault (used in string kfuncs) returns 0 for NULL and
 * user_ptr (instead of causing an exception) so the below two groups of tests
 * are not applicable.
 */
#ifndef __TARGET_ARCH_s390

/* Passing NULL to string kfuncs (treated as a userspace ptr) */
SEC("syscall") __retval(USER_PTR_ERR) int test_strcmp_null1(void *ctx) { return bpf_strcmp(NULL, "hello"); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strcmp_null2(void *ctx) { return bpf_strcmp("hello", NULL); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strchr_null(void *ctx) { return bpf_strchr(NULL, 'a'); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strchrnul_null(void *ctx) { return bpf_strchrnul(NULL, 'a'); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strnchr_null(void *ctx) { return bpf_strnchr(NULL, 1, 'a'); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strrchr_null(void *ctx) { return bpf_strrchr(NULL, 'a'); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strlen_null(void *ctx) { return bpf_strlen(NULL); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strnlen_null(void *ctx) { return bpf_strnlen(NULL, 1); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strspn_null1(void *ctx) { return bpf_strspn(NULL, "hello"); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strspn_null2(void *ctx) { return bpf_strspn("hello", NULL); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strcspn_null1(void *ctx) { return bpf_strcspn(NULL, "hello"); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strcspn_null2(void *ctx) { return bpf_strcspn("hello", NULL); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strstr_null1(void *ctx) { return bpf_strstr(NULL, "hello"); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strstr_null2(void *ctx) { return bpf_strstr("hello", NULL); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strnstr_null1(void *ctx) { return bpf_strnstr(NULL, "hello", 1); }
SEC("syscall")  __retval(USER_PTR_ERR)int test_strnstr_null2(void *ctx) { return bpf_strnstr("hello", NULL, 1); }

/* Passing userspace ptr to string kfuncs */
SEC("syscall") __retval(USER_PTR_ERR) int test_strcmp_user_ptr1(void *ctx) { return bpf_strcmp(user_ptr, "hello"); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strcmp_user_ptr2(void *ctx) { return bpf_strcmp("hello", user_ptr); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strchr_user_ptr(void *ctx) { return bpf_strchr(user_ptr, 'a'); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strchrnul_user_ptr(void *ctx) { return bpf_strchrnul(user_ptr, 'a'); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strnchr_user_ptr(void *ctx) { return bpf_strnchr(user_ptr, 1, 'a'); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strrchr_user_ptr(void *ctx) { return bpf_strrchr(user_ptr, 'a'); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strlen_user_ptr(void *ctx) { return bpf_strlen(user_ptr); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strnlen_user_ptr(void *ctx) { return bpf_strnlen(user_ptr, 1); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strspn_user_ptr1(void *ctx) { return bpf_strspn(user_ptr, "hello"); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strspn_user_ptr2(void *ctx) { return bpf_strspn("hello", user_ptr); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strcspn_user_ptr1(void *ctx) { return bpf_strcspn(user_ptr, "hello"); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strcspn_user_ptr2(void *ctx) { return bpf_strcspn("hello", user_ptr); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strstr_user_ptr1(void *ctx) { return bpf_strstr(user_ptr, "hello"); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strstr_user_ptr2(void *ctx) { return bpf_strstr("hello", user_ptr); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strnstr_user_ptr1(void *ctx) { return bpf_strnstr(user_ptr, "hello", 1); }
SEC("syscall") __retval(USER_PTR_ERR) int test_strnstr_user_ptr2(void *ctx) { return bpf_strnstr("hello", user_ptr, 1); }

#endif /* __TARGET_ARCH_s390 */

/* Passing invalid kernel ptr to string kfuncs should always return -EFAULT */
SEC("syscall") __retval(-EFAULT) int test_strcmp_pagefault1(void *ctx) { return bpf_strcmp(invalid_kern_ptr, "hello"); }
SEC("syscall") __retval(-EFAULT) int test_strcmp_pagefault2(void *ctx) { return bpf_strcmp("hello", invalid_kern_ptr); }
SEC("syscall") __retval(-EFAULT) int test_strchr_pagefault(void *ctx) { return bpf_strchr(invalid_kern_ptr, 'a'); }
SEC("syscall") __retval(-EFAULT) int test_strchrnul_pagefault(void *ctx) { return bpf_strchrnul(invalid_kern_ptr, 'a'); }
SEC("syscall") __retval(-EFAULT) int test_strnchr_pagefault(void *ctx) { return bpf_strnchr(invalid_kern_ptr, 1, 'a'); }
SEC("syscall") __retval(-EFAULT) int test_strrchr_pagefault(void *ctx) { return bpf_strrchr(invalid_kern_ptr, 'a'); }
SEC("syscall") __retval(-EFAULT) int test_strlen_pagefault(void *ctx) { return bpf_strlen(invalid_kern_ptr); }
SEC("syscall") __retval(-EFAULT) int test_strnlen_pagefault(void *ctx) { return bpf_strnlen(invalid_kern_ptr, 1); }
SEC("syscall") __retval(-EFAULT) int test_strspn_pagefault1(void *ctx) { return bpf_strspn(invalid_kern_ptr, "hello"); }
SEC("syscall") __retval(-EFAULT) int test_strspn_pagefault2(void *ctx) { return bpf_strspn("hello", invalid_kern_ptr); }
SEC("syscall") __retval(-EFAULT) int test_strcspn_pagefault1(void *ctx) { return bpf_strcspn(invalid_kern_ptr, "hello"); }
SEC("syscall") __retval(-EFAULT) int test_strcspn_pagefault2(void *ctx) { return bpf_strcspn("hello", invalid_kern_ptr); }
SEC("syscall") __retval(-EFAULT) int test_strstr_pagefault1(void *ctx) { return bpf_strstr(invalid_kern_ptr, "hello"); }
SEC("syscall") __retval(-EFAULT) int test_strstr_pagefault2(void *ctx) { return bpf_strstr("hello", invalid_kern_ptr); }
SEC("syscall") __retval(-EFAULT) int test_strnstr_pagefault1(void *ctx) { return bpf_strnstr(invalid_kern_ptr, "hello", 1); }
SEC("syscall") __retval(-EFAULT) int test_strnstr_pagefault2(void *ctx) { return bpf_strnstr("hello", invalid_kern_ptr, 1); }

char _license[] SEC("license") = "GPL";
