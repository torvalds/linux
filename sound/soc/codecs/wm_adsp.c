// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm_adsp.c  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm_adsp.h"

#define adsp_crit(_dsp, fmt, ...) \
	dev_crit(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define adsp_err(_dsp, fmt, ...) \
	dev_err(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define adsp_warn(_dsp, fmt, ...) \
	dev_warn(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define adsp_info(_dsp, fmt, ...) \
	dev_info(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define adsp_dbg(_dsp, fmt, ...) \
	dev_dbg(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)

#define compr_err(_obj, fmt, ...) \
	adsp_err(_obj->dsp, "%s: " fmt, _obj->name ? _obj->name : "legacy", \
		 ##__VA_ARGS__)
#define compr_dbg(_obj, fmt, ...) \
	adsp_dbg(_obj->dsp, "%s: " fmt, _obj->name ? _obj->name : "legacy", \
		 ##__VA_ARGS__)

#define ADSP1_CONTROL_1                   0x00
#define ADSP1_CONTROL_2                   0x02
#define ADSP1_CONTROL_3                   0x03
#define ADSP1_CONTROL_4                   0x04
#define ADSP1_CONTROL_5                   0x06
#define ADSP1_CONTROL_6                   0x07
#define ADSP1_CONTROL_7                   0x08
#define ADSP1_CONTROL_8                   0x09
#define ADSP1_CONTROL_9                   0x0A
#define ADSP1_CONTROL_10                  0x0B
#define ADSP1_CONTROL_11                  0x0C
#define ADSP1_CONTROL_12                  0x0D
#define ADSP1_CONTROL_13                  0x0F
#define ADSP1_CONTROL_14                  0x10
#define ADSP1_CONTROL_15                  0x11
#define ADSP1_CONTROL_16                  0x12
#define ADSP1_CONTROL_17                  0x13
#define ADSP1_CONTROL_18                  0x14
#define ADSP1_CONTROL_19                  0x16
#define ADSP1_CONTROL_20                  0x17
#define ADSP1_CONTROL_21                  0x18
#define ADSP1_CONTROL_22                  0x1A
#define ADSP1_CONTROL_23                  0x1B
#define ADSP1_CONTROL_24                  0x1C
#define ADSP1_CONTROL_25                  0x1E
#define ADSP1_CONTROL_26                  0x20
#define ADSP1_CONTROL_27                  0x21
#define ADSP1_CONTROL_28                  0x22
#define ADSP1_CONTROL_29                  0x23
#define ADSP1_CONTROL_30                  0x24
#define ADSP1_CONTROL_31                  0x26

/*
 * ADSP1 Control 19
 */
#define ADSP1_WDMA_BUFFER_LENGTH_MASK     0x00FF  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_SHIFT         0  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_WIDTH         8  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */


/*
 * ADSP1 Control 30
 */
#define ADSP1_DBG_CLK_ENA                 0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_MASK            0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_SHIFT                3  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_WIDTH                1  /* DSP1_DBG_CLK_ENA */
#define ADSP1_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP1_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP1_START                       0x0001  /* DSP1_START */
#define ADSP1_START_MASK                  0x0001  /* DSP1_START */
#define ADSP1_START_SHIFT                      0  /* DSP1_START */
#define ADSP1_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP1 Control 31
 */
#define ADSP1_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

#define ADSP2_CONTROL                     0x0
#define ADSP2_CLOCKING                    0x1
#define ADSP2V2_CLOCKING                  0x2
#define ADSP2_STATUS1                     0x4
#define ADSP2_WDMA_CONFIG_1               0x30
#define ADSP2_WDMA_CONFIG_2               0x31
#define ADSP2V2_WDMA_CONFIG_2             0x32
#define ADSP2_RDMA_CONFIG_1               0x34

#define ADSP2_SCRATCH0                    0x40
#define ADSP2_SCRATCH1                    0x41
#define ADSP2_SCRATCH2                    0x42
#define ADSP2_SCRATCH3                    0x43

#define ADSP2V2_SCRATCH0_1                0x40
#define ADSP2V2_SCRATCH2_3                0x42

/*
 * ADSP2 Control
 */

#define ADSP2_MEM_ENA                     0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_MASK                0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_SHIFT                    4  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_WIDTH                    1  /* DSP1_MEM_ENA */
#define ADSP2_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP2_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP2_START                       0x0001  /* DSP1_START */
#define ADSP2_START_MASK                  0x0001  /* DSP1_START */
#define ADSP2_START_SHIFT                      0  /* DSP1_START */
#define ADSP2_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP2 clocking
 */
#define ADSP2_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

/*
 * ADSP2V2 clocking
 */
#define ADSP2V2_CLK_SEL_MASK             0x70000  /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_SHIFT                 16  /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_WIDTH                  3  /* CLK_SEL_ENA */

#define ADSP2V2_RATE_MASK                 0x7800  /* DSP_RATE */
#define ADSP2V2_RATE_SHIFT                    11  /* DSP_RATE */
#define ADSP2V2_RATE_WIDTH                     4  /* DSP_RATE */

/*
 * ADSP2 Status 1
 */
#define ADSP2_RAM_RDY                     0x0001
#define ADSP2_RAM_RDY_MASK                0x0001
#define ADSP2_RAM_RDY_SHIFT                    0
#define ADSP2_RAM_RDY_WIDTH                    1

/*
 * ADSP2 Lock support
 */
#define ADSP2_LOCK_CODE_0                    0x5555
#define ADSP2_LOCK_CODE_1                    0xAAAA

#define ADSP2_WATCHDOG                       0x0A
#define ADSP2_BUS_ERR_ADDR                   0x52
#define ADSP2_REGION_LOCK_STATUS             0x64
#define ADSP2_LOCK_REGION_1_LOCK_REGION_0    0x66
#define ADSP2_LOCK_REGION_3_LOCK_REGION_2    0x68
#define ADSP2_LOCK_REGION_5_LOCK_REGION_4    0x6A
#define ADSP2_LOCK_REGION_7_LOCK_REGION_6    0x6C
#define ADSP2_LOCK_REGION_9_LOCK_REGION_8    0x6E
#define ADSP2_LOCK_REGION_CTRL               0x7A
#define ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR    0x7C

#define ADSP2_REGION_LOCK_ERR_MASK           0x8000
#define ADSP2_SLAVE_ERR_MASK                 0x4000
#define ADSP2_WDT_TIMEOUT_STS_MASK           0x2000
#define ADSP2_CTRL_ERR_PAUSE_ENA             0x0002
#define ADSP2_CTRL_ERR_EINT                  0x0001

#define ADSP2_BUS_ERR_ADDR_MASK              0x00FFFFFF
#define ADSP2_XMEM_ERR_ADDR_MASK             0x0000FFFF
#define ADSP2_PMEM_ERR_ADDR_MASK             0x7FFF0000
#define ADSP2_PMEM_ERR_ADDR_SHIFT            16
#define ADSP2_WDT_ENA_MASK                   0xFFFFFFFD

#define ADSP2_LOCK_REGION_SHIFT              16

#define ADSP_MAX_STD_CTRL_SIZE               512

#define WM_ADSP_ACKED_CTL_TIMEOUT_MS         100
#define WM_ADSP_ACKED_CTL_N_QUICKPOLLS       10
#define WM_ADSP_ACKED_CTL_MIN_VALUE          0
#define WM_ADSP_ACKED_CTL_MAX_VALUE          0xFFFFFF

/*
 * Event control messages
 */
#define WM_ADSP_FW_EVENT_SHUTDOWN            0x000001

/*
 * HALO system info
 */
#define HALO_AHBM_WINDOW_DEBUG_0             0x02040
#define HALO_AHBM_WINDOW_DEBUG_1             0x02044

/*
 * HALO core
 */
#define HALO_SCRATCH1                        0x005c0
#define HALO_SCRATCH2                        0x005c8
#define HALO_SCRATCH3                        0x005d0
#define HALO_SCRATCH4                        0x005d8
#define HALO_CCM_CORE_CONTROL                0x41000
#define HALO_CORE_SOFT_RESET                 0x00010
#define HALO_WDT_CONTROL                     0x47000

/*
 * HALO MPU banks
 */
#define HALO_MPU_XMEM_ACCESS_0               0x43000
#define HALO_MPU_YMEM_ACCESS_0               0x43004
#define HALO_MPU_WINDOW_ACCESS_0             0x43008
#define HALO_MPU_XREG_ACCESS_0               0x4300C
#define HALO_MPU_YREG_ACCESS_0               0x43014
#define HALO_MPU_XMEM_ACCESS_1               0x43018
#define HALO_MPU_YMEM_ACCESS_1               0x4301C
#define HALO_MPU_WINDOW_ACCESS_1             0x43020
#define HALO_MPU_XREG_ACCESS_1               0x43024
#define HALO_MPU_YREG_ACCESS_1               0x4302C
#define HALO_MPU_XMEM_ACCESS_2               0x43030
#define HALO_MPU_YMEM_ACCESS_2               0x43034
#define HALO_MPU_WINDOW_ACCESS_2             0x43038
#define HALO_MPU_XREG_ACCESS_2               0x4303C
#define HALO_MPU_YREG_ACCESS_2               0x43044
#define HALO_MPU_XMEM_ACCESS_3               0x43048
#define HALO_MPU_YMEM_ACCESS_3               0x4304C
#define HALO_MPU_WINDOW_ACCESS_3             0x43050
#define HALO_MPU_XREG_ACCESS_3               0x43054
#define HALO_MPU_YREG_ACCESS_3               0x4305C
#define HALO_MPU_XM_VIO_ADDR                 0x43100
#define HALO_MPU_XM_VIO_STATUS               0x43104
#define HALO_MPU_YM_VIO_ADDR                 0x43108
#define HALO_MPU_YM_VIO_STATUS               0x4310C
#define HALO_MPU_PM_VIO_ADDR                 0x43110
#define HALO_MPU_PM_VIO_STATUS               0x43114
#define HALO_MPU_LOCK_CONFIG                 0x43140

/*
 * HALO_AHBM_WINDOW_DEBUG_1
 */
#define HALO_AHBM_CORE_ERR_ADDR_MASK         0x0fffff00
#define HALO_AHBM_CORE_ERR_ADDR_SHIFT                 8
#define HALO_AHBM_FLAGS_ERR_MASK             0x000000ff

/*
 * HALO_CCM_CORE_CONTROL
 */
#define HALO_CORE_EN                        0x00000001

/*
 * HALO_CORE_SOFT_RESET
 */
#define HALO_CORE_SOFT_RESET_MASK           0x00000001

/*
 * HALO_WDT_CONTROL
 */
#define HALO_WDT_EN_MASK                    0x00000001

/*
 * HALO_MPU_?M_VIO_STATUS
 */
#define HALO_MPU_VIO_STS_MASK               0x007e0000
#define HALO_MPU_VIO_STS_SHIFT                      17
#define HALO_MPU_VIO_ERR_WR_MASK            0x00008000
#define HALO_MPU_VIO_ERR_SRC_MASK           0x00007fff
#define HALO_MPU_VIO_ERR_SRC_SHIFT                   0

static struct wm_adsp_ops wm_adsp1_ops;
static struct wm_adsp_ops wm_adsp2_ops[];
static struct wm_adsp_ops wm_halo_ops;

struct wm_adsp_buf {
	struct list_head list;
	void *buf;
};

static struct wm_adsp_buf *wm_adsp_buf_alloc(const void *src, size_t len,
					     struct list_head *list)
{
	struct wm_adsp_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	if (buf == NULL)
		return NULL;

	buf->buf = vmalloc(len);
	if (!buf->buf) {
		kfree(buf);
		return NULL;
	}
	memcpy(buf->buf, src, len);

	if (list)
		list_add_tail(&buf->list, list);

	return buf;
}

static void wm_adsp_buf_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct wm_adsp_buf *buf = list_first_entry(list,
							   struct wm_adsp_buf,
							   list);
		list_del(&buf->list);
		vfree(buf->buf);
		kfree(buf);
	}
}

#define WM_ADSP_FW_MBC_VSS  0
#define WM_ADSP_FW_HIFI     1
#define WM_ADSP_FW_TX       2
#define WM_ADSP_FW_TX_SPK   3
#define WM_ADSP_FW_RX       4
#define WM_ADSP_FW_RX_ANC   5
#define WM_ADSP_FW_CTRL     6
#define WM_ADSP_FW_ASR      7
#define WM_ADSP_FW_TRACE    8
#define WM_ADSP_FW_SPK_PROT 9
#define WM_ADSP_FW_MISC     10

#define WM_ADSP_NUM_FW      11

static const char *wm_adsp_fw_text[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] =  "MBC/VSS",
	[WM_ADSP_FW_HIFI] =     "MasterHiFi",
	[WM_ADSP_FW_TX] =       "Tx",
	[WM_ADSP_FW_TX_SPK] =   "Tx Speaker",
	[WM_ADSP_FW_RX] =       "Rx",
	[WM_ADSP_FW_RX_ANC] =   "Rx ANC",
	[WM_ADSP_FW_CTRL] =     "Voice Ctrl",
	[WM_ADSP_FW_ASR] =      "ASR Assist",
	[WM_ADSP_FW_TRACE] =    "Dbg Trace",
	[WM_ADSP_FW_SPK_PROT] = "Protection",
	[WM_ADSP_FW_MISC] =     "Misc",
};

struct wm_adsp_system_config_xm_hdr {
	__be32 sys_enable;
	__be32 fw_id;
	__be32 fw_rev;
	__be32 boot_status;
	__be32 watchdog;
	__be32 dma_buffer_size;
	__be32 rdma[6];
	__be32 wdma[8];
	__be32 build_job_name[3];
	__be32 build_job_number;
};

struct wm_halo_system_config_xm_hdr {
	__be32 halo_heartbeat;
	__be32 build_job_name[3];
	__be32 build_job_number;
};

struct wm_adsp_alg_xm_struct {
	__be32 magic;
	__be32 smoothing;
	__be32 threshold;
	__be32 host_buf_ptr;
	__be32 start_seq;
	__be32 high_water_mark;
	__be32 low_water_mark;
	__be64 smoothed_power;
};

struct wm_adsp_host_buf_coeff_v1 {
	__be32 host_buf_ptr;		/* Host buffer pointer */
	__be32 versions;		/* Version numbers */
	__be32 name[4];			/* The buffer name */
};

struct wm_adsp_buffer {
	__be32 buf1_base;		/* Base addr of first buffer area */
	__be32 buf1_size;		/* Size of buf1 area in DSP words */
	__be32 buf2_base;		/* Base addr of 2nd buffer area */
	__be32 buf1_buf2_size;		/* Size of buf1+buf2 in DSP words */
	__be32 buf3_base;		/* Base addr of buf3 area */
	__be32 buf_total_size;		/* Size of buf1+buf2+buf3 in DSP words */
	__be32 high_water_mark;		/* Point at which IRQ is asserted */
	__be32 irq_count;		/* bits 1-31 count IRQ assertions */
	__be32 irq_ack;			/* acked IRQ count, bit 0 enables IRQ */
	__be32 next_write_index;	/* word index of next write */
	__be32 next_read_index;		/* word index of next read */
	__be32 error;			/* error if any */
	__be32 oldest_block_index;	/* word index of oldest surviving */
	__be32 requested_rewind;	/* how many blocks rewind was done */
	__be32 reserved_space;		/* internal */
	__be32 min_free;		/* min free space since stream start */
	__be32 blocks_written[2];	/* total blocks written (64 bit) */
	__be32 words_written[2];	/* total words written (64 bit) */
};

struct wm_adsp_compr;

struct wm_adsp_compr_buf {
	struct list_head list;
	struct wm_adsp *dsp;
	struct wm_adsp_compr *compr;

	struct wm_adsp_buffer_region *regions;
	u32 host_buf_ptr;

	u32 error;
	u32 irq_count;
	int read_index;
	int avail;
	int host_buf_mem_type;

	char *name;
};

struct wm_adsp_compr {
	struct list_head list;
	struct wm_adsp *dsp;
	struct wm_adsp_compr_buf *buf;

	struct snd_compr_stream *stream;
	struct snd_compressed_buffer size;

	u32 *raw_buf;
	unsigned int copied_total;

	unsigned int sample_rate;

	const char *name;
};

#define WM_ADSP_DATA_WORD_SIZE         3

#define WM_ADSP_MIN_FRAGMENTS          1
#define WM_ADSP_MAX_FRAGMENTS          256
#define WM_ADSP_MIN_FRAGMENT_SIZE      (64 * WM_ADSP_DATA_WORD_SIZE)
#define WM_ADSP_MAX_FRAGMENT_SIZE      (4096 * WM_ADSP_DATA_WORD_SIZE)

#define WM_ADSP_ALG_XM_STRUCT_MAGIC    0x49aec7

#define HOST_BUFFER_FIELD(field) \
	(offsetof(struct wm_adsp_buffer, field) / sizeof(__be32))

#define ALG_XM_FIELD(field) \
	(offsetof(struct wm_adsp_alg_xm_struct, field) / sizeof(__be32))

#define HOST_BUF_COEFF_SUPPORTED_COMPAT_VER	1

#define HOST_BUF_COEFF_COMPAT_VER_MASK		0xFF00
#define HOST_BUF_COEFF_COMPAT_VER_SHIFT		8

static int wm_adsp_buffer_init(struct wm_adsp *dsp);
static int wm_adsp_buffer_free(struct wm_adsp *dsp);

struct wm_adsp_buffer_region {
	unsigned int offset;
	unsigned int cumulative_size;
	unsigned int mem_type;
	unsigned int base_addr;
};

struct wm_adsp_buffer_region_def {
	unsigned int mem_type;
	unsigned int base_offset;
	unsigned int size_offset;
};

static const struct wm_adsp_buffer_region_def default_regions[] = {
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(buf1_base),
		.size_offset = HOST_BUFFER_FIELD(buf1_size),
	},
	{
		.mem_type = WMFW_ADSP2_XM,
		.base_offset = HOST_BUFFER_FIELD(buf2_base),
		.size_offset = HOST_BUFFER_FIELD(buf1_buf2_size),
	},
	{
		.mem_type = WMFW_ADSP2_YM,
		.base_offset = HOST_BUFFER_FIELD(buf3_base),
		.size_offset = HOST_BUFFER_FIELD(buf_total_size),
	},
};

struct wm_adsp_fw_caps {
	u32 id;
	struct snd_codec_desc desc;
	int num_regions;
	const struct wm_adsp_buffer_region_def *region_defs;
};

static const struct wm_adsp_fw_caps ctrl_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 8,
			.sample_rates = { 16000 },
			.num_sample_rates = 1,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct wm_adsp_fw_caps trace_caps[] = {
	{
		.id = SND_AUDIOCODEC_BESPOKE,
		.desc = {
			.max_ch = 8,
			.sample_rates = {
				4000, 8000, 11025, 12000, 16000, 22050,
				24000, 32000, 44100, 48000, 64000, 88200,
				96000, 176400, 192000
			},
			.num_sample_rates = 15,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.num_regions = ARRAY_SIZE(default_regions),
		.region_defs = default_regions,
	},
};

static const struct {
	const char *file;
	int compr_direction;
	int num_caps;
	const struct wm_adsp_fw_caps *caps;
	bool voice_trigger;
} wm_adsp_fw[WM_ADSP_NUM_FW] = {
	[WM_ADSP_FW_MBC_VSS] =  { .file = "mbc-vss" },
	[WM_ADSP_FW_HIFI] =     { .file = "hifi" },
	[WM_ADSP_FW_TX] =       { .file = "tx" },
	[WM_ADSP_FW_TX_SPK] =   { .file = "tx-spk" },
	[WM_ADSP_FW_RX] =       { .file = "rx" },
	[WM_ADSP_FW_RX_ANC] =   { .file = "rx-anc" },
	[WM_ADSP_FW_CTRL] =     {
		.file = "ctrl",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(ctrl_caps),
		.caps = ctrl_caps,
		.voice_trigger = true,
	},
	[WM_ADSP_FW_ASR] =      { .file = "asr" },
	[WM_ADSP_FW_TRACE] =    {
		.file = "trace",
		.compr_direction = SND_COMPRESS_CAPTURE,
		.num_caps = ARRAY_SIZE(trace_caps),
		.caps = trace_caps,
	},
	[WM_ADSP_FW_SPK_PROT] = { .file = "spk-prot" },
	[WM_ADSP_FW_MISC] =     { .file = "misc" },
};

struct wm_coeff_ctl_ops {
	int (*xget)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
	int (*xput)(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol);
};

struct wm_coeff_ctl {
	const char *name;
	const char *fw_name;
	struct wm_adsp_alg_region alg_region;
	struct wm_coeff_ctl_ops ops;
	struct wm_adsp *dsp;
	unsigned int enabled:1;
	struct list_head list;
	void *cache;
	unsigned int offset;
	size_t len;
	unsigned int set:1;
	struct soc_bytes_ext bytes_ext;
	unsigned int flags;
	unsigned int type;
};

static const char *wm_adsp_mem_region_name(unsigned int type)
{
	switch (type) {
	case WMFW_ADSP1_PM:
		return "PM";
	case WMFW_HALO_PM_PACKED:
		return "PM_PACKED";
	case WMFW_ADSP1_DM:
		return "DM";
	case WMFW_ADSP2_XM:
		return "XM";
	case WMFW_HALO_XM_PACKED:
		return "XM_PACKED";
	case WMFW_ADSP2_YM:
		return "YM";
	case WMFW_HALO_YM_PACKED:
		return "YM_PACKED";
	case WMFW_ADSP1_ZM:
		return "ZM";
	default:
		return NULL;
	}
}

#ifdef CONFIG_DEBUG_FS
static void wm_adsp_debugfs_save_wmfwname(struct wm_adsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	kfree(dsp->wmfw_file_name);
	dsp->wmfw_file_name = tmp;
}

static void wm_adsp_debugfs_save_binname(struct wm_adsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	kfree(dsp->bin_file_name);
	dsp->bin_file_name = tmp;
}

static void wm_adsp_debugfs_clear(struct wm_adsp *dsp)
{
	kfree(dsp->wmfw_file_name);
	kfree(dsp->bin_file_name);
	dsp->wmfw_file_name = NULL;
	dsp->bin_file_name = NULL;
}

static ssize_t wm_adsp_debugfs_wmfw_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct wm_adsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->pwr_lock);

	if (!dsp->wmfw_file_name || !dsp->booted)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->wmfw_file_name,
					      strlen(dsp->wmfw_file_name));

	mutex_unlock(&dsp->pwr_lock);
	return ret;
}

