/*	$OpenBSD: sdhc.c,v 1.78 2024/10/19 21:10:03 hastings Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SD Host Controller driver based on the SD Host Controller Standard
 * Simplified Specification Version 1.00 (www.sdcard.org).
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <dev/sdmmc/sdhcreg.h>
#include <dev/sdmmc/sdhcvar.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmc_ioreg.h>

/* Timeouts in seconds */
#define SDHC_COMMAND_TIMEOUT	1
#define SDHC_BUFFER_TIMEOUT	1
#define SDHC_TRANSFER_TIMEOUT	1
#define SDHC_DMA_TIMEOUT	3

struct sdhc_host {
	struct sdhc_softc *sc;		/* host controller device */
	struct device *sdmmc;		/* generic SD/MMC device */
	bus_space_tag_t iot;		/* host register set tag */
	bus_space_handle_t ioh;		/* host register set handle */
	u_int16_t version;		/* specification version */
	u_int clkbase;			/* base clock frequency in KHz */
	int maxblklen;			/* maximum block length */
	int flags;			/* flags for this host */
	u_int32_t ocr;			/* OCR value from capabilities */
	u_int8_t regs[14];		/* host controller state */
	u_int16_t intr_status;		/* soft interrupt status */
	u_int16_t intr_error_status;	/* soft error status */

	bus_dmamap_t adma_map;
	bus_dma_segment_t adma_segs[1];
	caddr_t adma2;

	uint16_t block_size;
	uint16_t block_count;
	uint16_t transfer_mode;
};

/* flag values */
#define SHF_USE_DMA		0x0001
#define SHF_USE_DMA64		0x0002
#define SHF_USE_32BIT_ACCESS	0x0004

#define HREAD1(hp, reg)							\
	(sdhc_read_1((hp), (reg)))
#define HREAD2(hp, reg)							\
	(sdhc_read_2((hp), (reg)))
#define HREAD4(hp, reg)							\
	(bus_space_read_4((hp)->iot, (hp)->ioh, (reg)))
#define HWRITE1(hp, reg, val)						\
	sdhc_write_1((hp), (reg), (val))
#define HWRITE2(hp, reg, val)						\
	sdhc_write_2((hp), (reg), (val))
#define HWRITE4(hp, reg, val)						\
	bus_space_write_4((hp)->iot, (hp)->ioh, (reg), (val))
#define HCLR1(hp, reg, bits)						\
	HWRITE1((hp), (reg), HREAD1((hp), (reg)) & ~(bits))
#define HCLR2(hp, reg, bits)						\
	HWRITE2((hp), (reg), HREAD2((hp), (reg)) & ~(bits))
#define HSET1(hp, reg, bits)						\
	HWRITE1((hp), (reg), HREAD1((hp), (reg)) | (bits))
#define HSET2(hp, reg, bits)						\
	HWRITE2((hp), (reg), HREAD2((hp), (reg)) | (bits))

int	sdhc_host_reset(sdmmc_chipset_handle_t);
u_int32_t sdhc_host_ocr(sdmmc_chipset_handle_t);
int	sdhc_host_maxblklen(sdmmc_chipset_handle_t);
int	sdhc_card_detect(sdmmc_chipset_handle_t);
int	sdhc_bus_power(sdmmc_chipset_handle_t, u_int32_t);
int	sdhc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	sdhc_bus_width(sdmmc_chipset_handle_t, int);
void	sdhc_card_intr_mask(sdmmc_chipset_handle_t, int);
void	sdhc_card_intr_ack(sdmmc_chipset_handle_t);
int	sdhc_signal_voltage(sdmmc_chipset_handle_t, int);
void	sdhc_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
int	sdhc_start_command(struct sdhc_host *, struct sdmmc_command *);
int	sdhc_wait_state(struct sdhc_host *, u_int32_t, u_int32_t);
int	sdhc_soft_reset(struct sdhc_host *, int);
int	sdhc_wait_intr_cold(struct sdhc_host *, int, int);
int	sdhc_wait_intr(struct sdhc_host *, int, int);
void	sdhc_transfer_data(struct sdhc_host *, struct sdmmc_command *);
void	sdhc_read_data(struct sdhc_host *, u_char *, int);
void	sdhc_write_data(struct sdhc_host *, u_char *, int);
int	sdhc_hibernate_init(sdmmc_chipset_handle_t, void *);

#ifdef SDHC_DEBUG
int sdhcdebug = 0;
#define DPRINTF(n,s)	do { if ((n) <= sdhcdebug) printf s; } while (0)
void	sdhc_dump_regs(struct sdhc_host *);
#else
#define DPRINTF(n,s)	do {} while(0)
#endif

struct sdmmc_chip_functions sdhc_functions = {
	.host_reset = sdhc_host_reset,
	.host_ocr = sdhc_host_ocr,
	.host_maxblklen = sdhc_host_maxblklen,
	.card_detect = sdhc_card_detect,
	.bus_power = sdhc_bus_power,
	.bus_clock = sdhc_bus_clock,
	.bus_width = sdhc_bus_width,
	.exec_command = sdhc_exec_command,
	.card_intr_mask = sdhc_card_intr_mask,
	.card_intr_ack = sdhc_card_intr_ack,
	.signal_voltage = sdhc_signal_voltage,
	.hibernate_init = sdhc_hibernate_init,
};

struct cfdriver sdhc_cd = {
	NULL, "sdhc", DV_DULL
};

/*
 * Some controllers live on a bus that only allows 32-bit
 * transactions.  In that case we use a RMW cycle for 8-bit and 16-bit
 * register writes.  However that doesn't work for the Transfer Mode
 * register as this register lives in the same 32-bit word as the
 * Command register and writing the Command register triggers SD
 * command generation.  We avoid this issue by using a shadow variable
 * for the Transfer Mode register that we write out when we write the
 * Command register.
 *
 * The Arasan controller integrated on the Broadcom SoCs
 * used in the Raspberry Pi has an interesting bug where writing the
 * same 32-bit register twice doesn't work.  This means that we lose
 * writes to the Block Sine and/or Block Count register.  We work
 * around that issue by using shadow variables as well.
 */

uint8_t
sdhc_read_1(struct sdhc_host *hp, bus_size_t offset)
{
	uint32_t reg;

	if (hp->flags & SHF_USE_32BIT_ACCESS) {
		reg = bus_space_read_4(hp->iot, hp->ioh, offset & ~3);
		return (reg >> ((offset & 3) * 8)) & 0xff;
	}

	return bus_space_read_1(hp->iot, hp->ioh, offset);
}

