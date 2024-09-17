// SPDX-License-Identifier: GPL-2.0
//
// Freescale DMA ALSA SoC PCM driver
//
// Author: Timur Tabi <timur@freescale.com>
//
// Copyright 2007-2010 Freescale Semiconductor, Inc.
//
// This driver implements ASoC support for the Elo DMA controller, which is
// the DMA controller on Freescale 83xx, 85xx, and 86xx SOCs. In ALSA terms,
// the PCM driver is what handles the DMA buffer.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/io.h>

#include "fsl_dma.h"
#include "fsl_ssi.h"	/* For the offset of stx0 and srx0 */

#define DRV_NAME "fsl_dma"

/*
 * The formats that the DMA controller supports, which is anything
 * that is 8, 16, or 32 bits.
 */
#define FSLDMA_PCM_FORMATS (SNDRV_PCM_FMTBIT_S8 	| \
			    SNDRV_PCM_FMTBIT_U8 	| \
			    SNDRV_PCM_FMTBIT_S16_LE     | \
			    SNDRV_PCM_FMTBIT_S16_BE     | \
			    SNDRV_PCM_FMTBIT_U16_LE     | \
			    SNDRV_PCM_FMTBIT_U16_BE     | \
			    SNDRV_PCM_FMTBIT_S24_LE     | \
			    SNDRV_PCM_FMTBIT_S24_BE     | \
			    SNDRV_PCM_FMTBIT_U24_LE     | \
			    SNDRV_PCM_FMTBIT_U24_BE     | \
			    SNDRV_PCM_FMTBIT_S32_LE     | \
			    SNDRV_PCM_FMTBIT_S32_BE     | \
			    SNDRV_PCM_FMTBIT_U32_LE     | \
			    SNDRV_PCM_FMTBIT_U32_BE)
struct dma_object {
	struct snd_soc_component_driver dai;
	dma_addr_t ssi_stx_phys;
	dma_addr_t ssi_srx_phys;
	unsigned int ssi_fifo_depth;
	struct ccsr_dma_channel __iomem *channel;
	unsigned int irq;
	bool assigned;
};

/*
 * The number of DMA links to use.  Two is the bare minimum, but if you
 * have really small links you might need more.
 */
#define NUM_DMA_LINKS   2

/** fsl_dma_private: p-substream DMA data
 *
 * Each substream has a 1-to-1 association with a DMA channel.
 *
 * The link[] array is first because it needs to be aligned on a 32-byte
 * boundary, so putting it first will ensure alignment without padding the
 * structure.
 *
 * @link[]: array of link descriptors
 * @dma_channel: pointer to the DMA channel's registers
 * @irq: IRQ for this DMA channel
 * @substream: pointer to the substream object, needed by the ISR
 * @ssi_sxx_phys: bus address of the STX or SRX register to use
 * @ld_buf_phys: physical address of the LD buffer
 * @current_link: index into link[] of the link currently being processed
 * @dma_buf_phys: physical address of the DMA buffer
 * @dma_buf_next: physical address of the next period to process
 * @dma_buf_end: physical address of the byte after the end of the DMA
 * @buffer period_size: the size of a single period
 * @num_periods: the number of periods in the DMA buffer
 */
struct fsl_dma_private {
	struct fsl_dma_link_descriptor link[NUM_DMA_LINKS];
	struct ccsr_dma_channel __iomem *dma_channel;
	unsigned int irq;
	struct snd_pcm_substream *substream;
	dma_addr_t ssi_sxx_phys;
	unsigned int ssi_fifo_depth;
	dma_addr_t ld_buf_phys;
	unsigned int current_link;
	dma_addr_t dma_buf_phys;
	dma_addr_t dma_buf_next;
	dma_addr_t dma_buf_end;
	size_t period_size;
	unsigned int num_periods;
};