static ssize_t wm_adsp_debugfs_bin_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct wm_adsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->pwr_lock);

	if (!dsp->bin_file_name || !dsp->booted)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->bin_file_name,
					      strlen(dsp->bin_file_name));

	mutex_unlock(&dsp->pwr_lock);
	return ret;
}

static const struct {
	const char *name;
	const struct file_operations fops;
} wm_adsp_debugfs_fops[] = {
	{
		.name = "wmfw_file_name",
		.fops = {
			.open = simple_open,
			.read = wm_adsp_debugfs_wmfw_read,
		},
	},
	{
		.name = "bin_file_name",
		.fops = {
			.open = simple_open,
			.read = wm_adsp_debugfs_bin_read,
		},
	},
};

static void wm_adsp2_init_debugfs(struct wm_adsp *dsp,
				  struct snd_soc_component *component)
{
	struct dentry *root = NULL;
	int i;

	if (!component->debugfs_root) {
		adsp_err(dsp, "No codec debugfs root\n");
		goto err;
	}

	root = debugfs_create_dir(dsp->name, component->debugfs_root);

	if (!root)
		goto err;

	if (!debugfs_create_bool("booted", 0444, root, &dsp->booted))
		goto err;

	if (!debugfs_create_bool("running", 0444, root, &dsp->running))
		goto err;

	if (!debugfs_create_x32("fw_id", 0444, root, &dsp->fw_id))
		goto err;

	if (!debugfs_create_x32("fw_version", 0444, root, &dsp->fw_id_version))
		goto err;

	for (i = 0; i < ARRAY_SIZE(wm_adsp_debugfs_fops); ++i) {
		if (!debugfs_create_file(wm_adsp_debugfs_fops[i].name,
					 0444, root, dsp,
					 &wm_adsp_debugfs_fops[i].fops))
			goto err;
	}

	dsp->debugfs_root = root;
	return;

err:
	debugfs_remove_recursive(root);
	adsp_err(dsp, "Failed to create debugfs\n");
}

static void wm_adsp2_cleanup_debugfs(struct wm_adsp *dsp)
{
	wm_adsp_debugfs_clear(dsp);
	debugfs_remove_recursive(dsp->debugfs_root);
}
#else
static inline void wm_adsp2_init_debugfs(struct wm_adsp *dsp,
					 struct snd_soc_component *component)
{
}

static inline void wm_adsp2_cleanup_debugfs(struct wm_adsp *dsp)
{
}

static inline void wm_adsp_debugfs_save_wmfwname(struct wm_adsp *dsp,
						 const char *s)
{
}

static inline void wm_adsp_debugfs_save_binname(struct wm_adsp *dsp,
						const char *s)
{
}

static inline void wm_adsp_debugfs_clear(struct wm_adsp *dsp)
{
}
#endif

int wm_adsp_fw_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = dsp[e->shift_l].fw;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_fw_get);

int wm_adsp_fw_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct wm_adsp *dsp = snd_soc_component_get_drvdata(component);
	int ret = 0;

	if (ucontrol->value.enumerated.item[0] == dsp[e->shift_l].fw)
		return 0;

	if (ucontrol->value.enumerated.item[0] >= WM_ADSP_NUM_FW)
		return -EINVAL;

	mutex_lock(&dsp[e->shift_l].pwr_lock);

	if (dsp[e->shift_l].booted || !list_empty(&dsp[e->shift_l].compr_list))
		ret = -EBUSY;
	else
		dsp[e->shift_l].fw = ucontrol->value.enumerated.item[0];

	mutex_unlock(&dsp[e->shift_l].pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_fw_put);

const struct soc_enum wm_adsp_fw_enum[] = {
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 1, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 2, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 3, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 4, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 5, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
	SOC_ENUM_SINGLE(0, 6, ARRAY_SIZE(wm_adsp_fw_text), wm_adsp_fw_text),
};
EXPORT_SYMBOL_GPL(wm_adsp_fw_enum);

static struct wm_adsp_region const *wm_adsp_find_region(struct wm_adsp *dsp,
							int type)
{
	int i;

	for (i = 0; i < dsp->num_mems; i++)
		if (dsp->mem[i].type == type)
			return &dsp->mem[i];

	return NULL;
}

static unsigned int wm_adsp_region_to_reg(struct wm_adsp_region const *mem,
					  unsigned int offset)
{
	switch (mem->type) {
	case WMFW_ADSP1_PM:
		return mem->base + (offset * 3);
	case WMFW_ADSP1_DM:
	case WMFW_ADSP2_XM:
	case WMFW_ADSP2_YM:
	case WMFW_ADSP1_ZM:
		return mem->base + (offset * 2);
	default:
		WARN(1, "Unknown memory region type");
		return offset;
	}
}

static unsigned int wm_halo_region_to_reg(struct wm_adsp_region const *mem,
					  unsigned int offset)
{
	switch (mem->type) {
	case WMFW_ADSP2_XM:
	case WMFW_ADSP2_YM:
		return mem->base + (offset * 4);
	case WMFW_HALO_XM_PACKED:
	case WMFW_HALO_YM_PACKED:
		return (mem->base + (offset * 3)) & ~0x3;
	case WMFW_HALO_PM_PACKED:
		return mem->base + (offset * 5);
	default:
		WARN(1, "Unknown memory region type");
		return offset;
	}
}

static void wm_adsp_read_fw_status(struct wm_adsp *dsp,
				   int noffs, unsigned int *offs)
{
	unsigned int i;
	int ret;

	for (i = 0; i < noffs; ++i) {
		ret = regmap_read(dsp->regmap, dsp->base + offs[i], &offs[i]);
		if (ret) {
			adsp_err(dsp, "Failed to read SCRATCH%u: %d\n", i, ret);
			return;
		}
	}
}

static void wm_adsp2_show_fw_status(struct wm_adsp *dsp)
{
	unsigned int offs[] = {
		ADSP2_SCRATCH0, ADSP2_SCRATCH1, ADSP2_SCRATCH2, ADSP2_SCRATCH3,
	};

	wm_adsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	adsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		 offs[0], offs[1], offs[2], offs[3]);
}

static void wm_adsp2v2_show_fw_status(struct wm_adsp *dsp)
{
	unsigned int offs[] = { ADSP2V2_SCRATCH0_1, ADSP2V2_SCRATCH2_3 };

	wm_adsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	adsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		 offs[0] & 0xFFFF, offs[0] >> 16,
		 offs[1] & 0xFFFF, offs[1] >> 16);
}

static void wm_halo_show_fw_status(struct wm_adsp *dsp)
{
	unsigned int offs[] = {
		HALO_SCRATCH1, HALO_SCRATCH2, HALO_SCRATCH3, HALO_SCRATCH4,
	};

	wm_adsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	adsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		 offs[0], offs[1], offs[2], offs[3]);
}

static inline struct wm_coeff_ctl *bytes_ext_to_ctl(struct soc_bytes_ext *ext)
{
	return container_of(ext, struct wm_coeff_ctl, bytes_ext);
}

static int wm_coeff_base_reg(struct wm_coeff_ctl *ctl, unsigned int *reg)
{
	const struct wm_adsp_alg_region *alg_region = &ctl->alg_region;
	struct wm_adsp *dsp = ctl->dsp;
	const struct wm_adsp_region *mem;

	mem = wm_adsp_find_region(dsp, alg_region->type);
	if (!mem) {
		adsp_err(dsp, "No base for region %x\n",
			 alg_region->type);
		return -EINVAL;
	}

	*reg = dsp->ops->region_to_reg(mem, ctl->alg_region.base + ctl->offset);

	return 0;
}

static int wm_coeff_info(struct snd_kcontrol *kctl,
			 struct snd_ctl_elem_info *uinfo)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);

	switch (ctl->type) {
	case WMFW_CTL_TYPE_ACKED:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->value.integer.min = WM_ADSP_ACKED_CTL_MIN_VALUE;
		uinfo->value.integer.max = WM_ADSP_ACKED_CTL_MAX_VALUE;
		uinfo->value.integer.step = 1;
		uinfo->count = 1;
		break;
	default:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
		uinfo->count = ctl->len;
		break;
	}

	return 0;
}

