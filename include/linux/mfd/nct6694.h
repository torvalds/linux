/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Nuvoton NCT6694 USB transaction and data structure.
 */

#ifndef __MFD_NCT6694_H
#define __MFD_NCT6694_H

#define NCT6694_VENDOR_ID	0x0416
#define NCT6694_PRODUCT_ID	0x200B
#define NCT6694_INT_IN_EP	0x81
#define NCT6694_BULK_IN_EP	0x02
#define NCT6694_BULK_OUT_EP	0x03

#define NCT6694_HCTRL_SET	0x40
#define NCT6694_HCTRL_GET	0x80

#define NCT6694_URB_TIMEOUT	1000

enum nct6694_irq_id {
	NCT6694_IRQ_GPIO0 = 0,
	NCT6694_IRQ_GPIO1,
	NCT6694_IRQ_GPIO2,
	NCT6694_IRQ_GPIO3,
	NCT6694_IRQ_GPIO4,
	NCT6694_IRQ_GPIO5,
	NCT6694_IRQ_GPIO6,
	NCT6694_IRQ_GPIO7,
	NCT6694_IRQ_GPIO8,
	NCT6694_IRQ_GPIO9,
	NCT6694_IRQ_GPIOA,
	NCT6694_IRQ_GPIOB,
	NCT6694_IRQ_GPIOC,
	NCT6694_IRQ_GPIOD,
	NCT6694_IRQ_GPIOE,
	NCT6694_IRQ_GPIOF,
	NCT6694_IRQ_CAN0,
	NCT6694_IRQ_CAN1,
	NCT6694_IRQ_RTC,
	NCT6694_NR_IRQS,
};

enum nct6694_response_err_status {
	NCT6694_NO_ERROR = 0,
	NCT6694_FORMAT_ERROR,
	NCT6694_RESERVED1,
	NCT6694_RESERVED2,
	NCT6694_NOT_SUPPORT_ERROR,
	NCT6694_NO_RESPONSE_ERROR,
	NCT6694_TIMEOUT_ERROR,
	NCT6694_PENDING,
};

struct __packed nct6694_cmd_header {
	u8 rsv1;
	u8 mod;
	union __packed {
		__le16 offset;
		struct __packed {
			u8 cmd;
			u8 sel;
		};
	};
	u8 hctrl;
	u8 rsv2;
	__le16 len;
};

struct __packed nct6694_response_header {
	u8 sequence_id;
	u8 sts;
	u8 reserved[4];
	__le16 len;
};

union __packed nct6694_usb_msg {
	struct nct6694_cmd_header cmd_header;
	struct nct6694_response_header response_header;
};

struct nct6694 {
	struct device *dev;
	struct ida gpio_ida;
	struct ida i2c_ida;
	struct ida canfd_ida;
	struct ida wdt_ida;
	struct irq_domain *domain;
	struct mutex access_lock;
	spinlock_t irq_lock;
	struct urb *int_in_urb;
	struct usb_device *udev;
	union nct6694_usb_msg *usb_msg;
	__le32 *int_buffer;
	unsigned int irq_enable;
};

int nct6694_read_msg(struct nct6694 *nct6694, const struct nct6694_cmd_header *cmd_hd, void *buf);
int nct6694_write_msg(struct nct6694 *nct6694, const struct nct6694_cmd_header *cmd_hd, void *buf);

#endif
