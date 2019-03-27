/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2017 Marius Strobl <marius@FreeBSD.org>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcbrvar.h>

#include <dev/sdhci/sdhci.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

#include "opt_mmccam.h"

SYSCTL_NODE(_hw, OID_AUTO, sdhci, CTLFLAG_RD, 0, "sdhci driver");

static int sdhci_debug = 0;
SYSCTL_INT(_hw_sdhci, OID_AUTO, debug, CTLFLAG_RWTUN, &sdhci_debug, 0,
    "Debug level");
u_int sdhci_quirk_clear = 0;
SYSCTL_INT(_hw_sdhci, OID_AUTO, quirk_clear, CTLFLAG_RWTUN, &sdhci_quirk_clear,
    0, "Mask of quirks to clear");
u_int sdhci_quirk_set = 0;
SYSCTL_INT(_hw_sdhci, OID_AUTO, quirk_set, CTLFLAG_RWTUN, &sdhci_quirk_set, 0,
    "Mask of quirks to set");

#define	RD1(slot, off)	SDHCI_READ_1((slot)->bus, (slot), (off))
#define	RD2(slot, off)	SDHCI_READ_2((slot)->bus, (slot), (off))
#define	RD4(slot, off)	SDHCI_READ_4((slot)->bus, (slot), (off))
#define	RD_MULTI_4(slot, off, ptr, count)	\
    SDHCI_READ_MULTI_4((slot)->bus, (slot), (off), (ptr), (count))

#define	WR1(slot, off, val)	SDHCI_WRITE_1((slot)->bus, (slot), (off), (val))
#define	WR2(slot, off, val)	SDHCI_WRITE_2((slot)->bus, (slot), (off), (val))
#define	WR4(slot, off, val)	SDHCI_WRITE_4((slot)->bus, (slot), (off), (val))
#define	WR_MULTI_4(slot, off, ptr, count)	\
    SDHCI_WRITE_MULTI_4((slot)->bus, (slot), (off), (ptr), (count))

static void sdhci_acmd_irq(struct sdhci_slot *slot, uint16_t acmd_err);
static void sdhci_card_poll(void *arg);
static void sdhci_card_task(void *arg, int pending);
static void sdhci_cmd_irq(struct sdhci_slot *slot, uint32_t intmask);
static void sdhci_data_irq(struct sdhci_slot *slot, uint32_t intmask);
static int sdhci_exec_tuning(struct sdhci_slot *slot, bool reset);
static void sdhci_handle_card_present_locked(struct sdhci_slot *slot,
    bool is_present);
static void sdhci_finish_command(struct sdhci_slot *slot);
static void sdhci_init(struct sdhci_slot *slot);
static void sdhci_read_block_pio(struct sdhci_slot *slot);
static void sdhci_req_done(struct sdhci_slot *slot);
static void sdhci_req_wakeup(struct mmc_request *req);
static void sdhci_reset(struct sdhci_slot *slot, uint8_t mask);
static void sdhci_retune(void *arg);
static void sdhci_set_clock(struct sdhci_slot *slot, uint32_t clock);
static void sdhci_set_power(struct sdhci_slot *slot, u_char power);
static void sdhci_set_transfer_mode(struct sdhci_slot *slot,
   const struct mmc_data *data);
static void sdhci_start(struct sdhci_slot *slot);
static void sdhci_timeout(void *arg);
static void sdhci_start_command(struct sdhci_slot *slot,
   struct mmc_command *cmd);
static void sdhci_start_data(struct sdhci_slot *slot,
   const struct mmc_data *data);
static void sdhci_write_block_pio(struct sdhci_slot *slot);
static void sdhci_transfer_pio(struct sdhci_slot *slot);

#ifdef MMCCAM
/* CAM-related */
static void sdhci_cam_action(struct cam_sim *sim, union ccb *ccb);
static int sdhci_cam_get_possible_host_clock(const struct sdhci_slot *slot,
    int proposed_clock);
static void sdhci_cam_handle_mmcio(struct cam_sim *sim, union ccb *ccb);
static void sdhci_cam_poll(struct cam_sim *sim);
static int sdhci_cam_request(struct sdhci_slot *slot, union ccb *ccb);
static int sdhci_cam_settran_settings(struct sdhci_slot *slot, union ccb *ccb);
static int sdhci_cam_update_ios(struct sdhci_slot *slot);
#endif

/* helper routines */
static int sdhci_dma_alloc(struct sdhci_slot *slot);
static void sdhci_dma_free(struct sdhci_slot *slot);
static void sdhci_dumpregs(struct sdhci_slot *slot);
static void sdhci_getaddr(void *arg, bus_dma_segment_t *segs, int nsegs,
    int error);
static int slot_printf(const struct sdhci_slot *slot, const char * fmt, ...)
    __printflike(2, 3);
static uint32_t sdhci_tuning_intmask(const struct sdhci_slot *slot);

#define	SDHCI_LOCK(_slot)		mtx_lock(&(_slot)->mtx)
#define	SDHCI_UNLOCK(_slot)		mtx_unlock(&(_slot)->mtx)
#define	SDHCI_LOCK_INIT(_slot) \
	mtx_init(&_slot->mtx, "SD slot mtx", "sdhci", MTX_DEF)
#define	SDHCI_LOCK_DESTROY(_slot)	mtx_destroy(&_slot->mtx);
#define	SDHCI_ASSERT_LOCKED(_slot)	mtx_assert(&_slot->mtx, MA_OWNED);
#define	SDHCI_ASSERT_UNLOCKED(_slot)	mtx_assert(&_slot->mtx, MA_NOTOWNED);

#define	SDHCI_DEFAULT_MAX_FREQ	50

#define	SDHCI_200_MAX_DIVIDER	256
#define	SDHCI_300_MAX_DIVIDER	2046

#define	SDHCI_CARD_PRESENT_TICKS	(hz / 5)
#define	SDHCI_INSERT_DELAY_TICKS	(hz / 2)

/*
 * Broadcom BCM577xx Controller Constants
 */
/* Maximum divider supported by the default clock source. */
#define	BCM577XX_DEFAULT_MAX_DIVIDER	256
/* Alternative clock's base frequency. */
#define	BCM577XX_ALT_CLOCK_BASE		63000000

#define	BCM577XX_HOST_CONTROL		0x198
#define	BCM577XX_CTRL_CLKSEL_MASK	0xFFFFCFFF
#define	BCM577XX_CTRL_CLKSEL_SHIFT	12
#define	BCM577XX_CTRL_CLKSEL_DEFAULT	0x0
#define	BCM577XX_CTRL_CLKSEL_64MHZ	0x3

