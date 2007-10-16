/*
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 *  Driver EMU10K1X chips
 *
 *  Parts of this code were adapted from audigyls.c driver which is
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *
 *  Chips (SB0200 model):
 *    - EMU10K1X-DBQ
 *    - STAC 9708T
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/rawmidi.h>

MODULE_AUTHOR("Francisco Moraes <fmoraes@nc.rr.com>");
MODULE_DESCRIPTION("EMU10K1X");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Dell Creative Labs,SB Live!}");

// module parameters (see "Module Parameters")
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the EMU10K1X soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the EMU10K1X soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the EMU10K1X soundcard.");


// some definitions were borrowed from emu10k1 driver as they seem to be the same
/************************************************************************************************/
/* PCI function 0 registers, address = <val> + PCIBASE0						*/
/************************************************************************************************/

#define PTR			0x00		/* Indexed register set pointer register	*/
						/* NOTE: The CHANNELNUM and ADDRESS words can	*/
						/* be modified independently of each other.	*/

#define DATA			0x04		/* Indexed register set data register		*/

#define IPR			0x08		/* Global interrupt pending register		*/
						/* Clear pending interrupts by writing a 1 to	*/
						/* the relevant bits and zero to the other bits	*/
#define IPR_MIDITRANSBUFEMPTY   0x00000001	/* MIDI UART transmit buffer empty		*/
#define IPR_MIDIRECVBUFEMPTY    0x00000002	/* MIDI UART receive buffer empty		*/
#define IPR_CH_0_LOOP           0x00000800      /* Channel 0 loop                               */
#define IPR_CH_0_HALF_LOOP      0x00000100      /* Channel 0 half loop                          */
#define IPR_CAP_0_LOOP          0x00080000      /* Channel capture loop                         */
#define IPR_CAP_0_HALF_LOOP     0x00010000      /* Channel capture half loop                    */

#define INTE			0x0c		/* Interrupt enable register			*/
#define INTE_MIDITXENABLE       0x00000001	/* Enable MIDI transmit-buffer-empty interrupts	*/
#define INTE_MIDIRXENABLE       0x00000002	/* Enable MIDI receive-buffer-empty interrupts	*/
#define INTE_CH_0_LOOP          0x00000800      /* Channel 0 loop                               */
#define INTE_CH_0_HALF_LOOP     0x00000100      /* Channel 0 half loop                          */
#define INTE_CAP_0_LOOP         0x00080000      /* Channel capture loop                         */
#define INTE_CAP_0_HALF_LOOP    0x00010000      /* Channel capture half loop                    */

#define HCFG			0x14		/* Hardware config register			*/

#define HCFG_LOCKSOUNDCACHE	0x00000008	/* 1 = Cancel bustmaster accesses to soundcache */
						/* NOTE: This should generally never be used.  	*/
#define HCFG_AUDIOENABLE	0x00000001	/* 0 = CODECs transmit zero-valued samples	*/
						/* Should be set to 1 when the EMU10K1 is	*/
						/* completely initialized.			*/
#define GPIO			0x18		/* Defaults: 00001080-Analog, 00001000-SPDIF.   */


#define AC97DATA		0x1c		/* AC97 register set data register (16 bit)	*/

#define AC97ADDRESS		0x1e		/* AC97 register set address register (8 bit)	*/

/********************************************************************************************************/
/* Emu10k1x pointer-offset register set, accessed through the PTR and DATA registers			*/
/********************************************************************************************************/
#define PLAYBACK_LIST_ADDR	0x00		/* Base DMA address of a list of pointers to each period/size */
						/* One list entry: 4 bytes for DMA address, 
						 * 4 bytes for period_size << 16.
						 * One list entry is 8 bytes long.
						 * One list entry for each period in the buffer.
						 */
#define PLAYBACK_LIST_SIZE	0x01		/* Size of list in bytes << 16. E.g. 8 periods -> 0x00380000  */
#define PLAYBACK_LIST_PTR	0x02		/* Pointer to the current period being played */
#define PLAYBACK_DMA_ADDR	0x04		/* Playback DMA addresss */
#define PLAYBACK_PERIOD_SIZE	0x05		/* Playback period size */
#define PLAYBACK_POINTER	0x06		/* Playback period pointer. Sample currently in DAC */
#define PLAYBACK_UNKNOWN1       0x07
#define PLAYBACK_UNKNOWN2       0x08

/* Only one capture channel supported */
#define CAPTURE_DMA_ADDR	0x10		/* Capture DMA address */
#define CAPTURE_BUFFER_SIZE	0x11		/* Capture buffer size */
#define CAPTURE_POINTER		0x12		/* Capture buffer pointer. Sample currently in ADC */
#define CAPTURE_UNKNOWN         0x13

/* From 0x20 - 0x3f, last samples played on each channel */

#define TRIGGER_CHANNEL         0x40            /* Trigger channel playback                     */
#define TRIGGER_CHANNEL_0       0x00000001      /* Trigger channel 0                            */
#define TRIGGER_CHANNEL_1       0x00000002      /* Trigger channel 1                            */
#define TRIGGER_CHANNEL_2       0x00000004      /* Trigger channel 2                            */
#define TRIGGER_CAPTURE         0x00000100      /* Trigger capture channel                      */

#define ROUTING                 0x41            /* Setup sound routing ?                        */
#define ROUTING_FRONT_LEFT      0x00000001
#define ROUTING_FRONT_RIGHT     0x00000002
#define ROUTING_REAR_LEFT       0x00000004
#define ROUTING_REAR_RIGHT      0x00000008
#define ROUTING_CENTER_LFE      0x00010000

#define SPCS0			0x42		/* SPDIF output Channel Status 0 register	*/

