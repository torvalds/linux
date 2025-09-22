/* $OpenBSD: machdep.c,v 1.206 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: machdep.c,v 1.210 2000/06/01 17:12:38 thorpej Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center and by Chris G. Demetriou.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sched.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgbuf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/sysctl.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <net/if.h>
#include <uvm/uvm.h>

#include <machine/kcore.h>
#ifndef NO_IEEE
#include <machine/fpu.h>
#endif
#include <sys/timetc.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <dev/cons.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/reg.h>
#include <machine/rpb.h>
#include <machine/prom.h>
#include <machine/cpuconf.h>
#ifndef NO_IEEE
#include <machine/ieeefp.h>
#endif

#include <dev/pci/pcivar.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#endif

#include "ioasic.h"

#if NIOASIC > 0
#include <machine/tc_machdep.h>
#include <dev/tc/tcreg.h>
#include <dev/tc/ioasicvar.h>
#endif

int	cpu_dump(void);
int	cpu_dumpsize(void);
u_long	cpu_dump_mempagecnt(void);
void	dumpsys(void);
void	identifycpu(void);
void	regdump(struct trapframe *framep);
void	printregs(struct reg *);

struct uvm_constraint_range  isa_constraint = { 0x0, 0x00ffffffUL };
struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = {
	&isa_constraint,
	NULL
};

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

#ifdef APERTURE
int allowaperture = 0;
#endif

int	totalphysmem;		/* total amount of physical memory in system */
int	physmem;		/* physical mem used by OpenBSD + some rsvd */
int	resvmem;		/* amount of memory reserved for PROM */
int	unusedmem;		/* amount of memory for OS that we don't use */
int	unknownmem;		/* amount of memory with an unknown use */

int	cputype;		/* system type, from the RPB */

int	bootdev_debug = 0;	/* patchable, or from DDB */

/* the following is used externally (sysctl_hw) */
char	machine[] = MACHINE;		/* from <machine/param.h> */
char	cpu_model[128];

struct	user *proc0paddr;

/* Number of machine cycles per microsecond */
u_int64_t	cycles_per_usec;

struct bootinfo_kernel bootinfo;

struct consdev *cn_tab;

/* For built-in TCDS */
#if defined(DEC_3000_300) || defined(DEC_3000_500)
u_int8_t	dec_3000_scsiid[2], dec_3000_scsifast[2];
#endif

struct platform platform;

/* for cpu_sysctl() */
#ifndef NO_IEEE
int	alpha_fp_sync_complete = 0;	/* fp fixup if sync even without /s */
#endif
#if NIOASIC > 0
int	alpha_led_blink = 1;
#endif

/*
 * XXX This should be dynamically sized, but we have the chicken-egg problem!
 * XXX it should also be larger than it is, because not all of the mddt
 * XXX clusters end up being used for VM.
 */
phys_ram_seg_t mem_clusters[VM_PHYSSEG_MAX];	/* low size bits overloaded */
int	mem_cluster_cnt;

/*
 * Arguments are PFN of current level 1 page table, and bootinfo magic, pointer
 * and version.
 */
