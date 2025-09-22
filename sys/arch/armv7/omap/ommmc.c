/*	$OpenBSD: ommmc.c,v 1.41 2024/05/22 05:51:49 jsg Exp $	*/

/*
 * Copyright (c) 2009 Dale Rahn <drahn@openbsd.org>
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

/* Omap SD/MMC support derived from /sys/dev/sdmmc/sdhc.c */


#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcvar.h>

#include <armv7/omap/prcmvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/*
 * NOTE: on OMAP4430/AM335x these registers skew by 0x100
 * this is handled by mapping at base address + 0x100
 */
/* registers */
#define MMCHS_SYSCONFIG	0x010
#define MMCHS_SYSSTATUS	0x014
#define MMCHS_CSRE	0x024
#define MMCHS_SYSTEST	0x028
#define  MMCHS_SYSTEST_SDCD	(1 << 15)
#define MMCHS_CON	0x02C
#define  MMCHS_CON_INIT	(1<<1)
#define  MMCHS_CON_DW8	(1<<5)
#define  MMCHS_CON_OD	(1<<0)
#define MMCHS_PWCNT	0x030
#define MMCHS_BLK	0x104
#define  MMCHS_BLK_NBLK_MAX	0xffff
#define  MMCHS_BLK_NBLK_SHIFT	16
#define  MMCHS_BLK_NBLK_MASK	(MMCHS_BLK_NBLK_MAX<<MMCHS_BLK_NBLK_SHIFT)
#define  MMCHS_BLK_BLEN_MAX	0x400
#define  MMCHS_BLK_BLEN_SHIFT	0
#define  MMCHS_BLK_BLEN_MASK	(MMCHS_BLK_BLEN_MAX<<MMCHS_BLK_BLEN_SHIFT)
#define MMCHS_ARG	0x108
#define MMCHS_CMD	0x10C
#define  MMCHS_CMD_INDX_SHIFT		24
#define  MMCHS_CMD_INDX_SHIFT_MASK	(0x3f << MMCHS_CMD_INDX_SHIFT)
#define	 MMCHS_CMD_CMD_TYPE_SHIFT	22
#define	 MMCHS_CMD_DP_SHIFT		21
#define	 MMCHS_CMD_DP			(1 << MMCHS_CMD_DP_SHIFT)
#define	 MMCHS_CMD_CICE_SHIFT		20
#define	 MMCHS_CMD_CICE			(1 << MMCHS_CMD_CICE_SHIFT)
#define	 MMCHS_CMD_CCCE_SHIFT		19
#define	 MMCHS_CMD_CCCE			(1 << MMCHS_CMD_CCCE_SHIFT)
#define	 MMCHS_CMD_RSP_TYPE_SHIFT	16
#define  MMCHS_CMD_RESP_NONE		(0x0 << MMCHS_CMD_RSP_TYPE_SHIFT)
#define  MMCHS_CMD_RESP136		(0x1 << MMCHS_CMD_RSP_TYPE_SHIFT)
#define  MMCHS_CMD_RESP48		(0x2 << MMCHS_CMD_RSP_TYPE_SHIFT)
#define  MMCHS_CMD_RESP48B		(0x3 << MMCHS_CMD_RSP_TYPE_SHIFT)
#define  MMCHS_CMD_MSBS			(1 << 5)
#define  MMCHS_CMD_DDIR			(1 << 4)
#define  MMCHS_CMD_ACEN			(1 << 2)
#define  MMCHS_CMD_BCE			(1 << 1)
#define  MMCHS_CMD_DE			(1 << 0)
#define MMCHS_RSP10	0x110
#define MMCHS_RSP32	0x114
#define MMCHS_RSP54	0x118
#define MMCHS_RSP76	0x11C
#define MMCHS_DATA	0x120
#define MMCHS_PSTATE	0x124
#define  MMCHS_PSTATE_CLEV	(1<<24)
#define  MMCHS_PSTATE_DLEV_SH	20
#define  MMCHS_PSTATE_DLEV_M	(0xf << MMCHS_PSTATE_DLEV_SH)
#define  MMCHS_PSTATE_BRE	(1<<11)
#define  MMCHS_PSTATE_BWE	(1<<10)
#define  MMCHS_PSTATE_RTA	(1<<9)
#define  MMCHS_PSTATE_WTA	(1<<8)
#define  MMCHS_PSTATE_DLA	(1<<2)
#define  MMCHS_PSTATE_DATI	(1<<1)
#define  MMCHS_PSTATE_CMDI	(1<<0)
#define  MMCHS_PSTATE_FMT "\20" \
    "\x098_CLEV" \
    "\x08b_BRE" \
    "\x08a_BWE" \
    "\x089_RTA" \
    "\x088_WTA" \
    "\x082_DLA" \
    "\x081_DATI" \
    "\x080_CMDI"
