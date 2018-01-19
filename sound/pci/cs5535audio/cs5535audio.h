/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_CS5535AUDIO_H
#define __SOUND_CS5535AUDIO_H

#define cs_writel(cs5535au, reg, val)	outl(val, (cs5535au)->port + reg)
#define cs_writeb(cs5535au, reg, val)	outb(val, (cs5535au)->port + reg)
#define cs_readl(cs5535au, reg)		inl((cs5535au)->port + reg)
#define cs_readw(cs5535au, reg)		inw((cs5535au)->port + reg)
#define cs_readb(cs5535au, reg)		inb((cs5535au)->port + reg)

#define CS5535AUDIO_MAX_DESCRIPTORS	128

/* acc_codec bar0 reg addrs */
#define ACC_GPIO_STATUS			0x00
#define ACC_CODEC_STATUS		0x08
#define ACC_CODEC_CNTL			0x0C
#define ACC_IRQ_STATUS			0x12
#define ACC_BM0_CMD			0x20
#define ACC_BM1_CMD			0x28
#define ACC_BM0_PRD			0x24
#define ACC_BM1_PRD			0x2C
#define ACC_BM0_STATUS			0x21
#define ACC_BM1_STATUS			0x29
#define ACC_BM0_PNTR			0x60
#define ACC_BM1_PNTR			0x64

/* acc_codec bar0 reg bits */
/* ACC_IRQ_STATUS */
#define IRQ_STS 			0
#define WU_IRQ_STS 			1
#define BM0_IRQ_STS 			2
#define BM1_IRQ_STS 			3
/* ACC_BMX_STATUS */
#define EOP				(1<<0)
#define BM_EOP_ERR			(1<<1)
/* ACC_BMX_CTL */
#define BM_CTL_EN			0x01
#define BM_CTL_PAUSE			0x03
#define BM_CTL_DIS			0x00
#define BM_CTL_BYTE_ORD_LE		0x00
#define BM_CTL_BYTE_ORD_BE		0x04
/* cs5535 specific ac97 codec register defines */
#define CMD_MASK			0xFF00FFFF
#define CMD_NEW				0x00010000
#define STS_NEW				0x00020000
#define PRM_RDY_STS			0x00800000
#define ACC_CODEC_CNTL_WR_CMD		(~0x80000000)
#define ACC_CODEC_CNTL_RD_CMD		0x80000000
#define ACC_CODEC_CNTL_LNK_SHUTDOWN	0x00040000
#define ACC_CODEC_CNTL_LNK_WRM_RST	0x00020000
#define PRD_JMP				0x2000
#define PRD_EOP				0x4000
#define PRD_EOT				0x8000

enum { CS5535AUDIO_DMA_PLAYBACK, CS5535AUDIO_DMA_CAPTURE, NUM_CS5535AUDIO_DMAS };

struct cs5535audio;

struct cs5535audio_dma_ops {
	int type;
	void (*enable_dma)(struct cs5535audio *cs5535au);
	void (*disable_dma)(struct cs5535audio *cs5535au);
	void (*pause_dma)(struct cs5535audio *cs5535au);
	void (*setup_prd)(struct cs5535audio *cs5535au, u32 prd_addr);
	u32 (*read_prd)(struct cs5535audio *cs5535au);
	u32 (*read_dma_pntr)(struct cs5535audio *cs5535au);
};

struct cs5535audio_dma_desc {
	u32 addr;
	u16 size;
	u16 ctlreserved;
};

struct cs5535audio_dma {
	const struct cs5535audio_dma_ops *ops;
	struct snd_dma_buffer desc_buf;
	struct snd_pcm_substream *substream;
	unsigned int buf_addr, buf_bytes;
	unsigned int period_bytes, periods;
	u32 saved_prd;
	int pcm_open_flag;
};

struct cs5535audio {
	struct snd_card *card;
	struct snd_ac97 *ac97;
	struct snd_pcm *pcm;
	int irq;
	struct pci_dev *pci;
	unsigned long port;
	spinlock_t reg_lock;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	struct cs5535audio_dma dmas[NUM_CS5535AUDIO_DMAS];
};

extern const struct dev_pm_ops snd_cs5535audio_pm;

#ifdef CONFIG_OLPC
void olpc_prequirks(struct snd_card *card,
		    struct snd_ac97_template *ac97);
int olpc_quirks(struct snd_card *card, struct snd_ac97 *ac97);
void olpc_quirks_cleanup(void);
void olpc_analog_input(struct snd_ac97 *ac97, int on);
void olpc_mic_bias(struct snd_ac97 *ac97, int on);

static inline void olpc_capture_open(struct snd_ac97 *ac97)
{
	/* default to Analog Input off */
	olpc_analog_input(ac97, 0);
	/* enable MIC Bias for recording */
	olpc_mic_bias(ac97, 1);
}

static inline void olpc_capture_close(struct snd_ac97 *ac97)
{
	/* disable Analog Input */
	olpc_analog_input(ac97, 0);
	/* disable the MIC Bias (so the recording LED turns off) */
	olpc_mic_bias(ac97, 0);
}
#else
static inline void olpc_prequirks(struct snd_card *card,
		struct snd_ac97_template *ac97) { }
static inline int olpc_quirks(struct snd_card *card, struct snd_ac97 *ac97)
{
	return 0;
}
static inline void olpc_quirks_cleanup(void) { }
static inline void olpc_analog_input(struct snd_ac97 *ac97, int on) { }
static inline void olpc_mic_bias(struct snd_ac97 *ac97, int on) { }
static inline void olpc_capture_open(struct snd_ac97 *ac97) { }
static inline void olpc_capture_close(struct snd_ac97 *ac97) { }
#endif

int snd_cs5535audio_pcm(struct cs5535audio *cs5535audio);

#endif /* __SOUND_CS5535AUDIO_H */

