/*	$OpenBSD: amdmsr.h,v 1.4 2011/03/23 16:54:35 pirofti Exp $ */

/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
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

#ifndef _MACHINE_AMDMSR_H_
#define _MACHINE_AMDMSR_H_

struct amdmsr_req {
	u_int32_t addr;	/* 32-bit MSR address */
	u_int64_t val;	/* 64-bit MSR value */
};

#define RDMSR	_IOWR('M', 0, struct amdmsr_req)
#define WRMSR	_IOW('M', 1, struct amdmsr_req)

#ifdef _KERNEL
int	amdmsr_probe(void);
#endif

#endif	/* !_MACHINE_AMDMSR_H_ */
