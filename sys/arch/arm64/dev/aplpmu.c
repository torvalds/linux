/*	$OpenBSD: aplpmu.c,v 1.7 2022/12/12 18:45:01 kettenis Exp $	*/
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

#include <dev/clock_subr.h>
#include <dev/fdt/spmivar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

extern void (*cpuresetfn)(void);
extern void (*powerdownfn)(void);

/*
 * This driver is based on preliminary device tree bindings and will
 * almost certainly need changes once the official bindings land in
 * mainline Linux.  Support for these preliminary bindings will be
 * dropped as soon as official bindings are available.
 */

/*
 * Apple's "sera" PMU contains an RTC that provides time in 32.16
 * fixed-point format as well as a time offset in 33.15 fixed-point
 * format.  The sum of the two gives us a standard Unix timestamp with
 * sub-second resolution.  The time itself is read-only.  To set the
 * time we need to adjust the time offset.
 */
#define SERA_TIME		0xd002
#define SERA_TIME_OFFSET	0xd100
#define SERA_TIME_LEN		6

#define SERA_POWERDOWN		0x9f0f
#define SERA_POWERDOWN_MAGIC	0x08

struct aplpmu_nvmem {
	struct aplpmu_softc	*an_sc;
	struct nvmem_device	an_nd;
	bus_addr_t		an_base;
	bus_size_t		an_size;
};

struct aplpmu_softc {
	struct device		sc_dev;
	spmi_tag_t		sc_tag;
	int8_t			sc_sid;

	struct todr_chip_handle sc_todr;
	uint64_t		sc_offset;
};

struct aplpmu_softc *aplpmu_sc;

int	aplpmu_match(struct device *, void *, void *);
void	aplpmu_attach(struct device *, struct device *, void *);

const struct cfattach	aplpmu_ca = {
	sizeof (struct aplpmu_softc), aplpmu_match, aplpmu_attach
};

struct cfdriver aplpmu_cd = {
	NULL, "aplpmu", DV_DULL
};

int	aplpmu_gettime(struct todr_chip_handle *, struct timeval *);
int	aplpmu_settime(struct todr_chip_handle *, struct timeval *);
void	aplpmu_powerdown(void);
int	aplpmu_nvmem_read(void *, bus_addr_t, void *, bus_size_t);
int	aplpmu_nvmem_write(void *, bus_addr_t, const void *, bus_size_t);

int
aplpmu_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *sa = aux;

	return OF_is_compatible(sa->sa_node, "apple,sera-pmu") ||
	    OF_is_compatible(sa->sa_node, "apple,spmi-pmu");
}

void
aplpmu_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplpmu_softc *sc = (struct aplpmu_softc *)self;
	struct spmi_attach_args *sa = aux;
	uint8_t data[8] = {};
	int error, node;

	sc->sc_tag = sa->sa_tag;
	sc->sc_sid = sa->sa_sid;

	if (OF_is_compatible(sa->sa_node, "apple,sera-pmu")) {
		error = spmi_cmd_read(sc->sc_tag, sc->sc_sid,
		    SPMI_CMD_EXT_READL, SERA_TIME_OFFSET,
		    &data, SERA_TIME_LEN);
		if (error) {
			printf(": can't read offset\n");
			return;
		}
		sc->sc_offset = lemtoh64(data);

		sc->sc_todr.cookie = sc;
		sc->sc_todr.todr_gettime = aplpmu_gettime;
		sc->sc_todr.todr_settime = aplpmu_settime;
		sc->sc_todr.todr_quality = 0;
		todr_attach(&sc->sc_todr);

		aplpmu_sc = sc;
		powerdownfn = aplpmu_powerdown;
	}

	printf("\n");

	for (node = OF_child(sa->sa_node); node; node = OF_peer(node)) {
		struct aplpmu_nvmem *an;
		uint32_t reg[2];

		if (!OF_is_compatible(node, "apple,spmi-pmu-nvmem"))
			continue;

		if (OF_getpropintarray(node, "reg", reg,
		    sizeof(reg)) != sizeof(reg))
			continue;

		an = malloc(sizeof(*an), M_DEVBUF, M_WAITOK);
		an->an_sc = sc;
		an->an_base = reg[0];
		an->an_size = reg[1];
		an->an_nd.nd_node = node;
		an->an_nd.nd_cookie = an;
		an->an_nd.nd_read = aplpmu_nvmem_read;
		an->an_nd.nd_write = aplpmu_nvmem_write;
		nvmem_register(&an->an_nd);
	}
}

int
aplpmu_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct aplpmu_softc *sc = handle->cookie;
	uint8_t data[8] = {};
	uint64_t time;
	int error;

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    SERA_TIME, &data, SERA_TIME_LEN);
	if (error)
		return error;
	time = lemtoh64(data) + (sc->sc_offset << 1);

	tv->tv_sec = (time >> 16);
	tv->tv_usec = (((time & 0xffff) * 1000000) >> 16);
	return 0;
}

int
aplpmu_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct aplpmu_softc *sc = handle->cookie;
	uint8_t data[8] = {};
	uint64_t time;
	int error;

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    SERA_TIME, &data, SERA_TIME_LEN);
	if (error)
		return error;

	time = ((uint64_t)tv->tv_sec << 16);
	time |= ((uint64_t)tv->tv_usec << 16) / 1000000;
	sc->sc_offset = ((time - lemtoh64(data)) >> 1);

	htolem64(data, sc->sc_offset);
	return spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    SERA_TIME_OFFSET, &data, SERA_TIME_LEN);
}

void
aplpmu_powerdown(void)
{
	struct aplpmu_softc *sc = aplpmu_sc;
	uint8_t data = SERA_POWERDOWN_MAGIC;

	spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    SERA_POWERDOWN, &data, sizeof(data));

	cpuresetfn();
}

int
aplpmu_nvmem_read(void *cookie, bus_addr_t addr, void *data, bus_size_t size)
{
	struct aplpmu_nvmem *an = cookie;
	struct aplpmu_softc *sc = an->an_sc;

	if (addr >= an->an_size || size > an->an_size - addr)
		return EINVAL;

	return spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    an->an_base + addr, data, size);
}

int
aplpmu_nvmem_write(void *cookie, bus_addr_t addr, const void *data,
    bus_size_t size)
{
	struct aplpmu_nvmem *an = cookie;
	struct aplpmu_softc *sc = an->an_sc;

	if (addr >= an->an_size || size > an->an_size - addr)
		return EINVAL;

	return spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    an->an_base + addr, data, size);
}
