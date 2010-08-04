/*
 *  Asihpi soundcard
 *  Copyright (c) by AudioScience Inc <alsa@audioscience.com>
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
/* >0: print Hw params, timer vars. >1: print stream write/copy sizes  */
#define REALLY_VERBOSE_LOGGING 0

#if REALLY_VERBOSE_LOGGING
#define VPRINTK1 snd_printd
#else
#define VPRINTK1(...)
#endif

#if REALLY_VERBOSE_LOGGING > 1
#define VPRINTK2 snd_printd
#else
#define VPRINTK2(...)
#endif

#ifndef ASI_STYLE_NAMES
/* not sure how ALSA style name should look */
#define ASI_STYLE_NAMES 1
#endif

#include "hpi_internal.h"
#include "hpimsginit.h"
#include "hpioctl.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
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
MODULE_DESCRIPTION("AudioScience ALSA ASI5000 ASI6000 ASI87xx ASI89xx");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static int enable_hpi_hwdep = 1;

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
static char *build_info = "built using headers from kernel source";
module_param(build_info, charp, S_IRUGO);
MODULE_PARM_DESC(build_info, "built using headers from kernel source");
#else
static char *build_info = "built within ALSA source";
module_param(build_info, charp, S_IRUGO);
MODULE_PARM_DESC(build_info, "built within ALSA source");
#endif

/* set to 1 to dump every control from adapter to log */
static const int mixer_dump;

#define DEFAULT_SAMPLERATE 44100
static int adapter_fs = DEFAULT_SAMPLERATE;

static struct hpi_hsubsys *ss;	/* handle to HPI audio subsystem */

/* defaults */
#define PERIODS_MIN 2
#define PERIOD_BYTES_MIN  2304
#define BUFFER_BYTES_MAX (512 * 1024)

/*#define TIMER_MILLISECONDS 20
#define FORCE_TIMER_JIFFIES ((TIMER_MILLISECONDS * HZ + 999)/1000)
*/

#define MAX_CLOCKSOURCES (HPI_SAMPLECLOCK_SOURCE_LAST + 1 + 7)

struct clk_source {
	int source;
	int index;
	char *name;
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
	u16 adapter_index;
	u32 serial_number;
	u16 type;
	u16 version;
	u16 num_outstreams;
	u16 num_instreams;

	u32 h_mixer;
	struct clk_cache cc;

	u16 support_mmap;
	u16 support_grouping;
	u16 support_mrx;
	u16 update_interval_frames;
	u16 in_max_chans;
	u16 out_max_chans;
};

/* Per stream data */
struct snd_card_asihpi_pcm {
	struct timer_list timer;
	unsigned int respawn_timer;
	unsigned int hpi_buffer_attached;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int bytes_per_sec;
	unsigned int pcm_irq_pos;	/* IRQ position */
	unsigned int pcm_buf_pos;	/* position in buffer */
	struct snd_pcm_substream *substream;
	u32 h_stream;
	struct hpi_format format;
};

/* universal stream verbs work with out or in stream handles */

/* Functions to allow driver to give a buffer to HPI for busmastering */