#define MMCHS_HCTL	0x128
#define  MMCHS_HCTL_SDVS_SHIFT	9
#define  MMCHS_HCTL_SDVS_MASK	(0x7<<MMCHS_HCTL_SDVS_SHIFT)
#define  MMCHS_HCTL_SDVS_V18	(0x5<<MMCHS_HCTL_SDVS_SHIFT)
#define  MMCHS_HCTL_SDVS_V30	(0x6<<MMCHS_HCTL_SDVS_SHIFT)
#define  MMCHS_HCTL_SDVS_V33	(0x7<<MMCHS_HCTL_SDVS_SHIFT)
#define  MMCHS_HCTL_SDBP	(1<<8)
#define  MMCHS_HCTL_HSPE	(1<<2)
#define  MMCHS_HCTL_DTW		(1<<1)
#define MMCHS_SYSCTL	0x12C
#define  MMCHS_SYSCTL_SRD	(1<<26)
#define  MMCHS_SYSCTL_SRC	(1<<25)
#define  MMCHS_SYSCTL_SRA	(1<<24)
#define  MMCHS_SYSCTL_DTO_SH	16
#define  MMCHS_SYSCTL_DTO_MASK	0x000f0000
#define  MMCHS_SYSCTL_CLKD_SH	6
#define  MMCHS_SYSCTL_CLKD_MASK	0x0000ffc0
#define  MMCHS_SYSCTL_CEN	(1<<2)
#define  MMCHS_SYSCTL_ICS	(1<<1)
#define  MMCHS_SYSCTL_ICE	(1<<0)
#define MMCHS_STAT	0x130
#define  MMCHS_STAT_BADA	(1<<29)
#define  MMCHS_STAT_CERR	(1<<28)
#define  MMCHS_STAT_ACE		(1<<24)
#define  MMCHS_STAT_DEB		(1<<22)
#define  MMCHS_STAT_DCRC	(1<<21)
#define  MMCHS_STAT_DTO		(1<<20)
#define  MMCHS_STAT_CIE		(1<<19)
#define  MMCHS_STAT_CEB		(1<<18)
#define  MMCHS_STAT_CCRC	(1<<17)
#define  MMCHS_STAT_CTO		(1<<16)
#define  MMCHS_STAT_ERRI	(1<<15)
#define  MMCHS_STAT_OBI		(1<<9)
#define  MMCHS_STAT_CIRQ	(1<<8)
#define  MMCHS_STAT_BRR		(1<<5)
#define  MMCHS_STAT_BWR		(1<<4)
#define  MMCHS_STAT_BGE		(1<<2)
#define  MMCHS_STAT_TC		(1<<1)
#define  MMCHS_STAT_CC		(1<<0)
#define  MMCHS_STAT_FMT "\20" \
    "\x09d_BADA" \
    "\x09c_CERR" \
    "\x098_ACE" \
    "\x096_DEB" \
    "\x095_DCRC" \
    "\x094_DTO" \
    "\x093_CIE" \
    "\x092_CEB" \
    "\x091_CCRC" \
    "\x090_CTO" \
    "\x08f_ERRI" \
    "\x089_OBI" \
    "\x088_CIRQ" \
    "\x085_BRR" \
    "\x084_BWR" \
    "\x082_BGE" \
    "\x081_TC" \
    "\x080_CC"
#define MMCHS_IE	0x134
#define MMCHS_ISE	0x138
#define MMCHS_AC12	0x13C
#define MMCHS_CAPA	0x140
#define  MMCHS_CAPA_VS18	(1 << 26)
#define  MMCHS_CAPA_VS30	(1 << 25)
#define  MMCHS_CAPA_VS33	(1 << 24)
#define  MMCHS_CAPA_SRS		(1 << 23)
#define  MMCHS_CAPA_DS		(1 << 22)
#define  MMCHS_CAPA_HSS		(1 << 21)
#define  MMCHS_CAPA_MBL_SHIFT	16
#define  MMCHS_CAPA_MBL_MASK	(3 << MMCHS_CAPA_MBL_SHIFT)
#define MMCHS_CUR_CAPA	0x148
#define MMCHS_REV	0x1fc

#define SDHC_COMMAND_TIMEOUT	1 /* sec */
#define SDHC_BUFFER_TIMEOUT	1 /* sec */
#define SDHC_TRANSFER_TIMEOUT	1 /* sec */

int ommmc_match(struct device *, void *, void *);
void ommmc_attach(struct device *, struct device *, void *);

struct ommmc_softc {
	struct device sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;
	void			*sc_ih; /* Interrupt handler */
	uint32_t		sc_flags;
#define FL_HSS		(1 << 0)
	uint32_t		sc_gpio[4];

	struct device *sdmmc;		/* generic SD/MMC device */
	int clockbit;			/* clock control bit */
	uint32_t clkbase;		/* base clock frequency in kHz */
	int maxblklen;			/* maximum block length */
	int flags;			/* flags for this host */
	uint32_t ocr;			/* OCR value from capabilities */
	uint32_t intr_status;		/* soft interrupt status */
	uint32_t intr_error_status;	/*  */
};


/* Host controller functions called by the attachment driver. */
void	ommmc_power(int, void *);
void	ommmc_shutdown(void *);
int	ommmc_intr(void *);

/* RESET MODES */
#define MMC_RESET_DAT	1
#define MMC_RESET_CMD	2
#define MMC_RESET_ALL	(MMC_RESET_CMD|MMC_RESET_DAT)

/* flag values */
#define SHF_USE_DMA		0x0001

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