void
alpha_init(u_long unused, u_long ptb, u_long bim, u_long bip, u_long biv)
{
	extern char kernel_text[], _end[];
	struct mddt *mddtp;
	struct mddt_cluster *memc;
	int i, mddtweird;
	struct vm_physseg *vps;
	vaddr_t kernstart, kernend;
	paddr_t kernstartpfn, kernendpfn, pfn0, pfn1;
	char *p;
	const char *bootinfo_msg;
	const struct cpuinit *c;
	extern caddr_t esym;
	struct cpu_info *ci;
	cpuid_t cpu_id;

	/* NO OUTPUT ALLOWED UNTIL FURTHER NOTICE */

	/*
	 * Turn off interrupts (not mchecks) and floating point.
	 * Make sure the instruction and data streams are consistent.
	 */
	(void)alpha_pal_swpipl(ALPHA_PSL_IPL_HIGH);
	alpha_pal_wrfen(0);
	ALPHA_TBIA();
	alpha_pal_imb();

	/* Initialize the SCB. */
	scb_init();

	cpu_id = cpu_number();

#if defined(MULTIPROCESSOR)
	/*
	 * Set our SysValue to the address of our cpu_info structure.
	 * Secondary processors do this in their spinup trampoline.
	 */
	alpha_pal_wrval((u_long)&cpu_info_primary);
	cpu_info[cpu_id] = &cpu_info_primary;
#endif

	ci = curcpu();
	ci->ci_cpuid = cpu_id;

	/*
	 * Get critical system information (if possible, from the
	 * information provided by the boot program).
	 */
	bootinfo_msg = NULL;
	if (bim == BOOTINFO_MAGIC) {
		if (biv == 0) {		/* backward compat */
			biv = *(u_long *)bip;
			bip += 8;
		}
		switch (biv) {
		case 1: {
			struct bootinfo_v1 *v1p = (struct bootinfo_v1 *)bip;

			bootinfo.ssym = v1p->ssym;
			bootinfo.esym = v1p->esym;
			/* hwrpb may not be provided by boot block in v1 */
			if (v1p->hwrpb != NULL) {
				bootinfo.hwrpb_phys =
				    ((struct rpb *)v1p->hwrpb)->rpb_phys;
				bootinfo.hwrpb_size = v1p->hwrpbsize;
			} else {
				bootinfo.hwrpb_phys =
				    ((struct rpb *)HWRPB_ADDR)->rpb_phys;
				bootinfo.hwrpb_size =
				    ((struct rpb *)HWRPB_ADDR)->rpb_size;
			}
			bcopy(v1p->boot_flags, bootinfo.boot_flags,
			    min(sizeof v1p->boot_flags,
			      sizeof bootinfo.boot_flags));
			bcopy(v1p->booted_kernel, bootinfo.booted_kernel,
			    min(sizeof v1p->booted_kernel,
			      sizeof bootinfo.booted_kernel));
			boothowto = v1p->howto;
			/* booted dev not provided in bootinfo */
			init_prom_interface((struct rpb *)
			    ALPHA_PHYS_TO_K0SEG(bootinfo.hwrpb_phys));
			prom_getenv(PROM_E_BOOTED_DEV, bootinfo.booted_dev,
			    sizeof bootinfo.booted_dev);
			break;
		}
		default:
			bootinfo_msg = "unknown bootinfo version";
			goto nobootinfo;
		}
	} else {
		bootinfo_msg = "boot program did not pass bootinfo";
nobootinfo:
		bootinfo.ssym = (u_long)_end;
		bootinfo.esym = (u_long)_end;
		bootinfo.hwrpb_phys = ((struct rpb *)HWRPB_ADDR)->rpb_phys;
		bootinfo.hwrpb_size = ((struct rpb *)HWRPB_ADDR)->rpb_size;
		init_prom_interface((struct rpb *)HWRPB_ADDR);
		prom_getenv(PROM_E_BOOTED_OSFLAGS, bootinfo.boot_flags,
		    sizeof bootinfo.boot_flags);
		prom_getenv(PROM_E_BOOTED_FILE, bootinfo.booted_kernel,
		    sizeof bootinfo.booted_kernel);
		prom_getenv(PROM_E_BOOTED_DEV, bootinfo.booted_dev,
		    sizeof bootinfo.booted_dev);
	}

	esym = (caddr_t)bootinfo.esym;
	/*
	 * Initialize the kernel's mapping of the RPB.  It's needed for
	 * lots of things.
	 */
	hwrpb = (struct rpb *)ALPHA_PHYS_TO_K0SEG(bootinfo.hwrpb_phys);

#if defined(DEC_3000_300) || defined(DEC_3000_500)
	if (hwrpb->rpb_type == ST_DEC_3000_300 ||
	    hwrpb->rpb_type == ST_DEC_3000_500) {
		prom_getenv(PROM_E_SCSIID, dec_3000_scsiid,
		    sizeof(dec_3000_scsiid));
		prom_getenv(PROM_E_SCSIFAST, dec_3000_scsifast,
		    sizeof(dec_3000_scsifast));
	}
#endif

	/*
	 * Remember how many cycles there are per microsecond,
	 * so that we can use delay().  Round up, for safety.
	 */
	cycles_per_usec = (hwrpb->rpb_cc_freq + 999999) / 1000000;

	/*
	 * Initialize the (temporary) bootstrap console interface, so
	 * we can use printf until the VM system starts being setup.
	 * The real console is initialized before then.
	 */
	init_bootstrap_console();

	/* OUTPUT NOW ALLOWED */

	/* delayed from above */
	if (bootinfo_msg)
		printf("WARNING: %s (0x%lx, 0x%lx, 0x%lx)\n",
		    bootinfo_msg, bim, bip, biv);

	/* Initialize the trap vectors on the primary processor. */
	trap_init();

	/*
	 * Find out what hardware we're on, and do basic initialization.
	 */
	cputype = hwrpb->rpb_type;
	if (cputype < 0) {
		/*
		 * At least some white-box systems have SRM which
		 * reports a systype that's the negative of their
		 * blue-box counterpart.
		 */
		cputype = -cputype;
	}
	c = platform_lookup(cputype);
	if (c == NULL) {
		platform_not_supported();
		/* NOTREACHED */
	}
	(*c->init)();
	strlcpy(cpu_model, platform.model, sizeof cpu_model);

	/*
	 * Initialize the real console, so that the bootstrap console is
	 * no longer necessary.
	 */
	(*platform.cons_init)();

#if 0
	/* Paranoid sanity checking */

	assert(hwrpb->rpb_primary_cpu_id == alpha_pal_whami());

	/*
	 * On single-CPU systypes, the primary should always be CPU 0,
	 * except on Alpha 8200 systems where the CPU id is related
	 * to the VID, which is related to the Turbo Laser node id.
	 */
	if (cputype != ST_DEC_21000)
		assert(hwrpb->rpb_primary_cpu_id == 0);
#endif

	/* NO MORE FIRMWARE ACCESS ALLOWED */

#ifndef SMALL_KERNEL
	/*
	 * If we run on a BWX-capable processor, override cpu_switch
	 * with a faster version.
	 * We do this now because the kernel text might be mapped
	 * read-only eventually (although this is not the case at the moment).
	 */
	if (alpha_implver() >= ALPHA_IMPLVER_EV5) {
		if ((~alpha_amask(ALPHA_AMASK_BWX) & ALPHA_AMASK_BWX) != 0) {
			extern vaddr_t __bwx_switch0, __bwx_switch1,
			    __bwx_switch2, __bwx_switch3;
			u_int32_t *dst, *src, *end;

			src = (u_int32_t *)&__bwx_switch2;
			end = (u_int32_t *)&__bwx_switch3;
			dst = (u_int32_t *)&__bwx_switch0;
			while (src != end)
				*dst++ = *src++;
			src = (u_int32_t *)&__bwx_switch1;
			end = (u_int32_t *)&__bwx_switch2;
			while (src != end)
				*dst++ = *src++;
		}
	}
#endif

	/*
	 * find out this system's page size
	 */
	if ((uvmexp.pagesize = hwrpb->rpb_page_size) != 8192)
		panic("page size %d != 8192?!", uvmexp.pagesize);

	uvm_setpagesize();

	/*
	 * Find the beginning and end of the kernel (and leave a
	 * bit of space before the beginning for the bootstrap
	 * stack).
	 */
	kernstart = trunc_page((vaddr_t)kernel_text) - 2 * PAGE_SIZE;
	kernend = (vaddr_t)round_page((vaddr_t)bootinfo.esym);

	kernstartpfn = atop(ALPHA_K0SEG_TO_PHYS(kernstart));
	kernendpfn = atop(ALPHA_K0SEG_TO_PHYS(kernend));

	/*
	 * Find out how much memory is available, by looking at
	 * the memory cluster descriptors.  This also tries to do
	 * its best to detect things that have never been seen
	 * before...
	 */
	mddtp = (struct mddt *)(((caddr_t)hwrpb) + hwrpb->rpb_memdat_off);

	/* MDDT SANITY CHECKING */
	mddtweird = 0;
	if (mddtp->mddt_cluster_cnt < 2) {
		mddtweird = 1;
		printf("WARNING: weird number of mem clusters: %lu\n",
		    (unsigned long)mddtp->mddt_cluster_cnt);
	}

#if 0
	printf("Memory cluster count: %d\n", mddtp->mddt_cluster_cnt);
#endif

	for (i = 0; i < mddtp->mddt_cluster_cnt; i++) {
		memc = &mddtp->mddt_clusters[i];
#if 0
		printf("MEMC %d: pfn 0x%lx cnt 0x%lx usage 0x%lx\n", i,
		    memc->mddt_pfn, memc->mddt_pg_cnt, memc->mddt_usage);
#endif
		totalphysmem += memc->mddt_pg_cnt;
		if (mem_cluster_cnt < VM_PHYSSEG_MAX) {	/* XXX */
			mem_clusters[mem_cluster_cnt].start =
			    ptoa(memc->mddt_pfn);
			mem_clusters[mem_cluster_cnt].size =
			    ptoa(memc->mddt_pg_cnt);
			if (memc->mddt_usage & MDDT_mbz ||
			    memc->mddt_usage & MDDT_NONVOLATILE || /* XXX */
			    memc->mddt_usage & MDDT_PALCODE)
				mem_clusters[mem_cluster_cnt].size |=
				    PROT_READ;
			else
				mem_clusters[mem_cluster_cnt].size |=
				    PROT_READ | PROT_WRITE | PROT_EXEC;
			mem_cluster_cnt++;
		} /* XXX else print something! */

		if (memc->mddt_usage & MDDT_mbz) {
			mddtweird = 1;
			printf("WARNING: mem cluster %d has weird "
			    "usage 0x%lx\n", i, (long)memc->mddt_usage);
			unknownmem += memc->mddt_pg_cnt;
			continue;
		}
		if (memc->mddt_usage & MDDT_NONVOLATILE) {
			/* XXX should handle these... */
			printf("WARNING: skipping non-volatile mem "
			    "cluster %d\n", i);
			unusedmem += memc->mddt_pg_cnt;
			continue;
		}
		if (memc->mddt_usage & MDDT_PALCODE) {
			resvmem += memc->mddt_pg_cnt;
			continue;
		}

		/*
		 * We have a memory cluster available for system
		 * software use.  We must determine if this cluster
		 * holds the kernel.
		 */
		physmem += memc->mddt_pg_cnt;
		pfn0 = memc->mddt_pfn;
		pfn1 = memc->mddt_pfn + memc->mddt_pg_cnt;
		if (pfn0 <= kernstartpfn && kernendpfn <= pfn1) {
			/*
			 * Must compute the location of the kernel
			 * within the segment.
			 */
#if 0
			printf("Cluster %d contains kernel\n", i);
#endif
			if (pfn0 < kernstartpfn) {
				/*
				 * There is a chunk before the kernel.
				 */
#if 0
				printf("Loading chunk before kernel: "
				    "0x%lx / 0x%lx\n", pfn0, kernstartpfn);
#endif
				uvm_page_physload(pfn0, kernstartpfn,
				    pfn0, kernstartpfn, 0);
			}
			if (kernendpfn < pfn1) {
				/*
				 * There is a chunk after the kernel.
				 */
#if 0
				printf("Loading chunk after kernel: "
				    "0x%lx / 0x%lx\n", kernendpfn, pfn1);
#endif
				uvm_page_physload(kernendpfn, pfn1,
				    kernendpfn, pfn1, 0);
			}
		} else {
			/*
			 * Just load this cluster as one chunk.
			 */
#if 0
			printf("Loading cluster %d: 0x%lx / 0x%lx\n", i,
			    pfn0, pfn1);
#endif
			uvm_page_physload(pfn0, pfn1, pfn0, pfn1, 0);
		}
	}

#ifdef DEBUG
	/*
	 * Dump out the MDDT if it looks odd...
	 */
	if (mddtweird) {
		printf("\n");
		printf("complete memory cluster information:\n");
		for (i = 0; i < mddtp->mddt_cluster_cnt; i++) {
			printf("mddt %d:\n", i);
			printf("\tpfn %lx\n",
			    mddtp->mddt_clusters[i].mddt_pfn);
			printf("\tcnt %lx\n",
			    mddtp->mddt_clusters[i].mddt_pg_cnt);
			printf("\ttest %lx\n",
			    mddtp->mddt_clusters[i].mddt_pg_test);
			printf("\tbva %lx\n",
			    mddtp->mddt_clusters[i].mddt_v_bitaddr);
			printf("\tbpa %lx\n",
			    mddtp->mddt_clusters[i].mddt_p_bitaddr);
			printf("\tbcksum %lx\n",
			    mddtp->mddt_clusters[i].mddt_bit_cksum);
			printf("\tusage %lx\n",
			    mddtp->mddt_clusters[i].mddt_usage);
		}
		printf("\n");
	}
#endif

	if (totalphysmem == 0)
		panic("can't happen: system seems to have no memory!");
#if 0
	printf("totalphysmem = %u\n", totalphysmem);
	printf("physmem = %u\n", physmem);
	printf("resvmem = %d\n", resvmem);
	printf("unusedmem = %d\n", unusedmem);
	printf("unknownmem = %d\n", unknownmem);
#endif

	/*
	 * Initialize error message buffer (at end of core).
	 */
	{
		vsize_t sz = (vsize_t)round_page(MSGBUFSIZE);
		vsize_t reqsz = sz;

		vps = &vm_physmem[vm_nphysseg - 1];

		/* shrink so that it'll fit in the last segment */
		if ((vps->avail_end - vps->avail_start) < atop(sz))
			sz = ptoa(vps->avail_end - vps->avail_start);

		vps->end -= atop(sz);
		vps->avail_end -= atop(sz);
		initmsgbuf((caddr_t) ALPHA_PHYS_TO_K0SEG(ptoa(vps->end)), sz);

		/* Remove the last segment if it now has no pages. */
		if (vps->start == vps->end)
			vm_nphysseg--;

		/* warn if the message buffer had to be shrunk */
		if (sz != reqsz)
			printf("WARNING: %ld bytes not available for msgbuf "
			    "in last cluster (%ld used)\n", reqsz, sz);

	}

	/*
	 * Init mapping for u page(s) for proc 0
	 */
	proc0.p_addr = proc0paddr =
	    (struct user *)pmap_steal_memory(UPAGES * PAGE_SIZE, NULL, NULL);

	/*
	 * Initialize the virtual memory system, and set the
	 * page table base register in proc 0's PCB.
	 */
	pmap_bootstrap(ALPHA_PHYS_TO_K0SEG(ptb << PGSHIFT),
	    hwrpb->rpb_max_asn, hwrpb->rpb_pcs_cnt);

	/*
	 * Initialize the rest of proc 0's PCB, and cache its physical
	 * address.
	 */
	proc0.p_md.md_pcbpaddr =
	    (struct pcb *)ALPHA_K0SEG_TO_PHYS((vaddr_t)&proc0paddr->u_pcb);

	/*
	 * Set the kernel sp, reserving space for an (empty) trapframe,
	 * and make proc0's trapframe pointer point to it for sanity.
	 */
	proc0paddr->u_pcb.pcb_hw.apcb_ksp =
	    (u_int64_t)proc0paddr + USPACE - sizeof(struct trapframe);
	proc0.p_md.md_tf =
	    (struct trapframe *)proc0paddr->u_pcb.pcb_hw.apcb_ksp;

	/*
	 * Initialize the primary CPU's idle PCB to proc0's.  In a
	 * MULTIPROCESSOR configuration, each CPU will later get
	 * its own idle PCB when autoconfiguration runs.
	 */
	ci->ci_idle_pcb = &proc0paddr->u_pcb;
	ci->ci_idle_pcb_paddr = (u_long)proc0.p_md.md_pcbpaddr;

	/*
	 * Look at arguments passed to us and compute boothowto.
	 */

	for (p = bootinfo.boot_flags; p && *p != '\0'; p++) {
		/*
		 * Note that we'd really like to differentiate case here,
		 * but the Alpha AXP Architecture Reference Manual
		 * says that we shouldn't.
		 */
		switch (*p) {
		case 'a': /* Ignore */
		case 'A':
			break;

		case 'b': /* Enter DDB as soon as the console is initialised */
		case 'B':
			boothowto |= RB_KDB;
			break;

		case 'c': /* enter user kernel configuration */
		case 'C':
			boothowto |= RB_CONFIG;
			break;

#ifdef DEBUG
		case 'd': /* crash dump immediately after autoconfig */
		case 'D':
			boothowto |= RB_DUMP;
			break;
#endif

		case 'h': /* always halt, never reboot */
		case 'H':
			boothowto |= RB_HALT;
			break;


		case 'n': /* askname */
		case 'N':
			boothowto |= RB_ASKNAME;
			break;

		case 's': /* single-user */
		case 'S':
			boothowto |= RB_SINGLE;
			break;

		case '-':
			/*
			 * Just ignore this.  It's not required, but it's
			 * common for it to be passed regardless.
			 */
			break;

		default:
			printf("Unrecognized boot flag '%c'.\n", *p);
			break;
		}
	}


	/*
	 * Figure out the number of cpus in the box, from RPB fields.
	 * Really.  We mean it.
	 */
	for (ncpusfound = 0, i = 0; i < hwrpb->rpb_pcs_cnt; i++) {
		struct pcs *pcsp;

		pcsp = LOCATE_PCS(hwrpb, i);
		if ((pcsp->pcs_flags & PCS_PP) != 0)
			ncpusfound++;
	}

	/*
	 * Initialize debuggers, and break into them if appropriate.
	 */
#ifdef DDB
	db_machine_init();
	ddb_init();

	if (boothowto & RB_KDB)
		db_enter();
#endif
	/*
	 * Figure out our clock frequency, from RPB fields.
	 */
	hz = hwrpb->rpb_intr_freq >> 12;
	if (!(60 <= hz && hz <= 10240)) {
#ifdef DIAGNOSTIC
		printf("WARNING: unbelievable rpb_intr_freq: %lu (%d hz)\n",
			(unsigned long)hwrpb->rpb_intr_freq, hz);
#endif
		hz = 1024;
	}
	tick = 1000000 / hz;
	tick_nsec = 1000000000 / hz;
}

