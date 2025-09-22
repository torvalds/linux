/* $OpenBSD: imxccm.c,v 1.29 2022/06/28 23:43:12 naddy Exp $ */
/*
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/fdt/imxanatopvar.h>

/* registers */
#define CCM_CCR		0x00
#define CCM_CCDR	0x04
#define CCM_CSR		0x08
#define CCM_CCSR	0x0c
#define CCM_CACRR	0x10
#define CCM_CBCDR	0x14
#define CCM_CBCMR	0x18
#define CCM_CSCMR1	0x1c
#define CCM_CSCMR2	0x20
#define CCM_CSCDR1	0x24
#define CCM_CS1CDR	0x28
#define CCM_CS2CDR	0x2c
#define CCM_CDCDR	0x30
#define CCM_CHSCCDR	0x34
#define CCM_CSCDR2	0x38
#define CCM_CSCDR3	0x3c
#define CCM_CSCDR4	0x40
#define CCM_CDHIPR	0x48
#define CCM_CDCR	0x4c
#define CCM_CTOR	0x50
#define CCM_CLPCR	0x54
#define CCM_CISR	0x58
#define CCM_CIMR	0x5c
#define CCM_CCOSR	0x60
#define CCM_CGPR	0x64
#define CCM_CCGR0	0x68
#define CCM_CCGR1	0x6c
#define CCM_CCGR2	0x70
#define CCM_CCGR3	0x74
#define CCM_CCGR4	0x78
#define CCM_CCGR5	0x7c
#define CCM_CCGR6	0x80
#define CCM_CCGR7	0x84
#define CCM_CMEOR	0x88

/* bits and bytes */
#define CCM_CCSR_PLL3_SW_CLK_SEL		(1 << 0)
#define CCM_CCSR_PLL2_SW_CLK_SEL		(1 << 1)
#define CCM_CCSR_PLL1_SW_CLK_SEL		(1 << 2)
#define CCM_CCSR_STEP_SEL			(1 << 8)
#define CCM_CBCDR_IPG_PODF_SHIFT		8
#define CCM_CBCDR_IPG_PODF_MASK			0x3
#define CCM_CBCDR_AHB_PODF_SHIFT		10
#define CCM_CBCDR_AHB_PODF_MASK			0x7
#define CCM_CBCDR_PERIPH_CLK_SEL_SHIFT		25
#define CCM_CBCDR_PERIPH_CLK_SEL_MASK		0x1
#define CCM_CBCMR_PERIPH_CLK2_SEL_SHIFT		12
#define CCM_CBCMR_PERIPH_CLK2_SEL_MASK		0x3
#define CCM_CBCMR_PRE_PERIPH_CLK_SEL_SHIFT	18
#define CCM_CBCMR_PRE_PERIPH_CLK_SEL_MASK	0x3
#define CCM_CSCDR1_USDHCx_CLK_SEL_SHIFT(x)	((x) + 15)
#define CCM_CSCDR1_USDHCx_CLK_SEL_MASK		0x1
#define CCM_CSCDR1_USDHCx_PODF_MASK		0x7
#define CCM_CSCDR1_UART_PODF_MASK		0x7
#define CCM_CSCDR2_ECSPI_PODF_SHIFT		19
#define CCM_CSCDR2_ECSPI_PODF_MASK		0x7
#define CCM_CCGR1_ENET				(3 << 10)
#define CCM_CCGR4_125M_PCIE			(3 << 0)
#define CCM_CCGR5_100M_SATA			(3 << 4)
#define CCM_CSCMR1_PERCLK_CLK_PODF_MASK		0x1f
#define CCM_CSCMR1_PERCLK_CLK_SEL_MASK		(1 << 6)

/* Analog registers */
#define CCM_ANALOG_PLL_USB1			0x0010
#define CCM_ANALOG_PLL_USB1_SET			0x0014
#define CCM_ANALOG_PLL_USB1_CLR			0x0018
#define  CCM_ANALOG_PLL_USB1_LOCK		(1U << 31)
#define  CCM_ANALOG_PLL_USB1_BYPASS		(1 << 16)
#define  CCM_ANALOG_PLL_USB1_ENABLE		(1 << 13)
#define  CCM_ANALOG_PLL_USB1_POWER		(1 << 12)
#define  CCM_ANALOG_PLL_USB1_EN_USB_CLKS	(1 << 6)
#define CCM_ANALOG_PLL_USB2			0x0020
#define CCM_ANALOG_PLL_USB2_SET			0x0024
#define CCM_ANALOG_PLL_USB2_CLR			0x0028
#define  CCM_ANALOG_PLL_USB2_LOCK		(1U << 31)
#define  CCM_ANALOG_PLL_USB2_BYPASS		(1 << 16)
#define  CCM_ANALOG_PLL_USB2_ENABLE		(1 << 13)
#define  CCM_ANALOG_PLL_USB2_POWER		(1 << 12)
#define  CCM_ANALOG_PLL_USB2_EN_USB_CLKS	(1 << 6)
#define CCM_ANALOG_PLL_ENET			0x00e0
#define CCM_ANALOG_PLL_ENET_SET			0x00e4
#define CCM_ANALOG_PLL_ENET_CLR			0x00e8
#define  CCM_ANALOG_PLL_ENET_LOCK		(1U << 31)
#define  CCM_ANALOG_PLL_ENET_ENABLE_100M	(1 << 20) /* i.MX6 */
#define  CCM_ANALOG_PLL_ENET_BYPASS		(1 << 16)
#define  CCM_ANALOG_PLL_ENET_ENABLE		(1 << 13) /* i.MX6 */
#define  CCM_ANALOG_PLL_ENET_POWERDOWN		(1 << 12) /* i.MX6 */
#define  CCM_ANALOG_PLL_ENET_ENABLE_CLK_125MHZ	(1 << 10) /* i.MX7 */

/* Int PLL */
#define CCM_14XX_IMX8M_ARM_PLL_GNRL_CTL		0x84
#define CCM_14XX_IMX8M_ARM_PLL_DIV_CTL		0x88
#define CCM_14XX_IMX8M_SYS_PLL1_GNRL_CTL	0x94
#define CCM_14XX_IMX8M_SYS_PLL1_DIV_CTL		0x98
#define  CCM_INT_PLL_LOCK				(1U << 31)
#define  CCM_INT_PLL_LOCK_SEL				(1 << 29)
#define  CCM_INT_PLL_RST				(1 << 9)
#define  CCM_INT_PLL_BYPASS				(1 << 4)
#define  CCM_INT_PLL_MAIN_DIV_SHIFT			12
#define  CCM_INT_PLL_MAIN_DIV_MASK			0x3ff
#define  CCM_INT_PLL_PRE_DIV_SHIFT			4
#define  CCM_INT_PLL_PRE_DIV_MASK			0x3f
#define  CCM_INT_PLL_POST_DIV_SHIFT			0
#define  CCM_INT_PLL_POST_DIV_MASK			0x7

