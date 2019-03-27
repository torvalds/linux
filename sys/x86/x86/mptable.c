/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include "opt_mptable_force_htt.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/smp.h>
#ifdef NEW_PCIB
#include <sys/rman.h>
#endif

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <dev/pci/pcivar.h>
#ifdef NEW_PCIB
#include <dev/pci/pcib_private.h>
#endif
#include <x86/apicreg.h>
#include <x86/mptable.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <machine/md_var.h>
#include <machine/pc/bios.h>
#ifdef NEW_PCIB
#include <machine/resource.h>
#endif
#include <machine/specialreg.h>

/* string defined by the Intel MP Spec as identifying the MP table */
#define	MP_SIG			0x5f504d5f	/* _MP_ */

#ifdef __amd64__
#define	MAX_LAPIC_ID		63	/* Max local APIC ID for HTT fixup */
#else
#define	MAX_LAPIC_ID		31	/* Max local APIC ID for HTT fixup */
#endif

#define BIOS_BASE		(0xf0000)
#define BIOS_SIZE		(0x10000)
#define BIOS_COUNT		(BIOS_SIZE/4)

typedef	void mptable_entry_handler(u_char *entry, void *arg);
typedef	void mptable_extended_entry_handler(ext_entry_ptr entry, void *arg);

/* descriptions of MP table entries */
typedef struct BASETABLE_ENTRY {
	uint8_t	type;
	uint8_t	length;
	uint8_t	name[16];
}       basetable_entry;

static basetable_entry basetable_entry_types[] =
{
	{0, 20, "Processor"},
	{1, 8, "Bus"},
	{2, 8, "I/O APIC"},
	{3, 8, "I/O INT"},
	{4, 8, "Local INT"}
};

typedef struct BUSDATA {
	u_char  bus_id;
	enum busTypes bus_type;
}       bus_datum;

typedef struct INTDATA {
	u_char  int_type;
	u_short int_flags;
	u_char  src_bus_id;
	u_char  src_bus_irq;
	u_char  dst_apic_id;
	u_char  dst_apic_int;
	u_char	int_vector;
}       io_int, local_int;

typedef struct BUSTYPENAME {
	u_char  type;
	char    name[7];
}       bus_type_name;

/* From MP spec v1.4, table 4-8. */
static bus_type_name bus_type_table[] =
{
	{UNKNOWN_BUSTYPE, "CBUS  "},
	{UNKNOWN_BUSTYPE, "CBUSII"},
	{EISA, "EISA  "},
	{UNKNOWN_BUSTYPE, "FUTURE"},
	{UNKNOWN_BUSTYPE, "INTERN"},
	{ISA, "ISA   "},
	{UNKNOWN_BUSTYPE, "MBI   "},
	{UNKNOWN_BUSTYPE, "MBII  "},
	{MCA, "MCA   "},
	{UNKNOWN_BUSTYPE, "MPI   "},
	{UNKNOWN_BUSTYPE, "MPSA  "},
	{UNKNOWN_BUSTYPE, "NUBUS "},
	{PCI, "PCI   "},
	{UNKNOWN_BUSTYPE, "PCMCIA"},
	{UNKNOWN_BUSTYPE, "TC    "},
	{UNKNOWN_BUSTYPE, "VL    "},
	{UNKNOWN_BUSTYPE, "VME   "},
	{UNKNOWN_BUSTYPE, "XPRESS"}
};

/* From MP spec v1.4, table 5-1. */
static int default_data[7][5] =
{
/*   nbus, id0, type0, id1, type1 */
	{1, 0, ISA, 255, NOBUS},
	{1, 0, EISA, 255, NOBUS},
	{1, 0, EISA, 255, NOBUS},
	{1, 0, MCA, 255, NOBUS},
	{2, 0, ISA, 1, PCI},
	{2, 0, EISA, 1, PCI},
	{2, 0, MCA, 1, PCI}
};

struct pci_probe_table_args {
	u_char bus;
	u_char found;
};

struct pci_route_interrupt_args {
	u_char bus;		/* Source bus. */
	u_char irq;		/* Source slot:pin. */
	int vector;		/* Return value. */
};

static mpfps_t mpfps;
static mpcth_t mpct;
static ext_entry_ptr mpet;
static void *ioapics[IOAPIC_MAX_ID + 1];
static bus_datum *busses;
static int mptable_nioapics, mptable_nbusses, mptable_maxbusid;
static int pci0 = -1;

static MALLOC_DEFINE(M_MPTABLE, "mptable", "MP Table Items");

static enum intr_polarity conforming_polarity(u_char src_bus,
	    u_char src_bus_irq);
static enum intr_trigger conforming_trigger(u_char src_bus, u_char src_bus_irq);
static enum intr_polarity intentry_polarity(int_entry_ptr intr);
static enum intr_trigger intentry_trigger(int_entry_ptr intr);
static int	lookup_bus_type(char *name);
static void	mptable_count_items(void);
static void	mptable_count_items_handler(u_char *entry, void *arg);
#ifdef MPTABLE_FORCE_HTT
static void	mptable_hyperthread_fixup(u_int id_mask);
#endif
static void	mptable_parse_apics_and_busses(void);
static void	mptable_parse_apics_and_busses_handler(u_char *entry,
    void *arg);
