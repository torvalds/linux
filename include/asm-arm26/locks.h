/*
 *  linux/include/asm-arm/proc-armo/locks.h
 *
 *  Copyright (C) 2000 Russell King
 *  Fixes for 26 bit machines, (C) 2000 Dave Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Interrupt safe locking assembler. 
 */
#ifndef __ASM_PROC_LOCKS_H
#define __ASM_PROC_LOCKS_H

/* Decrements by 1, fails if value < 0 */
#define __down_op(ptr,fail)			\
	({					\
	__asm__ __volatile__ (			\
	"@ atomic down operation\n"		\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%0]\n"			\
"	and	ip, ip, #0x0c000003\n"		\
"	subs	lr, lr, #1\n"			\
"	str	lr, [%0]\n"			\
"	orrmi	ip, ip, #0x80000000	@ set N\n" \
"	teqp	ip, #0\n"			\
"	movmi	ip, %0\n"			\
"	blmi	" #fail				\
	:					\
	: "r" (ptr)				\
	: "ip", "lr", "cc");			\
	})

#define __down_op_ret(ptr,fail)			\
	({					\
		unsigned int result;		\
	__asm__ __volatile__ (			\
"	@ down_op_ret\n"			\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%1]\n"			\
"	and	ip, ip, #0x0c000003\n"		\
"	subs	lr, lr, #1\n"			\
"	str	lr, [%1]\n"			\
"	orrmi	ip, ip, #0x80000000	@ set N\n" \
"	teqp	ip, #0\n"			\
"	movmi	ip, %1\n"			\
"	movpl	ip, #0\n"			\
"	blmi	" #fail "\n"			\
"	mov	%0, ip"				\
	: "=&r" (result)			\
	: "r" (ptr)				\
	: "ip", "lr", "cc");			\
	result;					\
	})

#define __up_op(ptr,wake)			\
	({					\
	__asm__ __volatile__ (			\
	"@ up_op\n"				\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	ldr	lr, [%0]\n"			\
"	and	ip, ip, #0x0c000003\n"		\
"	adds	lr, lr, #1\n"			\
"	str	lr, [%0]\n"			\
"	orrle	ip, ip, #0x80000000	@ set N - should this be mi ??? DAG ! \n" \
"	teqp	ip, #0\n"			\
"	movmi	ip, %0\n"			\
"	blmi	" #wake				\
	:					\
	: "r" (ptr)				\
	: "ip", "lr", "cc");			\
	})

/*
 * The value 0x01000000 supports up to 128 processors and
 * lots of processes.  BIAS must be chosen such that sub'ing
 * BIAS once per CPU will result in the long remaining
 * negative.
 */
#define RW_LOCK_BIAS      0x01000000
#define RW_LOCK_BIAS_STR "0x01000000"

/* Decrements by RW_LOCK_BIAS rather than 1, fails if value != 0 */
#define __down_op_write(ptr,fail)		\
	({					\
	__asm__ __volatile__(			\
	"@ down_op_write\n"			\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
"	and	ip, ip, #0x0c000003\n"		\
\
"	ldr	lr, [%0]\n"			\
"	subs	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orreq ip, ip, #0x40000000 @ set Z \n"\
"	teqp	ip, #0\n"			\
"	movne	ip, %0\n"			\
"	blne	" #fail				\
	:					\
	: "r" (ptr), "I" (RW_LOCK_BIAS)		\
	: "ip", "lr", "cc");			\
	})

/* Increments by RW_LOCK_BIAS, wakes if value >= 0 */
#define __up_op_write(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op_read\n"			\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
\
"	ldr	lr, [%0]\n"			\
"	and	ip, ip, #0x0c000003\n"		\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orrcs ip, ip, #0x20000000 @ set C\n" \
"	teqp	ip, #0\n"			\
"	movcs	ip, %0\n"			\
"	blcs	" #wake				\
	:					\
	: "r" (ptr), "I" (RW_LOCK_BIAS)		\
	: "ip", "lr", "cc");			\
	})

#define __down_op_read(ptr,fail)		\
	__down_op(ptr, fail)

#define __up_op_read(ptr,wake)			\
	({					\
	__asm__ __volatile__(			\
	"@ up_op_read\n"			\
"	mov	ip, pc\n"			\
"	orr	lr, ip, #0x08000000\n"		\
"	teqp	lr, #0\n"			\
\
"	ldr	lr, [%0]\n"			\
"	and	ip, ip, #0x0c000003\n"		\
"	adds	lr, lr, %1\n"			\
"	str	lr, [%0]\n"			\
\
" orreq ip, ip, #0x40000000 @ Set Z \n" \
"	teqp	ip, #0\n"			\
"	moveq	ip, %0\n"			\
"	bleq	" #wake				\
	:					\
	: "r" (ptr), "I" (1)			\
	: "ip", "lr", "cc");			\
	})

#endif
