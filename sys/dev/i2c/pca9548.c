/*	$OpenBSD: pca9548.c,v 1.7 2024/05/13 01:15:50 jsg Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis
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

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#ifdef __HAVE_ACPI
#include "acpi.h"
#endif

#if NACPI > 0
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>
#endif

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>

#define PCAMUX_MAX_CHANNELS	8

struct pcamux_bus {
	struct pcamux_softc	*pb_sc;
	int			pb_node;
	void			*pb_devnode;
	int			pb_channel;
	struct i2c_controller	pb_ic;
	struct i2c_bus		pb_ib;
	struct device		*pb_iic;
};

struct pcamux_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;

	int			sc_node;
	void			*sc_devnode;
	int			sc_channel;
	int			sc_nchannel;
	struct pcamux_bus	sc_bus[PCAMUX_MAX_CHANNELS];
	struct rwlock		sc_lock;

	int			sc_switch;
	int			sc_enable;
};

#if NACPI > 0
struct pcamux_crs {
	uint16_t i2c_addr;
	struct aml_node *devnode;
};
#endif

int	pcamux_match(struct device *, void *, void *);
void	pcamux_attach(struct device *, struct device *, void *);

const struct cfattach pcamux_ca = {
	sizeof(struct pcamux_softc), pcamux_match, pcamux_attach
};

struct cfdriver pcamux_cd = {
	NULL, "pcamux", DV_DULL
};

void	pcamux_attach_fdt(struct pcamux_softc *, struct i2c_attach_args *);
void	pcamux_attach_acpi(struct pcamux_softc *, struct i2c_attach_args *);

#if NACPI > 0
int	pcamux_attach_acpi_mux(struct aml_node *, void *);
void	pcamux_acpi_bus_scan(struct device *,
	    struct i2cbus_attach_args *, void *);
int	pcamux_acpi_found_hid(struct aml_node *, void *);
int	pcamux_acpi_parse_crs(int, union acpi_resource *, void *);
#endif

int	pcamux_acquire_bus(void *, int);
void	pcamux_release_bus(void *, int);
int	pcamux_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);
void	pcamux_bus_scan(struct device *, struct i2cbus_attach_args *, void *);

int
pcamux_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "nxp,pca9546") == 0 ||
	    strcmp(ia->ia_name, "nxp,pca9547") == 0 ||
	    strcmp(ia->ia_name, "nxp,pca9548") == 0 ||
	    strcmp(ia->ia_name, "NXP0002") == 0)
		return (1);
	return (0);
}

void
pcamux_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcamux_softc *sc = (struct pcamux_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	sc->sc_channel = -1;	/* unknown */
	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);

	if (strcmp(ia->ia_name, "nxp,pca9546") == 0) {
		sc->sc_switch = 1;
		sc->sc_nchannel = 4;
	} else if (strcmp(ia->ia_name, "nxp,pca9547") == 0 ||
	    strcmp(ia->ia_name, "NXP0002") == 0) {
		sc->sc_enable = 1 << 3;
		sc->sc_nchannel = 8;
	} else if (strcmp(ia->ia_name, "nxp,pca9548") == 0) {
		sc->sc_switch = 1;
		sc->sc_nchannel = 8;
	}

	printf("\n");

	if (strcmp(ia->ia_name, "NXP0002") == 0)
		pcamux_attach_acpi(sc, ia);
	else
		pcamux_attach_fdt(sc, ia);
}

void
pcamux_attach_fdt(struct pcamux_softc *sc, struct i2c_attach_args *ia)
{
	int node = *(int *)ia->ia_cookie;

	sc->sc_node = node;
	for (node = OF_child(node); node; node = OF_peer(node)) {
		struct i2cbus_attach_args iba;
		struct pcamux_bus *pb;
		uint32_t channel;

		channel = OF_getpropint(node, "reg", -1);
		if (channel >= sc->sc_nchannel)
			continue;

		pb = &sc->sc_bus[channel];
		pb->pb_sc = sc;
		pb->pb_node = node;
		pb->pb_channel = channel;
		pb->pb_ic.ic_cookie = pb;
		pb->pb_ic.ic_acquire_bus = pcamux_acquire_bus;
		pb->pb_ic.ic_release_bus = pcamux_release_bus;
		pb->pb_ic.ic_exec = pcamux_exec;

		/* Configure the child busses. */
		memset(&iba, 0, sizeof(iba));
		iba.iba_name = "iic";
		iba.iba_tag = &pb->pb_ic;
		iba.iba_bus_scan = pcamux_bus_scan;
		iba.iba_bus_scan_arg = &pb->pb_node;

		config_found(&sc->sc_dev, &iba, iicbus_print);

		pb->pb_ib.ib_node = node;
		pb->pb_ib.ib_ic = &pb->pb_ic;
		i2c_register(&pb->pb_ib);
	}
}

void
pcamux_attach_acpi(struct pcamux_softc *sc, struct i2c_attach_args *ia)
{
#if NACPI > 0
	struct aml_node *node = ia->ia_cookie;

	sc->sc_devnode = node;
	aml_walknodes(node, AML_WALK_PRE, pcamux_attach_acpi_mux, sc);
#endif
}

