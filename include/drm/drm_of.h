#ifndef __DRM_OF_H__
#define __DRM_OF_H__

#include <linux/of_graph.h>

struct component_master_ops;
struct component_match;
struct device;
struct drm_device;
struct drm_encoder;
struct device_node;

#ifdef CONFIG_OF
extern uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
					   struct device_node *port);
extern void drm_of_component_match_add(struct device *master,
				       struct component_match **matchptr,
				       int (*compare)(struct device *, void *),
				       struct device_node *node);
extern int drm_of_component_probe(struct device *dev,
				  int (*compare_of)(struct device *, void *),
				  const struct component_master_ops *m_ops);
extern int drm_of_encoder_active_endpoint(struct device_node *node,
					  struct drm_encoder *encoder,
					  struct of_endpoint *endpoint);
#else
static inline uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
						  struct device_node *port)
{
	return 0;
}

static void drm_of_component_match_add(struct device *master,
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
#endif

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
