// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intel HDMI codec support
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hdmi_local.h"

static bool enable_silent_stream =
IS_ENABLED(CONFIG_SND_HDA_INTEL_HDMI_SILENT_STREAM);
module_param(enable_silent_stream, bool, 0644);
MODULE_PARM_DESC(enable_silent_stream, "Enable Silent Stream for HDMI devices");

enum {
	MODEL_HSW,
	MODEL_GLK,
	MODEL_ICL,
	MODEL_TGL,
	MODEL_ADLP,
	MODEL_BYT,
	MODEL_CPT,
};

#define INTEL_GET_VENDOR_VERB	0xf81
#define INTEL_SET_VENDOR_VERB	0x781
#define INTEL_EN_DP12		0x02	/* enable DP 1.2 features */
#define INTEL_EN_ALL_PIN_CVTS	0x01	/* enable 2nd & 3rd pins and convertors */

static void intel_haswell_enable_all_pins(struct hda_codec *codec,
					  bool update_tree)
{
	unsigned int vendor_param;
	struct hdmi_spec *spec = codec->spec;

	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_ALL_PIN_CVTS)
		return;

	vendor_param |= INTEL_EN_ALL_PIN_CVTS;
	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
	if (vendor_param == -1)
		return;

	if (update_tree)
		snd_hda_codec_update_widgets(codec);
}

static void intel_haswell_fixup_enable_dp12(struct hda_codec *codec)
{
	unsigned int vendor_param;
	struct hdmi_spec *spec = codec->spec;

	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_DP12)
		return;

	/* enable DP1.2 mode */
	vendor_param |= INTEL_EN_DP12;
	snd_hdac_regmap_add_vendor_verb(&codec->core, INTEL_SET_VENDOR_VERB);
	snd_hda_codec_write_cache(codec, spec->vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
}

/* Haswell needs to re-issue the vendor-specific verbs before turning to D0.
 * Otherwise you may get severe h/w communication errors.
 */
static void haswell_set_power_state(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state)
{
	/* check codec->spec: it can be called before the probe gets called */
	if (codec->spec) {
		if (power_state == AC_PWRST_D0) {
			intel_haswell_enable_all_pins(codec, false);
			intel_haswell_fixup_enable_dp12(codec);
		}
	}

	snd_hda_codec_read(codec, fg, 0, AC_VERB_SET_POWER_STATE, power_state);
	snd_hda_codec_set_power_to_all(codec, fg, power_state);
}

/* There is a fixed mapping between audio pin node and display port.
 * on SNB, IVY, HSW, BSW, SKL, BXT, KBL:
 * Pin Widget 5 - PORT B (port = 1 in i915 driver)
 * Pin Widget 6 - PORT C (port = 2 in i915 driver)
 * Pin Widget 7 - PORT D (port = 3 in i915 driver)
 *
 * on VLV, ILK:
 * Pin Widget 4 - PORT B (port = 1 in i915 driver)
 * Pin Widget 5 - PORT C (port = 2 in i915 driver)
 * Pin Widget 6 - PORT D (port = 3 in i915 driver)
 */
static int intel_base_nid(struct hda_codec *codec)
{
	switch (codec->core.vendor_id) {
	case 0x80860054: /* ILK */
	case 0x80862804: /* ILK */
	case 0x80862882: /* VLV */
		return 4;
	default:
		return 5;
	}
}

static int intel_pin2port(void *audio_ptr, int pin_nid)
{
	struct hda_codec *codec = audio_ptr;
	struct hdmi_spec *spec = codec->spec;
	int base_nid, i;

	if (!spec->port_num) {
		base_nid = intel_base_nid(codec);
		if (WARN_ON(pin_nid < base_nid || pin_nid >= base_nid + 3))
			return -1;
		return pin_nid - base_nid + 1;
	}

	/*
	 * looking for the pin number in the mapping table and return
	 * the index which indicate the port number
	 */
	for (i = 0; i < spec->port_num; i++) {
		if (pin_nid == spec->port_map[i])
			return i;
	}

	codec_info(codec, "Can't find the HDMI/DP port for pin NID 0x%x\n", pin_nid);
	return -1;
}

static int intel_port2pin(struct hda_codec *codec, int port)
{
	struct hdmi_spec *spec = codec->spec;

	if (!spec->port_num) {
		/* we assume only from port-B to port-D */
		if (port < 1 || port > 3)
			return 0;
		return port + intel_base_nid(codec) - 1;
	}

	if (port < 0 || port >= spec->port_num)
		return 0;
	return spec->port_map[port];
}

