/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_MISC_H__
#define __BPF_MISC_H__

#define __msg(msg)		__attribute__((btf_decl_tag("comment:test_expect_msg=" msg)))
#define __failure		__attribute__((btf_decl_tag("comment:test_expect_failure")))
#define __success		__attribute__((btf_decl_tag("comment:test_expect_success")))
#define __log_level(lvl)	__attribute__((btf_decl_tag("comment:test_log_level="#lvl)))

#if defined(__TARGET_ARCH_x86)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__x64_"
#elif defined(__TARGET_ARCH_s390)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__s390x_"
#elif defined(__TARGET_ARCH_arm64)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__arm64_"
#else
#define SYSCALL_WRAPPER 0
#define SYS_PREFIX "__se_"
#endif

#endif
