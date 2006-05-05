/*
 *  Dummy soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Dummy soundcard (/dev/null)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,Dummy soundcard}}");

#define MAX_PCM_DEVICES		4
#define MAX_PCM_SUBSTREAMS	16
#define MAX_MIDI_DEVICES	2

#if 0 /* emu10k1 emulation */
#define MAX_BUFFER_SIZE		(128 * 1024)
static int emu10k1_playback_constraints(struct snd_pcm_runtime *runtime)
{
	int err;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 256, UINT_MAX)) < 0)
		return err;
	return 0;
}
#define add_playback_constraints emu10k1_playback_constraints
#endif

#if 0 /* RME9652 emulation */
#define MAX_BUFFER_SIZE		(26 * 64 * 1024)
#define USE_FORMATS		SNDRV_PCM_FMTBIT_S32_LE
#define USE_CHANNELS_MIN	26
#define USE_CHANNELS_MAX	26
#define USE_PERIODS_MIN		2
#define USE_PERIODS_MAX		2
#endif

#if 0 /* ICE1712 emulation */
#define MAX_BUFFER_SIZE		(256 * 1024)
#define USE_FORMATS		SNDRV_PCM_FMTBIT_S32_LE
#define USE_CHANNELS_MIN	10
#define USE_CHANNELS_MAX	10
#define USE_PERIODS_MIN		1
#define USE_PERIODS_MAX		1024
#endif

#if 0 /* UDA1341 emulation */
#define MAX_BUFFER_SIZE		(16380)
#define USE_FORMATS		SNDRV_PCM_FMTBIT_S16_LE
#define USE_CHANNELS_MIN	2
#define USE_CHANNELS_MAX	2
#define USE_PERIODS_MIN		2
#define USE_PERIODS_MAX		255
#endif

#if 0 /* simple AC97 bridge (intel8x0) with 48kHz AC97 only codec */
#define USE_FORMATS		SNDRV_PCM_FMTBIT_S16_LE
#define USE_CHANNELS_MIN	2
#define USE_CHANNELS_MAX	2
#define USE_RATE		SNDRV_PCM_RATE_48000
#define USE_RATE_MIN		48000
#define USE_RATE_MAX		48000
#endif

#if 0 /* CA0106 */
#define USE_FORMATS		SNDRV_PCM_FMTBIT_S16_LE
#define USE_CHANNELS_MIN	2
#define USE_CHANNELS_MAX	2
#define USE_RATE		(SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_96000|SNDRV_PCM_RATE_192000) 
#define USE_RATE_MIN		48000 
#define USE_RATE_MAX		192000
#define MAX_BUFFER_SIZE		((65536-64)*8)
#define MAX_PERIOD_SIZE		(65536-64)
#define USE_PERIODS_MIN		2
#define USE_PERIODS_MAX		8
#endif


/* defaults */
#ifndef MAX_BUFFER_SIZE
#define MAX_BUFFER_SIZE		(64*1024)
#endif
#ifndef MAX_PERIOD_SIZE
#define MAX_PERIOD_SIZE		MAX_BUFFER_SIZE
#endif
#ifndef USE_FORMATS
#define USE_FORMATS 		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE)
#endif
#ifndef USE_RATE
#define USE_RATE		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000
#define USE_RATE_MIN		5500
#define USE_RATE_MAX		48000
#endif
#ifndef USE_CHANNELS_MIN
#define USE_CHANNELS_MIN 	1
#endif
#ifndef USE_CHANNELS_MAX
#define USE_CHANNELS_MAX 	2
#endif
#ifndef USE_PERIODS_MIN
#define USE_PERIODS_MIN 	1
#endif
#ifndef USE_PERIODS_MAX
#define USE_PERIODS_MAX 	1024
#endif
#ifndef add_playback_constraints
#define add_playback_constraints(x) 0
#endif
#ifndef add_capture_constraints
#define add_capture_constraints(x) 0
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int pcm_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
static int pcm_substreams[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8};
//static int midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for dummy soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for dummy soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this dummy soundcard.");
module_param_array(pcm_devs, int, NULL, 0444);
MODULE_PARM_DESC(pcm_devs, "PCM devices # (0-4) for dummy driver.");
module_param_array(pcm_substreams, int, NULL, 0444);
MODULE_PARM_DESC(pcm_substreams, "PCM substreams # (1-16) for dummy driver.");
//module_param_array(midi_devs, int, NULL, 0444);
//MODULE_PARM_DESC(midi_devs, "MIDI devices # (0-2) for dummy driver.");

