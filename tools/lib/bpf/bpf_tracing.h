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
#elif defined(__TARGET_ARCH_riscv)
	#define bpf_target_riscv
	#define bpf_target_defined
#else

/* Fall back to what the compiler says */
#if defined(__x86_64__)
	#define bpf_target_x86
	#define bpf_target_defined
#elif defined(__s390__)
	#define bpf_target_s390
	#define bpf_target_defined
#elif defined(__arm__)
	#define bpf_target_arm
	#define bpf_target_defined
#elif defined(__aarch64__)
	#define bpf_target_arm64
	#define bpf_target_defined
#elif defined(__mips__)
	#define bpf_target_mips
	#define bpf_target_defined
#elif defined(__powerpc__)
	#define bpf_target_powerpc
	#define bpf_target_defined
#elif defined(__sparc__)
	#define bpf_target_sparc
	#define bpf_target_defined
#elif defined(__riscv) && __riscv_xlen == 64
	#define bpf_target_riscv
	#define bpf_target_defined
#endif /* no compiler target */

#endif

#ifndef __BPF_TARGET_MISSING
#define __BPF_TARGET_MISSING "GCC error \"Must specify a BPF target arch via __TARGET_ARCH_xxx\""
#endif

#if defined(bpf_target_x86)

#if defined(__KERNEL__) || defined(__VMLINUX_H__)

#define __PT_PARM1_REG di
#define __PT_PARM2_REG si
#define __PT_PARM3_REG dx
#define __PT_PARM4_REG cx
#define __PT_PARM5_REG r8
#define __PT_RET_REG sp
#define __PT_FP_REG bp
#define __PT_RC_REG ax
#define __PT_SP_REG sp
#define __PT_IP_REG ip

#else

#ifdef __i386__

#define __PT_PARM1_REG eax
#define __PT_PARM2_REG edx
#define __PT_PARM3_REG ecx
/* i386 kernel is built with -mregparm=3 */
#define __PT_PARM4_REG __unsupported__
#define __PT_PARM5_REG __unsupported__
#define __PT_RET_REG esp
#define __PT_FP_REG ebp
#define __PT_RC_REG eax
#define __PT_SP_REG esp
#define __PT_IP_REG eip

#else /* __i386__ */

#define __PT_PARM1_REG rdi
#define __PT_PARM2_REG rsi
#define __PT_PARM3_REG rdx
#define __PT_PARM4_REG rcx
#define __PT_PARM5_REG r8
#define __PT_RET_REG rsp
#define __PT_FP_REG rbp
#define __PT_RC_REG rax
#define __PT_SP_REG rsp
#define __PT_IP_REG rip

#endif /* __i386__ */

#endif /* __KERNEL__ || __VMLINUX_H__ */

#elif defined(bpf_target_s390)

/* s390 provides user_pt_regs instead of struct pt_regs to userspace */
#define __PT_REGS_CAST(x) ((const user_pt_regs *)(x))
#define __PT_PARM1_REG gprs[2]
#define __PT_PARM2_REG gprs[3]
#define __PT_PARM3_REG gprs[4]
#define __PT_PARM4_REG gprs[5]
#define __PT_PARM5_REG gprs[6]
#define __PT_RET_REG grps[14]
#define __PT_FP_REG gprs[11]	/* Works only with CONFIG_FRAME_POINTER */
#define __PT_RC_REG gprs[2]
#define __PT_SP_REG gprs[15]
#define __PT_IP_REG psw.addr

#elif defined(bpf_target_arm)

#define __PT_PARM1_REG uregs[0]
#define __PT_PARM2_REG uregs[1]
#define __PT_PARM3_REG uregs[2]
#define __PT_PARM4_REG uregs[3]
#define __PT_PARM5_REG uregs[4]
#define __PT_RET_REG uregs[14]
#define __PT_FP_REG uregs[11]	/* Works only with CONFIG_FRAME_POINTER */
#define __PT_RC_REG uregs[0]
#define __PT_SP_REG uregs[13]
#define __PT_IP_REG uregs[12]

#elif defined(bpf_target_arm64)

/* arm64 provides struct user_pt_regs instead of struct pt_regs to userspace */
#define __PT_REGS_CAST(x) ((const struct user_pt_regs *)(x))
#define __PT_PARM1_REG regs[0]
#define __PT_PARM2_REG regs[1]
#define __PT_PARM3_REG regs[2]
#define __PT_PARM4_REG regs[3]
#define __PT_PARM5_REG regs[4]
#define __PT_RET_REG regs[30]
#define __PT_FP_REG regs[29]	/* Works only with CONFIG_FRAME_POINTER */
#define __PT_RC_REG regs[0]
#define __PT_SP_REG sp
#define __PT_IP_REG pc