static void
sdhci_getaddr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{

	if (error != 0) {
		printf("getaddr: error %d\n", error);
		return;
	}
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
slot_printf(const struct sdhci_slot *slot, const char * fmt, ...)
{
	va_list ap;
	int retval;

	retval = printf("%s-slot%d: ",
	    device_get_nameunit(slot->bus), slot->num);

	va_start(ap, fmt);
	retval += vprintf(fmt, ap);
	va_end(ap);
	return (retval);
}

static void
sdhci_dumpregs(struct sdhci_slot *slot)
{

	slot_printf(slot,
	    "============== REGISTER DUMP ==============\n");

	slot_printf(slot, "Sys addr: 0x%08x | Version:  0x%08x\n",
	    RD4(slot, SDHCI_DMA_ADDRESS), RD2(slot, SDHCI_HOST_VERSION));
	slot_printf(slot, "Blk size: 0x%08x | Blk cnt:  0x%08x\n",
	    RD2(slot, SDHCI_BLOCK_SIZE), RD2(slot, SDHCI_BLOCK_COUNT));
	slot_printf(slot, "Argument: 0x%08x | Trn mode: 0x%08x\n",
	    RD4(slot, SDHCI_ARGUMENT), RD2(slot, SDHCI_TRANSFER_MODE));
	slot_printf(slot, "Present:  0x%08x | Host ctl: 0x%08x\n",
	    RD4(slot, SDHCI_PRESENT_STATE), RD1(slot, SDHCI_HOST_CONTROL));
	slot_printf(slot, "Power:    0x%08x | Blk gap:  0x%08x\n",
	    RD1(slot, SDHCI_POWER_CONTROL), RD1(slot, SDHCI_BLOCK_GAP_CONTROL));
	slot_printf(slot, "Wake-up:  0x%08x | Clock:    0x%08x\n",
	    RD1(slot, SDHCI_WAKE_UP_CONTROL), RD2(slot, SDHCI_CLOCK_CONTROL));
	slot_printf(slot, "Timeout:  0x%08x | Int stat: 0x%08x\n",
	    RD1(slot, SDHCI_TIMEOUT_CONTROL), RD4(slot, SDHCI_INT_STATUS));
	slot_printf(slot, "Int enab: 0x%08x | Sig enab: 0x%08x\n",
	    RD4(slot, SDHCI_INT_ENABLE), RD4(slot, SDHCI_SIGNAL_ENABLE));
	slot_printf(slot, "AC12 err: 0x%08x | Host ctl2:0x%08x\n",
	    RD2(slot, SDHCI_ACMD12_ERR), RD2(slot, SDHCI_HOST_CONTROL2));
	slot_printf(slot, "Caps:     0x%08x | Caps2:    0x%08x\n",
	    RD4(slot, SDHCI_CAPABILITIES), RD4(slot, SDHCI_CAPABILITIES2));
	slot_printf(slot, "Max curr: 0x%08x | ADMA err: 0x%08x\n",
	    RD4(slot, SDHCI_MAX_CURRENT), RD1(slot, SDHCI_ADMA_ERR));
	slot_printf(slot, "ADMA addr:0x%08x | Slot int: 0x%08x\n",
	    RD4(slot, SDHCI_ADMA_ADDRESS_LO), RD2(slot, SDHCI_SLOT_INT_STATUS));

	slot_printf(slot,
	    "===========================================\n");
}

static void
sdhci_reset(struct sdhci_slot *slot, uint8_t mask)
{
	int timeout;
	uint32_t clock;

	if (slot->quirks & SDHCI_QUIRK_NO_CARD_NO_RESET) {
		if (!SDHCI_GET_CARD_PRESENT(slot->bus, slot))
			return;
	}

	/* Some controllers need this kick or reset won't work. */
	if ((mask & SDHCI_RESET_ALL) == 0 &&
	    (slot->quirks & SDHCI_QUIRK_CLOCK_BEFORE_RESET)) {
		/* This is to force an update */
		clock = slot->clock;
		slot->clock = 0;
		sdhci_set_clock(slot, clock);
	}

	if (mask & SDHCI_RESET_ALL) {
		slot->clock = 0;
		slot->power = 0;
	}

	WR1(slot, SDHCI_SOFTWARE_RESET, mask);

	if (slot->quirks & SDHCI_QUIRK_WAITFOR_RESET_ASSERTED) {
		/*
		 * Resets on TI OMAPs and AM335x are incompatible with SDHCI
		 * specification.  The reset bit has internal propagation delay,
		 * so a fast read after write returns 0 even if reset process is
		 * in progress.  The workaround is to poll for 1 before polling
		 * for 0.  In the worst case, if we miss seeing it asserted the
		 * time we spent waiting is enough to ensure the reset finishes.
		 */
		timeout = 10000;
		while ((RD1(slot, SDHCI_SOFTWARE_RESET) & mask) != mask) {
			if (timeout <= 0)
				break;
			timeout--;
			DELAY(1);
		}
	}

	/* Wait max 100 ms */
	timeout = 10000;
	/* Controller clears the bits when it's done */
	while (RD1(slot, SDHCI_SOFTWARE_RESET) & mask) {
		if (timeout <= 0) {
			slot_printf(slot, "Reset 0x%x never completed.\n",
			    mask);
			sdhci_dumpregs(slot);
			return;
		}
		timeout--;
		DELAY(10);
	}
}

static uint32_t
sdhci_tuning_intmask(const struct sdhci_slot *slot)
{
	uint32_t intmask;

	intmask = 0;
	if (slot->opt & SDHCI_TUNING_ENABLED) {
		intmask |= SDHCI_INT_TUNEERR;
		if (slot->retune_mode == SDHCI_RETUNE_MODE_2 ||
		    slot->retune_mode == SDHCI_RETUNE_MODE_3)
			intmask |= SDHCI_INT_RETUNE;
	}
	return (intmask);
}

static void
sdhci_init(struct sdhci_slot *slot)
{

	sdhci_reset(slot, SDHCI_RESET_ALL);

	/* Enable interrupts. */
	slot->intmask = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
	    SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX |
	    SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT |
	    SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL |
	    SDHCI_INT_DMA_END | SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE |
	    SDHCI_INT_ACMD12ERR;

	if (!(slot->quirks & SDHCI_QUIRK_POLL_CARD_PRESENT) &&
	    !(slot->opt & SDHCI_NON_REMOVABLE)) {
		slot->intmask |= SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT;
	}

	WR4(slot, SDHCI_INT_ENABLE, slot->intmask);
	WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
}

static void
sdhci_set_clock(struct sdhci_slot *slot, uint32_t clock)
{
	uint32_t clk_base;
	uint32_t clk_sel;
	uint32_t res;
	uint16_t clk;
	uint16_t div;
	int timeout;

	if (clock == slot->clock)
		return;
	slot->clock = clock;

	/* Turn off the clock. */
	clk = RD2(slot, SDHCI_CLOCK_CONTROL);
	WR2(slot, SDHCI_CLOCK_CONTROL, clk & ~SDHCI_CLOCK_CARD_EN);
	/* If no clock requested - leave it so. */
	if (clock == 0)
		return;

	/* Determine the clock base frequency */
	clk_base = slot->max_clk;
	if (slot->quirks & SDHCI_QUIRK_BCM577XX_400KHZ_CLKSRC) {
		clk_sel = RD2(slot, BCM577XX_HOST_CONTROL) &
		    BCM577XX_CTRL_CLKSEL_MASK;

		/*
		 * Select clock source appropriate for the requested frequency.
		 */
		if ((clk_base / BCM577XX_DEFAULT_MAX_DIVIDER) > clock) {
			clk_base = BCM577XX_ALT_CLOCK_BASE;
			clk_sel |= (BCM577XX_CTRL_CLKSEL_64MHZ <<
			    BCM577XX_CTRL_CLKSEL_SHIFT);
		} else {
			clk_sel |= (BCM577XX_CTRL_CLKSEL_DEFAULT <<
			    BCM577XX_CTRL_CLKSEL_SHIFT);
		}

		WR2(slot, BCM577XX_HOST_CONTROL, clk_sel);
	}

	/* Recalculate timeout clock frequency based on the new sd clock. */
	if (slot->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK)
		slot->timeout_clk = slot->clock / 1000;

	if (slot->version < SDHCI_SPEC_300) {
		/* Looking for highest freq <= clock. */
		res = clk_base;
		for (div = 1; div < SDHCI_200_MAX_DIVIDER; div <<= 1) {
			if (res <= clock)
				break;
			res >>= 1;
		}
		/* Divider 1:1 is 0x00, 2:1 is 0x01, 256:1 is 0x80 ... */
		div >>= 1;
	} else {
		/* Version 3.0 divisors are multiples of two up to 1023 * 2 */
		if (clock >= clk_base)
			div = 0;
		else {
			for (div = 2; div < SDHCI_300_MAX_DIVIDER; div += 2) {
				if ((clk_base / div) <= clock)
					break;
			}
		}
		div >>= 1;
	}

	if (bootverbose || sdhci_debug)
		slot_printf(slot, "Divider %d for freq %d (base %d)\n",
			div, clock, clk_base);

	/* Now we have got divider, set it. */
	clk = (div & SDHCI_DIVIDER_MASK) << SDHCI_DIVIDER_SHIFT;
	clk |= ((div >> SDHCI_DIVIDER_MASK_LEN) & SDHCI_DIVIDER_HI_MASK)
		<< SDHCI_DIVIDER_HI_SHIFT;

	WR2(slot, SDHCI_CLOCK_CONTROL, clk);
	/* Enable clock. */
	clk |= SDHCI_CLOCK_INT_EN;
	WR2(slot, SDHCI_CLOCK_CONTROL, clk);
	/* Wait up to 10 ms until it stabilize. */
	timeout = 10;
	while (!((clk = RD2(slot, SDHCI_CLOCK_CONTROL))
		& SDHCI_CLOCK_INT_STABLE)) {
		if (timeout == 0) {
			slot_printf(slot,
			    "Internal clock never stabilised.\n");
			sdhci_dumpregs(slot);
			return;
		}
		timeout--;
		DELAY(1000);
	}
	/* Pass clock signal to the bus. */
	clk |= SDHCI_CLOCK_CARD_EN;
	WR2(slot, SDHCI_CLOCK_CONTROL, clk);
}

static void
sdhci_set_power(struct sdhci_slot *slot, u_char power)
{
	int i;
	uint8_t pwr;

	if (slot->power == power)
		return;

	slot->power = power;

	/* Turn off the power. */
	pwr = 0;
	WR1(slot, SDHCI_POWER_CONTROL, pwr);
	/* If power down requested - leave it so. */
	if (power == 0)
		return;
	/* Set voltage. */
	switch (1 << power) {
	case MMC_OCR_LOW_VOLTAGE:
		pwr |= SDHCI_POWER_180;
		break;
	case MMC_OCR_290_300:
	case MMC_OCR_300_310:
		pwr |= SDHCI_POWER_300;
		break;
	case MMC_OCR_320_330:
	case MMC_OCR_330_340:
		pwr |= SDHCI_POWER_330;
		break;
	}
	WR1(slot, SDHCI_POWER_CONTROL, pwr);
	/*
	 * Turn on VDD1 power.  Note that at least some Intel controllers can
	 * fail to enable bus power on the first try after transiting from D3
	 * to D0, so we give them up to 2 ms.
	 */
	pwr |= SDHCI_POWER_ON;
	for (i = 0; i < 20; i++) {
		WR1(slot, SDHCI_POWER_CONTROL, pwr);
		if (RD1(slot, SDHCI_POWER_CONTROL) & SDHCI_POWER_ON)
			break;
		DELAY(100);
	}
	if (!(RD1(slot, SDHCI_POWER_CONTROL) & SDHCI_POWER_ON))
		slot_printf(slot, "Bus power failed to enable");

	if (slot->quirks & SDHCI_QUIRK_INTEL_POWER_UP_RESET) {
		WR1(slot, SDHCI_POWER_CONTROL, pwr | 0x10);
		DELAY(10);
		WR1(slot, SDHCI_POWER_CONTROL, pwr);
		DELAY(300);
	}
}

static void
sdhci_read_block_pio(struct sdhci_slot *slot)
{
	uint32_t data;
	char *buffer;
	size_t left;

	buffer = slot->curcmd->data->data;
	buffer += slot->offset;
	/* Transfer one block at a time. */
	left = min(512, slot->curcmd->data->len - slot->offset);
	slot->offset += left;

	/* If we are too fast, broken controllers return zeroes. */
	if (slot->quirks & SDHCI_QUIRK_BROKEN_TIMINGS)
		DELAY(10);
	/* Handle unaligned and aligned buffer cases. */
	if ((intptr_t)buffer & 3) {
		while (left > 3) {
			data = RD4(slot, SDHCI_BUFFER);
			buffer[0] = data;
			buffer[1] = (data >> 8);
			buffer[2] = (data >> 16);
			buffer[3] = (data >> 24);
			buffer += 4;
			left -= 4;
		}
	} else {
		RD_MULTI_4(slot, SDHCI_BUFFER,
		    (uint32_t *)buffer, left >> 2);
		left &= 3;
	}
	/* Handle uneven size case. */
	if (left > 0) {
		data = RD4(slot, SDHCI_BUFFER);
		while (left > 0) {
			*(buffer++) = data;
			data >>= 8;
			left--;
		}
	}
}

static void
sdhci_write_block_pio(struct sdhci_slot *slot)
{
	uint32_t data = 0;
	char *buffer;
	size_t left;

	buffer = slot->curcmd->data->data;
	buffer += slot->offset;
	/* Transfer one block at a time. */
	left = min(512, slot->curcmd->data->len - slot->offset);
	slot->offset += left;

	/* Handle unaligned and aligned buffer cases. */
	if ((intptr_t)buffer & 3) {
		while (left > 3) {
			data = buffer[0] +
			    (buffer[1] << 8) +
			    (buffer[2] << 16) +
			    (buffer[3] << 24);
			left -= 4;
			buffer += 4;
			WR4(slot, SDHCI_BUFFER, data);
		}
	} else {
		WR_MULTI_4(slot, SDHCI_BUFFER,
		    (uint32_t *)buffer, left >> 2);
		left &= 3;
	}
	/* Handle uneven size case. */
	if (left > 0) {
		while (left > 0) {
			data <<= 8;
			data += *(buffer++);
			left--;
		}
		WR4(slot, SDHCI_BUFFER, data);
	}
}

static void
sdhci_transfer_pio(struct sdhci_slot *slot)
{

	/* Read as many blocks as possible. */
	if (slot->curcmd->data->flags & MMC_DATA_READ) {
		while (RD4(slot, SDHCI_PRESENT_STATE) &
		    SDHCI_DATA_AVAILABLE) {
			sdhci_read_block_pio(slot);
			if (slot->offset >= slot->curcmd->data->len)
				break;
		}
	} else {
		while (RD4(slot, SDHCI_PRESENT_STATE) &
		    SDHCI_SPACE_AVAILABLE) {
			sdhci_write_block_pio(slot);
			if (slot->offset >= slot->curcmd->data->len)
				break;
		}
	}
}

static void
sdhci_card_task(void *arg, int pending __unused)
{
	struct sdhci_slot *slot = arg;
	device_t d;

	SDHCI_LOCK(slot);
	if (SDHCI_GET_CARD_PRESENT(slot->bus, slot)) {
#ifdef MMCCAM
		if (slot->card_present == 0) {
#else
		if (slot->dev == NULL) {
#endif
			/* If card is present - attach mmc bus. */
			if (bootverbose || sdhci_debug)
				slot_printf(slot, "Card inserted\n");
#ifdef MMCCAM
			slot->card_present = 1;
			union ccb *ccb;
			uint32_t pathid;
			pathid = cam_sim_path(slot->sim);
			ccb = xpt_alloc_ccb_nowait();
			if (ccb == NULL) {
				slot_printf(slot, "Unable to alloc CCB for rescan\n");
				SDHCI_UNLOCK(slot);
				return;
			}

			/*
			 * We create a rescan request for BUS:0:0, since the card
			 * will be at lun 0.
			 */
			if (xpt_create_path(&ccb->ccb_h.path, NULL, pathid,
					    /* target */ 0, /* lun */ 0) != CAM_REQ_CMP) {
				slot_printf(slot, "Unable to create path for rescan\n");
				SDHCI_UNLOCK(slot);
				xpt_free_ccb(ccb);
				return;
			}
			SDHCI_UNLOCK(slot);
			xpt_rescan(ccb);
#else
			d = slot->dev = device_add_child(slot->bus, "mmc", -1);
			SDHCI_UNLOCK(slot);
			if (d) {
				device_set_ivars(d, slot);
				(void)device_probe_and_attach(d);
			}
#endif
		} else
			SDHCI_UNLOCK(slot);
	} else {
#ifdef MMCCAM
		if (slot->card_present == 1) {
#else
		if (slot->dev != NULL) {
#endif
			/* If no card present - detach mmc bus. */
			if (bootverbose || sdhci_debug)
				slot_printf(slot, "Card removed\n");
			d = slot->dev;
			slot->dev = NULL;
#ifdef MMCCAM
			slot->card_present = 0;
			union ccb *ccb;
			uint32_t pathid;
			pathid = cam_sim_path(slot->sim);
			ccb = xpt_alloc_ccb_nowait();
			if (ccb == NULL) {
				slot_printf(slot, "Unable to alloc CCB for rescan\n");
				SDHCI_UNLOCK(slot);
				return;
			}

			/*
			 * We create a rescan request for BUS:0:0, since the card
			 * will be at lun 0.
			 */
			if (xpt_create_path(&ccb->ccb_h.path, NULL, pathid,
					    /* target */ 0, /* lun */ 0) != CAM_REQ_CMP) {
				slot_printf(slot, "Unable to create path for rescan\n");
				SDHCI_UNLOCK(slot);
				xpt_free_ccb(ccb);
				return;
			}
			SDHCI_UNLOCK(slot);
			xpt_rescan(ccb);
#else
			slot->intmask &= ~sdhci_tuning_intmask(slot);
			WR4(slot, SDHCI_INT_ENABLE, slot->intmask);
			WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
			slot->opt &= ~SDHCI_TUNING_ENABLED;
			SDHCI_UNLOCK(slot);
			callout_drain(&slot->retune_callout);
			device_delete_child(slot->bus, d);
#endif
		} else
			SDHCI_UNLOCK(slot);
	}
}

static void
sdhci_handle_card_present_locked(struct sdhci_slot *slot, bool is_present)
{
	bool was_present;

	/*
	 * If there was no card and now there is one, schedule the task to
	 * create the child device after a short delay.  The delay is to
	 * debounce the card insert (sometimes the card detect pin stabilizes
	 * before the other pins have made good contact).
	 *
	 * If there was a card present and now it's gone, immediately schedule
	 * the task to delete the child device.  No debouncing -- gone is gone,
	 * because once power is removed, a full card re-init is needed, and
	 * that happens by deleting and recreating the child device.
	 */
#ifdef MMCCAM
	was_present = slot->card_present;
#else
	was_present = slot->dev != NULL;
#endif
	if (!was_present && is_present) {
		taskqueue_enqueue_timeout(taskqueue_swi_giant,
		    &slot->card_delayed_task, -SDHCI_INSERT_DELAY_TICKS);
	} else if (was_present && !is_present) {
		taskqueue_enqueue(taskqueue_swi_giant, &slot->card_task);
	}
}

void
sdhci_handle_card_present(struct sdhci_slot *slot, bool is_present)
{

	SDHCI_LOCK(slot);
	sdhci_handle_card_present_locked(slot, is_present);
	SDHCI_UNLOCK(slot);
}

static void
sdhci_card_poll(void *arg)
{
	struct sdhci_slot *slot = arg;

	sdhci_handle_card_present(slot,
	    SDHCI_GET_CARD_PRESENT(slot->bus, slot));
	callout_reset(&slot->card_poll_callout, SDHCI_CARD_PRESENT_TICKS,
	    sdhci_card_poll, slot);
}

static int
sdhci_dma_alloc(struct sdhci_slot *slot)
{
	int err;

	if (!(slot->quirks & SDHCI_QUIRK_BROKEN_SDMA_BOUNDARY)) {
		if (MAXPHYS <= 1024 * 4)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_4K;
		else if (MAXPHYS <= 1024 * 8)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_8K;
		else if (MAXPHYS <= 1024 * 16)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_16K;
		else if (MAXPHYS <= 1024 * 32)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_32K;
		else if (MAXPHYS <= 1024 * 64)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_64K;
		else if (MAXPHYS <= 1024 * 128)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_128K;
		else if (MAXPHYS <= 1024 * 256)
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_256K;
		else
			slot->sdma_boundary = SDHCI_BLKSZ_SDMA_BNDRY_512K;
	}
	slot->sdma_bbufsz = SDHCI_SDMA_BNDRY_TO_BBUFSZ(slot->sdma_boundary);

	/*
	 * Allocate the DMA tag for an SDMA bounce buffer.
	 * Note that the SDHCI specification doesn't state any alignment
	 * constraint for the SDMA system address.  However, controllers
	 * typically ignore the SDMA boundary bits in SDHCI_DMA_ADDRESS when
	 * forming the actual address of data, requiring the SDMA buffer to
	 * be aligned to the SDMA boundary.
	 */
	err = bus_dma_tag_create(bus_get_dma_tag(slot->bus), slot->sdma_bbufsz,
	    0, BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    slot->sdma_bbufsz, 1, slot->sdma_bbufsz, BUS_DMA_ALLOCNOW,
	    NULL, NULL, &slot->dmatag);
	if (err != 0) {
		slot_printf(slot, "Can't create DMA tag for SDMA\n");
		return (err);
	}
	/* Allocate DMA memory for the SDMA bounce buffer. */
	err = bus_dmamem_alloc(slot->dmatag, (void **)&slot->dmamem,
	    BUS_DMA_NOWAIT, &slot->dmamap);
	if (err != 0) {
		slot_printf(slot, "Can't alloc DMA memory for SDMA\n");
		bus_dma_tag_destroy(slot->dmatag);
		return (err);
	}
	/* Map the memory of the SDMA bounce buffer. */
	err = bus_dmamap_load(slot->dmatag, slot->dmamap,
	    (void *)slot->dmamem, slot->sdma_bbufsz, sdhci_getaddr,
	    &slot->paddr, 0);
	if (err != 0 || slot->paddr == 0) {
		slot_printf(slot, "Can't load DMA memory for SDMA\n");
		bus_dmamem_free(slot->dmatag, slot->dmamem, slot->dmamap);
		bus_dma_tag_destroy(slot->dmatag);
		if (err)
			return (err);
		else
			return (EFAULT);
	}

	return (0);
}

static void
sdhci_dma_free(struct sdhci_slot *slot)
{

	bus_dmamap_unload(slot->dmatag, slot->dmamap);
	bus_dmamem_free(slot->dmatag, slot->dmamem, slot->dmamap);
	bus_dma_tag_destroy(slot->dmatag);
}

int
sdhci_init_slot(device_t dev, struct sdhci_slot *slot, int num)
{
	kobjop_desc_t kobj_desc;
	kobj_method_t *kobj_method;
	uint32_t caps, caps2, freq, host_caps;
	int err;

	SDHCI_LOCK_INIT(slot);

	slot->num = num;
	slot->bus = dev;

	slot->version = (RD2(slot, SDHCI_HOST_VERSION)
		>> SDHCI_SPEC_VER_SHIFT) & SDHCI_SPEC_VER_MASK;
	if (slot->quirks & SDHCI_QUIRK_MISSING_CAPS) {
		caps = slot->caps;
		caps2 = slot->caps2;
	} else {
		caps = RD4(slot, SDHCI_CAPABILITIES);
		if (slot->version >= SDHCI_SPEC_300)
			caps2 = RD4(slot, SDHCI_CAPABILITIES2);
		else
			caps2 = 0;
	}
	if (slot->version >= SDHCI_SPEC_300) {
		if ((caps & SDHCI_SLOTTYPE_MASK) != SDHCI_SLOTTYPE_REMOVABLE &&
		    (caps & SDHCI_SLOTTYPE_MASK) != SDHCI_SLOTTYPE_EMBEDDED) {
			slot_printf(slot,
			    "Driver doesn't support shared bus slots\n");
			SDHCI_LOCK_DESTROY(slot);
			return (ENXIO);
		} else if ((caps & SDHCI_SLOTTYPE_MASK) ==
		    SDHCI_SLOTTYPE_EMBEDDED) {
			slot->opt |= SDHCI_SLOT_EMBEDDED | SDHCI_NON_REMOVABLE;
		}
	}
	/* Calculate base clock frequency. */
	if (slot->version >= SDHCI_SPEC_300)
		freq = (caps & SDHCI_CLOCK_V3_BASE_MASK) >>
		    SDHCI_CLOCK_BASE_SHIFT;
	else
		freq = (caps & SDHCI_CLOCK_BASE_MASK) >>
		    SDHCI_CLOCK_BASE_SHIFT;
	if (freq != 0)
		slot->max_clk = freq * 1000000;
	/*
	 * If the frequency wasn't in the capabilities and the hardware driver
	 * hasn't already set max_clk we're probably not going to work right
	 * with an assumption, so complain about it.
	 */
	if (slot->max_clk == 0) {
		slot->max_clk = SDHCI_DEFAULT_MAX_FREQ * 1000000;
		slot_printf(slot, "Hardware doesn't specify base clock "
		    "frequency, using %dMHz as default.\n",
		    SDHCI_DEFAULT_MAX_FREQ);
	}
	/* Calculate/set timeout clock frequency. */
	if (slot->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK) {
		slot->timeout_clk = slot->max_clk / 1000;
	} else if (slot->quirks & SDHCI_QUIRK_DATA_TIMEOUT_1MHZ) {
		slot->timeout_clk = 1000;
	} else {
		slot->timeout_clk = (caps & SDHCI_TIMEOUT_CLK_MASK) >>
		    SDHCI_TIMEOUT_CLK_SHIFT;
		if (caps & SDHCI_TIMEOUT_CLK_UNIT)
			slot->timeout_clk *= 1000;
	}
	/*
	 * If the frequency wasn't in the capabilities and the hardware driver
	 * hasn't already set timeout_clk we'll probably work okay using the
	 * max timeout, but still mention it.
	 */
	if (slot->timeout_clk == 0) {
		slot_printf(slot, "Hardware doesn't specify timeout clock "
		    "frequency, setting BROKEN_TIMEOUT quirk.\n");
		slot->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;
	}

	slot->host.f_min = SDHCI_MIN_FREQ(slot->bus, slot);
	slot->host.f_max = slot->max_clk;
	slot->host.host_ocr = 0;
	if (caps & SDHCI_CAN_VDD_330)
	    slot->host.host_ocr |= MMC_OCR_320_330 | MMC_OCR_330_340;
	if (caps & SDHCI_CAN_VDD_300)
	    slot->host.host_ocr |= MMC_OCR_290_300 | MMC_OCR_300_310;
	/* 1.8V VDD is not supposed to be used for removable cards. */
	if ((caps & SDHCI_CAN_VDD_180) && (slot->opt & SDHCI_SLOT_EMBEDDED))
	    slot->host.host_ocr |= MMC_OCR_LOW_VOLTAGE;
	if (slot->host.host_ocr == 0) {
		slot_printf(slot, "Hardware doesn't report any "
		    "support voltages.\n");
	}

	host_caps = MMC_CAP_4_BIT_DATA;
	if (caps & SDHCI_CAN_DO_8BITBUS)
		host_caps |= MMC_CAP_8_BIT_DATA;
	if (caps & SDHCI_CAN_DO_HISPD)
		host_caps |= MMC_CAP_HSPEED;
	if (slot->quirks & SDHCI_QUIRK_BOOT_NOACC)
		host_caps |= MMC_CAP_BOOT_NOACC;
	if (slot->quirks & SDHCI_QUIRK_WAIT_WHILE_BUSY)
		host_caps |= MMC_CAP_WAIT_WHILE_BUSY;

	/* Determine supported UHS-I and eMMC modes. */
	if (caps2 & (SDHCI_CAN_SDR50 | SDHCI_CAN_SDR104 | SDHCI_CAN_DDR50))
		host_caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25;
	if (caps2 & SDHCI_CAN_SDR104) {
		host_caps |= MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50;
		if (!(slot->quirks & SDHCI_QUIRK_BROKEN_MMC_HS200))
			host_caps |= MMC_CAP_MMC_HS200;
	} else if (caps2 & SDHCI_CAN_SDR50)
		host_caps |= MMC_CAP_UHS_SDR50;
	if (caps2 & SDHCI_CAN_DDR50 &&
	    !(slot->quirks & SDHCI_QUIRK_BROKEN_UHS_DDR50))
		host_caps |= MMC_CAP_UHS_DDR50;
	if (slot->quirks & SDHCI_QUIRK_MMC_DDR52)
		host_caps |= MMC_CAP_MMC_DDR52;
	if (slot->quirks & SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 &&
	    caps2 & SDHCI_CAN_MMC_HS400)
		host_caps |= MMC_CAP_MMC_HS400;
	if (slot->quirks & SDHCI_QUIRK_MMC_HS400_IF_CAN_SDR104 &&
	    caps2 & SDHCI_CAN_SDR104)
		host_caps |= MMC_CAP_MMC_HS400;

	/*
	 * Disable UHS-I and eMMC modes if the set_uhs_timing method is the
	 * default NULL implementation.
	 */
	kobj_desc = &sdhci_set_uhs_timing_desc;
	kobj_method = kobj_lookup_method(((kobj_t)dev)->ops->cls, NULL,
	    kobj_desc);
	if (kobj_method == &kobj_desc->deflt)
		host_caps &= ~(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
		    MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 | MMC_CAP_UHS_SDR104 |
		    MMC_CAP_MMC_DDR52 | MMC_CAP_MMC_HS200 | MMC_CAP_MMC_HS400);

#define	SDHCI_CAP_MODES_TUNING(caps2)					\
    (((caps2) & SDHCI_TUNE_SDR50 ? MMC_CAP_UHS_SDR50 : 0) |		\
    MMC_CAP_UHS_DDR50 | MMC_CAP_UHS_SDR104 | MMC_CAP_MMC_HS200 |	\
    MMC_CAP_MMC_HS400)

	/*
	 * Disable UHS-I and eMMC modes that require (re-)tuning if either
	 * the tune or re-tune method is the default NULL implementation.
	 */
	kobj_desc = &mmcbr_tune_desc;
	kobj_method = kobj_lookup_method(((kobj_t)dev)->ops->cls, NULL,
	    kobj_desc);
	if (kobj_method == &kobj_desc->deflt)
		goto no_tuning;
	kobj_desc = &mmcbr_retune_desc;
	kobj_method = kobj_lookup_method(((kobj_t)dev)->ops->cls, NULL,
	    kobj_desc);
	if (kobj_method == &kobj_desc->deflt) {
no_tuning:
		host_caps &= ~(SDHCI_CAP_MODES_TUNING(caps2));
	}

	/* Allocate tuning structures and determine tuning parameters. */
	if (host_caps & SDHCI_CAP_MODES_TUNING(caps2)) {
		slot->opt |= SDHCI_TUNING_SUPPORTED;
		slot->tune_req = malloc(sizeof(*slot->tune_req), M_DEVBUF,
		    M_WAITOK);
		slot->tune_cmd = malloc(sizeof(*slot->tune_cmd), M_DEVBUF,
		    M_WAITOK);
		slot->tune_data = malloc(sizeof(*slot->tune_data), M_DEVBUF,
		    M_WAITOK);
		if (caps2 & SDHCI_TUNE_SDR50)
			slot->opt |= SDHCI_SDR50_NEEDS_TUNING;
		slot->retune_mode = (caps2 & SDHCI_RETUNE_MODES_MASK) >>
		    SDHCI_RETUNE_MODES_SHIFT;
		if (slot->retune_mode == SDHCI_RETUNE_MODE_1) {
			slot->retune_count = (caps2 & SDHCI_RETUNE_CNT_MASK) >>
			    SDHCI_RETUNE_CNT_SHIFT;
			if (slot->retune_count > 0xb) {
				slot_printf(slot, "Unknown re-tuning count "
				    "%x, using 1 sec\n", slot->retune_count);
				slot->retune_count = 1;
			} else if (slot->retune_count != 0)
				slot->retune_count =
				    1 << (slot->retune_count - 1);
		}
	}

#undef SDHCI_CAP_MODES_TUNING

	/* Determine supported VCCQ signaling levels. */
	host_caps |= MMC_CAP_SIGNALING_330;
	if (host_caps & (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
	    MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 | MMC_CAP_UHS_SDR104 |
	    MMC_CAP_MMC_DDR52_180 | MMC_CAP_MMC_HS200_180 |
	    MMC_CAP_MMC_HS400_180))
		host_caps |= MMC_CAP_SIGNALING_120 | MMC_CAP_SIGNALING_180;

	/*
	 * Disable 1.2 V and 1.8 V signaling if the switch_vccq method is the
	 * default NULL implementation.  Disable 1.2 V support if it's the
	 * generic SDHCI implementation.
	 */
	kobj_desc = &mmcbr_switch_vccq_desc;
	kobj_method = kobj_lookup_method(((kobj_t)dev)->ops->cls, NULL,
	    kobj_desc);
	if (kobj_method == &kobj_desc->deflt)
		host_caps &= ~(MMC_CAP_SIGNALING_120 | MMC_CAP_SIGNALING_180);
	else if (kobj_method->func == (kobjop_t)sdhci_generic_switch_vccq)
		host_caps &= ~MMC_CAP_SIGNALING_120;

	/* Determine supported driver types (type B is always mandatory). */
	if (caps2 & SDHCI_CAN_DRIVE_TYPE_A)
		host_caps |= MMC_CAP_DRIVER_TYPE_A;
	if (caps2 & SDHCI_CAN_DRIVE_TYPE_C)
		host_caps |= MMC_CAP_DRIVER_TYPE_C;
	if (caps2 & SDHCI_CAN_DRIVE_TYPE_D)
		host_caps |= MMC_CAP_DRIVER_TYPE_D;
	slot->host.caps = host_caps;

	/* Decide if we have usable DMA. */
	if (caps & SDHCI_CAN_DO_DMA)
		slot->opt |= SDHCI_HAVE_DMA;

	if (slot->quirks & SDHCI_QUIRK_BROKEN_DMA)
		slot->opt &= ~SDHCI_HAVE_DMA;
	if (slot->quirks & SDHCI_QUIRK_FORCE_DMA)
		slot->opt |= SDHCI_HAVE_DMA;
	if (slot->quirks & SDHCI_QUIRK_ALL_SLOTS_NON_REMOVABLE)
		slot->opt |= SDHCI_NON_REMOVABLE;

	/*
	 * Use platform-provided transfer backend
	 * with PIO as a fallback mechanism
	 */
	if (slot->opt & SDHCI_PLATFORM_TRANSFER)
		slot->opt &= ~SDHCI_HAVE_DMA;

	if (slot->opt & SDHCI_HAVE_DMA) {
		err = sdhci_dma_alloc(slot);
		if (err != 0) {
			if (slot->opt & SDHCI_TUNING_SUPPORTED) {
				free(slot->tune_req, M_DEVBUF);
				free(slot->tune_cmd, M_DEVBUF);
				free(slot->tune_data, M_DEVBUF);
			}
			SDHCI_LOCK_DESTROY(slot);
			return (err);
		}
	}

	if (bootverbose || sdhci_debug) {
		slot_printf(slot,
		    "%uMHz%s %s VDD:%s%s%s VCCQ: 3.3V%s%s DRV: B%s%s%s %s %s\n",
		    slot->max_clk / 1000000,
		    (caps & SDHCI_CAN_DO_HISPD) ? " HS" : "",
		    (host_caps & MMC_CAP_8_BIT_DATA) ? "8bits" :
			((host_caps & MMC_CAP_4_BIT_DATA) ? "4bits" : "1bit"),
		    (caps & SDHCI_CAN_VDD_330) ? " 3.3V" : "",
		    (caps & SDHCI_CAN_VDD_300) ? " 3.0V" : "",
		    ((caps & SDHCI_CAN_VDD_180) &&
		    (slot->opt & SDHCI_SLOT_EMBEDDED)) ? " 1.8V" : "",
		    (host_caps & MMC_CAP_SIGNALING_180) ? " 1.8V" : "",
		    (host_caps & MMC_CAP_SIGNALING_120) ? " 1.2V" : "",
		    (host_caps & MMC_CAP_DRIVER_TYPE_A) ? "A" : "",
		    (host_caps & MMC_CAP_DRIVER_TYPE_C) ? "C" : "",
		    (host_caps & MMC_CAP_DRIVER_TYPE_D) ? "D" : "",
		    (slot->opt & SDHCI_HAVE_DMA) ? "DMA" : "PIO",
		    (slot->opt & SDHCI_SLOT_EMBEDDED) ? "embedded" :
		    (slot->opt & SDHCI_NON_REMOVABLE) ? "non-removable" :
		    "removable");
		if (host_caps & (MMC_CAP_MMC_DDR52 | MMC_CAP_MMC_HS200 |
		    MMC_CAP_MMC_HS400 | MMC_CAP_MMC_ENH_STROBE))
			slot_printf(slot, "eMMC:%s%s%s%s\n",
			    (host_caps & MMC_CAP_MMC_DDR52) ? " DDR52" : "",
			    (host_caps & MMC_CAP_MMC_HS200) ? " HS200" : "",
			    (host_caps & MMC_CAP_MMC_HS400) ? " HS400" : "",
			    ((host_caps &
			    (MMC_CAP_MMC_HS400 | MMC_CAP_MMC_ENH_STROBE)) ==
			    (MMC_CAP_MMC_HS400 | MMC_CAP_MMC_ENH_STROBE)) ?
			    " HS400ES" : "");
		if (host_caps & (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
		    MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104))
			slot_printf(slot, "UHS-I:%s%s%s%s%s\n",
			    (host_caps & MMC_CAP_UHS_SDR12) ? " SDR12" : "",
			    (host_caps & MMC_CAP_UHS_SDR25) ? " SDR25" : "",
			    (host_caps & MMC_CAP_UHS_SDR50) ? " SDR50" : "",
			    (host_caps & MMC_CAP_UHS_SDR104) ? " SDR104" : "",
			    (host_caps & MMC_CAP_UHS_DDR50) ? " DDR50" : "");
		if (slot->opt & SDHCI_TUNING_SUPPORTED)
			slot_printf(slot, "Re-tuning count %d secs, mode %d\n",
			    slot->retune_count, slot->retune_mode + 1);
		sdhci_dumpregs(slot);
	}

	slot->timeout = 10;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(slot->bus),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(slot->bus)), OID_AUTO,
	    "timeout", CTLFLAG_RW, &slot->timeout, 0,
	    "Maximum timeout for SDHCI transfers (in secs)");
	TASK_INIT(&slot->card_task, 0, sdhci_card_task, slot);
	TIMEOUT_TASK_INIT(taskqueue_swi_giant, &slot->card_delayed_task, 0,
		sdhci_card_task, slot);
	callout_init(&slot->card_poll_callout, 1);
	callout_init_mtx(&slot->timeout_callout, &slot->mtx, 0);
	callout_init_mtx(&slot->retune_callout, &slot->mtx, 0);

	if ((slot->quirks & SDHCI_QUIRK_POLL_CARD_PRESENT) &&
	    !(slot->opt & SDHCI_NON_REMOVABLE)) {
		callout_reset(&slot->card_poll_callout,
		    SDHCI_CARD_PRESENT_TICKS, sdhci_card_poll, slot);
	}

	sdhci_init(slot);

	return (0);
}

#ifndef MMCCAM
void
sdhci_start_slot(struct sdhci_slot *slot)
{

	sdhci_card_task(slot, 0);
}
#endif

int
sdhci_cleanup_slot(struct sdhci_slot *slot)
{
	device_t d;

	callout_drain(&slot->timeout_callout);
	callout_drain(&slot->card_poll_callout);
	callout_drain(&slot->retune_callout);
	taskqueue_drain(taskqueue_swi_giant, &slot->card_task);
	taskqueue_drain_timeout(taskqueue_swi_giant, &slot->card_delayed_task);

	SDHCI_LOCK(slot);
	d = slot->dev;
	slot->dev = NULL;
	SDHCI_UNLOCK(slot);
	if (d != NULL)
		device_delete_child(slot->bus, d);

	SDHCI_LOCK(slot);
	sdhci_reset(slot, SDHCI_RESET_ALL);
	SDHCI_UNLOCK(slot);
	if (slot->opt & SDHCI_HAVE_DMA)
		sdhci_dma_free(slot);
	if (slot->opt & SDHCI_TUNING_SUPPORTED) {
		free(slot->tune_req, M_DEVBUF);
		free(slot->tune_cmd, M_DEVBUF);
		free(slot->tune_data, M_DEVBUF);
	}

	SDHCI_LOCK_DESTROY(slot);

	return (0);
}

int
sdhci_generic_suspend(struct sdhci_slot *slot)
{

	/*
	 * We expect the MMC layer to issue initial tuning after resume.
	 * Otherwise, we'd need to indicate re-tuning including circuit reset
	 * being required at least for re-tuning modes 1 and 2 ourselves.
	 */
	callout_drain(&slot->retune_callout);
	SDHCI_LOCK(slot);
	slot->opt &= ~SDHCI_TUNING_ENABLED;
	sdhci_reset(slot, SDHCI_RESET_ALL);
	SDHCI_UNLOCK(slot);

	return (0);
}

int
sdhci_generic_resume(struct sdhci_slot *slot)
{

	SDHCI_LOCK(slot);
	sdhci_init(slot);
	SDHCI_UNLOCK(slot);

	return (0);
}

uint32_t
sdhci_generic_min_freq(device_t brdev __unused, struct sdhci_slot *slot)
{

	if (slot->version >= SDHCI_SPEC_300)
		return (slot->max_clk / SDHCI_300_MAX_DIVIDER);
	else
		return (slot->max_clk / SDHCI_200_MAX_DIVIDER);
}

bool
sdhci_generic_get_card_present(device_t brdev __unused, struct sdhci_slot *slot)
{

	if (slot->opt & SDHCI_NON_REMOVABLE)
		return true;

	return (RD4(slot, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
}

void
sdhci_generic_set_uhs_timing(device_t brdev __unused, struct sdhci_slot *slot)
{
	const struct mmc_ios *ios;
	uint16_t hostctrl2;

	if (slot->version < SDHCI_SPEC_300)
		return;

	SDHCI_ASSERT_LOCKED(slot);
	ios = &slot->host.ios;
	sdhci_set_clock(slot, 0);
	hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
	hostctrl2 &= ~SDHCI_CTRL2_UHS_MASK;
	if (ios->clock > SD_SDR50_MAX) {
		if (ios->timing == bus_timing_mmc_hs400 ||
		    ios->timing == bus_timing_mmc_hs400es)
			hostctrl2 |= SDHCI_CTRL2_MMC_HS400;
		else
			hostctrl2 |= SDHCI_CTRL2_UHS_SDR104;
	}
	else if (ios->clock > SD_SDR25_MAX)
		hostctrl2 |= SDHCI_CTRL2_UHS_SDR50;
	else if (ios->clock > SD_SDR12_MAX) {
		if (ios->timing == bus_timing_uhs_ddr50 ||
		    ios->timing == bus_timing_mmc_ddr52)
			hostctrl2 |= SDHCI_CTRL2_UHS_DDR50;
		else
			hostctrl2 |= SDHCI_CTRL2_UHS_SDR25;
	} else if (ios->clock > SD_MMC_CARD_ID_FREQUENCY)
		hostctrl2 |= SDHCI_CTRL2_UHS_SDR12;
	WR2(slot, SDHCI_HOST_CONTROL2, hostctrl2);
	sdhci_set_clock(slot, ios->clock);
}

int
sdhci_generic_update_ios(device_t brdev, device_t reqdev)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	struct mmc_ios *ios = &slot->host.ios;

	SDHCI_LOCK(slot);
	/* Do full reset on bus power down to clear from any state. */
	if (ios->power_mode == power_off) {
		WR4(slot, SDHCI_SIGNAL_ENABLE, 0);
		sdhci_init(slot);
	}
	/* Configure the bus. */
	sdhci_set_clock(slot, ios->clock);
	sdhci_set_power(slot, (ios->power_mode == power_off) ? 0 : ios->vdd);
	if (ios->bus_width == bus_width_8) {
		slot->hostctrl |= SDHCI_CTRL_8BITBUS;
		slot->hostctrl &= ~SDHCI_CTRL_4BITBUS;
	} else if (ios->bus_width == bus_width_4) {
		slot->hostctrl &= ~SDHCI_CTRL_8BITBUS;
		slot->hostctrl |= SDHCI_CTRL_4BITBUS;
	} else if (ios->bus_width == bus_width_1) {
		slot->hostctrl &= ~SDHCI_CTRL_8BITBUS;
		slot->hostctrl &= ~SDHCI_CTRL_4BITBUS;
	} else {
		panic("Invalid bus width: %d", ios->bus_width);
	}
	if (ios->clock > SD_SDR12_MAX &&
	    !(slot->quirks & SDHCI_QUIRK_DONT_SET_HISPD_BIT))
		slot->hostctrl |= SDHCI_CTRL_HISPD;
	else
		slot->hostctrl &= ~SDHCI_CTRL_HISPD;
	WR1(slot, SDHCI_HOST_CONTROL, slot->hostctrl);
	SDHCI_SET_UHS_TIMING(brdev, slot);
	/* Some controllers like reset after bus changes. */
	if (slot->quirks & SDHCI_QUIRK_RESET_ON_IOS)
		sdhci_reset(slot, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	SDHCI_UNLOCK(slot);
	return (0);
}

int
sdhci_generic_switch_vccq(device_t brdev __unused, device_t reqdev)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	enum mmc_vccq vccq;
	int err;
	uint16_t hostctrl2;

	if (slot->version < SDHCI_SPEC_300)
		return (0);

	err = 0;
	vccq = slot->host.ios.vccq;
	SDHCI_LOCK(slot);
	sdhci_set_clock(slot, 0);
	hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
	switch (vccq) {
	case vccq_330:
		if (!(hostctrl2 & SDHCI_CTRL2_S18_ENABLE))
			goto done;
		hostctrl2 &= ~SDHCI_CTRL2_S18_ENABLE;
		WR2(slot, SDHCI_HOST_CONTROL2, hostctrl2);
		DELAY(5000);
		hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
		if (!(hostctrl2 & SDHCI_CTRL2_S18_ENABLE))
			goto done;
		err = EAGAIN;
		break;
	case vccq_180:
		if (!(slot->host.caps & MMC_CAP_SIGNALING_180)) {
			err = EINVAL;
			goto done;
		}
		if (hostctrl2 & SDHCI_CTRL2_S18_ENABLE)
			goto done;
		hostctrl2 |= SDHCI_CTRL2_S18_ENABLE;
		WR2(slot, SDHCI_HOST_CONTROL2, hostctrl2);
		DELAY(5000);
		hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
		if (hostctrl2 & SDHCI_CTRL2_S18_ENABLE)
			goto done;
		err = EAGAIN;
		break;
	default:
		slot_printf(slot,
		    "Attempt to set unsupported signaling voltage\n");
		err = EINVAL;
		break;
	}
done:
	sdhci_set_clock(slot, slot->host.ios.clock);
	SDHCI_UNLOCK(slot);
	return (err);
}

int
sdhci_generic_tune(device_t brdev __unused, device_t reqdev, bool hs400)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	const struct mmc_ios *ios = &slot->host.ios;
	struct mmc_command *tune_cmd;
	struct mmc_data *tune_data;
	uint32_t opcode;
	int err;

	if (!(slot->opt & SDHCI_TUNING_SUPPORTED))
		return (0);

	slot->retune_ticks = slot->retune_count * hz;
	opcode = MMC_SEND_TUNING_BLOCK;
	SDHCI_LOCK(slot);
	switch (ios->timing) {
	case bus_timing_mmc_hs400:
		slot_printf(slot, "HS400 must be tuned in HS200 mode\n");
		SDHCI_UNLOCK(slot);
		return (EINVAL);
	case bus_timing_mmc_hs200:
		/*
		 * In HS400 mode, controllers use the data strobe line to
		 * latch data from the devices so periodic re-tuning isn't
		 * expected to be required.
		 */
		if (hs400)
			slot->retune_ticks = 0;
		opcode = MMC_SEND_TUNING_BLOCK_HS200;
		break;
	case bus_timing_uhs_ddr50:
	case bus_timing_uhs_sdr104:
		break;
	case bus_timing_uhs_sdr50:
		if (slot->opt & SDHCI_SDR50_NEEDS_TUNING)
			break;
		/* FALLTHROUGH */
	default:
		SDHCI_UNLOCK(slot);
		return (0);
	}

	tune_cmd = slot->tune_cmd;
	memset(tune_cmd, 0, sizeof(*tune_cmd));
	tune_cmd->opcode = opcode;
	tune_cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	tune_data = tune_cmd->data = slot->tune_data;
	memset(tune_data, 0, sizeof(*tune_data));
	tune_data->len = (opcode == MMC_SEND_TUNING_BLOCK_HS200 &&
	    ios->bus_width == bus_width_8) ? MMC_TUNING_LEN_HS200 :
	    MMC_TUNING_LEN;
	tune_data->flags = MMC_DATA_READ;
	tune_data->mrq = tune_cmd->mrq = slot->tune_req;

	slot->opt &= ~SDHCI_TUNING_ENABLED;
	err = sdhci_exec_tuning(slot, true);
	if (err == 0) {
		slot->opt |= SDHCI_TUNING_ENABLED;
		slot->intmask |= sdhci_tuning_intmask(slot);
		WR4(slot, SDHCI_INT_ENABLE, slot->intmask);
		WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
		if (slot->retune_ticks) {
			callout_reset(&slot->retune_callout, slot->retune_ticks,
			    sdhci_retune, slot);
		}
	}
	SDHCI_UNLOCK(slot);
	return (err);
}

int
sdhci_generic_retune(device_t brdev __unused, device_t reqdev, bool reset)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	int err;

	if (!(slot->opt & SDHCI_TUNING_ENABLED))
		return (0);

	/* HS400 must be tuned in HS200 mode. */
	if (slot->host.ios.timing == bus_timing_mmc_hs400)
		return (EINVAL);

	SDHCI_LOCK(slot);
	err = sdhci_exec_tuning(slot, reset);
	/*
	 * There are two ways sdhci_exec_tuning() can fail:
	 * EBUSY should not actually happen when requests are only issued
	 *	 with the host properly acquired, and
	 * EIO   re-tuning failed (but it did work initially).
	 *
	 * In both cases, we should retry at later point if periodic re-tuning
	 * is enabled.  Note that due to slot->retune_req not being cleared in
	 * these failure cases, the MMC layer should trigger another attempt at
	 * re-tuning with the next request anyway, though.
	 */
	if (slot->retune_ticks) {
		callout_reset(&slot->retune_callout, slot->retune_ticks,
		    sdhci_retune, slot);
	}
	SDHCI_UNLOCK(slot);
	return (err);
}

