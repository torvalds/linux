/*-
 * Copyright (c) 2017-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * Cadence Quad SPI Flash Controller driver.
 * 4B-addressing mode supported only.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <geom/geom_disk.h>

#include <machine/bus.h>

#include <dev/fdt/simplebus.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/flash/cqspi.h>
#include <dev/flash/mx25lreg.h>
#include <dev/xdma/xdma.h>

#include "qspi_if.h"

#define CQSPI_DEBUG
#undef CQSPI_DEBUG

#ifdef CQSPI_DEBUG
#define dprintf(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
#define dprintf(fmt, ...)
#endif

#define	CQSPI_SECTORSIZE	512
#define	TX_QUEUE_SIZE		16
#define	RX_QUEUE_SIZE		16

#define	READ4(_sc, _reg) bus_read_4((_sc)->res[0], _reg)
#define	READ2(_sc, _reg) bus_read_2((_sc)->res[0], _reg)
#define	READ1(_sc, _reg) bus_read_1((_sc)->res[0], _reg)
#define	WRITE4(_sc, _reg, _val) bus_write_4((_sc)->res[0], _reg, _val)
#define	WRITE2(_sc, _reg, _val) bus_write_2((_sc)->res[0], _reg, _val)
#define	WRITE1(_sc, _reg, _val) bus_write_1((_sc)->res[0], _reg, _val)
#define	READ_DATA_4(_sc, _reg) bus_read_4((_sc)->res[1], _reg)
#define	READ_DATA_1(_sc, _reg) bus_read_1((_sc)->res[1], _reg)
#define	WRITE_DATA_4(_sc, _reg, _val) bus_write_4((_sc)->res[1], _reg, _val)
#define	WRITE_DATA_1(_sc, _reg, _val) bus_write_1((_sc)->res[1], _reg, _val)

struct cqspi_softc {
	device_t		dev;

	struct resource		*res[3];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih;
	uint8_t			read_op_done;
	uint8_t			write_op_done;

	uint32_t		fifo_depth;
	uint32_t		fifo_width;
	uint32_t		trigger_address;
	uint32_t		sram_phys;

	/* xDMA */
	xdma_controller_t	*xdma_tx;
	xdma_channel_t		*xchan_tx;
	void			*ih_tx;

	xdma_controller_t	*xdma_rx;
	xdma_channel_t		*xchan_rx;
	void			*ih_rx;

	struct intr_config_hook	config_intrhook;
	struct mtx		sc_mtx;
};

#define	CQSPI_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	CQSPI_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)
#define CQSPI_LOCK_INIT(_sc)					\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev),	\
	    "cqspi", MTX_DEF)
#define CQSPI_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define CQSPI_ASSERT_LOCKED(_sc)				\
	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define CQSPI_ASSERT_UNLOCKED(_sc)				\
	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

static struct resource_spec cqspi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{ "cdns,qspi-nor",	1 },
	{ NULL,			0 },
};

static void
cqspi_intr(void *arg)
{
	struct cqspi_softc *sc;
	uint32_t pending;

	sc = arg;

	pending = READ4(sc, CQSPI_IRQSTAT);

	dprintf("%s: IRQSTAT %x\n", __func__, pending);

	if (pending & (IRQMASK_INDOPDONE | IRQMASK_INDXFRLVL |
	    IRQMASK_INDSRAMFULL)) {
		/* TODO: PIO operation done */
	}

	WRITE4(sc, CQSPI_IRQSTAT, pending);
}

static int
cqspi_xdma_tx_intr(void *arg, xdma_transfer_status_t *status)
{
	struct xdma_transfer_status st;
	struct cqspi_softc *sc;
	struct bio *bp;
	int ret;
	int deq;

	sc = arg;

	dprintf("%s\n", __func__);

	deq = 0;

	while (1) {
		ret = xdma_dequeue_bio(sc->xchan_tx, &bp, &st);
		if (ret != 0) {
			break;
		}
		sc->write_op_done = 1;
		deq++;
	}

	if (deq > 1)
		device_printf(sc->dev,
		    "Warning: more than 1 tx bio dequeued\n");

	wakeup(&sc->xdma_tx);

	return (0);
}

