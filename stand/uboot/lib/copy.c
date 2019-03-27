/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2007 Semihalf, Rafal Jaworowski <raj@semihalf.com> 
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>

#include <stand.h>
#include <stdint.h>

#include "api_public.h"
#include "glue.h"
#include "libuboot.h"

/*
 * MD primitives supporting placement of module data 
 */

#ifdef __arm__
#define	KERN_ALIGN	(2 * 1024 * 1024)
#else
#define	KERN_ALIGN	PAGE_SIZE
#endif

/*
 * Avoid low memory, u-boot puts things like args and dtb blobs there.
 */
#define	KERN_MINADDR	max(KERN_ALIGN, (1024 * 1024))

extern void _start(void); /* ubldr entry point address. */

/*
 * This is called for every object loaded (kernel, module, dtb file, etc).  The
 * expected return value is the next address at or after the given addr which is
 * appropriate for loading the given object described by type and data.  On each
 * call the addr is the next address following the previously loaded object.
 *
 * The first call is for loading the kernel, and the addr argument will be zero,
 * and we search for a big block of ram to load the kernel and modules.
 *
 * On subsequent calls the addr will be non-zero, and we just round it up so
 * that each object begins on a page boundary.
 */
uint64_t
uboot_loadaddr(u_int type, void *data, uint64_t addr)
{
	struct sys_info *si;
	uint64_t sblock, eblock, subldr, eubldr;
	uint64_t biggest_block, this_block;
	uint64_t biggest_size, this_size;
	int i;
	char *envstr;

	if (addr == 0) {
		/*
		 * If the loader_kernaddr environment variable is set, blindly
		 * honor it.  It had better be right.  We force interpretation
		 * of the value in base-16 regardless of any leading 0x prefix,
		 * because that's the U-Boot convention.
		 */
		envstr = ub_env_get("loader_kernaddr");
		if (envstr != NULL)
			return (strtoul(envstr, NULL, 16));

		/*
		 *  Find addr/size of largest DRAM block.  Carve our own address
		 *  range out of the block, because loading the kernel over the
		 *  top ourself is a poor memory-conservation strategy. Avoid
		 *  memory at beginning of the first block of physical ram,
		 *  since u-boot likes to pass args and data there.  Assume that
		 *  u-boot has moved itself to the very top of ram and
		 *  optimistically assume that we won't run into it up there.
		 */
		if ((si = ub_get_sys_info()) == NULL)
			panic("could not retrieve system info");

		biggest_block = 0;
		biggest_size = 0;
		subldr = rounddown2((uintptr_t)_start, KERN_ALIGN);
		eubldr = roundup2((uint64_t)uboot_heap_end, KERN_ALIGN);
		for (i = 0; i < si->mr_no; i++) {
			if (si->mr[i].flags != MR_ATTR_DRAM)
				continue;
			sblock = roundup2((uint64_t)si->mr[i].start,
			    KERN_ALIGN);
			eblock = rounddown2((uint64_t)si->mr[i].start +
			    si->mr[i].size, KERN_ALIGN);
			if (biggest_size == 0)
				sblock += KERN_MINADDR;
			if (subldr >= sblock && subldr < eblock) {
				if (subldr - sblock > eblock - eubldr) {
					this_block = sblock;
					this_size  = subldr - sblock;
				} else {
					this_block = eubldr;
					this_size = eblock - eubldr;
				}
			} else if (subldr < sblock && eubldr < eblock) {
				/* Loader is below or engulfs the sblock */
				this_block = (eubldr < sblock) ? sblock : eubldr;
				this_size = eblock - this_block;
			} else {
				this_block = 0;
				this_size = 0;
			}
			if (biggest_size < this_size) {
				biggest_block = this_block;
				biggest_size  = this_size;
			}
		}
		if (biggest_size == 0)
			panic("Not enough DRAM to load kernel");
#if 0
		printf("Loading kernel into region 0x%08jx-0x%08jx (%ju MiB)\n",
		    (uintmax_t)biggest_block, 
		    (uintmax_t)biggest_block + biggest_size - 1,
		    (uintmax_t)biggest_size / 1024 / 1024);
#endif
		return (biggest_block);
	}
	return roundup2(addr, PAGE_SIZE);
}

ssize_t
uboot_copyin(const void *src, vm_offset_t dest, const size_t len)
{
	bcopy(src, (void *)dest, len);
	return (len);
}

ssize_t
uboot_copyout(const vm_offset_t src, void *dest, const size_t len)
{
	bcopy((void *)src, dest, len);
	return (len);
}

ssize_t
uboot_readin(const int fd, vm_offset_t dest, const size_t len)
{
	return (read(fd, (void *)dest, len));
}
