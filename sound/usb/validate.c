// SPDX-License-Identifier: GPL-2.0-or-later
//
// Validation of USB-audio class descriptors
//

#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>
#include <linux/usb/midi.h>
#include "usbaudio.h"
#include "helper.h"

struct usb_desc_validator {
	unsigned char protocol;
	unsigned char type;
	bool (*func)(const void *p, const struct usb_desc_validator *v);
	size_t size;
};

#define UAC_VERSION_ALL		(unsigned char)(-1)

/* UAC1 only */
static bool validate_uac1_header(const void *p,
				 const struct usb_desc_validator *v)
{
	const struct uac1_ac_header_descriptor *d = p;

	return d->bLength >= sizeof(*d) &&
		d->bLength >= sizeof(*d) + d->bInCollection;
}

/* for mixer unit; covering all UACs */
static bool validate_mixer_unit(const void *p,
				const struct usb_desc_validator *v)
{
	const struct uac_mixer_unit_descriptor *d = p;
	size_t len;

	if (d->bLength < sizeof(*d) || !d->bNrInPins)
		return false;
	len = sizeof(*d) + d->bNrInPins;
	/* We can't determine the bitmap size only from this unit descriptor,
	 * so just check with the remaining length.
	 * The actual bitmap is checked at mixer unit parser.
	 */
	switch (v->protocol) {
	case UAC_VERSION_1:
	default:
		len += 2 + 1; /* wChannelConfig, iChannelNames */
		/* bmControls[n*m] */
		len += 1; /* iMixer */
		break;
	case UAC_VERSION_2:
		len += 4 + 1; /* bmChannelConfig, iChannelNames */
		/* bmMixerControls[n*m] */
		len += 1 + 1; /* bmControls, iMixer */
		break;
	case UAC_VERSION_3:
		len += 2; /* wClusterDescrID */
		/* bmMixerControls[n*m] */
		break;
	}
	return d->bLength >= len;
}

/* both for processing and extension units; covering all UACs */
static bool validate_processing_unit(const void *p,
				     const struct usb_desc_validator *v)
{
	const struct uac_processing_unit_descriptor *d = p;
	const unsigned char *hdr = p;
	size_t len, m;

	if (d->bLength < sizeof(*d))
		return false;
	len = sizeof(*d) + d->bNrInPins;
	if (d->bLength < len)
		return false;
	switch (v->protocol) {
	case UAC_VERSION_1:
	default:
		/* bNrChannels, wChannelConfig, iChannelNames, bControlSize */
		len += 1 + 2 + 1 + 1;
		if (d->bLength < len) /* bControlSize */
			return false;
		m = hdr[len];
		len += 1 + m + 1; /* bControlSize, bmControls, iProcessing */
		break;
	case UAC_VERSION_2:
		/* bNrChannels, bmChannelConfig, iChannelNames */
		len += 1 + 4 + 1;
		if (v->type == UAC2_PROCESSING_UNIT_V2)
			len += 2; /* bmControls -- 2 bytes for PU */
		else
			len += 1; /* bmControls -- 1 byte for EU */
		len += 1; /* iProcessing */
		break;
	case UAC_VERSION_3:
		/* wProcessingDescrStr, bmControls */
		len += 2 + 4;
		break;
	}
	if (d->bLength < len)
		return false;

	switch (v->protocol) {
	case UAC_VERSION_1:
	default:
		if (v->type == UAC1_EXTENSION_UNIT)
			return true; /* OK */
		switch (d->wProcessType) {
		case UAC_PROCESS_UP_DOWNMIX:
		case UAC_PROCESS_DOLBY_PROLOGIC:
			if (d->bLength < len + 1) /* bNrModes */
				return false;
			m = hdr[len];
			len += 1 + m * 2; /* bNrModes, waModes(n) */
			break;
		default:
			break;
		}
		break;
	case UAC_VERSION_2:
		if (v->type == UAC2_EXTENSION_UNIT_V2)
			return true; /* OK */
		switch (d->wProcessType) {
		case UAC2_PROCESS_UP_DOWNMIX:
		case UAC2_PROCESS_DOLBY_PROLOCIC: /* SiC! */
			if (d->bLength < len + 1) /* bNrModes */
				return false;
			m = hdr[len];
			len += 1 + m * 4; /* bNrModes, daModes(n) */
			break;
		default:
			break;
		}
		break;
	case UAC_VERSION_3:
		if (v->type == UAC3_EXTENSION_UNIT) {
			len += 2; /* wClusterDescrID */
			break;
		}
		switch (d->wProcessType) {
		case UAC3_PROCESS_UP_DOWNMIX:
			if (d->bLength < len + 1) /* bNrModes */
				return false;
			m = hdr[len];
			len += 1 + m * 2; /* bNrModes, waClusterDescrID(n) */
			break;
		case UAC3_PROCESS_MULTI_FUNCTION:
			len += 2 + 4; /* wClusterDescrID, bmAlgorighms */
			break;
		default:
			break;
		}
		break;
	}
	if (d->bLength < len)
		return false;

	return true;
}