uint16_t
sdhc_read_2(struct sdhc_host *hp, bus_size_t offset)
{
	uint32_t reg;

	if (hp->flags & SHF_USE_32BIT_ACCESS) {
		reg = bus_space_read_4(hp->iot, hp->ioh, offset & ~2);
		return (reg >> ((offset & 2) * 8)) & 0xffff;
	}

	return bus_space_read_2(hp->iot, hp->ioh, offset);
}

void
sdhc_write_1(struct sdhc_host *hp, bus_size_t offset, uint8_t value)
{
	uint32_t reg;

	if (hp->flags & SHF_USE_32BIT_ACCESS) {
		reg = bus_space_read_4(hp->iot, hp->ioh, offset & ~3);
		reg &= ~(0xff << ((offset & 3) * 8));
		reg |= (value << ((offset & 3) * 8));
		bus_space_write_4(hp->iot, hp->ioh, offset & ~3, reg);
		return;
	}

	bus_space_write_1(hp->iot, hp->ioh, offset, value);
}

void
sdhc_write_2(struct sdhc_host *hp, bus_size_t offset, uint16_t value)
{
	uint32_t reg;

	if (hp->flags & SHF_USE_32BIT_ACCESS) {
		switch (offset) {
		case SDHC_BLOCK_SIZE:
			hp->block_size = value;
			return;
		case SDHC_BLOCK_COUNT:
			hp->block_count = value;
			return;
		case SDHC_TRANSFER_MODE:
			hp->transfer_mode = value;
			return;
		case SDHC_COMMAND:
			bus_space_write_4(hp->iot, hp->ioh, SDHC_BLOCK_SIZE,
			    (hp->block_count << 16) | hp->block_size);
			bus_space_write_4(hp->iot, hp->ioh, SDHC_TRANSFER_MODE,
			    (value << 16) | hp->transfer_mode);
			return;
		}

		reg = bus_space_read_4(hp->iot, hp->ioh, offset & ~2);
		reg &= ~(0xffff << ((offset & 2) * 8));
		reg |= (value << ((offset & 2) * 8));
		bus_space_write_4(hp->iot, hp->ioh, offset & ~2, reg);
		return;
	}

	bus_space_write_2(hp->iot, hp->ioh, offset, value);
}

/*
 * Called by attachment driver.  For each SD card slot there is one SD
 * host controller standard register set. (1.3)
 */
