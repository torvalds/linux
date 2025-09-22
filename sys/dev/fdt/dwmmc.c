/*	$OpenBSD: dwmmc.c,v 1.31 2024/12/19 18:02:47 patrick Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmc_ioreg.h>

#define SDMMC_CTRL		0x0000
#define  SDMMC_CTRL_USE_INTERNAL_DMAC	(1 << 25)
#define  SDMMC_CTRL_DMA_ENABLE		(1 << 5)
#define  SDMMC_CTRL_INT_ENABLE		(1 << 4)
#define  SDMMC_CTRL_DMA_RESET		(1 << 2)
#define  SDMMC_CTRL_FIFO_RESET		(1 << 1)
#define  SDMMC_CTRL_CONTROLLER_RESET	(1 << 0)
#define  SDMMC_CTRL_ALL_RESET	(SDMMC_CTRL_CONTROLLER_RESET | \
    SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET)
#define SDMMC_PWREN		0x0004
#define SDMMC_CLKDIV		0x0008
#define SDMMC_CLKSRC		0x000c
#define SDMMC_CLKENA		0x0010
#define  SDMMC_CLKENA_CCLK_LOW_POWER	(1 << 16)
#define  SDMMC_CLKENA_CCLK_ENABLE	(1 << 0)
#define SDMMC_TMOUT		0x0014
#define SDMMC_CTYPE		0x0018
#define  SDMMC_CTYPE_8BIT		(1 << 16)
#define  SDMMC_CTYPE_4BIT		(1 << 0)
#define SDMMC_BLKSIZ		0x001c
#define SDMMC_BYTCNT		0x0020
#define SDMMC_INTMASK		0x0024
#define SDMMC_CMDARG		0x0028
#define SDMMC_CMD		0x002c
#define  SDMMC_CMD_START_CMD		(1U << 31)
#define  SDMMC_CMD_USE_HOLD_REG		(1 << 29)
#define  SDMMC_CMD_UPDATE_CLOCK_REGISTERS_ONLY	(1 << 21)
#define  SDMMC_CMD_SEND_INITIALIZATION	(1 << 15)
#define  SDMMC_CMD_STOP_ABORT_CMD	(1 << 14)
#define  SDMMC_CMD_WAIT_PRVDATA_COMPLETE	(1 << 13)
#define  SDMMC_CMD_SEND_AUTO_STOP	(1 << 12)
#define  SDMMC_CMD_WR			(1 << 10)
#define  SDMMC_CMD_DATA_EXPECTED	(1 << 9)
#define  SDMMC_CMD_CHECK_REPONSE_CRC	(1 << 8)
#define  SDMMC_CMD_RESPONSE_LENGTH	(1 << 7)
#define  SDMMC_CMD_RESPONSE_EXPECT	(1 << 6)
#define SDMMC_RESP0		0x0030
#define SDMMC_RESP1		0x0034
#define SDMMC_RESP2		0x0038
#define SDMMC_RESP3		0x003c
#define SDMMC_MINTSTS		0x0040
#define SDMMC_RINTSTS		0x0044
#define  SDMMC_RINTSTS_SDIO		(1 << 24)
#define  SDMMC_RINTSTS_EBE		(1 << 15)
#define  SDMMC_RINTSTS_ACD		(1 << 14)
#define  SDMMC_RINTSTS_SBE		(1 << 13)
#define  SDMMC_RINTSTS_HLE		(1 << 12)
#define  SDMMC_RINTSTS_FRUN		(1 << 11)
#define  SDMMC_RINTSTS_HTO		(1 << 10)
#define  SDMMC_RINTSTS_DRTO		(1 << 9)
#define  SDMMC_RINTSTS_RTO		(1 << 8)
#define  SDMMC_RINTSTS_DCRC		(1 << 7)
#define  SDMMC_RINTSTS_RCRC		(1 << 6)
#define  SDMMC_RINTSTS_RXDR		(1 << 5)
#define  SDMMC_RINTSTS_TXDR		(1 << 4)
#define  SDMMC_RINTSTS_DTO		(1 << 3)
#define  SDMMC_RINTSTS_CD		(1 << 2)
#define  SDMMC_RINTSTS_RE		(1 << 1)
#define  SDMMC_RINTSTS_CDT		(1 << 0)
#define  SDMMC_RINTSTS_DATA_ERR	(SDMMC_RINTSTS_EBE | SDMMC_RINTSTS_SBE | \
    SDMMC_RINTSTS_HLE | SDMMC_RINTSTS_FRUN | SDMMC_RINTSTS_DCRC)
#define  SDMMC_RINTSTS_DATA_TO	(SDMMC_RINTSTS_HTO | SDMMC_RINTSTS_DRTO)
#define SDMMC_STATUS		0x0048
#define SDMMC_STATUS_FIFO_COUNT(x)	(((x) >> 17) & 0x1fff)
#define  SDMMC_STATUS_DATA_BUSY		(1 << 9)
#define SDMMC_FIFOTH		0x004c
#define  SDMMC_FIFOTH_MSIZE_SHIFT	28
#define  SDMMC_FIFOTH_RXWM_SHIFT	16
#define  SDMMC_FIFOTH_RXWM(x)		(((x) >> 16) & 0xfff)
#define  SDMMC_FIFOTH_TXWM_SHIFT	0
#define SDMMC_CDETECT		0x0050
#define  SDMMC_CDETECT_CARD_DETECT_0	(1 << 0)
#define SDMMC_WRTPRT		0x0054
#define SDMMC_TCBCNT		0x005c
#define SDMMC_TBBCNT		0x0060
#define SDMMC_DEBNCE		0x0064
#define SDMMC_USRID		0x0068
#define SDMMC_VERID		0x006c
#define SDMMC_HCON		0x0070
#define  SDMMC_HCON_DATA_WIDTH(x)	(((x) >> 7) & 0x7)
#define  SDMMC_HCON_DMA64		(1 << 27)
#define SDMMC_UHS_REG		0x0074
#define SDMMC_RST_n		0x0078
#define SDMMC_BMOD		0x0080
#define  SDMMC_BMOD_DE			(1 << 7)
#define  SDMMC_BMOD_FB			(1 << 1)
#define  SDMMC_BMOD_SWR			(1 << 0)
#define SDMMC_PLDMND		0x0084
#define SDMMC_DBADDR		0x0088
#define SDMMC_IDSTS32		0x008c
#define  SDMMC_IDSTS_NIS		(1 << 8)
#define  SDMMC_IDSTS_RI			(1 << 1)
#define  SDMMC_IDSTS_TI			(1 << 0)
#define SDMMC_IDINTEN32		0x0090
#define  SDMMC_IDINTEN_NI		(1 << 8)
#define  SDMMC_IDINTEN_RI		(1 << 1)
#define  SDMMC_IDINTEN_TI		(1 << 0)
#define SDMMC_DSCADDR		0x0094
#define SDMMC_BUFADDR		0x0098
#define SDMMC_CLKSEL		0x009c
#define SDMMC_CARDTHRCTL	0x0100
#define  SDMMC_CARDTHRCTL_RDTHR_SHIFT	16
#define  SDMMC_CARDTHRCTL_RDTHREN	(1 << 0)
#define SDMMC_BACK_END_POWER	0x0104
#define SDMMC_EMMC_DDR_REG	0x0108
#define SDMMC_FIFO_BASE		0x0200

#define SDMMC_DBADDRL		0x0088
#define SDMMC_DBADDRH		0x008c
#define SDMMC_IDSTS64		0x0090
#define SDMMC_IDINTEN64		0x0094
#define SDMMC_DSCADDRL		0x0098
#define SDMMC_DSCADDRH		0x009c
#define SDMMC_BUFADDRL		0x00a0
#define SDMMC_BUFADDRH		0x00a4

#define SDMMC_IDSTS(sc) \
    ((sc)->sc_dma64 ? SDMMC_IDSTS64 : SDMMC_IDSTS32)

#define HREAD4(sc, reg)							\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
    HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
    HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct dwmmc_desc32 {
	uint32_t des[4];
};

struct dwmmc_desc64 {
	uint32_t des[8];
};

#define DWMMC_NDESC	(PAGE_SIZE / sizeof(struct dwmmc_desc64))
#define DWMMC_MAXSEGSZ	0x1000

#define DES0_OWN	(1U << 31)
#define DES0_CES	(1 << 30)
#define DES0_ER		(1 << 5)
#define DES0_CH		(1 << 4)
#define DES0_FS		(1 << 3)
#define DES0_LD		(1 << 2)
#define DES0_DIC	(1 << 1)

#define DES1_BS2(sz)	(((sz) & 0x1fff) << 13)
#define DES1_BS1(sz)	(((sz) & 0x1fff) << 0)
#define DES2_BS2(sz)	DES1_BS2(sz)
#define DES2_BS1(sz)	DES1_BS1(sz)

struct dwmmc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_size;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmap;
	int			sc_node;

	void			*sc_ih;

	uint32_t		sc_clkbase;
	uint32_t		sc_fifo_depth;
	uint32_t		sc_fifo_width;
	void (*sc_read_data)(struct dwmmc_softc *, u_char *, int);
	void (*sc_write_data)(struct dwmmc_softc *, u_char *, int);
	int			sc_blklen;

	bus_dmamap_t		sc_desc_map;
	bus_dma_segment_t	sc_desc_segs[1];
	caddr_t			sc_desc;
	int			sc_dma64;
	int			sc_dmamode;
	uint32_t		sc_idsts;

	uint32_t		sc_gpio[4];
	int			sc_sdio_irq;
	uint32_t		sc_vqmmc;
	uint32_t		sc_pwrseq;
	uint32_t		sc_vdd;

	struct device		*sc_sdmmc;
};

int	dwmmc_match(struct device *, void *, void *);
void	dwmmc_attach(struct device *, struct device *, void *);

const struct cfattach dwmmc_ca = {
	sizeof(struct dwmmc_softc), dwmmc_match, dwmmc_attach
};

struct cfdriver dwmmc_cd = {
	NULL, "dwmmc", DV_DULL
};

int	dwmmc_intr(void *);

int	dwmmc_host_reset(sdmmc_chipset_handle_t);
uint32_t dwmmc_host_ocr(sdmmc_chipset_handle_t);
int	dwmmc_host_maxblklen(sdmmc_chipset_handle_t);
int	dwmmc_card_detect(sdmmc_chipset_handle_t);
int	dwmmc_bus_power(sdmmc_chipset_handle_t, uint32_t);
int	dwmmc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	dwmmc_bus_width(sdmmc_chipset_handle_t, int);
void	dwmmc_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
void	dwmmc_card_intr_mask(sdmmc_chipset_handle_t, int);
void	dwmmc_card_intr_ack(sdmmc_chipset_handle_t);
int	dwmmc_signal_voltage(sdmmc_chipset_handle_t, int);

struct sdmmc_chip_functions dwmmc_chip_functions = {
	.host_reset = dwmmc_host_reset,
	.host_ocr = dwmmc_host_ocr,
	.host_maxblklen = dwmmc_host_maxblklen,
	.card_detect = dwmmc_card_detect,
	.bus_power = dwmmc_bus_power,
	.bus_clock = dwmmc_bus_clock,
	.bus_width = dwmmc_bus_width,
	.exec_command = dwmmc_exec_command,
	.card_intr_mask = dwmmc_card_intr_mask,
	.card_intr_ack = dwmmc_card_intr_ack,
	.signal_voltage = dwmmc_signal_voltage,
};

void	dwmmc_pio_mode(struct dwmmc_softc *);
int	dwmmc_alloc_descriptors(struct dwmmc_softc *);
void	dwmmc_init_descriptors(struct dwmmc_softc *);
void	dwmmc_transfer_data(struct dwmmc_softc *, struct sdmmc_command *);
void	dwmmc_read_data32(struct dwmmc_softc *, u_char *, int);
void	dwmmc_write_data32(struct dwmmc_softc *, u_char *, int);
void	dwmmc_read_data64(struct dwmmc_softc *, u_char *, int);
void	dwmmc_write_data64(struct dwmmc_softc *, u_char *, int);
void	dwmmc_pwrseq_pre(uint32_t);
void	dwmmc_pwrseq_post(uint32_t);

int
dwmmc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "hisilicon,hi3660-dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "hisilicon,hi3670-dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3288-dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "samsung,exynos5420-dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "snps,dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "starfive,jh7110-mmc"));
}

void
dwmmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwmmc_softc *sc = (struct dwmmc_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct sdmmcbus_attach_args saa;
	uint32_t freq = 0, div = 0;
	uint32_t hcon, width;
	uint32_t fifoth;
	int error, timeout;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	sc->sc_size = faa->fa_reg[0].size;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	/*
	 * Determine FIFO width from hardware configuration register.
	 * We only support 32-bit and 64-bit FIFOs.
	 */
	hcon = HREAD4(sc, SDMMC_HCON);
	switch (SDMMC_HCON_DATA_WIDTH(hcon)) {
	case 1:
		sc->sc_fifo_width = 4;
		sc->sc_read_data = dwmmc_read_data32;
		sc->sc_write_data = dwmmc_write_data32;
		break;
	case 2:
		sc->sc_fifo_width = 8;
		sc->sc_read_data = dwmmc_read_data64;
		sc->sc_write_data = dwmmc_write_data64;
		break;
	default:
		printf(": unsupported FIFO width\n");
		return;
	}

	sc->sc_fifo_depth = OF_getpropint(faa->fa_node, "fifo-depth", 0);
	if (sc->sc_fifo_depth == 0) {
		fifoth = HREAD4(sc, SDMMC_FIFOTH);
		sc->sc_fifo_depth = SDMMC_FIFOTH_RXWM(fifoth) + 1;
	}

	if (hcon & SDMMC_HCON_DMA64)
		sc->sc_dma64 = 1;

	/* Some SoCs pre-divide the clock. */
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3288-dw-mshc"))
		div = 1;
	if (OF_is_compatible(faa->fa_node, "hisilicon,hi3660-dw-mshc") ||
	    OF_is_compatible(faa->fa_node, "hisilicon,hi3670-dw-mshc"))
		div = 7;

	/* Force the base clock to 50MHz on Rockchip SoCs. */
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3288-dw-mshc"))
		freq = 50000000;

	freq = OF_getpropint(faa->fa_node, "clock-frequency", freq);
	if (freq > 0)
		clock_set_frequency(faa->fa_node, "ciu", (div + 1) * freq);

	sc->sc_clkbase = clock_get_frequency(faa->fa_node, "ciu");
	/* if ciu clock is missing the rate is clock-frequency */
	if (sc->sc_clkbase == 0)
		sc->sc_clkbase = freq;
	if (sc->sc_clkbase == 0) {
		printf(": no clock base\n");
		return;
	}
	div = OF_getpropint(faa->fa_node, "samsung,dw-mshc-ciu-div", div);
	sc->sc_clkbase /= (div + 1);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    dwmmc_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	OF_getpropintarray(faa->fa_node, "cd-gpios", sc->sc_gpio,
	    sizeof(sc->sc_gpio));
	if (sc->sc_gpio[0])
		gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_INPUT);

	sc->sc_sdio_irq = (OF_getproplen(sc->sc_node, "cap-sdio-irq") == 0);
	sc->sc_vqmmc = OF_getpropint(sc->sc_node, "vqmmc-supply", 0);
	sc->sc_pwrseq = OF_getpropint(sc->sc_node, "mmc-pwrseq", 0);

	printf(": %d MHz base clock\n", sc->sc_clkbase / 1000000);

	HSET4(sc, SDMMC_CTRL, SDMMC_CTRL_ALL_RESET);
	for (timeout = 5000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SDMMC_CTRL) & SDMMC_CTRL_ALL_RESET) == 0)
			break;
		delay(100);
	}
	if (timeout == 0)
		printf("%s: reset failed\n", sc->sc_dev.dv_xname);

	/* Enable interrupts, but mask them all. */
	HWRITE4(sc, SDMMC_INTMASK, 0);
	HWRITE4(sc, SDMMC_RINTSTS, 0xffffffff);
	HSET4(sc, SDMMC_CTRL, SDMMC_CTRL_INT_ENABLE);

	dwmmc_bus_width(sc, 1);

	/* Start out in non-DMA mode. */
	dwmmc_pio_mode(sc);

	sc->sc_dmat = faa->fa_dmat;
	dwmmc_alloc_descriptors(sc);
	dwmmc_init_descriptors(sc);

	error = bus_dmamap_create(sc->sc_dmat, MAXPHYS, DWMMC_NDESC,
	    DWMMC_MAXSEGSZ, 0, BUS_DMA_WAITOK|BUS_DMA_ALLOCNOW, &sc->sc_dmap);
	if (error) {
		printf(": can't create DMA map\n");
		goto unmap;
	}

	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &dwmmc_chip_functions;
	saa.sch = sc;
	saa.dmat = sc->sc_dmat;
	saa.dmap = sc->sc_dmap;
	saa.caps |= SMC_CAPS_DMA;

	if (OF_getproplen(sc->sc_node, "cap-mmc-highspeed") == 0)
		saa.caps |= SMC_CAPS_MMC_HIGHSPEED;
	if (OF_getproplen(sc->sc_node, "cap-sd-highspeed") == 0)
		saa.caps |= SMC_CAPS_SD_HIGHSPEED;

	width = OF_getpropint(faa->fa_node, "bus-width", 1);
	if (width >= 8)
		saa.caps |= SMC_CAPS_8BIT_MODE;
	if (width >= 4)
		saa.caps |= SMC_CAPS_4BIT_MODE;

	sc->sc_sdmmc = config_found(self, &saa, NULL);
	return;

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
}

