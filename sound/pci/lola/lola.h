/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _LOLA_H
#define _LOLA_H

#define DRVNAME	"snd-lola"
#define SFX	DRVNAME ": "

/*
 * Lola HD Audio Registers BAR0
 */
#define LOLA_BAR0_GCAP		0x00
#define LOLA_BAR0_VMIN		0x02
#define LOLA_BAR0_VMAJ		0x03
#define LOLA_BAR0_OUTPAY	0x04
#define LOLA_BAR0_INPAY		0x06
#define LOLA_BAR0_GCTL		0x08
#define LOLA_BAR0_WAKEEN	0x0c
#define LOLA_BAR0_STATESTS	0x0e
#define LOLA_BAR0_GSTS		0x10
#define LOLA_BAR0_OUTSTRMPAY	0x18
#define LOLA_BAR0_INSTRMPAY	0x1a
#define LOLA_BAR0_INTCTL	0x20
#define LOLA_BAR0_INTSTS	0x24
#define LOLA_BAR0_WALCLK	0x30
#define LOLA_BAR0_SSYNC		0x38

#define LOLA_BAR0_CORBLBASE	0x40
#define LOLA_BAR0_CORBUBASE	0x44
#define LOLA_BAR0_CORBWP	0x48	/* no ULONG access */
#define LOLA_BAR0_CORBRP	0x4a	/* no ULONG access */
#define LOLA_BAR0_CORBCTL	0x4c	/* no ULONG access */
#define LOLA_BAR0_CORBSTS	0x4d	/* UCHAR access only */
#define LOLA_BAR0_CORBSIZE	0x4e	/* no ULONG access */

#define LOLA_BAR0_RIRBLBASE	0x50
#define LOLA_BAR0_RIRBUBASE	0x54
#define LOLA_BAR0_RIRBWP	0x58
#define LOLA_BAR0_RINTCNT	0x5a	/* no ULONG access */
#define LOLA_BAR0_RIRBCTL	0x5c
#define LOLA_BAR0_RIRBSTS	0x5d	/* UCHAR access only */
#define LOLA_BAR0_RIRBSIZE	0x5e	/* no ULONG access */

#define LOLA_BAR0_ICW		0x60
#define LOLA_BAR0_IRR		0x64
#define LOLA_BAR0_ICS		0x68
#define LOLA_BAR0_DPLBASE	0x70
#define LOLA_BAR0_DPUBASE	0x74

/* stream register offsets from stream base 0x80 */
#define LOLA_BAR0_SD0_OFFSET	0x80
#define LOLA_REG0_SD_CTL	0x00
#define LOLA_REG0_SD_STS	0x03
#define LOLA_REG0_SD_LPIB	0x04
#define LOLA_REG0_SD_CBL	0x08
#define LOLA_REG0_SD_LVI	0x0c
#define LOLA_REG0_SD_FIFOW	0x0e
#define LOLA_REG0_SD_FIFOSIZE	0x10
#define LOLA_REG0_SD_FORMAT	0x12
#define LOLA_REG0_SD_BDLPL	0x18
#define LOLA_REG0_SD_BDLPU	0x1c

/*
 * Lola Digigram Registers BAR1
 */
#define LOLA_BAR1_FPGAVER	0x00
#define LOLA_BAR1_DEVER		0x04
#define LOLA_BAR1_UCBMV		0x08
#define LOLA_BAR1_JTAG		0x0c
#define LOLA_BAR1_UARTRX	0x10
#define LOLA_BAR1_UARTTX	0x14
#define LOLA_BAR1_UARTCR	0x18
#define LOLA_BAR1_NVRAMVER	0x1c
#define LOLA_BAR1_CTRLSPI	0x20
#define LOLA_BAR1_DSPI		0x24
#define LOLA_BAR1_AISPI		0x28
#define LOLA_BAR1_GRAN		0x2c

#define LOLA_BAR1_DINTCTL	0x80
#define LOLA_BAR1_DIINTCTL	0x84
#define LOLA_BAR1_DOINTCTL	0x88
#define LOLA_BAR1_LRC		0x90
#define LOLA_BAR1_DINTSTS	0x94
#define LOLA_BAR1_DIINTSTS	0x98
#define LOLA_BAR1_DOINTSTS	0x9c

