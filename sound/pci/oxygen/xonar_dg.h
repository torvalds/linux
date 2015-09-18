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
	/* output select: headphone/speakers */
	unsigned char output_sel;
	/* volumes for all capture sources */
	char input_vol[4][2];
	/* input select: mic/fp mic/line/aux */
	unsigned char input_sel;
};

/* Xonar DG control routines */
int cs4245_write_spi(struct oxygen *chip, u8 reg);
int cs4245_read_spi(struct oxygen *chip, u8 reg);
int cs4245_shadow_control(struct oxygen *chip, enum cs4245_shadow_operation op);
void dg_init(struct oxygen *chip);
void set_cs4245_dac_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params);
void set_cs4245_adc_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params);
unsigned int adjust_dg_dac_routing(struct oxygen *chip,
					  unsigned int play_routing);
void dump_cs4245_registers(struct oxygen *chip,
				struct snd_info_buffer *buffer);
void dg_suspend(struct oxygen *chip);
void dg_resume(struct oxygen *chip);
void dg_cleanup(struct oxygen *chip);

extern struct oxygen_model model_xonar_dg;

#endif