int
sdhc_host_found(struct sdhc_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh, bus_size_t iosize, int usedma, uint64_t capmask,
    uint64_t capset)
{
	struct sdmmcbus_attach_args saa;
	struct sdhc_host *hp;
	uint32_t caps;
	int major, minor;
	int error = 1;
	int max_clock;

	/* Allocate one more host structure. */
	sc->sc_nhosts++;
	hp = malloc(sizeof(*hp), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_host[sc->sc_nhosts - 1] = hp;

	if (ISSET(sc->sc_flags, SDHC_F_32BIT_ACCESS))
		SET(hp->flags, SHF_USE_32BIT_ACCESS);

	/* Fill in the new host structure. */
	hp->sc = sc;
	hp->iot = iot;
	hp->ioh = ioh;

	/* Store specification version. */
	hp->version = HREAD2(hp, SDHC_HOST_CTL_VERSION);

	/*
	 * Reset the host controller and enable interrupts.
	 */
	(void)sdhc_host_reset(hp);

	/* Determine host capabilities. */
	caps = HREAD4(hp, SDHC_CAPABILITIES);
	caps &= ~capmask;
	caps |= capset;

	/* Use DMA if the host system and the controller support it. */
	if (usedma && ISSET(caps, SDHC_ADMA2_SUPP)) {
		SET(hp->flags, SHF_USE_DMA);
		if (ISSET(caps, SDHC_64BIT_DMA_SUPP))
			SET(hp->flags, SHF_USE_DMA64);
	}

	/*
	 * Determine the base clock frequency. (2.2.24)
	 */
	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3) {
		/* SDHC 3.0 supports 10-255 MHz. */
		max_clock = 255000;
		if (SDHC_BASE_FREQ_KHZ_V3(caps) != 0)
			hp->clkbase = SDHC_BASE_FREQ_KHZ_V3(caps);
	} else {
		/* SDHC 1.0/2.0 supports only 10-63 MHz. */
		max_clock = 63000;
		if (SDHC_BASE_FREQ_KHZ(caps) != 0)
			hp->clkbase = SDHC_BASE_FREQ_KHZ(caps);
	}
	if (hp->clkbase == 0) {
		/* Make sure we can clock down to 400 kHz. */
		max_clock = 400 * SDHC_SDCLK_DIV_MAX_V3;
		hp->clkbase = sc->sc_clkbase;
	}
	if (hp->clkbase == 0) {
		/* The attachment driver must tell us. */
		printf("%s: base clock frequency unknown\n",
		    sc->sc_dev.dv_xname);
		goto err;
	} else if (hp->clkbase < 10000 || hp->clkbase > max_clock) {
		printf("%s: base clock frequency out of range: %u MHz\n",
		    sc->sc_dev.dv_xname, hp->clkbase / 1000);
		goto err;
	}

	switch (SDHC_SPEC_VERSION(hp->version)) {
	case SDHC_SPEC_VERS_4_10:
		major = 4, minor = 10;
		break;
	case SDHC_SPEC_VERS_4_20:
		major = 4, minor = 20;
		break;
	default:
		major = SDHC_SPEC_VERSION(hp->version) + 1, minor = 0;
		break;
	}

	printf("%s: SDHC %d.%02d, %d MHz base clock\n", DEVNAME(sc),
	    major, minor, hp->clkbase / 1000);

	/*
	 * XXX Set the data timeout counter value according to
	 * capabilities. (2.2.15)
	 */

	/*
	 * Determine SD bus voltage levels supported by the controller.
	 */
	if (ISSET(caps, SDHC_VOLTAGE_SUPP_1_8V))
		SET(hp->ocr, MMC_OCR_1_65V_1_95V);
	if (ISSET(caps, SDHC_VOLTAGE_SUPP_3_0V))
		SET(hp->ocr, MMC_OCR_2_9V_3_0V | MMC_OCR_3_0V_3_1V);
	if (ISSET(caps, SDHC_VOLTAGE_SUPP_3_3V))
		SET(hp->ocr, MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V);

	/*
	 * Determine the maximum block length supported by the host
	 * controller. (2.2.24)
	 */
	switch((caps >> SDHC_MAX_BLK_LEN_SHIFT) & SDHC_MAX_BLK_LEN_MASK) {
	case SDHC_MAX_BLK_LEN_512:
		hp->maxblklen = 512;
		break;
	case SDHC_MAX_BLK_LEN_1024:
		hp->maxblklen = 1024;
		break;
	case SDHC_MAX_BLK_LEN_2048:
		hp->maxblklen = 2048;
		break;
	default:
		hp->maxblklen = 1;
		break;
	}

	if (ISSET(hp->flags, SHF_USE_DMA)) {
		int rseg;

		/* Allocate ADMA2 descriptor memory */
		error = bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE,
		    PAGE_SIZE, hp->adma_segs, 1, &rseg,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (error)
			goto adma_done;
		error = bus_dmamem_map(sc->sc_dmat, hp->adma_segs, rseg,
		    PAGE_SIZE, &hp->adma2, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
		if (error) {
			bus_dmamem_free(sc->sc_dmat, hp->adma_segs, rseg);
			goto adma_done;
		}
		error = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE,
		    0, BUS_DMA_WAITOK, &hp->adma_map);
		if (error) {
			bus_dmamem_unmap(sc->sc_dmat, hp->adma2, PAGE_SIZE);
			bus_dmamem_free(sc->sc_dmat, hp->adma_segs, rseg);
			goto adma_done;
		}
		error = bus_dmamap_load(sc->sc_dmat, hp->adma_map,
		    hp->adma2, PAGE_SIZE, NULL,
		    BUS_DMA_WAITOK | BUS_DMA_WRITE);
		if (error) {
			bus_dmamap_destroy(sc->sc_dmat, hp->adma_map);
			bus_dmamem_unmap(sc->sc_dmat, hp->adma2, PAGE_SIZE);
			bus_dmamem_free(sc->sc_dmat, hp->adma_segs, rseg);
			goto adma_done;
		}

	adma_done:
		if (error) {
			printf("%s: can't allocate DMA descriptor table\n",
			    DEVNAME(hp->sc));
			CLR(hp->flags, SHF_USE_DMA);
		}
	}

	/*
	 * Attach the generic SD/MMC bus driver.  (The bus driver must
	 * not invoke any chipset functions before it is attached.)
	 */
	bzero(&saa, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &sdhc_functions;
	saa.sch = hp;
	saa.caps = SMC_CAPS_4BIT_MODE;
	saa.dmat = sc->sc_dmat;
	saa.dma_boundary = sc->sc_dma_boundary;
	if (ISSET(hp->flags, SHF_USE_DMA))
		saa.caps |= SMC_CAPS_DMA;

	if (ISSET(caps, SDHC_HIGH_SPEED_SUPP))
		saa.caps |= SMC_CAPS_SD_HIGHSPEED;
	if (ISSET(caps, SDHC_HIGH_SPEED_SUPP))
		saa.caps |= SMC_CAPS_MMC_HIGHSPEED;

	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3) {
		uint32_t caps2 = HREAD4(hp, SDHC_CAPABILITIES2);
		caps2 &= ~(capmask >> 32);
		caps2 |= capset >> 32;

		if (ISSET(caps, SDHC_8BIT_MODE_SUPP))
			saa.caps |= SMC_CAPS_8BIT_MODE;

		if (ISSET(caps2, SDHC_DDR50_SUPP))
			saa.caps |= SMC_CAPS_MMC_DDR52;
	}

	if (ISSET(sc->sc_flags, SDHC_F_NONREMOVABLE))
		saa.caps |= SMC_CAPS_NONREMOVABLE;

	hp->sdmmc = config_found(&sc->sc_dev, &saa, NULL);
	if (hp->sdmmc == NULL) {
		error = 0;
		goto err;
	}
	
	return 0;

err:
	free(hp, M_DEVBUF, sizeof *hp);
	sc->sc_host[sc->sc_nhosts - 1] = NULL;
	sc->sc_nhosts--;
	return (error);
}

int
sdhc_activate(struct device *self, int act)
{
	struct sdhc_softc *sc = (struct sdhc_softc *)self;
	struct sdhc_host *hp;
	int n, i, rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);

		/* Save the host controller state. */
		for (n = 0; n < sc->sc_nhosts; n++) {
			hp = sc->sc_host[n];
			for (i = 0; i < sizeof hp->regs; i++)
				hp->regs[i] = HREAD1(hp, i);
		}
		break;
	case DVACT_RESUME:
		/* Restore the host controller state. */
		for (n = 0; n < sc->sc_nhosts; n++) {
			hp = sc->sc_host[n];
			(void)sdhc_host_reset(hp);
			for (i = 0; i < sizeof hp->regs; i++)
				HWRITE1(hp, i, hp->regs[i]);
		}
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		sdhc_shutdown(self);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

/*
 * Shutdown hook established by or called from attachment driver.
 */
void
sdhc_shutdown(void *arg)
{
	struct sdhc_softc *sc = arg;
	struct sdhc_host *hp;
	int i;

	/* XXX chip locks up if we don't disable it before reboot. */
	for (i = 0; i < sc->sc_nhosts; i++) {
		hp = sc->sc_host[i];
		(void)sdhc_host_reset(hp);
	}
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int
sdhc_host_reset(sdmmc_chipset_handle_t sch)
{
	struct sdhc_host *hp = sch;
	u_int16_t imask;
	int error;
	int s;

	s = splsdmmc();

	/* Disable all interrupts. */
	HWRITE2(hp, SDHC_NINTR_SIGNAL_EN, 0);

	/*
	 * Reset the entire host controller and wait up to 100ms for
	 * the controller to clear the reset bit.
	 */
	if ((error = sdhc_soft_reset(hp, SDHC_RESET_ALL)) != 0) {
		splx(s);
		return (error);
	}	

	/* Set data timeout counter value to max for now. */
	HWRITE1(hp, SDHC_TIMEOUT_CTL, SDHC_TIMEOUT_MAX);

	/* Enable interrupts. */
	imask = SDHC_CARD_REMOVAL | SDHC_CARD_INSERTION |
	    SDHC_BUFFER_READ_READY | SDHC_BUFFER_WRITE_READY |
	    SDHC_DMA_INTERRUPT | SDHC_BLOCK_GAP_EVENT |
	    SDHC_TRANSFER_COMPLETE | SDHC_COMMAND_COMPLETE;

	HWRITE2(hp, SDHC_NINTR_STATUS_EN, imask);
	HWRITE2(hp, SDHC_EINTR_STATUS_EN, SDHC_EINTR_STATUS_MASK);
	HWRITE2(hp, SDHC_NINTR_SIGNAL_EN, imask);
	HWRITE2(hp, SDHC_EINTR_SIGNAL_EN, SDHC_EINTR_SIGNAL_MASK);

	splx(s);
	return 0;
}

u_int32_t
sdhc_host_ocr(sdmmc_chipset_handle_t sch)
{
	struct sdhc_host *hp = sch;
	return hp->ocr;
}

int
sdhc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	struct sdhc_host *hp = sch;
	return hp->maxblklen;
}

