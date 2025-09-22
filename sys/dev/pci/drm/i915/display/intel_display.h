/*
 * Copyright © 2006-2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _INTEL_DISPLAY_H_
#define _INTEL_DISPLAY_H_

#include <drm/drm_util.h>

#include "i915_reg_defs.h"
#include "intel_display_limits.h"

enum drm_scaling_filter;
struct dpll;
struct drm_atomic_state;
struct drm_connector;
struct drm_device;
struct drm_display_mode;
struct drm_encoder;
struct drm_file;
struct drm_format_info;
struct drm_framebuffer;
struct drm_i915_gem_object;
struct drm_i915_private;
struct drm_mode_fb_cmd2;
struct drm_modeset_acquire_ctx;
struct drm_plane;
struct drm_plane_state;
struct i915_address_space;
struct i915_gtt_view;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_dp;
struct intel_encoder;
struct intel_initial_plane_config;
struct intel_link_m_n;
struct intel_plane;
struct intel_plane_state;
struct intel_power_domain_mask;
struct intel_remapped_info;
struct intel_rotation_info;
struct pci_dev;
struct work_struct;


#define pipe_name(p) ((p) + 'A')

static inline const char *transcoder_name(enum transcoder transcoder)
{
	switch (transcoder) {
	case TRANSCODER_A:
		return "A";
	case TRANSCODER_B:
		return "B";
	case TRANSCODER_C:
		return "C";
	case TRANSCODER_D:
		return "D";
	case TRANSCODER_EDP:
		return "EDP";
	case TRANSCODER_DSI_A:
		return "DSI A";
	case TRANSCODER_DSI_C:
		return "DSI C";
	default:
		return "<invalid>";
	}
}

static inline bool transcoder_is_dsi(enum transcoder transcoder)
{
	return transcoder == TRANSCODER_DSI_A || transcoder == TRANSCODER_DSI_C;
}

/*
 * Global legacy plane identifier. Valid only for primary/sprite
 * planes on pre-g4x, and only for primary planes on g4x-bdw.
 */
enum i9xx_plane_id {
	PLANE_A,
	PLANE_B,
	PLANE_C,
};

#define plane_name(p) ((p) + 'A')

#define for_each_plane_id_on_crtc(__crtc, __p) \
	for ((__p) = PLANE_PRIMARY; (__p) < I915_MAX_PLANES; (__p)++) \
		for_each_if((__crtc)->plane_ids_mask & BIT(__p))

#define for_each_dbuf_slice(__dev_priv, __slice) \
	for ((__slice) = DBUF_S1; (__slice) < I915_MAX_DBUF_SLICES; (__slice)++) \
		for_each_if(DISPLAY_INFO(__dev_priv)->dbuf.slice_mask & BIT(__slice))

#define for_each_dbuf_slice_in_mask(__dev_priv, __slice, __mask) \
	for_each_dbuf_slice((__dev_priv), (__slice)) \
		for_each_if((__mask) & BIT(__slice))

#define port_name(p) ((p) + 'A')

/*
 * Ports identifier referenced from other drivers.
 * Expected to remain stable over time
 */
static inline const char *port_identifier(enum port port)
{
	switch (port) {
	case PORT_A:
		return "Port A";
	case PORT_B:
		return "Port B";
	case PORT_C:
		return "Port C";
	case PORT_D:
		return "Port D";
	case PORT_E:
		return "Port E";
	case PORT_F:
		return "Port F";
	case PORT_G:
		return "Port G";
	case PORT_H:
		return "Port H";
	case PORT_I:
		return "Port I";
	default:
		return "<invalid>";
	}
}

enum tc_port {
	TC_PORT_NONE = -1,

	TC_PORT_1 = 0,
	TC_PORT_2,
	TC_PORT_3,
	TC_PORT_4,
	TC_PORT_5,
	TC_PORT_6,

	I915_MAX_TC_PORTS
};

enum aux_ch {
	AUX_CH_NONE = -1,

	AUX_CH_A,
	AUX_CH_B,
	AUX_CH_C,
	AUX_CH_D,
	AUX_CH_E, /* ICL+ */
	AUX_CH_F,
	AUX_CH_G,
	AUX_CH_H,
	AUX_CH_I,

	/* tgl+ */
	AUX_CH_USBC1 = AUX_CH_D,
	AUX_CH_USBC2,
	AUX_CH_USBC3,
	AUX_CH_USBC4,
	AUX_CH_USBC5,
	AUX_CH_USBC6,

	/* XE_LPD repositions D/E offsets and bitfields */
	AUX_CH_D_XELPD = AUX_CH_USBC5,
	AUX_CH_E_XELPD,
};

