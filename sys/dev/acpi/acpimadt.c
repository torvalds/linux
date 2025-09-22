/* $OpenBSD: acpimadt.c,v 1.41 2025/09/16 12:18:10 hshoexer Exp $ */
/*
 * Copyright (c) 2006 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/apicvar.h>
#include <machine/cpuvar.h>
#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <machine/i8259.h>
#include <machine/i82093reg.h>
#include <machine/i82093var.h>
#include <machine/i82489reg.h>
#include <machine/i82489var.h>

#include <machine/mpbiosvar.h>

#include "ioapic.h"

int acpimadt_match(struct device *, void *, void *);
void acpimadt_attach(struct device *, struct device *, void *);

const struct cfattach acpimadt_ca = {
	sizeof(struct device), acpimadt_match, acpimadt_attach
};

struct cfdriver acpimadt_cd = {
	NULL, "acpimadt", DV_DULL, CD_COCOVM
};

int acpimadt_validate(struct acpi_madt *);
int acpimadt_cfg_intr(int, uint32_t *);
int acpimadt_print(void *, const char *);

int
acpimadt_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_table_header *hdr;

	/*
	 * If we do not have a table, it is not us
	 */
	if (aaa->aaa_table == NULL)
		return (0);

	/*
	 * If it is an MADT table, we can attach
	 */
	hdr = (struct acpi_table_header *)aaa->aaa_table;
	if (memcmp(hdr->signature, MADT_SIG, sizeof(MADT_SIG) - 1) != 0)
		return (0);

	return (1);
}

int
acpimadt_validate(struct acpi_madt *madt)
{
	caddr_t addr = (caddr_t)(madt + 1);

	while (addr < (caddr_t)madt + madt->hdr.length) {
		union acpi_madt_entry *entry = (union acpi_madt_entry *)addr;
		uint8_t length = entry->madt_lapic.length;

		if (length < 2)
			return (0);

		if (addr + length > (caddr_t)madt + madt->hdr.length)
			return (0);

		switch (entry->madt_lapic.apic_type) {
		case ACPI_MADT_LAPIC:
			if (length != sizeof(entry->madt_lapic))
				return (0);
			break;
		case ACPI_MADT_IOAPIC:
			if (length != sizeof(entry->madt_ioapic))
				return (0);
			break;
		case ACPI_MADT_OVERRIDE:
			if (length != sizeof(entry->madt_override))
				return (0);
			break;
		case ACPI_MADT_NMI:
			if (length != sizeof(entry->madt_nmi))
				return (0);
			break;
		case ACPI_MADT_LAPIC_NMI:
			if (length != sizeof(entry->madt_lapic_nmi))
				return (0);
			break;
		case ACPI_MADT_LAPIC_OVERRIDE:
			if (length != sizeof(entry->madt_lapic_override))
				return (0);
			break;
		case ACPI_MADT_IO_SAPIC:
			if (length != sizeof(entry->madt_io_sapic))
				return (0);
			break;
		case ACPI_MADT_LOCAL_SAPIC:
			if (length != sizeof(entry->madt_local_sapic))
				return (0);
			break;
		case ACPI_MADT_PLATFORM_INT:
			if (length != sizeof(entry->madt_platform_int))
				return (0);
			break;
		case ACPI_MADT_X2APIC:
			if (length != sizeof(entry->madt_x2apic))
				return (0);
			break;
		case ACPI_MADT_X2APIC_NMI:
			if (length != sizeof(entry->madt_x2apic_nmi))
				return (0);
			break;
		}

		addr += length;
	}

	return (1);
}

struct mp_bus acpimadt_busses[256];
struct mp_bus acpimadt_isa_bus;

