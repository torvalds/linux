/*	$OpenBSD: amlmmc.c,v 1.12 2022/01/09 05:42:37 jsg Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/sdmmc/sdmmcvar.h>

#define SD_EMMC_CLOCK		0x0000
#define  SD_EMMC_CLOCK_ALWAYS_ON	(1 << 28)
#define  SD_EMMC_CLOCK_RX_PHASE_0	(0 << 12)
#define  SD_EMMC_CLOCK_RX_PHASE_90	(1 << 12)
#define  SD_EMMC_CLOCK_RX_PHASE_180	(2 << 12)
#define  SD_EMMC_CLOCK_RX_PHASE_270	(3 << 12)
#define  SD_EMMC_CLOCK_TX_PHASE_0	(0 << 10)
#define  SD_EMMC_CLOCK_TX_PHASE_90	(1 << 10)
#define  SD_EMMC_CLOCK_TX_PHASE_180	(2 << 10)
#define  SD_EMMC_CLOCK_TX_PHASE_270	(3 << 10)
#define  SD_EMMC_CLOCK_CO_PHASE_0	(0 << 8)
#define  SD_EMMC_CLOCK_CO_PHASE_90	(1 << 8)
#define  SD_EMMC_CLOCK_CO_PHASE_180	(2 << 8)
#define  SD_EMMC_CLOCK_CO_PHASE_270	(3 << 8)
#define  SD_EMMC_CLOCK_CLK_SRC_24M	(0 << 6)
#define  SD_EMMC_CLOCK_CLK_SRC_FCLK	(1 << 6)
#define  SD_EMMC_CLOCK_DIV_MAX		63
#define SD_EMMC_DELAY1		0x0004
#define SD_EMMC_DELAY2		0x0008
#define SD_EMMC_ADJUST		0x000c
#define  SD_EMMC_ADJUST_ADJ_FIXED	(1 << 13)
#define  SD_EMMC_ADJUST_ADJ_DELAY_MASK	(0x3f << 16)
#define  SD_EMMC_ADJUST_ADJ_DELAY_SHIFT	16
#define SD_EMMC_START		0x0040
#define  SD_EMMC_START_START		(1 << 1)
#define  SD_EMMC_START_STOP		(0 << 1)
#define SD_EMMC_CFG		0x0044
#define  SD_EMMC_CFG_ERR_ABORT		(1 << 27)
#define  SD_EMMC_CFG_AUTO_CLK		(1 << 23)
#define  SD_EMMC_CFG_STOP_CLOCK		(1 << 22)
#define  SD_EMMC_CFG_SDCLK_ALWAYS_ON	(1 << 18)
#define  SD_EMMC_CFG_RC_CC_MASK		(0xf << 12)
#define  SD_EMMC_CFG_RC_CC_16		(0x4 << 12)
#define  SD_EMMC_CFG_RESP_TIMEOUT_MASK	(0xf << 8)
#define  SD_EMMC_CFG_RESP_TIMEOUT_256	(0x8 << 8)
#define  SD_EMMC_CFG_BL_LEN_MASK	(0xf << 4)
#define  SD_EMMC_CFG_BL_LEN_SHIFT	4
#define  SD_EMMC_CFG_BL_LEN_512		(0x9 << 4)
#define  SD_EMMC_CFG_DDR		(1 << 2)
#define  SD_EMMC_CFG_BUS_WIDTH_MASK	(0x3 << 0)
#define  SD_EMMC_CFG_BUS_WIDTH_1	(0x0 << 0)
#define  SD_EMMC_CFG_BUS_WIDTH_4	(0x1 << 0)
#define  SD_EMMC_CFG_BUS_WIDTH_8	(0x2 << 0)
#define SD_EMMC_STATUS		0x0048
#define  SD_EMMC_STATUS_END_OF_CHAIN	(1 << 13)
#define  SD_EMMC_STATUS_DESC_TIMEOUT	(1 << 12)
#define  SD_EMMC_STATUS_RESP_TIMEOUT	(1 << 11)
#define  SD_EMMC_STATUS_MASK		0x00003fff
#define  SD_EMMC_STATUS_ERR_MASK	0x000007ff
#define SD_EMMC_IRQ_EN		0x004c
#define  SD_EMMC_IRQ_EN_MASK		SD_EMMC_STATUS_MASK
#define SD_EMMC_CMD_CFG		0x0050
#define  SD_EMMC_CMD_CFG_BLOCK_MODE	(1 << 9)
#define  SD_EMMC_CMD_CFG_R1B		(1 << 10)
#define  SD_EMMC_CMD_CFG_END_OF_CHAIN	(1 << 11)
#define  SD_EMMC_CMD_CFG_TIMEOUT_1024	(10 << 12)
#define  SD_EMMC_CMD_CFG_TIMEOUT_4096	(12 << 12)
#define  SD_EMMC_CMD_CFG_NO_RESP	(1 << 16)
#define  SD_EMMC_CMD_CFG_NO_CMD		(1 << 17)
#define  SD_EMMC_CMD_CFG_DATA_IO	(1 << 18)
#define  SD_EMMC_CMD_CFG_DATA_WR	(1 << 19)
#define  SD_EMMC_CMD_CFG_RESP_NOCRC	(1 << 20)
#define  SD_EMMC_CMD_CFG_RESP_128	(1 << 21)
#define  SD_EMMC_CMD_CFG_CMD_INDEX_SHIFT 24
#define  SD_EMMC_CMD_CFG_OWNER		(1U << 31)
#define SD_EMMC_CMD_ARG		0x0054
#define SD_EMMC_CMD_DAT		0x0058
#define SD_EMMC_CMD_RSP		0x005c
#define SD_EMMC_CMD_RSP1	0x0060
#define SD_EMMC_CMD_RSP2	0x0064
#define SD_EMMC_CMD_RSP3	0x0068

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlmmc_desc {
	uint32_t cmd_cfg;
	uint32_t cmd_arg;
	uint32_t data_addr;
	uint32_t resp_addr;
};

#define AMLMMC_NDESC		(PAGE_SIZE / sizeof(struct amlmmc_desc))
#define AMLMMC_MAXSEGSZ		0x20000

struct amlmmc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmap;

	void			*sc_ih;
	uint32_t		sc_status;

	bus_dmamap_t		sc_desc_map;
	bus_dma_segment_t	sc_desc_segs[1];
	int			sc_desc_nsegs;
	caddr_t			sc_desc;

	int			sc_node;
	uint32_t		sc_clkin0;
	uint32_t		sc_clkin1;
	uint32_t		sc_gpio[4];
	uint32_t		sc_vmmc;
	uint32_t		sc_vqmmc;
	uint32_t		sc_pwrseq;
	uint32_t		sc_vdd;
	uint32_t		sc_ocr;

	int			sc_blklen;
	struct device		*sc_sdmmc;
};

int amlmmc_match(struct device *, void *, void *);
void amlmmc_attach(struct device *, struct device *, void *);

const struct cfattach	amlmmc_ca = {
	sizeof (struct amlmmc_softc), amlmmc_match, amlmmc_attach
};

struct cfdriver amlmmc_cd = {
	NULL, "amlmmc", DV_DULL
};

int	amlmmc_alloc_descriptors(struct amlmmc_softc *);
void	amlmmc_free_descriptors(struct amlmmc_softc *);
int	amlmmc_intr(void *);

void	amlmmc_pwrseq_reset(uint32_t);

int	amlmmc_host_reset(sdmmc_chipset_handle_t);
uint32_t amlmmc_host_ocr(sdmmc_chipset_handle_t);
int	amlmmc_host_maxblklen(sdmmc_chipset_handle_t);
int	amlmmc_card_detect(sdmmc_chipset_handle_t);
int	amlmmc_bus_power(sdmmc_chipset_handle_t, uint32_t);
int	amlmmc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	amlmmc_bus_width(sdmmc_chipset_handle_t, int);
void	amlmmc_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
int	amlmmc_signal_voltage(sdmmc_chipset_handle_t, int);
int	amlmmc_execute_tuning(sdmmc_chipset_handle_t, int);

struct sdmmc_chip_functions amlmmc_chip_functions = {
	.host_reset = amlmmc_host_reset,
	.host_ocr = amlmmc_host_ocr,
	.host_maxblklen = amlmmc_host_maxblklen,
	.card_detect = amlmmc_card_detect,
	.bus_power = amlmmc_bus_power,
	.bus_clock = amlmmc_bus_clock,
	.bus_width = amlmmc_bus_width,
	.exec_command = amlmmc_exec_command,
	.signal_voltage = amlmmc_signal_voltage,
	.execute_tuning = amlmmc_execute_tuning,
};

int
amlmmc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "amlogic,meson-axg-mmc") ||
	    OF_is_compatible(faa->fa_node, "amlogic,meson-sm1-mmc"));
}

void
amlmmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlmmc_softc *sc = (struct amlmmc_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct sdmmcbus_attach_args saa;
	uint32_t cfg, width;
	int error;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_dmat = faa->fa_dmat;
	error = amlmmc_alloc_descriptors(sc);
	if (error) {
		printf(": can't allocate descriptors\n");
		goto unmap;
	}
	error = bus_dmamap_create(sc->sc_dmat, MAXPHYS, AMLMMC_NDESC,
	    AMLMMC_MAXSEGSZ, 0, BUS_DMA_WAITOK|BUS_DMA_ALLOCNOW, &sc->sc_dmap);
	if (error) {
		printf(": can't create DMA map\n");
		goto free;
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    amlmmc_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto destroy;
	}

	sc->sc_node = faa->fa_node;
	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	sc->sc_clkin0 = clock_get_frequency(faa->fa_node, "clkin0");
	sc->sc_clkin1 = clock_get_frequency(faa->fa_node, "clkin1");

	OF_getpropintarray(faa->fa_node, "cd-gpios", sc->sc_gpio,
	    sizeof(sc->sc_gpio));
	if (sc->sc_gpio[0])
		gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_INPUT);

	sc->sc_vmmc = OF_getpropint(sc->sc_node, "vmmc-supply", 0);
	sc->sc_vqmmc = OF_getpropint(sc->sc_node, "vqmmc-supply", 0);
	sc->sc_pwrseq = OF_getpropint(sc->sc_node, "mmc-pwrseq", 0);

	/* XXX Pretend we only support 3.3V for now. */
	sc->sc_ocr = MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V;

	/* Initialize timings and block size. */
	cfg = SD_EMMC_CFG_ERR_ABORT;
	cfg |= SD_EMMC_CFG_RC_CC_16;
	cfg |= SD_EMMC_CFG_RESP_TIMEOUT_256;
	cfg |= SD_EMMC_CFG_BL_LEN_512;
	HWRITE4(sc, SD_EMMC_CFG, cfg);

	/* Clear status bits & enable interrupts. */
	HWRITE4(sc, SD_EMMC_STATUS, SD_EMMC_STATUS_MASK);
	HWRITE4(sc, SD_EMMC_IRQ_EN, SD_EMMC_IRQ_EN_MASK);

	/* Reset eMMC. */
	if (sc->sc_pwrseq)
		amlmmc_pwrseq_reset(sc->sc_pwrseq);

	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &amlmmc_chip_functions;
	saa.sch = sc;
	saa.dmat = sc->sc_dmat;
	saa.dmap = sc->sc_dmap;
	saa.caps = SMC_CAPS_DMA;
	saa.flags = SMF_STOP_AFTER_MULTIPLE;

	if (OF_getproplen(sc->sc_node, "cap-mmc-highspeed") == 0)
		saa.caps |= SMC_CAPS_MMC_HIGHSPEED;
	if (OF_getproplen(sc->sc_node, "cap-sd-highspeed") == 0)
		saa.caps |= SMC_CAPS_SD_HIGHSPEED;
	if (OF_getproplen(sc->sc_node, "mmc-ddr-1_8v") == 0)
		saa.caps |= SMC_CAPS_MMC_DDR52;
	if (OF_getproplen(sc->sc_node, "mmc-hs200-1_8v") == 0)
		saa.caps |= SMC_CAPS_MMC_HS200;
	if (OF_getproplen(sc->sc_node, "sd-uhs-sdr50") == 0)
		saa.caps |= SMC_CAPS_UHS_SDR50;
