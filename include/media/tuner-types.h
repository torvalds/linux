/*
 * descriptions for simple tuners.
 */

#ifndef __TUNER_TYPES_H__
#define __TUNER_TYPES_H__

enum param_type {
	TUNER_PARAM_TYPE_RADIO, \
	TUNER_PARAM_TYPE_PAL, \
	TUNER_PARAM_TYPE_SECAM, \
	TUNER_PARAM_TYPE_NTSC
};

struct tuner_range {
	unsigned short limit;
	unsigned char config;
	unsigned char cb;
};

struct tuner_params {
	enum param_type type;

	/* Many Philips based tuners have a comment like this in their
	 * datasheet:
	 *
	 *   For channel selection involving band switching, and to ensure
	 *   smooth tuning to the desired channel without causing
	 *   unnecessary charge pump action, it is recommended to consider
	 *   the difference between wanted channel frequency and the
	 *   current channel frequency.  Unnecessary charge pump action
	 *   will result in very low tuning voltage which may drive the
	 *   oscillator to extreme conditions.
	 *
	 * Set cb_first_if_lower_freq to 1, if this check is
	 * required for this tuner.
	 *
	 * I tested this for PAL by first setting the TV frequency to
	 * 203 MHz and then switching to 96.6 MHz FM radio. The result was
	 * static unless the control byte was sent first.
	 */
	unsigned int cb_first_if_lower_freq:1;
	/* Set to 1 if this tuner uses a tda9887 */
	unsigned int has_tda9887:1;
	/* Many Philips tuners use tda9887 PORT1 to select the FM radio
	   sensitivity. If this setting is 1, then set PORT1 to 1 to
	   get proper FM reception. */
	unsigned int port1_fm_high_sensitivity:1;
	/* Some Philips tuners use tda9887 PORT2 to select the FM radio
	   sensitivity. If this setting is 1, then set PORT2 to 1 to
	   get proper FM reception. */
	unsigned int port2_fm_high_sensitivity:1;
	/* Most tuners with a tda9887 use QSS mode. Some (cheaper) tuners
	   use Intercarrier mode. If this setting is 1, then the tuner
	   needs to be set to intercarrier mode. */
	unsigned int intercarrier_mode:1;
	/* This setting sets the default value for PORT1.
	   0 means inactive, 1 means active. Note: the actual bit
	   value written to the tda9887 is inverted. So a 0 here
	   means a 1 in the B6 bit. */
	unsigned int port1_active:1;
	/* This setting sets the default value for PORT2.
	   0 means inactive, 1 means active. Note: the actual bit
	   value written to the tda9887 is inverted. So a 0 here
	   means a 1 in the B7 bit. */
	unsigned int port2_active:1;
	/* Sometimes PORT1 is inverted when the SECAM-L' standard is selected.
	   Set this bit to 1 if this is needed. */
	unsigned int port1_invert_for_secam_lc:1;
	/* Sometimes PORT2 is inverted when the SECAM-L' standard is selected.
	   Set this bit to 1 if this is needed. */
	unsigned int port2_invert_for_secam_lc:1;
	/* Some cards require PORT1 to be 1 for mono Radio FM and 0 for stereo. */
	unsigned int port1_set_for_fm_mono:1;
	/* Default tda9887 TOP value in dB for the low band. Default is 0.
	   Range: -16:+15 */
	signed int default_top_low:5;
	/* Default tda9887 TOP value in dB for the mid band. Default is 0.
	   Range: -16:+15 */
	signed int default_top_mid:5;
	/* Default tda9887 TOP value in dB for the high band. Default is 0.
	   Range: -16:+15 */
	signed int default_top_high:5;
	/* Default tda9887 TOP value in dB for SECAM-L/L' for the low band.
	   Default is 0. Several tuners require a different TOP value for
	   the SECAM-L/L' standards. Range: -16:+15 */
	signed int default_top_secam_low:5;
	/* Default tda9887 TOP value in dB for SECAM-L/L' for the mid band.
	   Default is 0. Several tuners require a different TOP value for
	   the SECAM-L/L' standards. Range: -16:+15 */
	signed int default_top_secam_mid:5;
	/* Default tda9887 TOP value in dB for SECAM-L/L' for the high band.
	   Default is 0. Several tuners require a different TOP value for
	   the SECAM-L/L' standards. Range: -16:+15 */
	signed int default_top_secam_high:5;


	unsigned int count;
	struct tuner_range *ranges;
};

struct tunertype {
	char *name;
	unsigned int count;
	struct tuner_params *params;
};

extern struct tunertype tuners[];
extern unsigned const int tuner_count;

#endif