static u16 hpi_stream_host_buffer_attach(
	struct hpi_hsubsys *hS,
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

static u16 hpi_stream_host_buffer_detach(
	struct hpi_hsubsys *hS,
	u32  h_stream
)
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

static inline u16 hpi_stream_start(struct hpi_hsubsys *hS, u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_start(hS, h_stream);
	else
		return hpi_instream_start(hS, h_stream);
}

static inline u16 hpi_stream_stop(struct hpi_hsubsys *hS, u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_stop(hS, h_stream);
	else
		return hpi_instream_stop(hS, h_stream);
}

static inline u16 hpi_stream_get_info_ex(
    struct hpi_hsubsys *hS,
    u32 h_stream,
    u16        *pw_state,
    u32        *pbuffer_size,
    u32        *pdata_in_buffer,
    u32        *psample_count,
    u32        *pauxiliary_data
)
{
	if (hpi_handle_object(h_stream)  ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_get_info_ex(hS, h_stream, pw_state,
					pbuffer_size, pdata_in_buffer,
					psample_count, pauxiliary_data);
	else
		return hpi_instream_get_info_ex(hS, h_stream, pw_state,
					pbuffer_size, pdata_in_buffer,
					psample_count, pauxiliary_data);
}

static inline u16 hpi_stream_group_add(struct hpi_hsubsys *hS,
					u32 h_master,
					u32 h_stream)
{
	if (hpi_handle_object(h_master) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_add(hS, h_master, h_stream);
	else
		return hpi_instream_group_add(hS, h_master, h_stream);
}

static inline u16 hpi_stream_group_reset(struct hpi_hsubsys *hS,
						u32 h_stream)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_reset(hS, h_stream);
	else
		return hpi_instream_group_reset(hS, h_stream);
}

static inline u16 hpi_stream_group_get_map(struct hpi_hsubsys *hS,
				u32 h_stream, u32 *mo, u32 *mi)
{
	if (hpi_handle_object(h_stream) ==  HPI_OBJ_OSTREAM)
		return hpi_outstream_group_get_map(hS, h_stream, mo, mi);
	else
		return hpi_instream_group_get_map(hS, h_stream, mo, mi);
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
#if REALLY_VERBOSE_LOGGING
static void print_hwparams(struct snd_pcm_hw_params *p)
{
	snd_printd("HWPARAMS \n");
	snd_printd("samplerate %d \n", params_rate(p));
	snd_printd("channels %d \n", params_channels(p));
	snd_printd("format %d \n", params_format(p));
	snd_printd("subformat %d \n", params_subformat(p));
	snd_printd("buffer bytes %d \n", params_buffer_bytes(p));
	snd_printd("period bytes %d \n", params_period_bytes(p));
	snd_printd("access %d \n", params_access(p));
	snd_printd("period_size %d \n", params_period_size(p));
	snd_printd("periods %d \n", params_periods(p));
	snd_printd("buffer_size %d \n", params_buffer_size(p));
}
#else
#define print_hwparams(x)
#endif

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
	/* SNDRV_PCM_FORMAT_S24_3LE */	/* { HPI_FORMAT_PCM24_SIGNED        15 */
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
		err = hpi_mixer_get_control(ss, asihpi->h_mixer,
					  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
					  HPI_CONTROL_SAMPLECLOCK, &h_control);
		if (err) {
			snd_printk(KERN_ERR
				"no local sampleclock, err %d\n", err);
		}

		for (idx = 0; idx < 100; idx++) {
			if (hpi_sample_clock_query_local_rate(ss,
				h_control, idx, &sample_rate)) {
				if (!idx)
					snd_printk(KERN_ERR
						"local rate query failed\n");

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

	/* printk(KERN_INFO "Supported rates %X %d %d\n",
	   rates, rate_min, rate_max); */
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
	unsigned int bytes_per_sec;

	print_hwparams(params);
	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (err < 0)
		return err;
	err = snd_card_asihpi_format_alsa2hpi(params_format(params), &format);
	if (err)
		return err;

	VPRINTK1(KERN_INFO "format %d, %d chans, %d_hz\n",
				format, params_channels(params),
				params_rate(params));

	hpi_handle_error(hpi_format_create(&dpcm->format,
			params_channels(params),
			format, params_rate(params), 0, 0));

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (hpi_instream_reset(ss, dpcm->h_stream) != 0)
			return -EINVAL;

		if (hpi_instream_set_format(ss,
			dpcm->h_stream, &dpcm->format) != 0)
			return -EINVAL;
	}

	dpcm->hpi_buffer_attached = 0;
	if (card->support_mmap) {

		err = hpi_stream_host_buffer_attach(ss, dpcm->h_stream,
			params_buffer_bytes(params),  runtime->dma_addr);
		if (err == 0) {
			snd_printd(KERN_INFO
				"stream_host_buffer_attach succeeded %u %lu\n",
				params_buffer_bytes(params),
				(unsigned long)runtime->dma_addr);
		} else {
			snd_printd(KERN_INFO
					"stream_host_buffer_attach error %d\n",
					err);
			return -ENOMEM;
		}

		err = hpi_stream_get_info_ex(ss, dpcm->h_stream, NULL,
						&dpcm->hpi_buffer_attached,
						NULL, NULL, NULL);

		snd_printd(KERN_INFO "stream_host_buffer_attach status 0x%x\n",
				dpcm->hpi_buffer_attached);
	}
	bytes_per_sec = params_rate(params) * params_channels(params);
	bytes_per_sec *= snd_pcm_format_width(params_format(params));
	bytes_per_sec /= 8;
	if (bytes_per_sec <= 0)
		return -EINVAL;

	dpcm->bytes_per_sec = bytes_per_sec;
	dpcm->pcm_size = params_buffer_bytes(params);
	dpcm->pcm_count = params_period_bytes(params);
	snd_printd(KERN_INFO "pcm_size=%d, pcm_count=%d, bps=%d\n",
			dpcm->pcm_size, dpcm->pcm_count, bytes_per_sec);

	dpcm->pcm_irq_pos = 0;
	dpcm->pcm_buf_pos = 0;
	return 0;
}

static void snd_card_asihpi_pcm_timer_start(struct snd_pcm_substream *
					    substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	int expiry;

	expiry = (dpcm->pcm_count * HZ / dpcm->bytes_per_sec);
	/* wait longer the first time, for samples to propagate */
	expiry = max(expiry, 20);
	dpcm->timer.expires = jiffies + expiry;
	dpcm->respawn_timer = 1;
	add_timer(&dpcm->timer);
}

static void snd_card_asihpi_pcm_timer_stop(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	dpcm->respawn_timer = 0;
	del_timer(&dpcm->timer);
}

static int snd_card_asihpi_trigger(struct snd_pcm_substream *substream,
					   int cmd)
{
	struct snd_card_asihpi_pcm *dpcm = substream->runtime->private_data;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	u16 e;

	snd_printd("trigger %dstream %d\n",
			substream->stream, substream->number);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_pcm_group_for_each_entry(s, substream) {
			struct snd_card_asihpi_pcm *ds;
			ds = s->runtime->private_data;

			if (snd_pcm_substream_chip(s) != card)
				continue;

			if ((s->stream == SNDRV_PCM_STREAM_PLAYBACK) &&
				(card->support_mmap)) {
				/* How do I know how much valid data is present
				* in buffer? Just guessing 2 periods, but if
				* buffer is bigger it may contain even more
				* data??
				*/
				unsigned int preload = ds->pcm_count * 2;
				VPRINTK2("preload %d\n", preload);
				hpi_handle_error(hpi_outstream_write_buf(
						ss, ds->h_stream,
						&s->runtime->dma_area[0],
						preload,
						&ds->format));
			}

			if (card->support_grouping) {
				VPRINTK1("\t_group %dstream %d\n", s->stream,
						s->number);
				e = hpi_stream_group_add(ss,
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
		snd_printd("start\n");
		/* start the master stream */
		snd_card_asihpi_pcm_timer_start(substream);
		hpi_handle_error(hpi_stream_start(ss, dpcm->h_stream));
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		snd_card_asihpi_pcm_timer_stop(substream);
		snd_pcm_group_for_each_entry(s, substream) {
			if (snd_pcm_substream_chip(s) != card)
				continue;

			/*? workaround linked streams don't
			transition to SETUP 20070706*/
			s->runtime->status->state = SNDRV_PCM_STATE_SETUP;

			if (card->support_grouping) {
				VPRINTK1("\t_group %dstream %d\n", s->stream,
					s->number);
				snd_pcm_trigger_done(s, substream);
			} else
				break;
		}
		snd_printd("stop\n");

		/* _prepare and _hwparams reset the stream */
		hpi_handle_error(hpi_stream_stop(ss, dpcm->h_stream));
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			hpi_handle_error(
				hpi_outstream_reset(ss, dpcm->h_stream));

		if (card->support_grouping)
			hpi_handle_error(hpi_stream_group_reset(ss,
						dpcm->h_stream));
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printd("pause release\n");
		hpi_handle_error(hpi_stream_start(ss, dpcm->h_stream));
		snd_card_asihpi_pcm_timer_start(substream);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printd("pause\n");
		snd_card_asihpi_pcm_timer_stop(substream);
		hpi_handle_error(hpi_stream_stop(ss, dpcm->h_stream));
		break;
	default:
		snd_printd("\tINVALID\n");
		return -EINVAL;
	}

	return 0;
}

static int
snd_card_asihpi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	if (dpcm->hpi_buffer_attached)
		hpi_stream_host_buffer_detach(ss, dpcm->h_stream);

	snd_pcm_lib_free_pages(substream);
	return 0;
}

static void snd_card_asihpi_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	kfree(dpcm);
}

/*algorithm outline
 Without linking degenerates to getting single stream pos etc
 Without mmap 2nd loop degenerates to snd_pcm_period_elapsed
*/
/*
buf_pos=get_buf_pos(s);
for_each_linked_stream(s) {
	buf_pos=get_buf_pos(s);
	min_buf_pos = modulo_min(min_buf_pos, buf_pos, pcm_size)
	new_data = min(new_data, calc_new_data(buf_pos,irq_pos)
}
timer.expires = jiffies + predict_next_period_ready(min_buf_pos);
for_each_linked_stream(s) {
	s->buf_pos = min_buf_pos;
	if (new_data > pcm_count) {
		if (mmap) {
			irq_pos = (irq_pos + pcm_count) % pcm_size;
			if (playback) {
				write(pcm_count);
			} else {
				read(pcm_count);
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
	struct snd_card_asihpi *card = snd_pcm_substream_chip(dpcm->substream);
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_substream *s;
	unsigned int newdata = 0;
	unsigned int buf_pos, min_buf_pos = 0;
	unsigned int remdata, xfercount, next_jiffies;
	int first = 1;
	u16 state;
	u32 buffer_size, data_avail, samples_played, aux;

	/* find minimum newdata and buffer pos in group */
	snd_pcm_group_for_each_entry(s, dpcm->substream) {
		struct snd_card_asihpi_pcm *ds = s->runtime->private_data;
		runtime = s->runtime;

		if (snd_pcm_substream_chip(s) != card)
			continue;

		hpi_handle_error(hpi_stream_get_info_ex(ss,
					ds->h_stream, &state,
					&buffer_size, &data_avail,
					&samples_played, &aux));

		/* number of bytes in on-card buffer */
		runtime->delay = aux;

		if (state == HPI_STATE_DRAINED) {
			snd_printd(KERN_WARNING  "outstream %d drained\n",
					s->number);
			snd_pcm_stop(s, SNDRV_PCM_STATE_XRUN);
			return;
		}

		if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			buf_pos = frames_to_bytes(runtime, samples_played);
		} else {
			buf_pos = data_avail + ds->pcm_irq_pos;
		}

		if (first) {
			/* can't statically init min when wrap is involved */
			min_buf_pos = buf_pos;
			newdata = (buf_pos - ds->pcm_irq_pos) % ds->pcm_size;
			first = 0;
		} else {
			min_buf_pos =
				modulo_min(min_buf_pos, buf_pos, UINT_MAX+1L);
			newdata = min(
				(buf_pos - ds->pcm_irq_pos) % ds->pcm_size,
				newdata);
		}

		VPRINTK1("PB timer hw_ptr x%04lX, appl_ptr x%04lX\n",
			(unsigned long)frames_to_bytes(runtime,
						runtime->status->hw_ptr),
			(unsigned long)frames_to_bytes(runtime,
						runtime->control->appl_ptr));
		VPRINTK1("%d S=%d, irq=%04X, pos=x%04X, left=x%04X,"
			" aux=x%04X space=x%04X\n", s->number,
			state,	ds->pcm_irq_pos, buf_pos, (int)data_avail,
			(int)aux, buffer_size-data_avail);
	}

	remdata = newdata % dpcm->pcm_count;
	xfercount = newdata - remdata; /* a multiple of pcm_count */
	next_jiffies = ((dpcm->pcm_count-remdata) * HZ / dpcm->bytes_per_sec)+1;
	next_jiffies = max(next_jiffies, 2U * HZ / 1000U);
	dpcm->timer.expires = jiffies + next_jiffies;
	VPRINTK1("jif %d buf pos x%04X newdata x%04X xc x%04X\n",
			next_jiffies, min_buf_pos, newdata, xfercount);

	snd_pcm_group_for_each_entry(s, dpcm->substream) {
		struct snd_card_asihpi_pcm *ds = s->runtime->private_data;
		ds->pcm_buf_pos = min_buf_pos;

		if (xfercount) {
			if (card->support_mmap) {
				ds->pcm_irq_pos = ds->pcm_irq_pos + xfercount;
				if (s->stream == SNDRV_PCM_STREAM_PLAYBACK) {
					VPRINTK2("write OS%d x%04x\n",
							s->number,
							ds->pcm_count);
					hpi_handle_error(
						hpi_outstream_write_buf(
							ss, ds->h_stream,
							&s->runtime->
								dma_area[0],
							xfercount,
							&ds->format));
				} else {
					VPRINTK2("read IS%d x%04x\n",
						s->number,
						dpcm->pcm_count);
					hpi_handle_error(
						hpi_instream_read_buf(
							ss, ds->h_stream,
							NULL, xfercount));
				}
			} /* else R/W will be handled by read/write callbacks */
			snd_pcm_period_elapsed(s);
		}
	}

	if (dpcm->respawn_timer)
		add_timer(&dpcm->timer);
}

/***************************** PLAYBACK OPS ****************/
static int snd_card_asihpi_playback_ioctl(struct snd_pcm_substream *substream,
					  unsigned int cmd, void *arg)
{
	/* snd_printd(KERN_INFO "Playback ioctl %d\n", cmd); */
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_asihpi_playback_prepare(struct snd_pcm_substream *
					    substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	snd_printd(KERN_INFO "playback prepare %d\n", substream->number);

	hpi_handle_error(hpi_outstream_reset(ss, dpcm->h_stream));
	dpcm->pcm_irq_pos = 0;
	dpcm->pcm_buf_pos = 0;

	return 0;
}

static snd_pcm_uframes_t
snd_card_asihpi_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t ptr;

	u32 samples_played;
	u16 err;

	if (!snd_pcm_stream_linked(substream)) {
		/* NOTE, can use samples played for playback position here and
		* in timer fn because it LAGS the actual read pointer, and is a
		* better representation of actual playout position
		*/
		err = hpi_outstream_get_info_ex(ss, dpcm->h_stream, NULL,
					NULL, NULL,
					&samples_played, NULL);
		hpi_handle_error(err);

		dpcm->pcm_buf_pos = frames_to_bytes(runtime, samples_played);
	}
	/* else must return most conservative value found in timer func
	 * by looping over all streams
	 */

	ptr = bytes_to_frames(runtime, dpcm->pcm_buf_pos  % dpcm->pcm_size);
	VPRINTK2("playback_pointer=%04ld\n", (unsigned long)ptr);
	return ptr;
}

static void snd_card_asihpi_playback_format(struct snd_card_asihpi *asihpi,
						u32 h_stream,
						struct snd_pcm_hardware *pcmhw)
{
	struct hpi_format hpi_format;
	u16 format;
	u16 err;
	u32 h_control;
	u32 sample_rate = 48000;

	/* on cards without SRC, must query at valid rate,
	* maybe set by external sync
	*/
	err = hpi_mixer_get_control(ss, asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_get_sample_rate(ss, h_control,
				&sample_rate);

	for (format = HPI_FORMAT_PCM8_UNSIGNED;
	     format <= HPI_FORMAT_PCM24_SIGNED; format++) {
		err = hpi_format_create(&hpi_format,
					2, format, sample_rate, 128000, 0);
		if (!err)
			err = hpi_outstream_query_format(ss, h_stream,
							&hpi_format);
		if (!err && (hpi_to_alsa_formats[format] != -1))
			pcmhw->formats |=
				(1ULL << hpi_to_alsa_formats[format]);
	}
}

static struct snd_pcm_hardware snd_card_asihpi_playback = {
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN,
	.periods_min = PERIODS_MIN,
	.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
	.fifo_size = 0,
};

static int snd_card_asihpi_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	int err;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;

	err =
	    hpi_outstream_open(ss, card->adapter_index,
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
	    If internal and other stream playing, cant switch
	*/

	init_timer(&dpcm->timer);
	dpcm->timer.data = (unsigned long) dpcm;
	dpcm->timer.function = snd_card_asihpi_timer_function;
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_asihpi_runtime_free;

	snd_card_asihpi_playback.channels_max = card->out_max_chans;
	/*?snd_card_asihpi_playback.period_bytes_min =
	card->out_max_chans * 4096; */

	snd_card_asihpi_playback_format(card, dpcm->h_stream,
					&snd_card_asihpi_playback);

	snd_card_asihpi_pcm_samplerates(card,  &snd_card_asihpi_playback);

	snd_card_asihpi_playback.info = SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_DOUBLE |
					SNDRV_PCM_INFO_BATCH |
					SNDRV_PCM_INFO_BLOCK_TRANSFER |
					SNDRV_PCM_INFO_PAUSE;

	if (card->support_mmap)
		snd_card_asihpi_playback.info |= SNDRV_PCM_INFO_MMAP |
						SNDRV_PCM_INFO_MMAP_VALID;

	if (card->support_grouping)
		snd_card_asihpi_playback.info |= SNDRV_PCM_INFO_SYNC_START;

	/* struct is copied, so can create initializer dynamically */
	runtime->hw = snd_card_asihpi_playback;

	if (card->support_mmap)
		err = snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	if (err < 0)
		return err;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames * 4, UINT_MAX);

	snd_pcm_set_sync(substream);

	snd_printd(KERN_INFO "playback open\n");

	return 0;
}

static int snd_card_asihpi_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	hpi_handle_error(hpi_outstream_close(ss, dpcm->h_stream));
	snd_printd(KERN_INFO "playback close\n");

	return 0;
}