int
dwmmc_alloc_descriptors(struct dwmmc_softc *sc)
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

	return 0;
}

void
dwmmc_init_descriptors32(struct dwmmc_softc *sc)
{
	struct dwmmc_desc32 *desc;
	bus_addr_t addr;
	int i;

	desc = (void *)sc->sc_desc;
	addr = sc->sc_desc_map->dm_segs[0].ds_addr;
	for (i = 0; i < DWMMC_NDESC; i++) {
		addr += sizeof(struct dwmmc_desc32);
		desc[i].des[3] = addr;
	}
	desc[DWMMC_NDESC - 1].des[3] = sc->sc_desc_map->dm_segs[0].ds_addr;
	desc[DWMMC_NDESC - 1].des[0] = DES0_ER;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
	    PAGE_SIZE, BUS_DMASYNC_PREWRITE);

	HWRITE4(sc, SDMMC_IDSTS32, 0xffffffff);
	HWRITE4(sc, SDMMC_IDINTEN32,
	    SDMMC_IDINTEN_NI | SDMMC_IDINTEN_RI | SDMMC_IDINTEN_TI);
	HWRITE4(sc, SDMMC_DBADDR, sc->sc_desc_map->dm_segs[0].ds_addr);
}

void
dwmmc_init_descriptors64(struct dwmmc_softc *sc)
{
	struct dwmmc_desc64 *desc;
	bus_addr_t addr;
	int i;

	desc = (void *)sc->sc_desc;
	addr = sc->sc_desc_map->dm_segs[0].ds_addr;
	for (i = 0; i < DWMMC_NDESC; i++) {
		addr += sizeof(struct dwmmc_desc64);
		desc[i].des[6] = addr;
		desc[i].des[7] = (uint64_t)addr >> 32;
	}
	desc[DWMMC_NDESC - 1].des[6] = sc->sc_desc_map->dm_segs[0].ds_addr;
	desc[DWMMC_NDESC - 1].des[7] =
	    (uint64_t)sc->sc_desc_map->dm_segs[0].ds_addr >> 32;
	desc[DWMMC_NDESC - 1].des[0] = DES0_ER;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0,
	    PAGE_SIZE, BUS_DMASYNC_PREWRITE);

	HWRITE4(sc, SDMMC_IDSTS64, 0xffffffff);
	HWRITE4(sc, SDMMC_IDINTEN64,
	    SDMMC_IDINTEN_NI | SDMMC_IDINTEN_RI | SDMMC_IDINTEN_TI);
	HWRITE4(sc, SDMMC_DBADDRL, sc->sc_desc_map->dm_segs[0].ds_addr);
	HWRITE4(sc, SDMMC_DBADDRH,
	    (uint64_t)sc->sc_desc_map->dm_segs[0].ds_addr >> 32);
}

