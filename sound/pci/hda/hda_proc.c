/*
 * Universal Interface for Intel High Definition Audio Codec
 * 
 * Generic proc interface
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

static const char *get_wid_type_name(unsigned int wid_value)
{
	static char *names[16] = {
		[AC_WID_AUD_OUT] = "Audio Output",
		[AC_WID_AUD_IN] = "Audio Input",
		[AC_WID_AUD_MIX] = "Audio Mixer",
		[AC_WID_AUD_SEL] = "Audio Selector",
		[AC_WID_PIN] = "Pin Complex",
		[AC_WID_POWER] = "Power Widget",
		[AC_WID_VOL_KNB] = "Volume Knob Widget",
		[AC_WID_BEEP] = "Beep Generator Widget",
		[AC_WID_VENDOR] = "Vendor Defined Widget",
	};
	wid_value &= 0xf;
	if (names[wid_value])
		return names[wid_value];
	else
		return "UNKNOWN Widget";
}

static void print_amp_caps(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid, int dir)
{
	unsigned int caps;
	caps = snd_hda_param_read(codec, nid,
				  dir == HDA_OUTPUT ?
				    AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP);
	if (caps == -1 || caps == 0) {
		snd_iprintf(buffer, "N/A\n");
		return;
	}
	snd_iprintf(buffer, "ofs=0x%02x, nsteps=0x%02x, stepsize=0x%02x, "
		    "mute=%x\n",
		    caps & AC_AMPCAP_OFFSET,
		    (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT,
		    (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT,
		    (caps & AC_AMPCAP_MUTE) >> AC_AMPCAP_MUTE_SHIFT);
}

static void print_amp_vals(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid,
			   int dir, int stereo, int indices)
{
	unsigned int val;
	int i;

	dir = dir == HDA_OUTPUT ? AC_AMP_GET_OUTPUT : AC_AMP_GET_INPUT;
	for (i = 0; i < indices; i++) {
		snd_iprintf(buffer, " [");
		if (stereo) {
			val = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_AMP_GAIN_MUTE,
						 AC_AMP_GET_LEFT | dir | i);
			snd_iprintf(buffer, "0x%02x ", val);
		}
		val = snd_hda_codec_read(codec, nid, 0,
					 AC_VERB_GET_AMP_GAIN_MUTE,
					 AC_AMP_GET_RIGHT | dir | i);
		snd_iprintf(buffer, "0x%02x]", val);
	}
	snd_iprintf(buffer, "\n");
}

static void print_pcm_rates(struct snd_info_buffer *buffer, unsigned int pcm)
{
	char buf[SND_PRINT_RATES_ADVISED_BUFSIZE];

	pcm &= AC_SUPPCM_RATES;
	snd_iprintf(buffer, "    rates [0x%x]:", pcm);
	snd_print_pcm_rates(pcm, buf, sizeof(buf));
	snd_iprintf(buffer, "%s\n", buf);
}

static void print_pcm_bits(struct snd_info_buffer *buffer, unsigned int pcm)
{
	char buf[SND_PRINT_BITS_ADVISED_BUFSIZE];

	snd_iprintf(buffer, "    bits [0x%x]:", (pcm >> 16) & 0xff);
	snd_print_pcm_bits(pcm, buf, sizeof(buf));
	snd_iprintf(buffer, "%s\n", buf);
}

static void print_pcm_formats(struct snd_info_buffer *buffer,
			      unsigned int streams)
{
	snd_iprintf(buffer, "    formats [0x%x]:", streams & 0xf);
	if (streams & AC_SUPFMT_PCM)
		snd_iprintf(buffer, " PCM");
	if (streams & AC_SUPFMT_FLOAT32)
		snd_iprintf(buffer, " FLOAT");
	if (streams & AC_SUPFMT_AC3)
		snd_iprintf(buffer, " AC3");
	snd_iprintf(buffer, "\n");
}

static void print_pcm_caps(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pcm = snd_hda_param_read(codec, nid, AC_PAR_PCM);
	unsigned int stream = snd_hda_param_read(codec, nid, AC_PAR_STREAM);
	if (pcm == -1 || stream == -1) {
		snd_iprintf(buffer, "N/A\n");
		return;
	}
	print_pcm_rates(buffer, pcm);
	print_pcm_bits(buffer, pcm);
	print_pcm_formats(buffer, stream);
}

static const char *get_jack_connection(u32 cfg)
{
	static char *names[16] = {
		"Unknown", "1/8", "1/4", "ATAPI",
		"RCA", "Optical","Digital", "Analog",
		"DIN", "XLR", "RJ11", "Comb",
		NULL, NULL, NULL, "Other"
	};
	cfg = (cfg & AC_DEFCFG_CONN_TYPE) >> AC_DEFCFG_CONN_TYPE_SHIFT;
	if (names[cfg])
		return names[cfg];
	else
		return "UNKNOWN";
}

static const char *get_jack_color(u32 cfg)
{
	static char *names[16] = {
		"Unknown", "Black", "Grey", "Blue",
		"Green", "Red", "Orange", "Yellow",
		"Purple", "Pink", NULL, NULL,
		NULL, NULL, "White", "Other",
	};
	cfg = (cfg & AC_DEFCFG_COLOR) >> AC_DEFCFG_COLOR_SHIFT;
	if (names[cfg])
		return names[cfg];
	else
		return "UNKNOWN";
}

static void print_pin_caps(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid,
			   int *supports_vref)
{
	static char *jack_conns[4] = { "Jack", "N/A", "Fixed", "Both" };
	unsigned int caps, val;

	caps = snd_hda_param_read(codec, nid, AC_PAR_PIN_CAP);
	snd_iprintf(buffer, "  Pincap 0x%08x:", caps);
	if (caps & AC_PINCAP_IN)
		snd_iprintf(buffer, " IN");
	if (caps & AC_PINCAP_OUT)
		snd_iprintf(buffer, " OUT");
	if (caps & AC_PINCAP_HP_DRV)
		snd_iprintf(buffer, " HP");
	if (caps & AC_PINCAP_EAPD)
		snd_iprintf(buffer, " EAPD");
	if (caps & AC_PINCAP_PRES_DETECT)
		snd_iprintf(buffer, " Detect");
	if (caps & AC_PINCAP_BALANCE)
		snd_iprintf(buffer, " Balanced");
	if (caps & AC_PINCAP_HDMI) {
		/* Realtek uses this bit as a different meaning */
		if ((codec->vendor_id >> 16) == 0x10ec)
			snd_iprintf(buffer, " R/L");
		else
			snd_iprintf(buffer, " HDMI");
	}
	if (caps & AC_PINCAP_TRIG_REQ)
		snd_iprintf(buffer, " Trigger");
	if (caps & AC_PINCAP_IMP_SENSE)
		snd_iprintf(buffer, " ImpSense");
	snd_iprintf(buffer, "\n");
	if (caps & AC_PINCAP_VREF) {
		unsigned int vref =
			(caps & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
		snd_iprintf(buffer, "    Vref caps:");
		if (vref & AC_PINCAP_VREF_HIZ)
			snd_iprintf(buffer, " HIZ");
		if (vref & AC_PINCAP_VREF_50)
			snd_iprintf(buffer, " 50");
		if (vref & AC_PINCAP_VREF_GRD)
			snd_iprintf(buffer, " GRD");
		if (vref & AC_PINCAP_VREF_80)
			snd_iprintf(buffer, " 80");
		if (vref & AC_PINCAP_VREF_100)
			snd_iprintf(buffer, " 100");
		snd_iprintf(buffer, "\n");
		*supports_vref = 1;
	} else
		*supports_vref = 0;
	if (caps & AC_PINCAP_EAPD) {
		val = snd_hda_codec_read(codec, nid, 0,
					 AC_VERB_GET_EAPD_BTLENABLE, 0);
		snd_iprintf(buffer, "  EAPD 0x%x:", val);
		if (val & AC_EAPDBTL_BALANCED)
			snd_iprintf(buffer, " BALANCED");
		if (val & AC_EAPDBTL_EAPD)
			snd_iprintf(buffer, " EAPD");
		if (val & AC_EAPDBTL_LR_SWAP)
			snd_iprintf(buffer, " R/L");
		snd_iprintf(buffer, "\n");
	}
	caps = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
	snd_iprintf(buffer, "  Pin Default 0x%08x: [%s] %s at %s %s\n", caps,
		    jack_conns[(caps & AC_DEFCFG_PORT_CONN) >> AC_DEFCFG_PORT_CONN_SHIFT],
		    snd_hda_get_jack_type(caps),
		    snd_hda_get_jack_connectivity(caps),
		    snd_hda_get_jack_location(caps));
	snd_iprintf(buffer, "    Conn = %s, Color = %s\n",
		    get_jack_connection(caps),
		    get_jack_color(caps));
	/* Default association and sequence values refer to default grouping
	 * of pin complexes and their sequence within the group. This is used
	 * for priority and resource allocation.
	 */
	snd_iprintf(buffer, "    DefAssociation = 0x%x, Sequence = 0x%x\n",
		    (caps & AC_DEFCFG_DEF_ASSOC) >> AC_DEFCFG_ASSOC_SHIFT,
		    caps & AC_DEFCFG_SEQUENCE);
	if (((caps & AC_DEFCFG_MISC) >> AC_DEFCFG_MISC_SHIFT) &
	    AC_DEFCFG_MISC_NO_PRESENCE) {
		/* Miscellaneous bit indicates external hardware does not
		 * support presence detection even if the pin complex
		 * indicates it is supported.
		 */
		snd_iprintf(buffer, "    Misc = NO_PRESENCE\n");
	}
}

