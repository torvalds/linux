/*
 *  Asihpi soundcard
 *  Copyright (c) by AudioScience Inc <support@audioscience.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation;
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
 *
 *  The following is not a condition of use, merely a request:
 *  If you modify this program, particularly if you fix errors, AudioScience Inc
 *  would appreciate it if you grant us the right to use those modifications
 *  for any purpose including commercial applications.
 */

#include "hpi_internal.h"
#include "hpi_version.h"
#include "hpimsginit.h"
#include "hpioctl.h"
#include "hpicmn.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/hwdep.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AudioScience inc. <support@audioscience.com>");
MODULE_DESCRIPTION("AudioScience ALSA ASI5xxx ASI6xxx ASI87xx ASI89xx "
			HPI_VER_STRING);

#if defined CONFIG_SND_DEBUG_VERBOSE
/**
 * snd_printddd - very verbose debug printk
 * @format: format string
 *
 * Works like snd_printk() for debugging purposes.
 * Ignored when CONFIG_SND_DEBUG_VERBOSE is not set.
 * Must set snd module debug parameter to 3 to enable at runtime.
 */
#define snd_printddd(format, args...) \
	__snd_printk(3, __FILE__, __LINE__, format, ##args)
#else
#define snd_printddd(format, args...) do { } while (0)
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static bool enable_hpi_hwdep = 1;

module_param_array(index, int, NULL, S_IRUGO);
MODULE_PARM_DESC(index, "ALSA index value for AudioScience soundcard.");

module_param_array(id, charp, NULL, S_IRUGO);
MODULE_PARM_DESC(id, "ALSA ID string for AudioScience soundcard.");

module_param_array(enable, bool, NULL, S_IRUGO);
MODULE_PARM_DESC(enable, "ALSA enable AudioScience soundcard.");

module_param(enable_hpi_hwdep, bool, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(enable_hpi_hwdep,
		"ALSA enable HPI hwdep for AudioScience soundcard ");

/* identify driver */
#ifdef KERNEL_ALSA_BUILD
static char *build_info = "Built using headers from kernel source";
module_param(build_info, charp, S_IRUGO);
MODULE_PARM_DESC(build_info, "Built using headers from kernel source");
#else
static char *build_info = "Built within ALSA source";
module_param(build_info, charp, S_IRUGO);
MODULE_PARM_DESC(build_info, "Built within ALSA source");
#endif

/* set to 1 to dump every control from adapter to log */
static const int mixer_dump;

#define DEFAULT_SAMPLERATE 44100
static int adapter_fs = DEFAULT_SAMPLERATE;

/* defaults */
#define PERIODS_MIN 2
#define PERIOD_BYTES_MIN  2048
#define BUFFER_BYTES_MAX (512 * 1024)

#define MAX_CLOCKSOURCES (HPI_SAMPLECLOCK_SOURCE_LAST + 1 + 7)

struct clk_source {
	int source;
	int index;
	const char *name;
};

struct clk_cache {
	int count;
	int has_local;
	struct clk_source s[MAX_CLOCKSOURCES];
};

/* Per card data */
struct snd_card_asihpi {
	struct snd_card *card;
	struct pci_dev *pci;
	struct hpi_adapter *hpi;

	/* In low latency mode there is only one stream, a pointer to its
	 * private data is stored here on trigger and cleared on stop.
	 * The interrupt handler uses it as a parameter when calling
	 * snd_card_asihpi_timer_function().
	 */
	struct snd_card_asihpi_pcm *llmode_streampriv;
	struct tasklet_struct t;
	void (*pcm_start)(struct snd_pcm_substream *substream);
	void (*pcm_stop)(struct snd_pcm_substream *substream);

	u32 h_mixer;
	struct clk_cache cc;

	u16 can_dma;
	u16 support_grouping;
	u16 support_mrx;
	u16 update_interval_frames;
	u16 in_max_chans;
	u16 out_max_chans;
	u16 in_min_chans;
	u16 out_min_chans;
};

/* Per stream data */
struct snd_card_asihpi_pcm {
	struct timer_list timer;
	unsigned int respawn_timer;
	unsigned int hpi_buffer_attached;
	unsigned int buffer_bytes;
	unsigned int period_bytes;
	unsigned int bytes_per_sec;
	unsigned int pcm_buf_host_rw_ofs; /* Host R/W pos */
	unsigned int pcm_buf_dma_ofs;	/* DMA R/W offset in buffer */
	unsigned int pcm_buf_elapsed_dma_ofs;	/* DMA R/W offset in buffer */
	unsigned int drained_count;
	struct snd_pcm_substream *substream;
	u32 h_stream;
	struct hpi_format format;
};

/* universal stream verbs work with out or in stream handles */

/* Functions to allow driver to give a buffer to HPI for busmastering */

static u16 hpi_stream_host_buffer_attach(
	u32 h_stream,   /* handle to outstream. */
	u32 size_in_bytes, /* size in bytes of bus mastering buffer */
	u32 pci_address
)
{
	struct hpi_message hm;
	struct hpi_response hr;
	unsigned int obj = hpi_handle_object(h_stream);

	if (!h_stream)
		return HPI_ERROR_INVALID_OBJ;
	hpi_init_message_response(&hm, &hr, obj,
			obj == HPI_OBJ_OSTREAM ?
				HPI_OSTREAM_HOSTBUFFER_ALLOC :
				HPI_ISTREAM_HOSTBUFFER_ALLOC);

	hpi_handle_to_indexes(h_stream, &hm.adapter_index,
				&hm.obj_index);

	hm.u.d.u.buffer.buffer_size = size_in_bytes;
	hm.u.d.u.buffer.pci_address = pci_address;
	hm.u.d.u.buffer.command = HPI_BUFFER_CMD_INTERNAL_GRANTADAPTER;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static u16 hpi_stream_host_buffer_detach(u32  h_stream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	unsigned int obj = hpi_handle_object(h_stream);

	if (!h_stream)
		return HPI_ERROR_INVALID_OBJ;

	hpi_init_message_response(&hm, &hr,  obj,
			obj == HPI_OBJ_OSTREAM ?
				HPI_OSTREAM_HOSTBUFFER_FREE :
				HPI_ISTREAM_HOSTBUFFER_FREE);

	hpi_handle_to_indexes(h_stream, &hm.adapter_index,
				&hm.obj_index);
	hm.u.d.u.buffer.command = HPI_BUFFER_CMD_INTERNAL_REVOKEADAPTER;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static inline u16 hpi_stream_start(u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_start(h_stream);
	else
		return hpi_instream_start(h_stream);
}

static inline u16 hpi_stream_stop(u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_stop(h_stream);
	else
		return hpi_instream_stop(h_stream);
}

static inline u16 hpi_stream_get_info_ex(
    u32 h_stream,
    u16        *pw_state,
    u32        *pbuffer_size,
    u32        *pdata_in_buffer,
    u32        *psample_count,
    u32        *pauxiliary_data
)
{
	u16 e;
	if (hpi_handle_object(h_stream)  ==  HPI_OBJ_OSTREAM)
		e = hpi_outstream_get_info_ex(h_stream, pw_state,
					pbuffer_size, pdata_in_buffer,
					psample_count, pauxiliary_data);
	else
		e = hpi_instream_get_info_ex(h_stream, pw_state,
					pbuffer_size, pdata_in_buffer,
					psample_count, pauxiliary_data);
	return e;
}

static inline u16 hpi_stream_group_add(
					u32 h_master,
					u32 h_stream)
{
	if (hpi_handle_object(h_master) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_add(h_master, h_stream);
	else
		return hpi_instream_group_add(h_master, h_stream);
}

static inline u16 hpi_stream_group_reset(u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_reset(h_stream);
	else
		return hpi_instream_group_reset(h_stream);
}

static inline u16 hpi_stream_group_get_map(
				u32 h_stream, u32 *mo, u32 *mi)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_get_map(h_stream, mo, mi);
	else
		return hpi_instream_group_get_map(h_stream, mo, mi);
}

static u16 handle_error(u16 err, int line, char *filename)
{
	if (err)
		printk(KERN_WARNING
			"in file %s, line %d: HPI error %d\n",
			filename, line, err);
	return err;
}

#define hpi_handle_error(x)  handle_error(x, __LINE__, __FILE__)

/***************************** GENERAL PCM ****************/

static void print_hwparams(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *p)
{
	char name[16];
	snd_pcm_debug_name(substream, name, sizeof(name));
	snd_printdd("%s HWPARAMS\n", name);
	snd_printdd(" samplerate=%dHz channels=%d format=%d subformat=%d\n",
		params_rate(p), params_channels(p),
		params_format(p), params_subformat(p));
	snd_printdd(" buffer=%dB period=%dB period_size=%dB periods=%d\n",
		params_buffer_bytes(p), params_period_bytes(p),
		params_period_size(p), params_periods(p));
	snd_printdd(" buffer_size=%d access=%d data_rate=%dB/s\n",
		params_buffer_size(p), params_access(p),
		params_rate(p) * params_channels(p) *
		snd_pcm_format_width(params_format(p)) / 8);
}

static snd_pcm_format_t hpi_to_alsa_formats[] = {
	-1,			/* INVALID */
	SNDRV_PCM_FORMAT_U8,	/* HPI_FORMAT_PCM8_UNSIGNED        1 */
	SNDRV_PCM_FORMAT_S16,	/* HPI_FORMAT_PCM16_SIGNED         2 */
	-1,			/* HPI_FORMAT_MPEG_L1              3 */
	SNDRV_PCM_FORMAT_MPEG,	/* HPI_FORMAT_MPEG_L2              4 */
	SNDRV_PCM_FORMAT_MPEG,	/* HPI_FORMAT_MPEG_L3              5 */
	-1,			/* HPI_FORMAT_DOLBY_AC2            6 */
	-1,			/* HPI_FORMAT_DOLBY_AC3            7 */
	SNDRV_PCM_FORMAT_S16_BE,/* HPI_FORMAT_PCM16_BIGENDIAN      8 */
	-1,			/* HPI_FORMAT_AA_TAGIT1_HITS       9 */
	-1,			/* HPI_FORMAT_AA_TAGIT1_INSERTS   10 */
	SNDRV_PCM_FORMAT_S32,	/* HPI_FORMAT_PCM32_SIGNED        11 */
	-1,			/* HPI_FORMAT_RAW_BITSTREAM       12 */
	-1,			/* HPI_FORMAT_AA_TAGIT1_HITS_EX1  13 */
	SNDRV_PCM_FORMAT_FLOAT,	/* HPI_FORMAT_PCM32_FLOAT         14 */
#if 1
	/* ALSA can't handle 3 byte sample size together with power-of-2
	 *  constraint on buffer_bytes, so disable this format
	 */
	-1
#else
	/* SNDRV_PCM_FORMAT_S24_3LE */ /* HPI_FORMAT_PCM24_SIGNED 15 */
#endif
};


static int snd_card_asihpi_format_alsa2hpi(snd_pcm_format_t alsa_format,
					   u16 *hpi_format)
{
	u16 format;

	for (format = HPI_FORMAT_PCM8_UNSIGNED;
	     format <= HPI_FORMAT_PCM24_SIGNED; format++) {
		if (hpi_to_alsa_formats[format] == alsa_format) {
			*hpi_format = format;
			return 0;
		}
	}

	snd_printd(KERN_WARNING "failed match for alsa format %d\n",
		   alsa_format);
	*hpi_format = 0;
	return -EINVAL;
}

static void snd_card_asihpi_pcm_samplerates(struct snd_card_asihpi *asihpi,
					 struct snd_pcm_hardware *pcmhw)
{
	u16 err;
	u32 h_control;
	u32 sample_rate;
	int idx;
	unsigned int rate_min = 200000;
	unsigned int rate_max = 0;
	unsigned int rates = 0;

	if (asihpi->support_mrx) {
		rates |= SNDRV_PCM_RATE_CONTINUOUS;
		rates |= SNDRV_PCM_RATE_8000_96000;
		rate_min = 8000;
		rate_max = 100000;
	} else {
		/* on cards without SRC,
		   valid rates are determined by sampleclock */
		err = hpi_mixer_get_control(asihpi->h_mixer,
					  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
					  HPI_CONTROL_SAMPLECLOCK, &h_control);
		if (err) {
			dev_err(&asihpi->pci->dev,
				"No local sampleclock, err %d\n", err);
		}

		for (idx = -1; idx < 100; idx++) {
			if (idx == -1) {
				if (hpi_sample_clock_get_sample_rate(h_control,
								&sample_rate))
					continue;
			} else if (hpi_sample_clock_query_local_rate(h_control,
							idx, &sample_rate)) {
				break;
			}

			rate_min = min(rate_min, sample_rate);
			rate_max = max(rate_max, sample_rate);

			switch (sample_rate) {
			case 5512:
				rates |= SNDRV_PCM_RATE_5512;
				break;
			case 8000:
				rates |= SNDRV_PCM_RATE_8000;
				break;
			case 11025:
				rates |= SNDRV_PCM_RATE_11025;
				break;
			case 16000:
				rates |= SNDRV_PCM_RATE_16000;
				break;
			case 22050:
				rates |= SNDRV_PCM_RATE_22050;
				break;
			case 32000:
				rates |= SNDRV_PCM_RATE_32000;
				break;
			case 44100:
				rates |= SNDRV_PCM_RATE_44100;
				break;
			case 48000:
				rates |= SNDRV_PCM_RATE_48000;
				break;
			case 64000:
				rates |= SNDRV_PCM_RATE_64000;
				break;
			case 88200:
				rates |= SNDRV_PCM_RATE_88200;
				break;
			case 96000:
				rates |= SNDRV_PCM_RATE_96000;
				break;
			case 176400:
				rates |= SNDRV_PCM_RATE_176400;
				break;
			case 192000:
				rates |= SNDRV_PCM_RATE_192000;
				break;
			default: /* some other rate */
				rates |= SNDRV_PCM_RATE_KNOT;
			}
		}
	}

	pcmhw->rates = rates;
	pcmhw->rate_min = rate_min;
	pcmhw->rate_max = rate_max;
}

static int snd_card_asihpi_pcm_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	int err;
	u16 format;
	int width;
	unsigned int bytes_per_sec;

	print_hwparams(substream, params);
	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (err < 0)
		return err;
	err = snd_card_asihpi_format_alsa2hpi(params_format(params), &format);
	if (err)
		return err;

	hpi_handle_error(hpi_format_create(&dpcm->format,
			params_channels(params),
			format, params_rate(params), 0, 0));

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (hpi_instream_reset(dpcm->h_stream) != 0)
			return -EINVAL;

		if (hpi_instream_set_format(
			dpcm->h_stream, &dpcm->format) != 0)
			return -EINVAL;
	}

	dpcm->hpi_buffer_attached = 0;
	if (card->can_dma) {
		err = hpi_stream_host_buffer_attach(dpcm->h_stream,
			params_buffer_bytes(params),  runtime->dma_addr);
		if (err == 0) {
			snd_printdd(
				"stream_host_buffer_attach success %u %lu\n",
				params_buffer_bytes(params),
				(unsigned long)runtime->dma_addr);
		} else {
			snd_printd("stream_host_buffer_attach error %d\n",
					err);
			return -ENOMEM;
		}

		err = hpi_stream_get_info_ex(dpcm->h_stream, NULL,
				&dpcm->hpi_buffer_attached, NULL, NULL, NULL);
	}
	bytes_per_sec = params_rate(params) * params_channels(params);
	width = snd_pcm_format_width(params_format(params));
	bytes_per_sec *= width;
	bytes_per_sec /= 8;
	if (width < 0 || bytes_per_sec == 0)
		return -EINVAL;

	dpcm->bytes_per_sec = bytes_per_sec;
	dpcm->buffer_bytes = params_buffer_bytes(params);
	dpcm->period_bytes = params_period_bytes(params);

	return 0;
}

static int
snd_card_asihpi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	if (dpcm->hpi_buffer_attached)
		hpi_stream_host_buffer_detach(dpcm->h_stream);

	snd_pcm_lib_free_pages(substream);
	return 0;
}

static void snd_card_asihpi_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	kfree(dpcm);
}

