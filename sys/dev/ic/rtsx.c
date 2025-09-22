/*	$OpenBSD: rtsx.c,v 1.22 2020/02/18 00:06:56 cheloha Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
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
 * Realtek RTS52xx/RTL84xx Card Reader driver.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <dev/ic/rtsxreg.h>
#include <dev/ic/rtsxvar.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmc_ioreg.h>

/* 
 * We use three DMA buffers: a command buffer, a data buffer, and a buffer for
 * ADMA transfer descriptors which describe scatter-gather (SG) I/O operations.
 *
 * The command buffer contains a command queue for the host controller,
 * which describes SD/MMC commands to run, and other parameters. The chip
 * runs the command queue when a special bit in the RTSX_HCBAR register is
 * set and signals completion with the TRANS_OK interrupt.
 * Each command is encoded as a 4 byte sequence containing command number
 * (read, write, or check a host controller register), a register address,
 * and a data bit-mask and value.
 * SD/MMC commands which do not transfer any data from/to the card only use
 * the command buffer.
 *
 * The smmmc stack provides DMA-safe buffers with data transfer commands.
 * In this case we write a list of descriptors to the ADMA descriptor buffer,
 * instructing the chip to transfer data directly from/to sdmmc DMA buffers.
 *
 * However, some sdmmc commands used during card initialization also carry
 * data, and these don't come with DMA-safe buffers. In this case, we transfer
 * data from/to the SD card via a DMA data bounce buffer.
 *
 * In both cases, data transfer is controlled via the RTSX_HDBAR register
 * and completion is signalled by the TRANS_OK interrupt.
 *
 * The chip is unable to perform DMA above 4GB.
 */

#define	RTSX_DMA_MAX_SEGSIZE	0x80000
#define	RTSX_HOSTCMD_MAX	256
#define	RTSX_HOSTCMD_BUFSIZE	(sizeof(u_int32_t) * RTSX_HOSTCMD_MAX)
#define	RTSX_DMA_DATA_BUFSIZE	MAXPHYS
#define	RTSX_ADMA_DESC_SIZE	(sizeof(uint64_t) * SDMMC_MAXNSEGS)

#define READ4(sc, reg)							\
	(bus_space_read_4((sc)->iot, (sc)->ioh, (reg)))
#define WRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->iot, (sc)->ioh, (reg), (val))

