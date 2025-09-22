/* $OpenBSD: cpu.c,v 1.50 2025/06/05 09:29:54 claudio Exp $ */
/* $NetBSD: cpu.c,v 1.44 2000/05/23 05:12:53 thorpej Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
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
#include <sys/clockintr.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/atomic.h>
#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/rpb.h>
#include <machine/prom.h>

struct cpu_info cpu_info_primary = { .ci_flags = CPUF_PRIMARY };
struct cpu_info *cpu_info_list = &cpu_info_primary;

#if defined(MULTIPROCESSOR)
#include <sys/malloc.h>

/*
 * Array of CPU info structures.  Must be statically-allocated because
 * curproc, etc. are used early.
 */
struct cpu_info *cpu_info[ALPHA_MAXPROCS];

/* Bitmask of CPUs booted, currently running, and paused. */
volatile u_long cpus_booted;
volatile u_long cpus_running;
volatile u_long cpus_paused;

void	cpu_boot_secondary(struct cpu_info *);
#endif /* MULTIPROCESSOR */

/*
 * The Implementation Version and the Architecture Mask must be
 * consistent across all CPUs in the system, so we set it for the
 * primary and announce the AMASK extensions if they exist.
 *
 * Note, we invert the AMASK so that if a bit is set, it means "has
 * extension".
 */
u_long	cpu_implver, cpu_amask;

/* Definition of the driver for autoconfig. */
int	cpumatch(struct device *, void *, void *);
void	cpuattach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpumatch, cpuattach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

void	cpu_announce_extensions(struct cpu_info *);

static const char *ev4minor[] = {
	"pass 2 or 2.1", "pass 3", 0
}, *lcaminor[] = {
	"",
	"21066 pass 1 or 1.1", "21066 pass 2",
	"21068 pass 1 or 1.1", "21068 pass 2",
	"21066A pass 1", "21068A pass 1", 0
}, *ev5minor[] = {
	"", "pass 2, rev BA or 2.2, rev CA", "pass 2.3, rev DA or EA",
	"pass 3", "pass 3.2", "pass 4", 0
}, *ev45minor[] = {
	"", "pass 1", "pass 1.1", "pass 2", 0
}, *ev56minor[] = {
	"", "pass 1", "pass 2", 0
}, *ev6minor[] = {
	"", "pass 1", "pass 2 or 2.1", "pass 2.2", "pass 2.3", "pass 3",
	"pass 2.4", "pass 2.5", 0
}, *pca56minor[] = {
	"", "pass 1", 0
}, *pca57minor[] = {
	"", "pass 1", 0
}, *ev67minor[] = {
	"", "pass 1", "pass 2.1", "pass 2.2", "pass 2.1.1",
	"pass 2.2.1", "pass 2.3 or 2.4", "pass 2.1.2", "pass 2.2.2",
	"pass 2.2.3 or 2.2.5", "pass 2.2.4", "pass 2.5", "pass 2.4.1",
	"pass 2.5.1", "pass 2.6", 0
}, *ev68cbminor[] = {
	/* what is the value for pass 2.3? */
	"", "", "", "", "",
	"pass 2.4", "pass 4.0", 0
};

struct cputable_struct {
	int	cpu_major_code;
	const char *cpu_major_name;
	const char **cpu_minor_names;
} cpunametable[] = {
	{ PCS_PROC_EV3,		"EV3",		0		},
	{ PCS_PROC_EV4,		"21064",	ev4minor	},
	{ PCS_PROC_SIMULATION,	"Sim",		0		},
	{ PCS_PROC_LCA4,	"LCA",		lcaminor	},
	{ PCS_PROC_EV5,		"21164",	ev5minor	},
	{ PCS_PROC_EV45,	"21064A",	ev45minor	},
	{ PCS_PROC_EV56,	"21164A",	ev56minor	},
	{ PCS_PROC_EV6,		"21264",	ev6minor	},
	{ PCS_PROC_PCA56,	"PCA56",	pca56minor	},
	{ PCS_PROC_PCA57,	"PCA57",	pca57minor	},
	{ PCS_PROC_EV67,	"21264A",	ev67minor	},
	{ PCS_PROC_EV68CB,	"21264C",	ev68cbminor	},
	{ PCS_PROC_EV68AL,	"21264B",	NULL		},
	{ PCS_PROC_EV68CX,	"21264D",	NULL		},
};

