/*	$OpenBSD: imxesdhc.c,v 1.18 2022/01/09 05:42:37 jsg Exp $	*/
/*
 * Copyright (c) 2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
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

/* i.MX SD/MMC support derived from /sys/dev/sdmmc/sdhc.c */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmc_ioreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

/* registers */
#define SDHC_DS_ADDR				0x00
#define SDHC_BLK_ATT				0x04
#define SDHC_CMD_ARG				0x08
#define SDHC_CMD_XFR_TYP			0x0c
#define SDHC_CMD_RSP0				0x10
#define SDHC_CMD_RSP1				0x14
#define SDHC_CMD_RSP2				0x18
#define SDHC_CMD_RSP3				0x1c
#define SDHC_DATA_BUFF_ACC_PORT			0x20
#define SDHC_PRES_STATE				0x24
#define SDHC_PROT_CTRL				0x28
#define SDHC_SYS_CTRL				0x2c
#define SDHC_INT_STATUS				0x30
#define SDHC_INT_STATUS_EN			0x34
#define SDHC_INT_SIGNAL_EN			0x38
#define SDHC_AUTOCMD12_ERR_STATUS		0x3c
#define SDHC_HOST_CTRL_CAP			0x40
#define SDHC_WTMK_LVL				0x44
#define SDHC_MIX_CTRL				0x48
#define SDHC_FORCE_EVENT			0x50
#define SDHC_ADMA_ERR_STATUS			0x54
#define SDHC_ADMA_SYS_ADDR			0x58
#define SDHC_DLL_CTRL				0x60
#define SDHC_DLL_STATUS				0x64
#define SDHC_CLK_TUNE_CTRL_STATUS		0x68
#define SDHC_VEND_SPEC				0xc0
#define SDHC_MMC_BOOT				0xc4
#define SDHC_VEND_SPEC2				0xc8
#define SDHC_HOST_CTRL_VER			0xfc

/* bits and bytes */
#define SDHC_BLK_ATT_BLKCNT_MAX			0xffff
#define SDHC_BLK_ATT_BLKCNT_SHIFT		16
#define SDHC_BLK_ATT_BLKSIZE_SHIFT		0
#define SDHC_CMD_XFR_TYP_CMDINDX_SHIFT		24
#define SDHC_CMD_XFR_TYP_CMDINDX_SHIFT_MASK	(0x3f << SDHC_CMD_XFR_TYP_CMDINDX_SHIFT)
#define SDHC_CMD_XFR_TYP_CMDTYP_SHIFT		22
#define SDHC_CMD_XFR_TYP_DPSEL_SHIFT		21
#define SDHC_CMD_XFR_TYP_DPSEL			(1 << SDHC_CMD_XFR_TYP_DPSEL_SHIFT)
#define SDHC_CMD_XFR_TYP_CICEN_SHIFT		20
#define SDHC_CMD_XFR_TYP_CICEN			(1 << SDHC_CMD_XFR_TYP_CICEN_SHIFT)
#define SDHC_CMD_XFR_TYP_CCCEN_SHIFT		19
#define SDHC_CMD_XFR_TYP_CCCEN			(1 << SDHC_CMD_XFR_TYP_CCCEN_SHIFT)
#define SDHC_CMD_XFR_TYP_RSPTYP_SHIFT		16
#define SDHC_CMD_XFR_TYP_RSP_NONE		(0x0 << SDHC_CMD_XFR_TYP_RSPTYP_SHIFT)
#define SDHC_CMD_XFR_TYP_RSP136			(0x1 << SDHC_CMD_XFR_TYP_RSPTYP_SHIFT)
#define SDHC_CMD_XFR_TYP_RSP48			(0x2 << SDHC_CMD_XFR_TYP_RSPTYP_SHIFT)
#define SDHC_CMD_XFR_TYP_RSP48B			(0x3 << SDHC_CMD_XFR_TYP_RSPTYP_SHIFT)
#define SDHC_PRES_STATE_WPSPL			(1 << 19)
#define SDHC_PRES_STATE_BREN			(1 << 11)
#define SDHC_PRES_STATE_BWEN			(1 << 10)
#define SDHC_PRES_STATE_SDSTB			(1 << 3)
#define SDHC_PRES_STATE_DLA			(1 << 2)
#define SDHC_PRES_STATE_CDIHB			(1 << 1)
#define SDHC_PRES_STATE_CIHB			(1 << 0)
#define SDHC_SYS_CTRL_RSTA			(1 << 24)
#define SDHC_SYS_CTRL_RSTC			(1 << 25)
#define SDHC_SYS_CTRL_RSTD			(1 << 26)
#define SDHC_SYS_CTRL_CLOCK_MASK		(0xfff << 4)
#define SDHC_SYS_CTRL_CLOCK_DIV_SHIFT		4
#define SDHC_SYS_CTRL_CLOCK_PRE_SHIFT		8
#define SDHC_SYS_CTRL_DTOCV_SHIFT		16
#define SDHC_INT_STATUS_CC			(1 << 0)
#define SDHC_INT_STATUS_TC			(1 << 1)
#define SDHC_INT_STATUS_BGE			(1 << 2)
#define SDHC_INT_STATUS_DINT			(1 << 3)
#define SDHC_INT_STATUS_BWR			(1 << 4)
#define SDHC_INT_STATUS_BRR			(1 << 5)
#define SDHC_INT_STATUS_CINS			(1 << 6)
#define SDHC_INT_STATUS_CRM			(1 << 7)
#define SDHC_INT_STATUS_CINT			(1 << 8)
#define SDHC_INT_STATUS_CTOE			(1 << 16)
#define SDHC_INT_STATUS_CCE			(1 << 17)
#define SDHC_INT_STATUS_CEBE			(1 << 18)
#define SDHC_INT_STATUS_CIC			(1 << 19)
#define SDHC_INT_STATUS_DTOE			(1 << 20)
#define SDHC_INT_STATUS_DCE			(1 << 21)
#define SDHC_INT_STATUS_DEBE			(1 << 22)
#define SDHC_INT_STATUS_DMAE			(1 << 28)
#define SDHC_INT_STATUS_CMD_ERR			(SDHC_INT_STATUS_CIC | SDHC_INT_STATUS_CEBE | SDHC_INT_STATUS_CCE)
#define SDHC_INT_STATUS_ERR			(SDHC_INT_STATUS_CTOE | SDHC_INT_STATUS_CCE | SDHC_INT_STATUS_CEBE | \
						 SDHC_INT_STATUS_CIC | SDHC_INT_STATUS_DTOE | SDHC_INT_STATUS_DCE | \
						 SDHC_INT_STATUS_DEBE | SDHC_INT_STATUS_DMAE)
