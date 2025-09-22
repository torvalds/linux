/*	$OpenBSD: aplspmi.c,v 1.2 2022/04/06 18:59:26 naddy Exp $	*/
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/fdt/spmivar.h>

/*
 * This driver is based on preliminary device tree bindings and will
 * almost certainly need changes once the official bindings land in
 * mainline Linux.  Support for these preliminary bindings will be
 * dropped as soon as official bindings are available.
 */

#define SPMI_STAT		0x00
#define  SPMI_STAT_RXEMPTY		(1 << 24)
#define  SPMI_STAT_TXEMPTY		(1 << 8)
#define SPMI_CMD		0x04
#define  SPMI_CMD_ADDR(x)		((x) << 16)
#define  SPMI_CMD_LAST			(1 << 15)
#define  SPMI_CMD_SID(x)		((x) << 8)
#define SPMI_RESP		0x08
#define SPMI_INTEN(i)		(0x20 + (i) * 4)
#define SPMI_INTSTAT(i)		(0x60 + (i) * 4)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplspmi_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct spmi_controller	sc_tag;
};

int	aplspmi_match(struct device *, void *, void *);
void	aplspmi_attach(struct device *, struct device *, void *);

const struct cfattach	aplspmi_ca = {
	sizeof (struct aplspmi_softc), aplspmi_match, aplspmi_attach
};

struct cfdriver aplspmi_cd = {
	NULL, "aplspmi", DV_DULL
};

int	aplspmi_print(void *, const char *);
int	aplspmi_cmd_read(void *, uint8_t, uint8_t, uint16_t, void *, size_t);
int	aplspmi_cmd_write(void *, uint8_t, uint8_t, uint16_t,
	    const void *, size_t);

int
aplspmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,spmi");
}

void
aplspmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplspmi_softc *sc = (struct aplspmi_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct spmi_attach_args sa;
	char name[32];
	uint32_t reg[2];
	int node;

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

	printf("\n");

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_cmd_read = aplspmi_cmd_read;
	sc->sc_tag.sc_cmd_write = aplspmi_cmd_write;

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_getpropintarray(node, "reg", reg,
		    sizeof(reg)) != sizeof(reg))
			continue;

		memset(name, 0, sizeof(name));
		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		memset(&sa, 0, sizeof(sa));
		sa.sa_tag = &sc->sc_tag;
		sa.sa_sid = reg[0];
		sa.sa_name = name;
		sa.sa_node = node;
		config_found(self, &sa, aplspmi_print);
	}
}

int
aplspmi_print(void *aux, const char *pnp)
{
	struct spmi_attach_args *sa = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", sa->sa_name, pnp);
	printf(" sid 0x%x", sa->sa_sid);

	return UNCONF;
}

int
aplspmi_read_resp(struct aplspmi_softc *sc, uint32_t *resp)
{
	int retry;

	for (retry = 1000; retry > 0; retry--) {
		if ((HREAD4(sc, SPMI_STAT) & SPMI_STAT_RXEMPTY) == 0)
			break;
		delay(1);
	}
	if (retry == 0)
		return ETIMEDOUT;

	*resp = HREAD4(sc, SPMI_RESP);
	return 0;
}

int
aplspmi_cmd_read(void *cookie, uint8_t sid, uint8_t cmd, uint16_t addr,
    void *buf, size_t len)
{
	struct aplspmi_softc *sc = cookie;
	uint8_t *cbuf = buf;
	uint32_t resp;
	int error;

	if (len == 0 || len > 8)
		return EINVAL;

	HWRITE4(sc, SPMI_CMD, SPMI_CMD_SID(sid) | cmd | SPMI_CMD_ADDR(addr) |
	    (len - 1) | SPMI_CMD_LAST);

	error = aplspmi_read_resp(sc, &resp);
	if (error)
		return error;

	while (len > 0) {
		error = aplspmi_read_resp(sc, &resp);
		if (error)
			return error;
		memcpy(cbuf, &resp, MIN(len, 4));
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}

	return 0;
}

int
aplspmi_cmd_write(void *cookie, uint8_t sid, uint8_t cmd, uint16_t addr,
    const void *buf, size_t len)
{
	struct aplspmi_softc *sc = cookie;
	const uint8_t *cbuf = buf;
	uint32_t data, resp;

	if (len == 0 || len > 8)
		return EINVAL;

	HWRITE4(sc, SPMI_CMD, SPMI_CMD_SID(sid) | cmd | SPMI_CMD_ADDR(addr) |
	    (len - 1) | SPMI_CMD_LAST);

	while (len > 0) {
		memcpy(&data, cbuf, MIN(len, 4));
		HWRITE4(sc, SPMI_CMD, data);
		cbuf += MIN(len, 4);
		len -= MIN(len, 4);
	}

	return aplspmi_read_resp(sc, &resp);
}
