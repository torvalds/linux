// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  card-als4000.c - driver for Avance Logic ALS4000 based soundcards.
 *  Copyright (C) 2000 by Bart Hartgers <bart@etpmod.phys.tue.nl>,
 *			  Jaroslav Kysela <perex@perex.cz>
 *  Copyright (C) 2002, 2008 by Andreas Mohr <hw7oshyuv3001@sneakemail.com>
 *
 *  Framework borrowed from Massimo Piccioni's card-als100.c.
 *
 * NOTES
 *
 *  Since Avance does not provide any meaningful documentation, and I
 *  bought an ALS4000 based soundcard, I was forced to base this driver
 *  on reverse engineering.
 *
 *  Note: this is no longer true (thank you!):
 *  pretty verbose chip docu (ALS4000a.PDF) can be found on the ALSA web site.
 *  Page numbers stated anywhere below with the "SPECS_PAGE:" tag
 *  refer to: ALS4000a.PDF specs Ver 1.0, May 28th, 1998.
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
 * ToDo:
 * - by default, don't enable legacy game and use PCI game I/O
 * - power management? (card can do voice wakeup according to datasheet!!)
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/gameport.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>
#include <sound/initval.h>

MODULE_AUTHOR("Bart Hartgers <bart@etpmod.phys.tue.nl>, Andreas Mohr");
MODULE_DESCRIPTION("Avance Logic ALS4000");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Avance Logic,ALS4000}}");

#if IS_REACHABLE(CONFIG_GAMEPORT)
#define SUPPORT_JOYSTICK 1
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
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
module_param_hw_array(joystick_port, int, ioport, NULL, 0444);
MODULE_PARM_DESC(joystick_port, "Joystick port address for ALS4000 soundcard. (0 = disabled)");
#endif

struct snd_card_als4000 {
	/* most frequent access first */
	unsigned long iobase;
	struct pci_dev *pci;
	struct snd_sb *chip;
#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
};