#define SDHC_MIX_CTRL_DMAEN			(1 << 0)
#define SDHC_MIX_CTRL_BCEN			(1 << 1)
#define SDHC_MIX_CTRL_AC12EN			(1 << 2)
#define SDHC_MIX_CTRL_DTDSEL			(1 << 4)
#define SDHC_MIX_CTRL_MSBSEL			(1 << 5)
#define SDHC_PROT_CTRL_DTW_MASK			(0x3 << 1)
#define SDHC_PROT_CTRL_DTW_4BIT			(1 << 1)
#define SDHC_PROT_CTRL_DTW_8BIT			(1 << 2)
#define SDHC_PROT_CTRL_DMASEL_MASK		(0x3 << 8)
#define SDHC_PROT_CTRL_DMASEL_SDMA		(0x0 << 8)
#define SDHC_PROT_CTRL_DMASEL_ADMA1		(0x1 << 8)
#define SDHC_PROT_CTRL_DMASEL_ADMA2		(0x2 << 8)
#define SDHC_HOST_CTRL_CAP_MBL_SHIFT		16
#define SDHC_HOST_CTRL_CAP_MBL_MASK		0x7
#define SDHC_HOST_CTRL_CAP_ADMAS		(1 << 20)
#define SDHC_HOST_CTRL_CAP_HSS			(1 << 21)
#define SDHC_HOST_CTRL_CAP_VS33			(1 << 24)
#define SDHC_HOST_CTRL_CAP_VS30			(1 << 25)
#define SDHC_HOST_CTRL_CAP_VS18			(1 << 26)
#define SDHC_VEND_SPEC_FRC_SDCLK_ON		(1 << 8)
#define SDHC_WTMK_LVL_RD_WML_SHIFT		0
#define SDHC_WTMK_LVL_RD_BRST_LEN_SHIFT		8
#define SDHC_WTMK_LVL_WR_WML_SHIFT		16
#define SDHC_WTMK_LVL_WR_BRST_LEN_SHIFT		24

/* timeouts in seconds */
#define SDHC_COMMAND_TIMEOUT			1
#define SDHC_BUFFER_TIMEOUT			1
#define SDHC_TRANSFER_TIMEOUT			1
#define SDHC_DMA_TIMEOUT			3

#define SDHC_ADMA2_VALID			(1 << 0)
#define SDHC_ADMA2_END				(1 << 1)
#define SDHC_ADMA2_INT				(1 << 2)
#define SDHC_ADMA2_ACT				(3 << 4)
#define SDHC_ADMA2_ACT_NOP			(0 << 4)
#define SDHC_ADMA2_ACT_TRANS			(2 << 4)
#define SDHC_ADMA2_ACT_LINK			(3 << 4)

struct sdhc_adma2_descriptor32 {
	uint16_t	attribute;
	uint16_t	length;
	uint32_t	address;
} __packed;


int	imxesdhc_match(struct device *, void *, void *);
void	imxesdhc_attach(struct device *, struct device *, void *);

struct imxesdhc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_dma_tag_t		 sc_dmat;
	void			*sc_ih;		/* interrupt handler */
	int			 sc_node;
	uint32_t		 sc_gpio[3];
	uint32_t		 sc_vmmc;
	uint32_t		 sc_pwrseq;
	uint32_t		 sc_vdd;
	int			 sc_cookies[SDMMC_MAX_FUNCTIONS];
	u_int sc_flags;

	struct device		*sdmmc;		/* generic SD/MMC device */
	int			 clockbit;	/* clock control bit */
	u_int			 clkbase;	/* base clock freq. in KHz */
	int			 maxblklen;	/* maximum block length */
	int			 flags;		/* flags for this host */
	uint32_t		 ocr;		/* OCR value from caps */
	uint32_t		 intr_status;	/* soft interrupt status */
	uint32_t		 intr_error_status;

	bus_dmamap_t		 adma_map;
	bus_dma_segment_t	 adma_segs[1];
	caddr_t			 adma2;
};

/* Host controller functions called by the attachment driver. */
int	imxesdhc_intr(void *);

void	imxesdhc_clock_enable(uint32_t);
void	imxesdhc_pwrseq_pre(uint32_t);
void	imxesdhc_pwrseq_post(uint32_t);