static int snd_card_asihpi_playback_copy(struct snd_pcm_substream *substream,
					int channel,
					snd_pcm_uframes_t pos,
					void __user *src,
					snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	unsigned int len;

	len = frames_to_bytes(runtime, count);

	if (copy_from_user(runtime->dma_area, src, len))
		return -EFAULT;

	VPRINTK2(KERN_DEBUG "playback copy%d %u bytes\n",
			substream->number, len);

	hpi_handle_error(hpi_outstream_write_buf(ss, dpcm->h_stream,
				runtime->dma_area, len, &dpcm->format));

	return 0;
}

static int snd_card_asihpi_playback_silence(struct snd_pcm_substream *
					    substream, int channel,
					    snd_pcm_uframes_t pos,
					    snd_pcm_uframes_t count)
{
	unsigned int len;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;

	len = frames_to_bytes(runtime, count);
	snd_printd(KERN_INFO "playback silence  %u bytes\n", len);

	memset(runtime->dma_area, 0, len);
	hpi_handle_error(hpi_outstream_write_buf(ss, dpcm->h_stream,
				runtime->dma_area, len, &dpcm->format));
	return 0;
}

static struct snd_pcm_ops snd_card_asihpi_playback_ops = {
	.open = snd_card_asihpi_playback_open,
	.close = snd_card_asihpi_playback_close,
	.ioctl = snd_card_asihpi_playback_ioctl,
	.hw_params = snd_card_asihpi_pcm_hw_params,
	.hw_free = snd_card_asihpi_hw_free,
	.prepare = snd_card_asihpi_playback_prepare,
	.trigger = snd_card_asihpi_trigger,
	.pointer = snd_card_asihpi_playback_pointer,
	.copy = snd_card_asihpi_playback_copy,
	.silence = snd_card_asihpi_playback_silence,
};