void
consinit(void)
{
	/*
	 * Everything related to console initialization is done
	 * in alpha_init().
	 */
}

void
cpu_startup(void)
{
	vaddr_t minaddr, maxaddr;
#if defined(DEBUG)
	extern int pmapdebug;
	int opmapdebug = pmapdebug;

	pmapdebug = 0;
#endif

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf("%s", version);
	identifycpu();
	printf("real mem = %lu (%luMB)\n", ptoa((psize_t)totalphysmem),
	    ptoa((psize_t)totalphysmem) / 1024 / 1024);
	printf("rsvd mem = %lu (%luMB)\n", ptoa((psize_t)resvmem),
	    ptoa((psize_t)resvmem) / 1024 / 1024);
	if (unusedmem) {
		printf("WARNING: unused memory = %lu (%luMB)\n",
		    ptoa((psize_t)unusedmem),
		    ptoa((psize_t)unusedmem) / 1024 / 1024);
	}
	if (unknownmem) {
		printf("WARNING: %lu (%luMB) of memory with unknown purpose\n",
		    ptoa((psize_t)unknownmem),
		    ptoa((psize_t)unknownmem) / 1024 / 1024);
	}

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

#if defined(DEBUG)
	pmapdebug = opmapdebug;
#endif
	printf("avail mem = %lu (%luMB)\n", ptoa((psize_t)uvmexp.free),
	    ptoa((psize_t)uvmexp.free) / 1024 / 1024);
#if 0
	{
		extern u_long pmap_pages_stolen;

		printf("stolen memory for VM structures = %d\n", pmap_pages_stolen * PAGE_SIZE);
	}
#endif

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	/*
	 * Configure the system.
	 */
	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}

	/*
	 * Set up the HWPCB so that it's safe to configure secondary
	 * CPUs.
	 */
	hwrpb_primary_init();
}

