/*-
 * Copyright (c) 2018 Conrad Meyer <cem@freebsd.org>
 * All rights reserved.
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

#include "zstd_kfreebsd.h"
#include <sys/param.h>
#include <sys/endian.h>

#ifndef _KERNEL
#include <strings.h>
#endif

/*
 * The kernel as a standalone target does not link against libgcc or
 * libcompiler-rt. On platforms (e.g., MIPS and RISCV) that do not have a
 * direct assembly implementation of the relevant builtin functions that zstd
 * references, the compiler converts them into calls to the runtime library
 * intrinsics.  Since the kernel does not link against the runtime libraries,
 * this results in a failure to link the kernel.
 *
 * The goal of the following definitions is to use supported kernel constructs
 * to implement the same functionality, without adding diff to the Zstd contrib
 * code.
 *
 * A subsequent enhancement might create a mini compiler-rt library for kernel
 * use and move these over there instead.
 */

/* Swap endianness */
int
__bswapsi2(int x)
{
	return (bswap32(x));
}

long long
__bswapdi2(long long x)
{
	return (bswap64(x));
}

/* Count trailing zeros */
int
__ctzsi2(int x)
{
       if (x == 0)
               return (sizeof(x) * NBBY);
       return (ffs(x) - 1);
}

long long
__ctzdi2(long long x)
{
       if (x == 0)
               return (sizeof(x) * NBBY);
       return (ffsll(x) - 1);
}

/* Count leading zeros */
int
__clzsi2(int x)
{
       return (sizeof(x) * NBBY - fls(x));
}

long long
__clzdi2(long long x)
{
       return (sizeof(x) * NBBY - flsll(x));
}
