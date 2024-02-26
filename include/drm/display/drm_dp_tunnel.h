/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __DRM_DP_TUNNEL_H__
#define __DRM_DP_TUNNEL_H__

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>

struct drm_dp_aux;

struct drm_device;

struct drm_atomic_state;
struct drm_dp_tunnel_mgr;
struct drm_dp_tunnel_state;

struct ref_tracker;

struct drm_dp_tunnel_ref {
	struct drm_dp_tunnel *tunnel;
	struct ref_tracker *tracker;
};

#ifdef CONFIG_DRM_DISPLAY_DP_TUNNEL

struct drm_dp_tunnel *
drm_dp_tunnel_get(struct drm_dp_tunnel *tunnel, struct ref_tracker **tracker);

void
drm_dp_tunnel_put(struct drm_dp_tunnel *tunnel, struct ref_tracker **tracker);

static inline void drm_dp_tunnel_ref_get(struct drm_dp_tunnel *tunnel,
					 struct drm_dp_tunnel_ref *tunnel_ref)
{
	tunnel_ref->tunnel = drm_dp_tunnel_get(tunnel, &tunnel_ref->tracker);
}

static inline void drm_dp_tunnel_ref_put(struct drm_dp_tunnel_ref *tunnel_ref)
{
	drm_dp_tunnel_put(tunnel_ref->tunnel, &tunnel_ref->tracker);
	tunnel_ref->tunnel = NULL;
}

struct drm_dp_tunnel *
drm_dp_tunnel_detect(struct drm_dp_tunnel_mgr *mgr,
		     struct drm_dp_aux *aux);
int drm_dp_tunnel_destroy(struct drm_dp_tunnel *tunnel);

int drm_dp_tunnel_enable_bw_alloc(struct drm_dp_tunnel *tunnel);
int drm_dp_tunnel_disable_bw_alloc(struct drm_dp_tunnel *tunnel);
bool drm_dp_tunnel_bw_alloc_is_enabled(const struct drm_dp_tunnel *tunnel);
int drm_dp_tunnel_alloc_bw(struct drm_dp_tunnel *tunnel, int bw);
int drm_dp_tunnel_get_allocated_bw(struct drm_dp_tunnel *tunnel);
int drm_dp_tunnel_update_state(struct drm_dp_tunnel *tunnel);

void drm_dp_tunnel_set_io_error(struct drm_dp_tunnel *tunnel);

int drm_dp_tunnel_handle_irq(struct drm_dp_tunnel_mgr *mgr,
			     struct drm_dp_aux *aux);

int drm_dp_tunnel_max_dprx_rate(const struct drm_dp_tunnel *tunnel);
int drm_dp_tunnel_max_dprx_lane_count(const struct drm_dp_tunnel *tunnel);
int drm_dp_tunnel_available_bw(const struct drm_dp_tunnel *tunnel);

const char *drm_dp_tunnel_name(const struct drm_dp_tunnel *tunnel);

struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_state(struct drm_atomic_state *state,
			       struct drm_dp_tunnel *tunnel);

struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_old_state(struct drm_atomic_state *state,
				   const struct drm_dp_tunnel *tunnel);

struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_new_state(struct drm_atomic_state *state,
				   const struct drm_dp_tunnel *tunnel);

int drm_dp_tunnel_atomic_set_stream_bw(struct drm_atomic_state *state,
				       struct drm_dp_tunnel *tunnel,
				       u8 stream_id, int bw);
int drm_dp_tunnel_atomic_get_group_streams_in_state(struct drm_atomic_state *state,
						    const struct drm_dp_tunnel *tunnel,
						    u32 *stream_mask);

int drm_dp_tunnel_atomic_check_stream_bws(struct drm_atomic_state *state,
					  u32 *failed_stream_mask);

int drm_dp_tunnel_atomic_get_required_bw(const struct drm_dp_tunnel_state *tunnel_state);

struct drm_dp_tunnel_mgr *
drm_dp_tunnel_mgr_create(struct drm_device *dev, int max_group_count);
void drm_dp_tunnel_mgr_destroy(struct drm_dp_tunnel_mgr *mgr);

#else

