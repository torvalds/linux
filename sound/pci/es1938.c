/*
 *  Driver for ESS Solo-1 (ES1938, ES1946, ES1969) soundcard
 *  Copyright (c) by Jaromir Koutek <miri@punknet.cz>,
 *                   Jaroslav Kysela <perex@perex.cz>,
 *                   Thomas Sailer <sailer@ife.ee.ethz.ch>,
 *                   Abramo Bagnara <abramo@alsa-project.org>,
 *                   Markus Gruber <gruber@eikon.tum.de>
 * 
 * Rewritten from sonicvibes.c source.
 *
 *  TODO:
 *    Rewrite better spinlocks
 *
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

/*
  NOTES:
  - Capture data is written unaligned starting from dma_base + 1 so I need to
    disable mmap and to add a copy callback.
  - After several cycle of the following:
    while : ; do arecord -d1 -f cd -t raw | aplay -f cd ; done
    a "playback write error (DMA or IRQ trouble?)" may happen.
    This is due to playback interrupts not generated.
    I suspect a timing issue.
  - Sometimes the interrupt handler is invoked wrongly during playback.
    This generates some harmless "Unexpected hw_pointer: wrong interrupt
    acknowledge".
    I've seen that using small period sizes.
    Reproducible with:
    mpg123 test.mp3 &
    hdparm -t -T /dev/hda
*/


#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/opl3.h>
#include <sound/mpu401.h>
#include <sound/initval.h>
#include <sound/tlv.h>

MODULE_AUTHOR("Jaromir Koutek <miri@punknet.cz>");
MODULE_DESCRIPTION("ESS Solo-1");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,ES1938},"
                "{ESS,ES1946},"
                "{ESS,ES1969},"
		"{TerraTec,128i PCI}}");

#if IS_REACHABLE(CONFIG_GAMEPORT)
#define SUPPORT_JOYSTICK 1
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ESS Solo-1 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ESS Solo-1 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ESS Solo-1 soundcard.");

#define SLIO_REG(chip, x) ((chip)->io_port + ESSIO_REG_##x)

#define SLDM_REG(chip, x) ((chip)->ddma_port + ESSDM_REG_##x)

#define SLSB_REG(chip, x) ((chip)->sb_port + ESSSB_REG_##x)

#define SL_PCI_LEGACYCONTROL		0x40
#define SL_PCI_CONFIG			0x50
#define SL_PCI_DDMACONTROL		0x60

#define ESSIO_REG_AUDIO2DMAADDR		0
#define ESSIO_REG_AUDIO2DMACOUNT	4
#define ESSIO_REG_AUDIO2MODE		6
#define ESSIO_REG_IRQCONTROL		7

#define ESSDM_REG_DMAADDR		0x00
#define ESSDM_REG_DMACOUNT		0x04
#define ESSDM_REG_DMACOMMAND		0x08
#define ESSDM_REG_DMASTATUS		0x08
#define ESSDM_REG_DMAMODE		0x0b
#define ESSDM_REG_DMACLEAR		0x0d
#define ESSDM_REG_DMAMASK		0x0f

#define ESSSB_REG_FMLOWADDR		0x00
#define ESSSB_REG_FMHIGHADDR		0x02
#define ESSSB_REG_MIXERADDR		0x04
#define ESSSB_REG_MIXERDATA		0x05

#define ESSSB_IREG_AUDIO1		0x14
#define ESSSB_IREG_MICMIX		0x1a
#define ESSSB_IREG_RECSRC		0x1c
#define ESSSB_IREG_MASTER		0x32
#define ESSSB_IREG_FM			0x36
#define ESSSB_IREG_AUXACD		0x38
#define ESSSB_IREG_AUXB			0x3a
#define ESSSB_IREG_PCSPEAKER		0x3c
#define ESSSB_IREG_LINE			0x3e
#define ESSSB_IREG_SPATCONTROL		0x50
#define ESSSB_IREG_SPATLEVEL		0x52
#define ESSSB_IREG_MASTER_LEFT		0x60
#define ESSSB_IREG_MASTER_RIGHT		0x62
#define ESSSB_IREG_MPU401CONTROL	0x64
#define ESSSB_IREG_MICMIXRECORD		0x68
#define ESSSB_IREG_AUDIO2RECORD		0x69
#define ESSSB_IREG_AUXACDRECORD		0x6a
#define ESSSB_IREG_FMRECORD		0x6b
#define ESSSB_IREG_AUXBRECORD		0x6c
#define ESSSB_IREG_MONO			0x6d
#define ESSSB_IREG_LINERECORD		0x6e
#define ESSSB_IREG_MONORECORD		0x6f
#define ESSSB_IREG_AUDIO2SAMPLE		0x70
#define ESSSB_IREG_AUDIO2MODE		0x71
#define ESSSB_IREG_AUDIO2FILTER		0x72
#define ESSSB_IREG_AUDIO2TCOUNTL	0x74
#define ESSSB_IREG_AUDIO2TCOUNTH	0x76
#define ESSSB_IREG_AUDIO2CONTROL1	0x78
#define ESSSB_IREG_AUDIO2CONTROL2	0x7a
#define ESSSB_IREG_AUDIO2		0x7c

#define ESSSB_REG_RESET			0x06

#define ESSSB_REG_READDATA		0x0a
#define ESSSB_REG_WRITEDATA		0x0c
#define ESSSB_REG_READSTATUS		0x0c

#define ESSSB_REG_STATUS		0x0e

#define ESS_CMD_EXTSAMPLERATE		0xa1
#define ESS_CMD_FILTERDIV		0xa2
#define ESS_CMD_DMACNTRELOADL		0xa4
#define ESS_CMD_DMACNTRELOADH		0xa5
#define ESS_CMD_ANALOGCONTROL		0xa8
#define ESS_CMD_IRQCONTROL		0xb1
#define ESS_CMD_DRQCONTROL		0xb2
#define ESS_CMD_RECLEVEL		0xb4
#define ESS_CMD_SETFORMAT		0xb6
#define ESS_CMD_SETFORMAT2		0xb7
#define ESS_CMD_DMACONTROL		0xb8
#define ESS_CMD_DMATYPE			0xb9
#define ESS_CMD_OFFSETLEFT		0xba	
#define ESS_CMD_OFFSETRIGHT		0xbb
#define ESS_CMD_READREG			0xc0
#define ESS_CMD_ENABLEEXT		0xc6
#define ESS_CMD_PAUSEDMA		0xd0
#define ESS_CMD_ENABLEAUDIO1		0xd1
#define ESS_CMD_STOPAUDIO1		0xd3
#define ESS_CMD_AUDIO1STATUS		0xd8
#define ESS_CMD_CONTDMA			0xd4
#define ESS_CMD_TESTIRQ			0xf2

