/*	$OpenBSD: machdep.c,v 1.146 2025/06/26 20:28:07 miod Exp $	*/
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
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
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/msgbuf.h>
#include <sys/syscallargs.h>
#include <sys/exec.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/extent.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <machine/asm.h>
#include <machine/asm_macro.h>
#include <machine/board.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/kcore.h>
#include <machine/reg.h>
#include <machine/trap.h>
#include <machine/m88100.h>

#include <luna88k/luna88k/isr.h>

#include <dev/cons.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>

#include "ksyms.h"
#if DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#include <ddb/db_output.h>		/* db_printf()		*/
#endif /* DDB */

void	consinit(void);
#ifdef MULTIPROCESSOR
void	cpu_boot_secondary_processors(void);
#endif
void	cpu_setup_secondary_processors(void);
void	dumpconf(void);
void	dumpsys(void);
int	getcpuspeed(void);
void	get_fuse_rom_data(void);
void	get_nvram_data(void);
void	identifycpu(void);
void	luna88k_bootstrap(void);
#ifdef MULTIPROCESSOR
void	luna88k_ipi_handler(struct trapframe *);
#endif
struct cpu_info *luna88k_set_cpu_number(cpuid_t);
void	luna88k_vector_init(uint32_t *, uint32_t *);
char	*nvram_by_symbol(char *);
void	powerdown(void);
void	savectx(struct pcb *);
void	secondary_main(void);
void   *secondary_pre_main(void);
void	setlevel(u_int);
vaddr_t size_memory(void);

extern int	clockintr(void *);		/* in clock.c */
extern void	get_autoboot_device(void);	/* in autoconf.c */

u_int32_t int_set_val[INT_LEVEL] = {
	INT_SET_LV0,
	INT_SET_LV1,
	INT_SET_LV2,
	INT_SET_LV3,
	INT_SET_LV4,
	INT_SET_LV5,
	INT_SET_LV6,
	INT_SET_LV7
};

/*
 * FUSE ROM and NVRAM data
 */
struct fuse_rom_byte {
	u_int32_t h;
	u_int32_t l;
};
#define FUSE_ROM_BYTES	(FUSE_ROM_SPACE / sizeof(struct fuse_rom_byte))
char fuse_rom_data[FUSE_ROM_BYTES];

#define NNVSYM		8
#define NVSYMLEN	16
#define NVVALLEN	16
struct nvram_t {
	char symbol[NVSYMLEN];
	char value[NVVALLEN];
} nvram[NNVSYM];

register_t kernel_vbr;
int physmem;	  /* available physical memory, in pages */

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

__cpu_simple_lock_t cpu_hatch_mutex = __SIMPLELOCK_UNLOCKED;
#ifdef MULTIPROCESSOR
__cpu_simple_lock_t cpu_boot_mutex = __SIMPLELOCK_LOCKED;
unsigned int hatch_pending_count;
vaddr_t hatch_stacks[MAXCPUS - 1];
#endif

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

/*
 * Info for CTL_HW
 */
char  machine[] = MACHINE;	 /* cpu "architecture" */
char  cpu_model[120];

#if defined(DDB) || NKSYMS > 0
extern char *esym;
#endif

int machtype = LUNA_88K;	/* may be overwritten in cpu_startup() */
int cputyp = CPU_88100;
int cpuspeed = 33;		/* safe guess */
int sysconsole = 0;		/* 0 = ttya, may be overwritten in locore0.S */
u_int16_t dipswitch = 0;	/* set in locore0.S */
int hwplanebits;		/* set in locore0.S */

extern struct consdev syscons;	/* in dev/siotty.c */

extern void syscnattach(int);	/* in dev/siotty.c */
extern int omfb_cnattach(void);	/* in dev/lunafb.c */
extern void ws_cnattach(void);	/* in dev/lunaws.c */

vaddr_t first_addr;
vaddr_t last_addr;

extern struct user *proc0paddr;

/*
 * Early console initialization: called early on from main, before vm init.
 */
void
consinit()
{
	/*
	 * Initialize the console before we print anything out.
	 */
	if (sysconsole == 0) {
		syscnattach(0);
	} else {
		omfb_cnattach();
		ws_cnattach();
	}

#if defined(DDB)
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB)
		db_enter();
#endif
}

/*
 * Figure out how much real memory is available.
 * Start looking from the megabyte after the end of the kernel data,
 * until we find non-memory.
 */
