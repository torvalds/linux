/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Aleksandr Rybalko.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Pseudo driver to copy the NVRAM settings from various sources
 * into the kernel environment.
 *
 * Drivers (such as ethernet devices) can then use environment
 * variables to set default parameters.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include "nvram2env.h"

static void
nvram2env_identify(driver_t * drv, device_t parent)
{
	int i, ivar;

	for (i = 0; !resource_int_value("nvram", i, "base", &ivar); i++)
		BUS_ADD_CHILD(parent, 0, "nvram2env", i);
}

int
nvram2env_probe(device_t dev)
{
	uint32_t i, ivar, sig;
	struct nvram2env_softc * sc = device_get_softc(dev);

	/*
	 * Please ensure that your implementation of NVRAM->ENV specifies
	 * bus tag
	 */
	if (sc->bst == NULL)
		return (ENXIO);

	if (sc->sig == 0)
		if (resource_int_value("nvram", device_get_unit(dev), "sig",
		    &sc->sig) != 0 || sc->sig == 0)
			sc->sig = CFE_NVRAM_SIGNATURE;

	if (sc->maxsize == 0)
		if (resource_int_value("nvram", device_get_unit(dev), "maxsize",
		    &sc->maxsize) != 0 || sc->maxsize == 0)
			sc->maxsize = NVRAM_MAX_SIZE;

	if (sc->flags == 0)
		if (resource_int_value("nvram", device_get_unit(dev), "flags",
		    &sc->flags) != 0 || sc->flags == 0)
			sc->flags = NVRAM_FLAGS_GENERIC;


	for (i = 0; i < 2; i ++)
	{
		switch (i) {
		case 0:
			break;
		case 1:
		case 2:
			if (resource_int_value("nvram", device_get_unit(dev),
			    (i == 1) ? "base" : "fallbackbase", &ivar) != 0 ||
			    ivar == 0)
				continue;

			sc->addr = ivar;
			break;
		default:
			break;
		}

		if (sc->addr == 0)
			continue;

		if (bootverbose)
			device_printf(dev, "base=0x%08x sig=0x%08x "
			    "maxsize=0x%08x flags=0x%08x\n",
			    sc->addr, sc->sig, sc->maxsize, sc->flags);

		if (bus_space_map(sc->bst, sc->addr, sc->maxsize, 0,
		    &sc->bsh) != 0)
			continue;

		sig = bus_space_read_4(sc->bst, sc->bsh, 0);
		if ( sig == sc->sig /*FLSH*/)
		{
			device_printf(dev, "Found NVRAM at %#x\n", 
			    (uint32_t)ivar);
			sc->need_swap = 0;
			goto unmap_done;
		}
		else if ( htole32(sig) == sc->sig /*HSLF*/)
		{
			device_printf(dev, "Found NVRAM at %#x\n", 
			    (uint32_t)ivar);
			sc->need_swap = 1;
			goto unmap_done;
		} else if (sc->flags & NVRAM_FLAGS_UBOOT) {
			device_printf(dev, "Use NVRAM at %#x\n", 
			    (uint32_t)ivar);
			sc->crc = sig;
			goto unmap_done;
		}
		bus_space_unmap(sc->bst, sc->bsh, NVRAM_MAX_SIZE);
	}
	sc->bst = 0;
	sc->bsh = 0;
	sc->addr = 0;
	return (ENXIO);

unmap_done:
	bus_space_unmap(sc->bst, sc->bsh, NVRAM_MAX_SIZE);
	device_set_desc(dev, "NVRAM to ENV pseudo-device");
	return (BUS_PROBE_SPECIFIC);

}

static uint32_t read_4(struct nvram2env_softc * sc, int offset) 
{
	if (sc->need_swap) 
		return (bswap32(bus_space_read_4(sc->bst, sc->bsh, offset)));
	else
		return (bus_space_read_4(sc->bst, sc->bsh, offset));
}


