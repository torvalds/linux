/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_MISC_H__
#define __BPF_MISC_H__

#define __msg(msg)		__attribute__((btf_decl_tag("comment:test_expect_msg=" msg)))
#define __failure		__attribute__((btf_decl_tag("comment:test_expect_failure")))
#define __success		__attribute__((btf_decl_tag("comment:test_expect_success")))
#define __log_level(lvl)	__attribute__((btf_decl_tag("comment:test_log_level="#lvl)))

/* Convenience macro for use with 'asm volatile' blocks */
#define __naked __attribute__((naked))
#define __clobber_all "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory"
#define __clobber_common "r0", "r1", "r2", "r3", "r4", "r5", "memory"
#define __imm(name) [name]"i"(name)
#define __imm_addr(name) [name]"i"(&name)

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

/* How many arguments are passed to function in register */
#if defined(__TARGET_ARCH_x86) || defined(__x86_64__)
#define FUNC_REG_ARG_CNT 6
#elif defined(__i386__)
#define FUNC_REG_ARG_CNT 3
#elif defined(__TARGET_ARCH_s390) || defined(__s390x__)
#define FUNC_REG_ARG_CNT 5
#elif defined(__TARGET_ARCH_arm) || defined(__arm__)
#define FUNC_REG_ARG_CNT 4
#elif defined(__TARGET_ARCH_arm64) || defined(__aarch64__)
#define FUNC_REG_ARG_CNT 8
#elif defined(__TARGET_ARCH_mips) || defined(__mips__)
#define FUNC_REG_ARG_CNT 8
#elif defined(__TARGET_ARCH_powerpc) || defined(__powerpc__) || defined(__powerpc64__)
#define FUNC_REG_ARG_CNT 8
#elif defined(__TARGET_ARCH_sparc) || defined(__sparc__)
#define FUNC_REG_ARG_CNT 6
#elif defined(__TARGET_ARCH_riscv) || defined(__riscv__)
#define FUNC_REG_ARG_CNT 8
#else
/* default to 5 for others */
#define FUNC_REG_ARG_CNT 5
#endif


#endif
