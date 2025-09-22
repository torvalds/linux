/*	$OpenBSD: arm32_machdep.c,v 1.63 2024/05/14 08:26:13 jsg Exp $	*/
/*	$NetBSD: arm32_machdep.c,v 1.42 2003/12/30 12:33:15 pk Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Machine dependant functions for kernel setup
 *
 * Created      : 17/09/94
 * Updated	: 18/04/01 updated for new wscons
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/msg.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>
#include <dev/ofw/openfirm.h>

#include <arm/machdep.h>

#ifdef CONF_HAVE_APM
#include "apm.h"
#else
#define NAPM	0
#endif

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

extern int physmem;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

int cold = 1;

pv_addr_t kernelstack;

/* the following is used externally (sysctl_hw) */
char	machine[] = MACHINE;		/* from <machine/param.h> */

/* Statically defined CPU info. */
struct cpu_info cpu_info_primary;
struct cpu_info *cpu_info_list = &cpu_info_primary;

#ifdef MULTIPROCESSOR
/*
 * Array of CPU info structures.  Must be statically-allocated because
 * curproc, etc. are used early.
 */
struct cpu_info *cpu_info[MAXCPUS] = { &cpu_info_primary };
#endif

caddr_t	msgbufaddr;
extern paddr_t msgbufphys;

struct user *proc0paddr;

#ifdef APERTURE
int allowaperture = 0;
#endif

struct consdev *cn_tab;

/* Prototypes */

void data_abort_handler		(trapframe_t *frame);
void prefetch_abort_handler	(trapframe_t *frame);

/*
 * arm32_vector_init:
 *
 *	Initialize the vector page, and select whether or not to
 *	relocate the vectors.
 *
 *	NOTE: We expect the vector page to be mapped at its expected
 *	destination.
 */
void
arm32_vector_init(vaddr_t va, int which)
{
	extern unsigned int page0[], page0_data[];
	unsigned int *vectors = (unsigned int *) va;
	unsigned int *vectors_data = vectors + (page0_data - page0);
	int vec;

	/*
	 * Loop through the vectors we're taking over, and copy the
	 * vector's insn and data word.
	 */
	for (vec = 0; vec < ARM_NVEC; vec++) {
		if ((which & (1 << vec)) == 0) {
			/* Don't want to take over this vector. */
			continue;
		}
		vectors[vec] = page0[vec];
		vectors_data[vec] = page0_data[vec];
	}

	/* Now sync the vectors. */
	cpu_icache_sync_range(va, (ARM_NVEC * 2) * sizeof(u_int));

	vector_page = va;

	if (va == ARM_VECTORS_HIGH) {
		/*
		 * Assume the MD caller knows what it's doing here, and
		 * really does want the vector page relocated.
		 *
		 * Note: This has to be done here (and not just in
		 * cpu_setup()) because the vector page needs to be
		 * accessible *before* main() is called.
		 * Think ddb(9) ...
		 *
		 * NOTE: If the CPU control register is not readable,
		 * this will totally fail!  We'll just assume that
		 * any system that has high vector support has a
		 * readable CPU control register, for now.  If we
		 * ever encounter one that does not, we'll have to
		 * rethink this.
		 */
		cpu_control(CPU_CONTROL_VECRELOC, CPU_CONTROL_VECRELOC);
	}
}

/*
 * Debug function just to park the CPU
 */

void
halt(void)
{
	while (1)
		cpu_sleep(0);
}


/* Sync the discs and unmount the filesystems */

void
bootsync(int howto)
{
	static int bootsyncdone = 0;

	if (bootsyncdone)
		return;

	bootsyncdone = 1;

	/* Make sure we can still manage to do things */
	if (__get_cpsr() & PSR_I) {
		/*
		 * If we get here then boot has been called without RB_NOSYNC
		 * and interrupts were disabled. This means the boot() call
		 * did not come from a user process e.g. shutdown, but must
		 * have come from somewhere in the kernel.
		 */
		__set_cpsr_c(PSR_I, 0);
		printf("Warning IRQ's disabled during boot()\n");
	}

	vfs_shutdown(curproc);

	if ((howto & RB_TIMEBAD) == 0) {
		resettodr();
	} else {
		printf("WARNING: not updating battery clock\n");
	}
}

/*
 * void cpu_startup(void)
 *
 * Machine dependant startup code. 
 *
 */
void
cpu_startup(void)
{
	u_int loop;
	paddr_t minaddr;
	paddr_t maxaddr;

	/* Lock down zero page */
	vector_page_setprot(PROT_READ | PROT_EXEC);

	/*
	 * Give pmap a chance to set up a few more things now the vm
	 * is initialised
	 */
	pmap_postinit();

	/*
	 * Allow per-board specific initialization
	 */
	board_startup();

	/*
	 * Initialize error message buffer (at end of core).
	 */

	/* msgbufphys was setup during the secondary boot strap */
	for (loop = 0; loop < atop(MSGBUFSIZE); ++loop)
		pmap_kenter_pa((vaddr_t)msgbufaddr + loop * PAGE_SIZE,
		    msgbufphys + loop * PAGE_SIZE, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());
	initmsgbuf(msgbufaddr, round_page(MSGBUFSIZE));

	/*
	 * Identify ourselves for the msgbuf (everything printed earlier will
	 * not be buffered).
	 */
	printf("%s", version);

	printf("real mem  = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
				   VM_PHYS_SIZE, 0, FALSE, NULL);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit(); 

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024/1024);

	curpcb = &proc0.p_addr->u_pcb;
	curpcb->pcb_flags = 0;
	curpcb->pcb_un.un_32.pcb32_und_sp = (u_int)proc0.p_addr +
	    USPACE_UNDEF_STACK_TOP;
	curpcb->pcb_un.un_32.pcb32_sp = (u_int)proc0.p_addr +
	    USPACE_SVC_STACK_TOP;
	pmap_set_pcb_pagedir(pmap_kernel(), curpcb);

        curpcb->pcb_tf = (struct trapframe *)curpcb->pcb_un.un_32.pcb32_sp - 1;
}

/*
 * machine dependent system variables.
 */

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	char *compatible;
	int node, len, error;

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case CPU_CONSDEV: {
		dev_t consdev;
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
			sizeof consdev));
	}

	case CPU_ALLOWAPERTURE:
#ifdef APERTURE
		if (securelevel > 0)
			return (sysctl_int_lower(oldp, oldlenp, newp, newlen,
			    &allowaperture));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &allowaperture));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif

	case CPU_COMPATIBLE:
		node = OF_finddevice("/");
		len = OF_getproplen(node, "compatible");
		if (len <= 0)
			return (EOPNOTSUPP); 
		compatible = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
		OF_getprop(node, "compatible", compatible, len);
		compatible[len - 1] = 0;
		error = sysctl_rdstring(oldp, oldlenp, newp, compatible);
		free(compatible, M_TEMP, len);
		return error;

	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
