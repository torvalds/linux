#ifndef __DRM_I2C_TDA998X_H__
#define __DRM_I2C_TDA998X_H__

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

	u8 audio_cfg;
	u8 audio_clk_cfg;
	u8 audio_frame[6];

	enum {
		AFMT_SPDIF,
		AFMT_I2S
	} audio_format;

	unsigned audio_sample_rate;
};

#endif
