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
 */


#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "card.h"
#include "proc.h"
#include "quirks.h"
#include "endpoint.h"
#include "pcm.h"
#include "helper.h"
#include "format.h"
#include "clock.h"
#include "stream.h"

/*
 * free a substream
 */
static void free_substream(struct snd_usb_substream *subs)
{
	struct list_head *p, *n;

	if (!subs->num_formats)
		return; /* not initialized */
	list_for_each_safe(p, n, &subs->fmt_list) {
		struct audioformat *fp = list_entry(p, struct audioformat, list);
		kfree(fp->rate_table);
		kfree(fp);
	}
	kfree(subs->rate_list.list);
}


/*
 * free a usb stream instance
 */
static void snd_usb_audio_stream_free(struct snd_usb_stream *stream)
{
	free_substream(&stream->substream[0]);
	free_substream(&stream->substream[1]);
	list_del(&stream->list);
	kfree(stream);
}

static void snd_usb_audio_pcm_free(struct snd_pcm *pcm)
{
	struct snd_usb_stream *stream = pcm->private_data;
	if (stream) {
		stream->pcm = NULL;
		snd_usb_audio_stream_free(stream);
	}
}


/*
 * add this endpoint to the chip instance.
 * if a stream with the same endpoint already exists, append to it.
 * if not, create a new pcm stream.
 */
int snd_usb_add_audio_stream(struct snd_usb_audio *chip,
			     int stream,
			     struct audioformat *fp)
{
	struct list_head *p;
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs;
	struct snd_pcm *pcm;
	int err;

	list_for_each(p, &chip->pcm_list) {
		as = list_entry(p, struct snd_usb_stream, list);
		if (as->fmt_type != fp->fmt_type)
			continue;
		subs = &as->substream[stream];
		if (!subs->endpoint)
			continue;
		if (subs->endpoint == fp->endpoint) {
			list_add_tail(&fp->list, &subs->fmt_list);
			subs->num_formats++;
			subs->formats |= fp->formats;
			return 0;
		}
	}
	/* look for an empty stream */
	list_for_each(p, &chip->pcm_list) {
		as = list_entry(p, struct snd_usb_stream, list);
		if (as->fmt_type != fp->fmt_type)
			continue;
		subs = &as->substream[stream];
		if (subs->endpoint)
			continue;
		err = snd_pcm_new_stream(as->pcm, stream, 1);
		if (err < 0)
			return err;
		snd_usb_init_substream(as, stream, fp);
		return 0;
	}

	/* create a new pcm */
	as = kzalloc(sizeof(*as), GFP_KERNEL);
	if (!as)
		return -ENOMEM;
	as->pcm_index = chip->pcm_devs;
	as->chip = chip;
	as->fmt_type = fp->fmt_type;
	err = snd_pcm_new(chip->card, "USB Audio", chip->pcm_devs,
			  stream == SNDRV_PCM_STREAM_PLAYBACK ? 1 : 0,
			  stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1,
			  &pcm);
	if (err < 0) {
		kfree(as);
		return err;
	}
	as->pcm = pcm;
	pcm->private_data = as;
	pcm->private_free = snd_usb_audio_pcm_free;
	pcm->info_flags = 0;
	if (chip->pcm_devs > 0)
		sprintf(pcm->name, "USB Audio #%d", chip->pcm_devs);
	else
		strcpy(pcm->name, "USB Audio");

	snd_usb_init_substream(as, stream, fp);

	list_add(&as->list, &chip->pcm_list);
	chip->pcm_devs++;

	snd_usb_proc_pcm_format_add(as);

	return 0;
}

static int parse_uac_endpoint_attributes(struct snd_usb_audio *chip,
					 struct usb_host_interface *alts,
					 int protocol, int iface_no)
{
	/* parsed with a v1 header here. that's ok as we only look at the
	 * header first which is the same for both versions */
	struct uac_iso_endpoint_descriptor *csep;
	struct usb_interface_descriptor *altsd = get_iface_desc(alts);
	int attributes = 0;

	csep = snd_usb_find_desc(alts->endpoint[0].extra, alts->endpoint[0].extralen, NULL, USB_DT_CS_ENDPOINT);

	/* Creamware Noah has this descriptor after the 2nd endpoint */
	if (!csep && altsd->bNumEndpoints >= 2)
		csep = snd_usb_find_desc(alts->endpoint[1].extra, alts->endpoint[1].extralen, NULL, USB_DT_CS_ENDPOINT);

	/*
	 * If we can't locate the USB_DT_CS_ENDPOINT descriptor in the extra
	 * bytes after the first endpoint, go search the entire interface.
	 * Some devices have it directly *before* the standard endpoint.
	 */
	if (!csep)
		csep = snd_usb_find_desc(alts->extra, alts->extralen, NULL, USB_DT_CS_ENDPOINT);

	if (!csep || csep->bLength < 7 ||
	    csep->bDescriptorSubtype != UAC_EP_GENERAL) {
		snd_printk(KERN_WARNING "%d:%u:%d : no or invalid"
			   " class specific endpoint descriptor\n",
			   chip->dev->devnum, iface_no,
			   altsd->bAlternateSetting);
		return 0;
	}

	if (protocol == UAC_VERSION_1) {
		attributes = csep->bmAttributes;
	} else {
		struct uac2_iso_endpoint_descriptor *csep2 =
			(struct uac2_iso_endpoint_descriptor *) csep;

		attributes = csep->bmAttributes & UAC_EP_CS_ATTR_FILL_MAX;

		/* emulate the endpoint attributes of a v1 device */
		if (csep2->bmControls & UAC2_CONTROL_PITCH)
			attributes |= UAC_EP_CS_ATTR_PITCH_CONTROL;
	}

	return attributes;
}