void
dwmmc_init_descriptors(struct dwmmc_softc *sc)
{
	if (sc->sc_dma64)
		dwmmc_init_descriptors64(sc);
	else
		dwmmc_init_descriptors32(sc);
}

int
dwmmc_intr(void *arg)
{
	struct dwmmc_softc *sc = arg;
	uint32_t stat;
	int handled = 0;

	stat = HREAD4(sc, SDMMC_IDSTS(sc));
	if (stat) {
		HWRITE4(sc, SDMMC_IDSTS(sc), stat);
		sc->sc_idsts |= stat;
		wakeup(&sc->sc_idsts);
		handled = 1;
	}

	stat = HREAD4(sc, SDMMC_MINTSTS);
	if (stat & SDMMC_RINTSTS_SDIO) {
		HWRITE4(sc, SDMMC_RINTSTS, SDMMC_RINTSTS_SDIO);
		HCLR4(sc, SDMMC_INTMASK, SDMMC_RINTSTS_SDIO);
		sdmmc_card_intr(sc->sc_sdmmc);
		handled = 1;
	}
		
	return handled;
}

void
dwmmc_card_intr_mask(sdmmc_chipset_handle_t sch, int enable)
{
	struct dwmmc_softc *sc = sch;

	if (enable)
		HSET4(sc, SDMMC_INTMASK, SDMMC_RINTSTS_SDIO);
	else
		HCLR4(sc, SDMMC_INTMASK, SDMMC_RINTSTS_SDIO);
}

