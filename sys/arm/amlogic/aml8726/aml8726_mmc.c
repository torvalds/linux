/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726 MMC host controller driver.
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
#include <dev/mmc/mmcbrvar.h>

#include <arm/amlogic/aml8726/aml8726_mmc.h>

#include "gpio_if.h"
#include "mmcbr_if.h"

struct aml8726_mmc_gpio {
	device_t	dev;
	uint32_t	pin;
	uint32_t	pol;
};

struct aml8726_mmc_softc {
	device_t		dev;
	struct resource		*res[2];
	struct mtx		mtx;
	struct callout		ch;
	uint32_t		port;
	unsigned int		ref_freq;
	struct aml8726_mmc_gpio pwr_en;
	int			voltages[2];
	struct aml8726_mmc_gpio vselect;
	bus_dma_tag_t		dmatag;
	bus_dmamap_t		dmamap;
	void			*ih_cookie;
	struct mmc_host		host;
	int			bus_busy;
	struct mmc_command 	*cmd;
	uint32_t		stop_timeout;
};

static struct resource_spec aml8726_mmc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	AML_MMC_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_MMC_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_MMC_LOCK_ASSERT(sc)		mtx_assert(&(sc)->mtx, MA_OWNED)
#define	AML_MMC_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "mmc", MTX_DEF)
#define	AML_MMC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	PWR_ON_FLAG(pol)		((pol) == 0 ? GPIO_PIN_LOW :	\
    GPIO_PIN_HIGH)
#define	PWR_OFF_FLAG(pol)		((pol) == 0 ? GPIO_PIN_HIGH :	\
    GPIO_PIN_LOW)

#define	MSECS_TO_TICKS(ms)		(((ms)*hz)/1000 + 1)

static void aml8726_mmc_timeout(void *arg);

static unsigned int
aml8726_mmc_clk(phandle_t node)
{
	pcell_t prop;
	ssize_t len;
	phandle_t clk_node;

	len = OF_getencprop(node, "clocks", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop == 0 ||
	    (clk_node = OF_node_from_xref(prop)) == 0)
		return (0);

	len = OF_getencprop(clk_node, "clock-frequency", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop == 0)
		return (0);

	return ((unsigned int)prop);
}

static uint32_t
aml8726_mmc_freq(struct aml8726_mmc_softc *sc, uint32_t divisor)
{

	return (sc->ref_freq / ((divisor + 1) * 2));
}

static uint32_t
aml8726_mmc_div(struct aml8726_mmc_softc *sc, uint32_t desired_freq)
{
	uint32_t divisor;

	divisor = sc->ref_freq / (desired_freq * 2);

	if (divisor == 0)
		divisor = 1;

	divisor -= 1;

	if (aml8726_mmc_freq(sc, divisor) > desired_freq)
		divisor += 1;

	if (divisor > (AML_MMC_CONFIG_CMD_CLK_DIV_MASK >>
	    AML_MMC_CONFIG_CMD_CLK_DIV_SHIFT)) {
		divisor = AML_MMC_CONFIG_CMD_CLK_DIV_MASK >>
		    AML_MMC_CONFIG_CMD_CLK_DIV_SHIFT;
	}

	return (divisor);
}

static void
aml8726_mmc_mapmem(void *arg, bus_dma_segment_t *segs, int nseg, int error)
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
aml8726_mmc_power_off(struct aml8726_mmc_softc *sc)
{

	if (sc->pwr_en.dev == NULL)
		return (0);

	return (GPIO_PIN_SET(sc->pwr_en.dev, sc->pwr_en.pin,
	    PWR_OFF_FLAG(sc->pwr_en.pol)));
}

static int
aml8726_mmc_power_on(struct aml8726_mmc_softc *sc)
{

	if (sc->pwr_en.dev == NULL)
		return (0);

	return (GPIO_PIN_SET(sc->pwr_en.dev, sc->pwr_en.pin,
	    PWR_ON_FLAG(sc->pwr_en.pol)));
}

static void
aml8726_mmc_soft_reset(struct aml8726_mmc_softc *sc, boolean_t enable_irq)
{
	uint32_t icr;

	icr = AML_MMC_IRQ_CONFIG_SOFT_RESET;

	if (enable_irq == true)
		icr |= AML_MMC_IRQ_CONFIG_CMD_DONE_EN;

	CSR_WRITE_4(sc, AML_MMC_IRQ_CONFIG_REG, icr);
	CSR_BARRIER(sc, AML_MMC_IRQ_CONFIG_REG);
}

