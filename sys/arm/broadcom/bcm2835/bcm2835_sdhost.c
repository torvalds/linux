/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Klaus P. Ohrhallinger <k@7he.at>
 * All rights reserved.
 *
 * Based on bcm2835_sdhci.c:
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * pin 48-53 - card slot
 * pin 34-39 - radio module
 *
 * alt-0 - rubbish SDHCI  (0x7e202000) aka sdhost
 * alt-3 - advanced SDHCI (0x7e300000) aka sdhci/mmc/sdio
 *
 * driving card slot with mmc:
 *
 * sdhost_pins {
 *         brcm,pins = <0x30 0x31 0x32 0x33 0x34 0x35>;
 *         brcm,function = <0x7>;
 *         brcm,pull = <0x0 0x2 0x2 0x2 0x2 0x2>;
 *         phandle = <0x17>;
 * };
 * sdio_pins {
 *         brcm,pins = <0x22 0x23 0x24 0x25 0x26 0x27>;
 *         brcm,function = <0x4>;
 *         brcm,pull = <0x0 0x2 0x2 0x2 0x2 0x2>;
 *         phandle = <0x18>;
 * };
 *
 * driving card slot with sdhost:
 *
 * sdhost_pins {
 *         brcm,pins = <0x30 0x31 0x32 0x33 0x34 0x35>;
 *         brcm,function = <0x4>;
 *         brcm,pull = <0x0 0x2 0x2 0x2 0x2 0x2>;
 *         phandle = <0x17>;
 * };
 * sdio_pins {
 *         brcm,pins = <0x22 0x23 0x24 0x25 0x26 0x27>;
 *         brcm,function = <0x7>;
 *         brcm,pull = <0x0 0x2 0x2 0x2 0x2 0x2>;
 *         phandle = <0x18>;
 * };
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>

#include <dev/sdhci/sdhci.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

#include "opt_mmccam.h"

#include "bcm2835_dma.h"
#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>
#include "bcm2835_vcbus.h"


/* #define SDHOST_DEBUG */


/* Registers */
#define HC_COMMAND		0x00	/* Command and flags */
#define HC_ARGUMENT		0x04
#define HC_TIMEOUTCOUNTER	0x08
#define HC_CLOCKDIVISOR		0x0c
#define HC_RESPONSE_0		0x10
#define HC_RESPONSE_1		0x14
#define HC_RESPONSE_2		0x18
#define HC_RESPONSE_3		0x1c
#define HC_HOSTSTATUS		0x20
#define HC_POWER		0x30
#define HC_DEBUG		0x34
#define HC_HOSTCONFIG		0x38
#define HC_BLOCKSIZE		0x3c
#define HC_DATAPORT		0x40
#define HC_BLOCKCOUNT		0x50

/* Flags for HC_COMMAND register */
#define HC_CMD_ENABLE			0x8000
#define HC_CMD_FAILED			0x4000
#define HC_CMD_BUSY			0x0800
#define HC_CMD_RESPONSE_NONE		0x0400
#define HC_CMD_RESPONSE_LONG		0x0200
#define HC_CMD_WRITE			0x0080
#define HC_CMD_READ			0x0040
#define HC_CMD_COMMAND_MASK		0x003f

#define HC_CLOCKDIVISOR_MAXVAL		0x07ff

/* Flags for HC_HOSTSTATUS register */
#define HC_HSTST_HAVEDATA		0x0001
#define HC_HSTST_ERROR_FIFO		0x0008
#define HC_HSTST_ERROR_CRC7		0x0010
#define HC_HSTST_ERROR_CRC16		0x0020
#define HC_HSTST_TIMEOUT_CMD		0x0040
#define HC_HSTST_TIMEOUT_DATA		0x0080
#define HC_HSTST_INT_BLOCK		0x0200
#define HC_HSTST_INT_BUSY		0x0400

#define HC_HSTST_RESET			0xffff

#define HC_HSTST_MASK_ERROR_DATA	(HC_HSTST_ERROR_FIFO | \
    HC_HSTST_ERROR_CRC7 | HC_HSTST_ERROR_CRC16 | HC_HSTST_TIMEOUT_DATA)

#define HC_HSTST_MASK_ERROR_ALL		(HC_HSTST_MASK_ERROR_DATA | \
    HC_HSTST_TIMEOUT_CMD)

/* Flags for HC_HOSTCONFIG register */
#define HC_HSTCF_INTBUS_WIDE		0x0002
#define HC_HSTCF_EXTBUS_4BIT		0x0004
#define HC_HSTCF_SLOW_CARD		0x0008
#define HC_HSTCF_INT_DATA		0x0010
#define HC_HSTCF_INT_BLOCK		0x0100
#define HC_HSTCF_INT_BUSY		0x0400

/* Flags for HC_DEBUG register */
#define HC_DBG_FIFO_THRESH_WRITE_SHIFT	9
#define HC_DBG_FIFO_THRESH_READ_SHIFT	14
#define HC_DBG_FIFO_THRESH_MASK		0x001f

/* Settings */
#define HC_FIFO_SIZE		16
#define HC_FIFO_THRESH_READ	4
#define HC_FIFO_THRESH_WRITE	4

#define HC_TIMEOUT_DEFAULT	0x00f00000

#define	BCM2835_DEFAULT_SDHCI_FREQ	50

static int bcm2835_sdhost_debug = 0;

#ifdef SDHOST_DEBUG

TUNABLE_INT("hw.bcm2835.sdhost.debug", &bcm2835_sdhost_debug);
SYSCTL_INT(_hw_sdhci, OID_AUTO, bcm2835_sdhost_debug, CTLFLAG_RWTUN,
    &bcm2835_sdhost_debug, 0, "bcm2835-sdhost Debug level");

