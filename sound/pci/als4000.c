/*
 *  card-als4000.c - driver for Avance Logic ALS4000 based soundcards.
 *  Copyright (C) 2000 by Bart Hartgers <bart@etpmod.phys.tue.nl>,
 *			  Jaroslav Kysela <perex@suse.cz>
 *  Copyright (C) 2002 by Andreas Mohr <hw7oshyuv3001@sneakemail.com>
 *
 *  Framework borrowed from Massimo Piccioni's card-als100.c.
 *
 * NOTES
 *
 *  Since Avance does not provide any meaningful documentation, and I
 *  bought an ALS4000 based soundcard, I was forced to base this driver
 *  on reverse engineering.
 *
 *  Note: this is no longer true. Pretty verbose chip docu (ALS4000a.PDF)
 *  can be found on the ALSA web site.
 *
 *  The ALS4000 seems to be the PCI-cousin of the ALS100. It contains an
 *  ALS100-like SB DSP/mixer, an OPL3 synth, a MPU401 and a gameport 
 *  interface. These subsystems can be mapped into ISA io-port space, 
 *  using the PCI-interface. In addition, the PCI-bit provides DMA and IRQ 
 *  services to the subsystems.
 * 
 * While ALS4000 is very similar to a SoundBlaster, the differences in
 * DMA and capturing require more changes to the SoundBlaster than
 * desirable, so I made this separate driver.
 * 
 * The ALS4000 can do real full duplex playback/capture.
 *
 * FMDAC:
 * - 0x4f -> port 0x14
 * - port 0x15 |= 1
 *
 * Enable/disable 3D sound:
 * - 0x50 -> port 0x14
 * - change bit 6 (0x40) of port 0x15
 *
 * Set QSound:
 * - 0xdb -> port 0x14
 * - set port 0x15:
 *   0x3e (mode 3), 0x3c (mode 2), 0x3a (mode 1), 0x38 (mode 0)
 *
 * Set KSound:
 * - value -> some port 0x0c0d
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>
#include <sound/initval.h>

MODULE_AUTHOR("Bart Hartgers <bart@etpmod.phys.tue.nl>");
MODULE_DESCRIPTION("Avance Logic ALS4000");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Avance Logic,ALS4000}}");

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
#ifdef SUPPORT_JOYSTICK
static int joystick_port[SNDRV_CARDS];
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ALS4000 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ALS4000 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ALS4000 soundcard.");
#ifdef SUPPORT_JOYSTICK
module_param_array(joystick_port, int, NULL, 0444);
MODULE_PARM_DESC(joystick_port, "Joystick port address for ALS4000 soundcard. (0 = disabled)");
#endif

typedef struct {
	struct pci_dev *pci;
	unsigned long gcr;
#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
} snd_card_als4000_t;

static struct pci_device_id snd_als4000_ids[] = {
	{ 0x4005, 0x4000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* ALS4000 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_als4000_ids);

static inline void snd_als4000_gcr_write_addr(unsigned long port, u32 reg, u32 val)
{
	outb(reg, port+0x0c);
	outl(val, port+0x08);
}

static inline void snd_als4000_gcr_write(sb_t *sb, u32 reg, u32 val)
{
	snd_als4000_gcr_write_addr(sb->alt_port, reg, val);
}	

static inline u32 snd_als4000_gcr_read_addr(unsigned long port, u32 reg)
{
	outb(reg, port+0x0c);
	return inl(port+0x08);
}

static inline u32 snd_als4000_gcr_read(sb_t *sb, u32 reg)
{
	return snd_als4000_gcr_read_addr(sb->alt_port, reg);
}

static void snd_als4000_set_rate(sb_t *chip, unsigned int rate)
{
	if (!(chip->mode & SB_RATE_LOCK)) {
		snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE_OUT);
		snd_sbdsp_command(chip, rate>>8);
		snd_sbdsp_command(chip, rate);
	}
}

static void snd_als4000_set_capture_dma(sb_t *chip, dma_addr_t addr, unsigned size)
{
	snd_als4000_gcr_write(chip, 0xa2, addr);
	snd_als4000_gcr_write(chip, 0xa3, (size-1));
}

static void snd_als4000_set_playback_dma(sb_t *chip, dma_addr_t addr, unsigned size)
{
	snd_als4000_gcr_write(chip, 0x91, addr);
	snd_als4000_gcr_write(chip, 0x92, (size-1)|0x180000);
}

#define ALS4000_FORMAT_SIGNED	(1<<0)
#define ALS4000_FORMAT_16BIT	(1<<1)
#define ALS4000_FORMAT_STEREO	(1<<2)

static int snd_als4000_get_format(snd_pcm_runtime_t *runtime)
{
	int result;

	result = 0;
	if (snd_pcm_format_signed(runtime->format))
		result |= ALS4000_FORMAT_SIGNED;
	if (snd_pcm_format_physical_width(runtime->format) == 16)
		result |= ALS4000_FORMAT_16BIT;
	if (runtime->channels > 1)
		result |= ALS4000_FORMAT_STEREO;
	return result;
}

/* structure for setting up playback */
static struct {
	unsigned char dsp_cmd, dma_on, dma_off, format;
} playback_cmd_vals[]={
/* ALS4000_FORMAT_U8_MONO */
{ SB_DSP4_OUT8_AI, SB_DSP_DMA8_ON, SB_DSP_DMA8_OFF, SB_DSP4_MODE_UNS_MONO },
/* ALS4000_FORMAT_S8_MONO */	
{ SB_DSP4_OUT8_AI, SB_DSP_DMA8_ON, SB_DSP_DMA8_OFF, SB_DSP4_MODE_SIGN_MONO },
/* ALS4000_FORMAT_U16L_MONO */
{ SB_DSP4_OUT16_AI, SB_DSP_DMA16_ON, SB_DSP_DMA16_OFF, SB_DSP4_MODE_UNS_MONO },
/* ALS4000_FORMAT_S16L_MONO */
{ SB_DSP4_OUT16_AI, SB_DSP_DMA16_ON, SB_DSP_DMA16_OFF, SB_DSP4_MODE_SIGN_MONO },
/* ALS4000_FORMAT_U8_STEREO */
{ SB_DSP4_OUT8_AI, SB_DSP_DMA8_ON, SB_DSP_DMA8_OFF, SB_DSP4_MODE_UNS_STEREO },
/* ALS4000_FORMAT_S8_STEREO */	
{ SB_DSP4_OUT8_AI, SB_DSP_DMA8_ON, SB_DSP_DMA8_OFF, SB_DSP4_MODE_SIGN_STEREO },
/* ALS4000_FORMAT_U16L_STEREO */
{ SB_DSP4_OUT16_AI, SB_DSP_DMA16_ON, SB_DSP_DMA16_OFF, SB_DSP4_MODE_UNS_STEREO },
/* ALS4000_FORMAT_S16L_STEREO */
{ SB_DSP4_OUT16_AI, SB_DSP_DMA16_ON, SB_DSP_DMA16_OFF, SB_DSP4_MODE_SIGN_STEREO },
};
#define playback_cmd(chip) (playback_cmd_vals[(chip)->playback_format])