static const struct pci_device_id snd_als4000_ids[] = {
	{ 0x4005, 0x4000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* ALS4000 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_als4000_ids);

enum als4k_iobase_t {
	/* IOx: B == Byte, W = Word, D = DWord; SPECS_PAGE: 37 */
	ALS4K_IOD_00_AC97_ACCESS = 0x00,
	ALS4K_IOW_04_AC97_READ = 0x04,
	ALS4K_IOB_06_AC97_STATUS = 0x06,
	ALS4K_IOB_07_IRQSTATUS = 0x07,
	ALS4K_IOD_08_GCR_DATA = 0x08,
	ALS4K_IOB_0C_GCR_INDEX = 0x0c,
	ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU = 0x0e,
	ALS4K_IOB_10_ADLIB_ADDR0 = 0x10,
	ALS4K_IOB_11_ADLIB_ADDR1 = 0x11,
	ALS4K_IOB_12_ADLIB_ADDR2 = 0x12,
	ALS4K_IOB_13_ADLIB_ADDR3 = 0x13,
	ALS4K_IOB_14_MIXER_INDEX = 0x14,
	ALS4K_IOB_15_MIXER_DATA = 0x15,
	ALS4K_IOB_16_ESP_RESET = 0x16,
	ALS4K_IOB_16_ACK_FOR_CR1E = 0x16, /* 2nd function */
	ALS4K_IOB_18_OPL_ADDR0 = 0x18,
	ALS4K_IOB_19_OPL_ADDR1 = 0x19,
	ALS4K_IOB_1A_ESP_RD_DATA = 0x1a,
	ALS4K_IOB_1C_ESP_CMD_DATA = 0x1c,
	ALS4K_IOB_1C_ESP_WR_STATUS = 0x1c, /* 2nd function */
	ALS4K_IOB_1E_ESP_RD_STATUS8 = 0x1e,
	ALS4K_IOB_1F_ESP_RD_STATUS16 = 0x1f,
	ALS4K_IOB_20_ESP_GAMEPORT_200 = 0x20,
	ALS4K_IOB_21_ESP_GAMEPORT_201 = 0x21,
	ALS4K_IOB_30_MIDI_DATA = 0x30,
	ALS4K_IOB_31_MIDI_STATUS = 0x31,
	ALS4K_IOB_31_MIDI_COMMAND = 0x31, /* 2nd function */
};

enum als4k_iobase_0e_t {
	ALS4K_IOB_0E_MPU_IRQ = 0x10,
	ALS4K_IOB_0E_CR1E_IRQ = 0x40,
	ALS4K_IOB_0E_SB_DMA_IRQ = 0x80,
};

enum als4k_gcr_t { /* all registers 32bit wide; SPECS_PAGE: 38 to 42 */
	ALS4K_GCR8C_MISC_CTRL = 0x8c,
	ALS4K_GCR90_TEST_MODE_REG = 0x90,
	ALS4K_GCR91_DMA0_ADDR = 0x91,
	ALS4K_GCR92_DMA0_MODE_COUNT = 0x92,
	ALS4K_GCR93_DMA1_ADDR = 0x93,
	ALS4K_GCR94_DMA1_MODE_COUNT = 0x94,
	ALS4K_GCR95_DMA3_ADDR = 0x95,
	ALS4K_GCR96_DMA3_MODE_COUNT = 0x96,
	ALS4K_GCR99_DMA_EMULATION_CTRL = 0x99,
	ALS4K_GCRA0_FIFO1_CURRENT_ADDR = 0xa0,
	ALS4K_GCRA1_FIFO1_STATUS_BYTECOUNT = 0xa1,
	ALS4K_GCRA2_FIFO2_PCIADDR = 0xa2,
	ALS4K_GCRA3_FIFO2_COUNT = 0xa3,
	ALS4K_GCRA4_FIFO2_CURRENT_ADDR = 0xa4,
	ALS4K_GCRA5_FIFO1_STATUS_BYTECOUNT = 0xa5,
	ALS4K_GCRA6_PM_CTRL = 0xa6,
	ALS4K_GCRA7_PCI_ACCESS_STORAGE = 0xa7,
	ALS4K_GCRA8_LEGACY_CFG1 = 0xa8,
	ALS4K_GCRA9_LEGACY_CFG2 = 0xa9,
	ALS4K_GCRFF_DUMMY_SCRATCH = 0xff,
};

enum als4k_gcr8c_t {
	ALS4K_GCR8C_IRQ_MASK_CTRL_ENABLE = 0x8000,
	ALS4K_GCR8C_CHIP_REV_MASK = 0xf0000
};

static inline void snd_als4k_iobase_writeb(unsigned long iobase,
						enum als4k_iobase_t reg,
						u8 val)
{
	outb(val, iobase + reg);
}

static inline void snd_als4k_iobase_writel(unsigned long iobase,
						enum als4k_iobase_t reg,
						u32 val)
{
	outl(val, iobase + reg);
}

static inline u8 snd_als4k_iobase_readb(unsigned long iobase,
						enum als4k_iobase_t reg)
{
	return inb(iobase + reg);
}

static inline u32 snd_als4k_iobase_readl(unsigned long iobase,
						enum als4k_iobase_t reg)
{
	return inl(iobase + reg);
}

static inline void snd_als4k_gcr_write_addr(unsigned long iobase,
						 enum als4k_gcr_t reg,
						 u32 val)
{
	snd_als4k_iobase_writeb(iobase, ALS4K_IOB_0C_GCR_INDEX, reg);
	snd_als4k_iobase_writel(iobase, ALS4K_IOD_08_GCR_DATA, val);
}

static inline void snd_als4k_gcr_write(struct snd_sb *sb,
					 enum als4k_gcr_t reg,
					 u32 val)
{
	snd_als4k_gcr_write_addr(sb->alt_port, reg, val);
}	

static inline u32 snd_als4k_gcr_read_addr(unsigned long iobase,
						 enum als4k_gcr_t reg)
{
	/* SPECS_PAGE: 37/38 */
	snd_als4k_iobase_writeb(iobase, ALS4K_IOB_0C_GCR_INDEX, reg);
	return snd_als4k_iobase_readl(iobase, ALS4K_IOD_08_GCR_DATA);
}

static inline u32 snd_als4k_gcr_read(struct snd_sb *sb, enum als4k_gcr_t reg)
{
	return snd_als4k_gcr_read_addr(sb->alt_port, reg);
}

enum als4k_cr_t { /* all registers 8bit wide; SPECS_PAGE: 20 to 23 */
	ALS4K_CR0_SB_CONFIG = 0x00,
	ALS4K_CR2_MISC_CONTROL = 0x02,
	ALS4K_CR3_CONFIGURATION = 0x03,
	ALS4K_CR17_FIFO_STATUS = 0x17,
	ALS4K_CR18_ESP_MAJOR_VERSION = 0x18,
	ALS4K_CR19_ESP_MINOR_VERSION = 0x19,
	ALS4K_CR1A_MPU401_UART_MODE_CONTROL = 0x1a,
	ALS4K_CR1C_FIFO2_BLOCK_LENGTH_LO = 0x1c,
	ALS4K_CR1D_FIFO2_BLOCK_LENGTH_HI = 0x1d,
	ALS4K_CR1E_FIFO2_CONTROL = 0x1e, /* secondary PCM FIFO (recording) */
	ALS4K_CR3A_MISC_CONTROL = 0x3a,
	ALS4K_CR3B_CRC32_BYTE0 = 0x3b, /* for testing, activate via CR3A */
	ALS4K_CR3C_CRC32_BYTE1 = 0x3c,
	ALS4K_CR3D_CRC32_BYTE2 = 0x3d,
	ALS4K_CR3E_CRC32_BYTE3 = 0x3e,
};

enum als4k_cr0_t {
	ALS4K_CR0_DMA_CONTIN_MODE_CTRL = 0x02, /* IRQ/FIFO controlled for 0/1 */
	ALS4K_CR0_DMA_90H_MODE_CTRL = 0x04, /* IRQ/FIFO controlled for 0/1 */
	ALS4K_CR0_MX80_81_REG_WRITE_ENABLE = 0x80,
};

static inline void snd_als4_cr_write(struct snd_sb *chip,
					enum als4k_cr_t reg,
					u8 data)
{
	/* Control Register is reg | 0xc0 (bit 7, 6 set) on sbmixer_index
	 * NOTE: assumes chip->mixer_lock to be locked externally already!
	 * SPECS_PAGE: 6 */
	snd_sbmixer_write(chip, reg | 0xc0, data);
}

static inline u8 snd_als4_cr_read(struct snd_sb *chip,
					enum als4k_cr_t reg)
{
	/* NOTE: assumes chip->mixer_lock to be locked externally already! */
	return snd_sbmixer_read(chip, reg | 0xc0);
}



static void snd_als4000_set_rate(struct snd_sb *chip, unsigned int rate)
{
	if (!(chip->mode & SB_RATE_LOCK)) {
		snd_sbdsp_command(chip, SB_DSP_SAMPLE_RATE_OUT);
		snd_sbdsp_command(chip, rate>>8);
		snd_sbdsp_command(chip, rate);
	}
}

static inline void snd_als4000_set_capture_dma(struct snd_sb *chip,
					       dma_addr_t addr, unsigned size)
{
	/* SPECS_PAGE: 40 */
	snd_als4k_gcr_write(chip, ALS4K_GCRA2_FIFO2_PCIADDR, addr);
	snd_als4k_gcr_write(chip, ALS4K_GCRA3_FIFO2_COUNT, (size-1));
}

static inline void snd_als4000_set_playback_dma(struct snd_sb *chip,
						dma_addr_t addr,
						unsigned size)
{
	/* SPECS_PAGE: 38 */
	snd_als4k_gcr_write(chip, ALS4K_GCR91_DMA0_ADDR, addr);
	snd_als4k_gcr_write(chip, ALS4K_GCR92_DMA0_MODE_COUNT,
							(size-1)|0x180000);
}

#define ALS4000_FORMAT_SIGNED	(1<<0)
#define ALS4000_FORMAT_16BIT	(1<<1)
#define ALS4000_FORMAT_STEREO	(1<<2)

static int snd_als4000_get_format(struct snd_pcm_runtime *runtime)
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
static const struct {
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
static const unsigned char capture_cmd_vals[]=
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

static int snd_als4000_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long size;
	unsigned count;

	chip->capture_format = snd_als4000_get_format(runtime);
		
	size = snd_pcm_lib_buffer_bytes(substream);
	count = snd_pcm_lib_period_bytes(substream);
	
	if (chip->capture_format & ALS4000_FORMAT_16BIT)
		count >>= 1;
	count--;

	spin_lock_irq(&chip->reg_lock);
	snd_als4000_set_rate(chip, runtime->rate);
	snd_als4000_set_capture_dma(chip, runtime->dma_addr, size);
	spin_unlock_irq(&chip->reg_lock);
	spin_lock_irq(&chip->mixer_lock);
	snd_als4_cr_write(chip, ALS4K_CR1C_FIFO2_BLOCK_LENGTH_LO, count & 0xff);
	snd_als4_cr_write(chip, ALS4K_CR1D_FIFO2_BLOCK_LENGTH_HI, count >> 8);
	spin_unlock_irq(&chip->mixer_lock);
	return 0;
}

static int snd_als4000_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long size;
	unsigned count;

	chip->playback_format = snd_als4000_get_format(runtime);
	
	size = snd_pcm_lib_buffer_bytes(substream);
	count = snd_pcm_lib_period_bytes(substream);
	
	if (chip->playback_format & ALS4000_FORMAT_16BIT)
		count >>= 1;
	count--;
	
	/* FIXME: from second playback on, there's a lot more clicks and pops
	 * involved here than on first playback. Fiddling with
	 * tons of different settings didn't help (DMA, speaker on/off,
	 * reordering, ...). Something seems to get enabled on playback
	 * that I haven't found out how to disable again, which then causes
	 * the switching pops to reach the speakers the next time here. */
	spin_lock_irq(&chip->reg_lock);
	snd_als4000_set_rate(chip, runtime->rate);
	snd_als4000_set_playback_dma(chip, runtime->dma_addr, size);
	
	/* SPEAKER_ON not needed, since dma_on seems to also enable speaker */
	/* snd_sbdsp_command(chip, SB_DSP_SPEAKER_ON); */
	snd_sbdsp_command(chip, playback_cmd(chip).dsp_cmd);
	snd_sbdsp_command(chip, playback_cmd(chip).format);
	snd_sbdsp_command(chip, count & 0xff);
	snd_sbdsp_command(chip, count >> 8);
	snd_sbdsp_command(chip, playback_cmd(chip).dma_off);	
	spin_unlock_irq(&chip->reg_lock);
	
	return 0;
}