#define SPCS1			0x43		/* SPDIF output Channel Status 1 register	*/

#define SPCS2			0x44		/* SPDIF output Channel Status 2 register	*/

#define SPCS_CLKACCYMASK	0x30000000	/* Clock accuracy				*/
#define SPCS_CLKACCY_1000PPM	0x00000000	/* 1000 parts per million			*/
#define SPCS_CLKACCY_50PPM	0x10000000	/* 50 parts per million				*/
#define SPCS_CLKACCY_VARIABLE	0x20000000	/* Variable accuracy				*/
#define SPCS_SAMPLERATEMASK	0x0f000000	/* Sample rate					*/
#define SPCS_SAMPLERATE_44	0x00000000	/* 44.1kHz sample rate				*/
#define SPCS_SAMPLERATE_48	0x02000000	/* 48kHz sample rate				*/
#define SPCS_SAMPLERATE_32	0x03000000	/* 32kHz sample rate				*/
#define SPCS_CHANNELNUMMASK	0x00f00000	/* Channel number				*/
#define SPCS_CHANNELNUM_UNSPEC	0x00000000	/* Unspecified channel number			*/
#define SPCS_CHANNELNUM_LEFT	0x00100000	/* Left channel					*/
#define SPCS_CHANNELNUM_RIGHT	0x00200000	/* Right channel				*/
#define SPCS_SOURCENUMMASK	0x000f0000	/* Source number				*/
#define SPCS_SOURCENUM_UNSPEC	0x00000000	/* Unspecified source number			*/
#define SPCS_GENERATIONSTATUS	0x00008000	/* Originality flag (see IEC-958 spec)		*/
#define SPCS_CATEGORYCODEMASK	0x00007f00	/* Category code (see IEC-958 spec)		*/
#define SPCS_MODEMASK		0x000000c0	/* Mode (see IEC-958 spec)			*/
#define SPCS_EMPHASISMASK	0x00000038	/* Emphasis					*/
#define SPCS_EMPHASIS_NONE	0x00000000	/* No emphasis					*/
#define SPCS_EMPHASIS_50_15	0x00000008	/* 50/15 usec 2 channel				*/
#define SPCS_COPYRIGHT		0x00000004	/* Copyright asserted flag -- do not modify	*/
#define SPCS_NOTAUDIODATA	0x00000002	/* 0 = Digital audio, 1 = not audio		*/
#define SPCS_PROFESSIONAL	0x00000001	/* 0 = Consumer (IEC-958), 1 = pro (AES3-1992)	*/

#define SPDIF_SELECT		0x45		/* Enables SPDIF or Analogue outputs 0-Analogue, 0x700-SPDIF */

/* This is the MPU port on the card                      					*/
#define MUDATA		0x47
#define MUCMD		0x48
#define MUSTAT		MUCMD

/* From 0x50 - 0x5f, last samples captured */

/**
 * The hardware has 3 channels for playback and 1 for capture.
 *  - channel 0 is the front channel
 *  - channel 1 is the rear channel
 *  - channel 2 is the center/lfe chanel
 * Volume is controlled by the AC97 for the front and rear channels by
 * the PCM Playback Volume, Sigmatel Surround Playback Volume and 
 * Surround Playback Volume. The Sigmatel 4-Speaker Stereo switch affects
 * the front/rear channel mixing in the REAR OUT jack. When using the
 * 4-Speaker Stereo, both front and rear channels will be mixed in the
 * REAR OUT.
 * The center/lfe channel has no volume control and cannot be muted during
 * playback.
 */

struct emu10k1x_voice {
	struct emu10k1x *emu;
	int number;
	int use;
  
	struct emu10k1x_pcm *epcm;
};

struct emu10k1x_pcm {
	struct emu10k1x *emu;
	struct snd_pcm_substream *substream;
	struct emu10k1x_voice *voice;
	unsigned short running;
};

struct emu10k1x_midi {
	struct emu10k1x *emu;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *substream_input;
	struct snd_rawmidi_substream *substream_output;
	unsigned int midi_mode;
	spinlock_t input_lock;
	spinlock_t output_lock;
	spinlock_t open_lock;
	int tx_enable, rx_enable;
	int port;
	int ipr_tx, ipr_rx;
	void (*interrupt)(struct emu10k1x *emu, unsigned int status);
};

// definition of the chip-specific record
struct emu10k1x {
	struct snd_card *card;
	struct pci_dev *pci;

	unsigned long port;
	struct resource *res_port;
	int irq;

	unsigned char revision;		/* chip revision */
	unsigned int serial;            /* serial number */
	unsigned short model;		/* subsystem id */

	spinlock_t emu_lock;
	spinlock_t voice_lock;

	struct snd_ac97 *ac97;
	struct snd_pcm *pcm;

	struct emu10k1x_voice voices[3];
	struct emu10k1x_voice capture_voice;
	u32 spdif_bits[3]; // SPDIF out setup

	struct snd_dma_buffer dma_buffer;

	struct emu10k1x_midi midi;
};

