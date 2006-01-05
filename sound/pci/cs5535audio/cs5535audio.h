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
#define ACC_BM2_CMD			0x30
#define ACC_BM3_CMD			0x38
#define ACC_BM4_CMD			0x40
#define ACC_BM5_CMD			0x48
#define ACC_BM6_CMD			0x50
#define ACC_BM7_CMD			0x58
#define ACC_BM0_PRD			0x24
#define ACC_BM1_PRD			0x2C
#define ACC_BM2_PRD			0x34
#define ACC_BM3_PRD			0x3C
#define ACC_BM4_PRD			0x44
#define ACC_BM5_PRD			0x4C
#define ACC_BM6_PRD			0x54
#define ACC_BM7_PRD			0x5C
#define ACC_BM0_STATUS			0x21
#define ACC_BM1_STATUS			0x29
#define ACC_BM2_STATUS			0x31
#define ACC_BM3_STATUS			0x39
#define ACC_BM4_STATUS			0x41
#define ACC_BM5_STATUS			0x49
#define ACC_BM6_STATUS			0x51
#define ACC_BM7_STATUS			0x59
#define ACC_BM0_PNTR			0x60
#define ACC_BM1_PNTR			0x64
#define ACC_BM2_PNTR			0x68
#define ACC_BM3_PNTR			0x6C
#define ACC_BM4_PNTR			0x70
#define ACC_BM5_PNTR			0x74
#define ACC_BM6_PNTR			0x78
#define ACC_BM7_PNTR			0x7C
/* acc_codec bar0 reg bits */
/* ACC_IRQ_STATUS */
#define IRQ_STS 			0
#define WU_IRQ_STS 			1
#define BM0_IRQ_STS 			2
#define BM1_IRQ_STS 			3
#define BM2_IRQ_STS 			4
#define BM3_IRQ_STS 			5
#define BM4_IRQ_STS 			6
#define BM5_IRQ_STS		 	7
#define BM6_IRQ_STS 			8
#define BM7_IRQ_STS 			9
/* ACC_BMX_STATUS */
#define EOP				(1<<0)
#define BM_EOP_ERR			(1<<1)
/* ACC_BMX_CTL */
#define BM_CTL_EN			0x00000001
#define BM_CTL_PAUSE			0x00000011
#define BM_CTL_DIS			0x00000000
#define BM_CTL_BYTE_ORD_LE		0x00000000
#define BM_CTL_BYTE_ORD_BE		0x00000100
/* cs5535 specific ac97 codec register defines */
#define CMD_MASK			0xFF00FFFF
#define CMD_NEW				0x00010000
#define STS_NEW				0x00020000
#define PRM_RDY_STS			0x00800000
#define ACC_CODEC_CNTL_WR_CMD		(~0x80000000)
#define ACC_CODEC_CNTL_RD_CMD		0x80000000
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
};

struct cs5535audio {
	struct snd_card *card;
	struct snd_ac97 *ac97;
	int irq;
	struct pci_dev *pci;
	unsigned long port;
	spinlock_t reg_lock;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	struct cs5535audio_dma dmas[NUM_CS5535AUDIO_DMAS];
};

int __devinit snd_cs5535audio_pcm(struct cs5535audio *cs5535audio);

#endif /* __SOUND_CS5535AUDIO_H */