/* both for selector and clock selector units; covering all UACs */
static bool validate_selector_unit(const void *p,
				   const struct usb_desc_validator *v)
{
	const struct uac_selector_unit_descriptor *d = p;
	size_t len;

	if (d->bLength < sizeof(*d))
		return false;
	len = sizeof(*d) + d->bNrInPins;
	switch (v->protocol) {
	case UAC_VERSION_1:
	default:
		len += 1; /* iSelector */
		break;
	case UAC_VERSION_2:
		len += 1 + 1; /* bmControls, iSelector */
		break;
	case UAC_VERSION_3:
		len += 4 + 2; /* bmControls, wSelectorDescrStr */
		break;
	}
	return d->bLength >= len;
}

static bool validate_uac1_feature_unit(const void *p,
				       const struct usb_desc_validator *v)
{
	const struct uac_feature_unit_descriptor *d = p;

	if (d->bLength < sizeof(*d) || !d->bControlSize)
		return false;
	/* at least bmaControls(0) for master channel + iFeature */
	return d->bLength >= sizeof(*d) + d->bControlSize + 1;
}

static bool validate_uac2_feature_unit(const void *p,
				       const struct usb_desc_validator *v)
{
	const struct uac2_feature_unit_descriptor *d = p;

	if (d->bLength < sizeof(*d))
		return false;
	/* at least bmaControls(0) for master channel + iFeature */
	return d->bLength >= sizeof(*d) + 4 + 1;
}

static bool validate_uac3_feature_unit(const void *p,
				       const struct usb_desc_validator *v)
{
	const struct uac3_feature_unit_descriptor *d = p;

	if (d->bLength < sizeof(*d))
		return false;
	/* at least bmaControls(0) for master channel + wFeatureDescrStr */
	return d->bLength >= sizeof(*d) + 4 + 2;
}

static bool validate_midi_out_jack(const void *p,
				   const struct usb_desc_validator *v)
{
	const struct usb_midi_out_jack_descriptor *d = p;

	return d->bLength >= sizeof(*d) &&
		d->bLength >= sizeof(*d) + d->bNrInputPins * 2;
}

#define FIXED(p, t, s) { .protocol = (p), .type = (t), .size = sizeof(s) }
#define FUNC(p, t, f) { .protocol = (p), .type = (t), .func = (f) }

static struct usb_desc_validator audio_validators[] = {
	/* UAC1 */
	FUNC(UAC_VERSION_1, UAC_HEADER, validate_uac1_header),
	FIXED(UAC_VERSION_1, UAC_INPUT_TERMINAL,
	      struct uac_input_terminal_descriptor),
	FIXED(UAC_VERSION_1, UAC_OUTPUT_TERMINAL,
	      struct uac1_output_terminal_descriptor),
	FUNC(UAC_VERSION_1, UAC_MIXER_UNIT, validate_mixer_unit),
	FUNC(UAC_VERSION_1, UAC_SELECTOR_UNIT, validate_selector_unit),
	FUNC(UAC_VERSION_1, UAC_FEATURE_UNIT, validate_uac1_feature_unit),
	FUNC(UAC_VERSION_1, UAC1_PROCESSING_UNIT, validate_processing_unit),
	FUNC(UAC_VERSION_1, UAC1_EXTENSION_UNIT, validate_processing_unit),