/**
 * fsl_dma_hardare: define characteristics of the PCM hardware.
 *
 * The PCM hardware is the Freescale DMA controller.  This structure defines
 * the capabilities of that hardware.
 *
 * Since the sampling rate and data format are not controlled by the DMA
 * controller, we specify no limits for those values.  The only exception is
 * period_bytes_min, which is set to a reasonably low value to prevent the
 * DMA controller from generating too many interrupts per second.
 *
 * Since each link descriptor has a 32-bit byte count field, we set
 * period_bytes_max to the largest 32-bit number.  We also have no maximum
 * number of periods.
 *
 * Note that we specify SNDRV_PCM_INFO_JOINT_DUPLEX here, but only because a
 * limitation in the SSI driver requires the sample rates for playback and
 * capture to be the same.
 */
static const struct snd_pcm_hardware fsl_dma_hardware = {

	.info   		= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_JOINT_DUPLEX |
				  SNDRV_PCM_INFO_PAUSE,
	.formats		= FSLDMA_PCM_FORMATS,
	.period_bytes_min       = 512,  	/* A reasonable limit */
	.period_bytes_max       = (u32) -1,
	.periods_min    	= NUM_DMA_LINKS,
	.periods_max    	= (unsigned int) -1,
	.buffer_bytes_max       = 128 * 1024,   /* A reasonable limit */
};

/**
 * fsl_dma_abort_stream: tell ALSA that the DMA transfer has aborted
 *
 * This function should be called by the ISR whenever the DMA controller
 * halts data transfer.
 */
static void fsl_dma_abort_stream(struct snd_pcm_substream *substream)
{
	snd_pcm_stop_xrun(substream);
}

/**
 * fsl_dma_update_pointers - update LD pointers to point to the next period
 *
 * As each period is completed, this function changes the link
 * descriptor pointers for that period to point to the next period.
 */
static void fsl_dma_update_pointers(struct fsl_dma_private *dma_private)
{
	struct fsl_dma_link_descriptor *link =
		&dma_private->link[dma_private->current_link];

	/* Update our link descriptors to point to the next period. On a 36-bit
	 * system, we also need to update the ESAD bits.  We also set (keep) the
	 * snoop bits.  See the comments in fsl_dma_hw_params() about snooping.
	 */
	if (dma_private->substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		link->source_addr = cpu_to_be32(dma_private->dma_buf_next);
#ifdef CONFIG_PHYS_64BIT
		link->source_attr = cpu_to_be32(CCSR_DMA_ATR_SNOOP |
			upper_32_bits(dma_private->dma_buf_next));
#endif
	} else {
		link->dest_addr = cpu_to_be32(dma_private->dma_buf_next);
#ifdef CONFIG_PHYS_64BIT
		link->dest_attr = cpu_to_be32(CCSR_DMA_ATR_SNOOP |
			upper_32_bits(dma_private->dma_buf_next));
#endif
	}

	/* Update our variables for next time */
	dma_private->dma_buf_next += dma_private->period_size;

	if (dma_private->dma_buf_next >= dma_private->dma_buf_end)
		dma_private->dma_buf_next = dma_private->dma_buf_phys;

	if (++dma_private->current_link >= NUM_DMA_LINKS)
		dma_private->current_link = 0;
}

/**
 * fsl_dma_isr: interrupt handler for the DMA controller
 *
 * @irq: IRQ of the DMA channel
 * @dev_id: pointer to the dma_private structure for this DMA channel
 */
