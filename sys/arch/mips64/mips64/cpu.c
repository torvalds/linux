/*	$OpenBSD: cpu.c,v 1.85 2025/06/05 09:29:54 claudio Exp $ */

/*
 * Copyright (c) 1997-2004 Opsycon AB (www.opsycon.se)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/autoconf.h>

int	cpumatch(struct device *, void *, void *);
void	cpuattach(struct device *, struct device *, void *);

struct cpu_info cpu_info_primary = { .ci_flags = CPUF_PRIMARY };
struct cpu_info *cpu_info_list = &cpu_info_primary;
struct cpu_info *cpu_info_secondaries;

extern void cpu_idle_cycle_nop(void);
extern void cpu_idle_cycle_wait(void);
void (*cpu_idle_cycle_func)(void) = cpu_idle_cycle_nop;

vaddr_t	cache_valias_mask;
int	cpu_has_synced_cp0_count;
int	cpu_has_userlocal;

const struct cfattach cpu_ca = {
	sizeof(struct device), cpumatch, cpuattach
};
struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL,
};

int
cpumatch(struct device *parent, void *match, void *aux)
{
	struct cpu_attach_args *caa = aux;

	/* make sure that we're looking for a CPU. */
	if (strcmp(caa->caa_maa.maa_name, cpu_cd.cd_name) != 0)
		return 0;

	return 20;	/* Make CPU probe first */
}

