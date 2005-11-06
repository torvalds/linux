/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.23
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
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>

MODULE_AUTHOR("James Courtier-Dutton <James@superbug.demon.co.uk>");
MODULE_DESCRIPTION("CA0106");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Creative,SB CA0106 chip}}");

// module parameters (see "Module Parameters")
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the CA0106 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the CA0106 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the CA0106 soundcard.");

#include "ca0106.h"

static ca0106_details_t ca0106_chip_details[] = {
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
	 /* MSI K8N Diamond Motherboard with onboard SB Live 24bit without AC97 */
	 { .serial = 0x10091462,
	   .name   = "MSI K8N Diamond MB [SB0438]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 /* Shuttle XPC SD31P which has an onboard Creative Labs Sound Blaster Live! 24-bit EAX
	  * high-definition 7.1 audio processor".
	  * Added using info from andrewvegan in alsa bug #1298
	  */
	 { .serial = 0x30381297,
	   .name   = "Shuttle XPC SD31P [SD31P]",
	   .gpio_type = 1,
	   .i2c_adc = 1 } ,
	 { .serial = 0,
	   .name   = "AudigyLS [Unknown]" }
};

/* hardware definition */
static snd_pcm_hardware_t snd_ca0106_playback_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
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

static snd_pcm_hardware_t snd_ca0106_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
	.rate_min =		44100,
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	((65536 - 64) * 8),
	.period_bytes_min =	64,
	.period_bytes_max =	(65536 - 64),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

unsigned int snd_ca0106_ptr_read(ca0106_t * emu, 
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

void snd_ca0106_ptr_write(ca0106_t *emu, 
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

int snd_ca0106_i2c_write(ca0106_t *emu,
				u32 reg,
				u32 value)
{
	u32 tmp;
	int timeout=0;
	int status;
	int retry;
	if ((reg > 0x7f) || (value > 0x1ff))
	{
                snd_printk("i2c_write: invalid values.\n");
		return -EINVAL;
	}

	tmp = reg << 25 | value << 16;
	/* Not sure what this I2C channel controls. */
	/* snd_ca0106_ptr_write(emu, I2C_D0, 0, tmp); */

	/* This controls the I2C connected to the WM8775 ADC Codec */
	snd_ca0106_ptr_write(emu, I2C_D1, 0, tmp);

	for(retry=0;retry<10;retry++)
	{
		/* Send the data to i2c */
		tmp = snd_ca0106_ptr_read(emu, I2C_A, 0);
		tmp = tmp & ~(I2C_A_ADC_READ|I2C_A_ADC_LAST|I2C_A_ADC_START|I2C_A_ADC_ADD_MASK);
		tmp = tmp | (I2C_A_ADC_LAST|I2C_A_ADC_START|I2C_A_ADC_ADD);
		snd_ca0106_ptr_write(emu, I2C_A, 0, tmp);

		/* Wait till the transaction ends */
		while(1)
		{
			status = snd_ca0106_ptr_read(emu, I2C_A, 0);
                	//snd_printk("I2C:status=0x%x\n", status);
			timeout++;
			if((status & I2C_A_ADC_START)==0)
				break;

			if(timeout>1000)
				break;
		}
		//Read back and see if the transaction is successful
		if((status & I2C_A_ADC_ABORT)==0)
			break;
	}

	if(retry==10)
	{
                snd_printk("Writing to ADC failed!\n");
		return -EINVAL;
	}
    
    	return 0;
}


static void snd_ca0106_intr_enable(ca0106_t *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) | intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_ca0106_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	kfree(runtime->private_data);
}

/* open_playback callback */
static int snd_ca0106_pcm_open_playback_channel(snd_pcm_substream_t *substream, int channel_id)
{
	ca0106_t *chip = snd_pcm_substream_chip(substream);
        ca0106_channel_t *channel = &(chip->playback_channels[channel_id]);
	ca0106_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;
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

        channel->use=1;
        //printk("open:channel_id=%d, chip=%p, channel=%p\n",channel_id, chip, channel);
        //channel->interrupt = snd_ca0106_pcm_channel_interrupt;
        channel->epcm=epcm;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
                return err;
	if ((err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64)) < 0)
                return err;
	return 0;
}

/* close callback */
static int snd_ca0106_pcm_close_playback(snd_pcm_substream_t *substream)
{
	ca0106_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        ca0106_pcm_t *epcm = runtime->private_data;
        chip->playback_channels[epcm->channel_id].use=0;
/* FIXME: maybe zero others */
	return 0;
}

static int snd_ca0106_pcm_open_playback_front(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_FRONT_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_center_lfe(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_CENTER_LFE_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_unknown(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_UNKNOWN_CHANNEL);
}

static int snd_ca0106_pcm_open_playback_rear(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_playback_channel(substream, PCM_REAR_CHANNEL);
}

/* open_capture callback */
static int snd_ca0106_pcm_open_capture_channel(snd_pcm_substream_t *substream, int channel_id)
{
	ca0106_t *chip = snd_pcm_substream_chip(substream);
        ca0106_channel_t *channel = &(chip->capture_channels[channel_id]);
	ca0106_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL) {
                snd_printk("open_capture_channel: failed epcm alloc\n");
		return -ENOMEM;
        }
	epcm->emu = chip;
	epcm->substream = substream;
        epcm->channel_id=channel_id;
  
	runtime->private_data = epcm;
	runtime->private_free = snd_ca0106_pcm_free_substream;
  
	runtime->hw = snd_ca0106_capture_hw;

        channel->emu = chip;
        channel->number = channel_id;

        channel->use=1;
        //printk("open:channel_id=%d, chip=%p, channel=%p\n",channel_id, chip, channel);
        //channel->interrupt = snd_ca0106_pcm_channel_interrupt;
        channel->epcm=epcm;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
                return err;
	//snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_capture_period_sizes);
	if ((err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64)) < 0)
                return err;
	return 0;
}

