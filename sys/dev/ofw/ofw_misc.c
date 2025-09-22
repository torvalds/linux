/*	$OpenBSD: ofw_misc.c,v 1.43 2023/05/17 23:25:45 patrick Exp $	*/
/*
 * Copyright (c) 2017-2021 Mark Kettenis
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

#include <sys/types.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_media.h>

#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>

/*
 * Register maps.
 */

struct regmap {
	int			rm_node;
	uint32_t		rm_phandle;
	bus_space_tag_t		rm_tag;
	bus_space_handle_t	rm_handle;
	bus_size_t		rm_size;
	
	LIST_ENTRY(regmap)	rm_list;
};

LIST_HEAD(, regmap) regmaps = LIST_HEAD_INITIALIZER(regmap);

void
regmap_register(int node, bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size)
{
	struct regmap *rm;

	rm = malloc(sizeof(struct regmap), M_DEVBUF, M_WAITOK);
	rm->rm_node = node;
	rm->rm_phandle = OF_getpropint(node, "phandle", 0);
	rm->rm_tag = tag;
	rm->rm_handle = handle;
	rm->rm_size = size;
	LIST_INSERT_HEAD(&regmaps, rm, rm_list);
}

struct regmap *
regmap_bycompatible(char *compatible)
{
	struct regmap *rm;

	LIST_FOREACH(rm, &regmaps, rm_list) {
		if (OF_is_compatible(rm->rm_node, compatible))
			return rm;
	}

	return NULL;
}

struct regmap *
regmap_bynode(int node)
{
	struct regmap *rm;

	LIST_FOREACH(rm, &regmaps, rm_list) {
		if (rm->rm_node == node)
			return rm;
	}

	return NULL;
}

struct regmap *
regmap_byphandle(uint32_t phandle)
{
	struct regmap *rm;

	if (phandle == 0)
		return NULL;

	LIST_FOREACH(rm, &regmaps, rm_list) {
		if (rm->rm_phandle == phandle)
			return rm;
	}

	return NULL;
}

void
regmap_write_4(struct regmap *rm, bus_size_t offset, uint32_t value)
{
	KASSERT(offset <= rm->rm_size - sizeof(uint32_t));
	bus_space_write_4(rm->rm_tag, rm->rm_handle, offset, value);
}

uint32_t
regmap_read_4(struct regmap *rm, bus_size_t offset)
{
	KASSERT(offset <= rm->rm_size - sizeof(uint32_t));
	return bus_space_read_4(rm->rm_tag, rm->rm_handle, offset);
}

/*
 * Network interface support.
 */

LIST_HEAD(, if_device) if_devices =
	LIST_HEAD_INITIALIZER(if_devices);

void
if_register(struct if_device *ifd)
{
	ifd->if_phandle = OF_getpropint(ifd->if_node, "phandle", 0);

	LIST_INSERT_HEAD(&if_devices, ifd, if_list);
}

struct ifnet *
if_bynode(int node)
{
	struct if_device *ifd;

	LIST_FOREACH(ifd, &if_devices, if_list) {
		if (ifd->if_node == node)
			return (ifd->if_ifp);
	}

	return (NULL);
}

struct ifnet *
if_byphandle(uint32_t phandle)
{
	struct if_device *ifd;

	if (phandle == 0)
		return (NULL);

	LIST_FOREACH(ifd, &if_devices, if_list) {
		if (ifd->if_phandle == phandle)
			return (ifd->if_ifp);
	}

	return (NULL);
}

/*
 * PHY support.
 */

LIST_HEAD(, phy_device) phy_devices =
	LIST_HEAD_INITIALIZER(phy_devices);

void
phy_register(struct phy_device *pd)
{
	pd->pd_cells = OF_getpropint(pd->pd_node, "#phy-cells", 0);
	pd->pd_phandle = OF_getpropint(pd->pd_node, "phandle", 0);
	if (pd->pd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&phy_devices, pd, pd_list);
}

int
phy_usb_nop_enable(int node)
{
	uint32_t vcc_supply;
	uint32_t *gpio;
	int len;

	vcc_supply = OF_getpropint(node, "vcc-supply", 0);
	if (vcc_supply)
		regulator_enable(vcc_supply);

	len = OF_getproplen(node, "reset-gpios");
	if (len <= 0)
		return 0;

	/* There should only be a single GPIO pin. */
	gpio = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", gpio, len);

	gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(gpio, 1);
	delay(10000);
	gpio_controller_set_pin(gpio, 0);

	free(gpio, M_TEMP, len);

	return 0;
}

