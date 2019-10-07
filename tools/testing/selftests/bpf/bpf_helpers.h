/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_HELPERS__
#define __BPF_HELPERS__

#include "bpf_helper_defs.h"

#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

/* helper macro to print out debug messages */
#define bpf_printk(fmt, ...)				\
({							\
	char ____fmt[] = fmt;				\
	bpf_trace_printk(____fmt, sizeof(____fmt),	\
			 ##__VA_ARGS__);		\
})

#ifdef __clang__

/* helper macro to place programs, maps, license in
 * different sections in elf_bpf file. Section names
 * are interpreted by elf_bpf loader
 */
#define SEC(NAME) __attribute__((section(NAME), used))

/* llvm builtin functions that eBPF C program may use to
 * emit BPF_LD_ABS and BPF_LD_IND instructions
 */
struct sk_buff;
unsigned long long load_byte(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.byte");
unsigned long long load_half(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.half");
unsigned long long load_word(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.word");

/* a helper structure used by eBPF C program
 * to describe map attributes to elf_bpf loader
 */
struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
	unsigned int inner_map_idx;
	unsigned int numa_node;
};

#else

#include <bpf-helpers.h>

#endif

#define BPF_ANNOTATE_KV_PAIR(name, type_key, type_val)		\
	struct ____btf_map_##name {				\
		type_key key;					\
		type_val value;					\
	};							\
	struct ____btf_map_##name				\
	__attribute__ ((section(".maps." #name), used))		\
		____btf_map_##name = { }

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
#define BPF_KPROBE_READ_RET_IP(ip, ctx)		({				\
		bpf_probe_read(&(ip), sizeof(ip), (void *)PT_REGS_RET(ctx)); })
#define BPF_KRETPROBE_READ_RET_IP(ip, ctx)	({				\
		bpf_probe_read(&(ip), sizeof(ip),				\
				(void *)(PT_REGS_FP(ctx) + sizeof(ip))); })
#endif

/*
 * BPF_CORE_READ abstracts away bpf_probe_read() call and captures offset
 * relocation for source address using __builtin_preserve_access_index()
 * built-in, provided by Clang.
 *
 * __builtin_preserve_access_index() takes as an argument an expression of
 * taking an address of a field within struct/union. It makes compiler emit
 * a relocation, which records BTF type ID describing root struct/union and an
 * accessor string which describes exact embedded field that was used to take
 * an address. See detailed description of this relocation format and
 * semantics in comments to struct bpf_offset_reloc in libbpf_internal.h.
 *
 * This relocation allows libbpf to adjust BPF instruction to use correct
 * actual field offset, based on target kernel BTF type that matches original
 * (local) BTF, used to record relocation.
 */
#define BPF_CORE_READ(dst, src)						\
	bpf_probe_read((dst), sizeof(*(src)),				\
		       __builtin_preserve_access_index(src))

#endif