#define	RTSX_READ(sc, reg, val) 				\
	do { 							\
		int err = rtsx_read((sc), (reg), (val)); 	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_WRITE(sc, reg, val) 				\
	do { 							\
		int err = rtsx_write((sc), (reg), 0xff, (val));	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_CLR(sc, reg, bits)					\
	do { 							\
		int err = rtsx_write((sc), (reg), (bits), 0); 	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_SET(sc, reg, bits)					\
	do { 							\
		int err = rtsx_write((sc), (reg), (bits), 0xff);\
		if (err) 					\
			return (err);				\
	} while (0)

int	rtsx_host_reset(sdmmc_chipset_handle_t);
u_int32_t rtsx_host_ocr(sdmmc_chipset_handle_t);
int	rtsx_host_maxblklen(sdmmc_chipset_handle_t);
int	rtsx_card_detect(sdmmc_chipset_handle_t);
int	rtsx_bus_power(sdmmc_chipset_handle_t, u_int32_t);
int	rtsx_bus_clock(sdmmc_chipset_handle_t, int, int);
int	rtsx_bus_width(sdmmc_chipset_handle_t, int);
void	rtsx_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
int	rtsx_init(struct rtsx_softc *, int);
void	rtsx_soft_reset(struct rtsx_softc *);
int	rtsx_bus_power_off(struct rtsx_softc *);
int	rtsx_bus_power_on(struct rtsx_softc *);
int	rtsx_set_bus_width(struct rtsx_softc *, int);
int	rtsx_stop_sd_clock(struct rtsx_softc *);
int	rtsx_switch_sd_clock(struct rtsx_softc *, u_int8_t, int, int);
int	rtsx_wait_intr(struct rtsx_softc *, int, int);
int	rtsx_read(struct rtsx_softc *, u_int16_t, u_int8_t *);
int	rtsx_write(struct rtsx_softc *, u_int16_t, u_int8_t, u_int8_t);
#ifdef notyet
int	rtsx_read_phy(struct rtsx_softc *, u_int8_t, u_int16_t *);
#endif
int	rtsx_write_phy(struct rtsx_softc *, u_int8_t, u_int16_t);
int	rtsx_read_cfg(struct rtsx_softc *, u_int8_t, u_int16_t, u_int32_t *);
#ifdef notyet
int	rtsx_write_cfg(struct rtsx_softc *, u_int8_t, u_int16_t, u_int32_t,
		u_int32_t);
#endif
void	rtsx_hostcmd(u_int32_t *, int *, u_int8_t, u_int16_t, u_int8_t,
		u_int8_t);
int	rtsx_hostcmd_send(struct rtsx_softc *, int);
u_int8_t rtsx_response_type(u_int16_t);
int	rtsx_xfer_exec(struct rtsx_softc *, bus_dmamap_t, int);
int	rtsx_xfer(struct rtsx_softc *, struct sdmmc_command *, u_int32_t *);
int	rtsx_xfer_bounce(struct rtsx_softc *, struct sdmmc_command *);
int	rtsx_xfer_adma(struct rtsx_softc *, struct sdmmc_command *);
void	rtsx_card_insert(struct rtsx_softc *);
void	rtsx_card_eject(struct rtsx_softc *);
int	rtsx_led_enable(struct rtsx_softc *);
int	rtsx_led_disable(struct rtsx_softc *);
void	rtsx_save_regs(struct rtsx_softc *);
void	rtsx_restore_regs(struct rtsx_softc *);

#ifdef RTSX_DEBUG
int rtsxdebug = 0;
#define DPRINTF(n,s)	do { if ((n) <= rtsxdebug) printf s; } while (0)
#else
#define DPRINTF(n,s)	do {} while(0)
#endif

struct sdmmc_chip_functions rtsx_functions = {
	/* host controller reset */
	rtsx_host_reset,
	/* host controller capabilities */
	rtsx_host_ocr,
	rtsx_host_maxblklen,
	/* card detection */
	rtsx_card_detect,
	/* bus power and clock frequency */
	rtsx_bus_power,
	rtsx_bus_clock,
	rtsx_bus_width,
	/* command execution */
	rtsx_exec_command,
	/* card interrupt */
	NULL, NULL
};

struct cfdriver rtsx_cd = {
	NULL, "rtsx", DV_DULL
};

/*
 * Called by attachment driver.
 */
int
rtsx_attach(struct rtsx_softc *sc, bus_space_tag_t iot,
    bus_space_handle_t ioh, bus_size_t iosize, bus_dma_tag_t dmat, int flags)
{
	struct sdmmcbus_attach_args saa;
	u_int32_t sdio_cfg;
	int rsegs;

	sc->iot = iot;
	sc->ioh = ioh;
	sc->dmat = dmat;
	sc->flags = flags;

	if (rtsx_init(sc, 1))
		return 1;

	if (rtsx_read_cfg(sc, 0, RTSX_SDIOCFG_REG, &sdio_cfg) == 0) {
		if ((sdio_cfg & RTSX_SDIOCFG_SDIO_ONLY) ||
		    (sdio_cfg & RTSX_SDIOCFG_HAVE_SDIO))
			sc->flags |= RTSX_F_SDIO_SUPPORT;
	}

	if (bus_dmamap_create(sc->dmat, RTSX_HOSTCMD_BUFSIZE, 1,
	    RTSX_DMA_MAX_SEGSIZE, 0, BUS_DMA_NOWAIT,
	    &sc->dmap_cmd) != 0)
		return 1;
	if (bus_dmamap_create(sc->dmat, RTSX_DMA_DATA_BUFSIZE, 1,
	    RTSX_DMA_MAX_SEGSIZE, 0, BUS_DMA_NOWAIT,
	    &sc->dmap_data) != 0)
	    	goto destroy_cmd;
	if (bus_dmamap_create(sc->dmat, RTSX_ADMA_DESC_SIZE, 1,
	    RTSX_DMA_MAX_SEGSIZE, 0, BUS_DMA_NOWAIT,
	    &sc->dmap_adma) != 0)
	    	goto destroy_data;
	if (bus_dmamem_alloc(sc->dmat, RTSX_ADMA_DESC_SIZE, 0, 0,
	    sc->adma_segs, 1, &rsegs, BUS_DMA_WAITOK|BUS_DMA_ZERO))
	    	goto destroy_adma;
	if (bus_dmamem_map(sc->dmat, sc->adma_segs, rsegs, RTSX_ADMA_DESC_SIZE,
	    &sc->admabuf, BUS_DMA_WAITOK|BUS_DMA_COHERENT))
	    	goto free_adma;

	/*
	 * Attach the generic SD/MMC bus driver.  (The bus driver must
	 * not invoke any chipset functions before it is attached.)
	 */
	bzero(&saa, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &rtsx_functions;
	saa.sch = sc;
	saa.flags = SMF_STOP_AFTER_MULTIPLE;
	saa.caps = SMC_CAPS_4BIT_MODE | SMC_CAPS_DMA;
	saa.dmat = sc->dmat;

	sc->sdmmc = config_found(&sc->sc_dev, &saa, NULL);
	if (sc->sdmmc == NULL)
		goto unmap_adma;

	/* Now handle cards discovered during attachment. */
	if (ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		rtsx_card_insert(sc);
	
	return 0;

unmap_adma:
	bus_dmamem_unmap(sc->dmat, sc->admabuf, RTSX_ADMA_DESC_SIZE);
free_adma:
	bus_dmamem_free(sc->dmat, sc->adma_segs, rsegs);
destroy_adma:
	bus_dmamap_destroy(sc->dmat, sc->dmap_adma);
destroy_data:
	bus_dmamap_destroy(sc->dmat, sc->dmap_data);
destroy_cmd:
	bus_dmamap_destroy(sc->dmat, sc->dmap_cmd);
	return 1;
}

int
rtsx_init(struct rtsx_softc *sc, int attaching)
{
	u_int32_t status;
	u_int8_t version;
	int error;

	/* Read IC version from dummy register. */
	if (sc->flags & RTSX_F_5229) {
		RTSX_READ(sc, RTSX_DUMMY_REG, &version);
		switch (version & 0x0F) {
		case RTSX_IC_VERSION_A:
		case RTSX_IC_VERSION_B:
		case RTSX_IC_VERSION_D:
			break;
		case RTSX_IC_VERSION_C:
			sc->flags |= RTSX_F_5229_TYPE_C;
			break;
		default:
			printf("rtsx_init: unknown ic %02x\n", version);
			return (1);
		}
	}

	/* Enable interrupt write-clear (default is read-clear). */
	RTSX_CLR(sc, RTSX_NFTS_TX_CTRL, RTSX_INT_READ_CLR);

	/* Clear any pending interrupts. */
	status = READ4(sc, RTSX_BIPR);
	WRITE4(sc, RTSX_BIPR, status);

	/* Check for cards already inserted at attach time. */
	if (attaching && (status & RTSX_SD_EXIST))
		sc->flags |= RTSX_F_CARD_PRESENT;

	/* Enable interrupts. */
	WRITE4(sc, RTSX_BIER,
	    RTSX_TRANS_OK_INT_EN | RTSX_TRANS_FAIL_INT_EN | RTSX_SD_INT_EN);

	/* Power on SSC clock. */
	RTSX_CLR(sc, RTSX_FPDCTL, RTSX_SSC_POWER_DOWN);
	delay(200);

	/* XXX magic numbers from linux driver */
	if (sc->flags & RTSX_F_5209)
		error = rtsx_write_phy(sc, 0x00, 0xB966);
	else
		error = rtsx_write_phy(sc, 0x00, 0xBA42);
	if (error) {
		printf("%s: cannot write phy register\n", DEVNAME(sc));
		return (1);
	}

	RTSX_SET(sc, RTSX_CLK_DIV, 0x07);

	/* Disable sleep mode. */
	RTSX_CLR(sc, RTSX_HOST_SLEEP_STATE,
	    RTSX_HOST_ENTER_S1 | RTSX_HOST_ENTER_S3);

	/* Disable card clock. */
	RTSX_CLR(sc, RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL);

	RTSX_CLR(sc, RTSX_CHANGE_LINK_STATE,
	    RTSX_FORCE_RST_CORE_EN | RTSX_NON_STICKY_RST_N_DBG | 0x04);
	RTSX_WRITE(sc, RTSX_SD30_DRIVE_SEL, RTSX_SD30_DRIVE_SEL_3V3);

	/* Enable SSC clock. */
	RTSX_WRITE(sc, RTSX_SSC_CTL1, RTSX_SSC_8X_EN | RTSX_SSC_SEL_4M);
	RTSX_WRITE(sc, RTSX_SSC_CTL2, 0x12);

	RTSX_SET(sc, RTSX_CHANGE_LINK_STATE, RTSX_MAC_PHY_RST_N_DBG);
	RTSX_SET(sc, RTSX_IRQSTAT0, RTSX_LINK_READY_INT);

	RTSX_WRITE(sc, RTSX_PERST_GLITCH_WIDTH, 0x80);

	/* Set RC oscillator to 400K. */
	RTSX_CLR(sc, RTSX_RCCTL, RTSX_RCCTL_F_2M);

	/* Request clock by driving CLKREQ pin to zero. */
	RTSX_SET(sc, RTSX_PETXCFG, RTSX_PETXCFG_CLKREQ_PIN);

	/* Set up LED GPIO. */
	if (sc->flags & RTSX_F_5209) {
		RTSX_WRITE(sc, RTSX_CARD_GPIO, 0x03);
		RTSX_WRITE(sc, RTSX_CARD_GPIO_DIR, 0x03);
	} else {
		RTSX_SET(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
		/* Switch LDO3318 source from DV33 to 3V3. */
		RTSX_CLR(sc, RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_DV33);
		RTSX_SET(sc, RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_3V3);
		/* Set default OLT blink period. */
		RTSX_SET(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_PERIOD);
	}

	return (0);
}

int
rtsx_activate(struct device *self, int act)
{
	struct rtsx_softc *sc = (struct rtsx_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		rtsx_save_regs(sc);
		break;
	case DVACT_RESUME:
		rtsx_restore_regs(sc);

		/* Handle cards ejected/inserted during suspend. */
		if (READ4(sc, RTSX_BIPR) & RTSX_SD_EXIST)
			rtsx_card_insert(sc);
		else
			rtsx_card_eject(sc);

		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
rtsx_led_enable(struct rtsx_softc *sc)
{
	if (sc->flags & RTSX_F_5209) {
		RTSX_CLR(sc, RTSX_CARD_GPIO, RTSX_CARD_GPIO_LED_OFF);
		RTSX_WRITE(sc, RTSX_CARD_AUTO_BLINK,
		    RTSX_LED_BLINK_EN | RTSX_LED_BLINK_SPEED);
	} else {
		RTSX_SET(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
		RTSX_SET(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_AUTOBLINK);
	}

	return 0;
}

int
rtsx_led_disable(struct rtsx_softc *sc)
{
	if (sc->flags & RTSX_F_5209) {
		RTSX_CLR(sc, RTSX_CARD_AUTO_BLINK, RTSX_LED_BLINK_EN);
		RTSX_WRITE(sc, RTSX_CARD_GPIO, RTSX_CARD_GPIO_LED_OFF);
	} else {
		RTSX_CLR(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_AUTOBLINK);
		RTSX_CLR(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
	}

	return 0;
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int
rtsx_host_reset(sdmmc_chipset_handle_t sch)
{
	struct rtsx_softc *sc = sch;
	int s;

	DPRINTF(1,("%s: host reset\n", DEVNAME(sc)));

	s = splsdmmc();

	if (ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		rtsx_soft_reset(sc);

	if (rtsx_init(sc, 0)) {
		splx(s);
		return 1;
	}

	splx(s);
	return 0;
}

u_int32_t
rtsx_host_ocr(sdmmc_chipset_handle_t sch)
{
	return RTSX_SUPPORT_VOLTAGE;
}

int
rtsx_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	return 512;
}

/*
 * Return non-zero if the card is currently inserted.
 */
int
rtsx_card_detect(sdmmc_chipset_handle_t sch)
{
	struct rtsx_softc *sc = sch;

	return ISSET(sc->flags, RTSX_F_CARD_PRESENT);
}

/*
 * Notice that the meaning of RTSX_PWR_GATE_CTRL changes between RTS5209 and
 * RTS5229. In RTS5209 it is a mask of disabled power gates, while in RTS5229
 * it is a mask of *enabled* gates.
 */

int
rtsx_bus_power_off(struct rtsx_softc *sc)
{
	int error;
	u_int8_t disable3;

	error = rtsx_stop_sd_clock(sc);
	if (error)
		return error;

	/* Disable SD output. */
	RTSX_CLR(sc, RTSX_CARD_OE, RTSX_CARD_OUTPUT_EN);

	/* Turn off power. */
	disable3 = RTSX_PULL_CTL_DISABLE3;
	if (sc->flags & RTSX_F_5209)
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF);
	else {
		RTSX_CLR(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC1 |
		    RTSX_LDO3318_VCC2);
		if (sc->flags & RTSX_F_5229_TYPE_C)
			disable3 = RTSX_PULL_CTL_DISABLE3_TYPE_C;
	}

	RTSX_SET(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF);
	RTSX_CLR(sc, RTSX_CARD_PWR_CTL, RTSX_PMOS_STRG_800mA);

	/* Disable pull control. */
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL1, RTSX_PULL_CTL_DISABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL2, RTSX_PULL_CTL_DISABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL3, disable3);

	return 0;
}

int
rtsx_bus_power_on(struct rtsx_softc *sc)
{
	u_int8_t enable3;
	int err;

	if (sc->flags & RTSX_F_525A) {
		err = rtsx_write(sc, RTSX_LDO_VCC_CFG1, RTSX_LDO_VCC_TUNE_MASK,
		    RTSX_LDO_VCC_3V3);
		if (err)
			return (err);
	}

	/* Select SD card. */
	RTSX_WRITE(sc, RTSX_CARD_SELECT, RTSX_SD_MOD_SEL);
	RTSX_WRITE(sc, RTSX_CARD_SHARE_MODE, RTSX_CARD_SHARE_48_SD);
	RTSX_SET(sc, RTSX_CARD_CLK_EN, RTSX_SD_CLK_EN);

	/* Enable pull control. */
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL1, RTSX_PULL_CTL_ENABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL2, RTSX_PULL_CTL_ENABLE12);
	if (sc->flags & RTSX_F_5229_TYPE_C)
		enable3 = RTSX_PULL_CTL_ENABLE3_TYPE_C;
	else
		enable3 = RTSX_PULL_CTL_ENABLE3;
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL3, enable3);

	/*
	 * To avoid a current peak, enable card power in two phases with a
	 * delay in between.
	 */

	/* Partial power. */
	RTSX_SET(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PARTIAL_PWR_ON);
	if (sc->flags & RTSX_F_5209)
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_SUSPEND);
	else
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC1);

	delay(200);

	/* Full power. */
	RTSX_CLR(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF);
	if (sc->flags & RTSX_F_5209)
		RTSX_CLR(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF);
	else
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC2);

	/* Enable SD card output. */
	RTSX_WRITE(sc, RTSX_CARD_OE, RTSX_SD_OUTPUT_EN);

	return 0;
}

int
rtsx_set_bus_width(struct rtsx_softc *sc, int w)
{
	u_int32_t bus_width;
	int error;

	switch (w) {
		case 8:
			bus_width = RTSX_BUS_WIDTH_8;
			break;
		case 4:
			bus_width = RTSX_BUS_WIDTH_4;
			break;
		case 1:
		default:
			bus_width = RTSX_BUS_WIDTH_1;
			break;
	}

	error = rtsx_write(sc, RTSX_SD_CFG1, RTSX_BUS_WIDTH_MASK, bus_width);
	return error;
}

int
rtsx_stop_sd_clock(struct rtsx_softc *sc)
{
	RTSX_CLR(sc, RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL);
	RTSX_SET(sc, RTSX_SD_BUS_STAT, RTSX_SD_CLK_FORCE_STOP);

	return 0;
}

int
rtsx_switch_sd_clock(struct rtsx_softc *sc, u_int8_t n, int div, int mcu)
{
	/* Enable SD 2.0 mode. */
	RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_SD_MODE_MASK);

	RTSX_SET(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);

	RTSX_WRITE(sc, RTSX_CARD_CLK_SOURCE,
	    RTSX_CRC_FIX_CLK | RTSX_SD30_VAR_CLK0 | RTSX_SAMPLE_VAR_CLK1);
	RTSX_CLR(sc, RTSX_SD_SAMPLE_POINT_CTL, RTSX_SD20_RX_SEL_MASK);
	RTSX_WRITE(sc, RTSX_SD_PUSH_POINT_CTL, RTSX_SD20_TX_NEG_EDGE);
	RTSX_WRITE(sc, RTSX_CLK_DIV, (div << 4) | mcu);
	RTSX_CLR(sc, RTSX_SSC_CTL1, RTSX_RSTB);
	RTSX_CLR(sc, RTSX_SSC_CTL2, RTSX_SSC_DEPTH_MASK);
	RTSX_WRITE(sc, RTSX_SSC_DIV_N_0, n);
	RTSX_SET(sc, RTSX_SSC_CTL1, RTSX_RSTB);
	delay(100);

	RTSX_CLR(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);

	return 0;
}

/*
 * Set or change SD bus voltage and enable or disable SD bus power.
 * Return zero on success.
 */
int
rtsx_bus_power(sdmmc_chipset_handle_t sch, u_int32_t ocr)
{
	struct rtsx_softc *sc = sch;
	int s, error = 0;

	DPRINTF(1,("%s: voltage change ocr=0x%x\n", DEVNAME(sc), ocr));

	s = splsdmmc();

	/*
	 * Disable bus power before voltage change.
	 */
	error = rtsx_bus_power_off(sc);
	if (error)
		goto ret;

	delay(200);

	/* If power is disabled, reset the host and return now. */
	if (ocr == 0) {
		splx(s);
		(void)rtsx_host_reset(sc);
		return 0;
	}

	if (!ISSET(ocr, RTSX_SUPPORT_VOLTAGE)) {
		/* Unsupported voltage level requested. */
		DPRINTF(1,("%s: unsupported voltage ocr=0x%x\n",
		    DEVNAME(sc), ocr));
		error = EINVAL;
		goto ret;
	}

	error = rtsx_bus_power_on(sc);
	if (error)
		goto ret;

	error = rtsx_set_bus_width(sc, 1);
ret:
	splx(s);
	return error;
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int
rtsx_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct rtsx_softc *sc = sch;
	int s;
	u_int8_t n;
	int div;
	int mcu;
	int error = 0;

	s = splsdmmc();

	if (freq == SDMMC_SDCLK_OFF) {
		error = rtsx_stop_sd_clock(sc);
		goto ret;
	}

	/* Round down to a supported frequency. */
	if (freq >= SDMMC_SDCLK_50MHZ)
		freq = SDMMC_SDCLK_50MHZ;
	else if (freq >= SDMMC_SDCLK_25MHZ)
		freq = SDMMC_SDCLK_25MHZ;
	else
		freq = SDMMC_SDCLK_400KHZ;

	/*
	 * Configure the clock frequency.
	 */
	switch (freq) {
	case SDMMC_SDCLK_400KHZ:
		n = 80; /* minimum */
		div = RTSX_CLK_DIV_8;
		mcu = 7;
		RTSX_SET(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_128);
		break;
	case SDMMC_SDCLK_25MHZ:
		n = 100;
		div = RTSX_CLK_DIV_4;
		mcu = 7;
		RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK);
		break;
	case SDMMC_SDCLK_50MHZ:
		n = 100;
		div = RTSX_CLK_DIV_2;
		mcu = 7;
		RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK);
		break;
	default:
		error = EINVAL;
		goto ret;
	}

	/*
	 * Enable SD clock.
	 */
	error = rtsx_switch_sd_clock(sc, n, div, mcu);
ret:
	splx(s);
	return error;
}

int
rtsx_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct rtsx_softc *sc = sch;

	return rtsx_set_bus_width(sc, width);
}

