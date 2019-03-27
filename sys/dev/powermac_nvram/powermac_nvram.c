/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Maxim Sobolev <sobomax@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/uio.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <dev/powermac_nvram/powermac_nvramvar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

/*
 * Device interface.
 */
static int		powermac_nvram_probe(device_t);
static int		powermac_nvram_attach(device_t);
static int		powermac_nvram_detach(device_t);

/* Helper functions */
static int		powermac_nvram_check(void *data);
static int		chrp_checksum(int sum, uint8_t *, uint8_t *);
static uint32_t		adler_checksum(uint8_t *, int);
static int		erase_bank(device_t, uint8_t *);
static int		write_bank(device_t, uint8_t *, uint8_t *);

/*
 * Driver methods.
 */
static device_method_t	powermac_nvram_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		powermac_nvram_probe),
	DEVMETHOD(device_attach,	powermac_nvram_attach),
	DEVMETHOD(device_detach,	powermac_nvram_detach),

	{ 0, 0 }
};

static driver_t	powermac_nvram_driver = {
	"powermac_nvram",
	powermac_nvram_methods,
	sizeof(struct powermac_nvram_softc)
};

static devclass_t powermac_nvram_devclass;

DRIVER_MODULE(powermac_nvram, ofwbus, powermac_nvram_driver, powermac_nvram_devclass, 0, 0);

/*
 * Cdev methods.
 */

static	d_open_t	powermac_nvram_open;
static	d_close_t	powermac_nvram_close;
static	d_read_t	powermac_nvram_read;
static	d_write_t	powermac_nvram_write;

static struct cdevsw powermac_nvram_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	powermac_nvram_open,
	.d_close =	powermac_nvram_close,
	.d_read =	powermac_nvram_read,
	.d_write =	powermac_nvram_write,
	.d_name =	"powermac_nvram",
};

static int
powermac_nvram_probe(device_t dev)
{
	const char	*type, *compatible;

	type = ofw_bus_get_type(dev);
	compatible = ofw_bus_get_compat(dev);

	if (type == NULL || compatible == NULL)
		return ENXIO;

	if (strcmp(type, "nvram") != 0)
		return ENXIO;
	if (strcmp(compatible, "amd-0137") != 0 &&
	    !ofw_bus_is_compatible(dev, "nvram,flash"))
		return ENXIO;

	device_set_desc(dev, "Apple NVRAM");
	return 0;
}

static int
powermac_nvram_attach(device_t dev)
{
	struct powermac_nvram_softc *sc;
	const char	*compatible;
	phandle_t node;
	u_int32_t reg[3];
	int gen0, gen1, i;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);

	if ((i = OF_getprop(node, "reg", reg, sizeof(reg))) < 8)
		return ENXIO;

	sc->sc_dev = dev;
	sc->sc_node = node;

	compatible = ofw_bus_get_compat(dev);
	if (strcmp(compatible, "amd-0137") == 0)
		sc->sc_type = FLASH_TYPE_AMD;
	else
		sc->sc_type = FLASH_TYPE_SM;

	/*
	 * Find which byte of reg corresponds to the 32-bit physical address.
	 * We should probably read #address-cells from /chosen instead.
	 */
	i = (i/4) - 2;

	sc->sc_bank0 = (vm_offset_t)pmap_mapdev(reg[i], NVRAM_SIZE * 2);
	sc->sc_bank1 = sc->sc_bank0 + NVRAM_SIZE;

	gen0 = powermac_nvram_check((void *)sc->sc_bank0);
	gen1 = powermac_nvram_check((void *)sc->sc_bank1);

	if (gen0 == -1 && gen1 == -1) {
		if ((void *)sc->sc_bank0 != NULL)
			pmap_unmapdev(sc->sc_bank0, NVRAM_SIZE * 2);
		device_printf(dev, "both banks appear to be corrupt\n");
		return ENXIO;
	}
	device_printf(dev, "bank0 generation %d, bank1 generation %d\n",
	    gen0, gen1);

	sc->sc_bank = (gen0 > gen1) ? sc->sc_bank0 : sc->sc_bank1;
	bcopy((void *)sc->sc_bank, (void *)sc->sc_data, NVRAM_SIZE);

	sc->sc_cdev = make_dev(&powermac_nvram_cdevsw, 0, 0, 0, 0600,
	    "powermac_nvram");
	sc->sc_cdev->si_drv1 = sc;

	return 0;
}