static void print_pin_ctls(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid,
			   int supports_vref)
{
	unsigned int pinctls;

	pinctls = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	snd_iprintf(buffer, "  Pin-ctls: 0x%02x:", pinctls);
	if (pinctls & AC_PINCTL_IN_EN)
		snd_iprintf(buffer, " IN");
	if (pinctls & AC_PINCTL_OUT_EN)
		snd_iprintf(buffer, " OUT");
	if (pinctls & AC_PINCTL_HP_EN)
		snd_iprintf(buffer, " HP");
	if (supports_vref) {
		int vref = pinctls & AC_PINCTL_VREFEN;
		switch (vref) {
		case AC_PINCTL_VREF_HIZ:
			snd_iprintf(buffer, " VREF_HIZ");
			break;
		case AC_PINCTL_VREF_50:
			snd_iprintf(buffer, " VREF_50");
			break;
		case AC_PINCTL_VREF_GRD:
			snd_iprintf(buffer, " VREF_GRD");
			break;
		case AC_PINCTL_VREF_80:
			snd_iprintf(buffer, " VREF_80");
			break;
		case AC_PINCTL_VREF_100:
			snd_iprintf(buffer, " VREF_100");
			break;
		}
	}
	snd_iprintf(buffer, "\n");
}

static void print_vol_knob(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int cap = snd_hda_param_read(codec, nid,
					      AC_PAR_VOL_KNB_CAP);
	snd_iprintf(buffer, "  Volume-Knob: delta=%d, steps=%d, ",
		    (cap >> 7) & 1, cap & 0x7f);
	cap = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	snd_iprintf(buffer, "direct=%d, val=%d\n",
		    (cap >> 7) & 1, cap & 0x7f);
}