int
phy_enable_cells(uint32_t *cells)
{
	struct phy_device *pd;
	uint32_t phandle = cells[0];
	int node;

	LIST_FOREACH(pd, &phy_devices, pd_list) {
		if (pd->pd_phandle == phandle)
			break;
	}

	if (pd && pd->pd_enable)
		return pd->pd_enable(pd->pd_cookie, &cells[1]);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return ENXIO;

	if (OF_is_compatible(node, "usb-nop-xceiv"))
		return phy_usb_nop_enable(node);

	return ENXIO;
}

uint32_t *
phy_next_phy(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#phy-cells", 0);
	return cells + ncells + 1;
}

int
phy_enable_prop_idx(int node, char *prop, int idx)
{
	uint32_t *phys;
	uint32_t *phy;
	int rv = -1;
	int len;

	len = OF_getproplen(node, prop);
	if (len <= 0)
		return -1;

	phys = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, prop, phys, len);

	phy = phys;
	while (phy && phy < phys + (len / sizeof(uint32_t))) {
		if (idx <= 0)
			rv = phy_enable_cells(phy);
		if (idx == 0)
			break;
		phy = phy_next_phy(phy);
		idx--;
	}

	free(phys, M_TEMP, len);
	return rv;
}

int
phy_enable_idx(int node, int idx)
{
	return (phy_enable_prop_idx(node, "phys", idx));
}

int
phy_enable(int node, const char *name)
{
	int idx;

	idx = OF_getindex(node, name, "phy-names");
	if (idx == -1)
		return -1;

	return phy_enable_idx(node, idx);
}

/*
 * I2C support.
 */

LIST_HEAD(, i2c_bus) i2c_busses =
	LIST_HEAD_INITIALIZER(i2c_bus);

void
i2c_register(struct i2c_bus *ib)
{
	ib->ib_phandle = OF_getpropint(ib->ib_node, "phandle", 0);
	if (ib->ib_phandle == 0)
		return;

	LIST_INSERT_HEAD(&i2c_busses, ib, ib_list);
}

struct i2c_controller *
i2c_bynode(int node)
{
	struct i2c_bus *ib;

	LIST_FOREACH(ib, &i2c_busses, ib_list) {
		if (ib->ib_node == node)
			return ib->ib_ic;
	}

	return NULL;
}

struct i2c_controller *
i2c_byphandle(uint32_t phandle)
{
	struct i2c_bus *ib;

	if (phandle == 0)
		return NULL;

	LIST_FOREACH(ib, &i2c_busses, ib_list) {
		if (ib->ib_phandle == phandle)
			return ib->ib_ic;
	}

	return NULL;
}

/*
 * SFP support.
 */

LIST_HEAD(, sfp_device) sfp_devices =
	LIST_HEAD_INITIALIZER(sfp_devices);

void
sfp_register(struct sfp_device *sd)
{
	sd->sd_phandle = OF_getpropint(sd->sd_node, "phandle", 0);
	if (sd->sd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&sfp_devices, sd, sd_list);
}

int
sfp_do_enable(uint32_t phandle, int enable)
{
	struct sfp_device *sd;

	if (phandle == 0)
		return ENXIO;

	LIST_FOREACH(sd, &sfp_devices, sd_list) {
		if (sd->sd_phandle == phandle)
			return sd->sd_enable(sd->sd_cookie, enable);
	}

	return ENXIO;
}

int
sfp_enable(uint32_t phandle)
{
	return sfp_do_enable(phandle, 1);
}

int
sfp_disable(uint32_t phandle)
{
	return sfp_do_enable(phandle, 0);
}

int
sfp_get_sffpage(uint32_t phandle, struct if_sffpage *sff)
{
	struct sfp_device *sd;

	if (phandle == 0)
		return ENXIO;

	LIST_FOREACH(sd, &sfp_devices, sd_list) {
		if (sd->sd_phandle == phandle)
			return sd->sd_get_sffpage(sd->sd_cookie, sff);
	}

	return ENXIO;
}

#define SFF8472_TCC_XCC			3 /* 10G Ethernet Compliance Codes */
#define SFF8472_TCC_XCC_10G_SR		(1 << 4)
#define SFF8472_TCC_XCC_10G_LR		(1 << 5)
#define SFF8472_TCC_XCC_10G_LRM		(1 << 6)
#define SFF8472_TCC_XCC_10G_ER		(1 << 7)
#define SFF8472_TCC_ECC			6 /* Ethernet Compliance Codes */
#define SFF8472_TCC_ECC_1000_SX		(1 << 0)
#define SFF8472_TCC_ECC_1000_LX		(1 << 1)
#define SFF8472_TCC_ECC_1000_CX		(1 << 2)
#define SFF8472_TCC_ECC_1000_T		(1 << 3)
#define SFF8472_TCC_SCT			8 /* SFP+ Cable Technology */
#define SFF8472_TCC_SCT_PASSIVE		(1 << 2)
#define SFF8472_TCC_SCT_ACTIVE		(1 << 3)

