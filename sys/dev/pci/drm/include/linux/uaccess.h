/*	$OpenBSD: uaccess.h,v 1.7 2022/02/01 04:09:14 jsg Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_UACCESS_H
#define _LINUX_UACCESS_H

#include <sys/param.h>
#include <sys/systm.h>
#include <uvm/uvm_extern.h>

#include <linux/sched.h>

static inline unsigned long
__copy_to_user(void *to, const void *from, unsigned long len)
{
	if (copyout(from, to, len))
		return len;
	return 0;
}

static inline unsigned long
copy_to_user(void *to, const void *from, unsigned long len)
{
	return __copy_to_user(to, from, len);
}

static inline unsigned long
__copy_from_user(void *to, const void *from, unsigned long len)
{
	if (copyin(from, to, len))
		return len;
	return 0;
}

static inline unsigned long
copy_from_user(void *to, const void *from, unsigned long len)
{
	return __copy_from_user(to, from, len);
}

#define get_user(x, ptr)	-copyin(ptr, &(x), sizeof(x))
#define put_user(x, ptr) ({				\
	__typeof((x)) __tmp = (x);			\
	-copyout(&(__tmp), ptr, sizeof(__tmp));		\
})
#define __get_user(x, ptr)	get_user((x), (ptr))
#define __put_user(x, ptr)	put_user((x), (ptr))

#define unsafe_put_user(x, ptr, err) ({				\
	__typeof((x)) __tmp = (x);				\
	if (copyout(&(__tmp), ptr, sizeof(__tmp)) != 0)		\
		goto err;					\
})

static inline int
access_ok(const void *addr, unsigned long size)
{
	vaddr_t startva = (vaddr_t)addr;
	vaddr_t endva = ((vaddr_t)addr) + size;
	return (startva >= VM_MIN_ADDRESS && endva >= VM_MIN_ADDRESS) &&
	    (startva <= VM_MAXUSER_ADDRESS && endva <= VM_MAXUSER_ADDRESS);
}

#define user_access_begin(addr, size)	access_ok(addr, size)
#define user_access_end()

#define user_write_access_begin(addr, size)	access_ok(addr, size)
#define user_write_access_end()

#if defined(__i386__) || defined(__amd64__)

static inline void
pagefault_disable(void)
{
	curcpu()->ci_inatomic++;
	KASSERT(curcpu()->ci_inatomic > 0);
}

static inline void
pagefault_enable(void)
{
	KASSERT(curcpu()->ci_inatomic > 0);
	curcpu()->ci_inatomic--;
}

static inline int
pagefault_disabled(void)
{
	return curcpu()->ci_inatomic;
}

static inline unsigned long
__copy_to_user_inatomic(void *to, const void *from, unsigned long len)
{
	struct cpu_info *ci = curcpu();
	int inatomic = ci->ci_inatomic;
	int error;

	ci->ci_inatomic = 1;
	error = copyout(from, to, len);
	ci->ci_inatomic = inatomic;

	return (error ? len : 0);
}

static inline unsigned long
__copy_from_user_inatomic(void *to, const void *from, unsigned long len)
{
	struct cpu_info *ci = curcpu();
	int inatomic = ci->ci_inatomic;
	int error;

	ci->ci_inatomic = 1;
	error = copyin(from, to, len);
	ci->ci_inatomic = inatomic;

	return (error ? len : 0);
}

static inline unsigned long
__copy_from_user_inatomic_nocache(void *to, const void *from, unsigned long len)
{
	return __copy_from_user_inatomic(to, from, len);
}

#endif /* defined(__i386__) || defined(__amd64__) */

#endif
