/*-
 * Copyright (c) 2013 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <machine/elf.h>

#include <stand.h>
#include <bootstrap.h>
#include <loader.h>
#include <mips.h>

static int	beri_arch_autoload(void);
static ssize_t	beri_arch_copyin(const void *src, vm_offset_t va, size_t len);
static ssize_t	beri_arch_copyout(vm_offset_t va, void *dst, size_t len);
static uint64_t	beri_arch_loadaddr(u_int type, void *data, uint64_t addr);
static ssize_t	beri_arch_readin(int fd, vm_offset_t va, size_t len);

struct arch_switch archsw = {
	.arch_autoload = beri_arch_autoload,
	.arch_getdev = beri_arch_getdev,
	.arch_copyin = beri_arch_copyin,
	.arch_copyout = beri_arch_copyout,
	.arch_loadaddr = beri_arch_loadaddr,
	.arch_readin = beri_arch_readin,

};

static int
beri_arch_autoload(void)
{

	return (0);
}

static ssize_t
beri_arch_copyin(const void *src, vm_offset_t va, size_t len)
{

	memcpy((void *)va, src, len);
	return (len);
}

static ssize_t
beri_arch_copyout(vm_offset_t va, void *dst, size_t len)
{

	memcpy(dst, (void *)va, len);
	return (len);
}

static uint64_t
beri_arch_loadaddr(u_int type, void *data, uint64_t addr)
{
	uint64_t align;

	/* Align ELF objects at page boundaries; others at cache lines. */
	align = (type == LOAD_ELF) ? PAGE_SIZE : CACHE_LINE_SIZE;
	return (roundup2(addr, align));
}

static ssize_t
beri_arch_readin(int fd, vm_offset_t va, size_t len)
{

	return (read(fd, (void *)va, len));
}
