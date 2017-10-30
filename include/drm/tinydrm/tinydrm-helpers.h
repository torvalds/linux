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
struct tinydrm_device;
struct drm_clip_rect;
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

bool tinydrm_merge_clips(struct drm_clip_rect *dst,
			 struct drm_clip_rect *src, unsigned int num_clips,
			 unsigned int flags, u32 max_width, u32 max_height);
void tinydrm_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		    struct drm_clip_rect *clip);
void tinydrm_swab16(u16 *dst, void *vaddr, struct drm_framebuffer *fb,
		    struct drm_clip_rect *clip);
void tinydrm_xrgb8888_to_rgb565(u16 *dst, void *vaddr,
				struct drm_framebuffer *fb,
				struct drm_clip_rect *clip, bool swap);
void tinydrm_xrgb8888_to_gray8(u8 *dst, void *vaddr, struct drm_framebuffer *fb,
			       struct drm_clip_rect *clip);

struct backlight_device *tinydrm_of_find_backlight(struct device *dev);
int tinydrm_enable_backlight(struct backlight_device *backlight);
int tinydrm_disable_backlight(struct backlight_device *backlight);

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
