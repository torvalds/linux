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
#include <linux/usb/midi.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>

#include "usbaudio.h"
#include "card.h"
#include "mixer.h"
#include "mixer_quirks.h"
#include "midi.h"
#include "quirks.h"
#include "helper.h"
#include "endpoint.h"
#include "pcm.h"
#include "clock.h"
#include "stream.h"

/*
 * handle the quirks for the contained interfaces
 */
static int create_composite_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  struct usb_driver *driver,
				  const struct snd_usb_audio_quirk *quirk_comp)
{
	int probed_ifnum = get_iface_desc(iface->altsetting)->bInterfaceNumber;
	const struct snd_usb_audio_quirk *quirk;
	int err;

	for (quirk = quirk_comp->data; quirk->ifnum >= 0; ++quirk) {
		iface = usb_ifnum_to_if(chip->dev, quirk->ifnum);
		if (!iface)
			continue;
		if (quirk->ifnum != probed_ifnum &&
		    usb_interface_claimed(iface))
			continue;
		err = snd_usb_create_quirk(chip, iface, driver, quirk);
		if (err < 0)
			return err;
	}

	for (quirk = quirk_comp->data; quirk->ifnum >= 0; ++quirk) {
		iface = usb_ifnum_to_if(chip->dev, quirk->ifnum);
		if (!iface)
			continue;
		if (quirk->ifnum != probed_ifnum &&
		    !usb_interface_claimed(iface))
			usb_driver_claim_interface(driver, iface, (void *)-1L);
	}

	return 0;
}

static int ignore_interface_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  struct usb_driver *driver,
				  const struct snd_usb_audio_quirk *quirk)
{
	return 0;
}


/*
 * Allow alignment on audio sub-slot (channel samples) rather than
 * on audio slots (audio frames)
 */
static int create_align_transfer_quirk(struct snd_usb_audio *chip,
				       struct usb_interface *iface,
				       struct usb_driver *driver,
				       const struct snd_usb_audio_quirk *quirk)
{
	chip->txfr_quirk = 1;
	return 1;	/* Continue with creating streams and mixer */
}

static int create_any_midi_quirk(struct snd_usb_audio *chip,
				 struct usb_interface *intf,
				 struct usb_driver *driver,
				 const struct snd_usb_audio_quirk *quirk)
{
	return snd_usbmidi_create(chip->card, intf, &chip->midi_list, quirk);
}

/*
 * create a stream for an interface with proper descriptors
 */
static int create_standard_audio_quirk(struct snd_usb_audio *chip,
				       struct usb_interface *iface,
				       struct usb_driver *driver,
				       const struct snd_usb_audio_quirk *quirk)
{
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	int err;

	if (chip->usb_id == USB_ID(0x1686, 0x00dd)) /* Zoom R16/24 */
		chip->tx_length_quirk = 1;

	alts = &iface->altsetting[0];
	altsd = get_iface_desc(alts);
	err = snd_usb_parse_audio_interface(chip, altsd->bInterfaceNumber);
	if (err < 0) {
		usb_audio_err(chip, "cannot setup if %d: error %d\n",
			   altsd->bInterfaceNumber, err);
		return err;
	}
	/* reset the current interface */
	usb_set_interface(chip->dev, altsd->bInterfaceNumber, 0);
	return 0;
}

/*
 * create a stream for an endpoint/altsetting without proper descriptors
 */
static int create_fixed_stream_quirk(struct snd_usb_audio *chip,
				     struct usb_interface *iface,
				     struct usb_driver *driver,
				     const struct snd_usb_audio_quirk *quirk)
{
	struct audioformat *fp;
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	int stream, err;
	unsigned *rate_table = NULL;

	fp = kmemdup(quirk->data, sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	INIT_LIST_HEAD(&fp->list);
	if (fp->nr_rates > MAX_NR_RATES) {
		kfree(fp);
		return -EINVAL;
	}
	if (fp->nr_rates > 0) {
		rate_table = kmemdup(fp->rate_table,
				     sizeof(int) * fp->nr_rates, GFP_KERNEL);
		if (!rate_table) {
			kfree(fp);
			return -ENOMEM;
		}
		fp->rate_table = rate_table;
	}

	stream = (fp->endpoint & USB_DIR_IN)
		? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
	err = snd_usb_add_audio_stream(chip, stream, fp);
	if (err < 0)
		goto error;
	if (fp->iface != get_iface_desc(&iface->altsetting[0])->bInterfaceNumber ||
	    fp->altset_idx >= iface->num_altsetting) {
		err = -EINVAL;
		goto error;
	}
	alts = &iface->altsetting[fp->altset_idx];
	altsd = get_iface_desc(alts);
	if (altsd->bNumEndpoints < 1) {
		err = -EINVAL;
		goto error;
	}

	fp->protocol = altsd->bInterfaceProtocol;

	if (fp->datainterval == 0)
		fp->datainterval = snd_usb_parse_datainterval(chip, alts);
	if (fp->maxpacksize == 0)
		fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);
	usb_set_interface(chip->dev, fp->iface, 0);
	snd_usb_init_pitch(chip, fp->iface, alts, fp);
	snd_usb_init_sample_rate(chip, fp->iface, alts, fp, fp->rate_max);
	return 0;

 error:
	list_del(&fp->list); /* unlink for avoiding double-free */
	kfree(fp);
	kfree(rate_table);
	return err;
}

static int create_auto_pcm_quirk(struct snd_usb_audio *chip,
				 struct usb_interface *iface,
				 struct usb_driver *driver)
{
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_endpoint_descriptor *epd;
	struct uac1_as_header_descriptor *ashd;
	struct uac_format_type_i_discrete_descriptor *fmtd;

	/*
	 * Most Roland/Yamaha audio streaming interfaces have more or less
	 * standard descriptors, but older devices might lack descriptors, and
	 * future ones might change, so ensure that we fail silently if the
	 * interface doesn't look exactly right.
	 */

	/* must have a non-zero altsetting for streaming */
	if (iface->num_altsetting < 2)
		return -ENODEV;
	alts = &iface->altsetting[1];
	altsd = get_iface_desc(alts);

	/* must have an isochronous endpoint for streaming */
	if (altsd->bNumEndpoints < 1)
		return -ENODEV;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_xfer_isoc(epd))
		return -ENODEV;

	/* must have format descriptors */
	ashd = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL,
				       UAC_AS_GENERAL);
	fmtd = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL,
				       UAC_FORMAT_TYPE);
	if (!ashd || ashd->bLength < 7 ||
	    !fmtd || fmtd->bLength < 8)
		return -ENODEV;

	return create_standard_audio_quirk(chip, iface, driver, NULL);
}

static int create_yamaha_midi_quirk(struct snd_usb_audio *chip,
				    struct usb_interface *iface,
				    struct usb_driver *driver,
				    struct usb_host_interface *alts)
{
	static const struct snd_usb_audio_quirk yamaha_midi_quirk = {
		.type = QUIRK_MIDI_YAMAHA
	};
	struct usb_midi_in_jack_descriptor *injd;
	struct usb_midi_out_jack_descriptor *outjd;