static int
sdhci_exec_tuning(struct sdhci_slot *slot, bool reset)
{
	struct mmc_request *tune_req;
	struct mmc_command *tune_cmd;
	int i;
	uint32_t intmask;
	uint16_t hostctrl2;
	u_char opt;

	SDHCI_ASSERT_LOCKED(slot);
	if (slot->req != NULL)
		return (EBUSY);

	/* Tuning doesn't work with DMA enabled. */
	opt = slot->opt;
	slot->opt = opt & ~SDHCI_HAVE_DMA;

	/*
	 * Ensure that as documented, SDHCI_INT_DATA_AVAIL is the only
	 * kind of interrupt we receive in response to a tuning request.
	 */
	intmask = slot->intmask;
	slot->intmask = SDHCI_INT_DATA_AVAIL;
	WR4(slot, SDHCI_INT_ENABLE, SDHCI_INT_DATA_AVAIL);
	WR4(slot, SDHCI_SIGNAL_ENABLE, SDHCI_INT_DATA_AVAIL);

	hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
	if (reset)
		hostctrl2 &= ~SDHCI_CTRL2_SAMPLING_CLOCK;
	else
		hostctrl2 |= SDHCI_CTRL2_SAMPLING_CLOCK;
	WR2(slot, SDHCI_HOST_CONTROL2, hostctrl2 | SDHCI_CTRL2_EXEC_TUNING);

	tune_req = slot->tune_req;
	tune_cmd = slot->tune_cmd;
	for (i = 0; i < MMC_TUNING_MAX; i++) {
		memset(tune_req, 0, sizeof(*tune_req));
		tune_req->cmd = tune_cmd;
		tune_req->done = sdhci_req_wakeup;
		tune_req->done_data = slot;
		slot->req = tune_req;
		slot->flags = 0;
		sdhci_start(slot);
		while (!(tune_req->flags & MMC_REQ_DONE))
			msleep(tune_req, &slot->mtx, 0, "sdhciet", 0);
		if (!(tune_req->flags & MMC_TUNE_DONE))
			break;
		hostctrl2 = RD2(slot, SDHCI_HOST_CONTROL2);
		if (!(hostctrl2 & SDHCI_CTRL2_EXEC_TUNING))
			break;
		if (tune_cmd->opcode == MMC_SEND_TUNING_BLOCK)
			DELAY(1000);
	}

	/*
	 * Restore DMA usage and interrupts.
	 * Note that the interrupt aggregation code might have cleared
	 * SDHCI_INT_DMA_END and/or SDHCI_INT_RESPONSE in slot->intmask
	 * and SDHCI_SIGNAL_ENABLE respectively so ensure SDHCI_INT_ENABLE
	 * doesn't lose these.
	 */
	slot->opt = opt;
	slot->intmask = intmask;
	WR4(slot, SDHCI_INT_ENABLE, intmask | SDHCI_INT_DMA_END |
	    SDHCI_INT_RESPONSE);
	WR4(slot, SDHCI_SIGNAL_ENABLE, intmask);

	if ((hostctrl2 & (SDHCI_CTRL2_EXEC_TUNING |
	    SDHCI_CTRL2_SAMPLING_CLOCK)) == SDHCI_CTRL2_SAMPLING_CLOCK) {
		slot->retune_req = 0;
		return (0);
	}

	slot_printf(slot, "Tuning failed, using fixed sampling clock\n");
	WR2(slot, SDHCI_HOST_CONTROL2, hostctrl2 & ~(SDHCI_CTRL2_EXEC_TUNING |
	    SDHCI_CTRL2_SAMPLING_CLOCK));
	sdhci_reset(slot, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	return (EIO);
}

static void
sdhci_retune(void *arg)
{
	struct sdhci_slot *slot = arg;

	slot->retune_req |= SDHCI_RETUNE_REQ_NEEDED;
}

#ifdef MMCCAM
static void
sdhci_req_done(struct sdhci_slot *slot)
{
	union ccb *ccb;

	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "%s\n", __func__);
	if (slot->ccb != NULL && slot->curcmd != NULL) {
		callout_stop(&slot->timeout_callout);
		ccb = slot->ccb;
		slot->ccb = NULL;
		slot->curcmd = NULL;

		/* Tell CAM the request is finished */
		struct ccb_mmcio *mmcio;
		mmcio = &ccb->mmcio;

		ccb->ccb_h.status =
		    (mmcio->cmd.error == 0 ? CAM_REQ_CMP : CAM_REQ_CMP_ERR);
		xpt_done(ccb);
	}
}
#else
static void
sdhci_req_done(struct sdhci_slot *slot)
{
	struct mmc_request *req;

	if (slot->req != NULL && slot->curcmd != NULL) {
		callout_stop(&slot->timeout_callout);
		req = slot->req;
		slot->req = NULL;
		slot->curcmd = NULL;
		req->done(req);
	}
}
#endif