vaddr_t
size_memory()
{
	unsigned int *volatile look;
	unsigned int *max;
#define PATTERN   0x5a5a5a5a
#define STRIDE    (4*1024) 	/* 4k at a time */
#define Roundup(value, stride) (((unsigned)(value) + (stride) - 1) & ~((stride)-1))
	/*
	 * count it up.
	 */
	max = (void *)MAXPHYSMEM;
	for (look = (void *)first_addr; look < max;
	    look = (int *)((unsigned)look + STRIDE)) {
		unsigned save;

		/* if can't access, we've reached the end */
		if (badaddr((vaddr_t)look, 4)) {
			look = (int *)((int)look - STRIDE);
			break;
		}

		/*
		 * If we write a value, we expect to read the same value back.
		 * We'll do this twice, the 2nd time with the opposite bit
		 * pattern from the first, to make sure we check all bits.
		 */
		save = *look;
		if (*look = PATTERN, *look != PATTERN)
			break;
		if (*look = ~PATTERN, *look != ~PATTERN)
			break;
		*look = save;
	}

	return (trunc_page((vaddr_t)look));
}

int
getcpuspeed()
{
	switch(machtype) {
	case LUNA_88K:
		return 25;
	case LUNA_88K2:
		return 33;
	default:
		panic("getcpuspeed: can not determine CPU speed");
	}
}

void
identifycpu()
{
	cpuspeed = getcpuspeed();
	snprintf(cpu_model, sizeof cpu_model,
	    "OMRON LUNA-88K%s, %dMHz", 
	    machtype == LUNA_88K2 ? "2" : "", cpuspeed);
}

void
cpu_startup()
{
	vaddr_t minaddr, maxaddr;

	/* Determine the machine type from FUSE ROM data.  */
	get_fuse_rom_data();
	if (strncmp(fuse_rom_data, "MNAME=LUNA88K+", 14) == 0) {
		machtype = LUNA_88K2;
	}

	/* Determine the 'auto-boot' device from NVRAM data */
	get_nvram_data();
	get_autoboot_device();

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf("%s", version);
	identifycpu();
	printf("real mem = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem) / 1024 / 1024);

	/*
	 * Check front DIP switch setting
	 */
#ifdef DEBUG
	printf("dipsw = 0x%x\n", dipswitch);
#endif

	/* Check DIP switch 1 - 1 */
	if ((0x8000 & dipswitch) == 0) {
		boothowto |= RB_SINGLE;
	}

	/* Check DIP switch 1 - 3 */
	if ((0x2000 & dipswitch) == 0) {
		boothowto |= RB_ASKNAME;
	}

	/* Check DIP switch 1 - 4 */
	if ((0x1000 & dipswitch) == 0) {
		boothowto |= RB_CONFIG;
	}

	/*
	 * Check frame buffer depth.
	 */
	switch (hwplanebits) {
	case 0:				/* No frame buffer */
	case 1:
	case 4:
	case 8:
		break;
	default:
		printf("unexpected frame buffer depth = %d\n", hwplanebits);
		hwplanebits = 0;
		break;
	}

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate map for physio.
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free) / 1024 / 1024);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	/*
	 * Initialize the autovectored interrupt list.
	 */
	isrinit();

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
}

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (curproc && curproc->p_addr)
		savectx(curpcb);

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0) {
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

	/* LUNA-88K supports automatic powerdown */
	if ((howto & RB_POWERDOWN) != 0) {
		printf("attempting to power down...\n");
		powerdown();
		/* if failed, fall through. */
	}

	if ((howto & RB_HALT) != 0) {
		printf("halted\n\n");
	} else {
doreset:
		/* Reset all cpus, which causes reboot */
		*((volatile uint32_t *)RESET_CPU_ALL) = 0;
	}

	for (;;)
		continue;
	/* NOTREACHED */
}