static int
cqspi_xdma_rx_intr(void *arg, xdma_transfer_status_t *status)
{
	struct xdma_transfer_status st;
	struct cqspi_softc *sc;
	struct bio *bp;
	int ret;
	int deq;

	sc = arg;

	dprintf("%s\n", __func__);

	deq = 0;

	while (1) {
		ret = xdma_dequeue_bio(sc->xchan_rx, &bp, &st);
		if (ret != 0) {
			break;
		}
		sc->read_op_done = 1;
		deq++;
	}

	if (deq > 1)
		device_printf(sc->dev,
		    "Warning: more than 1 rx bio dequeued\n");

	wakeup(&sc->xdma_rx);

	return (0);
}

static int
cqspi_wait_for_completion(struct cqspi_softc *sc)
{
	int timeout;
	int i;

	timeout = 10000;

	for (i = timeout; i > 0; i--) {
		if ((READ4(sc, CQSPI_FLASHCMD) & FLASHCMD_CMDEXECSTAT) == 0) {
			break;
		}
	}

	if (i == 0) {
		device_printf(sc->dev, "%s: cmd timed out: %x\n",
		    __func__, READ4(sc, CQSPI_FLASHCMD));
		return (-1);
	}

	return (0);
}

static int
cqspi_cmd_write_addr(struct cqspi_softc *sc, uint8_t cmd,
    uint32_t addr, uint32_t len)
{
	uint32_t reg;
	int ret;

	dprintf("%s: %x\n", __func__, cmd);

	WRITE4(sc, CQSPI_FLASHCMDADDR, addr);
	reg = (cmd << FLASHCMD_CMDOPCODE_S);
	reg |= (FLASHCMD_ENCMDADDR);
	reg |= ((len - 1) << FLASHCMD_NUMADDRBYTES_S);
	WRITE4(sc, CQSPI_FLASHCMD, reg);

	reg |= FLASHCMD_EXECCMD;
	WRITE4(sc, CQSPI_FLASHCMD, reg);

	ret = cqspi_wait_for_completion(sc);

	return (ret);
}

static int
cqspi_cmd_write(struct cqspi_softc *sc, uint8_t cmd,
    uint8_t *addr, uint32_t len)
{
	uint32_t reg;
	int ret;

	reg = (cmd << FLASHCMD_CMDOPCODE_S);
	WRITE4(sc, CQSPI_FLASHCMD, reg);
	reg |= FLASHCMD_EXECCMD;
	WRITE4(sc, CQSPI_FLASHCMD, reg);

	ret = cqspi_wait_for_completion(sc);

	return (ret);
}

static int
cqspi_cmd_read(struct cqspi_softc *sc, uint8_t cmd,
    uint8_t *addr, uint32_t len)
{
	uint32_t data;
	uint32_t reg;
	uint8_t *buf;
	int ret;
	int i;

	if (len > 8) {
		device_printf(sc->dev, "Failed to read data\n");
		return (-1);
	}

	dprintf("%s: %x\n", __func__, cmd);

	buf = (uint8_t *)addr;

	reg = (cmd << FLASHCMD_CMDOPCODE_S);
	reg |= ((len - 1) << FLASHCMD_NUMRDDATABYTES_S);
	reg |= FLASHCMD_ENRDDATA;
	WRITE4(sc, CQSPI_FLASHCMD, reg);

	reg |= FLASHCMD_EXECCMD;
	WRITE4(sc, CQSPI_FLASHCMD, reg);

	ret = cqspi_wait_for_completion(sc);
	if (ret != 0) {
		device_printf(sc->dev, "%s: cmd failed: %x\n",
		    __func__, cmd);
		return (ret);
	}

	data = READ4(sc, CQSPI_FLASHCMDRDDATALO);

	for (i = 0; i < len; i++)
		buf[i] = (data >> (i * 8)) & 0xff;

	return (0);
}

static int
cqspi_wait_ready(struct cqspi_softc *sc)
{
	uint8_t data;
	int ret;

	do {
		ret = cqspi_cmd_read(sc, CMD_READ_STATUS, &data, 1);
	} while (data & STATUS_WIP);

	return (0);
}

static int
cqspi_write_reg(device_t dev, device_t child,
    uint8_t opcode, uint8_t *addr, uint32_t len)
{
	struct cqspi_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	ret = cqspi_cmd_write(sc, opcode, addr, len);

	return (ret);
}

