/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, 2013 Citrix Systems, Inc.
 * Copyright (c) 2012 Spectra Logic Corporation
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
#include <vm/vm_param.h>

#include <dev/pci/pcivar.h>

#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/smp.h>

#include <x86/apicreg.h>

#include <xen/xen-os.h>
#include <xen/error.h>
#include <xen/features.h>
#include <xen/gnttab.h>
#include <xen/hypervisor.h>
#include <xen/hvm.h>
#include <xen/xen_intr.h>

#include <xen/interface/arch-x86/cpuid.h>
#include <xen/interface/hvm/params.h>
#include <xen/interface/vcpu.h>

/*--------------------------- Forward Declarations ---------------------------*/
static void xen_hvm_cpu_init(void);

/*-------------------------------- Global Data -------------------------------*/
enum xen_domain_type xen_domain_type = XEN_NATIVE;

#ifdef SMP
struct cpu_ops xen_hvm_cpu_ops = {
	.cpu_init	= xen_hvm_cpu_init,
	.cpu_resume	= xen_hvm_cpu_init
};
#endif

static MALLOC_DEFINE(M_XENHVM, "xen_hvm", "Xen HVM PV Support");

/**
 * If non-zero, the hypervisor has been configured to use a direct
 * IDT event callback for interrupt injection.
 */
int xen_vector_callback_enabled;

/**
 * Start info flags. ATM this only used to store the initial domain flag for
 * PVHv2, and it's always empty for HVM guests.
 */
uint32_t hvm_start_flags;

/**
 * Signal whether the vector injected for the event channel upcall requires to
 * be EOI'ed on the local APIC.
 */
bool xen_evtchn_needs_ack;

/*------------------------------- Per-CPU Data -------------------------------*/
DPCPU_DEFINE(struct vcpu_info, vcpu_local_info);
DPCPU_DEFINE(struct vcpu_info *, vcpu_info);

/*------------------ Hypervisor Access Shared Memory Regions -----------------*/
shared_info_t *HYPERVISOR_shared_info;

/*------------------------------ Sysctl tunables -----------------------------*/
int xen_disable_pv_disks = 0;
int xen_disable_pv_nics = 0;
TUNABLE_INT("hw.xen.disable_pv_disks", &xen_disable_pv_disks);
TUNABLE_INT("hw.xen.disable_pv_nics", &xen_disable_pv_nics);

/*---------------------- XEN Hypervisor Probe and Setup ----------------------*/

static uint32_t cpuid_base;

static uint32_t
xen_hvm_cpuid_base(void)
{
	uint32_t base, regs[4];

	for (base = 0x40000000; base < 0x40010000; base += 0x100) {
		do_cpuid(base, regs);
		if (!memcmp("XenVMMXenVMM", &regs[1], 12)
		    && (regs[0] - base) >= 2)
			return (base);
	}
	return (0);
}

static void
hypervisor_quirks(unsigned int major, unsigned int minor)
{
#ifdef SMP
	if (((major < 4) || (major == 4 && minor <= 5)) &&
	    msix_disable_migration == -1) {
		/*
		 * Xen hypervisors prior to 4.6.0 do not properly
		 * handle updates to enabled MSI-X table entries,
		 * so disable MSI-X interrupt migration in that
		 * case.
		 */
		if (bootverbose)
			printf(
"Disabling MSI-X interrupt migration due to Xen hypervisor bug.\n"
"Set machdep.msix_disable_migration=0 to forcefully enable it.\n");
		msix_disable_migration = 1;
	}
#endif
}

static void
hypervisor_version(void)
{
	uint32_t regs[4];
	int major, minor;

	do_cpuid(cpuid_base + 1, regs);

	major = regs[0] >> 16;
	minor = regs[0] & 0xffff;
	printf("XEN: Hypervisor version %d.%d detected.\n", major, minor);

	hypervisor_quirks(major, minor);
}