#ifdef notyet
	if (OF_getproplen(sc->sc_node, "sd-uhs-sdr104") == 0)
		saa.caps |= SMC_CAPS_UHS_SDR104;
#endif

	if (saa.caps & SMC_CAPS_UHS_MASK)
		sc->sc_ocr |= MMC_OCR_S18A;

	width = OF_getpropint(faa->fa_node, "bus-width", 1);
	if (width >= 8)
		saa.caps |= SMC_CAPS_8BIT_MODE;
	if (width >= 4)
		saa.caps |= SMC_CAPS_4BIT_MODE;

	sc->sc_sdmmc = config_found(self, &saa, NULL);
	return;

destroy:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dmap);
free:
	amlmmc_free_descriptors(sc);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

int
amlmmc_alloc_descriptors(struct amlmmc_softc *sc)
{
	int error, rseg;

	/* Allocate descriptor memory */
	error = bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE,
	    PAGE_SIZE, sc->sc_desc_segs, 1, &rseg,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error)
		return error;
	error = bus_dmamem_map(sc->sc_dmat, sc->sc_desc_segs, rseg,
	    PAGE_SIZE, &sc->sc_desc, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error) {
		bus_dmamem_free(sc->sc_dmat, sc->sc_desc_segs, rseg);
		return error;
	}
	error = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE,
	    0, BUS_DMA_WAITOK, &sc->sc_desc_map);
	if (error) {
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_desc, PAGE_SIZE);
		bus_dmamem_free(sc->sc_dmat, sc->sc_desc_segs, rseg);
		return error;
	}
	error = bus_dmamap_load(sc->sc_dmat, sc->sc_desc_map,
	    sc->sc_desc, PAGE_SIZE, NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE);
	if (error) {
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_desc_map);
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_desc, PAGE_SIZE);
		bus_dmamem_free(sc->sc_dmat, sc->sc_desc_segs, rseg);
		return error;
	}

	sc->sc_desc_nsegs = rseg;
	return 0;
}