void
dwmmc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	struct dwmmc_softc *sc = sch;

	HSET4(sc, SDMMC_INTMASK, SDMMC_RINTSTS_SDIO);
}

int
dwmmc_host_reset(sdmmc_chipset_handle_t sch)
{
	printf("%s\n", __func__);
	return 0;
}

uint32_t
dwmmc_host_ocr(sdmmc_chipset_handle_t sch)
{
	return MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V;
}

int
dwmmc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	return 512;
}

int
dwmmc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct dwmmc_softc *sc = sch;
	uint32_t cdetect;

	/* XXX treat broken-cd as non-removable */
	if (OF_getproplen(sc->sc_node, "non-removable") == 0 ||
	    OF_getproplen(sc->sc_node, "broken-cd") == 0)
		return 1;

	if (sc->sc_gpio[0]) {
		int inverted, val;

		val = gpio_controller_get_pin(sc->sc_gpio);

		inverted = (OF_getproplen(sc->sc_node, "cd-inverted") == 0);
		return inverted ? !val : val;
	}

	cdetect = HREAD4(sc, SDMMC_CDETECT);
	return !(cdetect & SDMMC_CDETECT_CARD_DETECT_0);
}

int
dwmmc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	struct dwmmc_softc *sc = sch;
	uint32_t vdd = 0;

	if (ISSET(ocr, MMC_OCR_3_2V_3_3V|MMC_OCR_3_3V_3_4V))
		vdd = 3300000;

	if (sc->sc_vdd == 0 && vdd > 0)
		dwmmc_pwrseq_pre(sc->sc_pwrseq);

	if (ISSET(ocr, MMC_OCR_3_2V_3_3V|MMC_OCR_3_3V_3_4V))
		HSET4(sc, SDMMC_PWREN, 1);
	else
		HCLR4(sc, SDMMC_PWREN, 0);

	if (sc->sc_vdd == 0 && vdd > 0)
		dwmmc_pwrseq_post(sc->sc_pwrseq);

	sc->sc_vdd = vdd;
	return 0;
}

