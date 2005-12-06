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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"

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
		return "UNKOWN Widget";
}

static void print_amp_caps(snd_info_buffer_t *buffer,
			   struct hda_codec *codec, hda_nid_t nid, int dir)
{
	unsigned int caps;
	if (dir == HDA_OUTPUT)
		caps = snd_hda_param_read(codec, nid, AC_PAR_AMP_OUT_CAP);
	else
		caps = snd_hda_param_read(codec, nid, AC_PAR_AMP_IN_CAP);
	if (caps == -1 || caps == 0) {
		snd_iprintf(buffer, "N/A\n");
		return;
	}
	snd_iprintf(buffer, "ofs=0x%02x, nsteps=0x%02x, stepsize=0x%02x, mute=%x\n",
		    caps & AC_AMPCAP_OFFSET,
		    (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT,
		    (caps & AC_AMPCAP_STEP_SIZE) >> AC_AMPCAP_STEP_SIZE_SHIFT,
		    (caps & AC_AMPCAP_MUTE) >> AC_AMPCAP_MUTE_SHIFT);
}

static void print_amp_vals(snd_info_buffer_t *buffer,
			   struct hda_codec *codec, hda_nid_t nid,
			   int dir, int stereo, int indices)
{
	unsigned int val;
	int i;

	if (dir == HDA_OUTPUT)
		dir = AC_AMP_GET_OUTPUT;
	else
		dir = AC_AMP_GET_INPUT;
	for (i = 0; i < indices; i++) {
		snd_iprintf(buffer, " [");
		if (stereo) {
			val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE,
						 AC_AMP_GET_LEFT | dir | i);
			snd_iprintf(buffer, "0x%02x ", val);
		}
		val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_AMP_GAIN_MUTE,
					 AC_AMP_GET_RIGHT | dir | i);
		snd_iprintf(buffer, "0x%02x]", val);
	}
	snd_iprintf(buffer, "\n");
}

static void print_pcm_caps(snd_info_buffer_t *buffer,
			   struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pcm = snd_hda_param_read(codec, nid, AC_PAR_PCM);
	unsigned int stream = snd_hda_param_read(codec, nid, AC_PAR_STREAM);
	if (pcm == -1 || stream == -1) {
		snd_iprintf(buffer, "N/A\n");
		return;
	}
	snd_iprintf(buffer, "rates 0x%03x, bits 0x%02x, types 0x%x\n",
		    pcm & AC_SUPPCM_RATES, (pcm >> 16) & 0xff, stream & 0xf);
}

