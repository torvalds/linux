/* Helper functions for Thinkpad LED control;
 * to be included from codec driver
 */

#if IS_ENABLED(CONFIG_THINKPAD_ACPI)

#include <linux/acpi.h>
#include <linux/thinkpad_acpi.h>

static int (*led_set_func)(int, bool);
static void (*old_vmaster_hook)(void *, int);

static acpi_status acpi_check_cb(acpi_handle handle, u32 lvl, void *context,
				 void **rv)
{
	bool *found = context;
	*found = true;
	return AE_OK;
}

static bool is_thinkpad(struct hda_codec *codec)
{
	bool found = false;
	if (codec->subsystem_id >> 16 != 0x17aa)
		return false;
	if (ACPI_SUCCESS(acpi_get_devices("LEN0068", acpi_check_cb, &found, NULL)) && found)
		return true;
	found = false;
	return ACPI_SUCCESS(acpi_get_devices("IBM0068", acpi_check_cb, &found, NULL)) && found;
}

static void update_tpacpi_mute_led(void *private_data, int enabled)
{
	if (old_vmaster_hook)
		old_vmaster_hook(private_data, enabled);

	if (led_set_func)
		led_set_func(TPACPI_LED_MUTE, !enabled);
}

static void update_tpacpi_micmute_led(struct hda_codec *codec,
				      struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	if (!ucontrol || !led_set_func)
		return;
	if (strcmp("Capture Switch", ucontrol->id.name) == 0 && ucontrol->id.index == 0) {
		/* TODO: How do I verify if it's a mono or stereo here? */
		bool val = ucontrol->value.integer.value[0] || ucontrol->value.integer.value[1];
		led_set_func(TPACPI_LED_MICMUTE, !val);
	}
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
		if (led_set_func(TPACPI_LED_MICMUTE, false) >= 0) {
			if (spec->num_adc_nids > 1)
				codec_dbg(codec,
					  "Skipping micmute LED control due to several ADCs");
			else {
				spec->cap_sync_hook = update_tpacpi_micmute_led;
				removefunc = false;
			}
		}
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