int
sfp_add_media(uint32_t phandle, struct mii_data *mii)
{
	struct if_sffpage sff;
	int error;

	memset(&sff, 0, sizeof(sff));
	sff.sff_addr = IFSFF_ADDR_EEPROM;
	sff.sff_page = 0;

	error = sfp_get_sffpage(phandle, &sff);
	if (error)
		return error;

	/* SFP */
	if (sff.sff_data[SFF8472_TCC_ECC] & SFF8472_TCC_ECC_1000_SX) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_1000_SX, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_1000_SX | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_ECC] & SFF8472_TCC_ECC_1000_LX) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_1000_LX, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_1000_LX | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_ECC] & SFF8472_TCC_ECC_1000_CX) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_1000_CX, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_1000_CX | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_ECC] & SFF8472_TCC_ECC_1000_T) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_1000_T, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
	}

	/* SFP+ */
	if (sff.sff_data[SFF8472_TCC_XCC] & SFF8472_TCC_XCC_10G_SR) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_10G_SR, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_10G_SR | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_XCC] & SFF8472_TCC_XCC_10G_LR) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_10G_LR, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_10G_LR | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_XCC] & SFF8472_TCC_XCC_10G_LRM) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_10G_LRM, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_10G_LRM | IFM_FDX;
	}
	if (sff.sff_data[SFF8472_TCC_XCC] & SFF8472_TCC_XCC_10G_ER) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_10G_ER, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_10G_ER | IFM_FDX;
	}

	/* SFP+ DAC */
	if (sff.sff_data[SFF8472_TCC_SCT] & SFF8472_TCC_SCT_PASSIVE ||
	    sff.sff_data[SFF8472_TCC_SCT] & SFF8472_TCC_SCT_ACTIVE) {
		ifmedia_add(&mii->mii_media,
		    IFM_ETHER | IFM_10G_SFP_CU, 0, NULL);
		mii->mii_media_active = IFM_ETHER | IFM_10G_SFP_CU | IFM_FDX;
	}

	return 0;
}

/*
 * PWM support.
 */

LIST_HEAD(, pwm_device) pwm_devices =
	LIST_HEAD_INITIALIZER(pwm_devices);

void
pwm_register(struct pwm_device *pd)
{
	pd->pd_cells = OF_getpropint(pd->pd_node, "#pwm-cells", 0);
	pd->pd_phandle = OF_getpropint(pd->pd_node, "phandle", 0);
	if (pd->pd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&pwm_devices, pd, pd_list);

}

int
pwm_init_state(uint32_t *cells, struct pwm_state *ps)
{
	struct pwm_device *pd;

	LIST_FOREACH(pd, &pwm_devices, pd_list) {
		if (pd->pd_phandle == cells[0]) {
			memset(ps, 0, sizeof(struct pwm_state));
			pd->pd_get_state(pd->pd_cookie, &cells[1], ps);
			ps->ps_pulse_width = 0;
			if (pd->pd_cells >= 2)
				ps->ps_period = cells[2];
			if (pd->pd_cells >= 3)
				ps->ps_flags = cells[3];
			return 0;
		}
	}

	return ENXIO;
}

int
pwm_get_state(uint32_t *cells, struct pwm_state *ps)
{
	struct pwm_device *pd;

	LIST_FOREACH(pd, &pwm_devices, pd_list) {
		if (pd->pd_phandle == cells[0])
			return pd->pd_get_state(pd->pd_cookie, &cells[1], ps);
	}

	return ENXIO;
}

int
pwm_set_state(uint32_t *cells, struct pwm_state *ps)
{
	struct pwm_device *pd;

	LIST_FOREACH(pd, &pwm_devices, pd_list) {
		if (pd->pd_phandle == cells[0])
			return pd->pd_set_state(pd->pd_cookie, &cells[1], ps);
	}

	return ENXIO;
}

/*
 * Non-volatile memory support.
 */

LIST_HEAD(, nvmem_device) nvmem_devices =
	LIST_HEAD_INITIALIZER(nvmem_devices);

