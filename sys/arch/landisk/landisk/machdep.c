/*	$OpenBSD: machdep.c,v 1.48 2020/05/31 06:23:57 dlg Exp $	*/
/*	$NetBSD: machdep.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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

#include "ksyms.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/exec.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <net/if.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <sh/bscreg.h>
#include <sh/cpgreg.h>
#include <sh/trap.h>

#include <sh/cache.h>
#include <sh/cache_sh4.h>
#include <sh/mmu_sh4.h>

#include <machine/cpu.h>
#include <machine/kcore.h>
#include <machine/pcb.h>

#include <landisk/landisk/landiskreg.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#endif

/* the following is used externally (sysctl_hw) */
char machine[] = MACHINE;		/* landisk */

__dead void landisk_startup(int, char *);
__dead void main(void);
void	cpu_init_kcore_hdr(void);
void	blink_led(void *);

int	led_blink;

extern u_int32_t getramsize(void);

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

void
cpu_startup(void)
{
	extern char cpu_model[120];

	strlcpy(cpu_model, "SH4 SH7751R", sizeof cpu_model);

        sh_startup();
}

vaddr_t kernend;	/* used by /dev/mem too */
char *esym;

__dead void
landisk_startup(int howto, char *_esym)
{
	u_int32_t ramsize;

	/* Start to determine heap area */
	esym = _esym;
	kernend = (vaddr_t)round_page((vaddr_t)esym);

	boothowto = howto;

	ramsize = getramsize();

	/* Initialize CPU ops. */
	sh_cpu_init(CPU_ARCH_SH4, CPU_PRODUCT_7751R);	

	/* Initialize early console */
	consinit();

	/* Load memory to UVM */
	if (ramsize == 0 || ramsize > 512 * 1024 * 1024)
		ramsize = IOM_RAM_SIZE;
	physmem = atop(ramsize);
	kernend = atop(round_page(SH3_P1SEG_TO_PHYS(kernend)));
	uvm_page_physload(atop(IOM_RAM_BEGIN),
	    atop(IOM_RAM_BEGIN + ramsize), kernend,
	    atop(IOM_RAM_BEGIN + ramsize), 0);
	cpu_init_kcore_hdr();	/* need to be done before pmap_bootstrap */

	/* Initialize proc0 u-area */
	sh_proc0_init();

	/* Initialize pmap and start to address translation */
	pmap_bootstrap();

#if defined(DDB)
	db_machine_init();
	ddb_init();
	if (boothowto & RB_KDB) {
		db_enter();
	}
#endif

	/* Jump to main */
	__asm volatile(
		"jmp	@%0\n\t"
		" mov	%1, sp"
		:: "r" (main), "r" (proc0.p_md.md_pcb->pcb_sf.sf_r7_bank));
	for (;;)
		continue;
	/* NOTREACHED */
}

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

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

	if ((howto & RB_POWERDOWN) != 0) {
		_reg_write_1(LANDISK_PWRMNG, PWRMNG_POWEROFF);
		delay(1 * 1000 * 1000);
		printf("POWEROFF FAILED!\n");
		howto |= RB_HALT;
	}

	if ((howto & RB_HALT) != 0) {
		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cnpollc(1);
		cngetc();
		cnpollc(0);
	}

doreset:
	printf("rebooting...\n");
	machine_reset();

	for (;;)
		continue;
	/* NOTREACHED */
}

void
machine_reset(void)
{
	_cpu_exception_suspend();
	_reg_write_4(SH_(EXPEVT), EXPEVT_RESET_MANUAL);
	(void)*(volatile uint32_t *)0x80000001;	/* CPU shutdown */

	/*NOTREACHED*/
	for (;;) {
		continue;
	}
}

#if !defined(DONT_INIT_BSC)
/*
 * InitializeBsc
 * : BSC(Bus State Controller)
 */
void InitializeBsc(void);

