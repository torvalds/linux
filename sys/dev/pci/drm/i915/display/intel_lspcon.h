/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_LSPCON_H__
#define __INTEL_LSPCON_H__

#include <linux/types.h>

struct drm_connector;
struct drm_connector_state;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_encoder;
struct intel_lspcon;

bool lspcon_init(struct intel_digital_port *dig_port);
void lspcon_detect_hdr_capability(struct intel_lspcon *lspcon);
void lspcon_resume(struct intel_digital_port *dig_port);
void lspcon_wait_pcon_mode(struct intel_lspcon *lspcon);
void lspcon_write_infoframe(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    unsigned int type,
			    const void *buf, ssize_t len);
void lspcon_read_infoframe(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   unsigned int type,
			   void *frame, ssize_t len);
void lspcon_set_infoframes(struct intel_encoder *encoder,
			   bool enable,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state);
u32 lspcon_infoframes_enabled(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config);
u32 intel_lspcon_infoframes_enabled(struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config);
void hsw_write_infoframe(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 unsigned int type,
			 const void *frame, ssize_t len);
void hsw_read_infoframe(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			unsigned int type,
			void *frame, ssize_t len);

#endif /* __INTEL_LSPCON_H__ */
