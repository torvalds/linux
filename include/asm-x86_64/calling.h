/* 
 * Some macros to handle stack frames in assembly.
 */ 


#define R15 0
#define R14 8
#define R13 16
#define R12 24
#define RBP 32
#define RBX 40
/* arguments: interrupts/non tracing syscalls only save upto here*/
#define R11 48
#define R10 56	
#define R9 64
#define R8 72
#define RAX 80
#define RCX 88
#define RDX 96
#define RSI 104
#define RDI 112
#define ORIG_RAX 120       /* + error_code */ 
/* end of arguments */ 	
/* cpu exception frame or undefined in case of fast syscall. */
#define RIP 128
#define CS 136
#define EFLAGS 144
#define RSP 152
#define SS 160
#define ARGOFFSET R11
#define SWFRAME ORIG_RAX

	.macro SAVE_ARGS addskip=0,norcx=0,nor891011=0
	subq  $9*8+\addskip,%rsp
	CFI_ADJUST_CFA_OFFSET	9*8+\addskip
	movq  %rdi,8*8(%rsp) 
	CFI_REL_OFFSET	rdi,8*8
	movq  %rsi,7*8(%rsp) 
	CFI_REL_OFFSET	rsi,7*8
	movq  %rdx,6*8(%rsp)
	CFI_REL_OFFSET	rdx,6*8
	.if \norcx
	.else
	movq  %rcx,5*8(%rsp)
	CFI_REL_OFFSET	rcx,5*8
	.endif
	movq  %rax,4*8(%rsp) 
	CFI_REL_OFFSET	rax,4*8
	.if \nor891011
	.else
	movq  %r8,3*8(%rsp) 
	CFI_REL_OFFSET	r8,3*8
	movq  %r9,2*8(%rsp) 
	CFI_REL_OFFSET	r9,2*8
	movq  %r10,1*8(%rsp) 
	CFI_REL_OFFSET	r10,1*8
	movq  %r11,(%rsp) 
	CFI_REL_OFFSET	r11,0*8
	.endif
	.endm

#define ARG_SKIP 9*8
	.macro RESTORE_ARGS skiprax=0,addskip=0,skiprcx=0,skipr11=0,skipr8910=0,skiprdx=0
	.if \skipr11
	.else
	movq (%rsp),%r11
	CFI_RESTORE r11
	.endif
	.if \skipr8910
	.else
	movq 1*8(%rsp),%r10
	CFI_RESTORE r10
	movq 2*8(%rsp),%r9
	CFI_RESTORE r9
	movq 3*8(%rsp),%r8
	CFI_RESTORE r8
	.endif
	.if \skiprax
	.else
	movq 4*8(%rsp),%rax
	CFI_RESTORE rax
	.endif
	.if \skiprcx
	.else
	movq 5*8(%rsp),%rcx
	CFI_RESTORE rcx
	.endif
	.if \skiprdx
	.else
	movq 6*8(%rsp),%rdx
	CFI_RESTORE rdx
	.endif
	movq 7*8(%rsp),%rsi
	CFI_RESTORE rsi
	movq 8*8(%rsp),%rdi
	CFI_RESTORE rdi
	.if ARG_SKIP+\addskip > 0
	addq $ARG_SKIP+\addskip,%rsp
	CFI_ADJUST_CFA_OFFSET	-(ARG_SKIP+\addskip)
	.endif
	.endm	

	.macro LOAD_ARGS offset
	movq \offset(%rsp),%r11
	movq \offset+8(%rsp),%r10
	movq \offset+16(%rsp),%r9
	movq \offset+24(%rsp),%r8
	movq \offset+40(%rsp),%rcx
	movq \offset+48(%rsp),%rdx
	movq \offset+56(%rsp),%rsi
	movq \offset+64(%rsp),%rdi
	movq \offset+72(%rsp),%rax
	.endm
			
#define REST_SKIP 6*8			
	.macro SAVE_REST
	subq $REST_SKIP,%rsp
	CFI_ADJUST_CFA_OFFSET	REST_SKIP
	movq %rbx,5*8(%rsp) 
	CFI_REL_OFFSET	rbx,5*8
	movq %rbp,4*8(%rsp) 
	CFI_REL_OFFSET	rbp,4*8
	movq %r12,3*8(%rsp) 
	CFI_REL_OFFSET	r12,3*8
	movq %r13,2*8(%rsp) 
	CFI_REL_OFFSET	r13,2*8
	movq %r14,1*8(%rsp) 
	CFI_REL_OFFSET	r14,1*8
	movq %r15,(%rsp) 
	CFI_REL_OFFSET	r15,0*8
	.endm		

	.macro RESTORE_REST
	movq (%rsp),%r15
	CFI_RESTORE r15
	movq 1*8(%rsp),%r14
	CFI_RESTORE r14
	movq 2*8(%rsp),%r13
	CFI_RESTORE r13
	movq 3*8(%rsp),%r12
	CFI_RESTORE r12
	movq 4*8(%rsp),%rbp
	CFI_RESTORE rbp
	movq 5*8(%rsp),%rbx
	CFI_RESTORE rbx
	addq $REST_SKIP,%rsp
	CFI_ADJUST_CFA_OFFSET	-(REST_SKIP)
	.endm
		
	.macro SAVE_ALL
	SAVE_ARGS
	SAVE_REST
	.endm
		
	.macro RESTORE_ALL addskip=0
	RESTORE_REST
	RESTORE_ARGS 0,\addskip
	.endm

	.macro icebp
	.byte 0xf1
	.endm
