/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Serial Peripheral Interface (SPI)
 * Chapter 47, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/spibus/spi.h>
#include <dev/spibus/spibusvar.h>

#include "spibus_if.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>

#define	SPI_FIFO_SIZE	4

#define	SPI_MCR		0x00		/* Module Configuration */
#define	 MCR_MSTR	(1 << 31)	/* Master/Slave Mode Select */
#define	 MCR_CONT_SCKE	(1 << 30)	/* Continuous SCK Enable */
#define	 MCR_FRZ	(1 << 27)	/* Freeze */
#define	 MCR_PCSIS_S	16		/* Peripheral Chip Select */
#define	 MCR_PCSIS_M	0x3f
#define	 MCR_MDIS	(1 << 14)	/* Module Disable */
#define	 MCR_CLR_TXF	(1 << 11)	/* Clear TX FIFO */
#define	 MCR_CLR_RXF	(1 << 10)	/* Clear RX FIFO */
#define	 MCR_HALT	(1 << 0)	/* Starts and stops SPI transfers */
#define	SPI_TCR		0x08		/* Transfer Count */
#define	SPI_CTAR0	0x0C		/* Clock and Transfer Attributes */
#define	SPI_CTAR0_SLAVE	0x0C		/* Clock and Transfer Attributes */
#define	SPI_CTAR1	0x10		/* Clock and Transfer Attributes */
#define	SPI_CTAR2	0x14		/* Clock and Transfer Attributes */
#define	SPI_CTAR3	0x18		/* Clock and Transfer Attributes */
#define	 CTAR_FMSZ_M	0xf
#define	 CTAR_FMSZ_S	27		/* Frame Size */
#define	 CTAR_FMSZ_8	0x7		/* 8 bits */
#define	 CTAR_CPOL	(1 << 26)	/* Clock Polarity */
#define	 CTAR_CPHA	(1 << 25)	/* Clock Phase */
#define	 CTAR_LSBFE	(1 << 24)	/* Less significant bit first */
#define	 CTAR_PCSSCK_M	0x3
#define	 CTAR_PCSSCK_S	22		/* PCS to SCK Delay Prescaler */
#define	 CTAR_PBR_M	0x3
#define	 CTAR_PBR_S	16		/* Baud Rate Prescaler */
#define	 CTAR_PBR_7	0x3		/* Divide by 7 */
#define	 CTAR_CSSCK_M	0xf
#define	 CTAR_CSSCK_S	12		/* PCS to SCK Delay Scaler */
#define	 CTAR_BR_M	0xf
#define	 CTAR_BR_S	0		/* Baud Rate Scaler */
#define	SPI_SR		0x2C		/* Status Register */
#define	 SR_TCF		(1 << 31)	/* Transfer Complete Flag */
#define	 SR_EOQF	(1 << 28)	/* End of Queue Flag */
#define	 SR_TFFF	(1 << 25)	/* Transmit FIFO Fill Flag */
#define	 SR_RFDF	(1 << 17)	/* Receive FIFO Drain Flag */
#define	SPI_RSER	0x30		/* DMA/Interrupt Select */
#define	 RSER_EOQF_RE	(1 << 28)	/* Finished Request Enable */
#define	SPI_PUSHR	0x34		/* PUSH TX FIFO In Master Mode */
#define	 PUSHR_CONT	(1 << 31)	/* Continuous Peripheral CS */
#define	 PUSHR_EOQ	(1 << 27)	/* End Of Queue */
#define	 PUSHR_CTCNT	(1 << 26)	/* Clear Transfer Counter */
#define	 PUSHR_PCS_M	0x3f
#define	 PUSHR_PCS_S	16		/* Select PCS signals */

#define	SPI_PUSHR_SLAVE	0x34	/* PUSH TX FIFO Register In Slave Mode */
#define	SPI_POPR	0x38	/* POP RX FIFO Register */
#define	SPI_TXFR0	0x3C	/* Transmit FIFO Registers */
#define	SPI_TXFR1	0x40
#define	SPI_TXFR2	0x44
#define	SPI_TXFR3	0x48
#define	SPI_RXFR0	0x7C	/* Receive FIFO Registers */
#define	SPI_RXFR1	0x80
#define	SPI_RXFR2	0x84
#define	SPI_RXFR3	0x88

struct spi_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih;
};

static struct resource_spec spi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
spi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-spi"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Serial Peripheral Interface");
	return (BUS_PROBE_DEFAULT);
}

