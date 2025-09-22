/* $OpenBSD: io.c,v 1.6 2021/09/17 15:18:04 deraadt Exp $ */
/*-
 * Copyright (c) 1998 Doug Rabson
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
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <stdlib.h>
#include <err.h>

#include "io.h"

static struct io_ops *ops;

int
ioperm(unsigned long from, unsigned long num, int on)
{
	int error;
	int bwx;
	size_t len = sizeof(bwx);
	int mib[3];

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_CHIPSET;
	mib[2] = CPU_CHIPSET_BWX;
	if ((error = sysctl(mib, 3, &bwx, &len, NULL, 0)) < 0)
		return error;
	if (bwx)
		ops = &bwx_io_ops;
	else
#ifdef notyet
		ops = &swiz_io_ops;
#else
	errx(1, "libio is only available on bwx capable machines");
#endif
	
	return ops->ioperm(from, num, on);
}

u_int8_t
inb(u_int32_t port)
{
	return ops->inb(port);
}

u_int16_t
inw(u_int32_t port)
{
	return ops->inw(port);
}

u_int32_t
inl(u_int32_t port)
{
	return ops->inl(port);
}

void
outb(u_int32_t port, u_int8_t val)
{
	ops->outb(port, val);
}

void
outw(u_int32_t port, u_int16_t val)
{
	ops->outw(port, val);
}

void
outl(u_int32_t port, u_int32_t val)
{
	ops->outl(port, val);
}

void *
map_memory(u_int32_t address, u_int32_t size)
{
	return ops->map_memory(address, size);
}

void
unmap_memory(void *handle, u_int32_t size)
{
	ops->unmap_memory(handle, size);
}

u_int8_t
readb(void *handle, u_int32_t offset)
{
	return ops->readb(handle, offset);
}

u_int16_t
readw(void *handle, u_int32_t offset)
{
	return ops->readw(handle, offset);
}

u_int32_t
readl(void *handle, u_int32_t offset)
{
	return ops->readl(handle, offset);
}

void
writeb(void *handle, u_int32_t offset, u_int8_t val)
{
	ops->writeb(handle, offset, val);
	__asm__ volatile ("mb");
}

void
writew(void *handle, u_int32_t offset, u_int16_t val)
{
	ops->writew(handle, offset, val);
	__asm__ volatile ("mb");
}

void
writel(void *handle, u_int32_t offset, u_int32_t val)
{
	ops->writel(handle, offset, val);
	__asm__ volatile ("mb");
}

void
writeb_nb(void *handle, u_int32_t offset, u_int8_t val)
{
	ops->writeb(handle, offset, val);
}

void
writew_nb(void *handle, u_int32_t offset, u_int16_t val)
{
	ops->writew(handle, offset, val);
}

void
writel_nb(void *handle, u_int32_t offset, u_int32_t val)
{
	ops->writel(handle, offset, val);
}

u_int64_t
dense_base(void)
{
	static u_int64_t base = 0;
	
	if (base == 0) {
		size_t len = sizeof(base);
		int error;
		int mib[3];
		
		mib[0] = CTL_MACHDEP;
		mib[1] = CPU_CHIPSET;
		mib[2] = CPU_CHIPSET_DENSE;
		
		if ((error = sysctl(mib, 3, &base, &len, NULL, 0)) < 0)
			err(1, "machdep.chipset.dense_base");
	}
	
	return base;
}