static void intel_pin_eld_notify(void *audio_ptr, int port, int pipe)
{
	struct hda_codec *codec = audio_ptr;
	int pin_nid;
	int dev_id = pipe;

	pin_nid = intel_port2pin(codec, port);
	if (!pin_nid)
		return;
	/* skip notification during system suspend (but not in runtime PM);
	 * the state will be updated at resume
	 */
	if (codec->core.dev.power.power_state.event == PM_EVENT_SUSPEND)
		return;

	snd_hdac_i915_set_bclk(&codec->bus->core);
	snd_hda_hdmi_check_presence_and_report(codec, pin_nid, dev_id);
}

static const struct drm_audio_component_audio_ops intel_audio_ops = {
	.pin2port = intel_pin2port,
	.pin_eld_notify = intel_pin_eld_notify,
};

/* register i915 component pin_eld_notify callback */
static void register_i915_notifier(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	spec->use_acomp_notifier = true;
	spec->port2pin = intel_port2pin;
	snd_hda_hdmi_setup_drm_audio_ops(codec, &intel_audio_ops);
	snd_hdac_acomp_register_notifier(&codec->bus->core,
					&spec->drm_audio_ops);
	/* no need for forcible resume for jack check thanks to notifier */
	codec->relaxed_resume = 1;
}

#define I915_SILENT_RATE		48000
#define I915_SILENT_CHANNELS		2
#define I915_SILENT_FORMAT_BITS	16
#define I915_SILENT_FMT_MASK		0xf

static void silent_stream_enable_i915(struct hda_codec *codec,
				      struct hdmi_spec_per_pin *per_pin)
{
	unsigned int format;

	snd_hdac_sync_audio_rate(&codec->core, per_pin->pin_nid,
				 per_pin->dev_id, I915_SILENT_RATE);

	/* trigger silent stream generation in hw */
	format = snd_hdac_stream_format(I915_SILENT_CHANNELS, I915_SILENT_FORMAT_BITS,
					I915_SILENT_RATE);
	snd_hda_codec_setup_stream(codec, per_pin->cvt_nid,
				   I915_SILENT_FMT_MASK, I915_SILENT_FMT_MASK, format);
	usleep_range(100, 200);
	snd_hda_codec_setup_stream(codec, per_pin->cvt_nid, I915_SILENT_FMT_MASK, 0, format);

	per_pin->channels = I915_SILENT_CHANNELS;
	snd_hda_hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);
}

static void silent_stream_set_kae(struct hda_codec *codec,
				  struct hdmi_spec_per_pin *per_pin,
				  bool enable)
{
	unsigned int param;

	codec_dbg(codec, "HDMI: KAE %d cvt-NID=0x%x\n", enable, per_pin->cvt_nid);

	param = snd_hda_codec_read(codec, per_pin->cvt_nid, 0, AC_VERB_GET_DIGI_CONVERT_1, 0);
	param = (param >> 16) & 0xff;

	if (enable)
		param |= AC_DIG3_KAE;
	else
		param &= ~AC_DIG3_KAE;

	snd_hda_codec_write(codec, per_pin->cvt_nid, 0, AC_VERB_SET_DIGI_CONVERT_3, param);
}

static void i915_set_silent_stream(struct hda_codec *codec,
				   struct hdmi_spec_per_pin *per_pin,
				   bool enable)
{
	struct hdmi_spec *spec = codec->spec;

	switch (spec->silent_stream_type) {
	case SILENT_STREAM_KAE:
		if (enable) {
			silent_stream_enable_i915(codec, per_pin);
			silent_stream_set_kae(codec, per_pin, true);
		} else {
			silent_stream_set_kae(codec, per_pin, false);
		}
		break;
	case SILENT_STREAM_I915:
		if (enable) {
			silent_stream_enable_i915(codec, per_pin);
			snd_hda_power_up_pm(codec);
		} else {
			/* release ref taken in silent_stream_enable() */
			snd_hda_power_down_pm(codec);
		}
		break;
	default:
		break;
	}
}