#elif defined(bpf_target_mips)

#define __PT_PARM1_REG regs[4]
#define __PT_PARM2_REG regs[5]
#define __PT_PARM3_REG regs[6]
#define __PT_PARM4_REG regs[7]
#define __PT_PARM5_REG regs[8]
#define __PT_RET_REG regs[31]
#define __PT_FP_REG regs[30]	/* Works only with CONFIG_FRAME_POINTER */
#define __PT_RC_REG regs[2]
#define __PT_SP_REG regs[29]
#define __PT_IP_REG cp0_epc

#elif defined(bpf_target_powerpc)

#define __PT_PARM1_REG gpr[3]
#define __PT_PARM2_REG gpr[4]
#define __PT_PARM3_REG gpr[5]
#define __PT_PARM4_REG gpr[6]
#define __PT_PARM5_REG gpr[7]
#define __PT_RET_REG regs[31]
#define __PT_FP_REG __unsupported__
#define __PT_RC_REG gpr[3]
#define __PT_SP_REG sp
#define __PT_IP_REG nip

#elif defined(bpf_target_sparc)

#define __PT_PARM1_REG u_regs[UREG_I0]
#define __PT_PARM2_REG u_regs[UREG_I1]
#define __PT_PARM3_REG u_regs[UREG_I2]
#define __PT_PARM4_REG u_regs[UREG_I3]
#define __PT_PARM5_REG u_regs[UREG_I4]
#define __PT_RET_REG u_regs[UREG_I7]
#define __PT_FP_REG __unsupported__
#define __PT_RC_REG u_regs[UREG_I0]
#define __PT_SP_REG u_regs[UREG_FP]
/* Should this also be a bpf_target check for the sparc case? */
#if defined(__arch64__)
#define __PT_IP_REG tpc
#else
#define __PT_IP_REG pc
#endif

#elif defined(bpf_target_riscv)

#define __PT_REGS_CAST(x) ((const struct user_regs_struct *)(x))
#define __PT_PARM1_REG a0
#define __PT_PARM2_REG a1
#define __PT_PARM3_REG a2
#define __PT_PARM4_REG a3
#define __PT_PARM5_REG a4
#define __PT_RET_REG ra
#define __PT_FP_REG fp
#define __PT_RC_REG a5
#define __PT_SP_REG sp
#define __PT_IP_REG epc

#endif

#if defined(bpf_target_defined)

struct pt_regs;

/* allow some architecutres to override `struct pt_regs` */
#ifndef __PT_REGS_CAST
#define __PT_REGS_CAST(x) (x)
#endif

#define PT_REGS_PARM1(x) (__PT_REGS_CAST(x)->__PT_PARM1_REG)
#define PT_REGS_PARM2(x) (__PT_REGS_CAST(x)->__PT_PARM2_REG)
#define PT_REGS_PARM3(x) (__PT_REGS_CAST(x)->__PT_PARM3_REG)
#define PT_REGS_PARM4(x) (__PT_REGS_CAST(x)->__PT_PARM4_REG)
#define PT_REGS_PARM5(x) (__PT_REGS_CAST(x)->__PT_PARM5_REG)
#define PT_REGS_RET(x) (__PT_REGS_CAST(x)->__PT_RET_REG)
#define PT_REGS_FP(x) (__PT_REGS_CAST(x)->__PT_FP_REG)
#define PT_REGS_RC(x) (__PT_REGS_CAST(x)->__PT_RC_REG)
#define PT_REGS_SP(x) (__PT_REGS_CAST(x)->__PT_SP_REG)
#define PT_REGS_IP(x) (__PT_REGS_CAST(x)->__PT_IP_REG)

#define PT_REGS_PARM1_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_PARM1_REG)
#define PT_REGS_PARM2_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_PARM2_REG)
#define PT_REGS_PARM3_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_PARM3_REG)
#define PT_REGS_PARM4_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_PARM4_REG)
#define PT_REGS_PARM5_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_PARM5_REG)
#define PT_REGS_RET_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_RET_REG)
#define PT_REGS_FP_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_FP_REG)
#define PT_REGS_RC_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_RC_REG)
#define PT_REGS_SP_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_SP_REG)
#define PT_REGS_IP_CORE(x) BPF_CORE_READ(__PT_REGS_CAST(x), __PT_IP_REG)

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
	({ bpf_probe_read_kernel(&(ip), sizeof(ip), (void *)(PT_REGS_FP(ctx) + sizeof(ip))); })

