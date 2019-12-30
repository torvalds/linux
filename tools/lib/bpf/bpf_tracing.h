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

#ifdef __KERNEL__
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

#elif defined(bpf_target_mips)

#define PT_REGS_PARM1(x) ((x)->regs[4])
#define PT_REGS_PARM2(x) ((x)->regs[5])
#define PT_REGS_PARM3(x) ((x)->regs[6])
#define PT_REGS_PARM4(x) ((x)->regs[7])
#define PT_REGS_PARM5(x) ((x)->regs[8])
#define PT_REGS_RET(x) ((x)->regs[31])
#define PT_REGS_FP(x) ((x)->regs[30]) /* Works only with CONFIG_FRAME_POINTER */
#define PT_REGS_RC(x) ((x)->regs[1])
#define PT_REGS_SP(x) ((x)->regs[29])
#define PT_REGS_IP(x) ((x)->cp0_epc)

#elif defined(bpf_target_powerpc)

#define PT_REGS_PARM1(x) ((x)->gpr[3])
#define PT_REGS_PARM2(x) ((x)->gpr[4])
#define PT_REGS_PARM3(x) ((x)->gpr[5])
#define PT_REGS_PARM4(x) ((x)->gpr[6])
#define PT_REGS_PARM5(x) ((x)->gpr[7])
#define PT_REGS_RC(x) ((x)->gpr[3])
#define PT_REGS_SP(x) ((x)->sp)
#define PT_REGS_IP(x) ((x)->nip)

#elif defined(bpf_target_sparc)

#define PT_REGS_PARM1(x) ((x)->u_regs[UREG_I0])
#define PT_REGS_PARM2(x) ((x)->u_regs[UREG_I1])
#define PT_REGS_PARM3(x) ((x)->u_regs[UREG_I2])
#define PT_REGS_PARM4(x) ((x)->u_regs[UREG_I3])
#define PT_REGS_PARM5(x) ((x)->u_regs[UREG_I4])
#define PT_REGS_RET(x) ((x)->u_regs[UREG_I7])
#define PT_REGS_RC(x) ((x)->u_regs[UREG_I0])
#define PT_REGS_SP(x) ((x)->u_regs[UREG_FP])

/* Should this also be a bpf_target check for the sparc case? */
#if defined(__arch64__)
#define PT_REGS_IP(x) ((x)->tpc)
#else
#define PT_REGS_IP(x) ((x)->pc)
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
	({ bpf_probe_read(&(ip), sizeof(ip), (void *)PT_REGS_RET(ctx)); })
#define BPF_KRETPROBE_READ_RET_IP(ip, ctx)				    \
	({ bpf_probe_read(&(ip), sizeof(ip),				    \
			  (void *)(PT_REGS_FP(ctx) + sizeof(ip))); })
#endif

#endif