/* RESET MODES */
#define MMC_RESET_DAT	1
#define MMC_RESET_CMD	2
#define MMC_RESET_ALL	(MMC_RESET_CMD|MMC_RESET_DAT)

#define HDEVNAME(sc)	((sc)->sc_dev.dv_xname)

/* flag values */
#define SHF_USE_DMA	0x0001

/* SDHC should only be accessed with 4 byte reads or writes. */
#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

int	imxesdhc_host_reset(sdmmc_chipset_handle_t);
uint32_t imxesdhc_host_ocr(sdmmc_chipset_handle_t);
int	imxesdhc_host_maxblklen(sdmmc_chipset_handle_t);
int	imxesdhc_card_detect(sdmmc_chipset_handle_t);
int	imxesdhc_bus_power(sdmmc_chipset_handle_t, uint32_t);
int	imxesdhc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	imxesdhc_bus_width(sdmmc_chipset_handle_t, int);
void	imxesdhc_card_intr_mask(sdmmc_chipset_handle_t, int);
void	imxesdhc_card_intr_ack(sdmmc_chipset_handle_t);
void	imxesdhc_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
int	imxesdhc_start_command(struct imxesdhc_softc *, struct sdmmc_command *);
int	imxesdhc_wait_state(struct imxesdhc_softc *, uint32_t, uint32_t);
int	imxesdhc_soft_reset(struct imxesdhc_softc *, int);
int	imxesdhc_wait_intr(struct imxesdhc_softc *, int, int);
void	imxesdhc_transfer_data(struct imxesdhc_softc *, struct sdmmc_command *);
void	imxesdhc_read_data(struct imxesdhc_softc *, u_char *, int);
void	imxesdhc_write_data(struct imxesdhc_softc *, u_char *, int);

//#define SDHC_DEBUG
#ifdef SDHC_DEBUG
int imxesdhcdebug = 20;
#define DPRINTF(n,s)	do { if ((n) <= imxesdhcdebug) printf s; } while (0)
#else
#define DPRINTF(n,s)	do {} while(0)
#endif

struct sdmmc_chip_functions imxesdhc_functions = {
	/* host controller reset */
	imxesdhc_host_reset,
	/* host controller capabilities */
	imxesdhc_host_ocr,
	imxesdhc_host_maxblklen,
	/* card detection */
	imxesdhc_card_detect,
	/* bus power and clock frequency */
	imxesdhc_bus_power,
	imxesdhc_bus_clock,
	imxesdhc_bus_width,
	/* command execution */
	imxesdhc_exec_command,
	/* card interrupt */
	imxesdhc_card_intr_mask,
	imxesdhc_card_intr_ack
};

struct cfdriver imxesdhc_cd = {
	NULL, "imxesdhc", DV_DULL
};

const struct cfattach imxesdhc_ca = {
	sizeof(struct imxesdhc_softc), imxesdhc_match, imxesdhc_attach
};

int
imxesdhc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx6q-usdhc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sl-usdhc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sx-usdhc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx7d-usdhc");
}

