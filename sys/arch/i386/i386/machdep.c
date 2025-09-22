/*	$OpenBSD: machdep.c,v 1.678 2025/09/10 00:07:29 jsg Exp $	*/
/*	$NetBSD: machdep.c,v 1.214 1996/11/10 03:16:17 thorpej Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*-
 * Copyright (c) 1993, 1994, 1995, 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/sysctl.h>
#include <sys/syscallargs.h>
#include <sys/core.h>
#include <sys/kcore.h>
#include <sys/sensors.h>

#include <dev/cons.h>
#include <stand/boot/bootarg.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <machine/cpu.h>
#include <machine/cpu_full.h>
#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/kcore.h>
#include <machine/pio.h>
#include <machine/psl.h>
#include <machine/specialreg.h>
#include <machine/biosvar.h>
#include <machine/pte.h>
#ifdef MULTIPROCESSOR
#include <machine/mpbiosvar.h>
#endif /* MULTIPROCESSOR */

#include <dev/isa/isareg.h>
#include <dev/ic/i8042reg.h>
#include <i386/isa/isa_machdep.h>

#include "acpi.h"
#if NACPI > 0
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#endif

#include "apm.h"
#if NAPM > 0
#include <machine/apmvar.h>
#endif

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#endif

#include "isa.h"
#include "isadma.h"
#include "npx.h"
#if NNPX > 0
extern struct proc *npxproc;
#endif

#include "bios.h"

#ifdef HIBERNATE
#include <machine/hibernate_var.h>
#include <sys/hibernate.h>
#endif /* HIBERNATE */

#include "ukbd.h"
#include "pckbc.h"
#if NPCKBC > 0 && NUKBD > 0
#include <dev/ic/pckbcvar.h>
#endif

/* #define MACHDEP_DEBUG */

#ifdef MACHDEP_DEBUG
#define DPRINTF(x...)	do  { printf(x); } while (0)
#else
#define DPRINTF(x...)
#endif	/* MACHDEP_DEBUG */

const int softintr_to_ssir[NSOFTINTR] = {
	SIR_CLOCK,
	SIR_NET,
	SIR_TTY,
};

void	replacesmap(void);
int     intr_handler(struct intrframe *, struct intrhand *);

/* the following is used externally (sysctl_hw) */
char machine[] = MACHINE;

/*
 * switchto vectors
 */
void (*cpu_idle_leave_fcn)(void) = NULL;
void (*cpu_idle_cycle_fcn)(void) = NULL;
void (*cpu_idle_enter_fcn)(void) = NULL;
void (*cpu_suspend_cycle_fcn)(void);


struct uvm_constraint_range  isa_constraint = { 0x0, 0x00ffffffUL };
struct uvm_constraint_range  dma_constraint = { 0x0, 0xffffffffUL };
struct uvm_constraint_range *uvm_md_constraints[] = {
	&isa_constraint,
	&dma_constraint,
	NULL
};

extern int	boothowto;
int	physmem;

struct dumpmem dumpmem[VM_PHYSSEG_MAX];
u_int ndumpmem;

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int	dumpsize = 0;		/* pages */
long	dumplo = 0;		/* blocks */

int	cpu_class;

int	i386_use_fxsave;
int	i386_has_sse;
int	i386_has_sse2;
int	i386_has_xcrypt;

bootarg_t *bootargp;
paddr_t avail_end;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

#if !defined(SMALL_KERNEL)
int p3_early;
void (*update_cpuspeed)(void) = NULL;
void	via_update_sensor(void *args);
#endif
int kbd_reset;
int lid_action = 1;
int pwr_action = 1;
int forceukbd;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int	safepri = 0;

#if !defined(SMALL_KERNEL)
int bus_clock;
#endif
void (*setperf_setup)(struct cpu_info *);
int setperf_prio = 0;		/* for concurrent handlers */

void (*cpusensors_setup)(struct cpu_info *);

void (*delay_func)(int) = i8254_delay;
void (*initclock_func)(void) = i8254_initclocks;
void (*startclock_func)(void) = i8254_start_both_clocks;

/*
 * Extent maps to manage I/O and ISA memory hole space.  Allocate
 * storage for 16 regions in each, initially.  Later, ioport_malloc_safe
 * will indicate that it's safe to use malloc() to dynamically allocate
 * region descriptors.
 *
 * N.B. At least two regions are _always_ allocated from the iomem
 * extent map; (0 -> ISA hole) and (end of ISA hole -> end of RAM).
 *
 * The extent maps are not static!  Machine-dependent ISA and EISA
 * routines need access to them for bus address space allocation.
 */
static	long ioport_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
static	long iomem_ex_storage[EXTENT_FIXED_STORAGE_SIZE(16) / sizeof(long)];
struct	extent *ioport_ex;
struct	extent *iomem_ex;
static	int ioport_malloc_safe;

void	dumpsys(void);
int	cpu_dump(void);
void	init386(paddr_t);
void	consinit(void);
void	(*cpuresetfn)(void);

int	bus_mem_add_mapping(bus_addr_t, bus_size_t,
	    int, bus_space_handle_t *);

#ifdef APERTURE
int allowaperture = 0;
#endif

int has_rdrand;
int has_rdseed;

void	winchip_cpu_setup(struct cpu_info *);
void	amd_family5_setperf_setup(struct cpu_info *);
void	amd_family5_setup(struct cpu_info *);
void	amd_family6_setperf_setup(struct cpu_info *);
void	amd_family6_setup(struct cpu_info *);
void	cyrix3_setperf_setup(struct cpu_info *);
void	cyrix3_cpu_setup(struct cpu_info *);
void	cyrix6x86_cpu_setup(struct cpu_info *);
void	natsem6x86_cpu_setup(struct cpu_info *);
void	intel586_cpu_setup(struct cpu_info *);
void	intel686_cpusensors_setup(struct cpu_info *);
void	intel686_setperf_setup(struct cpu_info *);
void	intel686_common_cpu_setup(struct cpu_info *);
void	intel686_cpu_setup(struct cpu_info *);
void	intel686_p4_cpu_setup(struct cpu_info *);
void	intelcore_update_sensor(void *);
void	tm86_cpu_setup(struct cpu_info *);
char *	intel686_cpu_name(int);
char *	cyrix3_cpu_name(int, int);
char *	tm86_cpu_name(int);
void	cyrix3_get_bus_clock(struct cpu_info *);
void	p4_get_bus_clock(struct cpu_info *);
void	p3_get_bus_clock(struct cpu_info *);
void	p4_update_cpuspeed(void);
void	p3_update_cpuspeed(void);
int	pentium_cpuspeed(int *);
void	enter_shared_special_pages(void);

static __inline u_char
cyrix_read_reg(u_char reg)
{
	outb(0x22, reg);
	return inb(0x23);
}

static __inline void
cyrix_write_reg(u_char reg, u_char data)
{
	outb(0x22, reg);
	outb(0x23, data);
}

/*
 * cpuid instruction.  request in eax, result in eax, ebx, ecx, edx.
 * requires caller to provide u_int32_t regs[4] array.
 */
void
cpuid(u_int32_t ax, u_int32_t *regs)
{
	__asm volatile(
	    "cpuid\n\t"
	    "movl	%%eax, 0(%2)\n\t"
	    "movl	%%ebx, 4(%2)\n\t"
	    "movl	%%ecx, 8(%2)\n\t"
	    "movl	%%edx, 12(%2)\n\t"
	    :"=a" (ax)
	    :"0" (ax), "S" (regs)
	    :"bx", "cx", "dx");
}

/*
 * Machine-dependent startup code
 */
void
cpu_startup(void)
{
	unsigned i;
	vaddr_t minaddr, maxaddr, va;
	paddr_t pa;

	/*
	 * Initialize error message buffer (at end of core).
	 * (space reserved in pmap_bootstrap)
	 */
	pa = avail_end;
	va = (vaddr_t)msgbufp;
	for (i = 0; i < atop(MSGBUFSIZE); i++) {
		pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
	initmsgbuf((caddr_t)msgbufp, round_page(MSGBUFSIZE));

	printf("%s", version);
	startclocks();
	rtcinit();

	printf("real mem  = %llu (%lluMB)\n",
	    (unsigned long long)ptoa((psize_t)physmem),
	    (unsigned long long)ptoa((psize_t)physmem)/1024U/1024U);

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

	printf("avail mem = %llu (%lluMB)\n",
	    (unsigned long long)ptoa((psize_t)uvmexp.free),
	    (unsigned long long)ptoa((psize_t)uvmexp.free)/1024U/1024U);

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
	ioport_malloc_safe = 1;

#ifndef SMALL_KERNEL
	cpu_ucode_setup();
#endif

	/* enter the IDT and trampoline code in the u-k maps */
	enter_shared_special_pages();

	/* initialize CPU0's TSS and GDT and put them in the u-k maps */
	cpu_enter_pages(&cpu_info_full_primary);

#ifdef HIBERNATE
	preallocate_hibernate_memory();
#endif /* HIBERNATE */
}

void
enter_shared_special_pages(void)
{
	extern char __kutext_start[], __kutext_end[], __kernel_kutext_phys[];
	extern char __kudata_start[], __kudata_end[], __kernel_kudata_phys[];
	vaddr_t	va;
	paddr_t	pa;

	/* idt */
	pmap_extract(pmap_kernel(), (vaddr_t)idt, &pa);
	pmap_enter_special((vaddr_t)idt, pa, PROT_READ, 0);

	/* .kutext section */
	va = (vaddr_t)__kutext_start;
	pa = (paddr_t)__kernel_kutext_phys;
	while (va < (vaddr_t)__kutext_end) {
		pmap_enter_special(va, pa, PROT_READ | PROT_EXEC, 0);
		DPRINTF("%s: entered kutext page va 0x%08lx pa 0x%08lx\n",
		    __func__, (unsigned long)va, (unsigned long)pa);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}

	/* .kudata section */
	va = (vaddr_t)__kudata_start;
	pa = (paddr_t)__kernel_kudata_phys;
	while (va < (vaddr_t)__kudata_end) {
		pmap_enter_special(va, pa, PROT_READ | PROT_WRITE, 0);
		DPRINTF("%s: entered kudata page va 0x%08lx pa 0x%08lx\n",
		    __func__, (unsigned long)va, (unsigned long)pa);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}
}

/*
 * Set up proc0's TSS
 */
void
i386_proc0_tss_init(void)
{
	struct pcb *pcb;

	curpcb = pcb = &proc0.p_addr->u_pcb;
	pcb->pcb_cr0 = rcr0();
	pcb->pcb_kstack = (int)proc0.p_addr + USPACE - 16;
	proc0.p_md.md_regs = (struct trapframe *)pcb->pcb_kstack - 1;
}

#ifdef MULTIPROCESSOR
void
i386_init_pcb_tss(struct cpu_info *ci)
{
	struct pcb *pcb = ci->ci_idle_pcb;

	pcb->pcb_cr0 = rcr0();
}
#endif	/* MULTIPROCESSOR */

/*
 * Info for CTL_HW
 */
char	cpu_model[120];

const char *classnames[] = {
	"",
	"486",
	"586",
	"686"
};

const char *modifiers[] = {
	"",
	"OverDrive ",
	"Dual ",
	""
};

const struct cpu_cpuid_nameclass i386_cpuid_cpus[] = {
	{
		"GenuineIntel",
		CPUVENDOR_INTEL,
		"Intel",
		/* Family 4 */
		{ {
			CPUCLASS_486,
			{
				"486DX", "486DX", "486SX", "486DX2", "486SL",
				"486SX2", 0, "486DX2 W/B",
				"486DX4", 0, 0, 0, 0, 0, 0, 0,
				"486"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"Pentium (A-step)", "Pentium (P5)",
				"Pentium (P54C)", "Pentium (P24T)",
				"Pentium/MMX", "Pentium", 0,
				"Pentium (P54C)", "Pentium/MMX",
				0, 0, 0, 0, 0, 0, 0,
				"Pentium"	/* Default */
			},
			intel586_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				"Pentium Pro", "Pentium Pro", 0,
				"Pentium II", "Pentium Pro",
				"Pentium II/Celeron",
				"Celeron",
				"Pentium III",
				"Pentium III",
				"Pentium M",
				"Pentium III Xeon",
				"Pentium III", 0,
				"Pentium M",
				"Core Duo/Solo", 0,
				"Pentium Pro, II or III"	/* Default */
			},
			intel686_cpu_setup
		},
		/* Family 7 */
		{
			CPUCLASS_686,
		} ,
		/* Family 8 */
		{
			CPUCLASS_686,
		} ,
		/* Family 9 */
		{
			CPUCLASS_686,
		} ,
		/* Family A */
		{
			CPUCLASS_686,
		} ,
		/* Family B */
		{
			CPUCLASS_686,
		} ,
		/* Family C */
		{
			CPUCLASS_686,
		} ,
		/* Family D */
		{
			CPUCLASS_686,
		} ,
		/* Family E */
		{
			CPUCLASS_686,
		} ,
		/* Family F */
		{
			CPUCLASS_686,
			{
				"Pentium 4", 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				"Pentium 4"	/* Default */
			},
			intel686_p4_cpu_setup
		} }
	},
	{
		"AuthenticAMD",
		CPUVENDOR_AMD,
		"AMD",
		/* Family 4 */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, "Am486DX2 W/T",
				0, 0, 0, "Am486DX2 W/B",
				"Am486DX4 W/T or Am5x86 W/T 150",
				"Am486DX4 W/B or Am5x86 W/B 150", 0, 0,
				0, 0, "Am5x86 W/T 133/160",
				"Am5x86 W/B 133/160",
				"Am486 or Am5x86"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"K5", "K5", "K5", "K5", 0, 0, "K6",
				"K6", "K6-2", "K6-III", 0, 0, 0,
				"K6-2+/III+", 0, 0,
				"K5 or K6"		/* Default */
			},
			amd_family5_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				0, "Athlon Model 1", "Athlon Model 2",
				"Duron Model 3",
				"Athlon Model 4",
				0, "Athlon XP Model 6",
				"Duron Model 7",
				"Athlon XP Model 8",
				0, "Athlon XP Model 10",
				0, 0, 0, 0, 0,
				"K7"		/* Default */
			},
			amd_family6_setup
		},
		/* Family 7 */
		{
			CPUCLASS_686,
		} ,
		/* Family 8 */
		{
			CPUCLASS_686,
		} ,
		/* Family 9 */
		{
			CPUCLASS_686,
		} ,
		/* Family A */
		{
			CPUCLASS_686,
		} ,
		/* Family B */
		{
			CPUCLASS_686,
		} ,
		/* Family C */
		{
			CPUCLASS_686,
		} ,
		/* Family D */
		{
			CPUCLASS_686,
		} ,
		/* Family E */
		{
			CPUCLASS_686,
		} ,
		/* Family F */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, "Athlon64",
				"Opteron or Athlon64FX", 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"AMD64"			/* DEFAULT */
			},
			amd_family6_setup
		} }
	},
	{
		"CyrixInstead",
		CPUVENDOR_CYRIX,
		"Cyrix",
		/* Family 4 */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, "MediaGX", 0, 0, 0, 0, "5x86", 0, 0,
				0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, "6x86", 0, "GXm", 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			cyrix6x86_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				"6x86MX", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0,
				"686 class"	/* Default */
			},
			NULL
		} }
	},
	{
		"CentaurHauls",
		CPUVENDOR_IDT,
		"IDT",
		/* Family 4, not available from IDT */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "WinChip C6", 0, 0, 0,
				"WinChip 2", "WinChip 3", 0, 0, 0, 0, 0, 0,
				"WinChip"		/* Default */
			},
			winchip_cpu_setup
		},
		/* Family 6 */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, 0, 0,
				"C3 Samuel",
				"C3 Samuel 2/Ezra",
				"C3 Ezra-T",
				"C3 Nehemiah", "C3 Esther", 0, 0, 0, 0, 0,
				"C3"		/* Default */
			},
			cyrix3_cpu_setup
		} }
	},
	{
		"GenuineTMx86",
		CPUVENDOR_TRANSMETA,
		"Transmeta",
		/* Family 4, not available from Transmeta */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"		/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "TMS5x00", 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0,
				"TMS5x00"		/* Default */
			},
			tm86_cpu_setup
		},
		/* Family 6, not yet available from Transmeta */
		{
			CPUCLASS_686,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"686 class"		/* Default */
			},
			NULL
		},
		/* Family 7 */
		{
			CPUCLASS_686,
		} ,
		/* Family 8 */
		{
			CPUCLASS_686,
		} ,
		/* Family 9 */
		{
			CPUCLASS_686,
		} ,
		/* Family A */
		{
			CPUCLASS_686,
		} ,
		/* Family B */
		{
			CPUCLASS_686,
		} ,
		/* Family C */
		{
			CPUCLASS_686,
		} ,
		/* Family D */
		{
			CPUCLASS_686,
		} ,
		/* Family E */
		{
			CPUCLASS_686,
		} ,
		/* Family F */
		{
			/* Extended processor family - Transmeta Efficeon */
			CPUCLASS_686,
			{
				0, 0, "TM8000", "TM8000",
				0, 0, 0, 0,
				0, 0, 0, 0,
				0, 0, 0, 0,
				"TM8000"	/* Default */
			},
			tm86_cpu_setup
		} }
	},
	{
		"Geode by NSC",
		CPUVENDOR_NS,
		"National Semiconductor",
		/* Family 4, not available from National Semiconductor */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				0, 0, 0, 0, "Geode GX1", 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			natsem6x86_cpu_setup
		} }
	},
	{
		"SiS SiS SiS ",
		CPUVENDOR_SIS,
		"SiS",
		/* Family 4, not available from SiS */
		{ {
			CPUCLASS_486,
			{
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0,
				"486 class"	/* Default */
			},
			NULL
		},
		/* Family 5 */
		{
			CPUCLASS_586,
			{
				"SiS55x", 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0,
				"586 class"	/* Default */
			},
			NULL
		} }
	}
};

