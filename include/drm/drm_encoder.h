/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_ENCODER_H__
#define __DRM_ENCODER_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_modeset.h>

/**
 * struct drm_encoder_funcs - encoder controls
 *
 * Encoders sit between CRTCs and connectors.
 */
struct drm_encoder_funcs {
	/**
	 * @reset:
	 *
	 * Reset encoder hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 */
	void (*reset)(struct drm_encoder *encoder);

	/**
	 * @destroy:
	 *
	 * Clean up encoder resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since an encoder cannot be
	 * hotplugged in DRM.
	 */
	void (*destroy)(struct drm_encoder *encoder);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the encoder like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_encoder *encoder);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the encoder from
	 * late_unregister(). It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_encoder *encoder);
};

/**
 * struct drm_encoder - central DRM encoder structure
 * @dev: parent DRM device
 * @head: list management
 * @base: base KMS object
 * @name: human readable name, can be overwritten by the driver
 * @crtc: currently bound CRTC
 * @bridge: bridge associated to the encoder
 * @funcs: control functions
 * @helper_private: mid-layer private data
 *
 * CRTCs drive pixels to encoders, which convert them into signals
 * appropriate for a given connector or set of connectors.
 */
struct drm_encoder {
	struct drm_device *dev;
	struct list_head head;

	struct drm_mode_object base;
	char *name;
	/**
	 * @encoder_type:
	 *
	 * One of the DRM_MODE_ENCODER_<foo> types in drm_mode.h. The following
	 * encoder types are defined thus far:
	 *
	 * - DRM_MODE_ENCODER_DAC for VGA and analog on DVI-I/DVI-A.
	 *
	 * - DRM_MODE_ENCODER_TMDS for DVI, HDMI and (embedded) DisplayPort.
	 *
	 * - DRM_MODE_ENCODER_LVDS for display panels, or in general any panel
	 *   with a proprietary parallel connector.
	 *
	 * - DRM_MODE_ENCODER_TVDAC for TV output (Composite, S-Video,
	 *   Component, SCART).
	 *
	 * - DRM_MODE_ENCODER_VIRTUAL for virtual machine displays
	 *
	 * - DRM_MODE_ENCODER_DSI for panels connected using the DSI serial bus.
	 *
	 * - DRM_MODE_ENCODER_DPI for panels connected using the DPI parallel
	 *   bus.
	 *
	 * - DRM_MODE_ENCODER_DPMST for special fake encoders used to allow
	 *   mutliple DP MST streams to share one physical encoder.
	 */
	int encoder_type;

	/**
	 * @index: Position inside the mode_config.list, can be used as an array
	 * index. It is invariant over the lifetime of the encoder.
	 */
	unsigned index;

	/**
	 * @possible_crtcs: Bitmask of potential CRTC bindings, using
	 * drm_crtc_index() as the index into the bitfield. The driver must set
	 * the bits for all &drm_crtc objects this encoder can be connected to
	 * before calling drm_encoder_init().
	 *
	 * In reality almost every driver gets this wrong.
	 *
	 * Note that since CRTC objects can't be hotplugged the assigned indices
	 * are stable and hence known before registering all objects.
	 */
	uint32_t possible_crtcs;

	/**
	 * @possible_clones: Bitmask of potential sibling encoders for cloning,
	 * using drm_encoder_index() as the index into the bitfield. The driver
	 * must set the bits for all &drm_encoder objects which can clone a
	 * &drm_crtc together with this encoder before calling
	 * drm_encoder_init(). Drivers should set the bit representing the
	 * encoder itself, too. Cloning bits should be set such that when two
	 * encoders can be used in a cloned configuration, they both should have
	 * each another bits set.
	 *
	 * In reality almost every driver gets this wrong.
	 *
	 * Note that since encoder objects can't be hotplugged the assigned indices
	 * are stable and hence known before registering all objects.
	 */
	uint32_t possible_clones;

	struct drm_crtc *crtc;
	struct drm_bridge *bridge;
	const struct drm_encoder_funcs *funcs;
	const struct drm_encoder_helper_funcs *helper_private;
};

#define obj_to_encoder(x) container_of(x, struct drm_encoder, base)

__printf(5, 6)
int drm_encoder_init(struct drm_device *dev,
		     struct drm_encoder *encoder,
		     const struct drm_encoder_funcs *funcs,
		     int encoder_type, const char *name, ...);

/**
 * drm_encoder_index - find the index of a registered encoder
 * @encoder: encoder to find index for
 *
 * Given a registered encoder, return the index of that encoder within a DRM
 * device's list of encoders.
 */
static inline unsigned int drm_encoder_index(struct drm_encoder *encoder)
{
	return encoder->index;
}

/* FIXME: We have an include file mess still, drm_crtc.h needs untangling. */
static inline uint32_t drm_crtc_mask(struct drm_crtc *crtc);

/**
 * drm_encoder_crtc_ok - can a given crtc drive a given encoder?
 * @encoder: encoder to test
 * @crtc: crtc to test
 *
 * Returns false if @encoder can't be driven by @crtc, true otherwise.
 */
static inline bool drm_encoder_crtc_ok(struct drm_encoder *encoder,
				       struct drm_crtc *crtc)
{
	return !!(encoder->possible_crtcs & drm_crtc_mask(crtc));
}

/**
 * drm_encoder_find - find a &drm_encoder
 * @dev: DRM device
 * @id: encoder id
 *
 * Returns the encoder with @id, NULL if it doesn't exist. Simple wrapper around
 * drm_mode_object_find().
 */
static inline struct drm_encoder *drm_encoder_find(struct drm_device *dev,
						   uint32_t id)
{
	struct drm_mode_object *mo;

	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);

	return mo ? obj_to_encoder(mo) : NULL;
}

void drm_encoder_cleanup(struct drm_encoder *encoder);

#endif