enum phy {
	PHY_NONE = -1,

	PHY_A = 0,
	PHY_B,
	PHY_C,
	PHY_D,
	PHY_E,
	PHY_F,
	PHY_G,
	PHY_H,
	PHY_I,

	I915_MAX_PHYS
};

#define phy_name(a) ((a) + 'A')

enum phy_fia {
	FIA1,
	FIA2,
	FIA3,
};

#define for_each_hpd_pin(__pin) \
	for ((__pin) = (HPD_NONE + 1); (__pin) < HPD_NUM_PINS; (__pin)++)

#define for_each_pipe(__dev_priv, __p) \
	for ((__p) = 0; (__p) < I915_MAX_PIPES; (__p)++) \
		for_each_if(DISPLAY_RUNTIME_INFO(__dev_priv)->pipe_mask & BIT(__p))

#define for_each_pipe_masked(__dev_priv, __p, __mask) \
	for_each_pipe(__dev_priv, __p) \
		for_each_if((__mask) & BIT(__p))

#define for_each_cpu_transcoder(__dev_priv, __t) \
	for ((__t) = 0; (__t) < I915_MAX_TRANSCODERS; (__t)++)	\
		for_each_if (DISPLAY_RUNTIME_INFO(__dev_priv)->cpu_transcoder_mask & BIT(__t))

#define for_each_cpu_transcoder_masked(__dev_priv, __t, __mask) \
	for_each_cpu_transcoder(__dev_priv, __t) \
		for_each_if ((__mask) & BIT(__t))

#define for_each_sprite(__dev_priv, __p, __s)				\
	for ((__s) = 0;							\
	     (__s) < DISPLAY_RUNTIME_INFO(__dev_priv)->num_sprites[(__p)];	\
	     (__s)++)

#define for_each_port(__port) \
	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++)

#define for_each_port_masked(__port, __ports_mask)			\
	for_each_port(__port)						\
		for_each_if((__ports_mask) & BIT(__port))

#define for_each_phy_masked(__phy, __phys_mask) \
	for ((__phy) = PHY_A; (__phy) < I915_MAX_PHYS; (__phy)++)	\
		for_each_if((__phys_mask) & BIT(__phy))

#define for_each_crtc(dev, crtc) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#define for_each_intel_plane(dev, intel_plane) \
	list_for_each_entry(intel_plane,			\
			    &(dev)->mode_config.plane_list,	\
			    base.head)

#define for_each_intel_plane_mask(dev, intel_plane, plane_mask)		\
	list_for_each_entry(intel_plane,				\
			    &(dev)->mode_config.plane_list,		\
			    base.head)					\
		for_each_if((plane_mask) &				\
			    drm_plane_mask(&intel_plane->base))

#define for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane)	\
	list_for_each_entry(intel_plane,				\
			    &(dev)->mode_config.plane_list,		\
			    base.head)					\
		for_each_if((intel_plane)->pipe == (intel_crtc)->pipe)

#define for_each_intel_crtc(dev, intel_crtc)				\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)

#define for_each_intel_crtc_in_pipe_mask(dev, intel_crtc, pipe_mask)	\
	list_for_each_entry(intel_crtc,					\
			    &(dev)->mode_config.crtc_list,		\
			    base.head)					\
		for_each_if((pipe_mask) & BIT(intel_crtc->pipe))

#define for_each_intel_crtc_in_pipe_mask_reverse(dev, intel_crtc, pipe_mask)	\
	list_for_each_entry_reverse((intel_crtc),				\
				    &(dev)->mode_config.crtc_list,		\
				    base.head)					\
		for_each_if((pipe_mask) & BIT((intel_crtc)->pipe))

#define for_each_intel_encoder(dev, intel_encoder)		\
	list_for_each_entry(intel_encoder,			\
			    &(dev)->mode_config.encoder_list,	\
			    base.head)

#define for_each_intel_encoder_mask(dev, intel_encoder, encoder_mask)	\
	list_for_each_entry(intel_encoder,				\
			    &(dev)->mode_config.encoder_list,		\
			    base.head)					\
		for_each_if((encoder_mask) &				\
			    drm_encoder_mask(&intel_encoder->base))

#define for_each_intel_encoder_mask_with_psr(dev, intel_encoder, encoder_mask) \
	list_for_each_entry((intel_encoder), &(dev)->mode_config.encoder_list, base.head) \
		for_each_if(((encoder_mask) & drm_encoder_mask(&(intel_encoder)->base)) && \
			    intel_encoder_can_psr(intel_encoder))

