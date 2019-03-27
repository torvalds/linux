/*-
 * Copyright (c) 2015 Andrew Turner
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
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/devmap.h>

#include <vm/vm.h>

#include <machine/bus.h>
#include <machine/platform.h>
#include <machine/platformvar.h>

#include <arm/qemu/virt_mp.h>

#include "platform_if.h"

/*
 * Set up static device mappings.
 */
static int
virt_devmap_init(platform_t plat)
{

	devmap_add_entry(0x09000000, 0x100000); /* Uart */
	return (0);
}

static platform_method_t virt_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	virt_devmap_init),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	virt_mp_start_ap),
#endif

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(virt, "virt", 0, "linux,dummy-virt", 1);

static int
gem5_devmap_init(platform_t plat)
{

	devmap_add_entry(0x1c090000, 0x100000); /* Uart */
	return (0);
}

static platform_method_t gem5_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	gem5_devmap_init),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(gem5, "gem5", 0, "arm,vexpress", 1);