static int
powermac_nvram_detach(device_t dev)
{
	struct powermac_nvram_softc *sc;

	sc = device_get_softc(dev);

	if ((void *)sc->sc_bank0 != NULL)
		pmap_unmapdev(sc->sc_bank0, NVRAM_SIZE * 2);

	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
	
	return 0;
}

static int
powermac_nvram_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct powermac_nvram_softc *sc = dev->si_drv1;

	if (sc->sc_isopen)
		return EBUSY;
	sc->sc_isopen = 1;
	sc->sc_rpos = sc->sc_wpos = 0;
	return 0;
}

static int
powermac_nvram_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct powermac_nvram_softc *sc = dev->si_drv1;
	struct core99_header *header;
	vm_offset_t bank;

	if (sc->sc_wpos != sizeof(sc->sc_data)) {
		/* Short write, restore in-memory copy */
		bcopy((void *)sc->sc_bank, (void *)sc->sc_data, NVRAM_SIZE);
		sc->sc_isopen = 0;
		return 0;
	}

	header = (struct core99_header *)sc->sc_data;

	header->generation = ((struct core99_header *)sc->sc_bank)->generation;
	header->generation++;
	header->chrp_header.signature = CORE99_SIGNATURE;

	header->adler_checksum =
	    adler_checksum((uint8_t *)&(header->generation),
	    NVRAM_SIZE - offsetof(struct core99_header, generation));
	header->chrp_header.chrp_checksum = chrp_checksum(header->chrp_header.signature,
	    (uint8_t *)&(header->chrp_header.length),
	    (uint8_t *)&(header->adler_checksum));

	bank = (sc->sc_bank == sc->sc_bank0) ? sc->sc_bank1 : sc->sc_bank0;
	if (erase_bank(sc->sc_dev, (uint8_t *)bank) != 0 ||
	    write_bank(sc->sc_dev, (uint8_t *)bank, sc->sc_data) != 0) {
		sc->sc_isopen = 0;
		return ENOSPC;
	}
	sc->sc_bank = bank;
	sc->sc_isopen = 0;
	return 0;
}

static int
powermac_nvram_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int rv, amnt, data_available;
	struct powermac_nvram_softc *sc = dev->si_drv1;

	rv = 0;
	while (uio->uio_resid > 0) {
		data_available = sizeof(sc->sc_data) - sc->sc_rpos;
		if (data_available > 0) {
			amnt = MIN(uio->uio_resid, data_available);
			rv = uiomove((void *)(sc->sc_data + sc->sc_rpos),
			    amnt, uio);
			if (rv != 0)
				break;
			sc->sc_rpos += amnt;
		} else {
			break;
		}
	}
	return rv;
}

static int
powermac_nvram_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int rv, amnt, data_available;
	struct powermac_nvram_softc *sc = dev->si_drv1;

	if (sc->sc_wpos >= sizeof(sc->sc_data))
		return EINVAL;

	rv = 0;
	while (uio->uio_resid > 0) {
		data_available = sizeof(sc->sc_data) - sc->sc_wpos;
		if (data_available > 0) {
			amnt = MIN(uio->uio_resid, data_available);
			rv = uiomove((void *)(sc->sc_data + sc->sc_wpos),
			    amnt, uio);
			if (rv != 0)
				break;
			sc->sc_wpos += amnt;
		} else {
			break;
		}
	}
	return rv;
}

static int
powermac_nvram_check(void *data)
{
	struct core99_header *header;

	header = (struct core99_header *)data;

	if (header->chrp_header.signature != CORE99_SIGNATURE)
		return -1;
	if (header->chrp_header.chrp_checksum !=
	    chrp_checksum(header->chrp_header.signature,
	    (uint8_t *)&(header->chrp_header.length),
	    (uint8_t *)&(header->adler_checksum)))
		return -1;
	if (header->adler_checksum !=
	    adler_checksum((uint8_t *)&(header->generation),
	    NVRAM_SIZE - offsetof(struct core99_header, generation)))
		return -1;
	return header->generation;
}

static int
chrp_checksum(int sum, uint8_t *data, uint8_t *end)
{

	for (; data < end; data++)
		sum += data[0];
	while (sum > 0xff)
		sum = (sum & 0xff) + (sum >> 8);
	return sum;
}

static uint32_t
adler_checksum(uint8_t *data, int len)
{
	uint32_t low, high;
	int i;

	low = 1;
	high = 0;
	for (i = 0; i < len; i++) {
		if ((i % 5000) == 0) {
			high %= 65521UL;
			high %= 65521UL;
		}
		low += data[i];
		high += low;
	}
	low %= 65521UL;
	high %= 65521UL;

	return (high << 16) | low;
}