static int snd_als4000_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	int result = 0;
	
	/* FIXME race condition in here!!!
	   chip->mode non-atomic update gets consistently protected
	   by reg_lock always, _except_ for this place!!
	   Probably need to take reg_lock as outer (or inner??) lock, too.
	   (or serialize both lock operations? probably not, though... - racy?)
	*/
	spin_lock(&chip->mixer_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		chip->mode |= SB_RATE_LOCK_CAPTURE;
		snd_als4_cr_write(chip, ALS4K_CR1E_FIFO2_CONTROL,
							 capture_cmd(chip));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		chip->mode &= ~SB_RATE_LOCK_CAPTURE;
		snd_als4_cr_write(chip, ALS4K_CR1E_FIFO2_CONTROL,
							 capture_cmd(chip));
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->mixer_lock);
	return result;
}

static int snd_als4000_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		chip->mode |= SB_RATE_LOCK_PLAYBACK;
		snd_sbdsp_command(chip, playback_cmd(chip).dma_on);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_sbdsp_command(chip, playback_cmd(chip).dma_off);
		chip->mode &= ~SB_RATE_LOCK_PLAYBACK;
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static snd_pcm_uframes_t snd_als4000_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	unsigned int result;

	spin_lock(&chip->reg_lock);	
	result = snd_als4k_gcr_read(chip, ALS4K_GCRA4_FIFO2_CURRENT_ADDR);
	spin_unlock(&chip->reg_lock);
	result &= 0xffff;
	return bytes_to_frames( substream->runtime, result );
}