static int wm_coeff_write_acked_control(struct wm_coeff_ctl *ctl,
					unsigned int event_id)
{
	struct wm_adsp *dsp = ctl->dsp;
	u32 val = cpu_to_be32(event_id);
	unsigned int reg;
	int i, ret;

	ret = wm_coeff_base_reg(ctl, &reg);
	if (ret)
		return ret;

	adsp_dbg(dsp, "Sending 0x%x to acked control alg 0x%x %s:0x%x\n",
		 event_id, ctl->alg_region.alg,
		 wm_adsp_mem_region_name(ctl->alg_region.type), ctl->offset);

	ret = regmap_raw_write(dsp->regmap, reg, &val, sizeof(val));
	if (ret) {
		adsp_err(dsp, "Failed to write %x: %d\n", reg, ret);
		return ret;
	}

	/*
	 * Poll for ack, we initially poll at ~1ms intervals for firmwares
	 * that respond quickly, then go to ~10ms polls. A firmware is unlikely
	 * to ack instantly so we do the first 1ms delay before reading the
	 * control to avoid a pointless bus transaction
	 */
	for (i = 0; i < WM_ADSP_ACKED_CTL_TIMEOUT_MS;) {
		switch (i) {
		case 0 ... WM_ADSP_ACKED_CTL_N_QUICKPOLLS - 1:
			usleep_range(1000, 2000);
			i++;
			break;
		default:
			usleep_range(10000, 20000);
			i += 10;
			break;
		}

		ret = regmap_raw_read(dsp->regmap, reg, &val, sizeof(val));
		if (ret) {
			adsp_err(dsp, "Failed to read %x: %d\n", reg, ret);
			return ret;
		}

		if (val == 0) {
			adsp_dbg(dsp, "Acked control ACKED at poll %u\n", i);
			return 0;
		}
	}

	adsp_warn(dsp, "Acked control @0x%x alg:0x%x %s:0x%x timed out\n",
		  reg, ctl->alg_region.alg,
		  wm_adsp_mem_region_name(ctl->alg_region.type),
		  ctl->offset);

	return -ETIMEDOUT;
}

static int wm_coeff_write_control(struct wm_coeff_ctl *ctl,
				  const void *buf, size_t len)
{
	struct wm_adsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	ret = wm_coeff_base_reg(ctl, &reg);
	if (ret)
		return ret;

	scratch = kmemdup(buf, len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_write(dsp->regmap, reg, scratch,
			       len);
	if (ret) {
		adsp_err(dsp, "Failed to write %zu bytes to %x: %d\n",
			 len, reg, ret);
		kfree(scratch);
		return ret;
	}
	adsp_dbg(dsp, "Wrote %zu bytes to %x\n", len, reg);

	kfree(scratch);

	return 0;
}

static int wm_coeff_put(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	char *p = ucontrol->value.bytes.data;
	int ret = 0;

	mutex_lock(&ctl->dsp->pwr_lock);

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
		ret = -EPERM;
	else
		memcpy(ctl->cache, p, ctl->len);

	ctl->set = 1;
	if (ctl->enabled && ctl->dsp->running)
		ret = wm_coeff_write_control(ctl, p, ctl->len);

	mutex_unlock(&ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_tlv_put(struct snd_kcontrol *kctl,
			    const unsigned int __user *bytes, unsigned int size)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	int ret = 0;

	mutex_lock(&ctl->dsp->pwr_lock);

	if (copy_from_user(ctl->cache, bytes, size)) {
		ret = -EFAULT;
	} else {
		ctl->set = 1;
		if (ctl->enabled && ctl->dsp->running)
			ret = wm_coeff_write_control(ctl, ctl->cache, size);
		else if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
			ret = -EPERM;
	}

	mutex_unlock(&ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_put_acked(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	unsigned int val = ucontrol->value.integer.value[0];
	int ret;

	if (val == 0)
		return 0;	/* 0 means no event */

	mutex_lock(&ctl->dsp->pwr_lock);

	if (ctl->enabled && ctl->dsp->running)
		ret = wm_coeff_write_acked_control(ctl, val);
	else
		ret = -EPERM;

	mutex_unlock(&ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_read_control(struct wm_coeff_ctl *ctl,
				 void *buf, size_t len)
{
	struct wm_adsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	ret = wm_coeff_base_reg(ctl, &reg);
	if (ret)
		return ret;

	scratch = kmalloc(len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_read(dsp->regmap, reg, scratch, len);
	if (ret) {
		adsp_err(dsp, "Failed to read %zu bytes from %x: %d\n",
			 len, reg, ret);
		kfree(scratch);
		return ret;
	}
	adsp_dbg(dsp, "Read %zu bytes from %x\n", len, reg);

	memcpy(buf, scratch, len);
	kfree(scratch);

	return 0;
}

static int wm_coeff_get(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	char *p = ucontrol->value.bytes.data;
	int ret = 0;

	mutex_lock(&ctl->dsp->pwr_lock);

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE) {
		if (ctl->enabled && ctl->dsp->running)
			ret = wm_coeff_read_control(ctl, p, ctl->len);
		else
			ret = -EPERM;
	} else {
		if (!ctl->flags && ctl->enabled && ctl->dsp->running)
			ret = wm_coeff_read_control(ctl, ctl->cache, ctl->len);

		memcpy(p, ctl->cache, ctl->len);
	}

	mutex_unlock(&ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_tlv_get(struct snd_kcontrol *kctl,
			    unsigned int __user *bytes, unsigned int size)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *)kctl->private_value;
	struct wm_coeff_ctl *ctl = bytes_ext_to_ctl(bytes_ext);
	int ret = 0;

	mutex_lock(&ctl->dsp->pwr_lock);

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE) {
		if (ctl->enabled && ctl->dsp->running)
			ret = wm_coeff_read_control(ctl, ctl->cache, size);
		else
			ret = -EPERM;
	} else {
		if (!ctl->flags && ctl->enabled && ctl->dsp->running)
			ret = wm_coeff_read_control(ctl, ctl->cache, size);
	}

	if (!ret && copy_to_user(bytes, ctl->cache, size))
		ret = -EFAULT;

	mutex_unlock(&ctl->dsp->pwr_lock);

	return ret;
}

static int wm_coeff_get_acked(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	/*
	 * Although it's not useful to read an acked control, we must satisfy
	 * user-side assumptions that all controls are readable and that a
	 * write of the same value should be filtered out (it's valid to send
	 * the same event number again to the firmware). We therefore return 0,
	 * meaning "no event" so valid event numbers will always be a change
	 */
	ucontrol->value.integer.value[0] = 0;

	return 0;
}

struct wmfw_ctl_work {
	struct wm_adsp *dsp;
	struct wm_coeff_ctl *ctl;
	struct work_struct work;
};

static unsigned int wmfw_convert_flags(unsigned int in, unsigned int len)
{
	unsigned int out, rd, wr, vol;

	if (len > ADSP_MAX_STD_CTRL_SIZE) {
		rd = SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		wr = SNDRV_CTL_ELEM_ACCESS_TLV_WRITE;
		vol = SNDRV_CTL_ELEM_ACCESS_VOLATILE;

		out = SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	} else {
		rd = SNDRV_CTL_ELEM_ACCESS_READ;
		wr = SNDRV_CTL_ELEM_ACCESS_WRITE;
		vol = SNDRV_CTL_ELEM_ACCESS_VOLATILE;

		out = 0;
	}

	if (in) {
		if (in & WMFW_CTL_FLAG_READABLE)
			out |= rd;
		if (in & WMFW_CTL_FLAG_WRITEABLE)
			out |= wr;
		if (in & WMFW_CTL_FLAG_VOLATILE)
			out |= vol;
	} else {
		out |= rd | wr | vol;
	}

	return out;
}

static int wmfw_add_ctl(struct wm_adsp *dsp, struct wm_coeff_ctl *ctl)
{
	struct snd_kcontrol_new *kcontrol;
	int ret;

	if (!ctl || !ctl->name)
		return -EINVAL;

	kcontrol = kzalloc(sizeof(*kcontrol), GFP_KERNEL);
	if (!kcontrol)
		return -ENOMEM;

	kcontrol->name = ctl->name;
	kcontrol->info = wm_coeff_info;
	kcontrol->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kcontrol->tlv.c = snd_soc_bytes_tlv_callback;
	kcontrol->private_value = (unsigned long)&ctl->bytes_ext;
	kcontrol->access = wmfw_convert_flags(ctl->flags, ctl->len);

	switch (ctl->type) {
	case WMFW_CTL_TYPE_ACKED:
		kcontrol->get = wm_coeff_get_acked;
		kcontrol->put = wm_coeff_put_acked;
		break;
	default:
		if (kcontrol->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
			ctl->bytes_ext.max = ctl->len;
			ctl->bytes_ext.get = wm_coeff_tlv_get;
			ctl->bytes_ext.put = wm_coeff_tlv_put;
		} else {
			kcontrol->get = wm_coeff_get;
			kcontrol->put = wm_coeff_put;
		}
		break;
	}

	ret = snd_soc_add_component_controls(dsp->component, kcontrol, 1);
	if (ret < 0)
		goto err_kcontrol;

	kfree(kcontrol);

	return 0;

err_kcontrol:
	kfree(kcontrol);
	return ret;
}

static int wm_coeff_init_control_caches(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled || ctl->set)
			continue;
		if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
			continue;

		/*
		 * For readable controls populate the cache from the DSP memory.
		 * For non-readable controls the cache was zero-filled when
		 * created so we don't need to do anything.
		 */
		if (!ctl->flags || (ctl->flags & WMFW_CTL_FLAG_READABLE)) {
			ret = wm_coeff_read_control(ctl, ctl->cache, ctl->len);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int wm_coeff_sync_controls(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled)
			continue;
		if (ctl->set && !(ctl->flags & WMFW_CTL_FLAG_VOLATILE)) {
			ret = wm_coeff_write_control(ctl, ctl->cache, ctl->len);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void wm_adsp_signal_event_controls(struct wm_adsp *dsp,
					  unsigned int event)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->type != WMFW_CTL_TYPE_HOSTEVENT)
			continue;

		if (!ctl->enabled)
			continue;

		ret = wm_coeff_write_acked_control(ctl, event);
		if (ret)
			adsp_warn(dsp,
				  "Failed to send 0x%x event to alg 0x%x (%d)\n",
				  event, ctl->alg_region.alg, ret);
	}
}

static void wm_adsp_ctl_work(struct work_struct *work)
{
	struct wmfw_ctl_work *ctl_work = container_of(work,
						      struct wmfw_ctl_work,
						      work);

	wmfw_add_ctl(ctl_work->dsp, ctl_work->ctl);
	kfree(ctl_work);
}

static void wm_adsp_free_ctl_blk(struct wm_coeff_ctl *ctl)
{
	kfree(ctl->cache);
	kfree(ctl->name);
	kfree(ctl);
}

static int wm_adsp_create_control(struct wm_adsp *dsp,
				  const struct wm_adsp_alg_region *alg_region,
				  unsigned int offset, unsigned int len,
				  const char *subname, unsigned int subname_len,
				  unsigned int flags, unsigned int type)
{
	struct wm_coeff_ctl *ctl;
	struct wmfw_ctl_work *ctl_work;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const char *region_name;
	int ret;

	region_name = wm_adsp_mem_region_name(alg_region->type);
	if (!region_name) {
		adsp_err(dsp, "Unknown region type: %d\n", alg_region->type);
		return -EINVAL;
	}

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s %s %x",
			 dsp->name, region_name, alg_region->alg);
		subname = NULL; /* don't append subname */
		break;
	case 2:
		ret = snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"%s%c %.12s %x", dsp->name, *region_name,
				wm_adsp_fw_text[dsp->fw], alg_region->alg);
		break;
	default:
		ret = snprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
				"%s %.12s %x", dsp->name,
				wm_adsp_fw_text[dsp->fw], alg_region->alg);
		break;
	}

	if (subname) {
		int avail = SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret - 2;
		int skip = 0;

		if (dsp->component->name_prefix)
			avail -= strlen(dsp->component->name_prefix) + 1;

		/* Truncate the subname from the start if it is too long */
		if (subname_len > avail)
			skip = subname_len - avail;

		snprintf(name + ret, SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret,
			 " %.*s", subname_len - skip, subname + skip);
	}

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!strcmp(ctl->name, name)) {
			if (!ctl->enabled)
				ctl->enabled = 1;
			return 0;
		}
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;
	ctl->fw_name = wm_adsp_fw_text[dsp->fw];
	ctl->alg_region = *alg_region;
	ctl->name = kmemdup(name, strlen(name) + 1, GFP_KERNEL);
	if (!ctl->name) {
		ret = -ENOMEM;
		goto err_ctl;
	}
	ctl->enabled = 1;
	ctl->set = 0;
	ctl->ops.xget = wm_coeff_get;
	ctl->ops.xput = wm_coeff_put;
	ctl->dsp = dsp;

	ctl->flags = flags;
	ctl->type = type;
	ctl->offset = offset;
	ctl->len = len;
	ctl->cache = kzalloc(ctl->len, GFP_KERNEL);
	if (!ctl->cache) {
		ret = -ENOMEM;
		goto err_ctl_name;
	}

	list_add(&ctl->list, &dsp->ctl_list);

	if (flags & WMFW_CTL_FLAG_SYS)
		return 0;

	ctl_work = kzalloc(sizeof(*ctl_work), GFP_KERNEL);
	if (!ctl_work) {
		ret = -ENOMEM;
		goto err_ctl_cache;
	}

	ctl_work->dsp = dsp;
	ctl_work->ctl = ctl;
	INIT_WORK(&ctl_work->work, wm_adsp_ctl_work);
	schedule_work(&ctl_work->work);

	return 0;

err_ctl_cache:
	kfree(ctl->cache);
err_ctl_name:
	kfree(ctl->name);
err_ctl:
	kfree(ctl);

	return ret;
}

struct wm_coeff_parsed_alg {
	int id;
	const u8 *name;
	int name_len;
	int ncoeff;
};

struct wm_coeff_parsed_coeff {
	int offset;
	int mem_type;
	const u8 *name;
	int name_len;
	int ctl_type;
	int flags;
	int len;
};

static int wm_coeff_parse_string(int bytes, const u8 **pos, const u8 **str)
{
	int length;

	switch (bytes) {
	case 1:
		length = **pos;
		break;
	case 2:
		length = le16_to_cpu(*((__le16 *)*pos));
		break;
	default:
		return 0;
	}

	if (str)
		*str = *pos + bytes;

	*pos += ((length + bytes) + 3) & ~0x03;

	return length;
}

static int wm_coeff_parse_int(int bytes, const u8 **pos)
{
	int val = 0;

	switch (bytes) {
	case 2:
		val = le16_to_cpu(*((__le16 *)*pos));
		break;
	case 4:
		val = le32_to_cpu(*((__le32 *)*pos));
		break;
	default:
		break;
	}

	*pos += bytes;

	return val;
}

static inline void wm_coeff_parse_alg(struct wm_adsp *dsp, const u8 **data,
				      struct wm_coeff_parsed_alg *blk)
{
	const struct wmfw_adsp_alg_data *raw;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_alg_data *)*data;
		*data = raw->data;

		blk->id = le32_to_cpu(raw->id);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ncoeff = le32_to_cpu(raw->ncoeff);
		break;
	default:
		blk->id = wm_coeff_parse_int(sizeof(raw->id), data);
		blk->name_len = wm_coeff_parse_string(sizeof(u8), data,
						      &blk->name);
		wm_coeff_parse_string(sizeof(u16), data, NULL);
		blk->ncoeff = wm_coeff_parse_int(sizeof(raw->ncoeff), data);
		break;
	}

	adsp_dbg(dsp, "Algorithm ID: %#x\n", blk->id);
	adsp_dbg(dsp, "Algorithm name: %.*s\n", blk->name_len, blk->name);
	adsp_dbg(dsp, "# of coefficient descriptors: %#x\n", blk->ncoeff);
}

