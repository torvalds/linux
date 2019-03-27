/*-
 * Copyright 2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726-m8 (and later) SDXC host controller driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcbrvar.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_sdxc-m8.h>

#include "gpio_if.h"
#include "mmcbr_if.h"

/*
 * The table is sorted from highest to lowest and
 * last entry in the table is mark by freq == 0.
 */
struct {
	uint32_t	voltage;
	uint32_t	freq;
	uint32_t	rx_phase;
} aml8726_sdxc_clk_phases[] = {
	{
		MMC_OCR_LOW_VOLTAGE | MMC_OCR_320_330 | MMC_OCR_330_340,
		100000000,
		1
	},
	{
		MMC_OCR_320_330 | MMC_OCR_330_340,
		45000000,
		15
	},
	{
		MMC_OCR_LOW_VOLTAGE,
		45000000,
		11
	},
	{
		MMC_OCR_LOW_VOLTAGE | MMC_OCR_320_330 | MMC_OCR_330_340,
		24999999,
		15
	},
	{
		MMC_OCR_LOW_VOLTAGE | MMC_OCR_320_330 | MMC_OCR_330_340,
		5000000,
		23
	},
	{
		MMC_OCR_LOW_VOLTAGE | MMC_OCR_320_330 | MMC_OCR_330_340,
		1000000,
		55
	},
	{
		MMC_OCR_LOW_VOLTAGE | MMC_OCR_320_330 | MMC_OCR_330_340,
		0,
		1061
	},
};

struct aml8726_sdxc_gpio {
	device_t	dev;
	uint32_t	pin;
	uint32_t	pol;
};

struct aml8726_sdxc_softc {
	device_t		dev;
	boolean_t		auto_fill_flush;
	struct resource		*res[2];
	struct mtx		mtx;
	struct callout		ch;
	unsigned int		ref_freq;
	struct aml8726_sdxc_gpio pwr_en;
	int			voltages[2];
	struct aml8726_sdxc_gpio vselect;
	struct aml8726_sdxc_gpio card_rst;
	bus_dma_tag_t		dmatag;
	bus_dmamap_t		dmamap;
	void			*ih_cookie;
	struct mmc_host		host;
	int			bus_busy;
	struct {
		uint32_t	time;
		uint32_t	error;
	} busy;
	struct mmc_command 	*cmd;
};

static struct resource_spec aml8726_sdxc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_SDXC_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_SDXC_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_SDXC_LOCK_ASSERT(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)
#define	AML_SDXC_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "sdxc", MTX_DEF)
#define	AML_SDXC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	PIN_ON_FLAG(pol)		((pol) == 0 ? \
    GPIO_PIN_LOW : GPIO_PIN_HIGH)
#define	PIN_OFF_FLAG(pol)		((pol) == 0 ? \
    GPIO_PIN_HIGH : GPIO_PIN_LOW)

#define	msecs_to_ticks(ms)		(((ms)*hz)/1000 + 1)

static void aml8726_sdxc_timeout(void *arg);

static void
aml8726_sdxc_mapmem(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *busaddrp;

	/*
	 * There should only be one bus space address since
	 * bus_dma_tag_create was called with nsegments = 1.
	 */

	 busaddrp = (bus_addr_t *)arg;
	 *busaddrp = segs->ds_addr;
}

static int
aml8726_sdxc_power_off(struct aml8726_sdxc_softc *sc)
{

	if (sc->pwr_en.dev == NULL)
		return (0);

	return (GPIO_PIN_SET(sc->pwr_en.dev, sc->pwr_en.pin,
	    PIN_OFF_FLAG(sc->pwr_en.pol)));
}

static int
aml8726_sdxc_power_on(struct aml8726_sdxc_softc *sc)
{

	if (sc->pwr_en.dev == NULL)
		return (0);

	return (GPIO_PIN_SET(sc->pwr_en.dev, sc->pwr_en.pin,
	    PIN_ON_FLAG(sc->pwr_en.pol)));
}

static void
aml8726_sdxc_soft_reset(struct aml8726_sdxc_softc *sc)
{

	CSR_WRITE_4(sc, AML_SDXC_SOFT_RESET_REG, AML_SDXC_SOFT_RESET);
	CSR_BARRIER(sc, AML_SDXC_SOFT_RESET_REG);
	DELAY(5);
}

static void
aml8726_sdxc_engage_dma(struct aml8726_sdxc_softc *sc)
{
	int i;
	uint32_t pdmar;
	uint32_t sr;
	struct mmc_data *data;

	data = sc->cmd->data;

	if (data == NULL || data->len == 0)
		return;

	/*
	 * Engaging the DMA hardware is recommended before writing
	 * to AML_SDXC_SEND_REG so that the FIFOs are ready to go.
	 *
	 * Presumably AML_SDXC_CNTRL_REG and AML_SDXC_DMA_ADDR_REG
	 * must be set up prior to this happening.
	 */

	pdmar = CSR_READ_4(sc, AML_SDXC_PDMA_REG);

	pdmar &= ~AML_SDXC_PDMA_RX_FLUSH_MODE_SW;
	pdmar |= AML_SDXC_PDMA_DMA_EN;

	if (sc->auto_fill_flush == true) {
		CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
		CSR_BARRIER(sc, AML_SDXC_PDMA_REG);
		return;
	}

	if ((data->flags & MMC_DATA_READ) != 0) {
		pdmar |= AML_SDXC_PDMA_RX_FLUSH_MODE_SW;
		CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
		CSR_BARRIER(sc, AML_SDXC_PDMA_REG);
	} else {
		pdmar |= AML_SDXC_PDMA_TX_FILL;
		CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
		CSR_BARRIER(sc, AML_SDXC_PDMA_REG);

		/*
		 * Wait up to 100us for data to show up.
		 */
		for (i = 0; i < 100; i++) {
			sr = CSR_READ_4(sc, AML_SDXC_STATUS_REG);
			if ((sr & AML_SDXC_STATUS_TX_CNT_MASK) != 0)
				break;
			DELAY(1);
		}
		if (i >= 100)
			device_printf(sc->dev, "TX FIFO fill timeout\n");
	}
}