/* hardware definition */
static struct snd_pcm_hardware snd_emu10k1x_playback_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(32*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(16*1024),
	.periods_min =		2,
	.periods_max =		8,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_emu10k1x_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(32*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(16*1024),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static unsigned int snd_emu10k1x_ptr_read(struct emu10k1x * emu, 
					  unsigned int reg, 
					  unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
  
	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR);
	val = inl(emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_emu10k1x_ptr_write(struct emu10k1x *emu, 
				   unsigned int reg, 
				   unsigned int chn, 
				   unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR);
	outl(data, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_emu10k1x_intr_enable(struct emu10k1x *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) | intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_emu10k1x_intr_disable(struct emu10k1x *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) & ~intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_emu10k1x_gpio_write(struct emu10k1x *emu, unsigned int value)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(value, emu->port + GPIO);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_emu10k1x_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static void snd_emu10k1x_pcm_interrupt(struct emu10k1x *emu, struct emu10k1x_voice *voice)
{
	struct emu10k1x_pcm *epcm;

	if ((epcm = voice->epcm) == NULL)
		return;
	if (epcm->substream == NULL)
		return;
#if 0
	snd_printk(KERN_INFO "IRQ: position = 0x%x, period = 0x%x, size = 0x%x\n",
		   epcm->substream->ops->pointer(epcm->substream),
		   snd_pcm_lib_period_bytes(epcm->substream),
		   snd_pcm_lib_buffer_bytes(epcm->substream));
#endif
	snd_pcm_period_elapsed(epcm->substream);
}

/* open callback */
static int snd_emu10k1x_playback_open(struct snd_pcm_substream *substream)
{
	struct emu10k1x *chip = snd_pcm_substream_chip(substream);
	struct emu10k1x_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0) {
		return err;
	}
	if ((err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64)) < 0)
                return err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = chip;
	epcm->substream = substream;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1x_pcm_free_substream;
  
	runtime->hw = snd_emu10k1x_playback_hw;

	return 0;
}

/* close callback */
static int snd_emu10k1x_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/* hw_params callback */
static int snd_emu10k1x_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;

	if (! epcm->voice) {
		epcm->voice = &epcm->emu->voices[substream->pcm->device];
		epcm->voice->use = 1;
		epcm->voice->epcm = epcm;
	}

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_emu10k1x_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm;

	if (runtime->private_data == NULL)
		return 0;
	
	epcm = runtime->private_data;

	if (epcm->voice) {
		epcm->voice->use = 0;
		epcm->voice->epcm = NULL;
		epcm->voice = NULL;
	}

	return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_emu10k1x_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;
	int voice = epcm->voice->number;
	u32 *table_base = (u32 *)(emu->dma_buffer.area+1024*voice);
	u32 period_size_bytes = frames_to_bytes(runtime, runtime->period_size);
	int i;
	
	for(i = 0; i < runtime->periods; i++) {
		*table_base++=runtime->dma_addr+(i*period_size_bytes);
		*table_base++=period_size_bytes<<16;
	}

	snd_emu10k1x_ptr_write(emu, PLAYBACK_LIST_ADDR, voice, emu->dma_buffer.addr+1024*voice);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_LIST_SIZE, voice, (runtime->periods - 1) << 19);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_LIST_PTR, voice, 0);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_POINTER, voice, 0);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_UNKNOWN1, voice, 0);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_UNKNOWN2, voice, 0);
	snd_emu10k1x_ptr_write(emu, PLAYBACK_DMA_ADDR, voice, runtime->dma_addr);

	snd_emu10k1x_ptr_write(emu, PLAYBACK_PERIOD_SIZE, voice, frames_to_bytes(runtime, runtime->period_size)<<16);

	return 0;
}

/* trigger callback */
static int snd_emu10k1x_pcm_trigger(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;
	int channel = epcm->voice->number;
	int result = 0;

//	snd_printk(KERN_INFO "trigger - emu10k1x = 0x%x, cmd = %i, pointer = %d\n", (int)emu, cmd, (int)substream->ops->pointer(substream));

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if(runtime->periods == 2)
			snd_emu10k1x_intr_enable(emu, (INTE_CH_0_LOOP | INTE_CH_0_HALF_LOOP) << channel);
		else
			snd_emu10k1x_intr_enable(emu, INTE_CH_0_LOOP << channel);
		epcm->running = 1;
		snd_emu10k1x_ptr_write(emu, TRIGGER_CHANNEL, 0, snd_emu10k1x_ptr_read(emu, TRIGGER_CHANNEL, 0)|(TRIGGER_CHANNEL_0<<channel));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		epcm->running = 0;
		snd_emu10k1x_intr_disable(emu, (INTE_CH_0_LOOP | INTE_CH_0_HALF_LOOP) << channel);
		snd_emu10k1x_ptr_write(emu, TRIGGER_CHANNEL, 0, snd_emu10k1x_ptr_read(emu, TRIGGER_CHANNEL, 0) & ~(TRIGGER_CHANNEL_0<<channel));
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer callback */
static snd_pcm_uframes_t
snd_emu10k1x_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;
	int channel = epcm->voice->number;
	snd_pcm_uframes_t ptr = 0, ptr1 = 0, ptr2= 0,ptr3 = 0,ptr4 = 0;

	if (!epcm->running)
		return 0;

	ptr3 = snd_emu10k1x_ptr_read(emu, PLAYBACK_LIST_PTR, channel);
	ptr1 = snd_emu10k1x_ptr_read(emu, PLAYBACK_POINTER, channel);
	ptr4 = snd_emu10k1x_ptr_read(emu, PLAYBACK_LIST_PTR, channel);

	if(ptr4 == 0 && ptr1 == frames_to_bytes(runtime, runtime->buffer_size))
		return 0;
	
	if (ptr3 != ptr4) 
		ptr1 = snd_emu10k1x_ptr_read(emu, PLAYBACK_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr2 += (ptr4 >> 3) * runtime->period_size;
	ptr = ptr2;

	if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;

	return ptr;
}

/* operators */
static struct snd_pcm_ops snd_emu10k1x_playback_ops = {
	.open =        snd_emu10k1x_playback_open,
	.close =       snd_emu10k1x_playback_close,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_emu10k1x_pcm_hw_params,
	.hw_free =     snd_emu10k1x_pcm_hw_free,
	.prepare =     snd_emu10k1x_pcm_prepare,
	.trigger =     snd_emu10k1x_pcm_trigger,
	.pointer =     snd_emu10k1x_pcm_pointer,
};

/* open_capture callback */
static int snd_emu10k1x_pcm_open_capture(struct snd_pcm_substream *substream)
{
	struct emu10k1x *chip = snd_pcm_substream_chip(substream);
	struct emu10k1x_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
                return err;
	if ((err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64)) < 0)
                return err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;

	epcm->emu = chip;
	epcm->substream = substream;

	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1x_pcm_free_substream;

	runtime->hw = snd_emu10k1x_capture_hw;

	return 0;
}

