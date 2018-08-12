// SPDX-License-Identifier: GPL-2.0
/* Helper functions for Dell Mic Mute LED control;
 * to be included from codec driver
 */

#if IS_ENABLED(CONFIG_DELL_LAPTOP)
#include <linux/dell-led.h>

static int (*dell_micmute_led_set_func)(int);

static void dell_micmute_update(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	dell_micmute_led_set_func(spec->micmute_led.led_value);
}

static void alc_fixup_dell_wmi(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	bool removefunc = false;

	if (action == HDA_FIXUP_ACT_PROBE) {
		if (!dell_micmute_led_set_func)
			dell_micmute_led_set_func = symbol_request(dell_micmute_led_set);
		if (!dell_micmute_led_set_func) {
			codec_warn(codec, "Failed to find dell wmi symbol dell_micmute_led_set\n");
			return;
		}

		removefunc = (dell_micmute_led_set_func(false) < 0) ||
			(snd_hda_gen_add_micmute_led(codec,
						     dell_micmute_update) <= 0);
	}

	if (dell_micmute_led_set_func && (action == HDA_FIXUP_ACT_FREE || removefunc)) {
		symbol_put(dell_micmute_led_set);
		dell_micmute_led_set_func = NULL;
	}
}

#else /* CONFIG_DELL_LAPTOP */
static void alc_fixup_dell_wmi(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
}

#endif /* CONFIG_DELL_LAPTOP */
