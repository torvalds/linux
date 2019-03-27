/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
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
 * i.MX6 Smart Direct Memory Access Controller (sDMA)
 * Chapter 41, i.MX 6Dual/6Quad Applications Processor Reference Manual,
 * Rev. 1, 04/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/firmware.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/imx/imx6_sdma.h>

#define	MAX_BD	(PAGE_SIZE / sizeof(struct sdma_buffer_descriptor))

#define	READ4(_sc, _reg)	\
	bus_space_read_4(_sc->bst, _sc->bsh, _reg)
#define	WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst, _sc->bsh, _reg, _val)

struct sdma_softc *sdma_sc;

static struct resource_spec sdma_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static void
sdma_intr(void *arg)
{
	struct sdma_buffer_descriptor *bd;
	struct sdma_channel *channel;
	struct sdma_conf *conf;
	struct sdma_softc *sc;
	int pending;
	int i;
	int j;

	sc = arg;

	pending = READ4(sc, SDMAARM_INTR);

	/* Ack intr */
	WRITE4(sc, SDMAARM_INTR, pending);

	for (i = 0; i < SDMA_N_CHANNELS; i++) {
		if ((pending & (1 << i)) == 0)
			continue;
		channel = &sc->channel[i];
		conf = channel->conf;
		if (!conf)
			continue;
		for (j = 0; j < conf->num_bd; j++) {
			bd = &channel->bd[j];
			bd->mode.status |= BD_DONE;
			if (bd->mode.status & BD_RROR)
				printf("sDMA error\n");
		}

		conf->ih(conf->ih_user, 1);

		WRITE4(sc, SDMAARM_HSTART, (1 << i));
	}
}

static int
sdma_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,imx6q-sdma"))
		return (ENXIO);

	device_set_desc(dev, "i.MX6 Smart Direct Memory Access Controller");
	return (BUS_PROBE_DEFAULT);
}

int
sdma_start(int chn)
{
	struct sdma_softc *sc;

	sc = sdma_sc;

	WRITE4(sc, SDMAARM_HSTART, (1 << chn));

	return (0);
}

int
sdma_stop(int chn)
{
	struct sdma_softc *sc;

	sc = sdma_sc;

	WRITE4(sc, SDMAARM_STOP_STAT, (1 << chn));

	return (0);
}

int
sdma_alloc(void)
{
	struct sdma_channel *channel;
	struct sdma_softc *sc;
	int found;
	int chn;
	int i;

	sc = sdma_sc;
	found = 0;

	/* Channel 0 can't be used */
	for (i = 1; i < SDMA_N_CHANNELS; i++) {
		channel = &sc->channel[i];
		if (channel->in_use == 0) {
			channel->in_use = 1;
			found = 1;
			break;
		}
	}

	if (!found)
		return (-1);

	chn = i;

	/* Allocate area for buffer descriptors */
	channel->bd = (void *)kmem_alloc_contig(PAGE_SIZE, M_ZERO, 0, ~0,
	    PAGE_SIZE, 0, VM_MEMATTR_UNCACHEABLE);

	return (chn);
}

int
sdma_free(int chn)
{
	struct sdma_channel *channel;
	struct sdma_softc *sc;

	sc = sdma_sc;

	channel = &sc->channel[chn];
	channel->in_use = 0;

	kmem_free((vm_offset_t)channel->bd, PAGE_SIZE);

	return (0);
}

static int
sdma_overrides(struct sdma_softc *sc, int chn,
		int evt, int host, int dsp)
{
	int reg;

	/* Ignore sDMA requests */
	reg = READ4(sc, SDMAARM_EVTOVR);
	if (evt)
		reg |= (1 << chn);
	else
		reg &= ~(1 << chn);
	WRITE4(sc, SDMAARM_EVTOVR, reg);

	/* Ignore enable bit (HE) */
	reg = READ4(sc, SDMAARM_HOSTOVR);
	if (host)
		reg |= (1 << chn);
	else
		reg &= ~(1 << chn);
	WRITE4(sc, SDMAARM_HOSTOVR, reg);

	/* Prevent sDMA channel from starting */
	reg = READ4(sc, SDMAARM_DSPOVR);
	if (!dsp)
		reg |= (1 << chn);
	else
		reg &= ~(1 << chn);
	WRITE4(sc, SDMAARM_DSPOVR, reg);

	return (0);
}

