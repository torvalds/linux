/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef AMD_DAL_DEV_AMDGPU_DM_AMDGPU_DM_CRC_H_
#define AMD_DAL_DEV_AMDGPU_DM_AMDGPU_DM_CRC_H_

struct drm_crtc;
struct dm_crtc_state;

enum amdgpu_dm_pipe_crc_source {
	AMDGPU_DM_PIPE_CRC_SOURCE_NONE = 0,
	AMDGPU_DM_PIPE_CRC_SOURCE_CRTC,
	AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER,
	AMDGPU_DM_PIPE_CRC_SOURCE_DPRX,
	AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER,
	AMDGPU_DM_PIPE_CRC_SOURCE_MAX,
	AMDGPU_DM_PIPE_CRC_SOURCE_INVALID = -1,
};

#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
struct crc_window_param {
	uint16_t x_start;
	uint16_t y_start;
	uint16_t x_end;
	uint16_t y_end;
	/* CRC window is activated or not*/
	bool activated;
	/* Update crc window during vertical blank or not */
	bool update_win;
	/* skip reading/writing for few frames */
	int skip_frame_cnt;
};

struct secure_display_context {
	/* work to notify PSP TA*/
	struct work_struct notify_ta_work;

	/* work to forward ROI to dmcu/dmub */
	struct work_struct forward_roi_work;

	struct drm_crtc *crtc;

	/* Region of Interest (ROI) */
	struct rect rect;
};
#endif

static inline bool amdgpu_dm_is_valid_crc_source(enum amdgpu_dm_pipe_crc_source source)
{
	return (source > AMDGPU_DM_PIPE_CRC_SOURCE_NONE) &&
	       (source < AMDGPU_DM_PIPE_CRC_SOURCE_MAX);
}

/* amdgpu_dm_crc.c */
#ifdef CONFIG_DEBUG_FS
int amdgpu_dm_crtc_configure_crc_source(struct drm_crtc *crtc,
					struct dm_crtc_state *dm_crtc_state,
					enum amdgpu_dm_pipe_crc_source source);
int amdgpu_dm_crtc_set_crc_source(struct drm_crtc *crtc, const char *src_name);
int amdgpu_dm_crtc_verify_crc_source(struct drm_crtc *crtc,
				     const char *src_name,
				     size_t *values_cnt);
const char *const *amdgpu_dm_crtc_get_crc_sources(struct drm_crtc *crtc,
						  size_t *count);
void amdgpu_dm_crtc_handle_crc_irq(struct drm_crtc *crtc);
#else
#define amdgpu_dm_crtc_set_crc_source NULL
#define amdgpu_dm_crtc_verify_crc_source NULL
#define amdgpu_dm_crtc_get_crc_sources NULL
#define amdgpu_dm_crtc_handle_crc_irq(x)
#endif

#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
bool amdgpu_dm_crc_window_is_activated(struct drm_crtc *crtc);
void amdgpu_dm_crtc_handle_crc_window_irq(struct drm_crtc *crtc);
struct secure_display_context *amdgpu_dm_crtc_secure_display_create_contexts(
						struct amdgpu_device *adev);
#else
#define amdgpu_dm_crc_window_is_activated(x)
#define amdgpu_dm_crtc_handle_crc_window_irq(x)
#define amdgpu_dm_crtc_secure_display_create_contexts(x)
#endif

#endif /* AMD_DAL_DEV_AMDGPU_DM_AMDGPU_DM_CRC_H_ */