/*
 * The following is an attempt to map out how booting secondary CPUs
 * works.
 *
 * As we find processors during the autoconfiguration sequence, all
 * processors have idle stacks and PCBs created for them, including
 * the primary (although the primary idles on proc0's PCB until its
 * idle PCB is created).
 *
 * As one of the last steps in booting, main() calls, on proc0's
 * context, cpu_boot_secondary_processors().  This is our key to
 * actually spin up the additional processors we've found.  We
 * run through our cpu_info[] array looking for secondary processors
 * with idle PCBs, and spin them up.
 *
 * The spinup involves switching the secondary processor to the
 * OSF/1 PALcode, setting the entry point to cpu_spinup_trampoline(),
 * and sending a "START" message to the secondary's console.
 *
 * Upon successful processor bootup, the cpu_spinup_trampoline will call
 * cpu_hatch(), which will print a message indicating that the processor
 * is running, and will set the "hatched" flag in its softc.  At the end
 * of cpu_hatch() is a spin-forever loop; we do not yet attempt to schedule
 * anything on secondary CPUs.
 */

int
cpumatch(struct device *parent, void *cfdata, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* make sure that we're looking for a CPU. */
	if (strcmp(ma->ma_name, cpu_cd.cd_name) != 0)
		return (0);

#ifndef MULTIPROCESSOR
	/* Only attach the boot processor. */
	if (ma->ma_slot != hwrpb->rpb_primary_cpu_id)
		return (0);
#endif

	return (1);
}