void
cpuattach(struct device *parent, struct device *dev, void *aux)
{
	struct cpu_attach_args *caa = aux;
	struct cpu_hwinfo *ch = caa->caa_hw;
	struct cpu_info *ci;
	int cpuno = dev->dv_unit;
	int fptype, vers_maj, vers_min;
	int displayver;

#ifdef MULTIPROCESSOR
	if (cpuno == 0) {
		ci = &cpu_info_primary;
		ci->ci_flags |= CPUF_RUNNING | CPUF_PRESENT | CPUF_PRIMARY;
		if (ncpusfound > 1) {
			cpu_info_secondaries = (struct cpu_info *)
			    alloc_contiguous_pages(sizeof(struct cpu_info) *
			      (ncpusfound - 1));
			if (cpu_info_secondaries == NULL)
				panic("unable to allocate cpu_info");
		}
	} else {
		ci = &cpu_info_secondaries[cpuno - 1];
		ci->ci_next = cpu_info_list->ci_next;
		cpu_info_list->ci_next = ci;
		ci->ci_flags |= CPUF_PRESENT;
	}
#else
	ci = &cpu_info_primary;
#endif
	ci->ci_self = ci;
	ci->ci_cpuid = cpuno;
	ci->ci_dev = dev;
	bcopy(ch, &ci->ci_hw, sizeof(struct cpu_hwinfo));
#ifdef MULTIPROCESSOR
	/*
	 * When attaching secondary processors, cache information is not
	 * available yet.  Copy the cache information from the primary cpu
	 * instead.
	 * XXX The MP boot sequence needs to be reworked to avoid this.
	 */
	if (!CPU_IS_PRIMARY(ci)) {
		ci->ci_l1inst = cpu_info_primary.ci_l1inst;
		ci->ci_l1data = cpu_info_primary.ci_l1data;
		ci->ci_l2 = cpu_info_primary.ci_l2;
		ci->ci_l3 = cpu_info_primary.ci_l3;
	}
#endif

	printf(": ");

	displayver = 1;
	fptype = (ch->c1prid >> 8) & 0xff;
	vers_maj = (ch->c0prid >> 4) & 0x0f;
	vers_min = ch->c0prid & 0x0f;
	switch (ch->type) {
	case MIPS_LOONGSON2:
		switch (ch->c0prid & 0xff) {
		case 0x00:
		case 0x02:
		case 0x03:
			printf("STC Loongson2%c CPU", 'C' + vers_min);
			break;
		case 0x05:
			printf("STC Loongson3%c CPU", 'A' + vers_min - 5);
			break;
		case 0x08:
			printf("STC Loongson3A2000/3B2000 CPU");
			break;
		default:
			printf("Unknown STC Loongson CPU type (%02x)",
			    ch->c0prid & 0xff);
			break;
		}
		displayver = 0;
		break;
	case MIPS_CN50XX:
		printf("CN50xx CPU");
		fptype = MIPS_SOFT;
		break;
	case MIPS_CN61XX:
		if (ci->ci_l2.size < 1024 * 1024)
			printf("CN60xx CPU");
		else
			printf("CN61xx CPU");
		fptype = MIPS_SOFT;
		break;
	case MIPS_CN63XX:
		printf("CN62xx/CN63xx CPU");
		fptype = MIPS_SOFT;
		break;
	case MIPS_CN66XX:
		printf("CN66xx CPU");
		fptype = MIPS_SOFT;
		break;
	case MIPS_CN68XX:
		printf("CN68xx CPU");
		fptype = MIPS_SOFT;
		break;
	case MIPS_CN71XX:
		printf("CN70xx/CN71xx CPU");
		break;
	case MIPS_CN73XX:
		printf("CN72xx/CN73xx CPU");
		break;
	case MIPS_CN78XX:
		printf("CN76xx/CN77xx/CN78xx CPU");
		break;
	default:
		printf("Unknown CPU type (0x%x)", ch->type);
		break;
	}
	if (displayver != 0)
		printf(" rev %d.%d", vers_maj, vers_min);
	printf(" %d MHz, ", ch->clock / 1000000);

	displayver = 1;
	vers_maj = (ch->c1prid >> 4) & 0x0f;
	vers_min = ch->c1prid & 0x0f;
	switch (fptype) {
	case MIPS_SOFT:
#ifdef FPUEMUL
		printf("Software FP emulation");
#else
		printf("no FPU");
#endif
		displayver = 0;
		break;
	case MIPS_LOONGSON2:
		switch (ch->c1prid & 0xff) {
		case 0x00:
		case 0x02:
		case 0x03:
			printf("STC Loongson2%c FPU", 'C' + vers_min);
			break;
		case 0x05:
			printf("STC Loongson3%c FPU", 'A' + vers_min - 5);
			break;
		case 0x08:
			printf("STC Loongson3A2000/3B2000 FPU");
			break;
		default:
			printf("Unknown STC Loongson FPU type (%02x)",
			    ch->c1prid & 0xff);
			break;
		}
		displayver = 0;
		break;
	case MIPS_CN71XX:
		printf("CN70xx/CN71xx FPU");
		break;
	case MIPS_CN73XX:
		printf("CN72xx/CN73xx FPU");
		break;
	case MIPS_CN78XX:
		printf("CN76xx/CN77xx/CN78xx FPU");
		break;
	default:
		printf("Unknown FPU type (0x%x)", fptype);
		break;
	}
	if (displayver != 0)
		printf(" rev %d.%d", vers_maj, vers_min);
	printf("\n");

	if (ci->ci_l1inst.sets == ci->ci_l1data.sets) {
		printf("cpu%d: cache L1-I %dKB D %dKB ", cpuno,
		    ci->ci_l1inst.size / 1024, ci->ci_l1data.size / 1024);
		if (ci->ci_l1inst.sets == 1)
			printf("direct");
		else
			printf("%d way", ci->ci_l1inst.sets);
	} else {
		printf("cpu%d: cache L1-I %dKB ", cpuno,
		    ci->ci_l1inst.size / 1024);
		if (ci->ci_l1inst.sets == 1)
			printf("direct");
		else
			printf("%d way", ci->ci_l1inst.sets);
		printf(" D %dKB ", ci->ci_l1data.size / 1024);
		if (ci->ci_l1data.sets == 1)
			printf("direct");
		else
			printf("%d way", ci->ci_l1data.sets);
	}

	if (ci->ci_l2.size != 0) {
		printf(", L2 %dKB ", ci->ci_l2.size / 1024);
		if (ci->ci_l2.sets == 1)
			printf("direct");
		else
			printf("%d way", ci->ci_l2.sets);
	}
	if (ci->ci_l3.size != 0) {
		printf(", L3 %dKB ", ci->ci_l3.size / 1024);
		if (ci->ci_l3.sets == 1)
			printf("direct");
		else
			printf("%d way", ci->ci_l3.sets);
	}

	if (cpuno == 0) {
		switch (ch->type) {
		case MIPS_CN50XX:
		case MIPS_CN61XX:
		case MIPS_CN71XX:
		case MIPS_CN73XX:
			cpu_idle_cycle_func = cpu_idle_cycle_wait;
			break;
		}
	}

	printf("\n");

#ifdef DEBUG
	printf("cpu%d: L1 set size %d:%d\n", cpuno,
	    ci->ci_l1inst.setsize, ci->ci_l1data.setsize);
	printf("cpu%d: L1 line size %d:%d\n", cpuno,
	    ci->ci_l1inst.linesize, ci->ci_l1data.linesize);
	printf("cpu%d: L2 line size %d\n", cpuno, ci->ci_l2.linesize);
	printf("cpu%d: cache configuration %x\n",
	    cpuno, ci->ci_cacheconfiguration);
	printf("cpu%d: virtual alias mask 0x%lx\n", cpuno, cache_valias_mask);
	printf("cpu%d: config register %016lx, status register %016lx\n",
	    cpuno, cp0_get_config(), getsr());
#endif
}