void
amlmmc_free_descriptors(struct amlmmc_softc *sc)
{
	bus_dmamap_unload(sc->sc_dmat, sc->sc_desc_map);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_desc_map);
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_desc, PAGE_SIZE);
	bus_dmamem_free(sc->sc_dmat, sc->sc_desc_segs, sc->sc_desc_nsegs);
}

int
amlmmc_intr(void *arg)
{
	struct amlmmc_softc *sc = arg;
	uint32_t status;

	status = HREAD4(sc, SD_EMMC_STATUS);
	if ((status & SD_EMMC_STATUS_MASK) == 0)
		return 0;

	HWRITE4(sc, SD_EMMC_STATUS, status);
	sc->sc_status = status & SD_EMMC_STATUS_MASK;
	wakeup(sc);
	return 1;
}

void
amlmmc_set_blklen(struct amlmmc_softc *sc, int blklen)
{
	uint32_t cfg;

	if (blklen == sc->sc_blklen)
		return;

	cfg = HREAD4(sc, SD_EMMC_CFG);
	cfg &= ~SD_EMMC_CFG_BL_LEN_MASK;
	cfg |= (fls(blklen) - 1) << SD_EMMC_CFG_BL_LEN_SHIFT;
	HWRITE4(sc, SD_EMMC_CFG, cfg);
	sc->sc_blklen = blklen;
}

