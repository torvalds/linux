/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Socionext UniPhier AIO ALSA driver.
 *
 * Copyright (c) 2016-2018 Socionext Inc.
 */

#ifndef SND_UNIPHIER_AIO_H__
#define SND_UNIPHIER_AIO_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

struct platform_device;

enum ID_PORT_TYPE {
	PORT_TYPE_UNKNOWN,
	PORT_TYPE_I2S,
	PORT_TYPE_SPDIF,
	PORT_TYPE_EVE,
	PORT_TYPE_CONV,
};

enum ID_PORT_DIR {
	PORT_DIR_OUTPUT,
	PORT_DIR_INPUT,
};

enum IEC61937_PC {
	IEC61937_PC_AC3   = 0x0001,
	IEC61937_PC_PAUSE = 0x0003,
	IEC61937_PC_MPA   = 0x0004,
	IEC61937_PC_MP3   = 0x0005,
	IEC61937_PC_DTS1  = 0x000b,
	IEC61937_PC_DTS2  = 0x000c,
	IEC61937_PC_DTS3  = 0x000d,
	IEC61937_PC_AAC   = 0x0007,
};

/* IEC61937 Repetition period of data-burst in IEC60958 frames */
#define IEC61937_FRM_STR_AC3       1536
#define IEC61937_FRM_STR_MPA       1152
#define IEC61937_FRM_STR_MP3       1152
#define IEC61937_FRM_STR_DTS1      512
#define IEC61937_FRM_STR_DTS2      1024
#define IEC61937_FRM_STR_DTS3      2048
#define IEC61937_FRM_STR_AAC       1024

/* IEC61937 Repetition period of Pause data-burst in IEC60958 frames */
#define IEC61937_FRM_PAU_AC3       3
#define IEC61937_FRM_PAU_MPA       32
#define IEC61937_FRM_PAU_MP3       32
#define IEC61937_FRM_PAU_DTS1      3
#define IEC61937_FRM_PAU_DTS2      3
#define IEC61937_FRM_PAU_DTS3      3
#define IEC61937_FRM_PAU_AAC       32

/* IEC61937 Pa and Pb */
#define IEC61937_HEADER_SIGN       0x1f4e72f8

#define AUD_HW_PCMIN1    0
#define AUD_HW_PCMIN2    1
#define AUD_HW_PCMIN3    2
#define AUD_HW_IECIN1    3
#define AUD_HW_DIECIN1   4

#define AUD_NAME_PCMIN1     "aio-pcmin1"
#define AUD_NAME_PCMIN2     "aio-pcmin2"
#define AUD_NAME_PCMIN3     "aio-pcmin3"
#define AUD_NAME_IECIN1     "aio-iecin1"
#define AUD_NAME_DIECIN1    "aio-diecin1"

#define AUD_HW_HPCMOUT1    0
#define AUD_HW_PCMOUT1     1
#define AUD_HW_PCMOUT2     2
#define AUD_HW_PCMOUT3     3
#define AUD_HW_EPCMOUT1    4
#define AUD_HW_EPCMOUT2    5
#define AUD_HW_EPCMOUT3    6
#define AUD_HW_EPCMOUT6    9
#define AUD_HW_HIECOUT1    10
#define AUD_HW_IECOUT1     11
#define AUD_HW_CMASTER     31

#define AUD_NAME_HPCMOUT1        "aio-hpcmout1"
#define AUD_NAME_PCMOUT1         "aio-pcmout1"
#define AUD_NAME_PCMOUT2         "aio-pcmout2"
#define AUD_NAME_PCMOUT3         "aio-pcmout3"
#define AUD_NAME_EPCMOUT1        "aio-epcmout1"
#define AUD_NAME_EPCMOUT2        "aio-epcmout2"
#define AUD_NAME_EPCMOUT3        "aio-epcmout3"
#define AUD_NAME_EPCMOUT6        "aio-epcmout6"
#define AUD_NAME_HIECOUT1        "aio-hiecout1"
#define AUD_NAME_IECOUT1         "aio-iecout1"
#define AUD_NAME_CMASTER         "aio-cmaster"
#define AUD_NAME_HIECCOMPOUT1    "aio-hieccompout1"
#define AUD_NAME_IECCOMPOUT1     "aio-ieccompout1"

#define AUD_GNAME_HDMI    "aio-hdmi"
#define AUD_GNAME_LINE    "aio-line"
#define AUD_GNAME_AUX     "aio-aux"
#define AUD_GNAME_IEC     "aio-iec"