struct nvmem_cell {
	uint32_t	nc_phandle;
	struct nvmem_device *nc_nd;
	bus_addr_t	nc_addr;
	bus_size_t	nc_size;
	uint32_t	nc_offset;
	uint32_t	nc_bitlen;

	LIST_ENTRY(nvmem_cell) nc_list;
};

LIST_HEAD(, nvmem_cell) nvmem_cells =
	LIST_HEAD_INITIALIZER(nvmem_cells);

void
nvmem_register_child(int node, struct nvmem_device *nd)
{
	struct nvmem_cell *nc;
	uint32_t phandle;
	uint32_t reg[2], bits[2] = {};

	phandle = OF_getpropint(node, "phandle", 0);
	if (phandle == 0)
		return;

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) != sizeof(reg))
		return;

	OF_getpropintarray(node, "bits", bits, sizeof(bits));
	
	nc = malloc(sizeof(struct nvmem_cell), M_DEVBUF, M_WAITOK);
	nc->nc_phandle = phandle;
	nc->nc_nd = nd;
	nc->nc_addr = reg[0];
	nc->nc_size = reg[1];
	nc->nc_offset = bits[0];
	nc->nc_bitlen = bits[1];
	LIST_INSERT_HEAD(&nvmem_cells, nc, nc_list);
}

void
nvmem_register(struct nvmem_device *nd)
{
	int node;

	nd->nd_phandle = OF_getpropint(nd->nd_node, "phandle", 0);
	if (nd->nd_phandle)
		LIST_INSERT_HEAD(&nvmem_devices, nd, nd_list);

	for (node = OF_child(nd->nd_node); node; node = OF_peer(node))
		nvmem_register_child(node, nd);
}

int
nvmem_read(uint32_t phandle, bus_addr_t addr, void *data, bus_size_t size)
{
	struct nvmem_device *nd;

	if (phandle == 0)
		return ENXIO;

	LIST_FOREACH(nd, &nvmem_devices, nd_list) {
		if (nd->nd_phandle == phandle)
			return nd->nd_read(nd->nd_cookie, addr, data, size);
	}

	return ENXIO;
}

int
nvmem_read_cell(int node, const char *name, void *data, bus_size_t size)
{
	struct nvmem_device *nd;
	struct nvmem_cell *nc;
	uint8_t *p = data;
	bus_addr_t addr;
	uint32_t phandle, *phandles;
	uint32_t offset, bitlen;
	int id, len, first;

	id = OF_getindex(node, name, "nvmem-cell-names");
	if (id < 0)
		return ENXIO;

	len = OF_getproplen(node, "nvmem-cells");
	if (len <= 0)
		return ENXIO;

	phandles = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "nvmem-cells", phandles, len);
	phandle = phandles[id];
	free(phandles, M_TEMP, len);

	LIST_FOREACH(nc, &nvmem_cells, nc_list) {
		if (nc->nc_phandle == phandle)
			break;
	}
	if (nc == NULL)
		return ENXIO;

	nd = nc->nc_nd;
	if (nd->nd_read == NULL)
		return EACCES;

	first = 1;
	addr = nc->nc_addr + (nc->nc_offset / 8);
	offset = nc->nc_offset % 8;
	bitlen = nc->nc_bitlen;
	while (bitlen > 0 && size > 0) {
		uint8_t mask, tmp;
		int error;

		error = nd->nd_read(nd->nd_cookie, addr++, &tmp, 1);
		if (error)
			return error;

		if (bitlen >= 8)
			mask = 0xff;
		else
			mask = (1 << bitlen) - 1;

		if (!first) {
			*p++ |= (tmp << (8 - offset)) & (mask << (8 - offset));
			bitlen -= MIN(offset, bitlen);
			mask >>= offset;
			size--;
		}

		if (bitlen > 0 && size > 0) {
			*p = (tmp >> offset) & mask;
			bitlen -= MIN(8 - offset, bitlen);
		}

		first = 0;
	}
	if (nc->nc_bitlen > 0)
		return 0;

	if (size > nc->nc_size)
		return EINVAL;

	return nd->nd_read(nd->nd_cookie, nc->nc_addr, data, size);
}

