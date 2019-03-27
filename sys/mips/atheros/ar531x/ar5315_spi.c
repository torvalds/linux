/*-
 * Copyright (c) 2016, Hiroki Mori
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>
#include "spibus_if.h"

#include <mips/atheros/ar531x/arspireg.h>
#include <mips/atheros/ar531x/ar5315reg.h>

#undef AR531X_SPI_DEBUG
#ifdef AR531X_SPI_DEBUG
#define dprintf printf
#else
#define dprintf(x, arg...)
#endif

/*
 * register space access macros
 */
#define SPI_WRITE(sc, reg, val)	do {	\
		bus_write_4(sc->sc_mem_res, (reg), (val)); \
	} while (0)

#define SPI_READ(sc, reg)	 bus_read_4(sc->sc_mem_res, (reg))

#define SPI_SET_BITS(sc, reg, bits)	\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) | (bits))

#define SPI_CLEAR_BITS(sc, reg, bits)	\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) & ~(bits))

struct ar5315_spi_softc {
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	uint32_t		sc_reg_ctrl;
	uint32_t		sc_debug;
};

static void
ar5315_spi_attach_sysctl(device_t dev)
{
	struct ar5315_spi_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLFLAG_RW, &sc->sc_debug, 0,
		"ar5315_spi debugging flags");
}

static int
ar5315_spi_probe(device_t dev)
{
	device_set_desc(dev, "AR5315 SPI");
	return (0);
}

static int
ar5315_spi_attach(device_t dev)
{
	struct ar5315_spi_softc *sc = device_get_softc(dev);
	int rid;

	sc->sc_dev = dev;
        rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "Could not map memory\n");
		return (ENXIO);
	}

	device_add_child(dev, "spibus", -1);
	ar5315_spi_attach_sysctl(dev);

	return (bus_generic_attach(dev));
}

static void
ar5315_spi_chip_activate(struct ar5315_spi_softc *sc, int cs)
{
}

static void
ar5315_spi_chip_deactivate(struct ar5315_spi_softc *sc, int cs)
{
}

static int
ar5315_spi_get_block(off_t offset, caddr_t data, off_t count)
{
	int i;
	for(i = 0; i < count / 4; ++i) {
		*((uint32_t *)data + i) = ATH_READ_REG(AR5315_MEM1_BASE + offset + i * 4);
	}
//	printf("ar5315_spi_get_blockr: %x %x %x\n", 
//		(int)offset, (int)count, *(uint32_t *)data);
	return (0);
}

static int
ar5315_spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct ar5315_spi_softc *sc;
	uint8_t *buf_in, *buf_out;
	int lin, lout;
	uint32_t ctl, cnt, op, rdat, cs;
	int i, j;

	sc = device_get_softc(dev);

	if (sc->sc_debug & 0x8000)
		printf("ar5315_spi_transfer: CMD ");

	spibus_get_cs(child, &cs);

	cs &= ~SPIBUS_CS_HIGH;

	/* Open SPI controller interface */
	ar5315_spi_chip_activate(sc, cs);

	do {
		ctl = SPI_READ(sc, ARSPI_REG_CTL);
	} while (ctl & ARSPI_CTL_BUSY);

	/*
	 * Transfer command
	 */
	buf_out = (uint8_t *)cmd->tx_cmd;
	op = buf_out[0];
	if(op == 0x0b) {
		int offset = buf_out[1] << 16 | buf_out[2] << 8 | buf_out[3];
		ar5315_spi_get_block(offset, cmd->rx_data, cmd->rx_data_sz);
		return (0);
	}
	do {
		ctl = SPI_READ(sc, ARSPI_REG_CTL);
	} while (ctl & ARSPI_CTL_BUSY);
	if (sc->sc_debug & 0x8000) {
		printf("%08x ", op);
		printf("tx_cmd_sz=%d rx_cmd_sz=%d ", cmd->tx_cmd_sz,
			cmd->rx_cmd_sz);
		if(cmd->tx_cmd_sz != 1) {
			printf("%08x ", *((uint32_t *)cmd->tx_cmd));
			printf("%08x ", *((uint32_t *)cmd->tx_cmd + 1));
		}
	}
	SPI_WRITE(sc, ARSPI_REG_OPCODE, op);

	/* clear all of the tx and rx bits */
	ctl &= ~(ARSPI_CTL_TXCNT_MASK | ARSPI_CTL_RXCNT_MASK);

	/* now set txcnt */
	cnt = 1;

	ctl |= (cnt << ARSPI_CTL_TXCNT_SHIFT);

	cnt = 24;
	/* now set txcnt */
	if(cmd->rx_cmd_sz < 24)
		cnt = cmd->rx_cmd_sz;
	ctl |= (cnt << ARSPI_CTL_RXCNT_SHIFT);

	ctl |= ARSPI_CTL_START;

	SPI_WRITE(sc, ARSPI_REG_CTL, ctl);

	if(op == 0x0b)
		SPI_WRITE(sc, ARSPI_REG_DATA, 0);
	if (sc->sc_debug & 0x8000)
		printf("\nDATA ");
	/*
	 * Receive/transmit data (depends on  command)
	 */
//	buf_out = (uint8_t *)cmd->tx_data;
	buf_in = (uint8_t *)cmd->rx_cmd;
//	lout = cmd->tx_data_sz;
	lin = cmd->rx_cmd_sz;
	if (sc->sc_debug & 0x8000)
		printf("t%d r%d ", lout, lin);
	for(i = 0; i <= (cnt - 1) / 4; ++i) {
		do {
			ctl = SPI_READ(sc, ARSPI_REG_CTL);
		} while (ctl & ARSPI_CTL_BUSY);

		rdat = SPI_READ(sc, ARSPI_REG_DATA);
		if (sc->sc_debug & 0x8000)
			printf("I%08x ", rdat);

		for(j = 0; j < 4; ++j) {
			buf_in[i * 4 + j + 1] = 0xff & (rdat >> (8 * j));
			if(i * 4 + j  + 2 == cnt)
				break;
		}
	}

	ar5315_spi_chip_deactivate(sc, cs);
	/*
	 * Close SPI controller interface, restore flash memory mapped access.
	 */
	if (sc->sc_debug & 0x8000)
		printf("\n");

	return (0);
}

static int
ar5315_spi_detach(device_t dev)
{
	struct ar5315_spi_softc *sc = device_get_softc(dev);

	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (0);
}

static device_method_t ar5315_spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ar5315_spi_probe),
	DEVMETHOD(device_attach,	ar5315_spi_attach),
	DEVMETHOD(device_detach,	ar5315_spi_detach),

	DEVMETHOD(spibus_transfer,	ar5315_spi_transfer),
//	DEVMETHOD(spibus_get_block,	ar5315_spi_get_block),

	DEVMETHOD_END
};

static driver_t ar5315_spi_driver = {
	"spi",
	ar5315_spi_methods,
	sizeof(struct ar5315_spi_softc),
};

static devclass_t ar5315_spi_devclass;

DRIVER_MODULE(ar5315_spi, nexus, ar5315_spi_driver, ar5315_spi_devclass, 0, 0);
