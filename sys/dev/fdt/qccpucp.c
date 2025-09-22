/*	$OpenBSD: qccpucp.c,v 1.1 2024/11/16 21:17:54 tobhe Exp $	*/
/*
 * Copyright (c) 2024 Tobias Heider <tobhe@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define CPUCP_MAX_CHANNELS	3

/* Registers */
#define CPUCP_REG_CMD(i)	(0x104 + ((i) * 8))
#define CPUCP_MASK_CMD		0xffffffffffffffffULL
#define CPUCP_REG_RX_MAP	0x4000
#define CPUCP_REG_RX_STAT	0x4400
#define CPUCP_REG_RX_CLEAR	0x4800
#define CPUCP_REG_RX_EN		0x4C00

#define RXREAD8(sc, reg)						\
	(bus_space_read_8((sc)->sc_iot, (sc)->sc_rx_ioh, (reg)))
#define RXWRITE8(sc, reg, val)						\
	bus_space_write_8((sc)->sc_iot, (sc)->sc_rx_ioh, (reg), (val))

#define TXWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_tx_ioh, (reg), (val))

struct qccpucp_channel {
	struct qccpucp_softc	*ch_sc;

	int			 ch_idx;
};

struct qccpucp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_rx_ioh;
	bus_space_handle_t	sc_tx_ioh;

	void			*sc_ih;

	struct qccpucp_channel	sc_chans[CPUCP_MAX_CHANNELS];

	struct mbox_device	sc_md;
};

int	qccpucp_match(struct device *, void *, void *);
void	qccpucp_attach(struct device *, struct device *, void *);

const struct cfattach qccpucp_ca = {
	sizeof (struct qccpucp_softc), qccpucp_match, qccpucp_attach
};

struct cfdriver qccpucp_cd = {
	NULL, "qccpucp", DV_DULL
};

void	*qccpucp_channel(void *, uint32_t *, struct mbox_client *);
int	qccpucp_send(void *, const void *, size_t);

int
qccpucp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,x1e80100-cpucp-mbox");
}

void
qccpucp_attach(struct device *parent, struct device *self, void *aux)
{
	struct qccpucp_softc *sc = (struct qccpucp_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_rx_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_tx_ioh)) {
		printf(": can't map registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_rx_ioh,
		    faa->fa_reg[0].size);
		return;
	}

	RXWRITE8(sc, CPUCP_REG_RX_EN, 0);
	RXWRITE8(sc, CPUCP_REG_RX_CLEAR, 0);
	RXWRITE8(sc, CPUCP_REG_RX_MAP, 0);

	printf("\n");

	RXWRITE8(sc, CPUCP_REG_RX_MAP, CPUCP_MASK_CMD);

	sc->sc_md.md_node = faa->fa_node;
	sc->sc_md.md_cookie = sc;
	sc->sc_md.md_channel = qccpucp_channel;
	sc->sc_md.md_send = qccpucp_send;
	mbox_register(&sc->sc_md);
}

void *
qccpucp_channel(void *cookie, uint32_t *cells, struct mbox_client *mc)
{
	struct qccpucp_softc *sc = cookie;
	struct qccpucp_channel *ch = NULL;
	uint64_t val;
	int i;

	for (i = 0; i < CPUCP_MAX_CHANNELS; i++) {
		if (sc->sc_chans[i].ch_sc == NULL) {
			ch = &sc->sc_chans[i];
			break;
		}
	}

	if (ch == NULL)
		return NULL;

	val = RXREAD8(sc, CPUCP_REG_RX_EN);
	val |= (1 << i);
	RXWRITE8(sc, CPUCP_REG_RX_EN, val);
	
	ch->ch_idx = i;
	ch->ch_sc = sc;

	return ch;
}

int
qccpucp_send(void *cookie, const void *data, size_t len)
{
	struct qccpucp_channel *ch = cookie;
	struct qccpucp_softc *sc = ch->ch_sc;

	TXWRITE4(sc, CPUCP_REG_CMD(ch->ch_idx), 0);
	
	return 0;
}
