/*	$OpenBSD: dwmshc.c,v 1.8 2024/07/15 09:56:30 patrick Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/sdmmc/sdhcreg.h>
#include <dev/sdmmc/sdhcvar.h>
#include <dev/sdmmc/sdmmcvar.h>

#define EMMC_VER_ID			0x500
#define EMMC_VER_TYPE			0x504
#define EMMC_HOST_CTRL3			0x508 /* B */
#define  EMMC_HOST_CTRL3_CMD_CONFLICT_CHECK	(1U << 0)
#define  EMMC_HOST_CTRL3_SW_CG_DIS		(1U << 4)
#define EMMC_EMMC_CTRL			0x52c /* HW */
#define  EMMC_EMMC_CTRL_CARD_IS_EMMC		(1U << 0) 
#define  EMMC_EMMC_CTRL_DISABLE_DATA_CRC_CHK	(1U << 1)
#define  EMMC_EMMC_CTRL_EMMC_RST_N		(1U << 2)
#define  EMMC_EMMC_CTRL_EMMC_RST_N_OE		(1U << 3)
#define  EMMC_EMMC_CTRL_ENH_STROBE_ENABLE	(1U << 8)
#define  EMMC_EMMC_CTRL_CQE_ALGO_SEL		(1U << 9)
#define  EMMC_EMMC_CTRL_CQE_PREFETCH_DISABLE	(1U << 10)
#define EMMC_BOOT_CTRL			0x52e /* HW */
#define  EMMC_BOOT_CTRL_MAN_BOOT_EN		(1U << 0)
#define  EMMC_BOOT_CTRL_VALIDATE_BOOT		(1U << 1)
#define  EMMC_BOOT_CTRL_BOOT_ACK_ENABLE		(1U << 8)
#define  EMMC_BOOT_CTRL_BOOT_TOUT_CNT_SHIFT	12
#define  EMMC_BOOT_CTRL_BOOT_TOUT_CNT_MASK	0xf
#define EMMC_AT_CTRL			0x540
#define  EMMC_AT_CTRL_SWIN_TH_EN		(1U << 2)
#define  EMMC_AT_CTRL_RPT_TUNE_ERR		(1U << 3)
#define  EMMC_AT_CTRL_SW_TUNE_EN		(1U << 4)
#define  EMMC_AT_CTRL_TUNE_CLK_STOP_EN		(1U << 16)
#define  EMMC_AT_CTRL_PRE_CHANGE_DLY_MASK	(0x3 << 17)
#define  EMMC_AT_CTRL_PRE_CHANGE_DLY_LT1	(0x0 << 17)
#define  EMMC_AT_CTRL_PRE_CHANGE_DLY_LT2	(0x1 << 17)
#define  EMMC_AT_CTRL_PRE_CHANGE_DLY_LT3	(0x2 << 17)
#define  EMMC_AT_CTRL_PRE_CHANGE_DLY_LT4	(0x3 << 17)
#define  EMMC_AT_CTRL_POST_CHANGE_DLY_MASK	(0x3 << 19)
#define  EMMC_AT_CTRL_POST_CHANGE_DLY_LT1	(0x0 << 19)
#define  EMMC_AT_CTRL_POST_CHANGE_DLY_LT2	(0x1 << 19)
#define  EMMC_AT_CTRL_POST_CHANGE_DLY_LT3	(0x2 << 19)
#define  EMMC_AT_CTRL_POST_CHANGE_DLY_LT4	(0x3 << 19)
#define EMMC_AT_STAT			0x544
#define  EMMC_AT_STAT_CENTER_PH_CODE_SHIFT	0
#define  EMMC_AT_STAT_CENTER_PH_CODE_MASK	0xff
#define  EMMC_AT_STAT_R_EDGE_PH_CODE_SHIFT	8
#define  EMMC_AT_STAT_R_EDGE_PH_CODE_MASK	0xff
#define  EMMC_AT_STAT_L_EDGE_PH_CODE_SHIFT	16
#define  EMMC_AT_STAT_L_EDGE_PH_CODE_MASK	0xff
#define EMMC_DLL_CTRL			0x800
#define  EMMC_DLL_CTRL_DLL_START		(1U << 0)
#define  EMMC_DLL_CTRL_DLL_SRST			(1U << 1)
#define  EMMC_DLL_CTRL_DLL_INCREMENT_SHIFT	8
#define  EMMC_DLL_CTRL_DLL_INCREMENT_MASK	0xff
#define  EMMC_DLL_CTRL_DLL_START_POINT_SHIFT	16
#define  EMMC_DLL_CTRL_DLL_START_POINT_MASK	0xff
#define  EMMC_DLL_CTRL_DLL_BYPASS_MODE		(1U << 24)
#define EMMC_DLL_RXCLK			0x804
#define  EMMC_DLL_RXCLK_RX_TAP_NUM_SHIFT	0
#define  EMMC_DLL_RXCLK_RX_TAP_NUM_MASK		0x1f
#define  EMMC_DLL_RXCLK_RX_TAP_VALUE_SHIFT	8
#define  EMMC_DLL_RXCLK_RX_TAP_VALUE_MASK	0xff
#define  EMMC_DLL_RXCLK_RX_DELAY_NUM_SHIFT	16
#define  EMMC_DLL_RXCLK_RX_DELAY_NUM_MASK	0xff
#define  EMMC_DLL_RXCLK_RX_TAP_NUM_SEL		(1U << 24)
#define  EMMC_DLL_RXCLK_RX_TAP_VALUE_SEL	(1U << 25)
#define  EMMC_DLL_RXCLK_RX_DELAY_NUM_SEL	(1U << 26)
#define  EMMC_DLL_RXCLK_RX_CLK_OUT_SEL		(1U << 27)
#define  EMMC_DLL_RXCLK_RX_CLK_CHANGE_WINDOW	(1U << 28)
#define  EMMC_DLL_RXCLK_RX_CLK_SRC_SEL		(1U << 29)
#define EMMC_DLL_TXCLK			0x808
#define  EMMC_DLL_TXCLK_TX_TAP_NUM_SHIFT	0
#define  EMMC_DLL_TXCLK_TX_TAP_NUM_MASK		0x1f
#define  EMMC_DLL_TXCLK_TX_TAP_NUM_90_DEG	0x8
#define  EMMC_DLL_TXCLK_TX_TAP_NUM_DEFAULT	0x10
#define  EMMC_DLL_TXCLK_TX_TAP_VALUE_SHIFT	8
#define  EMMC_DLL_TXCLK_TX_TAP_VALUE_MASK	0xff
#define  EMMC_DLL_TXCLK_TX_DELAY_SHIFT		16
#define  EMMC_DLL_TXCLK_TX_DELAY_MASK		0xff
#define  EMMC_DLL_TXCLK_TX_TAP_NUM_SEL		(1U << 24)
#define  EMMC_DLL_TXCLK_TX_TAP_VALUE_SEL	(1U << 25)
#define  EMMC_DLL_TXCLK_TX_DELAY_SEL		(1U << 26)
#define  EMMC_DLL_TXCLK_TX_CLK_OUT_SEL		(1U << 27)
#define EMMC_DLL_STRBIN			0x80c
#define  EMMC_DLL_STRBIN_TAP_NUM_SHIFT		0
#define  EMMC_DLL_STRBIN_TAP_NUM_MASK		0x1f
#define  EMMC_DLL_STRBIN_TAP_NUM_90_DEG		0x8
#define  EMMC_DLL_STRBIN_TAP_VALUE_SHIFT	8
#define  EMMC_DLL_STRBIN_TAP_VALUE_MASK		0xff
#define  EMMC_DLL_STRBIN_DELAY_NUM_SHIFT	16
#define  EMMC_DLL_STRBIN_DELAY_NUM_MASK		0xff
#define  EMMC_DLL_STRBIN_DELAY_NUM_DEFAULT	0x16
#define  EMMC_DLL_STRBIN_TAP_NUM_SEL		(1U << 24)
#define  EMMC_DLL_STRBIN_TAP_VALUE_SEL		(1U << 25)
#define  EMMC_DLL_STRBIN_DELAY_NUM_SEL		(1U << 26)
#define  EMMC_DLL_STRBIN_DELAY_ENA		(1U << 27)
#define EMMC_DLL_CMDOUT			0x810
#define  EMMC_DLL_CMDOUT_TAP_NUM_SHIFT		0
#define  EMMC_DLL_CMDOUT_TAP_NUM_MASK		0x1f
#define  EMMC_DLL_CMDOUT_TAP_NUM_90_DEG	0x8
#define  EMMC_DLL_CMDOUT_TAP_VALUE_SHIFT	8
#define  EMMC_DLL_CMDOUT_TAP_VALUE_MASK		0xff
#define  EMMC_DLL_CMDOUT_DELAY_NUM_SHIFT	16
#define  EMMC_DLL_CMDOUT_DELAY_NUM_MASK		0xff
#define  EMMC_DLL_CMDOUT_TAP_NUM_SEL		(1U << 24)
#define  EMMC_DLL_CMDOUT_TAP_VALUE_SEL		(1U << 25)
#define  EMMC_DLL_CMDOUT_DELAY_NUM_SEL		(1U << 26)
#define  EMMC_DLL_CMDOUT_DELAY_ENA		(1U << 27)
#define  EMMC_DLL_CMDOUT_SRC_SEL		(1U << 28)
#define  EMMC_DLL_CMDOUT_EN_SRC_SEL		(1U << 29)
#define EMMC_DLL_STATUS0		0x840
#define  EMMC_DLL_STATUS0_DLL_LOCK_VALUE_SHIFT	0
#define  EMMC_DLL_STATUS0_DLL_LOCK_VALUE_MASK	0xff
#define  EMMC_DLL_STATUS0_DLL_LOCK		(1U << 8)
#define  EMMC_DLL_STATUS0_DLL_LOCK_TIMEOUT	(1U << 9)
#define EMMC_DLL_STATUS1		0x844
#define  EMMC_DLL_STATUS1_DLL_TXCLK_DELAY_VALUE_SHIFT \
						0