u_long dumpmag = 0x8fca0101;	 /* magic number for savecore */
int   dumpsize = 0;	/* also for savecore */
long  dumplo = 0;
cpu_kcore_hdr_t cpu_kcore_hdr;

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpsize = physmem;

	/* luna88k only uses a single segment. */
	cpu_kcore_hdr.ram_segs[0].start = 0;
	cpu_kcore_hdr.ram_segs[0].size = ptoa(physmem);
	cpu_kcore_hdr.cputype = cputyp;

	/*
	 * Don't dump on the first block
	 * in case the dump device includes a disk label.
	 */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize + 1 > dtoc(nblks - dumplo))
		dumpsize = dtoc(nblks - dumplo) - 1;
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
void
dumpsys()
{
	int maj;
	int psize;
	daddr_t blkno;	/* current block to write */
				/* dump routine */
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int pg;			/* page being dumped */
	paddr_t maddr;		/* PA being dumped */
	int error;		/* error code from (*dump)() */
	kcore_seg_t *kseg_p;
	cpu_kcore_hdr_t *chdr_p;
	char dump_hdr[dbtob(1)];	/* XXX assume hdr fits in 1 block */

	extern int msgbufmapped;

	msgbufmapped = 0;

	/* Make sure dump device is valid. */
	if (dumpdev == NODEV)
		return;
	if (dumpsize == 0) {
		dumpconf();
		if (dumpsize == 0)
			return;
	}
	maj = major(dumpdev);
	if (dumplo < 0) {
		printf("\ndump to dev %u,%u not possible\n", maj,
		    minor(dumpdev));
		return;
	}
	dump = bdevsw[maj].d_dump;
	blkno = dumplo;

	printf("\ndumping to dev %u,%u offset %ld\n", maj,
	    minor(dumpdev), dumplo);

	/* Setup the dump header */
	kseg_p = (kcore_seg_t *)dump_hdr;
	chdr_p = (cpu_kcore_hdr_t *)&dump_hdr[ALIGN(sizeof(*kseg_p))];
	bzero(dump_hdr, sizeof(dump_hdr));

	CORE_SETMAGIC(*kseg_p, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg_p->c_size = dbtob(1) - ALIGN(sizeof(*kseg_p));
	*chdr_p = cpu_kcore_hdr;

	printf("dump ");
	psize = (*bdevsw[maj].d_psize)(dumpdev);
	if (psize == -1) {
		printf("area unavailable\n");
		return;
	}

	/* Dump the header. */
	error = (*dump)(dumpdev, blkno++, (caddr_t)dump_hdr, dbtob(1));
	if (error != 0)
		goto abort;

	maddr = (paddr_t)0;
	for (pg = 0; pg < dumpsize; pg++) {
#define NPGMB	(1024 * 1024 / PAGE_SIZE)
		/* print out how many MBs we have dumped */
		if (pg != 0 && (pg % NPGMB) == 0)
			printf("%d ", pg / NPGMB);
#undef NPGMB
		error = (*dump)(dumpdev, blkno, (caddr_t)maddr, PAGE_SIZE);
		if (error == 0) {
			maddr += PAGE_SIZE;
			blkno += btodb(PAGE_SIZE);
		} else
			break;
	}
abort:
	switch (error) {
	case 0:
		printf("succeeded\n");
		break;

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

	default:
		printf("error %d\n", error);
		break;
	}
}

/*
 * Release cpu_hatch_mutex to let secondary processors initialize.
 */
void
cpu_setup_secondary_processors()
{
#ifdef MULTIPROCESSOR
	unsigned int cpu;

	hatch_pending_count = ncpusfound - 1;

	/*
	 * Allocate idle stack for all the secondary processors here.
	 *
	 * We can't have this done by the secondaries themselves, because
	 * the main processor owns the kernel lock at this point; and we
	 * can't know in advance which cpuid our secondary processors will
	 * have, so we can't fill m88k_cpus[] directly.
	 *
	 * Allocation failure will be checked by the secondary processors
	 * so that we can still run in degraded mode if hell gets loose.
	 */
	for (cpu = 0; cpu < hatch_pending_count; cpu++)
		hatch_stacks[cpu] = (vaddr_t)km_alloc(USPACE, &kv_any,
		    &kp_zero, &kd_waitok);
#endif

	__cpu_simple_unlock(&cpu_hatch_mutex);

#ifdef MULTIPROCESSOR
	while (hatch_pending_count != 0)
		delay(10000);	/* 10ms */
#endif
}

struct cpu_info *
luna88k_set_cpu_number(cpuid_t number)
{
	struct cpu_info *ci;

	/* clock register for each CPU. */
	static const uint32_t clock_ack[] = {
		OBIO_CLOCK0,
		OBIO_CLOCK1,
		OBIO_CLOCK2,
		OBIO_CLOCK3
	};

	/* hardware interrupt mask and status register for each CPU.
	 *
	 * When written to:
	 * Bits 31 to 26 are used to enable ('1') or disable ('0') each
	 * interrupt level.  Bit 31 is for level 6, bit 26 is for level 1.
	 * 
	 * When read:
	 * Bits 31 to 29 shows the highest level of current (or most recent?)
	 * interrupt in 3 bits binary value (0 to 7).
	 * Bits 23 to 18 shows the current mask, which is the most recent
	 * written value in bits 31 to 26 as described above.
	 */
	static const uint32_t intr_mask[] = {
		INT_ST_MASK0,
		INT_ST_MASK1,
		INT_ST_MASK2,
		INT_ST_MASK3
	};

	/* software interrupt register for each CPU. */
	static const uint32_t swi_reg[] = {
		SOFT_INT0,
		SOFT_INT1,
		SOFT_INT2,
		SOFT_INT3
	};

	ci = set_cpu_number(number);
	ci->ci_curspl = IPL_HIGH;
	ci->ci_swireg = swi_reg[number];
	ci->ci_intr_mask = intr_mask[number];
	ci->ci_clock_ack = clock_ack[number];
	return ci;
}

#ifdef MULTIPROCESSOR
/*
 * Release cpu_boot_mutex to let secondary processors start running
 * processes.
 */
void
cpu_boot_secondary_processors()
{
	__cpu_simple_unlock(&cpu_boot_mutex);
}

/*
 * Secondary CPU early initialization routine.
 * Determine CPU number and set it, then return the startup stack.
 *
 * Running on a minimal stack here, with interrupts disabled; do nothing fancy.
 */
void *
secondary_pre_main()
{
	struct cpu_info *ci;

	/*
	 * Invoke the CMMU initialization routine as early as possible,
	 * so that we do not risk any memory writes to be lost during
	 * cache setup.
	 */
	cmmu_initialize_cpu(cmmu_cpu_number());

	/*
	 * Now initialize your cpu_info structure.
	 */
	ci = luna88k_set_cpu_number(cmmu_cpu_number());
	ci->ci_curproc = &proc0;
	m88100_smp_setup(ci);

	splhigh();

	/*
	 * Enable MMU on this processor.
	 */
	pmap_bootstrap_cpu(ci->ci_cpuid);

	/*
	 * Return our idle stack for the caller to switch to it.
	 */
	ci->ci_curpcb = (void *)hatch_stacks[hatch_pending_count - 1];
	if (ci->ci_curpcb == NULL) {
		printf("cpu%d: unable to get startup stack\n", ci->ci_cpuid);
		hatch_pending_count--;
		__cpu_simple_unlock(&cpu_hatch_mutex);
		for (;;)
			continue;
		/* NOTREACHED */
	}

	return ci->ci_curpcb;
}

/*
 * Further secondary CPU initialization.
 *
 * We are now running on our startup stack, with proper page tables.
 * There is nothing to do but display some details about the CPU and its CMMUs.
 */

void
secondary_main()
{
	struct cpu_info *ci = curcpu();

	cpu_configuration_print(0);
	ncpus++;

	clockqueue_init(&ci->ci_queue);
	sched_init_cpu(ci);
	ci->ci_curproc = NULL;
	ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;

	/*
	 * Release cpu_hatch_mutex to let other secondary processors
	 * have a chance to run.
	 */
	hatch_pending_count--;
	__cpu_simple_unlock(&cpu_hatch_mutex);

	/* wait for cpu_boot_secondary_processors() */
	__cpu_simple_lock(&cpu_boot_mutex);
	__cpu_simple_unlock(&cpu_boot_mutex);

	set_vbr(kernel_vbr);

	clockintr_cpu_init(NULL);

	spl0();
	set_psr(get_psr() & ~PSR_IND);

	SET(ci->ci_flags, CIF_ALIVE);

	sched_toidle();
}

#endif	/* MULTIPROCESSOR */

/*
 *	Device interrupt handler for LUNA-88K
 */

void 
luna88k_ext_int(struct trapframe *eframe)
{
	struct cpu_info *ci = curcpu();
	uint32_t cur_isr;
	u_int level, cur_int_level, old_spl;
	int unmasked = 0;

	cur_isr = *(volatile uint32_t *)ci->ci_intr_mask;
	old_spl = eframe->tf_mask;

	cur_int_level = cur_isr >> 29;

	/*
	 * Ignore level 0 interrupt and 'hardware lied' interrupt,
	 * as same as CMU Mach do.  The 'hardware lied' means that
	 * the received interrupt level is what we have masked before.
	 */
	if (cur_int_level == 0 ||
	    !(cur_isr & (1 << (cur_int_level + 17))))
		goto out;

	uvmexp.intrs++;

#ifdef MULTIPROCESSOR
	/*
	 * Handle unmaskable IPIs immediately, so that we can reenable
	 * interrupts before further processing. We rely on the interrupt
	 * mask to make sure that if we get an IPI, it's really for us
	 * and no other processor.
	 * 
	 * On luna88k, IPL_SOFTINT (level 1 interrupt) is used as IPI.
	 */
	while (cur_int_level == IPL_SOFTINT) {
		luna88k_ipi_handler(eframe);

		cur_isr = *(volatile uint32_t *)ci->ci_intr_mask;
		cur_int_level = cur_isr >> 29;
	}
	if (cur_int_level == 0)
		goto out;
#endif

	/*
	 * Service the highest interrupt, in order.
	 */
	do {
		level = (cur_int_level > old_spl ? cur_int_level : old_spl);
		setipl(level);

		if (unmasked == 0) {
			set_psr(get_psr() & ~PSR_IND);
			unmasked = 1;
		}

		switch (cur_int_level) {
		case 6:
			clockintr((void *)eframe);
			break;
		case 5:
		case 4:
		case 3:
			if (CPU_IS_PRIMARY(ci))
				isrdispatch_autovec(cur_int_level);
			break;
#ifdef MULTIPROCESSOR
		case 1:
			/*
			 * Another processor may have sent us an IPI
			 * while we were servicing a device interrupt.
			 */
			set_psr(get_psr() | PSR_IND);
			luna88k_ipi_handler(eframe);
			set_psr(get_psr() & ~PSR_IND);
			break;
#endif
		default:
			printf("%s: cpu%d level %d interrupt.\n",
				__func__, ci->ci_cpuid, cur_int_level);
			break;
		}

		cur_isr = *(volatile uint32_t *)ci->ci_intr_mask;
		cur_int_level = cur_isr >> 29;

		/* Again, ignore 'hardware lied' interrupt */
		if ( !(cur_isr & (1 << (cur_int_level + 17))))
			goto out;

	} while (cur_int_level != 0);

out:
	/*
	 * process any remaining data access exceptions before
	 * returning to assembler
	 */
	if (eframe->tf_dmt0 & DMT_VALID)
		m88100_trap(T_DATAFLT, eframe);

	/*
	 * Disable interrupts before returning to assembler, the spl will
	 * be restored later.
	 */
	set_psr(get_psr() | PSR_IND);
}

int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
#if 0
	struct sys_sysarch_args	/* {
	   syscallarg(int) op;
	   syscallarg(char *) parm;
	} */ *uap = v;
#endif

	return (ENOSYS);
}