static void haswell_verify_D0(struct hda_codec *codec,
			      hda_nid_t cvt_nid, hda_nid_t nid)
{
	int pwr;

	/* For Haswell, the converter 1/2 may keep in D3 state after bootup,
	 * thus pins could only choose converter 0 for use. Make sure the
	 * converters are in correct power state
	 */
	if (!snd_hda_check_power_state(codec, cvt_nid, AC_PWRST_D0))
		snd_hda_codec_write(codec, cvt_nid, 0, AC_VERB_SET_POWER_STATE, AC_PWRST_D0);

	if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D0)) {
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE,
				    AC_PWRST_D0);
		msleep(40);
		pwr = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_POWER_STATE, 0);
		pwr = (pwr & AC_PWRST_ACTUAL) >> AC_PWRST_ACTUAL_SHIFT;
		codec_dbg(codec, "Haswell HDMI audio: Power for NID 0x%x is now D%d\n", nid, pwr);
	}
}

/* Assure the pin select the right convetor */
static void intel_verify_pin_cvt_connect(struct hda_codec *codec,
			struct hdmi_spec_per_pin *per_pin)
{
	hda_nid_t pin_nid = per_pin->pin_nid;
	int mux_idx, curr;

	mux_idx = per_pin->mux_idx;
	curr = snd_hda_codec_read(codec, pin_nid, 0,
					  AC_VERB_GET_CONNECT_SEL, 0);
	if (curr != mux_idx)
		snd_hda_codec_write_cache(codec, pin_nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    mux_idx);
}

/* get the mux index for the converter of the pins
 * converter's mux index is the same for all pins on Intel platform
 */
static int intel_cvt_id_to_mux_idx(struct hdmi_spec *spec,
			hda_nid_t cvt_nid)
{
	int i;

	for (i = 0; i < spec->num_cvts; i++)
		if (spec->cvt_nids[i] == cvt_nid)
			return i;
	return -EINVAL;
}

/* Intel HDMI workaround to fix audio routing issue:
 * For some Intel display codecs, pins share the same connection list.
 * So a conveter can be selected by multiple pins and playback on any of these
 * pins will generate sound on the external display, because audio flows from
 * the same converter to the display pipeline. Also muting one pin may make
 * other pins have no sound output.
 * So this function assures that an assigned converter for a pin is not selected
 * by any other pins.
 */
static void intel_not_share_assigned_cvt(struct hda_codec *codec,
					 hda_nid_t pin_nid,
					 int dev_id, int mux_idx)
{
	struct hdmi_spec *spec = codec->spec;
	hda_nid_t nid;
	int cvt_idx, curr;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;
	int pin_idx;

	/* configure the pins connections */
	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		int dev_id_saved;
		int dev_num;

		per_pin = get_pin(spec, pin_idx);
		/*
		 * pin not connected to monitor
		 * no need to operate on it
		 */
		if (!per_pin->pcm)
			continue;

		if ((per_pin->pin_nid == pin_nid) &&
			(per_pin->dev_id == dev_id))
			continue;

		/*
		 * if per_pin->dev_id >= dev_num,
		 * snd_hda_get_dev_select() will fail,
		 * and the following operation is unpredictable.
		 * So skip this situation.
		 */
		dev_num = snd_hda_get_num_devices(codec, per_pin->pin_nid) + 1;
		if (per_pin->dev_id >= dev_num)
			continue;

		nid = per_pin->pin_nid;

		/*
		 * Calling this function should not impact
		 * on the device entry selection
		 * So let's save the dev id for each pin,
		 * and restore it when return
		 */
		dev_id_saved = snd_hda_get_dev_select(codec, nid);
		snd_hda_set_dev_select(codec, nid, per_pin->dev_id);
		curr = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_CONNECT_SEL, 0);
		if (curr != mux_idx) {
			snd_hda_set_dev_select(codec, nid, dev_id_saved);
			continue;
		}


		/* choose an unassigned converter. The conveters in the
		 * connection list are in the same order as in the codec.
		 */
		for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
			per_cvt = get_cvt(spec, cvt_idx);
			if (!per_cvt->assigned) {
				codec_dbg(codec,
					  "choose cvt %d for pin NID 0x%x\n",
					  cvt_idx, nid);
				snd_hda_codec_write_cache(codec, nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    cvt_idx);
				break;
			}
		}
		snd_hda_set_dev_select(codec, nid, dev_id_saved);
	}
}

/* A wrapper of intel_not_share_asigned_cvt() */
static void intel_not_share_assigned_cvt_nid(struct hda_codec *codec,
			hda_nid_t pin_nid, int dev_id, hda_nid_t cvt_nid)
{
	int mux_idx;
	struct hdmi_spec *spec = codec->spec;

	/* On Intel platform, the mapping of converter nid to
	 * mux index of the pins are always the same.
	 * The pin nid may be 0, this means all pins will not
	 * share the converter.
	 */
	mux_idx = intel_cvt_id_to_mux_idx(spec, cvt_nid);
	if (mux_idx >= 0)
		intel_not_share_assigned_cvt(codec, pin_nid, dev_id, mux_idx);
}