static void print_audio_io(struct snd_info_buffer *buffer,
			   struct hda_codec *codec, hda_nid_t nid,
			   unsigned int wid_type)
{
	int conv = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
	snd_iprintf(buffer,
		    "  Converter: stream=%d, channel=%d\n",
		    (conv & AC_CONV_STREAM) >> AC_CONV_STREAM_SHIFT,
		    conv & AC_CONV_CHANNEL);

	if (wid_type == AC_WID_AUD_IN && (conv & AC_CONV_CHANNEL) == 0) {
		int sdi = snd_hda_codec_read(codec, nid, 0,
					     AC_VERB_GET_SDI_SELECT, 0);
		snd_iprintf(buffer, "  SDI-Select: %d\n",
			    sdi & AC_SDI_SELECT);
	}
}

static void print_digital_conv(struct snd_info_buffer *buffer,
			       struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int digi1 = snd_hda_codec_read(codec, nid, 0,
						AC_VERB_GET_DIGI_CONVERT_1, 0);
	snd_iprintf(buffer, "  Digital:");
	if (digi1 & AC_DIG1_ENABLE)
		snd_iprintf(buffer, " Enabled");
	if (digi1 & AC_DIG1_V)
		snd_iprintf(buffer, " Validity");
	if (digi1 & AC_DIG1_VCFG)
		snd_iprintf(buffer, " ValidityCfg");
	if (digi1 & AC_DIG1_EMPHASIS)
		snd_iprintf(buffer, " Preemphasis");
	if (digi1 & AC_DIG1_COPYRIGHT)
		snd_iprintf(buffer, " Copyright");
	if (digi1 & AC_DIG1_NONAUDIO)
		snd_iprintf(buffer, " Non-Audio");
	if (digi1 & AC_DIG1_PROFESSIONAL)
		snd_iprintf(buffer, " Pro");
	if (digi1 & AC_DIG1_LEVEL)
		snd_iprintf(buffer, " GenLevel");
	snd_iprintf(buffer, "\n");
	snd_iprintf(buffer, "  Digital category: 0x%x\n",
		    (digi1 >> 8) & AC_DIG2_CC);
}

