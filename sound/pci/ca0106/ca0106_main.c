// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.25
 *
 *  FEATURES currently supported:
 *    Front, Rear and Center/LFE.
 *    Surround40 and Surround51.
 *    Capture from MIC an LINE IN input.
 *    SPDIF digital playback of PCM stereo and AC3/DTS works.
 *    (One can use a standard mono mini-jack to one RCA plugs cable.
 *     or one can use a standard stereo mini-jack to two RCA plugs cable.
 *     Plug one of the RCA plugs into the Coax input of the external decoder/receiver.)
 *    ( In theory one could output 3 different AC3 streams at once, to 3 different SPDIF outputs. )
 *    Notes on how to capture sound:
 *      The AC97 is used in the PLAYBACK direction.
 *      The output from the AC97 chip, instead of reaching the speakers, is fed into the Philips 1361T ADC.
 *      So, to record from the MIC, set the MIC Playback volume to max,
 *      unmute the MIC and turn up the MASTER Playback volume.
 *      So, to prevent feedback when capturing, minimise the "Capture feedback into Playback" volume.
 *   
 *    The only playback controls that currently do anything are: -
 *    Analog Front
 *    Analog Rear
 *    Analog Center/LFE
 *    SPDIF Front
 *    SPDIF Rear
 *    SPDIF Center/LFE
 *   
 *    For capture from Mic in or Line in.
 *    Digital/Analog ( switch must be in Analog mode for CAPTURE. )
 * 
 *    CAPTURE feedback into PLAYBACK
 * 
 *  Changelog:
 *    Support interrupts per period.
 *    Removed noise from Center/LFE channel when in Analog mode.
 *    Rename and remove mixer controls.
 *  0.0.6
 *    Use separate card based DMA buffer for periods table list.
 *  0.0.7
 *    Change remove and rename ctrls into lists.
 *  0.0.8
 *    Try to fix capture sources.
 *  0.0.9
 *    Fix AC3 output.
 *    Enable S32_LE format support.
 *  0.0.10
 *    Enable playback 48000 and 96000 rates. (Rates other that these do not work, even with "plug:front".)
 *  0.0.11
 *    Add Model name recognition.
 *  0.0.12
 *    Correct interrupt timing. interrupt at end of period, instead of in the middle of a playback period.
 *    Remove redundent "voice" handling.
 *  0.0.13
 *    Single trigger call for multi channels.
 *  0.0.14
 *    Set limits based on what the sound card hardware can do.
 *    playback periods_min=2, periods_max=8
 *    capture hw constraints require period_size = n * 64 bytes.
 *    playback hw constraints require period_size = n * 64 bytes.
 *  0.0.15
 *    Minor updates.
 *  0.0.16
 *    Implement 192000 sample rate.
 *  0.0.17
 *    Add support for SB0410 and SB0413.
 *  0.0.18
 *    Modified Copyright message.
 *  0.0.19
 *    Finally fix support for SB Live 24 bit. SB0410 and SB0413.
 *    The output codec needs resetting, otherwise all output is muted.
 *  0.0.20
 *    Merge "pci_disable_device(pci);" fixes.
 *  0.0.21
 *    Add 4 capture channels. (SPDIF only comes in on channel 0. )
 *    Add SPDIF capture using optional digital I/O module for SB Live 24bit. (Analog capture does not yet work.)
 *  0.0.22
 *    Add support for MSI K8N Diamond Motherboard with onboard SB Live 24bit without AC97. From kiksen, bug #901
 *  0.0.23
 *    Implement support for Line-in capture on SB Live 24bit.
 *  0.0.24
 *    Add support for mute control on SB Live 24bit (cards w/ SPI DAC)
 *  0.0.25
 *    Powerdown SPI DAC channels when not in use
 *
 *  BUGS:
 *    Some stability problems when unloading the snd-ca0106 kernel module.
 *    --
 *
 *  TODO:
 *    4 Capture channels, only one implemented so far.
 *    Other capture rates apart from 48khz not implemented.
 *    MIDI
 *    --
 *  GENERAL INFO:
 *    Model: SB0310
 *    P17 Chip: CA0106-DAT
 *    AC97 Codec: STAC 9721
 *    ADC: Philips 1361T (Stereo 24bit)
 *    DAC: WM8746EDS (6-channel, 24bit, 192Khz)
 *
 *  GENERAL INFO:
 *    Model: SB0410
 *    P17 Chip: CA0106-DAT
 *    AC97 Codec: None
 *    ADC: WM8775EDS (4 Channel)
 *    DAC: CS4382 (114 dB, 24-Bit, 192 kHz, 8-Channel D/A Converter with DSD Support)
 *    SPDIF Out control switches between Mic in and SPDIF out.
 *    No sound out or mic input working yet.
 * 
 *  GENERAL INFO:
 *    Model: SB0413
 *    P17 Chip: CA0106-DAT
 *    AC97 Codec: None.
 *    ADC: Unknown
 *    DAC: Unknown
 *    Trying to handle it like the SB0410.
 *
 *  This code was initially based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>

MODULE_AUTHOR("James Courtier-Dutton <James@superbug.demon.co.uk>");
MODULE_DESCRIPTION("CA0106");
MODULE_LICENSE("GPL");

// module parameters (see "Module Parameters")
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static uint subsystem[SNDRV_CARDS]; /* Force card subsystem model */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the CA0106 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the CA0106 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the CA0106 soundcard.");
module_param_array(subsystem, uint, NULL, 0444);
MODULE_PARM_DESC(subsystem, "Force card subsystem model.");

#include "ca0106.h"