int
acpimadt_cfg_intr(int flags, uint32_t *redir)
{
	int mpspo = (flags >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK;
	int mpstrig = (flags >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK;

	*redir &= ~IOAPIC_REDLO_DEL_MASK;
	switch (mpspo) {
	case MPS_INTPO_DEF:
	case MPS_INTPO_ACTHI:
		*redir &= ~IOAPIC_REDLO_ACTLO;
		break;
	case MPS_INTPO_ACTLO:
		*redir |= IOAPIC_REDLO_ACTLO;
		break;
	default:
		return (0);
	}

	*redir |= (IOAPIC_REDLO_DEL_LOPRI << IOAPIC_REDLO_DEL_SHIFT);

	switch (mpstrig) {
	case MPS_INTTR_LEVEL:
		*redir |= IOAPIC_REDLO_LEVEL;
		break;
	case MPS_INTTR_DEF:
	case MPS_INTTR_EDGE:
		*redir &= ~IOAPIC_REDLO_LEVEL;
		break;
	default:
		return (0);
	}

	return (1);
}

static uint8_t lapic_map[256];

void
acpimadt_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_softc *acpi_sc = (struct acpi_softc *)parent;
	struct device *mainbus = parent->dv_parent->dv_parent;
	struct acpi_attach_args *aaa = aux;
	struct acpi_madt *madt = (struct acpi_madt *)aaa->aaa_table;
	caddr_t addr = (caddr_t)(madt + 1);
	struct aml_value arg;
	struct mp_intr_map *map;
	struct ioapic_softc *apic;
	int nlapic_nmis = 0;
	int pin;

	/* Do some sanity checks before committing to run in APIC mode. */
	if (!acpimadt_validate(madt)) {
		printf(": invalid, skipping\n");
		return;
	}

	printf(" addr 0x%x", madt->local_apic_address);
	if (madt->flags & ACPI_APIC_PCAT_COMPAT)
		printf(": PC-AT compat");
	printf("\n");

	/* Tell the BIOS we will be using APIC mode. */
	memset(&arg, 0, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = 1;

	aml_evalname(acpi_sc, NULL, "\\_PIC", 1, &arg, NULL);

	mp_busses = acpimadt_busses;
	mp_nbusses = nitems(acpimadt_busses);
	mp_isa_bus = &acpimadt_isa_bus;

	lapic_boot_init(madt->local_apic_address);

	/* 1st pass, get CPUs and IOAPICs */
	while (addr < (caddr_t)madt + madt->hdr.length) {
		union acpi_madt_entry *entry = (union acpi_madt_entry *)addr;
		struct cpu_attach_args caa;
		struct apic_attach_args aaa;

		switch (entry->madt_lapic.apic_type) {
		case ACPI_MADT_LAPIC_OVERRIDE:
			if (entry->madt_lapic_override.lapic_address !=
			    madt->local_apic_address) {
				printf("%s: ignored LAPIC override 0x%llx\n",
				    self->dv_xname,
				    entry->madt_lapic_override.lapic_address);
			}
			break;
		case ACPI_MADT_LAPIC:
			dprintf("%s: LAPIC: acpi_proc_id %x, apic_id %x, flags 0x%x\n",
			    self->dv_xname, entry->madt_lapic.acpi_proc_id,
			    entry->madt_lapic.apic_id,
			    entry->madt_lapic.flags);

			if ((entry->madt_lapic.flags & ACPI_PROC_ENABLE) == 0)
				break;

			lapic_map[entry->madt_lapic.acpi_proc_id] =
			    entry->madt_lapic.apic_id;

			memset(&caa, 0, sizeof(struct cpu_attach_args));
			if (lapic_cpu_number() == entry->madt_lapic.apic_id)
				caa.cpu_role = CPU_ROLE_BP;
			else {
				caa.cpu_role = CPU_ROLE_AP;
				ncpusfound++;
			}
			caa.caa_name = "cpu";
			caa.cpu_apicid = entry->madt_lapic.apic_id;
			caa.cpu_acpi_proc_id = entry->madt_lapic.acpi_proc_id;
#ifdef MULTIPROCESSOR
			caa.cpu_func = &mp_cpu_funcs;
#endif
#ifdef __i386__
			/*
			 * XXX utterly wrong.  These are the
			 * cpu_feature/cpu_id from the BSP cpu, now
			 * being given to another cpu.  This is
			 * bullshit.
			 */
			extern int cpu_id, cpu_feature;
			caa.cpu_signature = cpu_id;
			caa.feature_flags = cpu_feature;
#endif

			config_found(mainbus, &caa, acpimadt_print);
			break;
		case ACPI_MADT_IOAPIC:
			dprintf("%s: IOAPIC: acpi_ioapic_id %x, address 0x%x, global_int_base 0x%x\n",
			    self->dv_xname, entry->madt_ioapic.acpi_ioapic_id,
			    entry->madt_ioapic.address,
			    entry->madt_ioapic.global_int_base);

			memset(&aaa, 0, sizeof(struct apic_attach_args));
			aaa.aaa_name = "ioapic";
			aaa.apic_memt = acpi_sc->sc_memt;
			aaa.apic_id = entry->madt_ioapic.acpi_ioapic_id;
			aaa.apic_address = entry->madt_ioapic.address;
			aaa.apic_vecbase = entry->madt_ioapic.global_int_base;

			config_found(mainbus, &aaa, acpimadt_print);
			break;
		case ACPI_MADT_LAPIC_NMI:
			nlapic_nmis++;
			break;
		case ACPI_MADT_X2APIC:
			dprintf("%s: X2APIC: acpi_proc_uid %x, apic_id %x, flags 0x%x\n",
			    self->dv_xname, entry->madt_x2apic.acpi_proc_uid,
			    entry->madt_x2apic.apic_id,
			    entry->madt_x2apic.flags);

			if (entry->madt_x2apic.apic_id > 255 ||
			    (entry->madt_x2apic.flags & ACPI_PROC_ENABLE) == 0)
				break;

			memset(&caa, 0, sizeof(struct cpu_attach_args));
			if (lapic_cpu_number() == entry->madt_x2apic.apic_id)
				caa.cpu_role = CPU_ROLE_BP;
			else {
				caa.cpu_role = CPU_ROLE_AP;
				ncpusfound++;
			}
			caa.caa_name = "cpu";
			caa.cpu_apicid = entry->madt_x2apic.apic_id;
			caa.cpu_acpi_proc_id = entry->madt_x2apic.acpi_proc_uid;
#ifdef MULTIPROCESSOR
			caa.cpu_func = &mp_cpu_funcs;
#endif
#ifdef __i386__
			/*
			 * XXX utterly wrong.  These are the
			 * cpu_feature/cpu_id from the BSP cpu, now
			 * being given to another cpu.  This is
			 * bullshit.
			 */
			extern int cpu_id, cpu_feature;
			caa.cpu_signature = cpu_id;
			caa.feature_flags = cpu_feature;
#endif

			config_found(mainbus, &caa, acpimadt_print);
			break;
		}
		addr += entry->madt_lapic.length;
	}

	mp_intrs = mallocarray(nlapic_nmis, sizeof(struct mp_intr_map),
	    M_DEVBUF, M_NOWAIT);
	if (mp_intrs == NULL)
		return;

	/* 2nd pass, get interrupt overrides */
	addr = (caddr_t)(madt + 1);
	while (addr < (caddr_t)madt + madt->hdr.length) {
		union acpi_madt_entry *entry = (union acpi_madt_entry *)addr;

		switch (entry->madt_lapic.apic_type) {
		case ACPI_MADT_LAPIC:
		case ACPI_MADT_IOAPIC:
			break;

		case ACPI_MADT_OVERRIDE:
			dprintf("%s: OVERRIDE: bus %x, source %x, global_int %x, flags %x\n",
			    self->dv_xname, entry->madt_override.bus,
			    entry->madt_override.source,
			    entry->madt_override.global_int,
			    entry->madt_override.flags);

			pin = entry->madt_override.global_int;
			apic = ioapic_find_bybase(pin);

			map = malloc(sizeof(*map), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (map == NULL)
				return;

			map->ioapic = apic;
			map->ioapic_pin = pin - apic->sc_apic_vecbase;
			map->bus_pin = entry->madt_override.source;
			map->flags = entry->madt_override.flags;

			if (!acpimadt_cfg_intr(entry->madt_override.flags, &map->redir)) {
				printf("%s: bogus override for pin %d\n",
				    self->dv_xname, pin);
				free(map, M_DEVBUF, sizeof(*map));
				break;
			}

			map->ioapic_ih = APIC_INT_VIA_APIC |
			    ((apic->sc_apicid << APIC_INT_APIC_SHIFT) |
			    (pin << APIC_INT_PIN_SHIFT));

			apic->sc_pins[pin].ip_map = map;

			map->next = mp_isa_bus->mb_intrs;
			mp_isa_bus->mb_intrs = map;
			break;

		case ACPI_MADT_LAPIC_NMI:
			dprintf("%s: LAPIC_NMI: acpi_proc_id %x, local_apic_lint %x, flags %x\n",
			    self->dv_xname, entry->madt_lapic_nmi.acpi_proc_id,
			    entry->madt_lapic_nmi.local_apic_lint,
			    entry->madt_lapic_nmi.flags);

			pin = entry->madt_lapic_nmi.local_apic_lint;

			map = &mp_intrs[mp_nintrs++];
			memset(map, 0, sizeof *map);
			map->cpu_id = lapic_map[entry->madt_lapic_nmi.acpi_proc_id];
			map->ioapic_pin = pin;
			map->flags = entry->madt_lapic_nmi.flags;

			if ((pin != 0 && pin != 1) ||
			    !acpimadt_cfg_intr(entry->madt_lapic_nmi.flags, &map->redir)) {
				printf("%s: bogus nmi for apid %d\n",
				    self->dv_xname, map->cpu_id);
				mp_nintrs--;
				break;
			}

			map->redir &= ~IOAPIC_REDLO_DEL_MASK;
			map->redir |= (IOAPIC_REDLO_DEL_NMI << IOAPIC_REDLO_DEL_SHIFT);
			break;

		case ACPI_MADT_X2APIC:
		case ACPI_MADT_X2APIC_NMI:
			break;

		default:
			if (entry->madt_lapic.apic_type < ACPI_MADT_OEM_RSVD) {
				printf("%s: unknown apic structure type %x\n",
				    self->dv_xname,
				    entry->madt_lapic.apic_type);
			}
		}

		addr += entry->madt_lapic.length;
	}

	/*
	 * ISA interrupts are supposed to be identity mapped unless
	 * there is an override, in which case we will already have a
	 * mapping for the interrupt.
	 */
	for (pin = 0; pin < ICU_LEN; pin++) {
		/* Skip if we already have a mapping for this interrupt. */
		for (map = mp_isa_bus->mb_intrs; map != NULL; map = map->next)
			if (map->bus_pin == pin)
				break;
		if (map != NULL)
			continue;

		apic = ioapic_find_bybase(pin);

		map = malloc(sizeof(*map), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (map == NULL)
			return;

		map->ioapic = apic;
		map->ioapic_pin = pin;
		map->bus_pin = pin;
		map->redir = (IOAPIC_REDLO_DEL_LOPRI << IOAPIC_REDLO_DEL_SHIFT);

		map->ioapic_ih = APIC_INT_VIA_APIC |
		    ((apic->sc_apicid << APIC_INT_APIC_SHIFT) |
		    (pin << APIC_INT_PIN_SHIFT));

		apic->sc_pins[pin].ip_map = map;

		map->next = mp_isa_bus->mb_intrs;
		mp_isa_bus->mb_intrs = map;
	}
}

int
acpimadt_print(void *aux, const char *pnp)
{
	struct apic_attach_args *aaa = aux;

	if (pnp)
		printf("%s at %s:", aaa->aaa_name, pnp);

	return (UNCONF);
}