/* close callback */
static int snd_ca0106_pcm_close_capture(snd_pcm_substream_t *substream)
{
	ca0106_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
        ca0106_pcm_t *epcm = runtime->private_data;
        chip->capture_channels[epcm->channel_id].use=0;
/* FIXME: maybe zero others */
	return 0;
}

static int snd_ca0106_pcm_open_0_capture(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 0);
}

static int snd_ca0106_pcm_open_1_capture(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 1);
}

static int snd_ca0106_pcm_open_2_capture(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 2);
}

static int snd_ca0106_pcm_open_3_capture(snd_pcm_substream_t *substream)
{
	return snd_ca0106_pcm_open_capture_channel(substream, 3);
}

/* hw_params callback */
static int snd_ca0106_pcm_hw_params_playback(snd_pcm_substream_t *substream,
				      snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_ca0106_pcm_hw_free_playback(snd_pcm_substream_t *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* hw_params callback */
static int snd_ca0106_pcm_hw_params_capture(snd_pcm_substream_t *substream,
				      snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_ca0106_pcm_hw_free_capture(snd_pcm_substream_t *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* prepare playback callback */
static int snd_ca0106_pcm_prepare_playback(snd_pcm_substream_t *substream)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ca0106_pcm_t *epcm = runtime->private_data;
	int channel = epcm->channel_id;
	u32 *table_base = (u32 *)(emu->buffer.area+(8*16*channel));
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
	
        //snd_printk("prepare:channel_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, periods=%u, frames_to_bytes=%d\n",channel, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size, runtime->periods, frames_to_bytes(runtime, 1));
        //snd_printk("dma_addr=%x, dma_area=%p, table_base=%p\n",runtime->dma_addr, runtime->dma_area, table_base);
	//snd_printk("dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",emu->buffer.addr, emu->buffer.area, emu->buffer.bytes);
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
	hcfg = inl(emu->port + HCFG) ;
	hcfg = (hcfg & ~hcfg_mask) | hcfg_set;
	outl(hcfg, emu->port + HCFG);
	reg40 = snd_ca0106_ptr_read(emu, 0x40, 0);
	reg40 = (reg40 & ~reg40_mask) | reg40_set;
	snd_ca0106_ptr_write(emu, 0x40, 0, reg40);
	reg71 = snd_ca0106_ptr_read(emu, 0x71, 0);
	reg71 = (reg71 & ~reg71_mask) | reg71_set;
	snd_ca0106_ptr_write(emu, 0x71, 0, reg71);

	/* FIXME: Check emu->buffer.size before actually writing to it. */
        for(i=0; i < runtime->periods; i++) {
		table_base[i*2]=runtime->dma_addr+(i*period_size_bytes);
		table_base[(i*2)+1]=period_size_bytes<<16;
	}
 
	snd_ca0106_ptr_write(emu, PLAYBACK_LIST_ADDR, channel, emu->buffer.addr+(8*16*channel));
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
	}
#endif

	return 0;
}

/* prepare capture callback */
static int snd_ca0106_pcm_prepare_capture(snd_pcm_substream_t *substream)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ca0106_pcm_t *epcm = runtime->private_data;
	int channel = epcm->channel_id;
	u32 hcfg_mask = HCFG_CAPTURE_S32_LE;
	u32 hcfg_set = 0x00000000;
	u32 hcfg;
	u32 over_sampling=0x2;
	u32 reg71_mask = 0x0000c000 ; /* Global. Set ADC rate. */
	u32 reg71_set = 0;
	u32 reg71;
	
        //snd_printk("prepare:channel_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, periods=%u, frames_to_bytes=%d\n",channel, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size, runtime->periods, frames_to_bytes(runtime, 1));
        //snd_printk("dma_addr=%x, dma_area=%p, table_base=%p\n",runtime->dma_addr, runtime->dma_area, table_base);
	//snd_printk("dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",emu->buffer.addr, emu->buffer.area, emu->buffer.bytes);
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
	hcfg = inl(emu->port + HCFG) ;
	hcfg = (hcfg & ~hcfg_mask) | hcfg_set;
	outl(hcfg, emu->port + HCFG);
	reg71 = snd_ca0106_ptr_read(emu, 0x71, 0);
	reg71 = (reg71 & ~reg71_mask) | reg71_set;
	snd_ca0106_ptr_write(emu, 0x71, 0, reg71);
        if (emu->details->i2c_adc == 1) { /* The SB0410 and SB0413 use I2C to control ADC. */
	        snd_ca0106_i2c_write(emu, ADC_MASTER, over_sampling); /* Adjust the over sampler to better suit the capture rate. */
	}


        //printk("prepare:channel_number=%d, rate=%d, format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, frames_to_bytes=%d\n",channel, runtime->rate, runtime->format, runtime->channels, runtime->buffer_size, runtime->period_size,  frames_to_bytes(runtime, 1));
	snd_ca0106_ptr_write(emu, 0x13, channel, 0);
	snd_ca0106_ptr_write(emu, CAPTURE_DMA_ADDR, channel, runtime->dma_addr);
	snd_ca0106_ptr_write(emu, CAPTURE_BUFFER_SIZE, channel, frames_to_bytes(runtime, runtime->buffer_size)<<16); // buffer size in bytes
	snd_ca0106_ptr_write(emu, CAPTURE_POINTER, channel, 0);

	return 0;
}

/* trigger_playback callback */
static int snd_ca0106_pcm_trigger_playback(snd_pcm_substream_t *substream,
				    int cmd)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime;
	ca0106_pcm_t *epcm;
	int channel;
	int result = 0;
	struct list_head *pos;
        snd_pcm_substream_t *s;
	u32 basic = 0;
	u32 extended = 0;
	int running=0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running=1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	default:
		running=0;
		break;
	}
        snd_pcm_group_for_each(pos, substream) {
                s = snd_pcm_group_substream_entry(pos);
		runtime = s->runtime;
		epcm = runtime->private_data;
		channel = epcm->channel_id;
		//snd_printk("channel=%d\n",channel);
		epcm->running = running;
		basic |= (0x1<<channel);
		extended |= (0x10<<channel);
                snd_pcm_trigger_done(s, substream);
        }
	//snd_printk("basic=0x%x, extended=0x%x\n",basic, extended);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0) | (extended));
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0)|(basic));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_ca0106_ptr_write(emu, BASIC_INTERRUPT, 0, snd_ca0106_ptr_read(emu, BASIC_INTERRUPT, 0) & ~(basic));
		snd_ca0106_ptr_write(emu, EXTENDED_INT_MASK, 0, snd_ca0106_ptr_read(emu, EXTENDED_INT_MASK, 0) & ~(extended));
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* trigger_capture callback */
static int snd_ca0106_pcm_trigger_capture(snd_pcm_substream_t *substream,
				    int cmd)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ca0106_pcm_t *epcm = runtime->private_data;
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
snd_ca0106_pcm_pointer_playback(snd_pcm_substream_t *substream)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ca0106_pcm_t *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2,ptr3,ptr4 = 0;
	int channel = epcm->channel_id;

	if (!epcm->running)
		return 0;

	ptr3 = snd_ca0106_ptr_read(emu, PLAYBACK_LIST_PTR, channel);
	ptr1 = snd_ca0106_ptr_read(emu, PLAYBACK_POINTER, channel);
	ptr4 = snd_ca0106_ptr_read(emu, PLAYBACK_LIST_PTR, channel);
	if (ptr3 != ptr4) ptr1 = snd_ca0106_ptr_read(emu, PLAYBACK_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr2+= (ptr4 >> 3) * runtime->period_size;
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;
	//printk("ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n", ptr1, ptr2, ptr, (int)runtime->buffer_size, (int)runtime->period_size, (int)runtime->frame_bits, (int)runtime->rate);

	return ptr;
}

