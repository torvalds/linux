/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACP_HW_H
#define __ACP_HW_H

#include "include/acp_2_2_d.h"
#include "include/acp_2_2_sh_mask.h"

#define ACP_PAGE_SIZE_4K_ENABLE			0x02

#define ACP_PLAYBACK_PTE_OFFSET			10
#define ACP_CAPTURE_PTE_OFFSET			0

/* Playback and Capture Offset for Stoney */
#define ACP_ST_PLAYBACK_PTE_OFFSET	0x04
#define ACP_ST_CAPTURE_PTE_OFFSET	0x00
#define ACP_ST_BT_PLAYBACK_PTE_OFFSET	0x08
#define ACP_ST_BT_CAPTURE_PTE_OFFSET	0x0c

#define ACP_GARLIC_CNTL_DEFAULT			0x00000FB4
#define ACP_ONION_CNTL_DEFAULT			0x00000FB4

#define ACP_PHYSICAL_BASE			0x14000

/*
 * In case of I2S SP controller instance, Stoney uses SRAM bank 1 for
 * playback and SRAM Bank 2 for capture where as in case of BT I2S
 * Instance, Stoney uses SRAM Bank 3 for playback & SRAM Bank 4 will
 * be used for capture. Carrizo uses I2S SP controller instance. SRAM Banks
 * 1, 2, 3, 4 will be used for playback & SRAM Banks 5, 6, 7, 8 will be used
 * for capture scenario.
 */
#define ACP_SRAM_BANK_1_ADDRESS		0x4002000
#define ACP_SRAM_BANK_2_ADDRESS		0x4004000
#define ACP_SRAM_BANK_3_ADDRESS		0x4006000
#define ACP_SRAM_BANK_4_ADDRESS		0x4008000
#define ACP_SRAM_BANK_5_ADDRESS		0x400A000

#define ACP_DMA_RESET_TIME			10000
#define ACP_CLOCK_EN_TIME_OUT_VALUE		0x000000FF
#define ACP_SOFT_RESET_DONE_TIME_OUT_VALUE	0x000000FF
#define ACP_DMA_COMPLETE_TIME_OUT_VALUE		0x000000FF

#define ACP_SRAM_BASE_ADDRESS			0x4000000
#define ACP_DAGB_GRP_SRAM_BASE_ADDRESS		0x4001000
#define ACP_DAGB_GRP_SRBM_SRAM_BASE_OFFSET	0x1000
#define ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS	0x00000000
#define ACP_INTERNAL_APERTURE_WINDOW_4_ADDRESS	0x01800000

#define TO_ACP_I2S_1   0x2
#define TO_ACP_I2S_2   0x4
#define TO_BLUETOOTH   0x3
#define FROM_ACP_I2S_1 0xa
#define FROM_ACP_I2S_2 0xb
#define FROM_BLUETOOTH 0xb

#define I2S_SP_INSTANCE                 0x01
#define I2S_BT_INSTANCE                 0x02
#define CAP_CHANNEL0			0x00
#define CAP_CHANNEL1			0x01

#define ACP_TILE_ON_MASK                0x03
#define ACP_TILE_OFF_MASK               0x02
#define ACP_TILE_ON_RETAIN_REG_MASK     0x1f
#define ACP_TILE_OFF_RETAIN_REG_MASK    0x20

#define ACP_TILE_P1_MASK                0x3e
#define ACP_TILE_P2_MASK                0x3d
#define ACP_TILE_DSP0_MASK              0x3b
#define ACP_TILE_DSP1_MASK              0x37

#define ACP_TILE_DSP2_MASK              0x2f
/* Playback DMA channels */
#define SYSRAM_TO_ACP_CH_NUM 12
#define ACP_TO_I2S_DMA_CH_NUM 13

/* Capture DMA channels */
#define I2S_TO_ACP_DMA_CH_NUM 14
#define ACP_TO_SYSRAM_CH_NUM 15

/* Playback DMA Channels for I2S BT instance */
#define SYSRAM_TO_ACP_BT_INSTANCE_CH_NUM  8
#define ACP_TO_I2S_DMA_BT_INSTANCE_CH_NUM 9

/* Capture DMA Channels for I2S BT Instance */
#define I2S_TO_ACP_DMA_BT_INSTANCE_CH_NUM 10
#define ACP_TO_SYSRAM_BT_INSTANCE_CH_NUM 11

#define NUM_DSCRS_PER_CHANNEL 2