static int
aml8726_mmc_start_command(struct aml8726_mmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_ios *ios = &sc->host.ios;
	bus_addr_t baddr;
	uint32_t block_size;
	uint32_t bus_width;
	uint32_t cmdr;
	uint32_t extr;
	uint32_t mcfgr;
	uint32_t nbits_per_pkg;
	uint32_t timeout;
	int error;
	struct mmc_data *data;

	if (cmd->opcode > 0x3f)
		return (MMC_ERR_INVALID);

	/*
	 * Ensure the hardware state machine is in a known state.
	 */
	aml8726_mmc_soft_reset(sc, true);

	/*
	 * Start and transmission bits are per section 4.7.2 of the:
	 *
	 *   SD Specifications Part 1
	 *   Physical Layer Simplified Specification
	 *   Version 4.10
	 */
	cmdr = AML_MMC_CMD_START_BIT | AML_MMC_CMD_TRANS_BIT_HOST | cmd->opcode;
	baddr = 0;
	extr = 0;
	mcfgr = sc->port;
	timeout = AML_MMC_CMD_TIMEOUT;

	/*
	 * If this is a linked command, then use the previous timeout.
	 */
	if (cmd == cmd->mrq->stop && sc->stop_timeout)
		timeout = sc->stop_timeout;
	sc->stop_timeout = 0;

	if ((cmd->flags & MMC_RSP_136) != 0) {
		cmdr |= AML_MMC_CMD_RESP_CRC7_FROM_8;
		cmdr |= (133 << AML_MMC_CMD_RESP_BITS_SHIFT);
	} else if ((cmd->flags & MMC_RSP_PRESENT) != 0)
		cmdr |= (45 << AML_MMC_CMD_RESP_BITS_SHIFT);

	if ((cmd->flags & MMC_RSP_CRC) == 0)
		cmdr |= AML_MMC_CMD_RESP_NO_CRC7;

	if ((cmd->flags & MMC_RSP_BUSY) != 0)
		cmdr |= AML_MMC_CMD_CHECK_DAT0_BUSY;

	data = cmd->data;

	if (data && data->len &&
	    (data->flags & (MMC_DATA_READ | MMC_DATA_WRITE)) != 0) {
		block_size = data->len;

		if ((data->flags & MMC_DATA_MULTI) != 0) {
			block_size = MMC_SECTOR_SIZE;
			if ((data->len % block_size) != 0)
				return (MMC_ERR_INVALID);
		}

		cmdr |= (((data->len / block_size) - 1) <<
		    AML_MMC_CMD_REP_PKG_CNT_SHIFT);

		mcfgr |= (data->flags & MMC_DATA_STREAM) ?
		    AML_MMC_MULT_CONFIG_STREAM_EN : 0;

		/*
		 * The number of bits per package equals the number
		 * of data bits + the number of CRC bits.  There are
		 * 16 bits of CRC calculate per bus line.
		 *
		 * A completed package appears to be detected by when
		 * a counter decremented by the width underflows, thus
		 * a value of zero always transfers 1 (or 4 bits depending
		 * on the mode) which is why bus_width is subtracted.
		 */
		bus_width = (ios->bus_width == bus_width_4) ? 4 : 1;
		nbits_per_pkg = block_size * 8 + 16 * bus_width - bus_width;
		if (nbits_per_pkg > 0x3fff)
			return (MMC_ERR_INVALID);

		extr |= (nbits_per_pkg << AML_MMC_EXTENSION_PKT_SIZE_SHIFT);

		error = bus_dmamap_load(sc->dmatag, sc->dmamap,
		    data->data, data->len, aml8726_mmc_mapmem, &baddr,
		    BUS_DMA_NOWAIT);
		if (error)
			return (MMC_ERR_NO_MEMORY);

		if ((data->flags & MMC_DATA_READ) != 0) {
			cmdr |= AML_MMC_CMD_RESP_HAS_DATA;
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_PREREAD);
			timeout = AML_MMC_READ_TIMEOUT *
			    (data->len / block_size);
		} else {
			cmdr |= AML_MMC_CMD_CMD_HAS_DATA;
			bus_dmamap_sync(sc->dmatag, sc->dmamap,
			    BUS_DMASYNC_PREWRITE);
			timeout = AML_MMC_WRITE_TIMEOUT *
			    (data->len / block_size);
		}

		/*
		 * Stop terminates a multiblock read / write and thus
		 * can take as long to execute as an actual read / write.
		 */
		if (cmd->mrq->stop != NULL)
			sc->stop_timeout = timeout;
	}

	sc->cmd = cmd;

	cmd->error = MMC_ERR_NONE;

	if (timeout > AML_MMC_MAX_TIMEOUT)
		timeout = AML_MMC_MAX_TIMEOUT;

	callout_reset(&sc->ch, MSECS_TO_TICKS(timeout),
	    aml8726_mmc_timeout, sc);

	CSR_WRITE_4(sc, AML_MMC_CMD_ARGUMENT_REG, cmd->arg);
	CSR_WRITE_4(sc, AML_MMC_MULT_CONFIG_REG, mcfgr);
	CSR_WRITE_4(sc, AML_MMC_EXTENSION_REG, extr);
	CSR_WRITE_4(sc, AML_MMC_DMA_ADDR_REG, (uint32_t)baddr);

	CSR_WRITE_4(sc, AML_MMC_CMD_SEND_REG, cmdr);
	CSR_BARRIER(sc, AML_MMC_CMD_SEND_REG);

	return (MMC_ERR_NONE);
}