int
nvmem_write_cell(int node, const char *name, const void *data, bus_size_t size)
{
	struct nvmem_device *nd;
	struct nvmem_cell *nc;
	const uint8_t *p = data;
	bus_addr_t addr;
	uint32_t phandle, *phandles;
	uint32_t offset, bitlen;
	int id, len, first;

	id = OF_getindex(node, name, "nvmem-cell-names");
	if (id < 0)
		return ENXIO;

	len = OF_getproplen(node, "nvmem-cells");
	if (len <= 0)
		return ENXIO;

	phandles = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "nvmem-cells", phandles, len);
	phandle = phandles[id];
	free(phandles, M_TEMP, len);

	LIST_FOREACH(nc, &nvmem_cells, nc_list) {
		if (nc->nc_phandle == phandle)
			break;
	}
	if (nc == NULL)
		return ENXIO;

	nd = nc->nc_nd;
	if (nd->nd_write == NULL)
		return EACCES;

	first = 1;
	addr = nc->nc_addr + (nc->nc_offset / 8);
	offset = nc->nc_offset % 8;
	bitlen = nc->nc_bitlen;
	while (bitlen > 0 && size > 0) {
		uint8_t mask, tmp;
		int error;

		error = nd->nd_read(nd->nd_cookie, addr, &tmp, 1);
		if (error)
			return error;

		if (bitlen >= 8)
			mask = 0xff;
		else
			mask = (1 << bitlen) - 1;

		tmp &= ~(mask << offset);
		tmp |= (*p++ << offset) & (mask << offset);
		bitlen -= MIN(8 - offset, bitlen);
		size--;

		if (!first && bitlen > 0 && size > 0) {
			tmp &= ~(mask >> (8 - offset));
			tmp |= (*p >> (8 - offset)) & (mask >> (8 - offset));
			bitlen -= MIN(offset, bitlen);
		}

		error = nd->nd_write(nd->nd_cookie, addr++, &tmp, 1);
		if (error)
			return error;

		first = 0;
	}
	if (nc->nc_bitlen > 0)
		return 0;

	if (size > nc->nc_size)
		return EINVAL;

	return nd->nd_write(nd->nd_cookie, nc->nc_addr, data, size);
}

/* Port/endpoint interface support */

LIST_HEAD(, endpoint) endpoints =
	LIST_HEAD_INITIALIZER(endpoints);

void
endpoint_register(int node, struct device_port *dp, enum endpoint_type type)
{
	struct endpoint *ep;

	ep = malloc(sizeof(*ep), M_DEVBUF, M_WAITOK);
	ep->ep_node = node;
	ep->ep_phandle = OF_getpropint(node, "phandle", 0);
	ep->ep_reg = OF_getpropint(node, "reg", -1);
	ep->ep_port = dp;
	ep->ep_type = type;

	LIST_INSERT_HEAD(&endpoints, ep, ep_list);
	LIST_INSERT_HEAD(&dp->dp_endpoints, ep, ep_plist);
}

void
device_port_register(int node, struct device_ports *ports,
    enum endpoint_type type)
{
	struct device_port *dp;

	dp = malloc(sizeof(*dp), M_DEVBUF, M_WAITOK);
	dp->dp_node = node;
	dp->dp_phandle = OF_getpropint(node, "phandle", 0);
	dp->dp_reg = OF_getpropint(node, "reg", -1);
	dp->dp_ports = ports;
	LIST_INIT(&dp->dp_endpoints);
	for (node = OF_child(node); node; node = OF_peer(node))
		endpoint_register(node, dp, type);

	LIST_INSERT_HEAD(&ports->dp_ports, dp, dp_list);
}

void
device_ports_register(struct device_ports *ports,
    enum endpoint_type type)
{
	int node;

	LIST_INIT(&ports->dp_ports);

	node = OF_getnodebyname(ports->dp_node, "ports");
	if (node == 0) {
		node = OF_getnodebyname(ports->dp_node, "port");
		if (node == 0)
			return;
		
		device_port_register(node, ports, type);
		return;
	}

	for (node = OF_child(node); node; node = OF_peer(node))
		device_port_register(node, ports, type);
}

struct device_ports *
device_ports_byphandle(uint32_t phandle)
{
	struct endpoint *ep;

	if (phandle == 0)
		return NULL;

	LIST_FOREACH(ep, &endpoints, ep_list) {
		if (ep->ep_port->dp_phandle == phandle)
			return ep->ep_port->dp_ports;
	}

	return NULL;
}

struct endpoint *
endpoint_byphandle(uint32_t phandle)
{
	struct endpoint *ep;

	if (phandle == 0)
		return NULL;

	LIST_FOREACH(ep, &endpoints, ep_list) {
		if (ep->ep_phandle == phandle)
			return ep;
	}

	return NULL;
}