static const struct snd_ca0106_details ca0106_chip_details[] = {
	 /* Sound Blaster X-Fi Extreme Audio. This does not have an AC97. 53SB079000000 */
	 /* It is really just a normal SB Live 24bit. */
	 /* Tested:
	  * See ALSA bug#3251
	  */
	 { .serial = 0x10131102,
	   .name   = "X-Fi Extreme Audio [SBxxxx]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 /* Sound Blaster X-Fi Extreme Audio. This does not have an AC97. 53SB079000000 */
	 /* It is really just a normal SB Live 24bit. */
	 /*
 	  * CTRL:CA0111-WTLF
	  * ADC: WM8775SEDS
	  * DAC: CS4382-KQZ
	  */
	 /* Tested:
	  * Playback on front, rear, center/lfe speakers
	  * Capture from Mic in.
	  * Not-Tested:
	  * Capture from Line in.
	  * Playback to digital out.
	  */
	 { .serial = 0x10121102,
	   .name   = "X-Fi Extreme Audio [SB0790]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 /* New Dell Sound Blaster Live! 7.1 24bit. This does not have an AC97.  */
	 /* AudigyLS[SB0310] */
	 { .serial = 0x10021102,
	   .name   = "AudigyLS [SB0310]",
	   .ac97   = 1 } , 
	 /* Unknown AudigyLS that also says SB0310 on it */
	 { .serial = 0x10051102,
	   .name   = "AudigyLS [SB0310b]",
	   .ac97   = 1 } ,
	 /* New Sound Blaster Live! 7.1 24bit. This does not have an AC97. 53SB041000001 */
	 { .serial = 0x10061102,
	   .name   = "Live! 7.1 24bit [SB0410]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 /* New Dell Sound Blaster Live! 7.1 24bit. This does not have an AC97.  */
	 { .serial = 0x10071102,
	   .name   = "Live! 7.1 24bit [SB0413]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 /* New Audigy SE. Has a different DAC. */
	 /* SB0570:
	  * CTRL:CA0106-DAT
	  * ADC: WM8775EDS
	  * DAC: WM8768GEDS
	  */
	 { .serial = 0x100a1102,
	   .name   = "Audigy SE [SB0570]",
	   .gpio_type = 1,
	   .i2c_adc = 1,
	   .spi_dac = 0x4021 } ,
	 /* New Audigy LS. Has a different DAC. */
	 /* SB0570:
	  * CTRL:CA0106-DAT
	  * ADC: WM8775EDS
	  * DAC: WM8768GEDS
	  */
	 { .serial = 0x10111102,
	   .name   = "Audigy SE OEM [SB0570a]",
	   .gpio_type = 1,
	   .i2c_adc = 1,
	   .spi_dac = 0x4021 } ,
	/* Sound Blaster 5.1vx
	 * Tested: Playback on front, rear, center/lfe speakers
	 * Not-Tested: Capture
	 */
	{ .serial = 0x10041102,
	  .name   = "Sound Blaster 5.1vx [SB1070]",
	  .gpio_type = 1,
	  .i2c_adc = 0,
	  .spi_dac = 0x0124
	 } ,
	 /* MSI K8N Diamond Motherboard with onboard SB Live 24bit without AC97 */
	 /* SB0438
	  * CTRL:CA0106-DAT
	  * ADC: WM8775SEDS
	  * DAC: CS4382-KQZ
	  */
	 { .serial = 0x10091462,
	   .name   = "MSI K8N Diamond MB [SB0438]",
	   .gpio_type = 2,
	   .i2c_adc = 1 } ,
	 /* MSI K8N Diamond PLUS MB */
	 { .serial = 0x10091102,
	   .name   = "MSI K8N Diamond MB",
	   .gpio_type = 2,
	   .i2c_adc = 1,
	   .spi_dac = 0x4021 } ,
	/* Giga-byte GA-G1975X mobo
	 * Novell bnc#395807
	 */
	/* FIXME: the GPIO and I2C setting aren't tested well */
	{ .serial = 0x1458a006,
	  .name = "Giga-byte GA-G1975X",
	  .gpio_type = 1,
	  .i2c_adc = 1 },
	 /* Shuttle XPC SD31P which has an onboard Creative Labs
	  * Sound Blaster Live! 24-bit EAX
	  * high-definition 7.1 audio processor".
	  * Added using info from andrewvegan in alsa bug #1298
	  */
	 { .serial = 0x30381297,
	   .name   = "Shuttle XPC SD31P [SD31P]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	/* Shuttle XPC SD11G5 which has an onboard Creative Labs
	 * Sound Blaster Live! 24-bit EAX
	 * high-definition 7.1 audio processor".
	 * Fixes ALSA bug#1600
         */
	{ .serial = 0x30411297,
	  .name = "Shuttle XPC SD11G5 [SD11G5]",
	  .gpio_type = 1,
	  .i2c_adc = 1 } ,
	 { .serial = 0,
	   .name   = "AudigyLS [Unknown]" }
};

/* hardware definition */
static const struct snd_pcm_hardware snd_ca0106_playback_hw = {
	.info =			SNDRV_PCM_INFO_MMAP | 
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_SYNC_START,
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		(SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000),
	.rate_min =		48000,
	.rate_max =		192000,
	.channels_min =		2,  //1,
	.channels_max =		2,  //6,
	.buffer_bytes_max =	((65536 - 64) * 8),
	.period_bytes_min =	64,
	.period_bytes_max =	(65536 - 64),
	.periods_min =		2,
	.periods_max =		8,
	.fifo_size =		0,
};

static const struct snd_pcm_hardware snd_ca0106_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
#if 0 /* FIXME: looks like 44.1kHz capture causes noisy output on 48kHz */
	.rates =		(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000),
	.rate_min =		44100,
#else
	.rates =		(SNDRV_PCM_RATE_48000 |
				 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000),
	.rate_min =		48000,
#endif /* FIXME */
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	65536 - 128,
	.period_bytes_min =	64,
	.period_bytes_max =	32768 - 64,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

unsigned int snd_ca0106_ptr_read(struct snd_ca0106 * emu, 
					  unsigned int reg, 
					  unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
  
	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + CA0106_PTR);
	val = inl(emu->port + CA0106_DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

void snd_ca0106_ptr_write(struct snd_ca0106 *emu, 
				   unsigned int reg, 
				   unsigned int chn, 
				   unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + CA0106_PTR);
	outl(data, emu->port + CA0106_DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

int snd_ca0106_spi_write(struct snd_ca0106 * emu,
				   unsigned int data)
{
	unsigned int reset, set;
	unsigned int reg, tmp;
	int n, result;
	reg = SPI;
	if (data > 0xffff) /* Only 16bit values allowed */
		return 1;
	tmp = snd_ca0106_ptr_read(emu, reg, 0);
	reset = (tmp & ~0x3ffff) | 0x20000; /* Set xxx20000 */
	set = reset | 0x10000; /* Set xxx1xxxx */
	snd_ca0106_ptr_write(emu, reg, 0, reset | data);
	tmp = snd_ca0106_ptr_read(emu, reg, 0); /* write post */
	snd_ca0106_ptr_write(emu, reg, 0, set | data);
	result = 1;
	/* Wait for status bit to return to 0 */
	for (n = 0; n < 100; n++) {
		udelay(10);
		tmp = snd_ca0106_ptr_read(emu, reg, 0);
		if (!(tmp & 0x10000)) {
			result = 0;
			break;
		}
	}
	if (result) /* Timed out */
		return 1;
	snd_ca0106_ptr_write(emu, reg, 0, reset | data);
	tmp = snd_ca0106_ptr_read(emu, reg, 0); /* Write post */
	return 0;
}

/* The ADC does not support i2c read, so only write is implemented */
int snd_ca0106_i2c_write(struct snd_ca0106 *emu,
				u32 reg,
				u32 value)
{
	u32 tmp;
	int timeout = 0;
	int status;
	int retry;
	if ((reg > 0x7f) || (value > 0x1ff)) {
		dev_err(emu->card->dev, "i2c_write: invalid values.\n");
		return -EINVAL;
	}

	tmp = reg << 25 | value << 16;
	/*
	dev_dbg(emu->card->dev, "I2C-write:reg=0x%x, value=0x%x\n", reg, value);
	*/
	/* Not sure what this I2C channel controls. */
	/* snd_ca0106_ptr_write(emu, I2C_D0, 0, tmp); */

	/* This controls the I2C connected to the WM8775 ADC Codec */
	snd_ca0106_ptr_write(emu, I2C_D1, 0, tmp);

	for (retry = 0; retry < 10; retry++) {
		/* Send the data to i2c */
		//tmp = snd_ca0106_ptr_read(emu, I2C_A, 0);
		//tmp = tmp & ~(I2C_A_ADC_READ|I2C_A_ADC_LAST|I2C_A_ADC_START|I2C_A_ADC_ADD_MASK);
		tmp = 0;
		tmp = tmp | (I2C_A_ADC_LAST|I2C_A_ADC_START|I2C_A_ADC_ADD);
		snd_ca0106_ptr_write(emu, I2C_A, 0, tmp);

		/* Wait till the transaction ends */
		while (1) {
			status = snd_ca0106_ptr_read(emu, I2C_A, 0);
			/*dev_dbg(emu->card->dev, "I2C:status=0x%x\n", status);*/
			timeout++;
			if ((status & I2C_A_ADC_START) == 0)
				break;

			if (timeout > 1000)
				break;
		}
		//Read back and see if the transaction is successful
		if ((status & I2C_A_ADC_ABORT) == 0)
			break;
	}

	if (retry == 10) {
		dev_err(emu->card->dev, "Writing to ADC failed!\n");
		return -EINVAL;
	}
    
    	return 0;
}


static void snd_ca0106_intr_enable(struct snd_ca0106 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int intr_enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	intr_enable = inl(emu->port + CA0106_INTE) | intrenb;
	outl(intr_enable, emu->port + CA0106_INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_ca0106_intr_disable(struct snd_ca0106 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int intr_enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	intr_enable = inl(emu->port + CA0106_INTE) & ~intrenb;
	outl(intr_enable, emu->port + CA0106_INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}


static void snd_ca0106_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static const int spi_dacd_reg[] = {
	SPI_DACD0_REG,
	SPI_DACD1_REG,
	SPI_DACD2_REG,
	0,
	SPI_DACD4_REG,
};
static const int spi_dacd_bit[] = {
	SPI_DACD0_BIT,
	SPI_DACD1_BIT,
	SPI_DACD2_BIT,
	0,
	SPI_DACD4_BIT,
};

static void restore_spdif_bits(struct snd_ca0106 *chip, int idx)
{
	if (chip->spdif_str_bits[idx] != chip->spdif_bits[idx]) {
		chip->spdif_str_bits[idx] = chip->spdif_bits[idx];
		snd_ca0106_ptr_write(chip, SPCS0 + idx, 0,
				     chip->spdif_str_bits[idx]);
	}
}

static int snd_ca0106_channel_dac(struct snd_ca0106 *chip,
				  const struct snd_ca0106_details *details,
				  int channel_id)
{
	switch (channel_id) {
	case PCM_FRONT_CHANNEL:
		return (details->spi_dac & 0xf000) >> (4 * 3);
	case PCM_REAR_CHANNEL:
		return (details->spi_dac & 0x0f00) >> (4 * 2);
	case PCM_CENTER_LFE_CHANNEL:
		return (details->spi_dac & 0x00f0) >> (4 * 1);
	case PCM_UNKNOWN_CHANNEL:
		return (details->spi_dac & 0x000f) >> (4 * 0);
	default:
		dev_dbg(chip->card->dev, "ca0106: unknown channel_id %d\n",
			   channel_id);
	}
	return 0;
}

static int snd_ca0106_pcm_power_dac(struct snd_ca0106 *chip, int channel_id,
				    int power)
{
	if (chip->details->spi_dac) {
		const int dac = snd_ca0106_channel_dac(chip, chip->details,
						       channel_id);
		const int reg = spi_dacd_reg[dac];
		const int bit = spi_dacd_bit[dac];

		if (power)
			/* Power up */
			chip->spi_dac_reg[reg] &= ~bit;
		else
			/* Power down */
			chip->spi_dac_reg[reg] |= bit;
		if (snd_ca0106_spi_write(chip, chip->spi_dac_reg[reg]) != 0)
			return -ENXIO;
	}
	return 0;
}

/* open_playback callback */
static int snd_ca0106_pcm_open_playback_channel(struct snd_pcm_substream *substream,
						int channel_id)
{
	struct snd_ca0106 *chip = snd_pcm_substream_chip(substream);
        struct snd_ca0106_channel *channel = &(chip->playback_channels[channel_id]);
	struct snd_ca0106_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);

	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = chip;
	epcm->substream = substream;
        epcm->channel_id=channel_id;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_ca0106_pcm_free_substream;
  
	runtime->hw = snd_ca0106_playback_hw;

        channel->emu = chip;
        channel->number = channel_id;

	channel->use = 1;
	/*
	dev_dbg(chip->card->dev, "open:channel_id=%d, chip=%p, channel=%p\n",
	       channel_id, chip, channel);
	*/
        //channel->interrupt = snd_ca0106_pcm_channel_interrupt;
	channel->epcm = epcm;
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
                return err;
	err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	if (err < 0)
                return err;
	snd_pcm_set_sync(substream);

	/* Front channel dac should already be on */
	if (channel_id != PCM_FRONT_CHANNEL) {
		err = snd_ca0106_pcm_power_dac(chip, channel_id, 1);
		if (err < 0)
			return err;
	}

	restore_spdif_bits(chip, channel_id);

	return 0;
}

/* close callback */
static int snd_ca0106_pcm_close_playback(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_ca0106_pcm *epcm = runtime->private_data;
	chip->playback_channels[epcm->channel_id].use = 0;

	restore_spdif_bits(chip, epcm->channel_id);

	/* Front channel dac should stay on */
	if (epcm->channel_id != PCM_FRONT_CHANNEL) {
		int err;
		err = snd_ca0106_pcm_power_dac(chip, epcm->channel_id, 0);
		if (err < 0)
			return err;
	}

	/* FIXME: maybe zero others */
	return 0;
}

static int snd_ca0106_pcm_open_playback_front(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_FRONT_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_center_lfe(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_CENTER_LFE_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_unknown(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_UNKNOWN_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_rear(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_REAR_CHANNEL);
}

/* open_capture callback */
static int snd_ca0106_pcm_open_capture_channel(struct snd_pcm_substream *substream,
					       int channel_id)
{
	struct snd_ca0106 *chip = snd_pcm_substream_chip(substream);
        struct snd_ca0106_channel *channel = &(chip->capture_channels[channel_id]);
	struct snd_ca0106_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (!epcm)
		return -ENOMEM;

	epcm->emu = chip;
	epcm->substream = substream;
        epcm->channel_id=channel_id;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_ca0106_pcm_free_substream;
  
	runtime->hw = snd_ca0106_capture_hw;

        channel->emu = chip;
        channel->number = channel_id;

	channel->use = 1;
	/*
	dev_dbg(chip->card->dev, "open:channel_id=%d, chip=%p, channel=%p\n",
	       channel_id, chip, channel);
	*/
        //channel->interrupt = snd_ca0106_pcm_channel_interrupt;
        channel->epcm = epcm;
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
                return err;
	//snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_capture_period_sizes);
	err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	if (err < 0)
                return err;
	return 0;
}

/* close callback */
static int snd_ca0106_pcm_close_capture(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_ca0106_pcm *epcm = runtime->private_data;
	chip->capture_channels[epcm->channel_id].use = 0;
	/* FIXME: maybe zero others */
	return 0;
}

static int snd_ca0106_pcm_open_0_capture(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 0);
}

static int snd_ca0106_pcm_open_1_capture(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 1);
}

static int snd_ca0106_pcm_open_2_capture(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 2);
}

static int snd_ca0106_pcm_open_3_capture(struct snd_pcm_substream *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 3);
}

/* prepare playback callback */
static int snd_ca0106_pcm_prepare_playback(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ca0106_pcm *epcm = runtime->private_data;
	int channel = epcm->channel_id;
	u32 *table_base = (u32 *)(emu->buffer->area+(8*16*channel));
	u32 period_size_bytes = frames_to_bytes(runtime, runtime->period_size);
	u32 hcfg_mask = HCFG_PLAYBACK_S32_LE;
	u32 hcfg_set = 0x00000000;
	u32 hcfg;
	u32 reg40_mask = 0x30000 << (channel<<1);
	u32 reg40_set = 0;
	u32 reg40;
	/* FIXME: Depending on mixer selection of SPDIF out or not, select the spdif rate or the DAC rate. */
	u32 reg71_mask = 0x03030000 ; /* Global. Set SPDIF rate. We only support 44100 to spdif, not to DAC. */
	u32 reg71_set = 0;
	u32 reg71;
	int i;
	
#if 0 /* debug */
	dev_dbg(emu->card->dev,
		   "prepare:channel_number=%d, rate=%d, format=0x%x, "
		   "channels=%d, buffer_size=%ld, period_size=%ld, "
		   "periods=%u, frames_to_bytes=%d\n",
		   channel, runtime->rate, runtime->format,
		   runtime->channels, runtime->buffer_size,
		   runtime->period_size, runtime->periods,
		   frames_to_bytes(runtime, 1));
	dev_dbg(emu->card->dev,
		"dma_addr=%x, dma_area=%p, table_base=%p\n",
		   runtime->dma_addr, runtime->dma_area, table_base);
	dev_dbg(emu->card->dev,
		"dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",
		   emu->buffer->addr, emu->buffer->area, emu->buffer->bytes);
#endif /* debug */
	/* Rate can be set per channel. */
	/* reg40 control host to fifo */
	/* reg71 controls DAC rate. */
	switch (runtime->rate) {
	case 44100:
		reg40_set = 0x10000 << (channel<<1);
		reg71_set = 0x01010000; 
		break;
        case 48000:
		reg40_set = 0;
		reg71_set = 0; 
		break;
	case 96000:
		reg40_set = 0x20000 << (channel<<1);
		reg71_set = 0x02020000; 
		break;
	case 192000:
		reg40_set = 0x30000 << (channel<<1);
		reg71_set = 0x03030000; 
		break;
	default:
		reg40_set = 0;
		reg71_set = 0; 
		break;
	}
	/* Format is a global setting */
	/* FIXME: Only let the first channel accessed set this. */
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hcfg_set = 0;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		hcfg_set = HCFG_PLAYBACK_S32_LE;
		break;
	default:
		hcfg_set = 0;
		break;
	}
	hcfg = inl(emu->port + CA0106_HCFG) ;
	hcfg = (hcfg & ~hcfg_mask) | hcfg_set;
	outl(hcfg, emu->port + CA0106_HCFG);
	reg40 = snd_ca0106_ptr_read(emu, 0x40, 0);
	reg40 = (reg40 & ~reg40_mask) | reg40_set;
	snd_ca0106_ptr_write(emu, 0x40, 0, reg40);
	reg71 = snd_ca0106_ptr_read(emu, 0x71, 0);
	reg71 = (reg71 & ~reg71_mask) | reg71_set;
	snd_ca0106_ptr_write(emu, 0x71, 0, reg71);

	/* FIXME: Check emu->buffer->size before actually writing to it. */
        for(i=0; i < runtime->periods; i++) {
		table_base[i*2] = runtime->dma_addr + (i * period_size_bytes);
		table_base[i*2+1] = period_size_bytes << 16;
	}
 
	snd_ca0106_ptr_write(emu, PLAYBACK_LIST_ADDR, channel, emu->buffer->addr+(8*16*channel));
	snd_ca0106_ptr_write(emu, PLAYBACK_LIST_SIZE, channel, (runtime->periods - 1) << 19);
	snd_ca0106_ptr_write(emu, PLAYBACK_LIST_PTR, channel, 0);
	snd_ca0106_ptr_write(emu, PLAYBACK_DMA_ADDR, channel, runtime->dma_addr);
	snd_ca0106_ptr_write(emu, PLAYBACK_PERIOD_SIZE, channel, frames_to_bytes(runtime, runtime->period_size)<<16); // buffer size in bytes
	/* FIXME  test what 0 bytes does. */
	snd_ca0106_ptr_write(emu, PLAYBACK_PERIOD_SIZE, channel, 0); // buffer size in bytes
	snd_ca0106_ptr_write(emu, PLAYBACK_POINTER, channel, 0);
	snd_ca0106_ptr_write(emu, 0x07, channel, 0x0);
	snd_ca0106_ptr_write(emu, 0x08, channel, 0);
        snd_ca0106_ptr_write(emu, PLAYBACK_MUTE, 0x0, 0x0); /* Unmute output */
#if 0
	snd_ca0106_ptr_write(emu, SPCS0, 0,
			       SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
			       SPCS_GENERATIONSTATUS | 0x00001200 |
			       0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT );
#endif

	return 0;
}

/* prepare capture callback */
static int snd_ca0106_pcm_prepare_capture(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ca0106_pcm *epcm = runtime->private_data;
	int channel = epcm->channel_id;
	u32 hcfg_mask = HCFG_CAPTURE_S32_LE;
	u32 hcfg_set = 0x00000000;
	u32 hcfg;
	u32 over_sampling=0x2;
	u32 reg71_mask = 0x0000c000 ; /* Global. Set ADC rate. */
	u32 reg71_set = 0;
	u32 reg71;
	
#if 0 /* debug */
	dev_dbg(emu->card->dev,
		   "prepare:channel_number=%d, rate=%d, format=0x%x, "
		   "channels=%d, buffer_size=%ld, period_size=%ld, "
		   "periods=%u, frames_to_bytes=%d\n",
		   channel, runtime->rate, runtime->format,
		   runtime->channels, runtime->buffer_size,
		   runtime->period_size, runtime->periods,
		   frames_to_bytes(runtime, 1));
	dev_dbg(emu->card->dev,
		"dma_addr=%x, dma_area=%p, table_base=%p\n",
		   runtime->dma_addr, runtime->dma_area, table_base);
	dev_dbg(emu->card->dev,
		"dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",
		   emu->buffer->addr, emu->buffer->area, emu->buffer->bytes);
#endif /* debug */
	/* reg71 controls ADC rate. */
	switch (runtime->rate) {
	case 44100:
		reg71_set = 0x00004000;
		break;
        case 48000:
		reg71_set = 0; 
		break;
	case 96000:
		reg71_set = 0x00008000;
		over_sampling=0xa;
		break;
	case 192000:
		reg71_set = 0x0000c000; 
		over_sampling=0xa;
		break;
	default:
		reg71_set = 0; 
		break;
	}
	/* Format is a global setting */
	/* FIXME: Only let the first channel accessed set this. */
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		hcfg_set = 0;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		hcfg_set = HCFG_CAPTURE_S32_LE;
		break;
	default:
		hcfg_set = 0;
		break;
	}
	hcfg = inl(emu->port + CA0106_HCFG) ;
	hcfg = (hcfg & ~hcfg_mask) | hcfg_set;
	outl(hcfg, emu->port + CA0106_HCFG);
	reg71 = snd_ca0106_ptr_read(emu, 0x71, 0);
	reg71 = (reg71 & ~reg71_mask) | reg71_set;
	snd_ca0106_ptr_write(emu, 0x71, 0, reg71);
        if (emu->details->i2c_adc == 1) { /* The SB0410 and SB0413 use I2C to control ADC. */
	        snd_ca0106_i2c_write(emu, ADC_MASTER, over_sampling); /* Adjust the over sampler to better suit the capture rate. */
	}


	/*
	dev_dbg(emu->card->dev,
	       "prepare:channel_number=%d, rate=%d, format=0x%x, channels=%d, "
	       "buffer_size=%ld, period_size=%ld, frames_to_bytes=%d\n",
	       channel, runtime->rate, runtime->format, runtime->channels,
	       runtime->buffer_size, runtime->period_size,
	       frames_to_bytes(runtime, 1));
	*/
	snd_ca0106_ptr_write(emu, 0x13, channel, 0);
	snd_ca0106_ptr_write(emu, CAPTURE_DMA_ADDR, channel, runtime->dma_addr);
	snd_ca0106_ptr_write(emu, CAPTURE_BUFFER_SIZE, channel, frames_to_bytes(runtime, runtime->buffer_size)<<16); // buffer size in bytes
	snd_ca0106_ptr_write(emu, CAPTURE_POINTER, channel, 0);

	return 0;
}

/* trigger_playback callback */
static int snd_ca0106_pcm_trigger_playback(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime;
	struct snd_ca0106_pcm *epcm;
	int channel;
	int result = 0;
        struct snd_pcm_substream *s;
	u32 basic = 0;
	u32 extended = 0;
	u32 bits;
	int running = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	default:
		running = 0;
		break;
	}
        snd_pcm_group_for_each_entry(s, substream) {
		if (snd_pcm_substream_chip(s) != emu ||
		    s->stream != SNDRV_PCM_STREAM_PLAYBACK)
			continue;
		runtime = s->runtime;
		epcm = runtime->private_data;
		channel = epcm->channel_id;
		/* dev_dbg(emu->card->dev, "channel=%d\n", channel); */
		epcm->running = running;
		basic |= (0x1 << channel);
		extended |= (0x10 << channel);
                snd_pcm_trigger_done(s, substream);
        }
	/* dev_dbg(emu->card->dev, "basic=0x%x, extended=0x%x\n",basic, extended); */

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		bits = snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0);
		bits |= extended;
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, bits);
		bits = snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0);
		bits |= basic;
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, bits);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		bits = snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0);
		bits &= ~basic;
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, bits);
		bits = snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0);
		bits &= ~extended;
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, bits);
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* trigger_capture callback */
static int snd_ca0106_pcm_trigger_capture(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ca0106_pcm *epcm = runtime->private_data;
	int channel = epcm->channel_id;
	int result = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0) | (0x110000<<channel));
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0)|(0x100<<channel));
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0) & ~(0x100<<channel));
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0) & ~(0x110000<<channel));
		epcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer_playback callback */