static void
aml8726_mmc_finish_command(struct aml8726_mmc_softc *sc, int mmc_error)
{
	int mmc_stop_error;
	struct mmc_command *cmd;
	struct mmc_command *stop_cmd;
	struct mmc_data *data;

	AML_MMC_LOCK_ASSERT(sc);

	/* Clear all interrupts since the request is no longer in flight. */
	CSR_WRITE_4(sc, AML_MMC_IRQ_STATUS_REG, AML_MMC_IRQ_STATUS_CLEAR_IRQ);
	CSR_BARRIER(sc, AML_MMC_IRQ_STATUS_REG);

	/* In some cases (e.g. finish called via timeout) this is a NOP. */
	callout_stop(&sc->ch);

	cmd = sc->cmd;
	sc->cmd = NULL;

	cmd->error = mmc_error;

	data = cmd->data;

	if (data && data->len &&
	    (data->flags & (MMC_DATA_READ | MMC_DATA_WRITE)) != 0) {
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
		mmc_stop_error = aml8726_mmc_start_command(sc, stop_cmd);
		if (mmc_stop_error == MMC_ERR_NONE) {
			AML_MMC_UNLOCK(sc);
			return;
		}
		stop_cmd->error = mmc_stop_error;
	}

	AML_MMC_UNLOCK(sc);

	/* Execute the callback after dropping the lock. */
	if (cmd->mrq)
		cmd->mrq->done(cmd->mrq);
}

static void
aml8726_mmc_timeout(void *arg)
{
	struct aml8726_mmc_softc *sc = (struct aml8726_mmc_softc *)arg;

	/*
	 * The command failed to complete in time so forcefully
	 * terminate it.
	 */
	aml8726_mmc_soft_reset(sc, false);

	/*
	 * Ensure the command has terminated before continuing on
	 * to things such as bus_dmamap_sync / bus_dmamap_unload.
	 */
	while ((CSR_READ_4(sc, AML_MMC_IRQ_STATUS_REG) &
	    AML_MMC_IRQ_STATUS_CMD_BUSY) != 0)
		cpu_spinwait();

	aml8726_mmc_finish_command(sc, MMC_ERR_TIMEOUT);
}