	/* must have some valid jack descriptors */
	injd = snd_usb_find_csint_desc(alts->extra, alts->extralen,
				       NULL, USB_MS_MIDI_IN_JACK);
	outjd = snd_usb_find_csint_desc(alts->extra, alts->extralen,
					NULL, USB_MS_MIDI_OUT_JACK);
	if (!injd && !outjd)
		return -ENODEV;
	if ((injd && !snd_usb_validate_midi_desc(injd)) ||
	    (outjd && !snd_usb_validate_midi_desc(outjd)))
		return -ENODEV;
	if (injd && (injd->bLength < 5 ||
		     (injd->bJackType != USB_MS_EMBEDDED &&
		      injd->bJackType != USB_MS_EXTERNAL)))
		return -ENODEV;
	if (outjd && (outjd->bLength < 6 ||
		      (outjd->bJackType != USB_MS_EMBEDDED &&
		       outjd->bJackType != USB_MS_EXTERNAL)))
		return -ENODEV;
	return create_any_midi_quirk(chip, iface, driver, &yamaha_midi_quirk);
}

static int create_roland_midi_quirk(struct snd_usb_audio *chip,
				    struct usb_interface *iface,
				    struct usb_driver *driver,
				    struct usb_host_interface *alts)
{
	static const struct snd_usb_audio_quirk roland_midi_quirk = {
		.type = QUIRK_MIDI_ROLAND
	};
	u8 *roland_desc = NULL;

	/* might have a vendor-specific descriptor <06 24 F1 02 ...> */
	for (;;) {
		roland_desc = snd_usb_find_csint_desc(alts->extra,
						      alts->extralen,
						      roland_desc, 0xf1);
		if (!roland_desc)
			return -ENODEV;
		if (roland_desc[0] < 6 || roland_desc[3] != 2)
			continue;
		return create_any_midi_quirk(chip, iface, driver,
					     &roland_midi_quirk);
	}
}

static int create_std_midi_quirk(struct snd_usb_audio *chip,
				 struct usb_interface *iface,
				 struct usb_driver *driver,
				 struct usb_host_interface *alts)
{
	struct usb_ms_header_descriptor *mshd;
	struct usb_ms_endpoint_descriptor *msepd;

	/* must have the MIDIStreaming interface header descriptor*/
	mshd = (struct usb_ms_header_descriptor *)alts->extra;
	if (alts->extralen < 7 ||
	    mshd->bLength < 7 ||
	    mshd->bDescriptorType != USB_DT_CS_INTERFACE ||
	    mshd->bDescriptorSubtype != USB_MS_HEADER)
		return -ENODEV;
	/* must have the MIDIStreaming endpoint descriptor*/
	msepd = (struct usb_ms_endpoint_descriptor *)alts->endpoint[0].extra;
	if (alts->endpoint[0].extralen < 4 ||
	    msepd->bLength < 4 ||
	    msepd->bDescriptorType != USB_DT_CS_ENDPOINT ||
	    msepd->bDescriptorSubtype != UAC_MS_GENERAL ||
	    msepd->bNumEmbMIDIJack < 1 ||
	    msepd->bNumEmbMIDIJack > 16)
		return -ENODEV;

	return create_any_midi_quirk(chip, iface, driver, NULL);
}

static int create_auto_midi_quirk(struct snd_usb_audio *chip,
				  struct usb_interface *iface,
				  struct usb_driver *driver)
{
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct usb_endpoint_descriptor *epd;
	int err;

	alts = &iface->altsetting[0];
	altsd = get_iface_desc(alts);

	/* must have at least one bulk/interrupt endpoint for streaming */
	if (altsd->bNumEndpoints < 1)
		return -ENODEV;
	epd = get_endpoint(alts, 0);
	if (!usb_endpoint_xfer_bulk(epd) &&
	    !usb_endpoint_xfer_int(epd))
		return -ENODEV;

	switch (USB_ID_VENDOR(chip->usb_id)) {
	case 0x0499: /* Yamaha */
		err = create_yamaha_midi_quirk(chip, iface, driver, alts);
		if (err != -ENODEV)
			return err;
		break;
	case 0x0582: /* Roland */
		err = create_roland_midi_quirk(chip, iface, driver, alts);
		if (err != -ENODEV)
			return err;
		break;
	}

	return create_std_midi_quirk(chip, iface, driver, alts);
}

static int create_autodetect_quirk(struct snd_usb_audio *chip,
				   struct usb_interface *iface,
				   struct usb_driver *driver)
{
	int err;

	err = create_auto_pcm_quirk(chip, iface, driver);
	if (err == -ENODEV)
		err = create_auto_midi_quirk(chip, iface, driver);
	return err;
}

static int create_autodetect_quirks(struct snd_usb_audio *chip,
				    struct usb_interface *iface,
				    struct usb_driver *driver,
				    const struct snd_usb_audio_quirk *quirk)
{
	int probed_ifnum = get_iface_desc(iface->altsetting)->bInterfaceNumber;
	int ifcount, ifnum, err;

	err = create_autodetect_quirk(chip, iface, driver);
	if (err < 0)
		return err;

	/*
	 * ALSA PCM playback/capture devices cannot be registered in two steps,
	 * so we have to claim the other corresponding interface here.
	 */
	ifcount = chip->dev->actconfig->desc.bNumInterfaces;
	for (ifnum = 0; ifnum < ifcount; ifnum++) {
		if (ifnum == probed_ifnum || quirk->ifnum >= 0)
			continue;
		iface = usb_ifnum_to_if(chip->dev, ifnum);
		if (!iface ||
		    usb_interface_claimed(iface) ||
		    get_iface_desc(iface->altsetting)->bInterfaceClass !=
							USB_CLASS_VENDOR_SPEC)
			continue;

		err = create_autodetect_quirk(chip, iface, driver);
		if (err >= 0)
			usb_driver_claim_interface(driver, iface, (void *)-1L);
	}

	return 0;
}

/*
 * Create a stream for an Edirol UA-700/UA-25/UA-4FX interface.  
 * The only way to detect the sample rate is by looking at wMaxPacketSize.
 */
