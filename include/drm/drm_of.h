/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRM_OF_H__
#define __DRM_OF_H__

#include <linux/of_graph.h>
#if IS_ENABLED(CONFIG_OF) && IS_ENABLED(CONFIG_DRM_PANEL_BRIDGE)
#include <drm/drm_bridge.h>
#endif

struct component_master_ops;
struct component_match;
struct device;
struct drm_device;
struct drm_encoder;
struct drm_panel;
struct drm_bridge;
struct device_node;

#ifdef CONFIG_OF
uint32_t drm_of_crtc_port_mask(struct drm_device *dev,
			    struct device_node *port);
uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
				    struct device_node *port);
void drm_of_component_match_add(struct device *master,
				struct component_match **matchptr,
				int (*compare)(struct device *, void *),
				struct device_node *node);
int drm_of_component_probe(struct device *dev,
			   int (*compare_of)(struct device *, void *),
			   const struct component_master_ops *m_ops);
int drm_of_encoder_active_endpoint(struct device_node *node,
				   struct drm_encoder *encoder,
				   struct of_endpoint *endpoint);
int drm_of_find_panel_or_bridge(const struct device_node *np,
				int port, int endpoint,
				struct drm_panel **panel,
				struct drm_bridge **bridge);
#else
static inline uint32_t drm_of_crtc_port_mask(struct drm_device *dev,
					  struct device_node *port)
{
	return 0;
}

static inline uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
						  struct device_node *port)
{
	return 0;
}

static inline void
drm_of_component_match_add(struct device *master,
			   struct component_match **matchptr,
			   int (*compare)(struct device *, void *),
			   struct device_node *node)
{
}

static inline int
drm_of_component_probe(struct device *dev,
		       int (*compare_of)(struct device *, void *),
		       const struct component_master_ops *m_ops)
{
	return -EINVAL;
}

static inline int drm_of_encoder_active_endpoint(struct device_node *node,
						 struct drm_encoder *encoder,
						 struct of_endpoint *endpoint)
{
	return -EINVAL;
}
static inline int drm_of_find_panel_or_bridge(const struct device_node *np,
					      int port, int endpoint,
					      struct drm_panel **panel,
					      struct drm_bridge **bridge)
{
	return -EINVAL;
}
#endif

/*
 * drm_of_panel_bridge_remove - remove panel bridge
 * @np: device tree node containing panel bridge output ports
 *
 * Remove the panel bridge of a given DT node's port and endpoint number
 *
 * Returns zero if successful, or one of the standard error codes if it fails.
 */
static inline int drm_of_panel_bridge_remove(const struct device_node *np,
					     int port, int endpoint)
{
#if IS_ENABLED(CONFIG_OF) && IS_ENABLED(CONFIG_DRM_PANEL_BRIDGE)
	struct drm_bridge *bridge;
	struct device_node *remote;

	remote = of_graph_get_remote_node(np, port, endpoint);
	if (!remote)
		return -ENODEV;

	bridge = of_drm_find_bridge(remote);
	drm_panel_bridge_remove(bridge);

	return 0;
#else
	return -EINVAL;
#endif
}

static inline int drm_of_encoder_active_endpoint_id(struct device_node *node,
						    struct drm_encoder *encoder)
{
	struct of_endpoint endpoint;
	int ret = drm_of_encoder_active_endpoint(node, encoder,
						 &endpoint);

	return ret ?: endpoint.id;
}

static inline int drm_of_encoder_active_port_id(struct device_node *node,
						struct drm_encoder *encoder)
{
	struct of_endpoint endpoint;
	int ret = drm_of_encoder_active_endpoint(node, encoder,
						 &endpoint);

	return ret ?: endpoint.port;
}

#endif /* __DRM_OF_H__ */