void
cpuattach(struct device *parent, struct device *dev, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	struct cpu_info *ci;
	int i;
	const char **s;
	struct pcs *p;
#ifdef DEBUG
	int needcomma;
#endif
	u_int32_t major, minor;
#if defined(MULTIPROCESSOR)
	vaddr_t pcbva;
	struct pcb *pcb;
	const struct kmem_pa_mode kp_contig = {
		.kp_constraint = &no_constraint,
		.kp_maxseg = 1,
		.kp_zero = 1
	};
#endif

	p = LOCATE_PCS(hwrpb, ma->ma_slot);
	major = PCS_CPU_MAJORTYPE(p);
	minor = PCS_CPU_MINORTYPE(p);

	printf(": ID %d%s, ", ma->ma_slot,
	    ma->ma_slot == hwrpb->rpb_primary_cpu_id ? " (primary)" : "");

	for (i = 0; i < sizeof cpunametable / sizeof cpunametable[0]; ++i) {
		if (cpunametable[i].cpu_major_code == major) {
			printf("%s-%d", cpunametable[i].cpu_major_name, minor);
			s = cpunametable[i].cpu_minor_names;
			for (i = 0; s && s[i]; ++i) {
				if (i == minor && strlen(s[i]) != 0) {
					printf(" (%s)", s[i]);
					goto recognized;
				}
			}
			printf(" (unknown minor type %d)", minor);
			goto recognized;
		}
	}
	printf("UNKNOWN CPU TYPE (%d:%d)", major, minor);

recognized:
	printf("\n");

#ifdef DEBUG
	if (p->pcs_proc_var != 0) {
		printf("%s: ", dev->dv_xname);

		needcomma = 0;
		if (p->pcs_proc_var & PCS_VAR_VAXFP) {
			printf("VAX FP support");
			needcomma = 1;
		}
		if (p->pcs_proc_var & PCS_VAR_IEEEFP) {
			printf("%sIEEE FP support", needcomma ? ", " : "");
			needcomma = 1;
		}
		if (p->pcs_proc_var & PCS_VAR_PE) {
			printf("%sPrimary Eligible", needcomma ? ", " : "");
			needcomma = 1;
		}
		if (p->pcs_proc_var & PCS_VAR_RESERVED)
			printf("%sreserved bits: 0x%lx", needcomma ? ", " : "",
			    p->pcs_proc_var & PCS_VAR_RESERVED);
		printf("\n");
	}
#endif

#if defined(MULTIPROCESSOR)
	if (ma->ma_slot > ALPHA_WHAMI_MAXID) {
		printf("%s: processor ID too large, ignoring\n", dev->dv_xname);
		return;
	}
#endif /* MULTIPROCESSOR */

#if defined(MULTIPROCESSOR)
	if (ma->ma_slot == hwrpb->rpb_primary_cpu_id)
		ci = &cpu_info_primary;
	else
		ci = malloc(sizeof(*ci), M_DEVBUF, M_WAITOK | M_ZERO);

	cpu_info[ma->ma_slot] = ci;
#else
	ci = &cpu_info_primary;
#endif
	ci->ci_cpuid = ma->ma_slot;
	ci->ci_dev = dev;

#if defined(MULTIPROCESSOR)
	/*
	 * Make sure the processor is available for use.
	 */
	if ((p->pcs_flags & PCS_PA) == 0) {
		if (ma->ma_slot == hwrpb->rpb_primary_cpu_id)
			panic("cpu_attach: primary not available?!");
		printf("%s: processor not available for use\n", dev->dv_xname);
		return;
	}

	/* Make sure the processor has valid PALcode. */
	if ((p->pcs_flags & PCS_PV) == 0) {
		if (ma->ma_slot == hwrpb->rpb_primary_cpu_id)
			panic("cpu_attach: primary has invalid PALcode?!");
		printf("%s: PALcode not valid\n", ci->ci_dev->dv_xname);
		return;
	}

	/*
	 * Allocate UPAGES contiguous pages for the idle PCB and stack.
	 */
	pcbva = (vaddr_t)km_alloc(USPACE, &kv_any, &kp_contig, &kd_waitok);
	if (pcbva == 0) {
		if (ma->ma_slot == hwrpb->rpb_primary_cpu_id) {
			panic("cpu_attach: unable to allocate idle stack for"
			    " primary");
		}
		printf("%s: unable to allocate idle stack\n", dev->dv_xname);
		return;
	}

	ci->ci_idle_pcb_paddr = ALPHA_K0SEG_TO_PHYS(pcbva);
	pcb = ci->ci_idle_pcb = (struct pcb *)pcbva;

	/*
	 * Initialize the idle stack pointer, reserving space for an
	 * (empty) trapframe (XXX is the trapframe really necessary?)
	 */
	pcb->pcb_hw.apcb_ksp =
	    (u_int64_t)pcb + USPACE - sizeof(struct trapframe);

	/*
	 * Initialize the idle PCB.
	 */
	pcb->pcb_hw.apcb_asn = proc0.p_addr->u_pcb.pcb_hw.apcb_asn;
	pcb->pcb_hw.apcb_ptbr = proc0.p_addr->u_pcb.pcb_hw.apcb_ptbr;
#if 0
	printf("%s: hwpcb ksp = 0x%lx\n", dev->dv_xname,
	    pcb->pcb_hw.apcb_ksp);
	printf("%s: hwpcb ptbr = 0x%lx\n", dev->dv_xname,
	    pcb->pcb_hw.apcb_ptbr);
#endif
#endif /* MULTIPROCESSOR */

	/*
	 * If we're the primary CPU, no more work to do; we're already
	 * running!
	 */
	if (ma->ma_slot == hwrpb->rpb_primary_cpu_id) {
		cpu_announce_extensions(ci);
#if defined(MULTIPROCESSOR)
		ci->ci_flags |= CPUF_PRIMARY | CPUF_RUNNING;
		atomic_setbits_ulong(&cpus_booted, (1UL << ma->ma_slot));
		atomic_setbits_ulong(&cpus_running, (1UL << ma->ma_slot));
#endif /* MULTIPROCESSOR */
	} else {
#if defined(MULTIPROCESSOR)
		/*
		 * Boot the secondary processor.  It will announce its
		 * extensions, and then spin up until we tell it to go
		 * on its merry way.
		 */
		cpu_boot_secondary(ci);
#endif /* MULTIPROCESSOR */
	}
}

void
cpu_announce_extensions(struct cpu_info *ci)
{
	u_long implver, amask = 0;

	implver = alpha_implver();
	if (implver >= ALPHA_IMPLVER_EV5)
		amask = (~alpha_amask(ALPHA_AMASK_ALL)) & ALPHA_AMASK_ALL;

	if (ci->ci_cpuid == hwrpb->rpb_primary_cpu_id) {
		cpu_implver = implver;
		cpu_amask = amask;
	} else {
		if (implver < cpu_implver)
			printf("%s: WARNING: IMPLVER %lu < %lu\n",
			    ci->ci_dev->dv_xname, implver, cpu_implver);

		/*
		 * Cap the system architecture mask to the intersection
		 * of features supported by all processors in the system.
		 */
		cpu_amask &= amask;
	}

	if (amask) {
		printf("%s: architecture extensions: %lb\n",
		    ci->ci_dev->dv_xname, amask, ALPHA_AMASK_BITS);
	}
}