/*
 * Retrieve the platform name from the DSR.
 */
const char *
alpha_dsr_sysname(void)
{
	struct dsrdb *dsr;
	const char *sysname;

	/*
	 * DSR does not exist on early HWRPB versions.
	 */
	if (hwrpb->rpb_version < HWRPB_DSRDB_MINVERS)
		return (NULL);

	dsr = (struct dsrdb *)(((caddr_t)hwrpb) + hwrpb->rpb_dsrdb_off);
	sysname = (const char *)((caddr_t)dsr + (dsr->dsr_sysname_off +
	    sizeof(u_int64_t)));
	return (sysname);
}

/*
 * Lookup the system specified system variation in the provided table,
 * returning the model string on match.
 */
const char *
alpha_variation_name(u_int64_t variation,
    const struct alpha_variation_table *avtp)
{
	int i;

	for (i = 0; avtp[i].avt_model != NULL; i++)
		if (avtp[i].avt_variation == variation)
			return (avtp[i].avt_model);
	return (NULL);
}

/*
 * Generate a default platform name based for unknown system variations.
 */
const char *
alpha_unknown_sysname(void)
{
	static char s[128];		/* safe size */

	snprintf(s, sizeof s, "%s family, unknown model variation 0x%lx",
	    platform.family, (unsigned long)hwrpb->rpb_variation & SV_ST_MASK);
	return ((const char *)s);
}