static void snd_card_asihpi_pcm_timer_start(struct snd_pcm_substream *
					    substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	int expiry;

	expiry = HZ / 200;

	expiry = max(expiry, 1); /* don't let it be zero! */
	mod_timer(&dpcm->timer, jiffies + expiry);
	dpcm->respawn_timer = 1;
}

static void snd_card_asihpi_pcm_timer_stop(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	dpcm->respawn_timer = 0;
	del_timer(&dpcm->timer);
}

static void snd_card_asihpi_pcm_int_start(struct snd_pcm_substream *substream)
{
	struct snd_card_asihpi_pcm *dpcm;
	struct snd_card_asihpi *card;

	BUG_ON(!substream);

	dpcm = (struct snd_card_asihpi_pcm *)substream->runtime->private_data;
	card = snd_pcm_substream_chip(substream);

	BUG_ON(in_interrupt());
	tasklet_disable(&card->t);
	card->llmode_streampriv = dpcm;
	tasklet_enable(&card->t);

	hpi_handle_error(hpi_adapter_set_property(card->hpi->adapter->index,
		HPI_ADAPTER_PROPERTY_IRQ_RATE,
		card->update_interval_frames, 0));
}

static void snd_card_asihpi_pcm_int_stop(struct snd_pcm_substream *substream)
{
	struct snd_card_asihpi_pcm *dpcm;
	struct snd_card_asihpi *card;

	BUG_ON(!substream);

	dpcm = (struct snd_card_asihpi_pcm *)substream->runtime->private_data;
	card = snd_pcm_substream_chip(substream);

	hpi_handle_error(hpi_adapter_set_property(card->hpi->adapter->index,
		HPI_ADAPTER_PROPERTY_IRQ_RATE, 0, 0));

	if (in_interrupt())
		card->llmode_streampriv = NULL;
	else {
		tasklet_disable(&card->t);
		card->llmode_streampriv = NULL;
		tasklet_enable(&card->t);
	}
}

static int snd_card_asihpi_trigger(struct snd_pcm_substream *substream,
					   int cmd)
{
	struct snd_card_asihpi_pcm *dpcm = substream->runtime->private_data;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	u16 e;
	char name[16];

	snd_pcm_debug_name(substream, name, sizeof(name));

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_printdd("%s trigger start\n", name);
		snd_pcm_group_for_each_entry(s, substream) {
			struct snd_pcm_runtime *runtime = s->runtime;
			struct snd_card_asihpi_pcm *ds = runtime->private_data;

			if (snd_pcm_substream_chip(s) != card)
				continue;

			/* don't link Cap and Play */
			if (substream->stream != s->stream)
				continue;

			ds->drained_count = 0;
			if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				/* How do I know how much valid data is present
				* in buffer? Must be at least one period!
				* Guessing 2 periods, but if
				* buffer is bigger it may contain even more
				* data??
				*/
				unsigned int preload = ds->period_bytes * 1;
				snd_printddd("%d preload %d\n", s->number, preload);
				hpi_handle_error(hpi_outstream_write_buf(
						ds->h_stream,
						&runtime->dma_area[0],
						preload,
						&ds->format));
				ds->pcm_buf_host_rw_ofs = preload;
			}

			if (card->support_grouping) {
				snd_printdd("%d group\n", s->number);
				e = hpi_stream_group_add(
					dpcm->h_stream,
					ds->h_stream);
				if (!e) {
					snd_pcm_trigger_done(s, substream);
				} else {
					hpi_handle_error(e);
					break;
				}
			} else
				break;
		}
		/* start the master stream */
		card->pcm_start(substream);
		if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) ||
			!card->can_dma)
			hpi_handle_error(hpi_stream_start(dpcm->h_stream));
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		snd_printdd("%s trigger stop\n", name);
		card->pcm_stop(substream);
		snd_pcm_group_for_each_entry(s, substream) {
			if (snd_pcm_substream_chip(s) != card)
				continue;
			/* don't link Cap and Play */
			if (substream->stream != s->stream)
				continue;

			/*? workaround linked streams don't
			transition to SETUP 20070706*/
			s->runtime->status->state = SNDRV_PCM_STATE_SETUP;

			if (card->support_grouping) {
				snd_printdd("%d group\n", s->number);
				snd_pcm_trigger_done(s, substream);
			} else
				break;
		}

		/* _prepare and _hwparams reset the stream */
		hpi_handle_error(hpi_stream_stop(dpcm->h_stream));
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			hpi_handle_error(
				hpi_outstream_reset(dpcm->h_stream));

		if (card->support_grouping)
			hpi_handle_error(hpi_stream_group_reset(dpcm->h_stream));
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printdd("%s trigger pause release\n", name);
		card->pcm_start(substream);
		hpi_handle_error(hpi_stream_start(dpcm->h_stream));
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printdd("%s trigger pause push\n", name);
		card->pcm_stop(substream);
		hpi_handle_error(hpi_stream_stop(dpcm->h_stream));
		break;
	default:
		snd_printd(KERN_ERR "\tINVALID\n");
		return -EINVAL;
	}

	return 0;
}

/*algorithm outline
 Without linking degenerates to getting single stream pos etc
 Without mmap 2nd loop degenerates to snd_pcm_period_elapsed
*/
/*
pcm_buf_dma_ofs=get_buf_pos(s);
for_each_linked_stream(s) {
	pcm_buf_dma_ofs=get_buf_pos(s);
	min_buf_pos = modulo_min(min_buf_pos, pcm_buf_dma_ofs, buffer_bytes)
	new_data = min(new_data, calc_new_data(pcm_buf_dma_ofs,irq_pos)
}
timer.expires = jiffies + predict_next_period_ready(min_buf_pos);
for_each_linked_stream(s) {
	s->pcm_buf_dma_ofs = min_buf_pos;
	if (new_data > period_bytes) {
		if (mmap) {
			irq_pos = (irq_pos + period_bytes) % buffer_bytes;
			if (playback) {
				write(period_bytes);
			} else {
				read(period_bytes);
			}
		}
		snd_pcm_period_elapsed(s);
	}
}
*/

/** Minimum of 2 modulo values.  Works correctly when the difference between
* the values is less than half the modulus
*/
static inline unsigned int modulo_min(unsigned int a, unsigned int b,
					unsigned long int modulus)
{
	unsigned int result;
	if (((a-b) % modulus) < (modulus/2))
		result = b;
	else
		result = a;

	return result;
}