#define LOLA_BAR1_DSD0_OFFSET	0xa0
#define LOLA_BAR1_DSD_SIZE	0x18

#define LOLA_BAR1_DSDnSTS       0x00
#define LOLA_BAR1_DSDnLPIB      0x04
#define LOLA_BAR1_DSDnCTL       0x08
#define LOLA_BAR1_DSDnLVI       0x0c
#define LOLA_BAR1_DSDnBDPL      0x10
#define LOLA_BAR1_DSDnBDPU      0x14

#define LOLA_BAR1_SSYNC		0x03e8

#define LOLA_BAR1_BOARD_CTRL	0x0f00
#define LOLA_BAR1_BOARD_MODE	0x0f02

#define LOLA_BAR1_SOURCE_GAIN_ENABLE		0x1000
#define LOLA_BAR1_DEST00_MIX_GAIN_ENABLE	0x1004
#define LOLA_BAR1_DEST31_MIX_GAIN_ENABLE	0x1080
#define LOLA_BAR1_SOURCE00_01_GAIN		0x1084
#define LOLA_BAR1_SOURCE30_31_GAIN		0x10c0
#define LOLA_BAR1_SOURCE_GAIN(src) \
	(LOLA_BAR1_SOURCE00_01_GAIN + (src) * 2)
#define LOLA_BAR1_DEST00_MIX00_01_GAIN		0x10c4
#define LOLA_BAR1_DEST00_MIX30_31_GAIN		0x1100
#define LOLA_BAR1_DEST01_MIX00_01_GAIN		0x1104
#define LOLA_BAR1_DEST01_MIX30_31_GAIN		0x1140
#define LOLA_BAR1_DEST31_MIX00_01_GAIN		0x1884
#define LOLA_BAR1_DEST31_MIX30_31_GAIN		0x18c0
#define LOLA_BAR1_MIX_GAIN(dest, mix) \
	(LOLA_BAR1_DEST00_MIX00_01_GAIN + (dest) * 0x40 + (mix) * 2)
#define LOLA_BAR1_ANALOG_CLIP_IN		0x18c4
#define LOLA_BAR1_PEAKMETERS_SOURCE00_01	0x18c8
#define LOLA_BAR1_PEAKMETERS_SOURCE30_31	0x1904
#define LOLA_BAR1_PEAKMETERS_SOURCE(src) \
	(LOLA_BAR1_PEAKMETERS_SOURCE00_01 + (src) * 2)
#define LOLA_BAR1_PEAKMETERS_DEST00_01		0x1908
#define LOLA_BAR1_PEAKMETERS_DEST30_31		0x1944
#define LOLA_BAR1_PEAKMETERS_DEST(dest) \
	(LOLA_BAR1_PEAKMETERS_DEST00_01 + (dest) * 2)
#define LOLA_BAR1_PEAKMETERS_AGC00_01		0x1948
#define LOLA_BAR1_PEAKMETERS_AGC14_15		0x1964
#define LOLA_BAR1_PEAKMETERS_AGC(x) \
	(LOLA_BAR1_PEAKMETERS_AGC00_01 + (x) * 2)

/* GCTL reset bit */
#define LOLA_GCTL_RESET		(1 << 0)
/* GCTL unsolicited response enable bit */
#define LOLA_GCTL_UREN		(1 << 8)

/* CORB/RIRB control, read/write pointer */
#define LOLA_RBCTL_DMA_EN	0x02	/* enable DMA */
#define LOLA_RBCTL_IRQ_EN	0x01	/* enable IRQ */
#define LOLA_RBRWP_CLR		0x8000	/* read/write pointer clear */

#define LOLA_RIRB_EX_UNSOL_EV	0x40000000
#define LOLA_RIRB_EX_ERROR	0x80000000

/* CORB int mask: CMEI[0] */
#define LOLA_CORB_INT_CMEI	0x01
#define LOLA_CORB_INT_MASK	LOLA_CORB_INT_CMEI