static snd_pcm_uframes_t snd_als4000_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	unsigned result;

	spin_lock(&chip->reg_lock);	
	result = snd_als4k_gcr_read(chip, ALS4K_GCRA0_FIFO1_CURRENT_ADDR);
	spin_unlock(&chip->reg_lock);
	result &= 0xffff;
	return bytes_to_frames( substream->runtime, result );
}

/* FIXME: this IRQ routine doesn't really support IRQ sharing (we always
 * return IRQ_HANDLED no matter whether we actually had an IRQ flag or not).
 * ALS4000a.PDF writes that while ACKing IRQ in PCI block will *not* ACK
 * the IRQ in the SB core, ACKing IRQ in SB block *will* ACK the PCI IRQ
 * register (alt_port + ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU). Probably something
 * could be optimized here to query/write one register only...
 * And even if both registers need to be queried, then there's still the
 * question of whether it's actually correct to ACK PCI IRQ before reading
 * SB IRQ like we do now, since ALS4000a.PDF mentions that PCI IRQ will *clear*
 * SB IRQ status.
 * (hmm, SPECS_PAGE: 38 mentions it the other way around!)
 * And do we *really* need the lock here for *reading* SB_DSP4_IRQSTATUS??
 * */
static irqreturn_t snd_als4000_interrupt(int irq, void *dev_id)
{
	struct snd_sb *chip = dev_id;
	unsigned pci_irqstatus;
	unsigned sb_irqstatus;

	/* find out which bit of the ALS4000 PCI block produced the interrupt,
	   SPECS_PAGE: 38, 5 */
	pci_irqstatus = snd_als4k_iobase_readb(chip->alt_port,
				 ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU);
	if ((pci_irqstatus & ALS4K_IOB_0E_SB_DMA_IRQ)
	 && (chip->playback_substream)) /* playback */
		snd_pcm_period_elapsed(chip->playback_substream);
	if ((pci_irqstatus & ALS4K_IOB_0E_CR1E_IRQ)
	 && (chip->capture_substream)) /* capturing */
		snd_pcm_period_elapsed(chip->capture_substream);
	if ((pci_irqstatus & ALS4K_IOB_0E_MPU_IRQ)
	 && (chip->rmidi)) /* MPU401 interrupt */
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);
	/* ACK the PCI block IRQ */
	snd_als4k_iobase_writeb(chip->alt_port,
			 ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU, pci_irqstatus);
	
	spin_lock(&chip->mixer_lock);
	/* SPECS_PAGE: 20 */
	sb_irqstatus = snd_sbmixer_read(chip, SB_DSP4_IRQSTATUS);
	spin_unlock(&chip->mixer_lock);
	
	if (sb_irqstatus & SB_IRQTYPE_8BIT)
		snd_sb_ack_8bit(chip);
	if (sb_irqstatus & SB_IRQTYPE_16BIT)
		snd_sb_ack_16bit(chip);
	if (sb_irqstatus & SB_IRQTYPE_MPUIN)
		inb(chip->mpu_port);
	if (sb_irqstatus & ALS4K_IRQTYPE_CR1E_DMA)
		snd_als4k_iobase_readb(chip->alt_port,
					ALS4K_IOB_16_ACK_FOR_CR1E);

	/* dev_dbg(chip->card->dev, "als4000: irq 0x%04x 0x%04x\n",
					 pci_irqstatus, sb_irqstatus); */

	/* only ack the things we actually handled above */
	return IRQ_RETVAL(
	     (pci_irqstatus & (ALS4K_IOB_0E_SB_DMA_IRQ|ALS4K_IOB_0E_CR1E_IRQ|
				ALS4K_IOB_0E_MPU_IRQ))
	  || (sb_irqstatus & (SB_IRQTYPE_8BIT|SB_IRQTYPE_16BIT|
				SB_IRQTYPE_MPUIN|ALS4K_IRQTYPE_CR1E_DMA))
	);
}