int
dwmmc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct dwmmc_softc *sc = sch;
	int div = 0, timeout;
	uint32_t clkena;

	HWRITE4(sc, SDMMC_CLKENA, 0);
	HWRITE4(sc, SDMMC_CLKSRC, 0);

	if (freq == 0)
		return 0;
	
	if (sc->sc_clkbase / 1000 > freq) {
		for (div = 1; div < 256; div++)
			if (sc->sc_clkbase / (2 * 1000 * div) <= freq)
				break;
	}
	HWRITE4(sc, SDMMC_CLKDIV, div);

	/* Update clock. */
	HWRITE4(sc, SDMMC_CMD, SDMMC_CMD_START_CMD |
	    SDMMC_CMD_WAIT_PRVDATA_COMPLETE |
	    SDMMC_CMD_UPDATE_CLOCK_REGISTERS_ONLY);
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SDMMC_CMD) & SDMMC_CMD_START_CMD) == 0)
			break;
	}
	if (timeout == 0) {
		printf("%s: timeout\n", __func__);
		return ETIMEDOUT;
	}

	/* Enable clock; low power mode only for memory mode. */
	clkena = SDMMC_CLKENA_CCLK_ENABLE;
	if (!sc->sc_sdio_irq)
		clkena |= SDMMC_CLKENA_CCLK_LOW_POWER;
	HWRITE4(sc, SDMMC_CLKENA, clkena);

	/* Update clock again. */
	HWRITE4(sc, SDMMC_CMD, SDMMC_CMD_START_CMD |
	    SDMMC_CMD_WAIT_PRVDATA_COMPLETE |
	    SDMMC_CMD_UPDATE_CLOCK_REGISTERS_ONLY);
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SDMMC_CMD) & SDMMC_CMD_START_CMD) == 0)
			break;
	}
	if (timeout == 0) {
		printf("%s: timeout\n", __func__);
		return ETIMEDOUT;
	}

	delay(1000000);

	return 0;
}

int
dwmmc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct dwmmc_softc *sc = sch;
	
	switch (width) {
	case 1:
		HCLR4(sc, SDMMC_CTYPE, SDMMC_CTYPE_8BIT|SDMMC_CTYPE_4BIT);
		break;
	case 4:
		HSET4(sc, SDMMC_CTYPE, SDMMC_CTYPE_4BIT);
		HCLR4(sc, SDMMC_CTYPE, SDMMC_CTYPE_8BIT);
		break;
	case 8:
		HSET4(sc, SDMMC_CTYPE, SDMMC_CTYPE_8BIT);
		break;
	default:
		return EINVAL;
	}

	return 0;
}

void
dwmmc_pio_mode(struct dwmmc_softc *sc)
{
	/* Disable DMA. */
	HCLR4(sc, SDMMC_CTRL, SDMMC_CTRL_USE_INTERNAL_DMAC |
	    SDMMC_CTRL_DMA_ENABLE);

	/* Set FIFO thresholds. */
	HWRITE4(sc, SDMMC_FIFOTH, 2 << SDMMC_FIFOTH_MSIZE_SHIFT |
	    (sc->sc_fifo_depth / 2 - 1) << SDMMC_FIFOTH_RXWM_SHIFT |
	    (sc->sc_fifo_depth / 2) << SDMMC_FIFOTH_TXWM_SHIFT);

	sc->sc_dmamode = 0;
	sc->sc_blklen = 0;
}

void
dwmmc_dma_mode(struct dwmmc_softc *sc)
{
	int timeout;

	/* Reset DMA. */
	HSET4(sc, SDMMC_BMOD, SDMMC_BMOD_SWR);
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SDMMC_BMOD) & SDMMC_BMOD_SWR) == 0)
			break;
		delay(100);
	}
	if (timeout == 0)
		printf("%s: DMA reset failed\n", sc->sc_dev.dv_xname);

	/* Enable DMA. */
	HSET4(sc, SDMMC_CTRL, SDMMC_CTRL_USE_INTERNAL_DMAC |
	    SDMMC_CTRL_DMA_ENABLE);
	HSET4(sc, SDMMC_BMOD, SDMMC_BMOD_FB | SDMMC_BMOD_DE);

	sc->sc_dmamode = 1;
}