#define  EMMC_DLL_STATUS1_DLL_TXCLK_DELAY_VALUE_MASK \
						0xff
#define  EMMC_DLL_STATUS1_DLL_RXCLK_DELAY_VALUE_SHIFT \
						8
#define  EMMC_DLL_STATUS1_DLL_RXCLK_DELAY_VALUE_MASK \
						0xff
#define  EMMC_DLL_STATUS1_DLL_STRBIN_DELAY_VALUE_SHIFT \
						16
#define  EMMC_DLL_STATUS1_DLL_STRBIN_DELAY_VALUE_MASK \
						0xff

struct dwmshc_softc {
	struct sdhc_softc	 sc_sdhc;

	int			 sc_node;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_size_t		 sc_ios;
	bus_dma_tag_t		 sc_dmat;
	void			*sc_ih;

	struct sdhc_host	*sc_host;
};

static int		 dwmshc_match(struct device *, void *, void *);
static void		 dwmshc_attach(struct device *, struct device *,
			     void *);

static inline void	 dwmshc_wr1(struct dwmshc_softc *,
			     bus_size_t, uint8_t);
static inline void	 dwmshc_wr4(struct dwmshc_softc *,
			     bus_size_t, uint32_t);
static inline uint32_t	 dwmshc_rd4(struct dwmshc_softc *, bus_size_t);