static void
sdhci_req_wakeup(struct mmc_request *req)
{
	struct sdhci_slot *slot;

	slot = req->done_data;
	req->flags |= MMC_REQ_DONE;
	wakeup(req);
}

static void
sdhci_timeout(void *arg)
{
	struct sdhci_slot *slot = arg;

	if (slot->curcmd != NULL) {
		slot_printf(slot, "Controller timeout\n");
		sdhci_dumpregs(slot);
		sdhci_reset(slot, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		slot->curcmd->error = MMC_ERR_TIMEOUT;
		sdhci_req_done(slot);
	} else {
		slot_printf(slot, "Spurious timeout - no active command\n");
	}
}

static void
sdhci_set_transfer_mode(struct sdhci_slot *slot, const struct mmc_data *data)
{
	uint16_t mode;

	if (data == NULL)
		return;

	mode = SDHCI_TRNS_BLK_CNT_EN;
	if (data->len > 512) {
		mode |= SDHCI_TRNS_MULTI;
		if (__predict_true(
#ifdef MMCCAM
		    slot->ccb->mmcio.stop.opcode == MMC_STOP_TRANSMISSION &&
#else
		    slot->req->stop != NULL &&
#endif
		    !(slot->quirks & SDHCI_QUIRK_BROKEN_AUTO_STOP)))
			mode |= SDHCI_TRNS_ACMD12;
	}
	if (data->flags & MMC_DATA_READ)
		mode |= SDHCI_TRNS_READ;
	if (slot->flags & SDHCI_USE_DMA)
		mode |= SDHCI_TRNS_DMA;

	WR2(slot, SDHCI_TRANSFER_MODE, mode);
}

static void
sdhci_start_command(struct sdhci_slot *slot, struct mmc_command *cmd)
{
	int flags, timeout;
	uint32_t mask;

	slot->curcmd = cmd;
	slot->cmd_done = 0;

	cmd->error = MMC_ERR_NONE;

	/* This flags combination is not supported by controller. */
	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		slot_printf(slot, "Unsupported response type!\n");
		cmd->error = MMC_ERR_FAILED;
		sdhci_req_done(slot);
		return;
	}

	/*
	 * Do not issue command if there is no card, clock or power.
	 * Controller will not detect timeout without clock active.
	 */
	if (!SDHCI_GET_CARD_PRESENT(slot->bus, slot) ||
	    slot->power == 0 ||
	    slot->clock == 0) {
		slot_printf(slot,
			    "Cannot issue a command (power=%d clock=%d)",
			    slot->power, slot->clock);
		cmd->error = MMC_ERR_FAILED;
		sdhci_req_done(slot);
		return;
	}
	/* Always wait for free CMD bus. */
	mask = SDHCI_CMD_INHIBIT;
	/* Wait for free DAT if we have data or busy signal. */
	if (cmd->data != NULL || (cmd->flags & MMC_RSP_BUSY))
		mask |= SDHCI_DAT_INHIBIT;
	/*
	 * We shouldn't wait for DAT for stop commands or CMD19/CMD21.  Note
	 * that these latter are also special in that SDHCI_CMD_DATA should
	 * be set below but no actual data is ever read from the controller.
	*/
#ifdef MMCCAM
	if (cmd == &slot->ccb->mmcio.stop ||
#else
	if (cmd == slot->req->stop ||
#endif
	    __predict_false(cmd->opcode == MMC_SEND_TUNING_BLOCK ||
	    cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200))
		mask &= ~SDHCI_DAT_INHIBIT;
	/*
	 *  Wait for bus no more then 250 ms.  Typically there will be no wait
	 *  here at all, but when writing a crash dump we may be bypassing the
	 *  host platform's interrupt handler, and in some cases that handler
	 *  may be working around hardware quirks such as not respecting r1b
	 *  busy indications.  In those cases, this wait-loop serves the purpose
	 *  of waiting for the prior command and data transfers to be done, and
	 *  SD cards are allowed to take up to 250ms for write and erase ops.
	 *  (It's usually more like 20-30ms in the real world.)
	 */
	timeout = 250;
	while (mask & RD4(slot, SDHCI_PRESENT_STATE)) {
		if (timeout == 0) {
			slot_printf(slot, "Controller never released "
			    "inhibit bit(s).\n");
			sdhci_dumpregs(slot);
			cmd->error = MMC_ERR_FAILED;
			sdhci_req_done(slot);
			return;
		}
		timeout--;
		DELAY(1000);
	}

	/* Prepare command flags. */
	if (!(cmd->flags & MMC_RSP_PRESENT))
		flags = SDHCI_CMD_RESP_NONE;
	else if (cmd->flags & MMC_RSP_136)
		flags = SDHCI_CMD_RESP_LONG;
	else if (cmd->flags & MMC_RSP_BUSY)
		flags = SDHCI_CMD_RESP_SHORT_BUSY;
	else
		flags = SDHCI_CMD_RESP_SHORT;
	if (cmd->flags & MMC_RSP_CRC)
		flags |= SDHCI_CMD_CRC;
	if (cmd->flags & MMC_RSP_OPCODE)
		flags |= SDHCI_CMD_INDEX;
	if (cmd->data != NULL)
		flags |= SDHCI_CMD_DATA;
	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		flags |= SDHCI_CMD_TYPE_ABORT;
	/* Prepare data. */
	sdhci_start_data(slot, cmd->data);
	/*
	 * Interrupt aggregation: To reduce total number of interrupts
	 * group response interrupt with data interrupt when possible.
	 * If there going to be data interrupt, mask response one.
	 */
	if (slot->data_done == 0) {
		WR4(slot, SDHCI_SIGNAL_ENABLE,
		    slot->intmask &= ~SDHCI_INT_RESPONSE);
	}
	/* Set command argument. */
	WR4(slot, SDHCI_ARGUMENT, cmd->arg);
	/* Set data transfer mode. */
	sdhci_set_transfer_mode(slot, cmd->data);
	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "Starting command!\n");
	/* Start command. */
	WR2(slot, SDHCI_COMMAND_FLAGS, (cmd->opcode << 8) | (flags & 0xff));
	/* Start timeout callout. */
	callout_reset(&slot->timeout_callout, slot->timeout * hz,
	    sdhci_timeout, slot);
}