static irqreturn_t fsl_dma_isr(int irq, void *dev_id)
{
	struct fsl_dma_private *dma_private = dev_id;
	struct snd_pcm_substream *substream = dma_private->substream;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct device *dev = rtd->dev;
	struct ccsr_dma_channel __iomem *dma_channel = dma_private->dma_channel;
	irqreturn_t ret = IRQ_NONE;
	u32 sr, sr2 = 0;

	/* We got an interrupt, so read the status register to see what we
	   were interrupted for.
	 */
	sr = in_be32(&dma_channel->sr);

	if (sr & CCSR_DMA_SR_TE) {
		dev_err(dev, "dma transmit error\n");
		fsl_dma_abort_stream(substream);
		sr2 |= CCSR_DMA_SR_TE;
		ret = IRQ_HANDLED;
	}

	if (sr & CCSR_DMA_SR_CH)
		ret = IRQ_HANDLED;

	if (sr & CCSR_DMA_SR_PE) {
		dev_err(dev, "dma programming error\n");
		fsl_dma_abort_stream(substream);
		sr2 |= CCSR_DMA_SR_PE;
		ret = IRQ_HANDLED;
	}

	if (sr & CCSR_DMA_SR_EOLNI) {
		sr2 |= CCSR_DMA_SR_EOLNI;
		ret = IRQ_HANDLED;
	}

	if (sr & CCSR_DMA_SR_CB)
		ret = IRQ_HANDLED;

	if (sr & CCSR_DMA_SR_EOSI) {
		/* Tell ALSA we completed a period. */
		snd_pcm_period_elapsed(substream);

		/*
		 * Update our link descriptors to point to the next period. We
		 * only need to do this if the number of periods is not equal to
		 * the number of links.
		 */
		if (dma_private->num_periods != NUM_DMA_LINKS)
			fsl_dma_update_pointers(dma_private);

		sr2 |= CCSR_DMA_SR_EOSI;
		ret = IRQ_HANDLED;
	}

	if (sr & CCSR_DMA_SR_EOLSI) {
		sr2 |= CCSR_DMA_SR_EOLSI;
		ret = IRQ_HANDLED;
	}

	/* Clear the bits that we set */
	if (sr2)
		out_be32(&dma_channel->sr, sr2);

	return ret;
}

/**
 * fsl_dma_new: initialize this PCM driver.
 *
 * This function is called when the codec driver calls snd_soc_new_pcms(),
 * once for each .dai_link in the machine driver's snd_soc_card
 * structure.
 *
 * snd_dma_alloc_pages() is just a front-end to dma_alloc_coherent(), which
 * (currently) always allocates the DMA buffer in lowmem, even if GFP_HIGHMEM
 * is specified. Therefore, any DMA buffers we allocate will always be in low
 * memory, but we support for 36-bit physical addresses anyway.
 *
 * Regardless of where the memory is actually allocated, since the device can
 * technically DMA to any 36-bit address, we do need to set the DMA mask to 36.
 */
static int fsl_dma_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	return snd_pcm_set_fixed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
					    card->dev,
					    fsl_dma_hardware.buffer_bytes_max);
}

/**
 * fsl_dma_open: open a new substream.
 *
 * Each substream has its own DMA buffer.
 *
 * ALSA divides the DMA buffer into N periods.  We create NUM_DMA_LINKS link
 * descriptors that ping-pong from one period to the next.  For example, if
 * there are six periods and two link descriptors, this is how they look
 * before playback starts:
 *
 *      	   The last link descriptor
 *   ____________  points back to the first
 *  |   	 |
 *  V   	 |
 *  ___    ___   |
 * |   |->|   |->|
 * |___|  |___|
 *   |      |
 *   |      |
 *   V      V
 *  _________________________________________
 * |      |      |      |      |      |      |  The DMA buffer is
 * |      |      |      |      |      |      |    divided into 6 parts
 * |______|______|______|______|______|______|
 *
 * and here's how they look after the first period is finished playing:
 *
 *   ____________
 *  |   	 |
 *  V   	 |
 *  ___    ___   |
 * |   |->|   |->|
 * |___|  |___|
 *   |      |
 *   |______________
 *          |       |
 *          V       V
 *  _________________________________________
 * |      |      |      |      |      |      |
 * |      |      |      |      |      |      |
 * |______|______|______|______|______|______|
 *
 * The first link descriptor now points to the third period.  The DMA
 * controller is currently playing the second period.  When it finishes, it
 * will jump back to the first descriptor and play the third period.
 *
 * There are four reasons we do this:
 *
 * 1. The only way to get the DMA controller to automatically restart the
 *    transfer when it gets to the end of the buffer is to use chaining
 *    mode.  Basic direct mode doesn't offer that feature.
 * 2. We need to receive an interrupt at the end of every period.  The DMA
 *    controller can generate an interrupt at the end of every link transfer
 *    (aka segment).  Making each period into a DMA segment will give us the
 *    interrupts we need.
 * 3. By creating only two link descriptors, regardless of the number of
 *    periods, we do not need to reallocate the link descriptors if the
 *    number of periods changes.
 * 4. All of the audio data is still stored in a single, contiguous DMA
 *    buffer, which is what ALSA expects.  We're just dividing it into
 *    contiguous parts, and creating a link descriptor for each one.
 */