/*
 * Allocate and fill in the hypcall page.
 */
int
xen_hvm_init_hypercall_stubs(enum xen_hvm_init_type init_type)
{
	uint32_t regs[4];

	/* Legacy PVH will get here without the cpuid leaf being set. */
	if (cpuid_base == 0)
		cpuid_base = xen_hvm_cpuid_base();
	if (cpuid_base == 0)
		return (ENXIO);

	if (xen_domain() && init_type == XEN_HVM_INIT_LATE) {
		/*
		 * If the domain type is already set we can assume that the
		 * hypercall page has been populated too, so just print the
		 * version (and apply any quirks) and exit.
		 */
		hypervisor_version();
		return 0;
	}

	if (init_type == XEN_HVM_INIT_LATE)
		hypervisor_version();

	/*
	 * Find the hypercall pages.
	 */
	do_cpuid(cpuid_base + 2, regs);
	if (regs[0] != 1)
		return (EINVAL);

	wrmsr(regs[1], (init_type == XEN_HVM_INIT_EARLY)
	    ? ((vm_paddr_t)&hypercall_page - KERNBASE)
	    : vtophys(&hypercall_page));

	return (0);
}

static void
xen_hvm_init_shared_info_page(void)
{
	struct xen_add_to_physmap xatp;

	if (xen_pv_domain()) {
		/*
		 * Already setup in the PV case, shared_info is passed inside
		 * of the start_info struct at start of day.
		 */
		return;
	}

	if (HYPERVISOR_shared_info == NULL) {
		HYPERVISOR_shared_info = malloc(PAGE_SIZE, M_XENHVM, M_NOWAIT);
		if (HYPERVISOR_shared_info == NULL)
			panic("Unable to allocate Xen shared info page");
	}

	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = vtophys(HYPERVISOR_shared_info) >> PAGE_SHIFT;
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
		panic("HYPERVISOR_memory_op failed");
}

static int
set_percpu_callback(unsigned int vcpu)
{
	struct xen_hvm_evtchn_upcall_vector vec;
	int error;

	vec.vcpu = vcpu;
	vec.vector = IDT_EVTCHN;
	error = HYPERVISOR_hvm_op(HVMOP_set_evtchn_upcall_vector, &vec);

	return (error != 0 ? xen_translate_error(error) : 0);
}

/*
 * Tell the hypervisor how to contact us for event channel callbacks.
 */
void
xen_hvm_set_callback(device_t dev)
{
	struct xen_hvm_param xhp;
	int irq;

	if (xen_vector_callback_enabled)
		return;

	xhp.domid = DOMID_SELF;
	xhp.index = HVM_PARAM_CALLBACK_IRQ;
	if (xen_feature(XENFEAT_hvm_callback_vector) != 0) {
		int error;

		error = set_percpu_callback(0);
		if (error == 0) {
			xen_evtchn_needs_ack = true;
			/* Trick toolstack to think we are enlightened */
			xhp.value = 1;
		} else
			xhp.value = HVM_CALLBACK_VECTOR(IDT_EVTCHN);
		error = HYPERVISOR_hvm_op(HVMOP_set_param, &xhp);
		if (error == 0) {
			xen_vector_callback_enabled = 1;
			return;
		} else if (xen_evtchn_needs_ack)
			panic("Unable to setup fake HVM param: %d", error);

		printf("Xen HVM callback vector registration failed (%d). "
		    "Falling back to emulated device interrupt\n", error);
	}
	xen_vector_callback_enabled = 0;
	if (dev == NULL) {
		/*
		 * Called from early boot or resume.
		 * xenpci will invoke us again later.
		 */
		return;
	}

	irq = pci_get_irq(dev);
	if (irq < 16) {
		xhp.value = HVM_CALLBACK_GSI(irq);
	} else {
		u_int slot;
		u_int pin;

		slot = pci_get_slot(dev);
		pin = pci_get_intpin(dev) - 1;
		xhp.value = HVM_CALLBACK_PCI_INTX(slot, pin);
	}

	if (HYPERVISOR_hvm_op(HVMOP_set_param, &xhp) != 0)
		panic("Can't set evtchn callback");
}