int
rtsx_read(struct rtsx_softc *sc, u_int16_t addr, u_int8_t *val)
{
	int tries = 1024;
	u_int32_t reg;
	
	WRITE4(sc, RTSX_HAIMR, RTSX_HAIMR_BUSY |
	    (u_int32_t)((addr & 0x3FFF) << 16));

	while (tries--) {
		reg = READ4(sc, RTSX_HAIMR);
		if (!(reg & RTSX_HAIMR_BUSY))
			break;
	}

	*val = (reg & 0xff);
	return (tries == 0) ? ETIMEDOUT : 0;
}

int
rtsx_write(struct rtsx_softc *sc, u_int16_t addr, u_int8_t mask, u_int8_t val)
{
	int tries = 1024;
	u_int32_t reg;

	WRITE4(sc, RTSX_HAIMR,
	    RTSX_HAIMR_BUSY | RTSX_HAIMR_WRITE |
	    (u_int32_t)(((addr & 0x3FFF) << 16) |
	    (mask << 8) | val));

	while (tries--) {
		reg = READ4(sc, RTSX_HAIMR);
		if (!(reg & RTSX_HAIMR_BUSY)) {
			if (val != (reg & 0xff))
				return EIO;
			return 0;
		}
	}

	return ETIMEDOUT;
}

