/* SPDX-License-Identifier: MIT
 * Copyright (C) 2018 Intel Corp.
 *
 * Authors:
 * Manasi Navare <manasi.d.navare@intel.com>
 */

#ifndef DRM_DSC_HELPER_H_
#define DRM_DSC_HELPER_H_

#include <drm/display/drm_dsc.h>

void drm_dsc_dp_pps_header_init(struct dp_sdp_header *pps_header);
int drm_dsc_dp_rc_buffer_size(u8 rc_buffer_block_size, u8 rc_buffer_size);
void drm_dsc_pps_payload_pack(struct drm_dsc_picture_parameter_set *pps_sdp,
			      const struct drm_dsc_config *dsc_cfg);
int drm_dsc_compute_rc_parameters(struct drm_dsc_config *vdsc_cfg);

#endif /* _DRM_DSC_HELPER_H_ */