const struct cpu_cpuid_feature i386_cpuid_features[] = {
	{ CPUID_FPU,	"FPU" },
	{ CPUID_VME,	"V86" },
	{ CPUID_DE,	"DE" },
	{ CPUID_PSE,	"PSE" },
	{ CPUID_TSC,	"TSC" },
	{ CPUID_MSR,	"MSR" },
	{ CPUID_PAE,	"PAE" },
	{ CPUID_MCE,	"MCE" },
	{ CPUID_CX8,	"CX8" },
	{ CPUID_APIC,	"APIC" },
	{ CPUID_SYS1,	"SYS" },
	{ CPUID_SEP,	"SEP" },
	{ CPUID_MTRR,	"MTRR" },
	{ CPUID_PGE,	"PGE" },
	{ CPUID_MCA,	"MCA" },
	{ CPUID_CMOV,	"CMOV" },
	{ CPUID_PAT,	"PAT" },
	{ CPUID_PSE36,	"PSE36" },
	{ CPUID_PSN,	"PSN" },
	{ CPUID_CFLUSH,	"CFLUSH" },
	{ CPUID_DS,	"DS" },
	{ CPUID_ACPI,	"ACPI" },
	{ CPUID_MMX,	"MMX" },
	{ CPUID_FXSR,	"FXSR" },
	{ CPUID_SSE,	"SSE" },
	{ CPUID_SSE2,	"SSE2" },
	{ CPUID_SS,	"SS" },
	{ CPUID_HTT,	"HTT" },
	{ CPUID_TM,	"TM" },
	{ CPUID_PBE,	"PBE" }
};

const struct cpu_cpuid_feature i386_ecpuid_features[] = {
	{ CPUID_MPC,		"MPC" },
	{ CPUID_NXE,		"NXE" },
	{ CPUID_MMXX,		"MMXX" },
	{ CPUID_FFXSR,		"FFXSR" },
	{ CPUID_PAGE1GB,	"PAGE1GB" },
	{ CPUID_RDTSCP,		"RDTSCP" },
	{ CPUID_LONG,		"LONG" },
	{ CPUID_3DNOW2,		"3DNOW2" },
	{ CPUID_3DNOW,		"3DNOW" }
};

const struct cpu_cpuid_feature i386_cpuid_ecxfeatures[] = {
	{ CPUIDECX_SSE3,	"SSE3" },
	{ CPUIDECX_PCLMUL,	"PCLMUL" },
	{ CPUIDECX_DTES64,	"DTES64" },
	{ CPUIDECX_MWAIT,	"MWAIT" },
	{ CPUIDECX_DSCPL,	"DS-CPL" },
	{ CPUIDECX_VMX,		"VMX" },
	{ CPUIDECX_SMX,		"SMX" },
	{ CPUIDECX_EST,		"EST" },
	{ CPUIDECX_TM2,		"TM2" },
	{ CPUIDECX_SSSE3,	"SSSE3" },
	{ CPUIDECX_CNXTID,	"CNXT-ID" },
	{ CPUIDECX_SDBG,	"SDBG" },
	{ CPUIDECX_FMA3,	"FMA3" },
	{ CPUIDECX_CX16,	"CX16" },
	{ CPUIDECX_XTPR,	"xTPR" },
	{ CPUIDECX_PDCM,	"PDCM" },
	{ CPUIDECX_PCID,	"PCID" },
	{ CPUIDECX_DCA,		"DCA" },
	{ CPUIDECX_SSE41,	"SSE4.1" },
	{ CPUIDECX_SSE42,	"SSE4.2" },
	{ CPUIDECX_X2APIC,	"x2APIC" },
	{ CPUIDECX_MOVBE,	"MOVBE" },
	{ CPUIDECX_POPCNT,	"POPCNT" },
	{ CPUIDECX_DEADLINE,	"DEADLINE" },
	{ CPUIDECX_AES,		"AES" },
	{ CPUIDECX_XSAVE,	"XSAVE" },
	{ CPUIDECX_OSXSAVE,	"OSXSAVE" },
	{ CPUIDECX_AVX,		"AVX" },
	{ CPUIDECX_F16C,	"F16C" },
	{ CPUIDECX_RDRAND,	"RDRAND" },
	{ CPUIDECX_HV,		"HV" },
};

const struct cpu_cpuid_feature i386_ecpuid_ecxfeatures[] = {
	{ CPUIDECX_LAHF,	"LAHF" },
	{ CPUIDECX_CMPLEG,	"CMPLEG" },
	{ CPUIDECX_SVM,		"SVM" },
	{ CPUIDECX_EAPICSP,	"EAPICSP" },
	{ CPUIDECX_AMCR8,	"AMCR8" },
	{ CPUIDECX_ABM,		"ABM" },
	{ CPUIDECX_SSE4A,	"SSE4A" },
	{ CPUIDECX_MASSE,	"MASSE" },
	{ CPUIDECX_3DNOWP,	"3DNOWP" },
	{ CPUIDECX_OSVW,	"OSVW" },
	{ CPUIDECX_IBS,		"IBS" },
	{ CPUIDECX_XOP,		"XOP" },
	{ CPUIDECX_SKINIT,	"SKINIT" },
	{ CPUIDECX_WDT,		"WDT" },
	{ CPUIDECX_LWP,		"LWP" },
	{ CPUIDECX_FMA4,	"FMA4" },
	{ CPUIDECX_TCE,		"TCE" },
	{ CPUIDECX_NODEID,	"NODEID" },
	{ CPUIDECX_TBM,		"TBM" },
	{ CPUIDECX_TOPEXT,	"TOPEXT" },
	{ CPUIDECX_CPCTR,	"CPCTR" },
	{ CPUIDECX_DBKP,	"DBKP" },
	{ CPUIDECX_PERFTSC,	"PERFTSC" },
	{ CPUIDECX_PCTRL3,	"PCTRL3" },
	{ CPUIDECX_MWAITX,	"MWAITX" },
};

const struct cpu_cpuid_feature cpu_seff0_ebxfeatures[] = {
	{ SEFF0EBX_FSGSBASE,	"FSGSBASE" },
	{ SEFF0EBX_TSC_ADJUST,	"TSC_ADJUST" },
	{ SEFF0EBX_SGX,		"SGX" },
	{ SEFF0EBX_BMI1,	"BMI1" },
	{ SEFF0EBX_HLE,		"HLE" },
	{ SEFF0EBX_AVX2,	"AVX2" },
	{ SEFF0EBX_SMEP,	"SMEP" },
	{ SEFF0EBX_BMI2,	"BMI2" },
	{ SEFF0EBX_ERMS,	"ERMS" },
	{ SEFF0EBX_INVPCID,	"INVPCID" },
	{ SEFF0EBX_RTM,		"RTM" },
	{ SEFF0EBX_PQM,		"PQM" },
	{ SEFF0EBX_MPX,		"MPX" },
	{ SEFF0EBX_AVX512F,	"AVX512F" },
	{ SEFF0EBX_AVX512DQ,	"AVX512DQ" },
	{ SEFF0EBX_RDSEED,	"RDSEED" },
	{ SEFF0EBX_ADX,		"ADX" },
	{ SEFF0EBX_SMAP,	"SMAP" },
	{ SEFF0EBX_AVX512IFMA,	"AVX512IFMA" },
	{ SEFF0EBX_PCOMMIT,	"PCOMMIT" },
	{ SEFF0EBX_CLFLUSHOPT,	"CLFLUSHOPT" },
	{ SEFF0EBX_CLWB,	"CLWB" },
	{ SEFF0EBX_PT,		"PT" },
	{ SEFF0EBX_AVX512PF,	"AVX512PF" },
	{ SEFF0EBX_AVX512ER,	"AVX512ER" },
	{ SEFF0EBX_AVX512CD,	"AVX512CD" },
	{ SEFF0EBX_SHA,		"SHA" },
	{ SEFF0EBX_AVX512BW,	"AVX512BW" },
	{ SEFF0EBX_AVX512VL,	"AVX512VL" },
};

const struct cpu_cpuid_feature cpu_seff0_ecxfeatures[] = {
	{ SEFF0ECX_PREFETCHWT1,	"PREFETCHWT1" },
	{ SEFF0ECX_UMIP,	"UMIP" },
	{ SEFF0ECX_AVX512VBMI,	"AVX512VBMI" },
	{ SEFF0ECX_PKU,		"PKU" },
	{ SEFF0ECX_WAITPKG,	"WAITPKG" },
};

const struct cpu_cpuid_feature cpu_seff0_edxfeatures[] = {
	{ SEFF0EDX_AVX512_4FNNIW, "AVX512FNNIW" },
	{ SEFF0EDX_AVX512_4FMAPS, "AVX512FMAPS" },
	{ SEFF0EDX_SRBDS_CTRL,	"SRBDS_CTRL" },
	{ SEFF0EDX_MD_CLEAR,	"MD_CLEAR" },
	{ SEFF0EDX_TSXFA,	"TSXFA" },
	{ SEFF0EDX_IBRS,	"IBRS,IBPB" },
	{ SEFF0EDX_STIBP,	"STIBP" },
	{ SEFF0EDX_L1DF,	"L1DF" },
	 /* SEFF0EDX_ARCH_CAP (not printed) */
	{ SEFF0EDX_SSBD,	"SSBD" },
};

const struct cpu_cpuid_feature cpu_tpm_eaxfeatures[] = {
	{ TPM_SENSOR,		"SENSOR" },
	{ TPM_ARAT,		"ARAT" },
};

const struct cpu_cpuid_feature i386_cpuid_eaxperf[] = {
	{ CPUIDEAX_VERID,	"PERF" },
};

const struct cpu_cpuid_feature i386_cpuid_edxapmi[] = {
	{ CPUIDEDX_ITSC,	"ITSC" },
};

const struct cpu_cpuid_feature cpu_xsave_extfeatures[] = {
	{ XSAVE_XSAVEOPT,	"XSAVEOPT" },
	{ XSAVE_XSAVEC,		"XSAVEC" },
	{ XSAVE_XGETBV1,	"XGETBV1" },
	{ XSAVE_XSAVES,		"XSAVES" },
};

void
winchip_cpu_setup(struct cpu_info *ci)
{

	switch ((ci->ci_signature >> 4) & 15) { /* model */
	case 4: /* WinChip C6 */
		ci->ci_feature_flags &= ~CPUID_TSC;
		/* Disable RDTSC instruction from user-level. */
		lcr4(rcr4() | CR4_TSD);
		printf("%s: TSC disabled\n", ci->ci_dev->dv_xname);
		break;
	}
}