#define dprintf(fmt, args...) \
	do { \
		if (bcm2835_sdhost_debug > 0) \
			printf(fmt,##args); \
	} while (0)
#else

#define dprintf(fmt, args...)

#endif /* ! SDHOST_DEBUG */

static struct ofw_compat_data compat_data[] = {
	{"brcm,bcm2835-sdhost",		1},
	{NULL,				0}
};

struct bcm_sdhost_softc {
	device_t		sc_dev;
	struct resource *	sc_mem_res;
	struct resource *	sc_irq_res;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	void *			sc_intrhand;
	struct mmc_request *	sc_req;
	struct sdhci_slot	sc_slot;

	struct mtx		mtx;

	char			cmdbusy;
	char			mmc_app_cmd;

	u_int32_t		sdhci_int_status;
	u_int32_t		sdhci_signal_enable;
	u_int32_t		sdhci_present_state;
	u_int32_t		sdhci_blocksize;
	u_int32_t		sdhci_blockcount;

	u_int32_t		sdcard_rca;
};

static int bcm_sdhost_probe(device_t);
static int bcm_sdhost_attach(device_t);
static int bcm_sdhost_detach(device_t);
static void bcm_sdhost_intr(void *);

static int bcm_sdhost_get_ro(device_t, device_t);


static inline uint32_t
RD4(struct bcm_sdhost_softc *sc, bus_size_t off)
{
	uint32_t val;

	val = bus_space_read_4(sc->sc_bst, sc->sc_bsh, off);

	return (val);
}

static inline void
WR4(struct bcm_sdhost_softc *sc, bus_size_t off, uint32_t val)
{

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, off, val);
}

static inline uint16_t
RD2(struct bcm_sdhost_softc *sc, bus_size_t off)
{
	uint32_t val;

	val = RD4(sc, off & ~3);

	return ((val >> (off & 3)*8) & 0xffff);
}

static inline uint8_t
RD1(struct bcm_sdhost_softc *sc, bus_size_t off)
{
	uint32_t val;

	val = RD4(sc, off & ~3);

	return ((val >> (off & 3)*8) & 0xff);
}

static inline void
WR2(struct bcm_sdhost_softc *sc, bus_size_t off, uint16_t val)
{
	uint32_t val32;

	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xffff << (off & 3)*8);
	val32 |= (val << (off & 3)*8);
	WR4(sc, off & ~3, val32);
}

static inline void
WR1(struct bcm_sdhost_softc *sc, bus_size_t off, uint8_t val)
{
	uint32_t val32;

	val32 = RD4(sc, off & ~3);
	val32 &= ~(0xff << (off & 3)*8);
	val32 |= (val << (off & 3)*8);
	WR4(sc, off & ~3, val32);
}

static void
bcm_sdhost_print_regs(struct bcm_sdhost_softc *sc, struct sdhci_slot *slot,
    int line, int error)
{

	if (bcm2835_sdhost_debug > 0 || error > 0) {

		printf("%s: sc=%p slot=%p\n",
		    __func__, sc, slot);
		printf("HC_COMMAND:        0x%08x\n",
		    RD4(sc, HC_COMMAND));
		printf("HC_ARGUMENT:       0x%08x\n",
		    RD4(sc, HC_ARGUMENT));
		printf("HC_TIMEOUTCOUNTER: 0x%08x\n",
		    RD4(sc, HC_TIMEOUTCOUNTER));
		printf("HC_CLOCKDIVISOR:   0x%08x\n",
		    RD4(sc, HC_CLOCKDIVISOR));
		printf("HC_RESPONSE_0:     0x%08x\n",
		    RD4(sc, HC_RESPONSE_0));
		printf("HC_RESPONSE_1:     0x%08x\n",
		    RD4(sc, HC_RESPONSE_1));
		printf("HC_RESPONSE_2:     0x%08x\n",
		    RD4(sc, HC_RESPONSE_2));
		printf("HC_RESPONSE_3:     0x%08x\n",
		    RD4(sc, HC_RESPONSE_3));
		printf("HC_HOSTSTATUS:     0x%08x\n",
		    RD4(sc, HC_HOSTSTATUS));
		printf("HC_POWER:          0x%08x\n",
		    RD4(sc, HC_POWER));
		printf("HC_DEBUG:          0x%08x\n",
		    RD4(sc, HC_DEBUG));
		printf("HC_HOSTCONFIG:     0x%08x\n",
		    RD4(sc, HC_HOSTCONFIG));
		printf("HC_BLOCKSIZE:      0x%08x\n",
		    RD4(sc, HC_BLOCKSIZE));
		printf("HC_BLOCKCOUNT:     0x%08x\n",
		    RD4(sc, HC_BLOCKCOUNT));

	} else {

		/*
		printf("%04d | HC_COMMAND: 0x%08x HC_ARGUMENT: 0x%08x "
		    "HC_HOSTSTATUS: 0x%08x HC_HOSTCONFIG: 0x%08x\n",
		    line, RD4(sc, HC_COMMAND), RD4(sc, HC_ARGUMENT),
		    RD4(sc, HC_HOSTSTATUS), RD4(sc, HC_HOSTCONFIG));
		*/
	}
}