#define ESS_RECSRC_MIC		0
#define ESS_RECSRC_AUXACD	2
#define ESS_RECSRC_AUXB		5
#define ESS_RECSRC_LINE		6
#define ESS_RECSRC_NONE		7

#define DAC1 0x01
#define ADC1 0x02
#define DAC2 0x04

/*

 */

#define SAVED_REG_SIZE	32 /* max. number of registers to save */

struct es1938 {
	int irq;

	unsigned long io_port;
	unsigned long sb_port;
	unsigned long vc_port;
	unsigned long mpu_port;
	unsigned long game_port;
	unsigned long ddma_port;

	unsigned char irqmask;
	unsigned char revision;

	struct snd_kcontrol *hw_volume;
	struct snd_kcontrol *hw_switch;
	struct snd_kcontrol *master_volume;
	struct snd_kcontrol *master_switch;

	struct pci_dev *pci;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *capture_substream;
	struct snd_pcm_substream *playback1_substream;
	struct snd_pcm_substream *playback2_substream;
	struct snd_rawmidi *rmidi;

	unsigned int dma1_size;
	unsigned int dma2_size;
	unsigned int dma1_start;
	unsigned int dma2_start;
	unsigned int dma1_shift;
	unsigned int dma2_shift;
	unsigned int last_capture_dmaaddr;
	unsigned int active;

	spinlock_t reg_lock;
	spinlock_t mixer_lock;
        struct snd_info_entry *proc_entry;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
#ifdef CONFIG_PM_SLEEP
	unsigned char saved_regs[SAVED_REG_SIZE];
#endif
};

static irqreturn_t snd_es1938_interrupt(int irq, void *dev_id);