/*
 * machine dependent system variables.
 */

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	dev_t consdev;

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR); /* overloaded */

	switch (name[0]) {
	case CPU_CONSDEV:
		if (cn_tab != NULL)
			consdev = cn_tab->cn_dev;
		else
			consdev = NODEV;
		return (sysctl_rdstruct(oldp, oldlenp, newp, &consdev,
		    sizeof consdev));
	case CPU_CPUTYPE:
		return (sysctl_rdint(oldp, oldlenp, newp, cputyp));
	default:
		return (EOPNOTSUPP);
	}
	/*NOTREACHED*/
}

void
luna88k_vector_init(uint32_t *bootvbr, uint32_t *vectors)
{
	extern vaddr_t vector_init(uint32_t *, uint32_t *, int); /* gross */
	extern int kernelstart;

	/*
	 * Set up bootstrap vectors, overwriting the existing PROM vbr
	 * page.
	 */
	vector_init(bootvbr, vectors, 1);

	/*
	 * Set up final vectors. These will be used by all processors,
	 * once autoconf is over.
	 */
	kernel_vbr = trunc_page((vaddr_t)&kernelstart);
	vector_init((uint32_t *)kernel_vbr, vectors, 0);
}

/*
 * Called from locore0.S during boot,
 * this is the first C code that's run.
 */