void
amlmmc_pwrseq_reset(uint32_t phandle)
{
	uint32_t *gpios, *gpio;
	int node;
	int len;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "mmc-pwrseq-emmc"))
		return;

	len = OF_getproplen(node, "reset-gpios");
	if (len <= 0)
		return;

	gpios = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", gpios, len);

	gpio = gpios;
	while (gpio && gpio < gpios + (len / sizeof(uint32_t))) {
		gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(gpio, 1);
		delay(1);
		gpio_controller_set_pin(gpio, 0);
		delay(200);
		gpio = gpio_controller_next_pin(gpio);
	}

	free(gpios, M_TEMP, len);
}

int
amlmmc_host_reset(sdmmc_chipset_handle_t sch)
{
	printf("%s\n", __func__);
	return 0;
}

uint32_t
amlmmc_host_ocr(sdmmc_chipset_handle_t sch)
{
	struct amlmmc_softc *sc = sch;
	return sc->sc_ocr;
}

int
amlmmc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	return 512;
}

int
amlmmc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct amlmmc_softc *sc = sch;

	if (OF_getproplen(sc->sc_node, "non-removable") == 0)
		return 1;

	if (sc->sc_gpio[0]) {
		int inverted, val;

		val = gpio_controller_get_pin(sc->sc_gpio);

		inverted = (OF_getproplen(sc->sc_node, "cd-inverted") == 0);
		return inverted ? !val : val;
	}

	return 1;
}

