/*********************************************************************
 *
 * msnd.h
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 *
 * Some parts of this header file were derived from the Turtle Beach
 * MultiSound Driver Development Kit.
 *
 * Copyright (C) 1998 Andrew Veliath
 * Copyright (C) 1993 Turtle Beach Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ********************************************************************/
#ifndef __MSND_H
#define __MSND_H

#define DEFSAMPLERATE		44100
#define DEFSAMPLESIZE		SNDRV_PCM_FORMAT_S16
#define DEFCHANNELS		1

#define SRAM_BANK_SIZE		0x8000
#define SRAM_CNTL_START		0x7F00
#define SMA_STRUCT_START	0x7F40

#define DSP_BASE_ADDR		0x4000
#define DSP_BANK_BASE		0x4000

#define AGND			0x01
#define SIGNAL			0x02

#define EXT_DSP_BIT_DCAL	0x0001
#define EXT_DSP_BIT_MIDI_CON	0x0002

#define BUFFSIZE		0x8000
#define HOSTQ_SIZE		0x40

#define DAP_BUFF_SIZE		0x2400

#define DAPQ_STRUCT_SIZE	0x10
#define DARQ_STRUCT_SIZE	0x10
#define DAPQ_BUFF_SIZE		(3 * 0x10)
#define DARQ_BUFF_SIZE		(3 * 0x10)
#define MODQ_BUFF_SIZE		0x400

#define DAPQ_DATA_BUFF		0x6C00
#define DARQ_DATA_BUFF		0x6C30
#define MODQ_DATA_BUFF		0x6C60
#define MIDQ_DATA_BUFF		0x7060

#define DAPQ_OFFSET		SRAM_CNTL_START
#define DARQ_OFFSET		(SRAM_CNTL_START + 0x08)
#define MODQ_OFFSET		(SRAM_CNTL_START + 0x10)
#define MIDQ_OFFSET		(SRAM_CNTL_START + 0x18)
#define DSPQ_OFFSET		(SRAM_CNTL_START + 0x20)

#define	HP_ICR			0x00
#define	HP_CVR			0x01
#define	HP_ISR			0x02
#define	HP_IVR			0x03
#define HP_NU			0x04
#define HP_INFO			0x04
#define	HP_TXH			0x05
#define	HP_RXH			0x05
#define	HP_TXM			0x06
#define	HP_RXM			0x06
#define	HP_TXL			0x07
#define	HP_RXL			0x07

#define HP_ICR_DEF		0x00
#define HP_CVR_DEF		0x12
#define HP_ISR_DEF		0x06
#define HP_IVR_DEF		0x0f
#define HP_NU_DEF		0x00

#define	HP_IRQM			0x09

#define	HPR_BLRC		0x08
#define	HPR_SPR1		0x09
#define	HPR_SPR2		0x0A
#define	HPR_TCL0		0x0B
#define	HPR_TCL1		0x0C
#define	HPR_TCL2		0x0D
#define	HPR_TCL3		0x0E
#define	HPR_TCL4		0x0F

#define	HPICR_INIT		0x80
#define HPICR_HM1		0x40
#define HPICR_HM0		0x20
#define HPICR_HF1		0x10
#define HPICR_HF0		0x08
#define	HPICR_TREQ		0x02
#define	HPICR_RREQ		0x01

#define HPCVR_HC		0x80

#define	HPISR_HREQ		0x80
#define HPISR_DMA		0x40
#define HPISR_HF3		0x10
#define HPISR_HF2		0x08
#define	HPISR_TRDY		0x04
#define	HPISR_TXDE		0x02
#define	HPISR_RXDF		0x01

#define	HPIO_290		0
#define	HPIO_260		1
#define	HPIO_250		2
#define	HPIO_240		3
#define	HPIO_230		4
#define	HPIO_220		5
#define	HPIO_210		6
#define	HPIO_3E0		7

#define	HPMEM_NONE		0
#define	HPMEM_B000		1
#define	HPMEM_C800		2
#define	HPMEM_D000		3
#define	HPMEM_D400		4
#define	HPMEM_D800		5
#define	HPMEM_E000		6
#define	HPMEM_E800		7

#define	HPIRQ_NONE		0
#define HPIRQ_5			1
#define HPIRQ_7			2
#define HPIRQ_9			3
#define HPIRQ_10		4
#define HPIRQ_11		5
#define HPIRQ_12		6
#define HPIRQ_15		7

#define	HIMT_PLAY_DONE		0x00
#define	HIMT_RECORD_DONE	0x01
#define	HIMT_MIDI_EOS		0x02
#define	HIMT_MIDI_OUT		0x03

#define	HIMT_MIDI_IN_UCHAR	0x0E
#define	HIMT_DSP		0x0F

#define	HDEX_BASE	       	0x92
#define	HDEX_PLAY_START		(0 + HDEX_BASE)
#define	HDEX_PLAY_STOP		(1 + HDEX_BASE)
#define	HDEX_PLAY_PAUSE		(2 + HDEX_BASE)
#define	HDEX_PLAY_RESUME	(3 + HDEX_BASE)
#define	HDEX_RECORD_START	(4 + HDEX_BASE)
#define	HDEX_RECORD_STOP	(5 + HDEX_BASE)
#define	HDEX_MIDI_IN_START 	(6 + HDEX_BASE)
#define	HDEX_MIDI_IN_STOP	(7 + HDEX_BASE)
#define	HDEX_MIDI_OUT_START	(8 + HDEX_BASE)
#define	HDEX_MIDI_OUT_STOP	(9 + HDEX_BASE)
#define	HDEX_AUX_REQ		(10 + HDEX_BASE)