static const char *get_pwr_state(u32 state)
{
	static const char *buf[4] = {
		"D0", "D1", "D2", "D3"
	};
	if (state < 4)
		return buf[state];
	return "UNKNOWN";
}

static void print_power_state(struct snd_info_buffer *buffer,
			      struct hda_codec *codec, hda_nid_t nid)
{
	int pwr = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_POWER_STATE, 0);
	snd_iprintf(buffer, "  Power: setting=%s, actual=%s\n",
		    get_pwr_state(pwr & AC_PWRST_SETTING),
		    get_pwr_state((pwr & AC_PWRST_ACTUAL) >>
				  AC_PWRST_ACTUAL_SHIFT));
}

static void print_unsol_cap(struct snd_info_buffer *buffer,
			      struct hda_codec *codec, hda_nid_t nid)
{
	int unsol = snd_hda_codec_read(codec, nid, 0,
				       AC_VERB_GET_UNSOLICITED_RESPONSE, 0);
	snd_iprintf(buffer,
		    "  Unsolicited: tag=%02x, enabled=%d\n",
		    unsol & AC_UNSOL_TAG,
		    (unsol & AC_UNSOL_ENABLED) ? 1 : 0);
}

static void print_proc_caps(struct snd_info_buffer *buffer,
			    struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int proc_caps = snd_hda_param_read(codec, nid,
						    AC_PAR_PROC_CAP);
	snd_iprintf(buffer, "  Processing caps: benign=%d, ncoeff=%d\n",
		    proc_caps & AC_PCAP_BENIGN,
		    (proc_caps & AC_PCAP_NUM_COEF) >> AC_PCAP_NUM_COEF_SHIFT);
}

static void print_conn_list(struct snd_info_buffer *buffer,
			    struct hda_codec *codec, hda_nid_t nid,
			    unsigned int wid_type, hda_nid_t *conn,
			    int conn_len)
{
	int c, curr = -1;

	if (conn_len > 1 &&
	    wid_type != AC_WID_AUD_MIX &&
	    wid_type != AC_WID_VOL_KNB &&
	    wid_type != AC_WID_POWER)
		curr = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_CONNECT_SEL, 0);
	snd_iprintf(buffer, "  Connection: %d\n", conn_len);
	if (conn_len > 0) {
		snd_iprintf(buffer, "    ");
		for (c = 0; c < conn_len; c++) {
			snd_iprintf(buffer, " 0x%02x", conn[c]);
			if (c == curr)
				snd_iprintf(buffer, "*");
		}
		snd_iprintf(buffer, "\n");
	}
}