int
sdma_configure(int chn, struct sdma_conf *conf)
{
	struct sdma_buffer_descriptor *bd0;
	struct sdma_buffer_descriptor *bd;
	struct sdma_context_data *context;
	struct sdma_channel *channel;
	struct sdma_softc *sc;
#if 0
	int timeout;
	int ret;
#endif
	int i;

	sc = sdma_sc;

	channel = &sc->channel[chn];
	channel->conf = conf;

	/* Ensure operation has stopped */
	sdma_stop(chn);

	/* Set priority and enable the channel */
	WRITE4(sc, SDMAARM_SDMA_CHNPRI(chn), 1);
	WRITE4(sc, SDMAARM_CHNENBL(conf->event), (1 << chn));

	sdma_overrides(sc, chn, 0, 0, 0);

	if (conf->num_bd > MAX_BD) {
		device_printf(sc->dev, "Error: too much buffer"
				" descriptors requested\n");
		return (-1);
	}

	for (i = 0; i < conf->num_bd; i++) {
		bd = &channel->bd[i];
		bd->mode.command = conf->command;
		bd->mode.status = BD_DONE | BD_EXTD | BD_CONT | BD_INTR;
		if (i == (conf->num_bd - 1))
			bd->mode.status |= BD_WRAP;
		bd->mode.count = conf->period;
		bd->buffer_addr = conf->saddr + (conf->period * i);
		bd->ext_buffer_addr = 0;
	}

	sc->ccb[chn].base_bd_ptr = vtophys(channel->bd);
	sc->ccb[chn].current_bd_ptr = vtophys(channel->bd);

	/*
	 * Load context.
	 *
	 * i.MX6 Reference Manual: Appendix A SDMA Scripts
	 * A.3.1.7.1 (mcu_2_app)
	 */

	/*
	 * TODO: allow using other scripts
	 */
	context = sc->context;
	memset(context, 0, sizeof(*context));
	context->channel_state.pc = sc->fw_scripts->mcu_2_app_addr;

	/*
	 * Tx FIFO 0 address (r6)
	 * Event_mask (r1)
	 * Event2_mask (r0)
	 * Watermark level (r7)
	 */

	if (conf->event > 32) {
		context->gReg[0] = (1 << (conf->event % 32));
		context->gReg[1] = 0;
	} else {
		context->gReg[0] = 0;
		context->gReg[1] = (1 << conf->event);
	}

	context->gReg[6] = conf->daddr;
	context->gReg[7] = conf->word_length;

	bd0 = sc->bd0;
	bd0->mode.command = C0_SETDM;
	bd0->mode.status = BD_DONE | BD_INTR | BD_WRAP | BD_EXTD;
	bd0->mode.count = sizeof(*context) / 4;
	bd0->buffer_addr = sc->context_phys;
	bd0->ext_buffer_addr = 2048 + (sizeof(*context) / 4) * chn;

	WRITE4(sc, SDMAARM_HSTART, 1);

#if 0
	/* Debug purposes */

	timeout = 1000;
	while (!(ret = READ4(sc, SDMAARM_INTR) & 1)) {
		if (timeout-- <= 0)
			break;
		DELAY(10);
	};

	if (!ret) {
		device_printf(sc->dev, "Failed to load context.\n");
		return (-1);
	}

	WRITE4(sc, SDMAARM_INTR, ret);

	device_printf(sc->dev, "Context loaded successfully.\n");
#endif

	return (0);
}

static int
load_firmware(struct sdma_softc *sc)
{
	const struct sdma_firmware_header *header;
	const struct firmware *fp;

	fp = firmware_get("sdma_fw");
	if (fp == NULL) {
		device_printf(sc->dev, "Can't get firmware.\n");
		return (-1);
	}

	header = fp->data;
	if (header->magic != FW_HEADER_MAGIC) {
		device_printf(sc->dev, "Can't use firmware.\n");
		return (-1);
	}

	sc->fw_header = header;
	sc->fw_scripts = (const void *)((const char *)header +
				header->script_addrs_start);

	return (0);
}