static void	mptable_parse_default_config_ints(void);
static void	mptable_parse_ints(void);
static void	mptable_parse_ints_handler(u_char *entry, void *arg);
static void	mptable_parse_io_int(int_entry_ptr intr);
static void	mptable_parse_local_int(int_entry_ptr intr);
static void	mptable_pci_probe_table_handler(u_char *entry, void *arg);
static void	mptable_pci_route_interrupt_handler(u_char *entry, void *arg);
static void	mptable_pci_setup(void);
static int	mptable_probe(void);
static int	mptable_probe_cpus(void);
static void	mptable_probe_cpus_handler(u_char *entry, void *arg __unused);
static void	mptable_setup_cpus_handler(u_char *entry, void *arg __unused);
static void	mptable_register(void *dummy);
static int	mptable_setup_local(void);
static int	mptable_setup_io(void);
#ifdef NEW_PCIB
static void	mptable_walk_extended_table(
    mptable_extended_entry_handler *handler, void *arg);
#endif
static void	mptable_walk_table(mptable_entry_handler *handler, void *arg);
static int	search_for_sig(u_int32_t target, int count);

static struct apic_enumerator mptable_enumerator = {
	.apic_name = "MPTable",
	.apic_probe = mptable_probe,
	.apic_probe_cpus = mptable_probe_cpus,
	.apic_setup_local = mptable_setup_local,
	.apic_setup_io = mptable_setup_io
};

/*
 * look for the MP spec signature
 */

static int
search_for_sig(u_int32_t target, int count)
{
	int     x;
	u_int32_t *addr;

	addr = (u_int32_t *)BIOS_PADDRTOVADDR(target);
	for (x = 0; x < count; x += 4)
		if (addr[x] == MP_SIG)
			/* make array index a byte index */
			return (target + (x * sizeof(u_int32_t)));
	return (-1);
}

static int
lookup_bus_type(char *name)
{
	int     x;

	for (x = 0; x < MAX_BUSTYPE; ++x)
		if (strncmp(bus_type_table[x].name, name, 6) == 0)
			return (bus_type_table[x].type);

	return (UNKNOWN_BUSTYPE);
}

/*
 * Look for an Intel MP spec table (ie, SMP capable hardware).
 */
static int
mptable_probe(void)
{
	int     x;
	u_long  segment;
	u_int32_t target;

	/* see if EBDA exists */
	if ((segment = *(u_short *)BIOS_PADDRTOVADDR(0x40e)) != 0) {
		/* search first 1K of EBDA */
		target = (u_int32_t) (segment << 4);
		if ((x = search_for_sig(target, 1024 / 4)) >= 0)
			goto found;
	} else {
		/* last 1K of base memory, effective 'top of base' passed in */
		target = (u_int32_t) ((basemem * 1024) - 0x400);
		if ((x = search_for_sig(target, 1024 / 4)) >= 0)
			goto found;
	}

	/* search the BIOS */
	target = (u_int32_t) BIOS_BASE;
	if ((x = search_for_sig(target, BIOS_COUNT)) >= 0)
		goto found;

	/* nothing found */
	return (ENXIO);

found:
	mpfps = (mpfps_t)BIOS_PADDRTOVADDR(x);

	/* Map in the configuration table if it exists. */
	if (mpfps->config_type != 0) {
		if (bootverbose)
			printf(
		"MP Table version 1.%d found using Default Configuration %d\n",
			    mpfps->spec_rev, mpfps->config_type);
		if (mpfps->config_type != 5 && mpfps->config_type != 6) {
			printf(
			"MP Table Default Configuration %d is unsupported\n",
			    mpfps->config_type);
			return (ENXIO);
		}
		mpct = NULL;
	} else {
		if ((uintptr_t)mpfps->pap >= 1024 * 1024) {
			printf("%s: Unable to map MP Configuration Table\n",
			    __func__);
			return (ENXIO);
		}
		mpct = (mpcth_t)BIOS_PADDRTOVADDR((uintptr_t)mpfps->pap);
		if (mpct->base_table_length + (uintptr_t)mpfps->pap >=
		    1024 * 1024) {
			printf("%s: Unable to map end of MP Config Table\n",
			    __func__);
			return (ENXIO);
		}
		if (mpct->extended_table_length != 0 &&
		    mpct->extended_table_length + mpct->base_table_length +
		    (uintptr_t)mpfps->pap < 1024 * 1024)
			mpet = (ext_entry_ptr)((char *)mpct +
			    mpct->base_table_length);
		if (mpct->signature[0] != 'P' || mpct->signature[1] != 'C' ||
		    mpct->signature[2] != 'M' || mpct->signature[3] != 'P') {
			printf("%s: MP Config Table has bad signature: %c%c%c%c\n",
			    __func__, mpct->signature[0], mpct->signature[1],
			    mpct->signature[2], mpct->signature[3]);
			return (ENXIO);
		}
		if (bootverbose)
			printf(
			"MP Configuration Table version 1.%d found at %p\n",
			    mpct->spec_rev, mpct);
	}

	return (-100);
}