/* setup_stream ops override for HSW+ */
static int i915_hsw_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
				 hda_nid_t pin_nid, int dev_id, u32 stream_tag,
				 int format)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = pin_id_to_pin_index(codec, pin_nid, dev_id);
	struct hdmi_spec_per_pin *per_pin;
	int res;

	if (pin_idx < 0)
		per_pin = NULL;
	else
		per_pin = get_pin(spec, pin_idx);

	haswell_verify_D0(codec, cvt_nid, pin_nid);

	if (spec->silent_stream_type == SILENT_STREAM_KAE && per_pin && per_pin->silent_stream) {
		silent_stream_set_kae(codec, per_pin, false);
		/* wait for pending transfers in codec to clear */
		usleep_range(100, 200);
	}

	res = snd_hda_hdmi_setup_stream(codec, cvt_nid, pin_nid, dev_id,
					stream_tag, format);

	if (spec->silent_stream_type == SILENT_STREAM_KAE && per_pin && per_pin->silent_stream) {
		usleep_range(100, 200);
		silent_stream_set_kae(codec, per_pin, true);
	}

	return res;
}

/* pin_cvt_fixup ops override for HSW+ and VLV+ */
static void i915_pin_cvt_fixup(struct hda_codec *codec,
			       struct hdmi_spec_per_pin *per_pin,
			       hda_nid_t cvt_nid)
{
	if (per_pin) {
		haswell_verify_D0(codec, per_pin->cvt_nid, per_pin->pin_nid);
		snd_hda_set_dev_select(codec, per_pin->pin_nid,
			       per_pin->dev_id);
		intel_verify_pin_cvt_connect(codec, per_pin);
		intel_not_share_assigned_cvt(codec, per_pin->pin_nid,
				     per_pin->dev_id, per_pin->mux_idx);
	} else {
		intel_not_share_assigned_cvt_nid(codec, 0, 0, cvt_nid);
	}
}

static int i915_hdmi_suspend(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	bool silent_streams = false;
	int pin_idx, res;

	res = snd_hda_hdmi_generic_suspend(codec);
	if (spec->silent_stream_type != SILENT_STREAM_KAE)
		return res;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);

		if (per_pin->silent_stream) {
			silent_streams = true;
			break;
		}
	}

	if (silent_streams) {
		/*
		 * stream-id should remain programmed when codec goes
		 * to runtime suspend
		 */
		codec->no_stream_clean_at_suspend = 1;

		/*
		 * the system might go to S3, in which case keep-alive
		 * must be reprogrammed upon resume
		 */
		codec->forced_resume = 1;

		codec_dbg(codec, "HDMI: KAE active at suspend\n");
	} else {
		codec->no_stream_clean_at_suspend = 0;
		codec->forced_resume = 0;
	}

	return res;
}

static int i915_hdmi_resume(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx, res;

	res = snd_hda_hdmi_generic_resume(codec);
	if (spec->silent_stream_type != SILENT_STREAM_KAE)
		return res;

	/* KAE not programmed at suspend, nothing to do here */
	if (!codec->no_stream_clean_at_suspend)
		return res;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);

		/*
		 * If system was in suspend with monitor connected,
		 * the codec setting may have been lost. Re-enable
		 * keep-alive.
		 */
		if (per_pin->silent_stream) {
			unsigned int param;

			param = snd_hda_codec_read(codec, per_pin->cvt_nid, 0,
						   AC_VERB_GET_CONV, 0);
			if (!param) {
				codec_dbg(codec, "HDMI: KAE: restore stream id\n");
				silent_stream_enable_i915(codec, per_pin);
			}

			param = snd_hda_codec_read(codec, per_pin->cvt_nid, 0,
						   AC_VERB_GET_DIGI_CONVERT_1, 0);
			if (!(param & (AC_DIG3_KAE << 16))) {
				codec_dbg(codec, "HDMI: KAE: restore DIG3_KAE\n");
				silent_stream_set_kae(codec, per_pin, true);
			}
		}
	}

	return res;
}