static int create_uaxx_quirk(struct snd_usb_audio *chip,
			     struct usb_interface *iface,
			     struct usb_driver *driver,
			     const struct snd_usb_audio_quirk *quirk)
{
	static const struct audioformat ua_format = {
		.formats = SNDRV_PCM_FMTBIT_S24_3LE,
		.channels = 2,
		.fmt_type = UAC_FORMAT_TYPE_I,
		.altsetting = 1,
		.altset_idx = 1,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
	};
	struct usb_host_interface *alts;
	struct usb_interface_descriptor *altsd;
	struct audioformat *fp;
	int stream, err;

	/* both PCM and MIDI interfaces have 2 or more altsettings */
	if (iface->num_altsetting < 2)
		return -ENXIO;
	alts = &iface->altsetting[1];
	altsd = get_iface_desc(alts);

	if (altsd->bNumEndpoints == 2) {
		static const struct snd_usb_midi_endpoint_info ua700_ep = {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		};
		static const struct snd_usb_audio_quirk ua700_quirk = {
			.type = QUIRK_MIDI_FIXED_ENDPOINT,
			.data = &ua700_ep
		};
		static const struct snd_usb_midi_endpoint_info uaxx_ep = {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		};
		static const struct snd_usb_audio_quirk uaxx_quirk = {
			.type = QUIRK_MIDI_FIXED_ENDPOINT,
			.data = &uaxx_ep
		};
		const struct snd_usb_audio_quirk *quirk =
			chip->usb_id == USB_ID(0x0582, 0x002b)
			? &ua700_quirk : &uaxx_quirk;
		return __snd_usbmidi_create(chip->card, iface,
					  &chip->midi_list, quirk,
					  chip->usb_id);
	}

	if (altsd->bNumEndpoints != 1)
		return -ENXIO;

	fp = kmemdup(&ua_format, sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->iface = altsd->bInterfaceNumber;
	fp->endpoint = get_endpoint(alts, 0)->bEndpointAddress;
	fp->ep_attr = get_endpoint(alts, 0)->bmAttributes;
	fp->datainterval = 0;
	fp->maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);
	INIT_LIST_HEAD(&fp->list);

	switch (fp->maxpacksize) {
	case 0x120:
		fp->rate_max = fp->rate_min = 44100;
		break;
	case 0x138:
	case 0x140:
		fp->rate_max = fp->rate_min = 48000;
		break;
	case 0x258:
	case 0x260:
		fp->rate_max = fp->rate_min = 96000;
		break;
	default:
		usb_audio_err(chip, "unknown sample rate\n");
		kfree(fp);
		return -ENXIO;
	}

	stream = (fp->endpoint & USB_DIR_IN)
		? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
	err = snd_usb_add_audio_stream(chip, stream, fp);
	if (err < 0) {
		list_del(&fp->list); /* unlink for avoiding double-free */
		kfree(fp);
		return err;
	}
	usb_set_interface(chip->dev, fp->iface, 0);
	return 0;
}

/*
 * Create a standard mixer for the specified interface.
 */
static int create_standard_mixer_quirk(struct snd_usb_audio *chip,
				       struct usb_interface *iface,
				       struct usb_driver *driver,
				       const struct snd_usb_audio_quirk *quirk)
{
	if (quirk->ifnum < 0)
		return 0;

	return snd_usb_create_mixer(chip, quirk->ifnum, 0);
}


static int setup_fmt_after_resume_quirk(struct snd_usb_audio *chip,
				       struct usb_interface *iface,
				       struct usb_driver *driver,
				       const struct snd_usb_audio_quirk *quirk)
{
	chip->setup_fmt_after_resume_quirk = 1;
	return 1;	/* Continue with creating streams and mixer */
}

/*
 * audio-interface quirks
 *
 * returns zero if no standard audio/MIDI parsing is needed.
 * returns a positive value if standard audio/midi interfaces are parsed
 * after this.
 * returns a negative value at error.
 */
int snd_usb_create_quirk(struct snd_usb_audio *chip,
			 struct usb_interface *iface,
			 struct usb_driver *driver,
			 const struct snd_usb_audio_quirk *quirk)
{
	typedef int (*quirk_func_t)(struct snd_usb_audio *,
				    struct usb_interface *,
				    struct usb_driver *,
				    const struct snd_usb_audio_quirk *);
	static const quirk_func_t quirk_funcs[] = {
		[QUIRK_IGNORE_INTERFACE] = ignore_interface_quirk,
		[QUIRK_COMPOSITE] = create_composite_quirk,
		[QUIRK_AUTODETECT] = create_autodetect_quirks,
		[QUIRK_MIDI_STANDARD_INTERFACE] = create_any_midi_quirk,
		[QUIRK_MIDI_FIXED_ENDPOINT] = create_any_midi_quirk,
		[QUIRK_MIDI_YAMAHA] = create_any_midi_quirk,
		[QUIRK_MIDI_ROLAND] = create_any_midi_quirk,
		[QUIRK_MIDI_MIDIMAN] = create_any_midi_quirk,
		[QUIRK_MIDI_NOVATION] = create_any_midi_quirk,
		[QUIRK_MIDI_RAW_BYTES] = create_any_midi_quirk,
		[QUIRK_MIDI_EMAGIC] = create_any_midi_quirk,
		[QUIRK_MIDI_CME] = create_any_midi_quirk,
		[QUIRK_MIDI_AKAI] = create_any_midi_quirk,
		[QUIRK_MIDI_FTDI] = create_any_midi_quirk,
		[QUIRK_MIDI_CH345] = create_any_midi_quirk,
		[QUIRK_AUDIO_STANDARD_INTERFACE] = create_standard_audio_quirk,
		[QUIRK_AUDIO_FIXED_ENDPOINT] = create_fixed_stream_quirk,
		[QUIRK_AUDIO_EDIROL_UAXX] = create_uaxx_quirk,
		[QUIRK_AUDIO_ALIGN_TRANSFER] = create_align_transfer_quirk,
		[QUIRK_AUDIO_STANDARD_MIXER] = create_standard_mixer_quirk,
		[QUIRK_SETUP_FMT_AFTER_RESUME] = setup_fmt_after_resume_quirk,
	};

	if (quirk->type < QUIRK_TYPE_COUNT) {
		return quirk_funcs[quirk->type](chip, iface, driver, quirk);
	} else {
		usb_audio_err(chip, "invalid quirk type %d\n", quirk->type);
		return -ENXIO;
	}
}

/*
 * boot quirks
 */

#define EXTIGY_FIRMWARE_SIZE_OLD 794
#define EXTIGY_FIRMWARE_SIZE_NEW 483

static int snd_usb_extigy_boot_quirk(struct usb_device *dev, struct usb_interface *intf)
{
	struct usb_host_config *config = dev->actconfig;
	int err;

	if (le16_to_cpu(get_cfg_desc(config)->wTotalLength) == EXTIGY_FIRMWARE_SIZE_OLD ||
	    le16_to_cpu(get_cfg_desc(config)->wTotalLength) == EXTIGY_FIRMWARE_SIZE_NEW) {
		dev_dbg(&dev->dev, "sending Extigy boot sequence...\n");
		/* Send message to force it to reconnect with full interface. */
		err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev,0),
				      0x10, 0x43, 0x0001, 0x000a, NULL, 0);
		if (err < 0)
			dev_dbg(&dev->dev, "error sending boot message: %d\n", err);
		err = usb_get_descriptor(dev, USB_DT_DEVICE, 0,
				&dev->descriptor, sizeof(dev->descriptor));
		config = dev->actconfig;
		if (err < 0)
			dev_dbg(&dev->dev, "error usb_get_descriptor: %d\n", err);
		err = usb_reset_configuration(dev);
		if (err < 0)
			dev_dbg(&dev->dev, "error usb_reset_configuration: %d\n", err);
		dev_dbg(&dev->dev, "extigy_boot: new boot length = %d\n",
			    le16_to_cpu(get_cfg_desc(config)->wTotalLength));
		return -ENODEV; /* quit this anyway */
	}
	return 0;
}