static snd_pcm_uframes_t
snd_ca0106_pcm_pointer_playback(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ca0106_pcm *epcm = runtime->private_data;
	unsigned int ptr, prev_ptr;
	int channel = epcm->channel_id;
	int timeout = 10;

	if (!epcm->running)
		return 0;

	prev_ptr = -1;
	do {
		ptr = snd_ca0106_ptr_read(emu, PLAYBACK_LIST_PTR, channel);
		ptr = (ptr >> 3) * runtime->period_size;
		ptr += bytes_to_frames(runtime,
			snd_ca0106_ptr_read(emu, PLAYBACK_POINTER, channel));
		if (ptr >= runtime->buffer_size)
			ptr -= runtime->buffer_size;
		if (prev_ptr == ptr)
			return ptr;
		prev_ptr = ptr;
	} while (--timeout);
	dev_warn(emu->card->dev, "ca0106: unstable DMA pointer!\n");
	return 0;
}

/* pointer_capture callback */
static snd_pcm_uframes_t
snd_ca0106_pcm_pointer_capture(struct snd_pcm_substream *substream)
{
	struct snd_ca0106 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ca0106_pcm *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2 = 0;
	int channel = epcm->channel_id;

	if (!epcm->running)
		return 0;

	ptr1 = snd_ca0106_ptr_read(emu, CAPTURE_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;
	/*
	dev_dbg(emu->card->dev, "ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, "
	       "buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n",
	       ptr1, ptr2, ptr, (int)runtime->buffer_size,
	       (int)runtime->period_size, (int)runtime->frame_bits,
	       (int)runtime->rate);
	*/
	return ptr;
}

/* operators */
static const struct snd_pcm_ops snd_ca0106_playback_front_ops = {
	.open =        snd_ca0106_pcm_open_playback_front,
	.close =       snd_ca0106_pcm_close_playback,
	.prepare =     snd_ca0106_pcm_prepare_playback,
	.trigger =     snd_ca0106_pcm_trigger_playback,
	.pointer =     snd_ca0106_pcm_pointer_playback,
};

static const struct snd_pcm_ops snd_ca0106_capture_0_ops = {
	.open =        snd_ca0106_pcm_open_0_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static const struct snd_pcm_ops snd_ca0106_capture_1_ops = {
	.open =        snd_ca0106_pcm_open_1_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static const struct snd_pcm_ops snd_ca0106_capture_2_ops = {
	.open =        snd_ca0106_pcm_open_2_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static const struct snd_pcm_ops snd_ca0106_capture_3_ops = {
	.open =        snd_ca0106_pcm_open_3_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static const struct snd_pcm_ops snd_ca0106_playback_center_lfe_ops = {
        .open =         snd_ca0106_pcm_open_playback_center_lfe,
        .close =        snd_ca0106_pcm_close_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};

static const struct snd_pcm_ops snd_ca0106_playback_unknown_ops = {
        .open =         snd_ca0106_pcm_open_playback_unknown,
        .close =        snd_ca0106_pcm_close_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};

static const struct snd_pcm_ops snd_ca0106_playback_rear_ops = {
        .open =         snd_ca0106_pcm_open_playback_rear,
        .close =        snd_ca0106_pcm_close_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};


static unsigned short snd_ca0106_ac97_read(struct snd_ac97 *ac97,
					     unsigned short reg)
{
	struct snd_ca0106 *emu = ac97->private_data;
	unsigned long flags;
	unsigned short val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + CA0106_AC97ADDRESS);
	val = inw(emu->port + CA0106_AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_ca0106_ac97_write(struct snd_ac97 *ac97,
				    unsigned short reg, unsigned short val)
{
	struct snd_ca0106 *emu = ac97->private_data;
	unsigned long flags;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + CA0106_AC97ADDRESS);
	outw(val, emu->port + CA0106_AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static int snd_ca0106_ac97(struct snd_ca0106 *chip)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static const struct snd_ac97_bus_ops ops = {
		.write = snd_ca0106_ac97_write,
		.read = snd_ca0106_ac97_read,
	};
  
	err = snd_ac97_bus(chip->card, 0, &ops, NULL, &pbus);
	if (err < 0)
		return err;
	pbus->no_vra = 1; /* we don't need VRA */

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.scaps = AC97_SCAP_NO_SPDIF;
	return snd_ac97_mixer(pbus, &ac97, &chip->ac97);
}

static void ca0106_stop_chip(struct snd_ca0106 *chip);

static void snd_ca0106_free(struct snd_card *card)
{
	struct snd_ca0106 *chip = card->private_data;

	ca0106_stop_chip(chip);
}

static irqreturn_t snd_ca0106_interrupt(int irq, void *dev_id)
{
	unsigned int status;

	struct snd_ca0106 *chip = dev_id;
	int i;
	int mask;
        unsigned int stat76;
	struct snd_ca0106_channel *pchannel;

	status = inl(chip->port + CA0106_IPR);
	if (! status)
		return IRQ_NONE;

        stat76 = snd_ca0106_ptr_read(chip, EXTENDED_INT, 0);
	/*
	dev_dbg(emu->card->dev, "interrupt status = 0x%08x, stat76=0x%08x\n",
		   status, stat76);
	dev_dbg(emu->card->dev, "ptr=0x%08x\n",
		   snd_ca0106_ptr_read(chip, PLAYBACK_POINTER, 0));
	*/
        mask = 0x11; /* 0x1 for one half, 0x10 for the other half period. */
	for(i = 0; i < 4; i++) {
		pchannel = &(chip->playback_channels[i]);
		if (stat76 & mask) {
/* FIXME: Select the correct substream for period elapsed */
			if(pchannel->use) {
				snd_pcm_period_elapsed(pchannel->epcm->substream);
				/* dev_dbg(emu->card->dev, "interrupt [%d] used\n", i); */
                        }
		}
		/*
		dev_dbg(emu->card->dev, "channel=%p\n", pchannel);
		dev_dbg(emu->card->dev, "interrupt stat76[%d] = %08x, use=%d, channel=%d\n", i, stat76, pchannel->use, pchannel->number);
		*/
		mask <<= 1;
	}
        mask = 0x110000; /* 0x1 for one half, 0x10 for the other half period. */
	for(i = 0; i < 4; i++) {
		pchannel = &(chip->capture_channels[i]);
		if (stat76 & mask) {
/* FIXME: Select the correct substream for period elapsed */
			if(pchannel->use) {
				snd_pcm_period_elapsed(pchannel->epcm->substream);
				/* dev_dbg(emu->card->dev, "interrupt [%d] used\n", i); */
                        }
		}
		/*
		dev_dbg(emu->card->dev, "channel=%p\n", pchannel);
		dev_dbg(emu->card->dev, "interrupt stat76[%d] = %08x, use=%d, channel=%d\n", i, stat76, pchannel->use, pchannel->number);
		*/
		mask <<= 1;
	}

        snd_ca0106_ptr_write(chip, EXTENDED_INT, 0, stat76);

	if (chip->midi.dev_id &&
	    (status & (chip->midi.ipr_tx|chip->midi.ipr_rx))) {
		if (chip->midi.interrupt)
			chip->midi.interrupt(&chip->midi, status);
		else
			chip->midi.interrupt_disable(&chip->midi, chip->midi.tx_enable | chip->midi.rx_enable);
	}

	// acknowledge the interrupt if necessary
	outl(status, chip->port + CA0106_IPR);

	return IRQ_HANDLED;
}

static const struct snd_pcm_chmap_elem surround_map[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ }
};

static const struct snd_pcm_chmap_elem clfe_map[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE } },
	{ }
};

static const struct snd_pcm_chmap_elem side_map[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_SL, SNDRV_CHMAP_SR } },
	{ }
};

static int snd_ca0106_pcm(struct snd_ca0106 *emu, int device)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	const struct snd_pcm_chmap_elem *map = NULL;
	int err;
  
	err = snd_pcm_new(emu->card, "ca0106", device, 1, 1, &pcm);
	if (err < 0)
		return err;
  
	pcm->private_data = emu;

	switch (device) {
	case 0:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_front_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_0_ops);
	  map = snd_pcm_std_chmaps;
          break;
	case 1:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_rear_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_1_ops);
	  map = surround_map;
          break;
	case 2:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_center_lfe_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_2_ops);
	  map = clfe_map;
          break;
	case 3:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_unknown_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_3_ops);
	  map = side_map;
          break;
        }

	pcm->info_flags = 0;
	strcpy(pcm->name, "CA0106");

	for(substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; 
	    substream; 
	    substream = substream->next) {
		snd_pcm_set_managed_buffer(substream, SNDRV_DMA_TYPE_DEV,
					   &emu->pci->dev,
					   64*1024, 64*1024);
	}

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; 
	      substream; 
	      substream = substream->next) {
		snd_pcm_set_managed_buffer(substream, SNDRV_DMA_TYPE_DEV,
					   &emu->pci->dev,
					   64*1024, 64*1024);
	}
  
	err = snd_pcm_add_chmap_ctls(pcm, SNDRV_PCM_STREAM_PLAYBACK, map, 2,
				     1 << 2, NULL);
	if (err < 0)
		return err;

	emu->pcm[device] = pcm;
  
	return 0;
}