int
amlmmc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	struct amlmmc_softc *sc = sch;
	uint32_t vdd = 0;

	if (ISSET(ocr, MMC_OCR_3_2V_3_3V|MMC_OCR_3_3V_3_4V))
		vdd = 3300000;

	/* enable mmc power */
	if (sc->sc_vmmc && vdd > 0)
		regulator_enable(sc->sc_vmmc);

	if (sc->sc_vqmmc && vdd > 0)
		regulator_enable(sc->sc_vqmmc);

	delay(10000);

	sc->sc_vdd = vdd;
	return 0;
}

int
amlmmc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct amlmmc_softc *sc = sch;
	uint32_t div, clock;

	/* XXX The ODROID-N2 eMMC doesn't work properly above 150 MHz. */
	if (freq > 150000)
		freq = 150000;

	pinctrl_byname(sc->sc_node, "clk-gate");
	
	if (freq == 0)
		return 0;

	/* Convert clock frequency from kHz to Hz. */
	freq = freq * 1000;

	/* Double the clock rate for DDR modes. */
	if (timing == SDMMC_TIMING_MMC_DDR52)
		freq = freq * 2;

	if (freq < (sc->sc_clkin1 / SD_EMMC_CLOCK_DIV_MAX)) {
		div = (sc->sc_clkin0 + freq - 1) / freq;
		clock = SD_EMMC_CLOCK_CLK_SRC_24M | div;
	} else {
		div = (sc->sc_clkin1 + freq - 1) / freq;
		clock = SD_EMMC_CLOCK_CLK_SRC_FCLK | div;
	}
	if (div > SD_EMMC_CLOCK_DIV_MAX)
		return EINVAL;

	HSET4(sc, SD_EMMC_CFG, SD_EMMC_CFG_STOP_CLOCK);

	if (timing == SDMMC_TIMING_MMC_DDR52)
		HSET4(sc, SD_EMMC_CFG, SD_EMMC_CFG_DDR);
	else
		HCLR4(sc, SD_EMMC_CFG, SD_EMMC_CFG_DDR);

	clock |= SD_EMMC_CLOCK_ALWAYS_ON;
	clock |= SD_EMMC_CLOCK_CO_PHASE_180;
	clock |= SD_EMMC_CLOCK_TX_PHASE_0;
	clock |= SD_EMMC_CLOCK_RX_PHASE_0;
	HWRITE4(sc, SD_EMMC_CLOCK, clock);

	HCLR4(sc, SD_EMMC_CFG, SD_EMMC_CFG_STOP_CLOCK);

	pinctrl_byname(sc->sc_node, "default");
	
	return 0;
}