static int snd_usb_audigy2nx_boot_quirk(struct usb_device *dev)
{
	u8 buf = 1;

	snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), 0x2a,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			0, 0, &buf, 1);
	if (buf == 0) {
		snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0x29,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
				1, 2000, NULL, 0);
		return -ENODEV;
	}
	return 0;
}

static int snd_usb_fasttrackpro_boot_quirk(struct usb_device *dev)
{
	int err;

	if (dev->actconfig->desc.bConfigurationValue == 1) {
		dev_info(&dev->dev,
			   "Fast Track Pro switching to config #2\n");
		/* This function has to be available by the usb core module.
		 * if it is not avialable the boot quirk has to be left out
		 * and the configuration has to be set by udev or hotplug
		 * rules
		 */
		err = usb_driver_set_configuration(dev, 2);
		if (err < 0)
			dev_dbg(&dev->dev,
				"error usb_driver_set_configuration: %d\n",
				err);
		/* Always return an error, so that we stop creating a device
		   that will just be destroyed and recreated with a new
		   configuration */
		return -ENODEV;
	} else
		dev_info(&dev->dev, "Fast Track Pro config OK\n");

	return 0;
}

/*
 * C-Media CM106/CM106+ have four 16-bit internal registers that are nicely
 * documented in the device's data sheet.
 */
static int snd_usb_cm106_write_int_reg(struct usb_device *dev, int reg, u16 value)
{
	u8 buf[4];
	buf[0] = 0x20;
	buf[1] = value & 0xff;
	buf[2] = (value >> 8) & 0xff;
	buf[3] = reg;
	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), USB_REQ_SET_CONFIGURATION,
			       USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			       0, 0, &buf, 4);
}

static int snd_usb_cm106_boot_quirk(struct usb_device *dev)
{
	/*
	 * Enable line-out driver mode, set headphone source to front
	 * channels, enable stereo mic.
	 */
	return snd_usb_cm106_write_int_reg(dev, 2, 0x8004);
}

/*
 * C-Media CM6206 is based on CM106 with two additional
 * registers that are not documented in the data sheet.
 * Values here are chosen based on sniffing USB traffic
 * under Windows.
 */
static int snd_usb_cm6206_boot_quirk(struct usb_device *dev)
{
	int err  = 0, reg;
	int val[] = {0x2004, 0x3000, 0xf800, 0x143f, 0x0000, 0x3000};

	for (reg = 0; reg < ARRAY_SIZE(val); reg++) {
		err = snd_usb_cm106_write_int_reg(dev, reg, val[reg]);
		if (err < 0)
			return err;
	}

	return err;
}

/* quirk for Plantronics GameCom 780 with CM6302 chip */
static int snd_usb_gamecon780_boot_quirk(struct usb_device *dev)
{
	/* set the initial volume and don't change; other values are either
	 * too loud or silent due to firmware bug (bko#65251)
	 */
	u8 buf[2] = { 0x74, 0xe3 };
	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			UAC_FU_VOLUME << 8, 9 << 8, buf, 2);
}

/*
 * Novation Twitch DJ controller
 * Focusrite Novation Saffire 6 USB audio card
 */
static int snd_usb_novation_boot_quirk(struct usb_device *dev)
{
	/* preemptively set up the device because otherwise the
	 * raw MIDI endpoints are not active */
	usb_set_interface(dev, 0, 1);
	return 0;
}

/*
 * This call will put the synth in "USB send" mode, i.e it will send MIDI
 * messages through USB (this is disabled at startup). The synth will
 * acknowledge by sending a sysex on endpoint 0x85 and by displaying a USB
 * sign on its LCD. Values here are chosen based on sniffing USB traffic
 * under Windows.
 */
static int snd_usb_accessmusic_boot_quirk(struct usb_device *dev)
{
	int err, actual_length;

	/* "midi send" enable */
	static const u8 seq[] = { 0x4e, 0x73, 0x52, 0x01 };

	void *buf = kmemdup(seq, ARRAY_SIZE(seq), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	err = usb_interrupt_msg(dev, usb_sndintpipe(dev, 0x05), buf,
			ARRAY_SIZE(seq), &actual_length, 1000);
	kfree(buf);
	if (err < 0)
		return err;

	return 0;
}

/*
 * Some sound cards from Native Instruments are in fact compliant to the USB
 * audio standard of version 2 and other approved USB standards, even though
 * they come up as vendor-specific device when first connected.
 *
 * However, they can be told to come up with a new set of descriptors
 * upon their next enumeration, and the interfaces announced by the new
 * descriptors will then be handled by the kernel's class drivers. As the
 * product ID will also change, no further checks are required.
 */

static int snd_usb_nativeinstruments_boot_quirk(struct usb_device *dev)
{
	int ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				  0xaf, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				  1, 0, NULL, 0, 1000);

	if (ret < 0)
		return ret;

	usb_reset_device(dev);

	/* return -EAGAIN, so the creation of an audio interface for this
	 * temporary device is aborted. The device will reconnect with a
	 * new product ID */
	return -EAGAIN;
}

static void mbox2_setup_48_24_magic(struct usb_device *dev)
{
	u8 srate[3];
	u8 temp[12];

	/* Choose 48000Hz permanently */
	srate[0] = 0x80;
	srate[1] = 0xbb;
	srate[2] = 0x00;

	/* Send the magic! */
	snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
		0x01, 0x22, 0x0100, 0x0085, &temp, 0x0003);
	snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
		0x81, 0xa2, 0x0100, 0x0085, &srate, 0x0003);
	snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
		0x81, 0xa2, 0x0100, 0x0086, &srate, 0x0003);
	snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
		0x81, 0xa2, 0x0100, 0x0003, &srate, 0x0003);
	return;
}

/* Digidesign Mbox 2 needs to load firmware onboard
 * and driver must wait a few seconds for initialisation.
 */

#define MBOX2_FIRMWARE_SIZE    646
#define MBOX2_BOOT_LOADING     0x01 /* Hard coded into the device */
#define MBOX2_BOOT_READY       0x02 /* Hard coded into the device */