static int
boot_firmware(struct sdma_softc *sc)
{
	struct sdma_buffer_descriptor *bd0;
	const uint32_t *ram_code;
	int timeout;
	int ret;
	int chn;
	int sz;
	int i;

	ram_code = (const void *)((const char *)sc->fw_header +
			sc->fw_header->ram_code_start);

	/* Make sure SDMA has not started yet */
	WRITE4(sc, SDMAARM_MC0PTR, 0);

	sz = SDMA_N_CHANNELS * sizeof(struct sdma_channel_control) + \
	    sizeof(struct sdma_context_data);
	sc->ccb = (void *)kmem_alloc_contig(sz, M_ZERO, 0, ~0, PAGE_SIZE, 0,
	    VM_MEMATTR_UNCACHEABLE);
	sc->ccb_phys = vtophys(sc->ccb);

	sc->context = (void *)((char *)sc->ccb + \
	    SDMA_N_CHANNELS * sizeof(struct sdma_channel_control));
	sc->context_phys = vtophys(sc->context);

	/* Disable all the channels */
	for (i = 0; i < SDMA_N_EVENTS; i++)
		WRITE4(sc, SDMAARM_CHNENBL(i), 0);

	/* All channels have priority 0 */
	for (i = 0; i < SDMA_N_CHANNELS; i++)
		WRITE4(sc, SDMAARM_SDMA_CHNPRI(i), 0);

	/* Channel 0 is used for booting firmware */
	chn = 0;

	sc->bd0 = (void *)kmem_alloc_contig(PAGE_SIZE, M_ZERO, 0, ~0, PAGE_SIZE,
	    0, VM_MEMATTR_UNCACHEABLE);
	bd0 = sc->bd0;
	sc->ccb[chn].base_bd_ptr = vtophys(bd0);
	sc->ccb[chn].current_bd_ptr = vtophys(bd0);

	WRITE4(sc, SDMAARM_SDMA_CHNPRI(chn), 1);

	sdma_overrides(sc, chn, 1, 0, 0);

	/* XXX: not sure what is that */
	WRITE4(sc, SDMAARM_CHN0ADDR, 0x4050);

	WRITE4(sc, SDMAARM_CONFIG, 0);
	WRITE4(sc, SDMAARM_MC0PTR, sc->ccb_phys);
	WRITE4(sc, SDMAARM_CONFIG, CONFIG_CSM);
	WRITE4(sc, SDMAARM_SDMA_CHNPRI(chn), 1);

	bd0->mode.command = C0_SETPM;
	bd0->mode.status = BD_DONE | BD_INTR | BD_WRAP | BD_EXTD;
	bd0->mode.count = sc->fw_header->ram_code_size / 2;
	bd0->buffer_addr = vtophys(ram_code);
	bd0->ext_buffer_addr = sc->fw_scripts->ram_code_start_addr;

	WRITE4(sc, SDMAARM_HSTART, 1);

	timeout = 100;
	while (!(ret = READ4(sc, SDMAARM_INTR) & 1)) {
		if (timeout-- <= 0)
			break;
		DELAY(10);
	}

	if (ret == 0) {
		device_printf(sc->dev, "SDMA failed to boot\n");
		return (-1);
	}

	WRITE4(sc, SDMAARM_INTR, ret);

#if 0
	device_printf(sc->dev, "SDMA booted successfully.\n");
#endif

	/* Debug is disabled */
	WRITE4(sc, SDMAARM_ONCE_ENB, 0);

	return (0);
}

static int
sdma_attach(device_t dev)
{
	struct sdma_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, sdma_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	sdma_sc = sc;

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, sdma_intr, sc, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	if (load_firmware(sc) == -1)
		return (ENXIO);

	if (boot_firmware(sc) == -1)
		return (ENXIO);

	return (0);
};

static device_method_t sdma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sdma_probe),
	DEVMETHOD(device_attach,	sdma_attach),
	{ 0, 0 }
};

static driver_t sdma_driver = {
	"sdma",
	sdma_methods,
	sizeof(struct sdma_softc),
};

static devclass_t sdma_devclass;

EARLY_DRIVER_MODULE(sdma, simplebus, sdma_driver, sdma_devclass, 0, 0,
    BUS_PASS_RESOURCE);
