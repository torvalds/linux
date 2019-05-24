/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
struct spi_message;
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
bool tinydrm_spi_bpw_supported(struct spi_device *spi, u8 bpw);
int tinydrm_spi_transfer(struct spi_device *spi, u32 speed_hz,
			 struct spi_transfer *header, u8 bpw, const void *buf,
			 size_t len);
void _tinydrm_dbg_spi_message(struct spi_device *spi, struct spi_message *m);

#ifdef DEBUG
/**
 * tinydrm_dbg_spi_message - Dump SPI message
 * @spi: SPI device
 * @m: SPI message
 *
 * Dumps info about the transfers in a SPI message including buffer content.
 * DEBUG has to be defined for this function to be enabled alongside setting
 * the DRM_UT_DRIVER bit of &drm_debug.
 */
static inline void tinydrm_dbg_spi_message(struct spi_device *spi,
					   struct spi_message *m)
{
	if (drm_debug & DRM_UT_DRIVER)
		_tinydrm_dbg_spi_message(spi, m);
}
#else
static inline void tinydrm_dbg_spi_message(struct spi_device *spi,
					   struct spi_message *m)
{
}
#endif /* DEBUG */

#endif /* __LINUX_TINYDRM_HELPERS_H */