/* precondition and allocation for Intel codecs */
static int alloc_intel_hdmi(struct hda_codec *codec)
{
	/* requires i915 binding */
	if (!codec->bus->core.audio_component) {
		codec_info(codec, "No i915 binding for Intel HDMI/DP codec\n");
		/* set probe_id here to prevent generic fallback binding */
		codec->probe_id = HDA_CODEC_ID_SKIP_PROBE;
		return -ENODEV;
	}

	return snd_hda_hdmi_generic_alloc(codec);
}

/* parse and post-process for Intel codecs */
static int parse_intel_hdmi(struct hda_codec *codec)
{
	int err, retries = 3;

	do {
		err = snd_hda_hdmi_parse_codec(codec);
	} while (err < 0 && retries--);

	if (err < 0)
		return err;

	snd_hda_hdmi_generic_init_per_pins(codec);
	register_i915_notifier(codec);
	return 0;
}

/* Intel Haswell and onwards; audio component with eld notifier */
static int intel_hsw_common_init(struct hda_codec *codec, hda_nid_t vendor_nid,
				 const int *port_map, int port_num, int dev_num,
				 bool send_silent_stream)
{
	struct hdmi_spec *spec;

	spec = codec->spec;
	codec->dp_mst = true;
	spec->vendor_nid = vendor_nid;
	spec->port_map = port_map;
	spec->port_num = port_num;
	spec->intel_hsw_fixup = true;
	spec->dev_num = dev_num;

	intel_haswell_enable_all_pins(codec, true);
	intel_haswell_fixup_enable_dp12(codec);

	codec->display_power_control = 1;

	codec->depop_delay = 0;
	codec->auto_runtime_pm = 1;

	spec->ops.setup_stream = i915_hsw_setup_stream;
	spec->ops.pin_cvt_fixup = i915_pin_cvt_fixup;
	spec->ops.silent_stream = i915_set_silent_stream;

	/*
	 * Enable silent stream feature, if it is enabled via
	 * module param or Kconfig option
	 */
	if (send_silent_stream)
		spec->silent_stream_type = SILENT_STREAM_I915;

	return parse_intel_hdmi(codec);
}

static int probe_i915_hsw_hdmi(struct hda_codec *codec)
{
	return intel_hsw_common_init(codec, 0x08, NULL, 0, 3,
				     enable_silent_stream);
}

static int probe_i915_glk_hdmi(struct hda_codec *codec)
{
	/*
	 * Silent stream calls audio component .get_power() from
	 * .pin_eld_notify(). On GLK this will deadlock in i915 due
	 * to the audio vs. CDCLK workaround.
	 */
	return intel_hsw_common_init(codec, 0x0b, NULL, 0, 3, false);
}

static int probe_i915_icl_hdmi(struct hda_codec *codec)
{
	/*
	 * pin to port mapping table where the value indicate the pin number and
	 * the index indicate the port number.
	 */
	static const int map[] = {0x0, 0x4, 0x6, 0x8, 0xa, 0xb};

	return intel_hsw_common_init(codec, 0x02, map, ARRAY_SIZE(map), 3,
				     enable_silent_stream);
}

static int probe_i915_tgl_hdmi(struct hda_codec *codec)
{
	/*
	 * pin to port mapping table where the value indicate the pin number and
	 * the index indicate the port number.
	 */
	static const int map[] = {0x4, 0x6, 0x8, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

	return intel_hsw_common_init(codec, 0x02, map, ARRAY_SIZE(map), 4,
				     enable_silent_stream);
}

static int probe_i915_adlp_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int res;

	res = probe_i915_tgl_hdmi(codec);
	if (!res) {
		spec = codec->spec;

		if (spec->silent_stream_type)
			spec->silent_stream_type = SILENT_STREAM_KAE;
	}

	return res;
}

/* Intel Baytrail and Braswell; with eld notifier */
static int probe_i915_byt_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;

	spec = codec->spec;

	/* For Valleyview/Cherryview, only the display codec is in the display
	 * power well and can use link_power ops to request/release the power.
	 */
	codec->display_power_control = 1;

	codec->depop_delay = 0;
	codec->auto_runtime_pm = 1;

	spec->ops.pin_cvt_fixup = i915_pin_cvt_fixup;

	return parse_intel_hdmi(codec);
}

/* Intel IronLake, SandyBridge and IvyBridge; with eld notifier */
static int probe_i915_cpt_hdmi(struct hda_codec *codec)
{
	return parse_intel_hdmi(codec);
}

/*
 * common driver probe
 */