/*****************************************************************/

static const struct snd_pcm_hardware snd_als4000_playback =
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

static const struct snd_pcm_hardware snd_als4000_capture =
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

static int snd_als4000_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	chip->playback_substream = substream;
	runtime->hw = snd_als4000_playback;
	return 0;
}

static int snd_als4000_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	return 0;
}

static int snd_als4000_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	chip->capture_substream = substream;
	runtime->hw = snd_als4000_capture;
	return 0;
}

static int snd_als4000_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_sb *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	return 0;
}

/******************************************************************/

static const struct snd_pcm_ops snd_als4000_playback_ops = {
	.open =		snd_als4000_playback_open,
	.close =	snd_als4000_playback_close,
	.prepare =	snd_als4000_playback_prepare,
	.trigger =	snd_als4000_playback_trigger,
	.pointer =	snd_als4000_playback_pointer
};

static const struct snd_pcm_ops snd_als4000_capture_ops = {
	.open =		snd_als4000_capture_open,
	.close =	snd_als4000_capture_close,
	.prepare =	snd_als4000_capture_prepare,
	.trigger =	snd_als4000_capture_trigger,
	.pointer =	snd_als4000_capture_pointer
};

static int snd_als4000_pcm(struct snd_sb *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(chip->card, "ALS4000 DSP", device, 1, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_als4000_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_als4000_capture_ops);

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
				       &chip->pci->dev, 64*1024, 64*1024);

	chip->pcm = pcm;

	return 0;
}

