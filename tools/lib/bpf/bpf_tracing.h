/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_TRACING_H__
#define __BPF_TRACING_H__

/* Scan the ARCH passed in from ARCH env variable (see Makefile) */
#if defined(__TARGET_ARCH_x86)
	#define bpf_target_x86
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_s390)
	#define bpf_target_s390
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_arm)
	#define bpf_target_arm
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_arm64)
	#define bpf_target_arm64
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_mips)
	#define bpf_target_mips
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_powerpc)
	#define bpf_target_powerpc
	#define bpf_target_defined
#elif defined(__TARGET_ARCH_sparc)
	#define bpf_target_sparc
	#define bpf_target_defined
#else
	#undef bpf_target_defined
#endif

/* Fall back to what the compiler says */
#ifndef bpf_target_defined
#if defined(__x86_64__)
	#define bpf_target_x86
#elif defined(__s390__)
	#define bpf_target_s390
#elif defined(__arm__)
	#define bpf_target_arm
#elif defined(__aarch64__)
	#define bpf_target_arm64
#elif defined(__mips__)
	#define bpf_target_mips
#elif defined(__powerpc__)
	#define bpf_target_powerpc
#elif defined(__sparc__)
	#define bpf_target_sparc
#endif
#endif

#if defined(bpf_target_x86)

#if defined(__KERNEL__) || defined(__VMLINUX_H__)

#define PT_REGS_PARM1(x) ((x)->di)
#define PT_REGS_PARM2(x) ((x)->si)
#define PT_REGS_PARM3(x) ((x)->dx)
#define PT_REGS_PARM4(x) ((x)->cx)
#define PT_REGS_PARM5(x) ((x)->r8)
#define PT_REGS_RET(x) ((x)->sp)
#define PT_REGS_FP(x) ((x)->bp)
#define PT_REGS_RC(x) ((x)->ax)
#define PT_REGS_SP(x) ((x)->sp)
#define PT_REGS_IP(x) ((x)->ip)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), di)
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), si)
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), dx)
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), cx)
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), r8)
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), sp)
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((x), bp)
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), ax)
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), sp)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), ip)

#else

#ifdef __i386__
/* i386 kernel is built with -mregparm=3 */
#define PT_REGS_PARM1(x) ((x)->eax)
#define PT_REGS_PARM2(x) ((x)->edx)
#define PT_REGS_PARM3(x) ((x)->ecx)
#define PT_REGS_PARM4(x) 0
#define PT_REGS_PARM5(x) 0
#define PT_REGS_RET(x) ((x)->esp)
#define PT_REGS_FP(x) ((x)->ebp)
#define PT_REGS_RC(x) ((x)->eax)
#define PT_REGS_SP(x) ((x)->esp)
#define PT_REGS_IP(x) ((x)->eip)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), eax)
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), edx)
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), ecx)
#define PT_REGS_PARM4_CORE(x) 0
#define PT_REGS_PARM5_CORE(x) 0
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), esp)
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((x), ebp)
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), eax)
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), esp)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), eip)

#else

#define PT_REGS_PARM1(x) ((x)->rdi)
#define PT_REGS_PARM2(x) ((x)->rsi)
#define PT_REGS_PARM3(x) ((x)->rdx)
#define PT_REGS_PARM4(x) ((x)->rcx)
#define PT_REGS_PARM5(x) ((x)->r8)
#define PT_REGS_RET(x) ((x)->rsp)
#define PT_REGS_FP(x) ((x)->rbp)
#define PT_REGS_RC(x) ((x)->rax)
#define PT_REGS_SP(x) ((x)->rsp)
#define PT_REGS_IP(x) ((x)->rip)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), rdi)
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), rsi)
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), rdx)
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), rcx)
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), r8)
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), rsp)
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((x), rbp)
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), rax)
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), rsp)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), rip)

#endif
#endif

#elif defined(bpf_target_s390)

