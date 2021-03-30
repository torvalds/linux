// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/devcoredump.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/pxa2xx_ssp.h>
#include "core.h"
#include "messages.h"
#include "registers.h"

static bool catpt_dma_filter(struct dma_chan *chan, void *param)
{
	return param == chan->device->dev;
}

/*
 * Either engine 0 or 1 can be used for image loading.
 * Align with Windows driver equivalent and stick to engine 1.
 */
#define CATPT_DMA_DEVID		1
#define CATPT_DMA_DSP_ADDR_MASK	GENMASK(31, 20)

struct dma_chan *catpt_dma_request_config_chan(struct catpt_dev *cdev)
{
	struct dma_slave_config config;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	chan = dma_request_channel(mask, catpt_dma_filter, cdev->dev);
	if (!chan) {
		dev_err(cdev->dev, "request channel failed\n");
		return ERR_PTR(-ENODEV);
	}

	memset(&config, 0, sizeof(config));
	config.direction = DMA_MEM_TO_DEV;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.src_maxburst = 16;
	config.dst_maxburst = 16;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(cdev->dev, "slave config failed: %d\n", ret);
		dma_release_channel(chan);
		return ERR_PTR(ret);
	}

	return chan;
}

static int catpt_dma_memcpy(struct catpt_dev *cdev, struct dma_chan *chan,
			    dma_addr_t dst_addr, dma_addr_t src_addr,
			    size_t size)
{
	struct dma_async_tx_descriptor *desc;
	enum dma_status status;

	desc = dmaengine_prep_dma_memcpy(chan, dst_addr, src_addr, size,
					 DMA_CTRL_ACK);
	if (!desc) {
		dev_err(cdev->dev, "prep dma memcpy failed\n");
		return -EIO;
	}

	/* enable demand mode for dma channel */
	catpt_updatel_shim(cdev, HMDC,
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id),
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id));
	dmaengine_submit(desc);
	status = dma_wait_for_async_tx(desc);
	/* regardless of status, disable access to HOST memory in demand mode */
	catpt_updatel_shim(cdev, HMDC,
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id), 0);

	return (status == DMA_COMPLETE) ? 0 : -EPROTO;
}

int catpt_dma_memcpy_todsp(struct catpt_dev *cdev, struct dma_chan *chan,
			   dma_addr_t dst_addr, dma_addr_t src_addr,
			   size_t size)
{
	return catpt_dma_memcpy(cdev, chan, dst_addr | CATPT_DMA_DSP_ADDR_MASK,
				src_addr, size);
}

int catpt_dma_memcpy_fromdsp(struct catpt_dev *cdev, struct dma_chan *chan,
			     dma_addr_t dst_addr, dma_addr_t src_addr,
			     size_t size)
{
	return catpt_dma_memcpy(cdev, chan, dst_addr,
				src_addr | CATPT_DMA_DSP_ADDR_MASK, size);
}

int catpt_dmac_probe(struct catpt_dev *cdev)
{
	struct dw_dma_chip *dmac;
	int ret;

	dmac = devm_kzalloc(cdev->dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac)
		return -ENOMEM;

	dmac->regs = cdev->lpe_ba + cdev->spec->host_dma_offset[CATPT_DMA_DEVID];
	dmac->dev = cdev->dev;
	dmac->irq = cdev->irq;

	ret = dma_coerce_mask_and_coherent(cdev->dev, DMA_BIT_MASK(31));
	if (ret)
		return ret;
	/*
	 * Caller is responsible for putting device in D0 to allow
	 * for I/O and memory access before probing DW.
	 */
	ret = dw_dma_probe(dmac);
	if (ret)
		return ret;

	cdev->dmac = dmac;
	return 0;
}

void catpt_dmac_remove(struct catpt_dev *cdev)
{
	/*
	 * As do_dma_remove() juggles with pm_runtime_get_xxx() and
	 * pm_runtime_put_xxx() while both ADSP and DW 'devices' are part of
	 * the same module, caller makes sure pm_runtime_disable() is invoked
	 * before removing DW to prevent postmortem resume and suspend.
	 */
	dw_dma_remove(cdev->dmac);
}

