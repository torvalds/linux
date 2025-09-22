/* $OpenBSD: acpimcfg.c,v 1.6 2025/09/16 12:18:10 hshoexer Exp $ */
/*
 * Copyright (c) 2010 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/pci/pcivar.h>

int acpimcfg_match(struct device *, void *, void *);
void acpimcfg_attach(struct device *, struct device *, void *);

const struct cfattach acpimcfg_ca = {
	sizeof(struct device), acpimcfg_match, acpimcfg_attach
};

struct cfdriver acpimcfg_cd = {
	NULL, "acpimcfg", DV_DULL, CD_COCOVM
};

int
acpimcfg_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_table_header *hdr;

	/*
	 * If we do not have a table, it is not us
	 */
	if (aaa->aaa_table == NULL)
		return (0);

	/*
	 * If it is an MCFG table, we can attach
	 */
	hdr = (struct acpi_table_header *)aaa->aaa_table;
	if (memcmp(hdr->signature, MCFG_SIG, sizeof(MCFG_SIG) - 1) != 0)
		return (0);

	return (1);
}

void
acpimcfg_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_mcfg *mcfg = (struct acpi_mcfg *)aaa->aaa_table;
	caddr_t addr = (caddr_t)(mcfg + 1);

	printf("\n");

	while (addr < (caddr_t)mcfg + mcfg->hdr.length) {
		struct acpi_mcfg_entry *entry = (struct acpi_mcfg_entry *)addr;

		printf("%s: addr 0x%llx, bus %d-%d\n", self->dv_xname,
		    entry->base_address, entry->min_bus_number, entry->max_bus_number);

		pci_mcfg_init(aaa->aaa_memt, entry->base_address,
		     entry->segment, entry->min_bus_number, entry->max_bus_number);
		addr += sizeof(struct acpi_mcfg_entry);
	}
}