/* s390 provides user_pt_regs instead of struct pt_regs to userspace */
struct pt_regs;
#define PT_REGS_S390 const volatile user_pt_regs
#define PT_REGS_PARM1(x) (((PT_REGS_S390 *)(x))->gprs[2])
#define PT_REGS_PARM2(x) (((PT_REGS_S390 *)(x))->gprs[3])
#define PT_REGS_PARM3(x) (((PT_REGS_S390 *)(x))->gprs[4])
#define PT_REGS_PARM4(x) (((PT_REGS_S390 *)(x))->gprs[5])
#define PT_REGS_PARM5(x) (((PT_REGS_S390 *)(x))->gprs[6])
#define PT_REGS_RET(x) (((PT_REGS_S390 *)(x))->gprs[14])
/* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_FP(x) (((PT_REGS_S390 *)(x))->gprs[11])
#define PT_REGS_RC(x) (((PT_REGS_S390 *)(x))->gprs[2])
#define PT_REGS_SP(x) (((PT_REGS_S390 *)(x))->gprs[15])
#define PT_REGS_IP(x) (((PT_REGS_S390 *)(x))->psw.addr)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[2])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[3])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[4])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[5])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[6])
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[14])
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[11])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[2])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), gprs[15])
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((PT_REGS_S390 *)(x), psw.addr)

#elif defined(bpf_target_arm)

#define PT_REGS_PARM1(x) ((x)->uregs[0])
#define PT_REGS_PARM2(x) ((x)->uregs[1])
#define PT_REGS_PARM3(x) ((x)->uregs[2])
#define PT_REGS_PARM4(x) ((x)->uregs[3])
#define PT_REGS_PARM5(x) ((x)->uregs[4])
#define PT_REGS_RET(x) ((x)->uregs[14])
#define PT_REGS_FP(x) ((x)->uregs[11]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x) ((x)->uregs[0])
#define PT_REGS_SP(x) ((x)->uregs[13])
#define PT_REGS_IP(x) ((x)->uregs[12])

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), uregs[0])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), uregs[1])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), uregs[2])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), uregs[3])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), uregs[4])
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), uregs[14])
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((x), uregs[11])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), uregs[0])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), uregs[13])
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), uregs[12])

#elif defined(bpf_target_arm64)

/* arm64 provides struct user_pt_regs instead of struct pt_regs to userspace */
struct pt_regs;
#define PT_REGS_ARM64 const volatile struct user_pt_regs
#define PT_REGS_PARM1(x) (((PT_REGS_ARM64 *)(x))->regs[0])
#define PT_REGS_PARM2(x) (((PT_REGS_ARM64 *)(x))->regs[1])
#define PT_REGS_PARM3(x) (((PT_REGS_ARM64 *)(x))->regs[2])
#define PT_REGS_PARM4(x) (((PT_REGS_ARM64 *)(x))->regs[3])
#define PT_REGS_PARM5(x) (((PT_REGS_ARM64 *)(x))->regs[4])
#define PT_REGS_RET(x) (((PT_REGS_ARM64 *)(x))->regs[30])
/* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_FP(x) (((PT_REGS_ARM64 *)(x))->regs[29])
#define PT_REGS_RC(x) (((PT_REGS_ARM64 *)(x))->regs[0])
#define PT_REGS_SP(x) (((PT_REGS_ARM64 *)(x))->sp)
#define PT_REGS_IP(x) (((PT_REGS_ARM64 *)(x))->pc)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[0])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[1])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[2])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[3])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[4])
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[30])
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[29])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), regs[0])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), sp)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((PT_REGS_ARM64 *)(x), pc)

#elif defined(bpf_target_mips)

#define PT_REGS_PARM1(x) ((x)->regs[4])
#define PT_REGS_PARM2(x) ((x)->regs[5])
#define PT_REGS_PARM3(x) ((x)->regs[6])
#define PT_REGS_PARM4(x) ((x)->regs[7])
#define PT_REGS_PARM5(x) ((x)->regs[8])
#define PT_REGS_RET(x) ((x)->regs[31])
#define PT_REGS_FP(x) ((x)->regs[30]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x) ((x)->regs[2])
#define PT_REGS_SP(x) ((x)->regs[29])
#define PT_REGS_IP(x) ((x)->cp0_epc)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), regs[4])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), regs[5])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), regs[6])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), regs[7])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), regs[8])
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), regs[31])
#define PT_REGS_FP_CORE(x) BPF_CORE_READ((x), regs[30])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), regs[2])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), regs[29])
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), cp0_epc)

#elif defined(bpf_target_powerpc)

#define PT_REGS_PARM1(x) ((x)->gpr[3])
#define PT_REGS_PARM2(x) ((x)->gpr[4])
#define PT_REGS_PARM3(x) ((x)->gpr[5])
#define PT_REGS_PARM4(x) ((x)->gpr[6])
#define PT_REGS_PARM5(x) ((x)->gpr[7])
#define PT_REGS_RC(x) ((x)->gpr[3])
#define PT_REGS_SP(x) ((x)->sp)
#define PT_REGS_IP(x) ((x)->nip)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), gpr[3])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), gpr[4])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), gpr[5])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), gpr[6])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), gpr[7])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), gpr[3])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), sp)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), nip)

#elif defined(bpf_target_sparc)

#define PT_REGS_PARM1(x) ((x)->u_regs[UREG_I0])
#define PT_REGS_PARM2(x) ((x)->u_regs[UREG_I1])
#define PT_REGS_PARM3(x) ((x)->u_regs[UREG_I2])
#define PT_REGS_PARM4(x) ((x)->u_regs[UREG_I3])
#define PT_REGS_PARM5(x) ((x)->u_regs[UREG_I4])
#define PT_REGS_RET(x) ((x)->u_regs[UREG_I7])
#define PT_REGS_RC(x) ((x)->u_regs[UREG_I0])
#define PT_REGS_SP(x) ((x)->u_regs[UREG_FP])

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I0])
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I1])
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I2])
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I3])
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I4])
#define PT_REGS_RET_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I7])
#define PT_REGS_RC_CORE(x) BPF_CORE_READ((x), u_regs[UREG_I0])
#define PT_REGS_SP_CORE(x) BPF_CORE_READ((x), u_regs[UREG_FP])

/* Should this also be a bpf_target check for the sparc case? */
#if defined(__arch64__)
#define PT_REGS_IP(x) ((x)->tpc)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), tpc)
#else
#define PT_REGS_IP(x) ((x)->pc)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ((x), pc)
#endif