void
imxesdhc_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxesdhc_softc *sc = (struct imxesdhc_softc *) self;
	struct fdt_attach_args *faa = aux;
	struct sdmmcbus_attach_args saa;
	int error = 1, node, reg;
	uint32_t caps;
	uint32_t width;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_node = faa->fa_node;
	sc->sc_dmat = faa->fa_dmat;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("imxesdhc_attach: bus_space_map failed!");

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_SDMMC,
	   imxesdhc_intr, sc, sc->sc_dev.dv_xname);

	OF_getpropintarray(sc->sc_node, "cd-gpios", sc->sc_gpio,
	    sizeof(sc->sc_gpio));
	gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_INPUT);

	sc->sc_vmmc = OF_getpropint(sc->sc_node, "vmmc-supply", 0);
	sc->sc_pwrseq = OF_getpropint(sc->sc_node, "mmc-pwrseq", 0);

	/*
	 * Reset the host controller and enable interrupts.
	 */
	if (imxesdhc_host_reset(sc))
		return;

	/* Determine host capabilities. */
	caps = HREAD4(sc, SDHC_HOST_CTRL_CAP);
	if (OF_is_compatible(sc->sc_node, "fsl,imx6sl-usdhc") ||
	    OF_is_compatible(sc->sc_node, "fsl,imx6sx-usdhc") ||
	    OF_is_compatible(sc->sc_node, "fsl,imx7d-usdhc"))
		caps &= 0xffff0000;

	/* Use DMA if the host system and the controller support it. */
	if (ISSET(caps, SDHC_HOST_CTRL_CAP_ADMAS))
		SET(sc->flags, SHF_USE_DMA);

	/*
	 * Determine the base clock frequency. (2.2.24)
	 */
	sc->clkbase = clock_get_frequency(faa->fa_node, "per") / 1000;

	printf("%s: %d MHz base clock\n", DEVNAME(sc), sc->clkbase / 1000);

	/*
	 * Determine SD bus voltage levels supported by the controller.
	 */
	if (caps & SDHC_HOST_CTRL_CAP_VS18)
		SET(sc->ocr, MMC_OCR_1_65V_1_95V);
	if (caps & SDHC_HOST_CTRL_CAP_VS30)
		SET(sc->ocr, MMC_OCR_2_9V_3_0V | MMC_OCR_3_0V_3_1V);
	if (caps & SDHC_HOST_CTRL_CAP_VS33)
		SET(sc->ocr, MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V);

	/*
	 * Determine max block size.
	 */
	switch ((caps >> SDHC_HOST_CTRL_CAP_MBL_SHIFT)
	    & SDHC_HOST_CTRL_CAP_MBL_MASK) {
	case 0:
		sc->maxblklen = 512;
		break;
	case 1:
		sc->maxblklen = 1024;
		break;
	case 2:
		sc->maxblklen = 2048;
		break;
	case 3:
		sc->maxblklen = 4096;
		break;
	default:
		sc->maxblklen = 512;
		printf("invalid capability blocksize in capa %08x,"
		    " trying 512\n", caps);
	}

	/* somewhere this blksize might be used instead of the device's */
	sc->maxblklen = 512;

	if (ISSET(sc->flags, SHF_USE_DMA)) {
		int rseg;

		/* Allocate ADMA2 descriptor memory */
		error = bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE,
		    PAGE_SIZE, sc->adma_segs, 1, &rseg,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (error)
			goto adma_done;
		error = bus_dmamem_map(sc->sc_dmat, sc->adma_segs, rseg,
		    PAGE_SIZE, &sc->adma2, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
		if (error) {
			bus_dmamem_free(sc->sc_dmat, sc->adma_segs, rseg);
			goto adma_done;
		}
		error = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE,
		    0, BUS_DMA_WAITOK, &sc->adma_map);
		if (error) {
			bus_dmamem_unmap(sc->sc_dmat, sc->adma2, PAGE_SIZE);
			bus_dmamem_free(sc->sc_dmat, sc->adma_segs, rseg);
			goto adma_done;
		}
		error = bus_dmamap_load(sc->sc_dmat, sc->adma_map,
		    sc->adma2, PAGE_SIZE, NULL,
		    BUS_DMA_WAITOK | BUS_DMA_WRITE);
		if (error) {
			bus_dmamap_destroy(sc->sc_dmat, sc->adma_map);
			bus_dmamem_unmap(sc->sc_dmat, sc->adma2, PAGE_SIZE);
			bus_dmamem_free(sc->sc_dmat, sc->adma_segs, rseg);
			goto adma_done;
		}

	adma_done:
		if (error) {
			printf("%s: can't allocate DMA descriptor table\n",
			    DEVNAME(sc));
			CLR(sc->flags, SHF_USE_DMA);
		}
	}

	/*
	 * Attach the generic SD/MMC bus driver.  (The bus driver must
	 * not invoke any chipset functions before it is attached.)
	 */
	bzero(&saa, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &imxesdhc_functions;
	saa.sch = sc;
	saa.dmat = sc->sc_dmat;
	if (ISSET(sc->flags, SHF_USE_DMA)) {
		saa.caps |= SMC_CAPS_DMA;
		saa.max_seg = 65535;
	}

	if (caps & SDHC_HOST_CTRL_CAP_HSS)
		saa.caps |= SMC_CAPS_MMC_HIGHSPEED | SMC_CAPS_SD_HIGHSPEED;

	width = OF_getpropint(sc->sc_node, "bus-width", 1);
	if (width >= 8)
		saa.caps |= SMC_CAPS_8BIT_MODE;
	if (width >= 4)
		saa.caps |= SMC_CAPS_4BIT_MODE;

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		reg = OF_getpropint(node, "reg", -1);
		if (reg < 0 || reg >= SDMMC_MAX_FUNCTIONS)
			continue;
		sc->sc_cookies[reg] = node;
		saa.cookies[reg] = &sc->sc_cookies[reg];
	}

	sc->sdmmc = config_found(&sc->sc_dev, &saa, NULL);
	if (sc->sdmmc == NULL) {
		error = 0;
		return;
	}
}

void
imxesdhc_clock_enable(uint32_t phandle)
{
	uint32_t gpios[3];
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "gpio-gate-clock"))
		return;

	pinctrl_byname(node, "default");

	OF_getpropintarray(node, "enable-gpios", gpios, sizeof(gpios));
	gpio_controller_config_pin(&gpios[0], GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(&gpios[0], 1);
}

void
imxesdhc_pwrseq_pre(uint32_t phandle)
{
	uint32_t *gpios, *gpio;
	uint32_t clocks;
	int node;
	int len;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "mmc-pwrseq-simple"))
		return;

	pinctrl_byname(node, "default");

	clocks = OF_getpropint(node, "clocks", 0);
	if (clocks)
		imxesdhc_clock_enable(clocks);

	len = OF_getproplen(node, "reset-gpios");
	if (len <= 0)
		return;

	gpios = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", gpios, len);

	gpio = gpios;
	while (gpio && gpio < gpios + (len / sizeof(uint32_t))) {
		gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(gpio, 1);
		gpio = gpio_controller_next_pin(gpio);
	}

	free(gpios, M_TEMP, len);
}