static struct snd_pcm_ops snd_card_asihpi_playback_mmap_ops = {
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

	VPRINTK2("capture pointer %d=%d\n",
			substream->number, dpcm->pcm_buf_pos);
	/* NOTE Unlike playback can't use actual dwSamplesPlayed
		for the capture position, because those samples aren't yet in
		the local buffer available for reading.
	*/
	return bytes_to_frames(runtime, dpcm->pcm_buf_pos % dpcm->pcm_size);
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

	hpi_handle_error(hpi_instream_reset(ss, dpcm->h_stream));
	dpcm->pcm_irq_pos = 0;
	dpcm->pcm_buf_pos = 0;

	snd_printd("capture prepare %d\n", substream->number);
	return 0;
}



static void snd_card_asihpi_capture_format(struct snd_card_asihpi *asihpi,
					u32 h_stream,
					 struct snd_pcm_hardware *pcmhw)
{
  struct hpi_format hpi_format;
	u16 format;
	u16 err;
	u32 h_control;
	u32 sample_rate = 48000;

	/* on cards without SRC, must query at valid rate,
		maybe set by external sync */
	err = hpi_mixer_get_control(ss, asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_get_sample_rate(ss, h_control,
			&sample_rate);

	for (format = HPI_FORMAT_PCM8_UNSIGNED;
		format <= HPI_FORMAT_PCM24_SIGNED; format++) {

		err = hpi_format_create(&hpi_format, 2, format,
				sample_rate, 128000, 0);
		if (!err)
			err = hpi_instream_query_format(ss, h_stream,
					    &hpi_format);
		if (!err)
			pcmhw->formats |=
				(1ULL << hpi_to_alsa_formats[format]);
	}
}


static struct snd_pcm_hardware snd_card_asihpi_capture = {
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX / PERIODS_MIN,
	.periods_min = PERIODS_MIN,
	.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
	.fifo_size = 0,
};

static int snd_card_asihpi_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi *card = snd_pcm_substream_chip(substream);
	struct snd_card_asihpi_pcm *dpcm;
	int err;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;

	snd_printd("hpi_instream_open adapter %d stream %d\n",
		   card->adapter_index, substream->number);

	err = hpi_handle_error(
	    hpi_instream_open(ss, card->adapter_index,
			     substream->number, &dpcm->h_stream));
	if (err)
		kfree(dpcm);
	if (err == HPI_ERROR_OBJ_ALREADY_OPEN)
		return -EBUSY;
	if (err)
		return -EIO;


	init_timer(&dpcm->timer);
	dpcm->timer.data = (unsigned long) dpcm;
	dpcm->timer.function = snd_card_asihpi_timer_function;
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_asihpi_runtime_free;

	snd_card_asihpi_capture.channels_max = card->in_max_chans;
	snd_card_asihpi_capture_format(card, dpcm->h_stream,
				       &snd_card_asihpi_capture);
	snd_card_asihpi_pcm_samplerates(card,  &snd_card_asihpi_capture);
	snd_card_asihpi_capture.info = SNDRV_PCM_INFO_INTERLEAVED;

	if (card->support_mmap)
		snd_card_asihpi_capture.info |= SNDRV_PCM_INFO_MMAP |
						SNDRV_PCM_INFO_MMAP_VALID;

	runtime->hw = snd_card_asihpi_capture;

	if (card->support_mmap)
		err = snd_pcm_hw_constraint_pow2(runtime, 0,
					SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
	if (err < 0)
		return err;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		card->update_interval_frames * 2, UINT_MAX);

	snd_pcm_set_sync(substream);

	return 0;
}

static int snd_card_asihpi_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_card_asihpi_pcm *dpcm = substream->runtime->private_data;

	hpi_handle_error(hpi_instream_close(ss, dpcm->h_stream));
	return 0;
}

static int snd_card_asihpi_capture_copy(struct snd_pcm_substream *substream,
				int channel, snd_pcm_uframes_t pos,
				void __user *dst, snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_asihpi_pcm *dpcm = runtime->private_data;
	u32 data_size;

	data_size = frames_to_bytes(runtime, count);

	VPRINTK2("capture copy%d %d bytes\n", substream->number, data_size);
	hpi_handle_error(hpi_instream_read_buf(ss, dpcm->h_stream,
				runtime->dma_area, data_size));

	/* Used by capture_pointer */
	dpcm->pcm_irq_pos = dpcm->pcm_irq_pos + data_size;

	if (copy_to_user(dst, runtime->dma_area, data_size))
		return -EFAULT;

	return 0;
}

static struct snd_pcm_ops snd_card_asihpi_capture_mmap_ops = {
	.open = snd_card_asihpi_capture_open,
	.close = snd_card_asihpi_capture_close,
	.ioctl = snd_card_asihpi_capture_ioctl,
	.hw_params = snd_card_asihpi_pcm_hw_params,
	.hw_free = snd_card_asihpi_hw_free,
	.prepare = snd_card_asihpi_capture_prepare,
	.trigger = snd_card_asihpi_trigger,
	.pointer = snd_card_asihpi_capture_pointer,
};