#if !defined(SMALL_KERNEL)
void
cyrix3_setperf_setup(struct cpu_info *ci)
{
	if (cpu_ecxfeature & CPUIDECX_EST) {
		if (rdmsr(MSR_MISC_ENABLE) & (1 << 16))
			est_init(ci, CPUVENDOR_VIA);
		else
			printf("%s: Enhanced SpeedStep disabled by BIOS\n",
			    ci->ci_dev->dv_xname);
	}
}
#endif

void
cyrix3_cpu_setup(struct cpu_info *ci)
{
	int model = (ci->ci_signature >> 4) & 15;
	int step = ci->ci_signature & 15;

	u_int64_t msreg;
	u_int32_t regs[4];
	unsigned int val;
#if !defined(SMALL_KERNEL)
	extern void (*pagezero)(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	pagezero = i686_pagezero;

	setperf_setup = cyrix3_setperf_setup;
#endif

	switch (model) {
	/* Possible earlier models */
	case 0: case 1: case 2:
	case 3: case 4: case 5:
		break;

	case 6: /* C3 Samuel 1 */
	case 7: /* C3 Samuel 2 or C3 Ezra */
	case 8: /* C3 Ezra-T */
		cpuid(0x80000001, regs);
		val = regs[3];
		if (val & (1U << 31)) {
			cpu_feature |= CPUID_3DNOW;
		} else {
			cpu_feature &= ~CPUID_3DNOW;
		}
		break;

	case 9:
		if (step < 3)
			break;
		/*
		 * C3 Nehemiah & later: fall through.
		 */

	case 10: /* C7-M Type A */
	case 13: /* C7-M Type D */
	case 15: /* Nano */
#if !defined(SMALL_KERNEL)
		if (CPU_IS_PRIMARY(ci) &&
		    (model == 10 || model == 13 || model == 15)) {
			/* Setup the sensors structures */
			strlcpy(ci->ci_sensordev.xname, ci->ci_dev->dv_xname,
			    sizeof(ci->ci_sensordev.xname));
			ci->ci_sensor.type = SENSOR_TEMP;
			sensor_task_register(ci, via_update_sensor, 5);
			sensor_attach(&ci->ci_sensordev, &ci->ci_sensor);
			sensordev_install(&ci->ci_sensordev);
		}
#endif

	default:
		/*
		 * C3 Nehemiah/Esther & later models:
		 * First we check for extended feature flags, and then
		 * (if present) retrieve the ones at 0xC0000001.  In this
		 * bit 2 tells us if the RNG is present.  Bit 3 tells us
		 * if the RNG has been enabled.  In order to use the RNG
		 * we need 3 things:  We need an RNG, we need the FXSR bit
		 * enabled in cr4 (SSE/SSE2 stuff), and we need to have
		 * Bit 6 of MSR 0x110B set to 1 (the default), which will
		 * show up as bit 3 set here.
		 */
		cpuid(0xC0000000, regs); /* Check for RNG */
		val = regs[0];
		if (val >= 0xC0000001) {
			cpuid(0xC0000001, regs);
			val = regs[3];
		} else
			val = 0;

		if (val & (C3_CPUID_HAS_RNG | C3_CPUID_HAS_ACE))
			printf("%s:", ci->ci_dev->dv_xname);

		/* Enable RNG if present and disabled */
		if (val & C3_CPUID_HAS_RNG) {
			extern int viac3_rnd_present;

			if (!(val & C3_CPUID_DO_RNG)) {
				msreg = rdmsr(0x110B);
				msreg |= 0x40;
				wrmsr(0x110B, msreg);
			}
			viac3_rnd_present = 1;
			printf(" RNG");
		}

		/* Enable AES engine if present and disabled */
		if (val & C3_CPUID_HAS_ACE) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_ACE)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28);
				wrmsr(0x1107, msreg);
			}
			i386_has_xcrypt |= C3_HAS_AES;
#endif /* CRYPTO */
			printf(" AES");
		}

		/* Enable ACE2 engine if present and disabled */
		if (val & C3_CPUID_HAS_ACE2) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_ACE2)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28);
				wrmsr(0x1107, msreg);
			}
			i386_has_xcrypt |= C3_HAS_AESCTR;
#endif /* CRYPTO */
			printf(" AES-CTR");
		}

		/* Enable SHA engine if present and disabled */
		if (val & C3_CPUID_HAS_PHE) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_PHE)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28/**/);
				wrmsr(0x1107, msreg);
			}
			i386_has_xcrypt |= C3_HAS_SHA;
#endif /* CRYPTO */
			printf(" SHA1 SHA256");
		}

		/* Enable MM engine if present and disabled */
		if (val & C3_CPUID_HAS_PMM) {
#ifdef CRYPTO
			if (!(val & C3_CPUID_DO_PMM)) {
				msreg = rdmsr(0x1107);
				msreg |= (0x01 << 28/**/);
				wrmsr(0x1107, msreg);
			}
			i386_has_xcrypt |= C3_HAS_MM;
#endif /* CRYPTO */
			printf(" RSA");
		}

		printf("\n");
		break;
	}
}

#if !defined(SMALL_KERNEL)
void
via_update_sensor(void *args)
{
	struct cpu_info *ci = (struct cpu_info *) args;
	u_int64_t msr;

	switch (ci->ci_model) {
	case 0xa:
	case 0xd:
		msr = rdmsr(MSR_C7M_TMTEMPERATURE);
		break;
	case 0xf:
		msr = rdmsr(MSR_CENT_TMTEMPERATURE);
		break;
	}
	ci->ci_sensor.value = (msr & 0xffffff);
	/* micro degrees */
	ci->ci_sensor.value *= 1000000;
	ci->ci_sensor.value += 273150000;
	ci->ci_sensor.flags &= ~SENSOR_FINVALID;
}
#endif

void
cyrix6x86_cpu_setup(struct cpu_info *ci)
{
	extern int clock_broken_latch;

	switch ((ci->ci_signature >> 4) & 15) { /* model */
	case -1: /* M1 w/o cpuid */
	case 2:	/* M1 */
		/* set up various cyrix registers */
		/* Enable suspend on halt */
		cyrix_write_reg(0xc2, cyrix_read_reg(0xc2) | 0x08);
		/* enable access to ccr4/ccr5 */
		cyrix_write_reg(0xC3, cyrix_read_reg(0xC3) | 0x10);
		/* cyrix's workaround  for the "coma bug" */
		cyrix_write_reg(0x31, cyrix_read_reg(0x31) | 0xf8);
		cyrix_write_reg(0x32, cyrix_read_reg(0x32) | 0x7f);
		cyrix_read_reg(0x33); cyrix_write_reg(0x33, 0);
		cyrix_write_reg(0x3c, cyrix_read_reg(0x3c) | 0x87);
		/* disable access to ccr4/ccr5 */
		cyrix_write_reg(0xC3, cyrix_read_reg(0xC3) & ~0x10);

		printf("%s: xchg bug workaround performed\n",
		    ci->ci_dev->dv_xname);
		break;	/* fallthrough? */
	case 4:	/* GXm */
		/* Unset the TSC bit until calibrate_delay() gets fixed. */
		clock_broken_latch = 1;
		curcpu()->ci_feature_flags &= ~CPUID_TSC;
		printf("%s: TSC disabled\n", ci->ci_dev->dv_xname);
		break;
	}
}

void
natsem6x86_cpu_setup(struct cpu_info *ci)
{
	extern int clock_broken_latch;
	int model = (ci->ci_signature >> 4) & 15;

	clock_broken_latch = 1;
	switch (model) {
	case 4:
		cpu_feature &= ~CPUID_TSC;
		printf("%s: TSC disabled\n", ci->ci_dev->dv_xname);
		break;
	}
}

void
intel586_cpu_setup(struct cpu_info *ci)
{
	if (!cpu_f00f_bug) {
		fix_f00f();
		printf("%s: F00F bug workaround installed\n",
		    ci->ci_dev->dv_xname);
	}
}

#if !defined(SMALL_KERNEL)
void
amd_family5_setperf_setup(struct cpu_info *ci)
{
	k6_powernow_init();
}
#endif

void
amd_family5_setup(struct cpu_info *ci)
{
	int model = (ci->ci_signature >> 4) & 15;

	switch (model) {
	case 0:		/* AMD-K5 Model 0 */
		/*
		 * According to the AMD Processor Recognition App Note,
		 * the AMD-K5 Model 0 uses the wrong bit to indicate
		 * support for global PTEs, instead using bit 9 (APIC)
		 * rather than bit 13 (i.e. "0x200" vs. 0x2000".  Oops!).
		 */
		if (cpu_feature & CPUID_APIC)
			cpu_feature = (cpu_feature & ~CPUID_APIC) | CPUID_PGE;
		/*
		 * XXX But pmap_pg_g is already initialized -- need to kick
		 * XXX the pmap somehow.  How does the MP branch do this?
		 */
		break;
	case 12:
	case 13:
#if !defined(SMALL_KERNEL)
		setperf_setup = amd_family5_setperf_setup;
#endif
		break;
	}
}

#if !defined(SMALL_KERNEL)
void
amd_family6_setperf_setup(struct cpu_info *ci)
{
	int family = (ci->ci_signature >> 8) & 15;

	switch (family) {
	case 6:
		k7_powernow_init();
		break;
	case 15:
		k8_powernow_init();
		break;
	}
	if (ci->ci_family >= 0x10)
		k1x_init(ci);
}
#endif