#define SPI_REG(reg, value)	(((reg) << SPI_REG_SHIFT) | (value))
static const unsigned int spi_dac_init[] = {
	SPI_REG(SPI_LDA1_REG,	SPI_DA_BIT_0dB), /* 0dB dig. attenuation */
	SPI_REG(SPI_RDA1_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_PL_REG,	SPI_PL_BIT_L_L | SPI_PL_BIT_R_R | SPI_IZD_BIT),
	SPI_REG(SPI_FMT_REG,	SPI_FMT_BIT_I2S | SPI_IWL_BIT_24),
	SPI_REG(SPI_LDA2_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_RDA2_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_LDA3_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_RDA3_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_MASTDA_REG,	SPI_DA_BIT_0dB),
	SPI_REG(9,		0x00),
	SPI_REG(SPI_MS_REG,	SPI_DACD0_BIT | SPI_DACD1_BIT | SPI_DACD2_BIT),
	SPI_REG(12,		0x00),
	SPI_REG(SPI_LDA4_REG,	SPI_DA_BIT_0dB),
	SPI_REG(SPI_RDA4_REG,	SPI_DA_BIT_0dB | SPI_DA_BIT_UPDATE),
	SPI_REG(SPI_DACD4_REG,	SPI_DACD4_BIT),
};

