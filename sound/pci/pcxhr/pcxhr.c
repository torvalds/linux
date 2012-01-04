/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * main file with alsa callbacks
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
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
 */


#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "pcxhr.h"
#include "pcxhr_mixer.h"
#include "pcxhr_hwdep.h"
#include "pcxhr_core.h"
#include "pcxhr_mix22.h"

#define DRIVER_NAME "pcxhr"

MODULE_AUTHOR("Markus Bollinger <bollinger@digigram.com>, "
	      "Marc Titinger <titinger@digigram.com>");
MODULE_DESCRIPTION("Digigram " DRIVER_NAME " " PCXHR_DRIVER_VERSION_STRING);
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram," DRIVER_NAME "}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */
static int mono[SNDRV_CARDS];				/* capture  mono only */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Digigram " DRIVER_NAME " soundcard");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Digigram " DRIVER_NAME " soundcard");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Digigram " DRIVER_NAME " soundcard");
module_param_array(mono, bool, NULL, 0444);
MODULE_PARM_DESC(mono, "Mono capture mode (default is stereo)");

enum {
	PCI_ID_VX882HR,
	PCI_ID_PCX882HR,
	PCI_ID_VX881HR,
	PCI_ID_PCX881HR,
	PCI_ID_VX882E,
	PCI_ID_PCX882E,
	PCI_ID_VX881E,
	PCI_ID_PCX881E,
	PCI_ID_VX1222HR,
	PCI_ID_PCX1222HR,
	PCI_ID_VX1221HR,
	PCI_ID_PCX1221HR,
	PCI_ID_VX1222E,
	PCI_ID_PCX1222E,
	PCI_ID_VX1221E,
	PCI_ID_PCX1221E,
	PCI_ID_VX222HR,
	PCI_ID_VX222E,
	PCI_ID_PCX22HR,
	PCI_ID_PCX22E,
	PCI_ID_VX222HRMIC,
	PCI_ID_VX222E_MIC,
	PCI_ID_PCX924HR,
	PCI_ID_PCX924E,
	PCI_ID_PCX924HRMIC,
	PCI_ID_PCX924E_MIC,
	PCI_ID_LAST
};

static DEFINE_PCI_DEVICE_TABLE(pcxhr_ids) = {
	{ 0x10b5, 0x9656, 0x1369, 0xb001, 0, 0, PCI_ID_VX882HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb101, 0, 0, PCI_ID_PCX882HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb201, 0, 0, PCI_ID_VX881HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb301, 0, 0, PCI_ID_PCX881HR, },
	{ 0x10b5, 0x9056, 0x1369, 0xb021, 0, 0, PCI_ID_VX882E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb121, 0, 0, PCI_ID_PCX882E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb221, 0, 0, PCI_ID_VX881E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb321, 0, 0, PCI_ID_PCX881E, },
	{ 0x10b5, 0x9656, 0x1369, 0xb401, 0, 0, PCI_ID_VX1222HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb501, 0, 0, PCI_ID_PCX1222HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb601, 0, 0, PCI_ID_VX1221HR, },
	{ 0x10b5, 0x9656, 0x1369, 0xb701, 0, 0, PCI_ID_PCX1221HR, },
	{ 0x10b5, 0x9056, 0x1369, 0xb421, 0, 0, PCI_ID_VX1222E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb521, 0, 0, PCI_ID_PCX1222E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb621, 0, 0, PCI_ID_VX1221E, },
	{ 0x10b5, 0x9056, 0x1369, 0xb721, 0, 0, PCI_ID_PCX1221E, },
	{ 0x10b5, 0x9056, 0x1369, 0xba01, 0, 0, PCI_ID_VX222HR, },
	{ 0x10b5, 0x9056, 0x1369, 0xba21, 0, 0, PCI_ID_VX222E, },
	{ 0x10b5, 0x9056, 0x1369, 0xbd01, 0, 0, PCI_ID_PCX22HR, },
	{ 0x10b5, 0x9056, 0x1369, 0xbd21, 0, 0, PCI_ID_PCX22E, },
	{ 0x10b5, 0x9056, 0x1369, 0xbc01, 0, 0, PCI_ID_VX222HRMIC, },
	{ 0x10b5, 0x9056, 0x1369, 0xbc21, 0, 0, PCI_ID_VX222E_MIC, },
	{ 0x10b5, 0x9056, 0x1369, 0xbb01, 0, 0, PCI_ID_PCX924HR, },
	{ 0x10b5, 0x9056, 0x1369, 0xbb21, 0, 0, PCI_ID_PCX924E, },
	{ 0x10b5, 0x9056, 0x1369, 0xbf01, 0, 0, PCI_ID_PCX924HRMIC, },
	{ 0x10b5, 0x9056, 0x1369, 0xbf21, 0, 0, PCI_ID_PCX924E_MIC, },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, pcxhr_ids);

struct board_parameters {
	char* board_name;
	short playback_chips;
	short capture_chips;
	short fw_file_set;
	short firmware_num;
};
static struct board_parameters pcxhr_board_params[] = {
[PCI_ID_VX882HR] =      { "VX882HR",      4, 4, 0, 41 },
[PCI_ID_PCX882HR] =     { "PCX882HR",     4, 4, 0, 41 },
[PCI_ID_VX881HR] =      { "VX881HR",      4, 4, 0, 41 },
[PCI_ID_PCX881HR] =     { "PCX881HR",     4, 4, 0, 41 },
[PCI_ID_VX882E] =       { "VX882e",       4, 4, 1, 41 },
[PCI_ID_PCX882E] =      { "PCX882e",      4, 4, 1, 41 },
[PCI_ID_VX881E] =       { "VX881e",       4, 4, 1, 41 },
[PCI_ID_PCX881E] =      { "PCX881e",      4, 4, 1, 41 },
[PCI_ID_VX1222HR] =     { "VX1222HR",     6, 1, 2, 42 },
[PCI_ID_PCX1222HR] =    { "PCX1222HR",    6, 1, 2, 42 },
[PCI_ID_VX1221HR] =     { "VX1221HR",     6, 1, 2, 42 },
[PCI_ID_PCX1221HR] =    { "PCX1221HR",    6, 1, 2, 42 },
[PCI_ID_VX1222E] =      { "VX1222e",      6, 1, 3, 42 },
[PCI_ID_PCX1222E] =     { "PCX1222e",     6, 1, 3, 42 },
[PCI_ID_VX1221E] =      { "VX1221e",      6, 1, 3, 42 },
[PCI_ID_PCX1221E] =     { "PCX1221e",     6, 1, 3, 42 },
[PCI_ID_VX222HR] =      { "VX222HR",      1, 1, 4, 44 },
[PCI_ID_VX222E] =       { "VX222e",       1, 1, 4, 44 },
[PCI_ID_PCX22HR] =      { "PCX22HR",      1, 0, 4, 44 },
[PCI_ID_PCX22E] =       { "PCX22e",       1, 0, 4, 44 },
[PCI_ID_VX222HRMIC] =   { "VX222HR-Mic",  1, 1, 5, 44 },
[PCI_ID_VX222E_MIC] =   { "VX222e-Mic",   1, 1, 5, 44 },
[PCI_ID_PCX924HR] =     { "PCX924HR",     1, 1, 5, 44 },
[PCI_ID_PCX924E] =      { "PCX924e",      1, 1, 5, 44 },
[PCI_ID_PCX924HRMIC] =  { "PCX924HR-Mic", 1, 1, 5, 44 },
[PCI_ID_PCX924E_MIC] =  { "PCX924e-Mic",  1, 1, 5, 44 },
};

