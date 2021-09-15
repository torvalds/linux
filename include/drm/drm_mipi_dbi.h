/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MIPI Display Bus Interface (DBI) LCD controller support
 *
 * Copyright 2016 Noralf Trønnes
 */

#ifndef __LINUX_MIPI_DBI_H
#define __LINUX_MIPI_DBI_H

#include <linux/mutex.h>
#include <drm/drm_device.h>
#include <drm/drm_simple_kms_helper.h>

struct drm_rect;
struct spi_device;
struct gpio_desc;
struct regulator;

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
	 * @pipe: Display pipe structure
	 */
	struct drm_simple_display_pipe pipe;

	/**
	 * @connector: Connector
	 */
	struct drm_connector connector;

	/**
	 * @mode: Fixed display mode
	 */
	struct drm_display_mode mode;

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
	 * @regulator: power regulator (optional)
	 */
	struct regulator *regulator;

	/**
	 * @dbi: MIPI DBI interface
	 */
	struct mipi_dbi dbi;
};

static inline struct mipi_dbi_dev *drm_to_mipi_dbi_dev(struct drm_device *drm)
{
	return container_of(drm, struct mipi_dbi_dev, drm);
}

int mipi_dbi_spi_init(struct spi_device *spi, struct mipi_dbi *dbi,
		      struct gpio_desc *dc);
int mipi_dbi_dev_init_with_formats(struct mipi_dbi_dev *dbidev,
				   const struct drm_simple_display_pipe_funcs *funcs,
				   const uint32_t *formats, unsigned int format_count,
				   const struct drm_display_mode *mode,
				   unsigned int rotation, size_t tx_buf_size);
int mipi_dbi_dev_init(struct mipi_dbi_dev *dbidev,
		      const struct drm_simple_display_pipe_funcs *funcs,
		      const struct drm_display_mode *mode, unsigned int rotation);
void mipi_dbi_pipe_update(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *old_state);
void mipi_dbi_enable_flush(struct mipi_dbi_dev *dbidev,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plan_state);
void mipi_dbi_pipe_disable(struct drm_simple_display_pipe *pipe);
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
int mipi_dbi_buf_copy(void *dst, struct drm_framebuffer *fb,
		      struct drm_rect *clip, bool swap);
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

#ifdef CONFIG_DEBUG_FS
void mipi_dbi_debugfs_init(struct drm_minor *minor);
#else
#define mipi_dbi_debugfs_init		NULL
#endif

#endif /* __LINUX_MIPI_DBI_H */