#define	XEN_MAGIC_IOPORT 0x10
enum {
	XMI_MAGIC			 = 0x49d2,
	XMI_UNPLUG_IDE_DISKS		 = 0x01,
	XMI_UNPLUG_NICS			 = 0x02,
	XMI_UNPLUG_IDE_EXCEPT_PRI_MASTER = 0x04
};

static void
xen_hvm_disable_emulated_devices(void)
{
	u_short disable_devs = 0;

	if (xen_pv_domain()) {
		/*
		 * No emulated devices in the PV case, so no need to unplug
		 * anything.
		 */
		if (xen_disable_pv_disks != 0 || xen_disable_pv_nics != 0)
			printf("PV devices cannot be disabled in PV guests\n");
		return;
	}

	if (inw(XEN_MAGIC_IOPORT) != XMI_MAGIC)
		return;

	if (xen_disable_pv_disks == 0) {
		if (bootverbose)
			printf("XEN: disabling emulated disks\n");
		disable_devs |= XMI_UNPLUG_IDE_DISKS;
	}
	if (xen_disable_pv_nics == 0) {
		if (bootverbose)
			printf("XEN: disabling emulated nics\n");
		disable_devs |= XMI_UNPLUG_NICS;
	}

	if (disable_devs != 0)
		outw(XEN_MAGIC_IOPORT, disable_devs);
}

static void
xen_hvm_init(enum xen_hvm_init_type init_type)
{
	int error;
	int i;

	if (init_type == XEN_HVM_INIT_CANCELLED_SUSPEND)
		return;

	error = xen_hvm_init_hypercall_stubs(init_type);

	switch (init_type) {
	case XEN_HVM_INIT_LATE:
		if (error != 0)
			return;

		/*
		 * If xen_domain_type is not set at this point
		 * it means we are inside a (PV)HVM guest, because
		 * for PVH the guest type is set much earlier
		 * (see hammer_time_xen).
		 */
		if (!xen_domain()) {
			xen_domain_type = XEN_HVM_DOMAIN;
			vm_guest = VM_GUEST_XEN;
		}

		setup_xen_features();
#ifdef SMP
		cpu_ops = xen_hvm_cpu_ops;
#endif
		break;
	case XEN_HVM_INIT_RESUME:
		if (error != 0)
			panic("Unable to init Xen hypercall stubs on resume");

		/* Clear stale vcpu_info. */
		CPU_FOREACH(i)
			DPCPU_ID_SET(i, vcpu_info, NULL);
		break;
	default:
		panic("Unsupported HVM initialization type");
	}

	xen_vector_callback_enabled = 0;
	xen_evtchn_needs_ack = false;
	xen_hvm_set_callback(NULL);

	/*
	 * On (PV)HVM domains we need to request the hypervisor to
	 * fill the shared info page, for PVH guest the shared_info page
	 * is passed inside the start_info struct and is already set, so this
	 * functions are no-ops.
	 */
	xen_hvm_init_shared_info_page();
	xen_hvm_disable_emulated_devices();
} 

void
xen_hvm_suspend(void)
{
}

void
xen_hvm_resume(bool suspend_cancelled)
{

	xen_hvm_init(suspend_cancelled ?
	    XEN_HVM_INIT_CANCELLED_SUSPEND : XEN_HVM_INIT_RESUME);

	/* Register vcpu_info area for CPU#0. */
	xen_hvm_cpu_init();
}
 
static void
xen_hvm_sysinit(void *arg __unused)
{
	xen_hvm_init(XEN_HVM_INIT_LATE);
}
SYSINIT(xen_hvm_init, SI_SUB_HYPERVISOR, SI_ORDER_FIRST, xen_hvm_sysinit, NULL);