static const unsigned int i2c_adc_init[][2] = {
	{ 0x17, 0x00 }, /* Reset */
	{ 0x07, 0x00 }, /* Timeout */
	{ 0x0b, 0x22 },  /* Interface control */
	{ 0x0c, 0x22 },  /* Master mode control */
	{ 0x0d, 0x08 },  /* Powerdown control */
	{ 0x0e, 0xcf },  /* Attenuation Left  0x01 = -103dB, 0xff = 24dB */
	{ 0x0f, 0xcf },  /* Attenuation Right 0.5dB steps */
	{ 0x10, 0x7b },  /* ALC Control 1 */
	{ 0x11, 0x00 },  /* ALC Control 2 */
	{ 0x12, 0x32 },  /* ALC Control 3 */
	{ 0x13, 0x00 },  /* Noise gate control */
	{ 0x14, 0xa6 },  /* Limiter control */
	{ 0x15, ADC_MUX_LINEIN },  /* ADC Mixer control */
};

static void ca0106_init_chip(struct snd_ca0106 *chip, int resume)
{
	int ch;
	unsigned int def_bits;

	outl(0, chip->port + CA0106_INTE);

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
	def_bits =
		SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
		SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
		SPCS_GENERATIONSTATUS | 0x00001200 |
		0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT;
	if (!resume) {
		chip->spdif_str_bits[0] = chip->spdif_bits[0] = def_bits;
		chip->spdif_str_bits[1] = chip->spdif_bits[1] = def_bits;
		chip->spdif_str_bits[2] = chip->spdif_bits[2] = def_bits;
		chip->spdif_str_bits[3] = chip->spdif_bits[3] = def_bits;
	}
	/* Only SPCS1 has been tested */
	snd_ca0106_ptr_write(chip, SPCS1, 0, chip->spdif_str_bits[1]);
	snd_ca0106_ptr_write(chip, SPCS0, 0, chip->spdif_str_bits[0]);
	snd_ca0106_ptr_write(chip, SPCS2, 0, chip->spdif_str_bits[2]);
	snd_ca0106_ptr_write(chip, SPCS3, 0, chip->spdif_str_bits[3]);

        snd_ca0106_ptr_write(chip, PLAYBACK_MUTE, 0, 0x00fc0000);
        snd_ca0106_ptr_write(chip, CAPTURE_MUTE, 0, 0x00fc0000);

        /* Write 0x8000 to AC97_REC_GAIN to mute it. */
        outb(AC97_REC_GAIN, chip->port + CA0106_AC97ADDRESS);
        outw(0x8000, chip->port + CA0106_AC97DATA);
#if 0 /* FIXME: what are these? */
	snd_ca0106_ptr_write(chip, SPCS0, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x42, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x43, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x44, 0, 0x2108006);
#endif

	/* OSS drivers set this. */
	/* snd_ca0106_ptr_write(chip, SPDIF_SELECT2, 0, 0xf0f003f); */

	/* Analog or Digital output */
	snd_ca0106_ptr_write(chip, SPDIF_SELECT1, 0, 0xf);
	/* 0x0b000000 for digital, 0x000b0000 for analog, from win2000 drivers.
	 * Use 0x000f0000 for surround71
	 */
	snd_ca0106_ptr_write(chip, SPDIF_SELECT2, 0, 0x000f0000);

	chip->spdif_enable = 0; /* Set digital SPDIF output off */
	/*snd_ca0106_ptr_write(chip, 0x45, 0, 0);*/ /* Analogue out */
	/*snd_ca0106_ptr_write(chip, 0x45, 0, 0xf00);*/ /* Digital out */

	/* goes to 0x40c80000 when doing SPDIF IN/OUT */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 0, 0x40c81000);
	/* (Mute) CAPTURE feedback into PLAYBACK volume.
	 * Only lower 16 bits matter.
	 */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 1, 0xffffffff);
	/* SPDIF IN Volume */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 2, 0x30300000);
	/* SPDIF IN Volume, 0x70 = (vol & 0x3f) | 0x40 */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 3, 0x00700000);

	snd_ca0106_ptr_write(chip, PLAYBACK_ROUTING1, 0, 0x32765410);
	snd_ca0106_ptr_write(chip, PLAYBACK_ROUTING2, 0, 0x76767676);
	snd_ca0106_ptr_write(chip, CAPTURE_ROUTING1, 0, 0x32765410);
	snd_ca0106_ptr_write(chip, CAPTURE_ROUTING2, 0, 0x76767676);

	for (ch = 0; ch < 4; ch++) {
		/* Only high 16 bits matter */
		snd_ca0106_ptr_write(chip, CAPTURE_VOLUME1, ch, 0x30303030);
		snd_ca0106_ptr_write(chip, CAPTURE_VOLUME2, ch, 0x30303030);
#if 0 /* Mute */
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME1, ch, 0x40404040);
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME2, ch, 0x40404040);
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME1, ch, 0xffffffff);
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME2, ch, 0xffffffff);
#endif
	}
	if (chip->details->i2c_adc == 1) {
	        /* Select MIC, Line in, TAD in, AUX in */
	        snd_ca0106_ptr_write(chip, CAPTURE_SOURCE, 0x0, 0x333300e4);
		/* Default to CAPTURE_SOURCE to i2s in */
		if (!resume)
			chip->capture_source = 3;
	} else if (chip->details->ac97 == 1) {
	        /* Default to AC97 in */
	        snd_ca0106_ptr_write(chip, CAPTURE_SOURCE, 0x0, 0x444400e4);
		/* Default to CAPTURE_SOURCE to AC97 in */
		if (!resume)
			chip->capture_source = 4;
	} else {
	        /* Select MIC, Line in, TAD in, AUX in */
	        snd_ca0106_ptr_write(chip, CAPTURE_SOURCE, 0x0, 0x333300e4);
		/* Default to Set CAPTURE_SOURCE to i2s in */
		if (!resume)
			chip->capture_source = 3;
	}

	if (chip->details->gpio_type == 2) {
		/* The SB0438 use GPIO differently. */
		/* FIXME: Still need to find out what the other GPIO bits do.
		 * E.g. For digital spdif out.
		 */
		outl(0x0, chip->port + CA0106_GPIO);
		/* outl(0x00f0e000, chip->port + CA0106_GPIO); */ /* Analog */
		outl(0x005f5301, chip->port + CA0106_GPIO); /* Analog */
	} else if (chip->details->gpio_type == 1) {
		/* The SB0410 and SB0413 use GPIO differently. */
		/* FIXME: Still need to find out what the other GPIO bits do.
		 * E.g. For digital spdif out.
		 */
		outl(0x0, chip->port + CA0106_GPIO);
		/* outl(0x00f0e000, chip->port + CA0106_GPIO); */ /* Analog */
		outl(0x005f5301, chip->port + CA0106_GPIO); /* Analog */
	} else {
		outl(0x0, chip->port + CA0106_GPIO);
		outl(0x005f03a3, chip->port + CA0106_GPIO); /* Analog */
		/* outl(0x005f02a2, chip->port + CA0106_GPIO); */ /* SPDIF */
	}
	snd_ca0106_intr_enable(chip, 0x105); /* Win2000 uses 0x1e0 */

	/* outl(HCFG_LOCKSOUNDCACHE|HCFG_AUDIOENABLE, chip->port+HCFG); */
	/* 0x1000 causes AC3 to fails. Maybe it effects 24 bit output. */
	/* outl(0x00001409, chip->port + CA0106_HCFG); */
	/* outl(0x00000009, chip->port + CA0106_HCFG); */
	/* AC97 2.0, Enable outputs. */
	outl(HCFG_AC97 | HCFG_AUDIOENABLE, chip->port + CA0106_HCFG);

	if (chip->details->i2c_adc == 1) {
		/* The SB0410 and SB0413 use I2C to control ADC. */
		int size, n;

		size = ARRAY_SIZE(i2c_adc_init);
		/* dev_dbg(emu->card->dev, "I2C:array size=0x%x\n", size); */
		for (n = 0; n < size; n++)
			snd_ca0106_i2c_write(chip, i2c_adc_init[n][0],
					     i2c_adc_init[n][1]);
		for (n = 0; n < 4; n++) {
			chip->i2c_capture_volume[n][0] = 0xcf;
			chip->i2c_capture_volume[n][1] = 0xcf;
		}
		chip->i2c_capture_source = 2; /* Line in */
		/* Enable Line-in capture. MIC in currently untested. */
		/* snd_ca0106_i2c_write(chip, ADC_MUX, ADC_MUX_LINEIN); */
	}

	if (chip->details->spi_dac) {
		/* The SB0570 use SPI to control DAC. */
		int size, n;

		size = ARRAY_SIZE(spi_dac_init);
		for (n = 0; n < size; n++) {
			int reg = spi_dac_init[n] >> SPI_REG_SHIFT;

			snd_ca0106_spi_write(chip, spi_dac_init[n]);
			if (reg < ARRAY_SIZE(chip->spi_dac_reg))
				chip->spi_dac_reg[reg] = spi_dac_init[n];
		}

		/* Enable front dac only */
		snd_ca0106_pcm_power_dac(chip, PCM_FRONT_CHANNEL, 1);
	}
}

