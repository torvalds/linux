/*     $OpenBSD: bcm2835_sdhost.c,v 1.2 2022/04/06 18:59:28 naddy Exp $ */

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
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

/*-
 * Copyright (c) 2017 Jared McNeill <jmcneill@invisible.ca>
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/openfirm.h>

#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>

#include <dev/ic/bcm2835_dmac.h>

#define SDCMD		0x00
#define  SDCMD_NEW	(1 << 15)
#define  SDCMD_FAIL	(1 << 14)
#define  SDCMD_BUSY	(1 << 11)
#define  SDCMD_NORESP	(1 << 10)
#define  SDCMD_LONGRESP	(1 << 9)
#define  SDCMD_WRITE	(1 << 7)
#define  SDCMD_READ	(1 << 6)
#define SDARG		0x04
#define SDTOUT		0x08
#define  SDTOUT_DEFAULT 0xf00000
#define SDCDIV		0x0c
#define  SDCDIV_MASK	((1 << 11) - 1)
#define SDRSP0		0x10
#define SDRSP1		0x14
#define SDRSP2		0x18
#define SDRSP3		0x1c
#define SDHSTS		0x20
#define  SDHSTS_BUSY	(1 << 10)
#define  SDHSTS_BLOCK	(1 << 9)
#define  SDHSTS_SDIO	(1 << 8)
#define  SDHSTS_REW_TO	(1 << 7)
#define  SDHSTS_CMD_TO	(1 << 6)
#define  SDHSTS_CRC16_E	(1 << 5)
#define  SDHSTS_CRC7_E	(1 << 4)
#define  SDHSTS_FIFO_E	(1 << 3)
#define  SDHSTS_DATA	(1 << 0)
#define  SDHSTS_TO_MASK	(SDHSTS_REW_TO | SDHSTS_CMD_TO)
#define  SDHSTS_E_MASK	(SDHSTS_CRC16_E | SDHSTS_CRC7_E | SDHSTS_FIFO_E)
#define SDVDD		0x30
#define  SDVDD_POWER	(1 << 0)
#define SDEDM		0x34
#define  SDEDM_RD_FIFO_MASK	(0x1f << 14)
#define  SDEDM_RD_FIFO_SHIFT	14
#define  SDEDM_WR_FIFO_MASK	(0x1f << 9)
#define  SDEDM_WR_FIFO_SHIFT	9
#define  SDEDM_FIFO_LEVEL(x)	(((x) >> 4) & 0x1f)
#define SDHCFG		0x38
#define  SDHCFG_BUSY_EN	(1 << 10)
#define  SDHCFG_BLOCK_EN (1 << 8)
#define  SDHCFG_SDIO_EN	(1 << 5)
#define  SDHCFG_DATA_EN	(1 << 4)
#define  SDHCFG_SLOW	(1 << 3)
#define  SDHCFG_WIDE_EXT (1 << 2)
#define  SDHCFG_WIDE_INT (1 << 1)
#define  SDHCFG_REL_CMD	(1 << 0)
#define SDHBCT		0x3c
#define SDDATA		0x40
#define SDHBLC		0x50

#define SDHOST_FIFO_SIZE 16

struct bcmsdhost_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	bus_addr_t sc_addr;
	bus_size_t sc_size;

	void *sc_ih;

	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_dmamap;
	bus_dma_segment_t sc_segs[1];
	struct bcmdmac_conblk *sc_cblk;
	struct bcmdmac_channel *sc_dmac;

	struct mutex sc_intr_lock;
	uint32_t sc_intr_hsts;
	uint32_t sc_intr_cv;
	uint32_t sc_dma_cv;

	unsigned int sc_rate;

	uint32_t sc_dma_status;
	uint32_t sc_dma_error;

	struct device *sc_sdmmc;
};

int bcmsdhost_match(struct device *, void *, void *);
void bcmsdhost_attach(struct device *, struct device *, void *);

const struct cfattach bcmsdhost_ca = {
	sizeof(struct bcmsdhost_softc),
	bcmsdhost_match,
	bcmsdhost_attach
};

int bcmsdhost_host_reset(sdmmc_chipset_handle_t);
uint32_t bcmsdhost_host_ocr(sdmmc_chipset_handle_t);
int bcmsdhost_host_maxblklen(sdmmc_chipset_handle_t);
int bcmsdhost_card_detect(sdmmc_chipset_handle_t);
int bcmsdhost_bus_power(sdmmc_chipset_handle_t, uint32_t);
int bcmsdhost_bus_clock(sdmmc_chipset_handle_t, int, int);
int bcmsdhost_bus_width(sdmmc_chipset_handle_t, int);
void bcmsdhost_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);

struct sdmmc_chip_functions bcmsdhost_chip_functions = {
	.host_reset = bcmsdhost_host_reset,
	.host_ocr = bcmsdhost_host_ocr,
	.host_maxblklen = bcmsdhost_host_maxblklen,
	.card_detect = bcmsdhost_card_detect,
	.bus_power = bcmsdhost_bus_power,
	.bus_clock = bcmsdhost_bus_clock,
	.bus_width = bcmsdhost_bus_width,
	.exec_command = bcmsdhost_exec_command,
};

int bcmsdhost_wait_idle(struct bcmsdhost_softc *sc, int timeout);
int bcmsdhost_dma_wait(struct bcmsdhost_softc *, struct sdmmc_command *);
int bcmsdhost_dma_transfer(struct bcmsdhost_softc *, struct sdmmc_command *);
void bcmsdhost_dma_done(uint32_t, uint32_t, void *);
int bcmsdhost_pio_transfer(struct bcmsdhost_softc *, struct sdmmc_command *);
int bcmsdhost_intr(void *);

struct cfdriver bcmsdhost_cd = { NULL, "bcmsdhost", DV_DISK };

static inline void
bcmsdhost_write(struct bcmsdhost_softc *sc, bus_size_t offset, uint32_t value)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, offset, value);
}

static inline uint32_t
bcmsdhost_read(struct bcmsdhost_softc *sc, bus_size_t offset)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, offset);
}

int
bcmsdhost_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2835-sdhost");
}

void
bcmsdhost_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmsdhost_softc *sc = (struct bcmsdhost_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct sdmmcbus_attach_args saa;
	int rseg;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	mtx_init(&sc->sc_intr_lock, IPL_BIO);

	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;
	sc->sc_size = faa->fa_reg[0].size;
	sc->sc_addr = faa->fa_reg[0].addr;

	if (bus_space_map(sc->sc_iot, sc->sc_addr, sc->sc_size, 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	clock_enable_all(faa->fa_node);
	sc->sc_rate = clock_get_frequency_idx(faa->fa_node, 0);

	sc->sc_dmac = bcmdmac_alloc(BCMDMAC_TYPE_NORMAL, IPL_SDMMC,
	    bcmsdhost_dma_done, sc);
	if (sc->sc_dmac == NULL) {
		printf(": can't allocate DMA channel\n");
		goto clean_clocks;
	}

	sc->sc_dmat = faa->fa_dmat;
	if (bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE, PAGE_SIZE,
	    sc->sc_segs, 1, &rseg, BUS_DMA_WAITOK)) {
		printf(": can't allocate DMA memory\n");
		goto clean_dmac_channel;
	}

	if (bus_dmamem_map(sc->sc_dmat, sc->sc_segs, rseg, PAGE_SIZE,
			   (char **)&sc->sc_cblk, BUS_DMA_WAITOK)) {
		printf(": can't map DMA memory\n");
		goto clean_dmamap_free;
	}

	memset(sc->sc_cblk, 0, PAGE_SIZE);

	if (bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE, 0,
			      BUS_DMA_WAITOK, &sc->sc_dmamap)) {
		printf(": can't create DMA map\n");
		goto clean_dmamap_unmap;
	}

	if (bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap, sc->sc_cblk, PAGE_SIZE,
			    NULL, BUS_DMA_WAITOK | BUS_DMA_WRITE)) {
		printf(": can't load DMA map\n");
		goto clean_dmamap_destroy;
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_SDMMC, bcmsdhost_intr,
				       sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto clean_dmamap;
	}

	printf(": %u MHz base clock\n", sc->sc_rate / 1000000);

	bcmsdhost_write(sc, SDHCFG, SDHCFG_BUSY_EN);
	bcmsdhost_bus_clock(sc, 400, false);
	bcmsdhost_host_reset(sc);
	bcmsdhost_bus_width(sc, 1);

	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &bcmsdhost_chip_functions;
	saa.sch = sc;
	saa.dmat = sc->sc_dmat;
	saa.flags = SMF_SD_MODE | SMF_STOP_AFTER_MULTIPLE;
	saa.caps = SMC_CAPS_DMA | SMC_CAPS_4BIT_MODE |
	    SMC_CAPS_SD_HIGHSPEED | SMC_CAPS_MMC_HIGHSPEED;

	sc->sc_sdmmc = config_found(&sc->sc_dev, &saa, NULL);
	return;

clean_dmamap:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dmamap);
clean_dmamap_destroy:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dmamap);
clean_dmamap_unmap:
	bus_dmamem_unmap(sc->sc_dmat, (void *)sc->sc_cblk, PAGE_SIZE);
clean_dmamap_free:
	bus_dmamem_free(sc->sc_dmat, sc->sc_segs, 1);
clean_dmac_channel:
	bcmdmac_free(sc->sc_dmac);
clean_clocks:
	clock_disable_all(faa->fa_node);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
}

int
bcmsdhost_host_reset(sdmmc_chipset_handle_t sch)
{
	struct bcmsdhost_softc *sc = sch;
	uint32_t edm;

	bcmsdhost_write(sc, SDVDD, 0);
	bcmsdhost_write(sc, SDCMD, 0);
	bcmsdhost_write(sc, SDARG, 0);
	bcmsdhost_write(sc, SDTOUT, SDTOUT_DEFAULT);
	bcmsdhost_write(sc, SDCDIV, 0);
	bcmsdhost_write(sc, SDHSTS, bcmsdhost_read(sc, SDHSTS));
	bcmsdhost_write(sc, SDHCFG, 0);
	bcmsdhost_write(sc, SDHBCT, 0);
	bcmsdhost_write(sc, SDHBLC, 0);

	edm = bcmsdhost_read(sc, SDEDM);
	edm &= ~(SDEDM_RD_FIFO_MASK | SDEDM_WR_FIFO_MASK);
	edm |= (4 << SDEDM_RD_FIFO_SHIFT);
	edm |= (4 << SDEDM_WR_FIFO_SHIFT);
	bcmsdhost_write(sc, SDEDM, edm);

	delay(20000);
	bcmsdhost_write(sc, SDVDD, SDVDD_POWER);
	delay(20000);

	bcmsdhost_write(sc, SDHCFG, bcmsdhost_read(sc, SDHCFG));
	bcmsdhost_write(sc, SDCDIV, bcmsdhost_read(sc, SDCDIV));

	return 0;
}

uint32_t
bcmsdhost_host_ocr(sdmmc_chipset_handle_t sch)
{
	return MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V;
}

int
bcmsdhost_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	return 1024;
}

int
bcmsdhost_card_detect(sdmmc_chipset_handle_t sch)
{
	return 1;
}

int
bcmsdhost_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	return 0;
}

int
bcmsdhost_bus_clock(sdmmc_chipset_handle_t sch, int freq, int ddr)
{
	struct bcmsdhost_softc *sc = sch;
	unsigned int target_rate = freq * 1000;
	int div;

	if (freq == 0)
		div = SDCDIV_MASK;
	else {
		div = sc->sc_rate / target_rate;
		if (div < 2)
			div = 2;
		if ((sc->sc_rate / div) > target_rate)
			div++;
		div -= 2;
		if (div > SDCDIV_MASK)
			div = SDCDIV_MASK;
	}

	bcmsdhost_write(sc, SDCDIV, div);

	return 0;
}

int
bcmsdhost_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct bcmsdhost_softc *sc = sch;
	uint32_t hcfg;

	hcfg = bcmsdhost_read(sc, SDHCFG);
	if (width == 4)
		hcfg |= SDHCFG_WIDE_EXT;
	else
		hcfg &= ~SDHCFG_WIDE_EXT;
	hcfg |= (SDHCFG_WIDE_INT | SDHCFG_SLOW);
	bcmsdhost_write(sc, SDHCFG, hcfg);

	return 0;
}

void
bcmsdhost_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct bcmsdhost_softc *sc = sch;
	uint32_t cmdval, hcfg;
	unsigned int nblks;

	mtx_enter(&sc->sc_intr_lock);

	hcfg = bcmsdhost_read(sc, SDHCFG);
	bcmsdhost_write(sc, SDHCFG, hcfg | SDHCFG_BUSY_EN);

	sc->sc_intr_hsts = 0;

	cmd->c_error = bcmsdhost_wait_idle(sc, 5000);
	if (cmd->c_error != 0)
		goto done;

	cmdval = SDCMD_NEW;
	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		cmdval |= SDCMD_NORESP;
	if (ISSET(cmd->c_flags, SCF_RSP_136))
		cmdval |= SDCMD_LONGRESP;
	if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		cmdval |= SDCMD_BUSY;

	if (cmd->c_datalen > 0) {
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			cmdval |= SDCMD_READ;
		else
			cmdval |= SDCMD_WRITE;

		nblks = cmd->c_datalen / cmd->c_blklen;
		if (nblks == 0 || (cmd->c_datalen % cmd->c_blklen) != 0)
			++nblks;

		bcmsdhost_write(sc, SDHBCT, cmd->c_blklen);
		bcmsdhost_write(sc, SDHBLC, nblks);
	}

	if (cmd->c_datalen > 0 && cmd->c_dmamap) {
		cmd->c_resid = cmd->c_datalen;
		cmd->c_error = bcmsdhost_dma_transfer(sc, cmd);
		if (cmd->c_error != 0)
			goto done;
	}

	bcmsdhost_write(sc, SDARG, cmd->c_arg);
	bcmsdhost_write(sc, SDCMD, cmdval | cmd->c_opcode);

	if (cmd->c_datalen > 0 && cmd->c_dmamap) {
		cmd->c_error = bcmsdhost_dma_wait(sc, cmd);
		if (cmd->c_error != 0)
			goto done;
	} else if (cmd->c_datalen > 0) {
		cmd->c_resid = cmd->c_datalen;
		cmd->c_error = bcmsdhost_pio_transfer(sc, cmd);
		if (cmd->c_error != 0)
			goto done;
	}

	cmd->c_error = bcmsdhost_wait_idle(sc, 5000);
	if (cmd->c_error != 0)
		goto done;

	if (ISSET(bcmsdhost_read(sc, SDCMD), SDCMD_FAIL)) {
		cmd->c_error = EIO;
		goto done;
	}

	if (ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (ISSET(cmd->c_flags, SCF_RSP_136)) {
			cmd->c_resp[0] = bcmsdhost_read(sc, SDRSP0);
			cmd->c_resp[1] = bcmsdhost_read(sc, SDRSP1);
			cmd->c_resp[2] = bcmsdhost_read(sc, SDRSP2);
			cmd->c_resp[3] = bcmsdhost_read(sc, SDRSP3);
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
			cmd->c_resp[0] = bcmsdhost_read(sc, SDRSP0);
		}
	}

done:
	cmd->c_flags |= SCF_ITSDONE;
	bcmsdhost_write(sc, SDHCFG, hcfg);
	bcmsdhost_write(sc, SDHSTS, bcmsdhost_read(sc, SDHSTS));
	mtx_leave(&sc->sc_intr_lock);
}

int
bcmsdhost_wait_idle(struct bcmsdhost_softc *sc, int timeout)
{
	int retry = timeout * 1000;

	while (--retry > 0) {
		const uint32_t cmd = bcmsdhost_read(sc, SDCMD);
		if (!ISSET(cmd, SDCMD_NEW))
			return 0;
		delay(1);
	}

	return ETIMEDOUT;
}

int
bcmsdhost_dma_wait(struct bcmsdhost_softc *sc, struct sdmmc_command *cmd)
{
	int error = 0;

	while (sc->sc_dma_status == 0 && sc->sc_dma_error == 0) {
		error = msleep_nsec(&sc->sc_dma_cv, &sc->sc_intr_lock,
		    PPAUSE, "pause", SEC_TO_NSEC(5));
		if (error == EWOULDBLOCK) {
			printf("%s: transfer timeout!\n", DEVNAME(sc));
			bcmdmac_halt(sc->sc_dmac);
			error = ETIMEDOUT;
			goto error;
		}
	}

	if (ISSET(sc->sc_dma_status, DMAC_CS_END)) {
		cmd->c_resid = 0;
		error = 0;
	} else {
		error = EIO;
	}

error:
	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
			sc->sc_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);

	return error;
}

int
bcmsdhost_dma_transfer(struct bcmsdhost_softc *sc, struct sdmmc_command *cmd)
{
	const bus_addr_t ad_sddata = sc->sc_addr + SDDATA;
	size_t seg;
	int error;

	for (seg = 0; seg < cmd->c_dmamap->dm_nsegs; seg++) {
		if (sizeof(cmd->c_dmamap->dm_segs[seg].ds_addr) >
		    sizeof(sc->sc_cblk[seg].cb_source_ad)) {
			if (cmd->c_dmamap->dm_segs[seg].ds_addr > 0xffffffffU)
				return EFBIG;
		}
		sc->sc_cblk[seg].cb_ti = 13 * DMAC_TI_PERMAP_BASE;
		sc->sc_cblk[seg].cb_txfr_len =
		    cmd->c_dmamap->dm_segs[seg].ds_len;

		/*
		 * All transfers are assumed to be multiples of 32 bits
		 */
		KASSERTMSG((sc->sc_cblk[seg].cb_txfr_len & 0x3) == 0,
			   "seg %zu len %d", seg, sc->sc_cblk[seg].cb_txfr_len);
		/* Use 128-bit mode if transfer is a multiple of 16 bytes.  */
		if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_DEST_INC;
			if ((sc->sc_cblk[seg].cb_txfr_len & 0xf) == 0)
				sc->sc_cblk[seg].cb_ti |= DMAC_TI_DEST_WIDTH;
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_SRC_DREQ;
			sc->sc_cblk[seg].cb_source_ad = ad_sddata;
			sc->sc_cblk[seg].cb_dest_ad =
			    cmd->c_dmamap->dm_segs[seg].ds_addr;
		} else {
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_SRC_INC;
			if ((sc->sc_cblk[seg].cb_txfr_len & 0xf) == 0)
				sc->sc_cblk[seg].cb_ti |= DMAC_TI_SRC_WIDTH;
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_DEST_DREQ;
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_WAIT_RESP;
			sc->sc_cblk[seg].cb_source_ad =
			    cmd->c_dmamap->dm_segs[seg].ds_addr;
			sc->sc_cblk[seg].cb_dest_ad = ad_sddata;
		}
		sc->sc_cblk[seg].cb_stride = 0;
		if (seg == cmd->c_dmamap->dm_nsegs - 1) {
			sc->sc_cblk[seg].cb_ti |= DMAC_TI_INTEN;
			sc->sc_cblk[seg].cb_nextconbk = 0;
		} else {
			sc->sc_cblk[seg].cb_nextconbk =
			    sc->sc_dmamap->dm_segs[0].ds_addr +
			    sizeof(struct bcmdmac_conblk) * (seg + 1);
		}
		sc->sc_cblk[seg].cb_padding[0] = 0;
		sc->sc_cblk[seg].cb_padding[1] = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
			sc->sc_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	sc->sc_dma_status = 0;
	sc->sc_dma_error = 0;

	bcmdmac_set_conblk_addr(sc->sc_dmac, sc->sc_dmamap->dm_segs[0].ds_addr);
	error = bcmdmac_transfer(sc->sc_dmac);

	if (error)
		return error;

	return 0;
}