struct endpoint *
endpoint_byreg(struct device_ports *ports, uint32_t dp_reg, uint32_t ep_reg)
{
	struct device_port *dp;
	struct endpoint *ep;

	LIST_FOREACH(dp, &ports->dp_ports, dp_list) {
		if (dp->dp_reg != dp_reg)
			continue;
		LIST_FOREACH(ep, &dp->dp_endpoints, ep_list) {
			if (ep->ep_reg != ep_reg)
				continue;
			return ep;
		}
	}

	return NULL;
}

struct endpoint *
endpoint_remote(struct endpoint *ep)
{
	struct endpoint *rep;
	int phandle;

	phandle = OF_getpropint(ep->ep_node, "remote-endpoint", 0);
	if (phandle == 0)
		return NULL;

	LIST_FOREACH(rep, &endpoints, ep_list) {
		if (rep->ep_phandle == phandle)
			return rep;
	}

	return NULL;
}

int
endpoint_activate(struct endpoint *ep, void *arg)
{
	struct device_ports *ports = ep->ep_port->dp_ports;
	return ports->dp_ep_activate(ports->dp_cookie, ep, arg);
}

void *
endpoint_get_cookie(struct endpoint *ep)
{
	struct device_ports *ports = ep->ep_port->dp_ports;
	return ports->dp_ep_get_cookie(ports->dp_cookie, ep);
}

int
device_port_activate(uint32_t phandle, void *arg)
{
	struct device_port *dp = NULL;
	struct endpoint *ep, *rep;
	int count;
	int error;

	if (phandle == 0)
		return ENXIO;

	LIST_FOREACH(ep, &endpoints, ep_list) {
		if (ep->ep_port->dp_phandle == phandle) {
			dp = ep->ep_port;
			break;
		}
	}
	if (dp == NULL)
		return ENXIO;

	count = 0;
	LIST_FOREACH(ep, &dp->dp_endpoints, ep_plist) {
		rep = endpoint_remote(ep);
		if (rep == NULL)
			continue;

		error = endpoint_activate(ep, arg);
		if (error)
			continue;
		error = endpoint_activate(rep, arg);
		if (error)
			continue;
		count++;
	}

	return count ? 0 : ENXIO;
}

/* Digital audio interface support */

LIST_HEAD(, dai_device) dai_devices =
	LIST_HEAD_INITIALIZER(dai_devices);

void *
dai_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	return cookie;
}

void
dai_register(struct dai_device *dd)
{
	dd->dd_phandle = OF_getpropint(dd->dd_node, "phandle", 0);
	if (dd->dd_phandle != 0)
		LIST_INSERT_HEAD(&dai_devices, dd, dd_list);

	dd->dd_ports.dp_node = dd->dd_node;
	dd->dd_ports.dp_cookie = dd;
	dd->dd_ports.dp_ep_get_cookie = dai_ep_get_cookie;
	device_ports_register(&dd->dd_ports, EP_DAI_DEVICE);
}

struct dai_device *
dai_byphandle(uint32_t phandle)
{
	struct dai_device *dd;

	if (phandle == 0)
		return NULL;

	LIST_FOREACH(dd, &dai_devices, dd_list) {
		if (dd->dd_phandle == phandle)
			return dd;
	}

	return NULL;
}

/* MII support */

LIST_HEAD(, mii_bus) mii_busses =
	LIST_HEAD_INITIALIZER(mii_busses);

void
mii_register(struct mii_bus *md)
{
	LIST_INSERT_HEAD(&mii_busses, md, md_list);
}

struct mii_bus *
mii_bynode(int node)
{
	struct mii_bus *md;

	LIST_FOREACH(md, &mii_busses, md_list) {
		if (md->md_node == node)
			return md;
	}

	return NULL;
}

struct mii_bus *
mii_byphandle(uint32_t phandle)
{
	int node;

	if (phandle == 0)
		return NULL;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	node = OF_parent(node);
	if (node == 0)
		return NULL;

	return mii_bynode(node);
}

/* IOMMU support */

LIST_HEAD(, iommu_device) iommu_devices =
	LIST_HEAD_INITIALIZER(iommu_devices);

void
iommu_device_register(struct iommu_device *id)
{
	id->id_phandle = OF_getpropint(id->id_node, "phandle", 0);
	if (id->id_phandle == 0)
		return;

	LIST_INSERT_HEAD(&iommu_devices, id, id_list);
}

bus_dma_tag_t
iommu_device_do_map(uint32_t phandle, uint32_t *cells, bus_dma_tag_t dmat)
{
	struct iommu_device *id;

	if (phandle == 0)
		return dmat;

	LIST_FOREACH(id, &iommu_devices, id_list) {
		if (id->id_phandle == phandle)
			return id->id_map(id->id_cookie, cells, dmat);
	}

	return dmat;
}

