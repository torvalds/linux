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
#include <sound/control.h>
#include <sound/tlv.h>

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
		kfree(fp->chmap);
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
 * initialize the substream instance.
 */

static void snd_usb_init_substream(struct snd_usb_stream *as,
				   int stream,
				   struct audioformat *fp)
{
	struct snd_usb_substream *subs = &as->substream[stream];

	INIT_LIST_HEAD(&subs->fmt_list);
	spin_lock_init(&subs->lock);

	subs->stream = as;
	subs->direction = stream;
	subs->dev = as->chip->dev;
	subs->txfr_quirk = as->chip->txfr_quirk;
	subs->speed = snd_usb_get_speed(subs->dev);

	snd_usb_set_pcm_ops(as->pcm, stream);

	list_add_tail(&fp->list, &subs->fmt_list);
	subs->formats |= fp->formats;
	subs->num_formats++;
	subs->fmt_type = fp->fmt_type;
	subs->ep_num = fp->endpoint;
	if (fp->channels > subs->channels_max)
		subs->channels_max = fp->channels;
}

/* kctl callbacks for usb-audio channel maps */
static int usb_chmap_ctl_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_usb_substream *subs = info->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = subs->channels_max;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_LAST;
	return 0;
}

/* check whether a duplicated entry exists in the audiofmt list */
static bool have_dup_chmap(struct snd_usb_substream *subs,
			   struct audioformat *fp)
{
	struct list_head *p;

	for (p = fp->list.prev; p != &subs->fmt_list; p = p->prev) {
		struct audioformat *prev;
		prev = list_entry(p, struct audioformat, list);
		if (prev->chmap &&
		    !memcmp(prev->chmap, fp->chmap, sizeof(*fp->chmap)))
			return true;
	}
	return false;
}

static int usb_chmap_ctl_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			     unsigned int size, unsigned int __user *tlv)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_usb_substream *subs = info->private_data;
	struct audioformat *fp;
	unsigned int __user *dst;
	int count = 0;

	if (size < 8)
		return -ENOMEM;
	if (put_user(SNDRV_CTL_TLVT_CONTAINER, tlv))
		return -EFAULT;
	size -= 8;
	dst = tlv + 2;
	list_for_each_entry(fp, &subs->fmt_list, list) {
		int i, ch_bytes;

		if (!fp->chmap)
			continue;
		if (have_dup_chmap(subs, fp))
			continue;
		/* copy the entry */
		ch_bytes = fp->chmap->channels * 4;
		if (size < 8 + ch_bytes)
			return -ENOMEM;
		if (put_user(SNDRV_CTL_TLVT_CHMAP_FIXED, dst) ||
		    put_user(ch_bytes, dst + 1))
			return -EFAULT;
		dst += 2;
		for (i = 0; i < fp->chmap->channels; i++, dst++) {
			if (put_user(fp->chmap->map[i], dst))
				return -EFAULT;
		}

		count += 8 + ch_bytes;
		size -= 8 + ch_bytes;
	}
	if (put_user(count, tlv + 1))
		return -EFAULT;
	return 0;
}

static int usb_chmap_ctl_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_usb_substream *subs = info->private_data;
	struct snd_pcm_chmap_elem *chmap = NULL;
	int i;

	memset(ucontrol->value.integer.value, 0,
	       sizeof(ucontrol->value.integer.value));
	if (subs->cur_audiofmt)
		chmap = subs->cur_audiofmt->chmap;
	if (chmap) {
		for (i = 0; i < chmap->channels; i++)
			ucontrol->value.integer.value[i] = chmap->map[i];
	}
	return 0;
}

/* create a chmap kctl assigned to the given USB substream */
static int add_chmap(struct snd_pcm *pcm, int stream,
		     struct snd_usb_substream *subs)
{
	struct audioformat *fp;
	struct snd_pcm_chmap *chmap;
	struct snd_kcontrol *kctl;
	int err;

	list_for_each_entry(fp, &subs->fmt_list, list)
		if (fp->chmap)
			goto ok;
	/* no chmap is found */
	return 0;

 ok:
	err = snd_pcm_add_chmap_ctls(pcm, stream, NULL, 0, 0, &chmap);
	if (err < 0)
		return err;

	/* override handlers */
	chmap->private_data = subs;
	kctl = chmap->kctl;
	kctl->info = usb_chmap_ctl_info;
	kctl->get = usb_chmap_ctl_get;
	kctl->tlv.c = usb_chmap_ctl_tlv;

	return 0;
}