int	ommmc_host_reset(sdmmc_chipset_handle_t);
uint32_t ommmc_host_ocr(sdmmc_chipset_handle_t);
int	ommmc_host_maxblklen(sdmmc_chipset_handle_t);
int	ommmc_card_detect(sdmmc_chipset_handle_t);
int	ommmc_bus_power(sdmmc_chipset_handle_t, uint32_t);
int	ommmc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	ommmc_bus_width(sdmmc_chipset_handle_t, int);
void	ommmc_card_intr_mask(sdmmc_chipset_handle_t, int);
void	ommmc_card_intr_ack(sdmmc_chipset_handle_t);
void	ommmc_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
int	ommmc_start_command(struct ommmc_softc *, struct sdmmc_command *);
int	ommmc_wait_state(struct ommmc_softc *, uint32_t, uint32_t);
int	ommmc_soft_reset(struct ommmc_softc *, int);
int	ommmc_wait_intr(struct ommmc_softc *, int, int);
void	ommmc_transfer_data(struct ommmc_softc *, struct sdmmc_command *);
void	ommmc_read_data(struct ommmc_softc *, uint8_t *, int);
void	ommmc_write_data(struct ommmc_softc *, uint8_t *, int);

/* #define SDHC_DEBUG */
#ifdef SDHC_DEBUG
int ommmcdebug = 20;
#define DPRINTF(n,s)	do { if ((n) <= ommmcdebug) printf s; } while (0)
void	ommmc_dump_regs(struct ommmc_softc *);
#else
#define DPRINTF(n,s)	do {} while(0)
#endif

struct sdmmc_chip_functions ommmc_functions = {
	/* host controller reset */
	ommmc_host_reset,
	/* host controller capabilities */
	ommmc_host_ocr,
	ommmc_host_maxblklen,
	/* card detection */
	ommmc_card_detect,
	/* bus power and clock frequency */
	ommmc_bus_power,
	ommmc_bus_clock,
	ommmc_bus_width,
	/* command execution */
	ommmc_exec_command,
	/* card interrupt */
	ommmc_card_intr_mask,
	ommmc_card_intr_ack
};

struct cfdriver ommmc_cd = {
	NULL, "ommmc", DV_DULL
};

const struct cfattach ommmc_ca = {
	sizeof(struct ommmc_softc), ommmc_match, ommmc_attach
};

int
ommmc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "ti,omap3-hsmmc") ||
	    OF_is_compatible(faa->fa_node, "ti,omap4-hsmmc") ||
	    OF_is_compatible(faa->fa_node, "ti,am335-sdhci"));
}

void
ommmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct ommmc_softc		*sc = (struct ommmc_softc *) self;
	struct fdt_attach_args		*faa = aux;
	struct sdmmcbus_attach_args	 saa;
	uint32_t			 caps, width;
	uint32_t			 addr, size;
	int				 len, unit;
	char				 hwmods[128];

	if (faa->fa_nreg < 1)
		return;

	if (faa->fa_reg[0].size <= 0x100)
		return;

	if (OF_is_compatible(faa->fa_node, "ti,omap4-hsmmc") ||
	    OF_is_compatible(faa->fa_node, "ti,am335-sdhci")) {
		addr = faa->fa_reg[0].addr + 0x100;
		size = faa->fa_reg[0].size - 0x100;
	} else {
		addr = faa->fa_reg[0].addr;
		size = faa->fa_reg[0].size;
	}

	unit = -1;
	if ((len = OF_getprop(faa->fa_node, "ti,hwmods", hwmods,
	    sizeof(hwmods))) == 5) {
		if (!strncmp(hwmods, "mmc", 3) &&
		    (hwmods[3] > '0') && (hwmods[3] <= '9'))
			unit = hwmods[3] - '1';
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	sc->sc_node = faa->fa_node;
	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	/* Enable ICLKEN, FCLKEN? */
	if (unit != -1)
		prcm_enablemodule(PRCM_MMC0 + unit);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_SDMMC,
	    ommmc_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf("%s: cannot map interrupt\n", DEVNAME(sc));
		goto err;
	}

	OF_getpropintarray(faa->fa_node, "cd-gpios", sc->sc_gpio,
	    sizeof(sc->sc_gpio));
	if (sc->sc_gpio[0])
		gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_INPUT);

	/* Controller Voltage Capabilities Initialization */
	HSET4(sc, MMCHS_CAPA, MMCHS_CAPA_VS18 | MMCHS_CAPA_VS30);

#ifdef SDHC_DEBUG
	ommmc_dump_regs(sc);
#endif

	/*
	 * Reset the host controller and enable interrupts.
	 */
	ommmc_host_reset(sc);

	/* Determine host capabilities. */
	caps = HREAD4(sc, MMCHS_CAPA);

#if 0
	/* we want this !! */
	/* Use DMA if the host system and the controller support it. */
	if (usedma && ISSET(caps, SDHC_DMA_SUPPORT))
		SET(sc->flags, SHF_USE_DMA);
#endif

	/*
	 * Determine the base clock frequency. (2.2.24)
	 */

	sc->clkbase = 96 * 1000;
#if 0
	if (SDHC_BASE_FREQ_KHZ(caps) != 0)
		sc->clkbase = SDHC_BASE_FREQ_KHZ(caps);
