/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_TINYDRM_HELPERS_H
#define __LINUX_TINYDRM_HELPERS_H

struct backlight_device;
struct drm_device;
struct drm_display_mode;
struct drm_framebuffer;
struct drm_rect;
struct drm_simple_display_pipe;
struct drm_simple_display_pipe_funcs;
struct spi_transfer;
struct spi_device;
struct device;

/**
 * tinydrm_machine_little_endian - Machine is little endian
 *
 * Returns:
 * true if *defined(__LITTLE_ENDIAN)*, false otherwise
 */
static inline bool tinydrm_machine_little_endian(void)
{
#if defined(__LITTLE_ENDIAN)
	return true;
#else
	return false;
#endif
}

int tinydrm_display_pipe_init(struct drm_device *drm,
			      struct drm_simple_display_pipe *pipe,
			      const struct drm_simple_display_pipe_funcs *funcs,
			      int connector_type,
			      const uint32_t *formats,
			      unsigned int format_count,
			      const struct drm_display_mode *mode,
			      unsigned int rotation);

size_t tinydrm_spi_max_transfer_size(struct spi_device *spi, size_t max_len);
int tinydrm_spi_transfer(struct spi_device *spi, u32 speed_hz,
			 struct spi_transfer *header, u8 bpw, const void *buf,
			 size_t len);

#endif /* __LINUX_TINYDRM_HELPERS_H */