static void catpt_dsp_set_srampge(struct catpt_dev *cdev, struct resource *sram,
				  unsigned long mask, unsigned long new)
{
	unsigned long old;
	u32 off = sram->start;
	u32 b = __ffs(mask);

	old = catpt_readl_pci(cdev, VDRTCTL0) & mask;
	dev_dbg(cdev->dev, "SRAMPGE [0x%08lx] 0x%08lx -> 0x%08lx",
		mask, old, new);

	if (old == new)
		return;

	catpt_updatel_pci(cdev, VDRTCTL0, mask, new);
	/* wait for SRAM power gating to propagate */
	udelay(60);

	/*
	 * Dummy read as the very first access after block enable
	 * to prevent byte loss in future operations.
	 */
	for_each_clear_bit_from(b, &new, fls_long(mask)) {
		u8 buf[4];

		/* newly enabled: new bit=0 while old bit=1 */
		if (test_bit(b, &old)) {
			dev_dbg(cdev->dev, "sanitize block %ld: off 0x%08x\n",
				b - __ffs(mask), off);
			memcpy_fromio(buf, cdev->lpe_ba + off, sizeof(buf));
		}
		off += CATPT_MEMBLOCK_SIZE;
	}
}

void catpt_dsp_update_srampge(struct catpt_dev *cdev, struct resource *sram,
			      unsigned long mask)
{
	struct resource *res;
	unsigned long new = 0;

	/* flag all busy blocks */
	for (res = sram->child; res; res = res->sibling) {
		u32 h, l;

		h = (res->end - sram->start) / CATPT_MEMBLOCK_SIZE;
		l = (res->start - sram->start) / CATPT_MEMBLOCK_SIZE;
		new |= GENMASK(h, l);
	}

	/* offset value given mask's start and invert it as ON=b0 */
	new = ~(new << __ffs(mask)) & mask;

	/* disable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

	catpt_dsp_set_srampge(cdev, sram, mask, new);

	/* enable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
			  CATPT_VDRTCTL2_DCLCGE);
}

int catpt_dsp_stall(struct catpt_dev *cdev, bool stall)
{
	u32 reg, val;

	val = stall ? CATPT_CS_STALL : 0;
	catpt_updatel_shim(cdev, CS1, CATPT_CS_STALL, val);

	return catpt_readl_poll_shim(cdev, CS1,
				     reg, (reg & CATPT_CS_STALL) == val,
				     500, 10000);
}

static int catpt_dsp_reset(struct catpt_dev *cdev, bool reset)
{
	u32 reg, val;

	val = reset ? CATPT_CS_RST : 0;
	catpt_updatel_shim(cdev, CS1, CATPT_CS_RST, val);

	return catpt_readl_poll_shim(cdev, CS1,
				     reg, (reg & CATPT_CS_RST) == val,
				     500, 10000);
}

void lpt_dsp_pll_shutdown(struct catpt_dev *cdev, bool enable)
{
	u32 val;

	val = enable ? LPT_VDRTCTL0_APLLSE : 0;
	catpt_updatel_pci(cdev, VDRTCTL0, LPT_VDRTCTL0_APLLSE, val);
}

void wpt_dsp_pll_shutdown(struct catpt_dev *cdev, bool enable)
{
	u32 val;

	val = enable ? WPT_VDRTCTL2_APLLSE : 0;
	catpt_updatel_pci(cdev, VDRTCTL2, WPT_VDRTCTL2_APLLSE, val);
}

static int catpt_dsp_select_lpclock(struct catpt_dev *cdev, bool lp, bool waiti)
{
	u32 mask, reg, val;
	int ret;

	mutex_lock(&cdev->clk_mutex);

	val = lp ? CATPT_CS_LPCS : 0;
	reg = catpt_readl_shim(cdev, CS1) & CATPT_CS_LPCS;
	dev_dbg(cdev->dev, "LPCS [0x%08lx] 0x%08x -> 0x%08x",
		CATPT_CS_LPCS, reg, val);

	if (reg == val) {
		mutex_unlock(&cdev->clk_mutex);
		return 0;
	}

	if (waiti) {
		/* wait for DSP to signal WAIT state */
		ret = catpt_readl_poll_shim(cdev, ISD,
					    reg, (reg & CATPT_ISD_DCPWM),
					    500, 10000);
		if (ret) {
			dev_warn(cdev->dev, "await WAITI timeout\n");
			/* no signal - only high clock selection allowed */
			if (lp) {
				mutex_unlock(&cdev->clk_mutex);
				return 0;
			}
		}
	}

	ret = catpt_readl_poll_shim(cdev, CLKCTL,
				    reg, !(reg & CATPT_CLKCTL_CFCIP),
				    500, 10000);
	if (ret)
		dev_warn(cdev->dev, "clock change still in progress\n");

	/* default to DSP core & audio fabric high clock */
	val |= CATPT_CS_DCS_HIGH;
	mask = CATPT_CS_LPCS | CATPT_CS_DCS;
	catpt_updatel_shim(cdev, CS1, mask, val);

	ret = catpt_readl_poll_shim(cdev, CLKCTL,
				    reg, !(reg & CATPT_CLKCTL_CFCIP),
				    500, 10000);
	if (ret)
		dev_warn(cdev->dev, "clock change still in progress\n");

	/* update PLL accordingly */
	cdev->spec->pll_shutdown(cdev, lp);

	mutex_unlock(&cdev->clk_mutex);
	return 0;
}