/** Timer function, equivalent to interrupt service routine for cards
*/
static void snd_card_asihpi_timer_function(unsigned long data)
{
	struct snd_card_asihpi_pcm *dpcm = (struct snd_card_asihpi_pcm *)data;
	struct snd_pcm_substream *substream = dpcm->substream;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *s;
	unsigned int newdata = 0;
	unsigned int pcm_buf_dma_ofs, min_buf_pos = 0;
	unsigned int remdata, xfercount, next_jiffies;
	int first = 1;
	int loops = 0;
	u16 state;
	u32 buffer_size, bytes_avail, samples_played, on_card_bytes;
	char name[16];


	snd_pcm_debug_name(substream, name, sizeof(name));

	/* find minimum newdata and buffer pos in group */
	snd_pcm_group_for_each_entry(s, substream) {
		struct snd_card_asihpi_pcm *ds = s->runtime->private_data;
		runtime = s->runtime;

		if (snd_pcm_substream_chip(s) != card)
			continue;

		/* don't link Cap and Play */
		if (substream->stream != s->stream)
			continue;

		hpi_handle_error(hpi_stream_get_info_ex(
					ds->h_stream, &state,
					&buffer_size, &bytes_avail,
					&samples_played, &on_card_bytes));

		/* number of bytes in on-card buffer */
		runtime->delay = on_card_bytes;

		if (!card->can_dma)
			on_card_bytes = bytes_avail;

		if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			pcm_buf_dma_ofs = ds->pcm_buf_host_rw_ofs - bytes_avail;
			if (state == HPI_STATE_STOPPED) {
				if (bytes_avail == 0) {
					hpi_handle_error(hpi_stream_start(ds->h_stream));
					snd_printdd("P%d start\n", s->number);
					ds->drained_count = 0;
				}
			} else if (state == HPI_STATE_DRAINED) {
				snd_printd(KERN_WARNING "P%d drained\n",
						s->number);
				ds->drained_count++;
				if (ds->drained_count > 20) {
					snd_pcm_stop_xrun(s);
					continue;
				}
			} else {
				ds->drained_count = 0;
			}
		} else
			pcm_buf_dma_ofs = bytes_avail + ds->pcm_buf_host_rw_ofs;

		if (first) {
			/* can't statically init min when wrap is involved */
			min_buf_pos = pcm_buf_dma_ofs;
			newdata = (pcm_buf_dma_ofs - ds->pcm_buf_elapsed_dma_ofs) % ds->buffer_bytes;
			first = 0;
		} else {
			min_buf_pos =
				modulo_min(min_buf_pos, pcm_buf_dma_ofs, UINT_MAX+1L);
			newdata = min(
				(pcm_buf_dma_ofs - ds->pcm_buf_elapsed_dma_ofs) % ds->buffer_bytes,
				newdata);
		}

		snd_printddd(
			"timer1, %s, %d, S=%d, elap=%d, rw=%d, dsp=%d, left=%d, aux=%d, space=%d, hw_ptr=%ld, appl_ptr=%ld\n",
			name, s->number, state,
			ds->pcm_buf_elapsed_dma_ofs,
			ds->pcm_buf_host_rw_ofs,
			pcm_buf_dma_ofs,
			(int)bytes_avail,

			(int)on_card_bytes,
			buffer_size-bytes_avail,
			(unsigned long)frames_to_bytes(runtime,
						runtime->status->hw_ptr),
			(unsigned long)frames_to_bytes(runtime,
						runtime->control->appl_ptr)
		);
		loops++;
	}
	pcm_buf_dma_ofs = min_buf_pos;

	remdata = newdata % dpcm->period_bytes;
	xfercount = newdata - remdata; /* a multiple of period_bytes */
	/* come back when on_card_bytes has decreased enough to allow
	   write to happen, or when data has been consumed to make another
	   period
	*/
	if (xfercount && (on_card_bytes  > dpcm->period_bytes))
		next_jiffies = ((on_card_bytes - dpcm->period_bytes) * HZ / dpcm->bytes_per_sec);
	else
		next_jiffies = ((dpcm->period_bytes - remdata) * HZ / dpcm->bytes_per_sec);

	next_jiffies = max(next_jiffies, 1U);
	dpcm->timer.expires = jiffies + next_jiffies;
	snd_printddd("timer2, jif=%d, buf_pos=%d, newdata=%d, xfer=%d\n",
			next_jiffies, pcm_buf_dma_ofs, newdata, xfercount);

	snd_pcm_group_for_each_entry(s, substream) {
		struct snd_card_asihpi_pcm *ds = s->runtime->private_data;
		runtime = s->runtime;

		/* don't link Cap and Play */
		if (substream->stream != s->stream)
			continue;

		/* Store dma offset for use by pointer callback */
		ds->pcm_buf_dma_ofs = pcm_buf_dma_ofs;

		if (xfercount &&
			/* Limit use of on card fifo for playback */
			((on_card_bytes <= ds->period_bytes) ||
			(s->stream == SNDRV_PCM_STREAM_CAPTURE)))

		{

			unsigned int buf_ofs = ds->pcm_buf_host_rw_ofs % ds->buffer_bytes;
			unsigned int xfer1, xfer2;
			char *pd = &s->runtime->dma_area[buf_ofs];

			if (card->can_dma) { /* buffer wrap is handled at lower level */
				xfer1 = xfercount;
				xfer2 = 0;
			} else {
				xfer1 = min(xfercount, ds->buffer_bytes - buf_ofs);
				xfer2 = xfercount - xfer1;
			}

			if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				snd_printddd("write1, P=%d, xfer=%d, buf_ofs=%d\n",
					s->number, xfer1, buf_ofs);
				hpi_handle_error(
					hpi_outstream_write_buf(
						ds->h_stream, pd, xfer1,
						&ds->format));

				if (xfer2) {
					pd = s->runtime->dma_area;

					snd_printddd("write2, P=%d, xfer=%d, buf_ofs=%d\n",
							s->number,
							xfercount - xfer1, buf_ofs);
					hpi_handle_error(
						hpi_outstream_write_buf(
							ds->h_stream, pd,
							xfercount - xfer1,
							&ds->format));
				}
			} else {
				snd_printddd("read1, C=%d, xfer=%d\n",
					s->number, xfer1);
				hpi_handle_error(
					hpi_instream_read_buf(
						ds->h_stream,
						pd, xfer1));
				if (xfer2) {
					pd = s->runtime->dma_area;
					snd_printddd("read2, C=%d, xfer=%d\n",
						s->number, xfer2);
					hpi_handle_error(
						hpi_instream_read_buf(
							ds->h_stream,
							pd, xfer2));
				}
			}
			/* ? host_rw_ofs always ahead of elapsed_dma_ofs by preload size? */
			ds->pcm_buf_host_rw_ofs += xfercount;
			ds->pcm_buf_elapsed_dma_ofs += xfercount;
			snd_pcm_period_elapsed(s);
		}
	}

	if (!card->hpi->interrupt_mode && dpcm->respawn_timer)
		add_timer(&dpcm->timer);
}

static void snd_card_asihpi_int_task(unsigned long data)
{
	struct hpi_adapter *a = (struct hpi_adapter *)data;
	struct snd_card_asihpi *asihpi;

	WARN_ON(!a || !a->snd_card || !a->snd_card->private_data);
	asihpi = (struct snd_card_asihpi *)a->snd_card->private_data;
	if (asihpi->llmode_streampriv)
		snd_card_asihpi_timer_function(
			(unsigned long)asihpi->llmode_streampriv);
}

static void snd_card_asihpi_isr(struct hpi_adapter *a)
{
	struct snd_card_asihpi *asihpi;

	WARN_ON(!a || !a->snd_card || !a->snd_card->private_data);
	asihpi = (struct snd_card_asihpi *)a->snd_card->private_data;
	tasklet_schedule(&asihpi->t);
}

/***************************** PLAYBACK OPS ****************/
static int snd_card_asihpi_playback_ioctl(struct snd_pcm_substream *substream,
					  unsigned int cmd, void *arg)
{
	char name[16];
	snd_pcm_debug_name(substream, name, sizeof(name));
	snd_printddd(KERN_INFO "%s ioctl %d\n", name, cmd);
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_asihpi_playback_prepare(struct snd_pcm_substream *
					    substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	snd_printdd("P%d prepare\n", substream->number);

	hpi_handle_error(hpi_outstream_reset(dpcm->h_stream));
	dpcm->pcm_buf_host_rw_ofs = 0;
	dpcm->pcm_buf_dma_ofs = 0;
	dpcm->pcm_buf_elapsed_dma_ofs = 0;
	return 0;
}

static snd_pcm_uframes_t
snd_card_asihpi_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t ptr;
	char name[16];
	snd_pcm_debug_name(substream, name, sizeof(name));

	ptr = bytes_to_frames(runtime, dpcm->pcm_buf_dma_ofs  % dpcm->buffer_bytes);
	snd_printddd("%s, pointer=%ld\n", name, (unsigned long)ptr);
	return ptr;
}

static u64 snd_card_asihpi_playback_formats(struct snd_card_asihpi *asihpi,
						u32 h_stream)
{
	struct hpi_format hpi_format;
	u16 format;
	u16 err;
	u32 h_control;
	u32 sample_rate = 48000;
	u64 formats = 0;

	/* on cards without SRC, must query at valid rate,
	* maybe set by external sync
	*/
	err = hpi_mixer_get_control(asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_get_sample_rate(h_control,
				&sample_rate);

	for (format = HPI_FORMAT_PCM8_UNSIGNED;
	     format <= HPI_FORMAT_PCM24_SIGNED; format++) {
		err = hpi_format_create(&hpi_format, asihpi->out_max_chans,
					format, sample_rate, 128000, 0);
		if (!err)
			err = hpi_outstream_query_format(h_stream, &hpi_format);
		if (!err && (hpi_to_alsa_formats[format] != -1))
			formats |= pcm_format_to_bits(hpi_to_alsa_formats[format]);
	}
	return formats;
}

static int snd_card_asihpi_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_pcm_hardware snd_card_asihpi_playback;
	int err;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;

	err = hpi_outstream_open(card->hpi->adapter->index,
			      substream->number, &dpcm->h_stream);
	hpi_handle_error(err);
	if (err)
		kfree(dpcm);
	if (err == HPI_ERROR_OBJ_ALREADY_OPEN)
		return -EBUSY;
	if (err)
		return -EIO;

	/*? also check ASI5000 samplerate source
	    If external, only support external rate.
	    If internal and other stream playing, can't switch
	*/

	setup_timer(&dpcm->timer, snd_card_asihpi_timer_function,
		    (unsigned long) dpcm);
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_asihpi_runtime_free;

	memset(&snd_card_asihpi_playback, 0, sizeof(snd_card_asihpi_playback));
	if (!card->hpi->interrupt_mode) {
		snd_card_asihpi_playback.buffer_bytes_max = BUFFER_BYTES_MAX;
		snd_card_asihpi_playback.period_bytes_min = PERIOD_BYTES_MIN;
		snd_card_asihpi_playback.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN;
		snd_card_asihpi_playback.periods_min = PERIODS_MIN;
		snd_card_asihpi_playback.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN;
	} else {
		size_t pbmin = card->update_interval_frames *
			card->out_max_chans;
		snd_card_asihpi_playback.buffer_bytes_max = BUFFER_BYTES_MAX;
		snd_card_asihpi_playback.period_bytes_min = pbmin;
		snd_card_asihpi_playback.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN;
		snd_card_asihpi_playback.periods_min = PERIODS_MIN;
		snd_card_asihpi_playback.periods_max = BUFFER_BYTES_MAX / pbmin;
	}

	/* snd_card_asihpi_playback.fifo_size = 0; */
	snd_card_asihpi_playback.channels_max = card->out_max_chans;
	snd_card_asihpi_playback.channels_min = card->out_min_chans;
	snd_card_asihpi_playback.formats =
			snd_card_asihpi_playback_formats(card, dpcm->h_stream);

	snd_card_asihpi_pcm_samplerates(card,  &snd_card_asihpi_playback);

	snd_card_asihpi_playback.info = SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_DOUBLE |
					SNDRV_PCM_INFO_BATCH |
					SNDRV_PCM_INFO_BLOCK_TRANSFER |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID;

	if (card->support_grouping) {
		snd_card_asihpi_playback.info |= SNDRV_PCM_INFO_SYNC_START;
		snd_pcm_set_sync(substream);
	}

	/* struct is copied, so can create initializer dynamically */
	runtime->hw = snd_card_asihpi_playback;

	if (card->can_dma)
		err = snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	if (err < 0)
		return err;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames);

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames, UINT_MAX);

	snd_printdd("playback open\n");

	return 0;
}