#define for_each_intel_dp(dev, intel_encoder)			\
	for_each_intel_encoder(dev, intel_encoder)		\
		for_each_if(intel_encoder_is_dp(intel_encoder))

#define for_each_intel_encoder_with_psr(dev, intel_encoder) \
	for_each_intel_encoder((dev), (intel_encoder)) \
		for_each_if(intel_encoder_can_psr(intel_encoder))

#define for_each_intel_connector_iter(intel_connector, iter) \
	while ((intel_connector = to_intel_connector(drm_connector_list_iter_next(iter))))

#define for_each_encoder_on_crtc(dev, __crtc, intel_encoder) \
	list_for_each_entry((intel_encoder), &(dev)->mode_config.encoder_list, base.head) \
		for_each_if((intel_encoder)->base.crtc == (__crtc))

#define for_each_old_intel_plane_in_state(__state, plane, old_plane_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_total_plane && \
		     ((plane) = to_intel_plane((__state)->base.planes[__i].ptr), \
		      (old_plane_state) = to_intel_plane_state((__state)->base.planes[__i].old_state), 1); \
	     (__i)++) \
		for_each_if(plane)

#define for_each_old_intel_crtc_in_state(__state, crtc, old_crtc_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_crtc && \
		     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
		      (old_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].old_state), 1); \
	     (__i)++) \
		for_each_if(crtc)

#define for_each_new_intel_plane_in_state(__state, plane, new_plane_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_total_plane && \
		     ((plane) = to_intel_plane((__state)->base.planes[__i].ptr), \
		      (new_plane_state) = to_intel_plane_state((__state)->base.planes[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(plane)

#define for_each_new_intel_crtc_in_state(__state, crtc, new_crtc_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_crtc && \
		     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
		      (new_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(crtc)

#define for_each_new_intel_crtc_in_state_reverse(__state, crtc, new_crtc_state, __i) \
	for ((__i) = (__state)->base.dev->mode_config.num_crtc - 1; \
	     (__i) >= 0  && \
	     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
	      (new_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].new_state), 1); \
	     (__i)--) \
		for_each_if(crtc)

#define for_each_oldnew_intel_plane_in_state(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_total_plane && \
		     ((plane) = to_intel_plane((__state)->base.planes[__i].ptr), \
		      (old_plane_state) = to_intel_plane_state((__state)->base.planes[__i].old_state), \
		      (new_plane_state) = to_intel_plane_state((__state)->base.planes[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(plane)

#define for_each_oldnew_intel_crtc_in_state(__state, crtc, old_crtc_state, new_crtc_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.dev->mode_config.num_crtc && \
		     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
		      (old_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].old_state), \
		      (new_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].new_state), 1); \
	     (__i)++) \
		for_each_if(crtc)

#define for_each_oldnew_intel_crtc_in_state_reverse(__state, crtc, old_crtc_state, new_crtc_state, __i) \
	for ((__i) = (__state)->base.dev->mode_config.num_crtc - 1; \
	     (__i) >= 0  && \
	     ((crtc) = to_intel_crtc((__state)->base.crtcs[__i].ptr), \
	      (old_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].old_state), \
	      (new_crtc_state) = to_intel_crtc_state((__state)->base.crtcs[__i].new_state), 1); \
	     (__i)--) \
		for_each_if(crtc)

#define intel_atomic_crtc_state_for_each_plane_state( \
		  plane, plane_state, \
		  crtc_state) \
	for_each_intel_plane_mask(((crtc_state)->uapi.state->dev), (plane), \
				((crtc_state)->uapi.plane_mask)) \
		for_each_if ((plane_state = \
			      to_intel_plane_state(__drm_atomic_get_current_plane_state((crtc_state)->uapi.state, &plane->base))))

#define for_each_new_intel_connector_in_state(__state, connector, new_connector_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->base.num_connector; \
	     (__i)++) \
		for_each_if ((__state)->base.connectors[__i].ptr && \
			     ((connector) = to_intel_connector((__state)->base.connectors[__i].ptr), \
			     (new_connector_state) = to_intel_digital_connector_state((__state)->base.connectors[__i].new_state), 1))

int intel_atomic_check(struct drm_device *dev, struct drm_atomic_state *state);
int intel_atomic_add_affected_planes(struct intel_atomic_state *state,
				     struct intel_crtc *crtc);
u8 intel_calc_active_pipes(struct intel_atomic_state *state,
			   u8 active_pipes);
void intel_link_compute_m_n(u16 bpp, int nlanes,
			    int pixel_clock, int link_clock,
			    int bw_overhead,
			    struct intel_link_m_n *m_n);
u32 intel_plane_fb_max_stride(struct drm_i915_private *dev_priv,
			      u32 pixel_format, u64 modifier);