/*
 * Return non-zero if the card is currently inserted.
 */
int
sdhc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct sdhc_host *hp = sch;

	if (hp->sc->sc_card_detect)
		return hp->sc->sc_card_detect(hp->sc);

	return ISSET(HREAD4(hp, SDHC_PRESENT_STATE), SDHC_CARD_INSERTED) ?
	    1 : 0;
}

/*
 * Set or change SD bus voltage and enable or disable SD bus power.
 * Return zero on success.
 */
int
sdhc_bus_power(sdmmc_chipset_handle_t sch, u_int32_t ocr)
{
	struct sdhc_host *hp = sch;
	u_int8_t vdd;
	int s;

	s = splsdmmc();

	/* If power is disabled, reset the host and return now. */
	if (ocr == 0) {
		HWRITE1(hp, SDHC_POWER_CTL, 0);
		splx(s);
		(void)sdhc_host_reset(hp);
		return 0;
	}

	/*
	 * Select the maximum voltage according to capabilities.
	 */
	ocr &= hp->ocr;
	if (ISSET(ocr, MMC_OCR_3_2V_3_3V|MMC_OCR_3_3V_3_4V))
		vdd = SDHC_VOLTAGE_3_3V;
	else if (ISSET(ocr, MMC_OCR_2_9V_3_0V|MMC_OCR_3_0V_3_1V))
		vdd = SDHC_VOLTAGE_3_0V;
	else if (ISSET(ocr, MMC_OCR_1_65V_1_95V))
		vdd = SDHC_VOLTAGE_1_8V;
	else {
		/* Unsupported voltage level requested. */
		splx(s);
		return EINVAL;
	}

	/*
	 * Return if no change to powered bus voltage.
	 */
	if (HREAD1(hp, SDHC_POWER_CTL) ==
	    ((vdd << SDHC_VOLTAGE_SHIFT) | SDHC_BUS_POWER)) {
		splx(s);
		return 0;
	}

	/*
	 * Disable bus power before voltage change.
	 */
	if (!(hp->sc->sc_flags & SDHC_F_NOPWR0))
		HWRITE1(hp, SDHC_POWER_CTL, 0);

	/*
	 * Enable bus power.  Wait at least 1 ms (or 74 clocks) plus
	 * voltage ramp until power rises.
	 */
	HWRITE1(hp, SDHC_POWER_CTL, (vdd << SDHC_VOLTAGE_SHIFT) |
	    SDHC_BUS_POWER);
	sdmmc_delay(10000);

	/*
	 * The host system may not power the bus due to battery low,
	 * etc.  In that case, the host controller should clear the
	 * bus power bit.
	 */
	if (!ISSET(HREAD1(hp, SDHC_POWER_CTL), SDHC_BUS_POWER)) {
		splx(s);
		return ENXIO;
	}

	splx(s);
	return 0;
}

/*
 * Return the smallest possible base clock frequency divisor value
 * for the CLOCK_CTL register to produce `freq' (KHz).
 */
static int
sdhc_clock_divisor(struct sdhc_host *hp, u_int freq)
{
	int div;

	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3) {
		if (hp->clkbase <= freq)
			return 0;

		for (div = 2; div <= SDHC_SDCLK_DIV_MAX_V3; div += 2)
			if ((hp->clkbase / div) <= freq)
				return (div / 2);
	} else {
		for (div = 1; div <= SDHC_SDCLK_DIV_MAX; div *= 2)
			if ((hp->clkbase / div) <= freq)
				return (div / 2);
	}

	/* No divisor found. */
	return -1;
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int
sdhc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct sdhc_host *hp = sch;
	struct sdhc_softc *sc = hp->sc;
	int s;
	int div;
	int sdclk;
	int timo;
	int error = 0;

	s = splsdmmc();

	if (hp->sc->sc_bus_clock_pre)
		hp->sc->sc_bus_clock_pre(hp->sc, freq, timing);

#ifdef DIAGNOSTIC
	/* Must not stop the clock if commands are in progress. */
	if (ISSET(HREAD4(hp, SDHC_PRESENT_STATE), SDHC_CMD_INHIBIT_MASK) &&
	    sdhc_card_detect(hp))
		printf("sdhc_sdclk_frequency_select: command in progress\n");
#endif

	/*
	 * Stop SD clock before changing the frequency.
	 */
	HWRITE2(hp, SDHC_CLOCK_CTL, 0);
	if (freq == SDMMC_SDCLK_OFF)
		goto ret;

	if (!ISSET(sc->sc_flags, SDHC_F_NO_HS_BIT)) {
		if (timing == SDMMC_TIMING_LEGACY)
			HCLR1(hp, SDHC_HOST_CTL, SDHC_HIGH_SPEED);
		else
			HSET1(hp, SDHC_HOST_CTL, SDHC_HIGH_SPEED);
	}

	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3) {
		switch (timing) {
		case SDMMC_TIMING_MMC_DDR52:
			HCLR2(hp, SDHC_HOST_CTL2, SDHC_UHS_MODE_SELECT_MASK);
			HSET2(hp, SDHC_HOST_CTL2, SDHC_UHS_MODE_SELECT_DDR50);
			break;
		}
	}

	/*
	 * Set the minimum base clock frequency divisor.
	 */
	if ((div = sdhc_clock_divisor(hp, freq)) < 0) {
		/* Invalid base clock frequency or `freq' value. */
		error = EINVAL;
		goto ret;
	}
	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3)
		sdclk = SDHC_SDCLK_DIV_V3(div);
	else
		sdclk = SDHC_SDCLK_DIV(div);
	HWRITE2(hp, SDHC_CLOCK_CTL, sdclk);

	/*
	 * Start internal clock.  Wait 10ms for stabilization.
	 */
	HSET2(hp, SDHC_CLOCK_CTL, SDHC_INTCLK_ENABLE);
	for (timo = 1000; timo > 0; timo--) {
		if (ISSET(HREAD2(hp, SDHC_CLOCK_CTL), SDHC_INTCLK_STABLE))
			break;
		sdmmc_delay(10);
	}
	if (timo == 0) {
		error = ETIMEDOUT;
		goto ret;
	}

	/*
	 * Enable SD clock.
	 */
	HSET2(hp, SDHC_CLOCK_CTL, SDHC_SDCLK_ENABLE);

	if (hp->sc->sc_bus_clock_post)
		hp->sc->sc_bus_clock_post(hp->sc, freq, timing);

