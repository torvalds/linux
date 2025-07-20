/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MIPI DSI Bus
 *
 * Copyright (C) 2012-2013, Samsung Electronics, Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 */

#ifndef __DRM_MIPI_DSI_H__
#define __DRM_MIPI_DSI_H__

#include <linux/device.h>
#include <linux/delay.h>

struct mipi_dsi_host;
struct mipi_dsi_device;
struct drm_dsc_picture_parameter_set;

/* request ACK from peripheral */
#define MIPI_DSI_MSG_REQ_ACK	BIT(0)
/* use Low Power Mode to transmit message */
#define MIPI_DSI_MSG_USE_LPM	BIT(1)

/**
 * struct mipi_dsi_msg - read/write DSI buffer
 * @channel: virtual channel id
 * @type: payload data type
 * @flags: flags controlling this message transmission
 * @tx_len: length of @tx_buf
 * @tx_buf: data to be written
 * @rx_len: length of @rx_buf
 * @rx_buf: data to be read, or NULL
 */
struct mipi_dsi_msg {
	u8 channel;
	u8 type;
	u16 flags;

	size_t tx_len;
	const void *tx_buf;

	size_t rx_len;
	void *rx_buf;
};

bool mipi_dsi_packet_format_is_short(u8 type);
bool mipi_dsi_packet_format_is_long(u8 type);

/**
 * struct mipi_dsi_packet - represents a MIPI DSI packet in protocol format
 * @size: size (in bytes) of the packet
 * @header: the four bytes that make up the header (Data ID, Word Count or
 *     Packet Data, and ECC)
 * @payload_length: number of bytes in the payload
 * @payload: a pointer to a buffer containing the payload, if any
 */
struct mipi_dsi_packet {
	size_t size;
	u8 header[4];
	size_t payload_length;
	const u8 *payload;
};

int mipi_dsi_create_packet(struct mipi_dsi_packet *packet,
			   const struct mipi_dsi_msg *msg);

/**
 * struct mipi_dsi_host_ops - DSI bus operations
 * @attach: attach DSI device to DSI host
 * @detach: detach DSI device from DSI host
 * @transfer: transmit a DSI packet
 *
 * DSI packets transmitted by .transfer() are passed in as mipi_dsi_msg
 * structures. This structure contains information about the type of packet
 * being transmitted as well as the transmit and receive buffers. When an
 * error is encountered during transmission, this function will return a
 * negative error code. On success it shall return the number of bytes
 * transmitted for write packets or the number of bytes received for read
 * packets.
 *
 * Note that typically DSI packet transmission is atomic, so the .transfer()
 * function will seldomly return anything other than the number of bytes
 * contained in the transmit buffer on success.
 *
 * Also note that those callbacks can be called no matter the state the
 * host is in. Drivers that need the underlying device to be powered to
 * perform these operations will first need to make sure it's been
 * properly enabled.
 */
struct mipi_dsi_host_ops {
	int (*attach)(struct mipi_dsi_host *host,
		      struct mipi_dsi_device *dsi);
	int (*detach)(struct mipi_dsi_host *host,
		      struct mipi_dsi_device *dsi);
	ssize_t (*transfer)(struct mipi_dsi_host *host,
			    const struct mipi_dsi_msg *msg);
};

/**
 * struct mipi_dsi_host - DSI host device
 * @dev: driver model device node for this DSI host
 * @ops: DSI host operations
 * @list: list management
 */
struct mipi_dsi_host {
	struct device *dev;
	const struct mipi_dsi_host_ops *ops;
	struct list_head list;
};

int mipi_dsi_host_register(struct mipi_dsi_host *host);
void mipi_dsi_host_unregister(struct mipi_dsi_host *host);
struct mipi_dsi_host *of_find_mipi_dsi_host_by_node(struct device_node *node);

/* DSI mode flags */