static void
aml8726_sdxc_disengage_dma(struct aml8726_sdxc_softc *sc)
{
	int i;
	uint32_t pdmar;
	uint32_t sr;
	struct mmc_data *data;

	data = sc->cmd->data;

	if (data == NULL || data->len == 0)
		return;

	pdmar = CSR_READ_4(sc, AML_SDXC_PDMA_REG);

	if (sc->auto_fill_flush == true) {
		pdmar &= ~AML_SDXC_PDMA_DMA_EN;
		CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
		CSR_BARRIER(sc, AML_SDXC_PDMA_REG);
		return;
	}

	if ((data->flags & MMC_DATA_READ) != 0) {
		pdmar |= AML_SDXC_PDMA_RX_FLUSH_NOW;
		CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
		CSR_BARRIER(sc, AML_SDXC_PDMA_REG);

		/*
		 * Wait up to 100us for data to drain.
		 */
		for (i = 0; i < 100; i++) {
			sr = CSR_READ_4(sc, AML_SDXC_STATUS_REG);
			if ((sr & AML_SDXC_STATUS_RX_CNT_MASK) == 0)
				break;
			DELAY(1);
		}
		if (i >= 100)
			device_printf(sc->dev, "RX FIFO drain timeout\n");
	}

	pdmar &= ~(AML_SDXC_PDMA_DMA_EN | AML_SDXC_PDMA_RX_FLUSH_MODE_SW);

	CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
	CSR_BARRIER(sc, AML_SDXC_PDMA_REG);
}

static int
aml8726_sdxc_start_command(struct aml8726_sdxc_softc *sc,
    struct mmc_command *cmd)
{
	bus_addr_t baddr;
	uint32_t block_size;
	uint32_t ctlr;
	uint32_t ier;
	uint32_t sndr;
	uint32_t timeout;
	int error;
	struct mmc_data *data;

	AML_SDXC_LOCK_ASSERT(sc);

	if (cmd->opcode > 0x3f)
		return (MMC_ERR_INVALID);

	/*
	 * Ensure the hardware state machine is in a known state.
	 */
	aml8726_sdxc_soft_reset(sc);

	sndr = cmd->opcode;

	if ((cmd->flags & MMC_RSP_136) != 0) {
		sndr |= AML_SDXC_SEND_CMD_HAS_RESP;
		sndr |= AML_SDXC_SEND_RESP_136;
		/*
		 * According to the SD spec the 136 bit response is
		 * used for getting the CID or CSD in which case the
		 * CRC7 is embedded in the contents rather than being
		 * calculated over the entire response (the controller
		 * always checks the CRC7 over the entire response).
		 */
		sndr |= AML_SDXC_SEND_RESP_NO_CRC7_CHECK;
	} else if ((cmd->flags & MMC_RSP_PRESENT) != 0)
		sndr |= AML_SDXC_SEND_CMD_HAS_RESP;

	if ((cmd->flags & MMC_RSP_CRC) == 0)
		sndr |= AML_SDXC_SEND_RESP_NO_CRC7_CHECK;

	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		sndr |= AML_SDXC_SEND_DATA_STOP;

	data = cmd->data;

	baddr = 0;
	ctlr = CSR_READ_4(sc, AML_SDXC_CNTRL_REG);
	ier = AML_SDXC_IRQ_ENABLE_STANDARD;
	timeout = AML_SDXC_CMD_TIMEOUT;

	ctlr &= ~AML_SDXC_CNTRL_PKG_LEN_MASK;

	if (data && data->len &&
	    (data->flags & (MMC_DATA_READ | MMC_DATA_WRITE)) != 0) {
		block_size = data->len;

		if ((data->flags & MMC_DATA_MULTI) != 0) {
			block_size = MMC_SECTOR_SIZE;
			if ((data->len % block_size) != 0)
				return (MMC_ERR_INVALID);
		}

		if (block_size > 512)
			return (MMC_ERR_INVALID);

		sndr |= AML_SDXC_SEND_CMD_HAS_DATA;
		sndr |= ((data->flags & MMC_DATA_WRITE) != 0) ?
		    AML_SDXC_SEND_DATA_WRITE : 0;
		sndr |= (((data->len / block_size) - 1) <<
		    AML_SDXC_SEND_REP_PKG_CNT_SHIFT);

		ctlr |= ((block_size < 512) ? block_size : 0) <<
		    AML_SDXC_CNTRL_PKG_LEN_SHIFT;

		ier &= ~AML_SDXC_IRQ_ENABLE_RESP_OK;
		ier |= (sc->auto_fill_flush == true ||
		    (data->flags & MMC_DATA_WRITE) != 0) ?
		    AML_SDXC_IRQ_ENABLE_DMA_DONE :
		    AML_SDXC_IRQ_ENABLE_TRANSFER_DONE_OK;

		error = bus_dmamap_load(sc->dmatag, sc->dmamap,
		    data->data, data->len, aml8726_sdxc_mapmem, &baddr,
		    BUS_DMA_NOWAIT);
		if (error)
			return (MMC_ERR_NO_MEMORY);

		if ((data->flags & MMC_DATA_READ) != 0) {
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_PREREAD);
			timeout = AML_SDXC_READ_TIMEOUT *
			    (data->len / block_size);
		} else {
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_PREWRITE);
			timeout = AML_SDXC_WRITE_TIMEOUT *
			    (data->len / block_size);
		}
	}

	sc->cmd = cmd;

	cmd->error = MMC_ERR_NONE;

	sc->busy.time = 0;
	sc->busy.error = MMC_ERR_NONE;

	if (timeout > AML_SDXC_MAX_TIMEOUT)
		timeout = AML_SDXC_MAX_TIMEOUT;

	callout_reset(&sc->ch, msecs_to_ticks(timeout),
	    aml8726_sdxc_timeout, sc);

	CSR_WRITE_4(sc, AML_SDXC_IRQ_ENABLE_REG, ier);

	CSR_WRITE_4(sc, AML_SDXC_CNTRL_REG, ctlr);
	CSR_WRITE_4(sc, AML_SDXC_DMA_ADDR_REG, (uint32_t)baddr);
	CSR_WRITE_4(sc, AML_SDXC_CMD_ARGUMENT_REG, cmd->arg);

	aml8726_sdxc_engage_dma(sc);

	CSR_WRITE_4(sc, AML_SDXC_SEND_REG, sndr);
	CSR_BARRIER(sc, AML_SDXC_SEND_REG);

	return (MMC_ERR_NONE);
}