#endif
	if (sc->clkbase == 0) {
		/* The attachment driver must tell us. */
		printf("%s: base clock frequency unknown\n", DEVNAME(sc));
		goto err;
	} else if (sc->clkbase < 10000 || sc->clkbase > 96000) {
		/* SDHC 1.0 supports only 10-63 MHz. */
		printf("%s: base clock frequency out of range: %u MHz\n",
		    DEVNAME(sc), sc->clkbase / 1000);
		goto err;
	}

	/*
	 * XXX Set the data timeout counter value according to
	 * capabilities. (2.2.15)
	 */


	/*
	 * Determine SD bus voltage levels supported by the controller.
	 */
	if (caps & MMCHS_CAPA_VS18)
		SET(sc->ocr, MMC_OCR_1_65V_1_95V);
	if (caps & MMCHS_CAPA_VS30)
		SET(sc->ocr, MMC_OCR_2_9V_3_0V | MMC_OCR_3_0V_3_1V);
	if (caps & MMCHS_CAPA_VS33)
		SET(sc->ocr, MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V);

	/*
	 * Omap max block size is fixed (single buffer), could limit
	 * this to 512 for double buffering, but dont see the point.
	 */
	switch ((caps & MMCHS_CAPA_MBL_MASK) >> MMCHS_CAPA_MBL_SHIFT) {
	case 0:
		sc->maxblklen = 512;
		break;
	case 1:
		sc->maxblklen = 1024;
		break;
	case 2:
		sc->maxblklen = 2048;
		break;
	default:
		sc->maxblklen = 512;
		printf("invalid capability blocksize in capa %08x,"
		    " trying 512\n", HREAD4(sc, MMCHS_CAPA));
	}
	/*
	 * MMC does not support blksize > 512 yet
	 */
	sc->maxblklen = 512;
	/*
	 * Attach the generic SD/MMC bus driver.  (The bus driver must
	 * not invoke any chipset functions before it is attached.)
	 */
	bzero(&saa, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &ommmc_functions;
	saa.sch = sc;
	if (OF_getproplen(faa->fa_node, "ti,needs-special-hs-handling") == 0 &&
	    (caps & MMCHS_CAPA_HSS)) {
		sc->sc_flags |= FL_HSS;
		saa.caps |= SMC_CAPS_MMC_HIGHSPEED | SMC_CAPS_SD_HIGHSPEED;
	}
	width = OF_getpropint(faa->fa_node, "bus-width", 1);
	/* with bbb emmc width > 1 ommmc_wait_intr MMCHS_STAT_CC times out */
	if (unit > 0)
		width = 1;
	if (width >= 8)
		saa.caps |= SMC_CAPS_8BIT_MODE;
	if (width >= 4)
		saa.caps |= SMC_CAPS_4BIT_MODE;

	sc->sdmmc = config_found(&sc->sc_dev, &saa, NULL);
	if (sc->sdmmc == NULL) {
		printf("%s: can't attach sdmmc\n", DEVNAME(sc));
		goto err;
	}

	return;
err:
	if (sc->sc_ih != NULL)
		arm_intr_disestablish(sc->sc_ih);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, size);
}


/*
 * Power hook established by or called from attachment driver.
 */
void
ommmc_power(int why, void *arg)
{
#if 0
	struct ommmc_softc *sc = arg;
	int n, i;
#endif

	switch(why) {
	case DVACT_SUSPEND:
		/* XXX poll for command completion or suspend command
		 * in progress */

		/* Save the host controller state. */
#if 0
		for (i = 0; i < sizeof sc->regs; i++)
			sc->regs[i] = HREAD1(sc, i);
#endif
		break;

	case DVACT_RESUME:
		/* Restore the host controller state. */
#if 0
		(void)ommmc_host_reset(sc);
		for (i = 0; i < sizeof sc->regs; i++)
			HWRITE1(sc, i, sc->regs[i]);
#endif
		break;
	}
}

/*
 * Shutdown hook established by or called from attachment driver.
 */
void
ommmc_shutdown(void *arg)
{
	struct ommmc_softc *sc = arg;

	/* XXX chip locks up if we don't disable it before reboot. */
	(void)ommmc_host_reset(sc);
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int
ommmc_host_reset(sdmmc_chipset_handle_t sch)
{
	struct ommmc_softc *sc = sch;
	uint32_t imask;
	int error;
	int s;

	s = splsdmmc();

	/* Disable all interrupts. */
	HWRITE4(sc, MMCHS_IE, 0);
	HWRITE4(sc, MMCHS_ISE, 0);

	/*
	 * Reset the entire host controller and wait up to 100ms for
	 * the controller to clear the reset bit.
	 */
	if ((error = ommmc_soft_reset(sc, MMCHS_SYSCTL_SRA)) != 0) {
		splx(s);
		return (error);
	}

#if 0
	HSET4(sc, MMCHS_CON, MMCHS_CON_INIT);
	HWRITE4(sc, MMCHS_CMD, 0);
	delay(100); /* should delay 1ms */

	HWRITE4(sc, MMCHS_STAT, MMCHS_STAT_CC);
	HCLR4(sc, MMCHS_CON, MMCHS_CON_INIT);
	HWRITE4(sc, MMCHS_STAT, ~0);
#endif


	/* Set data timeout counter value to max for now. */
	HSET4(sc, MMCHS_SYSCTL, 0xe << MMCHS_SYSCTL_DTO_SH);

	/* Enable interrupts. */
	imask = MMCHS_STAT_BRR | MMCHS_STAT_BWR | MMCHS_STAT_BGE |
	    MMCHS_STAT_TC | MMCHS_STAT_CC;

	imask |= MMCHS_STAT_BADA | MMCHS_STAT_CERR | MMCHS_STAT_DEB |
	    MMCHS_STAT_DCRC | MMCHS_STAT_DTO | MMCHS_STAT_CIE |
	    MMCHS_STAT_CEB | MMCHS_STAT_CCRC | MMCHS_STAT_CTO;

	HWRITE4(sc, MMCHS_IE, imask);
	HWRITE4(sc, MMCHS_ISE, imask);

	/* Switch back to 1-bit bus. */
	HCLR4(sc, MMCHS_CON, MMCHS_CON_DW8);
	HCLR4(sc, MMCHS_HCTL, MMCHS_HCTL_DTW);

	splx(s);
	return (0);
}

uint32_t
ommmc_host_ocr(sdmmc_chipset_handle_t sch)
{
	struct ommmc_softc *sc = sch;
	return (sc->ocr);
}

int
ommmc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	struct ommmc_softc *sc = sch;
	return (sc->maxblklen);
}