/* video mode */
#define MIPI_DSI_MODE_VIDEO		BIT(0)
/* video burst mode */
#define MIPI_DSI_MODE_VIDEO_BURST	BIT(1)
/* video pulse mode */
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE	BIT(2)
/* enable auto vertical count mode */
#define MIPI_DSI_MODE_VIDEO_AUTO_VERT	BIT(3)
/* enable hsync-end packets in vsync-pulse and v-porch area */
#define MIPI_DSI_MODE_VIDEO_HSE		BIT(4)
/* disable hfront-porch area */
#define MIPI_DSI_MODE_VIDEO_NO_HFP	BIT(5)
/* disable hback-porch area */
#define MIPI_DSI_MODE_VIDEO_NO_HBP	BIT(6)
/* disable hsync-active area */
#define MIPI_DSI_MODE_VIDEO_NO_HSA	BIT(7)
/* flush display FIFO on vsync pulse */
#define MIPI_DSI_MODE_VSYNC_FLUSH	BIT(8)
/* disable EoT packets in HS mode */
#define MIPI_DSI_MODE_NO_EOT_PACKET	BIT(9)
/* device supports non-continuous clock behavior (DSI spec 5.6.1) */
#define MIPI_DSI_CLOCK_NON_CONTINUOUS	BIT(10)
/* transmit data in low power */
#define MIPI_DSI_MODE_LPM		BIT(11)
/* transmit data ending at the same time for all lanes within one hsync */
#define MIPI_DSI_HS_PKT_END_ALIGNED	BIT(12)

enum mipi_dsi_pixel_format {
	MIPI_DSI_FMT_RGB888,
	MIPI_DSI_FMT_RGB666,
	MIPI_DSI_FMT_RGB666_PACKED,
	MIPI_DSI_FMT_RGB565,
};

#define DSI_DEV_NAME_SIZE		20

/**
 * struct mipi_dsi_device_info - template for creating a mipi_dsi_device
 * @type: DSI peripheral chip type
 * @channel: DSI virtual channel assigned to peripheral
 * @node: pointer to OF device node or NULL
 *
 * This is populated and passed to mipi_dsi_device_new to create a new
 * DSI device
 */
struct mipi_dsi_device_info {
	char type[DSI_DEV_NAME_SIZE];
	u32 channel;
	struct device_node *node;
};

/**
 * struct mipi_dsi_device - DSI peripheral device
 * @host: DSI host for this peripheral
 * @dev: driver model device node for this peripheral
 * @attached: the DSI device has been successfully attached
 * @name: DSI peripheral chip type
 * @channel: virtual channel assigned to the peripheral
 * @format: pixel format for video mode
 * @lanes: number of active data lanes
 * @mode_flags: DSI operation mode related flags
 * @hs_rate: maximum lane frequency for high speed mode in hertz, this should
 * be set to the real limits of the hardware, zero is only accepted for
 * legacy drivers
 * @lp_rate: maximum lane frequency for low power mode in hertz, this should
 * be set to the real limits of the hardware, zero is only accepted for
 * legacy drivers
 * @dsc: panel/bridge DSC pps payload to be sent
 */
struct mipi_dsi_device {
	struct mipi_dsi_host *host;
	struct device dev;
	bool attached;

	char name[DSI_DEV_NAME_SIZE];
	unsigned int channel;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	unsigned long hs_rate;
	unsigned long lp_rate;
	struct drm_dsc_config *dsc;
};

/**
 * struct mipi_dsi_multi_context - Context to call multiple MIPI DSI funcs in a row
 */
struct mipi_dsi_multi_context {
	/**
	 * @dsi: Pointer to the MIPI DSI device
	 */
	struct mipi_dsi_device *dsi;

	/**
	 * @accum_err: Storage for the accumulated error over the multiple calls
	 *
	 * Init to 0. If a function encounters an error then the error code
	 * will be stored here. If you call a function and this points to a
	 * non-zero value then the function will be a noop. This allows calling
	 * a function many times in a row and just checking the error at the
	 * end to see if any of them failed.
	 */
	int accum_err;
};

#define MIPI_DSI_MODULE_PREFIX "mipi-dsi:"

#define to_mipi_dsi_device(__dev)	container_of_const(__dev, struct mipi_dsi_device, dev)