int
iommu_device_lookup(int node, uint32_t *phandle, uint32_t *cells)
{
	uint32_t *cell;
	uint32_t *map;
	int len, icells, ncells;
	int ret = 1;
	int i;

	len = OF_getproplen(node, "iommus");
	if (len <= 0)
		return ret;

	map = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "iommus", map, len);

	cell = map;
	ncells = len / sizeof(uint32_t);
	while (ncells > 1) {
		node = OF_getnodebyphandle(cell[0]);
		if (node == 0)
			goto out;

		icells = OF_getpropint(node, "#iommu-cells", 1);
		if (ncells < icells + 1)
			goto out;

		KASSERT(icells <= 2);

		*phandle = cell[0];
		for (i = 0; i < icells; i++)
			cells[i] = cell[1 + i];
		ret = 0;
		break;

		cell += (1 + icells);
		ncells -= (1 + icells);
	}

out:
	free(map, M_TEMP, len);

	return ret;
}

int
iommu_device_lookup_pci(int node, uint32_t rid, uint32_t *phandle,
    uint32_t *cells)
{
	uint32_t sid_base;
	uint32_t *cell;
	uint32_t *map;
	uint32_t mask, rid_base;
	int len, length, icells, ncells;
	int ret = 1;

	len = OF_getproplen(node, "iommu-map");
	if (len <= 0)
		return ret;

	map = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "iommu-map", map, len);

	mask = OF_getpropint(node, "iommu-map-mask", 0xffff);
	rid = rid & mask;

	cell = map;
	ncells = len / sizeof(uint32_t);
	while (ncells > 1) {
		node = OF_getnodebyphandle(cell[1]);
		if (node == 0)
			goto out;

		icells = OF_getpropint(node, "#iommu-cells", 1);
		if (ncells < icells + 3)
			goto out;

		KASSERT(icells == 1);

		rid_base = cell[0];
		sid_base = cell[2];
		length = cell[3];
		if (rid >= rid_base && rid < rid_base + length) {
			cells[0] = sid_base + (rid - rid_base);
			*phandle = cell[1];
			ret = 0;
			break;
		}

		cell += 4;
		ncells -= 4;
	}

out:
	free(map, M_TEMP, len);

	return ret;
}

bus_dma_tag_t
iommu_device_map(int node, bus_dma_tag_t dmat)
{
	uint32_t phandle, cells[2] = {0};

	if (iommu_device_lookup(node, &phandle, &cells[0]))
		return dmat;

	return iommu_device_do_map(phandle, &cells[0], dmat);
}

bus_dma_tag_t
iommu_device_map_pci(int node, uint32_t rid, bus_dma_tag_t dmat)
{
	uint32_t phandle, cells[2] = {0};

	if (iommu_device_lookup_pci(node, rid, &phandle, &cells[0]))
		return dmat;

	return iommu_device_do_map(phandle, &cells[0], dmat);
}

void
iommu_device_do_reserve(uint32_t phandle, uint32_t *cells, bus_addr_t addr,
    bus_size_t size)
{
	struct iommu_device *id;

	if (phandle == 0)
		return;

	LIST_FOREACH(id, &iommu_devices, id_list) {
		if (id->id_phandle == phandle) {
			id->id_reserve(id->id_cookie, cells, addr, size);
			break;
		}
	}
}

void
iommu_reserve_region_pci(int node, uint32_t rid, bus_addr_t addr,
    bus_size_t size)
{
	uint32_t phandle, cells[2] = {0};

	if (iommu_device_lookup_pci(node, rid, &phandle, &cells[0]))
		return;

	return iommu_device_do_reserve(phandle, &cells[0], addr, size);
}

/*
 * Mailbox support.
 */

struct mbox_channel {
	struct mbox_device	*mc_md;
	void			*mc_cookie;
};

LIST_HEAD(, mbox_device) mbox_devices =
	LIST_HEAD_INITIALIZER(mbox_devices);

void
mbox_register(struct mbox_device *md)
{
	md->md_cells = OF_getpropint(md->md_node, "#mbox-cells", 0);
	md->md_phandle = OF_getpropint(md->md_node, "phandle", 0);
	if (md->md_phandle == 0)
		return;

	LIST_INSERT_HEAD(&mbox_devices, md, md_list);
}

