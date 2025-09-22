/*	$OpenBSD: acpihve.c,v 1.4 2022/04/06 18:59:27 naddy Exp $	*/

/*
 * Copyright (c) 2017 Jonathan Gray <jsg@openbsd.org>
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

int	 acpihve_match(struct device *, void *, void *);
void	 acpihve_attach(struct device *, struct device *, void *);

struct acpi_oem0 {
	struct acpi_table_header	hdr;
	uint32_t			entropy[16];
} __packed;

struct acpihve_softc {
	struct device			sc_dev;
};

const struct cfattach acpihve_ca = {
	sizeof(struct acpihve_softc), acpihve_match, acpihve_attach
};

struct cfdriver acpihve_cd = {
	NULL, "acpihve", DV_DULL
};

int	 acpihve_attached;

int
acpihve_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_table_header *hdr;

	/*
	 * If we do not have a table, it is not us; attach only once
	 */
	if (acpihve_attached || aaa->aaa_table == NULL)
		return (0);

	hdr = (struct acpi_table_header *)aaa->aaa_table;
	if (memcmp(hdr->signature, "OEM0", 4) != 0 ||
	    memcmp(hdr->oemid, "VRTUAL", 6) != 0 ||
	    memcmp(hdr->oemtableid, "MICROSFT", 8) != 0)
		return (0);

	return (1);
}

void
acpihve_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct acpi_oem0 *oem0 = (struct acpi_oem0 *)aaa->aaa_table;
	int i;

	acpihve_attached++;

	if (oem0->hdr.length != sizeof(*oem0)) {
		printf(": unexpected table length %u\n", oem0->hdr.length);
		return;
	}

	/* 64 bytes of entropy from OEM0 table */
	for (i = 0; i < nitems(oem0->entropy); i++)
		enqueue_randomness(oem0->entropy[i]);

	printf("\n");
}
