/* Analog Devices 1889 audio driver
 * Copyright (C) 2004, Kyle McMartin <kyle@parisc-linux.org>
 */

#ifndef __AD1889_H__
#define __AD1889_H__

#define AD_DS_WSMC	0x00 /* wave/synthesis channel mixer control */
#define  AD_DS_WSMC_SYEN 0x0004 /* synthesis channel enable */
#define  AD_DS_WSMC_SYRQ 0x0030 /* synth. fifo request point */
#define  AD_DS_WSMC_WA16 0x0100 /* wave channel 16bit select */
#define  AD_DS_WSMC_WAST 0x0200 /* wave channel stereo select */
#define  AD_DS_WSMC_WAEN 0x0400 /* wave channel enable */
#define  AD_DS_WSMC_WARQ 0x3000 /* wave fifo request point */

#define AD_DS_RAMC	0x02 /* resampler/ADC channel mixer control */
#define  AD_DS_RAMC_AD16 0x0001 /* ADC channel 16bit select */
#define  AD_DS_RAMC_ADST 0x0002 /* ADC channel stereo select */
#define  AD_DS_RAMC_ADEN 0x0004 /* ADC channel enable */
#define  AD_DS_RAMC_ACRQ 0x0030 /* ADC fifo request point */
#define  AD_DS_RAMC_REEN 0x0400 /* resampler channel enable */
#define  AD_DS_RAMC_RERQ 0x3000 /* res. fifo request point */

#define AD_DS_WADA	0x04 /* wave channel mix attenuation */
#define  AD_DS_WADA_RWAM 0x0080 /* right wave mute */
#define  AD_DS_WADA_RWAA 0x001f /* right wave attenuation */
#define  AD_DS_WADA_LWAM 0x8000 /* left wave mute */
#define  AD_DS_WADA_LWAA 0x3e00 /* left wave attenuation */

#define AD_DS_SYDA	0x06 /* synthesis channel mix attenuation */
#define  AD_DS_SYDA_RSYM 0x0080 /* right synthesis mute */
#define  AD_DS_SYDA_RSYA 0x001f /* right synthesis attenuation */
#define  AD_DS_SYDA_LSYM 0x8000 /* left synthesis mute */
#define  AD_DS_SYDA_LSYA 0x3e00 /* left synthesis attenuation */

#define AD_DS_WAS	0x08 /* wave channel sample rate */
#define  AD_DS_WAS_WAS   0xffff /* sample rate mask */

#define AD_DS_RES	0x0a /* resampler channel sample rate */
#define  AD_DS_RES_RES   0xffff /* sample rate mask */

#define AD_DS_CCS	0x0c /* chip control/status */
#define  AD_DS_CCS_ADO   0x0001 /* ADC channel overflow */
#define  AD_DS_CCS_REO   0x0002 /* resampler channel overflow */
#define  AD_DS_CCS_SYU   0x0004 /* synthesis channel underflow */
#define  AD_DS_CCS_WAU   0x0008 /* wave channel underflow */
/* bits 4 -> 7, 9, 11 -> 14 reserved */
#define  AD_DS_CCS_XTD   0x0100 /* xtd delay control (4096 clock cycles) */
#define  AD_DS_CCS_PDALL 0x0400 /* power */
#define  AD_DS_CCS_CLKEN 0x8000 /* clock */

#define AD_DMA_RESBA	0x40 /* RES base address */
#define AD_DMA_RESCA	0x44 /* RES current address */
#define AD_DMA_RESBC	0x48 /* RES base count */
#define AD_DMA_RESCC	0x4c /* RES current count */

#define AD_DMA_ADCBA	0x50 /* ADC base address */
#define AD_DMA_ADCCA	0x54 /* ADC current address */
#define AD_DMA_ADCBC	0x58 /* ADC base count */
#define AD_DMA_ADCCC	0x5c /* ADC current count */

#define AD_DMA_SYNBA	0x60 /* synth base address */
#define AD_DMA_SYNCA	0x64 /* synth current address */
#define AD_DMA_SYNBC	0x68 /* synth base count */
#define AD_DMA_SYNCC	0x6c /* synth current count */

#define AD_DMA_WAVBA	0x70 /* wave base address */
#define AD_DMA_WAVCA	0x74 /* wave current address */
#define AD_DMA_WAVBC	0x78 /* wave base count */
#define AD_DMA_WAVCC	0x7c /* wave current count */

#define AD_DMA_RESIC	0x80 /* RES dma interrupt current byte count */
#define AD_DMA_RESIB	0x84 /* RES dma interrupt base byte count */

#define AD_DMA_ADCIC	0x88 /* ADC dma interrupt current byte count */
#define AD_DMA_ADCIB	0x8c /* ADC dma interrupt base byte count */

#define AD_DMA_SYNIC	0x90 /* synth dma interrupt current byte count */
#define AD_DMA_SYNIB	0x94 /* synth dma interrupt base byte count */

#define AD_DMA_WAVIC	0x98 /* wave dma interrupt current byte count */
#define AD_DMA_WAVIB	0x9c /* wave dma interrupt base byte count */

#define  AD_DMA_ICC	0xffffff /* current byte count mask */
#define  AD_DMA_IBC	0xffffff /* base byte count mask */
/* bits 24 -> 31 reserved */

/* 4 bytes pad */
#define AD_DMA_ADC	0xa8	/* ADC      dma control and status */
#define AD_DMA_SYNTH	0xb0	/* Synth    dma control and status */
#define AD_DMA_WAV	0xb8	/* wave     dma control and status */
#define AD_DMA_RES	0xa0	/* Resample dma control and status */