#define AUD_CLK_IO        0
#define AUD_CLK_A1        1
#define AUD_CLK_F1        2
#define AUD_CLK_A2        3
#define AUD_CLK_F2        4
#define AUD_CLK_A         5
#define AUD_CLK_F         6
#define AUD_CLK_APLL      7
#define AUD_CLK_RX0       8
#define AUD_CLK_USB0      9
#define AUD_CLK_HSC0      10

#define AUD_PLL_A1        0
#define AUD_PLL_F1        1
#define AUD_PLL_A2        2
#define AUD_PLL_F2        3
#define AUD_PLL_APLL      4
#define AUD_PLL_RX0       5
#define AUD_PLL_USB0      6
#define AUD_PLL_HSC0      7

#define AUD_PLLDIV_1_2    0
#define AUD_PLLDIV_1_3    1
#define AUD_PLLDIV_1_1    2
#define AUD_PLLDIV_2_3    3

#define AUD_VOL_INIT         0x4000 /* +0dB */
#define AUD_VOL_MAX          0xffff /* +6dB */
#define AUD_VOL_FADE_TIME    20 /* 20ms */

#define AUD_RING_SIZE            (128 * 1024)

#define AUD_MIN_FRAGMENT         4
#define AUD_MAX_FRAGMENT         8
#define AUD_MIN_FRAGMENT_SIZE    (4 * 1024)
#define AUD_MAX_FRAGMENT_SIZE    (16 * 1024)

/* max 5 slots, 10 channels, 2 channel in 1 slot */
#define AUD_MAX_SLOTSEL    5

/*
 * This is a selector for virtual register map of AIO.
 *
 * map:  Specify the index of virtual register map.
 * hw :  Specify the ID of real register map, selector uses this value.
 *       A meaning of this value depends specification of SoC.
 */
struct uniphier_aio_selector {
	int map;
	int hw;
};

/**
 * 'SoftWare MAPping' setting of UniPhier AIO registers.
 *
 * We have to setup 'virtual' register maps to access 'real' registers of AIO.
 * This feature is legacy and meaningless but AIO needs this to work.
 *
 * Each hardware blocks have own virtual register maps as following:
 *
 * Address Virtual                      Real
 * ------- ---------                    ---------------
 * 0x12000 DMAC map0 --> [selector] --> DMAC hardware 3
 * 0x12080 DMAC map1 --> [selector] --> DMAC hardware 1
 * ...
 * 0x42000 Port map0 --> [selector] --> Port hardware 1
 * 0x42400 Port map1 --> [selector] --> Port hardware 2
 * ...
 *
 * ch   : Input or output channel of DMAC
 * rb   : Ring buffer
 * iport: PCM input port
 * iif  : Input interface
 * oport: PCM output port
 * oif  : Output interface
 * och  : Output channel of DMAC for sampling rate converter
 *
 * These are examples for sound data paths:
 *
 * For caputure device:
 *   (outer of AIO) -> iport -> iif -> ch -> rb -> (CPU)
 * For playback device:
 *   (CPU) -> rb -> ch -> oif -> oport -> (outer of AIO)
 * For sampling rate converter device:
 *   (CPU) -> rb -> ch -> oif -> (HW SRC) -> iif -> och -> orb -> (CPU)
 */
struct uniphier_aio_swmap {
	int type;
	int dir;

	struct uniphier_aio_selector ch;
	struct uniphier_aio_selector rb;
	struct uniphier_aio_selector iport;
	struct uniphier_aio_selector iif;
	struct uniphier_aio_selector oport;
	struct uniphier_aio_selector oif;
	struct uniphier_aio_selector och;
};

struct uniphier_aio_spec {
	const char *name;
	const char *gname;
	struct uniphier_aio_swmap swm;
};

struct uniphier_aio_pll {
	bool enable;
	unsigned int freq;
};

struct uniphier_aio_chip_spec {
	const struct uniphier_aio_spec *specs;
	int num_specs;
	const struct uniphier_aio_pll *plls;
	int num_plls;
	struct snd_soc_dai_driver *dais;
	int num_dais;

	/* DMA access mode, this is workaround for DMA hungup */
	int addr_ext;
};

struct uniphier_aio_sub {
	struct uniphier_aio *aio;

	/* Guard sub->rd_offs and wr_offs from IRQ handler. */
	spinlock_t lock;

	const struct uniphier_aio_swmap *swm;
	const struct uniphier_aio_spec *spec;

	/* For PCM audio */
	struct snd_pcm_substream *substream;
	struct snd_pcm_hw_params params;
	int vol;

	/* For compress audio */
	struct snd_compr_stream *cstream;
	struct snd_compr_params cparams;
	unsigned char *compr_area;
	dma_addr_t compr_addr;
	size_t compr_bytes;
	int pass_through;
	enum IEC61937_PC iec_pc;
	bool iec_header;