/* close callback */
static int snd_emu10k1x_pcm_close_capture(struct snd_pcm_substream *substream)
{
	return 0;
}

/* hw_params callback */
static int snd_emu10k1x_pcm_hw_params_capture(struct snd_pcm_substream *substream,
					      struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;

	if (! epcm->voice) {
		if (epcm->emu->capture_voice.use)
			return -EBUSY;
		epcm->voice = &epcm->emu->capture_voice;
		epcm->voice->epcm = epcm;
		epcm->voice->use = 1;
	}

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_emu10k1x_pcm_hw_free_capture(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	struct emu10k1x_pcm *epcm;

	if (runtime->private_data == NULL)
		return 0;
	epcm = runtime->private_data;

	if (epcm->voice) {
		epcm->voice->use = 0;
		epcm->voice->epcm = NULL;
		epcm->voice = NULL;
	}

	return snd_pcm_lib_free_pages(substream);
}

/* prepare capture callback */
static int snd_emu10k1x_pcm_prepare_capture(struct snd_pcm_substream *substream)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_emu10k1x_ptr_write(emu, CAPTURE_DMA_ADDR, 0, runtime->dma_addr);
	snd_emu10k1x_ptr_write(emu, CAPTURE_BUFFER_SIZE, 0, frames_to_bytes(runtime, runtime->buffer_size)<<16); // buffer size in bytes
	snd_emu10k1x_ptr_write(emu, CAPTURE_POINTER, 0, 0);
	snd_emu10k1x_ptr_write(emu, CAPTURE_UNKNOWN, 0, 0);

	return 0;
}

/* trigger_capture callback */
static int snd_emu10k1x_pcm_trigger_capture(struct snd_pcm_substream *substream,
					    int cmd)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;
	int result = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_emu10k1x_intr_enable(emu, INTE_CAP_0_LOOP | 
					 INTE_CAP_0_HALF_LOOP);
		snd_emu10k1x_ptr_write(emu, TRIGGER_CHANNEL, 0, snd_emu10k1x_ptr_read(emu, TRIGGER_CHANNEL, 0)|TRIGGER_CAPTURE);
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		epcm->running = 0;
		snd_emu10k1x_intr_disable(emu, INTE_CAP_0_LOOP | 
					  INTE_CAP_0_HALF_LOOP);
		snd_emu10k1x_ptr_write(emu, TRIGGER_CHANNEL, 0, snd_emu10k1x_ptr_read(emu, TRIGGER_CHANNEL, 0) & ~(TRIGGER_CAPTURE));
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer_capture callback */
static snd_pcm_uframes_t
snd_emu10k1x_pcm_pointer_capture(struct snd_pcm_substream *substream)
{
	struct emu10k1x *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct emu10k1x_pcm *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr;

	if (!epcm->running)
		return 0;

	ptr = bytes_to_frames(runtime, snd_emu10k1x_ptr_read(emu, CAPTURE_POINTER, 0));
	if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;

	return ptr;
}

static struct snd_pcm_ops snd_emu10k1x_capture_ops = {
	.open =        snd_emu10k1x_pcm_open_capture,
	.close =       snd_emu10k1x_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_emu10k1x_pcm_hw_params_capture,
	.hw_free =     snd_emu10k1x_pcm_hw_free_capture,
	.prepare =     snd_emu10k1x_pcm_prepare_capture,
	.trigger =     snd_emu10k1x_pcm_trigger_capture,
	.pointer =     snd_emu10k1x_pcm_pointer_capture,
};