/* Frac PLL */
#define CCM_FRAC_IMX8M_ARM_PLL0			0x28
#define CCM_FRAC_IMX8M_ARM_PLL1			0x2c
#define  CCM_FRAC_PLL_LOCK				(1U << 31)
#define  CCM_FRAC_PLL_ENABLE				(1 << 21)
#define  CCM_FRAC_PLL_POWERDOWN				(1 << 19)
#define  CCM_FRAC_PLL_REFCLK_SEL_SHIFT			16
#define  CCM_FRAC_PLL_REFCLK_SEL_MASK			0x3
#define  CCM_FRAC_PLL_LOCK_SEL				(1 << 15)
#define  CCM_FRAC_PLL_BYPASS				(1 << 14)
#define  CCM_FRAC_PLL_COUNTCLK_SEL			(1 << 13)
#define  CCM_FRAC_PLL_NEWDIV_VAL			(1 << 12)
#define  CCM_FRAC_PLL_NEWDIV_ACK			(1 << 11)
#define  CCM_FRAC_PLL_REFCLK_DIV_VAL_SHIFT		5
#define  CCM_FRAC_PLL_REFCLK_DIV_VAL_MASK		0x3f
#define  CCM_FRAC_PLL_OUTPUT_DIV_VAL_SHIFT		0
#define  CCM_FRAC_PLL_OUTPUT_DIV_VAL_MASK		0x1f
#define  CCM_FRAC_PLL_FRAC_DIV_CTL_SHIFT		7
#define  CCM_FRAC_PLL_FRAC_DIV_CTL_MASK			0x1ffffff
#define  CCM_FRAC_PLL_INT_DIV_CTL_SHIFT			0
#define  CCM_FRAC_PLL_INT_DIV_CTL_MASK			0x7f
#define  CCM_FRAC_PLL_DENOM				(1 << 24)
#define CCM_FRAC_IMX8M_PLLOUT_DIV_CFG		0x78
#define  CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_SHIFT	20
#define  CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_MASK		0x7

#define HCLK_FREQ				24000000
#define PLL3_60M				60000000
#define PLL3_80M				80000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxccm_gate {
	uint16_t reg;
	uint8_t pos;
	uint16_t parent;
};

struct imxccm_divider {
	uint16_t reg;
	uint16_t shift;
	uint16_t mask;
	uint16_t parent;
	uint16_t fixed;
};

struct imxccm_mux {
	uint16_t reg;
	uint16_t shift;
	uint16_t mask;
};

#include "imxccm_clocks.h"

struct imxccm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;
	uint32_t		sc_phandle;

	struct regmap		*sc_anatop;

	const struct imxccm_gate *sc_gates;
	int			sc_ngates;
	const struct imxccm_divider *sc_divs;
	int			sc_ndivs;
	const struct imxccm_mux	*sc_muxs;
	int			sc_nmuxs;
	const struct imxccm_divider *sc_predivs;
	int			sc_npredivs;
	struct clock_device	sc_cd;
};

int	imxccm_match(struct device *, void *, void *);
void	imxccm_attach(struct device *parent, struct device *self, void *args);

const struct cfattach	imxccm_ca = {
	sizeof (struct imxccm_softc), imxccm_match, imxccm_attach
};

struct cfdriver imxccm_cd = {
	NULL, "imxccm", DV_DULL
};