	/* Both PCM and compress audio */
	bool use_mmap;
	int setting;
	int running;
	u64 rd_offs;
	u64 wr_offs;
	u32 threshold;
	u64 rd_org;
	u64 wr_org;
	u64 rd_total;
	u64 wr_total;
};

struct uniphier_aio {
	struct uniphier_aio_chip *chip;

	struct uniphier_aio_sub sub[2];

	unsigned int fmt;
	/* Set one of AUD_CLK_X */
	int clk_in;
	int clk_out;
	/* Set one of AUD_PLL_X */
	int pll_in;
	int pll_out;
	/* Set one of AUD_PLLDIV_X */
	int plldiv;
};

struct uniphier_aio_chip {
	struct platform_device *pdev;
	const struct uniphier_aio_chip_spec *chip_spec;

	struct uniphier_aio *aios;
	int num_aios;
	int num_wup_aios;
	struct uniphier_aio_pll *plls;
	int num_plls;

	struct clk *clk;
	struct reset_control *rst;
	struct regmap *regmap;
	struct regmap *regmap_sg;
	int active;
};

static inline struct uniphier_aio *uniphier_priv(struct snd_soc_dai *dai)
{
	struct uniphier_aio_chip *chip = snd_soc_dai_get_drvdata(dai);

	return &chip->aios[dai->id];
}

int uniphier_aiodma_soc_register_platform(struct platform_device *pdev);
extern const struct snd_compress_ops uniphier_aio_compress_ops;

int uniphier_aio_probe(struct platform_device *pdev);
void uniphier_aio_remove(struct platform_device *pdev);
extern const struct snd_soc_dai_ops uniphier_aio_i2s_ld11_ops;
extern const struct snd_soc_dai_ops uniphier_aio_i2s_pxs2_ops;
extern const struct snd_soc_dai_ops uniphier_aio_spdif_ld11_ops;
extern const struct snd_soc_dai_ops uniphier_aio_spdif_ld11_ops2;
extern const struct snd_soc_dai_ops uniphier_aio_spdif_pxs2_ops;
extern const struct snd_soc_dai_ops uniphier_aio_spdif_pxs2_ops2;

u64 aio_rb_cnt(struct uniphier_aio_sub *sub);
u64 aio_rbt_cnt_to_end(struct uniphier_aio_sub *sub);
u64 aio_rb_space(struct uniphier_aio_sub *sub);
u64 aio_rb_space_to_end(struct uniphier_aio_sub *sub);

void aio_iecout_set_enable(struct uniphier_aio_chip *chip, bool enable);
int aio_chip_set_pll(struct uniphier_aio_chip *chip, int pll_id,
		     unsigned int freq);
void aio_chip_init(struct uniphier_aio_chip *chip);
int aio_init(struct uniphier_aio_sub *sub);
void aio_port_reset(struct uniphier_aio_sub *sub);
int aio_port_set_param(struct uniphier_aio_sub *sub, int pass_through,
		       const struct snd_pcm_hw_params *params);
void aio_port_set_enable(struct uniphier_aio_sub *sub, int enable);
int aio_port_get_volume(struct uniphier_aio_sub *sub);
void aio_port_set_volume(struct uniphier_aio_sub *sub, int vol);
int aio_if_set_param(struct uniphier_aio_sub *sub, int pass_through);
int aio_oport_set_stream_type(struct uniphier_aio_sub *sub,
			      enum IEC61937_PC pc);
void aio_src_reset(struct uniphier_aio_sub *sub);
int aio_src_set_param(struct uniphier_aio_sub *sub,
		      const struct snd_pcm_hw_params *params);
int aio_srcif_set_param(struct uniphier_aio_sub *sub);
int aio_srcch_set_param(struct uniphier_aio_sub *sub);
void aio_srcch_set_enable(struct uniphier_aio_sub *sub, int enable);

int aiodma_ch_set_param(struct uniphier_aio_sub *sub);
void aiodma_ch_set_enable(struct uniphier_aio_sub *sub, int enable);
int aiodma_rb_set_threshold(struct uniphier_aio_sub *sub, u64 size, u32 th);
int aiodma_rb_set_buffer(struct uniphier_aio_sub *sub, u64 start, u64 end,
			 int period);
void aiodma_rb_sync(struct uniphier_aio_sub *sub, u64 start, u64 size,
		    int period);
bool aiodma_rb_is_irq(struct uniphier_aio_sub *sub);
void aiodma_rb_clear_irq(struct uniphier_aio_sub *sub);

#endif /* SND_UNIPHIER_AIO_H__ */