static void		 dwmshc_clock_pre(struct sdhc_softc *, int, int);
static void		 dwmshc_clock_post(struct sdhc_softc *, int, int);
static int		 dwmshc_non_removable(struct sdhc_softc *);

const struct cfattach dwmshc_ca = {
	sizeof(struct dwmshc_softc), dwmshc_match, dwmshc_attach
};

struct cfdriver dwmshc_cd = {
	NULL, "dwmshc", DV_DULL
};

int
dwmshc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3568-dwcmshc") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-dwcmshc"));
}
static void
dwmshc_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwmshc_softc *sc = (struct dwmshc_softc *)self;
	struct sdhc_softc *sdhc = &sc->sc_sdhc;
	struct fdt_attach_args *faa = aux;
	uint64_t capmask = 0;
	uint16_t capset = 0;
	int bus_width;
	uint32_t freq;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(sc->sc_node, "default");

	clock_set_assigned(sc->sc_node);
	clock_enable_all(sc->sc_node);
	reset_deassert_all(sc->sc_node);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    sdhc_intr, sdhc, DEVNAME(sdhc));
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	/* Disable Command Conflict Check */
	dwmshc_wr1(sc, EMMC_HOST_CTRL3, 0);

	sdhc->sc_host = &sc->sc_host;
	sdhc->sc_dmat = faa->fa_dmat;
	sdhc->sc_dma_boundary = 128 * 1024 * 1024;

	sdhc->sc_bus_clock_pre = dwmshc_clock_pre;
	sdhc->sc_bus_clock_post = dwmshc_clock_post;

	if (OF_getpropbool(sc->sc_node, "non-removable")) {
		SET(sdhc->sc_flags, SDHC_F_NONREMOVABLE);
		sdhc->sc_card_detect = dwmshc_non_removable;
	}

	bus_width = OF_getpropint(faa->fa_node, "bus-width", 1);
	if (bus_width < 8)
		SET(capmask, SDHC_8BIT_MODE_SUPP);

	freq = clock_get_frequency(faa->fa_node, "block");
	sdhc->sc_clkbase = freq / 1000;

	SET(sdhc->sc_flags, SDHC_F_NOPWR0);
	SET(capmask, (uint64_t)SDHC_DDR50_SUPP << 32);

	sdhc_host_found(sdhc, sc->sc_iot, sc->sc_ioh, sc->sc_ios, 1,
	    capmask, capset);
}

