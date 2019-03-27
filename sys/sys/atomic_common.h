/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SYS_ATOMIC_COMMON_H_
#define	_SYS_ATOMIC_COMMON_H_

#ifndef _MACHINE_ATOMIC_H_
#error do not include this header, use machine/atomic.h
#endif

#define	atomic_load_char(p)	(*(volatile u_char *)(p))
#define	atomic_load_short(p)	(*(volatile u_short *)(p))
#define	atomic_load_int(p)	(*(volatile u_int *)(p))
#define	atomic_load_long(p)	(*(volatile u_long *)(p))
#define	atomic_load_ptr(p)	(*(volatile uintptr_t*)(p))
#define	atomic_load_8(p)	(*(volatile uint8_t *)(p))
#define	atomic_load_16(p)	(*(volatile uint16_t *)(p))
#define	atomic_load_32(p)	(*(volatile uint32_t *)(p))
#ifdef _LP64
#define	atomic_load_64(p)	(*(volatile uint64_t *)(p))
#endif

#define	atomic_store_char(p, v)		\
    (*(volatile u_char *)(p) = (u_char)(v))
#define	atomic_store_short(p, v)		\
    (*(volatile u_short *)(p) = (u_short)(v))
#define	atomic_store_int(p, v)		\
    (*(volatile u_int *)(p) = (u_int)(v))
#define	atomic_store_long(p, v)		\
    (*(volatile u_long *)(p) = (u_long)(v))
#define	atomic_store_ptr(p, v)		\
    (*(uintptr_t *)(p) = (uintptr_t)(v))
#define	atomic_store_8(p, v)		\
    (*(volatile uint8_t *)(p) = (uint8_t)(v))
#define	atomic_store_16(p, v)		\
    (*(volatile uint16_t *)(p) = (uint16_t)(v))
#define	atomic_store_32(p, v)		\
    (*(volatile uint32_t *)(p) = (uint32_t)(v))
#ifdef _LP64
#define	atomic_store_64(p, v)		\
    (*(volatile uint64_t *)(p) = (uint64_t)(v))
#endif

#endif