static const struct pci_device_id snd_es1938_ids[] = {
	{ PCI_VDEVICE(ESS, 0x1969), 0, },   /* Solo-1 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_es1938_ids);

#define RESET_LOOP_TIMEOUT	0x10000
#define WRITE_LOOP_TIMEOUT	0x10000
#define GET_LOOP_TIMEOUT	0x01000

/* -----------------------------------------------------------------
 * Write to a mixer register
 * -----------------------------------------------------------------*/
static void snd_es1938_mixer_write(struct es1938 *chip, unsigned char reg, unsigned char val)
{
	unsigned long flags;
	spin_lock_irqsave(&chip->mixer_lock, flags);
	outb(reg, SLSB_REG(chip, MIXERADDR));
	outb(val, SLSB_REG(chip, MIXERDATA));
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	dev_dbg(chip->card->dev, "Mixer reg %02x set to %02x\n", reg, val);
}

/* -----------------------------------------------------------------
 * Read from a mixer register
 * -----------------------------------------------------------------*/
static int snd_es1938_mixer_read(struct es1938 *chip, unsigned char reg)
{
	int data;
	unsigned long flags;
	spin_lock_irqsave(&chip->mixer_lock, flags);
	outb(reg, SLSB_REG(chip, MIXERADDR));
	data = inb(SLSB_REG(chip, MIXERDATA));
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	dev_dbg(chip->card->dev, "Mixer reg %02x now is %02x\n", reg, data);
	return data;
}

/* -----------------------------------------------------------------
 * Write to some bits of a mixer register (return old value)
 * -----------------------------------------------------------------*/
static int snd_es1938_mixer_bits(struct es1938 *chip, unsigned char reg,
				 unsigned char mask, unsigned char val)
{
	unsigned long flags;
	unsigned char old, new, oval;
	spin_lock_irqsave(&chip->mixer_lock, flags);
	outb(reg, SLSB_REG(chip, MIXERADDR));
	old = inb(SLSB_REG(chip, MIXERDATA));
	oval = old & mask;
	if (val != oval) {
		new = (old & ~mask) | (val & mask);
		outb(new, SLSB_REG(chip, MIXERDATA));
		dev_dbg(chip->card->dev,
			"Mixer reg %02x was %02x, set to %02x\n",
			   reg, old, new);
	}
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	return oval;
}

/* -----------------------------------------------------------------
 * Write command to Controller Registers
 * -----------------------------------------------------------------*/
static void snd_es1938_write_cmd(struct es1938 *chip, unsigned char cmd)
{
	int i;
	unsigned char v;
	for (i = 0; i < WRITE_LOOP_TIMEOUT; i++) {
		if (!(v = inb(SLSB_REG(chip, READSTATUS)) & 0x80)) {
			outb(cmd, SLSB_REG(chip, WRITEDATA));
			return;
		}
	}
	dev_err(chip->card->dev,
		"snd_es1938_write_cmd timeout (0x02%x/0x02%x)\n", cmd, v);
}

/* -----------------------------------------------------------------
 * Read the Read Data Buffer
 * -----------------------------------------------------------------*/
static int snd_es1938_get_byte(struct es1938 *chip)
{
	int i;
	unsigned char v;
	for (i = GET_LOOP_TIMEOUT; i; i--)
		if ((v = inb(SLSB_REG(chip, STATUS))) & 0x80)
			return inb(SLSB_REG(chip, READDATA));
	dev_err(chip->card->dev, "get_byte timeout: status 0x02%x\n", v);
	return -ENODEV;
}

/* -----------------------------------------------------------------
 * Write value cmd register
 * -----------------------------------------------------------------*/
static void snd_es1938_write(struct es1938 *chip, unsigned char reg, unsigned char val)
{
	unsigned long flags;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1938_write_cmd(chip, reg);
	snd_es1938_write_cmd(chip, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	dev_dbg(chip->card->dev, "Reg %02x set to %02x\n", reg, val);
}

/* -----------------------------------------------------------------
 * Read data from cmd register and return it
 * -----------------------------------------------------------------*/
static unsigned char snd_es1938_read(struct es1938 *chip, unsigned char reg)
{
	unsigned char val;
	unsigned long flags;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1938_write_cmd(chip, ESS_CMD_READREG);
	snd_es1938_write_cmd(chip, reg);
	val = snd_es1938_get_byte(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	dev_dbg(chip->card->dev, "Reg %02x now is %02x\n", reg, val);
	return val;
}

/* -----------------------------------------------------------------
 * Write data to cmd register and return old value
 * -----------------------------------------------------------------*/
static int snd_es1938_bits(struct es1938 *chip, unsigned char reg, unsigned char mask,
			   unsigned char val)
{
	unsigned long flags;
	unsigned char old, new, oval;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1938_write_cmd(chip, ESS_CMD_READREG);
	snd_es1938_write_cmd(chip, reg);
	old = snd_es1938_get_byte(chip);
	oval = old & mask;
	if (val != oval) {
		snd_es1938_write_cmd(chip, reg);
		new = (old & ~mask) | (val & mask);
		snd_es1938_write_cmd(chip, new);
		dev_dbg(chip->card->dev, "Reg %02x was %02x, set to %02x\n",
			   reg, old, new);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return oval;
}

/* --------------------------------------------------------------------
 * Reset the chip
 * --------------------------------------------------------------------*/
static void snd_es1938_reset(struct es1938 *chip)
{
	int i;

	outb(3, SLSB_REG(chip, RESET));
	inb(SLSB_REG(chip, RESET));
	outb(0, SLSB_REG(chip, RESET));
	for (i = 0; i < RESET_LOOP_TIMEOUT; i++) {
		if (inb(SLSB_REG(chip, STATUS)) & 0x80) {
			if (inb(SLSB_REG(chip, READDATA)) == 0xaa)
				goto __next;
		}
	}
	dev_err(chip->card->dev, "ESS Solo-1 reset failed\n");

     __next:
	snd_es1938_write_cmd(chip, ESS_CMD_ENABLEEXT);

	/* Demand transfer DMA: 4 bytes per DMA request */
	snd_es1938_write(chip, ESS_CMD_DMATYPE, 2);

	/* Change behaviour of register A1
	   4x oversampling
	   2nd channel DAC asynchronous */                                                      
	snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2MODE, 0x32);
	/* enable/select DMA channel and IRQ channel */
	snd_es1938_bits(chip, ESS_CMD_IRQCONTROL, 0xf0, 0x50);
	snd_es1938_bits(chip, ESS_CMD_DRQCONTROL, 0xf0, 0x50);
	snd_es1938_write_cmd(chip, ESS_CMD_ENABLEAUDIO1);
	/* Set spatializer parameters to recommended values */
	snd_es1938_mixer_write(chip, 0x54, 0x8f);
	snd_es1938_mixer_write(chip, 0x56, 0x95);
	snd_es1938_mixer_write(chip, 0x58, 0x94);
	snd_es1938_mixer_write(chip, 0x5a, 0x80);
}

/* --------------------------------------------------------------------
 * Reset the FIFOs
 * --------------------------------------------------------------------*/
static void snd_es1938_reset_fifo(struct es1938 *chip)
{
	outb(2, SLSB_REG(chip, RESET));
	outb(0, SLSB_REG(chip, RESET));
}

static struct snd_ratnum clocks[2] = {
	{
		.num = 793800,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	},
	{
		.num = 768000,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	}
};

static struct snd_pcm_hw_constraint_ratnums hw_constraints_clocks = {
	.nrats = 2,
	.rats = clocks,
};


static void snd_es1938_rate_set(struct es1938 *chip, 
				struct snd_pcm_substream *substream,
				int mode)
{
	unsigned int bits, div0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (runtime->rate_num == clocks[0].num)
		bits = 128 - runtime->rate_den;
	else
		bits = 256 - runtime->rate_den;

	/* set filter register */
	div0 = 256 - 7160000*20/(8*82*runtime->rate);
		
	if (mode == DAC2) {
		snd_es1938_mixer_write(chip, 0x70, bits);
		snd_es1938_mixer_write(chip, 0x72, div0);
	} else {
		snd_es1938_write(chip, 0xA1, bits);
		snd_es1938_write(chip, 0xA2, div0);
	}
}

/* --------------------------------------------------------------------
 * Configure Solo1 builtin DMA Controller
 * --------------------------------------------------------------------*/

static void snd_es1938_playback1_setdma(struct es1938 *chip)
{
	outb(0x00, SLIO_REG(chip, AUDIO2MODE));
	outl(chip->dma2_start, SLIO_REG(chip, AUDIO2DMAADDR));
	outw(0, SLIO_REG(chip, AUDIO2DMACOUNT));
	outw(chip->dma2_size, SLIO_REG(chip, AUDIO2DMACOUNT));
}

static void snd_es1938_playback2_setdma(struct es1938 *chip)
{
	/* Enable DMA controller */
	outb(0xc4, SLDM_REG(chip, DMACOMMAND));
	/* 1. Master reset */
	outb(0, SLDM_REG(chip, DMACLEAR));
	/* 2. Mask DMA */
	outb(1, SLDM_REG(chip, DMAMASK));
	outb(0x18, SLDM_REG(chip, DMAMODE));
	outl(chip->dma1_start, SLDM_REG(chip, DMAADDR));
	outw(chip->dma1_size - 1, SLDM_REG(chip, DMACOUNT));
	/* 3. Unmask DMA */
	outb(0, SLDM_REG(chip, DMAMASK));
}

static void snd_es1938_capture_setdma(struct es1938 *chip)
{
	/* Enable DMA controller */
	outb(0xc4, SLDM_REG(chip, DMACOMMAND));
	/* 1. Master reset */
	outb(0, SLDM_REG(chip, DMACLEAR));
	/* 2. Mask DMA */
	outb(1, SLDM_REG(chip, DMAMASK));
	outb(0x14, SLDM_REG(chip, DMAMODE));
	outl(chip->dma1_start, SLDM_REG(chip, DMAADDR));
	chip->last_capture_dmaaddr = chip->dma1_start;
	outw(chip->dma1_size - 1, SLDM_REG(chip, DMACOUNT));
	/* 3. Unmask DMA */
	outb(0, SLDM_REG(chip, DMAMASK));
}

/* ----------------------------------------------------------------------
 *
 *                           *** PCM part ***
 */

static int snd_es1938_capture_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	int val;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = 0x0f;
		chip->active |= ADC1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = 0x00;
		chip->active &= ~ADC1;
		break;
	default:
		return -EINVAL;
	}
	snd_es1938_write(chip, ESS_CMD_DMACONTROL, val);
	return 0;
}

static int snd_es1938_playback1_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* According to the documentation this should be:
		   0x13 but that value may randomly swap stereo channels */
                snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2CONTROL1, 0x92);
                udelay(10);
		snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2CONTROL1, 0x93);
                /* This two stage init gives the FIFO -> DAC connection time to
                 * settle before first data from DMA flows in.  This should ensure
                 * no swapping of stereo channels.  Report a bug if otherwise :-) */
		outb(0x0a, SLIO_REG(chip, AUDIO2MODE));
		chip->active |= DAC2;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		outb(0, SLIO_REG(chip, AUDIO2MODE));
		snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2CONTROL1, 0);
		chip->active &= ~DAC2;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int snd_es1938_playback2_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	int val;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = 5;
		chip->active |= DAC1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = 0;
		chip->active &= ~DAC1;
		break;
	default:
		return -EINVAL;
	}
	snd_es1938_write(chip, ESS_CMD_DMACONTROL, val);
	return 0;
}