static unsigned short snd_emu10k1x_ac97_read(struct snd_ac97 *ac97,
					     unsigned short reg)
{
	struct emu10k1x *emu = ac97->private_data;
	unsigned long flags;
	unsigned short val;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	val = inw(emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_emu10k1x_ac97_write(struct snd_ac97 *ac97,
				    unsigned short reg, unsigned short val)
{
	struct emu10k1x *emu = ac97->private_data;
	unsigned long flags;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	outw(val, emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static int snd_emu10k1x_ac97(struct emu10k1x *chip)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_emu10k1x_ac97_write,
		.read = snd_emu10k1x_ac97_read,
	};
  
	if ((err = snd_ac97_bus(chip->card, 0, &ops, NULL, &pbus)) < 0)
		return err;
	pbus->no_vra = 1; /* we don't need VRA */

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.scaps = AC97_SCAP_NO_SPDIF;
	return snd_ac97_mixer(pbus, &ac97, &chip->ac97);
}

static int snd_emu10k1x_free(struct emu10k1x *chip)
{
	snd_emu10k1x_ptr_write(chip, TRIGGER_CHANNEL, 0, 0);
	// disable interrupts
	outl(0, chip->port + INTE);
	// disable audio
	outl(HCFG_LOCKSOUNDCACHE, chip->port + HCFG);

	// release the i/o port
	release_and_free_resource(chip->res_port);

	// release the irq
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	// release the DMA
	if (chip->dma_buffer.area) {
		snd_dma_free_pages(&chip->dma_buffer);
	}

	pci_disable_device(chip->pci);

	// release the data
	kfree(chip);
	return 0;
}

static int snd_emu10k1x_dev_free(struct snd_device *device)
{
	struct emu10k1x *chip = device->device_data;
	return snd_emu10k1x_free(chip);
}

static irqreturn_t snd_emu10k1x_interrupt(int irq, void *dev_id)
{
	unsigned int status;

	struct emu10k1x *chip = dev_id;
	struct emu10k1x_voice *pvoice = chip->voices;
	int i;
	int mask;

	status = inl(chip->port + IPR);

	if (! status)
		return IRQ_NONE;

	// capture interrupt
	if (status & (IPR_CAP_0_LOOP | IPR_CAP_0_HALF_LOOP)) {
		struct emu10k1x_voice *pvoice = &chip->capture_voice;
		if (pvoice->use)
			snd_emu10k1x_pcm_interrupt(chip, pvoice);
		else
			snd_emu10k1x_intr_disable(chip, 
						  INTE_CAP_0_LOOP |
						  INTE_CAP_0_HALF_LOOP);
	}
		
	mask = IPR_CH_0_LOOP|IPR_CH_0_HALF_LOOP;
	for (i = 0; i < 3; i++) {
		if (status & mask) {
			if (pvoice->use)
				snd_emu10k1x_pcm_interrupt(chip, pvoice);
			else 
				snd_emu10k1x_intr_disable(chip, mask);
		}
		pvoice++;
		mask <<= 1;
	}
		
	if (status & (IPR_MIDITRANSBUFEMPTY|IPR_MIDIRECVBUFEMPTY)) {
		if (chip->midi.interrupt)
			chip->midi.interrupt(chip, status);
		else
			snd_emu10k1x_intr_disable(chip, INTE_MIDITXENABLE|INTE_MIDIRXENABLE);
	}
		
	// acknowledge the interrupt if necessary
	outl(status, chip->port + IPR);

	// snd_printk(KERN_INFO "interrupt %08x\n", status);
	return IRQ_HANDLED;
}

static int __devinit snd_emu10k1x_pcm(struct emu10k1x *emu, int device, struct snd_pcm **rpcm)
{
	struct snd_pcm *pcm;
	int err;
	int capture = 0;
  
	if (rpcm)
		*rpcm = NULL;
	if (device == 0)
		capture = 1;
	
	if ((err = snd_pcm_new(emu->card, "emu10k1x", device, 1, capture, &pcm)) < 0)
		return err;
  
	pcm->private_data = emu;
	
	switch(device) {
	case 0:
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1x_playback_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1x_capture_ops);
		break;
	case 1:
	case 2:
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1x_playback_ops);
		break;
	}

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	switch(device) {
	case 0:
		strcpy(pcm->name, "EMU10K1X Front");
		break;
	case 1:
		strcpy(pcm->name, "EMU10K1X Rear");
		break;
	case 2:
		strcpy(pcm->name, "EMU10K1X Center/LFE");
		break;
	}
	emu->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(emu->pci), 
					      32*1024, 32*1024);
  
	if (rpcm)
		*rpcm = pcm;
  
	return 0;
}

static int __devinit snd_emu10k1x_create(struct snd_card *card,
					 struct pci_dev *pci,
					 struct emu10k1x **rchip)
{
	struct emu10k1x *chip;
	int err;
	int ch;
	static struct snd_device_ops ops = {
		.dev_free = snd_emu10k1x_dev_free,
	};

	*rchip = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;
	if (pci_set_dma_mask(pci, DMA_28BIT_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_28BIT_MASK) < 0) {
		snd_printk(KERN_ERR "error to set 28bit mask DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	spin_lock_init(&chip->emu_lock);
	spin_lock_init(&chip->voice_lock);
  
	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 8,
					     "EMU10K1X")) == NULL) { 
		snd_printk(KERN_ERR "emu10k1x: cannot allocate the port 0x%lx\n", chip->port);
		snd_emu10k1x_free(chip);
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_emu10k1x_interrupt,
			IRQF_SHARED, "EMU10K1X", chip)) {
		snd_printk(KERN_ERR "emu10k1x: cannot grab irq %d\n", pci->irq);
		snd_emu10k1x_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
  
	if(snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
			       4 * 1024, &chip->dma_buffer) < 0) {
		snd_emu10k1x_free(chip);
		return -ENOMEM;
	}

	pci_set_master(pci);
	/* read revision & serial */
	chip->revision = pci->revision;
	pci_read_config_dword(pci, PCI_SUBSYSTEM_VENDOR_ID, &chip->serial);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &chip->model);
	snd_printk(KERN_INFO "Model %04x Rev %08x Serial %08x\n", chip->model,
		   chip->revision, chip->serial);

	outl(0, chip->port + INTE);	

	for(ch = 0; ch < 3; ch++) {
		chip->voices[ch].emu = chip;
		chip->voices[ch].number = ch;
	}

	/*
	 *  Init to 0x02109204 :
	 *  Clock accuracy    = 0     (1000ppm)
	 *  Sample Rate       = 2     (48kHz)
	 *  Audio Channel     = 1     (Left of 2)
	 *  Source Number     = 0     (Unspecified)
	 *  Generation Status = 1     (Original for Cat Code 12)
	 *  Cat Code          = 12    (Digital Signal Mixer)
	 *  Mode              = 0     (Mode 0)
	 *  Emphasis          = 0     (None)
	 *  CP                = 1     (Copyright unasserted)
	 *  AN                = 0     (Audio data)
	 *  P                 = 0     (Consumer)
	 */
	snd_emu10k1x_ptr_write(chip, SPCS0, 0,
			       chip->spdif_bits[0] = 
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_emu10k1x_ptr_write(chip, SPCS1, 0,
			       chip->spdif_bits[1] = 
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_emu10k1x_ptr_write(chip, SPCS2, 0,
			       chip->spdif_bits[2] = 
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);

	snd_emu10k1x_ptr_write(chip, SPDIF_SELECT, 0, 0x700); // disable SPDIF
	snd_emu10k1x_ptr_write(chip, ROUTING, 0, 0x1003F); // routing
	snd_emu10k1x_gpio_write(chip, 0x1080); // analog mode

	outl(HCFG_LOCKSOUNDCACHE|HCFG_AUDIOENABLE, chip->port+HCFG);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  chip, &ops)) < 0) {
		snd_emu10k1x_free(chip);
		return err;
	}
	*rchip = chip;
	return 0;
}

