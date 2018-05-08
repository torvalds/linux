/*
 * AMD ALSA SoC PCM Driver for ACP 2.x
 *
 * Copyright 2014-2015 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/pm_runtime.h>

#include <sound/soc.h>
#include <drm/amd_asic_type.h>
#include "acp.h"

#define DRV_NAME "acp_audio_dma"

#define PLAYBACK_MIN_NUM_PERIODS    2
#define PLAYBACK_MAX_NUM_PERIODS    2
#define PLAYBACK_MAX_PERIOD_SIZE    16384
#define PLAYBACK_MIN_PERIOD_SIZE    1024
#define CAPTURE_MIN_NUM_PERIODS     2
#define CAPTURE_MAX_NUM_PERIODS     2
#define CAPTURE_MAX_PERIOD_SIZE     16384
#define CAPTURE_MIN_PERIOD_SIZE     1024

#define MAX_BUFFER (PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define MIN_BUFFER MAX_BUFFER

#define ST_PLAYBACK_MAX_PERIOD_SIZE 8192
#define ST_CAPTURE_MAX_PERIOD_SIZE  ST_PLAYBACK_MAX_PERIOD_SIZE
#define ST_MAX_BUFFER (ST_PLAYBACK_MAX_PERIOD_SIZE * PLAYBACK_MAX_NUM_PERIODS)
#define ST_MIN_BUFFER ST_MAX_BUFFER

#define DRV_NAME "acp_audio_dma"

static const struct snd_pcm_hardware acp_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
	    SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.buffer_bytes_max = CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp_st_pcm_hardware_playback = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_8000_96000,
	.rate_min = 8000,
	.rate_max = 96000,
	.buffer_bytes_max = ST_MAX_BUFFER,
	.period_bytes_min = PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max = ST_PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min = PLAYBACK_MIN_NUM_PERIODS,
	.periods_max = PLAYBACK_MAX_NUM_PERIODS,
};

static const struct snd_pcm_hardware acp_st_pcm_hardware_capture = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 1,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.buffer_bytes_max = ST_MAX_BUFFER,
	.period_bytes_min = CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max = ST_CAPTURE_MAX_PERIOD_SIZE,
	.periods_min = CAPTURE_MIN_NUM_PERIODS,
	.periods_max = CAPTURE_MAX_NUM_PERIODS,
};

static u32 acp_reg_read(void __iomem *acp_mmio, u32 reg)
{
	return readl(acp_mmio + (reg * 4));
}

static void acp_reg_write(u32 val, void __iomem *acp_mmio, u32 reg)
{
	writel(val, acp_mmio + (reg * 4));
}

/*
 * Configure a given dma channel parameters - enable/disable,
 * number of descriptors, priority
 */
static void config_acp_dma_channel(void __iomem *acp_mmio, u8 ch_num,
				   u16 dscr_strt_idx, u16 num_dscrs,
				   enum acp_dma_priority_level priority_level)
{
	u32 dma_ctrl;

	/* disable the channel run field */
	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/* program a DMA channel with first descriptor to be processed. */
	acp_reg_write((ACP_DMA_DSCR_STRT_IDX_0__DMAChDscrStrtIdx_MASK
			& dscr_strt_idx),
			acp_mmio, mmACP_DMA_DSCR_STRT_IDX_0 + ch_num);

	/*
	 * program a DMA channel with the number of descriptors to be
	 * processed in the transfer
	 */
	acp_reg_write(ACP_DMA_DSCR_CNT_0__DMAChDscrCnt_MASK & num_dscrs,
		      acp_mmio, mmACP_DMA_DSCR_CNT_0 + ch_num);

	/* set DMA channel priority */
	acp_reg_write(priority_level, acp_mmio, mmACP_DMA_PRIO_0 + ch_num);
}