/* convert from USB ChannelConfig bits to ALSA chmap element */
static struct snd_pcm_chmap_elem *convert_chmap(int channels, unsigned int bits,
						int protocol)
{
	static unsigned int uac1_maps[] = {
		SNDRV_CHMAP_FL,		/* left front */
		SNDRV_CHMAP_FR,		/* right front */
		SNDRV_CHMAP_FC,		/* center front */
		SNDRV_CHMAP_LFE,	/* LFE */
		SNDRV_CHMAP_SL,		/* left surround */
		SNDRV_CHMAP_SR,		/* right surround */
		SNDRV_CHMAP_FLC,	/* left of center */
		SNDRV_CHMAP_FRC,	/* right of center */
		SNDRV_CHMAP_RC,		/* surround */
		SNDRV_CHMAP_SL,		/* side left */
		SNDRV_CHMAP_SR,		/* side right */
		SNDRV_CHMAP_TC,		/* top */
		0 /* terminator */
	};
	static unsigned int uac2_maps[] = {
		SNDRV_CHMAP_FL,		/* front left */
		SNDRV_CHMAP_FR,		/* front right */
		SNDRV_CHMAP_FC,		/* front center */
		SNDRV_CHMAP_LFE,	/* LFE */
		SNDRV_CHMAP_RL,		/* back left */
		SNDRV_CHMAP_RR,		/* back right */
		SNDRV_CHMAP_FLC,	/* front left of center */
		SNDRV_CHMAP_FRC,	/* front right of center */
		SNDRV_CHMAP_RC,		/* back center */
		SNDRV_CHMAP_SL,		/* side left */
		SNDRV_CHMAP_SR,		/* side right */
		SNDRV_CHMAP_TC,		/* top center */
		SNDRV_CHMAP_TFL,	/* top front left */
		SNDRV_CHMAP_TFC,	/* top front center */
		SNDRV_CHMAP_TFR,	/* top front right */
		SNDRV_CHMAP_TRL,	/* top back left */
		SNDRV_CHMAP_TRC,	/* top back center */
		SNDRV_CHMAP_TRR,	/* top back right */
		SNDRV_CHMAP_TFLC,	/* top front left of center */
		SNDRV_CHMAP_TFRC,	/* top front right of center */
		SNDRV_CHMAP_LLFE,	/* left LFE */
		SNDRV_CHMAP_RLFE,	/* right LFE */
		SNDRV_CHMAP_TSL,	/* top side left */
		SNDRV_CHMAP_TSR,	/* top side right */
		SNDRV_CHMAP_BC,		/* bottom center */
		SNDRV_CHMAP_BLC,	/* bottom left center */
		SNDRV_CHMAP_BRC,	/* bottom right center */
		0 /* terminator */
	};
	struct snd_pcm_chmap_elem *chmap;
	const unsigned int *maps;
	int c;

	if (!bits)
		return NULL;
	if (channels > ARRAY_SIZE(chmap->map))
		return NULL;

	chmap = kzalloc(sizeof(*chmap), GFP_KERNEL);
	if (!chmap)
		return NULL;

	maps = protocol == UAC_VERSION_2 ? uac2_maps : uac1_maps;
	chmap->channels = channels;
	c = 0;
	for (; bits && *maps; maps++, bits >>= 1) {
		if (bits & 1)
			chmap->map[c++] = *maps;
	}

	for (; c < channels; c++)
		chmap->map[c] = SNDRV_CHMAP_UNKNOWN;

	return chmap;
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
		if (subs->ep_num == fp->endpoint) {
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
		if (subs->ep_num)
			continue;
		err = snd_pcm_new_stream(as->pcm, stream, 1);
		if (err < 0)
			return err;
		snd_usb_init_substream(as, stream, fp);
		return add_chmap(as->pcm, stream, subs);
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

	return add_chmap(pcm, stream, &as->substream[stream]);
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

/* find an input terminal descriptor (either UAC1 or UAC2) with the given
 * terminal id
 */
static void *
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
	unsigned int chconfig;

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

		chconfig = 0;
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
			struct uac_input_terminal_descriptor *iterm;

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

			iterm = snd_usb_find_input_terminal_descriptor(chip->ctrl_intf,
								       as->bTerminalLink);
			if (iterm) {
				num_channels = iterm->bNrChannels;
				chconfig = le16_to_cpu(iterm->wChannelConfig);
			}

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
				chconfig = le32_to_cpu(input_term->bmChannelConfig);
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
		fp->channels = num_channels;
		if (snd_usb_get_speed(dev) == USB_SPEED_HIGH)
			fp->maxpacksize = (((fp->maxpacksize >> 11) & 3) + 1)
					* (fp->maxpacksize & 0x7ff);
		fp->attributes = parse_uac_endpoint_attributes(chip, alts, protocol, iface_no);
		fp->clock = clock;
		fp->chmap = convert_chmap(num_channels, chconfig, protocol);

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
			kfree(fp->chmap);
			kfree(fp);
			fp = NULL;
			continue;
		}

		snd_printdd(KERN_INFO "%d:%u:%d: add audio endpoint %#x\n", dev->devnum, iface_no, altno, fp->endpoint);
		err = snd_usb_add_audio_stream(chip, stream, fp);
		if (err < 0) {
			kfree(fp->rate_table);
			kfree(fp->chmap);
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