/******************************************************************/

static void snd_als4000_set_addr(unsigned long iobase,
					unsigned int sb_io,
					unsigned int mpu_io,
					unsigned int opl_io,
					unsigned int game_io)
{
	u32 cfg1 = 0;
	u32 cfg2 = 0;

	if (mpu_io > 0)
		cfg2 |= (mpu_io | 1) << 16;
	if (sb_io > 0)
		cfg2 |= (sb_io | 1);
	if (game_io > 0)
		cfg1 |= (game_io | 1) << 16;
	if (opl_io > 0)
		cfg1 |= (opl_io | 1);
	snd_als4k_gcr_write_addr(iobase, ALS4K_GCRA8_LEGACY_CFG1, cfg1);
	snd_als4k_gcr_write_addr(iobase, ALS4K_GCRA9_LEGACY_CFG2, cfg2);
}

static void snd_als4000_configure(struct snd_sb *chip)
{
	u8 tmp;
	int i;

	/* do some more configuration */
	spin_lock_irq(&chip->mixer_lock);
	tmp = snd_als4_cr_read(chip, ALS4K_CR0_SB_CONFIG);
	snd_als4_cr_write(chip, ALS4K_CR0_SB_CONFIG,
				tmp|ALS4K_CR0_MX80_81_REG_WRITE_ENABLE);
	/* always select DMA channel 0, since we do not actually use DMA
	 * SPECS_PAGE: 19/20 */
	snd_sbmixer_write(chip, SB_DSP4_DMASETUP, SB_DMASETUP_DMA0);
	snd_als4_cr_write(chip, ALS4K_CR0_SB_CONFIG,
				 tmp & ~ALS4K_CR0_MX80_81_REG_WRITE_ENABLE);
	spin_unlock_irq(&chip->mixer_lock);
	
	spin_lock_irq(&chip->reg_lock);
	/* enable interrupts */
	snd_als4k_gcr_write(chip, ALS4K_GCR8C_MISC_CTRL,
					ALS4K_GCR8C_IRQ_MASK_CTRL_ENABLE);

	/* SPECS_PAGE: 39 */
	for (i = ALS4K_GCR91_DMA0_ADDR; i <= ALS4K_GCR96_DMA3_MODE_COUNT; ++i)
		snd_als4k_gcr_write(chip, i, 0);
	/* enable burst mode to prevent dropouts during high PCI bus usage */
	snd_als4k_gcr_write(chip, ALS4K_GCR99_DMA_EMULATION_CTRL,
		(snd_als4k_gcr_read(chip, ALS4K_GCR99_DMA_EMULATION_CTRL) & ~0x07) | 0x04);
	spin_unlock_irq(&chip->reg_lock);
}