void
cpu_switchto(struct proc *oldproc, struct proc *newproc)
{
#ifdef MULTIPROCESSOR
	struct cpu_info *ci = curcpu();
	if (ci->ci_fpuproc)
		save_fpu();
#endif

	cpu_switchto_asm(oldproc, newproc);
}

void
enable_fpu(struct proc *p)
{
	struct cpu_info *ci = curcpu();

	if (!CPU_HAS_FPU(ci))
		return;

	if (p->p_md.md_regs->sr & SR_FR_32)
		MipsSwitchFPState(ci->ci_fpuproc, p->p_md.md_regs);
	else
		MipsSwitchFPState16(ci->ci_fpuproc, p->p_md.md_regs);
	atomic_inc_int(&uvmexp.fpswtch);

	ci->ci_fpuproc = p;
	p->p_md.md_regs->sr |= SR_COP_1_BIT;
	p->p_md.md_flags |= MDP_FPUSED;
}

void
save_fpu(void)
{
	struct cpu_info *ci = curcpu();
	struct proc *p;

	if (!CPU_HAS_FPU(ci))
		return;

	KASSERT(ci->ci_fpuproc);
	p = ci->ci_fpuproc;
	if (p->p_md.md_regs->sr & SR_FR_32)
		MipsSaveCurFPState(p);
	else
		MipsSaveCurFPState16(p);
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	if (ci->ci_curproc != NULL) {
		/*
		 * Ensure that preceding stores are visible to other CPUs
		 * before setting the AST flag.
		 */
		membar_producer();

		aston(ci->ci_curproc);
		cpu_unidle(ci);
	}
}

#ifdef MULTIPROCESSOR
struct cpu_info *
get_cpu_info(int cpuno)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci->ci_cpuid == cpuno)
			return ci;
	}
	return NULL;
}

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	mips64_ipi_init();

	CPU_INFO_FOREACH(cii, ci) {
		if ((ci->ci_flags & CPUF_PRESENT) == 0)
			continue;
		if (CPU_IS_PRIMARY(ci))
			continue;

		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);
		cpu_boot_secondary(ci);
	}
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		mips64_send_ipi(ci->ci_cpuid, MIPS64_IPI_NOP);
}

vaddr_t
alloc_contiguous_pages(size_t sz)
{
	const struct kmem_pa_mode kp_contig = {
		.kp_constraint = &no_constraint,
		.kp_maxseg = 1,
		.kp_zero = 1
	};
	paddr_t pa;

	pa = (paddr_t)km_alloc(round_page(sz), &kv_any, &kp_contig, &kd_nowait);
	if (pa == 0)
		return 0;

	return PHYS_TO_XKPHYS(pa, CCA_CACHED);
}
#endif
