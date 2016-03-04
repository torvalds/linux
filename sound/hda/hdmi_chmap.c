/*
 * HDMI Channel map support helpers
 */

#include <sound/hda_chmap.h>

static int hdmi_pin_set_slot_channel(struct hdac_device *codec,
		hda_nid_t pin_nid, int asp_slot, int channel)
{
	return snd_hdac_codec_write(codec, pin_nid, 0,
				AC_VERB_SET_HDMI_CHAN_SLOT,
				(channel << 4) | asp_slot);
}

static int hdmi_pin_get_slot_channel(struct hdac_device *codec,
			hda_nid_t pin_nid, int asp_slot)
{
	return (snd_hdac_codec_read(codec, pin_nid, 0,
				   AC_VERB_GET_HDMI_CHAN_SLOT,
				   asp_slot) & 0xf0) >> 4;
}

static int hdmi_get_channel_count(struct hdac_device *codec, hda_nid_t cvt_nid)
{
	return 1 + snd_hdac_codec_read(codec, cvt_nid, 0,
					AC_VERB_GET_CVT_CHAN_COUNT, 0);
}

static void hdmi_set_channel_count(struct hdac_device *codec,
				   hda_nid_t cvt_nid, int chs)
{
	if (chs != hdmi_get_channel_count(codec, cvt_nid))
		snd_hdac_codec_write(codec, cvt_nid, 0,
				    AC_VERB_SET_CVT_CHAN_COUNT, chs - 1);
}

static const struct hdac_chmap_ops chmap_ops = {
	.pin_get_slot_channel			= hdmi_pin_get_slot_channel,
	.pin_set_slot_channel			= hdmi_pin_set_slot_channel,
	.set_channel_count			= hdmi_set_channel_count,
};

void snd_hdac_register_chmap_ops(struct hdac_device *hdac,
				struct hdac_chmap *chmap)
{
	chmap->ops = chmap_ops;
	chmap->hdac = hdac;
}
EXPORT_SYMBOL_GPL(snd_hdac_register_chmap_ops);
