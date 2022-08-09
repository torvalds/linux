// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/pci.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>
#include "cldma.h"
#include "registers.h"

/* Stream Registers */
#define AZX_CL_SD_BASE			0x80
#define AZX_SD_CTL_STRM_MASK		GENMASK(23, 20)
#define AZX_SD_CTL_STRM(s)		(((s)->stream_tag << 20) & AZX_SD_CTL_STRM_MASK)
#define AZX_SD_BDLPL_BDLPLBA_MASK	GENMASK(31, 7)
#define AZX_SD_BDLPL_BDLPLBA(lb)	((lb) & AZX_SD_BDLPL_BDLPLBA_MASK)

/* Software Position Based FIFO Capability Registers */
#define AZX_CL_SPBFCS			0x20
#define AZX_REG_CL_SPBFCTL		(AZX_CL_SPBFCS + 0x4)
#define AZX_REG_CL_SD_SPIB		(AZX_CL_SPBFCS + 0x8)

#define AVS_CL_OP_INTERVAL_US		3
#define AVS_CL_OP_TIMEOUT_US		300
#define AVS_CL_IOC_TIMEOUT_MS		300
#define AVS_CL_STREAM_INDEX		0

struct hda_cldma {
	struct device *dev;
	struct hdac_bus *bus;
	void __iomem *dsp_ba;

	unsigned int buffer_size;
	unsigned int num_periods;
	unsigned int stream_tag;
	void __iomem *sd_addr;

	struct snd_dma_buffer dmab_data;
	struct snd_dma_buffer dmab_bdl;
	struct delayed_work memcpy_work;
	struct completion completion;

	/* runtime */
	void *position;
	unsigned int remaining;
	unsigned int sd_status;
};

static void cldma_memcpy_work(struct work_struct *work);

struct hda_cldma code_loader = {
	.stream_tag	= AVS_CL_STREAM_INDEX + 1,
	.memcpy_work	= __DELAYED_WORK_INITIALIZER(code_loader.memcpy_work, cldma_memcpy_work, 0),
	.completion	= COMPLETION_INITIALIZER(code_loader.completion),
};

void hda_cldma_fill(struct hda_cldma *cl)
{
	unsigned int size, offset;

	if (cl->remaining > cl->buffer_size)
		size = cl->buffer_size;
	else
		size = cl->remaining;

	offset = snd_hdac_stream_readl(cl, CL_SD_SPIB);
	if (offset + size > cl->buffer_size) {
		unsigned int ss;

		ss = cl->buffer_size - offset;
		memcpy(cl->dmab_data.area + offset, cl->position, ss);
		offset = 0;
		size -= ss;
		cl->position += ss;
		cl->remaining -= ss;
	}

	memcpy(cl->dmab_data.area + offset, cl->position, size);
	cl->position += size;
	cl->remaining -= size;

	snd_hdac_stream_writel(cl, CL_SD_SPIB, offset + size);
}

static void cldma_memcpy_work(struct work_struct *work)
{
	struct hda_cldma *cl = container_of(work, struct hda_cldma, memcpy_work.work);
	int ret;

	ret = hda_cldma_start(cl);
	if (ret < 0) {
		dev_err(cl->dev, "cldma set RUN failed: %d\n", ret);
		return;
	}

	while (true) {
		ret = wait_for_completion_timeout(&cl->completion,
						  msecs_to_jiffies(AVS_CL_IOC_TIMEOUT_MS));
		if (!ret) {
			dev_err(cl->dev, "cldma IOC timeout\n");
			break;
		}

		if (!(cl->sd_status & SD_INT_COMPLETE)) {
			dev_err(cl->dev, "cldma transfer error, SD status: 0x%08x\n",
				cl->sd_status);
			break;
		}

		if (!cl->remaining)
			break;

		reinit_completion(&cl->completion);
		hda_cldma_fill(cl);
		/* enable CLDMA interrupt */
		snd_hdac_adsp_updatel(cl, AVS_ADSP_REG_ADSPIC, AVS_ADSP_ADSPIC_CLDMA,
				      AVS_ADSP_ADSPIC_CLDMA);
	}
}