void
identifycpu(void)
{
	char *s;
	int slen;

	/*
	 * print out CPU identification information.
	 */
	printf("%s", cpu_model);
	for(s = cpu_model; *s; ++s)
		if(strncasecmp(s, "MHz", 3) == 0)
			goto skipMHz;
	printf(", %luMHz", (unsigned long)hwrpb->rpb_cc_freq / 1000000);
skipMHz:
	/* fill in hw_serial if a serial number is known */
	slen = strlen(hwrpb->rpb_ssn) + 1;
	if (slen > 1) {
		hw_serial = malloc(slen, M_SYSCTL, M_NOWAIT);
		if (hw_serial)
			strlcpy(hw_serial, (char *)hwrpb->rpb_ssn, slen);
	}

	printf("\n");
	printf("%lu byte page size, %d processor%s.\n",
	    (unsigned long)hwrpb->rpb_page_size, ncpusfound,
	    ncpusfound == 1 ? "" : "s");
#if 0
	/* this is not particularly useful! */
	printf("variation: 0x%lx, revision 0x%lx\n",
	    hwrpb->rpb_variation, *(long *)hwrpb->rpb_revision);
#endif
}

int	waittime = -1;
struct pcb dumppcb;

__dead void
boot(int howto)
{
#if defined(MULTIPROCESSOR)
	u_long wait_mask;
	int i;
#endif

	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	if ((boothowto & RB_HALT) != 0)
		howto |= RB_HALT;

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown(curproc);

		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}
	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

#if defined(MULTIPROCESSOR)
	/*
	 * Halt all other CPUs.
	 */
	wait_mask = (1UL << hwrpb->rpb_primary_cpu_id);
	alpha_broadcast_ipi(ALPHA_IPI_HALT);

	/* Ensure any CPUs paused by DDB resume execution so they can halt */
	cpus_paused = 0;

	for (i = 0; i < 10000; i++) {
		alpha_mb();
		if (cpus_running == wait_mask)
			break;
		delay(1000);
	}
	alpha_mb();
	if (cpus_running != wait_mask)
		printf("WARNING: Unable to halt secondary CPUs (0x%lx)\n",
		    cpus_running);
#endif

	if ((howto & RB_DUMP) != 0)
		dumpsys();

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

#ifdef BOOTKEY
	printf("hit any key to %s...\n",
	    (howto & RB_HALT) != 0 ? "halt" : "reboot");
	cnpollc(1);	/* for proper keyboard command handling */
	cngetc();
	cnpollc(0);
	printf("\n");
#endif

	/* Finally, powerdown/halt/reboot the system. */
	if ((howto & RB_POWERDOWN) != 0 &&
	    platform.powerdown != NULL) {
		(*platform.powerdown)();
		printf("WARNING: powerdown failed!\n");
	}
doreset:
	printf("%s\n\n",
	    (howto & RB_HALT) != 0 ? "halted." : "rebooting...");
	prom_halt((howto & RB_HALT) != 0);
	for (;;)
		continue;
	/* NOTREACHED */
}

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

/*
 * cpu_dumpsize: calculate size of machine-dependent kernel core dump headers.
 */
int
cpu_dumpsize(void)
{
	int size;

	size = ALIGN(sizeof(kcore_seg_t)) + ALIGN(sizeof(cpu_kcore_hdr_t)) +
	    ALIGN(mem_cluster_cnt * sizeof(phys_ram_seg_t));
	if (roundup(size, dbtob(1)) != dbtob(1))
		return -1;

	return (1);
}

/*
 * cpu_dump_mempagecnt: calculate size of RAM (in pages) to be dumped.
 */
u_long
cpu_dump_mempagecnt(void)
{
	u_long i, n;

	n = 0;
	for (i = 0; i < mem_cluster_cnt; i++)
		n += atop(mem_clusters[i].size);
	return (n);
}

/*
 * cpu_dump: dump machine-dependent kernel core dump headers.
 */
int
cpu_dump(void)
{
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	char buf[dbtob(1)];
	kcore_seg_t *segp;
	cpu_kcore_hdr_t *cpuhdrp;
	phys_ram_seg_t *memsegp;
	int i;

	dump = bdevsw[major(dumpdev)].d_dump;

	bzero(buf, sizeof buf);
	segp = (kcore_seg_t *)buf;
	cpuhdrp = (cpu_kcore_hdr_t *)&buf[ALIGN(sizeof(*segp))];
	memsegp = (phys_ram_seg_t *)&buf[ALIGN(sizeof(*segp)) +
	    ALIGN(sizeof(*cpuhdrp))];

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	/*
	 * Add the machine-dependent header info.
	 */
	cpuhdrp->lev1map_pa = ALPHA_K0SEG_TO_PHYS((vaddr_t)kernel_lev1map);
	cpuhdrp->page_size = PAGE_SIZE;
	cpuhdrp->nmemsegs = mem_cluster_cnt;

	/*
	 * Fill in the memory segment descriptors.
	 */
	for (i = 0; i < mem_cluster_cnt; i++) {
		memsegp[i].start = mem_clusters[i].start;
		memsegp[i].size = mem_clusters[i].size & ~PAGE_MASK;
	}

	return (dump(dumpdev, dumplo, (caddr_t)buf, dbtob(1)));
}

/*
 * This is called by main to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks, dumpblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpblks = cpu_dumpsize();
	if (dumpblks < 0)
		return;
	dumpblks += ctod(cpu_dump_mempagecnt());

	/* If dump won't fit (incl. room for possible label), punt. */
	if (dumpblks > (nblks - ctod(1)))
		return;

	/* Put dump at end of partition */
	dumplo = nblks - dumpblks;

	/* dumpsize is in page units, and doesn't include headers. */
	dumpsize = cpu_dump_mempagecnt();
}

/*
 * Dump the kernel's image to the swap partition.
 */
#define	BYTES_PER_DUMP	PAGE_SIZE