/* Initialize a dma descriptor in SRAM based on descritor information passed */
static void config_dma_descriptor_in_sram(void __iomem *acp_mmio,
					  u16 descr_idx,
					  acp_dma_dscr_transfer_t *descr_info)
{
	u32 sram_offset;

	sram_offset = (descr_idx * sizeof(acp_dma_dscr_transfer_t));

	/* program the source base address. */
	acp_reg_write(sram_offset, acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->src,	acp_mmio, mmACP_SRBM_Targ_Idx_Data);
	/* program the destination base address. */
	acp_reg_write(sram_offset + 4,	acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->dest, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

	/* program the number of bytes to be transferred for this descriptor. */
	acp_reg_write(sram_offset + 8,	acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
	acp_reg_write(descr_info->xfer_val, acp_mmio, mmACP_SRBM_Targ_Idx_Data);
}

/*
 * Initialize the DMA descriptor information for transfer between
 * system memory <-> ACP SRAM
 */
static void set_acp_sysmem_dma_descriptors(void __iomem *acp_mmio,
					   u32 size, int direction,
					   u32 pte_offset, u16 ch,
					   u32 sram_bank, u16 dma_dscr_idx,
					   u32 asic_type)
{
	u16 i;
	acp_dma_dscr_transfer_t dmadscr[NUM_DSCRS_PER_CHANNEL];

	for (i = 0; i < NUM_DSCRS_PER_CHANNEL; i++) {
		dmadscr[i].xfer_val = 0;
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			dma_dscr_idx = dma_dscr_idx + i;
			dmadscr[i].dest = sram_bank + (i * (size / 2));
			dmadscr[i].src = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS
				+ (pte_offset * SZ_4K) + (i * (size / 2));
			switch (asic_type) {
			case CHIP_STONEY:
				dmadscr[i].xfer_val |=
				(ACP_DMA_ATTR_DAGB_GARLIC_TO_SHAREDMEM  << 16) |
				(size / 2);
				break;
			default:
				dmadscr[i].xfer_val |=
				(ACP_DMA_ATTR_DAGB_ONION_TO_SHAREDMEM  << 16) |
				(size / 2);
			}
		} else {
			dma_dscr_idx = dma_dscr_idx + i;
			dmadscr[i].src = sram_bank + (i * (size / 2));
			dmadscr[i].dest =
			ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * SZ_4K) + (i * (size / 2));
			switch (asic_type) {
			case CHIP_STONEY:
				dmadscr[i].xfer_val |=
				BIT(22) |
				(ACP_DMA_ATTR_SHARED_MEM_TO_DAGB_GARLIC << 16) |
				(size / 2);
				break;
			default:
				dmadscr[i].xfer_val |=
				BIT(22) |
				(ACP_DMA_ATTR_SHAREDMEM_TO_DAGB_ONION << 16) |
				(size / 2);
			}
		}
		config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx,
					      &dmadscr[i]);
	}
	config_acp_dma_channel(acp_mmio, ch,
			       dma_dscr_idx - 1,
			       NUM_DSCRS_PER_CHANNEL,
			       ACP_DMA_PRIORITY_LEVEL_NORMAL);
}

/*
 * Initialize the DMA descriptor information for transfer between
 * ACP SRAM <-> I2S
 */
static void set_acp_to_i2s_dma_descriptors(void __iomem *acp_mmio, u32 size,
					   int direction, u32 sram_bank,
					   u16 destination, u16 ch,
					   u16 dma_dscr_idx, u32 asic_type)
{
	u16 i;
	acp_dma_dscr_transfer_t dmadscr[NUM_DSCRS_PER_CHANNEL];

	for (i = 0; i < NUM_DSCRS_PER_CHANNEL; i++) {
		dmadscr[i].xfer_val = 0;
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			dma_dscr_idx = dma_dscr_idx + i;
			dmadscr[i].src = sram_bank  + (i * (size / 2));
			/* dmadscr[i].dest is unused by hardware. */
			dmadscr[i].dest = 0;
			dmadscr[i].xfer_val |= BIT(22) | (destination << 16) |
						(size / 2);
		} else {
			dma_dscr_idx = dma_dscr_idx + i;
			/* dmadscr[i].src is unused by hardware. */
			dmadscr[i].src = 0;
			dmadscr[i].dest =
				 sram_bank + (i * (size / 2));
			dmadscr[i].xfer_val |= BIT(22) |
				(destination << 16) | (size / 2);
		}
		config_dma_descriptor_in_sram(acp_mmio, dma_dscr_idx,
					      &dmadscr[i]);
	}
	/* Configure the DMA channel with the above descriptore */
	config_acp_dma_channel(acp_mmio, ch, dma_dscr_idx - 1,
			       NUM_DSCRS_PER_CHANNEL,
			       ACP_DMA_PRIORITY_LEVEL_NORMAL);
}