static const char *get_jack_location(u32 cfg)
{
	static char *bases[7] = {
		"N/A", "Rear", "Front", "Left", "Right", "Top", "Bottom",
	};
	static unsigned char specials_idx[] = {
		0x07, 0x08,
		0x17, 0x18, 0x19,
		0x37, 0x38
	};
	static char *specials[] = {
		"Rear Panel", "Drive Bar",
		"Riser", "HDMI", "ATAPI",
		"Mobile-In", "Mobile-Out"
	};
	int i;
	cfg = (cfg & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT;
	if ((cfg & 0x0f) < 7)
		return bases[cfg & 0x0f];
	for (i = 0; i < ARRAY_SIZE(specials_idx); i++) {
		if (cfg == specials_idx[i])
			return specials[i];
	}
	return "UNKNOWN";
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

static void print_pin_caps(snd_info_buffer_t *buffer,
			   struct hda_codec *codec, hda_nid_t nid)
{
	static char *jack_conns[4] = { "Jack", "N/A", "Fixed", "Both" };
	static char *jack_types[16] = {
		"Line Out", "Speaker", "HP Out", "CD",
		"SPDIF Out", "Digital Out", "Modem Line", "Modem Hand",
		"Line In", "Aux", "Mic", "Telephony",
		"SPDIF In", "Digitial In", "Reserved", "Other"
	};
	static char *jack_locations[4] = { "Ext", "Int", "Sep", "Oth" };
	unsigned int caps;

	caps = snd_hda_param_read(codec, nid, AC_PAR_PIN_CAP);
	snd_iprintf(buffer, "  Pincap 0x08%x:", caps);
	if (caps & AC_PINCAP_IN)
		snd_iprintf(buffer, " IN");
	if (caps & AC_PINCAP_OUT)
		snd_iprintf(buffer, " OUT");
	if (caps & AC_PINCAP_HP_DRV)
		snd_iprintf(buffer, " HP");
	snd_iprintf(buffer, "\n");
	caps = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
	snd_iprintf(buffer, "  Pin Default 0x%08x: [%s] %s at %s %s\n", caps,
		    jack_conns[(caps & AC_DEFCFG_PORT_CONN) >> AC_DEFCFG_PORT_CONN_SHIFT],
		    jack_types[(caps & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT],
		    jack_locations[(caps >> (AC_DEFCFG_LOCATION_SHIFT + 4)) & 3],
		    get_jack_location(caps));
	snd_iprintf(buffer, "    Conn = %s, Color = %s\n",
		    get_jack_connection(caps),
		    get_jack_color(caps));
}


static void print_codec_info(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	struct hda_codec *codec = entry->private_data;
	char buf[32];
	hda_nid_t nid;
	int i, nodes;

	snd_hda_get_codec_name(codec, buf, sizeof(buf));
	snd_iprintf(buffer, "Codec: %s\n", buf);
	snd_iprintf(buffer, "Address: %d\n", codec->addr);
	snd_iprintf(buffer, "Vendor Id: 0x%x\n", codec->vendor_id);
	snd_iprintf(buffer, "Subsystem Id: 0x%x\n", codec->subsystem_id);
	snd_iprintf(buffer, "Revision Id: 0x%x\n", codec->revision_id);
	if (! codec->afg)
		return;
	snd_iprintf(buffer, "Default PCM: ");
	print_pcm_caps(buffer, codec, codec->afg);
	snd_iprintf(buffer, "Default Amp-In caps: ");
	print_amp_caps(buffer, codec, codec->afg, HDA_INPUT);
	snd_iprintf(buffer, "Default Amp-Out caps: ");
	print_amp_caps(buffer, codec, codec->afg, HDA_OUTPUT);

	nodes = snd_hda_get_sub_nodes(codec, codec->afg, &nid);
	if (! nid || nodes < 0) {
		snd_iprintf(buffer, "Invalid AFG subtree\n");
		return;
	}
	for (i = 0; i < nodes; i++, nid++) {
		unsigned int wid_caps = snd_hda_param_read(codec, nid,
							   AC_PAR_AUDIO_WIDGET_CAP);
		unsigned int wid_type = (wid_caps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		int conn_len = 0; 
		hda_nid_t conn[HDA_MAX_CONNECTIONS];

		snd_iprintf(buffer, "Node 0x%02x [%s] wcaps 0x%x:", nid,
			    get_wid_type_name(wid_type), wid_caps);
		if (wid_caps & AC_WCAP_STEREO)
			snd_iprintf(buffer, " Stereo");
		else
			snd_iprintf(buffer, " Mono");
		if (wid_caps & AC_WCAP_DIGITAL)
			snd_iprintf(buffer, " Digital");
		if (wid_caps & AC_WCAP_IN_AMP)
			snd_iprintf(buffer, " Amp-In");
		if (wid_caps & AC_WCAP_OUT_AMP)
			snd_iprintf(buffer, " Amp-Out");
		snd_iprintf(buffer, "\n");

		if (wid_caps & AC_WCAP_CONN_LIST)
			conn_len = snd_hda_get_connections(codec, nid, conn,
							   HDA_MAX_CONNECTIONS);

		if (wid_caps & AC_WCAP_IN_AMP) {
			snd_iprintf(buffer, "  Amp-In caps: ");
			print_amp_caps(buffer, codec, nid, HDA_INPUT);
			snd_iprintf(buffer, "  Amp-In vals: ");
			print_amp_vals(buffer, codec, nid, HDA_INPUT,
				       wid_caps & AC_WCAP_STEREO, conn_len);
		}
		if (wid_caps & AC_WCAP_OUT_AMP) {
			snd_iprintf(buffer, "  Amp-Out caps: ");
			print_amp_caps(buffer, codec, nid, HDA_OUTPUT);
			snd_iprintf(buffer, "  Amp-Out vals: ");
			print_amp_vals(buffer, codec, nid, HDA_OUTPUT,
				       wid_caps & AC_WCAP_STEREO, 1);
		}

		if (wid_type == AC_WID_PIN) {
			unsigned int pinctls;
			print_pin_caps(buffer, codec, nid);
			pinctls = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			snd_iprintf(buffer, "  Pin-ctls: 0x%02x:", pinctls);
			if (pinctls & AC_PINCTL_IN_EN)
				snd_iprintf(buffer, " IN");
			if (pinctls & AC_PINCTL_OUT_EN)
				snd_iprintf(buffer, " OUT");
			if (pinctls & AC_PINCTL_HP_EN)
				snd_iprintf(buffer, " HP");
			snd_iprintf(buffer, "\n");
		}

		if ((wid_type == AC_WID_AUD_OUT || wid_type == AC_WID_AUD_IN) &&
		    (wid_caps & AC_WCAP_FORMAT_OVRD)) {
			snd_iprintf(buffer, "  PCM: ");
			print_pcm_caps(buffer, codec, nid);
		}

		if (wid_caps & AC_WCAP_POWER)
			snd_iprintf(buffer, "  Power: 0x%x\n",
				    snd_hda_codec_read(codec, nid, 0,
						       AC_VERB_GET_POWER_STATE, 0));

		if (wid_caps & AC_WCAP_CONN_LIST) {
			int c, curr = -1;
			if (conn_len > 1 && wid_type != AC_WID_AUD_MIX)
				curr = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_CONNECT_SEL, 0);
			snd_iprintf(buffer, "  Connection: %d\n", conn_len);
			snd_iprintf(buffer, "    ");
			for (c = 0; c < conn_len; c++) {
				snd_iprintf(buffer, " 0x%02x", conn[c]);
				if (c == curr)
					snd_iprintf(buffer, "*");
			}
			snd_iprintf(buffer, "\n");
		}
	}
}

/*
 * create a proc read
 */
int snd_hda_codec_proc_new(struct hda_codec *codec)
{
	char name[32];
	snd_info_entry_t *entry;
	int err;

	snprintf(name, sizeof(name), "codec#%d", codec->addr);
	err = snd_card_proc_new(codec->bus->card, name, &entry);
	if (err < 0)
		return err;

	snd_info_set_text_ops(entry, codec, 32 * 1024, print_codec_info);
	return 0;
}