static void
aml8726_mmc_intr(void *arg)
{
	struct aml8726_mmc_softc *sc = (struct aml8726_mmc_softc *)arg;
	uint32_t cmdr;
	uint32_t isr;
	uint32_t mcfgr;
	uint32_t previous_byte;
	uint32_t resp;
	int mmc_error;
	unsigned int i;

	AML_MMC_LOCK(sc);

	isr = CSR_READ_4(sc, AML_MMC_IRQ_STATUS_REG);
	cmdr = CSR_READ_4(sc, AML_MMC_CMD_SEND_REG);

	if (sc->cmd == NULL)
		goto spurious;

	mmc_error = MMC_ERR_NONE;

	if ((isr & AML_MMC_IRQ_STATUS_CMD_DONE_IRQ) != 0) {
		/* Check for CRC errors if the command has completed. */
		if ((cmdr & AML_MMC_CMD_RESP_NO_CRC7) == 0 &&
		    (isr & AML_MMC_IRQ_STATUS_RESP_CRC7_OK) == 0)
			mmc_error = MMC_ERR_BADCRC;
		if ((cmdr & AML_MMC_CMD_RESP_HAS_DATA) != 0 &&
		    (isr & AML_MMC_IRQ_STATUS_RD_CRC16_OK) == 0)
			mmc_error = MMC_ERR_BADCRC;
		if ((cmdr & AML_MMC_CMD_CMD_HAS_DATA) != 0 &&
		    (isr & AML_MMC_IRQ_STATUS_WR_CRC16_OK) == 0)
			mmc_error = MMC_ERR_BADCRC;
	} else {
spurious:

		/*
		 * Clear spurious interrupts while leaving intacted any
		 * interrupts that may have occurred after we read the
		 * interrupt status register.
		 */

		CSR_WRITE_4(sc, AML_MMC_IRQ_STATUS_REG,
		    (AML_MMC_IRQ_STATUS_CLEAR_IRQ & isr));
		CSR_BARRIER(sc, AML_MMC_IRQ_STATUS_REG);
		AML_MMC_UNLOCK(sc);
		return;
	}

	if ((cmdr & AML_MMC_CMD_RESP_BITS_MASK) != 0) {
		mcfgr = sc->port;
		mcfgr |= AML_MMC_MULT_CONFIG_RESP_READOUT_EN;
		CSR_WRITE_4(sc, AML_MMC_MULT_CONFIG_REG, mcfgr);

		if ((cmdr & AML_MMC_CMD_RESP_CRC7_FROM_8) != 0) {

			/*
			 * Controller supplies 135:8 instead of
			 * 127:0 so discard the leading 8 bits
			 * and provide a trailing 8 zero bits
			 * where the CRC belongs.
			 */

			previous_byte = 0;

			for (i = 0; i < 4; i++) {
				resp = CSR_READ_4(sc, AML_MMC_CMD_ARGUMENT_REG);
				sc->cmd->resp[3 - i] = (resp << 8) |
				    previous_byte;
				previous_byte = (resp >> 24) & 0xff;
			}
		} else
			sc->cmd->resp[0] = CSR_READ_4(sc,
			    AML_MMC_CMD_ARGUMENT_REG);
	}

	if ((isr & AML_MMC_IRQ_STATUS_CMD_BUSY) != 0 &&
	    /*
	     * A multiblock operation may keep the hardware
	     * busy until stop transmission is executed.
	     */
	    (isr & AML_MMC_IRQ_STATUS_CMD_DONE_IRQ) == 0) {
		if (mmc_error == MMC_ERR_NONE)
			mmc_error = MMC_ERR_FAILED;

		/*
		 * Issue a soft reset to terminate the command.
		 *
		 * Ensure the command has terminated before continuing on
		 * to things such as bus_dmamap_sync / bus_dmamap_unload.
		 */

		aml8726_mmc_soft_reset(sc, false);

		while ((CSR_READ_4(sc, AML_MMC_IRQ_STATUS_REG) &
		    AML_MMC_IRQ_STATUS_CMD_BUSY) != 0)
			cpu_spinwait();
	}

	aml8726_mmc_finish_command(sc, mmc_error);
}

