/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MIPI Display Bus Interface (DBI) LCD controller support
 *
 * Copyright 2016 Noralf Trønnes
 */

#ifndef __LINUX_MIPI_DBI_H
#define __LINUX_MIPI_DBI_H

#include <linux/mutex.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

struct drm_format_conv_state;
struct drm_rect;
struct gpio_desc;
struct iosys_map;
struct regulator;
struct spi_device;

/**
 * struct mipi_dbi - MIPI DBI interface
 */
struct mipi_dbi {
	/**
	 * @cmdlock: Command lock
	 */
	struct mutex cmdlock;

	/**
	 * @command: Bus specific callback executing commands.
	 */
	int (*command)(struct mipi_dbi *dbi, u8 *cmd, u8 *param, size_t num);

	/**
	 * @read_commands: Array of read commands terminated by a zero entry.
	 *                 Reading is disabled if this is NULL.
	 */
	const u8 *read_commands;

	/**
	 * @swap_bytes: Swap bytes in buffer before transfer
	 */
	bool swap_bytes;

	/**
	 * @reset: Optional reset gpio
	 */
	struct gpio_desc *reset;

	/* Type C specific */

	/**
	 * @spi: SPI device
	 */
	struct spi_device *spi;

	/**
	 * @write_memory_bpw: Bits per word used on a MIPI_DCS_WRITE_MEMORY_START transfer
	 */
	unsigned int write_memory_bpw;

	/**
	 * @dc: Optional D/C gpio.
	 */
	struct gpio_desc *dc;

	/**
	 * @tx_buf9: Buffer used for Option 1 9-bit conversion
	 */
	void *tx_buf9;

	/**
	 * @tx_buf9_len: Size of tx_buf9.
	 */
	size_t tx_buf9_len;
};

/**
 * struct mipi_dbi_dev - MIPI DBI device
 */
struct mipi_dbi_dev {
	/**
	 * @drm: DRM device
	 */
	struct drm_device drm;

	/**
	 * @mode: Fixed display mode
	 */
	struct drm_display_mode mode;

	/**
	 * @pixel_format: Native pixel format (DRM_FORMAT\_\*)
	 */
	u32 pixel_format;

	/**
	 * @tx_buf: Buffer used for transfer (copy clip rect area)
	 */
	u16 *tx_buf;

	/**
	 * @rotation: initial rotation in degrees Counter Clock Wise
	 */
	unsigned int rotation;

	/**
	 * @left_offset: Horizontal offset of the display relative to the
	 *               controller's driver array
	 */
	unsigned int left_offset;

	/**
	 * @top_offset: Vertical offset of the display relative to the
	 *              controller's driver array
	 */
	unsigned int top_offset;

	/**
	 * @backlight: backlight device (optional)
	 */
	struct backlight_device *backlight;

	/**
	 * @regulator: power regulator (Vdd) (optional)
	 */
	struct regulator *regulator;

	/**
	 * @io_regulator: I/O power regulator (Vddi) (optional)
	 */
	struct regulator *io_regulator;

	/**
	 * @dbi: MIPI DBI interface
	 */
	struct mipi_dbi dbi;

	/**
	 * @driver_private: Driver private data.
	 *                  Necessary for drivers with private data since devm_drm_dev_alloc()
	 *                  can't allocate structures that embed a structure which then again
	 *                  embeds drm_device.
	 */
	void *driver_private;
};

static inline struct mipi_dbi_dev *drm_to_mipi_dbi_dev(struct drm_device *drm)
{
	return container_of(drm, struct mipi_dbi_dev, drm);
}

int mipi_dbi_spi_init(struct spi_device *spi, struct mipi_dbi *dbi,
		      struct gpio_desc *dc);

int drm_mipi_dbi_dev_init(struct mipi_dbi_dev *dbidev, const struct drm_display_mode *mode,
			  u32 format, unsigned int rotation, size_t tx_buf_size);

void mipi_dbi_hw_reset(struct mipi_dbi *dbi);
bool mipi_dbi_display_is_on(struct mipi_dbi *dbi);
int mipi_dbi_poweron_reset(struct mipi_dbi_dev *dbidev);
int mipi_dbi_poweron_conditional_reset(struct mipi_dbi_dev *dbidev);

u32 mipi_dbi_spi_cmd_max_speed(struct spi_device *spi, size_t len);
int mipi_dbi_spi_transfer(struct spi_device *spi, u32 speed_hz,
			  u8 bpw, const void *buf, size_t len);