#ifdef notyet
int
rtsx_read_phy(struct rtsx_softc *sc, u_int8_t addr, u_int16_t *val)
{
	int timeout = 100000;
	u_int8_t data0;
	u_int8_t data1;
	u_int8_t rwctl;

	RTSX_WRITE(sc, RTSX_PHY_ADDR, addr);
	RTSX_WRITE(sc, RTSX_PHY_RWCTL, RTSX_PHY_BUSY|RTSX_PHY_READ);

	while (timeout--) {
		RTSX_READ(sc, RTSX_PHY_RWCTL, &rwctl);
		if (!(rwctl & RTSX_PHY_BUSY))
			break;
	}
	
	if (timeout == 0)
		return ETIMEDOUT;
		
	RTSX_READ(sc, RTSX_PHY_DATA0, &data0);
	RTSX_READ(sc, RTSX_PHY_DATA1, &data1);
	*val = data0 | (data1 << 8);

	return 0;
}
#endif

int
rtsx_write_phy(struct rtsx_softc *sc, u_int8_t addr, u_int16_t val)
{
	int timeout = 100000;
	u_int8_t rwctl;

	RTSX_WRITE(sc, RTSX_PHY_DATA0, val);
	RTSX_WRITE(sc, RTSX_PHY_DATA1, val >> 8);
	RTSX_WRITE(sc, RTSX_PHY_ADDR, addr);
	RTSX_WRITE(sc, RTSX_PHY_RWCTL, RTSX_PHY_BUSY|RTSX_PHY_WRITE);

	while (timeout--) {
		RTSX_READ(sc, RTSX_PHY_RWCTL, &rwctl);
		if (!(rwctl & RTSX_PHY_BUSY))
			break;
	}
	
	if (timeout == 0)
		return ETIMEDOUT;
		
	return 0;
}