static int snd_es1938_playback_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	switch (substream->number) {
	case 0:
		return snd_es1938_playback1_trigger(substream, cmd);
	case 1:
		return snd_es1938_playback2_trigger(substream, cmd);
	}
	snd_BUG();
	return -EINVAL;
}

/* --------------------------------------------------------------------
 * First channel for Extended Mode Audio 1 ADC Operation
 * --------------------------------------------------------------------*/
static int snd_es1938_capture_prepare(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int u, is8, mono;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma1_size = size;
	chip->dma1_start = runtime->dma_addr;

	mono = (runtime->channels > 1) ? 0 : 1;
	is8 = snd_pcm_format_width(runtime->format) == 16 ? 0 : 1;
	u = snd_pcm_format_unsigned(runtime->format);

	chip->dma1_shift = 2 - mono - is8;

	snd_es1938_reset_fifo(chip);
	
	/* program type */
	snd_es1938_bits(chip, ESS_CMD_ANALOGCONTROL, 0x03, (mono ? 2 : 1));

	/* set clock and counters */
        snd_es1938_rate_set(chip, substream, ADC1);

	count = 0x10000 - count;
	snd_es1938_write(chip, ESS_CMD_DMACNTRELOADL, count & 0xff);
	snd_es1938_write(chip, ESS_CMD_DMACNTRELOADH, count >> 8);

	/* initialize and configure ADC */
	snd_es1938_write(chip, ESS_CMD_SETFORMAT2, u ? 0x51 : 0x71);
	snd_es1938_write(chip, ESS_CMD_SETFORMAT2, 0x90 | 
		       (u ? 0x00 : 0x20) | 
		       (is8 ? 0x00 : 0x04) | 
		       (mono ? 0x40 : 0x08));

	//	snd_es1938_reset_fifo(chip);	

	/* 11. configure system interrupt controller and DMA controller */
	snd_es1938_capture_setdma(chip);

	return 0;
}


/* ------------------------------------------------------------------------------
 * Second Audio channel DAC Operation
 * ------------------------------------------------------------------------------*/
static int snd_es1938_playback1_prepare(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int u, is8, mono;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma2_size = size;
	chip->dma2_start = runtime->dma_addr;

	mono = (runtime->channels > 1) ? 0 : 1;
	is8 = snd_pcm_format_width(runtime->format) == 16 ? 0 : 1;
	u = snd_pcm_format_unsigned(runtime->format);

	chip->dma2_shift = 2 - mono - is8;

        snd_es1938_reset_fifo(chip);

	/* set clock and counters */
        snd_es1938_rate_set(chip, substream, DAC2);

	count >>= 1;
	count = 0x10000 - count;
	snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2TCOUNTL, count & 0xff);
	snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2TCOUNTH, count >> 8);

	/* initialize and configure Audio 2 DAC */
	snd_es1938_mixer_write(chip, ESSSB_IREG_AUDIO2CONTROL2, 0x40 | (u ? 0 : 4) |
			       (mono ? 0 : 2) | (is8 ? 0 : 1));

	/* program DMA */
	snd_es1938_playback1_setdma(chip);
	
	return 0;
}

static int snd_es1938_playback2_prepare(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int u, is8, mono;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma1_size = size;
	chip->dma1_start = runtime->dma_addr;

	mono = (runtime->channels > 1) ? 0 : 1;
	is8 = snd_pcm_format_width(runtime->format) == 16 ? 0 : 1;
	u = snd_pcm_format_unsigned(runtime->format);

	chip->dma1_shift = 2 - mono - is8;

	count = 0x10000 - count;
 
	/* reset */
	snd_es1938_reset_fifo(chip);
	
	snd_es1938_bits(chip, ESS_CMD_ANALOGCONTROL, 0x03, (mono ? 2 : 1));

	/* set clock and counters */
        snd_es1938_rate_set(chip, substream, DAC1);
	snd_es1938_write(chip, ESS_CMD_DMACNTRELOADL, count & 0xff);
	snd_es1938_write(chip, ESS_CMD_DMACNTRELOADH, count >> 8);

	/* initialized and configure DAC */
        snd_es1938_write(chip, ESS_CMD_SETFORMAT, u ? 0x80 : 0x00);
        snd_es1938_write(chip, ESS_CMD_SETFORMAT, u ? 0x51 : 0x71);
        snd_es1938_write(chip, ESS_CMD_SETFORMAT2, 
			 0x90 | (mono ? 0x40 : 0x08) |
			 (is8 ? 0x00 : 0x04) | (u ? 0x00 : 0x20));

	/* program DMA */
	snd_es1938_playback2_setdma(chip);
	
	return 0;
}

static int snd_es1938_playback_prepare(struct snd_pcm_substream *substream)
{
	switch (substream->number) {
	case 0:
		return snd_es1938_playback1_prepare(substream);
	case 1:
		return snd_es1938_playback2_prepare(substream);
	}
	snd_BUG();
	return -EINVAL;
}

/* during the incrementing of dma counters the DMA register reads sometimes
   returns garbage. To ensure a valid hw pointer, the following checks which
   should be very unlikely to fail are used:
   - is the current DMA address in the valid DMA range ?
   - is the sum of DMA address and DMA counter pointing to the last DMA byte ?
   One can argue this could differ by one byte depending on which register is
   updated first, so the implementation below allows for that.
*/
static snd_pcm_uframes_t snd_es1938_capture_pointer(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
#if 0
	size_t old, new;
	/* This stuff is *needed*, don't ask why - AB */
	old = inw(SLDM_REG(chip, DMACOUNT));
	while ((new = inw(SLDM_REG(chip, DMACOUNT))) != old)
		old = new;
	ptr = chip->dma1_size - 1 - new;
#else
	size_t count;
	unsigned int diff;

	ptr = inl(SLDM_REG(chip, DMAADDR));
	count = inw(SLDM_REG(chip, DMACOUNT));
	diff = chip->dma1_start + chip->dma1_size - ptr - count;

	if (diff > 3 || ptr < chip->dma1_start
	      || ptr >= chip->dma1_start+chip->dma1_size)
	  ptr = chip->last_capture_dmaaddr;            /* bad, use last saved */
	else
	  chip->last_capture_dmaaddr = ptr;            /* good, remember it */

	ptr -= chip->dma1_start;
#endif
	return ptr >> chip->dma1_shift;
}

