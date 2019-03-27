/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _ASM_UACCESS_H_
#define _ASM_UACCESS_H_

#include <linux/uaccess.h>

static inline long
copy_to_user(void *to, const void *from, unsigned long n)
{
	if (linux_copyout(from, to, n) != 0)
		return n;
	return 0;
}
#define	__copy_to_user(...)	copy_to_user(__VA_ARGS__)

static inline long
copy_from_user(void *to, const void *from, unsigned long n)
{
	if (linux_copyin(from, to, n) != 0)
		return n;
	return 0;
}
#define	__copy_from_user(...)	copy_from_user(__VA_ARGS__)
#define	__copy_in_user(...)	copy_from_user(__VA_ARGS__)

#define	user_access_begin() do { } while (0)
#define	user_access_end() do { } while (0)

#define	unsafe_get_user(x, ptr, err) do { \
	if (unlikely(__get_user(x, ptr))) \
		goto err; \
} while (0)

#define	unsafe_put_user(x, ptr, err) do { \
	if (unlikely(__put_user(x, ptr))) \
		goto err; \
} while (0)

#endif	/* _ASM_UACCESS_H_ */