int
rtsx_read_cfg(struct rtsx_softc *sc, u_int8_t func, u_int16_t addr,
    u_int32_t *val)
{
	int tries = 1024;
	u_int8_t data0, data1, data2, data3, rwctl;

	RTSX_WRITE(sc, RTSX_CFGADDR0, addr);
	RTSX_WRITE(sc, RTSX_CFGADDR1, addr >> 8);
	RTSX_WRITE(sc, RTSX_CFGRWCTL, RTSX_CFG_BUSY | (func & 0x03 << 4));

	while (tries--) {
		RTSX_READ(sc, RTSX_CFGRWCTL, &rwctl);
		if (!(rwctl & RTSX_CFG_BUSY))
			break;
	}

	if (tries == 0)
		return EIO;
	
	RTSX_READ(sc, RTSX_CFGDATA0, &data0);
	RTSX_READ(sc, RTSX_CFGDATA1, &data1);
	RTSX_READ(sc, RTSX_CFGDATA2, &data2);
	RTSX_READ(sc, RTSX_CFGDATA3, &data3);

	*val = (data3 << 24) | (data2 << 16) | (data1 << 8) | data0;

	return 0;
}

#ifdef notyet
int
rtsx_write_cfg(struct rtsx_softc *sc, u_int8_t func, u_int16_t addr,
    u_int32_t mask, u_int32_t val)
{
	int i, writemask = 0, tries = 1024;
	u_int8_t rwctl;

	for (i = 0; i < 4; i++) {
		if (mask & 0xff) {
			RTSX_WRITE(sc, RTSX_CFGDATA0 + i, val & mask & 0xff);
			writemask |= (1 << i);
		}
		mask >>= 8;
		val >>= 8;
	}

	if (writemask) {
		RTSX_WRITE(sc, RTSX_CFGADDR0, addr);
		RTSX_WRITE(sc, RTSX_CFGADDR1, addr >> 8);
		RTSX_WRITE(sc, RTSX_CFGRWCTL,
		    RTSX_CFG_BUSY | writemask | (func & 0x03 << 4));
	}

	while (tries--) {
		RTSX_READ(sc, RTSX_CFGRWCTL, &rwctl);
		if (!(rwctl & RTSX_CFG_BUSY))
			break;
	}

	if (tries == 0)
		return EIO;
	
	return 0;
}
#endif

/* Append a properly encoded host command to the host command buffer. */
void
rtsx_hostcmd(u_int32_t *cmdbuf, int *n, u_int8_t cmd, u_int16_t reg,
    u_int8_t mask, u_int8_t data)
{
	KASSERT(*n < RTSX_HOSTCMD_MAX);

	cmdbuf[(*n)++] = htole32((u_int32_t)(cmd & 0x3) << 30) |
	    ((u_int32_t)(reg & 0x3fff) << 16) |
	    ((u_int32_t)(mask) << 8) |
	    ((u_int32_t)data);
}

void
rtsx_save_regs(struct rtsx_softc *sc)
{
	int s, i;
	u_int16_t reg;

	s = splsdmmc();

	i = 0;
	for (reg = 0xFDA0; reg < 0xFDAE; reg++)
		(void)rtsx_read(sc, reg, &sc->regs[i++]);
	for (reg = 0xFD52; reg < 0xFD69; reg++)
		(void)rtsx_read(sc, reg, &sc->regs[i++]);
	for (reg = 0xFE20; reg < 0xFE34; reg++)
		(void)rtsx_read(sc, reg, &sc->regs[i++]);

	sc->regs4[0] = READ4(sc, RTSX_HCBAR);
	sc->regs4[1] = READ4(sc, RTSX_HCBCTLR);
	sc->regs4[2] = READ4(sc, RTSX_HDBAR);
	sc->regs4[3] = READ4(sc, RTSX_HDBCTLR);
	sc->regs4[4] = READ4(sc, RTSX_HAIMR);
	sc->regs4[5] = READ4(sc, RTSX_BIER);
	/* Not saving RTSX_BIPR. */

	splx(s);
}

void
rtsx_restore_regs(struct rtsx_softc *sc)
{
	int s, i;
	u_int16_t reg;

	s = splsdmmc();

	WRITE4(sc, RTSX_HCBAR, sc->regs4[0]);
	WRITE4(sc, RTSX_HCBCTLR, sc->regs4[1]);
	WRITE4(sc, RTSX_HDBAR, sc->regs4[2]);
	WRITE4(sc, RTSX_HDBCTLR, sc->regs4[3]);
	WRITE4(sc, RTSX_HAIMR, sc->regs4[4]);
	WRITE4(sc, RTSX_BIER, sc->regs4[5]);
	/* Not writing RTSX_BIPR since doing so would clear it. */

	i = 0;
	for (reg = 0xFDA0; reg < 0xFDAE; reg++)
		(void)rtsx_write(sc, reg, 0xff, sc->regs[i++]);
	for (reg = 0xFD52; reg < 0xFD69; reg++)
		(void)rtsx_write(sc, reg, 0xff, sc->regs[i++]);
	for (reg = 0xFE20; reg < 0xFE34; reg++)
		(void)rtsx_write(sc, reg, 0xff, sc->regs[i++]);

	splx(s);
}