/*
 * Run through the MP table enumerating CPUs.
 */
static int
mptable_probe_cpus(void)
{
	u_int cpu_mask;

	/* Is this a pre-defined config? */
	if (mpfps->config_type != 0) {
#ifdef SMP
		mp_ncpus = 2;
		mp_maxid = 1;
#endif
		max_apic_id = 1;
	} else {
		mptable_walk_table(mptable_probe_cpus_handler, &cpu_mask);
	}
	return (0);
}

/*
 * Initialize the local APIC on the BSP.
 */
static int
mptable_setup_local(void)
{
	vm_paddr_t addr;
	u_int cpu_mask;

	/* Is this a pre-defined config? */
	printf("MPTable: <");
	if (mpfps->config_type != 0) {
		lapic_create(0, 1);
		lapic_create(1, 0);
		addr = DEFAULT_APIC_BASE;
		printf("Default Configuration %d", mpfps->config_type);

	} else {
		cpu_mask = 0;
		mptable_walk_table(mptable_setup_cpus_handler, &cpu_mask);
#ifdef MPTABLE_FORCE_HTT
		mptable_hyperthread_fixup(cpu_mask);
#endif
		addr = mpct->apic_address;
		printf("%.*s %.*s", (int)sizeof(mpct->oem_id), mpct->oem_id,
		    (int)sizeof(mpct->product_id), mpct->product_id);
	}
	printf(">\n");
	lapic_init(addr);
	return (0);
}

/*
 * Run through the MP table enumerating I/O APICs.
 */
static int
mptable_setup_io(void)
{
	int i;
	u_char byte;

	/* First, we count individual items and allocate arrays. */
	mptable_count_items();
	busses = malloc((mptable_maxbusid + 1) * sizeof(bus_datum), M_MPTABLE,
	    M_WAITOK);
	for (i = 0; i <= mptable_maxbusid; i++)
		busses[i].bus_type = NOBUS;

	/* Second, we run through adding I/O APIC's and buses. */
	mptable_parse_apics_and_busses();	

	/* Third, we run through the table tweaking interrupt sources. */
	mptable_parse_ints();

	/* Fourth, we register all the I/O APIC's. */
	for (i = 0; i <= IOAPIC_MAX_ID; i++)
		if (ioapics[i] != NULL)
			ioapic_register(ioapics[i]);

	/* Fifth, we setup data structures to handle PCI interrupt routing. */
	mptable_pci_setup();

	/* Finally, we throw the switch to enable the I/O APIC's. */
	if (mpfps->mpfb2 & MPFB2_IMCR_PRESENT) {
		outb(0x22, 0x70);	/* select IMCR */
		byte = inb(0x23);	/* current contents */
		byte |= 0x01;		/* mask external INTR */
		outb(0x23, byte);	/* disconnect 8259s/NMI */
	}

	return (0);
}

static void
mptable_register(void *dummy __unused)
{

	apic_register_enumerator(&mptable_enumerator);
}
SYSINIT(mptable_register, SI_SUB_TUNABLES - 1, SI_ORDER_FIRST, mptable_register,
    NULL);

/*
 * Call the handler routine for each entry in the MP config base table.
 */
static void
mptable_walk_table(mptable_entry_handler *handler, void *arg)
{
	u_int i;
	u_char *entry;

	entry = (u_char *)(mpct + 1);
	for (i = 0; i < mpct->entry_count; i++) {
		switch (*entry) {
		case MPCT_ENTRY_PROCESSOR:
		case MPCT_ENTRY_IOAPIC:
		case MPCT_ENTRY_BUS:
		case MPCT_ENTRY_INT:
		case MPCT_ENTRY_LOCAL_INT:
			break;
		default:
			panic("%s: Unknown MP Config Entry %d\n", __func__,
			    (int)*entry);
		}
		handler(entry, arg);
		entry += basetable_entry_types[*entry].length;
	}
}

#ifdef NEW_PCIB
/*
 * Call the handler routine for each entry in the MP config extended
 * table.
 */
static void
mptable_walk_extended_table(mptable_extended_entry_handler *handler, void *arg)
{
	ext_entry_ptr end, entry;

	if (mpet == NULL)
		return;
	entry = mpet;
	end = (ext_entry_ptr)((char *)mpet + mpct->extended_table_length);
	while (entry < end) {
		handler(entry, arg);
		entry = (ext_entry_ptr)((char *)entry + entry->length);
	}
}
#endif

static void
mptable_probe_cpus_handler(u_char *entry, void *arg)
{
	proc_entry_ptr proc;

	switch (*entry) {
	case MPCT_ENTRY_PROCESSOR:
		proc = (proc_entry_ptr)entry;
		if (proc->cpu_flags & PROCENTRY_FLAG_EN &&
		    proc->apic_id < MAX_LAPIC_ID && mp_ncpus < MAXCPU) {
#ifdef SMP
			mp_ncpus++;
			mp_maxid = mp_ncpus - 1;
#endif
			max_apic_id = max(max_apic_id, proc->apic_id);
		}
		break;
	}
}