static int
cqspi_read_reg(device_t dev, device_t child,
    uint8_t opcode, uint8_t *addr, uint32_t len)
{
	struct cqspi_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	ret = cqspi_cmd_read(sc, opcode, addr, len);

	return (ret);
}

static int
cqspi_wait_idle(struct cqspi_softc *sc)
{
	uint32_t reg;

	do {
		reg = READ4(sc, CQSPI_CFG);
		if (reg & CFG_IDLE) {
			break;
		}
	} while (1);

	return (0);
}

static int
cqspi_erase(device_t dev, device_t child, off_t offset)
{
	struct cqspi_softc *sc;
	int ret;

	sc = device_get_softc(dev);

	cqspi_wait_idle(sc);
	cqspi_wait_ready(sc);
	ret = cqspi_cmd_write(sc, CMD_WRITE_ENABLE, 0, 0);

	cqspi_wait_idle(sc);
	cqspi_wait_ready(sc);
	ret = cqspi_cmd_write_addr(sc, CMD_QUAD_SECTOR_ERASE, offset, 4);

	cqspi_wait_idle(sc);

	return (0);
}

static int
cqspi_write(device_t dev, device_t child, struct bio *bp,
    off_t offset, caddr_t data, off_t count)
{
	struct cqspi_softc *sc;
	uint32_t reg;

	dprintf("%s: offset 0x%llx count %lld bytes\n",
	    __func__, offset, count);

	sc = device_get_softc(dev);

	cqspi_wait_ready(sc);
	reg = cqspi_cmd_write(sc, CMD_WRITE_ENABLE, 0, 0);

	cqspi_wait_idle(sc);
	cqspi_wait_ready(sc);
	cqspi_wait_idle(sc);

	reg = DMAPER_NUMSGLREQBYTES_4;
	reg |= DMAPER_NUMBURSTREQBYTES_4;
	WRITE4(sc, CQSPI_DMAPER, reg);

	WRITE4(sc, CQSPI_INDWRWATER, 64);
	WRITE4(sc, CQSPI_INDWR, INDRD_IND_OPS_DONE_STATUS);
	WRITE4(sc, CQSPI_INDWR, 0);

	WRITE4(sc, CQSPI_INDWRCNT, count);
	WRITE4(sc, CQSPI_INDWRSTADDR, offset);

	reg = (0 << DEVWR_DUMMYWRCLKS_S);
	reg |= DEVWR_DATA_WIDTH_QUAD;
	reg |= DEVWR_ADDR_WIDTH_SINGLE;
	reg |= (CMD_QUAD_PAGE_PROGRAM << DEVWR_WROPCODE_S);
	WRITE4(sc, CQSPI_DEVWR, reg);

	reg = DEVRD_DATA_WIDTH_QUAD;
	reg |= DEVRD_ADDR_WIDTH_SINGLE;
	reg |= DEVRD_INST_WIDTH_SINGLE;
	WRITE4(sc, CQSPI_DEVRD, reg);

	xdma_enqueue_bio(sc->xchan_tx, &bp,
	    sc->sram_phys, 4, 4, XDMA_MEM_TO_DEV);
	xdma_queue_submit(sc->xchan_tx);

	sc->write_op_done = 0;

	WRITE4(sc, CQSPI_INDWR, INDRD_START);

	while (sc->write_op_done == 0)
		tsleep(&sc->xdma_tx, PCATCH | PZERO, "spi", hz/2);

	cqspi_wait_idle(sc);

	return (0);
}