/* boards without hw AES1 and SRC onboard are all using fw_file_set==4 */
/* VX222HR, VX222e, PCX22HR and PCX22e */
#define PCXHR_BOARD_HAS_AES1(x) (x->fw_file_set != 4)
/* some boards do not support 192kHz on digital AES input plugs */
#define PCXHR_BOARD_AESIN_NO_192K(x) ((x->capture_chips == 0) || \
				      (x->fw_file_set == 0)   || \
				      (x->fw_file_set == 2))

static int pcxhr_pll_freq_register(unsigned int freq, unsigned int* pllreg,
				   unsigned int* realfreq)
{
	unsigned int reg;

	if (freq < 6900 || freq > 110000)
		return -EINVAL;
	reg = (28224000 * 2) / freq;
	reg = (reg - 1) / 2;
	if (reg < 0x200)
		*pllreg = reg + 0x800;
	else if (reg < 0x400)
		*pllreg = reg & 0x1ff;
	else if (reg < 0x800) {
		*pllreg = ((reg >> 1) & 0x1ff) + 0x200;
		reg &= ~1;
	} else {
		*pllreg = ((reg >> 2) & 0x1ff) + 0x400;
		reg &= ~3;
	}
	if (realfreq)
		*realfreq = (28224000 / (reg + 1));
	return 0;
}


#define PCXHR_FREQ_REG_MASK		0x1f
#define PCXHR_FREQ_QUARTZ_48000		0x00
#define PCXHR_FREQ_QUARTZ_24000		0x01
#define PCXHR_FREQ_QUARTZ_12000		0x09
#define PCXHR_FREQ_QUARTZ_32000		0x08
#define PCXHR_FREQ_QUARTZ_16000		0x04
#define PCXHR_FREQ_QUARTZ_8000		0x0c
#define PCXHR_FREQ_QUARTZ_44100		0x02
#define PCXHR_FREQ_QUARTZ_22050		0x0a
#define PCXHR_FREQ_QUARTZ_11025		0x06
#define PCXHR_FREQ_PLL			0x05
#define PCXHR_FREQ_QUARTZ_192000	0x10
#define PCXHR_FREQ_QUARTZ_96000		0x18
#define PCXHR_FREQ_QUARTZ_176400	0x14
#define PCXHR_FREQ_QUARTZ_88200		0x1c
#define PCXHR_FREQ_QUARTZ_128000	0x12
#define PCXHR_FREQ_QUARTZ_64000		0x1a

#define PCXHR_FREQ_WORD_CLOCK		0x0f
#define PCXHR_FREQ_SYNC_AES		0x0e
#define PCXHR_FREQ_AES_1		0x07
#define PCXHR_FREQ_AES_2		0x0b
#define PCXHR_FREQ_AES_3		0x03
#define PCXHR_FREQ_AES_4		0x0d

static int pcxhr_get_clock_reg(struct pcxhr_mgr *mgr, unsigned int rate,
			       unsigned int *reg, unsigned int *freq)
{
	unsigned int val, realfreq, pllreg;
	struct pcxhr_rmh rmh;
	int err;

	realfreq = rate;
	switch (mgr->use_clock_type) {
	case PCXHR_CLOCK_TYPE_INTERNAL :	/* clock by quartz or pll */
		switch (rate) {
		case 48000 :	val = PCXHR_FREQ_QUARTZ_48000;	break;
		case 24000 :	val = PCXHR_FREQ_QUARTZ_24000;	break;
		case 12000 :	val = PCXHR_FREQ_QUARTZ_12000;	break;
		case 32000 :	val = PCXHR_FREQ_QUARTZ_32000;	break;
		case 16000 :	val = PCXHR_FREQ_QUARTZ_16000;	break;
		case 8000 :	val = PCXHR_FREQ_QUARTZ_8000;	break;
		case 44100 :	val = PCXHR_FREQ_QUARTZ_44100;	break;
		case 22050 :	val = PCXHR_FREQ_QUARTZ_22050;	break;
		case 11025 :	val = PCXHR_FREQ_QUARTZ_11025;	break;
		case 192000 :	val = PCXHR_FREQ_QUARTZ_192000;	break;
		case 96000 :	val = PCXHR_FREQ_QUARTZ_96000;	break;
		case 176400 :	val = PCXHR_FREQ_QUARTZ_176400;	break;
		case 88200 :	val = PCXHR_FREQ_QUARTZ_88200;	break;
		case 128000 :	val = PCXHR_FREQ_QUARTZ_128000;	break;
		case 64000 :	val = PCXHR_FREQ_QUARTZ_64000;	break;
		default :
			val = PCXHR_FREQ_PLL;
			/* get the value for the pll register */
			err = pcxhr_pll_freq_register(rate, &pllreg, &realfreq);
			if (err)
				return err;
			pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_WRITE);
			rmh.cmd[0] |= IO_NUM_REG_GENCLK;
			rmh.cmd[1]  = pllreg & MASK_DSP_WORD;
			rmh.cmd[2]  = pllreg >> 24;
			rmh.cmd_len = 3;
			err = pcxhr_send_msg(mgr, &rmh);
			if (err < 0) {
				snd_printk(KERN_ERR
					   "error CMD_ACCESS_IO_WRITE "
					   "for PLL register : %x!\n", err);
				return err;
			}
		}
		break;
	case PCXHR_CLOCK_TYPE_WORD_CLOCK:
		val = PCXHR_FREQ_WORD_CLOCK;
		break;
	case PCXHR_CLOCK_TYPE_AES_SYNC:
		val = PCXHR_FREQ_SYNC_AES;
		break;
	case PCXHR_CLOCK_TYPE_AES_1:
		val = PCXHR_FREQ_AES_1;
		break;
	case PCXHR_CLOCK_TYPE_AES_2:
		val = PCXHR_FREQ_AES_2;
		break;
	case PCXHR_CLOCK_TYPE_AES_3:
		val = PCXHR_FREQ_AES_3;
		break;
	case PCXHR_CLOCK_TYPE_AES_4:
		val = PCXHR_FREQ_AES_4;
		break;
	default:
		return -EINVAL;
	}
	*reg = val;
	*freq = realfreq;
	return 0;
}


static int pcxhr_sub_set_clock(struct pcxhr_mgr *mgr,
			       unsigned int rate,
			       int *changed)
{
	unsigned int val, realfreq, speed;
	struct pcxhr_rmh rmh;
	int err;

	err = pcxhr_get_clock_reg(mgr, rate, &val, &realfreq);
	if (err)
		return err;

	/* codec speed modes */
	if (rate < 55000)
		speed = 0;	/* single speed */
	else if (rate < 100000)
		speed = 1;	/* dual speed */
	else
		speed = 2;	/* quad speed */
	if (mgr->codec_speed != speed) {
		pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_WRITE); /* mute outputs */
		rmh.cmd[0] |= IO_NUM_REG_MUTE_OUT;
		if (DSP_EXT_CMD_SET(mgr)) {
			rmh.cmd[1]  = 1;
			rmh.cmd_len = 2;
		}
		err = pcxhr_send_msg(mgr, &rmh);
		if (err)
			return err;

		pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_WRITE); /* set speed ratio */
		rmh.cmd[0] |= IO_NUM_SPEED_RATIO;
		rmh.cmd[1] = speed;
		rmh.cmd_len = 2;
		err = pcxhr_send_msg(mgr, &rmh);
		if (err)
			return err;
	}
	/* set the new frequency */
	snd_printdd("clock register : set %x\n", val);
	err = pcxhr_write_io_num_reg_cont(mgr, PCXHR_FREQ_REG_MASK,
					  val, changed);
	if (err)
		return err;

	mgr->sample_rate_real = realfreq;
	mgr->cur_clock_type = mgr->use_clock_type;

	/* unmute after codec speed modes */
	if (mgr->codec_speed != speed) {
		pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_READ); /* unmute outputs */
		rmh.cmd[0] |= IO_NUM_REG_MUTE_OUT;
		if (DSP_EXT_CMD_SET(mgr)) {
			rmh.cmd[1]  = 1;
			rmh.cmd_len = 2;
		}
		err = pcxhr_send_msg(mgr, &rmh);
		if (err)
			return err;
		mgr->codec_speed = speed;	/* save new codec speed */
	}

	snd_printdd("pcxhr_sub_set_clock to %dHz (realfreq=%d)\n",
		    rate, realfreq);
	return 0;
}

