// SPDX-License-Identifier: GPL-2.0
/* Helper functions for Thinkpad LED control;
 * to be included from codec driver
 */

#if IS_ENABLED(CONFIG_THINKPAD_ACPI)

#include <linux/acpi.h>
#include <linux/thinkpad_acpi.h>

static int (*led_set_func)(int, bool);
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

	if (led_set_func)
		led_set_func(TPACPI_LED_MUTE, !enabled);
}

static void update_tpacpi_micmute(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	led_set_func(TPACPI_LED_MICMUTE, spec->micmute_led.led_value);
}

static void hda_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct hda_gen_spec *spec = codec->spec;
	bool removefunc = false;

	if (action == HDA_FIXUP_ACT_PROBE) {
		if (!is_thinkpad(codec))
			return;
		if (!led_set_func)
			led_set_func = symbol_request(tpacpi_led_set);
		if (!led_set_func) {
			codec_warn(codec,
				   "Failed to find thinkpad-acpi symbol tpacpi_led_set\n");
			return;
		}

		removefunc = true;
		if (led_set_func(TPACPI_LED_MUTE, false) >= 0) {
			old_vmaster_hook = spec->vmaster_mute.hook;
			spec->vmaster_mute.hook = update_tpacpi_mute_led;
			removefunc = false;
		}
		if (led_set_func(TPACPI_LED_MICMUTE, false) >= 0 &&
		    snd_hda_gen_add_micmute_led(codec,
						update_tpacpi_micmute) > 0)
			removefunc = false;
	}

	if (led_set_func && (action == HDA_FIXUP_ACT_FREE || removefunc)) {
		symbol_put(tpacpi_led_set);
		led_set_func = NULL;
		old_vmaster_hook = NULL;
	}
}

#else /* CONFIG_THINKPAD_ACPI */

static void hda_fixup_thinkpad_acpi(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
}

#endif /* CONFIG_THINKPAD_ACPI */