void
luna88k_bootstrap()
{
	extern const struct cmmu_p cmmu8820x;
	extern vaddr_t avail_start;
	extern vaddr_t avail_end;
#ifndef MULTIPROCESSOR
	cpuid_t master_cpu;
#endif

	cmmu = &cmmu8820x;

	/* clear and disable all interrupts */
	*(volatile uint32_t *)INT_ST_MASK0 = 0;
	*(volatile uint32_t *)INT_ST_MASK1 = 0;
	*(volatile uint32_t *)INT_ST_MASK2 = 0;
	*(volatile uint32_t *)INT_ST_MASK3 = 0;

	/* clear software interrupts; just read registers */
	*(volatile uint32_t *)SOFT_INT0;
	*(volatile uint32_t *)SOFT_INT1;
	*(volatile uint32_t *)SOFT_INT2;
	*(volatile uint32_t *)SOFT_INT3;

	uvmexp.pagesize = PAGE_SIZE;
	uvm_setpagesize();

	first_addr = round_page(first_addr);
	last_addr = size_memory();
	physmem = atop(last_addr);

	setup_board_config();
	master_cpu = cmmu_init();
	(void)luna88k_set_cpu_number(master_cpu);
#ifdef MULTIPROCESSOR
	m88100_smp_setup(curcpu());
#endif
	SET(curcpu()->ci_flags, CIF_ALIVE | CIF_PRIMARY);

	m88100_apply_patches();

	/*
	 * Now that set_cpu_number() set us with a valid cpu_info pointer,
	 * we need to initialize p_addr and curpcb before autoconf, for the
	 * fault handler to behave properly [except for badaddr() faults,
	 * which can be taken care of without a valid curcpu()].
	 */
	proc0.p_addr = proc0paddr;
	curproc = &proc0;
	curpcb = &proc0paddr->u_pcb;

	avail_start = first_addr;
	avail_end = last_addr;

#ifdef DEBUG
	printf("LUNA-88K boot: memory from 0x%lx to 0x%lx\n",
	    avail_start, avail_end);
#endif

	/*
	 * Tell the VM system about available physical memory.
	 * luna88k only has one segment.
	 */
	uvm_page_physload(atop(avail_start), atop(avail_end),
	    atop(avail_start), atop(avail_end), 0);

	/*
	 * Initialize message buffer.
	 */
	initmsgbuf((caddr_t)pmap_steal_memory(MSGBUFSIZE, NULL, NULL),
	    MSGBUFSIZE);

	pmap_bootstrap(0, 0x20000);	/* ROM needs 128KB */

	/* Initialize the "u-area" pages. */
	bzero((caddr_t)curpcb, USPACE);

#ifndef MULTIPROCESSOR
	/* Release the cpu_hatch_mutex */
	cpu_setup_secondary_processors();
#endif

#ifdef DEBUG
	printf("leaving luna88k_bootstrap()\n");
#endif
}