static struct snd_pcm_ops snd_card_asihpi_capture_ops = {
	.open = snd_card_asihpi_capture_open,
	.close = snd_card_asihpi_capture_close,
	.ioctl = snd_card_asihpi_capture_ioctl,
	.hw_params = snd_card_asihpi_pcm_hw_params,
	.hw_free = snd_card_asihpi_hw_free,
	.prepare = snd_card_asihpi_capture_prepare,
	.trigger = snd_card_asihpi_trigger,
	.pointer = snd_card_asihpi_capture_pointer,
	.copy = snd_card_asihpi_capture_copy
};

static int __devinit snd_card_asihpi_pcm_new(struct snd_card_asihpi *asihpi,
				      int device, int substreams)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(asihpi->card, "asihpi PCM", device,
			 asihpi->num_outstreams, asihpi->num_instreams,
			 &pcm);
	if (err < 0)
		return err;
	/* pointer to ops struct is stored, dont change ops afterwards! */
	if (asihpi->support_mmap) {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_card_asihpi_playback_mmap_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_card_asihpi_capture_mmap_ops);
	} else {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_card_asihpi_playback_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_card_asihpi_capture_ops);
	}

	pcm->private_data = asihpi;
	pcm->info_flags = 0;
	strcpy(pcm->name, "asihpi PCM");

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
	char name[44]; /* copied to snd_ctl_elem_id.name[44]; */
};

static char *asihpi_tuner_band_names[] =
{
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
};

compile_time_assert(
	(ARRAY_SIZE(asihpi_tuner_band_names) ==
		(HPI_TUNER_BAND_LAST+1)),
	assert_tuner_band_names_size);

#if ASI_STYLE_NAMES
static char *asihpi_src_names[] =
{
	"no source",
	"outstream",
	"line_in",
	"aes_in",
	"tuner",
	"RF",
	"clock",
	"bitstr",
	"mic",
	"cobranet",
	"analog_in",
	"adapter",
};
#else
static char *asihpi_src_names[] =
{
	"no source",
	"PCM playback",
	"line in",
	"digital in",
	"tuner",
	"RF",
	"clock",
	"bitstream",
	"mic",
	"cobranet in",
	"analog in",
	"adapter",
};
#endif

compile_time_assert(
	(ARRAY_SIZE(asihpi_src_names) ==
		(HPI_SOURCENODE_LAST_INDEX-HPI_SOURCENODE_BASE+1)),
	assert_src_names_size);

#if ASI_STYLE_NAMES
static char *asihpi_dst_names[] =
{
	"no destination",
	"instream",
	"line_out",
	"aes_out",
	"RF",
	"speaker" ,
	"cobranet",
	"analog_out",
};
#else
static char *asihpi_dst_names[] =
{
	"no destination",
	"PCM capture",
	"line out",
	"digital out",
	"RF",
	"speaker",
	"cobranet out",
	"analog out"
};
#endif

compile_time_assert(
	(ARRAY_SIZE(asihpi_dst_names) ==
		(HPI_DESTNODE_LAST_INDEX-HPI_DESTNODE_BASE+1)),
	assert_dst_names_size);

static inline int ctl_add(struct snd_card *card, struct snd_kcontrol_new *ctl,
				struct snd_card_asihpi *asihpi)
{
	int err;

	err = snd_ctl_add(card, snd_ctl_new1(ctl, asihpi));
	if (err < 0)
		return err;
	else if (mixer_dump)
		snd_printk(KERN_INFO "added %s(%d)\n", ctl->name, ctl->index);

	return 0;
}

/* Convert HPI control name and location into ALSA control name */
static void asihpi_ctl_init(struct snd_kcontrol_new *snd_control,
				struct hpi_control *hpi_ctl,
				char *name)
{
	memset(snd_control, 0, sizeof(*snd_control));
	snd_control->name = hpi_ctl->name;
	snd_control->private_value = hpi_ctl->h_control;
	snd_control->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	snd_control->index = 0;

	if (hpi_ctl->src_node_type && hpi_ctl->dst_node_type)
		sprintf(hpi_ctl->name, "%s%d to %s%d %s",
			asihpi_src_names[hpi_ctl->src_node_type],
			hpi_ctl->src_node_index,
			asihpi_dst_names[hpi_ctl->dst_node_type],
			hpi_ctl->dst_node_index,
			name);
	else if (hpi_ctl->dst_node_type) {
		sprintf(hpi_ctl->name, "%s%d %s",
		asihpi_dst_names[hpi_ctl->dst_node_type],
		hpi_ctl->dst_node_index,
		name);
	} else {
		sprintf(hpi_ctl->name, "%s%d %s",
		asihpi_src_names[hpi_ctl->src_node_type],
		hpi_ctl->src_node_index,
		name);
	}
}

/*------------------------------------------------------------
   Volume controls
 ------------------------------------------------------------*/
#define VOL_STEP_mB 1
static int snd_asihpi_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	u32 h_control = kcontrol->private_value;
	u16 err;
	/* native gains are in millibels */
	short min_gain_mB;
	short max_gain_mB;
	short step_gain_mB;

	err = hpi_volume_query_range(ss, h_control,
			&min_gain_mB, &max_gain_mB, &step_gain_mB);
	if (err) {
		max_gain_mB = 0;
		min_gain_mB = -10000;
		step_gain_mB = VOL_STEP_mB;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
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

	hpi_handle_error(hpi_volume_get_gain(ss, h_control, an_gain_mB));
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
	hpi_handle_error(hpi_volume_set_gain(ss, h_control, an_gain_mB));
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_100, -10000, VOL_STEP_mB, 0);

static int __devinit snd_asihpi_volume_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "volume");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ;
	snd_control.info = snd_asihpi_volume_info;
	snd_control.get = snd_asihpi_volume_get;
	snd_control.put = snd_asihpi_volume_put;
	snd_control.tlv.p = db_scale_100;

	return ctl_add(card, &snd_control, asihpi);
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
	    hpi_level_query_range(ss, h_control, &min_gain_mB,
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

	hpi_handle_error(hpi_level_get_gain(ss, h_control, an_gain_mB));
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
	hpi_handle_error(hpi_level_set_gain(ss, h_control, an_gain_mB));
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_level, -1000, 100, 0);

static int __devinit snd_asihpi_level_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	/* can't use 'volume' cos some nodes have volume as well */
	asihpi_ctl_init(&snd_control, hpi_ctl, "level");
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
static char *asihpi_aesebu_format_names[] =
{
	"N/A",
	"S/PDIF",
	"AES/EBU",
};

static int snd_asihpi_aesebu_format_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item =
			uinfo->value.enumerated.items - 1;

	strcpy(uinfo->value.enumerated.name,
		asihpi_aesebu_format_names[uinfo->value.enumerated.item]);

	return 0;
}