#define	HDEXAR_CLEAR_PEAKS	1
#define	HDEXAR_IN_SET_POTS	2
#define	HDEXAR_AUX_SET_POTS	3
#define	HDEXAR_CAL_A_TO_D	4
#define	HDEXAR_RD_EXT_DSP_BITS	5

/* Pinnacle only HDEXAR defs */
#define	HDEXAR_SET_ANA_IN	0
#define	HDEXAR_SET_SYNTH_IN	4
#define	HDEXAR_READ_DAT_IN	5
#define	HDEXAR_MIC_SET_POTS	6
#define	HDEXAR_SET_DAT_IN	7

#define HDEXAR_SET_SYNTH_48	8
#define HDEXAR_SET_SYNTH_44	9

#define HIWORD(l)		((u16)((((u32)(l)) >> 16) & 0xFFFF))
#define LOWORD(l)		((u16)(u32)(l))
#define HIBYTE(w)		((u8)(((u16)(w) >> 8) & 0xFF))
#define LOBYTE(w)		((u8)(w))
#define MAKELONG(low, hi)	((long)(((u16)(low))|(((u32)((u16)(hi)))<<16)))
#define MAKEWORD(low, hi)	((u16)(((u8)(low))|(((u16)((u8)(hi)))<<8)))

#define PCTODSP_OFFSET(w)	(u16)((w)/2)
#define PCTODSP_BASED(w)	(u16)(((w)/2) + DSP_BASE_ADDR)
#define DSPTOPC_BASED(w)	(((w) - DSP_BASE_ADDR) * 2)

#ifdef SLOWIO
#  undef outb
#  undef inb
#  define outb			outb_p
#  define inb			inb_p
#endif

/* JobQueueStruct */
#define JQS_wStart		0x00
#define JQS_wSize		0x02
#define JQS_wHead		0x04
#define JQS_wTail		0x06
#define JQS__size		0x08

/* DAQueueDataStruct */
#define DAQDS_wStart		0x00
#define DAQDS_wSize		0x02
#define DAQDS_wFormat		0x04
#define DAQDS_wSampleSize	0x06
#define DAQDS_wChannels		0x08
#define DAQDS_wSampleRate	0x0A
#define DAQDS_wIntMsg		0x0C
#define DAQDS_wFlags		0x0E
#define DAQDS__size		0x10

#include <sound/pcm.h>

struct snd_msnd {
	void __iomem		*mappedbase;
	int			play_period_bytes;
	int			playLimit;
	int			playPeriods;
	int 			playDMAPos;
	int			banksPlayed;
	int 			captureDMAPos;
	int			capturePeriodBytes;
	int			captureLimit;
	int			capturePeriods;
	struct snd_card		*card;
	void			*msndmidi_mpu;
	struct snd_rawmidi	*rmidi;

	/* Hardware resources */
	long io;
	int memid, irqid;
	int irq, irq_ref;
	unsigned long base;

	/* Motorola 56k DSP SMA */
	void __iomem	*SMA;
	void __iomem	*DAPQ;
	void __iomem	*DARQ;
	void __iomem	*MODQ;
	void __iomem	*MIDQ;
	void __iomem	*DSPQ;
	int dspq_data_buff, dspq_buff_size;

	/* State variables */
	enum { msndClassic, msndPinnacle } type;
	mode_t mode;
	unsigned long flags;
#define F_RESETTING			0
#define F_HAVEDIGITAL			1
#define F_AUDIO_WRITE_INUSE		2
#define F_WRITING			3
#define F_WRITEBLOCK			4
#define F_WRITEFLUSH			5
#define F_AUDIO_READ_INUSE		6
#define F_READING			7
#define F_READBLOCK			8
#define F_EXT_MIDI_INUSE		9
#define F_HDR_MIDI_INUSE		10
#define F_DISABLE_WRITE_NDELAY		11
	spinlock_t lock;
	spinlock_t mixer_lock;
	int nresets;
	unsigned recsrc;
#define LEVEL_ENTRIES 32
	int left_levels[LEVEL_ENTRIES];
	int right_levels[LEVEL_ENTRIES];
	int calibrate_signal;
	int play_sample_size, play_sample_rate, play_channels;
	int play_ndelay;
	int capture_sample_size, capture_sample_rate, capture_channels;
	int capture_ndelay;
	u8 bCurrentMidiPatch;

	int last_playbank, last_recbank;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;

};

void snd_msnd_init_queue(void *base, int start, int size);

int snd_msnd_send_dsp_cmd(struct snd_msnd *chip, u8 cmd);
int snd_msnd_send_word(struct snd_msnd *chip,
			   unsigned char high,
			   unsigned char mid,
			   unsigned char low);
int snd_msnd_upload_host(struct snd_msnd *chip,
			     const u8 *bin, int len);
int snd_msnd_enable_irq(struct snd_msnd *chip);
int snd_msnd_disable_irq(struct snd_msnd *chip);
void snd_msnd_dsp_halt(struct snd_msnd *chip, struct file *file);
int snd_msnd_DAPQ(struct snd_msnd *chip, int start);
int snd_msnd_DARQ(struct snd_msnd *chip, int start);
int snd_msnd_pcm(struct snd_card *card, int device, struct snd_pcm **rpcm);

int snd_msndmidi_new(struct snd_card *card, int device);
void snd_msndmidi_input_read(void *mpu);

void snd_msndmix_setup(struct snd_msnd *chip);
int __devinit snd_msndmix_new(struct snd_card *card);
int snd_msndmix_force_recsrc(struct snd_msnd *chip, int recsrc);
#endif /* __MSND_H */