static struct uac2_input_terminal_descriptor *
	snd_usb_find_input_terminal_descriptor(struct usb_host_interface *ctrl_iface,
					       int terminal_id)
{
	struct uac2_input_terminal_descriptor *term = NULL;

	while ((term = snd_usb_find_csint_desc(ctrl_iface->extra,
					       ctrl_iface->extralen,
					       term, UAC_INPUT_TERMINAL))) {
		if (term->bTerminalID == terminal_id)
			return term;
	}

	return NULL;
}

static struct uac2_output_terminal_descriptor *
	snd_usb_find_output_terminal_descriptor(struct usb_host_interface *ctrl_iface,
						int terminal_id)
{
	struct uac2_output_terminal_descriptor *term = NULL;

	while ((term = snd_usb_find_csint_desc(ctrl_iface->extra,
					       ctrl_iface->extralen,
					       term, UAC_OUTPUT_TERMINAL))) {
		if (term->bTerminalID == terminal_id)
			return term;
	}

	return NULL;
}

int snd_usb_parse_audio_interface(struct snd_usb_audio *chip, int iface_no)
{
	struct usb_device *dev;
	struct usb_interface *iface;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	int i, altno, err, stream;
	int format = 0, num_channels = 0;
	struct audioformat *fp = NULL;
	int num, protocol, clock = 0;
	struct uac_format_type_i_continuous_descriptor *fmt;

	dev = chip->dev;

	/* parse the interface's altsettings */
	iface = usb_ifnum_to_if(dev, iface_no);

	num = iface->num_altsetting;

	/*
	 * Dallas DS4201 workaround: It presents 5 altsettings, but the last
	 * one misses syncpipe, and does not produce any sound.
	 */
	if (chip->usb_id == USB_ID(0x04fa, 0x4201))
		num = 4;

	for (i = 0; i < num; i++) {
		alts = &iface->altsetting[i];
		altsd = get_iface_desc(alts);
		protocol = altsd->bInterfaceProtocol;
		/* skip invalid one */
		if ((altsd->bInterfaceClass != USB_CLASS_AUDIO &&
		     altsd->bInterfaceClass != USB_CLASS_VENDOR_SPEC) ||
		    (altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING &&
		     altsd->bInterfaceSubClass != USB_SUBCLASS_VENDOR_SPEC) ||
		    altsd->bNumEndpoints < 1 ||
		    le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize) == 0)
			continue;
		/* must be isochronous */
		if ((get_endpoint(alts, 0)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
		    USB_ENDPOINT_XFER_ISOC)
			continue;
		/* check direction */
		stream = (get_endpoint(alts, 0)->bEndpointAddress & USB_DIR_IN) ?
			SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
		altno = altsd->bAlternateSetting;

		if (snd_usb_apply_interface_quirk(chip, iface_no, altno))
			continue;

		/* get audio formats */
		switch (protocol) {
		default:
			snd_printdd(KERN_WARNING "%d:%u:%d: unknown interface protocol %#02x, assuming v1\n",
				    dev->devnum, iface_no, altno, protocol);
			protocol = UAC_VERSION_1;
			/* fall through */

		case UAC_VERSION_1: {
			struct uac1_as_header_descriptor *as =
				snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);

			if (!as) {
				snd_printk(KERN_ERR "%d:%u:%d : UAC_AS_GENERAL descriptor not found\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			if (as->bLength < sizeof(*as)) {
				snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_AS_GENERAL desc\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			format = le16_to_cpu(as->wFormatTag); /* remember the format value */
			break;
		}

		case UAC_VERSION_2: {
			struct uac2_input_terminal_descriptor *input_term;
			struct uac2_output_terminal_descriptor *output_term;
			struct uac2_as_header_descriptor *as =
				snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);

			if (!as) {
				snd_printk(KERN_ERR "%d:%u:%d : UAC_AS_GENERAL descriptor not found\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			if (as->bLength < sizeof(*as)) {
				snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_AS_GENERAL desc\n",
					   dev->devnum, iface_no, altno);
				continue;
			}

			num_channels = as->bNrChannels;
			format = le32_to_cpu(as->bmFormats);

			/* lookup the terminal associated to this interface
			 * to extract the clock */
			input_term = snd_usb_find_input_terminal_descriptor(chip->ctrl_intf,
									    as->bTerminalLink);
			if (input_term) {
				clock = input_term->bCSourceID;
				break;
			}

			output_term = snd_usb_find_output_terminal_descriptor(chip->ctrl_intf,
									      as->bTerminalLink);
			if (output_term) {
				clock = output_term->bCSourceID;
				break;
			}

			snd_printk(KERN_ERR "%d:%u:%d : bogus bTerminalLink %d\n",
				   dev->devnum, iface_no, altno, as->bTerminalLink);
			continue;
		}
		}

		/* get format type */
		fmt = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_FORMAT_TYPE);
		if (!fmt) {
			snd_printk(KERN_ERR "%d:%u:%d : no UAC_FORMAT_TYPE desc\n",
				   dev->devnum, iface_no, altno);
			continue;
		}
		if (((protocol == UAC_VERSION_1) && (fmt->bLength < 8)) ||
		    ((protocol == UAC_VERSION_2) && (fmt->bLength < 6))) {
			snd_printk(KERN_ERR "%d:%u:%d : invalid UAC_FORMAT_TYPE desc\n",
				   dev->devnum, iface_no, altno);
			continue;
		}

		/*
		 * Blue Microphones workaround: The last altsetting is identical
		 * with the previous one, except for a larger packet size, but
		 * is actually a mislabeled two-channel setting; ignore it.
		 */
		if (fmt->bNrChannels == 1 &&
		    fmt->bSubframeSize == 2 &&
		    altno == 2 && num == 3 &&
		    fp && fp->altsetting == 1 && fp->channels == 1 &&
		    fp->formats == SNDRV_PCM_FMTBIT_S16_LE &&
		    protocol == UAC_VERSION_1 &&
		    le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize) ==
							fp->maxpacksize * 2)
			continue;

		fp = kzalloc(sizeof(*fp), GFP_KERNEL);
		if (! fp) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -ENOMEM;
		}

		fp->iface = iface_no;
		fp->altsetting = altno;
		fp->altset_idx = i;
		fp->endpoint = get_endpoint(alts, 0)->bEndpointAddress;
		fp->ep_attr = get_endpoint(alts, 0)->bmAttributes;
		fp->datainterval = snd_usb_parse_datainterval(chip, alts);
		fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);
		/* num_channels is only set for v2 interfaces */
		fp->channels = num_channels;
		if (snd_usb_get_speed(dev) == USB_SPEED_HIGH)
			fp->maxpacksize = (((fp->maxpacksize >> 11) & 3) + 1)
					* (fp->maxpacksize & 0x7ff);
		fp->attributes = parse_uac_endpoint_attributes(chip, alts, protocol, iface_no);
		fp->clock = clock;

		/* some quirks for attributes here */

		switch (chip->usb_id) {
		case USB_ID(0x0a92, 0x0053): /* AudioTrak Optoplay */
			/* Optoplay sets the sample rate attribute although
			 * it seems not supporting it in fact.
			 */
			fp->attributes &= ~UAC_EP_CS_ATTR_SAMPLE_RATE;
			break;
		case USB_ID(0x041e, 0x3020): /* Creative SB Audigy 2 NX */
		case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
			/* doesn't set the sample rate attribute, but supports it */
			fp->attributes |= UAC_EP_CS_ATTR_SAMPLE_RATE;
			break;
		case USB_ID(0x0763, 0x2001):  /* M-Audio Quattro USB */
		case USB_ID(0x0763, 0x2012):  /* M-Audio Fast Track Pro USB */
		case USB_ID(0x047f, 0x0ca1): /* plantronics headset */
		case USB_ID(0x077d, 0x07af): /* Griffin iMic (note that there is
						an older model 77d:223) */
		/*
		 * plantronics headset and Griffin iMic have set adaptive-in
		 * although it's really not...
		 */
			fp->ep_attr &= ~USB_ENDPOINT_SYNCTYPE;
			if (stream == SNDRV_PCM_STREAM_PLAYBACK)
				fp->ep_attr |= USB_ENDPOINT_SYNC_ADAPTIVE;
			else
				fp->ep_attr |= USB_ENDPOINT_SYNC_SYNC;
			break;
		}

		/* ok, let's parse further... */
		if (snd_usb_parse_audio_format(chip, fp, format, fmt, stream, alts) < 0) {
			kfree(fp->rate_table);
			kfree(fp);
			fp = NULL;
			continue;
		}

		snd_printdd(KERN_INFO "%d:%u:%d: add audio endpoint %#x\n", dev->devnum, iface_no, altno, fp->endpoint);
		err = snd_usb_add_audio_stream(chip, stream, fp);
		if (err < 0) {
			kfree(fp->rate_table);
			kfree(fp);
			return err;
		}
		/* try to set the interface... */
		usb_set_interface(chip->dev, iface_no, altno);
		snd_usb_init_pitch(chip, iface_no, alts, fp);
		snd_usb_init_sample_rate(chip, iface_no, alts, fp, fp->rate_max);
	}
	return 0;
}