void hda_cldma_transfer(struct hda_cldma *cl, unsigned long start_delay)
{
	if (!cl->remaining)
		return;

	reinit_completion(&cl->completion);
	/* fill buffer with the first chunk before scheduling run */
	hda_cldma_fill(cl);

	schedule_delayed_work(&cl->memcpy_work, start_delay);
}

int hda_cldma_start(struct hda_cldma *cl)
{
	unsigned int reg;

	/* enable interrupts */
	snd_hdac_adsp_updatel(cl, AVS_ADSP_REG_ADSPIC, AVS_ADSP_ADSPIC_CLDMA,
			      AVS_ADSP_ADSPIC_CLDMA);
	snd_hdac_stream_updateb(cl, SD_CTL, SD_INT_MASK | SD_CTL_DMA_START,
				SD_INT_MASK | SD_CTL_DMA_START);

	/* await DMA engine start */
	return snd_hdac_stream_readb_poll(cl, SD_CTL, reg, reg & SD_CTL_DMA_START,
					  AVS_CL_OP_INTERVAL_US, AVS_CL_OP_TIMEOUT_US);
}

int hda_cldma_stop(struct hda_cldma *cl)
{
	unsigned int reg;
	int ret;

	/* disable interrupts */
	snd_hdac_adsp_updatel(cl, AVS_ADSP_REG_ADSPIC, AVS_ADSP_ADSPIC_CLDMA, 0);
	snd_hdac_stream_updateb(cl, SD_CTL, SD_INT_MASK | SD_CTL_DMA_START, 0);

	/* await DMA engine stop */
	ret = snd_hdac_stream_readb_poll(cl, SD_CTL, reg, !(reg & SD_CTL_DMA_START),
					 AVS_CL_OP_INTERVAL_US, AVS_CL_OP_TIMEOUT_US);
	cancel_delayed_work_sync(&cl->memcpy_work);

	return ret;
}

int hda_cldma_reset(struct hda_cldma *cl)
{
	unsigned int reg;
	int ret;

	ret = hda_cldma_stop(cl);
	if (ret < 0) {
		dev_err(cl->dev, "cldma stop failed: %d\n", ret);
		return ret;
	}

	snd_hdac_stream_updateb(cl, SD_CTL, 1, 1);
	ret = snd_hdac_stream_readb_poll(cl, SD_CTL, reg, (reg & 1), AVS_CL_OP_INTERVAL_US,
					 AVS_CL_OP_TIMEOUT_US);
	if (ret < 0) {
		dev_err(cl->dev, "cldma set SRST failed: %d\n", ret);
		return ret;
	}

	snd_hdac_stream_updateb(cl, SD_CTL, 1, 0);
	ret = snd_hdac_stream_readb_poll(cl, SD_CTL, reg, !(reg & 1), AVS_CL_OP_INTERVAL_US,
					 AVS_CL_OP_TIMEOUT_US);
	if (ret < 0) {
		dev_err(cl->dev, "cldma unset SRST failed: %d\n", ret);
		return ret;
	}

	return 0;
}

void hda_cldma_set_data(struct hda_cldma *cl, void *data, unsigned int size)
{
	/* setup runtime */
	cl->position = data;
	cl->remaining = size;
}