/* powerdown */

struct pio {
	volatile u_int8_t portA;
	volatile unsigned : 24;
	volatile u_int8_t portB;
	volatile unsigned : 24;
	volatile u_int8_t portC;
	volatile unsigned : 24;
	volatile u_int8_t cntrl;
	volatile unsigned : 24;
};

#define	PIO1_POWER	0x04

#define	PIO1_ENABLE	0x01
#define	PIO1_DISABLE	0x00

void
powerdown(void) 
{
	struct pio *p1 = (struct pio *)OBIO_PIO1_BASE;

	DELAY(100000);
	p1->cntrl = (PIO1_POWER << 1) | PIO1_DISABLE;
	*(volatile u_int8_t *)&p1->portC;
}

/* Get data from FUSE ROM */

void
get_fuse_rom_data(void)
{
	int i;
	struct fuse_rom_byte *p = (struct fuse_rom_byte *)FUSE_ROM_ADDR;

	for (i = 0; i < FUSE_ROM_BYTES; i++) {
		fuse_rom_data[i] =
		    (char)((((p->h) >> 24) & 0x000000f0) |
			   (((p->l) >> 28) & 0x0000000f));
		p++;
	}
}

/* Get data from NVRAM */

void
get_nvram_data(void)
{
	int i, j;
	u_int8_t *page;
	char buf[NVSYMLEN], *data;

	if (machtype == LUNA_88K) {
		data = (char *)(NVRAM_ADDR + 0x80);

		for (i = 0; i < NNVSYM; i++) {
			for (j = 0; j < NVSYMLEN; j++) {
				buf[j] = *data;
				data += 4;
			}
			strlcpy(nvram[i].symbol, buf, sizeof(nvram[i].symbol));

			for (j = 0; j < NVVALLEN; j++) {
				buf[j] = *data;
				data += 4;
			}
			strlcpy(nvram[i].value, buf, sizeof(nvram[i].value));
		}
	} else if (machtype == LUNA_88K2) {
		page = (u_int8_t *)(NVRAM_ADDR_88K2 + 0x20);

		for (i = 0; i < NNVSYM; i++) {
			*page = (u_int8_t)i;

			data = (char *)NVRAM_ADDR_88K2;
			strlcpy(nvram[i].symbol, data, sizeof(nvram[i].symbol));

			data = (char *)(NVRAM_ADDR_88K2 + 0x10);
			strlcpy(nvram[i].value, data, sizeof(nvram[i].value));
		}
	}
}