static int snd_card_asihpi_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	hpi_handle_error(hpi_outstream_close(dpcm->h_stream));
	snd_printdd("playback close\n");

	return 0;
}

static const struct snd_pcm_ops snd_card_asihpi_playback_mmap_ops = {
	.open = snd_card_asihpi_playback_open,
	.close = snd_card_asihpi_playback_close,
	.ioctl = snd_card_asihpi_playback_ioctl,
	.hw_params = snd_card_asihpi_pcm_hw_params,
	.hw_free = snd_card_asihpi_hw_free,
	.prepare = snd_card_asihpi_playback_prepare,
	.trigger = snd_card_asihpi_trigger,
	.pointer = snd_card_asihpi_playback_pointer,
};

/***************************** CAPTURE OPS ****************/
static snd_pcm_uframes_t
snd_card_asihpi_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	char name[16];
	snd_pcm_debug_name(substream, name, sizeof(name));

	snd_printddd("%s, pointer=%d\n", name, dpcm->pcm_buf_dma_ofs);
	/* NOTE Unlike playback can't use actual samples_played
		for the capture position, because those samples aren't yet in
		the local buffer available for reading.
	*/
	return bytes_to_frames(runtime, dpcm->pcm_buf_dma_ofs % dpcm->buffer_bytes);
}

static int snd_card_asihpi_capture_ioctl(struct snd_pcm_substream *substream,
					 unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_asihpi_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	hpi_handle_error(hpi_instream_reset(dpcm->h_stream));
	dpcm->pcm_buf_host_rw_ofs = 0;
	dpcm->pcm_buf_dma_ofs = 0;
	dpcm->pcm_buf_elapsed_dma_ofs = 0;

	snd_printdd("Capture Prepare %d\n", substream->number);
	return 0;
}

static u64 snd_card_asihpi_capture_formats(struct snd_card_asihpi *asihpi,
					u32 h_stream)
{
  struct hpi_format hpi_format;
	u16 format;
	u16 err;
	u32 h_control;
	u32 sample_rate = 48000;
	u64 formats = 0;

	/* on cards without SRC, must query at valid rate,
		maybe set by external sync */
	err = hpi_mixer_get_control(asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_get_sample_rate(h_control,
			&sample_rate);

	for (format = HPI_FORMAT_PCM8_UNSIGNED;
		format <= HPI_FORMAT_PCM24_SIGNED; format++) {

		err = hpi_format_create(&hpi_format, asihpi->in_max_chans,
					format, sample_rate, 128000, 0);
		if (!err)
			err = hpi_instream_query_format(h_stream, &hpi_format);
		if (!err && (hpi_to_alsa_formats[format] != -1))
			formats |= pcm_format_to_bits(hpi_to_alsa_formats[format]);
	}
	return formats;
}

static int snd_card_asihpi_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_card_asihpi_pcm *dpcm;
	struct snd_pcm_hardware snd_card_asihpi_capture;
	int err;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;

	snd_printdd("capture open adapter %d stream %d\n",
			card->hpi->adapter->index, substream->number);

	err = hpi_handle_error(
	    hpi_instream_open(card->hpi->adapter->index,
			     substream->number, &dpcm->h_stream));
	if (err)
		kfree(dpcm);
	if (err == HPI_ERROR_OBJ_ALREADY_OPEN)
		return -EBUSY;
	if (err)
		return -EIO;

	setup_timer(&dpcm->timer, snd_card_asihpi_timer_function,
		    (unsigned long) dpcm);
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_asihpi_runtime_free;

	memset(&snd_card_asihpi_capture, 0, sizeof(snd_card_asihpi_capture));
	if (!card->hpi->interrupt_mode) {
		snd_card_asihpi_capture.buffer_bytes_max = BUFFER_BYTES_MAX;
		snd_card_asihpi_capture.period_bytes_min = PERIOD_BYTES_MIN;
		snd_card_asihpi_capture.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN;
		snd_card_asihpi_capture.periods_min = PERIODS_MIN;
		snd_card_asihpi_capture.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN;
	} else {
		size_t pbmin = card->update_interval_frames *
			card->out_max_chans;
		snd_card_asihpi_capture.buffer_bytes_max = BUFFER_BYTES_MAX;
		snd_card_asihpi_capture.period_bytes_min = pbmin;
		snd_card_asihpi_capture.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN;
		snd_card_asihpi_capture.periods_min = PERIODS_MIN;
		snd_card_asihpi_capture.periods_max = BUFFER_BYTES_MAX / pbmin;
	}
	/* snd_card_asihpi_capture.fifo_size = 0; */
	snd_card_asihpi_capture.channels_max = card->in_max_chans;
	snd_card_asihpi_capture.channels_min = card->in_min_chans;
	snd_card_asihpi_capture.formats =
		snd_card_asihpi_capture_formats(card, dpcm->h_stream);
	snd_card_asihpi_pcm_samplerates(card,  &snd_card_asihpi_capture);
	snd_card_asihpi_capture.info = SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID;

	if (card->support_grouping)
		snd_card_asihpi_capture.info |= SNDRV_PCM_INFO_SYNC_START;

	runtime->hw = snd_card_asihpi_capture;

	if (card->can_dma)
		err = snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	if (err < 0)
		return err;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames, UINT_MAX);

	snd_pcm_set_sync(substream);

	return 0;
}

static int snd_card_asihpi_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_card_asihpi_pcm *dpcm = substream->runtime->private_data;

	hpi_handle_error(hpi_instream_close(dpcm->h_stream));
	return 0;
}

static const struct snd_pcm_ops snd_card_asihpi_capture_mmap_ops = {
	.open = snd_card_asihpi_capture_open,
	.close = snd_card_asihpi_capture_close,
	.ioctl = snd_card_asihpi_capture_ioctl,
	.hw_params = snd_card_asihpi_pcm_hw_params,
	.hw_free = snd_card_asihpi_hw_free,
	.prepare = snd_card_asihpi_capture_prepare,
	.trigger = snd_card_asihpi_trigger,
	.pointer = snd_card_asihpi_capture_pointer,
};

static int snd_card_asihpi_pcm_new(struct snd_card_asihpi *asihpi, int device)
{
	struct snd_pcm *pcm;
	int err;
	u16 num_instreams, num_outstreams, x16;
	u32 x32;

	err = hpi_adapter_get_info(asihpi->hpi->adapter->index,
			&num_outstreams, &num_instreams,
			&x16, &x32, &x16);

	err = snd_pcm_new(asihpi->card, "Asihpi PCM", device,
			num_outstreams,	num_instreams, &pcm);
	if (err < 0)
		return err;

	/* pointer to ops struct is stored, dont change ops afterwards! */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_card_asihpi_playback_mmap_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_card_asihpi_capture_mmap_ops);

	pcm->private_data = asihpi;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Asihpi PCM");

	/*? do we want to emulate MMAP for non-BBM cards?
	Jack doesn't work with ALSAs MMAP emulation - WHY NOT? */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						snd_dma_pci_data(asihpi->pci),
						64*1024, BUFFER_BYTES_MAX);

	return 0;
}

/***************************** MIXER CONTROLS ****************/
struct hpi_control {
	u32 h_control;
	u16 control_type;
	u16 src_node_type;
	u16 src_node_index;
	u16 dst_node_type;
	u16 dst_node_index;
	u16 band;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN]; /* copied to snd_ctl_elem_id.name[44]; */
};

static const char * const asihpi_tuner_band_names[] = {
	"invalid",
	"AM",
	"FM mono",
	"TV NTSC-M",
	"FM stereo",
	"AUX",
	"TV PAL BG",
	"TV PAL I",
	"TV PAL DK",
	"TV SECAM",
	"TV DAB",
};
/* Number of strings must match the enumerations for HPI_TUNER_BAND in hpi.h */
compile_time_assert(
	(ARRAY_SIZE(asihpi_tuner_band_names) ==
		(HPI_TUNER_BAND_LAST+1)),
	assert_tuner_band_names_size);

static const char * const asihpi_src_names[] = {
	"no source",
	"PCM",
	"Line",
	"Digital",
	"Tuner",
	"RF",
	"Clock",
	"Bitstream",
	"Mic",
	"Net",
	"Analog",
	"Adapter",
	"RTP",
	"Internal",
	"AVB",
	"BLU-Link"
};
/* Number of strings must match the enumerations for HPI_SOURCENODES in hpi.h */
compile_time_assert(
	(ARRAY_SIZE(asihpi_src_names) ==
		(HPI_SOURCENODE_LAST_INDEX-HPI_SOURCENODE_NONE+1)),
	assert_src_names_size);

static const char * const asihpi_dst_names[] = {
	"no destination",
	"PCM",
	"Line",
	"Digital",
	"RF",
	"Speaker",
	"Net",
	"Analog",
	"RTP",
	"AVB",
	"Internal",
	"BLU-Link"
};
/* Number of strings must match the enumerations for HPI_DESTNODES in hpi.h */
compile_time_assert(
	(ARRAY_SIZE(asihpi_dst_names) ==
		(HPI_DESTNODE_LAST_INDEX-HPI_DESTNODE_NONE+1)),
	assert_dst_names_size);

static inline int ctl_add(struct snd_card *card, struct snd_kcontrol_new *ctl,
				struct snd_card_asihpi *asihpi)
{
	int err;

	err = snd_ctl_add(card, snd_ctl_new1(ctl, asihpi));
	if (err < 0)
		return err;
	else if (mixer_dump)
		dev_info(&asihpi->pci->dev, "added %s(%d)\n", ctl->name, ctl->index);

	return 0;
}