static void snd_emu10k1x_proc_reg_read(struct snd_info_entry *entry, 
				       struct snd_info_buffer *buffer)
{
	struct emu10k1x *emu = entry->private_data;
	unsigned long value,value1,value2;
	unsigned long flags;
	int i;

	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=4) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inl(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %08lX\n", i, value);
	}
	snd_iprintf(buffer, "\nRegisters\n\n");
	for(i = 0; i <= 0x48; i++) {
		value = snd_emu10k1x_ptr_read(emu, i, 0);
		if(i < 0x10 || (i >= 0x20 && i < 0x40)) {
			value1 = snd_emu10k1x_ptr_read(emu, i, 1);
			value2 = snd_emu10k1x_ptr_read(emu, i, 2);
			snd_iprintf(buffer, "%02X: %08lX %08lX %08lX\n", i, value, value1, value2);
		} else {
			snd_iprintf(buffer, "%02X: %08lX\n", i, value);
		}
	}
}

static void snd_emu10k1x_proc_reg_write(struct snd_info_entry *entry, 
					struct snd_info_buffer *buffer)
{
	struct emu10k1x *emu = entry->private_data;
	char line[64];
	unsigned int reg, channel_id , val;

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x %x", &reg, &channel_id, &val) != 3)
			continue;

		if ((reg < 0x49) && (reg >= 0) && (val <= 0xffffffff) 
		    && (channel_id >= 0) && (channel_id <= 2) )
			snd_emu10k1x_ptr_write(emu, reg, channel_id, val);
	}
}

static int __devinit snd_emu10k1x_proc_init(struct emu10k1x * emu)
{
	struct snd_info_entry *entry;
	
	if(! snd_card_proc_new(emu->card, "emu10k1x_regs", &entry)) {
		snd_info_set_text_ops(entry, emu, snd_emu10k1x_proc_reg_read);
		entry->c.text.write = snd_emu10k1x_proc_reg_write;
		entry->mode |= S_IWUSR;
		entry->private_data = emu;
	}
	
	return 0;
}

#define snd_emu10k1x_shared_spdif_info	snd_ctl_boolean_mono_info

static int snd_emu10k1x_shared_spdif_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct emu10k1x *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = (snd_emu10k1x_ptr_read(emu, SPDIF_SELECT, 0) == 0x700) ? 0 : 1;

	return 0;
}

static int snd_emu10k1x_shared_spdif_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct emu10k1x *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.integer.value[0] ;

	if (val) {
		// enable spdif output
		snd_emu10k1x_ptr_write(emu, SPDIF_SELECT, 0, 0x000);
		snd_emu10k1x_ptr_write(emu, ROUTING, 0, 0x700);
		snd_emu10k1x_gpio_write(emu, 0x1000);
	} else {
		// disable spdif output
		snd_emu10k1x_ptr_write(emu, SPDIF_SELECT, 0, 0x700);
		snd_emu10k1x_ptr_write(emu, ROUTING, 0, 0x1003F);
		snd_emu10k1x_gpio_write(emu, 0x1080);
	}
	return change;
}

static struct snd_kcontrol_new snd_emu10k1x_shared_spdif __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Analog/Digital Output Jack",
	.info =		snd_emu10k1x_shared_spdif_info,
	.get =		snd_emu10k1x_shared_spdif_get,
	.put =		snd_emu10k1x_shared_spdif_put
};

static int snd_emu10k1x_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_emu10k1x_spdif_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct emu10k1x *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.iec958.status[0] = (emu->spdif_bits[idx] >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (emu->spdif_bits[idx] >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (emu->spdif_bits[idx] >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (emu->spdif_bits[idx] >> 24) & 0xff;
	return 0;
}

static int snd_emu10k1x_spdif_get_mask(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static int snd_emu10k1x_spdif_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct emu10k1x *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	int change;
	unsigned int val;

	val = (ucontrol->value.iec958.status[0] << 0) |
		(ucontrol->value.iec958.status[1] << 8) |
		(ucontrol->value.iec958.status[2] << 16) |
		(ucontrol->value.iec958.status[3] << 24);
	change = val != emu->spdif_bits[idx];
	if (change) {
		snd_emu10k1x_ptr_write(emu, SPCS0 + idx, 0, val);
		emu->spdif_bits[idx] = val;
	}
	return change;
}

static struct snd_kcontrol_new snd_emu10k1x_spdif_mask_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.count =	3,
	.info =         snd_emu10k1x_spdif_info,
	.get =          snd_emu10k1x_spdif_get_mask
};

static struct snd_kcontrol_new snd_emu10k1x_spdif_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.count =	3,
	.info =         snd_emu10k1x_spdif_info,
	.get =          snd_emu10k1x_spdif_get,
	.put =          snd_emu10k1x_spdif_put
};

