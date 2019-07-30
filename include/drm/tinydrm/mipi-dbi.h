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
 * struct mipi_dbi - MIPI DBI controller
 * @spi: SPI device
 * @enabled: Pipeline is enabled
 * @cmdlock: Command lock
 * @command: Bus specific callback executing commands.
 * @read_commands: Array of read commands terminated by a zero entry.
 *                 Reading is disabled if this is NULL.
 * @dc: Optional D/C gpio.
 * @tx_buf: Buffer used for transfer (copy clip rect area)
 * @tx_buf9: Buffer used for Option 1 9-bit conversion
 * @tx_buf9_len: Size of tx_buf9.
 * @swap_bytes: Swap bytes in buffer before transfer
 * @reset: Optional reset gpio
 * @rotation: initial rotation in degrees Counter Clock Wise
 * @backlight: backlight device (optional)
 * @regulator: power regulator (optional)
 */
struct mipi_dbi {
	/**
	 * @drm: DRM device
	 */
	struct drm_device drm;

	/**
	 * @pipe: Display pipe structure
	 */
	struct drm_simple_display_pipe pipe;

	struct spi_device *spi;
	bool enabled;
	struct mutex cmdlock;
	int (*command)(struct mipi_dbi *mipi, u8 *cmd, u8 *param, size_t num);
	const u8 *read_commands;
	struct gpio_desc *dc;
	u16 *tx_buf;
	void *tx_buf9;
	size_t tx_buf9_len;
	bool swap_bytes;
	struct gpio_desc *reset;
	unsigned int rotation;
	struct backlight_device *backlight;
	struct regulator *regulator;
};

static inline struct mipi_dbi *drm_to_mipi_dbi(struct drm_device *drm)
{
	return container_of(drm, struct mipi_dbi, drm);
}

int mipi_dbi_spi_init(struct spi_device *spi, struct mipi_dbi *mipi,
		      struct gpio_desc *dc);
int mipi_dbi_init(struct mipi_dbi *mipi,
		  const struct drm_simple_display_pipe_funcs *funcs,
		  const struct drm_display_mode *mode, unsigned int rotation);
void mipi_dbi_release(struct drm_device *drm);
void mipi_dbi_pipe_update(struct drm_simple_display_pipe *pipe,
			  struct drm_plane_state *old_state);
void mipi_dbi_enable_flush(struct mipi_dbi *mipi,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plan_state);
void mipi_dbi_pipe_disable(struct drm_simple_display_pipe *pipe);
void mipi_dbi_hw_reset(struct mipi_dbi *mipi);
bool mipi_dbi_display_is_on(struct mipi_dbi *mipi);
int mipi_dbi_poweron_reset(struct mipi_dbi *mipi);
int mipi_dbi_poweron_conditional_reset(struct mipi_dbi *mipi);
u32 mipi_dbi_spi_cmd_max_speed(struct spi_device *spi, size_t len);

int mipi_dbi_command_read(struct mipi_dbi *mipi, u8 cmd, u8 *val);
int mipi_dbi_command_buf(struct mipi_dbi *mipi, u8 cmd, u8 *data, size_t len);
int mipi_dbi_command_stackbuf(struct mipi_dbi *mipi, u8 cmd, u8 *data, size_t len);
int mipi_dbi_buf_copy(void *dst, struct drm_framebuffer *fb,
		      struct drm_rect *clip, bool swap);
/**
 * mipi_dbi_command - MIPI DCS command with optional parameter(s)
 * @mipi: MIPI structure
 * @cmd: Command
 * @seq...: Optional parameter(s)
 *
 * Send MIPI DCS command to the controller. Use mipi_dbi_command_read() for
 * get/read.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
#define mipi_dbi_command(mipi, cmd, seq...) \
({ \
	u8 d[] = { seq }; \
	mipi_dbi_command_stackbuf(mipi, cmd, d, ARRAY_SIZE(d)); \
})

#ifdef CONFIG_DEBUG_FS
int mipi_dbi_debugfs_init(struct drm_minor *minor);
#else
#define mipi_dbi_debugfs_init		NULL
#endif

#endif /* __LINUX_MIPI_DBI_H */
