/* SPDX-License-Identifier: GPL-2.0 or MIT */

#ifndef __DRM_DRM_PANIC_HELPER_H_
#define __DRM_DRM_PANIC_HELPER_H_

#include <linux/types.h>

enum drm_panic_type;

struct drm_plane;
struct drm_scanout_buffer;

/* drm_panic_helper.c */
int drm_plane_helper_display_panic_screen(struct drm_plane *plane,
					  const char *description,
					  enum drm_panic_type panic_type,
					  u32 fg_color, u32 bg_color,
					  unsigned int qr_version);
#if IS_ENABLED(CONFIG_KUNIT)
int drm_panic_helper_draw_screen_user(struct drm_scanout_buffer *sb, u32 fg_color, u32 bg_color);
int drm_panic_helper_draw_screen_kmsg(struct drm_scanout_buffer *sb, u32 fg_color, u32 bg_color);
int drm_panic_helper_draw_screen_qr_code(struct drm_scanout_buffer *sb,
					 u32 fg_color, u32 bg_color, unsigned int qr_version);
void drm_panic_helper_set_description(const char *description);
void drm_panic_helper_clear_description(void);
#endif

/* drm_panic_helper_qr.rs */
size_t drm_panic_helper_qr_max_data_size(u8 version, size_t url_len);
u8 drm_panic_helper_qr_generate(const char *url, u8 *data, size_t data_len, size_t data_size,
				u8 *tmp, size_t tmp_size);

#if IS_ENABLED(CONFIG_DRM_PANIC)
#define DRM_PANIC_PLANE_FUNCS \
	.display_panic_screen = drm_plane_helper_display_panic_screen
#else
#define DRM_PANIC_PLANE_FUNCS \
	.display_panic_screen = NULL
#endif

#endif