static void
aml8726_sdxc_finish_command(struct aml8726_sdxc_softc *sc, int mmc_error)
{
	int mmc_stop_error;
	struct mmc_command *cmd;
	struct mmc_command *stop_cmd;
	struct mmc_data *data;

	AML_SDXC_LOCK_ASSERT(sc);

	/* Clear all interrupts since the request is no longer in flight. */
	CSR_WRITE_4(sc, AML_SDXC_IRQ_STATUS_REG, AML_SDXC_IRQ_STATUS_CLEAR);
	CSR_BARRIER(sc, AML_SDXC_IRQ_STATUS_REG);

	/* In some cases (e.g. finish called via timeout) this is a NOP. */
	callout_stop(&sc->ch);

	cmd = sc->cmd;
	sc->cmd = NULL;

	cmd->error = mmc_error;

	data = cmd->data;

	if (data && data->len
	    && (data->flags & (MMC_DATA_READ | MMC_DATA_WRITE)) != 0) {
		if ((data->flags & MMC_DATA_READ) != 0)
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_POSTREAD);
		else
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->dmatag, sc->dmamap);
	}

	/*
	 * If there's a linked stop command, then start the stop command.
	 * In order to establish a known state attempt the stop command
	 * even if the original request encountered an error.
	 */

	stop_cmd = (cmd->mrq->stop != cmd) ? cmd->mrq->stop : NULL;

	if (stop_cmd != NULL) {

		/*
		 * If the original command executed successfully, then
		 * the hardware will also have automatically executed
		 * a stop command so don't bother with the one supplied
		 * with the original request.
		 */

		if (mmc_error == MMC_ERR_NONE) {
			stop_cmd->error = MMC_ERR_NONE;
			stop_cmd->resp[0] = cmd->resp[0];
			stop_cmd->resp[1] = cmd->resp[1];
			stop_cmd->resp[2] = cmd->resp[2];
			stop_cmd->resp[3] = cmd->resp[3];
		} else {
			mmc_stop_error = aml8726_sdxc_start_command(sc,
			    stop_cmd);
			if (mmc_stop_error == MMC_ERR_NONE) {
				AML_SDXC_UNLOCK(sc);
				return;
			}
			stop_cmd->error = mmc_stop_error;
		}
	}

	AML_SDXC_UNLOCK(sc);

	/* Execute the callback after dropping the lock. */
	if (cmd->mrq != NULL)
		cmd->mrq->done(cmd->mrq);
}

static void
aml8726_sdxc_timeout(void *arg)
{
	struct aml8726_sdxc_softc *sc = (struct aml8726_sdxc_softc *)arg;

	/*
	 * The command failed to complete in time so forcefully
	 * terminate it.
	 */
	aml8726_sdxc_soft_reset(sc);

	/*
	 * Ensure the command has terminated before continuing on
	 * to things such as bus_dmamap_sync / bus_dmamap_unload.
	 */
	while ((CSR_READ_4(sc, AML_SDXC_STATUS_REG) &
	    AML_SDXC_STATUS_BUSY) != 0)
		cpu_spinwait();

	aml8726_sdxc_finish_command(sc, MMC_ERR_TIMEOUT);
}

static void
aml8726_sdxc_busy_check(void *arg)
{
	struct aml8726_sdxc_softc *sc = (struct aml8726_sdxc_softc *)arg;
	uint32_t sr;

	sc->busy.time += AML_SDXC_BUSY_POLL_INTVL;

	sr = CSR_READ_4(sc, AML_SDXC_STATUS_REG);

	if ((sr & AML_SDXC_STATUS_DAT0) == 0) {
		if (sc->busy.time < AML_SDXC_BUSY_TIMEOUT) {
			callout_reset(&sc->ch,
			    msecs_to_ticks(AML_SDXC_BUSY_POLL_INTVL),
			    aml8726_sdxc_busy_check, sc);
			AML_SDXC_UNLOCK(sc);
			return;
		}
		if (sc->busy.error == MMC_ERR_NONE)
			sc->busy.error = MMC_ERR_TIMEOUT;
	}

	aml8726_sdxc_finish_command(sc, sc->busy.error);
}

