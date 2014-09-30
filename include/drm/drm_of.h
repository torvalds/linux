#ifndef __DRM_OF_H__
#define __DRM_OF_H__

struct drm_device;
struct device_node;

#ifdef CONFIG_OF
extern uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
					   struct device_node *port);
#else
static inline uint32_t drm_of_find_possible_crtcs(struct drm_device *dev,
						  struct device_node *port)
{
	return 0;
}
#endif

#endif /* __DRM_OF_H__ */
