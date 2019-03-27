/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/smp.h>

#include <x86/apicreg.h>
#include <x86/apicvar.h>

#include <xen/xen-os.h>
#include <xen/features.h>
#include <xen/gnttab.h>
#include <xen/hypervisor.h>
#include <xen/hvm.h>
#include <xen/xen_intr.h>

#include <xen/interface/vcpu.h>

/*--------------------------------- Macros -----------------------------------*/

#define XEN_APIC_UNSUPPORTED \
	panic("%s: not available in Xen PV port.", __func__)


/*--------------------------- Forward Declarations ---------------------------*/
#ifdef SMP
static driver_filter_t xen_smp_rendezvous_action;
static driver_filter_t xen_invltlb;
static driver_filter_t xen_invlpg;
static driver_filter_t xen_invlrng;
static driver_filter_t xen_invlcache;
static driver_filter_t xen_ipi_bitmap_handler;
static driver_filter_t xen_cpustop_handler;
static driver_filter_t xen_cpususpend_handler;
static driver_filter_t xen_cpustophard_handler;
#endif

/*---------------------------------- Macros ----------------------------------*/
#define	IPI_TO_IDX(ipi) ((ipi) - APIC_IPI_INTS)

/*--------------------------------- Xen IPIs ---------------------------------*/
#ifdef SMP
struct xen_ipi_handler
{
	driver_filter_t	*filter;
	const char	*description;
};

static struct xen_ipi_handler xen_ipis[] = 
{
	[IPI_TO_IDX(IPI_RENDEZVOUS)]	= { xen_smp_rendezvous_action,	"r"   },
	[IPI_TO_IDX(IPI_INVLTLB)]	= { xen_invltlb,		"itlb"},
	[IPI_TO_IDX(IPI_INVLPG)]	= { xen_invlpg,			"ipg" },
	[IPI_TO_IDX(IPI_INVLRNG)]	= { xen_invlrng,		"irg" },
	[IPI_TO_IDX(IPI_INVLCACHE)]	= { xen_invlcache,		"ic"  },
	[IPI_TO_IDX(IPI_BITMAP_VECTOR)] = { xen_ipi_bitmap_handler,	"b"   },
	[IPI_TO_IDX(IPI_STOP)]		= { xen_cpustop_handler,	"st"  },
	[IPI_TO_IDX(IPI_SUSPEND)]	= { xen_cpususpend_handler,	"sp"  },
	[IPI_TO_IDX(IPI_STOP_HARD)]	= { xen_cpustophard_handler,	"sth" },
};
#endif

/*------------------------------- Per-CPU Data -------------------------------*/
#ifdef SMP
DPCPU_DEFINE(xen_intr_handle_t, ipi_handle[nitems(xen_ipis)]);
#endif

/*------------------------------- Xen PV APIC --------------------------------*/

static void
xen_pv_lapic_create(u_int apic_id, int boot_cpu)
{
#ifdef SMP
	cpu_add(apic_id, boot_cpu);
#endif
}

static void
xen_pv_lapic_init(vm_paddr_t addr)
{

}

static void
xen_pv_lapic_setup(int boot)
{

}

static void
xen_pv_lapic_dump(const char *str)
{

	printf("cpu%d %s XEN PV LAPIC\n", PCPU_GET(cpuid), str);
}

static void
xen_pv_lapic_disable(void)
{

}

static bool
xen_pv_lapic_is_x2apic(void)
{

	return (false);
}

static void
xen_pv_lapic_eoi(void)
{

	XEN_APIC_UNSUPPORTED;
}

static int
xen_pv_lapic_id(void)
{

	return (PCPU_GET(apic_id));
}

static int
xen_pv_lapic_intr_pending(u_int vector)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static u_int
xen_pv_apic_cpuid(u_int apic_id)
{
#ifdef SMP
	return (apic_cpuids[apic_id]);
#else
	return (0);
#endif
}

static u_int
xen_pv_apic_alloc_vector(u_int apic_id, u_int irq)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static u_int
xen_pv_apic_alloc_vectors(u_int apic_id, u_int *irqs, u_int count, u_int align)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static void
xen_pv_apic_disable_vector(u_int apic_id, u_int vector)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_apic_enable_vector(u_int apic_id, u_int vector)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_apic_free_vector(u_int apic_id, u_int vector, u_int irq)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_lapic_set_logical_id(u_int apic_id, u_int cluster, u_int cluster_id)
{

	XEN_APIC_UNSUPPORTED;
}

static int
xen_pv_lapic_enable_pmc(void)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static void
xen_pv_lapic_disable_pmc(void)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_lapic_reenable_pmc(void)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_lapic_enable_cmc(void)
{

}