/* RIRB int mask: overrun[2], response[0] */
#define LOLA_RIRB_INT_RESPONSE	0x01
#define LOLA_RIRB_INT_OVERRUN	0x04
#define LOLA_RIRB_INT_MASK	(LOLA_RIRB_INT_RESPONSE | LOLA_RIRB_INT_OVERRUN)

/* DINTCTL and DINTSTS */
#define LOLA_DINT_GLOBAL	0x80000000 /* global interrupt enable bit */
#define LOLA_DINT_CTRL		0x40000000 /* controller interrupt enable bit */
#define LOLA_DINT_FIFOERR	0x20000000 /* global fifo error enable bit */
#define LOLA_DINT_MUERR		0x10000000 /* global microcontroller underrun error */

/* DSDnCTL bits */
#define LOLA_DSD_CTL_SRST	0x01	/* stream reset bit */
#define LOLA_DSD_CTL_SRUN	0x02	/* stream DMA start bit */
#define LOLA_DSD_CTL_IOCE	0x04	/* interrupt on completion enable */
#define LOLA_DSD_CTL_DEIE	0x10	/* descriptor error interrupt enable */
#define LOLA_DSD_CTL_VLRCV	0x20	/* valid LRCountValue information in bits 8..31 */
#define LOLA_LRC_MASK		0xffffff00

/* DSDnSTS */
#define LOLA_DSD_STS_BCIS	0x04	/* buffer completion interrupt status */
#define LOLA_DSD_STS_DESE	0x10	/* descriptor error interrupt */
#define LOLA_DSD_STS_FIFORDY	0x20	/* fifo ready */

#define LOLA_CORB_ENTRIES	256

#define MAX_STREAM_IN_COUNT	16
#define MAX_STREAM_OUT_COUNT	16
#define MAX_STREAM_COUNT	16
#define MAX_PINS		MAX_STREAM_COUNT
#define MAX_STREAM_BUFFER_COUNT	16
#define MAX_AUDIO_INOUT_COUNT	16

#define LOLA_CLOCK_TYPE_INTERNAL    0
#define LOLA_CLOCK_TYPE_AES         1
#define LOLA_CLOCK_TYPE_AES_SYNC    2
#define LOLA_CLOCK_TYPE_WORDCLOCK   3
#define LOLA_CLOCK_TYPE_ETHERSOUND  4
#define LOLA_CLOCK_TYPE_VIDEO       5

#define LOLA_CLOCK_FORMAT_NONE      0
#define LOLA_CLOCK_FORMAT_NTSC      1
#define LOLA_CLOCK_FORMAT_PAL       2

#define MAX_SAMPLE_CLOCK_COUNT  48

/* parameters used with mixer widget's mixer capabilities */
#define LOLA_PEAK_METER_CAN_AGC_MASK		1
#define LOLA_PEAK_METER_CAN_ANALOG_CLIP_MASK	2

struct lola_bar {
	unsigned long addr;
	void __iomem *remap_addr;
};

/* CORB/RIRB */
struct lola_rb {
	u32 *buf;		/* CORB/RIRB buffer, 8 byte per each entry */
	dma_addr_t addr;	/* physical address of CORB/RIRB buffer */
	unsigned short rp, wp;	/* read/write pointers */
	int cmds;		/* number of pending requests */
};

/* Pin widget setup */
struct lola_pin {
	unsigned int nid;
	bool is_analog;
	unsigned int amp_mute;
	unsigned int amp_step_size;
	unsigned int amp_num_steps;
	unsigned int amp_offset;
	unsigned int max_level;
	unsigned int config_default_reg;
	unsigned int fixed_gain_list_len;
	unsigned int cur_gain_step;
};

struct lola_pin_array {
	unsigned int num_pins;
	unsigned int num_analog_pins;
	struct lola_pin pins[MAX_PINS];
};

/* Clock widget setup */
struct lola_sample_clock {
	unsigned int type;
	unsigned int format;
	unsigned int freq;
};