static int __devinit snd_emu10k1x_mixer(struct emu10k1x *emu)
{
	int err;
	struct snd_kcontrol *kctl;
	struct snd_card *card = emu->card;

	if ((kctl = snd_ctl_new1(&snd_emu10k1x_spdif_mask_control, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = snd_ctl_new1(&snd_emu10k1x_shared_spdif, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = snd_ctl_new1(&snd_emu10k1x_spdif_control, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;

	return 0;
}

#define EMU10K1X_MIDI_MODE_INPUT	(1<<0)
#define EMU10K1X_MIDI_MODE_OUTPUT	(1<<1)

static inline unsigned char mpu401_read(struct emu10k1x *emu, struct emu10k1x_midi *mpu, int idx)
{
	return (unsigned char)snd_emu10k1x_ptr_read(emu, mpu->port + idx, 0);
}

static inline void mpu401_write(struct emu10k1x *emu, struct emu10k1x_midi *mpu, int data, int idx)
{
	snd_emu10k1x_ptr_write(emu, mpu->port + idx, 0, data);
}

#define mpu401_write_data(emu, mpu, data)	mpu401_write(emu, mpu, data, 0)
#define mpu401_write_cmd(emu, mpu, data)	mpu401_write(emu, mpu, data, 1)
#define mpu401_read_data(emu, mpu)		mpu401_read(emu, mpu, 0)
#define mpu401_read_stat(emu, mpu)		mpu401_read(emu, mpu, 1)

#define mpu401_input_avail(emu,mpu)	(!(mpu401_read_stat(emu,mpu) & 0x80))
#define mpu401_output_ready(emu,mpu)	(!(mpu401_read_stat(emu,mpu) & 0x40))

#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		0xfe

static void mpu401_clear_rx(struct emu10k1x *emu, struct emu10k1x_midi *mpu)
{
	int timeout = 100000;
	for (; timeout > 0 && mpu401_input_avail(emu, mpu); timeout--)
		mpu401_read_data(emu, mpu);
#ifdef CONFIG_SND_DEBUG
	if (timeout <= 0)
		snd_printk(KERN_ERR "cmd: clear rx timeout (status = 0x%x)\n", mpu401_read_stat(emu, mpu));
#endif
}

/*

 */

static void do_emu10k1x_midi_interrupt(struct emu10k1x *emu,
				       struct emu10k1x_midi *midi, unsigned int status)
{
	unsigned char byte;

	if (midi->rmidi == NULL) {
		snd_emu10k1x_intr_disable(emu, midi->tx_enable | midi->rx_enable);
		return;
	}

	spin_lock(&midi->input_lock);
	if ((status & midi->ipr_rx) && mpu401_input_avail(emu, midi)) {
		if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_INPUT)) {
			mpu401_clear_rx(emu, midi);
		} else {
			byte = mpu401_read_data(emu, midi);
			if (midi->substream_input)
				snd_rawmidi_receive(midi->substream_input, &byte, 1);
		}
	}
	spin_unlock(&midi->input_lock);

	spin_lock(&midi->output_lock);
	if ((status & midi->ipr_tx) && mpu401_output_ready(emu, midi)) {
		if (midi->substream_output &&
		    snd_rawmidi_transmit(midi->substream_output, &byte, 1) == 1) {
			mpu401_write_data(emu, midi, byte);
		} else {
			snd_emu10k1x_intr_disable(emu, midi->tx_enable);
		}
	}
	spin_unlock(&midi->output_lock);
}

static void snd_emu10k1x_midi_interrupt(struct emu10k1x *emu, unsigned int status)
{
	do_emu10k1x_midi_interrupt(emu, &emu->midi, status);
}

static int snd_emu10k1x_midi_cmd(struct emu10k1x * emu,
				  struct emu10k1x_midi *midi, unsigned char cmd, int ack)
{
	unsigned long flags;
	int timeout, ok;

	spin_lock_irqsave(&midi->input_lock, flags);
	mpu401_write_data(emu, midi, 0x00);
	/* mpu401_clear_rx(emu, midi); */

	mpu401_write_cmd(emu, midi, cmd);
	if (ack) {
		ok = 0;
		timeout = 10000;
		while (!ok && timeout-- > 0) {
			if (mpu401_input_avail(emu, midi)) {
				if (mpu401_read_data(emu, midi) == MPU401_ACK)
					ok = 1;
			}
		}
		if (!ok && mpu401_read_data(emu, midi) == MPU401_ACK)
			ok = 1;
	} else {
		ok = 1;
	}
	spin_unlock_irqrestore(&midi->input_lock, flags);
	if (!ok) {
		snd_printk(KERN_ERR "midi_cmd: 0x%x failed at 0x%lx (status = 0x%x, data = 0x%x)!!!\n",
			   cmd, emu->port,
			   mpu401_read_stat(emu, midi),
			   mpu401_read_data(emu, midi));
		return 1;
	}
	return 0;
}

static int snd_emu10k1x_midi_input_open(struct snd_rawmidi_substream *substream)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	unsigned long flags;
	
	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1X_MIDI_MODE_INPUT;
	midi->substream_input = substream;
	if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		if (snd_emu10k1x_midi_cmd(emu, midi, MPU401_RESET, 1))
			goto error_out;
		if (snd_emu10k1x_midi_cmd(emu, midi, MPU401_ENTER_UART, 1))
			goto error_out;
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;

error_out:
	return -EIO;
}

static int snd_emu10k1x_midi_output_open(struct snd_rawmidi_substream *substream)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1X_MIDI_MODE_OUTPUT;
	midi->substream_output = substream;
	if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		if (snd_emu10k1x_midi_cmd(emu, midi, MPU401_RESET, 1))
			goto error_out;
		if (snd_emu10k1x_midi_cmd(emu, midi, MPU401_ENTER_UART, 1))
			goto error_out;
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;

error_out:
	return -EIO;
}

static int snd_emu10k1x_midi_input_close(struct snd_rawmidi_substream *substream)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	unsigned long flags;
	int err = 0;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1x_intr_disable(emu, midi->rx_enable);
	midi->midi_mode &= ~EMU10K1X_MIDI_MODE_INPUT;
	midi->substream_input = NULL;
	if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		err = snd_emu10k1x_midi_cmd(emu, midi, MPU401_RESET, 0);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return err;
}

