/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef __ASSEMBLER__
# define __ASM_FORM(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_RAW(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_COMMA(x, ...)	x,## __VA_ARGS__,
#else
#include <linux/stringify.h>
# define __ASM_FORM(x, ...)		" " __stringify(x,##__VA_ARGS__) " "
# define __ASM_FORM_RAW(x, ...)		    __stringify(x,##__VA_ARGS__)
# define __ASM_FORM_COMMA(x, ...)	" " __stringify(x,##__VA_ARGS__) ","
#endif

#define _ASM_BYTES(x, ...)	__ASM_FORM(.byte x,##__VA_ARGS__ ;)

#ifndef __x86_64__
/* 32 bit */
# define __ASM_SEL(a,b)		__ASM_FORM(a)
# define __ASM_SEL_RAW(a,b)	__ASM_FORM_RAW(a)
#else
/* 64 bit */
# define __ASM_SEL(a,b)		__ASM_FORM(b)
# define __ASM_SEL_RAW(a,b)	__ASM_FORM_RAW(b)
#endif

#define __ASM_SIZE(inst, ...)	__ASM_SEL(inst##l##__VA_ARGS__, \
					  inst##q##__VA_ARGS__)
#define __ASM_REG(reg)         __ASM_SEL_RAW(e##reg, r##reg)

#define _ASM_PTR	__ASM_SEL(.long, .quad)
#define _ASM_ALIGN	__ASM_SEL(.balign 4, .balign 8)

#define _ASM_MOV	__ASM_SIZE(mov)
#define _ASM_INC	__ASM_SIZE(inc)
#define _ASM_DEC	__ASM_SIZE(dec)
#define _ASM_ADD	__ASM_SIZE(add)
#define _ASM_SUB	__ASM_SIZE(sub)
#define _ASM_XADD	__ASM_SIZE(xadd)
#define _ASM_MUL	__ASM_SIZE(mul)

#define _ASM_AX		__ASM_REG(ax)
#define _ASM_BX		__ASM_REG(bx)
#define _ASM_CX		__ASM_REG(cx)
#define _ASM_DX		__ASM_REG(dx)
#define _ASM_SP		__ASM_REG(sp)
#define _ASM_BP		__ASM_REG(bp)
#define _ASM_SI		__ASM_REG(si)
#define _ASM_DI		__ASM_REG(di)

#ifndef __x86_64__
/* 32 bit */

#define _ASM_ARG1	_ASM_AX
#define _ASM_ARG2	_ASM_DX
#define _ASM_ARG3	_ASM_CX

#define _ASM_ARG1L	eax
#define _ASM_ARG2L	edx
#define _ASM_ARG3L	ecx

#define _ASM_ARG1W	ax
#define _ASM_ARG2W	dx
#define _ASM_ARG3W	cx

#define _ASM_ARG1B	al
#define _ASM_ARG2B	dl
#define _ASM_ARG3B	cl

#else
/* 64 bit */

#define _ASM_ARG1	_ASM_DI
#define _ASM_ARG2	_ASM_SI
#define _ASM_ARG3	_ASM_DX
#define _ASM_ARG4	_ASM_CX
#define _ASM_ARG5	r8
#define _ASM_ARG6	r9

#define _ASM_ARG1Q	rdi
#define _ASM_ARG2Q	rsi
#define _ASM_ARG3Q	rdx
#define _ASM_ARG4Q	rcx
#define _ASM_ARG5Q	r8
#define _ASM_ARG6Q	r9

#define _ASM_ARG1L	edi
#define _ASM_ARG2L	esi
#define _ASM_ARG3L	edx
#define _ASM_ARG4L	ecx
#define _ASM_ARG5L	r8d
#define _ASM_ARG6L	r9d

#define _ASM_ARG1W	di
#define _ASM_ARG2W	si
#define _ASM_ARG3W	dx
#define _ASM_ARG4W	cx
#define _ASM_ARG5W	r8w
#define _ASM_ARG6W	r9w

#define _ASM_ARG1B	dil
#define _ASM_ARG2B	sil
#define _ASM_ARG3B	dl
#define _ASM_ARG4B	cl
#define _ASM_ARG5B	r8b
#define _ASM_ARG6B	r9b

#endif

#ifdef __KERNEL__

/* Exception table entry */
#ifdef __ASSEMBLER__
# define _ASM_EXTABLE_HANDLE(from, to, handler)			\
	.pushsection "__ex_table","a" ;				\
	.balign 4 ;						\
	.long (from) - . ;					\
	.long (to) - . ;					\
	.long (handler) - . ;					\
	.popsection

# define _ASM_EXTABLE(from, to)					\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_default)

# define _ASM_EXTABLE_UA(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_uaccess)

# define _ASM_EXTABLE_CPY(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_copy)

# define _ASM_EXTABLE_FAULT(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_fault)

# ifdef CONFIG_KPROBES
#  define _ASM_NOKPROBE(entry)					\
	.pushsection "_kprobe_blacklist","aw" ;			\
	_ASM_ALIGN ;						\
	_ASM_PTR (entry);					\
	.popsection
# else
#  define _ASM_NOKPROBE(entry)
# endif

#else /* ! __ASSEMBLER__ */
# define _EXPAND_EXTABLE_HANDLE(x) #x
# define _ASM_EXTABLE_HANDLE(from, to, handler)			\
	" .pushsection \"__ex_table\",\"a\"\n"			\
	" .balign 4\n"						\
	" .long (" #from ") - .\n"				\
	" .long (" #to ") - .\n"				\
	" .long (" _EXPAND_EXTABLE_HANDLE(handler) ") - .\n"	\
	" .popsection\n"

# define _ASM_EXTABLE(from, to)					\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_default)

# define _ASM_EXTABLE_UA(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_uaccess)

# define _ASM_EXTABLE_CPY(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_copy)

# define _ASM_EXTABLE_FAULT(from, to)				\
	_ASM_EXTABLE_HANDLE(from, to, ex_handler_fault)

/* For C file, we already have NOKPROBE_SYMBOL macro */

/*
 * This output constraint should be used for any inline asm which has a "call"
 * instruction.  Otherwise the asm may be inserted before the frame pointer
 * gets set up by the containing function.  If you forget to do this, objtool
 * may print a "call without frame pointer save/setup" warning.
 */
register unsigned long current_stack_pointer asm(_ASM_SP);
#define ASM_CALL_CONSTRAINT "+r" (current_stack_pointer)
#endif /* __ASSEMBLER__ */

#endif /* __KERNEL__ */

#endif /* _ASM_X86_ASM_H */