/* Convert HPI control name and location into ALSA control name */
static void asihpi_ctl_init(struct snd_kcontrol_new *snd_control,
				struct hpi_control *hpi_ctl,
				char *name)
{
	char *dir;
	memset(snd_control, 0, sizeof(*snd_control));
	snd_control->name = hpi_ctl->name;
	snd_control->private_value = hpi_ctl->h_control;
	snd_control->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	snd_control->index = 0;

	if (hpi_ctl->src_node_type + HPI_SOURCENODE_NONE == HPI_SOURCENODE_CLOCK_SOURCE)
		dir = ""; /* clock is neither capture nor playback */
	else if (hpi_ctl->dst_node_type + HPI_DESTNODE_NONE == HPI_DESTNODE_ISTREAM)
		dir = "Capture ";  /* On or towards a PCM capture destination*/
	else if ((hpi_ctl->src_node_type + HPI_SOURCENODE_NONE != HPI_SOURCENODE_OSTREAM) &&
		(!hpi_ctl->dst_node_type))
		dir = "Capture "; /* On a source node that is not PCM playback */
	else if (hpi_ctl->src_node_type &&
		(hpi_ctl->src_node_type + HPI_SOURCENODE_NONE != HPI_SOURCENODE_OSTREAM) &&
		(hpi_ctl->dst_node_type))
		dir = "Monitor Playback "; /* Between an input and an output */
	else
		dir = "Playback "; /* PCM Playback source, or  output node */

	if (hpi_ctl->src_node_type && hpi_ctl->dst_node_type)
		sprintf(hpi_ctl->name, "%s %d %s %d %s%s",
			asihpi_src_names[hpi_ctl->src_node_type],
			hpi_ctl->src_node_index,
			asihpi_dst_names[hpi_ctl->dst_node_type],
			hpi_ctl->dst_node_index,
			dir, name);
	else if (hpi_ctl->dst_node_type) {
		sprintf(hpi_ctl->name, "%s %d %s%s",
		asihpi_dst_names[hpi_ctl->dst_node_type],
		hpi_ctl->dst_node_index,
		dir, name);
	} else {
		sprintf(hpi_ctl->name, "%s %d %s%s",
		asihpi_src_names[hpi_ctl->src_node_type],
		hpi_ctl->src_node_index,
		dir, name);
	}
	/* printk(KERN_INFO "Adding %s %d to %d ",  hpi_ctl->name,
		hpi_ctl->wSrcNodeType, hpi_ctl->wDstNodeType); */
}

/*------------------------------------------------------------
   Volume controls
 ------------------------------------------------------------*/
#define VOL_STEP_mB 1
static int snd_asihpi_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u32 count;
	u16 err;
	/* native gains are in millibels */
	short min_gain_mB;
	short max_gain_mB;
	short step_gain_mB;

	err = hpi_volume_query_range(h_control,
			&min_gain_mB, &max_gain_mB, &step_gain_mB);
	if (err) {
		max_gain_mB = 0;
		min_gain_mB = -10000;
		step_gain_mB = VOL_STEP_mB;
	}

	err = hpi_meter_query_channels(h_control, &count);
	if (err)
		count = HPI_MAX_CHANNELS;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = count;
	uinfo->value.integer.min = min_gain_mB / VOL_STEP_mB;
	uinfo->value.integer.max = max_gain_mB / VOL_STEP_mB;
	uinfo->value.integer.step = step_gain_mB / VOL_STEP_mB;
	return 0;
}

static int snd_asihpi_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	short an_gain_mB[HPI_MAX_CHANNELS];

	hpi_handle_error(hpi_volume_get_gain(h_control, an_gain_mB));
	ucontrol->value.integer.value[0] = an_gain_mB[0] / VOL_STEP_mB;
	ucontrol->value.integer.value[1] = an_gain_mB[1] / VOL_STEP_mB;

	return 0;
}

static int snd_asihpi_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int change;
	u32 h_control = kcontrol->private_value;
	short an_gain_mB[HPI_MAX_CHANNELS];

	an_gain_mB[0] =
	    (ucontrol->value.integer.value[0]) * VOL_STEP_mB;
	an_gain_mB[1] =
	    (ucontrol->value.integer.value[1]) * VOL_STEP_mB;
	/*  change = asihpi->mixer_volume[addr][0] != left ||
	   asihpi->mixer_volume[addr][1] != right;
	 */
	change = 1;
	hpi_handle_error(hpi_volume_set_gain(h_control, an_gain_mB));
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_100, -10000, VOL_STEP_mB, 0);

#define snd_asihpi_volume_mute_info	snd_ctl_boolean_mono_info

static int snd_asihpi_volume_mute_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 mute;

	hpi_handle_error(hpi_volume_get_mute(h_control, &mute));
	ucontrol->value.integer.value[0] = mute ? 0 : 1;

	return 0;
}

static int snd_asihpi_volume_mute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	int change = 1;
	/* HPI currently only supports all or none muting of multichannel volume
	ALSA Switch element has opposite sense to HPI mute: on==unmuted, off=muted
	*/
	int mute =  ucontrol->value.integer.value[0] ? 0 : HPI_BITMASK_ALL_CHANNELS;
	hpi_handle_error(hpi_volume_set_mute(h_control, mute));
	return change;
}

static int snd_asihpi_volume_add(struct snd_card_asihpi *asihpi,
				 struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;
	int err;
	u32 mute;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Volume");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ;
	snd_control.info = snd_asihpi_volume_info;
	snd_control.get = snd_asihpi_volume_get;
	snd_control.put = snd_asihpi_volume_put;
	snd_control.tlv.p = db_scale_100;

	err = ctl_add(card, &snd_control, asihpi);
	if (err)
		return err;

	if (hpi_volume_get_mute(hpi_ctl->h_control, &mute) == 0) {
		asihpi_ctl_init(&snd_control, hpi_ctl, "Switch");
		snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
		snd_control.info = snd_asihpi_volume_mute_info;
		snd_control.get = snd_asihpi_volume_mute_get;
		snd_control.put = snd_asihpi_volume_mute_put;
		err = ctl_add(card, &snd_control, asihpi);
	}
	return err;
}

/*------------------------------------------------------------
   Level controls
 ------------------------------------------------------------*/
static int snd_asihpi_level_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u16 err;
	short min_gain_mB;
	short max_gain_mB;
	short step_gain_mB;

	err =
	    hpi_level_query_range(h_control, &min_gain_mB,
			       &max_gain_mB, &step_gain_mB);
	if (err) {
		max_gain_mB = 2400;
		min_gain_mB = -1000;
		step_gain_mB = 100;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = min_gain_mB / HPI_UNITS_PER_dB;
	uinfo->value.integer.max = max_gain_mB / HPI_UNITS_PER_dB;
	uinfo->value.integer.step = step_gain_mB / HPI_UNITS_PER_dB;
	return 0;
}

static int snd_asihpi_level_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	short an_gain_mB[HPI_MAX_CHANNELS];

	hpi_handle_error(hpi_level_get_gain(h_control, an_gain_mB));
	ucontrol->value.integer.value[0] =
	    an_gain_mB[0] / HPI_UNITS_PER_dB;
	ucontrol->value.integer.value[1] =
	    an_gain_mB[1] / HPI_UNITS_PER_dB;

	return 0;
}

static int snd_asihpi_level_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int change;
	u32 h_control = kcontrol->private_value;
	short an_gain_mB[HPI_MAX_CHANNELS];

	an_gain_mB[0] =
	    (ucontrol->value.integer.value[0]) * HPI_UNITS_PER_dB;
	an_gain_mB[1] =
	    (ucontrol->value.integer.value[1]) * HPI_UNITS_PER_dB;
	/*  change = asihpi->mixer_level[addr][0] != left ||
	   asihpi->mixer_level[addr][1] != right;
	 */
	change = 1;
	hpi_handle_error(hpi_level_set_gain(h_control, an_gain_mB));
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_level, -1000, 100, 0);

static int snd_asihpi_level_add(struct snd_card_asihpi *asihpi,
				struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	/* can't use 'volume' cos some nodes have volume as well */
	asihpi_ctl_init(&snd_control, hpi_ctl, "Level");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ;
	snd_control.info = snd_asihpi_level_info;
	snd_control.get = snd_asihpi_level_get;
	snd_control.put = snd_asihpi_level_put;
	snd_control.tlv.p = db_scale_level;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   AESEBU controls
 ------------------------------------------------------------*/

/* AESEBU format */
static const char * const asihpi_aesebu_format_names[] = {
	"N/A", "S/PDIF", "AES/EBU" };

static int snd_asihpi_aesebu_format_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, 3, asihpi_aesebu_format_names);
}

static int snd_asihpi_aesebu_format_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol,
			u16 (*func)(u32, u16 *))
{
	u32 h_control = kcontrol->private_value;
	u16 source, err;

	err = func(h_control, &source);

	/* default to N/A */
	ucontrol->value.enumerated.item[0] = 0;
	/* return success but set the control to N/A */
	if (err)
		return 0;
	if (source == HPI_AESEBU_FORMAT_SPDIF)
		ucontrol->value.enumerated.item[0] = 1;
	if (source == HPI_AESEBU_FORMAT_AESEBU)
		ucontrol->value.enumerated.item[0] = 2;

	return 0;
}

static int snd_asihpi_aesebu_format_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol,
			 u16 (*func)(u32, u16))
{
	u32 h_control = kcontrol->private_value;

	/* default to S/PDIF */
	u16 source = HPI_AESEBU_FORMAT_SPDIF;

	if (ucontrol->value.enumerated.item[0] == 1)
		source = HPI_AESEBU_FORMAT_SPDIF;
	if (ucontrol->value.enumerated.item[0] == 2)
		source = HPI_AESEBU_FORMAT_AESEBU;

	if (func(h_control, source) != 0)
		return -EINVAL;

	return 1;
}

static int snd_asihpi_aesebu_rx_format_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_get(kcontrol, ucontrol,
					hpi_aesebu_receiver_get_format);
}

static int snd_asihpi_aesebu_rx_format_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_put(kcontrol, ucontrol,
					hpi_aesebu_receiver_set_format);
}

static int snd_asihpi_aesebu_rxstatus_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;

	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0X1F;
	uinfo->value.integer.step = 1;

	return 0;
}

static int snd_asihpi_aesebu_rxstatus_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {

	u32 h_control = kcontrol->private_value;
	u16 status;

	hpi_handle_error(hpi_aesebu_receiver_get_error_status(
					 h_control, &status));
	ucontrol->value.integer.value[0] = status;
	return 0;
}

static int snd_asihpi_aesebu_rx_add(struct snd_card_asihpi *asihpi,
				    struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Format");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_aesebu_format_info;
	snd_control.get = snd_asihpi_aesebu_rx_format_get;
	snd_control.put = snd_asihpi_aesebu_rx_format_put;


	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Status");
	snd_control.access =
	    SNDRV_CTL_ELEM_ACCESS_VOLATILE | SNDRV_CTL_ELEM_ACCESS_READ;
	snd_control.info = snd_asihpi_aesebu_rxstatus_info;
	snd_control.get = snd_asihpi_aesebu_rxstatus_get;

	return ctl_add(card, &snd_control, asihpi);
}

static int snd_asihpi_aesebu_tx_format_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_get(kcontrol, ucontrol,
					hpi_aesebu_transmitter_get_format);
}

static int snd_asihpi_aesebu_tx_format_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_put(kcontrol, ucontrol,
					hpi_aesebu_transmitter_set_format);
}


