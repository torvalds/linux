// SPDX-License-Identifier: GPL-2.0-or-later
//
// Realtek HD-audio codec support code
//

#ifndef __HDA_REALTEK_H
#define __HDA_REALTEK_H

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/ctype.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "../generic.h"
#include "../side-codecs/hda_component.h"

/* extra amp-initialization sequence types */
enum {
	ALC_INIT_UNDEFINED,
	ALC_INIT_NONE,
	ALC_INIT_DEFAULT,
};

enum {
	ALC_HEADSET_MODE_UNKNOWN,
	ALC_HEADSET_MODE_UNPLUGGED,
	ALC_HEADSET_MODE_HEADSET,
	ALC_HEADSET_MODE_MIC,
	ALC_HEADSET_MODE_HEADPHONE,
};

enum {
	ALC_HEADSET_TYPE_UNKNOWN,
	ALC_HEADSET_TYPE_CTIA,
	ALC_HEADSET_TYPE_OMTP,
};

enum {
	ALC_KEY_MICMUTE_INDEX,
};

struct alc_customize_define {
	unsigned int  sku_cfg;
	unsigned char port_connectivity;
	unsigned char check_sum;
	unsigned char customization;
	unsigned char external_amp;
	unsigned int  enable_pcbeep:1;
	unsigned int  platform_type:1;
	unsigned int  swap:1;
	unsigned int  override:1;
	unsigned int  fixup:1; /* Means that this sku is set by driver, not read from hw */
};

struct alc_coef_led {
	unsigned int idx;
	unsigned int mask;
	unsigned int on;
	unsigned int off;
};

struct alc_spec {
	struct hda_gen_spec gen; /* must be at head */

	/* codec parameterization */
	struct alc_customize_define cdefine;
	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

	/* GPIO bits */
	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	bool gpio_write_delay;	/* add a delay before writing gpio_data */

	/* mute LED for HP laptops, see vref_mute_led_set() */
	int mute_led_polarity;
	int micmute_led_polarity;
	hda_nid_t mute_led_nid;
	hda_nid_t cap_mute_led_nid;

	unsigned int gpio_mute_led_mask;
	unsigned int gpio_mic_led_mask;
	struct alc_coef_led mute_led_coef;
	struct alc_coef_led mic_led_coef;
	struct mutex coef_mutex;

	hda_nid_t headset_mic_pin;
	hda_nid_t headphone_mic_pin;
	int current_headset_mode;
	int current_headset_type;

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*power_hook)(struct hda_codec *codec);
	void (*shutup)(struct hda_codec *codec);

	int init_amp;
	int codec_variant;	/* flag for other variants */
	unsigned int has_alc5505_dsp:1;
	unsigned int no_depop_delay:1;
	unsigned int done_hp_init:1;
	unsigned int no_shutup_pins:1;
	unsigned int ultra_low_power:1;
	unsigned int has_hs_key:1;
	unsigned int no_internal_mic_pin:1;
	unsigned int en_3kpull_low:1;
	int num_speaker_amps;

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;
	unsigned int coef0;
	struct input_dev *kb_dev;
	u8 alc_mute_keycode_map[1];

	/* component binding */
	struct hda_component_parent comps;
};

int alc_read_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			unsigned int coef_idx);
void alc_write_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			  unsigned int coef_idx, unsigned int coef_val);
void alc_update_coefex_idx(struct hda_codec *codec, hda_nid_t nid,
			   unsigned int coef_idx, unsigned int mask,
			   unsigned int bits_set);
#define alc_read_coef_idx(codec, coef_idx) \
	alc_read_coefex_idx(codec, 0x20, coef_idx)
#define alc_write_coef_idx(codec, coef_idx, coef_val) \
	alc_write_coefex_idx(codec, 0x20, coef_idx, coef_val)
#define alc_update_coef_idx(codec, coef_idx, mask, bits_set)	\
	alc_update_coefex_idx(codec, 0x20, coef_idx, mask, bits_set)

unsigned int alc_get_coef0(struct hda_codec *codec);

/* coef writes/updates batch */
struct coef_fw {
	unsigned char nid;
	unsigned char idx;
	unsigned short mask;
	unsigned short val;
};

#define UPDATE_COEFEX(_nid, _idx, _mask, _val) \
	{ .nid = (_nid), .idx = (_idx), .mask = (_mask), .val = (_val) }
#define WRITE_COEFEX(_nid, _idx, _val) UPDATE_COEFEX(_nid, _idx, -1, _val)
#define WRITE_COEF(_idx, _val) WRITE_COEFEX(0x20, _idx, _val)
#define UPDATE_COEF(_idx, _mask, _val) UPDATE_COEFEX(0x20, _idx, _mask, _val)

void alc_process_coef_fw(struct hda_codec *codec, const struct coef_fw *fw);

/*
 * GPIO helpers
 */
void alc_setup_gpio(struct hda_codec *codec, unsigned int mask);
void alc_write_gpio_data(struct hda_codec *codec);
void alc_update_gpio_data(struct hda_codec *codec, unsigned int mask,
			  bool on);
void alc_write_gpio(struct hda_codec *codec);

/* common GPIO fixups */
void alc_fixup_gpio(struct hda_codec *codec, int action, unsigned int mask);
void alc_fixup_gpio1(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action);
void alc_fixup_gpio2(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action);
void alc_fixup_gpio3(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action);
void alc_fixup_gpio4(struct hda_codec *codec,
		     const struct hda_fixup *fix, int action);