static int snd_asihpi_aesebu_format_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol,
			u16 (*func)(const struct hpi_hsubsys *, u32, u16 *))
{
	u32 h_control = kcontrol->private_value;
	u16 source, err;

	err = func(ss, h_control, &source);

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
			 u16 (*func)(const struct hpi_hsubsys *, u32, u16))
{
	u32 h_control = kcontrol->private_value;

	/* default to S/PDIF */
	u16 source = HPI_AESEBU_FORMAT_SPDIF;

	if (ucontrol->value.enumerated.item[0] == 1)
		source = HPI_AESEBU_FORMAT_SPDIF;
	if (ucontrol->value.enumerated.item[0] == 2)
		source = HPI_AESEBU_FORMAT_AESEBU;

	if (func(ss, h_control, source) != 0)
		return -EINVAL;

	return 1;
}

static int snd_asihpi_aesebu_rx_format_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_get(kcontrol, ucontrol,
					HPI_AESEBU__receiver_get_format);
}

static int snd_asihpi_aesebu_rx_format_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_put(kcontrol, ucontrol,
					HPI_AESEBU__receiver_set_format);
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

	hpi_handle_error(HPI_AESEBU__receiver_get_error_status(
				ss, h_control, &status));
	ucontrol->value.integer.value[0] = status;
	return 0;
}

static int __devinit snd_asihpi_aesebu_rx_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "format");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_aesebu_format_info;
	snd_control.get = snd_asihpi_aesebu_rx_format_get;
	snd_control.put = snd_asihpi_aesebu_rx_format_put;


	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;

	asihpi_ctl_init(&snd_control, hpi_ctl, "status");
	snd_control.access =
	    SNDRV_CTL_ELEM_ACCESS_VOLATILE | SNDRV_CTL_ELEM_ACCESS_READ;
	snd_control.info = snd_asihpi_aesebu_rxstatus_info;
	snd_control.get = snd_asihpi_aesebu_rxstatus_get;

	return ctl_add(card, &snd_control, asihpi);
}

static int snd_asihpi_aesebu_tx_format_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_get(kcontrol, ucontrol,
					HPI_AESEBU__transmitter_get_format);
}

static int snd_asihpi_aesebu_tx_format_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol) {
	return snd_asihpi_aesebu_format_put(kcontrol, ucontrol,
					HPI_AESEBU__transmitter_set_format);
}


static int __devinit snd_asihpi_aesebu_tx_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "format");
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
		err = hpi_tuner_query_gain(ss, h_control,
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

	hpi_handle_error(hpi_tuner_get_gain(ss, h_control, &gain));
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
	hpi_handle_error(hpi_tuner_set_gain(ss, h_control, gain));

	return 1;
}

/* Band  */

static int asihpi_tuner_band_query(struct snd_kcontrol *kcontrol,
					u16 *band_list, u32 len) {
	u32 h_control = kcontrol->private_value;
	u16 err = 0;
	u32 i;

	for (i = 0; i < len; i++) {
		err = hpi_tuner_query_band(ss,
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

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = num_bands;

	if (num_bands > 0) {
		if (uinfo->value.enumerated.item >=
					uinfo->value.enumerated.items)
			uinfo->value.enumerated.item =
				uinfo->value.enumerated.items - 1;

		strcpy(uinfo->value.enumerated.name,
			asihpi_tuner_band_names[
				tuner_bands[uinfo->value.enumerated.item]]);

	}
	return 0;
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

	hpi_handle_error(hpi_tuner_get_band(ss, h_control, &band));

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
	u16 band;
	u16 tuner_bands[HPI_TUNER_BAND_LAST];
	u32 num_bands = 0;

	num_bands = asihpi_tuner_band_query(kcontrol, tuner_bands,
			HPI_TUNER_BAND_LAST);

	band = tuner_bands[ucontrol->value.enumerated.item[0]];
	hpi_handle_error(hpi_tuner_set_band(ss, h_control, band));

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
			err = hpi_tuner_query_frequency(ss, h_control,
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

	hpi_handle_error(hpi_tuner_get_frequency(ss, h_control, &freq));
	ucontrol->value.integer.value[0] = freq;

	return 0;
}

static int snd_asihpi_tuner_freq_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u32 freq;

	freq = ucontrol->value.integer.value[0];
	hpi_handle_error(hpi_tuner_set_frequency(ss, h_control, freq));

	return 1;
}

/* Tuner control group initializer  */
static int __devinit snd_asihpi_tuner_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	snd_control.private_value = hpi_ctl->h_control;
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;

	if (!hpi_tuner_get_gain(ss, hpi_ctl->h_control, NULL)) {
		asihpi_ctl_init(&snd_control, hpi_ctl, "gain");
		snd_control.info = snd_asihpi_tuner_gain_info;
		snd_control.get = snd_asihpi_tuner_gain_get;
		snd_control.put = snd_asihpi_tuner_gain_put;

		if (ctl_add(card, &snd_control, asihpi) < 0)
			return -EINVAL;
	}

	asihpi_ctl_init(&snd_control, hpi_ctl, "band");
	snd_control.info = snd_asihpi_tuner_band_info;
	snd_control.get = snd_asihpi_tuner_band_get;
	snd_control.put = snd_asihpi_tuner_band_put;

	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;

	asihpi_ctl_init(&snd_control, hpi_ctl, "freq");
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
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = HPI_MAX_CHANNELS;
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

	err = hpi_meter_get_peak(ss, h_control, an_gain_mB);

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

static int __devinit snd_asihpi_meter_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl, int subidx)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "meter");
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
		err = hpi_multiplexer_query_source(ss, h_control, s,
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
	    hpi_multiplexer_query_source(ss, h_control,
					uinfo->value.enumerated.item,
					&src_node_type, &src_node_index);

	sprintf(uinfo->value.enumerated.name, "%s %d",
		asihpi_src_names[src_node_type - HPI_SOURCENODE_BASE],
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

	hpi_handle_error(hpi_multiplexer_get_source(ss, h_control,
				&source_type, &source_index));
	/* Should cache this search result! */
	for (s = 0; s < 256; s++) {
		if (hpi_multiplexer_query_source(ss, h_control, s,
					    &src_node_type, &src_node_index))
			break;

		if ((source_type == src_node_type)
		    && (source_index == src_node_index)) {
			ucontrol->value.enumerated.item[0] = s;
			return 0;
		}
	}
	snd_printd(KERN_WARNING
		"control %x failed to match mux source %hu %hu\n",
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

	e = hpi_multiplexer_query_source(ss, h_control,
				    ucontrol->value.enumerated.item[0],
				    &source_type, &source_index);
	if (!e)
		hpi_handle_error(
			hpi_multiplexer_set_source(ss, h_control,
						source_type, source_index));
	return change;
}


static int  __devinit snd_asihpi_mux_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

#if ASI_STYLE_NAMES
	asihpi_ctl_init(&snd_control, hpi_ctl, "multiplexer");
#else
	asihpi_ctl_init(&snd_control, hpi_ctl, "route");
#endif
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
	static char *mode_names[HPI_CHANNEL_MODE_LAST] = {
		"normal", "swap",
		"from_left", "from_right",
		"to_left", "to_right"
	};

	u32 h_control = kcontrol->private_value;
	u16 mode;
	int i;

	/* HPI channel mode values can be from 1 to 6
	Some adapters only support a contiguous subset
	*/
	for (i = 0; i < HPI_CHANNEL_MODE_LAST; i++)
		if (hpi_channel_mode_query_mode(
			ss,  h_control, i, &mode))
			break;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = i;

	if (uinfo->value.enumerated.item >= i)
		uinfo->value.enumerated.item = i - 1;

	strcpy(uinfo->value.enumerated.name,
	       mode_names[uinfo->value.enumerated.item]);

	return 0;
}

static int snd_asihpi_cmode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u32 h_control = kcontrol->private_value;
	u16 mode;

	if (hpi_channel_mode_get(ss, h_control, &mode))
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

	hpi_handle_error(hpi_channel_mode_set(ss, h_control,
			   ucontrol->value.enumerated.item[0] + 1));
	return change;
}