#if defined(MULTIPROCESSOR)
void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	u_long i;

	for (i = 0; i < ALPHA_MAXPROCS; i++) {
		ci = cpu_info[i];
		if (ci == NULL || ci->ci_idle_pcb == NULL)
			continue;
		if (CPU_IS_PRIMARY(ci))
			continue;
		if ((cpus_booted & (1UL << i)) == 0)
			continue;

		/*
		 * Link the processor into the list, and launch it.
		 */
		ci->ci_next = cpu_info_list->ci_next;
		cpu_info_list->ci_next = ci;
		atomic_setbits_ulong(&ci->ci_flags, CPUF_RUNNING);
		atomic_setbits_ulong(&cpus_running, (1UL << i));
		ncpus++;
	}

	/*
	 * Reset the HWRPB to prevent processors resetting from
	 * running through the hatching code again.
	 */
	hwrpb->rpb_restart = (u_int64_t) cpu_halt;
	hwrpb->rpb_checksum = hwrpb_checksum();
}

void
cpu_boot_secondary(struct cpu_info *ci)
{
	long timeout;
	struct pcs *pcsp, *primary_pcsp;
	struct pcb *pcb;
	u_long cpumask;

	pcb = ci->ci_idle_pcb;
	primary_pcsp = LOCATE_PCS(hwrpb, hwrpb->rpb_primary_cpu_id);
	pcsp = LOCATE_PCS(hwrpb, ci->ci_cpuid);
	cpumask = (1UL << ci->ci_cpuid);

	/*
	 * Set up the PCS's HWPCB to match ours.
	 */
	memcpy(pcsp->pcs_hwpcb, &pcb->pcb_hw, sizeof(pcb->pcb_hw));

	/*
	 * Set up the HWRPB to restart the secondary processor
	 * with our spin-up trampoline.
	 */
	hwrpb->rpb_restart = (u_int64_t) cpu_spinup_trampoline;
	hwrpb->rpb_restart_val = (u_int64_t) ci;
	hwrpb->rpb_checksum = hwrpb_checksum();

	/*
	 * Configure the CPU to start in OSF/1 PALcode by copying
	 * the primary CPU's PALcode revision info to the secondary
	 * CPUs PCS.
	 */
	memcpy(&pcsp->pcs_pal_rev, &primary_pcsp->pcs_pal_rev,
	    sizeof(pcsp->pcs_pal_rev));
	pcsp->pcs_flags |= (PCS_CV|PCS_RC);
	pcsp->pcs_flags &= ~PCS_BIP;

	/* Make sure the secondary console sees all this. */
	alpha_mb();

	/* Send a "START" command to the secondary CPU's console. */
	if (cpu_iccb_send(ci->ci_cpuid, "START\r\n")) {
		printf("%s: unable to issue `START' command\n",
		    ci->ci_dev->dv_xname);
		return;
	}

	/* Wait for the processor to boot. */
	for (timeout = 10000; timeout != 0; timeout--) {
		alpha_mb();
		if (pcsp->pcs_flags & PCS_BIP)
			break;
		delay(1000);
	}
	if (timeout == 0)
		printf("%s: processor failed to boot\n", ci->ci_dev->dv_xname);

	/*
	 * ...and now wait for verification that it's running kernel
	 * code.
	 */
	for (timeout = 10000; timeout != 0; timeout--) {
		alpha_mb();
		if (cpus_booted & cpumask)
			break;
		delay(1000);
	}
	if (timeout == 0)
		printf("%s: processor failed to hatch\n", ci->ci_dev->dv_xname);
}

void
cpu_pause_resume(u_long cpu_id, int pause)
{
	u_long cpu_mask = (1UL << cpu_id);

	if (pause) {
		atomic_setbits_ulong(&cpus_paused, cpu_mask);
		alpha_send_ipi(cpu_id, ALPHA_IPI_PAUSE);
	} else
		atomic_clearbits_ulong(&cpus_paused, cpu_mask);
}

void
cpu_pause_resume_all(int pause)
{
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;
		cpu_pause_resume(ci->ci_cpuid, pause);
	}
}

void
cpu_halt(void)
{
	struct cpu_info *ci = curcpu();
	u_long cpu_id = cpu_number();
	struct pcs *pcsp = LOCATE_PCS(hwrpb, cpu_id);

	printf("%s: shutting down...\n", ci->ci_dev->dv_xname);

	pcsp->pcs_flags &= ~(PCS_RC | PCS_HALT_REQ);
	pcsp->pcs_flags |= PCS_HALT_STAY_HALTED;

	atomic_clearbits_ulong(&cpus_running, (1UL << cpu_id));
	atomic_clearbits_ulong(&cpus_booted, (1U << cpu_id));

	alpha_pal_halt();
	/* NOTREACHED */
}