void
imxesdhc_pwrseq_post(uint32_t phandle)
{
	uint32_t *gpios, *gpio;
	int node;
	int len;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "mmc-pwrseq-simple"))
		return;

	len = OF_getproplen(node, "reset-gpios");
	if (len <= 0)
		return;

	gpios = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", gpios, len);

	gpio = gpios;
	while (gpio && gpio < gpios + (len / sizeof(uint32_t))) {
		gpio_controller_set_pin(gpio, 0);
		gpio = gpio_controller_next_pin(gpio);
	}

	free(gpios, M_TEMP, len);
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int
imxesdhc_host_reset(sdmmc_chipset_handle_t sch)
{
	struct imxesdhc_softc *sc = sch;
	u_int32_t imask;
	int error;
	int s;

	s = splsdmmc();

	/* Disable all interrupts. */
	HWRITE4(sc, SDHC_INT_STATUS_EN, 0);
	HWRITE4(sc, SDHC_INT_SIGNAL_EN, 0);

	/*
	 * Reset the entire host controller and wait up to 100ms for
	 * the controller to clear the reset bit.
	 */
	if ((error = imxesdhc_soft_reset(sc, SDHC_SYS_CTRL_RSTA)) != 0) {
		splx(s);
		return (error);
	}

	/* Set data timeout counter value to max for now. */
	HSET4(sc, SDHC_SYS_CTRL, 0xe << SDHC_SYS_CTRL_DTOCV_SHIFT);

	/* Enable interrupts. */
	imask = SDHC_INT_STATUS_CC | SDHC_INT_STATUS_TC |
	    SDHC_INT_STATUS_BGE | SDHC_INT_STATUS_DINT |
	    SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR;

	imask |= SDHC_INT_STATUS_CTOE | SDHC_INT_STATUS_CCE |
	    SDHC_INT_STATUS_CEBE | SDHC_INT_STATUS_CIC |
	    SDHC_INT_STATUS_DTOE | SDHC_INT_STATUS_DCE |
	    SDHC_INT_STATUS_DEBE | SDHC_INT_STATUS_DMAE;

	HWRITE4(sc, SDHC_INT_STATUS_EN, imask);
	HWRITE4(sc, SDHC_INT_SIGNAL_EN, imask);

	/* Switch back to no-DMA/SDMA. */
	HCLR4(sc, SDHC_PROT_CTRL, SDHC_PROT_CTRL_DMASEL_MASK);

	/* Switch back to 1-bit bus. */
	HCLR4(sc, SDHC_PROT_CTRL, SDHC_PROT_CTRL_DTW_MASK);

	/* Set watermarks and burst lengths to something sane. */
	HWRITE4(sc, SDHC_WTMK_LVL,
	   (64 << SDHC_WTMK_LVL_RD_WML_SHIFT) |
	   (16 << SDHC_WTMK_LVL_RD_BRST_LEN_SHIFT) |
	   (64 << SDHC_WTMK_LVL_WR_WML_SHIFT) |
	   (16 << SDHC_WTMK_LVL_WR_BRST_LEN_SHIFT));

	splx(s);
	return 0;
}

uint32_t
imxesdhc_host_ocr(sdmmc_chipset_handle_t sch)
{
	struct imxesdhc_softc *sc = sch;

	return sc->ocr;
}

int
imxesdhc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	struct imxesdhc_softc *sc = sch;

	return sc->maxblklen;
}

/*
 * Return non-zero if the card is currently inserted.
 */
int
imxesdhc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct imxesdhc_softc *sc = sch;

	if (OF_getproplen(sc->sc_node, "non-removable") == 0)
		return 1;

	return gpio_controller_get_pin(sc->sc_gpio);
}

/*
 * Set or change SD bus voltage and enable or disable SD bus power.
 * Return zero on success.
 */
int
imxesdhc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	struct imxesdhc_softc *sc = sch;
	uint32_t vdd = 0;

	ocr &= sc->ocr;
	if (ISSET(ocr, MMC_OCR_3_2V_3_3V|MMC_OCR_3_3V_3_4V))
		vdd = 3300000;
	else if (ISSET(ocr, MMC_OCR_2_9V_3_0V|MMC_OCR_3_0V_3_1V))
		vdd = 3000000;
	else if (ISSET(ocr, MMC_OCR_1_65V_1_95V))
		vdd = 1800000;

	if (sc->sc_vdd == 0 && vdd > 0)
		imxesdhc_pwrseq_pre(sc->sc_pwrseq);

	/* enable mmc power */
	if (sc->sc_vmmc && vdd > 0)
		regulator_enable(sc->sc_vmmc);

	if (sc->sc_vdd == 0 && vdd > 0)
		imxesdhc_pwrseq_post(sc->sc_pwrseq);

	sc->sc_vdd = vdd;
	return 0;
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int
imxesdhc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct imxesdhc_softc *sc = sch;
	int div, pre_div, cur_freq, s;
	int error = 0;

	s = splsdmmc();

	if (sc->clkbase / 16 > freq) {
		for (pre_div = 2; pre_div < 256; pre_div *= 2)
			if ((sc->clkbase / pre_div) <= (freq * 16))
				break;
	} else
		pre_div = 2;

	if (sc->clkbase == freq)
		pre_div = 1;

	for (div = 1; div <= 16; div++)
		if ((sc->clkbase / (div * pre_div)) <= freq)
			break;

	div -= 1;
	pre_div >>= 1;

	cur_freq = sc->clkbase / (pre_div * 2) / (div + 1);

	/* disable force CLK output active */
	HCLR4(sc, SDHC_VEND_SPEC, SDHC_VEND_SPEC_FRC_SDCLK_ON);

	/* wait while clock is unstable */
	if ((error = imxesdhc_wait_state(sc,
	    SDHC_PRES_STATE_SDSTB, SDHC_PRES_STATE_SDSTB)) != 0)
		goto ret;

	HCLR4(sc, SDHC_SYS_CTRL, SDHC_SYS_CTRL_CLOCK_MASK);
	HSET4(sc, SDHC_SYS_CTRL,
	    (div << SDHC_SYS_CTRL_CLOCK_DIV_SHIFT) |
	    (pre_div << SDHC_SYS_CTRL_CLOCK_PRE_SHIFT));

	/* wait while clock is unstable */
	if ((error = imxesdhc_wait_state(sc,
	    SDHC_PRES_STATE_SDSTB, SDHC_PRES_STATE_SDSTB)) != 0)
		goto ret;