static void
bcm_sdhost_reset(device_t dev, struct sdhci_slot *slot)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	u_int32_t dbg;

	WR4(sc, HC_POWER, 0);

	WR4(sc, HC_COMMAND, 0);
	WR4(sc, HC_ARGUMENT, 0);
	WR4(sc, HC_TIMEOUTCOUNTER, HC_TIMEOUT_DEFAULT);
	WR4(sc, HC_CLOCKDIVISOR, 0);
	WR4(sc, HC_HOSTSTATUS, HC_HSTST_RESET);
	WR4(sc, HC_HOSTCONFIG, 0);
	WR4(sc, HC_BLOCKSIZE, 0);
	WR4(sc, HC_BLOCKCOUNT, 0);

	dbg = RD4(sc, HC_DEBUG);
	dbg &= ~( (HC_DBG_FIFO_THRESH_MASK << HC_DBG_FIFO_THRESH_READ_SHIFT) |
	          (HC_DBG_FIFO_THRESH_MASK << HC_DBG_FIFO_THRESH_WRITE_SHIFT) );
	dbg |= (HC_FIFO_THRESH_READ << HC_DBG_FIFO_THRESH_READ_SHIFT) |
	       (HC_FIFO_THRESH_WRITE << HC_DBG_FIFO_THRESH_WRITE_SHIFT);
	WR4(sc, HC_DEBUG, dbg);

	DELAY(250000);

	WR4(sc, HC_POWER, 1);

	DELAY(250000);

	sc->sdhci_present_state = SDHCI_CARD_PRESENT | SDHCI_CARD_STABLE |
		SDHCI_WRITE_PROTECT;

	WR4(sc, HC_CLOCKDIVISOR, HC_CLOCKDIVISOR_MAXVAL);
	WR4(sc, HC_HOSTCONFIG, HC_HSTCF_INT_BUSY);
}

static int
bcm_sdhost_probe(device_t dev)
{

	dprintf("%s:\n", __func__);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Broadcom 2708 SDHOST controller");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm_sdhost_attach(device_t dev)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	int rid, err;
	u_int default_freq;

	dprintf("%s: dev=%p sc=%p unit=%d\n",
	    __func__, dev, sc, device_get_unit(dev));

	mtx_init(&sc->mtx, "BCM SDHOST mtx", "bcm_sdhost",
	    MTX_DEF | MTX_RECURSE);

	sc->sc_dev = dev;
	sc->sc_req = NULL;

	sc->cmdbusy = 0;
	sc->mmc_app_cmd = 0;
	sc->sdhci_int_status = 0;
	sc->sdhci_signal_enable = 0;
	sc->sdhci_present_state = 0;
	sc->sdhci_blocksize = 0;
	sc->sdhci_blockcount = 0;

	sc->sdcard_rca = 0;

	default_freq = 50;
	err = 0;

	if (bootverbose)
		device_printf(dev, "SDHCI frequency: %dMHz\n", default_freq);

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		err = ENXIO;
		goto fail;
	}

	sc->sc_bst = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bsh = rman_get_bushandle(sc->sc_mem_res);

	bcm_sdhost_reset(dev, &sc->sc_slot);

	bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 0);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		device_printf(dev, "cannot allocate interrupt\n");
		err = ENXIO;
		goto fail;
	}

	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, bcm_sdhost_intr, sc, &sc->sc_intrhand)) {
		device_printf(dev, "cannot setup interrupt handler\n");
		err = ENXIO;
		goto fail;
	}

	sc->sc_slot.caps = 0;
	sc->sc_slot.caps |= SDHCI_CAN_VDD_330;
	sc->sc_slot.caps |= SDHCI_CAN_DO_HISPD;
	sc->sc_slot.caps |= (default_freq << SDHCI_CLOCK_BASE_SHIFT);

	sc->sc_slot.quirks = 0;
	sc->sc_slot.quirks |= SDHCI_QUIRK_MISSING_CAPS;
	sc->sc_slot.quirks |= SDHCI_QUIRK_DONT_SHIFT_RESPONSE;

	sc->sc_slot.opt = 0;

	/* XXX ?
	sc->slot->timeout_clk = ...;
	*/

	sdhci_init_slot(dev, &sc->sc_slot, 0);

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	sdhci_start_slot(&sc->sc_slot);

	return (0);

    fail:
	if (sc->sc_intrhand)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intrhand);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (err);
}

static int
bcm_sdhost_detach(device_t dev)
{

	dprintf("%s:\n", __func__);

	return (EBUSY);
}

/*
 * rv 0 --> command finished
 * rv 1 --> command timed out
 */
static inline int
bcm_sdhost_waitcommand(struct bcm_sdhost_softc *sc)
{
	int timeout = 1000;

	mtx_assert(&sc->mtx, MA_OWNED);

	while ((RD4(sc, HC_COMMAND) & HC_CMD_ENABLE) && --timeout > 0) {
		DELAY(100);
	}

	return ((timeout > 0) ? 0 : 1);
}

static int
bcm_sdhost_waitcommand_status(struct bcm_sdhost_softc *sc)
{
	u_int32_t cdst;
	int i;

	/* wait for card to change status from
	 * ''prg'' to ''trn''
	 * card status: sd specs p. 103
	 */
	i = 0;
	do {
		DELAY(1000);
		WR4(sc, HC_ARGUMENT, sc->sdcard_rca << 16);
		WR4(sc, HC_COMMAND,
		    MMC_SEND_STATUS | HC_CMD_ENABLE);
		bcm_sdhost_waitcommand(sc);
		cdst = RD4(sc, HC_RESPONSE_0);
		dprintf("%s: card status %08x (cs %d)\n",
		    __func__, cdst, (cdst & 0x0e00) >> 9);
		if (i++ > 100) {
			printf("%s: giving up, "
			    "card status %08x (cs %d)\n",
			    __func__, cdst,
			    (cdst & 0x0e00) >> 9);
			return (1);
			break;
		}
	} while (((cdst & 0x0e00) >> 9) != 4);

	return (0);
}