/* structure for setting up capture */
enum { CMD_WIDTH8=0x04, CMD_SIGNED=0x10, CMD_MONO=0x80, CMD_STEREO=0xA0 };
static unsigned char capture_cmd_vals[]=
{
CMD_WIDTH8|CMD_MONO,			/* ALS4000_FORMAT_U8_MONO */
CMD_WIDTH8|CMD_SIGNED|CMD_MONO,		/* ALS4000_FORMAT_S8_MONO */	
CMD_MONO,				/* ALS4000_FORMAT_U16L_MONO */
CMD_SIGNED|CMD_MONO,			/* ALS4000_FORMAT_S16L_MONO */
CMD_WIDTH8|CMD_STEREO,			/* ALS4000_FORMAT_U8_STEREO */
CMD_WIDTH8|CMD_SIGNED|CMD_STEREO,	/* ALS4000_FORMAT_S8_STEREO */	
CMD_STEREO,				/* ALS4000_FORMAT_U16L_STEREO */
CMD_SIGNED|CMD_STEREO,			/* ALS4000_FORMAT_S16L_STEREO */
};	
#define capture_cmd(chip) (capture_cmd_vals[(chip)->capture_format])

static int snd_als4000_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_als4000_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_als4000_capture_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long size;
	unsigned count;

	chip->capture_format = snd_als4000_get_format(runtime);
		
	size = snd_pcm_lib_buffer_bytes(substream);
	count = snd_pcm_lib_period_bytes(substream);
	
	if (chip->capture_format & ALS4000_FORMAT_16BIT)
		count >>=1;
	count--;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_als4000_set_rate(chip, runtime->rate);
	snd_als4000_set_capture_dma(chip, runtime->dma_addr, size);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	spin_lock_irqsave(&chip->mixer_lock, flags );
	snd_sbmixer_write(chip, 0xdc, count);
	snd_sbmixer_write(chip, 0xdd, count>>8);
	spin_unlock_irqrestore(&chip->mixer_lock, flags );
	return 0;
}