ret:
	splx(s);
	return error;
}

int
sdhc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct sdhc_host *hp = (struct sdhc_host *)sch;
	int reg;
	int s;

	if (width != 1 && width != 4 && width != 8)
		return EINVAL;

	s = splsdmmc();

	reg = HREAD1(hp, SDHC_HOST_CTL);
	reg &= ~SDHC_4BIT_MODE;
	if (SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3) {
		reg &= ~SDHC_8BIT_MODE;
	}
	if (width == 4) {
		reg |= SDHC_4BIT_MODE;
	} else if (width == 8) {
		KASSERT(SDHC_SPEC_VERSION(hp->version) >= SDHC_SPEC_V3);
		reg |= SDHC_8BIT_MODE;
	}
	HWRITE1(hp, SDHC_HOST_CTL, reg);

	splx(s);

	return 0;
}

void
sdhc_card_intr_mask(sdmmc_chipset_handle_t sch, int enable)
{
	struct sdhc_host *hp = sch;

	if (enable) {
		HSET2(hp, SDHC_NINTR_STATUS_EN, SDHC_CARD_INTERRUPT);
		HSET2(hp, SDHC_NINTR_SIGNAL_EN, SDHC_CARD_INTERRUPT);
	} else {
		HCLR2(hp, SDHC_NINTR_SIGNAL_EN, SDHC_CARD_INTERRUPT);
		HCLR2(hp, SDHC_NINTR_STATUS_EN, SDHC_CARD_INTERRUPT);
	}
}

void
sdhc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	struct sdhc_host *hp = sch;

	HSET2(hp, SDHC_NINTR_STATUS_EN, SDHC_CARD_INTERRUPT);
}

int
sdhc_signal_voltage(sdmmc_chipset_handle_t sch, int signal_voltage)
{
	struct sdhc_host *hp = sch;

	if (hp->sc->sc_signal_voltage)
		return hp->sc->sc_signal_voltage(hp->sc, signal_voltage);

	if (SDHC_SPEC_VERSION(hp->version) < SDHC_SPEC_V3)
		return EINVAL;

	switch (signal_voltage) {
	case SDMMC_SIGNAL_VOLTAGE_180:
		HSET2(hp, SDHC_HOST_CTL2, SDHC_1_8V_SIGNAL_EN);
		break;
	case SDMMC_SIGNAL_VOLTAGE_330:
		HCLR2(hp, SDHC_HOST_CTL2, SDHC_1_8V_SIGNAL_EN);
		break;
	default:
		return EINVAL;
	}

	/* Regulator output shall be stable within 5 ms. */
	sdmmc_delay(5000);

	/* Host controller clears this bit if 1.8V signalling fails. */
	if (signal_voltage == SDMMC_SIGNAL_VOLTAGE_180 &&
	    !ISSET(HREAD2(hp, SDHC_HOST_CTL2), SDHC_1_8V_SIGNAL_EN))
		return EIO;

	return 0;
}

int
sdhc_wait_state(struct sdhc_host *hp, u_int32_t mask, u_int32_t value)
{
	u_int32_t state;
	int timeout;

	for (timeout = 10; timeout > 0; timeout--) {
		if (((state = HREAD4(hp, SDHC_PRESENT_STATE)) & mask)
		    == value)
			return 0;
		sdmmc_delay(10000);
	}
	DPRINTF(0,("%s: timeout waiting for %x (state=%b)\n", DEVNAME(hp->sc),
	    value, state, SDHC_PRESENT_STATE_BITS));
	return ETIMEDOUT;
}

void
sdhc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct sdhc_host *hp = sch;
	int error;

	/*
	 * Start the MMC command, or mark `cmd' as failed and return.
	 */
	error = sdhc_start_command(hp, cmd);
	if (error != 0) {
		cmd->c_error = error;
		SET(cmd->c_flags, SCF_ITSDONE);
		return;
	}

	/*
	 * Wait until the command phase is done, or until the command
	 * is marked done for any other reason.
	 */
	if (!sdhc_wait_intr(hp, SDHC_COMMAND_COMPLETE,
	    SDHC_COMMAND_TIMEOUT)) {
		cmd->c_error = ETIMEDOUT;
		SET(cmd->c_flags, SCF_ITSDONE);
		return;
	}

	/*
	 * The host controller removes bits [0:7] from the response
	 * data (CRC) and we pass the data up unchanged to the bus
	 * driver (without padding).
	 */
	if (cmd->c_error == 0 && ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (ISSET(cmd->c_flags, SCF_RSP_136)) {
			u_char *p = (u_char *)cmd->c_resp;
			int i;

			for (i = 0; i < 15; i++)
				*p++ = HREAD1(hp, SDHC_RESPONSE + i);
		} else
			cmd->c_resp[0] = HREAD4(hp, SDHC_RESPONSE);
	}

	/*
	 * If the command has data to transfer in any direction,
	 * execute the transfer now.
	 */
	if (cmd->c_error == 0 && cmd->c_data != NULL)
		sdhc_transfer_data(hp, cmd);

	/* Turn off the LED. */
	HCLR1(hp, SDHC_HOST_CTL, SDHC_LED_ON);

	DPRINTF(1,("%s: cmd %u done (flags=%#x error=%d)\n",
	    DEVNAME(hp->sc), cmd->c_opcode, cmd->c_flags, cmd->c_error));
	SET(cmd->c_flags, SCF_ITSDONE);
}