static void
mptable_setup_cpus_handler(u_char *entry, void *arg)
{
	proc_entry_ptr proc;
	u_int *cpu_mask;

	switch (*entry) {
	case MPCT_ENTRY_PROCESSOR:
		proc = (proc_entry_ptr)entry;
		if (proc->cpu_flags & PROCENTRY_FLAG_EN) {
			lapic_create(proc->apic_id, proc->cpu_flags &
			    PROCENTRY_FLAG_BP);
			if (proc->apic_id < MAX_LAPIC_ID) {
				cpu_mask = (u_int *)arg;
				*cpu_mask |= (1ul << proc->apic_id);
			}
		}
		break;
	}
}

static void
mptable_count_items_handler(u_char *entry, void *arg __unused)
{
	io_apic_entry_ptr apic;
	bus_entry_ptr bus;

	switch (*entry) {
	case MPCT_ENTRY_BUS:
		bus = (bus_entry_ptr)entry;
		mptable_nbusses++;
		if (bus->bus_id > mptable_maxbusid)
			mptable_maxbusid = bus->bus_id;
		break;
	case MPCT_ENTRY_IOAPIC:
		apic = (io_apic_entry_ptr)entry;
		if (apic->apic_flags & IOAPICENTRY_FLAG_EN)
			mptable_nioapics++;
		break;
	}
}

/*
 * Count items in the table.
 */
static void
mptable_count_items(void)
{

	/* Is this a pre-defined config? */
	if (mpfps->config_type != 0) {
		mptable_nioapics = 1;
		switch (mpfps->config_type) {
		case 1:
		case 2:
		case 3:
		case 4:
			mptable_nbusses = 1;
			break;
		case 5:
		case 6:
		case 7:
			mptable_nbusses = 2;
			break;
		default:
			panic("Unknown pre-defined MP Table config type %d",
			    mpfps->config_type);
		}
		mptable_maxbusid = mptable_nbusses - 1;
	} else
		mptable_walk_table(mptable_count_items_handler, NULL);
}

/*
 * Add a bus or I/O APIC from an entry in the table.
 */
static void
mptable_parse_apics_and_busses_handler(u_char *entry, void *arg __unused)
{
	io_apic_entry_ptr apic;
	bus_entry_ptr bus;
	enum busTypes bus_type;
	int i;


	switch (*entry) {
	case MPCT_ENTRY_BUS:
		bus = (bus_entry_ptr)entry;
		bus_type = lookup_bus_type(bus->bus_type);
		if (bus_type == UNKNOWN_BUSTYPE) {
			printf("MPTable: Unknown bus %d type \"", bus->bus_id);
			for (i = 0; i < 6; i++)
				printf("%c", bus->bus_type[i]);
			printf("\"\n");
		}
		busses[bus->bus_id].bus_id = bus->bus_id;
		busses[bus->bus_id].bus_type = bus_type;
		break;
	case MPCT_ENTRY_IOAPIC:
		apic = (io_apic_entry_ptr)entry;
		if (!(apic->apic_flags & IOAPICENTRY_FLAG_EN))
			break;
		if (apic->apic_id > IOAPIC_MAX_ID)
			panic("%s: I/O APIC ID %d too high", __func__,
			    apic->apic_id);
		if (ioapics[apic->apic_id] != NULL)
			panic("%s: Double APIC ID %d", __func__,
			    apic->apic_id);
		ioapics[apic->apic_id] = ioapic_create(apic->apic_address,
		    apic->apic_id, -1);
		break;
	default:
		break;
	}
}

/*
 * Enumerate I/O APIC's and buses.
 */
static void
mptable_parse_apics_and_busses(void)
{

	/* Is this a pre-defined config? */
	if (mpfps->config_type != 0) {
		ioapics[2] = ioapic_create(DEFAULT_IO_APIC_BASE, 2, 0);
		busses[0].bus_id = 0;
		busses[0].bus_type = default_data[mpfps->config_type - 1][2];
		if (mptable_nbusses > 1) {
			busses[1].bus_id = 1;
			busses[1].bus_type =
			    default_data[mpfps->config_type - 1][4];
		}
	} else
		mptable_walk_table(mptable_parse_apics_and_busses_handler,
		    NULL);
}

/*
 * Determine conforming polarity for a given bus type.
 */
static enum intr_polarity
conforming_polarity(u_char src_bus, u_char src_bus_irq)
{

	KASSERT(src_bus <= mptable_maxbusid, ("bus id %d too large", src_bus));
	switch (busses[src_bus].bus_type) {
	case ISA:
	case EISA:
		return (INTR_POLARITY_HIGH);
	case PCI:
		return (INTR_POLARITY_LOW);
	default:
		panic("%s: unknown bus type %d", __func__,
		    busses[src_bus].bus_type);
	}
}

/*
 * Determine conforming trigger for a given bus type.
 */