int catpt_dsp_update_lpclock(struct catpt_dev *cdev)
{
	struct catpt_stream_runtime *stream;

	list_for_each_entry(stream, &cdev->stream_list, node)
		if (stream->prepared)
			return catpt_dsp_select_lpclock(cdev, false, true);

	return catpt_dsp_select_lpclock(cdev, true, true);
}

/* bring registers to their defaults as HW won't reset itself */
static void catpt_dsp_set_regs_defaults(struct catpt_dev *cdev)
{
	int i;

	catpt_writel_shim(cdev, CS1, CATPT_CS_DEFAULT);
	catpt_writel_shim(cdev, ISC, CATPT_ISC_DEFAULT);
	catpt_writel_shim(cdev, ISD, CATPT_ISD_DEFAULT);
	catpt_writel_shim(cdev, IMC, CATPT_IMC_DEFAULT);
	catpt_writel_shim(cdev, IMD, CATPT_IMD_DEFAULT);
	catpt_writel_shim(cdev, IPCC, CATPT_IPCC_DEFAULT);
	catpt_writel_shim(cdev, IPCD, CATPT_IPCD_DEFAULT);
	catpt_writel_shim(cdev, CLKCTL, CATPT_CLKCTL_DEFAULT);
	catpt_writel_shim(cdev, CS2, CATPT_CS2_DEFAULT);
	catpt_writel_shim(cdev, LTRC, CATPT_LTRC_DEFAULT);
	catpt_writel_shim(cdev, HMDC, CATPT_HMDC_DEFAULT);

	for (i = 0; i < CATPT_SSP_COUNT; i++) {
		catpt_writel_ssp(cdev, i, SSCR0, CATPT_SSC0_DEFAULT);
		catpt_writel_ssp(cdev, i, SSCR1, CATPT_SSC1_DEFAULT);
		catpt_writel_ssp(cdev, i, SSSR, CATPT_SSS_DEFAULT);
		catpt_writel_ssp(cdev, i, SSITR, CATPT_SSIT_DEFAULT);
		catpt_writel_ssp(cdev, i, SSDR, CATPT_SSD_DEFAULT);
		catpt_writel_ssp(cdev, i, SSTO, CATPT_SSTO_DEFAULT);
		catpt_writel_ssp(cdev, i, SSPSP, CATPT_SSPSP_DEFAULT);
		catpt_writel_ssp(cdev, i, SSTSA, CATPT_SSTSA_DEFAULT);
		catpt_writel_ssp(cdev, i, SSRSA, CATPT_SSRSA_DEFAULT);
		catpt_writel_ssp(cdev, i, SSTSS, CATPT_SSTSS_DEFAULT);
		catpt_writel_ssp(cdev, i, SSCR2, CATPT_SSCR2_DEFAULT);
		catpt_writel_ssp(cdev, i, SSPSP2, CATPT_SSPSP2_DEFAULT);
	}
}

