/*
 * skl-sst-cldma.c - Code Loader DMA handler
 *
 * Copyright (C) 2015, Intel Corporation.
 * Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"

static void skl_cldma_int_enable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPIC,
				SKL_ADSPIC_CL_DMA, SKL_ADSPIC_CL_DMA);
}

void skl_cldma_int_disable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_ADSPIC, SKL_ADSPIC_CL_DMA, 0);
}

/* Code loader helper APIs */
static void skl_cldma_setup_bdle(struct sst_dsp *ctx,
		struct snd_dma_buffer *dmab_data,
		u32 **bdlp, int size, int with_ioc)
{
	u32 *bdl = *bdlp;

	ctx->cl_dev.frags = 0;
	while (size > 0) {
		phys_addr_t addr = virt_to_phys(dmab_data->area +
				(ctx->cl_dev.frags * ctx->cl_dev.bufsize));

		bdl[0] = cpu_to_le32(lower_32_bits(addr));
		bdl[1] = cpu_to_le32(upper_32_bits(addr));

		bdl[2] = cpu_to_le32(ctx->cl_dev.bufsize);

		size -= ctx->cl_dev.bufsize;
		bdl[3] = (size || !with_ioc) ? 0 : cpu_to_le32(0x01);

		bdl += 4;
		ctx->cl_dev.frags++;
	}
}

/*
 * Setup controller
 * Configure the registers to update the dma buffer address and
 * enable interrupts.
 * Note: Using the channel 1 for transfer
 */
static void skl_cldma_setup_controller(struct sst_dsp  *ctx,
		struct snd_dma_buffer *dmab_bdl, unsigned int max_size,
		u32 count)
{
	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_BDLPL,
			CL_SD_BDLPLBA(dmab_bdl->addr));
	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_BDLPU,
			CL_SD_BDLPUBA(dmab_bdl->addr));

	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_CBL, max_size);
	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_LVI, count - 1);
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_IOCE_MASK, CL_SD_CTL_IOCE(1));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_FEIE_MASK, CL_SD_CTL_FEIE(1));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_DEIE_MASK, CL_SD_CTL_DEIE(1));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_STRM_MASK, CL_SD_CTL_STRM(FW_CL_STREAM_NUMBER));
}

static void skl_cldma_setup_spb(struct sst_dsp  *ctx,
		unsigned int size, bool enable)
{
	if (enable)
		sst_dsp_shim_update_bits_unlocked(ctx,
				SKL_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
				CL_SPBFIFO_SPBFCCTL_SPIBE_MASK,
				CL_SPBFIFO_SPBFCCTL_SPIBE(1));

	sst_dsp_shim_write_unlocked(ctx, SKL_ADSP_REG_CL_SPBFIFO_SPIB, size);
}

static void skl_cldma_cleanup_spb(struct sst_dsp  *ctx)
{
	sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_CL_SPBFIFO_SPBFCCTL,
			CL_SPBFIFO_SPBFCCTL_SPIBE_MASK,
			CL_SPBFIFO_SPBFCCTL_SPIBE(0));

	sst_dsp_shim_write_unlocked(ctx, SKL_ADSP_REG_CL_SPBFIFO_SPIB, 0);
}

static void skl_cldma_trigger(struct sst_dsp  *ctx, bool enable)
{
	if (enable)
		sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_RUN_MASK, CL_SD_CTL_RUN(1));
	else
		sst_dsp_shim_update_bits_unlocked(ctx,
			SKL_ADSP_REG_CL_SD_CTL,
			CL_SD_CTL_RUN_MASK, CL_SD_CTL_RUN(0));
}

static void skl_cldma_cleanup(struct sst_dsp  *ctx)
{
	skl_cldma_cleanup_spb(ctx);

	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
				CL_SD_CTL_IOCE_MASK, CL_SD_CTL_IOCE(0));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
				CL_SD_CTL_FEIE_MASK, CL_SD_CTL_FEIE(0));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
				CL_SD_CTL_DEIE_MASK, CL_SD_CTL_DEIE(0));
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_CL_SD_CTL,
				CL_SD_CTL_STRM_MASK, CL_SD_CTL_STRM(0));

	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_BDLPL, CL_SD_BDLPLBA(0));
	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_BDLPU, 0);

	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_CBL, 0);
	sst_dsp_shim_write(ctx, SKL_ADSP_REG_CL_SD_LVI, 0);
}

static int skl_cldma_wait_interruptible(struct sst_dsp *ctx)
{
	int ret = 0;

	if (!wait_event_timeout(ctx->cl_dev.wait_queue,
				ctx->cl_dev.wait_condition,
				msecs_to_jiffies(SKL_WAIT_TIMEOUT))) {
		dev_err(ctx->dev, "%s: Wait timeout\n", __func__);
		ret = -EIO;
		goto cleanup;
	}

	dev_dbg(ctx->dev, "%s: Event wake\n", __func__);
	if (ctx->cl_dev.wake_status != SKL_CL_DMA_BUF_COMPLETE) {
		dev_err(ctx->dev, "%s: DMA Error\n", __func__);
		ret = -EIO;
	}

cleanup:
	ctx->cl_dev.wake_status = SKL_CL_DMA_STATUS_NONE;
	return ret;
}

static void skl_cldma_stop(struct sst_dsp *ctx)
{
	ctx->cl_dev.ops.cl_trigger(ctx, false);
}

static void skl_cldma_fill_buffer(struct sst_dsp *ctx, unsigned int size,
		const void *curr_pos, bool intr_enable, bool trigger)
{
	dev_dbg(ctx->dev, "Size: %x, intr_enable: %d\n", size, intr_enable);
	dev_dbg(ctx->dev, "buf_pos_index:%d, trigger:%d\n",
			ctx->cl_dev.dma_buffer_offset, trigger);
	dev_dbg(ctx->dev, "spib position: %d\n", ctx->cl_dev.curr_spib_pos);

	memcpy(ctx->cl_dev.dmab_data.area + ctx->cl_dev.dma_buffer_offset,
			curr_pos, size);

	if (ctx->cl_dev.curr_spib_pos == ctx->cl_dev.bufsize)
		ctx->cl_dev.dma_buffer_offset = 0;
	else
		ctx->cl_dev.dma_buffer_offset = ctx->cl_dev.curr_spib_pos;

	ctx->cl_dev.wait_condition = false;

	if (intr_enable)
		skl_cldma_int_enable(ctx);

	ctx->cl_dev.ops.cl_setup_spb(ctx, ctx->cl_dev.curr_spib_pos, trigger);
	if (trigger)
		ctx->cl_dev.ops.cl_trigger(ctx, true);
}