	/* UAC2 */
	FIXED(UAC_VERSION_2, UAC_HEADER, struct uac2_ac_header_descriptor),
	FIXED(UAC_VERSION_2, UAC_INPUT_TERMINAL,
	      struct uac2_input_terminal_descriptor),
	FIXED(UAC_VERSION_2, UAC_OUTPUT_TERMINAL,
	      struct uac2_output_terminal_descriptor),
	FUNC(UAC_VERSION_2, UAC_MIXER_UNIT, validate_mixer_unit),
	FUNC(UAC_VERSION_2, UAC_SELECTOR_UNIT, validate_selector_unit),
	FUNC(UAC_VERSION_2, UAC_FEATURE_UNIT, validate_uac2_feature_unit),
	/* UAC_VERSION_2, UAC2_EFFECT_UNIT: not implemented yet */
	FUNC(UAC_VERSION_2, UAC2_PROCESSING_UNIT_V2, validate_processing_unit),
	FUNC(UAC_VERSION_2, UAC2_EXTENSION_UNIT_V2, validate_processing_unit),
	FIXED(UAC_VERSION_2, UAC2_CLOCK_SOURCE,
	      struct uac_clock_source_descriptor),
	FUNC(UAC_VERSION_2, UAC2_CLOCK_SELECTOR, validate_selector_unit),
	FIXED(UAC_VERSION_2, UAC2_CLOCK_MULTIPLIER,
	      struct uac_clock_multiplier_descriptor),
	/* UAC_VERSION_2, UAC2_SAMPLE_RATE_CONVERTER: not implemented yet */

	/* UAC3 */
	FIXED(UAC_VERSION_2, UAC_HEADER, struct uac3_ac_header_descriptor),
	FIXED(UAC_VERSION_3, UAC_INPUT_TERMINAL,
	      struct uac3_input_terminal_descriptor),
	FIXED(UAC_VERSION_3, UAC_OUTPUT_TERMINAL,
	      struct uac3_output_terminal_descriptor),
	/* UAC_VERSION_3, UAC3_EXTENDED_TERMINAL: not implemented yet */
	FUNC(UAC_VERSION_3, UAC3_MIXER_UNIT, validate_mixer_unit),
	FUNC(UAC_VERSION_3, UAC3_SELECTOR_UNIT, validate_selector_unit),
	FUNC(UAC_VERSION_3, UAC_FEATURE_UNIT, validate_uac3_feature_unit),
	/*  UAC_VERSION_3, UAC3_EFFECT_UNIT: not implemented yet */
	FUNC(UAC_VERSION_3, UAC3_PROCESSING_UNIT, validate_processing_unit),
	FUNC(UAC_VERSION_3, UAC3_EXTENSION_UNIT, validate_processing_unit),
	FIXED(UAC_VERSION_3, UAC3_CLOCK_SOURCE,
	      struct uac3_clock_source_descriptor),
	FUNC(UAC_VERSION_3, UAC3_CLOCK_SELECTOR, validate_selector_unit),
	FIXED(UAC_VERSION_3, UAC3_CLOCK_MULTIPLIER,
	      struct uac3_clock_multiplier_descriptor),
	/* UAC_VERSION_3, UAC3_SAMPLE_RATE_CONVERTER: not implemented yet */
	/* UAC_VERSION_3, UAC3_CONNECTORS: not implemented yet */
	{ } /* terminator */
};

static struct usb_desc_validator midi_validators[] = {
	FIXED(UAC_VERSION_ALL, USB_MS_HEADER,
	      struct usb_ms_header_descriptor),
	FIXED(UAC_VERSION_ALL, USB_MS_MIDI_IN_JACK,
	      struct usb_midi_in_jack_descriptor),
	FUNC(UAC_VERSION_ALL, USB_MS_MIDI_OUT_JACK,
	     validate_midi_out_jack),
	{ } /* terminator */
};


/* Validate the given unit descriptor, return true if it's OK */
static bool validate_desc(unsigned char *hdr, int protocol,
			  const struct usb_desc_validator *v)
{
	if (hdr[1] != USB_DT_CS_INTERFACE)
		return true; /* don't care */

	for (; v->type; v++) {
		if (v->type == hdr[2] &&
		    (v->protocol == UAC_VERSION_ALL ||
		     v->protocol == protocol)) {
			if (v->func)
				return v->func(hdr, v);
			/* check for the fixed size */
			return hdr[0] >= v->size;
		}
	}

	return true; /* not matching, skip validation */
}

bool snd_usb_validate_audio_desc(void *p, int protocol)
{
	return validate_desc(p, protocol, audio_validators);
}

bool snd_usb_validate_midi_desc(void *p)
{
	return validate_desc(p, UAC_VERSION_1, midi_validators);
}