void
dumpsys(void)
{
	u_long totalbytesleft, bytes, i, n, memcl;
	u_long maddr;
	int psize;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error;
	extern int msgbufmapped;

	/* Save registers. */
	savectx(&dumppcb);

	msgbufmapped = 0;	/* don't record dump msgs in msgbuf */
	if (dumpdev == NODEV)
		return;

	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo <= 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);

	psize = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (psize == -1) {
		printf("area unavailable\n");
		return;
	}

	/* XXX should purge all outstanding keystrokes. */

	if ((error = cpu_dump()) != 0)
		goto err;

	totalbytesleft = ptoa(cpu_dump_mempagecnt());
	blkno = dumplo + cpu_dumpsize();
	dump = bdevsw[major(dumpdev)].d_dump;
	error = 0;

	for (memcl = 0; memcl < mem_cluster_cnt; memcl++) {
		maddr = mem_clusters[memcl].start;
		bytes = mem_clusters[memcl].size & ~PAGE_MASK;

		for (i = 0; i < bytes; i += n, totalbytesleft -= n) {

			/* Print out how many MBs we to go. */
			if ((totalbytesleft % (1024*1024)) == 0)
				printf("%ld ", totalbytesleft / (1024 * 1024));

			/* Limit size for next transfer. */
			n = bytes - i;
			if (n > BYTES_PER_DUMP)
				n =  BYTES_PER_DUMP;
	
			error = (*dump)(dumpdev, blkno,
			    (caddr_t)ALPHA_PHYS_TO_K0SEG(maddr), n);
			if (error)
				goto err;
			maddr += n;
			blkno += btodb(n);			/* XXX? */

			/* XXX should look for keystrokes, to cancel. */
		}
	}

err:
	switch (error) {
#ifdef DEBUG
	case ENXIO:
		printf("device bad\n");
		break;

	case EFAULT:
		printf("device not ready\n");
		break;

	case EINVAL:
		printf("area improper\n");
		break;

	case EIO:
		printf("i/o error\n");
		break;

	case EINTR:
		printf("aborted from console\n");
		break;
#endif /* DEBUG */
	case 0:
		printf("succeeded\n");
		break;

	default:
		printf("error %d\n", error);
		break;
	}
	printf("\n\n");
	delay(1000);
}

void
frametoreg(struct trapframe *framep, struct reg *regp)
{

	regp->r_regs[R_V0] = framep->tf_regs[FRAME_V0];
	regp->r_regs[R_T0] = framep->tf_regs[FRAME_T0];
	regp->r_regs[R_T1] = framep->tf_regs[FRAME_T1];
	regp->r_regs[R_T2] = framep->tf_regs[FRAME_T2];
	regp->r_regs[R_T3] = framep->tf_regs[FRAME_T3];
	regp->r_regs[R_T4] = framep->tf_regs[FRAME_T4];
	regp->r_regs[R_T5] = framep->tf_regs[FRAME_T5];
	regp->r_regs[R_T6] = framep->tf_regs[FRAME_T6];
	regp->r_regs[R_T7] = framep->tf_regs[FRAME_T7];
	regp->r_regs[R_S0] = framep->tf_regs[FRAME_S0];
	regp->r_regs[R_S1] = framep->tf_regs[FRAME_S1];
	regp->r_regs[R_S2] = framep->tf_regs[FRAME_S2];
	regp->r_regs[R_S3] = framep->tf_regs[FRAME_S3];
	regp->r_regs[R_S4] = framep->tf_regs[FRAME_S4];
	regp->r_regs[R_S5] = framep->tf_regs[FRAME_S5];
	regp->r_regs[R_S6] = framep->tf_regs[FRAME_S6];
	regp->r_regs[R_A0] = framep->tf_regs[FRAME_A0];
	regp->r_regs[R_A1] = framep->tf_regs[FRAME_A1];
	regp->r_regs[R_A2] = framep->tf_regs[FRAME_A2];
	regp->r_regs[R_A3] = framep->tf_regs[FRAME_A3];
	regp->r_regs[R_A4] = framep->tf_regs[FRAME_A4];
	regp->r_regs[R_A5] = framep->tf_regs[FRAME_A5];
	regp->r_regs[R_T8] = framep->tf_regs[FRAME_T8];
	regp->r_regs[R_T9] = framep->tf_regs[FRAME_T9];
	regp->r_regs[R_T10] = framep->tf_regs[FRAME_T10];
	regp->r_regs[R_T11] = framep->tf_regs[FRAME_T11];
	regp->r_regs[R_RA] = framep->tf_regs[FRAME_RA];
	regp->r_regs[R_T12] = framep->tf_regs[FRAME_T12];
	regp->r_regs[R_AT] = framep->tf_regs[FRAME_AT];
	regp->r_regs[R_GP] = framep->tf_regs[FRAME_GP];
	/* regp->r_regs[R_SP] = framep->tf_regs[FRAME_SP]; XXX */
	regp->r_regs[R_ZERO] = 0;
}

void
regtoframe(struct reg *regp, struct trapframe *framep)
{

	framep->tf_regs[FRAME_V0] = regp->r_regs[R_V0];
	framep->tf_regs[FRAME_T0] = regp->r_regs[R_T0];
	framep->tf_regs[FRAME_T1] = regp->r_regs[R_T1];
	framep->tf_regs[FRAME_T2] = regp->r_regs[R_T2];
	framep->tf_regs[FRAME_T3] = regp->r_regs[R_T3];
	framep->tf_regs[FRAME_T4] = regp->r_regs[R_T4];
	framep->tf_regs[FRAME_T5] = regp->r_regs[R_T5];
	framep->tf_regs[FRAME_T6] = regp->r_regs[R_T6];
	framep->tf_regs[FRAME_T7] = regp->r_regs[R_T7];
	framep->tf_regs[FRAME_S0] = regp->r_regs[R_S0];
	framep->tf_regs[FRAME_S1] = regp->r_regs[R_S1];
	framep->tf_regs[FRAME_S2] = regp->r_regs[R_S2];
	framep->tf_regs[FRAME_S3] = regp->r_regs[R_S3];
	framep->tf_regs[FRAME_S4] = regp->r_regs[R_S4];
	framep->tf_regs[FRAME_S5] = regp->r_regs[R_S5];
	framep->tf_regs[FRAME_S6] = regp->r_regs[R_S6];
	framep->tf_regs[FRAME_A0] = regp->r_regs[R_A0];
	framep->tf_regs[FRAME_A1] = regp->r_regs[R_A1];
	framep->tf_regs[FRAME_A2] = regp->r_regs[R_A2];
	framep->tf_regs[FRAME_A3] = regp->r_regs[R_A3];
	framep->tf_regs[FRAME_A4] = regp->r_regs[R_A4];
	framep->tf_regs[FRAME_A5] = regp->r_regs[R_A5];
	framep->tf_regs[FRAME_T8] = regp->r_regs[R_T8];
	framep->tf_regs[FRAME_T9] = regp->r_regs[R_T9];
	framep->tf_regs[FRAME_T10] = regp->r_regs[R_T10];
	framep->tf_regs[FRAME_T11] = regp->r_regs[R_T11];
	framep->tf_regs[FRAME_RA] = regp->r_regs[R_RA];
	framep->tf_regs[FRAME_T12] = regp->r_regs[R_T12];
	framep->tf_regs[FRAME_AT] = regp->r_regs[R_AT];
	framep->tf_regs[FRAME_GP] = regp->r_regs[R_GP];
	/* framep->tf_regs[FRAME_SP] = regp->r_regs[R_SP]; XXX */
	/* ??? = regp->r_regs[R_ZERO]; */
}

