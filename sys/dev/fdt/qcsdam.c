/*	$OpenBSD: qcsdam.c,v 1.1 2023/07/22 22:43:53 patrick Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct qcsdam_softc {
	struct device		sc_dev;
	spmi_tag_t		sc_tag;
	int8_t			sc_sid;
	uint16_t		sc_addr;

	struct nvmem_device	sc_nd;
};

int qcsdam_match(struct device *, void *, void *);
void qcsdam_attach(struct device *, struct device *, void *);

const struct cfattach	qcsdam_ca = {
	sizeof (struct qcsdam_softc), qcsdam_match, qcsdam_attach
};

struct cfdriver qcsdam_cd = {
	NULL, "qcsdam", DV_DULL
};

int	qcsdam_nvmem_read(void *, bus_addr_t, void *, bus_size_t);
int	qcsdam_nvmem_write(void *, bus_addr_t, const void *, bus_size_t);

int
qcsdam_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *saa = aux;

	return OF_is_compatible(saa->sa_node, "qcom,spmi-sdam");
}

void
qcsdam_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcsdam_softc *sc = (struct qcsdam_softc *)self;
	struct spmi_attach_args *saa = aux;
	uint32_t reg;

	if (OF_getpropintarray(saa->sa_node, "reg",
	    &reg, sizeof(reg)) != sizeof(reg)) {
		printf(": can't find registers\n");
		return;
	}

	sc->sc_tag = saa->sa_tag;
	sc->sc_sid = saa->sa_sid;
	sc->sc_addr = reg;

	printf("\n");

	sc->sc_nd.nd_node = saa->sa_node;
	sc->sc_nd.nd_cookie = sc;
	sc->sc_nd.nd_read = qcsdam_nvmem_read;
	sc->sc_nd.nd_write = qcsdam_nvmem_write;
	nvmem_register(&sc->sc_nd);
}

int
qcsdam_nvmem_read(void *cookie, bus_addr_t addr, void *data, bus_size_t size)
{
	struct qcsdam_softc *sc = cookie;
	int error;

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    sc->sc_addr + addr, data, size);
	if (error) {
		printf("%s: error reading NVMEM\n", sc->sc_dev.dv_xname);
		return error;
	}

	return 0;
}

int
qcsdam_nvmem_write(void *cookie, bus_addr_t addr, const void *data,
    bus_size_t size)
{
	struct qcsdam_softc *sc = cookie;
	int error;

	error = spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    sc->sc_addr + addr, data, size);
	if (error) {
		printf("%s: error reading NVMEM\n", sc->sc_dev.dv_xname);
		return error;
	}

	return 0;
}