/* pointer_capture callback */
static snd_pcm_uframes_t
snd_ca0106_pcm_pointer_capture(snd_pcm_substream_t *substream)
{
	ca0106_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ca0106_pcm_t *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2 = 0;
	int channel = channel=epcm->channel_id;

	if (!epcm->running)
		return 0;

	ptr1 = snd_ca0106_ptr_read(emu, CAPTURE_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;
	//printk("ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n", ptr1, ptr2, ptr, (int)runtime->buffer_size, (int)runtime->period_size, (int)runtime->frame_bits, (int)runtime->rate);

	return ptr;
}

/* operators */
static snd_pcm_ops_t snd_ca0106_playback_front_ops = {
	.open =        snd_ca0106_pcm_open_playback_front,
	.close =       snd_ca0106_pcm_close_playback,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_ca0106_pcm_hw_params_playback,
	.hw_free =     snd_ca0106_pcm_hw_free_playback,
	.prepare =     snd_ca0106_pcm_prepare_playback,
	.trigger =     snd_ca0106_pcm_trigger_playback,
	.pointer =     snd_ca0106_pcm_pointer_playback,
};

static snd_pcm_ops_t snd_ca0106_capture_0_ops = {
	.open =        snd_ca0106_pcm_open_0_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_ca0106_pcm_hw_params_capture,
	.hw_free =     snd_ca0106_pcm_hw_free_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static snd_pcm_ops_t snd_ca0106_capture_1_ops = {
	.open =        snd_ca0106_pcm_open_1_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_ca0106_pcm_hw_params_capture,
	.hw_free =     snd_ca0106_pcm_hw_free_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static snd_pcm_ops_t snd_ca0106_capture_2_ops = {
	.open =        snd_ca0106_pcm_open_2_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_ca0106_pcm_hw_params_capture,
	.hw_free =     snd_ca0106_pcm_hw_free_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static snd_pcm_ops_t snd_ca0106_capture_3_ops = {
	.open =        snd_ca0106_pcm_open_3_capture,
	.close =       snd_ca0106_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_ca0106_pcm_hw_params_capture,
	.hw_free =     snd_ca0106_pcm_hw_free_capture,
	.prepare =     snd_ca0106_pcm_prepare_capture,
	.trigger =     snd_ca0106_pcm_trigger_capture,
	.pointer =     snd_ca0106_pcm_pointer_capture,
};

static snd_pcm_ops_t snd_ca0106_playback_center_lfe_ops = {
        .open =         snd_ca0106_pcm_open_playback_center_lfe,
        .close =        snd_ca0106_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_ca0106_pcm_hw_params_playback,
        .hw_free =      snd_ca0106_pcm_hw_free_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};

static snd_pcm_ops_t snd_ca0106_playback_unknown_ops = {
        .open =         snd_ca0106_pcm_open_playback_unknown,
        .close =        snd_ca0106_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_ca0106_pcm_hw_params_playback,
        .hw_free =      snd_ca0106_pcm_hw_free_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};

static snd_pcm_ops_t snd_ca0106_playback_rear_ops = {
        .open =         snd_ca0106_pcm_open_playback_rear,
        .close =        snd_ca0106_pcm_close_playback,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_ca0106_pcm_hw_params_playback,
		.hw_free =      snd_ca0106_pcm_hw_free_playback,
        .prepare =      snd_ca0106_pcm_prepare_playback,     
        .trigger =      snd_ca0106_pcm_trigger_playback,  
        .pointer =      snd_ca0106_pcm_pointer_playback, 
};


static unsigned short snd_ca0106_ac97_read(ac97_t *ac97,
					     unsigned short reg)
{
	ca0106_t *emu = ac97->private_data;
	unsigned long flags;
	unsigned short val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	val = inw(emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_ca0106_ac97_write(ac97_t *ac97,
				    unsigned short reg, unsigned short val)
{
	ca0106_t *emu = ac97->private_data;
	unsigned long flags;
  
	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	outw(val, emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static int snd_ca0106_ac97(ca0106_t *chip)
{
	ac97_bus_t *pbus;
	ac97_template_t ac97;
	int err;
	static ac97_bus_ops_t ops = {
		.write = snd_ca0106_ac97_write,
		.read = snd_ca0106_ac97_read,
	};
  
	if ((err = snd_ac97_bus(chip->card, 0, &ops, NULL, &pbus)) < 0)
		return err;
	pbus->no_vra = 1; /* we don't need VRA */

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.scaps = AC97_SCAP_NO_SPDIF;
	return snd_ac97_mixer(pbus, &ac97, &chip->ac97);
}

static int snd_ca0106_free(ca0106_t *chip)
{
	if (chip->res_port != NULL) {    /* avoid access to already used hardware */
		// disable interrupts
		snd_ca0106_ptr_write(chip, BASIC_INTERRUPT, 0, 0);
		outl(0, chip->port + INTE);
		snd_ca0106_ptr_write(chip, EXTENDED_INT_MASK, 0, 0);
		udelay(1000);
		// disable audio
		//outl(HCFG_LOCKSOUNDCACHE, chip->port + HCFG);
		outl(0, chip->port + HCFG);
		/* FIXME: We need to stop and DMA transfers here.
		 *        But as I am not sure how yet, we cannot from the dma pages.
		 * So we can fix: snd-malloc: Memory leak?  pages not freed = 8
		 */
	}
	// release the data
#if 1
	if (chip->buffer.area)
		snd_dma_free_pages(&chip->buffer);
#endif

	// release the i/o port
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	// release the irq
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_ca0106_dev_free(snd_device_t *device)
{
	ca0106_t *chip = device->device_data;
	return snd_ca0106_free(chip);
}

static irqreturn_t snd_ca0106_interrupt(int irq, void *dev_id,
					  struct pt_regs *regs)
{
	unsigned int status;

	ca0106_t *chip = dev_id;
	int i;
	int mask;
        unsigned int stat76;
	ca0106_channel_t *pchannel;

	spin_lock(&chip->emu_lock);

	status = inl(chip->port + IPR);

	// call updater, unlock before it
	spin_unlock(&chip->emu_lock);
  
	if (! status)
		return IRQ_NONE;

        stat76 = snd_ca0106_ptr_read(chip, EXTENDED_INT, 0);
	//snd_printk("interrupt status = 0x%08x, stat76=0x%08x\n", status, stat76);
	//snd_printk("ptr=0x%08x\n",snd_ca0106_ptr_read(chip, PLAYBACK_POINTER, 0));
        mask = 0x11; /* 0x1 for one half, 0x10 for the other half period. */
	for(i = 0; i < 4; i++) {
		pchannel = &(chip->playback_channels[i]);
		if(stat76 & mask) {
/* FIXME: Select the correct substream for period elapsed */
			if(pchannel->use) {
                          snd_pcm_period_elapsed(pchannel->epcm->substream);
	                //printk(KERN_INFO "interrupt [%d] used\n", i);
                        }
		}
	        //printk(KERN_INFO "channel=%p\n",pchannel);
	        //printk(KERN_INFO "interrupt stat76[%d] = %08x, use=%d, channel=%d\n", i, stat76, pchannel->use, pchannel->number);
		mask <<= 1;
	}
        mask = 0x110000; /* 0x1 for one half, 0x10 for the other half period. */
	for(i = 0; i < 4; i++) {
		pchannel = &(chip->capture_channels[i]);
		if(stat76 & mask) {
/* FIXME: Select the correct substream for period elapsed */
			if(pchannel->use) {
                          snd_pcm_period_elapsed(pchannel->epcm->substream);
	                //printk(KERN_INFO "interrupt [%d] used\n", i);
                        }
		}
	        //printk(KERN_INFO "channel=%p\n",pchannel);
	        //printk(KERN_INFO "interrupt stat76[%d] = %08x, use=%d, channel=%d\n", i, stat76, pchannel->use, pchannel->number);
		mask <<= 1;
	}

        snd_ca0106_ptr_write(chip, EXTENDED_INT, 0, stat76);
	spin_lock(&chip->emu_lock);
	// acknowledge the interrupt if necessary
	outl(status, chip->port+IPR);

	spin_unlock(&chip->emu_lock);

	return IRQ_HANDLED;
}

static void snd_ca0106_pcm_free(snd_pcm_t *pcm)
{
	ca0106_t *emu = pcm->private_data;
	emu->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_ca0106_pcm(ca0106_t *emu, int device, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	int err;
  
	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(emu->card, "ca0106", device, 1, 1, &pcm)) < 0)
		return err;
  
	pcm->private_data = emu;
	pcm->private_free = snd_ca0106_pcm_free;

	switch (device) {
	case 0:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_front_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_0_ops);
          break;
	case 1:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_rear_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_1_ops);
          break;
	case 2:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_center_lfe_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_2_ops);
          break;
	case 3:
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ca0106_playback_unknown_ops);
	  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ca0106_capture_3_ops);
          break;
        }

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "CA0106");
	emu->pcm = pcm;

	for(substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; 
	    substream; 
	    substream = substream->next) {
		if ((err = snd_pcm_lib_preallocate_pages(substream, 
							 SNDRV_DMA_TYPE_DEV, 
							 snd_dma_pci_data(emu->pci), 
							 64*1024, 64*1024)) < 0) /* FIXME: 32*1024 for sound buffer, between 32and64 for Periods table. */
			return err;
	}

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; 
	      substream; 
	      substream = substream->next) {
 		if ((err = snd_pcm_lib_preallocate_pages(substream, 
	                                           SNDRV_DMA_TYPE_DEV, 
	                                           snd_dma_pci_data(emu->pci), 
	                                           64*1024, 64*1024)) < 0)
			return err;
	}
  
	if (rpcm)
		*rpcm = pcm;
  
	return 0;
}

static int __devinit snd_ca0106_create(snd_card_t *card,
					 struct pci_dev *pci,
					 ca0106_t **rchip)
{
	ca0106_t *chip;
	ca0106_details_t *c;
	int err;
	int ch;
	static snd_device_ops_t ops = {
		.dev_free = snd_ca0106_dev_free,
	};
  
	*rchip = NULL;
  
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	if (pci_set_dma_mask(pci, DMA_32BIT_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_32BIT_MASK) < 0) {
		printk(KERN_ERR "error to set 32bit mask DMA\n");
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
  
	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 0x20,
					     "snd_ca0106")) == NULL) { 
		snd_ca0106_free(chip);
		printk(KERN_ERR "cannot allocate the port\n");
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_ca0106_interrupt,
			SA_INTERRUPT|SA_SHIRQ, "snd_ca0106",
			(void *)chip)) {
		snd_ca0106_free(chip);
		printk(KERN_ERR "cannot grab irq\n");
		return -EBUSY;
	}
	chip->irq = pci->irq;
  
 	/* This stores the periods table. */ 
	if(snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci), 1024, &chip->buffer) < 0) {
		snd_ca0106_free(chip);
		return -ENOMEM;
	}

	pci_set_master(pci);
	/* read revision & serial */
	pci_read_config_byte(pci, PCI_REVISION_ID, (char *)&chip->revision);
	pci_read_config_dword(pci, PCI_SUBSYSTEM_VENDOR_ID, &chip->serial);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &chip->model);