static int snd_als4000_playback_prepare(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long size;
	unsigned count;

	chip->playback_format = snd_als4000_get_format(runtime);
	
	size = snd_pcm_lib_buffer_bytes(substream);
	count = snd_pcm_lib_period_bytes(substream);
	
	if (chip->playback_format & ALS4000_FORMAT_16BIT)
		count >>=1;
	count--;
	
	/* FIXME: from second playback on, there's a lot more clicks and pops
	 * involved here than on first playback. Fiddling with
	 * tons of different settings didn't help (DMA, speaker on/off,
	 * reordering, ...). Something seems to get enabled on playback
	 * that I haven't found out how to disable again, which then causes
	 * the switching pops to reach the speakers the next time here. */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_als4000_set_rate(chip, runtime->rate);
	snd_als4000_set_playback_dma(chip, runtime->dma_addr, size);
	
	/* SPEAKER_ON not needed, since dma_on seems to also enable speaker */
	/* snd_sbdsp_command(chip, SB_DSP_SPEAKER_ON); */
	snd_sbdsp_command(chip, playback_cmd(chip).dsp_cmd);
	snd_sbdsp_command(chip, playback_cmd(chip).format);
	snd_sbdsp_command(chip, count);
	snd_sbdsp_command(chip, count>>8);
	snd_sbdsp_command(chip, playback_cmd(chip).dma_off);	
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	
	return 0;
}

static int snd_als4000_capture_trigger(snd_pcm_substream_t * substream, int cmd)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	int result = 0;
	
	spin_lock(&chip->mixer_lock);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		chip->mode |= SB_RATE_LOCK_CAPTURE;
		snd_sbmixer_write(chip, 0xde, capture_cmd(chip));
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		chip->mode &= ~SB_RATE_LOCK_CAPTURE;
		snd_sbmixer_write(chip, 0xde, 0);
	} else {
		result = -EINVAL;
	}
	spin_unlock(&chip->mixer_lock);
	return result;
}

static int snd_als4000_playback_trigger(snd_pcm_substream_t * substream, int cmd)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	int result = 0;

	spin_lock(&chip->reg_lock);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		chip->mode |= SB_RATE_LOCK_PLAYBACK;
		snd_sbdsp_command(chip, playback_cmd(chip).dma_on);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		snd_sbdsp_command(chip, playback_cmd(chip).dma_off);
		chip->mode &= ~SB_RATE_LOCK_PLAYBACK;
	} else {
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static snd_pcm_uframes_t snd_als4000_capture_pointer(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	unsigned int result;

	spin_lock(&chip->reg_lock);	
	result = snd_als4000_gcr_read(chip, 0xa4) & 0xffff;
	spin_unlock(&chip->reg_lock);
	return bytes_to_frames( substream->runtime, result );
}

static snd_pcm_uframes_t snd_als4000_playback_pointer(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	unsigned result;

	spin_lock(&chip->reg_lock);	
	result = snd_als4000_gcr_read(chip, 0xa0) & 0xffff;
	spin_unlock(&chip->reg_lock);
	return bytes_to_frames( substream->runtime, result );
}

static irqreturn_t snd_als4000_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	sb_t *chip = dev_id;
	unsigned gcr_status;
	unsigned sb_status;

	/* find out which bit of the ALS4000 produced the interrupt */
	gcr_status = inb(chip->alt_port + 0xe);

	if ((gcr_status & 0x80) && (chip->playback_substream)) /* playback */
		snd_pcm_period_elapsed(chip->playback_substream);
	if ((gcr_status & 0x40) && (chip->capture_substream)) /* capturing */
		snd_pcm_period_elapsed(chip->capture_substream);
	if ((gcr_status & 0x10) && (chip->rmidi)) /* MPU401 interrupt */
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
	/* release the gcr */
	outb(gcr_status, chip->alt_port + 0xe);
	
	spin_lock(&chip->mixer_lock);
	sb_status = snd_sbmixer_read(chip, SB_DSP4_IRQSTATUS);
	spin_unlock(&chip->mixer_lock);
	
	if (sb_status & SB_IRQTYPE_8BIT) 
		snd_sb_ack_8bit(chip);
	if (sb_status & SB_IRQTYPE_16BIT) 
		snd_sb_ack_16bit(chip);
	if (sb_status & SB_IRQTYPE_MPUIN)
		inb(chip->mpu_port);
	if (sb_status & 0x20)
		inb(SBP(chip, RESET));
	return IRQ_HANDLED;
}

/*****************************************************************/

