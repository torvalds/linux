// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>

#include "us144mkii.h"

/**
 * @brief Text descriptions for playback output source options.
 *
 * Used by ALSA kcontrol elements to provide user-friendly names for
 * the playback routing options (e.g., "Playback 1-2", "Playback 3-4").
 */
static const char *const playback_source_texts[] = { "Playback 1-2",
						     "Playback 3-4" };

/**
 * @brief Text descriptions for capture input source options.
 *
 * Used by ALSA kcontrol elements to provide user-friendly names for
 * the capture routing options (e.g., "Analog In", "Digital In").
 */
static const char *const capture_source_texts[] = { "Analog In", "Digital In" };

/**
 * tascam_playback_source_info() - ALSA control info callback for playback
 * source.
 * @kcontrol: The ALSA kcontrol instance.
 * @uinfo: The ALSA control element info structure to fill.
 *
 * This function provides information about the enumerated playback source
 * control, including its type, count, and available items (Playback 1-2,
 * Playback 3-4).
 *
 * Return: 0 on success.
 */
static int tascam_playback_source_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, 2, playback_source_texts);
}

/**
 * tascam_line_out_get() - ALSA control get callback for Line Outputs Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure to fill.
 *
 * This function retrieves the current selection for the Line Outputs source
 * (Playback 1-2 or Playback 3-4) from the driver's private data and populates
 * the ALSA control element value.
 *
 * Return: 0 on success.
 */
static int tascam_line_out_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		ucontrol->value.enumerated.item[0] = tascam->line_out_source;
	}
	return 0;
}

/**
 * tascam_line_out_put() - ALSA control put callback for Line Outputs Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure containing the new value.
 *
 * This function sets the Line Outputs source (Playback 1-2 or Playback 3-4)
 * based on the user's selection from the ALSA control element. It validates
 * the input and updates the driver's private data.
 *
 * Return: 1 if the value was changed, 0 if unchanged, or a negative error code.
 */
static int tascam_line_out_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->line_out_source != ucontrol->value.enumerated.item[0]) {
			tascam->line_out_source = ucontrol->value.enumerated.item[0];
			changed = 1;
		}
	}
	return changed;
}

/**
 * tascam_line_out_control - ALSA kcontrol definition for Line Outputs Source.
 *
 * This defines a new ALSA mixer control named "Line OUTPUTS Source" that allows
 * the user to select between "Playback 1-2" and "Playback 3-4" for the analog
 * line outputs of the device. It uses the `tascam_playback_source_info` for
 * information and `tascam_line_out_get`/`tascam_line_out_put` for value
 * handling.
 */
static const struct snd_kcontrol_new tascam_line_out_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line Playback Source",
	.info = tascam_playback_source_info,
	.get = tascam_line_out_get,
	.put = tascam_line_out_put,
};

/**
 * tascam_digital_out_get() - ALSA control get callback for Digital Outputs
 * Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure to fill.
 *
 * This function retrieves the current selection for the Digital Outputs source
 * (Playback 1-2 or Playback 3-4) from the driver's private data and populates
 * the ALSA control element value.
 *
 * Return: 0 on success.
 */
static int tascam_digital_out_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		ucontrol->value.enumerated.item[0] = tascam->digital_out_source;
	}
	return 0;
}

/**
 * tascam_digital_out_put() - ALSA control put callback for Digital Outputs
 * Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure containing the new value.
 *
 * This function sets the Digital Outputs source (Playback 1-2 or Playback 3-4)
 * based on the user's selection from the ALSA control element. It validates
 * the input and updates the driver's private data.
 *
 * Return: 1 if the value was changed, 0 if unchanged, or a negative error code.
 */
static int tascam_digital_out_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->digital_out_source != ucontrol->value.enumerated.item[0]) {
			tascam->digital_out_source = ucontrol->value.enumerated.item[0];
			changed = 1;
		}
	}
	return changed;
}