#ifdef SMP
static void
xen_pv_lapic_ipi_raw(register_t icrlo, u_int dest)
{

	XEN_APIC_UNSUPPORTED;
}

static void
xen_pv_lapic_ipi_vectored(u_int vector, int dest)
{
	xen_intr_handle_t *ipi_handle;
	int ipi_idx, to_cpu, self;

	ipi_idx = IPI_TO_IDX(vector);
	if (ipi_idx >= nitems(xen_ipis))
		panic("IPI out of range");

	switch(dest) {
	case APIC_IPI_DEST_SELF:
		ipi_handle = DPCPU_GET(ipi_handle);
		xen_intr_signal(ipi_handle[ipi_idx]);
		break;
	case APIC_IPI_DEST_ALL:
		CPU_FOREACH(to_cpu) {
			ipi_handle = DPCPU_ID_GET(to_cpu, ipi_handle);
			xen_intr_signal(ipi_handle[ipi_idx]);
		}
		break;
	case APIC_IPI_DEST_OTHERS:
		self = PCPU_GET(cpuid);
		CPU_FOREACH(to_cpu) {
			if (to_cpu != self) {
				ipi_handle = DPCPU_ID_GET(to_cpu, ipi_handle);
				xen_intr_signal(ipi_handle[ipi_idx]);
			}
		}
		break;
	default:
		to_cpu = apic_cpuid(dest);
		ipi_handle = DPCPU_ID_GET(to_cpu, ipi_handle);
		xen_intr_signal(ipi_handle[ipi_idx]);
		break;
	}
}

static int
xen_pv_lapic_ipi_wait(int delay)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}
#endif	/* SMP */

static int
xen_pv_lapic_ipi_alloc(inthand_t *ipifunc)
{

	XEN_APIC_UNSUPPORTED;
	return (-1);
}

static void
xen_pv_lapic_ipi_free(int vector)
{

	XEN_APIC_UNSUPPORTED;
}

static int
xen_pv_lapic_set_lvt_mask(u_int apic_id, u_int lvt, u_char masked)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static int
xen_pv_lapic_set_lvt_mode(u_int apic_id, u_int lvt, uint32_t mode)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static int
xen_pv_lapic_set_lvt_polarity(u_int apic_id, u_int lvt, enum intr_polarity pol)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

static int
xen_pv_lapic_set_lvt_triggermode(u_int apic_id, u_int lvt,
    enum intr_trigger trigger)
{

	XEN_APIC_UNSUPPORTED;
	return (0);
}

/* Xen apic_ops implementation */
struct apic_ops xen_apic_ops = {
	.create			= xen_pv_lapic_create,
	.init			= xen_pv_lapic_init,
	.xapic_mode		= xen_pv_lapic_disable,
	.is_x2apic		= xen_pv_lapic_is_x2apic,
	.setup			= xen_pv_lapic_setup,
	.dump			= xen_pv_lapic_dump,
	.disable		= xen_pv_lapic_disable,
	.eoi			= xen_pv_lapic_eoi,
	.id			= xen_pv_lapic_id,
	.intr_pending		= xen_pv_lapic_intr_pending,
	.set_logical_id		= xen_pv_lapic_set_logical_id,
	.cpuid			= xen_pv_apic_cpuid,
	.alloc_vector		= xen_pv_apic_alloc_vector,
	.alloc_vectors		= xen_pv_apic_alloc_vectors,
	.enable_vector		= xen_pv_apic_enable_vector,
	.disable_vector		= xen_pv_apic_disable_vector,
	.free_vector		= xen_pv_apic_free_vector,
	.enable_pmc		= xen_pv_lapic_enable_pmc,
	.disable_pmc		= xen_pv_lapic_disable_pmc,
	.reenable_pmc		= xen_pv_lapic_reenable_pmc,
	.enable_cmc		= xen_pv_lapic_enable_cmc,
#ifdef SMP
	.ipi_raw		= xen_pv_lapic_ipi_raw,
	.ipi_vectored		= xen_pv_lapic_ipi_vectored,
	.ipi_wait		= xen_pv_lapic_ipi_wait,
#endif
	.ipi_alloc		= xen_pv_lapic_ipi_alloc,
	.ipi_free		= xen_pv_lapic_ipi_free,
	.set_lvt_mask		= xen_pv_lapic_set_lvt_mask,
	.set_lvt_mode		= xen_pv_lapic_set_lvt_mode,
	.set_lvt_polarity	= xen_pv_lapic_set_lvt_polarity,
	.set_lvt_triggermode	= xen_pv_lapic_set_lvt_triggermode,
};

