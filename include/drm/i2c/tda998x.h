#ifndef __DRM_I2C_TDA998X_H__
#define __DRM_I2C_TDA998X_H__

#include <linux/hdmi.h>
#include <dt-bindings/display/tda998x.h>

enum {
	AFMT_UNUSED =	0,
	AFMT_SPDIF =	TDA998x_SPDIF,
	AFMT_I2S =	TDA998x_I2S,
};

struct tda998x_audio_params {
	u8 config;
	u8 format;
	unsigned sample_width;
	unsigned sample_rate;
	struct hdmi_audio_infoframe cea;
	u8 status[5];
};

struct tda998x_encoder_params {
	u8 swap_b:3;
	u8 mirr_b:1;
	u8 swap_a:3;
	u8 mirr_a:1;
	u8 swap_d:3;
	u8 mirr_d:1;
	u8 swap_c:3;
	u8 mirr_c:1;
	u8 swap_f:3;
	u8 mirr_f:1;
	u8 swap_e:3;
	u8 mirr_e:1;

	struct tda998x_audio_params audio_params;
};

#endif