#define PCXHR_MODIFY_CLOCK_S_BIT	0x04

#define PCXHR_IRQ_TIMER_FREQ		92000
#define PCXHR_IRQ_TIMER_PERIOD		48

int pcxhr_set_clock(struct pcxhr_mgr *mgr, unsigned int rate)
{
	struct pcxhr_rmh rmh;
	int err, changed;

	if (rate == 0)
		return 0; /* nothing to do */

	if (mgr->is_hr_stereo)
		err = hr222_sub_set_clock(mgr, rate, &changed);
	else
		err = pcxhr_sub_set_clock(mgr, rate, &changed);

	if (err)
		return err;

	if (changed) {
		pcxhr_init_rmh(&rmh, CMD_MODIFY_CLOCK);
		rmh.cmd[0] |= PCXHR_MODIFY_CLOCK_S_BIT; /* resync fifos  */
		if (rate < PCXHR_IRQ_TIMER_FREQ)
			rmh.cmd[1] = PCXHR_IRQ_TIMER_PERIOD;
		else
			rmh.cmd[1] = PCXHR_IRQ_TIMER_PERIOD * 2;
		rmh.cmd[2] = rate;
		rmh.cmd_len = 3;
		err = pcxhr_send_msg(mgr, &rmh);
		if (err)
			return err;
	}
	return 0;
}


static int pcxhr_sub_get_external_clock(struct pcxhr_mgr *mgr,
					enum pcxhr_clock_type clock_type,
					int *sample_rate)
{
	struct pcxhr_rmh rmh;
	unsigned char reg;
	int err, rate;

	switch (clock_type) {
	case PCXHR_CLOCK_TYPE_WORD_CLOCK:
		reg = REG_STATUS_WORD_CLOCK;
		break;
	case PCXHR_CLOCK_TYPE_AES_SYNC:
		reg = REG_STATUS_AES_SYNC;
		break;
	case PCXHR_CLOCK_TYPE_AES_1:
		reg = REG_STATUS_AES_1;
		break;
	case PCXHR_CLOCK_TYPE_AES_2:
		reg = REG_STATUS_AES_2;
		break;
	case PCXHR_CLOCK_TYPE_AES_3:
		reg = REG_STATUS_AES_3;
		break;
	case PCXHR_CLOCK_TYPE_AES_4:
		reg = REG_STATUS_AES_4;
		break;
	default:
		return -EINVAL;
	}
	pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_READ);
	rmh.cmd_len = 2;
	rmh.cmd[0] |= IO_NUM_REG_STATUS;
	if (mgr->last_reg_stat != reg) {
		rmh.cmd[1]  = reg;
		err = pcxhr_send_msg(mgr, &rmh);
		if (err)
			return err;
		udelay(100);	/* wait minimum 2 sample_frames at 32kHz ! */
		mgr->last_reg_stat = reg;
	}
	rmh.cmd[1]  = REG_STATUS_CURRENT;
	err = pcxhr_send_msg(mgr, &rmh);
	if (err)
		return err;
	switch (rmh.stat[1] & 0x0f) {
	case REG_STATUS_SYNC_32000 :	rate = 32000; break;
	case REG_STATUS_SYNC_44100 :	rate = 44100; break;
	case REG_STATUS_SYNC_48000 :	rate = 48000; break;
	case REG_STATUS_SYNC_64000 :	rate = 64000; break;
	case REG_STATUS_SYNC_88200 :	rate = 88200; break;
	case REG_STATUS_SYNC_96000 :	rate = 96000; break;
	case REG_STATUS_SYNC_128000 :	rate = 128000; break;
	case REG_STATUS_SYNC_176400 :	rate = 176400; break;
	case REG_STATUS_SYNC_192000 :	rate = 192000; break;
	default: rate = 0;
	}
	snd_printdd("External clock is at %d Hz\n", rate);
	*sample_rate = rate;
	return 0;
}


int pcxhr_get_external_clock(struct pcxhr_mgr *mgr,
			     enum pcxhr_clock_type clock_type,
			     int *sample_rate)
{
	if (mgr->is_hr_stereo)
		return hr222_get_external_clock(mgr, clock_type,
						sample_rate);
	else
		return pcxhr_sub_get_external_clock(mgr, clock_type,
						    sample_rate);
}

/*
 *  start or stop playback/capture substream
 */
static int pcxhr_set_stream_state(struct pcxhr_stream *stream)
{
	int err;
	struct snd_pcxhr *chip;
	struct pcxhr_rmh rmh;
	int stream_mask, start;

	if (stream->status == PCXHR_STREAM_STATUS_SCHEDULE_RUN)
		start = 1;
	else {
		if (stream->status != PCXHR_STREAM_STATUS_SCHEDULE_STOP) {
			snd_printk(KERN_ERR "ERROR pcxhr_set_stream_state "
				   "CANNOT be stopped\n");
			return -EINVAL;
		}
		start = 0;
	}
	if (!stream->substream)
		return -EINVAL;

	stream->timer_abs_periods = 0;
	stream->timer_period_frag = 0;	/* reset theoretical stream pos */
	stream->timer_buf_periods = 0;
	stream->timer_is_synced = 0;

	stream_mask =
	  stream->pipe->is_capture ? 1 : 1<<stream->substream->number;

	pcxhr_init_rmh(&rmh, start ? CMD_START_STREAM : CMD_STOP_STREAM);
	pcxhr_set_pipe_cmd_params(&rmh, stream->pipe->is_capture,
				  stream->pipe->first_audio, 0, stream_mask);

	chip = snd_pcm_substream_chip(stream->substream);

	err = pcxhr_send_msg(chip->mgr, &rmh);
	if (err)
		snd_printk(KERN_ERR "ERROR pcxhr_set_stream_state err=%x;\n",
			   err);
	stream->status =
	  start ? PCXHR_STREAM_STATUS_STARTED : PCXHR_STREAM_STATUS_STOPPED;
	return err;
}

#define HEADER_FMT_BASE_LIN		0xfed00000
#define HEADER_FMT_BASE_FLOAT		0xfad00000
#define HEADER_FMT_INTEL		0x00008000
#define HEADER_FMT_24BITS		0x00004000
#define HEADER_FMT_16BITS		0x00002000
#define HEADER_FMT_UPTO11		0x00000200
#define HEADER_FMT_UPTO32		0x00000100
#define HEADER_FMT_MONO			0x00000080

