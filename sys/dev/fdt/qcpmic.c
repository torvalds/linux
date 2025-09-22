/*	$OpenBSD: qcpmic.c,v 1.3 2025/01/03 14:13:55 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* PMIC Registers. */
#define PMIC_REV2	0x101
#define PMIC_REV3	0x102
#define PMIC_REV4	0x103
#define PMIC_TYPE	0x104
#define  PMIC_TYPE_VAL		0x51
#define PMIC_SUBTYPE	0x105
#define PMIC_FAB_ID	0x1f2

struct qcpmic_softc {
	struct device		sc_dev;
	int			sc_node;

	spmi_tag_t		sc_tag;
	int8_t			sc_sid;

	void			*sc_ih;

	struct timeout		sc_tick;
};

int	qcpmic_match(struct device *, void *, void *);
void	qcpmic_attach(struct device *, struct device *, void *);

const struct cfattach qcpmic_ca = {
	sizeof(struct qcpmic_softc), qcpmic_match, qcpmic_attach
};

struct cfdriver qcpmic_cd = {
	NULL, "qcpmic", DV_DULL
};

uint8_t	qcpmic_read(struct qcpmic_softc *, uint16_t);

int
qcpmic_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *saa = aux;

	return OF_is_compatible(saa->sa_node, "qcom,spmi-pmic");
}

void
qcpmic_attach(struct device *parent, struct device *self, void *aux)
{
	struct spmi_attach_args *saa = aux;
	struct qcpmic_softc *sc = (struct qcpmic_softc *)self;
	struct spmi_attach_args sa;
	char name[32];
	int node;

	sc->sc_node = saa->sa_node;
	sc->sc_tag = saa->sa_tag;
	sc->sc_sid = saa->sa_sid;

	if (qcpmic_read(sc, PMIC_TYPE) != PMIC_TYPE_VAL) {
		printf(": unknown PMIC type\n");
		return;
	}

	printf("\n");

	for (node = OF_child(saa->sa_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		memset(&sa, 0, sizeof(sa));
		sa.sa_tag = sc->sc_tag;
		sa.sa_sid = sc->sc_sid;
		sa.sa_name = name;
		sa.sa_node = node;
		config_found(self, &sa, NULL);
	}
}

uint8_t
qcpmic_read(struct qcpmic_softc *sc, uint16_t addr)
{
	uint8_t reg = 0;
	int err;

	err = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    addr, &reg, sizeof(reg));
	if (err)
		printf("%s: error (%u) reading 0x%x", sc->sc_dev.dv_xname,
		    err, addr);

	return reg;
}