static void
bcm_sdhost_intr(void *arg)
{
	struct bcm_sdhost_softc *sc = arg;
	struct sdhci_slot *slot = &sc->sc_slot;
	uint32_t hstst;
	uint32_t cmd;

	mtx_lock(&sc->mtx);

	hstst = RD4(sc, HC_HOSTSTATUS);
	cmd = RD4(sc, HC_COMMAND);
	if (hstst & HC_HSTST_HAVEDATA) {
		if (cmd & HC_CMD_READ) {
			sc->sdhci_present_state |= SDHCI_DATA_AVAILABLE;
			sc->sdhci_int_status |= SDHCI_INT_DATA_AVAIL;
		} else if (cmd & HC_CMD_WRITE) {
			sc->sdhci_present_state |= SDHCI_SPACE_AVAILABLE;
			sc->sdhci_int_status |= SDHCI_INT_SPACE_AVAIL;
		} else {
			panic("%s: hstst & HC_HSTST_HAVEDATA but no "
			    "HC_CMD_READ or HC_CMD_WRITE: cmd=%0x8 "
			    "hstst=%08x\n", __func__, cmd, hstst);
		}
	} else {
		sc->sdhci_present_state &=
		    ~(SDHCI_DATA_AVAILABLE|SDHCI_SPACE_AVAILABLE);
		sc->sdhci_int_status &=
		    ~(SDHCI_INT_DATA_AVAIL|SDHCI_INT_SPACE_AVAIL);
	}

	if (hstst & HC_HSTST_MASK_ERROR_ALL) {
		printf("%s: ERROR: HC_HOSTSTATUS: %08x\n", __func__, hstst);
		bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		sc->sdhci_int_status |= SDHCI_INT_ERROR;
	} else {
		sc->sdhci_int_status &= ~SDHCI_INT_ERROR;
	}

	dprintf("%s: hstst=%08x offset=%08lx sdhci_present_state=%08x "
	    "sdhci_int_status=%08x\n", __func__, hstst, slot->offset,
	    sc->sdhci_present_state, sc->sdhci_int_status);

	sdhci_generic_intr(&sc->sc_slot);

	sc->sdhci_int_status &=
	    ~(SDHCI_INT_ERROR|SDHCI_INT_DATA_AVAIL|SDHCI_INT_DATA_END);
	sc->sdhci_present_state &= ~SDHCI_DATA_AVAILABLE;

	if ((hstst & HC_HSTST_HAVEDATA) &&
	    (sc->sdhci_blocksize * sc->sdhci_blockcount == slot->offset)) {
		dprintf("%s: offset=%08lx sdhci_blocksize=%08x "
		    "sdhci_blockcount=%08x\n", __func__, slot->offset,
		    sc->sdhci_blocksize, sc->sdhci_blockcount);
		sc->sdhci_int_status &=
		    ~(SDHCI_INT_DATA_AVAIL|SDHCI_INT_SPACE_AVAIL);
		sc->sdhci_int_status |= SDHCI_INT_DATA_END;
		sdhci_generic_intr(&sc->sc_slot);
		sc->sdhci_int_status &= ~SDHCI_INT_DATA_END;

		if ((cmd & HC_CMD_COMMAND_MASK) == MMC_READ_MULTIPLE_BLOCK ||
		    (cmd & HC_CMD_COMMAND_MASK) == MMC_WRITE_MULTIPLE_BLOCK) {
			WR4(sc, HC_ARGUMENT, 0x00000000);
			WR4(sc, HC_COMMAND,
			    MMC_STOP_TRANSMISSION | HC_CMD_ENABLE);

			if (bcm_sdhost_waitcommand(sc)) {
				printf("%s: timeout #1\n", __func__);
				bcm_sdhost_print_regs(sc, &sc->sc_slot,
				    __LINE__, 1);
			}
		}

		if (cmd & HC_CMD_WRITE) {
			if (bcm_sdhost_waitcommand_status(sc) != 0)
				sc->sdhci_int_status |= SDHCI_INT_ERROR;
		}

		slot->data_done = 1;

		sc->sdhci_int_status |= SDHCI_INT_RESPONSE;
		sdhci_generic_intr(&sc->sc_slot);
		sc->sdhci_int_status &= ~(SDHCI_INT_RESPONSE|SDHCI_INT_ERROR);
	}

	/* this resets the interrupt */
	WR4(sc, HC_HOSTSTATUS,
	    (HC_HSTST_INT_BUSY|HC_HSTST_INT_BLOCK|HC_HSTST_HAVEDATA));

	mtx_unlock(&sc->mtx);
}

static int
bcm_sdhost_get_ro(device_t bus, device_t child)
{

	dprintf("%s:\n", __func__);

	return (0);
}

static bool
bcm_sdhost_get_card_present(device_t dev, struct sdhci_slot *slot)
{

	dprintf("%s:\n", __func__);

	return (1);
}