int
sdhc_start_command(struct sdhc_host *hp, struct sdmmc_command *cmd)
{
	struct sdhc_adma2_descriptor32 *desc32 = (void *)hp->adma2;
	struct sdhc_adma2_descriptor64 *desc64 = (void *)hp->adma2;
	struct sdhc_softc *sc = hp->sc;
	u_int16_t blksize = 0;
	u_int16_t blkcount = 0;
	u_int16_t mode;
	u_int16_t command;
	int error;
	int seg;
	int s;
	
	DPRINTF(1,("%s: start cmd %u arg=%#x data=%p dlen=%d flags=%#x\n",
	    DEVNAME(hp->sc), cmd->c_opcode, cmd->c_arg, cmd->c_data,
	    cmd->c_datalen, cmd->c_flags));

	/*
	 * The maximum block length for commands should be the minimum
	 * of the host buffer size and the card buffer size. (1.7.2)
	 */

	/* Fragment the data into proper blocks. */
	if (cmd->c_datalen > 0) {
		blksize = MIN(cmd->c_datalen, cmd->c_blklen);
		blkcount = cmd->c_datalen / blksize;
		if (cmd->c_datalen % blksize > 0) {
			/* XXX: Split this command. (1.7.4) */
			printf("%s: data not a multiple of %d bytes\n",
			    DEVNAME(hp->sc), blksize);
			return EINVAL;
		}
	}

	/* Check limit imposed by 9-bit block count. (1.7.2) */
	if (blkcount > SDHC_BLOCK_COUNT_MAX) {
		printf("%s: too much data\n", DEVNAME(hp->sc));
		return EINVAL;
	}

	/* Prepare transfer mode register value. (2.2.5) */
	mode = 0;
	if (ISSET(cmd->c_flags, SCF_CMD_READ))
		mode |= SDHC_READ_MODE;
	if (blkcount > 0) {
		mode |= SDHC_BLOCK_COUNT_ENABLE;
		if (blkcount > 1) {
			mode |= SDHC_MULTI_BLOCK_MODE;
			if (cmd->c_opcode != SD_IO_RW_EXTENDED)
				mode |= SDHC_AUTO_CMD12_ENABLE;
		}
	}
	if (cmd->c_dmamap && cmd->c_datalen > 0 &&
	    ISSET(hp->flags, SHF_USE_DMA))
		mode |= SDHC_DMA_ENABLE;

	/*
	 * Prepare command register value. (2.2.6)
	 */
	command = (cmd->c_opcode & SDHC_COMMAND_INDEX_MASK) <<
	    SDHC_COMMAND_INDEX_SHIFT;

	if (ISSET(cmd->c_flags, SCF_RSP_CRC))
		command |= SDHC_CRC_CHECK_ENABLE;
	if (ISSET(cmd->c_flags, SCF_RSP_IDX))
		command |= SDHC_INDEX_CHECK_ENABLE;
	if (cmd->c_data != NULL)
		command |= SDHC_DATA_PRESENT_SELECT;

	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		command |= SDHC_NO_RESPONSE;
	else if (ISSET(cmd->c_flags, SCF_RSP_136))
		command |= SDHC_RESP_LEN_136;
	else if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		command |= SDHC_RESP_LEN_48_CHK_BUSY;
	else
		command |= SDHC_RESP_LEN_48;

	/* Wait until command and data inhibit bits are clear. (1.5) */
	if ((error = sdhc_wait_state(hp, SDHC_CMD_INHIBIT_MASK, 0)) != 0)
		return error;

	s = splsdmmc();

	/* Alert the user not to remove the card. */
	HSET1(hp, SDHC_HOST_CTL, SDHC_LED_ON);

	/* Set DMA start address if SHF_USE_DMA is set. */
	if (cmd->c_dmamap && ISSET(hp->flags, SHF_USE_DMA)) {
		for (seg = 0; seg < cmd->c_dmamap->dm_nsegs; seg++) {
			bus_addr_t paddr =
			    cmd->c_dmamap->dm_segs[seg].ds_addr;
			uint16_t len =
			    cmd->c_dmamap->dm_segs[seg].ds_len == 65536 ?
			    0 : cmd->c_dmamap->dm_segs[seg].ds_len;
			uint16_t attr;

			attr = SDHC_ADMA2_VALID | SDHC_ADMA2_ACT_TRANS;
			if (seg == cmd->c_dmamap->dm_nsegs - 1)
				attr |= SDHC_ADMA2_END;

			if (ISSET(hp->flags, SHF_USE_DMA64)) {
				desc64[seg].attribute = htole16(attr);
				desc64[seg].length = htole16(len);
				desc64[seg].address_lo =
				    htole32((uint64_t)paddr & 0xffffffff);
				desc64[seg].address_hi =
				    htole32((uint64_t)paddr >> 32);
			} else {
				desc32[seg].attribute = htole16(attr);
				desc32[seg].length = htole16(len);
				desc32[seg].address = htole32(paddr);
			}
		}

		if (ISSET(hp->flags, SHF_USE_DMA64))
			desc64[cmd->c_dmamap->dm_nsegs].attribute = htole16(0);
		else
			desc32[cmd->c_dmamap->dm_nsegs].attribute = htole16(0);

		bus_dmamap_sync(sc->sc_dmat, hp->adma_map, 0, PAGE_SIZE,
		    BUS_DMASYNC_PREWRITE);

		HCLR1(hp, SDHC_HOST_CTL, SDHC_DMA_SELECT);
		if (ISSET(hp->flags, SHF_USE_DMA64))
			HSET1(hp, SDHC_HOST_CTL, SDHC_DMA_SELECT_ADMA64);
		else
			HSET1(hp, SDHC_HOST_CTL, SDHC_DMA_SELECT_ADMA32);

		HWRITE4(hp, SDHC_ADMA_SYSTEM_ADDR,
		    hp->adma_map->dm_segs[0].ds_addr);
	} else
		HCLR1(hp, SDHC_HOST_CTL, SDHC_DMA_SELECT);

	DPRINTF(1,("%s: cmd=%#x mode=%#x blksize=%d blkcount=%d\n",
	    DEVNAME(hp->sc), command, mode, blksize, blkcount));

	/* We're starting a new command, reset state. */
	hp->intr_status = 0;

	/*
	 * Start a CPU data transfer.  Writing to the high order byte
	 * of the SDHC_COMMAND register triggers the SD command. (1.5)
	 */
	HWRITE2(hp, SDHC_TRANSFER_MODE, mode);
	HWRITE2(hp, SDHC_BLOCK_SIZE, blksize);
	HWRITE2(hp, SDHC_BLOCK_COUNT, blkcount);
	HWRITE4(hp, SDHC_ARGUMENT, cmd->c_arg);
	HWRITE2(hp, SDHC_COMMAND, command);

	splx(s);
	return 0;
}