void
amd_family6_setup(struct cpu_info *ci)
{
#if !defined(SMALL_KERNEL)
	int family = (ci->ci_signature >> 8) & 15;
	extern void (*pagezero)(void *, size_t);
	extern void sse2_pagezero(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	if (cpu_feature & CPUID_SSE2)
		pagezero = sse2_pagezero;
	else
		pagezero = i686_pagezero;

	setperf_setup = amd_family6_setperf_setup;

	if (family == 0xf) {
		amd64_errata(ci);
	}
#endif
}

#if !defined(SMALL_KERNEL)
/*
 * Temperature read on the CPU is relative to the maximum
 * temperature supported by the CPU, Tj(Max).
 * Refer to:
 * 64-ia-32-architectures-software-developer-vol-3c-part-3-manual.pdf
 * Section 35 and
 * http://www.intel.com/content/dam/www/public/us/en/documents/
 * white-papers/cpu-monitoring-dts-peci-paper.pdf
 *
 * The temperature on Intel CPUs can be between 70 and 105 degC, since
 * Westmere we can read the TJmax from the die. For older CPUs we have
 * to guess or use undocumented MSRs. Then we subtract the temperature
 * portion of thermal status from max to get current temperature.
 */
void
intelcore_update_sensor(void *args)
{
	struct cpu_info *ci = (struct cpu_info *) args;
	u_int64_t msr;
	int max = 100;

	/* Only some Core family chips have MSR_TEMPERATURE_TARGET. */
	if (ci->ci_model == 0x0e &&
	    (rdmsr(MSR_TEMPERATURE_TARGET_UNDOCUMENTED) &
	     MSR_TEMPERATURE_TARGET_LOW_BIT_UNDOCUMENTED))
		max = 85;

	/*
	 * Newer CPUs can tell you what their max temperature is.
	 * See: '64-ia-32-architectures-software-developer-
	 * vol-3c-part-3-manual.pdf'
	 */
	if (ci->ci_model > 0x17 && ci->ci_model != 0x1c &&
	    ci->ci_model != 0x26 && ci->ci_model != 0x27 &&
	    ci->ci_model != 0x35 && ci->ci_model != 0x36)
		max = MSR_TEMPERATURE_TARGET_TJMAX(
		    rdmsr(MSR_TEMPERATURE_TARGET));

	msr = rdmsr(MSR_THERM_STATUS);
	if (msr & MSR_THERM_STATUS_VALID_BIT) {
		ci->ci_sensor.value = max - MSR_THERM_STATUS_TEMP(msr);
		/* micro degrees */
		ci->ci_sensor.value *= 1000000;
		/* kelvin */
		ci->ci_sensor.value += 273150000;
		ci->ci_sensor.flags &= ~SENSOR_FINVALID;
	} else {
		ci->ci_sensor.value = 0;
		ci->ci_sensor.flags |= SENSOR_FINVALID;
	}
}

void
intel686_cpusensors_setup(struct cpu_info *ci)
{
	if (!CPU_IS_PRIMARY(ci) || (ci->ci_feature_tpmflags & TPM_SENSOR) == 0)
		return;

	/* Setup the sensors structures */
	strlcpy(ci->ci_sensordev.xname, ci->ci_dev->dv_xname,
	    sizeof(ci->ci_sensordev.xname));
	ci->ci_sensor.type = SENSOR_TEMP;
	sensor_task_register(ci, intelcore_update_sensor, 5);
	sensor_attach(&ci->ci_sensordev, &ci->ci_sensor);
	sensordev_install(&ci->ci_sensordev);
}
#endif

#if !defined(SMALL_KERNEL)
void
intel686_setperf_setup(struct cpu_info *ci)
{
	int family = (ci->ci_signature >> 8) & 15;
	int step = ci->ci_signature & 15;

	if (cpu_ecxfeature & CPUIDECX_EST) {
		if (rdmsr(MSR_MISC_ENABLE) & (1 << 16))
			est_init(ci, CPUVENDOR_INTEL);
		else
			printf("%s: Enhanced SpeedStep disabled by BIOS\n",
			    ci->ci_dev->dv_xname);
	} else if ((cpu_feature & (CPUID_ACPI | CPUID_TM)) ==
	    (CPUID_ACPI | CPUID_TM))
		p4tcc_init(family, step);
}
#endif

void
intel686_common_cpu_setup(struct cpu_info *ci)
{

#if !defined(SMALL_KERNEL)
	setperf_setup = intel686_setperf_setup;
	cpusensors_setup = intel686_cpusensors_setup;
	{
	extern void (*pagezero)(void *, size_t);
	extern void sse2_pagezero(void *, size_t);
	extern void i686_pagezero(void *, size_t);

	if (cpu_feature & CPUID_SSE2)
		pagezero = sse2_pagezero;
	else
		pagezero = i686_pagezero;
	}
#endif
	/*
	 * Make sure SYSENTER is disabled.
	 */
	if (cpu_feature & CPUID_SEP)
		wrmsr(MSR_SYSENTER_CS, 0);
}

void
intel686_cpu_setup(struct cpu_info *ci)
{
	int model = (ci->ci_signature >> 4) & 15;
	int step = ci->ci_signature & 15;
	u_quad_t msr119;

	intel686_common_cpu_setup(ci);

	/*
	 * Original PPro returns SYSCALL in CPUID but is non-functional.
	 * From Intel Application Note #485.
	 */
	if ((model == 1) && (step < 3))
		ci->ci_feature_flags &= ~CPUID_SEP;

	/*
	 * Disable the Pentium3 serial number.
	 */
	if ((model == 7) && (ci->ci_feature_flags & CPUID_PSN)) {
		msr119 = rdmsr(MSR_BBL_CR_CTL);
		msr119 |= 0x0000000000200000LL;
		wrmsr(MSR_BBL_CR_CTL, msr119);

		printf("%s: disabling processor serial number\n",
			 ci->ci_dev->dv_xname);
		ci->ci_feature_flags &= ~CPUID_PSN;
		ci->ci_level = 2;
	}

#if !defined(SMALL_KERNEL)
	p3_early = (model == 8 && step == 1) ? 1 : 0;
	update_cpuspeed = p3_update_cpuspeed;
#endif
}

void
intel686_p4_cpu_setup(struct cpu_info *ci)
{
	intel686_common_cpu_setup(ci);

#if !defined(SMALL_KERNEL)
	update_cpuspeed = p4_update_cpuspeed;
#endif
}

void
tm86_cpu_setup(struct cpu_info *ci)
{
#if !defined(SMALL_KERNEL)
	longrun_init();
#endif
}

char *
intel686_cpu_name(int model)
{
	char *ret = NULL;

	switch (model) {
	case 5:
		switch (cpu_cache_edx & 0xFF) {
		case 0x40:
		case 0x41:
			ret = "Celeron";
			break;
		/* 0x42 should not exist in this model. */
		case 0x43:
			ret = "Pentium II";
			break;
		case 0x44:
		case 0x45:
			ret = "Pentium II Xeon";
			break;
		}
		break;
	case 7:
		switch (cpu_cache_edx & 0xFF) {
		/* 0x40 - 0x42 should not exist in this model. */
		case 0x43:
			ret = "Pentium III";
			break;
		case 0x44:
		case 0x45:
			ret = "Pentium III Xeon";
			break;
		}
		break;
	}

	return (ret);
}

char *
cyrix3_cpu_name(int model, int step)
{
	char	*name = NULL;

	switch (model) {
	case 7:
		if (step < 8)
			name = "C3 Samuel 2";
		else
			name = "C3 Ezra";
		break;
	}
	return name;
}

/*
 * Print identification for the given CPU.
 * XXX XXX
 * This is not as clean as one might like, because it references
 *
 * the "cpuid_level" and "cpu_vendor" globals.
 * cpuid_level isn't so bad, since both CPU's will hopefully
 * be of the same level.
 *
 * The Intel multiprocessor spec doesn't give us the cpu_vendor
 * information; however, the chance of multi-vendor SMP actually
 * ever *working* is sufficiently low that it's probably safe to assume
 * all processors are of the same vendor.
 */
void
identifycpu(struct cpu_info *ci)
{
	const char *name, *modifier, *vendorname, *token;
	int class = CPUCLASS_486, vendor, i, max;
	int family, model, step, modif, cachesize;
	const struct cpu_cpuid_nameclass *cpup = NULL;
	char *brandstr_from, *brandstr_to;
	char *cpu_device = ci->ci_dev->dv_xname;
	int skipspace;
	extern uint32_t cpu_meltdown;
	uint64_t msr, nmsr;

	if (cpuid_level == -1) {
		name = "486DX";
		vendor = CPUVENDOR_INTEL;
		vendorname = "Intel";
		model = -1;
		step = -1;
		class = CPUCLASS_486;
		ci->cpu_setup = NULL;
		modifier = "";
		token = "";
	} else {
		max = sizeof (i386_cpuid_cpus) / sizeof (i386_cpuid_cpus[0]);
		modif = (ci->ci_signature >> 12) & 3;
		family = (ci->ci_signature >> 8) & 15;
		ci->ci_family = family;
		model = (ci->ci_signature >> 4) & 15;
		ci->ci_model = model;
		step = ci->ci_signature & 15;
#ifdef CPUDEBUG
		printf("%s: cpuid level %d cache eax %x ebx %x ecx %x edx %x\n",
		    cpu_device, cpuid_level, cpu_cache_eax, cpu_cache_ebx,
		    cpu_cache_ecx, cpu_cache_edx);
#endif
		if (family < CPU_MINFAMILY)
			panic("identifycpu: strange family value");

		for (i = 0; i < max; i++) {
			if (!strncmp(cpu_vendor,
			    i386_cpuid_cpus[i].cpu_id, 12)) {
				cpup = &i386_cpuid_cpus[i];
				break;
			}
		}

		if (cpup == NULL) {
			vendor = CPUVENDOR_UNKNOWN;
			if (cpu_vendor[0] != '\0')
				vendorname = &cpu_vendor[0];
			else
				vendorname = "Unknown";
			if (family > CPU_MAXFAMILY)
				family = CPU_MAXFAMILY;
			class = family - 3;
			if (class > CPUCLASS_686)
				class = CPUCLASS_686;
			modifier = "";
			name = "";
			token = "";
			ci->cpu_setup = NULL;
		} else {
			token = cpup->cpu_id;
			vendor = cpup->cpu_vendor;
			vendorname = cpup->cpu_vendorname;
			/*
			 * Special hack for the VIA C3 series.
			 *
			 * VIA bought Centaur Technology from IDT in Aug 1999
			 * and marketed the processors as VIA Cyrix III/C3.
			 */
			if (vendor == CPUVENDOR_IDT && family >= 6) {
				vendor = CPUVENDOR_VIA;
				vendorname = "VIA";
			}
			modifier = modifiers[modif];
			if (family > CPU_MAXFAMILY) {
				family = CPU_MAXFAMILY;
				model = CPU_DEFMODEL;
			} else if (model > CPU_MAXMODEL)
				model = CPU_DEFMODEL;
			i = family - CPU_MINFAMILY;

			/* store extended family/model values for later use */
			if ((vendor == CPUVENDOR_INTEL &&
			    (family == 0x6 || family == 0xf)) ||
			    (vendor == CPUVENDOR_AMD && family == 0xf)) {
				ci->ci_family += (ci->ci_signature >> 20) &
				    0xff;
				ci->ci_model += ((ci->ci_signature >> 16) &
				    0x0f) << 4;
			}

			/* Special hack for the PentiumII/III series. */
			if (vendor == CPUVENDOR_INTEL && family == 6 &&
			    (model == 5 || model == 7)) {
				name = intel686_cpu_name(model);
			/* Special hack for the VIA C3 series. */
			} else if (vendor == CPUVENDOR_VIA && family == 6 &&
			    model == 7) {
				name = cyrix3_cpu_name(model, step);
			/* Special hack for the TMS5x00 series. */
			} else if (vendor == CPUVENDOR_TRANSMETA &&
			    family == 5 && model == 4) {
				name = tm86_cpu_name(model);
			} else
				name = cpup->cpu_family[i].cpu_models[model];
			if (name == NULL) {
				name = cpup->cpu_family[i].cpu_models[CPU_DEFMODEL];
				if (name == NULL)
					name = "";
			}
			class = cpup->cpu_family[i].cpu_class;
			ci->cpu_setup = cpup->cpu_family[i].cpu_setup;
		}
	}

	/* Find the amount of on-chip L2 cache. */
	cachesize = -1;
	if (vendor == CPUVENDOR_INTEL && cpuid_level >= 2 && family < 0xf) {
		int intel_cachetable[] = { 0, 128, 256, 512, 1024, 2048 };

		if ((cpu_cache_edx & 0xFF) >= 0x40 &&
		    (cpu_cache_edx & 0xFF) <= 0x45)
			cachesize = intel_cachetable[(cpu_cache_edx & 0xFF) - 0x40];
	} else if (vendor == CPUVENDOR_AMD && class == CPUCLASS_686) {
		u_int regs[4];
		cpuid(0x80000000, regs);

		if (regs[0] >= 0x80000006) {
			cpuid(0x80000006, regs);
			cachesize = (regs[2] >> 16);
		}
	}

	if (ci->ci_feature_flags & CPUID_CFLUSH) {
		u_int regs[4];

		/* to get the cacheline size you must do cpuid
		 * with eax 0x01
		 */

		cpuid(0x01, regs);
		ci->ci_cflushsz = ((regs[1] >> 8) & 0xff) * 8;
	}

	if (vendor == CPUVENDOR_INTEL) {
		/*
		 * PIII, Core Solo and Core Duo CPUs have known
		 * errata stating:
		 * "Page with PAT set to WC while associated MTRR is UC
		 * may consolidate to UC".
		 * Because of this it is best we just fallback to mtrrs
		 * in this case.
		 */
		if (ci->ci_family == 6 && ci->ci_model < 15)
		    ci->ci_feature_flags &= ~CPUID_PAT;
	}

	/* Remove leading, trailing and duplicated spaces from cpu_brandstr */
	brandstr_from = brandstr_to = cpu_brandstr;
	skipspace = 1;
	while (*brandstr_from != '\0') {
		if (!skipspace || *brandstr_from != ' ') {
			skipspace = 0;
			*(brandstr_to++) = *brandstr_from;
		}
		if (*brandstr_from == ' ')
			skipspace = 1;
		brandstr_from++;
	}
	if (skipspace && brandstr_to > cpu_brandstr)
		brandstr_to--;
	*brandstr_to = '\0';

	if (cpu_brandstr[0] == '\0') {
		snprintf(cpu_brandstr, 48 /* sizeof(cpu_brandstr) */,
		    "%s %s%s", vendorname, modifier, name);
	}

	if (cachesize > -1) {
		snprintf(cpu_model, sizeof(cpu_model),
		    "%s (%s%s%s%s-class, %dKB L2 cache)",
		    cpu_brandstr,
		    ((*token) ? "\"" : ""), ((*token) ? token : ""),
		    ((*token) ? "\" " : ""), classnames[class], cachesize);
	} else {
		snprintf(cpu_model, sizeof(cpu_model),
		    "%s (%s%s%s%s-class)",
		    cpu_brandstr,
		    ((*token) ? "\"" : ""), ((*token) ? token : ""),
		    ((*token) ? "\" " : ""), classnames[class]);
	}

	printf("%s: %s", cpu_device, cpu_model);

	if (ci->ci_feature_flags && (ci->ci_feature_flags & CPUID_TSC)) {
		/* Has TSC, check if it's constant */
		switch (vendor) {
		case CPUVENDOR_INTEL:
			if ((ci->ci_family == 0x0f && ci->ci_model >= 0x03) ||
			    (ci->ci_family == 0x06 && ci->ci_model >= 0x0e)) {
				ci->ci_flags |= CPUF_CONST_TSC;
			}
			break;
		case CPUVENDOR_VIA:
			if (ci->ci_model >= 0x0f) {
				ci->ci_flags |= CPUF_CONST_TSC;
			}
			break;
		}
		calibrate_cyclecounter();
		if (cpuspeed > 994) {
			int ghz, fr;

			ghz = (cpuspeed + 9) / 1000;
			fr = ((cpuspeed + 9) / 10 ) % 100;
			if (fr)
				printf(" %d.%02d GHz", ghz, fr);
			else
				printf(" %d GHz", ghz);
		} else {
			printf(" %d MHz", cpuspeed);
		}
	}

	if (cpuid_level != -1)
		printf(", %02x-%02x-%02x", ci->ci_family, ci->ci_model,
		    step);

	if ((cpu_ecxfeature & CPUIDECX_HV) == 0) {
		uint64_t level = 0;
		uint32_t dummy;

		if (strcmp(cpu_vendor, "AuthenticAMD") == 0 &&
		    ci->ci_family >= 0x0f) {
			level = rdmsr(MSR_PATCH_LEVEL);
		} else if (strcmp(cpu_vendor, "GenuineIntel") == 0 &&
		    ci->ci_family >= 6) {
			wrmsr(MSR_BIOS_SIGN, 0);
			CPUID(1, dummy, dummy, dummy, dummy);
			level = rdmsr(MSR_BIOS_SIGN) >> 32;
		}
		if (level != 0)
			printf(", patch %08llx", level);
	}

	printf("\n");

	if (ci->ci_feature_flags) {
		int numbits = 0;

		printf("%s: ", cpu_device);
		max = sizeof(i386_cpuid_features) /
		    sizeof(i386_cpuid_features[0]);
		for (i = 0; i < max; i++) {
			if (ci->ci_feature_flags &
			    i386_cpuid_features[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_features[i].feature_name);
				numbits++;
			}
		}
		max = sizeof(i386_cpuid_ecxfeatures)
			/ sizeof(i386_cpuid_ecxfeatures[0]);
		for (i = 0; i < max; i++) {
			if (cpu_ecxfeature &
			    i386_cpuid_ecxfeatures[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_ecxfeatures[i].feature_name);
				numbits++;
			}
		}
		for (i = 0; i < nitems(i386_ecpuid_features); i++) {
			if (ecpu_feature &
			    i386_ecpuid_features[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_ecpuid_features[i].feature_name);
				numbits++;
			}
		}
		for (i = 0; i < nitems(i386_ecpuid_ecxfeatures); i++) {
			if (ecpu_ecxfeature &
			    i386_ecpuid_ecxfeatures[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_ecpuid_ecxfeatures[i].feature_name);
				numbits++;
			}
		}
		for (i = 0; i < nitems(i386_cpuid_eaxperf); i++) {
			if (cpu_perf_eax &
			    i386_cpuid_eaxperf[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_eaxperf[i].feature_name);
				numbits++;
			}
		}
		for (i = 0; i < nitems(i386_cpuid_edxapmi); i++) {
			if (cpu_apmi_edx &
			    i386_cpuid_edxapmi[i].feature_bit) {
				printf("%s%s", (numbits == 0 ? "" : ","),
				    i386_cpuid_edxapmi[i].feature_name);
				numbits++;
			}
		}

		if (cpuid_level >= 0x07) {
			u_int dummy;

			/* "Structured Extended Feature Flags" */
			CPUID_LEAF(0x7, 0, dummy,
			    ci->ci_feature_sefflags_ebx,
			    ci->ci_feature_sefflags_ecx,
			    ci->ci_feature_sefflags_edx);
			for (i = 0; i < nitems(cpu_seff0_ebxfeatures); i++)
				if (ci->ci_feature_sefflags_ebx &
				    cpu_seff0_ebxfeatures[i].feature_bit)
					printf("%s%s",
					    (numbits == 0 ? "" : ","),
					    cpu_seff0_ebxfeatures[i].feature_name);
			for (i = 0; i < nitems(cpu_seff0_ecxfeatures); i++)
				if (ci->ci_feature_sefflags_ecx &
				    cpu_seff0_ecxfeatures[i].feature_bit)
					printf("%s%s",
					    (numbits == 0 ? "" : ","),
					    cpu_seff0_ecxfeatures[i].feature_name);
			for (i = 0; i < nitems(cpu_seff0_edxfeatures); i++)
				if (ci->ci_feature_sefflags_edx &
				    cpu_seff0_edxfeatures[i].feature_bit)
					printf("%s%s",
					    (numbits == 0 ? "" : ","),
					    cpu_seff0_edxfeatures[i].feature_name);
		}

		if (!strcmp(cpu_vendor, "GenuineIntel") &&
		    cpuid_level >= 0x06 ) {
			u_int dummy;

			CPUID(0x06, ci->ci_feature_tpmflags, dummy,
			    dummy, dummy);
			max = nitems(cpu_tpm_eaxfeatures);
			for (i = 0; i < max; i++)
				if (ci->ci_feature_tpmflags &
				    cpu_tpm_eaxfeatures[i].feature_bit)
					printf(",%s", cpu_tpm_eaxfeatures[i].feature_name);
		}

		/* xsave subfeatures */
		if (cpuid_level >= 0xd) {
			uint32_t dummy, val;

			CPUID_LEAF(0xd, 1, val, dummy, dummy, dummy);
			for (i = 0; i < nitems(cpu_xsave_extfeatures); i++)
				if (val & cpu_xsave_extfeatures[i].feature_bit)
					printf(",%s",
					    cpu_xsave_extfeatures[i].feature_name);
		}

		if (cpu_meltdown)
			printf(",MELTDOWN");

		printf("\n");
	}

	/*
	 * "Mitigation G-2" per AMD's Whitepaper "Software Techniques
	 * for Managing Speculation on AMD Processors"
	 *
	 * By setting MSR C001_1029[1]=1, LFENCE becomes a dispatch
	 * serializing instruction.
	 *
	 * This MSR is available on all AMD families >= 10h, except 11h
 	 * where LFENCE is always serializing.
	 */
	if (!strcmp(cpu_vendor, "AuthenticAMD")) {
		if (ci->ci_family >= 0x10 && ci->ci_family != 0x11) {
			nmsr = msr = rdmsr(MSR_DE_CFG);
			nmsr |= DE_CFG_SERIALIZE_LFENCE;
			if (msr != nmsr)
				wrmsr(MSR_DE_CFG, nmsr);
		}
		if (ci->ci_family == 0x17 && ci->ci_model >= 0x31 &&
		    (cpu_ecxfeature & CPUIDECX_HV) == 0) {
			nmsr = msr = rdmsr(MSR_DE_CFG);
			nmsr |= DE_CFG_SERIALIZE_9;
			if (msr != nmsr)
				wrmsr(MSR_DE_CFG, nmsr);
		}
	}

	/*
	 * Attempt to disable Silicon Debug and lock the configuration
	 * if it's enabled and unlocked.
	 */
	if (!strcmp(cpu_vendor, "GenuineIntel") &&
	    (cpu_ecxfeature & CPUIDECX_SDBG)) {
		uint64_t msr;

		msr = rdmsr(IA32_DEBUG_INTERFACE);
		if ((msr & IA32_DEBUG_INTERFACE_ENABLE) &&
		    (msr & IA32_DEBUG_INTERFACE_LOCK) == 0) {
			msr &= IA32_DEBUG_INTERFACE_MASK;
			msr |= IA32_DEBUG_INTERFACE_LOCK;
			wrmsr(IA32_DEBUG_INTERFACE, msr);
		} else if (msr & IA32_DEBUG_INTERFACE_ENABLE)
			printf("%s: cannot disable silicon debug\n",
			    cpu_device);
	}

	if (CPU_IS_PRIMARY(ci)) {
		if (cpu_ecxfeature & CPUIDECX_RDRAND)
			has_rdrand = 1;
		if (ci->ci_feature_sefflags_ebx & SEFF0EBX_RDSEED)
			has_rdseed = 1;
		if (ci->ci_feature_sefflags_ebx & SEFF0EBX_SMAP)
			replacesmap();
	}

#ifndef SMALL_KERNEL
	if (cpuspeed != 0 && cpu_cpuspeed == NULL)
		cpu_cpuspeed = pentium_cpuspeed;
#endif

	cpu_class = class;

	ci->cpu_class = class;

	/*
	 * Enable ring 0 write protection.
	 */
	lcr0(rcr0() | CR0_WP);

	/*
	 * If we have FXSAVE/FXRESTOR, use them.
	 */
	if (cpu_feature & CPUID_FXSR) {
		i386_use_fxsave = 1;
		lcr4(rcr4() | CR4_OSFXSR);

		/*
		 * If we have SSE/SSE2, enable XMM exceptions, and
		 * notify userland.
		 */
		if (cpu_feature & (CPUID_SSE|CPUID_SSE2)) {
			if (cpu_feature & CPUID_SSE)
				i386_has_sse = 1;
			if (cpu_feature & CPUID_SSE2)
				i386_has_sse2 = 1;
			lcr4(rcr4() | CR4_OSXMMEXCPT);
		}
	} else
		i386_use_fxsave = 0;

}

char *
tm86_cpu_name(int model)
{
	u_int32_t regs[4];
	char *name = NULL;

	cpuid(0x80860001, regs);

	switch (model) {
	case 4:
		if (((regs[1] >> 16) & 0xff) >= 0x3)
			name = "TMS5800";
		else
			name = "TMS5600";
	}

	return name;
}

#ifndef SMALL_KERNEL
void
cyrix3_get_bus_clock(struct cpu_info *ci)
{
	u_int64_t msr;
	int bus;

	msr = rdmsr(MSR_EBL_CR_POWERON);
	bus = (msr >> 18) & 0x3;
	switch (bus) {
	case 0:
		bus_clock = BUS100;
		break;
	case 1:
		bus_clock = BUS133;
		break;
	case 2:
		bus_clock = BUS200;
		break;
	case 3:
		bus_clock = BUS166;
		break;
	}
}

void
p4_get_bus_clock(struct cpu_info *ci)
{
	u_int64_t msr;
	int model, bus;

	model = (ci->ci_signature >> 4) & 15;
	msr = rdmsr(MSR_EBC_FREQUENCY_ID);
	if (model < 2) {
		bus = (msr >> 21) & 0x7;
		switch (bus) {
		case 0:
			bus_clock = BUS100;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		default:
			printf("%s: unknown Pentium 4 (model %d) "
			    "EBC_FREQUENCY_ID value %d\n",
			    ci->ci_dev->dv_xname, model, bus);
			break;
		}
	} else {
		bus = (msr >> 16) & 0x7;
		switch (bus) {
		case 0:
			bus_clock = (model == 2) ? BUS100 : BUS266;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		case 2:
			bus_clock = BUS200;
			break;
		case 3:
			bus_clock = BUS166;
			break;
		default:
			printf("%s: unknown Pentium 4 (model %d) "
			    "EBC_FREQUENCY_ID value %d\n",
			    ci->ci_dev->dv_xname, model, bus);
			break;
		}
	}
}

void
p3_get_bus_clock(struct cpu_info *ci)
{
	u_int64_t msr;
	int bus;

	switch (ci->ci_model) {
	case 0x9: /* Pentium M (130 nm, Banias) */
		bus_clock = BUS100;
		break;
	case 0xd: /* Pentium M (90 nm, Dothan) */
		msr = rdmsr(MSR_FSB_FREQ);
		bus = (msr >> 0) & 0x7;
		switch (bus) {
		case 0:
			bus_clock = BUS100;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		default:
			printf("%s: unknown Pentium M FSB_FREQ value %d",
			    ci->ci_dev->dv_xname, bus);
			goto print_msr;
		}
		break;
	case 0x15:	/* EP80579 no FSB */
		break;
	case 0xe: /* Core Duo/Solo */
	case 0xf: /* Core Xeon */
	case 0x16: /* 65nm Celeron */
	case 0x17: /* Core 2 Extreme/45nm Xeon */
	case 0x1d: /* Xeon MP 7400 */
		msr = rdmsr(MSR_FSB_FREQ);
		bus = (msr >> 0) & 0x7;
		switch (bus) {
		case 5:
			bus_clock = BUS100;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		case 3:
			bus_clock = BUS166;
			break;
		case 2:
			bus_clock = BUS200;
			break;
		case 0:
			bus_clock = BUS266;
			break;
		case 4:
			bus_clock = BUS333;
			break;
		default:
			printf("%s: unknown Core FSB_FREQ value %d",
			    ci->ci_dev->dv_xname, bus);
			goto print_msr;
		}
		break;
	case 0x1c: /* Atom */
	case 0x26: /* Atom Z6xx */
	case 0x36: /* Atom [DN]2xxx */
		msr = rdmsr(MSR_FSB_FREQ);
		bus = (msr >> 0) & 0x7;
		switch (bus) {
		case 5:
			bus_clock = BUS100;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		case 3:
			bus_clock = BUS166;
			break;
		case 2:
			bus_clock = BUS200;
			break;
		default:
			printf("%s: unknown Atom FSB_FREQ value %d",
			    ci->ci_dev->dv_xname, bus);
			goto print_msr;
		}
		break;
	case 0x1: /* Pentium Pro, model 1 */
	case 0x3: /* Pentium II, model 3 */
	case 0x5: /* Pentium II, II Xeon, Celeron, model 5 */
	case 0x6: /* Celeron, model 6 */
	case 0x7: /* Pentium III, III Xeon, model 7 */
	case 0x8: /* Pentium III, III Xeon, Celeron, model 8 */
	case 0xa: /* Pentium III Xeon, model A */
	case 0xb: /* Pentium III, model B */
		msr = rdmsr(MSR_EBL_CR_POWERON);
		bus = (msr >> 18) & 0x3;
		switch (bus) {
		case 0:
			bus_clock = BUS66;
			break;
		case 1:
			bus_clock = BUS133;
			break;
		case 2:
			bus_clock = BUS100;
			break;
		default:
			printf("%s: unknown i686 EBL_CR_POWERON value %d",
			    ci->ci_dev->dv_xname, bus);
			goto print_msr;
		}
		break;
	default:
		/* no FSB on modern Intel processors */
		break;
	}
	return;
print_msr:
	/*
	 * Show the EBL_CR_POWERON MSR, so we'll at least have
	 * some extra information, such as clock ratio, etc.
	 */
	printf(" (0x%llx)\n", rdmsr(MSR_EBL_CR_POWERON));
}

void
p4_update_cpuspeed(void)
{
	struct cpu_info *ci;
	u_int64_t msr;
	int mult;

	ci = curcpu();
	p4_get_bus_clock(ci);

	if (bus_clock == 0) {
		printf("p4_update_cpuspeed: unknown bus clock\n");
		return;
	}

	msr = rdmsr(MSR_EBC_FREQUENCY_ID);
	mult = ((msr >> 24) & 0xff);

	cpuspeed = (bus_clock * mult) / 100;
}

void
p3_update_cpuspeed(void)
{
	struct cpu_info *ci;
	u_int64_t msr;
	int mult;
	const u_int8_t mult_code[] = {
	    50, 30, 40, 0, 55, 35, 45, 0, 0, 70, 80, 60, 0, 75, 0, 65 };

	ci = curcpu();
	p3_get_bus_clock(ci);

	if (bus_clock == 0) {
		printf("p3_update_cpuspeed: unknown bus clock\n");
		return;
	}

	msr = rdmsr(MSR_EBL_CR_POWERON);
	mult = (msr >> 22) & 0xf;
	mult = mult_code[mult];
	if (!p3_early)
		mult += ((msr >> 27) & 0x1) * 40;

	cpuspeed = (bus_clock * mult) / 1000;
}

int
pentium_cpuspeed(int *freq)
{
	*freq = cpuspeed;
	return (0);
}
#endif	/* !SMALL_KERNEL */

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * in u. to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct trapframe *tf = p->p_md.md_regs;
	struct sigframe *fp, frame;
	register_t sp;

	/*
	 * Build the argument list for the signal handler.
	 */
	bzero(&frame, sizeof(frame));
	frame.sf_signum = sig;

	/*
	 * Allocate space for the signal handler context.
	 */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(tf->tf_esp) && onstack)
		sp = trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		sp = tf->tf_esp;

	frame.sf_sc.sc_fpstate = NULL;
	if (p->p_md.md_flags & MDP_USEDFPU) {
		npxsave_proc(p, 1);
		sp -= sizeof(union savefpu);
		sp &= ~0xf;	/* for XMM regs */
		frame.sf_sc.sc_fpstate = (void *)sp;
		if (copyout(&p->p_addr->u_pcb.pcb_savefpu,
		    (void *)sp, sizeof(union savefpu)))
		    	return 1;

		/* Signal handlers get a completely clean FP state */
		p->p_md.md_flags &= ~MDP_USEDFPU;
	}

	fp = (struct sigframe *)sp - 1;
	frame.sf_scp = &fp->sf_sc;
	frame.sf_sip = NULL;
	frame.sf_handler = catcher;

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	frame.sf_sc.sc_err = tf->tf_err;
	frame.sf_sc.sc_trapno = tf->tf_trapno;
	frame.sf_sc.sc_mask = mask;
	frame.sf_sc.sc_fs = tf->tf_fs;
	frame.sf_sc.sc_gs = tf->tf_gs;
	frame.sf_sc.sc_es = tf->tf_es;
	frame.sf_sc.sc_ds = tf->tf_ds;
	frame.sf_sc.sc_eflags = tf->tf_eflags;
	frame.sf_sc.sc_edi = tf->tf_edi;
	frame.sf_sc.sc_esi = tf->tf_esi;
	frame.sf_sc.sc_ebp = tf->tf_ebp;
	frame.sf_sc.sc_ebx = tf->tf_ebx;
	frame.sf_sc.sc_edx = tf->tf_edx;
	frame.sf_sc.sc_ecx = tf->tf_ecx;
	frame.sf_sc.sc_eax = tf->tf_eax;
	frame.sf_sc.sc_eip = tf->tf_eip;
	frame.sf_sc.sc_cs = tf->tf_cs;
	frame.sf_sc.sc_esp = tf->tf_esp;
	frame.sf_sc.sc_ss = tf->tf_ss;

	if (info) {
		frame.sf_sip = &fp->sf_si;
		frame.sf_si = *ksip;
	}

	/* XXX don't copyout siginfo if not needed? */
	frame.sf_sc.sc_cookie = (long)&fp->sf_sc ^ p->p_p->ps_sigcookie;
	if (copyout(&frame, fp, sizeof(frame)) != 0)
		return 1;

	/*
	 * Build context to run handler in.
	 */
	tf->tf_fs = GSEL(GUFS_SEL, SEL_UPL);
	tf->tf_gs = GSEL(GUGS_SEL, SEL_UPL);
	tf->tf_es = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_ds = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_eip = p->p_p->ps_sigcode;
	tf->tf_cs = GSEL(GUCODE_SEL, SEL_UPL);
	tf->tf_eflags &= ~(PSL_T|PSL_D|PSL_VM|PSL_AC);
	tf->tf_esp = (int)fp;
	tf->tf_ss = GSEL(GUDATA_SEL, SEL_UPL);

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
	struct trapframe *tf = p->p_md.md_regs;
	int error;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	if ((error = copyin((caddr_t)scp, &ksc, sizeof(*scp))))
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
	 * Restore signal ksc.
	 */
	/*
	 * Check for security violations.  If we're returning to
	 * protected mode, the CPU will validate the segment registers
	 * automatically and generate a trap on violations.  We handle
	 * the trap, rather than doing all of the checking here.
	 */
	if (((ksc.sc_eflags ^ tf->tf_eflags) & PSL_USERSTATIC) != 0 ||
	    !USERMODE(ksc.sc_cs, ksc.sc_eflags))
		return (EINVAL);

	tf->tf_fs = ksc.sc_fs;
	tf->tf_gs = ksc.sc_gs;
	tf->tf_es = ksc.sc_es;
	tf->tf_ds = ksc.sc_ds;
	tf->tf_eflags = ksc.sc_eflags;
	tf->tf_edi = ksc.sc_edi;
	tf->tf_esi = ksc.sc_esi;
	tf->tf_ebp = ksc.sc_ebp;
	tf->tf_ebx = ksc.sc_ebx;
	tf->tf_edx = ksc.sc_edx;
	tf->tf_ecx = ksc.sc_ecx;
	tf->tf_eax = ksc.sc_eax;
	tf->tf_eip = ksc.sc_eip;
	tf->tf_cs = ksc.sc_cs;
	tf->tf_esp = ksc.sc_esp;
	tf->tf_ss = ksc.sc_ss;

	if (p->p_md.md_flags & MDP_USEDFPU)
		npxsave_proc(p, 0);

	if (ksc.sc_fpstate) {
		union savefpu *sfp = &p->p_addr->u_pcb.pcb_savefpu;

		if ((error = copyin(ksc.sc_fpstate, sfp, sizeof(*sfp))))
			return (error);
		if (i386_use_fxsave)
			sfp->sv_xmm.sv_env.en_mxcsr &= fpu_mxcsr_mask;
		p->p_md.md_flags |= MDP_USEDFPU;
	}

	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

	return (EJUSTRETURN);
}