static void
aml8726_sdxc_intr(void *arg)
{
	struct aml8726_sdxc_softc *sc = (struct aml8726_sdxc_softc *)arg;
	uint32_t isr;
	uint32_t pdmar;
	uint32_t sndr;
	uint32_t sr;
	int i;
	int mmc_error;
	int start;
	int stop;

	AML_SDXC_LOCK(sc);

	isr = CSR_READ_4(sc, AML_SDXC_IRQ_STATUS_REG);
	sndr = CSR_READ_4(sc, AML_SDXC_SEND_REG);
	sr = CSR_READ_4(sc, AML_SDXC_STATUS_REG);

	if (sc->cmd == NULL)
		goto spurious;

	mmc_error = MMC_ERR_NONE;

	if ((isr & (AML_SDXC_IRQ_STATUS_TX_FIFO_EMPTY |
	    AML_SDXC_IRQ_STATUS_RX_FIFO_FULL)) != 0)
		mmc_error = MMC_ERR_FIFO;
	else if ((isr & (AML_SDXC_IRQ_ENABLE_A_PKG_CRC_ERR |
	    AML_SDXC_IRQ_ENABLE_RESP_CRC_ERR)) != 0)
		mmc_error = MMC_ERR_BADCRC;
	else if ((isr & (AML_SDXC_IRQ_ENABLE_A_PKG_TIMEOUT_ERR |
	    AML_SDXC_IRQ_ENABLE_RESP_TIMEOUT_ERR)) != 0)
		mmc_error = MMC_ERR_TIMEOUT;
	else if ((isr & (AML_SDXC_IRQ_STATUS_RESP_OK |
	    AML_SDXC_IRQ_STATUS_DMA_DONE |
	    AML_SDXC_IRQ_STATUS_TRANSFER_DONE_OK)) != 0) {
		;
	}
	else {
spurious:
		/*
		 * Clear spurious interrupts while leaving intacted any
		 * interrupts that may have occurred after we read the
		 * interrupt status register.
		 */

		CSR_WRITE_4(sc, AML_SDXC_IRQ_STATUS_REG,
		    (AML_SDXC_IRQ_STATUS_CLEAR & isr));
		CSR_BARRIER(sc, AML_SDXC_IRQ_STATUS_REG);
		AML_SDXC_UNLOCK(sc);
		return;
	}

	aml8726_sdxc_disengage_dma(sc);

	if ((sndr & AML_SDXC_SEND_CMD_HAS_RESP) != 0) {
		start = 0;
		stop = 1;
		if ((sndr & AML_SDXC_SEND_RESP_136) != 0) {
			start = 1;
			stop = start + 4;
		}
		for (i = start; i < stop; i++) {
			pdmar = CSR_READ_4(sc, AML_SDXC_PDMA_REG);
			pdmar &= ~(AML_SDXC_PDMA_DMA_EN |
			    AML_SDXC_PDMA_RESP_INDEX_MASK);
			pdmar |= i << AML_SDXC_PDMA_RESP_INDEX_SHIFT;
			CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);
			sc->cmd->resp[(stop - 1) - i] = CSR_READ_4(sc,
			    AML_SDXC_CMD_ARGUMENT_REG);
		}
	}

	if ((sr & AML_SDXC_STATUS_BUSY) != 0 &&
	    /*
	     * A multiblock operation may keep the hardware
	     * busy until stop transmission is executed.
	     */
	    (isr & (AML_SDXC_IRQ_STATUS_DMA_DONE |
	    AML_SDXC_IRQ_STATUS_TRANSFER_DONE_OK)) == 0) {
		if (mmc_error == MMC_ERR_NONE)
			mmc_error = MMC_ERR_FAILED;

		/*
		 * Issue a soft reset to terminate the command.
		 *
		 * Ensure the command has terminated before continuing on
		 * to things such as bus_dmamap_sync / bus_dmamap_unload.
		 */

		aml8726_sdxc_soft_reset(sc);

		while ((CSR_READ_4(sc, AML_SDXC_STATUS_REG) &
		    AML_SDXC_STATUS_BUSY) != 0)
			cpu_spinwait();
	}

	/*
	 * The stop command can be generated either manually or
	 * automatically by the hardware if MISC_MANUAL_STOP_MODE
	 * has not been set.  In either case check for busy.
	 */

	if (((sc->cmd->flags & MMC_RSP_BUSY) != 0 ||
	    (sndr & AML_SDXC_SEND_INDEX_MASK) == MMC_STOP_TRANSMISSION) &&
	    (sr & AML_SDXC_STATUS_DAT0) == 0) {
		sc->busy.error = mmc_error;
		callout_reset(&sc->ch,
		    msecs_to_ticks(AML_SDXC_BUSY_POLL_INTVL),
		    aml8726_sdxc_busy_check, sc);
		CSR_WRITE_4(sc, AML_SDXC_IRQ_STATUS_REG,
		    (AML_SDXC_IRQ_STATUS_CLEAR & isr));
		CSR_BARRIER(sc, AML_SDXC_IRQ_STATUS_REG);
		AML_SDXC_UNLOCK(sc);
		return;
	}

	aml8726_sdxc_finish_command(sc, mmc_error);
}