#ifdef SMP
/*---------------------------- XEN PV IPI Handlers ---------------------------*/
/*
 * These are C clones of the ASM functions found in apic_vector.
 */
static int
xen_ipi_bitmap_handler(void *arg)
{
	struct trapframe *frame;

	frame = arg;
	ipi_bitmap_handler(*frame);
	return (FILTER_HANDLED);
}

static int
xen_smp_rendezvous_action(void *arg)
{
#ifdef COUNT_IPIS
	(*ipi_rendezvous_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	smp_rendezvous_action();
	return (FILTER_HANDLED);
}

static int
xen_invltlb(void *arg)
{

	invltlb_handler();
	return (FILTER_HANDLED);
}

#ifdef __amd64__
static int
xen_invltlb_invpcid(void *arg)
{

	invltlb_invpcid_handler();
	return (FILTER_HANDLED);
}

static int
xen_invltlb_pcid(void *arg)
{

	invltlb_pcid_handler();
	return (FILTER_HANDLED);
}

static int
xen_invltlb_invpcid_pti(void *arg)
{

	invltlb_invpcid_pti_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlpg_invpcid_handler(void *arg)
{

	invlpg_invpcid_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlpg_pcid_handler(void *arg)
{

	invlpg_pcid_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlrng_invpcid_handler(void *arg)
{

	invlrng_invpcid_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlrng_pcid_handler(void *arg)
{

	invlrng_pcid_handler();
	return (FILTER_HANDLED);
}
#endif

static int
xen_invlpg(void *arg)
{

	invlpg_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlrng(void *arg)
{

	invlrng_handler();
	return (FILTER_HANDLED);
}

static int
xen_invlcache(void *arg)
{

	invlcache_handler();
	return (FILTER_HANDLED);
}

static int
xen_cpustop_handler(void *arg)
{

	cpustop_handler();
	return (FILTER_HANDLED);
}

static int
xen_cpususpend_handler(void *arg)
{

	cpususpend_handler();
	return (FILTER_HANDLED);
}

static int
xen_cpustophard_handler(void *arg)
{

	ipi_nmi_handler();
	return (FILTER_HANDLED);
}

/*----------------------------- XEN PV IPI setup -----------------------------*/
/*
 * Those functions are provided outside of the Xen PV APIC implementation
 * so PVHVM guests can also use PV IPIs without having an actual Xen PV APIC,
 * because on PVHVM there's an emulated LAPIC provided by Xen.
 */
static void
xen_cpu_ipi_init(int cpu)
{
	xen_intr_handle_t *ipi_handle;
	const struct xen_ipi_handler *ipi;
	int idx, rc;

	ipi_handle = DPCPU_ID_GET(cpu, ipi_handle);

	for (ipi = xen_ipis, idx = 0; idx < nitems(xen_ipis); ipi++, idx++) {

		if (ipi->filter == NULL) {
			ipi_handle[idx] = NULL;
			continue;
		}

		rc = xen_intr_alloc_and_bind_ipi(cpu, ipi->filter,
		    INTR_TYPE_TTY, &ipi_handle[idx]);
		if (rc != 0)
			panic("Unable to allocate a XEN IPI port");
		xen_intr_describe(ipi_handle[idx], "%s", ipi->description);
	}
}

static void
xen_setup_cpus(void)
{
	int i;

	if (!xen_vector_callback_enabled)
		return;

#ifdef __amd64__
	if (pmap_pcid_enabled) {
		if (pti)
			xen_ipis[IPI_TO_IDX(IPI_INVLTLB)].filter =
			    invpcid_works ? xen_invltlb_invpcid_pti :
			    xen_invltlb_pcid;
		else
			xen_ipis[IPI_TO_IDX(IPI_INVLTLB)].filter =
			    invpcid_works ? xen_invltlb_invpcid :
			    xen_invltlb_pcid;
		xen_ipis[IPI_TO_IDX(IPI_INVLPG)].filter = invpcid_works ?
		    xen_invlpg_invpcid_handler : xen_invlpg_pcid_handler;
		xen_ipis[IPI_TO_IDX(IPI_INVLRNG)].filter = invpcid_works ?
		    xen_invlrng_invpcid_handler : xen_invlrng_pcid_handler;
	}
#endif
	CPU_FOREACH(i)
		xen_cpu_ipi_init(i);

	/* Set the xen pv ipi ops to replace the native ones */
	if (xen_hvm_domain())
		apic_ops.ipi_vectored = xen_pv_lapic_ipi_vectored;
}

/* Switch to using PV IPIs as soon as the vcpu_id is set. */
SYSINIT(xen_setup_cpus, SI_SUB_SMP, SI_ORDER_SECOND, xen_setup_cpus, NULL);
#endif /* SMP */