static inline void wm_coeff_parse_coeff(struct wm_adsp *dsp, const u8 **data,
					struct wm_coeff_parsed_coeff *blk)
{
	const struct wmfw_adsp_coeff_data *raw;
	const u8 *tmp;
	int length;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_coeff_data *)*data;
		*data = *data + sizeof(raw->hdr) + le32_to_cpu(raw->hdr.size);

		blk->offset = le16_to_cpu(raw->hdr.offset);
		blk->mem_type = le16_to_cpu(raw->hdr.type);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ctl_type = le16_to_cpu(raw->ctl_type);
		blk->flags = le16_to_cpu(raw->flags);
		blk->len = le32_to_cpu(raw->len);
		break;
	default:
		tmp = *data;
		blk->offset = wm_coeff_parse_int(sizeof(raw->hdr.offset), &tmp);
		blk->mem_type = wm_coeff_parse_int(sizeof(raw->hdr.type), &tmp);
		length = wm_coeff_parse_int(sizeof(raw->hdr.size), &tmp);
		blk->name_len = wm_coeff_parse_string(sizeof(u8), &tmp,
						      &blk->name);
		wm_coeff_parse_string(sizeof(u8), &tmp, NULL);
		wm_coeff_parse_string(sizeof(u16), &tmp, NULL);
		blk->ctl_type = wm_coeff_parse_int(sizeof(raw->ctl_type), &tmp);
		blk->flags = wm_coeff_parse_int(sizeof(raw->flags), &tmp);
		blk->len = wm_coeff_parse_int(sizeof(raw->len), &tmp);

		*data = *data + sizeof(raw->hdr) + length;
		break;
	}

	adsp_dbg(dsp, "\tCoefficient type: %#x\n", blk->mem_type);
	adsp_dbg(dsp, "\tCoefficient offset: %#x\n", blk->offset);
	adsp_dbg(dsp, "\tCoefficient name: %.*s\n", blk->name_len, blk->name);
	adsp_dbg(dsp, "\tCoefficient flags: %#x\n", blk->flags);
	adsp_dbg(dsp, "\tALSA control type: %#x\n", blk->ctl_type);
	adsp_dbg(dsp, "\tALSA control len: %#x\n", blk->len);
}

static int wm_adsp_check_coeff_flags(struct wm_adsp *dsp,
				const struct wm_coeff_parsed_coeff *coeff_blk,
				unsigned int f_required,
				unsigned int f_illegal)
{
	if ((coeff_blk->flags & f_illegal) ||
	    ((coeff_blk->flags & f_required) != f_required)) {
		adsp_err(dsp, "Illegal flags 0x%x for control type 0x%x\n",
			 coeff_blk->flags, coeff_blk->ctl_type);
		return -EINVAL;
	}

	return 0;
}

static int wm_adsp_parse_coeff(struct wm_adsp *dsp,
			       const struct wmfw_region *region)
{
	struct wm_adsp_alg_region alg_region = {};
	struct wm_coeff_parsed_alg alg_blk;
	struct wm_coeff_parsed_coeff coeff_blk;
	const u8 *data = region->data;
	int i, ret;

	wm_coeff_parse_alg(dsp, &data, &alg_blk);
	for (i = 0; i < alg_blk.ncoeff; i++) {
		wm_coeff_parse_coeff(dsp, &data, &coeff_blk);

		switch (coeff_blk.ctl_type) {
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			break;
		case WMFW_CTL_TYPE_ACKED:
			if (coeff_blk.flags & WMFW_CTL_FLAG_SYS)
				continue;	/* ignore */

			ret = wm_adsp_check_coeff_flags(dsp, &coeff_blk,
						WMFW_CTL_FLAG_VOLATILE |
						WMFW_CTL_FLAG_WRITEABLE |
						WMFW_CTL_FLAG_READABLE,
						0);
			if (ret)
				return -EINVAL;
			break;
		case WMFW_CTL_TYPE_HOSTEVENT:
			ret = wm_adsp_check_coeff_flags(dsp, &coeff_blk,
						WMFW_CTL_FLAG_SYS |
						WMFW_CTL_FLAG_VOLATILE |
						WMFW_CTL_FLAG_WRITEABLE |
						WMFW_CTL_FLAG_READABLE,
						0);
			if (ret)
				return -EINVAL;
			break;
		case WMFW_CTL_TYPE_HOST_BUFFER:
			ret = wm_adsp_check_coeff_flags(dsp, &coeff_blk,
						WMFW_CTL_FLAG_SYS |
						WMFW_CTL_FLAG_VOLATILE |
						WMFW_CTL_FLAG_READABLE,
						0);
			if (ret)
				return -EINVAL;
			break;
		default:
			adsp_err(dsp, "Unknown control type: %d\n",
				 coeff_blk.ctl_type);
			return -EINVAL;
		}

		alg_region.type = coeff_blk.mem_type;
		alg_region.alg = alg_blk.id;

		ret = wm_adsp_create_control(dsp, &alg_region,
					     coeff_blk.offset,
					     coeff_blk.len,
					     coeff_blk.name,
					     coeff_blk.name_len,
					     coeff_blk.flags,
					     coeff_blk.ctl_type);
		if (ret < 0)
			adsp_err(dsp, "Failed to create control: %.*s, %d\n",
				 coeff_blk.name_len, coeff_blk.name, ret);
	}

	return 0;
}

static unsigned int wm_adsp1_parse_sizes(struct wm_adsp *dsp,
					 const char * const file,
					 unsigned int pos,
					 const struct firmware *firmware)
{
	const struct wmfw_adsp1_sizes *adsp1_sizes;

	adsp1_sizes = (void *)&firmware->data[pos];

	adsp_dbg(dsp, "%s: %d DM, %d PM, %d ZM\n", file,
		 le32_to_cpu(adsp1_sizes->dm), le32_to_cpu(adsp1_sizes->pm),
		 le32_to_cpu(adsp1_sizes->zm));

	return pos + sizeof(*adsp1_sizes);
}

static unsigned int wm_adsp2_parse_sizes(struct wm_adsp *dsp,
					 const char * const file,
					 unsigned int pos,
					 const struct firmware *firmware)
{
	const struct wmfw_adsp2_sizes *adsp2_sizes;

	adsp2_sizes = (void *)&firmware->data[pos];

	adsp_dbg(dsp, "%s: %d XM, %d YM %d PM, %d ZM\n", file,
		 le32_to_cpu(adsp2_sizes->xm), le32_to_cpu(adsp2_sizes->ym),
		 le32_to_cpu(adsp2_sizes->pm), le32_to_cpu(adsp2_sizes->zm));

	return pos + sizeof(*adsp2_sizes);
}

static bool wm_adsp_validate_version(struct wm_adsp *dsp, unsigned int version)
{
	switch (version) {
	case 0:
		adsp_warn(dsp, "Deprecated file format %d\n", version);
		return true;
	case 1:
	case 2:
		return true;
	default:
		return false;
	}
}

static bool wm_halo_validate_version(struct wm_adsp *dsp, unsigned int version)
{
	switch (version) {
	case 3:
		return true;
	default:
		return false;
	}
}

static int wm_adsp_load(struct wm_adsp *dsp)
{
	LIST_HEAD(buf_list);
	const struct firmware *firmware;
	struct regmap *regmap = dsp->regmap;
	unsigned int pos = 0;
	const struct wmfw_header *header;
	const struct wmfw_adsp1_sizes *adsp1_sizes;
	const struct wmfw_footer *footer;
	const struct wmfw_region *region;
	const struct wm_adsp_region *mem;
	const char *region_name;
	char *file, *text = NULL;
	struct wm_adsp_buf *buf;
	unsigned int reg;
	int regions = 0;
	int ret, offset, type;

	file = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (file == NULL)
		return -ENOMEM;

	snprintf(file, PAGE_SIZE, "%s-%s-%s.wmfw", dsp->part, dsp->fwf_name,
		 wm_adsp_fw[dsp->fw].file);
	file[PAGE_SIZE - 1] = '\0';

	ret = request_firmware(&firmware, file, dsp->dev);
	if (ret != 0) {
		adsp_err(dsp, "Failed to request '%s'\n", file);
		goto out;
	}
	ret = -EINVAL;

	pos = sizeof(*header) + sizeof(*adsp1_sizes) + sizeof(*footer);
	if (pos >= firmware->size) {
		adsp_err(dsp, "%s: file too short, %zu bytes\n",
			 file, firmware->size);
		goto out_fw;
	}

	header = (void *)&firmware->data[0];

	if (memcmp(&header->magic[0], "WMFW", 4) != 0) {
		adsp_err(dsp, "%s: invalid magic\n", file);
		goto out_fw;
	}

	if (!dsp->ops->validate_version(dsp, header->ver)) {
		adsp_err(dsp, "%s: unknown file format %d\n",
			 file, header->ver);
		goto out_fw;
	}

	adsp_info(dsp, "Firmware version: %d\n", header->ver);
	dsp->fw_ver = header->ver;

	if (header->core != dsp->type) {
		adsp_err(dsp, "%s: invalid core %d != %d\n",
			 file, header->core, dsp->type);
		goto out_fw;
	}

	pos = sizeof(*header);
	pos = dsp->ops->parse_sizes(dsp, file, pos, firmware);

	footer = (void *)&firmware->data[pos];
	pos += sizeof(*footer);

	if (le32_to_cpu(header->len) != pos) {
		adsp_err(dsp, "%s: unexpected header length %d\n",
			 file, le32_to_cpu(header->len));
		goto out_fw;
	}

	adsp_dbg(dsp, "%s: timestamp %llu\n", file,
		 le64_to_cpu(footer->timestamp));

	while (pos < firmware->size &&
	       sizeof(*region) < firmware->size - pos) {
		region = (void *)&(firmware->data[pos]);
		region_name = "Unknown";
		reg = 0;
		text = NULL;
		offset = le32_to_cpu(region->offset) & 0xffffff;
		type = be32_to_cpu(region->type) & 0xff;

		switch (type) {
		case WMFW_NAME_TEXT:
			region_name = "Firmware name";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ALGORITHM_DATA:
			region_name = "Algorithm";
			ret = wm_adsp_parse_coeff(dsp, region);
			if (ret != 0)
				goto out_fw;
			break;
		case WMFW_INFO_TEXT:
			region_name = "Information";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ABSOLUTE:
			region_name = "Absolute";
			reg = offset;
			break;
		case WMFW_ADSP1_PM:
		case WMFW_ADSP1_DM:
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_ADSP1_ZM:
		case WMFW_HALO_PM_PACKED:
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
			mem = wm_adsp_find_region(dsp, type);
			if (!mem) {
				adsp_err(dsp, "No region of type: %x\n", type);
				goto out_fw;
			}

			region_name = wm_adsp_mem_region_name(type);
			reg = dsp->ops->region_to_reg(mem, offset);
			break;
		default:
			adsp_warn(dsp,
				  "%s.%d: Unknown region type %x at %d(%x)\n",
				  file, regions, type, pos, pos);
			break;
		}

		adsp_dbg(dsp, "%s.%d: %d bytes at %d in %s\n", file,
			 regions, le32_to_cpu(region->len), offset,
			 region_name);

		if (le32_to_cpu(region->len) >
		    firmware->size - pos - sizeof(*region)) {
			adsp_err(dsp,
				 "%s.%d: %s region len %d bytes exceeds file length %zu\n",
				 file, regions, region_name,
				 le32_to_cpu(region->len), firmware->size);
			ret = -EINVAL;
			goto out_fw;
		}

		if (text) {
			memcpy(text, region->data, le32_to_cpu(region->len));
			adsp_info(dsp, "%s: %s\n", file, text);
			kfree(text);
			text = NULL;
		}

		if (reg) {
			buf = wm_adsp_buf_alloc(region->data,
						le32_to_cpu(region->len),
						&buf_list);
			if (!buf) {
				adsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(region->len));
			if (ret != 0) {
				adsp_err(dsp,
					"%s.%d: Failed to write %d bytes at %d in %s: %d\n",
					file, regions,
					le32_to_cpu(region->len), offset,
					region_name, ret);
				goto out_fw;
			}
		}

		pos += le32_to_cpu(region->len) + sizeof(*region);
		regions++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0) {
		adsp_err(dsp, "Failed to complete async write: %d\n", ret);
		goto out_fw;
	}

	if (pos > firmware->size)
		adsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			  file, regions, pos - firmware->size);

	wm_adsp_debugfs_save_wmfwname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	wm_adsp_buf_free(&buf_list);
	release_firmware(firmware);
	kfree(text);
out:
	kfree(file);

	return ret;
}

static void wm_adsp_ctl_fixup_base(struct wm_adsp *dsp,
				  const struct wm_adsp_alg_region *alg_region)
{
	struct wm_coeff_ctl *ctl;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->fw_name == wm_adsp_fw_text[dsp->fw] &&
		    alg_region->alg == ctl->alg_region.alg &&
		    alg_region->type == ctl->alg_region.type) {
			ctl->alg_region.base = alg_region->base;
		}
	}
}

static void *wm_adsp_read_algs(struct wm_adsp *dsp, size_t n_algs,
			       const struct wm_adsp_region *mem,
			       unsigned int pos, unsigned int len)
{
	void *alg;
	unsigned int reg;
	int ret;
	__be32 val;

	if (n_algs == 0) {
		adsp_err(dsp, "No algorithms\n");
		return ERR_PTR(-EINVAL);
	}

	if (n_algs > 1024) {
		adsp_err(dsp, "Algorithm count %zx excessive\n", n_algs);
		return ERR_PTR(-EINVAL);
	}

	/* Read the terminator first to validate the length */
	reg = dsp->ops->region_to_reg(mem, pos + len);

	ret = regmap_raw_read(dsp->regmap, reg, &val, sizeof(val));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm list end: %d\n",
			ret);
		return ERR_PTR(ret);
	}

	if (be32_to_cpu(val) != 0xbedead)
		adsp_warn(dsp, "Algorithm list end %x 0x%x != 0xbedead\n",
			  reg, be32_to_cpu(val));

	/* Convert length from DSP words to bytes */
	len *= sizeof(u32);

	alg = kzalloc(len, GFP_KERNEL | GFP_DMA);
	if (!alg)
		return ERR_PTR(-ENOMEM);

	reg = dsp->ops->region_to_reg(mem, pos);

	ret = regmap_raw_read(dsp->regmap, reg, alg, len);
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm list: %d\n", ret);
		kfree(alg);
		return ERR_PTR(ret);
	}

	return alg;
}

static struct wm_adsp_alg_region *
	wm_adsp_find_alg_region(struct wm_adsp *dsp, int type, unsigned int id)
{
	struct wm_adsp_alg_region *alg_region;

	list_for_each_entry(alg_region, &dsp->alg_regions, list) {
		if (id == alg_region->alg && type == alg_region->type)
			return alg_region;
	}

	return NULL;
}

static struct wm_adsp_alg_region *wm_adsp_create_region(struct wm_adsp *dsp,
							int type, __be32 id,
							__be32 base)
{
	struct wm_adsp_alg_region *alg_region;

	alg_region = kzalloc(sizeof(*alg_region), GFP_KERNEL);
	if (!alg_region)
		return ERR_PTR(-ENOMEM);

	alg_region->type = type;
	alg_region->alg = be32_to_cpu(id);
	alg_region->base = be32_to_cpu(base);

	list_add_tail(&alg_region->list, &dsp->alg_regions);

	if (dsp->fw_ver > 0)
		wm_adsp_ctl_fixup_base(dsp, alg_region);

	return alg_region;
}

static void wm_adsp_free_alg_regions(struct wm_adsp *dsp)
{
	struct wm_adsp_alg_region *alg_region;

	while (!list_empty(&dsp->alg_regions)) {
		alg_region = list_first_entry(&dsp->alg_regions,
					      struct wm_adsp_alg_region,
					      list);
		list_del(&alg_region->list);
		kfree(alg_region);
	}
}

static void wmfw_parse_id_header(struct wm_adsp *dsp,
				 struct wmfw_id_hdr *fw, int nalgs)
{
	dsp->fw_id = be32_to_cpu(fw->id);
	dsp->fw_id_version = be32_to_cpu(fw->ver);

	adsp_info(dsp, "Firmware: %x v%d.%d.%d, %d algorithms\n",
		  dsp->fw_id, (dsp->fw_id_version & 0xff0000) >> 16,
		  (dsp->fw_id_version & 0xff00) >> 8, dsp->fw_id_version & 0xff,
		  nalgs);
}