#ifdef MULTIPROCESSOR
/* force a CPU into the kernel, whether or not it's idle */
void
cpu_kick(struct cpu_info *ci)
{
	/* only need to kick other CPUs */
	if (ci != curcpu()) {
		if (cpu_mwait_size > 0) {
			/*
			 * If not idling, then send an IPI, else
			 * just clear the "keep idling" bit.
			 */
			if ((ci->ci_mwait & MWAIT_IN_IDLE) == 0)
				i386_send_ipi(ci, I386_IPI_NOP);
			else
				atomic_clearbits_int(&ci->ci_mwait,
				    MWAIT_KEEP_IDLING);
		} else {
			/* no mwait, so need an IPI */
			i386_send_ipi(ci, I386_IPI_NOP);
		}
	}
}
#endif

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */
void
signotify(struct proc *p)
{
	aston(p);
	cpu_kick(p->p_cpu);
}

#ifdef MULTIPROCESSOR
void
cpu_unidle(struct cpu_info *ci)
{
	if (cpu_mwait_size > 0 && (ci->ci_mwait & MWAIT_ONLY)) {
		/*
		 * Just clear the "keep idling" bit; if it wasn't
		 * idling then we didn't need to do anything anyway.
		 */
		atomic_clearbits_int(&ci->ci_mwait, MWAIT_KEEP_IDLING);
		return;
	}

	if (ci != curcpu())
		i386_send_ipi(ci, I386_IPI_NOP);
}
#endif