#ifdef SUPPORT_JOYSTICK
static int snd_als4000_create_gameport(struct snd_card_als4000 *acard, int dev)
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
		dev_warn(&acard->pci->dev, "cannot reserve joystick ports\n");
		return -EBUSY;
	}

	acard->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(&acard->pci->dev, "cannot allocate memory for gameport\n");
		release_and_free_resource(r);
		return -ENOMEM;
	}

	gameport_set_name(gp, "ALS4000 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(acard->pci));
	gameport_set_dev_parent(gp, &acard->pci->dev);
	gp->io = io_port;
	gameport_set_port_data(gp, r);

	/* Enable legacy joystick port */
	snd_als4000_set_addr(acard->iobase, 0, 0, 0, 1);

	gameport_register_port(acard->gameport);

	return 0;
}

static void snd_als4000_free_gameport(struct snd_card_als4000 *acard)
{
	if (acard->gameport) {
		struct resource *r = gameport_get_port_data(acard->gameport);

		gameport_unregister_port(acard->gameport);
		acard->gameport = NULL;

		/* disable joystick */
		snd_als4000_set_addr(acard->iobase, 0, 0, 0, 0);

		release_and_free_resource(r);
	}
}
#else
static inline int snd_als4000_create_gameport(struct snd_card_als4000 *acard, int dev) { return -ENOSYS; }
static inline void snd_als4000_free_gameport(struct snd_card_als4000 *acard) { }
#endif

static void snd_card_als4000_free( struct snd_card *card )
{
	struct snd_card_als4000 *acard = card->private_data;

	/* make sure that interrupts are disabled */
	snd_als4k_gcr_write_addr(acard->iobase, ALS4K_GCR8C_MISC_CTRL, 0);
	/* free resources */
	snd_als4000_free_gameport(acard);
	pci_release_regions(acard->pci);
	pci_disable_device(acard->pci);
}

