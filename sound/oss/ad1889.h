#ifndef _AD1889_H_
#define _AD1889_H_

#define AD_DS_WSMC	0x00	/* DMA input wave/syn mixer control */
#define AD_DS_RAMC	0x02	/* DMA output resamp/ADC mixer control */
#define AD_DS_WADA	0x04	/* DMA input wave attenuation */
#define AD_DS_SYDA	0x06	/* DMA input syn attentuation */
#define AD_DS_WAS	0x08	/* wave input sample rate */
#define AD_DS_RES	0x0a	/* resampler output sample rate */
#define AD_DS_CCS	0x0c	/* chip control/status */

#define AD_DMA_RESBA	0x40	/* RES base addr */
#define AD_DMA_RESCA	0x44	/* RES current addr */
#define AD_DMA_RESBC	0x48	/* RES base cnt */
#define AD_DMA_RESCC	0x4c	/* RES current count */
#define AD_DMA_ADCBA	0x50	/* ADC */
#define AD_DMA_ADCCA	0x54
#define AD_DMA_ADCBC	0x58
#define AD_DMA_ADCCC	0x5c
#define AD_DMA_SYNBA	0x60	/* SYN */
#define AD_DMA_SYNCA	0x64
#define AD_DMA_SYNBC	0x68
#define AD_DMA_SYNCC	0x6c
#define AD_DMA_WAVBA	0x70	/* WAV */
#define AD_DMA_WAVCA	0x74
#define AD_DMA_WAVBC	0x78
#define AD_DMA_WAVCC	0x7c
#define AD_DMA_RESICC	0x80	/* RES interrupt current count */
#define AD_DMA_RESIBC	0x84	/* RES interrupt base count */
#define AD_DMA_ADCICC	0x88	/* ADC interrupt current count */
#define AD_DMA_ADCIBC	0x8c	/* ADC interrupt base count */
#define AD_DMA_SYNICC	0x90	/* SYN interrupt current count */
#define AD_DMA_SYNIBC	0x94	/* SYN interrupt base count */
#define AD_DMA_WAVICC	0x98	/* WAV interrupt current count */
#define AD_DMA_WAVIBC	0x9c	/* WAV interrupt base count */
#define AD_DMA_RESCTRL	0xa0	/* RES PCI control/status */
#define AD_DMA_ADCCTRL	0xa8	/* ADC PCI control/status */
#define AD_DMA_SYNCTRL	0xb0	/* SYN PCI control/status */
#define AD_DMA_WAVCTRL	0xb8	/* WAV PCI control/status */
#define AD_DMA_DISR	0xc0	/* PCI DMA intr status */
#define AD_DMA_CHSS	0xc4	/* PCI DMA channel stop status */

#define AD_GPIO_IPC	0xc8	/* IO port ctrl */
#define AD_GPIO_OP	0xca	/* IO output status */
#define AD_GPIO_IP	0xcc	/* IO input status */

/* AC97 registers, 0x100 - 0x17f; see ac97.h */
#define AD_AC97_BASE    0x100   /* ac97 base register */
#define AD_AC97_ACIC	0x180	/* AC Link interface ctrl */

/* OPL3; BAR1 */
#define AD_OPL_M0AS	0x00	/* Music0 address/status */
#define AD_OPL_M0DATA	0x01	/* Music0 data */
#define AD_OPL_M1A	0x02	/* Music1 address */
#define AD_OPL_M1DATA	0x03	/* Music1 data */
/* 0x04-0x0f reserved */

/* MIDI; BAR2 */
#define AD_MIDA		0x00	/* MIDI data */
#define AD_MISC		0x01	/* MIDI status/cmd */
/* 0x02-0xff reserved */

#define AD_DS_IOMEMSIZE	512
#define AD_OPL_MEMSIZE	16
#define AD_MIDI_MEMSIZE	16

#define AD_WAV_STATE	0
#define AD_ADC_STATE	1
#define AD_MAX_STATES	2

#define DMA_SIZE	(128*1024)

#define DMA_FLAG_MAPPED	1

struct ad1889_dev;

typedef struct ad1889_state {
	struct ad1889_dev *card;

	mode_t open_mode;
	struct dmabuf {
		unsigned int rate;
		unsigned char fmt, enable;

		/* buf management */
		size_t rawbuf_size;
		void *rawbuf;
		dma_addr_t dma_handle;	/* mapped address */
		unsigned long dma_len;	/* number of bytes mapped */

		/* indexes into rawbuf for setting up DMA engine */
		volatile unsigned long rd_ptr, wr_ptr;

		wait_queue_head_t wait; /* to wait for buf servicing */

		/* OSS bits */
		unsigned int mapped:1;
		unsigned int ready:1;
		unsigned int ossfragshift;
		int ossmaxfrags;
		unsigned int subdivision;
	} dmabuf;

	struct mutex mutex;
} ad1889_state_t;

typedef struct ad1889_dev {
	void __iomem *regbase;
	struct pci_dev *pci;
	
	spinlock_t lock;

	int dev_audio;

	/* states; one per channel; right now only WAV and ADC */
	struct ad1889_state state[AD_MAX_STATES];

	/* AC97 codec */
	struct ac97_codec *ac97_codec;
	u16 ac97_features;

	/* debugging stuff */
	struct stats {
		unsigned int wav_intrs, adc_intrs;
		unsigned int blocks, underrun, error;
	} stats;
} ad1889_dev_t;

typedef struct ad1889_reg {
	const char *name;
	int offset;
	int width;
} ad1889_reg_t;

#endif