static void
dwmshc_clock_pre(struct sdhc_softc *sdhc, int freq, int timing)
{
#if 0
	struct dwmshc_softc *sc = (struct dwmshc_softc *)sdhc;

	/*
	 * before switching to hs400es the driver needs to enable
	 * enhanced strobe. i think this is the right place to do
	 * that.
	 */
	if (timing == hs400es) {
		dwmshc_wr4(sc, EMMC_DLL_STRBIN, EMMC_DLL_STRBIN_DELAY_ENA |
		    EMMC_DLL_STRBIN_DELAY_NUM_SEL |
		    (EMMC_DLL_STRBIN_DELAY_NUM_DEFAULT <<
		     EMMC_DLL_STRBIN_DELAY_NUM_SHIFT));
	}
#endif
}

static int
dwmshc_dll_wait(struct dwmshc_softc *sc)
{
	uint32_t status0;
	int i;

	for (i = 0; i < 500 * 1000; i++) {
		delay(1);

		status0 = dwmshc_rd4(sc, EMMC_DLL_STATUS0);
		if (ISSET(status0, EMMC_DLL_STATUS0_DLL_LOCK)) {
			if (ISSET(status0, EMMC_DLL_STATUS0_DLL_LOCK_TIMEOUT)) {
				printf("%s: lock timeout\n",
				    DEVNAME(&sc->sc_sdhc));
				return (EIO);
			}
			return (0);
		}
	}

	printf("%s: poll timeout\n", DEVNAME(&sc->sc_sdhc));
	return (ETIMEDOUT);
}