static int snd_emu10k1x_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	unsigned long flags;
	int err = 0;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1x_intr_disable(emu, midi->tx_enable);
	midi->midi_mode &= ~EMU10K1X_MIDI_MODE_OUTPUT;
	midi->substream_output = NULL;
	if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		err = snd_emu10k1x_midi_cmd(emu, midi, MPU401_RESET, 0);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return err;
}

static void snd_emu10k1x_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	emu = midi->emu;
	snd_assert(emu, return);

	if (up)
		snd_emu10k1x_intr_enable(emu, midi->rx_enable);
	else
		snd_emu10k1x_intr_disable(emu, midi->rx_enable);
}

static void snd_emu10k1x_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct emu10k1x *emu;
	struct emu10k1x_midi *midi = substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return);

	if (up) {
		int max = 4;
		unsigned char byte;
	
		/* try to send some amount of bytes here before interrupts */
		spin_lock_irqsave(&midi->output_lock, flags);
		while (max > 0) {
			if (mpu401_output_ready(emu, midi)) {
				if (!(midi->midi_mode & EMU10K1X_MIDI_MODE_OUTPUT) ||
				    snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					/* no more data */
					spin_unlock_irqrestore(&midi->output_lock, flags);
					return;
				}
				mpu401_write_data(emu, midi, byte);
				max--;
			} else {
				break;
			}
		}
		spin_unlock_irqrestore(&midi->output_lock, flags);
		snd_emu10k1x_intr_enable(emu, midi->tx_enable);
	} else {
		snd_emu10k1x_intr_disable(emu, midi->tx_enable);
	}
}

/*

 */

static struct snd_rawmidi_ops snd_emu10k1x_midi_output =
{
	.open =		snd_emu10k1x_midi_output_open,
	.close =	snd_emu10k1x_midi_output_close,
	.trigger =	snd_emu10k1x_midi_output_trigger,
};

static struct snd_rawmidi_ops snd_emu10k1x_midi_input =
{
	.open =		snd_emu10k1x_midi_input_open,
	.close =	snd_emu10k1x_midi_input_close,
	.trigger =	snd_emu10k1x_midi_input_trigger,
};

static void snd_emu10k1x_midi_free(struct snd_rawmidi *rmidi)
{
	struct emu10k1x_midi *midi = rmidi->private_data;
	midi->interrupt = NULL;
	midi->rmidi = NULL;
}

static int __devinit emu10k1x_midi_init(struct emu10k1x *emu,
					struct emu10k1x_midi *midi, int device, char *name)
{
	struct snd_rawmidi *rmidi;
	int err;

	if ((err = snd_rawmidi_new(emu->card, name, device, 1, 1, &rmidi)) < 0)
		return err;
	midi->emu = emu;
	spin_lock_init(&midi->open_lock);
	spin_lock_init(&midi->input_lock);
	spin_lock_init(&midi->output_lock);
	strcpy(rmidi->name, name);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_emu10k1x_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_emu10k1x_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = midi;
	rmidi->private_free = snd_emu10k1x_midi_free;
	midi->rmidi = rmidi;
	return 0;
}

static int __devinit snd_emu10k1x_midi(struct emu10k1x *emu)
{
	struct emu10k1x_midi *midi = &emu->midi;
	int err;

	if ((err = emu10k1x_midi_init(emu, midi, 0, "EMU10K1X MPU-401 (UART)")) < 0)
		return err;

	midi->tx_enable = INTE_MIDITXENABLE;
	midi->rx_enable = INTE_MIDIRXENABLE;
	midi->port = MUDATA;
	midi->ipr_tx = IPR_MIDITRANSBUFEMPTY;
	midi->ipr_rx = IPR_MIDIRECVBUFEMPTY;
	midi->interrupt = snd_emu10k1x_midi_interrupt;
	return 0;
}

static int __devinit snd_emu10k1x_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct emu10k1x *chip;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_emu10k1x_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_emu10k1x_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_emu10k1x_pcm(chip, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_emu10k1x_pcm(chip, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_emu10k1x_ac97(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_emu10k1x_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	if ((err = snd_emu10k1x_midi(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	snd_emu10k1x_proc_init(chip);

	strcpy(card->driver, "EMU10K1X");
	strcpy(card->shortname, "Dell Sound Blaster Live!");
	sprintf(card->longname, "%s at 0x%lx irq %i",
		card->shortname, chip->port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_emu10k1x_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

// PCI IDs
static struct pci_device_id snd_emu10k1x_ids[] = {
	{ 0x1102, 0x0006, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* Dell OEM version (EMU10K1) */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, snd_emu10k1x_ids);

// pci_driver definition
static struct pci_driver driver = {
	.name = "EMU10K1X",
	.id_table = snd_emu10k1x_ids,
	.probe = snd_emu10k1x_probe,
	.remove = __devexit_p(snd_emu10k1x_remove),
};

// initialization of the module
static int __init alsa_card_emu10k1x_init(void)
{
	return pci_register_driver(&driver);
}

// clean up the module
static void __exit alsa_card_emu10k1x_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_emu10k1x_init)
module_exit(alsa_card_emu10k1x_exit)