/*
 * Return non-zero if the card is currently inserted.
 */
int
ommmc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct ommmc_softc *sc = sch;

	if (OF_getproplen(sc->sc_node, "non-removable") == 0)
		return 1;

	if (sc->sc_gpio[0]) {
		int inverted, val;

		val = gpio_controller_get_pin(sc->sc_gpio);

		inverted = (OF_getproplen(sc->sc_node, "cd-inverted") == 0);
		return inverted ? !val : val;
	}

	return !ISSET(HREAD4(sc, MMCHS_SYSTEST), MMCHS_SYSTEST_SDCD) ?
	    1 : 0;
}

/*
 * Set or change SD bus voltage and enable or disable SD bus power.
 * Return zero on success.
 */
int
ommmc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	struct ommmc_softc *sc = sch;
	uint32_t vdd;
	uint32_t reg;
	int s;

	s = splsdmmc();

	/*
	 * Disable bus power before voltage change.
	 */
	HCLR4(sc, MMCHS_HCTL, MMCHS_HCTL_SDBP);

	/* If power is disabled, reset the host and return now. */
	if (ocr == 0) {
		splx(s);
		(void)ommmc_host_reset(sc);
		return (0);
	}

	/*
	 * Select the maximum voltage according to capabilities.
	 */
	ocr &= sc->ocr;

	if (ISSET(ocr, MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V))
		vdd = MMCHS_HCTL_SDVS_V33;
	else if (ISSET(ocr, MMC_OCR_2_9V_3_0V | MMC_OCR_3_0V_3_1V))
		vdd = MMCHS_HCTL_SDVS_V30;
	else if (ISSET(ocr, MMC_OCR_1_65V_1_95V))
		vdd = MMCHS_HCTL_SDVS_V18;
	else {
		/* Unsupported voltage level requested. */
		splx(s);
		return (EINVAL);
	}

	/*
	 * Enable bus power.  Wait at least 1 ms (or 74 clocks) plus
	 * voltage ramp until power rises.
	 */
	reg = HREAD4(sc, MMCHS_HCTL);
	reg &= ~MMCHS_HCTL_SDVS_MASK;
	reg |= vdd;
	HWRITE4(sc, MMCHS_HCTL, reg);

	HSET4(sc, MMCHS_HCTL, MMCHS_HCTL_SDBP);
	delay(10000); /* XXX */

	/*
	 * The host system may not power the bus due to battery low,
	 * etc.  In that case, the host controller should clear the
	 * bus power bit.
	 */
	if (!ISSET(HREAD4(sc, MMCHS_HCTL), MMCHS_HCTL_SDBP)) {
		splx(s);
		return (ENXIO);
	}

	splx(s);
	return (0);
}

/*
 * Return the smallest possible base clock frequency divisor value
 * for the CLOCK_CTL register to produce `freq' (kHz).
 */
static int
ommmc_clock_divisor(struct ommmc_softc *sc, uint32_t freq)
{
	int div;
	uint32_t maxclk = MMCHS_SYSCTL_CLKD_MASK>>MMCHS_SYSCTL_CLKD_SH;

	for (div = 1; div <= maxclk; div++)
		if ((sc->clkbase / div) <= freq) {
			return (div);
		}

	printf("divisor failure\n");
	/* No divisor found. */
	return (-1);
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int
ommmc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	int error = 0;
	struct ommmc_softc *sc = sch;
	uint32_t reg;
	int s;
	int div;
	int timo;

	s = splsdmmc();

	/* Must not stop the clock if commands are in progress. */
	for (timo = 1000; timo > 0; timo--) {
		if (!ISSET(HREAD4(sc, MMCHS_PSTATE),
		    MMCHS_PSTATE_CMDI|MMCHS_PSTATE_DATI))
			break;
		delay(10);
	}
	if (timo == 0) {
		error = ETIMEDOUT;
		goto ret;
	}

	/*
	 * Stop SD clock before changing the frequency.
	 */
	HCLR4(sc, MMCHS_SYSCTL, MMCHS_SYSCTL_CEN);
	if (freq == SDMMC_SDCLK_OFF)
		goto ret;

	/*
	 * Set the minimum base clock frequency divisor.
	 */
	if ((div = ommmc_clock_divisor(sc, freq)) < 0) {
		/* Invalid base clock frequency or `freq' value. */
		error = EINVAL;
		goto ret;
	}
	reg = HREAD4(sc, MMCHS_SYSCTL);
	reg &= ~MMCHS_SYSCTL_CLKD_MASK;
	reg |= div << MMCHS_SYSCTL_CLKD_SH;
	HWRITE4(sc, MMCHS_SYSCTL, reg);

	if ((timing == SDMMC_TIMING_HIGHSPEED) && (sc->sc_flags & FL_HSS))
		HSET4(sc, MMCHS_HCTL, MMCHS_HCTL_HSPE);
	else
		HCLR4(sc, MMCHS_HCTL, MMCHS_HCTL_HSPE);

	/*
	 * Start internal clock.  Wait 10ms for stabilization.
	 */
	HSET4(sc, MMCHS_SYSCTL, MMCHS_SYSCTL_ICE);
	for (timo = 1000; timo > 0; timo--) {
		if (ISSET(HREAD4(sc, MMCHS_SYSCTL), MMCHS_SYSCTL_ICS))
			break;
		delay(10);
	}
	if (timo == 0) {
		error = ETIMEDOUT;
		goto ret;
	}

	/*
	 * Enable SD clock.
	 */
	HSET4(sc, MMCHS_SYSCTL, MMCHS_SYSCTL_CEN);
ret:
	splx(s);
	return (error);
}

