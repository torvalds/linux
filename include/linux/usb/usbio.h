/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Intel Corporation.
 *
 */

#ifndef _LINUX_USBIO_H_
#define _LINUX_USBIO_H_

#include <linux/auxiliary_bus.h>
#include <linux/byteorder/generic.h>
#include <linux/list.h>
#include <linux/types.h>

/***********************
 * USBIO Clients Names *
 ***********************/
#define USBIO_GPIO_CLIENT		"usbio-gpio"
#define USBIO_I2C_CLIENT		"usbio-i2c"

/****************
 * USBIO quirks *
 ****************/
#define USBIO_QUIRK_BULK_MAXP_63	BIT(0)  /* Force bulk endpoint maxp to 63 */
#define USBIO_QUIRK_I2C_NO_INIT_ACK	BIT(8)  /* Do not ask for ack on I2C init */
#define USBIO_QUIRK_I2C_MAX_RW_LEN_52	BIT(9)  /* Set i2c-adapter max r/w len to 52 */
#define USBIO_QUIRK_I2C_USE_CHUNK_LEN	BIT(10) /* Send chunk-len for split xfers */
#define USBIO_QUIRK_I2C_ALLOW_400KHZ	BIT(11) /* Override desc, allowing 400 KHz */

/**************************
 * USBIO Type Definitions *
 **************************/

/* USBIO Packet Type */
#define USBIO_PKTTYPE_CTRL		1
#define USBIO_PKTTYPE_DBG		2
#define USBIO_PKTTYPE_GPIO		3
#define USBIO_PKTTYPE_I2C		4

/* USBIO Packet Header */
struct usbio_packet_header {
	u8 type;
	u8 cmd;
	u8 flags;
} __packed;

/* USBIO Control Transfer Packet */
struct usbio_ctrl_packet {
	struct usbio_packet_header header;
	u8 len;
	u8 data[] __counted_by(len);
} __packed;

/* USBIO Bulk Transfer Packet */
struct usbio_bulk_packet {
	struct usbio_packet_header header;
	__le16 len;
	u8 data[] __counted_by(len);
} __packed;

/* USBIO GPIO commands */
enum usbio_gpio_cmd {
	USBIO_GPIOCMD_DEINIT,
	USBIO_GPIOCMD_INIT,
	USBIO_GPIOCMD_READ,
	USBIO_GPIOCMD_WRITE,
	USBIO_GPIOCMD_END
};

/* USBIO GPIO config */
enum usbio_gpio_pincfg {
	USBIO_GPIO_PINCFG_DEFAULT,
	USBIO_GPIO_PINCFG_PULLUP,
	USBIO_GPIO_PINCFG_PULLDOWN,
	USBIO_GPIO_PINCFG_PUSHPULL
};

#define USBIO_GPIO_PINCFG_SHIFT		2
#define USBIO_GPIO_PINCFG_MASK		(0x3 << USBIO_GPIO_PINCFG_SHIFT)
#define USBIO_GPIO_SET_PINCFG(pincfg) \
	(((pincfg) << USBIO_GPIO_PINCFG_SHIFT) & USBIO_GPIO_PINCFG_MASK)

enum usbio_gpio_pinmode {
	USBIO_GPIO_PINMOD_INVAL,
	USBIO_GPIO_PINMOD_INPUT,
	USBIO_GPIO_PINMOD_OUTPUT,
	USBIO_GPIO_PINMOD_MAXVAL
};

#define USBIO_GPIO_PINMOD_MASK		0x3
#define USBIO_GPIO_SET_PINMOD(pin)	(pin & USBIO_GPIO_PINMOD_MASK)

/*************************
 * USBIO GPIO Controller *
 *************************/

#define USBIO_MAX_GPIOBANKS		5
#define USBIO_GPIOSPERBANK		32

struct usbio_gpio_bank_desc {
	u8 id;
	u8 pins;
	__le32 bmap;
} __packed;

struct usbio_gpio_init {
	u8 bankid;
	u8 config;
	u8 pincount;
	u8 pin;
} __packed;

struct usbio_gpio_rw {
	u8 bankid;
	u8 pincount;
	u8 pin;
	__le32 value;
} __packed;

/* USBIO I2C commands */
enum usbio_i2c_cmd {
	USBIO_I2CCMD_UNINIT,
	USBIO_I2CCMD_INIT,
	USBIO_I2CCMD_READ,
	USBIO_I2CCMD_WRITE,
	USBIO_I2CCMD_END
};

/************************
 * USBIO I2C Controller *
 ************************/

#define USBIO_MAX_I2CBUSES 5

#define USBIO_I2C_BUS_ADDR_CAP_10B	BIT(3) /* 10bit address support */
#define USBIO_I2C_BUS_MODE_CAP_MASK	0x3
#define USBIO_I2C_BUS_MODE_CAP_SM	0 /* Standard Mode */
#define USBIO_I2C_BUS_MODE_CAP_FM	1 /* Fast Mode */
#define USBIO_I2C_BUS_MODE_CAP_FMP	2 /* Fast Mode+ */
#define USBIO_I2C_BUS_MODE_CAP_HSM	3 /* High-Speed Mode */

struct usbio_i2c_bus_desc {
	u8 id;
	u8 caps;
} __packed;

struct usbio_i2c_uninit {
	u8 busid;
	__le16 config;
} __packed;

struct usbio_i2c_init {
	u8 busid;
	__le16 config;
	__le32 speed;
} __packed;

struct usbio_i2c_rw {
	u8 busid;
	__le16 config;
	__le16 size;
	u8 data[] __counted_by(size);
} __packed;

int usbio_control_msg(struct auxiliary_device *adev, u8 type, u8 cmd,
		      const void *obuf, u16 obuf_len, void *ibuf, u16 ibuf_len);

int usbio_bulk_msg(struct auxiliary_device *adev, u8 type, u8 cmd, bool last,
		   const void *obuf, u16 obuf_len, void *ibuf, u16 ibuf_len);

int usbio_acquire(struct auxiliary_device *adev);
void usbio_release(struct auxiliary_device *adev);
void usbio_get_txrxbuf_len(struct auxiliary_device *adev, u16 *txbuf_len, u16 *rxbuf_len);
unsigned long usbio_get_quirks(struct auxiliary_device *adev);
void usbio_acpi_bind(struct auxiliary_device *adev, const struct acpi_device_id *hids);

#endif
