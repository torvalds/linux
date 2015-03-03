/*
 */

#ifndef __HDAC_LOCAL_H
#define __HDAC_LOCAL_H

int hdac_read_parm(struct hdac_device *codec, hda_nid_t nid, int parm);

#define get_wcaps(codec, nid) \
	hdac_read_parm(codec, nid, AC_PAR_AUDIO_WIDGET_CAP)
/* get the widget type from widget capability bits */
static inline int get_wcaps_type(unsigned int wcaps)
{
	if (!wcaps)
		return -1; /* invalid type */
	return (wcaps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
}

#define get_pin_caps(codec, nid) \
	hdac_read_parm(codec, nid, AC_PAR_PIN_CAP)

static inline
unsigned int get_pin_cfg(struct hdac_device *codec, hda_nid_t nid)
{
	unsigned int val;

	if (snd_hdac_read(codec, nid, AC_VERB_GET_CONFIG_DEFAULT, 0, &val))
		return -1;
	return val;
}

#define get_amp_caps(codec, nid, dir) \
	hdac_read_parm(codec, nid, (dir) == HDA_OUTPUT ? \
		       AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP)

#define get_power_caps(codec, nid) \
	hdac_read_parm(codec, nid, AC_PAR_POWER_STATE)

#endif /* __HDAC_LOCAL_H */