static int __devinit snd_asihpi_cmode_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	asihpi_ctl_init(&snd_control, hpi_ctl, "channel mode");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	snd_control.info = snd_asihpi_cmode_info;
	snd_control.get = snd_asihpi_cmode_get;
	snd_control.put = snd_asihpi_cmode_put;

	return ctl_add(card, &snd_control, asihpi);
}

/*------------------------------------------------------------
   Sampleclock source  controls
 ------------------------------------------------------------*/

static char *sampleclock_sources[MAX_CLOCKSOURCES] =
    { "N/A", "local PLL", "AES/EBU sync", "word external", "word header",
	  "SMPTE", "AES/EBU in1", "auto", "network", "invalid",
	  "prev module",
	  "AES/EBU in2", "AES/EBU in3", "AES/EBU in4", "AES/EBU in5",
	  "AES/EBU in6", "AES/EBU in7", "AES/EBU in8"};



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
	if (hpi_sample_clock_get_source(ss, h_control, &source))
		source = 0;

	if (source == HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT)
		if (hpi_sample_clock_get_source_index(ss, h_control, &srcindex))
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
	int change, item;
	u32 h_control = kcontrol->private_value;

	change = 1;
	item = ucontrol->value.enumerated.item[0];
	if (item >= clkcache->count)
		item = clkcache->count-1;

	hpi_handle_error(hpi_sample_clock_set_source(ss,
				h_control, clkcache->s[item].source));

	if (clkcache->s[item].source == HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT)
		hpi_handle_error(hpi_sample_clock_set_source_index(ss,
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

	e = hpi_sample_clock_get_local_rate(ss, h_control, &rate);
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
	hpi_handle_error(hpi_sample_clock_set_local_rate(ss, h_control,
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

	e = hpi_sample_clock_get_sample_rate(ss, h_control, &rate);
	if (!e)
		ucontrol->value.integer.value[0] = rate;
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int __devinit snd_asihpi_sampleclock_add(struct snd_card_asihpi *asihpi,
					struct hpi_control *hpi_ctl)
{
	struct snd_card *card = asihpi->card;
	struct snd_kcontrol_new snd_control;

	struct clk_cache *clkcache = &asihpi->cc;
	u32 hSC =  hpi_ctl->h_control;
	int has_aes_in = 0;
	int i, j;
	u16 source;

	snd_control.private_value = hpi_ctl->h_control;

	clkcache->has_local = 0;

	for (i = 0; i <= HPI_SAMPLECLOCK_SOURCE_LAST; i++) {
		if  (hpi_sample_clock_query_source(ss, hSC,
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
			if (hpi_sample_clock_query_source_index(ss, hSC,
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

	asihpi_ctl_init(&snd_control, hpi_ctl, "source");
	snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE ;
	snd_control.info = snd_asihpi_clksrc_info;
	snd_control.get = snd_asihpi_clksrc_get;
	snd_control.put = snd_asihpi_clksrc_put;
	if (ctl_add(card, &snd_control, asihpi) < 0)
		return -EINVAL;


	if (clkcache->has_local) {
		asihpi_ctl_init(&snd_control, hpi_ctl, "local_rate");
		snd_control.access = SNDRV_CTL_ELEM_ACCESS_READWRITE ;
		snd_control.info = snd_asihpi_clklocal_info;
		snd_control.get = snd_asihpi_clklocal_get;
		snd_control.put = snd_asihpi_clklocal_put;


		if (ctl_add(card, &snd_control, asihpi) < 0)
			return -EINVAL;
	}

	asihpi_ctl_init(&snd_control, hpi_ctl, "rate");
	snd_control.access =
	    SNDRV_CTL_ELEM_ACCESS_VOLATILE | SNDRV_CTL_ELEM_ACCESS_READ;
	snd_control.info = snd_asihpi_clkrate_info;
	snd_control.get = snd_asihpi_clkrate_get;

	return ctl_add(card, &snd_control, asihpi);
}
/*------------------------------------------------------------
   Mixer
 ------------------------------------------------------------*/

static int __devinit snd_card_asihpi_mixer_new(struct snd_card_asihpi *asihpi)
{
	struct snd_card *card = asihpi->card;
	unsigned int idx = 0;
	unsigned int subindex = 0;
	int err;
	struct hpi_control hpi_ctl, prev_ctl;

	if (snd_BUG_ON(!asihpi))
		return -EINVAL;
	strcpy(card->mixername, "asihpi mixer");

	err =
	    hpi_mixer_open(ss, asihpi->adapter_index,
			  &asihpi->h_mixer);
	hpi_handle_error(err);
	if (err)
		return -err;

	memset(&prev_ctl, 0, sizeof(prev_ctl));
	prev_ctl.control_type = -1;

	for (idx = 0; idx < 2000; idx++) {
		err = hpi_mixer_get_control_by_index(
				ss, asihpi->h_mixer,
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
					snd_printk(KERN_INFO
						   "disabled HPI control(%d)\n",
						   idx);
				continue;
			} else
				break;

		}

		hpi_ctl.src_node_type -= HPI_SOURCENODE_BASE;
		hpi_ctl.dst_node_type -= HPI_DESTNODE_BASE;

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
				snd_printk(KERN_INFO
					"untranslated HPI control"
					"(%d) %d %d %d %d %d\n",
					idx,
					hpi_ctl.control_type,
					hpi_ctl.src_node_type,
					hpi_ctl.src_node_index,
					hpi_ctl.dst_node_type,
					hpi_ctl.dst_node_index);
			continue;
		};
		if (err < 0)
			return err;
	}
	if (HPI_ERROR_INVALID_OBJ_INDEX != err)
		hpi_handle_error(err);

	snd_printk(KERN_INFO "%d mixer controls found\n", idx);

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
	u16 version;
	u32 h_control;
	u32 rate = 0;
	u16 source = 0;
	int err;

	snd_iprintf(buffer, "ASIHPI driver proc file\n");
	snd_iprintf(buffer,
		"adapter ID=%4X\n_index=%d\n"
		"num_outstreams=%d\n_num_instreams=%d\n",
		asihpi->type, asihpi->adapter_index,
		asihpi->num_outstreams, asihpi->num_instreams);

	version = asihpi->version;
	snd_iprintf(buffer,
		"serial#=%d\n_hw version %c%d\nDSP code version %03d\n",
		asihpi->serial_number, ((version >> 3) & 0xf) + 'A',
		version & 0x7,
		((version >> 13) * 100) + ((version >> 7) & 0x3f));

	err = hpi_mixer_get_control(ss, asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err) {
		err = hpi_sample_clock_get_sample_rate(ss,
					h_control, &rate);
		err += hpi_sample_clock_get_source(ss, h_control, &source);

		if (!err)
			snd_iprintf(buffer, "sample_clock=%d_hz, source %s\n",
			rate, sampleclock_sources[source]);
	}

}


static void __devinit snd_asihpi_proc_init(struct snd_card_asihpi *asihpi)
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
static int __devinit snd_asihpi_hpi_new(struct snd_card_asihpi *asihpi,
	int device, struct snd_hwdep **rhwdep)
{
	struct snd_hwdep *hw;
	int err;

	if (rhwdep)
		*rhwdep = NULL;
	err = snd_hwdep_new(asihpi->card, "HPI", device, &hw);
	if (err < 0)
		return err;
	strcpy(hw->name, "asihpi (HPI)");
	hw->iface = SNDRV_HWDEP_IFACE_LAST;
	hw->ops.open = snd_asihpi_hpi_open;
	hw->ops.ioctl = snd_asihpi_hpi_ioctl;
	hw->ops.release = snd_asihpi_hpi_release;
	hw->private_data = asihpi;
	if (rhwdep)
		*rhwdep = hw;
	return 0;
}

/*------------------------------------------------------------
   CARD
 ------------------------------------------------------------*/
static int __devinit snd_asihpi_probe(struct pci_dev *pci_dev,
				       const struct pci_device_id *pci_id)
{
	int err;

	u16 version;
	int pcm_substreams;

	struct hpi_adapter *hpi_card;
	struct snd_card *card;
	struct snd_card_asihpi *asihpi;

	u32 h_control;
	u32 h_stream;

	static int dev;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	/* Should this be enable[hpi_card->index] ? */
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = asihpi_adapter_probe(pci_dev, pci_id);
	if (err < 0)
		return err;

	hpi_card = pci_get_drvdata(pci_dev);
	/* first try to give the card the same index as its hardware index */
	err = snd_card_create(hpi_card->index,
			      id[hpi_card->index], THIS_MODULE,
			      sizeof(struct snd_card_asihpi),
			      &card);
	if (err < 0) {
		/* if that fails, try the default index==next available */
		err =
		    snd_card_create(index[dev], id[dev],
				    THIS_MODULE,
				    sizeof(struct snd_card_asihpi),
				    &card);
		if (err < 0)
			return err;
		snd_printk(KERN_WARNING
			"**** WARNING **** adapter index %d->ALSA index %d\n",
			hpi_card->index, card->number);
	}

	asihpi = (struct snd_card_asihpi *) card->private_data;
	asihpi->card = card;
	asihpi->pci = hpi_card->pci;
	asihpi->adapter_index = hpi_card->index;
	hpi_handle_error(hpi_adapter_get_info(ss,
				 asihpi->adapter_index,
				 &asihpi->num_outstreams,
				 &asihpi->num_instreams,
				 &asihpi->version,
				 &asihpi->serial_number, &asihpi->type));

	version = asihpi->version;
	snd_printk(KERN_INFO "adapter ID=%4X index=%d num_outstreams=%d "
			"num_instreams=%d S/N=%d\n"
			"hw version %c%d DSP code version %03d\n",
			asihpi->type, asihpi->adapter_index,
			asihpi->num_outstreams,
			asihpi->num_instreams, asihpi->serial_number,
			((version >> 3) & 0xf) + 'A',
			version & 0x7,
			((version >> 13) * 100) + ((version >> 7) & 0x3f));

	pcm_substreams = asihpi->num_outstreams;
	if (pcm_substreams < asihpi->num_instreams)
		pcm_substreams = asihpi->num_instreams;

	err = hpi_adapter_get_property(ss, asihpi->adapter_index,
		HPI_ADAPTER_PROPERTY_CAPS1,
		NULL, &asihpi->support_grouping);
	if (err)
		asihpi->support_grouping = 0;

	err = hpi_adapter_get_property(ss, asihpi->adapter_index,
		HPI_ADAPTER_PROPERTY_CAPS2,
		&asihpi->support_mrx, NULL);
	if (err)
		asihpi->support_mrx = 0;

	err = hpi_adapter_get_property(ss, asihpi->adapter_index,
		HPI_ADAPTER_PROPERTY_INTERVAL,
		NULL, &asihpi->update_interval_frames);
	if (err)
		asihpi->update_interval_frames = 512;

	hpi_handle_error(hpi_instream_open(ss, asihpi->adapter_index,
			     0, &h_stream));

	err = hpi_instream_host_buffer_free(ss, h_stream);
	asihpi->support_mmap = (!err);

	hpi_handle_error(hpi_instream_close(ss, h_stream));

	err = hpi_adapter_get_property(ss, asihpi->adapter_index,
		HPI_ADAPTER_PROPERTY_CURCHANNELS,
		&asihpi->in_max_chans, &asihpi->out_max_chans);
	if (err) {
		asihpi->in_max_chans = 2;
		asihpi->out_max_chans = 2;
	}

	snd_printk(KERN_INFO "supports mmap:%d grouping:%d mrx:%d\n",
			asihpi->support_mmap,
			asihpi->support_grouping,
			asihpi->support_mrx
	      );


	err = snd_card_asihpi_pcm_new(asihpi, 0, pcm_substreams);
	if (err < 0) {
		snd_printk(KERN_ERR "pcm_new failed\n");
		goto __nodev;
	}
	err = snd_card_asihpi_mixer_new(asihpi);
	if (err < 0) {
		snd_printk(KERN_ERR "mixer_new failed\n");
		goto __nodev;
	}

	err = hpi_mixer_get_control(ss, asihpi->h_mixer,
				  HPI_SOURCENODE_CLOCK_SOURCE, 0, 0, 0,
				  HPI_CONTROL_SAMPLECLOCK, &h_control);

	if (!err)
		err = hpi_sample_clock_set_local_rate(
			ss, h_control, adapter_fs);

	snd_asihpi_proc_init(asihpi);

	/* always create, can be enabled or disabled dynamically
	    by enable_hwdep  module param*/
	snd_asihpi_hpi_new(asihpi, 0, NULL);

	if (asihpi->support_mmap)
		strcpy(card->driver, "ASIHPI-MMAP");
	else
		strcpy(card->driver, "ASIHPI");

	sprintf(card->shortname, "AudioScience ASI%4X", asihpi->type);
	sprintf(card->longname, "%s %i",
			card->shortname, asihpi->adapter_index);
	err = snd_card_register(card);
	if (!err) {
		hpi_card->snd_card_asihpi = card;
		dev++;
		return 0;
	}
__nodev:
	snd_card_free(card);
	snd_printk(KERN_ERR "snd_asihpi_probe error %d\n", err);
	return err;

}

static void __devexit snd_asihpi_remove(struct pci_dev *pci_dev)
{
	struct hpi_adapter *hpi_card = pci_get_drvdata(pci_dev);

	snd_card_free(hpi_card->snd_card_asihpi);
	hpi_card->snd_card_asihpi = NULL;
	asihpi_adapter_remove(pci_dev);
}

static DEFINE_PCI_DEVICE_TABLE(asihpi_pci_tbl) = {
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
	.name = "asihpi",
	.id_table = asihpi_pci_tbl,
	.probe = snd_asihpi_probe,
	.remove = __devexit_p(snd_asihpi_remove),
#ifdef CONFIG_PM
/*	.suspend = snd_asihpi_suspend,
	.resume = snd_asihpi_resume, */
#endif
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