static int pcxhr_set_format(struct pcxhr_stream *stream)
{
	int err, is_capture, sample_rate, stream_num;
	struct snd_pcxhr *chip;
	struct pcxhr_rmh rmh;
	unsigned int header;

	switch (stream->format) {
	case SNDRV_PCM_FORMAT_U8:
		header = HEADER_FMT_BASE_LIN;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		header = HEADER_FMT_BASE_LIN |
			 HEADER_FMT_16BITS | HEADER_FMT_INTEL;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		header = HEADER_FMT_BASE_LIN | HEADER_FMT_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		header = HEADER_FMT_BASE_LIN |
			 HEADER_FMT_24BITS | HEADER_FMT_INTEL;
		break;
	case SNDRV_PCM_FORMAT_S24_3BE:
		header = HEADER_FMT_BASE_LIN | HEADER_FMT_24BITS;
		break;
	case SNDRV_PCM_FORMAT_FLOAT_LE:
		header = HEADER_FMT_BASE_FLOAT | HEADER_FMT_INTEL;
		break;
	default:
		snd_printk(KERN_ERR
			   "error pcxhr_set_format() : unknown format\n");
		return -EINVAL;
	}
	chip = snd_pcm_substream_chip(stream->substream);

	sample_rate = chip->mgr->sample_rate;
	if (sample_rate <= 32000 && sample_rate !=0) {
		if (sample_rate <= 11025)
			header |= HEADER_FMT_UPTO11;
		else
			header |= HEADER_FMT_UPTO32;
	}
	if (stream->channels == 1)
		header |= HEADER_FMT_MONO;

	is_capture = stream->pipe->is_capture;
	stream_num = is_capture ? 0 : stream->substream->number;

	pcxhr_init_rmh(&rmh, is_capture ?
		       CMD_FORMAT_STREAM_IN : CMD_FORMAT_STREAM_OUT);
	pcxhr_set_pipe_cmd_params(&rmh, is_capture, stream->pipe->first_audio,
				  stream_num, 0);
	if (is_capture) {
		/* bug with old dsp versions: */
		/* bit 12 also sets the format of the playback stream */
		if (DSP_EXT_CMD_SET(chip->mgr))
			rmh.cmd[0] |= 1<<10;
		else
			rmh.cmd[0] |= 1<<12;
	}
	rmh.cmd[1] = 0;
	rmh.cmd_len = 2;
	if (DSP_EXT_CMD_SET(chip->mgr)) {
		/* add channels and set bit 19 if channels>2 */
		rmh.cmd[1] = stream->channels;
		if (!is_capture) {
			/* playback : add channel mask to command */
			rmh.cmd[2] = (stream->channels == 1) ? 0x01 : 0x03;
			rmh.cmd_len = 3;
		}
	}
	rmh.cmd[rmh.cmd_len++] = header >> 8;
	rmh.cmd[rmh.cmd_len++] = (header & 0xff) << 16;
	err = pcxhr_send_msg(chip->mgr, &rmh);
	if (err)
		snd_printk(KERN_ERR "ERROR pcxhr_set_format err=%x;\n", err);
	return err;
}

static int pcxhr_update_r_buffer(struct pcxhr_stream *stream)
{
	int err, is_capture, stream_num;
	struct pcxhr_rmh rmh;
	struct snd_pcm_substream *subs = stream->substream;
	struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);

	is_capture = (subs->stream == SNDRV_PCM_STREAM_CAPTURE);
	stream_num = is_capture ? 0 : subs->number;

	snd_printdd("pcxhr_update_r_buffer(pcm%c%d) : "
		    "addr(%p) bytes(%zx) subs(%d)\n",
		    is_capture ? 'c' : 'p',
		    chip->chip_idx, (void *)(long)subs->runtime->dma_addr,
		    subs->runtime->dma_bytes, subs->number);

	pcxhr_init_rmh(&rmh, CMD_UPDATE_R_BUFFERS);
	pcxhr_set_pipe_cmd_params(&rmh, is_capture, stream->pipe->first_audio,
				  stream_num, 0);

	/* max buffer size is 2 MByte */
	snd_BUG_ON(subs->runtime->dma_bytes >= 0x200000);
	/* size in bits */
	rmh.cmd[1] = subs->runtime->dma_bytes * 8;
	/* most significant byte */
	rmh.cmd[2] = subs->runtime->dma_addr >> 24;
	/* this is a circular buffer */
	rmh.cmd[2] |= 1<<19;
	/* least 3 significant bytes */
	rmh.cmd[3] = subs->runtime->dma_addr & MASK_DSP_WORD;
	rmh.cmd_len = 4;
	err = pcxhr_send_msg(chip->mgr, &rmh);
	if (err)
		snd_printk(KERN_ERR
			   "ERROR CMD_UPDATE_R_BUFFERS err=%x;\n", err);
	return err;
}


#if 0
static int pcxhr_pipe_sample_count(struct pcxhr_stream *stream,
				   snd_pcm_uframes_t *sample_count)
{
	struct pcxhr_rmh rmh;
	int err;
	pcxhr_t *chip = snd_pcm_substream_chip(stream->substream);
	pcxhr_init_rmh(&rmh, CMD_PIPE_SAMPLE_COUNT);
	pcxhr_set_pipe_cmd_params(&rmh, stream->pipe->is_capture, 0, 0,
				  1<<stream->pipe->first_audio);
	err = pcxhr_send_msg(chip->mgr, &rmh);
	if (err == 0) {
		*sample_count = ((snd_pcm_uframes_t)rmh.stat[0]) << 24;
		*sample_count += (snd_pcm_uframes_t)rmh.stat[1];
	}
	snd_printdd("PIPE_SAMPLE_COUNT = %lx\n", *sample_count);
	return err;
}
#endif

static inline int pcxhr_stream_scheduled_get_pipe(struct pcxhr_stream *stream,
						  struct pcxhr_pipe **pipe)
{
	if (stream->status == PCXHR_STREAM_STATUS_SCHEDULE_RUN) {
		*pipe = stream->pipe;
		return 1;
	}
	return 0;
}