static int snd_usb_mbox2_boot_quirk(struct usb_device *dev)
{
	struct usb_host_config *config = dev->actconfig;
	int err;
	u8 bootresponse[0x12];
	int fwsize;
	int count;

	fwsize = le16_to_cpu(get_cfg_desc(config)->wTotalLength);

	if (fwsize != MBOX2_FIRMWARE_SIZE) {
		dev_err(&dev->dev, "Invalid firmware size=%d.\n", fwsize);
		return -ENODEV;
	}

	dev_dbg(&dev->dev, "Sending Digidesign Mbox 2 boot sequence...\n");

	count = 0;
	bootresponse[0] = MBOX2_BOOT_LOADING;
	while ((bootresponse[0] == MBOX2_BOOT_LOADING) && (count < 10)) {
		msleep(500); /* 0.5 second delay */
		snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			/* Control magic - load onboard firmware */
			0x85, 0xc0, 0x0001, 0x0000, &bootresponse, 0x0012);
		if (bootresponse[0] == MBOX2_BOOT_READY)
			break;
		dev_dbg(&dev->dev, "device not ready, resending boot sequence...\n");
		count++;
	}

	if (bootresponse[0] != MBOX2_BOOT_READY) {
		dev_err(&dev->dev, "Unknown bootresponse=%d, or timed out, ignoring device.\n", bootresponse[0]);
		return -ENODEV;
	}

	dev_dbg(&dev->dev, "device initialised!\n");

	err = usb_get_descriptor(dev, USB_DT_DEVICE, 0,
		&dev->descriptor, sizeof(dev->descriptor));
	config = dev->actconfig;
	if (err < 0)
		dev_dbg(&dev->dev, "error usb_get_descriptor: %d\n", err);

	err = usb_reset_configuration(dev);
	if (err < 0)
		dev_dbg(&dev->dev, "error usb_reset_configuration: %d\n", err);
	dev_dbg(&dev->dev, "mbox2_boot: new boot length = %d\n",
		le16_to_cpu(get_cfg_desc(config)->wTotalLength));

	mbox2_setup_48_24_magic(dev);

	dev_info(&dev->dev, "Digidesign Mbox 2: 24bit 48kHz");

	return 0; /* Successful boot */
}

static int snd_usb_axefx3_boot_quirk(struct usb_device *dev)
{
	int err;

	dev_dbg(&dev->dev, "Waiting for Axe-Fx III to boot up...\n");

	/* If the Axe-Fx III has not fully booted, it will timeout when trying
	 * to enable the audio streaming interface. A more generous timeout is
	 * used here to detect when the Axe-Fx III has finished booting as the
	 * set interface message will be acked once it has
	 */
	err = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
				1, 1, NULL, 0, 120000);
	if (err < 0) {
		dev_err(&dev->dev,
			"failed waiting for Axe-Fx III to boot: %d\n", err);
		return err;
	}

	dev_dbg(&dev->dev, "Axe-Fx III is now ready\n");

	err = usb_set_interface(dev, 1, 0);
	if (err < 0)
		dev_dbg(&dev->dev,
			"error stopping Axe-Fx III interface: %d\n", err);

	return 0;
}

/*
 * Setup quirks
 */
#define MAUDIO_SET		0x01 /* parse device_setup */
#define MAUDIO_SET_COMPATIBLE	0x80 /* use only "win-compatible" interfaces */
#define MAUDIO_SET_DTS		0x02 /* enable DTS Digital Output */
#define MAUDIO_SET_96K		0x04 /* 48-96KHz rate if set, 8-48KHz otherwise */
#define MAUDIO_SET_24B		0x08 /* 24bits sample if set, 16bits otherwise */
#define MAUDIO_SET_DI		0x10 /* enable Digital Input */
#define MAUDIO_SET_MASK		0x1f /* bit mask for setup value */
#define MAUDIO_SET_24B_48K_DI	 0x19 /* 24bits+48KHz+Digital Input */
#define MAUDIO_SET_24B_48K_NOTDI 0x09 /* 24bits+48KHz+No Digital Input */
#define MAUDIO_SET_16B_48K_DI	 0x11 /* 16bits+48KHz+Digital Input */
#define MAUDIO_SET_16B_48K_NOTDI 0x01 /* 16bits+48KHz+No Digital Input */

static int quattro_skip_setting_quirk(struct snd_usb_audio *chip,
				      int iface, int altno)
{
	/* Reset ALL ifaces to 0 altsetting.
	 * Call it for every possible altsetting of every interface.
	 */
	usb_set_interface(chip->dev, iface, 0);
	if (chip->setup & MAUDIO_SET) {
		if (chip->setup & MAUDIO_SET_COMPATIBLE) {
			if (iface != 1 && iface != 2)
				return 1; /* skip all interfaces but 1 and 2 */
		} else {
			unsigned int mask;
			if (iface == 1 || iface == 2)
				return 1; /* skip interfaces 1 and 2 */
			if ((chip->setup & MAUDIO_SET_96K) && altno != 1)
				return 1; /* skip this altsetting */
			mask = chip->setup & MAUDIO_SET_MASK;
			if (mask == MAUDIO_SET_24B_48K_DI && altno != 2)
				return 1; /* skip this altsetting */
			if (mask == MAUDIO_SET_24B_48K_NOTDI && altno != 3)
				return 1; /* skip this altsetting */
			if (mask == MAUDIO_SET_16B_48K_NOTDI && altno != 4)
				return 1; /* skip this altsetting */
		}
	}
	usb_audio_dbg(chip,
		    "using altsetting %d for interface %d config %d\n",
		    altno, iface, chip->setup);
	return 0; /* keep this altsetting */
}

static int audiophile_skip_setting_quirk(struct snd_usb_audio *chip,
					 int iface,
					 int altno)
{
	/* Reset ALL ifaces to 0 altsetting.
	 * Call it for every possible altsetting of every interface.
	 */
	usb_set_interface(chip->dev, iface, 0);

	if (chip->setup & MAUDIO_SET) {
		unsigned int mask;
		if ((chip->setup & MAUDIO_SET_DTS) && altno != 6)
			return 1; /* skip this altsetting */
		if ((chip->setup & MAUDIO_SET_96K) && altno != 1)
			return 1; /* skip this altsetting */
		mask = chip->setup & MAUDIO_SET_MASK;
		if (mask == MAUDIO_SET_24B_48K_DI && altno != 2)
			return 1; /* skip this altsetting */
		if (mask == MAUDIO_SET_24B_48K_NOTDI && altno != 3)
			return 1; /* skip this altsetting */
		if (mask == MAUDIO_SET_16B_48K_DI && altno != 4)
			return 1; /* skip this altsetting */
		if (mask == MAUDIO_SET_16B_48K_NOTDI && altno != 5)
			return 1; /* skip this altsetting */
	}

	return 0; /* keep this altsetting */
}

static int fasttrackpro_skip_setting_quirk(struct snd_usb_audio *chip,
					   int iface, int altno)
{
	/* Reset ALL ifaces to 0 altsetting.
	 * Call it for every possible altsetting of every interface.
	 */
	usb_set_interface(chip->dev, iface, 0);

	/* possible configuration where both inputs and only one output is
	 *used is not supported by the current setup
	 */
	if (chip->setup & (MAUDIO_SET | MAUDIO_SET_24B)) {
		if (chip->setup & MAUDIO_SET_96K) {
			if (altno != 3 && altno != 6)
				return 1;
		} else if (chip->setup & MAUDIO_SET_DI) {
			if (iface == 4)
				return 1; /* no analog input */
			if (altno != 2 && altno != 5)
				return 1; /* enable only altsets 2 and 5 */
		} else {
			if (iface == 5)
				return 1; /* disable digialt input */
			if (altno != 2 && altno != 5)
				return 1; /* enalbe only altsets 2 and 5 */
		}
	} else {
		/* keep only 16-Bit mode */
		if (altno != 1)
			return 1;
	}

	usb_audio_dbg(chip,
		    "using altsetting %d for interface %d config %d\n",
		    altno, iface, chip->setup);
	return 0; /* keep this altsetting */
}