static void ca0106_stop_chip(struct snd_ca0106 *chip)
{
	/* disable interrupts */
	snd_ca0106_ptr_write(chip, BASIC_INTERRUPT, 0, 0);
	outl(0, chip->port + CA0106_INTE);
	snd_ca0106_ptr_write(chip, EXTENDED_INT_MASK, 0, 0);
	udelay(1000);
	/* disable audio */
	/* outl(HCFG_LOCKSOUNDCACHE, chip->port + HCFG); */
	outl(0, chip->port + CA0106_HCFG);
	/* FIXME: We need to stop and DMA transfers here.
	 *        But as I am not sure how yet, we cannot from the dma pages.
	 * So we can fix: snd-malloc: Memory leak?  pages not freed = 8
	 */
}

static int snd_ca0106_create(int dev, struct snd_card *card,
			     struct pci_dev *pci)
{
	struct snd_ca0106 *chip = card->private_data;
	const struct snd_ca0106_details *c;
	int err;

	err = pcim_enable_device(pci);
	if (err < 0)
		return err;
	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(32))) {
		dev_err(card->dev, "error to set 32bit mask DMA\n");
		return -ENXIO;
	}

	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	spin_lock_init(&chip->emu_lock);

	err = pci_request_regions(pci, "snd_ca0106");
	if (err < 0)
		return err;
	chip->port = pci_resource_start(pci, 0);

	if (devm_request_irq(&pci->dev, pci->irq, snd_ca0106_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "cannot grab irq\n");
		return -EBUSY;
	}
	chip->irq = pci->irq;
	card->sync_irq = chip->irq;

	/* This stores the periods table. */
	chip->buffer = snd_devm_alloc_pages(&pci->dev, SNDRV_DMA_TYPE_DEV, 1024);
	if (!chip->buffer)
		return -ENOMEM;

	pci_set_master(pci);
	/* read serial */
	pci_read_config_dword(pci, PCI_SUBSYSTEM_VENDOR_ID, &chip->serial);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &chip->model);
	dev_info(card->dev, "Model %04x Rev %08x Serial %08x\n",
	       chip->model, pci->revision, chip->serial);
	strcpy(card->driver, "CA0106");
	strcpy(card->shortname, "CA0106");

	for (c = ca0106_chip_details; c->serial; c++) {
		if (subsystem[dev]) {
			if (c->serial == subsystem[dev])
				break;
		} else if (c->serial == chip->serial)
			break;
	}
	chip->details = c;
	if (subsystem[dev]) {
		dev_info(card->dev, "Sound card name=%s, "
		       "subsystem=0x%x. Forced to subsystem=0x%x\n",
		       c->name, chip->serial, subsystem[dev]);
	}

	sprintf(card->longname, "%s at 0x%lx irq %i",
		c->name, chip->port, chip->irq);

	ca0106_init_chip(chip, 0);
	return 0;
}