void
dwmmc_dma_setup32(struct dwmmc_softc *sc, struct sdmmc_command *cmd)
{
	struct dwmmc_desc32 *desc = (void *)sc->sc_desc;
	uint32_t flags;
	int seg;

	flags = DES0_OWN | DES0_FS | DES0_CH | DES0_DIC;
	for (seg = 0; seg < cmd->c_dmamap->dm_nsegs; seg++) {
		bus_addr_t addr = cmd->c_dmamap->dm_segs[seg].ds_addr;
		bus_size_t len = cmd->c_dmamap->dm_segs[seg].ds_len;

		if (seg == cmd->c_dmamap->dm_nsegs - 1) {
			flags |= DES0_LD;
			flags &= ~DES0_DIC;
		}

		KASSERT((desc[seg].des[0] & DES0_OWN) == 0);
		desc[seg].des[0] = flags;
		desc[seg].des[1] = DES1_BS1(len);
		desc[seg].des[2] = addr;
		flags &= ~DES0_FS;
	}
}

void
dwmmc_dma_setup64(struct dwmmc_softc *sc, struct sdmmc_command *cmd)
{
	struct dwmmc_desc64 *desc = (void *)sc->sc_desc;
	uint32_t flags;
	int seg;

	flags = DES0_OWN | DES0_FS | DES0_CH | DES0_DIC;
	for (seg = 0; seg < cmd->c_dmamap->dm_nsegs; seg++) {
		bus_addr_t addr = cmd->c_dmamap->dm_segs[seg].ds_addr;
		bus_size_t len = cmd->c_dmamap->dm_segs[seg].ds_len;

		if (seg == cmd->c_dmamap->dm_nsegs - 1) {
			flags |= DES0_LD;
			flags &= ~DES0_DIC;
		}

		KASSERT((desc[seg].des[0] & DES0_OWN) == 0);
		desc[seg].des[0] = flags;
		desc[seg].des[2] = DES2_BS1(len);
		desc[seg].des[4] = addr;
		desc[seg].des[5] = (uint64_t)addr >> 32;
		flags &= ~DES0_FS;
	}
}

void
dwmmc_dma_setup(struct dwmmc_softc *sc, struct sdmmc_command *cmd)
{
	if (sc->sc_dma64)
		dwmmc_dma_setup64(sc, cmd);
	else
		dwmmc_dma_setup32(sc, cmd);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_desc_map, 0, PAGE_SIZE,
	    BUS_DMASYNC_PREWRITE);

	sc->sc_idsts = 0;
}

void
dwmmc_dma_reset(struct dwmmc_softc *sc, struct sdmmc_command *cmd)
{
	int timeout;

	/* Reset DMA unit. */
	HSET4(sc, SDMMC_BMOD, SDMMC_BMOD_SWR);
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SDMMC_BMOD) &
		    SDMMC_BMOD_SWR) == 0)
			break;
		delay(100);
	}

	dwmmc_pio_mode(sc);

	/* Clear descriptors that were in use, */
	memset(sc->sc_desc, 0, PAGE_SIZE);
	dwmmc_init_descriptors(sc);
}

void
dwmmc_fifo_setup(struct dwmmc_softc *sc, int blklen)
{
	int blkdepth = blklen / sc->sc_fifo_width;
	int txwm = sc->sc_fifo_depth / 2;
	int rxwm, msize = 0;

	/*
	 * Bursting is only possible of the block size is a multiple of
	 * the FIFO width.
	 */
	if (blklen % sc->sc_fifo_width == 0)
		msize = 7;

	/* Magic to calculate the maximum burst size. */
	while (msize > 0) {
		if (blkdepth % (2 << msize) == 0 &&
		    (sc->sc_fifo_depth - txwm) % (2 << msize) == 0)
			break;
		msize--;
	}
	rxwm = (2 << msize) - 1;

	HWRITE4(sc, SDMMC_FIFOTH,
	    msize << SDMMC_FIFOTH_MSIZE_SHIFT |
	    rxwm << SDMMC_FIFOTH_RXWM_SHIFT |
	    txwm << SDMMC_FIFOTH_TXWM_SHIFT);

	sc->sc_blklen = blklen;
}

void
dwmmc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct dwmmc_softc *sc = sch;
	uint32_t cmdval = SDMMC_CMD_START_CMD | SDMMC_CMD_USE_HOLD_REG;
	uint32_t status;
	int error, timeout;
	int s;

#if 0
	printf("%s: cmd %d arg 0x%x flags 0x%x data %p datalen %d blklen %d\n",
	    sc->sc_dev.dv_xname, cmd->c_opcode, cmd->c_arg, cmd->c_flags,
	    cmd->c_data, cmd->c_datalen, cmd->c_blklen);