static void
bcm_sdhost_command(device_t dev, struct sdhci_slot *slot, uint16_t val)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	struct mmc_data *data = slot->curcmd->data;
	uint16_t val2;
	uint8_t opcode;
	uint8_t flags;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (RD4(sc, HC_COMMAND) & HC_CMD_ENABLE) {
		panic("%s: HC_CMD_ENABLE on entry\n", __func__);
	}

	if (sc->cmdbusy == 1)
		panic("%s: cmdbusy\n", __func__);

	sc->cmdbusy = 1;

	val2 = ((val >> 8) & HC_CMD_COMMAND_MASK) | HC_CMD_ENABLE;

	opcode = val >> 8;
	flags = val & 0xff;

	if (opcode == MMC_APP_CMD)
		sc->mmc_app_cmd = 1;

	if ((flags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_LONG)
		val2 |= HC_CMD_RESPONSE_LONG;
	else if ((flags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_SHORT_BUSY)
		/* XXX XXX when enabled, cmd 7 (select card) blocks forever */
		;/*val2 |= HC_CMD_BUSY; */
	else if ((flags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_SHORT)
		;
	else
		val2 |= HC_CMD_RESPONSE_NONE;

	if (val2 & HC_CMD_BUSY)
		sc->sdhci_present_state |=
		    SDHCI_CMD_INHIBIT | SDHCI_DAT_INHIBIT;

	if (data != NULL && data->flags & MMC_DATA_READ)
		val2 |= HC_CMD_READ;
	else if (data != NULL && data->flags & MMC_DATA_WRITE)
		val2 |= HC_CMD_WRITE;

	dprintf("%s: SDHCI_COMMAND_FLAGS --> HC_COMMAND   %04x --> %04x\n",
	    __func__, val, val2);

	if (opcode == MMC_READ_MULTIPLE_BLOCK ||
	    opcode == MMC_WRITE_MULTIPLE_BLOCK) {
		u_int32_t save_sdarg;

		dprintf("%s: issuing MMC_SET_BLOCK_COUNT: CMD %08x ARG %08x\n",
		    __func__, MMC_SET_BLOCK_COUNT | HC_CMD_ENABLE,
		    sc->sdhci_blockcount);

		save_sdarg = RD4(sc, HC_ARGUMENT);
		WR4(sc, HC_ARGUMENT, sc->sdhci_blockcount);
		WR4(sc, HC_COMMAND, MMC_SET_BLOCK_COUNT | HC_CMD_ENABLE);

		/* Seems to always return timeout */

		if (bcm_sdhost_waitcommand(sc)) {
			printf("%s: timeout #2\n", __func__);
			bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		} else {
			bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 0);
		}
		WR4(sc, HC_ARGUMENT, save_sdarg);

	} else if (opcode == MMC_SELECT_CARD) {
		sc->sdcard_rca = (RD4(sc, HC_ARGUMENT) >> 16);
	}

	/* actually issuing the command */
	WR4(sc, HC_COMMAND, val2);

	if (val2 & HC_CMD_READ || val2 & HC_CMD_WRITE) {
		u_int8_t hstcfg;

		hstcfg = RD4(sc, HC_HOSTCONFIG);
		hstcfg |= (HC_HSTCF_INT_BUSY | HC_HSTCF_INT_DATA);
		WR4(sc, HC_HOSTCONFIG, hstcfg);
		slot->data_done = 0;

		if (bcm_sdhost_waitcommand(sc)) {
			printf("%s: timeout #3\n", __func__);
			bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		}

	} else if (opcode == MMC_ERASE) {
		if (bcm_sdhost_waitcommand_status(sc) != 0) {
			printf("%s: timeout #4\n", __func__);
			bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		}
		slot->data_done = 1;
		sc->sdhci_present_state &=
		    ~(SDHCI_CMD_INHIBIT | SDHCI_DAT_INHIBIT);

	} else {
		if (bcm_sdhost_waitcommand(sc)) {
			printf("%s: timeout #5\n", __func__);
			bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		}
		slot->data_done = 1;
		sc->sdhci_present_state &=
		    ~(SDHCI_CMD_INHIBIT | SDHCI_DAT_INHIBIT);
	}

	bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 0);

	if (RD4(sc, HC_HOSTSTATUS) & HC_HSTST_TIMEOUT_CMD)
		slot->curcmd->error = MMC_ERR_TIMEOUT;
	else if (RD4(sc, HC_COMMAND) & HC_CMD_FAILED)
		slot->curcmd->error = MMC_ERR_FAILED;

	dprintf("%s: curcmd->flags=%d data_done=%d\n",
	    __func__, slot->curcmd->flags, slot->data_done);

	if (val2 & HC_CMD_RESPONSE_NONE)
		slot->curcmd->error = 0;

	if (sc->mmc_app_cmd == 1 && opcode != MMC_APP_CMD)
		sc->mmc_app_cmd = 0;

	if (RD4(sc, HC_COMMAND) & HC_CMD_ENABLE) {
		bcm_sdhost_print_regs(sc, &sc->sc_slot, __LINE__, 1);
		panic("%s: still HC_CMD_ENABLE on exit\n", __func__);
	}

	sc->cmdbusy = 0;

	if (!(val2 & HC_CMD_READ || val2 & HC_CMD_WRITE))
		sc->sdhci_int_status |= SDHCI_INT_RESPONSE;

	/* HACK, so sdhci_finish_command() does not
	 * have to be exported
	 */
	mtx_unlock(&slot->mtx);
	sdhci_generic_intr(slot);
	mtx_lock(&slot->mtx);
	sc->sdhci_int_status &= ~SDHCI_INT_RESPONSE;
}