int snd_usb_apply_interface_quirk(struct snd_usb_audio *chip,
				  int iface,
				  int altno)
{
	/* audiophile usb: skip altsets incompatible with device_setup */
	if (chip->usb_id == USB_ID(0x0763, 0x2003))
		return audiophile_skip_setting_quirk(chip, iface, altno);
	/* quattro usb: skip altsets incompatible with device_setup */
	if (chip->usb_id == USB_ID(0x0763, 0x2001))
		return quattro_skip_setting_quirk(chip, iface, altno);
	/* fasttrackpro usb: skip altsets incompatible with device_setup */
	if (chip->usb_id == USB_ID(0x0763, 0x2012))
		return fasttrackpro_skip_setting_quirk(chip, iface, altno);

	return 0;
}

int snd_usb_apply_boot_quirk(struct usb_device *dev,
			     struct usb_interface *intf,
			     const struct snd_usb_audio_quirk *quirk,
			     unsigned int id)
{
	switch (id) {
	case USB_ID(0x041e, 0x3000):
		/* SB Extigy needs special boot-up sequence */
		/* if more models come, this will go to the quirk list. */
		return snd_usb_extigy_boot_quirk(dev, intf);

	case USB_ID(0x041e, 0x3020):
		/* SB Audigy 2 NX needs its own boot-up magic, too */
		return snd_usb_audigy2nx_boot_quirk(dev);

	case USB_ID(0x10f5, 0x0200):
		/* C-Media CM106 / Turtle Beach Audio Advantage Roadie */
		return snd_usb_cm106_boot_quirk(dev);

	case USB_ID(0x0d8c, 0x0102):
		/* C-Media CM6206 / CM106-Like Sound Device */
	case USB_ID(0x0ccd, 0x00b1): /* Terratec Aureon 7.1 USB */
		return snd_usb_cm6206_boot_quirk(dev);

	case USB_ID(0x0dba, 0x3000):
		/* Digidesign Mbox 2 */
		return snd_usb_mbox2_boot_quirk(dev);

	case USB_ID(0x1235, 0x0010): /* Focusrite Novation Saffire 6 USB */
	case USB_ID(0x1235, 0x0018): /* Focusrite Novation Twitch */
		return snd_usb_novation_boot_quirk(dev);

	case USB_ID(0x133e, 0x0815):
		/* Access Music VirusTI Desktop */
		return snd_usb_accessmusic_boot_quirk(dev);

	case USB_ID(0x17cc, 0x1000): /* Komplete Audio 6 */
	case USB_ID(0x17cc, 0x1010): /* Traktor Audio 6 */
	case USB_ID(0x17cc, 0x1020): /* Traktor Audio 10 */
		return snd_usb_nativeinstruments_boot_quirk(dev);
	case USB_ID(0x0763, 0x2012):  /* M-Audio Fast Track Pro USB */
		return snd_usb_fasttrackpro_boot_quirk(dev);
	case USB_ID(0x047f, 0xc010): /* Plantronics Gamecom 780 */
		return snd_usb_gamecon780_boot_quirk(dev);
	case USB_ID(0x2466, 0x8010): /* Fractal Audio Axe-Fx 3 */
		return snd_usb_axefx3_boot_quirk(dev);
	}

	return 0;
}

/*
 * check if the device uses big-endian samples
 */
int snd_usb_is_big_endian_format(struct snd_usb_audio *chip, struct audioformat *fp)
{
	/* it depends on altsetting whether the device is big-endian or not */
	switch (chip->usb_id) {
	case USB_ID(0x0763, 0x2001): /* M-Audio Quattro: captured data only */
		if (fp->altsetting == 2 || fp->altsetting == 3 ||
			fp->altsetting == 5 || fp->altsetting == 6)
			return 1;
		break;
	case USB_ID(0x0763, 0x2003): /* M-Audio Audiophile USB */
		if (chip->setup == 0x00 ||
			fp->altsetting == 1 || fp->altsetting == 2 ||
			fp->altsetting == 3)
			return 1;
		break;
	case USB_ID(0x0763, 0x2012): /* M-Audio Fast Track Pro */
		if (fp->altsetting == 2 || fp->altsetting == 3 ||
			fp->altsetting == 5 || fp->altsetting == 6)
			return 1;
		break;
	}
	return 0;
}

/*
 * For E-Mu 0404USB/0202USB/TrackerPre/0204 sample rate should be set for device,
 * not for interface.
 */

enum {
	EMU_QUIRK_SR_44100HZ = 0,
	EMU_QUIRK_SR_48000HZ,
	EMU_QUIRK_SR_88200HZ,
	EMU_QUIRK_SR_96000HZ,
	EMU_QUIRK_SR_176400HZ,
	EMU_QUIRK_SR_192000HZ
};

static void set_format_emu_quirk(struct snd_usb_substream *subs,
				 struct audioformat *fmt)
{
	unsigned char emu_samplerate_id = 0;

	/* When capture is active
	 * sample rate shouldn't be changed
	 * by playback substream
	 */
	if (subs->direction == SNDRV_PCM_STREAM_PLAYBACK) {
		if (subs->stream->substream[SNDRV_PCM_STREAM_CAPTURE].interface != -1)
			return;
	}

	switch (fmt->rate_min) {
	case 48000:
		emu_samplerate_id = EMU_QUIRK_SR_48000HZ;
		break;
	case 88200:
		emu_samplerate_id = EMU_QUIRK_SR_88200HZ;
		break;
	case 96000:
		emu_samplerate_id = EMU_QUIRK_SR_96000HZ;
		break;
	case 176400:
		emu_samplerate_id = EMU_QUIRK_SR_176400HZ;
		break;
	case 192000:
		emu_samplerate_id = EMU_QUIRK_SR_192000HZ;
		break;
	default:
		emu_samplerate_id = EMU_QUIRK_SR_44100HZ;
		break;
	}
	snd_emuusb_set_samplerate(subs->stream->chip, emu_samplerate_id);
	subs->pkt_offset_adj = (emu_samplerate_id >= EMU_QUIRK_SR_176400HZ) ? 4 : 0;
}

void snd_usb_set_format_quirk(struct snd_usb_substream *subs,
			      struct audioformat *fmt)
{
	switch (subs->stream->chip->usb_id) {
	case USB_ID(0x041e, 0x3f02): /* E-Mu 0202 USB */
	case USB_ID(0x041e, 0x3f04): /* E-Mu 0404 USB */
	case USB_ID(0x041e, 0x3f0a): /* E-Mu Tracker Pre */
	case USB_ID(0x041e, 0x3f19): /* E-Mu 0204 USB */
		set_format_emu_quirk(subs, fmt);
		break;
	}
}