static int
spi_attach(device_t dev)
{
	struct spi_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, spi_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	reg = READ4(sc, SPI_MCR);
	reg |= MCR_MSTR;
	reg &= ~(MCR_CONT_SCKE | MCR_MDIS | MCR_FRZ);
	reg &= ~(MCR_PCSIS_M << MCR_PCSIS_S);
	reg |= (MCR_PCSIS_M << MCR_PCSIS_S);	/* PCS Active low */
	reg |= (MCR_CLR_TXF | MCR_CLR_RXF);
	WRITE4(sc, SPI_MCR, reg);

	reg = READ4(sc, SPI_RSER);
	reg |= RSER_EOQF_RE;
	WRITE4(sc, SPI_RSER, reg);

	reg = READ4(sc, SPI_MCR);
	reg &= ~MCR_HALT;
	WRITE4(sc, SPI_MCR, reg);

	reg = READ4(sc, SPI_CTAR0);
	reg &= ~(CTAR_FMSZ_M << CTAR_FMSZ_S);
	reg |= (CTAR_FMSZ_8 << CTAR_FMSZ_S);
	/*
	 * TODO: calculate BR
	 * SCK baud rate = ( fsys / PBR ) * (1 + DBR) / BR
	 *
	 * reg &= ~(CTAR_BR_M << CTAR_BR_S);
	 */
	reg &= ~CTAR_CPOL; /* Polarity */
	reg |= CTAR_CPHA;
	/*
	 * Set LSB (Less significant bit first)
	 * must be used for some applications, e.g. some LCDs
	 */
	reg |= CTAR_LSBFE;
	WRITE4(sc, SPI_CTAR0, reg);

	reg = READ4(sc, SPI_CTAR0);
	reg &= ~(CTAR_PBR_M << CTAR_PBR_S);
	reg |= (CTAR_PBR_7 << CTAR_PBR_S);
	WRITE4(sc, SPI_CTAR0, reg);

	device_add_child(dev, "spibus", 0);
	return (bus_generic_attach(dev));
}

static int
spi_txrx(struct spi_softc *sc, uint8_t *out_buf,
    uint8_t *in_buf, int bufsz, int cs)
{
	uint32_t reg, wreg;
	uint32_t txcnt;
	uint32_t i;

	txcnt = 0;

	for (i = 0; i < bufsz; i++) {
		txcnt++;
		wreg = out_buf[i];
		wreg |= PUSHR_CONT;
		wreg |= (cs << PUSHR_PCS_S);
		if (i == 0)
			wreg |= PUSHR_CTCNT;
		if (i == (bufsz - 1) || txcnt == SPI_FIFO_SIZE)
			wreg |= PUSHR_EOQ;
		WRITE4(sc, SPI_PUSHR, wreg);

		if (i == (bufsz - 1) || txcnt == SPI_FIFO_SIZE) {
			txcnt = 0;

			/* Wait last entry in a queue to be transmitted */
			while((READ4(sc, SPI_SR) & SR_EOQF) == 0)
				continue;

			reg = READ4(sc, SPI_SR);
			reg |= (SR_TCF | SR_EOQF);
			WRITE4(sc, SPI_SR, reg);
		}

		/* Wait until RX FIFO is empty */
		while((READ4(sc, SPI_SR) & SR_RFDF) == 0)
			continue;

		in_buf[i] = READ1(sc, SPI_POPR);
	}

	return (0);
}

static int
spi_transfer(device_t dev, device_t child, struct spi_command *cmd)
{
	struct spi_softc *sc;
	uint32_t cs;

	sc = device_get_softc(dev);

	KASSERT(cmd->tx_cmd_sz == cmd->rx_cmd_sz,
	    ("%s: TX/RX command sizes should be equal", __func__));
	KASSERT(cmd->tx_data_sz == cmd->rx_data_sz,
	    ("%s: TX/RX data sizes should be equal", __func__));

	/* get the proper chip select */
	spibus_get_cs(child, &cs);

	cs &= ~SPIBUS_CS_HIGH;

	/* Command */
	spi_txrx(sc, cmd->tx_cmd, cmd->rx_cmd, cmd->tx_cmd_sz, cs);

	/* Data */
	spi_txrx(sc, cmd->tx_data, cmd->rx_data, cmd->tx_data_sz, cs);

	return (0);
}

static device_method_t spi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		spi_probe),
	DEVMETHOD(device_attach,	spi_attach),
	/* SPI interface */
	DEVMETHOD(spibus_transfer,	spi_transfer),
	{ 0, 0 }
};

static driver_t spi_driver = {
	"spi",
	spi_methods,
	sizeof(struct spi_softc),
};

static devclass_t spi_devclass;

DRIVER_MODULE(spi, simplebus, spi_driver, spi_devclass, 0, 0);