#define	OUTB_DELAY(a, v)	outb(a, v); DELAY(1);

static int
wait_operation_complete_amd(uint8_t *bank)
{
	int i;

	for (i = 1000000; i != 0; i--)
		if ((inb(bank) ^ inb(bank)) == 0)
			return 0;
	return -1;
}

static int
erase_bank_amd(device_t dev, uint8_t *bank)
{
	unsigned int i;

	/* Unlock 1 */
	OUTB_DELAY(bank + 0x555, 0xaa);
	/* Unlock 2 */
	OUTB_DELAY(bank + 0x2aa, 0x55);

	/* Sector-Erase */
	OUTB_DELAY(bank + 0x555, 0x80);
	OUTB_DELAY(bank + 0x555, 0xaa);
	OUTB_DELAY(bank + 0x2aa, 0x55);
	OUTB_DELAY(bank, 0x30);

	if (wait_operation_complete_amd(bank) != 0) {
		device_printf(dev, "flash erase timeout\n");
		return -1;
	}

	/* Reset */
	OUTB_DELAY(bank, 0xf0);

	for (i = 0; i < NVRAM_SIZE; i++) {
		if (bank[i] != 0xff) {
			device_printf(dev, "flash erase has failed\n");
			return -1;
		}
	}
	return 0;
}

static int
write_bank_amd(device_t dev, uint8_t *bank, uint8_t *data)
{
	unsigned int i;

	for (i = 0; i < NVRAM_SIZE; i++) {
		/* Unlock 1 */
		OUTB_DELAY(bank + 0x555, 0xaa);
		/* Unlock 2 */
		OUTB_DELAY(bank + 0x2aa, 0x55);

		/* Write single word */
		OUTB_DELAY(bank + 0x555, 0xa0);
		OUTB_DELAY(bank + i, data[i]);
		if (wait_operation_complete_amd(bank) != 0) {
			device_printf(dev, "flash write timeout\n");
			return -1;
		}
	}

	/* Reset */
	OUTB_DELAY(bank, 0xf0);

	for (i = 0; i < NVRAM_SIZE; i++) {
		if (bank[i] != data[i]) {
			device_printf(dev, "flash write has failed\n");
			return -1;
		}
	}
	return 0;
}

static int
wait_operation_complete_sm(uint8_t *bank)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		outb(bank, SM_FLASH_CMD_READ_STATUS);
		if (inb(bank) & SM_FLASH_STATUS_DONE)
			return (0);
	}
	return (-1);
}

static int
erase_bank_sm(device_t dev, uint8_t *bank)
{
	unsigned int i;

	outb(bank, SM_FLASH_CMD_ERASE_SETUP);
	outb(bank, SM_FLASH_CMD_ERASE_CONFIRM);

	if (wait_operation_complete_sm(bank) != 0) {
		device_printf(dev, "flash erase timeout\n");
		return (-1);
	}

	outb(bank, SM_FLASH_CMD_CLEAR_STATUS);
	outb(bank, SM_FLASH_CMD_RESET);

	for (i = 0; i < NVRAM_SIZE; i++) {
		if (bank[i] != 0xff) {
			device_printf(dev, "flash write has failed\n");
			return (-1);
		}
	}
	return (0);
}

static int
write_bank_sm(device_t dev, uint8_t *bank, uint8_t *data)
{
	unsigned int i;

	for (i = 0; i < NVRAM_SIZE; i++) {
		OUTB_DELAY(bank + i, SM_FLASH_CMD_WRITE_SETUP);
		outb(bank + i, data[i]);
		if (wait_operation_complete_sm(bank) != 0) {
			device_printf(dev, "flash write error/timeout\n");
			break;
		}
	}

	outb(bank, SM_FLASH_CMD_CLEAR_STATUS);
	outb(bank, SM_FLASH_CMD_RESET);

	for (i = 0; i < NVRAM_SIZE; i++) {
		if (bank[i] != data[i]) {
			device_printf(dev, "flash write has failed\n");
			return (-1);
		}
	}
	return (0);
}

static int
erase_bank(device_t dev, uint8_t *bank)
{
	struct powermac_nvram_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_type == FLASH_TYPE_AMD)
		return (erase_bank_amd(dev, bank));
	else
		return (erase_bank_sm(dev, bank));
}

static int
write_bank(device_t dev, uint8_t *bank, uint8_t *data)
{
	struct powermac_nvram_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_type == FLASH_TYPE_AMD)
		return (write_bank_amd(dev, bank, data));
	else
		return (write_bank_sm(dev, bank, data));
}