#define  AD_DMA_SGDE	0x0001 /* SGD mode enable */
#define  AD_DMA_LOOP	0x0002 /* loop enable */
#define  AD_DMA_IM	0x000c /* interrupt mode mask */
#define  AD_DMA_IM_DIS	(~AD_DMA_IM)	/* disable */
#define  AD_DMA_IM_CNT	0x0004 /* interrupt on count */
#define  AD_DMA_IM_SGD	0x0008 /* interrupt on SGD flag */
#define  AD_DMA_IM_EOL	0x000c /* interrupt on End of Linked List */
#define  AD_DMA_SGDS	0x0030 /* SGD status */
#define  AD_DMA_SFLG	0x0040 /* SGD flag */
#define  AD_DMA_EOL	0x0080 /* SGD end of list */
/* bits 8 -> 15 reserved */

#define AD_DMA_DISR	0xc0 /* dma interrupt status */
#define  AD_DMA_DISR_RESI 0x000001 /* resampler channel interrupt */
#define  AD_DMA_DISR_ADCI 0x000002 /* ADC channel interrupt */
#define  AD_DMA_DISR_SYNI 0x000004 /* synthesis channel interrupt */
#define  AD_DMA_DISR_WAVI 0x000008 /* wave channel interrupt */
/* bits 4, 5 reserved */
#define  AD_DMA_DISR_SEPS 0x000040 /* serial eeprom status */
/* bits 7 -> 13 reserved */
#define  AD_DMA_DISR_PMAI 0x004000 /* pci master abort interrupt */
#define  AD_DMA_DISR_PTAI 0x008000 /* pci target abort interrupt */
#define  AD_DMA_DISR_PTAE 0x010000 /* pci target abort interrupt enable */
#define  AD_DMA_DISR_PMAE 0x020000 /* pci master abort interrupt enable */
/* bits 19 -> 31 reserved */

/* interrupt mask */
#define  AD_INTR_MASK     (AD_DMA_DISR_RESI|AD_DMA_DISR_ADCI| \
                           AD_DMA_DISR_WAVI|AD_DMA_DISR_SYNI| \
                           AD_DMA_DISR_PMAI|AD_DMA_DISR_PTAI)

#define AD_DMA_CHSS	0xc4 /* dma channel stop status */
#define  AD_DMA_CHSS_RESS 0x000001 /* resampler channel stopped */
#define  AD_DMA_CHSS_ADCS 0x000002 /* ADC channel stopped */
#define  AD_DMA_CHSS_SYNS 0x000004 /* synthesis channel stopped */
#define  AD_DMA_CHSS_WAVS 0x000008 /* wave channel stopped */

#define AD_GPIO_IPC	0xc8	/* gpio port control */
#define AD_GPIO_OP	0xca	/* gpio output port status */
#define AD_GPIO_IP	0xcc	/* gpio  input port status */

#define AD_AC97_BASE	0x100	/* ac97 base register */

#define AD_AC97_RESET   0x100   /* reset */

#define AD_AC97_PWR_CTL	0x126	/* == AC97_POWERDOWN */
#define  AD_AC97_PWR_ADC 0x0001 /* ADC ready status */
#define  AD_AC97_PWR_DAC 0x0002 /* DAC ready status */
#define  AD_AC97_PWR_PR0 0x0100 /* PR0 (ADC) powerdown */
#define  AD_AC97_PWR_PR1 0x0200 /* PR1 (DAC) powerdown */

#define AD_MISC_CTL     0x176 /* misc control */
#define  AD_MISC_CTL_DACZ   0x8000 /* set for zero fill, unset for repeat */
#define  AD_MISC_CTL_ARSR   0x0001 /* set for SR1, unset for SR0 */
#define  AD_MISC_CTL_ALSR   0x0100
#define  AD_MISC_CTL_DLSR   0x0400
#define  AD_MISC_CTL_DRSR   0x0004

#define AD_AC97_SR0     0x178 /* sample rate 0, 0xbb80 == 48K */
#define  AD_AC97_SR0_48K 0xbb80 /* 48KHz */
#define AD_AC97_SR1     0x17a /* sample rate 1 */

#define AD_AC97_ACIC	0x180 /* ac97 codec interface control */
#define  AD_AC97_ACIC_ACIE  0x0001 /* analog codec interface enable */
#define  AD_AC97_ACIC_ACRD  0x0002 /* analog codec reset disable */
#define  AD_AC97_ACIC_ASOE  0x0004 /* audio stream output enable */
#define  AD_AC97_ACIC_VSRM  0x0008 /* variable sample rate mode */
#define  AD_AC97_ACIC_FSDH  0x0100 /* force SDATA_OUT high */
#define  AD_AC97_ACIC_FSYH  0x0200 /* force sync high */
#define  AD_AC97_ACIC_ACRDY 0x8000 /* analog codec ready status */
/* bits 10 -> 14 reserved */


#define AD_DS_MEMSIZE	512
#define AD_OPL_MEMSIZE	16
#define AD_MIDI_MEMSIZE	16

#define AD_WAV_STATE	0
#define AD_ADC_STATE	1
#define AD_MAX_STATES	2

#define AD_CHAN_WAV	0x0001
#define AD_CHAN_ADC	0x0002
#define AD_CHAN_RES	0x0004
#define AD_CHAN_SYN	0x0008


/* The chip would support 4 GB buffers and 16 MB periods,
 * but let's not overdo it ... */
#define BUFFER_BYTES_MAX	(256 * 1024)
#define PERIOD_BYTES_MIN	32
#define PERIOD_BYTES_MAX	(BUFFER_BYTES_MAX / 2)
#define PERIODS_MIN		2
#define PERIODS_MAX		(BUFFER_BYTES_MAX / PERIOD_BYTES_MIN)

#endif /* __AD1889_H__ */
