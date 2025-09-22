/* $OpenBSD: bwx.c,v 1.10 2021/09/17 15:19:52 deraadt Exp $ */
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
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <machine/bwx.h>
#include <machine/cpu.h>
#include <machine/sysarch.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdlib.h>
#include <unistd.h>

#include "io.h"

#define	round_page(x)	(((x) + page_mask) & ~page_mask)
#define	trunc_page(x)	((x) & ~page_mask)
static long		page_mask;

#define PATH_APERTURE "/dev/xf86"

#define mb()	__asm__ volatile("mb"  : : : "memory")
#define wmb()	__asm__ volatile("wmb" : : : "memory")

static int		mem_fd = -1;	/* file descriptor to /dev/mem */
static void	       *bwx_int1_ports = MAP_FAILED; /* mapped int1 io ports */
static void	       *bwx_int2_ports = MAP_FAILED; /* mapped int2 io ports */
static void	       *bwx_int4_ports = MAP_FAILED; /* mapped int4 io ports */
static u_int64_t	bwx_io_base;	/* physical address of ports */
static u_int64_t	bwx_mem_base;	/* physical address of bwx mem */

static void
bwx_open_mem(void)
{

	if (mem_fd != -1) 
		return;
	mem_fd = open(_PATH_MEM, O_RDWR);
	if (mem_fd < 0) 
		mem_fd = open(PATH_APERTURE, O_RDWR);
	if (mem_fd < 0)
		err(1, "Failed to open both %s and %s", _PATH_MEM, 
		    PATH_APERTURE);
}

static void 
bwx_close_mem(void)
{

	if (mem_fd != -1) {
		close(mem_fd);
		mem_fd = -1;
	}
}

static void
bwx_init(void)
{
	size_t len = sizeof(u_int64_t);
	int error;
	int mib[3];
	
	page_mask = getpagesize() - 1;

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_CHIPSET;
	mib[2] = CPU_CHIPSET_PORTS;
	if ((error = sysctl(mib, 3, &bwx_io_base, &len, NULL, 0)) < 0)
		err(1, "machdep.chipset.ports_base");
	mib[2] = CPU_CHIPSET_MEM;
	if ((error = sysctl(mib, 3, &bwx_mem_base, &len, 0, 0)) < 0)
		err(1, "machdep.chipset.memory");
}

static int
bwx_ioperm(u_int32_t from, u_int32_t num, int on)
{
	u_int32_t start, end;

	if (bwx_int1_ports == MAP_FAILED)
		bwx_init();

	if (!on)
		return -1;		/* XXX can't unmap yet */

	if (bwx_int1_ports != MAP_FAILED)
		return 0;
	
	bwx_open_mem();
	start = trunc_page(from);
	end = round_page(from + num);
	if ((bwx_int1_ports = mmap(0, end-start, PROT_READ|PROT_WRITE,
	    MAP_SHARED, mem_fd, bwx_io_base + BWX_EV56_INT1 + start)) ==
	    MAP_FAILED) 
		err(1, "mmap int1");
	if ((bwx_int2_ports = mmap(0, end-start, PROT_READ|PROT_WRITE,
	    MAP_SHARED, mem_fd, bwx_io_base + BWX_EV56_INT2 + start)) ==
	    MAP_FAILED)
		err(1, "mmap int2");
	if ((bwx_int4_ports = mmap(0, end-start, PROT_READ|PROT_WRITE,
	    MAP_SHARED, mem_fd, bwx_io_base + BWX_EV56_INT4 + start)) ==
	    MAP_FAILED) 
		err(1, "mmap int4");
	bwx_close_mem();
	return 0;
}

static u_int8_t
bwx_inb(u_int32_t port)
{
	mb();
	return alpha_ldbu(bwx_int1_ports + port);
}

static u_int16_t
bwx_inw(u_int32_t port)
{
	mb();
	return alpha_ldwu(bwx_int2_ports + port);
}