extern const struct bus_type mipi_dsi_bus_type;
#define dev_is_mipi_dsi(dev)	((dev)->bus == &mipi_dsi_bus_type)

/**
 * mipi_dsi_pixel_format_to_bpp - obtain the number of bits per pixel for any
 *                                given pixel format defined by the MIPI DSI
 *                                specification
 * @fmt: MIPI DSI pixel format
 *
 * Returns: The number of bits per pixel of the given pixel format.
 */
static inline int mipi_dsi_pixel_format_to_bpp(enum mipi_dsi_pixel_format fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB888:
	case MIPI_DSI_FMT_RGB666:
		return 24;

	case MIPI_DSI_FMT_RGB666_PACKED:
		return 18;

	case MIPI_DSI_FMT_RGB565:
		return 16;
	}

	return -EINVAL;
}

enum mipi_dsi_compression_algo {
	MIPI_DSI_COMPRESSION_DSC = 0,
	MIPI_DSI_COMPRESSION_VENDOR = 3,
	/* other two values are reserved, DSI 1.3 */
};

struct mipi_dsi_device *
mipi_dsi_device_register_full(struct mipi_dsi_host *host,
			      const struct mipi_dsi_device_info *info);
void mipi_dsi_device_unregister(struct mipi_dsi_device *dsi);
struct mipi_dsi_device *
devm_mipi_dsi_device_register_full(struct device *dev, struct mipi_dsi_host *host,
				   const struct mipi_dsi_device_info *info);
struct mipi_dsi_device *of_find_mipi_dsi_device_by_node(struct device_node *np);
int mipi_dsi_attach(struct mipi_dsi_device *dsi);
int mipi_dsi_detach(struct mipi_dsi_device *dsi);
int devm_mipi_dsi_attach(struct device *dev, struct mipi_dsi_device *dsi);
int mipi_dsi_shutdown_peripheral(struct mipi_dsi_device *dsi);
int mipi_dsi_turn_on_peripheral(struct mipi_dsi_device *dsi);
int mipi_dsi_set_maximum_return_packet_size(struct mipi_dsi_device *dsi,
					    u16 value);
int mipi_dsi_compression_mode(struct mipi_dsi_device *dsi, bool enable);
int mipi_dsi_compression_mode_ext(struct mipi_dsi_device *dsi, bool enable,
				  enum mipi_dsi_compression_algo algo,
				  unsigned int pps_selector);
int mipi_dsi_picture_parameter_set(struct mipi_dsi_device *dsi,
				   const struct drm_dsc_picture_parameter_set *pps);

void mipi_dsi_compression_mode_ext_multi(struct mipi_dsi_multi_context *ctx,
					 bool enable,
					 enum mipi_dsi_compression_algo algo,
					 unsigned int pps_selector);
void mipi_dsi_compression_mode_multi(struct mipi_dsi_multi_context *ctx,
				     bool enable);
void mipi_dsi_picture_parameter_set_multi(struct mipi_dsi_multi_context *ctx,
					  const struct drm_dsc_picture_parameter_set *pps);

ssize_t mipi_dsi_generic_write(struct mipi_dsi_device *dsi, const void *payload,
			       size_t size);
int mipi_dsi_generic_write_chatty(struct mipi_dsi_device *dsi,
				  const void *payload, size_t size);
void mipi_dsi_generic_write_multi(struct mipi_dsi_multi_context *ctx,
				  const void *payload, size_t size);
ssize_t mipi_dsi_generic_read(struct mipi_dsi_device *dsi, const void *params,
			      size_t num_params, void *data, size_t size);
u32 drm_mipi_dsi_get_input_bus_fmt(enum mipi_dsi_pixel_format dsi_format);

#define mipi_dsi_msleep(ctx, delay)	\
	do {				\
		if (!(ctx)->accum_err)	\
			msleep(delay);	\
	} while (0)

#define mipi_dsi_usleep_range(ctx, min, max)	\
	do {					\
		if (!(ctx)->accum_err)		\
			usleep_range(min, max);	\
	} while (0)