enum drm_mode_status
intel_mode_valid_max_plane_size(struct drm_i915_private *dev_priv,
				const struct drm_display_mode *mode,
				bool joiner);
enum drm_mode_status
intel_cpu_transcoder_mode_valid(struct drm_i915_private *i915,
				const struct drm_display_mode *mode);
enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port);
bool is_trans_port_sync_mode(const struct intel_crtc_state *state);
bool is_trans_port_sync_master(const struct intel_crtc_state *state);
u8 intel_crtc_joined_pipe_mask(const struct intel_crtc_state *crtc_state);
bool intel_crtc_is_joiner_secondary(const struct intel_crtc_state *crtc_state);
bool intel_crtc_is_joiner_primary(const struct intel_crtc_state *crtc_state);
u8 intel_crtc_joiner_secondary_pipes(const struct intel_crtc_state *crtc_state);
struct intel_crtc *intel_primary_crtc(const struct intel_crtc_state *crtc_state);
bool intel_crtc_get_pipe_config(struct intel_crtc_state *crtc_state);
bool intel_pipe_config_compare(const struct intel_crtc_state *current_config,
			       const struct intel_crtc_state *pipe_config,
			       bool fastset);

void intel_plane_destroy(struct drm_plane *plane);
void i9xx_set_pipeconf(const struct intel_crtc_state *crtc_state);
void ilk_set_pipeconf(const struct intel_crtc_state *crtc_state);
void intel_enable_transcoder(const struct intel_crtc_state *new_crtc_state);
void intel_disable_transcoder(const struct intel_crtc_state *old_crtc_state);
void i830_enable_pipe(struct drm_i915_private *dev_priv, enum pipe pipe);
void i830_disable_pipe(struct drm_i915_private *dev_priv, enum pipe pipe);
int vlv_get_hpll_vco(struct drm_i915_private *dev_priv);
int vlv_get_cck_clock(struct drm_i915_private *dev_priv,
		      const char *name, u32 reg, int ref_freq);
int vlv_get_cck_clock_hpll(struct drm_i915_private *dev_priv,
			   const char *name, u32 reg);
void intel_init_display_hooks(struct drm_i915_private *dev_priv);
unsigned int intel_fb_xy_to_linear(int x, int y,
				   const struct intel_plane_state *state,
				   int plane);
void intel_add_fb_offsets(int *x, int *y,
			  const struct intel_plane_state *state, int plane);
unsigned int intel_rotation_info_size(const struct intel_rotation_info *rot_info);
unsigned int intel_remapped_info_size(const struct intel_remapped_info *rem_info);
bool intel_has_pending_fb_unpin(struct drm_i915_private *dev_priv);
void intel_encoder_destroy(struct drm_encoder *encoder);
struct drm_display_mode *
intel_encoder_current_mode(struct intel_encoder *encoder);
void intel_encoder_get_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *crtc_state);
bool intel_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy);
bool intel_phy_is_tc(struct drm_i915_private *dev_priv, enum phy phy);
bool intel_phy_is_snps(struct drm_i915_private *dev_priv, enum phy phy);
enum tc_port intel_port_to_tc(struct drm_i915_private *dev_priv,
			      enum port port);

enum phy intel_encoder_to_phy(struct intel_encoder *encoder);
bool intel_encoder_is_combo(struct intel_encoder *encoder);
bool intel_encoder_is_snps(struct intel_encoder *encoder);
bool intel_encoder_is_tc(struct intel_encoder *encoder);
enum tc_port intel_encoder_to_tc(struct intel_encoder *encoder);

int intel_get_pipe_from_crtc_id_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);

int ilk_get_lanes_required(int target_clock, int link_bw, int bpp);
void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
			 struct intel_digital_port *dig_port,
			 unsigned int expected_mask);
struct drm_framebuffer *
intel_framebuffer_create(struct drm_i915_gem_object *obj,
			 struct drm_mode_fb_cmd2 *mode_cmd);

bool intel_fuzzy_clock_check(int clock1, int clock2);

void intel_zero_m_n(struct intel_link_m_n *m_n);
void intel_set_m_n(struct drm_i915_private *i915,
		   const struct intel_link_m_n *m_n,
		   i915_reg_t data_m_reg, i915_reg_t data_n_reg,
		   i915_reg_t link_m_reg, i915_reg_t link_n_reg);
void intel_get_m_n(struct drm_i915_private *i915,
		   struct intel_link_m_n *m_n,
		   i915_reg_t data_m_reg, i915_reg_t data_n_reg,
		   i915_reg_t link_m_reg, i915_reg_t link_n_reg);