static enum intr_trigger
conforming_trigger(u_char src_bus, u_char src_bus_irq)
{

	KASSERT(src_bus <= mptable_maxbusid, ("bus id %d too large", src_bus));
	switch (busses[src_bus].bus_type) {
	case ISA:
		if (elcr_found)
			return (elcr_read_trigger(src_bus_irq));
		else
			return (INTR_TRIGGER_EDGE);
	case PCI:
		return (INTR_TRIGGER_LEVEL);

	case EISA:
		KASSERT(src_bus_irq < 16, ("Invalid EISA IRQ %d", src_bus_irq));
		KASSERT(elcr_found, ("Missing ELCR"));
		return (elcr_read_trigger(src_bus_irq));

	default:
		panic("%s: unknown bus type %d", __func__,
		    busses[src_bus].bus_type);
	}
}

static enum intr_polarity
intentry_polarity(int_entry_ptr intr)
{

	switch (intr->int_flags & INTENTRY_FLAGS_POLARITY) {
	case INTENTRY_FLAGS_POLARITY_CONFORM:
		return (conforming_polarity(intr->src_bus_id,
			    intr->src_bus_irq));
	case INTENTRY_FLAGS_POLARITY_ACTIVEHI:
		return (INTR_POLARITY_HIGH);
	case INTENTRY_FLAGS_POLARITY_ACTIVELO:
		return (INTR_POLARITY_LOW);
	default:
		panic("Bogus interrupt flags");
	}
}

static enum intr_trigger
intentry_trigger(int_entry_ptr intr)
{

	switch (intr->int_flags & INTENTRY_FLAGS_TRIGGER) {
	case INTENTRY_FLAGS_TRIGGER_CONFORM:
		return (conforming_trigger(intr->src_bus_id,
			    intr->src_bus_irq));
	case INTENTRY_FLAGS_TRIGGER_EDGE:
		return (INTR_TRIGGER_EDGE);
	case INTENTRY_FLAGS_TRIGGER_LEVEL:
		return (INTR_TRIGGER_LEVEL);
	default:
		panic("Bogus interrupt flags");
	}
}

/*
 * Parse an interrupt entry for an I/O interrupt routed to a pin on an I/O APIC.
 */
static void
mptable_parse_io_int(int_entry_ptr intr)
{
	void *ioapic;
	u_int pin, apic_id;

	apic_id = intr->dst_apic_id;
	if (intr->dst_apic_id == 0xff) {
		/*
		 * An APIC ID of 0xff means that the interrupt is connected
		 * to the specified pin on all I/O APICs in the system.  If
		 * there is only one I/O APIC, then use that APIC to route
		 * the interrupts.  If there is more than one I/O APIC, then
		 * punt.
		 */
		if (mptable_nioapics == 1) {
			apic_id = 0;
			while (ioapics[apic_id] == NULL)
				apic_id++;
		} else {
			printf(
			"MPTable: Ignoring global interrupt entry for pin %d\n",
			    intr->dst_apic_int);
			return;
		}
	}
	if (apic_id > IOAPIC_MAX_ID) {
		printf("MPTable: Ignoring interrupt entry for ioapic%d\n",
		    intr->dst_apic_id);
		return;
	}
	ioapic = ioapics[apic_id];
	if (ioapic == NULL) {
		printf(
	"MPTable: Ignoring interrupt entry for missing ioapic%d\n",
		    apic_id);
		return;
	}
	pin = intr->dst_apic_int;
	switch (intr->int_type) {
	case INTENTRY_TYPE_INT:
		switch (busses[intr->src_bus_id].bus_type) {
		case NOBUS:
			panic("interrupt from missing bus");
		case ISA:
		case EISA:
			if (busses[intr->src_bus_id].bus_type == ISA)
				ioapic_set_bus(ioapic, pin, APIC_BUS_ISA);
			else
				ioapic_set_bus(ioapic, pin, APIC_BUS_EISA);
			if (intr->src_bus_irq == pin)
				break;
			ioapic_remap_vector(ioapic, pin, intr->src_bus_irq);
			if (ioapic_get_vector(ioapic, intr->src_bus_irq) ==
			    intr->src_bus_irq)
				ioapic_disable_pin(ioapic, intr->src_bus_irq);
			break;
		case PCI:
			ioapic_set_bus(ioapic, pin, APIC_BUS_PCI);
			break;
		default:
			ioapic_set_bus(ioapic, pin, APIC_BUS_UNKNOWN);
			break;
		}
		break;
	case INTENTRY_TYPE_NMI:
		ioapic_set_nmi(ioapic, pin);
		break;
	case INTENTRY_TYPE_SMI:
		ioapic_set_smi(ioapic, pin);
		break;
	case INTENTRY_TYPE_EXTINT:
		ioapic_set_extint(ioapic, pin);
		break;
	default:
		panic("%s: invalid interrupt entry type %d\n", __func__,
		    intr->int_type);
	}
	if (intr->int_type == INTENTRY_TYPE_INT ||
	    (intr->int_flags & INTENTRY_FLAGS_TRIGGER) !=
	    INTENTRY_FLAGS_TRIGGER_CONFORM)
		ioapic_set_triggermode(ioapic, pin, intentry_trigger(intr));
	if (intr->int_type == INTENTRY_TYPE_INT ||
	    (intr->int_flags & INTENTRY_FLAGS_POLARITY) !=
	    INTENTRY_FLAGS_POLARITY_CONFORM)
		ioapic_set_polarity(ioapic, pin, intentry_polarity(intr));
}