static int snd_asihpi_aesebu_tx_add(struct snd_card_asihpi *asihpi,
				    struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Format");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_aesebu_format_info;
	snd_control.get = snd_asihpi_aesebu_tx_format_get;
	snd_control.put = snd_asihpi_aesebu_tx_format_put;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   Tuner controls
 ------------------------------------------------------------*/

/* Gain */

static int snd_asihpi_tuner_gain_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u16 err;
	short idx;
	u16 gain_range[3];

	for (idx = 0; idx < 3; idx++) {
		err = hpi_tuner_query_gain(h_control,
					  idx, &gain_range[idx]);
		if (err != 0)
			return err;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = ((int)gain_range[0]) / HPI_UNITS_PER_dB;
	uinfo->value.integer.max = ((int)gain_range[1]) / HPI_UNITS_PER_dB;
	uinfo->value.integer.step = ((int) gain_range[2]) / HPI_UNITS_PER_dB;
	return 0;
}

static int snd_asihpi_tuner_gain_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	/*
	struct snd_card_asihpi *asihpi = snd_kcontrol_chip(kcontrol);
	*/
	u32 h_control = kcontrol->private_value;
	short gain;

	hpi_handle_error(hpi_tuner_get_gain(h_control, &gain));
	ucontrol->value.integer.value[0] = gain / HPI_UNITS_PER_dB;

	return 0;
}

static int snd_asihpi_tuner_gain_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	/*
	struct snd_card_asihpi *asihpi = snd_kcontrol_chip(kcontrol);
	*/
	u32 h_control = kcontrol->private_value;
	short gain;

	gain = (ucontrol->value.integer.value[0]) * HPI_UNITS_PER_dB;
	hpi_handle_error(hpi_tuner_set_gain(h_control, gain));

	return 1;
}

/* Band  */

static int asihpi_tuner_band_query(struct snd_kcontrol *kcontrol,
					u16 *band_list, u32 len) {
	u32 h_control = kcontrol->private_value;
	u16 err = 0;
	u32 i;

	for (i = 0; i < len; i++) {
		err = hpi_tuner_query_band(
				h_control, i, &band_list[i]);
		if (err != 0)
			break;
	}

	if (err && (err != HPI_ERROR_INVALID_OBJ_INDEX))
		return -EIO;

	return i;
}

static int snd_asihpi_tuner_band_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	u16 tuner_bands[HPI_TUNER_BAND_LAST];
	int num_bands = 0;

	num_bands = asihpi_tuner_band_query(kcontrol, tuner_bands,
				HPI_TUNER_BAND_LAST);

	if (num_bands < 0)
		return num_bands;

	return snd_ctl_enum_info(uinfo, 1, num_bands, asihpi_tuner_band_names);
}

static int snd_asihpi_tuner_band_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	/*
	struct snd_card_asihpi *asihpi = snd_kcontrol_chip(kcontrol);
	*/
	u16 band, idx;
	u16 tuner_bands[HPI_TUNER_BAND_LAST];
	u32 num_bands = 0;

	num_bands = asihpi_tuner_band_query(kcontrol, tuner_bands,
				HPI_TUNER_BAND_LAST);

	hpi_handle_error(hpi_tuner_get_band(h_control, &band));

	ucontrol->value.enumerated.item[0] = -1;
	for (idx = 0; idx < HPI_TUNER_BAND_LAST; idx++)
		if (tuner_bands[idx] == band) {
			ucontrol->value.enumerated.item[0] = idx;
			break;
		}

	return 0;
}

static int snd_asihpi_tuner_band_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	/*
	struct snd_card_asihpi *asihpi = snd_kcontrol_chip(kcontrol);
	*/
	u32 h_control = kcontrol->private_value;
	unsigned int idx;
	u16 band;
	u16 tuner_bands[HPI_TUNER_BAND_LAST];
	u32 num_bands = 0;

	num_bands = asihpi_tuner_band_query(kcontrol, tuner_bands,
			HPI_TUNER_BAND_LAST);

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= ARRAY_SIZE(tuner_bands))
		idx = ARRAY_SIZE(tuner_bands) - 1;
	band = tuner_bands[idx];
	hpi_handle_error(hpi_tuner_set_band(h_control, band));

	return 1;
}

/* Freq */

static int snd_asihpi_tuner_freq_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u16 err;
	u16 tuner_bands[HPI_TUNER_BAND_LAST];
	u16 num_bands = 0, band_iter, idx;
	u32 freq_range[3], temp_freq_range[3];

	num_bands = asihpi_tuner_band_query(kcontrol, tuner_bands,
			HPI_TUNER_BAND_LAST);

	freq_range[0] = INT_MAX;
	freq_range[1] = 0;
	freq_range[2] = INT_MAX;

	for (band_iter = 0; band_iter < num_bands; band_iter++) {
		for (idx = 0; idx < 3; idx++) {
			err = hpi_tuner_query_frequency(h_control,
				idx, tuner_bands[band_iter],
				&temp_freq_range[idx]);
			if (err != 0)
				return err;
		}

		/* skip band with bogus stepping */
		if (temp_freq_range[2] <= 0)
			continue;

		if (temp_freq_range[0] < freq_range[0])
			freq_range[0] = temp_freq_range[0];
		if (temp_freq_range[1] > freq_range[1])
			freq_range[1] = temp_freq_range[1];
		if (temp_freq_range[2] < freq_range[2])
			freq_range[2] = temp_freq_range[2];
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = ((int)freq_range[0]);
	uinfo->value.integer.max = ((int)freq_range[1]);
	uinfo->value.integer.step = ((int)freq_range[2]);
	return 0;
}

static int snd_asihpi_tuner_freq_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 freq;

	hpi_handle_error(hpi_tuner_get_frequency(h_control, &freq));
	ucontrol->value.integer.value[0] = freq;

	return 0;
}

static int snd_asihpi_tuner_freq_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 freq;

	freq = ucontrol->value.integer.value[0];
	hpi_handle_error(hpi_tuner_set_frequency(h_control, freq));

	return 1;
}

/* Tuner control group initializer  */
static int snd_asihpi_tuner_add(struct snd_card_asihpi *asihpi,
				struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	snd_control.private_value = hpi_ctl->h_control;
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;

	if (!hpi_tuner_get_gain(hpi_ctl->h_control, NULL)) {
		asihpi_ctl_init(&snd_control, hpi_ctl, "Gain");
		snd_control.info = snd_asihpi_tuner_gain_info;
		snd_control.get = snd_asihpi_tuner_gain_get;
		snd_control.put = snd_asihpi_tuner_gain_put;

		if (ctl_add(card, &snd_control, asihpi) < 0)
			return -EINVAL;
	}

	asihpi_ctl_init(&snd_control, hpi_ctl, "Band");
	snd_control.info = snd_asihpi_tuner_band_info;
	snd_control.get = snd_asihpi_tuner_band_get;
	snd_control.put = snd_asihpi_tuner_band_put;

	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Freq");
	snd_control.info = snd_asihpi_tuner_freq_info;
	snd_control.get = snd_asihpi_tuner_freq_get;
	snd_control.put = snd_asihpi_tuner_freq_put;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   Meter controls
 ------------------------------------------------------------*/
static int snd_asihpi_meter_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u32 count;
	u16 err;
	err = hpi_meter_query_channels(h_control, &count);
	if (err)
		count = HPI_MAX_CHANNELS;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x7FFFFFFF;
	return 0;
}

/* linear values for 10dB steps */
static int log2lin[] = {
	0x7FFFFFFF, /* 0dB */
	679093956,
	214748365,
	 67909396,
	 21474837,
	  6790940,
	  2147484, /* -60dB */
	   679094,
	   214748, /* -80 */
	    67909,
	    21475, /* -100 */
	     6791,
	     2147,
	      679,
	      214,
	       68,
	       21,
		7,
		2
};

static int snd_asihpi_meter_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	short an_gain_mB[HPI_MAX_CHANNELS], i;
	u16 err;

	err = hpi_meter_get_peak(h_control, an_gain_mB);

	for (i = 0; i < HPI_MAX_CHANNELS; i++) {
		if (err) {
			ucontrol->value.integer.value[i] = 0;
		} else if (an_gain_mB[i] >= 0) {
			ucontrol->value.integer.value[i] =
				an_gain_mB[i] << 16;
		} else {
			/* -ve is log value in millibels < -60dB,
			* convert to (roughly!) linear,
			*/
			ucontrol->value.integer.value[i] =
					log2lin[an_gain_mB[i] / -1000];
		}
	}
	return 0;
}

static int snd_asihpi_meter_add(struct snd_card_asihpi *asihpi,
				struct hpi_control *hpi_ctl, int subidx)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Meter");
	snd_control.access =
	    SNDRV_CTL_ELEM_ACCESS_VOLATILE | SNDRV_CTL_ELEM_ACCESS_READ;
	snd_control.info = snd_asihpi_meter_info;
	snd_control.get = snd_asihpi_meter_get;

	snd_control.index = subidx;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   Multiplexer controls
 ------------------------------------------------------------*/
static int snd_card_asihpi_mux_count_sources(struct snd_kcontrol *snd_control)
{
	u32 h_control = snd_control->private_value;
	struct hpi_control hpi_ctl;
	int s, err;
	for (s = 0; s < 32; s++) {
		err = hpi_multiplexer_query_source(h_control, s,
						  &hpi_ctl.
						  src_node_type,
						  &hpi_ctl.
						  src_node_index);
		if (err)
			break;
	}
	return s;
}

static int snd_asihpi_mux_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	int err;
	u16 src_node_type, src_node_index;
	u32 h_control = kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items =
	    snd_card_asihpi_mux_count_sources(kcontrol);

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;

	err =
	    hpi_multiplexer_query_source(h_control,
					uinfo->value.enumerated.item,
					&src_node_type, &src_node_index);

	sprintf(uinfo->value.enumerated.name, "%s %d",
		asihpi_src_names[src_node_type - HPI_SOURCENODE_NONE],
		src_node_index);
	return 0;
}

static int snd_asihpi_mux_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u16 source_type, source_index;
	u16 src_node_type, src_node_index;
	int s;

	hpi_handle_error(hpi_multiplexer_get_source(h_control,
				&source_type, &source_index));
	/* Should cache this search result! */
	for (s = 0; s < 256; s++) {
		if (hpi_multiplexer_query_source(h_control, s,
					    &src_node_type, &src_node_index))
			break;

		if ((source_type == src_node_type)
		    && (source_index == src_node_index)) {
			ucontrol->value.enumerated.item[0] = s;
			return 0;
		}
	}
	snd_printd(KERN_WARNING
		"Control %x failed to match mux source %hu %hu\n",
		h_control, source_type, source_index);
	ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int snd_asihpi_mux_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int change;
	u32 h_control = kcontrol->private_value;
	u16 source_type, source_index;
	u16 e;

	change = 1;

	e = hpi_multiplexer_query_source(h_control,
				    ucontrol->value.enumerated.item[0],
				    &source_type, &source_index);
	if (!e)
		hpi_handle_error(
			hpi_multiplexer_set_source(h_control,
						source_type, source_index));
	return change;
}


