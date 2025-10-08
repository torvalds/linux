/* SPDX-License-Identifier: MIT */

#ifndef DRM_DISPLAY_HDMI_CEC_HELPER
#define DRM_DISPLAY_HDMI_CEC_HELPER

#include <linux/types.h>

struct drm_connector;

struct cec_msg;
struct device;

struct drm_connector_hdmi_cec_funcs {
	/**
	 * @init: perform hardware-specific initialization before registering the CEC adapter
	 */
	int (*init)(struct drm_connector *connector);

	/**
	 * @uninit: perform hardware-specific teardown for the CEC adapter
	 */
	void (*uninit)(struct drm_connector *connector);

	/**
	 * @enable: enable or disable CEC adapter
	 */
	int (*enable)(struct drm_connector *connector, bool enable);

	/**
	 * @log_addr: set adapter's logical address, can be called multiple
	 * times if adapter supports several LAs
	 */
	int (*log_addr)(struct drm_connector *connector, u8 logical_addr);

	/**
	 * @transmit: start transmission of the specified CEC message
	 */
	int (*transmit)(struct drm_connector *connector, u8 attempts,
			u32 signal_free_time, struct cec_msg *msg);
};

int drmm_connector_hdmi_cec_register(struct drm_connector *connector,
				     const struct drm_connector_hdmi_cec_funcs *funcs,
				     const char *name,
				     u8 available_las,
				     struct device *dev);

void drm_connector_hdmi_cec_received_msg(struct drm_connector *connector,
					 struct cec_msg *msg);

void drm_connector_hdmi_cec_transmit_done(struct drm_connector *connector,
					  u8 status,
					  u8 arb_lost_cnt, u8 nack_cnt,
					  u8 low_drive_cnt, u8 error_cnt);

void drm_connector_hdmi_cec_transmit_attempt_done(struct drm_connector *connector,
						  u8 status);

#if IS_ENABLED(CONFIG_DRM_DISPLAY_HDMI_CEC_NOTIFIER_HELPER)
int drmm_connector_hdmi_cec_notifier_register(struct drm_connector *connector,
					      const char *port_name,
					      struct device *dev);
#else
static inline int drmm_connector_hdmi_cec_notifier_register(struct drm_connector *connector,
							    const char *port_name,
							    struct device *dev)
{
	return 0;
}
#endif

#endif