int
ommmc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct ommmc_softc *sc = sch;
	int s;

	if (width != 1 && width != 4 && width != 8)
		return (1);

	s = splsdmmc();

	if (width == 8)
		HSET4(sc, MMCHS_CON, MMCHS_CON_DW8);
	else
		HCLR4(sc, MMCHS_CON, MMCHS_CON_DW8);

	if (width == 4)
		HSET4(sc, MMCHS_HCTL, MMCHS_HCTL_DTW);
	else if (width == 1)
		HCLR4(sc, MMCHS_HCTL, MMCHS_HCTL_DTW);

	splx(s);

	return (0);
}

void
ommmc_card_intr_mask(sdmmc_chipset_handle_t sch, int enable)
{
	/* - this is SDIO card interrupt */
	struct ommmc_softc *sc = sch;

	if (enable) {
		HSET4(sc, MMCHS_IE, MMCHS_STAT_CIRQ);
		HSET4(sc, MMCHS_ISE, MMCHS_STAT_CIRQ);
	} else {
		HCLR4(sc, MMCHS_IE, MMCHS_STAT_CIRQ);
		HCLR4(sc, MMCHS_ISE, MMCHS_STAT_CIRQ);
	}
}

void
ommmc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	struct ommmc_softc *sc = sch;

	HWRITE4(sc, MMCHS_STAT, MMCHS_STAT_CIRQ);
}

int
ommmc_wait_state(struct ommmc_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;

	state = HREAD4(sc, MMCHS_PSTATE);
	DPRINTF(3,("%s: wait_state %x %x %x(state=%b)\n", DEVNAME(sc),
	    mask, value, state, state, MMCHS_PSTATE_FMT));
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD4(sc, MMCHS_PSTATE)) & mask) == value)
			return (0);
		delay(10);
	}
	DPRINTF(0,("%s: timeout waiting for %x (state=%b)\n", DEVNAME(sc),
	    value, state, MMCHS_PSTATE_FMT));
	return (ETIMEDOUT);
}

void
ommmc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct ommmc_softc *sc = sch;
	int error;

	/*
	 * Start the MMC command, or mark `cmd' as failed and return.
	 */
	error = ommmc_start_command(sc, cmd);
	if (error != 0) {
		cmd->c_error = error;
		SET(cmd->c_flags, SCF_ITSDONE);
		return;
	}

	/*
	 * Wait until the command phase is done, or until the command
	 * is marked done for any other reason.
	 */
	if (!ommmc_wait_intr(sc, MMCHS_STAT_CC, SDHC_COMMAND_TIMEOUT)) {
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
			uint32_t v0,v1,v2,v3;
			v0 = HREAD4(sc, MMCHS_RSP10);
			v1 = HREAD4(sc, MMCHS_RSP32);
			v2 = HREAD4(sc, MMCHS_RSP54);
			v3 = HREAD4(sc, MMCHS_RSP76);

			cmd->c_resp[0] = (v0 >> 8) | ((v1 & 0xff)  << 24);
			cmd->c_resp[1] = (v1 >> 8) | ((v2 & 0xff)  << 24);
			cmd->c_resp[2] = (v2 >> 8) | ((v3 & 0xff)  << 24);
			cmd->c_resp[3] = v3 >> 8;
#ifdef SDHC_DEBUG
			printf("resp[0] 0x%08x\nresp[1] 0x%08x\nresp[2] 0x%08x\nresp[3] 0x%08x\n", cmd->c_resp[0], cmd->c_resp[1], cmd->c_resp[2], cmd->c_resp[3]);
#endif
		} else  {
			cmd->c_resp[0] = HREAD4(sc, MMCHS_RSP10);
#ifdef SDHC_DEBUG
			printf("resp[0] 0x%08x\n", cmd->c_resp[0]);
#endif
		}
	}

	/*
	 * If the command has data to transfer in any direction,
	 * execute the transfer now.
	 */
	if (cmd->c_error == 0 && cmd->c_data != NULL)
		ommmc_transfer_data(sc, cmd);

