// SPDX-License-Identifier: GPL-1.0+
/*
 * Renesas USB
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Copyright (C) 2019 Renesas Electronics Corporation
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef RENESAS_USB_H
#define RENESAS_USB_H
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>

/*
 * module type
 *
 * it will be return value from get_id
 */
enum {
	USBHS_HOST = 0,
	USBHS_GADGET,
	USBHS_MAX,
};

/*
 * callback functions for platform
 *
 * These functions are called from driver for platform
 */
struct renesas_usbhs_platform_callback {

	/*
	 * option:
	 *
	 * Hardware init function for platform.
	 * it is called when driver was probed.
	 */
	int (*hardware_init)(struct platform_device *pdev);

	/*
	 * option:
	 *
	 * Hardware exit function for platform.
	 * it is called when driver was removed
	 */
	int (*hardware_exit)(struct platform_device *pdev);

	/*
	 * option:
	 *
	 * for board specific clock control
	 */
	int (*power_ctrl)(struct platform_device *pdev,
			   void __iomem *base, int enable);

	/*
	 * option:
	 *
	 * Phy reset for platform
	 */
	int (*phy_reset)(struct platform_device *pdev);

	/*
	 * get USB ID function
	 *  - USBHS_HOST
	 *  - USBHS_GADGET
	 */
	int (*get_id)(struct platform_device *pdev);

	/*
	 * get VBUS status function.
	 */
	int (*get_vbus)(struct platform_device *pdev);

	/*
	 * option:
	 *
	 * VBUS control is needed for Host
	 */
	int (*set_vbus)(struct platform_device *pdev, int enable);

	/*
	 * option:
	 * extcon notifier to set host/peripheral mode.
	 */
	int (*notifier)(struct notifier_block *nb, unsigned long event,
			void *data);
};

/*
 * parameters for renesas usbhs
 *
 * some register needs USB chip specific parameters.
 * This struct show it to driver
 */

struct renesas_usbhs_driver_pipe_config {
	u8 type;	/* USB_ENDPOINT_XFER_xxx */
	u16 bufsize;
	u8 bufnum;
	bool double_buf;
};
#define RENESAS_USBHS_PIPE(_type, _size, _num, _double_buf)	{	\
			.type = (_type),		\
			.bufsize = (_size),		\
			.bufnum = (_num),		\
			.double_buf = (_double_buf),	\
	}

struct renesas_usbhs_driver_param {
	/*
	 * pipe settings
	 */
	struct renesas_usbhs_driver_pipe_config *pipe_configs;
	int pipe_size; /* pipe_configs array size */

	/*
	 * option:
	 *
	 * for BUSWAIT :: BWAIT
	 * see
	 *	renesas_usbhs/common.c :: usbhsc_set_buswait()
	 * */
	int buswait_bwait;

	/*
	 * option:
	 *
	 * delay time from notify_hotplug callback
	 */
	int detection_delay; /* msec */

	/*
	 * option:
	 *
	 * dma id for dmaengine
	 * The data transfer direction on D0FIFO/D1FIFO should be
	 * fixed for keeping consistency.
	 * So, the platform id settings will be..
	 *	.d0_tx_id = xx_TX,
	 *	.d1_rx_id = xx_RX,
	 * or
	 *	.d1_tx_id = xx_TX,
	 *	.d0_rx_id = xx_RX,
	 */
	int d0_tx_id;
	int d0_rx_id;
	int d1_tx_id;
	int d1_rx_id;
	int d2_tx_id;
	int d2_rx_id;
	int d3_tx_id;
	int d3_rx_id;

	/*
	 * option:
	 *
	 * pio <--> dma border.
	 */
	int pio_dma_border; /* default is 64byte */

	/*
	 * option:
	 */
	u32 has_usb_dmac:1; /* for USB-DMAC */
	u32 runtime_pwctrl:1;
	u32 has_cnen:1;
	u32 cfifo_byte_addr:1; /* CFIFO is byte addressable */
#define USBHS_USB_DMAC_XFER_SIZE	32	/* hardcode the xfer size */
	u32 multi_clks:1;
	u32 has_new_pipe_configs:1;
};

/*
 * option:
 *
 * platform information for renesas_usbhs driver.
 */
struct renesas_usbhs_platform_info {
	/*
	 * option:
	 *
	 * platform set these functions before
	 * call platform_add_devices if needed
	 */
	struct renesas_usbhs_platform_callback	platform_callback;

	/*
	 * option:
	 *
	 * driver use these param for some register
	 */
	struct renesas_usbhs_driver_param	driver_param;
};

/*
 * macro for platform
 */
#define renesas_usbhs_get_info(pdev)\
	((struct renesas_usbhs_platform_info *)(pdev)->dev.platform_data)
#endif /* RENESAS_USB_H */