static void
sdhci_finish_command(struct sdhci_slot *slot)
{
	int i;
	uint32_t val;
	uint8_t extra;

	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "%s: called, err %d flags %d\n",
		    __func__, slot->curcmd->error, slot->curcmd->flags);
	slot->cmd_done = 1;
	/*
	 * Interrupt aggregation: Restore command interrupt.
	 * Main restore point for the case when command interrupt
	 * happened first.
	 */
	if (__predict_true(slot->curcmd->opcode != MMC_SEND_TUNING_BLOCK &&
	    slot->curcmd->opcode != MMC_SEND_TUNING_BLOCK_HS200))
		WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask |=
		    SDHCI_INT_RESPONSE);
	/* In case of error - reset host and return. */
	if (slot->curcmd->error) {
		if (slot->curcmd->error == MMC_ERR_BADCRC)
			slot->retune_req |= SDHCI_RETUNE_REQ_RESET;
		sdhci_reset(slot, SDHCI_RESET_CMD);
		sdhci_reset(slot, SDHCI_RESET_DATA);
		sdhci_start(slot);
		return;
	}
	/* If command has response - fetch it. */
	if (slot->curcmd->flags & MMC_RSP_PRESENT) {
		if (slot->curcmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need one byte shift. */
			extra = 0;
			for (i = 0; i < 4; i++) {
				val = RD4(slot, SDHCI_RESPONSE + i * 4);
				if (slot->quirks &
				    SDHCI_QUIRK_DONT_SHIFT_RESPONSE)
					slot->curcmd->resp[3 - i] = val;
				else {
					slot->curcmd->resp[3 - i] =
					    (val << 8) | extra;
					extra = val >> 24;
				}
			}
		} else
			slot->curcmd->resp[0] = RD4(slot, SDHCI_RESPONSE);
	}
	if (__predict_false(sdhci_debug > 1))
		printf("Resp: %02x %02x %02x %02x\n",
		    slot->curcmd->resp[0], slot->curcmd->resp[1],
		    slot->curcmd->resp[2], slot->curcmd->resp[3]);

	/* If data ready - finish. */
	if (slot->data_done)
		sdhci_start(slot);
}