static int
aml8726_sdxc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-sdxc-m8"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726-m8 SDXC");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_sdxc_attach(device_t dev)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(dev);
	char *voltages;
	char *voltage;
	int error;
	int nvoltages;
	pcell_t prop[3];
	phandle_t node;
	ssize_t len;
	device_t child;
	uint32_t ectlr;
	uint32_t miscr;
	uint32_t pdmar;

	sc->dev = dev;

	sc->auto_fill_flush = false;

	pdmar = AML_SDXC_PDMA_DMA_URGENT |
	    (49 << AML_SDXC_PDMA_TX_THOLD_SHIFT) |
	    (7 << AML_SDXC_PDMA_RX_THOLD_SHIFT) |
	    (15 << AML_SDXC_PDMA_RD_BURST_SHIFT) |
	    (7 << AML_SDXC_PDMA_WR_BURST_SHIFT);

	miscr = (2 << AML_SDXC_MISC_WCRC_OK_PAT_SHIFT) |
	    (5 << AML_SDXC_MISC_WCRC_ERR_PAT_SHIFT);

	ectlr = (12 << AML_SDXC_ENH_CNTRL_SDIO_IRQ_PERIOD_SHIFT);

	/*
	 * Certain bitfields are dependent on the hardware revision.
	 */
	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
		switch (aml8726_soc_metal_rev) {
		case AML_SOC_M8_METAL_REV_M2_A:
			sc->auto_fill_flush = true;
			miscr |= (6 << AML_SDXC_MISC_TXSTART_THOLD_SHIFT);
			ectlr |= (64 << AML_SDXC_ENH_CNTRL_RX_FULL_THOLD_SHIFT) |
			    AML_SDXC_ENH_CNTRL_WR_RESP_MODE_SKIP_M8M2;
			break;
		default:
			miscr |= (7 << AML_SDXC_MISC_TXSTART_THOLD_SHIFT);
			ectlr |= (63 << AML_SDXC_ENH_CNTRL_RX_FULL_THOLD_SHIFT) |
			    AML_SDXC_ENH_CNTRL_DMA_NO_WR_RESP_CHECK_M8 |
			    (255 << AML_SDXC_ENH_CNTRL_RX_TIMEOUT_SHIFT_M8);

			break;
		}
		break;
	case AML_SOC_HW_REV_M8B:
		miscr |= (7 << AML_SDXC_MISC_TXSTART_THOLD_SHIFT);
		ectlr |= (63 << AML_SDXC_ENH_CNTRL_RX_FULL_THOLD_SHIFT) |
		    AML_SDXC_ENH_CNTRL_DMA_NO_WR_RESP_CHECK_M8 |
		    (255 << AML_SDXC_ENH_CNTRL_RX_TIMEOUT_SHIFT_M8);
		break;
	default:
		device_printf(dev, "unsupported SoC\n");
		return (ENXIO);
		/* NOTREACHED */
	}

	node = ofw_bus_get_node(dev);

	len = OF_getencprop(node, "clock-frequency", prop, sizeof(prop));
	if ((len / sizeof(prop[0])) != 1 || prop[0] == 0) {
		device_printf(dev,
		    "missing clock-frequency attribute in FDT\n");
		return (ENXIO);
	}

	sc->ref_freq = prop[0];

	sc->pwr_en.dev = NULL;

	len = OF_getencprop(node, "mmc-pwr-en", prop, sizeof(prop));
	if (len > 0) {
		if ((len / sizeof(prop[0])) == 3) {
			sc->pwr_en.dev = OF_device_from_xref(prop[0]);
			sc->pwr_en.pin = prop[1];
			sc->pwr_en.pol = prop[2];
		}

		if (sc->pwr_en.dev == NULL) {
			device_printf(dev,
			    "unable to process mmc-pwr-en attribute in FDT\n");
			return (ENXIO);
		}

		/* Turn off power and then configure the output driver. */
		if (aml8726_sdxc_power_off(sc) != 0 ||
		    GPIO_PIN_SETFLAGS(sc->pwr_en.dev, sc->pwr_en.pin,
		    GPIO_PIN_OUTPUT) != 0) {
			device_printf(dev,
			    "could not use gpio to control power\n");
			return (ENXIO);
		}
	}

	len = OF_getprop_alloc(node, "mmc-voltages",
	    (void **)&voltages);

	if (len < 0) {
		device_printf(dev, "missing mmc-voltages attribute in FDT\n");
		return (ENXIO);
	}

	sc->voltages[0] = 0;
	sc->voltages[1] = 0;

	voltage = voltages;
	nvoltages = 0;

	while (len && nvoltages < 2) {
		if (strncmp("1.8", voltage, len) == 0)
			sc->voltages[nvoltages] = MMC_OCR_LOW_VOLTAGE;
		else if (strncmp("3.3", voltage, len) == 0)
			sc->voltages[nvoltages] = MMC_OCR_320_330 |
			    MMC_OCR_330_340;
		else {
			device_printf(dev,
			    "unknown voltage attribute %.*s in FDT\n",
			    len, voltage);
			OF_prop_free(voltages);
			return (ENXIO);
		}

		nvoltages++;

		/* queue up next string */
		while (*voltage && len) {
			voltage++;
			len--;
		}
		if (len) {
			voltage++;
			len--;
		}
	}

	OF_prop_free(voltages);

	sc->vselect.dev = NULL;

	len = OF_getencprop(node, "mmc-vselect", prop, sizeof(prop));
	if (len > 0) {
		if ((len / sizeof(prop[0])) == 2) {
			sc->vselect.dev = OF_device_from_xref(prop[0]);
			sc->vselect.pin = prop[1];
			sc->vselect.pol = 1;
		}

		if (sc->vselect.dev == NULL) {
			device_printf(dev,
			    "unable to process mmc-vselect attribute in FDT\n");
			return (ENXIO);
		}

		/*
		 * With the power off select voltage 0 and then
		 * configure the output driver.
		 */
		if (GPIO_PIN_SET(sc->vselect.dev, sc->vselect.pin, 0) != 0 ||
		    GPIO_PIN_SETFLAGS(sc->vselect.dev, sc->vselect.pin,
		    GPIO_PIN_OUTPUT) != 0) {
			device_printf(dev,
			    "could not use gpio to set voltage\n");
			return (ENXIO);
		}
	}

	if (nvoltages == 0) {
		device_printf(dev, "no voltages in FDT\n");
		return (ENXIO);
	} else if (nvoltages == 1 && sc->vselect.dev != NULL) {
		device_printf(dev, "only one voltage in FDT\n");
		return (ENXIO);
	} else if (nvoltages == 2 && sc->vselect.dev == NULL) {
		device_printf(dev, "too many voltages in FDT\n");
		return (ENXIO);
	}

	sc->card_rst.dev = NULL;

	len = OF_getencprop(node, "mmc-rst", prop, sizeof(prop));
	if (len > 0) {
		if ((len / sizeof(prop[0])) == 3) {
			sc->card_rst.dev = OF_device_from_xref(prop[0]);
			sc->card_rst.pin = prop[1];
			sc->card_rst.pol = prop[2];
		}

		if (sc->card_rst.dev == NULL) {
			device_printf(dev,
			    "unable to process mmc-rst attribute in FDT\n");
			return (ENXIO);
		}
	}

	if (bus_alloc_resources(dev, aml8726_sdxc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	AML_SDXC_LOCK_INIT(sc);

	error = bus_dma_tag_create(bus_get_dma_tag(dev), AML_SDXC_ALIGN_DMA, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    AML_SDXC_MAX_DMA, 1, AML_SDXC_MAX_DMA, 0, NULL, NULL, &sc->dmatag);
	if (error)
		goto fail;

	error = bus_dmamap_create(sc->dmatag, 0, &sc->dmamap);

	if (error)
		goto fail;

	error = bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, aml8726_sdxc_intr, sc, &sc->ih_cookie);
	if (error) {
		device_printf(dev, "could not setup interrupt handler\n");
		goto fail;
	}

	callout_init_mtx(&sc->ch, &sc->mtx, CALLOUT_RETURNUNLOCKED);

	sc->host.f_min = 200000;
	sc->host.f_max = 100000000;
	sc->host.host_ocr = sc->voltages[0] | sc->voltages[1];
	sc->host.caps = MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA |
	    MMC_CAP_HSPEED;

	aml8726_sdxc_soft_reset(sc);

	CSR_WRITE_4(sc, AML_SDXC_PDMA_REG, pdmar);

	CSR_WRITE_4(sc, AML_SDXC_MISC_REG, miscr);

	CSR_WRITE_4(sc, AML_SDXC_ENH_CNTRL_REG, ectlr);

	child = device_add_child(dev, "mmc", -1);

	if (!child) {
		device_printf(dev, "could not add mmc\n");
		error = ENXIO;
		goto fail;
	}

	error = device_probe_and_attach(child);

	if (error) {
		device_printf(dev, "could not attach mmc\n");
		goto fail;
	}

	return (0);

fail:
	if (sc->ih_cookie)
		bus_teardown_intr(dev, sc->res[1], sc->ih_cookie);

	if (sc->dmamap)
		bus_dmamap_destroy(sc->dmatag, sc->dmamap);

	if (sc->dmatag)
		bus_dma_tag_destroy(sc->dmatag);

	AML_SDXC_LOCK_DESTROY(sc);

	(void)aml8726_sdxc_power_off(sc);

	bus_release_resources(dev, aml8726_sdxc_spec, sc->res);

	return (error);
}