int	waittime = -1;
struct pcb dumppcb;

__dead void
boot(int howto)
{
#if NACPI > 0
	if ((howto & RB_POWERDOWN) != 0 && acpi_softc)
		acpi_softc->sc_state = ACPI_STATE_S5;
#endif

	if ((howto & RB_POWERDOWN) != 0)
		lid_action = 0;

	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

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

	if ((howto & RB_DUMP) != 0)
		dumpsys();

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

#ifdef MULTIPROCESSOR
	i386_broadcast_ipi(I386_IPI_HALT);
#endif

	if ((howto & RB_HALT) != 0) {
#if NACPI > 0 && !defined(SMALL_KERNEL)
		extern int acpi_enabled;

		if (acpi_enabled) {
			delay(500000);
			if ((howto & RB_POWERDOWN) != 0)
				acpi_powerdown();
		}
#endif

#if NAPM > 0
		if ((howto & RB_POWERDOWN) != 0) {
			int rv;

			printf("\nAttempting to power down...\n");
			/*
			 * Turn off, if we can.  But try to turn disk off and
			 * wait a bit first--some disk drives are slow to
			 * clean up and users have reported disk corruption.
			 *
			 * If apm_set_powstate() fails the first time, don't
			 * try to turn the system off.
			 */
			delay(500000);
			apm_set_powstate(APM_DEV_DISK(0xff), APM_SYS_OFF);
			delay(500000);
			rv = apm_set_powstate(APM_DEV_DISK(0xff), APM_SYS_OFF);
			if (rv == 0 || rv == ENXIO) {
				delay(500000);
				(void) apm_set_powstate(APM_DEV_ALLDEVS,
							APM_SYS_OFF);
			}
		}
#endif
		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cnpollc(1);	/* for proper keyboard command handling */
		cngetc();
		cnpollc(0);
	}

doreset:
	printf("rebooting...\n");
	cpu_reset();
	for (;;)
		continue;
	/* NOTREACHED */
}

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first block of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks;	/* size of dump area */
	int i;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	/* Always skip the first block, in case there is a label there. */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

	for (i = 0; i < ndumpmem; i++)
		dumpsize = max(dumpsize, dumpmem[i].end);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize > dtoc(nblks - dumplo - 1))
		dumpsize = dtoc(nblks - dumplo - 1);
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;
}

/*
 * cpu_dump: dump machine-dependent kernel core dump headers.
 */
int
cpu_dump(void)
{
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	long buf[dbtob(1) / sizeof (long)];
	kcore_seg_t	*segp;

	dump = bdevsw[major(dumpdev)].d_dump;

	segp = (kcore_seg_t *)buf;

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	return (dump(dumpdev, dumplo, (caddr_t)buf, dbtob(1)));
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
static vaddr_t dumpspace;

vaddr_t
reserve_dumppages(vaddr_t p)
{

	dumpspace = p;
	return (p + PAGE_SIZE);
}

void
dumpsys(void)
{
	u_int i, j, npg;
	int maddr;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error;
	char *str;
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
	if (dumplo < 0)
		return;
	printf("\ndumping to dev %x, offset %ld\n", dumpdev, dumplo);

	error = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (error == -1) {
		printf("area unavailable\n");
		return;
	}

#if 0	/* XXX this doesn't work.  grr. */
	/* toss any characters present prior to dump */
	while (sget() != NULL); /*syscons and pccons differ */
#endif

	/* scan through the dumpmem list */
	dump = bdevsw[major(dumpdev)].d_dump;
	error = cpu_dump();
	for (i = 0; !error && i < ndumpmem; i++) {

		npg = dumpmem[i].end - dumpmem[i].start;
		maddr = ptoa(dumpmem[i].start);
		blkno = dumplo + btodb(maddr) + 1;
#if 0
		printf("(%d %lld %d) ", maddr, (long long)blkno, npg);
#endif
		for (j = npg; j--; maddr += NBPG, blkno += btodb(NBPG)) {

			/* Print out how many MBs we have more to go. */
			if (dbtob(blkno - dumplo) % (1024 * 1024) < NBPG)
				printf("%ld ",
				    (ptoa(dumpsize) - maddr) / (1024 * 1024));
#if 0
			printf("(%x %lld) ", maddr, (long long)blkno);
#endif
			pmap_enter(pmap_kernel(), dumpspace, maddr,
			    PROT_READ, PMAP_WIRED);
			if ((error = (*dump)(dumpdev, blkno,
			    (caddr_t)dumpspace, NBPG)))
				break;

#if 0	/* XXX this doesn't work.  grr. */
			/* operator aborting dump? */
			if (sget() != NULL) {
				error = EINTR;
				break;
			}
#endif
		}
	}

	switch (error) {

	case 0:		str = "succeeded\n\n";			break;
	case ENXIO:	str = "device bad\n\n";			break;
	case EFAULT:	str = "device not ready\n\n";		break;
	case EINVAL:	str = "area improper\n\n";		break;
	case EIO:	str = "i/o error\n\n";			break;
	case EINTR:	str = "aborted from console\n\n";	break;
	default:	str = "error %d\n\n";			break;
	}
	printf(str, error);

	delay(5000000);		/* 5 seconds */
}

/*
 * Clear registers on exec
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct pmap *pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	struct trapframe *tf = p->p_md.md_regs;

#if NNPX > 0
	/* If we were using the FPU, forget about it. */
	if (pcb->pcb_fpcpu != NULL)
		npxsave_proc(p, 0);
	p->p_md.md_flags &= ~MDP_USEDFPU;
#endif

	initcodesegment(&pmap->pm_codeseg);
	setsegment(&pcb->pcb_threadsegs[TSEG_FS], 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1);
	setsegment(&pcb->pcb_threadsegs[TSEG_GS], 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1);

	/*
	 * And update the GDT since we return to the user process
	 * by leaving the syscall (we don't do another pmap_activate()).
	 */
	curcpu()->ci_gdt[GUCODE_SEL].sd = pmap->pm_codeseg;
	curcpu()->ci_gdt[GUFS_SEL].sd = pcb->pcb_threadsegs[TSEG_FS];
	curcpu()->ci_gdt[GUGS_SEL].sd = pcb->pcb_threadsegs[TSEG_GS];

	/*
	 * And reset the hiexec marker in the pmap.
	 */
	pmap->pm_hiexec = 0;

	tf->tf_fs = GSEL(GUFS_SEL, SEL_UPL);
	tf->tf_gs = GSEL(GUGS_SEL, SEL_UPL);
	tf->tf_es = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_ds = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_edi = 0;
	tf->tf_esi = 0;
	tf->tf_ebp = 0;
	tf->tf_ebx = (int)p->p_p->ps_strings;
	tf->tf_edx = 0;
	tf->tf_ecx = 0;
	tf->tf_eax = 0;
	tf->tf_eip = pack->ep_entry;
	tf->tf_cs = GSEL(GUCODE_SEL, SEL_UPL);
	tf->tf_eflags = PSL_USERSET;
	tf->tf_esp = stack;
	tf->tf_ss = GSEL(GUDATA_SEL, SEL_UPL);
}

/*
 * Initialize segments and descriptor tables
 */

/* IDT is now a full page, so we can map it in u-k */
union {
	struct gate_descriptor	idt[NIDT];
	char			align[PAGE_SIZE];
} _idt_region __aligned(PAGE_SIZE);
#define idt_region _idt_region.idt
struct gate_descriptor *idt = idt_region;

extern  struct user *proc0paddr;

void
setgate(struct gate_descriptor *gd, void *func, int args, int type, int dpl,
    int seg)
{

	gd->gd_looffset = (int)func;
	gd->gd_selector = GSEL(seg, SEL_KPL);
	gd->gd_stkcpy = args;
	gd->gd_xx = 0;
	gd->gd_type = type;
	gd->gd_dpl = dpl;
	gd->gd_p = 1;
	gd->gd_hioffset = (int)func >> 16;
}

void
unsetgate(struct gate_descriptor *gd)
{
	gd->gd_p = 0;
	gd->gd_hioffset = 0;
	gd->gd_looffset = 0;
	gd->gd_selector = 0;
	gd->gd_xx = 0;
	gd->gd_stkcpy = 0;
	gd->gd_type = 0;
	gd->gd_dpl = 0;
}

void
setregion(struct region_descriptor *rd, void *base, size_t limit)
{
	rd->rd_limit = (int)limit;
	rd->rd_base = (int)base;
}

void
initcodesegment(struct segment_descriptor *cs)
{
	if (cpu_pae) {
		/*
		 * When code execution is managed using NX feature
		 * in pmapae.c, GUCODE_SEL should cover userland.
		 */
		setsegment(cs, 0, atop(VM_MAXUSER_ADDRESS - 1),
		    SDT_MEMERA, SEL_UPL, 1, 1);
	} else {
		/*
		 * For pmap.c's non-PAE/NX line-in-the-sand execution, reset
		 * the code segment limit to I386_MAX_EXE_ADDR in the pmap;
		 * this gets copied into the GDT for GUCODE_SEL by
		 * pmap_activate().  Similarly, reset the base of each of
		 * the two thread data segments to zero in the pcb; they'll
		 * get copied into the GDT for GUFS_SEL and GUGS_SEL.
		 */
		setsegment(cs, 0, atop(I386_MAX_EXE_ADDR - 1),
		    SDT_MEMERA, SEL_UPL, 1, 1);
	}
}

