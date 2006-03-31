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