static int
aml8726_mmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-mmc"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 MMC");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_mmc_attach(device_t dev)
{
	struct aml8726_mmc_softc *sc = device_get_softc(dev);
	char *function_name;
	char *voltages;
	char *voltage;
	int error;
	int nvoltages;
	pcell_t prop[3];
	phandle_t node;
	ssize_t len;
	device_t child;

	sc->dev = dev;

	node = ofw_bus_get_node(dev);

	sc->ref_freq = aml8726_mmc_clk(node);

	if (sc->ref_freq == 0) {
		device_printf(dev, "missing clocks attribute in FDT\n");
		return (ENXIO);
	}

	/*
	 * The pins must be specified as part of the device in order
	 * to know which port to used.
	 */

	len = OF_getencprop(node, "pinctrl-0", prop, sizeof(prop));

	if ((len / sizeof(prop[0])) != 1 || prop[0] == 0) {
		device_printf(dev, "missing pinctrl-0 attribute in FDT\n");
		return (ENXIO);
	}

	len = OF_getprop_alloc(OF_node_from_xref(prop[0]), "amlogic,function",
	    (void **)&function_name);

	if (len < 0) {
		device_printf(dev,
		    "missing amlogic,function attribute in FDT\n");
		return (ENXIO);
	}

	if (strncmp("sdio-a", function_name, len) == 0)
		sc->port = AML_MMC_MULT_CONFIG_PORT_A;
	else if (strncmp("sdio-b", function_name, len) == 0)
		sc->port = AML_MMC_MULT_CONFIG_PORT_B;
	else if (strncmp("sdio-c", function_name, len) == 0)
		sc->port = AML_MMC_MULT_CONFIG_PORT_C;
	else {
		device_printf(dev, "unknown function attribute %.*s in FDT\n",
		    len, function_name);
		OF_prop_free(function_name);
		return (ENXIO);
	}

	OF_prop_free(function_name);

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
		if (aml8726_mmc_power_off(sc) != 0 ||
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

	if (bus_alloc_resources(dev, aml8726_mmc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	AML_MMC_LOCK_INIT(sc);

	error = bus_dma_tag_create(bus_get_dma_tag(dev), AML_MMC_ALIGN_DMA, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    AML_MMC_MAX_DMA, 1, AML_MMC_MAX_DMA, 0, NULL, NULL, &sc->dmatag);
	if (error)
		goto fail;

	error = bus_dmamap_create(sc->dmatag, 0, &sc->dmamap);

	if (error)
		goto fail;

	error = bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, aml8726_mmc_intr, sc, &sc->ih_cookie);
	if (error) {
		device_printf(dev, "could not setup interrupt handler\n");
		goto fail;
	}

	callout_init_mtx(&sc->ch, &sc->mtx, CALLOUT_RETURNUNLOCKED);

	sc->host.f_min = aml8726_mmc_freq(sc, aml8726_mmc_div(sc, 200000));
	sc->host.f_max = aml8726_mmc_freq(sc, aml8726_mmc_div(sc, 50000000));
	sc->host.host_ocr = sc->voltages[0] | sc->voltages[1];
	sc->host.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_HSPEED;

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

	AML_MMC_LOCK_DESTROY(sc);

	aml8726_mmc_power_off(sc);

	bus_release_resources(dev, aml8726_mmc_spec, sc->res);

	return (error);
}

static int
aml8726_mmc_detach(device_t dev)
{
	struct aml8726_mmc_softc *sc = device_get_softc(dev);

	AML_MMC_LOCK(sc);

	if (sc->cmd != NULL) {
		AML_MMC_UNLOCK(sc);
		return (EBUSY);
	}

	/*
	 * Turn off the power, reset the hardware state machine,
	 * disable the interrupts, and clear the interrupts.
	 */
	(void)aml8726_mmc_power_off(sc);
	aml8726_mmc_soft_reset(sc, false);
	CSR_WRITE_4(sc, AML_MMC_IRQ_STATUS_REG, AML_MMC_IRQ_STATUS_CLEAR_IRQ);

	/* This should be a NOP since no command was in flight. */
	callout_stop(&sc->ch);

	AML_MMC_UNLOCK(sc);

	bus_generic_detach(dev);

	bus_teardown_intr(dev, sc->res[1], sc->ih_cookie);

	bus_dmamap_destroy(sc->dmatag, sc->dmamap);

	bus_dma_tag_destroy(sc->dmatag);

	AML_MMC_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_mmc_spec, sc->res);

	return (0);
}

static int
aml8726_mmc_shutdown(device_t dev)
{
	struct aml8726_mmc_softc *sc = device_get_softc(dev);

	/*
	 * Turn off the power, reset the hardware state machine,
	 * disable the interrupts, and clear the interrupts.
	 */
	(void)aml8726_mmc_power_off(sc);
	aml8726_mmc_soft_reset(sc, false);
	CSR_WRITE_4(sc, AML_MMC_IRQ_STATUS_REG, AML_MMC_IRQ_STATUS_CLEAR_IRQ);

	return (0);
}

static int
aml8726_mmc_update_ios(device_t bus, device_t child)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);
	struct mmc_ios *ios = &sc->host.ios;
	int error;
	int i;
	uint32_t cfgr;

	cfgr = (2 << AML_MMC_CONFIG_WR_CRC_STAT_SHIFT) |
	    (2 << AML_MMC_CONFIG_WR_DELAY_SHIFT) |
	    AML_MMC_CONFIG_DMA_ENDIAN_SBW |
	    (39 << AML_MMC_CONFIG_CMD_ARG_BITS_SHIFT);

	switch (ios->bus_width) {
	case bus_width_4:
		cfgr |= AML_MMC_CONFIG_BUS_WIDTH_4;
		break;
	case bus_width_1:
		cfgr |= AML_MMC_CONFIG_BUS_WIDTH_1;
		break;
	default:
		return (EINVAL);
	}

	cfgr |= aml8726_mmc_div(sc, ios->clock) <<
	    AML_MMC_CONFIG_CMD_CLK_DIV_SHIFT;

	CSR_WRITE_4(sc, AML_MMC_CONFIG_REG, cfgr);

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
		error = aml8726_mmc_power_on(sc);
		break;
	case power_off:
		error = aml8726_mmc_power_off(sc);
		break;
	default:
		return (EINVAL);
	}

	return (error);
}

