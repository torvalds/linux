/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/rtas.h>
#include <machine/stdarg.h>

#include <dev/ofw/openfirm.h>

static MALLOC_DEFINE(M_RTAS, "rtas", "Run Time Abstraction Service");

static vm_offset_t	rtas_bounce_phys;
static caddr_t		rtas_bounce_virt;
static off_t		rtas_bounce_offset;
static size_t		rtas_bounce_size;
static uintptr_t	rtas_private_data;
static struct mtx	rtas_mtx;
static phandle_t	rtas;

/* From ofwcall.S */
int rtascall(vm_offset_t callbuffer, uintptr_t rtas_privdat);
extern uintptr_t	rtas_entry;
extern register_t	rtasmsr;

/*
 * After the VM is up, allocate RTAS memory and instantiate it
 */

static void rtas_setup(void *);

SYSINIT(rtas_setup, SI_SUB_KMEM, SI_ORDER_ANY, rtas_setup, NULL);

static void
rtas_setup(void *junk)
{
	ihandle_t rtasi;
	cell_t rtas_size = 0, rtas_ptr;
	char path[31];
	int result;

	rtas = OF_finddevice("/rtas");
	if (rtas == -1) {
		rtas = 0;
		return;
	}
	OF_package_to_path(rtas, path, sizeof(path));

	mtx_init(&rtas_mtx, "RTAS", NULL, MTX_SPIN);

	/* RTAS must be called with everything turned off in MSR */
	rtasmsr = mfmsr();
	rtasmsr &= ~(PSL_IR | PSL_DR | PSL_EE | PSL_SE);
	#ifdef __powerpc64__
	rtasmsr &= ~PSL_SF;
	#endif

	/*
	 * Allocate rtas_size + one page of contiguous, wired physical memory
	 * that can fit into a 32-bit address space and accessed from real mode.
	 * This is used both to bounce arguments and for RTAS private data.
	 *
	 * It must be 4KB-aligned and not cross a 256 MB boundary.
	 */

	OF_getencprop(rtas, "rtas-size", &rtas_size, sizeof(rtas_size));
	rtas_size = round_page(rtas_size);
	rtas_bounce_virt = contigmalloc(rtas_size + PAGE_SIZE, M_RTAS, 0, 0,
	    ulmin(platform_real_maxaddr(), BUS_SPACE_MAXADDR_32BIT),
	    4096, 256*1024*1024);

	rtas_private_data = vtophys(rtas_bounce_virt);
	rtas_bounce_virt += rtas_size;	/* Actual bounce area */
	rtas_bounce_phys = vtophys(rtas_bounce_virt);
	rtas_bounce_size = PAGE_SIZE;

	/*
	 * Instantiate RTAS. We always use the 32-bit version.
	 */

	if (OF_hasprop(rtas, "linux,rtas-entry") &&
	    OF_hasprop(rtas, "linux,rtas-base")) {
		OF_getencprop(rtas, "linux,rtas-base", &rtas_ptr,
		    sizeof(rtas_ptr));
		rtas_private_data = rtas_ptr;
		OF_getencprop(rtas, "linux,rtas-entry", &rtas_ptr,
		    sizeof(rtas_ptr));
	} else {
		rtasi = OF_open(path);
		if (rtasi == 0) {
			rtas = 0;
			printf("Error initializing RTAS: could not open "
			    "node\n");
			return;
		}

		result = OF_call_method("instantiate-rtas", rtasi, 1, 1,
		    (cell_t)rtas_private_data, &rtas_ptr);
		OF_close(rtasi);

		if (result != 0) {
			rtas = 0;
			rtas_ptr = 0;
			printf("Error initializing RTAS (%d)\n", result);
			return;
		}
	}

	rtas_entry = (uintptr_t)(rtas_ptr);
}

static cell_t
rtas_real_map(const void *buf, size_t len)
{
	cell_t phys;

	mtx_assert(&rtas_mtx, MA_OWNED);

	/*
	 * Make sure the bounce page offset satisfies any reasonable
	 * alignment constraint.
	 */
	rtas_bounce_offset += sizeof(register_t) -
	    (rtas_bounce_offset % sizeof(register_t));

	if (rtas_bounce_offset + len > rtas_bounce_size) {
		panic("Oversize RTAS call!");
		return 0;
	}

	if (buf != NULL)
		memcpy(rtas_bounce_virt + rtas_bounce_offset, buf, len);
	else
		return (0);

	phys = rtas_bounce_phys + rtas_bounce_offset;
	rtas_bounce_offset += len;

	return (phys);
}

static void
rtas_real_unmap(cell_t physaddr, void *buf, size_t len)
{
	mtx_assert(&rtas_mtx, MA_OWNED);

	if (physaddr == 0)
		return;

	memcpy(buf, rtas_bounce_virt + (physaddr - rtas_bounce_phys), len);
}

/* Check if we have RTAS */
int
rtas_exists(void)
{
	return (rtas != 0);
}

/* Call an RTAS method by token */
int
rtas_call_method(cell_t token, int nargs, int nreturns, ...)
{
	vm_offset_t argsptr;
	jmp_buf env, *oldfaultbuf;
	va_list ap;
	struct {
		cell_t token;
		cell_t nargs;
		cell_t nreturns;
		cell_t args_n_results[12];
	} args;
	int n, result;

	if (!rtas_exists() || nargs + nreturns > 12)
		return (-1);

	args.token = token;
	va_start(ap, nreturns);

	mtx_lock_spin(&rtas_mtx);
	rtas_bounce_offset = 0;

	args.nargs = nargs;
	args.nreturns = nreturns;

	for (n = 0; n < nargs; n++)
		args.args_n_results[n] = va_arg(ap, cell_t);

	argsptr = rtas_real_map(&args, sizeof(args));

	/* Get rid of any stale machine checks that have been waiting.  */
	__asm __volatile ("sync; isync");
	oldfaultbuf = curthread->td_pcb->pcb_onfault;
	curthread->td_pcb->pcb_onfault = &env;
	if (!setjmp(env)) {
		__asm __volatile ("sync");
		result = rtascall(argsptr, rtas_private_data);
		__asm __volatile ("sync; isync");
	} else {
		result = RTAS_HW_ERROR;
	}
	curthread->td_pcb->pcb_onfault = oldfaultbuf;
	__asm __volatile ("sync");

	rtas_real_unmap(argsptr, &args, sizeof(args));
	mtx_unlock_spin(&rtas_mtx);

	if (result < 0)
		return (result);

	for (n = nargs; n < nargs + nreturns; n++)
		*va_arg(ap, cell_t *) = args.args_n_results[n];
	return (result);
}

/* Look up an RTAS token */
cell_t
rtas_token_lookup(const char *method)
{
	cell_t token;
	
	if (!rtas_exists())
		return (-1);

	if (OF_getencprop(rtas, method, &token, sizeof(token)) == -1)
		return (-1);

	return (token);
}