static void print_gpio(struct snd_info_buffer *buffer,
		       struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int gpio =
		snd_hda_param_read(codec, codec->afg, AC_PAR_GPIO_CAP);
	unsigned int enable, direction, wake, unsol, sticky, data;
	int i, max;
	snd_iprintf(buffer, "GPIO: io=%d, o=%d, i=%d, "
		    "unsolicited=%d, wake=%d\n",
		    gpio & AC_GPIO_IO_COUNT,
		    (gpio & AC_GPIO_O_COUNT) >> AC_GPIO_O_COUNT_SHIFT,
		    (gpio & AC_GPIO_I_COUNT) >> AC_GPIO_I_COUNT_SHIFT,
		    (gpio & AC_GPIO_UNSOLICITED) ? 1 : 0,
		    (gpio & AC_GPIO_WAKE) ? 1 : 0);
	max = gpio & AC_GPIO_IO_COUNT;
	if (!max || max > 8)
		return;
	enable = snd_hda_codec_read(codec, nid, 0,
				    AC_VERB_GET_GPIO_MASK, 0);
	direction = snd_hda_codec_read(codec, nid, 0,
				       AC_VERB_GET_GPIO_DIRECTION, 0);
	wake = snd_hda_codec_read(codec, nid, 0,
				  AC_VERB_GET_GPIO_WAKE_MASK, 0);
	unsol  = snd_hda_codec_read(codec, nid, 0,
				    AC_VERB_GET_GPIO_UNSOLICITED_RSP_MASK, 0);
	sticky = snd_hda_codec_read(codec, nid, 0,
				    AC_VERB_GET_GPIO_STICKY_MASK, 0);
	data = snd_hda_codec_read(codec, nid, 0,
				  AC_VERB_GET_GPIO_DATA, 0);
	for (i = 0; i < max; ++i)
		snd_iprintf(buffer,
			    "  IO[%d]: enable=%d, dir=%d, wake=%d, "
			    "sticky=%d, data=%d, unsol=%d\n", i,
			    (enable & (1<<i)) ? 1 : 0,
			    (direction & (1<<i)) ? 1 : 0,
			    (wake & (1<<i)) ? 1 : 0,
			    (sticky & (1<<i)) ? 1 : 0,
			    (data & (1<<i)) ? 1 : 0,
			    (unsol & (1<<i)) ? 1 : 0);
	/* FIXME: add GPO and GPI pin information */
}