static int fsl_dma_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = component->dev;
	struct dma_object *dma =
		container_of(component->driver, struct dma_object, dai);
	struct fsl_dma_private *dma_private;
	struct ccsr_dma_channel __iomem *dma_channel;
	dma_addr_t ld_buf_phys;
	u64 temp_link;  	/* Pointer to next link descriptor */
	u32 mr;
	int ret = 0;
	unsigned int i;

	/*
	 * Reject any DMA buffer whose size is not a multiple of the period
	 * size.  We need to make sure that the DMA buffer can be evenly divided
	 * into periods.
	 */
	ret = snd_pcm_hw_constraint_integer(runtime,
		SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(dev, "invalid buffer size\n");
		return ret;
	}

	if (dma->assigned) {
		dev_err(dev, "dma channel already assigned\n");
		return -EBUSY;
	}

	dma_private = dma_alloc_coherent(dev, sizeof(struct fsl_dma_private),
					 &ld_buf_phys, GFP_KERNEL);
	if (!dma_private) {
		dev_err(dev, "can't allocate dma private data\n");
		return -ENOMEM;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_private->ssi_sxx_phys = dma->ssi_stx_phys;
	else
		dma_private->ssi_sxx_phys = dma->ssi_srx_phys;

	dma_private->ssi_fifo_depth = dma->ssi_fifo_depth;
	dma_private->dma_channel = dma->channel;
	dma_private->irq = dma->irq;
	dma_private->substream = substream;
	dma_private->ld_buf_phys = ld_buf_phys;
	dma_private->dma_buf_phys = substream->dma_buffer.addr;

	ret = request_irq(dma_private->irq, fsl_dma_isr, 0, "fsldma-audio",
			  dma_private);
	if (ret) {
		dev_err(dev, "can't register ISR for IRQ %u (ret=%i)\n",
			dma_private->irq, ret);
		dma_free_coherent(dev, sizeof(struct fsl_dma_private),
			dma_private, dma_private->ld_buf_phys);
		return ret;
	}

	dma->assigned = true;

	snd_soc_set_runtime_hwparams(substream, &fsl_dma_hardware);
	runtime->private_data = dma_private;

	/* Program the fixed DMA controller parameters */

	dma_channel = dma_private->dma_channel;

	temp_link = dma_private->ld_buf_phys +
		sizeof(struct fsl_dma_link_descriptor);

	for (i = 0; i < NUM_DMA_LINKS; i++) {
		dma_private->link[i].next = cpu_to_be64(temp_link);

		temp_link += sizeof(struct fsl_dma_link_descriptor);
	}
	/* The last link descriptor points to the first */
	dma_private->link[i - 1].next = cpu_to_be64(dma_private->ld_buf_phys);

	/* Tell the DMA controller where the first link descriptor is */
	out_be32(&dma_channel->clndar,
		CCSR_DMA_CLNDAR_ADDR(dma_private->ld_buf_phys));
	out_be32(&dma_channel->eclndar,
		CCSR_DMA_ECLNDAR_ADDR(dma_private->ld_buf_phys));

	/* The manual says the BCR must be clear before enabling EMP */
	out_be32(&dma_channel->bcr, 0);

	/*
	 * Program the mode register for interrupts, external master control,
	 * and source/destination hold.  Also clear the Channel Abort bit.
	 */
	mr = in_be32(&dma_channel->mr) &
		~(CCSR_DMA_MR_CA | CCSR_DMA_MR_DAHE | CCSR_DMA_MR_SAHE);

	/*
	 * We want External Master Start and External Master Pause enabled,
	 * because the SSI is controlling the DMA controller.  We want the DMA
	 * controller to be set up in advance, and then we signal only the SSI
	 * to start transferring.
	 *
	 * We want End-Of-Segment Interrupts enabled, because this will generate
	 * an interrupt at the end of each segment (each link descriptor
	 * represents one segment).  Each DMA segment is the same thing as an
	 * ALSA period, so this is how we get an interrupt at the end of every
	 * period.
	 *
	 * We want Error Interrupt enabled, so that we can get an error if
	 * the DMA controller is mis-programmed somehow.
	 */
	mr |= CCSR_DMA_MR_EOSIE | CCSR_DMA_MR_EIE | CCSR_DMA_MR_EMP_EN |
		CCSR_DMA_MR_EMS_EN;

	/* For playback, we want the destination address to be held.  For
	   capture, set the source address to be held. */
	mr |= (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		CCSR_DMA_MR_DAHE : CCSR_DMA_MR_SAHE;

	out_be32(&dma_channel->mr, mr);

	return 0;
}

/**
 * fsl_dma_hw_params: continue initializing the DMA links
 *
 * This function obtains hardware parameters about the opened stream and
 * programs the DMA controller accordingly.
 *
 * One drawback of big-endian is that when copying integers of different
 * sizes to a fixed-sized register, the address to which the integer must be
 * copied is dependent on the size of the integer.
 *
 * For example, if P is the address of a 32-bit register, and X is a 32-bit
 * integer, then X should be copied to address P.  However, if X is a 16-bit
 * integer, then it should be copied to P+2.  If X is an 8-bit register,
 * then it should be copied to P+3.
 *
 * So for playback of 8-bit samples, the DMA controller must transfer single
 * bytes from the DMA buffer to the last byte of the STX0 register, i.e.
 * offset by 3 bytes. For 16-bit samples, the offset is two bytes.
 *
 * For 24-bit samples, the offset is 1 byte.  However, the DMA controller
 * does not support 3-byte copies (the DAHTS register supports only 1, 2, 4,
 * and 8 bytes at a time).  So we do not support packed 24-bit samples.
 * 24-bit data must be padded to 32 bits.
 */
static int fsl_dma_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_dma_private *dma_private = runtime->private_data;
	struct device *dev = component->dev;

	/* Number of bits per sample */
	unsigned int sample_bits =
		snd_pcm_format_physical_width(params_format(hw_params));

	/* Number of bytes per frame */
	unsigned int sample_bytes = sample_bits / 8;

	/* Bus address of SSI STX register */
	dma_addr_t ssi_sxx_phys = dma_private->ssi_sxx_phys;

	/* Size of the DMA buffer, in bytes */
	size_t buffer_size = params_buffer_bytes(hw_params);

	/* Number of bytes per period */
	size_t period_size = params_period_bytes(hw_params);

	/* Pointer to next period */
	dma_addr_t temp_addr = substream->dma_buffer.addr;

	/* Pointer to DMA controller */
	struct ccsr_dma_channel __iomem *dma_channel = dma_private->dma_channel;

	u32 mr; /* DMA Mode Register */

	unsigned int i;

	/* Initialize our DMA tracking variables */
	dma_private->period_size = period_size;
	dma_private->num_periods = params_periods(hw_params);
	dma_private->dma_buf_end = dma_private->dma_buf_phys + buffer_size;
	dma_private->dma_buf_next = dma_private->dma_buf_phys +
		(NUM_DMA_LINKS * period_size);

	if (dma_private->dma_buf_next >= dma_private->dma_buf_end)
		/* This happens if the number of periods == NUM_DMA_LINKS */
		dma_private->dma_buf_next = dma_private->dma_buf_phys;

	mr = in_be32(&dma_channel->mr) & ~(CCSR_DMA_MR_BWC_MASK |
		  CCSR_DMA_MR_SAHTS_MASK | CCSR_DMA_MR_DAHTS_MASK);

	/* Due to a quirk of the SSI's STX register, the target address
	 * for the DMA operations depends on the sample size.  So we calculate
	 * that offset here.  While we're at it, also tell the DMA controller
	 * how much data to transfer per sample.
	 */
	switch (sample_bits) {
	case 8:
		mr |= CCSR_DMA_MR_DAHTS_1 | CCSR_DMA_MR_SAHTS_1;
		ssi_sxx_phys += 3;
		break;
	case 16:
		mr |= CCSR_DMA_MR_DAHTS_2 | CCSR_DMA_MR_SAHTS_2;
		ssi_sxx_phys += 2;
		break;
	case 32:
		mr |= CCSR_DMA_MR_DAHTS_4 | CCSR_DMA_MR_SAHTS_4;
		break;
	default:
		/* We should never get here */
		dev_err(dev, "unsupported sample size %u\n", sample_bits);
		return -EINVAL;
	}

	/*
	 * BWC determines how many bytes are sent/received before the DMA
	 * controller checks the SSI to see if it needs to stop. BWC should
	 * always be a multiple of the frame size, so that we always transmit
	 * whole frames.  Each frame occupies two slots in the FIFO.  The
	 * parameter for CCSR_DMA_MR_BWC() is rounded down the next power of two
	 * (MR[BWC] can only represent even powers of two).
	 *
	 * To simplify the process, we set BWC to the largest value that is
	 * less than or equal to the FIFO watermark.  For playback, this ensures
	 * that we transfer the maximum amount without overrunning the FIFO.
	 * For capture, this ensures that we transfer the maximum amount without
	 * underrunning the FIFO.
	 *
	 * f = SSI FIFO depth
	 * w = SSI watermark value (which equals f - 2)
	 * b = DMA bandwidth count (in bytes)
	 * s = sample size (in bytes, which equals frame_size * 2)
	 *
	 * For playback, we never transmit more than the transmit FIFO
	 * watermark, otherwise we might write more data than the FIFO can hold.
	 * The watermark is equal to the FIFO depth minus two.
	 *
	 * For capture, two equations must hold:
	 *	w > f - (b / s)
	 *	w >= b / s
	 *
	 * So, b > 2 * s, but b must also be <= s * w.  To simplify, we set
	 * b = s * w, which is equal to
	 *      (dma_private->ssi_fifo_depth - 2) * sample_bytes.
	 */
	mr |= CCSR_DMA_MR_BWC((dma_private->ssi_fifo_depth - 2) * sample_bytes);

	out_be32(&dma_channel->mr, mr);

	for (i = 0; i < NUM_DMA_LINKS; i++) {
		struct fsl_dma_link_descriptor *link = &dma_private->link[i];

		link->count = cpu_to_be32(period_size);

		/* The snoop bit tells the DMA controller whether it should tell
		 * the ECM to snoop during a read or write to an address. For
		 * audio, we use DMA to transfer data between memory and an I/O
		 * device (the SSI's STX0 or SRX0 register). Snooping is only
		 * needed if there is a cache, so we need to snoop memory
		 * addresses only.  For playback, that means we snoop the source
		 * but not the destination.  For capture, we snoop the
		 * destination but not the source.
		 *
		 * Note that failing to snoop properly is unlikely to cause
		 * cache incoherency if the period size is larger than the
		 * size of L1 cache.  This is because filling in one period will
		 * flush out the data for the previous period.  So if you
		 * increased period_bytes_min to a large enough size, you might
		 * get more performance by not snooping, and you'll still be
		 * okay.  You'll need to update fsl_dma_update_pointers() also.
		 */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			link->source_addr = cpu_to_be32(temp_addr);
			link->source_attr = cpu_to_be32(CCSR_DMA_ATR_SNOOP |
				upper_32_bits(temp_addr));

			link->dest_addr = cpu_to_be32(ssi_sxx_phys);
			link->dest_attr = cpu_to_be32(CCSR_DMA_ATR_NOSNOOP |
				upper_32_bits(ssi_sxx_phys));
		} else {
			link->source_addr = cpu_to_be32(ssi_sxx_phys);
			link->source_attr = cpu_to_be32(CCSR_DMA_ATR_NOSNOOP |
				upper_32_bits(ssi_sxx_phys));

			link->dest_addr = cpu_to_be32(temp_addr);
			link->dest_attr = cpu_to_be32(CCSR_DMA_ATR_SNOOP |
				upper_32_bits(temp_addr));
		}

		temp_addr += period_size;
	}

	return 0;
}