void
InitializeBsc(void)
{

	/*
	 * Drive RAS,CAS in stand by mode and bus release mode
	 * Area0 = Normal memory, Area5,6=Normal(no burst)
	 * Area2 = Normal memory, Area3 = SDRAM, Area5 = Normal memory
	 * Area4 = Normal Memory
	 * Area6 = Normal memory
	 */
	_reg_write_4(SH4_BCR1, BSC_BCR1_VAL);

	/*
	 * Bus Width
	 * Area4: Bus width = 16bit
	 * Area6,5 = 16bit
	 * Area1 = 8bit
	 * Area2,3: Bus width = 32bit
	 */
	_reg_write_2(SH4_BCR2, BSC_BCR2_VAL);

#if defined(SH4) && defined(SH7751R)
	if (cpu_product == CPU_PRODUCT_7751R) {
#ifdef BSC_BCR3_VAL
		_reg_write_2(SH4_BCR3, BSC_BCR3_VAL);
#endif
#ifdef BSC_BCR4_VAL
		_reg_write_4(SH4_BCR4, BSC_BCR4_VAL);
#endif
	}
#endif	/* SH4 && SH7751R */

	/*
	 * Idle cycle number in transition area and read to write
	 * Area6 = 3, Area5 = 3, Area4 = 3, Area3 = 3, Area2 = 3
	 * Area1 = 3, Area0 = 3
	 */
	_reg_write_4(SH4_WCR1, BSC_WCR1_VAL);

	/*
	 * Wait cycle
	 * Area 6 = 6
	 * Area 5 = 2
	 * Area 4 = 10
	 * Area 3 = 3
	 * Area 2,1 = 3
	 * Area 0 = 6
	 */
	_reg_write_4(SH4_WCR2, BSC_WCR2_VAL);

#ifdef BSC_WCR3_VAL
	_reg_write_4(SH4_WCR3, BSC_WCR3_VAL);
#endif

	/*
	 * RAS pre-charge = 2cycle, RAS-CAS delay = 3 cycle,
	 * write pre-charge=1cycle
	 * CAS before RAS refresh RAS assert time = 3 cycle
	 * Disable burst, Bus size=32bit, Column Address=10bit, Refresh ON
	 * CAS before RAS refresh ON, EDO DRAM
	 */
	_reg_write_4(SH4_MCR, BSC_MCR_VAL);

#ifdef BSC_SDMR2_VAL
	_reg_write_1(BSC_SDMR2_VAL, 0);
#endif

#ifdef BSC_SDMR3_VAL
	_reg_write_1(BSC_SDMR3_VAL, 0);
#endif /* BSC_SDMR3_VAL */

	/*
	 * PCMCIA Control Register
	 * OE/WE assert delay 3.5 cycle
	 * OE/WE negate-address delay 3.5 cycle
	 */
#ifdef BSC_PCR_VAL
	_reg_write_2(SH4_PCR, BSC_PCR_VAL);
#endif

	/*
	 * Refresh Timer Control/Status Register
	 * Disable interrupt by CMF, closk 1/16, Disable OVF interrupt
	 * Count Limit = 1024
	 * In following statement, the reason why high byte = 0xa5(a4 in RFCR)
	 * is the rule of SH3 in writing these register.
	 */
	_reg_write_2(SH4_RTCSR, BSC_RTCSR_VAL);

	/*
	 * Refresh Timer Counter
	 * Initialize to 0
	 */
#ifdef BSC_RTCNT_VAL
	_reg_write_2(SH4_RTCNT, BSC_RTCNT_VAL);
#endif

	/* set Refresh Time Constant Register */
	_reg_write_2(SH4_RTCOR, BSC_RTCOR_VAL);

	/* init Refresh Count Register */
#ifdef BSC_RFCR_VAL
	_reg_write_2(SH4_RFCR, BSC_RFCR_VAL);
#endif

	/*
	 * Clock Pulse Generator
	 */
	/* Set Clock mode (make internal clock double speed) */
	_reg_write_2(SH4_FRQCR, FRQCR_VAL);
}
#endif /* !DONT_INIT_BSC */