static int
cqspi_read(device_t dev, device_t child, struct bio *bp,
    off_t offset, caddr_t data, off_t count)
{
	struct cqspi_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	dprintf("%s: offset 0x%llx count %lld bytes\n",
	    __func__, offset, count);

	cqspi_wait_idle(sc);

	reg = DMAPER_NUMSGLREQBYTES_4;
	reg |= DMAPER_NUMBURSTREQBYTES_4;
	WRITE4(sc, CQSPI_DMAPER, reg);

	WRITE4(sc, CQSPI_INDRDWATER, 64);
	WRITE4(sc, CQSPI_INDRD, INDRD_IND_OPS_DONE_STATUS);
	WRITE4(sc, CQSPI_INDRD, 0);

	WRITE4(sc, CQSPI_INDRDCNT, count);
	WRITE4(sc, CQSPI_INDRDSTADDR, offset);

	reg = (0 << DEVRD_DUMMYRDCLKS_S);
	reg |= DEVRD_DATA_WIDTH_QUAD;
	reg |= DEVRD_ADDR_WIDTH_SINGLE;
	reg |= DEVRD_INST_WIDTH_SINGLE;
	reg |= DEVRD_ENMODEBITS;
	reg |= (CMD_READ_4B_QUAD_OUTPUT << DEVRD_RDOPCODE_S);
	WRITE4(sc, CQSPI_DEVRD, reg);

	WRITE4(sc, CQSPI_MODEBIT, 0xff);
	WRITE4(sc, CQSPI_IRQMASK, 0);

	xdma_enqueue_bio(sc->xchan_rx, &bp, sc->sram_phys, 4, 4,
	    XDMA_DEV_TO_MEM);
	xdma_queue_submit(sc->xchan_rx);

	sc->read_op_done = 0;

	WRITE4(sc, CQSPI_INDRD, INDRD_START);

	while (sc->read_op_done == 0)
		tsleep(&sc->xdma_rx, PCATCH | PZERO, "spi", hz/2);

	cqspi_wait_idle(sc);

	return (0);
}

static int
cqspi_init(struct cqspi_softc *sc)
{
	pcell_t dts_value[1];
	phandle_t node;
	uint32_t reg;
	int len;

	device_printf(sc->dev, "Module ID %x\n",
	    READ4(sc, CQSPI_MODULEID));

	if ((node = ofw_bus_get_node(sc->dev)) == -1) {
		return (ENXIO);
	}

	if ((len = OF_getproplen(node, "cdns,fifo-depth")) <= 0) {
		return (ENXIO);
	}
	OF_getencprop(node, "cdns,fifo-depth", dts_value, len);
	sc->fifo_depth = dts_value[0];

	if ((len = OF_getproplen(node, "cdns,fifo-width")) <= 0) {
		return (ENXIO);
	}
	OF_getencprop(node, "cdns,fifo-width", dts_value, len);
	sc->fifo_width = dts_value[0];

	if ((len = OF_getproplen(node, "cdns,trigger-address")) <= 0) {
		return (ENXIO);
	}
	OF_getencprop(node, "cdns,trigger-address", dts_value, len);
	sc->trigger_address = dts_value[0];

	/* Disable controller */
	reg = READ4(sc, CQSPI_CFG);
	reg &= ~(CFG_EN);
	WRITE4(sc, CQSPI_CFG, reg);

	reg = READ4(sc, CQSPI_DEVSZ);
	reg &= ~(DEVSZ_NUMADDRBYTES_M);
	reg |= ((4 - 1) - DEVSZ_NUMADDRBYTES_S);
	WRITE4(sc, CQSPI_DEVSZ, reg);

	WRITE4(sc, CQSPI_SRAMPART, sc->fifo_depth/2);

	/* TODO: calculate baud rate and delay values. */

	reg = READ4(sc, CQSPI_CFG);
	/* Configure baud rate */
	reg &= ~(CFG_BAUD_M);
	reg |= CFG_BAUD12;
	reg |= CFG_ENDMA;
	WRITE4(sc, CQSPI_CFG, reg);

	reg = (3 << DELAY_NSS_S);
	reg |= (3  << DELAY_BTWN_S);
	reg |= (1 << DELAY_AFTER_S);
	reg |= (1 << DELAY_INIT_S);
	WRITE4(sc, CQSPI_DELAY, reg);

	READ4(sc, CQSPI_RDDATACAP);
	reg &= ~(RDDATACAP_DELAY_M);
	reg |= (1 << RDDATACAP_DELAY_S);
	WRITE4(sc, CQSPI_RDDATACAP, reg);

	/* Enable controller */
	reg = READ4(sc, CQSPI_CFG);
	reg |= (CFG_EN);
	WRITE4(sc, CQSPI_CFG, reg);

	return (0);
}

static int
cqspi_add_devices(device_t dev)
{
	phandle_t child, node;
	device_t child_dev;
	int error;

	node = ofw_bus_get_node(dev);

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		child_dev =
		    simplebus_add_device(dev, child, 0, NULL, -1, NULL);
		if (child_dev == NULL) {
			return (ENXIO);
		}

		error = device_probe_and_attach(child_dev);
		if (error != 0) {
			printf("can't probe and attach: %d\n", error);
		}
	}

	return (0);
}