/**
 * fsl_dma_pointer: determine the current position of the DMA transfer
 *
 * This function is called by ALSA when ALSA wants to know where in the
 * stream buffer the hardware currently is.
 *
 * For playback, the SAR register contains the physical address of the most
 * recent DMA transfer.  For capture, the value is in the DAR register.
 *
 * The base address of the buffer is stored in the source_addr field of the
 * first link descriptor.
 */
static snd_pcm_uframes_t fsl_dma_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_dma_private *dma_private = runtime->private_data;
	struct device *dev = component->dev;
	struct ccsr_dma_channel __iomem *dma_channel = dma_private->dma_channel;
	dma_addr_t position;
	snd_pcm_uframes_t frames;

	/* Obtain the current DMA pointer, but don't read the ESAD bits if we
	 * only have 32-bit DMA addresses.  This function is typically called
	 * in interrupt context, so we need to optimize it.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		position = in_be32(&dma_channel->sar);
#ifdef CONFIG_PHYS_64BIT
		position |= (u64)(in_be32(&dma_channel->satr) &
				  CCSR_DMA_ATR_ESAD_MASK) << 32;
#endif
	} else {
		position = in_be32(&dma_channel->dar);
#ifdef CONFIG_PHYS_64BIT
		position |= (u64)(in_be32(&dma_channel->datr) &
				  CCSR_DMA_ATR_ESAD_MASK) << 32;
#endif
	}

	/*
	 * When capture is started, the SSI immediately starts to fill its FIFO.
	 * This means that the DMA controller is not started until the FIFO is
	 * full.  However, ALSA calls this function before that happens, when
	 * MR.DAR is still zero.  In this case, just return zero to indicate
	 * that nothing has been received yet.
	 */
	if (!position)
		return 0;

	if ((position < dma_private->dma_buf_phys) ||
	    (position > dma_private->dma_buf_end)) {
		dev_err(dev, "dma pointer is out of range, halting stream\n");
		return SNDRV_PCM_POS_XRUN;
	}

	frames = bytes_to_frames(runtime, position - dma_private->dma_buf_phys);

	/*
	 * If the current address is just past the end of the buffer, wrap it
	 * around.
	 */
	if (frames == runtime->buffer_size)
		frames = 0;

	return frames;
}