/* Create page table entries in ACP SRAM for the allocated memory */
static void acp_pte_config(void __iomem *acp_mmio, struct page *pg,
			   u16 num_of_pages, u32 pte_offset)
{
	u16 page_idx;
	u64 addr;
	u32 low;
	u32 high;
	u32 offset;

	offset	= ACP_DAGB_GRP_SRBM_SRAM_BASE_OFFSET + (pte_offset * 8);
	for (page_idx = 0; page_idx < (num_of_pages); page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		acp_reg_write((offset + (page_idx * 8)),
			      acp_mmio, mmACP_SRBM_Targ_Idx_Addr);
		addr = page_to_phys(pg);

		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		acp_reg_write(low, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

		/* Load the High address of page int ACP SRAM through SRBM */
		acp_reg_write((offset + (page_idx * 8) + 4),
			      acp_mmio, mmACP_SRBM_Targ_Idx_Addr);

		/* page enable in ACP */
		high |= BIT(31);
		acp_reg_write(high, acp_mmio, mmACP_SRBM_Targ_Idx_Data);

		/* Move to next physically contiguos page */
		pg++;
	}
}

static void config_acp_dma(void __iomem *acp_mmio,
			   struct audio_substream_data *rtd,
			   u32 asic_type)
{
	u32 pte_offset, sram_bank;

	if (rtd->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		pte_offset = ACP_PLAYBACK_PTE_OFFSET;
		sram_bank = ACP_SHARED_RAM_BANK_1_ADDRESS;
	} else {
		pte_offset = ACP_CAPTURE_PTE_OFFSET;
		switch (asic_type) {
		case CHIP_STONEY:
			sram_bank = ACP_SHARED_RAM_BANK_3_ADDRESS;
			break;
		default:
			sram_bank = ACP_SHARED_RAM_BANK_5_ADDRESS;
		}
	}
	acp_pte_config(acp_mmio, rtd->pg, rtd->num_of_pages,
		       pte_offset);
	/* Configure System memory <-> ACP SRAM DMA descriptors */
	set_acp_sysmem_dma_descriptors(acp_mmio, rtd->size,
				       rtd->direction, pte_offset,
				       rtd->ch1, sram_bank,
				       rtd->dma_dscr_idx_1, asic_type);
	/* Configure ACP SRAM <-> I2S DMA descriptors */
	set_acp_to_i2s_dma_descriptors(acp_mmio, rtd->size,
				       rtd->direction, sram_bank,
				       rtd->destination, rtd->ch2,
				       rtd->dma_dscr_idx_2, asic_type);
}

/* Start a given DMA channel transfer */
static void acp_dma_start(void __iomem *acp_mmio,
			  u16 ch_num, bool is_circular)
{
	u32 dma_ctrl;

	/* read the dma control register and disable the channel run field */
	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/* Invalidating the DAGB cache */
	acp_reg_write(1, acp_mmio, mmACP_DAGB_ATU_CTRL);

	/*
	 * configure the DMA channel and start the DMA transfer
	 * set dmachrun bit to start the transfer and enable the
	 * interrupt on completion of the dma transfer
	 */
	dma_ctrl |= ACP_DMA_CNTL_0__DMAChRun_MASK;

	switch (ch_num) {
	case ACP_TO_I2S_DMA_CH_NUM:
	case ACP_TO_SYSRAM_CH_NUM:
	case I2S_TO_ACP_DMA_CH_NUM:
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChIOCEn_MASK;
		break;
	default:
		dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;
		break;
	}

	/* enable  for ACP SRAM to/from I2S DMA channel */
	if (is_circular == true)
		dma_ctrl |= ACP_DMA_CNTL_0__Circular_DMA_En_MASK;
	else
		dma_ctrl &= ~ACP_DMA_CNTL_0__Circular_DMA_En_MASK;

	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
}

/* Stop a given DMA channel transfer */
static int acp_dma_stop(void __iomem *acp_mmio, u8 ch_num)
{
	u32 dma_ctrl;
	u32 dma_ch_sts;
	u32 count = ACP_DMA_RESET_TIME;

	dma_ctrl = acp_reg_read(acp_mmio, mmACP_DMA_CNTL_0 + ch_num);

	/*
	 * clear the dma control register fields before writing zero
	 * in reset bit
	 */
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;

	acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	dma_ch_sts = acp_reg_read(acp_mmio, mmACP_DMA_CH_STS);

	if (dma_ch_sts & BIT(ch_num)) {
		/*
		 * set the reset bit for this channel to stop the dma
		 *  transfer
		 */
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChRst_MASK;
		acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0 + ch_num);
	}

	/* check the channel status bit for some time and return the status */
	while (true) {
		dma_ch_sts = acp_reg_read(acp_mmio, mmACP_DMA_CH_STS);
		if (!(dma_ch_sts & BIT(ch_num))) {
			/*
			 * clear the reset flag after successfully stopping
			 * the dma transfer and break from the loop
			 */
			dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRst_MASK;

			acp_reg_write(dma_ctrl, acp_mmio, mmACP_DMA_CNTL_0
				      + ch_num);
			break;
		}
		if (--count == 0) {
			pr_err("Failed to stop ACP DMA channel : %d\n", ch_num);
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	return 0;
}

static void acp_set_sram_bank_state(void __iomem *acp_mmio, u16 bank,
				    bool power_on)
{
	u32 val, req_reg, sts_reg, sts_reg_mask;
	u32 loops = 1000;

	if (bank < 32) {
		req_reg = mmACP_MEM_SHUT_DOWN_REQ_LO;
		sts_reg = mmACP_MEM_SHUT_DOWN_STS_LO;
		sts_reg_mask = 0xFFFFFFFF;

	} else {
		bank -= 32;
		req_reg = mmACP_MEM_SHUT_DOWN_REQ_HI;
		sts_reg = mmACP_MEM_SHUT_DOWN_STS_HI;
		sts_reg_mask = 0x0000FFFF;
	}

	val = acp_reg_read(acp_mmio, req_reg);
	if (val & (1 << bank)) {
		/* bank is in off state */
		if (power_on == true)
			/* request to on */
			val &= ~(1 << bank);
		else
			/* request to off */
			return;
	} else {
		/* bank is in on state */
		if (power_on == false)
			/* request to off */
			val |= 1 << bank;
		else
			/* request to on */
			return;
	}
	acp_reg_write(val, acp_mmio, req_reg);

	while (acp_reg_read(acp_mmio, sts_reg) != sts_reg_mask) {
		if (!loops--) {
			pr_err("ACP SRAM bank %d state change failed\n", bank);
			break;
		}
		cpu_relax();
	}
}

/* Initialize and bring ACP hardware to default state. */
static int acp_init(void __iomem *acp_mmio, u32 asic_type)
{
	u16 bank;
	u32 val, count, sram_pte_offset;

	/* Assert Soft reset of ACP */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	count = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (true) {
		val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
		if (--count == 0) {
			pr_err("Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}

	/* Enable clock to ACP and wait until the clock is enabled */
	val = acp_reg_read(acp_mmio, mmACP_CONTROL);
	val = val | ACP_CONTROL__ClkEn_MASK;
	acp_reg_write(val, acp_mmio, mmACP_CONTROL);

	count = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (true) {
		val = acp_reg_read(acp_mmio, mmACP_STATUS);
		if (val & (u32)0x1)
			break;
		if (--count == 0) {
			pr_err("Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}

	/* Deassert the SOFT RESET flags */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
	val &= ~ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	/* initiailize Onion control DAGB register */
	acp_reg_write(ACP_ONION_CNTL_DEFAULT, acp_mmio,
		      mmACP_AXI2DAGB_ONION_CNTL);

	/* initiailize Garlic control DAGB registers */
	acp_reg_write(ACP_GARLIC_CNTL_DEFAULT, acp_mmio,
		      mmACP_AXI2DAGB_GARLIC_CNTL);

	sram_pte_offset = ACP_DAGB_GRP_SRAM_BASE_ADDRESS |
			ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBSnoopSel_MASK |
			ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBTargetMemSel_MASK |
			ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBGrpEnable_MASK;
	acp_reg_write(sram_pte_offset,  acp_mmio, mmACP_DAGB_BASE_ADDR_GRP_1);
	acp_reg_write(ACP_PAGE_SIZE_4K_ENABLE, acp_mmio,
		      mmACP_DAGB_PAGE_SIZE_GRP_1);

	acp_reg_write(ACP_SRAM_BASE_ADDRESS, acp_mmio,
		      mmACP_DMA_DESC_BASE_ADDR);

	/* Num of descriptiors in SRAM 0x4, means 256 descriptors;(64 * 4) */
	acp_reg_write(0x4, acp_mmio, mmACP_DMA_DESC_MAX_NUM_DSCR);
	acp_reg_write(ACP_EXTERNAL_INTR_CNTL__DMAIOCMask_MASK,
		      acp_mmio, mmACP_EXTERNAL_INTR_CNTL);

       /*
	* When ACP_TILE_P1 is turned on, all SRAM banks get turned on.
	* Now, turn off all of them. This can't be done in 'poweron' of
	* ACP pm domain, as this requires ACP to be initialized.
	* For Stoney, Memory gating is disabled,i.e SRAM Banks
	* won't be turned off. The default state for SRAM banks is ON.
	* Setting SRAM bank state code skipped for STONEY platform.
	*/
	if (asic_type != CHIP_STONEY) {
		for (bank = 1; bank < 48; bank++)
			acp_set_sram_bank_state(acp_mmio, bank, false);
	}
	return 0;
}

/* Deinitialize ACP */
static int acp_deinit(void __iomem *acp_mmio)
{
	u32 val;
	u32 count;

	/* Assert Soft reset of ACP */
	val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	acp_reg_write(val, acp_mmio, mmACP_SOFT_RESET);

	count = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (true) {
		val = acp_reg_read(acp_mmio, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
		if (--count == 0) {
			pr_err("Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	/* Disable ACP clock */
	val = acp_reg_read(acp_mmio, mmACP_CONTROL);
	val &= ~ACP_CONTROL__ClkEn_MASK;
	acp_reg_write(val, acp_mmio, mmACP_CONTROL);

	count = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (true) {
		val = acp_reg_read(acp_mmio, mmACP_STATUS);
		if (!(val & (u32)0x1))
			break;
		if (--count == 0) {
			pr_err("Failed to reset ACP\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	return 0;
}

/* ACP DMA irq handler routine for playback, capture usecases */
static irqreturn_t dma_irq_handler(int irq, void *arg)
{
	u16 dscr_idx;
	u32 intr_flag, ext_intr_status;
	struct audio_drv_data *irq_data;
	void __iomem *acp_mmio;
	struct device *dev = arg;
	bool valid_irq = false;

	irq_data = dev_get_drvdata(dev);
	acp_mmio = irq_data->acp_mmio;

	ext_intr_status = acp_reg_read(acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	intr_flag = (((ext_intr_status &
		      ACP_EXTERNAL_INTR_STAT__DMAIOCStat_MASK) >>
		     ACP_EXTERNAL_INTR_STAT__DMAIOCStat__SHIFT));

	if ((intr_flag & BIT(ACP_TO_I2S_DMA_CH_NUM)) != 0) {
		valid_irq = true;
		if (acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_13) ==
				PLAYBACK_START_DMA_DESCR_CH13)
			dscr_idx = PLAYBACK_END_DMA_DESCR_CH12;
		else
			dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
		config_acp_dma_channel(acp_mmio, SYSRAM_TO_ACP_CH_NUM, dscr_idx,
				       1, 0);
		acp_dma_start(acp_mmio, SYSRAM_TO_ACP_CH_NUM, false);

		snd_pcm_period_elapsed(irq_data->play_i2ssp_stream);

		acp_reg_write((intr_flag & BIT(ACP_TO_I2S_DMA_CH_NUM)) << 16,
			      acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	}

	if ((intr_flag & BIT(I2S_TO_ACP_DMA_CH_NUM)) != 0) {
		valid_irq = true;
		if (acp_reg_read(acp_mmio, mmACP_DMA_CUR_DSCR_15) ==
				CAPTURE_START_DMA_DESCR_CH15)
			dscr_idx = CAPTURE_END_DMA_DESCR_CH14;
		else
			dscr_idx = CAPTURE_START_DMA_DESCR_CH14;
		config_acp_dma_channel(acp_mmio, ACP_TO_SYSRAM_CH_NUM, dscr_idx,
				       1, 0);
		acp_dma_start(acp_mmio, ACP_TO_SYSRAM_CH_NUM, false);

		acp_reg_write((intr_flag & BIT(I2S_TO_ACP_DMA_CH_NUM)) << 16,
			      acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	}

	if ((intr_flag & BIT(ACP_TO_SYSRAM_CH_NUM)) != 0) {
		valid_irq = true;
		snd_pcm_period_elapsed(irq_data->capture_i2ssp_stream);
		acp_reg_write((intr_flag & BIT(ACP_TO_SYSRAM_CH_NUM)) << 16,
			      acp_mmio, mmACP_EXTERNAL_INTR_STAT);
	}

	if (valid_irq)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int acp_dma_open(struct snd_pcm_substream *substream)
{
	u16 bank;
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(prtd,
								    DRV_NAME);
	struct audio_drv_data *intr_data = dev_get_drvdata(component->dev);
	struct audio_substream_data *adata =
		kzalloc(sizeof(struct audio_substream_data), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (intr_data->asic_type) {
		case CHIP_STONEY:
			runtime->hw = acp_st_pcm_hardware_playback;
			break;
		default:
			runtime->hw = acp_pcm_hardware_playback;
		}
	} else {
		switch (intr_data->asic_type) {
		case CHIP_STONEY:
			runtime->hw = acp_st_pcm_hardware_capture;
			break;
		default:
			runtime->hw = acp_pcm_hardware_capture;
		}
	}

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(component->dev, "set integer constraint failed\n");
		kfree(adata);
		return ret;
	}

	adata->acp_mmio = intr_data->acp_mmio;
	runtime->private_data = adata;

	/*
	 * Enable ACP irq, when neither playback or capture streams are
	 * active by the time when a new stream is being opened.
	 * This enablement is not required for another stream, if current
	 * stream is not closed
	 */
	if (!intr_data->play_i2ssp_stream && !intr_data->capture_i2ssp_stream)
		acp_reg_write(1, adata->acp_mmio, mmACP_EXTERNAL_INTR_ENB);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		intr_data->play_i2ssp_stream = substream;
		/*
		 * For Stoney, Memory gating is disabled,i.e SRAM Banks
		 * won't be turned off. The default state for SRAM banks is ON.
		 * Setting SRAM bank state code skipped for STONEY platform.
		 */
		if (intr_data->asic_type != CHIP_STONEY) {
			for (bank = 1; bank <= 4; bank++)
				acp_set_sram_bank_state(intr_data->acp_mmio,
							bank, true);
		}
	} else {
		intr_data->capture_i2ssp_stream = substream;
		if (intr_data->asic_type != CHIP_STONEY) {
			for (bank = 5; bank <= 8; bank++)
				acp_set_sram_bank_state(intr_data->acp_mmio,
							bank, true);
		}
	}

	return 0;
}

static int acp_dma_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int status;
	uint64_t size;
	u32 val = 0;
	struct page *pg;
	struct snd_pcm_runtime *runtime;
	struct audio_substream_data *rtd;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(prtd,
								    DRV_NAME);
	struct audio_drv_data *adata = dev_get_drvdata(component->dev);

	runtime = substream->runtime;
	rtd = runtime->private_data;

	if (WARN_ON(!rtd))
		return -EINVAL;

	if (adata->asic_type == CHIP_STONEY) {
		val = acp_reg_read(adata->acp_mmio,
				   mmACP_I2S_16BIT_RESOLUTION_EN);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			val |= ACP_I2S_SP_16BIT_RESOLUTION_EN;
		else
			val |= ACP_I2S_MIC_16BIT_RESOLUTION_EN;
		acp_reg_write(val, adata->acp_mmio,
			      mmACP_I2S_16BIT_RESOLUTION_EN);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		rtd->ch1 = SYSRAM_TO_ACP_CH_NUM;
		rtd->ch2 = ACP_TO_I2S_DMA_CH_NUM;
		rtd->destination = TO_ACP_I2S_1;
		rtd->dma_dscr_idx_1 = PLAYBACK_START_DMA_DESCR_CH12;
		rtd->dma_dscr_idx_2 = PLAYBACK_START_DMA_DESCR_CH13;
	} else {
		rtd->ch1 = ACP_TO_SYSRAM_CH_NUM;
		rtd->ch2 = I2S_TO_ACP_DMA_CH_NUM;
		rtd->destination = FROM_ACP_I2S_1;
		rtd->dma_dscr_idx_1 = CAPTURE_START_DMA_DESCR_CH14;
		rtd->dma_dscr_idx_2 = CAPTURE_START_DMA_DESCR_CH15;
	}

	size = params_buffer_bytes(params);
	status = snd_pcm_lib_malloc_pages(substream, size);
	if (status < 0)
		return status;

	memset(substream->runtime->dma_area, 0, params_buffer_bytes(params));
	pg = virt_to_page(substream->dma_buffer.area);

	if (pg) {
		acp_set_sram_bank_state(rtd->acp_mmio, 0, true);
		/* Save for runtime private data */
		rtd->pg = pg;
		rtd->order = get_order(size);

		/* Fill the page table entries in ACP SRAM */
		rtd->pg = pg;
		rtd->size = size;
		rtd->num_of_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
		rtd->direction = substream->stream;

		config_acp_dma(rtd->acp_mmio, rtd, adata->asic_type);
		status = 0;
	} else {
		status = -ENOMEM;
	}
	return status;
}

static int acp_dma_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static u64 acp_get_byte_count(void __iomem *acp_mmio, int stream)
{
	union acp_dma_count playback_dma_count;
	union acp_dma_count capture_dma_count;
	u64 bytescount = 0;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		playback_dma_count.bcount.high = acp_reg_read(acp_mmio,
					mmACP_I2S_TRANSMIT_BYTE_CNT_HIGH);
		playback_dma_count.bcount.low  = acp_reg_read(acp_mmio,
					mmACP_I2S_TRANSMIT_BYTE_CNT_LOW);
		bytescount = playback_dma_count.bytescount;
	} else {
		capture_dma_count.bcount.high = acp_reg_read(acp_mmio,
					mmACP_I2S_RECEIVED_BYTE_CNT_HIGH);
		capture_dma_count.bcount.low  = acp_reg_read(acp_mmio,
					mmACP_I2S_RECEIVED_BYTE_CNT_LOW);
		bytescount = capture_dma_count.bytescount;
	}
	return bytescount;
}

static snd_pcm_uframes_t acp_dma_pointer(struct snd_pcm_substream *substream)
{
	u32 buffersize;
	u32 pos = 0;
	u64 bytescount = 0;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	if (!rtd)
		return -EINVAL;

	buffersize = frames_to_bytes(runtime, runtime->buffer_size);
	bytescount = acp_get_byte_count(rtd->acp_mmio, substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (bytescount > rtd->i2ssp_renderbytescount)
			bytescount = bytescount - rtd->i2ssp_renderbytescount;
	} else {
		if (bytescount > rtd->i2ssp_capturebytescount)
			bytescount = bytescount - rtd->i2ssp_capturebytescount;
	}
	pos = do_div(bytescount, buffersize);
	return bytes_to_frames(runtime, pos);
}

static int acp_dma_mmap(struct snd_pcm_substream *substream,
			struct vm_area_struct *vma)
{
	return snd_pcm_lib_default_mmap(substream, vma);
}

static int acp_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;

	if (!rtd)
		return -EINVAL;

	config_acp_dma_channel(rtd->acp_mmio,
			       rtd->ch1,
			       rtd->dma_dscr_idx_1,
			       NUM_DSCRS_PER_CHANNEL, 0);
	config_acp_dma_channel(rtd->acp_mmio,
			       rtd->ch2,
			       rtd->dma_dscr_idx_2,
			       NUM_DSCRS_PER_CHANNEL, 0);
	return 0;
}

static int acp_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret;
	u32 loops = 4000;
	u64 bytescount = 0;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct audio_substream_data *rtd = runtime->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(prtd,
								    DRV_NAME);

	if (!rtd)
		return -EINVAL;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		bytescount = acp_get_byte_count(rtd->acp_mmio,
						substream->stream);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			if (rtd->i2ssp_renderbytescount == 0)
				rtd->i2ssp_renderbytescount = bytescount;
			acp_dma_start(rtd->acp_mmio, rtd->ch1, false);
			while (acp_reg_read(rtd->acp_mmio, mmACP_DMA_CH_STS) &
				BIT(rtd->ch1)) {
				if (!loops--) {
					dev_err(component->dev,
						"acp dma start timeout\n");
					return -ETIMEDOUT;
				}
				cpu_relax();
			}
		} else {
			if (rtd->i2ssp_capturebytescount == 0)
				rtd->i2ssp_capturebytescount = bytescount;
		}
		acp_dma_start(rtd->acp_mmio, rtd->ch2, true);
		ret = 0;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		/* For playback, non circular dma should be stopped first
		 * i.e Sysram to acp dma transfer channel(rtd->ch1) should be
		 * stopped before stopping cirular dma which is acp sram to i2s
		 * fifo dma transfer channel(rtd->ch2). Where as in Capture
		 * scenario, i2s fifo to acp sram dma channel(rtd->ch2) stopped
		 * first before stopping acp sram to sysram which is circular
		 * dma(rtd->ch1).
		 */
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			acp_dma_stop(rtd->acp_mmio, rtd->ch1);
			ret =  acp_dma_stop(rtd->acp_mmio, rtd->ch2);
			rtd->i2ssp_renderbytescount = 0;
		} else {
			acp_dma_stop(rtd->acp_mmio, rtd->ch2);
			ret = acp_dma_stop(rtd->acp_mmio, rtd->ch1);
			rtd->i2ssp_capturebytescount = 0;
		}
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int acp_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd,
								    DRV_NAME);
	struct audio_drv_data *adata = dev_get_drvdata(component->dev);

	switch (adata->asic_type) {
	case CHIP_STONEY:
		ret = snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
							    SNDRV_DMA_TYPE_DEV,
							    NULL, ST_MIN_BUFFER,
							    ST_MAX_BUFFER);
		break;
	default:
		ret = snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
							    SNDRV_DMA_TYPE_DEV,
							    NULL, MIN_BUFFER,
							    MAX_BUFFER);
		break;
	}
	if (ret < 0)
		dev_err(component->dev,
			"buffer preallocation failure error:%d\n", ret);
	return ret;
}

static int acp_dma_close(struct snd_pcm_substream *substream)
{
	u16 bank;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_substream_data *rtd = runtime->private_data;
	struct snd_soc_pcm_runtime *prtd = substream->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(prtd,
								    DRV_NAME);
	struct audio_drv_data *adata = dev_get_drvdata(component->dev);

	kfree(rtd);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		adata->play_i2ssp_stream = NULL;
		/*
		 * For Stoney, Memory gating is disabled,i.e SRAM Banks
		 * won't be turned off. The default state for SRAM banks is ON.
		 * Setting SRAM bank state code skipped for STONEY platform.
		 * added condition checks for Carrizo platform only
		 */
		if (adata->asic_type != CHIP_STONEY) {
			for (bank = 1; bank <= 4; bank++)
				acp_set_sram_bank_state(adata->acp_mmio, bank,
							false);
		}
	} else  {
		adata->capture_i2ssp_stream = NULL;
		if (adata->asic_type != CHIP_STONEY) {
			for (bank = 5; bank <= 8; bank++)
				acp_set_sram_bank_state(adata->acp_mmio, bank,
							false);
		}
	}

	/*
	 * Disable ACP irq, when the current stream is being closed and
	 * another stream is also not active.
	 */
	if (!adata->play_i2ssp_stream && !adata->capture_i2ssp_stream)
		acp_reg_write(0, adata->acp_mmio, mmACP_EXTERNAL_INTR_ENB);

	return 0;
}

static const struct snd_pcm_ops acp_dma_ops = {
	.open = acp_dma_open,
	.close = acp_dma_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = acp_dma_hw_params,
	.hw_free = acp_dma_hw_free,
	.trigger = acp_dma_trigger,
	.pointer = acp_dma_pointer,
	.mmap = acp_dma_mmap,
	.prepare = acp_dma_prepare,
};

static const struct snd_soc_component_driver acp_asoc_platform = {
	.name = DRV_NAME,
	.ops = &acp_dma_ops,
	.pcm_new = acp_dma_new,
};

static int acp_audio_probe(struct platform_device *pdev)
{
	int status;
	struct audio_drv_data *audio_drv_data;
	struct resource *res;
	const u32 *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENODEV;
	}

	audio_drv_data = devm_kzalloc(&pdev->dev, sizeof(struct audio_drv_data),
				      GFP_KERNEL);
	if (!audio_drv_data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	audio_drv_data->acp_mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(audio_drv_data->acp_mmio))
		return PTR_ERR(audio_drv_data->acp_mmio);

	/*
	 * The following members gets populated in device 'open'
	 * function. Till then interrupts are disabled in 'acp_init'
	 * and device doesn't generate any interrupts.
	 */

	audio_drv_data->play_i2ssp_stream = NULL;
	audio_drv_data->capture_i2ssp_stream = NULL;

	audio_drv_data->asic_type =  *pdata;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_IRQ FAILED\n");
		return -ENODEV;
	}

	status = devm_request_irq(&pdev->dev, res->start, dma_irq_handler,
				  0, "ACP_IRQ", &pdev->dev);
	if (status) {
		dev_err(&pdev->dev, "ACP IRQ request failed\n");
		return status;
	}

	dev_set_drvdata(&pdev->dev, audio_drv_data);

	/* Initialize the ACP */
	status = acp_init(audio_drv_data->acp_mmio, audio_drv_data->asic_type);
	if (status) {
		dev_err(&pdev->dev, "ACP Init failed status:%d\n", status);
		return status;
	}

	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp_asoc_platform, NULL, 0);
	if (status != 0) {
		dev_err(&pdev->dev, "Fail to register ALSA platform device\n");
		return status;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 10000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return status;
}

static int acp_audio_remove(struct platform_device *pdev)
{
	int status;
	struct audio_drv_data *adata = dev_get_drvdata(&pdev->dev);

	status = acp_deinit(adata->acp_mmio);
	if (status)
		dev_err(&pdev->dev, "ACP Deinit failed status:%d\n", status);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int acp_pcm_resume(struct device *dev)
{
	u16 bank;
	int status;
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	status = acp_init(adata->acp_mmio, adata->asic_type);
	if (status) {
		dev_err(dev, "ACP Init failed status:%d\n", status);
		return status;
	}

	if (adata->play_i2ssp_stream && adata->play_i2ssp_stream->runtime) {
		/*
		 * For Stoney, Memory gating is disabled,i.e SRAM Banks
		 * won't be turned off. The default state for SRAM banks is ON.
		 * Setting SRAM bank state code skipped for STONEY platform.
		 */
		if (adata->asic_type != CHIP_STONEY) {
			for (bank = 1; bank <= 4; bank++)
				acp_set_sram_bank_state(adata->acp_mmio, bank,
							true);
		}
		config_acp_dma(adata->acp_mmio,
			       adata->play_i2ssp_stream->runtime->private_data,
			       adata->asic_type);
	}
	if (adata->capture_i2ssp_stream &&
	    adata->capture_i2ssp_stream->runtime) {
		if (adata->asic_type != CHIP_STONEY) {
			for (bank = 5; bank <= 8; bank++)
				acp_set_sram_bank_state(adata->acp_mmio, bank,
							true);
		}
		config_acp_dma(adata->acp_mmio,
			       adata->capture_i2ssp_stream->runtime->private_data,
			       adata->asic_type);
	}
	acp_reg_write(1, adata->acp_mmio, mmACP_EXTERNAL_INTR_ENB);
	return 0;
}

static int acp_pcm_runtime_suspend(struct device *dev)
{
	int status;
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	status = acp_deinit(adata->acp_mmio);
	if (status)
		dev_err(dev, "ACP Deinit failed status:%d\n", status);
	acp_reg_write(0, adata->acp_mmio, mmACP_EXTERNAL_INTR_ENB);
	return 0;
}

static int acp_pcm_runtime_resume(struct device *dev)
{
	int status;
	struct audio_drv_data *adata = dev_get_drvdata(dev);

	status = acp_init(adata->acp_mmio, adata->asic_type);
	if (status) {
		dev_err(dev, "ACP Init failed status:%d\n", status);
		return status;
	}
	acp_reg_write(1, adata->acp_mmio, mmACP_EXTERNAL_INTR_ENB);
	return 0;
}

static const struct dev_pm_ops acp_pm_ops = {
	.resume = acp_pcm_resume,
	.runtime_suspend = acp_pcm_runtime_suspend,
	.runtime_resume = acp_pcm_runtime_resume,
};

static struct platform_driver acp_dma_driver = {
	.probe = acp_audio_probe,
	.remove = acp_audio_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &acp_pm_ops,
	},
};

module_platform_driver(acp_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_DESCRIPTION("AMD ACP PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:"DRV_NAME);
