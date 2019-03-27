/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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
#include <sys/param.h>
#include <sys/systm.h>

#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <machine/resource.h>

#include <machine/pci_cfgreg.h>

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <dev/hptmv/global.h>
#include <dev/hptmv/hptintf.h>
#include <dev/hptmv/mvOs.h>
#include <dev/hptmv/osbsd.h>


void HPTLIBAPI
MV_REG_WRITE_BYTE(MV_BUS_ADDR_T base, MV_U32 offset, MV_U8 val)
{ 
	writeb((void *)((ULONG_PTR)base + offset), val); 
}

void HPTLIBAPI
MV_REG_WRITE_WORD(MV_BUS_ADDR_T base, MV_U32 offset, MV_U16 val)
{ 
	writew((void *)((ULONG_PTR)base + offset), val); 
}

void HPTLIBAPI
MV_REG_WRITE_DWORD(MV_BUS_ADDR_T base, MV_U32 offset, MV_U32 val)
{
	writel((void *)((ULONG_PTR)base + offset), val);
}

MV_U8  HPTLIBAPI
MV_REG_READ_BYTE(MV_BUS_ADDR_T base, MV_U32 offset)
{
	return readb((void *)((ULONG_PTR)base + offset));
}

MV_U16 HPTLIBAPI
MV_REG_READ_WORD(MV_BUS_ADDR_T base, MV_U32 offset)
{
	return readw((void *)((ULONG_PTR)base + offset));
}

MV_U32 HPTLIBAPI
MV_REG_READ_DWORD(MV_BUS_ADDR_T base, MV_U32 offset)
{
	return readl((void *)((ULONG_PTR)base + offset));
}

int HPTLIBAPI
os_memcmp(const void *cs, const void *ct, unsigned len)
{
	return memcmp(cs, ct, len);
}

void HPTLIBAPI
os_memcpy(void *to, const void *from, unsigned len)
{
	memcpy(to, from, len);
}

void HPTLIBAPI
os_memset(void *s, char c, unsigned len)
{
	memset(s, c, len);
}

unsigned HPTLIBAPI
os_strlen(const char *s)
{
	return strlen(s);
}

void HPTLIBAPI
mvMicroSecondsDelay(MV_U32 msecs)
{
	DELAY(msecs);
}

ULONG_PTR HPTLIBAPI
fOsPhysicalAddress(void *addr)
{
	return (ULONG_PTR)(vtophys(addr));
}