static snd_pcm_uframes_t snd_es1938_playback1_pointer(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
#if 1
	ptr = chip->dma2_size - inw(SLIO_REG(chip, AUDIO2DMACOUNT));
#else
	ptr = inl(SLIO_REG(chip, AUDIO2DMAADDR)) - chip->dma2_start;
#endif
	return ptr >> chip->dma2_shift;
}

static snd_pcm_uframes_t snd_es1938_playback2_pointer(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	size_t old, new;
#if 1
	/* This stuff is *needed*, don't ask why - AB */
	old = inw(SLDM_REG(chip, DMACOUNT));
	while ((new = inw(SLDM_REG(chip, DMACOUNT))) != old)
		old = new;
	ptr = chip->dma1_size - 1 - new;
#else
	ptr = inl(SLDM_REG(chip, DMAADDR)) - chip->dma1_start;
#endif
	return ptr >> chip->dma1_shift;
}

static snd_pcm_uframes_t snd_es1938_playback_pointer(struct snd_pcm_substream *substream)
{
	switch (substream->number) {
	case 0:
		return snd_es1938_playback1_pointer(substream);
	case 1:
		return snd_es1938_playback2_pointer(substream);
	}
	snd_BUG();
	return -EINVAL;
}

static int snd_es1938_capture_copy(struct snd_pcm_substream *substream,
				   int channel,
				   snd_pcm_uframes_t pos,
				   void __user *dst,
				   snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	pos <<= chip->dma1_shift;
	count <<= chip->dma1_shift;
	if (snd_BUG_ON(pos + count > chip->dma1_size))
		return -EINVAL;
	if (pos + count < chip->dma1_size) {
		if (copy_to_user(dst, runtime->dma_area + pos + 1, count))
			return -EFAULT;
	} else {
		if (copy_to_user(dst, runtime->dma_area + pos + 1, count - 1))
			return -EFAULT;
		if (put_user(runtime->dma_area[0], ((unsigned char __user *)dst) + count - 1))
			return -EFAULT;
	}
	return 0;
}

/*
 * buffer management
 */
static int snd_es1938_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)

{
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	return 0;
}

static int snd_es1938_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* ----------------------------------------------------------------------
 * Audio1 Capture (ADC)
 * ----------------------------------------------------------------------*/
static struct snd_pcm_hardware snd_es1938_capture =
{
	.info =			(SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		6000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
        .buffer_bytes_max =	0x8000,       /* DMA controller screws on higher values */
	.period_bytes_min =	64,
	.period_bytes_max =	0x8000,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		256,
};

/* -----------------------------------------------------------------------
 * Audio2 Playback (DAC)
 * -----------------------------------------------------------------------*/
static struct snd_pcm_hardware snd_es1938_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		6000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
        .buffer_bytes_max =	0x8000,       /* DMA controller screws on higher values */
	.period_bytes_min =	64,
	.period_bytes_max =	0x8000,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		256,
};

static int snd_es1938_capture_open(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (chip->playback2_substream)
		return -EAGAIN;
	chip->capture_substream = substream;
	runtime->hw = snd_es1938_capture;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clocks);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 0, 0xff00);
	return 0;
}

static int snd_es1938_playback_open(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	switch (substream->number) {
	case 0:
		chip->playback1_substream = substream;
		break;
	case 1:
		if (chip->capture_substream)
			return -EAGAIN;
		chip->playback2_substream = substream;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	runtime->hw = snd_es1938_playback;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clocks);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 0, 0xff00);
	return 0;
}

static int snd_es1938_capture_close(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	return 0;
}

static int snd_es1938_playback_close(struct snd_pcm_substream *substream)
{
	struct es1938 *chip = snd_pcm_substream_chip(substream);

	switch (substream->number) {
	case 0:
		chip->playback1_substream = NULL;
		break;
	case 1:
		chip->playback2_substream = NULL;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	return 0;
}

static const struct snd_pcm_ops snd_es1938_playback_ops = {
	.open =		snd_es1938_playback_open,
	.close =	snd_es1938_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_es1938_pcm_hw_params,
	.hw_free =	snd_es1938_pcm_hw_free,
	.prepare =	snd_es1938_playback_prepare,
	.trigger =	snd_es1938_playback_trigger,
	.pointer =	snd_es1938_playback_pointer,
};

static const struct snd_pcm_ops snd_es1938_capture_ops = {
	.open =		snd_es1938_capture_open,
	.close =	snd_es1938_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_es1938_pcm_hw_params,
	.hw_free =	snd_es1938_pcm_hw_free,
	.prepare =	snd_es1938_capture_prepare,
	.trigger =	snd_es1938_capture_trigger,
	.pointer =	snd_es1938_capture_pointer,
	.copy =		snd_es1938_capture_copy,
};

static int snd_es1938_new_pcm(struct es1938 *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "es-1938-1946", device, 2, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_es1938_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_es1938_capture_ops);
	
	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ESS Solo-1");

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 64*1024);

	chip->pcm = pcm;
	return 0;
}

/* -------------------------------------------------------------------
 * 
 *                       *** Mixer part ***
 */

static int snd_es1938_info_mux(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[8] = {
		"Mic", "Mic Master", "CD", "AOUT",
		"Mic1", "Mix", "Line", "Master"
	};

	return snd_ctl_enum_info(uinfo, 1, 8, texts);
}

static int snd_es1938_get_mux(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = snd_es1938_mixer_read(chip, 0x1c) & 0x07;
	return 0;
}

static int snd_es1938_put_mux(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = ucontrol->value.enumerated.item[0];
	
	if (val > 7)
		return -EINVAL;
	return snd_es1938_mixer_bits(chip, 0x1c, 0x07, val) != val;
}

#define snd_es1938_info_spatializer_enable	snd_ctl_boolean_mono_info

static int snd_es1938_get_spatializer_enable(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = snd_es1938_mixer_read(chip, 0x50);
	ucontrol->value.integer.value[0] = !!(val & 8);
	return 0;
}