static inline struct drm_dp_tunnel *
drm_dp_tunnel_get(struct drm_dp_tunnel *tunnel, struct ref_tracker **tracker)
{
	return NULL;
}

static inline void
drm_dp_tunnel_put(struct drm_dp_tunnel *tunnel, struct ref_tracker **tracker) {}

static inline void drm_dp_tunnel_ref_get(struct drm_dp_tunnel *tunnel,
					 struct drm_dp_tunnel_ref *tunnel_ref) {}

static inline void drm_dp_tunnel_ref_put(struct drm_dp_tunnel_ref *tunnel_ref) {}

static inline struct drm_dp_tunnel *
drm_dp_tunnel_detect(struct drm_dp_tunnel_mgr *mgr,
		     struct drm_dp_aux *aux)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int
drm_dp_tunnel_destroy(struct drm_dp_tunnel *tunnel)
{
	return 0;
}

static inline int drm_dp_tunnel_enable_bw_alloc(struct drm_dp_tunnel *tunnel)
{
	return -EOPNOTSUPP;
}

static inline int drm_dp_tunnel_disable_bw_alloc(struct drm_dp_tunnel *tunnel)
{
	return -EOPNOTSUPP;
}

static inline bool drm_dp_tunnel_bw_alloc_is_enabled(const struct drm_dp_tunnel *tunnel)
{
	return false;
}

static inline int
drm_dp_tunnel_alloc_bw(struct drm_dp_tunnel *tunnel, int bw)
{
	return -EOPNOTSUPP;
}

static inline int
drm_dp_tunnel_get_allocated_bw(struct drm_dp_tunnel *tunnel)
{
	return -1;
}

static inline int
drm_dp_tunnel_update_state(struct drm_dp_tunnel *tunnel)
{
	return -EOPNOTSUPP;
}

static inline void drm_dp_tunnel_set_io_error(struct drm_dp_tunnel *tunnel) {}

static inline int
drm_dp_tunnel_handle_irq(struct drm_dp_tunnel_mgr *mgr,
			 struct drm_dp_aux *aux)
{
	return -EOPNOTSUPP;
}

static inline int
drm_dp_tunnel_max_dprx_rate(const struct drm_dp_tunnel *tunnel)
{
	return 0;
}

static inline int
drm_dp_tunnel_max_dprx_lane_count(const struct drm_dp_tunnel *tunnel)
{
	return 0;
}

static inline int
drm_dp_tunnel_available_bw(const struct drm_dp_tunnel *tunnel)
{
	return -1;
}

static inline const char *
drm_dp_tunnel_name(const struct drm_dp_tunnel *tunnel)
{
	return NULL;
}

static inline struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_state(struct drm_atomic_state *state,
			       struct drm_dp_tunnel *tunnel)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct drm_dp_tunnel_state *
drm_dp_tunnel_atomic_get_new_state(struct drm_atomic_state *state,
				   const struct drm_dp_tunnel *tunnel)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int
drm_dp_tunnel_atomic_set_stream_bw(struct drm_atomic_state *state,
				   struct drm_dp_tunnel *tunnel,
				   u8 stream_id, int bw)
{
	return -EOPNOTSUPP;
}

static inline int
drm_dp_tunnel_atomic_get_group_streams_in_state(struct drm_atomic_state *state,
						const struct drm_dp_tunnel *tunnel,
						u32 *stream_mask)
{
	return -EOPNOTSUPP;
}

static inline int
drm_dp_tunnel_atomic_check_stream_bws(struct drm_atomic_state *state,
				      u32 *failed_stream_mask)
{
	return -EOPNOTSUPP;
}

static inline int
drm_dp_tunnel_atomic_get_required_bw(const struct drm_dp_tunnel_state *tunnel_state)
{
	return 0;
}

static inline struct drm_dp_tunnel_mgr *
drm_dp_tunnel_mgr_create(struct drm_device *dev, int max_group_count)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline
void drm_dp_tunnel_mgr_destroy(struct drm_dp_tunnel_mgr *mgr) {}

#endif /* CONFIG_DRM_DISPLAY_DP_TUNNEL */

#endif /* __DRM_DP_TUNNEL_H__ */