struct mbox_channel *
mbox_channel_cells(uint32_t *cells, struct mbox_client *client)
{
	struct mbox_device *md;
	struct mbox_channel *mc;
	uint32_t phandle = cells[0];
	void *cookie;

	LIST_FOREACH(md, &mbox_devices, md_list) {
		if (md->md_phandle == phandle)
			break;
	}

	if (md && md->md_channel) {
		cookie = md->md_channel(md->md_cookie, &cells[1], client);
		if (cookie) {
			mc = malloc(sizeof(*mc), M_DEVBUF, M_WAITOK);
			mc->mc_md = md;
			mc->mc_cookie = cookie;
			return mc;
		}
	}

	return NULL;
}

uint32_t *
mbox_next_mbox(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#mbox-cells", 0);
	return cells + ncells + 1;
}

struct mbox_channel *
mbox_channel_idx(int node, int idx, struct mbox_client *client)
{
	struct mbox_channel *mc = NULL;
	uint32_t *mboxes;
	uint32_t *mbox;
	int len;

	len = OF_getproplen(node, "mboxes");
	if (len <= 0)
		return NULL;

	mboxes = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "mboxes", mboxes, len);

	mbox = mboxes;
	while (mbox && mbox < mboxes + (len / sizeof(uint32_t))) {
		if (idx == 0) {
			mc = mbox_channel_cells(mbox, client);
			break;
		}
		mbox = mbox_next_mbox(mbox);
		idx--;
	}

	free(mboxes, M_TEMP, len);
	return mc;
}

struct mbox_channel *
mbox_channel(int node, const char *name, struct mbox_client *client)
{
	int idx;

	idx = OF_getindex(node, name, "mbox-names");
	if (idx == -1)
		return NULL;

	return mbox_channel_idx(node, idx, client);
}

int
mbox_send(struct mbox_channel *mc, const void *data, size_t len)
{
	struct mbox_device *md = mc->mc_md;

	if (md->md_send)
		return md->md_send(mc->mc_cookie, data, len);

	return ENXIO;
}

int
mbox_recv(struct mbox_channel *mc, void *data, size_t len)
{
	struct mbox_device *md = mc->mc_md;

	if (md->md_recv)
		return md->md_recv(mc->mc_cookie, data, len);

	return ENXIO;
}

/* hwlock support */

LIST_HEAD(, hwlock_device) hwlock_devices =
	LIST_HEAD_INITIALIZER(hwlock_devices);

void
hwlock_register(struct hwlock_device *hd)
{
	hd->hd_cells = OF_getpropint(hd->hd_node, "#hwlock-cells", 0);
	hd->hd_phandle = OF_getpropint(hd->hd_node, "phandle", 0);
	if (hd->hd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&hwlock_devices, hd, hd_list);
}

int
hwlock_lock_cells(uint32_t *cells, int lock)
{
	struct hwlock_device *hd;
	uint32_t phandle = cells[0];

	LIST_FOREACH(hd, &hwlock_devices, hd_list) {
		if (hd->hd_phandle == phandle)
			break;
	}

	if (hd && hd->hd_lock)
		return hd->hd_lock(hd->hd_cookie, &cells[1], lock);

	return ENXIO;
}

uint32_t *
hwlock_next_hwlock(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#hwlock-cells", 0);
	return cells + ncells + 1;
}

int
hwlock_do_lock_idx(int node, int idx, int lock)
{
	uint32_t *hwlocks;
	uint32_t *hwlock;
	int rv = -1;
	int len;

	len = OF_getproplen(node, "hwlocks");
	if (len <= 0)
		return -1;

	hwlocks = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "hwlocks", hwlocks, len);

	hwlock = hwlocks;
	while (hwlock && hwlock < hwlocks + (len / sizeof(uint32_t))) {
		if (idx <= 0)
			rv = hwlock_lock_cells(hwlock, lock);
		if (idx == 0)
			break;
		hwlock = hwlock_next_hwlock(hwlock);
		idx--;
	}

	free(hwlocks, M_TEMP, len);
	return rv;
}

int
hwlock_lock_idx(int node, int idx)
{
	return hwlock_do_lock_idx(node, idx, 1);
}

int
hwlock_lock_idx_timeout(int node, int idx, int ms)
{
	int i, ret = ENXIO;

	for (i = 0; i <= ms; i++) {
		ret = hwlock_do_lock_idx(node, idx, 1);
		if (ret == EAGAIN) {
			delay(1000);
			continue;
		}
		break;
	}

	return ret;
}

int
hwlock_unlock_idx(int node, int idx)
{
	return hwlock_do_lock_idx(node, idx, 0);
}