ret:
	splx(s);
	return error;
}

int
imxesdhc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct imxesdhc_softc *sc = sch;
	uint32_t reg;
	int s;

	if (width != 1 && width != 4 && width != 8)
		return (1);

	s = splsdmmc();

	reg = HREAD4(sc, SDHC_PROT_CTRL) & ~SDHC_PROT_CTRL_DTW_MASK;
	if (width == 4)
		reg |= SDHC_PROT_CTRL_DTW_4BIT;
	else if (width == 8)
		reg |= SDHC_PROT_CTRL_DTW_8BIT;
	HWRITE4(sc, SDHC_PROT_CTRL, reg);

	splx(s);

	return (0);
}

void
imxesdhc_card_intr_mask(sdmmc_chipset_handle_t sch, int enable)
{
	struct imxesdhc_softc *sc = sch;

	if (enable) {
		HSET4(sc, SDHC_INT_STATUS_EN, SDHC_INT_STATUS_CINT);
		HSET4(sc, SDHC_INT_SIGNAL_EN, SDHC_INT_STATUS_CINT);
	} else {
		HCLR4(sc, SDHC_INT_STATUS_EN, SDHC_INT_STATUS_CINT);
		HCLR4(sc, SDHC_INT_SIGNAL_EN, SDHC_INT_STATUS_CINT);
	}
}

void
imxesdhc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	struct imxesdhc_softc *sc = sch;

	HSET4(sc, SDHC_INT_STATUS_EN, SDHC_INT_STATUS_CINT);
}

int
imxesdhc_wait_state(struct imxesdhc_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;

	state = HREAD4(sc, SDHC_PRES_STATE);
	DPRINTF(3,("%s: wait_state %x %x %x)\n",
	    HDEVNAME(sc), mask, value, state));
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD4(sc, SDHC_PRES_STATE)) & mask) == value)
			return 0;
		delay(10);
	}
	DPRINTF(0,("%s: timeout waiting for %x, state %x\n",
	    HDEVNAME(sc), value, state));

	return ETIMEDOUT;
}

void
imxesdhc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct imxesdhc_softc *sc = sch;
	int error;

	/*
	 * Start the command, or mark `cmd' as failed and return.
	 */
	error = imxesdhc_start_command(sc, cmd);
	if (error != 0) {
		cmd->c_error = error;
		SET(cmd->c_flags, SCF_ITSDONE);
		return;
	}

	/*
	 * Wait until the command phase is done, or until the command
	 * is marked done for any other reason.
	 */
	if (!imxesdhc_wait_intr(sc, SDHC_INT_STATUS_CC, SDHC_COMMAND_TIMEOUT)) {
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
			cmd->c_resp[0] = HREAD4(sc, SDHC_CMD_RSP0);
			cmd->c_resp[1] = HREAD4(sc, SDHC_CMD_RSP1);
			cmd->c_resp[2] = HREAD4(sc, SDHC_CMD_RSP2);
			cmd->c_resp[3] = HREAD4(sc, SDHC_CMD_RSP3);
#ifdef SDHC_DEBUG
			printf("resp[0] 0x%08x\nresp[1] 0x%08x\n"
			    "resp[2] 0x%08x\nresp[3] 0x%08x\n",
			    cmd->c_resp[0],
			    cmd->c_resp[1],
			    cmd->c_resp[2],
			    cmd->c_resp[3]);
#endif
		} else  {
			cmd->c_resp[0] = HREAD4(sc, SDHC_CMD_RSP0);
#ifdef SDHC_DEBUG
			printf("resp[0] 0x%08x\n", cmd->c_resp[0]);
#endif
		}
	}

	/*
	 * If the command has data to transfer in any direction,
	 * execute the transfer now.
	 */
	if (cmd->c_error == 0 && cmd->c_data)
		imxesdhc_transfer_data(sc, cmd);

	DPRINTF(1,("%s: cmd %u done (flags=%#x error=%d)\n",
	    HDEVNAME(sc), cmd->c_opcode, cmd->c_flags, cmd->c_error));
	SET(cmd->c_flags, SCF_ITSDONE);
}