#if 1
	printk(KERN_INFO "Model %04x Rev %08x Serial %08x\n", chip->model,
	       chip->revision, chip->serial);
#endif
	strcpy(card->driver, "CA0106");
	strcpy(card->shortname, "CA0106");

	for (c=ca0106_chip_details; c->serial; c++) {
		if (c->serial == chip->serial) break;
	}
	chip->details = c;
	sprintf(card->longname, "%s at 0x%lx irq %i",
		c->name, chip->port, chip->irq);

	outl(0, chip->port + INTE);

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
	snd_ca0106_ptr_write(chip, SPCS0, 0,
				chip->spdif_bits[0] =
				SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
				SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
				SPCS_GENERATIONSTATUS | 0x00001200 |
				0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	/* Only SPCS1 has been tested */
	snd_ca0106_ptr_write(chip, SPCS1, 0,
				chip->spdif_bits[1] =
				SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
				SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
				SPCS_GENERATIONSTATUS | 0x00001200 |
				0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_ca0106_ptr_write(chip, SPCS2, 0,
				chip->spdif_bits[2] =
				SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
				SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
				SPCS_GENERATIONSTATUS | 0x00001200 |
				0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);
	snd_ca0106_ptr_write(chip, SPCS3, 0,
				chip->spdif_bits[3] =
				SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
				SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
				SPCS_GENERATIONSTATUS | 0x00001200 |
				0x00000000 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);

        snd_ca0106_ptr_write(chip, PLAYBACK_MUTE, 0, 0x00fc0000);
        snd_ca0106_ptr_write(chip, CAPTURE_MUTE, 0, 0x00fc0000);

        /* Write 0x8000 to AC97_REC_GAIN to mute it. */
        outb(AC97_REC_GAIN, chip->port + AC97ADDRESS);
        outw(0x8000, chip->port + AC97DATA);
#if 0
	snd_ca0106_ptr_write(chip, SPCS0, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x42, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x43, 0, 0x2108006);
	snd_ca0106_ptr_write(chip, 0x44, 0, 0x2108006);
#endif

	//snd_ca0106_ptr_write(chip, SPDIF_SELECT2, 0, 0xf0f003f); /* OSS drivers set this. */
	/* Analog or Digital output */
	snd_ca0106_ptr_write(chip, SPDIF_SELECT1, 0, 0xf);
	snd_ca0106_ptr_write(chip, SPDIF_SELECT2, 0, 0x000f0000); /* 0x0b000000 for digital, 0x000b0000 for analog, from win2000 drivers. Use 0x000f0000 for surround71 */
	chip->spdif_enable = 0; /* Set digital SPDIF output off */
	chip->capture_source = 3; /* Set CAPTURE_SOURCE */
	//snd_ca0106_ptr_write(chip, 0x45, 0, 0); /* Analogue out */
	//snd_ca0106_ptr_write(chip, 0x45, 0, 0xf00); /* Digital out */

	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 0, 0x40c81000); /* goes to 0x40c80000 when doing SPDIF IN/OUT */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 1, 0xffffffff); /* (Mute) CAPTURE feedback into PLAYBACK volume. Only lower 16 bits matter. */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 2, 0x30300000); /* SPDIF IN Volume */
	snd_ca0106_ptr_write(chip, CAPTURE_CONTROL, 3, 0x00700000); /* SPDIF IN Volume, 0x70 = (vol & 0x3f) | 0x40 */
	snd_ca0106_ptr_write(chip, PLAYBACK_ROUTING1, 0, 0x32765410);
	snd_ca0106_ptr_write(chip, PLAYBACK_ROUTING2, 0, 0x76767676);
	snd_ca0106_ptr_write(chip, CAPTURE_ROUTING1, 0, 0x32765410);
	snd_ca0106_ptr_write(chip, CAPTURE_ROUTING2, 0, 0x76767676);
	for(ch = 0; ch < 4; ch++) {
		snd_ca0106_ptr_write(chip, CAPTURE_VOLUME1, ch, 0x30303030); /* Only high 16 bits matter */
		snd_ca0106_ptr_write(chip, CAPTURE_VOLUME2, ch, 0x30303030);
		//snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME1, ch, 0x40404040); /* Mute */
		//snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME2, ch, 0x40404040); /* Mute */
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME1, ch, 0xffffffff); /* Mute */
		snd_ca0106_ptr_write(chip, PLAYBACK_VOLUME2, ch, 0xffffffff); /* Mute */
	}
        snd_ca0106_ptr_write(chip, CAPTURE_SOURCE, 0x0, 0x333300e4); /* Select MIC, Line in, TAD in, AUX in */
	chip->capture_source = 3; /* Set CAPTURE_SOURCE */

        if (chip->details->gpio_type == 1) { /* The SB0410 and SB0413 use GPIO differently. */
		/* FIXME: Still need to find out what the other GPIO bits do. E.g. For digital spdif out. */
		outl(0x0, chip->port+GPIO);
		//outl(0x00f0e000, chip->port+GPIO); /* Analog */
		outl(0x005f5301, chip->port+GPIO); /* Analog */
	} else {
		outl(0x0, chip->port+GPIO);
		outl(0x005f03a3, chip->port+GPIO); /* Analog */
		//outl(0x005f02a2, chip->port+GPIO);   /* SPDIF */
	}
	snd_ca0106_intr_enable(chip, 0x105); /* Win2000 uses 0x1e0 */

	//outl(HCFG_LOCKSOUNDCACHE|HCFG_AUDIOENABLE, chip->port+HCFG);
	//outl(0x00001409, chip->port+HCFG); /* 0x1000 causes AC3 to fails. Maybe it effects 24 bit output. */
	//outl(0x00000009, chip->port+HCFG);
	outl(HCFG_AC97 | HCFG_AUDIOENABLE, chip->port+HCFG); /* AC97 2.0, Enable outputs. */

        if (chip->details->i2c_adc == 1) { /* The SB0410 and SB0413 use I2C to control ADC. */
	        snd_ca0106_i2c_write(chip, ADC_MUX, ADC_MUX_LINEIN); /* Enable Line-in capture. MIC in currently untested. */
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  chip, &ops)) < 0) {
		snd_ca0106_free(chip);
		return err;
	}
	*rchip = chip;
	return 0;
}

static int __devinit snd_ca0106_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	ca0106_t *chip;
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

	if ((err = snd_ca0106_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ca0106_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ca0106_pcm(chip, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ca0106_pcm(chip, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ca0106_pcm(chip, 3, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
        if (chip->details->ac97 == 1) { /* The SB0410 and SB0413 do not have an AC97 chip. */
		if ((err = snd_ca0106_ac97(chip)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_ca0106_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	snd_ca0106_proc_init(chip);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_ca0106_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

// PCI IDs
static struct pci_device_id snd_ca0106_ids[] = {
	{ 0x1102, 0x0007, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* Audigy LS or Live 24bit */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, snd_ca0106_ids);

// pci_driver definition
static struct pci_driver driver = {
	.name = "CA0106",
	.owner = THIS_MODULE,
	.id_table = snd_ca0106_ids,
	.probe = snd_ca0106_probe,
	.remove = __devexit_p(snd_ca0106_remove),
};

// initialization of the module
static int __init alsa_card_ca0106_init(void)
{
	int err;

	if ((err = pci_register_driver(&driver)) > 0)
		return err;

	return 0;
}

// clean up the module
static void __exit alsa_card_ca0106_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ca0106_init)
module_exit(alsa_card_ca0106_exit)