/*
 * Parse an interrupt entry for a local APIC LVT pin.
 */
static void
mptable_parse_local_int(int_entry_ptr intr)
{
	u_int apic_id, pin;

	if (intr->dst_apic_id == 0xff)
		apic_id = APIC_ID_ALL;
	else
		apic_id = intr->dst_apic_id;
	if (intr->dst_apic_int == 0)
		pin = APIC_LVT_LINT0;
	else
		pin = APIC_LVT_LINT1;
	switch (intr->int_type) {
	case INTENTRY_TYPE_INT:
#if 1
		printf(
	"MPTable: Ignoring vectored local interrupt for LINTIN%d vector %d\n",
		    intr->dst_apic_int, intr->src_bus_irq);
		return;
#else
		lapic_set_lvt_mode(apic_id, pin, APIC_LVT_DM_FIXED);
		break;
#endif
	case INTENTRY_TYPE_NMI:
		lapic_set_lvt_mode(apic_id, pin, APIC_LVT_DM_NMI);
		break;
	case INTENTRY_TYPE_SMI:
		lapic_set_lvt_mode(apic_id, pin, APIC_LVT_DM_SMI);
		break;
	case INTENTRY_TYPE_EXTINT:
		lapic_set_lvt_mode(apic_id, pin, APIC_LVT_DM_EXTINT);
		break;
	default:
		panic("%s: invalid interrupt entry type %d\n", __func__,
		    intr->int_type);
	}
	if ((intr->int_flags & INTENTRY_FLAGS_TRIGGER) !=
	    INTENTRY_FLAGS_TRIGGER_CONFORM)
		lapic_set_lvt_triggermode(apic_id, pin,
		    intentry_trigger(intr));
	if ((intr->int_flags & INTENTRY_FLAGS_POLARITY) !=
	    INTENTRY_FLAGS_POLARITY_CONFORM)
		lapic_set_lvt_polarity(apic_id, pin, intentry_polarity(intr));
}

/*
 * Parse interrupt entries.
 */
static void
mptable_parse_ints_handler(u_char *entry, void *arg __unused)
{
	int_entry_ptr intr;

	intr = (int_entry_ptr)entry;
	switch (*entry) {
	case MPCT_ENTRY_INT:
		mptable_parse_io_int(intr);
		break;
	case MPCT_ENTRY_LOCAL_INT:
		mptable_parse_local_int(intr);
		break;
	}
}

/*
 * Configure interrupt pins for a default configuration.  For details see
 * Table 5-2 in Section 5 of the MP Table specification.
 */
static void
mptable_parse_default_config_ints(void)
{
	struct INTENTRY entry;
	int pin;

	/*
	 * All default configs route IRQs from bus 0 to the first 16 pins
	 * of the first I/O APIC with an APIC ID of 2.
	 */
	entry.type = MPCT_ENTRY_INT;
	entry.int_flags = INTENTRY_FLAGS_POLARITY_CONFORM |
	    INTENTRY_FLAGS_TRIGGER_CONFORM;
	entry.src_bus_id = 0;
	entry.dst_apic_id = 2;

	/* Run through all 16 pins. */
	for (pin = 0; pin < 16; pin++) {
		entry.dst_apic_int = pin;
		switch (pin) {
		case 0:
			/* Pin 0 is an ExtINT pin. */
			entry.int_type = INTENTRY_TYPE_EXTINT;
			break;
		case 2:
			/* IRQ 0 is routed to pin 2. */
			entry.int_type = INTENTRY_TYPE_INT;
			entry.src_bus_irq = 0;
			break;
		default:
			/* All other pins are identity mapped. */
			entry.int_type = INTENTRY_TYPE_INT;
			entry.src_bus_irq = pin;
			break;
		}
		mptable_parse_io_int(&entry);
	}

	/* Certain configs disable certain pins. */
	if (mpfps->config_type == 7)
		ioapic_disable_pin(ioapics[2], 0);
	if (mpfps->config_type == 2) {
		ioapic_disable_pin(ioapics[2], 2);
		ioapic_disable_pin(ioapics[2], 13);
	}
}

/*
 * Configure the interrupt pins
 */
static void
mptable_parse_ints(void)
{

	/* Is this a pre-defined config? */
	if (mpfps->config_type != 0) {
		/* Configure LINT pins. */
		lapic_set_lvt_mode(APIC_ID_ALL, APIC_LVT_LINT0,
		    APIC_LVT_DM_EXTINT);
		lapic_set_lvt_mode(APIC_ID_ALL, APIC_LVT_LINT1, APIC_LVT_DM_NMI);

		/* Configure I/O APIC pins. */
		mptable_parse_default_config_ints();
	} else
		mptable_walk_table(mptable_parse_ints_handler, NULL);
}

