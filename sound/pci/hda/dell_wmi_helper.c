/* Helper functions for Dell Mic Mute LED control;
 * to be included from codec driver
 */

#if IS_ENABLED(CONFIG_LEDS_DELL_NETBOOKS)
#include <linux/dell-led.h>

static int dell_led_value;
static int (*dell_led_set_func)(int, int);
static void (*dell_old_cap_hook)(struct hda_codec *,
			         struct snd_kcontrol *,
				 struct snd_ctl_elem_value *);

static void update_dell_wmi_micmute_led(struct hda_codec *codec,
				        struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (dell_old_cap_hook)
		dell_old_cap_hook(codec, kcontrol, ucontrol);

	if (!ucontrol || !dell_led_set_func)
		return;
	if (strcmp("Capture Switch", ucontrol->id.name) == 0 && ucontrol->id.index == 0) {
		/* TODO: How do I verify if it's a mono or stereo here? */
		int val = (ucontrol->value.integer.value[0] || ucontrol->value.integer.value[1]) ? 0 : 1;
		if (val == dell_led_value)
			return;
		dell_led_value = val;
		if (dell_led_set_func)
			dell_led_set_func(DELL_LED_MICMUTE, dell_led_value);
	}
}


static void alc_fixup_dell_wmi(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	bool removefunc = false;

	if (action == HDA_FIXUP_ACT_PROBE) {
		if (!dell_led_set_func)
			dell_led_set_func = symbol_request(dell_app_wmi_led_set);
		if (!dell_led_set_func) {
			codec_warn(codec, "Failed to find dell wmi symbol dell_app_wmi_led_set\n");
			return;
		}

		removefunc = true;
		if (dell_led_set_func(DELL_LED_MICMUTE, false) >= 0) {
			dell_led_value = 0;
			if (spec->gen.num_adc_nids > 1)
				codec_dbg(codec, "Skipping micmute LED control due to several ADCs");
			else {
				dell_old_cap_hook = spec->gen.cap_sync_hook;
				spec->gen.cap_sync_hook = update_dell_wmi_micmute_led;
				removefunc = false;
			}
		}

	}

	if (dell_led_set_func && (action == HDA_FIXUP_ACT_FREE || removefunc)) {
		symbol_put(dell_app_wmi_led_set);
		dell_led_set_func = NULL;
		dell_old_cap_hook = NULL;
	}
}

#else /* CONFIG_LEDS_DELL_NETBOOKS */
static void alc_fixup_dell_wmi(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
}

#endif /* CONFIG_LEDS_DELL_NETBOOKS */