static void
sdhci_start_data(struct sdhci_slot *slot, const struct mmc_data *data)
{
	uint32_t blkcnt, blksz, current_timeout, sdma_bbufsz, target_timeout;
	uint8_t div;

	if (data == NULL && (slot->curcmd->flags & MMC_RSP_BUSY) == 0) {
		slot->data_done = 1;
		return;
	}

	slot->data_done = 0;

	/* Calculate and set data timeout.*/
	/* XXX: We should have this from mmc layer, now assume 1 sec. */
	if (slot->quirks & SDHCI_QUIRK_BROKEN_TIMEOUT_VAL) {
		div = 0xE;
	} else {
		target_timeout = 1000000;
		div = 0;
		current_timeout = (1 << 13) * 1000 / slot->timeout_clk;
		while (current_timeout < target_timeout && div < 0xE) {
			++div;
			current_timeout <<= 1;
		}
		/* Compensate for an off-by-one error in the CaFe chip.*/
		if (div < 0xE &&
		    (slot->quirks & SDHCI_QUIRK_INCR_TIMEOUT_CONTROL)) {
			++div;
		}
	}
	WR1(slot, SDHCI_TIMEOUT_CONTROL, div);

	if (data == NULL)
		return;

	/* Use DMA if possible. */
	if ((slot->opt & SDHCI_HAVE_DMA))
		slot->flags |= SDHCI_USE_DMA;
	/* If data is small, broken DMA may return zeroes instead of data. */
	if ((slot->quirks & SDHCI_QUIRK_BROKEN_TIMINGS) &&
	    (data->len <= 512))
		slot->flags &= ~SDHCI_USE_DMA;
	/* Some controllers require even block sizes. */
	if ((slot->quirks & SDHCI_QUIRK_32BIT_DMA_SIZE) &&
	    ((data->len) & 0x3))
		slot->flags &= ~SDHCI_USE_DMA;
	/* Load DMA buffer. */
	if (slot->flags & SDHCI_USE_DMA) {
		sdma_bbufsz = slot->sdma_bbufsz;
		if (data->flags & MMC_DATA_READ)
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_PREREAD);
		else {
			memcpy(slot->dmamem, data->data, ulmin(data->len,
			    sdma_bbufsz));
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_PREWRITE);
		}
		WR4(slot, SDHCI_DMA_ADDRESS, slot->paddr);
		/*
		 * Interrupt aggregation: Mask border interrupt for the last
		 * bounce buffer and unmask otherwise.
		 */
		if (data->len == sdma_bbufsz)
			slot->intmask &= ~SDHCI_INT_DMA_END;
		else
			slot->intmask |= SDHCI_INT_DMA_END;
		WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
	}
	/* Current data offset for both PIO and DMA. */
	slot->offset = 0;
	/* Set block size and request border interrupts on the SDMA boundary. */
	blksz = SDHCI_MAKE_BLKSZ(slot->sdma_boundary, ulmin(data->len, 512));
	WR2(slot, SDHCI_BLOCK_SIZE, blksz);
	/* Set block count. */
	blkcnt = howmany(data->len, 512);
	WR2(slot, SDHCI_BLOCK_COUNT, blkcnt);
	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "Blk size: 0x%08x | Blk cnt:  0x%08x\n",
		    blksz, blkcnt);
}

void
sdhci_finish_data(struct sdhci_slot *slot)
{
	struct mmc_data *data = slot->curcmd->data;
	size_t left;

	/* Interrupt aggregation: Restore command interrupt.
	 * Auxiliary restore point for the case when data interrupt
	 * happened first. */
	if (!slot->cmd_done) {
		WR4(slot, SDHCI_SIGNAL_ENABLE,
		    slot->intmask |= SDHCI_INT_RESPONSE);
	}
	/* Unload rest of data from DMA buffer. */
	if (!slot->data_done && (slot->flags & SDHCI_USE_DMA) &&
	    slot->curcmd->data != NULL) {
		if (data->flags & MMC_DATA_READ) {
			left = data->len - slot->offset;
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_POSTREAD);
			memcpy((u_char*)data->data + slot->offset, slot->dmamem,
			    ulmin(left, slot->sdma_bbufsz));
		} else
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_POSTWRITE);
	}
	slot->data_done = 1;
	/* If there was error - reset the host. */
	if (slot->curcmd->error) {
		if (slot->curcmd->error == MMC_ERR_BADCRC)
			slot->retune_req |= SDHCI_RETUNE_REQ_RESET;
		sdhci_reset(slot, SDHCI_RESET_CMD);
		sdhci_reset(slot, SDHCI_RESET_DATA);
		sdhci_start(slot);
		return;
	}
	/* If we already have command response - finish. */
	if (slot->cmd_done)
		sdhci_start(slot);
}

#ifdef MMCCAM
static void
sdhci_start(struct sdhci_slot *slot)
{
	union ccb *ccb;
	struct ccb_mmcio *mmcio;

	ccb = slot->ccb;
	if (ccb == NULL)
		return;

	mmcio = &ccb->mmcio;
	if (!(slot->flags & CMD_STARTED)) {
		slot->flags |= CMD_STARTED;
		sdhci_start_command(slot, &mmcio->cmd);
		return;
	}

	/*
	 * Old stack doesn't use this!
	 * Enabling this code causes significant performance degradation
	 * and IRQ storms on BBB, Wandboard behaves fine.
	 * Not using this code does no harm...
	if (!(slot->flags & STOP_STARTED) && mmcio->stop.opcode != 0) {
		slot->flags |= STOP_STARTED;
		sdhci_start_command(slot, &mmcio->stop);
		return;
	}
	*/
	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "result: %d\n", mmcio->cmd.error);
	if (mmcio->cmd.error == 0 &&
	    (slot->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST)) {
		sdhci_reset(slot, SDHCI_RESET_CMD);
		sdhci_reset(slot, SDHCI_RESET_DATA);
	}

	sdhci_req_done(slot);
}
#else
static void
sdhci_start(struct sdhci_slot *slot)
{
	const struct mmc_request *req;

	req = slot->req;
	if (req == NULL)
		return;

	if (!(slot->flags & CMD_STARTED)) {
		slot->flags |= CMD_STARTED;
		sdhci_start_command(slot, req->cmd);
		return;
	}
	if ((slot->quirks & SDHCI_QUIRK_BROKEN_AUTO_STOP) &&
	    !(slot->flags & STOP_STARTED) && req->stop) {
		slot->flags |= STOP_STARTED;
		sdhci_start_command(slot, req->stop);
		return;
	}
	if (__predict_false(sdhci_debug > 1))
		slot_printf(slot, "result: %d\n", req->cmd->error);
	if (!req->cmd->error &&
	    ((slot->curcmd == req->stop &&
	     (slot->quirks & SDHCI_QUIRK_BROKEN_AUTO_STOP)) ||
	     (slot->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST))) {
		sdhci_reset(slot, SDHCI_RESET_CMD);
		sdhci_reset(slot, SDHCI_RESET_DATA);
	}

	sdhci_req_done(slot);
}
#endif