u_int8_t
rtsx_response_type(u_int16_t sdmmc_rsp)
{
	int i;
	struct rsp_type {
		u_int16_t sdmmc_rsp;
		u_int8_t rtsx_rsp;
	} rsp_types[] = {
		{ SCF_RSP_R0,	RTSX_SD_RSP_TYPE_R0 },
		{ SCF_RSP_R1,	RTSX_SD_RSP_TYPE_R1 },
		{ SCF_RSP_R1B,	RTSX_SD_RSP_TYPE_R1B },
		{ SCF_RSP_R2,	RTSX_SD_RSP_TYPE_R2 },
		{ SCF_RSP_R3,	RTSX_SD_RSP_TYPE_R3 },
		{ SCF_RSP_R4,	RTSX_SD_RSP_TYPE_R4 },
		{ SCF_RSP_R5,	RTSX_SD_RSP_TYPE_R5 },
		{ SCF_RSP_R6,	RTSX_SD_RSP_TYPE_R6 },
		{ SCF_RSP_R7,	RTSX_SD_RSP_TYPE_R7 }
	};

	for (i = 0; i < nitems(rsp_types); i++) {
		if (sdmmc_rsp == rsp_types[i].sdmmc_rsp)
			return rsp_types[i].rtsx_rsp;
	}

	return 0;
}

int
rtsx_hostcmd_send(struct rtsx_softc *sc, int ncmd)
{
	int s;

	s = splsdmmc();

	/* Tell the chip where the command buffer is and run the commands. */
	WRITE4(sc, RTSX_HCBAR, sc->dmap_cmd->dm_segs[0].ds_addr);
	WRITE4(sc, RTSX_HCBCTLR,
	    ((ncmd * 4) & 0x00ffffff) | RTSX_START_CMD | RTSX_HW_AUTO_RSP);

	splx(s);

	return 0;
}

int
rtsx_xfer_exec(struct rtsx_softc *sc, bus_dmamap_t dmap, int dmaflags)
{
	int s = splsdmmc();

	/* Tell the chip where the data buffer is and run the transfer. */
	WRITE4(sc, RTSX_HDBAR, dmap->dm_segs[0].ds_addr);
	WRITE4(sc, RTSX_HDBCTLR, dmaflags);

	splx(s);

	/* Wait for completion. */
	return rtsx_wait_intr(sc, RTSX_TRANS_OK_INT, 10);
}

int
rtsx_xfer(struct rtsx_softc *sc, struct sdmmc_command *cmd, u_int32_t *cmdbuf)
{
	int ncmd, dma_dir, error, tmode;
	int read = ISSET(cmd->c_flags, SCF_CMD_READ);
	u_int8_t cfg2;

	DPRINTF(3,("%s: %s xfer: %d bytes with block size %d\n", DEVNAME(sc),
	    read ? "read" : "write",
	    cmd->c_datalen, cmd->c_blklen));

	if (cmd->c_datalen > RTSX_DMA_DATA_BUFSIZE) {
		DPRINTF(3, ("%s: cmd->c_datalen too large: %d > %d\n",
		    DEVNAME(sc), cmd->c_datalen, RTSX_DMA_DATA_BUFSIZE));
		return ENOMEM;
	}

	/* Configure DMA transfer mode parameters. */
	cfg2 = RTSX_SD_NO_CHECK_WAIT_CRC_TO | RTSX_SD_CHECK_CRC16 |
	    RTSX_SD_NO_WAIT_BUSY_END | RTSX_SD_RSP_LEN_0;
	if (read) {
		dma_dir = RTSX_DMA_DIR_FROM_CARD;
		/* Use transfer mode AUTO_READ3, which assumes we've already
		 * sent the read command and gotten the response, and will
		 * send CMD 12 manually after reading multiple blocks. */
		tmode = RTSX_TM_AUTO_READ3;
		cfg2 |= RTSX_SD_CALCULATE_CRC7 | RTSX_SD_CHECK_CRC7;
	} else {
		dma_dir = RTSX_DMA_DIR_TO_CARD;
		/* Use transfer mode AUTO_WRITE3, which assumes we've already
		 * sent the write command and gotten the response, and will
		 * send CMD 12 manually after writing multiple blocks. */
		tmode = RTSX_TM_AUTO_WRITE3;
		cfg2 |= RTSX_SD_NO_CALCULATE_CRC7 | RTSX_SD_NO_CHECK_CRC7;
	}

	ncmd = 0;

	rtsx_hostcmd(cmdbuf, &ncmd, RTSX_WRITE_REG_CMD, RTSX_SD_CFG2,
	    0xff, cfg2); 

	/* Queue commands to configure data transfer size. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_L, 0xff,
	    (cmd->c_blklen & 0xff));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_H, 0xff,
	    (cmd->c_blklen >> 8));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_L, 0xff,
	    ((cmd->c_datalen / cmd->c_blklen) & 0xff));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_H, 0xff,
	    ((cmd->c_datalen / cmd->c_blklen) >> 8));

	/* Use the DMA ring buffer for commands which transfer data. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE, 0x01, RTSX_RING_BUFFER);

	/* Configure DMA controller. */
	rtsx_hostcmd(cmdbuf, &ncmd, RTSX_WRITE_REG_CMD, RTSX_IRQSTAT0,
	    RTSX_DMA_DONE_INT, RTSX_DMA_DONE_INT);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC3, 0xff, cmd->c_datalen >> 24);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC2, 0xff, cmd->c_datalen >> 16);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC1, 0xff, cmd->c_datalen >> 8);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC0, 0xff, cmd->c_datalen);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMACTL,
	    0x03 | RTSX_DMA_PACK_SIZE_MASK,
	    dma_dir | RTSX_DMA_EN | RTSX_DMA_512);

	/* Queue commands to perform SD transfer. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER,
	    0xff, tmode | RTSX_SD_TRANSFER_START);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER,
	    RTSX_SD_TRANSFER_END, RTSX_SD_TRANSFER_END);

	error = rtsx_hostcmd_send(sc, ncmd);
	if (error)
		goto ret;

	if (cmd->c_dmamap)
		error = rtsx_xfer_adma(sc, cmd);
	else
		error = rtsx_xfer_bounce(sc, cmd);
ret:
	DPRINTF(3,("%s: xfer done, error=%d\n", DEVNAME(sc), error));
	return error;
}

int
rtsx_xfer_bounce(struct rtsx_softc *sc, struct sdmmc_command *cmd)
{
    	caddr_t datakvap;
	bus_dma_segment_t segs;
	int rsegs, error;
	int read = ISSET(cmd->c_flags, SCF_CMD_READ);

	/* Allocate and map DMA bounce buffer for data transfer. */
	error = bus_dmamem_alloc(sc->dmat, cmd->c_datalen, 0, 0, &segs, 1,
	    &rsegs, BUS_DMA_WAITOK|BUS_DMA_ZERO);
	if (error) {
		DPRINTF(3, ("%s: could not allocate %d bytes\n",
		    DEVNAME(sc), cmd->c_datalen));
		return error;
	}
	error = bus_dmamem_map(sc->dmat, &segs, rsegs, cmd->c_datalen,
	    &datakvap, BUS_DMA_WAITOK|BUS_DMA_COHERENT);
	if (error) {
		DPRINTF(3, ("%s: could not map data buffer\n", DEVNAME(sc)));
		goto free_databuf;
	}

	/* If this is a write, copy data from sdmmc-provided buffer. */
	if (!read)
		memcpy(datakvap, cmd->c_data, cmd->c_datalen);

	/* Load the data buffer and sync it. */
	error = bus_dmamap_load(sc->dmat, sc->dmap_data, datakvap,
	    cmd->c_datalen, NULL, BUS_DMA_WAITOK);
	if (error) {
		DPRINTF(3, ("%s: could not load DMA map\n", DEVNAME(sc)));
		goto unmap_databuf;
	}
	bus_dmamap_sync(sc->dmat, sc->dmap_data, 0, cmd->c_datalen,
	    BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(sc->dmat, sc->dmap_data, 0, cmd->c_datalen,
	    BUS_DMASYNC_PREWRITE);

	error = rtsx_xfer_exec(sc, sc->dmap_data,
	    RTSX_TRIG_DMA | (read ? RTSX_DMA_READ : 0) |
	    (cmd->c_datalen & 0x00ffffff));
	if (error)
		goto unload_databuf;

	/* Sync and unload data DMA buffer. */
	bus_dmamap_sync(sc->dmat, sc->dmap_data, 0, cmd->c_datalen,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->dmat, sc->dmap_data, 0, cmd->c_datalen,
	    BUS_DMASYNC_POSTWRITE);

unload_databuf:
	bus_dmamap_unload(sc->dmat, sc->dmap_data);

	/* If this is a read, copy data into sdmmc-provided buffer. */
	if (error == 0 && read)
		memcpy(cmd->c_data, datakvap, cmd->c_datalen);

	/* Free DMA data buffer. */
unmap_databuf:
	bus_dmamem_unmap(sc->dmat, datakvap, cmd->c_datalen);
free_databuf:
	bus_dmamem_free(sc->dmat, &segs, rsegs);
	return error;
}

