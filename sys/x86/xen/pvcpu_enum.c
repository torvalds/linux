/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Roger Pau Monné <roger.pau@citrix.com>
 * All rights reserved.
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/pcpu.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/intr_machdep.h>
#include <x86/apicvar.h>

#include <machine/cpu.h>
#include <machine/smp.h>
#include <machine/md_var.h>

#include <xen/xen-os.h>
#include <xen/xen_intr.h>
#include <xen/hypervisor.h>

#include <xen/interface/vcpu.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/aclocal.h>
#include <contrib/dev/acpica/include/actables.h>

#include <dev/acpica/acpivar.h>

static int xenpv_probe(void);
static int xenpv_probe_cpus(void);
static int xenpv_setup_local(void);
static int xenpv_setup_io(void);

static ACPI_TABLE_MADT *madt;
static vm_paddr_t madt_physaddr;
static vm_offset_t madt_length;

static struct apic_enumerator xenpv_enumerator = {
	.apic_name = "Xen PV",
	.apic_probe = xenpv_probe,
	.apic_probe_cpus = xenpv_probe_cpus,
	.apic_setup_local = xenpv_setup_local,
	.apic_setup_io = xenpv_setup_io
};

/*--------------------- Helper functions to parse MADT -----------------------*/

/*
 * Parse an interrupt source override for an ISA interrupt.
 */
static void
madt_parse_interrupt_override(ACPI_MADT_INTERRUPT_OVERRIDE *intr)
{
	enum intr_trigger trig;
	enum intr_polarity pol;
	int ret;

	if (acpi_quirks & ACPI_Q_MADT_IRQ0 && intr->SourceIrq == 0 &&
	    intr->GlobalIrq == 2) {
		if (bootverbose)
			printf("MADT: Skipping timer override\n");
		return;
	}

	madt_parse_interrupt_values(intr, &trig, &pol);

	/* Remap the IRQ if it is mapped to a different interrupt vector. */
	if (intr->SourceIrq != intr->GlobalIrq && intr->GlobalIrq > 15 &&
	    intr->SourceIrq == AcpiGbl_FADT.SciInterrupt)
		/*
		 * If the SCI is remapped to a non-ISA global interrupt,
		 * then override the vector we use to setup.
		 */
		acpi_OverrideInterruptLevel(intr->GlobalIrq);

	/* Register the IRQ with the polarity and trigger mode found. */
	ret = xen_register_pirq(intr->GlobalIrq, trig, pol);
	if (ret != 0)
		panic("Unable to register interrupt override");
}

/*
 * Call the handler routine for each entry in the MADT table.
 */
static void
madt_walk_table(acpi_subtable_handler *handler, void *arg)
{

	acpi_walk_subtables(madt + 1, (char *)madt + madt->Header.Length,
	    handler, arg);
}

/*
 * Parse interrupt entries.
 */
static void
madt_parse_ints(ACPI_SUBTABLE_HEADER *entry, void *arg __unused)
{

	if (entry->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE)
		madt_parse_interrupt_override(
		    (ACPI_MADT_INTERRUPT_OVERRIDE *)entry);
}

/*---------------------------- Xen PV enumerator -----------------------------*/

/*
 * This enumerator will only be registered on PVH
 */
static int
xenpv_probe(void)
{
	return (0);
}

/*
 * Test each possible vCPU in order to find the number of vCPUs
 */
static int
xenpv_probe_cpus(void)
{
#ifdef SMP
	int i, ret;

	for (i = 0; i < MAXCPU && (i * 2) < MAX_APIC_ID; i++) {
		ret = HYPERVISOR_vcpu_op(VCPUOP_is_up, i, NULL);
		mp_ncpus = min(mp_ncpus + 1, MAXCPU);
	}
	mp_maxid = mp_ncpus - 1;
	max_apic_id = mp_ncpus * 2;
#endif
	return (0);
}

/*
 * Initialize the vCPU id of the BSP
 */
static int
xenpv_setup_local(void)
{
#ifdef SMP
	int i, ret;

	for (i = 0; i < MAXCPU && (i * 2) < MAX_APIC_ID; i++) {
		ret = HYPERVISOR_vcpu_op(VCPUOP_is_up, i, NULL);
		if (ret >= 0)
			lapic_create((i * 2), (i == 0));
	}
#endif

	PCPU_SET(vcpu_id, 0);
	lapic_init(0);
	return (0);
}

/*
 * On PVH guests there's no IO APIC
 */
static int
xenpv_setup_io(void)
{

	if (xen_initial_domain()) {
		/*
		 * NB: we could iterate over the MADT IOAPIC entries in order
		 * to figure out the exact number of IOAPIC interrupts, but
		 * this is legacy code so just keep using the previous
		 * behaviour and assume a maximum of 256 interrupts.
		 */
		num_io_irqs = max(255, num_io_irqs);

		acpi_SetDefaultIntrModel(ACPI_INTR_APIC);
	}
	return (0);
}

void
xenpv_register_pirqs(struct pic *pic __unused)
{
	unsigned int i;
	int ret;

	/* Map MADT */
	madt_physaddr = acpi_find_table(ACPI_SIG_MADT);
	madt = acpi_map_table(madt_physaddr, ACPI_SIG_MADT);
	madt_length = madt->Header.Length;

	/* Try to initialize ACPI so that we can access the FADT. */
	ret = acpi_Startup();
	if (ACPI_FAILURE(ret)) {
		printf("MADT: ACPI Startup failed with %s\n",
		    AcpiFormatException(ret));
		printf("Try disabling either ACPI or apic support.\n");
		panic("Using MADT but ACPI doesn't work");
	}

	/* Run through the table to see if there are any overrides. */
	madt_walk_table(madt_parse_ints, NULL);

	/*
	 * If there was not an explicit override entry for the SCI,
	 * force it to use level trigger and active-low polarity.
	 */
	if (!madt_found_sci_override) {
		printf(
"MADT: Forcing active-low polarity and level trigger for SCI\n");
		ret = xen_register_pirq(AcpiGbl_FADT.SciInterrupt,
		    INTR_TRIGGER_LEVEL, INTR_POLARITY_LOW);
		if (ret != 0)
			panic("Unable to register SCI IRQ");
	}

	/* Register legacy ISA IRQs */
	for (i = 1; i < 16; i++) {
		if (intr_lookup_source(i) != NULL)
			continue;
		ret = xen_register_pirq(i, INTR_TRIGGER_EDGE,
		    INTR_POLARITY_LOW);
		if (ret != 0 && bootverbose)
			printf("Unable to register legacy IRQ#%u: %d\n", i,
			    ret);
	}
}

static void
xenpv_register(void *dummy __unused)
{
	if (xen_pv_domain()) {
		apic_register_enumerator(&xenpv_enumerator);
	}
}
SYSINIT(xenpv_register, SI_SUB_TUNABLES - 1, SI_ORDER_FIRST, xenpv_register, NULL);