static void print_codec_info(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct hda_codec *codec = entry->private_data;
	hda_nid_t nid;
	int i, nodes;

	snd_iprintf(buffer, "Codec: ");
	if (codec->vendor_name && codec->chip_name)
		snd_iprintf(buffer, "%s %s\n",
			    codec->vendor_name, codec->chip_name);
	else
		snd_iprintf(buffer, "Not Set\n");
	snd_iprintf(buffer, "Address: %d\n", codec->addr);
	snd_iprintf(buffer, "Function Id: 0x%x\n", codec->function_id);
	snd_iprintf(buffer, "Vendor Id: 0x%08x\n", codec->vendor_id);
	snd_iprintf(buffer, "Subsystem Id: 0x%08x\n", codec->subsystem_id);
	snd_iprintf(buffer, "Revision Id: 0x%x\n", codec->revision_id);

	if (codec->mfg)
		snd_iprintf(buffer, "Modem Function Group: 0x%x\n", codec->mfg);
	else
		snd_iprintf(buffer, "No Modem Function Group found\n");

	if (! codec->afg)
		return;
	snd_hda_power_up(codec);
	snd_iprintf(buffer, "Default PCM:\n");
	print_pcm_caps(buffer, codec, codec->afg);
	snd_iprintf(buffer, "Default Amp-In caps: ");
	print_amp_caps(buffer, codec, codec->afg, HDA_INPUT);
	snd_iprintf(buffer, "Default Amp-Out caps: ");
	print_amp_caps(buffer, codec, codec->afg, HDA_OUTPUT);

	nodes = snd_hda_get_sub_nodes(codec, codec->afg, &nid);
	if (! nid || nodes < 0) {
		snd_iprintf(buffer, "Invalid AFG subtree\n");
		snd_hda_power_down(codec);
		return;
	}

	print_gpio(buffer, codec, codec->afg);
	if (codec->proc_widget_hook)
		codec->proc_widget_hook(buffer, codec, codec->afg);

	for (i = 0; i < nodes; i++, nid++) {
		unsigned int wid_caps =
			snd_hda_param_read(codec, nid,
					   AC_PAR_AUDIO_WIDGET_CAP);
		unsigned int wid_type = get_wcaps_type(wid_caps);
		hda_nid_t conn[HDA_MAX_CONNECTIONS];
		int conn_len = 0;

		snd_iprintf(buffer, "Node 0x%02x [%s] wcaps 0x%x:", nid,
			    get_wid_type_name(wid_type), wid_caps);
		if (wid_caps & AC_WCAP_STEREO) {
			unsigned int chans = get_wcaps_channels(wid_caps);
			if (chans == 2)
				snd_iprintf(buffer, " Stereo");
			else
				snd_iprintf(buffer, " %d-Channels", chans);
		} else
			snd_iprintf(buffer, " Mono");
		if (wid_caps & AC_WCAP_DIGITAL)
			snd_iprintf(buffer, " Digital");
		if (wid_caps & AC_WCAP_IN_AMP)
			snd_iprintf(buffer, " Amp-In");
		if (wid_caps & AC_WCAP_OUT_AMP)
			snd_iprintf(buffer, " Amp-Out");
		if (wid_caps & AC_WCAP_STRIPE)
			snd_iprintf(buffer, " Stripe");
		if (wid_caps & AC_WCAP_LR_SWAP)
			snd_iprintf(buffer, " R/L");
		if (wid_caps & AC_WCAP_CP_CAPS)
			snd_iprintf(buffer, " CP");
		snd_iprintf(buffer, "\n");

		/* volume knob is a special widget that always have connection
		 * list
		 */
		if (wid_type == AC_WID_VOL_KNB)
			wid_caps |= AC_WCAP_CONN_LIST;

		if (wid_caps & AC_WCAP_CONN_LIST)
			conn_len = snd_hda_get_connections(codec, nid, conn,
							   HDA_MAX_CONNECTIONS);

		if (wid_caps & AC_WCAP_IN_AMP) {
			snd_iprintf(buffer, "  Amp-In caps: ");
			print_amp_caps(buffer, codec, nid, HDA_INPUT);
			snd_iprintf(buffer, "  Amp-In vals: ");
			print_amp_vals(buffer, codec, nid, HDA_INPUT,
				       wid_caps & AC_WCAP_STEREO,
				       wid_type == AC_WID_PIN ? 1 : conn_len);
		}
		if (wid_caps & AC_WCAP_OUT_AMP) {
			snd_iprintf(buffer, "  Amp-Out caps: ");
			print_amp_caps(buffer, codec, nid, HDA_OUTPUT);
			snd_iprintf(buffer, "  Amp-Out vals: ");
			if (wid_type == AC_WID_PIN &&
			    codec->pin_amp_workaround)
				print_amp_vals(buffer, codec, nid, HDA_OUTPUT,
					       wid_caps & AC_WCAP_STEREO,
					       conn_len);
			else
				print_amp_vals(buffer, codec, nid, HDA_OUTPUT,
					       wid_caps & AC_WCAP_STEREO, 1);
		}

		switch (wid_type) {
		case AC_WID_PIN: {
			int supports_vref;
			print_pin_caps(buffer, codec, nid, &supports_vref);
			print_pin_ctls(buffer, codec, nid, supports_vref);
			break;
		}
		case AC_WID_VOL_KNB:
			print_vol_knob(buffer, codec, nid);
			break;
		case AC_WID_AUD_OUT:
		case AC_WID_AUD_IN:
			print_audio_io(buffer, codec, nid, wid_type);
			if (wid_caps & AC_WCAP_DIGITAL)
				print_digital_conv(buffer, codec, nid);
			if (wid_caps & AC_WCAP_FORMAT_OVRD) {
				snd_iprintf(buffer, "  PCM:\n");
				print_pcm_caps(buffer, codec, nid);
			}
			break;
		}

		if (wid_caps & AC_WCAP_UNSOL_CAP)
			print_unsol_cap(buffer, codec, nid);

		if (wid_caps & AC_WCAP_POWER)
			print_power_state(buffer, codec, nid);

		if (wid_caps & AC_WCAP_DELAY)
			snd_iprintf(buffer, "  Delay: %d samples\n",
				    (wid_caps & AC_WCAP_DELAY) >>
				    AC_WCAP_DELAY_SHIFT);

		if (wid_caps & AC_WCAP_CONN_LIST)
			print_conn_list(buffer, codec, nid, wid_type,
					conn, conn_len);

		if (wid_caps & AC_WCAP_PROC_WID)
			print_proc_caps(buffer, codec, nid);

		if (codec->proc_widget_hook)
			codec->proc_widget_hook(buffer, codec, nid);
	}
	snd_hda_power_down(codec);
}

/*
 * create a proc read
 */
int snd_hda_codec_proc_new(struct hda_codec *codec)
{
	char name[32];
	struct snd_info_entry *entry;
	int err;

	snprintf(name, sizeof(name), "codec#%d", codec->addr);
	err = snd_card_proc_new(codec->bus->card, name, &entry);
	if (err < 0)
		return err;

	snd_info_set_text_ops(entry, codec, print_codec_info);
	return 0;
}