char *
nvram_by_symbol(char *symbol)
{
	char *value;
	int i;

	value = NULL;

	for (i = 0; i < NNVSYM; i++) {
		if (strncmp(nvram[i].symbol, symbol, NVSYMLEN) == 0) {
			value = nvram[i].value;
			break;
		}
	}

	return value;
}

void
setlevel(u_int level)
{
	u_int32_t set_value;
	struct cpu_info *ci = curcpu();

	set_value = int_set_val[level];

#ifdef MULTIPROCESSOR
	if (!CPU_IS_PRIMARY(ci))
		set_value &= INT_SLAVE_MASK;
#endif

	ci->ci_curspl = level;
	*(volatile uint32_t *)ci->ci_intr_mask = set_value;
	/*
	 * We do not flush the pipeline here, because we are invoked
	 * with interrupts disabled, and the caller will synchronize
	 * the pipeline when restoring the psr.
	 */
}

int
getipl(void)
{
	return (int)curcpu()->ci_curspl;
}

int
setipl(int level)
{
	int curspl;
	uint32_t psr;
	struct cpu_info *ci = curcpu();

	psr = get_psr();
	set_psr(psr | PSR_IND);

	curspl = (int)ci->ci_curspl;
	setlevel((u_int)level);

	set_psr(psr);
	return curspl;
}

int
splraise(int level)
{
	int curspl;
	uint32_t psr;
	struct cpu_info *ci = curcpu();

	psr = get_psr();
	set_psr(psr | PSR_IND);

	curspl = (int)ci->ci_curspl;
	if (curspl < (u_int)level)
		setlevel((u_int)level);

	set_psr(psr);
	return curspl;
}

#ifdef MULTIPROCESSOR
void
m88k_send_ipi(int ipi, cpuid_t cpu)
{
	struct cpu_info *ci = &m88k_cpus[cpu];

	if (ci->ci_ipi & ipi)
		return;

	atomic_setbits_int(&ci->ci_ipi, ipi);
	*(volatile uint32_t *)ci->ci_swireg = ~0;
}

/*
 * Process inter-processor interrupts.
 */

/*
 * Unmaskable IPIs - those are processed with interrupts disabled,
 * and no lock held.
 */
void
luna88k_ipi_handler(struct trapframe *eframe)
{
	struct cpu_info *ci = curcpu();
	int ipi = ci->ci_ipi & (CI_IPI_DDB | CI_IPI_NOTIFY);

	/* just read; reset software interrupt */
	*(volatile uint32_t *)ci->ci_swireg;
	atomic_clearbits_int(&ci->ci_ipi, ipi);

	if (ipi & CI_IPI_DDB) {
#ifdef DDB
		/*
		 * Another processor has entered DDB. Spin on the ddb lock
		 * until it is done.
		 */
		extern struct __mp_lock ddb_mp_lock;

		__mp_lock(&ddb_mp_lock);
		__mp_unlock(&ddb_mp_lock);

		/*
		 * If ddb is hoping to us, it's our turn to enter ddb now.
		 */
		if (ci->ci_cpuid == ddb_mp_nextcpu)
			db_enter();
#endif
	}
	if (ipi & CI_IPI_NOTIFY) {
		/* nothing to do */
	}
}

void
m88k_broadcast_ipi(int ipi)
{
	struct cpu_info *us = curcpu();
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == us)
			continue;

		if (ISSET(ci->ci_flags, CIF_ALIVE))
			m88k_send_ipi(ipi, ci->ci_cpuid);
	}
}
#endif

unsigned int
cpu_rnd_messybits(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (ts.tv_nsec ^ (ts.tv_sec << 20));
}