static void wmfw_v3_parse_id_header(struct wm_adsp *dsp,
				    struct wmfw_v3_id_hdr *fw, int nalgs)
{
	dsp->fw_id = be32_to_cpu(fw->id);
	dsp->fw_id_version = be32_to_cpu(fw->ver);
	dsp->fw_vendor_id = be32_to_cpu(fw->vendor_id);

	adsp_info(dsp, "Firmware: %x vendor: 0x%x v%d.%d.%d, %d algorithms\n",
		  dsp->fw_id, dsp->fw_vendor_id,
		  (dsp->fw_id_version & 0xff0000) >> 16,
		  (dsp->fw_id_version & 0xff00) >> 8, dsp->fw_id_version & 0xff,
		  nalgs);
}

static int wm_adsp_create_regions(struct wm_adsp *dsp, __be32 id, int nregions,
				int *type, __be32 *base)
{
	struct wm_adsp_alg_region *alg_region;
	int i;

	for (i = 0; i < nregions; i++) {
		alg_region = wm_adsp_create_region(dsp, type[i], id, base[i]);
		if (IS_ERR(alg_region))
			return PTR_ERR(alg_region);
	}

	return 0;
}

static int wm_adsp1_setup_algs(struct wm_adsp *dsp)
{
	struct wmfw_adsp1_id_hdr adsp1_id;
	struct wmfw_adsp1_alg_hdr *adsp1_alg;
	struct wm_adsp_alg_region *alg_region;
	const struct wm_adsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = wm_adsp_find_region(dsp, WMFW_ADSP1_DM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp1_id,
			      sizeof(adsp1_id));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm info: %d\n",
			 ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp1_id.n_algs);

	wmfw_parse_id_header(dsp, &adsp1_id.fw, n_algs);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_ZM,
					   adsp1_id.fw.id, adsp1_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_DM,
					   adsp1_id.fw.id, adsp1_id.dm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	/* Calculate offset and length in DSP words */
	pos = sizeof(adsp1_id) / sizeof(u32);
	len = (sizeof(*adsp1_alg) * n_algs) / sizeof(u32);

	adsp1_alg = wm_adsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(adsp1_alg))
		return PTR_ERR(adsp1_alg);

	for (i = 0; i < n_algs; i++) {
		adsp_info(dsp, "%d: ID %x v%d.%d.%d DM@%x ZM@%x\n",
			  i, be32_to_cpu(adsp1_alg[i].alg.id),
			  (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff0000) >> 16,
			  (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff00) >> 8,
			  be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff,
			  be32_to_cpu(adsp1_alg[i].dm),
			  be32_to_cpu(adsp1_alg[i].zm));

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_DM,
						   adsp1_alg[i].alg.id,
						   adsp1_alg[i].dm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].dm);
				len -= be32_to_cpu(adsp1_alg[i].dm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						     len, NULL, 0, 0,
						     SNDRV_CTL_ELEM_TYPE_BYTES);
			} else {
				adsp_warn(dsp, "Missing length info for region DM with ID %x\n",
					  be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP1_ZM,
						   adsp1_alg[i].alg.id,
						   adsp1_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].zm);
				len -= be32_to_cpu(adsp1_alg[i].zm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						     len, NULL, 0, 0,
						     SNDRV_CTL_ELEM_TYPE_BYTES);
			} else {
				adsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					  be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp1_alg);
	return ret;
}

static int wm_adsp2_setup_algs(struct wm_adsp *dsp)
{
	struct wmfw_adsp2_id_hdr adsp2_id;
	struct wmfw_adsp2_alg_hdr *adsp2_alg;
	struct wm_adsp_alg_region *alg_region;
	const struct wm_adsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = wm_adsp_find_region(dsp, WMFW_ADSP2_XM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp2_id,
			      sizeof(adsp2_id));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm info: %d\n",
			 ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp2_id.n_algs);

	wmfw_parse_id_header(dsp, &adsp2_id.fw, n_algs);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_XM,
					   adsp2_id.fw.id, adsp2_id.xm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_YM,
					   adsp2_id.fw.id, adsp2_id.ym);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_ZM,
					   adsp2_id.fw.id, adsp2_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	/* Calculate offset and length in DSP words */
	pos = sizeof(adsp2_id) / sizeof(u32);
	len = (sizeof(*adsp2_alg) * n_algs) / sizeof(u32);

	adsp2_alg = wm_adsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(adsp2_alg))
		return PTR_ERR(adsp2_alg);

	for (i = 0; i < n_algs; i++) {
		adsp_info(dsp,
			  "%d: ID %x v%d.%d.%d XM@%x YM@%x ZM@%x\n",
			  i, be32_to_cpu(adsp2_alg[i].alg.id),
			  (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff0000) >> 16,
			  (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff00) >> 8,
			  be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff,
			  be32_to_cpu(adsp2_alg[i].xm),
			  be32_to_cpu(adsp2_alg[i].ym),
			  be32_to_cpu(adsp2_alg[i].zm));

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_XM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].xm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].xm);
				len -= be32_to_cpu(adsp2_alg[i].xm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						     len, NULL, 0, 0,
						     SNDRV_CTL_ELEM_TYPE_BYTES);
			} else {
				adsp_warn(dsp, "Missing length info for region XM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_YM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].ym);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].ym);
				len -= be32_to_cpu(adsp2_alg[i].ym);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						     len, NULL, 0, 0,
						     SNDRV_CTL_ELEM_TYPE_BYTES);
			} else {
				adsp_warn(dsp, "Missing length info for region YM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = wm_adsp_create_region(dsp, WMFW_ADSP2_ZM,
						   adsp2_alg[i].alg.id,
						   adsp2_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].zm);
				len -= be32_to_cpu(adsp2_alg[i].zm);
				len *= 4;
				wm_adsp_create_control(dsp, alg_region, 0,
						     len, NULL, 0, 0,
						     SNDRV_CTL_ELEM_TYPE_BYTES);
			} else {
				adsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					  be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp2_alg);
	return ret;
}

static int wm_halo_create_regions(struct wm_adsp *dsp, __be32 id,
				  __be32 xm_base, __be32 ym_base)
{
	int types[] = {
		WMFW_ADSP2_XM, WMFW_HALO_XM_PACKED,
		WMFW_ADSP2_YM, WMFW_HALO_YM_PACKED
	};
	__be32 bases[] = { xm_base, xm_base, ym_base, ym_base };

	return wm_adsp_create_regions(dsp, id, ARRAY_SIZE(types), types, bases);
}

static int wm_halo_setup_algs(struct wm_adsp *dsp)
{
	struct wmfw_halo_id_hdr halo_id;
	struct wmfw_halo_alg_hdr *halo_alg;
	const struct wm_adsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = wm_adsp_find_region(dsp, WMFW_ADSP2_XM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &halo_id,
			      sizeof(halo_id));
	if (ret != 0) {
		adsp_err(dsp, "Failed to read algorithm info: %d\n",
			 ret);
		return ret;
	}

	n_algs = be32_to_cpu(halo_id.n_algs);

	wmfw_v3_parse_id_header(dsp, &halo_id.fw, n_algs);

	ret = wm_halo_create_regions(dsp, halo_id.fw.id,
				     halo_id.xm_base, halo_id.ym_base);
	if (ret)
		return ret;

	/* Calculate offset and length in DSP words */
	pos = sizeof(halo_id) / sizeof(u32);
	len = (sizeof(*halo_alg) * n_algs) / sizeof(u32);

	halo_alg = wm_adsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(halo_alg))
		return PTR_ERR(halo_alg);

	for (i = 0; i < n_algs; i++) {
		adsp_info(dsp,
			  "%d: ID %x v%d.%d.%d XM@%x YM@%x\n",
			  i, be32_to_cpu(halo_alg[i].alg.id),
			  (be32_to_cpu(halo_alg[i].alg.ver) & 0xff0000) >> 16,
			  (be32_to_cpu(halo_alg[i].alg.ver) & 0xff00) >> 8,
			  be32_to_cpu(halo_alg[i].alg.ver) & 0xff,
			  be32_to_cpu(halo_alg[i].xm_base),
			  be32_to_cpu(halo_alg[i].ym_base));

		ret = wm_halo_create_regions(dsp, halo_alg[i].alg.id,
					     halo_alg[i].xm_base,
					     halo_alg[i].ym_base);
		if (ret)
			goto out;
	}

out:
	kfree(halo_alg);
	return ret;
}

static int wm_adsp_load_coeff(struct wm_adsp *dsp)
{
	LIST_HEAD(buf_list);
	struct regmap *regmap = dsp->regmap;
	struct wmfw_coeff_hdr *hdr;
	struct wmfw_coeff_item *blk;
	const struct firmware *firmware;
	const struct wm_adsp_region *mem;
	struct wm_adsp_alg_region *alg_region;
	const char *region_name;
	int ret, pos, blocks, type, offset, reg;
	char *file;
	struct wm_adsp_buf *buf;

	file = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (file == NULL)
		return -ENOMEM;

	snprintf(file, PAGE_SIZE, "%s-%s-%s.bin", dsp->part, dsp->fwf_name,
		 wm_adsp_fw[dsp->fw].file);
	file[PAGE_SIZE - 1] = '\0';

	ret = request_firmware(&firmware, file, dsp->dev);
	if (ret != 0) {
		adsp_warn(dsp, "Failed to request '%s'\n", file);
		ret = 0;
		goto out;
	}
	ret = -EINVAL;

	if (sizeof(*hdr) >= firmware->size) {
		adsp_err(dsp, "%s: file too short, %zu bytes\n",
			file, firmware->size);
		goto out_fw;
	}

	hdr = (void *)&firmware->data[0];
	if (memcmp(hdr->magic, "WMDR", 4) != 0) {
		adsp_err(dsp, "%s: invalid magic\n", file);
		goto out_fw;
	}

	switch (be32_to_cpu(hdr->rev) & 0xff) {
	case 1:
		break;
	default:
		adsp_err(dsp, "%s: Unsupported coefficient file format %d\n",
			 file, be32_to_cpu(hdr->rev) & 0xff);
		ret = -EINVAL;
		goto out_fw;
	}

	adsp_dbg(dsp, "%s: v%d.%d.%d\n", file,
		(le32_to_cpu(hdr->ver) >> 16) & 0xff,
		(le32_to_cpu(hdr->ver) >>  8) & 0xff,
		le32_to_cpu(hdr->ver) & 0xff);

	pos = le32_to_cpu(hdr->len);

	blocks = 0;
	while (pos < firmware->size &&
	       sizeof(*blk) < firmware->size - pos) {
		blk = (void *)(&firmware->data[pos]);

		type = le16_to_cpu(blk->type);
		offset = le16_to_cpu(blk->offset);

		adsp_dbg(dsp, "%s.%d: %x v%d.%d.%d\n",
			 file, blocks, le32_to_cpu(blk->id),
			 (le32_to_cpu(blk->ver) >> 16) & 0xff,
			 (le32_to_cpu(blk->ver) >>  8) & 0xff,
			 le32_to_cpu(blk->ver) & 0xff);
		adsp_dbg(dsp, "%s.%d: %d bytes at 0x%x in %x\n",
			 file, blocks, le32_to_cpu(blk->len), offset, type);

		reg = 0;
		region_name = "Unknown";
		switch (type) {
		case (WMFW_NAME_TEXT << 8):
		case (WMFW_INFO_TEXT << 8):
			break;
		case (WMFW_ABSOLUTE << 8):
			/*
			 * Old files may use this for global
			 * coefficients.
			 */
			if (le32_to_cpu(blk->id) == dsp->fw_id &&
			    offset == 0) {
				region_name = "global coefficients";
				mem = wm_adsp_find_region(dsp, type);
				if (!mem) {
					adsp_err(dsp, "No ZM\n");
					break;
				}
				reg = dsp->ops->region_to_reg(mem, 0);

			} else {
				region_name = "register";
				reg = offset;
			}
			break;

		case WMFW_ADSP1_DM:
		case WMFW_ADSP1_ZM:
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
		case WMFW_HALO_PM_PACKED:
			adsp_dbg(dsp, "%s.%d: %d bytes in %x for %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 type, le32_to_cpu(blk->id));

			mem = wm_adsp_find_region(dsp, type);
			if (!mem) {
				adsp_err(dsp, "No base for region %x\n", type);
				break;
			}

			alg_region = wm_adsp_find_alg_region(dsp, type,
						le32_to_cpu(blk->id));
			if (alg_region) {
				reg = alg_region->base;
				reg = dsp->ops->region_to_reg(mem, reg);
				reg += offset;
			} else {
				adsp_err(dsp, "No %x for algorithm %x\n",
					 type, le32_to_cpu(blk->id));
			}
			break;

		default:
			adsp_err(dsp, "%s.%d: Unknown region type %x at %d\n",
				 file, blocks, type, pos);
			break;
		}

		if (reg) {
			if (le32_to_cpu(blk->len) >
			    firmware->size - pos - sizeof(*blk)) {
				adsp_err(dsp,
					 "%s.%d: %s region len %d bytes exceeds file length %zu\n",
					 file, blocks, region_name,
					 le32_to_cpu(blk->len),
					 firmware->size);
				ret = -EINVAL;
				goto out_fw;
			}

			buf = wm_adsp_buf_alloc(blk->data,
						le32_to_cpu(blk->len),
						&buf_list);
			if (!buf) {
				adsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			adsp_dbg(dsp, "%s.%d: Writing %d bytes at %x\n",
				 file, blocks, le32_to_cpu(blk->len),
				 reg);
			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(blk->len));
			if (ret != 0) {
				adsp_err(dsp,
					"%s.%d: Failed to write to %x in %s: %d\n",
					file, blocks, reg, region_name, ret);
			}
		}

		pos += (le32_to_cpu(blk->len) + sizeof(*blk) + 3) & ~0x03;
		blocks++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0)
		adsp_err(dsp, "Failed to complete async write: %d\n", ret);

	if (pos > firmware->size)
		adsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			  file, blocks, pos - firmware->size);

	wm_adsp_debugfs_save_binname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	release_firmware(firmware);
	wm_adsp_buf_free(&buf_list);
out:
	kfree(file);
	return ret;
}

static int wm_adsp_create_name(struct wm_adsp *dsp)
{
	char *p;

	if (!dsp->name) {
		dsp->name = devm_kasprintf(dsp->dev, GFP_KERNEL, "DSP%d",
					   dsp->num);
		if (!dsp->name)
			return -ENOMEM;
	}

	if (!dsp->fwf_name) {
		p = devm_kstrdup(dsp->dev, dsp->name, GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		dsp->fwf_name = p;
		for (; *p != 0; ++p)
			*p = tolower(*p);
	}

	return 0;
}

static int wm_adsp_common_init(struct wm_adsp *dsp)
{
	int ret;

	ret = wm_adsp_create_name(dsp);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&dsp->alg_regions);
	INIT_LIST_HEAD(&dsp->ctl_list);
	INIT_LIST_HEAD(&dsp->compr_list);
	INIT_LIST_HEAD(&dsp->buffer_list);

	mutex_init(&dsp->pwr_lock);

	return 0;
}

int wm_adsp1_init(struct wm_adsp *dsp)
{
	dsp->ops = &wm_adsp1_ops;

	return wm_adsp_common_init(dsp);
}
EXPORT_SYMBOL_GPL(wm_adsp1_init);