uint32_t imxccm_get_armclk(struct imxccm_softc *);
void imxccm_armclk_set_parent(struct imxccm_softc *, enum imxanatop_clocks);
uint32_t imxccm_get_usdhx(struct imxccm_softc *, int x);
uint32_t imxccm_get_periphclk(struct imxccm_softc *);
uint32_t imxccm_get_ahbclk(struct imxccm_softc *);
uint32_t imxccm_get_ipgclk(struct imxccm_softc *);
uint32_t imxccm_get_ipg_perclk(struct imxccm_softc *);
uint32_t imxccm_get_uartclk(struct imxccm_softc *);
uint32_t imxccm_imx8mm_enet(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mm_ahb(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mm_i2c(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mm_uart(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mm_usdhc(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mp_enet_qos_timer(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mp_enet_qos(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mp_hsio_axi(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_ecspi(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_enet(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_ahb(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_i2c(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_pwm(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_uart(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_usdhc(struct imxccm_softc *sc, uint32_t);
uint32_t imxccm_imx8mq_usb(struct imxccm_softc *sc, uint32_t);
int imxccm_imx8m_set_div(struct imxccm_softc *, uint32_t, uint64_t, uint64_t);
void imxccm_enable(void *, uint32_t *, int);
uint32_t imxccm_get_frequency(void *, uint32_t *);
int imxccm_set_frequency(void *, uint32_t *, uint32_t);
int imxccm_set_parent(void *, uint32_t *, uint32_t *);

int
imxccm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx6q-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sl-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sx-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6ul-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx7d-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mm-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mp-ccm") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-ccm"));
}

void
imxccm_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxccm_softc *sc = (struct imxccm_softc *)self;
	struct fdt_attach_args *faa = aux;

	KASSERT(faa->fa_nreg >= 1);

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	sc->sc_phandle = OF_getpropint(sc->sc_node, "phandle", 0);

	if (OF_is_compatible(sc->sc_node, "fsl,imx8mm-ccm")) {
		sc->sc_anatop = regmap_bycompatible("fsl,imx8mm-anatop");
		KASSERT(sc->sc_anatop != NULL);
		sc->sc_gates = imx8mm_gates;
		sc->sc_ngates = nitems(imx8mm_gates);
		sc->sc_divs = imx8mm_divs;
		sc->sc_ndivs = nitems(imx8mm_divs);
		sc->sc_muxs = imx8mm_muxs;
		sc->sc_nmuxs = nitems(imx8mm_muxs);
		sc->sc_predivs = imx8mm_predivs;
		sc->sc_npredivs = nitems(imx8mm_predivs);
	} else if (OF_is_compatible(sc->sc_node, "fsl,imx8mp-ccm")) {
		sc->sc_anatop = regmap_bycompatible("fsl,imx8mp-anatop");
		KASSERT(sc->sc_anatop != NULL);
		sc->sc_gates = imx8mp_gates;
		sc->sc_ngates = nitems(imx8mp_gates);
		sc->sc_divs = imx8mp_divs;
		sc->sc_ndivs = nitems(imx8mp_divs);
		sc->sc_muxs = imx8mp_muxs;
		sc->sc_nmuxs = nitems(imx8mp_muxs);
		sc->sc_predivs = imx8mp_predivs;
		sc->sc_npredivs = nitems(imx8mp_predivs);
	} else if (OF_is_compatible(sc->sc_node, "fsl,imx8mq-ccm")) {
		sc->sc_anatop = regmap_bycompatible("fsl,imx8mq-anatop");
		KASSERT(sc->sc_anatop != NULL);
		sc->sc_gates = imx8mq_gates;
		sc->sc_ngates = nitems(imx8mq_gates);
		sc->sc_divs = imx8mq_divs;
		sc->sc_ndivs = nitems(imx8mq_divs);
		sc->sc_muxs = imx8mq_muxs;
		sc->sc_nmuxs = nitems(imx8mq_muxs);
		sc->sc_predivs = imx8mq_predivs;
		sc->sc_npredivs = nitems(imx8mq_predivs);
	} else if (OF_is_compatible(sc->sc_node, "fsl,imx7d-ccm")) {
		sc->sc_gates = imx7d_gates;
		sc->sc_ngates = nitems(imx7d_gates);
		sc->sc_divs = imx7d_divs;
		sc->sc_ndivs = nitems(imx7d_divs);
		sc->sc_muxs = imx7d_muxs;
		sc->sc_nmuxs = nitems(imx7d_muxs);
	} else if (OF_is_compatible(sc->sc_node, "fsl,imx6ul-ccm")) {
		sc->sc_gates = imx6ul_gates;
		sc->sc_ngates = nitems(imx6ul_gates);
	} else {
		sc->sc_gates = imx6_gates;
		sc->sc_ngates = nitems(imx6_gates);
	}

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = imxccm_enable;
	sc->sc_cd.cd_get_frequency = imxccm_get_frequency;
	sc->sc_cd.cd_set_frequency = imxccm_set_frequency;
	sc->sc_cd.cd_set_parent = imxccm_set_parent;
	clock_register(&sc->sc_cd);
}

uint32_t
imxccm_get_armclk(struct imxccm_softc *sc)
{
	uint32_t ccsr = HREAD4(sc, CCM_CCSR);

	if (!(ccsr & CCM_CCSR_PLL1_SW_CLK_SEL))
		return imxanatop_decode_pll(ARM_PLL1, HCLK_FREQ);
	else if (ccsr & CCM_CCSR_STEP_SEL)
		return imxanatop_get_pll2_pfd(2);
	else
		return HCLK_FREQ;
}

void
imxccm_armclk_set_parent(struct imxccm_softc *sc, enum imxanatop_clocks clock)
{
	switch (clock)
	{
	case ARM_PLL1:
		/* jump onto pll1 */
		HCLR4(sc, CCM_CCSR, CCM_CCSR_PLL1_SW_CLK_SEL);
		/* put step clk on OSC, power saving */
		HCLR4(sc, CCM_CCSR, CCM_CCSR_STEP_SEL);
		break;
	case OSC:
		/* put step clk on OSC */
		HCLR4(sc, CCM_CCSR, CCM_CCSR_STEP_SEL);
		/* jump onto step clk */
		HSET4(sc, CCM_CCSR, CCM_CCSR_PLL1_SW_CLK_SEL);
		break;
	case SYS_PLL2_PFD2:
		/* put step clk on pll2-pfd2 400 MHz */
		HSET4(sc, CCM_CCSR, CCM_CCSR_STEP_SEL);
		/* jump onto step clk */
		HSET4(sc, CCM_CCSR, CCM_CCSR_PLL1_SW_CLK_SEL);
		break;
	default:
		panic("%s: parent not possible for arm clk", __func__);
	}
}

uint32_t
imxccm_get_ecspiclk(struct imxccm_softc *sc)
{
	uint32_t clkroot = PLL3_60M;
	uint32_t podf = HREAD4(sc, CCM_CSCDR2);

	podf >>= CCM_CSCDR2_ECSPI_PODF_SHIFT;
	podf &= CCM_CSCDR2_ECSPI_PODF_MASK;

	return clkroot / (podf + 1);
}

unsigned int
imxccm_get_usdhx(struct imxccm_softc *sc, int x)
{
	uint32_t cscmr1 = HREAD4(sc, CCM_CSCMR1);
	uint32_t cscdr1 = HREAD4(sc, CCM_CSCDR1);
	uint32_t podf, clkroot;

	// Odd bitsetting. Damn you.
	if (x == 1)
		podf = ((cscdr1 >> 11) & CCM_CSCDR1_USDHCx_PODF_MASK);
	else
		podf = ((cscdr1 >> (10 + 3*x)) & CCM_CSCDR1_USDHCx_PODF_MASK);

	if (cscmr1 & (1 << CCM_CSCDR1_USDHCx_CLK_SEL_SHIFT(x)))
		clkroot = imxanatop_get_pll2_pfd(0); // 352 MHz
	else
		clkroot = imxanatop_get_pll2_pfd(2); // 396 MHz

	return clkroot / (podf + 1);
}

uint32_t
imxccm_get_uartclk(struct imxccm_softc *sc)
{
	uint32_t clkroot = PLL3_80M;
	uint32_t podf = HREAD4(sc, CCM_CSCDR1) & CCM_CSCDR1_UART_PODF_MASK;

	return clkroot / (podf + 1);
}

uint32_t
imxccm_get_periphclk(struct imxccm_softc *sc)
{
	if ((HREAD4(sc, CCM_CBCDR) >> CCM_CBCDR_PERIPH_CLK_SEL_SHIFT)
		    & CCM_CBCDR_PERIPH_CLK_SEL_MASK) {
		switch((HREAD4(sc, CCM_CBCMR)
		    >> CCM_CBCMR_PERIPH_CLK2_SEL_SHIFT) & CCM_CBCMR_PERIPH_CLK2_SEL_MASK) {
		case 0:
			return imxanatop_decode_pll(USB1_PLL3, HCLK_FREQ);
		case 1:
		case 2:
			return HCLK_FREQ;
		default:
			return 0;
		}
	
	} else {
		switch((HREAD4(sc, CCM_CBCMR)
		    >> CCM_CBCMR_PRE_PERIPH_CLK_SEL_SHIFT) & CCM_CBCMR_PRE_PERIPH_CLK_SEL_MASK) {
		default:
		case 0:
			return imxanatop_decode_pll(SYS_PLL2, HCLK_FREQ);
		case 1:
			return imxanatop_get_pll2_pfd(2); // 396 MHz
		case 2:
			return imxanatop_get_pll2_pfd(0); // 352 MHz
		case 3:
			return imxanatop_get_pll2_pfd(2) / 2; // 198 MHz
		}
	}
}

uint32_t
imxccm_get_ahbclk(struct imxccm_softc *sc)
{
	uint32_t ahb_podf;

	ahb_podf = (HREAD4(sc, CCM_CBCDR) >> CCM_CBCDR_AHB_PODF_SHIFT)
	    & CCM_CBCDR_AHB_PODF_MASK;
	return imxccm_get_periphclk(sc) / (ahb_podf + 1);
}

uint32_t
imxccm_get_ipgclk(struct imxccm_softc *sc)
{
	uint32_t ipg_podf;

	ipg_podf = (HREAD4(sc, CCM_CBCDR) >> CCM_CBCDR_IPG_PODF_SHIFT)
	    & CCM_CBCDR_IPG_PODF_MASK;
	return imxccm_get_ahbclk(sc) / (ipg_podf + 1);
}

uint32_t
imxccm_get_ipg_perclk(struct imxccm_softc *sc)
{
	uint32_t cscmr1 = HREAD4(sc, CCM_CSCMR1);
	uint32_t freq, ipg_podf;

	if (sc->sc_gates == imx6ul_gates &&
	    cscmr1 & CCM_CSCMR1_PERCLK_CLK_SEL_MASK)
		freq = HCLK_FREQ;
	else
		freq = imxccm_get_ipgclk(sc);

	ipg_podf = cscmr1 & CCM_CSCMR1_PERCLK_CLK_PODF_MASK;

	return freq / (ipg_podf + 1);
}

void
imxccm_imx6_enable_pll_enet(struct imxccm_softc *sc, int on)
{
	KASSERT(on);

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_ENET_CLR,
	    CCM_ANALOG_PLL_ENET_POWERDOWN);

	/* Wait for the PLL to lock. */
	while ((regmap_read_4(sc->sc_anatop,
	    CCM_ANALOG_PLL_ENET) & CCM_ANALOG_PLL_ENET_LOCK) == 0)
		;

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_ENET_CLR,
	    CCM_ANALOG_PLL_ENET_BYPASS);
}

void
imxccm_imx6_enable_pll_usb1(struct imxccm_softc *sc, int on)
{
	KASSERT(on);

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB1_SET,
	    CCM_ANALOG_PLL_USB1_POWER);

	/* Wait for the PLL to lock. */
	while ((regmap_read_4(sc->sc_anatop,
	    CCM_ANALOG_PLL_USB1) & CCM_ANALOG_PLL_USB1_LOCK) == 0)
		;

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB1_CLR,
	    CCM_ANALOG_PLL_USB1_BYPASS);
}

void
imxccm_imx6_enable_pll_usb2(struct imxccm_softc *sc, int on)
{
	KASSERT(on);

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB2_SET,
	    CCM_ANALOG_PLL_USB2_POWER);

	/* Wait for the PLL to lock. */
	while ((regmap_read_4(sc->sc_anatop,
	    CCM_ANALOG_PLL_USB2) & CCM_ANALOG_PLL_USB2_LOCK) == 0)
		;

	regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB2_CLR,
	    CCM_ANALOG_PLL_USB2_BYPASS);
}

uint32_t
imxccm_imx7d_enet(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc");
	case 7:
		return 392000000; /* pll_sys_pfd4_clk XXX not fixed */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx7d_i2c(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc");
	case 1:
		return 120000000; /* pll_sys_main_120m_clk */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx7d_uart(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc");
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx7d_usdhc(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc");
	case 1:
		return 392000000; /* pll_sys_pfd0_392m_clk */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mm_get_pll(struct imxccm_softc *sc, uint32_t idx)
{
	uint64_t main_div, pre_div, post_div, div;
	uint32_t pll0, pll1;
	uint64_t freq;

	switch (idx) {
	case IMX8MM_ARM_PLL:
		pll0 = regmap_read_4(sc->sc_anatop,
		    CCM_14XX_IMX8M_ARM_PLL_GNRL_CTL);
		pll1 = regmap_read_4(sc->sc_anatop,
		    CCM_14XX_IMX8M_ARM_PLL_DIV_CTL);
		div = 1;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return 0;
	}

	freq = clock_get_frequency(sc->sc_node, "osc_24m");
	if (pll0 & CCM_INT_PLL_BYPASS)
		return freq;

	main_div = (pll1 >> CCM_INT_PLL_MAIN_DIV_SHIFT) &
	    CCM_INT_PLL_MAIN_DIV_MASK;
	pre_div = (pll1 >> CCM_INT_PLL_PRE_DIV_SHIFT) &
	    CCM_INT_PLL_PRE_DIV_MASK;
	post_div = (pll1 >> CCM_INT_PLL_POST_DIV_SHIFT) &
	    CCM_INT_PLL_POST_DIV_MASK;

	freq = freq * main_div;
	freq = freq / (pre_div * (1 << post_div) * div);
	return freq;
}

int
imxccm_imx8mm_set_pll(struct imxccm_softc *sc, uint32_t idx, uint64_t freq)
{
	uint64_t main_div, pre_div, post_div;
	uint32_t pll0, pll1, reg;
	int i;

	switch (idx) {
	case IMX8MM_ARM_PLL:
		pre_div = 3;
		switch (freq) {
		case 1800000000U:
			main_div = 225;
			post_div = 0;
			break;
		case 1600000000U:
			main_div = 200;
			post_div = 0;
			break;
		case 1200000000U:
			main_div = 300;
			post_div = 1;
			break;
		case 1000000000U:
			main_div = 250;
			post_div = 1;
			break;
		case 800000000U:
			main_div = 200;
			post_div = 1;
			break;
		case 750000000U:
			main_div = 250;
			post_div = 2;
			break;
		case 700000000U:
			main_div = 350;
			post_div = 2;
			break;
		case 600000000U:
			main_div = 300;
			post_div = 2;
			break;
		default:
			printf("%s: 0x%08x\n", __func__, idx);
			return -1;
		}
		pll0 = CCM_14XX_IMX8M_ARM_PLL_GNRL_CTL;
		pll1 = CCM_14XX_IMX8M_ARM_PLL_DIV_CTL;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return -1;
	}

	regmap_write_4(sc->sc_anatop, pll0,
	    regmap_read_4(sc->sc_anatop, pll0) |
	    CCM_INT_PLL_LOCK_SEL);
	regmap_write_4(sc->sc_anatop, pll0,
	    regmap_read_4(sc->sc_anatop, pll0) &
	    ~CCM_INT_PLL_RST);
	regmap_write_4(sc->sc_anatop, pll1,
	    main_div << CCM_INT_PLL_MAIN_DIV_SHIFT |
	    pre_div << CCM_INT_PLL_PRE_DIV_SHIFT |
	    post_div << CCM_INT_PLL_POST_DIV_SHIFT);
	delay(3);
	regmap_write_4(sc->sc_anatop, pll0,
	    regmap_read_4(sc->sc_anatop, pll0) |
	    CCM_INT_PLL_RST);
	for (i = 0; i < 5000; i++) {
		reg = regmap_read_4(sc->sc_anatop, pll0);
		if (reg & CCM_INT_PLL_LOCK)
			break;
		delay(10);
	}
	if (i == 5000)
		printf("%s: timeout\n", __func__);
	regmap_write_4(sc->sc_anatop, pll0,
	    regmap_read_4(sc->sc_anatop, pll0) &
	    ~CCM_INT_PLL_BYPASS);

	return 0;
}

uint32_t
imxccm_imx8mm_enet(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 266 * 1000 * 1000; /* sys1_pll_266m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mm_ahb(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 133 * 1000 * 1000; /* sys_pll1_133m */
	case 2:
		return 800 * 1000 * 1000; /* sys_pll1_800m */
	case 3:
		return 400 * 1000 * 1000; /* sys_pll1_400m */
	case 4:
		return 125 * 1000 * 1000; /* sys_pll2_125m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mm_i2c(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mm_uart(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mm_usdhc(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 400 * 1000 * 1000; /* sys1_pll_400m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mp_enet_qos(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 125 * 1000 * 1000; /* sys2_pll_125m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mp_enet_qos_timer(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 100 * 1000 * 1000; /* sys2_pll_100m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mp_hsio_axi(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_24m");
	case 1:
		return 500 * 1000 * 1000; /* sys2_pll_500m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_ecspi(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		return 200 * 1000 * 1000; /* sys2_pll_200m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_enet(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		return 266 * 1000 * 1000; /* sys1_pll_266m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_ahb(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		return 133 * 1000 * 1000; /* sys1_pll_133m */
	case 2:
		return 800 * 1000 * 1000; /* sys1_pll_800m */
	case 3:
		return 400 * 1000 * 1000; /* sys1_pll_400m */
	case 4:
		return 125 * 1000 * 1000; /* sys2_pll_125m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_i2c(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_pwm(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		return 100 * 1000 * 1000; /* sys1_pll_100m */
	case 2:
		return 160 * 1000 * 1000; /* sys1_pll_160m */
	case 3:
		return 40 * 1000 * 1000; /* sys1_pll_40m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_uart(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_usdhc(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		return 400 * 1000 * 1000; /* sys1_pll_400m */
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_usb(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t mux;

	if (idx >= sc->sc_nmuxs || sc->sc_muxs[idx].reg == 0)
		return 0;

	mux = HREAD4(sc, sc->sc_muxs[idx].reg);
	mux >>= sc->sc_muxs[idx].shift;
	mux &= sc->sc_muxs[idx].mask;

	switch (mux) {
	case 0:
		return clock_get_frequency(sc->sc_node, "osc_25m");
	case 1:
		if (idx == IMX8MQ_CLK_USB_CORE_REF ||
		    idx == IMX8MQ_CLK_USB_PHY_REF)
			return 100 * 1000 * 1000; /* sys1_pll_100m */
		if (idx == IMX8MQ_CLK_USB_BUS)
			return 500 * 1000 * 1000; /* sys2_pll_500m */
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}
}

uint32_t
imxccm_imx8mq_get_pll(struct imxccm_softc *sc, uint32_t idx)
{
	uint32_t divr_val, divq_val, divf_val;
	uint32_t divff, divfi;
	uint32_t pllout_div;
	uint32_t pll0, pll1;
	uint32_t freq;
	uint32_t mux;

	pllout_div = regmap_read_4(sc->sc_anatop, CCM_FRAC_IMX8M_PLLOUT_DIV_CFG);

	switch (idx) {
	case IMX8MQ_ARM_PLL:
		pll0 = regmap_read_4(sc->sc_anatop, CCM_FRAC_IMX8M_ARM_PLL0);
		pll1 = regmap_read_4(sc->sc_anatop, CCM_FRAC_IMX8M_ARM_PLL1);
		pllout_div >>= CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_SHIFT;
		pllout_div &= CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_MASK;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return 0;
	}

	if (pll0 & CCM_FRAC_PLL_POWERDOWN)
		return 0;

	if ((pll0 & CCM_FRAC_PLL_ENABLE) == 0)
		return 0;

	mux = (pll0 >> CCM_FRAC_PLL_REFCLK_SEL_SHIFT) &
	    CCM_FRAC_PLL_REFCLK_SEL_MASK;
	switch (mux) {
	case 0:
		freq = clock_get_frequency(sc->sc_node, "osc_25m");
		break;
	case 1:
	case 2:
		freq = clock_get_frequency(sc->sc_node, "osc_27m");
		break;
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return 0;
	}

	if (pll0 & CCM_FRAC_PLL_BYPASS)
		return freq;

	divr_val = (pll0 >> CCM_FRAC_PLL_REFCLK_DIV_VAL_SHIFT) &
	    CCM_FRAC_PLL_REFCLK_DIV_VAL_MASK;
	divq_val = pll0 & CCM_FRAC_PLL_OUTPUT_DIV_VAL_MASK;
	divff = (pll1 >> CCM_FRAC_PLL_FRAC_DIV_CTL_SHIFT) &
	    CCM_FRAC_PLL_FRAC_DIV_CTL_MASK;
	divfi = pll1 & CCM_FRAC_PLL_INT_DIV_CTL_MASK;
	divf_val = 1 + divfi + divff / CCM_FRAC_PLL_DENOM;

	freq = freq / (divr_val + 1) * 8 * divf_val / ((divq_val + 1) * 2);
	return freq / (pllout_div + 1);
}

int
imxccm_imx8mq_set_pll(struct imxccm_softc *sc, uint32_t idx, uint64_t freq)
{
	uint64_t divff, divfi, divr;
	uint32_t pllout_div;
	uint32_t pll0, pll1;
	uint32_t mux, reg;
	uint64_t pfreq;
	int i;

	pllout_div = regmap_read_4(sc->sc_anatop, CCM_FRAC_IMX8M_PLLOUT_DIV_CFG);

	switch (idx) {
	case IMX8MQ_ARM_PLL:
		pll0 = CCM_FRAC_IMX8M_ARM_PLL0;
		pll1 = CCM_FRAC_IMX8M_ARM_PLL1;
		pllout_div >>= CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_SHIFT;
		pllout_div &= CCM_FRAC_IMX8M_PLLOUT_DIV_CFG_ARM_MASK;
		/* XXX: Assume fixed divider to ease math. */
		KASSERT(pllout_div == 0);
		divr = 5;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return -1;
	}

	reg = regmap_read_4(sc->sc_anatop, pll0);
	mux = (reg >> CCM_FRAC_PLL_REFCLK_SEL_SHIFT) &
	    CCM_FRAC_PLL_REFCLK_SEL_MASK;
	switch (mux) {
	case 0:
		pfreq = clock_get_frequency(sc->sc_node, "osc_25m");
		break;
	case 1:
	case 2:
		pfreq = clock_get_frequency(sc->sc_node, "osc_27m");
		break;
	default:
		printf("%s: 0x%08x 0x%08x\n", __func__, idx, mux);
		return -1;
	}

	/* Frac divider follows the PLL */
	freq *= divr;

	/* PLL calculation */
	freq *= 2;
	pfreq *= 8;
	divfi = freq / pfreq;
	divff = (uint64_t)(freq - divfi * pfreq);
	divff = (divff * CCM_FRAC_PLL_DENOM) / pfreq;

	reg = regmap_read_4(sc->sc_anatop, pll1);
	reg &= ~(CCM_FRAC_PLL_FRAC_DIV_CTL_MASK << CCM_FRAC_PLL_FRAC_DIV_CTL_SHIFT);
	reg |= divff << CCM_FRAC_PLL_FRAC_DIV_CTL_SHIFT;
	reg &= ~(CCM_FRAC_PLL_INT_DIV_CTL_MASK << CCM_FRAC_PLL_INT_DIV_CTL_SHIFT);
	reg |= (divfi - 1) << CCM_FRAC_PLL_INT_DIV_CTL_SHIFT;
	regmap_write_4(sc->sc_anatop, pll1, reg);

	reg = regmap_read_4(sc->sc_anatop, pll0);
	reg &= ~CCM_FRAC_PLL_OUTPUT_DIV_VAL_MASK;
	reg &= ~(CCM_FRAC_PLL_REFCLK_DIV_VAL_MASK << CCM_FRAC_PLL_REFCLK_DIV_VAL_SHIFT);
	reg |= (divr - 1) << CCM_FRAC_PLL_REFCLK_DIV_VAL_SHIFT;
	regmap_write_4(sc->sc_anatop, pll0, reg);

	reg = regmap_read_4(sc->sc_anatop, pll0);
	reg |= CCM_FRAC_PLL_NEWDIV_VAL;
	regmap_write_4(sc->sc_anatop, pll0, reg);

	for (i = 0; i < 5000; i++) {
		reg = regmap_read_4(sc->sc_anatop, pll0);
		if (reg & CCM_FRAC_PLL_BYPASS)
			break;
		if (reg & CCM_FRAC_PLL_POWERDOWN)
			break;
		if (reg & CCM_FRAC_PLL_NEWDIV_ACK)
			break;
		delay(10);
	}
	if (i == 5000)
		printf("%s: timeout\n", __func__);

	reg = regmap_read_4(sc->sc_anatop, pll0);
	reg &= ~CCM_FRAC_PLL_NEWDIV_VAL;
	regmap_write_4(sc->sc_anatop, pll0, reg);

	return 0;
}

int
imxccm_imx8m_set_div(struct imxccm_softc *sc, uint32_t idx, uint64_t freq,
    uint64_t parent_freq)
{
	uint64_t div;
	uint32_t reg;

	if (parent_freq < freq) {
		printf("%s: parent frequency too low (0x%08x)\n",
		    __func__, idx);
		return -1;
	}

	/* divisor can only be changed if enabled */
	imxccm_enable(sc, &idx, 1);

	div = 0;
	while (parent_freq / (div + 1) > freq)
		div++;
	reg = HREAD4(sc, sc->sc_divs[idx].reg);
	reg &= ~(sc->sc_divs[idx].mask << sc->sc_divs[idx].shift);
	reg |= (div << sc->sc_divs[idx].shift);
	HWRITE4(sc, sc->sc_divs[idx].reg, reg);
	HCLR4(sc, sc->sc_predivs[idx].reg,
	    sc->sc_predivs[idx].mask << sc->sc_predivs[idx].shift);
	return 0;
}

void
imxccm_enable_parent(struct imxccm_softc *sc, uint32_t parent, int on)
{
	if (on)
		imxccm_enable(sc, &parent, on);
}

void
imxccm_enable(void *cookie, uint32_t *cells, int on)
{
	struct imxccm_softc *sc = cookie;
	uint32_t idx = cells[0], parent;
	uint32_t pcells[2];
	uint16_t reg;
	uint8_t pos;

	/* Dummy clock. */
	if (idx == 0)
		return;

	if (sc->sc_gates == imx8mm_gates) {
		switch (idx) {
		case IMX8MM_CLK_PCIE1_CTRL:
		case IMX8MM_CLK_PCIE2_CTRL:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MM_SYS_PLL2_250M;
			imxccm_set_parent(cookie, &idx, pcells);
			break;
		case IMX8MM_CLK_PCIE1_PHY:
		case IMX8MM_CLK_PCIE2_PHY:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MM_SYS_PLL2_100M;
			imxccm_set_parent(cookie, &idx, pcells);
			break;
		}
	} else if (sc->sc_gates == imx8mq_gates) {
		switch (idx) {
		case IMX8MQ_CLK_32K:
			/* always on */
			return;
		case IMX8MQ_CLK_PCIE1_CTRL:
		case IMX8MQ_CLK_PCIE2_CTRL:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MQ_SYS2_PLL_250M;
			imxccm_set_parent(cookie, &idx, pcells);
			break;
		case IMX8MQ_CLK_PCIE1_PHY:
		case IMX8MQ_CLK_PCIE2_PHY:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MQ_SYS2_PLL_100M;
			imxccm_set_parent(cookie, &idx, pcells);
			break;
		}
	} else if (sc->sc_gates == imx7d_gates) {
		if (sc->sc_anatop == NULL) {
			sc->sc_anatop = regmap_bycompatible("fsl,imx7d-anatop");
			KASSERT(sc->sc_anatop);
		}

		switch (idx) {
		case IMX7D_PLL_ENET_MAIN_125M_CLK:
			regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_ENET_SET,
			    CCM_ANALOG_PLL_ENET_ENABLE_CLK_125MHZ);
			return;
		default:
			break;
		}
	} else if (sc->sc_gates == imx6_gates) {
		if (sc->sc_anatop == NULL) {
			sc->sc_anatop = regmap_bycompatible("fsl,imx6q-anatop");
			KASSERT(sc->sc_anatop);
		}

		switch (idx) {
		case IMX6_CLK_PLL3:
			imxccm_imx6_enable_pll_usb1(sc, on);
			return;
		case IMX6_CLK_PLL6:
			imxccm_imx6_enable_pll_enet(sc, on);
			return;
		case IMX6_CLK_PLL7:
			imxccm_imx6_enable_pll_usb2(sc, on);
			return;
		case IMX6_CLK_PLL3_USB_OTG:
			imxccm_enable_parent(sc, IMX6_CLK_PLL3, on);
			regmap_write_4(sc->sc_anatop,
			    on ? CCM_ANALOG_PLL_USB1_SET : CCM_ANALOG_PLL_USB1_CLR,
			    CCM_ANALOG_PLL_USB1_ENABLE);
			return;
		case IMX6_CLK_PLL6_ENET:
			imxccm_enable_parent(sc, IMX6_CLK_PLL6, on);
			regmap_write_4(sc->sc_anatop,
			    on ? CCM_ANALOG_PLL_ENET_SET : CCM_ANALOG_PLL_ENET_CLR,
			    CCM_ANALOG_PLL_ENET_ENABLE);
			return;
		case IMX6_CLK_PLL7_USB_HOST:
			imxccm_enable_parent(sc, IMX6_CLK_PLL7, on);
			regmap_write_4(sc->sc_anatop,
			    on ? CCM_ANALOG_PLL_USB2_SET : CCM_ANALOG_PLL_USB2_CLR,
			    CCM_ANALOG_PLL_USB2_ENABLE);
			return;
		case IMX6_CLK_USBPHY1:
			/* PLL outputs should always be on. */
			regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB1_SET,
			    CCM_ANALOG_PLL_USB1_EN_USB_CLKS);
			imxccm_enable_parent(sc, IMX6_CLK_PLL3_USB_OTG, on);
			return;
		case IMX6_CLK_USBPHY2:
			/* PLL outputs should always be on. */
			regmap_write_4(sc->sc_anatop, CCM_ANALOG_PLL_USB2_SET,
			    CCM_ANALOG_PLL_USB2_EN_USB_CLKS);
			imxccm_enable_parent(sc, IMX6_CLK_PLL7_USB_HOST, on);
			return;
		case IMX6_CLK_SATA_REF_100:
			imxccm_enable_parent(sc, IMX6_CLK_PLL6_ENET, on);
			regmap_write_4(sc->sc_anatop,
			   on ? CCM_ANALOG_PLL_ENET_SET : CCM_ANALOG_PLL_ENET_CLR,
			   CCM_ANALOG_PLL_ENET_ENABLE_100M);
			return;
		case IMX6_CLK_ENET_REF:
			imxccm_enable_parent(sc, IMX6_CLK_PLL6_ENET, on);
			return;
		case IMX6_CLK_IPG:
		case IMX6_CLK_IPG_PER:
		case IMX6_CLK_ECSPI_ROOT:
			/* always on */
			return;
		default:
			break;
		}
	}

	if (on) {
		if (idx < sc->sc_ngates && sc->sc_gates[idx].parent) {
			parent = sc->sc_gates[idx].parent;
			imxccm_enable(sc, &parent, on);
		}

		if (idx < sc->sc_ndivs && sc->sc_divs[idx].parent) {
			parent = sc->sc_divs[idx].parent;
			imxccm_enable(sc, &parent, on);
		}
	}

	if (idx >= sc->sc_ngates || sc->sc_gates[idx].reg == 0) {
		if ((idx < sc->sc_ndivs && sc->sc_divs[idx].reg != 0) ||
		    (idx < sc->sc_nmuxs && sc->sc_muxs[idx].reg != 0))
			return;
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}

	reg = sc->sc_gates[idx].reg;
	pos = sc->sc_gates[idx].pos;

	if (on)
		HSET4(sc, reg, 0x3 << (2 * pos));
	else
		HCLR4(sc, reg, 0x3 << (2 * pos));
}

uint32_t
imxccm_get_frequency(void *cookie, uint32_t *cells)
{
	struct imxccm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t div, pre, reg, parent;
	uint32_t freq;

	/* Dummy clock. */
	if (idx == 0)
		return 0;

	if (idx < sc->sc_ngates && sc->sc_gates[idx].parent) {
		parent = sc->sc_gates[idx].parent;
		return imxccm_get_frequency(sc, &parent);
	}

	if (idx < sc->sc_ndivs && sc->sc_divs[idx].parent) {
		div = HREAD4(sc, sc->sc_divs[idx].reg);
		div = div >> sc->sc_divs[idx].shift;
		div = div & sc->sc_divs[idx].mask;
		parent = sc->sc_divs[idx].parent;
		return imxccm_get_frequency(sc, &parent) / (div + 1);
	}

	if (sc->sc_gates == imx8mm_gates) {
		switch (idx) {
		case IMX8MM_CLK_ARM:
			parent = IMX8MM_ARM_PLL;
			return imxccm_get_frequency(sc, &parent);
		case IMX8MM_ARM_PLL:
			return imxccm_imx8mm_get_pll(sc, idx);
		}

		/* These are composite clocks. */
		if (idx < sc->sc_ngates && sc->sc_gates[idx].reg &&
		    idx < sc->sc_ndivs && sc->sc_divs[idx].reg &&
		    idx < sc->sc_npredivs && sc->sc_predivs[idx].reg) {
			switch (idx) {
			case IMX8MM_CLK_ENET_AXI:
				freq = imxccm_imx8mm_enet(sc, idx);
				break;
			case IMX8MM_CLK_AHB:
				freq = imxccm_imx8mm_ahb(sc, idx);
				break;
			case IMX8MM_CLK_I2C1:
			case IMX8MM_CLK_I2C2:
			case IMX8MM_CLK_I2C3:
			case IMX8MM_CLK_I2C4:
				freq = imxccm_imx8mm_i2c(sc, idx);
				break;
			case IMX8MM_CLK_UART1:
			case IMX8MM_CLK_UART2:
			case IMX8MM_CLK_UART3:
			case IMX8MM_CLK_UART4:
				freq = imxccm_imx8mm_uart(sc, idx);
				break;
			case IMX8MM_CLK_USDHC1:
			case IMX8MM_CLK_USDHC2:
			case IMX8MM_CLK_USDHC3:
				freq = imxccm_imx8mm_usdhc(sc, idx);
				break;
			default:
				printf("%s: 0x%08x\n", __func__, idx);
				return 0;
			}

			reg = HREAD4(sc, sc->sc_divs[idx].reg);
			div = reg >> sc->sc_divs[idx].shift;
			div = div & sc->sc_divs[idx].mask;
			pre = reg >> sc->sc_predivs[idx].shift;
			pre = pre & sc->sc_predivs[idx].mask;
			return ((freq / (pre + 1)) / (div + 1));
		}
	} else if (sc->sc_gates == imx8mp_gates) {
		/* These are composite clocks. */
		if (idx < sc->sc_ngates && sc->sc_gates[idx].reg &&
		    idx < sc->sc_ndivs && sc->sc_divs[idx].reg &&
		    idx < sc->sc_npredivs && sc->sc_predivs[idx].reg) {
			switch (idx) {
			case IMX8MP_CLK_ENET_AXI:
				freq = imxccm_imx8mm_enet(sc, idx);
				break;
			case IMX8MP_CLK_AHB:
				freq = imxccm_imx8mm_ahb(sc, idx);
				break;
			case IMX8MP_CLK_I2C1:
			case IMX8MP_CLK_I2C2:
			case IMX8MP_CLK_I2C3:
			case IMX8MP_CLK_I2C4:
			case IMX8MP_CLK_I2C5:
			case IMX8MP_CLK_I2C6:
				freq = imxccm_imx8mm_i2c(sc, idx);
				break;
			case IMX8MP_CLK_UART1:
			case IMX8MP_CLK_UART2:
			case IMX8MP_CLK_UART3:
			case IMX8MP_CLK_UART4:
				freq = imxccm_imx8mm_uart(sc, idx);
				break;
			case IMX8MP_CLK_USDHC1:
			case IMX8MP_CLK_USDHC2:
			case IMX8MP_CLK_USDHC3:
				freq = imxccm_imx8mm_usdhc(sc, idx);
				break;
			case IMX8MP_CLK_ENET_QOS:
				freq = imxccm_imx8mp_enet_qos(sc, idx);
				break;
			case IMX8MP_CLK_ENET_QOS_TIMER:
				freq = imxccm_imx8mp_enet_qos_timer(sc, idx);
				break;
			case IMX8MP_CLK_HSIO_AXI:
				freq = imxccm_imx8mp_hsio_axi(sc, idx);
				break;
			default:
				printf("%s: 0x%08x\n", __func__, idx);
				return 0;
			}

			reg = HREAD4(sc, sc->sc_divs[idx].reg);
			div = reg >> sc->sc_divs[idx].shift;
			div = div & sc->sc_divs[idx].mask;
			pre = reg >> sc->sc_predivs[idx].shift;
			pre = pre & sc->sc_predivs[idx].mask;
			return ((freq / (pre + 1)) / (div + 1));
		}
	} else if (sc->sc_gates == imx8mq_gates) {
		switch (idx) {
		case IMX8MQ_CLK_ARM:
			parent = IMX8MQ_ARM_PLL;
			return imxccm_get_frequency(sc, &parent);
		case IMX8MQ_ARM_PLL:
			return imxccm_imx8mq_get_pll(sc, idx);
		}

		/* These are composite clocks. */
		if (idx < sc->sc_ngates && sc->sc_gates[idx].reg &&
		    idx < sc->sc_ndivs && sc->sc_divs[idx].reg &&
		    idx < sc->sc_npredivs && sc->sc_predivs[idx].reg) {
			switch (idx) {
			case IMX8MQ_CLK_ENET_AXI:
				freq = imxccm_imx8mq_enet(sc, idx);
				break;
			case IMX8MQ_CLK_AHB:
				freq = imxccm_imx8mq_ahb(sc, idx);
				break;
			case IMX8MQ_CLK_I2C1:
			case IMX8MQ_CLK_I2C2:
			case IMX8MQ_CLK_I2C3:
			case IMX8MQ_CLK_I2C4:
				freq = imxccm_imx8mq_i2c(sc, idx);
				break;
			case IMX8MQ_CLK_UART1:
			case IMX8MQ_CLK_UART2:
			case IMX8MQ_CLK_UART3:
			case IMX8MQ_CLK_UART4:
				freq = imxccm_imx8mq_uart(sc, idx);
				break;
			case IMX8MQ_CLK_USDHC1:
			case IMX8MQ_CLK_USDHC2:
				freq = imxccm_imx8mq_usdhc(sc, idx);
				break;
			case IMX8MQ_CLK_USB_BUS:
			case IMX8MQ_CLK_USB_CORE_REF:
			case IMX8MQ_CLK_USB_PHY_REF:
				freq = imxccm_imx8mq_usb(sc, idx);
				break;
			case IMX8MQ_CLK_ECSPI1:
			case IMX8MQ_CLK_ECSPI2:
			case IMX8MQ_CLK_ECSPI3:
				freq = imxccm_imx8mq_ecspi(sc, idx);
				break;
			case IMX8MQ_CLK_PWM1:
			case IMX8MQ_CLK_PWM2:
			case IMX8MQ_CLK_PWM3:
			case IMX8MQ_CLK_PWM4:
				freq = imxccm_imx8mq_pwm(sc, idx);
				break;
			default:
				printf("%s: 0x%08x\n", __func__, idx);
				return 0;
			}

			reg = HREAD4(sc, sc->sc_divs[idx].reg);
			div = reg >> sc->sc_divs[idx].shift;
			div = div & sc->sc_divs[idx].mask;
			pre = reg >> sc->sc_predivs[idx].shift;
			pre = pre & sc->sc_predivs[idx].mask;
			return ((freq / (pre + 1)) / (div + 1));
		}

	} else if (sc->sc_gates == imx7d_gates) {
		switch (idx) {
		case IMX7D_ENET_AXI_ROOT_SRC:
			return imxccm_imx7d_enet(sc, idx);
		case IMX7D_I2C1_ROOT_SRC:
		case IMX7D_I2C2_ROOT_SRC:
		case IMX7D_I2C3_ROOT_SRC:
		case IMX7D_I2C4_ROOT_SRC:
			return imxccm_imx7d_i2c(sc, idx);
		case IMX7D_UART1_ROOT_SRC:
		case IMX7D_UART2_ROOT_SRC:
		case IMX7D_UART3_ROOT_SRC:
		case IMX7D_UART4_ROOT_SRC:
		case IMX7D_UART5_ROOT_SRC:
		case IMX7D_UART6_ROOT_SRC:
		case IMX7D_UART7_ROOT_SRC:
			return imxccm_imx7d_uart(sc, idx);
		case IMX7D_USDHC1_ROOT_SRC:
		case IMX7D_USDHC2_ROOT_SRC:
		case IMX7D_USDHC3_ROOT_SRC:
			return imxccm_imx7d_usdhc(sc, idx);
		}
	} else if (sc->sc_gates == imx6ul_gates) {
		switch (idx) {
		case IMX6UL_CLK_ARM:
			return imxccm_get_armclk(sc);
		case IMX6UL_CLK_IPG:
			return imxccm_get_ipgclk(sc);
		case IMX6UL_CLK_PERCLK:
			return imxccm_get_ipg_perclk(sc);
		case IMX6UL_CLK_UART1_SERIAL:
			return imxccm_get_uartclk(sc);
		case IMX6UL_CLK_USDHC1:
		case IMX6UL_CLK_USDHC2:
			return imxccm_get_usdhx(sc, idx - IMX6UL_CLK_USDHC1 + 1);
		}
	} else if (sc->sc_gates == imx6_gates) {
		switch (idx) {
		case IMX6_CLK_AHB:
			return imxccm_get_ahbclk(sc);
		case IMX6_CLK_ARM:
			return imxccm_get_armclk(sc);
		case IMX6_CLK_IPG:
			return imxccm_get_ipgclk(sc);
		case IMX6_CLK_IPG_PER:
			return imxccm_get_ipg_perclk(sc);
		case IMX6_CLK_ECSPI_ROOT:
			return imxccm_get_ecspiclk(sc);
		case IMX6_CLK_UART_SERIAL:
			return imxccm_get_uartclk(sc);
		case IMX6_CLK_USDHC1:
		case IMX6_CLK_USDHC2:
		case IMX6_CLK_USDHC3:
		case IMX6_CLK_USDHC4:
			return imxccm_get_usdhx(sc, idx - IMX6_CLK_USDHC1 + 1);
		}
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
imxccm_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct imxccm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, div, parent, parent_freq;
	uint32_t pcells[2];
	int ret;

	if (sc->sc_divs == imx8mm_divs) {
		switch (idx) {
		case IMX8MM_CLK_ARM:
			parent = IMX8MM_CLK_A53_SRC;
			return imxccm_set_frequency(cookie, &parent, freq);
		case IMX8MM_CLK_A53_SRC:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MM_SYS_PLL1_800M;
			ret = imxccm_set_parent(cookie, &idx, pcells);
			if (ret)
				return ret;
			ret = imxccm_imx8mm_set_pll(sc, IMX8MM_ARM_PLL, freq);
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MM_ARM_PLL_OUT;
			imxccm_set_parent(cookie, &idx, pcells);
			return ret;
		case IMX8MM_CLK_USDHC1_ROOT:
		case IMX8MM_CLK_USDHC2_ROOT:
		case IMX8MM_CLK_USDHC3_ROOT:
			parent = sc->sc_gates[idx].parent;
			return imxccm_set_frequency(sc, &parent, freq);
		case IMX8MM_CLK_USDHC1:
		case IMX8MM_CLK_USDHC2:
		case IMX8MM_CLK_USDHC3:
			parent_freq = imxccm_imx8mm_usdhc(sc, idx);
			return imxccm_imx8m_set_div(sc, idx, freq, parent_freq);
		}
	} else if (sc->sc_divs == imx8mp_divs) {
		switch (idx) {
		case IMX8MP_CLK_ENET_QOS:
			parent_freq = imxccm_imx8mp_enet_qos(sc, idx);
			return imxccm_imx8m_set_div(sc, idx, freq, parent_freq);
		case IMX8MP_CLK_ENET_QOS_TIMER:
			parent_freq = imxccm_imx8mp_enet_qos_timer(sc, idx);
			return imxccm_imx8m_set_div(sc, idx, freq, parent_freq);
		case IMX8MP_CLK_HSIO_AXI:
			parent_freq = imxccm_imx8mp_hsio_axi(sc, idx);
			return imxccm_imx8m_set_div(sc, idx, freq, parent_freq);
		}
	} else if (sc->sc_divs == imx8mq_divs) {
		switch (idx) {
		case IMX8MQ_CLK_ARM:
			parent = IMX8MQ_CLK_A53_SRC;
			return imxccm_set_frequency(cookie, &parent, freq);
		case IMX8MQ_CLK_A53_SRC:
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MQ_SYS1_PLL_800M;
			ret = imxccm_set_parent(cookie, &idx, pcells);
			if (ret)
				return ret;
			ret = imxccm_imx8mq_set_pll(sc, IMX8MQ_ARM_PLL, freq);
			pcells[0] = sc->sc_phandle;
			pcells[1] = IMX8MQ_ARM_PLL_OUT;
			imxccm_set_parent(cookie, &idx, pcells);
			return ret;
		case IMX8MQ_CLK_USB_BUS:
		case IMX8MQ_CLK_USB_CORE_REF:
		case IMX8MQ_CLK_USB_PHY_REF:
			if (imxccm_get_frequency(sc, cells) != freq)
				break;
			return 0;
		case IMX8MQ_CLK_USDHC1:
		case IMX8MQ_CLK_USDHC2:
			parent_freq = imxccm_imx8mq_usdhc(sc, idx);
			return imxccm_imx8m_set_div(sc, idx, freq, parent_freq);
		}
	} else if (sc->sc_divs == imx7d_divs) {
		switch (idx) {
		case IMX7D_USDHC1_ROOT_CLK:
		case IMX7D_USDHC2_ROOT_CLK:
		case IMX7D_USDHC3_ROOT_CLK:
			parent = sc->sc_gates[idx].parent;
			return imxccm_set_frequency(sc, &parent, freq);
		case IMX7D_USDHC1_ROOT_DIV:
		case IMX7D_USDHC2_ROOT_DIV:
		case IMX7D_USDHC3_ROOT_DIV:
			parent = sc->sc_divs[idx].parent;
			parent_freq = imxccm_get_frequency(sc, &parent);
			div = 0;
			while (parent_freq / (div + 1) > freq)
				div++;
			reg = HREAD4(sc, sc->sc_divs[idx].reg);
			reg &= ~(sc->sc_divs[idx].mask << sc->sc_divs[idx].shift);
			reg |= (div << sc->sc_divs[idx].shift);
			HWRITE4(sc, sc->sc_divs[idx].reg, reg);
			return 0;
		}
	}

	printf("%s: 0x%08x %x\n", __func__, idx, freq);
	return -1;
}

int
imxccm_set_parent(void *cookie, uint32_t *cells, uint32_t *pcells)
{
	struct imxccm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t pidx;
	uint32_t mux;

	if (pcells[0] != sc->sc_phandle) {
		printf("%s: 0x%08x parent 0x%08x\n", __func__, idx, pcells[0]);
		return -1;
	}

	pidx = pcells[1];

	if (sc->sc_muxs == imx8mm_muxs) {
		switch (idx) {
		case IMX8MM_CLK_A53_SRC:
			if (pidx != IMX8MM_ARM_PLL_OUT &&
			    pidx != IMX8MM_SYS_PLL1_800M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			if (pidx == IMX8MM_ARM_PLL_OUT)
				mux |= (0x1 << sc->sc_muxs[idx].shift);
			if (pidx == IMX8MM_SYS_PLL1_800M)
				mux |= (0x4 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MM_CLK_USB_BUS:
			if (pidx != IMX8MM_SYS_PLL2_500M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MM_CLK_USB_CORE_REF:
		case IMX8MM_CLK_USB_PHY_REF:
			if (pidx != IMX8MM_SYS_PLL1_100M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MM_CLK_PCIE1_CTRL:
		case IMX8MM_CLK_PCIE2_CTRL:
			if (pidx != IMX8MM_SYS_PLL2_250M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MM_CLK_PCIE1_PHY:
		case IMX8MM_CLK_PCIE2_PHY:
			if (pidx != IMX8MM_SYS_PLL2_100M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		}
	} else if (sc->sc_muxs == imx8mp_muxs) {
		switch (idx) {
		case IMX8MP_CLK_ENET_AXI:
			if (pidx != IMX8MP_SYS_PLL1_266M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_PCIE_PHY:
			if (pidx != IMX8MP_CLK_24M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x0 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_PCIE_AUX:
			if (pidx != IMX8MP_SYS_PLL2_50M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x2 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_ENET_QOS:
			if (pidx != IMX8MP_SYS_PLL2_125M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_ENET_QOS_TIMER:
			if (pidx != IMX8MP_SYS_PLL2_100M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_USB_PHY_REF:
			if (pidx != IMX8MP_CLK_24M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x0 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MP_CLK_HSIO_AXI:
			if (pidx != IMX8MP_SYS_PLL2_500M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		}
	} else if (sc->sc_muxs == imx8mq_muxs) {
		switch (idx) {
		case IMX8MQ_CLK_A53_SRC:
			if (pidx != IMX8MQ_ARM_PLL_OUT &&
			    pidx != IMX8MQ_SYS1_PLL_800M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			if (pidx == IMX8MQ_ARM_PLL_OUT)
				mux |= (0x1 << sc->sc_muxs[idx].shift);
			if (pidx == IMX8MQ_SYS1_PLL_800M)
				mux |= (0x4 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MQ_CLK_USB_BUS:
			if (pidx != IMX8MQ_SYS2_PLL_500M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MQ_CLK_USB_CORE_REF:
		case IMX8MQ_CLK_USB_PHY_REF:
			if (pidx != IMX8MQ_SYS1_PLL_100M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MQ_CLK_PCIE1_CTRL:
		case IMX8MQ_CLK_PCIE2_CTRL:
			if (pidx != IMX8MQ_SYS2_PLL_250M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		case IMX8MQ_CLK_PCIE1_PHY:
		case IMX8MQ_CLK_PCIE2_PHY:
			if (pidx != IMX8MQ_SYS2_PLL_100M)
				break;
			mux = HREAD4(sc, sc->sc_muxs[idx].reg);
			mux &= ~(sc->sc_muxs[idx].mask << sc->sc_muxs[idx].shift);
			mux |= (0x1 << sc->sc_muxs[idx].shift);
			HWRITE4(sc, sc->sc_muxs[idx].reg, mux);
			return 0;
		}
	}

	printf("%s: 0x%08x 0x%08x\n", __func__, idx, pidx);
	return -1;
}