/*
 * Dump the machine-dependent dump header.
 */
u_int
cpu_dump(int (*dump)(dev_t, daddr_t, caddr_t, size_t), daddr_t *blknop)
{
	extern cpu_kcore_hdr_t cpu_kcore_hdr;
	char buf[dbtob(1)];
	cpu_kcore_hdr_t *h;
	kcore_seg_t *kseg;
	int rc;

#ifdef DIAGNOSTIC
	if (cpu_dumpsize() > btodb(sizeof buf)) {
		printf("buffer too small in cpu_dump, ");
		return (EINVAL);	/* "aborted" */
	}
#endif

	bzero(buf, sizeof buf);
	kseg = (kcore_seg_t *)buf;
	h = (cpu_kcore_hdr_t *)(buf + ALIGN(sizeof(kcore_seg_t)));

	/* Create the segment header */
	CORE_SETMAGIC(*kseg, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg->c_size = dbtob(1) - ALIGN(sizeof(kcore_seg_t));

	bcopy(&cpu_kcore_hdr, h, sizeof(*h));
	/* We can now fill kptp in the header... */
	h->kcore_kptp = SH3_P1SEG_TO_PHYS((vaddr_t)pmap_kernel()->pm_ptp);

	rc = (*dump)(dumpdev, *blknop, buf, sizeof buf);
	*blknop += btodb(sizeof buf);
	return (rc);
}

/*
 * Return the size of the machine-dependent dump header, in disk blocks.
 */
u_int
cpu_dumpsize(void)
{
	u_int size;

	size = ALIGN(sizeof(kcore_seg_t)) + ALIGN(sizeof(cpu_kcore_hdr_t));
	return (btodb(roundup(size, dbtob(1))));
}

/*
 * Fill the machine-dependent dump header.
 */
void
cpu_init_kcore_hdr(void)
{
	extern cpu_kcore_hdr_t cpu_kcore_hdr;
	cpu_kcore_hdr_t *h = &cpu_kcore_hdr;
	phys_ram_seg_t *seg = cpu_kcore_hdr.kcore_segs;
	struct vm_physseg *physseg = vm_physmem;
	u_int i;

	bzero(h, sizeof(*h));

	h->kcore_nsegs = min(NPHYS_RAM_SEGS, (u_int)vm_nphysseg);
	for (i = h->kcore_nsegs; i != 0; i--) {
		seg->start = ptoa(physseg->start);
		seg->size = (psize_t)ptoa(physseg->end - physseg->start);
		seg++;
		physseg++;
	}
}

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	int oldval, ret;

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

	case CPU_LED_BLINK:
		oldval = led_blink;
		ret = sysctl_int(oldp, oldlenp, newp, newlen, &led_blink);
		if (oldval != led_blink)
			blink_led(NULL);
		return (ret);

	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

void
blink_led(void *whatever)
{
	static struct timeout blink_tmo;
	u_int8_t ledctrl;

	if (led_blink == 0) {
		_reg_write_1(LANDISK_LEDCTRL,
		    LED_POWER_CHANGE | LED_POWER_VALUE);
		return;
	}

	ledctrl = (u_int8_t)_reg_read_1(LANDISK_LEDCTRL) & LED_POWER_VALUE;
	ledctrl ^= (LED_POWER_CHANGE | LED_POWER_VALUE);
	_reg_write_1(LANDISK_LEDCTRL, ledctrl);

	timeout_set(&blink_tmo, blink_led, NULL);
	timeout_add(&blink_tmo,
	    ((averunnable.ldavg[0] + FSCALE) * hz) >> FSHIFT);
}

unsigned int
cpu_rnd_messybits(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (ts.tv_nsec ^ (ts.tv_sec << 20));
}
