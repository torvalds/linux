/*	$OpenBSD: qcscm_acpi.c,v 1.1 2025/04/21 21:11:04 mglocker Exp $ */
/*
 * Copyright (c) 2025 Marcus Glocker <mglocker@openbsd.org>
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

void	qcscm_attach(struct device *, struct device *, void *);

struct qcscm_acpi_softc {
	struct device sc_dev;
};

int	qcscm_acpi_match(struct device *, void *, void *);
void	qcscm_acpi_attach(struct device *, struct device *, void *);

const struct cfattach qcscm_acpi_ca = {
	sizeof(struct qcscm_acpi_softc), qcscm_acpi_match, qcscm_acpi_attach,
	NULL, NULL
};

const char *qcscm_hids[] = {
	"QCOM04DD",
	NULL
};

int
qcscm_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, qcscm_hids, cf->cf_driver->cd_name);
}

void
qcscm_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	qcscm_attach(parent, self, aux);
}