int
rtsx_xfer_adma(struct rtsx_softc *sc, struct sdmmc_command *cmd)
{
	int i, error;
	uint64_t *descp;
	int read = ISSET(cmd->c_flags, SCF_CMD_READ);

	/* Initialize scatter-gather transfer descriptors. */
	descp = (uint64_t *)sc->admabuf;
	for (i = 0; i < cmd->c_dmamap->dm_nsegs; i++) {
		uint64_t paddr = cmd->c_dmamap->dm_segs[i].ds_addr;
		uint64_t len = cmd->c_dmamap->dm_segs[i].ds_len;
		uint8_t sgflags = RTSX_SG_VALID | RTSX_SG_TRANS_DATA;
		uint64_t desc;

		if (i == cmd->c_dmamap->dm_nsegs - 1)
			sgflags |= RTSX_SG_END;
		len &= 0x00ffffff;
		desc = htole64((paddr << 32) | (len << 12) | sgflags);
		memcpy(descp, &desc, sizeof(*descp));
		descp++;
	}

	error = bus_dmamap_load(sc->dmat, sc->dmap_adma, sc->admabuf,
	    RTSX_ADMA_DESC_SIZE, NULL, BUS_DMA_WAITOK);
	if (error) {
		DPRINTF(3, ("%s: could not load DMA map\n", DEVNAME(sc)));
		return error;
	}
	bus_dmamap_sync(sc->dmat, sc->dmap_adma, 0, RTSX_ADMA_DESC_SIZE,
	    	BUS_DMASYNC_PREWRITE);

	error = rtsx_xfer_exec(sc, sc->dmap_adma,
	    RTSX_ADMA_MODE | RTSX_TRIG_DMA | (read ? RTSX_DMA_READ : 0));

	bus_dmamap_sync(sc->dmat, sc->dmap_adma, 0, RTSX_ADMA_DESC_SIZE,
	    	BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->dmat, sc->dmap_adma);
	return error;
}

