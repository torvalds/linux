/*	$OpenBSD: cpu.c,v 1.47 2023/10/24 13:20:09 claudio Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/reboot.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/pdc.h>
#include <machine/reg.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

struct cpu_softc {
	struct  device sc_dev;
};

#ifdef MULTIPROCESSOR
struct cpu_info *cpu_hatch_info;
static volatile int start_secondary_cpu;
#endif

int	cpumatch(struct device *, void *, void *);
void	cpuattach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct cpu_softc), cpumatch, cpuattach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

int
cpumatch(struct device *parent, void *cfdata, void *aux)
{
	struct cfdata *cf = cfdata;
	struct confargs *ca = aux;

	/* probe any 1.0, 1.1 or 2.0 */
	if (ca->ca_type.iodc_type != HPPA_TYPE_NPROC ||
	    ca->ca_type.iodc_sv_model != HPPA_NPROC_HPPA)
		return 0;

	if (cf->cf_unit >= MAXCPUS)
		return 0;

	return 1;
}

void
cpuattach(struct device *parent, struct device *self, void *aux)
{
	/* machdep.c */
	extern struct pdc_model pdc_model;
	extern struct pdc_cache pdc_cache;
	extern struct pdc_btlb pdc_btlb;
	extern u_int cpu_ticksnum, cpu_ticksdenom;
	extern u_int fpu_enable;
	/* clock.c */
	extern int itmr_intr(void *);
	/* ipi.c */
	extern int hppa_ipi_intr(void *);

	struct confargs *ca = (struct confargs *)aux;
	struct cpu_info *ci;
	u_int mhz = 100 * cpu_ticksnum / cpu_ticksdenom;
	int cpuno = self->dv_unit;
	struct pglist mlist;
	struct vm_page *m;
	const char *p;
	int error;

	ci = &cpu_info[cpuno];
	ci->ci_dev = self;
	ci->ci_cpuid = cpuno;
	ci->ci_hpa = ca->ca_hpa;

	/* Allocate stack for spin up and FPU emulation. */
	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(PAGE_SIZE, 0, -1L, 0, 0, &mlist, 1,
	    UVM_PLA_NOWAIT);
	if (error) {
		printf(": unable to allocate CPU stack!\n");
		return;
	}
	m = TAILQ_FIRST(&mlist);
	ci->ci_stack = VM_PAGE_TO_PHYS(m);

	printf (": %s ", cpu_typename);
	if (pdc_model.hvers) {
		static const char lvls[4][4] = { "0", "1", "1.5", "2" };

		printf("L%s-%c ", lvls[pdc_model.pa_lvl], "AB"[pdc_model.mc]);
	}

	printf ("%d", mhz / 100);
	if (mhz % 100 > 9)
		printf(".%02d", mhz % 100);
	printf("MHz");

	if (fpu_enable) {
		extern u_int fpu_version;
		u_int32_t ver[2];

		mtctl(fpu_enable, CR_CCR);
		__asm volatile(
		    "fstds   %%fr0,0(%0)\n\t"
		    "copr,0,0\n\t"
		    "fstds   %%fr0,0(%0)"
		    :: "r" (&ver) : "memory");
		mtctl(0, CR_CCR);
		fpu_version = HPPA_FPUVER(ver[0]);
		printf(", FPU %s rev %d",
		    hppa_mod_info(HPPA_TYPE_FPU, fpu_version >> 5),
		    fpu_version & 0x1f);
	}

	printf("\n%s: ", self->dv_xname);
	p = "";
	if (!pdc_cache.dc_conf.cc_sh) {
		printf("%uK(%db/l) Icache, ",
		    pdc_cache.ic_size / 1024, pdc_cache.ic_conf.cc_line * 16);
		p = "D";
	}

	printf("%uK(%db/l) wr-%s %scache, ",
	    pdc_cache.dc_size / 1024, pdc_cache.dc_conf.cc_line * 16,
	    pdc_cache.dc_conf.cc_wt? "thru" : "back", p);

	p = "";
	if (!pdc_cache.dt_conf.tc_sh) {
		printf("%u ITLB, ", pdc_cache.it_size);
		p = "D";
	}
	printf("%u %scoherent %sTLB",
	    pdc_cache.dt_size, pdc_cache.dt_conf.tc_cst? "" : "in", p);

	if (pdc_btlb.finfo.num_c)
		printf(", %u BTLB", pdc_btlb.finfo.num_c);
	else if (pdc_btlb.finfo.num_i || pdc_btlb.finfo.num_d)
		printf(", %u/%u D/I BTLBs",
		    pdc_btlb.finfo.num_i, pdc_btlb.finfo.num_d);

	cpu_intr_establish(IPL_CLOCK, 31, itmr_intr, NULL, "clock");
#ifdef MULTIPROCESSOR
	cpu_intr_establish(IPL_IPI, 30, hppa_ipi_intr, NULL, "ipi");
#endif

	printf("\n");
}

#ifdef MULTIPROCESSOR
void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	struct iomod *cpu;
	int i, j;

	/* Initialise primary CPU. */
	ci = curcpu();
	ci->ci_flags |= CPUF_RUNNING;
	hppa_ipi_init(ci);

	for (i = 0; i < HPPA_MAXCPUS; i++) {

		ci = &cpu_info[i];
		if (ci->ci_cpuid == 0)
			continue;

		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;

		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);

		/* Release the specified CPU by triggering an EIR{0}. */
		cpu_hatch_info = ci;
		cpu = (struct iomod *)(ci->ci_hpa);
		cpu->io_eir = 0;
		asm volatile ("sync" ::: "memory");

		/* Wait for CPU to wake up... */
		j = 0;
		while (!(ci->ci_flags & CPUF_RUNNING) && j++ < 10000)
			delay(1000);
		if (!(ci->ci_flags & CPUF_RUNNING))
			printf("failed to hatch cpu %i!\n", ci->ci_cpuid);
	}

	/* Release secondary CPUs. */
	start_secondary_cpu = 1;
	asm volatile ("sync" ::: "memory");
}

void
cpu_hw_init(void)
{
	struct cpu_info *ci = curcpu();

	/* Purge TLB and flush caches. */
	ptlball();
	ficacheall();
	fdcacheall();

	/* Enable address translations. */
	ci->ci_psw = PSL_I | PSL_Q | PSL_P | PSL_C | PSL_D;
	ci->ci_psw |= (cpu_info[0].ci_psw & PSL_O);
}

void
cpu_hatch(void)
{
	struct cpu_info *ci = curcpu();

	/* Initialise IPIs. */
	hppa_ipi_init(ci);

	/* Initialise clock. */
	mtctl((1U << 31), CR_EIRR);
	ci->ci_mask |= (1U << 31);
	cpu_startclock();

	/* Enable interrupts. */
	mtctl(ci->ci_mask, CR_EIEM);

	ncpus++;
	ci->ci_flags |= CPUF_RUNNING;

	/* Wait for additional CPUs to spinup. */
	while (!start_secondary_cpu)
		;

	sched_toidle();
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		hppa_ipi_send(ci, HPPA_IPI_NOP);
}
#endif

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		setsoftast(ci->ci_curproc);
		cpu_unidle(ci);
	}
}