static int
aml8726_sdxc_detach(device_t dev)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(dev);

	AML_SDXC_LOCK(sc);

	if (sc->cmd != NULL) {
		AML_SDXC_UNLOCK(sc);
		return (EBUSY);
	}

	/*
	 * Turn off the power, reset the hardware state machine,
	 * and disable the interrupts.
	 */
	aml8726_sdxc_power_off(sc);
	aml8726_sdxc_soft_reset(sc);
	CSR_WRITE_4(sc, AML_SDXC_IRQ_ENABLE_REG, 0);

	AML_SDXC_UNLOCK(sc);

	bus_generic_detach(dev);

	bus_teardown_intr(dev, sc->res[1], sc->ih_cookie);

	bus_dmamap_destroy(sc->dmatag, sc->dmamap);

	bus_dma_tag_destroy(sc->dmatag);

	AML_SDXC_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_sdxc_spec, sc->res);

	return (0);
}

static int
aml8726_sdxc_shutdown(device_t dev)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(dev);

	/*
	 * Turn off the power, reset the hardware state machine,
	 * and disable the interrupts.
	 */
	aml8726_sdxc_power_off(sc);
	aml8726_sdxc_soft_reset(sc);
	CSR_WRITE_4(sc, AML_SDXC_IRQ_ENABLE_REG, 0);

	return (0);
}