static uint8_t
bcm_sdhost_read_1(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint32_t val1, val2;

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_HOST_CONTROL:
		val1 = RD4(sc, HC_HOSTCONFIG);
		val2 = 0;
		if (val1 & HC_HSTCF_EXTBUS_4BIT)
			val2 |= SDHCI_CTRL_4BITBUS;
		dprintf("%s: SDHCI_HOST_CONTROL --> HC_HOSTCONFIG val2 %02x\n",
		    __func__, val2);
		break;
	case SDHCI_POWER_CONTROL:
		val1 = RD1(sc, HC_POWER);
		val2 = (val1 == 1) ? 0x0f : 0;
		dprintf("%s: SDHCI_POWER_CONTROL --> HC_POWER     val2 %02x\n",
		    __func__, val2);
		break;
	case SDHCI_BLOCK_GAP_CONTROL:
		dprintf("%s: SDHCI_BLOCK_GAP_CONTROL\n", __func__);
		val2 = 0;
		break;
	case SDHCI_WAKE_UP_CONTROL:
		dprintf("%s: SDHCI_WAKE_UP_CONTROL\n", __func__);
		val2 = 0;
		break;
	case SDHCI_TIMEOUT_CONTROL:
		dprintf("%s: SDHCI_TIMEOUT_CONTROL\n", __func__);
		val2 = 0;
		break;
	case SDHCI_SOFTWARE_RESET:
		dprintf("%s: SDHCI_SOFTWARE_RESET\n", __func__);
		val2 = 0;
		break;
	case SDHCI_ADMA_ERR:
		dprintf("%s: SDHCI_ADMA_ERR\n", __func__);
		val2 = 0;
		break;
	default:
		dprintf("%s: UNKNOWN off=%08lx\n", __func__, off);
		val2 = 0;
		break;
	}

	mtx_unlock(&sc->mtx);

	return (val2);
}

static uint16_t
bcm_sdhost_read_2(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint32_t val2, val; /* = RD4(sc, off & ~3); */

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_BLOCK_SIZE:
		val2 = sc->sdhci_blocksize;
		dprintf("%s: SDHCI_BLOCK_SIZE      --> HC_BLOCKSIZE   %08x\n",
		    __func__, val2);
		break;
	case SDHCI_BLOCK_COUNT:
		val2 = sc->sdhci_blockcount;
		dprintf("%s: SDHCI_BLOCK_COUNT     --> HC_BLOCKCOUNT  %08x\n",
		    __func__, val2);
		break;
	case SDHCI_TRANSFER_MODE:
		dprintf("%s: SDHCI_TRANSFER_MODE\n", __func__);
		val2 = 0;
		break;
	case SDHCI_CLOCK_CONTROL:
		val = RD4(sc, HC_CLOCKDIVISOR);
		val2 = (val << SDHCI_DIVIDER_SHIFT) |
		    SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN |
		    SDHCI_CLOCK_INT_STABLE;
		dprintf("%s: SDHCI_CLOCK_CONTROL     %04x --> %04x\n",
		    __func__, val, val2);
		break;
	case SDHCI_ACMD12_ERR:
		dprintf("%s: SDHCI_ACMD12_ERR\n", __func__);
		val2 = 0;
		break;
	case SDHCI_HOST_CONTROL2:
		dprintf("%s: SDHCI_HOST_CONTROL2\n", __func__);
		val2 = 0;
		break;
	case SDHCI_SLOT_INT_STATUS:
		dprintf("%s: SDHCI_SLOT_INT_STATUS\n", __func__);
		val2 = 0;
		break;
	case SDHCI_HOST_VERSION:
		dprintf("%s: SDHCI_HOST_VERSION\n", __func__);
		val2 = 0;
		break;
	default:
		dprintf("%s: UNKNOWN off=%08lx\n", __func__, off);
		val2 = 0;
		break;
	}

	mtx_unlock(&sc->mtx);

	return (val2);
}

static uint32_t
bcm_sdhost_read_4(device_t dev, struct sdhci_slot *slot, bus_size_t off)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint32_t val2;

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_DMA_ADDRESS:
		dprintf("%s: SDHCI_DMA_ADDRESS\n", __func__);
		val2 = 0;
		break;
	case SDHCI_ARGUMENT:
		dprintf("%s: SDHCI_ARGUMENT\n", __func__);
		val2 = (RD4(sc, HC_COMMAND) << 16) |
		    (RD4(sc, HC_ARGUMENT) & 0x0000ffff);
		break;
	case SDHCI_RESPONSE + 0:
		val2 = RD4(sc, HC_RESPONSE_0);
		dprintf("%s: SDHCI_RESPONSE+0       %08x\n", __func__, val2);
		break;
	case SDHCI_RESPONSE + 4:
		val2 = RD4(sc, HC_RESPONSE_1);
		dprintf("%s: SDHCI_RESPONSE+4       %08x\n", __func__, val2);
		break;
	case SDHCI_RESPONSE + 8:
		val2 = RD4(sc, HC_RESPONSE_2);
		dprintf("%s: SDHCI_RESPONSE+8       %08x\n", __func__, val2);
		break;
	case SDHCI_RESPONSE + 12:
		val2 = RD4(sc, HC_RESPONSE_3);
		dprintf("%s: SDHCI_RESPONSE+12      %08x\n", __func__, val2);
		break;
	case SDHCI_BUFFER:
		dprintf("%s: SDHCI_BUFFER\n", __func__);
		val2 = 0;
		break;
	case SDHCI_PRESENT_STATE:
		dprintf("%s: SDHCI_PRESENT_STATE      %08x\n",
		    __func__, sc->sdhci_present_state);
		val2 = sc->sdhci_present_state;
		break;
	case SDHCI_INT_STATUS:
		dprintf("%s: SDHCI_INT_STATUS        %08x\n",
		    __func__, sc->sdhci_int_status);
		val2 = sc->sdhci_int_status;
		break;
	case SDHCI_INT_ENABLE:
		dprintf("%s: SDHCI_INT_ENABLE\n", __func__);
		val2 = 0;
		break;
	case SDHCI_SIGNAL_ENABLE:
		dprintf("%s: SDHCI_SIGNAL_ENABLE      %08x\n",
		    __func__, sc->sdhci_signal_enable);
		val2 = sc->sdhci_signal_enable;
		break;
	case SDHCI_CAPABILITIES:
		val2 = 0;
		break;
	case SDHCI_CAPABILITIES2:
		dprintf("%s: SDHCI_CAPABILITIES2\n", __func__);
		val2 = 0;
		break;
	case SDHCI_MAX_CURRENT:
		dprintf("%s: SDHCI_MAX_CURRENT\n", __func__);
		val2 = 0;
		break;
	case SDHCI_ADMA_ADDRESS_LO:
		dprintf("%s: SDHCI_ADMA_ADDRESS_LO\n", __func__);
		val2 = 0;
		break;
	default:
		dprintf("%s: UNKNOWN off=%08lx\n", __func__, off);
		val2 = 0;
		break;
	}

	mtx_unlock(&sc->mtx);

	return (val2);
}