int
amlmmc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct amlmmc_softc *sc = sch;
	uint32_t cfg;

	cfg = HREAD4(sc, SD_EMMC_CFG);
	cfg &= ~SD_EMMC_CFG_BUS_WIDTH_MASK;
	switch (width) {
	case 1:
		cfg |= SD_EMMC_CFG_BUS_WIDTH_1;
		break;
	case 4:
		cfg |= SD_EMMC_CFG_BUS_WIDTH_4;
		break;
	case 8:
		cfg |= SD_EMMC_CFG_BUS_WIDTH_8;
		break;
	default:
		return EINVAL;
	}
	HWRITE4(sc, SD_EMMC_CFG, cfg);

	return 0;
}

void
amlmmc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct amlmmc_softc *sc = sch;
	uint32_t cmd_cfg, status;
	uint32_t data_addr = 0;
	int s;

	KASSERT(sc->sc_status == 0);

	/* Setup descriptor flags. */
	cmd_cfg = cmd->c_opcode << SD_EMMC_CMD_CFG_CMD_INDEX_SHIFT;
	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		cmd_cfg |= SD_EMMC_CMD_CFG_NO_RESP;
	if (ISSET(cmd->c_flags, SCF_RSP_136))
		cmd_cfg |= SD_EMMC_CMD_CFG_RESP_128;
	if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		cmd_cfg |= SD_EMMC_CMD_CFG_R1B;
	if (!ISSET(cmd->c_flags, SCF_RSP_CRC))
		cmd_cfg |= SD_EMMC_CMD_CFG_RESP_NOCRC;
	if (cmd->c_datalen > 0) {
		cmd_cfg |= SD_EMMC_CMD_CFG_DATA_IO;
		if (cmd->c_datalen >= cmd->c_blklen)
			cmd_cfg |= SD_EMMC_CMD_CFG_BLOCK_MODE;
		if (!ISSET(cmd->c_flags, SCF_CMD_READ))
			cmd_cfg |= SD_EMMC_CMD_CFG_DATA_WR;
		cmd_cfg |= SD_EMMC_CMD_CFG_TIMEOUT_4096;
	} else {
		cmd_cfg |= SD_EMMC_CMD_CFG_TIMEOUT_1024;
	}
	cmd_cfg |= SD_EMMC_CMD_CFG_OWNER;

	/* If we have multiple DMA segments we need to use descriptors. */
	if (cmd->c_datalen > 0 &&
	    cmd->c_dmamap && cmd->c_dmamap->dm_nsegs > 1) {
		struct amlmmc_desc *desc = (struct amlmmc_desc *)sc->sc_desc;
		int seg;

		for (seg = 0; seg < cmd->c_dmamap->dm_nsegs; seg++) {
			bus_addr_t addr = cmd->c_dmamap->dm_segs[seg].ds_addr;
			bus_size_t len = cmd->c_dmamap->dm_segs[seg].ds_len;

			if (seg == cmd->c_dmamap->dm_nsegs - 1)
				cmd_cfg |= SD_EMMC_CMD_CFG_END_OF_CHAIN;

			KASSERT((addr & 0x7) == 0);
			desc[seg].cmd_cfg = cmd_cfg | (len / cmd->c_blklen);
			desc[seg].cmd_arg = cmd->c_arg;
			desc[seg].data_addr = addr;
			desc[seg].resp_addr = 0;
			cmd_cfg |= SD_EMMC_CMD_CFG_NO_CMD;
		}

		bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
		    cmd->c_dmamap->dm_nsegs * sizeof(struct amlmmc_desc),
		    BUS_DMASYNC_PREWRITE);
		HWRITE4(sc, SD_EMMC_START, SD_EMMC_START_START |
		    sc->sc_desc_map->dm_segs[0].ds_addr);
		goto wait;
	}

	/* Bounce if we don't have a DMA map. */
	if (cmd->c_datalen > 0 && !cmd->c_dmamap) {
		/* Abuse DMA descriptor memory as bounce buffer. */
		KASSERT(cmd->c_datalen <= PAGE_SIZE);
		if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
			    cmd->c_datalen, BUS_DMASYNC_PREREAD);
		} else {
			memcpy(sc->sc_desc, cmd->c_data, cmd->c_datalen);
			bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
			    cmd->c_datalen, BUS_DMASYNC_PREWRITE);
		}
	}

	if (cmd->c_datalen > 0) {
		if (cmd->c_datalen >= cmd->c_blklen) {
			amlmmc_set_blklen(sc, cmd->c_blklen);
			cmd_cfg |= cmd->c_datalen / cmd->c_blklen;
		} else {
			cmd_cfg |= cmd->c_datalen;
		}

		if (cmd->c_dmamap)
			data_addr = cmd->c_dmamap->dm_segs[0].ds_addr;
		else
			data_addr = sc->sc_desc_map->dm_segs[0].ds_addr;
	}

	cmd_cfg |= SD_EMMC_CMD_CFG_END_OF_CHAIN;

	KASSERT((data_addr & 0x7) == 0);
	HWRITE4(sc, SD_EMMC_CMD_CFG, cmd_cfg);
	HWRITE4(sc, SD_EMMC_CMD_DAT, data_addr);
	HWRITE4(sc, SD_EMMC_CMD_RSP, 0);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, SD_EMMC_CMD_CFG,
	    SD_EMMC_CMD_RSP1 - SD_EMMC_CMD_CFG, BUS_SPACE_BARRIER_WRITE);
	HWRITE4(sc, SD_EMMC_CMD_ARG, cmd->c_arg);