int mipi_dbi_command_read(struct mipi_dbi *dbi, u8 cmd, u8 *val);
int mipi_dbi_command_buf(struct mipi_dbi *dbi, u8 cmd, u8 *data, size_t len);
int mipi_dbi_command_stackbuf(struct mipi_dbi *dbi, u8 cmd, const u8 *data,
			      size_t len);
int mipi_dbi_buf_copy(void *dst, struct iosys_map *src, struct drm_framebuffer *fb,
		      struct drm_rect *clip, bool swap,
		      struct drm_format_conv_state *fmtcnv_state);

/**
 * mipi_dbi_command - MIPI DCS command with optional parameter(s)
 * @dbi: MIPI DBI structure
 * @cmd: Command
 * @seq: Optional parameter(s)
 *
 * Send MIPI DCS command to the controller. Use mipi_dbi_command_read() for
 * get/read.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
#define mipi_dbi_command(dbi, cmd, seq...) \
({ \
	const u8 d[] = { seq }; \
	struct device *dev = &(dbi)->spi->dev;	\
	int ret; \
	ret = mipi_dbi_command_stackbuf(dbi, cmd, d, ARRAY_SIZE(d)); \
	if (ret) \
		dev_err_ratelimited(dev, "error %d when sending command %#02x\n", ret, cmd); \
	ret; \
})

/*
 * Plane
 */

#define DRM_MIPI_DBI_PLANE_FORMATS \
	DRM_FORMAT_RGB565, \
	DRM_FORMAT_XRGB8888

#define DRM_MIPI_DBI_PLANE_FORMAT_MODIFIERS \
	DRM_FORMAT_MOD_LINEAR, \
	DRM_FORMAT_MOD_INVALID

#define DRM_MIPI_DBI_PLANE_FUNCS \
	DRM_GEM_SHADOW_PLANE_FUNCS, \
	.update_plane = drm_atomic_helper_update_plane, \
	.disable_plane = drm_atomic_helper_disable_plane

int drm_mipi_dbi_plane_helper_atomic_check(struct drm_plane *plane,
					   struct drm_atomic_commit *state);
void drm_mipi_dbi_plane_helper_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_commit *state);

#define DRM_MIPI_DBI_PLANE_HELPER_FUNCS \
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS, \
	.atomic_check = drm_mipi_dbi_plane_helper_atomic_check, \
	.atomic_update = drm_mipi_dbi_plane_helper_atomic_update

/*
 * CRTC
 */

#define DRM_MIPI_DBI_CRTC_FUNCS \
	.reset = drm_atomic_helper_crtc_reset, \
	.set_config = drm_atomic_helper_set_config, \
	.page_flip = drm_atomic_helper_page_flip, \
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state, \
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state

enum drm_mode_status drm_mipi_dbi_crtc_helper_mode_valid(struct drm_crtc *crtc,
							 const struct drm_display_mode *mode);
int drm_mipi_dbi_crtc_helper_atomic_check(struct drm_crtc *crtc,
					  struct drm_atomic_commit *state);
void drm_mipi_dbi_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					     struct drm_atomic_commit *state);

#define DRM_MIPI_DBI_CRTC_HELPER_FUNCS \
	.mode_valid = drm_mipi_dbi_crtc_helper_mode_valid, \
	.atomic_check = drm_mipi_dbi_crtc_helper_atomic_check, \
	.atomic_disable = drm_mipi_dbi_crtc_helper_atomic_disable

/*
 * Connector
 */

#define DRM_MIPI_DBI_CONNECTOR_FUNCS \
	.reset = drm_atomic_helper_connector_reset, \
	.fill_modes = drm_helper_probe_single_connector_modes, \
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state, \
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state

int drm_mipi_dbi_connector_helper_get_modes(struct drm_connector *connector);

#define DRM_MIPI_DBI_CONNECTOR_HELPER_FUNCS \
	.get_modes = drm_mipi_dbi_connector_helper_get_modes

/*
 * Mode config
 */

#define DRM_MIPI_DBI_MODE_CONFIG_FUNCS \
	.fb_create = drm_gem_fb_create_with_dirty, \
	.atomic_check = drm_atomic_helper_check, \
	.atomic_commit = drm_atomic_helper_commit

#define DRM_MIPI_DBI_MODE_CONFIG_HELPER_FUNCS \
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm

/*
 * Debug FS
 */

#ifdef CONFIG_DEBUG_FS
void mipi_dbi_debugfs_init(struct drm_minor *minor);
#else
static inline void mipi_dbi_debugfs_init(struct drm_minor *minor) {}
#endif

#endif /* __LINUX_MIPI_DBI_H */