static void
bcm_sdhost_read_multi_4(device_t dev, struct sdhci_slot *slot, bus_size_t off,
    uint32_t *data, bus_size_t count)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	bus_size_t i;
	bus_size_t avail;
	uint32_t edm;

	mtx_lock(&sc->mtx);

	dprintf("%s: off=%08lx count=%08lx\n", __func__, off, count);

	for (i = 0; i < count;) {
		edm = RD4(sc, HC_DEBUG);
		avail = ((edm >> 4) & 0x1f);
		if (i + avail > count)
			avail = count - i;
		if (avail > 0)
			bus_space_read_multi_4(sc->sc_bst, sc->sc_bsh,
			    HC_DATAPORT, data + i, avail);
		i += avail;
		DELAY(1);
	}

	mtx_unlock(&sc->mtx);
}

static void
bcm_sdhost_write_1(device_t dev, struct sdhci_slot *slot,
    bus_size_t off, uint8_t val)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint32_t val2;

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_HOST_CONTROL:
		val2 = RD4(sc, HC_HOSTCONFIG);
		val2 |= HC_HSTCF_INT_BUSY;
		val2 |= HC_HSTCF_INTBUS_WIDE | HC_HSTCF_SLOW_CARD;
		if (val & SDHCI_CTRL_4BITBUS)
			val2 |= HC_HSTCF_EXTBUS_4BIT;
		dprintf("%s: SDHCI_HOST_CONTROL --> HC_HOSTC %04x --> %04x\n",
		    __func__, val, val2);
		WR4(sc, HC_HOSTCONFIG, val2);
		break;
	case SDHCI_POWER_CONTROL:
		val2 = (val != 0) ? 1 : 0;
		dprintf("%s: SDHCI_POWER_CONTROL --> HC_POWER %02x --> %02x\n",
		    __func__, val, val2);
		WR1(sc, HC_POWER, val2);
		break;
	case SDHCI_BLOCK_GAP_CONTROL:
		dprintf("%s: SDHCI_BLOCK_GAP_CONTROL   val=%02x\n",
		    __func__, val);
		break;
	case SDHCI_TIMEOUT_CONTROL:
		dprintf("%s: SDHCI_TIMEOUT_CONTROL     val=%02x\n",
		    __func__, val);
		break;
	case SDHCI_SOFTWARE_RESET:
		dprintf("%s: SDHCI_SOFTWARE_RESET      val=%02x\n",
		    __func__, val);
		break;
	case SDHCI_ADMA_ERR:
		dprintf("%s: SDHCI_ADMA_ERR            val=%02x\n",
		    __func__, val);
		break;
	default:
		dprintf("%s: UNKNOWN off=%08lx val=%08x\n",
		    __func__, off, val);
		break;
	}

	mtx_unlock(&sc->mtx);
}

static void
bcm_sdhost_write_2(device_t dev, struct sdhci_slot *slot, bus_size_t off, uint16_t val)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint16_t val2;

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_BLOCK_SIZE:
		dprintf("%s: SDHCI_BLOCK_SIZE          val=%04x\n" ,
		    __func__, val);
		sc->sdhci_blocksize = val;
		WR2(sc, HC_BLOCKSIZE, val);
		break;

	case SDHCI_BLOCK_COUNT:
		dprintf("%s: SDHCI_BLOCK_COUNT         val=%04x\n" ,
		    __func__, val);
		sc->sdhci_blockcount = val;
		WR2(sc, HC_BLOCKCOUNT, val);
		break;

	case SDHCI_TRANSFER_MODE:
		dprintf("%s: SDHCI_TRANSFER_MODE       val=%04x\n" ,
		    __func__, val);
		break;

	case SDHCI_COMMAND_FLAGS:
		bcm_sdhost_command(dev, slot, val);
		break;

	case SDHCI_CLOCK_CONTROL:
		val2 = (val & ~SDHCI_DIVIDER_MASK) >> SDHCI_DIVIDER_SHIFT;
		/* get crc16 errors with cdiv=0 */
		if (val2 == 0)
			val2 = 1;
		dprintf("%s: SDHCI_CLOCK_CONTROL       %04x --> SCDIV %04x\n",
		    __func__, val, val2);
		WR4(sc, HC_CLOCKDIVISOR, val2);
		break;

	case SDHCI_ACMD12_ERR:
		dprintf("%s: SDHCI_ACMD12_ERR          val=%04x\n" ,
		    __func__, val);
		break;

	case SDHCI_HOST_CONTROL2:
		dprintf("%s: SDHCI_HOST_CONTROL2       val=%04x\n" ,
		    __func__, val);
		break;

	case SDHCI_SLOT_INT_STATUS:
		dprintf("%s: SDHCI_SLOT_INT_STATUS     val=%04x\n" ,
		    __func__, val);
		break;

	default:
		dprintf("%s: UNKNOWN off=%08lx val=%04x\n",
		    __func__, off, val);
		break;
	}

	mtx_unlock(&sc->mtx);
}