#if NACPI > 0
int
pcamux_attach_acpi_mux(struct aml_node *node, void *arg)
{
	struct pcamux_softc *sc = arg;
	struct i2cbus_attach_args iba;
	struct pcamux_bus *pb;
	uint64_t channel;

	/* Only the node's direct children */
	if (node->parent != sc->sc_devnode)
		return 0;

	/* Must have channel as address */
	if (aml_evalinteger(acpi_softc, node, "_ADR", 0, NULL, &channel) ||
	    channel >= sc->sc_nchannel)
		return 0;

	pb = &sc->sc_bus[channel];
	pb->pb_sc = sc;
	pb->pb_devnode = node;
	pb->pb_channel = channel;
	pb->pb_ic.ic_cookie = pb;
	pb->pb_ic.ic_acquire_bus = pcamux_acquire_bus;
	pb->pb_ic.ic_release_bus = pcamux_release_bus;
	pb->pb_ic.ic_exec = pcamux_exec;

	/* Configure the child busses. */
	memset(&iba, 0, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &pb->pb_ic;
	iba.iba_bus_scan = pcamux_acpi_bus_scan;
	iba.iba_bus_scan_arg = pb;

	config_found(&sc->sc_dev, &iba, iicbus_print);

#ifndef SMALL_KERNEL
	node->i2c = &pb->pb_ic;
	acpi_register_gsb(acpi_softc, node);
#endif
	return 0;
}

void
pcamux_acpi_bus_scan(struct device *iic, struct i2cbus_attach_args *iba,
    void *aux)
{
	struct pcamux_bus *pb = aux;

	pb->pb_iic = iic;
	aml_find_node(pb->pb_devnode, "_HID", pcamux_acpi_found_hid, aux);
}

int
pcamux_acpi_found_hid(struct aml_node *node, void *arg)
{
	struct pcamux_bus *pb = arg;
	struct pcamux_softc *sc = pb->pb_sc;
	struct pcamux_crs crs;
	struct aml_value res;
	int64_t sta;
	char cdev[16], dev[16];
	struct i2c_attach_args ia;

	/* Skip our own _HID. */
	if (node->parent == pb->pb_devnode)
		return 0;

	/* Only direct descendants, because of possible muxes. */
	if (node->parent && node->parent->parent != pb->pb_devnode)
		return 0;

	if (acpi_parsehid(node, arg, cdev, dev, 16) != 0)
		return 0;

	sta = acpi_getsta(acpi_softc, node->parent);
	if ((sta & STA_PRESENT) == 0)
		return 0;

	if (aml_evalname(acpi_softc, node->parent, "_CRS", 0, NULL, &res))
		return 0;

	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		printf("%s: invalid _CRS object (type %d len %d)\n",
		    sc->sc_dev.dv_xname, res.type, res.length);
		aml_freevalue(&res);
		return (0);
	}
	memset(&crs, 0, sizeof(crs));
	crs.devnode = sc->sc_devnode;
	aml_parse_resource(&res, pcamux_acpi_parse_crs, &crs);
	aml_freevalue(&res);

	acpi_attach_deps(acpi_softc, node->parent);

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = &pb->pb_ic;
	ia.ia_name = dev;
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = node->parent;

	config_found(pb->pb_iic, &ia, iic_print);
	node->parent->attached = 1;

	return 0;
}

int
pcamux_acpi_parse_crs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct pcamux_crs *sc_crs = arg;

	switch (AML_CRSTYPE(crs)) {
	case LR_SERBUS:
		if (crs->lr_serbus.type == LR_SERBUS_I2C)
			sc_crs->i2c_addr = crs->lr_i2cbus._adr;
		break;

	default:
		printf("%s: unknown resource type %d\n", __func__,
		    AML_CRSTYPE(crs));
	}

	return 0;
}
#endif

int
pcamux_set_channel(struct pcamux_softc *sc, int channel, int flags)
{
	uint8_t data;
	int error;

	if (channel < -1 || channel >= sc->sc_nchannel)
		return ENXIO;

	if (sc->sc_channel == channel)
		return 0;

	data = 0;
	if (channel != -1) {
		if (sc->sc_switch)
			data = 1 << channel;
		else
			data = sc->sc_enable | channel;
	}
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, NULL, 0, &data, sizeof data, flags);

	return error;
}

int
pcamux_acquire_bus(void *cookie, int flags)
{
	struct pcamux_bus *pb = cookie;
	struct pcamux_softc *sc = pb->pb_sc;
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

	error = pcamux_set_channel(sc, pb->pb_channel, flags);
	if (error) {
		iic_release_bus(sc->sc_tag, flags);
		rw_exit_write(&sc->sc_lock);
		return error;
	}

	return 0;
}

void
pcamux_release_bus(void *cookie, int flags)
{
	struct pcamux_bus *pb = cookie;
	struct pcamux_softc *sc = pb->pb_sc;

	/* Release parent bus. */
	iic_release_bus(sc->sc_tag, flags);
	rw_exit_write(&sc->sc_lock);
}

int
pcamux_exec(void *cookie, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct pcamux_bus *pb = cookie;
	struct pcamux_softc *sc = pb->pb_sc;

	rw_assert_wrlock(&sc->sc_lock);

	/* Issue the transaction on the parent bus. */
	return iic_exec(sc->sc_tag, op, addr, cmd, cmdlen, buf, buflen, flags);
}

void
pcamux_bus_scan(struct device *self, struct i2cbus_attach_args *iba, void *arg)
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