struct lola_clock_widget {
	unsigned int nid;
	unsigned int items;
	unsigned int cur_index;
	unsigned int cur_freq;
	bool cur_valid;
	struct lola_sample_clock sample_clock[MAX_SAMPLE_CLOCK_COUNT];
	unsigned int idx_lookup[MAX_SAMPLE_CLOCK_COUNT];
};

#define LOLA_MIXER_DIM      32
struct lola_mixer_array {
	u32 src_gain_enable;
	u32 dest_mix_gain_enable[LOLA_MIXER_DIM];
	u16 src_gain[LOLA_MIXER_DIM];
	u16 dest_mix_gain[LOLA_MIXER_DIM][LOLA_MIXER_DIM];
};

/* Mixer widget setup */
struct lola_mixer_widget {
	unsigned int nid;
	unsigned int caps;
	struct lola_mixer_array __user *array;
	struct lola_mixer_array *array_saved;
	unsigned int src_stream_outs;
	unsigned int src_phys_ins;
	unsigned int dest_stream_ins;
	unsigned int dest_phys_outs;
	unsigned int src_stream_out_ofs;
	unsigned int dest_phys_out_ofs;
	unsigned int src_mask;
	unsigned int dest_mask;
};

/* Audio stream */
struct lola_stream {
	unsigned int nid;	/* audio widget NID */
	unsigned int index;	/* array index */
	unsigned int dsd;	/* DSD index */
	bool can_float;
	struct snd_pcm_substream *substream; /* assigned PCM substream */
	struct lola_stream *master;	/* master stream (for multi-channel) */

	/* buffer setup */
	unsigned int bufsize;
	unsigned int period_bytes;
	unsigned int frags;

	/* format + channel setup */
	unsigned int format_verb;

	/* flags */
	unsigned int opened:1;
	unsigned int prepared:1;
	unsigned int paused:1;
	unsigned int running:1;
};

#define PLAY	SNDRV_PCM_STREAM_PLAYBACK
#define CAPT	SNDRV_PCM_STREAM_CAPTURE

struct lola_pcm {
	unsigned int num_streams;
	struct snd_dma_buffer bdl; /* BDL buffer */
	struct lola_stream streams[MAX_STREAM_COUNT];
};

/* card instance */
struct lola {
	struct snd_card *card;
	struct pci_dev *pci;

	/* pci resources */
	struct lola_bar bar[2];
	int irq;

	/* locks */
	spinlock_t reg_lock;
	struct mutex open_mutex;

	/* CORB/RIRB */
	struct lola_rb corb;
	struct lola_rb rirb;
	unsigned int res, res_ex;	/* last read values */
	/* last command (for debugging) */
	unsigned int last_cmd_nid, last_verb, last_data, last_extdata;

	/* CORB/RIRB buffers */
	struct snd_dma_buffer rb;

	/* unsolicited events */
	unsigned int last_unsol_res;

	/* streams */
	struct lola_pcm pcm[2];

	/* input src */
	unsigned int input_src_caps_mask;
	unsigned int input_src_mask;

	/* pins */
	struct lola_pin_array pin[2];

	/* clock */
	struct lola_clock_widget clock;
	int ref_count_rate;
	unsigned int sample_rate;

	/* mixer */
	struct lola_mixer_widget mixer;

	/* hw info */
	unsigned int version;
	unsigned int lola_caps;

	/* parameters */
	unsigned int granularity;
	unsigned int sample_rate_min;
	unsigned int sample_rate_max;

	/* flags */
	unsigned int initialized:1;
	unsigned int cold_reset:1;
	unsigned int polling_mode:1;

	/* for debugging */
	unsigned int debug_res;
	unsigned int debug_res_ex;
};

#define BAR0	0
#define BAR1	1