void
sdhc_transfer_data(struct sdhc_host *hp, struct sdmmc_command *cmd)
{
	struct sdhc_softc *sc = hp->sc;
	u_char *datap = cmd->c_data;
	int i, datalen;
	int mask;
	int error;

	if (cmd->c_dmamap) {
		int status;

		error = 0;
		for (;;) {
			status = sdhc_wait_intr(hp,
			    SDHC_DMA_INTERRUPT|SDHC_TRANSFER_COMPLETE,
			    SDHC_DMA_TIMEOUT);
			if (status & SDHC_TRANSFER_COMPLETE)
				break;
			if (!status) {
				error = ETIMEDOUT;
				break;
			}
		}

		bus_dmamap_sync(sc->sc_dmat, hp->adma_map, 0, PAGE_SIZE,
		    BUS_DMASYNC_POSTWRITE);
		goto done;
	}

	mask = ISSET(cmd->c_flags, SCF_CMD_READ) ?
	    SDHC_BUFFER_READ_ENABLE : SDHC_BUFFER_WRITE_ENABLE;
	error = 0;
	datalen = cmd->c_datalen;

	DPRINTF(1,("%s: resp=%#x datalen=%d\n", DEVNAME(hp->sc),
	    MMC_R1(cmd->c_resp), datalen));

#ifdef SDHC_DEBUG
	/* XXX I forgot why I wanted to know when this happens :-( */
	if ((cmd->c_opcode == 52 || cmd->c_opcode == 53) &&
	    ISSET(MMC_R1(cmd->c_resp), 0xcb00))
		printf("%s: CMD52/53 error response flags %#x\n",
		    DEVNAME(hp->sc), MMC_R1(cmd->c_resp) & 0xff00);
#endif

	while (datalen > 0) {
		if (!sdhc_wait_intr(hp, SDHC_BUFFER_READ_READY|
		    SDHC_BUFFER_WRITE_READY, SDHC_BUFFER_TIMEOUT)) {
			error = ETIMEDOUT;
			break;
		}

		if ((error = sdhc_wait_state(hp, mask, mask)) != 0)
			break;

		i = MIN(datalen, cmd->c_blklen);
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			sdhc_read_data(hp, datap, i);
		else
			sdhc_write_data(hp, datap, i);

		datap += i;
		datalen -= i;
	}

	if (error == 0 && !sdhc_wait_intr(hp, SDHC_TRANSFER_COMPLETE,
	    SDHC_TRANSFER_TIMEOUT))
		error = ETIMEDOUT;

done:
	if (error != 0)
		cmd->c_error = error;
	SET(cmd->c_flags, SCF_ITSDONE);

	DPRINTF(1,("%s: data transfer done (error=%d)\n",
	    DEVNAME(hp->sc), cmd->c_error));
}

void
sdhc_read_data(struct sdhc_host *hp, u_char *datap, int datalen)
{
	while (datalen > 3) {
		*(u_int32_t *)datap = HREAD4(hp, SDHC_DATA);
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		u_int32_t rv = HREAD4(hp, SDHC_DATA);
		do {
			*datap++ = rv & 0xff;
			rv = rv >> 8;
		} while (--datalen > 0);
	}
}

void
sdhc_write_data(struct sdhc_host *hp, u_char *datap, int datalen)
{
	while (datalen > 3) {
		DPRINTF(3,("%08x\n", *(u_int32_t *)datap));
		HWRITE4(hp, SDHC_DATA, *((u_int32_t *)datap));
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		u_int32_t rv = *datap++;
		if (datalen > 1)
			rv |= *datap++ << 8;
		if (datalen > 2)
			rv |= *datap++ << 16;
		DPRINTF(3,("rv %08x\n", rv));
		HWRITE4(hp, SDHC_DATA, rv);
	}
}

/* Prepare for another command. */
int
sdhc_soft_reset(struct sdhc_host *hp, int mask)
{
	int timo;

	DPRINTF(1,("%s: software reset reg=%#x\n", DEVNAME(hp->sc), mask));

	HWRITE1(hp, SDHC_SOFTWARE_RESET, mask);
	for (timo = 10; timo > 0; timo--) {
		if (!ISSET(HREAD1(hp, SDHC_SOFTWARE_RESET), mask))
			break;
		sdmmc_delay(10000);
		HWRITE1(hp, SDHC_SOFTWARE_RESET, 0);
	}
	if (timo == 0) {
		DPRINTF(1,("%s: timeout reg=%#x\n", DEVNAME(hp->sc),
		    HREAD1(hp, SDHC_SOFTWARE_RESET)));
		HWRITE1(hp, SDHC_SOFTWARE_RESET, 0);
		return (ETIMEDOUT);
	}

	return (0);
}

int
sdhc_wait_intr_cold(struct sdhc_host *hp, int mask, int secs)
{
	int status, usecs;

	mask |= SDHC_ERROR_INTERRUPT;
	usecs = secs * 1000000;
	status = hp->intr_status;
	while ((status & mask) == 0) {

		status = HREAD2(hp, SDHC_NINTR_STATUS);
		if (ISSET(status, SDHC_NINTR_STATUS_MASK)) {
			HWRITE2(hp, SDHC_NINTR_STATUS, status);
			if (ISSET(status, SDHC_ERROR_INTERRUPT)) {
				uint16_t error;
				error = HREAD2(hp, SDHC_EINTR_STATUS);
				HWRITE2(hp, SDHC_EINTR_STATUS, error);
				hp->intr_status |= status;

				if (ISSET(error, SDHC_CMD_TIMEOUT_ERROR|
				    SDHC_DATA_TIMEOUT_ERROR))
					break;
			}

			if (ISSET(status, SDHC_BUFFER_READ_READY |
			    SDHC_BUFFER_WRITE_READY | SDHC_COMMAND_COMPLETE |
			    SDHC_TRANSFER_COMPLETE)) {
				hp->intr_status |= status;
				break;
			}

			if (ISSET(status, SDHC_CARD_INTERRUPT)) {
				HSET2(hp, SDHC_NINTR_STATUS_EN,
				    SDHC_CARD_INTERRUPT);
			}

			continue;
		}

		delay(1);
		if (usecs-- == 0) {
			status |= SDHC_ERROR_INTERRUPT;
			break;
		}
	}

	hp->intr_status &= ~(status & mask);
	return (status & mask);
}

