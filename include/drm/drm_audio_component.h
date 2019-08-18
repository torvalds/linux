// SPDX-License-Identifier: MIT
// Copyright © 2014 Intel Corporation

#ifndef _DRM_AUDIO_COMPONENT_H_
#define _DRM_AUDIO_COMPONENT_H_

struct drm_audio_component;
struct device;

/**
 * struct drm_audio_component_ops - Ops implemented by DRM driver, called by hda driver
 */
struct drm_audio_component_ops {
	/**
	 * @owner: drm module to pin down
	 */
	struct module *owner;
	/**
	 * @get_power: get the POWER_DOMAIN_AUDIO power well
	 *
	 * Request the power well to be turned on.
	 */
	void (*get_power)(struct device *);
	/**
	 * @put_power: put the POWER_DOMAIN_AUDIO power well
	 *
	 * Allow the power well to be turned off.
	 */
	void (*put_power)(struct device *);
	/**
	 * @codec_wake_override: Enable/disable codec wake signal
	 */
	void (*codec_wake_override)(struct device *, bool enable);
	/**
	 * @get_cdclk_freq: Get the Core Display Clock in kHz
	 */
	int (*get_cdclk_freq)(struct device *);
	/**
	 * @sync_audio_rate: set n/cts based on the sample rate
	 *
	 * Called from audio driver. After audio driver sets the
	 * sample rate, it will call this function to set n/cts
	 */
	int (*sync_audio_rate)(struct device *, int port, int pipe, int rate);
	/**
	 * @get_eld: fill the audio state and ELD bytes for the given port
	 *
	 * Called from audio driver to get the HDMI/DP audio state of the given
	 * digital port, and also fetch ELD bytes to the given pointer.
	 *
	 * It returns the byte size of the original ELD (not the actually
	 * copied size), zero for an invalid ELD, or a negative error code.
	 *
	 * Note that the returned size may be over @max_bytes.  Then it
	 * implies that only a part of ELD has been copied to the buffer.
	 */
	int (*get_eld)(struct device *, int port, int pipe, bool *enabled,
		       unsigned char *buf, int max_bytes);
};

/**
 * struct drm_audio_component_audio_ops - Ops implemented by hda driver, called by DRM driver
 */
struct drm_audio_component_audio_ops {
	/**
	 * @audio_ptr: Pointer to be used in call to pin_eld_notify
	 */
	void *audio_ptr;
	/**
	 * @pin_eld_notify: Notify the HDA driver that pin sense and/or ELD information has changed
	 *
	 * Called when the DRM driver has set up audio pipeline or has just
	 * begun to tear it down. This allows the HDA driver to update its
	 * status accordingly (even when the HDA controller is in power save
	 * mode).
	 */
	void (*pin_eld_notify)(void *audio_ptr, int port, int pipe);
	/**
	 * @pin2port: Check and convert from pin node to port number
	 *
	 * Called by HDA driver to check and convert from the pin widget node
	 * number to a port number in the graphics side.
	 */
	int (*pin2port)(void *audio_ptr, int pin);
	/**
	 * @master_bind: (Optional) component master bind callback
	 *
	 * Called at binding master component, for HDA codec-specific
	 * handling of dynamic binding.
	 */
	int (*master_bind)(struct device *dev, struct drm_audio_component *);
	/**
	 * @master_unbind: (Optional) component master unbind callback
	 *
	 * Called at unbinding master component, for HDA codec-specific
	 * handling of dynamic unbinding.
	 */
	void (*master_unbind)(struct device *dev, struct drm_audio_component *);
};

/**
 * struct drm_audio_component - Used for direct communication between DRM and hda drivers
 */
struct drm_audio_component {
	/**
	 * @dev: DRM device, used as parameter for ops
	 */
	struct device *dev;
	/**
	 * @ops: Ops implemented by DRM driver, called by hda driver
	 */
	const struct drm_audio_component_ops *ops;
	/**
	 * @audio_ops: Ops implemented by hda driver, called by DRM driver
	 */
	const struct drm_audio_component_audio_ops *audio_ops;
};

#endif /* _DRM_AUDIO_COMPONENT_H_ */
