/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BPF_MISC_H__
#define __BPF_MISC_H__

#define XSTR(s) STR(s)
#define STR(s) #s

/* Expand a macro and then stringize the expansion */
#define QUOTE(str) #str
#define EXPAND_QUOTE(str) QUOTE(str)

/* This set of attributes controls behavior of the
 * test_loader.c:test_loader__run_subtests().
 *
 * The test_loader sequentially loads each program in a skeleton.
 * Programs could be loaded in privileged and unprivileged modes.
 * - __success, __failure, __msg, __regex imply privileged mode;
 * - __success_unpriv, __failure_unpriv, __msg_unpriv, __regex_unpriv
 *   imply unprivileged mode.
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
 *                   To match a regular expression use "{{" "}}" brackets,
 *                   e.g. "foo{{[0-9]+}}"  matches strings like "foo007".
 *                   Extended POSIX regular expression syntax is allowed
 *                   inside the brackets.
 * __msg_unpriv      Same as __msg but for unprivileged mode.
 *
 * __xlated          Expect a line in a disassembly log after verifier applies rewrites.
 *                   Multiple __xlated attributes could be specified.
 *                   Regular expressions could be specified same way as in __msg.
 * __xlated_unpriv   Same as __xlated but for unprivileged mode.
 *
 * __jited           Match a line in a disassembly of the jited BPF program.
 *                   Has to be used after __arch_* macro.
 *                   For example:
 *
 *                       __arch_x86_64
 *                       __jited("   endbr64")
 *                       __jited("   nopl    (%rax,%rax)")
 *                       __jited("   xorq    %rax, %rax")
 *                       ...
 *                       __naked void some_test(void)
 *                       {
 *                           asm volatile (... ::: __clobber_all);
 *                       }
 *
 *                   Regular expressions could be included in patterns same way
 *                   as in __msg.
 *
 *                   By default assume that each pattern has to be matched on the
 *                   next consecutive line of disassembly, e.g.:
 *
 *                       __jited("   endbr64")             # matched on line N
 *                       __jited("   nopl    (%rax,%rax)") # matched on line N+1
 *
 *                   If match occurs on a wrong line an error is reported.
 *                   To override this behaviour use literal "...", e.g.:
 *
 *                       __jited("   endbr64")             # matched on line N
 *                       __jited("...")                    # not matched
 *                       __jited("   nopl    (%rax,%rax)") # matched on any line >= N
 *
 * __jited_unpriv    Same as __jited but for unprivileged mode.
 *
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
 *                   - a macro which expands to one of the above
 *                   - literal _INT_MIN (expands to INT_MIN)
 *                   In addition, two special macros are defined below:
 *                   - POINTER_VALUE
 *                   - TEST_DATA_LEN
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
 *
 * __arch_*          Specify on which architecture the test case should be tested.
 *                   Several __arch_* annotations could be specified at once.
 *                   When test case is not run on current arch it is marked as skipped.
 * __caps_unpriv     Specify the capabilities that should be set when running the test.
 */
#define __msg(msg)		__attribute__((btf_decl_tag("comment:test_expect_msg=" XSTR(__COUNTER__) "=" msg)))
#define __xlated(msg)		__attribute__((btf_decl_tag("comment:test_expect_xlated=" XSTR(__COUNTER__) "=" msg)))
#define __jited(msg)		__attribute__((btf_decl_tag("comment:test_jited=" XSTR(__COUNTER__) "=" msg)))
#define __failure		__attribute__((btf_decl_tag("comment:test_expect_failure")))
#define __success		__attribute__((btf_decl_tag("comment:test_expect_success")))
#define __description(desc)	__attribute__((btf_decl_tag("comment:test_description=" desc)))
#define __msg_unpriv(msg)	__attribute__((btf_decl_tag("comment:test_expect_msg_unpriv=" XSTR(__COUNTER__) "=" msg)))
#define __xlated_unpriv(msg)	__attribute__((btf_decl_tag("comment:test_expect_xlated_unpriv=" XSTR(__COUNTER__) "=" msg)))
#define __jited_unpriv(msg)	__attribute__((btf_decl_tag("comment:test_jited=" XSTR(__COUNTER__) "=" msg)))
#define __failure_unpriv	__attribute__((btf_decl_tag("comment:test_expect_failure_unpriv")))
#define __success_unpriv	__attribute__((btf_decl_tag("comment:test_expect_success_unpriv")))
#define __log_level(lvl)	__attribute__((btf_decl_tag("comment:test_log_level="#lvl)))
#define __flag(flag)		__attribute__((btf_decl_tag("comment:test_prog_flags="#flag)))
#define __retval(val)		__attribute__((btf_decl_tag("comment:test_retval="XSTR(val))))
#define __retval_unpriv(val)	__attribute__((btf_decl_tag("comment:test_retval_unpriv="XSTR(val))))
#define __auxiliary		__attribute__((btf_decl_tag("comment:test_auxiliary")))
#define __auxiliary_unpriv	__attribute__((btf_decl_tag("comment:test_auxiliary_unpriv")))
#define __btf_path(path)	__attribute__((btf_decl_tag("comment:test_btf_path=" path)))
#define __arch(arch)		__attribute__((btf_decl_tag("comment:test_arch=" arch)))
#define __arch_x86_64		__arch("X86_64")
#define __arch_arm64		__arch("ARM64")
#define __arch_riscv64		__arch("RISCV64")
#define __caps_unpriv(caps)	__attribute__((btf_decl_tag("comment:test_caps_unpriv=" EXPAND_QUOTE(caps))))
#define __load_if_JITed()	__attribute__((btf_decl_tag("comment:load_mode=jited")))
#define __load_if_no_JITed()	__attribute__((btf_decl_tag("comment:load_mode=no_jited")))

/* Define common capabilities tested using __caps_unpriv */
#define CAP_NET_ADMIN		12
#define CAP_SYS_ADMIN		21
#define CAP_PERFMON		38
#define CAP_BPF			39

/* Convenience macro for use with 'asm volatile' blocks */
#define __naked __attribute__((naked))
#define __clobber_all "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory"
#define __clobber_common "r0", "r1", "r2", "r3", "r4", "r5", "memory"
#define __imm(name) [name]"i"(name)
#define __imm_const(name, expr) [name]"i"(expr)
#define __imm_addr(name) [name]"i"(&name)
#define __imm_ptr(name) [name]"r"(&name)
#define __imm_insn(name, expr) [name]"i"(*(long *)&(expr))

/* Magic constants used with __retval() */
#define POINTER_VALUE	0xbadcafe
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
#elif defined(__TARGET_ARCH_powerpc)
#define SYSCALL_WRAPPER 1
#define SYS_PREFIX ""
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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) ||	\
     (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) ||		\
     defined(__TARGET_ARCH_arm) || defined(__TARGET_ARCH_s390) ||	\
     defined(__TARGET_ARCH_loongarch)) &&				\
	__clang_major__ >= 18
#define CAN_USE_GOTOL
#endif

#if __clang_major__ >= 18
#define CAN_USE_BPF_ST
#endif

#if __clang_major__ >= 18 && defined(ENABLE_ATOMICS_TESTS) &&		\
	(defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) ||	\
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64))
#define CAN_USE_LOAD_ACQ_STORE_REL
#endif

#if defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86)
#define SPEC_V1
#endif

#if defined(__TARGET_ARCH_x86)
#define SPEC_V4
#endif

#endif