/**
 * enum mipi_dsi_dcs_tear_mode - Tearing Effect Output Line mode
 * @MIPI_DSI_DCS_TEAR_MODE_VBLANK: the TE output line consists of V-Blanking
 *    information only
 * @MIPI_DSI_DCS_TEAR_MODE_VHBLANK : the TE output line consists of both
 *    V-Blanking and H-Blanking information
 */
enum mipi_dsi_dcs_tear_mode {
	MIPI_DSI_DCS_TEAR_MODE_VBLANK,
	MIPI_DSI_DCS_TEAR_MODE_VHBLANK,
};

#define MIPI_DSI_DCS_POWER_MODE_DISPLAY (1 << 2)
#define MIPI_DSI_DCS_POWER_MODE_NORMAL  (1 << 3)
#define MIPI_DSI_DCS_POWER_MODE_SLEEP   (1 << 4)
#define MIPI_DSI_DCS_POWER_MODE_PARTIAL (1 << 5)
#define MIPI_DSI_DCS_POWER_MODE_IDLE    (1 << 6)

ssize_t mipi_dsi_dcs_write_buffer(struct mipi_dsi_device *dsi,
				  const void *data, size_t len);
int mipi_dsi_dcs_write_buffer_chatty(struct mipi_dsi_device *dsi,
				     const void *data, size_t len);
void mipi_dsi_dcs_write_buffer_multi(struct mipi_dsi_multi_context *ctx,
				     const void *data, size_t len);
ssize_t mipi_dsi_dcs_write(struct mipi_dsi_device *dsi, u8 cmd,
			   const void *data, size_t len);
ssize_t mipi_dsi_dcs_read(struct mipi_dsi_device *dsi, u8 cmd, void *data,
			  size_t len);