int
imxesdhc_start_command(struct imxesdhc_softc *sc, struct sdmmc_command *cmd)
{
	struct sdhc_adma2_descriptor32 *desc = (void *)sc->adma2;
	u_int32_t blksize = 0;
	u_int32_t blkcount = 0;
	u_int32_t command;
	int error;
	int seg;
	int s;

	DPRINTF(1,("%s: start cmd %u arg=%#x data=%p dlen=%d flags=%#x\n",
	    HDEVNAME(sc), cmd->c_opcode, cmd->c_arg, cmd->c_data,
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
			    HDEVNAME(sc), blksize);
			return EINVAL;
		}
	}

	/* Check limit imposed by 9-bit block count. (1.7.2) */
	if (blkcount > SDHC_BLK_ATT_BLKCNT_MAX) {
		printf("%s: too much data\n", HDEVNAME(sc));
		return EINVAL;
	}

	/* Check for write protection. */
	if (!ISSET(cmd->c_flags, SCF_CMD_READ)) {
		if (!(HREAD4(sc, SDHC_PRES_STATE) & SDHC_PRES_STATE_WPSPL)) {
			printf("%s: card is write protected\n",
			    HDEVNAME(sc));
			return EINVAL;
		}
	}

	/* Prepare transfer mode register value. (2.2.5) */
	command = 0;

	if (ISSET(cmd->c_flags, SCF_CMD_READ))
		command |= SDHC_MIX_CTRL_DTDSEL;
	if (blkcount > 0) {
		command |= SDHC_MIX_CTRL_BCEN;
		if (blkcount > 1) {
			command |= SDHC_MIX_CTRL_MSBSEL;
			if (cmd->c_opcode != SD_IO_RW_EXTENDED)
				command |= SDHC_MIX_CTRL_AC12EN;
		}
	}
	if (cmd->c_dmamap && cmd->c_datalen > 0 &&
	    ISSET(sc->flags, SHF_USE_DMA))
		command |= SDHC_MIX_CTRL_DMAEN;

	command |= (cmd->c_opcode << SDHC_CMD_XFR_TYP_CMDINDX_SHIFT) &
	   SDHC_CMD_XFR_TYP_CMDINDX_SHIFT_MASK;

	if (ISSET(cmd->c_flags, SCF_RSP_CRC))
		command |= SDHC_CMD_XFR_TYP_CCCEN;
	if (ISSET(cmd->c_flags, SCF_RSP_IDX))
		command |= SDHC_CMD_XFR_TYP_CICEN;
	if (cmd->c_data != NULL)
		command |= SDHC_CMD_XFR_TYP_DPSEL;

	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		command |= SDHC_CMD_XFR_TYP_RSP_NONE;
	else if (ISSET(cmd->c_flags, SCF_RSP_136))
		command |= SDHC_CMD_XFR_TYP_RSP136;
	else if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		command |= SDHC_CMD_XFR_TYP_RSP48B;
	else
		command |= SDHC_CMD_XFR_TYP_RSP48;

	/* Wait until command and data inhibit bits are clear. (1.5) */
	if ((error = imxesdhc_wait_state(sc, SDHC_PRES_STATE_CIHB, 0)) != 0)
		return error;

	s = splsdmmc();

	/* Set DMA start address if SHF_USE_DMA is set. */
	if (cmd->c_dmamap && ISSET(sc->flags, SHF_USE_DMA)) {
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

			desc[seg].attribute = htole16(attr);
			desc[seg].length = htole16(len);
			desc[seg].address = htole32(paddr);
		}

		desc[cmd->c_dmamap->dm_nsegs].attribute = htole16(0);

		bus_dmamap_sync(sc->sc_dmat, sc->adma_map, 0, PAGE_SIZE,
		    BUS_DMASYNC_PREWRITE);

		HCLR4(sc, SDHC_PROT_CTRL, SDHC_PROT_CTRL_DMASEL_MASK);
		HSET4(sc, SDHC_PROT_CTRL, SDHC_PROT_CTRL_DMASEL_ADMA2);

		HWRITE4(sc, SDHC_ADMA_SYS_ADDR,
		    sc->adma_map->dm_segs[0].ds_addr);
	} else
		HCLR4(sc, SDHC_PROT_CTRL, SDHC_PROT_CTRL_DMASEL_MASK);

	/*
	 * Start a CPU data transfer.  Writing to the high order byte
	 * of the SDHC_COMMAND register triggers the SD command. (1.5)
	 */
	HWRITE4(sc, SDHC_BLK_ATT, blkcount << SDHC_BLK_ATT_BLKCNT_SHIFT |
	    blksize << SDHC_BLK_ATT_BLKSIZE_SHIFT);
	HWRITE4(sc, SDHC_CMD_ARG, cmd->c_arg);
	HWRITE4(sc, SDHC_MIX_CTRL,
	    (HREAD4(sc, SDHC_MIX_CTRL) & (0xf << 22)) | (command & 0xffff));
	HWRITE4(sc, SDHC_CMD_XFR_TYP, command);

	splx(s);
	return 0;
}

void
imxesdhc_transfer_data(struct imxesdhc_softc *sc, struct sdmmc_command *cmd)
{
	u_char *datap = cmd->c_data;
	int i, datalen;
	int mask;
	int error;

	if (cmd->c_dmamap) {
		int status;

		error = 0;
		for (;;) {
			status = imxesdhc_wait_intr(sc,
			    SDHC_INT_STATUS_DINT|SDHC_INT_STATUS_TC,
			    SDHC_DMA_TIMEOUT);
			if (status & SDHC_INT_STATUS_TC)
				break;
			if (!status) {
				error = ETIMEDOUT;
				break;
			}
		}

		bus_dmamap_sync(sc->sc_dmat, sc->adma_map, 0, PAGE_SIZE,
		    BUS_DMASYNC_POSTWRITE);
		goto done;
	}

	mask = ISSET(cmd->c_flags, SCF_CMD_READ) ?
	    SDHC_PRES_STATE_BREN : SDHC_PRES_STATE_BWEN;
	error = 0;
	datalen = cmd->c_datalen;

	DPRINTF(1,("%s: resp=%#x datalen=%d\n",
	    HDEVNAME(sc), MMC_R1(cmd->c_resp), datalen));

	while (datalen > 0) {
		if (!imxesdhc_wait_intr(sc,
		    SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR,
		    SDHC_BUFFER_TIMEOUT)) {
			error = ETIMEDOUT;
			break;
		}

		if ((error = imxesdhc_wait_state(sc, mask, mask)) != 0)
			break;

		/* FIXME: wait a bit, else it fails */
		delay(100);
		i = MIN(datalen, cmd->c_blklen);
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			imxesdhc_read_data(sc, datap, i);
		else
			imxesdhc_write_data(sc, datap, i);

		datap += i;
		datalen -= i;
	}

	if (error == 0 && !imxesdhc_wait_intr(sc, SDHC_INT_STATUS_TC,
	    SDHC_TRANSFER_TIMEOUT))
		error = ETIMEDOUT;

done:
	if (error != 0)
		cmd->c_error = error;
	SET(cmd->c_flags, SCF_ITSDONE);

	DPRINTF(1,("%s: data transfer done (error=%d)\n",
	    HDEVNAME(sc), cmd->c_error));
}