static int
aml8726_sdxc_update_ios(device_t bus, device_t child)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);
	struct mmc_ios *ios = &sc->host.ios;
	unsigned int divisor;
	int error;
	int i;
	uint32_t cctlr;
	uint32_t clk2r;
	uint32_t ctlr;
	uint32_t freq;

	ctlr = (7 << AML_SDXC_CNTRL_TX_ENDIAN_SHIFT) |
	    (7 << AML_SDXC_CNTRL_RX_ENDIAN_SHIFT) |
	    (0xf << AML_SDXC_CNTRL_RX_PERIOD_SHIFT) |
	    (0x7f << AML_SDXC_CNTRL_RX_TIMEOUT_SHIFT);

	switch (ios->bus_width) {
	case bus_width_8:
		ctlr |= AML_SDXC_CNTRL_BUS_WIDTH_8;
		break;
	case bus_width_4:
		ctlr |= AML_SDXC_CNTRL_BUS_WIDTH_4;
		break;
	case bus_width_1:
		ctlr |= AML_SDXC_CNTRL_BUS_WIDTH_1;
		break;
	default:
		return (EINVAL);
	}

	CSR_WRITE_4(sc, AML_SDXC_CNTRL_REG, ctlr);

	/*
	 * Disable clocks and then clock module prior to setting desired values.
	 */
	cctlr = CSR_READ_4(sc, AML_SDXC_CLK_CNTRL_REG);
	cctlr &= ~(AML_SDXC_CLK_CNTRL_TX_CLK_EN | AML_SDXC_CLK_CNTRL_RX_CLK_EN |
	    AML_SDXC_CLK_CNTRL_SD_CLK_EN);
	CSR_WRITE_4(sc, AML_SDXC_CLK_CNTRL_REG, cctlr);
	CSR_BARRIER(sc, AML_SDXC_CLK_CNTRL_REG);
	cctlr &= ~AML_SDXC_CLK_CNTRL_CLK_MODULE_EN;
	CSR_WRITE_4(sc, AML_SDXC_CLK_CNTRL_REG, cctlr);
	CSR_BARRIER(sc, AML_SDXC_CLK_CNTRL_REG);

	/*
	 *                  aml8726-m8
	 *
	 * Clock select 1   fclk_div2 (1.275 GHz)
	 */
	cctlr &= ~AML_SDXC_CLK_CNTRL_CLK_SEL_MASK;
	cctlr |= (1 << AML_SDXC_CLK_CNTRL_CLK_SEL_SHIFT);

	divisor = sc->ref_freq / ios->clock - 1;
	if (divisor == 0 || divisor == -1)
		divisor = 1;
	if ((sc->ref_freq / (divisor + 1)) > ios->clock)
		divisor += 1;
	if (divisor > (AML_SDXC_CLK_CNTRL_CLK_DIV_MASK >>
	    AML_SDXC_CLK_CNTRL_CLK_DIV_SHIFT))
		divisor = AML_SDXC_CLK_CNTRL_CLK_DIV_MASK >>
		    AML_SDXC_CLK_CNTRL_CLK_DIV_SHIFT;

	cctlr &= ~AML_SDXC_CLK_CNTRL_CLK_DIV_MASK;
	cctlr |= divisor << AML_SDXC_CLK_CNTRL_CLK_DIV_SHIFT;

	cctlr &= ~AML_SDXC_CLK_CNTRL_MEM_PWR_MASK;
	cctlr |= AML_SDXC_CLK_CNTRL_MEM_PWR_ON;

	CSR_WRITE_4(sc, AML_SDXC_CLK_CNTRL_REG, cctlr);
	CSR_BARRIER(sc, AML_SDXC_CLK_CNTRL_REG);

	/*
	 * Enable clock module and then clocks after setting desired values.
	 */
	cctlr |= AML_SDXC_CLK_CNTRL_CLK_MODULE_EN;
	CSR_WRITE_4(sc, AML_SDXC_CLK_CNTRL_REG, cctlr);
	CSR_BARRIER(sc, AML_SDXC_CLK_CNTRL_REG);
	cctlr |= AML_SDXC_CLK_CNTRL_TX_CLK_EN | AML_SDXC_CLK_CNTRL_RX_CLK_EN |
	    AML_SDXC_CLK_CNTRL_SD_CLK_EN;
	CSR_WRITE_4(sc, AML_SDXC_CLK_CNTRL_REG, cctlr);
	CSR_BARRIER(sc, AML_SDXC_CLK_CNTRL_REG);

	freq = sc->ref_freq / divisor;

	for (i = 0; aml8726_sdxc_clk_phases[i].voltage; i++) {
		if ((aml8726_sdxc_clk_phases[i].voltage &
		    (1 << ios->vdd)) != 0 &&
		    freq > aml8726_sdxc_clk_phases[i].freq)
			break;
		if (aml8726_sdxc_clk_phases[i].freq == 0)
			break;
	}

	clk2r = (1 << AML_SDXC_CLK2_SD_PHASE_SHIFT) |
	    (aml8726_sdxc_clk_phases[i].rx_phase <<
	    AML_SDXC_CLK2_RX_PHASE_SHIFT);
	CSR_WRITE_4(sc, AML_SDXC_CLK2_REG, clk2r);
	CSR_BARRIER(sc, AML_SDXC_CLK2_REG);

	error = 0;

	switch (ios->power_mode) {
	case power_up:
		/*
		 * Configure and power on the regulator so that the
		 * voltage stabilizes prior to powering on the card.
		 */
		if (sc->vselect.dev != NULL) {
			for (i = 0; i < 2; i++)
				if ((sc->voltages[i] & (1 << ios->vdd)) != 0)
					break;
			if (i >= 2)
				return (EINVAL);
			error = GPIO_PIN_SET(sc->vselect.dev,
			    sc->vselect.pin, i);
		}
		break;
	case power_on:
		error = aml8726_sdxc_power_on(sc);
		if (error)
			break;

		if (sc->card_rst.dev != NULL) {
			if (GPIO_PIN_SET(sc->card_rst.dev, sc->card_rst.pin,
			    PIN_ON_FLAG(sc->card_rst.pol)) != 0 ||
			    GPIO_PIN_SETFLAGS(sc->card_rst.dev,
			    sc->card_rst.pin,
			    GPIO_PIN_OUTPUT) != 0)
				error = ENXIO;

			DELAY(5);

			if (GPIO_PIN_SET(sc->card_rst.dev, sc->card_rst.pin,
			    PIN_OFF_FLAG(sc->card_rst.pol)) != 0)
				error = ENXIO;

			DELAY(5);

			if (error) {
				device_printf(sc->dev,
				    "could not use gpio to reset card\n");
				break;
			}
		}
		break;
	case power_off:
		error = aml8726_sdxc_power_off(sc);
		break;
	default:
		return (EINVAL);
	}

	return (error);
}