void
setsegment(struct segment_descriptor *sd, void *base, size_t limit, int type,
    int dpl, int def32, int gran)
{

	sd->sd_lolimit = (int)limit;
	sd->sd_lobase = (int)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (int)limit >> 16;
	sd->sd_xx = 0;
	sd->sd_def32 = def32;
	sd->sd_gran = gran;
	sd->sd_hibase = (int)base >> 24;
}

#define	IDTVEC(name)	__CONCAT(X, name)
extern int IDTVEC(div), IDTVEC(dbg), IDTVEC(nmi), IDTVEC(bpt), IDTVEC(ofl),
    IDTVEC(bnd), IDTVEC(ill), IDTVEC(dna), IDTVEC(dble), IDTVEC(fpusegm),
    IDTVEC(tss), IDTVEC(missing), IDTVEC(stk), IDTVEC(prot), IDTVEC(page),
    IDTVEC(rsvd), IDTVEC(fpu), IDTVEC(align), IDTVEC(syscall), IDTVEC(mchk),
    IDTVEC(simd);

extern int IDTVEC(f00f_redirect);

int cpu_f00f_bug = 0;

void
fix_f00f(void)
{
	struct region_descriptor region;
	vaddr_t va;
	paddr_t pa;
	void *p;

	/* Allocate two new pages */
	va = (vaddr_t)km_alloc(NBPG*2, &kv_any, &kp_zero, &kd_waitok);
	p = (void *)(va + NBPG - 7*sizeof(*idt));

	/* Copy over old IDT */
	bcopy(idt, p, sizeof(idt_region));
	idt = p;

	/* Fix up paging redirect */
	setgate(&idt[ 14], &IDTVEC(f00f_redirect), 0, SDT_SYS386IGT, SEL_KPL,
	    GCODE_SEL);

	/* Map first page RO */
	pmap_pte_setbits(va, 0, PG_RW);

	/* add k-u read-only mappings XXX old IDT stays in place */
	/* XXX hshoexer: are f00f affected CPUs affected by meltdown? */
	pmap_extract(pmap_kernel(), va, &pa);
	pmap_enter_special(va, pa, PROT_READ, 0);
	pmap_extract(pmap_kernel(), va + PAGE_SIZE, &pa);
	pmap_enter_special(va + PAGE_SIZE, pa, PROT_READ, 0);

	/* Reload idtr */
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);

	/* Tell the rest of the world */
	cpu_f00f_bug = 1;
}

#ifdef MULTIPROCESSOR
void
cpu_init_idt(void)
{
	struct region_descriptor region;
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);
}
#endif /* MULTIPROCESSOR */

void
init386(paddr_t first_avail)
{
	int i, kb;
	struct region_descriptor region;
	bios_memmap_t *im;

	proc0.p_addr = proc0paddr;
	cpu_info_primary.ci_self = &cpu_info_primary;
	cpu_info_primary.ci_curpcb = &proc0.p_addr->u_pcb;
	cpu_info_primary.ci_tss = &cpu_info_full_primary.cif_tss;
	cpu_info_primary.ci_nmi_tss = &cpu_info_full_primary.cif_nmi_tss;
	cpu_info_primary.ci_gdt = (void *)&cpu_info_full_primary.cif_gdt;

	/* make bootstrap gdt gates and memory segments */
	setsegment(&cpu_info_primary.ci_gdt[GCODE_SEL].sd, 0, 0xfffff,
	    SDT_MEMERA, SEL_KPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GICODE_SEL].sd, 0, 0xfffff,
	    SDT_MEMERA, SEL_KPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GDATA_SEL].sd, 0, 0xfffff,
	    SDT_MEMRWA, SEL_KPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GUCODE_SEL].sd, 0,
	    atop(I386_MAX_EXE_ADDR) - 1, SDT_MEMERA, SEL_UPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GUDATA_SEL].sd, 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GCPU_SEL].sd, &cpu_info_primary,
	    sizeof(struct cpu_info)-1, SDT_MEMRWA, SEL_KPL, 0, 0);
	setsegment(&cpu_info_primary.ci_gdt[GUFS_SEL].sd, 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GUGS_SEL].sd, 0,
	    atop(VM_MAXUSER_ADDRESS) - 1, SDT_MEMRWA, SEL_UPL, 1, 1);
	setsegment(&cpu_info_primary.ci_gdt[GTSS_SEL].sd,
	    cpu_info_primary.ci_tss, sizeof(struct i386tss)-1,
	    SDT_SYS386TSS, SEL_KPL, 0, 0);
	setsegment(&cpu_info_primary.ci_gdt[GNMITSS_SEL].sd,
	    cpu_info_primary.ci_nmi_tss, sizeof(struct i386tss)-1,
	    SDT_SYS386TSS, SEL_KPL, 0, 0);

	/* exceptions */
	setgate(&idt[  0], &IDTVEC(div),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  1], &IDTVEC(dbg),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  2], NULL,             0, SDT_SYSTASKGT, SEL_KPL, GNMITSS_SEL);
	setgate(&idt[  3], &IDTVEC(bpt),     0, SDT_SYS386IGT, SEL_UPL, GCODE_SEL);
	setgate(&idt[  4], &IDTVEC(ofl),     0, SDT_SYS386IGT, SEL_UPL, GCODE_SEL);
	setgate(&idt[  5], &IDTVEC(bnd),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  6], &IDTVEC(ill),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  7], &IDTVEC(dna),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  8], &IDTVEC(dble),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[  9], &IDTVEC(fpusegm), 0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 10], &IDTVEC(tss),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 11], &IDTVEC(missing), 0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 12], &IDTVEC(stk),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 13], &IDTVEC(prot),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 14], &IDTVEC(page),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 15], &IDTVEC(rsvd),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 16], &IDTVEC(fpu),     0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 17], &IDTVEC(align),   0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 18], &IDTVEC(mchk),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	setgate(&idt[ 19], &IDTVEC(simd),    0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	for (i = 20; i < NRSVIDT; i++)
		setgate(&idt[i], &IDTVEC(rsvd), 0, SDT_SYS386IGT, SEL_KPL, GCODE_SEL);
	for (i = NRSVIDT; i < NIDT; i++)
		unsetgate(&idt[i]);
	setgate(&idt[128], &IDTVEC(syscall), 0, SDT_SYS386IGT, SEL_UPL, GCODE_SEL);

	setregion(&region, cpu_info_primary.ci_gdt, GDT_SIZE - 1);
	lgdt(&region);
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);

	/*
	 * Initialize the I/O port and I/O mem extent maps.
	 * Note: we don't have to check the return value since
	 * creation of a fixed extent map will never fail (since
	 * descriptor storage has already been allocated).
	 *
	 * N.B. The iomem extent manages _all_ physical addresses
	 * on the machine.  When the amount of RAM is found, the two
	 * extents of RAM are allocated from the map (0 -> ISA hole
	 * and end of ISA hole -> end of RAM).
	 */
	ioport_ex = extent_create("ioport", 0x0, 0xffff, M_DEVBUF,
	    (caddr_t)ioport_ex_storage, sizeof(ioport_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);
	iomem_ex = extent_create("iomem", 0x0, 0xffffffff, M_DEVBUF,
	    (caddr_t)iomem_ex_storage, sizeof(iomem_ex_storage),
	    EX_NOCOALESCE|EX_NOWAIT);

#if NISA > 0
	isa_defaultirq();
#endif

	/*
	 * Attach the glass console early in case we need to display a panic.
	 */
	cninit();

	/*
	 * Saving SSE registers won't work if the save area isn't
	 * 16-byte aligned.
	 */
	if (offsetof(struct user, u_pcb.pcb_savefpu) & 0xf)
		panic("init386: pcb_savefpu not 16-byte aligned");

	/* call pmap initialization to make new kernel address space */
	pmap_bootstrap((vaddr_t)atdevbase + IOM_SIZE);

	/*
	 * Boot arguments are in a single page specified by /boot.
	 *
	 * We require the "new" vector form, as well as memory ranges
	 * to be given in bytes rather than KB.
	 */
	if ((bootapiver & (BAPIV_VECTOR | BAPIV_BMEMMAP)) ==
	    (BAPIV_VECTOR | BAPIV_BMEMMAP)) {
		if (bootargc > NBPG)
			panic("too many boot args");

		if (extent_alloc_region(iomem_ex, (paddr_t)bootargv, bootargc,
		    EX_NOWAIT))
			panic("cannot reserve /boot args memory");

		pmap_enter(pmap_kernel(), (vaddr_t)bootargp, (paddr_t)bootargv,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);

		bios_getopt();

	} else
		panic("/boot too old: upgrade!");

#ifdef DIAGNOSTIC
	if (bios_memmap == NULL)
		panic("no BIOS memory map supplied");
#endif

	/*
	 * account all the memory passed in the map from /boot
	 * calculate avail_end and count the physmem.
	 */
	avail_end = 0;
	physmem = 0;
#ifdef DEBUG
	printf("memmap:");
#endif
	for(i = 0, im = bios_memmap; im->type != BIOS_MAP_END; im++)
		if (im->type == BIOS_MAP_FREE) {
			paddr_t a, e;
#ifdef DEBUG
			printf(" %llx-%llx", im->addr, im->addr + im->size);
#endif

			if (im->addr >= 0x100000000ULL) {
#ifdef DEBUG
				printf("-H");
#endif
				continue;
			}

			a = round_page(im->addr);
			if (im->addr + im->size <= 0xfffff000ULL)
				e = trunc_page(im->addr + im->size);
			else {
#ifdef DEBUG
				printf("-T");
#endif
				e = 0xfffff000;
			}

			/* skip first 16 pages due to SMI corruption */
			if (a < 16 * NBPG)
				a = 16 * NBPG;

#ifdef MULTIPROCESSOR
			/* skip MP trampoline code page */
			if (a < MP_TRAMPOLINE + NBPG)
				a = MP_TRAMPOLINE + NBPG;

			/* skip MP trampoline data page */
			if (a < MP_TRAMP_DATA + NBPG)
				a = MP_TRAMP_DATA + NBPG;
#endif /* MULTIPROCESSOR */

#if NACPI > 0 && !defined(SMALL_KERNEL)
			/* skip ACPI resume trampoline code page */
			if (a < ACPI_TRAMPOLINE + NBPG)
				a = ACPI_TRAMPOLINE + NBPG;

			/* skip ACPI resume trampoline data page */
			if (a < ACPI_TRAMP_DATA + NBPG)
				a = ACPI_TRAMP_DATA + NBPG;
#endif /* ACPI */

#ifdef HIBERNATE
			/* skip hibernate reserved pages */
			if (a < HIBERNATE_HIBALLOC_PAGE + PAGE_SIZE)
				a = HIBERNATE_HIBALLOC_PAGE + PAGE_SIZE;
#endif /* HIBERNATE */

			/* skip shorter than page regions */
			if (a >= e || (e - a) < NBPG) {
#ifdef DEBUG
				printf("-S");
#endif
				continue;
			}

			/*
			 * XXX Some buggy ACPI BIOSes use memory that
			 * they declare as free. Current worst offender
			 * is Supermicro 5019D-FTN4.  Typically the
			 * affected memory areas are small blocks
			 * between areas reserved for ACPI and other
			 * BIOS goo.  So skip areas smaller than 32 MB
			 * above the 16 MB boundary (to avoid
			 * affecting legacy stuff).
			 */
			if (a > 16*1024*1024 && (e - a) < 32*1024*1024) {
#ifdef DEBUG
				printf("-X");
#endif
				continue;
			}

			/* skip legacy IO region */
			if ((a > IOM_BEGIN && a < IOM_END) ||
			    (e > IOM_BEGIN && e < IOM_END)) {
#ifdef DEBUG
				printf("-I");
#endif
				continue;
			}

			if (extent_alloc_region(iomem_ex, a, e - a, EX_NOWAIT))
				/* XXX What should we do? */
				printf("\nWARNING: CAN'T ALLOCATE RAM (%lx-%lx)"
				    " FROM IOMEM EXTENT MAP!\n", a, e);

			physmem += atop(e - a);
			dumpmem[i].start = atop(a);
			dumpmem[i].end = atop(e);
			i++;
			avail_end = max(avail_end, e);
		}

	ndumpmem = i;
	avail_end -= round_page(MSGBUFSIZE);

#ifdef DEBUG
	printf(": %lx\n", avail_end);
#endif
	if (physmem < atop(4 * 1024 * 1024)) {
		printf("\awarning: too little memory available;"
		    "running in degraded mode\npress a key to confirm\n\n");
		cnpollc(1);
		cngetc();
		cnpollc(0);
	}

#ifdef DEBUG
	printf("physload: ");
#endif
	kb = atop(KERNTEXTOFF - KERNBASE);
	if (kb > atop(IOM_END)) {
		paddr_t lim = atop(IOM_END);
#ifdef DEBUG
		printf(" %lx-%x (<16M)", lim, kb);
#endif
		uvm_page_physload(lim, kb, lim, kb, 0);
	}

	for (i = 0; i < ndumpmem; i++) {
		paddr_t a, e;

		a = dumpmem[i].start;
		e = dumpmem[i].end;
		if (a < atop(first_avail) && e > atop(first_avail))
			a = atop(first_avail);
		if (e > atop(avail_end))
			e = atop(avail_end);

		if (a < e) {
#ifdef DEBUG
				printf(" %lx-%lx", a, e);
#endif
				uvm_page_physload(a, e, a, e, 0);
		}
	}
#ifdef DEBUG
	printf("\n");
#endif

	tlbflush();
#if 0
#if NISADMA > 0
	/*
	 * Some motherboards/BIOSes remap the 384K of RAM that would
	 * normally be covered by the ISA hole to the end of memory
	 * so that it can be used.  However, on a 16M system, this
	 * would cause bounce buffers to be allocated and used.
	 * This is not desirable behaviour, as more than 384K of
	 * bounce buffers might be allocated.  As a work-around,
	 * we round memory down to the nearest 1M boundary if
	 * we're using any isadma devices and the remapped memory
	 * is what puts us over 16M.
	 */
	if (extmem > (15*1024) && extmem < (16*1024)) {
		printf("Warning: ignoring %dk of remapped memory\n",
		    extmem - (15*1024));
		extmem = (15*1024);
	}
#endif
#endif

#ifdef DDB
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB)
		db_enter();