#endif

	s = splbio();

	for (timeout = 10000; timeout > 0; timeout--) {
		status = HREAD4(sc, SDMMC_STATUS);
		if ((status & SDMMC_STATUS_DATA_BUSY) == 0)
			break;
		delay(100);
	}
	if (timeout == 0) {
		printf("%s: timeout on data busy\n", sc->sc_dev.dv_xname);
		goto done;
	}

	if (cmd->c_opcode == MMC_STOP_TRANSMISSION)
		cmdval |= SDMMC_CMD_STOP_ABORT_CMD;
	else if (cmd->c_opcode != MMC_SEND_STATUS)
		cmdval |= SDMMC_CMD_WAIT_PRVDATA_COMPLETE;

	if (cmd->c_opcode == 0)
		cmdval |= SDMMC_CMD_SEND_INITIALIZATION;
	if (cmd->c_flags & SCF_RSP_PRESENT)
		cmdval |= SDMMC_CMD_RESPONSE_EXPECT;
	if (cmd->c_flags & SCF_RSP_136)
		cmdval |= SDMMC_CMD_RESPONSE_LENGTH;
	if (cmd->c_flags & SCF_RSP_CRC)
		cmdval |= SDMMC_CMD_CHECK_REPONSE_CRC;

	if (cmd->c_datalen > 0) {
		HWRITE4(sc, SDMMC_TMOUT, 0xffffffff);
		HWRITE4(sc, SDMMC_BYTCNT, cmd->c_datalen);
		HWRITE4(sc, SDMMC_BLKSIZ, cmd->c_blklen);

		if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
			/* Set card read threshold to the size of a block. */
			HWRITE4(sc, SDMMC_CARDTHRCTL, 
			    cmd->c_blklen << SDMMC_CARDTHRCTL_RDTHR_SHIFT |
			    SDMMC_CARDTHRCTL_RDTHREN);
		}

		cmdval |= SDMMC_CMD_DATA_EXPECTED;
		if (!ISSET(cmd->c_flags, SCF_CMD_READ))
			cmdval |= SDMMC_CMD_WR;
		if (cmd->c_datalen > cmd->c_blklen &&
		    cmd->c_opcode != SD_IO_RW_EXTENDED)
			cmdval |= SDMMC_CMD_SEND_AUTO_STOP;
	}

	if (cmd->c_datalen > 0 && !cmd->c_dmamap) {
		HSET4(sc, SDMMC_CTRL, SDMMC_CTRL_FIFO_RESET);
		for (timeout = 1000; timeout > 0; timeout--) {
			if ((HREAD4(sc, SDMMC_CTRL) &
			    SDMMC_CTRL_FIFO_RESET) == 0)
				break;
			delay(100);
		}
		if (timeout == 0)
			printf("%s: FIFO reset failed\n", sc->sc_dev.dv_xname);

		/* Disable DMA if we are switching back to PIO. */
		if (sc->sc_dmamode)
			dwmmc_pio_mode(sc);
	}

	if (cmd->c_datalen > 0 && cmd->c_dmamap) {
		dwmmc_dma_setup(sc, cmd);
		HWRITE4(sc, SDMMC_PLDMND, 1);

		/* Enable DMA if we did PIO before. */
		if (!sc->sc_dmamode)
			dwmmc_dma_mode(sc);

		/* Reconfigure FIFO thresholds if block size changed. */
		if (cmd->c_blklen != sc->sc_blklen)
			dwmmc_fifo_setup(sc, cmd->c_blklen);
	}

	HWRITE4(sc, SDMMC_RINTSTS, ~SDMMC_RINTSTS_SDIO);

	HWRITE4(sc, SDMMC_CMDARG, cmd->c_arg);
	HWRITE4(sc, SDMMC_CMD, cmdval | cmd->c_opcode);

	for (timeout = 1000; timeout > 0; timeout--) {
		status = HREAD4(sc, SDMMC_RINTSTS);
		if (status & SDMMC_RINTSTS_CD)
			break;
		delay(100);
	}
	if (timeout == 0 || status & SDMMC_RINTSTS_RTO) {
		cmd->c_error = ETIMEDOUT;
		dwmmc_dma_reset(sc, cmd);
		goto done;
	}

	if (cmd->c_flags & SCF_RSP_PRESENT) {
		if (cmd->c_flags & SCF_RSP_136) {
			cmd->c_resp[0] = HREAD4(sc, SDMMC_RESP0);
			cmd->c_resp[1] = HREAD4(sc, SDMMC_RESP1);
			cmd->c_resp[2] = HREAD4(sc, SDMMC_RESP2);
			cmd->c_resp[3] = HREAD4(sc, SDMMC_RESP3);
			if (cmd->c_flags & SCF_RSP_CRC) {
				cmd->c_resp[0] = (cmd->c_resp[0] >> 8) |
				    (cmd->c_resp[1] << 24);
				cmd->c_resp[1] = (cmd->c_resp[1] >> 8) |
				    (cmd->c_resp[2] << 24);
				cmd->c_resp[2] = (cmd->c_resp[2] >> 8) |
				    (cmd->c_resp[3] << 24);
				cmd->c_resp[3] = (cmd->c_resp[3] >> 8);
			}
		} else {
			cmd->c_resp[0] = HREAD4(sc, SDMMC_RESP0);
		}
	}

	if (cmd->c_datalen > 0 && !cmd->c_dmamap)
		dwmmc_transfer_data(sc, cmd);
	
	if (cmd->c_datalen > 0 && cmd->c_dmamap) {
		while (sc->sc_idsts == 0) {
			error = tsleep_nsec(&sc->sc_idsts, PWAIT, "idsts",
			    SEC_TO_NSEC(1));
			if (error) {
				cmd->c_error = error;
				dwmmc_dma_reset(sc, cmd);
				goto done;
			}
		}
		
		for (timeout = 10000; timeout > 0; timeout--) {
			status = HREAD4(sc, SDMMC_RINTSTS);
			if (status & SDMMC_RINTSTS_DTO)
				break;
			delay(100);
		}
		if (timeout == 0) {
			cmd->c_error = ETIMEDOUT;
			dwmmc_dma_reset(sc, cmd);
			goto done;
		}
	}

	if (cmdval & SDMMC_CMD_SEND_AUTO_STOP) {
		for (timeout = 10000; timeout > 0; timeout--) {
			status = HREAD4(sc, SDMMC_RINTSTS);
			if (status & SDMMC_RINTSTS_ACD)
				break;
			delay(10);
		}
		if (timeout == 0) {
			cmd->c_error = ETIMEDOUT;
			dwmmc_dma_reset(sc, cmd);
			goto done;
		}
	}

done:
	cmd->c_flags |= SCF_ITSDONE;