#ifdef MPTABLE_FORCE_HTT
/*
 * Perform a hyperthreading "fix-up" to enumerate any logical CPU's
 * that aren't already listed in the table.
 *
 * XXX: We assume that all of the physical CPUs in the
 * system have the same number of logical CPUs.
 *
 * XXX: We assume that APIC ID's are allocated such that
 * the APIC ID's for a physical processor are aligned
 * with the number of logical CPU's in the processor.
 */
static void
mptable_hyperthread_fixup(u_int id_mask)
{
	u_int i, id, logical_cpus;

	/* Nothing to do if there is no HTT support. */
	if ((cpu_feature & CPUID_HTT) == 0)
		return;
	logical_cpus = (cpu_procinfo & CPUID_HTT_CORES) >> 16;
	if (logical_cpus <= 1)
		return;

	/*
	 * For each APIC ID of a CPU that is set in the mask,
	 * scan the other candidate APIC ID's for this
	 * physical processor.  If any of those ID's are
	 * already in the table, then kill the fixup.
	 */
	for (id = 0; id <= MAX_LAPIC_ID; id++) {
		if ((id_mask & 1 << id) == 0)
			continue;
		/* First, make sure we are on a logical_cpus boundary. */
		if (id % logical_cpus != 0)
			return;
		for (i = id + 1; i < id + logical_cpus; i++)
			if ((id_mask & 1 << i) != 0)
				return;
	}

	/*
	 * Ok, the ID's checked out, so perform the fixup by
	 * adding the logical CPUs.
	 */
	while ((id = ffs(id_mask)) != 0) {
		id--;
		for (i = id + 1; i < id + logical_cpus; i++) {
			if (bootverbose)
				printf(
			"MPTable: Adding logical CPU %d from main CPU %d\n",
				    i, id);
			lapic_create(i, 0);
		}
		id_mask &= ~(1 << id);
	}
}
#endif /* MPTABLE_FORCE_HTT */

/*
 * Support code for routing PCI interrupts using the MP Table.
 */
static void
mptable_pci_setup(void)
{
	int i;

	/*
	 * Find the first pci bus and call it 0.  Panic if pci0 is not
	 * bus zero and there are multiple PCI buses.
	 */
	for (i = 0; i <= mptable_maxbusid; i++)
		if (busses[i].bus_type == PCI) {
			if (pci0 == -1)
				pci0 = i;
			else if (pci0 != 0)
				panic(
		"MPTable contains multiple PCI buses but no PCI bus 0");
		}
}

static void
mptable_pci_probe_table_handler(u_char *entry, void *arg)
{
	struct pci_probe_table_args *args;
	int_entry_ptr intr;

	if (*entry != MPCT_ENTRY_INT)
		return;
	intr = (int_entry_ptr)entry;
	args = (struct pci_probe_table_args *)arg;
	KASSERT(args->bus <= mptable_maxbusid,
	    ("bus %d is too big", args->bus));
	KASSERT(busses[args->bus].bus_type == PCI, ("probing for non-PCI bus"));
	if (intr->src_bus_id == args->bus)
		args->found = 1;
}

int
mptable_pci_probe_table(int bus)
{
	struct pci_probe_table_args args;

	if (bus < 0)
		return (EINVAL);
	if (mpct == NULL || pci0 == -1 || pci0 + bus > mptable_maxbusid)
		return (ENXIO);
	if (busses[pci0 + bus].bus_type != PCI)
		return (ENXIO);
	args.bus = pci0 + bus;
	args.found = 0;
	mptable_walk_table(mptable_pci_probe_table_handler, &args);
	if (args.found == 0)
		return (ENXIO);
	return (0);
}

static void
mptable_pci_route_interrupt_handler(u_char *entry, void *arg)
{
	struct pci_route_interrupt_args *args;
	int_entry_ptr intr;
	int vector;

	if (*entry != MPCT_ENTRY_INT)
		return;
	intr = (int_entry_ptr)entry;
	args = (struct pci_route_interrupt_args *)arg;
	if (intr->src_bus_id != args->bus || intr->src_bus_irq != args->irq)
		return;

	/* Make sure the APIC maps to a known APIC. */
	KASSERT(ioapics[intr->dst_apic_id] != NULL,
	    ("No I/O APIC %d to route interrupt to", intr->dst_apic_id));

	/*
	 * Look up the vector for this APIC / pin combination.  If we
	 * have previously matched an entry for this PCI IRQ but it
	 * has the same vector as this entry, just return.  Otherwise,
	 * we use the vector for this APIC / pin combination.
	 */
	vector = ioapic_get_vector(ioapics[intr->dst_apic_id],
	    intr->dst_apic_int);
	if (args->vector == vector)
		return;
	KASSERT(args->vector == -1,
	    ("Multiple IRQs for PCI interrupt %d.%d.INT%c: %d and %d\n",
	    args->bus, args->irq >> 2, 'A' + (args->irq & 0x3), args->vector,
	    vector));
	args->vector = vector;
}

