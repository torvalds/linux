/*-
 * Copyright (c) 2016 Microsoft Corp.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <dev/hyperv/vmbus/hyperv_machdep.h>

uint64_t
hypercall_md(volatile void *hc_addr, uint64_t in_val,
    uint64_t in_paddr, uint64_t out_paddr)
{
	uint32_t in_val_hi = in_val >> 32;
	uint32_t in_val_lo = in_val & 0xFFFFFFFF;
	uint32_t status_hi, status_lo;
	uint32_t in_paddr_hi = in_paddr >> 32;
	uint32_t in_paddr_lo = in_paddr & 0xFFFFFFFF;
	uint32_t out_paddr_hi = out_paddr >> 32;
	uint32_t out_paddr_lo = out_paddr & 0xFFFFFFFF;

	__asm__ __volatile__ ("call *%8" : "=d"(status_hi), "=a"(status_lo) :
	    "d" (in_val_hi), "a" (in_val_lo),
	    "b" (in_paddr_hi), "c" (in_paddr_lo),
	    "D"(out_paddr_hi), "S"(out_paddr_lo),
	    "m" (hc_addr));
	return (status_lo | ((uint64_t)status_hi << 32));
}