int catpt_dsp_power_down(struct catpt_dev *cdev)
{
	u32 mask, val;

	/* disable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

	catpt_dsp_reset(cdev, true);
	/* set 24Mhz clock for both SSPs */
	catpt_updatel_shim(cdev, CS1, CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1),
			   CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1));
	catpt_dsp_select_lpclock(cdev, true, false);
	/* disable MCLK */
	catpt_updatel_shim(cdev, CLKCTL, CATPT_CLKCTL_SMOS, 0);

	catpt_dsp_set_regs_defaults(cdev);

	/* switch clock gating */
	mask = CATPT_VDRTCTL2_CGEALL & (~CATPT_VDRTCTL2_DCLCGE);
	val = mask & (~CATPT_VDRTCTL2_DTCGE);
	catpt_updatel_pci(cdev, VDRTCTL2, mask, val);
	/* enable DTCGE separatelly */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DTCGE,
			  CATPT_VDRTCTL2_DTCGE);

	/* SRAM power gating all */
	catpt_dsp_set_srampge(cdev, &cdev->dram, cdev->spec->dram_mask,
			      cdev->spec->dram_mask);
	catpt_dsp_set_srampge(cdev, &cdev->iram, cdev->spec->iram_mask,
			      cdev->spec->iram_mask);
	mask = cdev->spec->d3srampgd_bit | cdev->spec->d3pgd_bit;
	catpt_updatel_pci(cdev, VDRTCTL0, mask, cdev->spec->d3pgd_bit);

	catpt_updatel_pci(cdev, PMCS, PCI_PM_CTRL_STATE_MASK, PCI_D3hot);
	/* give hw time to drop off */
	udelay(50);

	/* enable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
			  CATPT_VDRTCTL2_DCLCGE);
	udelay(50);

	return 0;
}

int catpt_dsp_power_up(struct catpt_dev *cdev)
{
	u32 mask, val;

	/* disable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

	/* switch clock gating */
	mask = CATPT_VDRTCTL2_CGEALL & (~CATPT_VDRTCTL2_DCLCGE);
	val = mask & (~CATPT_VDRTCTL2_DTCGE);
	catpt_updatel_pci(cdev, VDRTCTL2, mask, val);

	catpt_updatel_pci(cdev, PMCS, PCI_PM_CTRL_STATE_MASK, PCI_D0);

	/* SRAM power gating none */
	mask = cdev->spec->d3srampgd_bit | cdev->spec->d3pgd_bit;
	catpt_updatel_pci(cdev, VDRTCTL0, mask, mask);
	catpt_dsp_set_srampge(cdev, &cdev->dram, cdev->spec->dram_mask, 0);
	catpt_dsp_set_srampge(cdev, &cdev->iram, cdev->spec->iram_mask, 0);

	catpt_dsp_set_regs_defaults(cdev);

	/* restore MCLK */
	catpt_updatel_shim(cdev, CLKCTL, CATPT_CLKCTL_SMOS, CATPT_CLKCTL_SMOS);
	catpt_dsp_select_lpclock(cdev, false, false);
	/* set 24Mhz clock for both SSPs */
	catpt_updatel_shim(cdev, CS1, CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1),
			   CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1));
	catpt_dsp_reset(cdev, false);

	/* enable core clock gating */
	catpt_updatel_pci(cdev, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
			  CATPT_VDRTCTL2_DCLCGE);

	/* generate int deassert msg to fix inversed int logic */
	catpt_updatel_shim(cdev, IMC, CATPT_IMC_IPCDB | CATPT_IMC_IPCCD, 0);

	return 0;
}

#define CATPT_DUMP_MAGIC		0xcd42
#define CATPT_DUMP_SECTION_ID_FILE	0x00
#define CATPT_DUMP_SECTION_ID_IRAM	0x01
#define CATPT_DUMP_SECTION_ID_DRAM	0x02
#define CATPT_DUMP_SECTION_ID_REGS	0x03
#define CATPT_DUMP_HASH_SIZE		20

