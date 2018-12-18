/*
 *  Copyright © 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_RASPBERRY_FIRMWARE_H__
#define __SOC_RASPBERRY_FIRMWARE_H__

#include <linux/types.h>
#include <linux/of_device.h>

struct rpi_firmware;

enum rpi_firmware_property_status {
	RPI_FIRMWARE_STATUS_REQUEST = 0,
	RPI_FIRMWARE_STATUS_SUCCESS = 0x80000000,
	RPI_FIRMWARE_STATUS_ERROR =   0x80000001,
};

/**
 * struct rpi_firmware_property_tag_header - Firmware property tag header
 * @tag:		One of enum_mbox_property_tag.
 * @buf_size:		The number of bytes in the value buffer following this
 *			struct.
 * @req_resp_size:	On submit, the length of the request (though it doesn't
 *			appear to be currently used by the firmware).  On return,
 *			the length of the response (always 4 byte aligned), with
 *			the low bit set.
 */
struct rpi_firmware_property_tag_header {
	u32 tag;
	u32 buf_size;
	u32 req_resp_size;
};

enum rpi_firmware_property_tag {
	RPI_FIRMWARE_PROPERTY_END =                           0,
	RPI_FIRMWARE_GET_FIRMWARE_REVISION =                  0x00000001,

	RPI_FIRMWARE_SET_CURSOR_INFO =                        0x00008010,
	RPI_FIRMWARE_SET_CURSOR_STATE =                       0x00008011,

	RPI_FIRMWARE_GET_BOARD_MODEL =                        0x00010001,
	RPI_FIRMWARE_GET_BOARD_REVISION =                     0x00010002,
	RPI_FIRMWARE_GET_BOARD_MAC_ADDRESS =                  0x00010003,
	RPI_FIRMWARE_GET_BOARD_SERIAL =                       0x00010004,
	RPI_FIRMWARE_GET_ARM_MEMORY =                         0x00010005,
	RPI_FIRMWARE_GET_VC_MEMORY =                          0x00010006,
	RPI_FIRMWARE_GET_CLOCKS =                             0x00010007,
	RPI_FIRMWARE_GET_POWER_STATE =                        0x00020001,
	RPI_FIRMWARE_GET_TIMING =                             0x00020002,
	RPI_FIRMWARE_SET_POWER_STATE =                        0x00028001,
	RPI_FIRMWARE_GET_CLOCK_STATE =                        0x00030001,
	RPI_FIRMWARE_GET_CLOCK_RATE =                         0x00030002,
	RPI_FIRMWARE_GET_VOLTAGE =                            0x00030003,
	RPI_FIRMWARE_GET_MAX_CLOCK_RATE =                     0x00030004,
	RPI_FIRMWARE_GET_MAX_VOLTAGE =                        0x00030005,
	RPI_FIRMWARE_GET_TEMPERATURE =                        0x00030006,
	RPI_FIRMWARE_GET_MIN_CLOCK_RATE =                     0x00030007,
	RPI_FIRMWARE_GET_MIN_VOLTAGE =                        0x00030008,
	RPI_FIRMWARE_GET_TURBO =                              0x00030009,
	RPI_FIRMWARE_GET_MAX_TEMPERATURE =                    0x0003000a,
	RPI_FIRMWARE_GET_STC =                                0x0003000b,
	RPI_FIRMWARE_ALLOCATE_MEMORY =                        0x0003000c,
	RPI_FIRMWARE_LOCK_MEMORY =                            0x0003000d,
	RPI_FIRMWARE_UNLOCK_MEMORY =                          0x0003000e,
	RPI_FIRMWARE_RELEASE_MEMORY =                         0x0003000f,
	RPI_FIRMWARE_EXECUTE_CODE =                           0x00030010,
	RPI_FIRMWARE_EXECUTE_QPU =                            0x00030011,
	RPI_FIRMWARE_SET_ENABLE_QPU =                         0x00030012,
	RPI_FIRMWARE_GET_DISPMANX_RESOURCE_MEM_HANDLE =       0x00030014,
	RPI_FIRMWARE_GET_EDID_BLOCK =                         0x00030020,
	RPI_FIRMWARE_GET_CUSTOMER_OTP =                       0x00030021,
	RPI_FIRMWARE_GET_DOMAIN_STATE =                       0x00030030,
	RPI_FIRMWARE_SET_CLOCK_STATE =                        0x00038001,
	RPI_FIRMWARE_SET_CLOCK_RATE =                         0x00038002,
	RPI_FIRMWARE_SET_VOLTAGE =                            0x00038003,
	RPI_FIRMWARE_SET_TURBO =                              0x00038009,
	RPI_FIRMWARE_SET_CUSTOMER_OTP =                       0x00038021,
	RPI_FIRMWARE_SET_DOMAIN_STATE =                       0x00038030,
	RPI_FIRMWARE_GET_GPIO_STATE =                         0x00030041,
	RPI_FIRMWARE_SET_GPIO_STATE =                         0x00038041,
	RPI_FIRMWARE_SET_SDHOST_CLOCK =                       0x00038042,
	RPI_FIRMWARE_GET_GPIO_CONFIG =                        0x00030043,
	RPI_FIRMWARE_SET_GPIO_CONFIG =                        0x00038043,
	RPI_FIRMWARE_GET_PERIPH_REG =                         0x00030045,
	RPI_FIRMWARE_SET_PERIPH_REG =                         0x00038045,