static void cldma_setup_bdle(struct hda_cldma *cl, u32 bdle_size)
{
	struct snd_dma_buffer *dmab = &cl->dmab_data;
	__le32 *bdl = (__le32 *)cl->dmab_bdl.area;
	int remaining = cl->buffer_size;
	int offset = 0;

	cl->num_periods = 0;

	while (remaining > 0) {
		phys_addr_t addr;
		int chunk;

		addr = snd_sgbuf_get_addr(dmab, offset);
		bdl[0] = cpu_to_le32(lower_32_bits(addr));
		bdl[1] = cpu_to_le32(upper_32_bits(addr));
		chunk = snd_sgbuf_get_chunk_size(dmab, offset, bdle_size);
		bdl[2] = cpu_to_le32(chunk);

		remaining -= chunk;
		/* set IOC only for the last entry */
		bdl[3] = (remaining > 0) ? 0 : cpu_to_le32(0x01);

		bdl += 4;
		offset += chunk;
		cl->num_periods++;
	}
}

void hda_cldma_setup(struct hda_cldma *cl)
{
	dma_addr_t bdl_addr = cl->dmab_bdl.addr;

	cldma_setup_bdle(cl, cl->buffer_size / 2);

	snd_hdac_stream_writel(cl, SD_BDLPL, AZX_SD_BDLPL_BDLPLBA(lower_32_bits(bdl_addr)));
	snd_hdac_stream_writel(cl, SD_BDLPU, upper_32_bits(bdl_addr));

	snd_hdac_stream_writel(cl, SD_CBL, cl->buffer_size);
	snd_hdac_stream_writeb(cl, SD_LVI, cl->num_periods - 1);

	snd_hdac_stream_updatel(cl, SD_CTL, AZX_SD_CTL_STRM_MASK, AZX_SD_CTL_STRM(cl));
	/* enable spib */
	snd_hdac_stream_writel(cl, CL_SPBFCTL, 1);
}

static irqreturn_t cldma_irq_handler(int irq, void *dev_id)
{
	struct hda_cldma *cl = dev_id;
	u32 adspis;

	adspis = snd_hdac_adsp_readl(cl, AVS_ADSP_REG_ADSPIS);
	if (adspis == UINT_MAX)
		return IRQ_NONE;
	if (!(adspis & AVS_ADSP_ADSPIS_CLDMA))
		return IRQ_NONE;

	cl->sd_status = snd_hdac_stream_readb(cl, SD_STS);
	dev_warn(cl->dev, "%s sd_status: 0x%08x\n", __func__, cl->sd_status);

	/* disable CLDMA interrupt */
	snd_hdac_adsp_updatel(cl, AVS_ADSP_REG_ADSPIC, AVS_ADSP_ADSPIC_CLDMA, 0);

	complete(&cl->completion);

	return IRQ_HANDLED;
}

int hda_cldma_init(struct hda_cldma *cl, struct hdac_bus *bus, void __iomem *dsp_ba,
		   unsigned int buffer_size)
{
	struct pci_dev *pci = to_pci_dev(bus->dev);
	int ret;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, bus->dev, buffer_size, &cl->dmab_data);
	if (ret < 0)
		return ret;

	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, bus->dev, BDL_SIZE, &cl->dmab_bdl);
	if (ret < 0)
		goto alloc_err;

	cl->dev = bus->dev;
	cl->bus = bus;
	cl->dsp_ba = dsp_ba;
	cl->buffer_size = buffer_size;
	cl->sd_addr = dsp_ba + AZX_CL_SD_BASE;

	ret = pci_request_irq(pci, 0, cldma_irq_handler, NULL, cl, "CLDMA");
	if (ret < 0) {
		dev_err(cl->dev, "Failed to request CLDMA IRQ handler: %d\n", ret);
		goto req_err;
	}

	return 0;

req_err:
	snd_dma_free_pages(&cl->dmab_bdl);
alloc_err:
	snd_dma_free_pages(&cl->dmab_data);

	return ret;
}

void hda_cldma_free(struct hda_cldma *cl)
{
	struct pci_dev *pci = to_pci_dev(cl->dev);

	pci_free_irq(pci, 0, cl);
	snd_dma_free_pages(&cl->dmab_data);
	snd_dma_free_pages(&cl->dmab_bdl);
}
