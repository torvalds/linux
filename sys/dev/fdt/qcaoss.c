/*	$OpenBSD: qcaoss.c,v 1.1 2023/05/23 14:10:27 patrick Exp $	*/
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
#include <sys/malloc.h>
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define AOSS_DESC_MAGIC			0x0
#define AOSS_DESC_VERSION		0x4
#define AOSS_DESC_FEATURES		0x8
#define AOSS_DESC_UCORE_LINK_STATE	0xc
#define AOSS_DESC_UCORE_LINK_STATE_ACK	0x10
#define AOSS_DESC_UCORE_CH_STATE	0x14
#define AOSS_DESC_UCORE_CH_STATE_ACK	0x18
#define AOSS_DESC_UCORE_MBOX_SIZE	0x1c
#define AOSS_DESC_UCORE_MBOX_OFFSET	0x20
#define AOSS_DESC_MCORE_LINK_STATE	0x24
#define AOSS_DESC_MCORE_LINK_STATE_ACK	0x28
#define AOSS_DESC_MCORE_CH_STATE	0x2c
#define AOSS_DESC_MCORE_CH_STATE_ACK	0x30
#define AOSS_DESC_MCORE_MBOX_SIZE	0x34
#define AOSS_DESC_MCORE_MBOX_OFFSET	0x38

#define AOSS_MAGIC			0x4d41494c
#define AOSS_VERSION			1

#define AOSS_STATE_UP			(0xffffU << 0)
#define AOSS_STATE_DOWN			(0xffffU << 16)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct qcaoss_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	size_t			sc_offset;
	size_t			sc_size;

	struct mbox_channel	*sc_mc;
};

struct qcaoss_softc *qcaoss_sc;

int	qcaoss_match(struct device *, void *, void *);
void	qcaoss_attach(struct device *, struct device *, void *);

const struct cfattach qcaoss_ca = {
	sizeof (struct qcaoss_softc), qcaoss_match, qcaoss_attach
};

struct cfdriver qcaoss_cd = {
	NULL, "qcaoss", DV_DULL
};

int
qcaoss_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,aoss-qmp");
}

void
qcaoss_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcaoss_softc *sc = (struct qcaoss_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_mc = mbox_channel_idx(faa->fa_node, 0, NULL);
	if (sc->sc_mc == NULL) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		printf(": can't find mbox\n");
		return;
	}

	if (HREAD4(sc, AOSS_DESC_MAGIC) != AOSS_MAGIC ||
	    HREAD4(sc, AOSS_DESC_VERSION) != AOSS_VERSION) {
		printf(": invalid QMP info\n");
		return;
	}

	sc->sc_offset = HREAD4(sc, AOSS_DESC_MCORE_MBOX_OFFSET);
	sc->sc_size = HREAD4(sc, AOSS_DESC_MCORE_MBOX_SIZE);
	if (sc->sc_size == 0) {
		printf(": invalid mailbox size\n");
		return;
	}

	HWRITE4(sc, AOSS_DESC_UCORE_LINK_STATE_ACK,
	    HREAD4(sc, AOSS_DESC_UCORE_LINK_STATE));

	HWRITE4(sc, AOSS_DESC_MCORE_LINK_STATE, AOSS_STATE_UP);
	mbox_send(sc->sc_mc, NULL, 0);

	for (i = 1000; i > 0; i--) {
		if (HREAD4(sc, AOSS_DESC_MCORE_LINK_STATE_ACK) == AOSS_STATE_UP)
			break;
		delay(1000);
	}
	if (i == 0) {
		printf(": didn't get link state ack\n");
		return;
	}

	HWRITE4(sc, AOSS_DESC_MCORE_CH_STATE, AOSS_STATE_UP);
	mbox_send(sc->sc_mc, NULL, 0);

	for (i = 1000; i > 0; i--) {
		if (HREAD4(sc, AOSS_DESC_UCORE_CH_STATE) == AOSS_STATE_UP)
			break;
		delay(1000);
	}
	if (i == 0) {
		printf(": didn't get open channel\n");
		return;
	}

	HWRITE4(sc, AOSS_DESC_UCORE_CH_STATE_ACK, AOSS_STATE_UP);
	mbox_send(sc->sc_mc, NULL, 0);

	for (i = 1000; i > 0; i--) {
		if (HREAD4(sc, AOSS_DESC_MCORE_CH_STATE_ACK) == AOSS_STATE_UP)
			break;
		delay(1000);
	}
	if (i == 0) {
		printf(": didn't get channel ack\n");
		return;
	}

	printf("\n");

	qcaoss_sc = sc;
}

int
qcaoss_send(char *data, size_t len)
{
	struct qcaoss_softc *sc = qcaoss_sc;
	uint32_t reg;
	int i;

	if (sc == NULL)
		return ENXIO;

	if (data == NULL || sizeof(uint32_t) + len > sc->sc_size ||
	    (len % sizeof(uint32_t)) != 0)
		return EINVAL;

	/* Write data first, needs to be 32-bit access. */
	for (i = 0; i < len; i += 4) {
		memcpy(&reg, data + i, sizeof(reg));
		HWRITE4(sc, sc->sc_offset + sizeof(uint32_t) + i, reg);
	}

	/* Commit transaction by writing length. */
	HWRITE4(sc, sc->sc_offset, len);

	/* Assert it's stored and inform peer. */
	KASSERT(HREAD4(sc, sc->sc_offset) == len);
	mbox_send(sc->sc_mc, NULL, 0);

	for (i = 1000; i > 0; i--) {
		if (HREAD4(sc, sc->sc_offset) == 0)
			break;
		delay(1000);
	}
	if (i == 0) {
		printf("%s: timeout sending message\n", sc->sc_dev.dv_xname);
		HWRITE4(sc, sc->sc_offset, 0);
		return ETIMEDOUT;
	}

	return 0;
}
