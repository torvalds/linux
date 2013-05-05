/*
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

#include <linux/init.h>
#include <linux/usb.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "helper.h"
#include "card.h"
#include "endpoint.h"
#include "proc.h"

/* convert our full speed USB rate into sampling rate in Hz */
static inline unsigned get_full_speed_hz(unsigned int usb_rate)
{
	return (usb_rate * 125 + (1 << 12)) >> 13;
}

/* convert our high speed USB rate into sampling rate in Hz */
static inline unsigned get_high_speed_hz(unsigned int usb_rate)
{
	return (usb_rate * 125 + (1 << 9)) >> 10;
}

/*
 * common proc files to show the usb device info
 */
static void proc_audio_usbbus_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_audio *chip = entry->private_data;
	if (!chip->shutdown)
		snd_iprintf(buffer, "%03d/%03d\n", chip->dev->bus->busnum, chip->dev->devnum);
}

static void proc_audio_usbid_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_audio *chip = entry->private_data;
	if (!chip->shutdown)
		snd_iprintf(buffer, "%04x:%04x\n", 
			    USB_ID_VENDOR(chip->usb_id),
			    USB_ID_PRODUCT(chip->usb_id));
}

void snd_usb_audio_create_proc(struct snd_usb_audio *chip)
{
	struct snd_info_entry *entry;
	if (!snd_card_proc_new(chip->card, "usbbus", &entry))
		snd_info_set_text_ops(entry, chip, proc_audio_usbbus_read);
	if (!snd_card_proc_new(chip->card, "usbid", &entry))
		snd_info_set_text_ops(entry, chip, proc_audio_usbid_read);
}

/*
 * proc interface for list the supported pcm formats
 */
static void proc_dump_substream_formats(struct snd_usb_substream *subs, struct snd_info_buffer *buffer)
{
	struct audioformat *fp;
	static char *sync_types[4] = {
		"NONE", "ASYNC", "ADAPTIVE", "SYNC"
	};

	list_for_each_entry(fp, &subs->fmt_list, list) {
		snd_pcm_format_t fmt;

		snd_iprintf(buffer, "  Interface %d\n", fp->iface);
		snd_iprintf(buffer, "    Altset %d\n", fp->altsetting);
		snd_iprintf(buffer, "    Format:");
		for (fmt = 0; fmt <= SNDRV_PCM_FORMAT_LAST; ++fmt)
			if (fp->formats & pcm_format_to_bits(fmt))
				snd_iprintf(buffer, " %s",
					    snd_pcm_format_name(fmt));
		snd_iprintf(buffer, "\n");
		snd_iprintf(buffer, "    Channels: %d\n", fp->channels);
		snd_iprintf(buffer, "    Endpoint: %d %s (%s)\n",
			    fp->endpoint & USB_ENDPOINT_NUMBER_MASK,
			    fp->endpoint & USB_DIR_IN ? "IN" : "OUT",
			    sync_types[(fp->ep_attr & USB_ENDPOINT_SYNCTYPE) >> 2]);
		if (fp->rates & SNDRV_PCM_RATE_CONTINUOUS) {
			snd_iprintf(buffer, "    Rates: %d - %d (continuous)\n",
				    fp->rate_min, fp->rate_max);
		} else {
			unsigned int i;
			snd_iprintf(buffer, "    Rates: ");
			for (i = 0; i < fp->nr_rates; i++) {
				if (i > 0)
					snd_iprintf(buffer, ", ");
				snd_iprintf(buffer, "%d", fp->rate_table[i]);
			}
			snd_iprintf(buffer, "\n");
		}
		if (subs->speed != USB_SPEED_FULL)
			snd_iprintf(buffer, "    Data packet interval: %d us\n",
				    125 * (1 << fp->datainterval));
		// snd_iprintf(buffer, "    Max Packet Size = %d\n", fp->maxpacksize);
		// snd_iprintf(buffer, "    EP Attribute = %#x\n", fp->attributes);
	}
}

static void proc_dump_ep_status(struct snd_usb_substream *subs,
				struct snd_usb_endpoint *ep,
				struct snd_info_buffer *buffer)
{
	if (!ep)
		return;
	snd_iprintf(buffer, "    Packet Size = %d\n", ep->curpacksize);
	snd_iprintf(buffer, "    Momentary freq = %u Hz (%#x.%04x)\n",
		    subs->speed == USB_SPEED_FULL
		    ? get_full_speed_hz(ep->freqm)
		    : get_high_speed_hz(ep->freqm),
		    ep->freqm >> 16, ep->freqm & 0xffff);
	if (ep->freqshift != INT_MIN) {
		int res = 16 - ep->freqshift;
		snd_iprintf(buffer, "    Feedback Format = %d.%d\n",
			    (ep->syncmaxsize > 3 ? 32 : 24) - res, res);
	}
}

static void proc_dump_substream_status(struct snd_usb_substream *subs, struct snd_info_buffer *buffer)
{
	if (subs->running) {
		snd_iprintf(buffer, "  Status: Running\n");
		snd_iprintf(buffer, "    Interface = %d\n", subs->interface);
		snd_iprintf(buffer, "    Altset = %d\n", subs->altset_idx);
		proc_dump_ep_status(subs, subs->data_endpoint, buffer);
		proc_dump_ep_status(subs, subs->sync_endpoint, buffer);
	} else {
		snd_iprintf(buffer, "  Status: Stop\n");
	}
}

static void proc_pcm_format_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_usb_stream *stream = entry->private_data;

	snd_iprintf(buffer, "%s : %s\n", stream->chip->card->longname, stream->pcm->name);

	if (stream->substream[SNDRV_PCM_STREAM_PLAYBACK].num_formats) {
		snd_iprintf(buffer, "\nPlayback:\n");
		proc_dump_substream_status(&stream->substream[SNDRV_PCM_STREAM_PLAYBACK], buffer);
		proc_dump_substream_formats(&stream->substream[SNDRV_PCM_STREAM_PLAYBACK], buffer);
	}
	if (stream->substream[SNDRV_PCM_STREAM_CAPTURE].num_formats) {
		snd_iprintf(buffer, "\nCapture:\n");
		proc_dump_substream_status(&stream->substream[SNDRV_PCM_STREAM_CAPTURE], buffer);
		proc_dump_substream_formats(&stream->substream[SNDRV_PCM_STREAM_CAPTURE], buffer);
	}
}

void snd_usb_proc_pcm_format_add(struct snd_usb_stream *stream)
{
	struct snd_info_entry *entry;
	char name[32];
	struct snd_card *card = stream->chip->card;

	sprintf(name, "stream%d", stream->pcm_index);
	if (!snd_card_proc_new(card, name, &entry))
		snd_info_set_text_ops(entry, stream, proc_pcm_format_read);
}

