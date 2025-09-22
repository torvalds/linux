/* Public domain. */

#ifndef _DRM_LEASE_H
#define _DRM_LEASE_H

struct drm_master;

#define drm_lease_held(f, id)		(true)
#define drm_lease_filter_crtcs(f, in)	(in)

static inline void
drm_lease_revoke(struct drm_master *m)
{
}

static inline void
drm_lease_destroy(struct drm_master *m)
{
}

#endif