static snd_pcm_hardware_t snd_als4000_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,	/* formats */
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0
};

static snd_pcm_hardware_t snd_als4000_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE,	/* formats */
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0
};

/*****************************************************************/

static int snd_als4000_playback_open(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->playback_substream = substream;
	runtime->hw = snd_als4000_playback;
	return 0;
}

static int snd_als4000_playback_close(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_als4000_capture_open(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->capture_substream = substream;
	runtime->hw = snd_als4000_capture;
	return 0;
}

static int snd_als4000_capture_close(snd_pcm_substream_t * substream)
{
	sb_t *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	snd_pcm_lib_free_pages(substream);
	return 0;
}

/******************************************************************/

static snd_pcm_ops_t snd_als4000_playback_ops = {
	.open =		snd_als4000_playback_open,
	.close =	snd_als4000_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_als4000_hw_params,
	.hw_free =	snd_als4000_hw_free,
	.prepare =	snd_als4000_playback_prepare,
	.trigger =	snd_als4000_playback_trigger,
	.pointer =	snd_als4000_playback_pointer
};

static snd_pcm_ops_t snd_als4000_capture_ops = {
	.open =		snd_als4000_capture_open,
	.close =	snd_als4000_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_als4000_hw_params,
	.hw_free =	snd_als4000_hw_free,
	.prepare =	snd_als4000_capture_prepare,
	.trigger =	snd_als4000_capture_trigger,
	.pointer =	snd_als4000_capture_pointer
};

static void snd_als4000_pcm_free(snd_pcm_t *pcm)
{
	sb_t *chip = pcm->private_data;
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_als4000_pcm(sb_t *chip, int device)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "ALS4000 DSP", device, 1, 1, &pcm)) < 0)
		return err;
	pcm->private_free = snd_als4000_pcm_free;
	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_als4000_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_als4000_capture_ops);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
					      64*1024, 64*1024);

	chip->pcm = pcm;

	return 0;
}

/******************************************************************/

static void snd_als4000_set_addr(unsigned long gcr,
					unsigned int sb,
					unsigned int mpu,
					unsigned int opl,
					unsigned int game)
{
	u32 confA = 0;
	u32 confB = 0;

	if (mpu > 0)
		confB |= (mpu | 1) << 16;
	if (sb > 0)
		confB |= (sb | 1);
	if (game > 0)
		confA |= (game | 1) << 16;
	if (opl > 0)	
		confA |= (opl | 1);
	snd_als4000_gcr_write_addr(gcr, 0xa8, confA);
	snd_als4000_gcr_write_addr(gcr, 0xa9, confB);
}

static void __devinit snd_als4000_configure(sb_t *chip)
{
	unsigned tmp;
	int i;

	/* do some more configuration */
	spin_lock_irq(&chip->mixer_lock);
	tmp = snd_sbmixer_read(chip, 0xc0);
	snd_sbmixer_write(chip, 0xc0, tmp|0x80);
	/* always select DMA channel 0, since we do not actually use DMA */
	snd_sbmixer_write(chip, SB_DSP4_DMASETUP, SB_DMASETUP_DMA0);
	snd_sbmixer_write(chip, 0xc0, tmp&0x7f);
	spin_unlock_irq(&chip->mixer_lock);
	
	spin_lock_irq(&chip->reg_lock);
	/* magic number. Enables interrupts(?) */
	snd_als4000_gcr_write(chip, 0x8c, 0x28000);
	for(i = 0x91; i <= 0x96; ++i)
		snd_als4000_gcr_write(chip, i, 0);
	
	snd_als4000_gcr_write(chip, 0x99, snd_als4000_gcr_read(chip, 0x99));
	spin_unlock_irq(&chip->reg_lock);
}

#ifdef SUPPORT_JOYSTICK
static int __devinit snd_als4000_create_gameport(snd_card_als4000_t *acard, int dev)
{
	struct gameport *gp;
	struct resource *r;
	int io_port;

	if (joystick_port[dev] == 0)
		return -ENODEV;

	if (joystick_port[dev] == 1) { /* auto-detect */
		for (io_port = 0x200; io_port <= 0x218; io_port += 8) {
			r = request_region(io_port, 8, "ALS4000 gameport");
			if (r)
				break;
		}
	} else {
		io_port = joystick_port[dev];
		r = request_region(io_port, 8, "ALS4000 gameport");
	}

	if (!r) {
		printk(KERN_WARNING "als4000: cannot reserve joystick ports\n");
		return -EBUSY;
	}

	acard->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "als4000: cannot allocate memory for gameport\n");
		release_and_free_resource(r);
		return -ENOMEM;
	}

	gameport_set_name(gp, "ALS4000 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(acard->pci));
	gameport_set_dev_parent(gp, &acard->pci->dev);
	gp->io = io_port;
	gameport_set_port_data(gp, r);

	/* Enable legacy joystick port */
	snd_als4000_set_addr(acard->gcr, 0, 0, 0, 1);

	gameport_register_port(acard->gameport);

	return 0;
}