static void pcxhr_trigger_tasklet(unsigned long arg)
{
	unsigned long flags;
	int i, j, err;
	struct pcxhr_pipe *pipe;
	struct snd_pcxhr *chip;
	struct pcxhr_mgr *mgr = (struct pcxhr_mgr*)(arg);
	int capture_mask = 0;
	int playback_mask = 0;

#ifdef CONFIG_SND_DEBUG_VERBOSE
	struct timeval my_tv1, my_tv2;
	do_gettimeofday(&my_tv1);
#endif
	mutex_lock(&mgr->setup_mutex);

	/* check the pipes concerned and build pipe_array */
	for (i = 0; i < mgr->num_cards; i++) {
		chip = mgr->chip[i];
		for (j = 0; j < chip->nb_streams_capt; j++) {
			if (pcxhr_stream_scheduled_get_pipe(&chip->capture_stream[j], &pipe))
				capture_mask |= (1 << pipe->first_audio);
		}
		for (j = 0; j < chip->nb_streams_play; j++) {
			if (pcxhr_stream_scheduled_get_pipe(&chip->playback_stream[j], &pipe)) {
				playback_mask |= (1 << pipe->first_audio);
				break;	/* add only once, as all playback
					 * streams of one chip use the same pipe
					 */
			}
		}
	}
	if (capture_mask == 0 && playback_mask == 0) {
		mutex_unlock(&mgr->setup_mutex);
		snd_printk(KERN_ERR "pcxhr_trigger_tasklet : no pipes\n");
		return;
	}

	snd_printdd("pcxhr_trigger_tasklet : "
		    "playback_mask=%x capture_mask=%x\n",
		    playback_mask, capture_mask);

	/* synchronous stop of all the pipes concerned */
	err = pcxhr_set_pipe_state(mgr,  playback_mask, capture_mask, 0);
	if (err) {
		mutex_unlock(&mgr->setup_mutex);
		snd_printk(KERN_ERR "pcxhr_trigger_tasklet : "
			   "error stop pipes (P%x C%x)\n",
			   playback_mask, capture_mask);
		return;
	}

	/* the dsp lost format and buffer info with the stop pipe */
	for (i = 0; i < mgr->num_cards; i++) {
		struct pcxhr_stream *stream;
		chip = mgr->chip[i];
		for (j = 0; j < chip->nb_streams_capt; j++) {
			stream = &chip->capture_stream[j];
			if (pcxhr_stream_scheduled_get_pipe(stream, &pipe)) {
				err = pcxhr_set_format(stream);
				err = pcxhr_update_r_buffer(stream);
			}
		}
		for (j = 0; j < chip->nb_streams_play; j++) {
			stream = &chip->playback_stream[j];
			if (pcxhr_stream_scheduled_get_pipe(stream, &pipe)) {
				err = pcxhr_set_format(stream);
				err = pcxhr_update_r_buffer(stream);
			}
		}
	}
	/* start all the streams */
	for (i = 0; i < mgr->num_cards; i++) {
		struct pcxhr_stream *stream;
		chip = mgr->chip[i];
		for (j = 0; j < chip->nb_streams_capt; j++) {
			stream = &chip->capture_stream[j];
			if (pcxhr_stream_scheduled_get_pipe(stream, &pipe))
				err = pcxhr_set_stream_state(stream);
		}
		for (j = 0; j < chip->nb_streams_play; j++) {
			stream = &chip->playback_stream[j];
			if (pcxhr_stream_scheduled_get_pipe(stream, &pipe))
				err = pcxhr_set_stream_state(stream);
		}
	}

	/* synchronous start of all the pipes concerned */
	err = pcxhr_set_pipe_state(mgr, playback_mask, capture_mask, 1);
	if (err) {
		mutex_unlock(&mgr->setup_mutex);
		snd_printk(KERN_ERR "pcxhr_trigger_tasklet : "
			   "error start pipes (P%x C%x)\n",
			   playback_mask, capture_mask);
		return;
	}

	/* put the streams into the running state now
	 * (increment pointer by interrupt)
	 */
	spin_lock_irqsave(&mgr->lock, flags);
	for ( i =0; i < mgr->num_cards; i++) {
		struct pcxhr_stream *stream;
		chip = mgr->chip[i];
		for(j = 0; j < chip->nb_streams_capt; j++) {
			stream = &chip->capture_stream[j];
			if(stream->status == PCXHR_STREAM_STATUS_STARTED)
				stream->status = PCXHR_STREAM_STATUS_RUNNING;
		}
		for (j = 0; j < chip->nb_streams_play; j++) {
			stream = &chip->playback_stream[j];
			if (stream->status == PCXHR_STREAM_STATUS_STARTED) {
				/* playback will already have advanced ! */
				stream->timer_period_frag += mgr->granularity;
				stream->status = PCXHR_STREAM_STATUS_RUNNING;
			}
		}
	}
	spin_unlock_irqrestore(&mgr->lock, flags);

	mutex_unlock(&mgr->setup_mutex);

#ifdef CONFIG_SND_DEBUG_VERBOSE
	do_gettimeofday(&my_tv2);
	snd_printdd("***TRIGGER TASKLET*** TIME = %ld (err = %x)\n",
		    (long)(my_tv2.tv_usec - my_tv1.tv_usec), err);
#endif
}


/*
 *  trigger callback
 */
static int pcxhr_trigger(struct snd_pcm_substream *subs, int cmd)
{
	struct pcxhr_stream *stream;
	struct snd_pcm_substream *s;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_printdd("SNDRV_PCM_TRIGGER_START\n");
		if (snd_pcm_stream_linked(subs)) {
			struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);
			snd_pcm_group_for_each_entry(s, subs) {
				if (snd_pcm_substream_chip(s) != chip)
					continue;
				stream = s->runtime->private_data;
				stream->status =
					PCXHR_STREAM_STATUS_SCHEDULE_RUN;
				snd_pcm_trigger_done(s, subs);
			}
			tasklet_schedule(&chip->mgr->trigger_taskq);
		} else {
			stream = subs->runtime->private_data;
			snd_printdd("Only one Substream %c %d\n",
				    stream->pipe->is_capture ? 'C' : 'P',
				    stream->pipe->first_audio);
			if (pcxhr_set_format(stream))
				return -EINVAL;
			if (pcxhr_update_r_buffer(stream))
				return -EINVAL;

			stream->status = PCXHR_STREAM_STATUS_SCHEDULE_RUN;
			if (pcxhr_set_stream_state(stream))
				return -EINVAL;
			stream->status = PCXHR_STREAM_STATUS_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_printdd("SNDRV_PCM_TRIGGER_STOP\n");
		snd_pcm_group_for_each_entry(s, subs) {
			stream = s->runtime->private_data;
			stream->status = PCXHR_STREAM_STATUS_SCHEDULE_STOP;
			if (pcxhr_set_stream_state(stream))
				return -EINVAL;
			snd_pcm_trigger_done(s, subs);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* TODO */
	default:
		return -EINVAL;
	}
	return 0;
}


static int pcxhr_hardware_timer(struct pcxhr_mgr *mgr, int start)
{
	struct pcxhr_rmh rmh;
	int err;

	pcxhr_init_rmh(&rmh, CMD_SET_TIMER_INTERRUPT);
	if (start) {
		/* last dsp time invalid */
		mgr->dsp_time_last = PCXHR_DSP_TIME_INVALID;
		rmh.cmd[0] |= mgr->granularity;
	}
	err = pcxhr_send_msg(mgr, &rmh);
	if (err < 0)
		snd_printk(KERN_ERR "error pcxhr_hardware_timer err(%x)\n",
			   err);
	return err;
}

/*
 *  prepare callback for all pcms
 */
static int pcxhr_prepare(struct snd_pcm_substream *subs)
{
	struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);
	struct pcxhr_mgr *mgr = chip->mgr;
	int err = 0;

	snd_printdd("pcxhr_prepare : period_size(%lx) periods(%x) buffer_size(%lx)\n",
		    subs->runtime->period_size, subs->runtime->periods,
		    subs->runtime->buffer_size);

	mutex_lock(&mgr->setup_mutex);

	do {
		/* only the first stream can choose the sample rate */
		/* set the clock only once (first stream) */
		if (mgr->sample_rate != subs->runtime->rate) {
			err = pcxhr_set_clock(mgr, subs->runtime->rate);
			if (err)
				break;
			if (mgr->sample_rate == 0)
				/* start the DSP-timer */
				err = pcxhr_hardware_timer(mgr, 1);
			mgr->sample_rate = subs->runtime->rate;
		}
	} while(0);	/* do only once (so we can use break instead of goto) */

	mutex_unlock(&mgr->setup_mutex);

	return err;
}


/*
 *  HW_PARAMS callback for all pcms
 */