static int intelhdmi_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	int err;

	err = alloc_intel_hdmi(codec);
	if (err < 0)
		return err;

	switch (id->driver_data) {
	case MODEL_HSW:
		err = probe_i915_hsw_hdmi(codec);
		break;
	case MODEL_GLK:
		err = probe_i915_glk_hdmi(codec);
		break;
	case MODEL_ICL:
		err = probe_i915_icl_hdmi(codec);
		break;
	case MODEL_TGL:
		err = probe_i915_tgl_hdmi(codec);
		break;
	case MODEL_ADLP:
		err = probe_i915_adlp_hdmi(codec);
		break;
	case MODEL_BYT:
		err = probe_i915_byt_hdmi(codec);
		break;
	case MODEL_CPT:
		err = probe_i915_cpt_hdmi(codec);
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err < 0) {
		snd_hda_hdmi_generic_spec_free(codec);
		return err;
	}

	return 0;
}

static const struct hda_codec_ops intelhdmi_codec_ops = {
	.probe = intelhdmi_probe,
	.remove = snd_hda_hdmi_generic_remove,
	.init = snd_hda_hdmi_generic_init,
	.build_pcms = snd_hda_hdmi_generic_build_pcms,
	.build_controls = snd_hda_hdmi_generic_build_controls,
	.unsol_event = snd_hda_hdmi_generic_unsol_event,
	.suspend = i915_hdmi_suspend,
	.resume = i915_hdmi_resume,
	.set_power_state = haswell_set_power_state,
};

/*
 * driver entries
 */
static const struct hda_device_id snd_hda_id_intelhdmi[] = {
	HDA_CODEC_ID_MODEL(0x80860054, "IbexPeak HDMI",		MODEL_CPT),
	HDA_CODEC_ID_MODEL(0x80862800, "Geminilake HDMI",	MODEL_GLK),
	HDA_CODEC_ID_MODEL(0x80862804, "IbexPeak HDMI",		MODEL_CPT),
	HDA_CODEC_ID_MODEL(0x80862805, "CougarPoint HDMI",	MODEL_CPT),
	HDA_CODEC_ID_MODEL(0x80862806, "PantherPoint HDMI",	MODEL_CPT),
	HDA_CODEC_ID_MODEL(0x80862807, "Haswell HDMI",		MODEL_HSW),
	HDA_CODEC_ID_MODEL(0x80862808, "Broadwell HDMI",	MODEL_HSW),
	HDA_CODEC_ID_MODEL(0x80862809, "Skylake HDMI",		MODEL_HSW),
	HDA_CODEC_ID_MODEL(0x8086280a, "Broxton HDMI",		MODEL_HSW),
	HDA_CODEC_ID_MODEL(0x8086280b, "Kabylake HDMI",		MODEL_HSW),
	HDA_CODEC_ID_MODEL(0x8086280c, "Cannonlake HDMI",	MODEL_GLK),
	HDA_CODEC_ID_MODEL(0x8086280d, "Geminilake HDMI",	MODEL_GLK),
	HDA_CODEC_ID_MODEL(0x8086280f, "Icelake HDMI",		MODEL_ICL),
	HDA_CODEC_ID_MODEL(0x80862812, "Tigerlake HDMI",	MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x80862814, "DG1 HDMI",		MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x80862815, "Alderlake HDMI",	MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x80862816, "Rocketlake HDMI",	MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x80862818, "Raptorlake HDMI",	MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x80862819, "DG2 HDMI",		MODEL_TGL),
	HDA_CODEC_ID_MODEL(0x8086281a, "Jasperlake HDMI",	MODEL_ICL),
	HDA_CODEC_ID_MODEL(0x8086281b, "Elkhartlake HDMI",	MODEL_ICL),
	HDA_CODEC_ID_MODEL(0x8086281c, "Alderlake-P HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x8086281d, "Meteor Lake HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x8086281e, "Battlemage HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x8086281f, "Raptor Lake P HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x80862820, "Lunar Lake HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x80862822, "Panther Lake HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x80862823, "Wildcat Lake HDMI",	MODEL_ADLP),
	HDA_CODEC_ID_MODEL(0x80862882, "Valleyview2 HDMI",	MODEL_BYT),
	HDA_CODEC_ID_MODEL(0x80862883, "Braswell HDMI",		MODEL_BYT),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_intelhdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel HDMI HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_HDMI");

static struct hda_codec_driver intelhdmi_driver = {
	.id = snd_hda_id_intelhdmi,
	.ops = &intelhdmi_codec_ops,
};

module_hda_codec_driver(intelhdmi_driver);