void
printregs(struct reg *regp)
{
	int i;

	for (i = 0; i < 32; i++)
		printf("R%d:\t0x%016lx%s", i, regp->r_regs[i],
		   i & 1 ? "\n" : "\t");
}

void
regdump(struct trapframe *framep)
{
	struct reg reg;

	frametoreg(framep, &reg);
	reg.r_regs[R_SP] = alpha_pal_rdusp();

	printf("REGISTERS:\n");
	printregs(&reg);
}

/*
 * Send an interrupt to process.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct sigcontext ksc, *scp;
	struct fpreg *fpregs = (struct fpreg *)&ksc.sc_fpregs;
	struct trapframe *frame;
	unsigned long oldsp;
	int fsize, rndfsize, kscsize;
	siginfo_t *sip;

	oldsp = alpha_pal_rdusp();
	frame = p->p_md.md_tf;
	fsize = sizeof ksc;
	rndfsize = ((fsize + 15) / 16) * 16;
	kscsize = rndfsize;

	if (info) {
		fsize += sizeof *ksip;
		rndfsize = ((fsize + 15) / 16) * 16;
	}

	/*
	 * Allocate space for the signal handler context.
	 */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(oldsp) && onstack)
		scp = (struct sigcontext *)
		    (trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size)
		    - rndfsize);
	else
		scp = (struct sigcontext *)(oldsp - rndfsize);

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	bzero(&ksc, sizeof(ksc));
	ksc.sc_mask = mask;
	ksc.sc_pc = frame->tf_regs[FRAME_PC];
	ksc.sc_ps = frame->tf_regs[FRAME_PS];

	/* copy the registers. */
	frametoreg(frame, (struct reg *)ksc.sc_regs);
	ksc.sc_regs[R_SP] = oldsp;

	/* save the floating-point state, if necessary, then copy it. */
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 1);
	ksc.sc_ownedfp = p->p_md.md_flags & MDP_FPUSED;
	memcpy(/*ksc.sc_*/fpregs, &p->p_addr->u_pcb.pcb_fp,
	    sizeof(struct fpreg));
#ifndef NO_IEEE
	ksc.sc_fp_control = alpha_read_fp_c(p);
#else
	ksc.sc_fp_control = 0;
#endif
	memset(ksc.sc_reserved, 0, sizeof ksc.sc_reserved);	/* XXX */
	memset(ksc.sc_xxx, 0, sizeof ksc.sc_xxx);		/* XXX */

	if (info) {
		sip = (void *)scp + kscsize;
		if (copyout(ksip, (caddr_t)sip, fsize - kscsize) != 0)
			return 1;
	} else
		sip = NULL;

	ksc.sc_cookie = (long)scp ^ p->p_p->ps_sigcookie;
	if (copyout((caddr_t)&ksc, (caddr_t)scp, kscsize) != 0)
		return 1;

	/*
	 * Set up the registers to return to sigcode.
	 */
	frame->tf_regs[FRAME_PC] = p->p_p->ps_sigcode;
	frame->tf_regs[FRAME_A0] = sig;
	frame->tf_regs[FRAME_A1] = (u_int64_t)sip;
	frame->tf_regs[FRAME_A2] = (u_int64_t)scp;
	frame->tf_regs[FRAME_T12] = (u_int64_t)catcher;		/* t12 is pv */
	alpha_pal_wrusp((unsigned long)scp);

	return 0;
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psl to gain improper privileges or to cause
 * a machine fault.
 */
int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext ksc, *scp = SCARG(uap, sigcntxp);
	struct fpreg *fpregs = (struct fpreg *)&ksc.sc_fpregs;
	int error;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	if ((error = copyin(scp, &ksc, sizeof(ksc))) != 0)
		return (error);

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	/*
	 * Restore the user-supplied information
	 */
	p->p_sigmask = ksc.sc_mask &~ sigcantmask;

	p->p_md.md_tf->tf_regs[FRAME_PC] = ksc.sc_pc;
	p->p_md.md_tf->tf_regs[FRAME_PS] =
	    (ksc.sc_ps | ALPHA_PSL_USERSET) & ~ALPHA_PSL_USERCLR;

	regtoframe((struct reg *)ksc.sc_regs, p->p_md.md_tf);
	alpha_pal_wrusp(ksc.sc_regs[R_SP]);

	/* XXX ksc.sc_ownedfp ? */
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);
	memcpy(&p->p_addr->u_pcb.pcb_fp, /*ksc.sc_*/fpregs,
	    sizeof(struct fpreg));
#ifndef NO_IEEE
	p->p_addr->u_pcb.pcb_fp.fpr_cr = ksc.sc_fpcr;
	p->p_md.md_flags = ksc.sc_fp_control & MDP_FP_C;
#endif
	return (EJUSTRETURN);
}

/*
 * machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	dev_t consdev;
#if NIOASIC > 0
	int oldval, ret;
#endif

	if (name[0] != CPU_CHIPSET && namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case CPU_CONSDEV:
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
			sizeof consdev));

#ifndef SMALL_KERNEL
	case CPU_BOOTED_KERNEL:
		return (sysctl_rdstring(oldp, oldlenp, newp,
		    bootinfo.booted_kernel));
	
	case CPU_CHIPSET:
		return (alpha_sysctl_chipset(name + 1, namelen - 1, oldp,
		    oldlenp));
#endif /* SMALL_KERNEL */

#ifndef NO_IEEE
	case CPU_FP_SYNC_COMPLETE:
		return (sysctl_int(oldp, oldlenp, newp, newlen,
		    &alpha_fp_sync_complete));
#endif
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
#if NIOASIC > 0
	case CPU_LED_BLINK:
		oldval = alpha_led_blink;
		ret = sysctl_int(oldp, oldlenp, newp, newlen, &alpha_led_blink);
		if (oldval != alpha_led_blink)
			ioasic_led_blink(NULL);
		return (ret);
#endif
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * Set registers on exec.
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct trapframe *tfp = p->p_md.md_tf;
#ifdef DEBUG
	int i;
#endif