/* Helper macros */
#define lola_readl(chip, idx, name) \
	readl((chip)->bar[idx].remap_addr + LOLA_##idx##_##name)
#define lola_readw(chip, idx, name) \
	readw((chip)->bar[idx].remap_addr + LOLA_##idx##_##name)
#define lola_readb(chip, idx, name) \
	readb((chip)->bar[idx].remap_addr + LOLA_##idx##_##name)
#define lola_writel(chip, idx, name, val) \
	writel((val), (chip)->bar[idx].remap_addr + LOLA_##idx##_##name)
#define lola_writew(chip, idx, name, val) \
	writew((val), (chip)->bar[idx].remap_addr + LOLA_##idx##_##name)
#define lola_writeb(chip, idx, name, val) \
	writeb((val), (chip)->bar[idx].remap_addr + LOLA_##idx##_##name)

#define lola_dsd_read(chip, dsd, name) \
	readl((chip)->bar[BAR1].remap_addr + LOLA_BAR1_DSD0_OFFSET + \
	      (LOLA_BAR1_DSD_SIZE * (dsd)) + LOLA_BAR1_DSDn##name)
#define lola_dsd_write(chip, dsd, name, val) \
	writel((val), (chip)->bar[BAR1].remap_addr + LOLA_BAR1_DSD0_OFFSET + \
	       (LOLA_BAR1_DSD_SIZE * (dsd)) + LOLA_BAR1_DSDn##name)

/* GET verbs HDAudio */
#define LOLA_VERB_GET_STREAM_FORMAT		0xa00
#define LOLA_VERB_GET_AMP_GAIN_MUTE		0xb00
#define LOLA_VERB_PARAMETERS			0xf00
#define LOLA_VERB_GET_POWER_STATE		0xf05
#define LOLA_VERB_GET_CONV			0xf06
#define LOLA_VERB_GET_UNSOLICITED_RESPONSE	0xf08
#define LOLA_VERB_GET_DIGI_CONVERT_1		0xf0d
#define LOLA_VERB_GET_CONFIG_DEFAULT		0xf1c
#define LOLA_VERB_GET_SUBSYSTEM_ID		0xf20
/* GET verbs Digigram */
#define LOLA_VERB_GET_FIXED_GAIN		0xfc0
#define LOLA_VERB_GET_GAIN_SELECT		0xfc1
#define LOLA_VERB_GET_MAX_LEVEL			0xfc2
#define LOLA_VERB_GET_CLOCK_LIST		0xfc3
#define LOLA_VERB_GET_CLOCK_SELECT		0xfc4
#define LOLA_VERB_GET_CLOCK_STATUS		0xfc5

/* SET verbs HDAudio */
#define LOLA_VERB_SET_STREAM_FORMAT		0x200
#define LOLA_VERB_SET_AMP_GAIN_MUTE		0x300
#define LOLA_VERB_SET_POWER_STATE		0x705
#define LOLA_VERB_SET_CHANNEL_STREAMID		0x706
#define LOLA_VERB_SET_UNSOLICITED_ENABLE	0x708
#define LOLA_VERB_SET_DIGI_CONVERT_1		0x70d
/* SET verbs Digigram */
#define LOLA_VERB_SET_GAIN_SELECT		0xf81
#define LOLA_VERB_SET_CLOCK_SELECT		0xf84
#define LOLA_VERB_SET_GRANULARITY_STEPS		0xf86
#define LOLA_VERB_SET_SOURCE_GAIN		0xf87
#define LOLA_VERB_SET_MIX_GAIN			0xf88
#define LOLA_VERB_SET_DESTINATION_GAIN		0xf89
#define LOLA_VERB_SET_SRC			0xf8a

/* Parameter IDs used with LOLA_VERB_PARAMETERS */
#define LOLA_PAR_VENDOR_ID			0x00
#define LOLA_PAR_FUNCTION_TYPE			0x05
#define LOLA_PAR_AUDIO_WIDGET_CAP		0x09
#define LOLA_PAR_PCM				0x0a
#define LOLA_PAR_STREAM_FORMATS			0x0b
#define LOLA_PAR_PIN_CAP			0x0c
#define LOLA_PAR_AMP_IN_CAP			0x0d
#define LOLA_PAR_CONNLIST_LEN			0x0e
#define LOLA_PAR_POWER_STATE			0x0f
#define LOLA_PAR_GPIO_CAP			0x11
#define LOLA_PAR_AMP_OUT_CAP			0x12
#define LOLA_PAR_SPECIFIC_CAPS			0x80
#define LOLA_PAR_FIXED_GAIN_LIST		0x81

/* extract results of LOLA_PAR_SPECIFIC_CAPS */
#define LOLA_AFG_MIXER_WIDGET_PRESENT(res)	((res & (1 << 21)) != 0)
#define LOLA_AFG_CLOCK_WIDGET_PRESENT(res)	((res & (1 << 20)) != 0)
#define LOLA_AFG_INPUT_PIN_COUNT(res)		((res >> 10) & 0x2ff)
#define LOLA_AFG_OUTPUT_PIN_COUNT(res)		((res) & 0x2ff)

/* extract results of LOLA_PAR_AMP_IN_CAP / LOLA_PAR_AMP_OUT_CAP */
#define LOLA_AMP_MUTE_CAPABLE(res)		((res & (1 << 31)) != 0)
#define LOLA_AMP_STEP_SIZE(res)			((res >> 24) & 0x7f)
#define LOLA_AMP_NUM_STEPS(res)			((res >> 12) & 0x3ff)
#define LOLA_AMP_OFFSET(res)			((res) & 0x3ff)

#define LOLA_GRANULARITY_MIN		8
#define LOLA_GRANULARITY_MAX		32
#define LOLA_GRANULARITY_STEP		8

/* parameters used with unsolicited command/response */
#define LOLA_UNSOLICITED_TAG_MASK	0x3f
#define LOLA_UNSOLICITED_TAG		0x1a
#define LOLA_UNSOLICITED_ENABLE		0x80
#define LOLA_UNSOL_RESP_TAG_OFFSET	26

/* count values in the Vendor Specific Mixer Widget's Audio Widget Capabilities */
#define LOLA_MIXER_SRC_INPUT_PLAY_SEPARATION(res)   ((res >> 2) & 0x1f)
#define LOLA_MIXER_DEST_REC_OUTPUT_SEPATATION(res)  ((res >> 7) & 0x1f)

int lola_codec_write(struct lola *chip, unsigned int nid, unsigned int verb,
		     unsigned int data, unsigned int extdata);
int lola_codec_read(struct lola *chip, unsigned int nid, unsigned int verb,
		    unsigned int data, unsigned int extdata,
		    unsigned int *val, unsigned int *extval);
int lola_codec_flush(struct lola *chip);
#define lola_read_param(chip, nid, param, val) \
	lola_codec_read(chip, nid, LOLA_VERB_PARAMETERS, param, 0, val, NULL)

/* PCM */
int lola_create_pcm(struct lola *chip);
void lola_free_pcm(struct lola *chip);
int lola_init_pcm(struct lola *chip, int dir, int *nidp);
void lola_pcm_update(struct lola *chip, struct lola_pcm *pcm, unsigned int bits);

/* clock */
int lola_init_clock_widget(struct lola *chip, int nid);
int lola_set_granularity(struct lola *chip, unsigned int val, bool force);
int lola_enable_clock_events(struct lola *chip);
int lola_set_clock_index(struct lola *chip, unsigned int idx);
int lola_set_clock(struct lola *chip, int idx);
int lola_set_sample_rate(struct lola *chip, int rate);
bool lola_update_ext_clock_freq(struct lola *chip, unsigned int val);
unsigned int lola_sample_rate_convert(unsigned int coded);

/* mixer */
int lola_init_pins(struct lola *chip, int dir, int *nidp);
int lola_init_mixer_widget(struct lola *chip, int nid);
void lola_free_mixer(struct lola *chip);
int lola_create_mixer(struct lola *chip);
int lola_setup_all_analog_gains(struct lola *chip, int dir, bool mute);
void lola_save_mixer(struct lola *chip);
void lola_restore_mixer(struct lola *chip);
int lola_set_src_config(struct lola *chip, unsigned int src_mask, bool update);

/* proc */
#ifdef CONFIG_SND_DEBUG
void lola_proc_debug_new(struct lola *chip);
#else
#define lola_proc_debug_new(chip)
#endif

#endif /* _LOLA_H */