static int snd_card_als4000_probe(struct pci_dev *pci,
				  const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_card_als4000 *acard;
	unsigned long iobase;
	struct snd_sb *chip;
	struct snd_opl3 *opl3;
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
	if (dma_set_mask(&pci->dev, DMA_BIT_MASK(24)) < 0 ||
	    dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(24)) < 0) {
		dev_err(&pci->dev, "architecture does not support 24bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	if ((err = pci_request_regions(pci, "ALS4000")) < 0) {
		pci_disable_device(pci);
		return err;
	}
	iobase = pci_resource_start(pci, 0);

	pci_read_config_word(pci, PCI_COMMAND, &word);
	pci_write_config_word(pci, PCI_COMMAND, word | PCI_COMMAND_IO);
	pci_set_master(pci);
	
	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(*acard) /* private_data: acard */,
			   &card);
	if (err < 0) {
		pci_release_regions(pci);
		pci_disable_device(pci);
		return err;
	}

	acard = card->private_data;
	acard->pci = pci;
	acard->iobase = iobase;
	card->private_free = snd_card_als4000_free;

	/* disable all legacy ISA stuff */
	snd_als4000_set_addr(acard->iobase, 0, 0, 0, 0);

	if ((err = snd_sbdsp_create(card,
				    iobase + ALS4K_IOB_10_ADLIB_ADDR0,
				    pci->irq,
		/* internally registered as IRQF_SHARED in case of ALS4000 SB */
				    snd_als4000_interrupt,
				    -1,
				    -1,
				    SB_HW_ALS4000,
				    &chip)) < 0) {
		goto out_err;
	}
	acard->chip = chip;

	chip->pci = pci;
	chip->alt_port = iobase;

	snd_als4000_configure(chip);

	strcpy(card->driver, "ALS4000");
	strcpy(card->shortname, "Avance Logic ALS4000");
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->alt_port, chip->irq);

	if ((err = snd_mpu401_uart_new( card, 0, MPU401_HW_ALS4000,
					iobase + ALS4K_IOB_30_MIDI_DATA,
					MPU401_INFO_INTEGRATED |
					MPU401_INFO_IRQ_HOOK,
					-1, &chip->rmidi)) < 0) {
		dev_err(&pci->dev, "no MPU-401 device at 0x%lx?\n",
				iobase + ALS4K_IOB_30_MIDI_DATA);
		goto out_err;
	}
	/* FIXME: ALS4000 has interesting MPU401 configuration features
	 * at ALS4K_CR1A_MPU401_UART_MODE_CONTROL
	 * (pass-thru / UART switching, fast MIDI clock, etc.),
	 * however there doesn't seem to be an ALSA API for this...
	 * SPECS_PAGE: 21 */

	if ((err = snd_als4000_pcm(chip, 0)) < 0) {
		goto out_err;
	}
	if ((err = snd_sbmixer_new(chip)) < 0) {
		goto out_err;
	}	    

	if (snd_opl3_create(card,
				iobase + ALS4K_IOB_10_ADLIB_ADDR0,
				iobase + ALS4K_IOB_12_ADLIB_ADDR2,
			    OPL3_HW_AUTO, 1, &opl3) < 0) {
		dev_err(&pci->dev, "no OPL device at 0x%lx-0x%lx?\n",
			   iobase + ALS4K_IOB_10_ADLIB_ADDR0,
			   iobase + ALS4K_IOB_12_ADLIB_ADDR2);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			goto out_err;
		}
	}

	snd_als4000_create_gameport(acard, dev);

	if ((err = snd_card_register(card)) < 0) {
		goto out_err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	err = 0;
	goto out;

out_err:
	snd_card_free(card);
	
out:
	return err;
}

static void snd_card_als4000_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

#ifdef CONFIG_PM_SLEEP
static int snd_als4000_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_card_als4000 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	
	snd_sbmixer_suspend(chip);
	return 0;
}

static int snd_als4000_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_card_als4000 *acard = card->private_data;
	struct snd_sb *chip = acard->chip;

	snd_als4000_configure(chip);
	snd_sbdsp_reset(chip);
	snd_sbmixer_resume(chip);

#ifdef SUPPORT_JOYSTICK
	if (acard->gameport)
		snd_als4000_set_addr(acard->iobase, 0, 0, 0, 1);
#endif

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_als4000_pm, snd_als4000_suspend, snd_als4000_resume);
#define SND_ALS4000_PM_OPS	&snd_als4000_pm
#else
#define SND_ALS4000_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver als4000_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_als4000_ids,
	.probe = snd_card_als4000_probe,
	.remove = snd_card_als4000_remove,
	.driver = {
		.pm = SND_ALS4000_PM_OPS,
	},
};

module_pci_driver(als4000_driver);
