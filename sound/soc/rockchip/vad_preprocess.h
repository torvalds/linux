/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip VAD Preprocess
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

#ifndef _ROCKCHIP_VAD_PREPROCESS_H
#define _ROCKCHIP_VAD_PREPROCESS_H

struct vad_params {
	int noise_abs;
	int noise_level;
	int sound_thd;
	int vad_con_thd;
	int voice_gain;
};

struct vad_uparams {
	int noise_abs;
};

void vad_preprocess_init(struct vad_params *params);
void vad_preprocess_destroy(void);
void vad_preprocess_update_params(struct vad_uparams *uparams);
int vad_preprocess(int data);

#endif