static int
aml8726_mmc_request(device_t bus, device_t child, struct mmc_request *req)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);
	int mmc_error;

	AML_MMC_LOCK(sc);

	if (sc->cmd != NULL) {
		AML_MMC_UNLOCK(sc);
		return (EBUSY);
	}

	mmc_error = aml8726_mmc_start_command(sc, req->cmd);

	AML_MMC_UNLOCK(sc);

	/* Execute the callback after dropping the lock. */
	if (mmc_error != MMC_ERR_NONE) {
		req->cmd->error = mmc_error;
		req->done(req);
	}

	return (0);
}

static int
aml8726_mmc_read_ivar(device_t bus, device_t child,
    int which, uintptr_t *result)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);

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
		*(int *)result = AML_MMC_MAX_DMA / MMC_SECTOR_SIZE;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

static int
aml8726_mmc_write_ivar(device_t bus, device_t child,
    int which, uintptr_t value)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);

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
aml8726_mmc_get_ro(device_t bus, device_t child)
{

	return (0);
}

static int
aml8726_mmc_acquire_host(device_t bus, device_t child)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);

	AML_MMC_LOCK(sc);

	while (sc->bus_busy)
		mtx_sleep(sc, &sc->mtx, PZERO, "mmc", hz / 5);
	sc->bus_busy++;

	AML_MMC_UNLOCK(sc);

	return (0);
}

static int
aml8726_mmc_release_host(device_t bus, device_t child)
{
	struct aml8726_mmc_softc *sc = device_get_softc(bus);

	AML_MMC_LOCK(sc);

	sc->bus_busy--;
	wakeup(sc);

	AML_MMC_UNLOCK(sc);

	return (0);
}

static device_method_t aml8726_mmc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_mmc_probe),
	DEVMETHOD(device_attach,	aml8726_mmc_attach),
	DEVMETHOD(device_detach,	aml8726_mmc_detach),
	DEVMETHOD(device_shutdown,	aml8726_mmc_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	aml8726_mmc_read_ivar),
	DEVMETHOD(bus_write_ivar,	aml8726_mmc_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	aml8726_mmc_update_ios),
	DEVMETHOD(mmcbr_request,	aml8726_mmc_request),
	DEVMETHOD(mmcbr_get_ro,		aml8726_mmc_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	aml8726_mmc_acquire_host),
	DEVMETHOD(mmcbr_release_host,	aml8726_mmc_release_host),

	DEVMETHOD_END
};

static driver_t aml8726_mmc_driver = {
	"aml8726_mmc",
	aml8726_mmc_methods,
	sizeof(struct aml8726_mmc_softc),
};

static devclass_t aml8726_mmc_devclass;

DRIVER_MODULE(aml8726_mmc, simplebus, aml8726_mmc_driver,
    aml8726_mmc_devclass, NULL, NULL);
MODULE_DEPEND(aml8726_mmc, aml8726_gpio, 1, 1, 1);
MMC_DECLARE_BRIDGE(aml8726_mmc);