static int  snd_asihpi_mux_add(struct snd_card_asihpi *asihpi,
			       struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Route");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_mux_info;
	snd_control.get = snd_asihpi_mux_get;
	snd_control.put = snd_asihpi_mux_put;

	return ctl_add(card, &snd_control, asihpi);

}

/*------------------------------------------------------------
   Channel mode controls
 ------------------------------------------------------------*/
static int snd_asihpi_cmode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static const char * const mode_names[HPI_CHANNEL_MODE_LAST + 1] = {
		"invalid",
		"Normal", "Swap",
		"From Left", "From Right",
		"To Left", "To Right"
	};

	u32 h_control = kcontrol->private_value;
	u16 mode;
	int i;
	const char *mapped_names[6];
	int valid_modes = 0;

	/* HPI channel mode values can be from 1 to 6
	Some adapters only support a contiguous subset
	*/
	for (i = 0; i < HPI_CHANNEL_MODE_LAST; i++)
		if (!hpi_channel_mode_query_mode(
			h_control, i, &mode)) {
			mapped_names[valid_modes] = mode_names[mode];
			valid_modes++;
			}

	if (!valid_modes)
		return -EINVAL;

	return snd_ctl_enum_info(uinfo, 1, valid_modes, mapped_names);
}

static int snd_asihpi_cmode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u16 mode;

	if (hpi_channel_mode_get(h_control, &mode))
		mode = 1;

	ucontrol->value.enumerated.item[0] = mode - 1;

	return 0;
}

static int snd_asihpi_cmode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int change;
	u32 h_control = kcontrol->private_value;

	change = 1;

	hpi_handle_error(hpi_channel_mode_set(h_control,
			   ucontrol->value.enumerated.item[0] + 1));
	return change;
}


static int snd_asihpi_cmode_add(struct snd_card_asihpi *asihpi,
				struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Mode");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_cmode_info;
	snd_control.get = snd_asihpi_cmode_get;
	snd_control.put = snd_asihpi_cmode_put;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   Sampleclock source  controls
 ------------------------------------------------------------*/
static const char * const sampleclock_sources[] = {
	"N/A", "Local PLL", "Digital Sync", "Word External", "Word Header",
	"SMPTE", "Digital1", "Auto", "Network", "Invalid",
	"Prev Module", "BLU-Link",
	"Digital2", "Digital3", "Digital4", "Digital5",
	"Digital6", "Digital7", "Digital8"};

	/* Number of strings must match expected enumerated values */
	compile_time_assert(
		(ARRAY_SIZE(sampleclock_sources) == MAX_CLOCKSOURCES),
		assert_sampleclock_sources_size);

static int snd_asihpi_clksrc_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct snd_card_asihpi *asihpi =
			(struct snd_card_asihpi *)(kcontrol->private_data);
	struct clk_cache *clkcache = &asihpi->cc;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = clkcache->count;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;

	strcpy(uinfo->value.enumerated.name,
	       clkcache->s[uinfo->value.enumerated.item].name);
	return 0;
}

static int snd_asihpi_clksrc_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_card_asihpi *asihpi =
			(struct snd_card_asihpi *)(kcontrol->private_data);
	struct clk_cache *clkcache = &asihpi->cc;
	u32 h_control = kcontrol->private_value;
	u16 source, srcindex = 0;
	int i;

	ucontrol->value.enumerated.item[0] = 0;
	if (hpi_sample_clock_get_source(h_control, &source))
		source = 0;

	if (source == HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT)
		if (hpi_sample_clock_get_source_index(h_control, &srcindex))
			srcindex = 0;

	for (i = 0; i < clkcache->count; i++)
		if ((clkcache->s[i].source == source) &&
			(clkcache->s[i].index == srcindex))
			break;

	ucontrol->value.enumerated.item[0] = i;

	return 0;
}

static int snd_asihpi_clksrc_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_card_asihpi *asihpi =
			(struct snd_card_asihpi *)(kcontrol->private_data);
	struct clk_cache *clkcache = &asihpi->cc;
	unsigned int item;
	int change;
	u32 h_control = kcontrol->private_value;

	change = 1;
	item = ucontrol->value.enumerated.item[0];
	if (item >= clkcache->count)
		item = clkcache->count-1;

	hpi_handle_error(hpi_sample_clock_set_source(
				h_control, clkcache->s[item].source));

	if (clkcache->s[item].source == HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT)
		hpi_handle_error(hpi_sample_clock_set_source_index(
				h_control, clkcache->s[item].index));
	return change;
}

/*------------------------------------------------------------
   Clkrate controls
 ------------------------------------------------------------*/
/* Need to change this to enumerated control with list of rates */
static int snd_asihpi_clklocal_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 8000;
	uinfo->value.integer.max = 192000;
	uinfo->value.integer.step = 100;

	return 0;
}

static int snd_asihpi_clklocal_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 rate;
	u16 e;

	e = hpi_sample_clock_get_local_rate(h_control, &rate);
	if (!e)
		ucontrol->value.integer.value[0] = rate;
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int snd_asihpi_clklocal_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	int change;
	u32 h_control = kcontrol->private_value;

	/*  change = asihpi->mixer_clkrate[addr][0] != left ||
	   asihpi->mixer_clkrate[addr][1] != right;
	 */
	change = 1;
	hpi_handle_error(hpi_sample_clock_set_local_rate(h_control,
				      ucontrol->value.integer.value[0]));
	return change;
}

static int snd_asihpi_clkrate_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 8000;
	uinfo->value.integer.max = 192000;
	uinfo->value.integer.step = 100;

	return 0;
}

static int snd_asihpi_clkrate_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 rate;
	u16 e;

	e = hpi_sample_clock_get_sample_rate(h_control, &rate);
	if (!e)
		ucontrol->value.integer.value[0] = rate;
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int snd_asihpi_sampleclock_add(struct snd_card_asihpi *asihpi,
				      struct hpi_control *hpi_ctl)
{
	struct snd_card *card;
	struct snd_kcontrol_new snd_control;

	struct clk_cache *clkcache;
	u32 hSC =  hpi_ctl->h_control;
	int has_aes_in = 0;
	int i, j;
	u16 source;

	if (snd_BUG_ON(!asihpi))
		return -EINVAL;
	card = asihpi->card;
	clkcache = &asihpi->cc;
	snd_control.private_value = hpi_ctl->h_control;

	clkcache->has_local = 0;

	for (i = 0; i <= HPI_SAMPLECLOCK_SOURCE_LAST; i++) {
		if  (hpi_sample_clock_query_source(hSC,
				i, &source))
			break;
		clkcache->s[i].source = source;
		clkcache->s[i].index = 0;
		clkcache->s[i].name = sampleclock_sources[source];
		if (source == HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT)
			has_aes_in = 1;
		if (source == HPI_SAMPLECLOCK_SOURCE_LOCAL)
			clkcache->has_local = 1;
	}
	if (has_aes_in)
		/* already will have picked up index 0 above */
		for (j = 1; j < 8; j++) {
			if (hpi_sample_clock_query_source_index(hSC,
				j, HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT,
				&source))
				break;
			clkcache->s[i].source =
				HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT;
			clkcache->s[i].index = j;
			clkcache->s[i].name = sampleclock_sources[
					j+HPI_SAMPLECLOCK_SOURCE_LAST];
			i++;
		}
	clkcache->count = i;

	asihpi_ctl_init(&snd_control, hpi_ctl, "Source");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE ;
	snd_control.info = snd_asihpi_clksrc_info;
	snd_control.get = snd_asihpi_clksrc_get;
	snd_control.put = snd_asihpi_clksrc_put;
	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;


	if (clkcache->has_local) {
		asihpi_ctl_init(&snd_control, hpi_ctl, "Localrate");
		snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE ;
		snd_control.info = snd_asihpi_clklocal_info;
		snd_control.get = snd_asihpi_clklocal_get;
		snd_control.put = snd_asihpi_clklocal_put;


		if (ctl_add(card, &snd_control, asihpi) < 0)
			return -EINVAL;
	}

	asihpi_ctl_init(&snd_control, hpi_ctl, "Rate");
	snd_control.access =
	    SNDRV_CTL_ELEM_ACCESS_VOLATILE | SNDRV_CTL_ELEM_ACCESS_READ;
	snd_control.info = snd_asihpi_clkrate_info;
	snd_control.get = snd_asihpi_clkrate_get;

	return ctl_add(card, &snd_control, asihpi);
}
/*------------------------------------------------------------
   Mixer
 ------------------------------------------------------------*/

static int snd_card_asihpi_mixer_new(struct snd_card_asihpi *asihpi)
{
	struct snd_card *card;
	unsigned int idx = 0;
	unsigned int subindex = 0;
	int err;
	struct hpi_control hpi_ctl, prev_ctl;

	if (snd_BUG_ON(!asihpi))
		return -EINVAL;
	card = asihpi->card;
	strcpy(card->mixername, "Asihpi Mixer");

	err =
	    hpi_mixer_open(asihpi->hpi->adapter->index,
			  &asihpi->h_mixer);
	hpi_handle_error(err);
	if (err)
		return -err;

	memset(&prev_ctl, 0, sizeof(prev_ctl));
	prev_ctl.control_type = -1;

	for (idx = 0; idx < 2000; idx++) {
		err = hpi_mixer_get_control_by_index(
				asihpi->h_mixer,
				idx,
				&hpi_ctl.src_node_type,
				&hpi_ctl.src_node_index,
				&hpi_ctl.dst_node_type,
				&hpi_ctl.dst_node_index,
				&hpi_ctl.control_type,
				&hpi_ctl.h_control);
		if (err) {
			if (err == HPI_ERROR_CONTROL_DISABLED) {
				if (mixer_dump)
					dev_info(&asihpi->pci->dev,
						   "Disabled HPI Control(%d)\n",
						   idx);
				continue;
			} else
				break;

		}

		hpi_ctl.src_node_type -= HPI_SOURCENODE_NONE;
		hpi_ctl.dst_node_type -= HPI_DESTNODE_NONE;

		/* ASI50xx in SSX mode has multiple meters on the same node.
		   Use subindex to create distinct ALSA controls
		   for any duplicated controls.
		*/
		if ((hpi_ctl.control_type == prev_ctl.control_type) &&
		    (hpi_ctl.src_node_type == prev_ctl.src_node_type) &&
		    (hpi_ctl.src_node_index == prev_ctl.src_node_index) &&
		    (hpi_ctl.dst_node_type == prev_ctl.dst_node_type) &&
		    (hpi_ctl.dst_node_index == prev_ctl.dst_node_index))
			subindex++;
		else
			subindex = 0;

		prev_ctl = hpi_ctl;

		switch (hpi_ctl.control_type) {
		case HPI_CONTROL_VOLUME:
			err = snd_asihpi_volume_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_LEVEL:
			err = snd_asihpi_level_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_MULTIPLEXER:
			err = snd_asihpi_mux_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_CHANNEL_MODE:
			err = snd_asihpi_cmode_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_METER:
			err = snd_asihpi_meter_add(asihpi, &hpi_ctl, subindex);
			break;
		case HPI_CONTROL_SAMPLECLOCK:
			err = snd_asihpi_sampleclock_add(
						asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_CONNECTION:	/* ignore these */
			continue;
		case HPI_CONTROL_TUNER:
			err = snd_asihpi_tuner_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_AESEBU_TRANSMITTER:
			err = snd_asihpi_aesebu_tx_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_AESEBU_RECEIVER:
			err = snd_asihpi_aesebu_rx_add(asihpi, &hpi_ctl);
			break;
		case HPI_CONTROL_VOX:
		case HPI_CONTROL_BITSTREAM:
		case HPI_CONTROL_MICROPHONE:
		case HPI_CONTROL_PARAMETRIC_EQ:
		case HPI_CONTROL_COMPANDER:
		default:
			if (mixer_dump)
				dev_info(&asihpi->pci->dev,
					"Untranslated HPI Control (%d) %d %d %d %d %d\n",
					idx,
					hpi_ctl.control_type,
					hpi_ctl.src_node_type,
					hpi_ctl.src_node_index,
					hpi_ctl.dst_node_type,
					hpi_ctl.dst_node_index);
			continue;
		}
		if (err < 0)
			return err;
	}
	if (HPI_ERROR_INVALID_OBJ_INDEX != err)
		hpi_handle_error(err);

	dev_info(&asihpi->pci->dev, "%d mixer controls found\n", idx);

	return 0;
}

/*------------------------------------------------------------
   /proc interface
 ------------------------------------------------------------*/

static void
snd_asihpi_proc_read(struct snd_info_entry *entry,
			struct snd_info_buffer *buffer)
{
	struct snd_card_asihpi *asihpi = entry->private_data;
	u32 h_control;
	u32 rate = 0;
	u16 source = 0;

	u16 num_outstreams;
	u16 num_instreams;
	u16 version;
	u32 serial_number;
	u16 type;

	int err;

	snd_iprintf(buffer, "ASIHPI driver proc file\n");

	hpi_handle_error(hpi_adapter_get_info(asihpi->hpi->adapter->index,
			&num_outstreams, &num_instreams,
			&version, &serial_number, &type));

	snd_iprintf(buffer,
			"Adapter type ASI%4X\nHardware Index %d\n"
			"%d outstreams\n%d instreams\n",
			type, asihpi->hpi->adapter->index,
			num_outstreams, num_instreams);

	snd_iprintf(buffer,
		"Serial#%d\nHardware version %c%d\nDSP code version %03d\n",
		serial_number, ((version >> 3) & 0xf) + 'A', version & 0x7,
		((version >> 13) * 100) + ((version >> 7) & 0x3f));

	err = hpi_mixer_get_control(asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err) {
		err = hpi_sample_clock_get_sample_rate(h_control, &rate);
		err += hpi_sample_clock_get_source(h_control, &source);

		if (!err)
			snd_iprintf(buffer, "Sample Clock %dHz, source %s\n",
			rate, sampleclock_sources[source]);
	}
}

static void snd_asihpi_proc_init(struct snd_card_asihpi *asihpi)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(asihpi->card, "info", &entry))
		snd_info_set_text_ops(entry, asihpi, snd_asihpi_proc_read);
}