#endif

#else /* defined(bpf_target_defined) */

#define PT_REGS_PARM1(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM2(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM3(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM4(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM5(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_RET(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_FP(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_RC(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_SP(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_IP(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })

#define PT_REGS_PARM1_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM2_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM3_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM4_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_PARM5_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_RET_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_FP_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_RC_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_SP_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define PT_REGS_IP_CORE(x) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })

#define BPF_KPROBE_READ_RET_IP(ip, ctx) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })
#define BPF_KRETPROBE_READ_RET_IP(ip, ctx) ({ _Pragma(__BPF_TARGET_MISSING); 0l; })

#endif /* defined(bpf_target_defined) */

#ifndef ___bpf_concat
#define ___bpf_concat(a, b) a ## b
#endif
#ifndef ___bpf_apply
#define ___bpf_apply(fn, n) ___bpf_concat(fn, n)
#endif
#ifndef ___bpf_nth
#define ___bpf_nth(_, _1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, N, ...) N
#endif
#ifndef ___bpf_narg
#define ___bpf_narg(...) ___bpf_nth(_, ##__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#endif

#define ___bpf_ctx_cast0()            ctx
#define ___bpf_ctx_cast1(x)           ___bpf_ctx_cast0(), (void *)ctx[0]
#define ___bpf_ctx_cast2(x, args...)  ___bpf_ctx_cast1(args), (void *)ctx[1]
#define ___bpf_ctx_cast3(x, args...)  ___bpf_ctx_cast2(args), (void *)ctx[2]
#define ___bpf_ctx_cast4(x, args...)  ___bpf_ctx_cast3(args), (void *)ctx[3]
#define ___bpf_ctx_cast5(x, args...)  ___bpf_ctx_cast4(args), (void *)ctx[4]
#define ___bpf_ctx_cast6(x, args...)  ___bpf_ctx_cast5(args), (void *)ctx[5]
#define ___bpf_ctx_cast7(x, args...)  ___bpf_ctx_cast6(args), (void *)ctx[6]
#define ___bpf_ctx_cast8(x, args...)  ___bpf_ctx_cast7(args), (void *)ctx[7]
#define ___bpf_ctx_cast9(x, args...)  ___bpf_ctx_cast8(args), (void *)ctx[8]
#define ___bpf_ctx_cast10(x, args...) ___bpf_ctx_cast9(args), (void *)ctx[9]
#define ___bpf_ctx_cast11(x, args...) ___bpf_ctx_cast10(args), (void *)ctx[10]
#define ___bpf_ctx_cast12(x, args...) ___bpf_ctx_cast11(args), (void *)ctx[11]
#define ___bpf_ctx_cast(args...)      ___bpf_apply(___bpf_ctx_cast, ___bpf_narg(args))(args)

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

#define ___bpf_kprobe_args0()           ctx
#define ___bpf_kprobe_args1(x)          ___bpf_kprobe_args0(), (void *)PT_REGS_PARM1(ctx)
#define ___bpf_kprobe_args2(x, args...) ___bpf_kprobe_args1(args), (void *)PT_REGS_PARM2(ctx)
#define ___bpf_kprobe_args3(x, args...) ___bpf_kprobe_args2(args), (void *)PT_REGS_PARM3(ctx)
#define ___bpf_kprobe_args4(x, args...) ___bpf_kprobe_args3(args), (void *)PT_REGS_PARM4(ctx)
#define ___bpf_kprobe_args5(x, args...) ___bpf_kprobe_args4(args), (void *)PT_REGS_PARM5(ctx)
#define ___bpf_kprobe_args(args...)     ___bpf_apply(___bpf_kprobe_args, ___bpf_narg(args))(args)

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

#define ___bpf_kretprobe_args0()       ctx
#define ___bpf_kretprobe_args1(x)      ___bpf_kretprobe_args0(), (void *)PT_REGS_RC(ctx)
#define ___bpf_kretprobe_args(args...) ___bpf_apply(___bpf_kretprobe_args, ___bpf_narg(args))(args)

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

#endif