void
cpu_hatch(struct cpu_info *ci)
{
	u_long cpu_id = cpu_number();
	u_long cpumask = (1UL << cpu_id);

	/* Mark the kernel pmap active on this processor. */
	atomic_setbits_ulong(&pmap_kernel()->pm_cpus, cpumask);

	/* Initialize trap vectors for this processor. */
	trap_init();

	/* Yahoo!  We're running kernel code!  Announce it! */
	cpu_announce_extensions(ci);

	atomic_setbits_ulong(&cpus_booted, cpumask);

	/*
	 * Spin here until we're told we can start.
	 */
	while ((cpus_running & cpumask) == 0)
		/* spin */ ;

	/*
	 * Invalidate the TLB and sync the I-stream before we
	 * jump into the kernel proper.  We have to do this
	 * because we haven't been getting IPIs while we've
	 * been spinning.
	 */
	ALPHA_TBIA();
	alpha_pal_imb();

	clockqueue_init(&ci->ci_queue);
	KERNEL_LOCK();
	sched_init_cpu(ci);
	ci->ci_curproc = ci->ci_fpcurproc = NULL;
	ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
	KERNEL_UNLOCK();

	clockintr_cpu_init(NULL);

	(void) alpha_pal_swpipl(ALPHA_PSL_IPL_0);
	sched_toidle();
	/* NOTREACHED */
}

int
cpu_iccb_send(cpuid_t cpu_id, const char *msg)
{
	struct pcs *pcsp = LOCATE_PCS(hwrpb, cpu_id);
	int timeout;
	u_long cpumask = (1UL << cpu_id);

	/* Wait for the ICCB to become available. */
	for (timeout = 10000; timeout != 0; timeout--) {
		alpha_mb();
		if ((hwrpb->rpb_rxrdy & cpumask) == 0)
			break;
		delay(1000);
	}
	if (timeout == 0)
		return (EIO);

	/*
	 * Copy the message into the ICCB, and tell the secondary console
	 * that it's there.  The atomic operation performs a memory barrier.
	 */
	strlcpy(pcsp->pcs_iccb.iccb_rxbuf, msg,
	    sizeof pcsp->pcs_iccb.iccb_rxbuf);
	pcsp->pcs_iccb.iccb_rxlen = strlen(msg);
	/* XXX cast to volatile */
	atomic_setbits_ulong((volatile u_long *)&hwrpb->rpb_rxrdy, cpumask);
	alpha_mb();

	/* Wait for the message to be received. */
	for (timeout = 10000; timeout != 0; timeout--) {
		alpha_mb();
		if ((hwrpb->rpb_rxrdy & cpumask) == 0)
			break;
		delay(1000);
	}
	if (timeout == 0)
		return (EIO);

	return (0);
}

void
cpu_iccb_receive(void)
{
#if 0	/* Don't bother... we don't get any important messages anyhow. */
	u_int64_t txrdy;
	char *cp1, *cp2, buf[80];
	struct pcs *pcsp;
	u_int cnt;
	cpuid_t cpu_id;

	txrdy = hwrpb->rpb_txrdy;

	for (cpu_id = 0; cpu_id < hwrpb->rpb_pcs_cnt; cpu_id++) {
		if (txrdy & (1UL << cpu_id)) {
			pcsp = LOCATE_PCS(hwrpb, cpu_id);
			printf("Inter-console message from CPU %lu "
			    "HALT REASON = 0x%lx, FLAGS = 0x%lx\n",
			    cpu_id, pcsp->pcs_halt_reason, pcsp->pcs_flags);
			
			cnt = pcsp->pcs_iccb.iccb_txlen;
			if (cnt >= 80) {
				printf("Malformed inter-console message\n");
				continue;
			}
			cp1 = pcsp->pcs_iccb.iccb_txbuf;
			cp2 = buf;
			while (cnt--) {
				if (*cp1 != '\r' && *cp1 != '\n')
					*cp2++ = *cp1;
				cp1++;
			}
			*cp2 = '\0';
			printf("Message from CPU %lu: %s\n", cpu_id, buf);
		}
	}
#endif /* 0 */
	hwrpb->rpb_txrdy = 0;
	alpha_mb();
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		alpha_send_ipi(ci->ci_cpuid, ALPHA_IPI_AST);
}
#endif /* MULTIPROCESSOR */
