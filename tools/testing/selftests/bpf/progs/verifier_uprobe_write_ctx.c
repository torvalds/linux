// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/*
 * A uprobe prog can write only the registers that ptrace can set to any
 * value. The fields the kernel restores on the way to user space (segments,
 * flags, privilege state) are read-only.
 */

#define WRITE_CTX(name, field, sz)					\
__naked void name(void)							\
{									\
	asm volatile ("							\
	r2 = 0;								\
	*(" #sz " *)(r1 + %[off]) = r2;					\
	r0 = 0;								\
	exit;								\
"	:								\
	: __imm_const(off, offsetof(struct pt_regs, field))		\
	: __clobber_all);						\
}

#define READ_CTX(name, field)						\
__naked void name(void)							\
{									\
	asm volatile ("							\
	r0 = *(u64 *)(r1 + %[off]);					\
	r0 = 0;								\
	exit;								\
"	:								\
	: __imm_const(off, offsetof(struct pt_regs, field))		\
	: __clobber_all);						\
}

#if defined(__TARGET_ARCH_x86)

SEC("uprobe")
__description("uprobe: write ctx->ax")
__success
WRITE_CTX(write_ax, ax, u64)

SEC("uprobe")
__description("uprobe: write ctx->r15")
__success
WRITE_CTX(write_r15, r15, u64)

SEC("uprobe")
__description("uprobe: write ctx->di")
__success
WRITE_CTX(write_di, di, u64)

SEC("uprobe")
__description("uprobe: write ctx->ip")
__success
WRITE_CTX(write_ip, ip, u64)

SEC("uprobe")
__description("uprobe: write ctx->sp")
__success
WRITE_CTX(write_sp, sp, u64)

SEC("uprobe")
__description("uprobe: write lower half of ctx->ax")
__success
WRITE_CTX(write_ax_u32, ax, u32)

SEC("uprobe")
__description("uprobe: write ctx->orig_ax")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_orig_ax, orig_ax, u64)

SEC("uprobe")
__description("uprobe: write ctx->cs")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_cs, cs, u64)

SEC("uprobe")
__description("uprobe: write lower half of ctx->cs")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_cs_u32, cs, u32)

SEC("uprobe")
__description("uprobe: write ctx->flags")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_flags, flags, u64)

SEC("uprobe")
__description("uprobe: write ctx->ss")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_ss, ss, u64)

SEC("uprobe")
__description("uprobe: read ctx->flags")
__success
READ_CTX(read_flags, flags)

SEC("kprobe")
__description("kprobe: write ctx->flags")
__failure __msg("invalid bpf_context access")
WRITE_CTX(kprobe_write_flags, flags, u64)

#elif defined(__TARGET_ARCH_arm64)

SEC("uprobe")
__description("uprobe: write ctx->regs[0]")
__success
WRITE_CTX(write_x0, regs[0], u64)

SEC("uprobe")
__description("uprobe: write ctx->regs[30]")
__success
WRITE_CTX(write_x30, regs[30], u64)

SEC("uprobe")
__description("uprobe: write ctx->sp")
__success
WRITE_CTX(write_sp, sp, u64)

SEC("uprobe")
__description("uprobe: write ctx->pc")
__success
WRITE_CTX(write_pc, pc, u64)

SEC("uprobe")
__description("uprobe: write ctx->pstate")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_pstate, pstate, u64)

SEC("uprobe")
__description("uprobe: write lower half of ctx->pstate")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_pstate_u32, pstate, u32)

SEC("uprobe")
__description("uprobe: write ctx->orig_x0")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_orig_x0, orig_x0, u64)

SEC("uprobe")
__description("uprobe: read ctx->pstate")
__success
READ_CTX(read_pstate, pstate)

#elif defined(__TARGET_ARCH_riscv)

SEC("uprobe")
__description("uprobe: write ctx->epc")
__success
WRITE_CTX(write_epc, epc, u64)

SEC("uprobe")
__description("uprobe: write ctx->a0")
__success
WRITE_CTX(write_a0, a0, u64)

SEC("uprobe")
__description("uprobe: write ctx->t6")
__success
WRITE_CTX(write_t6, t6, u64)

SEC("uprobe")
__description("uprobe: write ctx->status")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_status, status, u64)

SEC("uprobe")
__description("uprobe: write ctx->badaddr")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_badaddr, badaddr, u64)

SEC("uprobe")
__description("uprobe: write ctx->cause")
__failure __msg("invalid bpf_context access")
WRITE_CTX(write_cause, cause, u64)

SEC("uprobe")
__description("uprobe: read ctx->status")
__success
READ_CTX(read_status, status)

#else

SEC("uprobe")
__description("uprobe: ctx writes not checked on this arch")
__success
__naked void unsupported(void)
{
	asm volatile ("r0 = 0; exit;" ::: __clobber_all);
}

#endif

char _license[] SEC("license") = "GPL";