static void snd_als4000_free_gameport(snd_card_als4000_t *acard)
{
	if (acard->gameport) {
		struct resource *r = gameport_get_port_data(acard->gameport);

		gameport_unregister_port(acard->gameport);
		acard->gameport = NULL;

		snd_als4000_set_addr(acard->gcr, 0, 0, 0, 0); /* disable joystick */
		release_and_free_resource(r);
	}
}
#else
static inline int snd_als4000_create_gameport(snd_card_als4000_t *acard, int dev) { return -ENOSYS; }
static inline void snd_als4000_free_gameport(snd_card_als4000_t *acard) { }
#endif

static void snd_card_als4000_free( snd_card_t *card )
{
	snd_card_als4000_t * acard = (snd_card_als4000_t *)card->private_data;

	/* make sure that interrupts are disabled */
	snd_als4000_gcr_write_addr( acard->gcr, 0x8c, 0);
	/* free resources */
	snd_als4000_free_gameport(acard);
	pci_release_regions(acard->pci);
	pci_disable_device(acard->pci);
}

static int __devinit snd_card_als4000_probe(struct pci_dev *pci,
					  const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	snd_card_als4000_t *acard;
	unsigned long gcr;
	sb_t *chip;
	opl3_t *opl3;
	unsigned short word;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0) {
		return err;
	}
	/* check, if we can restrict PCI DMA transfers to 24 bits */
	if (pci_set_dma_mask(pci, 0x00ffffff) < 0 ||
	    pci_set_consistent_dma_mask(pci, 0x00ffffff) < 0) {
		snd_printk(KERN_ERR "architecture does not support 24bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	if ((err = pci_request_regions(pci, "ALS4000")) < 0) {
		pci_disable_device(pci);
		return err;
	}
	gcr = pci_resource_start(pci, 0);

	pci_read_config_word(pci, PCI_COMMAND, &word);
	pci_write_config_word(pci, PCI_COMMAND, word | PCI_COMMAND_IO);
	pci_set_master(pci);
	
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 
			    sizeof( snd_card_als4000_t ) );
	if (card == NULL) {
		pci_release_regions(pci);
		pci_disable_device(pci);
		return -ENOMEM;
	}

	acard = (snd_card_als4000_t *)card->private_data;
	acard->pci = pci;
	acard->gcr = gcr;
	card->private_free = snd_card_als4000_free;

	/* disable all legacy ISA stuff */
	snd_als4000_set_addr(acard->gcr, 0, 0, 0, 0);

	if ((err = snd_sbdsp_create(card,
				    gcr + 0x10,
				    pci->irq,
				    snd_als4000_interrupt,
				    -1,
				    -1,
				    SB_HW_ALS4000,
				    &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	chip->pci = pci;
	chip->alt_port = gcr;
	snd_card_set_dev(card, &pci->dev);

	snd_als4000_configure(chip);

	strcpy(card->driver, "ALS4000");
	strcpy(card->shortname, "Avance Logic ALS4000");
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->alt_port, chip->irq);

	if ((err = snd_mpu401_uart_new( card, 0, MPU401_HW_ALS4000,
				        gcr+0x30, 1, pci->irq, 0,
				        &chip->rmidi)) < 0) {
		snd_card_free(card);
		printk(KERN_ERR "als4000: no MPU-401device at 0x%lx ?\n", gcr+0x30);
		return err;
	}

	if ((err = snd_als4000_pcm(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return err;
	}	    

	if (snd_opl3_create(card, gcr+0x10, gcr+0x12,
			    OPL3_HW_AUTO, 1, &opl3) < 0) {
		printk(KERN_ERR "als4000: no OPL device at 0x%lx-0x%lx ?\n",
			   gcr+0x10, gcr+0x12 );
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	snd_als4000_create_gameport(acard, dev);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_card_als4000_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "ALS4000",
	.owner = THIS_MODULE,
	.id_table = snd_als4000_ids,
	.probe = snd_card_als4000_probe,
	.remove = __devexit_p(snd_card_als4000_remove),
};

static int __init alsa_card_als4000_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_als4000_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_als4000_init)
module_exit(alsa_card_als4000_exit)