static struct platform_device *devices[SNDRV_CARDS];

#define MIXER_ADDR_MASTER	0
#define MIXER_ADDR_LINE		1
#define MIXER_ADDR_MIC		2
#define MIXER_ADDR_SYNTH	3
#define MIXER_ADDR_CD		4
#define MIXER_ADDR_LAST		4

struct snd_dummy {
	struct snd_card *card;
	struct snd_pcm *pcm;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int capture_source[MIXER_ADDR_LAST+1][2];
};

struct snd_dummy_pcm {
	struct snd_dummy *dummy;
	spinlock_t lock;
	struct timer_list timer;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int pcm_bps;		/* bytes per second */
	unsigned int pcm_jiffie;	/* bytes per one jiffie */
	unsigned int pcm_irq_pos;	/* IRQ position */
	unsigned int pcm_buf_pos;	/* position in buffer */
	struct snd_pcm_substream *substream;
};


static inline void snd_card_dummy_pcm_timer_start(struct snd_dummy_pcm *dpcm)
{
	dpcm->timer.expires = 1 + jiffies;
	add_timer(&dpcm->timer);
}

static inline void snd_card_dummy_pcm_timer_stop(struct snd_dummy_pcm *dpcm)
{
	del_timer(&dpcm->timer);
}

static int snd_card_dummy_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dummy_pcm *dpcm = runtime->private_data;
	int err = 0;

	spin_lock(&dpcm->lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		snd_card_dummy_pcm_timer_start(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_card_dummy_pcm_timer_stop(dpcm);
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock(&dpcm->lock);
	return 0;
}

static int snd_card_dummy_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dummy_pcm *dpcm = runtime->private_data;
	unsigned int bps;

	bps = runtime->rate * runtime->channels;
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;
	dpcm->pcm_bps = bps;
	dpcm->pcm_jiffie = bps / HZ;
	dpcm->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	dpcm->pcm_count = snd_pcm_lib_period_bytes(substream);
	dpcm->pcm_irq_pos = 0;
	dpcm->pcm_buf_pos = 0;
	return 0;
}

static void snd_card_dummy_pcm_timer_function(unsigned long data)
{
	struct snd_dummy_pcm *dpcm = (struct snd_dummy_pcm *)data;
	unsigned long flags;
	
	spin_lock_irqsave(&dpcm->lock, flags);
	dpcm->timer.expires = 1 + jiffies;
	add_timer(&dpcm->timer);
	dpcm->pcm_irq_pos += dpcm->pcm_jiffie;
	dpcm->pcm_buf_pos += dpcm->pcm_jiffie;
	dpcm->pcm_buf_pos %= dpcm->pcm_size;
	if (dpcm->pcm_irq_pos >= dpcm->pcm_count) {
		dpcm->pcm_irq_pos %= dpcm->pcm_count;
		spin_unlock_irqrestore(&dpcm->lock, flags);
		snd_pcm_period_elapsed(dpcm->substream);
	} else
		spin_unlock_irqrestore(&dpcm->lock, flags);
}

static snd_pcm_uframes_t snd_card_dummy_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dummy_pcm *dpcm = runtime->private_data;

	return bytes_to_frames(runtime, dpcm->pcm_buf_pos);
}

static struct snd_pcm_hardware snd_card_dummy_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		USE_FORMATS,
	.rates =		USE_RATE,
	.rate_min =		USE_RATE_MIN,
	.rate_max =		USE_RATE_MAX,
	.channels_min =		USE_CHANNELS_MIN,
	.channels_max =		USE_CHANNELS_MAX,
	.buffer_bytes_max =	MAX_BUFFER_SIZE,
	.period_bytes_min =	64,
	.period_bytes_max =	MAX_BUFFER_SIZE,
	.periods_min =		USE_PERIODS_MIN,
	.periods_max =		USE_PERIODS_MAX,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_card_dummy_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		USE_FORMATS,
	.rates =		USE_RATE,
	.rate_min =		USE_RATE_MIN,
	.rate_max =		USE_RATE_MAX,
	.channels_min =		USE_CHANNELS_MIN,
	.channels_max =		USE_CHANNELS_MAX,
	.buffer_bytes_max =	MAX_BUFFER_SIZE,
	.period_bytes_min =	64,
	.period_bytes_max =	MAX_PERIOD_SIZE,
	.periods_min =		USE_PERIODS_MIN,
	.periods_max =		USE_PERIODS_MAX,
	.fifo_size =		0,
};