int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol,
		   int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	struct wm_coeff_ctl *ctl;
	int ret;
	unsigned int val;

	dsp->component = component;

	mutex_lock(&dsp->pwr_lock);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_SYS_ENA, ADSP1_SYS_ENA);

		/*
		 * For simplicity set the DSP clock rate to be the
		 * SYSCLK rate rather than making it configurable.
		 */
		if (dsp->sysclk_reg) {
			ret = regmap_read(dsp->regmap, dsp->sysclk_reg, &val);
			if (ret != 0) {
				adsp_err(dsp, "Failed to read SYSCLK state: %d\n",
				ret);
				goto err_mutex;
			}

			val = (val & dsp->sysclk_mask) >> dsp->sysclk_shift;

			ret = regmap_update_bits(dsp->regmap,
						 dsp->base + ADSP1_CONTROL_31,
						 ADSP1_CLK_SEL_MASK, val);
			if (ret != 0) {
				adsp_err(dsp, "Failed to set clock rate: %d\n",
					 ret);
				goto err_mutex;
			}
		}

		ret = wm_adsp_load(dsp);
		if (ret != 0)
			goto err_ena;

		ret = wm_adsp1_setup_algs(dsp);
		if (ret != 0)
			goto err_ena;

		ret = wm_adsp_load_coeff(dsp);
		if (ret != 0)
			goto err_ena;

		/* Initialize caches for enabled and unset controls */
		ret = wm_coeff_init_control_caches(dsp);
		if (ret != 0)
			goto err_ena;

		/* Sync set controls */
		ret = wm_coeff_sync_controls(dsp);
		if (ret != 0)
			goto err_ena;

		dsp->booted = true;

		/* Start the core running */
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_CORE_ENA | ADSP1_START,
				   ADSP1_CORE_ENA | ADSP1_START);

		dsp->running = true;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		dsp->running = false;
		dsp->booted = false;

		/* Halt the core */
		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_CORE_ENA | ADSP1_START, 0);

		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_19,
				   ADSP1_WDMA_BUFFER_LENGTH_MASK, 0);

		regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
				   ADSP1_SYS_ENA, 0);

		list_for_each_entry(ctl, &dsp->ctl_list, list)
			ctl->enabled = 0;


		wm_adsp_free_alg_regions(dsp);
		break;

	default:
		break;
	}

	mutex_unlock(&dsp->pwr_lock);

	return 0;

err_ena:
	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_SYS_ENA, 0);
err_mutex:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp1_event);

static int wm_adsp2v2_enable_core(struct wm_adsp *dsp)
{
	unsigned int val;
	int ret, count;

	/* Wait for the RAM to start, should be near instantaneous */
	for (count = 0; count < 10; ++count) {
		ret = regmap_read(dsp->regmap, dsp->base + ADSP2_STATUS1, &val);
		if (ret != 0)
			return ret;

		if (val & ADSP2_RAM_RDY)
			break;

		usleep_range(250, 500);
	}

	if (!(val & ADSP2_RAM_RDY)) {
		adsp_err(dsp, "Failed to start DSP RAM\n");
		return -EBUSY;
	}

	adsp_dbg(dsp, "RAM ready after %d polls\n", count);

	return 0;
}

static int wm_adsp2_enable_core(struct wm_adsp *dsp)
{
	int ret;

	ret = regmap_update_bits_async(dsp->regmap, dsp->base + ADSP2_CONTROL,
				       ADSP2_SYS_ENA, ADSP2_SYS_ENA);
	if (ret != 0)
		return ret;

	return wm_adsp2v2_enable_core(dsp);
}

static int wm_adsp2_lock(struct wm_adsp *dsp, unsigned int lock_regions)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int code0, code1, lock_reg;

	if (!(lock_regions & WM_ADSP2_REGION_ALL))
		return 0;

	lock_regions &= WM_ADSP2_REGION_ALL;
	lock_reg = dsp->base + ADSP2_LOCK_REGION_1_LOCK_REGION_0;

	while (lock_regions) {
		code0 = code1 = 0;
		if (lock_regions & BIT(0)) {
			code0 = ADSP2_LOCK_CODE_0;
			code1 = ADSP2_LOCK_CODE_1;
		}
		if (lock_regions & BIT(1)) {
			code0 |= ADSP2_LOCK_CODE_0 << ADSP2_LOCK_REGION_SHIFT;
			code1 |= ADSP2_LOCK_CODE_1 << ADSP2_LOCK_REGION_SHIFT;
		}
		regmap_write(regmap, lock_reg, code0);
		regmap_write(regmap, lock_reg, code1);
		lock_regions >>= 2;
		lock_reg += 2;
	}

	return 0;
}

static int wm_adsp2_enable_memory(struct wm_adsp *dsp)
{
	return regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				  ADSP2_MEM_ENA, ADSP2_MEM_ENA);
}

static void wm_adsp2_disable_memory(struct wm_adsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_MEM_ENA, 0);
}

static void wm_adsp2_disable_core(struct wm_adsp *dsp)
{
	regmap_write(dsp->regmap, dsp->base + ADSP2_RDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_2, 0);

	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_SYS_ENA, 0);
}

static void wm_adsp2v2_disable_core(struct wm_adsp *dsp)
{
	regmap_write(dsp->regmap, dsp->base + ADSP2_RDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2V2_WDMA_CONFIG_2, 0);
}

static void wm_adsp_boot_work(struct work_struct *work)
{
	struct wm_adsp *dsp = container_of(work,
					   struct wm_adsp,
					   boot_work);
	int ret;

	mutex_lock(&dsp->pwr_lock);

	if (dsp->ops->enable_memory) {
		ret = dsp->ops->enable_memory(dsp);
		if (ret != 0)
			goto err_mutex;
	}

	if (dsp->ops->enable_core) {
		ret = dsp->ops->enable_core(dsp);
		if (ret != 0)
			goto err_mem;
	}

	ret = wm_adsp_load(dsp);
	if (ret != 0)
		goto err_ena;

	ret = dsp->ops->setup_algs(dsp);
	if (ret != 0)
		goto err_ena;

	ret = wm_adsp_load_coeff(dsp);
	if (ret != 0)
		goto err_ena;

	/* Initialize caches for enabled and unset controls */
	ret = wm_coeff_init_control_caches(dsp);
	if (ret != 0)
		goto err_ena;

	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);

	dsp->booted = true;

	mutex_unlock(&dsp->pwr_lock);

	return;

err_ena:
	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);
err_mem:
	if (dsp->ops->disable_memory)
		dsp->ops->disable_memory(dsp);
err_mutex:
	mutex_unlock(&dsp->pwr_lock);
}

static int wm_halo_configure_mpu(struct wm_adsp *dsp, unsigned int lock_regions)
{
	struct reg_sequence config[] = {
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0x5555 },
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0xAAAA },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_0,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_0,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_0, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_0,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_0,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_1,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_1,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_1, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_1,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_1,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_2,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_2,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_2, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_2,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_2,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_3,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_3,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_3, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_3,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_3,   lock_regions },
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0 },
	};

	return regmap_multi_reg_write(dsp->regmap, config, ARRAY_SIZE(config));
}

int wm_adsp2_set_dspclk(struct snd_soc_dapm_widget *w, unsigned int freq)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	int ret;

	ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CLOCKING,
				 ADSP2_CLK_SEL_MASK,
				 freq << ADSP2_CLK_SEL_SHIFT);
	if (ret)
		adsp_err(dsp, "Failed to set clock rate: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp2_set_dspclk);

int wm_adsp2_preloader_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct wm_adsp *dsp = &dsps[mc->shift - 1];

	ucontrol->value.integer.value[0] = dsp->preloaded;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_preloader_get);

int wm_adsp2_preloader_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct wm_adsp *dsp = &dsps[mc->shift - 1];
	char preload[32];

	snprintf(preload, ARRAY_SIZE(preload), "%s Preload", dsp->name);

	dsp->preloaded = ucontrol->value.integer.value[0];

	if (ucontrol->value.integer.value[0])
		snd_soc_component_force_enable_pin(component, preload);
	else
		snd_soc_component_disable_pin(component, preload);

	snd_soc_dapm_sync(dapm);

	flush_work(&dsp->boot_work);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_preloader_put);

static void wm_adsp_stop_watchdog(struct wm_adsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_WATCHDOG,
			   ADSP2_WDT_ENA_MASK, 0);
}

static void wm_halo_stop_watchdog(struct wm_adsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + HALO_WDT_CONTROL,
			   HALO_WDT_EN_MASK, 0);
}

int wm_adsp_early_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	struct wm_coeff_ctl *ctl;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		queue_work(system_unbound_wq, &dsp->boot_work);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		mutex_lock(&dsp->pwr_lock);

		wm_adsp_debugfs_clear(dsp);

		dsp->fw_id = 0;
		dsp->fw_id_version = 0;

		dsp->booted = false;

		if (dsp->ops->disable_memory)
			dsp->ops->disable_memory(dsp);

		list_for_each_entry(ctl, &dsp->ctl_list, list)
			ctl->enabled = 0;

		wm_adsp_free_alg_regions(dsp);

		mutex_unlock(&dsp->pwr_lock);

		adsp_dbg(dsp, "Shutdown complete\n");
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_early_event);

static int wm_adsp2_start_core(struct wm_adsp *dsp)
{
	return regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				 ADSP2_CORE_ENA | ADSP2_START,
				 ADSP2_CORE_ENA | ADSP2_START);
}

static void wm_adsp2_stop_core(struct wm_adsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_CORE_ENA | ADSP2_START, 0);
}

int wm_adsp_event(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm_adsp *dsps = snd_soc_component_get_drvdata(component);
	struct wm_adsp *dsp = &dsps[w->shift];
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		flush_work(&dsp->boot_work);

		mutex_lock(&dsp->pwr_lock);

		if (!dsp->booted) {
			ret = -EIO;
			goto err;
		}

		if (dsp->ops->enable_core) {
			ret = dsp->ops->enable_core(dsp);
			if (ret != 0)
				goto err;
		}

		/* Sync set controls */
		ret = wm_coeff_sync_controls(dsp);
		if (ret != 0)
			goto err;

		if (dsp->ops->lock_memory) {
			ret = dsp->ops->lock_memory(dsp, dsp->lock_regions);
			if (ret != 0) {
				adsp_err(dsp, "Error configuring MPU: %d\n",
					 ret);
				goto err;
			}
		}

		if (dsp->ops->start_core) {
			ret = dsp->ops->start_core(dsp);
			if (ret != 0)
				goto err;
		}

		if (wm_adsp_fw[dsp->fw].num_caps != 0) {
			ret = wm_adsp_buffer_init(dsp);
			if (ret < 0)
				goto err;
		}

		dsp->running = true;

		mutex_unlock(&dsp->pwr_lock);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* Tell the firmware to cleanup */
		wm_adsp_signal_event_controls(dsp, WM_ADSP_FW_EVENT_SHUTDOWN);

		if (dsp->ops->stop_watchdog)
			dsp->ops->stop_watchdog(dsp);

		/* Log firmware state, it can be useful for analysis */
		if (dsp->ops->show_fw_status)
			dsp->ops->show_fw_status(dsp);

		mutex_lock(&dsp->pwr_lock);

		dsp->running = false;

		if (dsp->ops->stop_core)
			dsp->ops->stop_core(dsp);
		if (dsp->ops->disable_core)
			dsp->ops->disable_core(dsp);

		if (wm_adsp_fw[dsp->fw].num_caps != 0)
			wm_adsp_buffer_free(dsp);

		dsp->fatal_error = false;

		mutex_unlock(&dsp->pwr_lock);

		adsp_dbg(dsp, "Execution stopped\n");
		break;

	default:
		break;
	}

	return 0;
err:
	if (dsp->ops->stop_core)
		dsp->ops->stop_core(dsp);
	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);
	mutex_unlock(&dsp->pwr_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_event);

static int wm_halo_start_core(struct wm_adsp *dsp)
{
	return regmap_update_bits(dsp->regmap,
				  dsp->base + HALO_CCM_CORE_CONTROL,
				  HALO_CORE_EN, HALO_CORE_EN);
}

static void wm_halo_stop_core(struct wm_adsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + HALO_CCM_CORE_CONTROL,
			   HALO_CORE_EN, 0);

	/* reset halo core with CORE_SOFT_RESET */
	regmap_update_bits(dsp->regmap, dsp->base + HALO_CORE_SOFT_RESET,
			   HALO_CORE_SOFT_RESET_MASK, 1);
}

int wm_adsp2_component_probe(struct wm_adsp *dsp, struct snd_soc_component *component)
{
	char preload[32];

	snprintf(preload, ARRAY_SIZE(preload), "%s Preload", dsp->name);
	snd_soc_component_disable_pin(component, preload);

	wm_adsp2_init_debugfs(dsp, component);

	dsp->component = component;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_component_probe);

int wm_adsp2_component_remove(struct wm_adsp *dsp, struct snd_soc_component *component)
{
	wm_adsp2_cleanup_debugfs(dsp);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_component_remove);

int wm_adsp2_init(struct wm_adsp *dsp)
{
	int ret;

	ret = wm_adsp_common_init(dsp);
	if (ret)
		return ret;

	switch (dsp->rev) {
	case 0:
		/*
		 * Disable the DSP memory by default when in reset for a small
		 * power saving.
		 */
		ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
					 ADSP2_MEM_ENA, 0);
		if (ret) {
			adsp_err(dsp,
				 "Failed to clear memory retention: %d\n", ret);
			return ret;
		}

		dsp->ops = &wm_adsp2_ops[0];
		break;
	case 1:
		dsp->ops = &wm_adsp2_ops[1];
		break;
	default:
		dsp->ops = &wm_adsp2_ops[2];
		break;
	}

	INIT_WORK(&dsp->boot_work, wm_adsp_boot_work);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp2_init);

int wm_halo_init(struct wm_adsp *dsp)
{
	int ret;

	ret = wm_adsp_common_init(dsp);
	if (ret)
		return ret;

	dsp->ops = &wm_halo_ops;

	INIT_WORK(&dsp->boot_work, wm_adsp_boot_work);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_halo_init);

void wm_adsp2_remove(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;

	while (!list_empty(&dsp->ctl_list)) {
		ctl = list_first_entry(&dsp->ctl_list, struct wm_coeff_ctl,
					list);
		list_del(&ctl->list);
		wm_adsp_free_ctl_blk(ctl);
	}
}
EXPORT_SYMBOL_GPL(wm_adsp2_remove);

static inline int wm_adsp_compr_attached(struct wm_adsp_compr *compr)
{
	return compr->buf != NULL;
}

static int wm_adsp_compr_attach(struct wm_adsp_compr *compr)
{
	struct wm_adsp_compr_buf *buf = NULL, *tmp;

	if (compr->dsp->fatal_error)
		return -EINVAL;

	list_for_each_entry(tmp, &compr->dsp->buffer_list, list) {
		if (!tmp->name || !strcmp(compr->name, tmp->name)) {
			buf = tmp;
			break;
		}
	}

	if (!buf)
		return -EINVAL;

	compr->buf = buf;
	buf->compr = compr;

	return 0;
}

static void wm_adsp_compr_detach(struct wm_adsp_compr *compr)
{
	if (!compr)
		return;

	/* Wake the poll so it can see buffer is no longer attached */
	if (compr->stream)
		snd_compr_fragment_elapsed(compr->stream);

	if (wm_adsp_compr_attached(compr)) {
		compr->buf->compr = NULL;
		compr->buf = NULL;
	}
}