	/* Dispmanx TAGS */
	RPI_FIRMWARE_FRAMEBUFFER_ALLOCATE =                   0x00040001,
	RPI_FIRMWARE_FRAMEBUFFER_BLANK =                      0x00040002,
	RPI_FIRMWARE_FRAMEBUFFER_GET_PHYSICAL_WIDTH_HEIGHT =  0x00040003,
	RPI_FIRMWARE_FRAMEBUFFER_GET_VIRTUAL_WIDTH_HEIGHT =   0x00040004,
	RPI_FIRMWARE_FRAMEBUFFER_GET_DEPTH =                  0x00040005,
	RPI_FIRMWARE_FRAMEBUFFER_GET_PIXEL_ORDER =            0x00040006,
	RPI_FIRMWARE_FRAMEBUFFER_GET_ALPHA_MODE =             0x00040007,
	RPI_FIRMWARE_FRAMEBUFFER_GET_PITCH =                  0x00040008,
	RPI_FIRMWARE_FRAMEBUFFER_GET_VIRTUAL_OFFSET =         0x00040009,
	RPI_FIRMWARE_FRAMEBUFFER_GET_OVERSCAN =               0x0004000a,
	RPI_FIRMWARE_FRAMEBUFFER_GET_PALETTE =                0x0004000b,
	RPI_FIRMWARE_FRAMEBUFFER_GET_TOUCHBUF =               0x0004000f,
	RPI_FIRMWARE_FRAMEBUFFER_GET_GPIOVIRTBUF =            0x00040010,
	RPI_FIRMWARE_FRAMEBUFFER_RELEASE =                    0x00048001,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_PHYSICAL_WIDTH_HEIGHT = 0x00044003,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_VIRTUAL_WIDTH_HEIGHT =  0x00044004,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_DEPTH =                 0x00044005,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_PIXEL_ORDER =           0x00044006,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_ALPHA_MODE =            0x00044007,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_VIRTUAL_OFFSET =        0x00044009,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_OVERSCAN =              0x0004400a,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_PALETTE =               0x0004400b,
	RPI_FIRMWARE_FRAMEBUFFER_TEST_VSYNC =                 0x0004400e,
	RPI_FIRMWARE_FRAMEBUFFER_SET_PHYSICAL_WIDTH_HEIGHT =  0x00048003,
	RPI_FIRMWARE_FRAMEBUFFER_SET_VIRTUAL_WIDTH_HEIGHT =   0x00048004,
	RPI_FIRMWARE_FRAMEBUFFER_SET_DEPTH =                  0x00048005,
	RPI_FIRMWARE_FRAMEBUFFER_SET_PIXEL_ORDER =            0x00048006,
	RPI_FIRMWARE_FRAMEBUFFER_SET_ALPHA_MODE =             0x00048007,
	RPI_FIRMWARE_FRAMEBUFFER_SET_VIRTUAL_OFFSET =         0x00048009,
	RPI_FIRMWARE_FRAMEBUFFER_SET_OVERSCAN =               0x0004800a,
	RPI_FIRMWARE_FRAMEBUFFER_SET_PALETTE =                0x0004800b,
	RPI_FIRMWARE_FRAMEBUFFER_SET_TOUCHBUF =               0x0004801f,
	RPI_FIRMWARE_FRAMEBUFFER_SET_GPIOVIRTBUF =            0x00048020,
	RPI_FIRMWARE_FRAMEBUFFER_SET_VSYNC =                  0x0004800e,
	RPI_FIRMWARE_FRAMEBUFFER_SET_BACKLIGHT =              0x0004800f,

	RPI_FIRMWARE_VCHIQ_INIT =                             0x00048010,

	RPI_FIRMWARE_GET_COMMAND_LINE =                       0x00050001,
	RPI_FIRMWARE_GET_DMA_CHANNELS =                       0x00060001,
};

#if IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE)
int rpi_firmware_property(struct rpi_firmware *fw,
			  u32 tag, void *data, size_t len);
int rpi_firmware_property_list(struct rpi_firmware *fw,
			       void *data, size_t tag_size);
struct rpi_firmware *rpi_firmware_get(struct device_node *firmware_node);
#else
static inline int rpi_firmware_property(struct rpi_firmware *fw, u32 tag,
					void *data, size_t len)
{
	return -ENOSYS;
}

static inline int rpi_firmware_property_list(struct rpi_firmware *fw,
					     void *data, size_t tag_size)
{
	return -ENOSYS;
}

static inline struct rpi_firmware *rpi_firmware_get(struct device_node *firmware_node)
{
	return NULL;
}
#endif

#endif /* __SOC_RASPBERRY_FIRMWARE_H__ */