#if 0
	/* Turn off the LED. */
	HCLR1(sc, SDHC_HOST_CTL, SDHC_LED_ON);
#endif

	DPRINTF(1,("%s: cmd %u done (flags=%#x error=%d)\n",
	    DEVNAME(sc), cmd->c_opcode, cmd->c_flags, cmd->c_error));
	SET(cmd->c_flags, SCF_ITSDONE);
}

int
ommmc_start_command(struct ommmc_softc *sc, struct sdmmc_command *cmd)
{
	uint32_t blksize = 0;
	uint32_t blkcount = 0;
	uint32_t command;
	int error;
	int s;

	DPRINTF(1,("%s: start cmd %u arg=%#x data=%p dlen=%d flags=%#x\n",
	    DEVNAME(sc), cmd->c_opcode, cmd->c_arg, cmd->c_data,
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
			    DEVNAME(sc), blksize);
			return (EINVAL);
		}
	}

	/* Check limit imposed by 9-bit block count. (1.7.2) */
	if (blkcount > MMCHS_BLK_NBLK_MAX) {
		printf("%s: too much data\n", DEVNAME(sc));
		return (EINVAL);
	}

	/* Prepare transfer mode register value. (2.2.5) */
	command = 0;
	if (ISSET(cmd->c_flags, SCF_CMD_READ))
		command |= MMCHS_CMD_DDIR;
	if (blkcount > 0) {
		command |= MMCHS_CMD_BCE;
		if (blkcount > 1) {
			command |= MMCHS_CMD_MSBS;
			/* XXX only for memory commands? */
			command |= MMCHS_CMD_ACEN;
		}
	}
#ifdef notyet
	if (ISSET(sc->flags, SHF_USE_DMA))
		command |= MMCHS_CMD_DE;
#endif

	/*
	 * Prepare command register value. (2.2.6)
	 */
	command |= (cmd->c_opcode << MMCHS_CMD_INDX_SHIFT) &
	   MMCHS_CMD_INDX_SHIFT_MASK;

	if (ISSET(cmd->c_flags, SCF_RSP_CRC))
		command |= MMCHS_CMD_CCCE;
	if (ISSET(cmd->c_flags, SCF_RSP_IDX))
		command |= MMCHS_CMD_CICE;
	if (cmd->c_data != NULL)
		command |= MMCHS_CMD_DP;

	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		command |= MMCHS_CMD_RESP_NONE;
	else if (ISSET(cmd->c_flags, SCF_RSP_136))
		command |= MMCHS_CMD_RESP136;
	else if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		command |= MMCHS_CMD_RESP48B;
	else
		command |= MMCHS_CMD_RESP48;

	/* Wait until command and data inhibit bits are clear. (1.5) */
	if ((error = ommmc_wait_state(sc, MMCHS_PSTATE_CMDI, 0)) != 0)
		return (error);

	s = splsdmmc();

#if 0
	/* Alert the user not to remove the card. */
	HSET1(sc, SDHC_HOST_CTL, SDHC_LED_ON);
#endif

	/* XXX: Set DMA start address if SHF_USE_DMA is set. */

	DPRINTF(1,("%s: cmd=%#x blksize=%d blkcount=%d\n",
	    DEVNAME(sc), command, blksize, blkcount));

	/*
	 * Start a CPU data transfer.  Writing to the high order byte
	 * of the SDHC_COMMAND register triggers the SD command. (1.5)
	 */
	HWRITE4(sc, MMCHS_BLK, (blkcount << MMCHS_BLK_NBLK_SHIFT) |
	    (blksize << MMCHS_BLK_BLEN_SHIFT));
	HWRITE4(sc, MMCHS_ARG, cmd->c_arg);
	HWRITE4(sc, MMCHS_CMD, command);

	splx(s);
	return (0);
}

void
ommmc_transfer_data(struct ommmc_softc *sc, struct sdmmc_command *cmd)
{
	uint8_t *datap = cmd->c_data;
	int i, datalen;
	int mask;
	int error;

	mask = ISSET(cmd->c_flags, SCF_CMD_READ) ?
	    MMCHS_PSTATE_BRE : MMCHS_PSTATE_BWE;
	error = 0;
	datalen = cmd->c_datalen;

	DPRINTF(1,("%s: resp=%#x datalen=%d\n", DEVNAME(sc),
	    MMC_R1(cmd->c_resp), datalen));

	while (datalen > 0) {
		if (!ommmc_wait_intr(sc, MMCHS_STAT_BRR| MMCHS_STAT_BWR,
		    SDHC_BUFFER_TIMEOUT)) {
			error = ETIMEDOUT;
			break;
		}

		if ((error = ommmc_wait_state(sc, mask, mask)) != 0)
			break;

		i = MIN(datalen, cmd->c_blklen);
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			ommmc_read_data(sc, datap, i);
		else
			ommmc_write_data(sc, datap, i);

		datap += i;
		datalen -= i;
	}

	if (error == 0 && !ommmc_wait_intr(sc, MMCHS_STAT_TC,
	    SDHC_TRANSFER_TIMEOUT))
		error = ETIMEDOUT;

	if (error != 0)
		cmd->c_error = error;
	SET(cmd->c_flags, SCF_ITSDONE);

	DPRINTF(1,("%s: data transfer done (error=%d)\n",
	    DEVNAME(sc), cmd->c_error));
}