static void
xen_hvm_cpu_init(void)
{
	struct vcpu_register_vcpu_info info;
	struct vcpu_info *vcpu_info;
	uint32_t regs[4];
	int cpu, rc;

	if (!xen_domain())
		return;

	if (DPCPU_GET(vcpu_info) != NULL) {
		/*
		 * vcpu_info is already set.  We're resuming
		 * from a failed migration and our pre-suspend
		 * configuration is still valid.
		 */
		return;
	}

	/*
	 * Set vCPU ID. If available fetch the ID from CPUID, if not just use
	 * the ACPI ID.
	 */
	KASSERT(cpuid_base != 0, ("Invalid base Xen CPUID leaf"));
	cpuid_count(cpuid_base + 4, 0, regs);
	KASSERT((regs[0] & XEN_HVM_CPUID_VCPU_ID_PRESENT) ||
	    !xen_pv_domain(),
	    ("Xen PV domain without vcpu_id in cpuid"));
	PCPU_SET(vcpu_id, (regs[0] & XEN_HVM_CPUID_VCPU_ID_PRESENT) ?
	    regs[1] : PCPU_GET(acpi_id));

	if (xen_evtchn_needs_ack && !IS_BSP()) {
		/*
		 * Setup the per-vpcu event channel upcall vector. This is only
		 * required when using the new HVMOP_set_evtchn_upcall_vector
		 * hypercall, which allows using a different vector for each
		 * vCPU. Note that FreeBSD uses the same vector for all vCPUs
		 * because it's not dynamically allocated.
		 */
		rc = set_percpu_callback(PCPU_GET(vcpu_id));
		if (rc != 0)
			panic("Event channel upcall vector setup failed: %d",
			    rc);
	}

	/*
	 * Set the vCPU info.
	 *
	 * NB: the vCPU info for vCPUs < 32 can be fetched from the shared info
	 * page, but in order to make sure the mapping code is correct always
	 * attempt to map the vCPU info at a custom place.
	 */
	vcpu_info = DPCPU_PTR(vcpu_local_info);
	cpu = PCPU_GET(vcpu_id);
	info.mfn = vtophys(vcpu_info) >> PAGE_SHIFT;
	info.offset = vtophys(vcpu_info) - trunc_page(vtophys(vcpu_info));

	rc = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, cpu, &info);
	if (rc != 0)
		DPCPU_SET(vcpu_info, &HYPERVISOR_shared_info->vcpu_info[cpu]);
	else
		DPCPU_SET(vcpu_info, vcpu_info);
}
SYSINIT(xen_hvm_cpu_init, SI_SUB_INTR, SI_ORDER_FIRST, xen_hvm_cpu_init, NULL);

/* HVM/PVH start_info accessors */
static vm_paddr_t
hvm_get_xenstore_mfn(void)
{

	return (hvm_get_parameter(HVM_PARAM_STORE_PFN));
}

static evtchn_port_t
hvm_get_xenstore_evtchn(void)
{

	return (hvm_get_parameter(HVM_PARAM_STORE_EVTCHN));
}

static vm_paddr_t
hvm_get_console_mfn(void)
{

	return (hvm_get_parameter(HVM_PARAM_CONSOLE_PFN));
}

static evtchn_port_t
hvm_get_console_evtchn(void)
{

	return (hvm_get_parameter(HVM_PARAM_CONSOLE_EVTCHN));
}

static uint32_t
hvm_get_start_flags(void)
{

	return (hvm_start_flags);
}

struct hypervisor_info hypervisor_info = {
	.get_xenstore_mfn		= hvm_get_xenstore_mfn,
	.get_xenstore_evtchn		= hvm_get_xenstore_evtchn,
	.get_console_mfn		= hvm_get_console_mfn,
	.get_console_evtchn		= hvm_get_console_evtchn,
	.get_start_flags		= hvm_get_start_flags,
};
