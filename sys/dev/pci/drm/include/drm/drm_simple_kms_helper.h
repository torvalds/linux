/* Public domain. */

#ifndef _DRM_DRM_SIMPLE_KMS_HELPER_H
#define _DRM_DRM_SIMPLE_KMS_HELPER_H

static inline void *
__drmm_simple_encoder_alloc(struct drm_device *dev, size_t size,
			   size_t offset, int type)
{
	return __drmm_encoder_alloc(dev, size, offset, NULL, type, NULL);
}

#define drmm_simple_encoder_alloc(dev, type, member, encoder_type)	\
    ((type *) __drmm_simple_encoder_alloc(dev, sizeof(type), 		\
        offsetof(type, member), encoder_type))

#endif