static void
bcm_sdhost_write_4(device_t dev, struct sdhci_slot *slot, bus_size_t off, uint32_t val)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	uint32_t val2;
	uint32_t hstcfg;

	mtx_lock(&sc->mtx);

	switch (off) {
	case SDHCI_ARGUMENT:
		val2 = val;
		dprintf("%s: SDHCI_ARGUMENT --> HC_ARGUMENT   val=%08x\n",
		    __func__, val);
		WR4(sc, HC_ARGUMENT, val2);
		break;
	case SDHCI_INT_STATUS:
		dprintf("%s: SDHCI_INT_STATUS           val=%08x\n",
		    __func__, val);
		sc->sdhci_int_status = val;
		break;
	case SDHCI_INT_ENABLE:
		dprintf("%s: SDHCI_INT_ENABLE          val=%08x\n" ,
		     __func__, val);
		break;
	case SDHCI_SIGNAL_ENABLE:
		sc->sdhci_signal_enable = val;
		hstcfg = RD4(sc, HC_HOSTCONFIG);
		if (val != 0)
			hstcfg &= ~(HC_HSTCF_INT_BLOCK | HC_HSTCF_INT_DATA);
		else
			hstcfg |= (HC_HSTCF_INT_BUSY|HC_HSTCF_INT_BLOCK|
			         HC_HSTCF_INT_DATA);
		hstcfg |= HC_HSTCF_INT_BUSY;
		dprintf("%s: SDHCI_SIGNAL_ENABLE --> HC_HOSTC %08x --> %08x\n" ,
		    __func__, val, hstcfg);
		WR4(sc, HC_HOSTCONFIG, hstcfg);
		break;
	case SDHCI_CAPABILITIES:
		dprintf("%s: SDHCI_CAPABILITIES        val=%08x\n",
		    __func__, val);
		break;
	case SDHCI_CAPABILITIES2:
		dprintf("%s: SDHCI_CAPABILITIES2       val=%08x\n",
		    __func__, val);
		break;
	case SDHCI_MAX_CURRENT:
		dprintf("%s: SDHCI_MAX_CURRENT         val=%08x\n",
		    __func__, val);
		break;
	case SDHCI_ADMA_ADDRESS_LO:
		dprintf("%s: SDHCI_ADMA_ADDRESS_LO     val=%08x\n",
		    __func__, val);
		break;
	default:
		dprintf("%s: UNKNOWN off=%08lx val=%08x\n",
		    __func__, off, val);
		break;
	}

	mtx_unlock(&sc->mtx);
}

static void
bcm_sdhost_write_multi_4(device_t dev, struct sdhci_slot *slot,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct bcm_sdhost_softc *sc = device_get_softc(dev);
	bus_size_t i;
	bus_size_t space;
	uint32_t edm;

	mtx_lock(&sc->mtx);

	dprintf("%s: off=%08lx count=%02lx\n", __func__, off, count);

	for (i = 0; i < count;) {
		edm = RD4(sc, HC_DEBUG);
		space = HC_FIFO_SIZE - ((edm >> 4) & 0x1f);
		if (i + space > count)
			space = count - i;
		if (space > 0)
			bus_space_write_multi_4(sc->sc_bst, sc->sc_bsh,
			    HC_DATAPORT, data + i, space);
		i += space;
		DELAY(1);
        }

	/* wait until FIFO is really empty */
	while (((RD4(sc, HC_DEBUG) >> 4) & 0x1f) > 0)
		DELAY(1);

	mtx_unlock(&sc->mtx);
}

static device_method_t bcm_sdhost_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm_sdhost_probe),
	DEVMETHOD(device_attach,	bcm_sdhost_attach),
	DEVMETHOD(device_detach,	bcm_sdhost_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	sdhci_generic_update_ios),
	DEVMETHOD(mmcbr_request,	sdhci_generic_request),
	DEVMETHOD(mmcbr_get_ro,		bcm_sdhost_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,	sdhci_generic_release_host),

	/* SDHCI registers accessors */
	DEVMETHOD(sdhci_read_1,		bcm_sdhost_read_1),
	DEVMETHOD(sdhci_read_2,		bcm_sdhost_read_2),
	DEVMETHOD(sdhci_read_4,		bcm_sdhost_read_4),
	DEVMETHOD(sdhci_read_multi_4,	bcm_sdhost_read_multi_4),
	DEVMETHOD(sdhci_write_1,	bcm_sdhost_write_1),
	DEVMETHOD(sdhci_write_2,	bcm_sdhost_write_2),
	DEVMETHOD(sdhci_write_4,	bcm_sdhost_write_4),
	DEVMETHOD(sdhci_write_multi_4,	bcm_sdhost_write_multi_4),
	DEVMETHOD(sdhci_get_card_present,bcm_sdhost_get_card_present),

	DEVMETHOD_END
};

static devclass_t bcm_sdhost_devclass;

static driver_t bcm_sdhost_driver = {
	"sdhost_bcm",
	bcm_sdhost_methods,
	sizeof(struct bcm_sdhost_softc),
};

DRIVER_MODULE(sdhost_bcm, simplebus, bcm_sdhost_driver, bcm_sdhost_devclass,
    NULL, NULL);
SDHCI_DEPEND(sdhost_bcm);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhost_bcm);
#endif