void
ommmc_read_data(struct ommmc_softc *sc, uint8_t *datap, int datalen)
{
	while (datalen > 3) {
		*(uint32_t *)datap = HREAD4(sc, MMCHS_DATA);
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = HREAD4(sc, MMCHS_DATA);
		do {
			*datap++ = rv & 0xff;
			rv = rv >> 8;
		} while (--datalen > 0);
	}
}

void
ommmc_write_data(struct ommmc_softc *sc, uint8_t *datap, int datalen)
{
	while (datalen > 3) {
		DPRINTF(3,("%08x\n", *(uint32_t *)datap));
		HWRITE4(sc, MMCHS_DATA, *((uint32_t *)datap));
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = *datap++;
		if (datalen > 1)
			rv |= *datap++ << 8;
		if (datalen > 2)
			rv |= *datap++ << 16;
		DPRINTF(3,("rv %08x\n", rv));
		HWRITE4(sc, MMCHS_DATA, rv);
	}
}

/* Prepare for another command. */
int
ommmc_soft_reset(struct ommmc_softc *sc, int mask)
{

	int timo;

	DPRINTF(1,("%s: software reset reg=%#x\n", DEVNAME(sc), mask));

	HSET4(sc, MMCHS_SYSCTL, mask);
	/*
	 * If we read the software reset register too fast after writing it we
	 * can get back a zero that means the reset hasn't started yet rather
	 * than that the reset is complete. Per TI recommendations, work around
	 * it by reading until we see the reset bit asserted, then read until
	 * it's clear.
	 */
	for (timo = 1000; timo > 0; timo--) {
		if (ISSET(HREAD4(sc, MMCHS_SYSCTL), mask))
			break;
		delay(1);
	}
	for (timo = 1000; timo > 0; timo--) {
		if (!ISSET(HREAD4(sc, MMCHS_SYSCTL), mask))
			break;
		delay(10);
	}
	if (timo == 0) {
		DPRINTF(1,("%s: timeout reg=%#x\n", DEVNAME(sc),
		    HREAD4(sc, MMCHS_SYSCTL)));
		return (ETIMEDOUT);
	}

	return (0);
}

int
ommmc_wait_intr(struct ommmc_softc *sc, int mask, int sec)
{
	int status;
	int s;

	mask |= MMCHS_STAT_ERRI;

	s = splsdmmc();
	status = sc->intr_status & mask;
	while (status == 0) {
		if (tsleep_nsec(&sc->intr_status, PWAIT, "hcintr",
		    SEC_TO_NSEC(sec)) == EWOULDBLOCK) {
			status |= MMCHS_STAT_ERRI;
			break;
		}
		status = sc->intr_status & mask;
	}
	sc->intr_status &= ~status;

	DPRINTF(2,("%s: intr status %#x error %#x\n", DEVNAME(sc), status,
	    sc->intr_error_status));

	/* Command timeout has higher priority than command complete. */
	if (ISSET(status, MMCHS_STAT_ERRI)) {
		sc->intr_error_status = 0;
		(void)ommmc_soft_reset(sc, MMCHS_SYSCTL_SRC|MMCHS_SYSCTL_SRD);
		status = 0;
	}

	splx(s);
	return (status);
}

/*
 * Established by attachment driver at interrupt priority IPL_SDMMC.
 */
int
ommmc_intr(void *arg)
{
	struct ommmc_softc *sc = arg;

	uint32_t status;

	/* Find out which interrupts are pending. */
	status = HREAD4(sc, MMCHS_STAT);

	/* Acknowledge the interrupts we are about to handle. */
	HWRITE4(sc, MMCHS_STAT, status);
	DPRINTF(2,("%s: interrupt status=%b\n", DEVNAME(sc),
	    status, MMCHS_STAT_FMT));

	/*
	 * Service error interrupts.
	 */
	if (ISSET(status, MMCHS_STAT_ERRI)) {
		if (ISSET(status, MMCHS_STAT_CTO|
		    MMCHS_STAT_DTO)) {
			sc->intr_status |= status;
			sc->intr_error_status |= status & 0xffff0000;
			wakeup(&sc->intr_status);
		}
	}

#if 0
	/*
	 * Wake up the sdmmc event thread to scan for cards.
	 */
	if (ISSET(status, SDHC_CARD_REMOVAL|SDHC_CARD_INSERTION))
		ommmc_needs_discover(sc->sdmmc);
#endif

	/*
	 * Wake up the blocking process to service command
	 * related interrupt(s).
	 */
	if (ISSET(status, MMCHS_STAT_BRR|
	    MMCHS_STAT_BWR|MMCHS_STAT_TC|
	    MMCHS_STAT_CC)) {
		sc->intr_status |= status;
		wakeup(&sc->intr_status);
	}

	/*
	 * Service SD card interrupts.
	 */
	if (ISSET(status, MMCHS_STAT_CIRQ)) {
		DPRINTF(0,("%s: card interrupt\n", DEVNAME(sc)));
		HCLR4(sc, MMCHS_STAT, MMCHS_STAT_CIRQ);
		sdmmc_card_intr(sc->sdmmc);
	}
	return 1;
}

#ifdef SDHC_DEBUG
void
ommmc_dump_regs(struct ommmc_softc *sc)
{
}
#endif