/*------------------------------------------------------------
   HWDEP
 ------------------------------------------------------------*/

static int snd_asihpi_hpi_open(struct snd_hwdep *hw, struct file *file)
{
	if (enable_hpi_hwdep)
		return 0;
	else
		return -ENODEV;

}

static int snd_asihpi_hpi_release(struct snd_hwdep *hw, struct file *file)
{
	if (enable_hpi_hwdep)
		return asihpi_hpi_release(file);
	else
		return -ENODEV;
}

static int snd_asihpi_hpi_ioctl(struct snd_hwdep *hw, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	if (enable_hpi_hwdep)
		return asihpi_hpi_ioctl(file, cmd, arg);
	else
		return -ENODEV;
}


/* results in /dev/snd/hwC#D0 file for each card with index #
   also /proc/asound/hwdep will contain '#-00: asihpi (HPI) for each card'
*/
static int snd_asihpi_hpi_new(struct snd_card_asihpi *asihpi, int device)
{
	struct snd_hwdep *hw;
	int err;

	err = snd_hwdep_new(asihpi->card, "HPI", device, &hw);
	if (err < 0)
		return err;
	strcpy(hw->name, "asihpi (HPI)");
	hw->iface = SNDRV_HWDEP_IFACE_LAST;
	hw->ops.open = snd_asihpi_hpi_open;
	hw->ops.ioctl = snd_asihpi_hpi_ioctl;
	hw->ops.release = snd_asihpi_hpi_release;
	hw->private_data = asihpi;
	return 0;
}

/*------------------------------------------------------------
   CARD
 ------------------------------------------------------------*/
static int snd_asihpi_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *pci_id)
{
	int err;
	struct hpi_adapter *hpi;
	struct snd_card *card;
	struct snd_card_asihpi *asihpi;

	u32 h_control;
	u32 h_stream;
	u32 adapter_index;

	static int dev;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	/* Should this be enable[hpi->index] ? */
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* Initialise low-level HPI driver */
	err = asihpi_adapter_probe(pci_dev, pci_id);
	if (err < 0)
		return err;

	hpi = pci_get_drvdata(pci_dev);
	adapter_index = hpi->adapter->index;
	/* first try to give the card the same index as its hardware index */
	err = snd_card_new(&pci_dev->dev, adapter_index, id[adapter_index],
			   THIS_MODULE, sizeof(struct snd_card_asihpi), &card);
	if (err < 0) {
		/* if that fails, try the default index==next available */
		err = snd_card_new(&pci_dev->dev, index[dev], id[dev],
				   THIS_MODULE, sizeof(struct snd_card_asihpi),
				   &card);
		if (err < 0)
			return err;
		dev_warn(&pci_dev->dev, "Adapter index %d->ALSA index %d\n",
			adapter_index, card->number);
	}

	asihpi = card->private_data;
	asihpi->card = card;
	asihpi->pci = pci_dev;
	asihpi->hpi = hpi;
	hpi->snd_card = card;

	err = hpi_adapter_get_property(adapter_index,
		HPI_ADAPTER_PROPERTY_CAPS1,
		NULL, &asihpi->support_grouping);
	if (err)
		asihpi->support_grouping = 0;

	err = hpi_adapter_get_property(adapter_index,
		HPI_ADAPTER_PROPERTY_CAPS2,
		&asihpi->support_mrx, NULL);
	if (err)
		asihpi->support_mrx = 0;

	err = hpi_adapter_get_property(adapter_index,
		HPI_ADAPTER_PROPERTY_INTERVAL,
		NULL, &asihpi->update_interval_frames);
	if (err)
		asihpi->update_interval_frames = 512;

	if (hpi->interrupt_mode) {
		asihpi->pcm_start = snd_card_asihpi_pcm_int_start;
		asihpi->pcm_stop = snd_card_asihpi_pcm_int_stop;
		tasklet_init(&asihpi->t, snd_card_asihpi_int_task,
			(unsigned long)hpi);
		hpi->interrupt_callback = snd_card_asihpi_isr;
	} else {
		asihpi->pcm_start = snd_card_asihpi_pcm_timer_start;
		asihpi->pcm_stop = snd_card_asihpi_pcm_timer_stop;
	}

	hpi_handle_error(hpi_instream_open(adapter_index,
			     0, &h_stream));

	err = hpi_instream_host_buffer_free(h_stream);
	asihpi->can_dma = (!err);

	hpi_handle_error(hpi_instream_close(h_stream));

	if (!asihpi->can_dma)
		asihpi->update_interval_frames *= 2;

	err = hpi_adapter_get_property(adapter_index,
		HPI_ADAPTER_PROPERTY_CURCHANNELS,
		&asihpi->in_max_chans, &asihpi->out_max_chans);
	if (err) {
		asihpi->in_max_chans = 2;
		asihpi->out_max_chans = 2;
	}

	if (asihpi->out_max_chans > 2) { /* assume LL mode */
		asihpi->out_min_chans = asihpi->out_max_chans;
		asihpi->in_min_chans = asihpi->in_max_chans;
		asihpi->support_grouping = 0;
	} else {
		asihpi->out_min_chans = 1;
		asihpi->in_min_chans = 1;
	}

	dev_info(&pci_dev->dev, "Has dma:%d, grouping:%d, mrx:%d, uif:%d\n",
			asihpi->can_dma,
			asihpi->support_grouping,
			asihpi->support_mrx,
			asihpi->update_interval_frames
	      );

	err = snd_card_asihpi_pcm_new(asihpi, 0);
	if (err < 0) {
		dev_err(&pci_dev->dev, "pcm_new failed\n");
		goto __nodev;
	}
	err = snd_card_asihpi_mixer_new(asihpi);
	if (err < 0) {
		dev_err(&pci_dev->dev, "mixer_new failed\n");
		goto __nodev;
	}

	err = hpi_mixer_get_control(asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_set_local_rate(
			h_control, adapter_fs);

	snd_asihpi_proc_init(asihpi);

	/* always create, can be enabled or disabled dynamically
	    by enable_hwdep  module param*/
	snd_asihpi_hpi_new(asihpi, 0);

	strcpy(card->driver, "ASIHPI");

	sprintf(card->shortname, "AudioScience ASI%4X",
			asihpi->hpi->adapter->type);
	sprintf(card->longname, "%s %i",
			card->shortname, adapter_index);
	err = snd_card_register(card);

	if (!err) {
		dev++;
		return 0;
	}
__nodev:
	snd_card_free(card);
	dev_err(&pci_dev->dev, "snd_asihpi_probe error %d\n", err);
	return err;

}

static void snd_asihpi_remove(struct pci_dev *pci_dev)
{
	struct hpi_adapter *hpi = pci_get_drvdata(pci_dev);
	struct snd_card_asihpi *asihpi = hpi->snd_card->private_data;

	/* Stop interrupts */
	if (hpi->interrupt_mode) {
		hpi->interrupt_callback = NULL;
		hpi_handle_error(hpi_adapter_set_property(hpi->adapter->index,
			HPI_ADAPTER_PROPERTY_IRQ_RATE, 0, 0));
		tasklet_kill(&asihpi->t);
	}

	snd_card_free(hpi->snd_card);
	hpi->snd_card = NULL;
	asihpi_adapter_remove(pci_dev);
}

static const struct pci_device_id asihpi_pci_tbl[] = {
	{HPI_PCI_VENDOR_ID_TI, HPI_PCI_DEV_ID_DSP6205,
		HPI_PCI_VENDOR_ID_AUDIOSCIENCE, PCI_ANY_ID, 0, 0,
		(kernel_ulong_t)HPI_6205},
	{HPI_PCI_VENDOR_ID_TI, HPI_PCI_DEV_ID_PCI2040,
		HPI_PCI_VENDOR_ID_AUDIOSCIENCE, PCI_ANY_ID, 0, 0,
		(kernel_ulong_t)HPI_6000},
	{0,}
};
MODULE_DEVICE_TABLE(pci, asihpi_pci_tbl);

static struct pci_driver driver = {
	.name = KBUILD_MODNAME,
	.id_table = asihpi_pci_tbl,
	.probe = snd_asihpi_probe,
	.remove = snd_asihpi_remove,
};

static int __init snd_asihpi_init(void)
{
	asihpi_init();
	return pci_register_driver(&driver);
}

static void __exit snd_asihpi_exit(void)
{

	pci_unregister_driver(&driver);
	asihpi_exit();
}

module_init(snd_asihpi_init)
module_exit(snd_asihpi_exit)

