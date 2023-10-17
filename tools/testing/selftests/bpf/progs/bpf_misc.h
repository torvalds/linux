/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_MISC_H__
#define __BPF_MISC_H__

/* This set of attributes controls behavior of the
 * test_loader.c:test_loader__run_subtests().
 *
 * The test_loader sequentially loads each program in a skeleton.
 * Programs could be loaded in privileged and unprivileged modes.
 * - __success, __failure, __msg imply privileged mode;
 * - __success_unpriv, __failure_unpriv, __msg_unpriv imply
 *   unprivileged mode.
 * If combination of privileged and unprivileged attributes is present
 * both modes are used. If none are present privileged mode is implied.
 *
 * See test_loader.c:drop_capabilities() for exact set of capabilities
 * that differ between privileged and unprivileged modes.
 *
 * For test filtering purposes the name of the program loaded in
 * unprivileged mode is derived from the usual program name by adding
 * `@unpriv' suffix.
 *
 * __msg             Message expected to be found in the verifier log.
 *                   Multiple __msg attributes could be specified.
 * __msg_unpriv      Same as __msg but for unprivileged mode.
 *
 * __success         Expect program load success in privileged mode.
 * __success_unpriv  Expect program load success in unprivileged mode.
 *
 * __failure         Expect program load failure in privileged mode.
 * __failure_unpriv  Expect program load failure in unprivileged mode.
 *
 * __retval          Execute the program using BPF_PROG_TEST_RUN command,
 *                   expect return value to match passed parameter:
 *                   - a decimal number
 *                   - a hexadecimal number, when starts from 0x
 *                   - literal INT_MIN
 *                   - literal POINTER_VALUE (see definition below)
 *                   - literal TEST_DATA_LEN (see definition below)
 * __retval_unpriv   Same, but load program in unprivileged mode.
 *
 * __description     Text to be used instead of a program name for display
 *                   and filtering purposes.
 *
 * __log_level       Log level to use for the program, numeric value expected.
 *
 * __flag            Adds one flag use for the program, the following values are valid:
 *                   - BPF_F_STRICT_ALIGNMENT;
 *                   - BPF_F_TEST_RND_HI32;
 *                   - BPF_F_TEST_STATE_FREQ;
 *                   - BPF_F_SLEEPABLE;
 *                   - BPF_F_XDP_HAS_FRAGS;
 *                   - A numeric value.
 *                   Multiple __flag attributes could be specified, the final flags
 *                   value is derived by applying binary "or" to all specified values.
 *
 * __auxiliary         Annotated program is not a separate test, but used as auxiliary
 *                     for some other test cases and should always be loaded.
 * __auxiliary_unpriv  Same, but load program in unprivileged mode.
 */
#define __msg(msg)		__attribute__((btf_decl_tag("comment:test_expect_msg=" msg)))
#define __failure		__attribute__((btf_decl_tag("comment:test_expect_failure")))
#define __success		__attribute__((btf_decl_tag("comment:test_expect_success")))
#define __description(desc)	__attribute__((btf_decl_tag("comment:test_description=" desc)))
#define __msg_unpriv(msg)	__attribute__((btf_decl_tag("comment:test_expect_msg_unpriv=" msg)))
#define __failure_unpriv	__attribute__((btf_decl_tag("comment:test_expect_failure_unpriv")))
#define __success_unpriv	__attribute__((btf_decl_tag("comment:test_expect_success_unpriv")))
#define __log_level(lvl)	__attribute__((btf_decl_tag("comment:test_log_level="#lvl)))
#define __flag(flag)		__attribute__((btf_decl_tag("comment:test_prog_flags="#flag)))
#define __retval(val)		__attribute__((btf_decl_tag("comment:test_retval="#val)))
#define __retval_unpriv(val)	__attribute__((btf_decl_tag("comment:test_retval_unpriv="#val)))
#define __auxiliary		__attribute__((btf_decl_tag("comment:test_auxiliary")))
#define __auxiliary_unpriv	__attribute__((btf_decl_tag("comment:test_auxiliary_unpriv")))

/* Convenience macro for use with 'asm volatile' blocks */
#define __naked __attribute__((naked))
#define __clobber_all "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory"
#define __clobber_common "r0", "r1", "r2", "r3", "r4", "r5", "memory"
#define __imm(name) [name]"i"(name)
#define __imm_const(name, expr) [name]"i"(expr)
#define __imm_addr(name) [name]"i"(&name)
#define __imm_ptr(name) [name]"p"(&name)
#define __imm_insn(name, expr) [name]"i"(*(long *)&(expr))

/* Magic constants used with __retval() */
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64

#ifndef __used
#define __used __attribute__((used))
#endif

#if defined(__TARGET_ARCH_x86)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__x64_"
#elif defined(__TARGET_ARCH_s390)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__s390x_"
#elif defined(__TARGET_ARCH_arm64)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__arm64_"
#elif defined(__TARGET_ARCH_riscv)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX "__riscv_"
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

/* make it look to compiler like value is read and written */
#define __sink(expr) asm volatile("" : "+g"(expr))

#endif