/**
 * tascam_digital_out_control - ALSA kcontrol definition for Digital Outputs
 * Source.
 *
 * This defines a new ALSA mixer control named "Digital OUTPUTS Source" that
 * allows the user to select between "Playback 1-2" and "Playback 3-4" for the
 * digital outputs of the device. It uses the `tascam_playback_source_info` for
 * information and `tascam_digital_out_get`/`tascam_digital_out_put` for value
 * handling.
 */
static const struct snd_kcontrol_new tascam_digital_out_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Source",
	.info = tascam_playback_source_info,
	.get = tascam_digital_out_get,
	.put = tascam_digital_out_put,
};

/**
 * tascam_capture_source_info() - ALSA control info callback for capture source.
 * @kcontrol: The ALSA kcontrol instance.
 * @uinfo: The ALSA control element info structure to fill.
 *
 * This function provides information about the enumerated capture source
 * control, including its type, count, and available items (Analog In, Digital
 * In).
 *
 * Return: 0 on success.
 */
static int tascam_capture_source_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, 2, capture_source_texts);
}

/**
 * tascam_capture_12_get() - ALSA control get callback for Capture channels 1
 * and 2 Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure to fill.
 *
 * This function retrieves the current selection for the Capture channels 1 and
 * 2 source (Analog In or Digital In) from the driver's private data and
 * populates the ALSA control element value.
 *
 * Return: 0 on success.
 */
static int tascam_capture_12_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		ucontrol->value.enumerated.item[0] = tascam->capture_12_source;
	}
	return 0;
}

/**
 * tascam_capture_12_put() - ALSA control put callback for Capture channels 1
 * and 2 Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure containing the new value.
 *
 * This function sets the Capture channels 1 and 2 source (Analog In or Digital
 * In) based on the user's selection from the ALSA control element. It validates
 * the input and updates the driver's private data.
 *
 * Return: 1 if the value was changed, 0 if unchanged, or a negative error code.
 */
static int tascam_capture_12_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->capture_12_source != ucontrol->value.enumerated.item[0]) {
			tascam->capture_12_source = ucontrol->value.enumerated.item[0];
			changed = 1;
		}
	}
	return changed;
}

/**
 * tascam_capture_12_control - ALSA kcontrol definition for Capture channels 1
 * and 2 Source.
 *
 * This defines a new ALSA mixer control named "ch1 and ch2 Source" that allows
 * the user to select between "Analog In" and "Digital In" for the first two
 * capture channels of the device. It uses the `tascam_capture_source_info` for
 * information and `tascam_capture_12_get`/`tascam_capture_12_put` for value
 * handling.
 */
static const struct snd_kcontrol_new tascam_capture_12_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Ch1/2 Capture Source",
	.info = tascam_capture_source_info,
	.get = tascam_capture_12_get,
	.put = tascam_capture_12_put,
};

/**
 * tascam_capture_34_get() - ALSA control get callback for Capture channels 3
 * and 4 Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure to fill.
 *
 * This function retrieves the current selection for the Capture channels 3 and
 * 4 source (Analog In or Digital In) from the driver's private data and
 * populates the ALSA control element value.
 *
 * Return: 0 on success.
 */
static int tascam_capture_34_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		ucontrol->value.enumerated.item[0] = tascam->capture_34_source;
	}
	return 0;
}

/**
 * tascam_capture_34_put() - ALSA control put callback for Capture channels 3
 * and 4 Source.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure containing the new value.
 *
 * This function sets the Capture channels 3 and 4 source (Analog In or Digital
 * In) based on the user's selection from the ALSA control element. It validates
 * the input and updates the driver's private data.
 *
 * Return: 1 if the value was changed, 0 if unchanged, or a negative error code.
 */
static int tascam_capture_34_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->capture_34_source != ucontrol->value.enumerated.item[0]) {
			tascam->capture_34_source = ucontrol->value.enumerated.item[0];
			changed = 1;
		}
	}
	return changed;
}