static void snd_card_dummy_runtime_free(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static int snd_card_dummy_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_card_dummy_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static struct snd_dummy_pcm *new_pcm_stream(struct snd_pcm_substream *substream)
{
	struct snd_dummy_pcm *dpcm;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (! dpcm)
		return dpcm;
	init_timer(&dpcm->timer);
	dpcm->timer.data = (unsigned long) dpcm;
	dpcm->timer.function = snd_card_dummy_pcm_timer_function;
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;
	return dpcm;
}

static int snd_card_dummy_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dummy_pcm *dpcm;
	int err;

	if ((dpcm = new_pcm_stream(substream)) == NULL)
		return -ENOMEM;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_dummy_runtime_free;
	runtime->hw = snd_card_dummy_playback;
	if (substream->pcm->device & 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}
	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
	if ((err = add_playback_constraints(runtime)) < 0) {
		kfree(dpcm);
		return err;
	}

	return 0;
}

static int snd_card_dummy_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dummy_pcm *dpcm;
	int err;

	if ((dpcm = new_pcm_stream(substream)) == NULL)
		return -ENOMEM;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_dummy_runtime_free;
	runtime->hw = snd_card_dummy_capture;
	if (substream->pcm->device == 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}
	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
	if ((err = add_capture_constraints(runtime)) < 0) {
		kfree(dpcm);
		return err;
	}

	return 0;
}

static int snd_card_dummy_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_card_dummy_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_pcm_ops snd_card_dummy_playback_ops = {
	.open =			snd_card_dummy_playback_open,
	.close =		snd_card_dummy_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_card_dummy_hw_params,
	.hw_free =		snd_card_dummy_hw_free,
	.prepare =		snd_card_dummy_pcm_prepare,
	.trigger =		snd_card_dummy_pcm_trigger,
	.pointer =		snd_card_dummy_pcm_pointer,
};

static struct snd_pcm_ops snd_card_dummy_capture_ops = {
	.open =			snd_card_dummy_capture_open,
	.close =		snd_card_dummy_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_card_dummy_hw_params,
	.hw_free =		snd_card_dummy_hw_free,
	.prepare =		snd_card_dummy_pcm_prepare,
	.trigger =		snd_card_dummy_pcm_trigger,
	.pointer =		snd_card_dummy_pcm_pointer,
};

static int __init snd_card_dummy_pcm(struct snd_dummy *dummy, int device, int substreams)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(dummy->card, "Dummy PCM", device,
			       substreams, substreams, &pcm)) < 0)
		return err;
	dummy->pcm = pcm;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_card_dummy_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_dummy_capture_ops);
	pcm->private_data = dummy;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Dummy PCM");
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data(GFP_KERNEL),
					      0, 64*1024);
	return 0;
}

#define DUMMY_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_dummy_volume_info, \
  .get = snd_dummy_volume_get, .put = snd_dummy_volume_put, \
  .private_value = addr }

static int snd_dummy_volume_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = -50;
	uinfo->value.integer.max = 100;
	return 0;
}
 
static int snd_dummy_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&dummy->mixer_lock);
	ucontrol->value.integer.value[0] = dummy->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = dummy->mixer_volume[addr][1];
	spin_unlock_irq(&dummy->mixer_lock);
	return 0;
}

static int snd_dummy_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0];
	if (left < -50)
		left = -50;
	if (left > 100)
		left = 100;
	right = ucontrol->value.integer.value[1];
	if (right < -50)
		right = -50;
	if (right > 100)
		right = 100;
	spin_lock_irq(&dummy->mixer_lock);
	change = dummy->mixer_volume[addr][0] != left ||
	         dummy->mixer_volume[addr][1] != right;
	dummy->mixer_volume[addr][0] = left;
	dummy->mixer_volume[addr][1] = right;
	spin_unlock_irq(&dummy->mixer_lock);
	return change;
}