void
imxesdhc_read_data(struct imxesdhc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 3) {
		*(uint32_t *)datap = HREAD4(sc, SDHC_DATA_BUFF_ACC_PORT);
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = HREAD4(sc, SDHC_DATA_BUFF_ACC_PORT);
		do {
			*datap++ = rv & 0xff;
			rv = rv >> 8;
		} while (--datalen > 0);
	}
}

void
imxesdhc_write_data(struct imxesdhc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 3) {
		DPRINTF(3,("%08x\n", *(uint32_t *)datap));
		HWRITE4(sc, SDHC_DATA_BUFF_ACC_PORT, *((uint32_t *)datap));
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
		HWRITE4(sc, SDHC_DATA_BUFF_ACC_PORT, rv);
	}
}

/* Prepare for another command. */
int
imxesdhc_soft_reset(struct imxesdhc_softc *sc, int mask)
{
	int timo;

	DPRINTF(1,("%s: software reset reg=%#x\n", HDEVNAME(sc), mask));

	/* disable force CLK output active */
	HCLR4(sc, SDHC_VEND_SPEC, SDHC_VEND_SPEC_FRC_SDCLK_ON);

	/* reset */
	HSET4(sc, SDHC_SYS_CTRL, mask);
	delay(10);

	for (timo = 1000; timo > 0; timo--) {
		if (!ISSET(HREAD4(sc, SDHC_SYS_CTRL), mask))
			break;
		delay(10);
	}
	if (timo == 0) {
		DPRINTF(1,("%s: timeout reg=%#x\n", HDEVNAME(sc),
		    HREAD4(sc, SDHC_SYS_CTRL)));
		return ETIMEDOUT;
	}

	return 0;
}

int
imxesdhc_wait_intr(struct imxesdhc_softc *sc, int mask, int secs)
{
	int status;
	int s;

	mask |= SDHC_INT_STATUS_ERR;
	s = splsdmmc();

	/* enable interrupts for brr and bwr */
	if (mask & (SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR))
		HSET4(sc, SDHC_INT_SIGNAL_EN,
		    (SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR));

	status = sc->intr_status & mask;
	while (status == 0) {
		if (tsleep_nsec(&sc->intr_status, PWAIT, "hcintr",
		    SEC_TO_NSEC(secs)) == EWOULDBLOCK) {
			status |= SDHC_INT_STATUS_ERR;
			break;
		}
		status = sc->intr_status & mask;
	}
	sc->intr_status &= ~status;
	DPRINTF(2,("%s: intr status %#x error %#x\n", HDEVNAME(sc), status,
	    sc->intr_error_status));

	/* Command timeout has higher priority than command complete. */
	if (ISSET(status, SDHC_INT_STATUS_ERR)) {
		sc->intr_error_status = 0;
		(void)imxesdhc_soft_reset(sc,
		    SDHC_SYS_CTRL_RSTC | SDHC_SYS_CTRL_RSTD);
		status = 0;
	}

	splx(s);
	return status;
}

/*
 * Established by attachment driver at interrupt priority IPL_SDMMC.
 */
int
imxesdhc_intr(void *arg)
{
	struct imxesdhc_softc *sc = arg;

	u_int32_t status;

	/* Find out which interrupts are pending. */
	status = HREAD4(sc, SDHC_INT_STATUS);

	/* disable interrupts for brr and bwr, else we get flooded */
	if (status & (SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR))
		HCLR4(sc, SDHC_INT_SIGNAL_EN,
		    (SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR));

	/* Acknowledge the interrupts we are about to handle. */
	HWRITE4(sc, SDHC_INT_STATUS, status);
	DPRINTF(2,("%s: interrupt status=0x%08x\n", HDEVNAME(sc), status));

	/*
	 * Service error interrupts.
	 */
	if (ISSET(status, SDHC_INT_STATUS_CMD_ERR |
	    SDHC_INT_STATUS_CTOE | SDHC_INT_STATUS_DTOE)) {
		sc->intr_status |= status;
		sc->intr_error_status |= status & 0xffff0000;
		wakeup(&sc->intr_status);
	}

	/*
	 * Wake up the blocking process to service command
	 * related interrupt(s).
	 */
	if (ISSET(status, SDHC_INT_STATUS_BRR | SDHC_INT_STATUS_BWR |
	    SDHC_INT_STATUS_TC | SDHC_INT_STATUS_CC)) {
		sc->intr_status |= status;
		wakeup(&sc->intr_status);
	}

	/*
	 * Service SD card interrupts.
	 */
	if (ISSET(status, SDHC_INT_STATUS_CINT)) {
		DPRINTF(0,("%s: card interrupt\n", HDEVNAME(sc)));
		HCLR4(sc, SDHC_INT_STATUS_EN, SDHC_INT_STATUS_CINT);
		sdmmc_card_intr(sc->sdmmc);
	}

	return 1;
}
