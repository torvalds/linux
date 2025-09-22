/*	$OpenBSD: iicmux.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
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
#include <sys/malloc.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>

#define IICMUX_MAX_CHANNELS	4

struct iicmux_bus {
	struct iicmux_softc	*pb_sc;
	int			pb_node;
	int			pb_channel;
	struct i2c_controller	pb_ic;
	struct i2c_bus		pb_ib;
	struct device		*pb_iic;
};

struct iicmux_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;

	int			sc_node;
	int			sc_channel;
	int			sc_nchannel;
	struct iicmux_bus	sc_bus[IICMUX_MAX_CHANNELS];
	const char		*sc_busnames[IICMUX_MAX_CHANNELS];
	struct rwlock		sc_lock;

	int			sc_enable;
};

int	iicmux_match(struct device *, void *, void *);
void	iicmux_attach(struct device *, struct device *, void *);

const struct cfattach iicmux_ca = {
	sizeof(struct iicmux_softc), iicmux_match, iicmux_attach
};

struct cfdriver iicmux_cd = {
	NULL, "iicmux", DV_DULL
};

int	iicmux_acquire_bus(void *, int);
void	iicmux_release_bus(void *, int);
int	iicmux_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);
void	iicmux_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
iicmux_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "i2c-mux-pinctrl");
}

void
iicmux_attach(struct device *parent, struct device *self, void *aux)
{
	struct iicmux_softc *sc = (struct iicmux_softc *)self;
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;
	uint32_t phandle;
	char *names;
	char *name;
	char *end;
	int len;

	sc->sc_channel = -1;	/* unknown */
	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);

	phandle = OF_getpropint(node, "i2c-parent", 0);
	sc->sc_tag = i2c_byphandle(phandle);
	if (sc->sc_tag == NULL) {
		printf(": can't find parent bus\n");
		return;
	}

	len = OF_getproplen(node, "pinctrl-names");
	if (len <= 0) {
		printf(": no channels\n");
		return;
	}

	names = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getprop(node, "pinctrl-names", names, len);
	end = names + len;
	name = names;
	while (name < end) {
		if (strcmp(name, "idle") == 0)
			break;

		if (sc->sc_nchannel >= IICMUX_MAX_CHANNELS) {
			printf(": too many channels\n");
			free(names, M_DEVBUF, len);
			return;
		}

		sc->sc_busnames[sc->sc_nchannel] = name;
		name += strlen(name) + 1;
		sc->sc_nchannel++;
	}

	printf("\n");

	sc->sc_node = node;
	for (node = OF_child(node); node; node = OF_peer(node)) {
		struct i2cbus_attach_args iba;
		struct iicmux_bus *pb;
		uint32_t channel;

		channel = OF_getpropint(node, "reg", -1);
		if (channel >= sc->sc_nchannel)
			continue;

		pb = &sc->sc_bus[channel];
		pb->pb_sc = sc;
		pb->pb_node = node;
		pb->pb_channel = channel;
		pb->pb_ic.ic_cookie = pb;
		pb->pb_ic.ic_acquire_bus = iicmux_acquire_bus;
		pb->pb_ic.ic_release_bus = iicmux_release_bus;
		pb->pb_ic.ic_exec = iicmux_exec;

		/* Configure the child busses. */
		memset(&iba, 0, sizeof(iba));
		iba.iba_name = "iic";
		iba.iba_tag = &pb->pb_ic;
		iba.iba_bus_scan = iicmux_bus_scan;
		iba.iba_bus_scan_arg = &pb->pb_node;

		config_found(&sc->sc_dev, &iba, iicbus_print);

		pb->pb_ib.ib_node = node;
		pb->pb_ib.ib_ic = &pb->pb_ic;
		i2c_register(&pb->pb_ib);
	}
}

int
iicmux_set_channel(struct iicmux_softc *sc, int channel, int flags)
{
	int error;

	if (channel < -1 || channel >= sc->sc_nchannel)
		return ENXIO;

	if (sc->sc_channel == channel)
		return 0;

	error = pinctrl_byname(sc->sc_node, sc->sc_busnames[channel]);
	if (error == 0)
		sc->sc_channel = channel;

	return error ? EIO : 0;
}

int
iicmux_acquire_bus(void *cookie, int flags)
{
	struct iicmux_bus *pb = cookie;
	struct iicmux_softc *sc = pb->pb_sc;
	int rwflags = RW_WRITE;
	int error;

	if (flags & I2C_F_POLL)
		rwflags |= RW_NOSLEEP;

	error = rw_enter(&sc->sc_lock, rwflags);
	if (error)
		return error;

	/* Acquire parent bus. */
	error = iic_acquire_bus(sc->sc_tag, flags);
	if (error) {
		rw_exit_write(&sc->sc_lock);
		return error;
	}

	error = iicmux_set_channel(sc, pb->pb_channel, flags);
	if (error) {
		iic_release_bus(sc->sc_tag, flags);
		rw_exit_write(&sc->sc_lock);
		return error;
	}

	return 0;
}

void
iicmux_release_bus(void *cookie, int flags)
{
	struct iicmux_bus *pb = cookie;
	struct iicmux_softc *sc = pb->pb_sc;

	if (pinctrl_byname(sc->sc_node, "idle") == 0)
		sc->sc_channel = -1;

	/* Release parent bus. */
	iic_release_bus(sc->sc_tag, flags);
	rw_exit_write(&sc->sc_lock);
}

int
iicmux_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct iicmux_bus *pb = cookie;
	struct iicmux_softc *sc = pb->pb_sc;

	rw_assert_wrlock(&sc->sc_lock);

	/* Issue the transaction on the parent bus. */
	return iic_exec(sc->sc_tag, op, addr, cmd, cmdlen, buf, buflen, flags);
}

void
iicmux_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
{
	int iba_node = *(int *)arg;
	struct i2c_attach_args ia;
	char name[32];
	uint32_t reg[1];
	int node;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = name;
		ia.ia_cookie = &node;
		config_found(self, &ia, iic_print);
	}
}