#if 0
	printf("%s: err %d rintsts 0x%x\n", sc->sc_dev.dv_xname, cmd->c_error,
	    HREAD4(sc, SDMMC_RINTSTS));
#endif
	splx(s);
}

void
dwmmc_transfer_data(struct dwmmc_softc *sc, struct sdmmc_command *cmd)
{
	int datalen = cmd->c_datalen;
	u_char *datap = cmd->c_data;
	uint32_t status;
	int count, timeout;
	int fifodr = SDMMC_RINTSTS_DTO | SDMMC_RINTSTS_HTO;

	if (ISSET(cmd->c_flags, SCF_CMD_READ))
		fifodr |= SDMMC_RINTSTS_RXDR;
	else
		fifodr |= SDMMC_RINTSTS_TXDR;

	while (datalen > 0) {
		status = HREAD4(sc, SDMMC_RINTSTS);
		if (status & SDMMC_RINTSTS_DATA_ERR) {
			cmd->c_error = EIO;
			return;
		}
		if (status & SDMMC_RINTSTS_DRTO) {
			cmd->c_error = ETIMEDOUT;
			return;
		}

		for (timeout = 10000; timeout > 0; timeout--) {
			status = HREAD4(sc, SDMMC_RINTSTS);
			if (status & fifodr)
				break;
			delay(100);
		}
		if (timeout == 0) {
			cmd->c_error = ETIMEDOUT;
			return;
		}

		count = SDMMC_STATUS_FIFO_COUNT(HREAD4(sc, SDMMC_STATUS));
		if (!ISSET(cmd->c_flags, SCF_CMD_READ))
		    count = sc->sc_fifo_depth - count;

		count = MIN(datalen, count * sc->sc_fifo_width);
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			sc->sc_read_data(sc, datap, count);
		else
			sc->sc_write_data(sc, datap, count);

		datap += count;
		datalen -= count;
	}

	for (timeout = 10000; timeout > 0; timeout--) {
		status = HREAD4(sc, SDMMC_RINTSTS);
		if (status & SDMMC_RINTSTS_DTO)
			break;
		delay(100);
	}
	if (timeout == 0)
		cmd->c_error = ETIMEDOUT;
}

void
dwmmc_read_data32(struct dwmmc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 3) {
		*(uint32_t *)datap = HREAD4(sc, SDMMC_FIFO_BASE);
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = HREAD4(sc, SDMMC_FIFO_BASE);
		do {
			*datap++ = rv & 0xff;
			rv = rv >> 8;
		} while (--datalen > 0);
	}
	HWRITE4(sc, SDMMC_RINTSTS, SDMMC_RINTSTS_RXDR);
}

void
dwmmc_write_data32(struct dwmmc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 3) {
		HWRITE4(sc, SDMMC_FIFO_BASE, *((uint32_t *)datap));
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = *datap++;
		if (datalen > 1)
			rv |= *datap++ << 8;
		if (datalen > 2)
			rv |= *datap++ << 16;
		HWRITE4(sc, SDMMC_FIFO_BASE, rv);
	}
	HWRITE4(sc, SDMMC_RINTSTS, SDMMC_RINTSTS_TXDR);
}

void
dwmmc_read_data64(struct dwmmc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 7) {
		*(uint32_t *)datap = HREAD4(sc, SDMMC_FIFO_BASE);
		datap += 4;
		datalen -= 4;
		*(uint32_t *)datap = HREAD4(sc, SDMMC_FIFO_BASE + 4);
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint64_t rv = HREAD4(sc, SDMMC_FIFO_BASE) |
		    ((uint64_t)HREAD4(sc, SDMMC_FIFO_BASE + 4) << 32);
		do {
			*datap++ = rv & 0xff;
			rv = rv >> 8;
		} while (--datalen > 0);
	}
	HWRITE4(sc, SDMMC_RINTSTS, SDMMC_RINTSTS_RXDR);
}

void
dwmmc_write_data64(struct dwmmc_softc *sc, u_char *datap, int datalen)
{
	while (datalen > 7) {
		HWRITE4(sc, SDMMC_FIFO_BASE, *((uint32_t *)datap));
		datap += 4;
		datalen -= 4;
		HWRITE4(sc, SDMMC_FIFO_BASE + 4, *((uint32_t *)datap));
		datap += 4;
		datalen -= 4;
	}
	if (datalen > 0) {
		uint32_t rv = *datap++;
		if (datalen > 1)
			rv |= *datap++ << 8;
		if (datalen > 2)
			rv |= *datap++ << 16;
		if (datalen > 3)
			rv |= *datap++ << 24;
		HWRITE4(sc, SDMMC_FIFO_BASE, rv);
		if (datalen > 4)
			rv = *datap++;
		if (datalen > 5)
			rv |= *datap++ << 8;
		if (datalen > 6)
			rv |= *datap++ << 16;
		HWRITE4(sc, SDMMC_FIFO_BASE + 4, rv);
	}
	HWRITE4(sc, SDMMC_RINTSTS, SDMMC_RINTSTS_TXDR);
}

void
dwmmc_pwrseq_pre(uint32_t phandle)
{
	uint32_t *gpios, *gpio;
	int node;
	int len;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "mmc-pwrseq-simple"))
		return;

	pinctrl_byname(node, "default");

	clock_enable(node, "ext_clock");

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
dwmmc_pwrseq_post(uint32_t phandle)
{
	uint32_t *gpios, *gpio;
	int post_delay;
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

	post_delay = OF_getpropint(node, "post-power-on-delay-ms", 0);
	if (post_delay)
		delay(post_delay * 1000);

	free(gpios, M_TEMP, len);
}

int
dwmmc_signal_voltage(sdmmc_chipset_handle_t sch, int signal_voltage)
{
	struct dwmmc_softc *sc = sch;
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