static int pcxhr_hw_params(struct snd_pcm_substream *subs,
			   struct snd_pcm_hw_params *hw)
{
	struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);
	struct pcxhr_mgr *mgr = chip->mgr;
	struct pcxhr_stream *stream = subs->runtime->private_data;
	snd_pcm_format_t format;
	int err;
	int channels;

	/* set up channels */
	channels = params_channels(hw);

	/*  set up format for the stream */
	format = params_format(hw);

	mutex_lock(&mgr->setup_mutex);

	stream->channels = channels;
	stream->format = format;

	/* allocate buffer */
	err = snd_pcm_lib_malloc_pages(subs, params_buffer_bytes(hw));

	mutex_unlock(&mgr->setup_mutex);

	return err;
}

static int pcxhr_hw_free(struct snd_pcm_substream *subs)
{
	snd_pcm_lib_free_pages(subs);
	return 0;
}


/*
 *  CONFIGURATION SPACE for all pcms, mono pcm must update channels_max
 */
static struct snd_pcm_hardware pcxhr_caps =
{
	.info             = (SNDRV_PCM_INFO_MMAP |
			     SNDRV_PCM_INFO_INTERLEAVED |
			     SNDRV_PCM_INFO_MMAP_VALID |
			     SNDRV_PCM_INFO_SYNC_START),
	.formats	  = (SNDRV_PCM_FMTBIT_U8 |
			     SNDRV_PCM_FMTBIT_S16_LE |
			     SNDRV_PCM_FMTBIT_S16_BE |
			     SNDRV_PCM_FMTBIT_S24_3LE |
			     SNDRV_PCM_FMTBIT_S24_3BE |
			     SNDRV_PCM_FMTBIT_FLOAT_LE),
	.rates            = (SNDRV_PCM_RATE_CONTINUOUS |
			     SNDRV_PCM_RATE_8000_192000),
	.rate_min         = 8000,
	.rate_max         = 192000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max = (32*1024),
	/* 1 byte == 1 frame U8 mono (PCXHR_GRANULARITY is frames!) */
	.period_bytes_min = (2*PCXHR_GRANULARITY),
	.period_bytes_max = (16*1024),
	.periods_min      = 2,
	.periods_max      = (32*1024/PCXHR_GRANULARITY),
};


static int pcxhr_open(struct snd_pcm_substream *subs)
{
	struct snd_pcxhr       *chip = snd_pcm_substream_chip(subs);
	struct pcxhr_mgr       *mgr = chip->mgr;
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct pcxhr_stream    *stream;
	int err;

	mutex_lock(&mgr->setup_mutex);

	/* copy the struct snd_pcm_hardware struct */
	runtime->hw = pcxhr_caps;

	if( subs->stream == SNDRV_PCM_STREAM_PLAYBACK ) {
		snd_printdd("pcxhr_open playback chip%d subs%d\n",
			    chip->chip_idx, subs->number);
		stream = &chip->playback_stream[subs->number];
	} else {
		snd_printdd("pcxhr_open capture chip%d subs%d\n",
			    chip->chip_idx, subs->number);
		if (mgr->mono_capture)
			runtime->hw.channels_max = 1;
		else
			runtime->hw.channels_min = 2;
		stream = &chip->capture_stream[subs->number];
	}
	if (stream->status != PCXHR_STREAM_STATUS_FREE){
		/* streams in use */
		snd_printk(KERN_ERR "pcxhr_open chip%d subs%d in use\n",
			   chip->chip_idx, subs->number);
		mutex_unlock(&mgr->setup_mutex);
		return -EBUSY;
	}

	/* float format support is in some cases buggy on stereo cards */
	if (mgr->is_hr_stereo)
		runtime->hw.formats &= ~SNDRV_PCM_FMTBIT_FLOAT_LE;

	/* buffer-size should better be multiple of period-size */
	err = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0) {
		mutex_unlock(&mgr->setup_mutex);
		return err;
	}

	/* if a sample rate is already used or fixed by external clock,
	 * the stream cannot change
	 */
	if (mgr->sample_rate)
		runtime->hw.rate_min = runtime->hw.rate_max = mgr->sample_rate;
	else {
		if (mgr->use_clock_type != PCXHR_CLOCK_TYPE_INTERNAL) {
			int external_rate;
			if (pcxhr_get_external_clock(mgr, mgr->use_clock_type,
						     &external_rate) ||
			    external_rate == 0) {
				/* cannot detect the external clock rate */
				mutex_unlock(&mgr->setup_mutex);
				return -EBUSY;
			}
			runtime->hw.rate_min = external_rate;
			runtime->hw.rate_max = external_rate;
		}
	}

	stream->status      = PCXHR_STREAM_STATUS_OPEN;
	stream->substream   = subs;
	stream->channels    = 0; /* not configured yet */

	runtime->private_data = stream;

	/* better get a divisor of granularity values (96 or 192) */
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 32);
	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 32);
	snd_pcm_set_sync(subs);

	mgr->ref_count_rate++;

	mutex_unlock(&mgr->setup_mutex);
	return 0;
}


static int pcxhr_close(struct snd_pcm_substream *subs)
{
	struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);
	struct pcxhr_mgr *mgr = chip->mgr;
	struct pcxhr_stream *stream = subs->runtime->private_data;

	mutex_lock(&mgr->setup_mutex);

	snd_printdd("pcxhr_close chip%d subs%d\n",
		    chip->chip_idx, subs->number);

	/* sample rate released */
	if (--mgr->ref_count_rate == 0) {
		mgr->sample_rate = 0;	/* the sample rate is no more locked */
		pcxhr_hardware_timer(mgr, 0);	/* stop the DSP-timer */
	}

	stream->status    = PCXHR_STREAM_STATUS_FREE;
	stream->substream = NULL;

	mutex_unlock(&mgr->setup_mutex);

	return 0;
}


static snd_pcm_uframes_t pcxhr_stream_pointer(struct snd_pcm_substream *subs)
{
	unsigned long flags;
	u_int32_t timer_period_frag;
	int timer_buf_periods;
	struct snd_pcxhr *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct pcxhr_stream *stream  = runtime->private_data;

	spin_lock_irqsave(&chip->mgr->lock, flags);

	/* get the period fragment and the nb of periods in the buffer */
	timer_period_frag = stream->timer_period_frag;
	timer_buf_periods = stream->timer_buf_periods;

	spin_unlock_irqrestore(&chip->mgr->lock, flags);

	return (snd_pcm_uframes_t)((timer_buf_periods * runtime->period_size) +
				   timer_period_frag);
}


static struct snd_pcm_ops pcxhr_ops = {
	.open      = pcxhr_open,
	.close     = pcxhr_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.prepare   = pcxhr_prepare,
	.hw_params = pcxhr_hw_params,
	.hw_free   = pcxhr_hw_free,
	.trigger   = pcxhr_trigger,
	.pointer   = pcxhr_stream_pointer,
};

/*
 */
int pcxhr_create_pcm(struct snd_pcxhr *chip)
{
	int err;
	struct snd_pcm *pcm;
	char name[32];

	sprintf(name, "pcxhr %d", chip->chip_idx);
	if ((err = snd_pcm_new(chip->card, name, 0,
			       chip->nb_streams_play,
			       chip->nb_streams_capt, &pcm)) < 0) {
		snd_printk(KERN_ERR "cannot create pcm %s\n", name);
		return err;
	}
	pcm->private_data = chip;

	if (chip->nb_streams_play)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcxhr_ops);
	if (chip->nb_streams_capt)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcxhr_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, name);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->mgr->pci),
					      32*1024, 32*1024);
	chip->pcm = pcm;
	return 0;
}