bool snd_usb_get_sample_rate_quirk(struct snd_usb_audio *chip)
{
	/* devices which do not support reading the sample rate. */
	switch (chip->usb_id) {
	case USB_ID(0x041E, 0x4080): /* Creative Live Cam VF0610 */
	case USB_ID(0x04D8, 0xFEEA): /* Benchmark DAC1 Pre */
	case USB_ID(0x0556, 0x0014): /* Phoenix Audio TMX320VC */
	case USB_ID(0x05A3, 0x9420): /* ELP HD USB Camera */
	case USB_ID(0x05a7, 0x1020): /* Bose Companion 5 */
	case USB_ID(0x074D, 0x3553): /* Outlaw RR2150 (Micronas UAC3553B) */
	case USB_ID(0x1395, 0x740a): /* Sennheiser DECT */
	case USB_ID(0x1901, 0x0191): /* GE B850V3 CP2114 audio interface */
	case USB_ID(0x21B4, 0x0081): /* AudioQuest DragonFly */
	case USB_ID(0x2912, 0x30c8): /* Audioengine D1 */
		return true;
	}

	/* devices of these vendors don't support reading rate, either */
	switch (USB_ID_VENDOR(chip->usb_id)) {
	case 0x045E: /* MS Lifecam */
	case 0x047F: /* Plantronics */
	case 0x1de7: /* Phoenix Audio */
		return true;
	}

	return false;
}

/* ITF-USB DSD based DACs need a vendor cmd to switch
 * between PCM and native DSD mode
 */
static bool is_itf_usb_dsd_dac(unsigned int id)
{
	switch (id) {
	case USB_ID(0x154e, 0x1002): /* Denon DCD-1500RE */
	case USB_ID(0x154e, 0x1003): /* Denon DA-300USB */
	case USB_ID(0x154e, 0x3005): /* Marantz HD-DAC1 */
	case USB_ID(0x154e, 0x3006): /* Marantz SA-14S1 */
	case USB_ID(0x1852, 0x5065): /* Luxman DA-06 */
	case USB_ID(0x0644, 0x8043): /* TEAC UD-501/UD-501V2/UD-503/NT-503 */
	case USB_ID(0x0644, 0x8044): /* Esoteric D-05X */
	case USB_ID(0x0644, 0x804a): /* TEAC UD-301 */
		return true;
	}
	return false;
}

int snd_usb_select_mode_quirk(struct snd_usb_substream *subs,
			      struct audioformat *fmt)
{
	struct usb_device *dev = subs->dev;
	int err;

	if (is_itf_usb_dsd_dac(subs->stream->chip->usb_id)) {
		/* First switch to alt set 0, otherwise the mode switch cmd
		 * will not be accepted by the DAC
		 */
		err = usb_set_interface(dev, fmt->iface, 0);
		if (err < 0)
			return err;

		msleep(20); /* Delay needed after setting the interface */

		/* Vendor mode switch cmd is required. */
		if (fmt->formats & SNDRV_PCM_FMTBIT_DSD_U32_BE) {
			/* DSD mode (DSD_U32) requested */
			err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0,
					      USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
					      1, 1, NULL, 0);
			if (err < 0)
				return err;

		} else {
			/* PCM or DOP mode (S32) requested */
			/* PCM mode (S16) requested */
			err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0,
					      USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE,
					      0, 1, NULL, 0);
			if (err < 0)
				return err;

		}
		msleep(20);
	}
	return 0;
}

void snd_usb_endpoint_start_quirk(struct snd_usb_endpoint *ep)
{
	/*
	 * "Playback Design" products send bogus feedback data at the start
	 * of the stream. Ignore them.
	 */
	if (USB_ID_VENDOR(ep->chip->usb_id) == 0x23ba &&
	    ep->type == SND_USB_ENDPOINT_TYPE_SYNC)
		ep->skip_packets = 4;

	/*
	 * M-Audio Fast Track C400/C600 - when packets are not skipped, real
	 * world latency varies by approx. +/- 50 frames (at 96KHz) each time
	 * the stream is (re)started. When skipping packets 16 at endpoint
	 * start up, the real world latency is stable within +/- 1 frame (also
	 * across power cycles).
	 */
	if ((ep->chip->usb_id == USB_ID(0x0763, 0x2030) ||
	     ep->chip->usb_id == USB_ID(0x0763, 0x2031)) &&
	    ep->type == SND_USB_ENDPOINT_TYPE_DATA)
		ep->skip_packets = 16;

	/* Work around devices that report unreasonable feedback data */
	if ((ep->chip->usb_id == USB_ID(0x0644, 0x8038) ||  /* TEAC UD-H01 */
	     ep->chip->usb_id == USB_ID(0x1852, 0x5034)) && /* T+A Dac8 */
	    ep->syncmaxsize == 4)
		ep->tenor_fb_quirk = 1;
}

void snd_usb_set_interface_quirk(struct usb_device *dev)
{
	struct snd_usb_audio *chip = dev_get_drvdata(&dev->dev);

	if (!chip)
		return;
	/*
	 * "Playback Design" products need a 50ms delay after setting the
	 * USB interface.
	 */
	switch (USB_ID_VENDOR(chip->usb_id)) {
	case 0x23ba: /* Playback Design */
	case 0x0644: /* TEAC Corp. */
		msleep(50);
		break;
	}
}

/* quirk applied after snd_usb_ctl_msg(); not applied during boot quirks */
void snd_usb_ctl_msg_quirk(struct usb_device *dev, unsigned int pipe,
			   __u8 request, __u8 requesttype, __u16 value,
			   __u16 index, void *data, __u16 size)
{
	struct snd_usb_audio *chip = dev_get_drvdata(&dev->dev);

	if (!chip)
		return;
	/*
	 * "Playback Design" products need a 20ms delay after each
	 * class compliant request
	 */
	if (USB_ID_VENDOR(chip->usb_id) == 0x23ba &&
	    (requesttype & USB_TYPE_MASK) == USB_TYPE_CLASS)
		msleep(20);

	/*
	 * "TEAC Corp." products need a 20ms delay after each
	 * class compliant request
	 */
	if (USB_ID_VENDOR(chip->usb_id) == 0x0644 &&
	    (requesttype & USB_TYPE_MASK) == USB_TYPE_CLASS)
		msleep(20);

	/* ITF-USB DSD based DACs functionality need a delay
	 * after each class compliant request
	 */
	if (is_itf_usb_dsd_dac(chip->usb_id)
	    && (requesttype & USB_TYPE_MASK) == USB_TYPE_CLASS)
		msleep(20);

	/* Zoom R16/24, Logitech H650e, Jabra 550a, Kingston HyperX needs a tiny
	 * delay here, otherwise requests like get/set frequency return as
	 * failed despite actually succeeding.
	 */
	if ((chip->usb_id == USB_ID(0x1686, 0x00dd) ||
	     chip->usb_id == USB_ID(0x046d, 0x0a46) ||
	     chip->usb_id == USB_ID(0x0b0e, 0x0349) ||
	     chip->usb_id == USB_ID(0x0951, 0x16ad)) &&
	    (requesttype & USB_TYPE_MASK) == USB_TYPE_CLASS)
		usleep_range(1000, 2000);

	/*
	 * Samsung USBC Headset (AKG) need a tiny delay after each
	 * class compliant request. (Model number: AAM625R or AAM627R)
	 */
	if (chip->usb_id == USB_ID(0x04e8, 0xa051) &&
	    (requesttype & USB_TYPE_MASK) == USB_TYPE_CLASS)
		usleep_range(5000, 6000);
}