void
rtsx_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct rtsx_softc *sc = sch;
	bus_dma_segment_t segs;
	int rsegs;
	caddr_t cmdkvap;
	u_int32_t *cmdbuf;
	u_int8_t rsp_type;
	u_int16_t r;
	int ncmd;
	int error = 0;

	DPRINTF(3,("%s: executing cmd %hu\n", DEVNAME(sc), cmd->c_opcode));

	/* Refuse SDIO probe if the chip doesn't support SDIO. */
	if (cmd->c_opcode == SD_IO_SEND_OP_COND &&
	    !ISSET(sc->flags, RTSX_F_SDIO_SUPPORT)) {
		error = ENOTSUP;
		goto ret;
	}

	rsp_type = rtsx_response_type(cmd->c_flags & 0xff00);
	if (rsp_type == 0) {
		printf("%s: unknown response type 0x%x\n", DEVNAME(sc),
			(cmd->c_flags & 0xff00));
		error = EINVAL;
		goto ret;
	}

	/* Allocate and map the host command buffer. */
	error = bus_dmamem_alloc(sc->dmat, RTSX_HOSTCMD_BUFSIZE, 0, 0, &segs, 1,
	    &rsegs, BUS_DMA_WAITOK|BUS_DMA_ZERO);
	if (error)
		goto ret;
	error = bus_dmamem_map(sc->dmat, &segs, rsegs, RTSX_HOSTCMD_BUFSIZE,
	    &cmdkvap, BUS_DMA_WAITOK|BUS_DMA_COHERENT);
	if (error)
		goto free_cmdbuf;

	/* The command buffer queues commands the host controller will
	 * run asynchronously. */
	cmdbuf = (u_int32_t *)cmdkvap;
	ncmd = 0;

	/* Queue commands to set SD command index and argument. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD0, 0xff, 0x40 | cmd->c_opcode); 
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD1, 0xff, cmd->c_arg >> 24);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD2, 0xff, cmd->c_arg >> 16);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD3, 0xff, cmd->c_arg >> 8);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD4, 0xff, cmd->c_arg);

	/* Queue command to set response type. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CFG2, 0xff, rsp_type);

	/* Use the ping-pong buffer for commands which do not transfer data. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE,
	    0x01, RTSX_PINGPONG_BUFFER);

	/* Queue commands to perform SD transfer. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER,
	    0xff, RTSX_TM_CMD_RSP | RTSX_SD_TRANSFER_START);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER,
	    RTSX_SD_TRANSFER_END|RTSX_SD_STAT_IDLE,
	    RTSX_SD_TRANSFER_END|RTSX_SD_STAT_IDLE);

	/* Queue commands to read back card status response.*/
	if (rsp_type == RTSX_SD_RSP_TYPE_R2) {
		for (r = RTSX_PPBUF_BASE2 + 15; r > RTSX_PPBUF_BASE2; r--)
			rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, r, 0, 0);
		rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, RTSX_SD_CMD5,
		    0, 0);
	} else if (rsp_type != RTSX_SD_RSP_TYPE_R0) {
		for (r = RTSX_SD_CMD0; r <= RTSX_SD_CMD4; r++)
			rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, r, 0, 0);
	}

	/* Load and sync command DMA buffer. */
	error = bus_dmamap_load(sc->dmat, sc->dmap_cmd, cmdkvap,
	    RTSX_HOSTCMD_BUFSIZE, NULL, BUS_DMA_WAITOK);
	if (error)
		goto unmap_cmdbuf;

	bus_dmamap_sync(sc->dmat, sc->dmap_cmd, 0, RTSX_HOSTCMD_BUFSIZE,
	    BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(sc->dmat, sc->dmap_cmd, 0, RTSX_HOSTCMD_BUFSIZE,
	    BUS_DMASYNC_PREWRITE);

	/* Run the command queue and wait for completion. */
	error = rtsx_hostcmd_send(sc, ncmd);
	if (error == 0)
		error = rtsx_wait_intr(sc, RTSX_TRANS_OK_INT, 1);
	if (error)
		goto unload_cmdbuf;

	bus_dmamap_sync(sc->dmat, sc->dmap_cmd, 0, RTSX_HOSTCMD_BUFSIZE,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->dmat, sc->dmap_cmd, 0, RTSX_HOSTCMD_BUFSIZE,
	    BUS_DMASYNC_POSTWRITE);

	/* Copy card response into sdmmc response buffer. */
	if (ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		/* Copy bytes like sdhc(4), which on little-endian uses
		 * different byte order for short and long responses... */
		if (ISSET(cmd->c_flags, SCF_RSP_136)) {
			memcpy(cmd->c_resp, cmdkvap + 1, sizeof(cmd->c_resp));
		} else {
			/* First byte is CHECK_REG_CMD return value, second
			 * one is the command op code -- we skip those. */
			cmd->c_resp[0] =
			    ((betoh32(cmdbuf[0]) & 0x0000ffff) << 16) |
			    ((betoh32(cmdbuf[1]) & 0xffff0000) >> 16);
		}
	}

	if (cmd->c_data) {
		error = rtsx_xfer(sc, cmd, cmdbuf);
		if (error) {
			u_int8_t stat1;

			if (rtsx_read(sc, RTSX_SD_STAT1, &stat1) == 0 &&
			    (stat1 & RTSX_SD_CRC_ERR))
				printf("%s: CRC error\n", DEVNAME(sc));
		}
	}

unload_cmdbuf:
	bus_dmamap_unload(sc->dmat, sc->dmap_cmd);
unmap_cmdbuf:
	bus_dmamem_unmap(sc->dmat, cmdkvap, RTSX_HOSTCMD_BUFSIZE);
free_cmdbuf:
	bus_dmamem_free(sc->dmat, &segs, rsegs);
ret:
	SET(cmd->c_flags, SCF_ITSDONE);
	cmd->c_error = error;
}

/* Prepare for another command. */
void
rtsx_soft_reset(struct rtsx_softc *sc)
{
	DPRINTF(1,("%s: soft reset\n", DEVNAME(sc)));

	/* Stop command transfer. */
	WRITE4(sc, RTSX_HCBCTLR, RTSX_STOP_CMD);

	(void)rtsx_write(sc, RTSX_CARD_STOP, RTSX_SD_STOP|RTSX_SD_CLR_ERR,
		    RTSX_SD_STOP|RTSX_SD_CLR_ERR);

	/* Stop DMA transfer. */
	WRITE4(sc, RTSX_HDBCTLR, RTSX_STOP_DMA);
	(void)rtsx_write(sc, RTSX_DMACTL, RTSX_DMA_RST, RTSX_DMA_RST);

	(void)rtsx_write(sc, RTSX_RBCTL, RTSX_RB_FLUSH, RTSX_RB_FLUSH);
}

int
rtsx_wait_intr(struct rtsx_softc *sc, int mask, int secs)
{
	int status;
	int error = 0;
	int s;

	mask |= RTSX_TRANS_FAIL_INT;

	s = splsdmmc();
	status = sc->intr_status & mask;
	while (status == 0) {
		if (tsleep_nsec(&sc->intr_status, PRIBIO, "rtsxintr",
		    SEC_TO_NSEC(secs)) == EWOULDBLOCK) {
			rtsx_soft_reset(sc);
			error = ETIMEDOUT;
			break;
		}
		status = sc->intr_status & mask;
	}
	sc->intr_status &= ~status;

	/* Has the card disappeared? */
	if (!ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		error = ENODEV;

	splx(s);

	if (error == 0 && (status & RTSX_TRANS_FAIL_INT))
		error = EIO;

	return error;
}

void
rtsx_card_insert(struct rtsx_softc *sc)
{
	DPRINTF(1, ("%s: card inserted\n", DEVNAME(sc)));

	sc->flags |= RTSX_F_CARD_PRESENT;
	(void)rtsx_led_enable(sc);

	/* Schedule card discovery task. */
	sdmmc_needs_discover(sc->sdmmc);
}

void
rtsx_card_eject(struct rtsx_softc *sc)
{
	DPRINTF(1, ("%s: card ejected\n", DEVNAME(sc)));

	sc->flags &= ~RTSX_F_CARD_PRESENT;
	(void)rtsx_led_disable(sc);

	/* Schedule card discovery task. */
	sdmmc_needs_discover(sc->sdmmc);
}

/*
 * Established by attachment driver at interrupt priority IPL_SDMMC.
 */
int
rtsx_intr(void *arg)
{
	struct rtsx_softc *sc = arg;
	u_int32_t enabled, status;

	enabled = READ4(sc, RTSX_BIER);
	status = READ4(sc, RTSX_BIPR);

	/* Ack interrupts. */
	WRITE4(sc, RTSX_BIPR, status);

	if (((enabled & status) == 0) || status == 0xffffffff)
		return 0;

	if (status & RTSX_SD_INT) {
		if (status & RTSX_SD_EXIST) {
			if (!ISSET(sc->flags, RTSX_F_CARD_PRESENT))
				rtsx_card_insert(sc);
		} else {
			rtsx_card_eject(sc);
		}
	}

	if (status & (RTSX_TRANS_OK_INT | RTSX_TRANS_FAIL_INT)) {
		sc->intr_status |= status;
		wakeup(&sc->intr_status);
	}

	return 1;
}
