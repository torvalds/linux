#ifndef _AD1889_H_
#define _AD1889_H_

#define AD_DSWSMC	0x00	/* DMA input wave/syn mixer control */
#define AD_DSRAMC	0x02	/* DMA output resamp/ADC mixer control */
#define AD_DSWADA	0x04	/* DMA input wave attenuation */
#define AD_DSSYDA	0x06	/* DMA input syn attentuation */
#define AD_DSWAS	0x08	/* wave input sample rate */
#define AD_DSRES	0x0a	/* resampler output sample rate */
#define AD_DSCCS	0x0c	/* chip control/status */

#define AD_DMARESBA	0x40	/* RES base addr */
#define AD_DMARESCA	0x44	/* RES current addr */
#define AD_DMARESBC	0x48	/* RES base cnt */
#define AD_DMARESCC	0x4c	/* RES current count */
#define AD_DMAADCBA	0x50	/* ADC */
#define AD_DMAADCCA	0x54
#define AD_DMAADCBC	0x58
#define AD_DMAADCCC	0x5c
#define AD_DMASYNBA	0x60	/* SYN */
#define AD_DMASYNCA	0x64
#define AD_DMASYNBC	0x68
#define AD_DMASYNCC	0x6c
#define AD_DMAWAVBA	0x70	/* WAV */
#define AD_DMAWAVCA	0x74
#define AD_DMAWAVBC	0x78
#define AD_DMAWAVCC	0x7c
#define AD_DMARESICC	0x80	/* RES interrupt current count */
#define AD_DMARESIBC	0x84	/* RES interrupt base count */
#define AD_DMAADCICC	0x88	/* ADC interrupt current count */
#define AD_DMAADCIBC	0x8c	/* ADC interrupt base count */
#define AD_DMASYNICC	0x90	/* SYN interrupt current count */
#define AD_DMASYNIBC	0x94	/* SYN interrupt base count */
#define AD_DMAWAVICC	0x98	/* WAV interrupt current count */
#define AD_DMAWAVIBC	0x9c	/* WAV interrupt base count */
#define AD_DMARESCTRL	0xa0	/* RES PCI control/status */
#define AD_DMAADCCTRL	0xa8	/* ADC PCI control/status */
#define AD_DMASYNCTRL	0xb0	/* SYN PCI control/status */
#define AD_DMAWAVCTRL	0xb8	/* WAV PCI control/status */
#define AD_DMADISR	0xc0	/* PCI DMA intr status */
#define AD_DMACHSS	0xc4	/* PCI DMA channel stop status */

#define AD_GPIOIPC	0xc8	/* IO port ctrl */
#define AD_GPIOOP	0xca	/* IO output status */
#define AD_GPIOIP	0xcc	/* IO input status */

/* AC97 registers, 0x100 - 0x17f; see ac97.h */
#define AD_ACIC		0x180	/* AC Link interface ctrl */

/* OPL3; BAR1 */
#define AD_OPLM0AS	0x00	/* Music0 address/status */
#define AD_OPLM0DATA	0x01	/* Music0 data */
#define AD_OPLM1A	0x02	/* Music1 address */
#define AD_OPLM1DATA	0x03	/* Music1 data */
/* 0x04-0x0f reserved */

/* MIDI; BAR2 */
#define AD_MIDA		0x00	/* MIDI data */
#define AD_MISC		0x01	/* MIDI status/cmd */
/* 0x02-0xff reserved */

#define AD_DSIOMEMSIZE	512
#define AD_OPLMEMSIZE	16
#define AD_MIDIMEMSIZE	16

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

	struct semaphore sem;
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
