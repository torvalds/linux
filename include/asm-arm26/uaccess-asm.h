/*
 *  linux/include/asm-arm/proc-armo/uaccess.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * The fs functions are implemented on the ARM2 and ARM3 architectures
 * manually.
 * Use *_user functions to access user memory with faulting behaving
 *   as though the user is accessing the memory.
 * Use set_fs(get_ds()) and then the *_user functions to allow them to
 *   access kernel memory.
 */

/*
 * These are the values used to represent the user `fs' and the kernel `ds'
 * FIXME - the KERNEL_DS should end at 0x03000000 but we want to access ROM at
 * 0x03400000. ideally we want to forbid access to the IO space inbetween.
 */
#define KERNEL_DS	0x03FFFFFF
#define USER_DS   	0x02000000

extern uaccess_t uaccess_user, uaccess_kernel;

static inline void set_fs (mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
	current->thread.uaccess = (fs == USER_DS ? &uaccess_user : &uaccess_kernel);
}

#define __range_ok(addr,size) ({					\
	unsigned long flag, roksum;					\
	__asm__ __volatile__("subs %1, %0, %3; cmpcs %1, %2; movcs %0, #0" \
		: "=&r" (flag), "=&r" (roksum)				\
		: "r" (addr), "Ir" (size), "0" (current_thread_info()->addr_limit)	\
		: "cc");						\
	flag; })

#define __addr_ok(addr) ({						\
	unsigned long flag;						\
	__asm__ __volatile__("cmp %2, %0; movlo %0, #0"			\
		: "=&r" (flag)						\
		: "0" (current_thread_info()->addr_limit), "r" (addr)	\
		: "cc");						\
	(flag == 0); })

#define __put_user_asm_byte(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->thread.uaccess->put_byte),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __put_user_asm_half(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->thread.uaccess->put_half),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __put_user_asm_word(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %1\n"					\
	"	mov	r1, %2\n"					\
	"	mov	r2, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r2\n"					\
	: "=r" (err)							\
	: "r" (x), "r" (addr), "r" (current->thread.uaccess->put_word),	\
	  "0" (err)							\
	: "r0", "r1", "r2", "lr")

#define __put_user_asm_dword(x,addr,err)                                 \
        __asm__ __volatile__(                                           \
        "       mov     r0, %1\n"                                       \
        "       mov     r1, %2\n"                                       \
        "       mov     r2, %0\n"                                       \
        "       mov     lr, pc\n"                                       \
        "       mov     pc, %3\n"                                       \
        "       mov     %0, r2\n"                                       \
        : "=r" (err)                                                    \
        : "r" (x), "r" (addr), "r" (current->thread.uaccess->put_dword), \
          "0" (err)                                                     \
        : "r0", "r1", "r2", "lr")

#define __get_user_asm_byte(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->thread.uaccess->get_byte), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __get_user_asm_half(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->thread.uaccess->get_half), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __get_user_asm_word(x,addr,err)					\
	__asm__ __volatile__(						\
	"	mov	r0, %2\n"					\
	"	mov	r1, %0\n"					\
	"	mov	lr, pc\n"					\
	"	mov	pc, %3\n"					\
	"	mov	%0, r1\n"					\
	"	mov	%1, r0\n"					\
	: "=r" (err), "=r" (x)						\
	: "r" (addr), "r" (current->thread.uaccess->get_word), "0" (err)	\
	: "r0", "r1", "r2", "lr")

#define __do_copy_from_user(to,from,n)					\
	(n) = current->thread.uaccess->copy_from_user((to),(from),(n))

#define __do_copy_to_user(to,from,n)					\
	(n) = current->thread.uaccess->copy_to_user((to),(from),(n))

#define __do_clear_user(addr,sz)					\
	(sz) = current->thread.uaccess->clear_user((addr),(sz))

#define __do_strncpy_from_user(dst,src,count,res)			\
	(res) = current->thread.uaccess->strncpy_from_user(dst,src,count)

#define __do_strnlen_user(s,n,res)					\
	(res) = current->thread.uaccess->strnlen_user(s,n)