/*
 * snd_usb_interface_dsd_format_quirks() is called from format.c to
 * augment the PCM format bit-field for DSD types. The UAC standards
 * don't have a designated bit field to denote DSD-capable interfaces,
 * hence all hardware that is known to support this format has to be
 * listed here.
 */
u64 snd_usb_interface_dsd_format_quirks(struct snd_usb_audio *chip,
					struct audioformat *fp,
					unsigned int sample_bytes)
{
	struct usb_interface *iface;

	/* Playback Designs */
	if (USB_ID_VENDOR(chip->usb_id) == 0x23ba &&
	    USB_ID_PRODUCT(chip->usb_id) < 0x0110) {
		switch (fp->altsetting) {
		case 1:
			fp->dsd_dop = true;
			return SNDRV_PCM_FMTBIT_DSD_U16_LE;
		case 2:
			fp->dsd_bitrev = true;
			return SNDRV_PCM_FMTBIT_DSD_U8;
		case 3:
			fp->dsd_bitrev = true;
			return SNDRV_PCM_FMTBIT_DSD_U16_LE;
		}
	}

	/* XMOS based USB DACs */
	switch (chip->usb_id) {
	case USB_ID(0x1511, 0x0037): /* AURALiC VEGA */
	case USB_ID(0x2522, 0x0012): /* LH Labs VI DAC Infinity */
	case USB_ID(0x2772, 0x0230): /* Pro-Ject Pre Box S2 Digital */
		if (fp->altsetting == 2)
			return SNDRV_PCM_FMTBIT_DSD_U32_BE;
		break;

	case USB_ID(0x0d8c, 0x0316): /* Hegel HD12 DSD */
	case USB_ID(0x10cb, 0x0103): /* The Bit Opus #3; with fp->dsd_raw */
	case USB_ID(0x16d0, 0x06b2): /* NuPrime DAC-10 */
	case USB_ID(0x16d0, 0x09dd): /* Encore mDSD */
	case USB_ID(0x16d0, 0x0733): /* Furutech ADL Stratos */
	case USB_ID(0x16d0, 0x09db): /* NuPrime Audio DAC-9 */
	case USB_ID(0x1db5, 0x0003): /* Bryston BDA3 */
	case USB_ID(0x22e1, 0xca01): /* HDTA Serenade DSD */
	case USB_ID(0x249c, 0x9326): /* M2Tech Young MkIII */
	case USB_ID(0x2616, 0x0106): /* PS Audio NuWave DAC */
	case USB_ID(0x2622, 0x0041): /* Audiolab M-DAC+ */
	case USB_ID(0x27f7, 0x3002): /* W4S DAC-2v2SE */
	case USB_ID(0x29a2, 0x0086): /* Mutec MC3+ USB */
	case USB_ID(0x6b42, 0x0042): /* MSB Technology */
		if (fp->altsetting == 3)
			return SNDRV_PCM_FMTBIT_DSD_U32_BE;
		break;

	/* Amanero Combo384 USB based DACs with native DSD support */
	case USB_ID(0x16d0, 0x071a):  /* Amanero - Combo384 */
	case USB_ID(0x2ab6, 0x0004):  /* T+A DAC8DSD-V2.0, MP1000E-V2.0, MP2000R-V2.0, MP2500R-V2.0, MP3100HV-V2.0 */
	case USB_ID(0x2ab6, 0x0005):  /* T+A USB HD Audio 1 */
	case USB_ID(0x2ab6, 0x0006):  /* T+A USB HD Audio 2 */
		if (fp->altsetting == 2) {
			switch (le16_to_cpu(chip->dev->descriptor.bcdDevice)) {
			case 0x199:
				return SNDRV_PCM_FMTBIT_DSD_U32_LE;
			case 0x19b:
			case 0x203:
				return SNDRV_PCM_FMTBIT_DSD_U32_BE;
			default:
				break;
			}
		}
		break;
	case USB_ID(0x16d0, 0x0a23):
		if (fp->altsetting == 2)
			return SNDRV_PCM_FMTBIT_DSD_U32_BE;
		break;

	default:
		break;
	}

	/* ITF-USB DSD based DACs */
	if (is_itf_usb_dsd_dac(chip->usb_id)) {
		iface = usb_ifnum_to_if(chip->dev, fp->iface);

		/* Altsetting 2 support native DSD if the num of altsets is
		 * three (0-2),
		 * Altsetting 3 support native DSD if the num of altsets is
		 * four (0-3).
		 */
		if (fp->altsetting == iface->num_altsetting - 1)
			return SNDRV_PCM_FMTBIT_DSD_U32_BE;
	}

	/* Mostly generic method to detect many DSD-capable implementations -
	 * from XMOS/Thesycon
	 */
	switch (USB_ID_VENDOR(chip->usb_id)) {
	case 0x152a:  /* Thesycon devices */
	case 0x20b1:  /* XMOS based devices */
	case 0x22d9:  /* Oppo */
	case 0x23ba:  /* Playback Designs */
	case 0x25ce:  /* Mytek devices */
	case 0x278b:  /* Rotel? */
	case 0x292b:  /* Gustard/Ess based devices */
	case 0x2ab6:  /* T+A devices */
	case 0x3842:  /* EVGA */
	case 0xc502:  /* HiBy devices */
		if (fp->dsd_raw)
			return SNDRV_PCM_FMTBIT_DSD_U32_BE;
		break;
	default:
		break;

	}

	return 0;
}

void snd_usb_audioformat_attributes_quirk(struct snd_usb_audio *chip,
					  struct audioformat *fp,
					  int stream)
{
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
}

/*
 * registration quirk:
 * the registration is skipped if a device matches with the given ID,
 * unless the interface reaches to the defined one.  This is for delaying
 * the registration until the last known interface, so that the card and
 * devices appear at the same time.
 */

struct registration_quirk {
	unsigned int usb_id;	/* composed via USB_ID() */
	unsigned int interface;	/* the interface to trigger register */
};

#define REG_QUIRK_ENTRY(vendor, product, iface) \
	{ .usb_id = USB_ID(vendor, product), .interface = (iface) }

static const struct registration_quirk registration_quirks[] = {
	REG_QUIRK_ENTRY(0x0951, 0x16d8, 2),	/* Kingston HyperX AMP */
	REG_QUIRK_ENTRY(0x0951, 0x16ed, 2),	/* Kingston HyperX Cloud Alpha S */
	REG_QUIRK_ENTRY(0x0951, 0x16ea, 2),	/* Kingston HyperX Cloud Flight S */
	{ 0 }					/* terminator */
};

/* return true if skipping registration */
bool snd_usb_registration_quirk(struct snd_usb_audio *chip, int iface)
{
	const struct registration_quirk *q;

	for (q = registration_quirks; q->usb_id; q++)
		if (chip->usb_id == q->usb_id)
			return iface != q->interface;

	/* Register as normal */
	return false;
}