#define DUMMY_CAPSRC(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_dummy_capsrc_info, \
  .get = snd_dummy_capsrc_get, .put = snd_dummy_capsrc_put, \
  .private_value = addr }

static int snd_dummy_capsrc_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int snd_dummy_capsrc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&dummy->mixer_lock);
	ucontrol->value.integer.value[0] = dummy->capture_source[addr][0];
	ucontrol->value.integer.value[1] = dummy->capture_source[addr][1];
	spin_unlock_irq(&dummy->mixer_lock);
	return 0;
}

static int snd_dummy_capsrc_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irq(&dummy->mixer_lock);
	change = dummy->capture_source[addr][0] != left &&
	         dummy->capture_source[addr][1] != right;
	dummy->capture_source[addr][0] = left;
	dummy->capture_source[addr][1] = right;
	spin_unlock_irq(&dummy->mixer_lock);
	return change;
}

static struct snd_kcontrol_new snd_dummy_controls[] = {
DUMMY_VOLUME("Master Volume", 0, MIXER_ADDR_MASTER),
DUMMY_CAPSRC("Master Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Synth Volume", 0, MIXER_ADDR_SYNTH),
DUMMY_CAPSRC("Synth Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Line Volume", 0, MIXER_ADDR_LINE),
DUMMY_CAPSRC("Line Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Mic Volume", 0, MIXER_ADDR_MIC),
DUMMY_CAPSRC("Mic Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("CD Volume", 0, MIXER_ADDR_CD),
DUMMY_CAPSRC("CD Capture Switch", 0, MIXER_ADDR_MASTER)
};

static int __init snd_card_dummy_new_mixer(struct snd_dummy *dummy)
{
	struct snd_card *card = dummy->card;
	unsigned int idx;
	int err;

	snd_assert(dummy != NULL, return -EINVAL);
	spin_lock_init(&dummy->mixer_lock);
	strcpy(card->mixername, "Dummy Mixer");

	for (idx = 0; idx < ARRAY_SIZE(snd_dummy_controls); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_dummy_controls[idx], dummy))) < 0)
			return err;
	}
	return 0;
}

static int __init snd_dummy_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_dummy *dummy;
	int idx, err;
	int dev = devptr->id;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_dummy));
	if (card == NULL)
		return -ENOMEM;
	dummy = card->private_data;
	dummy->card = card;
	for (idx = 0; idx < MAX_PCM_DEVICES && idx < pcm_devs[dev]; idx++) {
		if (pcm_substreams[dev] < 1)
			pcm_substreams[dev] = 1;
		if (pcm_substreams[dev] > MAX_PCM_SUBSTREAMS)
			pcm_substreams[dev] = MAX_PCM_SUBSTREAMS;
		if ((err = snd_card_dummy_pcm(dummy, idx, pcm_substreams[dev])) < 0)
			goto __nodev;
	}
	if ((err = snd_card_dummy_new_mixer(dummy)) < 0)
		goto __nodev;
	strcpy(card->driver, "Dummy");
	strcpy(card->shortname, "Dummy");
	sprintf(card->longname, "Dummy %i", dev + 1);

	snd_card_set_dev(card, &devptr->dev);

	if ((err = snd_card_register(card)) == 0) {
		platform_set_drvdata(devptr, card);
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int snd_dummy_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_dummy_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct snd_dummy *dummy = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(dummy->pcm);
	return 0;
}
	
static int snd_dummy_resume(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

#define SND_DUMMY_DRIVER	"snd_dummy"

static struct platform_driver snd_dummy_driver = {
	.probe		= snd_dummy_probe,
	.remove		= snd_dummy_remove,
#ifdef CONFIG_PM
	.suspend	= snd_dummy_suspend,
	.resume		= snd_dummy_resume,
#endif
	.driver		= {
		.name	= SND_DUMMY_DRIVER
	},
};

static void __init_or_module snd_dummy_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_dummy_driver);
}

static int __init alsa_card_dummy_init(void)
{
	int i, cards, err;

	if ((err = platform_driver_register(&snd_dummy_driver)) < 0)
		return err;

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(SND_DUMMY_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device))
			continue;
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Dummy soundcard not found or device busy\n");
#endif
		snd_dummy_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_dummy_exit(void)
{
	snd_dummy_unregister_all();
}

module_init(alsa_card_dummy_init)
module_exit(alsa_card_dummy_exit)