/**
 * fsl_dma_hw_free: release resources allocated in fsl_dma_hw_params()
 *
 * Release the resources allocated in fsl_dma_hw_params() and de-program the
 * registers.
 *
 * This function can be called multiple times.
 */
static int fsl_dma_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_dma_private *dma_private = runtime->private_data;

	if (dma_private) {
		struct ccsr_dma_channel __iomem *dma_channel;

		dma_channel = dma_private->dma_channel;

		/* Stop the DMA */
		out_be32(&dma_channel->mr, CCSR_DMA_MR_CA);
		out_be32(&dma_channel->mr, 0);

		/* Reset all the other registers */
		out_be32(&dma_channel->sr, -1);
		out_be32(&dma_channel->clndar, 0);
		out_be32(&dma_channel->eclndar, 0);
		out_be32(&dma_channel->satr, 0);
		out_be32(&dma_channel->sar, 0);
		out_be32(&dma_channel->datr, 0);
		out_be32(&dma_channel->dar, 0);
		out_be32(&dma_channel->bcr, 0);
		out_be32(&dma_channel->nlndar, 0);
		out_be32(&dma_channel->enlndar, 0);
	}

	return 0;
}

/**
 * fsl_dma_close: close the stream.
 */
static int fsl_dma_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_dma_private *dma_private = runtime->private_data;
	struct device *dev = component->dev;
	struct dma_object *dma =
		container_of(component->driver, struct dma_object, dai);

	if (dma_private) {
		if (dma_private->irq)
			free_irq(dma_private->irq, dma_private);

		/* Deallocate the fsl_dma_private structure */
		dma_free_coherent(dev, sizeof(struct fsl_dma_private),
				  dma_private, dma_private->ld_buf_phys);
		substream->runtime->private_data = NULL;
	}

	dma->assigned = false;

	return 0;
}