#ifdef DEBUG
	/*
	 * Crash and dump, if the user requested it.
	 */
	if (boothowto & RB_DUMP)
		panic("crash requested by boot flags");
#endif

#ifdef DEBUG
	for (i = 0; i < FRAME_SIZE; i++)
		tfp->tf_regs[i] = 0xbabefacedeadbeef;
	tfp->tf_regs[FRAME_A1] = 0;
#else
	memset(tfp->tf_regs, 0, FRAME_SIZE * sizeof tfp->tf_regs[0]);
#endif
	memset(&p->p_addr->u_pcb.pcb_fp, 0, sizeof p->p_addr->u_pcb.pcb_fp);
	alpha_pal_wrusp(stack);
	tfp->tf_regs[FRAME_PS] = ALPHA_PSL_USERSET;
	tfp->tf_regs[FRAME_PC] = pack->ep_entry & ~3;

	tfp->tf_regs[FRAME_A0] = stack;
	/* a1 and a2 already zeroed */
	tfp->tf_regs[FRAME_T12] = tfp->tf_regs[FRAME_PC];	/* a.k.a. PV */

	p->p_md.md_flags &= ~MDP_FPUSED;
#ifndef NO_IEEE
	if (__predict_true((p->p_md.md_flags & IEEE_INHERIT) == 0)) {
		p->p_md.md_flags &= ~MDP_FP_C;
		p->p_addr->u_pcb.pcb_fp.fpr_cr = FPCR_DYN(FP_RN);
	}
#endif
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);
}

/*
 * Release the FPU.
 */
void
fpusave_cpu(struct cpu_info *ci, int save)
{
	struct proc *p;
#if defined(MULTIPROCESSOR)
	int s;
#endif

	KDASSERT(ci == curcpu());

#if defined(MULTIPROCESSOR)
	/* Need to block IPIs */
	s = splipi();
	atomic_setbits_ulong(&ci->ci_flags, CPUF_FPUSAVE);
#endif

	p = ci->ci_fpcurproc;
	if (p == NULL)
		goto out;

	if (save) {
		alpha_pal_wrfen(1);
		savefpstate(&p->p_addr->u_pcb.pcb_fp);
	}

	alpha_pal_wrfen(0);

	p->p_addr->u_pcb.pcb_fpcpu = NULL;
	ci->ci_fpcurproc = NULL;

out:
#if defined(MULTIPROCESSOR)
	atomic_clearbits_ulong(&ci->ci_flags, CPUF_FPUSAVE);
	alpha_pal_swpipl(s);
#endif
	return;
}

/*
 * Synchronize FP state for this process.
 */
void
fpusave_proc(struct proc *p, int save)
{
	struct cpu_info *ci = curcpu();
	struct cpu_info *oci;
#if defined(MULTIPROCESSOR)
	u_long ipi = save ? ALPHA_IPI_SYNCH_FPU : ALPHA_IPI_DISCARD_FPU;
	int s;
#endif

	KDASSERT(p->p_addr != NULL);

	for (;;) {
#if defined(MULTIPROCESSOR)
		/* Need to block IPIs */
		s = splipi();
#endif

		oci = p->p_addr->u_pcb.pcb_fpcpu;
		if (oci == NULL) {
#if defined(MULTIPROCESSOR)
			alpha_pal_swpipl(s);
#endif
			return;
		}

#if defined(MULTIPROCESSOR)
		if (oci == ci) {
			KASSERT(ci->ci_fpcurproc == p);
			alpha_pal_swpipl(s);
			fpusave_cpu(ci, save);
			return;
		}

		/*
		 * The other cpu may still be running and could have
		 * discarded the fpu context on its own.
		 */
		if (oci->ci_fpcurproc != p) {
			alpha_pal_swpipl(s);
			continue;
		}

		alpha_send_ipi(oci->ci_cpuid, ipi);
		alpha_pal_swpipl(s);

		while (p->p_addr->u_pcb.pcb_fpcpu != NULL)
			CPU_BUSY_CYCLE();
#else
		KASSERT(ci->ci_fpcurproc == p);
		fpusave_cpu(ci, save);
#endif /* MULTIPROCESSOR */

		break;
	}
}

int
spl0(void)
{

	if (ssir) {
		(void) alpha_pal_swpipl(ALPHA_PSL_IPL_SOFT);
		dosoftint();
	}

	return (alpha_pal_swpipl(ALPHA_PSL_IPL_0));
}

/*
 * Wait "n" microseconds.
 */
void
delay(unsigned long n)
{
	unsigned long pcc0, pcc1, curcycle, cycles, usec;

	if (n == 0)
		return;

	pcc0 = alpha_rpcc() & 0xffffffffUL;
	cycles = 0;
	usec = 0;

	while (usec <= n) {
		/*
		 * Get the next CPU cycle count - assumes that we can not
		 * have had more than one 32 bit overflow.
		 */
		pcc1 = alpha_rpcc() & 0xffffffffUL;
		if (pcc1 < pcc0)
			curcycle = (pcc1 + 0x100000000UL) - pcc0;
		else
			curcycle = pcc1 - pcc0;

		/*
		 * We now have the number of processor cycles since we
		 * last checked. Add the current cycle count to the
		 * running total. If it's over cycles_per_usec, increment
		 * the usec counter.
		 */
		cycles += curcycle;
		while (cycles >= cycles_per_usec) {
			usec++;
			cycles -= cycles_per_usec;
		}
		pcc0 = pcc1;
	}
}

int
alpha_pa_access(u_long pa)
{
	int i;

	for (i = 0; i < mem_cluster_cnt; i++) {
		if (pa < mem_clusters[i].start)
			continue;
		if ((pa - mem_clusters[i].start) >=
		    (mem_clusters[i].size & ~PAGE_MASK))
			continue;
		return (mem_clusters[i].size & PAGE_MASK);	/* prot */
	}

	/*
	 * Address is not a memory address.  If we're secure, disallow
	 * access.  Otherwise, grant read/write.
	 */
	if (securelevel > 0)
		return (PROT_NONE);
	else
		return (PROT_READ | PROT_WRITE);
}

/* XXX XXX BEGIN XXX XXX */
paddr_t alpha_XXX_dmamap_or;					/* XXX */
								/* XXX */
paddr_t								/* XXX */
alpha_XXX_dmamap(vaddr_t v)					/* XXX */
{								/* XXX */
								/* XXX */
	return (vtophys(v) | alpha_XXX_dmamap_or);		/* XXX */
}								/* XXX */
/* XXX XXX END XXX XXX */
