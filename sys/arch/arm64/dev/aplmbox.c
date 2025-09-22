/*	$OpenBSD: aplmbox.c,v 1.6 2023/07/23 11:17:49 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#include <arm64/dev/aplmbox.h>

#define MBOX_A2I_CTRL		0x110
#define  MBOX_A2I_CTRL_FULL	(1 << 16)
#define MBOX_I2A_CTRL		0x114
#define  MBOX_I2A_CTRL_EMPTY	(1 << 17)
#define MBOX_A2I_SEND0		0x800
#define MBOX_A2I_SEND1		0x808
#define MBOX_I2A_RECV0		0x830
#define MBOX_I2A_RECV1		0x838

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HREAD8(sc, reg)							\
	(bus_space_read_8((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HWRITE8(sc, reg, val)						\
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplmbox_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;
	void			(*sc_rx_callback)(void *);
	void			*sc_rx_arg;

	struct mbox_device	sc_md;
};

int	aplmbox_match(struct device *, void *, void *);
void	aplmbox_attach(struct device *, struct device *, void *);

const struct cfattach aplmbox_ca = {
	sizeof (struct aplmbox_softc), aplmbox_match, aplmbox_attach
};

struct cfdriver aplmbox_cd = {
	NULL, "aplmbox", DV_DULL
};

int	aplmbox_intr(void *);
void	*aplmbox_channel(void *, uint32_t *, struct mbox_client *);
int	aplmbox_send(void *, const void *, size_t);
int	aplmbox_recv(void *, void *, size_t);

int
aplmbox_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "apple,asc-mailbox") ||
	    OF_is_compatible(faa->fa_node, "apple,asc-mailbox-v4"));
}

void
aplmbox_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplmbox_softc *sc = (struct aplmbox_softc *)self;
	struct fdt_attach_args *faa = aux;
	int idx;

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

	idx = OF_getindex(faa->fa_node, "recv-not-empty", "interrupt-names");
	if (idx > 0) {
		sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, idx, IPL_BIO,
		    aplmbox_intr, sc, sc->sc_dev.dv_xname);
	}

	printf("\n");

	power_domain_enable(faa->fa_node);

	sc->sc_md.md_node = faa->fa_node;
	sc->sc_md.md_cookie = sc;
	sc->sc_md.md_channel = aplmbox_channel;
	sc->sc_md.md_send = aplmbox_send;
	sc->sc_md.md_recv = aplmbox_recv;
	mbox_register(&sc->sc_md);
}

int
aplmbox_intr(void *arg)
{
	struct aplmbox_softc *sc = arg;
	uint32_t ctrl;

	ctrl = HREAD4(sc, MBOX_I2A_CTRL);
	if (ctrl & MBOX_I2A_CTRL_EMPTY)
		return 0;

	if (sc->sc_rx_callback) {
		sc->sc_rx_callback(sc->sc_rx_arg);
	} else {
		printf("%s: 0x%016llx 0x%016llx\n", sc->sc_dev.dv_xname,
		    HREAD8(sc, MBOX_I2A_RECV0), HREAD8(sc, MBOX_I2A_RECV1));
	}

	return 1;
}

void *
aplmbox_channel(void *cookie, uint32_t *cells, struct mbox_client *mc)
{
	struct aplmbox_softc *sc = cookie;

	if (mc) {
		sc->sc_rx_callback = mc->mc_rx_callback;
		sc->sc_rx_arg = mc->mc_rx_arg;

		if (mc->mc_flags & MC_WAKEUP)
			intr_set_wakeup(sc->sc_ih);
	}

	return sc;
}

int
aplmbox_send(void *cookie, const void *data, size_t len)
{
	struct aplmbox_softc *sc = cookie;
	const struct aplmbox_msg *msg = data;
	uint32_t ctrl;

	if (len != sizeof(struct aplmbox_msg))
		return EINVAL;

	ctrl = HREAD4(sc, MBOX_A2I_CTRL);
	if (ctrl & MBOX_A2I_CTRL_FULL)
		return EBUSY;

	HWRITE8(sc, MBOX_A2I_SEND0, msg->data0);
	HWRITE8(sc, MBOX_A2I_SEND1, msg->data1);

	return 0;
}

int
aplmbox_recv(void *cookie, void *data, size_t len)
{
	struct aplmbox_softc *sc = cookie;
	struct aplmbox_msg *msg = data;
	uint32_t ctrl;

	if (len != sizeof(struct aplmbox_msg))
		return EINVAL;

	ctrl = HREAD4(sc, MBOX_I2A_CTRL);
	if (ctrl & MBOX_I2A_CTRL_EMPTY)
		return EWOULDBLOCK;

	msg->data0 = HREAD8(sc, MBOX_I2A_RECV0);
	msg->data1 = HREAD8(sc, MBOX_I2A_RECV1);

	return 0;
}