int wm_adsp_compr_open(struct wm_adsp *dsp, struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr, *tmp;
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	if (wm_adsp_fw[dsp->fw].num_caps == 0) {
		adsp_err(dsp, "%s: Firmware does not support compressed API\n",
			 rtd->codec_dai->name);
		ret = -ENXIO;
		goto out;
	}

	if (wm_adsp_fw[dsp->fw].compr_direction != stream->direction) {
		adsp_err(dsp, "%s: Firmware does not support stream direction\n",
			 rtd->codec_dai->name);
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(tmp, &dsp->compr_list, list) {
		if (!strcmp(tmp->name, rtd->codec_dai->name)) {
			adsp_err(dsp, "%s: Only a single stream supported per dai\n",
				 rtd->codec_dai->name);
			ret = -EBUSY;
			goto out;
		}
	}

	compr = kzalloc(sizeof(*compr), GFP_KERNEL);
	if (!compr) {
		ret = -ENOMEM;
		goto out;
	}

	compr->dsp = dsp;
	compr->stream = stream;
	compr->name = rtd->codec_dai->name;

	list_add_tail(&compr->list, &dsp->compr_list);

	stream->runtime->private_data = compr;

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_open);

int wm_adsp_compr_free(struct snd_compr_stream *stream)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;

	mutex_lock(&dsp->pwr_lock);

	wm_adsp_compr_detach(compr);
	list_del(&compr->list);

	kfree(compr->raw_buf);
	kfree(compr);

	mutex_unlock(&dsp->pwr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_free);

static int wm_adsp_compr_check_params(struct snd_compr_stream *stream,
				      struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	const struct wm_adsp_fw_caps *caps;
	const struct snd_codec_desc *desc;
	int i, j;

	if (params->buffer.fragment_size < WM_ADSP_MIN_FRAGMENT_SIZE ||
	    params->buffer.fragment_size > WM_ADSP_MAX_FRAGMENT_SIZE ||
	    params->buffer.fragments < WM_ADSP_MIN_FRAGMENTS ||
	    params->buffer.fragments > WM_ADSP_MAX_FRAGMENTS ||
	    params->buffer.fragment_size % WM_ADSP_DATA_WORD_SIZE) {
		compr_err(compr, "Invalid buffer fragsize=%d fragments=%d\n",
			  params->buffer.fragment_size,
			  params->buffer.fragments);

		return -EINVAL;
	}

	for (i = 0; i < wm_adsp_fw[dsp->fw].num_caps; i++) {
		caps = &wm_adsp_fw[dsp->fw].caps[i];
		desc = &caps->desc;

		if (caps->id != params->codec.id)
			continue;

		if (stream->direction == SND_COMPRESS_PLAYBACK) {
			if (desc->max_ch < params->codec.ch_out)
				continue;
		} else {
			if (desc->max_ch < params->codec.ch_in)
				continue;
		}

		if (!(desc->formats & (1 << params->codec.format)))
			continue;

		for (j = 0; j < desc->num_sample_rates; ++j)
			if (desc->sample_rates[j] == params->codec.sample_rate)
				return 0;
	}

	compr_err(compr, "Invalid params id=%u ch=%u,%u rate=%u fmt=%u\n",
		  params->codec.id, params->codec.ch_in, params->codec.ch_out,
		  params->codec.sample_rate, params->codec.format);
	return -EINVAL;
}

static inline unsigned int wm_adsp_compr_frag_words(struct wm_adsp_compr *compr)
{
	return compr->size.fragment_size / WM_ADSP_DATA_WORD_SIZE;
}

int wm_adsp_compr_set_params(struct snd_compr_stream *stream,
			     struct snd_compr_params *params)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	unsigned int size;
	int ret;

	ret = wm_adsp_compr_check_params(stream, params);
	if (ret)
		return ret;

	compr->size = params->buffer;

	compr_dbg(compr, "fragment_size=%d fragments=%d\n",
		  compr->size.fragment_size, compr->size.fragments);

	size = wm_adsp_compr_frag_words(compr) * sizeof(*compr->raw_buf);
	compr->raw_buf = kmalloc(size, GFP_DMA | GFP_KERNEL);
	if (!compr->raw_buf)
		return -ENOMEM;

	compr->sample_rate = params->codec.sample_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_set_params);

int wm_adsp_compr_get_caps(struct snd_compr_stream *stream,
			   struct snd_compr_caps *caps)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	int fw = compr->dsp->fw;
	int i;

	if (wm_adsp_fw[fw].caps) {
		for (i = 0; i < wm_adsp_fw[fw].num_caps; i++)
			caps->codecs[i] = wm_adsp_fw[fw].caps[i].id;

		caps->num_codecs = i;
		caps->direction = wm_adsp_fw[fw].compr_direction;

		caps->min_fragment_size = WM_ADSP_MIN_FRAGMENT_SIZE;
		caps->max_fragment_size = WM_ADSP_MAX_FRAGMENT_SIZE;
		caps->min_fragments = WM_ADSP_MIN_FRAGMENTS;
		caps->max_fragments = WM_ADSP_MAX_FRAGMENTS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_get_caps);

static int wm_adsp_read_data_block(struct wm_adsp *dsp, int mem_type,
				   unsigned int mem_addr,
				   unsigned int num_words, u32 *data)
{
	struct wm_adsp_region const *mem = wm_adsp_find_region(dsp, mem_type);
	unsigned int i, reg;
	int ret;

	if (!mem)
		return -EINVAL;

	reg = dsp->ops->region_to_reg(mem, mem_addr);

	ret = regmap_raw_read(dsp->regmap, reg, data,
			      sizeof(*data) * num_words);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_words; ++i)
		data[i] = be32_to_cpu(data[i]) & 0x00ffffffu;

	return 0;
}

static inline int wm_adsp_read_data_word(struct wm_adsp *dsp, int mem_type,
					 unsigned int mem_addr, u32 *data)
{
	return wm_adsp_read_data_block(dsp, mem_type, mem_addr, 1, data);
}

static int wm_adsp_write_data_word(struct wm_adsp *dsp, int mem_type,
				   unsigned int mem_addr, u32 data)
{
	struct wm_adsp_region const *mem = wm_adsp_find_region(dsp, mem_type);
	unsigned int reg;

	if (!mem)
		return -EINVAL;

	reg = dsp->ops->region_to_reg(mem, mem_addr);

	data = cpu_to_be32(data & 0x00ffffffu);

	return regmap_raw_write(dsp->regmap, reg, &data, sizeof(data));
}

static inline int wm_adsp_buffer_read(struct wm_adsp_compr_buf *buf,
				      unsigned int field_offset, u32 *data)
{
	return wm_adsp_read_data_word(buf->dsp, buf->host_buf_mem_type,
				      buf->host_buf_ptr + field_offset, data);
}

static inline int wm_adsp_buffer_write(struct wm_adsp_compr_buf *buf,
				       unsigned int field_offset, u32 data)
{
	return wm_adsp_write_data_word(buf->dsp, buf->host_buf_mem_type,
				       buf->host_buf_ptr + field_offset, data);
}

static void wm_adsp_remove_padding(u32 *buf, int nwords, int data_word_size)
{
	u8 *pack_in = (u8 *)buf;
	u8 *pack_out = (u8 *)buf;
	int i, j;

	/* Remove the padding bytes from the data read from the DSP */
	for (i = 0; i < nwords; i++) {
		for (j = 0; j < data_word_size; j++)
			*pack_out++ = *pack_in++;

		pack_in += sizeof(*buf) - data_word_size;
	}
}

static int wm_adsp_buffer_populate(struct wm_adsp_compr_buf *buf)
{
	const struct wm_adsp_fw_caps *caps = wm_adsp_fw[buf->dsp->fw].caps;
	struct wm_adsp_buffer_region *region;
	u32 offset = 0;
	int i, ret;

	buf->regions = kcalloc(caps->num_regions, sizeof(*buf->regions),
			       GFP_KERNEL);
	if (!buf->regions)
		return -ENOMEM;

	for (i = 0; i < caps->num_regions; ++i) {
		region = &buf->regions[i];

		region->offset = offset;
		region->mem_type = caps->region_defs[i].mem_type;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].base_offset,
					  &region->base_addr);
		if (ret < 0)
			return ret;

		ret = wm_adsp_buffer_read(buf, caps->region_defs[i].size_offset,
					  &offset);
		if (ret < 0)
			return ret;

		region->cumulative_size = offset;

		compr_dbg(buf,
			  "region=%d type=%d base=%08x off=%08x size=%08x\n",
			  i, region->mem_type, region->base_addr,
			  region->offset, region->cumulative_size);
	}

	return 0;
}

static void wm_adsp_buffer_clear(struct wm_adsp_compr_buf *buf)
{
	buf->irq_count = 0xFFFFFFFF;
	buf->read_index = -1;
	buf->avail = 0;
}

static struct wm_adsp_compr_buf *wm_adsp_buffer_alloc(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->dsp = dsp;

	wm_adsp_buffer_clear(buf);

	list_add_tail(&buf->list, &dsp->buffer_list);

	return buf;
}

static int wm_adsp_buffer_parse_legacy(struct wm_adsp *dsp)
{
	struct wm_adsp_alg_region *alg_region;
	struct wm_adsp_compr_buf *buf;
	u32 xmalg, addr, magic;
	int i, ret;

	buf = wm_adsp_buffer_alloc(dsp);
	if (!buf)
		return -ENOMEM;

	alg_region = wm_adsp_find_alg_region(dsp, WMFW_ADSP2_XM, dsp->fw_id);
	xmalg = dsp->ops->sys_config_size / sizeof(__be32);

	addr = alg_region->base + xmalg + ALG_XM_FIELD(magic);
	ret = wm_adsp_read_data_word(dsp, WMFW_ADSP2_XM, addr, &magic);
	if (ret < 0)
		return ret;

	if (magic != WM_ADSP_ALG_XM_STRUCT_MAGIC)
		return -ENODEV;

	addr = alg_region->base + xmalg + ALG_XM_FIELD(host_buf_ptr);
	for (i = 0; i < 5; ++i) {
		ret = wm_adsp_read_data_word(dsp, WMFW_ADSP2_XM, addr,
					     &buf->host_buf_ptr);
		if (ret < 0)
			return ret;

		if (buf->host_buf_ptr)
			break;

		usleep_range(1000, 2000);
	}

	if (!buf->host_buf_ptr)
		return -EIO;

	buf->host_buf_mem_type = WMFW_ADSP2_XM;

	ret = wm_adsp_buffer_populate(buf);
	if (ret < 0)
		return ret;

	compr_dbg(buf, "legacy host_buf_ptr=%x\n", buf->host_buf_ptr);

	return 0;
}

static int wm_adsp_buffer_parse_coeff(struct wm_coeff_ctl *ctl)
{
	struct wm_adsp_host_buf_coeff_v1 coeff_v1;
	struct wm_adsp_compr_buf *buf;
	unsigned int val, reg;
	int ret, i;

	ret = wm_coeff_base_reg(ctl, &reg);
	if (ret)
		return ret;

	for (i = 0; i < 5; ++i) {
		ret = regmap_raw_read(ctl->dsp->regmap, reg, &val, sizeof(val));
		if (ret < 0)
			return ret;

		if (val)
			break;

		usleep_range(1000, 2000);
	}

	if (!val) {
		adsp_err(ctl->dsp, "Failed to acquire host buffer\n");
		return -EIO;
	}

	buf = wm_adsp_buffer_alloc(ctl->dsp);
	if (!buf)
		return -ENOMEM;

	buf->host_buf_mem_type = ctl->alg_region.type;
	buf->host_buf_ptr = be32_to_cpu(val);

	ret = wm_adsp_buffer_populate(buf);
	if (ret < 0)
		return ret;

	/*
	 * v0 host_buffer coefficients didn't have versioning, so if the
	 * control is one word, assume version 0.
	 */
	if (ctl->len == 4) {
		compr_dbg(buf, "host_buf_ptr=%x\n", buf->host_buf_ptr);
		return 0;
	}

	ret = regmap_raw_read(ctl->dsp->regmap, reg, &coeff_v1,
			      sizeof(coeff_v1));
	if (ret < 0)
		return ret;

	coeff_v1.versions = be32_to_cpu(coeff_v1.versions);
	val = coeff_v1.versions & HOST_BUF_COEFF_COMPAT_VER_MASK;
	val >>= HOST_BUF_COEFF_COMPAT_VER_SHIFT;

	if (val > HOST_BUF_COEFF_SUPPORTED_COMPAT_VER) {
		adsp_err(ctl->dsp,
			 "Host buffer coeff ver %u > supported version %u\n",
			 val, HOST_BUF_COEFF_SUPPORTED_COMPAT_VER);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(coeff_v1.name); i++)
		coeff_v1.name[i] = be32_to_cpu(coeff_v1.name[i]);

	wm_adsp_remove_padding((u32 *)&coeff_v1.name,
			       ARRAY_SIZE(coeff_v1.name),
			       WM_ADSP_DATA_WORD_SIZE);

	buf->name = kasprintf(GFP_KERNEL, "%s-dsp-%s", ctl->dsp->part,
			      (char *)&coeff_v1.name);

	compr_dbg(buf, "host_buf_ptr=%x coeff version %u\n",
		  buf->host_buf_ptr, val);

	return val;
}

static int wm_adsp_buffer_init(struct wm_adsp *dsp)
{
	struct wm_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->type != WMFW_CTL_TYPE_HOST_BUFFER)
			continue;

		if (!ctl->enabled)
			continue;

		ret = wm_adsp_buffer_parse_coeff(ctl);
		if (ret < 0) {
			adsp_err(dsp, "Failed to parse coeff: %d\n", ret);
			goto error;
		} else if (ret == 0) {
			/* Only one buffer supported for version 0 */
			return 0;
		}
	}

	if (list_empty(&dsp->buffer_list)) {
		/* Fall back to legacy support */
		ret = wm_adsp_buffer_parse_legacy(dsp);
		if (ret) {
			adsp_err(dsp, "Failed to parse legacy: %d\n", ret);
			goto error;
		}
	}

	return 0;

error:
	wm_adsp_buffer_free(dsp);
	return ret;
}

static int wm_adsp_buffer_free(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf, *tmp;

	list_for_each_entry_safe(buf, tmp, &dsp->buffer_list, list) {
		wm_adsp_compr_detach(buf->compr);

		kfree(buf->name);
		kfree(buf->regions);
		list_del(&buf->list);
		kfree(buf);
	}

	return 0;
}

static int wm_adsp_buffer_get_error(struct wm_adsp_compr_buf *buf)
{
	int ret;

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(error), &buf->error);
	if (ret < 0) {
		compr_err(buf, "Failed to check buffer error: %d\n", ret);
		return ret;
	}
	if (buf->error != 0) {
		compr_err(buf, "Buffer error occurred: %d\n", buf->error);
		return -EIO;
	}

	return 0;
}

int wm_adsp_compr_trigger(struct snd_compr_stream *stream, int cmd)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret = 0;

	compr_dbg(compr, "Trigger: %d\n", cmd);

	mutex_lock(&dsp->pwr_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!wm_adsp_compr_attached(compr)) {
			ret = wm_adsp_compr_attach(compr);
			if (ret < 0) {
				compr_err(compr, "Failed to link buffer and stream: %d\n",
					  ret);
				break;
			}
		}

		ret = wm_adsp_buffer_get_error(compr->buf);
		if (ret < 0)
			break;

		/* Trigger the IRQ at one fragment of data */
		ret = wm_adsp_buffer_write(compr->buf,
					   HOST_BUFFER_FIELD(high_water_mark),
					   wm_adsp_compr_frag_words(compr));
		if (ret < 0) {
			compr_err(compr, "Failed to set high water mark: %d\n",
				  ret);
			break;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (wm_adsp_compr_attached(compr))
			wm_adsp_buffer_clear(compr->buf);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_trigger);

static inline int wm_adsp_buffer_size(struct wm_adsp_compr_buf *buf)
{
	int last_region = wm_adsp_fw[buf->dsp->fw].caps->num_regions - 1;

	return buf->regions[last_region].cumulative_size;
}