static int
aml8726_sdxc_request(device_t bus, device_t child, struct mmc_request *req)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);
	int mmc_error;

	AML_SDXC_LOCK(sc);

	if (sc->cmd != NULL) {
		AML_SDXC_UNLOCK(sc);
		return (EBUSY);
	}

	mmc_error = aml8726_sdxc_start_command(sc, req->cmd);

	AML_SDXC_UNLOCK(sc);

	/* Execute the callback after dropping the lock. */
	if (mmc_error != MMC_ERR_NONE) {
		req->cmd->error = mmc_error;
		req->done(req);
	}

	return (0);
}

static int
aml8726_sdxc_read_ivar(device_t bus, device_t child,
    int which, uintptr_t *result)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);

	switch (which) {
	case MMCBR_IVAR_BUS_MODE:
		*(int *)result = sc->host.ios.bus_mode;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		*(int *)result = sc->host.ios.bus_width;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		*(int *)result = sc->host.ios.chip_select;
		break;
	case MMCBR_IVAR_CLOCK:
		*(int *)result = sc->host.ios.clock;
		break;
	case MMCBR_IVAR_F_MIN:
		*(int *)result = sc->host.f_min;
		break;
	case MMCBR_IVAR_F_MAX:
		*(int *)result = sc->host.f_max;
		break;
	case MMCBR_IVAR_HOST_OCR:
		*(int *)result = sc->host.host_ocr;
		break;
	case MMCBR_IVAR_MODE:
		*(int *)result = sc->host.mode;
		break;
	case MMCBR_IVAR_OCR:
		*(int *)result = sc->host.ocr;
		break;
	case MMCBR_IVAR_POWER_MODE:
		*(int *)result = sc->host.ios.power_mode;
		break;
	case MMCBR_IVAR_VDD:
		*(int *)result = sc->host.ios.vdd;
		break;
	case MMCBR_IVAR_CAPS:
		*(int *)result = sc->host.caps;
		break;
	case MMCBR_IVAR_MAX_DATA:
		*(int *)result = AML_SDXC_MAX_DMA / MMC_SECTOR_SIZE;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
aml8726_sdxc_write_ivar(device_t bus, device_t child,
    int which, uintptr_t value)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);

	switch (which) {
	case MMCBR_IVAR_BUS_MODE:
		sc->host.ios.bus_mode = value;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		sc->host.ios.bus_width = value;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		sc->host.ios.chip_select = value;
		break;
	case MMCBR_IVAR_CLOCK:
		sc->host.ios.clock = value;
		break;
	case MMCBR_IVAR_MODE:
		sc->host.mode = value;
		break;
	case MMCBR_IVAR_OCR:
		sc->host.ocr = value;
		break;
	case MMCBR_IVAR_POWER_MODE:
		sc->host.ios.power_mode = value;
		break;
	case MMCBR_IVAR_VDD:
		sc->host.ios.vdd = value;
		break;
	/* These are read-only */
	case MMCBR_IVAR_CAPS:
	case MMCBR_IVAR_HOST_OCR:
	case MMCBR_IVAR_F_MIN:
	case MMCBR_IVAR_F_MAX:
	case MMCBR_IVAR_MAX_DATA:
	default:
		return (EINVAL);
	}

	return (0);
}

static int
aml8726_sdxc_get_ro(device_t bus, device_t child)
{

	return (0);
}

static int
aml8726_sdxc_acquire_host(device_t bus, device_t child)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);

	AML_SDXC_LOCK(sc);

	while (sc->bus_busy)
		mtx_sleep(sc, &sc->mtx, PZERO, "sdxc", hz / 5);
	sc->bus_busy++;

	AML_SDXC_UNLOCK(sc);

	return (0);
}

static int
aml8726_sdxc_release_host(device_t bus, device_t child)
{
	struct aml8726_sdxc_softc *sc = device_get_softc(bus);

	AML_SDXC_LOCK(sc);

	sc->bus_busy--;
	wakeup(sc);

	AML_SDXC_UNLOCK(sc);

	return (0);
}

static device_method_t aml8726_sdxc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_sdxc_probe),
	DEVMETHOD(device_attach,	aml8726_sdxc_attach),
	DEVMETHOD(device_detach,	aml8726_sdxc_detach),
	DEVMETHOD(device_shutdown,	aml8726_sdxc_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	aml8726_sdxc_read_ivar),
	DEVMETHOD(bus_write_ivar,	aml8726_sdxc_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	aml8726_sdxc_update_ios),
	DEVMETHOD(mmcbr_request,	aml8726_sdxc_request),
	DEVMETHOD(mmcbr_get_ro,		aml8726_sdxc_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	aml8726_sdxc_acquire_host),
	DEVMETHOD(mmcbr_release_host,	aml8726_sdxc_release_host),

	DEVMETHOD_END
};

static driver_t aml8726_sdxc_driver = {
	"aml8726_sdxc",
	aml8726_sdxc_methods,
	sizeof(struct aml8726_sdxc_softc),
};

static devclass_t aml8726_sdxc_devclass;

DRIVER_MODULE(aml8726_sdxc, simplebus, aml8726_sdxc_driver,
    aml8726_sdxc_devclass, NULL, NULL);
MODULE_DEPEND(aml8726_sdxc, aml8726_gpio, 1, 1, 1);
MMC_DECLARE_BRIDGE(aml8726_sdxc);
