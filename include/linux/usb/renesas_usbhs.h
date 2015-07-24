/*
 * Renesas USB
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef RENESAS_USB_H
#define RENESAS_USB_H
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
 * callback functions table for driver
 *
 * These functions are called from platform for driver.
 * Callback function's pointer will be set before
 * renesas_usbhs_platform_callback :: hardware_init was called
 */
struct renesas_usbhs_driver_callback {
	int (*notify_hotplug)(struct platform_device *pdev);
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
};

/*
 * parameters for renesas usbhs
 *
 * some register needs USB chip specific parameters.
 * This struct show it to driver
 */
struct renesas_usbhs_driver_param {
	/*
	 * pipe settings
	 */
	u32 *pipe_type; /* array of USB_ENDPOINT_XFER_xxx (from ep0) */
	int pipe_size; /* pipe_type array size */

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

	u32 type;
	u32 enable_gpio;

	/*
	 * option:
	 */
	u32 has_otg:1; /* for controlling PWEN/EXTLP */
	u32 has_sudmac:1; /* for SUDMAC */
	u32 has_usb_dmac:1; /* for USB-DMAC */
#define USBHS_USB_DMAC_XFER_SIZE	32	/* hardcode the xfer size */
};

#define USBHS_TYPE_RCAR_GEN2	1

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
	 * driver set these callback functions pointer.
	 * platform can use it on callback functions
	 */
	struct renesas_usbhs_driver_callback	driver_callback;

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

#define renesas_usbhs_call_notify_hotplug(pdev)				\
	({								\
		struct renesas_usbhs_driver_callback *dc;		\
		dc = &(renesas_usbhs_get_info(pdev)->driver_callback);	\
		if (dc && dc->notify_hotplug)				\
			dc->notify_hotplug(pdev);			\
	})
#endif /* RENESAS_USB_H */