static void
cqspi_delayed_attach(void *arg)
{
	struct cqspi_softc *sc;

	sc = arg;

	cqspi_add_devices(sc->dev);
	bus_generic_attach(sc->dev);

	config_intrhook_disestablish(&sc->config_intrhook);
}

static int
cqspi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev)) {
		return (ENXIO);
	}

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
		return (ENXIO);
	}

	device_set_desc(dev, "Cadence Quad SPI controller");

	return (0);
}

static int
cqspi_attach(device_t dev)
{
	struct cqspi_softc *sc;
	uint32_t caps;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, cqspi_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	sc->sram_phys = rman_get_start(sc->res[1]);

	/* Setup interrupt handlers */
	if (bus_setup_intr(sc->dev, sc->res[2], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, cqspi_intr, sc, &sc->ih)) {
		device_printf(sc->dev, "Unable to setup intr\n");
		return (ENXIO);
	}

	CQSPI_LOCK_INIT(sc);

	caps = 0;

	/* Get xDMA controller. */
	sc->xdma_tx = xdma_ofw_get(sc->dev, "tx");
	if (sc->xdma_tx == NULL) {
		device_printf(dev, "Can't find DMA controller.\n");
		return (ENXIO);
	}

	sc->xdma_rx = xdma_ofw_get(sc->dev, "rx");
	if (sc->xdma_rx == NULL) {
		device_printf(dev, "Can't find DMA controller.\n");
		return (ENXIO);
	}

	/* Alloc xDMA virtual channels. */
	sc->xchan_tx = xdma_channel_alloc(sc->xdma_tx, caps);
	if (sc->xchan_tx == NULL) {
		device_printf(dev, "Can't alloc virtual DMA channel.\n");
		return (ENXIO);
	}

	sc->xchan_rx = xdma_channel_alloc(sc->xdma_rx, caps);
	if (sc->xchan_rx == NULL) {
		device_printf(dev, "Can't alloc virtual DMA channel.\n");
		return (ENXIO);
	}

	/* Setup xDMA interrupt handlers. */
	error = xdma_setup_intr(sc->xchan_tx, cqspi_xdma_tx_intr,
	    sc, &sc->ih_tx);
	if (error) {
		device_printf(sc->dev,
		    "Can't setup xDMA interrupt handler.\n");
		return (ENXIO);
	}

	error = xdma_setup_intr(sc->xchan_rx, cqspi_xdma_rx_intr,
	    sc, &sc->ih_rx);
	if (error) {
		device_printf(sc->dev,
		    "Can't setup xDMA interrupt handler.\n");
		return (ENXIO);
	}

	xdma_prep_sg(sc->xchan_tx, TX_QUEUE_SIZE, MAXPHYS, 8, 16, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR);
	xdma_prep_sg(sc->xchan_rx, TX_QUEUE_SIZE, MAXPHYS, 8, 16, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR);

	cqspi_init(sc);

	sc->config_intrhook.ich_func = cqspi_delayed_attach;
	sc->config_intrhook.ich_arg = sc;
	if (config_intrhook_establish(&sc->config_intrhook) != 0) {
		device_printf(dev, "config_intrhook_establish failed\n");
		return (ENOMEM);
	}

	return (0);
}

static int
cqspi_detach(device_t dev)
{

	return (ENXIO);
}

static device_method_t cqspi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cqspi_probe),
	DEVMETHOD(device_attach,	cqspi_attach),
	DEVMETHOD(device_detach,	cqspi_detach),

	/* Quad SPI Flash Interface */
	DEVMETHOD(qspi_read_reg,	cqspi_read_reg),
	DEVMETHOD(qspi_write_reg,	cqspi_write_reg),
	DEVMETHOD(qspi_read,		cqspi_read),
	DEVMETHOD(qspi_write,		cqspi_write),
	DEVMETHOD(qspi_erase,		cqspi_erase),

	{ 0, 0 }
};

static devclass_t cqspi_devclass;

DEFINE_CLASS_1(cqspi, cqspi_driver, cqspi_methods,
    sizeof(struct cqspi_softc), simplebus_driver);

DRIVER_MODULE(cqspi, simplebus, cqspi_driver, cqspi_devclass, 0, 0);