void
bcmsdhost_dma_done(uint32_t status, uint32_t error, void *arg)
{
	struct bcmsdhost_softc *sc = arg;

	mtx_enter(&sc->sc_intr_lock);

	sc->sc_dma_status = status;
	sc->sc_dma_error = error;
	wakeup(&sc->sc_dma_cv);

	mtx_leave(&sc->sc_intr_lock);
}

int
bcmsdhost_pio_transfer(struct bcmsdhost_softc *sc, struct sdmmc_command *cmd)
{
	uint32_t *datap = cmd->c_data;
	uint32_t edm, status;
	int count;

	if ((cmd->c_datalen % 4) != 0)
		return EINVAL;

	while (cmd->c_resid > 0) {
		edm = bcmsdhost_read(sc, SDEDM);
		count = SDEDM_FIFO_LEVEL(edm);
		if (!ISSET(cmd->c_flags, SCF_CMD_READ))
			count = SDHOST_FIFO_SIZE - count;

		if (count == 0) {
			status = bcmsdhost_read(sc, SDHSTS);
			if (status & SDHSTS_E_MASK)
				return EIO;
			if (status & SDHSTS_TO_MASK)
				return ETIMEDOUT;
		}

		while (count-- > 0 && cmd->c_resid > 0) {
			if (ISSET(cmd->c_flags, SCF_CMD_READ))
				*(datap++) = bcmsdhost_read(sc, SDDATA);
			else
				bcmsdhost_write(sc, SDDATA, *(datap++));

			cmd->c_resid -= 4;
			count--;
		}
	}

	status = bcmsdhost_read(sc, SDHSTS);
	if (status & SDHSTS_E_MASK)
		return EIO;
	if (status & SDHSTS_TO_MASK)
		return ETIMEDOUT;
	return 0;
}

int
bcmsdhost_intr(void *arg)
{
	struct bcmsdhost_softc *sc = arg;
	uint32_t hsts;

	mtx_enter(&sc->sc_intr_lock);

	hsts = bcmsdhost_read(sc, SDHSTS);
	if (hsts) {
		bcmsdhost_write(sc, SDHSTS, hsts);
		sc->sc_intr_hsts |= hsts;
		wakeup(&sc->sc_intr_cv);
	}

	mtx_leave(&sc->sc_intr_lock);

	return hsts ? 1 : 0;
}
