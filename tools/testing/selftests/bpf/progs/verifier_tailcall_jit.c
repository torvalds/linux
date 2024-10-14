// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

int main(void);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table SEC(".maps") = {
	.values = {
		[0] = (void *) &main,
	},
};

__noinline __auxiliary
static __naked int sub(void)
{
	asm volatile (
	"r2 = %[jmp_table] ll;"
	"r3 = 0;"
	"call 12;"
	"exit;"
	:
	: __imm_addr(jmp_table)
	: __clobber_all);
}

__success
__arch_x86_64
/* program entry for main(), regular function prologue */
__jited("	endbr64")
__jited("	nopl	(%rax,%rax)")
__jited("	xorq	%rax, %rax")
__jited("	pushq	%rbp")
__jited("	movq	%rsp, %rbp")
/* tail call prologue for program:
 * - establish memory location for tail call counter at &rbp[-8];
 * - spill tail_call_cnt_ptr at &rbp[-16];
 * - expect tail call counter to be passed in rax;
 * - for entry program rax is a raw counter, value < 33;
 * - for tail called program rax is tail_call_cnt_ptr (value > 33).
 */
__jited("	endbr64")
__jited("	cmpq	$0x21, %rax")
__jited("	ja	L0")
__jited("	pushq	%rax")
__jited("	movq	%rsp, %rax")
__jited("	jmp	L1")
__jited("L0:	pushq	%rax")			/* rbp[-8]  = rax         */
__jited("L1:	pushq	%rax")			/* rbp[-16] = rax         */
/* on subprogram call restore rax to be tail_call_cnt_ptr from rbp[-16]
 * (cause original rax might be clobbered by this point)
 */
__jited("	movq	-0x10(%rbp), %rax")
__jited("	callq	0x{{.*}}")		/* call to sub()          */
__jited("	xorl	%eax, %eax")
__jited("	leave")
__jited("	{{(retq|jmp	0x)}}")		/* return or jump to rethunk */
__jited("...")
/* subprogram entry for sub(), regular function prologue */
__jited("	endbr64")
__jited("	nopl	(%rax,%rax)")
__jited("	nopl	(%rax)")
__jited("	pushq	%rbp")
__jited("	movq	%rsp, %rbp")
/* tail call prologue for subprogram address of tail call counter
 * stored at rbp[-16].
 */
__jited("	endbr64")
__jited("	pushq	%rax")			/* rbp[-8]  = rax          */
__jited("	pushq	%rax")			/* rbp[-16] = rax          */
__jited("	movabsq	${{.*}}, %rsi")		/* r2 = &jmp_table         */
__jited("	xorl	%edx, %edx")		/* r3 = 0                  */
/* bpf_tail_call implementation:
 * - load tail_call_cnt_ptr from rbp[-16];
 * - if *tail_call_cnt_ptr < 33, increment it and jump to target;
 * - otherwise do nothing.
 */
__jited("	movq	-0x10(%rbp), %rax")
__jited("	cmpq	$0x21, (%rax)")
__jited("	jae	L0")
__jited("	nopl	(%rax,%rax)")
__jited("	addq	$0x1, (%rax)")		/* *tail_call_cnt_ptr += 1 */
__jited("	popq	%rax")
__jited("	popq	%rax")
__jited("	jmp	{{.*}}")		/* jump to tail call tgt   */
__jited("L0:	leave")
__jited("	{{(retq|jmp	0x)}}")		/* return or jump to rethunk */
SEC("tc")
__naked int main(void)
{
	asm volatile (
	"call %[sub];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(sub)
	: __clobber_all);
}

char __license[] SEC("license") = "GPL";
