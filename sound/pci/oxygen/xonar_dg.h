#ifndef XONAR_DG_H_INCLUDED
#define XONAR_DG_H_INCLUDED

#include "oxygen.h"

#define GPIO_MAGIC		0x0008
#define GPIO_HP_DETECT		0x0010
#define GPIO_INPUT_ROUTE	0x0060
#define GPIO_HP_REAR		0x0080
#define GPIO_OUTPUT_ENABLE	0x0100

#define CAPTURE_SRC_MIC		0
#define CAPTURE_SRC_FP_MIC	1
#define CAPTURE_SRC_LINE	2
#define CAPTURE_SRC_AUX		3

#define PLAYBACK_DST_HP		0
#define PLAYBACK_DST_HP_FP	1
#define PLAYBACK_DST_MULTICH	2

enum cs4245_shadow_operation {
	CS4245_SAVE_TO_SHADOW,
	CS4245_LOAD_FROM_SHADOW
};

struct dg {
	/* shadow copy of the CS4245 register space */
	unsigned char cs4245_shadow[17];
	unsigned int output_sel;
	s8 input_vol[4][2];
	unsigned int input_sel;
	u8 hp_vol_att;
};

extern struct oxygen_model model_xonar_dg;

#endif
