/*	$OpenBSD: amlsm.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_misc.h>

extern register_t smc_call(register_t, register_t, register_t, register_t);

/* Calls */
#define AML_SM_GET_SHARE_MEM_INPUT_BASE		0x82000020
#define AML_SM_GET_SHARE_MEM_OUTPUT_BASE	0x82000021
#define AML_SM_EFUSE_READ			0x82000030
#define AML_SM_EFUSE_USER_MAX			0x82000033
#define AML_SM_JTAG_ON				0x82000040
#define AML_SM_JTAG_OFF				0x82000041
#define AML_SM_GET_CHIP_ID			0x82000044

struct aml_cpu_info {
	uint32_t	version;
	uint32_t	chip_id[4];
};

struct amlsm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_in_ioh;
	bus_space_handle_t	sc_out_ioh;
};

int	amlsm_match(struct device *, void *, void *);
void	amlsm_attach(struct device *, struct device *, void *);

const struct cfattach	amlsm_ca = {
	sizeof (struct amlsm_softc), amlsm_match, amlsm_attach
};

struct cfdriver amlsm_cd = {
	NULL, "amlsm", DV_DULL
};

int
amlsm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,meson-gxbb-sm");
}

void
amlsm_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlsm_softc *sc = (struct amlsm_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct aml_cpu_info *info;
	bus_addr_t addr;
	int32_t ret;
	int i;

	sc->sc_iot = faa->fa_iot;

	addr = smc_call(AML_SM_GET_SHARE_MEM_INPUT_BASE, 0, 0, 0);
	if (addr == (bus_addr_t)-1 ||
	    bus_space_map(sc->sc_iot, addr, PAGE_SIZE,
	    BUS_SPACE_MAP_CACHEABLE, &sc->sc_in_ioh)) {
		printf(": can't map shared memory\n");
		return;
	}
	addr = smc_call(AML_SM_GET_SHARE_MEM_OUTPUT_BASE, 0, 0, 0);
	if (addr == (bus_addr_t)-1 ||
	    bus_space_map(sc->sc_iot, addr, PAGE_SIZE,
	    BUS_SPACE_MAP_CACHEABLE, &sc->sc_out_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_in_ioh, PAGE_SIZE);
		printf(": can't map shared memory\n");
		return;
	}

	/*
	 * Version 2 gives us a 16-byte chip ID.  If that fails, fall
	 * back on version 0/1 which gives us a 12-byte chipt ID.
	 */
	info = bus_space_vaddr(sc->sc_iot, sc->sc_out_ioh);
	memset(info, 0, sizeof(struct aml_cpu_info));
	ret = smc_call(AML_SM_GET_CHIP_ID, 2, 0, 0);
	if (ret == -1)
		ret = smc_call(AML_SM_GET_CHIP_ID, 0, 0, 0);

	printf(": ver %u\n", info->version);
	
	if (ret != -1) {
		for (i = 0; i < nitems(info->chip_id); i++)
			enqueue_randomness(info->chip_id[i]);
	}
}
