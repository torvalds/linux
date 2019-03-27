/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman.
 *
 * bktr_mem : This kernel module allows us to keep our allocated
 *            contiguous memory for the video buffer, DMA programs and VBI data
 *            while the main bktr driver is unloaded and reloaded.
 *            This avoids the problem of trying to allocate contiguous each
 *            time the bktr driver is loaded.
 */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the
 * Copyright (c) 2000 Roger Hardiman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <dev/bktr/bktr_mem.h>

struct memory_pointers {
	int		addresses_stored;
	vm_offset_t	dma_prog;
	vm_offset_t	odd_dma_prog;
	vm_offset_t	vbidata;
	vm_offset_t	vbibuffer;
	vm_offset_t	buf;
} memory_pointers;

static struct memory_pointers memory_list[BKTR_MEM_MAX_DEVICES];

/*************************************************************/

static int
bktr_mem_modevent(module_t mod, int type, void *unused){

	switch (type) {
	case MOD_LOAD:
		printf("bktr_mem: memory holder loaded\n");
		/*
		 * bzero((caddr_t)memory_list, sizeof(memory_list));
		 * causes a panic. So use a simple for loop for now.
		 */
		{
			int x;
			unsigned char *d;

			d = (unsigned char *)memory_list;
			for (x = 0; x < sizeof(memory_list); x++)
				d[x] = 0;
		}
		return 0;
	case MOD_UNLOAD:
		printf("bktr_mem: memory holder cannot be unloaded\n");
		return EBUSY;
	default:
		return EOPNOTSUPP;
		break;
	}
	return (0);
}

/*************************************************************/

int
bktr_has_stored_addresses(int unit)
{

	if (unit < 0 || unit >= BKTR_MEM_MAX_DEVICES) {
		printf("bktr_mem: Unit number %d invalid\n", unit);
		return 0;
	}

	return memory_list[unit].addresses_stored;
}

/*************************************************************/

void
bktr_store_address(int unit, int type, vm_offset_t addr)
{

	if (unit < 0 || unit >= BKTR_MEM_MAX_DEVICES) {
		printf("bktr_mem: Unit number %d invalid for memory type %d, address %p\n",
		       unit, type, (void *) addr);
		return;
	}

	switch (type) {
	case BKTR_MEM_DMA_PROG:
		memory_list[unit].dma_prog = addr;
		memory_list[unit].addresses_stored = 1;
		break;
	case BKTR_MEM_ODD_DMA_PROG:
		memory_list[unit].odd_dma_prog = addr;
		memory_list[unit].addresses_stored = 1;
		break;
	case BKTR_MEM_VBIDATA:
		memory_list[unit].vbidata = addr;
		memory_list[unit].addresses_stored = 1;
		break;
	case BKTR_MEM_VBIBUFFER:
		memory_list[unit].vbibuffer = addr;
		memory_list[unit].addresses_stored = 1;
		break;
	case BKTR_MEM_BUF:
		memory_list[unit].buf = addr;
		memory_list[unit].addresses_stored = 1;
		break;
	default:
		printf("bktr_mem: Invalid memory type %d for bktr%d, address %p\n",
			type, unit, (void *)addr);
		break;
	}
}

/*************************************************************/

vm_offset_t
bktr_retrieve_address(int unit, int type)
{

	if (unit < 0 || unit >= BKTR_MEM_MAX_DEVICES) {
		printf("bktr_mem: Unit number %d too large for memory type %d\n",
			unit, type);
		return (0);
	}
	switch (type) {
	case BKTR_MEM_DMA_PROG:
		return memory_list[unit].dma_prog;
	case BKTR_MEM_ODD_DMA_PROG:
		return memory_list[unit].odd_dma_prog;
	case BKTR_MEM_VBIDATA:
		return memory_list[unit].vbidata;
	case BKTR_MEM_VBIBUFFER:
		return memory_list[unit].vbibuffer;
	case BKTR_MEM_BUF:
		return memory_list[unit].buf;
	default:
		printf("bktr_mem: Invalid memory type %d for bktr%d",
		       type, unit);
		return (0);
	}
}

/*************************************************************/

static moduledata_t bktr_mem_mod = {
	"bktr_mem",
	bktr_mem_modevent,
	0
};

/*
 * The load order is First and module type is Driver to make sure bktr_mem
 * loads (and initialises) before bktr when both are loaded together.
 */
DECLARE_MODULE(bktr_mem, bktr_mem_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(bktr_mem, 1);