static int pcxhr_chip_free(struct snd_pcxhr *chip)
{
	kfree(chip);
	return 0;
}

static int pcxhr_chip_dev_free(struct snd_device *device)
{
	struct snd_pcxhr *chip = device->device_data;
	return pcxhr_chip_free(chip);
}


/*
 */
static int __devinit pcxhr_create(struct pcxhr_mgr *mgr,
				  struct snd_card *card, int idx)
{
	int err;
	struct snd_pcxhr *chip;
	static struct snd_device_ops ops = {
		.dev_free = pcxhr_chip_dev_free,
	};

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (! chip) {
		snd_printk(KERN_ERR "cannot allocate chip\n");
		return -ENOMEM;
	}

	chip->card = card;
	chip->chip_idx = idx;
	chip->mgr = mgr;

	if (idx < mgr->playback_chips)
		/* stereo or mono streams */
		chip->nb_streams_play = PCXHR_PLAYBACK_STREAMS;

	if (idx < mgr->capture_chips) {
		if (mgr->mono_capture)
			chip->nb_streams_capt = 2;	/* 2 mono streams */
		else
			chip->nb_streams_capt = 1;	/* or 1 stereo stream */
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		pcxhr_chip_free(chip);
		return err;
	}

	mgr->chip[idx] = chip;
	snd_card_set_dev(card, &mgr->pci->dev);

	return 0;
}

/* proc interface */
static void pcxhr_proc_info(struct snd_info_entry *entry,
			    struct snd_info_buffer *buffer)
{
	struct snd_pcxhr *chip = entry->private_data;
	struct pcxhr_mgr *mgr = chip->mgr;

	snd_iprintf(buffer, "\n%s\n", mgr->longname);

	/* stats available when embedded DSP is running */
	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_MAIN_INDEX)) {
		struct pcxhr_rmh rmh;
		short ver_maj = (mgr->dsp_version >> 16) & 0xff;
		short ver_min = (mgr->dsp_version >> 8) & 0xff;
		short ver_build = mgr->dsp_version & 0xff;
		snd_iprintf(buffer, "module version %s\n",
			    PCXHR_DRIVER_VERSION_STRING);
		snd_iprintf(buffer, "dsp version %d.%d.%d\n",
			    ver_maj, ver_min, ver_build);
		if (mgr->board_has_analog)
			snd_iprintf(buffer, "analog io available\n");
		else
			snd_iprintf(buffer, "digital only board\n");

		/* calc cpu load of the dsp */
		pcxhr_init_rmh(&rmh, CMD_GET_DSP_RESOURCES);
		if( ! pcxhr_send_msg(mgr, &rmh) ) {
			int cur = rmh.stat[0];
			int ref = rmh.stat[1];
			if (ref > 0) {
				if (mgr->sample_rate_real != 0 &&
				    mgr->sample_rate_real != 48000) {
					ref = (ref * 48000) /
					  mgr->sample_rate_real;
					if (mgr->sample_rate_real >=
					    PCXHR_IRQ_TIMER_FREQ)
						ref *= 2;
				}
				cur = 100 - (100 * cur) / ref;
				snd_iprintf(buffer, "cpu load    %d%%\n", cur);
				snd_iprintf(buffer, "buffer pool %d/%d\n",
					    rmh.stat[2], rmh.stat[3]);
			}
		}
		snd_iprintf(buffer, "dma granularity : %d\n",
			    mgr->granularity);
		snd_iprintf(buffer, "dsp time errors : %d\n",
			    mgr->dsp_time_err);
		snd_iprintf(buffer, "dsp async pipe xrun errors : %d\n",
			    mgr->async_err_pipe_xrun);
		snd_iprintf(buffer, "dsp async stream xrun errors : %d\n",
			    mgr->async_err_stream_xrun);
		snd_iprintf(buffer, "dsp async last other error : %x\n",
			    mgr->async_err_other_last);
		/* debug zone dsp */
		rmh.cmd[0] = 0x4200 + PCXHR_SIZE_MAX_STATUS;
		rmh.cmd_len = 1;
		rmh.stat_len = PCXHR_SIZE_MAX_STATUS;
		rmh.dsp_stat = 0;
		rmh.cmd_idx = CMD_LAST_INDEX;
		if( ! pcxhr_send_msg(mgr, &rmh) ) {
			int i;
			if (rmh.stat_len > 8)
				rmh.stat_len = 8;
			for (i = 0; i < rmh.stat_len; i++)
				snd_iprintf(buffer, "debug[%02d] = %06x\n",
					    i,  rmh.stat[i]);
		}
	} else
		snd_iprintf(buffer, "no firmware loaded\n");
	snd_iprintf(buffer, "\n");
}
static void pcxhr_proc_sync(struct snd_info_entry *entry,
			    struct snd_info_buffer *buffer)
{
	struct snd_pcxhr *chip = entry->private_data;
	struct pcxhr_mgr *mgr = chip->mgr;
	static const char *textsHR22[3] = {
		"Internal", "AES Sync", "AES 1"
	};
	static const char *textsPCXHR[7] = {
		"Internal", "Word", "AES Sync",
		"AES 1", "AES 2", "AES 3", "AES 4"
	};
	const char **texts;
	int max_clock;
	if (mgr->is_hr_stereo) {
		texts = textsHR22;
		max_clock = HR22_CLOCK_TYPE_MAX;
	} else {
		texts = textsPCXHR;
		max_clock = PCXHR_CLOCK_TYPE_MAX;
	}

	snd_iprintf(buffer, "\n%s\n", mgr->longname);
	snd_iprintf(buffer, "Current Sample Clock\t: %s\n",
		    texts[mgr->cur_clock_type]);
	snd_iprintf(buffer, "Current Sample Rate\t= %d\n",
		    mgr->sample_rate_real);
	/* commands available when embedded DSP is running */
	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_MAIN_INDEX)) {
		int i, err, sample_rate;
		for (i = 1; i <= max_clock; i++) {
			err = pcxhr_get_external_clock(mgr, i, &sample_rate);
			if (err)
				break;
			snd_iprintf(buffer, "%s Clock\t\t= %d\n",
				    texts[i], sample_rate);
		}
	} else
		snd_iprintf(buffer, "no firmware loaded\n");
	snd_iprintf(buffer, "\n");
}

static void pcxhr_proc_gpio_read(struct snd_info_entry *entry,
				 struct snd_info_buffer *buffer)
{
	struct snd_pcxhr *chip = entry->private_data;
	struct pcxhr_mgr *mgr = chip->mgr;
	/* commands available when embedded DSP is running */
	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_MAIN_INDEX)) {
		/* gpio ports on stereo boards only available */
		int value = 0;
		hr222_read_gpio(mgr, 1, &value);	/* GPI */
		snd_iprintf(buffer, "GPI: 0x%x\n", value);
		hr222_read_gpio(mgr, 0, &value);	/* GP0 */
		snd_iprintf(buffer, "GPO: 0x%x\n", value);
	} else
		snd_iprintf(buffer, "no firmware loaded\n");
	snd_iprintf(buffer, "\n");
}
static void pcxhr_proc_gpo_write(struct snd_info_entry *entry,
				 struct snd_info_buffer *buffer)
{
	struct snd_pcxhr *chip = entry->private_data;
	struct pcxhr_mgr *mgr = chip->mgr;
	char line[64];
	int value;
	/* commands available when embedded DSP is running */
	if (!(mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_MAIN_INDEX)))
		return;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "GPO: 0x%x", &value) != 1)
			continue;
		hr222_write_gpo(mgr, value);	/* GP0 */
	}
}

