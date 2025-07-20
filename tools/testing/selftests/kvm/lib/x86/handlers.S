handle_exception:
	push %r15
	push %r14
	push %r13
	push %r12
	push %r11
	push %r10
	push %r9
	push %r8

	push %rdi
	push %rsi
	push %rbp
	push %rbx
	push %rdx
	push %rcx
	push %rax
	mov %rsp, %rdi

	call route_exception

	pop %rax
	pop %rcx
	pop %rdx
	pop %rbx
	pop %rbp
	pop %rsi
	pop %rdi
	pop %r8
	pop %r9
	pop %r10
	pop %r11
	pop %r12
	pop %r13
	pop %r14
	pop %r15

	/* Discard vector and error code. */
	add $16, %rsp
	iretq

/*
 * Build the handle_exception wrappers which push the vector/error code on the
 * stack and an array of pointers to those wrappers.
 */
.pushsection .rodata
.globl idt_handlers
idt_handlers:
.popsection

.macro HANDLERS has_error from to
	vector = \from
	.rept \to - \from + 1
	.align 8

	/* Fetch current address and append it to idt_handlers. */
666 :
.pushsection .rodata
	.quad 666b
.popsection

	.if ! \has_error
	pushq $0
	.endif
	pushq $vector
	jmp handle_exception
	vector = vector + 1
	.endr
.endm

.global idt_handler_code
idt_handler_code:
	HANDLERS has_error=0 from=0  to=7
	HANDLERS has_error=1 from=8  to=8
	HANDLERS has_error=0 from=9  to=9
	HANDLERS has_error=1 from=10 to=14
	HANDLERS has_error=0 from=15 to=16
	HANDLERS has_error=1 from=17 to=17
	HANDLERS has_error=0 from=18 to=255

.section        .note.GNU-stack, "", %progbits