#endif

#if defined(bpf_target_powerpc)
#define BPF_KPROBE_READ_RET_IP(ip, ctx)		({ (ip) = (ctx)->link; })
#define BPF_KRETPROBE_READ_RET_IP		BPF_KPROBE_READ_RET_IP
#elif defined(bpf_target_sparc)
#define BPF_KPROBE_READ_RET_IP(ip, ctx)		({ (ip) = PT_REGS_RET(ctx); })
#define BPF_KRETPROBE_READ_RET_IP		BPF_KPROBE_READ_RET_IP
#else
#define BPF_KPROBE_READ_RET_IP(ip, ctx)					    \
	({ bpf_probe_read_kernel(&(ip), sizeof(ip), (void *)PT_REGS_RET(ctx)); })
#define BPF_KRETPROBE_READ_RET_IP(ip, ctx)				    \
	({ bpf_probe_read_kernel(&(ip), sizeof(ip),			    \
			  (void *)(PT_REGS_FP(ctx) + sizeof(ip))); })
#endif

#define ___bpf_concat(a, b) a ## b
#define ___bpf_apply(fn, n) ___bpf_concat(fn, n)
#define ___bpf_nth(_, _1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, N, ...) N
#define ___bpf_narg(...) \
	___bpf_nth(_, ##__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define ___bpf_empty(...) \
	___bpf_nth(_, ##__VA_ARGS__, N, N, N, N, N, N, N, N, N, N, 0)

#define ___bpf_ctx_cast0() ctx
#define ___bpf_ctx_cast1(x) ___bpf_ctx_cast0(), (void *)ctx[0]
#define ___bpf_ctx_cast2(x, args...) ___bpf_ctx_cast1(args), (void *)ctx[1]
#define ___bpf_ctx_cast3(x, args...) ___bpf_ctx_cast2(args), (void *)ctx[2]
#define ___bpf_ctx_cast4(x, args...) ___bpf_ctx_cast3(args), (void *)ctx[3]
#define ___bpf_ctx_cast5(x, args...) ___bpf_ctx_cast4(args), (void *)ctx[4]
#define ___bpf_ctx_cast6(x, args...) ___bpf_ctx_cast5(args), (void *)ctx[5]
#define ___bpf_ctx_cast7(x, args...) ___bpf_ctx_cast6(args), (void *)ctx[6]
#define ___bpf_ctx_cast8(x, args...) ___bpf_ctx_cast7(args), (void *)ctx[7]
#define ___bpf_ctx_cast9(x, args...) ___bpf_ctx_cast8(args), (void *)ctx[8]
#define ___bpf_ctx_cast10(x, args...) ___bpf_ctx_cast9(args), (void *)ctx[9]
#define ___bpf_ctx_cast11(x, args...) ___bpf_ctx_cast10(args), (void *)ctx[10]
#define ___bpf_ctx_cast12(x, args...) ___bpf_ctx_cast11(args), (void *)ctx[11]
#define ___bpf_ctx_cast(args...) \
	___bpf_apply(___bpf_ctx_cast, ___bpf_narg(args))(args)

/*
 * BPF_PROG is a convenience wrapper for generic tp_btf/fentry/fexit and
 * similar kinds of BPF programs, that accept input arguments as a single
 * pointer to untyped u64 array, where each u64 can actually be a typed
 * pointer or integer of different size. Instead of requring user to write
 * manual casts and work with array elements by index, BPF_PROG macro
 * allows user to declare a list of named and typed input arguments in the
 * same syntax as for normal C function. All the casting is hidden and
 * performed transparently, while user code can just assume working with
 * function arguments of specified type and name.
 *
 * Original raw context argument is preserved as well as 'ctx' argument.
 * This is useful when using BPF helpers that expect original context
 * as one of the parameters (e.g., for bpf_perf_event_output()).
 */
#define BPF_PROG(name, args...)						    \
name(unsigned long long *ctx);						    \
static __attribute__((always_inline)) typeof(name(0))			    \
____##name(unsigned long long *ctx, ##args);				    \
typeof(name(0)) name(unsigned long long *ctx)				    \
{									    \
	_Pragma("GCC diagnostic push")					    \
	_Pragma("GCC diagnostic ignored \"-Wint-conversion\"")		    \
	return ____##name(___bpf_ctx_cast(args));			    \
	_Pragma("GCC diagnostic pop")					    \
}									    \
static __attribute__((always_inline)) typeof(name(0))			    \
____##name(unsigned long long *ctx, ##args)

struct pt_regs;

#define ___bpf_kprobe_args0() ctx
#define ___bpf_kprobe_args1(x) \
	___bpf_kprobe_args0(), (void *)PT_REGS_PARM1(ctx)
#define ___bpf_kprobe_args2(x, args...) \
	___bpf_kprobe_args1(args), (void *)PT_REGS_PARM2(ctx)
#define ___bpf_kprobe_args3(x, args...) \
	___bpf_kprobe_args2(args), (void *)PT_REGS_PARM3(ctx)
#define ___bpf_kprobe_args4(x, args...) \
	___bpf_kprobe_args3(args), (void *)PT_REGS_PARM4(ctx)
#define ___bpf_kprobe_args5(x, args...) \
	___bpf_kprobe_args4(args), (void *)PT_REGS_PARM5(ctx)
#define ___bpf_kprobe_args(args...) \
	___bpf_apply(___bpf_kprobe_args, ___bpf_narg(args))(args)

/*
 * BPF_KPROBE serves the same purpose for kprobes as BPF_PROG for
 * tp_btf/fentry/fexit BPF programs. It hides the underlying platform-specific
 * low-level way of getting kprobe input arguments from struct pt_regs, and
 * provides a familiar typed and named function arguments syntax and
 * semantics of accessing kprobe input paremeters.
 *
 * Original struct pt_regs* context is preserved as 'ctx' argument. This might
 * be necessary when using BPF helpers like bpf_perf_event_output().
 */
#define BPF_KPROBE(name, args...)					    \
name(struct pt_regs *ctx);						    \
static __attribute__((always_inline)) typeof(name(0))			    \
____##name(struct pt_regs *ctx, ##args);				    \
typeof(name(0)) name(struct pt_regs *ctx)				    \
{									    \
	_Pragma("GCC diagnostic push")					    \
	_Pragma("GCC diagnostic ignored \"-Wint-conversion\"")		    \
	return ____##name(___bpf_kprobe_args(args));			    \
	_Pragma("GCC diagnostic pop")					    \
}									    \
static __attribute__((always_inline)) typeof(name(0))			    \
____##name(struct pt_regs *ctx, ##args)

#define ___bpf_kretprobe_args0() ctx
#define ___bpf_kretprobe_args1(x) \
	___bpf_kretprobe_args0(), (void *)PT_REGS_RC(ctx)
#define ___bpf_kretprobe_args(args...) \
	___bpf_apply(___bpf_kretprobe_args, ___bpf_narg(args))(args)

/*
 * BPF_KRETPROBE is similar to BPF_KPROBE, except, it only provides optional
 * return value (in addition to `struct pt_regs *ctx`), but no input
 * arguments, because they will be clobbered by the time probed function
 * returns.
 */
#define BPF_KRETPROBE(name, args...)					    \
name(struct pt_regs *ctx);						    \
static __attribute__((always_inline)) typeof(name(0))			    \
____##name(struct pt_regs *ctx, ##args);				    \
typeof(name(0)) name(struct pt_regs *ctx)				    \
{									    \
	_Pragma("GCC diagnostic push")					    \
	_Pragma("GCC diagnostic ignored \"-Wint-conversion\"")		    \
	return ____##name(___bpf_kretprobe_args(args));			    \
	_Pragma("GCC diagnostic pop")					    \
}									    \
static __always_inline typeof(name(0)) ____##name(struct pt_regs *ctx, ##args)

#define ___bpf_fill0(arr, p, x) do {} while (0)
#define ___bpf_fill1(arr, p, x) arr[p] = x
#define ___bpf_fill2(arr, p, x, args...) arr[p] = x; ___bpf_fill1(arr, p + 1, args)
#define ___bpf_fill3(arr, p, x, args...) arr[p] = x; ___bpf_fill2(arr, p + 1, args)
#define ___bpf_fill4(arr, p, x, args...) arr[p] = x; ___bpf_fill3(arr, p + 1, args)
#define ___bpf_fill5(arr, p, x, args...) arr[p] = x; ___bpf_fill4(arr, p + 1, args)
#define ___bpf_fill6(arr, p, x, args...) arr[p] = x; ___bpf_fill5(arr, p + 1, args)
#define ___bpf_fill7(arr, p, x, args...) arr[p] = x; ___bpf_fill6(arr, p + 1, args)
#define ___bpf_fill8(arr, p, x, args...) arr[p] = x; ___bpf_fill7(arr, p + 1, args)
#define ___bpf_fill9(arr, p, x, args...) arr[p] = x; ___bpf_fill8(arr, p + 1, args)
#define ___bpf_fill10(arr, p, x, args...) arr[p] = x; ___bpf_fill9(arr, p + 1, args)
#define ___bpf_fill11(arr, p, x, args...) arr[p] = x; ___bpf_fill10(arr, p + 1, args)
#define ___bpf_fill12(arr, p, x, args...) arr[p] = x; ___bpf_fill11(arr, p + 1, args)
#define ___bpf_fill(arr, args...) \
	___bpf_apply(___bpf_fill, ___bpf_narg(args))(arr, 0, args)

/*
 * BPF_SEQ_PRINTF to wrap bpf_seq_printf to-be-printed values
 * in a structure.
 */
#define BPF_SEQ_PRINTF(seq, fmt, args...)			\
({								\
	static const char ___fmt[] = fmt;			\
	unsigned long long ___param[___bpf_narg(args)];		\
								\
	_Pragma("GCC diagnostic push")				\
	_Pragma("GCC diagnostic ignored \"-Wint-conversion\"")	\
	___bpf_fill(___param, args);				\
	_Pragma("GCC diagnostic pop")				\
								\
	bpf_seq_printf(seq, ___fmt, sizeof(___fmt),		\
		       ___param, sizeof(___param));		\
})

#endif
