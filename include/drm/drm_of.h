#ifndef __DRM_OF_H__
#define __DRM_OF_H__

struct component_master_ops;
struct device;
struct drm_device;
struct device_node;

#ifdef CONFIG_OF
extern uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
					   struct device_node *port);
extern int drm_of_component_probe(struct device *dev,
				  int (*compare_of)(struct device *, void *),
				  const struct component_master_ops *m_ops);
#else
static inline uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
						  struct device_node *port)
{
	return 0;
}

static inline int
drm_of_component_probe(struct device *dev,
		       int (*compare_of)(struct device *, void *),
		       const struct component_master_ops *m_ops)
{
	return -EINVAL;
}
#endif

#endif /* __DRM_OF_H__ */