static void ca0106_midi_interrupt_enable(struct snd_ca_midi *midi, int intr)
{
	snd_ca0106_intr_enable((struct snd_ca0106 *)(midi->dev_id), intr);
}

static void ca0106_midi_interrupt_disable(struct snd_ca_midi *midi, int intr)
{
	snd_ca0106_intr_disable((struct snd_ca0106 *)(midi->dev_id), intr);
}

static unsigned char ca0106_midi_read(struct snd_ca_midi *midi, int idx)
{
	return (unsigned char)snd_ca0106_ptr_read((struct snd_ca0106 *)(midi->dev_id),
						  midi->port + idx, 0);
}

static void ca0106_midi_write(struct snd_ca_midi *midi, int data, int idx)
{
	snd_ca0106_ptr_write((struct snd_ca0106 *)(midi->dev_id), midi->port + idx, 0, data);
}

static struct snd_card *ca0106_dev_id_card(void *dev_id)
{
	return ((struct snd_ca0106 *)dev_id)->card;
}

static int ca0106_dev_id_port(void *dev_id)
{
	return ((struct snd_ca0106 *)dev_id)->port;
}

static int snd_ca0106_midi(struct snd_ca0106 *chip, unsigned int channel)
{
	struct snd_ca_midi *midi;
	char *name;
	int err;

	if (channel == CA0106_MIDI_CHAN_B) {
		name = "CA0106 MPU-401 (UART) B";
		midi =  &chip->midi2;
		midi->tx_enable = INTE_MIDI_TX_B;
		midi->rx_enable = INTE_MIDI_RX_B;
		midi->ipr_tx = IPR_MIDI_TX_B;
		midi->ipr_rx = IPR_MIDI_RX_B;
		midi->port = MIDI_UART_B_DATA;
	} else {
		name = "CA0106 MPU-401 (UART)";
		midi =  &chip->midi;
		midi->tx_enable = INTE_MIDI_TX_A;
		midi->rx_enable = INTE_MIDI_TX_B;
		midi->ipr_tx = IPR_MIDI_TX_A;
		midi->ipr_rx = IPR_MIDI_RX_A;
		midi->port = MIDI_UART_A_DATA;
	}

	midi->reset = CA0106_MPU401_RESET;
	midi->enter_uart = CA0106_MPU401_ENTER_UART;
	midi->ack = CA0106_MPU401_ACK;

	midi->input_avail = CA0106_MIDI_INPUT_AVAIL;
	midi->output_ready = CA0106_MIDI_OUTPUT_READY;

	midi->channel = channel;

	midi->interrupt_enable = ca0106_midi_interrupt_enable;
	midi->interrupt_disable = ca0106_midi_interrupt_disable;

	midi->read = ca0106_midi_read;
	midi->write = ca0106_midi_write;

	midi->get_dev_id_card = ca0106_dev_id_card;
	midi->get_dev_id_port = ca0106_dev_id_port;

	midi->dev_id = chip;
	
	err = ca_midi_init(chip, midi, 0, name);
	if (err < 0)
		return err;

	return 0;
}