wait:
	s = splbio();
	while (sc->sc_status == 0) {
		if (tsleep_nsec(sc, PWAIT, "amlmmc", 10000000000))
			break;
	}
	status = sc->sc_status;
	sc->sc_status = 0;
	splx(s);

	if (!ISSET(status, SD_EMMC_STATUS_END_OF_CHAIN))
		cmd->c_error = ETIMEDOUT;
	else if (ISSET(status, SD_EMMC_STATUS_DESC_TIMEOUT))
		cmd->c_error = ETIMEDOUT;
	else if (ISSET(status, SD_EMMC_STATUS_RESP_TIMEOUT))
		cmd->c_error = ETIMEDOUT;
	else if (ISSET(status, SD_EMMC_STATUS_ERR_MASK))
		cmd->c_error = EIO;

	if (ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (ISSET(cmd->c_flags, SCF_RSP_136)) {
			cmd->c_resp[0] = HREAD4(sc, SD_EMMC_CMD_RSP);
			cmd->c_resp[1] = HREAD4(sc, SD_EMMC_CMD_RSP1);
			cmd->c_resp[2] = HREAD4(sc, SD_EMMC_CMD_RSP2);
			cmd->c_resp[3] = HREAD4(sc, SD_EMMC_CMD_RSP3);
			if (ISSET(cmd->c_flags, SCF_RSP_CRC)) {
				cmd->c_resp[0] = (cmd->c_resp[0] >> 8) |
				    (cmd->c_resp[1] << 24);
				cmd->c_resp[1] = (cmd->c_resp[1] >> 8) |
				    (cmd->c_resp[2] << 24);
				cmd->c_resp[2] = (cmd->c_resp[2] >> 8) |
				    (cmd->c_resp[3] << 24);
				cmd->c_resp[3] = (cmd->c_resp[3] >> 8);
			}
		} else {
			cmd->c_resp[0] = HREAD4(sc, SD_EMMC_CMD_RSP);
		}
	}

	/* Unbounce if we don't have a DMA map. */
	if (cmd->c_datalen > 0 && !cmd->c_dmamap) {
		if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
			    cmd->c_datalen, BUS_DMASYNC_POSTREAD);
			memcpy(cmd->c_data, sc->sc_desc, cmd->c_datalen);
		} else {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
			    cmd->c_datalen, BUS_DMASYNC_POSTWRITE);
		}
	}

	/* Cleanup descriptors. */
	if (cmd->c_datalen > 0 &&
	    cmd->c_dmamap && cmd->c_dmamap->dm_nsegs > 1) {
		HWRITE4(sc, SD_EMMC_START, SD_EMMC_START_STOP);
		bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
		    cmd->c_dmamap->dm_nsegs * sizeof(struct amlmmc_desc),
		    BUS_DMASYNC_POSTWRITE);
	}

	SET(cmd->c_flags, SCF_ITSDONE);
}

