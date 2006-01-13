/*
 * descriptions for simple tuners.
 */

#ifndef __TUNER_TYPES_H__
#define __TUNER_TYPES_H__

enum param_type {
	TUNER_PARAM_TYPE_RADIO, \
	TUNER_PARAM_TYPE_PAL, \
	TUNER_PARAM_TYPE_SECAM, \
	TUNER_PARAM_TYPE_NTSC, \
	TUNER_PARAM_TYPE_ATSC, \
	TUNER_PARAM_TYPE_DVBT, \
	TUNER_PARAM_TYPE_DVBS, \
	TUNER_PARAM_TYPE_DVBC
};

struct tuner_range {
	unsigned short limit;
	unsigned char cb;
};

struct tuner_params {
	enum param_type type;
	unsigned char config; /* to be moved into struct tuner_range for dvb-pll merge */

	unsigned int count;
	struct tuner_range *ranges;
};

struct tunertype {
	char *name;
	unsigned int has_tda988x:1;
	struct tuner_params *params;
};

extern struct tunertype tuners[];
extern unsigned const int tuner_count;

#endif