int
sdhc_wait_intr(struct sdhc_host *hp, int mask, int secs)
{
	int status;
	int s;

	if (cold)
		return (sdhc_wait_intr_cold(hp, mask, secs));

	mask |= SDHC_ERROR_INTERRUPT;

	s = splsdmmc();
	status = hp->intr_status & mask;
	while (status == 0) {
		if (tsleep_nsec(&hp->intr_status, PWAIT, "hcintr",
		    SEC_TO_NSEC(secs)) == EWOULDBLOCK) {
			status |= SDHC_ERROR_INTERRUPT;
			break;
		}
		status = hp->intr_status & mask;
	}
	hp->intr_status &= ~status;

	DPRINTF(2,("%s: intr status %#x error %#x\n", DEVNAME(hp->sc), status,
	    hp->intr_error_status));
	
	/* Command timeout has higher priority than command complete. */
	if (ISSET(status, SDHC_ERROR_INTERRUPT)) {
		hp->intr_error_status = 0;
		(void)sdhc_soft_reset(hp, SDHC_RESET_DAT|SDHC_RESET_CMD);
		status = 0;
	}

	splx(s);
	return status;
}

/*
 * Established by attachment driver at interrupt priority IPL_SDMMC.
 */
int
sdhc_intr(void *arg)
{
	struct sdhc_softc *sc = arg;
	int host;
	int done = 0;

	/* We got an interrupt, but we don't know from which slot. */
	for (host = 0; host < sc->sc_nhosts; host++) {
		struct sdhc_host *hp = sc->sc_host[host];
		u_int16_t status;

		if (hp == NULL)
			continue;

		/* Find out which interrupts are pending. */
		status = HREAD2(hp, SDHC_NINTR_STATUS);
		if (!ISSET(status, SDHC_NINTR_STATUS_MASK))
			continue; /* no interrupt for us */

		/* Acknowledge the interrupts we are about to handle. */
		HWRITE2(hp, SDHC_NINTR_STATUS, status);
		DPRINTF(2,("%s: interrupt status=%b\n", DEVNAME(hp->sc),
		    status, SDHC_NINTR_STATUS_BITS));

		/* Claim this interrupt. */
		done = 1;

		/*
		 * Service error interrupts.
		 */
		if (ISSET(status, SDHC_ERROR_INTERRUPT)) {
			u_int16_t error;

			/* Acknowledge error interrupts. */
			error = HREAD2(hp, SDHC_EINTR_STATUS);
			HWRITE2(hp, SDHC_EINTR_STATUS, error);
			DPRINTF(2,("%s: error interrupt, status=%b\n",
			    DEVNAME(hp->sc), error, SDHC_EINTR_STATUS_BITS));

			if (ISSET(error, SDHC_CMD_TIMEOUT_ERROR|
			    SDHC_DATA_TIMEOUT_ERROR)) {
				hp->intr_error_status |= error;
				hp->intr_status |= status;
				wakeup(&hp->intr_status);
			}
		}

		/*
		 * Wake up the sdmmc event thread to scan for cards.
		 */
		if (ISSET(status, SDHC_CARD_REMOVAL|SDHC_CARD_INSERTION))
			sdmmc_needs_discover(hp->sdmmc);

		/*
		 * Wake up the blocking process to service command
		 * related interrupt(s).
		 */
		if (ISSET(status, SDHC_BUFFER_READ_READY|
		    SDHC_BUFFER_WRITE_READY|SDHC_COMMAND_COMPLETE|
		    SDHC_TRANSFER_COMPLETE)) {
			hp->intr_status |= status;
			wakeup(&hp->intr_status);
		}

		/*
		 * Service SD card interrupts.
		 */
		if (ISSET(status, SDHC_CARD_INTERRUPT)) {
			DPRINTF(0,("%s: card interrupt\n", DEVNAME(hp->sc)));
			HCLR2(hp, SDHC_NINTR_STATUS_EN, SDHC_CARD_INTERRUPT);
			sdmmc_card_intr(hp->sdmmc);
		}
	}
	return done;
}

void
sdhc_needs_discover(struct sdhc_softc *sc)
{
	int host;

	for (host = 0; host < sc->sc_nhosts; host++)
		sdmmc_needs_discover(sc->sc_host[host]->sdmmc);
}

#ifdef SDHC_DEBUG
void
sdhc_dump_regs(struct sdhc_host *hp)
{
	printf("0x%02x PRESENT_STATE:    %b\n", SDHC_PRESENT_STATE,
	    HREAD4(hp, SDHC_PRESENT_STATE), SDHC_PRESENT_STATE_BITS);
	printf("0x%02x POWER_CTL:        %x\n", SDHC_POWER_CTL,
	    HREAD1(hp, SDHC_POWER_CTL));
	printf("0x%02x NINTR_STATUS:     %x\n", SDHC_NINTR_STATUS,
	    HREAD2(hp, SDHC_NINTR_STATUS));
	printf("0x%02x EINTR_STATUS:     %x\n", SDHC_EINTR_STATUS,
	    HREAD2(hp, SDHC_EINTR_STATUS));
	printf("0x%02x NINTR_STATUS_EN:  %x\n", SDHC_NINTR_STATUS_EN,
	    HREAD2(hp, SDHC_NINTR_STATUS_EN));
	printf("0x%02x EINTR_STATUS_EN:  %x\n", SDHC_EINTR_STATUS_EN,
	    HREAD2(hp, SDHC_EINTR_STATUS_EN));
	printf("0x%02x NINTR_SIGNAL_EN:  %x\n", SDHC_NINTR_SIGNAL_EN,
	    HREAD2(hp, SDHC_NINTR_SIGNAL_EN));
	printf("0x%02x EINTR_SIGNAL_EN:  %x\n", SDHC_EINTR_SIGNAL_EN,
	    HREAD2(hp, SDHC_EINTR_SIGNAL_EN));
	printf("0x%02x CAPABILITIES:     %x\n", SDHC_CAPABILITIES,
	    HREAD4(hp, SDHC_CAPABILITIES));
	printf("0x%02x MAX_CAPABILITIES: %x\n", SDHC_MAX_CAPABILITIES,
	    HREAD4(hp, SDHC_MAX_CAPABILITIES));
}
#endif

int
sdhc_hibernate_init(sdmmc_chipset_handle_t sch, void *fake_softc)
{
	struct sdhc_host *hp, *fhp;
	fhp = fake_softc;
	hp = sch;
	*fhp = *hp;

	return (0);
}