void alc_fixup_micmute_led(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action);

/*
 * Common init code, callbacks and helpers
 */
void alc_fix_pll(struct hda_codec *codec);
void alc_fix_pll_init(struct hda_codec *codec, hda_nid_t nid,
		      unsigned int coef_idx, unsigned int coef_bit);
void alc_fill_eapd_coef(struct hda_codec *codec);
void alc_auto_setup_eapd(struct hda_codec *codec, bool on);

int alc_find_ext_mic_pin(struct hda_codec *codec);
void alc_headset_mic_no_shutup(struct hda_codec *codec);
void alc_shutup_pins(struct hda_codec *codec);
void alc_eapd_shutup(struct hda_codec *codec);
void alc_auto_init_amp(struct hda_codec *codec, int type);
hda_nid_t alc_get_hp_pin(struct alc_spec *spec);
int alc_auto_parse_customize_define(struct hda_codec *codec);
int alc_subsystem_id(struct hda_codec *codec, const hda_nid_t *ports);
void alc_ssid_check(struct hda_codec *codec, const hda_nid_t *ports);
int alc_build_controls(struct hda_codec *codec);
void alc_update_knob_master(struct hda_codec *codec,
			    struct hda_jack_callback *jack);

static inline void alc_pre_init(struct hda_codec *codec)
{
	alc_fill_eapd_coef(codec);
}

#define is_s3_resume(codec) \
	((codec)->core.dev.power.power_state.event == PM_EVENT_RESUME)
#define is_s4_resume(codec) \
	((codec)->core.dev.power.power_state.event == PM_EVENT_RESTORE)
#define is_s4_suspend(codec) \
	((codec)->core.dev.power.power_state.event == PM_EVENT_FREEZE)

int alc_init(struct hda_codec *codec);
void alc_shutup(struct hda_codec *codec);
void alc_power_eapd(struct hda_codec *codec);
int alc_suspend(struct hda_codec *codec);
int alc_resume(struct hda_codec *codec);

int alc_parse_auto_config(struct hda_codec *codec,
			  const hda_nid_t *ignore_nids,
			  const hda_nid_t *ssid_nids);
int alc_alloc_spec(struct hda_codec *codec, hda_nid_t mixer_nid);

#define alc_codec_rename(codec, name) snd_hda_codec_set_name(codec, name)

#ifdef CONFIG_SND_HDA_INPUT_BEEP
int alc_set_beep_amp(struct alc_spec *spec, hda_nid_t nid, int idx, int dir);
int alc_has_cdefine_beep(struct hda_codec *codec);
#define set_beep_amp		alc_set_beep_amp
#define has_cdefine_beep	alc_has_cdefine_beep
#else
#define set_beep_amp(spec, nid, idx, dir)	0
#define has_cdefine_beep(codec)		0
#endif

static inline void rename_ctl(struct hda_codec *codec, const char *oldname,
			      const char *newname)
{
	struct snd_kcontrol *kctl;

	kctl = snd_hda_find_mixer_ctl(codec, oldname);
	if (kctl)
		snd_ctl_rename(codec->card, kctl, newname);
}

/* Common fixups */
void alc_fixup_sku_ignore(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action);
void alc_fixup_no_depop_delay(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action);
void alc_fixup_inv_dmic(struct hda_codec *codec,
			const struct hda_fixup *fix, int action);
void alc_fixup_dual_codecs(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action);
void alc_fixup_bass_chmap(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action);
void alc_fixup_headset_mode(struct hda_codec *codec,
			    const struct hda_fixup *fix, int action);
void alc_fixup_headset_mode_no_hp_mic(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action);
void alc_fixup_headset_mic(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action);
void alc_update_headset_jack_cb(struct hda_codec *codec,
				struct hda_jack_callback *jack);
void alc_update_gpio_led(struct hda_codec *codec, unsigned int mask,
			 int polarity, bool enabled);
void alc_fixup_hp_gpio_led(struct hda_codec *codec,
			   int action,
			   unsigned int mute_mask,
			   unsigned int micmute_mask);
void alc_fixup_no_jack_detect(struct hda_codec *codec,
			      const struct hda_fixup *fix, int action);
void alc_fixup_disable_aamix(struct hda_codec *codec,
			     const struct hda_fixup *fix, int action);
void alc_fixup_auto_mute_via_amp(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action);

/* device-specific, but used by multiple codec drivers */
void alc1220_fixup_gb_dual_codecs(struct hda_codec *codec,
				  const struct hda_fixup *fix,
				  int action);
void alc233_alc662_fixup_lenovo_dual_codecs(struct hda_codec *codec,
					    const struct hda_fixup *fix,
					    int action);
void alc_fixup_dell_xps13(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action);

/*
 * COEF access helper functions
 */
static inline void coef_mutex_lock(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_power_up_pm(codec);
	mutex_lock(&spec->coef_mutex);
}

static inline void coef_mutex_unlock(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	mutex_unlock(&spec->coef_mutex);
	snd_hda_power_down_pm(codec);
}

DEFINE_GUARD(coef_mutex, struct hda_codec *, coef_mutex_lock(_T), coef_mutex_unlock(_T))

#endif /* __HDA_REALTEK_H */