static int snd_es1938_put_spatializer_enable(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	unsigned char oval, nval;
	int change;
	nval = ucontrol->value.integer.value[0] ? 0x0c : 0x04;
	oval = snd_es1938_mixer_read(chip, 0x50) & 0x0c;
	change = nval != oval;
	if (change) {
		snd_es1938_mixer_write(chip, 0x50, nval & ~0x04);
		snd_es1938_mixer_write(chip, 0x50, nval);
	}
	return change;
}

static int snd_es1938_info_hw_volume(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 63;
	return 0;
}

static int snd_es1938_get_hw_volume(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = snd_es1938_mixer_read(chip, 0x61) & 0x3f;
	ucontrol->value.integer.value[1] = snd_es1938_mixer_read(chip, 0x63) & 0x3f;
	return 0;
}

#define snd_es1938_info_hw_switch		snd_ctl_boolean_stereo_info

static int snd_es1938_get_hw_switch(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = !(snd_es1938_mixer_read(chip, 0x61) & 0x40);
	ucontrol->value.integer.value[1] = !(snd_es1938_mixer_read(chip, 0x63) & 0x40);
	return 0;
}

static void snd_es1938_hwv_free(struct snd_kcontrol *kcontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	chip->master_volume = NULL;
	chip->master_switch = NULL;
	chip->hw_volume = NULL;
	chip->hw_switch = NULL;
}

static int snd_es1938_reg_bits(struct es1938 *chip, unsigned char reg,
			       unsigned char mask, unsigned char val)
{
	if (reg < 0xa0)
		return snd_es1938_mixer_bits(chip, reg, mask, val);
	else
		return snd_es1938_bits(chip, reg, mask, val);
}

static int snd_es1938_reg_read(struct es1938 *chip, unsigned char reg)
{
	if (reg < 0xa0)
		return snd_es1938_mixer_read(chip, reg);
	else
		return snd_es1938_read(chip, reg);
}

#define ES1938_SINGLE_TLV(xname, xindex, reg, shift, mask, invert, xtlv)    \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,\
  .name = xname, .index = xindex, \
  .info = snd_es1938_info_single, \
  .get = snd_es1938_get_single, .put = snd_es1938_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (invert << 24), \
  .tlv = { .p = xtlv } }
#define ES1938_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_es1938_info_single, \
  .get = snd_es1938_get_single, .put = snd_es1938_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (invert << 24) }

static int snd_es1938_info_single(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es1938_get_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int val;
	
	val = snd_es1938_reg_read(chip, reg);
	ucontrol->value.integer.value[0] = (val >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_es1938_put_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned char val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	mask <<= shift;
	val <<= shift;
	return snd_es1938_reg_bits(chip, reg, mask, val) != val;
}

#define ES1938_DOUBLE_TLV(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert, xtlv) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,\
  .name = xname, .index = xindex, \
  .info = snd_es1938_info_double, \
  .get = snd_es1938_get_double, .put = snd_es1938_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22), \
  .tlv = { .p = xtlv } }
#define ES1938_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_es1938_info_double, \
  .get = snd_es1938_get_double, .put = snd_es1938_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