static void __devinit pcxhr_proc_init(struct snd_pcxhr *chip)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(chip->card, "info", &entry))
		snd_info_set_text_ops(entry, chip, pcxhr_proc_info);
	if (! snd_card_proc_new(chip->card, "sync", &entry))
		snd_info_set_text_ops(entry, chip, pcxhr_proc_sync);
	/* gpio available on stereo sound cards only */
	if (chip->mgr->is_hr_stereo &&
	    !snd_card_proc_new(chip->card, "gpio", &entry)) {
		snd_info_set_text_ops(entry, chip, pcxhr_proc_gpio_read);
		entry->c.text.write = pcxhr_proc_gpo_write;
		entry->mode |= S_IWUSR;
	}
}
/* end of proc interface */

/*
 * release all the cards assigned to a manager instance
 */
static int pcxhr_free(struct pcxhr_mgr *mgr)
{
	unsigned int i;

	for (i = 0; i < mgr->num_cards; i++) {
		if (mgr->chip[i])
			snd_card_free(mgr->chip[i]->card);
	}

	/* reset board if some firmware was loaded */
	if(mgr->dsp_loaded) {
		pcxhr_reset_board(mgr);
		snd_printdd("reset pcxhr !\n");
	}

	/* release irq  */
	if (mgr->irq >= 0)
		free_irq(mgr->irq, mgr);

	pci_release_regions(mgr->pci);

	/* free hostport purgebuffer */
	if (mgr->hostport.area) {
		snd_dma_free_pages(&mgr->hostport);
		mgr->hostport.area = NULL;
	}

	kfree(mgr->prmh);

	pci_disable_device(mgr->pci);
	kfree(mgr);
	return 0;
}

/*
 *    probe function - creates the card manager
 */
static int __devinit pcxhr_probe(struct pci_dev *pci,
				 const struct pci_device_id *pci_id)
{
	static int dev;
	struct pcxhr_mgr *mgr;
	unsigned int i;
	int err;
	size_t size;
	char *card_name;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (! enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	pci_set_master(pci);

	/* check if we can restrict PCI DMA transfers to 32 bits */
	if (pci_set_dma_mask(pci, DMA_BIT_MASK(32)) < 0) {
		snd_printk(KERN_ERR "architecture does not support "
			   "32bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	/* alloc card manager */
	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (! mgr) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	if (snd_BUG_ON(pci_id->driver_data >= PCI_ID_LAST)) {
		kfree(mgr);
		pci_disable_device(pci);
		return -ENODEV;
	}
	card_name =
		pcxhr_board_params[pci_id->driver_data].board_name;
	mgr->playback_chips =
		pcxhr_board_params[pci_id->driver_data].playback_chips;
	mgr->capture_chips  =
		pcxhr_board_params[pci_id->driver_data].capture_chips;
	mgr->fw_file_set =
		pcxhr_board_params[pci_id->driver_data].fw_file_set;
	mgr->firmware_num  =
		pcxhr_board_params[pci_id->driver_data].firmware_num;
	mgr->mono_capture = mono[dev];
	mgr->is_hr_stereo = (mgr->playback_chips == 1);
	mgr->board_has_aes1 = PCXHR_BOARD_HAS_AES1(mgr);
	mgr->board_aes_in_192k = !PCXHR_BOARD_AESIN_NO_192K(mgr);

	if (mgr->is_hr_stereo)
		mgr->granularity = PCXHR_GRANULARITY_HR22;
	else
		mgr->granularity = PCXHR_GRANULARITY;

	/* resource assignment */
	if ((err = pci_request_regions(pci, card_name)) < 0) {
		kfree(mgr);
		pci_disable_device(pci);
		return err;
	}
	for (i = 0; i < 3; i++)
		mgr->port[i] = pci_resource_start(pci, i);

	mgr->pci = pci;
	mgr->irq = -1;

	if (request_irq(pci->irq, pcxhr_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, mgr)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		pcxhr_free(mgr);
		return -EBUSY;
	}
	mgr->irq = pci->irq;

	sprintf(mgr->shortname, "Digigram %s", card_name);
	sprintf(mgr->longname, "%s at 0x%lx & 0x%lx, 0x%lx irq %i",
		mgr->shortname,
		mgr->port[0], mgr->port[1], mgr->port[2], mgr->irq);

	/* ISR spinlock  */
	spin_lock_init(&mgr->lock);
	spin_lock_init(&mgr->msg_lock);

	/* init setup mutex*/
	mutex_init(&mgr->setup_mutex);

	/* init taslket */
	tasklet_init(&mgr->msg_taskq, pcxhr_msg_tasklet,
		     (unsigned long) mgr);
	tasklet_init(&mgr->trigger_taskq, pcxhr_trigger_tasklet,
		     (unsigned long) mgr);

	mgr->prmh = kmalloc(sizeof(*mgr->prmh) + 
			    sizeof(u32) * (PCXHR_SIZE_MAX_LONG_STATUS -
					   PCXHR_SIZE_MAX_STATUS),
			    GFP_KERNEL);
	if (! mgr->prmh) {
		pcxhr_free(mgr);
		return -ENOMEM;
	}

	for (i=0; i < PCXHR_MAX_CARDS; i++) {
		struct snd_card *card;
		char tmpid[16];
		int idx;

		if (i >= max(mgr->playback_chips, mgr->capture_chips))
			break;
		mgr->num_cards++;

		if (index[dev] < 0)
			idx = index[dev];
		else
			idx = index[dev] + i;

		snprintf(tmpid, sizeof(tmpid), "%s-%d",
			 id[dev] ? id[dev] : card_name, i);
		err = snd_card_create(idx, tmpid, THIS_MODULE, 0, &card);

		if (err < 0) {
			snd_printk(KERN_ERR "cannot allocate the card %d\n", i);
			pcxhr_free(mgr);
			return err;
		}

		strcpy(card->driver, DRIVER_NAME);
		sprintf(card->shortname, "%s [PCM #%d]", mgr->shortname, i);
		sprintf(card->longname, "%s [PCM #%d]", mgr->longname, i);

		if ((err = pcxhr_create(mgr, card, i)) < 0) {
			snd_card_free(card);
			pcxhr_free(mgr);
			return err;
		}

		if (i == 0)
			/* init proc interface only for chip0 */
			pcxhr_proc_init(mgr->chip[i]);

		if ((err = snd_card_register(card)) < 0) {
			pcxhr_free(mgr);
			return err;
		}
	}

	/* create hostport purgebuffer */
	size = PAGE_ALIGN(sizeof(struct pcxhr_hostport));
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				size, &mgr->hostport) < 0) {
		pcxhr_free(mgr);
		return -ENOMEM;
	}
	/* init purgebuffer */
	memset(mgr->hostport.area, 0, size);

	/* create a DSP loader */
	err = pcxhr_setup_firmware(mgr);
	if (err < 0) {
		pcxhr_free(mgr);
		return err;
	}

	pci_set_drvdata(pci, mgr);
	dev++;
	return 0;
}

static void __devexit pcxhr_remove(struct pci_dev *pci)
{
	pcxhr_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = KBUILD_MODNAME,
	.id_table = pcxhr_ids,
	.probe = pcxhr_probe,
	.remove = __devexit_p(pcxhr_remove),
};

static int __init pcxhr_module_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit pcxhr_module_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(pcxhr_module_init)
module_exit(pcxhr_module_exit)