#define PLAYBACK_START_DMA_DESCR_CH12 0
#define PLAYBACK_END_DMA_DESCR_CH12 1
#define PLAYBACK_START_DMA_DESCR_CH13 2
#define PLAYBACK_END_DMA_DESCR_CH13 3

#define CAPTURE_START_DMA_DESCR_CH14 4
#define CAPTURE_END_DMA_DESCR_CH14 5
#define CAPTURE_START_DMA_DESCR_CH15 6
#define CAPTURE_END_DMA_DESCR_CH15 7

/* I2S BT Instance DMA Descriptors */
#define PLAYBACK_START_DMA_DESCR_CH8 8
#define PLAYBACK_END_DMA_DESCR_CH8 9
#define PLAYBACK_START_DMA_DESCR_CH9 10
#define PLAYBACK_END_DMA_DESCR_CH9 11

#define CAPTURE_START_DMA_DESCR_CH10 12
#define CAPTURE_END_DMA_DESCR_CH10 13
#define CAPTURE_START_DMA_DESCR_CH11 14
#define CAPTURE_END_DMA_DESCR_CH11 15

#define mmACP_I2S_16BIT_RESOLUTION_EN       0x5209
#define ACP_I2S_MIC_16BIT_RESOLUTION_EN 0x01
#define ACP_I2S_SP_16BIT_RESOLUTION_EN	0x02
#define ACP_I2S_BT_16BIT_RESOLUTION_EN	0x04
#define ACP_BT_UART_PAD_SELECT_MASK	0x1

enum acp_dma_priority_level {
	/* 0x0 Specifies the DMA channel is given normal priority */
	ACP_DMA_PRIORITY_LEVEL_NORMAL = 0x0,
	/* 0x1 Specifies the DMA channel is given high priority */
	ACP_DMA_PRIORITY_LEVEL_HIGH = 0x1,
	ACP_DMA_PRIORITY_LEVEL_FORCESIZE = 0xFF
};

struct audio_substream_data {
	struct page *pg;
	unsigned int order;
	u16 num_of_pages;
	u16 i2s_instance;
	u16 capture_channel;
	u16 direction;
	u16 ch1;
	u16 ch2;
	u16 destination;
	u16 dma_dscr_idx_1;
	u16 dma_dscr_idx_2;
	u32 pte_offset;
	u32 sram_bank;
	u32 byte_cnt_high_reg_offset;
	u32 byte_cnt_low_reg_offset;
	u32 dma_curr_dscr;
	uint64_t size;
	u64 bytescount;
	void __iomem *acp_mmio;
};

struct audio_drv_data {
	struct snd_pcm_substream *play_i2ssp_stream;
	struct snd_pcm_substream *capture_i2ssp_stream;
	struct snd_pcm_substream *play_i2sbt_stream;
	struct snd_pcm_substream *capture_i2sbt_stream;
	void __iomem *acp_mmio;
	u32 asic_type;
};

/*
 * this structure used for platform data transfer between machine driver
 * and dma driver
 */
struct acp_platform_info {
	u16 i2s_instance;
	u16 capture_channel;
};

union acp_dma_count {
	struct {
	u32 low;
	u32 high;
	} bcount;
	u64 bytescount;
};

enum {
	ACP_TILE_P1 = 0,
	ACP_TILE_P2,
	ACP_TILE_DSP0,
	ACP_TILE_DSP1,
	ACP_TILE_DSP2,
};

enum {
	ACP_DMA_ATTR_SHAREDMEM_TO_DAGB_ONION = 0x0,
	ACP_DMA_ATTR_SHARED_MEM_TO_DAGB_GARLIC = 0x1,
	ACP_DMA_ATTR_DAGB_ONION_TO_SHAREDMEM = 0x8,
	ACP_DMA_ATTR_DAGB_GARLIC_TO_SHAREDMEM = 0x9,
	ACP_DMA_ATTR_FORCE_SIZE = 0xF
};

typedef struct acp_dma_dscr_transfer {
	/* Specifies the source memory location for the DMA data transfer. */
	u32 src;
	/*
	 * Specifies the destination memory location to where the data will
	 * be transferred.
	 */
	u32 dest;
	/*
	 * Specifies the number of bytes need to be transferred
	 * from source to destination memory.Transfer direction & IOC enable
	 */
	u32 xfer_val;
	/* Reserved for future use */
	u32 reserved;
} acp_dma_dscr_transfer_t;

#endif /*__ACP_HW_H */