int mipi_dsi_dcs_nop(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_soft_reset(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_get_power_mode(struct mipi_dsi_device *dsi, u8 *mode);
int mipi_dsi_dcs_get_pixel_format(struct mipi_dsi_device *dsi, u8 *format);
int mipi_dsi_dcs_enter_sleep_mode(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_exit_sleep_mode(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_set_display_off(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_set_display_on(struct mipi_dsi_device *dsi);
int mipi_dsi_dcs_set_column_address(struct mipi_dsi_device *dsi, u16 start,
				    u16 end);
int mipi_dsi_dcs_set_page_address(struct mipi_dsi_device *dsi, u16 start,
				  u16 end);
int mipi_dsi_dcs_set_tear_on(struct mipi_dsi_device *dsi,
			     enum mipi_dsi_dcs_tear_mode mode);
int mipi_dsi_dcs_set_pixel_format(struct mipi_dsi_device *dsi, u8 format);
int mipi_dsi_dcs_set_tear_scanline(struct mipi_dsi_device *dsi, u16 scanline);
int mipi_dsi_dcs_set_display_brightness(struct mipi_dsi_device *dsi,
					u16 brightness);
int mipi_dsi_dcs_get_display_brightness(struct mipi_dsi_device *dsi,
					u16 *brightness);
int mipi_dsi_dcs_set_display_brightness_large(struct mipi_dsi_device *dsi,
					     u16 brightness);
int mipi_dsi_dcs_get_display_brightness_large(struct mipi_dsi_device *dsi,
					     u16 *brightness);

void mipi_dsi_dcs_nop_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_enter_sleep_mode_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_exit_sleep_mode_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_set_display_off_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_set_display_on_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_set_tear_on_multi(struct mipi_dsi_multi_context *ctx,
				    enum mipi_dsi_dcs_tear_mode mode);
void mipi_dsi_turn_on_peripheral_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_soft_reset_multi(struct mipi_dsi_multi_context *ctx);
void mipi_dsi_dcs_set_display_brightness_multi(struct mipi_dsi_multi_context *ctx,
					       u16 brightness);
void mipi_dsi_dcs_set_pixel_format_multi(struct mipi_dsi_multi_context *ctx,
					 u8 format);
void mipi_dsi_dcs_set_column_address_multi(struct mipi_dsi_multi_context *ctx,
					   u16 start, u16 end);
void mipi_dsi_dcs_set_page_address_multi(struct mipi_dsi_multi_context *ctx,
					 u16 start, u16 end);
void mipi_dsi_dcs_set_tear_scanline_multi(struct mipi_dsi_multi_context *ctx,
					  u16 scanline);
void mipi_dsi_dcs_set_tear_off_multi(struct mipi_dsi_multi_context *ctx);

/**
 * mipi_dsi_generic_write_seq - transmit data using a generic write packet
 *
 * This macro will print errors for you and will RETURN FROM THE CALLING
 * FUNCTION (yes this is non-intuitive) upon error.
 *
 * Because of the non-intuitive return behavior, THIS MACRO IS DEPRECATED.
 * Please replace calls of it with mipi_dsi_generic_write_seq_multi().
 *
 * @dsi: DSI peripheral device
 * @seq: buffer containing the payload
 */
#define mipi_dsi_generic_write_seq(dsi, seq...)                                \
	do {                                                                   \
		static const u8 d[] = { seq };                                 \
		int ret;                                                       \
		ret = mipi_dsi_generic_write_chatty(dsi, d, ARRAY_SIZE(d));    \
		if (ret < 0)                                                   \
			return ret;                                            \
	} while (0)

/**
 * mipi_dsi_generic_write_seq_multi - transmit data using a generic write packet
 *
 * This macro will print errors for you and error handling is optimized for
 * callers that call this multiple times in a row.
 *
 * @ctx: Context for multiple DSI transactions
 * @seq: buffer containing the payload
 */
#define mipi_dsi_generic_write_seq_multi(ctx, seq...)                \
	do {                                                         \
		static const u8 d[] = { seq };                       \
		mipi_dsi_generic_write_multi(ctx, d, ARRAY_SIZE(d)); \
	} while (0)

/**
 * mipi_dsi_dcs_write_seq_multi - transmit a DCS command with payload
 *
 * This macro will print errors for you and error handling is optimized for
 * callers that call this multiple times in a row.
 *
 * @ctx: Context for multiple DSI transactions
 * @cmd: Command
 * @seq: buffer containing data to be transmitted
 */
#define mipi_dsi_dcs_write_seq_multi(ctx, cmd, seq...)                  \
	do {                                                            \
		static const u8 d[] = { cmd, seq };                     \
		mipi_dsi_dcs_write_buffer_multi(ctx, d, ARRAY_SIZE(d)); \
	} while (0)

/**
 * struct mipi_dsi_driver - DSI driver
 * @driver: device driver model driver
 * @probe: callback for device binding
 * @remove: callback for device unbinding
 * @shutdown: called at shutdown time to quiesce the device
 */
struct mipi_dsi_driver {
	struct device_driver driver;
	int(*probe)(struct mipi_dsi_device *dsi);
	void (*remove)(struct mipi_dsi_device *dsi);
	void (*shutdown)(struct mipi_dsi_device *dsi);
};

static inline struct mipi_dsi_driver *
to_mipi_dsi_driver(struct device_driver *driver)
{
	return container_of(driver, struct mipi_dsi_driver, driver);
}

static inline void *mipi_dsi_get_drvdata(const struct mipi_dsi_device *dsi)
{
	return dev_get_drvdata(&dsi->dev);
}

static inline void mipi_dsi_set_drvdata(struct mipi_dsi_device *dsi, void *data)
{
	dev_set_drvdata(&dsi->dev, data);
}

int mipi_dsi_driver_register_full(struct mipi_dsi_driver *driver,
				  struct module *owner);
void mipi_dsi_driver_unregister(struct mipi_dsi_driver *driver);

#define mipi_dsi_driver_register(driver) \
	mipi_dsi_driver_register_full(driver, THIS_MODULE)

#define module_mipi_dsi_driver(__mipi_dsi_driver) \
	module_driver(__mipi_dsi_driver, mipi_dsi_driver_register, \
			mipi_dsi_driver_unregister)

#endif /* __DRM_MIPI_DSI__ */