bool intel_cpu_transcoder_has_m2_n2(struct drm_i915_private *dev_priv,
				    enum transcoder transcoder);
void intel_cpu_transcoder_set_m1_n1(struct intel_crtc *crtc,
				    enum transcoder cpu_transcoder,
				    const struct intel_link_m_n *m_n);
void intel_cpu_transcoder_set_m2_n2(struct intel_crtc *crtc,
				    enum transcoder cpu_transcoder,
				    const struct intel_link_m_n *m_n);
void intel_cpu_transcoder_get_m1_n1(struct intel_crtc *crtc,
				    enum transcoder cpu_transcoder,
				    struct intel_link_m_n *m_n);
void intel_cpu_transcoder_get_m2_n2(struct intel_crtc *crtc,
				    enum transcoder cpu_transcoder,
				    struct intel_link_m_n *m_n);
int intel_dotclock_calculate(int link_freq, const struct intel_link_m_n *m_n);
int intel_crtc_dotclock(const struct intel_crtc_state *pipe_config);
enum intel_display_power_domain intel_port_to_power_domain(struct intel_digital_port *dig_port);
enum intel_display_power_domain
intel_aux_power_domain(struct intel_digital_port *dig_port);
void intel_crtc_arm_fifo_underrun(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state);
void ilk_pfit_disable(const struct intel_crtc_state *old_crtc_state);

int bdw_get_pipe_misc_bpp(struct intel_crtc *crtc);
unsigned int intel_plane_fence_y_offset(const struct intel_plane_state *plane_state);

bool intel_plane_uses_fence(const struct intel_plane_state *plane_state);

struct intel_encoder *
intel_get_crtc_new_encoder(const struct intel_atomic_state *state,
			   const struct intel_crtc_state *crtc_state);
void intel_plane_disable_noatomic(struct intel_crtc *crtc,
				  struct intel_plane *plane);
void intel_set_plane_visible(struct intel_crtc_state *crtc_state,
			     struct intel_plane_state *plane_state,
			     bool visible);
void intel_plane_fixup_bitmasks(struct intel_crtc_state *crtc_state);

void intel_update_watermarks(struct drm_i915_private *i915);

bool intel_crtc_vrr_disabling(struct intel_atomic_state *state,
			      struct intel_crtc *crtc);

/* modesetting */
int intel_modeset_pipes_in_mask_early(struct intel_atomic_state *state,
				      const char *reason, u8 pipe_mask);
int intel_modeset_all_pipes_late(struct intel_atomic_state *state,
				 const char *reason);
int intel_modeset_commit_pipes(struct drm_i915_private *i915,
			       u8 pipe_mask,
			       struct drm_modeset_acquire_ctx *ctx);
void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state,
					  struct intel_power_domain_mask *old_domains);
void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc,
					  struct intel_power_domain_mask *domains);

/* interface for intel_display_driver.c */
void intel_setup_outputs(struct drm_i915_private *i915);
int intel_initial_commit(struct drm_device *dev);
void intel_panel_sanitize_ssc(struct drm_i915_private *i915);
void intel_update_czclk(struct drm_i915_private *i915);
void intel_atomic_helper_free_state_worker(struct work_struct *work);
enum drm_mode_status intel_mode_valid(struct drm_device *dev,
				      const struct drm_display_mode *mode);
int intel_atomic_commit(struct drm_device *dev, struct drm_atomic_state *_state,
			bool nonblock);

void intel_hpd_poll_fini(struct drm_i915_private *i915);

/* modesetting asserts */
void assert_transcoder(struct drm_i915_private *dev_priv,
		       enum transcoder cpu_transcoder, bool state);
#define assert_transcoder_enabled(d, t) assert_transcoder(d, t, true)
#define assert_transcoder_disabled(d, t) assert_transcoder(d, t, false)

bool assert_port_valid(struct drm_i915_private *i915, enum port port);

/*
 * Use I915_STATE_WARN(x) (rather than WARN() and WARN_ON()) for hw state sanity
 * checks to check for unexpected conditions which may not necessarily be a user
 * visible problem. This will either WARN() or DRM_ERROR() depending on the
 * verbose_state_checks module param, to enable distros and users to tailor
 * their preferred amount of i915 abrt spam.
 */
#define I915_STATE_WARN(__i915, condition, format...) ({		\
	struct drm_device *drm = &(__i915)->drm;			\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		if (!drm_WARN(drm, __i915->display.params.verbose_state_checks, format)) \
			drm_err(drm, format);				\
	unlikely(__ret_warn_on);					\
})

bool intel_scanout_needs_vtd_wa(struct drm_i915_private *i915);

#endif
