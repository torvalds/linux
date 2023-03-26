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

/* make it look to compiler like value is read and written */
#define __sink(expr) asm volatile("" : "+g"(expr))

struct bpf_iter_num;

extern int bpf_iter_num_new(struct bpf_iter_num *it, int start, int end) __ksym;
extern int *bpf_iter_num_next(struct bpf_iter_num *it) __ksym;
extern void bpf_iter_num_destroy(struct bpf_iter_num *it) __ksym;

#ifndef bpf_for_each
/* bpf_for_each(iter_type, cur_elem, args...) provides generic construct for
 * using BPF open-coded iterators without having to write mundane explicit
 * low-level loop logic. Instead, it provides for()-like generic construct
 * that can be used pretty naturally. E.g., for some hypothetical cgroup
 * iterator, you'd write:
 *
 * struct cgroup *cg, *parent_cg = <...>;
 *
 * bpf_for_each(cgroup, cg, parent_cg, CG_ITER_CHILDREN) {
 *     bpf_printk("Child cgroup id = %d", cg->cgroup_id);
 *     if (cg->cgroup_id == 123)
 *         break;
 * }
 *
 * I.e., it looks almost like high-level for each loop in other languages,
 * supports continue/break, and is verifiable by BPF verifier.
 *
 * For iterating integers, the difference betwen bpf_for_each(num, i, N, M)
 * and bpf_for(i, N, M) is in that bpf_for() provides additional proof to
 * verifier that i is in [N, M) range, and in bpf_for_each() case i is `int
 * *`, not just `int`. So for integers bpf_for() is more convenient.
 *
 * Note: this macro relies on C99 feature of allowing to declare variables
 * inside for() loop, bound to for() loop lifetime. It also utilizes GCC
 * extension: __attribute__((cleanup(<func>))), supported by both GCC and
 * Clang.
 */
#define bpf_for_each(type, cur, args...) for (							\
	/* initialize and define destructor */							\
	struct bpf_iter_##type ___it __attribute__((aligned(8), /* enforce, just in case */,	\
						    cleanup(bpf_iter_##type##_destroy))),	\
	/* ___p pointer is just to call bpf_iter_##type##_new() *once* to init ___it */		\
			       *___p __attribute__((unused)) = (				\
					bpf_iter_##type##_new(&___it, ##args),			\
	/* this is a workaround for Clang bug: it currently doesn't emit BTF */			\
	/* for bpf_iter_##type##_destroy() when used from cleanup() attribute */		\
					(void)bpf_iter_##type##_destroy, (void *)0);		\
	/* iteration and termination check */							\
	(((cur) = bpf_iter_##type##_next(&___it)));						\
)
#endif /* bpf_for_each */

#ifndef bpf_for
/* bpf_for(i, start, end) implements a for()-like looping construct that sets
 * provided integer variable *i* to values starting from *start* through,
 * but not including, *end*. It also proves to BPF verifier that *i* belongs
 * to range [start, end), so this can be used for accessing arrays without
 * extra checks.
 *
 * Note: *start* and *end* are assumed to be expressions with no side effects
 * and whose values do not change throughout bpf_for() loop execution. They do
 * not have to be statically known or constant, though.
 *
 * Note: similarly to bpf_for_each(), it relies on C99 feature of declaring for()
 * loop bound variables and cleanup attribute, supported by GCC and Clang.
 */
#define bpf_for(i, start, end) for (								\
	/* initialize and define destructor */							\
	struct bpf_iter_num ___it __attribute__((aligned(8), /* enforce, just in case */	\
						 cleanup(bpf_iter_num_destroy))),		\
	/* ___p pointer is necessary to call bpf_iter_num_new() *once* to init ___it */		\
			    *___p __attribute__((unused)) = (					\
				bpf_iter_num_new(&___it, (start), (end)),			\
	/* this is a workaround for Clang bug: it currently doesn't emit BTF */			\
	/* for bpf_iter_num_destroy() when used from cleanup() attribute */			\
				(void)bpf_iter_num_destroy, (void *)0);				\
	({											\
		/* iteration step */								\
		int *___t = bpf_iter_num_next(&___it);						\
		/* termination and bounds check */						\
		(___t && ((i) = *___t, (i) >= (start) && (i) < (end)));				\
	});											\
)
#endif /* bpf_for */

#ifndef bpf_repeat
/* bpf_repeat(N) performs N iterations without exposing iteration number
 *
 * Note: similarly to bpf_for_each(), it relies on C99 feature of declaring for()
 * loop bound variables and cleanup attribute, supported by GCC and Clang.
 */
#define bpf_repeat(N) for (									\
	/* initialize and define destructor */							\
	struct bpf_iter_num ___it __attribute__((aligned(8), /* enforce, just in case */	\
						 cleanup(bpf_iter_num_destroy))),		\
	/* ___p pointer is necessary to call bpf_iter_num_new() *once* to init ___it */		\
			    *___p __attribute__((unused)) = (					\
				bpf_iter_num_new(&___it, 0, (N)),				\
	/* this is a workaround for Clang bug: it currently doesn't emit BTF */			\
	/* for bpf_iter_num_destroy() when used from cleanup() attribute */			\
				(void)bpf_iter_num_destroy, (void *)0);				\
	bpf_iter_num_next(&___it);								\
	/* nothing here  */									\
)
#endif /* bpf_repeat */

#endif