static u_int32_t
bwx_inl(u_int32_t port)
{
	mb();
	return alpha_ldlu(bwx_int4_ports + port);
}

static void
bwx_outb(u_int32_t port, u_int8_t val)
{
	alpha_stb(bwx_int1_ports + port, val);
	mb();
	wmb();
}

static void
bwx_outw(u_int32_t port, u_int16_t val)
{
	alpha_stw(bwx_int2_ports + port, val);
	mb();
	wmb();
}

static void
bwx_outl(u_int32_t port, u_int32_t val)
{
	alpha_stl(bwx_int4_ports + port, val);
	mb();
	wmb();
}

struct bwx_mem_handle {
	void	*virt1;		/* int1 address in user address-space */
	void	*virt2;		/* int2 address in user address-space */
	void	*virt4;		/* int4 address in user address-space */
};

static void *
bwx_map_memory(u_int32_t address, u_int32_t size)
{
	struct bwx_mem_handle *h;
	size_t sz = (size_t)size << 5;

	h = malloc(sizeof(struct bwx_mem_handle));
	if (h == NULL) return NULL;
	bwx_open_mem();
	h->virt1 = mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED,
	    mem_fd, bwx_mem_base + BWX_EV56_INT1 + address);
	if (h->virt1 == MAP_FAILED) {
		bwx_close_mem();
		free(h);
		return NULL;
	}
	h->virt2 = mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED,
	    mem_fd, bwx_mem_base + BWX_EV56_INT2 + address);
	if (h->virt2 == MAP_FAILED) {
		munmap(h->virt1, sz);
		bwx_close_mem();
		free(h);
		return NULL;
	}
	h->virt4 = mmap(0, sz, PROT_READ|PROT_WRITE, MAP_SHARED,
	    mem_fd, bwx_mem_base + BWX_EV56_INT4 + address);
	if (h->virt4 == MAP_FAILED) {
		munmap(h->virt1, sz);
		munmap(h->virt2, sz);
		bwx_close_mem();
		free(h);
		return NULL;
	}
	bwx_close_mem();
	return h;
}

static void
bwx_unmap_memory(void *handle, u_int32_t size)
{
	struct bwx_mem_handle *h = handle;
	size_t sz = (size_t)size << 5;

	munmap(h->virt1, sz);
	munmap(h->virt2, sz);
	munmap(h->virt4, sz);
	free(h);
}

static u_int8_t
bwx_readb(void *handle, u_int32_t offset)
{
	struct bwx_mem_handle *h = handle;

	return alpha_ldbu(h->virt1 + offset);
}

static u_int16_t
bwx_readw(void *handle, u_int32_t offset)
{
	struct bwx_mem_handle *h = handle;

	return alpha_ldwu(h->virt2 + offset);
}

static u_int32_t
bwx_readl(void *handle, u_int32_t offset)
{
	struct bwx_mem_handle *h = handle;

	return alpha_ldlu(h->virt4 + offset);
}

static void
bwx_writeb(void *handle, u_int32_t offset, u_int8_t val)
{
	struct bwx_mem_handle *h = handle;

	alpha_stb(h->virt1 + offset, val);
}

static void
bwx_writew(void *handle, u_int32_t offset, u_int16_t val)
{
	struct bwx_mem_handle *h = handle;

	alpha_stw(h->virt2 + offset, val);
}

static void
bwx_writel(void *handle, u_int32_t offset, u_int32_t val)
{
	struct bwx_mem_handle *h = handle;

	alpha_stl(h->virt4 + offset, val);
}

struct io_ops bwx_io_ops = {
	bwx_ioperm,
	bwx_inb,
	bwx_inw,
	bwx_inl,
	bwx_outb,
	bwx_outw,
	bwx_outl,
	bwx_map_memory,
	bwx_unmap_memory,
	bwx_readb,
	bwx_readw,
	bwx_readl,
	bwx_writeb,
	bwx_writew,
	bwx_writel,
};