static int snd_ca0106_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_ca0106 *chip;
	int i, err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_devm_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
				sizeof(*chip), &card);
	if (err < 0)
		return err;
	chip = card->private_data;

	err = snd_ca0106_create(dev, card, pci);
	if (err < 0)
		return err;
	card->private_free = snd_ca0106_free;

	for (i = 0; i < 4; i++) {
		err = snd_ca0106_pcm(chip, i);
		if (err < 0)
			return err;
	}

	if (chip->details->ac97 == 1) {
		/* The SB0410 and SB0413 do not have an AC97 chip. */
		err = snd_ca0106_ac97(chip);
		if (err < 0)
			return err;
	}
	err = snd_ca0106_mixer(chip);
	if (err < 0)
		return err;

	dev_dbg(card->dev, "probe for MIDI channel A ...");
	err = snd_ca0106_midi(chip, CA0106_MIDI_CHAN_A);
	if (err < 0)
		return err;
	dev_dbg(card->dev, " done.\n");

#ifdef CONFIG_SND_PROC_FS
	snd_ca0106_proc_init(chip);
#endif

	err = snd_card_register(card);
	if (err < 0)
		return err;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int snd_ca0106_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ca0106 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	if (chip->details->ac97)
		snd_ac97_suspend(chip->ac97);
	snd_ca0106_mixer_suspend(chip);

	ca0106_stop_chip(chip);
	return 0;
}

static int snd_ca0106_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ca0106 *chip = card->private_data;
	int i;

	ca0106_init_chip(chip, 1);

	if (chip->details->ac97)
		snd_ac97_resume(chip->ac97);
	snd_ca0106_mixer_resume(chip);
	if (chip->details->spi_dac) {
		for (i = 0; i < ARRAY_SIZE(chip->spi_dac_reg); i++)
			snd_ca0106_spi_write(chip, chip->spi_dac_reg[i]);
	}

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_ca0106_pm, snd_ca0106_suspend, snd_ca0106_resume);
#define SND_CA0106_PM_OPS	&snd_ca0106_pm
#else
#define SND_CA0106_PM_OPS	NULL
#endif

// PCI IDs
static const struct pci_device_id snd_ca0106_ids[] = {
	{ PCI_VDEVICE(CREATIVE, 0x0007), 0 },	/* Audigy LS or Live 24bit */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, snd_ca0106_ids);

// pci_driver definition
static struct pci_driver ca0106_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_ca0106_ids,
	.probe = snd_ca0106_probe,
	.driver = {
		.pm = SND_CA0106_PM_OPS,
	},
};

module_pci_driver(ca0106_driver);