/**
 * find_ssi_node -- returns the SSI node that points to its DMA channel node
 *
 * Although this DMA driver attempts to operate independently of the other
 * devices, it still needs to determine some information about the SSI device
 * that it's working with.  Unfortunately, the device tree does not contain
 * a pointer from the DMA channel node to the SSI node -- the pointer goes the
 * other way.  So we need to scan the device tree for SSI nodes until we find
 * the one that points to the given DMA channel node.  It's ugly, but at least
 * it's contained in this one function.
 */
static struct device_node *find_ssi_node(struct device_node *dma_channel_np)
{
	struct device_node *ssi_np, *np;

	for_each_compatible_node(ssi_np, NULL, "fsl,mpc8610-ssi") {
		/* Check each DMA phandle to see if it points to us.  We
		 * assume that device_node pointers are a valid comparison.
		 */
		np = of_parse_phandle(ssi_np, "fsl,playback-dma", 0);
		of_node_put(np);
		if (np == dma_channel_np)
			return ssi_np;

		np = of_parse_phandle(ssi_np, "fsl,capture-dma", 0);
		of_node_put(np);
		if (np == dma_channel_np)
			return ssi_np;
	}

	return NULL;
}

static int fsl_soc_dma_probe(struct platform_device *pdev)
{
	struct dma_object *dma;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ssi_np;
	struct resource res;
	const uint32_t *iprop;
	int ret;

	/* Find the SSI node that points to us. */
	ssi_np = find_ssi_node(np);
	if (!ssi_np) {
		dev_err(&pdev->dev, "cannot find parent SSI node\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(ssi_np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "could not determine resources for %pOF\n",
			ssi_np);
		of_node_put(ssi_np);
		return ret;
	}

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma) {
		of_node_put(ssi_np);
		return -ENOMEM;
	}

	dma->dai.name = DRV_NAME;
	dma->dai.open = fsl_dma_open;
	dma->dai.close = fsl_dma_close;
	dma->dai.hw_params = fsl_dma_hw_params;
	dma->dai.hw_free = fsl_dma_hw_free;
	dma->dai.pointer = fsl_dma_pointer;
	dma->dai.pcm_construct = fsl_dma_new;

	/* Store the SSI-specific information that we need */
	dma->ssi_stx_phys = res.start + REG_SSI_STX0;
	dma->ssi_srx_phys = res.start + REG_SSI_SRX0;

	iprop = of_get_property(ssi_np, "fsl,fifo-depth", NULL);
	if (iprop)
		dma->ssi_fifo_depth = be32_to_cpup(iprop);
	else
                /* Older 8610 DTs didn't have the fifo-depth property */
		dma->ssi_fifo_depth = 8;

	of_node_put(ssi_np);

	ret = devm_snd_soc_register_component(&pdev->dev, &dma->dai, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "could not register platform\n");
		kfree(dma);
		return ret;
	}

	dma->channel = of_iomap(np, 0);
	dma->irq = irq_of_parse_and_map(np, 0);

	dev_set_drvdata(&pdev->dev, dma);

	return 0;
}

static void fsl_soc_dma_remove(struct platform_device *pdev)
{
	struct dma_object *dma = dev_get_drvdata(&pdev->dev);

	iounmap(dma->channel);
	irq_dispose_mapping(dma->irq);
	kfree(dma);
}

static const struct of_device_id fsl_soc_dma_ids[] = {
	{ .compatible = "fsl,ssi-dma-channel", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_soc_dma_ids);

static struct platform_driver fsl_soc_dma_driver = {
	.driver = {
		.name = "fsl-pcm-audio",
		.of_match_table = fsl_soc_dma_ids,
	},
	.probe = fsl_soc_dma_probe,
	.remove = fsl_soc_dma_remove,
};

module_platform_driver(fsl_soc_dma_driver);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale Elo DMA ASoC PCM Driver");
MODULE_LICENSE("GPL v2");