int
sdhci_generic_request(device_t brdev __unused, device_t reqdev,
    struct mmc_request *req)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);

	SDHCI_LOCK(slot);
	if (slot->req != NULL) {
		SDHCI_UNLOCK(slot);
		return (EBUSY);
	}
	if (__predict_false(sdhci_debug > 1)) {
		slot_printf(slot,
		    "CMD%u arg %#x flags %#x dlen %u dflags %#x\n",
		    req->cmd->opcode, req->cmd->arg, req->cmd->flags,
		    (req->cmd->data)?(u_int)req->cmd->data->len:0,
		    (req->cmd->data)?req->cmd->data->flags:0);
	}
	slot->req = req;
	slot->flags = 0;
	sdhci_start(slot);
	SDHCI_UNLOCK(slot);
	if (dumping) {
		while (slot->req != NULL) {
			sdhci_generic_intr(slot);
			DELAY(10);
		}
	}
	return (0);
}

int
sdhci_generic_get_ro(device_t brdev __unused, device_t reqdev)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	uint32_t val;

	SDHCI_LOCK(slot);
	val = RD4(slot, SDHCI_PRESENT_STATE);
	SDHCI_UNLOCK(slot);
	return (!(val & SDHCI_WRITE_PROTECT));
}

int
sdhci_generic_acquire_host(device_t brdev __unused, device_t reqdev)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);
	int err = 0;

	SDHCI_LOCK(slot);
	while (slot->bus_busy)
		msleep(slot, &slot->mtx, 0, "sdhciah", 0);
	slot->bus_busy++;
	/* Activate led. */
	WR1(slot, SDHCI_HOST_CONTROL, slot->hostctrl |= SDHCI_CTRL_LED);
	SDHCI_UNLOCK(slot);
	return (err);
}

int
sdhci_generic_release_host(device_t brdev __unused, device_t reqdev)
{
	struct sdhci_slot *slot = device_get_ivars(reqdev);

	SDHCI_LOCK(slot);
	/* Deactivate led. */
	WR1(slot, SDHCI_HOST_CONTROL, slot->hostctrl &= ~SDHCI_CTRL_LED);
	slot->bus_busy--;
	SDHCI_UNLOCK(slot);
	wakeup(slot);
	return (0);
}

static void
sdhci_cmd_irq(struct sdhci_slot *slot, uint32_t intmask)
{

	if (!slot->curcmd) {
		slot_printf(slot, "Got command interrupt 0x%08x, but "
		    "there is no active command.\n", intmask);
		sdhci_dumpregs(slot);
		return;
	}
	if (intmask & SDHCI_INT_TIMEOUT)
		slot->curcmd->error = MMC_ERR_TIMEOUT;
	else if (intmask & SDHCI_INT_CRC)
		slot->curcmd->error = MMC_ERR_BADCRC;
	else if (intmask & (SDHCI_INT_END_BIT | SDHCI_INT_INDEX))
		slot->curcmd->error = MMC_ERR_FIFO;

	sdhci_finish_command(slot);
}

static void
sdhci_data_irq(struct sdhci_slot *slot, uint32_t intmask)
{
	struct mmc_data *data;
	size_t left;
	uint32_t sdma_bbufsz;

	if (!slot->curcmd) {
		slot_printf(slot, "Got data interrupt 0x%08x, but "
		    "there is no active command.\n", intmask);
		sdhci_dumpregs(slot);
		return;
	}
	if (slot->curcmd->data == NULL &&
	    (slot->curcmd->flags & MMC_RSP_BUSY) == 0) {
		slot_printf(slot, "Got data interrupt 0x%08x, but "
		    "there is no active data operation.\n",
		    intmask);
		sdhci_dumpregs(slot);
		return;
	}
	if (intmask & SDHCI_INT_DATA_TIMEOUT)
		slot->curcmd->error = MMC_ERR_TIMEOUT;
	else if (intmask & (SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_END_BIT))
		slot->curcmd->error = MMC_ERR_BADCRC;
	if (slot->curcmd->data == NULL &&
	    (intmask & (SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL |
	    SDHCI_INT_DMA_END))) {
		slot_printf(slot, "Got data interrupt 0x%08x, but "
		    "there is busy-only command.\n", intmask);
		sdhci_dumpregs(slot);
		slot->curcmd->error = MMC_ERR_INVALID;
	}
	if (slot->curcmd->error) {
		/* No need to continue after any error. */
		goto done;
	}

	/* Handle tuning completion interrupt. */
	if (__predict_false((intmask & SDHCI_INT_DATA_AVAIL) &&
	    (slot->curcmd->opcode == MMC_SEND_TUNING_BLOCK ||
	    slot->curcmd->opcode == MMC_SEND_TUNING_BLOCK_HS200))) {
		slot->req->flags |= MMC_TUNE_DONE;
		sdhci_finish_command(slot);
		sdhci_finish_data(slot);
		return;
	}
	/* Handle PIO interrupt. */
	if (intmask & (SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL)) {
		if ((slot->opt & SDHCI_PLATFORM_TRANSFER) &&
		    SDHCI_PLATFORM_WILL_HANDLE(slot->bus, slot)) {
			SDHCI_PLATFORM_START_TRANSFER(slot->bus, slot,
			    &intmask);
			slot->flags |= PLATFORM_DATA_STARTED;
		} else
			sdhci_transfer_pio(slot);
	}
	/* Handle DMA border. */
	if (intmask & SDHCI_INT_DMA_END) {
		data = slot->curcmd->data;
		sdma_bbufsz = slot->sdma_bbufsz;

		/* Unload DMA buffer ... */
		left = data->len - slot->offset;
		if (data->flags & MMC_DATA_READ) {
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_POSTREAD);
			memcpy((u_char*)data->data + slot->offset, slot->dmamem,
			    ulmin(left, sdma_bbufsz));
		} else {
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_POSTWRITE);
		}
		/* ... and reload it again. */
		slot->offset += sdma_bbufsz;
		left = data->len - slot->offset;
		if (data->flags & MMC_DATA_READ) {
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_PREREAD);
		} else {
			memcpy(slot->dmamem, (u_char*)data->data + slot->offset,
			    ulmin(left, sdma_bbufsz));
			bus_dmamap_sync(slot->dmatag, slot->dmamap,
			    BUS_DMASYNC_PREWRITE);
		}
		/*
		 * Interrupt aggregation: Mask border interrupt for the last
		 * bounce buffer.
		 */
		if (left == sdma_bbufsz) {
			slot->intmask &= ~SDHCI_INT_DMA_END;
			WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
		}
		/* Restart DMA. */
		WR4(slot, SDHCI_DMA_ADDRESS, slot->paddr);
	}
	/* We have got all data. */
	if (intmask & SDHCI_INT_DATA_END) {
		if (slot->flags & PLATFORM_DATA_STARTED) {
			slot->flags &= ~PLATFORM_DATA_STARTED;
			SDHCI_PLATFORM_FINISH_TRANSFER(slot->bus, slot);
		} else
			sdhci_finish_data(slot);
	}
done:
	if (slot->curcmd != NULL && slot->curcmd->error != 0) {
		if (slot->flags & PLATFORM_DATA_STARTED) {
			slot->flags &= ~PLATFORM_DATA_STARTED;
			SDHCI_PLATFORM_FINISH_TRANSFER(slot->bus, slot);
		} else
			sdhci_finish_data(slot);
	}
}

static void
sdhci_acmd_irq(struct sdhci_slot *slot, uint16_t acmd_err)
{

	if (!slot->curcmd) {
		slot_printf(slot, "Got AutoCMD12 error 0x%04x, but "
		    "there is no active command.\n", acmd_err);
		sdhci_dumpregs(slot);
		return;
	}
	slot_printf(slot, "Got AutoCMD12 error 0x%04x\n", acmd_err);
	sdhci_reset(slot, SDHCI_RESET_CMD);
}

void
sdhci_generic_intr(struct sdhci_slot *slot)
{
	uint32_t intmask, present;
	uint16_t val16;

	SDHCI_LOCK(slot);
	/* Read slot interrupt status. */
	intmask = RD4(slot, SDHCI_INT_STATUS);
	if (intmask == 0 || intmask == 0xffffffff) {
		SDHCI_UNLOCK(slot);
		return;
	}
	if (__predict_false(sdhci_debug > 2))
		slot_printf(slot, "Interrupt %#x\n", intmask);

	/* Handle tuning error interrupt. */
	if (__predict_false(intmask & SDHCI_INT_TUNEERR)) {
		WR4(slot, SDHCI_INT_STATUS, SDHCI_INT_TUNEERR);
		slot_printf(slot, "Tuning error indicated\n");
		slot->retune_req |= SDHCI_RETUNE_REQ_RESET;
		if (slot->curcmd) {
			slot->curcmd->error = MMC_ERR_BADCRC;
			sdhci_finish_command(slot);
		}
	}
	/* Handle re-tuning interrupt. */
	if (__predict_false(intmask & SDHCI_INT_RETUNE))
		slot->retune_req |= SDHCI_RETUNE_REQ_NEEDED;
	/* Handle card presence interrupts. */
	if (intmask & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		present = (intmask & SDHCI_INT_CARD_INSERT) != 0;
		slot->intmask &=
		    ~(SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE);
		slot->intmask |= present ? SDHCI_INT_CARD_REMOVE :
		    SDHCI_INT_CARD_INSERT;
		WR4(slot, SDHCI_INT_ENABLE, slot->intmask);
		WR4(slot, SDHCI_SIGNAL_ENABLE, slot->intmask);
		WR4(slot, SDHCI_INT_STATUS, intmask &
		    (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE));
		sdhci_handle_card_present_locked(slot, present);
	}
	/* Handle command interrupts. */
	if (intmask & SDHCI_INT_CMD_MASK) {
		WR4(slot, SDHCI_INT_STATUS, intmask & SDHCI_INT_CMD_MASK);
		sdhci_cmd_irq(slot, intmask & SDHCI_INT_CMD_MASK);
	}
	/* Handle data interrupts. */
	if (intmask & SDHCI_INT_DATA_MASK) {
		WR4(slot, SDHCI_INT_STATUS, intmask & SDHCI_INT_DATA_MASK);
		/* Don't call data_irq in case of errored command. */
		if ((intmask & SDHCI_INT_CMD_ERROR_MASK) == 0)
			sdhci_data_irq(slot, intmask & SDHCI_INT_DATA_MASK);
	}
	/* Handle AutoCMD12 error interrupt. */
	if (intmask & SDHCI_INT_ACMD12ERR) {
		/* Clearing SDHCI_INT_ACMD12ERR may clear SDHCI_ACMD12_ERR. */
		val16 = RD2(slot, SDHCI_ACMD12_ERR);
		WR4(slot, SDHCI_INT_STATUS, SDHCI_INT_ACMD12ERR);
		sdhci_acmd_irq(slot, val16);
	}
	/* Handle bus power interrupt. */
	if (intmask & SDHCI_INT_BUS_POWER) {
		WR4(slot, SDHCI_INT_STATUS, SDHCI_INT_BUS_POWER);
		slot_printf(slot, "Card is consuming too much power!\n");
	}
	intmask &= ~(SDHCI_INT_ERROR | SDHCI_INT_TUNEERR | SDHCI_INT_RETUNE |
	    SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE | SDHCI_INT_CMD_MASK |
	    SDHCI_INT_DATA_MASK | SDHCI_INT_ACMD12ERR | SDHCI_INT_BUS_POWER);
	/* The rest is unknown. */
	if (intmask) {
		WR4(slot, SDHCI_INT_STATUS, intmask);
		slot_printf(slot, "Unexpected interrupt 0x%08x.\n",
		    intmask);
		sdhci_dumpregs(slot);
	}

	SDHCI_UNLOCK(slot);
}

int
sdhci_generic_read_ivar(device_t bus, device_t child, int which,
    uintptr_t *result)
{
	const struct sdhci_slot *slot = device_get_ivars(child);

	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		*result = slot->host.ios.bus_mode;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		*result = slot->host.ios.bus_width;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		*result = slot->host.ios.chip_select;
		break;
	case MMCBR_IVAR_CLOCK:
		*result = slot->host.ios.clock;
		break;
	case MMCBR_IVAR_F_MIN:
		*result = slot->host.f_min;
		break;
	case MMCBR_IVAR_F_MAX:
		*result = slot->host.f_max;
		break;
	case MMCBR_IVAR_HOST_OCR:
		*result = slot->host.host_ocr;
		break;
	case MMCBR_IVAR_MODE:
		*result = slot->host.mode;
		break;
	case MMCBR_IVAR_OCR:
		*result = slot->host.ocr;
		break;
	case MMCBR_IVAR_POWER_MODE:
		*result = slot->host.ios.power_mode;
		break;
	case MMCBR_IVAR_VDD:
		*result = slot->host.ios.vdd;
		break;
	case MMCBR_IVAR_RETUNE_REQ:
		if (slot->opt & SDHCI_TUNING_ENABLED) {
			if (slot->retune_req & SDHCI_RETUNE_REQ_RESET) {
				*result = retune_req_reset;
				break;
			}
			if (slot->retune_req & SDHCI_RETUNE_REQ_NEEDED) {
				*result = retune_req_normal;
				break;
			}
		}
		*result = retune_req_none;
		break;
	case MMCBR_IVAR_VCCQ:
		*result = slot->host.ios.vccq;
		break;
	case MMCBR_IVAR_CAPS:
		*result = slot->host.caps;
		break;
	case MMCBR_IVAR_TIMING:
		*result = slot->host.ios.timing;
		break;
	case MMCBR_IVAR_MAX_DATA:
		/*
		 * Re-tuning modes 1 and 2 restrict the maximum data length
		 * per read/write command to 4 MiB.
		 */
		if (slot->opt & SDHCI_TUNING_ENABLED &&
		    (slot->retune_mode == SDHCI_RETUNE_MODE_1 ||
		    slot->retune_mode == SDHCI_RETUNE_MODE_2)) {
			*result = 4 * 1024 * 1024 / MMC_SECTOR_SIZE;
			break;
		}
		*result = 65535;
		break;
	case MMCBR_IVAR_MAX_BUSY_TIMEOUT:
		/*
		 * Currently, sdhci_start_data() hardcodes 1 s for all CMDs.
		 */
		*result = 1000000;
		break;
	}
	return (0);
}