static int wm_adsp_buffer_update_avail(struct wm_adsp_compr_buf *buf)
{
	u32 next_read_index, next_write_index;
	int write_index, read_index, avail;
	int ret;

	/* Only sync read index if we haven't already read a valid index */
	if (buf->read_index < 0) {
		ret = wm_adsp_buffer_read(buf,
				HOST_BUFFER_FIELD(next_read_index),
				&next_read_index);
		if (ret < 0)
			return ret;

		read_index = sign_extend32(next_read_index, 23);

		if (read_index < 0) {
			compr_dbg(buf, "Avail check on unstarted stream\n");
			return 0;
		}

		buf->read_index = read_index;
	}

	ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(next_write_index),
			&next_write_index);
	if (ret < 0)
		return ret;

	write_index = sign_extend32(next_write_index, 23);

	avail = write_index - buf->read_index;
	if (avail < 0)
		avail += wm_adsp_buffer_size(buf);

	compr_dbg(buf, "readindex=0x%x, writeindex=0x%x, avail=%d\n",
		  buf->read_index, write_index, avail * WM_ADSP_DATA_WORD_SIZE);

	buf->avail = avail;

	return 0;
}

int wm_adsp_compr_handle_irq(struct wm_adsp *dsp)
{
	struct wm_adsp_compr_buf *buf;
	struct wm_adsp_compr *compr;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	if (list_empty(&dsp->buffer_list)) {
		ret = -ENODEV;
		goto out;
	}

	adsp_dbg(dsp, "Handling buffer IRQ\n");

	list_for_each_entry(buf, &dsp->buffer_list, list) {
		compr = buf->compr;

		ret = wm_adsp_buffer_get_error(buf);
		if (ret < 0)
			goto out_notify; /* Wake poll to report error */

		ret = wm_adsp_buffer_read(buf, HOST_BUFFER_FIELD(irq_count),
					  &buf->irq_count);
		if (ret < 0) {
			compr_err(buf, "Failed to get irq_count: %d\n", ret);
			goto out;
		}

		ret = wm_adsp_buffer_update_avail(buf);
		if (ret < 0) {
			compr_err(buf, "Error reading avail: %d\n", ret);
			goto out;
		}

		if (wm_adsp_fw[dsp->fw].voice_trigger && buf->irq_count == 2)
			ret = WM_ADSP_COMPR_VOICE_TRIGGER;

out_notify:
		if (compr && compr->stream)
			snd_compr_fragment_elapsed(compr->stream);
	}

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_handle_irq);

static int wm_adsp_buffer_reenable_irq(struct wm_adsp_compr_buf *buf)
{
	if (buf->irq_count & 0x01)
		return 0;

	compr_dbg(buf, "Enable IRQ(0x%x) for next fragment\n", buf->irq_count);

	buf->irq_count |= 0x01;

	return wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(irq_ack),
				    buf->irq_count);
}

int wm_adsp_compr_pointer(struct snd_compr_stream *stream,
			  struct snd_compr_tstamp *tstamp)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	struct wm_adsp_compr_buf *buf;
	int ret = 0;

	compr_dbg(compr, "Pointer request\n");

	mutex_lock(&dsp->pwr_lock);

	buf = compr->buf;

	if (dsp->fatal_error || !buf || buf->error) {
		snd_compr_stop_error(stream, SNDRV_PCM_STATE_XRUN);
		ret = -EIO;
		goto out;
	}

	if (buf->avail < wm_adsp_compr_frag_words(compr)) {
		ret = wm_adsp_buffer_update_avail(buf);
		if (ret < 0) {
			compr_err(compr, "Error reading avail: %d\n", ret);
			goto out;
		}

		/*
		 * If we really have less than 1 fragment available tell the
		 * DSP to inform us once a whole fragment is available.
		 */
		if (buf->avail < wm_adsp_compr_frag_words(compr)) {
			ret = wm_adsp_buffer_get_error(buf);
			if (ret < 0) {
				if (buf->error)
					snd_compr_stop_error(stream,
							SNDRV_PCM_STATE_XRUN);
				goto out;
			}

			ret = wm_adsp_buffer_reenable_irq(buf);
			if (ret < 0) {
				compr_err(compr, "Failed to re-enable buffer IRQ: %d\n",
					  ret);
				goto out;
			}
		}
	}

	tstamp->copied_total = compr->copied_total;
	tstamp->copied_total += buf->avail * WM_ADSP_DATA_WORD_SIZE;
	tstamp->sampling_rate = compr->sample_rate;

out:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_pointer);

static int wm_adsp_buffer_capture_block(struct wm_adsp_compr *compr, int target)
{
	struct wm_adsp_compr_buf *buf = compr->buf;
	unsigned int adsp_addr;
	int mem_type, nwords, max_read;
	int i, ret;

	/* Calculate read parameters */
	for (i = 0; i < wm_adsp_fw[buf->dsp->fw].caps->num_regions; ++i)
		if (buf->read_index < buf->regions[i].cumulative_size)
			break;

	if (i == wm_adsp_fw[buf->dsp->fw].caps->num_regions)
		return -EINVAL;

	mem_type = buf->regions[i].mem_type;
	adsp_addr = buf->regions[i].base_addr +
		    (buf->read_index - buf->regions[i].offset);

	max_read = wm_adsp_compr_frag_words(compr);
	nwords = buf->regions[i].cumulative_size - buf->read_index;

	if (nwords > target)
		nwords = target;
	if (nwords > buf->avail)
		nwords = buf->avail;
	if (nwords > max_read)
		nwords = max_read;
	if (!nwords)
		return 0;

	/* Read data from DSP */
	ret = wm_adsp_read_data_block(buf->dsp, mem_type, adsp_addr,
				      nwords, compr->raw_buf);
	if (ret < 0)
		return ret;

	wm_adsp_remove_padding(compr->raw_buf, nwords, WM_ADSP_DATA_WORD_SIZE);

	/* update read index to account for words read */
	buf->read_index += nwords;
	if (buf->read_index == wm_adsp_buffer_size(buf))
		buf->read_index = 0;

	ret = wm_adsp_buffer_write(buf, HOST_BUFFER_FIELD(next_read_index),
				   buf->read_index);
	if (ret < 0)
		return ret;

	/* update avail to account for words read */
	buf->avail -= nwords;

	return nwords;
}

static int wm_adsp_compr_read(struct wm_adsp_compr *compr,
			      char __user *buf, size_t count)
{
	struct wm_adsp *dsp = compr->dsp;
	int ntotal = 0;
	int nwords, nbytes;

	compr_dbg(compr, "Requested read of %zu bytes\n", count);

	if (dsp->fatal_error || !compr->buf || compr->buf->error) {
		snd_compr_stop_error(compr->stream, SNDRV_PCM_STATE_XRUN);
		return -EIO;
	}

	count /= WM_ADSP_DATA_WORD_SIZE;

	do {
		nwords = wm_adsp_buffer_capture_block(compr, count);
		if (nwords < 0) {
			compr_err(compr, "Failed to capture block: %d\n",
				  nwords);
			return nwords;
		}

		nbytes = nwords * WM_ADSP_DATA_WORD_SIZE;

		compr_dbg(compr, "Read %d bytes\n", nbytes);

		if (copy_to_user(buf + ntotal, compr->raw_buf, nbytes)) {
			compr_err(compr, "Failed to copy data to user: %d, %d\n",
				  ntotal, nbytes);
			return -EFAULT;
		}

		count -= nwords;
		ntotal += nbytes;
	} while (nwords > 0 && count > 0);

	compr->copied_total += ntotal;

	return ntotal;
}

int wm_adsp_compr_copy(struct snd_compr_stream *stream, char __user *buf,
		       size_t count)
{
	struct wm_adsp_compr *compr = stream->runtime->private_data;
	struct wm_adsp *dsp = compr->dsp;
	int ret;

	mutex_lock(&dsp->pwr_lock);

	if (stream->direction == SND_COMPRESS_CAPTURE)
		ret = wm_adsp_compr_read(compr, buf, count);
	else
		ret = -ENOTSUPP;

	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm_adsp_compr_copy);

static void wm_adsp_fatal_error(struct wm_adsp *dsp)
{
	struct wm_adsp_compr *compr;

	dsp->fatal_error = true;

	list_for_each_entry(compr, &dsp->compr_list, list) {
		if (compr->stream)
			snd_compr_fragment_elapsed(compr->stream);
	}
}

irqreturn_t wm_adsp2_bus_error(struct wm_adsp *dsp)
{
	unsigned int val;
	struct regmap *regmap = dsp->regmap;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	ret = regmap_read(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL, &val);
	if (ret) {
		adsp_err(dsp,
			"Failed to read Region Lock Ctrl register: %d\n", ret);
		goto error;
	}

	if (val & ADSP2_WDT_TIMEOUT_STS_MASK) {
		adsp_err(dsp, "watchdog timeout error\n");
		dsp->ops->stop_watchdog(dsp);
		wm_adsp_fatal_error(dsp);
	}

	if (val & (ADSP2_SLAVE_ERR_MASK | ADSP2_REGION_LOCK_ERR_MASK)) {
		if (val & ADSP2_SLAVE_ERR_MASK)
			adsp_err(dsp, "bus error: slave error\n");
		else
			adsp_err(dsp, "bus error: region lock error\n");

		ret = regmap_read(regmap, dsp->base + ADSP2_BUS_ERR_ADDR, &val);
		if (ret) {
			adsp_err(dsp,
				 "Failed to read Bus Err Addr register: %d\n",
				 ret);
			goto error;
		}

		adsp_err(dsp, "bus error address = 0x%x\n",
			 val & ADSP2_BUS_ERR_ADDR_MASK);

		ret = regmap_read(regmap,
				  dsp->base + ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR,
				  &val);
		if (ret) {
			adsp_err(dsp,
				 "Failed to read Pmem Xmem Err Addr register: %d\n",
				 ret);
			goto error;
		}

		adsp_err(dsp, "xmem error address = 0x%x\n",
			 val & ADSP2_XMEM_ERR_ADDR_MASK);
		adsp_err(dsp, "pmem error address = 0x%x\n",
			 (val & ADSP2_PMEM_ERR_ADDR_MASK) >>
			 ADSP2_PMEM_ERR_ADDR_SHIFT);
	}

	regmap_update_bits(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL,
			   ADSP2_CTRL_ERR_EINT, ADSP2_CTRL_ERR_EINT);

error:
	mutex_unlock(&dsp->pwr_lock);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_adsp2_bus_error);

irqreturn_t wm_halo_bus_error(struct wm_adsp *dsp)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int fault[6];
	struct reg_sequence clear[] = {
		{ dsp->base + HALO_MPU_XM_VIO_STATUS,     0x0 },
		{ dsp->base + HALO_MPU_YM_VIO_STATUS,     0x0 },
		{ dsp->base + HALO_MPU_PM_VIO_STATUS,     0x0 },
	};
	int ret;

	mutex_lock(&dsp->pwr_lock);

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_1,
			  fault);
	if (ret) {
		adsp_warn(dsp, "Failed to read AHB DEBUG_1: %d\n", ret);
		goto exit_unlock;
	}

	adsp_warn(dsp, "AHB: STATUS: 0x%x ADDR: 0x%x\n",
		  *fault & HALO_AHBM_FLAGS_ERR_MASK,
		  (*fault & HALO_AHBM_CORE_ERR_ADDR_MASK) >>
		  HALO_AHBM_CORE_ERR_ADDR_SHIFT);

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_0,
			  fault);
	if (ret) {
		adsp_warn(dsp, "Failed to read AHB DEBUG_0: %d\n", ret);
		goto exit_unlock;
	}

	adsp_warn(dsp, "AHB: SYS_ADDR: 0x%x\n", *fault);

	ret = regmap_bulk_read(regmap, dsp->base + HALO_MPU_XM_VIO_ADDR,
			       fault, ARRAY_SIZE(fault));
	if (ret) {
		adsp_warn(dsp, "Failed to read MPU fault info: %d\n", ret);
		goto exit_unlock;
	}

	adsp_warn(dsp, "XM: STATUS:0x%x ADDR:0x%x\n", fault[1], fault[0]);
	adsp_warn(dsp, "YM: STATUS:0x%x ADDR:0x%x\n", fault[3], fault[2]);
	adsp_warn(dsp, "PM: STATUS:0x%x ADDR:0x%x\n", fault[5], fault[4]);

	ret = regmap_multi_reg_write(dsp->regmap, clear, ARRAY_SIZE(clear));
	if (ret)
		adsp_warn(dsp, "Failed to clear MPU status: %d\n", ret);

exit_unlock:
	mutex_unlock(&dsp->pwr_lock);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_halo_bus_error);

irqreturn_t wm_halo_wdt_expire(int irq, void *data)
{
	struct wm_adsp *dsp = data;

	mutex_lock(&dsp->pwr_lock);

	adsp_warn(dsp, "WDT Expiry Fault\n");
	dsp->ops->stop_watchdog(dsp);
	wm_adsp_fatal_error(dsp);

	mutex_unlock(&dsp->pwr_lock);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wm_halo_wdt_expire);

static struct wm_adsp_ops wm_adsp1_ops = {
	.validate_version = wm_adsp_validate_version,
	.parse_sizes = wm_adsp1_parse_sizes,
	.region_to_reg = wm_adsp_region_to_reg,
};

static struct wm_adsp_ops wm_adsp2_ops[] = {
	{
		.sys_config_size = sizeof(struct wm_adsp_system_config_xm_hdr),
		.parse_sizes = wm_adsp2_parse_sizes,
		.validate_version = wm_adsp_validate_version,
		.setup_algs = wm_adsp2_setup_algs,
		.region_to_reg = wm_adsp_region_to_reg,

		.show_fw_status = wm_adsp2_show_fw_status,

		.enable_memory = wm_adsp2_enable_memory,
		.disable_memory = wm_adsp2_disable_memory,

		.enable_core = wm_adsp2_enable_core,
		.disable_core = wm_adsp2_disable_core,

		.start_core = wm_adsp2_start_core,
		.stop_core = wm_adsp2_stop_core,

	},
	{
		.sys_config_size = sizeof(struct wm_adsp_system_config_xm_hdr),
		.parse_sizes = wm_adsp2_parse_sizes,
		.validate_version = wm_adsp_validate_version,
		.setup_algs = wm_adsp2_setup_algs,
		.region_to_reg = wm_adsp_region_to_reg,

		.show_fw_status = wm_adsp2v2_show_fw_status,

		.enable_memory = wm_adsp2_enable_memory,
		.disable_memory = wm_adsp2_disable_memory,
		.lock_memory = wm_adsp2_lock,

		.enable_core = wm_adsp2v2_enable_core,
		.disable_core = wm_adsp2v2_disable_core,

		.start_core = wm_adsp2_start_core,
		.stop_core = wm_adsp2_stop_core,
	},
	{
		.sys_config_size = sizeof(struct wm_adsp_system_config_xm_hdr),
		.parse_sizes = wm_adsp2_parse_sizes,
		.validate_version = wm_adsp_validate_version,
		.setup_algs = wm_adsp2_setup_algs,
		.region_to_reg = wm_adsp_region_to_reg,

		.show_fw_status = wm_adsp2v2_show_fw_status,
		.stop_watchdog = wm_adsp_stop_watchdog,

		.enable_memory = wm_adsp2_enable_memory,
		.disable_memory = wm_adsp2_disable_memory,
		.lock_memory = wm_adsp2_lock,

		.enable_core = wm_adsp2v2_enable_core,
		.disable_core = wm_adsp2v2_disable_core,

		.start_core = wm_adsp2_start_core,
		.stop_core = wm_adsp2_stop_core,
	},
};

static struct wm_adsp_ops wm_halo_ops = {
	.sys_config_size = sizeof(struct wm_halo_system_config_xm_hdr),
	.parse_sizes = wm_adsp2_parse_sizes,
	.validate_version = wm_halo_validate_version,
	.setup_algs = wm_halo_setup_algs,
	.region_to_reg = wm_halo_region_to_reg,

	.show_fw_status = wm_halo_show_fw_status,
	.stop_watchdog = wm_halo_stop_watchdog,

	.lock_memory = wm_halo_configure_mpu,

	.start_core = wm_halo_start_core,
	.stop_core = wm_halo_stop_core,
};

MODULE_LICENSE("GPL v2");