int
nvram2env_attach(device_t dev)
{
	struct nvram2env_softc 	*sc;
	struct nvram 		*nv;
	char *pair, *value, *assign;
	uint32_t sig, size, i, *tmp;

	sc = device_get_softc(dev);

	if (sc->bst == 0 || sc->addr == 0)
		return (ENXIO);

	if (bus_space_map(sc->bst, sc->addr, NVRAM_MAX_SIZE, 0,
		&sc->bsh) != 0)
		return (ENXIO);

	sig  = read_4(sc, 0);
	size = read_4(sc, 4);

	if (bootverbose)
		device_printf(dev, " size=0x%05x maxsize=0x%05x\n", size,
				sc->maxsize);

	size = (size > sc->maxsize)?sc->maxsize:size;


	if (sig == sc->sig || (sc->flags & NVRAM_FLAGS_UBOOT))
	{

		/* align size to 32bit size*/
		size += 3;
		size &= ~3;

		nv = malloc(size, M_DEVBUF, M_WAITOK | M_ZERO);
		if (!nv)
			return (ENOMEM);
		/* set tmp pointer to begin of NVRAM */
		tmp = (uint32_t *) nv;

		/* use read_4 to swap bytes if it's required */
		for (i = 0; i < size; i += 4) {
			*tmp = read_4(sc, i);
			tmp++;
		}
		/* now tmp pointer is end of NVRAM */

		if (sc->flags & NVRAM_FLAGS_BROADCOM) {
			device_printf(dev, "sig = %#x\n",  nv->sig);
			device_printf(dev, "size = %#x\n", nv->size);
		}

		if (!(sc->flags & NVRAM_FLAGS_NOCHECK)) {
			/* TODO: need checksum verification */
		}

		if (sc->flags & NVRAM_FLAGS_GENERIC)
			pair = (char*)nv+4;
		if (sc->flags & NVRAM_FLAGS_UBOOT)
			pair = (char*)nv+4;
		else if (sc->flags & NVRAM_FLAGS_BROADCOM)
			pair = (char*)nv+20;
		else
			pair = (char*)nv+4;

		/* iterate over buffer till end. tmp points to end of NVRAM */
		for ( ; pair < (char*)tmp; 
		    pair += strlen(pair) + strlen(value) + 2 ) {

			if (!pair || (strlen(pair) == 0))
				break;

			/* hint.nvram.0. */
			assign = strchr(pair,'=');
			assign[0] = '\0';
			value = assign+1;

			if (bootverbose)
				printf("ENV[%p]: %s=%s\n",
				    (void*)((char*)pair - (char*)nv),
				    pair, value);

			kern_setenv(pair, value);

			if (strcasecmp(pair, "WAN_MAC_ADDR") == 0) {
				/* Alias for MAC address of eth0 */
				if (bootverbose)
					printf("ENV: aliasing "
					    "WAN_MAC_ADDR to ethaddr"
					    " = %s\n",  value);
				kern_setenv("ethaddr", value);
			}
			else if (strcasecmp(pair, "LAN_MAC_ADDR") == 0){
				/* Alias for MAC address of eth1 */
				if (bootverbose)
					printf("ENV: aliasing "
					    "LAN_MAC_ADDR to eth1addr"
					    " = %s\n",  value);
				kern_setenv("eth1addr", value);
			}

			if (strcmp(pair, "bootverbose") == 0)
				bootverbose = strtoul(value, 0, 0);
			if (strcmp(pair, "boothowto"  ) == 0)
				boothowto   = strtoul(value, 0, 0);

		}
		free(nv, M_DEVBUF);
	}

	bus_space_unmap(sc->bst, sc->bsh, NVRAM_MAX_SIZE);

	return (0);
}

static device_method_t nvram2env_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify, 	nvram2env_identify),
	DEVMETHOD(device_probe,		nvram2env_probe),
	DEVMETHOD(device_attach,	nvram2env_attach),

	DEVMETHOD_END
};

driver_t nvram2env_driver = {
	"nvram2env",
	nvram2env_methods,
	sizeof(struct nvram2env_softc),
};

devclass_t nvram2env_devclass;