int
sdhci_generic_write_ivar(device_t bus, device_t child, int which,
    uintptr_t value)
{
	struct sdhci_slot *slot = device_get_ivars(child);
	uint32_t clock, max_clock;
	int i;

	if (sdhci_debug > 1)
		slot_printf(slot, "%s: var=%d\n", __func__, which);
	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		slot->host.ios.bus_mode = value;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		slot->host.ios.bus_width = value;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		slot->host.ios.chip_select = value;
		break;
	case MMCBR_IVAR_CLOCK:
		if (value > 0) {
			max_clock = slot->max_clk;
			clock = max_clock;

			if (slot->version < SDHCI_SPEC_300) {
				for (i = 0; i < SDHCI_200_MAX_DIVIDER;
				    i <<= 1) {
					if (clock <= value)
						break;
					clock >>= 1;
				}
			} else {
				for (i = 0; i < SDHCI_300_MAX_DIVIDER;
				    i += 2) {
					if (clock <= value)
						break;
					clock = max_clock / (i + 2);
				}
			}

			slot->host.ios.clock = clock;
		} else
			slot->host.ios.clock = 0;
		break;
	case MMCBR_IVAR_MODE:
		slot->host.mode = value;
		break;
	case MMCBR_IVAR_OCR:
		slot->host.ocr = value;
		break;
	case MMCBR_IVAR_POWER_MODE:
		slot->host.ios.power_mode = value;
		break;
	case MMCBR_IVAR_VDD:
		slot->host.ios.vdd = value;
		break;
	case MMCBR_IVAR_VCCQ:
		slot->host.ios.vccq = value;
		break;
	case MMCBR_IVAR_TIMING:
		slot->host.ios.timing = value;
		break;
	case MMCBR_IVAR_CAPS:
	case MMCBR_IVAR_HOST_OCR:
	case MMCBR_IVAR_F_MIN:
	case MMCBR_IVAR_F_MAX:
	case MMCBR_IVAR_MAX_DATA:
	case MMCBR_IVAR_RETUNE_REQ:
		return (EINVAL);
	}
	return (0);
}

#ifdef MMCCAM
void
sdhci_start_slot(struct sdhci_slot *slot)
{

	if ((slot->devq = cam_simq_alloc(1)) == NULL)
		goto fail;

	mtx_init(&slot->sim_mtx, "sdhcisim", NULL, MTX_DEF);
	slot->sim = cam_sim_alloc(sdhci_cam_action, sdhci_cam_poll,
	    "sdhci_slot", slot, device_get_unit(slot->bus),
	    &slot->sim_mtx, 1, 1, slot->devq);

	if (slot->sim == NULL) {
		cam_simq_free(slot->devq);
		slot_printf(slot, "cannot allocate CAM SIM\n");
		goto fail;
	}

	mtx_lock(&slot->sim_mtx);
	if (xpt_bus_register(slot->sim, slot->bus, 0) != 0) {
		slot_printf(slot, "cannot register SCSI pass-through bus\n");
		cam_sim_free(slot->sim, FALSE);
		cam_simq_free(slot->devq);
		mtx_unlock(&slot->sim_mtx);
		goto fail;
	}
	mtx_unlock(&slot->sim_mtx);

	/* End CAM-specific init */
	slot->card_present = 0;
	sdhci_card_task(slot, 0);
	return;

fail:
	if (slot->sim != NULL) {
		mtx_lock(&slot->sim_mtx);
		xpt_bus_deregister(cam_sim_path(slot->sim));
		cam_sim_free(slot->sim, FALSE);
		mtx_unlock(&slot->sim_mtx);
	}

	if (slot->devq != NULL)
		cam_simq_free(slot->devq);
}

static void
sdhci_cam_handle_mmcio(struct cam_sim *sim, union ccb *ccb)
{
	struct sdhci_slot *slot;

	slot = cam_sim_softc(sim);

	sdhci_cam_request(slot, ccb);
}

void
sdhci_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct sdhci_slot *slot;

	slot = cam_sim_softc(sim);
	if (slot == NULL) {
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		xpt_done(ccb);
		return;
	}

	mtx_assert(&slot->sim_mtx, MA_OWNED);

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi;

		cpi = &ccb->cpi;
		cpi->version_num = 1;
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 1;
		cpi->maxio = MAXPHYS;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "Deglitch Networks", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 100; /* XXX WTF? */
		cpi->protocol = PROTO_MMCSD;
		cpi->protocol_version = SCSI_REV_0;
		cpi->transport = XPORT_MMCSD;
		cpi->transport_version = 0;

		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;

		if (sdhci_debug > 1)
			slot_printf(slot, "Got XPT_GET_TRAN_SETTINGS\n");

		cts->protocol = PROTO_MMCSD;
		cts->protocol_version = 1;
		cts->transport = XPORT_MMCSD;
		cts->transport_version = 1;
		cts->xport_specific.valid = 0;
		cts->proto_specific.mmc.host_ocr = slot->host.host_ocr;
		cts->proto_specific.mmc.host_f_min = slot->host.f_min;
		cts->proto_specific.mmc.host_f_max = slot->host.f_max;
		cts->proto_specific.mmc.host_caps = slot->host.caps;
		memcpy(&cts->proto_specific.mmc.ios, &slot->host.ios, sizeof(struct mmc_ios));
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
	{
		if (sdhci_debug > 1)
			slot_printf(slot, "Got XPT_SET_TRAN_SETTINGS\n");
		sdhci_cam_settran_settings(slot, ccb);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:
		if (sdhci_debug > 1)
			slot_printf(slot, "Got XPT_RESET_BUS, ACK it...\n");
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_MMC_IO:
		/*
		 * Here is the HW-dependent part of
		 * sending the command to the underlying h/w
		 * At some point in the future an interrupt comes.
		 * Then the request will be marked as completed.
		 */
		if (__predict_false(sdhci_debug > 1))
			slot_printf(slot, "Got XPT_MMC_IO\n");
		ccb->ccb_h.status = CAM_REQ_INPROG;

		sdhci_cam_handle_mmcio(sim, ccb);
		return;
		/* NOTREACHED */
		break;
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
	return;
}

void
sdhci_cam_poll(struct cam_sim *sim)
{
	return;
}

static int
sdhci_cam_get_possible_host_clock(const struct sdhci_slot *slot,
    int proposed_clock)
{
	int max_clock, clock, i;

	if (proposed_clock == 0)
		return 0;
	max_clock = slot->max_clk;
	clock = max_clock;

	if (slot->version < SDHCI_SPEC_300) {
		for (i = 0; i < SDHCI_200_MAX_DIVIDER; i <<= 1) {
			if (clock <= proposed_clock)
				break;
			clock >>= 1;
		}
	} else {
		for (i = 0; i < SDHCI_300_MAX_DIVIDER; i += 2) {
			if (clock <= proposed_clock)
				break;
			clock = max_clock / (i + 2);
		}
	}
	return clock;
}

static int
sdhci_cam_settran_settings(struct sdhci_slot *slot, union ccb *ccb)
{
	struct mmc_ios *ios;
	const struct mmc_ios *new_ios;
	const struct ccb_trans_settings_mmc *cts;

	ios = &slot->host.ios;
	cts = &ccb->cts.proto_specific.mmc;
	new_ios = &cts->ios;

	/* Update only requested fields */
	if (cts->ios_valid & MMC_CLK) {
		ios->clock = sdhci_cam_get_possible_host_clock(slot, new_ios->clock);
		slot_printf(slot, "Clock => %d\n", ios->clock);
	}
	if (cts->ios_valid & MMC_VDD) {
		ios->vdd = new_ios->vdd;
		slot_printf(slot, "VDD => %d\n", ios->vdd);
	}
	if (cts->ios_valid & MMC_CS) {
		ios->chip_select = new_ios->chip_select;
		slot_printf(slot, "CS => %d\n", ios->chip_select);
	}
	if (cts->ios_valid & MMC_BW) {
		ios->bus_width = new_ios->bus_width;
		slot_printf(slot, "Bus width => %d\n", ios->bus_width);
	}
	if (cts->ios_valid & MMC_PM) {
		ios->power_mode = new_ios->power_mode;
		slot_printf(slot, "Power mode => %d\n", ios->power_mode);
	}
	if (cts->ios_valid & MMC_BT) {
		ios->timing = new_ios->timing;
		slot_printf(slot, "Timing => %d\n", ios->timing);
	}
	if (cts->ios_valid & MMC_BM) {
		ios->bus_mode = new_ios->bus_mode;
		slot_printf(slot, "Bus mode => %d\n", ios->bus_mode);
	}

	/* XXX Provide a way to call a chip-specific IOS update, required for TI */
	return (sdhci_cam_update_ios(slot));
}

static int
sdhci_cam_update_ios(struct sdhci_slot *slot)
{
	struct mmc_ios *ios = &slot->host.ios;

	slot_printf(slot, "%s: power_mode=%d, clk=%d, bus_width=%d, timing=%d\n",
		    __func__, ios->power_mode, ios->clock, ios->bus_width, ios->timing);
	SDHCI_LOCK(slot);
	/* Do full reset on bus power down to clear from any state. */
	if (ios->power_mode == power_off) {
		WR4(slot, SDHCI_SIGNAL_ENABLE, 0);
		sdhci_init(slot);
	}
	/* Configure the bus. */
	sdhci_set_clock(slot, ios->clock);
	sdhci_set_power(slot, (ios->power_mode == power_off) ? 0 : ios->vdd);
	if (ios->bus_width == bus_width_8) {
		slot->hostctrl |= SDHCI_CTRL_8BITBUS;
		slot->hostctrl &= ~SDHCI_CTRL_4BITBUS;
	} else if (ios->bus_width == bus_width_4) {
		slot->hostctrl &= ~SDHCI_CTRL_8BITBUS;
		slot->hostctrl |= SDHCI_CTRL_4BITBUS;
	} else if (ios->bus_width == bus_width_1) {
		slot->hostctrl &= ~SDHCI_CTRL_8BITBUS;
		slot->hostctrl &= ~SDHCI_CTRL_4BITBUS;
	} else {
		panic("Invalid bus width: %d", ios->bus_width);
	}
	if (ios->timing == bus_timing_hs &&
	    !(slot->quirks & SDHCI_QUIRK_DONT_SET_HISPD_BIT))
		slot->hostctrl |= SDHCI_CTRL_HISPD;
	else
		slot->hostctrl &= ~SDHCI_CTRL_HISPD;
	WR1(slot, SDHCI_HOST_CONTROL, slot->hostctrl);
	/* Some controllers like reset after bus changes. */
	if(slot->quirks & SDHCI_QUIRK_RESET_ON_IOS)
		sdhci_reset(slot, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	SDHCI_UNLOCK(slot);
	return (0);
}

static int
sdhci_cam_request(struct sdhci_slot *slot, union ccb *ccb)
{
	const struct ccb_mmcio *mmcio;

	mmcio = &ccb->mmcio;

	SDHCI_LOCK(slot);
/*	if (slot->req != NULL) {
		SDHCI_UNLOCK(slot);
		return (EBUSY);
	}
*/
	if (__predict_false(sdhci_debug > 1)) {
		slot_printf(slot, "CMD%u arg %#x flags %#x dlen %u dflags %#x\n",
			    mmcio->cmd.opcode, mmcio->cmd.arg, mmcio->cmd.flags,
			    mmcio->cmd.data != NULL ? (unsigned int) mmcio->cmd.data->len : 0,
			    mmcio->cmd.data != NULL ? mmcio->cmd.data->flags: 0);
	}
	if (mmcio->cmd.data != NULL) {
		if (mmcio->cmd.data->len == 0 || mmcio->cmd.data->flags == 0)
			panic("data->len = %d, data->flags = %d -- something is b0rked",
			    (int)mmcio->cmd.data->len, mmcio->cmd.data->flags);
	}
	slot->ccb = ccb;
	slot->flags = 0;
	sdhci_start(slot);
	SDHCI_UNLOCK(slot);
	if (dumping) {
		while (slot->ccb != NULL) {
			sdhci_generic_intr(slot);
			DELAY(10);
		}
	}
	return (0);
}
#endif /* MMCCAM */

MODULE_VERSION(sdhci, SDHCI_VERSION);