struct catpt_dump_section_hdr {
	u16 magic;
	u8 core_id;
	u8 section_id;
	u32 size;
};

int catpt_coredump(struct catpt_dev *cdev)
{
	struct catpt_dump_section_hdr *hdr;
	size_t dump_size, regs_size;
	u8 *dump, *pos;
	const char *eof;
	char *info;
	int i;

	regs_size = CATPT_SHIM_REGS_SIZE;
	regs_size += CATPT_DMA_COUNT * CATPT_DMA_REGS_SIZE;
	regs_size += CATPT_SSP_COUNT * CATPT_SSP_REGS_SIZE;
	dump_size = resource_size(&cdev->dram);
	dump_size += resource_size(&cdev->iram);
	dump_size += regs_size;
	/* account for header of each section and hash chunk */
	dump_size += 4 * sizeof(*hdr) + CATPT_DUMP_HASH_SIZE;

	dump = vzalloc(dump_size);
	if (!dump)
		return -ENOMEM;

	pos = dump;

	hdr = (struct catpt_dump_section_hdr *)pos;
	hdr->magic = CATPT_DUMP_MAGIC;
	hdr->core_id = cdev->spec->core_id;
	hdr->section_id = CATPT_DUMP_SECTION_ID_FILE;
	hdr->size = dump_size - sizeof(*hdr);
	pos += sizeof(*hdr);

	info = cdev->ipc.config.fw_info;
	eof = info + FW_INFO_SIZE_MAX;
	/* navigate to fifth info segment (fw hash) */
	for (i = 0; i < 4 && info < eof; i++, info++) {
		/* info segments are separated by space each */
		info = strnchr(info, eof - info, ' ');
		if (!info)
			break;
	}

	if (i == 4 && info)
		memcpy(pos, info, min_t(u32, eof - info, CATPT_DUMP_HASH_SIZE));
	pos += CATPT_DUMP_HASH_SIZE;

	hdr = (struct catpt_dump_section_hdr *)pos;
	hdr->magic = CATPT_DUMP_MAGIC;
	hdr->core_id = cdev->spec->core_id;
	hdr->section_id = CATPT_DUMP_SECTION_ID_IRAM;
	hdr->size = resource_size(&cdev->iram);
	pos += sizeof(*hdr);

	memcpy_fromio(pos, cdev->lpe_ba + cdev->iram.start, hdr->size);
	pos += hdr->size;

	hdr = (struct catpt_dump_section_hdr *)pos;
	hdr->magic = CATPT_DUMP_MAGIC;
	hdr->core_id = cdev->spec->core_id;
	hdr->section_id = CATPT_DUMP_SECTION_ID_DRAM;
	hdr->size = resource_size(&cdev->dram);
	pos += sizeof(*hdr);

	memcpy_fromio(pos, cdev->lpe_ba + cdev->dram.start, hdr->size);
	pos += hdr->size;

	hdr = (struct catpt_dump_section_hdr *)pos;
	hdr->magic = CATPT_DUMP_MAGIC;
	hdr->core_id = cdev->spec->core_id;
	hdr->section_id = CATPT_DUMP_SECTION_ID_REGS;
	hdr->size = regs_size;
	pos += sizeof(*hdr);

	memcpy_fromio(pos, catpt_shim_addr(cdev), CATPT_SHIM_REGS_SIZE);
	pos += CATPT_SHIM_REGS_SIZE;

	for (i = 0; i < CATPT_SSP_COUNT; i++) {
		memcpy_fromio(pos, catpt_ssp_addr(cdev, i),
			      CATPT_SSP_REGS_SIZE);
		pos += CATPT_SSP_REGS_SIZE;
	}
	for (i = 0; i < CATPT_DMA_COUNT; i++) {
		memcpy_fromio(pos, catpt_dma_addr(cdev, i),
			      CATPT_DMA_REGS_SIZE);
		pos += CATPT_DMA_REGS_SIZE;
	}

	dev_coredumpv(cdev->dev, dump, dump_size, GFP_KERNEL);

	return 0;
}