#endif

	softintr_init();
}

/*
 * consinit:
 * initialize the system console.
 */
void
consinit(void)
{
	/* Already done in init386(). */
}

void
cpu_reset(void)
{
	struct region_descriptor region;

	intr_disable();

	if (cpuresetfn)
		(*cpuresetfn)();

	/*
	 * The keyboard controller has 4 random output pins, one of which is
	 * connected to the RESET pin on the CPU in many PCs.  We tell the
	 * keyboard controller to pulse this line a couple of times.
	 */
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);
	outb(IO_KBD + KBCMDP, KBC_PULSE0);
	delay(100000);

	/*
	 * Try to cause a triple fault and watchdog reset by setting the
	 * IDT to point to nothing.
	 */
	bzero((caddr_t)idt, sizeof(idt_region));
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);
	__asm volatile("divl %0,%1" : : "q" (0), "a" (0));

	/*
	 * Try to cause a triple fault and watchdog reset by unmapping the
	 * entire address space.
	 */
	bzero((caddr_t)PTD, NBPG);
	tlbflush();

	for (;;)
		continue;
	/* NOTREACHED */
}

void
cpu_initclocks(void)
{
	(*initclock_func)();		/* lapic or i8254 */
}

void
cpu_startclock(void)
{
	(*startclock_func)();
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		aston(ci->ci_curproc);
		cpu_kick(ci);
	}
}

/* Allocate an IDT vector slot within the given range.
 * XXX needs locking to avoid MP allocation races.
 */

int
idt_vec_alloc(int low, int high)
{
	int vec;

	for (vec = low; vec <= high; vec++)
		if (idt[vec].gd_p == 0)
			return (vec);
	return (0);
}

void
idt_vec_set(int vec, void (*function)(void))
{
	setgate(&idt[vec], function, 0, SDT_SYS386IGT, SEL_KPL, GICODE_SEL);
}

void
idt_vec_free(int vec)
{
	unsetgate(&idt[vec]);
}

const struct sysctl_bounded_args cpuctl_vars[] = {
	{ CPU_LIDACTION, &lid_action, 0, 2 },
	{ CPU_CPUID, &cpu_id, SYSCTL_INT_READONLY },
	{ CPU_OSFXSR, &i386_use_fxsave, SYSCTL_INT_READONLY },
	{ CPU_SSE, &i386_has_sse, SYSCTL_INT_READONLY },
	{ CPU_SSE2, &i386_has_sse2, SYSCTL_INT_READONLY },
	{ CPU_XCRYPT, &i386_has_xcrypt, SYSCTL_INT_READONLY },
};

/*
 * machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	dev_t dev;

	switch (name[0]) {
	case CPU_CONSDEV:
		if (namelen != 1)
			return (ENOTDIR);		/* overloaded */

		if (cn_tab != NULL)
			dev = cn_tab->cn_dev;
		else
			dev = NODEV;
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
#if NBIOS > 0
	case CPU_BIOS:
		return bios_sysctl(name + 1, namelen - 1, oldp, oldlenp,
		    newp, newlen, p);
#endif
	case CPU_BLK2CHR:
		if (namelen != 2)
			return (ENOTDIR);		/* overloaded */
		dev = blktochr((dev_t)name[1]);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
	case CPU_CHR2BLK:
		if (namelen != 2)
			return (ENOTDIR);		/* overloaded */
		dev = chrtoblk((dev_t)name[1]);
		return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
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
	case CPU_CPUVENDOR:
		return (sysctl_rdstring(oldp, oldlenp, newp, cpu_vendor));
	case CPU_CPUFEATURE:
		return (sysctl_rdint(oldp, oldlenp, newp, curcpu()->ci_feature_flags));
	case CPU_KBDRESET:
		return (sysctl_securelevel_int(oldp, oldlenp, newp, newlen,
		    &kbd_reset));
#if NPCKBC > 0 && NUKBD > 0
	case CPU_FORCEUKBD:
		{
		int error;

		if (forceukbd)
			return (sysctl_rdint(oldp, oldlenp, newp, forceukbd));

		error = sysctl_int(oldp, oldlenp, newp, newlen, &forceukbd);
		if (forceukbd)
			pckbc_release_console();
		return (error);
		}
#endif
	default:
		return (sysctl_bounded_arr(cpuctl_vars, nitems(cpuctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
	/* NOTREACHED */
}

int
bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	int error;
	struct extent *ex;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == I386_BUS_SPACE_IO) {
		ex = ioport_ex;
		if (flags & BUS_SPACE_MAP_LINEAR)
			return (EINVAL);
	} else if (t == I386_BUS_SPACE_MEM) {
		ex = iomem_ex;
	} else {
		panic("bus_space_map: bad bus space tag");
	}

	/*
	 * Before we go any further, let's make sure that this
	 * region is available.
	 */
	error = extent_alloc_region(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0));
	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	if (IOM_BEGIN <= bpa && bpa <= IOM_END) {
		*bshp = (bus_space_handle_t)ISA_HOLE_VADDR(bpa);
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = bus_mem_add_mapping(bpa, size, flags, bshp);
	if (error) {
		if (extent_free(ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}

	return (error);
}

int
_bus_space_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	return (bus_mem_add_mapping(bpa, size, flags, bshp));
}

int
bus_space_alloc(bus_space_tag_t t, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary,
    int flags, bus_addr_t *bpap, bus_space_handle_t *bshp)
{
	struct extent *ex;
	u_long bpa;
	int error;

	/*
	 * Pick the appropriate extent map.
	 */
	if (t == I386_BUS_SPACE_IO) {
		ex = ioport_ex;
	} else if (t == I386_BUS_SPACE_MEM) {
		ex = iomem_ex;
	} else {
		panic("bus_space_alloc: bad bus space tag");
	}

	/*
	 * Sanity check the allocation against the extent's boundaries.
	 */
	if (rstart < ex->ex_start || rend > ex->ex_end)
		panic("bus_space_alloc: bad region start/end");

	/*
	 * Do the requested allocation.
	 */
	error = extent_alloc_subregion(ex, rstart, rend, size, alignment, 0,
	    boundary, EX_NOWAIT | (ioport_malloc_safe ?  EX_MALLOCOK : 0),
	    &bpa);

	if (error)
		return (error);

	/*
	 * For I/O space, that's all she wrote.
	 */
	if (t == I386_BUS_SPACE_IO) {
		*bshp = *bpap = bpa;
		return (0);
	}

	/*
	 * For memory space, map the bus physical address to
	 * a kernel virtual address.
	 */
	error = bus_mem_add_mapping(bpa, size, flags, bshp);
	if (error) {
		if (extent_free(iomem_ex, bpa, size, EX_NOWAIT |
		    (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
			printf("bus_space_alloc: pa 0x%lx, size 0x%lx\n",
			    bpa, size);
			printf("bus_space_alloc: can't free region\n");
		}
	}

	*bpap = bpa;

	return (error);
}

int
bus_mem_add_mapping(bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	paddr_t pa, endpa;
	vaddr_t va;
	bus_size_t map_size;
	int pmap_flags = PMAP_NOCACHE;

	pa = trunc_page(bpa);
	endpa = round_page(bpa + size);

#ifdef DIAGNOSTIC
	if (endpa <= pa && endpa != 0)
		panic("bus_mem_add_mapping: overflow");
#endif

	map_size = endpa - pa;

	va = (vaddr_t)km_alloc(map_size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return (ENOMEM);

	*bshp = (bus_space_handle_t)(va + (bpa & PGOFSET));

	if (flags & BUS_SPACE_MAP_CACHEABLE)
		pmap_flags = 0;
	else if (flags & BUS_SPACE_MAP_PREFETCHABLE)
		pmap_flags = PMAP_WC;

	for (; map_size > 0;
	    pa += PAGE_SIZE, va += PAGE_SIZE, map_size -= PAGE_SIZE)
		pmap_kenter_pa(va, pa | pmap_flags,
		    PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	return 0;
}

void
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
	struct extent *ex;
	u_long va, endva;
	bus_addr_t bpa;

	/*
	 * Find the correct extent and bus physical address.
	 */
	if (t == I386_BUS_SPACE_IO) {
		ex = ioport_ex;
		bpa = bsh;
	} else if (t == I386_BUS_SPACE_MEM) {
		ex = iomem_ex;
		bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		if (IOM_BEGIN <= bpa && bpa <= IOM_END)
			goto ok;

		va = trunc_page(bsh);
		endva = round_page(bsh + size);

#ifdef DIAGNOSTIC
		if (endva <= va)
			panic("bus_space_unmap: overflow");
#endif

		(void) pmap_extract(pmap_kernel(), va, &bpa);
		bpa += (bsh & PGOFSET);

		pmap_kremove(va, endva - va);
		pmap_update(pmap_kernel());

		/*
		 * Free the kernel virtual mapping.
		 */
		km_free((void *)va, endva - va, &kv_any, &kp_none);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (extent_free(ex, bpa, size,
	    EX_NOWAIT | (ioport_malloc_safe ? EX_MALLOCOK : 0))) {
		printf("bus_space_unmap: %s 0x%lx, size 0x%lx\n",
		    (t == I386_BUS_SPACE_IO) ? "port" : "pa", bpa, size);
		printf("bus_space_unmap: can't free region\n");
	}
}

void
_bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size,
    bus_addr_t *adrp)
{
	u_long va, endva;
	bus_addr_t bpa;

	/*
	 * Find the correct bus physical address.
	 */
	if (t == I386_BUS_SPACE_IO) {
		bpa = bsh;
	} else if (t == I386_BUS_SPACE_MEM) {
		bpa = (bus_addr_t)ISA_PHYSADDR(bsh);
		if (IOM_BEGIN <= bpa && bpa <= IOM_END)
			goto ok;

		va = trunc_page(bsh);
		endva = round_page(bsh + size);

#ifdef DIAGNOSTIC
		if (endva <= va)
			panic("_bus_space_unmap: overflow");
#endif

		(void) pmap_extract(pmap_kernel(), va, &bpa);
		bpa += (bsh & PGOFSET);

		pmap_kremove(va, endva - va);
		pmap_update(pmap_kernel());

		/*
		 * Free the kernel virtual mapping.
		 */
		km_free((void *)va, endva - va, &kv_any, &kp_none);
	} else
		panic("bus_space_unmap: bad bus space tag");

ok:
	if (adrp != NULL)
		*adrp = bpa;
}

void
bus_space_free(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{

	/* bus_space_unmap() does all that we need to do. */
	bus_space_unmap(t, bsh, size);
}

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return (0);
}

paddr_t
bus_space_mmap(bus_space_tag_t t, bus_addr_t addr, off_t off, int prot, int flags)
{
	/* Can't mmap I/O space. */
	if (t == I386_BUS_SPACE_IO)
		return (-1);

	return (addr + off);
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	if (lapic_tpr < wantipl)
		splassert_fail(wantipl, lapic_tpr, func);
	if (wantipl == IPL_NONE && curcpu()->ci_idepth != 0)
		splassert_fail(-1, curcpu()->ci_idepth, func);
}
#endif

int
copyin32(const uint32_t *uaddr, uint32_t *kaddr)
{
	if ((vaddr_t)uaddr & 0x3)
		return EFAULT;

	/* copyin(9) is atomic */
	return copyin(uaddr, kaddr, sizeof(uint32_t));
}

/*
 * True if the system has any non-level interrupts which are shared
 * on the same pin.
 */
int	intr_shared_edge;

/*
 * Software interrupt registration
 *
 * We hand-code this to ensure that it's atomic.
 */
void
softintr(int si_level)
{
	struct cpu_info *ci = curcpu();
	int sir = softintr_to_ssir[si_level];

	__asm volatile("orl %1, %0" :
	    "=m" (ci->ci_ipending) : "ir" (1 << sir));
}

/*
 * Raise current interrupt priority level, and return the old one.
 */
int
splraise(int ncpl)
{
	int ocpl;

	KASSERT(ncpl >= IPL_NONE);

	_SPLRAISE(ocpl, ncpl);
	return (ocpl);
}

/*
 * Restore an old interrupt priority level.  If any thereby unmasked
 * interrupts are pending, call Xspllower() to process them.
 */
void
splx(int ncpl)
{
	_SPLX(ncpl);
}

/*
 * Same as splx(), but we return the old value of spl, for the
 * benefit of some splsoftclock() callers.
 */
int
spllower(int ncpl)
{
	int ocpl = lapic_tpr;

	splx(ncpl);
	return (ocpl);
}

int
intr_handler(struct intrframe *frame, struct intrhand *ih)
{
	int rc;
#ifdef MULTIPROCESSOR
	int need_lock;

	if (ih->ih_flags & IPL_MPSAFE)
		need_lock = 0;
	else
		need_lock = 1;

	if (need_lock)
		__mp_lock(&kernel_lock);
#endif
	rc = (*ih->ih_fun)(ih->ih_arg ? ih->ih_arg : frame);
#ifdef MULTIPROCESSOR
	if (need_lock)
		__mp_unlock(&kernel_lock);
#endif
	return rc;
}

void
intr_barrier(void *ih)
{
	sched_barrier(NULL);
}

#ifdef SUSPEND

void
intr_enable_wakeup(void)
{
}

void
intr_disable_wakeup(void)
{
}

#endif

unsigned int
cpu_rnd_messybits(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (ts.tv_nsec ^ (ts.tv_sec << 20));
}

int i386_delay_quality;

void
delay_init(void(*fn)(int), int fn_quality)
{
	if (fn_quality > i386_delay_quality) {
		delay_func = fn;
		i386_delay_quality = fn_quality;
	}
}

void
delay_fini(void (*fn)(int))
{
	if (delay_func == fn) {
		delay_func = i8254_delay;
		i386_delay_quality = 0;
	}
}
