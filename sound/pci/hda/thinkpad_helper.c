// SPDX-License-Identifier: GPL-2.0
/* Helper functions for Thinkpad LED control;
 * to be included from codec driver
 */

#if IS_ENABLED(CONFIG_THINKPAD_ACPI) && IS_REACHABLE(CONFIG_LEDS_TRIGGER_AUDIO)

#include <linux/acpi.h>
#include <linux/leds.h>

static void (*old_vmaster_hook)(void *, int);

static bool is_thinkpad(struct hda_codec *codec)
{
	return (codec->core.subsystem_id >> 16 == 0x17aa) &&
	       (acpi_dev_found("LEN0068") || acpi_dev_found("LEN0268") ||
		acpi_dev_found("IBM0068"));
}

static void update_tpacpi_mute_led(void *private_data, int enabled)
{
	if (old_vmaster_hook)
		old_vmaster_hook(private_data, enabled);

	ledtrig_audio_set(LED_AUDIO_MUTE, enabled ? LED_OFF : LED_ON);
}

static void hda_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct hda_gen_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		if (!is_thinkpad(codec))
			return;
		old_vmaster_hook = spec->vmaster_mute.hook;
		spec->vmaster_mute.hook = update_tpacpi_mute_led;
		snd_hda_gen_fixup_micmute_led(codec, fix, action);
	}
}

#else /* CONFIG_THINKPAD_ACPI */

static void hda_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
}

#endif /* CONFIG_THINKPAD_ACPI */