int
mptable_pci_route_interrupt(device_t pcib, device_t dev, int pin)
{
	struct pci_route_interrupt_args args;
	int slot;

	/* Like ACPI, pin numbers are 0-3, not 1-4. */
	pin--;
	KASSERT(pci0 != -1, ("do not know how to route PCI interrupts"));
	args.bus = pci_get_bus(dev) + pci0;
	slot = pci_get_slot(dev);

	/*
	 * PCI interrupt entries in the MP Table encode both the slot and
	 * pin into the IRQ with the pin being the two least significant
	 * bits, the slot being the next five bits, and the most significant
	 * bit being reserved.
	 */
	args.irq = slot << 2 | pin;
	args.vector = -1;
	mptable_walk_table(mptable_pci_route_interrupt_handler, &args);
	if (args.vector < 0) {
		device_printf(pcib, "unable to route slot %d INT%c\n", slot,
		    'A' + pin);
		return (PCI_INVALID_IRQ);
	}
	if (bootverbose)
		device_printf(pcib, "slot %d INT%c routed to irq %d\n", slot,
		    'A' + pin, args.vector);
	return (args.vector);
}

#ifdef NEW_PCIB
struct host_res_args {
	struct mptable_hostb_softc *sc;
	device_t dev;
	u_char	bus;
};

/*
 * Initialize a Host-PCI bridge so it can restrict resource allocation
 * requests to the resources it actually decodes according to MP
 * config table extended entries.
 */
static void
mptable_host_res_handler(ext_entry_ptr entry, void *arg)
{
	struct host_res_args *args;
	cbasm_entry_ptr cbasm;
	sas_entry_ptr sas;
	const char *name;
	uint64_t start, end;
	int error, *flagp, flags, type;

	args = arg;
	switch (entry->type) {
	case MPCT_EXTENTRY_SAS:
		sas = (sas_entry_ptr)entry;
		if (sas->bus_id != args->bus)
			break;
		switch (sas->address_type) {
		case SASENTRY_TYPE_IO:
			type = SYS_RES_IOPORT;
			flags = 0;
			break;
		case SASENTRY_TYPE_MEMORY:
			type = SYS_RES_MEMORY;
			flags = 0;
			break;
		case SASENTRY_TYPE_PREFETCH:
			type = SYS_RES_MEMORY;
			flags = RF_PREFETCHABLE;
			break;
		default:
			printf(
	    "MPTable: Unknown systems address space type for bus %u: %d\n",
			    sas->bus_id, sas->address_type);
			return;
		}
		start = sas->address_base;
		end = sas->address_base + sas->address_length - 1;
#ifdef __i386__
		if (start > ULONG_MAX) {
			device_printf(args->dev,
			    "Ignoring %d range above 4GB (%#jx-%#jx)\n",
			    type, (uintmax_t)start, (uintmax_t)end);
			break;
		}
		if (end > ULONG_MAX) {
			device_printf(args->dev,
		    "Truncating end of %d range above 4GB (%#jx-%#jx)\n",
			    type, (uintmax_t)start, (uintmax_t)end);
			end = ULONG_MAX;
		}
#endif
		error = pcib_host_res_decodes(&args->sc->sc_host_res, type,
		    start, end, flags);
		if (error)
			panic("Failed to manage %d range (%#jx-%#jx): %d",
			    type, (uintmax_t)start, (uintmax_t)end, error);
		break;
	case MPCT_EXTENTRY_CBASM:
		cbasm = (cbasm_entry_ptr)entry;
		if (cbasm->bus_id != args->bus)
			break;
		switch (cbasm->predefined_range) {
		case CBASMENTRY_RANGE_ISA_IO:
			flagp = &args->sc->sc_decodes_isa_io;
			name = "ISA I/O";
			break;
		case CBASMENTRY_RANGE_VGA_IO:
			flagp = &args->sc->sc_decodes_vga_io;
			name = "VGA I/O";
			break;
		default:
			printf(
    "MPTable: Unknown compatiblity address space range for bus %u: %d\n",
			    cbasm->bus_id, cbasm->predefined_range);
			return;
		}
		if (*flagp != 0)
			printf(
		    "MPTable: Duplicate compatibility %s range for bus %u\n",
			    name, cbasm->bus_id);
		switch (cbasm->address_mod) {
		case CBASMENTRY_ADDRESS_MOD_ADD:
			*flagp = 1;
			if (bootverbose)
				device_printf(args->dev, "decoding %s ports\n",
				    name);
			break;
		case CBASMENTRY_ADDRESS_MOD_SUBTRACT:
			*flagp = -1;
			if (bootverbose)
				device_printf(args->dev,
				    "not decoding %s ports\n", name);
			break;
		default:
			printf(
	    "MPTable: Unknown compatibility address space modifier: %u\n",
			    cbasm->address_mod);
			break;
		}
		break;
	}
}

void
mptable_pci_host_res_init(device_t pcib)
{
	struct host_res_args args;

	KASSERT(pci0 != -1, ("do not know how to map PCI bus IDs"));
	args.bus = pci_get_bus(pcib) + pci0;
	args.dev = pcib;
	args.sc = device_get_softc(pcib);
	if (pcib_host_res_init(pcib, &args.sc->sc_host_res) != 0)
		panic("failed to init hostb resources");
	mptable_walk_extended_table(mptable_host_res_handler, &args);
}
#endif