static void
dwmshc_clock_post(struct sdhc_softc *sdhc, int freq, int timing)
{
	struct dwmshc_softc *sc = (struct dwmshc_softc *)sdhc;
	uint32_t txclk_tapnum = EMMC_DLL_TXCLK_TX_TAP_NUM_DEFAULT;

	clock_set_frequency(sc->sc_node, 0, freq * 1000);

	if (timing == SDMMC_TIMING_LEGACY) { /* disable dll */
		/*
                 * the bypass and start bits need to be set if dll
		 * is not locked.
		 */
		dwmshc_wr4(sc, EMMC_DLL_CTRL,
		    EMMC_DLL_CTRL_DLL_START | EMMC_DLL_CTRL_DLL_BYPASS_MODE);
		dwmshc_wr4(sc, EMMC_DLL_RXCLK, 0);
		dwmshc_wr4(sc, EMMC_DLL_TXCLK, 0);
		dwmshc_wr4(sc, EMMC_DLL_STRBIN, 0);
		return;
	}

	dwmshc_wr4(sc, EMMC_DLL_CTRL, EMMC_DLL_CTRL_DLL_SRST);
	delay(1);
	dwmshc_wr4(sc, EMMC_DLL_CTRL, 0);

	if (OF_is_compatible(sc->sc_node, "rockchip,rk3568-dwcmshc"))
		dwmshc_wr4(sc, EMMC_DLL_RXCLK, EMMC_DLL_RXCLK_RX_CLK_OUT_SEL |
		    EMMC_DLL_RXCLK_RX_CLK_SRC_SEL);
	else
		dwmshc_wr4(sc, EMMC_DLL_RXCLK, EMMC_DLL_RXCLK_RX_CLK_OUT_SEL);
	dwmshc_wr4(sc, EMMC_DLL_CTRL, EMMC_DLL_CTRL_DLL_START |
	    0x5 << EMMC_DLL_CTRL_DLL_START_POINT_SHIFT |
	    0x2 << EMMC_DLL_CTRL_DLL_INCREMENT_SHIFT);

	if (dwmshc_dll_wait(sc) != 0)
		return;

	dwmshc_wr4(sc, EMMC_AT_CTRL, EMMC_AT_CTRL_TUNE_CLK_STOP_EN |
	    EMMC_AT_CTRL_PRE_CHANGE_DLY_LT4 |
	    EMMC_AT_CTRL_POST_CHANGE_DLY_LT4);

	if (timing >= SDMMC_TIMING_MMC_HS200) {
		txclk_tapnum = OF_getpropint(sc->sc_node,
		    "rockchip,txclk-tapnum", txclk_tapnum);

#ifdef notyet
		if (OF_is_compatible(sc->sc_node, "rockchip,rk3588-dwcmshc") &&
		    timing == SDMMC_TIMING_MMC_HS400) {
			txclk_tapnum = EMMC_DLL_TXCLK_TX_TAP_NUM_90_DEG;
			dwmshc_wr4(sc, EMMC_DLL_CMDOUT,
			    EMMC_DLL_CMDOUT_TAP_NUM_90_DEG |
			    EMMC_DLL_CMDOUT_TAP_NUM_SEL |
			    EMMC_DLL_CMDOUT_DELAY_ENA |
			    EMMC_DLL_CMDOUT_SRC_SEL |
			    EMMC_DLL_CMDOUT_EN_SRC_SEL);
		}
#endif
	}

	dwmshc_wr4(sc, EMMC_DLL_TXCLK, EMMC_DLL_TXCLK_TX_CLK_OUT_SEL |
	    EMMC_DLL_TXCLK_TX_TAP_NUM_SEL |
	    txclk_tapnum << EMMC_DLL_TXCLK_TX_TAP_NUM_SHIFT);
	dwmshc_wr4(sc, EMMC_DLL_STRBIN, EMMC_DLL_STRBIN_DELAY_ENA |
	    EMMC_DLL_STRBIN_TAP_NUM_SEL |
	    (EMMC_DLL_STRBIN_TAP_NUM_90_DEG <<
	     EMMC_DLL_STRBIN_TAP_NUM_SHIFT));
}

static int
dwmshc_non_removable(struct sdhc_softc *sdhc)
{
	return (1);
}

static inline void
dwmshc_wr1(struct dwmshc_softc *sc, bus_size_t off, uint8_t v)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, v);
}

static inline void
dwmshc_wr4(struct dwmshc_softc *sc, bus_size_t off, uint32_t v)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, v);
}

static inline uint32_t
dwmshc_rd4(struct dwmshc_softc *sc, bus_size_t off)
{
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, off));
}
