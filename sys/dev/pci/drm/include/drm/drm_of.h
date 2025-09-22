/* Public domain. */

#ifndef _DRM_DRM_OF_H
#define _DRM_DRM_OF_H

struct component_match;

void drm_of_component_match_add(struct device *, struct component_match **,
    int (*)(struct device *, void *), struct device_node *);

#endif