static int snd_es1938_info_double(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es1938_get_double(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	unsigned char left, right;
	
	left = snd_es1938_reg_read(chip, left_reg);
	if (left_reg != right_reg)
		right = snd_es1938_reg_read(chip, right_reg);
	else
		right = left;
	ucontrol->value.integer.value[0] = (left >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (right >> shift_right) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_es1938_put_double(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct es1938 *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned char val1, val2, mask1, mask2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	mask1 = mask << shift_left;
	mask2 = mask << shift_right;
	if (left_reg != right_reg) {
		change = 0;
		if (snd_es1938_reg_bits(chip, left_reg, mask1, val1) != val1)
			change = 1;
		if (snd_es1938_reg_bits(chip, right_reg, mask2, val2) != val2)
			change = 1;
	} else {
		change = (snd_es1938_reg_bits(chip, left_reg, mask1 | mask2, 
					      val1 | val2) != (val1 | val2));
	}
	return change;
}

static const DECLARE_TLV_DB_RANGE(db_scale_master,
	0, 54, TLV_DB_SCALE_ITEM(-3600, 50, 1),
	54, 63, TLV_DB_SCALE_ITEM(-900, 100, 0),
);

static const DECLARE_TLV_DB_RANGE(db_scale_audio1,
	0, 8, TLV_DB_SCALE_ITEM(-3300, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-900, 150, 0),
);

static const DECLARE_TLV_DB_RANGE(db_scale_audio2,
	0, 8, TLV_DB_SCALE_ITEM(-3450, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-1050, 150, 0),
);

static const DECLARE_TLV_DB_RANGE(db_scale_mic,
	0, 8, TLV_DB_SCALE_ITEM(-2400, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(0, 150, 0),
);

static const DECLARE_TLV_DB_RANGE(db_scale_line,
	0, 8, TLV_DB_SCALE_ITEM(-3150, 300, 1),
	8, 15, TLV_DB_SCALE_ITEM(-750, 150, 0),
);

static const DECLARE_TLV_DB_SCALE(db_scale_capture, 0, 150, 0);

static struct snd_kcontrol_new snd_es1938_controls[] = {
ES1938_DOUBLE_TLV("Master Playback Volume", 0, 0x60, 0x62, 0, 0, 63, 0,
		  db_scale_master),
ES1938_DOUBLE("Master Playback Switch", 0, 0x60, 0x62, 6, 6, 1, 1),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Hardware Master Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_es1938_info_hw_volume,
	.get = snd_es1938_get_hw_volume,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READ |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name = "Hardware Master Playback Switch",
	.info = snd_es1938_info_hw_switch,
	.get = snd_es1938_get_hw_switch,
	.tlv = { .p = db_scale_master },
},
ES1938_SINGLE("Hardware Volume Split", 0, 0x64, 7, 1, 0),
ES1938_DOUBLE_TLV("Line Playback Volume", 0, 0x3e, 0x3e, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE("CD Playback Volume", 0, 0x38, 0x38, 4, 0, 15, 0),
ES1938_DOUBLE_TLV("FM Playback Volume", 0, 0x36, 0x36, 4, 0, 15, 0,
		  db_scale_mic),
ES1938_DOUBLE_TLV("Mono Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("Mic Playback Volume", 0, 0x1a, 0x1a, 4, 0, 15, 0,
		  db_scale_mic),
ES1938_DOUBLE_TLV("Aux Playback Volume", 0, 0x3a, 0x3a, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("Capture Volume", 0, 0xb4, 0xb4, 4, 0, 15, 0,
		  db_scale_capture),
ES1938_SINGLE("Beep Volume", 0, 0x3c, 0, 7, 0),
ES1938_SINGLE("Record Monitor", 0, 0xa8, 3, 1, 0),
ES1938_SINGLE("Capture Switch", 0, 0x1c, 4, 1, 1),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_es1938_info_mux,
	.get = snd_es1938_get_mux,
	.put = snd_es1938_put_mux,
},
ES1938_DOUBLE_TLV("Mono Input Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("PCM Capture Volume", 0, 0x69, 0x69, 4, 0, 15, 0,
		  db_scale_audio2),
ES1938_DOUBLE_TLV("Mic Capture Volume", 0, 0x68, 0x68, 4, 0, 15, 0,
		  db_scale_mic),
ES1938_DOUBLE_TLV("Line Capture Volume", 0, 0x6e, 0x6e, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("FM Capture Volume", 0, 0x6b, 0x6b, 4, 0, 15, 0,
		  db_scale_mic),
ES1938_DOUBLE_TLV("Mono Capture Volume", 0, 0x6f, 0x6f, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("CD Capture Volume", 0, 0x6a, 0x6a, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("Aux Capture Volume", 0, 0x6c, 0x6c, 4, 0, 15, 0,
		  db_scale_line),
ES1938_DOUBLE_TLV("PCM Playback Volume", 0, 0x7c, 0x7c, 4, 0, 15, 0,
		  db_scale_audio2),
ES1938_DOUBLE_TLV("PCM Playback Volume", 1, 0x14, 0x14, 4, 0, 15, 0,
		  db_scale_audio1),
ES1938_SINGLE("3D Control - Level", 0, 0x52, 0, 63, 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Switch",
	.info = snd_es1938_info_spatializer_enable,
	.get = snd_es1938_get_spatializer_enable,
	.put = snd_es1938_put_spatializer_enable,
},
ES1938_SINGLE("Mic Boost (+26dB)", 0, 0x7d, 3, 1, 0)
};


/* ---------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------- */

/*
 * initialize the chip - used by resume callback, too
 */
static void snd_es1938_chip_init(struct es1938 *chip)
{
	/* reset chip */
	snd_es1938_reset(chip);

	/* configure native mode */

	/* enable bus master */
	pci_set_master(chip->pci);

	/* disable legacy audio */
	pci_write_config_word(chip->pci, SL_PCI_LEGACYCONTROL, 0x805f);

	/* set DDMA base */
	pci_write_config_word(chip->pci, SL_PCI_DDMACONTROL, chip->ddma_port | 1);

	/* set DMA/IRQ policy */
	pci_write_config_dword(chip->pci, SL_PCI_CONFIG, 0);

	/* enable Audio 1, Audio 2, MPU401 IRQ and HW volume IRQ*/
	outb(0xf0, SLIO_REG(chip, IRQCONTROL));

	/* reset DMA */
	outb(0, SLDM_REG(chip, DMACLEAR));
}

#ifdef CONFIG_PM_SLEEP
/*
 * PM support
 */

static unsigned char saved_regs[SAVED_REG_SIZE+1] = {
	0x14, 0x1a, 0x1c, 0x3a, 0x3c, 0x3e, 0x36, 0x38,
	0x50, 0x52, 0x60, 0x61, 0x62, 0x63, 0x64, 0x68,
	0x69, 0x6a, 0x6b, 0x6d, 0x6e, 0x6f, 0x7c, 0x7d,
	0xa8, 0xb4,
};


static int es1938_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct es1938 *chip = card->private_data;
	unsigned char *s, *d;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);

	/* save mixer-related registers */
	for (s = saved_regs, d = chip->saved_regs; *s; s++, d++)
		*d = snd_es1938_reg_read(chip, *s);

	outb(0x00, SLIO_REG(chip, IRQCONTROL)); /* disable irqs */
	if (chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}
	return 0;
}

static int es1938_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct es1938 *chip = card->private_data;
	unsigned char *s, *d;

	if (request_irq(pci->irq, snd_es1938_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(dev, "unable to grab IRQ %d, disabling device\n",
			pci->irq);
		snd_card_disconnect(card);
		return -EIO;
	}
	chip->irq = pci->irq;
	snd_es1938_chip_init(chip);

	/* restore mixer-related registers */
	for (s = saved_regs, d = chip->saved_regs; *s; s++, d++) {
		if (*s < 0xa0)
			snd_es1938_mixer_write(chip, *s, *d);
		else
			snd_es1938_write(chip, *s, *d);
	}

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(es1938_pm, es1938_suspend, es1938_resume);
#define ES1938_PM_OPS	&es1938_pm
#else
#define ES1938_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

#ifdef SUPPORT_JOYSTICK
static int snd_es1938_create_gameport(struct es1938 *chip)
{
	struct gameport *gp;

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(chip->card->dev,
			"cannot allocate memory for gameport\n");
		return -ENOMEM;
	}

	gameport_set_name(gp, "ES1938");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = chip->game_port;

	gameport_register_port(gp);

	return 0;
}

static void snd_es1938_free_gameport(struct es1938 *chip)
{
	if (chip->gameport) {
		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
	}
}
#else
static inline int snd_es1938_create_gameport(struct es1938 *chip) { return -ENOSYS; }
static inline void snd_es1938_free_gameport(struct es1938 *chip) { }
#endif /* SUPPORT_JOYSTICK */

static int snd_es1938_free(struct es1938 *chip)
{
	/* disable irqs */
	outb(0x00, SLIO_REG(chip, IRQCONTROL));
	if (chip->rmidi)
		snd_es1938_mixer_bits(chip, ESSSB_IREG_MPU401CONTROL, 0x40, 0);

	snd_es1938_free_gameport(chip);

	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_es1938_dev_free(struct snd_device *device)
{
	struct es1938 *chip = device->device_data;
	return snd_es1938_free(chip);
}

static int snd_es1938_create(struct snd_card *card,
			     struct pci_dev *pci,
			     struct es1938 **rchip)
{
	struct es1938 *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_es1938_dev_free,
	};

	*rchip = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
        /* check, if we can restrict PCI DMA transfers to 24 bits */
	if (dma_set_mask(&pci->dev, DMA_BIT_MASK(24)) < 0 ||
	    dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(24)) < 0) {
		dev_err(card->dev,
			"architecture does not support 24bit PCI busmaster DMA\n");
		pci_disable_device(pci);
                return -ENXIO;
        }

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->mixer_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	if ((err = pci_request_regions(pci, "ESS Solo-1")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->io_port = pci_resource_start(pci, 0);
	chip->sb_port = pci_resource_start(pci, 1);
	chip->vc_port = pci_resource_start(pci, 2);
	chip->mpu_port = pci_resource_start(pci, 3);
	chip->game_port = pci_resource_start(pci, 4);
	if (request_irq(pci->irq, snd_es1938_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_es1938_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	dev_dbg(card->dev,
		"create: io: 0x%lx, sb: 0x%lx, vc: 0x%lx, mpu: 0x%lx, game: 0x%lx\n",
		   chip->io_port, chip->sb_port, chip->vc_port, chip->mpu_port, chip->game_port);

	chip->ddma_port = chip->vc_port + 0x00;		/* fix from Thomas Sailer */

	snd_es1938_chip_init(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_es1938_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

/* --------------------------------------------------------------------
 * Interrupt handler
 * -------------------------------------------------------------------- */
static irqreturn_t snd_es1938_interrupt(int irq, void *dev_id)
{
	struct es1938 *chip = dev_id;
	unsigned char status, audiostatus;
	int handled = 0;

	status = inb(SLIO_REG(chip, IRQCONTROL));
#if 0
	dev_dbg(chip->card->dev,
		"Es1938debug - interrupt status: =0x%x\n", status);
#endif
	
	/* AUDIO 1 */
	if (status & 0x10) {
#if 0
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 interrupt\n");
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA count: %u\n",
		       inw(SLDM_REG(chip, DMACOUNT)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA base: %u\n",
		       inl(SLDM_REG(chip, DMAADDR)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA status: 0x%x\n",
		       inl(SLDM_REG(chip, DMASTATUS)));
#endif
		/* clear irq */
		handled = 1;
		audiostatus = inb(SLSB_REG(chip, STATUS));
		if (chip->active & ADC1)
			snd_pcm_period_elapsed(chip->capture_substream);
		else if (chip->active & DAC1)
			snd_pcm_period_elapsed(chip->playback2_substream);
	}
	
	/* AUDIO 2 */
	if (status & 0x20) {
#if 0
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 interrupt\n");
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 DMAC DMA count: %u\n",
		       inw(SLIO_REG(chip, AUDIO2DMACOUNT)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 DMAC DMA base: %u\n",
		       inl(SLIO_REG(chip, AUDIO2DMAADDR)));

#endif
		/* clear irq */
		handled = 1;
		snd_es1938_mixer_bits(chip, ESSSB_IREG_AUDIO2CONTROL2, 0x80, 0);
		if (chip->active & DAC2)
			snd_pcm_period_elapsed(chip->playback1_substream);
	}

	/* Hardware volume */
	if (status & 0x40) {
		int split = snd_es1938_mixer_read(chip, 0x64) & 0x80;
		handled = 1;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_switch->id);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_volume->id);
		if (!split) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_switch->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_volume->id);
		}
		/* ack interrupt */
		snd_es1938_mixer_write(chip, 0x66, 0x00);
	}

	/* MPU401 */
	if (status & 0x80) {
		// the following line is evil! It switches off MIDI interrupt handling after the first interrupt received.
		// replacing the last 0 by 0x40 works for ESS-Solo1, but just doing nothing works as well!
		// andreas@flying-snail.de
		// snd_es1938_mixer_bits(chip, ESSSB_IREG_MPU401CONTROL, 0x40, 0); /* ack? */
		if (chip->rmidi) {
			handled = 1;
			snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);
		}
	}
	return IRQ_RETVAL(handled);
}

#define ES1938_DMA_SIZE 64

static int snd_es1938_mixer(struct es1938 *chip)
{
	struct snd_card *card;
	unsigned int idx;
	int err;

	card = chip->card;

	strcpy(card->mixername, "ESS Solo-1");

	for (idx = 0; idx < ARRAY_SIZE(snd_es1938_controls); idx++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(&snd_es1938_controls[idx], chip);
		switch (idx) {
			case 0:
				chip->master_volume = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 1:
				chip->master_switch = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 2:
				chip->hw_volume = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 3:
				chip->hw_switch = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			}
		if ((err = snd_ctl_add(card, kctl)) < 0)
			return err;
	}
	return 0;
}
       

static int snd_es1938_probe(struct pci_dev *pci,
			    const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct es1938 *chip;
	struct snd_opl3 *opl3;
	int idx, err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;
	for (idx = 0; idx < 5; idx++) {
		if (pci_resource_start(pci, idx) == 0 ||
		    !(pci_resource_flags(pci, idx) & IORESOURCE_IO)) {
		    	snd_card_free(card);
		    	return -ENODEV;
		}
	}
	if ((err = snd_es1938_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	strcpy(card->driver, "ES1938");
	strcpy(card->shortname, "ESS ES1938 (Solo-1)");
	sprintf(card->longname, "%s rev %i, irq %i",
		card->shortname,
		chip->revision,
		chip->irq);

	if ((err = snd_es1938_new_pcm(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es1938_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_opl3_create(card,
			    SLSB_REG(chip, FMLOWADDR),
			    SLSB_REG(chip, FMHIGHADDR),
			    OPL3_HW_OPL3, 1, &opl3) < 0) {
		dev_err(card->dev, "OPL3 not detected at 0x%lx\n",
			   SLSB_REG(chip, FMLOWADDR));
	} else {
	        if ((err = snd_opl3_timer_new(opl3, 0, 1)) < 0) {
	                snd_card_free(card);
	                return err;
		}
	        if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
	                snd_card_free(card);
	                return err;
		}
	}
	if (snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
				chip->mpu_port,
				MPU401_INFO_INTEGRATED | MPU401_INFO_IRQ_HOOK,
				-1, &chip->rmidi) < 0) {
		dev_err(card->dev, "unable to initialize MPU-401\n");
	} else {
		// this line is vital for MIDI interrupt handling on ess-solo1
		// andreas@flying-snail.de
		snd_es1938_mixer_bits(chip, ESSSB_IREG_MPU401CONTROL, 0x40, 0x40);
	}

	snd_es1938_create_gameport(chip);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void snd_es1938_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver es1938_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_es1938_ids,
	.probe = snd_es1938_probe,
	.remove = snd_es1938_remove,
	.driver = {
		.pm = ES1938_PM_OPS,
	},
};

module_pci_driver(es1938_driver);