int
amlmmc_signal_voltage(sdmmc_chipset_handle_t sch, int signal_voltage)
{
	struct amlmmc_softc *sc = sch;
	uint32_t vccq;

	if (sc->sc_vqmmc == 0)
		return ENODEV;

	switch (signal_voltage) {
	case SDMMC_SIGNAL_VOLTAGE_180:
		vccq = 1800000;
		break;
	case SDMMC_SIGNAL_VOLTAGE_330:
		vccq = 3300000;
		break;
	default:
		return EINVAL;
	}

	if (regulator_get_voltage(sc->sc_vqmmc) == vccq)
		return 0;

	return regulator_set_voltage(sc->sc_vqmmc, vccq);
}

int
amlmmc_execute_tuning(sdmmc_chipset_handle_t sch, int timing)
{
	struct amlmmc_softc *sc = sch;
	struct sdmmc_command cmd;
	uint32_t adjust, cfg, div;
	int opcode, delay;
	char data[128];

	switch (timing) {
	case SDMMC_TIMING_MMC_HS200:
		opcode = MMC_SEND_TUNING_BLOCK_HS200;
		break;
	case SDMMC_TIMING_UHS_SDR50:
	case SDMMC_TIMING_UHS_SDR104:
		opcode = MMC_SEND_TUNING_BLOCK;
		break;
	default:
		return EINVAL;
	}

	cfg = HREAD4(sc, SD_EMMC_CFG);
	div = HREAD4(sc, SD_EMMC_CLOCK) & SD_EMMC_CLOCK_DIV_MAX;

	adjust = HREAD4(sc, SD_EMMC_ADJUST);
	adjust |= SD_EMMC_ADJUST_ADJ_FIXED;
	HWRITE4(sc, SD_EMMC_ADJUST, adjust);

	for (delay = 0; delay < div; delay++) {
		adjust &= ~SD_EMMC_ADJUST_ADJ_DELAY_MASK;
		adjust |= (delay << SD_EMMC_ADJUST_ADJ_DELAY_SHIFT);
		HWRITE4(sc, SD_EMMC_ADJUST, adjust);

		memset(&cmd, 0, sizeof(cmd));
		cmd.c_opcode = opcode;
		cmd.c_arg = 0;
		cmd.c_flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;
		if (cfg & SD_EMMC_CFG_BUS_WIDTH_8) {
			cmd.c_blklen = cmd.c_datalen = 128;
		} else {
			cmd.c_blklen = cmd.c_datalen = 64;
		}
		cmd.c_data = data;

		amlmmc_exec_command(sch, &cmd);
		if (cmd.c_error == 0)
			return 0;
	}

	adjust = HREAD4(sc, SD_EMMC_ADJUST);
	adjust &= ~SD_EMMC_ADJUST_ADJ_FIXED;
	HWRITE4(sc, SD_EMMC_ADJUST, adjust);

	return EIO;
}