/**
 * tascam_capture_34_control - ALSA kcontrol definition for Capture channels 3
 * and 4 Source.
 *
 * This defines a new ALSA mixer control named "ch3 and ch4 Source" that allows
 * the user to select between "Analog In" and "Digital In" for the third and
 * fourth capture channels of the device. It uses the
 * `tascam_capture_source_info` for information and
 * `tascam_capture_34_get`/`tascam_capture_34_put` for value handling.
 */
static const struct snd_kcontrol_new tascam_capture_34_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Ch3/4 Capture Source",
	.info = tascam_capture_source_info,
	.get = tascam_capture_34_get,
	.put = tascam_capture_34_put,
};

/**
 * tascam_samplerate_info() - ALSA control info callback for Sample Rate.
 * @kcontrol: The ALSA kcontrol instance.
 * @uinfo: The ALSA control element info structure to fill.
 *
 * This function provides information about the Sample Rate control, defining
 * it as an integer type with a minimum value of 0 and a maximum of 96000.
 *
 * Return: 0 on success.
 */
static int tascam_samplerate_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 96000;
	return 0;
}

/**
 * tascam_samplerate_get() - ALSA control get callback for Sample Rate.
 * @kcontrol: The ALSA kcontrol instance.
 * @ucontrol: The ALSA control element value structure to fill.
 *
 * This function retrieves the current sample rate from the device via a USB
 * control message and populates the ALSA control element value. If the rate
 * is already known (i.e., `current_rate` is set), it returns that value
 * directly.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int tascam_samplerate_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct tascam_card *tascam =
		(struct tascam_card *)snd_kcontrol_chip(kcontrol);
	u8 *buf __free(kfree) = NULL;
	int err;
	u32 rate = 0;

	scoped_guard(spinlock_irqsave, &tascam->lock) {
		if (tascam->current_rate > 0) {
			ucontrol->value.integer.value[0] = tascam->current_rate;
			return 0;
		}
	}

	buf = kmalloc(3, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	err = usb_control_msg(tascam->dev, usb_rcvctrlpipe(tascam->dev, 0),
			      UAC_GET_CUR, RT_D2H_CLASS_EP,
			      UAC_SAMPLING_FREQ_CONTROL, EP_AUDIO_IN, buf, 3,
			      USB_CTRL_TIMEOUT_MS);

	if (err >= 3)
		rate = buf[0] | (buf[1] << 8) | (buf[2] << 16);

	ucontrol->value.integer.value[0] = rate;
	return 0;
}

/**
 * tascam_samplerate_control - ALSA kcontrol definition for Sample Rate.
 *
 * This defines a new ALSA mixer control named "Sample Rate" that displays
 * the current sample rate of the device. It is a read-only control.
 */
static const struct snd_kcontrol_new tascam_samplerate_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sample Rate",
	.info = tascam_samplerate_info,
	.get = tascam_samplerate_get,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
};

int tascam_create_controls(struct tascam_card *tascam)
{
	int err;

	err = snd_ctl_add(tascam->card,
			  snd_ctl_new1(&tascam_line_out_control, tascam));
	if (err < 0)
		return err;
	err = snd_ctl_add(tascam->card,
			  snd_ctl_new1(&tascam_digital_out_control, tascam));
	if (err < 0)
		return err;
	err = snd_ctl_add(tascam->card,
			  snd_ctl_new1(&tascam_capture_12_control, tascam));
	if (err < 0)
		return err;
	err = snd_ctl_add(tascam->card,
			  snd_ctl_new1(&tascam_capture_34_control, tascam));
	if (err < 0)
		return err;

	err = snd_ctl_add(tascam->card,
			  snd_ctl_new1(&tascam_samplerate_control, tascam));
	if (err < 0)
		return err;

	return 0;
}
